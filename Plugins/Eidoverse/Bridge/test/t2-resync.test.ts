// T2 resync: a lost log message or a purge makes one re-join; the fold and the scene agree with a fresh joiner's (ADAPTER-DESIGN §1.1, §5).
import { test, expect, beforeAll, afterAll } from "bun:test";
import { startServer, startBridge, startProxy, renderer, person, until, type Server, type Bridge, type Proxy, type Sock } from "./harness.ts";
import { join } from "./fixtures/renderer.ts";
import { create } from "../engine/shared/protocol/requests.js";
import { makeReplica } from "../engine/shared/protocol/replica.js";
import { viewHash } from "../src/session.ts";

let srv: Server, br: Bridge, px: Proxy, r: Sock, ann: Awaited<ReturnType<typeof person>>;
const make = (x: number) => { const c = create(null, { model: "eidoverse/assets/models/lamp.glb", transform: { pos: [x, 0, 0] } }); return ann.req(c.verb, c.args); };
const status = async () => { const k = r.msgs.length; r.send({ type: "bridge/status" }); return r.wait("bridge/status", (m) => m.type === "bridge/status", k); };
/** A fresh joiner's fold of the world, now: the reference. */
async function fresh() {
  const p = await person(srv.url, "glade", `Ref${Math.random().toString(36).slice(2, 7)}`);
  const rep = makeReplica(); rep.adopt(p.snap.cut); p.close(); return { head: rep.head, hash: viewHash(rep.view()) };
}
const joinsUp = (k: number) => px.up.slice(k).map((x) => JSON.parse(x)).filter((m) => m.type === "join");

beforeAll(async () => {
  srv = await startServer(); px = await startProxy(srv.url); br = await startBridge();
  ann = await person(srv.url, "glade", "Ann");
  r = await renderer(br, px.url); r.send(join("glade", "Rae").raw);
  await r.wait("bridge/ready", (m) => m.type === "bridge/ready");
  expect((await make(1))?.ok).toBe(true);
});
afterAll(() => { ann?.close(); br?.stop(); px?.stop(); srv?.stop(); });

test("T2a a lost log message (a gap): one re-join, one snapshot, and the fold matches a fresh joiner's", async () => {
  await until("the bridge has folded the first create", async () => (await status()).head === (await fresh()).head);
  const scene = async (reset = false) => { const k = r.msgs.length; r.send({ type: "bridge/scene", version: 2, ...(reset ? { reset } : {}) }); return r.wait("bridge/scene", (m) => m.type === "bridge/scene", k); };
  const before = (await scene(true)).upserts.map((u: any) => u.id);
  const ku = px.up.length, kr = r.msgs.length, kd = px.down.length;
  px.drop((raw) => JSON.parse(raw).type === "log");
  px.hold((raw) => JSON.parse(raw).type === "join");   // the re-join is slow to reach the server…
  expect((await make(2))?.ok).toBe(true);   // its log is lost
  expect((await make(3))?.ok).toBe(true);   // the gap shows here: the bridge re-joins
  await until("the bridge sent its re-join", () => px.held() > 0);
  expect((await make(4))?.ok).toBe(true);   // …so this log arrives before the fresh snapshot: inside the coming cut, no second re-join
  await until("the bridge has had make(4)'s log", () => px.down.slice(kd).filter((x) => JSON.parse(x).type === "log").length >= 2);   // 3's and 4's (2's was dropped)
  px.release();
  await r.wait("a fresh snapshot", (m) => m.type === "snapshot", kr);
  await r.wait("bridge/ready again", (m) => m.type === "bridge/ready", kr);
  const ref = await until("the bridge catches up", async () => { const f = await fresh(); const s = await status(); return s.head === f.head ? { f, s } : null; });
  expect(ref.s.viewHash).toBe(ref.f.hash);
  const joins = joinsUp(ku);
  expect(joins.length).toBe(1);
  expect(joins[0]).toEqual({ ...JSON.parse(join("glade", "Rae").raw), replica: true, clock: true });
  expect(r.since(kr).filter((m) => m.type === "snapshot").length).toBe(1);
  expect(ref.s.stats.resyncs).toBeGreaterThanOrEqual(1);
  // the scene the renderer holds stays consistent across the resync: the next poll is just what changed
  const next = await scene();
  expect(next.removed).toEqual([]);
  expect(next.upserts.length).toBe(3);
  expect(next.upserts.every((u: any) => !before.includes(u.id))).toBe(true);
  const all = (await scene(true)).upserts.map((u: any) => u.id).sort();
  expect(all).toEqual([...before, ...next.upserts.map((u: any) => u.id)].sort());
});

test("T2b a purge (retroactive by law): one re-join and a fresh snapshot", async () => {
  const s0 = await status();
  const ku = px.up.length, kr = r.msgs.length;
  px.inject(JSON.stringify({ type: "log", world: "glade", from: s0.head + 1, entries: [{ kind: "purge", actor: "system", ts: Date.now(), body: { of: [0] } }] }));
  await r.wait("a fresh snapshot", (m) => m.type === "snapshot", kr);
  await r.wait("bridge/ready again", (m) => m.type === "bridge/ready", kr);
  expect(joinsUp(ku).length).toBe(1);
  expect((await make(5))?.ok).toBe(true);
  const ref = await until("the bridge folds on from the new cut", async () => { const f = await fresh(); const s = await status(); return s.head === f.head ? { f, s } : null; });
  expect(ref.s.viewHash).toBe(ref.f.hash);
  expect(r.since(kr).filter((m) => m.type === "snapshot").length).toBe(1);
});
