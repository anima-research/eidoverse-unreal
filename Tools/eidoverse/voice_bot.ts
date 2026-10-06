// Voice test bot: joins a LOCAL world as an embodied participant, speaks the SFU
// signalling exactly as the browser does (relay-cred → voice-consent → sfu-want-negotiate,
// answer every server offer with gen+cred, trickle sfu-ice, sfu-pos every 500 ms), publishes
// a continuous tone on the server's floor m-line, and reports what it hears from others.
//   bun Tools/eidoverse/voice_bot.ts <ws-url> <world> [id] [seconds] [--tone=440] [--no-tone] [--no-recv] [--pos=x,y,z]
// werift comes from the server checkout (EIDOVERSE_WORLDS, default ~/connectome-local/eidoverse-worlds-native).
// Output: one JSON object per line. `{"heard":{speaker:{packets,bytesPerPacket,meanDb,maxDb}}}`
// is printed at exit; meanDb/maxDb come from decoding the received Opus with ffmpeg.
import { tmpdir, homedir } from "node:os";
import { join } from "node:path";
import { readFileSync, writeFileSync, existsSync } from "node:fs";

const argv = process.argv.slice(2);
const flags = Object.fromEntries(argv.filter((a) => a.startsWith("--")).map((a) => { const [k, v] = a.slice(2).split("="); return [k, v ?? "1"]; }));
const [url, world, idArg, secondsArg] = argv.filter((a) => !a.startsWith("--"));
if (!/^ws:\/\/(127\.0\.0\.1|localhost):/.test(url ?? "") || !world) {
  console.error("usage: voice_bot.ts ws://127.0.0.1:<port>/ws <world> [id] [seconds] [--tone=440] [--no-tone] [--no-recv]"); process.exit(2);
}
const id = idArg ?? "voicebot";
const seconds = Number(secondsArg ?? 60);
const toneHz = flags["no-tone"] ? 0 : Number(flags.tone ?? 440);
const recv = !flags["no-recv"];
const pos = (flags.pos ?? "0,0,2").split(",").map(Number);
const repo = process.env.EIDOVERSE_WORLDS ?? join(homedir(), "connectome-local/eidoverse-worlds-native");
const { RTCPeerConnection, MediaStreamTrack, MediaStream, RTCRtpCodecParameters, RtpPacket, RtpHeader } =
  await import(join(repo, "node_modules/werift/lib/index.mjs"));
const log = (o: unknown) => console.log(JSON.stringify(o));

// ── tone: ffmpeg encodes 2 s of an integral number of cycles to Ogg Opus; we loop the packets ──
function oggPackets(buf: Uint8Array): Uint8Array[] {
  const out: Uint8Array[] = []; let cur: number[] = []; let p = 0;
  while (p + 27 <= buf.length && buf[p] === 0x4f && buf[p + 1] === 0x67) {
    const nseg = buf[p + 26]; const table = buf.subarray(p + 27, p + 27 + nseg); let d = p + 27 + nseg;
    for (const len of table) { for (let i = 0; i < len; i++) cur.push(buf[d + i]); d += len; if (len < 255) { out.push(Uint8Array.from(cur)); cur = []; } }
    p = d;
  }
  return out;
}
let tone: Uint8Array[] = [];
if (toneHz > 0) {
  const f = join(tmpdir(), `voicebot-tone-${toneHz}.opus`);
  if (!existsSync(f)) {
    const r = Bun.spawnSync(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", `sine=frequency=${toneHz}:sample_rate=48000:duration=2`,
      "-af", "volume=4", "-ac", "1", "-c:a", "libopus", "-b:a", "64k", "-frame_duration", "20", "-vbr", "off", f]);
    if (r.exitCode !== 0) { console.error(String(r.stderr)); process.exit(3); }
  }
  tone = oggPackets(readFileSync(f)).slice(2);   // drop OpusHead + OpusTags
}

