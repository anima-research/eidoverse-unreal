// src/scene.ts — slice A: bridge/scene v2, the render model from the fold's view, in main's render-scene.ts shape (ADAPTER-DESIGN §1, §3.6; T3).
import { createHash } from "node:crypto";
import type { Ctx, Slice } from "./slice.ts";
import { planStructure } from "../engine/shared/structure.js";
import { terrainParams, makeHeightField } from "../engine/shared/terrainmath.js";
import { waterParams as levelParams, shapeReady, shapeParams, requestGrid, setGridFetcher } from "../engine/shared/terrainshape.js";
import { analyzeWater } from "../engine/shared/waterkind.js";
import { effectiveWorldTransform } from "../engine/mcpl/effective.ts";
import { since } from "../engine/client/lib/motioneval.js";
import { effectiveSky, effectiveClock } from "../engine/shared/forecast.js";
import { projectFlora } from "../engine/server/render-flora.ts";
import { waterParams, vector, MAX_AIR } from "../engine/shared/water.js";
import { resolveParticles } from "../engine/shared/particle_effects.js";
import { normalizeSound } from "../engine/shared/sound.js";
import { enc } from "../engine/shared/protocol/names.js";
import { addBodies } from "./bodies.ts";

export const SCENE_VERSION = 2;
const MAX_ENTITIES = 512, MAX_BYTES = 6 * 1024 * 1024;
const hash = (v: unknown) => createHash("sha256").update(JSON.stringify(v)).digest("hex").slice(0, 24);
const isObj = (v: any): v is Record<string, any> => !!v && typeof v === "object" && !Array.isArray(v);

/** Shaped terrain's grids come from the server's library over HTTP, as the agent loads them (mcpl/agent.ts:2030). */
export function useLibrary(server: string) {
  const base = server.replace(/^ws/, "http").replace(/\/ws$/, "");
  setGridFetcher(async (ref: string) => { const r = await fetch(`${base}/library/${ref}`); if (!r.ok) throw new Error(`${r.status}`); return r.json(); });
}

// ---- the environment: ground, its water, sky, grass, flora ----
function levelWaterOf(water: any, height: (x: number, z: number) => number, size: number, n: number) {
  const w = levelParams({ water });
  if (!w) return null;
  const a = analyzeWater(height, size, { level: w.level, kind: water?.kind, flow: water?.flow }, n);
  const K: Record<string, number> = { still: 0, river: 1, ocean: 2 };
  const r3 = (v: number) => Math.round(v * 1000) / 1000;
  const cells: number[] = [];
  for (let k = 0; k < a.depth.length; k++) {
    if (!(a.depth[k] > 0) || a.comp[k] < 0) continue;
    const b = a.bodies[a.comp[k]]; if (!b) continue;
    const river = b.kind === "river", ocean = b.kind === "ocean";
    cells.push(k, K[b.kind] ?? 0, river ? r3(a.flow[2 * k]) : 0, river ? r3(a.flow[2 * k + 1]) : 0, river ? r3(b.waveSpeed ?? 0) : 0,
      ocean ? r3(a.toShore[2 * k]) : 0, ocean ? r3(a.toShore[2 * k + 1]) : 0, r3(Math.min(a.shore[k], 999)));
  }
  return { level: w.level, color: w.color, opacity: w.opacity, cells };
}
const colorInt = (c: any, d: number) => { const v = typeof c === "string" ? parseInt(c.replace("#", ""), 16) : Number(c); return Number.isFinite(v) ? v : d; };
/** The ground as heights on a grid, its water, and the sky/grass/flora (render-scene.ts projectEnvironment, on the view). */
export function environmentOf(view: any) {
  const T = view.terrain;
  const p = terrainParams(T ?? { size: 200, amplitude: 0 });
  if (!(p.size > 0 && p.size <= 10000)) throw Error("terrain size unsupported");
  const n = Math.min(128, Math.max(64, Math.round(Number(T?.segments) || 64))), height = makeHeightField(p), heights: number[] = [];
  for (let z = 0; z <= n; z++) for (let x = 0; x <= n; x++) heights.push(height((x / n - 0.5) * p.size, (z / n - 0.5) * p.size));
  const layers = Array.isArray(T?.layers) ? T.layers.slice(0, 8).map((l: any) => ({ color: colorInt(l?.color ?? 0x70815a, NaN), repeat: Number.isFinite(l?.repeat) ? l.repeat : 16 })) : [];
  return { terrain: { size: p.size, segments: n, heights, color: colorInt(T?.color ?? T?.layers?.[0]?.color ?? 0x70815a, 0x70815a), authored: !!T, layers },
    water: levelWaterOf(view.water, height, p.size, n),
    sky: view.sky ?? null, grass: view.grass ?? null, flora: projectFlora(view.grass, height) };
}

