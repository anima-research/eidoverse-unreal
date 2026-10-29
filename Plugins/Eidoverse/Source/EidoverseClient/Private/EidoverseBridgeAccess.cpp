// Slice D's hook (Plugins/Eidoverse/Bridge/SLICES.md): bridge/access-state and bridge/access-directory (Bridge/src/access.ts).
#include "EidoverseBridge.h"
#include "Dom/JsonObject.h"

void AEidoverseBridge::OnAccessMsg(const FString& Type,const TSharedPtr<FJsonObject>& J)
{
    // slice D: not built yet (Bridge/SLICES.md)
}

// red-first stubs (the test's surface; built in the next commit)
bool AEidoverseBridge::SendAccessDoor(const FString& Door) { return false; }
bool AEidoverseBridge::SendAccessRole(const FString& Who,const FString& NewRole,double UntilMs) { return false; }
bool AEidoverseBridge::SendAccessRevoke(const FString& Who) { return false; }
bool AEidoverseBridge::AccessMayEdit() const { return false; }
void AEidoverseBridge::AccessReplied(const TSharedPtr<FJsonObject>& Reply) {}
TSharedPtr<FJsonObject> AEidoverseBridge::AccessTabState(const TSharedPtr<FJsonObject>& Wire) const { return nullptr; }
bool AEidoverseBridge::SendAccessGrantBridged(const TSharedRef<FJsonObject>& Args) { return false; }
