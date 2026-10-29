// src/session.ts — one renderer connection: its server socket, the join as a replica, the fold, bridge/* (ADAPTER-DESIGN §1, §3.2, §3.4).
import type { ServerWebSocket } from "bun";
import { makeReplica } from "../engine/shared/protocol/replica.js";
import { SLICES, table } from "./slices.ts";
import type { Ctx, Slice } from "./slice.ts";

const TABLE = table(SLICES);
const OWN_ID = "bridge:";   // the bridge's own request ids; a renderer's may not start so

export const BRIDGE_PROTOCOL = 1;

/** JSON with keys sorted at every depth: the same value always spells the same. */
export const canon = (v: unknown): string => JSON.stringify(v, (_k, x) => (x && typeof x === "object" && !Array.isArray(x) ? Object.fromEntries(Object.keys(x).sort().map((k) => [k, x[k]])) : x));
/** A digest of a fold's view (chat aside: it isn't world state), to say two folds agree. */
export const viewHash = (view: any) => { const { recentChat, chatTotal, ...w } = view ?? {}; return new Bun.CryptoHasher("sha256").update(canon(w)).digest("hex").slice(0, 32); };

/** Why this server URL can't be dialled, or null (the C++'s own rule, EidoverseBridge.cpp:253-255). */
export function serverProblem(u: unknown): string | null {
  if (typeof u !== "string" || u.length > 512) return "server must be a URL string";
  const local = u.startsWith("ws://127.0.0.1:") || u.startsWith("ws://localhost:");
  if (!local && !u.startsWith("wss://")) return "use wss://host/ws, or loopback ws://127.0.0.1:port/ws";
  if (u.includes("?") || u.includes("@") || u.includes("#")) return "no credentials, query or fragment in the server URL";
  try { new URL(u); } catch { return "server isn't a URL"; }
  return null;
}
const COOKIE = /^[A-Za-z0-9]{1,256}$/;   // the ew_sess value (64 hex today): nothing that could end a header

/** Close codes a server may send in a frame; others (1005, 1006, 1015) mean "no frame", so the renderer gets a cut. */
const sendable = (c: number) => c === 1000 || (c >= 1001 && c <= 1003) || (c >= 1007 && c <= 1014) || (c >= 3000 && c <= 4999);

export class Session {
  server: WebSocket | null = null;
  phase: "hello" | "dialing" | "open" | "closed" = "hello";
  queue: string[] = [];
  replica: any = null;
  joinMsg: Record<string, unknown> | null = null;
  serverUrl: string | null = null;
  /** A re-join is in flight: log messages before its snapshot are inside the coming cut, so they're not folded. */
  resyncing = false;
  private pending = new Map<string, { resolve: (r: any) => void; timer: ReturnType<typeof setTimeout>; verb: string }>();
  private reqSeq = 0;
  private states = new Map<Slice, unknown>();
  private ctxs = new Map<Slice, Ctx>();

  constructor(readonly local: ServerWebSocket<any>) {}

  /** What `slice` is given (src/slice.ts). */
  ctxFor(slice: Slice): Ctx {
    let c = this.ctxs.get(slice); if (c) return c;
    const self = this;
    c = {
      send: (m) => self.send(m),
      error: (f, code, reason) => self.error(f, code, reason),
      request: (verb, args = {}, opts) => self.request(verb, args, opts?.timeoutMs),
      get replica() { return self.replica; },
      view: () => (self.replica?.active ? self.replica.view() : null),
      get join() { return self.joinMsg; },
      get server() { return self.serverUrl; },
      own: <T>(init: () => T) => { if (!self.states.has(slice)) self.states.set(slice, init()); return self.states.get(slice) as T; },
    };
    this.ctxs.set(slice, c); return c;
  }
  private each(hook: "adopted" | "ingested" | "closed", ...args: any[]) {
    for (const sl of TABLE.slices) { const f = sl[hook] as any; if (!f) continue;
      try { f.call(sl, this.ctxFor(sl), ...args); } catch (e) { console.error(`eido-bridge: ${sl.name}.${hook}:`, e); } }
  }

