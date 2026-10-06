#include "EidoverseFlight.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include <cmath>

// Port of eidoverse-worlds-native shared/flight.js, flightpilot.js, flightbody.js,
// flightcap.js and the flight branch of client/lib/controller.js. Arithmetic is kept in
// the JS evaluation order so trajectories agree to the last few ulps; see
// Eidoverse.FlightConformance.
namespace EidoFlight
{
namespace
{
    constexpr double Pi = 3.141592653589793;   // Math.PI

    double Clamp(double V, double Lo, double Hi) { return V < Lo ? Lo : V > Hi ? Hi : V; }
    double Sign(double V) { return V > 0 ? 1.0 : V < 0 ? -1.0 : V; }   // Math.sign
    double JsRound(double V) { return std::floor(V + 0.5); }           // Math.round

    // Number.prototype.toFixed, and `+x.toFixed(2)` printed back through String(number).
    FString ToFixed(double V, int32 Digits) { const double X = V == 0 ? 0.0 : V; return Digits == 1 ? FString::Printf(TEXT("%.1f"), X) : FString::Printf(TEXT("%.2f"), X); }
    double Round2(double V) { return FCString::Atod(*ToFixed(V, 2)); }
    FString JsNum2(double V)
    {
        if (V == 0) return TEXT("0");
        FString S = ToFixed(V, 2);
        if (S.Contains(TEXT(".")))
        {
            while (S.EndsWith(TEXT("0"))) S.LeftChopInline(1);
            if (S.EndsWith(TEXT("."))) S.LeftChopInline(1);
        }
        return S;
    }

    FFlightEvent Ev(double T, const TCHAR* Kind)
    {
        FFlightEvent E;
        E.T = T;
        E.Kind = Kind;
        return E;
    }
    void SetOptString(FFlightEvent& E, const TCHAR* Key, const TOptional<FString>& V) { if (V.IsSet()) E.Strings.Add(Key, V.GetValue()); }

    bool SamePolar(const FPolarConfig& A, const FPolarConfig& B)
    {
        return A.MinSinkSpeed == B.MinSinkSpeed && A.SinkAtMinSink == B.SinkAtMinSink && A.MinSpeed == B.MinSpeed
            && A.MaxSpeed == B.MaxSpeed && A.Curvature == B.Curvature;
    }

