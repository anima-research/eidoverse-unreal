#include "WaterOpticsController.h"
#include "Components/BoxComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

AWaterOpticsController::AWaterOpticsController()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    #if WITH_EDITORONLY_DATA
    bIsSpatiallyLoaded = false;
    #endif
}

void AWaterOpticsController::RefreshRegistry()
{
    AirVolumes.Reset();
    Sources.Reset();
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        if (*It == this) continue;
        TArray<UBoxComponent*> Boxes;
        It->GetComponents(Boxes);
        for (UBoxComponent* Box : Boxes)
        {
            if (Box->GetFName() == TEXT("AirVolume") || Box->ComponentHasTag(TEXT("WaterAirVolume")))
                AirVolumes.Add(Box);
        }
        TArray<ULocalLightComponent*> Lights;
        It->GetComponents(Lights);
        for (ULocalLightComponent* Light : Lights)
        {
            if (!Light->ComponentHasTag(TEXT("SkipWaterOptics"))) Sources.Add(Light);
        }
    }
    AirVolumes.Sort([](const TWeakObjectPtr<UBoxComponent>& A, const TWeakObjectPtr<UBoxComponent>& B)
    { return A->GetPathName() < B->GetPathName(); });
    AirVolumeCount = AirVolumes.Num();
    LocalLightCount = Sources.Num();
    const bool bOverflow = AirVolumes.Num() > 16;
    if (bOverflow && !bRegistryOverflow)
        UE_LOG(LogTemp, Error, TEXT("Water optics supports 16 active air boxes; found %d. Increase the shader and collection capacity before adding more."), AirVolumes.Num());
    bRegistryOverflow = bOverflow;
    for (auto It = LightMaterials.CreateIterator(); It; ++It)
        if (!IsValid(It.Key())) It.RemoveCurrent();
    for (auto It = OriginalMaterials.CreateIterator(); It; ++It)
        if (!IsValid(It.Key())) It.RemoveCurrent();
}

void AWaterOpticsController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Collection || !GetWorld()) return;
    RefreshTime -= DeltaSeconds;
    if (RefreshTime <= 0) { RefreshRegistry(); RefreshTime = 0.25f; }
    auto* MPC = GetWorld()->GetParameterCollectionInstance(Collection);
    if (!MPC) return;
    MPC->SetScalarParameterValue(TEXT("OpticsSurfaceZ"), SurfaceZ);
    MPC->SetScalarParameterValue(TEXT("OceanTime"), GetWorld()->GetTimeSeconds());
    MPC->SetScalarParameterValue(TEXT("OceanEnabled"), bEnableOceanSurface ? 1.f : 0.f);
    MPC->SetScalarParameterValue(TEXT("OpticsAirCount"), FMath::Min(AirVolumes.Num(), 16));
    // Rewrite every slot, including unused ones: destroyed actors cannot leave ghost air volumes.
    for (int32 I = 0; I < 16; ++I)
    {
        FVector P = FVector::ZeroVector, E = FVector::ZeroVector;
        FVector X = FVector::ForwardVector, Y = FVector::RightVector;
        bool bActive = AirVolumes.IsValidIndex(I) && AirVolumes[I].IsValid();
        if (bActive)
        {
            UBoxComponent* Box = AirVolumes[I].Get();
            P = Box->GetComponentLocation(); E = Box->GetScaledBoxExtent();
            X = Box->GetForwardVector(); Y = Box->GetRightVector();
        }
        MPC->SetVectorParameterValue(FName(*FString::Printf(TEXT("OpticsAir%dC"), I)), FLinearColor(P.X,P.Y,P.Z,bActive?1:0));
        MPC->SetVectorParameterValue(FName(*FString::Printf(TEXT("OpticsAir%dE"), I)), FLinearColor(E.X,E.Y,E.Z,0));
        MPC->SetVectorParameterValue(FName(*FString::Printf(TEXT("OpticsAir%dX"), I)), FLinearColor(X.X,X.Y,X.Z,0));
        MPC->SetVectorParameterValue(FName(*FString::Printf(TEXT("OpticsAir%dY"), I)), FLinearColor(Y.X,Y.Y,Y.Z,0));
    }

    // Legacy pawn graphs can still write density; disabling rendering keeps them from changing the medium.
    for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
        It->GetComponent()->SetVisibility(false);

    if (!GetWorld()->IsGameWorld()) return;
    if (bEnableOceanSurface) UpdateOcean(DeltaSeconds);
    // Use one exposure implementation. Histogram metering responds to actual interior lighting.
    FLinearColor WP(0,1.5,0.7,0.35);
    MPC->GetVectorParameterValue(TEXT("WaterParams"), WP);
    for (TActorIterator<APostProcessVolume> It(GetWorld()); It; ++It)
    {
        if (!It->ActorHasTag(TEXT("WaterOpticsPostProcess"))) continue;
        FPostProcessSettings& S = It->Settings;
        const float Adapt = FMath::Clamp(WP.B,0.f,1.f);
        S.AutoExposureMinBrightness = FMath::Lerp(3.f,-6.f,Adapt);
        S.AutoExposureMaxBrightness = FMath::Lerp(3.f,8.f,Adapt);
        S.AutoExposureSpeedUp = FMath::Lerp(0.5f,4.f,FMath::Clamp(WP.A,0.f,1.f));
        S.AutoExposureSpeedDown = S.AutoExposureSpeedUp * 0.5f;
    }
    // Camera membership is the union of real boxes, not an enter/exit boolean from any one actor.
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        if (PC->PlayerCameraManager)
        {
            const FVector Camera = PC->PlayerCameraManager->GetCameraLocation();
            bool bInside = false;
            for (auto BoxPtr : AirVolumes)
                if (UBoxComponent* Box = BoxPtr.Get())
                {
                    const FVector Q = Box->GetComponentTransform().InverseTransformPosition(Camera).GetAbs();
                    const FVector E = Box->GetUnscaledBoxExtent();
                    bInside |= Q.X < E.X && Q.Y < E.Y && Q.Z < E.Z;
                }
            for (TActorIterator<APawn> It(GetWorld()); It; ++It)
                if (FBoolProperty* Prop = FindFProperty<FBoolProperty>(It->GetClass(), TEXT("InsideHabitat")))
                    Prop->SetPropertyValue_InContainer(*It, bInside);
        }
    }
    // Attenuate each source's own lighting, preserving the engine's shadowing and distance falloff.
    if (LocalLightMaterial)
        for (auto LightPtr : Sources)
            if (ULocalLightComponent* Light = LightPtr.Get())
            {
                UMaterialInstanceDynamic* MID = LightMaterials.FindRef(Light).Get();
                if (!MID)
                {
                    OriginalMaterials.Add(Light, Light->LightFunctionMaterial);
                    MID = UMaterialInstanceDynamic::Create(LocalLightMaterial, this);
                    LightMaterials.Add(Light, MID);
                    Light->SetLightFunctionMaterial(MID);
                    Light->SetLightFunctionFadeDistance(10000000.f);
                    Light->DisabledBrightness = 0.f;
                }
                const FVector P = Light->GetComponentLocation();
                MID->SetVectorParameterValue(TEXT("SourcePosition"), FLinearColor(P.X,P.Y,P.Z,1));
            }
}

