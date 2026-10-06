// Layers over the clip pose, as client/lib/avatar.js applies them each frame:
// head pitch -> held pose -> agent animations -> eyes (VRM look-at), all on the
// normalized humanoid rotations (glTF axes, identity = rest); then, after spring
// bones, the raw-bone part of a held pose, custom eye/eyelid rigs, blink and mouth.
#include "EidoverseAvatar.h"
#include "EidoverseBridge.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    FQuat LayerToBody(FQuat Q, bool bVRM0) { if (bVRM0) { Q.X = -Q.X; Q.Z = -Q.Z; } return AEidoverseBridge::FromWireRotation(Q); }
    FVector LayerToBodyVector(FVector V, bool bVRM0) { if (bVRM0) { V.X = -V.X; V.Z = -V.Z; } return AEidoverseBridge::FromWirePosition(V); }   // metres -> cm
    FVector LayerToModel(const FVector& BodyCm, bool bVRM0) { FVector W = AEidoverseBridge::ToWirePosition(BodyCm); if (bVRM0) { W.X = -W.X; W.Z = -W.Z; } return W; }

    bool LayerReadQuat(const TSharedPtr<FJsonValue>& V, FQuat& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A;
        if (!V || !V->TryGetArray(A) || A->Num() != 4) return false;
        Out = FQuat((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber(), (*A)[3]->AsNumber());
        if (Out.ContainsNaN() || Out.SizeSquared() < 1e-6) return false;
        Out.Normalize(); return true;
    }
    bool LayerReadVec(const TSharedPtr<FJsonValue>& V, FVector& Out, bool bAllowScalar)
    {
        if (!V) return false;
        double N;
        if (bAllowScalar && V->TryGetNumber(N) && FMath::IsFinite(N)) { Out = FVector(N); return true; }
        const TArray<TSharedPtr<FJsonValue>>* A;
        if (!V->TryGetArray(A) || A->Num() != 3) return false;
        Out = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
        return !Out.ContainsNaN();
    }
    float LayerMapRange(const FVector2f& Map, float Degrees) { return Map.Y * FMath::Clamp(Degrees / FMath::Max(Map.X, 1e-3f), 0.f, 1.f); }
    const FName LayerHips(TEXT("hips")), LayerHead(TEXT("head")), LayerLeftEye(TEXT("leftEye")), LayerRightEye(TEXT("rightEye"));
}

void UEidoverseAvatarMesh::SetHeldPose(const TSharedPtr<FJsonObject>& Pose)
{
    if (!Pose || Pose->Values.IsEmpty()) { ClearHeldPose(); return; }
    TMap<FName, FHeldBone> Next;
    for (const auto& Pair : Pose->Values)
    {
        if (Next.Num() >= 256) break;
        FHeldBone B;
        if (LayerReadQuat(Pair.Value, B.Q)) B.bQ = true;
        else if (const TSharedPtr<FJsonObject>* O; Pair.Value && Pair.Value->TryGetObject(O))
        {
            B.bQ = LayerReadQuat((*O)->TryGetField(TEXT("q")), B.Q);
            B.bT = LayerReadVec((*O)->TryGetField(TEXT("t")), B.T, false);
            B.bS = LayerReadVec((*O)->TryGetField(TEXT("s")), B.S, true);
        }
        if (B.bQ || B.bT || B.bS) Next.Add(FName(FString(Pair.Key)), B);
    }
    if (Next.IsEmpty()) { ClearHeldPose(); return; }
    HeldPose = MoveTemp(Next);
    HeldWant = 1;
}

void UEidoverseAvatarMesh::ClearHeldPose()
{
    HeldWant = 0;
}

