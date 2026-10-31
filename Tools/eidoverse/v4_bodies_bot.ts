// The world for run_v4_bodies.sh (Eidoverse.LiveV4Bodies): its founder makes a bench with a seat and a grabbable lamp, prints
// {"ok":true,"bench":…,"lamp":…}, then stays: it makes <client> the world's owner when they arrive, and a guest stands by
// (here, not listed) for the access tab. usage: bun Tools/eidoverse/v4_bodies_bot.ts <ws://127.0.0.1:P/ws> <world> <founder> <client> <guest>
const [url, world, founder, client, guest] = process.argv.slice(2);
if (!url?.startsWith("ws://127.0.0.1:") || !world || !founder || !client || !guest) { console.error("usage: v4_bodies_bot.ts ws://127.0.0.1:<port>/ws <world> <founder> <client> <guest>"); process.exit(2); }
const LAMP = "eidoverse/assets/models/lamp.glb";
const T = (pos: number[]) => ({ pos, q: [0, 0, 0, 1], scale: [1, 1, 1] });
const timer = setTimeout(() => { console.error("v4_bodies_bot: no seed in 20 s"); process.exit(1); }, 20_000);

function open(name: string, onSnapshot: (ask: (verb: string, args: unknown) => Promise<any>, ws: WebSocket) => void, onMsg?: (m: any, ask: (verb: string, args: unknown) => Promise<any>) => void) {
  const ws = new WebSocket(url); let n = 0;
  const waiting = new Map<string, (r: any) => void>();
  const ask = (verb: string, args: unknown) => new Promise<any>((res) => { const id = `${name}-${++n}`; waiting.set(id, res); ws.send(JSON.stringify({ type: "request", id, verb, args })); });
  ws.onmessage = (ev) => {
    const m = JSON.parse(String(ev.data));
    if (m.type === "reply" && waiting.has(m.id)) { waiting.get(m.id)!(m.reply); waiting.delete(m.id); return; }
    if (m.type === "snapshot") onSnapshot(ask, ws);
    onMsg?.(m, ask);
  };
  ws.onopen = () => ws.send(JSON.stringify({ type: "join", world, id: name, avatar: "", replica: true }));
  ws.onclose = (ev) => { console.error(`v4_bodies_bot: ${name} closed ${ev.code} ${ev.reason}`); process.exit(1); };
  return ws;
}

let seeded = false;
open(founder, async (ask) => {
  if (seeded) return; seeded = true;
  const bench = await ask("create", { model: LAMP, components: { transform: T([8, 0, 8]), label: "bench", sockets: { seat: { pos: [0, 0.5, 0], body: { action: "sit", pose: "sitchair" } } } } });
  const lamp = await ask("create", { model: LAMP, components: { transform: T([8.4, 0, 6.8]), label: "lamp", grabbable: {} } });
  const ok = bench?.ok === true && lamp?.ok === true;
  console.log(JSON.stringify({ ok, bench: bench?.refs?.[0]?.id, lamp: lamp?.refs?.[0]?.id, refused: ok ? undefined : [bench?.refused, lamp?.refused] }));
  clearTimeout(timer);
  if (!ok) process.exit(1);
  open(guest, () => {});
}, async (m, ask) => {
  if (m.type !== "arrive" || m.id !== client || typeof m.actorId !== "string") return;
  const r = await ask("set", { id: "@world", component: `roles:${m.actorId}`, value: { role: "owner" } });
  console.log(JSON.stringify({ owner: client, ok: r?.ok === true }));
});
