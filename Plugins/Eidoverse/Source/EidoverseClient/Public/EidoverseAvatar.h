#pragma once
#include "CoreMinimal.h"
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
    TMap<FName,FName> Humanoid;
    UPROPERTY() TObjectPtr<USkeletalMesh> Mesh;
    UPROPERTY() TArray<FEidoverseAvatarAttachment> Attachments;
    TArray<FEidoverseAvatarLamp> Lamps;
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
private:
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> EmissiveMaterials;
    void CreateEmissiveLights();
    TArray<FTransform> Reference;
    TArray<FQuat> ReferenceComponentRotations;
    double LastAnimationTime=-1, CyclePhase=0;
    float SmoothedEffort=0, SmoothedBank=0, SmoothedDive=0, SmoothedSurface=0;
    void RotateBone(FName HumanBone,const FVector& Axis,float Degrees);
};