// ---- the tree: what is drawn as a thing, and what is part of its parent ----
/** A child drawn as its host's data, not a thing: a socket, a water volume, a dry box (translate-v1 VOLUMES). */
const partOfHost = (r: any) => isObj(r?.socket) || (isObj(r?.zone) && (isObj(r.water) || isObj(r["water-exclusion"])));
type Tree = Map<string, string[]>;
function treeOf(view: any): Tree {
  const kids: Tree = new Map();
  for (const [id, r] of Object.entries<any>(view.entities)) if (r.parent?.to) kids.set(r.parent.to, [...(kids.get(r.parent.to) ?? []), id]);
  return kids;
}
const partsOf = (view: any, kids: Tree, id: string) => (kids.get(id) ?? []).map((k) => [k, view.entities[k]] as const).filter(([, r]) => partOfHost(r));

/** Socket children as the renderer's map {name: {pos, yaw, pose?, part?}}. */
function socketsOf(view: any, kids: Tree, id: string) {
  let out: Record<string, any> | null = null;
  for (const [k, s] of partsOf(view, kids, id)) {
    if (!isObj(s.socket)) continue;
    const st = isObj(s.station) ? s.station : {}, pose = st.anim ?? st.pose;
    (out ??= {})[String(s.socket.name ?? k)] = { pos: s.pos, yaw: s.yaw, ...(typeof pose === "string" ? { pose } : {}), ...(typeof s.socket.part === "string" ? { part: s.socket.part } : {}) };
  }
  return out;
}

/** Renderer-facing component parameters (render-scene.ts projectComponentData), from the protocol's names. */
export function componentData(view: any, kids: Tree, id: string) {
  const rec = view.entities[id], data: Record<string, unknown> = {};
  if (rec.particles) { const { ok, ...r } = resolveParticles(rec.particles, { entityId: id }) as any; if (ok) data.particles = r; }
  if (isObj(rec.material) && typeof rec.material.texture === "string") {   // main's picture (translate-v1 CARRIED.picture)
    const m = rec.material, flip = isObj(rec["imported:material"]) ? rec["imported:material"].flip : undefined;
    data.picture = { src: m.texture, ...(typeof m.node === "string" ? { part: m.node } : {}), ...(m.lit !== undefined ? { lit: m.lit } : {}),
      ...(m.description !== undefined ? { look: m.description } : {}), ...(flip !== undefined ? { flip } : {}) };
  }
  if (rec.sound) { const r = normalizeSound(rec.sound) as any; if (r.ok) data.sound = r.sound; }
  const parts: Record<string, unknown> = {};
  for (const [k, v] of Object.entries<any>(rec)) if (k.startsWith("motion:") && k.length > 7 && isObj(v)) { const { node, ...m } = v; parts[String(typeof node === "string" ? node : k.slice(7)).slice(0, 64)] = m; }
  if (Object.keys(parts).length) data.parts = Object.fromEntries(Object.entries(parts).slice(0, 32));
  const own = isObj(rec.zone) && isObj(rec.water) ? [[id, { ...rec, pos: [0, 0, 0] }] as const] : [];
  const vols = [...own, ...partsOf(view, kids, id).filter(([, r]) => isObj(r.zone))];
  const wet = vols.find(([, r]) => isObj(r.water));
  if (wet) { const [, r] = wet; const { center, size, absorption, scatter, speed, waves } = waterParams({ ...r.water, center: r.pos, size: r.zone.size }); data.water = { center, size, absorption, scatter, speed, waves }; }
  const dry = vols.filter(([, r]) => isObj(r["water-exclusion"])).slice(0, MAX_AIR);
  if (dry.length) data.air = { boxes: dry.map(([, r]) => ({ center: vector(r.pos), size: vector(r.zone.size, [1, 1, 1]).map(Math.abs), q: Array.isArray(r.q) && r.q.length === 4 ? r.q : [0, 0, 0, 1] })) };
  if (isObj(rec.collider) && typeof rec.collider.enabled === "boolean") data.collision = { enabled: rec.collider.enabled };   // CARRIED.collision
  return Object.keys(data).length ? data : null;
}