void UEidoverseAvatarMesh::PlayAgentAnimation(const TSharedPtr<FJsonObject>& Tracks, float Duration, bool bLoop, bool bReplace)
{
    if (!Tracks) return;
    FAnimLayer Layer;
    Layer.Duration = FMath::Clamp(FMath::IsFinite(Duration) && Duration > 0 ? Duration : 2.f, .1f, 30.f);
    Layer.bLoop = bLoop;
    for (const auto& Pair : Tracks->Values)
    {
        const TArray<TSharedPtr<FJsonValue>>* Keys;
        if (!Pair.Value || !Pair.Value->TryGetArray(Keys)) continue;
        TArray<FAnimKey> Out;
        for (const auto& K : *Keys)
        {
            const TSharedPtr<FJsonObject> O = K ? K->AsObject() : nullptr; double T; FQuat Q;
            if (O && O->TryGetNumberField(TEXT("t"), T) && FMath::IsFinite(T) && LayerReadQuat(O->TryGetField(TEXT("q")), Q)) Out.Add({float(T), Q});
            if (Out.Num() >= 64) break;
        }
        if (Out.IsEmpty()) continue;
        Out.Sort([](const FAnimKey& A, const FAnimKey& B) { return A.T < B.T; });
        Layer.Tracks.Add(FName(FString(Pair.Key)), MoveTemp(Out));
    }
    if (Layer.Tracks.IsEmpty()) return;
    if (bReplace) AnimLayers.Reset();
    else for (FAnimLayer& Other : AnimLayers) for (const auto& T : Layer.Tracks) Other.Tracks.Remove(T.Key);
    AnimLayers.Add(MoveTemp(Layer));
}

void UEidoverseAvatarMesh::SetLimp(bool bOn)
{
    if (bLimp == bOn) return;
    bLimp = bOn;
    if (bOn) { bEyesHeldBeforeLimp = bEyesHeld; bEyesHeld = true; AnimLayers.Reset(); if (HasOneShot()) CancelOneShot(); }
    else { bEyesHeld = bEyesHeldBeforeLimp; HeldWant = HeldPose.IsEmpty() ? 0.f : 1.f; }
}

void UEidoverseAvatarMesh::SetHeldPoseDirect(const TMap<FName, FQuat>& Pose)
{
    for (const auto& Pair : Pose)
    {
        FHeldBone& B = HeldPose.FindOrAdd(Pair.Key);
        B.Q = Pair.Value; B.bQ = true;
    }
    HeldWant = 1;
    if (bLimp) HeldWeight = 1;
}

bool UEidoverseAvatarMesh::SetEyesHeldShut(bool bShut)
{
    bool bHasLids = HasExpression(TEXT("blink"));
    if (!bHasLids && Avatar && Avatar->Mesh)
        for (const TCHAR* N : {TEXT("L_Eyelid_Upper"), TEXT("R_Eyelid_Upper")}) bHasLids |= Avatar->Mesh->GetRefSkeleton().FindBoneIndex(FName(N)) != INDEX_NONE;
    if (!bHasLids) return false;
    bEyesHeld = bShut;
    return true;
}

void UEidoverseAvatarMesh::SetGazeTarget(const FVector& World, bool bHasTarget)
{
    GazeGoal = World; bGazeTarget = bHasTarget;
}

