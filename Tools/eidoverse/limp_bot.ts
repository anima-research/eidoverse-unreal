// A limp body to be dragged: streams clip 'ragdoll' with a lying pose at (x, z), and
// logs the bodydrag messages it receives (grab, samples, end with sim/pinAt).
//   bun Tools/eidoverse/limp_bot.ts <ws-url> <world> <avatar> [seconds] [x] [z]
const [url, world, avatar, secondsArg, xArg, zArg] = process.argv.slice(2);
if (!/^ws:\/\/(127\.0\.0\.1|localhost):/.test(url ?? "")) { console.error("local servers only"); process.exit(2); }
const x = Number(xArg ?? 0), z = Number(zArg ?? 2);
const ws = new WebSocket(url);
let joined = false, samples = 0;
ws.onopen = () => ws.send(JSON.stringify({ type: "join", world, id: "limp-bot", avatar }));
const lying = { hips: [-0.7071, 0, 0, 0.7071] };
ws.onmessage = (ev) => {
  const m = JSON.parse(String(ev.data));
  if (m.type === "bodydrag") {
    if (m.pose && !m.end && m.grab == null) { if (++samples % 15 === 1) console.log(JSON.stringify({ sample: samples, p: m.p })); }
    else console.log(JSON.stringify({ bodydrag: { grab: m.grab, end: m.end, pinAt: m.pinAt, sim: m.sim ? m.sim.j?.length : undefined, by: m.by } }));
  }
  if (m.type !== "snapshot" || joined) return;
  joined = true;
  setInterval(() => ws.send(JSON.stringify({ type: "pose", pose: { p: [x, 0, z], yaw: 0, speed: 0, clip: "ragdoll", pose: lying } })), 100);
};
setTimeout(() => { console.log(JSON.stringify({ total_samples: samples })); ws.close(); process.exit(0); }, Number(secondsArg ?? 90) * 1000);
