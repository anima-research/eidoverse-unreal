#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "EidoverseScene.generated.h"
class FJsonObject;
class UEidoverseAvatarAsset;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UStaticMesh;
class UDirectionalLightComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class AWaterOpticsController;
class ASpectralOcean;
class APostProcessVolume;
class UBoxComponent;
namespace EidoWater { struct FParams; }

struct FEidoverseSceneItem
{
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<UStaticMeshComponent> Model;
    TWeakObjectPtr<UEidoverseAvatarAsset> Asset;
    FString Revision;
    FTransform Target;
    bool bPlaced=false;
    TSharedPtr<FJsonObject> Interact;
    bool bCollide=true;
    // Client-evaluated closed-form motion (render-scene transform.motion).
    TSharedPtr<FJsonObject> Motion;
    TSharedPtr<FJsonObject> MotionBase;
    // Mount onto another scene entity (render-scene transform.mount), wire units.
    FString MountTo;
    FVector MountOffsetWire=FVector::ZeroVector;
    double MountYaw=0,OwnScale=1;
    // Renderer-relevant component data (render-scene entity.data).
    TSharedPtr<FJsonObject> WaterData;
    FString Lib, Kind;
    TSharedPtr<FJsonObject> EnvironmentData;
    TArray<TWeakObjectPtr<UBoxComponent>> AirBoxes;
};

// Disposable scene projection, never a second persistent fold.
UCLASS()
class EIDOVERSECLIENT_API AEidoverseScene : public AActor
{
    GENERATED_BODY()
public:
    AEidoverseScene();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void Apply(const TSharedPtr<FJsonObject>& Message,const FVector& Origin,const FString& Library);
    void ApplyWeather(const TSharedPtr<FJsonObject>& Atmosphere);
    // Client-only lighting inspection; a negative hour restores the server clock.
    UFUNCTION(BlueprintCallable,Category="Eidoverse|Preview") void SetPreviewHour(float Hour);
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Eidoverse|Preview") float PreviewHour=-1;
    AActor* EntityActor(const FString& Id) const;
    TSharedPtr<FJsonObject> Interaction(const FString& Id) const;
    FString EntityId(AActor* Actor) const;
    void LeaseTransform(const FString& Id,const TSharedPtr<FJsonObject>& State,const FVector& Origin);
    void HoldTransform(const FString& Id,bool bHold);
    void SetLocalTransform(const FString& Id,const FTransform& Transform);
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") int32 FloraCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") FString WeatherState;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") float WeatherHour=12;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") float CloudCover=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") float SunLight=1;
    UPROPERTY() TObjectPtr<UDirectionalLightComponent> Sun;
    UPROPERTY() TObjectPtr<USkyLightComponent> SkyFill;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") int32 EntityCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") int32 LoadedModels=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") int32 FailedModels=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") FString Diagnostic;

    // Aquatic worlds are realized from `water`/`air`/`environment` components,
    // never from the world's name (eidoverse-worlds tools/water/README.md).
    // WaterAt: the volume containing a WIRE position (metres, Y-up), or null in
    // air, including inside dry boxes. Same rule as shared/water.js mediumAt.
    const EidoWater::FParams* WaterAt(const FVector& WirePosition) const;
    // Seconds on the shared wave clock (server epoch wrapped hourly).
    double WaterTime() const;
    // True inside any dry box. UE world position.
    bool DryAt(const FVector& WorldPosition) const;
    // Authored environment spawn in WIRE coordinates, if any.
    bool EnvironmentSpawn(FVector& OutWire) const;
    FVector GetSceneOrigin() const { return SceneOrigin; }
    // Entities as projected: id -> library path (or kind for lights/structures), sorted by id.
    void GetEntities(TArray<TPair<FString, FString>>& Out) const;
    // Projection revision of an entity (changes with its lib/kind/components/data), or empty.
    FString GetEntityRevision(const FString& Id) const;
    // Build mode (EidoverseSceneBuild.cpp). Wire pose of an entity as last projected;
    // bAnimated when motion or a mount drives it (placing would fight the server).
    bool GetEntityPose(const FString& Id, FVector& OutWirePos, double& OutYaw, double& OutScale, FString& OutLib, bool& bAnimated) const;
    // Selection overlay on one entity's meshes (empty Id clears).
    void SetSelected(const FString& Id);
    FString GetSelected() const { return SelectedId; }
    // Placement ghost (web build.js holdGhost): a see-through preview of a library
    // model that follows the aim. Never an entity and never sent; spawn commits it.
    bool HoldGhost(const FString& Lib);
    void ClearGhost();
    bool HasGhost() const { return GhostActor.IsValid(); }
    FString GetGhostLib() const { return GhostLib; }
    AActor* GetGhostActor() const { return GhostActor.Get(); }
    // UE world position; yaw in wire radians about +Y; uniform wire scale.
    void SetGhostPose(const FVector& World, double WireYaw, double Scale);
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse|Water") int32 WaterVolumeCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse|Water") int32 AirBoxCount=0;
private:
    TMap<FString,FEidoverseSceneItem> Items;
    UPROPERTY() TMap<FString,TObjectPtr<UEidoverseAvatarAsset>> Assets;
    UPROPERTY() TMap<FString,TObjectPtr<UMaterialInstanceDynamic>> Materials;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Terrain;
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Flora;
    UPROPERTY() TObjectPtr<UStaticMesh> GroundCoverMesh;
    UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GroundCover;
    UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> WeatherFog;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Rain;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Clouds;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;
    TSet<FString> HeldTransforms;
    TMap<FString,double> RemoteLeases;
    float RainAmount=0,CloudAmount=0;
    double LastRainUpdate=0;
    TSharedPtr<FJsonObject> FloraData;
    TSharedPtr<FJsonObject> LastAtmosphere;
    FVector SceneOrigin=FVector::ZeroVector;
    // Server clock estimate: local seconds -> server epoch ms.
    double ServerClockOffsetMs=0;
    bool bServerClockKnown=false;
    double ServerNowMs() const;
    void ObserveServerTime(double ServerMs);
    bool LocalTransform(const FString& Id,FTransform& Out,int32 Depth);
    bool bFloraDirty=false;
    void BuildFlora(const TSharedPtr<FJsonObject>& Data,const FVector& Origin);
    void TickWeather(float Dt);
    UMaterialInstanceDynamic* Material(const FString& Slot);
    void BuildStructure(AActor* Actor,const TSharedPtr<FJsonObject>& Plan);
    void ApplyEnvironment(const TSharedPtr<FJsonObject>& Env,const FVector& Origin);
    // Water realization (EidoverseSceneWater.cpp).
    TSharedPtr<TArray<EidoWater::FParams>> Waters;
    UPROPERTY() TObjectPtr<AWaterOpticsController> WaterOptics;
    UPROPERTY() TObjectPtr<ASpectralOcean> Ocean;
    UPROPERTY() TObjectPtr<APostProcessVolume> WaterPost;
    bool bTerrainAuthored=true;
    FString SelectedId;
    UPROPERTY() TObjectPtr<UMaterialInterface> SelectionMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> GhostMaterial;
    UPROPERTY() TObjectPtr<UEidoverseAvatarAsset> GhostAsset;
    TWeakObjectPtr<AActor> GhostActor;
    TWeakObjectPtr<UStaticMeshComponent> GhostMesh;
    FString GhostLib, LibraryBase;
    bool bFloraBuiltOnce=false;
    void TickGhost();
    // Spectral sunlight rig for aquatic worlds (sharp/scattered RGB + upwelling),
    // following the scene sun; the sun itself then lights only the atmosphere.
    UPROPERTY() TArray<TObjectPtr<UDirectionalLightComponent>> WaterRig;
    // Daylight x cloud factor from the last weather update (1 = clear noon).
    float SunFactor=1;
    void UpdateWaterLighting(bool bWater);
    void ReadComponentData(AActor* Actor,FEidoverseSceneItem& Item,const TSharedPtr<FJsonObject>& Data);
    void UpdateWater();
    void DestroyWater();
    void ApplyEnvironmentFloor(const TSharedPtr<FJsonObject>& Environment);
    TSharedPtr<FJsonObject> CurrentEnvironment() const;
};