const LIGHT = { color: 0xffe0b0, intensity: 1, range: 10 };
const NOT_COMPONENTS = new Set(["id", "pos", "q", "yaw", "scale", "scale3", "born", "model", "parent"]);
/** One drawn thing, in the renderer's upsert shape (render-scene.ts projectEntity). */
export function upsertOf(view: any, kids: Tree, id: string) {
  const rec = view.entities[id];
  let structure: any = null;
  if (rec.structure) {
    const plan = planStructure(rec.structure);
    structure = { boxes: plan.boxes, parts: plan.levels.flatMap((l: any) => l.parts), sweeps: plan.levels.flatMap((l: any) => l.sweeps) };
    if (structure.parts.length + structure.boxes.length > 12000 || structure.sweeps.reduce((n: number, s: any) => n + s.positions.length, 0) > 600000)
      throw Error("structure geometry budget exceeded");
  }
  // a light is a thing with no model and a light (the v1 light verb's); a light ON a model is a model's component
  const kind = rec.model ? "model" : isObj(rec.light) ? "light" : "empty";
  const L = kind === "light" ? rec.light : {};
  const out = { id, lib: rec.model ?? "", kind, structure,
    color: L.color ?? LIGHT.color, intensity: L.intensity ?? LIGHT.intensity, range: L.range ?? LIGHT.range,
    collide: rec.collide ?? "exact", collideAuthored: rec.collide ?? null, components: Object.keys(rec).filter((k) => !NOT_COMPONENTS.has(k)),
    data: componentData(view, kids, id),
    ...(kind === "light" ? { keep: L.important === true, day: L.daytime !== "full" } : {}),
    interact: { actions: Object.keys(rec.reactions ?? {}).slice(0, 32), sockets: socketsOf(view, kids, id) ?? null,
      locked: !!rec.lock, mounted: !!rec.parent, moving: !!rec.motion || Object.keys(rec).some((k) => k.startsWith("motion:")),
      // a guard is a lock now, and who placed / last wrote a thing is the log's, not the fold's (translate-v1 DROPPED)
      guarded: false, placer: null, placerSub: null, lastActor: null } };
  return { ...out, revision: hash(out) };
}

