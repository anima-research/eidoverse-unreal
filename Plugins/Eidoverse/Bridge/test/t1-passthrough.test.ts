// T1 passthrough: the PASS lanes byte for byte, both ways, checked at a proxy before the server (ADAPTER-DESIGN §3.3-3.4, §5).
import { test, expect, beforeAll, afterAll } from "bun:test";
import { startServer, startBridge, startProxy, renderer, person, until, type Server, type Bridge, type Proxy, type Sock } from "./harness.ts";
import { join, upLanes, downLanes } from "./fixtures/renderer.ts";

let srv: Server, br: Bridge, px: Proxy, r: Sock, ann: Awaited<ReturnType<typeof person>>;
beforeAll(async () => {
  srv = await startServer(); px = await startProxy(srv.url); br = await startBridge();
  ann = await person(srv.url, "glade", "Ann");
  r = await renderer(br, px.url); r.send(join("glade", "Rae").raw);
  await r.wait("bridge/ready", (m) => m.type === "bridge/ready");
});
afterAll(() => { ann?.close(); br?.stop(); px?.stop(); srv?.stop(); });

test("T1a renderer → server: every PASS lane reaches the server byte-identical and in order", async () => {
  const k = px.up.length;
  const lanes = upLanes("Ann");
  for (const f of lanes) r.send(f.raw);
  const got = await until("every lane reached the server", () => { const u = px.up.slice(k); return lanes.every((f) => u.includes(f.raw)) ? u : null; }, 8000)
    .catch(() => px.up.slice(k));
  for (const f of lanes) expect({ lane: f.type, src: f.src, arrived: got.includes(f.raw) }).toEqual({ lane: f.type, src: f.src, arrived: true });
  expect(got.filter((x) => lanes.some((f) => f.raw === x))).toEqual(lanes.map((f) => f.raw));
});

test("T1b server → renderer: every PASS lane reaches the renderer byte-identical and in order", async () => {
  const k = r.raw.length;
  const lanes = downLanes("Ann");
  for (const f of lanes) px.inject(f.raw);
  const got = await until("every lane reached the renderer", () => { const d = r.raw.slice(k); return lanes.every((f) => d.includes(f.raw)) ? d : null; }, 8000)
    .catch(() => r.raw.slice(k));
  for (const f of lanes) expect({ lane: f.type, src: f.src, arrived: got.includes(f.raw) }).toEqual({ lane: f.type, src: f.src, arrived: true });
  expect(got.filter((x) => lanes.some((f) => f.raw === x))).toEqual(lanes.map((f) => f.raw));
});

test("T1c the server's own traffic (arrive, frame, error) reaches the renderer as the server wrote it", async () => {
  const kd = px.down.length;
  const bob = await person(srv.url, "glade", "Bob");
  bob.send({ type: "pose", pose: { p: [1, 0, 1], yaw: 0, speed: 0, clip: "idle" } });
  await until("an arrive and a frame came down", () => { const d = px.down.slice(kd).map((x) => JSON.parse(x).type); return d.includes("arrive") && d.includes("frame"); });
  const passed = px.down.slice(kd).filter((x) => !["snapshot", "log"].includes(JSON.parse(x).type));
  await until("the renderer has each of them", () => passed.every((x) => r.raw.includes(x)));
  expect(passed.every((x) => r.raw.includes(x))).toBe(true);
  bob.close();
});

test("T1d a raw request passes; its reply comes back unchanged (client/lib/net.js:330 sendRequest)", async () => {
  const raw = JSON.stringify({ type: "request", id: 7, verb: "help", args: {} });
  const k = px.up.length, kr = r.raw.length;
  r.send(raw);
  await until("the request reached the server", () => px.up.slice(k).includes(raw));
  const reply = await r.wait("its reply", (m) => m.type === "reply" && m.id === 7, 0);
  const rawReply = px.down.find((x) => { const m = JSON.parse(x); return m.type === "reply" && m.id === 7; });
  expect(r.raw.slice(kr)).toContain(rawReply!);
  expect(reply.reply).toBeTruthy();
});
