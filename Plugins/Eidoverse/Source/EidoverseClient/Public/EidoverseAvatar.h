#pragma once
#include "CoreMinimal.h"
#include "EidoverseSprings.h"
#include "Components/PoseableMeshComponent.h"
#include "EidoverseAvatar.generated.h"

class UglTFRuntimeAsset;
class IHttpRequest;
class FJsonObject;
class UMaterialInterface;
class UPointLightComponent;
class UMaterialInstanceDynamic;
struct FglTFRuntimeMaterialsConfig;
struct FglTFRuntimeConfig;
struct FglTFRuntimeStaticMeshConfig;

// Pure motion targets: velocity is metres/second in the participant's local frame.
// Keeping this independent of a particular skeleton makes VRM rigs share the gait.
struct FEidoverseSwimMotion
{
    bool bAquatic=false;
    FString State=TEXT("Standing");
    float Lean=0, Bank=0, Effort=0, Dive=0, Surface=0, CycleHz=.28f;
    static FEidoverseSwimMotion Evaluate(const FString& Mode,float Speed,const FVector& LocalVelocity);
};

USTRUCT()
struct FEidoverseAvatarAttachment
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<UStaticMesh> Mesh;
    FName Bone;
    FTransform Transform;
};

struct FEidoverseAvatarLamp
{
    int32 MaterialIndex=INDEX_NONE;
    FName Bone;
    FVector Offset=FVector::ZeroVector;
    FLinearColor Color=FLinearColor::White;
    float Intensity=0,Radius=2,EmissiveStrength=1;
    bool bBreath=false;
    static float InferredIntensity(const FLinearColor& Emission,float Strength);
    static float Breath(double Time);
};