    // ---- config field table: flight.js key names -> FConfig members
    struct FField { const TCHAR* Section; const TCHAR* Key; double* Num; bool* Bool; };
    TArray<FField> Fields(FConfig& C)
    {
        return {
            {nullptr, TEXT("breath"), &C.BreathSec, nullptr},
            {TEXT("polar"), TEXT("minSinkSpeed"), &C.Polar.MinSinkSpeed, nullptr},
            {TEXT("polar"), TEXT("sinkAtMinSink"), &C.Polar.SinkAtMinSink, nullptr},
            {TEXT("polar"), TEXT("minSpeed"), &C.Polar.MinSpeed, nullptr},
            {TEXT("polar"), TEXT("maxSpeed"), &C.Polar.MaxSpeed, nullptr},
            {TEXT("polar"), TEXT("curvature"), &C.Polar.Curvature, nullptr},
            {TEXT("stamina"), TEXT("pool"), &C.Stamina.Pool, nullptr},
            {TEXT("stamina"), TEXT("climbPerMetre"), &C.Stamina.ClimbPerMetre, nullptr},
            {TEXT("stamina"), TEXT("flapSustainPerSec"), &C.Stamina.FlapSustainPerSec, nullptr},
            {TEXT("stamina"), TEXT("refillGroundPerSec"), &C.Stamina.RefillGroundPerSec, nullptr},
            {TEXT("stamina"), TEXT("refillPerchPerSec"), &C.Stamina.RefillPerchPerSec, nullptr},
            {TEXT("stamina"), TEXT("refillAirPerSec"), &C.Stamina.RefillAirPerSec, nullptr},
            {TEXT("leaf"), TEXT("period"), &C.Leaf.Period, nullptr},
            {TEXT("leaf"), TEXT("bankAmplitudeDeg"), &C.Leaf.BankAmplitudeDeg, nullptr},
            {TEXT("leaf"), TEXT("lateralAmplitudeM"), &C.Leaf.LateralAmplitudeM, nullptr},
            {TEXT("leaf"), TEXT("dampingPerCycle"), &C.Leaf.DampingPerCycle, nullptr},
            {TEXT("leaf"), TEXT("dampingFloor"), &C.Leaf.DampingFloor, nullptr},
            {TEXT("leaf"), TEXT("terminalV"), &C.Leaf.TerminalV, nullptr},
            {TEXT("leaf"), TEXT("spinUpTime"), &C.Leaf.SpinUpTime, nullptr},
            {TEXT("leaf"), TEXT("yawPerBank"), &C.Leaf.YawPerBank, nullptr},
            {TEXT("leaf"), TEXT("pitchCoupling"), &C.Leaf.PitchCoupling, nullptr},
            {TEXT("recover"), TEXT("finishBeat"), nullptr, &C.Recover.bFinishBeat},
            {TEXT("recover"), TEXT("reopenTime"), &C.Recover.ReopenTime, nullptr},
            {TEXT("recover"), TEXT("maxTotal"), &C.Recover.MaxTotal, nullptr},
            {TEXT("recover"), TEXT("minTotal"), &C.Recover.MinTotal, nullptr},
            {TEXT("bounds"), TEXT("ceiling"), &C.Bounds.Ceiling, nullptr},
            {TEXT("bounds"), TEXT("softMargin"), &C.Bounds.SoftMargin, nullptr},
            {TEXT("bounds"), TEXT("radius"), &C.Bounds.Radius, nullptr},
            {TEXT("bounds"), TEXT("groundClearance"), &C.Bounds.GroundClearance, nullptr},
            {TEXT("pilot"), TEXT("spoilSink"), &C.Pilot.SpoilSink, nullptr},
            {TEXT("pilot"), TEXT("flapClimb"), &C.Pilot.FlapClimb, nullptr},
            {TEXT("pilot"), TEXT("launchBoost"), &C.Pilot.LaunchBoost, nullptr},
            {nullptr, TEXT("watchdogSec"), &C.WatchdogSec, nullptr},
        };
    }
    bool SectionIs(const FField& F, const FString& Section) { return F.Section ? Section == F.Section : Section.IsEmpty(); }
}

const TCHAR* PhaseName(EPhase P)
{
    switch (P)
    {
    case EPhase::Ground: return TEXT("GROUND");
    case EPhase::Launch: return TEXT("LAUNCH");
    case EPhase::Glide: return TEXT("GLIDE");
    case EPhase::Climb: return TEXT("CLIMB");
    case EPhase::Circle: return TEXT("CIRCLE");
    case EPhase::Pilot: return TEXT("PILOT");
    case EPhase::Leaf: return TEXT("LEAF");
    case EPhase::Recover: return TEXT("RECOVER");
    case EPhase::Landed: return TEXT("LANDED");
    case EPhase::Ragdoll: return TEXT("RAGDOLL");
    }
    return TEXT("?");
}
const TCHAR* WingsName(EWings W) { return W == EWings::Open ? TEXT("OPEN") : W == EWings::Folded ? TEXT("FOLDED") : TEXT("LIMP"); }
const TCHAR* ModeName(EMode M) { return M == EMode::Live ? TEXT("live") : M == EMode::Plan ? TEXT("plan") : TEXT("reflex"); }

// ---------------------------------------------------------------- config

const FLeafPreset* FindLeafPreset(const FString& Name)
{
    // `spec` is the authored default. `heavy` is a real-weight fall that still wanders and
    // rolls; `brisk` sits between. The period shortens as the fall speeds up, deliberately:
    // at 12 m/s a 3.4s oscillation is one and a half swings from 30 m.
    static const FLeafPreset Spec{2.5, Breath, 0.8, 35, 0.9};
    static const FLeafPreset Brisk{6.5, 2.2, 1.1, 40, 0.8};
    static const FLeafPreset Heavy{12.0, 1.5, 1.4, 45, 0.7};
    if (Name == TEXT("spec")) return &Spec;
    if (Name == TEXT("brisk")) return &Brisk;
    if (Name == TEXT("heavy")) return &Heavy;
    return nullptr;
}

void ApplyLeafPreset(FConfig& Cfg, const FLeafPreset& P)
{
    Cfg.Leaf.TerminalV = P.TerminalV;
    Cfg.Leaf.Period = P.Period;
    Cfg.Leaf.LateralAmplitudeM = P.LateralAmplitudeM;
    Cfg.Leaf.BankAmplitudeDeg = P.BankAmplitudeDeg;
    Cfg.Leaf.SpinUpTime = P.SpinUpTime;
}

bool MakeConfig(const TSharedPtr<FJsonObject>& Over, FConfig& Out, FString* OutError)
{
    FConfig C;
    TArray<FField> Table = Fields(C);
    TArray<FString> Bad;
    if (Over.IsValid())
    {
        for (const auto& Pair : Over->Values)
        {
            const FString K(*Pair.Key);
            const TSharedPtr<FJsonValue>& V = Pair.Value;
            const FField* Top = Table.FindByPredicate([&](const FField& F) { return !F.Section && K == F.Key; });
            const bool bSection = Table.ContainsByPredicate([&](const FField& F) { return F.Section && K == F.Section; });
            if (!Top && !bSection) { Bad.Add(FString::Printf(TEXT("unknown section '%s'"), *K)); continue; }
            if (Top)
            {
                if (!V.IsValid() || V->Type != EJson::Number) { Bad.Add(FString::Printf(TEXT("'%s' must be a finite number"), *K)); continue; }
                *Top->Num = V->AsNumber();
                continue;
            }
            if (!V.IsValid() || V->Type != EJson::Object) { Bad.Add(FString::Printf(TEXT("'%s' must be an object"), *K)); continue; }
            for (const auto& P2 : V->AsObject()->Values)
            {
                const FString K2(*P2.Key);
                const FField* F = Table.FindByPredicate([&](const FField& X) { return SectionIs(X, K) && K2 == X.Key; });
                if (!F) { Bad.Add(FString::Printf(TEXT("unknown key '%s.%s'"), *K, *K2)); continue; }
                const TSharedPtr<FJsonValue>& V2 = P2.Value;
                if (F->Num)
                {
                    if (!V2.IsValid() || V2->Type != EJson::Number) { Bad.Add(FString::Printf(TEXT("'%s.%s' must be a finite number"), *K, *K2)); continue; }
                    *F->Num = V2->AsNumber();
                }
                else
                {
                    // Stricter than the JS (which stores any value here): a boolean or nothing.
                    if (!V2.IsValid() || V2->Type != EJson::Boolean) { Bad.Add(FString::Printf(TEXT("'%s.%s' must be a boolean"), *K, *K2)); continue; }
                    *F->Bool = V2->AsBool();
                }
            }
        }
    }
    // Ranges that would produce a nonsense body rather than a differently-tuned one.
    // Deliberately few: this rejects the impossible, not the unwise.
    struct FRange { const TCHAR* Name; double V, Lo, Hi; };
    const FRange R[] = {
        {TEXT("leaf.period"), C.Leaf.Period, 0.05, 60},
        {TEXT("leaf.dampingPerCycle"), C.Leaf.DampingPerCycle, 0, 0.999},
        {TEXT("leaf.dampingFloor"), C.Leaf.DampingFloor, 0, 1},
        {TEXT("leaf.terminalV"), C.Leaf.TerminalV, 0.01, 100},
        {TEXT("polar.minSpeed"), C.Polar.MinSpeed, 0.1, C.Polar.MaxSpeed},
        {TEXT("polar.minSinkSpeed"), C.Polar.MinSinkSpeed, C.Polar.MinSpeed, C.Polar.MaxSpeed},
        {TEXT("stamina.pool"), C.Stamina.Pool, 0, 1e6},
        {TEXT("bounds.softMargin"), C.Bounds.SoftMargin, 0.01, C.Bounds.Ceiling},
    };
    for (const FRange& X : R)
    {
        if (!(X.V >= X.Lo && X.V <= X.Hi))
            Bad.Add(FString::Printf(TEXT("'%s' = %s is outside [%s, %s]"), X.Name, *FString::SanitizeFloat(X.V), *FString::SanitizeFloat(X.Lo), *FString::SanitizeFloat(X.Hi)));
    }
    if (Bad.Num())
    {
        if (OutError) *OutError = TEXT("flight config: ") + FString::Join(Bad, TEXT("; "));
        return false;
    }
    Out = C;
    Out.BestCache.Reset();
    return true;
}

TSharedPtr<FJsonObject> ConfigToJson(const FConfig& Cfg)
{
    FConfig C = Cfg;
    auto Root = MakeShared<FJsonObject>();
    for (const FField& F : Fields(C))
    {
        TSharedPtr<FJsonObject> Into = Root;
        if (F.Section)
        {
            if (!Root->HasTypedField<EJson::Object>(F.Section)) Root->SetObjectField(F.Section, MakeShared<FJsonObject>());
            Into = Root->GetObjectField(F.Section);
        }
        if (F.Num) Into->SetNumberField(F.Key, *F.Num);
        else Into->SetBoolField(F.Key, *F.Bool);
    }
    return Root;
}

// ---------------------------------------------------------------- polar

double SinkRate(const FConfig& Cfg, double Airspeed)
{
    const FPolarConfig& P = Cfg.Polar;
    const double V = Clamp(Airspeed, P.MinSpeed, P.MaxSpeed);
    const double K = P.SinkAtMinSink / (P.MinSinkSpeed * P.MinSinkSpeed);
    const double D = V - P.MinSinkSpeed;
    return P.SinkAtMinSink + K * D * D * P.Curvature;
}

FBestGlide BestGlide(const FConfig& Cfg)
{
    if (Cfg.BestCache.IsSet() && SamePolar(Cfg.BestCacheFor, Cfg.Polar)) return Cfg.BestCache.GetValue();
    const FPolarConfig& P = Cfg.Polar;
    FBestGlide Best{P.MinSinkSpeed, 0, 0};
    // Coarse sweep then a bisection refine: the curve is smooth and unimodal in ratio, so
    // this lands on the true optimum rather than a grid point.
    for (double V = P.MinSpeed; V <= P.MaxSpeed; V += 0.01)
    {
        const double R = V / SinkRate(Cfg, V);
        if (R > Best.Ratio) Best = {V, R, SinkRate(Cfg, V)};
    }
    for (double StepSize = 0.005; StepSize > 1e-6; StepSize /= 2)
    {
        for (const double Dir : {-1.0, 1.0})
        {
            const double V = Clamp(Best.Speed + Dir * StepSize, P.MinSpeed, P.MaxSpeed);
            const double R = V / SinkRate(Cfg, V);
            if (R > Best.Ratio) Best = {V, R, SinkRate(Cfg, V)};
        }
    }
    Cfg.BestCache = Best;
    Cfg.BestCacheFor = P;
    return Best;
}

double GlideRatio(const FConfig& Cfg, double Airspeed)
{
    const double V = Clamp(Airspeed, Cfg.Polar.MinSpeed, Cfg.Polar.MaxSpeed);
    return V / SinkRate(Cfg, V);
}

double AirspeedAfter(const FConfig& Cfg, double Airspeed, double Pitch, double Dt)
{
    const FPolarConfig& P = Cfg.Polar;
    const double G = 9.81;
    const double Accel = -std::sin(Pitch) * G * 0.55;            // 0.55: drag eats the rest
    const double Toward = (P.MinSinkSpeed - Airspeed) * 0.25;    // drag pulls toward min sink
    const double V = Airspeed + (Accel + Toward) * Dt;
    return Clamp(V, P.MinSpeed * 0.6, P.MaxSpeed);
}

double GlideRange(const FConfig& Cfg, double Altitude) { return FMath::Max(0.0, Altitude) * BestGlide(Cfg).Ratio; }

// ---------------------------------------------------------------- leaf

FLeafSample LeafAt(const FConfig& Cfg, double T, double V0)
{
    const FLeafConfig& L = Cfg.Leaf;
    const double W = (2 * Pi) / L.Period;
    // DAMPING is authored per CYCLE and applied per second: a fraction f lost per cycle is
    // a rate of -ln(1-f)/period.
    const double RatePerSec = -std::log(FMath::Max(1e-9, 1 - L.DampingPerCycle)) / L.Period;
    const double Floor = L.DampingFloor;
    const double Env = Floor + (1 - Floor) * std::exp(-RatePerSec * T);
    FLeafSample S;
    S.Bank = (L.BankAmplitudeDeg * Pi / 180) * Env * std::sin(W * T);
    S.YawRate = L.YawPerBank * S.Bank;
    S.Pitch = L.PitchCoupling * S.Bank;
    // Vertical speed eases from whatever it was into terminal, continuous with the glide.
    const double K = 1 - std::exp(-T / FMath::Max(1e-3, L.SpinUpTime));
    S.Vy = -(std::abs(V0) + (L.TerminalV - std::abs(V0)) * K);
    // DRIFT is a velocity; the authored thing is the DISPLACEMENT A: peak speed A*w.
    S.Drift = L.LateralAmplitudeM * W * Env * std::cos(W * T);
    S.BeatPhase = std::fmod(std::fmod(T, L.Period) + L.Period, L.Period) / L.Period;
    S.Envelope = Env;
    return S;
}

double LeafLateralSwing(const FConfig& Cfg, double Cycles, double Dt)
{
    double X = 0, Lo = 0, Hi = 0;
    const int64 N = (int64)JsRound((Cfg.Leaf.Period * Cycles) / Dt);
    for (int64 I = 0; I < N; ++I)
    {
        X += LeafAt(Cfg, I * Dt, 0).Drift * Dt;
        if (X < Lo) Lo = X;
        if (X > Hi) Hi = X;
    }
    return Hi - Lo;
}

double BeatRemaining(const FConfig& Cfg, double T)
{
    const double Half = Cfg.Leaf.Period / 2;
    const double Since = std::fmod(std::fmod(T, Half) + Half, Half);
    return Since < 1e-9 ? 0 : Half - Since;
}

// ---------------------------------------------------------------- state

FState InitialState(const FConfig* Cfg)
{
    FState S;
    S.Stamina = Cfg ? Cfg->Stamina.Pool : FConfig().Stamina.Pool;
    return S;
}

// ---------------------------------------------------------------- events

FState BodyDown(const FState& State, const FDownEvent& In)
{
    FState S = State;
    S.Events.Reset();
    if (S.Phase == EPhase::Leaf || S.Phase == EPhase::Ragdoll) return S;   // already telling the truth
    S.DownEventId = In.EventId;
    S.LastEvent = FLastEvent{In.EventId, In.State};
    S.Wings = EWings::Limp;
    S.Mode = EMode::Reflex;
    // Airborne -> the leaf. Grounded -> ragdoll in place. Both involuntary; neither plays a
    // landing animation, ever. Remember who had the controls, so recovery can hand them back.
    if (S.Phase == EPhase::Pilot || S.Phase == EPhase::Glide) S.FlownAs = S.Phase;
    const bool bAirborne = S.Pos.Y > 0.5;
    S.Phase = bAirborne ? EPhase::Leaf : EPhase::Ragdoll;
    S.PhaseT = 0;
    S.LeafV0 = std::abs(S.Vel.Y);
    S.RecoverAt.Reset();
    S.RecoverPlan.Reset();
    FFlightEvent E = Ev(S.T, bAirborne ? TEXT("down.airborne") : TEXT("down.grounded"));
    SetOptString(E, TEXT("eventId"), In.EventId);
    E.Values.Add(TEXT("altitude"), S.Pos.Y);
    S.Events.Add(MoveTemp(E));
    return S;
}

FState BodyRecovered(const FState& State, const FRecoveredEvent& In)
{
    FState S = State;
    S.Events.Reset();
    if (S.Phase != EPhase::Leaf && S.Phase != EPhase::Ragdoll) return S;
    S.LastEvent = FLastEvent{In.EventId, TEXT("RECOVERED")};
    S.RecoveryGeneration = In.RecoveryGeneration;
    S.RecoverAt = S.T;
    if (S.Phase == EPhase::Ragdoll)
    {
        // Ground case: SIT UP first, taking the full breath (down-spec section 3).
        S.Phase = EPhase::Recover;
        S.PhaseT = 0;
        S.RecoverPlan = FRecoverPlan{0, Breath, true};
        FFlightEvent E = Ev(S.T, TEXT("recover.situp"));
        SetOptString(E, TEXT("eventId"), In.EventId);
        S.Events.Add(MoveTemp(E));
    }
    return S;   // airborne: the plan is built by Step(), which knows the beat phase
}

// ---------------------------------------------------------------- step
namespace
{
    double GroundOf(const FEnv& Env, double X, double Z) { return Env.GroundY ? Env.GroundY(X, Z) : 0.0; }

