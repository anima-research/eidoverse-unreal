// My own body's physics, as localbody.js / controller.js run it: going limp (R), being
// knocked over (consented shoves and blasts), getting up (movement or R), and flight (F).
// The sim is the owner's alone; everyone else sees the streamed 'ragdoll' clip + pose.
#include "EidoverseBridge.h"
#include "EidoverseUI.h"
#include "EidoverseScene.h"
#include "EidoverseAvatar.h"
#include "EidoverseRagdoll.h"
#include "EidoverseFlight.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

struct FEidoFlightRuntime
{
    EidoFlight::FConfig Cfg;
    TOptional<EidoFlight::FState> State;
    TOptional<EidoFlight::FFlightEnd> LastEnd;
    float Effort = 0;
};

namespace
{
    TSharedPtr<FJsonObject> PoseJson(const TMap<FName, FQuat>& Pose)
    {
        auto J = MakeShared<FJsonObject>();
        for (const auto& P : Pose)
            J->SetArrayField(P.Key.ToString(), {MakeShared<FJsonValueNumber>(P.Value.X), MakeShared<FJsonValueNumber>(P.Value.Y), MakeShared<FJsonValueNumber>(P.Value.Z), MakeShared<FJsonValueNumber>(P.Value.W)});
        return J;
    }
}

void AEidoverseBridge::GoLimp()
{
    // localbody.js goLimp: topple along my facing at 0.9 + min(1.2, speed x 0.35) m/s.
    const APawn* Me = UGameplayStatics::GetPlayerPawn(this, 0); if (!Me) return;
    const FVector F = ToWirePosition(Me->GetActorForwardVector() * 100).GetSafeNormal();
    const auto* X = Cast<AEidoverseExplorer>(Me);
    const double Speed = X ? FVector(X->BodyVelocity.X, X->BodyVelocity.Y, 0).Size() / 100 : 0;
    GoLimpWith(FVector(F.X, 0, F.Z) * (.9 + FMath::Min(1.2, Speed * .35)), FString());
}

void AEidoverseBridge::GoLimpWith(const FVector& LeanWire, const FString& By)
{
    auto* Explorer = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0));
    UEidoverseAvatarMesh* M = SelfBody.AvatarMesh.Get();
    if (!Explorer || !M || !M->Avatar || Explorer->bSeated || !SeatTo.IsEmpty() || Climb.IsSet() || Helm.bActive) return;
    const FVector Lean = LeanWire.GetClampedToMaxSize(6);
    if (Rag.IsValid() && !Rag->IsDone()) { Rag->Impulse(Lean); if (HUD && !By.IsEmpty()) HUD->Hint(By + TEXT(" knocked you over"), 3); return; }
    EidoRagdoll::FInit Init;
    if (!M->RestWorldWire(Init.RestWorld, OriginCentimeters)) return;
    const FVector F = ToWirePosition(Explorer->GetActorForwardVector() * 100);
    Init.RootPosition = ToWirePosition(M->GetComponentLocation() - OriginCentimeters);
    Init.RootYaw = FMath::Atan2(F.X, F.Z);
    Init.bVrm0 = M->Avatar->bVRM0;
    // Park everything not driven at identity, keep the driven bones where they are now
    // (or the settled pose when already down: a shove re-tumbles from the floor).
    const TMap<FName, FQuat>& From = bDowned && DownedPose.Num() ? DownedPose : M->LastNormalized;
    for (const FName B : EidoRagdoll::DrivenBones()) if (const FQuat* Q = From.Find(B)) Init.StartLocal.Add(B, *Q);
    Init.Lean = Lean;
    // The world under the body: the highest thing a vertical ray meets.
    const FVector Origin = OriginCentimeters;
    UWorld* World = GetWorld();
    TWeakObjectPtr<AEidoverseExplorer> WeakX(Explorer);
    Init.HeightAt = [World, Origin, WeakX](double Xw, double Zw)
    {
        const FVector Top = Origin + FromWirePosition(FVector(Xw, 200, Zw)), Bottom = Origin + FromWirePosition(FVector(Xw, -200, Zw));
        FCollisionQueryParams Q(SCENE_QUERY_STAT(EidoverseRagdollGround), true); if (WeakX.IsValid()) Q.AddIgnoredActor(WeakX.Get());
        FHitResult Hit;
        return World && World->LineTraceSingleByChannel(Hit, Top, Bottom, ECC_Visibility, Q) ? (Hit.ImpactPoint.Z - Origin.Z) / 100 : 0.;
    };
    Init.ResolveColliders = [](FVector& P, const EidoRagdoll::FHeightFn& HeightAt, double, double) { return HeightAt(P.X, P.Z); };
    Rag = MakeShared<EidoRagdoll::FRagdoll>(Init);
    bDowned = true;
    Explorer->SetSeated(true); Explorer->Posture.Empty();
    M->SetLimp(true);
    ClearReach();
    if (HUD) HUD->Hint(By.IsEmpty() ? TEXT("limp — move to get up") : By + TEXT(" knocked you over — move to get up"), 3);
}

