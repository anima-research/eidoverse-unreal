// Requests aimed at my own body, honoured only with consent (localbody.js):
// puppet (pose / anim / shove), bodydrag, and force blasts. Posable defaults off,
// pushable on. A shove knocks me down (a lying fall until local ragdoll lands).
#include "EidoverseBridge.h"
#include "EidoverseUI.h"
#include "EidoverseScene.h"
#include "EidoverseAvatar.h"
#include "EidoverseSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

namespace ConsentWire
{
    FString CStr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString S; if (O) O->TryGetStringField(K, S); return S; }
    double CNum(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double D) { double N; return O && O->TryGetNumberField(K, N) && FMath::IsFinite(N) ? N : D; }
    bool CVec(const TSharedPtr<FJsonObject>& O, const TCHAR* K, FVector& V)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!O || !O->TryGetArrayField(K, A) || A->Num() != 3) return false;
        for (int32 I = 0; I < 3; ++I) { double N; if (!(*A)[I]->TryGetNumber(N) || !FMath::IsFinite(N)) return false; V[I] = N; }
        return true;
    }
}
using namespace ConsentWire;

void AEidoverseBridge::SetSelfHeldPose(const TSharedPtr<FJsonObject>& Pose, bool bAnnounce)
{
    SelfHeldPose = Pose && Pose->Values.Num() > 0 ? Pose : nullptr;
    if (!SelfHeldPose) bSendHeldNull = true;
    if (auto* M = SelfBody.AvatarMesh.Get()) { if (SelfHeldPose) M->SetHeldPose(SelfHeldPose); else M->ClearHeldPose(); }
    (void)bAnnounce;
}

void AEidoverseBridge::KnockDown(const FVector& LeanWire, const FString& By)
{
    auto* Explorer = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!Explorer || Explorer->bSeated) return;
    // Topple along the lean: face away from it and lie down (move to get up).
    const FVector Dir = FromWirePosition(FVector(LeanWire.X, 0, LeanWire.Z)).GetSafeNormal2D();
    if (!Dir.IsNearlyZero()) Explorer->SetActorRotation(FRotator(0, (-Dir).Rotation().Yaw, 0));
    Explorer->LaunchCharacter(Dir * FMath::Min(LeanWire.Size(), 6.0) * 60, false, false);
    Explorer->Posture = TEXT("lie");
    if (HUD) HUD->Hint(By.IsEmpty() ? TEXT("you're knocked over — move to get up") : By + TEXT(" knocked you over — move to get up"), 4);
}

void AEidoverseBridge::ReceivePuppet(const TSharedPtr<FJsonObject>& J)
{
    const FString By = CStr(J, TEXT("by")).Left(64);
    const TSharedPtr<FJsonValue> Ragdoll = J->TryGetField(TEXT("ragdoll"));
    if (Ragdoll && !Ragdoll->IsNull())
    {
        if (!EidoSettings::Pushable()) { ToastText(By + TEXT(" tried to knock you over — /pushable on to allow")); return; }
        FVector Lean(0);
        if (const TSharedPtr<FJsonObject> R = Ragdoll->Type == EJson::Object ? Ragdoll->AsObject() : nullptr) CVec(R, TEXT("lean"), Lean);
        if (Lean.IsNearlyZero())
            if (const APawn* Me = UGameplayStatics::GetPlayerPawn(this, 0)) Lean = ToWirePosition(Me->GetActorForwardVector() * 100) * 1.5;
        KnockDown(Lean.GetClampedToMaxSize(6), By);
        return;
    }
    if (!EidoSettings::Posable()) { ToastText(By + TEXT(" tried to pose you — enable “let others pose me” in settings to allow")); return; }
    const TSharedPtr<FJsonValue> PoseV = J->TryGetField(TEXT("pose"));
    if (PoseV && PoseV->Type == EJson::Object)
    {
        const TSharedPtr<FJsonObject> In = PoseV->AsObject();
        bool bMerge = false; J->TryGetBoolField(TEXT("merge"), bMerge);
        TSharedPtr<FJsonObject> Next;
        if (bMerge)
        {
            // mergePose: a null bone deletes it; an empty result releases.
            Next = SelfHeldPose ? MakeShared<FJsonObject>(*SelfHeldPose) : MakeShared<FJsonObject>();
            for (const auto& Pair : In->Values) { if (!Pair.Value || Pair.Value->IsNull()) Next->RemoveField(Pair.Key); else Next->SetField(Pair.Key, Pair.Value); }
        }
        else Next = In;
        SetSelfHeldPose(Next, true);
        if (HUD) HUD->Hint(SelfHeldPose ? By + TEXT(" posed you") : By + TEXT(" released your pose"), 3);
    }
    else if (PoseV && PoseV->IsNull() && J->HasField(TEXT("pose")) && !J->HasField(TEXT("anim")))
    {
        SetSelfHeldPose(nullptr, true);
        if (HUD) HUD->Hint(By + TEXT(" released your pose"), 3);
    }
    const TSharedPtr<FJsonValue> AnimV = J->TryGetField(TEXT("anim"));
    if (AnimV && AnimV->Type == EJson::Object)
    {
        const TSharedPtr<FJsonObject> Anim = AnimV->AsObject();
        bool bReplace = true; Anim->TryGetBoolField(TEXT("replace"), bReplace);
        bool bLoop = false; Anim->TryGetBoolField(TEXT("loop"), bLoop);
        const TSharedPtr<FJsonObject>* Tracks = nullptr;
        if (!Anim->TryGetObjectField(TEXT("tracks"), Tracks)) return;
        if (bReplace) SetSelfHeldPose(nullptr, false);
        const float Dur = float(CNum(Anim, TEXT("dur"), 2));
        if (auto* M = SelfBody.AvatarMesh.Get()) M->PlayAgentAnimation(*Tracks, Dur, bLoop, bReplace);
        // Everyone else sees it because I play it (the body's owner speaks for it).
        auto Out = MakeShared<FJsonObject>();
        Out->SetNumberField(TEXT("dur"), Dur); Out->SetObjectField(TEXT("tracks"), *Tracks);
        Out->SetBoolField(TEXT("loop"), bLoop); Out->SetBoolField(TEXT("replace"), bReplace);
        SendRaw(TEXT("anim"), Out);
    }
}

