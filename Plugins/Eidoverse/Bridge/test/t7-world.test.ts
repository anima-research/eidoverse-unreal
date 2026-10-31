// T7 world and moderation writes: world panels and commands as bridge/do; names resolve to identities here, rights stay the authority's; /boom is the engine's force (ADAPTER-DESIGN §3.5, §5).
import { test, expect, beforeAll, afterAll } from "bun:test";
import { startServer, startBridge, startProxy, renderer, person, until, type Server, type Bridge, type Proxy, type Sock } from "./harness.ts";
import { join } from "./fixtures/renderer.ts";
import { worldWrites, grant, revoke, kick, ban, unban, boom, script } from "./fixtures/writes.ts";
import * as R from "../engine/shared/protocol/requests.js";
import { makeReplica } from "../engine/shared/protocol/replica.js";
import { doorOf, doorWrites } from "../engine/shared/protocol/access.js";

let srv: Server, br: Bridge, px: Proxy, r: Sock, you: string;
const ups = (k: number) => px.up.slice(k).map((x) => JSON.parse(x)).filter((m) => m.type === "request").map((m) => ({ verb: m.verb, args: m.args }));
const done = (req: string, k = 0) => r.wait(`the answer to ${req}`, (m) => (m.type === "bridge/done" || m.type === "bridge/error") && m.req === req, k);
const fresh = async () => { const rep = makeReplica(); const p = await person(srv.url, "home", `V${Math.random().toString(36).slice(2, 7)}`); rep.adopt(p.snap.cut); p.close(); return rep; };

beforeAll(async () => {
  srv = await startServer({ worlds: [["home", "Rae"]] }); px = await startProxy(srv.url); br = await startBridge();
  r = await renderer(br, px.url); r.send(join("home", "Rae").raw);
  you = (await r.wait("bridge/ready", (m) => m.type === "bridge/ready")).you;
});
afterAll(() => { br?.stop(); px?.stop(); srv?.stop(); });

test("T7a the world panels' writes go up as @world sets and deletes (sky as its verb), and land", async () => {
  const want = (m: any) => m.fn === "sky" ? { verb: "sky", args: m.value } : m.fn === "epoch" ? (m.engine ? R.set("@world", "physics-engine", { engine: m.engine, tickMs: m.tickMs }) : R.unset("@world", "physics-engine"))
    : m.value === null ? R.unset("@world", m.fn) : R.set("@world", m.fn, m.value);
  for (const f of worldWrites) {
    const k = r.msgs.length, ku = px.up.length;
    r.send(f.m);
    const a = await done(f.m.req, k);
    expect([f.src, f.was, a.type, a.reply?.ok, a.reply?.refused]).toEqual([f.src, f.was, "bridge/done", true, undefined]);
    expect([f.src, ups(ku)]).toEqual([f.src, [want(f.m)]]);
  }
  const v = (await fresh()).view();
  expect([v.sky?.hours, v.terrain?.seed, v.grass]).toEqual([12, 3, null]);
  expect((await fresh()).st.entities["@world"].c["physics-engine"]).toBeUndefined();
});

test("T7e /access <door>: the engine's doorWrites from the bridge's fold, as one request, and the door is then that one; a door not on the list is answered locally", async () => {
  // a moderator reads the world: a closed door turns others away, and a second Rae would replace the renderer's connection
  const mod = await person(srv.url, "home", "Mo"); const modId = mod.snap.cut.you as string; mod.close();
  let k0 = r.msgs.length; r.send({ type: "bridge/do", req: "mo", fn: "grant", who: modId, role: "moderator" });
  expect((await done("mo", k0)).reply?.ok).toBe(true);
  const owner = async () => { const rep = makeReplica(); const p = await person(srv.url, "home", "Mo"); rep.adopt(p.snap.cut); p.close(); return rep; };
  const caughtUp = async () => { const h = (await owner()).head; await until("the bridge catches up", async () => { const k = r.msgs.length; r.send({ type: "bridge/status" }); return (await r.wait("status", (m) => m.type === "bridge/status", k)).head >= h; }); };
  const was = doorOf((await owner()).st.entities["@world"].c);
  for (const door of ["invite-only", "watch", "public", "open", was]) {
    await caughtUp(); const wc = (await owner()).st.entities["@world"].c;
    const k = r.msgs.length, ku = px.up.length;
    r.send({ type: "bridge/do", req: `door-${door}`, fn: "door", door });
    const a = await done(`door-${door}`, k);
    expect([door, a.type, a.reply?.ok, a.reply?.refused]).toEqual([door, "bridge/done", true, undefined]);
    expect([door, ups(ku)]).toEqual([door, [R.batch(doorWrites(door, wc)!.map((x: any) => R.set("@world", x.component, x.value)))]]);
    await until(`the door is ${door}`, async () => doorOf((await owner()).st.entities["@world"].c) === door);
  }
  const k = r.msgs.length, ku = px.up.length;
  r.send({ type: "bridge/do", req: "door-x", fn: "door", door: "members" });
  expect(await done("door-x", k)).toMatchObject({ type: "bridge/error", fn: "door", code: "bad-args" });
  expect(ups(ku)).toEqual([]);
  k0 = r.msgs.length; r.send({ type: "bridge/do", req: "mo-", fn: "revoke", who: modId });
  expect((await done("mo-", k0)).reply?.ok).toBe(true);
});