    double AngleTo(double From, double To)
    {
        double D = To - From;
        while (D > Pi) D -= 2 * Pi;
        while (D < -Pi) D += 2 * Pi;
        return D;
    }

    // R3 CEILING/BOUNDS: soft, banking you back, "never a wall-slam". Authoritative for every
    // airborne phase. A LIMP body is not steered (R2 owns it) -- altitude is only capped.
    void Bounds(const FConfig& Cfg, FState& S, double Dt)
    {
        const FBoundsConfig& B = Cfg.Bounds;
        const bool bLimp = S.Wings == EWings::Limp;
        const double R = std::hypot(S.Pos.X, S.Pos.Z);
        if (R > B.Radius && !bLimp)
        {
            const double Inward = std::atan2(-S.Pos.Z, -S.Pos.X);
            // Authority ramps in over the margin rather than snapping.
            const double Over = FMath::Min(1.0, (R - B.Radius) / FMath::Max(1e-6, B.SoftMargin));
            S.Yaw += AngleTo(S.Yaw, Inward) * FMath::Min(1.0, 1.5 * Over * Dt);
            if (!S.bBoundNote)
            {
                FFlightEvent E = Ev(S.T, TEXT("reflex.r3_bounds"));
                E.Values.Add(TEXT("radius"), R);
                S.Events.Add(MoveTemp(E));
                S.bBoundNote = true;
            }
        }
        else if (R <= B.Radius) S.bBoundNote = false;

        const double Soft = B.Ceiling - B.SoftMargin;
        if (S.Pos.Y > Soft && S.Vel.Y > 0 && !bLimp)
        {
            // Bleed the climb out across the margin; at the ceiling it is zero.
            const double K = FMath::Min(1.0, (S.Pos.Y - Soft) / FMath::Max(1e-6, B.SoftMargin));
            S.Vel.Y *= (1 - K);
            if (!S.bCeilNote)
            {
                FFlightEvent E = Ev(S.T, TEXT("reflex.r3_ceiling"));
                E.Values.Add(TEXT("altitude"), S.Pos.Y);
                S.Events.Add(MoveTemp(E));
                S.bCeilNote = true;
            }
        }
        else if (S.Pos.Y <= Soft) S.bCeilNote = false;
        // A hard stop remains only as a backstop for a body that got above the ceiling some
        // other way (a teleport, a bad initial state).
        if (S.Pos.Y > B.Ceiling) { S.Pos.Y = B.Ceiling; if (S.Vel.Y > 0) S.Vel.Y = 0; }
    }

