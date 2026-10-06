#pragma once
#include "CoreMinimal.h"

class FJsonObject;

// C++ port of the Eidoverse human-flight law (eidoverse-worlds-native):
//   shared/flight.js       the deterministic integrator: glide polar, falling leaf,
//                          stamina, and the state machine that decides which owns the body
//   shared/flightpilot.js  held keys -> stick inputs
//   shared/flightbody.js + shared/flightcap.js   "may this body fly": rights.fly + rig
//   client/lib/controller.js  the per-frame flight branch (StartFlight/TickFlight/FlightReport)
//
// WIRE space: metres, Y-up, right-handed; the caller converts to UE. The integrator's
// yaw is atan2(dz, dx) (forward = (cos yaw, 0, sin yaw)); the world's is atan2(dx, dz).
// worldYaw = PI/2 - flightYaw, both ways, at the boundary only (as controller.js does).
//
// PURE: no clock, no randomness (flight.js uses no Math.random), no UObjects. The caller
// owns every piece of state; functions return a new state rather than mutating it, as
// the JS does, so a replay from a snapshot is exact. Must reproduce
// Resources/Conformance/flight.json; see Eidoverse.FlightConformance.
namespace EidoFlight
{
    /** Seconds. The house's breath (shared/breath.js): wing idle, stamina tick and the
     *  leaf oscillation all beat on it (spec section 5, section 2, T8). */
    constexpr double Breath = 3.4;
    constexpr double BreathHz = 1.0 / Breath;

    enum class EPhase : uint8 { Ground, Launch, Glide, Climb, Circle, Pilot, Leaf, Recover, Landed, Ragdoll };
    enum class EWings : uint8 { Open, Folded, Limp };
    enum class EMode : uint8 { Live, Plan, Reflex };
    EIDOVERSECLIENT_API const TCHAR* PhaseName(EPhase P);   // "GROUND", "PILOT", ... (the JS strings)
    EIDOVERSECLIENT_API const TCHAR* WingsName(EWings W);   // "OPEN" | "FOLDED" | "LIMP"
    EIDOVERSECLIENT_API const TCHAR* ModeName(EMode M);     // "live" | "plan" | "reflex"

    struct FVec3 { double X = 0, Y = 0, Z = 0; };

    // ---------------------------------------------------------------- config
    // CONFIG, NOT CONSTANTS: everything a reviewer might tune, with the spec's value as
    // its default (flight.js DEFAULT_CONFIG; the comments there are the authority).
    struct FPolarConfig
    {
        // Sink rate as a function of airspeed: a parabola about minimum sink. A polar has
        // TWO optima: minSinkSpeed (slowest altitude loss, circle a thermal here) and best
        // glide (furthest per metre lost), which is always faster and is DERIVED from the
        // curve by BestGlide(), never asserted. Halved from 11 m/s ("the flying speed is
        // super fast"): 5.5 min sink, best glide ~6.4 m/s, glide ratio untouched at ~12.9.
        double MinSinkSpeed = 5.5;        // m/s -- minimum sink. The shape parameter.
        double SinkAtMinSink = 5.5 / 12;  // m/s at that speed
        double MinSpeed = 3;              // stall (spec R1 triggers below this)
        double MaxSpeed = 15;             // never-exceed; drag rises steeply past best
        double Curvature = 3;             // how sharply sink rises either side of min-sink
    };
    struct FStaminaConfig   // "shaped like breath" (spec section 5)
    {
        double Pool = 100;
        double ClimbPerMetre = 1;         // -1/m
        double FlapSustainPerSec = 2;     // -2/s, deliberately expensive: I am a glider
        double RefillGroundPerSec = 0.5;
        double RefillPerchPerSec = 2;     // SPEC ONLY -- no perch detection, no runtime consumer (Stage 2)
        double RefillAirPerSec = 0;       // never refills airborne
    };
    struct FLeafConfig      // R2 falling leaf (spec section 3 R2, down-spec section 3 airborne case)
    {
        // A leaf is not a crash: a slow spiral, survivable by design, and it must LAND --
        // no flare, no autoland, no last-metre mercy hover (T4). UNITS ARE NAMED: an
        // ATTITUDE amplitude (degrees of roll) and a PATH amplitude (metres of wander).
        double Period = Breath;           // s -- one full oscillation
        double BankAmplitudeDeg = 35;     // DEGREES of roll at peak. Attitude, not path.
        double LateralAmplitudeM = 0.8;   // METRES from centreline at peak (=1.6 m peak-to-peak)
        double DampingPerCycle = 0.08;    // FRACTION of swing lost per CYCLE; per-second rate derived in LeafAt
        double DampingFloor = 0.35;       // the swing decays TOWARD this fraction, not to zero
        // TERMINAL VELOCITY IS CONTESTED: the spec says 2-3 m/s (slower than a parachute);
        // Janus found it "way too slow". Default stays the spec's; LeafPresets make the
        // alternative one word to try.
        double TerminalV = 2.5;           // m/s downward. Spec says 2-3.
        double SpinUpTime = 0.9;          // s to reach terminalV from whatever v it had
        double YawPerBank = 0.55;         // rad/s of yaw per radian of bank -- the spiral
        double PitchCoupling = 0.35;      // how much bank leaks into pitch; 0 = pure roll
    };
    struct FRecoverConfig   // down-spec section 3: "the aerial sit-up" -- not instant, never over one breath
    {
        bool bFinishBeat = true;          // ride the current oscillation to its zero crossing
        double ReopenTime = 1.1;          // s of visible wing reload after the beat ends
        double MaxTotal = Breath;         // hard ceiling on beat + reopen (acceptance band)
        double MinTotal = 0.35;           // floor: anything faster reads as a teleport
    };
    struct FBoundsConfig    // spec R3
    {
        double Ceiling = 60;              // m; soft -- banks you back, never a wall-slam
        double SoftMargin = 8;            // m of authority band below the ceiling
        double Radius = 110;              // m from origin; circumscribes the ~160 m square terrain
        double GroundClearance = 0.15;    // m; below this counts as ground contact
    };
    struct FPilotConfig     // the stick, when a person is holding it
    {
        double SpoilSink = 2.5;           // m/s of extra sink with the spoilers out (Shift)
        double FlapClimb = 2.2;           // m/s of climb while Space is held; costs stamina
        double LaunchBoost = 9.0;         // m/s launch impulse, SHAPED over ~1.5s in the pilot step
    };
    struct FBestGlide { double Speed = 0, Ratio = 0, Sink = 0; };

