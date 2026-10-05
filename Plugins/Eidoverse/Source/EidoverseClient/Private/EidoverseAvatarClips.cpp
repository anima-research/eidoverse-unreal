// VRM Animation playback on a body, matching the browser (avatar.js setClip /
// playEmote / _setAction and @pixiv/three-vrm-animation's retargeting):
// - clip keys are normalized rest-relative rotations (EidoverseClips.h), applied to
//   the raw skeleton as raw = P^-1 * q * P * rest, P the raw parent's rest rotation;
// - hips translation is scaled by bodyRestHips / clipRestHips;
// - VRM 0.x bodies face -Z: x and z of every key are negated (three-vrm does the same);
// - crossfades are linear weight ramps (0.22 s, 0.1 s into a jump), emotes are a full
//   crossfade that holds the last frame and fades out over 0.25 s.
#include "EidoverseAvatar.h"
#include "EidoverseBridge.h"
#include "EidoverseClips.h"
#include "EidoverseLoadTrace.h"

namespace
{
    FQuat ToBody(FQuat Q, bool bVRM0)
    {
        if (bVRM0) { Q.X = -Q.X; Q.Z = -Q.Z; }
        return AEidoverseBridge::FromWireRotation(Q);   // glTF axes -> UE axes (same basis as the importer)
    }
    FVector ToBodyPosition(FVector P, bool bVRM0)
    {
        if (bVRM0) { P.X = -P.X; P.Z = -P.Z; }
        return AEidoverseBridge::FromWirePosition(P);   // metres -> cm, glTF -> UE axes
    }
}

void UEidoverseAvatarMesh::PrepareRetarget()
{
    ClipPlays.Reset(); HumanBoneIndex.Reset(); ReferenceComponent.Reset();
    if (!Avatar || !Avatar->Mesh) return;
    const FReferenceSkeleton& Ref = Avatar->Mesh->GetRefSkeleton();
    ReferenceComponent.SetNum(Reference.Num());
    for (int32 I = 0; I < Reference.Num(); ++I)
    {
        const int32 P = Ref.GetParentIndex(I);
        ReferenceComponent[I] = P >= 0 ? Reference[I] * ReferenceComponent[P] : Reference[I];
    }
    for (const auto& Pair : Avatar->Humanoid)
    {
        const int32 Index = Ref.FindBoneIndex(Pair.Value);
        if (Index != INDEX_NONE) HumanBoneIndex.Add(Pair.Key, Index);
    }
    if (const int32* Hips = HumanBoneIndex.Find(FName(TEXT("hips"))))
        RestHipsHeight = FMath::Max(.05, ReferenceComponent[*Hips].GetLocation().Z / 100.0);
}

void UEidoverseAvatarMesh::SetClip(TSharedPtr<const FEidoClip> Clip, const FString& Slot, float Rate, float FadeSeconds)
{
    if (!Clip) return;
    if (HasOneShot()) return;   // an emote owns the body until it ends or is cancelled
    FClipPlay* Top = ClipPlays.Num() ? &ClipPlays.Last() : nullptr;
    if (Top && Top->Target > 0 && Top->Clip == Clip && Top->Slot == Slot) { Top->Rate = Rate; return; }
    const float Speed = 1.f / FMath::Max(FadeSeconds, .01f);
    for (FClipPlay& P : ClipPlays) { P.Target = 0; P.FadeSpeed = Speed; }
    FClipPlay& P = ClipPlays.AddDefaulted_GetRef();
    P.Clip = Clip; P.Slot = Slot; P.Rate = Rate; P.Target = 1; P.FadeSpeed = Speed; P.bLoop = true;
    P.Weight = ClipPlays.Num() == 1 ? 1.f : 0.f;   // the first clip on a body starts at full weight
    P.Time = Slot == TEXT("jump") ? Clip->Takeoff : 0.f;   // jumps start at take-off, not the crouch
}

void UEidoverseAvatarMesh::PlayOneShot(TSharedPtr<const FEidoClip> Clip, const FString& Name)
{
    if (!Clip) return;
    for (FClipPlay& P : ClipPlays) { P.Target = 0; P.FadeSpeed = 1 / .22f; }
    FClipPlay& P = ClipPlays.AddDefaulted_GetRef();
    P.Clip = Clip; P.Slot = TEXT("emote:") + Name; P.Rate = 1; P.Target = 1; P.FadeSpeed = 1 / .22f; P.bLoop = false;
    P.Weight = ClipPlays.Num() == 1 ? 1.f : 0.f;
}

bool UEidoverseAvatarMesh::HasOneShot() const
{
    for (const FClipPlay& P : ClipPlays) if (!P.bLoop && P.Target > 0 && !P.bFinished) return true;
    return false;
}

