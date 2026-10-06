#include "EidoverseBridge.h"
#include "EidoverseLoadTrace.h"
#include "EidoverseClips.h"
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
    PendingEmote=Name;SelfBody.ActiveEmote=Name;SelfBody.EmoteUntil=FPlatformTime::Seconds()+2.5;SelfBody.bEmotePending=true;return true;
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
    // Automated runs (-unattended) and command-line joins must not overwrite the
    // person's remembered server, world and name at the front door.
    if(FApp::IsUnattended()||FParse::Param(FCommandLine::Get(),TEXT("EidoverseJoin"))||FCommandLine::Get()&&FCString::Strifind(FCommandLine::Get(),TEXT("-EidoverseJoin=")))return;
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
        // Refusals go to the door's status line; LoginStatus keeps the pairing code visible.
        if(!HasProductionSession())
        {
            ConnectionStatus=bAuthenticating?TEXT("Finish the Discord sign-in in your browser first (the code is shown below), then join."):TEXT("The public server needs a Discord sign-in first.");
            return false;
        }
        if(!LoginWorldScope.IsEmpty()&&LoginWorldScope!=TEXT("*")&&LoginWorldScope!=W)
        {ConnectionStatus=TEXT("This sign-in only approved '")+LoginWorldScope+TEXT("'. Sign out, sign in again and approve all accessible worlds.");return false;}
        WorldName=W;SaveClientSettings();JoinProduction();return true;
    }
    Endpoint=S;WorldName=W;ParticipantName=N;AvatarLibraryBase=HttpBase()+TEXT("/library/");
    SaveClientSettings();
    Connect();
    return ConnectionStatus!=TEXT("offline")&&!ConnectionStatus.StartsWith(TEXT("refused"));
}
bool AEidoverseBridge::RequestInspect(const FString& Id)
{
    if(!bJoined||Id.IsEmpty()||Id.Len()>128)return false;
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("type"),TEXT("inspect"));J->SetStringField(TEXT("id"),Id);
    Send(J);return true;
}
FString AEidoverseBridge::GetInspectedComponents() const
{
    const TSharedPtr<FJsonObject>* Comp=nullptr;
    if(!InspectReply||!InspectReply->TryGetObjectField(TEXT("comp"),Comp))return FString();
    FString Out;auto W=TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    FJsonSerializer::Serialize((*Comp).ToSharedRef(),W);return Out;
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
void AEidoverseBridge::AddChatLine(const FString& Sender,const FString& Text,bool bPrivate,const FString& Utt,bool bSpoken)
{
    // The continuation of one spoken utterance (spoken:true, same author + utt) is one line.
    if(!Utt.IsEmpty()&&Utt==LastSpokenUtt&&Sender==LastSpokenSender&&!ChatHistory.IsEmpty()&&ChatHistory.Last().Sender==Sender.Left(64))
    {
        ChatHistory.Last().Text=(ChatHistory.Last().Text+TEXT(" ")+Text).Left(8000);
        if(ChatPanel)ChatPanel->RefreshHistory();
        if(HUD)HUD->AppendToLastChatLine(Sender.Left(64),Text.Left(4000));
        OnChat.Broadcast(Sender,Text);
        return;
    }
    LastSpokenUtt=Utt;LastSpokenSender=Utt.IsEmpty()?FString():Sender;
    FEidoverseChatLine Line;Line.Sender=Sender.Left(64);Line.Text=Text.Left(4000);Line.bPrivate=bPrivate;
    ChatHistory.Add(MoveTemp(Line));if(ChatHistory.Num()>150)ChatHistory.RemoveAt(0,ChatHistory.Num()-150);
    if(ChatPanel){ChatPanel->NotifyChatLine(Sender,Text,bPrivate);ChatPanel->RefreshHistory();}
    if(HUD)HUD->NotifyChatLine(Line.Sender.IsEmpty()?Sender:Sender.Left(64),Text.Left(4000),bPrivate);
    if(!bPrivate&&!bSpoken&&EidoSettings::Bubbles())if(auto* Peer=Peers.Find(Sender))if(auto* Plate=Peer->Plate.Get())
        if(auto* W=Cast<UEidoverseNameplate>(Plate->GetUserWidgetObject()))W->ShowBubble(Text,FMath::Clamp(2.5f+Text.Len()*.045f,5.f,22.f));
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
    if(SelfBody.AvatarMesh.IsValid())SelfBody.AvatarMesh->DestroyComponent();if(SelfBody.Mesh.IsValid())SelfBody.Mesh->DestroyComponent();SelfBody=FEidoversePeer();
    for(auto& A:AvatarCache)if(A.Value)A.Value->Cancel();AvatarCache.Empty();
}
void AEidoverseBridge::Disconnect()
{
    if(!bRetrying)QueuedMessages.Reset();
    ClearInteractions();VoiceLost();
    bWanted=false;bJoined=false;++Generation;RetryAt=0;bCaptureAdvertised=false;
    if(Socket){Socket->OnConnected().Clear();Socket->OnMessage().Clear();Socket->OnClosed().Clear();Socket->OnConnectionError().Clear();Socket->Close();Socket.Reset();}
    ClearPeers();EndDynamicScene();ConnectionStatus=TEXT("offline");AssignedIdentity.Empty();
}
void AEidoverseBridge::Connect()
{
    Disconnect();
    // A retry after a dropped connection keeps the chat, queued actions and body state.
    if(!bRetrying)
    {
        ChatHistory.Empty();if(ChatPanel)ChatPanel->RefreshHistory();
        RenderedChatSeqs.Reset();OldestChatSeq=-1;bHistoryDone=false;bHistoryPending=false;LastWhisperFrom.Empty();
        bBodyMovedThisSession=false;PendingRestore.Reset();SelfHeldPose.Reset();RetryDelay=1.2;
    }
    RemoteMounts.Reset();
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
        B.SendJoin();B.Status(TEXT("joining"));});});
    Socket->OnMessage().AddLambda([Weak,G](const FString& M){if(M.Len()>8*1024*1024)return;Eido::Dispatch(Weak,[G,M](AEidoverseBridge& B){if(B.Generation==G)B.Receive(M);});});
    Socket->OnClosed().AddLambda([Weak,G](int32 Code,const FString&,bool){Eido::Dispatch(Weak,[G,Code](AEidoverseBridge& B){if(B.Generation==G)B.Lost(Code);});});
    Socket->OnConnectionError().AddLambda([Weak,G](const FString&){Eido::Dispatch(Weak,[G](AEidoverseBridge& B){if(B.Generation==G)B.Lost(1006);});});
    Status(TEXT("connecting"));Socket->Connect();
}
void AEidoverseBridge::SendJoin()
{
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("type"),TEXT("join"));J->SetStringField(TEXT("world"),WorldName);
    J->SetStringField(TEXT("id"),ParticipantName);J->SetStringField(TEXT("avatar"),AvatarPath);J->SetBoolField(TEXT("spectate"),bSpectator);
    if(!HasProductionSession()||Endpoint!=ProductionEndpoint)
    {
        if(!JoinToken.IsEmpty())J->SetStringField(TEXT("token"),JoinToken);
        if(!IdentityToken.IsEmpty())J->SetStringField(TEXT("agentToken"),IdentityToken);
    }
    Send(J);
}
bool AEidoverseBridge::ChangeAvatar(const FString& Path)
{
    if(!bJoined||!Socket||!Socket->IsConnected()||Path.IsEmpty()||Path.Len()>256)return false;
    // A join on a joined socket is travel to the same world: the server re-announces
    // us wearing the new body. The rendered world stays; only presence is rebuilt.
    AvatarPath=Path;bRejoining=true;SendJoin();return true;
}
void AEidoverseBridge::ReadRights(const TSharedPtr<FJsonObject>& Rights)
{
    WorldRole.Empty();bWorldOpen=false;bCanGenerate=false;
    if(!Rights)return;
    Rights->TryGetStringField(TEXT("role"),WorldRole);
    Rights->TryGetBoolField(TEXT("open"),bWorldOpen);
    {bool bFly=false;RightsFly.Reset();if(Rights->TryGetBoolField(TEXT("fly"),bFly))RightsFly=bFly;}
    bCanGenerate=true;Rights->TryGetBoolField(TEXT("gen"),bCanGenerate);
}
bool AEidoverseBridge::SendVerb(const FString& Verb,const TSharedRef<FJsonObject>& Args)
{
    if(bSpectator||Verb.IsEmpty())return false;
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("type"),TEXT("verb"));J->SetStringField(TEXT("verb"),Verb);J->SetObjectField(TEXT("args"),Args);
    if(!bJoined||!Socket||!Socket->IsConnected())return QueueWhileReconnecting(J,Verb);
    Send(J);return true;
}
bool AEidoverseBridge::SendVerbJson(const FString& Verb,const FString& ArgsJson)
{
    TSharedPtr<FJsonObject> Args;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ArgsJson),Args)||!Args)return false;
    return SendVerb(Verb,Args.ToSharedRef());
}
void AEidoverseBridge::Lost(int32 Code)
{
    ClearInteractions();VoiceLost();
    bJoined=false;ClearPeers();EndDynamicScene();
    bool bRetry=true;const FString Why=CloseReason(Code,bRetry);
    if(!bRetry){bWanted=false;RetryAt=0;QueuedMessages.Reset();Status(Why);ToastText(Why,true);if(Code==4003&&HasProductionSession())LogoutProduction();if(Code==4003&&!HasProductionSession())bDoorKeyRefused=true;}
    else if(bWanted&&bReconnect){RetryAt=FPlatformTime::Seconds()+RetryDelay;RetryDelay=FMath::Min(15.0,FMath::Max(1.2,RetryDelay*1.6));Status(Why);}
    else Status(TEXT("offline"));
}
void AEidoverseBridge::Send(const TSharedRef<FJsonObject>& Message)
{if(Socket&&Socket->IsConnected()){FString Text;FJsonSerializer::Serialize(Message,TJsonWriterFactory<>::Create(&Text));Socket->Send(Text);}}
bool AEidoverseBridge::Say(const FString& Text)
{
    const double Now=FPlatformTime::Seconds();
    if(bSpectator||Text.TrimStartAndEnd().IsEmpty()||Text.Len()>4000||Now-LastChat<.4)return false;
    auto J=MakeShared<FJsonObject>(),A=MakeShared<FJsonObject>();A->SetStringField(TEXT("text"),Text);
    J->SetStringField(TEXT("type"),TEXT("verb"));J->SetStringField(TEXT("verb"),TEXT("say"));J->SetObjectField(TEXT("args"),A);
    if(!bJoined||!Socket||!Socket->IsConnected())return QueueWhileReconnecting(J,TEXT("say"));
    Send(J);LastChat=Now;SpeakOwnSay(Text);return true;
}
bool AEidoverseBridge::Whisper(const FString& To,const FString& Text)
{
    const double Now=FPlatformTime::Seconds();
    if(!bJoined||!Socket||!Socket->IsConnected()||bSpectator||To.TrimStartAndEnd().IsEmpty()||To.Len()>64||Text.TrimStartAndEnd().IsEmpty()||Text.Len()>4000||Now-LastChat<.4)return false;
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("type"),TEXT("whisper"));J->SetStringField(TEXT("to"),To);J->SetStringField(TEXT("text"),Text);
    Send(J);LastChat=Now;return true;
}
void AEidoverseBridge::Announce(const FString& Id,const FString& Avatar)
{
    if(Id==AssignedIdentity&&!Id.IsEmpty()){if(!Avatar.IsEmpty()&&Avatar!=SelfBody.AvatarPath)RequestAvatar(SelfBody,Avatar);return;}
    if(Id.IsEmpty()||Id.Len()>64)return;
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
    if(AvatarCache.Num()>=32)
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
    // A few bodies in flight: parsing is on the game thread but mesh building is
    // asynchronous, so strictly one at a time left a busy world grey for minutes.
    int32 Loading=0;for(auto& A:AvatarCache)Loading+=A.Value->State==UEidoverseAvatarAsset::EState::Loading;
    // Nearest people first (my own body counts as nearest).
    if(Loading<3)
    {
        TMap<UEidoverseAvatarAsset*,double> Nearest;
        auto Want=[&Nearest](UEidoverseAvatarAsset* A,double D){if(A&&A->State==UEidoverseAvatarAsset::EState::Queued){double& Best=Nearest.FindOrAdd(A,D);Best=FMath::Min(Best,D);}};
        Want(SelfBody.AvatarAsset.Get(),-1);
        for(auto& Pair:Peers)if(Pair.Value.Actor.IsValid())Want(Pair.Value.AvatarAsset.Get(),FVector::DistSquared(Eye,Pair.Value.Actor->GetActorLocation()));
        // Failed bodies retry (0.4 / 1.6 / 5 s), then stay as the fallback.
        const double NowSec=FPlatformTime::Seconds();
        for(auto& A:AvatarCache)if(UEidoverseAvatarAsset* Asset=A.Value.Get())if(Asset->State==UEidoverseAvatarAsset::EState::Failed&&Asset->Retries<3)
        {
            static const double Delays[3]={.4,1.6,5.};
            if(Asset->RetryAt==0)Asset->RetryAt=NowSec+Delays[Asset->Retries];
            else if(NowSec>=Asset->RetryAt){Asset->RetryAt=0;if(Asset->RetryAfterFailure())Want(Asset,0);}
        }
        Nearest.ValueSort([](double A,double B){return A<B;});
        for(auto& N:Nearest){if(Loading>=3)break;N.Key->Start();++Loading;}
    }
    int32 Appeared=0; // one new body per frame, like models (EidoverseScene Tick)
    LoadedAvatarCount=0;
    for(auto& Pair:Peers)
    {
        auto& P=Pair.Value;auto* Asset=P.AvatarAsset.Get();
        if(Asset&&Asset->State==UEidoverseAvatarAsset::EState::Ready&&P.Actor.IsValid())
        {
            if((!P.AvatarMesh.IsValid()||P.AvatarMesh->Avatar!=Asset)&&Appeared++<1)
            {
                if(!P.AvatarMesh.IsValid())
                {
                    auto* M=NewObject<UEidoverseAvatarMesh>(P.Actor.Get());P.Actor->AddInstanceComponent(M);
                    M->SetupAttachment(P.Actor->GetRootComponent());M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->RegisterComponent();P.AvatarMesh=M;
                }
                P.AvatarMesh->SetAvatar(Asset);P.bHeldPoseDirty=true;
            }
        }
        if(P.AvatarMesh.IsValid())
        {
            ++LoadedAvatarCount;
            P.AvatarMesh->SetVisibility(!P.bSubmarine,true);P.AvatarMesh->SetRelativeLocation(FVector(0,0,P.bPawnAnchor?-85:0));
            // remotes.js: animate every frame under 8 m, every 2nd under 20 m, else every 4th
            // (staggered per body); closed-form clips land at the right phase regardless.
            const double DistM=FVector::Dist(Eye,P.Actor->GetActorLocation())/100;
            const uint32 Every=uint32((DistM<8?1:DistM<20?2:4)*EidoSettings::AvatarLodBias());   // x the avatar-detail bias
            const bool bAnimateNow=Every==1||((GFrameCounter+GetTypeHash(Pair.Key))%Every)==0||P.AvatarMesh->IsLimp();
            if(!P.bSubmarine&&bAnimateNow)
            {
                const bool Fresh=!P.Samples.IsEmpty()&&Time-P.Samples.Last().Time<.75;
                const FVector LocalVelocity=Fresh?P.Actor->GetActorQuat().UnrotateVector(P.VelocityMeters):FVector::ZeroVector;
                const bool bAquatic=FEidoverseSwimMotion::Evaluate(P.Mode,0,FVector::ZeroVector).bAquatic;
                FString Slot=P.Clip;if(Slot.IsEmpty()||Slot==TEXT("ragdoll"))Slot=Fresh&&P.Speed>.05f?TEXT("walk"):TEXT("idle");
                P.AvatarMesh->SetHeadPitch(P.Pitch);
                P.AvatarMesh->SetWings(P.bWingsFolded,0);
                if(P.bHeldPoseDirty){P.bHeldPoseDirty=false;if(P.HeldPose)P.AvatarMesh->SetHeldPose(P.HeldPose);else P.AvatarMesh->ClearHeldPose();}
                if(bAquatic||!AnimateWithClips(P,Slot,Fresh?P.Speed:0,Time))
                {
                    P.AvatarMesh->Animate(P.Mode,Fresh?P.Speed:0,LocalVelocity,Time);
                    if(P.Speed>.05)P.EmoteUntil=0;
                    if(Time<P.EmoteUntil)P.AvatarMesh->ApplyGesture(P.ActiveEmote,Time);
                }
            }
            if(P.Mesh.IsValid())P.Mesh->SetVisibility(P.bSubmarine);
            if(!P.AvatarMesh->EmissiveLights.IsEmpty())Lamps.Add({P.AvatarMesh.Get(),FVector::DistSquared(Eye,P.Actor->GetActorLocation()),!P.bSubmarine});
        }
    }
    TickSelfBody(Time);
    TickPinMarkers();
    TickFloraPushers();
    TickGaze(Time);
    if(SelfBody.AvatarMesh.IsValid()&&!SelfBody.AvatarMesh->EmissiveLights.IsEmpty())
        Lamps.Add({SelfBody.AvatarMesh.Get(),0.,SelfBody.AvatarMesh->IsVisible()});
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
bool AEidoverseBridge::AnimateWithClips(FEidoversePeer& P,const FString& Slot,float Speed,double Time)
{
    UEidoverseAvatarMesh* M=P.AvatarMesh.Get();
    if(!M||!ClipLibrary)return false;
    // Emotes: one-shot clips from the server's emote table, loaded on first use
    // (avatar.js playEmote); moving cancels them (setClip with speed > 0.05).
    if(P.bEmotePending)
    {
        const FString File=ClipLibrary->EmoteClip(P.ActiveEmote);
        if(File.IsEmpty()||Time>P.EmoteUntil)P.bEmotePending=false;
        else if(TSharedPtr<const FEidoClip> Emote=ClipLibrary->Find(File))
        {
            M->PlayOneShot(Emote,P.ActiveEmote);P.bEmotePending=false;P.EmoteUntil=0;
            UE_LOG(LogTemp,Display,TEXT("EIDOVERSE emote %s -> %s (%.1fs)"),*P.ActiveEmote,*File,Emote->Duration);
        }
    }
    if(Speed>.05f&&M->HasOneShot())M->CancelOneShot();
    FString Played;
    if(TSharedPtr<const FEidoClip> Clip=ClipLibrary->Resolve(Slot,&Played))
    {
        const bool bEase=&P==&SelfBody&&bSelfJumpEase;
        M->SetClip(Clip,Played,P.ClipRate>0?P.ClipRate:EidoClipRules::RateFor(Played,Speed),Played==TEXT("jump")?(bEase?.5f:.1f):.22f);
        if(P.bClipSample&&P.ClipTimeSlot==Played)M->SyncClipPhase(Played,P.ClipTime,P.ClipRate);
    }
    P.bClipSample=false;
    return M->AnimateClips(Time);
}
void AEidoverseBridge::TickSelfBody(double Time)
{
    auto* Explorer=Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this,0));
    auto* Asset=SelfBody.AvatarAsset.Get();
    if(SelfBody.AvatarMesh.IsValid()&&SelfBody.AvatarMesh->GetOwner()!=Explorer){SelfBody.AvatarMesh->DestroyComponent();SelfBody.AvatarMesh.Reset();}
    if(SelfBody.Mesh.IsValid()&&SelfBody.Mesh->GetOwner()!=Explorer){SelfBody.Mesh->DestroyComponent();SelfBody.Mesh.Reset();}
    if(!Explorer)return;
    // No wearable body (none named, still loading, or failed): the diver proxy peers see.
    const bool bReady=Asset&&Asset->State==UEidoverseAvatarAsset::EState::Ready;
    if(!bReady&&DiverProxyMesh&&(!Asset||Asset->State==UEidoverseAvatarAsset::EState::Failed))
    {
        if(!SelfBody.Mesh.IsValid())
        {
            auto* Proxy=NewObject<UStaticMeshComponent>(Explorer);Explorer->AddInstanceComponent(Proxy);Proxy->SetupAttachment(Explorer->GetRootComponent());
            Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);Proxy->SetStaticMesh(DiverProxyMesh);Proxy->SetRelativeScale3D(FVector(.5,.5,1.7));
            Proxy->RegisterComponent();SelfBody.Mesh=Proxy;
        }
        SelfBody.Mesh->SetVisibility(!Explorer->bFirstPerson);
    }
    else if(SelfBody.Mesh.IsValid())SelfBody.Mesh->SetVisibility(false);
    if(!bReady)return;
    if(!SelfBody.AvatarMesh.IsValid())
    {
        auto* M=NewObject<UEidoverseAvatarMesh>(Explorer);Explorer->AddInstanceComponent(M);
        M->SetupAttachment(Explorer->GetRootComponent());M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        M->SetRelativeLocation(FVector(0,0,-88));M->RegisterComponent();SelfBody.AvatarMesh=M;SelfBody.Actor=Explorer;
    }
    auto* M=SelfBody.AvatarMesh.Get();
    if(M->Avatar!=Asset)M->SetAvatar(Asset);
    // Don't render the inside of your own head (controller.js updateFollowCamera).
    M->SetVisibility(!Explorer->bFirstPerson,true);
    const FVector Velocity=Explorer->BodyVelocity/100.;
    const FVector Local=Explorer->GetActorQuat().UnrotateVector(Velocity);
    const float Speed=Velocity.Size();
    // My clip, chosen as the browser's controller does (locomotion_clip.js).
    const bool bSwimming=Explorer->LocomotionMode!=TEXT("walk");
    const float Ground=FVector(Velocity.X,Velocity.Y,0).Size();
    TickLocomotion(Explorer,FMath::Min(float(FApp::GetDeltaTime()),.1f));
    TickBodyPhysics(Explorer,FMath::Min(float(FApp::GetDeltaTime()),.1f));
    const bool bFlying=IsFlying();
    if(LocoMode.IsEmpty()&&!bDowned&&!bFlying)TickSeating(Explorer,FPlatformTime::Seconds());
    M->SetHeadPitch(SelfHeadPitch());
    // G folds the wings (controller.js toggleFold): needs wing chains to start; releasing always works.
    if(Explorer->bFoldPressed)
    {
        Explorer->bFoldPressed=false;
        if(!bWingsFolded&&!M->CanFoldWings()){}
        else
        {
            bWingsFolded=!bWingsFolded;
            if(HUD)HUD->Hint(bWingsFolded?TEXT("wings folded \u2014 the vigil posture costs the sky (G to unfold)"):M->CanFoldWings()?TEXT("wings open"):TEXT("folded-wing posture released \u2014 this body has no wings"),3);
        }
    }
    M->SetWings(bWingsFolded,FlightEffort());
    SelfClipSlot=!LocoClip.IsEmpty()?LocoClip:Explorer->bMantling?TEXT("climb"):!SeatClip.IsEmpty()?SeatClip:EidoClipRules::SelectLocomotion(Explorer->bJumped,Explorer->AirborneSeconds,Explorer->bWantMove,Ground,Explorer->Posture);
    // Walking off a ledge eases into the fall over 0.5 s; a deliberate jump snaps in 0.1 s.
    bSelfJumpEase=SelfClipSlot==TEXT("jump")&&!Explorer->bJumped;
    // First person while seated, lying or posed: the eye rides the head (fp_view.js).
    {
        static const TSet<FString> Upright={TEXT("idle"),TEXT("walk"),TEXT("run"),TEXT("jump"),TEXT("climb")};
        FVector Head;
        Explorer->bEyeOverride=Explorer->bFirstPerson&&(SelfHeldPose.IsValid()||!Upright.Contains(SelfClipSlot))&&M->HumanBonePosition(TEXT("head"),Head);
        if(Explorer->bEyeOverride)
            if(const APlayerController* PC=Cast<APlayerController>(Explorer->GetController()))
                Explorer->EyeOverride=Head+FVector(0,0,6)+FRotator(0,PC->GetControlRotation().Yaw,0).Vector()*16;
    }
    // Movement bounds (controller.js constrainMovement): authored box, else a 78 m stage.
    if(DynamicScene&&!Explorer->bSeated)
    {
        FVector Lo,Hi;FVector W=ToWirePosition(Explorer->GetActorLocation()-OriginCentimeters);const FVector Before=W;
        if(DynamicScene->EnvironmentBounds(Lo,Hi)){for(int32 I=0;I<3;++I)W[I]=FMath::Clamp(W[I],FMath::Min(Lo[I],Hi[I]),FMath::Max(Lo[I],Hi[I]));}
        else if(!DynamicScene->WaterAt(W-FVector(0,.88,0))){const double R=FVector2D(W.X,W.Z).Size();if(R>78){W.X*=78/R;W.Z*=78/R;}}
        if(!W.Equals(Before,.001))Explorer->SetActorLocation(OriginCentimeters+FromWirePosition(W),false,nullptr,ETeleportType::TeleportPhysics);
    }
    if(!bSwimming&&AnimateWithClips(SelfBody,SelfClipSlot,Ground,Time))return;
    M->Animate(Explorer->LocomotionMode,Speed,Local,Time);
    const double Now=FPlatformTime::Seconds();
    if(Speed>.05)SelfBody.EmoteUntil=0;
    if(Now<SelfBody.EmoteUntil)M->ApplyGesture(SelfBody.ActiveEmote,Time);
}
void AEidoverseBridge::Pose(const FString& Id,const TSharedPtr<FJsonObject>& Data,double Time)
{
    auto* Peer=Peers.Find(Id);if(!Peer||!Peer->Actor.IsValid())return; // roster authority, never create from a frame
    const TSharedPtr<FJsonObject>* Reach=nullptr;Peer->Reach=Data->TryGetObjectField(TEXT("reach"),Reach)?*Reach:nullptr;
    const FString Emote=Eido::String(Data,TEXT("emote"));
    if(!Emote.IsEmpty()&&Emote!=Peer->LastEmote){Peer->ActiveEmote=Emote;Peer->EmoteUntil=Time+2.5;Peer->bEmotePending=true;}
    Peer->LastEmote=Emote;
    // Clip and its phase (pose-wire: clip, clipTime, clipTimeSlot, clipRate).
    {
        const FString Clip=Eido::String(Data,TEXT("clip")).Left(48);
        if(!Clip.IsEmpty())Peer->Clip=Clip;
        double ClipTime=-1,ClipRate=0;
        if(Data->TryGetNumberField(TEXT("clipTime"),ClipTime)&&FMath::IsFinite(ClipTime)&&ClipTime>=0&&ClipTime<3600)
        {Peer->ClipTime=float(ClipTime);Peer->ClipTimeSlot=Eido::String(Data,TEXT("clipTimeSlot")).Left(48);Peer->bClipSample=true;}
        Peer->ClipRate=Data->TryGetNumberField(TEXT("clipRate"),ClipRate)&&FMath::IsFinite(ClipRate)&&ClipRate>0&&ClipRate<=3.2?float(ClipRate):0.f;
    }
    {bool bFold=false;if(Data->TryGetBoolField(TEXT("wingsFolded"),bFold))Peer->bWingsFolded=bFold;}
    if(const TSharedPtr<FJsonValue> PinsV=Data->TryGetField(TEXT("pins")))
    {
        Peer->Pins.Reset();
        const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
        if(PinsV->TryGetArray(A))for(const auto& V:*A)
        {
            const TSharedPtr<FJsonObject>* O=nullptr;const TArray<TSharedPtr<FJsonValue>>* At=nullptr;FVector W;bool bOk=false;
            if(V->TryGetObject(O)&&(*O)->TryGetArrayField(TEXT("at"),At)&&At->Num()==3){bOk=true;for(int32 I=0;I<3;++I){double N;bOk&=(*At)[I]->TryGetNumber(N)&&FMath::IsFinite(N);W[I]=bOk?N:0;}}
            if(bOk&&Peer->Pins.Num()<8)Peer->Pins.Emplace(FName(Eido::String(*O,TEXT("j")).Left(48)),W);
        }
    }
    {const TSharedPtr<FJsonObject>* X=nullptr;Peer->XR=Data->TryGetObjectField(TEXT("xr"),X)?*X:nullptr;}
    {const FString Presence=Eido::String(Data,TEXT("presence"));if(Presence==TEXT("present")||Presence==TEXT("away")||Presence==TEXT("busy"))Peer->Presence=Presence;}
    // Head pitch and a held pose (sent on every packet once set; null clears).
    {
        double Pitch=0;Peer->Pitch=Data->TryGetNumberField(TEXT("pitch"),Pitch)&&FMath::IsFinite(Pitch)?float(FMath::Clamp(Pitch,-.6,.6)):0.f;
        if(Data->HasField(TEXT("pose")))
        {
            const TSharedPtr<FJsonObject>* Held=nullptr;
            FString Key;
            if(Data->TryGetObjectField(TEXT("pose"),Held)&&(*Held)->Values.Num()>0)
            {auto W=TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Key);FJsonSerializer::Serialize((*Held).ToSharedRef(),W);}
            if(Key!=Peer->HeldPoseKey){Peer->HeldPoseKey=Key;Peer->HeldPose=Key.IsEmpty()?nullptr:*Held;Peer->bHeldPoseDirty=true;}
            if(Key.IsEmpty())Peer->HeldPose.Reset();
        }
    }
    TArray<double> P;if(!Eido::Vector(Data,TEXT("p"),3,P))return;
    FEidoverseSample Sample;Sample.Time=Time;Sample.Pose=Peer->HeldPose;Sample.bLimp=Eido::String(Data,TEXT("clip"))==TEXT("ragdoll");Sample.Position=OriginCentimeters+FromWirePosition(FVector(P[0],P[1],P[2]));
    double Yaw=0;if(Data->HasField(TEXT("yaw"))&&(!Data->TryGetNumberField(TEXT("yaw"),Yaw)||!FMath::IsFinite(Yaw)))return;
    Sample.Orientation=FromWireRotation(FQuat(FVector::YAxisVector,Yaw));
    TArray<double> Q;if(Data->HasField(TEXT("q")))
    {if(!Eido::Vector(Data,TEXT("q"),4,Q))return;FQuat Rotation(Q[0],Q[1],Q[2],Q[3]);if(Rotation.SizeSquared()<.0001)return;Sample.Orientation=FromWireRotation(Rotation.GetNormalized());}
    const TSharedPtr<FJsonObject>* Loc=nullptr;FString Body;
    if(Data->TryGetObjectField(TEXT("locomotion"),Loc)){Peer->Mode=Eido::String(*Loc,TEXT("mode")).Left(32);Body=Eido::String(*Loc,TEXT("body"));}
    else Peer->Mode=Eido::String(Data,TEXT("clip"));
    double Speed=0;Data->TryGetNumberField(TEXT("speed"),Speed);Peer->Speed=FMath::IsFinite(Speed)?FMath::Clamp(float(Speed),0.f,10.f):0;
    // Every humanoid sends its feet (web swimmers/climbers/pilots say body:'diver' with
    // feet coordinates); only a submarine is anchored at its centre.
    Peer->bPawnAnchor=Body==TEXT("submarine");
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
    if(ForwardVoiceSignal(Type,J))return;
    if(Type==TEXT("snapshot"))
    {
        AssignedIdentity=Eido::String(J,TEXT("you"));if(AssignedIdentity.IsEmpty())return;
        ClearPeers();LastFrameTime=-1;bJoined=true;RetryDelay=1;RetryAt=0;
        const TSharedPtr<FJsonObject>* Rights=nullptr;ReadRights(J->TryGetObjectField(TEXT("yourRights"),Rights)?*Rights:nullptr);
        VoiceJoined();
        AvatarChoices.Reset();
        const TArray<TSharedPtr<FJsonValue>>* Avatars=nullptr;
        if(J->TryGetArrayField(TEXT("avatars"),Avatars))for(const auto& V:*Avatars)if(const auto O=V->AsObject())
        {
            FEidoverseAvatarChoice C;O->TryGetStringField(TEXT("name"),C.Name);O->TryGetStringField(TEXT("path"),C.Path);
            if(!C.Name.IsEmpty()&&!C.Path.IsEmpty()&&AvatarChoices.Num()<256)AvatarChoices.Add(C);
        }
        double SceneVersion=0;bSceneSupported=J->TryGetNumberField(TEXT("renderSceneVersion"),SceneVersion)&&SceneVersion==1;
        // An avatar change re-joins the same world: keep the rendered scene and the pawn.
        if(bRejoining&&DynamicScene){bRejoining=false;}
        else if(bRenderDynamicWorld&&bSceneSupported)BeginDynamicScene();
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
        // The roster normally names my body; fall back to what I asked to wear.
        if(!SelfBody.AvatarAsset.IsValid()&&!AvatarPath.IsEmpty()&&SelfBody.AvatarPath!=AvatarPath)RequestAvatar(SelfBody,AvatarPath);
        Status(TEXT("joined ")+WorldName+TEXT(" as ")+AssignedIdentity);
        bool Recording=false;if(J->TryGetBoolField(TEXT("recording"),Recording)&&Recording)Narrate(TEXT("⏺ this performance is being recorded (movement + chat, for the archive)"));
        if(Sim&&Epoch)(*Epoch)->TryGetStringField(TEXT("sim"),SimId);
        ReceiveSnapshotExtras(J,Now);
    }
    else if(Type==TEXT("render-scene")&&bJoined)ReceiveDynamicScene(J);
    else if(Type==TEXT("lease")&&bJoined)ReceiveLease(J);
    else if(Type==TEXT("snap")&&bJoined)ReceiveCapture(J);
    else if(Type==TEXT("arrive")&&bJoined)
    {
        const FString Id=Eido::String(J,TEXT("id"));
        if(!Peers.Contains(Id)&&Id!=AssignedIdentity)Narrate(Id.Left(64)+TEXT(" arrived"));
        Announce(Id,Eido::String(J,TEXT("avatar")));
    }
    else if(Type==TEXT("avatar-updated")&&bJoined)
    {
        const FString Path=Eido::String(J,TEXT("path"));double Version;
        if(!J->TryGetNumberField(TEXT("v"),Version)||!FMath::IsFinite(Version)||Version<0||Version>9e15)return;
        for(auto& P:Peers)
        {FString Base=P.Value.AvatarPath;int32 Query;if(Base.FindChar('?',Query))Base=Base.Left(Query);if(Base==Path)RequestAvatar(P.Value,Path+FString::Printf(TEXT("?v=%.0f"),Version));}
        {FString Base=SelfBody.AvatarPath;int32 Query;if(Base.FindChar('?',Query))Base=Base.Left(Query);if(Base==Path)RequestAvatar(SelfBody,Path+FString::Printf(TEXT("?v=%.0f"),Version));}
        Narrate(FString::Printf(TEXT("avatar “%s” updated (v%.0f)"),*Eido::String(J,TEXT("name")).Left(64),Version));
    }
    else if(Type==TEXT("leave"))
    {
        const FString Id=Eido::String(J,TEXT("id"));
        if(bJoined&&Peers.Contains(Id))Narrate(Id.Left(64)+TEXT(" left"));
        if(auto* P=Peers.Find(Id))if(P->Actor.IsValid())P->Actor->Destroy();Peers.Remove(Id);RemoteCount=Peers.Num();RemoteMounts.Remove(Id);
    }
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
        NarrateLogEntry(*Entry);
        {
            const FString Verb=Eido::String(*Entry,TEXT("verb"));const TSharedPtr<FJsonObject>* SeatArgs=nullptr;
            if((Verb==TEXT("mount")||Verb==TEXT("dismount"))&&(*Entry)->TryGetObjectField(TEXT("args"),SeatArgs))ReceiveSeatLog(Verb,*SeatArgs);
        }
        if(Eido::String(*Entry,TEXT("verb"))!=TEXT("say"))return;
        const TSharedPtr<FJsonObject>* Args=nullptr;if(!(*Entry)->TryGetObjectField(TEXT("args"),Args))return;
        FString Sender=Eido::String(*Entry,TEXT("actor")),Text=Eido::String(*Args,TEXT("text")).Left(4000);FString Utt;double UttN;bool bSpoken=false;(*Args)->TryGetBoolField(TEXT("spoken"),bSpoken);
        if(bSpoken&&(*Args)->TryGetNumberField(TEXT("utt"),UttN))Utt=FString::Printf(TEXT("%.0f"),UttN);else if(bSpoken)(*Args)->TryGetStringField(TEXT("utt"),Utt);
        if(RenderedChatSeqs.Contains(int64(Seq)))return;RenderedChatSeqs.Add(int64(Seq));if(OldestChatSeq<0)OldestChatSeq=int64(Seq);
        AddChatLine(Sender,Text,false,Utt.Left(64),bSpoken);
        if(!bSpoken)NoteSpeaker(Sender,Text);   // spoken says were performed live by their captions
    }
    else if(Type==TEXT("your-rights")&&bJoined)
    {
        const TSharedPtr<FJsonObject>* Rights=nullptr;if(!J->TryGetObjectField(TEXT("rights"),Rights))return;
        const FString Before=WorldRole;const bool bGenBefore=bCanGenerate;ReadRights(*Rights);
        if((WorldRole!=Before||bCanGenerate!=bGenBefore)&&!bWorldOpen&&WorldRole!=TEXT("owner"))
            Narrate(FString::Printf(TEXT("this world has an owner — you are a %s%s here"),*WorldRole,bCanGenerate?TEXT(" +gen"):TEXT("")));
    }
    else if(Type==TEXT("anim")&&bJoined)ReceiveAgentAnimation(J);
    else if(Type==TEXT("caption")&&bJoined)PerformSpeech(Eido::String(J,TEXT("id")),Eido::String(J,TEXT("text")).Left(500));
    else if(Type==TEXT("inspect")&&bJoined){InspectedId=Eido::String(J,TEXT("id"));InspectReply=J;++InspectSerial;}
    else if(Type==TEXT("typing")&&bJoined)
    {
        // Public composing, or a whisper being composed to us; others' DMs stay private.
        const FString Id=Eido::String(J,TEXT("id")),To=Eido::String(J,TEXT("to"));
        if(Id.IsEmpty()||Id==AssignedIdentity||(!To.IsEmpty()&&To!=AssignedIdentity))return;
        // state: ear | think | tool | mic glyphs (agents, live voices); none = composing dots.
        const FString State=Eido::String(J,TEXT("state"));
        if(HUD&&State.IsEmpty())HUD->NoteTyping(Id);
        if(auto* Peer=Peers.Find(Id))if(auto* Plate=Peer->Plate.Get())if(auto* W=Cast<UEidoverseNameplate>(Plate->GetUserWidgetObject()))W->ShowTypingState(State);
    }
    else if(Type==TEXT("whisper")&&bJoined)
    {
        // The server echoes my own whispers (echo:true) so every tab of mine sees them.
        const FString From=Eido::String(J,TEXT("from")).Left(64),To=Eido::String(J,TEXT("to")).Left(64),Text=Eido::String(J,TEXT("text")).Left(4000);
        bool bEcho=false;J->TryGetBoolField(TEXT("echo"),bEcho);
        if(bEcho)AddChatLine(TEXT("to ")+To,Text,true);
        else{LastWhisperFrom=From;AddChatLine(From,Text,true);if(HUD)HUD->Toast(From+TEXT(" whispered you"));}
    }
    else ReceiveProtocol(Type,J,Now);
}
void AEidoverseBridge::SendPose()
{
    auto* Pawn=UGameplayStatics::GetPlayerPawn(this,0);if(!Pawn)return;
    const bool Sub=Pawn->ActorHasTag(TEXT("OceanSubmarine"));
    const bool Land=Pawn->IsA<AEidoverseExplorer>();
    const FVector P=ToWirePosition(Pawn->GetActorLocation()-OriginCentimeters-(Land?FVector(0,0,88):Sub?FVector::ZeroVector:FVector(0,0,85)));
    const FQuat Q=ToWireRotation(Pawn->GetActorQuat().GetNormalized());
    const FVector F=ToWirePosition(Pawn->GetActorForwardVector());
    const FVector BodyV=Cast<AEidoverseExplorer>(Pawn)?Cast<AEidoverseExplorer>(Pawn)->BodyVelocity:Pawn->GetVelocity();
    const bool bOnLand=Cast<AEidoverseExplorer>(Pawn)&&Cast<AEidoverseExplorer>(Pawn)->LocomotionMode==TEXT("walk");
    const bool bSeatedPose=Cast<AEidoverseExplorer>(Pawn)&&Cast<AEidoverseExplorer>(Pawn)->bSeated;
    const double Speed=bSeatedPose?0.:(bOnLand?FVector(BodyV.X,BodyV.Y,0):BodyV).Size()/100.;
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
    // The clip and its phase, as the browser sends them (net.js sendPose): web viewers
    // play the same clip in step with ours. Swimmers keep walk/idle (no swim clip).
    FString Clip=Speed>.05?TEXT("walk"):TEXT("idle");
    if(Explorer&&!bSwimmer)Clip=SelfClipSlot;
    Data->SetStringField(TEXT("clip"),Clip);
    if(Explorer&&!bSwimmer)Data->SetNumberField(TEXT("pitch"),FMath::RoundToDouble(SelfHeadPitch()*1000)/1000);
    if(const UEidoverseAvatarMesh* M=SelfBody.AvatarMesh.Get())
        if(!bSwimmer&&!M->HasOneShot()&&M->CurrentClipSlot()==Clip)
        {
            Data->SetNumberField(TEXT("clipTime"),FMath::RoundToDouble(M->CurrentClipTime()*1000)/1000);
            Data->SetStringField(TEXT("clipTimeSlot"),Clip);
            Data->SetNumberField(TEXT("clipRate"),FMath::RoundToDouble(M->CurrentClipRate()*1000)/1000);
        }
    // Climbing, piloting and flying speak for the body (traversal.js / vehicles.js / flight).
    const bool bLoco=!LocoMode.IsEmpty();
    if(bLoco){Mode=LocoMode;Data->SetNumberField(TEXT("speed"),LocoSpeed);}
    Loc->SetStringField(TEXT("mode"),Mode);Loc->SetStringField(TEXT("body"),bLoco||bSwimmer?TEXT("diver"):Land?TEXT("avatar"):Sub?TEXT("submarine"):TEXT("diver"));
    Loc->SetStringField(TEXT("medium"),(Mode==TEXT("walk")||Mode==TEXT("climb"))?TEXT("air"):TEXT("water"));Data->SetObjectField(TEXT("locomotion"),Loc);
    if(LocalReach)Data->SetObjectField(TEXT("reach"),LocalReach);
    // The rest of the public body (net.js sendPose): presence, wings, held pose.
    Data->SetStringField(TEXT("presence"),CurrentPresence());
    Data->SetBoolField(TEXT("wingsFolded"),bWingsFolded);
    if(Speed>=.05){bBodyMovedThisSession=true;if(SelfHeldPose)SetSelfHeldPose(nullptr,false);}   // walking away ends a held pose
    if(SelfHeldPose)Data->SetObjectField(TEXT("pose"),SelfHeldPose);
    else if(bSendHeldNull){Data->SetField(TEXT("pose"),MakeShared<FJsonValueNull>());bSendHeldNull=false;}
    // Bodydrag nails ride presence too (net.js): [{j, at}] while any, null once to clear.
    if(MyPins.Num()){TArray<TSharedPtr<FJsonValue>> Pins;for(const auto& Pin:MyPins){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("j"),Pin.Key.ToString());O->SetArrayField(TEXT("at"),Eido::Numbers({Pin.Value.X,Pin.Value.Y,Pin.Value.Z}));Pins.Add(MakeShared<FJsonValueObject>(O));}Data->SetArrayField(TEXT("pins"),Pins);}
    else if(bSendPinsNull){Data->SetField(TEXT("pins"),MakeShared<FJsonValueNull>());bSendPinsNull=false;}
    if(!PendingEmote.IsEmpty()){Data->SetStringField(TEXT("emote"),PendingEmote);PendingEmote.Empty();}
    J->SetStringField(TEXT("type"),TEXT("pose"));J->SetObjectField(TEXT("pose"),Data);Send(J);++SentPoseCount;
}
// -EidoverseJoin=<server>,<world>,<name> [-EidoverseAvatar=<library path>]
// [-EidoverseQuitAfter=<seconds after joining>]: launch straight into a world, and
// optionally quit after a while logging EIDOVERSE_LOADSTATS (load measurements).
void AEidoverseBridge::TickCommandLine(double Now)
{
    if(!bCommandLineJoinTried&&HUD)
    {
        bCommandLineJoinTried=true;FString Spec;
        if(FParse::Value(FCommandLine::Get(),TEXT("EidoverseJoin="),Spec,false))
        {
            TArray<FString> Parts;Spec.ParseIntoArray(Parts,TEXT(","),false);
            FString Avatar;if(FParse::Value(FCommandLine::Get(),TEXT("EidoverseAvatar="),Avatar,false))AvatarPath=Avatar;
            if(Parts.Num()==3)JoinFromDoor(Parts[0],Parts[1],Parts[2]);
            else UE_LOG(LogTemp,Warning,TEXT("EIDOVERSE -EidoverseJoin wants <server>,<world>,<name>"));
        }
    }
    double QuitAfter=0;
    if(bJoined&&FParse::Value(FCommandLine::Get(),TEXT("EidoverseQuitAfter="),QuitAfter)&&QuitAfter>0)
    {
        if(CommandLineJoinedAt<=0)CommandLineJoinedAt=Now;
        else if(Now-CommandLineJoinedAt>QuitAfter&&!bCommandLineQuitting)
        {
            bCommandLineQuitting=true;
            UE_LOG(LogTemp,Warning,TEXT("EIDOVERSE_LOADSTATS %s"),*EidoLoadTrace::Summary());
            UE_LOG(LogTemp,Warning,TEXT("EIDOVERSE_COUNTS {\"remote\":%d,\"avatars\":%d,\"entities\":%d,\"models\":%d,\"failed_models\":%d}"),RemoteCount,LoadedAvatarCount,
                DynamicScene?DynamicScene->EntityCount:0,DynamicScene?DynamicScene->LoadedModels:0,DynamicScene?DynamicScene->FailedModels:0);
            FPlatformMisc::RequestExit(false);
        }
    }
}
void AEidoverseBridge::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);const double Now=FPlatformTime::Seconds();
    EidoLoadTrace::Tick(FApp::GetDeltaTime());
    TickCommandLine(Now);
    TickProductionLogin(Now);
    TickVoice(DeltaSeconds);
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
    if(bWanted&&RetryAt>0&&Now>=RetryAt){bRetrying=true;Connect();bRetrying=false;return;}
    // world-reset: reload after the 1.8 s the web gives people to read the notice.
    if(ResetReconnectAt>0&&Now>=ResetReconnectAt){ResetReconnectAt=0;Connect();return;}
    if(FSlateApplication::IsInitialized())LastLocalInputAt=FMath::Max(LastLocalInputAt,FSlateApplication::Get().GetLastUserInteractionTime());
    if(bJoined&&!bSpectator&&Now-LastSend>=1./15){LastSend=Now;SendPose();}
    if(bJoined)
    {
        if(!ClipLibrary)ClipLibrary=NewObject<UEidoverseClipLibrary>(this);
        const FString Base=HttpBase();
        if(!Base.IsEmpty())
        {
            ClipLibrary->Configure(Base);
            for(const TCHAR* Slot:{TEXT("idle"),TEXT("walk"),TEXT("run"),TEXT("jump"),TEXT("sit"),TEXT("lie")})ClipLibrary->Prefetch(Slot);
        }
        TickAvatars(Now);
    }
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
        // A seated rider follows its seat every frame, not the 15 Hz stream (remotes.js).
        if(const TSharedPtr<FJsonObject>* Mount=RemoteMounts.Find(Pair.Key))
        {FTransform Seat;FString SeatPose;if(MountWorldTransform(*Mount,Seat,SeatPose))P.Actor->SetActorLocationAndRotation(Seat.GetLocation(),Seat.GetRotation());}
        // A limp body: blend its streamed pose between packets with the root's alpha;
        // a bone present in only one sample holds (remotes.js).
        if(UEidoverseAvatarMesh* M=P.AvatarMesh.Get())
        {
            const bool bLimp=P.Samples.Last().bLimp;
            M->SetLimp(bLimp);
            if(bLimp&&(A.Pose||B.Pose))
            {
                TMap<FName,FQuat> Blend;
                auto Read=[](const TSharedPtr<FJsonObject>& O,const FString& K,FQuat& Q)
                {
                    if(!O)return false;const TArray<TSharedPtr<FJsonValue>>* Arr=nullptr;const TSharedPtr<FJsonObject>* Bone=nullptr;
                    if(!O->TryGetArrayField(K,Arr)){if(!O->TryGetObjectField(K,Bone)||!(*Bone)->TryGetArrayField(TEXT("q"),Arr))return false;}
                    if(Arr->Num()!=4)return false;Q=FQuat((*Arr)[0]->AsNumber(),(*Arr)[1]->AsNumber(),(*Arr)[2]->AsNumber(),(*Arr)[3]->AsNumber());
                    return !Q.ContainsNaN()&&Q.SizeSquared()>1e-6;
                };
                TSet<FString> Keys;if(A.Pose)for(const auto& K:A.Pose->Values)Keys.Add(FString(K.Key));if(B.Pose)for(const auto& K:B.Pose->Values)Keys.Add(FString(K.Key));
                for(const FString& K:Keys)
                {
                    FQuat Qa,Qb;const bool bA=Read(A.Pose,K,Qa),bB=Read(B.Pose,K,Qb);
                    if(bA&&bB)Blend.Add(FName(*K),FQuat::Slerp(Qa.GetNormalized(),Qb.GetNormalized(),Alpha));
                    else if(bA||bB)Blend.Add(FName(*K),(bA?Qa:Qb).GetNormalized());
                }
                M->SetHeldPoseDirect(Blend);
            }
        }
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