    struct EIDOVERSECLIENT_API FConfig
    {
        double BreathSec = Breath;        // `breath` -- reported; the integrator reads the constant, as flight.js does
        FPolarConfig Polar;
        FStaminaConfig Stamina;
        FLeafConfig Leaf;
        FRecoverConfig Recover;
        FBoundsConfig Bounds;
        FPilotConfig Pilot;
        double WatchdogSec = 90;          // spec R2 / open question Q1: proposal as default

        // bestGlide's memo (the JS keys a WeakMap by config object). Recomputed whenever
        // the polar it was computed from changes.
        mutable TOptional<FBestGlide> BestCache;
        mutable FPolarConfig BestCacheFor;
    };

    /** LEAF_PRESETS: named leaf characters ("spec", "brisk", "heavy"), so "too slow" is a
     *  word rather than a patch. Each overwrites these five leaf fields. */
    struct FLeafPreset { double TerminalV, Period, LateralAmplitudeM, BankAmplitudeDeg, SpinUpTime; };
    EIDOVERSECLIENT_API const FLeafPreset* FindLeafPreset(const FString& Name);
    EIDOVERSECLIENT_API void ApplyLeafPreset(FConfig& Cfg, const FLeafPreset& P);

    /** makeConfig(over): deep-merge a partial config (JSON, flight.js key names) over the
     *  defaults, REJECTING unknown sections/keys, non-finite or mistyped values and the
     *  impossible ranges. Returns false and fills OutError ("flight config: ...") on reject. */
    EIDOVERSECLIENT_API bool MakeConfig(const TSharedPtr<FJsonObject>& Over, FConfig& Out, FString* OutError = nullptr);
    inline FConfig MakeConfig() { return FConfig(); }
    /** The effective config as JSON with flight.js key names (diagnostics, conformance). */
    EIDOVERSECLIENT_API TSharedPtr<FJsonObject> ConfigToJson(const FConfig& Cfg);

    // ---------------------------------------------------------------- polar
    /** Sink rate (m/s, positive = descending), airspeed clamped to [minSpeed, maxSpeed]. */
    EIDOVERSECLIENT_API double SinkRate(const FConfig& Cfg, double Airspeed);
    /** The polar's ACTUAL best-glide point, found by searching the curve (memoised). */
    EIDOVERSECLIENT_API FBestGlide BestGlide(const FConfig& Cfg);
    /** Glide ratio (metres forward per metre down) at an airspeed. */
    EIDOVERSECLIENT_API double GlideRatio(const FConfig& Cfg, double Airspeed);
    /** Airspeed after one frame at a pitch: nose down buys speed, nose up sells it. */
    EIDOVERSECLIENT_API double AirspeedAfter(const FConfig& Cfg, double Airspeed, double Pitch, double Dt);
    /** How far this altitude carries you at best glide, in metres (spec T2). */
    EIDOVERSECLIENT_API double GlideRange(const FConfig& Cfg, double Altitude);

