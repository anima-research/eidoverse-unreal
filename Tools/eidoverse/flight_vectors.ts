// flight_vectors -- reference trajectories of the shared flight law
// (eidoverse-worlds-native/shared/flight.js + flightpilot.js + flightcap.js)
// for the C++ port in Plugins/Eidoverse (EidoFlight, Eidoverse.FlightConformance).
//
//   BUN_RUNTIME_TRANSPILER_CACHE_PATH=0 bun Tools/eidoverse/flight_vectors.ts
//
// Every scenario is written to the JSON as DATA (ops and held keys per step
// range), so the C++ test replays the exact same inputs rather than a copy of
// this file's choreography. flight.js uses no Math.random and no clock, so the
// scenarios are deterministic as-is.
//
// Two loop modes:
//   tick  the client's per-frame flight branch (client/lib/controller.js): F
//         toggles flight via takeOff with the ground under the feet, then each
//         frame pilotInput -> step -> clip/wingEffort -> end on
//         LANDED/GROUND/RAGDOLL. Mirrored by EidoFlight::StartFlight/TickFlight.
//   raw   bare step() with pilotInput, never ending -- for the ground-side
//         state machine (ragdoll, sit-up, fold/unfold, refusals).

import {
  DEFAULT_CONFIG, LEAF_PRESETS, makeConfig, sinkRate, bestGlide, glideRatio, glideRange, airspeedAfter,
  leafAt, leafLateralSwing, beatRemaining, initialState, bodyDown, bodyRecovered, step,
  foldDown, unfold, takeOff,
} from '/Users/olena/connectome-local/eidoverse-worlds-native/shared/flight.js';
import { pilotInput } from '/Users/olena/connectome-local/eidoverse-worlds-native/shared/flightpilot.js';
import { worldFlightProvider, resolveFlight } from '/Users/olena/connectome-local/eidoverse-worlds-native/shared/flightcap.js';
import { inspectBody } from '/Users/olena/connectome-local/eidoverse-worlds-native/shared/flightbody.js';
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';

const OUT = '/Users/olena/Documents/Unreal Projects/Test1/Plugins/Eidoverse/Resources/Conformance/flight.json';
const DT = 1 / 60;
const SAMPLE_EVERY = 30;   // 0.5 s

type KeySpan = { from: number; to: number; codes: string[] };
type Op = { step: number; op: string; [k: string]: any };
type Scenario = {
  name: string; mode: 'tick' | 'raw'; seconds: number; groundY: number;
  config?: any; init?: { pos?: number[]; worldYaw?: number; stamina?: number; phase?: string; wings?: string };
  ops: Op[]; keys: KeySpan[];
};

const S = (sec: number) => Math.round(sec / DT);
const W = 'KeyW', Sd = 'KeyS', A = 'KeyA', D = 'KeyD', SP = 'Space', SH = 'ShiftLeft';