    // Ground contact. NO AUTOLAND: a body in LEAF hits the ground as a ragdoll (T4).
    void GroundContact(const FConfig& Cfg, FState& S, const FEnv& Env, bool bRagdoll)
    {
        const double Gy = GroundOf(Env, S.Pos.X, S.Pos.Z);
        if (S.Pos.Y - Gy > Cfg.Bounds.GroundClearance) return;
        // Capture the impact BEFORE zeroing it.
        const double ImpactV = std::abs(S.Vel.Y);
        const double ImpactSpeed = std::hypot(S.Vel.X, S.Vel.Y, S.Vel.Z);
        S.Pos.Y = Gy;
        S.Vel = FVec3();
        if (bRagdoll)
        {
            S.Phase = EPhase::Ragdoll; S.Wings = EWings::Limp; S.Mode = EMode::Reflex;
            FFlightEvent E = Ev(S.T, TEXT("ground.ragdoll"));
            E.Values.Add(TEXT("impactV"), ImpactV);
            E.Values.Add(TEXT("impactSpeed"), ImpactSpeed);
            E.Values.Add(TEXT("entrySink"), S.LeafV0);
            SetOptString(E, TEXT("eventId"), S.DownEventId);
            S.Events.Add(MoveTemp(E));
        }
        else
        {
            S.Phase = EPhase::Landed; S.Mode = EMode::Live;
            FFlightEvent E = Ev(S.T, TEXT("ground.landed"));
            E.Values.Add(TEXT("impactV"), ImpactV);
            E.Values.Add(TEXT("impactSpeed"), ImpactSpeed);
            S.Events.Add(MoveTemp(E));
        }
        S.PhaseT = 0;
    }

    // Hand-flown flight: GLIDE's physics with a stick on it. A pilot and an agent fly the
    // same integrator.
    void StepPilot(const FConfig& Cfg, FState& S, double Dt, const FEnv& Env)
    {
        const FControls Inp = Env.Input.IsSet() ? Env.Input.GetValue() : FControls();
        S.Bank = Inp.Bank;
        S.Pitch = Inp.Pitch;
        S.Yaw += Inp.YawRate * Dt;

        // Airspeed is the pilot's to spend: nose down buys it, nose up sells it.
        S.Airspeed = AirspeedAfter(Cfg, S.Airspeed != 0 ? S.Airspeed : BestGlide(Cfg).Speed, S.Pitch, Dt);

        // R1 STALL RECOVERY, as a reflex and not as a punishment: the nose drops.
        if (S.Airspeed < Cfg.Polar.MinSpeed)
        {
            S.Pitch = FMath::Min(S.Pitch, -0.25);
            S.Airspeed = Cfg.Polar.MinSpeed;
            if (S.Mode != EMode::Reflex) S.Events.Add(Ev(S.T, TEXT("reflex.r1_stall")));
        }

        const double Lift = Env.Lift ? Env.Lift(S.Pos.X, S.Pos.Y, S.Pos.Z) : 0.0;
        double Sink = SinkRate(Cfg, S.Airspeed);
        if (Inp.bSpoil) Sink += Cfg.Pilot.SpoilSink;

        // Flapping is the expensive way to stay up (spec section 5: -2/s, "I am a glider").
        double Climb = 0;
        if (Inp.bFlap && S.Stamina > 0)
        {
            Climb = Cfg.Pilot.FlapClimb;
            S.Stamina = FMath::Max(0.0, S.Stamina - Cfg.Stamina.FlapSustainPerSec * Dt);
            if (S.Stamina == 0) S.Events.Add(Ev(S.T, TEXT("winded")));
        }

        // A LAUNCH BUILDS, THEN FADES: the impulse eases IN over the first beat or so as the
        // wings load, peaks, and bleeds away -- not a one-frame velocity, not a jump.
        if (S.LaunchV > 0.01 || S.LaunchT > 0)
        {
            S.LaunchT = S.LaunchT + Dt;
            const double Spool = 1 - std::exp(-S.LaunchT / 0.55);      // ~1.5s to full
            const double Fade = std::exp(-FMath::Max(0.0, S.LaunchT - 1.2) / 1.4);
            S.LaunchNow = S.LaunchV0 * Spool * Fade;
            if (S.LaunchNow < 0.02 && S.LaunchT > 1.2) { S.LaunchV = 0; S.LaunchT = 0; S.LaunchNow = 0; }
        }
        else S.LaunchNow = 0;
        S.Vel.Y = -Sink + Lift + Climb + S.LaunchNow;
        S.Vel.X = std::cos(S.Yaw) * S.Airspeed;
        S.Vel.Z = std::sin(S.Yaw) * S.Airspeed;
        S.Pos.X += S.Vel.X * Dt;
        S.Pos.Y += S.Vel.Y * Dt;
        S.Pos.Z += S.Vel.Z * Dt;

        Bounds(Cfg, S, Dt);
        GroundContact(Cfg, S, Env, /*bRagdoll=*/false);
    }