    // ---------------------------------------------------------------- leaf
    struct FLeafSample
    {
        double Bank = 0, YawRate = 0, Pitch = 0;   // rad, rad/s, rad
        double Vy = 0;                             // m/s, negative = down
        double Drift = 0;                          // m/s lateral
        double BeatPhase = 0;                      // 0..1 through the current oscillation
        double Envelope = 0;
    };
    /** Attitude and velocity of a falling-leaf descent at elapsed time T (pure in t). */
    EIDOVERSECLIENT_API FLeafSample LeafAt(const FConfig& Cfg, double T, double V0 = 0);
    /** Peak-to-peak lateral wander of the leaf's PATH over its first Cycles cycles. */
    EIDOVERSECLIENT_API double LeafLateralSwing(const FConfig& Cfg, double Cycles = 3, double Dt = 1.0 / 120);
    /** Seconds from T until the current oscillation next crosses zero bank, in [0, period/2). */
    EIDOVERSECLIENT_API double BeatRemaining(const FConfig& Cfg, double T);

    // ---------------------------------------------------------------- state
    /** One emitted event ({t, kind, ...} in the JS). Optional payload fields are named in
     *  Values (altitude, impactV, impactSpeed, entrySink, beatWait, total, radius) and
     *  Strings (reason, eventId, recoveryGeneration). */
    struct FFlightEvent
    {
        double T = 0;
        FString Kind;
        TMap<FString, double> Values;
        TMap<FString, FString> Strings;
    };
    struct FRecoverPlan { double BeatEnds = 0, ReopenEnds = 0; bool bGround = false; };
    struct FLastEvent { TOptional<FString> EventId; FString Kind; };

    struct EIDOVERSECLIENT_API FState
    {
        EPhase Phase = EPhase::Ground;
        EWings Wings = EWings::Open;
        EMode Mode = EMode::Live;          // supplied by the CALLER (spec section 4)
        double T = 0;                      // seconds since the world's flight epoch
        FVec3 Pos, Vel;
        double Yaw = 0, Pitch = 0, Bank = 0;   // integrator yaw convention (see top)
        double Airspeed = 0;
        double Stamina = 100;
        // --- phase-local clocks, all derived from t so a snapshot is complete
        double PhaseT = 0;                 // seconds in the current phase
        double LeafV0 = 0;                 // sink rate at the moment LEAF began
        TOptional<double> RecoverAt;       // t at which RECOVER was requested
        TOptional<FRecoverPlan> RecoverPlan;
        // --- provenance, so a log line can always say WHY the body did that
        TOptional<FLastEvent> LastEvent;   // last trusted event
        EPhase FlownAs = EPhase::Pilot;    // the phase a cut interrupted; recovery restores it
        double LaunchV = 0;                // launch impulse (takeOff); shaped by LaunchT
        double LaunchV0 = 0, LaunchT = 0, LaunchNow = 0;
        TOptional<FString> DownEventId;
        TOptional<FString> RecoveryGeneration;
        bool bBoundNote = false, bCeilNote = false;   // _boundNote/_ceilNote: R3 events once per excursion
        TArray<FFlightEvent> Events;             // emitted this step; caller drains
    };

    /** initialState(over, cfg): a fresh GROUND state; the stamina pool comes from the
     *  effective config when one is supplied. Set any "over" fields on the result. */
    EIDOVERSECLIENT_API FState InitialState(const FConfig* Cfg = nullptr);

    // ---------------------------------------------------------------- events
    struct FDownEvent { TOptional<FString> EventId; FString State = TEXT("DOWN"); };
    struct FRecoveredEvent { TOptional<FString> EventId; TOptional<FString> RecoveryGeneration; };
    /** Involuntary. Airborne (y > 0.5) -> LEAF; grounded -> RAGDOLL in place. */
    EIDOVERSECLIENT_API FState BodyDown(const FState& State, const FDownEvent& Ev = FDownEvent());
    /** Capability is back. Ground: RECOVER (sit up, one breath) -> GROUND. Airborne: the
     *  recovery plan (finish the beat, reload the wings) is built by the next Step. */
    EIDOVERSECLIENT_API FState BodyRecovered(const FState& State, const FRecoveredEvent& Ev = FRecoveredEvent());

