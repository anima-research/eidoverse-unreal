#include "EidoverseBridge.h"
#include "EidoverseAvatar.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseCoordinates,"Eidoverse.Coordinates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEidoverseCoordinates::RunTest(const FString&)
{
    TestTrue(TEXT("ordinary destination accepted"),AEidoverseBridge::IsWorldName(TEXT("commons-2_test")));
    for(const TCHAR* Bad:{TEXT(""),TEXT("../water"),TEXT("water?token=x"),TEXT("water world"),TEXT("*"),TEXT("https://example.test")})
        TestFalse(TEXT("unsafe destination refused"),AEidoverseBridge::IsWorldName(Bad));
    TestFalse(TEXT("long world refused"),AEidoverseBridge::IsWorldName(FString::ChrN(65,'a')));
    const FVector P(7900,-2150,-700),Wire=AEidoverseBridge::ToWirePosition(P);
    TestTrue(TEXT("metres, Y-up, +Z forward, reflected handedness"),Wire.Equals(FVector(21.5,-7,79),1e-6));
    TestTrue(TEXT("position round trip"),AEidoverseBridge::FromWirePosition(Wire).Equals(P,1e-6));
    TestTrue(TEXT("wire +X stays screen-left when looking +Z"),AEidoverseBridge::FromWirePosition(FVector(1,0,0)).Equals(FVector(0,-100,0)));
    const FQuat WireYaw(FVector::YAxisVector,PI/2);
    TestTrue(TEXT("positive wire yaw turns toward UE -Y"),AEidoverseBridge::FromWireRotation(WireYaw).RotateVector(FVector::ForwardVector).Equals(-FVector::YAxisVector,1e-6));
    for(FRotator R:{FRotator(0,0,0),FRotator(25,90,13),FRotator(-20,-148,-30),FRotator(89,179,60)})
    {
        FQuat Q=R.Quaternion(),W=AEidoverseBridge::ToWireRotation(Q);
        TestTrue(TEXT("quaternion round trip"),AEidoverseBridge::FromWireRotation(W).Equals(Q,1e-6));
        const FVector Expected=AEidoverseBridge::ToWirePosition(Q.RotateVector(FVector::ForwardVector))*100;
        TestTrue(TEXT("orientation rotates forward consistently"),W.RotateVector(FVector(0,0,1)).Equals(Expected,1e-6));
        for(FVector Axis:{FVector::XAxisVector,FVector::YAxisVector,FVector::ZAxisVector})
            TestTrue(TEXT("basis conjugation preserves every rotated axis"),AEidoverseBridge::FromWirePosition(W.RotateVector(Axis)).Equals(Q.RotateVector(AEidoverseBridge::FromWirePosition(Axis)),1e-5));
    }
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseSwimming,"Eidoverse.Swimming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEidoverseSwimming::RunTest(const FString&)
{
    const auto Hover=FEidoverseSwimMotion::Evaluate(TEXT("swim"),0,FVector::ZeroVector);
    const auto Swim=FEidoverseSwimMotion::Evaluate(TEXT("swim"),1,FVector(1,0,0));
    const auto Dive=FEidoverseSwimMotion::Evaluate(TEXT("swim"),1,FVector(.7,0,-.7));
    const auto Ascend=FEidoverseSwimMotion::Evaluate(TEXT("swim"),1,FVector(.7,0,.7));
    const auto Surface=FEidoverseSwimMotion::Evaluate(TEXT("surface"),1,FVector(1,0,-.3));
    TestEqual(TEXT("rest uses hovering"),Hover.State,FString(TEXT("Hovering")));
    TestTrue(TEXT("hover is upright and slow"),Hover.Lean<15&&Hover.CycleHz<.4f);
    TestTrue(TEXT("cruise lies almost horizontal"),Swim.Lean>70&&Swim.Lean<90);
    TestEqual(TEXT("negative vertical travel dives"),Dive.State,FString(TEXT("Diving")));
    TestEqual(TEXT("positive vertical travel ascends"),Ascend.State,FString(TEXT("Ascending")));
    TestTrue(TEXT("dive points down, ascent points up"),Dive.Lean>100&&Ascend.Lean<60);
    TestTrue(TEXT("surface keeps head upright"),Surface.Lean<=35&&Surface.Dive==0);
    TestFalse(TEXT("walking does not enable aquatic gait"),FEidoverseSwimMotion::Evaluate(TEXT("walk"),1,FVector(1,0,0)).bAquatic);
    TestTrue(TEXT("backpedal stays upright"),FEidoverseSwimMotion::Evaluate(TEXT("swim"),1,FVector(-1,0,0)).Lean<=30);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseAvatarSafety,"Eidoverse.AvatarSafety",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEidoverseAvatarSafety::RunTest(const FString&)
{
    const FString Base=TEXT("https://eidoverse.animalabs.ai/library/");FString Url;
    TestTrue(TEXT("versioned library VRM"),UEidoverseAvatarAsset::ResolveUrl(Base,TEXT("eidoverse/assets/vrms/claude.vrm?v=1786600887000"),Url));
    TestEqual(TEXT("resolved library URL"),Url,Base+TEXT("eidoverse/assets/vrms/claude.vrm?v=1786600887000"));
    for(const TCHAR* Bad:{TEXT("https://evil.test/a.vrm"),TEXT("//evil.test/a.vrm"),TEXT("../a.vrm"),TEXT("a/%2e%2e/a.vrm"),TEXT("a.vrm?token=secret"),TEXT("a.vrm?v=1&url=x"),TEXT("a.vrm#x"),TEXT("a.gltf"),TEXT("a\\b.vrm")})
        TestFalse(TEXT("untrusted reference rejected"),UEidoverseAvatarAsset::ResolveUrl(Base,Bad,Url));
    TestFalse(TEXT("cleartext library rejected"),UEidoverseAvatarAsset::ResolveUrl(TEXT("http://eidoverse.animalabs.ai/library/"),TEXT("a.vrm"),Url));
    TSharedPtr<FJsonObject> Json;FString Error;TArray<uint8> Empty;
    TestFalse(TEXT("empty asset rejected"),UEidoverseAvatarAsset::ValidateGlb(Empty,Json,Error));
    Empty.SetNumZeroed(32);
    TestFalse(TEXT("non-GLB asset rejected"),UEidoverseAvatarAsset::ValidateGlb(Empty,Json,Error));
    return true;
}
#endif
