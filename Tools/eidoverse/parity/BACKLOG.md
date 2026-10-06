# Parity backlog (2026-10-06)

Specs live in the four audit reports next to this file: `ui-protocol.md`, `avatars.md`,
`world.md`, `audio.md`. Ticks are updated as work lands.

## M1 Protocol and chat
- [x] caption, typing state glyphs (ear/think/tool/mic), spoken say merge (utt)
- [x] server `error` text → toast + system line every time
- [x] recentChat on join, chatTotal note; history paging (scroll to top)
- [x] whisper echo (no triple lines), `/r`, LastWhisperFrom
- [x] command table: /me /who /clear /name /help /panels /role /grant /kick /ban /unban /bans /gban /gunban /gbans
      /punt /push /touch /letgo /pushable /boom /epoch /fork /reset /goto /sit /emote /use(+aliases) /mount /dismount /debug
- [x] world-reset, world-forked, mod, defs-updated, avatar-updated line, arrive/leave lines, rights notice, empty-world toast
- [x] log narration: grant/ban/unban/kick/asset
- [x] restore pose on first snapshot, adopt renamed `you`
- [x] verb queue while reconnecting; close codes 4002/4003/4005/4006
- [x] presence (present/away/busy) send/receive
- [x] diver anchor bug (-85 cm for feet-anchored humanoid swimmers/climbers/pilots)

## M2 Bodies
- [x] remote ragdoll: per-sample pose blending, SetLimp (park non-driven bones, eyes shut, limp springs)
- [x] puppet consent (posable off / pushable on), self held pose sent in `pose`, anim rebroadcast
- [x] remote mounts from snapshot.state.mounts + log (riders pinned to seat), mount.yaw
- [x] wings: flap idle/power, fold (G), wingsFolded wire
- [x] jump walk-off ease, FP eye from head when posed, self gaze speaker-only, body load retry, movement bounds
- [x] seat hints, /sit, far-seat message
- [x] reach: weight easing, self solve, reached, narration

## M3 World rendering
- [x] fog/sky args (fog, sun, ambient, exposure, azimuth, clouds), night floor
- [x] placed lights: day dimming, gizmo, 8-slot budget, no shadows (server: keep/day)
- [x] inferred lamps on emissive models
- [x] geom placeholders, comp collision.enabled=false
- [x] weather surfaces: wetness, puddles, cloud shadows, cloud kind
- [x] server render-scene: particles/picture/sound passthrough, part motions, part sockets, sim v
- [x] node-hierarchy import + part motion; pictures; particles (Niagara)
- [x] residency demotion; flora pushers, keep(d) 15–90 m with growth, per-placement tilt
- [ ] flora: local placement past the 12k cap, real species meshes

## M4 Build tools
- [x] drag move (send/receive), lights editor, lock/guard, scene tree + attach, seat anchor editor,
      sky sliders, ground auto-apply, structure editor, uploads, sound/picture editors, scripts panel

## M5 Locomotion
- [x] traversal climb (E), mantle, vehicles/helm, flight (F), local ragdoll (R, shoves, force)
- [x] bodydrag owner half: grab/stream/release handover, nails (pinAt/unpin), pins in presence, markers
- [ ] bodydrag dragger half (LMB on a limp body, wheel depth, P nails)

## M6 Audio
- [x] sound components, WebRTC SFU receive, consent/volumes, mic publish (V / push-to-talk), mouths
- [x] TTS endpoints (ws synth / http), chunker conformance
- [x] STT (SFSpeechRecognizer on the voice mic; built, awaiting an authorized run)

## M7 Polish
- [x] chat: name colours, timestamps, grouping, links/markdown, ↓ recall
- [x] chat: DM tabs, autocomplete
- [x] toasts coalescing, photo mode, remote XR, distance LOD update rate, emote bar posture tiles
- [x] video settings (render scale, shadows, particle tier, avatar detail)