void AEidoverseBridge::ReceiveBodyDrag(const TSharedPtr<FJsonObject>& J)
{
    // Being dragged needs a limp body; until local ragdoll exists, decline politely
    // so the dragger's client lets go at once instead of waiting out a timeout.
    const FString By = CStr(J, TEXT("by")).Left(64);
    if (!J->HasField(TEXT("grab")) || By.IsEmpty()) return;
    if (!EidoSettings::Pushable()) ToastText(By + TEXT(" tried to drag you — /pushable on to allow"));
    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("target"), By); Out->SetBoolField(TEXT("end"), true);
    SendRaw(TEXT("bodydrag"), Out);
}

void AEidoverseBridge::ReceiveForce(const TSharedPtr<FJsonObject>& A)
{
    // shared/force.js: radius 4, power 3, cap 6, below 0.3 a breeze, inside 0.05 no direction.
    if (!A || !EidoSettings::Pushable() || bSpectator) return;
    FVector At; if (!CVec(A, TEXT("at"), At)) return;
    const double Radius = FMath::Clamp(CNum(A, TEXT("radius"), 4), .5, 30.), Power = FMath::Clamp(CNum(A, TEXT("power"), 3), .2, 12.);
    const APawn* Me = UGameplayStatics::GetPlayerPawn(this, 0); if (!Me) return;
    const FVector MeWire = ToWirePosition(Me->GetActorLocation() - OriginCentimeters);
    const FVector2D D(MeWire.X - At.X, MeWire.Z - At.Z);
    const double Dist = D.Size();
    if (Dist >= Radius) return;
    const double Mag = FMath::Min(6., Power * (1 - Dist / Radius));
    if (Mag < .3) return;
    FVector Lean;
    if (Dist > .05) Lean = FVector(D.X / Dist * Mag, 0, D.Y / Dist * Mag);
    else Lean = ToWirePosition(Me->GetActorForwardVector() * 100) * FMath::Max(Mag, .5);
    KnockDown(Lean, TEXT("a blast"));
}

bool AEidoverseBridge::ShovePerson(const FString& Name, float Power, const FString& Verb)
{
    const APawn* Me = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Me) return false;
    const FVector MeWire = ToWirePosition(Me->GetActorLocation() - OriginCentimeters);
    FString Target = Name; double Best = 2.5; FVector TargetWire;
    for (const auto& Pair : Peers)
    {
        if (!Pair.Value.Actor.IsValid()) continue;
        const FVector W = ToWirePosition(Pair.Value.Actor->GetActorLocation() - OriginCentimeters);
        const double D = FVector2D(W.X - MeWire.X, W.Z - MeWire.Z).Size();
        if (!Name.IsEmpty() && Pair.Key == Name)
        {
            if (D > 2.5) { Narrate(FString::Printf(TEXT("%s is too far away to %s (%.1fm)"), *Name, *Verb, D)); return false; }
            TargetWire = W; Best = D; Target = Pair.Key; break;
        }
        if (Name.IsEmpty() && D < Best) { Best = D; Target = Pair.Key; TargetWire = W; }
    }
    if (Target.IsEmpty() || !Peers.Contains(Target)) { Narrate(Name.IsEmpty() ? TEXT("nobody within reach to push") : FString::Printf(TEXT("nobody here called “%s”"), *Name)); return false; }
    FVector Dir(TargetWire.X - MeWire.X, 0, TargetWire.Z - MeWire.Z);
    if (Dir.Size() <= .05) Dir = ToWirePosition(Me->GetActorForwardVector() * 100);
    Dir = Dir.GetSafeNormal() * FMath::Clamp(Power, .5f, 4.f);
    auto Lean = MakeShared<FJsonObject>();
    Lean->SetArrayField(TEXT("lean"), {MakeShared<FJsonValueNumber>(Dir.X), MakeShared<FJsonValueNumber>(0), MakeShared<FJsonValueNumber>(Dir.Z)});
    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("target"), Target); Out->SetObjectField(TEXT("ragdoll"), Lean);
    Out->SetField(TEXT("pose"), MakeShared<FJsonValueNull>()); Out->SetField(TEXT("anim"), MakeShared<FJsonValueNull>());
    if (!SendRaw(TEXT("puppet"), Out)) return false;
    Narrate(FString::Printf(TEXT("you %s %s"), *Verb, *Target));
    return true;
}
