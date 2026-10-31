// The bridge/* table: answered locally, never sent up; the bridge's own requests stay its own (ADAPTER-DESIGN §1, §6).
import { test, expect, beforeAll, afterAll } from "bun:test";
import { startServer, startBridge, startProxy, renderer, until, type Server, type Bridge, type Proxy, type Sock } from "./harness.ts";
import { join } from "./fixtures/renderer.ts";
import { table, SLICES } from "../src/slices.ts";
import { Session } from "../src/session.ts";

let srv: Server, br: Bridge, px: Proxy, r: Sock;
beforeAll(async () => {
  srv = await startServer(); px = await startProxy(srv.url); br = await startBridge();
  r = await renderer(br, px.url); r.send(join("glade", "Rae").raw);
  await r.wait("bridge/ready", (m) => m.type === "bridge/ready");
});
afterAll(() => { br?.stop(); px?.stop(); srv?.stop(); });

test("H1 an unknown bridge/* is refused; none reach the server", async () => {
  const k = r.msgs.length, ku = px.up.length;
  const want: Record<string, string> = {};   // every slice is built: no stubs left
  for (const t of Object.keys(want)) r.send({ type: t });
  r.send({ type: "bridge/nope" });
  r.send({ type: "bridge/do", req: "q1", fn: "nope" });   // slice C: built; an unknown fn is refused by name
  for (const [t, slice] of Object.entries(want)) {
    const e = await r.wait(`the answer to ${t}`, (m) => m.type === "bridge/error" && m.for === t, k);
    expect([e.code, e.reason]).toEqual(["unbuilt", `${t} arrives with slice ${slice}`]);
  }
  expect((await r.wait("the answer to bridge/do nope", (m) => m.type === "bridge/error" && m.for === "bridge/do", k))).toMatchObject({ req: "q1", fn: "nope", code: "unknown-fn" });
  expect((await r.wait("the answer to bridge/nope", (m) => m.type === "bridge/error" && m.for === "bridge/nope", k)).code).toBe("unknown");
  r.send({ type: "bridge/scene", version: 2, reset: true }); expect((await r.wait("bridge/scene", (m) => m.type === "bridge/scene", k)).version).toBe(2);   // slice A: built
  r.send({ type: "bridge/status" }); await r.wait("status", (m) => m.type === "bridge/status", k);
  expect(px.up.slice(ku).filter((x) => x.includes("bridge/"))).toEqual([]);
});

test("H2 a renderer request may not use the bridge's own id space", async () => {
  const k = r.msgs.length, ku = px.up.length;
  r.send({ type: "request", id: "bridge:1", verb: "help", args: {} });
  const e = await r.wait("the refusal", (m) => m.type === "bridge/error" && m.for === "request", k);
  expect(e.code).toBe("reserved-id");
  expect(px.up.slice(ku)).toEqual([]);
});

test("H3 the table refuses a non-bridge local name, a type owned twice, a core type, a tap on snapshot or log", () => {
  const f = () => {};
  expect(() => table([{ name: "x", local: { scene: f } }])).toThrow(/bridge\/\*/);
  expect(() => table([{ name: "x", local: { "bridge/a": f } }, { name: "y", local: { "bridge/a": f } }])).toThrow(/already x's/);
  expect(() => table([{ name: "x", local: { "bridge/status": f } }])).toThrow(/core's/);
  expect(() => table([{ name: "x", server: { log: f } }])).toThrow(/can't tap log/);
  expect(() => table([{ name: "x", server: { snapshot: f } }])).toThrow(/can't tap snapshot/);
  expect(() => table(SLICES)).not.toThrow();
  expect(SLICES.map((s) => s.name)).toEqual(["scene", "chat", "writes", "bodies", "access"]);
});

test("H4 ctx.request: the bridge's own request goes up as {id: bridge:<n>}; its reply resolves it and is not forwarded", async () => {
  const toRenderer: string[] = [], toServer: string[] = [];
  const s = new Session({ send: (x: string) => toRenderer.push(x), close() {}, terminate() {} } as any);
  s.phase = "open"; s.server = { readyState: 1, send: (x: string) => toServer.push(x) } as any;
  const p = s.ctxFor(SLICES[0]).request("help", { about: "create" });
  const up = JSON.parse(toServer[0]);
  expect(up).toEqual({ type: "request", id: "bridge:1", verb: "help", args: { about: "create" } });
  s.fromServer(JSON.stringify({ type: "reply", id: "bridge:1", reply: { ok: true, text: "…" } }));
  expect(await p).toEqual({ ok: true, text: "…" });
  s.fromServer(JSON.stringify({ type: "reply", id: 3, reply: { ok: true } }));   // the renderer's own: forwarded
  expect(toRenderer.map((x) => JSON.parse(x))).toEqual([{ type: "reply", id: 3, reply: { ok: true } }]);
  // a socket that closes answers what's in flight, as net.js failPending does
  const q = s.ctxFor(SLICES[0]).request("help");
  s.serverClosed(1000, "");
  expect((await q).refused?.code).toBe("offline");
});
