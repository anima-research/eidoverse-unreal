// Eidoverse.SnapFrame: the native side of optional /snap framing, against eidoverse-worlds
// tools/snapframe-test.ts's normaliser cases and net_snap.js / fp_view.js's camera geometry.
#include "EidoverseSnapFrame.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    TSharedPtr<FJsonObject> ParseFrame(const TCHAR* Json)
    {
        TSharedPtr<FJsonObject> O;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), O);
        return O;
    }
    FString Show(const EidoSnapFrame::FSnapFrame& F)
    {
        TArray<FString> P;
        if (F.Dist) P.Add(FString::Printf(TEXT("dist=%g"), *F.Dist));
        if (F.Height) P.Add(FString::Printf(TEXT("height=%g"), *F.Height));
        if (F.Pitch) P.Add(FString::Printf(TEXT("pitch=%g"), *F.Pitch));
        return TEXT("{") + FString::Join(P, TEXT(",")) + TEXT("}");
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseSnapFrame, "Eidoverse.SnapFrame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseSnapFrame::RunTest(const FString&)
{
    using namespace EidoSnapFrame;
    auto Is = [this](const TCHAR* What, const TCHAR* Json, const TCHAR* Want)
    {
        TestEqual(What, Show(Normalize(ParseFrame(Json))), FString(Want));
    };
    // ---- the normaliser
    Is(TEXT("omitted fields -> empty frame"), TEXT("{}"), TEXT("{}"));
    TestTrue(TEXT("no frame object -> empty frame"), Normalize(nullptr).IsEmpty());
    Is(TEXT("zero is a value, not an absence"), TEXT("{\"height\":0,\"pitch\":0}"), TEXT("{height=0,pitch=0}"));
    Is(TEXT("numeric strings (HTTP query) parse"), TEXT("{\"dist\":\"1.5\",\"pitch\":\"-40\"}"), TEXT("{dist=1.5,pitch=-40}"));
    Is(TEXT("blank / null strings drop"), TEXT("{\"dist\":\"\",\"height\":\"  \",\"pitch\":null}"), TEXT("{}"));
    Is(TEXT("booleans drop (false must not become 0)"), TEXT("{\"height\":false,\"dist\":true}"), TEXT("{}"));
    Is(TEXT("arrays and objects drop"), TEXT("{\"dist\":[3],\"pitch\":{\"valueOf\":5}}"), TEXT("{}"));
    Is(TEXT("non-numeric strings drop"), TEXT("{\"dist\":\"abc\",\"height\":\"12abc\",\"pitch\":\"Infinity\"}"), TEXT("{}"));
    Is(TEXT("clamps at the low ends"), TEXT("{\"dist\":0,\"height\":-5,\"pitch\":-200}"), TEXT("{dist=0.5,height=0,pitch=-85}"));
    Is(TEXT("clamps at the high ends"), TEXT("{\"dist\":999,\"height\":999,\"pitch\":200}"), TEXT("{dist=30,height=30,pitch=85}"));
    Is(TEXT("unknown fields are not carried"), TEXT("{\"dist\":2,\"fov\":90}"), TEXT("{dist=2}"));
    Is(TEXT("exponents and signs parse"), TEXT("{\"dist\":\" 2e0 \",\"height\":\"+3\"}"), TEXT("{dist=2,height=3}"));

    // ---- the cameras (feet at the origin, facing +X)
    const FVector Feet(0), Fwd(1, 0, 0);
    FVector Eye; FQuat Rot;
    BodyCamera(TEXT("third"), FSnapFrame(), Feet, Fwd, Eye, Rot);
    TestTrue(TEXT("third default: 3.4 m behind, 2.1 m up"), Eye.Equals(FVector(-340, 0, 210), 1e-6));
    TestTrue(TEXT("third default aims 1.2 m up, 4 m ahead"),
        Rot.GetForwardVector().Equals((FVector(400, 0, 120) - Eye).GetSafeNormal(), 1e-6));
    FSnapFrame Near; Near.Dist = 1.7; Near.Height = .8;
    BodyCamera(TEXT("third"), Near, Feet, Fwd, Eye, Rot);
    TestTrue(TEXT("third framed: dist and height"), Eye.Equals(FVector(-170, 0, 80), 1e-6));
    TestTrue(TEXT("third framed: the look-ahead scales with the distance"),
        Rot.GetForwardVector().Equals((FVector(200, 0, 120) - Eye).GetSafeNormal(), 1e-6));
    BodyCamera(TEXT("selfie"), FSnapFrame(), Feet, FVector(0, 1, .4), Eye, Rot);
    TestTrue(TEXT("selfie default: 2.6 m in front (flattened facing), 1.6 m up"), Eye.Equals(FVector(0, 260, 160), 1e-6));
    TestTrue(TEXT("selfie aims at the chest"), Rot.GetForwardVector().Equals((FVector(0, 0, 125) - Eye).GetSafeNormal(), 1e-6));
    TestFalse(TEXT("first is not a body camera"), BodyCamera(TEXT("first"), FSnapFrame(), Feet, Fwd, Eye, Rot));

    // ---- first-person pitch: + looks up, - looks down, the heading kept
    const FQuat Facing = FRotator(-12, 90, 0).Quaternion();
    const FRotator Up = FirstPersonGaze(30, Facing).Rotator(), Down = FirstPersonGaze(-40, Facing).Rotator(), Level = FirstPersonGaze(0, Facing).Rotator();
    TestTrue(TEXT("+30 looks up at 30 degrees"), FMath::IsNearlyEqual(Up.Pitch, 30., 1e-4));
    TestTrue(TEXT("-40 looks down at 40 degrees"), FMath::IsNearlyEqual(Down.Pitch, -40., 1e-4));
    TestTrue(TEXT("0 is level (not the view's own pitch)"), FMath::IsNearlyEqual(Level.Pitch, 0., 1e-4));
    TestTrue(TEXT("pitch keeps the heading"), FMath::IsNearlyEqual(Up.Yaw, 90., 1e-4) && FMath::IsNearlyEqual(Down.Yaw, 90., 1e-4));
    return true;
}
#endif
