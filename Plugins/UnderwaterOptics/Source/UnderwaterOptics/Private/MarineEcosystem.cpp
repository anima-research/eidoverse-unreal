#include "MarineEcosystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

AMarineEcosystem::AMarineEcosystem()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

float AMarineEcosystem::Ground(float X, float Y) const
{
    const float GX = FMath::Clamp((X+10000.f)/500.f,0.f,59.999f);
    const float GY = FMath::Clamp((Y+10000.f)/500.f,0.f,59.999f);
    const int32 IX = FMath::FloorToInt(GX), IY = FMath::FloorToInt(GY);
    return FMath::Lerp(FMath::Lerp(Bed[IY*61+IX],Bed[IY*61+IX+1],GX-IX),
        FMath::Lerp(Bed[(IY+1)*61+IX],Bed[(IY+1)*61+IX+1],GX-IX),GY-IY);
}

void AMarineEcosystem::BeginPlay()
{
    Super::BeginPlay();
    if (SpeciesMeshes.Num()<4) return;
    // Cache the real terrain AND solid reef tops, not a second terrain formula.
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MarineBed),true,this);
    for (int32 Y=0;Y<=60;++Y) for (int32 X=0;X<=60;++X)
    {
        FHitResult H; const FVector P(-10000+X*500,-10000+Y*500,-180);
        Bed.Add(GetWorld()->LineTraceSingleByChannel(H,P,P-FVector(0,0,30000),ECC_Visibility,Q)?H.ImpactPoint.Z:-20100);
    }
    for (int32 S=0;S<4;++S)
    {
        auto* C=NewObject<UInstancedStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent); C->SetMobility(EComponentMobility::Movable);
        C->SetStaticMesh(SpeciesMeshes[S]); C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCastShadow(false); C->NumCustomDataFloats=2;
        C->SetCullDistances(9000,14000); C->RegisterComponent(); Groups.Add(C);
    }
    const FVector Centers[]={{4200,-800,0},{10500,700,0},{7100,4300,0},{2600,4500,0},
        {10100,-3400,0},{-1800,6000,0},{1200,8200,0},{13000,5000,0},
        {4700,1800,0},{10000,4300,0},{2800,-2300,0},{11900,2500,0},
        {4000,9000,0},{-2300,1800,0},{13500,-1600,0},{8500,10000,0}};
    FRandomStream R(Seed);
    for (int32 I=0;I<16;++I)
    {
        const int32 S= I<6?0:I<8?1:I<12?2:I<14?1:3;
        FSchool G; G.Center=Centers[I]; G.Radius=S==2?650:1100;
        G.Speed=S==0?105:S==1?170:S==2?48:65; G.Clearance=S==3?140:S==2?400:950; G.Species=S;
        G.Center.Z=FMath::Min(-400.f,Ground(G.Center.X,G.Center.Y)+G.Clearance);
        Schools.Add(G);
        const int32 Count=S==0?52:S==1?28:S==2?12:3;
        for (int32 J=0;J<Count;++J)
        {
            FSwimmer F; F.School=I;F.Species=S;F.Size=R.FRandRange(.75,1.25);F.Phase=R.FRandRange(0,6.283f);
            F.P=G.Center+FVector(R.FRandRange(-350,350),R.FRandRange(-350,350),R.FRandRange(-100,100));
            F.P.Z=FMath::Min(-220.f,FMath::Max(F.P.Z,Ground(F.P.X,F.P.Y)+100));
            F.V=FVector(FMath::Cos(I*1.7f),FMath::Sin(I*1.7f),0)*G.Speed;F.Avoid=FVector::ZeroVector;
            F.Instance=Groups[S]->AddInstance(FTransform(F.V.Rotation(),F.P,FVector(F.Size)));
            Groups[S]->SetCustomDataValue(F.Instance,0,F.Phase,false);
            Groups[S]->SetCustomDataValue(F.Instance,1,G.Speed/105.f,false);
            Fish.Add(F);
        }
    }
    FishCount=Fish.Num();
}

