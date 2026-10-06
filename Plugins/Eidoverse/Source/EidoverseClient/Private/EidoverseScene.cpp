#include "EidoverseScene.h"
#include "Kismet/GameplayStatics.h"
#include "EidoverseLoadTrace.h"
#include "EidoverseMotion.h"
#include "EidoverseWater.h"
#include "EidoverseSettings.h"
#include "EngineUtils.h"
#include "EidoverseBridge.h"
#include "EidoverseAvatar.h"
#include "Dom/JsonObject.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "EidoverseUI.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/InputSettings.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"

namespace SceneWire
{
using Array=TArray<TSharedPtr<FJsonValue>>;
const Array& Arr(const TSharedPtr<FJsonObject>& O,const TCHAR* K){static Array Empty;const Array* A=nullptr;return O&&O->TryGetArrayField(K,A)?*A:Empty;}
TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O,const TCHAR* K){const TSharedPtr<FJsonObject>* V=nullptr;return O&&O->TryGetObjectField(K,V)?*V:nullptr;}
FString Str(const TSharedPtr<FJsonObject>& O,const TCHAR* K){FString S;if(O)O->TryGetStringField(K,S);return S;}
double Num(const TSharedPtr<FJsonObject>& O,const TCHAR* K,double D=0){double V;return O&&O->TryGetNumberField(K,V)&&FMath::IsFinite(V)?V:D;}
bool Vec(const Array& A,FVector& V){if(A.Num()!=3)return false;double N[3];for(int I=0;I<3;++I)if(!A[I]->TryGetNumber(N[I])||!FMath::IsFinite(N[I])||FMath::Abs(N[I])>1e6)return false;V=FVector(N[0],N[1],N[2]);return true;}
struct Mesh {TArray<FVector> P,N;TArray<int32> I;TArray<FVector2D> UV;};
void Triangle(Mesh& M,FVector A,FVector B,FVector C)
{
    A=AEidoverseBridge::FromWirePosition(A);B=AEidoverseBridge::FromWirePosition(B);C=AEidoverseBridge::FromWirePosition(C);
    // Reflection turns source CCW into UE clockwise; do not reverse it twice.
    FVector N=FVector::CrossProduct(C-A,B-A).GetSafeNormal();
    int32 I=M.P.Num();M.P.Append({A,B,C});M.N.Append({N,N,N});M.I.Append({I,I+1,I+2});
    for(FVector P:{A,B,C})M.UV.Add(FVector2D(P.X,-P.Y)/100.0);
}
void Box(Mesh& M,const TSharedPtr<FJsonObject>& B)
{
    const double X0=Num(B,TEXT("x0")),X1=Num(B,TEXT("x1")),Y0=Num(B,TEXT("y0")),Y1=Num(B,TEXT("y1")),Z0=Num(B,TEXT("z0")),Z1=Num(B,TEXT("z1"));
    FVector P[]={{X0,Y0,Z0},{X1,Y0,Z0},{X1,Y1,Z0},{X0,Y1,Z0},{X0,Y0,Z1},{X1,Y0,Z1},{X1,Y1,Z1},{X0,Y1,Z1}};
    const int F[][4]={{0,3,2,1},{4,5,6,7},{0,4,7,3},{1,2,6,5},{0,1,5,4},{3,7,6,2}};
    for(auto& Q:F){Triangle(M,P[Q[0]],P[Q[1]],P[Q[2]]);Triangle(M,P[Q[0]],P[Q[2]],P[Q[3]]);}
}
FString Slot(const TSharedPtr<FJsonObject>& B)
{
    FString M=Str(B,TEXT("mat")),K=Str(B,TEXT("kind"));
    for(const TCHAR* S:{TEXT("wall"),TEXT("floor"),TEXT("roof"),TEXT("glass"),TEXT("trim")})if(M==S)return M;
    if(K==TEXT("floor")||K==TEXT("roof")||K==TEXT("glass"))return K;
    return K==TEXT("base")||K==TEXT("case")||K==TEXT("sill")||K==TEXT("lintel")?TEXT("trim"):TEXT("wall");
}
}
using namespace SceneWire;
AEidoverseScene::AEidoverseScene(){PrimaryActorTick.bCanEverTick=true;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
UMaterialInstanceDynamic* AEidoverseScene::Material(const FString& SlotName)
{
    if(auto* M=Materials.Find(SlotName))return *M;
    auto* Base=LoadObject<UMaterialInterface>(nullptr,SlotName==TEXT("glass")?TEXT("/Game/Underwater/Avatars/Materials/MI_AvatarPBR_TwoSidedTranslucent"):SlotName==TEXT("terrain")?TEXT("/Game/Underwater/Avatars/Materials/M_EidoverseTerrain"):TEXT("/Game/Underwater/Avatars/Materials/M_EidoverseSurface"));
    if(!Base)Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Underwater/Avatars/Materials/MI_AvatarLit_Opaque"));
    auto* M=UMaterialInstanceDynamic::Create(Base,this);if(!M)return nullptr;
    uint32 Hex=SlotName==TEXT("floor")?0xa8814f:SlotName==TEXT("roof")?0x6d6257:SlotName==TEXT("trim")?0xf2efe8:SlotName==TEXT("terrain")?0x70815a:SlotName==TEXT("glass")?0xcfe2e8:0xe4ded1;
    FLinearColor C=FLinearColor::FromSRGBColor(FColor((Hex>>16)&255,(Hex>>8)&255,Hex&255));C.A=SlotName==TEXT("glass")?.22f:1;
    M->SetVectorParameterValue(TEXT("baseColorFactor"),C);M->SetScalarParameterValue(TEXT("metallicFactor"),0);M->SetScalarParameterValue(TEXT("roughnessFactor"),SlotName==TEXT("glass")?.08f:.86f);
    Materials.Add(SlotName,M);return M;
}
void AEidoverseScene::BuildStructure(AActor* Actor,const TSharedPtr<FJsonObject>& Plan)
{
    TMap<FString,SceneWire::Mesh> Groups;
    for(auto& V:Arr(Plan,TEXT("parts")))if(auto B=V->AsObject())Box(Groups.FindOrAdd(Slot(B)),B);
    for(auto& V:Arr(Plan,TEXT("sweeps")))if(auto S=V->AsObject())
    {
        auto& M=Groups.FindOrAdd(Slot(S));const auto& P=Arr(S,TEXT("positions"));const auto& I=Arr(S,TEXT("indices"));
        if(P.Num()>600000||I.Num()>600000)continue;
        for(int32 T=0;T+2<I.Num();T+=3)
        {
            FVector Q[3];bool OK=true;
            for(int J=0;J<3;++J){int32 At=int32(I[T+J]->AsNumber())*3;if(At<0||At+2>=P.Num()){OK=false;break;}Q[J]=FVector(P[At]->AsNumber(),P[At+1]->AsNumber(),P[At+2]->AsNumber());if(Q[J].ContainsNaN())OK=false;}
            if(OK)Triangle(M,Q[0],Q[1],Q[2]);
        }
    }
    auto* Mesh=NewObject<UProceduralMeshComponent>(Actor);Actor->AddInstanceComponent(Mesh);Mesh->SetupAttachment(Actor->GetRootComponent());Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->RegisterComponent();
    int32 Section=0;for(auto& Pair:Groups){auto& M=Pair.Value;Mesh->CreateMeshSection_LinearColor(Section,M.P,M.I,M.N,M.UV,{}, {},false);Mesh->SetMaterial(Section++,Material(Pair.Key));}
    for(auto& V:Arr(Plan,TEXT("boxes")))if(auto B=V->AsObject())
    {
        FVector Lo(Num(B,TEXT("x0")),Num(B,TEXT("y0")),Num(B,TEXT("z0"))),Hi(Num(B,TEXT("x1")),Num(B,TEXT("y1")),Num(B,TEXT("z1")));
        auto* C=NewObject<UBoxComponent>(Actor);Actor->AddInstanceComponent(C);C->SetupAttachment(Actor->GetRootComponent());
        C->SetRelativeLocation(AEidoverseBridge::FromWirePosition((Lo+Hi)*.5));C->SetBoxExtent(AEidoverseBridge::FromWirePosition((Hi-Lo)*.5).GetAbs());C->SetCollisionProfileName(TEXT("BlockAll"));C->RegisterComponent();
    }
}
void AEidoverseScene::ApplyEnvironment(const TSharedPtr<FJsonObject>& Env,const FVector& Origin)
{
    FloraData=Obj(Env,TEXT("flora"));SceneOrigin=Origin;bFloraDirty=true;
    auto T=Obj(Env,TEXT("terrain"));if(!T)return;
    // Older servers omit `authored`: treat their terrain as authored (unchanged).
    bTerrainAuthored=true;T->TryGetBoolField(TEXT("authored"),bTerrainAuthored);
    const auto& H=Arr(T,TEXT("heights"));int32 N=int32(Num(T,TEXT("segments")));double Size=Num(T,TEXT("size"));
    if(N<1||N>128||H.Num()!=(N+1)*(N+1)||Size<=0||Size>10000)return;
    if(Terrain)Terrain->DestroyComponent();Terrain=NewObject<UProceduralMeshComponent>(this);AddInstanceComponent(Terrain);Terrain->SetupAttachment(RootComponent);Terrain->SetWorldLocation(Origin);Terrain->bUseAsyncCooking=true;Terrain->RegisterComponent();
    TArray<FVector> P,Normals;TArray<FVector2D> UV;TArray<int32> Indices;
    for(int Z=0;Z<=N;++Z)for(int X=0;X<=N;++X){double Y=H[Z*(N+1)+X]->AsNumber();if(!FMath::IsFinite(Y))Y=0;P.Add(AEidoverseBridge::FromWirePosition(FVector((double(X)/N-.5)*Size,Y,(double(Z)/N-.5)*Size)));UV.Add(FVector2D(double(X)/N,double(Z)/N)*Size);Normals.Add(FVector::ZeroVector);}
    for(int Z=0;Z<N;++Z)for(int X=0;X<N;++X){int A=Z*(N+1)+X,B=A+1,C=A+N+1,D=C+1;Indices.Append({A,C,B,B,C,D});}
    for(int I=0;I<Indices.Num();I+=3){int A=Indices[I],B=Indices[I+1],C=Indices[I+2];FVector Normal=FVector::CrossProduct(P[C]-P[A],P[B]-P[A]);Normals[A]+=Normal;Normals[B]+=Normal;Normals[C]+=Normal;}
    for(auto& Normal:Normals)Normal.Normalize();
    auto* GroundMaterial=Material(TEXT("terrain"));const uint32 Tint=uint32(Num(T,TEXT("color"),0x70815a));
    if(GroundMaterial)GroundMaterial->SetVectorParameterValue(TEXT("baseColorFactor"),FLinearColor::FromSRGBColor(FColor((Tint>>16)&255,(Tint>>8)&255,Tint&255)));
    Terrain->CreateMeshSection_LinearColor(0,P,Indices,Normals,UV,{}, {},true);Terrain->SetMaterial(0,GroundMaterial);Terrain->SetCollisionProfileName(TEXT("BlockAll"));
}
void AEidoverseScene::Apply(const TSharedPtr<FJsonObject>& Message,const FVector& Origin,const FString& Library)
{
    if(Message->HasField(TEXT("error"))){Diagnostic=Str(Message,TEXT("error"));return;}
    SceneOrigin=Origin;LibraryBase=Library;
    if(double ServerMs;Message->TryGetNumberField(TEXT("time"),ServerMs)&&FMath::IsFinite(ServerMs))ObserveServerTime(ServerMs);
    ApplyWeather(Obj(Message,TEXT("atmosphere")));
    if(auto Env=Obj(Message,TEXT("environment")))ApplyEnvironment(Env,Origin);
    if(Arr(Message,TEXT("removed")).Num()||Arr(Message,TEXT("upserts")).Num())bFloraDirty=true;
    for(auto& V:Arr(Message,TEXT("removed"))){FString Id=V->AsString();if(auto* E=Items.Find(Id))if(E->Actor.IsValid())E->Actor->Destroy();Items.Remove(Id);}
    for(auto& V:Arr(Message,TEXT("upserts")))if(auto E=V->AsObject())
    {
        FString Id=Str(E,TEXT("id")),Revision=Str(E,TEXT("revision"));if(Id.IsEmpty()||Id.Len()>128)continue;
        auto* Old=Items.Find(Id);if(Old&&Old->Revision==Revision)continue;
        if(Old&&Old->Actor.IsValid())Old->Actor->Destroy();Items.Remove(Id);if(Items.Num()>=512)continue;
        AActor* A=GetWorld()->SpawnActor<AActor>();A->SetFlags(RF_Transient);A->Tags.Add(FName(*Id));auto* R=NewObject<USceneComponent>(A);A->AddInstanceComponent(R);A->SetRootComponent(R);R->RegisterComponent();
        FEidoverseSceneItem Item;Item.Actor=A;Item.Revision=Revision;Item.Lib=Str(E,TEXT("lib"));Item.Kind=Str(E,TEXT("kind"));Item.Interact=Obj(E,TEXT("interact"));Item.bCollide=Str(E,TEXT("collide"))!=TEXT("none");
        // comp collision {enabled:false}: non-solid by authorship (colliders.js).
        {bool bEnabled=true;if(auto Coll=Obj(Obj(E,TEXT("data")),TEXT("collision")))if(Coll->TryGetBoolField(TEXT("enabled"),bEnabled)&&!bEnabled)Item.bCollide=false;}
        if(auto Plan=Obj(E,TEXT("structure")))BuildStructure(A,Plan);
        else if(Str(E,TEXT("kind"))==TEXT("light"))
        {
            auto* L=NewObject<UPointLightComponent>(A);A->AddInstanceComponent(L);L->SetupAttachment(R);L->SetIntensityUnits(ELightUnits::Candelas);
            Item.LightIntensity=FMath::Clamp(Num(E,TEXT("intensity"),16),0.,100000.);Item.LightRange=FMath::Clamp(Num(E,TEXT("range"),10),0.,200.);
            L->SetIntensity(Item.LightIntensity);L->SetAttenuationRadius(Item.LightRange*100);
            uint32 C=uint32(Num(E,TEXT("color"),16767392));const FLinearColor Color=FLinearColor::FromSRGBColor(FColor((C>>16)&255,(C>>8)&255,C&255));
            // Placed lights never shadow (lightrig.js: only an avatar lamp in slot 0 may).
            L->SetLightColor(Color);L->SetCastShadows(false);L->SetVisibility(false);L->RegisterComponent();
            bool bKeep=false,bDay=true;E->TryGetBoolField(TEXT("keep"),bKeep);E->TryGetBoolField(TEXT("day"),bDay);
            Item.Light=L;Item.bLightKeep=bKeep;Item.bLightDayAware=bDay;
            // The bulb: a 0.13 m unlit sphere in the light's colour, always shown.
            if(UMaterialInterface* Unlit=LoadObject<UMaterialInterface>(nullptr,TEXT("/Eidoverse/UI/M_EidoverseUnlit.M_EidoverseUnlit")))
            {
                auto* Bulb=NewObject<UStaticMeshComponent>(A);A->AddInstanceComponent(Bulb);Bulb->SetupAttachment(R);
                Bulb->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));Bulb->SetRelativeScale3D(FVector(.26));
                auto* MI=UMaterialInstanceDynamic::Create(Unlit,Bulb);MI->SetVectorParameterValue(TEXT("Color"),Color);Bulb->SetMaterial(0,MI);
                Bulb->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Bulb->SetCollisionResponseToAllChannels(ECR_Ignore);Bulb->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
                Bulb->SetCastShadow(false);Bulb->RegisterComponent();
            }
        }
        else
        {
            FString URL;
            if(UEidoverseAvatarAsset::ResolveUrl(Library,Str(E,TEXT("lib")),URL))
            {
                URL+=URL.Contains(TEXT("?"))?TEXT("&native=1"):TEXT("?native=1");
                // Async jobs can outlive EndPlay. Rooted jobs must not be
                // children of the PIE world, which forcibly marks its outers
                // as garbage while worker callbacks are still outstanding.
                // Named parts (part motions, a picture's part, part sockets) split the model.
                TArray<FString> PartNames;
                if(auto Parts=Obj(Obj(E,TEXT("data")),TEXT("parts")))for(const auto& P:Parts->Values)PartNames.AddUnique(FString(P.Key));
                {const FString Pic=Str(Obj(Obj(E,TEXT("data")),TEXT("picture")),TEXT("part"));if(!Pic.IsEmpty())PartNames.AddUnique(Pic);}
                if(auto Sockets=Obj(Obj(E,TEXT("interact")),TEXT("sockets")))for(const auto& S:Sockets->Values){const FString SP=Str(S.Value?S.Value->AsObject():nullptr,TEXT("part"));if(!SP.IsEmpty())PartNames.AddUnique(SP);}
                PartNames.Sort();if(PartNames.Num()>16)PartNames.SetNum(16);
                const FString Key=PartNames.IsEmpty()?URL:URL+TEXT("#parts=")+FString::Join(PartNames,TEXT(","));
                if(!Assets.Contains(Key)&&Assets.Num()<64){auto* Asset=NewObject<UEidoverseAvatarAsset>();Asset->bStaticModel=true;Asset->StaticMeshOuter=this;Asset->Url=URL;Asset->SplitParts=PartNames;Assets.Add(Key,Asset);}
                if(auto* Asset=Assets.Find(Key))Item.Asset=*Asset;
            }
            auto* M=NewObject<UStaticMeshComponent>(A);A->AddInstanceComponent(M);M->SetupAttachment(R);
            M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetRelativeScale3D(FVector(.25));M->SetMaterial(0,Material(TEXT("trim")));
            if(const auto* G=GeomByLib.Find(Item.Lib)){M->SetRelativeLocation(AEidoverseBridge::FromWirePosition(G->Key));M->SetRelativeScale3D(AEidoverseBridge::FromWirePosition(G->Value).GetAbs()/100.0);}M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->RegisterComponent();Item.Model=M;
        }
        ReadComponentData(A,Item,Obj(E,TEXT("data")));
        Items.Add(Id,MoveTemp(Item));
    }
    for(auto& V:Arr(Message,TEXT("transforms")))if(auto T=V->AsObject())if(auto* E=Items.Find(Str(T,TEXT("id"))))
    {
        const FString Id=Str(T,TEXT("id"));if(HeldTransforms.Contains(Id))continue;
        if(auto* Until=RemoteLeases.Find(Id))if(FPlatformTime::Seconds()<*Until)continue;
        FVector P;const auto& Q=Arr(T,TEXT("q"));if(!Vec(Arr(T,TEXT("p")),P)||Q.Num()!=4)continue;
        FQuat Rot(Q[0]->AsNumber(),Q[1]->AsNumber(),Q[2]->AsNumber(),Q[3]->AsNumber());if(Rot.ContainsNaN()||Rot.SizeSquared()<.001)continue;
        E->Target=FTransform(AEidoverseBridge::FromWireRotation(Rot.GetNormalized()),Origin+AEidoverseBridge::FromWirePosition(P),FVector(FMath::Clamp(Num(T,TEXT("scale"),1),.001,1000.)));
        // Closed-form parameters let Tick animate at frame rate; the sampled
        // transform above remains the fallback when evaluation refuses.
        auto Motion=Obj(T,TEXT("motion"));
        E->Motion=Motion?Obj(Motion,TEXT("m")):nullptr;E->MotionBase=Motion?Obj(Motion,TEXT("base")):nullptr;
        E->MountTo.Empty();E->MountPart.Empty();
        E->PartMotions=Obj(T,TEXT("parts"));
        if(auto Mount=Obj(T,TEXT("mount")))
        {
            FVector Offset;
            if(Vec(Arr(Mount,TEXT("offset")),Offset)&&!Str(Mount,TEXT("to")).IsEmpty())
            {E->MountTo=Str(Mount,TEXT("to"));E->MountPart=Str(Mount,TEXT("part"));E->MountOffsetWire=Offset;E->MountYaw=Num(Mount,TEXT("yaw"));E->OwnScale=FMath::Clamp(Num(Mount,TEXT("scale"),1),.001,1000.);}
        }
        if(!E->bPlaced&&E->Actor.IsValid()){E->Actor->SetActorTransform(E->Target);E->bPlaced=true;}
    }
    EntityCount=Items.Num();Diagnostic=FString::Printf(TEXT("%d entities · %d projection warnings"),EntityCount,Arr(Message,TEXT("warnings")).Num());
}
void AEidoverseScene::Tick(float Dt)
{
    Super::Tick(Dt);TickWeather(Dt);TickPlacedLights(FPlatformTime::Seconds());TickParts();TickPictures();TickParticles(Dt);int Loading=0;for(auto& A:Assets)Loading+=A.Value->State==UEidoverseAvatarAsset::EState::Loading;
    // Stream nearest first: start queued models in order of distance to the viewer.
    if(Loading<4)
    {
        FVector Eye=SceneOrigin;FRotator EyeRotation;
        if(auto* PC=GetWorld()->GetFirstPlayerController())PC->GetPlayerViewPoint(Eye,EyeRotation);
        TMap<UEidoverseAvatarAsset*,double> Nearest;
        for(auto& Pair:Items)if(auto* A=Pair.Value.Asset.Get())if(A->State==UEidoverseAvatarAsset::EState::Queued&&Pair.Value.Actor.IsValid())
        {const double D=FVector::DistSquared(Eye,Pair.Value.Actor->GetActorLocation());double& Best=Nearest.FindOrAdd(A,D);Best=FMath::Min(Best,D);}
        if(GhostAsset&&GhostAsset->State==UEidoverseAvatarAsset::EState::Queued)Nearest.Add(GhostAsset,0);
        Nearest.ValueSort([](double A,double B){return A<B;});
        for(auto& N:Nearest){if(Loading>=4)break;N.Key->Start();++Loading;}
    }
    // Appear gradually: each new mesh brings GPU work (ray tracing structures, Lumen
    // cards, shaders) that lands in one frame; at most two new models per frame.
    int32 Appeared=0;
    LoadedModels=FailedModels=0;
    for(auto& Pair:Items)
    {
        auto& E=Pair.Value;if(!E.Actor.IsValid())continue;
        const bool bRemoteLease=RemoteLeases.Contains(Pair.Key)&&FPlatformTime::Seconds()<RemoteLeases[Pair.Key];
        FTransform Local;
        if(E.bPlaced&&!HeldTransforms.Contains(Pair.Key)&&!bRemoteLease&&LocalTransform(Pair.Key,Local,8))E.Actor->SetActorTransform(Local);
        else if(E.bPlaced&&!HeldTransforms.Contains(Pair.Key)&&!E.Actor->GetActorTransform().Equals(E.Target,.001)){FTransform T;T.Blend(E.Actor->GetActorTransform(),E.Target,1-FMath::Exp(-Dt*14));E.Actor->SetActorTransform(T);}
        if(auto* A=E.Asset.Get())
        {
            if(A->State==UEidoverseAvatarAsset::EState::Ready&&E.Model.IsValid()){if(E.Model->GetStaticMesh()!=A->StaticMesh&&Appeared++<2){EidoLoadTrace::FScope Apply(TEXT("model-apply"),A->Url);E.Model->SetStaticMesh(A->StaticMesh);E.Model->SetRelativeLocation(FVector::ZeroVector);E.Model->SetRelativeScale3D(A->bMirrored?FVector(1,-1,1):FVector::OneVector);E.Model->EmptyOverrideMaterials();E.Model->SetCollisionProfileName(E.bCollide?TEXT("BlockAll"):TEXT("NoCollision"));bFloraDirty=true;
                for(auto& L:E.ModelLamps)if(L.IsValid())L->DestroyComponent();E.ModelLamps.Reset();E.ModelLampIntensity.Reset();E.ModelLampSlotted.Reset();
                for(auto& PC:E.PartComps)if(PC.Value.IsValid())PC.Value->DestroyComponent();E.PartComps.Reset();
                for(const auto& PM:A->PartMeshes)if(A->PartMeshObjects.IsValidIndex(PM.Mesh))
                {
                    auto* C=NewObject<UStaticMeshComponent>(E.Actor.Get());E.Actor->AddInstanceComponent(C);C->SetupAttachment(E.Model.Get());
                    C->SetStaticMesh(A->PartMeshObjects[PM.Mesh]);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->RegisterComponent();
                    E.PartComps.Add(PM.Name,C);
                }
                for(const FEidoverseAvatarLamp& Lamp:A->Lamps)
                {
                    auto* L=NewObject<UPointLightComponent>(E.Actor.Get());E.Actor->AddInstanceComponent(L);L->SetupAttachment(E.Model.Get());
                    L->SetRelativeLocation(Lamp.Offset);L->SetIntensityUnits(ELightUnits::Candelas);L->SetAttenuationRadius(1200);L->SetLightColor(Lamp.Color);
                    L->SetCastShadows(false);L->SetVisibility(false);L->RegisterComponent();
                    E.ModelLamps.Add(L);E.ModelLampIntensity.Add(Lamp.Intensity);E.ModelLampSlotted.Add(false);
                }}++LoadedModels;}
            if(A->State==UEidoverseAvatarAsset::EState::Failed)++FailedModels;
        }
        else if(E.Model.IsValid())++FailedModels;
    }
    // Retire unused jobs/assets, so hopping/editing cannot grow resident memory.
    TSet<UEidoverseAvatarAsset*> Used;for(auto& E:Items)if(E.Value.Asset.IsValid())Used.Add(E.Value.Asset.Get());
    if(GhostAsset)Used.Add(GhostAsset);TickGhost();
    for(auto It=Assets.CreateIterator();It;++It)if(!Used.Contains(It.Value())){It.Value()->Cancel();It.RemoveCurrent();}
    // Grass avoids model footprints, so it is rebuilt once loading settles; but the
    // first build doesn't wait for the slowest model in a busy world.
    if(bFloraDirty&&(Loading==0||!bFloraBuiltOnce)){bFloraDirty=Loading>0;bFloraBuiltOnce=true;EidoLoadTrace::FScope F(TEXT("flora"),FString());BuildFlora(FloraData,SceneOrigin);}
    UpdateWater();
}
double AEidoverseScene::ServerNowMs() const
{
    return FPlatformTime::Seconds()*1000+ServerClockOffsetMs;
}
void AEidoverseScene::ObserveServerTime(double ServerMs)
{
    // Messages arrive late, never early: the largest observed offset is the
    // least-delayed sample. Decay slowly so a clock step is followed.
    const double Offset=ServerMs-FPlatformTime::Seconds()*1000;
    if(!bServerClockKnown||Offset>ServerClockOffsetMs||ServerClockOffsetMs-Offset>2000)ServerClockOffsetMs=Offset;
    else ServerClockOffsetMs-=FMath::Min(5.,ServerClockOffsetMs-Offset);
    bServerClockKnown=true;
}
bool AEidoverseScene::LocalTransform(const FString& Id,FTransform& Out,int32 Depth)
{
    const auto* E=Items.Find(Id);
    if(!E||Depth<=0||!bServerClockKnown)return false;
    if(!E->MountTo.IsEmpty()&&!E->MountPart.IsEmpty())
    {
        // Cargo on a part socket rides the part's live transform (models.js execMount).
        FTransform Model;
        if(!ModelWorldTransform(E->MountTo,E->MountPart,Model))return false;
        const FQuat Yaw=AEidoverseBridge::FromWireRotation(FQuat(FVector(0,1,0),E->MountYaw));
        Out=FTransform(Model.GetRotation()*Yaw,Model.TransformPosition(AEidoverseBridge::FromWirePosition(E->MountOffsetWire)),FVector(Model.GetScale3D().X*E->OwnScale));
        return true;
    }
    if(!E->MountTo.IsEmpty())
    {
        // world = parent ∘ (offset × parent scale, then yaw about wire Y).
        // Only compose when the parent itself is locally animated; otherwise
        // the sampled server transform is already exact.
        FTransform Parent;
        if(!LocalTransform(E->MountTo,Parent,Depth-1))return false;
        const double ParentScale=Parent.GetScale3D().X;
        const FVector Offset=AEidoverseBridge::FromWirePosition(E->MountOffsetWire)*ParentScale;
        const FQuat Yaw=AEidoverseBridge::FromWireRotation(FQuat(FVector(0,1,0),E->MountYaw));
        Out=FTransform(Parent.GetRotation()*Yaw,Parent.GetLocation()+Parent.GetRotation().RotateVector(Offset),FVector(ParentScale*E->OwnScale));
        return true;
    }
    if(!E->Motion||!E->MotionBase)return false;
    EidoMotion::FBase Base;
    if(!EidoMotion::ReadBase(E->MotionBase,Base))return false;
    const auto R=EidoMotion::EvalWholeMotion(Base,E->Motion,ServerNowMs());
    if(!R.bOk)return false;
    const FQuat Rot=AEidoverseBridge::FromWireRotation(FQuat(R.Quat.X,R.Quat.Y,R.Quat.Z,R.Quat.W));
    Out=FTransform(Rot.GetNormalized(),SceneOrigin+AEidoverseBridge::FromWirePosition(FVector(R.Pos.X,R.Pos.Y,R.Pos.Z)),E->Target.GetScale3D());
    return true;
}
void AEidoverseScene::EndPlay(const EEndPlayReason::Type Reason)
{ClearGhost();DestroyWater();for(auto& A:Assets)A.Value->Cancel();Assets.Empty();for(auto& E:Items)if(E.Value.Actor.IsValid())E.Value.Actor->Destroy();Items.Empty();Super::EndPlay(Reason);}
AEidoverseExplorer::AEidoverseExplorer()
{
    // Browser locomotion (controller.js): walk 1.55 m/s, Shift 4.0, Alt 0.55; a jump is
    // vy 5.6 m/s against gravity 16 m/s^2.
    PrimaryActorTick.bCanEverTick=true;GetCapsuleComponent()->InitCapsuleSize(32,88);GetCharacterMovement()->MaxWalkSpeed=155;GetCharacterMovement()->JumpZVelocity=560;
    GetCharacterMovement()->GravityScale=16.f/9.81f;
    GetCharacterMovement()->MaxAcceleration=6000;GetCharacterMovement()->BrakingDecelerationWalking=7000;
    // The boom pivots at the head (1.45 m above the feet; the capsule centre is 88 cm up).
    Boom=CreateDefaultSubobject<USpringArmComponent>(TEXT("Boom"));Boom->SetupAttachment(GetRootComponent());
    Boom->SetRelativeLocation(FVector(0,0,57));Boom->bUsePawnControlRotation=false;Boom->bInheritPitch=Boom->bInheritYaw=Boom->bInheritRoll=false;
    Boom->SetUsingAbsoluteRotation(true);Boom->bDoCollisionTest=true;Boom->ProbeSize=18;Boom->ProbeChannel=ECC_Camera;
    Boom->bEnableCameraLag=true;Boom->CameraLagSpeed=14;Boom->TargetArmLength=CameraDistance*100;
    Eyes=CreateDefaultSubobject<UCameraComponent>(TEXT("Eyes"));Eyes->SetupAttachment(Boom,USpringArmComponent::SocketName);Eyes->bUsePawnControlRotation=false;
    // Third person: the body turns toward where it walks, not where you look.
    bUseControllerRotationYaw=false;GetCharacterMovement()->bOrientRotationToMovement=true;GetCharacterMovement()->RotationRate=FRotator(0,690,0);
}
void AEidoverseExplorer::SetCameraDistance(float Meters)
{
    CameraDistance=FMath::Clamp(Meters,0.f,16.f);
    const bool bWas=bFirstPerson;
    // Hysteresis: enter first person under 0.4 m, leave past 0.7 m (controller.js wheel).
    bFirstPerson=bWas?CameraDistance<0.7f:CameraDistance<0.4f;
    if(bFirstPerson!=bWas)
    {
        bUseControllerRotationYaw=bFirstPerson;GetCharacterMovement()->bOrientRotationToMovement=!bFirstPerson;
        for(TActorIterator<AEidoverseBridge> It(GetWorld());It;++It)if(It->HUD)It->HUD->Hint(bFirstPerson?TEXT("first person"):TEXT("third person"),1.5f);
    }
}
void AEidoverseExplorer::Zoom(float Notches)
{
    // Typing (chat, search) owns the wheel too.
    const TSharedPtr<SWidget> Focused=FSlateApplication::Get().GetKeyboardFocusedWidget();
    if(Focused.IsValid()&&(Focused->GetType()==TEXT("SEditableText")||Focused->GetType()==TEXT("SMultiLineEditableText")))return;
    // One wheel notch is ~100 deltaY in the browser, times 0.004 there.
    if(Notches!=0)SetCameraDistance(CameraDistance-Notches*.4f);
}
void AEidoverseExplorer::UpdateCamera(float Dt,APlayerController* PC)
{
    const FRotator View=PC->GetControlRotation();
    if(bFirstPerson)
    {
        // Eye slightly ahead of the head joint so the face never clips the near plane.
        Boom->TargetArmLength=0;Boom->bDoCollisionTest=false;Boom->bEnableCameraLag=false;
        Boom->SocketOffset=FVector::ZeroVector;Boom->SetWorldRotation(View);
        Eyes->SetRelativeLocationAndRotation(FVector(16,0,0),FRotator::ZeroRotator);
        if(bEyeOverride)Eyes->SetWorldLocation(EyeOverride);
        return;
    }
    // Web camPitch rests at 0.32 rad above the look direction: the orbit sits a little
    // high and looks down at the head; mouse pitch moves both together.
    const float OrbitPitch=FMath::Clamp(FRotator::NormalizeAxis(View.Pitch)-FMath::RadiansToDegrees(.32f),-85.f,80.f);
    Boom->SetWorldRotation(FRotator(OrbitPitch,View.Yaw,0));
    Boom->TargetArmLength=CameraDistance*100;Boom->bDoCollisionTest=true;Boom->bEnableCameraLag=true;
    // Shoulder offset so the body doesn't sit dead-centre over what you aim at.
    const float Shoulder=FMath::Clamp(CameraDistance*.16f,0.f,.62f)*100;
    Boom->SocketOffset=FVector(0,Shoulder,0);
    Eyes->SetRelativeLocationAndRotation(FVector::ZeroVector,FRotator::ZeroRotator);
    // Aim at a point half the shoulder over and 10 cm under the head, like the browser.
    const FVector Focus=Boom->GetComponentLocation()+FRotationMatrix(FRotator(0,View.Yaw,0)).GetUnitAxis(EAxis::Y)*Shoulder*.5f-FVector(0,0,10);
    Eyes->SetWorldRotation((Focus-Eyes->GetComponentLocation()).Rotation());
    // Never under the floor.
    FHitResult Floor;FCollisionQueryParams Q(SCENE_QUERY_STAT(EidoverseCameraFloor),false,this);
    const FVector Cam=Eyes->GetComponentLocation();
    if(GetWorld()->LineTraceSingleByChannel(Floor,Cam+FVector(0,0,200),Cam-FVector(0,0,35),ECC_Camera,Q)&&Floor.ImpactPoint.Z+35>Cam.Z&&!Floor.bStartPenetrating)
        Eyes->SetWorldLocation(FVector(Cam.X,Cam.Y,Floor.ImpactPoint.Z+35));
}
void AEidoverseExplorer::BeginPlay()
{
    Super::BeginPlay();auto* Settings=GetMutableDefault<UInputSettings>();bPreviousMouseSmoothing=Settings->bEnableMouseSmoothing;Settings->bEnableMouseSmoothing=false;
}
void AEidoverseExplorer::EndPlay(const EEndPlayReason::Type Reason)
{
    GetMutableDefault<UInputSettings>()->bEnableMouseSmoothing=bPreviousMouseSmoothing;Super::EndPlay(Reason);
}
void AEidoverseExplorer::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxisKey(EKeys::MouseX,this,&AEidoverseExplorer::LookYaw);
    Input->BindAxisKey(EKeys::MouseY,this,&AEidoverseExplorer::LookPitch);
    Input->BindAxisKey(EKeys::MouseWheelAxis,this,&AEidoverseExplorer::Zoom);
}
void AEidoverseExplorer::SetSeated(bool bOn)
{
    if(bSeated==bOn)return;
    bSeated=bOn;
    auto* Move=GetCharacterMovement();
    if(bOn){Move->StopMovementImmediately();Move->DisableMovement();SetActorEnableCollision(false);}
    else{SetActorEnableCollision(true);Move->SetMovementMode(MOVE_Walking);bSitPressed=false;}
}
void AEidoverseExplorer::LookYaw(float Value)
{if(auto* PC=Cast<APlayerController>(Controller))if(!PC->IsLookInputIgnored()){auto R=PC->GetControlRotation();R.Yaw+=Value*1.6f*EidoSettings::LookSensitivity();PC->SetControlRotation(R);}}
void AEidoverseExplorer::LookPitch(float Value)
{if(auto* PC=Cast<APlayerController>(Controller))if(!PC->IsLookInputIgnored()){auto R=PC->GetControlRotation();R.Pitch=FMath::Clamp(FMath::UnwindDegrees(R.Pitch)+Value*1.6f*EidoSettings::LookSensitivity()*(EidoSettings::InvertLook()?-1:1),-85.f,85.f);PC->SetControlRotation(R);}}
void AEidoverseExplorer::Tick(float Dt)
{
    Super::Tick(Dt);
    {
        const FVector Here=GetActorLocation();
        // Teleports (spawn, respawn) read as zero, not a 100 m/s sprint.
        FVector V=bHasLastBodyLocation&&Dt>0?(Here-LastBodyLocation)/Dt:FVector::ZeroVector;
        if(V.Size()>2000)V=FVector::ZeroVector;
        BodyVelocity=FMath::Lerp(BodyVelocity,V,FMath::Clamp(Dt*20,0.f,1.f));
        LastBodyLocation=Here;bHasLastBodyLocation=true;
    }
    auto* PC=Cast<APlayerController>(Controller);if(!PC||!PC->IsLocalController())return;
    UpdateCamera(Dt,PC);
    if(!bSeated&&TickSwimming(Dt,PC))
    {
        Posture.Empty();
        // Swimming bypasses CharacterMovement: turn the body toward the stroke (controller.js).
        const FVector H(BodyVelocity.X,BodyVelocity.Y,0);
        if(!bFirstPerson&&H.Size()>5)SetActorRotation(FMath::RInterpTo(GetActorRotation(),FRotator(0,H.Rotation().Yaw,0),Dt,8));
        else if(bFirstPerson)SetActorRotation(FRotator(0,PC->GetControlRotation().Yaw,0));
        return;
    }
    // Airtime for the clip choice; a landing clears the jump.
    if(GetCharacterMovement()->IsFalling())AirborneSeconds+=Dt;else{AirborneSeconds=0;bJumped=false;}
    bWantMove=false;
    if(PC->IsMoveInputIgnored())return;
    if(bSeated)
    {
        // Placed by the bridge; WASD or X means "get up" (localbody.js updateMountedMe).
        bWantMove=PC->IsInputKeyDown(EKeys::W)||PC->IsInputKeyDown(EKeys::A)||PC->IsInputKeyDown(EKeys::S)||PC->IsInputKeyDown(EKeys::D);
        if(PC->WasInputKeyJustPressed(EKeys::X))bSitPressed=true;
        AirborneSeconds=0;bJumped=false;
        return;
    }
    FRotator R(0,PC->GetControlRotation().Yaw,0);
    const float Forward=(PC->IsInputKeyDown(EKeys::W)?1:0)-(PC->IsInputKeyDown(EKeys::S)?1:0);
    const float Right=(PC->IsInputKeyDown(EKeys::D)?1:0)-(PC->IsInputKeyDown(EKeys::A)?1:0);
    bWantMove=Forward!=0||Right!=0;
    const bool bRun=PC->IsInputKeyDown(EKeys::LeftShift)||PC->IsInputKeyDown(EKeys::RightShift);
    const bool bCreep=PC->IsInputKeyDown(EKeys::LeftAlt)||PC->IsInputKeyDown(EKeys::RightAlt);
    GetCharacterMovement()->MaxWalkSpeed=bRun?400:bCreep?55:155;
    AddMovementInput(R.Vector(),Forward);
    AddMovementInput(FRotationMatrix(R).GetUnitAxis(EAxis::Y),Right);
    // Postures: X sit, Z lie (Ctrl+Z stays build-mode undo); walking or jumping stands up.
    const bool bCtrl=PC->IsInputKeyDown(EKeys::LeftControl)||PC->IsInputKeyDown(EKeys::RightControl)||PC->IsInputKeyDown(EKeys::LeftCommand);
    if(PC->WasInputKeyJustPressed(EKeys::X)){if(bSeatHandled)bSitPressed=true;else Posture=Posture==TEXT("sit")?FString():TEXT("sit");}
    if(PC->WasInputKeyJustPressed(EKeys::G))bFoldPressed=true;
    if(!bCtrl&&PC->WasInputKeyJustPressed(EKeys::Z))Posture=Posture==TEXT("lie")?FString():TEXT("lie");
    if(bWantMove||PC->WasInputKeyJustPressed(EKeys::SpaceBar))Posture.Empty();
    if(PC->WasInputKeyJustPressed(EKeys::SpaceBar)){if(!GetCharacterMovement()->IsFalling())bJumped=true;Jump();}
    if(PC->WasInputKeyJustReleased(EKeys::SpaceBar))StopJumping();
}