test("T7b grant/revoke/kick/ban/unban name a person; the bridge resolves the name to their identity, the authority decides", async () => {
  let bob = await person(srv.url, "home", "Bob");
  const bobId = bob.snap.cut.you as string;
  expect(bobId).toMatch(/^u-/);
  await until("the bridge saw Bob arrive", () => r.msgs.some((m) => m.type === "arrive" && m.id === "Bob"));
  const step = async (f: { src: string; m: any }, wantUp: any[]) => {
    const k = r.msgs.length, ku = px.up.length;
    r.send(f.m);
    const a = await done(f.m.req, k);
    expect([f.src, f.m.fn, a.type, a.reply?.ok, a.reply?.refused]).toEqual([f.src, f.m.fn, "bridge/done", true, undefined]);
    expect([f.m.fn, ups(ku)]).toEqual([f.m.fn, wantUp]);
  };
  await step(grant("Bob", "builder"), [R.set("@world", `roles:${bobId}`, { role: "builder" })]);
  await until("the bridge folded the grant", async () => { const k = r.msgs.length; r.send({ type: "bridge/status" }); return (await r.wait("status", (m) => m.type === "bridge/status", k)).head >= (await fresh()).head; });
  await step(grant("Bob", "moderator"), [R.set("@world", `roles:${bobId}`, { role: "moderator" }, { merge: true })]);   // a standing grant is merged (client/lib/people.js)
  expect((await fresh()).view().roles[bobId]?.role).toBe("moderator");
  await step(revoke("Bob"), [R.unset("@world", `roles:${bobId}`)]);
  expect((await fresh()).view().roles[bobId]).toBeUndefined();
  await step(kick("Bob"), [{ verb: "kick", args: { who: bobId, reason: "testing" } }]);
  await bob.waitClosed();
  await step(unban(bobId), [{ verb: "unban", args: { who: bobId } }]);   // gone: by identity
  bob = await person(srv.url, "home", "Bob");
  await until("the bridge saw Bob come back", () => r.msgs.filter((m) => m.type === "arrive" && m.id === "Bob").length >= 2);
  await step(ban("Bob"), [{ verb: "ban", args: { who: bobId, days: 1, reason: "testing" } }]);
  await bob.waitClosed();
  await step(unban(bobId), [{ verb: "unban", args: { who: bobId } }]);
});

test("T7b' an unknown name is answered locally, nothing goes up; a non-owner's kick goes up and the authority refuses it", async () => {
  let k = r.msgs.length, ku = px.up.length;
  r.send(kick("Zed").m);
  expect(await done("k-Zed", k)).toMatchObject({ type: "bridge/error", for: "bridge/do", fn: "kick", code: "unknown-person", reason: "who is that? they must be here, or give their identity (u-…)" });
  expect(ups(ku)).toEqual([]);
  const cy = await renderer(br, px.url); cy.send(join("home", "Cy").raw);
  await cy.wait("bridge/ready", (m) => m.type === "bridge/ready");
  await until("Cy's bridge knows Rae is here", async () => { k = cy.msgs.length; cy.send({ ...kick("Rae").m, req: "cy-k" }); const a = await cy.wait("Cy's kick", (m) => m.req === "cy-k", k); return a.type === "bridge/done" ? a : null; })
    .then((a: any) => expect(a.reply).toMatchObject({ ok: false, refused: { code: "rights" } }));
  cy.close();
});

test("T7c /boom is the engine's force {at, power, radius}: it goes up as the renderer sent it and the authority answers", async () => {
  const p = await person(srv.url, "home", "Hal");
  const help = await p.req("help", { verb: "force" });
  p.close();
  expect(help?.ok).toBe(true);   // the live authority has the verb (verbspec force)
  const k = r.msgs.length, ku = px.up.length;
  r.send(boom.m);
  const a = await done("boom", k);
  expect([a.type, a.fn, a.reply?.ok, a.reply?.refused]).toEqual(["bridge/done", "force", true, undefined]);
  expect(ups(ku)).toEqual([{ verb: "force", args: { at: boom.m.at, power: boom.m.power, radius: boom.m.radius } }]);
});

test("T7d a script: upload {text}, then set script:<name> {…, src} with the reply's src; the thing then runs it", async () => {
  let k = r.msgs.length;
  r.send({ type: "bridge/do", req: "bell", fn: "create", ref: "$bell", model: "eidoverse/assets/models/lamp.glb", components: { reactions: { ring: { kind: "script", script: "bell" } } } });
  const id = (await done("bell", k)).reply.ids.$bell;
  const f = script(id);
  k = r.msgs.length; const ku = px.up.length;
  r.send(f.m);
  const a = await done(f.m.req, k);
  expect([a.type, a.reply?.ok, a.reply?.refused]).toEqual(["bridge/done", true, undefined]);
  expect(a.reply.src).toMatch(/^sha256:/);
  expect(ups(ku)).toEqual([{ verb: "upload", args: { text: f.m.text } }, R.set(id, "script:bell", { ...f.m.value, src: a.reply.src })]);
  k = r.msgs.length;
  r.send({ type: "bridge/do", req: "ring", fn: "use", id, action: "ring" });
  expect((await done("ring", k)).reply).toMatchObject({ ok: true });
  await until("the bell rang", async () => (await fresh()).view().entities[id]?.["t/bell"]?.rung === 1);
  // a refused upload stops there: nothing is bound
  k = r.msgs.length; const ku2 = px.up.length;
  r.send({ ...f.m, req: "bad", text: 42 });
  expect((await done("bad", k)).reply).toMatchObject({ ok: false, refused: { code: "shape" } });
  expect(ups(ku2).map((u) => u.verb)).toEqual(["upload"]);
});
