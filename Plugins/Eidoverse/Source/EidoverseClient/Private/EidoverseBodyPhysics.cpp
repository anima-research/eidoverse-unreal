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
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EidoverseSettings.h"

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
    if (!DraggedBy.IsEmpty()) return;   // a hand already holds me; its sim owns the tumble
    StartSelfRagdoll(Lean, nullptr);
    if (!Rag.IsValid()) return;
    bDowned = true;
    Explorer->SetSeated(true); Explorer->Posture.Empty();
    M->SetLimp(true);
    ClearReach();
    if (HUD) HUD->Hint(By.IsEmpty() ? TEXT("limp — move to get up") : By + TEXT(" knocked you over — move to get up"), 3);
}

// A fresh sim on my body from where it is now (limp from standing, a shove re-tumbling
// from the floor, or a dragger's release handing its sim state over), my nails re-applied.
void AEidoverseBridge::StartSelfRagdoll(const FVector& Lean, const EidoRagdoll::FSnapshot* Seed)
{
    auto* Explorer = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0));
    UEidoverseAvatarMesh* M = SelfBody.AvatarMesh.Get();
    if (!Explorer || !M || !M->Avatar) return;
    EidoRagdoll::FInit Init;
    if (!M->RestWorldWire(Init.RestWorld, OriginCentimeters)) return;
    const FVector F = ToWirePosition(Explorer->GetActorForwardVector() * 100);
    Init.RootPosition = bDowned ? DownedRootWire : ToWirePosition(M->GetComponentLocation() - OriginCentimeters);
    Init.RootYaw = bDowned ? DownedYaw : FMath::Atan2(F.X, F.Z);
    Init.bVrm0 = M->Avatar->bVRM0;
    // Park everything not driven at identity, keep the driven bones where they are now
    // (or the settled pose when already down: a shove re-tumbles from the floor).
    const TMap<FName, FQuat>& From = bDowned && DownedPose.Num() ? DownedPose : M->LastNormalized;
    for (const FName B : EidoRagdoll::DrivenBones()) if (const FQuat* Q = From.Find(B)) Init.StartLocal.Add(B, *Q);
    if (Seed) Init.Seed = *Seed; else Init.Lean = Lean;
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
    DownedRootWire = Init.RootPosition; DownedYaw = Init.RootYaw;
    ApplyMyPins();
}

void AEidoverseBridge::GetUp()
{
    auto* Explorer = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!bDowned) return;
    // Break free of a hand (tell the dragger), and my own movement tears out every nail.
    if (!DraggedBy.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("target"), DraggedBy); E->SetBoolField(TEXT("end"), true); SendRaw(TEXT("bodydrag"), E); DraggedBy.Empty(); }
    ClearPins();
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
        // 1.2 s without a sample from the hand that holds me = it let go.
        if (!DraggedBy.IsEmpty() && GetWorld()->GetTimeSeconds() - DragLastSampleAt > 1.2) { DraggedBy.Empty(); EndDragged(nullptr); }
        if (!DraggedBy.IsEmpty()) {}   // the dragger's stream drives the body (ApplyDraggedSample)
        else if (Rag.IsValid())
        {
            const EidoRagdoll::FStepResult& R = Rag->Step(Dt);
            if (R.bActive || R.bDone)
            {
                DownedPose = R.Pose; DownedRootWire = R.RootPosition; DownedYaw = R.RootYaw;
                SetSelfHeldPose(PoseJson(R.Pose), false);
                if (M) M->SetHeldPoseDirect(R.Pose);
            }
            if (R.bDone) Rag.Reset();   // the settled pose is held until getting up
        }
        else if (M && DownedPose.Num()) M->SetHeldPoseDirect(DownedPose);
        Explorer->SetActorLocationAndRotation(OriginCentimeters + FromWirePosition(DownedRootWire) + FVector(0, 0, Half), FromWireRotation(FQuat(FVector::YAxisVector, DownedYaw)), false, nullptr, ETeleportType::TeleportPhysics);
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

