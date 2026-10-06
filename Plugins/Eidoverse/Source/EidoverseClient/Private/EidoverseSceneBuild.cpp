// Build-mode support for AEidoverseScene: entity poses in wire units and the
// selection overlay. Authoring itself goes through verbs (spawn/place/remove).
#include "EidoverseScene.h"
#include "EidoverseBridge.h"
#include "EidoverseMotion.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "EidoverseAvatar.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

bool AEidoverseScene::GetEntityPose(const FString& Id, FVector& OutWirePos, double& OutYaw, double& OutScale, FString& OutLib, bool& bAnimated) const
{
    const FEidoverseSceneItem* E = Items.Find(Id);
    if (!E || !E->bPlaced) return false;
    OutWirePos = AEidoverseBridge::ToWirePosition(E->Target.GetLocation() - SceneOrigin);
    const FQuat Wire = AEidoverseBridge::ToWireRotation(E->Target.GetRotation());
    OutYaw = EidoMotion::YawOf({Wire.X, Wire.Y, Wire.Z, Wire.W});
    OutScale = E->Target.GetScale3D().X;
    OutLib = E->Lib;
    bAnimated = E->Motion.IsValid() || !E->MountTo.IsEmpty();
    return true;
}

void AEidoverseScene::SetSelected(const FString& Id)
{
    if (!SelectionMaterial) SelectionMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Eidoverse/UI/M_EidoverseSelection.M_EidoverseSelection"));
    auto Apply = [this](const FString& Which, UMaterialInterface* Material)
    {
        const FEidoverseSceneItem* E = Items.Find(Which);
        if (!E || !E->Actor.IsValid()) return;
        TArray<UMeshComponent*> Meshes;
        E->Actor->GetComponents(Meshes);
        for (UMeshComponent* M : Meshes) M->SetOverlayMaterial(Material);
    };
    if (!SelectedId.IsEmpty()) Apply(SelectedId, nullptr);
    SelectedId = Items.Contains(Id) ? Id : FString();
    if (!SelectedId.IsEmpty()) Apply(SelectedId, SelectionMaterial);
}

bool AEidoverseScene::HoldGhost(const FString& Lib)
{
    ClearGhost();
    if (Lib == TEXT("@light"))
    {
        // lights.js: the light tile holds a 0.13 m bulb, placed 0.9 m above the aim.
        AActor* A = GetWorld()->SpawnActor<AActor>();
        A->SetFlags(RF_Transient); A->SetActorEnableCollision(false);
        auto* Root = NewObject<USceneComponent>(A); A->AddInstanceComponent(Root); A->SetRootComponent(Root); Root->RegisterComponent();
        auto* M = NewObject<UStaticMeshComponent>(A); A->AddInstanceComponent(M); M->SetupAttachment(Root);
        M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
        M->SetRelativeScale3D(FVector(.26)); M->SetRelativeLocation(FVector(0, 0, 90));
        M->SetCollisionEnabled(ECollisionEnabled::NoCollision); M->SetCastShadow(false); M->RegisterComponent();
        GhostActor = A; GhostMesh = M; GhostLib = Lib;
        TickGhost();
        return true;
    }
    FString Url;
    if (!UEidoverseAvatarAsset::ResolveUrl(LibraryBase, Lib, Url)) return false;
    Url += Url.Contains(TEXT("?")) ? TEXT("&native=1") : TEXT("?native=1");
    // Share the entity cache: the model is usually about to become an entity anyway.
    if (!Assets.Contains(Url) && Assets.Num() < 64)
    {
        auto* Asset = NewObject<UEidoverseAvatarAsset>();
        Asset->bStaticModel = true; Asset->StaticMeshOuter = this; Asset->Url = Url;
        Assets.Add(Url, Asset);
    }
    TObjectPtr<UEidoverseAvatarAsset>* Found = Assets.Find(Url);
    if (!Found) return false;
    GhostAsset = *Found;
    AActor* A = GetWorld()->SpawnActor<AActor>();
    A->SetFlags(RF_Transient); A->SetActorEnableCollision(false);
    auto* Root = NewObject<USceneComponent>(A); A->AddInstanceComponent(Root); A->SetRootComponent(Root); Root->RegisterComponent();
    // Until the model arrives, a small seafoam cube marks the spot.
    auto* M = NewObject<UStaticMeshComponent>(A); A->AddInstanceComponent(M); M->SetupAttachment(Root);
    M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    M->SetRelativeScale3D(FVector(.3)); M->SetRelativeLocation(FVector(0, 0, 15));
    M->SetCollisionEnabled(ECollisionEnabled::NoCollision); M->SetCastShadow(false);
    M->RegisterComponent();
    GhostActor = A; GhostMesh = M; GhostLib = Lib;
    TickGhost();
    return true;
}

void AEidoverseScene::ClearGhost()
{
    if (GhostActor.IsValid()) GhostActor->Destroy();
    GhostActor.Reset(); GhostMesh.Reset(); GhostAsset = nullptr; GhostLib.Empty();
}

void AEidoverseScene::SetGhostPose(const FVector& World, double WireYaw, double Scale)
{
    if (!GhostActor.IsValid()) return;
    const FQuat Wire(FVector(0, 1, 0), WireYaw);
    GhostActor->SetActorTransform(FTransform(AEidoverseBridge::FromWireRotation(Wire), World, FVector(Scale)));
}

void AEidoverseScene::TickGhost()
{
    UStaticMeshComponent* M = GhostMesh.Get();
    if (!M) return;
    if (!GhostMaterial) GhostMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Eidoverse/UI/M_EidoverseGhost.M_EidoverseGhost"));
    if (M->GetMaterial(0) != GhostMaterial) M->SetMaterial(0, GhostMaterial);
    if (!GhostAsset) return;
    if (GhostAsset->State == UEidoverseAvatarAsset::EState::Failed) { M->SetVisibility(true); return; }
    if (GhostAsset->State != UEidoverseAvatarAsset::EState::Ready || M->GetStaticMesh() == GhostAsset->StaticMesh) return;
    M->SetStaticMesh(GhostAsset->StaticMesh);
    M->SetRelativeLocation(FVector::ZeroVector);
    M->SetRelativeScale3D(GhostAsset->bMirrored ? FVector(1, -1, 1) : FVector::OneVector);
    for (int32 I = 0; I < M->GetNumMaterials(); ++I) M->SetMaterial(I, GhostMaterial);
}

FString AEidoverseScene::GetEntityRevision(const FString& Id) const
{
    const FEidoverseSceneItem* E = Items.Find(Id);
    return E ? E->Revision : FString();
}
