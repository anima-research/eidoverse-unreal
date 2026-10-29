// T0 join: refused as sent, admitted through the bridge as a replica; plus the process and local-socket rules (ADAPTER-DESIGN §1.1, §5).
import { test, expect, beforeAll, afterAll } from "bun:test";
import { startServer, startBridge, startProxy, renderer, openSock, person, until, type Server, type Bridge, type Proxy } from "./harness.ts";
import { join } from "./fixtures/renderer.ts";
import { create } from "../engine/shared/protocol/requests.js";

let srv: Server, br: Bridge, px: Proxy;
beforeAll(async () => { srv = await startServer(); px = await startProxy(srv.url); br = await startBridge(); });
afterAll(() => { br?.stop(); px?.stop(); srv?.stop(); });

test("T0a the renderer's join sent as is (a pass-everything hop) is refused: error, then close 4005", async () => {
  const s = await openSock(px.url);
  s.send(join("glade", "Una").raw);
  const closed = await s.waitClosed();
  expect(closed.code).toBe(4005);
  expect(s.msgs.some((m) => m?.type === "error")).toBe(true);
});

test("T0b through the bridge: snapshot (its cut adopted, not forwarded) then bridge/ready; the join went up with replica and clock", async () => {
  const k = px.up.length;
  const r = await renderer(br, px.url);
  const j = join("glade", "Bea");
  r.send(j.raw);
  const snap = await r.wait("a snapshot", (m) => m.type === "snapshot");
  const ready = await r.wait("bridge/ready", (m) => m.type === "bridge/ready");
  expect(snap.you).toBe("Bea");
  expect("cut" in snap).toBe(false);
  expect(r.msgs.indexOf(ready)).toBeGreaterThan(r.msgs.indexOf(snap));
  expect(ready.world).toBe("glade");
  expect(Number.isInteger(ready.head) && ready.head >= 0).toBe(true);
  const sent = px.up.slice(k).map((x) => JSON.parse(x)).find((m) => m.type === "join");
  expect(sent).toEqual({ ...JSON.parse(j.raw), replica: true, clock: true });
  expect(r.closed).toBe(null);
  // the server treats it as a replica: someone else's change reaches the bridge's fold
  const ann = await person(srv.url, "glade", "Ann");
  const before = ready.head;
  const c = create(null, { model: "eidoverse/assets/models/lamp.glb", transform: { pos: [1, 0, 1] } });
  const made = await ann.req(c.verb, c.args);
  expect(made?.ok).toBe(true);
  const st = await until("the bridge folds Ann's create", async () => {
    const k2 = r.msgs.length; r.send({ type: "bridge/status" });
    const m = await r.wait("bridge/status", (x) => x.type === "bridge/status", k2); return m.head > before ? m : null; });
  expect(st.entities).toBeGreaterThan(0);
  ann.close(); r.close();
});

test("T0c the local socket: no token, a wrong token, or an Origin header is refused", async () => {
  const bare = await openSock(`ws://127.0.0.1:${br.port}/ws`);
  expect(bare.opened).toBe(false);
  const wrong = await openSock(`ws://127.0.0.1:${br.port}/ws`, { protocols: [`eido-bridge.${"0".repeat(64)}`] });
  expect(wrong.opened).toBe(false);
  const page = await openSock(`ws://127.0.0.1:${br.port}/ws`, { protocols: [`eido-bridge.${br.token}`], headers: { Origin: "https://example.com" } });
  expect(page.opened).toBe(false);
  const ok = await openSock(`ws://127.0.0.1:${br.port}/ws`, { protocols: [`eido-bridge.${br.token}`] });
  expect(ok.opened).toBe(true);
  expect(ok.ws.protocol).toBe(`eido-bridge.${br.token}`);
  ok.close();
});

test("T0d a server close is mirrored to the renderer, code and reason; a cut is a cut (1006)", async () => {
  for (const [code, reason] of [[4010, "you were removed from this world"], [4011, "the world moved"], [4002, "replaced"]] as const) {
    const r = await renderer(br, px.url);
    r.send(join("glade", `C${code}`).raw);
    await r.wait("bridge/ready", (m) => m.type === "bridge/ready");
    px.closeClients(code, reason);
    const c = await r.waitClosed();
    expect(c).toEqual({ code, reason });
  }
  const r = await renderer(br, px.url);
  r.send(join("glade", "Cut").raw);
  await r.wait("bridge/ready", (m) => m.type === "bridge/ready");
  px.cutClients();
  expect((await r.waitClosed()).code).toBe(1006);
});

test("T0e a hello naming a non-loopback cleartext server, or carrying URL credentials, is refused", async () => {
  for (const server of ["ws://example.com/ws", "wss://user:pw@example.com/ws", "wss://example.com/ws?token=x", "http://127.0.0.1:1/ws"]) {
    const s = await openSock(`ws://127.0.0.1:${br.port}/ws`, { protocols: [`eido-bridge.${br.token}`] });
    s.send({ type: "bridge/hello", server });
    const e = await s.wait("bridge/error", (m) => m.type === "bridge/error");
    expect(e.for).toBe("bridge/hello");
    await s.waitClosed();
  }
});

test("T0f the process: one port line on stdout; it exits when stdin closes (its parent is gone)", async () => {
  const b = await startBridge();
  expect(b.stdout()).toMatch(/^\{"port":\d+\}\n$/);
  b.endStdin();
  const code = await Promise.race([b.proc.exited, new Promise((r) => setTimeout(() => r("still running"), 5000))]);
  expect(code).toBe(0);
});

test("T0g a bad token on stdin: the bridge refuses to start", async () => {
  await expect(startBridge({ token: "short" })).rejects.toThrow(/exited/);
});
