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
    // Placed lights (lights.js): day-aware, budgeted to 8 live slots, never shadowed.
    TWeakObjectPtr<class UPointLightComponent> Light;
    float LightIntensity=16, LightRange=10;
    bool bLightKeep=false, bLightDayAware=true, bLightSlotted=false;
    // Lamps inferred from a model's strong emitters: the lowest light tier.
    TArray<TWeakObjectPtr<class UPointLightComponent>> ModelLamps;
    TArray<float> ModelLampIntensity;
    TArray<bool> ModelLampSlotted;
    // Named parts (motion.js): one component per part, animated by its own motion.
    TMap<FString,TWeakObjectPtr<UStaticMeshComponent>> PartComps;
    TSharedPtr<FJsonObject> PartMotions;      // transforms[].parts: {part: motion with t0}
    FString MountPart;                        // mounted on a socket that rides a part
    bool bStructure=false;                    // a griddled building (structure component)
    // picture (pictures.js): an image hung on a named part's material.
    TSharedPtr<FJsonObject> Picture;
    FString PictureApplied;                   // src|lit|flip last applied
    // Presentation data kept for sounds and particles (render-scene entity.data).
    TSharedPtr<FJsonObject> Sound, Particles;
    TSharedPtr<FJsonObject> Traversal, Vehicle;   // climb routes, helm (traversal.js, vehicles.js)
    // The particle emitter built from Particles (EidoverseSceneParticles.cpp).
    TSharedPtr<struct FEidoParticleEmitter> Emitter;
    TSharedPtr<struct FEidoSoundPlayer> SoundPlayer;
    FString EmitterKey;
    // Residency (models.js): the model's asset key and how to recreate it, and whether
    // it is demoted to its placeholder for being far away.
    FString AssetKey, AssetUrl;
    TArray<FString> AssetParts;
    bool bDemoted=false;
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
    const TMap<FString,FEidoverseSceneItem>& GetItems() const { return Items; }
    TSharedPtr<FJsonObject> Interaction(const FString& Id) const;
    FString EntityId(AActor* Actor) const;
    void LeaseTransform(const FString& Id,const TSharedPtr<FJsonObject>& State,const FVector& Origin);
    void HoldTransform(const FString& Id,bool bHold);
    void SetLocalTransform(const FString& Id,const FTransform& Transform);
    // Someone else's live drag: shown at once and held briefly over projection updates.
    void HoldRemote(const FString& Id,const FTransform& Transform,double Seconds);
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
    // environment.bounds {min,max} (wire metres); false when the world authored none.
    bool EnvironmentBounds(FVector& MinWire,FVector& MaxWire) const;
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
public:
    // The folded sky bag (render-scene environment.sky): fog, sun, ambient, fill,
    // exposure, azimuth, clouds tune the light exactly where the browser applies them.
    // geom side-channel ({lib:{bbox:{center,size}}}): placeholders take the model's size.
    void SetGeom(const TSharedPtr<FJsonObject>& Geom);
    UFUNCTION(BlueprintCallable,Category="Eidoverse") FString DescribeLighting() const;
    // The model-space delta a named part has moved this frame (identity at rest).
    bool PartDelta(const FString& Id,const FString& Part,FTransform& OutDelta) const;
    // World transform of an entity's model space, carried by a part when named.
    bool ModelWorldTransform(const FString& Id,const FString& Part,FTransform& Out) const;
    UFUNCTION(BlueprintCallable,Category="Eidoverse") FString DescribePart(const FString& Id,const FString& Part) const;
    TMap<FString,TPair<FVector,FVector>> GeomByLib;   // wire centre, size
    void SetSkyArgs(const TSharedPtr<FJsonObject>& Sky) { SkyArgs=Sky; if(LastAtmosphere) ApplyWeather(LastAtmosphere); }
    TWeakObjectPtr<APostProcessVolume> ScenePost;
    // Weather on surfaces (materials.js): wetness eases toward wet in 10 s, dry in 40 s;
    // cloud cover eases over 15 s. Read by the surface materials' parameter collection.
    float Wetness=0, CoverTarget=0, WetTarget=0;
    bool bWeatherEased=false;
    FVector2D CloudScroll1=FVector2D::ZeroVector, CloudScroll2=FVector2D::ZeroVector;
    float SunElevationSin=1;
    UPROPERTY() TObjectPtr<class UMaterialParameterCollection> WeatherCollection;
