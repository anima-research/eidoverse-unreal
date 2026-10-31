// T9 seats: bridge/scene lists every seat (a socket child with a station) with who sits in it, from the frames; sitting is the authority's to allow (ADAPTER-DESIGN §3.5 mount row, §3.6, §5).
import { test, expect, beforeAll, afterAll } from "bun:test";
import { startServer, startBridge, startProxy, renderer, person, until, type Server, type Bridge, type Proxy, type Sock } from "./harness.ts";
import { join } from "./fixtures/renderer.ts";
import { sit, move, pose } from "./fixtures/bodies.ts";

const LAMP = "eidoverse/assets/models/lamp.glb";
let srv: Server, br: Bridge, px: Proxy, r: Sock, ann: Awaited<ReturnType<typeof person>>, bench = "", seat = "", crate = "";

async function ask(f: { id: string; raw: string }) {
  const k = r.msgs.length; r.send(f.raw);
  const m = await r.wait(`the reply to ${f.id}`, (x) => x.type === "reply" && x.id === f.id, k);
  expect(r.raw).toContain(px.down.find((x) => { const y = JSON.parse(x); return y.type === "reply" && y.id === f.id; })!);
  return m.reply;
}
const scene = async () => { const k = r.msgs.length; r.send({ type: "bridge/scene", version: 2, reset: true }); return r.wait("bridge/scene", (m) => m.type === "bridge/scene", k); };
/** bridge/ride or bridge/unride: the bridge's own request, its reply as bridge/ridden; nothing of the renderer's goes up. */
let rq = 0;
async function ridden(type: "bridge/ride" | "bridge/unride", args: Record<string, unknown>) {
  const req = `rd-${++rq}`, k = r.msgs.length; r.send({ type, req, ...args });
  const m = await r.wait(`bridge/ridden ${req}`, (x) => x.type === "bridge/ridden" && x.req === req, k);
  expect(m.fn).toBe(type === "bridge/ride" ? "ride" : "unride");
  return m.reply;
}
const seatsUntil = (what: string, want: (s: any[]) => boolean) => until(what, async () => { const s = (await scene()).seats; return Array.isArray(s) && want(s) ? s : null; });

beforeAll(async () => {
  srv = await startServer(); px = await startProxy(srv.url); br = await startBridge();
  ann = await person(srv.url, "glade", "Ann");
  const b = await ann.req("create", { model: LAMP, components: { transform: { pos: [2, 0, 0] }, sockets: { seat: { pos: [0, 0.5, 0], body: { action: "sit", pose: "sitchair" } }, pannier: { pos: [0.4, 0.8, 0] } } } });
  bench = b.refs[0].id; seat = b.notes.find((x: any) => x.legacy === "sockets").wrote.seat;
  crate = (await ann.req("create", { model: LAMP, components: { transform: { pos: [-1, 0, 0] } } })).refs[0].id;
  r = await renderer(br, px.url); r.send(join("glade", "Rae").raw);
  await r.wait("bridge/ready", (m) => m.type === "bridge/ready");
});
afterAll(() => { ann?.close(); br?.stop(); px?.stop(); srv?.stop(); });

test("T9a the scene lists the bench's seat (a socket with a station), empty; a socket with no station is no seat", async () => {
  const s = await seatsUntil("the seat listed", (x) => x.length > 0);
  expect(s).toEqual([{ seat, host: bench, name: "seat", action: "sit", pose: "sitchair", occupant: null, thing: null }]);
});

test("T9b a far seat is refused in the authority's words; near, sitting is allowed (bridge/ride, or the raw use), and the frames' seat fills the occupant", async () => {
  expect((await ask(move([-3, 0, 0]))).ok).toBe(true);
  const far = await ridden("bridge/ride", { to: bench, socket: "seat" });
  expect([far.ok, far.refused?.code]).toEqual([false, "out-of-reach"]);
  expect(far.refused.reason).toMatch(/a seat is within 3\.5 m to sit on/);
  for (const p of [[0, 0, 0], [1.5, 0, 0]]) await until(`Rae walks to ${p}`, async () => (await ask(move(p))).ok, 8000, 150);   // a body covers 6 m/s
  const ok = await ridden("bridge/ride", { to: bench, socket: "seat" });
  expect([ok.ok, ok.attach?.to, ok.attach?.host, ok.attach?.pose]).toEqual([true, seat, bench, "sitchair"]);
  r.send(pose([2, 0.5, 0], { to: ok.attach.to, pose: ok.attach.pose }).raw);
  await seatsUntil("Rae in the seat", (x) => x[0]?.occupant === "Rae");
});

test("T9c someone else's sit on a taken seat is refused by the authority (one body per socket); the raw use (EidoverseSeats.cpp:121-123's request form) is judged alike", async () => {
  await ann.req("move", { to: [1.8, 0, 0.8] });
  const no = await ann.req("use", { id: bench, action: "sit", args: { socket: "seat" } });
  expect([no.ok, no.refused?.code]).toEqual([false, "occupied"]);
  expect(no.refused.reason).toMatch(/Rae sits in/);
  const again = await ask(sit(bench, "seat"));
  expect(again.ok).toBe(true);
});

test("T9d standing up (EidoverseSeats.cpp:164-175: a pose with no seat, then the landing): the seat is empty again", async () => {
  r.send(pose([2, 0, 0.7]).raw);
  await until("Rae steps off", async () => (await ridden("bridge/unride", { pos: [2, 0, 0.7] })).ok, 8000, 150);
  await seatsUntil("the seat empty", (x) => x[0]?.occupant === null);
});

test("T9e a thing put in the seat (EidoverseSeats.cpp:268-280 mount {id, to, slot} → bridge/ride {id, to, socket}) shows as its thing, and leaves it on unride (:285-297)", async () => {
  expect((await ridden("bridge/ride", { id: crate, to: bench, socket: "seat" })).ok).toBe(true);
  await seatsUntil("the crate in the seat", (x) => x[0]?.thing === crate);
  expect((await ridden("bridge/unride", { id: crate, pos: [-1, 0, 0] })).ok).toBe(true);
  await seatsUntil("the seat empty of things", (x) => x[0]?.thing === null);
});

test("T9f a ride the authority refuses comes back in its words (a thing onto a taken seat is the authority's call, never the bridge's)", async () => {
  const no = await ridden("bridge/ride", { id: crate, to: "nothing-here" });
  expect(no.ok).toBe(false);
  expect(typeof no.refused?.reason).toBe("string");
});
