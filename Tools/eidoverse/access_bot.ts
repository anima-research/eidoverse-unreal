// access_bot -- the other people in Eidoverse.LiveAccess (run_game_tests.sh starts it).
//
//   bun Tools/eidoverse/access_bot.ts keygen <dir>       write a scratch issuer key; prints HN_ISSUER_KEY
//   bun Tools/eidoverse/access_bot.ts <ws-url> <open-world> <closed-world> <keeper-open-world> <control-port> <key-dir>
//
// Closing a world takes a signed-in owner (a typed name can't hold a closed door), so "keeper" signs in through the
// scratch issuer (an aid1 token at /auth, as tools/native-login-wire-test.ts does) and founds two worlds: it closes
// <closed-world> to members at once (the native client is refused at that door), and keeps <keeper-open-world> open
// until POST /close-open (the native client inside is removed). "guest" walks into <open-world>, which the native
// client owns, on POST /guest. Loopback control only:
//   POST /guest, POST /close-open, GET /report {keeperClosed, keeperOpen, guestJoined, guestClosed, guestError}
import { generateKeyPairSync, createPublicKey, createPrivateKey, sign } from "node:crypto";
import { writeFileSync, readFileSync, mkdirSync } from "node:fs";
import { join } from "node:path";
if (process.argv[2] === "keygen") {
  const dir = process.argv[3]; mkdirSync(dir, { recursive: true });
  const pair = generateKeyPairSync("ed25519");
  writeFileSync(join(dir, "issuer.pem"), pair.privateKey.export({ format: "pem", type: "pkcs8" }) as string, { mode: 0o600 });
  const pub = createPublicKey(pair.privateKey).export({ format: "der", type: "spki" }) as Buffer;
  console.log(`ed25519:${pub.subarray(pub.length - 32).toString("base64url")}`);
  process.exit(0);
}
const [url, openWorld, closedWorld, keeperOpen, controlArg, keyDir] = process.argv.slice(2);
if (!/^ws:\/\/(127\.0\.0\.1|localhost):\d+\/ws$/.test(url ?? "")) { console.error("local servers only"); process.exit(2); }
const http = url.replace(/^ws:/, "http:").replace(/\/ws$/, "");
async function signIn(name: string) {
  const key = createPrivateKey(readFileSync(join(keyDir, "issuer.pem"), "utf8"));
  const now = Math.floor(Date.now() / 1000);
  const payload = { v: 1, iss: "id.test", sub: `human:discord:${name}`, kind: "human", name, aud: "eidoverse", scopes: ["worlds:join", "worlds:spectate"], iat: now, exp: now + 3600, jti: `access-bot-${name}-${now}` };
  const seg = Buffer.from(JSON.stringify(payload)).toString("base64url");
  const token = `aid1.${seg}.${sign(null, Buffer.from(`aid1.${seg}`), key).toString("base64url")}`;
  const r = await fetch(http + "/auth", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ token }) });
  const c = /^ew_sess=[a-f0-9]{64}/.exec(r.headers.get("set-cookie") ?? "")?.[0];
  if (!c) { console.error("sign-in failed", r.status, await r.text()); process.exit(3); }
  return c;
}
const report: Record<string, unknown> = { keeperClosed: false, keeperOpen: false, guestJoined: false, guestClosed: null, guestError: null };
const avatar = "eidoverse/assets/vrms/shino.vrm";

const cookie = await signIn("keeper");
// the keeper in one world: joins as its signed-in self; closes it now, or when asked
function keep(world: string, closeNow: boolean, onClosed: (role: string) => void) {
  const ws = new WebSocket(url, { headers: { cookie } } as any);
  let joined = false;
  ws.onopen = () => ws.send(JSON.stringify({ type: "join", world, id: "keeper", avatar, session: `keeper-${world}` }));
  ws.onmessage = (ev) => {
    const m = JSON.parse(String(ev.data));
    if (m.type === "snapshot" && !joined) { joined = true; if (closeNow) close(); else report.keeperOpen = true; }
    if (m.type === "log" && m.entry?.verb === "grant" && m.entry?.args?.id === "*") onClosed(m.entry.args.role);
    if (m.type === "error") console.log(JSON.stringify({ keeper: world, error: m.error }));
  };
  const close = () => ws.send(JSON.stringify({ type: "verb", verb: "grant", args: { id: "*", role: "none" } }));
  setInterval(() => { if (joined) ws.send(JSON.stringify({ type: "pose", pose: { p: [0, 0, 0], yaw: 0, speed: 0, clip: "idle" } })); }, 500);
  return close;
}
keep(closedWorld, true, (role) => { report.keeperClosed = role; });
const closeOpen = keep(keeperOpen, false, (role) => { report.keeperOpenClosed = role; });

let guest: WebSocket | null = null;
function guestIn() {
  if (guest) return;
  guest = new WebSocket(url);
  guest.onopen = () => guest!.send(JSON.stringify({ type: "join", world: openWorld, id: "guest", avatar, session: "guest-page-1" }));
  guest.onmessage = (ev) => {
    const m = JSON.parse(String(ev.data));
    if (m.type === "snapshot") report.guestJoined = true;
    if (m.type === "error") report.guestError = m.error;
  };
  guest.onclose = (ev) => { report.guestClosed = ev.code; };
  setInterval(() => { if (guest && guest.readyState === WebSocket.OPEN) guest.send(JSON.stringify({ type: "pose", pose: { p: [3, 0, 3], yaw: 0, speed: 0, clip: "idle" } })); }, 500);
}

Bun.serve({
  hostname: "127.0.0.1", port: Number(controlArg ?? 19045),
  fetch(req) {
    const u = new URL(req.url);
    if (u.pathname === "/report") return Response.json(report);
    if (u.pathname === "/guest" && req.method === "POST") { guestIn(); return Response.json({ ok: true }); }
    if (u.pathname === "/close-open" && req.method === "POST") { closeOpen(); return Response.json({ ok: true }); }
    return new Response("no", { status: 404 });
  },
});
setTimeout(() => process.exit(0), 10 * 60 * 1000);
