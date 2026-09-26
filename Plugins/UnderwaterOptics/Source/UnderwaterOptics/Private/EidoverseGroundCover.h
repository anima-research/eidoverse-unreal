#pragma once
#include "CoreMinimal.h"
class UStaticMesh;
class UMaterialInterface;
namespace EidoGroundCover
{
// Shared centimetre-scale tuft: opaque geometry, 36/12/6 blades across 3 LODs.
UStaticMesh* BuildTuft(UObject* Owner,UMaterialInterface* Material);
}
