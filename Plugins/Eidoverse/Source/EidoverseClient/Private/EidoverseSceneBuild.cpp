// Build-mode support for AEidoverseScene: entity poses in wire units and the
// selection overlay. Authoring itself goes through verbs (spawn/place/remove).
#include "EidoverseScene.h"
#include "EidoverseBridge.h"
#include "EidoverseMotion.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"

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