void UEidoverseAvatarMesh::ApplyNormalizedLayers(TMap<FName, FQuat>& Normalized, FVector& HipsOffsetModel, float Dt, double Time)
{
    const bool bVRM0 = Avatar && Avatar->bVRM0;
    if (bLimp)
    {
        // rigmeasure.js DRIVEN_BONES carry the streamed pose; everything else rests.
        static const TSet<FName> Driven = {TEXT("hips"), TEXT("spine"), TEXT("chest"), TEXT("neck"), TEXT("leftUpperArm"), TEXT("leftLowerArm"),
            TEXT("rightUpperArm"), TEXT("rightLowerArm"), TEXT("leftUpperLeg"), TEXT("leftLowerLeg"), TEXT("rightUpperLeg"), TEXT("rightLowerLeg")};
        for (auto& Pair : Normalized)
        {
            const FHeldBone* B = Driven.Contains(Pair.Key) ? HeldPose.Find(Pair.Key) : nullptr;
            Pair.Value = B && B->bQ ? B->Q : FQuat::Identity;
        }
        HeldWeight = 1;
        return;
    }
    // Head pitch: a rotation about X in the parent's frame (premultiplied).
    if (FQuat* H = Normalized.Find(LayerHead)) *H = (FQuat(FVector(1, 0, 0), HeadPitch) * *H).GetNormalized();

    // Held pose: absolute normalized rotations, replacing the clip's by the eased weight.
    HeldWeight += (HeldWant - HeldWeight) * FMath::Min(1.f, 12 * Dt);
    if (HeldWant <= 0 && HeldWeight < .02f) { HeldPose.Reset(); HeldWeight = 0; }
    if (HeldWeight > 0)
        for (const auto& Pair : HeldPose)
        {
            if (FQuat* Q = Normalized.Find(Pair.Key)) { if (Pair.Value.bQ) *Q = FQuat::Slerp(*Q, Pair.Value.Q, HeldWeight).GetNormalized(); }
            if (Pair.Key == LayerHips && Pair.Value.bT) HipsOffsetModel += Pair.Value.T * HeldWeight;
        }

    // Agent animations: keyframes slerped, eased in/out like held poses.
    for (FAnimLayer& L : AnimLayers)
    {
        L.Time += Dt;
        if (L.Time >= L.Duration) { if (L.bLoop) L.Time = FMath::Fmod(L.Time, L.Duration); else { L.Time = L.Duration; L.Want = 0; } }
        L.Weight += (L.Want - L.Weight) * FMath::Min(1.f, 12 * Dt);
        for (const auto& Track : L.Tracks)
        {
            FQuat* Q = Normalized.Find(Track.Key);
            if (!Q) continue;
            const TArray<FAnimKey>& K = Track.Value;
            FQuat Sample = K[0].Q;
            if (L.Time >= K.Last().T) Sample = K.Last().Q;
            else for (int32 I = 0; I + 1 < K.Num(); ++I)
                if (L.Time < K[I + 1].T) { const float Span = K[I + 1].T - K[I].T; Sample = FQuat::Slerp(K[I].Q, K[I + 1].Q, Span > 0 ? FMath::Clamp((L.Time - K[I].T) / Span, 0.f, 1.f) : 1.f); break; }
            *Q = FQuat::Slerp(*Q, Sample, L.Weight).GetNormalized();
        }
    }
    AnimLayers.RemoveAll([](const FAnimLayer& L) { return L.Want <= 0 && L.Weight < .02f; });

    // A VR body: the head's look spread down the chain (replacing the clip there), fingers curled.
    if (bXR)
    {
        // three.js Euler 'YXZ' from the head quaternion.
        const FQuat& Q = XRHead;
        const double M13 = 2 * (Q.X * Q.Z + Q.W * Q.Y), M23 = 2 * (Q.Y * Q.Z - Q.W * Q.X), M33 = 1 - 2 * (Q.X * Q.X + Q.Y * Q.Y);
        const double M21 = 2 * (Q.X * Q.Y + Q.W * Q.Z), M22 = 1 - 2 * (Q.X * Q.X + Q.Z * Q.Z), M31 = 2 * (Q.X * Q.Z - Q.W * Q.Y), M11 = 1 - 2 * (Q.Y * Q.Y + Q.Z * Q.Z);
        const double Ex = FMath::Asin(-FMath::Clamp(M23, -1., 1.));
        const double Ey = FMath::Abs(M23) < .9999999 ? FMath::Atan2(M13, M33) : FMath::Atan2(-M31, M11);
        const double Ez = FMath::Abs(M23) < .9999999 ? FMath::Atan2(M21, M22) : 0;
        const double Pitch = FMath::Clamp(Ex, -.7, .7), Roll = FMath::Clamp(Ez, -.5, .5);
        const float S = Dt > 0 ? FMath::Min(1.f, Dt * 9) : .5f;
        XRLook.X += (Pitch - XRLook.X) * S; XRLook.Y += (Ey - XRLook.Y) * S; XRLook.Z += (Roll - XRLook.Z) * S;
        const double YawFade = FMath::Max(0., FMath::Cos(XRLook.X));
        static const TCHAR* Chain[] = {TEXT("spine"), TEXT("chest"), TEXT("upperChest"), TEXT("neck"), TEXT("head")};
        static const double WY[] = {.12, .12, .16, .25, .35}, WP[] = {.10, .12, .16, .26, .36}, WR[] = {.08, .10, .14, .28, .40};
        double SY = 0, SP = 0, SR = 0;
        for (int32 I = 0; I < 5; ++I) if (Normalized.Contains(FName(Chain[I]))) { SY += WY[I]; SP += WP[I]; SR += WR[I]; }
        auto FromYXZ = [](double X, double Y, double Z)
        {
            const double C1 = FMath::Cos(X / 2), C2 = FMath::Cos(Y / 2), C3 = FMath::Cos(Z / 2), S1 = FMath::Sin(X / 2), S2 = FMath::Sin(Y / 2), S3 = FMath::Sin(Z / 2);
            return FQuat(S1 * C2 * C3 + C1 * S2 * S3, C1 * S2 * C3 - S1 * C2 * S3, C1 * C2 * S3 - S1 * S2 * C3, C1 * C2 * C3 + S1 * S2 * S3);
        };
        for (int32 I = 0; I < 5; ++I)
            if (FQuat* B = Normalized.Find(FName(Chain[I])))
                *B = FromYXZ(XRLook.X * WP[I] / FMath::Max(SP, 1e-6), XRLook.Y * WY[I] / FMath::Max(SY, 1e-6) * YawFade, XRLook.Z * WR[I] / FMath::Max(SR, 1e-6));
        // Curls: distal 1.22 / 1.57 / 0.96 rad for a full fist, handed sign, onto the clip's pose.
        static const double Curl[] = {1.22, 1.57, .96};
        for (const TCHAR* Side : {TEXT("left"), TEXT("right")})
        {
            const bool bLeft = Side[0] == 'l'; const double Sgn = bLeft ? -1 : 1;
            for (const TPair<const TCHAR*, int32>& F : {TPair<const TCHAR*, int32>{TEXT("Index"), 0}, {TEXT("Middle"), 1}, {TEXT("Ring"), 1}, {TEXT("Little"), 1}})
            {
                const float Amt = FMath::Clamp(bLeft ? (F.Value ? XRCurls.Y : XRCurls.X) : (F.Value ? XRCurls.W : XRCurls.Z), 0.f, 1.f);
                const TCHAR* Segs[] = {TEXT("Proximal"), TEXT("Intermediate"), TEXT("Distal")};
                for (int32 I = 0; I < 3; ++I)
                    if (FQuat* B = Normalized.Find(FName(FString(Side) + F.Key + Segs[I]))) *B = (*B * FQuat(FVector(0, 0, 1), Sgn * Curl[I] * Amt)).GetNormalized();
            }
        }
    }
    // Gaze point: eases toward its target, or drifts 1.5 m up and 3 m ahead.
    if (const AActor* Owner = GetOwner())
    {
        const FVector Drift = GetComponentLocation() + Owner->GetActorForwardVector() * 300 + FVector(0, 0, 150);
        const FVector Goal = bGazeTarget ? GazeGoal : Drift;
        if (!bGazeInit) { GazePoint = Goal; bGazeInit = true; }
        GazePoint = FMath::Lerp(GazePoint, Goal, 1 - FMath::Exp(-(bGazeTarget ? 6.f : 3.f) * Dt));
    }
    // VRM look-at (three-vrm VRMLookAt): yaw/pitch of the gaze in the head's frame.
    if (!Avatar || !HumanBoneIndex.Contains(LayerHead)) return;
    FQuat HeadWorld = FQuat::Identity;
    for (const TCHAR* Bone : {TEXT("hips"), TEXT("spine"), TEXT("chest"), TEXT("upperChest"), TEXT("neck"), TEXT("head")})
        if (const FQuat* Q = Normalized.Find(FName(Bone))) HeadWorld = HeadWorld * *Q;
    const FVector HeadModel = LayerToModel(ReferenceComponent[HumanBoneIndex[LayerHead]].GetLocation(), bVRM0) + FVector(0, .06, 0) + HipsOffsetModel;
    const FVector TargetModel = LayerToModel(GetComponentTransform().InverseTransformPosition(GazePoint), bVRM0);
    const FVector D = HeadWorld.UnrotateVector(TargetModel - HeadModel);
    const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(D.X, D.Z));              // + = the body's left
    const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(D.Y, FVector2D(D.X, D.Z).Size()));   // + = up
    if (Avatar->bLookAtExpressions)
    {
        SetExpression(TEXT("lookLeft"), Yaw > 0 ? LayerMapRange(Avatar->LookHorizontalOuter, Yaw) : 0.f);
        SetExpression(TEXT("lookRight"), Yaw < 0 ? LayerMapRange(Avatar->LookHorizontalOuter, -Yaw) : 0.f);
        SetExpression(TEXT("lookUp"), Pitch > 0 ? LayerMapRange(Avatar->LookVerticalUp, Pitch) : 0.f);
        SetExpression(TEXT("lookDown"), Pitch < 0 ? LayerMapRange(Avatar->LookVerticalDown, -Pitch) : 0.f);
        return;
    }
    if (!Normalized.Contains(LayerLeftEye) && !Normalized.Contains(LayerRightEye)) return;
    // A positive X rotation looks down; a positive Y rotation turns toward +X (left).
    const float Ex = Pitch < 0 ? LayerMapRange(Avatar->LookVerticalDown, -Pitch) : -LayerMapRange(Avatar->LookVerticalUp, Pitch);
    const float LeftY = Yaw > 0 ? LayerMapRange(Avatar->LookHorizontalOuter, Yaw) : -LayerMapRange(Avatar->LookHorizontalInner, -Yaw);
    const float RightY = Yaw > 0 ? LayerMapRange(Avatar->LookHorizontalInner, Yaw) : -LayerMapRange(Avatar->LookHorizontalOuter, -Yaw);
    auto Eye = [Ex](float Ey) { return FQuat(FVector(0, 1, 0), FMath::DegreesToRadians(Ey)) * FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(Ex)); };   // Euler YXZ
    if (FQuat* Q = Normalized.Find(LayerLeftEye)) *Q = Eye(LeftY);
    if (FQuat* Q = Normalized.Find(LayerRightEye)) *Q = Eye(RightY);
}

