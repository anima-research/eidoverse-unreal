#include "EidoverseMotion.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseMotionConformance, "Eidoverse.MotionConformance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    bool Near(double A, double B, double Tolerance) { return FMath::Abs(A - B) <= Tolerance * FMath::Max(1.0, FMath::Abs(B)); }
    double At(const TArray<TSharedPtr<FJsonValue>>& A, int32 I) { return A[I]->AsNumber(); }
}

bool FEidoverseMotionConformance::RunTest(const FString&)
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Eidoverse"));
    if (!TestNotNull(TEXT("Eidoverse plugin"), Plugin.Get())) return false;
    const FString Path = Plugin->GetBaseDir() / TEXT("Resources/Conformance/motion.json");
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!TestTrue(TEXT("vectors readable"), FFileHelper::LoadFileToString(Text, *Path) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())) return false;

    const double Tolerance = Root->GetNumberField(TEXT("tolerance"));
    int32 Checked = 0, Failed = 0;
    for (const auto& V : Root->GetArrayField(TEXT("cases")))
    {
        const auto C = V->AsObject();
        const auto Expect = C->GetObjectField(TEXT("expect"));
        EidoMotion::FBase Base;
        EidoMotion::ReadBase(C->GetObjectField(TEXT("base")), Base);
        const auto R = EidoMotion::EvalWholeMotion(Base, C->GetObjectField(TEXT("motion")), C->GetNumberField(TEXT("nowMs")));
        const FString Name = FString::Printf(TEXT("%s @ %.0f"), *C->GetStringField(TEXT("name")), C->GetNumberField(TEXT("nowMs")));
        ++Checked;
        bool bOk = R.bOk == Expect->GetBoolField(TEXT("ok"));
        if (bOk && R.bOk)
        {
            const auto& P = Expect->GetArrayField(TEXT("pos"));
            const auto& Q = Expect->GetArrayField(TEXT("quat"));
            bOk = Near(R.Pos.X, At(P, 0), Tolerance) && Near(R.Pos.Y, At(P, 1), Tolerance) && Near(R.Pos.Z, At(P, 2), Tolerance)
                && Near(R.Quat.X, At(Q, 0), Tolerance) && Near(R.Quat.Y, At(Q, 1), Tolerance) && Near(R.Quat.Z, At(Q, 2), Tolerance) && Near(R.Quat.W, At(Q, 3), Tolerance)
                && Near(R.Yaw, Expect->GetNumberField(TEXT("yaw")), Tolerance)
                && R.bRot == Expect->GetBoolField(TEXT("rot"));
        }
        if (!bOk)
        {
            ++Failed;
            AddError(FString::Printf(TEXT("%s: got ok=%d pos=(%.9g,%.9g,%.9g) yaw=%.9g %s"), *Name, R.bOk, R.Pos.X, R.Pos.Y, R.Pos.Z, R.Yaw, *R.Why));
        }
    }
    UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_MOTION_CONFORMANCE checked=%d failed=%d"), Checked, Failed);
    return TestTrue(TEXT("vectors present"), Checked > 50) && Failed == 0;
}
#endif
