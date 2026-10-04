#pragma once
#include "CoreMinimal.h"

// Small secrets (the production sign-in) in the platform's credential store:
// the macOS Keychain on Mac; elsewhere nothing is stored (Save returns false).
namespace EidoSecureStore
{
    bool Save(const FString& Account, const FString& Secret);
    bool Load(const FString& Account, FString& OutSecret);
    void Delete(const FString& Account);
}
