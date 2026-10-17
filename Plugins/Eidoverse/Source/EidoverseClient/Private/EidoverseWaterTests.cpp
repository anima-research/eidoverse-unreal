#include "EidoverseWater.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseWaterConformance, "Eidoverse.WaterConformance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    bool Near(double A, double B, double Tolerance) { return FMath::Abs(A - B) <= Tolerance * FMath::Max(1.0, FMath::Abs(B)); }
    EidoWater::FVec3 Vec(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
    {
        const auto& A = O->GetArrayField(Key);
        return {A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()};
    }
    bool NearVec(const EidoWater::FVec3& A, const EidoWater::FVec3& B, double T) { return Near(A.X, B.X, T) && Near(A.Y, B.Y, T) && Near(A.Z, B.Z, T); }
    TSharedPtr<FJsonObject> DataOf(const TSharedPtr<FJsonObject>& C)
    {
        // Params data can hold NaN-free malformed values; missing object -> empty.
        const TSharedPtr<FJsonObject>* D = nullptr;
        return C->TryGetObjectField(TEXT("data"), D) ? *D : MakeShared<FJsonObject>();
    }
}

bool FEidoverseWaterConformance::RunTest(const FString&)
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Eidoverse"));
    if (!TestNotNull(TEXT("Eidoverse plugin"), Plugin.Get())) return false;
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!TestTrue(TEXT("vectors readable"), FFileHelper::LoadFileToString(Text, *(Plugin->GetBaseDir() / TEXT("Resources/Conformance/water.json"))) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())) return false;
    const double T = Root->GetNumberField(TEXT("tolerance"));
    int32 Checked = 0, Failed = 0;
    auto Check = [&](bool bOk, const FString& What) { ++Checked; if (!bOk) { ++Failed; AddError(What); } };

    for (const auto& V : Root->GetArrayField(TEXT("params")))
    {
        const auto C = V->AsObject(), E = C->GetObjectField(TEXT("expect"));
        const auto W = EidoWater::ReadParams(DataOf(C));
        const auto& Waves = E->GetArrayField(TEXT("waves"));
        bool bOk = NearVec(W.Center, Vec(E, TEXT("center")), T) && NearVec(W.Size, Vec(E, TEXT("size")), T)
            && NearVec(W.Absorption, Vec(E, TEXT("absorption")), T) && NearVec(W.Scatter, Vec(E, TEXT("scatter")), T)
            && Near(W.Speed, E->GetNumberField(TEXT("speed")), T) && Near(W.Top, E->GetNumberField(TEXT("top")), T)
            && W.Waves.Num() == Waves.Num();
        for (int32 I = 0; bOk && I < Waves.Num(); ++I)
        {
            const auto X = Waves[I]->AsObject();
            const auto& D = X->GetArrayField(TEXT("direction"));
            bOk = Near(W.Waves[I].Amplitude, X->GetNumberField(TEXT("amplitude")), T) && Near(W.Waves[I].Wavelength, X->GetNumberField(TEXT("wavelength")), T)
                && Near(W.Waves[I].DirX, D[0]->AsNumber(), T) && Near(W.Waves[I].DirZ, D[1]->AsNumber(), T) && Near(W.Waves[I].Phase, X->GetNumberField(TEXT("phase")), T);
        }
        Check(bOk, TEXT("params: ") + C->GetStringField(TEXT("name")));
    }
    for (const auto& V : Root->GetArrayField(TEXT("heights")))
    {
        const auto C = V->AsObject();
        const auto W = EidoWater::ReadParams(DataOf(C));
        const double H = EidoWater::WaveHeight(W, C->GetNumberField(TEXT("x")), C->GetNumberField(TEXT("z")), C->GetNumberField(TEXT("t")));
        Check(Near(H, C->GetNumberField(TEXT("expect")), T), FString::Printf(TEXT("height %s: got %.9g"), *C->GetStringField(TEXT("name")), H));
    }
    const TSharedPtr<FJsonObject> WorldData = Root->GetArrayField(TEXT("params"))[0]->AsObject()->GetObjectField(TEXT("data"));
    for (const auto& V : Root->GetArrayField(TEXT("swims")))
    {
        const auto C = V->AsObject(), In = C->GetObjectField(TEXT("input"));
        const auto W = EidoWater::ReadParams(WorldData);
        EidoWater::FSwimInput Input;
        Input.Direction = Vec(In, TEXT("direction"));
        In->TryGetBoolField(TEXT("fast"), Input.bFast);
        In->TryGetBoolField(TEXT("dive"), Input.bDive);
        In->TryGetBoolField(TEXT("rise"), Input.bRise);
        In->TryGetNumberField(TEXT("stature"), Input.Stature);
        EidoWater::FVec3 Pos = Vec(C, TEXT("start")), Vel;
        double Time = C->GetNumberField(TEXT("t0"));
        int32 Step = 0;
        for (const auto& S : C->GetArrayField(TEXT("steps")))
        {
            const auto Want = S->AsObject();
            const double Dt = Want->GetNumberField(TEXT("dt"));
            const auto R = EidoWater::SwimStep(Pos, Vel, Input, W, Dt, Time);
            Time += Dt;
            const bool bOk = NearVec(R.Pos, Vec(Want, TEXT("pos")), T) && NearVec(R.Velocity, Vec(Want, TEXT("velocity")), T)
                && R.bSurface == (Want->GetStringField(TEXT("mode")) == TEXT("surface"));
            Check(bOk, FString::Printf(TEXT("swim %s from (%g,%g,%g) step %d: pos (%.9g,%.9g,%.9g)"), *C->GetStringField(TEXT("name")),
                Vec(C, TEXT("start")).X, Vec(C, TEXT("start")).Y, Vec(C, TEXT("start")).Z, Step, R.Pos.X, R.Pos.Y, R.Pos.Z));
            // Continue from the reference so one divergence reports once, not forty times.
            Pos = Vec(Want, TEXT("pos")); Vel = Vec(Want, TEXT("velocity")); ++Step;
        }
    }
    // A half-size body floats half as deep, so its eyes clear the surface (tools/water/math-test.ts, review #248);
    // a missing or nonsense stature is a 100% body.
    {
        const auto W = EidoWater::ReadParams(WorldData);
        for (const double Stature : {1.0, .5, 2.0, 0.0, -1.0})
        {
            EidoWater::FSwimInput Input;
            Input.Direction = {0, 1, 0}; Input.bRise = true; Input.Stature = Stature;
            const double X = W.Center.X, Z = W.Center.Z;
            EidoWater::FVec3 Pos{X, W.Top - 4, Z}, Vel{0, 0, 0};
            EidoWater::FSwimResult R;
            double Time = 0;
            for (int32 I = 0; I < 300; ++I) { R = EidoWater::SwimStep(Pos, Vel, Input, W, 1. / 60, Time); Pos = R.Pos; Vel = R.Velocity; Time += 1. / 60; }
            const double Expect = EidoWater::WaveHeight(W, Pos.X, Pos.Z, Time - 1. / 60) - 1.1 * (Stature > 0 ? Stature : 1);
            Check(R.bSurface && Near(Pos.Y, Expect, 1e-8), FString::Printf(TEXT("stature %g floats at %.9g, want %.9g"), Stature, Pos.Y, Expect));
        }
    }
    UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_WATER_CONFORMANCE checked=%d failed=%d"), Checked, Failed);
    return TestTrue(TEXT("vectors present"), Checked > 400) && Failed == 0;
}
#endif