// ── Ogg writer for what we hear, so ffmpeg can measure the decoded level ──
const CRC = (() => { const t = new Uint32Array(256); for (let i = 0; i < 256; i++) { let r = i << 24; for (let j = 0; j < 8; j++) r = r & 0x80000000 ? (r << 1) ^ 0x04c11db7 : r << 1; t[i] = r >>> 0; } return t; })();
function oggPage(packet: Uint8Array, granule: bigint, seq: number, flag: number): Uint8Array {
  const segs: number[] = []; let n = packet.length; while (n >= 255) { segs.push(255); n -= 255; } segs.push(n);
  const h = new Uint8Array(27 + segs.length + packet.length); const v = new DataView(h.buffer);
  h.set([0x4f, 0x67, 0x67, 0x53]); h[5] = flag; v.setBigUint64(6, granule, true); v.setUint32(14, 0x564f4943, true); v.setUint32(18, seq, true);
  h[26] = segs.length; h.set(segs, 27); h.set(packet, 27 + segs.length);
  let c = 0; for (const b of h) c = ((c << 8) ^ CRC[((c >>> 24) ^ b) & 0xff]) >>> 0; v.setUint32(22, c, true);
  return h;
}
function oggFile(packets: Uint8Array[]): Uint8Array {
  const head = new Uint8Array(19); head.set(new TextEncoder().encode("OpusHead")); head[8] = 1; head[9] = 2; new DataView(head.buffer).setUint32(12, 48000, true);
  const tags = new Uint8Array(16); tags.set(new TextEncoder().encode("OpusTags"));
  const pages = [oggPage(head, 0n, 0, 2), oggPage(tags, 0n, 1, 0)];
  packets.forEach((p, i) => pages.push(oggPage(p, BigInt((i + 1) * 960), i + 2, i === packets.length - 1 ? 4 : 0)));
  const all = new Uint8Array(pages.reduce((a, p) => a + p.length, 0)); let o = 0; for (const p of pages) { all.set(p, o); o += p.length; } return all;
}

// ── world socket + SFU client ──
const ws = new WebSocket(url);
const send = (o: unknown) => { if (ws.readyState === 1) ws.send(JSON.stringify(o)); };
let pc: any = null, cred: any = null, joined = false;
const pendingIce: any[] = [];
const mic = new MediaStreamTrack({ kind: "audio" });
const micStream = new MediaStream({ id, tracks: [mic] });
const heard = new Map<string, { packets: number; bytes: number; frames: Uint8Array[] }>();
const routeQueue: string[] = [];
const subscribed = new WeakSet<object>();

function publishOnFloor() {
  // The browser's rule (voicesfu.js): reuse our sendonly sender; else the inactive floor
  // (replaceTrack + direction sendonly); addTrack only when neither exists.
  if (!tone.length) return false;
  const ts = pc.getTransceivers();
  if (ts.some((t: any) => t.sender.track === mic && (t.currentDirection === "sendonly" || t.direction === "sendonly"))) return false;
  // werift stores the offered direction REVERSED (offerDirection "sendonly" = the server offered
  // recvonly) — that is the floor, the only m-line the server will receive on.
  const floor = ts.find((t: any) => t.offerDirection === "sendonly" && !t.sender.track);
  if (floor) { floor.sender.replaceTrack(mic); floor.setDirection("sendonly"); log({ publish: "floor", mid: floor.mid }); return true; }
  pc.addTrack(mic, micStream); log({ publish: "addTrack" }); return true;
}

function connect(c: any) {
  cred = c;
  try { pc?.close(); } catch {}
  pendingIce.length = 0; routeQueue.length = 0;
  pc = new RTCPeerConnection({ iceServers: c.iceServers ?? [], codecs: { audio: [new RTCRtpCodecParameters({ mimeType: "audio/opus", clockRate: 48000, channels: 2, payloadType: 111 })] } });
  const myPc = pc;
  pc.onicecandidate = (e: any) => { if (e.candidate && myPc === pc) send({ type: "sfu-ice", candidate: e.candidate.toJSON?.() ?? e.candidate, gen: cred.mediaGen }); };
  pc.iceConnectionStateChange.subscribe((s: string) => log({ ice: s }));
  pc.ontrack = (e: any) => {
    const viaMsid = e.streams?.[0]?.id; const viaQueue = routeQueue.shift();
    const speaker = viaMsid && viaMsid !== "default" ? viaMsid : viaQueue;
    if (!speaker || subscribed.has(e.track)) return;     // ontrack re-fires on every renegotiation
    subscribed.add(e.track);
    log({ track: speaker });
    let h = heard.get(speaker); if (!h) { h = { packets: 0, bytes: 0, frames: [] }; heard.set(speaker, h); }
    const hh = h;
    e.track.onReceiveRtp.subscribe((rtp: any) => { hh.packets++; hh.bytes += rtp.payload.length; if (hh.frames.length < 3000) hh.frames.push(Uint8Array.from(rtp.payload)); });
  };
  send({ type: "sfu-want-negotiate" });
}

