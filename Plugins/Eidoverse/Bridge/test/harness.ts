// test/harness.ts — a real server from engine/, the bridge as a process, a recording proxy, a fake renderer (cf. engine/tools/protocol/presence-check.ts).
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import crypto from "node:crypto";

export const ROOT = path.resolve(import.meta.dir, "..");
export const ENGINE = path.join(ROOT, "engine");

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
/** Poll `probe` until truthy (resolves its value), else throw naming `what`. */
export async function until<T>(what: string, probe: () => T | Promise<T>, ms = 8000, step = 25): Promise<T> {
  const end = Date.now() + ms; let last: unknown;
  for (;;) {
    try { const v = await probe(); if (v) return v; last = v; } catch (e) { last = e; }
    if (Date.now() > end) throw new Error(`never became true within ${ms} ms: ${what} (last: ${String(last).slice(0, 200)})`);
    await sleep(step);
  }
}

export function scratch(tag: string) {
  const base = process.env.BRIDGE_SCRATCH ?? os.tmpdir();
  const d = fs.mkdtempSync(path.join(base, `eido-bridge-${tag}-`));
  if (!process.env.BRIDGE_KEEP) process.on("exit", () => fs.rmSync(d, { recursive: true, force: true }));
  return d;
}

async function freePort() {
  const s = Bun.listen({ hostname: "127.0.0.1", port: 0, socket: { data() {} } }); const p = s.port; s.stop(true); return p;
}

export type Server = Awaited<ReturnType<typeof startServer>>;
/** A real server from engine/, worlds founded (`found <name> --owner <owner>`) or copied in (`seed(worldsDir)`). */
export async function startServer({ worlds = [["glade", "Ann"]] as [string, string][], seed }: { worlds?: [string, string][]; seed?: (dir: string) => void } = {}) {
  const root = scratch("srv"), W = path.join(root, "worlds"); fs.mkdirSync(W, { recursive: true });
  const env = (extra = {}) => ({ ...process.env, WORLDS_DIR: W, OPT_DIR: path.join(root, "opt"), SKIP_OPT_SWEEP: "1", HOST: "127.0.0.1",
    BUN_RUNTIME_TRANSPILER_CACHE_PATH: "0", JOIN_TOKEN: "", SERVER_OPERATORS: "", TRUST_NAMES: "1", SESSIONS_PATH: path.join(root, "sessions.json"), ...extra });
  for (const [name, owner] of worlds) {
    const r = Bun.spawnSync(["bun", "tools/protocol/world.ts", "found", name, "--owner", owner], { cwd: ENGINE, env: env() });
    if (r.exitCode !== 0) throw new Error(`found ${name}: ${r.stderr.toString().slice(-400)}`);
  }
  seed?.(W);
  const port = await freePort();
  const log = path.join(root, "srv.log");
  const proc = Bun.spawn(["bun", "server/server.ts"], { cwd: ENGINE, env: env({ PORT: String(port) }), stdout: Bun.file(log), stderr: Bun.file(log + ".err") });
  process.on("exit", () => { try { proc.kill(); } catch {} });
  await until("the server answers /version", async () => (await fetch(`http://127.0.0.1:${port}/version`, { signal: AbortSignal.timeout(800) })).ok, 90_000, 150);
  return { port, url: `ws://127.0.0.1:${port}/ws`, http: `http://127.0.0.1:${port}`, root, worlds: W, proc, log,
    stop: () => { try { proc.kill(); } catch {} } };
}

export type Bridge = Awaited<ReturnType<typeof startBridge>>;
/** The bridge as the sidecar launcher runs it: token on stdin, `{"port": P}` on stdout. */
export async function startBridge({ token = crypto.randomBytes(32).toString("hex"), entry = path.join(ROOT, "src/main.ts") } = {}) {
  // BRIDGE_EXE: a built executable (build.sh) in place of bun on the sources
  const proc = Bun.spawn(process.env.BRIDGE_EXE ? [process.env.BRIDGE_EXE] : ["bun", entry], { cwd: ROOT, stdin: "pipe", stdout: "pipe", stderr: "pipe", env: { ...process.env, BUN_RUNTIME_TRANSPILER_CACHE_PATH: "0" } });
  process.on("exit", () => { try { proc.kill(); } catch {} });
  proc.stdin.write(token + "\n"); proc.stdin.flush();
  const reader = proc.stdout.getReader(); let out = "", port = 0;
  const end = Date.now() + 15_000;
  while (!port) {
    const r = await Promise.race([reader.read(), sleep(15_000).then(() => null)]);
    if (!r || Date.now() > end) throw new Error(`the bridge printed no port line within 15 s: ${JSON.stringify(out)}`);
    if (r.done) throw new Error(`the bridge exited (${await proc.exited}): ${JSON.stringify(out)} ${(await new Response(proc.stderr).text()).slice(0, 400)}`);
    out += new TextDecoder().decode(r.value);
    const m = /^\{"port":(\d+)\}\n/.exec(out); if (m) port = Number(m[1]);
  }
  reader.releaseLock();
  return { port, token, proc, stdout: () => out, endStdin: () => proc.stdin.end(), stop: () => { try { proc.kill(); } catch {} } };
}

