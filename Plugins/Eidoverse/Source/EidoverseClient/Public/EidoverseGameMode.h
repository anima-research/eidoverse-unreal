#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "EidoverseGameMode.generated.h"

class AEidoverseBridge;

// The general-purpose client: no pawn and no scenery until a world is joined.
// Spawns the bridge (unless the level already has one), which shows the front
// door while offline and renders only the server-authored world once joined.
UCLASS()
class EIDOVERSECLIENT_API AEidoverseGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AEidoverseGameMode();
    virtual void BeginPlay() override;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Eidoverse") TSubclassOf<AEidoverseBridge> BridgeClass;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse") TObjectPtr<AEidoverseBridge> Bridge;
};
