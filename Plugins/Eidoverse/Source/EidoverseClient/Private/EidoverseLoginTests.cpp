#include "EidoverseBridge.h"
#include "Misc/AutomationTest.h"
#include "EidoverseSecureStore.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseLoginSafety,"Eidoverse.LoginSafety",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEidoverseLoginSafety::RunTest(const FString&)
{
    const FString SID=FString::ChrN(64,'a');FString Out;
    TestTrue(TEXT("valid session cookie"),AEidoverseBridge::ParseProductionSession(TEXT("ew_sess=")+SID+TEXT("; Path=/; HttpOnly; Secure"),Out));
    TestEqual(TEXT("exact session extracted"),Out,SID);
    const TArray<FString> Invalid={TEXT(""),TEXT("ew_sess="),TEXT("other=")+SID,TEXT("ew_sess=")+SID+TEXT("\r\nCookie: injected"),TEXT("ew_sess=")+FString::ChrN(64,'z'),TEXT("ew_sess=")+SID+TEXT(",other=value")};
    for(const auto& H:Invalid){TestFalse(TEXT("invalid session refused"),AEidoverseBridge::ParseProductionSession(H,Out));TestTrue(TEXT("failure clears stale credential"),Out.IsEmpty());}
    TestTrue(TEXT("valid browser pairing code"),AEidoverseBridge::IsPairingCode(TEXT("ABCDE-12345")));
    for(const FString& Code:TArray<FString>{TEXT(""),TEXT("abcde-12345"),TEXT("ABCDE12345"),TEXT("ABCDE-12345&token=secret"),TEXT("ZZZZZ-12345"),TEXT("https://evil.test")})
        TestFalse(TEXT("invalid pairing URL component refused"),AEidoverseBridge::IsPairingCode(Code));
    return true;
}
// The Keychain store behind the saved production sign-in, on its own account so a
// real saved sign-in is never touched.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseSecureStore,"Eidoverse.SecureStore",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEidoverseSecureStore::RunTest(const FString&)
{
#if PLATFORM_MAC
    const FString Account=TEXT("automation-probe");FString Out;
    EidoSecureStore::Delete(Account);
    TestFalse(TEXT("absent before save"),EidoSecureStore::Load(Account,Out));
    TestTrue(TEXT("saved"),EidoSecureStore::Save(Account,TEXT("{\"session\":\"first \u00e9\"}")));
    TestTrue(TEXT("loaded"),EidoSecureStore::Load(Account,Out));
    TestEqual(TEXT("round trip (UTF-8)"),Out,FString(TEXT("{\"session\":\"first \u00e9\"}")));
    TestTrue(TEXT("overwrite"),EidoSecureStore::Save(Account,TEXT("second")));
    TestTrue(TEXT("loaded again"),EidoSecureStore::Load(Account,Out));
    TestEqual(TEXT("latest value wins"),Out,FString(TEXT("second")));
    EidoSecureStore::Delete(Account);
    TestFalse(TEXT("gone after delete"),EidoSecureStore::Load(Account,Out));
#endif
    return true;
}
#endif
