#include "SurfaceBoardingPlatform.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "InputCoreTypes.h"

ASurfaceBoardingPlatform::ASurfaceBoardingPlatform()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
}

bool ASurfaceBoardingPlatform::TryUseLadder(APawn* Pawn)
{
    if (!Pawn || !Pawn->ActorHasTag(TEXT("OceanDiver")) || Pawn->GetAttachParentActor()) return false;
    const FVector P=Pawn->GetActorLocation()-GetActorLocation();
    const bool AtFoot=FVector2D(P.X,P.Y+600).Size()<240 && P.Z>-330 && P.Z<160;
    const bool AtTop=BoardingState==2 && FMath::Abs(P.X)<190 && P.Y<-230;
    if (BoardingState==0 && AtFoot)
    {
        Boarder=Pawn; ClimbStart=Pawn->GetActorLocation(); Progress=0; BoardingState=1;
        Pawn->Tags.AddUnique(TEXT("SurfaceBoarded"));
        if (auto* M=Cast<UFloatingPawnMovement>(Pawn->GetMovementComponent())) {OldSpeed=M->MaxSpeed;M->MaxSpeed=0;M->StopMovementImmediately();}
        return true;
    }
    if (AtTop)
    {
        ClimbStart=Pawn->GetActorLocation(); Progress=0;BoardingState=3;
        if (auto* M=Cast<UFloatingPawnMovement>(Pawn->GetMovementComponent())) {M->MaxSpeed=0;M->StopMovementImmediately();}
        return true;
    }
    return false;
}

void ASurfaceBoardingPlatform::Release()
{
    if (auto* P=Boarder.Get())
    {
        P->Tags.Remove(TEXT("SurfaceBoarded"));
        if (auto* M=Cast<UFloatingPawnMovement>(P->GetMovementComponent())) M->MaxSpeed=OldSpeed;
    }
    Boarder.Reset(); BoardingState=0;FallSpeed=0;
}

void ASurfaceBoardingPlatform::Tick(float DT)
{
    Super::Tick(DT);
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    APawn* P=PC?PC->GetPawn():nullptr;
    if (!P || !P->ActorHasTag(TEXT("OceanDiver"))) {if (BoardingState) Release(); return;}
    const FVector Local=P->GetActorLocation()-GetActorLocation();
    const bool Near=FVector2D(Local.X,Local.Y+600).Size()<240 && Local.Z>-330 && Local.Z<420;
    if ((Near || (BoardingState==2 && FMath::Abs(Local.X)<190 && Local.Y<-230)) && GEngine)
        GEngine->AddOnScreenDebugMessage(81584,.15f,FColor(200,230,220),BoardingState==2?TEXT("E  —  Climb down to water"):TEXT("E  —  Climb research-platform ladder"));
    if (PC->WasInputKeyJustPressed(EKeys::E)) TryUseLadder(P);
    if (!BoardingState) return;
    if (!Boarder.IsValid() || Boarder.Get()!=P) {Release();return;}
    if (BoardingState==1 || BoardingState==3)
    {
        const bool Up=BoardingState==1;
        Progress=FMath::Min(1.f,Progress+DT/3.2f);
        FVector A=GetActorLocation()+FVector(0,-605,-100);
        FVector B=GetActorLocation()+FVector(0,-605,DeckHeight+90);
        FVector C=GetActorLocation()+FVector(0,-330,DeckHeight+90);
        const float U=Up?Progress:1-Progress;
        FVector Target=U<.75f?FMath::Lerp(A,B,U/.75f):FMath::Lerp(B,C,(U-.75f)/.25f);
        // First 0.3 s eases onto the ladder; the remaining path follows its rungs.
        Target=FMath::Lerp(ClimbStart,Target,FMath::Min(1.f,Progress*10));
        P->SetActorLocation(Target,false);
        if (auto* M=P->GetMovementComponent()) M->StopMovementImmediately();
        if (Progress>=1)
        {
            if (Up) {BoardingState=2;if (auto* M=Cast<UFloatingPawnMovement>(P->GetMovementComponent())) M->MaxSpeed=240;}
            else Release();
        }
    }
    else if (BoardingState==2)
    {
        if (FMath::Abs(Local.X)>610 || FMath::Abs(Local.Y)>510) {BoardingState=4;FallSpeed=0;return;}
        FVector Pos=P->GetActorLocation(); Pos.Z=GetActorLocation().Z+DeckHeight+90;
        P->SetActorLocation(Pos,true);
        if (auto* M=P->GetMovementComponent()) M->Velocity.Z=0;
    }
    else if (BoardingState==4)
    {
        FallSpeed+=980*DT; FVector Pos=P->GetActorLocation();Pos.Z-=FallSpeed*DT;
        P->SetActorLocation(Pos,true);
        if (auto* M=P->GetMovementComponent()) M->Velocity.Z=0;
        if (Pos.Z<GetActorLocation().Z-90) Release();
    }
}

void ASurfaceBoardingPlatform::EndPlay(const EEndPlayReason::Type Reason)
{
    Release();Super::EndPlay(Reason);
}