// Shared swim law (EidoWater::SwimStep, port of shared/water.js) with the
// browser controller's input mapping (client/lib/controller.js updateSwimming).
bool AEidoverseExplorer::TickSwimming(float Dt,APlayerController* PC)
{
    TActorIterator<AEidoverseScene> SceneIt(GetWorld());
    AEidoverseScene* Scene=SceneIt?*SceneIt:nullptr;
    auto* Move=GetCharacterMovement();
    auto Leave=[&]()
    {
        if(bSwimming){bSwimming=false;SwimVelocityWire=FVector::ZeroVector;Move->SetMovementMode(MOVE_Falling);}
        LocomotionMode=TEXT("walk");
        return false;
    };
    if(!Scene)return Leave();
    const FVector Origin=Scene->GetSceneOrigin();
    const double FeetOffset=GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    FVector Body=AEidoverseBridge::ToWirePosition(GetActorLocation()-Origin-FVector(0,0,FeetOffset));
    const EidoWater::FParams* Water=Scene->WaterAt(Body+FVector(0,.7,0));
    if(!Water)return Leave();
    const bool bInput=!PC->IsMoveInputIgnored();
    const bool bRise=bInput&&PC->IsInputKeyDown(EKeys::SpaceBar);
    const bool bDive=bInput&&(PC->IsInputKeyDown(EKeys::C)||PC->IsInputKeyDown(EKeys::LeftControl)||PC->IsInputKeyDown(EKeys::RightControl));
    const double Time=Scene->WaterTime();
    // Wading is still walking: hand over only where the floor is deep enough.
    FHitResult FloorHit;FCollisionQueryParams Query(SCENE_QUERY_STAT(EidoverseSwimFloor),true,this);
    const FVector Feet=GetActorLocation()-FVector(0,0,FeetOffset);
    double FloorWire=-1e9;
    if(GetWorld()->LineTraceSingleByChannel(FloorHit,Feet+FVector(0,0,50),Feet-FVector(0,0,5000),ECC_Visibility,Query))
        FloorWire=(FloorHit.ImpactPoint.Z-Origin.Z)/100;
    if(FloorWire>=EidoWater::WaveHeight(*Water,Body.X,Body.Z,Time)-1.1&&!bRise)return Leave();

    if(!bSwimming){bSwimming=true;SwimVelocityWire=AEidoverseBridge::ToWirePosition(Move->Velocity);}
    Move->DisableMovement();
    const FRotator View=PC->GetControlRotation();
    const double Fwd=bInput?(PC->IsInputKeyDown(EKeys::W)?1:0)-(PC->IsInputKeyDown(EKeys::S)?1:0):0;
    const double Strafe=bInput?(PC->IsInputKeyDown(EKeys::D)?1:0)-(PC->IsInputKeyDown(EKeys::A)?1:0):0;
    const FRotator Yaw(0,View.Yaw,0);
    const FVector Horizontal=AEidoverseBridge::ToWirePosition((Yaw.Vector()*Fwd+FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y)*Strafe)*100);
    EidoWater::FSwimInput Input;
    // UE pitch is positive looking up: swimming forward follows the view.
    const double Pitch=FMath::DegreesToRadians(FMath::Clamp(FRotator::NormalizeAxis(View.Pitch),-80.f,80.f));
    Input.Direction={Horizontal.X,double(bRise)-double(bDive)+Fwd*FMath::Sin(Pitch),Horizontal.Z};
    Input.bRise=bRise;Input.bDive=bDive;
    Input.bFast=bInput&&(PC->IsInputKeyDown(EKeys::LeftShift)||PC->IsInputKeyDown(EKeys::RightShift));

    bool bSurface=false;
    const int32 Steps=FMath::Max(1,FMath::CeilToInt(Dt*60));
    for(int32 I=0;I<Steps;++I)
    {
        const auto R=EidoWater::SwimStep({Body.X,Body.Y,Body.Z},{SwimVelocityWire.X,SwimVelocityWire.Y,SwimVelocityWire.Z},Input,*Water,Dt/Steps,Time);
        const FVector Target=Origin+AEidoverseBridge::FromWirePosition(FVector(R.Pos.X,R.Pos.Y,R.Pos.Z))+FVector(0,0,FeetOffset);
        FHitResult Hit;
        SetActorLocation(Target,true,&Hit);
        // Blocked by a hull, habitat ceiling or seabed: stop, as the browser's sweep does.
        if(Hit.bBlockingHit)SwimVelocityWire=FVector::ZeroVector;
        else SwimVelocityWire=FVector(R.Velocity.X,R.Velocity.Y,R.Velocity.Z);
        Body=AEidoverseBridge::ToWirePosition(GetActorLocation()-Origin-FVector(0,0,FeetOffset));
        bSurface=R.bSurface&&!Hit.bBlockingHit;
    }
    Move->Velocity=AEidoverseBridge::FromWirePosition(SwimVelocityWire);
    LocomotionMode=bSurface?TEXT("surface"):TEXT("swim");
    return true;
}