// ---------------------------------------------------------------- dragged (bodydrag.js owner half)
// Someone else's sim drives my limp body. My client stays the authority: it applies the
// stream to itself and republishes it as ordinary presence, so everyone sees my motion.
namespace
{
    bool WireVec(const TSharedPtr<FJsonValue>& V, FVector& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!V || !V->TryGetArray(A) || A->Num() != 3) return false;
        for (int32 I = 0; I < 3; ++I) { double N; if (!(*A)[I]->TryGetNumber(N) || !FMath::IsFinite(N)) return false; Out[I] = N; }
        return true;
    }
    bool ParseSnapshot(const TSharedPtr<FJsonObject>& S, EidoRagdoll::FSnapshot& Out)
    {
        const TArray<TSharedPtr<FJsonValue>> *J = nullptr, *P = nullptr, *V = nullptr, *H = nullptr;
        if (!S || !S->TryGetArrayField(TEXT("j"), J) || !S->TryGetArrayField(TEXT("p"), P) || !S->TryGetArrayField(TEXT("v"), V)) return false;
        if (J->Num() == 0 || J->Num() > 24 || P->Num() != J->Num() * 3 || V->Num() != J->Num() * 3) return false;
        for (const auto& X : *J) Out.J.Add(FName(X->AsString().Left(48)));
        auto Nums = [](const TArray<TSharedPtr<FJsonValue>>& In, TArray<double>& To) { for (const auto& X : In) { double N; if (!X->TryGetNumber(N) || !FMath::IsFinite(N)) return false; To.Add(N); } return true; };
        if (!Nums(*P, Out.P) || !Nums(*V, Out.V)) return false;
        if (S->TryGetArrayField(TEXT("h"), H) && !Nums(*H, Out.H)) Out.H.Reset();
        double Dy; if (S->TryGetNumberField(TEXT("dy"), Dy) && FMath::IsFinite(Dy)) Out.Dy = Dy;
        return true;
    }
}

void AEidoverseBridge::ReceiveBodyDrag(const TSharedPtr<FJsonObject>& J)
{
    FString By; J->TryGetStringField(TEXT("by"), By); By = By.Left(64);
    if (By.IsEmpty()) return;
    auto Reply = [this, &By](const TSharedRef<FJsonObject>& O) { O->SetStringField(TEXT("target"), By); SendRaw(TEXT("bodydrag"), O); };
    auto EndTo = [&Reply]() { auto O = MakeShared<FJsonObject>(); O->SetBoolField(TEXT("end"), true); Reply(O); };
    const TSharedPtr<FJsonValue> Grab = J->TryGetField(TEXT("grab"));
    if (Grab && !Grab->IsNull())
    {
        if (!EidoSettings::Pushable()) { ToastText(By + TEXT(" tried to drag you — /pushable on to allow")); EndTo(); return; }
        if (!bDowned || !SelfBody.AvatarMesh.IsValid()) { EndTo(); return; }           // only limp bodies drag
        if (!DraggedBy.IsEmpty() && DraggedBy != By) { EndTo(); return; }              // first grab wins
        DraggedBy = By; DragLastSampleAt = GetWorld()->GetTimeSeconds();
        Rag.Reset();   // the dragger's sim owns the tumble now
        if (UEidoverseAvatarMesh* M = SelfBody.AvatarMesh.Get()) M->SetLimp(true);
        if (HUD) HUD->Hint(By + TEXT(" grabs you — move to break free"), 3);
        // Grabbing a nailed joint takes it back into the hand; the others stay mine,
        // and the dragger's takeover sim must enforce them.
        FString Joint; if (Grab->Type == EJson::Object) Grab->AsObject()->TryGetStringField(TEXT("joint"), Joint);
        if (!Joint.IsEmpty()) RemovePin(FName(Joint.Left(48)));
        if (MyPins.Num())
        {
            TArray<TSharedPtr<FJsonValue>> Pins;
            for (const auto& P : MyPins)
            {
                auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("j"), P.Key.ToString());
                O->SetArrayField(TEXT("at"), {MakeShared<FJsonValueNumber>(P.Value.X), MakeShared<FJsonValueNumber>(P.Value.Y), MakeShared<FJsonValueNumber>(P.Value.Z)});
                Pins.Add(MakeShared<FJsonValueObject>(O));
            }
            auto O = MakeShared<FJsonObject>(); O->SetArrayField(TEXT("pins"), Pins); Reply(O);
        }
        return;
    }
    const TSharedPtr<FJsonValue> Unpin = J->TryGetField(TEXT("unpin"));
    if (Unpin && !Unpin->IsNull())
    {
        if (!EidoSettings::Pushable()) { ToastText(By + TEXT(" tried to unpin you — /pushable on to allow")); return; }
        FString Joint; if (Unpin->Type == EJson::Object) Unpin->AsObject()->TryGetStringField(TEXT("joint"), Joint);
        RemovePin(FName(Joint.Left(48)));
        return;
    }
    if (J->HasField(TEXT("pins"))) return;   // only a dragger cares (this client never drags yet)
    const TSharedPtr<FJsonValue> End = J->TryGetField(TEXT("end"));
    if (End && !End->IsNull())
    {
        if (DraggedBy != By) return;
        DraggedBy.Empty();
        const TSharedPtr<FJsonObject>* PinAt = nullptr;
        if (J->TryGetObjectField(TEXT("pinAt"), PinAt))
        {
            FString Joint; FVector At;
            if ((*PinAt)->TryGetStringField(TEXT("joint"), Joint) && WireVec((*PinAt)->TryGetField(TEXT("at")), At)) AddPin(FName(Joint.Left(48)), At);
        }
        EndDragged(J);
        return;
    }
    if (DraggedBy == By && J->HasTypedField<EJson::Object>(TEXT("pose"))) { DragLastSampleAt = GetWorld()->GetTimeSeconds(); ApplyDraggedSample(J); }
}