void AMarineEcosystem::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Fish.Num()) return;
    Accumulator+=DeltaSeconds;
    if (Accumulator<.05f) return;
    const float DT=FMath::Min(Accumulator,.1f); Accumulator=0;
    const float T=GetWorld()->GetTimeSeconds();
    APawn* Player=UGameplayStatics::GetPlayerPawn(this,0);
    const FVector PlayerP=Player?Player->GetActorLocation():FVector(1e8);
    // Budget eight swept collision probes per simulation step. Cached avoidance
    // is persistent between probes; no per-fish physics bodies or per-frame traces.
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MarineAvoidance),true,this);
    for (int32 N=0;N<8;++N)
    {
        auto& F=Fish[ProbeCursor++%Fish.Num()]; FHitResult H;
        if (GetWorld()->SweepSingleByChannel(H,F.P,F.P+F.V.GetSafeNormal()*350,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(18),Q))
            F.Avoid=H.ImpactNormal*450+FVector(0,0,100);
        else F.Avoid=FVector::ZeroVector;
    }
    TArray<FVector> NextV; NextV.Reserve(Fish.Num());
    for (int32 I=0;I<Fish.Num();++I)
    {
        const auto& F=Fish[I]; const auto& G=Schools[F.School];
        FVector Cohesion=FVector::ZeroVector, Alignment=FVector::ZeroVector, Separation=FVector::ZeroVector; int32 Neighbors=0;
        for (const auto& Other:Fish)
        {
            if (Other.School!=F.School || &Other==&F) continue;
            const FVector D=F.P-Other.P; const float D2=D.SizeSquared();
            if (D2<450*450) {Cohesion+=Other.P;Alignment+=Other.V;++Neighbors;}
            if (D2<95*95 && D2>1) Separation+=D/D2*8500;
        }
        const float A=T*(G.Speed/G.Radius)*.65f+F.School*1.71f;
        FVector Target=G.Center+FVector(FMath::Cos(A)*G.Radius,FMath::Sin(A*.83f)*G.Radius*.7f,FMath::Sin(A*1.3f)*100);
        FVector Force=(Target-F.P)*.10f+Separation*1.8f+F.Avoid;
        if (Neighbors) Force+=(Cohesion/Neighbors-F.P)*.12f+(Alignment/Neighbors-F.V)*.65f;
        Force+=FVector(FMath::Sin(T*.7f+F.Phase),FMath::Cos(T*.53f+F.Phase),FMath::Sin(T*.4f+F.Phase)*.3f)*24;
        const FVector Away=F.P-PlayerP; const float PD=Away.Size();
        if (PD<550) Force+=Away.GetSafeNormal()*(550-PD)*1.6f;
        const float Floor=Ground(F.P.X+F.V.X*1.2f,F.P.Y+F.V.Y*1.2f);
        const float DesiredZ=F.Species==3?Floor+G.Clearance:Target.Z;
        Force.Z+=(DesiredZ-F.P.Z)*.55f;
        if (F.P.Z<Floor+150) Force.Z+=(Floor+150-F.P.Z)*3;
        if (F.P.Z>-220) Force.Z-= (F.P.Z+220)*4;
        FVector V=F.V+Force.GetClampedToMaxSize(G.Speed*1.7f)*DT;
        V=V.GetSafeNormal()*FMath::Clamp(V.Size(),G.Speed*.65f,G.Speed*1.45f);
        V.Z=FMath::Clamp(V.Z,-G.Speed*.35f,G.Speed*.35f);
        NextV.Add(V);
    }
    for (int32 I=0;I<Fish.Num();++I)
    {
        auto& F=Fish[I]; const FVector Old=F.V;F.V=NextV[I];F.P+=F.V*DT;
        FRotator Rot=F.V.Rotation();
        Rot.Roll=FMath::Clamp(FVector::CrossProduct(Old.GetSafeNormal(),F.V.GetSafeNormal()).Z*600.f,-20.f,20.f);
        Groups[F.Species]->UpdateInstanceTransform(F.Instance,FTransform(Rot,F.P,FVector(F.Size)),false,false,true);
    }
    for (auto& C:Groups) C->MarkRenderStateDirty();
}
