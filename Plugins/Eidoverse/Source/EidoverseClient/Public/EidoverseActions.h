#pragma once
#include "CoreMinimal.h"

// actions.js natively: the one list of things a person can do here — panels, body keys, emotes,
// voice, slash commands — each registered where it is defined, ranked by the same tiers:
// exact 1000 > prefix 900 > key 850 > word-start 700 > all-words 650 > substring 400 > subsequence 100-199,
// a keyword match one tier lower (-100), one stray letter never a match.
struct EIDOVERSECLIENT_API FEidoAction
{
    FString Id, Title, Group = TEXT("other"), Key, Icon, Detail, Fill;
    TArray<FString> Keywords;
    TFunction<void()> Run;
    TFunction<bool()> When;
};

namespace EidoActions
{
    EIDOVERSECLIENT_API void Register(const FEidoAction& A);   // an existing id merges (set fields win)
    EIDOVERSECLIENT_API void Unregister(const FString& Id);
    EIDOVERSECLIENT_API void Clear();
    EIDOVERSECLIENT_API int32 Score(const FString& Query, const FEidoAction& A);
    // best first: score, then the shorter title, then registration order
    EIDOVERSECLIENT_API TArray<TPair<const FEidoAction*, int32>> List(const FString& Query, int32 Limit = 50);
    EIDOVERSECLIENT_API bool Run(const FString& Id);
    constexpr int32 Strong = 850;
}