void AEidoverseBridge::ApplyDraggedSample(const TSharedPtr<FJsonObject>& J)
{
    UEidoverseAvatarMesh* M = SelfBody.AvatarMesh.Get();
    if (!J || !M) return;
    FVector P; if (WireVec(J->TryGetField(TEXT("p")), P) && P.Size() < 1e6) DownedRootWire = P;
    double Yaw; if (J->TryGetNumberField(TEXT("yaw"), Yaw) && FMath::IsFinite(Yaw)) DownedYaw = FMath::Atan2(FMath::Sin(Yaw), FMath::Cos(Yaw));
    const TSharedPtr<FJsonObject>* Pose = nullptr;
    if (J->TryGetObjectField(TEXT("pose"), Pose))
    {
        TMap<FName, FQuat> Next;
        for (const auto& Pair : (*Pose)->Values)
        {
            const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
            if (!Pair.Value || !Pair.Value->TryGetArray(A) || A->Num() != 4) continue;
            double Q[4]; bool bOk = true;
            for (int32 I = 0; I < 4; ++I) bOk &= (*A)[I]->TryGetNumber(Q[I]) && FMath::IsFinite(Q[I]);
            if (bOk) Next.Add(FName(FString(Pair.Key).Left(48)), FQuat(Q[0], Q[1], Q[2], Q[3]).GetNormalized());
            if (Next.Num() >= 64) break;
        }
        if (Next.Num())
        {
            DownedPose = Next;
            SetSelfHeldPose(MakeShared<FJsonObject>(**Pose), false);
            M->SetHeldPoseDirect(DownedPose);
        }
    }
    if (auto* X = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0)))
    {
        const double Half = X->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        X->SetActorLocationAndRotation(OriginCentimeters + FromWirePosition(DownedRootWire) + FVector(0, 0, Half), FromWireRotation(FQuat(FVector::YAxisVector, DownedYaw)), false, nullptr, ETeleportType::TeleportPhysics);
    }
}

