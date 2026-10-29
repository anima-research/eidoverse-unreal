// T8 holding: grab/put/throw/drop/punt go up as requests the authority judges; its holds mark the item `held` in bridge/scene, drawn at the holder's hand from their frames (ADAPTER-DESIGN §3.6, §5).
import { test, expect, beforeAll, afterAll } from "bun:test";
import { startServer, startBridge, startProxy, renderer, person, until, type Server, type Bridge, type Proxy, type Sock } from "./harness.ts";
import { join } from "./fixtures/renderer.ts";
import { grab, put, throwIt, drop, punt, move, pose } from "./fixtures/bodies.ts";
import { HAND_Y } from "../engine/shared/protocol/physconst.js";

const LAMP = "eidoverse/assets/models/lamp.glb";
let srv: Server, br: Bridge, px: Proxy, r: Sock, ann: Awaited<ReturnType<typeof person>>, raeId = "", annId = "", ball = "", jar = "";

/** Send a request fixture; its reply as the renderer got it, checked to be the server's bytes. */
async function ask(s: Sock, f: { id: string; raw: string }) {
  const k = s.msgs.length; s.send(f.raw);
  const m = await s.wait(`the reply to ${f.id}`, (x) => x.type === "reply" && x.id === f.id, k);
  expect(s.raw).toContain(px.down.find((x) => { const y = JSON.parse(x); return y.type === "reply" && y.id === f.id; })!);
  return m.reply;
}
const scene = async (s: Sock) => { const k = s.msgs.length; s.send({ type: "bridge/scene", version: 2, reset: true }); return s.wait("bridge/scene", (m) => m.type === "bridge/scene", k); };
const placed = (sc: any, id: string) => sc.transforms.find((t: any) => t.id === id);
const near = (a: number[], b: number[]) => a.every((x, i) => Math.abs(x - b[i]) < 1e-6);
/** Until the scene shows `id` as `want` says. */
const sceneUntil = (s: Sock, what: string, id: string, want: (t: any) => boolean) => until(what, async () => { const t = placed(await scene(s), id); return t && want(t) ? t : null; });

beforeAll(async () => {
  srv = await startServer(); px = await startProxy(srv.url); br = await startBridge();
  ann = await person(srv.url, "glade", "Ann");
  ball = (await ann.req("create", { model: LAMP, components: { transform: { pos: [0, 0, 0.5] }, grabbable: {} } })).refs[0].id;
  jar = (await ann.req("create", { model: LAMP, components: { transform: { pos: [0.4, 0, 0.4] }, grabbable: { steal: false } } })).refs[0].id;
  r = await renderer(br, px.url); r.send(join("glade", "Rae").raw);
  raeId = (await r.wait("bridge/ready", (m) => m.type === "bridge/ready")).you;
  annId = (await ann.req("whois", { name: "Ann" })).people[0].id;
  expect((await ask(r, move([0, 0, 0]))).ok).toBe(true);
  r.send(pose([0.1, 0, 0.05]).raw);
});
afterAll(() => { ann?.close(); br?.stop(); px?.stop(); srv?.stop(); });

test("T8a grab: the authority's holds pass through as it sent them, and the scene draws the item held {by, hand} at the holder's hand (frame pose + the authority's HAND_Y)", async () => {
  const kd = px.down.length;
  const g = await ask(r, grab(ball));
  expect([g.ok, g.hand]).toEqual([true, "right"]);
  const holds = await until("a holds message", () => px.down.slice(kd).find((x) => JSON.parse(x).type === "holds" && JSON.parse(x).holds[ball]));
  await until("the renderer has it", () => r.raw.includes(holds));
  const t = await sceneUntil(r, "the ball drawn at Rae's hand", ball, (x) => x.held && near(x.p, [0.1, HAND_Y, 0.05]));
  expect(t.held).toEqual({ by: raeId, hand: "right" });
});

test("T8b taking what another holds is the authority's to refuse (grabbable.steal false), in its words; the jar is drawn at its holder's hand", async () => {
  await ann.req("move", { to: [0.3, 0, 0.2] });
  ann.send({ type: "pose", pose: { p: [0.3, 0, 0.2], q: [0, 0, 0, 1], yaw: 0, speed: 0, clip: "idle" } });
  expect((await ann.req("grab", { id: jar }))?.ok).toBe(true);
  const no = await ask(r, grab(jar));
  expect([no.ok, no.refused?.code]).toEqual([false, "held"]);
  expect(no.refused.reason).toMatch(/Ann is holding/);
  const t = await sceneUntil(r, "the jar at Ann's hand", jar, (x) => x.held?.by === annId && near(x.p, [0.3, HAND_Y, 0.2]));
  expect(t.held.hand).toBe("right");
});

test("T8c a renderer joining now sees what is held from its join cut", async () => {
  const r2 = await renderer(br, px.url); r2.send(join("glade", "Ike").raw);
  await r2.wait("bridge/ready", (m) => m.type === "bridge/ready");
  const sc = await scene(r2);
  expect([placed(sc, ball)?.held?.by, placed(sc, jar)?.held?.by]).toEqual([raeId, annId]);
  r2.close();
});

test("T8d put: the item is let go where the authority set it, and isn't drawn held", async () => {
  const p = await ask(r, put(ball));
  expect(p.ok).toBe(true);
  const t = await sceneUntil(r, "the ball let go", ball, (x) => !x.held);
  expect(near(t.p, p.settled.pos)).toBe(true);
});

test("T8e throw, drop and punt are the authority's: answered in its words (this test server runs no physics: a throw is set down, a punt is refused)", async () => {
  expect((await ask(r, grab(ball))).ok).toBe(true);
  const th = await ask(r, throwIt(ball, [0, 2, 1]));
  expect([th.ok, th.notes?.[0]?.thrown]).toEqual([true, "set down"]);
  expect((await ask(r, grab(ball))).ok).toBe(true);
  expect((await ask(r, drop(ball))).ok).toBe(true);
  const pu = await ask(r, punt(ball, [0, 0, 1], 4));
  expect([pu.ok, pu.refused?.code]).toEqual([false, "no-physics"]);
  await sceneUntil(r, "the ball not held", ball, (x) => !x.held);
});
