#include "EidoverseSprings.h"
#include "EidoverseAvatar.h"
#include "EidoverseBridge.h"
#include "Dom/JsonObject.h"

namespace
{
    using FJsonArray = TArray<TSharedPtr<FJsonValue>>;
    const FJsonArray& Arr(const TSharedPtr<FJsonObject>& J, const TCHAR* K) { static const FJsonArray E; const FJsonArray* A; return J && J->TryGetArrayField(K, A) ? *A : E; }
    TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& J, const TCHAR* K) { const TSharedPtr<FJsonObject>* O; return J && J->TryGetObjectField(K, O) ? *O : nullptr; }
    double Num(const TSharedPtr<FJsonObject>& J, const TCHAR* K, double D) { double V; return J && J->TryGetNumberField(K, V) && FMath::IsFinite(V) ? V : D; }
    FVector Vec3(const FJsonArray& A, const FVector& D) { return A.Num() == 3 ? FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()) : D; }
    FVector VecXYZ(const TSharedPtr<FJsonObject>& O, const FVector& D) { return O ? FVector(Num(O, TEXT("x"), D.X), Num(O, TEXT("y"), D.Y), Num(O, TEXT("z"), D.Z)) : D; }
    // glTF metres/axes -> body space (the importer's basis): positions and directions.
    FVector Pos(const FVector& G) { return AEidoverseBridge::FromWirePosition(G); }
    FVector Dir(const FVector& G) { return AEidoverseBridge::FromWirePosition(G).GetSafeNormal(); }
}

