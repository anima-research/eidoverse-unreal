// Wings (avatar.js _findWings/_flap): bones named L_/R_Wing_Upper|Lower[_n] beat a
// slow idle (or a fast power stroke with effort), rebuilt from rest every frame so
// nothing integrates, and fold into Janus's authored vigil pose (G). Applied after
// spring bones; not while limp.
#include "EidoverseAvatar.h"
#include "EidoverseBridge.h"
#include "Internationalization/Regex.h"

namespace
{
    struct FWingDials { float Deg, Hz, Bias, Tip, Lag, Recover, Sweep, SweepPhase; };
    const FWingDials WingIdle{10, 1 / 3.4f, 0, .9f, .25f, .5f, 18, .25f};
    const FWingDials WingPower{8, 2.1f, -8, .78f, .09f, .55f, 17, .25f};
    FWingDials Mix(const FWingDials& A, const FWingDials& B, float T)
    {
        auto L = [T](float X, float Y) { return X + (Y - X) * T; };
        return {L(A.Deg, B.Deg), L(A.Hz, B.Hz), L(A.Bias, B.Bias), L(A.Tip, B.Tip), L(A.Lag, B.Lag), A.Recover, L(A.Sweep, B.Sweep), L(A.SweepPhase, B.SweepPhase)};
    }
    // glTF local-to-rest quaternions [x,y,z,w], authored in Blender (wings-folded_1).
    const TMap<FName, FQuat>& WingFolded()
    {
        static const TMap<FName, FQuat> M = {
            {TEXT("L_Wing_Lower"), FQuat(-0.19528, +0.01928, +0.08113, +0.97720)},
            {TEXT("L_Wing_Lower_1"), FQuat(+0.00000, +0.00000, -0.14119, +0.98998)},
            {TEXT("L_Wing_Lower_2"), FQuat(+0.00000, -0.00000, -0.07015, +0.99754)},
            {TEXT("L_Wing_Upper"), FQuat(-0.61553, +0.10840, -0.23210, +0.74532)},
            {TEXT("L_Wing_Upper_1"), FQuat(+0.02722, +0.15488, -0.05078, +0.98625)},
            {TEXT("L_Wing_Upper_2"), FQuat(+0.07325, +0.01376, -0.08462, +0.99362)},
            {TEXT("R_Wing_Lower"), FQuat(-0.18819, -0.03912, -0.05598, +0.97976)},
            {TEXT("R_Wing_Lower_1"), FQuat(-0.00000, +0.00000, +0.15163, +0.98844)},
            {TEXT("R_Wing_Lower_2"), FQuat(-0.00000, +0.00000, +0.09890, +0.99510)},
            {TEXT("R_Wing_Upper"), FQuat(-0.57842, -0.13811, +0.24053, +0.76714)},
            {TEXT("R_Wing_Upper_1"), FQuat(+0.00000, -0.00000, +0.17326, +0.98488)},
        };
        return M;
    }
}

void UEidoverseAvatarMesh::PrepareWings()
{
    Wings.Reset();
    if (!Avatar || !Avatar->Mesh) return;
    const FReferenceSkeleton& Ref = Avatar->Mesh->GetRefSkeleton();
    const FRegexPattern Pattern(TEXT("^([LR])_Wing_(Upper|Lower)(?:_(\\d+))?$"));
    TSet<FString> Chains;
    for (int32 I = 0; I < Ref.GetNum(); ++I)
    {
        const FString Name = Ref.GetBoneName(I).ToString();
        FRegexMatcher M(Pattern, Name);
        if (!M.FindNext()) continue;
        FWing W; W.Bone = I; W.Name = Ref.GetBoneName(I);
        W.Side = M.GetCaptureGroup(1) == TEXT("L") ? 1.f : -1.f;
        W.bLower = M.GetCaptureGroup(2) == TEXT("Lower");
        W.Depth = M.GetCaptureGroup(3).IsEmpty() ? 0 : FCString::Atoi(*M.GetCaptureGroup(3));
        Wings.Add(W);
        Chains.Add(M.GetCaptureGroup(1) + M.GetCaptureGroup(2));
    }
    // Roots before tips: a tip composes on its root's NEW orientation this frame.
    Wings.StableSort([](const FWing& A, const FWing& B) { return A.Depth < B.Depth; });
    bCanFoldWings = Chains.Num() >= 2 && Wings.Num() >= 4;
    WingRef = INDEX_NONE;
    for (const TCHAR* B : {TEXT("upperChest"), TEXT("chest"), TEXT("spine")}) if (const int32* I = HumanBoneIndex.Find(FName(B))) { WingRef = *I; break; }
}

