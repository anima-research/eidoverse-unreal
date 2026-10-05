// One still bot for close-up checks: stands at wire (x, 0, z) facing the origin.
//   bun Tools/eidoverse/pose_bot.ts <ws-url> <world> <avatar path> [seconds] [x] [z] [turn degrees] [walk m/s]
// With walk > 0 it paces sideways (x in -1..1) at that speed, facing where it goes.
// POSE_BOT_EMOTE=<name> sends that emote every 6 s (once per packet, as browsers do).
const [url, world, avatar, secondsArg, xArg, zArg, turnArg, walkArg] = process.argv.slice(2);
const walk = Number(walkArg ?? 0);
if (!/^ws:\/\/(127\.0\.0\.1|localhost):/.test(url ?? "")) { console.error("local servers only"); process.exit(2); }
const x = Number(xArg ?? 0), z = Number(zArg ?? 1.2);
const ws = new WebSocket(url);
ws.onopen = () => ws.send(JSON.stringify({ type: "join", world, id: "pose-bot", avatar }));
let timer: ReturnType<typeof setInterval> | undefined;
ws.onmessage = (ev) => {
  const m = JSON.parse(String(ev.data));
  if (m.type !== "snapshot" || timer) return;
  const yaw = Math.atan2(-x, -z) + Number(turnArg ?? 0) * Math.PI / 180;   // face the origin (wire yaw = atan2(fx, fz)), then turn
  const t0 = Date.now();
  timer = setInterval(() => {
    const emoteName = process.env.POSE_BOT_EMOTE;
    const emote = emoteName && Math.floor((Date.now() - t0) / 100) % 60 === 5 ? { emote: emoteName } : {};
    if (!walk) { ws.send(JSON.stringify({ type: "pose", pose: { p: [x, 0, z], yaw, speed: 0, clip: process.env.POSE_BOT_CLIP ?? "idle", ...emote } })); return; }
    const t = (Date.now() - t0) / 1000, period = 4 / walk;            // -1 -> 1 -> -1
    const phase = (t % period) / period, dir = phase < .5 ? 1 : -1;
    const px = -1 + 2 * (phase < .5 ? phase * 2 : 2 - phase * 2);
    ws.send(JSON.stringify({ type: "pose", pose: { p: [px, 0, z], yaw: Math.atan2(dir, 0), speed: walk, clip: "walk" } }));
  }, 100);
};
setTimeout(() => { ws.close(); process.exit(0); }, Number(secondsArg ?? 120) * 1000);
