#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpectralOcean.generated.h"

class UTexture2D;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class USceneCaptureComponentCube;
class UTextureRenderTargetCube;

// CPU spectral reference implementation. The same FFT displacement grids are
// uploaded to the renderer and sampled by physics; no asynchronous readback lag.
struct FSpectralBand
{
    int32 N=128;
    float Length=12800, Gain=1;
    TArray<FVector2f> Initial, Height, X, Y;
    TArray<FVector4f> Pixels;
    TArray<float> Foam;
};

UCLASS(BlueprintType)
class UNDERWATEROPTICS_API ASpectralOcean : public AActor
{
    GENERATED_BODY()
public:
    ASpectralOcean();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ocean") TObjectPtr<UStaticMeshComponent> SurfaceMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ocean") TObjectPtr<UStaticMeshComponent> UnderMesh;
    UPROPERTY(VisibleAnywhere, Category="Ocean") TObjectPtr<USceneCaptureComponentCube> UnderCapture;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ocean") TObjectPtr<UMaterialInterface> SurfaceMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ocean") TObjectPtr<UMaterialInterface> UnderMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ocean") TObjectPtr<UMaterialInterface> OpticsMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ocean") TObjectPtr<UMaterialParameterCollection> Collection;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ocean") float SurfaceZ=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ocean") float WaveScale=1;
    // Valid for this demo's forward-lit glass and unlit particles/water.
    // Disable if adding translucency which consumes the lighting-volume grid.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ocean|Performance") bool bSkipUnusedTranslucencyVolume=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ocean|Performance",meta=(ClampMin="0")) int32 ShadowPageBudget=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ocean") float SimulationMilliseconds=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ocean") float CameraDepth=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ocean") int32 UpdateCount=0;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category="Ocean") TArray<TObjectPtr<UTexture2D>> Fields;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category="Ocean") TObjectPtr<UTexture2D> WakeField;
    UFUNCTION(BlueprintPure, Category="Ocean") float GetSurfaceHeight(FVector Position) const;
    UFUNCTION(BlueprintPure, Category="Ocean") FVector GetDisplacement(FVector Position) const;
    UFUNCTION(BlueprintCallable, Category="Ocean") void InitializeOcean();
    // Authored analytic waves (eidoverse `water` component) added to the FFT surface,
    // on the CPU and in SpectralField.ush. Waves: (amplitude cm, wavenumber rad/cm,
    // dirX, dirY) in UE world; Phases: radians including time/origin, any range.
    // At most 8. An empty array removes them.
    UFUNCTION(BlueprintCallable, Category="Ocean") void SetAuthoredWaves(const TArray<FVector4>& Waves, const TArray<double>& Phases);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ocean") int32 AuthoredWaveCount=0;
    UFUNCTION(BlueprintPure, Category="Ocean") float GetAuthoredHeight(FVector Position) const;
    // Synchronous scene-only capture. Restore every camera-dependent render setting
    // before returning; never tick physics or alter the local player camera.
    void WithCaptureView(const FVector& Camera,TFunctionRef<void()> Capture);
private:
    TArray<FSpectralBand> Bands;
    TArray<FVector4> AuthoredWaves;
    TArray<double> AuthoredPhases;
    void WriteAuthoredWaves();
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SurfaceMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> UnderMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> OpticsMID;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTargetCube> UnderReflection;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTargetCube> RemoteReflection;
    float CaptureCountdown=0;
    float LightCacheCountdown=0;
    TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> LightMaterials;
    TArray<float> WakeHeight, WakeVelocity, WakeFoam;
    FVector2D WakeCenter=FVector2D::ZeroVector;
    FVector LastSubPosition=FVector::ZeroVector;
    bool bHadSub=false;
    double LastTime=-1;
    FName PerformanceOverrideTag;
    int32 PreviousTranslucencyVolume=1,PreviousShadowPages=0;
    void Evaluate(float Time,float Dt);
    void UpdateWake(float Dt);
    void Bind(UMaterialInstanceDynamic* Material);
    static void FFT2(TArray<FVector2f>& Data,int32 N);
    static UTexture2D* MakeField(int32 N);
    static void Upload(UTexture2D* Texture,const TArray<FVector4f>& Data,int32 N);
    static FVector4f Sample(const TArray<FVector4f>& Pixels,int32 N,double X,double Y);
    friend class FSpectralSamplingTest;
};