async function onOffer(sdp: string) {
  if (!pc) return; const myPc = pc;
  await myPc.setRemoteDescription({ type: "offer", sdp });
  while (pendingIce.length) { try { await myPc.addIceCandidate(pendingIce.shift()); } catch {} }
  const republish = publishOnFloor();
  const answer = await myPc.createAnswer();
  await myPc.setLocalDescription(answer);
  if (myPc !== pc) return;
  send({ type: "sfu-answer", sdp: myPc.localDescription.sdp, gen: cred.mediaGen,
    cred: { world: cred.world, id: cred.id, primaryGen: cred.primaryGen, mediaGen: cred.mediaGen, incarnation: cred.incarnation, nonce: cred.nonce } });
  if (republish) log({ published: true });
}

ws.onopen = () => send({ type: "join", world, id, avatar: "eidoverse/assets/vrms/shino.vrm" });
ws.onmessage = (ev) => {
  const m = JSON.parse(String(ev.data));
  switch (m.type) {
    case "snapshot":
      if (joined) break; joined = true; log({ joined: world, id });
      send({ type: "relay-cred" }); send({ type: "voice-consent", recv });
      setInterval(() => send({ type: "pose", pose: { p: pos, yaw: Math.PI, speed: 0, clip: "idle" } }), 100);
      setInterval(() => { if (pc) send({ type: "sfu-pos", x: +pos[0].toFixed(2), y: +pos[1].toFixed(2), z: +pos[2].toFixed(2) }); }, 500);
      break;
    case "relay-cred": log({ cred: { identity: m.identity, mediaGen: m.mediaGen, iceServers: m.iceServers } }); connect(m); break;
    case "sfu-route": routeQueue.push(m.speaker); break;
    case "sfu-offer": onOffer(m.sdp).catch((e) => log({ offerError: String(e) })); break;
    case "sfu-ice": if (!pc) break; if (!pc.remoteDescription) pendingIce.push(m.candidate); else pc.addIceCandidate(m.candidate).catch(() => {}); break;
    case "voice-consent": log({ consent: m }); break;
    case "surface-transition":
      if ((m.surface === "voice" || m.surface === "voice-relay") && m.id !== id && m.gen != null && pc) send({ type: "sfu-want-negotiate" });
      break;
    case "error": log({ error: m.error }); break;
  }
};
ws.onclose = () => log({ closed: true });

// 20 ms pacer for the tone — Opus RTP timestamps advance 960 per frame.
let seq = 0, tsr = 0, frame = 0;
const ssrc = (Math.random() * 0xffffffff) >>> 0;
if (tone.length) setInterval(() => {
  const p = tone[frame++ % tone.length];
  mic.writeRtp(new RtpPacket(new RtpHeader({ payloadType: 111, sequenceNumber: seq++ & 0xffff, timestamp: (tsr += 960) >>> 0, ssrc, marker: false }), Buffer.from(p)));
}, 20);

setInterval(() => log({ rx: Object.fromEntries([...heard].map(([k, v]) => [k, { packets: v.packets, bytesPerPacket: v.packets ? +(v.bytes / v.packets).toFixed(1) : 0 }])) }), 5000);

setTimeout(() => {
  const report: Record<string, unknown> = {};
  for (const [k, v] of heard) {
    let meanDb: number | null = null, maxDb: number | null = null;
    if (v.frames.length > 50) {
      const f = join(tmpdir(), `voicebot-heard-${id}-${k.replace(/[^\w-]/g, "_")}.opus`);
      writeFileSync(f, oggFile(v.frames.slice(25)));
      const r = Bun.spawnSync(["ffmpeg", "-hide_banner", "-i", f, "-af", "volumedetect", "-f", "null", "-"]);
      const s = String(r.stderr);
      meanDb = Number(/mean_volume: (-?[\d.]+)/.exec(s)?.[1] ?? NaN); maxDb = Number(/max_volume: (-?[\d.]+)/.exec(s)?.[1] ?? NaN);
    }
    report[k] = { packets: v.packets, bytesPerPacket: v.packets ? +(v.bytes / v.packets).toFixed(1) : 0, meanDb, maxDb };
  }
  log({ heard: report });
  try { pc?.close(); } catch {}
  ws.close(); process.exit(0);
}, seconds * 1000);
