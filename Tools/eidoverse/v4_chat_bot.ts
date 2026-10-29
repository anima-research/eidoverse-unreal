// The other person in run_v4_chat.sh's world: its founder (owner), declared an agent, who talks, grants roles and poses
// on cue from Eidoverse.LiveV4Chat over a loopback control port. usage: bun Tools/eidoverse/v4_chat_bot.ts <ws-url> <world> <founder> <control port> <client name>
const [url, world, founder, portArg, client] = process.argv.slice(2);
if (!url?.startsWith("ws://127.0.0.1:") || !world || !founder || !portArg || !client) { console.error("usage: v4_chat_bot.ts ws://127.0.0.1:<port>/ws <world> <founder> <control port> <client name>"); process.exit(2); }
const ws = new WebSocket(url);
const waiting = new Map<string, (r: any) => void>();
let n = 0, ready = false, clientActor = "", animLeft = 0, anim = "";
const seen = { anim: [] as string[], emote: [] as string[] };
const ask = (verb: string, args: unknown) => new Promise<any>((res) => { const id = `b${++n}`; waiting.set(id, res); ws.send(JSON.stringify({ type: "request", id, verb, args })); });
const actorOf = async () => clientActor || (await ask("whois", { name: client }))?.people?.[0]?.id || "";
ws.onmessage = async (ev) => {
  const m = JSON.parse(String(ev.data));
  if (m.type === "reply" && waiting.has(m.id)) { waiting.get(m.id)!(m.reply); waiting.delete(m.id); return; }
  if (m.type === "arrive" && m.id === client && m.actorId) clientActor = m.actorId;
  if (m.type === "frame") { const p = m.poses?.[client]; if (p?.anim) seen.anim.push(p.anim); if (p?.emote) seen.emote.push(p.emote); }
  if (m.type !== "snapshot" || ready) return;
  // members can't build here; a custom role can (bridge test/t5-rights.test.ts)
  const a = await ask("set", { id: "@world", component: "default-role", value: { role: "member" } });
  const b = await ask("set", { id: "@world", component: "role-defs", value: { crew: { name: "Crew", rank: 50, can: { build: true } } } });
  const c = await ask("say", { text: "said before you came" });
  ready = a?.ok === true && b?.ok === true && c?.ok === true;
  console.log(JSON.stringify({ ready, refused: ready ? undefined : [a?.refused, b?.refused, c?.refused] }));
  setInterval(() => {
    const once = animLeft > 0 ? (animLeft--, { anim }) : {};
    ws.send(JSON.stringify({ type: "pose", pose: { p: [1, 0, 2], yaw: 0, speed: 0, clip: "idle", ...once } }));
  }, 100);
};
ws.onopen = () => ws.send(JSON.stringify({ type: "join", world, id: founder, avatar: "", replica: true, declares: "agent" }));
ws.onclose = (ev) => { console.error(`v4_chat_bot: closed ${ev.code} ${ev.reason}`); process.exit(1); };
const json = (v: unknown) => new Response(JSON.stringify(v), { headers: { "content-type": "application/json" } });
Bun.serve({ hostname: "127.0.0.1", port: Number(portArg), async fetch(req) {
  const u = new URL(req.url), q = (k: string) => u.searchParams.get(k) ?? "";
  switch (u.pathname) {
    case "/ready": return json({ ready });
    case "/say": return json(await ask("say", { text: q("text"), ...(q("utt") ? { spoken: true, utt: Number(q("utt")) } : {}) }));
    case "/say2": { const u1 = await ask("say", { text: q("a"), spoken: true, utt: Number(q("utt")) }); return json([u1, await ask("say", { text: q("b"), spoken: true, utt: Number(q("utt")) })]); }
    case "/grant": return json(await ask("set", { id: "@world", component: `roles:${await actorOf()}`, value: { role: q("role") } }));
    case "/revoke": return json(await ask("delete", { id: "@world", component: `roles:${await actorOf()}` }));
    case "/pose": anim = q("anim"); animLeft = 3; return json({ ok: true });
    case "/seen": return json(seen);
    default: return new Response("no", { status: 404 });
  }
} });
setTimeout(() => process.exit(0), 15 * 60_000);
