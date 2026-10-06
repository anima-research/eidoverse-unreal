#include "EidoverseRagdoll.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseRagdollConformance, "Eidoverse.RagdollConformance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    using namespace EidoRagdoll;
    using FJsonArr = TArray<TSharedPtr<FJsonValue>>;

    // The C++ math is a bit-faithful port (same operation order, no FMA contraction, same
    // libm, sin/cos never fused), and with the generator's 2^-12-quantized skeletons every
    // case but one replays to the last bit (measured error 0). The exception is the yawed
    // VRM0 case: the integrator hands in WORLD rest positions, so the rig's local offsets
    // come back through the inverse yaw and differ from three.js's by ~1e-16, which grows
    // to ~3e-9 m over its 214 frames. PosTolerance leaves ~30x headroom on that.
    // Pose components are streamed rounded to 4 decimals (toFixed(4)); a 1e-9 difference
    // can land on the other side of a rounding boundary, so one step is the pose bound.
    constexpr double PosTolerance = 1e-7;     // metres: particles and root
    constexpr double QuatTolerance = 1.0001e-4;
    constexpr int32 FrameSlack = 0;           // time-to-settle, frames (measured: identical)

    FVector Vec(const FJsonArr& A) { return FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()); }
    FQuat Quat(const FJsonArr& A) { return FQuat(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber(), A[3]->AsNumber()); }

    FInit ReadInit(const FJsonObject& In)
    {
        FInit Init;
        for (const auto& KV : In.GetObjectField(TEXT("rest"))->Values) Init.RestWorld.Add(FName(KV.Key), Vec(KV.Value->AsArray()));
        for (const auto& KV : In.GetObjectField(TEXT("parents"))->Values)
            if (KV.Value->Type == EJson::String) Init.Parents.Add(FName(KV.Key), FName(KV.Value->AsString()));
        Init.RootPosition = Vec(In.GetArrayField(TEXT("rootPos")));
        Init.RootYaw = In.GetNumberField(TEXT("rootYaw"));
        Init.bVrm0 = In.GetBoolField(TEXT("vrm0"));
        for (const auto& KV : In.GetObjectField(TEXT("startLocal"))->Values) Init.StartLocal.Add(FName(KV.Key), Quat(KV.Value->AsArray()));
        const TSharedPtr<FJsonValue> Lean = In.TryGetField(TEXT("lean"));
        if (Lean.IsValid() && Lean->Type == EJson::Array) Init.Lean = Vec(Lean->AsArray());
        return Init;
    }

    struct FCaseStats
    {
        double PosErr = 0, QuatErr = 0;
        int32 Samples = 0, FirstBadFrame = -1;
        FString FirstBad;
    };
}

