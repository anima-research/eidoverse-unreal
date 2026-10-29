// T3 the render model: (a) main's render-scene on main's fold vs bridge/scene on the translated world, only named differences; (b) the C++ key pin (ADAPTER-DESIGN §5; README).
import { test, expect, beforeAll, afterAll } from "bun:test";
import fs from "node:fs";
import path from "node:path";
import { startServer, startBridge, renderer, ROOT, ENGINE, scratch, type Server, type Bridge } from "./harness.ts";
import { join } from "./fixtures/renderer.ts";
import { scanKeys } from "./renderer-keys.ts";
import { DROPPED } from "../engine/tools/protocol/import-v1/translate-v1.js";

const MAIN = process.env.MAIN_DIR ?? "/home/claude/eido/wt-main-ccd5";
const UNREAL = process.env.UNREAL_DIR ?? "/home/claude/eido/ref/eidoverse-unreal";
const TOL = 1e-6;
const dirsIn = (d: string) => fs.existsSync(d) ? fs.readdirSync(d).sort().map((n) => path.join(d, n)).filter((p) => fs.existsSync(path.join(p, "log.jsonl"))) : [];
const WORLDS = [...dirsIn(path.join(ENGINE, "spec/fixtures")), ...dirsIn(path.join(ROOT, "test/fixtures/worlds")),
  ...(process.env.ORACLE_WORLDS ?? "").split(path.delimiter).filter(Boolean).flatMap((d) => fs.existsSync(path.join(d, "log.jsonl")) ? [d] : dirsIn(d))];
const nameOf = (d: string) => path.basename(d).replace(/[^A-Za-z0-9_-]/g, "_");

const KEYS = JSON.parse(fs.readFileSync(path.join(ROOT, "test/fixtures/renderer-keys.json"), "utf8")) as Record<string, { sites: string[]; at?: string[]; not?: string }>;
type Diff = { world: string; at: string; main: unknown; bridge: unknown };
const show = (v: unknown) => { const s = JSON.stringify(v); return s === undefined ? "(absent)" : s.length > 140 ? s.slice(0, 137) + "…" : s; };
function deepDiff(a: any, b: any, at: string, out: Diff[], world: string) {
  if (typeof a === "number" && typeof b === "number") { if (!(a === b || Math.abs(a - b) <= TOL)) out.push({ world, at, main: a, bridge: b }); return; }
  if (Array.isArray(a) && Array.isArray(b)) { if (a.length !== b.length) out.push({ world, at, main: a, bridge: b }); else a.forEach((x, i) => deepDiff(x, b[i], `${at}[${i}]`, out, world)); return; }
  if (a && b && typeof a === "object" && typeof b === "object" && !Array.isArray(a) && !Array.isArray(b)) {
    for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) deepDiff(a[k], b[k], `${at}.${k}`, out, world); return; }
  if (JSON.stringify(a) !== JSON.stringify(b)) out.push({ world, at, main: a, bridge: b });
}
const byId = (xs: any[]) => Object.fromEntries((xs ?? []).map((x) => [x.id, x]));