    void StepLeaf(const FConfig& Cfg, FState& S, double Dt, const FEnv& Env)
    {
        const FLeafSample A = LeafAt(Cfg, S.PhaseT, S.LeafV0);
        S.Bank = A.Bank; S.Pitch = A.Pitch;
        S.Yaw += A.YawRate * Dt;
        S.Vel.Y = A.Vy;
        // LATERAL means perpendicular to the heading: forward is (cos yaw, sin yaw), so
        // sideways is (-sin yaw, cos yaw). A leaf that wanders forwards is a glide.
        S.Vel.X = -std::sin(S.Yaw) * A.Drift;
        S.Vel.Z = std::cos(S.Yaw) * A.Drift;
        Bounds(Cfg, S, Dt);
        S.Pos.X += S.Vel.X * Dt;
        S.Pos.Y += S.Vel.Y * Dt;
        S.Pos.Z += S.Vel.Z * Dt;

        // A recovery request lands here: finish the beat, then reload the wings -- the WHOLE
        // transition clamped to the acceptance band (no teleport, no dawdle).
        if (S.RecoverAt.IsSet() && !S.RecoverPlan.IsSet())
        {
            const double Beat = Cfg.Recover.bFinishBeat ? BeatRemaining(Cfg, S.PhaseT) : 0;
            double Total = Beat + Cfg.Recover.ReopenTime;
            Total = Clamp(Total, Cfg.Recover.MinTotal, Cfg.Recover.MaxTotal);
            const double BeatEnds = FMath::Min(Beat, FMath::Max(0.0, Total - 0.05));
            S.RecoverPlan = FRecoverPlan{S.PhaseT + BeatEnds, S.PhaseT + Total, false};
            FFlightEvent E = Ev(S.T, TEXT("recover.begin"));
            E.Values.Add(TEXT("altitude"), S.Pos.Y);
            E.Values.Add(TEXT("beatWait"), BeatEnds);
            E.Values.Add(TEXT("total"), Total);
            S.Events.Add(MoveTemp(E));
        }
        if (S.RecoverPlan.IsSet() && S.PhaseT >= S.RecoverPlan->ReopenEnds)
        {
            // Wings reload; flight resumes. RETURN TO WHOEVER WAS FLYING (`flownAs`).
            S.Phase = S.FlownAs == EPhase::Pilot ? EPhase::Pilot : EPhase::Glide;
            S.Wings = EWings::Open; S.Mode = EMode::Live;
            S.Bank = 0; S.Pitch = 0;        // wings level on waking; the leaf's attitude was not yours
            S.PhaseT = 0; S.RecoverPlan.Reset();
            S.Airspeed = FMath::Max(Cfg.Polar.MinSpeed, BestGlide(Cfg).Speed * 0.8);
            FFlightEvent E = Ev(S.T, TEXT("recover.airborne"));
            E.Values.Add(TEXT("altitude"), S.Pos.Y);
            SetOptString(E, TEXT("recoveryGeneration"), S.RecoveryGeneration);
            S.Events.Add(MoveTemp(E));
        }

        GroundContact(Cfg, S, Env, /*bRagdoll=*/true);
    }

    void StepRecover(FState& S)
    {
        // Ground sit-up: the caller animates it; the phase is held so nothing else claims the
        // body mid-rise. Standing up puts you on your feet, not back in the air: takeOff() is
        // the door back to the sky.
        if (S.PhaseT >= (S.RecoverPlan.IsSet() ? S.RecoverPlan->ReopenEnds : Breath))
        {
            S.Phase = EPhase::Ground;
            S.Wings = EWings::Open; S.Mode = EMode::Live;
            S.Bank = 0; S.Pitch = 0;
            S.PhaseT = 0; S.RecoverPlan.Reset();
            S.Events.Add(Ev(S.T, TEXT("recover.stood")));
        }
    }

    void StepGlide(const FConfig& Cfg, FState& S, double Dt, const FEnv& Env)
    {
        const double Lift = Env.Lift ? Env.Lift(S.Pos.X, S.Pos.Y, S.Pos.Z) : 0.0;
        const double Speed = S.Airspeed != 0 ? S.Airspeed : BestGlide(Cfg).Speed;
        const double Sink = SinkRate(Cfg, Speed);
        S.Vel.Y = -Sink + Lift;
        S.Vel.X = std::cos(S.Yaw) * Speed;
        S.Vel.Z = std::sin(S.Yaw) * Speed;
        Bounds(Cfg, S, Dt);
        S.Pos.X += S.Vel.X * Dt;
        S.Pos.Y += S.Vel.Y * Dt;
        S.Pos.Z += S.Vel.Z * Dt;
        // Glide is stamina-neutral (spec section 5).
        GroundContact(Cfg, S, Env, /*bRagdoll=*/false);
    }