const scenarios: Scenario[] = [
  { name: 'glide_no_input', mode: 'tick', seconds: 6, groundY: 0, ops: [{ step: 0, op: 'takeoff', groundY: 0 }], keys: [] },
  { name: 'flap_then_glide', mode: 'tick', seconds: 9, groundY: 0, init: { worldYaw: 0.7 },
    ops: [{ step: 0, op: 'takeoff', groundY: 0 }], keys: [{ from: 0, to: S(3), codes: [SP] }] },
  { name: 'pitch_down_up', mode: 'tick', seconds: 10, groundY: 0, init: { pos: [3, 0, -4], worldYaw: -1.2 },
    ops: [{ step: 0, op: 'takeoff', groundY: 0 }],
    keys: [{ from: 0, to: S(3), codes: [SP] }, { from: S(3), to: S(4.5), codes: [W] },
           { from: S(4.5), to: S(7), codes: [Sd, SP] }] },
  { name: 'bank_turns', mode: 'tick', seconds: 10, groundY: 0, init: { worldYaw: 2.5 },
    ops: [{ step: 0, op: 'takeoff', groundY: 0 }],
    keys: [{ from: 0, to: S(10), codes: [SP] }, { from: S(1), to: S(3), codes: [A, SP] },
           { from: S(3), to: S(4), codes: [SP] }, { from: S(4), to: S(7), codes: ['ArrowRight', SP] },
           { from: S(7), to: S(7.5), codes: [A, D, SP] }] },
  { name: 'stamina_exhaustion', mode: 'tick', seconds: 12, groundY: 0, init: { stamina: 20 },
    ops: [{ step: 0, op: 'takeoff', groundY: 0 }], keys: [{ from: 0, to: S(12), codes: [SP] }] },
  // Launched off a 45 m perch over ground at 0, flapping the whole way: hits
  // the soft ceiling (52..60 m) and, flying straight from the origin, the
  // 110 m radius bounds.
  { name: 'ceiling_and_bounds', mode: 'tick', seconds: 24, groundY: 0, init: { pos: [0, 45, 0], worldYaw: 1.0 },
    ops: [{ step: 0, op: 'takeoff', groundY: 45 }], keys: [{ from: 0, to: S(24), codes: [SP] }] },
  { name: 'landing_spoil', mode: 'tick', seconds: 15, groundY: 0, init: { worldYaw: -0.3 },
    ops: [{ step: 0, op: 'takeoff', groundY: 0 }],
    keys: [{ from: S(3), to: S(15), codes: [SH] }, { from: S(3), to: S(4), codes: [SH, W] }] },
  // takeOff given a stale ground (0) over a 2 m floor: the "jumps and lands
  // immediately" bug the controller's early-landing hint exists to name.
  { name: 'early_landing', mode: 'tick', seconds: 4, groundY: 2,
    ops: [{ step: 0, op: 'takeoff', groundY: 0 }], keys: [{ from: 0, to: S(4), codes: [SH, W] }] },
  { name: 'raised_ground', mode: 'tick', seconds: 16, groundY: -0.76, init: { pos: [-25, -0.76, -65], worldYaw: 0.2 },
    ops: [{ step: 0, op: 'takeoff', groundY: -0.76 }], keys: [{ from: S(4), to: S(16), codes: [SH] }] },
  { name: 'leaf_descent', mode: 'tick', seconds: 25, groundY: 0, init: { worldYaw: 0.4 },
    ops: [{ step: 0, op: 'takeoff', groundY: 0 }, { step: S(6), op: 'down', eventId: 'cut-1' }],
    keys: [{ from: 0, to: S(6), codes: [SP] }, { from: S(6), to: S(25), codes: [A, SP] }] },
  { name: 'leaf_heavy_preset', mode: 'tick', seconds: 15, groundY: 0, config: { leaf: LEAF_PRESETS.heavy },
    ops: [{ step: 0, op: 'takeoff', groundY: 0 }, { step: S(6), op: 'down', eventId: 'cut-2' }],
    keys: [{ from: 0, to: S(6), codes: [SP] }] },
  { name: 'leaf_recover_airborne', mode: 'tick', seconds: 14, groundY: 0, init: { worldYaw: -2.0 },
    ops: [{ step: 0, op: 'takeoff', groundY: 0 }, { step: S(5), op: 'down', eventId: 'cut-3' },
          { step: S(6.3), op: 'recover', eventId: 'rec-3', generation: 'gen-7' }],
    keys: [{ from: 0, to: S(5), codes: [SP] }, { from: S(9), to: S(14), codes: [D] }] },
  { name: 'capability_revoked', mode: 'tick', seconds: 20, groundY: 0,
    ops: [{ step: 0, op: 'takeoff', groundY: 0 }, { step: S(4), op: 'revoke' }],
    keys: [{ from: 0, to: S(20), codes: [SP] }] },
  { name: 'tuned_config', mode: 'tick', seconds: 8, groundY: 0,
    config: { polar: { minSinkSpeed: 7, curvature: 2.5 }, pilot: { launchBoost: 6, flapClimb: 3 }, stamina: { pool: 40 } },
    ops: [{ step: 0, op: 'takeoff', groundY: 0 }], keys: [{ from: S(1), to: S(5), codes: [SP, W] }] },
  // Ground-side state machine, bare step().
  { name: 'raw_ground_ragdoll_situp', mode: 'raw', seconds: 8, groundY: 0, init: { stamina: 50 },
    ops: [{ step: S(0.5), op: 'down', eventId: 'trip' }, { step: S(1), op: 'down', eventId: 'again' },
          { step: S(2), op: 'recover', eventId: 'up', generation: 'g1' },
          { step: S(6), op: 'takeoff', groundY: 0 }],
    keys: [] },
  { name: 'raw_fold_refusals', mode: 'raw', seconds: 6, groundY: 0, init: { stamina: 30 },
    ops: [{ step: S(0.5), op: 'fold' }, { step: S(0.6), op: 'fold' }, { step: S(1), op: 'takeoff', groundY: 0 },
          { step: S(1.5), op: 'unfold' }, { step: S(1.6), op: 'unfold' }, { step: S(2), op: 'takeoff', groundY: 0 },
          { step: S(2.5), op: 'fold' }, { step: S(2.6), op: 'takeoff', groundY: 0 }],
    keys: [{ from: S(2), to: S(6), codes: [SP, D] }] },
];