void UEidoverseAvatarMesh::ApplyRawLayers(float Dt, double Time)
{
    if (!Avatar || !Avatar->Mesh || BoneSpaceTransforms.Num() != Reference.Num()) return;
    const FReferenceSkeleton& Ref = Avatar->Mesh->GetRefSkeleton();
    const bool bVRM0 = Avatar->bVRM0;
    auto ComponentRotation = [&](int32 I) { FQuat R = FQuat::Identity; while (I != INDEX_NONE) { R = BoneSpaceTransforms[I].GetRotation() * R; I = Ref.GetParentIndex(I); } return R; };
    auto ComponentTransform = [&](int32 I) { FTransform T = FTransform::Identity; while (I != INDEX_NONE) { T = T * BoneSpaceTransforms[I]; I = Ref.GetParentIndex(I); } return T; };

    // Held pose on raw bones (names that aren't humanoid), and t/s on any bone; after
    // spring bones, so a held pose wins over hair physics (avatar.js order).
    if (HeldWeight > 0)
        for (const auto& Pair : HeldPose)
        {
            const bool bHuman = HumanBoneIndex.Contains(Pair.Key);
            const int32 Index = bHuman ? HumanBoneIndex[Pair.Key] : Ref.FindBoneIndex(Pair.Key);
            if (Index == INDEX_NONE) continue;
            const int32 Parent = Ref.GetParentIndex(Index);
            const FHeldBone& B = Pair.Value;
            if (!bHuman && B.bQ)
            {
                const FQuat P = Parent >= 0 ? ReferenceComponentRotations[Parent] : FQuat::Identity;
                const FQuat Local = P.Inverse() * LayerToBody(B.Q, bVRM0) * P * Reference[Index].GetRotation();
                BoneSpaceTransforms[Index].SetRotation(FQuat::Slerp(BoneSpaceTransforms[Index].GetRotation(), Local, HeldWeight).GetNormalized());
            }
            if (B.bT && !(bHuman && Pair.Key == LayerHips))
            {
                const FVector Offset = Parent >= 0 ? ReferenceComponent[Parent].InverseTransformVector(LayerToBodyVector(B.T, bVRM0)) : LayerToBodyVector(B.T, bVRM0);
                BoneSpaceTransforms[Index].AddToTranslation(Offset * HeldWeight);
            }
            if (B.bS)
            {
                const FVector S(B.S.Z, B.S.X, B.S.Y);   // glTF (x,y,z) -> UE (z,x,y) axes
                BoneSpaceTransforms[Index].SetScale3D(BoneSpaceTransforms[Index].GetScale3D() * FMath::Lerp(FVector::OneVector, S, HeldWeight));
            }
        }

    // Custom eye rigs (L_Eye/R_Eye nodes, no humanoid eyes): turn toward the gaze, at
    // most 0.42 rad from the head's forward.
    if (!HumanBoneIndex.Contains(LayerLeftEye) && HumanBoneIndex.Contains(LayerHead))
    {
        const int32 HeadIndex = HumanBoneIndex[LayerHead];
        const FQuat HeadDelta = ComponentRotation(HeadIndex) * ReferenceComponentRotations[HeadIndex].Inverse();
        const FVector Forward = HeadDelta.RotateVector(bVRM0 ? FVector(-1, 0, 0) : FVector(1, 0, 0));
        const FVector GazeCS = GetComponentTransform().InverseTransformPosition(GazePoint);
        for (const TCHAR* Name : {TEXT("L_Eye"), TEXT("R_Eye")})
        {
            const int32 Index = Ref.FindBoneIndex(FName(Name));
            if (Index == INDEX_NONE) continue;
            const FVector Dir = (GazeCS - ComponentTransform(Index).GetLocation()).GetSafeNormal();
            if (Dir.IsNearlyZero()) continue;
            FQuat D = FQuat::FindBetweenNormals(Forward, Dir);
            const float Angle = D.GetAngle();
            if (Angle > .42f) D = FQuat::Slerp(FQuat::Identity, D, .42f / Angle);
            const int32 Parent = Ref.GetParentIndex(Index);
            const FQuat Pw = Parent >= 0 ? ComponentRotation(Parent) : FQuat::Identity;
            const FQuat Want = D * HeadDelta * ReferenceComponentRotations[Index];
            BoneSpaceTransforms[Index].SetRotation((Pw.Inverse() * Want).GetNormalized());
        }
    }

    // Blink: a 0.28 s sine; next one 2.2-6.4 s after this ends, or a quick double (22%).
    if (NextBlink <= 0) NextBlink = Time + 1.2 + FMath::FRand() * 3.6;
    if (BlinkStart < 0 && Time >= NextBlink) BlinkStart = Time;
    float Blink = 0;
    if (BlinkStart >= 0)
    {
        const double K = (Time - BlinkStart) / .28;
        if (K >= 1) { BlinkStart = -1; NextBlink = Time + (FMath::FRand() < .22f ? .22 : 2.2 + FMath::FRand() * 4.2); }
        else Blink = FMath::Sin(PI * K);
    }
    LidHold += ((bEyesHeld ? 1.f : 0.f) - LidHold) * FMath::Min(1.f, 12 * Dt);
    Blink = FMath::Max(Blink, LidHold);
    SetExpression(TEXT("blink"), Blink);
    // Eyelid bones on custom rigs: closed about X, 1.2 rad upper, -0.35 rad lower.
    for (const TCHAR* Name : {TEXT("L_Eyelid_Upper"), TEXT("R_Eyelid_Upper"), TEXT("L_Eyelid_Lower"), TEXT("R_Eyelid_Lower")})
    {
        const int32 Index = Ref.FindBoneIndex(FName(Name));
        if (Index == INDEX_NONE || Blink <= 0) continue;
        const float Closed = FString(Name).Contains(TEXT("Upper")) ? 1.2f : -.35f;
        BoneSpaceTransforms[Index].SetRotation((Reference[Index].GetRotation() * LayerToBody(FQuat(FVector(1, 0, 0), Closed * Blink), bVRM0)).GetNormalized());
    }

    // Mouth while speaking (no voice level here): avatar.js's fallback envelope on 'aa'.
    const float T = float(FMath::Fmod(Time, 1000.0));
    SetExpression(TEXT("aa"), Time < SpeakingUntil ? FMath::Max(0.f, .5f + .5f * FMath::Sin(17 * T) * FMath::Sin(7.3f * T)) * .65f : 0.f);
}

bool UEidoverseAvatarMesh::RestWorldWire(TMap<FName, FVector>& Out, const FVector& OriginCm) const
{
    Out.Reset();
    if (ReferenceComponent.IsEmpty()) return false;
    for (const auto& Pair : HumanBoneIndex)
        if (ReferenceComponent.IsValidIndex(Pair.Value))
            Out.Add(Pair.Key, AEidoverseBridge::ToWirePosition(GetComponentTransform().TransformPosition(ReferenceComponent[Pair.Value].GetLocation()) - OriginCm));
    return Out.Contains(LayerHips);
}

TArray<FString> UEidoverseAvatarMesh::BoneNames() const
{
    TArray<FString> Names;
    if (Avatar && Avatar->Mesh) for (const FMeshBoneInfo& B : Avatar->Mesh->GetRefSkeleton().GetRefBoneInfo()) Names.Add(B.Name.ToString());
    for (const auto& Pair : HumanBoneIndex) Names.AddUnique(Pair.Key.ToString());
    return Names;
}
