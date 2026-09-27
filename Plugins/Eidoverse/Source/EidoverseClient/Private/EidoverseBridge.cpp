#include "EidoverseBridge.h"
#include "EidoverseUI.h"
#include "EidoverseSettings.h"
#include "Components/WidgetComponent.h"
#include "Misc/ConfigCacheIni.h"
#include "EidoverseScene.h"
#include "Blueprint/UserWidget.h"
#include "HAL/IConsoleManager.h"
#include "EidoverseAvatar.h"
#include "EidoverseScene.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "SurfaceBoardingPlatform.h"
#include "UObject/ConstructorHelpers.h"
#include "EidoversePanel.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/SConstraintCanvas.h"

namespace Eido
{
    TArray<TSharedPtr<FJsonValue>> Numbers(std::initializer_list<double> Values)
    { TArray<TSharedPtr<FJsonValue>> Out;for(double V:Values)Out.Add(MakeShared<FJsonValueNumber>(V));return Out; }
    bool Vector(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,int32 Size,TArray<double>& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
        if(!O||!O->TryGetArrayField(Key,A)||A->Num()!=Size)return false;
        for(auto& V:*A){double N;if(!V||!V->TryGetNumber(N)||!FMath::IsFinite(N)||FMath::Abs(N)>1e7)return false;Out.Add(N);}return true;
    }
    FString String(const TSharedPtr<FJsonObject>& O,const TCHAR* Key)
    { FString S;if(O)O->TryGetStringField(Key,S);return S; }
    // Dispatch through a weak UObject + connection generation. Old sockets cannot
    // resurrect a peer or mutate the next PIE world after disconnect/reconnect.
    template<typename F> void Dispatch(TWeakObjectPtr<AEidoverseBridge> Owner,F Work)
    { AsyncTask(ENamedThreads::GameThread,[Owner,Work](){if(Owner.IsValid())Work(*Owner.Get());}); }
}

