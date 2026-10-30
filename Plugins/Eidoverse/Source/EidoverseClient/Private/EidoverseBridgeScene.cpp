// Slice A's hook (Plugins/Eidoverse/Bridge/SLICES.md): bridge/scene v2: the render model the bridge folds (Bridge/src/scene.ts). Slice 0 hands it to the v1 path as it is.
#include "EidoverseBridge.h"
#include "Dom/JsonObject.h"

void AEidoverseBridge::OnSceneMsg(const FString& Type,const TSharedPtr<FJsonObject>& J)
{
    ReceiveDynamicScene(J);
    ReceiveSceneSeats(J);   // slice D's seats[] (EidoverseBridgeBodies.cpp)
}
