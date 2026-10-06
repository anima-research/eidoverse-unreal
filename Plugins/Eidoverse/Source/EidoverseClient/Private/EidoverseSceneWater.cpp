// Water realization for AEidoverseScene: `water`, `air` and `environment`
// component data (render-scene entity.data) -> the UnderwaterOptics renderer.
//
// A world is aquatic because its entities carry these components. The
// spectral model is the reference medium; authored absorption/scatter scale it
// relative to the reference below, and authored waves are the shared surface
// for rendering and physics, with the FFT field as low-amplitude detail.
#include "EidoverseScene.h"
#include "EidoverseBridge.h"
#include "EidoverseWater.h"
#include "WaterOpticsController.h"
#include "SpectralOcean.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "ProceduralMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/EngineTypes.h"
#include "HAL/IConsoleManager.h"

namespace
{
    // The medium the Unreal spectral model was tuned to match: the water world's
    // authored values (eidoverse-worlds tools/water/prepare.ts).
    const FVector3d ReferenceAbsorption(.10, .036, .018);
    const FVector3d ReferenceScatter(.014, .11, .17);
    // FFT amplitude when authored waves carry the swell: detail, not a second sea.
    constexpr float DetailWaveScale = .35f, OpenSeaWaveScale = 1.f;

    const TCHAR* MPCPath = TEXT("/Game/Underwater/Materials/MPC_Water.MPC_Water");
    const TCHAR* LocalLightPath = TEXT("/Game/Underwater/SpectralPrototype/M_SpectralLocalLight.M_SpectralLocalLight");
    const TCHAR* SurfacePath = TEXT("/Game/Underwater/SpectralPrototype/M_SpectralSurface.M_SpectralSurface");
    const TCHAR* UndersidePath = TEXT("/Game/Underwater/SpectralPrototype/M_SpectralUnderside.M_SpectralUnderside");
    const TCHAR* OpticsPath = TEXT("/Game/Underwater/SpectralPrototype/M_SpectralOptics.M_SpectralOptics");
    // The surface and underside share one camera-following grid (as in the prototype level).
    const TCHAR* GridPath = TEXT("/Game/Underwater/SpectralPrototype/SM_SpectralOceanGrid.SM_SpectralOceanGrid");

    TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
    {
        const TSharedPtr<FJsonObject>* V = nullptr;
        return O && O->TryGetObjectField(Key, V) ? *V : nullptr;
    }

    bool Vector3(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FVector& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!O || !O->TryGetArrayField(Key, A) || A->Num() != 3) return false;
        for (int32 I = 0; I < 3; ++I)
        {
            if ((*A)[I]->Type != EJson::Number || !FMath::IsFinite((*A)[I]->AsNumber())) return false;
            Out[I] = (*A)[I]->AsNumber();
        }
        return true;
    }

    double SafeRatio(double Value, double Reference) { return FMath::Clamp(Value / Reference, 0.0, 50.0); }
}