export type Sock = Awaited<ReturnType<typeof openSock>>;
/** A ws client that keeps every frame it gets, raw and parsed. */
export async function openSock(url: string, opts: { protocols?: string[]; headers?: Record<string, string> } = {}) {
  const ws = new WebSocket(url, { protocols: opts.protocols, headers: opts.headers } as any);
  const raw: string[] = [], msgs: any[] = []; let closed: { code: number; reason: string } | null = null;
  ws.onmessage = (ev) => { raw.push(String(ev.data)); try { msgs.push(JSON.parse(String(ev.data))); } catch { msgs.push(null); } };
  ws.onclose = (ev) => { closed = { code: ev.code, reason: ev.reason }; };
  const opened = await new Promise<boolean>((r) => { ws.onopen = () => r(true); ws.onerror = () => r(false); });
  const s = {
    ws, raw, msgs, opened, get closed() { return closed; },
    send: (m: unknown) => ws.send(typeof m === "string" ? m : JSON.stringify(m)),
    since: (k: number) => msgs.slice(k),
    wait: (what: string, pred: (m: any) => boolean, from = 0, ms = 8000) => until(what, () => msgs.slice(from).find((m) => m && pred(m)), ms),
    waitClosed: (ms = 8000) => until("the socket closes", () => closed, ms),
    close: () => ws.close(),
  };
  return s;
}

/** The fake renderer: a local connection presenting the token, then bridge/hello naming the server. */
export async function renderer(b: Bridge, server: string, { cookie }: { cookie?: string } = {}) {
  const s = await openSock(`ws://127.0.0.1:${b.port}/ws`, { protocols: [`eido-bridge.${b.token}`] });
  if (!s.opened) throw new Error("the bridge refused the renderer's connection");
  s.send({ type: "bridge/hello", server, ...(cookie ? { cookie } : {}) });
  return s;
}

export type Proxy = Awaited<ReturnType<typeof startProxy>>;
/** A ws proxy in front of the server: records every frame each way (raw), can drop server frames and inject its own. */
export async function startProxy(target: string) {
  const up: string[] = [], down: string[] = [], headers: Record<string, string>[] = [];
  let dropNext: ((raw: string) => boolean) | null = null, holdNext: ((raw: string) => boolean) | null = null;
  const held: { client: any; raw: string }[] = [];
  const pairs = new Set<{ client: any; server: WebSocket }>();
  const srv = Bun.serve({
    hostname: "127.0.0.1", port: 0,
    fetch(req, s) { headers.push(Object.fromEntries(req.headers)); return s.upgrade(req, { data: { queue: [] as string[] } }) ? undefined : new Response("no", { status: 400 }); },
    websocket: {
      open(client: any) {
        const server = new WebSocket(target, { headers: { cookie: headers.at(-1)?.cookie ?? "" } } as any);
        const pair = { client, server }; pairs.add(pair); client.data.pair = pair;
        server.onopen = () => { for (const m of client.data.queue) server.send(m); client.data.queue = []; };
        server.onmessage = (ev) => { const raw = String(ev.data); if (dropNext?.(raw)) { dropNext = null; return; }
          down.push(raw); client.send(raw); };
        server.onclose = (ev) => { try { client.close(ev.code, ev.reason); } catch {} };
      },
      message(client: any, m) {
        const raw = String(m); up.push(raw); const p = client.data.pair;
        if (holdNext?.(raw) || held.length) { holdNext = null; held.push({ client, raw }); return; }   // order is kept behind a held frame
        if (p.server.readyState === 1) p.server.send(raw); else client.data.queue.push(raw); },
      close(client: any) { try { client.data.pair.server.close(); } catch {} pairs.delete(client.data.pair); },
    },
  });
  return {
    url: `ws://127.0.0.1:${srv.port}/ws`, up, down, headers,
    /** Drop the next server frame matching `pred` (a lost message: a gap in the log). */
    drop: (pred: (raw: string) => boolean) => { dropNext = pred; },
    /** Hold the next client frame matching `pred`, and everything after it, before the server sees it: a slow uplink. */
    hold: (pred: (raw: string) => boolean) => { holdNext = pred; },
    /** Let held frames go to the server, in order. */
    release: () => { for (const h of held.splice(0)) h.client.data.pair.server.send(h.raw); },
    held: () => held.length,
    /** Send a frame to every client as if the server had. */
    inject: (raw: string) => { for (const p of pairs) { down.push(raw); p.client.send(raw); } },
    /** Close every client as the server would (a code and a reason). */
    closeClients: (code: number, reason: string) => { for (const p of pairs) p.client.close(code, reason); },
    /** Cut every client's TCP with no close frame (the client sees 1006). */
    cutClients: () => { for (const p of pairs) p.client.terminate(); },
    stop: () => srv.stop(true),
  };
}

/** A direct client (a second person in the world), as the engine's suites join. */
export async function person(url: string, world: string, name: string) {
  const s = await openSock(url);
  s.send({ type: "join", world, id: name, avatar: "", replica: true });
  const snap = await s.wait(`${name}'s snapshot`, (m) => m.type === "snapshot");
  let n = 0;
  return { ...s, snap, req: async (verb: string, args: any = {}) => {
    const id = `p${++n}`; const k = s.msgs.length; s.send({ type: "request", id, verb, args });
    return (await s.wait(`the reply to ${verb}`, (m) => m.type === "reply" && m.id === id, k)).reply; } };
}
