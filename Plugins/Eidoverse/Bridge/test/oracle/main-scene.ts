// test/oracle/main-scene.ts — main's own render-scene on main's own fold (MAIN_DIR, read only); usage: bun main-scene.ts <log.jsonl> <name> <nowMs>
// @ts-nocheck
import fs from "node:fs";
const MAIN = process.env.MAIN_DIR;
if (!MAIN || !process.env.WORLDS_DIR || !process.env.OPT_DIR) { console.error("MAIN_DIR, WORLDS_DIR and OPT_DIR are required"); process.exit(2); }
const [logPath, name, nowArg] = process.argv.slice(2);
const { emptyState, foldEntry } = await import(`${MAIN}/shared/fold.js`);
const { renderScene } = await import(`${MAIN}/server/render-scene.ts`);
const entries = fs.readFileSync(logPath, "utf8").split("\n").filter((l) => l.trim()).flatMap((l) => { try { return [JSON.parse(l)]; } catch { return []; } });
const state = emptyState();
for (const e of entries) foldEntry(state, e);
const seq = entries.at(-1)?.seq ?? -1;
// the world as renderScene reads it (server/world.ts): its fold, no sim epoch, no leases, this one client
const w: any = { name, state, sim: { epoch: null, bodies: {} }, leases: new Map(), clients: new Set(), joinPayload: () => ({ state, tail: [], throughSeq: seq }) };
const c: any = { world: w, gen: 1, surface: "world" }; w.clients.add(c);
const r = renderScene(c, { version: 1, reset: true }, Number(nowArg));
process.stdout.write(JSON.stringify({ state, scene: r }));
