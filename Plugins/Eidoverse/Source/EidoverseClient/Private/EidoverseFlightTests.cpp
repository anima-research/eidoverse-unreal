#include "EidoverseFlight.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseFlightConformance, "Eidoverse.FlightConformance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Replays Resources/Conformance/flight.json (Tools/eidoverse/flight_vectors.ts, from the
// shared JS flight law) through EidoFlight: pure functions, config validation, capability
// verdicts and per-step scenario trajectories, through both bare Step() and the
// controller-level StartFlight/TickFlight. flight.js uses no Math.random, so nothing is
// seeded.
namespace
{
    using namespace EidoFlight;
    using FJArr = TArray<TSharedPtr<FJsonValue>>;

    struct FCheck
    {
        FAutomationTestBase& Test;
        double Tolerance = 1e-6;
        int32 Checked = 0, Failed = 0;
        FString Where;

        void Fail(const FString& Msg) { if (++Failed <= 60) Test.AddError(Where + TEXT(": ") + Msg); }
        void Num(const TCHAR* What, double Got, double Want)
        {
            ++Checked;
            if (!(FMath::Abs(Got - Want) <= Tolerance * FMath::Max(1.0, FMath::Abs(Want))))
                Fail(FString::Printf(TEXT("%s got %.12g want %.12g"), What, Got, Want));
        }
        void Str(const TCHAR* What, const FString& Got, const FString& Want)
        {
            ++Checked;
            if (!Got.Equals(Want, ESearchCase::CaseSensitive)) Fail(FString::Printf(TEXT("%s got '%s' want '%s'"), What, *Got, *Want));
        }
        void Bool(const TCHAR* What, bool Got, bool Want)
        {
            ++Checked;
            if (Got != Want) Fail(FString::Printf(TEXT("%s got %d want %d"), What, Got, Want));
        }
        void Vec(const TCHAR* What, const FVec3& Got, const FJArr& Want)
        {
            Num(*FString::Printf(TEXT("%s.x"), What), Got.X, Want[0]->AsNumber());
            Num(*FString::Printf(TEXT("%s.y"), What), Got.Y, Want[1]->AsNumber());
            Num(*FString::Printf(TEXT("%s.z"), What), Got.Z, Want[2]->AsNumber());
        }
    };