AEidoverseBridge::AEidoverseBridge()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    DiverProxyMesh=Sphere.Object;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sub(TEXT("/Game/Underwater/Vessels/Meshes/SM_Submarine_Shell.SM_Submarine_Shell"));
    SubmarineProxyMesh=Sub.Object;
}
void AEidoverseBridge::BeginPlay(){Super::BeginPlay();if(bAutoConnect)Connect();}
void AEidoverseBridge::EndPlay(const EEndPlayReason::Type Reason)
{
    CloseChat();
    if(HUD){HUD->RemoveFromParent();HUD=nullptr;}
    if(ChatOverlay&&GetWorld()&&GetWorld()->GetGameViewport())GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(ChatOverlay.ToSharedRef());
    ChatOverlay.Reset();ChatPanel.Reset();Disconnect();LogoutProduction();Super::EndPlay(Reason);
}
void AEidoverseBridge::OpenChat(){if(HUD)HUD->FocusChat();if(ChatPanel)ChatPanel->Open();}
void AEidoverseBridge::CloseChat(){if(ChatPanel)ChatPanel->Close();}
TArray<FString> AEidoverseBridge::GetParticipants() const{TArray<FString> Out;Peers.GetKeys(Out);return Out;}
bool AEidoverseBridge::Emote(const FString& Name)
{
    if(!bJoined||bSpectator||Name.IsEmpty()||Name.Len()>32)return false;
    for(TCHAR C:Name)if(!FChar::IsAlnum(C)&&C!='_'&&C!='-')return false;
    PendingEmote=Name;return true;
}
const TCHAR* AEidoverseBridge::ProductionEndpoint=TEXT("wss://eidoverse.animalabs.ai/ws");
namespace { const TCHAR* ClientSection=TEXT("/Script/EidoverseClient.ClientSettings"); }
void AEidoverseBridge::LoadClientSettings()
{
    // User-local and non-secret only: server, world, display name.
    FString Value;
    Endpoint=GConfig->GetString(ClientSection,TEXT("Server"),Value,GGameUserSettingsIni)&&!Value.IsEmpty()?Value:FString(ProductionEndpoint);
    WorldName=GConfig->GetString(ClientSection,TEXT("World"),Value,GGameUserSettingsIni)&&IsWorldName(Value)?Value:FString(TEXT("commons"));
    if(GConfig->GetString(ClientSection,TEXT("Name"),Value,GGameUserSettingsIni)&&!Value.IsEmpty())ParticipantName=Value.Left(64);
    AvatarLibraryBase=HttpBase()+TEXT("/library/");
}
void AEidoverseBridge::SaveClientSettings() const
{
    GConfig->SetString(ClientSection,TEXT("Server"),*Endpoint,GGameUserSettingsIni);
    GConfig->SetString(ClientSection,TEXT("World"),*WorldName,GGameUserSettingsIni);
    GConfig->SetString(ClientSection,TEXT("Name"),*ParticipantName,GGameUserSettingsIni);
    GConfig->Flush(false,GGameUserSettingsIni);
}
bool AEidoverseBridge::JoinFromDoor(const FString& Server,const FString& World,const FString& Name)
{
    const FString S=Server.TrimStartAndEnd(),W=World.TrimStartAndEnd(),N=Name.TrimStartAndEnd().Left(64);
    if(!IsWorldName(W)){ConnectionStatus=TEXT("World names are letters, numbers, - and _.");return false;}
    if(N.IsEmpty()&&S!=ProductionEndpoint){ConnectionStatus=TEXT("Choose a name first.");return false;}
    Disconnect();
    bRenderDynamicWorld=true;
    if(S==ProductionEndpoint)
    {
        if(!HasProductionSession()){LoginStatus=TEXT("The public server needs a Discord sign-in first.");return false;}
        WorldName=W;SaveClientSettings();JoinProduction();return true;
    }
    Endpoint=S;WorldName=W;ParticipantName=N;AvatarLibraryBase=HttpBase()+TEXT("/library/");
    SaveClientSettings();
    Connect();
    return ConnectionStatus!=TEXT("offline")&&!ConnectionStatus.StartsWith(TEXT("refused"));
}
bool AEidoverseBridge::SendTyping(const FString& To)
{
    if(!bJoined||bSpectator)return false;
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("type"),TEXT("typing"));
    if(!To.IsEmpty()&&To.Len()<=64)J->SetStringField(TEXT("to"),To);
    Send(J);return true;
}
FString AEidoverseBridge::HttpBase() const
{
    FString Base=Endpoint;
    if(Base.StartsWith(TEXT("wss://")))Base=TEXT("https://")+Base.Mid(6);
    else if(Base.StartsWith(TEXT("ws://")))Base=TEXT("http://")+Base.Mid(5);
    else return FString();
    Base.RemoveFromEnd(TEXT("/ws"));
    return Base;
}
#if WITH_DEV_AUTOMATION_TESTS
void AEidoverseBridge::ProbeLocalChatUI(bool bPrivate){if(ChatPanel)ChatPanel->ProbeLocalUI(bPrivate);}
#endif
void AEidoverseBridge::AddChatLine(const FString& Sender,const FString& Text,bool bPrivate)
{
    FEidoverseChatLine Line;Line.Sender=Sender.Left(64);Line.Text=Text.Left(4000);Line.bPrivate=bPrivate;
    ChatHistory.Add(MoveTemp(Line));if(ChatHistory.Num()>150)ChatHistory.RemoveAt(0,ChatHistory.Num()-150);
    if(ChatPanel){ChatPanel->NotifyChatLine(Sender,Text,bPrivate);ChatPanel->RefreshHistory();}
    if(HUD)HUD->NotifyChatLine(Line.Sender.IsEmpty()?Sender:Sender.Left(64),Text.Left(4000),bPrivate);
    if(!bPrivate&&EidoSettings::Bubbles())if(auto* Peer=Peers.Find(Sender))if(auto* Plate=Peer->Plate.Get())
        if(auto* W=Cast<UEidoverseNameplate>(Plate->GetUserWidgetObject()))W->ShowBubble(Text);
    OnChat.Broadcast(Sender,Text);
}
void AEidoverseBridge::Status(const FString& Value)
{
    ConnectionStatus=Value;
    UE_LOG(LogTemp,Display,TEXT("EIDOVERSE %s"),*Value);
    // The UMG shell reports status itself (door, system lines, toasts).
    if(GEngine&&!HUD)GEngine->AddOnScreenDebugMessage(uint64(GetUniqueID()),6,FColor::Cyan,TEXT("Eidoverse: ")+Value);
}
void AEidoverseBridge::ClearPeers()
{
    for(auto& P:Peers)if(P.Value.Actor.IsValid())P.Value.Actor->Destroy();Peers.Empty();RemoteCount=0;LoadedAvatarCount=0;
    for(auto& A:AvatarCache)if(A.Value)A.Value->Cancel();AvatarCache.Empty();
}
void AEidoverseBridge::Disconnect()
{
    ClearInteractions();
    bWanted=false;bJoined=false;++Generation;RetryAt=0;bCaptureAdvertised=false;
    if(Socket){Socket->OnConnected().Clear();Socket->OnMessage().Clear();Socket->OnClosed().Clear();Socket->OnConnectionError().Clear();Socket->Close();Socket.Reset();}
    ClearPeers();EndDynamicScene();ConnectionStatus=TEXT("offline");AssignedIdentity.Empty();
}
void AEidoverseBridge::Connect()
{
    Disconnect();
    ChatHistory.Empty();if(ChatPanel)ChatPanel->RefreshHistory();
    // Development cleartext is loopback-only. Do not leak credentials in URL,
    // diagnostics, levels or a non-TLS connection to another host.
    const bool Local=Endpoint.StartsWith(TEXT("ws://127.0.0.1:"))||Endpoint.StartsWith(TEXT("ws://localhost:"));
    if((!Local&&!Endpoint.StartsWith(TEXT("wss://")))||Endpoint.Contains(TEXT("?"))||Endpoint.Contains(TEXT("@")))
    {Status(TEXT("refused endpoint: use wss://host/ws or loopback ws://host:port/ws, without URL credentials"));return;}
    if(!IsWorldName(WorldName)||ParticipantName.IsEmpty()){Status(TEXT("world name must be 1–64 letters, numbers, hyphens or underscores"));return;}
    const bool ProductionLogin=Endpoint==TEXT("wss://eidoverse.animalabs.ai/ws")&&HasProductionSession();
    bLocalCommonsPreset=bLocalCommonsPreset&&Endpoint==TEXT("ws://127.0.0.1:19002/ws");
    if(!ProductionLogin&&!bLocalCommonsPreset&&JoinToken.IsEmpty())JoinToken=FPlatformMisc::GetEnvironmentVariable(TEXT("EIDOVERSE_JOIN_TOKEN"));
    if(!ProductionLogin&&!bLocalCommonsPreset&&IdentityToken.IsEmpty())IdentityToken=FPlatformMisc::GetEnvironmentVariable(TEXT("EIDOVERSE_IDENTITY_TOKEN"));
    bWanted=true;OpenedAt=FPlatformTime::Seconds();AssignedIdentity.Empty();LastFrameTime=-1;LastLogSequence=-1;
    TMap<FString,FString> Headers;if(ProductionLogin)Headers.Add(TEXT("Cookie"),TEXT("ew_sess=")+ProductionSession);
    Socket=FWebSocketsModule::Get().CreateWebSocket(Endpoint,TEXT(""),Headers);
    Socket->SetTextMessageMemoryLimit(8*1024*1024);
    const uint32 G=Generation;TWeakObjectPtr<AEidoverseBridge> Weak(this);
    Socket->OnConnected().AddLambda([Weak,G](){Eido::Dispatch(Weak,[G](AEidoverseBridge& B){
        if(B.Generation!=G)return;
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("type"),TEXT("join"));J->SetStringField(TEXT("world"),B.WorldName);
        J->SetStringField(TEXT("id"),B.ParticipantName);J->SetStringField(TEXT("avatar"),B.AvatarPath);J->SetBoolField(TEXT("spectate"),B.bSpectator);
        if(!B.HasProductionSession()||B.Endpoint!=TEXT("wss://eidoverse.animalabs.ai/ws"))
        {
            if(!B.JoinToken.IsEmpty())J->SetStringField(TEXT("token"),B.JoinToken);
            if(!B.IdentityToken.IsEmpty())J->SetStringField(TEXT("agentToken"),B.IdentityToken);
        }
        B.Send(J);B.Status(TEXT("joining"));});});
    Socket->OnMessage().AddLambda([Weak,G](const FString& M){if(M.Len()>8*1024*1024)return;Eido::Dispatch(Weak,[G,M](AEidoverseBridge& B){if(B.Generation==G)B.Receive(M);});});
    Socket->OnClosed().AddLambda([Weak,G](int32 Code,const FString&,bool){Eido::Dispatch(Weak,[G,Code](AEidoverseBridge& B){if(B.Generation==G)B.Lost(Code);});});
    Socket->OnConnectionError().AddLambda([Weak,G](const FString&){Eido::Dispatch(Weak,[G](AEidoverseBridge& B){if(B.Generation==G)B.Lost(1006);});});
    Status(TEXT("connecting"));Socket->Connect();
}
void AEidoverseBridge::Lost(int32 Code)
{
    ClearInteractions();
    bJoined=false;ClearPeers();EndDynamicScene();
    if(Code>=4000&&Code<4100){bWanted=false;RetryAt=0;Status(FString::Printf(TEXT("connection refused/taken over (%d); reconnect manually"),Code));}
    else if(bWanted&&bReconnect){RetryAt=FPlatformTime::Seconds()+RetryDelay;RetryDelay=FMath::Min(30.0,RetryDelay*2);Status(TEXT("disconnected; retry pending"));}
    else Status(TEXT("offline"));
}
void AEidoverseBridge::Send(const TSharedRef<FJsonObject>& Message)
{if(Socket&&Socket->IsConnected()){FString Text;FJsonSerializer::Serialize(Message,TJsonWriterFactory<>::Create(&Text));Socket->Send(Text);}}
bool AEidoverseBridge::Say(const FString& Text)
{
    const double Now=FPlatformTime::Seconds();
    if(!bJoined||!Socket||!Socket->IsConnected()||bSpectator||Text.TrimStartAndEnd().IsEmpty()||Text.Len()>4000||Now-LastChat<.4)return false;
    auto J=MakeShared<FJsonObject>(),A=MakeShared<FJsonObject>();A->SetStringField(TEXT("text"),Text);
    J->SetStringField(TEXT("type"),TEXT("verb"));J->SetStringField(TEXT("verb"),TEXT("say"));J->SetObjectField(TEXT("args"),A);
    Send(J);LastChat=Now;return true;
}
bool AEidoverseBridge::Whisper(const FString& To,const FString& Text)
{
    const double Now=FPlatformTime::Seconds();
    if(!bJoined||!Socket||!Socket->IsConnected()||bSpectator||To.TrimStartAndEnd().IsEmpty()||To.Len()>64||Text.TrimStartAndEnd().IsEmpty()||Text.Len()>4000||Now-LastChat<.4)return false;
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("type"),TEXT("whisper"));J->SetStringField(TEXT("to"),To);J->SetStringField(TEXT("text"),Text);
    Send(J);LastChat=Now;AddChatLine(TEXT("to ")+To,Text,true);return true;
}
void AEidoverseBridge::Announce(const FString& Id,const FString& Avatar)
{
    if(Id.IsEmpty()||Id==AssignedIdentity||Id.Len()>64)return;
    if(auto* Old=Peers.Find(Id)){if(Old->Actor.IsValid())Old->Actor->Destroy();Peers.Remove(Id);}
    if(Peers.Num()>=256)return;
    AActor* A=GetWorld()->SpawnActor<AActor>();if(!A)return;
    A->SetFlags(RF_Transient);A->SetActorEnableCollision(false);A->SetActorHiddenInGame(true);
    auto* Root=NewObject<USceneComponent>(A);A->SetRootComponent(Root);A->AddInstanceComponent(Root);Root->RegisterComponent();
    auto* Mesh=NewObject<UStaticMeshComponent>(A);A->AddInstanceComponent(Mesh);Mesh->SetupAttachment(Root);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCastShadow(false);Mesh->SetStaticMesh(DiverProxyMesh);Mesh->SetRelativeScale3D(FVector(.5,.5,1.7));Mesh->RegisterComponent();
    auto* Label=NewObject<UTextRenderComponent>(A);A->AddInstanceComponent(Label);Label->SetupAttachment(Root);Label->SetRelativeLocation(FVector(0,0,210));Label->SetWorldSize(16);Label->SetHorizontalAlignment(EHTA_Center);Label->SetVerticalAlignment(EVRTA_TextBottom);Label->SetCastShadow(false);Label->SetText(FText::FromString(Id));Label->RegisterComponent();
    FEidoversePeer P;P.Actor=A;P.Mesh=Mesh;P.Label=Label;
    if(HUD)
    {
        // The text label stays as the head anchor; the UMG pill is what shows.
        Label->SetText(FText::GetEmpty());
        auto* Plate=NewObject<UWidgetComponent>(A);A->AddInstanceComponent(Plate);Plate->SetupAttachment(Label);
        Plate->SetWidgetSpace(EWidgetSpace::Screen);Plate->SetDrawAtDesiredSize(true);Plate->SetPivot(FVector2D(.5,1));
        Plate->SetWidgetClass(UEidoverseNameplate::StaticClass());Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Plate->RegisterComponent();
        if(auto* W=Cast<UEidoverseNameplate>(Plate->GetUserWidgetObject()))W->SetName(Id);
        P.Plate=Plate;
    }
    RequestAvatar(P,Avatar);Peers.Add(Id,MoveTemp(P));RemoteCount=Peers.Num();
}
void AEidoverseBridge::RequestAvatar(FEidoversePeer& Peer,const FString& Path)
{
    Peer.AvatarPath=Path;Peer.AvatarAsset.Reset();
    FString Url;if(!bLoadRemoteAvatars||!UEidoverseAvatarAsset::ResolveUrl(AvatarLibraryBase,Path,Url))return;
    if(auto* Cached=AvatarCache.Find(Url)){Peer.AvatarAsset=*Cached;return;}
    // Bound unique resident assets. Evict unreferenced completed jobs first.
    if(AvatarCache.Num()>=16)
    {
        FString Evict;
        for(auto& Entry:AvatarCache)
        {
            bool Used=false;for(auto& P:Peers)if(P.Value.AvatarAsset.Get()==Entry.Value){Used=true;break;}
            if(!Used&&Entry.Value->State!=UEidoverseAvatarAsset::EState::Loading){Evict=Entry.Key;break;}
        }
        if(Evict.IsEmpty())return;AvatarCache.Remove(Evict);
    }
    auto* Asset=NewObject<UEidoverseAvatarAsset>();Asset->Url=Url;AvatarCache.Add(Url,Asset);Peer.AvatarAsset=Asset;
}
void AEidoverseBridge::TickAvatars(double Time)
{
    struct FLampCandidate{UEidoverseAvatarMesh* Mesh;double Distance;bool bVisible;};
    TArray<FLampCandidate> Lamps;
    FVector Eye=OriginCentimeters;FRotator EyeRotation;
    if(auto* PC=UGameplayStatics::GetPlayerController(this,0))PC->GetPlayerViewPoint(Eye,EyeRotation);
    bool Loading=false;for(auto& A:AvatarCache)if(A.Value->State==UEidoverseAvatarAsset::EState::Loading){Loading=true;break;}
    if(!Loading)for(auto& A:AvatarCache)if(A.Value->State==UEidoverseAvatarAsset::EState::Queued){A.Value->Start();break;}
    LoadedAvatarCount=0;
    for(auto& Pair:Peers)
    {
        auto& P=Pair.Value;auto* Asset=P.AvatarAsset.Get();
        if(Asset&&Asset->State==UEidoverseAvatarAsset::EState::Ready&&P.Actor.IsValid())
        {
            if(!P.AvatarMesh.IsValid())
            {
                auto* M=NewObject<UEidoverseAvatarMesh>(P.Actor.Get());P.Actor->AddInstanceComponent(M);
                M->SetupAttachment(P.Actor->GetRootComponent());M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->RegisterComponent();P.AvatarMesh=M;
            }
            if(P.AvatarMesh->Avatar!=Asset)P.AvatarMesh->SetAvatar(Asset);
        }
        if(P.AvatarMesh.IsValid())
        {
            ++LoadedAvatarCount;
            P.AvatarMesh->SetVisibility(!P.bSubmarine,true);P.AvatarMesh->SetRelativeLocation(FVector(0,0,P.bPawnAnchor?-85:0));
            if(!P.bSubmarine)
            {
                const bool Fresh=!P.Samples.IsEmpty()&&Time-P.Samples.Last().Time<.75;
                const FVector LocalVelocity=Fresh?P.Actor->GetActorQuat().UnrotateVector(P.VelocityMeters):FVector::ZeroVector;
                P.AvatarMesh->Animate(P.Mode,Fresh?P.Speed:0,LocalVelocity,Time);
                if(P.Speed>.05)P.EmoteUntil=0;
                if(Time<P.EmoteUntil)P.AvatarMesh->ApplyGesture(P.ActiveEmote,Time);
            }
            if(P.Mesh.IsValid())P.Mesh->SetVisibility(P.bSubmarine);
            if(!P.AvatarMesh->EmissiveLights.IsEmpty())Lamps.Add({P.AvatarMesh.Get(),FVector::DistSquared(Eye,P.Actor->GetActorLocation()),!P.bSubmarine});
        }
    }
    Lamps.StableSort([](const FLampCandidate& A,const FLampCandidate& B){return A.Distance<B.Distance;});
    int32 Budget=4; // Real shadowed lights, not one unbounded light per remote user.
    const float Day=DynamicScene?FMath::Clamp(FMath::Sin((DynamicScene->WeatherHour-6)*PI/12)*3.f,0.f,1.f):0;
    for(const auto& Candidate:Lamps)
    {
        const int32 Count=Candidate.Mesh->EmissiveLights.Num();
        const bool Enable=Candidate.bVisible&&Candidate.Distance<1200.*1200.&&Count<=Budget;
        if(Enable)Budget-=Count;
        Candidate.Mesh->UpdateEmissiveLights(Time,Day,Enable);
    }
}
void AEidoverseBridge::Pose(const FString& Id,const TSharedPtr<FJsonObject>& Data,double Time)
{
    auto* Peer=Peers.Find(Id);if(!Peer||!Peer->Actor.IsValid())return; // roster authority, never create from a frame
    const TSharedPtr<FJsonObject>* Reach=nullptr;Peer->Reach=Data->TryGetObjectField(TEXT("reach"),Reach)?*Reach:nullptr;
    const FString Emote=Eido::String(Data,TEXT("emote"));
    if(!Emote.IsEmpty()&&Emote!=Peer->LastEmote){Peer->ActiveEmote=Emote;Peer->EmoteUntil=Time+2.5;}
    Peer->LastEmote=Emote;
    TArray<double> P;if(!Eido::Vector(Data,TEXT("p"),3,P))return;
    FEidoverseSample Sample;Sample.Time=Time;Sample.Position=OriginCentimeters+FromWirePosition(FVector(P[0],P[1],P[2]));
    double Yaw=0;if(Data->HasField(TEXT("yaw"))&&(!Data->TryGetNumberField(TEXT("yaw"),Yaw)||!FMath::IsFinite(Yaw)))return;
    Sample.Orientation=FromWireRotation(FQuat(FVector::YAxisVector,Yaw));
    TArray<double> Q;if(Data->HasField(TEXT("q")))
    {if(!Eido::Vector(Data,TEXT("q"),4,Q))return;FQuat Rotation(Q[0],Q[1],Q[2],Q[3]);if(Rotation.SizeSquared()<.0001)return;Sample.Orientation=FromWireRotation(Rotation.GetNormalized());}
    const TSharedPtr<FJsonObject>* Loc=nullptr;FString Body;
    if(Data->TryGetObjectField(TEXT("locomotion"),Loc)){Peer->Mode=Eido::String(*Loc,TEXT("mode")).Left(32);Body=Eido::String(*Loc,TEXT("body"));}
    else Peer->Mode=Eido::String(Data,TEXT("clip"));
    double Speed=0;Data->TryGetNumberField(TEXT("speed"),Speed);Peer->Speed=FMath::IsFinite(Speed)?FMath::Clamp(float(Speed),0.f,10.f):0;
    Peer->bPawnAnchor=Body==TEXT("diver")||Body==TEXT("submarine");
    const bool Sub=Body==TEXT("submarine");
    if(Sub!=Peer->bSubmarine&&Peer->Mesh.IsValid())
    {Peer->bSubmarine=Sub;Peer->Mesh->SetStaticMesh(Sub&&SubmarineProxyMesh?SubmarineProxyMesh:DiverProxyMesh);Peer->Mesh->SetRelativeScale3D(Sub?FVector(1):FVector(.5,.5,1.7));}
    if(Peer->Samples.Num()&&Time<=Peer->Samples.Last().Time)return;
    Peer->VelocityMeters=FVector::ZeroVector;
    if(!Peer->Samples.IsEmpty())
    {
        const auto& Previous=Peer->Samples.Last();const double Dt=Time-Previous.Time;
        const FVector Delta=Sample.Position-Previous.Position;
        // Do not turn a reconnect, teleport, or long network stall into a dive.
        if(Dt>=.01&&Dt<.5&&Delta.Size()<1000)Peer->VelocityMeters=(Delta/(100*Dt)).GetClampedToMaxSize(6);
    }
    Peer->Samples.Add(Sample);if(Peer->Samples.Num()>12)Peer->Samples.RemoveAt(0);
    Peer->Actor->SetActorHiddenInGame(false);
}
void AEidoverseBridge::Receive(const FString& Message)
{
    TSharedPtr<FJsonObject> J;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Message),J)||!J)return;
    FString Type=Eido::String(J,TEXT("type"));const double Now=FPlatformTime::Seconds();
    if(Type==TEXT("snapshot"))
    {
        AssignedIdentity=Eido::String(J,TEXT("you"));if(AssignedIdentity.IsEmpty())return;
        ClearPeers();LastFrameTime=-1;bJoined=true;RetryDelay=1;RetryAt=0;
        double SceneVersion=0;bSceneSupported=J->TryGetNumberField(TEXT("renderSceneVersion"),SceneVersion)&&SceneVersion==1;
        if(bRenderDynamicWorld&&bSceneSupported)BeginDynamicScene();
        else if(bRenderDynamicWorld)WorldRenderStatus=TEXT("Server needs the dynamic-scene update. Local prototype remains active.");
        const TSharedPtr<FJsonObject>* Sim=nullptr;const TSharedPtr<FJsonObject>* Epoch=nullptr;
        bDeterministicScenePhysics=J->TryGetObjectField(TEXT("sim"),Sim)&&(*Sim)->TryGetObjectField(TEXT("epoch"),Epoch);
        AdvertiseCapture();
        double Seq;if(J->TryGetNumberField(TEXT("throughSeq"),Seq))LastLogSequence=int64(Seq);
        const TArray<TSharedPtr<FJsonValue>>* History=nullptr;
        if(J->TryGetArrayField(TEXT("entries"),History))for(auto& V:*History){const TSharedPtr<FJsonObject>* E=nullptr;double S;if(V->TryGetObject(E)&&(*E)->TryGetNumberField(TEXT("seq"),S))LastLogSequence=FMath::Max(LastLogSequence,int64(S));}
        const TArray<TSharedPtr<FJsonValue>>* Present=nullptr;
        if(J->TryGetArrayField(TEXT("present"),Present))for(auto& V:*Present)
        {const TSharedPtr<FJsonObject>* Person=nullptr;if(!V->TryGetObject(Person))continue;const FString Id=Eido::String(*Person,TEXT("id"));Announce(Id,Eido::String(*Person,TEXT("avatar")));const TSharedPtr<FJsonObject>* P=nullptr;if((*Person)->TryGetObjectField(TEXT("pose"),P))Pose(Id,*P,Now);}
        Status(TEXT("joined ")+WorldName+TEXT(" as ")+AssignedIdentity);
        bool Recording=false;if(J->TryGetBoolField(TEXT("recording"),Recording)&&Recording)Status(TEXT("joined; server is recording movement and chat"));
    }
    else if(Type==TEXT("render-scene")&&bJoined)ReceiveDynamicScene(J);
    else if(Type==TEXT("lease")&&bJoined)ReceiveLease(J);
    else if(Type==TEXT("snap")&&bJoined)ReceiveCapture(J);
    else if(Type==TEXT("arrive")&&bJoined)Announce(Eido::String(J,TEXT("id")),Eido::String(J,TEXT("avatar")));
    else if(Type==TEXT("avatar-updated")&&bJoined)
    {
        const FString Path=Eido::String(J,TEXT("path"));double Version;
        if(!J->TryGetNumberField(TEXT("v"),Version)||!FMath::IsFinite(Version)||Version<0||Version>9e15)return;
        for(auto& P:Peers)
        {FString Base=P.Value.AvatarPath;int32 Query;if(Base.FindChar('?',Query))Base=Base.Left(Query);if(Base==Path)RequestAvatar(P.Value,Path+FString::Printf(TEXT("?v=%.0f"),Version));}
    }
    else if(Type==TEXT("leave"))
    {const FString Id=Eido::String(J,TEXT("id"));if(auto* P=Peers.Find(Id))if(P->Actor.IsValid())P->Actor->Destroy();Peers.Remove(Id);RemoteCount=Peers.Num();}
    else if(Type==TEXT("frame")&&bJoined)
    {
        double T;if(!J->TryGetNumberField(TEXT("t"),T)||!FMath::IsFinite(T)||T<=LastFrameTime)return;LastFrameTime=T;
        const TSharedPtr<FJsonObject>* Poses=nullptr;if(J->TryGetObjectField(TEXT("poses"),Poses))for(auto& P:(*Poses)->Values)
        {const TSharedPtr<FJsonObject>* O=nullptr;if(P.Value->TryGetObject(O))Pose(FString(P.Key),*O,Now);}
    }
    else if(Type==TEXT("log")&&bJoined)
    {
        const TSharedPtr<FJsonObject>* Entry=nullptr;if(!J->TryGetObjectField(TEXT("entry"),Entry))return;
        double Seq;if(!(*Entry)->TryGetNumberField(TEXT("seq"),Seq)||Seq<=LastLogSequence)return;LastLogSequence=int64(Seq);
        bSceneDirty=true;
        ReceiveInteraction(*Entry);
        if(Eido::String(*Entry,TEXT("verb"))!=TEXT("say"))return;
        const TSharedPtr<FJsonObject>* Args=nullptr;if(!(*Entry)->TryGetObjectField(TEXT("args"),Args))return;
        FString Sender=Eido::String(*Entry,TEXT("actor")),Text=Eido::String(*Args,TEXT("text")).Left(4000);AddChatLine(Sender,Text);
    }
    else if(Type==TEXT("typing")&&bJoined)
    {
        // Public composing, or a whisper being composed to us; others' DMs stay private.
        const FString Id=Eido::String(J,TEXT("id")),To=Eido::String(J,TEXT("to"));
        if(Id.IsEmpty()||Id==AssignedIdentity||(!To.IsEmpty()&&To!=AssignedIdentity))return;
        if(HUD)HUD->NoteTyping(Id);
        if(auto* Peer=Peers.Find(Id))if(auto* Plate=Peer->Plate.Get())if(auto* W=Cast<UEidoverseNameplate>(Plate->GetUserWidgetObject()))W->ShowTyping(4);
    }
    else if(Type==TEXT("whisper")&&bJoined)
    {FString Sender=Eido::String(J,TEXT("from")),Text=Eido::String(J,TEXT("text")).Left(4000);AddChatLine(Sender,Text,true);}
    else if(Type==TEXT("error"))Status(TEXT("server refused a request; check permissions/credentials")); // no credential-bearing payload logs
}
void AEidoverseBridge::SendPose()
{
    auto* Pawn=UGameplayStatics::GetPlayerPawn(this,0);if(!Pawn)return;
    const bool Sub=Pawn->ActorHasTag(TEXT("OceanSubmarine"));
    const bool Land=Pawn->IsA<AEidoverseExplorer>();
    const FVector P=ToWirePosition(Pawn->GetActorLocation()-OriginCentimeters-(Land?FVector(0,0,88):FVector::ZeroVector));
    const FQuat Q=ToWireRotation(Pawn->GetActorQuat().GetNormalized());
    const FVector F=ToWirePosition(Pawn->GetActorForwardVector());
    const double Speed=Pawn->GetVelocity().Size()/100.;
    if(P.ContainsNaN()||Q.ContainsNaN()||!FMath::IsFinite(Speed))return;
    const auto* Explorer=Cast<AEidoverseExplorer>(Pawn);
    FString Mode=Explorer?Explorer->LocomotionMode:Sub?TEXT("pilot"):TEXT("swim");
    const bool bSwimmer=Explorer&&Mode!=TEXT("walk");
    if(!Land&&Pawn->ActorHasTag(TEXT("SurfaceBoarded")))
    {
        Mode=TEXT("walk");
        for(TActorIterator<ASurfaceBoardingPlatform> It(GetWorld());It;++It)
            if(It->BoardingState==1||It->BoardingState==3){Mode=TEXT("climb");break;}
    }
    else if(!Land&&Pawn->GetActorLocation().Z>=SurfaceZ-80)Mode=TEXT("surface");
    auto Data=MakeShared<FJsonObject>(),Loc=MakeShared<FJsonObject>(),J=MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("p"),Eido::Numbers({P.X,P.Y,P.Z}));Data->SetArrayField(TEXT("q"),Eido::Numbers({Q.X,Q.Y,Q.Z,Q.W}));
    Data->SetNumberField(TEXT("yaw"),FMath::Atan2(F.X,F.Z));Data->SetNumberField(TEXT("speed"),Speed);
    Data->SetStringField(TEXT("clip"),Speed>.05?TEXT("walk"):TEXT("idle")); // legacy clients have no swim clip
    Loc->SetStringField(TEXT("mode"),Mode);Loc->SetStringField(TEXT("body"),bSwimmer?TEXT("diver"):Land?TEXT("avatar"):Sub?TEXT("submarine"):TEXT("diver"));
    Loc->SetStringField(TEXT("medium"),(Mode==TEXT("walk")||Mode==TEXT("climb"))?TEXT("air"):TEXT("water"));Data->SetObjectField(TEXT("locomotion"),Loc);
    if(LocalReach)Data->SetObjectField(TEXT("reach"),LocalReach);
    if(!PendingEmote.IsEmpty()){Data->SetStringField(TEXT("emote"),PendingEmote);PendingEmote.Empty();}
    J->SetStringField(TEXT("type"),TEXT("pose"));J->SetObjectField(TEXT("pose"),Data);Send(J);++SentPoseCount;
}
void AEidoverseBridge::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);const double Now=FPlatformTime::Seconds();
    TickProductionLogin(Now);
    if(!ProductionSession.IsEmpty()&&Now>=ProductionSessionExpires){LogoutProduction();LoginStatus=TEXT("Session expired. Please sign in again.");}
    static IConsoleVariable* LegacyUI=IConsoleManager::Get().RegisterConsoleVariable(TEXT("Eidoverse.UI.Legacy"),0,
        TEXT("1: use the original Slate chat/login panel instead of the UMG shell (for older test scripts)."),ECVF_Default);
    if(bShowChatUI&&!HUD&&!ChatPanel&&LegacyUI->GetInt()==0)
        if(auto* PC=UGameplayStatics::GetPlayerController(this,0))
        {
            HUD=CreateWidget<UEidoverseHUD>(PC,HUDClass?HUDClass.Get():UEidoverseHUD::StaticClass());
            HUD->Bridge=this;HUD->AddToViewport(20);HUD->SetPointerMode(false);
        }
    if(HUD)if(auto* Explorer=Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this,0)))
    {
        if(Explorer->LocomotionMode!=LastExplorerMode)
        {
            if(Explorer->LocomotionMode==TEXT("swim")&&LastExplorerMode==TEXT("walk"))HUD->Hint(TEXT("swimming \u2014 WASD move, Space rise, C / Ctrl dive, Shift faster"),5);
            LastExplorerMode=Explorer->LocomotionMode;
        }
    }
    if(bShowChatUI&&!HUD&&!ChatPanel&&GetWorld()->GetGameViewport())
    {
        SAssignNew(ChatPanel,SEidoversePanel).Bridge(this);
        ChatOverlay=SNew(SConstraintCanvas)
            +SConstraintCanvas::Slot().Anchors(FAnchors(0,1)).Alignment(FVector2D(0,1)).Offset(FMargin(24,-24,0,0)).AutoSize(true)[ChatPanel.ToSharedRef()];
        GetWorld()->GetGameViewport()->AddViewportWidgetContent(ChatOverlay.ToSharedRef(),20);
    }
    if(ChatPanel&&!HUD)if(auto* PC=UGameplayStatics::GetPlayerController(this,0))
        if(PC->WasInputKeyJustPressed(EKeys::Enter))OpenChat(); // F2 is Unreal's Unlit debug binding.
    if(bWanted&&!bJoined&&RetryAt==0&&Now-OpenedAt>15){Lost(1006);}
    if(bWanted&&RetryAt>0&&Now>=RetryAt){Connect();return;}
    if(bJoined&&!bSpectator&&Now-LastSend>=1./15){LastSend=Now;SendPose();}
    if(bJoined)TickAvatars(Now);
    if(bJoined)TickDynamicScene(Now);
    if(bJoined)TickInteractions(DeltaSeconds,Now);
    const double RenderTime=Now-.11;
    for(auto& Pair:Peers)
    {
        auto& P=Pair.Value;if(!P.Actor.IsValid()||P.Samples.IsEmpty())continue;
        while(P.Samples.Num()>2&&P.Samples[1].Time<RenderTime)P.Samples.RemoveAt(0);
        const auto& A=P.Samples[0];const auto& B=P.Samples.Num()>1?P.Samples[1]:A;
        const double Alpha=B.Time>A.Time?FMath::Clamp((RenderTime-A.Time)/(B.Time-A.Time),0.,1.):1.;
        P.Actor->SetActorLocationAndRotation(FMath::Lerp(A.Position,B.Position,Alpha),FQuat::Slerp(A.Orientation,B.Orientation,Alpha));
        if(P.Label.IsValid())
        {
            // Feet-anchored avatars are not all 1.7 m tall; use the posed head,
            // not the protocol root. World-up clearance also works when diving.
            FVector Anchor;
            if(!P.bSubmarine&&P.AvatarMesh.IsValid()&&P.AvatarMesh->HumanBonePosition(TEXT("head"),Anchor))Anchor.Z+=40;
            else
            {
                UPrimitiveComponent* Body=!P.bSubmarine&&P.AvatarMesh.IsValid()?static_cast<UPrimitiveComponent*>(P.AvatarMesh.Get()):P.Mesh.Get();
                const FBoxSphereBounds Bounds=Body?Body->CalcBounds(Body->GetComponentTransform()):FBoxSphereBounds(P.Actor->GetActorLocation(),FVector(50,50,85),110);
                Anchor=Bounds.Origin+FVector(0,0,Bounds.BoxExtent.Z+30);
            }
            P.Label->SetWorldLocation(Anchor);
            if(auto* Plate=P.Plate.Get())if(auto* PC=UGameplayStatics::GetPlayerController(this,0))
            {
                // Legible nearby, fading out by 70 m (screen-space pills never shrink).
                FVector Camera;FRotator Rot;PC->GetPlayerViewPoint(Camera,Rot);
                const float Metres=FVector::Dist(Camera,Anchor)/100;
                Plate->SetVisibility(Metres<70&&EidoSettings::Nameplates());
                if(auto* W=Cast<UEidoverseNameplate>(Plate->GetUserWidgetObject()))W->SetDistanceFade(FMath::GetMappedRangeValueClamped(FVector2D(40,70),FVector2D(1,0),Metres));
            }
            if(auto* PC=UGameplayStatics::GetPlayerController(this,0))
            {FVector Camera;FRotator Rot;PC->GetPlayerViewPoint(Camera,Rot);P.Label->SetWorldRotation((Camera-Anchor).Rotation());}
        }
    }
    if(bJoined)TickReaches();
}

