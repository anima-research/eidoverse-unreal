// Slice B's hook (Plugins/Eidoverse/Bridge/SLICES.md): bridge/say and bridge/narration (Bridge/src/chat.ts).
#include "EidoverseBridge.h"
#include "EidoverseClips.h"
#include "Dom/JsonObject.h"

void AEidoverseBridge::OnChatMsg(const FString& Type,const TSharedPtr<FJsonObject>& J)
{
    // slice B: not built yet (Bridge/SLICES.md)
}
FString AEidoverseBridge::PeerOnceAnim(const FString& Id) const
{
    const FEidoversePeer* P=Peers.Find(Id);return P?P->ActiveEmote:FString();
}
TArray<FString> AEidoverseBridge::ClipEmoteNames() const
{
    return ClipLibrary?ClipLibrary->EmoteNames():TArray<FString>();
}