// Grounded exploration controls for server-authored land scenes. The aquatic
// prototype keeps its existing diver/submarine pawn unchanged.
UCLASS()
class EIDOVERSECLIENT_API AEidoverseExplorer : public ACharacter
{
    GENERATED_BODY()
public:
    AEidoverseExplorer();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void Tick(float DeltaSeconds) override;
    // walk | swim | surface: reported in presence locomotion.
    UPROPERTY(Transient,BlueprintReadOnly,Category="Eidoverse") FString LocomotionMode=TEXT("walk");
    // Follow camera (web controller.js updateFollowCamera): the wheel dollies from
    // 16 m behind the body through into first person, with hysteresis.
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") TObjectPtr<class USpringArmComponent> Boom;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") TObjectPtr<class UCameraComponent> Eyes;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Eidoverse") float CameraDistance=4.2f;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Eidoverse") bool bFirstPerson=false;
    // Measured body velocity (cm/s), including shared-law swimming which moves the
    // actor directly and leaves CharacterMovement's velocity at zero.
    UPROPERTY(Transient,BlueprintReadOnly,Category="Eidoverse") FVector BodyVelocity=FVector::ZeroVector;
    UFUNCTION(BlueprintCallable,Category="Eidoverse") void SetCameraDistance(float Meters);
    UFUNCTION(BlueprintCallable,Category="Eidoverse") void Zoom(float Notches);
private:
    FVector LastBodyLocation=FVector::ZeroVector;
    bool bHasLastBodyLocation=false;
    void UpdateCamera(float Dt,class APlayerController* PC);
    bool bPreviousMouseSmoothing=true;
    FVector SwimVelocityWire=FVector::ZeroVector;
    bool bSwimming=false;
    // Returns true while the shared swim law moved the pawn this frame.
    bool TickSwimming(float Dt,class APlayerController* PC);
    void LookYaw(float Value);
    void LookPitch(float Value);
};