  /** A request of the bridge's own; answered by its reply, a timeout, or the socket closing (as net.js sendRequest). */
  request(verb: string, args: unknown, timeoutMs = 15000): Promise<any> {
    if (this.phase !== "open") return Promise.resolve({ ok: false, refused: { code: "offline", reason: "not connected to the world", fix: "wait for the connection to come back, then try again" } });
    const id = `${OWN_ID}${++this.reqSeq}`;
    return new Promise((resolve) => {
      const timer = setTimeout(() => { this.pending.delete(id); resolve({ ok: false, refused: { code: "timeout", reason: `the authority didn't answer '${verb}' in ${timeoutMs / 1000} s`, fix: "it may still land: look before trying again", mayHaveLanded: true } }); }, timeoutMs);
      this.pending.set(id, { resolve, timer, verb });
      this.sendServer(JSON.stringify({ type: "request", id, verb, args }));
    });
  }
  private failPending() {
    for (const [id, p] of this.pending) { this.pending.delete(id); clearTimeout(p.timer);
      p.resolve({ ok: false, refused: { code: "offline", reason: `the connection dropped before the authority answered '${p.verb}'`, fix: "it may have landed: look before trying again", mayHaveLanded: true } }); }
  }

  /** To the renderer. */
  send(m: Record<string, unknown>) { if (this.phase !== "closed") this.local.send(JSON.stringify(m)); }
  error(forType: string, code: string, reason: string) { this.send({ type: "bridge/error", for: forType, code, reason }); }
  /** To the server, as the wire has it. */
  sendServer(raw: string) { if (this.phase === "open" && this.server?.readyState === 1) this.server.send(raw); else if (this.phase === "dialing") this.queue.push(raw); }

  fromRenderer(raw: string) {
    let m: any; try { m = JSON.parse(raw); } catch { m = null; }
    if (!m || typeof m !== "object" || Array.isArray(m) || typeof m.type !== "string") return this.error("?", "malformed", "a message is a JSON object with a string type");
    if (this.phase === "hello") return this.hello(m);
    if (m.type.startsWith("bridge/")) return this.local_(m);
    if (m.type === "join") return this.join(m);
    if (m.type === "request" && typeof m.id === "string" && m.id.startsWith(OWN_ID)) return this.error("request", "reserved-id", `request ids starting ${OWN_ID} are the bridge's own`);
    this.sendServer(raw);   // a PASS lane: verbatim (ADAPTER-DESIGN §3.3)
  }

  hello(m: any) {
    if (m.type !== "bridge/hello") { this.error(m.type, "hello-first", "the first message is bridge/hello {server, cookie?}"); return this.local.close(1008, "hello first"); }
    const bad = serverProblem(m.server) ?? (m.cookie !== undefined && !(typeof m.cookie === "string" && COOKIE.test(m.cookie)) ? "cookie is the ew_sess value (letters and digits)" : null);
    if (bad) { this.error("bridge/hello", "bad-hello", bad); return this.local.close(1008, "bad hello"); }
    this.phase = "dialing"; this.serverUrl = m.server;
    const ws = new WebSocket(m.server, { headers: m.cookie ? { Cookie: `ew_sess=${m.cookie}` } : {} } as any);
    this.server = ws;
    ws.onopen = () => {
      if (this.phase !== "dialing") return ws.close();
      this.phase = "open"; this.send({ type: "bridge/hello", ok: true, protocol: BRIDGE_PROTOCOL });
      for (const q of this.queue.splice(0)) ws.send(q);
    };
    ws.onmessage = (ev) => this.fromServer(String(ev.data));
    ws.onclose = (ev) => this.serverClosed(ev.code, ev.reason);
  }

  /** The renderer's join, as a replica: the bridge folds (ADAPTER-DESIGN §3.2). Kept for a re-join. */
  join(m: Record<string, unknown>) {
    this.joinMsg = { ...m, replica: true, clock: true };
    this.sendServer(JSON.stringify(this.joinMsg));
  }

