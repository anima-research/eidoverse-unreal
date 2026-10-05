#pragma once
#include "CoreMinimal.h"

// VRM spring bones (hair, tails, petals, skirts): VRMC_springBone 1.0 and VRM 0.x
// secondaryAnimation, simulated as @pixiv/three-vrm's VRMSpringBoneJoint does.
// Everything here is in the body's own (component) space, UE axes, centimetres;
// bone names are the skeleton's (glTF node) names.
struct FEidoSpringCollider
{
    FName Bone;
    FVector Offset = FVector::ZeroVector;   // bone-local
    FVector Tail = FVector::ZeroVector;     // capsule end, bone-local
    float Radius = 0;
    bool bCapsule = false;
};

struct FEidoSpringJoint
{
    FName Bone;
    FName Child;                             // NAME_None: a leaf, extended along its own bone
    float HitRadius = 0, Stiffness = 1, GravityPower = 0, DragForce = .4f;
    FVector GravityDir = FVector(0, 0, -1);  // body space
    TArray<int32> Colliders;                 // into FEidoSpringData::Colliders
};

struct EIDOVERSECLIENT_API FEidoSpringData
{
    TArray<FEidoSpringJoint> Joints;         // chains root -> tip, in order
    TArray<FEidoSpringCollider> Colliders;
    // NodeName maps a glTF node index to the skeleton bone name (empty when none).
    static FEidoSpringData Parse(const TSharedPtr<class FJsonObject>& Gltf, TFunctionRef<FString(int32)> NodeName);
    bool IsEmpty() const { return Joints.IsEmpty(); }
};