// A version-keyed, session-only shared load. No downloaded files are persisted.
UCLASS()
class EIDOVERSECLIENT_API UEidoverseAvatarAsset : public UObject
{
    GENERATED_BODY()
public:
    enum class EState { Queued, Loading, Ready, Failed };
    EState State=EState::Queued;
    // A failed body retries 3 times, at 0.4 / 1.6 / 5 s (remotes.js), unless cancelled.
    int32 Retries=0; double RetryAt=0;
    bool RetryAfterFailure();
    FString Url;
    FString Failure;
    bool bVRM0=false;
    bool bStaticModel=false;
    // Imported un-mirrored: display with component scale (1,-1,1) (glTF x = UE -Y).
    bool bMirrored=false;
    double StartedAt=0,DecodedAt=0; // load trace (EidoverseLoadTrace.h)
    double CookStartedAt=0;
    // Built, collision still cooking on a worker.
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> PendingStaticMesh;
    // The rooted async job must not belong to a PIE world. Collision meshes
    // still need a world-aware (unrooted) outer for runtime physics cooking.
    TWeakObjectPtr<UObject> StaticMeshOuter;
    UPROPERTY() TObjectPtr<UStaticMesh> StaticMesh;
    // Named parts (motion:<part>, picture, part sockets): loaded as their own meshes so
    // they can move; the base mesh excludes their subtrees. Vertices stay in model space;
    // rest transforms are in glTF numbers (metres, right-handed) for the motion law.
    TArray<FString> SplitParts;
    struct FPartMesh { FString Name; FTransform RestLocal; FMatrix ParentWorld=FMatrix::Identity; int32 Mesh=INDEX_NONE; };
    TArray<FPartMesh> PartMeshes;
    UPROPERTY() TArray<TObjectPtr<UStaticMesh>> PartMeshObjects;
    TMap<FName,FName> Humanoid;
    UPROPERTY() TObjectPtr<USkeletalMesh> Mesh;
    UPROPERTY() TArray<FEidoverseAvatarAttachment> Attachments;
    TArray<FEidoverseAvatarLamp> Lamps;
    void BuildStaticLamps();
    // Hair, tails, petals, skirts (VRM spring bones), parsed at load.
    FEidoSpringData Springs;
    // VRM look-at (eyes): bone or expression type, with the four range maps (degrees in,
    // degrees or weight out). Defaults per VRM 1.0.
    bool bLookAtExpressions=false;
    FVector2f LookHorizontalInner{90,10},LookHorizontalOuter{90,10},LookVerticalDown{90,10},LookVerticalUp{90,10};
    // VRM expressions (blink, aa, happy, ...) -> blend shape names and bind weights.
    TMap<FName,TArray<TPair<FName,float>>> Expressions;
    void ParseExpressions(const TSharedPtr<class FJsonObject>& Json);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eidoverse|Materials") int32 AdaptedMaterialCount=0;
    bool PrepareLitMaterials(const TSharedPtr<FJsonObject>& Document);
    FglTFRuntimeMaterialsConfig MaterialConfig() const;
    static FglTFRuntimeConfig ImportConfig();
    static FglTFRuntimeStaticMeshConfig StaticGeometryConfig();
    // Mirrored models (node transforms with negative determinant) are imported
    // un-mirrored; bMirrored then tells the renderer to apply scale (1,-1,1).
    static bool UnmirrorDocument(const TSharedPtr<FJsonObject>& Document);
    static bool ReplaceGlbJson(TArray<uint8>& Bytes,const TSharedPtr<FJsonObject>& Document);
    void Start();
    void Cancel();
    static bool ResolveUrl(const FString& Base,const FString& Reference,FString& Out);
    static bool ValidateGlb(const TArray<uint8>& Data,TSharedPtr<FJsonObject>& Json,FString& Error,bool bRequireSkin=true);
private:
    UPROPERTY() TObjectPtr<UglTFRuntimeAsset> Loader;
    UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> LitTemplates;
    UPROPERTY() TMap<int32,TObjectPtr<UMaterialInterface>> LitParents;
    TSharedPtr<IHttpRequest,ESPMode::ThreadSafe> Request;
    bool bCancelled=false;
    bool bRootedForBuild=false;
    void Decode(const TArray<uint8>& Bytes);
    void FinishDecode(const TSharedPtr<FJsonObject>& Json,TSharedRef<class FglTFRuntimeParser> Parser);
    bool bRootedForDecode=false;
    void Fail(const FString& Why);
    void BuildEmissiveLamps();
    UFUNCTION() void MeshReady(USkeletalMesh* Result);
    UFUNCTION() void PartMeshReady(UStaticMesh* Result);
    void LoadNextPart();
    void LoadBaseStatic();
    int32 NextPart=0;
    TArray<FString> BaseExclude;
    UFUNCTION() void StaticMeshReady(UStaticMesh* Result);
};

// Each participant owns its own pose, while meshes/materials are shared.
UCLASS()
class EIDOVERSECLIENT_API UEidoverseAvatarMesh : public UPoseableMeshComponent
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<UEidoverseAvatarAsset> Avatar;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> RigidParts;
    // Rigid parts and lamp lights are the owner's components, only attached to this
    // mesh: destroying the mesh (a re-join, an avatar change) must take them along,
    // or they detach and float beside the next body at their last offset.
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse|Lighting") TArray<TObjectPtr<UPointLightComponent>> EmissiveLights;
    void UpdateEmissiveLights(double Time,float Dayness,bool bEnabled);
    void SetAvatar(UEidoverseAvatarAsset* Asset);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eidoverse|Animation") FString AnimationState;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eidoverse|Animation") float BodyLeanDegrees=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eidoverse|Animation") float AquaticBlend=0;
    void Animate(const FString& Mode,float Speed,const FVector& LocalVelocity,double Time);
    UFUNCTION(BlueprintCallable,Category="Eidoverse|Animation") bool HumanBonePosition(FName Bone,FVector& WorldPosition) const;
    // Two-bone reach toward a world point, blended over the animated pose by Weight.
    bool SolveReachWeighted(FName Limb,const FVector& WorldTarget,float Weight);
    bool SolveReach(FName Limb,const FVector& WorldTarget);
    void ApplyGesture(const FString& Gesture,double Time);

    // ---- VRM Animation clips (EidoverseAvatarClips.cpp), as the browser plays them ----
    // Crossfade to a looping clip (no-op when it is already the one playing; the rate
    // still updates). FadeSeconds 0.22 by default, 0.1 into a jump.
    void SetClip(TSharedPtr<const struct FEidoClip> Clip,const FString& Slot,float Rate,float FadeSeconds=.22f);
    // A one-shot (emote): crossfades in, holds its last frame, fades out at its end.
    void PlayOneShot(TSharedPtr<const struct FEidoClip> Clip,const FString& Name);
    bool HasOneShot() const;
    void CancelOneShot();
    // A remote's clipTime/clipRate for the slot it is playing (poseclips.js syncClipPhase).
    void SyncClipPhase(const FString& Slot,float ClipTime,float Rate);
    // Pose from clips; false when nothing is playing yet (callers fall back to Animate).
    bool AnimateClips(double Time);
    void StopClips();
    FString CurrentClipSlot() const;
    float CurrentClipTime() const;
    float CurrentClipRate() const;
