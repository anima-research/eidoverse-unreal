// T4 chat: says arrive as bridge/say with names, spoken lines keep their utterance, speech is arrival-only, history pages pass, whisper is refused by policy, logged role and asset acts are narrated (ADAPTER-DESIGN §3.3-3.5, §5).
import { test, expect, beforeAll, afterAll } from "bun:test";
import { startServer, startBridge, startProxy, renderer, person, until, type Server, type Bridge, type Proxy, type Sock } from "./harness.ts";
import { join } from "./fixtures/renderer.ts";
import { say, spoken, spokenAsSent, whisper, historyPage } from "./fixtures/chat.ts";

let srv: Server, br: Bridge, px: Proxy, r: Sock, ann: Awaited<ReturnType<typeof person>>, bob: Awaited<ReturnType<typeof person>>;
let bobId = "", raeId = "";
const reply = (id: string, from = 0) => r.wait(`the reply to ${id}`, (m) => m.type === "reply" && m.id === id, from);
const idOf = (f: { raw: string }) => JSON.parse(f.raw).id as string;

beforeAll(async () => {
  srv = await startServer(); px = await startProxy(srv.url); br = await startBridge();
  ann = await person(srv.url, "glade", "Ann"); bob = await person(srv.url, "glade", "Bob");
  expect((await bob.req("say", { text: "said before you came" }))?.ok).toBe(true);
  r = await renderer(br, px.url); r.send(join("glade", "Rae").raw);
  raeId = (await r.wait("bridge/ready", (m) => m.type === "bridge/ready")).you;
  bobId = (await ann.req("whois", { name: "Bob" })).people[0].id;
});
afterAll(() => { ann?.close(); bob?.close(); br?.stop(); px?.stop(); srv?.stop(); });

test("T4a someone's say arrives as bridge/say {seq, actor, name, text, ts}; the log itself isn't forwarded; nothing said before arrival is shown", async () => {
  const k = r.msgs.length;
  const sent = await bob.req("say", { text: "line 1 of 3" });
  const s = await r.wait("bridge/say", (m) => m.type === "bridge/say" && m.text === "line 1 of 3", k);
  expect(s).toEqual({ type: "bridge/say", seq: sent.seq, actor: bobId, name: "Bob", text: "line 1 of 3", ts: s.ts });
  expect(Number.isInteger(s.ts)).toBe(true);
  expect(r.msgs.some((m) => m?.type === "log")).toBe(false);
  expect(r.msgs.some((m) => m?.type === "bridge/say" && m.text === "said before you came")).toBe(false);
});

test("T4b a spoken line keeps spoken/utt/t0 (the C++ merges it with its captions); the renderer's own say comes back named; `voiced` is the authority's to refuse", async () => {
  const k = r.msgs.length;
  await bob.req("say", { text: "out loud", spoken: true, utt: 3, t0: Date.now() - 500 });
  const s = await r.wait("the spoken line", (m) => m.type === "bridge/say" && m.text === "out loud", k);
  expect([s.spoken, s.utt, Number.isFinite(s.t0)]).toEqual([true, 3, true]);
  const mine = spoken("hello there", 7);
  r.send(mine.raw);
  expect((await reply(idOf(mine), k)).reply?.ok).toBe(true);
  const back = await r.wait("my own line", (m) => m.type === "bridge/say" && m.actor === raeId, k);
  expect([back.name, back.text, back.spoken, back.utt]).toEqual(["Rae", "\u{1F399} hello there", true, 7]);
  const old = spokenAsSent("hi", 8); r.send(old.raw);
  expect((await reply(idOf(old), k)).reply?.refused?.code).toBe("unknown-arg");
  const plain = say("typed"); r.send(plain.raw);
  expect((await r.wait("the typed line", (m) => m.type === "bridge/say" && m.text === "typed", k)).spoken).toBeUndefined();
});

test("T4c history pages pass byte for byte and are arrival-only for a non-admin", async () => {
  const f = historyPage("h1"), kr = r.raw.length;
  r.send(f.raw);
  const page = await r.wait("the history page", (m) => m.type === "history" && m.reqId === "h1");
  const raw = px.down.find((x) => { const m = JSON.parse(x); return m.type === "history" && m.reqId === "h1"; })!;
  expect(r.raw.slice(kr)).toContain(raw);
  expect(raw).toContain("line 1 of 3");
  expect(raw).not.toContain("said before you came");
  expect(Array.isArray(page.entries)).toBe(true);
});

test("T4d a whisper is refused by the server's policy (public, arrival-only speech) and passed as it said it; the bridge adds nothing", async () => {
  const k = r.msgs.length, kr = r.raw.length;
  r.send(whisper("Bob", "psst").raw);
  const e = await r.wait("the refusal", (m) => m.type === "error" && m.about === "whisper", k);
  expect(e.refused?.code).toBe("removed");
  expect(e.refused?.reason).toMatch(/public and arrival-only/);
  expect(r.raw.slice(kr)).toContain(px.down.find((x) => x.includes('"about":"whisper"'))!);
  expect(r.msgs.slice(k).some((m) => m?.type === "bridge/error")).toBe(false);
});

test("T4e logged role grants, revokes and assets become bridge/narration {text, cause}; a refused act is not narrated", async () => {
  const k = r.msgs.length;
  const g = await ann.req("set", { id: "@world", component: `roles:${raeId}`, value: { role: "moderator" } });
  const n1 = await r.wait("the grant's narration", (m) => m.type === "bridge/narration" && m.cause === g.act, k);
  expect(n1.text).toBe("Ann made Rae moderator");
  const d = await ann.req("delete", { id: "@world", component: `roles:${raeId}` });
  expect((await r.wait("the revoke's narration", (m) => m.type === "bridge/narration" && m.cause === d.act, k)).text).toBe("Ann took Rae off the list");
  const a = await ann.req("set", { id: "@world", component: "assets:lamp", value: { name: "lamp", path: "eidoverse/assets/models/lamp.glb" } });
  expect((await r.wait("the asset's narration", (m) => m.type === "bridge/narration" && m.cause === a.act, k)).text).toBe("lamp joined the world's palette");
  const no = await bob.req("set", { id: "@world", component: `roles:${bobId}`, value: { role: "owner" } });
  expect(no?.ok).toBe(false);
  await bob.req("say", { text: "after the refusal" });
  await r.wait("a later line", (m) => m.type === "bridge/say" && m.text === "after the refusal", k);
  expect(r.msgs.slice(k).filter((m) => m?.type === "bridge/narration").map((m) => m.cause)).toEqual([g.act, d.act, a.act]);
});
