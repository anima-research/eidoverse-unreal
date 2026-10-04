#include "EidoverseSecureStore.h"

#if PLATFORM_MAC
#include "Mac/MacPlatformString.h"
#include <Security/Security.h>

namespace
{
    // One generic-password item per account under this service.
    const TCHAR* Service = TEXT("ai.animalabs.eidoverse.unreal");

    CFMutableDictionaryRef Query(const FString& Account)
    {
        CFMutableDictionaryRef Q = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFStringRef S = FPlatformString::TCHARToCFString(Service);
        CFStringRef A = FPlatformString::TCHARToCFString(*Account);
        CFDictionarySetValue(Q, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(Q, kSecAttrService, S);
        CFDictionarySetValue(Q, kSecAttrAccount, A);
        CFRelease(S); CFRelease(A);
        return Q;
    }
}

bool EidoSecureStore::Save(const FString& Account, const FString& Secret)
{
    const FTCHARToUTF8 Utf8(*Secret);
    CFDataRef Data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(Utf8.Get()), Utf8.Length());
    CFMutableDictionaryRef Q = Query(Account);
    CFMutableDictionaryRef Update = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(Update, kSecValueData, Data);
    OSStatus Status = SecItemUpdate(Q, Update);
    if (Status == errSecItemNotFound)
    {
        CFDictionarySetValue(Q, kSecValueData, Data);
        CFDictionarySetValue(Q, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
        Status = SecItemAdd(Q, nullptr);
    }
    CFRelease(Update); CFRelease(Q); CFRelease(Data);
    if (Status != errSecSuccess) UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE keychain save failed (OSStatus %d)"), int32(Status));
    return Status == errSecSuccess;
}

bool EidoSecureStore::Load(const FString& Account, FString& OutSecret)
{
    OutSecret.Empty();
    CFMutableDictionaryRef Q = Query(Account);
    CFDictionarySetValue(Q, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(Q, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef Result = nullptr;
    const OSStatus Status = SecItemCopyMatching(Q, &Result);
    CFRelease(Q);
    if (Status != errSecSuccess || !Result) return false;
    CFDataRef Data = static_cast<CFDataRef>(Result);
    const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(CFDataGetBytePtr(Data)), int32(CFDataGetLength(Data)));
    OutSecret = FString(Text.Length(), Text.Get());
    CFRelease(Result);
    return !OutSecret.IsEmpty();
}

void EidoSecureStore::Delete(const FString& Account)
{
    CFMutableDictionaryRef Q = Query(Account);
    SecItemDelete(Q);
    CFRelease(Q);
}

#else

bool EidoSecureStore::Save(const FString&, const FString&) { return false; }
bool EidoSecureStore::Load(const FString&, FString& OutSecret) { OutSecret.Empty(); return false; }
void EidoSecureStore::Delete(const FString&) {}

#endif