private:
    struct FClipPlay
    {
        TSharedPtr<const struct FEidoClip> Clip; FString Slot;
        float Time=0, Rate=1, Weight=0, Target=1, FadeSpeed=1/.22f;
        bool bLoop=true, bFinished=false;
    };
    TArray<FClipPlay> ClipPlays;
    TArray<FTransform> ReferenceComponent;
    TMap<FName,int32> HumanBoneIndex;
    float RestHipsHeight=1;                  // the body's hips height above its origin (m)
    double LastClipTime=-1, ClipBlendUntil=0;
    bool bLastFrameFromClips=false;
    TArray<FTransform> PreviousPose;
    void PrepareRetarget();
    // Spring bones (EidoverseSprings.cpp): simulated after every posed frame.
    struct FSpringState
    {
        const FEidoSpringJoint* Joint=nullptr; int32 Bone=INDEX_NONE, Parent=INDEX_NONE;
        FQuat InitialLocal=FQuat::Identity; FVector ChildLocal=FVector::ZeroVector, Axis=FVector::ZeroVector;
        float Length=0; FVector Tail=FVector::ZeroVector, PrevTail=FVector::ZeroVector; bool bInit=false;
    };
    TArray<FSpringState> SpringStates;
    float SpringAccumulator=0;
    bool bSpringsSettled=false;
    void PrepareSprings();
    void UpdateSprings(float Dt);
public:
    // ---- layers over the clip pose (EidoverseAvatarLayers.cpp), as avatar.js applies them ----
    // Head pitch (radians, positive looks down), on the normalized head.
    void SetHeadPitch(float Pitch) { HeadPitch=FMath::Clamp(Pitch,-.5f,.6f); }
    // A held pose ({bone: [x,y,z,w] | {q,t,s}}); null/empty clears (eases out).
    void SetHeldPose(const TSharedPtr<class FJsonObject>& Pose);
    void ClearHeldPose();
    // An agent animation ({bone:[{t,q}]}, dur, loop, replace).
    void PlayAgentAnimation(const TSharedPtr<class FJsonObject>& Tracks,float Duration,bool bLoop,bool bReplace);
    // Where the eyes look (world space); bHasTarget false drifts ahead (avatar.js gaze).
    void SetGazeTarget(const FVector& World,bool bHasTarget);
    // Seconds the mouth should move as if talking (chat/captions).
    void NoteSpeaking(double Until) { SpeakingUntil=FMath::Max(SpeakingUntil,Until); }
    // Eyes held shut (/eyes, limp): lids ease at 12/s. False when the rig has no lids.
    bool SetEyesHeldShut(bool bShut);
    bool EyesHeldShut() const { return bEyesHeld; }
    // Limp (a ragdolling owner streams clip 'ragdoll' + DRIVEN_BONES): the streamed pose
    // drives those bones at full weight, every other humanoid bone parks at rest, the
    // head stops pitching, eyes ease shut, springs go slack (avatar.js setLimp).
    void SetLimp(bool bOn);
    UFUNCTION(BlueprintPure,Category="Eidoverse|Animation") bool IsLimp() const { return bLimp; }
    // Wings (EidoverseWings.cpp): fold (G, wingsFolded on the wire) and effort (flight).
    void SetWings(bool bFolded, float Effort) { bWingsFoldedWant = bFolded; WingEffortWant = Effort; }
    bool CanFoldWings() const { return bCanFoldWings; }
    bool HasWings() const { return !Wings.IsEmpty(); }
    // Remote VR bodies (xrbody.js applyRemoteXR): head relative to the facing frame (glTF
    // quaternion), finger curls [lIndex,lGrip,rIndex,rGrip]; hands are solved by the bridge.
    void SetXR(bool bOn, const FQuat& HeadRel, const FVector4f& Curls) { bXR=bOn; XRHead=HeadRel; XRCurls=Curls; }
    // Per-frame interpolated pose (normalized rotations), applied immediately.
    void SetHeldPoseDirect(const TMap<FName,FQuat>& Pose);
    // Expression weights for this frame (0..1), applied with the pose (blend shapes).
    void SetExpression(FName Expression,float Weight);
    void ApplyExpressions();
    bool HasExpression(FName Expression) const { return Avatar&&Avatar->Expressions.Contains(Expression); }
