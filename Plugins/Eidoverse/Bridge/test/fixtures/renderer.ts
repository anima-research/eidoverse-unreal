// test/fixtures/renderer.ts — the Unreal client's messages as its code writes them (eidoverse-unreal@59df129, Plugins/Eidoverse/Source/EidoverseClient/Private/).

export type Fixture = { type: string; src: string; raw: string };
const fx = (src: string, m: Record<string, unknown>): Fixture => ({ type: String(m.type), src, raw: JSON.stringify(m) });

/** The join, as SendJoin writes it (no replica: the C++ doesn't fold). */
export const join = (world: string, id: string) => fx("EidoverseBridge.cpp:278-287",
  { type: "join", world, id, avatar: "eidoverse/assets/vrms/shino.vrm", spectate: false, session: "page-0123456789abcdef" });

/** Renderer → server lanes the bridge passes verbatim (ADAPTER-DESIGN §3.3 PASS rows). `who` is another person present. */
export const upLanes = (who: string, thing = "thing-1"): Fixture[] => [
  fx("EidoverseBridge.cpp:845-882 (pose_bot.ts:50)", { type: "pose", pose: { p: [0.5, 0, 0.5], q: [0, 0, 0, 1], yaw: 0, speed: 0, clip: "idle", pitch: 0, locomotion: { mode: "walk", body: "avatar", medium: "air" }, presence: "present", wingsFolded: false, mic: false, hear: true, emote: "wave" } }),
  fx("EidoverseBridge.cpp:152-157", { type: "typing", to: who }),
  fx("EidoverseVoice.cpp:594", { type: "typing", state: "mic" }),
  fx("EidoverseConsent.cpp:97-102 (pose_bot.ts:35)", { type: "anim", dur: 1.6, tracks: { head: [[0, [0, 0, 0, 1]], [0.8, [0.1, 0, 0, 0.995]]] }, loop: true, replace: true }),
  fx("EidoverseConsent.cpp:148-152 (drag_bot.ts:27)", { type: "puppet", target: who, ragdoll: { lean: [2.5, 0, 0] }, pose: null, anim: null }),
  fx("EidoverseBodyDrag.cpp:98-100 (drag_bot.ts:30)", { type: "bodydrag", target: who, grab: { joint: "hips" } }),
  fx("EidoverseBodyPhysics.cpp:110", { type: "bodydrag", target: who, end: true }),
  fx("EidoverseUIBuildTools.cpp:150-153 (pose_bot.ts:41)", { type: "drag", id: thing, pos: [1.5, 0, 0], yaw: 0 }),
  fx("EidoverseCapture.cpp:39-44", { type: "capture-capabilities", capture: { version: 1, engine: "unreal", scene: "world-projection" } }),
  fx("EidoverseCommands.cpp:410-415", { type: "debug", reqId: "d1", limit: 12 }),
  fx("EidoverseCommands.cpp:396-400", { type: "debug", reqId: "d2", behaviors: true }),
  fx("EidoverseProtocol.cpp:330-338", { type: "history", reqId: "h1", limit: 40, verbs: ["say"] }),
  fx("EidoverseCommands.cpp:258", { type: "world-bans" }),
  fx("EidoverseCommands.cpp:267", { type: "global-bans" }),
  fx("EidoverseCommands.cpp:350", { type: "world-fork", to: "glade-fork" }),
  // voice (EidoverseVoice.cpp; voice_bot.ts:98-142)
  fx("EidoverseVoice.cpp:380-382 (voice_bot.ts:132)", { type: "relay-cred" }),
  fx("EidoverseVoice.cpp:381 (voice_bot.ts:132)", { type: "voice-consent", recv: true }),
  fx("EidoverseVoice.cpp:458", { type: "sfu-want-negotiate" }),
  fx("EidoverseVoice.cpp:465 (voice_bot.ts:121)", { type: "sfu-answer", sdp: "v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\n", gen: 1 }),
  fx("EidoverseVoice.cpp:619 (voice_bot.ts:98)", { type: "sfu-ice", candidate: { candidate: "candidate:1 1 udp 1 127.0.0.1 9 typ host", sdpMid: "0", sdpMLineIndex: 0 }, gen: 1 }),
  fx("EidoverseVoice.cpp:582 (voice_bot.ts:134)", { type: "sfu-pos", x: 0.5, y: 0, z: 0.5 }),
  // last: world-reset (a non-owner's is refused; nothing is reset)
  fx("EidoverseCommands.cpp:355", { type: "world-reset", name: "glade" }),
];