    void StepGround(const FConfig& Cfg, FState& S, double Dt)
    {
        const double R = Cfg.Stamina.RefillGroundPerSec;
        S.Stamina = FMath::Min(Cfg.Stamina.Pool, S.Stamina + R * Dt);
    }
}

FState Step(const FConfig& Cfg, const FState& State, double Dt, const FEnv& Env)
{
    FState S = State;
    S.Events.Reset();
    S.T += Dt;
    S.PhaseT += Dt;
    switch (S.Phase)
    {
    case EPhase::Leaf: StepLeaf(Cfg, S, Dt, Env); break;
    case EPhase::Recover: StepRecover(S); break;
    case EPhase::Glide: StepGlide(Cfg, S, Dt, Env); break;
    case EPhase::Pilot: StepPilot(Cfg, S, Dt, Env); break;
    case EPhase::Ragdoll: break;   // the ragdoll owns the body; nothing to integrate
    case EPhase::Ground:
    case EPhase::Landed: StepGround(Cfg, S, Dt); break;
    default: break;
    }
    return S;
}

FState FoldDown(const FConfig&, const FState& State)
{
    FState S = State;
    S.Events.Reset();
    auto Refuse = [&S](const FString& Why) { FFlightEvent E = Ev(S.T, TEXT("fold.refused")); E.Strings.Add(TEXT("reason"), Why); S.Events.Add(MoveTemp(E)); return S; };
    if (S.Phase != EPhase::Ground && S.Phase != EPhase::Landed) return Refuse(FString::Printf(TEXT("airborne (%s)"), PhaseName(S.Phase)));
    if (S.Wings == EWings::Limp) return Refuse(TEXT("wings limp"));
    if (S.Wings == EWings::Folded) return Refuse(TEXT("already folded"));
    S.Wings = EWings::Folded;
    S.Events.Add(Ev(S.T, TEXT("wings.folded")));
    return S;
}

FState Unfold(const FConfig&, const FState& State)
{
    FState S = State;
    S.Events.Reset();
    if (S.Wings != EWings::Folded)
    {
        FFlightEvent E = Ev(S.T, TEXT("unfold.refused"));
        E.Strings.Add(TEXT("reason"), FString::Printf(TEXT("wings are %s"), WingsName(S.Wings)));
        S.Events.Add(MoveTemp(E));
        return S;
    }
    S.Wings = EWings::Open;
    S.Events.Add(Ev(S.T, TEXT("wings.open")));
    return S;
}

FState TakeOff(const FConfig& Cfg, const FState& State, const FTakeOffOptions& Opts)
{
    FState S = State;
    S.Events.Reset();
    // The config's boost unless a caller overrides it.
    const double Boost = Opts.Boost.IsSet() ? Opts.Boost.GetValue() : Cfg.Pilot.LaunchBoost;
    auto Refuse = [&S](const FString& Why) { FFlightEvent E = Ev(S.T, TEXT("takeoff.refused")); E.Strings.Add(TEXT("reason"), Why); S.Events.Add(MoveTemp(E)); return S; };
    if (S.Wings == EWings::Folded) return Refuse(TEXT("wings folded"));
    if (S.Wings == EWings::Limp) return Refuse(TEXT("wings limp"));
    if (S.Phase != EPhase::Ground && S.Phase != EPhase::Landed) return Refuse(FString::Printf(TEXT("already %s"), PhaseName(S.Phase)));
    S.Phase = EPhase::Pilot; S.Mode = EMode::Live;
    S.Airspeed = Opts.LaunchSpeed.IsSet() ? Opts.LaunchSpeed.GetValue() : BestGlide(Cfg).Speed;
    // Clear the GROUND, not the world origin: the caller passes the height under her feet.
    S.Pos.Y = (Opts.GroundY.IsSet() ? Opts.GroundY.GetValue() : 0.0) + Cfg.Bounds.GroundClearance + 0.6;
    S.LaunchV = Boost; S.LaunchV0 = Boost; S.LaunchT = 0;   // shaped in the pilot step
    S.Bank = 0; S.Pitch = 0;
    S.PhaseT = 0;
    S.Stamina = FMath::Max(0.0, S.Stamina - Boost * Cfg.Stamina.ClimbPerMetre);
    FFlightEvent E = Ev(S.T, TEXT("took off"));
    E.Values.Add(TEXT("altitude"), S.Pos.Y);
    S.Events.Add(MoveTemp(E));
    return S;
}

// ---------------------------------------------------------------- pilot

FPilotInput HeldFromCodes(const TSet<FString>& Codes, const FBinds& Binds)
{
    auto Held = [&Codes](const TArray<FString>& List) { return List.ContainsByPredicate([&Codes](const FString& K) { return Codes.Contains(K); }); };
    FPilotInput In;
    In.bPitchDown = Held(Binds.PitchDown); In.bPitchUp = Held(Binds.PitchUp);
    In.bBankLeft = Held(Binds.BankLeft); In.bBankRight = Held(Binds.BankRight);
    In.bFlap = Held(Binds.Flap); In.bSpoil = Held(Binds.Spoil);
    In.bRehearseDown = Held(Binds.RehearseDown); In.bRehearseRecover = Held(Binds.RehearseRecover);
    return In;
}

FControls PilotInput(const FPilotInput& Held, const FState& State, double Dt, const FAuthority& A)
{
    double Bank = State.Bank, Pitch = State.Pitch;
    if (Held.bBankLeft && !Held.bBankRight) Bank -= A.BankRate * Dt;
    else if (Held.bBankRight && !Held.bBankLeft) Bank += A.BankRate * Dt;
    else
    {
        // Hands OFF the wings roll level. Hands ON they do not.
        const double Back = FMath::Min(std::abs(Bank), A.LevelReturn * Dt);
        Bank -= Sign(Bank) * Back;
    }
    Bank = Clamp(Bank, -A.MaxBank, A.MaxBank);

    if (Held.bPitchDown && !Held.bPitchUp) Pitch -= A.PitchRate * Dt;
    else if (Held.bPitchUp && !Held.bPitchDown) Pitch += A.PitchRate * Dt;
    else
    {
        const double Back = FMath::Min(std::abs(Pitch), A.LevelReturn * Dt);
        Pitch -= Sign(Pitch) * Back;
    }
    Pitch = Clamp(Pitch, -A.MaxPitch, A.MaxPitch);

    FControls C;
    C.Bank = Bank;
    C.Pitch = Pitch;
    C.YawRate = A.TurnPerBank * Bank;   // a banked wing turns; there is no rudder
    C.bFlap = Held.bFlap;
    C.bSpoil = Held.bSpoil;
    C.bEdgeDown = Held.bRehearseDown;
    C.bEdgeRecover = Held.bRehearseRecover;
    return C;
}

FString PilotHelp()
{
    return FString::Join(TArray<FString>{
        TEXT("W/S  nose down / up (trade altitude for speed)"),
        TEXT("A/D  bank left / right (a banked wing turns)"),
        TEXT("Shift  spoil -- bleed altitude without gaining speed"),
        TEXT("Space  flap (expensive: -2/s stamina, you are a glider)"),
        TEXT("X  rehearse DOWN   R  rehearse RECOVER"),
        TEXT("C  re-attach the chase camera (drag to look around)"),
    }, TEXT("\n"));
}

// ---------------------------------------------------------------- capability
namespace
{
    // WING_RE = /^([LR])_Wing_(Upper|Lower)(?:_(\d+))?$/ -> side, row, depth (-1: none).
    bool MatchWing(const FString& N, TCHAR& Side, FString& Row, int32& DepthIndex)
    {
        if (N.Len() < 12 || (N[0] != TEXT('L') && N[0] != TEXT('R'))) return false;
        if (!N.Mid(1, 6).Equals(TEXT("_Wing_"), ESearchCase::CaseSensitive)) return false;
        const FString R = N.Mid(7, 5);
        if (!R.Equals(TEXT("Upper"), ESearchCase::CaseSensitive) && !R.Equals(TEXT("Lower"), ESearchCase::CaseSensitive)) return false;
        const FString Rest = N.Mid(12);
        DepthIndex = -1;
        if (!Rest.IsEmpty())
        {
            if (Rest.Len() < 2 || Rest[0] != TEXT('_')) return false;
            for (int32 I = 1; I < Rest.Len(); ++I) if (Rest[I] < TEXT('0') || Rest[I] > TEXT('9')) return false;
            DepthIndex = (int32)FCString::Atod(*Rest.Mid(1));
        }
        Side = N[0];
        Row = R;
        return true;
    }
    int32 DepthOf(const FString& Name)
    {
        TCHAR Side; FString Row; int32 D = -1;
        return MatchWing(Name, Side, Row, D) && D >= 0 ? D : 0;
    }
    bool JsLess(const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; }   // Array.prototype.sort

