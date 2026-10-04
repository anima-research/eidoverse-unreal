#include "EidoverseGameMode.h"
#include "EidoverseWarmup.h"
#include "EidoverseBridge.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

AEidoverseGameMode::AEidoverseGameMode()
{
    // Nothing to walk around in until the server provides a world.
    DefaultPawnClass = nullptr;
    BridgeClass = AEidoverseBridge::StaticClass();
}

void AEidoverseGameMode::BeginPlay()
{
    Super::BeginPlay();
    TActorIterator<AEidoverseBridge> Existing(GetWorld());
    if (Existing) Bridge = *Existing;
    if (!Bridge)
    {
        FActorSpawnParameters Params;
        Params.ObjectFlags |= RF_Transient;
        Bridge = GetWorld()->SpawnActor<AEidoverseBridge>(BridgeClass ? BridgeClass.Get() : AEidoverseBridge::StaticClass(), Params);
    }
    Bridge->bRenderDynamicWorld = true;
    Bridge->bShowChatUI = true;
    Bridge->bFrontDoor = true;
    Bridge->LoadClientSettings();
    Bridge->RestoreProductionSession();
    // Compile the shaders a world will need while the door is up (EidoverseWarmup.h).
    FActorSpawnParameters WarmupParams; WarmupParams.ObjectFlags |= RF_Transient;
    GetWorld()->SpawnActor<AEidoverseShaderWarmup>(WarmupParams);
}