private:
    TSharedPtr<FJsonObject> SkyArgs;
    UPROPERTY() TObjectPtr<UDirectionalLightComponent> Moon;
    void ApplyLightingNow();
    double NextLightRank=0;
    FVector LastLightRankEye=FVector::ZeroVector;
    void TickPlacedLights(double Now);
    void TickParts();
    // Promote within R = 80 + 4 x bbox diagonal (m), demote beyond R + 20, at 2 Hz.
    void TickResidency(double Now);
    void DemoteItem(FEidoverseSceneItem& E);
    double NextResidency=0;
public:
    UFUNCTION(BlueprintPure,Category="Eidoverse") int32 GetDemotedCount() const;
    UFUNCTION(BlueprintPure,Category="Eidoverse") FString DescribeResidency(const FString& Id) const;
    UFUNCTION(BlueprintPure,Category="Eidoverse") bool IsItemDemoted(const FString& Id) const { const FEidoverseSceneItem* E=Items.Find(Id); return E&&E->bDemoted; }
    UFUNCTION(BlueprintPure,Category="Eidoverse") bool IsItemModelLoaded(const FString& Id) const;
private:
    void TickPictures();
    void TickParticles(float Dt);
    void TickSounds();
    // Decoded PCM per library path (shared by every entity playing it).
    TMap<FString, TSharedPtr<struct FEidoPcm>> SoundCache;
    TSet<FString> SoundLoading;
    // A library texture fetched once and shared (pictures, particle sprites).
    UTexture2D* LibraryTexture(const FString& Path, bool bFlip, bool& bPending);
    struct FPictureLoad { bool bDone=false; TWeakObjectPtr<UTexture2D> Texture; };
    TMap<FString,FPictureLoad> PictureLoads;      // url+flip -> texture
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> PictureTextures;
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
    // Inputs to the browser's clip choice (locomotion_clip.js): a movement key held,
    // seconds airborne, and whether this airtime began with a jump.
    UPROPERTY(Transient,BlueprintReadOnly,Category="Eidoverse") bool bWantMove=false;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Eidoverse") float AirborneSeconds=0;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Eidoverse") bool bJumped=false;
    // "sit" | "lie" | empty: X sits, Z lies down, moving stands up (controller.js postures).
    UPROPERTY(Transient,BlueprintReadWrite,Category="Eidoverse") FString Posture;
    // Seats: X is handed to the bridge (bSeatHandled) as bSitPressed; while seated the
    // body is placed by the bridge each frame and movement only signals "get up".
    UPROPERTY(Transient,BlueprintReadWrite,Category="Eidoverse") bool bSitPressed=false;
    bool bSeatHandled=false;
    UPROPERTY(Transient,BlueprintReadWrite,Category="Eidoverse") bool bFoldPressed=false;
    // First person while seated, lying or posed: the eye rides the head bone (fp_view.js).
    bool bEyeOverride=false; FVector EyeOverride=FVector::ZeroVector;
    // Mantle (controller.js): Space at a 0.3-1.7 m ledge lifts you onto it over 0.55 s.
    UPROPERTY(Transient,BlueprintReadOnly,Category="Eidoverse") bool bMantling=false;
    bool bArrowsForBuild=false;
    // Photo mode (controller.js): a free, damped camera; the body stays where it is.
    UPROPERTY(Transient,BlueprintReadOnly,Category="Eidoverse") bool bPhoto=false;
    FVector PhotoPos=FVector::ZeroVector, PhotoVel=FVector::ZeroVector; FRotator PhotoView=FRotator::ZeroRotator; float PhotoFov=55, PhotoFovWant=55;
    UFUNCTION(BlueprintCallable,Category="Eidoverse") void SetPhotoMode(bool bOn);
    void TickPhoto(float Dt,class APlayerController* PC);
    FVector MantleFrom=FVector::ZeroVector, MantleTo=FVector::ZeroVector; float MantleT=0;
    UFUNCTION(BlueprintCallable,Category="Eidoverse") bool TryMantle();
    UPROPERTY(Transient,BlueprintReadOnly,Category="Eidoverse") bool bSeated=false;
    void SetSeated(bool bOn);
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