void UEidoverseAvatarMesh::UpdateWings(float Dt)
{
    if (Wings.IsEmpty() || BoneSpaceTransforms.Num() != Reference.Num() || !Avatar || !Avatar->Mesh) return;
    if (bLimp) { WingBlend = 0; for (FWing& W : Wings) W.From = BoneSpaceTransforms[W.Bone].GetRotation(); return; }
    const FReferenceSkeleton& Ref = Avatar->Mesh->GetRefSkeleton();
    auto ComponentRotation = [&](int32 I) { FQuat R = FQuat::Identity; while (I != INDEX_NONE) { R = BoneSpaceTransforms[I].GetRotation() * R; I = Ref.GetParentIndex(I); } return R; };
    // Effort comes on fast (τ .18) and bleeds off slowly (τ .55); folding eases over τ .45.
    const float Want = FMath::Clamp(WingEffortWant, 0.f, 1.f);
    WingEffort += (Want - WingEffort) * (1 - FMath::Exp(-Dt / (Want > WingEffort ? .18f : .55f)));
    WingFold += ((bWingsFoldedWant ? 1.f : 0.f) - WingFold) * (1 - FMath::Exp(-Dt / .45f));
    const FWingDials D = WingEffort < .001f ? WingIdle : Mix(WingIdle, WingPower, WingEffort);
    WingBlend = FMath::Min(1.f, WingBlend + Dt / FMath::Max(.01f, D.Recover));
    WingT += Dt * FMath::Max(0.f, D.Hz); WingT -= FMath::FloorToFloat(WingT);
    // The body's forward and up from the chest's live frame (rest faces +X, or -X for VRM 0.x).
    FQuat Delta = FQuat::Identity;
    if (WingRef != INDEX_NONE) Delta = ComponentRotation(WingRef) * ReferenceComponentRotations[WingRef].Inverse();
    const FVector Forward = Delta.RotateVector(Avatar->bVRM0 ? FVector(-1, 0, 0) : FVector(1, 0, 0)).GetSafeNormal();
    const FVector Up = Delta.RotateVector(FVector(0, 0, 1)).GetSafeNormal();
    for (FWing& W : Wings)
    {
        const float Ph = WingT - D.Lag * W.Depth - (W.bLower ? 0.f : 0.f);   // idle dials are sync: lowers beat with uppers
        const float Amp = D.Deg * FMath::Pow(D.Tip, float(W.Depth));
        const float Ang = FMath::DegreesToRadians((Amp * FMath::Sin(Ph * 2 * PI) + D.Bias) * W.Side);
        const float Sw = FMath::DegreesToRadians(D.Sweep * FMath::Pow(D.Tip, float(W.Depth)) * FMath::Sin((Ph + D.SweepPhase) * 2 * PI) * -W.Side);
        // Right-handed angles about mapped axes flip sign in UE's left-handed space.
        const FQuat R = FQuat(Up, -Sw) * FQuat(Forward, -Ang);
        const int32 Parent = Ref.GetParentIndex(W.Bone);
        const FQuat Pw = Parent >= 0 ? ComponentRotation(Parent) : FQuat::Identity;
        const FQuat Rest = Reference[W.Bone].GetRotation();
        FQuat Local = (Pw.Inverse() * R * Pw * Rest).GetNormalized();
        if (WingFold > .001f)
            if (const FQuat* F = WingFolded().Find(W.Name))
                Local = FQuat::Slerp(Local, (Rest * AEidoverseBridge::FromWireRotation(*F)).GetNormalized(), WingFold);
        if (WingBlend < 1 && !W.From.Equals(FQuat::Identity, 0)) Local = FQuat::Slerp(W.From, Local, WingBlend);
        BoneSpaceTransforms[W.Bone].SetRotation(Local.GetNormalized());
    }
}