// ---- where things are now ----
/** A root motion for the renderer to run (render-scene.ts localMotion): its base pose, the motion with t0 resolved. */
function rootMotion(rec: any, nowMs: number, simBody: boolean) {
  const m = rec?.motion;
  if (rec?.parent || simBody || !isObj(m) || typeof m.type !== "string") return null;
  const t0 = Number.isFinite(m.t0) ? m.t0 : nowMs - since(m, nowMs) * 1000;
  return { base: { pos: rec.pos, yaw: rec.yaw ?? 0 }, m: { ...m, t0, ...(Array.isArray(rec.pivot?.pos) ? { pivot: rec.pivot.pos } : {}) } };
}
/** A mount in the renderer's terms {to, offset, yaw, scale, slot?, part?}: a socket child said as host + slot (render-scene.ts localMount). */
function mountOf(view: any, rec: any) {
  const p = rec?.parent; if (!p?.to) return null;
  const s = view.entities[p.to];
  if (isObj(s?.socket) && s.parent?.to) {
    const [x, y, z] = Array.isArray(p.offset) ? p.offset : [0, 0, 0], c = Math.cos(s.yaw ?? 0), sn = Math.sin(s.yaw ?? 0);
    return { to: s.parent.to, offset: [s.pos[0] + c * x + sn * z, s.pos[1] + y, s.pos[2] - sn * x + c * z], yaw: (s.yaw ?? 0) + (rec.yaw ?? 0), scale: rec.scale ?? 1,
      slot: String(s.socket.name), ...(typeof s.socket.part === "string" ? { part: s.socket.part.slice(0, 64) } : {}) };
  }
  return { to: p.to, offset: p.offset ?? [0, 0, 0], yaw: rec.yaw ?? 0, scale: rec.scale ?? 1 };
}
function partMotions(rec: any, nowMs: number) {
  const out: Record<string, unknown> = {};
  for (const [k, v] of Object.entries<any>(rec ?? {})) {
    if (!k.startsWith("motion:") || k.length <= 7 || !isObj(v) || typeof v.type !== "string") continue;
    const { node, ...m } = v;
    out[String(typeof node === "string" ? node : k.slice(7)).slice(0, 64)] = { ...m, t0: Number.isFinite(m.t0) ? m.t0 : nowMs - since(m, nowMs) * 1000 };
  }
  return Object.keys(out).length ? out : null;
}

/** What a renderer was last sent; the view's projection, kept while the fold's head stands. */
export type Delivered = { worldId: unknown; entities: Map<string, string>; environment: string };
export type Memo = { head?: number; worldId?: unknown; entities?: any[]; environment?: any; signature?: string; envSource?: string };