    // ---------------------------------------------------------------- step
    /** The stick: what flightpilot.pilotInput returns and stepPilot consumes. */
    struct FControls
    {
        double Bank = 0, Pitch = 0, YawRate = 0;
        bool bFlap = false, bSpoil = false;
        bool bEdgeDown = false, bEdgeRecover = false;   // `edges` (rehearsal keys held); caller debounces
    };
    struct FEnv
    {
        TFunction<double(double X, double Z)> GroundY;      // unset: ground at y = 0
        TFunction<double(double X, double Y, double Z)> Lift; // unset: no lift
        TOptional<FControls> Input;                          // unset: stick centred
    };
    /** One fixed timestep. Deterministic: same (cfg, state, dt, env) in, same state out. */
    EIDOVERSECLIENT_API FState Step(const FConfig& Cfg, const FState& State, double Dt, const FEnv& Env = FEnv());

    /** fold_down: ground only; FOLDED grounds the flier ("the vigil posture costs the sky"). */
    EIDOVERSECLIENT_API FState FoldDown(const FConfig& Cfg, const FState& State);
    /** The explicit act that ends the vigil. */
    EIDOVERSECLIENT_API FState Unfold(const FConfig& Cfg, const FState& State);
    struct FTakeOffOptions { TOptional<double> LaunchSpeed, Boost, GroundY; };
    /** take_off: GROUND/LANDED with OPEN wings -> PILOT at groundY + clearance + 0.6, with a
     *  shaped launch impulse; costs boost * climbPerMetre stamina. Refusals return the
     *  input phase plus a `takeoff.refused` event (reason "wings folded" | "wings limp" |
     *  "already PHASE"). */
    EIDOVERSECLIENT_API FState TakeOff(const FConfig& Cfg, const FState& State, const FTakeOffOptions& Opts = FTakeOffOptions());

    // ---------------------------------------------------------------- pilot (flightpilot.js)
    /** Held keys, already resolved through the bindings. DEFAULT_BINDS (JS key codes):
     *  pitchDown W/ArrowUp, pitchUp S/ArrowDown, bankLeft A/ArrowLeft, bankRight D/ArrowRight,
     *  flap Space, spoil ShiftLeft/ShiftRight, rehearseDown X, rehearseRecover R. */
    struct FPilotInput
    {
        bool bPitchDown = false, bPitchUp = false, bBankLeft = false, bBankRight = false;
        bool bFlap = false, bSpoil = false, bRehearseDown = false, bRehearseRecover = false;
    };
    struct FBinds
    {
        TArray<FString> PitchDown{TEXT("KeyW"), TEXT("ArrowUp")}, PitchUp{TEXT("KeyS"), TEXT("ArrowDown")};
        TArray<FString> BankLeft{TEXT("KeyA"), TEXT("ArrowLeft")}, BankRight{TEXT("KeyD"), TEXT("ArrowRight")};
        TArray<FString> Flap{TEXT("Space")}, Spoil{TEXT("ShiftLeft"), TEXT("ShiftRight")};
        TArray<FString> RehearseDown{TEXT("KeyX")}, RehearseRecover{TEXT("KeyR")};
    };
    /** Resolve a set of held DOM-style key codes ("KeyW", "Space", ...) through bindings. */
    EIDOVERSECLIENT_API FPilotInput HeldFromCodes(const TSet<FString>& Codes, const FBinds& Binds = FBinds());
    /** DEFAULT_AUTHORITY: control authority per second at full deflection. */
    struct FAuthority
    {
        double BankRate = 2.6;      // rad/s toward the commanded bank
        double MaxBank = 1.15;      // ~66deg
        double PitchRate = 1.5;     // rad/s
        double MaxPitch = 0.7;      // ~40deg
        double TurnPerBank = 1.5;   // rad/s of yaw per radian of bank -- a banked wing turns
        double LevelReturn = 1.1;   // rad/s back to level -- ONLY with the stick centred
    };
    /** pilotInput(keys, state, dt): one frame of piloting from the state's bank/pitch. */
    EIDOVERSECLIENT_API FControls PilotInput(const FPilotInput& Held, const FState& State, double Dt, const FAuthority& A = FAuthority());
    /** pilotHelp(): the HUD key hint. */
    EIDOVERSECLIENT_API FString PilotHelp();