float AWaterOpticsController::GetOceanHeight(FVector WorldPosition) const
{
    if (!bEnableOceanSurface || !GetWorld()) return SurfaceZ;
    const double Time = GetWorld()->GetTimeSeconds();
    double Height = SurfaceZ;
    #define OCEAN_WAVE(A,L,DX,DY,P) { const double K=2.0*PI/L; Height += A*FMath::Sin(K*(WorldPosition.X*DX+WorldPosition.Y*DY)-FMath::Sqrt(980.0*K)*Time+P); }
    #include "../../../Shaders/OceanWaveData.ush"
    #undef OCEAN_WAVE
    return Height;
}

void AWaterOpticsController::UpdateOcean(float DeltaSeconds)
{
    auto* MPC = GetWorld()->GetParameterCollectionInstance(Collection);
    MPC->SetVectorParameterValue(TEXT("OceanSubCenter"), FLinearColor(0,0,0,0));
    for (TActorIterator<APawn> It(GetWorld()); It; ++It)
    {
        APawn* Pawn = *It;
        const bool bSub = Pawn->ActorHasTag(TEXT("OceanSubmarine"));
        const bool bDiver = Pawn->ActorHasTag(TEXT("OceanDiver"));
        if (!bSub && !bDiver) continue;
        FVector Position = Pawn->GetActorLocation();
        bool bDryRoom = false;
        if (bDiver)
            for (auto BoxPtr : AirVolumes)
                if (UBoxComponent* Box = BoxPtr.Get())
                {
                    const FVector Q=Box->GetComponentTransform().InverseTransformPosition(Position).GetAbs();
                    const FVector E=Box->GetUnscaledBoxExtent();
                    if (Q.X<E.X && Q.Y<E.Y && Q.Z<E.Z) { bDryRoom=true; break; }
                }
        if (bDiver && (bDryRoom || Pawn->GetAttachParentActor()))
        { FloatingPawns.Remove(Pawn); continue; }
        if (UPawnMovementComponent* Movement=Pawn->GetMovementComponent())
        {
            // Floating state is latched at the waterline, never an upward force at depth.
            // The same vertical input used for swimming releases it immediately to dive.
            const bool bDive=Pawn->GetLastMovementInputVector().Z < -0.05;
            const float Target=GetOceanHeight(Position)+35.f;
            if (bDive) FloatingPawns.Remove(Pawn);
            else if (Position.Z>=Target-20.f) FloatingPawns.Add(Pawn);
            if (FloatingPawns.Contains(Pawn))
            {
                const float Alpha=1.f-FMath::Exp(-8.f*DeltaSeconds);
                Position.Z=FMath::Min(FMath::Lerp(Position.Z,double(Target),double(Alpha)),double(Target+12.f));
                Pawn->SetActorLocation(Position,true);
                Movement->Velocity.Z=0.f;
            }
        }
        if (bSub)
        {
            Position=Pawn->GetActorLocation();
            const FVector X=Pawn->GetActorForwardVector(),Y=Pawn->GetActorRightVector();
            MPC->SetVectorParameterValue(TEXT("OceanSubCenter"),FLinearColor(Position.X,Position.Y,Position.Z,1));
            MPC->SetVectorParameterValue(TEXT("OceanSubX"),FLinearColor(X.X,X.Y,X.Z,0));
            MPC->SetVectorParameterValue(TEXT("OceanSubY"),FLinearColor(Y.X,Y.Y,Y.Z,0));
        }
    }
    for (auto It=FloatingPawns.CreateIterator();It;++It) if (!It->IsValid()) It.RemoveCurrent();
    FloatingPawnCount=FloatingPawns.Num();
}

void AWaterOpticsController::EndPlay(const EEndPlayReason::Type Reason)
{
    for (auto& Pair : OriginalMaterials)
        if (IsValid(Pair.Key)) Pair.Key->SetLightFunctionMaterial(Pair.Value);
    Super::EndPlay(Reason);
}