    FString OptString(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
    {
        FString S;
        return O->TryGetStringField(Key, S) ? S : FString();
    }

    struct FSnap
    {
        FState S;
        bool bTick = false;
        double WorldYaw = 0, Speed = 0, Effort = 0;
        FString Clip;
        FControls Controls;
    };
    struct FRun
    {
        TMap<int32, FSnap> Samples;
        TArray<TPair<double, FString>> Events;
        bool bEnded = false, bReleased = false;
        int32 EndStep = -1;
        FFrameResult LastFrame;
        FString StartMsg;
    };

    void Log(FRun& Run, const TArray<FFlightEvent>& Events) { for (const FFlightEvent& E : Events) Run.Events.Emplace(E.T, E.Kind); }

    FRun Replay(FCheck& C, const TSharedPtr<FJsonObject>& Sc, double Dt, int32 SampleEvery)
    {
        FRun Run;
        FConfig Cfg;
        const TSharedPtr<FJsonObject>* Over = nullptr;
        if (Sc->TryGetObjectField(TEXT("config"), Over))
        {
            FString Err;
            if (!MakeConfig(*Over, Cfg, &Err)) C.Fail(TEXT("config rejected: ") + Err);
        }
        const bool bRaw = Sc->GetStringField(TEXT("mode")) == TEXT("raw");
        const double GroundY = Sc->GetNumberField(TEXT("groundY"));
        FVec3 Pos{0, GroundY, 0};
        double WorldYaw = 0;
        TOptional<double> Stamina;
        const TSharedPtr<FJsonObject>* Init = nullptr;
        if (Sc->TryGetObjectField(TEXT("init"), Init))
        {
            const FJArr* P = nullptr;
            if ((*Init)->TryGetArrayField(TEXT("pos"), P)) Pos = FVec3{(*P)[0]->AsNumber(), (*P)[1]->AsNumber(), (*P)[2]->AsNumber()};
            (*Init)->TryGetNumberField(TEXT("worldYaw"), WorldYaw);
            double St = 0;
            if ((*Init)->TryGetNumberField(TEXT("stamina"), St)) Stamina = St;
        }
        FState GroundState = InitialState(&Cfg);
        GroundState.Pos = Pos;
        GroundState.Yaw = UE_DOUBLE_PI / 2 - WorldYaw;
        if (Stamina.IsSet()) GroundState.Stamina = Stamina.GetValue();

        TOptional<FState> Flight;
        if (bRaw) Flight = GroundState;
        bool bGranted = true;
        FVerdict Granted;
        Granted.bEnabled = true;
        FVerdict Revoked;
        Revoked.Reason = TEXT("revoked by the scenario");

        const FJArr& Ops = Sc->GetArrayField(TEXT("ops"));
        const FJArr& Keys = Sc->GetArrayField(TEXT("keys"));
        const int32 N = FMath::FloorToInt(Sc->GetNumberField(TEXT("seconds")) / Dt + 0.5);
        for (int32 I = 0; I < N; ++I)
        {
            for (const auto& OV : Ops)
            {
                const auto Op = OV->AsObject();
                if ((int32)Op->GetNumberField(TEXT("step")) != I) continue;
                const FString Kind = Op->GetStringField(TEXT("op"));
                if (Kind == TEXT("takeoff"))
                {
                    const double Gy = Op->GetNumberField(TEXT("groundY"));
                    if (!bRaw && !Stamina.IsSet())
                    {
                        // The controller path proper: toggleFlight's take-off half.
                        Run.StartMsg = StartFlight(Cfg, Granted, false, Pos, WorldYaw, Gy, Flight);
                        if (Flight) Log(Run, Flight->Events);
                    }
                    else
                    {
                        FTakeOffOptions O;
                        O.GroundY = Gy;
                        Flight = TakeOff(Cfg, bRaw ? Flight.GetValue() : GroundState, O);
                        Log(Run, Flight->Events);
                        if (!bRaw && Flight->Phase != EPhase::Pilot) Flight.Reset();
                    }
                }
                else if (Kind == TEXT("down") && Flight)
                {
                    FDownEvent D;
                    D.EventId = OptString(Op, TEXT("eventId"));
                    Flight = BodyDown(*Flight, D);
                    Log(Run, Flight->Events);
                }
                else if (Kind == TEXT("recover") && Flight)
                {
                    FRecoveredEvent Rc;
                    Rc.EventId = OptString(Op, TEXT("eventId"));
                    Rc.RecoveryGeneration = OptString(Op, TEXT("generation"));
                    Flight = BodyRecovered(*Flight, Rc);
                    Log(Run, Flight->Events);
                }
                else if (Kind == TEXT("fold") && Flight) { Flight = FoldDown(Cfg, *Flight); Log(Run, Flight->Events); }
                else if (Kind == TEXT("unfold") && Flight) { Flight = Unfold(Cfg, *Flight); Log(Run, Flight->Events); }
                else if (Kind == TEXT("revoke")) bGranted = false;
            }
            if (!Flight) break;
            TSet<FString> Codes;
            for (const auto& KV : Keys)
            {
                const auto K = KV->AsObject();
                if (I >= (int32)K->GetNumberField(TEXT("from")) && I < (int32)K->GetNumberField(TEXT("to")))
                    for (const auto& CV : K->GetArrayField(TEXT("codes"))) Codes.Add(CV->AsString());
            }
            const FPilotInput Held = HeldFromCodes(Codes);
            const bool bSample = (I + 1) % SampleEvery == 0 || I + 1 == N;
            if (bRaw)
            {
                FEnv Env;
                Env.GroundY = [GroundY](double, double) { return GroundY; };
                Env.Input = PilotInput(Held, *Flight, Dt);
                Flight = Step(Cfg, *Flight, Dt, Env);
                Log(Run, Flight->Events);
                if (bSample) { FSnap Snap; Snap.S = *Flight; Run.Samples.Add(I + 1, Snap); }
                continue;
            }
            FState& Live = Flight.GetValue();
            const FFrameResult R = TickFlight(Cfg, Live, Held, Dt, GroundY, bGranted ? nullptr : &Revoked);
            Log(Run, R.Events);
            Run.LastFrame = R;
            if (R.bEnded && R.EndReason == TEXT("released")) { Run.bEnded = Run.bReleased = true; Run.EndStep = I; break; }
            if (bSample || R.bEnded)
            {
                FSnap Snap;
                Snap.S = Live;
                Snap.bTick = true;
                Snap.WorldYaw = R.WorldYaw; Snap.Speed = R.Speed; Snap.Effort = R.WingEffort; Snap.Clip = R.Clip; Snap.Controls = R.Controls;
                Run.Samples.Add(I + 1, Snap);
            }
            if (R.bEnded) { Run.bEnded = true; Run.EndStep = I + 1; Flight.Reset(); break; }
        }
        return Run;
    }

    void CompareScenario(FCheck& C, const TSharedPtr<FJsonObject>& Sc, const FRun& Run)
    {
        const FString Name = Sc->GetStringField(TEXT("name"));
        FString WantStart;
        if (Sc->TryGetStringField(TEXT("startMsg"), WantStart) && !Run.StartMsg.IsEmpty()) { C.Where = Name; C.Str(TEXT("startMsg"), Run.StartMsg, WantStart); }

        const FJArr& Samples = Sc->GetArrayField(TEXT("samples"));
        C.Where = Name;
        C.Num(TEXT("sample count"), Run.Samples.Num(), Samples.Num());
        for (const auto& SV : Samples)
        {
            const auto W = SV->AsObject();
            const int32 At = (int32)W->GetNumberField(TEXT("step"));
            C.Where = FString::Printf(TEXT("%s step %d"), *Name, At);
            const FSnap* Snap = Run.Samples.Find(At);
            if (!Snap) { C.Fail(TEXT("no C++ sample at this step")); continue; }
            const FState& S = Snap->S;
            C.Str(TEXT("phase"), PhaseName(S.Phase), W->GetStringField(TEXT("phase")));
            C.Str(TEXT("wings"), WingsName(S.Wings), W->GetStringField(TEXT("wings")));
            C.Str(TEXT("mode"), ModeName(S.Mode), W->GetStringField(TEXT("mode")));
            C.Str(TEXT("flownAs"), PhaseName(S.FlownAs), W->GetStringField(TEXT("flownAs")));
            C.Num(TEXT("t"), S.T, W->GetNumberField(TEXT("t")));
            C.Vec(TEXT("pos"), S.Pos, W->GetArrayField(TEXT("pos")));
            C.Vec(TEXT("vel"), S.Vel, W->GetArrayField(TEXT("vel")));
            C.Num(TEXT("yaw"), S.Yaw, W->GetNumberField(TEXT("yaw")));
            C.Num(TEXT("pitch"), S.Pitch, W->GetNumberField(TEXT("pitch")));
            C.Num(TEXT("bank"), S.Bank, W->GetNumberField(TEXT("bank")));
            C.Num(TEXT("airspeed"), S.Airspeed, W->GetNumberField(TEXT("airspeed")));
            C.Num(TEXT("stamina"), S.Stamina, W->GetNumberField(TEXT("stamina")));
            C.Num(TEXT("phaseT"), S.PhaseT, W->GetNumberField(TEXT("phaseT")));
            C.Num(TEXT("leafV0"), S.LeafV0, W->GetNumberField(TEXT("leafV0")));
            C.Num(TEXT("launchV"), S.LaunchV, W->GetNumberField(TEXT("launchV")));
            C.Num(TEXT("launchT"), S.LaunchT, W->GetNumberField(TEXT("launchT")));
            C.Num(TEXT("launchNow"), S.LaunchNow, W->GetNumberField(TEXT("launchNow")));
            double RecoverAt = 0;
            const bool bWantRecover = W->TryGetNumberField(TEXT("recoverAt"), RecoverAt);
            C.Bool(TEXT("recoverAt set"), S.RecoverAt.IsSet(), bWantRecover);
            if (bWantRecover && S.RecoverAt.IsSet()) C.Num(TEXT("recoverAt"), S.RecoverAt.GetValue(), RecoverAt);
            if (Snap->bTick)
            {
                C.Num(TEXT("worldYaw"), Snap->WorldYaw, W->GetNumberField(TEXT("worldYaw")));
                C.Num(TEXT("speed"), Snap->Speed, W->GetNumberField(TEXT("speed")));
                C.Num(TEXT("effort"), Snap->Effort, W->GetNumberField(TEXT("effort")));
                C.Str(TEXT("clip"), Snap->Clip, W->GetStringField(TEXT("clip")));
                const auto Ctl = W->GetObjectField(TEXT("controls"));
                C.Num(TEXT("controls.bank"), Snap->Controls.Bank, Ctl->GetNumberField(TEXT("bank")));
                C.Num(TEXT("controls.pitch"), Snap->Controls.Pitch, Ctl->GetNumberField(TEXT("pitch")));
                C.Num(TEXT("controls.yawRate"), Snap->Controls.YawRate, Ctl->GetNumberField(TEXT("yawRate")));
                C.Bool(TEXT("controls.flap"), Snap->Controls.bFlap, Ctl->GetBoolField(TEXT("flap")));
                C.Bool(TEXT("controls.spoil"), Snap->Controls.bSpoil, Ctl->GetBoolField(TEXT("spoil")));
            }
        }

        C.Where = Name + TEXT(" events");
        const FJArr& Events = Sc->GetArrayField(TEXT("events"));
        C.Num(TEXT("count"), Run.Events.Num(), Events.Num());
        for (int32 I = 0; I < FMath::Min(Events.Num(), Run.Events.Num()); ++I)
        {
            const auto E = Events[I]->AsObject();
            C.Str(*FString::Printf(TEXT("[%d] kind"), I), Run.Events[I].Value, E->GetStringField(TEXT("kind")));
            C.Num(*FString::Printf(TEXT("[%d] t"), I), Run.Events[I].Key, E->GetNumberField(TEXT("t")));
        }

        C.Where = Name + TEXT(" end");
        const TSharedPtr<FJsonObject>* End = nullptr;
        const bool bWantEnd = Sc->TryGetObjectField(TEXT("end"), End);
        C.Bool(TEXT("ended"), Run.bEnded, bWantEnd);
        if (!bWantEnd || !Run.bEnded) return;
        C.Num(TEXT("step"), Run.EndStep, (*End)->GetNumberField(TEXT("step")));
        bool bReleased = false;
        (*End)->TryGetBoolField(TEXT("released"), bReleased);
        C.Bool(TEXT("released"), Run.bReleased, bReleased);
        if (bReleased) return;
        const FFrameResult& R = Run.LastFrame;
        if (!R.End.IsSet()) { C.Fail(TEXT("no FFlightEnd")); return; }
        C.Str(TEXT("phase"), PhaseName(R.End->Phase), (*End)->GetStringField(TEXT("phase")));
        C.Str(TEXT("reason"), R.EndReason, (*End)->GetStringField(TEXT("phase")).ToLower());
        C.Num(TEXT("t"), R.End->T, (*End)->GetNumberField(TEXT("t")));
        C.Num(TEXT("y"), R.End->Y, (*End)->GetNumberField(TEXT("y")));
        C.Num(TEXT("ground"), R.End->Ground, (*End)->GetNumberField(TEXT("ground")));
        C.Bool(TEXT("early"), R.bEarlyEnd, (*End)->GetBoolField(TEXT("early")));
        FString Hint;
        if ((*End)->TryGetStringField(TEXT("hint"), Hint)) C.Str(TEXT("hint"), R.Hint, Hint);
    }

    void CheckFunctions(FCheck& C, const TSharedPtr<FJsonObject>& F)
    {
        const FConfig Cfg;
        C.Where = TEXT("bestGlide");
        const FBestGlide B = BestGlide(Cfg);
        const auto WB = F->GetObjectField(TEXT("bestGlide"));
        C.Num(TEXT("speed"), B.Speed, WB->GetNumberField(TEXT("speed")));
        C.Num(TEXT("ratio"), B.Ratio, WB->GetNumberField(TEXT("ratio")));
        C.Num(TEXT("sink"), B.Sink, WB->GetNumberField(TEXT("sink")));
        auto Pairs = [&](const TCHAR* Key, TFunctionRef<double(double)> Fn)
        {
            C.Where = Key;
            for (const auto& V : F->GetArrayField(Key))
            {
                const FJArr& P = V->AsArray();
                C.Num(*FString::Printf(TEXT("(%g)"), P[0]->AsNumber()), Fn(P[0]->AsNumber()), P[1]->AsNumber());
            }
        };
        Pairs(TEXT("sinkRate"), [&](double X) { return SinkRate(Cfg, X); });
        Pairs(TEXT("glideRatio"), [&](double X) { return GlideRatio(Cfg, X); });
        Pairs(TEXT("glideRange"), [&](double X) { return GlideRange(Cfg, X); });
        Pairs(TEXT("beatRemaining"), [&](double X) { return BeatRemaining(Cfg, X); });
        C.Where = TEXT("airspeedAfter");
        for (const auto& V : F->GetArrayField(TEXT("airspeedAfter")))
        {
            const FJArr& P = V->AsArray();
            C.Num(*FString::Printf(TEXT("(%g,%g)"), P[0]->AsNumber(), P[1]->AsNumber()),
                AirspeedAfter(Cfg, P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()), P[3]->AsNumber());
        }
        FConfig Heavy;
        ApplyLeafPreset(Heavy, *FindLeafPreset(TEXT("heavy")));
        for (const TCHAR* Key : {TEXT("leafAt"), TEXT("leafAtHeavy")})
        {
            const FConfig& LC = FCString::Strcmp(Key, TEXT("leafAt")) == 0 ? Cfg : Heavy;
            for (const auto& V : F->GetArrayField(Key))
            {
                const auto W = V->AsObject();
                const double T = W->GetNumberField(TEXT("t")), V0 = W->GetNumberField(TEXT("v0"));
                C.Where = FString::Printf(TEXT("%s(t=%g, v0=%g)"), Key, T, V0);
                const FLeafSample L = LeafAt(LC, T, V0);
                C.Num(TEXT("bank"), L.Bank, W->GetNumberField(TEXT("bank")));
                C.Num(TEXT("yawRate"), L.YawRate, W->GetNumberField(TEXT("yawRate")));
                C.Num(TEXT("pitch"), L.Pitch, W->GetNumberField(TEXT("pitch")));
                C.Num(TEXT("vy"), L.Vy, W->GetNumberField(TEXT("vy")));
                C.Num(TEXT("drift"), L.Drift, W->GetNumberField(TEXT("drift")));
                C.Num(TEXT("beatPhase"), L.BeatPhase, W->GetNumberField(TEXT("beatPhase")));
                C.Num(TEXT("envelope"), L.Envelope, W->GetNumberField(TEXT("envelope")));
            }
        }
        C.Where = TEXT("leafLateralSwing");
        C.Num(TEXT("default"), LeafLateralSwing(Cfg), F->GetNumberField(TEXT("leafLateralSwing")));
    }

    void CheckConfig(FCheck& C, const TSharedPtr<FJsonObject>& Root)
    {
        // Every key flight.js's DEFAULT_CONFIG has, the port has, with the same default.
        C.Where = TEXT("defaultConfig");
        const auto Want = Root->GetObjectField(TEXT("defaultConfig"));
        const auto Got = ConfigToJson(FConfig());
        FConfig Parsed;
        FString Err;
        C.Bool(TEXT("MakeConfig(DEFAULT_CONFIG) accepted"), MakeConfig(Want, Parsed, &Err), true);
        for (const auto& Pair : Want->Values)
        {
            const FString Key(*Pair.Key);
            if (Pair.Value->Type == EJson::Object)
            {
                const TSharedPtr<FJsonObject>* Sec = nullptr;
                if (!Got->TryGetObjectField(Key, Sec)) { C.Fail(TEXT("missing section ") + Key); continue; }
                for (const auto& P2 : Pair.Value->AsObject()->Values)
                {
                    const FString Key2(*P2.Key);
                    const FString Name = Key + TEXT(".") + Key2;
                    if (!(*Sec)->HasField(Key2)) { C.Fail(TEXT("missing key ") + Name); continue; }
                    if (P2.Value->Type == EJson::Boolean) C.Bool(*Name, (*Sec)->GetBoolField(Key2), P2.Value->AsBool());
                    else C.Num(*Name, (*Sec)->GetNumberField(Key2), P2.Value->AsNumber());
                }
            }
            else if (!Got->HasField(Key)) C.Fail(TEXT("missing key ") + Key);
            else C.Num(*Key, Got->GetNumberField(Key), Pair.Value->AsNumber());
        }
        for (const auto& V : Root->GetArrayField(TEXT("configCases")))
        {
            const auto Case = V->AsObject();
            FConfig Out;
            FString Why;
            const bool bOk = MakeConfig(Case->GetObjectField(TEXT("over")), Out, &Why);
            FString Js;
            Case->TryGetStringField(TEXT("error"), Js);
            C.Where = TEXT("makeConfig ") + Js;
            C.Bool(TEXT("ok"), bOk, Case->GetBoolField(TEXT("ok")));
        }
    }

    void CheckCapability(FCheck& C, const TSharedPtr<FJsonObject>& Root)
    {
        for (const auto& V : Root->GetArrayField(TEXT("capCases")))
        {
            const auto Case = V->AsObject();
            C.Where = TEXT("capability ") + Case->GetStringField(TEXT("name"));
            TOptional<bool> Fly;
            const TSharedPtr<FJsonObject>* Rights = nullptr;
            if (Case->TryGetObjectField(TEXT("rights"), Rights)) { bool B = false; (*Rights)->TryGetBoolField(TEXT("fly"), B); Fly = B; }
            TArray<FString> Bones;
            const FJArr* BV = nullptr;
            const bool bHasBones = Case->TryGetArrayField(TEXT("bones"), BV);
            if (bHasBones) for (const auto& X : *BV) Bones.Add(X->AsString());
            const FVerdict R = ResolveFlight(Fly, bHasBones ? &Bones : nullptr);
            C.Bool(TEXT("enabled"), R.bEnabled, Case->GetBoolField(TEXT("enabled")));
            FString S;
            if (Case->TryGetStringField(TEXT("reason"), S)) C.Str(TEXT("reason"), R.Reason, S);
            if (Case->TryGetStringField(TEXT("digest"), S)) C.Str(TEXT("digest"), R.Digest, S);
            double Wc = 0;
            if (Case->TryGetNumberField(TEXT("wingCount"), Wc)) C.Num(TEXT("wingCount"), R.WingCount, Wc);
            const FBodyReport Body = InspectBody(Bones);
            C.Bool(TEXT("canAnimateWings"), Body.bCanAnimateWings, Case->GetBoolField(TEXT("canAnimateWings")));
            C.Bool(TEXT("canFly"), Body.bCanFly, Case->GetBoolField(TEXT("canFly")));
            TArray<FString> Notes;
            for (const auto& X : Case->GetArrayField(TEXT("notes"))) Notes.Add(X->AsString());
            C.Str(TEXT("notes"), FString::Join(Body.Notes, TEXT(" | ")), FString::Join(Notes, TEXT(" | ")));
        }
    }

    void CheckReport(FCheck& C)
    {
        // flightReport() reads module state in the client, so it has no vectors; these are
        // its template filled by hand from controller.js.
        C.Where = TEXT("flightReport");
        FVerdict Ok;
        Ok.bEnabled = true;
        Ok.WingCount = 10;
        FState S;
        S.Phase = EPhase::Pilot;
        S.T = 3.04;
        S.Pos.Y = 12.3456;
        S.Airspeed = 6.37;
        S.Vel.Y = -0.5;
        S.Stamina = 80.5;
        C.Str(TEXT("flying"), FlightReport(Ok, 15, &S, 0, nullptr),
            TEXT("rig: 15 bones, granted (10 wing bones)\nstate: PILOT · t=3.0s · y=12.35 (ground 0.00)\n")
            TEXT("speed: 6.4 m/s air · vy=-0.50 · wings OPEN · stamina 81"));
        FVerdict No;
        No.Reason = TEXT("no world rights yet — not joined");
        FFlightEnd End;
        End.Phase = EPhase::Landed;
        End.T = 8.55;
        End.Y = -0.76;
        End.Ground = -0.76;
        C.Str(TEXT("landed"), FlightReport(No, 3, nullptr, 0, &End),
            TEXT("rig: 3 bones, DENIED — no world rights yet — not joined\n")
            TEXT("state: not flying · last flight ended LANDED at t=8.55s, y=-0.76 over ground -0.76"));
        C.Str(TEXT("none"), FlightReport(No, 0, nullptr, 0, nullptr),
            TEXT("rig: 0 bones, DENIED — no world rights yet — not joined\nstate: not flying · no flight this session"));
    }
}

bool FEidoverseFlightConformance::RunTest(const FString&)
{
    const FString Path = FPaths::ProjectPluginsDir() / TEXT("Eidoverse/Resources/Conformance/flight.json");
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!TestTrue(TEXT("vectors readable"), FFileHelper::LoadFileToString(Text, *Path) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())) return false;

    FCheck C{*this};
    C.Tolerance = Root->GetNumberField(TEXT("tolerance"));
    const double Dt = Root->GetNumberField(TEXT("dt"));
    const int32 SampleEvery = (int32)Root->GetNumberField(TEXT("sampleEvery"));
    CheckFunctions(C, Root->GetObjectField(TEXT("functions")));
    CheckConfig(C, Root);
    CheckCapability(C, Root);
    CheckReport(C);
    int32 Scenarios = 0;
    for (const auto& V : Root->GetArrayField(TEXT("scenarios")))
    {
        const auto Sc = V->AsObject();
        C.Where = Sc->GetStringField(TEXT("name"));
        const FRun Run = Replay(C, Sc, Dt, SampleEvery);
        CompareScenario(C, Sc, Run);
        ++Scenarios;
    }
    UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_FLIGHT_CONFORMANCE scenarios=%d checked=%d failed=%d"), Scenarios, C.Checked, C.Failed);
    return TestTrue(TEXT("vectors present"), Scenarios >= 10 && C.Checked > 1000) && C.Failed == 0;
}
#endif
