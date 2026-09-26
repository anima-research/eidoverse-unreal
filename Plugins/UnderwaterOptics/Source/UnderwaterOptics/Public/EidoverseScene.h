#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "EidoverseScene.generated.h"
class FJsonObject;
class UEidoverseAvatarAsset;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UStaticMesh;
class UDirectionalLightComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;

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
};

// Disposable scene projection, never a second persistent fold.
UCLASS()
class UNDERWATEROPTICS_API AEidoverseScene : public AActor
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
    UPROPERTY() TObjectPtr<UDirectionalLightComponent> Sun;
    UPROPERTY() TObjectPtr<USkyLightComponent> SkyFill;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") int32 EntityCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") int32 LoadedModels=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") int32 FailedModels=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse") FString Diagnostic;
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
    bool bFloraDirty=false;
    void BuildFlora(const TSharedPtr<FJsonObject>& Data,const FVector& Origin);
    void TickWeather(float Dt);
    UMaterialInstanceDynamic* Material(const FString& Slot);
    void BuildStructure(AActor* Actor,const TSharedPtr<FJsonObject>& Plan);
    void ApplyEnvironment(const TSharedPtr<FJsonObject>& Env,const FVector& Origin);
};

// Grounded exploration controls for server-authored land scenes. The aquatic
// prototype keeps its existing diver/submarine pawn unchanged.
UCLASS()
class UNDERWATEROPTICS_API AEidoverseExplorer : public ACharacter
{
    GENERATED_BODY()
public:
    AEidoverseExplorer();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void Tick(float DeltaSeconds) override;
private:
    bool bPreviousMouseSmoothing=true;
    void LookYaw(float Value);
    void LookPitch(float Value);
};
