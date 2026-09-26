#include "EidoverseBridge.h"
#include "Misc/AutomationTest.h"
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
#endif
