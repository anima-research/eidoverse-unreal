// T5 rights stay the authority's: what the renderer may do arrives as the server wrote it, and every write goes up to be judged; the bridge never refuses or grants (ADAPTER-DESIGN §1, §3.4 your-rights row, §5).
import { test, expect, beforeAll, afterAll } from "bun:test";
import { startServer, startBridge, startProxy, renderer, person, until, type Server, type Bridge, type Proxy, type Sock } from "./harness.ts";
import { join } from "./fixtures/renderer.ts";
import { create } from "../engine/shared/protocol/requests.js";

let srv: Server, br: Bridge, px: Proxy, r: Sock, ann: Awaited<ReturnType<typeof person>>, raeId = "";
let n = 0;
/** The spawn of EidoverseUIBuild.cpp:256-262 ({id, lib, pos, yaw}) as a create request; its reply as it reached the renderer and as the server sent it. */
async function spawn() {
  const c = create(null, { model: "eidoverse/assets/models/lamp.glb", transform: { pos: [1, 0, 1] } });
  const id = `spawn-${++n}`, raw = JSON.stringify({ type: "request", id, ...c }), ku = px.up.length, kr = r.raw.length;
  r.send(raw);
  const got = await r.wait(`the reply to ${id}`, (m) => m.type === "reply" && m.id === id);
  const down = px.down.find((x) => { const m = JSON.parse(x); return m.type === "reply" && m.id === id; })!;
  return { got, down, mine: r.raw.slice(kr), up: px.up.slice(ku), raw };
}
/** The latest your-rights the renderer got, checked to be the server's own bytes. */
async function rightsAfter(k: number, kd: number, want: (x: any) => boolean) {
  const m = await r.wait("your-rights", (x) => x.type === "your-rights" && want(x.rights), k);
  const raw = px.down.slice(kd).find((x) => JSON.parse(x).type === "your-rights" && want(JSON.parse(x).rights))!;
  expect(r.raw).toContain(raw);
  return m.rights;
}

beforeAll(async () => {
  srv = await startServer(); px = await startProxy(srv.url); br = await startBridge();
  ann = await person(srv.url, "glade", "Ann");
  expect((await ann.req("set", { id: "@world", component: "default-role", value: { role: "inhabitant" } }))?.ok).toBe(true);
  expect((await ann.req("set", { id: "@world", component: "role-defs", value: { crew: { name: "Crew", rank: 50, can: { build: true } } } }))?.ok).toBe(true);
  r = await renderer(br, px.url); r.send(join("glade", "Rae").raw);
  raeId = (await r.wait("bridge/ready", (m) => m.type === "bridge/ready")).you;
});
afterAll(() => { ann?.close(); br?.stop(); px?.stop(); srv?.stop(); });

test("T5a the join's rights are the server's: snapshot.yourRights reaches the renderer as sent", async () => {
  const snap = r.msgs.find((m) => m?.type === "snapshot");
  const sent = JSON.parse(px.down.find((x) => JSON.parse(x).type === "snapshot")!);
  expect(snap.yourRights).toEqual(sent.yourRights);
  expect([snap.yourRights.role, snap.yourRights.permissions]).toEqual(["inhabitant", []]);
});

test("T5b an inhabitant's create still goes up and comes back refused in the authority's words; the bridge sends nothing of its own", async () => {
  const s = await spawn();
  expect(s.up).toEqual([s.raw]);
  expect(s.mine).toContain(s.down);
  expect(s.got.reply.ok).toBe(false);
  expect(s.got.reply.refused.code).toBe("rights");
  expect(s.mine.some((x) => x.includes("bridge/error"))).toBe(false);
});

test("T5c a custom role with build, and a moderator: the renderer reads permissions[] as the server sent them, and the authority lets it build", async () => {
  for (const role of ["crew", "moderator"]) {
    const k = r.msgs.length, kd = px.down.length;
    expect((await ann.req("set", { id: "@world", component: `roles:${raeId}`, value: { role } }))?.ok).toBe(true);
    const rights = await rightsAfter(k, kd, (x) => x?.role === role);
    expect(rights.permissions).toContain("build");
    const s = await spawn();
    expect([role, s.got.reply.ok]).toEqual([role, true]);
    expect(s.mine).toContain(s.down);
  }
});

test("T5d taken away again: the next create is refused by the authority, not by the bridge", async () => {
  const k = r.msgs.length, kd = px.down.length;
  expect((await ann.req("delete", { id: "@world", component: `roles:${raeId}` }))?.ok).toBe(true);
  await rightsAfter(k, kd, (x) => x?.role === "inhabitant");
  const s = await spawn();
  expect(s.up).toEqual([s.raw]);
  expect(s.got.reply.refused?.code).toBe("rights");
});
