// A talkative bot for chat/protocol checks: says N numbered lines (paced under the
// server's 12-verbs-per-4s limit), then whenever TARGET is present, whispers it once
// and shows a "think" typing glyph every few seconds.
//   bun Tools/eidoverse/talk_bot.ts <ws-url> <world> <lines> <target> [seconds]
const [url, world, linesArg, target, secondsArg] = process.argv.slice(2);
if (!/^ws:\/\/(127\.0\.0\.1|localhost):/.test(url ?? "")) { console.error("local servers only"); process.exit(2); }
const lines = Number(linesArg ?? 50);
const ws = new WebSocket(url);
let said = 0, whispered = false, joined = false;
ws.onopen = () => ws.send(JSON.stringify({ type: "join", world, id: "talker", avatar: "eidoverse/assets/vrms/shino.vrm" }));
ws.onmessage = (ev) => {
  const m = JSON.parse(String(ev.data));
  if (m.type === "snapshot" && !joined) {
    joined = true;
    const tick = setInterval(() => {
      if (said >= lines) { clearInterval(tick); console.log(JSON.stringify({ said })); return; }
      ws.send(JSON.stringify({ type: "verb", verb: "say", args: { text: `line ${++said} of ${lines}` } }));
    }, 360);
    setInterval(() => ws.send(JSON.stringify({ type: "pose", pose: { p: [0, 0, 2], yaw: Math.PI, speed: 0, clip: "idle" } })), 100);
    setInterval(() => ws.send(JSON.stringify({ type: "typing", state: "think" })), 3000);
  }
  if (m.type === "arrive" && m.id === target && !whispered) {
    whispered = true;
    setTimeout(() => ws.send(JSON.stringify({ type: "whisper", to: target, text: "psst, a private hello" })), 4000);
  }
  if (m.type === "whisper" && !m.echo) console.log(JSON.stringify({ gotWhisper: m.text }));
};
setTimeout(() => { ws.close(); process.exit(0); }, Number(secondsArg ?? 300) * 1000);