void AEidoverseBridge::EndDragged(const TSharedPtr<FJsonObject>& J)
{
    // Land on the hand's final frame, then settle under my own sim from there: dropped
    // from a height the body falls; the takeover was only ever the moving part.
    if (!bDowned) return;
    if (J && (J->HasField(TEXT("pose")) || J->HasField(TEXT("p")))) ApplyDraggedSample(J);
    EidoRagdoll::FSnapshot Seed;
    const TSharedPtr<FJsonObject>* Sim = nullptr;
    const bool bSeed = J && J->TryGetObjectField(TEXT("sim"), Sim) && ParseSnapshot(*Sim, Seed);
    StartSelfRagdoll(FVector::ZeroVector, bSeed ? &Seed : nullptr);
}

// ---------------------------------------------------------------- pins (my nails)
void AEidoverseBridge::AddPin(FName Joint, const FVector& AtWire)
{
    if (Joint.IsNone() || AtWire.ContainsNaN() || (MyPins.Num() >= 8 && !MyPins.Contains(Joint))) return;
    MyPins.Add(Joint, AtWire);
    ApplyMyPins();
}

void AEidoverseBridge::RemovePin(FName Joint)
{
    if (!MyPins.Remove(Joint)) return;
    if (Rag.IsValid()) Rag->Unpin(Joint);
    if (!MyPins.Num()) bSendPinsNull = true;
    // Freed of a nail while lying still with nobody's hand on me: sag from what remains.
    if (bDowned && !Rag.IsValid() && DraggedBy.IsEmpty()) StartSelfRagdoll(FVector::ZeroVector, nullptr);
}

void AEidoverseBridge::ClearPins()
{
    if (!MyPins.Num()) return;
    MyPins.Reset();
    if (Rag.IsValid()) Rag->UnpinAll();
    bSendPinsNull = true;
}

void AEidoverseBridge::ApplyMyPins()
{
    if (Rag.IsValid()) for (const auto& P : MyPins) Rag->PinJoint(P.Key, P.Value, true);
}

int32 AEidoverseBridge::GetPinMarkerCount() const
{
    int32 N = 0; for (const auto& C : PinMarkers) if (C && C->IsVisible() && C->IsRegistered()) ++N; return N;
}

void AEidoverseBridge::TickPinMarkers()
{
    // A small gold sphere (r 0.055 m, #ffd166) on every nail: mine and every body's.
    TArray<FVector> At;
    for (const auto& P : MyPins) At.Add(P.Value);
    for (const auto& Pair : Peers) for (const auto& P : Pair.Value.Pins) At.Add(P.Value);
    if (At.Num() && !PinHolder)
    {
        // Their own visible actor: components of the (hidden) bridge never render.
        FActorSpawnParameters SP; SP.ObjectFlags |= RF_Transient;
        PinHolder = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SP);
        if (PinHolder) { auto* Root = NewObject<USceneComponent>(PinHolder); PinHolder->SetRootComponent(Root); Root->RegisterComponent(); }
    }
    while (PinHolder && PinMarkers.Num() < At.Num())
    {
        auto* C = NewObject<UStaticMeshComponent>(PinHolder);
        C->SetupAttachment(PinHolder->GetRootComponent()); PinHolder->AddInstanceComponent(C);
        C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
        C->SetWorldScale3D(FVector(.11));
        if (UMaterialInterface* Unlit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Eidoverse/UI/M_EidoverseUnlit.M_EidoverseUnlit")))
        { auto* MI = UMaterialInstanceDynamic::Create(Unlit, C); MI->SetVectorParameterValue(TEXT("Color"), FLinearColor(FColor(0xff, 0xd1, 0x66))); C->SetMaterial(0, MI); }
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCastShadow(false);
        C->SetMobility(EComponentMobility::Movable);
        C->RegisterComponent();
        PinMarkers.Add(C);
    }
    for (int32 I = 0; I < PinMarkers.Num(); ++I)
    {
        UStaticMeshComponent* C = PinMarkers[I]; if (!C) continue;
        const bool bOn = I < At.Num();
        C->SetVisibility(bOn);
        if (bOn) C->SetWorldLocation(OriginCentimeters + FromWirePosition(At[I]));
    }
}
