// The other hand in a bodydrag (bodydrag.js dragger half, scripted): standing at (x, z),
// DRAG_AT s after first seeing <target> it shoves <target> (puppet ragdoll), 6 s later grabs their hips, streams
// {pose, p, yaw} for 3 s sliding them 1.5 m along +x, releases with a nail on the left
// hand (pinAt), and 4 s later asks to pull that nail (unpin). Logs every bodydrag reply.
//   bun Tools/eidoverse/drag_bot.ts <ws-url> <world> <avatar> <target> [seconds]
const [url, world, avatar, target, secondsArg] = process.argv.slice(2);
if (!/^ws:\/\/(127\.0\.0\.1|localhost):/.test(url ?? "")) { console.error("local servers only"); process.exit(2); }
const ws = new WebSocket(url);
const t0 = Date.now(), at = Number(process.env.DRAG_AT ?? 25);
let them: number[] | null = null, joined = false, lastPins = "";
ws.onopen = () => ws.send(JSON.stringify({ type: "join", world, id: "drag-bot", avatar }));
const send = (o: object) => ws.send(JSON.stringify(o));
ws.onmessage = (ev) => {
  const m = JSON.parse(String(ev.data));
  if (m.type === "error") console.log("DRAG_BOT error", m.error);
  if (m.type === "bodydrag") console.log("DRAG_BOT got", JSON.stringify(m));
  const tp = m.type === "frame" ? m.poses?.[target] : undefined;
  if (tp && Array.isArray(tp.p)) { if (!them) arm(); them = tp.p; }
  if (tp && tp.pins !== undefined && JSON.stringify(tp.pins) !== lastPins) { lastPins = JSON.stringify(tp.pins); console.log("DRAG_BOT pins", lastPins); }
  if (m.type !== "snapshot" || joined) return;
  joined = true;
  setInterval(() => send({ type: "pose", pose: { p: [1.2, 0, 0], yaw: -Math.PI / 2, speed: 0, clip: "idle" } }), 100);
};
function arm() {
  console.log("DRAG_BOT sees", target);
  send({ type: "verb", verb: "grant", args: { id: target, fly: true } });   // first in, so the owner here
  setTimeout(() => { console.log("DRAG_BOT shove"); send({ type: "puppet", target, ragdoll: { lean: [2.5, 0, 0] }, pose: null, anim: null }); }, at * 1000);
  setTimeout(() => {
    console.log("DRAG_BOT grab", JSON.stringify(them));
    send({ type: "bodydrag", target, grab: { joint: "hips" } });
    const start = them ?? [0, 0, 0], t1 = Date.now();
    const lying = { hips: [-0.7071, 0, 0, 0.7071], leftUpperArm: [0, 0, 0.5, 0.866], rightUpperArm: [0, 0, -0.5, 0.866] };
    const iv = setInterval(() => {
      const k = Math.min(1, (Date.now() - t1) / 3000);
      const p = [start[0] - 1.5 * k, start[1] + 0.3 * Math.sin(Math.PI * k), start[2]];
      send({ type: "bodydrag", target, pose: lying, p, yaw: 1.0 });
      if (k >= 1) {
        clearInterval(iv);
        console.log("DRAG_BOT release with nail");
        send({ type: "bodydrag", target, end: true, pinAt: { joint: "leftHand", at: [p[0] - 0.4, 0.1, p[2] + 0.2] }, pose: lying, p, yaw: 1.0 });
        setTimeout(() => { console.log("DRAG_BOT unpin"); send({ type: "bodydrag", target, unpin: { joint: "leftHand" } }); }, 6000);
      }
    }, 66);
  }, (at + 6) * 1000);
}
setTimeout(() => { ws.close(); process.exit(0); }, Number(secondsArg ?? 90) * 1000);
