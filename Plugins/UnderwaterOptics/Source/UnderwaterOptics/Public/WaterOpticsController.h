#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaterOpticsController.generated.h"

class UMaterialParameterCollection;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class ULocalLightComponent;
class UBoxComponent;

UCLASS(BlueprintType)
class UNDERWATEROPTICS_API AWaterOpticsController : public AActor
{
    GENERATED_BODY()
public:
    AWaterOpticsController();
    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    UPROPERTY(EditAnywhere, Category="Water") TObjectPtr<UMaterialParameterCollection> Collection;
    UPROPERTY(EditAnywhere, Category="Water") TObjectPtr<UMaterialInterface> LocalLightMaterial;
    UPROPERTY(EditAnywhere, Category="Water") float SurfaceZ = 0;
    UPROPERTY(VisibleAnywhere, Category="Water") int32 AirVolumeCount = 0;
    UPROPERTY(VisibleAnywhere, Category="Water") int32 LocalLightCount = 0;
    UPROPERTY(VisibleAnywhere, Category="Water") bool bRegistryOverflow = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ocean") bool bEnableOceanSurface = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ocean") int32 FloatingPawnCount = 0;
    UFUNCTION(BlueprintPure, Category="Ocean") float GetOceanHeight(FVector WorldPosition) const;

private:
    TArray<TWeakObjectPtr<UBoxComponent>> AirVolumes;
    TArray<TWeakObjectPtr<ULocalLightComponent>> Sources;
    UPROPERTY(Transient) TMap<TObjectPtr<ULocalLightComponent>, TObjectPtr<UMaterialInstanceDynamic>> LightMaterials;
    UPROPERTY(Transient) TMap<TObjectPtr<ULocalLightComponent>, TObjectPtr<UMaterialInterface>> OriginalMaterials;
    float RefreshTime = 0;
    void RefreshRegistry();
    void UpdateOcean(float DeltaSeconds);
    TSet<TWeakObjectPtr<APawn>> FloatingPawns;
};