void UEidoverseAvatarMesh::CancelOneShot()
{
    for (FClipPlay& P : ClipPlays) if (!P.bLoop) { P.Target = 0; P.FadeSpeed = 1 / .25f; P.bFinished = true; }
}

void UEidoverseAvatarMesh::SyncClipPhase(const FString& Slot, float ClipTime, float Rate)
{
    if (ClipPlays.IsEmpty()) return;
    FClipPlay& Top = ClipPlays.Last();
    if (Top.Slot != Slot || !Top.Clip || Top.Clip->Duration <= 0 || !FMath::IsFinite(ClipTime)) return;
    Top.Time = FMath::Fmod(FMath::Max(0.f, ClipTime), Top.Clip->Duration);
    if (Rate > 0 && Rate <= 3.2f) Top.Rate = Rate;
}

void UEidoverseAvatarMesh::StopClips()
{
    ClipPlays.Reset();
}

FString UEidoverseAvatarMesh::CurrentClipSlot() const
{
    for (int32 I = ClipPlays.Num() - 1; I >= 0; --I) if (ClipPlays[I].Target > 0) return ClipPlays[I].Slot;
    return FString();
}
float UEidoverseAvatarMesh::CurrentClipTime() const
{
    for (int32 I = ClipPlays.Num() - 1; I >= 0; --I) if (ClipPlays[I].Target > 0) return ClipPlays[I].Time;
    return 0;
}
float UEidoverseAvatarMesh::CurrentClipRate() const
{
    for (int32 I = ClipPlays.Num() - 1; I >= 0; --I) if (ClipPlays[I].Target > 0) return ClipPlays[I].Rate;
    return 1;
}

bool UEidoverseAvatarMesh::AnimateClips(double Time)
{
    if (!Avatar || Reference.IsEmpty() || HumanBoneIndex.IsEmpty() || ClipPlays.IsEmpty()) { bLastFrameFromClips = false; return false; }
    const float Dt = LastClipTime < 0 ? 1.f / 60 : FMath::Clamp(float(Time - LastClipTime), 0.f, .1f);
    LastClipTime = Time;
    // Coming from the procedural pose (swimming): blend over a short window instead of popping.
    if (!bLastFrameFromClips) { ClipBlendUntil = Time + .3; PreviousPose = BoneSpaceTransforms; }
    bLastFrameFromClips = true;

    // Advance clocks and weights.
    for (FClipPlay& P : ClipPlays)
    {
        const float Duration = P.Clip->Duration;
        P.Time += Dt * P.Rate;
        if (P.bLoop) { if (Duration > 0) P.Time = FMath::Fmod(P.Time, Duration); }
        else if (P.Time >= Duration && !P.bFinished)
        { P.Time = Duration; P.bFinished = true; P.Target = 0; P.FadeSpeed = 1 / .25f; }   // clampWhenFinished + fadeOut(0.25)
        P.Weight = P.Target > P.Weight ? FMath::Min(P.Target, P.Weight + Dt * P.FadeSpeed) : FMath::Max(P.Target, P.Weight - Dt * P.FadeSpeed);
    }
    // A finished emote fading out with nothing underneath: keep holding it until a
    // locomotion clip is set (the bridge does that next frame).
    ClipPlays.RemoveAll([this](const FClipPlay& P) { return P.Weight <= 0 && P.Target <= 0 && ClipPlays.Num() > 1; });
    float Total = 0; for (const FClipPlay& P : ClipPlays) Total += P.Weight;
    if (Total <= 0) { for (FClipPlay& P : ClipPlays) if (P.Target > 0) P.Weight = 1; Total = 1; }

    BoneSpaceTransforms = Reference;
    const bool bVRM0 = Avatar->bVRM0;
    const FReferenceSkeleton& Ref = Avatar->Mesh->GetRefSkeleton();
    // 1. Normalized humanoid rotations from the clips (identity = rest).
    TMap<FName, FQuat> Normalized; Normalized.Reserve(HumanBoneIndex.Num());
    for (const auto& Pair : HumanBoneIndex)
    {
        FQuat Blend = FQuat::Identity; float Accumulated = 0;
        for (const FClipPlay& P : ClipPlays)
        {
            if (P.Weight <= 0) continue;
            FQuat Q;
            if (!P.Clip->SampleRotation(Pair.Key, P.Time, Q)) continue;
            Accumulated += P.Weight;
            Blend = Accumulated == P.Weight ? Q : FQuat::Slerp(Blend, Q, P.Weight / Accumulated);
        }
        if (Accumulated > 0 && Accumulated < Total) Blend = FQuat::Slerp(FQuat::Identity, Blend, Accumulated / Total);
        Normalized.Add(Pair.Key, Blend.GetNormalized());
    }
    // 2. Layers on top (head pitch, held pose, agent animations, eyes), still normalized.
    FVector HipsOffsetModel = FVector::ZeroVector;
    ApplyNormalizedLayers(Normalized, HipsOffsetModel, Dt, Time);
    // 3. Normalized -> raw: raw = P^-1 * q * P * rest.
    for (const auto& Pair : HumanBoneIndex)
    {
        const FQuat* Norm = Normalized.Find(Pair.Key);
        if (!Norm || Norm->Equals(FQuat::Identity, 1e-6f)) continue;
        const int32 Index = Pair.Value;
        const int32 Parent = Ref.GetParentIndex(Index);
        const FQuat P = Parent >= 0 ? ReferenceComponentRotations[Parent] : FQuat::Identity;
        const FQuat Q = ToBody(*Norm, bVRM0);
        BoneSpaceTransforms[Index].SetRotation((P.Inverse() * Q * P * Reference[Index].GetRotation()).GetNormalized());
    }
    // Hips translation (the only translation three-vrm keeps), scaled by hips height,
    // plus a held pose's hips offset (model metres).
    if (const int32* Hips = HumanBoneIndex.Find(FName(TEXT("hips"))))
    {
        FVector Sum = FVector::ZeroVector; float W = 0;
        for (const FClipPlay& P : ClipPlays)
        {
            FVector H;
            if (P.Weight <= 0 || !P.Clip->SampleHips(P.Time, H)) continue;
            Sum += H * (RestHipsHeight / FMath::Max(.01f, P.Clip->RestHipsHeight)) * P.Weight; W += P.Weight;
        }
        if (W > 0 || !HipsOffsetModel.IsNearlyZero())
        {
            const FVector RestCS = ReferenceComponent[*Hips].GetLocation();
            FVector CS = W > 0 ? ToBodyPosition(Sum / W, bVRM0) : RestCS;
            if (W > 0 && W < Total) CS = FMath::Lerp(RestCS, CS, W / Total);
            CS += ToBodyPosition(HipsOffsetModel, bVRM0);
            const int32 Parent = Ref.GetParentIndex(*Hips);
            const FVector Local = Parent >= 0 ? ReferenceComponent[Parent].InverseTransformPosition(CS) : CS;
            BoneSpaceTransforms[*Hips].SetTranslation(Local);
        }
    }
    if (Time < ClipBlendUntil && PreviousPose.Num() == BoneSpaceTransforms.Num())
    {
        const float A = 1 - FMath::Exp(-Dt / .08f);
        for (int32 I = 0; I < BoneSpaceTransforms.Num(); ++I)
        {
            PreviousPose[I].SetRotation(FQuat::Slerp(PreviousPose[I].GetRotation(), BoneSpaceTransforms[I].GetRotation(), A).GetNormalized());
            PreviousPose[I].SetTranslation(FMath::Lerp(PreviousPose[I].GetTranslation(), BoneSpaceTransforms[I].GetTranslation(), A));
        }
        BoneSpaceTransforms = PreviousPose;
    }
    AnimationState = CurrentClipSlot();
    UpdateSprings(Dt);
    ApplyRawLayers(Dt, Time);
    ApplyExpressions();
    MarkRefreshTransformDirty();
    return true;
}

