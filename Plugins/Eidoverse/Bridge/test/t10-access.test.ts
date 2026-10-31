// T10 access: bridge/access-state from @world's access/default-role/roles:*, pushed again on change; the tab's grants are requests; a removal's 4010 passes; a directory miss is the §3.3 gap (ADAPTER-DESIGN §3.3, §7.2, §5).
import { test, expect, beforeAll, afterAll } from "bun:test";
import { startServer, startBridge, startProxy, renderer, person, until, type Server, type Bridge, type Proxy, type Sock } from "./harness.ts";
import { join } from "./fixtures/renderer.ts";
import { DOOR, grant, revoke } from "./fixtures/access.ts";
import { BASE } from "../engine/shared/protocol/roles.js";

const GAP = "search by name isn't available on this server: pick someone here, or paste their identity (u-…)";
let srv: Server, br: Bridge, px: Proxy, r: Sock, ann: Awaited<ReturnType<typeof person>>, bob: Awaited<ReturnType<typeof person>>;
let raeId = "", annId = "", bobId = "";

async function ask(f: { id: string; raw: string }) {
  const k = r.msgs.length; r.send(f.raw);
  return (await r.wait(`the reply to ${f.id}`, (x) => x.type === "reply" && x.id === f.id, k)).reply;
}
const state = async () => { const k = r.msgs.length; r.send({ type: "bridge/access-state" }); return r.wait("bridge/access-state", (m) => m.type === "bridge/access-state", k); };
/** A push the renderer didn't ask for, after `k`, matching `want`. */
const pushed = (k: number, what: string, want: (s: any) => boolean) => r.wait(what, (m) => m.type === "bridge/access-state" && want(m), k);
const directory = async (q: string) => { const k = r.msgs.length; r.send({ type: "bridge/access-directory", q }); return r.wait(`directory ${q}`, (m) => m.type === "bridge/access-directory" && m.q === q, k); };

beforeAll(async () => {
  srv = await startServer(); px = await startProxy(srv.url); br = await startBridge();
  ann = await person(srv.url, "glade", "Ann"); bob = await person(srv.url, "glade", "Bob");
  annId = (await ann.req("whois", { name: "Ann" })).people[0].id; bobId = (await ann.req("whois", { name: "Bob" })).people[0].id;
  r = await renderer(br, px.url); r.send(join("glade", "Rae").raw);
  raeId = (await r.wait("bridge/ready", (m) => m.type === "bridge/ready")).you;
  const k = r.msgs.length;
  expect((await ann.req("set", { id: "@world", component: `roles:${raeId}`, value: { role: "owner" } }))?.ok).toBe(true);
  await r.wait("Rae's rights as owner", (m) => m.type === "your-rights" && m.rights?.role === "owner", k);
});
afterAll(() => { ann?.close(); bob?.close(); br?.stop(); px?.stop(); srv?.stop(); });

test("T10a the tab's facts: the door in the protocol's words, the listed members with names, who is here unlisted, you, your rights as the server sent them", async () => {
  const s = await state();
  expect([s.world, s.door, s.access, s.defaultRole]).toEqual(["glade", "open", null, "builder"]);
  expect(Object.keys(s.doors)).toEqual(["open", "public", "watch", "invite-only"]);
  expect(s.members.map((m: any) => [m.name, m.role, m.here]).sort()).toEqual([["Ann", "owner", true], ["Rae", "owner", true]]);
  expect(s.here).toEqual([{ id: "Bob", identity: bobId, name: "Bob" }]);
  expect([s.you.id, s.you.role, s.rights.role]).toEqual([raeId, "owner", "owner"]);
});

test("T10b the tab's grants (EidoverseUIAccess.cpp:296-301) are requests the authority applies; each change is pushed again without asking", async () => {
  let k = r.msgs.length;
  expect((await ask(DOOR.visitor.req())).ok).toBe(true);
  await pushed(k, "the door pushed as public", (s) => s.door === DOOR.visitor.to);
  k = r.msgs.length;
  const until = Date.now() + 3_600_000;
  expect((await ask(grant(bobId, "builder", until))).ok).toBe(true);
  const s = await pushed(k, "Bob listed", (x) => x.members.some((m: any) => m.id === bobId));
  expect(s.members.find((m: any) => m.id === bobId)).toEqual({ id: bobId, name: "Bob", role: "builder", until, here: true });
  expect(s.here).toEqual([]);
  k = r.msgs.length;
  expect((await ask(revoke(bobId))).ok).toBe(true);
  await pushed(k, "Bob off the list, here unlisted", (x) => !x.members.some((m: any) => m.id === bobId) && x.here.some((h: any) => h.identity === bobId));
});

test("T10c closing the door removes whoever isn't listed: a bridged renderer gets the server's 4010 and reason, mirrored", async () => {
  const ike = await renderer(br, px.url); ike.send(join("glade", "Ike").raw);
  await ike.wait("bridge/ready", (m) => m.type === "bridge/ready");
  const k = r.msgs.length;
  expect((await ask(DOOR.none.req())).ok).toBe(true);
  await pushed(k, "the door pushed as invite-only", (s) => s.door === DOOR.none.to);
  const c = await ike.waitClosed();
  expect(c.code).toBe(4010);
  expect(c.reason.length).toBeGreaterThan(0);
  expect((await bob.waitClosed()).code).toBe(4010);
});

test("T10d the directory: whois for a name or an identity this world has seen; a miss answers with the §3.3 gap", async () => {
  const byName = await directory("Ann");
  expect(byName.results.map((x: any) => [x.name, x.sub])).toEqual([["Ann", annId]]);
  expect(byName.error).toBeUndefined();
  expect((await directory(raeId)).results.map((x: any) => [x.sub, x.here])).toEqual([[raeId, true]]);
  const miss = await directory("Nobody Ever");
  expect([miss.results, miss.error]).toEqual([[], GAP]);
  expect(await directory("")).toEqual({ type: "bridge/access-directory", q: "", results: [] });
});

test("T10e a world with no default-role: the tab names the engine's base rung (roles.js BASE), as the authority reads it", async () => {
  const k = r.msgs.length;
  expect((await ann.req("delete", { id: "@world", component: "default-role" }))?.ok).toBe(true);
  await pushed(k, "the change pushed", () => true);
  expect((await state()).defaultRole).toBe(BASE);
});
