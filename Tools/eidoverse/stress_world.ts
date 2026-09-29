// Load harness for native-client streaming work: a busy world like production commons.
//
//   bun Tools/eidoverse/stress_world.ts <ws-url> <world> <avatars.txt> <models.txt> [seconds]
//
// One bot per avatar path joins <world> and walks slow circles near the origin; the
// first bot also spawns every model on a ring 4-28 m out (once — a world that already
// has them keeps them). Bots stay until [seconds] (default 600) or Ctrl+C.
// Only point this at a scratch server (e.g. tools/water/preview.ts on :8993).
import { readFileSync } from "node:fs";

const [url, world, avatarFile, modelFile, secondsArg] = process.argv.slice(2);
if (!url || !world || !avatarFile || !modelFile) {
  console.error("usage: stress_world.ts <ws-url> <world> <avatars.txt> <models.txt> [seconds]");
  process.exit(2);
}
if (!/^ws:\/\/(127\.0\.0\.1|localhost):/.test(url)) { console.error("refusing a non-local server"); process.exit(2); }
const lines = (f: string) => readFileSync(f, "utf8").split("\n").map((s) => s.trim()).filter(Boolean);
const avatars = lines(avatarFile), models = lines(modelFile);
const seconds = Number(secondsArg ?? 600);

function bot(i: number, avatar: string): Promise<void> {
  return new Promise((resolve) => {
    const ws = new WebSocket(url);
    const id = `stress-${i}`;
    const r = 2 + (i % 5) * 1.5, phase = (i / avatars.length) * Math.PI * 2, speed = 0.25 + (i % 3) * 0.1;
    let timer: ReturnType<typeof setInterval> | undefined;
    ws.onopen = () => ws.send(JSON.stringify({ type: "join", world, id, avatar }));
    ws.onmessage = (ev) => {
      const m = JSON.parse(String(ev.data));
      if (m.type !== "snapshot" || timer) return;
      if (i === 0) {
        const present = new Set(Object.values(m.state?.entities ?? {}).map((e: any) => e.lib));
        // Paced: the server reads each never-seen model before committing its spawn.
        models.forEach((lib, k) => setTimeout(() => {
          if (present.has(lib) || ws.readyState !== WebSocket.OPEN) return;
          const a = (k / models.length) * Math.PI * 2, d = 4 + (k % 7) * 4;
          ws.send(JSON.stringify({ type: "verb", verb: "spawn", args: {
            id: `m${k.toString(36)}${Math.random().toString(36).slice(2, 6)}`, lib,
            pos: [Math.cos(a) * d, 0, Math.sin(a) * d], yaw: a } }));
        }, k * 250));
      }
      const t0 = Date.now();
      timer = setInterval(() => {
        const t = (Date.now() - t0) / 1000, a = phase + t * speed / r;
        const p = [Math.cos(a) * r, 0, Math.sin(a) * r];
        const yaw = Math.atan2(-Math.sin(a), Math.cos(a));   // tangent, wire convention
        ws.send(JSON.stringify({ type: "pose", pose: { p, yaw, speed, clip: "walk" } }));
      }, 100);
    };
    ws.onclose = () => { if (timer) clearInterval(timer); resolve(); };
    setTimeout(() => ws.close(), seconds * 1000);
  });
}

console.log(`stress: ${avatars.length} bots, ${models.length} models -> ${url} ${world}`);
// Stagger joins a little, like people arriving, but all inside a couple of seconds.
await Promise.all(avatars.map((a, i) => new Promise<void>((res) => setTimeout(() => bot(i, a).then(res), i * 120))));
console.log("stress: done");