FEidoSpringData FEidoSpringData::Parse(const TSharedPtr<FJsonObject>& Gltf, TFunctionRef<FString(int32)> NodeName)
{
    FEidoSpringData D;
    const FJsonArray& Nodes = Arr(Gltf, TEXT("nodes"));
    auto Bone = [&](int32 Node) { const FString N = Node >= 0 && Node < Nodes.Num() ? NodeName(Node) : FString(); return N.IsEmpty() ? FName() : FName(N); };
    const TSharedPtr<FJsonObject> Ext = Obj(Gltf, TEXT("extensions"));

    if (const TSharedPtr<FJsonObject> S = Obj(Ext, TEXT("VRMC_springBone")))
    {
        for (const auto& CV : Arr(S, TEXT("colliders")))
        {
            const TSharedPtr<FJsonObject> C = CV->AsObject(); FEidoSpringCollider Col;
            Col.Bone = Bone(int32(Num(C, TEXT("node"), -1)));
            const TSharedPtr<FJsonObject> Shape = Obj(C, TEXT("shape"));
            const TSharedPtr<FJsonObject> Sphere = Obj(Shape, TEXT("sphere")), Capsule = Obj(Shape, TEXT("capsule"));
            const TSharedPtr<FJsonObject> Body = Sphere ? Sphere : Capsule;
            Col.Offset = Pos(Vec3(Arr(Body, TEXT("offset")), FVector::ZeroVector));
            Col.Radius = Num(Body, TEXT("radius"), 0) * 100;
            if (Capsule) { Col.bCapsule = true; Col.Tail = Pos(Vec3(Arr(Capsule, TEXT("tail")), FVector::ZeroVector)); }
            D.Colliders.Add(Col);   // keep indices aligned even when the node is unknown
        }
        TArray<TArray<int32>> Groups;
        for (const auto& GV : Arr(S, TEXT("colliderGroups")))
        { TArray<int32>& G = Groups.AddDefaulted_GetRef(); for (const auto& I : Arr(GV->AsObject(), TEXT("colliders"))) G.Add(int32(I->AsNumber())); }
        for (const auto& SV : Arr(S, TEXT("springs")))
        {
            const TSharedPtr<FJsonObject> Spring = SV->AsObject();
            TArray<int32> Colliders;
            for (const auto& GI : Arr(Spring, TEXT("colliderGroups"))) { const int32 G = int32(GI->AsNumber()); if (Groups.IsValidIndex(G)) for (int32 C : Groups[G]) if (D.Colliders.IsValidIndex(C) && !D.Colliders[C].Bone.IsNone()) Colliders.AddUnique(C); }
            const FJsonArray& Joints = Arr(Spring, TEXT("joints"));
            // The last joint is the chain's tail point, not a simulated joint (VRMC_springBone).
            for (int32 I = 0; I + 1 < Joints.Num(); ++I)
            {
                const TSharedPtr<FJsonObject> J = Joints[I]->AsObject(), Next = Joints[I + 1]->AsObject();
                FEidoSpringJoint Joint;
                Joint.Bone = Bone(int32(Num(J, TEXT("node"), -1))); Joint.Child = Bone(int32(Num(Next, TEXT("node"), -1)));
                if (Joint.Bone.IsNone()) continue;
                Joint.HitRadius = Num(J, TEXT("hitRadius"), 0) * 100;
                Joint.Stiffness = Num(J, TEXT("stiffness"), 1);
                Joint.GravityPower = Num(J, TEXT("gravityPower"), 0);
                Joint.GravityDir = Dir(Vec3(Arr(J, TEXT("gravityDir")), FVector(0, -1, 0)));
                Joint.DragForce = Num(J, TEXT("dragForce"), .5);
                Joint.Colliders = Colliders;
                D.Joints.Add(Joint);
            }
        }
        return D;
    }

    // VRM 0.x: groups of root bones; every descendant is a joint whose tail is its first
    // child. Offsets and gravity are in Unity's axes: z is negated (three-vrm v0 import).
    const TSharedPtr<FJsonObject> Secondary = Obj(Obj(Ext, TEXT("VRM")), TEXT("secondaryAnimation"));
    if (!Secondary) return D;
    TArray<TArray<int32>> Groups;
    for (const auto& GV : Arr(Secondary, TEXT("colliderGroups")))
    {
        const TSharedPtr<FJsonObject> G = GV->AsObject(); TArray<int32>& Indices = Groups.AddDefaulted_GetRef();
        const FName GroupBone = Bone(int32(Num(G, TEXT("node"), -1)));
        for (const auto& CV : Arr(G, TEXT("colliders")))
        {
            const TSharedPtr<FJsonObject> C = CV->AsObject(); FEidoSpringCollider Col; Col.Bone = GroupBone;
            FVector O = VecXYZ(Obj(C, TEXT("offset")), FVector::ZeroVector); O.Z = -O.Z;
            Col.Offset = Pos(O); Col.Radius = Num(C, TEXT("radius"), 0) * 100;
            Indices.Add(D.Colliders.Add(Col));
        }
    }
    for (const auto& BV : Arr(Secondary, TEXT("boneGroups")))
    {
        const TSharedPtr<FJsonObject> G = BV->AsObject();
        FEidoSpringJoint Base;
        Base.Stiffness = Num(G, TEXT("stiffiness"), Num(G, TEXT("stiffness"), 1));   // sic: the 0.x schema's spelling
        Base.GravityPower = Num(G, TEXT("gravityPower"), 0);
        FVector Gd = VecXYZ(Obj(G, TEXT("gravityDir")), FVector(0, -1, 0)); Gd.Z = -Gd.Z;
        Base.GravityDir = Dir(Gd);
        Base.DragForce = Num(G, TEXT("dragForce"), .4);
        Base.HitRadius = Num(G, TEXT("hitRadius"), .02) * 100;
        for (const auto& GI : Arr(G, TEXT("colliderGroups"))) { const int32 I = int32(GI->AsNumber()); if (Groups.IsValidIndex(I)) for (int32 C : Groups[I]) if (!D.Colliders[C].Bone.IsNone()) Base.Colliders.AddUnique(C); }
        TFunction<void(int32, int32)> Walk = [&](int32 Node, int32 Depth)
        {
            if (Node < 0 || Node >= Nodes.Num() || Depth > 64) return;
            const FJsonArray& Children = Arr(Nodes[Node]->AsObject(), TEXT("children"));
            FEidoSpringJoint J = Base; J.Bone = Bone(Node);
            J.Child = Children.Num() ? Bone(int32(Children[0]->AsNumber())) : FName();
            if (!J.Bone.IsNone()) D.Joints.Add(J);
            for (const auto& C : Children) Walk(int32(C->AsNumber()), Depth + 1);
        };
        for (const auto& R : Arr(G, TEXT("bones"))) Walk(int32(R->AsNumber()), 0);
    }
    return D;
}

// ---- runtime on a body --------------------------------------------------------

void UEidoverseAvatarMesh::PrepareSprings()
{
    SpringStates.Reset(); SpringAccumulator = 0; bSpringsSettled = false;
    if (!Avatar || !Avatar->Mesh || Avatar->Springs.IsEmpty()) return;
    const FReferenceSkeleton& Ref = Avatar->Mesh->GetRefSkeleton();
    for (const FEidoSpringJoint& J : Avatar->Springs.Joints)
    {
        const int32 Index = Ref.FindBoneIndex(J.Bone);
        if (Index == INDEX_NONE) continue;
        FSpringState S; S.Joint = &J; S.Bone = Index; S.Parent = Ref.GetParentIndex(Index);
        if (S.Parent == INDEX_NONE) continue;
        const int32 Child = J.Child.IsNone() ? INDEX_NONE : Ref.FindBoneIndex(J.Child);
        // The tail in the joint's own space: its child's rest offset, or 7 cm onward.
        S.ChildLocal = Child != INDEX_NONE && Ref.GetParentIndex(Child) == Index ? Reference[Child].GetTranslation()
            : Reference[Index].GetTranslation().GetSafeNormal() * 7.0;
        if (S.ChildLocal.IsNearlyZero()) continue;
        S.Axis = S.ChildLocal.GetSafeNormal();
        S.Length = S.ChildLocal.Size();
        S.InitialLocal = Reference[Index].GetRotation();
        SpringStates.Add(S);
    }
}

