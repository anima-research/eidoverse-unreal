// Seed a current-protocol world for run_v4_join.sh: its founder joins (a folding client) and creates a lamp and a light.
// usage: bun Tools/eidoverse/v4_seed.ts <ws://127.0.0.1:P/ws> <world> <founder>   (prints {"ok":true,"ids":[…]})
const [url, world, founder] = process.argv.slice(2);
if (!url?.startsWith("ws://127.0.0.1:") || !world || !founder) { console.error("usage: v4_seed.ts ws://127.0.0.1:<port>/ws <world> <founder>"); process.exit(2); }
const ws = new WebSocket(url);
const T = (pos: number[]) => ({ pos, q: [0, 0, 0, 1], scale: [1, 1, 1] });   // a whole transform (requests.js create)
const waiting = new Map<string, (r: any) => void>();
const ask = (id: string, verb: string, args: unknown) => new Promise<any>((res) => { waiting.set(id, res); ws.send(JSON.stringify({ type: "request", id, verb, args })); });
const timer = setTimeout(() => { console.error("v4_seed: no answer in 20 s"); process.exit(1); }, 20_000);
ws.onmessage = async (ev) => {
  const m = JSON.parse(String(ev.data));
  if (m.type === "reply" && waiting.has(m.id)) { waiting.get(m.id)!(m.reply); waiting.delete(m.id); return; }
  if (m.type !== "snapshot") return;
  const lamp = await ask("s1", "create", { model: "eidoverse/assets/models/lamp.glb", components: { transform: T([0, 0, 4]), label: "seed lamp" } });
  const light = await ask("s2", "create", { components: { transform: T([2, 2, 4]), light: { kind: "point", intensity: 25.93, range: 10 }, label: "seed light" } });
  const ok = lamp?.ok === true && light?.ok === true;
  console.log(JSON.stringify({ ok, ids: [lamp?.id ?? lamp?.ids, light?.id ?? light?.ids], refused: ok ? undefined : [lamp?.refused, light?.refused] }));
  clearTimeout(timer); ws.close(); process.exit(ok ? 0 : 1);
};
ws.onopen = () => ws.send(JSON.stringify({ type: "join", world, id: founder, avatar: "", replica: true }));
ws.onclose = (ev) => { console.error(`v4_seed: closed ${ev.code} ${ev.reason}`); process.exit(1); };
