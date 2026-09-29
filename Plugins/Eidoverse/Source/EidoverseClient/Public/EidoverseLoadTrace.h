#pragma once
#include "CoreMinimal.h"

// Where does a busy join spend its frames? Stages of asset loading note their
// game-thread milliseconds here; a frame over the hitch threshold logs what ran in it
// (with the GPU frame time), and `Eidoverse.LoadStats` prints per-stage totals.
// Cheap enough to leave on: notes are a few strings per loaded asset.
namespace EidoLoadTrace
{
    EIDOVERSECLIENT_API void Note(const TCHAR* Stage, const FString& What, double Ms);
    // Once per frame from the bridge.
    EIDOVERSECLIENT_API void Tick(float DeltaSeconds);
    EIDOVERSECLIENT_API void Reset();
    EIDOVERSECLIENT_API FString Summary();

    struct FScope
    {
        const TCHAR* Stage; FString What; double Start;
        FScope(const TCHAR* InStage, const FString& InWhat) : Stage(InStage), What(InWhat), Start(FPlatformTime::Seconds()) {}
        ~FScope() { Note(Stage, What, (FPlatformTime::Seconds() - Start) * 1000.0); }
    };
}