void UEidoverseAvatarMesh::UpdateSprings(float Dt)
{
    if (SpringStates.IsEmpty() || BoneSpaceTransforms.Num() != Reference.Num() || Dt <= 0) return;
    if (!WasRecentlyRendered(.5f) && bSpringsSettled) return;   // off screen: nothing to swing
    const FReferenceSkeleton& Ref = Avatar->Mesh->GetRefSkeleton();
    // Component space of the posed skeleton (clips/procedural already applied).
    TArray<FTransform> CS; CS.SetNum(BoneSpaceTransforms.Num());
    for (int32 I = 0; I < CS.Num(); ++I) { const int32 P = Ref.GetParentIndex(I); CS[I] = P >= 0 ? BoneSpaceTransforms[I] * CS[P] : BoneSpaceTransforms[I]; }
    auto Refresh = [&](int32 I) { const int32 P = Ref.GetParentIndex(I); CS[I] = P >= 0 ? BoneSpaceTransforms[I] * CS[P] : BoneSpaceTransforms[I]; };
    // Colliders in body space this frame.
    const TArray<FEidoSpringCollider>& Colliders = Avatar->Springs.Colliders;
    TArray<FVector> ColA, ColB; TArray<int32> ColBone; ColA.SetNum(Colliders.Num()); ColB.SetNum(Colliders.Num()); ColBone.Init(INDEX_NONE, Colliders.Num());
    for (int32 C = 0; C < Colliders.Num(); ++C)
    {
        const int32 B = Colliders[C].Bone.IsNone() ? INDEX_NONE : Ref.FindBoneIndex(Colliders[C].Bone);
        ColBone[C] = B;
        if (B == INDEX_NONE) continue;
        ColA[C] = CS[B].TransformPosition(Colliders[C].Offset);
        ColB[C] = Colliders[C].bCapsule ? CS[B].TransformPosition(Colliders[C].Tail) : ColA[C];
    }
    // Fixed 60 Hz steps, as a stable stand-in for three-vrm's per-frame delta.
    SpringAccumulator = FMath::Min(SpringAccumulator + Dt, .1f);
    const float Step = 1.f / 60;
    const int32 Steps = FMath::Max(1, FMath::FloorToInt(SpringAccumulator / Step));
    SpringAccumulator = FMath::Max(0.f, SpringAccumulator - Steps * Step);
    for (FSpringState& S : SpringStates)
    {
        const FEidoSpringJoint& J = *S.Joint;
        BoneSpaceTransforms[S.Bone].SetRotation(S.InitialLocal);
        Refresh(S.Bone);
        const FQuat ParentRotation = CS[S.Parent].GetRotation();
        const FVector Head = CS[S.Bone].GetLocation();
        const FVector RestDir = (ParentRotation * S.InitialLocal).RotateVector(S.Axis);
        if (!S.bInit) { S.Tail = S.PrevTail = Head + RestDir * S.Length; S.bInit = true; }
        for (int32 K = 0; K < Steps; ++K)
        {
            // three-vrm VRMSpringBoneJoint.update, in centimetres.
            FVector Next = S.Tail + (S.Tail - S.PrevTail) * (1 - J.DragForce)
                + RestDir * (J.Stiffness * Step * 100) + J.GravityDir * (J.GravityPower * Step * 100);
            Next = Head + (Next - Head).GetSafeNormal() * S.Length;
            for (int32 C : J.Colliders)
            {
                if (ColBone[C] == INDEX_NONE) continue;
                const FVector Closest = Colliders[C].bCapsule ? FMath::ClosestPointOnSegment(Next, ColA[C], ColB[C]) : ColA[C];
                const FVector Delta = Next - Closest;
                const float Distance = Delta.Size() - Colliders[C].Radius - J.HitRadius;
                if (Distance < 0 && Delta.Size() > KINDA_SMALL_NUMBER)
                    Next = Head + ((Next - Delta.GetSafeNormal() * Distance) - Head).GetSafeNormal() * S.Length;
            }
            S.PrevTail = S.Tail; S.Tail = Next;
        }
        // Point the bone at its tail: initialLocal * rotation(axis -> tail in rest frame).
        const FVector To = (ParentRotation * S.InitialLocal).UnrotateVector(S.Tail - Head).GetSafeNormal();
        if (!To.IsNearlyZero())
        {
            BoneSpaceTransforms[S.Bone].SetRotation((S.InitialLocal * FQuat::FindBetweenNormals(S.Axis, To)).GetNormalized());
            Refresh(S.Bone);
        }
    }
    bSpringsSettled = true;
    MarkRefreshTransformDirty();
}