/** The render model at `now` for a renderer last sent `old` (null: everything). */
export function renderModel(replica: any, world: string, old: Delivered | null, now: number, memo: Memo = {}) {
  const view = replica.view();
  if (!old || old.worldId !== replica.worldId) old = { worldId: replica.worldId, entities: new Map(), environment: "" };
  // a shaped ground whose grid hasn't loaded would be drawn as the noise beneath it: wait for it (render-scene.ts)
  if (!shapeReady(shapeParams(view.terrain ?? {}))) {
    void requestGrid(view.terrain?.heightmap?.grid);
    return { delivered: old, msg: { type: "bridge/scene", version: SCENE_VERSION, world, error: "terrain_pending", moving: true, updateIntervalMs: 300,
      detail: `heightmap grid ${String(view.terrain?.heightmap?.grid ?? "").slice(0, 120)} is not available yet` } };
  }
  const kids = treeOf(view);
  const drawn = Object.keys(view.entities).filter((id) => !(partOfHost(view.entities[id]) && view.entities[id].parent?.to));
  if (drawn.length > MAX_ENTITIES) throw Error(`scene exceeds ${MAX_ENTITIES} entities`);
  if (memo.head !== replica.head || memo.worldId !== replica.worldId) {
    const envSource = hash([view.terrain, view.sky, view.grass, view.water]);
    if (memo.envSource !== envSource) { memo.environment = environmentOf(view); memo.signature = hash(memo.environment); memo.envSource = envSource; }
    let numbers = 0, parts = 0;
    memo.entities = drawn.map((id) => { const e = upsertOf(view, kids, id);
      numbers += e.structure?.sweeps.reduce((n: number, s: any) => n + s.positions.length + s.indices.length, 0) ?? 0;
      parts += (e.structure?.parts.length ?? 0) + (e.structure?.boxes.length ?? 0);
      if (numbers > 600000 || parts > 12000) throw Error("total scene geometry budget exceeded");
      return e; });
    memo.head = replica.head; memo.worldId = replica.worldId;
  }
  // the engine's flights at this render time (the replica's copy, advanced; never the canonical sim)
  const sim = replica.sim?.sim ? replica.simAt(now) : null;
  const flying = (id: string) => { const b = sim?.bodies?.[enc(id)]; return b && replica.sim?.launched?.[enc(id)] ? b : null; };
  const activeBodies = sim ? Object.values<any>(sim.bodies ?? {}).filter((b) => !b.resting).length : 0;
  const ev = { entity: (id: string) => { const r = view.entities[id], b = flying(id); return r && b ? { ...r, pos: b.p, q: b.q ?? r.q, motion: null } : r; },
    mount: (id: string) => view.entities[id]?.parent };
  const transforms: any[] = [], warnings: string[] = [...(memo.environment.flora?.warnings ?? [])];
  let moving = !!view.sky || activeBodies > 0;
  for (const e of memo.entities!) {
    const rec = view.entities[e.id], b = flying(e.id), pm = partMotions(rec, now);
    const t = effectiveWorldTransform(e.id, ev as any, now);
    if (!t.ok) { warnings.push(`${e.id}: ${t.why}`); continue; }
    const motion = rootMotion(rec, now, !!b), mount = rec.parent ? mountOf(view, rec) : null;
    transforms.push({ id: e.id, p: t.pos, q: t.quat, scale: t.scale, ...(motion ? { motion } : {}), ...(mount ? { mount } : {}), ...(pm ? { parts: pm } : {}),
      ...(b ? { sim: { v: b.v, resting: !!b.resting, on: !!b.on, tickMs: sim?.epoch?.tickMs ?? 66 } } : {}) });
    moving ||= !!t.moving || !!pm;
  }
  const revisions = new Map(memo.entities!.map((e) => [e.id, e.revision]));
  const msg = { type: "bridge/scene", version: SCENE_VERSION, world, seq: replica.head, time: now, moving,
    simulation: { enabled: !!replica.st?.entities?.["@world"]?.c?.["physics-engine"], foreign: !!sim?.epoch?.foreign, activeBodies },
    updateIntervalMs: activeBodies ? 66 : 300,
    atmosphere: { clock: effectiveClock(view.sky, now), weather: effectiveSky(view.sky, now), time: now },
    upserts: memo.entities!.filter((e) => old!.entities.get(e.id) !== e.revision),
    removed: [...old.entities.keys()].filter((id) => !revisions.has(id)), transforms,
    ...(old.environment !== memo.signature ? { environment: memo.environment } : {}), warnings };
  if (Buffer.byteLength(JSON.stringify(msg)) > MAX_BYTES) throw Error("scene exceeds message budget");
  return { delivered: { worldId: old.worldId, entities: revisions, environment: memo.signature! }, msg };
}

type State = { delivered: Delivered | null; memo: Memo };
const stateOf = (ctx: Ctx) => ctx.own<State>(() => ({ delivered: null, memo: {} }));

/** bridge/scene {version: 2, reset?}: answered from the fold here; nothing goes to the network. */
function scene(ctx: Ctx, msg: any) {
  const world = String(ctx.join?.world ?? "");
  if (msg.version !== SCENE_VERSION) return ctx.send({ type: "bridge/scene", version: SCENE_VERSION, world, error: "unsupported_version" });
  if (!ctx.replica?.active) return ctx.send({ type: "bridge/scene", version: SCENE_VERSION, world, error: "not_joined", moving: true, updateIntervalMs: 300 });
  const s = stateOf(ctx);
  try {
    const r = renderModel(ctx.replica, world, msg.reset === true ? null : s.delivered, Date.now(), s.memo);
    if (!("error" in r.msg)) { s.delivered = r.delivered; addBodies(ctx, r.msg); }
    ctx.send(r.msg);
  } catch (err) {
    ctx.send({ type: "bridge/scene", version: SCENE_VERSION, world, error: "projection_failed", detail: String(err).slice(0, 160) });
  }
}

export default {
  name: "scene",
  local: { "bridge/scene": scene },
  adopted(ctx) { const s = stateOf(ctx); s.memo = {}; if (ctx.server) useLibrary(ctx.server); },
} satisfies Slice;
