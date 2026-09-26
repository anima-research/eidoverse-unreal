#include "EidoverseAvatar.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseEmissive,"Underwater.Eidoverse.EmissiveLamps",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEidoverseEmissive::RunTest(const FString&)
{
    TestEqual(TEXT("dark materials do not create lights"),FEidoverseAvatarLamp::InferredIntensity(FLinearColor::Black,1),0.f);
    TestEqual(TEXT("dim gold trim is not a lamp"),FEidoverseAvatarLamp::InferredIntensity(FLinearColor(.075,.047,.01),1),0.f);
    TestTrue(TEXT("authored strength selects lampglass"),FMath::IsNearlyEqual(FEidoverseAvatarLamp::InferredIntensity(FLinearColor(1,.72,.25),3),13.8f,1e-4f));
    TestEqual(TEXT("inferred light remains bounded"),FEidoverseAvatarLamp::InferredIntensity(FLinearColor::White,1e6),40.f);
    TestEqual(TEXT("zero strength stays dark"),FEidoverseAvatarLamp::InferredIntensity(FLinearColor::White,0),0.f);
    TestEqual(TEXT("negative strength is refused"),FEidoverseAvatarLamp::InferredIntensity(FLinearColor::White,-1),0.f);
    TestTrue(TEXT("ember floor"),FMath::IsNearlyEqual(FEidoverseAvatarLamp::Breath(0),.03f));
    TestTrue(TEXT("soft peak"),FMath::IsNearlyEqual(FEidoverseAvatarLamp::Breath(1.7),.45f));
    for(double T=0;T<7;T+=.1)
    {
        const float A=FEidoverseAvatarLamp::Breath(T);
        TestTrue(TEXT("pulse stays within authored bounds"),A>=.02999f&&A<=.45001f);
        TestTrue(TEXT("3.4 second repeat"),FMath::IsNearlyEqual(A,FEidoverseAvatarLamp::Breath(T+3.4),1e-5f));
    }
    return true;
}
#endif