void AEidoverseScene::ReadComponentData(AActor* Actor, FEidoverseSceneItem& Item, const TSharedPtr<FJsonObject>& Data)
{
    Item.WaterData = ObjectField(Data, TEXT("water"));
    Item.EnvironmentData = ObjectField(Data, TEXT("environment"));
    const TArray<TSharedPtr<FJsonValue>>* Boxes = nullptr;
    const auto Air = ObjectField(Data, TEXT("air"));
    if (!Actor || !Air || !Air->TryGetArrayField(TEXT("boxes"), Boxes)) return;
    for (const auto& V : *Boxes)
    {
        const TSharedPtr<FJsonObject> Box = V->Type == EJson::Object ? V->AsObject() : nullptr;
        if (!Box || Item.AirBoxes.Num() >= EidoWater::MaxAir) continue;
        FVector Center(0), Size(1);
        Vector3(Box, TEXT("center"), Center);
        Vector3(Box, TEXT("size"), Size);
        FQuat Q = FQuat::Identity;
        const TArray<TSharedPtr<FJsonValue>>* QA = nullptr;
        if (Box->TryGetArrayField(TEXT("q"), QA) && QA->Num() == 4)
        {
            const FQuat Wire((*QA)[0]->AsNumber(), (*QA)[1]->AsNumber(), (*QA)[2]->AsNumber(), (*QA)[3]->AsNumber());
            if (!Wire.ContainsNaN() && Wire.SizeSquared() > 1e-6) Q = AEidoverseBridge::FromWireRotation(Wire.GetNormalized());
        }
        // Wire (x,y,z) -> UE (z,-x,y): extents permute, centimetres, never negative.
        const FVector Extent(FMath::Max(.001, FMath::Abs(Size.Z)) * 50, FMath::Max(.001, FMath::Abs(Size.X)) * 50, FMath::Max(.001, FMath::Abs(Size.Y)) * 50);
        auto* C = NewObject<UBoxComponent>(Actor);
        Actor->AddInstanceComponent(C);
        C->SetupAttachment(Actor->GetRootComponent());
        C->SetRelativeLocationAndRotation(AEidoverseBridge::FromWirePosition(Center), Q);
        C->SetBoxExtent(Extent, false);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetHiddenInGame(true);
        // AWaterOpticsController discovers tagged boxes: the optics and local
        // lights then treat them as dry, following the live entity transform.
        C->ComponentTags.Add(TEXT("WaterAirVolume"));
        C->RegisterComponent();
        Item.AirBoxes.Add(C);
    }
}

TSharedPtr<FJsonObject> AEidoverseScene::CurrentEnvironment() const
{
    TArray<FString> Ids;
    Items.GetKeys(Ids);
    Ids.Sort();
    for (const FString& Id : Ids)
        if (const auto& Env = Items[Id].EnvironmentData) return Env;
    return nullptr;
}

bool AEidoverseScene::EnvironmentSpawn(FVector& OutWire) const
{
    return Vector3(CurrentEnvironment(), TEXT("spawn"), OutWire);
}

bool AEidoverseScene::EnvironmentBounds(FVector& MinWire, FVector& MaxWire) const
{
    const TSharedPtr<FJsonObject> Env = CurrentEnvironment();
    const TSharedPtr<FJsonObject>* B = nullptr;
    return Env && Env->TryGetObjectField(TEXT("bounds"), B) && Vector3(*B, TEXT("min"), MinWire) && Vector3(*B, TEXT("max"), MaxWire);
}

double AEidoverseScene::WaterTime() const
{
    return EidoWater::WaveTime(ServerNowMs());
}

bool AEidoverseScene::DryAt(const FVector& WorldPosition) const
{
    for (const auto& Pair : Items)
        for (const auto& Weak : Pair.Value.AirBoxes)
            if (const UBoxComponent* Box = Weak.Get())
            {
                const FVector Q = Box->GetComponentTransform().InverseTransformPosition(WorldPosition).GetAbs();
                const FVector E = Box->GetUnscaledBoxExtent();
                if (Q.X <= E.X && Q.Y <= E.Y && Q.Z <= E.Z) return true;
            }
    return false;
}

const EidoWater::FParams* AEidoverseScene::WaterAt(const FVector& Wire) const
{
    if (!Waters || Waters->IsEmpty()) return nullptr;
    if (DryAt(SceneOrigin + AEidoverseBridge::FromWirePosition(Wire))) return nullptr;
    const double Time = WaterTime();
    // Later volumes (sorted id order) take priority where they overlap.
    for (int32 I = Waters->Num() - 1; I >= 0; --I)
        if (EidoWater::InVolume((*Waters)[I], {Wire.X, Wire.Y, Wire.Z}, Time)) return &(*Waters)[I];
    return nullptr;
}