void AEidoverseScene::TickPlacedLights(double Now)
{
    // lightrig.js: 8 slots, keep first, then nearest (incumbents at 0.85 distance for
    // hysteresis), re-ranked every 0.6 s or after 1.5 m of camera travel; the rest glow only.
    FVector Eye=FVector::ZeroVector;FRotator View;
    if(auto* PC=UGameplayStatics::GetPlayerController(this,0))PC->GetPlayerViewPoint(Eye,View);
    const bool bRank=Now>=NextLightRank||FVector::Dist(Eye,LastLightRankEye)>150;
    const float Dayness=bServerClockKnown||LastAtmosphere?FMath::Max(0.f,FMath::Sin((WeatherHour-6)*PI/12)):1.f;
    struct FCand{FEidoverseSceneItem* Item;double Score;int32 Lamp=-1;};TArray<FCand> Cands;
    for(auto& Pair:Items)
    {
        FEidoverseSceneItem& E=Pair.Value;
        for(int32 I=0;I<E.ModelLamps.Num();++I)if(UPointLightComponent* ML=E.ModelLamps[I].Get())
        {
            ML->SetIntensity(E.ModelLampIntensity[I]*FMath::Pow(6.f,.3f)*FMath::Square(1-Dayness));
            if(bRank){double D=FVector::Dist(Eye,ML->GetComponentLocation());if(E.ModelLampSlotted[I])D*=.85;Cands.Add({&E,2e9+D,I});}
        }
        UPointLightComponent* L=E.Light.Get();if(!L)continue;
        // Inverse-square vs the browser's d^-1.7: compensate by (range/2)^0.3; dim by day.
        const float Day=E.bLightDayAware?FMath::Square(1-Dayness):1.f;
        L->SetIntensity(E.LightIntensity*FMath::Pow(FMath::Max(1.f,E.LightRange*.5f),.3f)*Day);
        if(bRank){double D=FVector::Dist(Eye,L->GetComponentLocation());if(E.bLightSlotted)D*=.85;Cands.Add({&E,(E.bLightKeep?0.:1e9)+D});}
    }
    if(!bRank)return;
    NextLightRank=Now+.6;LastLightRankEye=Eye;
    Cands.Sort([](const FCand& A,const FCand& B){return A.Score<B.Score;});
    for(int32 I=0;I<Cands.Num();++I)
    {
        FEidoverseSceneItem& E=*Cands[I].Item;
        if(Cands[I].Lamp>=0){const bool bOn=I<8;E.ModelLampSlotted[Cands[I].Lamp]=bOn;if(UPointLightComponent* ML=E.ModelLamps[Cands[I].Lamp].Get())ML->SetVisibility(bOn);continue;}
        E.bLightSlotted=I<8||E.bLightKeep;
        if(UPointLightComponent* L=E.Light.Get())L->SetVisibility(E.bLightSlotted);
    }
}

