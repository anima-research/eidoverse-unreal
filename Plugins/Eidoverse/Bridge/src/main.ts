// src/main.ts — the process: token on stdin, a loopback listener, a Session per connection, exit on EOF (ADAPTER-DESIGN §1.1).
import crypto from "node:crypto";
import { Session } from "./session.ts";

const TOKEN = /^[A-Za-z0-9_-]{32,256}$/;   // a subprotocol token (RFC 6455 §4.1): 32 random bytes, hex or base64url
const PROTO = "eido-bridge.";

/** The first stdin line, and a promise that settles when stdin ends (the parent is gone). */
async function readStdin() {
  const reader = Bun.stdin.stream().getReader(); let buf = "";
  while (!buf.includes("\n")) { const r = await reader.read(); if (r.done) break; buf += new TextDecoder().decode(r.value); }
  const line = buf.split("\n")[0].trim();
  const ended = (async () => { if (!buf.includes("\n")) return; for (;;) { const r = await reader.read(); if (r.done) return; } })();
  return { line, ended };
}

const sameToken = (a: string, b: string) => a.length === b.length && crypto.timingSafeEqual(Buffer.from(a), Buffer.from(b));

const { line: token, ended } = await readStdin();
if (!TOKEN.test(token)) { console.error("eido-bridge: stdin's first line must be the launch token (32–256 of A-Z a-z 0-9 _ -)"); process.exit(2); }

const listener = Bun.serve<{ session?: Session }>({
  hostname: "127.0.0.1", port: 0,
  fetch(req, srv) {
    if (new URL(req.url).pathname !== "/ws") return new Response("not found", { status: 404 });
    // a page can't drive a local bridge: any Origin is refused (a native client sends none)
    if (req.headers.has("origin")) return new Response("forbidden", { status: 403 });
    const offered = (req.headers.get("sec-websocket-protocol") ?? "").split(",").map((s) => s.trim());
    const proto = offered.find((p) => p.startsWith(PROTO) && sameToken(p.slice(PROTO.length), token));
    if (!proto) return new Response("forbidden", { status: 403 });
    return srv.upgrade(req, { headers: { "Sec-WebSocket-Protocol": proto }, data: {} }) ? undefined : new Response("upgrade failed", { status: 400 });
  },
  websocket: {
    maxPayloadLength: 16 * 1024 * 1024,
    open(ws) { ws.data.session = new Session(ws); },
    message(ws, m) { ws.data.session?.fromRenderer(String(m)); },
    close(ws) { ws.data.session?.end(); },
  },
});

process.stdout.write(JSON.stringify({ port: listener.port }) + "\n");
await ended;
listener.stop(true);
process.exit(0);
