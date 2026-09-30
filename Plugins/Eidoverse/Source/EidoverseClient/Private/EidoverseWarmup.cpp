#include "EidoverseWarmup.h"
#include "EidoverseScene.h"
#include "EngineUtils.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"

namespace
{
    // glTFRuntime's base materials (the importer picks one per glTF material type)
    // and the client's own runtime templates.
    const TCHAR* WarmMaterialPaths[] = {
        TEXT("/glTFRuntime/M_glTFRuntimeBase"), TEXT("/glTFRuntime/M_glTFRuntimeMasked_Inst"),
        TEXT("/glTFRuntime/M_glTFRuntimeTranslucent_Inst"), TEXT("/glTFRuntime/M_glTFRuntimeTwoSided_Inst"),
        TEXT("/glTFRuntime/M_glTFRuntimeTwoSidedMasked_Inst"), TEXT("/glTFRuntime/M_glTFRuntimeTwoSidedTranslucent_Inst"),
        TEXT("/glTFRuntime/M_Unlit_glTFRuntimeBase"), TEXT("/glTFRuntime/M_Unlit_glTFRuntimeMasked_Inst"),
        TEXT("/glTFRuntime/M_Unlit_glTFRuntimeTranslucent_Inst"), TEXT("/glTFRuntime/M_Unlit_glTFRuntimeTwoSided_Inst"),
        TEXT("/glTFRuntime/M_Unlit_glTFRuntimeTwoSidedMasked_Inst"), TEXT("/glTFRuntime/M_Unlit_glTFRuntimeTwoSidedTranslucent_Inst"),
        TEXT("/glTFRuntime/M_ClearCoat_glTFRuntimeBase"), TEXT("/glTFRuntime/M_ClearCoat_glTFRuntimeTranslucent_Inst"),
        TEXT("/glTFRuntime/M_ClearCoat_glTFRuntimeTwoSided_Inst"), TEXT("/glTFRuntime/M_ClearCoat_glTFRuntimeTwoSidedTranslucent_Inst"),
        TEXT("/glTFRuntime/M_glTFRuntime_SG_Base"), TEXT("/glTFRuntime/M_glTFRuntime_SG_Translucent_Inst"),
        TEXT("/glTFRuntime/M_glTFRuntime_SG_Twosided_Inst"), TEXT("/glTFRuntime/M_glTFRuntime_SG_TwoSidedTranslucent_Inst"),
        TEXT("/glTFRuntime/M_Transmission_glTFRuntimeBase"), TEXT("/glTFRuntime/M_Transmission_glTFRuntimeTwoSided_Inst"),
        TEXT("/Game/Underwater/Avatars/Materials/MI_AvatarLit_Opaque"), TEXT("/Game/Underwater/Avatars/Materials/MI_AvatarLit_TwoSided"),
        TEXT("/Game/Underwater/Avatars/Materials/MI_AvatarLit_Masked"), TEXT("/Game/Underwater/Avatars/Materials/MI_AvatarLit_TwoSidedMasked"),
        TEXT("/Game/Underwater/Avatars/Materials/MI_AvatarLit_Translucent"), TEXT("/Game/Underwater/Avatars/Materials/MI_AvatarLit_TwoSidedTranslucent"),
        TEXT("/Game/Underwater/Avatars/Materials/MI_AvatarPBR_Translucent"), TEXT("/Game/Underwater/Avatars/Materials/MI_AvatarPBR_TwoSidedTranslucent"),
        TEXT("/Game/Underwater/Avatars/Materials/M_EidoverseGroundCover"),
    };
    // Close to the camera and tiny: a ~12 cm block of 2 cm pieces 1.5 m ahead,
    // hidden behind the centred door panel but still drawn (and ray traced).
    constexpr float Distance = 150, Piece = 2.f, Pitch = 2.6f;
}

AEidoverseShaderWarmup::AEidoverseShaderWarmup()
{
    PrimaryActorTick.bCanEverTick = true;
    SetActorEnableCollision(false);
    Anchor = CreateDefaultSubobject<USceneComponent>(TEXT("Anchor"));
    SetRootComponent(Anchor);
}

void AEidoverseShaderWarmup::BeginPlay()
{
    Super::BeginPlay();
    StartedAt = FPlatformTime::Seconds();
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    USkeletalMesh* Skinned = LoadObject<USkeletalMesh>(nullptr, TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
    // A shadow-casting sun, so shadow-depth pipelines compile too.
    auto* Sun = NewObject<UDirectionalLightComponent>(this);
    Sun->SetupAttachment(Anchor); Sun->SetMobility(EComponentMobility::Movable);
    Sun->SetRelativeRotation(FRotator(-50, 30, 0)); Sun->SetIntensity(2); Sun->RegisterComponent();
    int32 Index = 0;
    for (const TCHAR* Path : WarmMaterialPaths)
    {
        UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Path);
        if (!M) { UE_LOG(LogTemp, Verbose, TEXT("EIDOVERSE warm-up: missing %s"), Path); continue; }
        Materials.Add(M);
        const FVector Cell((Index % 6 - 2.5f) * Pitch, (Index / 6 - 2.5f) * Pitch, 0);
        if (Cube)
        {
            auto* S = NewObject<UStaticMeshComponent>(this);
            S->SetupAttachment(Anchor); S->SetStaticMesh(Cube); S->SetMaterial(0, M);
            S->SetRelativeLocation(FVector(0, Cell.X, Cell.Y)); S->SetRelativeScale3D(FVector(Piece / 100.f));
            S->SetCollisionEnabled(ECollisionEnabled::NoCollision); S->RegisterComponent();
        }
        if (Skinned)
        {
            auto* K = NewObject<USkeletalMeshComponent>(this);
            K->SetupAttachment(Anchor); K->SetSkeletalMesh(Skinned);
            for (int32 Slot = 0; Slot < FMath::Max(1, K->GetNumMaterials()); ++Slot) K->SetMaterial(Slot, M);
            K->SetRelativeLocation(FVector(Pitch * .5f, Cell.X + Pitch * .5f, Cell.Y + Pitch * .5f));
            K->SetRelativeScale3D(FVector(Piece / 100.f)); K->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            K->RegisterComponent();
        }
        ++Index;
    }
    UE_LOG(LogTemp, Display, TEXT("EIDOVERSE warm-up: %d base materials on static and skinned meshes"), Materials.Num());
}

void AEidoverseShaderWarmup::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // A world is being joined: its own meshes take over from here.
    if (TActorIterator<AEidoverseScene>(GetWorld())) { Destroy(); return; }
    // Ride the view: the door has no pawn, so follow whatever the camera is.
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye, View);
        SetActorLocationAndRotation(Eye + View.Vector() * Distance, View);
    }
    int32 Pending = 0;
    for (UMaterialInterface* M : Materials) if (M && M->IsCompiling()) ++Pending;
    PendingMaterials = Pending;
    WarmMaterials = Materials.Num() - Pending;
    if (!bReported && Pending == 0 && FPlatformTime::Seconds() - StartedAt > 2)
    {
        bReported = true;
        UE_LOG(LogTemp, Display, TEXT("EIDOVERSE warm-up: %d base materials ready after %.1fs"), Materials.Num(), FPlatformTime::Seconds() - StartedAt);
    }
}