void AEidoverseScene::SetGeom(const TSharedPtr<FJsonObject>& Geom)
{
    if(!Geom)return;
    for(const auto& Pair:Geom->Values)
    {
        const TSharedPtr<FJsonObject> O=Pair.Value?Pair.Value->AsObject():nullptr;
        const TSharedPtr<FJsonObject> B=Obj(O,TEXT("bbox"));if(!B)continue;
        FVector C,S;
        auto Read=[&B](const TCHAR* K,FVector& V){const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!B->TryGetArrayField(K,A)||A->Num()!=3)return false;for(int32 I=0;I<3;++I)V[I]=(*A)[I]->AsNumber();return !V.ContainsNaN()&&V.GetAbsMax()<1e4;};
        if(!Read(TEXT("center"),C)||!Read(TEXT("size"),S))continue;
        GeomByLib.Add(FString(Pair.Key),{C,S});
    }
    // Placeholders still waiting for their model take the true size at once.
    for(auto& Pair:Items)
        if(UStaticMeshComponent* M=Pair.Value.Model.Get())
            if(const auto* G=GeomByLib.Find(Pair.Value.Lib))
                if(M->GetStaticMesh()&&M->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Engine/BasicShapes/Cube")))
                {M->SetRelativeLocation(AEidoverseBridge::FromWirePosition(G->Key));M->SetRelativeScale3D(AEidoverseBridge::FromWirePosition(G->Value).GetAbs()/100.0);}
}