    // A small, stable, dependency-free digest over the load-bearing names (change
    // detection, not security). UTF-16 code units, as charCodeAt sees them.
    FString DigestOf(const TArray<FString>& Parts)
    {
        const FString S = FString::Join(Parts, TEXT("|"));
        FTCHARToUTF16 U(*S);
        uint32 H1 = 0x811c9dc5u, H2 = 0x01000193u;
        for (int32 I = 0; I < U.Length(); ++I)
        {
            const uint32 C = (uint32)(uint16)U.Get()[I];
            H1 = (H1 ^ C) * 0x01000193u;
            H2 = (H2 + C + (uint32)I) * 0x85ebca6bu;
        }
        return FString::Printf(TEXT("rig1-%08x%08x"), H1, H2);
    }
}

FBodyReport InspectBody(const TArray<FString>& BoneNames)
{
    FBodyReport R;
    for (const FString& N : BoneNames)
    {
        TCHAR Side; FString Row; int32 D;
        if (!MatchWing(N, Side, Row, D)) continue;
        R.Chains.FindOrAdd(FString::Printf(TEXT("%c_%s"), Side, *Row)).Add(N);   // L_Upper, R_Lower, ...
    }
    // Sort each chain root-first. The INDEX in the name is the depth.
    for (auto& Pair : R.Chains) Pair.Value.StableSort([](const FString& A, const FString& B) { return DepthOf(A) < DepthOf(B); });
    for (const auto& Pair : R.Chains) R.WingCount += Pair.Value.Num();
    // CORE BONES BY ANY NAME: Tripo (Hip/Spine02), three-vrm (hips/chest), Mixamo (Hips/Spine),
    // matched case-insensitively over a set of spellings.
    static const TArray<TArray<FString>> Core = {
        {TEXT("hip"), TEXT("hips")},
        {TEXT("spine02"), TEXT("spine01"), TEXT("spine"), TEXT("chest"), TEXT("upperchest")},
        {TEXT("head")},
    };
    TSet<FString> Lower;
    for (const FString& N : BoneNames) Lower.Add(N.ToLower());
    for (const TArray<FString>& Alts : Core)
        if (!Alts.ContainsByPredicate([&Lower](const FString& X) { return Lower.Contains(X); })) R.Missing.Add(Alts[0]);

    R.bCanAnimateWings = R.Chains.Num() >= 2 && R.WingCount >= 4;
    if (!R.bCanAnimateWings)
    {
        R.Notes.Add(R.WingCount == 0
            ? FString(TEXT("no [LR]_Wing_* bones: physics will fly, wings will not animate"))
            : FString::Printf(TEXT("only %d wing bone(s) in %d chain(s); expected 4 chains (L/R x Upper/Lower)"), R.WingCount, R.Chains.Num()));
    }
    if (R.Missing.Num()) R.Notes.Add(TEXT("missing core bone(s): ") + FString::Join(R.Missing, TEXT(", ")));
    // Chains of differing depth are fine and worth SAYING.
    TArray<FString> Depths;
    for (const auto& Pair : R.Chains) Depths.AddUnique(FString::FromInt(Pair.Value.Num()));
    if (Depths.Num() > 1) R.Notes.Add(FString::Printf(TEXT("wing chains have uneven depth (%s)"), *FString::Join(Depths, TEXT("/"))));
    R.bCanFly = R.Missing.Num() == 0;   // the integrator needs a root, not feathers
    return R;
}

FString DescribeBody(const FBodyReport& R)
{
    if (!R.bCanFly) return FString::Printf(TEXT("NOT FLIGHT-CAPABLE: %s absent"), *FString::Join(R.Missing, TEXT(", ")));
    const FString Wings = R.bCanAnimateWings
        ? FString::Printf(TEXT("%d wing bones in %d chains"), R.WingCount, R.Chains.Num())
        : FString(TEXT("no animatable wings"));
    return FString::Printf(TEXT("flight-capable, %s%s"), *Wings, R.Notes.Num() ? *(TEXT(" -- ") + FString::Join(R.Notes, TEXT("; "))) : TEXT(""));
}

FVerdict ResolveFlight(TOptional<bool> RightsFly, const TArray<FString>* BoneNames, const FString& Label)
{
    FVerdict V;
    // A MISSING RIGHTS OBJECT IS A NO: "nobody has said you may" is the same answer as "no".
    if (!RightsFly.IsSet()) { V.Reason = TEXT("no world rights yet — not joined"); return V; }
    if (!RightsFly.GetValue()) { V.Reason = TEXT("this world has not granted you flight (owner: /grant <you> +fly)"); return V; }
    if (!BoneNames) { V.Reason = TEXT("no bone list available for this avatar"); return V; }
    // rigProfile: WINGS ARE REQUIRED FOR A CAPABILITY even though the physics does not need
    // them -- "could this body be integrated?" and "may this body take off?" differ.
    const FBodyReport R = InspectBody(*BoneNames);
    if (!R.bCanFly) { V.Reason = TEXT("not flight-capable: ") + DescribeBody(R); return V; }
    if (!R.bCanAnimateWings) { V.Reason = TEXT("no flight rig: ") + DescribeBody(R); return V; }
    static const TSet<FString> CoreAny = {TEXT("hip"), TEXT("hips"), TEXT("spine"), TEXT("spine01"), TEXT("spine02"),
        TEXT("chest"), TEXT("upperchest"), TEXT("head"), TEXT("neck"), TEXT("necktwist01")};
    TArray<FString> Wings, CoreBones;
    for (const auto& Pair : R.Chains) Wings.Append(Pair.Value);
    for (const FString& N : *BoneNames) if (CoreAny.Contains(N.ToLower())) CoreBones.Add(N);
    Wings.Sort(JsLess);
    CoreBones.Sort(JsLess);
    Wings.Append(CoreBones);
    V.bEnabled = true;
    V.WingCount = R.WingCount;
    V.Digest = DigestOf(Wings);
    V.Source = Label;
    return V;
}

// ---------------------------------------------------------------- controller

FString StartFlight(const FConfig& Cfg, const FVerdict& Capability, bool bWingsFolded,
    const FVec3& Pos, double WorldYaw, double GroundY, TOptional<FState>& OutFlight)
{
    OutFlight.Reset();
    static const FString NotArmed = TEXT("flight not armed for this body");
    if (!Capability.bEnabled && Capability.Reason == NotArmed) return NotArmed;
    // The spec's precondition, enforced where the human meets it.
    if (bWingsFolded) return TEXT("wings are folded — press G to unfold first");
    if (!Capability.bEnabled) return TEXT("no: ") + Capability.Reason;
    // ARM IT IN A LOCAL, PUBLISH IT ONCE: a toggle either flies or it does not.
    FState Next = InitialState(&Cfg);
    Next.Phase = EPhase::Ground;
    Next.Pos = Pos;
    // The world's yaw convention is atan2(dx,dz); the integrator's is atan2(dz,dx).
    Next.Yaw = Pi / 2 - WorldYaw;
    FTakeOffOptions Opts;
    Opts.GroundY = GroundY;   // THE GROUND UNDER HER FEET, not where she is
    Next = TakeOff(Cfg, Next, Opts);
    // A REFUSAL IS NOT A TAKE-OFF.
    if (Next.Phase != EPhase::Pilot)
    {
        FString Why = PhaseName(Next.Phase);
        for (const FFlightEvent& E : Next.Events)
            if (E.Kind == TEXT("takeoff.refused")) { if (const FString* R = E.Strings.Find(TEXT("reason"))) Why = *R; break; }
        return TEXT("cannot take off: ") + Why;
    }
    OutFlight = MoveTemp(Next);
    return TEXT("flying — W/S pitch, A/D bank, Shift spoil, Space flap, F to land");
}

FFrameResult TickFlight(const FConfig& Cfg, FState& State, const FPilotInput& Keys, double Dt, TFunction<double(double X, double Z)> GroundAt, const FVerdict* StillGranted)
{
    FFrameResult Out;
    auto Ground = [&GroundAt](double X, double Z) { return GroundAt ? GroundAt(X, Z) : 0.0; };
    // A GROUNDED FLIGHT DOES NOT OWN A BODY: release it rather than freeze the person.
    if (State.Phase == EPhase::Ground)
    {
        Out.Pos = State.Pos;
        Out.WorldYaw = Pi / 2 - State.Yaw;
        Out.Clip = TEXT("idle");
        Out.bEnded = true;
        Out.EndReason = TEXT("released");
        Out.Hint = TEXT("flight did not start — released");
        return Out;
    }
    // A REVOKED GRANT GROUNDS A BODY THAT IS ALREADY UP -- handed to the leaf, not deleted.
    if (State.Phase != EPhase::Ragdoll && StillGranted && !StillGranted->bEnabled)
    {
        Out.Hint = TEXT("flight withdrawn — ") + StillGranted->Reason;
        FDownEvent Down;
        Down.EventId = FString(TEXT("capability-revoked"));
        State = BodyDown(State, Down);
        Out.Events.Append(State.Events);
    }
    const FControls Input = PilotInput(Keys, State, Dt);
    FEnv Env;
    Env.GroundY = Ground;
    Env.Input = Input;
    State = Step(Cfg, State, Dt, Env);
    Out.Events.Append(State.Events);
    Out.Controls = Input;
    Out.Pos = State.Pos;
    Out.WorldYaw = Pi / 2 - State.Yaw;           // back to world convention
    Out.Speed = std::hypot(State.Vel.X, State.Vel.Z);
    Out.Clip = State.Wings == EWings::Limp ? TEXT("ragdoll") : Input.bFlap ? TEXT("fly") : TEXT("soar");
    // WINGS THAT WORK WHEN THE PHYSICS IS WORKING: effort is read off the flight state --
    // the shaped launch impulse (spools ~1.5s, fades by ~3s) or Space held -- normalised
    // against the config's own launch boost.
    const double Boost = Cfg.Pilot.LaunchBoost;
    const double FromLaunch = FMath::Min(1.0, State.LaunchNow / (Boost * 0.55));
    Out.WingEffort = Input.bFlap ? 1 : FromLaunch;
    if (State.Phase == EPhase::Landed || State.Phase == EPhase::Ground || State.Phase == EPhase::Ragdoll)
    {
        FFlightEnd End;
        End.Phase = State.Phase;
        End.T = Round2(State.T);
        End.Y = Round2(State.Pos.Y);
        End.Ground = Round2(Ground(State.Pos.X, State.Pos.Z));
        // A FLIGHT THAT ENDS IN ITS FIRST SECOND IS A BUG REPORTING ITSELF.
        Out.bEarlyEnd = End.T < 1.5;
        if (Out.bEarlyEnd)
            Out.Hint = FString::Printf(TEXT("flight ended after %ss — landed at y=%s, ground %s"), *JsNum2(End.T), *JsNum2(End.Y), *JsNum2(End.Ground));
        Out.End = End;
        Out.bEnded = true;
        Out.EndReason = FString(PhaseName(State.Phase)).ToLower();
        Out.Clip = TEXT("idle");
        Out.Speed = 0;
        Out.WingEffort = 0;   // back to the resting flap
    }
    return Out;
}

FFrameResult TickFlight(const FConfig& Cfg, FState& State, const FPilotInput& Keys, double Dt, double GroundY, const FVerdict* StillGranted)
{
    return TickFlight(Cfg, State, Keys, Dt, [GroundY](double, double) { return GroundY; }, StillGranted);
}

FString FlightReport(const FVerdict& Capability, int32 BoneCount, const FState* Flight, double GroundHere, const FFlightEnd* LastEnd)
{
    TArray<FString> L;
    L.Add(FString::Printf(TEXT("rig: %d bones, "), BoneCount) + (Capability.bEnabled
        ? FString::Printf(TEXT("granted (%d wing bones)"), Capability.WingCount)
        : TEXT("DENIED — ") + Capability.Reason));
    if (Flight)
    {
        L.Add(FString::Printf(TEXT("state: %s · t=%ss · y=%s (ground %s)"), PhaseName(Flight->Phase),
            *ToFixed(Flight->T, 1), *ToFixed(Flight->Pos.Y, 2), *ToFixed(GroundHere, 2)));
        L.Add(FString::Printf(TEXT("speed: %s m/s air · vy=%s · wings %s · stamina %d"), *ToFixed(Flight->Airspeed, 1),
            *ToFixed(Flight->Vel.Y, 2), WingsName(Flight->Wings), (int32)JsRound(Flight->Stamina)));
        // The freeze that started this: a phase that integrates nothing while holding the body.
        if (Flight->Phase == EPhase::Ground) L.Add(TEXT("!! GROUND while held — this is the freeze bug; press F"));
    }
    else
    {
        L.Add(TEXT("state: not flying") + (LastEnd
            ? FString::Printf(TEXT(" · last flight ended %s at t=%ss, y=%s over ground %s"), PhaseName(LastEnd->Phase),
                *JsNum2(LastEnd->T), *JsNum2(LastEnd->Y), *JsNum2(LastEnd->Ground))
            : FString(TEXT(" · no flight this session"))));
    }
    return FString::Join(L, TEXT("\n"));
}
}
