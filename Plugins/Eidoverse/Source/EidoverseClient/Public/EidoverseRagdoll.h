#pragma once
#include "CoreMinimal.h"

// C++ port of eidoverse-worlds-native client/lib/ragdoll.js (the Verlet body engine,
// with rigmeasure.js / bodyengine.js / shared/joints.js folded in). Plain C++, no
// UObject, no avatar dependency.
//
// Everything is WIRE space: metres, Y-up, right-handed (three.js), quaternions
// [x,y,z,w] carried in FQuat's X/Y/Z/W. Convert with AEidoverseBridge::FromWire* at the
// edges only. The math is a bit-faithful port of the three.js helpers ragdoll.js
// calls (Vector3/Quaternion/Matrix4, including Object3D.getWorldQuaternion's
// matrix decompose), so it reproduces Resources/Conformance/ragdoll.json; see
// Eidoverse.RagdollConformance and Tools/eidoverse/ragdoll_vectors.ts. When ragdoll.js
// changes, port the change and regenerate the vectors.
namespace EidoRagdoll
{
    // ragdoll.js TUNING (the solver half; the debug-panel switches are kept too).
    struct FTuning
    {
        double Gravity = -9.8;
        int32 Substeps = 2, Iter = 3, IterPinned = 16;
        double Damp = 0.98, SleepDamp = 0.8, Yield = 0.5, CapsuleSoft = 0.6;
        double TwistDamp = 0.88, TwistStiff = 26, TwistLag = 0;
        bool bOnFlex = true, bOnCone = true, bOnBehind = true, bOnHinge = true;
        bool bOnCapsule = true, bOnBrace = true, bOnTwist = true, bOnGround = true;
        bool bPaused = false, bPinVel = false;
        double SettleV = 0.06, SettleTime = 0.4, SettleReset = 3, Deadline = 8;
    };

    // Ground height under (X, Z), wire space. Default: 0.
    using FHeightFn = TFunction<double(double X, double Z)>;
    // colliders.js resolveColliders(pos, heightAt, r, tall): may push P.X/P.Z out of a
    // collider's sides (in place) and returns the height to rest on (terrain, or a
    // box top the point is above). Radius is horizontal, Tall the body height above
    // the point; the ragdoll passes its joint clearance for both. Default: HeightAt.
    using FResolveFn = TFunction<double(FVector& P, const FHeightFn& HeightAt, double Radius, double Tall)>;

    // rigmeasure.js snapshotPacker {j,p,v} + ragdoll.js `h` (transported hinge normals).
    struct FSnapshot
    {
        TArray<FName> J;
        TArray<double> P, V, H;
        double Dy = 0;
    };

    struct FInit
    {
        // Neutral rest (every humanoid local rotation identity) world positions of the
        // humanoid bones, with the root at RootPosition/RootYaw: avatar.restBonePositions().
        // Required: hips. Used: hips spine chest neck head, left/right UpperArm LowerArm
        // Hand UpperLeg LowerLeg Foot; upperChest, left/rightShoulder when the rig has them
        // (they only carry the parent chain and stay at StartLocal).
        TMap<FName, FVector> RestWorld;
        // Bone -> nearest humanoid ancestor present. Optional; default is the VRM humanoid
        // chain collapsed onto the bones present in RestWorld.
        TMap<FName, FName> Parents;
        FVector RootPosition = FVector::ZeroVector;
        double RootYaw = 0;          // avatar.root.rotation.y
        bool bVrm0 = false;          // three-vrm's VRM0 half-turn between root and normalized rig
        // Normalized local rotations at the moment of going limp (wire [x,y,z,w]);
        // absent = identity. Non-driven bones should be parked at identity (avatar.setLimp).
        TMap<FName, FQuat> StartLocal;
        TOptional<FVector> Lean;     // topple velocity, m/s (localbody.toppleVelocity)
        TMap<FName, FVector> SeedVelocity;   // joint -> m/s, inherited motion
        TOptional<FSnapshot> Seed;   // handover snapshot; outranks SeedVelocity
        FHeightFn HeightAt;
        FResolveFn ResolveColliders;
        FTuning Tuning;
    };

    struct FStepResult
    {
        // false once the sim was already done before this call (JS step() returned null)
        bool bActive = false;
        // DRIVEN_BONES -> normalized local quaternion, rounded to 4 decimals like the
        // streamed pose wire (insertion order = the drive order)
        TMap<FName, FQuat> Pose;
        FVector RootPosition = FVector::ZeroVector;
        double RootYaw = 0;
        bool bDone = false;          // captured: Pose is the final held pose
        bool bSettled = false;       // ...because it came to rest (else the deadline fired)
        double MaxV = 0;
    };

    // DRIVEN_BONES, in drive order: the keys a pose can carry.
    const TArray<FName>& DrivenBones();
    // JOINTS: the simulated particles.
    const TArray<FName>& Joints();

    class EIDOVERSECLIENT_API FRagdoll
    {
    public:
        explicit FRagdoll(const FInit& Init);
        ~FRagdoll();
        FRagdoll(const FRagdoll&) = delete;
        FRagdoll& operator=(const FRagdoll&) = delete;

        // Advance by a render frame (fixed 1/60 internal step, at most 4 per call).
        const FStepResult& Step(double Dt);
        // A shove landing mid-tumble (capped at 8 m/s); restarts settle and the deadline.
        void Impulse(const FVector& V);
        // Pin a joint to a world target (a hand, a nail). bFirm is accepted for engine
        // parity and unused, as in JS. Settle and the deadline are suspended while pinned.
        void PinJoint(FName Joint, const FVector& Target, bool bFirm = false);
        void Unpin(FName Joint);
        void UnpinAll();
        bool IsPinned() const;
        bool IsDone() const;
        FSnapshot Snapshot() const;

        // Introspection / handover helpers.
        bool GetParticle(FName Joint, FVector& Out) const;
        double GetMaxV() const;
        double GetSettledFor() const;
        double GetElapsed() const;
        FVector GetRootPosition() const;
        // Exact (unrounded) normalized local rotations of every bone in the internal rig.
        TMap<FName, FQuat> GetLocalRotations() const;
        // restBonePositions() for a re-created sim: the neutral rig with the root moved.
        TMap<FName, FVector> RestPositionsAt(const FVector& RootPosition) const;
        const FStepResult& LastResult() const;

    private:
        struct FImpl;
        TUniquePtr<FImpl> Impl;
    };
}
