// One still bot for close-up checks: stands at wire (x, 0, z) facing the origin.
//   bun Tools/eidoverse/pose_bot.ts <ws-url> <world> <avatar path> [seconds] [x] [z] [turn degrees] [walk m/s]
// With walk > 0 it paces sideways (x in -1..1) at that speed, facing where it goes.
// POSE_BOT_EMOTE=<name> sends that emote every 6 s (once per packet, as browsers do).
// POSE_BOT_HELD=<json> holds that pose (pose-wire `pose`), POSE_BOT_PITCH=<rad> tilts
// the head, POSE_BOT_ANIM=1 plays a looping nod (`anim`), POSE_BOT_SAY=<text> says it
// every 8 s, POSE_BOT_VERBS=<json [{verb,args}]> sends verbs once joined (spawn, comp).
// POSE_BOT_ID=<id> (default pose-bot), POSE_BOT_FOLD=1 sends wingsFolded:true.
const [url, world, avatar, secondsArg, xArg, zArg, turnArg, walkArg] = process.argv.slice(2);
const walk = Number(walkArg ?? 0);
if (!/^ws:\/\/(127\.0\.0\.1|localhost):/.test(url ?? "")) { console.error("local servers only"); process.exit(2); }
const x = Number(xArg ?? 0), z = Number(zArg ?? 1.2);
const ws = new WebSocket(url);
const botId = process.env.POSE_BOT_ID ?? "pose-bot";
ws.onopen = () => ws.send(JSON.stringify({ type: "join", world, id: botId, avatar }));
let timer: ReturnType<typeof setInterval> | undefined;
ws.onmessage = (ev) => {
  const m = JSON.parse(String(ev.data));
  if (m.type !== "snapshot" || timer) return;
  const yaw = Math.atan2(-x, -z) + Number(turnArg ?? 0) * Math.PI / 180;   // face the origin (wire yaw = atan2(fx, fz)), then turn
  const t0 = Date.now();
  const held = process.env.POSE_BOT_HELD ? { pose: JSON.parse(process.env.POSE_BOT_HELD) } : {};
  const pitch = process.env.POSE_BOT_PITCH ? { pitch: Number(process.env.POSE_BOT_PITCH) } : {};
  const fold = process.env.POSE_BOT_FOLD ? { wingsFolded: true } : {};
  const reach = process.env.POSE_BOT_REACH ? { reach: JSON.parse(process.env.POSE_BOT_REACH) } : {};
  for (const v of JSON.parse(process.env.POSE_BOT_VERBS ?? "[]")) ws.send(JSON.stringify({ type: "verb", verb: v.verb, args: v.args }));
  const nod = () => ws.send(JSON.stringify({ type: "anim", dur: 1.6, loop: true, replace: true, tracks: { head: [
      { t: 0, q: [0, 0, 0, 1] }, { t: .4, q: [0.38, 0, 0, 0.925] }, { t: .8, q: [0, 0, 0, 1] }, { t: 1.2, q: [0, 0.38, 0, 0.925] }, { t: 1.6, q: [0, 0, 0, 1] }] } }));
  if (process.env.POSE_BOT_ANIM) { nod(); setInterval(nod, 6000); }   // late joiners miss a one-off
  if (process.env.POSE_BOT_SAY) {
    const say = () => ws.send(JSON.stringify({ type: "verb", verb: "say", args: { text: process.env.POSE_BOT_SAY } }));
    setTimeout(say, 1500); setInterval(say, 8000);
  }
  timer = setInterval(() => {
    const emoteName = process.env.POSE_BOT_EMOTE;
    const emote = emoteName && Math.floor((Date.now() - t0) / 100) % 60 === 5 ? { emote: emoteName } : {};
    if (!walk) { ws.send(JSON.stringify({ type: "pose", pose: { p: [x, 0, z], yaw, speed: 0, clip: process.env.POSE_BOT_CLIP ?? "idle", ...emote, ...held, ...pitch, ...fold, ...reach } })); return; }
    const t = (Date.now() - t0) / 1000, period = 4 / walk;            // -1 -> 1 -> -1
    const phase = (t % period) / period, dir = phase < .5 ? 1 : -1;
    const px = -1 + 2 * (phase < .5 ? phase * 2 : 2 - phase * 2);
    ws.send(JSON.stringify({ type: "pose", pose: { p: [px, 0, z], yaw: Math.atan2(dir, 0), speed: walk, clip: "walk" } }));
  }, 100);
};
setTimeout(() => { ws.close(); process.exit(0); }, Number(secondsArg ?? 120) * 1000);