const keysAt = (sc: Scenario, i: number) => {
  const set = new Set<string>();
  for (const k of sc.keys) if (i >= k.from && i < k.to) for (const c of k.codes) set.add(c);
  return set;
};

const r2 = (v: number) => +v.toFixed(2);

function sampleOf(i: number, s: any, extra: any = {}) {
  return {
    step: i, t: s.t, phase: s.phase, wings: s.wings, mode: s.mode, flownAs: s.flownAs,
    pos: [s.pos.x, s.pos.y, s.pos.z], vel: [s.vel.x, s.vel.y, s.vel.z],
    yaw: s.yaw, pitch: s.pitch, bank: s.bank, airspeed: s.airspeed, stamina: s.stamina,
    phaseT: s.phaseT, leafV0: s.leafV0, launchV: s.launchV, launchT: s.launchT ?? 0, launchNow: s.launchNow ?? 0,
    recoverAt: s.recoverAt ?? null,
    ...extra,
  };
}

function run(sc: Scenario) {
  const cfg = makeConfig(sc.config ?? {});
  const groundY = () => sc.groundY;
  const init = sc.init ?? {};
  const pos = init.pos ?? [0, sc.groundY, 0];
  const samples: any[] = [], events: any[] = [];
  const log = (i: number, s: any) => { for (const e of s.events ?? []) events.push({ step: i, t: e.t, kind: e.kind }); };
  let end: any = null, startMsg: string | null = null;
  let granted = true;
  const n = S(sc.seconds);

  // The body before flight. In tick mode this is toggleFlight's GROUND state
  // (world yaw converted to the integrator's convention at the boundary).
  let flight: any = null;
  let ground: any = initialState({
    phase: init.phase ?? 'GROUND', ...(init.wings ? { wings: init.wings } : {}),
    pos: { x: pos[0], y: pos[1], z: pos[2] }, yaw: Math.PI / 2 - (init.worldYaw ?? 0),
    ...(init.stamina != null ? { stamina: init.stamina } : {}),
  }, cfg);
  if (sc.mode === 'raw') flight = ground;

  for (let i = 0; i < n; i++) {
    for (const op of sc.ops.filter(o => o.step === i)) {
      if (op.op === 'takeoff') {
        if (sc.mode === 'tick') {
          // toggleFlight, granted and unfolded.
          const next = takeOff(cfg, ground, { groundY: op.groundY });
          log(i, next);
          if (next.phase !== 'PILOT') {
            const why = next.events?.find((ev: any) => ev.kind === 'takeoff.refused')?.reason ?? next.phase;
            startMsg = `cannot take off: ${why}`;
          } else { flight = next; startMsg = 'flying — W/S pitch, A/D bank, Shift spoil, Space flap, F to land'; }
        } else { flight = takeOff(cfg, flight, { groundY: op.groundY }); log(i, flight); }
      } else if (op.op === 'down') { if (flight) { flight = bodyDown(flight, { eventId: op.eventId }); log(i, flight); } }
      else if (op.op === 'recover') { if (flight) { flight = bodyRecovered(flight, { eventId: op.eventId, recoveryGeneration: op.generation }); log(i, flight); } }
      else if (op.op === 'fold') { flight = foldDown(cfg, flight); log(i, flight); }
      else if (op.op === 'unfold') { flight = unfold(cfg, flight); log(i, flight); }
      else if (op.op === 'revoke') granted = false;
    }
    if (!flight) break;
    const keys = keysAt(sc, i);
    if (sc.mode === 'raw') {
      const input = pilotInput(keys, flight, DT);
      flight = step(cfg, flight, DT, { groundY, input });
      log(i + 1, flight);
      if ((i + 1) % SAMPLE_EVERY === 0 || i + 1 === n) samples.push(sampleOf(i + 1, flight));
      continue;
    }
    // ---- controller.js per-frame flight branch
    if (flight.phase === 'GROUND') { end = { step: i, released: true }; flight = null; break; }
    if (flight.phase !== 'RAGDOLL' && !granted) {
      flight = bodyDown(flight, { eventId: 'capability-revoked' });
      log(i, flight);
    }
    const input = pilotInput(keys, flight, DT);
    flight = step(cfg, flight, DT, { groundY, input });
    log(i + 1, flight);
    const worldYaw = Math.PI / 2 - flight.yaw;
    let speed = Math.hypot(flight.vel.x, flight.vel.z);
    let clip = flight.wings === 'LIMP' ? 'ragdoll' : (input.flap ? 'fly' : 'soar');
    const boost = cfg.pilot?.launchBoost ?? 9.0;
    const fromLaunch = Math.min(1, (flight.launchNow ?? 0) / (boost * 0.55));
    let effort = input.flap ? 1 : fromLaunch;
    let ended = false;
    if (flight.phase === 'LANDED' || flight.phase === 'GROUND' || flight.phase === 'RAGDOLL') {
      const endedAt = r2(flight.t);
      const last = { phase: flight.phase, y: r2(flight.pos.y), ground: r2(groundY()), t: endedAt };
      end = { step: i + 1, ...last, early: endedAt < 1.5,
              hint: endedAt < 1.5 ? `flight ended after ${endedAt}s — landed at y=${last.y}, ground ${last.ground}` : null };
      clip = 'idle'; speed = 0; effort = 0; ended = true;
    }
    if ((i + 1) % SAMPLE_EVERY === 0 || i + 1 === n || ended) {
      samples.push(sampleOf(i + 1, flight, { worldYaw, speed, clip, effort,
        controls: { bank: input.bank, pitch: input.pitch, yawRate: input.yawRate, flap: input.flap, spoil: input.spoil } }));
    }
    if (ended) { flight = null; break; }
  }
  return { ...sc, startMsg, samples, events, end };
}