// ---- expressions -> blend shapes ------------------------------------------------

void UEidoverseAvatarMesh::SetExpression(FName Expression, float Weight)
{
    if (Weight <= 0.001f) ExpressionWeights.Remove(Expression);
    else ExpressionWeights.Add(Expression, FMath::Min(Weight, 1.f));
}

void UEidoverseAvatarMesh::ApplyExpressions()
{
    if (!Avatar || !Avatar->Mesh) return;
    const TArray<TObjectPtr<UMorphTarget>>& Morphs = Avatar->Mesh->GetMorphTargets();
    if (Morphs.IsEmpty()) return;
    if (MorphTargetWeights.Num() != Morphs.Num()) MorphTargetWeights.SetNumZeroed(Morphs.Num());
    else FMemory::Memzero(MorphTargetWeights.GetData(), MorphTargetWeights.Num() * sizeof(float));
    ActiveMorphTargets.Reset();
    for (const auto& E : ExpressionWeights)
    {
        const TArray<TPair<FName, float>>* Binds = Avatar->Expressions.Find(E.Key);
        if (!Binds) continue;
        for (const auto& Bind : *Binds)
        {
            int32 Index = INDEX_NONE;
            UMorphTarget* Morph = Avatar->Mesh->FindMorphTargetAndIndex(Bind.Key, Index);
            if (!Morph || !MorphTargetWeights.IsValidIndex(Index)) continue;
            MorphTargetWeights[Index] = FMath::Min(1.f, MorphTargetWeights[Index] + E.Value * Bind.Value);
            ActiveMorphTargets.Add(Morph, Index);
        }
    }
}