/** Why a difference is expected. Each cites translate-v1's DROPPED (by key) or a design row; `when` narrows it. */
const D = (k: string) => { if (!(k in DROPPED)) throw new Error(`no DROPPED row ${k}`); return `translate-v1 DROPPED["${k}"]`; };
type Rule = { at: RegExp; why: string; when?: (d: Diff, ctx: any) => boolean };
const RULES: Rule[] = [
  { at: /^\.type$/, why: "ADAPTER-DESIGN §1: the render model is bridge/scene v2 (a local message), not the wire's render-scene" },
  { at: /^\.version$/, why: "ADAPTER-DESIGN §1: bridge/scene v2" },
  { at: /^\.seq$/, why: "a position in each engine's own log (the v1 log's seq; the translated log's head)" },
  { at: /\.interact\.(placer|placerSub)$/, why: D("placer") },
  { at: /\.interact\.lastActor$/, why: D("entity.actor / entity.ts") },
  { at: /\.interact\.(guarded|locked)$/, why: D("comp.guard"), when: (d, c) => !!c.mainState.entities[c.id]?.comp?.guard },
  { at: /^\.upserts\.[^.]+\.components(\[|$)/, why: "the protocol's component names (translate-v1 project); the renderer reads no `components` (renderer-keys.json)" },
  { at: /\.data\.particles(\.|$)/, why: D("particles.wobble / up / spread / gravity / grow / swirl / twinkle / converge / rotate / color / blending") + ": the protocol's particles are an effect instance {effect, …}, resolved by shared/particle_effects.js" },
  { at: /\.data\.picture\.look$/, why: D("sound.look") + " (the picture's look likewise: material.description)" },
  { at: /\.data\.sound\.(look|description)$/, why: D("sound.look") },
  { at: /\.(data\.parts|parts)\.[^.]+(\.|$)|\.motion\.m(\.|$)/, why: `${D("motion.rpm")}, ${D("motion.type")}, ${D("motion.part")}; ADAPTER-DESIGN §3.4: the renderer's motion vectors are regenerated from the protocol's motion` },
  { at: /^\.atmosphere\.clock\.(seq|by)$/, why: D("entity.actor / entity.ts") + " (a stored sky carries no seq/by: the entry that wrote it does; replica.js #260 I286)" },
  { at: /\.data\.picture\.flip$/, why: D("picture.flip false"), when: (d) => d.main === false && d.bridge === undefined },
  { at: /\.interact\.moving$/, why: D("motion.part") + ": a part's motion is motion:<slot>, and moves the thing as main's bare part motion did", when: (d, c) => d.bridge === true && Object.keys(c.mainState.entities[c.id] ?? {}).length > 0 },
  // additive (ADAPTER-DESIGN §3.6): a key main never sent that the renderer doesn't read (renderer-keys.json)
  { at: /\.([^.[\]]+)$/, why: "additive: a key main never sent, which the renderer doesn't read (renderer-keys.json)", when: (d) => d.main === undefined && !(/\.([^.[\]]+)$/.exec(d.at)![1] in KEYS) },
  { at: /^\.environment\.(sky|grass)(\.|$)/, why: "translate-v1 @12 wave 2 (render-media-env): @world sky and grass in today's spelling (shared/protocol/shaperepair.js)" },
];
/** revision is a digest of the entity's other fields: it may differ only where one of those differs by a rule. */
const revisionRule = (d: Diff, excused: Set<string>) => /^\.upserts\.([^.]+)\.revision$/.test(d.at) && [...excused].some((a) => a.startsWith(d.at.replace(/\.revision$/, ".")));

const results: { world: string; main: any; mainState: any; bridge: any; again: any }[] = [];
const probes: any[] = [];
let srv: Server, br: Bridge;

beforeAll(async () => {
  srv = await startServer({ worlds: [], seed: (W) => {
    for (const d of WORLDS) {
      const r = Bun.spawnSync(["bun", path.join(ROOT, "test/oracle/translate.ts"), d, nameOf(d)], { cwd: ROOT,
        env: { ...process.env, WORLDS_DIR: W, OPT_DIR: path.join(path.dirname(W), "opt"), BUN_RUNTIME_TRANSPILER_CACHE_PATH: "0" } });
      if (r.exitCode !== 0) throw new Error(`translate ${d}: ${r.stderr.toString().slice(-600)}`);
    } } });
  br = await startBridge();
  for (const d of WORLDS) {
    const world = nameOf(d);
    const r = await renderer(br, srv.url); r.send(join(world, "Oracle").raw);
    await r.wait(`${world}: bridge/ready`, (m) => m.type === "bridge/ready" || m.type === "error");
    let k = r.msgs.length; r.send({ type: "bridge/scene", version: 2, reset: true });
    const bridge = await r.wait(`${world}: bridge/scene`, (m) => m.type === "bridge/scene" || (m.type === "bridge/error" && m.for === "bridge/scene"), k);
    k = r.msgs.length; r.send({ type: "bridge/scene", version: 2 });
    const again = await r.wait(`${world}: bridge/scene again`, (m) => m.type === "bridge/scene" || m.type === "bridge/error", k);
    if (!probes.length) { k = r.msgs.length; r.send({ type: "bridge/scene", version: 1 }); probes.push(await r.wait("a v1 ask", (m) => m.type === "bridge/scene", k)); }
    r.close();
    const s = scratch("main");
    const mr = Bun.spawnSync(["bun", path.join(ROOT, "test/oracle/main-scene.ts"), path.join(d, "log.jsonl"), world, String(bridge.time ?? Date.now())], { cwd: MAIN,
      env: { ...process.env, MAIN_DIR: MAIN, WORLDS_DIR: path.join(s, "w"), OPT_DIR: path.join(s, "o"), BUN_RUNTIME_TRANSPILER_CACHE_PATH: "0" } });
    if (mr.exitCode !== 0) throw new Error(`main's render-scene on ${world}: ${mr.stderr.toString().slice(-600)}`);
    const { state, scene } = JSON.parse(mr.stdout.toString());
    results.push({ world, main: scene, mainState: state, bridge, again });
  }
}, 600_000);
afterAll(() => { br?.stop(); srv?.stop(); });

test("T3a every world: bridge/scene agrees with main's render-scene field by field, but for named reasons", () => {
  const unexplained: string[] = [], explained = new Map<string, number>();
  expect(results.length).toBe(WORLDS.length);
  for (const { world, main, mainState, bridge } of results) {
    if (bridge.type !== "bridge/scene" || bridge.error) { unexplained.push(`${world}: bridge answered ${show(bridge)}`); continue; }
    if (main.error) { unexplained.push(`${world}: main answered ${show(main)}`); continue; }
    const diffs: Diff[] = [];
    const M = { ...main, upserts: byId(main.upserts), transforms: byId(main.transforms) }, B = { ...bridge, upserts: byId(bridge.upserts), transforms: byId(bridge.transforms) };
    // a thing main's fold never kept (a lib-less spawn: main predates the Empty, spec/fixtures/README 06-components) is extra
    for (const part of ["upserts", "transforms"] as const) for (const id of Object.keys(B[part])) if (!(id in M[part]) && !mainState.entities[id]) {
      explained.set("an Empty main's fold refuses (lib-less spawn; spec/fixtures/README 06-components)", (explained.get("an Empty main's fold refuses (lib-less spawn; spec/fixtures/README 06-components)") ?? 0) + 1); delete B[part][id]; }
    deepDiff(M, B, "", diffs, world);
    const excused = new Set<string>();
    for (const d of diffs) {
      const id = /^\.(?:upserts|transforms)\.([^.[]+)/.exec(d.at)?.[1];
      const rule = RULES.find((r) => r.at.test(d.at) && (!r.when || r.when(d, { id, mainState })));
      if (rule) { excused.add(d.at); explained.set(rule.why, (explained.get(rule.why) ?? 0) + 1); }
    }
    for (const d of diffs) if (!excused.has(d.at)) {
      if (revisionRule(d, excused)) { explained.set("revision: a digest of the fields above", (explained.get("revision: a digest of the fields above") ?? 0) + 1); continue; }
      unexplained.push(`${world} ${d.at}: main ${show(d.main)}  bridge ${show(d.bridge)}`);
    }
  }
  console.log(`T3a ${results.length} worlds (${results.map((r) => r.world).join(", ")}); differences explained:`);
  for (const [why, n] of explained) console.log(`  ${String(n).padStart(4)} × ${why}`);
  expect(unexplained).toEqual([]);
});

test("T3c an ask in another version is refused by name (the renderer's v1 poll gets unsupported_version)", () => {
  expect(probes[0]).toMatchObject({ type: "bridge/scene", version: 2, error: "unsupported_version" });
});

test("T3a' a second poll is incremental: no upserts, nothing removed, no environment (as main's cache)", () => {
  for (const { world, again } of results) expect({ world, upserts: again.upserts?.length, removed: again.removed?.length, env: "environment" in again }).toEqual({ world, upserts: 0, removed: 0, env: false });
});


test.skipIf(!fs.existsSync(UNREAL))("T3b the renderer's reads (a scan of its C++) are the checked-in list", () => {
  const scan = scanKeys(UNREAL);
  expect(Object.fromEntries(Object.entries(KEYS).map(([k, v]) => [k, v.sites]))).toEqual(scan);
});

test("T3b every key the renderer reads is produced where it reads it (`at`), or listed with why not", () => {
  const paths = new Set<string>();
  const walk = (v: any, at: string) => { if (Array.isArray(v)) v.forEach((x) => walk(x, at + "[]")); else if (v && typeof v === "object") for (const [k, x] of Object.entries(v)) { const p = at ? `${at}.${k}` : k; paths.add(p); walk(x, p); } };
  for (const r of results) walk(r.bridge, "");
  for (const p of probes) walk(p, "");
  const has = (pat: string) => { const P = pat.split("."); return [...paths].some((c) => { const C = c.split("."); return C.length === P.length && P.every((s, i) => s === "*" || s === C[i]); }); };
  const bad: string[] = [];
  for (const [k, v] of Object.entries(KEYS)) {
    if (!v.at?.length && !v.not) bad.push(`${k}: neither produced nor listed with a reason (${v.sites[0]})`);
    for (const pat of v.at ?? []) if (!has(pat)) bad.push(`${k}: listed as produced at ${pat}, but no world's bridge/scene has it (${v.sites[0]})`);
  }
  expect(bad).toEqual([]);
});
