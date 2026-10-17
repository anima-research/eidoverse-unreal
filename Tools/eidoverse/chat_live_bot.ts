// chat_live_bot -- the other people in Eidoverse.LiveChat (run_chat_live_test.sh starts it).
//
//   bun Tools/eidoverse/chat_live_bot.ts <ws-url> <world> <control-port> <target> <loud-lines>
//
// "quietone" says one line, then "loudbot" says <loud-lines> numbered lines, so the world's fair arrival
// window (shared/fold.js trimRecentChat, 40 lines, the loudest speaker's oldest dropped first) keeps the quiet
// line above a hole of trimmed loud talk. A loopback-only HTTP control port lets the test ask for more:
//   GET  /ready           {seeded}
//   POST /say?n=50        loudbot says "gap i of n" lines
//   POST /mention         loudbot says a line that mentions <target>
//   POST /whisper         loudbot whispers <target>
//   GET  /report          {said, gapSaid, arrivals: [{id, session}]}
// Local servers only; it never touches a production world.
const [url, world, controlArg, target, loudArg] = process.argv.slice(2);
if (!/^ws:\/\/(127\.0\.0\.1|localhost):\d+\/ws$/.test(url ?? "")) { console.error("local servers only"); process.exit(2); }
const loudLines = Number(loudArg ?? 80), controlPort = Number(controlArg ?? 19042);
const avatar = "eidoverse/assets/vrms/shino.vrm";
const arrivals: { id: string; session: string | null }[] = [];
let seeded = false, said = 0, gapSaid = 0;

function person(id: string, onJoined: (ws: WebSocket) => void) {
  const ws = new WebSocket(url);
  let joined = false;
  ws.onopen = () => ws.send(JSON.stringify({ type: "join", world, id, avatar, session: `${id}-page-1` }));
  ws.onmessage = (ev) => {
    const m = JSON.parse(String(ev.data));
    if (m.type === "snapshot" && !joined) { joined = true; onJoined(ws); }
    if (id === "loudbot" && m.type === "arrive") arrivals.push({ id: m.id, session: m.session ?? null });
    if (m.type === "error") console.log(JSON.stringify({ [id]: m.error }));
  };
  setInterval(() => { if (joined) ws.send(JSON.stringify({ type: "pose", pose: { p: [id === "loudbot" ? 2 : -2, 0, 2], yaw: 0, speed: 0, clip: "idle" } })); }, 500);
  return ws;
}
const say = (ws: WebSocket, text: string) => ws.send(JSON.stringify({ type: "verb", verb: "say", args: { text } }));
function sayMany(ws: WebSocket, n: number, label: (i: number) => string, done: () => void, count: () => void) {
  let i = 0;
  const tick = setInterval(() => {
    if (i >= n) { clearInterval(tick); done(); return; }
    say(ws, label(++i)); count();
  }, 120);   // the scratch server raises VERB_RATE; production's 12 per 4 s would need 360 ms
}

let loud: WebSocket | null = null;
person("quietone", (quiet) => {
  say(quiet, "quiet hello from the start of the day");
  setTimeout(() => {
    loud = person("loudbot", (ws) => sayMany(ws, loudLines, (i) => `loud ${i} of ${loudLines}`, () => { seeded = true; console.log(JSON.stringify({ seeded: said })); }, () => said++));
  }, 400);
});

Bun.serve({
  hostname: "127.0.0.1", port: controlPort,
  fetch(req) {
    const u = new URL(req.url);
    const json = (o: unknown) => new Response(JSON.stringify(o), { headers: { "content-type": "application/json" } });
    if (u.pathname === "/ready") return json({ seeded });
    if (u.pathname === "/report") return json({ said, gapSaid, arrivals });
    if (req.method !== "POST" || !loud) return new Response("no", { status: 404 });
    if (u.pathname === "/say") {
      const n = Math.min(500, Math.max(1, Number(u.searchParams.get("n") ?? 50)));
      gapSaid = 0;
      sayMany(loud, n, (i) => `gap ${i} of ${n}`, () => console.log(JSON.stringify({ gap: n })), () => gapSaid++);
      return json({ ok: true });
    }
    if (u.pathname === "/mention") { say(loud, `@${target} are you there?`); return json({ ok: true }); }
    if (u.pathname === "/whisper") { loud.send(JSON.stringify({ type: "whisper", to: target, text: "psst, a private hello" })); return json({ ok: true }); }
    return new Response("no", { status: 404 });
  },
});
setTimeout(() => process.exit(0), 10 * 60 * 1000);