  fromServer(raw: string) {
    let m: any; try { m = JSON.parse(raw); } catch { m = null; }
    if (m?.type === "snapshot") return this.snapshot(m);
    if (m?.type === "log") return this.log(m);
    if (m?.type === "reply" && typeof m.id === "string" && m.id.startsWith(OWN_ID)) {
      const p = this.pending.get(m.id); if (p) { this.pending.delete(m.id); clearTimeout(p.timer); p.resolve(m.reply ?? { ok: false, refused: { code: "no-reply", reason: "the authority sent no reply body" } }); }
      return;
    }
    let consumed = false;
    for (const t of TABLE.server.get(m?.type) ?? []) { try { consumed = t.fn(this.ctxFor(t.slice), m, raw) === true || consumed; } catch (e) { console.error(`eido-bridge: ${t.slice.name} tap ${m.type}:`, e); } }
    if (!consumed && this.phase !== "closed") this.local.send(raw);   // a PASS lane: verbatim (§3.4)
  }

  snapshot(m: any) {
    const { cut, ...rest } = m;
    if (cut?.state) { const stats = this.replica?.stats; this.replica = makeReplica(); this.replica.adopt(cut); if (stats) this.replica.stats = stats; this.resyncing = false; }
    this.send(rest);
    if (cut?.state && this.replica?.active) {
      this.send({ type: "bridge/ready", world: m.world ?? this.joinMsg?.world ?? null, worldId: this.replica.worldId, head: this.replica.head, you: this.replica.you });
      this.each("adopted", rest, cut);
    }
  }

  log(m: any) {
    if (!this.replica?.active || this.resyncing) return;
    const r = this.replica.ingest(m.entries ?? [], m.from ?? -1, m.names);
    if (r.resync) return this.resync(r.why);
    this.each("ingested", r, m);
  }

  /** This cut can't continue: re-send the join for a fresh snapshot (client/lib/net.js:357, mcpl/agent.ts:1311). */
  resync(why: string) {
    if (!this.joinMsg) return;
    this.resyncing = true;
    console.error(`eido-bridge: can't continue this cut (${why}): re-joining`);
    this.sendServer(JSON.stringify(this.joinMsg));
  }

  local_(m: any) {
    if (m.type === "bridge/status") return this.send(this.status());
    const h = TABLE.local.get(m.type);
    if (!h) return this.error(m.type, "unknown", `no such bridge message: ${m.type}`);
    const fail = (e: unknown) => { console.error(`eido-bridge: ${h.slice.name} ${m.type}:`, e); this.error(m.type, "failed", String((e as Error)?.message ?? e).slice(0, 200)); };
    try { const p = h.fn(this.ctxFor(h.slice), m); if (p && typeof (p as any).catch === "function") (p as Promise<void>).catch(fail); } catch (e) { fail(e); }
  }

  status() {
    const r = this.replica;
    return { type: "bridge/status", active: !!r?.active, world: this.joinMsg?.world ?? null, worldId: r?.worldId ?? null, head: r?.head ?? -1,
      entities: r?.st ? Object.keys(r.st.entities).filter((k) => k !== "@world").length : 0, stats: r?.stats ?? null,
      viewHash: r?.active ? viewHash(r.view()) : null };
  }

  serverClosed(code: number, reason: string) {
    if (this.phase === "closed") return;
    const wasDialing = this.phase === "dialing";
    if (wasDialing) this.error("bridge/hello", "unreachable", "the server couldn't be reached");
    this.phase = "closed"; this.failPending(); this.each("closed");
    if (!wasDialing && sendable(code)) this.local.close(code, reason); else this.local.terminate();
  }

  /** The renderer went away. */
  end() { if (this.phase === "closed") return; this.phase = "closed"; this.failPending(); this.each("closed"); try { this.server?.close(1000); } catch {} }
}