namespace
{
    // glTF numbers (metres, right-handed, row-vector) -> mesh space (UE cm): v' = (z, -x, y) * 100.
    const FMatrix& GltfToUE() { static const FMatrix B(FPlane(0,-100,0,0),FPlane(0,0,100,0),FPlane(100,0,0,0),FPlane(0,0,0,1)); return B; }
}

bool AEidoverseScene::PartDelta(const FString& Id,const FString& Part,FTransform& OutDelta) const
{
    OutDelta=FTransform::Identity;
    const FEidoverseSceneItem* E=Items.Find(Id);if(!E)return false;
    const UStaticMeshComponent* C=E->PartComps.FindRef(Part).Get();if(!C)return false;
    OutDelta=C->GetRelativeTransform();
    return true;
}

bool AEidoverseScene::ModelWorldTransform(const FString& Id,const FString& Part,FTransform& Out) const
{
    const FEidoverseSceneItem* E=Items.Find(Id);
    if(!E||!E->Actor.IsValid())return false;
    Out=E->Actor->GetActorTransform();
    FTransform Delta;
    if(!Part.IsEmpty()&&PartDelta(Id,Part,Delta))Out=Delta*Out;
    return true;
}

void AEidoverseScene::TickParts()
{
    if(!bServerClockKnown)return;
    FVector Eye=SceneOrigin;FRotator View;
    if(auto* PC=GetWorld()->GetFirstPlayerController())PC->GetPlayerViewPoint(Eye,View);
    const double Now=ServerNowMs();
    const FMatrix& B=GltfToUE();const FMatrix BInv=B.Inverse();
    for(auto& Pair:Items)
    {
        FEidoverseSceneItem& E=Pair.Value;
        if(E.PartComps.IsEmpty()||!E.Actor.IsValid())continue;
        // Closed-form: a far part skipped now lands at the right phase later (90 m gate).
        if(FVector::DistSquared(Eye,E.Actor->GetActorLocation())>9000.*9000.)continue;
        const UEidoverseAvatarAsset* A=E.Asset.Get();if(!A)continue;
        for(const auto& PM:A->PartMeshes)
        {
            UStaticMeshComponent* C=E.PartComps.FindRef(PM.Name).Get();if(!C)continue;
            const TSharedPtr<FJsonObject> M=Obj(E.PartMotions,*PM.Name);
            EidoMotion::FPartPose Rest,Live;
            const FVector RP=PM.RestLocal.GetTranslation();const FQuat RQ=PM.RestLocal.GetRotation();
            Rest.Pos={RP.X,RP.Y,RP.Z};Rest.Quat={RQ.X,RQ.Y,RQ.Z,RQ.W};
            if(!M||!EidoMotion::EvalPartMotion(Rest,M,Now,Live)){if(!C->GetRelativeTransform().Equals(FTransform::Identity))C->SetRelativeTransform(FTransform::Identity);continue;}
            // Model-space delta (row-vector): D = PW^-1 * Lrest^-1 * Llive * PW, then into UE.
            const FTransform LiveLocal(FQuat(Live.Quat.X,Live.Quat.Y,Live.Quat.Z,Live.Quat.W),FVector(Live.Pos.X,Live.Pos.Y,Live.Pos.Z),PM.RestLocal.GetScale3D());
            const FMatrix D=PM.ParentWorld.Inverse()*PM.RestLocal.ToMatrixWithScale().Inverse()*LiveLocal.ToMatrixWithScale()*PM.ParentWorld;
            C->SetRelativeTransform(FTransform(BInv*D*B));
        }
    }
}

FString AEidoverseScene::DescribePart(const FString& Id,const FString& Part) const
{
    FTransform D;if(!PartDelta(Id,Part,D))return TEXT("no part");
    return FString::Printf(TEXT("rot=%s loc=%s"),*D.Rotator().ToString(),*D.GetLocation().ToString());
}