// ---- pure functions at fixed points
const cfgD = makeConfig();
const functions: any = {
  bestGlide: bestGlide(cfgD),
  sinkRate: [0, 2, 3, 4.5, 5.5, 6.37, 8, 12, 15, 20].map(v => [v, sinkRate(cfgD, v)]),
  glideRatio: [1, 3, 5.5, 6.4, 9, 15, 30].map(v => [v, glideRatio(cfgD, v)]),
  glideRange: [-5, 0, 10, 30, 60].map(h => [h, glideRange(cfgD, h)]),
  airspeedAfter: [[6, 0], [6, -0.5], [6, 0.7], [3.1, 0.7], [14.9, -0.7], [1, 0]].map(([v, p]) => [v, p, DT, airspeedAfter(cfgD, v, p, DT)]),
  leafAt: [0, 0.3, 1.1, 1.7, 3.4, 5.05, 9.9, 20].flatMap(t => [0, 4].map(v0 => ({ t, v0, ...leafAt(cfgD, t, v0) }))),
  leafAtHeavy: [0, 0.75, 2.2, 7].map(t => ({ t, v0: 1, ...leafAt(makeConfig({ leaf: LEAF_PRESETS.heavy }), t, 1) })),
  beatRemaining: [0, 0.5, 1.7, 2.0, 3.4, 4.1, 10.2, -0.3].map(t => [t, beatRemaining(cfgD, t)]),
  leafLateralSwing: leafLateralSwing(cfgD),
};