namespace
{
#if WITH_DEV_AUTOMATION_TESTS
    FAutoConsoleCommand ChatProbe(TEXT("Eidoverse.TestChatUI"),TEXT("Loopback fixture only: exercise native text entry and send."),
        FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args){
            if(!GEngine)return;
            for(const auto& Context:GEngine->GetWorldContexts())if(Context.World()&&Context.World()->IsGameWorld())
                for(TActorIterator<AEidoverseBridge> It(Context.World());It;++It)It->ProbeLocalChatUI(Args.Num()>0&&Args[0]==TEXT("private"));
        }));
#endif
    AEidoverseBridge* Bridge(UWorld* World)
    {if(!World||!World->IsGameWorld())return nullptr;TActorIterator<AEidoverseBridge> It(World);return It?*It:nullptr;}
    FAutoConsoleCommandWithWorldAndArgs EidoConnect(TEXT("Eidoverse.Connect"),TEXT("Connect [ws(s)://host:port/ws world participant]. Tokens come from environment, never arguments."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A,UWorld* W){
            if(auto* B=Bridge(W)){if(A.Num()>0)B->Endpoint=A[0];if(A.Num()>1)B->WorldName=A[1];if(A.Num()>2)B->ParticipantName=A[2];B->Connect();}
        }));
    FAutoConsoleCommandWithWorldAndArgs EidoDisconnect(TEXT("Eidoverse.Disconnect"),TEXT("Disconnect and remove remote proxies."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&,UWorld* W){if(auto* B=Bridge(W))B->Disconnect();}));
    FAutoConsoleCommandWithWorldAndArgs EidoSay(TEXT("Eidoverse.Say"),TEXT("Say <text>. Public, persisted by the server only when echoed."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A,UWorld* W){if(auto* B=Bridge(W))B->Say(FString::Join(A,TEXT(" ")));}));
    FAutoConsoleCommandWithWorldAndArgs EidoWhisper(TEXT("Eidoverse.Whisper"),TEXT("Whisper <participant> <text>. Private, not a world-log verb."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A,UWorld* W){if(A.Num()<2)return;TArray<FString> Text=A;Text.RemoveAt(0);if(auto* B=Bridge(W))B->Whisper(A[0],FString::Join(Text,TEXT(" ")));}));
}
