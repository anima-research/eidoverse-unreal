// T6 build writes: the renderer's build sites as bridge/do → the engine's requests.js requests; `$b<n>` → the minted id; refusals are the authority's (ADAPTER-DESIGN §3.5, §5).
import { test, expect, beforeAll, afterAll } from "bun:test";
import path from "node:path";
import { startServer, startBridge, startProxy, renderer, person, until, ROOT, type Server, type Bridge, type Proxy, type Sock } from "./harness.ts";
import { join } from "./fixtures/renderer.ts";
import { creates, edits, lightEdit, asset, removeOf } from "./fixtures/writes.ts";
import * as R from "../engine/shared/protocol/requests.js";
import { enc } from "../engine/shared/protocol/names.js";
import { makeReplica } from "../engine/shared/protocol/replica.js";
import { BASE } from "../engine/shared/protocol/roles.js";

let srv: Server, br: Bridge, px: Proxy, r: Sock, ann: Awaited<ReturnType<typeof person>>, you: string;
const ups = (k: number, verb?: string) => px.up.slice(k).map((x) => JSON.parse(x)).filter((m) => m.type === "request" && (!verb || m.verb === verb));
const done = (req: string, k = 0) => r.wait(`bridge/done ${req}`, (m) => (m.type === "bridge/done" || m.type === "bridge/error") && m.req === req, k);
const scene = async (reset = false) => { const k = r.msgs.length; r.send({ type: "bridge/scene", version: 2, ...(reset ? { reset } : {}) }); return r.wait("bridge/scene", (m) => m.type === "bridge/scene", k); };

beforeAll(async () => {
  srv = await startServer({ seed: (W) => {   // + an imported world whose ids need enc
    const t = Bun.spawnSync(["bun", path.join(ROOT, "test/oracle/translate.ts"), path.join(ROOT, "test/fixtures/writes-worlds/odd-ids"), "odd-ids"], { cwd: ROOT,
      env: { ...process.env, WORLDS_DIR: W, OPT_DIR: path.join(path.dirname(W), "opt"), BUN_RUNTIME_TRANSPILER_CACHE_PATH: "0" } });
    if (t.exitCode !== 0) throw new Error(`translate odd-ids: ${t.stderr.toString().slice(-600)}`); } });
  px = await startProxy(srv.url); br = await startBridge();
  ann = await person(srv.url, "glade", "Ann");
  r = await renderer(br, px.url); r.send(join("glade", "Rae").raw);
  you = (await r.wait("bridge/ready", (m) => m.type === "bridge/ready")).you;
});
afterAll(() => { ann?.close(); br?.stop(); px?.stop(); srv?.stop(); });

test("T6a each create fixture goes up as requests.js create; the reply's ids give the minted id, and the scene draws it there", async () => {
  for (const f of creates(1)) {
    const k = r.msgs.length, ku = px.up.length;
    r.send(f.m);
    const a = await done(f.m.req, k);
    expect([f.src, a.type, a.fn, a.reply?.ok]).toEqual([f.src, "bridge/done", "create", true]);
    const want = R.create(f.m.ref, { model: f.m.model ?? null, transform: f.m.transform, components: f.m.components ?? {} });
    const up = ups(ku);
    expect(up.length).toBe(1);
    expect({ verb: up[0].verb, args: up[0].args }).toEqual(want);
    const id = a.reply.ids?.[f.m.ref];
    expect(typeof id).toBe("string");
    expect(id).not.toBe(f.m.ref);
    const s = await until(`${f.src}: the scene has ${id}`, async () => { const s = await scene(true); return s.upserts.some((x: any) => x.id === id) ? s : null; });
    expect(s.upserts.find((x: any) => x.id === id).lib).toBe(f.m.model ?? "");
    expect(s.transforms.find((x: any) => x.id === id).p).toEqual(f.m.transform.pos);
  }
});

/** A thing made through the bridge: its minted id. */
async function made(ref: string, what: Record<string, unknown> = { model: "eidoverse/assets/models/lamp.glb" }) {
  const k = r.msgs.length; r.send({ type: "bridge/do", req: `mk${ref}`, fn: "create", ref, ...what, transform: { pos: [1, 0, 1] } });
  return (await done(`mk${ref}`, k)).reply.ids[ref] as string;
}
const fresh = async () => { const rep = makeReplica(); const p = await person(srv.url, "glade", `V${Math.random().toString(36).slice(2, 7)}`); rep.adopt(p.snap.cut); p.close(); return rep; };
const viewOf = async (id: string) => (await fresh()).view().entities[id];
/** The bridge has folded everything a fresh joiner sees. */
const caughtUp = async () => { const h = (await fresh()).head; await until("the bridge catches up", async () => { const k = r.msgs.length; r.send({ type: "bridge/status" }); return (await r.wait("status", (m) => m.type === "bridge/status", k)).head >= h; }); };