void AEidoverseBridge::GetUp()
{
    auto* Explorer = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!bDowned) return;
    Rag.Reset(); bDowned = false; DownedPose.Reset();
    SetSelfHeldPose(nullptr, false);
    if (UEidoverseAvatarMesh* M = SelfBody.AvatarMesh.Get()) M->SetLimp(false);
    if (Explorer)
    {
        // Resume from where the body lies; gravity finds the floor.
        const double Half = Explorer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        Explorer->SetActorLocation(OriginCentimeters + FromWirePosition(DownedRootWire) + FVector(0, 0, Half + 30), false, nullptr, ETeleportType::TeleportPhysics);
        Explorer->SetSeated(false);
    }
}

bool AEidoverseBridge::HandleFlyKey()
{
    auto* Explorer = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0));
    UEidoverseAvatarMesh* M = SelfBody.AvatarMesh.Get();
    if (!Explorer || !M || bSpectator) return false;
    if (!FlightRt) FlightRt = MakeShared<FEidoFlightRuntime>();
    if (FlightRt->State.IsSet())
    {
        // F while flying: the controller simply lets go (the state resets, wings rest).
        FlightRt->State.Reset(); FlightRt->Effort = 0;
        Explorer->SetSeated(false); Explorer->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        return true;
    }
    const TArray<FString> Bones = M->BoneNames();
    const EidoFlight::FVerdict Verdict = EidoFlight::ResolveFlight(RightsFly, &Bones);
    if (!Verdict.bEnabled && !RightsFly.Get(false)) return false;   // no grant here: F stays the punt key
    const double Half = Explorer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Feet = Explorer->GetActorLocation() - FVector(0, 0, Half);
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(EidoverseFlightGround), true, Explorer);
    const double Ground = GetWorld()->LineTraceSingleByChannel(Hit, Feet + FVector(0, 0, 50), Feet - FVector(0, 0, 5000), ECC_Visibility, Q) ? (Hit.ImpactPoint.Z - OriginCentimeters.Z) / 100 : (Feet.Z - OriginCentimeters.Z) / 100;
    const FVector P = ToWirePosition(Feet - OriginCentimeters), F = ToWirePosition(Explorer->GetActorForwardVector() * 100);
    TOptional<EidoFlight::FState> Out;
    const FString Msg = EidoFlight::StartFlight(FlightRt->Cfg, Verdict, bWingsFolded, {P.X, P.Y, P.Z}, FMath::Atan2(F.X, F.Z), Ground, Out);
    if (HUD && !Msg.IsEmpty()) HUD->Hint(Msg, 4);
    if (Out.IsSet()) { FlightRt->State = Out; Explorer->SetSeated(true); Explorer->Posture.Empty(); }
    return true;
}

FString AEidoverseBridge::FlightDiagnostic()
{
    UEidoverseAvatarMesh* M = SelfBody.AvatarMesh.Get();
    if (!FlightRt) FlightRt = MakeShared<FEidoFlightRuntime>();
    const TArray<FString> Bones = M ? M->BoneNames() : TArray<FString>();
    const EidoFlight::FVerdict Verdict = EidoFlight::ResolveFlight(RightsFly, &Bones);
    return EidoFlight::FlightReport(Verdict, Bones.Num(), FlightRt->State.GetPtrOrNull(), 0, FlightRt->LastEnd.GetPtrOrNull());
}