/** Server → renderer PASS lanes (§3.4), as the C++ reads them (EidoverseBridge.cpp:684-818, EidoverseProtocol.cpp:136-286, EidoverseVoice.cpp:388-423). */
export const downLanes = (who: string): Fixture[] => [
  fx("EidoverseBridge.cpp:757-762 (avatar_fixture_server.ts:39)", { type: "frame", t: 1786600887.5, poses: { [who]: { p: [-26, -7, 76], yaw: 0, speed: 0, clip: "idle", anim: "wave" } } }),
  fx("EidoverseBridge.cpp:733-740 (avatar_fixture_server.ts:21)", { type: "arrive", id: "avatar-a", avatar: "eidoverse/assets/vrms/claude_suit.vrm", session: "s1", actorId: "u-aaaaaaaaaaaaaaaaaaaa" }),
  fx("EidoverseBridge.cpp:741-749 (avatar_fixture_server.ts:22)", { type: "avatar-updated", path: "eidoverse/assets/vrms/claude_suit.vrm", v: 1786600887001, name: "avatar-a" }),
  fx("EidoverseBridge.cpp:750-756 (avatar_fixture_server.ts:25)", { type: "leave", id: "avatar-a" }),
  fx("EidoverseBridge.cpp:786-792", { type: "your-rights", rights: { role: "builder", permissions: ["build"], rank: 1 } }),
  fx("EidoverseBridge.cpp:793-805", { type: "typing", id: who, to: "me" }),
  fx("EidoverseBridge.cpp:793-805", { type: "caption", id: who, text: "hello" }),
  fx("EidoverseBridge.cpp:793-805", { type: "anim", id: who, dur: 1, tracks: {}, loop: false }),
  fx("EidoverseBridge.cpp:793-805", { type: "puppet", by: who, ragdoll: { lean: [1, 0, 0] } }),
  fx("EidoverseBridge.cpp:793-805", { type: "bodydrag", by: who, grab: { joint: "hips" } }),
  fx("EidoverseBridge.cpp:793-805", { type: "drag", id: "thing-1", by: who, pos: [1, 0, 0], yaw: 0 }),
  fx("EidoverseProtocol.cpp:136-147", { type: "debug", reqId: "d1", lines: ["ok"] }),
  fx("EidoverseProtocol.cpp:148-193", { type: "history", reqId: "h1", entries: [], oldestSeq: -1, hasMore: false }),
  fx("EidoverseProtocol.cpp:194-210", { type: "mod", text: "a moderator note" }),
  fx("EidoverseProtocol.cpp:227-240", { type: "notice", text: "the server restarts in 5 minutes" }),
  fx("EidoverseProtocol.cpp:241", { type: "defs-updated" }),
  fx("EidoverseProtocol.cpp:242-260", { type: "world-forked", to: "glade-fork" }),
  fx("EidoverseProtocol.cpp:261-270", { type: "world-reset", name: "glade" }),
  fx("EidoverseProtocol.cpp:271-283", { type: "error", error: "something was refused" }),
  fx("EidoverseProtocol.cpp:284", { type: "geom", geom: { "eido/sha256/00": { bbox: { center: [0, 0.5, 0], size: [1, 1, 1] } } } }),
  fx("EidoverseBridge.cpp:732 (snapshots.ts:312)", { type: "capture", id: "c1", follow: who, view: "front", width: 640, height: 360 }),
  // voice ×7 (EidoverseVoice.cpp:388-423)
  fx("EidoverseVoice.cpp:388", { type: "relay-cred", urls: ["turn:127.0.0.1:3478"], username: "u", credential: "c", mediaGen: 1 }),
  fx("EidoverseVoice.cpp:389", { type: "sfu-route", speaker: who }),
  fx("EidoverseVoice.cpp:390", { type: "sfu-offer", sdp: "v=0\r\n", gen: 1 }),
  fx("EidoverseVoice.cpp:395", { type: "sfu-ice", candidate: { candidate: "candidate:2 1 udp 1 127.0.0.1 9 typ host" }, gen: 1 }),
  fx("EidoverseVoice.cpp:401", { type: "surface-transition", surface: "voice", id: who, gen: 1 }),
  fx("EidoverseVoice.cpp:411", { type: "voice-service", up: true }),
  fx("EidoverseVoice.cpp:418", { type: "voice-consent", from: who, recv: true }),
];