// ---- makeConfig validation (the C++ port must reject the same overrides)
const configCases = [
  { over: {}, ok: true },
  { over: { leaf: { damping: 0.1 } }, ok: false },
  { over: { wings: {} }, ok: false },
  { over: { leaf: { period: 0.01 } }, ok: false },
  { over: { polar: { minSinkSpeed: 2 } }, ok: false },
  { over: { bounds: { softMargin: 70 } }, ok: false },
  { over: { watchdogSec: 30, recover: { finishBeat: false }, leaf: { terminalV: 6 } }, ok: true },
].map(c => { try { makeConfig(c.over); return { ...c, ok: true }; } catch (e: any) { return { ...c, ok: false, error: String(e.message) }; } });

// ---- capability: rights.fly + rig
const rig = ['Hip', 'Spine01', 'Spine02', 'Head', 'NeckTwist01', 'L_Wing_Upper', 'L_Wing_Upper_1', 'L_Wing_Upper_2',
  'L_Wing_Lower', 'L_Wing_Lower_1', 'R_Wing_Upper', 'R_Wing_Upper_1', 'R_Wing_Lower', 'R_Wing_Lower_1', 'Hair_01'];
const capCases = [
  { name: 'granted', rights: { fly: true }, bones: rig },
  { name: 'vrm names', rights: { fly: true }, bones: ['hips', 'chest', 'head', 'L_Wing_Upper', 'L_Wing_Upper_1', 'R_Wing_Upper', 'R_Wing_Upper_1'] },
  { name: 'no rights', rights: null, bones: rig },
  { name: 'not granted', rights: { fly: false }, bones: rig },
  { name: 'no bones', rights: { fly: true }, bones: null },
  { name: 'commons avatar', rights: { fly: true }, bones: ['Hips', 'Spine', 'Head'] },
  { name: 'one chain', rights: { fly: true }, bones: ['Hip', 'Spine02', 'Head', 'L_Wing_Upper', 'L_Wing_Upper_1', 'L_Wing_Upper_2', 'L_Wing_Upper_3'] },
  { name: 'missing core', rights: { fly: true }, bones: rig.filter(b => b !== 'Head') },
].map(c => {
  const v: any = resolveFlight(worldFlightProvider({ rights: c.rights }), { identity: 'me', avatar: { boneNames: c.bones } });
  const body = inspectBody(c.bones ?? []);
  return { ...c, enabled: v.enabled, reason: v.reason ?? null, wingCount: v.profile?.wingCount ?? null,
           digest: v.profile?.digest ?? null, canAnimateWings: body.canAnimateWings, canFly: body.canFly, notes: body.notes };
});

const out = {
  source: 'eidoverse-worlds-native/shared/flight.js + flightpilot.js + flightcap.js + flightbody.js; loop mirrors client/lib/controller.js',
  generator: 'Tools/eidoverse/flight_vectors.ts',
  dt: DT, sampleEvery: SAMPLE_EVERY, tolerance: 1e-6,
  defaultConfig: DEFAULT_CONFIG,
  functions, configCases, capCases,
  scenarios: scenarios.map(run),
};
mkdirSync(dirname(OUT), { recursive: true });
writeFileSync(OUT, JSON.stringify(out, null, 1) + '\n');
for (const s of out.scenarios) {
  const last = s.samples[s.samples.length - 1];
  console.log(`${s.name.padEnd(26)} samples=${String(s.samples.length).padStart(3)} events=${s.events.map((e: any) => e.kind).join(',')}` +
              ` end=${s.end ? JSON.stringify(s.end) : '-'} last=${last?.phase} y=${last?.pos[1].toFixed(2)}`);
}
console.log(`wrote ${OUT}`);