void AEidoverseBridge::TickBodyPhysics(AEidoverseExplorer* Explorer, float Dt)
{
    APlayerController* PC = Cast<APlayerController>(Explorer->GetController());
    UEidoverseAvatarMesh* M = SelfBody.AvatarMesh.Get();
    const double Half = Explorer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    // R: limp / get up (not while typing or building; HUD gates the keyboard for text).
    const bool bTyping = HUD && HUD->IsChatFocused();
    if (PC && !bTyping && PC->WasInputKeyJustPressed(EKeys::R) && !(HUD && HUD->IsFrameOpen(TEXT("build"))))
    { if (bDowned) GetUp(); else GoLimp(); }
    if (bDowned)
    {
        // Any movement key (or Space) gets up (main.js).
        if (PC && !bTyping && (Explorer->bWantMove || PC->WasInputKeyJustPressed(EKeys::SpaceBar))) { GetUp(); return; }
        if (Rag.IsValid())
        {
            const EidoRagdoll::FStepResult& R = Rag->Step(Dt);
            if (R.bActive || R.bDone)
            {
                DownedPose = R.Pose; DownedRootWire = R.RootPosition;
                SetSelfHeldPose(PoseJson(R.Pose), false);
                if (M) M->SetHeldPoseDirect(R.Pose);
            }
            if (R.bDone) Rag.Reset();   // the settled pose is held until getting up
        }
        else if (M && DownedPose.Num()) M->SetHeldPoseDirect(DownedPose);
        Explorer->SetActorLocation(OriginCentimeters + FromWirePosition(DownedRootWire) + FVector(0, 0, Half), false, nullptr, ETeleportType::TeleportPhysics);
        LocoClip = TEXT("ragdoll"); LocoSpeed = 0;
        return;
    }
    if (FlightRt && FlightRt->State.IsSet())
    {
        if (Explorer->LocomotionMode != TEXT("walk")) { FlightRt->State.Reset(); Explorer->SetSeated(false); return; }   // swimming cancels flight
        EidoFlight::FPilotInput Keys;
        auto Down = [PC](const FKey& K) { return PC && PC->IsInputKeyDown(K); };
        Keys.bPitchDown = Down(EKeys::W) || Down(EKeys::Up); Keys.bPitchUp = Down(EKeys::S) || Down(EKeys::Down);
        Keys.bBankLeft = Down(EKeys::A) || Down(EKeys::Left); Keys.bBankRight = Down(EKeys::D) || Down(EKeys::Right);
        Keys.bFlap = Down(EKeys::SpaceBar); Keys.bSpoil = Down(EKeys::LeftShift) || Down(EKeys::RightShift);
        UWorld* World = GetWorld(); const FVector Origin = OriginCentimeters; TWeakObjectPtr<AEidoverseExplorer> WeakX(Explorer);
        auto GroundAt = [World, Origin, WeakX](double Xw, double Zw)
        {
            FCollisionQueryParams Q(SCENE_QUERY_STAT(EidoverseFlightFloor), true); if (WeakX.IsValid()) Q.AddIgnoredActor(WeakX.Get());
            FHitResult Hit; const FVector Top = Origin + FromWirePosition(FVector(Xw, 500, Zw)), Bottom = Origin + FromWirePosition(FVector(Xw, -500, Zw));
            return World && World->LineTraceSingleByChannel(Hit, Top, Bottom, ECC_Visibility, Q) ? (Hit.ImpactPoint.Z - Origin.Z) / 100 : -1e9;
        };
        const TArray<FString> Bones = M ? M->BoneNames() : TArray<FString>();
        const EidoFlight::FVerdict Still = EidoFlight::ResolveFlight(RightsFly, &Bones);
        const EidoFlight::FFrameResult R = EidoFlight::TickFlight(FlightRt->Cfg, FlightRt->State.GetValue(), Keys, Dt, GroundAt, &Still);
        Explorer->SetActorLocationAndRotation(OriginCentimeters + FromWirePosition(FVector(R.Pos.X, R.Pos.Y, R.Pos.Z)) + FVector(0, 0, Half),
            FromWireRotation(FQuat(FVector::YAxisVector, R.WorldYaw)), false, nullptr, ETeleportType::TeleportPhysics);
        FlightRt->Effort = float(R.WingEffort);
        LocoClip = R.Clip; LocoSpeed = float(R.Speed);
        if (!R.Hint.IsEmpty() && HUD) HUD->Hint(R.Hint, 4);
        if (R.bEnded)
        {
            FlightRt->LastEnd = R.End; FlightRt->State.Reset(); FlightRt->Effort = 0;
            Explorer->SetSeated(false); Explorer->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            LocoClip.Empty();
            if (R.EndReason == TEXT("ragdoll")) GoLimpWith(FVector::ZeroVector, FString());
        }
    }
}

bool AEidoverseBridge::IsFlying() const { return FlightRt.IsValid() && FlightRt->State.IsSet(); }
float AEidoverseBridge::FlightEffort() const { return IsFlying() ? FlightRt->Effort : 0.f; }
