// Reference vectors for the C++ Verlet ragdoll port (EidoverseRagdoll.cpp):
// runs eidoverse-worlds-native/client/lib/ragdoll.js headlessly on synthetic
// rest skeletons, flat ground (heightAt = 0, no colliders), fixed dt 1/60, and
// writes Plugins/Eidoverse/Resources/Conformance/ragdoll.json, which the
// Eidoverse.RagdollConformance automation test replays.
//
//   BUN_RUNTIME_TRANSPILER_CACHE_PATH=0 bun Tools/eidoverse/ragdoll_vectors.ts
//   (EIDOVERSE_REPO=<path> overrides ~/connectome-local/eidoverse-worlds-native)
//
// Nothing in the eidoverse repo is edited: core.js / terrain.js / colliders.js are
// swapped for in-memory stubs by a bun loader plugin (the tools/ragdoll-test.ts
// approach). ragdoll.js and its cone (rigmeasure, bodyengine, shared/joints) use
// no Math.random, so the run is deterministic.
//
// Skeleton coordinates are multiples of 2^-12 m so the sums/differences the C++
// side recomputes (local offset = rest[j] - rest[parent]) are exact and the two
// sims start bit-identical; only the yawed scenario rounds through a rotation.

import { plugin } from 'bun';
import { homedir, tmpdir } from 'node:os';
import { writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { dirname, join } from 'node:path';

const REPO = process.env.EIDOVERSE_REPO ?? join(homedir(), 'connectome-local/eidoverse-worlds-native');
const THREE_PATH = join(REPO, 'client/node_modules/three/build/three.module.js');
const OUT = join(import.meta.dir, '../../Plugins/Eidoverse/Resources/Conformance/ragdoll.json');

const STUBS: Record<string, string> = {
  core: `import * as T from ${JSON.stringify(THREE_PATH)}; export const THREE = T;`,
  terrain: `export const heightAt = () => 0;`,
  colliders: `export function resolveColliders(pos, terrainAt) { return terrainAt(pos.x, pos.z); }`,
};
// Runtime plugins resolve to real files, so the stubs are written to a temp dir.
const STUB_DIR = join(tmpdir(), `eido-ragdoll-stubs-${process.pid}`);
mkdirSync(STUB_DIR, { recursive: true });
for (const [k, v] of Object.entries(STUBS)) writeFileSync(join(STUB_DIR, `${k}.mjs`), v);
plugin({
  name: 'ragdoll-stubs',
  setup(build) {
    build.onResolve({ filter: /^\.\/(core|terrain|colliders)\.js$/ }, (a) => ({ path: join(STUB_DIR, `${a.path.slice(2, -3)}.mjs`) }));
  },
});

const THREE: any = await import(THREE_PATH);
const { Ragdoll, TUNING } = await import(join(REPO, 'client/lib/ragdoll.js'));
const { rigMath, PARENT } = await import(join(REPO, 'shared/rig.js'));
const { makeAvatar } = rigMath(THREE);

const Q = 4096;
const qz = (v: number) => Math.round(v * Q) / Q;

// ~1.6 m T-pose: the ragdoll-test synthetic body, a little deeper in Z so it is
// not perfectly planar, quantized.
const TPOSE: Record<string, number[]> = {
  hips: [0, 0.95, 0], spine: [0, 1.05, -0.01], chest: [0, 1.2, -0.015],
  neck: [0, 1.4, -0.01], head: [0, 1.5, 0.01],
  leftUpperArm: [0.18, 1.35, -0.02], leftLowerArm: [0.45, 1.35, -0.025], leftHand: [0.7, 1.35, -0.02],
  rightUpperArm: [-0.18, 1.35, -0.02], rightLowerArm: [-0.45, 1.35, -0.025], rightHand: [-0.7, 1.35, -0.02],
  leftUpperLeg: [0.09, 0.88, 0], leftLowerLeg: [0.095, 0.48, 0.01], leftFoot: [0.1, 0.07, -0.02],
  rightUpperLeg: [-0.09, 0.88, 0], rightLowerLeg: [-0.095, 0.48, 0.01], rightFoot: [-0.1, 0.07, -0.02],
};
// ~1.6 m A-pose with upperChest + shoulders, arms ~40° down: the 50-bone-rig shape,
// where leftUpperArm's real parent is leftShoulder, not chest.
const APOSE: Record<string, number[]> = {
  hips: [0, 0.93, 0.005], spine: [0, 1.03, -0.005], chest: [0, 1.15, -0.012], upperChest: [0, 1.27, -0.015],
  neck: [0, 1.41, -0.01], head: [0, 1.5, 0.005],
  leftShoulder: [0.04, 1.36, -0.02], leftUpperArm: [0.16, 1.355, -0.025], leftLowerArm: [0.36, 1.18, -0.03], leftHand: [0.54, 1.02, -0.02],
  rightShoulder: [-0.04, 1.36, -0.02], rightUpperArm: [-0.16, 1.355, -0.025], rightLowerArm: [-0.36, 1.18, -0.03], rightHand: [-0.54, 1.02, -0.02],
  leftUpperLeg: [0.085, 0.86, 0.005], leftLowerLeg: [0.09, 0.47, 0.012], leftFoot: [0.095, 0.07, -0.025],
  rightUpperLeg: [-0.085, 0.86, 0.005], rightLowerLeg: [-0.09, 0.47, 0.012], rightFoot: [-0.095, 0.07, -0.025],
};
const APOSE_PARENT: Record<string, string | null> = {
  ...PARENT, upperChest: 'chest', neck: 'upperChest',
  leftShoulder: 'upperChest', rightShoulder: 'upperChest',
  leftUpperArm: 'leftShoulder', rightUpperArm: 'rightShoulder',
};
const SKELETONS: Record<string, { P: Record<string, number[]>, parent: Record<string, string | null> }> = {
  tpose: { P: TPOSE, parent: PARENT },
  apose: { P: APOSE, parent: APOSE_PARENT },
};

type Scenario = {
  name: string; skeleton: string; lean: number[] | null;
  rootPos?: number[]; rootYaw?: number; vrm0?: boolean;
  startLocal?: Record<string, number[]>;           // normalized local quats at go-limp
  impulses?: { frame: number, v: number[] }[];       // applied BEFORE step(frame)
  pins?: { frame: number, joint: string, at?: number[] | null, follow?: number[] }[];
  handover?: number;                                 // snapshot + rebuild before step(frame)
  maxFrames?: number;
};

const SCENARIOS: Scenario[] = [
  { name: 'tpose_fall_forward', skeleton: 'tpose', lean: [0, 0, 1.5] },
  { name: 'tpose_fall_sideways', skeleton: 'tpose', lean: [1.5, 0, 0] },
  { name: 'apose_fall_forward', skeleton: 'apose', lean: [0, 0, 1.5] },
  { name: 'apose_impulse_midfall', skeleton: 'apose', lean: [0, 0, 1.5], impulses: [{ frame: 25, v: [-2, 0.5, 1] }] },
  { name: 'tpose_impulse_after_land', skeleton: 'tpose', lean: [-1.2, 0, 0.4], impulses: [{ frame: 90, v: [0, 0, -3] }] },
  // pinned right hand dragged 0.5 m along +X over 2 s, held, then released: pins
  // suspend settle/deadline, so the release is what lets it finish
  { name: 'apose_pinned_hand', skeleton: 'apose', lean: [0.3, 0, 1.0],
    pins: [{ frame: 0, joint: 'rightHand', follow: [0.5 / 120, 0, 0] }, { frame: 120, joint: 'rightHand', follow: [0, 0, 0] },
      { frame: 200, joint: 'rightHand', at: null }] },
  // conventions: root off the origin, yawed, VRM0 half-turn pivot, a mid-stride start pose
  { name: 'apose_yaw_vrm0_stride', skeleton: 'apose', lean: null, rootPos: [qz(2.5), qz(0.0), qz(-1.25)], rootYaw: 0.7, vrm0: true,
    startLocal: {
      leftUpperLeg: [-0.3, 0, 0, Math.sqrt(1 - 0.09)], rightUpperLeg: [0.25, 0, 0, Math.sqrt(1 - 0.0625)],
      leftLowerLeg: [0.38, 0, 0, Math.sqrt(1 - 0.1444)], spine: [0.07, 0.03, 0, Math.sqrt(1 - 0.0058)],
      leftUpperArm: [0, 0, -0.2, Math.sqrt(1 - 0.04)],
    } },
  { name: 'tpose_handover', skeleton: 'tpose', lean: [0.4, 0, 1.4], handover: 70 },
];
// the yawed scenario falls the way it faces (localbody.toppleVelocity)
SCENARIOS[6].lean = [Math.sin(0.7) * 1.4, 0, Math.cos(0.7) * 1.4];

const v3 = (a: number[]) => new THREE.Vector3(a[0], a[1], a[2]);
const arr = (v: any) => [v.x, v.y, v.z];
const SAMPLE_EVERY = 10;

function buildAvatar(s: Scenario) {
  const sk = SKELETONS[s.skeleton];
  const P = Object.fromEntries(Object.entries(sk.P).map(([k, v]) => [k, new THREE.Vector3(qz(v[0]), qz(v[1]), qz(v[2]))]));
  const av = makeAvatar(P, { realParent: sk.parent === PARENT ? null : sk.parent, vrm0: !!s.vrm0 });
  if (s.rootPos) av.root.position.set(s.rootPos[0], s.rootPos[1], s.rootPos[2]);
  av.root.rotation.y = s.rootYaw ?? 0;
  for (const [j, q] of Object.entries(s.startLocal ?? {})) av.nodes[j].quaternion.set(q[0], q[1], q[2], q[3]);
  av.root.updateMatrixWorld(true);
  return av;
}

function restMap(av: any) {
  return Object.fromEntries(Object.entries(av.restBonePositions()).map(([k, v]: any) => [k, arr(v)]));
}

function run(s: Scenario) {
  Object.assign(TUNING, BASE_TUNING);
  const av = buildAvatar(s);
  const sk = SKELETONS[s.skeleton];
  const rest = av.restBonePositions();
  const input = {
    rest: restMap(av),
    parents: Object.fromEntries(Object.keys(av.nodes).map((j) => [j, sk.parent[j] ?? null])),
    rootPos: arr(av.root.position), rootYaw: s.rootYaw ?? 0, vrm0: !!s.vrm0,
    startLocal: s.startLocal ?? {}, lean: s.lean,
  };
  let rd: any = new Ragdoll(av, s.lean ? v3(s.lean) : null, rest);
  const samples: any[] = [];
  const events: any[] = [];
  const pinState = new Map<string, { at: any, follow: number[] }>();
  const maxFrames = s.maxFrames ?? 2000;
  let frame = 0;
  let handover: any = null;
  for (; frame < maxFrames && !rd.done; frame++) {
    for (const im of s.impulses ?? []) if (im.frame === frame) { rd.impulse(v3(im.v)); events.push({ frame, kind: 'impulse', v: im.v }); }
    for (const pe of s.pins ?? []) {
      if (pe.frame !== frame) continue;
      if (pe.at === null) { pinState.delete(pe.joint); rd.setPin(pe.joint, null); events.push({ frame, kind: 'unpin', joint: pe.joint }); continue; }
      const at = pe.at ? v3(pe.at) : (pinState.get(pe.joint)?.at ?? rd.p[pe.joint].clone());
      pinState.set(pe.joint, { at, follow: pe.follow ?? [0, 0, 0] });
    }
    for (const [j, ps] of pinState) {
      ps.at.add(v3(ps.follow));
      rd.setPin(j, ps.at);
      events.push({ frame, kind: 'pin', joint: j, at: arr(ps.at) });
    }
    if (s.handover === frame) {
      // a re-creation mid-flight (bodydrag release): exact snapshot + the live
      // skeleton the old sim left, rest re-measured at the root's NEW position
      const snap = rd.snapshot();
      const locals = Object.fromEntries(Object.entries(av.nodes).map(([k, n]: any) => [k, [n.quaternion.x, n.quaternion.y, n.quaternion.z, n.quaternion.w]]));
      av.root.updateMatrixWorld(true);
      const rest2 = av.restBonePositions();
      handover = { frame, snapshot: snap, locals, rest: restMap(av), rootPos: arr(av.root.position) };
      rd = new Ragdoll(av, null, rest2, snap);
    }
    const pose = rd.step(1 / 60);
    if (frame % SAMPLE_EVERY === 0 || rd.done) {
      samples.push({
        frame, done: rd.done, pose, root: arr(av.root.position),
        p: Object.fromEntries(Object.entries(rd.p).map(([k, v]: any) => [k, arr(v)])),
        maxV: rd.maxV, settledFor: rd.settledFor, elapsed: rd.elapsed,
      });
    }
  }
  const final = samples[samples.length - 1];
  console.log(`${s.name.padEnd(28)} frames=${frame} done=${rd.done} settledFor=${rd.settledFor.toFixed(3)} elapsed=${rd.elapsed.toFixed(3)} maxV=${rd.maxV.toFixed(4)} hips=${final.p.hips.map((x: number) => x.toFixed(3))}`);
  return { name: s.name, skeleton: s.skeleton, input, events, handover, frames: frame, done: rd.done, samples, finalPose: rd.finalPose };
}

const BASE_TUNING = { ...TUNING };
const cases = SCENARIOS.map(run);
mkdirSync(dirname(OUT), { recursive: true });
writeFileSync(OUT, JSON.stringify({
  source: 'eidoverse-worlds-native client/lib/ragdoll.js via Tools/eidoverse/ragdoll_vectors.ts',
  dt: 1 / 60, sampleEvery: SAMPLE_EVERY, tuning: BASE_TUNING, cases,
}));
rmSync(STUB_DIR, { recursive: true, force: true });
console.log(`wrote ${OUT}`);