    // ---------------------------------------------------------------- capability (flightbody.js + flightcap.js)
    struct FBodyReport
    {
        bool bCanFly = false, bCanAnimateWings = false;
        TMap<FString, TArray<FString>> Chains;   // "L_Upper" -> root-first bone names
        int32 WingCount = 0;
        TArray<FString> Missing, Notes;
    };
    /** inspectBody(boneNames): wing chains ([LR]_Wing_(Upper|Lower)[_n]) and core bones (any vocabulary). */
    EIDOVERSECLIENT_API FBodyReport InspectBody(const TArray<FString>& BoneNames);
    EIDOVERSECLIENT_API FString DescribeBody(const FBodyReport& R);
    /** resolveFlight(worldFlightProvider({rights}), {avatar:{boneNames}}): BOTH must say yes --
     *  rights.fly (server-authoritative grant; unset = not joined yet) and a rig with core
     *  bones and >= 2 wing chains / >= 4 wing bones. Re-resolve at every action, never cache. */
    struct FVerdict
    {
        bool bEnabled = false;
        FString Reason;                          // when denied
        int32 WingCount = 0;                     // profile.wingCount when granted
        FString Digest, Source;                  // profile.digest ("rig1-..."), provider label
    };
    EIDOVERSECLIENT_API FVerdict ResolveFlight(TOptional<bool> RightsFly, const TArray<FString>* BoneNames, const FString& Label = TEXT("world-grant"));

    // ---------------------------------------------------------------- controller (client/lib/controller.js)
    /** toggleFlight's take-off half. Pos/WorldYaw are the walking body's (wire space, world
     *  yaw convention); GroundY is the resolved floor under the feet (NOT Pos.Y). On success
     *  OutFlight is set and the message is the flying hint; otherwise OutFlight is left unset
     *  and the message says why (folded, denied, refused). Landing by F is the caller simply
     *  dropping the state (message "landed"; wingEffort 0). Stamina carries nothing across
     *  flights, as in the client (each take-off builds a fresh state at the config's pool). */
    EIDOVERSECLIENT_API FString StartFlight(const FConfig& Cfg, const FVerdict& Capability, bool bWingsFolded,
        const FVec3& Pos, double WorldYaw, double GroundY, TOptional<FState>& OutFlight);

    struct FFlightEnd { EPhase Phase = EPhase::Landed; double T = 0, Y = 0, Ground = 0; };   // lastFlightEnd (2-dp rounded)
    struct FFrameResult
    {
        FVec3 Pos;                 // wire space; apply to the body this frame
        double WorldYaw = 0;       // world convention: PI/2 - flight yaw
        double Speed = 0;          // horizontal m/s, for the clip rate
        FString Clip;              // "fly" (Space held) | "soar" | "ragdoll" (wings LIMP) | "idle" (ended)
        double WingEffort = 0;     // 0..1: 1 while flapping, else the launch envelope / (boost*0.55)
        FControls Controls;        // this frame's stick (bank/pitch for posing; edges)
        bool bEnded = false;       // drop the flight state; walking resumes (grounded, vy = 0)
        FString EndReason;         // "landed" | "ground" | "ragdoll" | "released" (phase was GROUND) | "" ; see Hint
        TOptional<FFlightEnd> End; // lastFlightEnd, for FlightReport
        bool bEarlyEnd = false;    // ended before 1.5 s of flight: a bug reporting itself (Hint carries the numbers)
        FString Hint;              // flashHint text when the controller shows one (revoked, early landing, released)
        TArray<FFlightEvent> Events;     // the step's events, plus a capability-revoked bodyDown's
    };
    /** One frame of controller.js's flight branch: GROUND-state release, a withdrawn grant
     *  hands the body to the leaf (StillGranted re-resolved this frame and denied -> bodyDown
     *  'capability-revoked'; nullptr = still granted),
     *  pilotInput -> step with the ground -> pose/clip/effort -> end on LANDED/GROUND/RAGDOLL.
     *  GroundY is the floor under the flier (the JS probes at the flier's altitude). Swimming
     *  cancels flight in the client: the caller drops the state when it enters swimming. */
    EIDOVERSECLIENT_API FFrameResult TickFlight(const FConfig& Cfg, FState& State, const FPilotInput& Keys, double Dt, double GroundY, const FVerdict* StillGranted = nullptr);
    EIDOVERSECLIENT_API FFrameResult TickFlight(const FConfig& Cfg, FState& State, const FPilotInput& Keys, double Dt, TFunction<double(double X, double Z)> GroundAt, const FVerdict* StillGranted = nullptr);

    /** flightReport(): the /flight diagnostic, line for line. Flight null = not flying;
     *  GroundHere is the floor under the flier (ignored when not flying). */
    EIDOVERSECLIENT_API FString FlightReport(const FVerdict& Capability, int32 BoneCount, const FState* Flight, double GroundHere, const FFlightEnd* LastEnd);
}