void AEidoverseScene::ApplyEnvironmentFloor(const TSharedPtr<FJsonObject>& Environment)
{
    if (!Terrain) return;
    // Browser rule (client/lib/terrain.js): an authored environment replaces the
    // default ground plane; a finite `floor` becomes the ground height.
    if (Environment && !bTerrainAuthored)
    {
        double Floor = 0;
        const bool bFloor = Environment->TryGetNumberField(TEXT("floor"), Floor) && FMath::IsFinite(Floor);
        Terrain->SetVisibility(false);
        Terrain->SetWorldLocation(SceneOrigin + FVector(0, 0, bFloor ? Floor * 100 : 0));
        Terrain->SetCollisionEnabled(bFloor ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    }
    else
    {
        Terrain->SetVisibility(true);
        Terrain->SetWorldLocation(SceneOrigin);
        Terrain->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
}

namespace
{
    struct FRigLight { const TCHAR* Function; FColor Color; bool bShadows; float SourceAngle; bool bUpwelling; };
    // Mirrors the prototype level's rig (L_SpectralOcean_Prototype), intensity 10 each.
    const FRigLight Rig[] = {
        {TEXT("MI_LF_Sharp_R"), FColor::Red, true, 6, false},
        {TEXT("MI_LF_Sharp_G"), FColor::Green, true, 6, false},
        {TEXT("MI_LF_Sharp_B"), FColor::Blue, true, 6, false},
        {TEXT("MI_LF_Scat_R"), FColor::Red, false, 25, false},
        {TEXT("MI_LF_Scat_G"), FColor::Green, false, 25, false},
        {TEXT("MI_LF_Scat_B"), FColor::Blue, false, 25, false},
        {TEXT("MI_LF_Up"), FColor::White, false, 30, true},
    };
    constexpr float RigIntensity = 10.f, LandSunIntensity = 3.2f;
}

static TAutoConsoleVariable<int32> CVarSpectralSunRig(TEXT("Eidoverse.Water.SpectralSunRig"), 0,
    TEXT("1: light aquatic worlds with the prototype's spectral sun rig (experimental; surface colour not yet correct)."));

void AEidoverseScene::UpdateWaterLighting(bool bWater)
{
    // Fog is always the optics' job in water, rig or not.
    if (WeatherFog) WeatherFog->SetVisibility(!bWater);
    if (!bWater || CVarSpectralSunRig.GetValueOnGameThread() == 0)
    {
        for (auto& L : WaterRig) if (L) L->DestroyComponent();
        if (!WaterRig.IsEmpty() && Sun)
        {
            // Hand lighting back to the land sun (ApplyWeather's own value).
            Sun->SetLightingChannels(true, false, false);
            Sun->SetIntensity(LandSunIntensity * SunFactor);
        }
        WaterRig.Reset();
        return;
    }
    if (WaterRig.IsEmpty())
        for (const FRigLight& R : Rig)
        {
            auto* L = NewObject<UDirectionalLightComponent>(this);
            AddInstanceComponent(L);
            L->SetupAttachment(RootComponent);
            L->SetMobility(EComponentMobility::Movable);
            L->SetLightColor(FLinearColor(R.Color));
            L->SetCastShadows(R.bShadows);
            L->LightSourceAngle = R.SourceAngle;
            L->SetVolumetricScatteringIntensity(0);
            L->LightFunctionScale = FVector(1024);
            L->LightFunctionFadeDistance = 10000000.f;
            L->DisabledBrightness = 0;
            L->bUseTemperature = false;
            L->SetLightFunctionMaterial(LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Underwater/Materials/%s.%s"), R.Function, R.Function)));
            L->RegisterComponent();
            WaterRig.Add(L);
        }
    const FRotator SunRotation = Sun ? Sun->GetComponentRotation() : FRotator(-58, 35, 0);
    for (int32 I = 0; I < WaterRig.Num(); ++I)
        if (auto* L = WaterRig[I].Get())
        {
            L->SetWorldRotation(Rig[I].bUpwelling ? FRotator(90, 0, 0) : SunRotation);
            L->SetIntensity(RigIntensity * SunFactor);
        }
    if (Sun)
    {
        // As the prototype's Sun_AtmosphereOnly: sky and sun disc, no surface lighting.
        Sun->SetLightingChannels(false, false, false);
        Sun->SetIntensity(RigIntensity * SunFactor);
    }
}

void AEidoverseScene::DestroyWater()
{
    UpdateWaterLighting(false);
    for (AActor* A : TArray<AActor*>{Ocean, WaterOptics, WaterPost})
        if (IsValid(A)) A->Destroy();
    Ocean = nullptr;
    WaterOptics = nullptr;
    WaterPost = nullptr;
    if (Waters) Waters->Reset();
    WaterVolumeCount = 0;
}

void AEidoverseScene::UpdateWater()
{
    if (!Waters) Waters = MakeShared<TArray<EidoWater::FParams>>();
    Waters->Reset();
    AirBoxCount = 0;
    TArray<FString> Ids;
    Items.GetKeys(Ids);
    Ids.Sort();
    for (const FString& Id : Ids)
    {
        const auto& Item = Items[Id];
        AirBoxCount += Item.AirBoxes.Num();
        const AActor* A = Item.Actor.Get();
        if (!Item.WaterData || !A) continue;
        if (Waters->Num() >= EidoWater::MaxWater)
        {
            Diagnostic += TEXT(" · extra water volumes inactive");
            break;
        }
        // Place like client/lib/water.js: centre through the live entity
        // transform; bounds stay horizontal and axis-aligned, scaled.
        EidoWater::FParams W = EidoWater::ReadParams(Item.WaterData);
        const FVector World = A->GetActorTransform().TransformPosition(AEidoverseBridge::FromWirePosition(FVector(W.Center.X, W.Center.Y, W.Center.Z)));
        const FVector Wire = AEidoverseBridge::ToWirePosition(World - SceneOrigin);
        const double Scale = FMath::Abs(A->GetActorScale3D().X);
        W.Center = {Wire.X, Wire.Y, Wire.Z};
        W.Size = {W.Size.X * Scale, W.Size.Y * Scale, W.Size.Z * Scale};
        W.UpdateBounds();
        Waters->Add(W);
    }
    WaterVolumeCount = Waters->Num();
    ApplyEnvironmentFloor(CurrentEnvironment());
    if (Waters->IsEmpty())
    {
        DestroyWater();
        return;
    }
    UpdateWaterLighting(true);

    UWorld* World = GetWorld();
    auto* Collection = LoadObject<UMaterialParameterCollection>(nullptr, MPCPath);
    if (!World || !Collection) return;
    if (!IsValid(WaterPost))
    {
        // Spawned before the ocean: ASpectralOcean::BeginPlay installs its optics
        // blendable on post-process volumes carrying this tag.
        WaterPost = World->SpawnActor<APostProcessVolume>();
        WaterPost->SetFlags(RF_Transient);
        WaterPost->Tags.Add(TEXT("WaterOpticsPostProcess"));
        WaterPost->bUnbound = true;
        WaterPost->Priority = 101;  // above the land scene's fixed-exposure volume
        auto& S = WaterPost->Settings;
        S.bOverride_AutoExposureMethod = true;
        S.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
        S.bOverride_AutoExposureMinBrightness = S.bOverride_AutoExposureMaxBrightness = true;
        S.bOverride_AutoExposureSpeedUp = S.bOverride_AutoExposureSpeedDown = true;
    }
    if (!IsValid(Ocean))
    {
        Ocean = World->SpawnActorDeferred<ASpectralOcean>(ASpectralOcean::StaticClass(), FTransform::Identity);
        Ocean->SetFlags(RF_Transient);
        Ocean->Collection = Collection;
        Ocean->SurfaceMaterial = LoadObject<UMaterialInterface>(nullptr, SurfacePath);
        Ocean->UnderMaterial = LoadObject<UMaterialInterface>(nullptr, UndersidePath);
        Ocean->OpticsMaterial = LoadObject<UMaterialInterface>(nullptr, OpticsPath);
        if (auto* Grid = LoadObject<UStaticMesh>(nullptr, GridPath))
        {
            Ocean->SurfaceMesh->SetStaticMesh(Grid);
            Ocean->UnderMesh->SetStaticMesh(Grid);
        }
        Ocean->FinishSpawning(FTransform::Identity);
    }
    if (!IsValid(WaterOptics))
    {
        WaterOptics = World->SpawnActorDeferred<AWaterOpticsController>(AWaterOpticsController::StaticClass(), FTransform::Identity);
        WaterOptics->SetFlags(RF_Transient);
        WaterOptics->Collection = Collection;
        WaterOptics->LocalLightMaterial = LoadObject<UMaterialInterface>(nullptr, LocalLightPath);
        WaterOptics->bEnableOceanSurface = true;
        WaterOptics->SpectralOcean = Ocean;
        WaterOptics->FinishSpawning(FTransform::Identity);
    }

    // One ocean surface and one optical medium: the highest-priority volume.
    // Finite-volume clipping of the rendered surface is not implemented yet.
    const EidoWater::FParams& W = Waters->Last();
    const double SurfaceZ = SceneOrigin.Z + W.Top * 100;
    Ocean->SurfaceZ = SurfaceZ;
    WaterOptics->SurfaceZ = SurfaceZ;
    Ocean->WaveScale = W.Waves.IsEmpty() ? OpenSeaWaveScale : DetailWaveScale;

    // Shared wave law -> UE cm. Wire x = -(UE.Y-O.Y)/100, z = (UE.X-O.X)/100, so
    // k(dx*x + dz*z) = k_cm * dot(UE.xy - O.xy, (dz, -dx)). The origin and time
    // terms fold into each wave's phase; ASpectralOcean reduces it mod 2pi.
    const double Time = WaterTime();
    TArray<FVector4> Waves;
    TArray<double> Phases;
    for (const auto& Wave : W.Waves)
    {
        const double K = 2 * UE_DOUBLE_PI / Wave.Wavelength;  // rad/m
        const double KCm = K / 100;
        const FVector2D Dir(Wave.DirZ, -Wave.DirX);
        Waves.Add(FVector4(Wave.Amplitude * 100, KCm, Dir.X, Dir.Y));
        Phases.Add(Wave.Phase - FMath::Sqrt(9.81 * K) * Time - KCm * FVector2D::DotProduct(FVector2D(SceneOrigin.X, SceneOrigin.Y), Dir));
    }
    Ocean->SetAuthoredWaves(Waves, Phases);

    if (auto* MPC = World->GetParameterCollectionInstance(Collection))
    {
        MPC->SetVectorParameterValue(TEXT("WaterExtinctionScale"), FLinearColor(
            SafeRatio(W.Absorption.X, ReferenceAbsorption.X), SafeRatio(W.Absorption.Y, ReferenceAbsorption.Y), SafeRatio(W.Absorption.Z, ReferenceAbsorption.Z), 0));
        MPC->SetVectorParameterValue(TEXT("WaterScatterTint"), FLinearColor(
            SafeRatio(W.Scatter.X, ReferenceScatter.X), SafeRatio(W.Scatter.Y, ReferenceScatter.Y), SafeRatio(W.Scatter.Z, ReferenceScatter.Z), 0));
    }
}

void AEidoverseScene::GetEntities(TArray<TPair<FString, FString>>& Out) const
{
    Out.Reset();
    for (const auto& Pair : Items)
        Out.Add({Pair.Key, !Pair.Value.Lib.IsEmpty() ? Pair.Value.Lib : Pair.Value.Kind});
    Out.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B) { return A.Key < B.Key; });
}