bool FEidoverseRagdollConformance::RunTest(const FString&)
{
    const FString Path = FPaths::ProjectPluginsDir() / TEXT("Eidoverse/Resources/Conformance/ragdoll.json");
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!TestTrue(TEXT("vectors readable"), FFileHelper::LoadFileToString(Text, *Path) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())) return false;

    const double Dt = Root->GetNumberField(TEXT("dt"));
    int32 Cases = 0, Failed = 0;
    for (const auto& CV : Root->GetArrayField(TEXT("cases")))
    {
        const TSharedPtr<FJsonObject> C = CV->AsObject();
        const FString Name = C->GetStringField(TEXT("name"));
        const FInit Base = ReadInit(*C->GetObjectField(TEXT("input")));
        TUniquePtr<FRagdoll> Sim = MakeUnique<FRagdoll>(Base);
        ++Cases;

        TMultiMap<int32, TSharedPtr<FJsonObject>> Events;
        for (const auto& EV : C->GetArrayField(TEXT("events"))) Events.Add(int32(EV->AsObject()->GetNumberField(TEXT("frame"))), EV->AsObject());
        int32 HandoverFrame = -1;
        const TSharedPtr<FJsonObject>* Handover = nullptr;
        if (C->TryGetObjectField(TEXT("handover"), Handover)) HandoverFrame = int32((*Handover)->GetNumberField(TEXT("frame")));

        const FJsonArr& Samples = C->GetArrayField(TEXT("samples"));
        const int32 ExpectFrames = int32(C->GetNumberField(TEXT("frames")));
        FCaseStats St;
        int32 SampleIdx = 0, Frame = 0;
        const int32 MaxRun = ExpectFrames + 600;
        for (; Frame < MaxRun && !Sim->IsDone(); ++Frame)
        {
            TArray<TSharedPtr<FJsonObject>> Now;
            Events.MultiFind(Frame, Now, /*bMaintainOrder*/ true);
            for (const TSharedPtr<FJsonObject>& E : Now)
            {
                const FString Kind = E->GetStringField(TEXT("kind"));
                if (Kind == TEXT("impulse")) Sim->Impulse(Vec(E->GetArrayField(TEXT("v"))));
                else if (Kind == TEXT("pin")) Sim->PinJoint(FName(E->GetStringField(TEXT("joint"))), Vec(E->GetArrayField(TEXT("at"))));
                else if (Kind == TEXT("unpin")) Sim->Unpin(FName(E->GetStringField(TEXT("joint"))));
            }
            if (Frame == HandoverFrame)
            {
                // bodydrag release: the old sim's exact state seeds a new one, with the rest
                // re-measured at the root's current position (avatar.restBonePositions())
                FInit Next = Base;
                Next.Lean.Reset();
                Next.RootPosition = Sim->GetRootPosition();
                Next.RestWorld = Sim->RestPositionsAt(Next.RootPosition);
                Next.StartLocal = Sim->GetLocalRotations();
                Next.Seed = Sim->Snapshot();
                Sim = MakeUnique<FRagdoll>(Next);
            }
            const FStepResult& R = Sim->Step(Dt);
            if (Frame % 10 != 0 && !R.bDone) continue;
            if (!Samples.IsValidIndex(SampleIdx)) break;
            const TSharedPtr<FJsonObject> S = Samples[SampleIdx]->AsObject();
            if (int32(S->GetNumberField(TEXT("frame"))) != Frame) break;   // settle timing diverged; judged below
            ++SampleIdx;
            ++St.Samples;
            auto Note = [&](double Err, double Tol, const FString& What)
            {
                if (Err > Tol && St.FirstBadFrame < 0) { St.FirstBadFrame = Frame; St.FirstBad = FString::Printf(TEXT("%s err=%.3g"), *What, Err); }
            };
            const double RootErr = (R.RootPosition - Vec(S->GetArrayField(TEXT("root")))).GetAbsMax();
            St.PosErr = FMath::Max(St.PosErr, RootErr);
            Note(RootErr, PosTolerance, TEXT("root"));
            for (const auto& KV : S->GetObjectField(TEXT("p"))->Values)
            {
                FVector Got;
                if (!Sim->GetParticle(FName(KV.Key), Got)) { Note(1e9, PosTolerance, FString::Printf(TEXT("particle %s missing"), *KV.Key)); continue; }
                const double Err = (Got - Vec(KV.Value->AsArray())).GetAbsMax();
                St.PosErr = FMath::Max(St.PosErr, Err);
                Note(Err, PosTolerance, FString::Printf(TEXT("particle %s"), *KV.Key));
            }
            const TSharedPtr<FJsonObject>* Pose = nullptr;
            if (S->TryGetObjectField(TEXT("pose"), Pose))
            {
                if ((*Pose)->Values.Num() != R.Pose.Num()) Note(1e9, QuatTolerance, TEXT("pose bone count"));
                for (const auto& KV : (*Pose)->Values)
                {
                    const FQuat* Got = R.Pose.Find(FName(KV.Key));
                    if (!Got) { Note(1e9, QuatTolerance, FString::Printf(TEXT("pose %s missing"), *KV.Key)); continue; }
                    const FQuat W = Quat(KV.Value->AsArray());
                    const double Err = FMath::Max(FMath::Max(FMath::Abs(Got->X - W.X), FMath::Abs(Got->Y - W.Y)), FMath::Max(FMath::Abs(Got->Z - W.Z), FMath::Abs(Got->W - W.W)));
                    St.QuatErr = FMath::Max(St.QuatErr, Err);
                    Note(Err, QuatTolerance, FString::Printf(TEXT("pose %s"), *KV.Key));
                }
            }
            if (S->GetBoolField(TEXT("done")) != R.bDone) Note(1e9, 0, TEXT("done flag"));
        }
        // run on to the C++ sim's own capture so time-to-settle is comparable even after a divergence
        for (; Frame < MaxRun && !Sim->IsDone(); ++Frame) Sim->Step(Dt);

        const bool bTiming = Sim->IsDone() && FMath::Abs(Frame - ExpectFrames) <= FrameSlack;
        // the settled pose must match closely regardless of where per-sample checks stand
        const TSharedPtr<FJsonObject> Last = Samples.Last()->AsObject();
        double FinalQuatErr = 0, FinalPosErr = 0;
        for (const auto& KV : Last->GetObjectField(TEXT("pose"))->Values)
        {
            const FQuat* Got = Sim->LastResult().Pose.Find(FName(KV.Key));
            const FQuat W = Quat(KV.Value->AsArray());
            FinalQuatErr = FMath::Max(FinalQuatErr, Got ? FMath::Max(FMath::Max(FMath::Abs(Got->X - W.X), FMath::Abs(Got->Y - W.Y)), FMath::Max(FMath::Abs(Got->Z - W.Z), FMath::Abs(Got->W - W.W))) : 1e9);
        }
        FinalPosErr = (Sim->GetRootPosition() - Vec(Last->GetArrayField(TEXT("root")))).GetAbsMax();
        const bool bSamples = St.FirstBadFrame < 0 && SampleIdx == Samples.Num();
        const bool bFinal = FinalQuatErr <= QuatTolerance && FinalPosErr <= PosTolerance;
        const bool bOk = bTiming && bSamples && bFinal;
        UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_RAGDOLL %s frames=%d/%d samples=%d/%d maxPosErr=%.3g maxQuatErr=%.3g finalPosErr=%.3g finalQuatErr=%.3g %s"),
            *Name, Frame, ExpectFrames, SampleIdx, Samples.Num(), St.PosErr, St.QuatErr, FinalPosErr, FinalQuatErr, bOk ? TEXT("ok") : TEXT("FAIL"));
        if (!bOk)
        {
            ++Failed;
            AddError(FString::Printf(TEXT("%s: frames %d vs %d, samples %d/%d, first bad at frame %d (%s), final pose err %.3g root err %.3g"),
                *Name, Frame, ExpectFrames, SampleIdx, Samples.Num(), St.FirstBadFrame, *St.FirstBad, FinalQuatErr, FinalPosErr));
        }
    }
    UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_RAGDOLL_CONFORMANCE cases=%d failed=%d"), Cases, Failed);
    return TestTrue(TEXT("cases present"), Cases >= 6) && Failed == 0;
}
#endif