private:
    TMap<FName,float> ExpressionWeights;
    struct FHeldBone { FQuat Q=FQuat::Identity; FVector T=FVector::ZeroVector, S=FVector::OneVector; bool bQ=false,bT=false,bS=false; };
    TMap<FName,FHeldBone> HeldPose;            // humanoid names or raw bone names
    float HeldWeight=0, HeldWant=0;
    struct FAnimKey { float T; FQuat Q; };
    struct FAnimLayer { TMap<FName,TArray<FAnimKey>> Tracks; float Duration=2, Time=0, Weight=0, Want=1; bool bLoop=false; };
    TArray<FAnimLayer> AnimLayers;
    float HeadPitch=0;
    FVector GazeGoal=FVector::ZeroVector, GazePoint=FVector::ZeroVector; bool bGazeTarget=false, bGazeInit=false;
    double SpeakingUntil=0, NextBlink=0, BlinkStart=-1;
    bool bEyesHeld=false, bEyesHeldBeforeLimp=false, bLimp=false; float LidHold=0;
    bool bXR=false; FQuat XRHead=FQuat::Identity; FVector4f XRCurls=FVector4f(0,0,0,0); FVector XRLook=FVector::ZeroVector;
    struct FWing { int32 Bone=INDEX_NONE; FName Name; float Side=1; bool bLower=false; int32 Depth=0; FQuat From=FQuat::Identity; };
    TArray<FWing> Wings;
    int32 WingRef=INDEX_NONE;
    bool bCanFoldWings=false, bWingsFoldedWant=false;
    float WingEffortWant=0, WingEffort=0, WingFold=0, WingT=0, WingBlend=1;
    void PrepareWings();
    void UpdateWings(float Dt);
    // Applied inside AnimateClips: modifies the normalized humanoid rotations / hips offset.
    void ApplyNormalizedLayers(TMap<FName,FQuat>& Normalized,FVector& HipsOffsetModel,float Dt,double Time);
    // Applied after spring bones: raw-bone held pose, custom eyes and eyelids, blink, mouth.
    void ApplyRawLayers(float Dt,double Time);
private:
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> EmissiveMaterials;
    void CreateEmissiveLights();
    TArray<FTransform> Reference;
    TArray<FQuat> ReferenceComponentRotations;
    double LastAnimationTime=-1, CyclePhase=0;
    float SmoothedEffort=0, SmoothedBank=0, SmoothedDive=0, SmoothedSurface=0;
    void RotateBone(FName HumanBone,const FVector& Axis,float Degrees);
};