test("T6b each edit fixture goes up as its requests.js request, built from the bridge's view, and lands", async () => {
  const id = await made("$e1"), light = await made("$e2", { components: { light: { color: 16767392, intensity: 16, range: 10 } } });
  await until("the bridge folded them", async () => { const s = await scene(true); return [id, light].every((i) => s.upserts.some((x: any) => x.id === i)); });
  for (const f of [...edits(id), lightEdit(light)]) {
    const id = f.m.id;
    await caughtUp(); const before = await viewOf(id);   // the bridge builds a whole transform from its view, as the browser does
    const k = r.msgs.length, ku = px.up.length;
    r.send(f.m);
    const a = await done(f.m.req, k);
    expect([f.src, a.type, a.reply?.ok, a.reply?.refused]).toEqual([f.src, "bridge/done", true, undefined]);
    const want = f.m.fn === "setTransform" ? R.setTransform(before, id, f.m.patch) : f.m.fn === "set" ? R.set(id, f.m.component, f.m.value, { merge: !!f.m.merge }) : R.unset(id, f.m.component);
    const up = ups(ku);
    expect([f.src, up.length, { verb: up[0].verb, args: up[0].args }]).toEqual([f.src, 1, want]);
    const after = await viewOf(id);
    if (f.m.fn === "setTransform") expect([f.src, after.pos]).toEqual([f.src, f.m.patch.pos]);
    if (f.m.fn === "set") expect({ [f.src]: after[f.m.component] }).toMatchObject({ [f.src]: f.m.value });   // the authority fills defaults in
    if (f.m.fn === "unset") expect(after[f.m.component]).toBeUndefined();
  }
  // asset: @world assets:<path> (client/lib/generate.js keys it so). A builder is refused by the authority (needs generate), then may
  let k = r.msgs.length, ku = px.up.length;
  const wantAsset = R.set("@world", `assets:${enc(asset.m.path)}`, { name: asset.m.name, path: asset.m.path });
  r.send(asset.m);
  expect((await done(asset.m.req, k)).reply).toMatchObject({ ok: false, refused: { code: "rights", needs: "generate" } });
  expect(ups(ku).map((m) => ({ verb: m.verb, args: m.args }))).toEqual([wantAsset]);
  expect((await ann.req("set", { id: "@world", component: `roles:${you}`, value: { role: "builder", can: { generate: true } } }))?.ok).toBe(true);
  await caughtUp();
  k = r.msgs.length; ku = px.up.length;
  r.send(asset.m);
  expect((await done(asset.m.req, k)).reply).toMatchObject({ ok: true });
  expect(ups(ku).map((m) => ({ verb: m.verb, args: m.args }))).toEqual([wantAsset]);
  // remove: delete {id}; the scene drops it
  const x = removeOf(id); k = r.msgs.length; ku = px.up.length;
  r.send(x.m);
  expect((await done(x.m.req, k)).reply?.ok).toBe(true);
  expect(ups(ku).map((m) => ({ verb: m.verb, args: m.args }))).toEqual([R.remove(id)]);
  await until("the scene drops it", async () => !(await scene(true)).upserts.some((u: any) => u.id === id));
});

test("T6b' a setTransform on a thing the bridge doesn't hold is answered locally (it can't build the whole transform); nothing goes up", async () => {
  const k = r.msgs.length, ku = px.up.length;
  r.send({ type: "bridge/do", req: "gone", fn: "setTransform", id: "no-such-thing", patch: { pos: [0, 0, 0] } });
  expect(await done("gone", k)).toMatchObject({ type: "bridge/error", for: "bridge/do", fn: "setTransform", code: "not-here" });
  expect(ups(ku)).toEqual([]);
});

test("T6d ids that need enc: an imported world's Lamp_1 and 'chaise longue ☼', named in view spelling, go up escaped (names.js enc) and land", async () => {
  const w = await renderer(br, srv.url); w.send(join("odd-ids", "Rae").raw);
  await w.wait("bridge/ready", (m) => m.type === "bridge/ready");
  const kw = w.msgs.length; w.send({ type: "bridge/scene", version: 2, reset: true });
  const ids = (await w.wait("bridge/scene", (m) => m.type === "bridge/scene", kw)).upserts.map((u: any) => u.id).sort();
  expect(ids).toEqual(["Lamp_1", "chaise longue ☼"].sort());
  for (const [i, id] of ["Lamp_1", "chaise longue ☼"].entries()) {
    expect(enc(id)).toMatch(/^x--/);
    for (const m of [{ fn: "setTransform", id, patch: { pos: [i, 0, 5] } }, { fn: "set", id, component: "label", value: `odd ${i}` }, ...(i ? [{ fn: "remove", id }] : [])]) {
      const k = w.msgs.length; w.send({ type: "bridge/do", req: `o${i}${m.fn}`, ...m });
      const a = await w.wait(`${id} ${m.fn}`, (x) => (x.type === "bridge/done" || x.type === "bridge/error") && x.req === `o${i}${m.fn}`, k);
      expect([id, m.fn, a.type, a.reply?.ok, a.reply?.refused]).toEqual([id, m.fn, "bridge/done", true, undefined]);
    }
  }
  const p = await person(srv.url, "odd-ids", "Ody"); const rep = makeReplica(); rep.adopt(p.snap.cut); p.close();
  const E = rep.view().entities;
  expect([E["Lamp_1"]?.pos, E["Lamp_1"]?.label, "chaise longue ☼" in E]).toEqual([[0, 0, 5], "odd 0", false]);
  w.close();
});

test("T6c a refused create: the request goes up, the authority's refusal comes back as it is, no id is minted, nothing is drawn", async () => {
  expect((await ann.req("set", { id: "@world", component: `roles:${you}`, value: { role: BASE } }))?.ok).toBe(true);
  await caughtUp();
  const f = creates(20)[0], k = r.msgs.length, ku = px.up.length, before = (await scene(true)).upserts.length;
  r.send(f.m);
  const a = await done(f.m.req, k);
  expect(a).toMatchObject({ type: "bridge/done", fn: "create", reply: { ok: false, refused: { code: "rights" } } });
  expect(a.reply.refused.reason).toMatch(new RegExp(BASE));
  expect(a.reply.ids).toBeUndefined();
  expect(ups(ku, "create").length).toBe(1);   // the bridge never pre-empts (T5): the authority decided
  await caughtUp();
  expect((await scene(true)).upserts.length).toBe(before);
});
