#pragma once
#include "CoreMinimal.h"

// tts-chunk.js: the spoken form of an utterance, cut into synthesis-sized chunks.
namespace EidoTts
{
    EIDOVERSECLIENT_API FString SpokenForm(const FString& Text);
    EIDOVERSECLIENT_API TArray<FString> Chunks(const FString& Text);
    EIDOVERSECLIENT_API TArray<float> To48k(const TArray<int16>& Pcm, int32 Rate);
}
