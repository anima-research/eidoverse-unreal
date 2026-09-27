#include "EidoverseScene.h"
#include "EidoverseMotion.h"
#include "EidoverseWater.h"
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
    SceneOrigin=Origin;
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
        FEidoverseSceneItem Item;Item.Actor=A;Item.Revision=Revision;Item.Interact=Obj(E,TEXT("interact"));Item.bCollide=Str(E,TEXT("collide"))!=TEXT("none");
        if(auto Plan=Obj(E,TEXT("structure")))BuildStructure(A,Plan);
        else if(Str(E,TEXT("kind"))==TEXT("light"))
        {
            auto* L=NewObject<UPointLightComponent>(A);A->AddInstanceComponent(L);L->SetupAttachment(R);L->SetIntensityUnits(ELightUnits::Candelas);L->SetIntensity(FMath::Clamp(Num(E,TEXT("intensity")),0.,100000.));L->SetAttenuationRadius(FMath::Clamp(Num(E,TEXT("range")),0.,200.)*100);
            uint32 C=uint32(Num(E,TEXT("color")));L->SetLightColor(FLinearColor::FromSRGBColor(FColor((C>>16)&255,(C>>8)&255,C&255)));L->SetCastShadows(true);L->RegisterComponent();
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
                if(!Assets.Contains(URL)&&Assets.Num()<64){auto* Asset=NewObject<UEidoverseAvatarAsset>();Asset->bStaticModel=true;Asset->StaticMeshOuter=this;Asset->Url=URL;Assets.Add(URL,Asset);}
                if(auto* Asset=Assets.Find(URL))Item.Asset=*Asset;
            }
            auto* M=NewObject<UStaticMeshComponent>(A);A->AddInstanceComponent(M);M->SetupAttachment(R);
            M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetRelativeScale3D(FVector(.25));M->SetMaterial(0,Material(TEXT("trim")));M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->RegisterComponent();Item.Model=M;
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
        E->MountTo.Empty();
        if(auto Mount=Obj(T,TEXT("mount")))
        {
            FVector Offset;
            if(Vec(Arr(Mount,TEXT("offset")),Offset)&&!Str(Mount,TEXT("to")).IsEmpty())
            {E->MountTo=Str(Mount,TEXT("to"));E->MountOffsetWire=Offset;E->MountYaw=Num(Mount,TEXT("yaw"));E->OwnScale=FMath::Clamp(Num(Mount,TEXT("scale"),1),.001,1000.);}
        }
        if(!E->bPlaced&&E->Actor.IsValid()){E->Actor->SetActorTransform(E->Target);E->bPlaced=true;}
    }
    EntityCount=Items.Num();Diagnostic=FString::Printf(TEXT("%d entities · %d projection warnings"),EntityCount,Arr(Message,TEXT("warnings")).Num());
}
void AEidoverseScene::Tick(float Dt)
{
    Super::Tick(Dt);TickWeather(Dt);int Loading=0;for(auto& A:Assets)Loading+=A.Value->State==UEidoverseAvatarAsset::EState::Loading;
    for(auto& A:Assets)if(Loading<2&&A.Value->State==UEidoverseAvatarAsset::EState::Queued){A.Value->Start();++Loading;}
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
            if(A->State==UEidoverseAvatarAsset::EState::Ready&&E.Model.IsValid()){if(E.Model->GetStaticMesh()!=A->StaticMesh){E.Model->SetStaticMesh(A->StaticMesh);E.Model->SetRelativeScale3D(A->bMirrored?FVector(1,-1,1):FVector::OneVector);E.Model->EmptyOverrideMaterials();E.Model->SetCollisionProfileName(E.bCollide?TEXT("BlockAll"):TEXT("NoCollision"));bFloraDirty=true;}++LoadedModels;}
            if(A->State==UEidoverseAvatarAsset::EState::Failed)++FailedModels;
        }
        else if(E.Model.IsValid())++FailedModels;
    }
    // Retire unused jobs/assets, so hopping/editing cannot grow resident memory.
    TSet<UEidoverseAvatarAsset*> Used;for(auto& E:Items)if(E.Value.Asset.IsValid())Used.Add(E.Value.Asset.Get());
    for(auto It=Assets.CreateIterator();It;++It)if(!Used.Contains(It.Value())){It.Value()->Cancel();It.RemoveCurrent();}
    if(bFloraDirty&&Loading==0){bFloraDirty=false;BuildFlora(FloraData,SceneOrigin);}
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
{DestroyWater();for(auto& A:Assets)A.Value->Cancel();Assets.Empty();for(auto& E:Items)if(E.Value.Actor.IsValid())E.Value.Actor->Destroy();Items.Empty();Super::EndPlay(Reason);}
AEidoverseExplorer::AEidoverseExplorer()
{
    PrimaryActorTick.bCanEverTick=true;GetCapsuleComponent()->InitCapsuleSize(32,88);GetCharacterMovement()->MaxWalkSpeed=400;GetCharacterMovement()->JumpZVelocity=430;
    GetCharacterMovement()->MaxAcceleration=6000;GetCharacterMovement()->BrakingDecelerationWalking=7000;
    auto* Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("Eyes"));Camera->SetupAttachment(GetRootComponent());Camera->SetRelativeLocation(FVector(0,0,72));Camera->bUsePawnControlRotation=true;bUseControllerRotationYaw=true;
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
}
void AEidoverseExplorer::LookYaw(float Value)
{if(auto* PC=Cast<APlayerController>(Controller))if(!PC->IsLookInputIgnored()){auto R=PC->GetControlRotation();R.Yaw+=Value*1.6f;PC->SetControlRotation(R);}}
void AEidoverseExplorer::LookPitch(float Value)
{if(auto* PC=Cast<APlayerController>(Controller))if(!PC->IsLookInputIgnored()){auto R=PC->GetControlRotation();R.Pitch=FMath::Clamp(FMath::UnwindDegrees(R.Pitch)+Value*1.6f,-85.f,85.f);PC->SetControlRotation(R);}}
void AEidoverseExplorer::Tick(float Dt)
{
    Super::Tick(Dt);auto* PC=Cast<APlayerController>(Controller);if(!PC||!PC->IsLocalController())return;
    if(TickSwimming(Dt,PC))return;
    if(PC->IsMoveInputIgnored())return;
    FRotator R(0,PC->GetControlRotation().Yaw,0);
    AddMovementInput(R.Vector(),(PC->IsInputKeyDown(EKeys::W)?1:0)-(PC->IsInputKeyDown(EKeys::S)?1:0));
    AddMovementInput(FRotationMatrix(R).GetUnitAxis(EAxis::Y),(PC->IsInputKeyDown(EKeys::D)?1:0)-(PC->IsInputKeyDown(EKeys::A)?1:0));
    if(PC->WasInputKeyJustPressed(EKeys::SpaceBar))Jump();if(PC->WasInputKeyJustReleased(EKeys::SpaceBar))StopJumping();
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
