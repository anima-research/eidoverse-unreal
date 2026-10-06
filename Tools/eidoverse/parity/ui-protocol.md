# Eidoverse parity audit: UI, tools, commands and network protocol (web vs Unreal)

Web paths are relative to `~/connectome-local/eidoverse-worlds-native/client/` (lib files written as `net.js`, etc.). Server paths are relative to `.../server/`. Unreal paths are relative to `.../Plugins/Eidoverse/Source/EidoverseClient/Private/`. This was a read-only audit; no files were changed.

Status key: **Done** = works the same · **Partial** = present but differs in a way users notice · **Missing** = absent · **N/A** = does not apply to a native client (reason given).

**Architecture note.** The web client replays the world log itself through `shared/fold.js` (`state.js`). Unreal does not: it asks the server for a ready-built scene with the `render-scene` message, and treats each incoming `log` message only as "the scene is out of date" (EidoverseBridge.cpp:578). That is intended. But anything the web derives from the log itself has to be handled explicitly in Unreal: chat, narration lines, live causes such as `force`, and the asset palette.

---

## 0. Highest-impact gaps (fix first)

1. **Server error text is thrown away.** Every `{type:"error", error}` becomes the fixed status "server refused a request; check permissions/credentials" (Bridge.cpp:602). The HUD only adds a line when the status text *changes*, so a second, different refusal shows nothing. This silently swallows every refusal: rank, lock, guard, rate limit, kick/ban reasons, "X isn't here — they'll get it when they arrive". The web shows the exact text as a toast (net.js:748-764) and tells optimistic previews to roll back. **Spec S1** in §3.6.
2. **Chat is empty when you join.** Unreal never reads `snapshot.state.recentChat` (up to 40 lines of `{actor,text,ts,seq}`) and never sends `history`, so there is no way to scroll back. **Specs CH-B / CH-C** in §3.1.
3. **A whisper you send shows three times.** `Whisper()` adds a line (Bridge.cpp:274), `SendChat()` adds another (UI.cpp:1262), and the server's echo `{…, echo:true}` (messages.ts:207) adds a third (Bridge.cpp:600-601). **Spec CH-F.**
4. **No arrive/leave or moderation narration**, and `mod`, `world-reset` and `world-forked` are not handled. After a reset the Unreal scene keeps showing objects that no longer exist.
5. **Requests to your body are ignored with no reply.** Unreal never handles `puppet`, `bodydrag` or the `force` verb. A dragger's simulation streams into nothing, and a pose request gets no refusal message.
6. **Only 9 web chat commands exist in Unreal**, and most of their arguments differ. There is no moderation, no `/r`, `/who`, `/me` and others. See §2.
7. **Building:** no mouse drag-to-move, the live `drag` message is neither sent nor received, no lights, no lock/guard controls, no structure editor, no attach/detach.
8. **Key conflict:** Unreal uses **E** to use the aimed object and **F** to punt. On the web, E grabs a ladder or helm and F toggles flight (controller.js:313-318).

---

## 1. Network protocol

### 1.1 Server → client messages

Server sources: messages.ts, server.ts, verbs.ts, world.ts, sfuadapter.ts, snapshots.ts, upload.ts, seats.ts.

| type | Fields | Web handling (net.js) | Unreal | Notes / spec |
|---|---|---|---|---|
| `snapshot` | `renderSceneVersion, world, you, yourSub?, gen, yourSurfaces[{surface,gen}], recording, state, throughSeq, entries[], sim?, avatars[{name,path}], yourRights{role,gen,fly?,caption?,open}, restore, present[{id,avatar,pose,agent,surfaces[]}]` (server.ts:633-680) | `onSnapshot` (net.js:766-890) does all of the following: stores rights; stores the avatar roster; adopts the server-assigned name with an 8 s toast `you're appearing as "X"`; prints the recording notice once; restores your pose once if you have not moved; flushes verbs queued while offline; removes ghosts not in `present`; creates participants; hydrates the fold; shows `state.recentChat` with a "showing N of M" note. | **Partial** (Bridge.cpp:523-553): identity, rights, avatars, `renderSceneVersion`, `sim.epoch`, `throughSeq`, present, recording. **Missing:** `restore` (S8), `recentChat`/`chatTotal` (CH-B), name-changed notice (S9), verb-queue flush (S10), `yourSub`, `present[].surfaces`. | — |
| `your-rights` | `rights`, `causeSeq` (verbs.ts:154) | Stores rights, re-gates the UI (edit pin, conjure), and narrates `this world has an owner — you are a {role}{ +gen} here` (commands/handlers.js:410-415). | **Partial** (Bridge.cpp:588-589): stored only. | S19 |
| `arrive` | `id, avatar, agent` | Creates the participant and adds `* {id} arrived` (net.js:537-550). | **Partial** (Bridge.cpp:557): no chat line, `agent` ignored. | S3. Show an "agent" tag in People. |
| `leave` | `id` | Tears down the participant and adds `* {id} left` (net.js:552-556). | **Partial** (Bridge.cpp:566-567): no line, the person stays in the typing list. | S3 |
| `frame` | `seq, t, poses{id:pose}` at 15 Hz (server.ts:1084) | Pushes each pose; skips your own id; drops ids it has not been told about (net.js:572-584). | **Done** (Bridge.cpp:568-573, `Pose()` 465-518). Ignores pose fields `presence`, `wingsFolded`, `pins`, `xr`, `locomotion.medium`. | See pose fields in §1.4 |
| `pose` | legacy single pose | Handled (net.js:558-570). | Missing | Legacy: the server never sends it now. **N/A.** |
| `log` | `entry{seq,ts,actor,verb,args}` | Folds the entry, then passes causes on (`realize/causes.js`): say → chat, bubble, mouth; use/punt/force → physics and your body; grant/ban/unban/kick narration (causes.js:129-139). | **Partial** (Bridge.cpp:574-587): seq guard, scene marked dirty, use/punt/epoch interactions, mount/dismount seats, say → chat. **Missing:** narration (S5), `force` (S12), `asset` palette toast, spoken merge (S6). | — |
| `drag` | `id, pos, yaw, by` | Live preview of someone else's build drag (build.js:331-337). | **Missing** | S14 / BT-A/B |
| `history` | `reqId, entries[], oldestSeq, hasMore` (world.ts:235-262) | Paging promise (net.js:592-598, chat.js:162-196). | Missing | CH-C |
| `debug` | `reqId` plus `events[]`, `sim`, or `{behavior,status,events}` | `/debug`, scripts console (scenegraph.js:337-397). | Missing | §2 `/debug`, BT-G |
| `inspect` | `id, reqId, lib, kind, parent, comp` or `error:not_found / too_large(types,size)` | Not used: the web reads its own fold. | **Done** (Bridge.cpp:591; UIInspect.cpp). | Native only |
| `render-scene` | `version, world, seq, time, moving, simulation{enabled,foreign,activeBodies}, updateIntervalMs, upserts[], removed[], transforms[], environment?, warnings` or `error` | Not used. | **Done** (Worlds.cpp:113-141) | Native only |
| `whisper` | `from, to, text, ts, echo?` | `logWhisper`: DM tab, unread count, ping, `lastWhisperFrom` (chat.js:1009-1043). | **Partial / buggy** (Bridge.cpp:600-601): `echo` ignored, so it duplicates. | CH-F |
| `typing` | `id, to?, state?` (`ear`/`think`/`tool`/`mic`) | "X is typing…" plus dots or a state glyph on the avatar (net.js:634-639). | **Partial** (Bridge.cpp:592-599): dots only; `state` glyph ignored. | Show `state`: ear → 👂, think → 💭, tool → 🔧, mic → 🎙 instead of the dots, for 4 s. |
| `caption` | `id, text, utt` | Bubble and mouth movement per spoken sentence (voicemouths.js:102). | Missing | S15 |
| `anim` | `id, dur, tracks, loop, replace` | `playAnimation` (net.js:641-645). | **Done** (Seats.cpp:243-253) | — |
| `puppet` | `by, pose, merge, anim{…,replace}, ragdoll: true\|{lean[3]}` | Consent-gated: posable (default off) / pushable (default on); toasts on refusal (localbody.js:365-394). | **Missing** (only mentioned in `/help`, Interactions.cpp:161) | S11 |
| `bodydrag` | `by, grab?, end?, pose?, p?, yaw?, sim?{j,p,v}, pinAt?, unpin?, pins?` | Grab / end / pins handshake (bodydrag.js:268-336). | **Missing** (no reply, so the dragger hangs) | S13 (minimum: reply `{end:true}`) |
| `lease` | `op`: granted(+`from`), denied(`why`), lost(`to`), claimed(`by`), state(`by,p,yaw?,q?`), released | Physics volunteer; feedback `can't kick {id} — {why}`, `{to} took {id}` (physobj.js:331-370). | **Done** for the mechanics (Interactions.cpp:89-107); feedback is only stored in `InteractionStatus`, which nothing displays. | S16 |
| `geom` | `geom{lib:{bbox}}` | Correctly sized placeholder boxes (models.js:928). | Missing (fixed 0.25 m cube, Scene.cpp:149) | S23 (low priority) |
| `snap` | `id, follow, view(first/third/selfie), width 960, height 540` | Answered only when joined as `?renderer` (net.js:687-692). | **Done**, opt-in (Capture.cpp:47-131) | — |
| `avatar-updated` | `name, path, v` | Swaps the body and adds `avatar "X" updated (vN)`. | **Partial** (Bridge.cpp:558-565): no line. | S21 |
| `avatar-profile-updated` | `name, pose, rev` | Notes the seat-profile cache revision (seats.js:39). | Missing | Low; only if Unreal caches seat profiles |
| `defs-updated` | — | Re-fetches `/defs`; repaints emotes, sky, ground, help, world (net.js:713-718). | Missing | S20 |
| `world-forked` | `from, to` | Chat line with a link plus a 12 s toast. | Missing | S7 |
| `world-reset` | `world, by` | Toast, line, local state reset, reload after 1.8 s. | Missing | S7 |
| `mod` | `text` (lines split on `\n`) | System lines (`/bans`, `/gban` replies). | Missing | S7 |
| `error` | `error` | Toast with the exact text; tells previews to roll back; notes "no voice relay". | **Partial / bad** | S1 |
| `performed` | `rung, incarnation, id, seq, gen, surface` | Cancels the local-TTS fallback (causes.js:49). | Missing | **N/A** until Unreal speaks chat aloud; then required to avoid speaking every line twice. |
| `surface-transition` | `id, surface, gen, retired` | Tracks which people have a live voice leg. | Missing | Same as `performed` |
| `voice-service`, `voice-consent` (ack), `relay-cred`, `sfu-offer`, `sfu-route` | WebRTC SFU voice signalling | voicesfubridge.js / voicesfu.js | Missing | Voice spec V below |
| `sfu-ice` (server → client) | — | Handled (net.js:603) | — | The server never sends it (ICE candidates come bundled in the offer). **N/A.** |
| `voice-moderated` | `id, muted, by` | **Not handled by the web either** | — | Server-only today. |

### 1.2 Client → server messages

| type | Fields | Web sender | Unreal | Notes |
|---|---|---|---|---|
| `join` | `world, id, avatar, spectate, renderer, token, agentToken?, surface?, agent?, capture?` | net.js:198-209 | **Done** (Bridge.cpp:213-223). Rejoining in place to change avatar is also done (`ChangeAvatar`). | — |
| `verb` | `verb, args` | net.js:95-102; queues verbs while offline | **Partial** (Bridge.cpp:239-244): nothing is queued, no feedback when not sent. | S10 |
| `pose` | see §1.4; 66 ms throttle | net.js:231-288 | **Partial** (Bridge.cpp:604-647, 15 Hz) | §1.4 |
| `typing` | `to?, state?` | chat.js:932-938; mic glyph from micstate.js:143 | **Done** for `to` (Bridge.cpp:137-143) | `state` only matters once voice exists |
| `whisper` | `to, text` (≤ 4000) | net.js:175 | **Done** (Bridge.cpp:269-275), apart from the duplicate line | CH-F |
| `history` | `reqId, before?, after?, limit (≤300, default 50), verbs?[]` | chat.js:170 (`limit:40, verbs:['say']`) | Missing | CH-C |
| `debug` | `reqId, limit?, kinds?[], sim?, behavior?, behaviors?` | net.js:138-146 | Missing | `/debug`, BT-G |
| `inspect` | `id, reqId?` (rate-limited to one per 50 ms) | — | **Done** (Bridge.cpp:124-129) | — |
| `render-scene` | `version:1, reset?` | — | **Done** (Worlds.cpp:106-110) | — |
| `anim` | `dur, tracks, loop, replace` (≤ 64 KB) | localbody.js:394; main.js:829 | Missing | Needed to re-broadcast an accepted puppet `anim` (S11) |
| `puppet` | `target, pose, merge?, anim, ragdoll` | `/push`, `/punt <person>` (handlers.js:200); main.js:819-826 | Missing | §2 `/push` |
| `bodydrag` | `target, grab\|end\|pose\|p\|yaw\|sim\|pinAt\|unpin\|pins` | bodydrag.js | Missing | S13 |
| `lease` | `op: claim(take?) / state(p,yaw?,q?) / release(p?,yaw?), id` | physobj.js; vehicles.js | **Done** (Interactions.cpp:57-63, 87) | — |
| `drag` | `id, pos, yaw` (≤ 1 per 66 ms; relayed only for rank ≥ 1) | build.js:327 | Missing | BT-A |
| `caption` | `text (≤500), utt` | Not sent by the web page (voice leg only) | Missing | **N/A** (voice-surface leg) |
| `attest` | `seq, digest, gen` | Voice leg only | Missing | **N/A** |
| `world-fork` | `to` (`^[A-Za-z0-9_-]{1,64}$`), owner only | net.js:149 | Missing | `/fork` |
| `world-reset` | `name` = current world, owner only | net.js:158 | Missing | `/reset` |
| `world-bans` | — | `/bans` | Missing | — |
| `global-ban` / `global-unban` / `global-bans` | `id, reason?(≤200)` / `id` / — ; WORLD_ADMIN only | `/gban`, `/gunban`, `/gbans` | Missing | — |
| `capture-capabilities` | `capture:{version:1, engine, scene}\|null` | — (the web uses the `renderer` join flag instead) | **Done** (Capture.cpp:31-45) | Native only |
| `snap-result` | `id, dataUrl\|error` | net.js:691 | **Done** (Capture.cpp:52+) | — |
| `bc` | `tag` (crash breadcrumbs) | bc.js:16 | Missing | **N/A** (developer forensics) |
| `relay-cred`, `voice-consent{recv}`, `sfu-answer{sdp,gen,cred}`, `sfu-ice{candidate,gen}`, `sfu-pos{x,y,z}`, `sfu-want-negotiate` | Voice | voicesfubridge.js, voicesfu.js | Missing | V |
| `voice-moderate` | `id, muted` (owner) | Not sent by the web | — | **N/A** |

**V. Voice (both directions), outline only.**
- Ask for a credential with `relay-cred {}` and receive `relay-cred {...cred, gen}`.
- The server sends `sfu-offer {sdp}`. Answer with `sfu-answer {sdp, gen: cred.mediaGen ?? cred.gen, cred}` and trickle `sfu-ice {candidate, gen}`.
- `sfu-route {speaker}` maps incoming tracks to speakers.
- Send `sfu-pos {x,y,z}` (2 dp, wire metres) for the proximity gate, and `voice-consent {recv}` whenever the "hear voices" setting changes.
- While the mic is live, send `typing {state:"mic"}`.
- This needs a native WebRTC stack (for example UE's WebRTC third-party module, or libwebrtc). It is a project in its own right; until then the audio panel should say voice is unavailable (§3.2, WP-18).

### 1.3 Verbs (all sent as `{type:"verb", verb, args}`)

Minimum rank comes from rights.ts:59-99: 0 visitor, 1 builder, 2 owner. Fold behaviour is in shared/fold.js:181-528; validators in verbs.ts.

| verb | rank | args shape | Web sender | Unreal sender | Unreal receive |
|---|---|---|---|---|---|
| `say` | 0 | `{text, spoken?, utt?, t0?, voiced?}` (spoken fields are kept only when `spoken===true` and `utt` is an integer ≥ 0; `t0` is clamped to the last 300 s) | main.js:155; stt.js:175 | **Done** (Bridge.cpp:261-268; 0.4 s rate limit, text ≤ 4000) | **Partial** (no spoken merge or `t0` placement) |
| `use` | 0 | `{id, action}` | `/use`, `/push <thing>`, aliases | **Done** (Interactions.cpp:41-45; E key) | Status only |
| `punt` | 0 | `{id, power 0.5–10 (default 5), dir?[3]}`; the server checks you are within 4 m; `dir` is required under an epoch | physobj.js:286 | **Partial** (Interactions.cpp:46-56) | **Done** (lease volunteer) |
| `force` | 1 | `{at[3], radius 0.5–30 (default 4), power 0.2–12 (default 3)}` | `/boom` | Missing | **Missing** (S12) |
| `spawn` | 1 | `{id, lib, pos, yaw, scale?, collide?}` (the server adds `box` under an epoch) | build.js:357; structure_ui.js:159; worldquad.js:37 | **Done** (UIBuild.cpp:250, 392) | via render-scene |
| `place` | 1 | `{id, pos, yaw?, scale?}` | build.js:398; scenegraph.js:231 | **Done** (UIBuild.cpp:219) | render-scene |
| `remove` | 1 | `{id}` | build.js:426; scenegraph.js:263 | **Done** (UIBuild.cpp:313; UIWorld.cpp:459) | render-scene |
| `light` | 1 | `{id, pos?, color?(int), intensity? (default 16), range? (default 10), keep?, day?}`; a repeat on the same id is a partial update | build.js:352; lights.js | **Missing** | render-scene |
| `comp` | 1 | `{id, type (≤32), data \| null}`; `captions` is refused | many (lock, guard, sockets, structure, sound, picture) | **Done**, generic (UIInspect.cpp:75) | render-scene |
| `motion` | 1 | `{id, type \| null, ...params, t0?}` | (agents / comp) | Missing (use `comp` motion) | render-scene |
| `mount` | 1 (0 when mounting yourself) | `{id, to, slot?, offset?[3], yaw?}` | localbody.js:154; scenegraph.js:297, 313 | **Partial**: yourself only (Seats.cpp:86) | **Done** for seats (Bridge.cpp:580-583) |
| `dismount` | 1 (0 for yourself) | `{id, pos?[3], yaw?}` | localbody.js:93; scenegraph.js:326 | **Partial**: yourself only (Seats.cpp:138) | **Done** |
| `behavior` | 1 | `{id, src, runtime?:"client", attach?, caps?, knobs?}` or `{id, remove:true}` | mods.js:313; scenegraph.js:370 | Missing | — |
| `asset` | 1 + gen | `{name, path}` | palette.js:164; conjure.js:316 | Missing | Missing (palette toast) |
| `terrain` | 2 | `{seed, size:160, segments:200, amplitude, flatRadius:16, layers:[{color, repeat:16}]}` | groundpanel.js:45 | **Done** (UIWorld.cpp:418) | render-scene |
| `grass` | 2 | `{...planting.args, height?, color?, density}` or `{clear:true}` | groundpanel.js:61, 101 | **Done** (UIWorld.cpp:430, 450) | render-scene |
| `sky` | 2 | `{hours, rate?, azimuth, sun?, ambient?, fill?, exposure?, fog?, clouds?, weather?, weatherSeconds?, clock?:"real", tz?}` | skypanel.js:284 | **Partial** (UIWorld.cpp:401; hours, clouds, weather only) | render-scene |
| `weather` | 2 | `{weather, ...}` (folds over the existing sky) | (agents) | Missing | render-scene |
| `epoch` | 2 | `{sim: SIM_ID, tickMs 16–1000 (default 66)}` or `{sim:null}` to leave | `/epoch` | Missing | **Done** (Interactions.cpp:72) |
| `grant` | 2 | `{id, role?, gen?, fly?, caption?{id,born}\|null, sub?}` | `/grant` | Missing | narration missing |
| `kick` / `ban` | 2 | `{id, reason?}` (ban may add `sub`) | `/kick`, `/ban` | Missing | narration missing |
| `unban` | 2 | `{id}` | `/unban` | Missing | narration missing |
| `caption` | 0 + caption deed | `{id, ...}` (screen captioner) | — | — | **N/A** (bot verb) |
| `genesis`, `bstate` | server only | — | — | — | **N/A** |

### 1.4 Pose payload (`{type:"pose", pose}`)

The web builds it in net.js:231-288; the server sanity-checks it in posecheck.ts.

| field | Web | Unreal send (Bridge.cpp:604-647) | Unreal receive (`Pose()`) |
|---|---|---|---|
| `p[3]`, `yaw`, `q[4]`, `speed`, `clip`, `pitch` | yes | yes (pitch only when walking) | yes |
| `clipTime`, `clipTimeSlot`, `clipRate` | yes | yes | yes |
| `locomotion{mode, body, medium}` | yes | yes | mode and body |
| `emote` (one-shot) | yes | yes | yes |
| `pose` (held bone pose; `null` clears) | yes | **never sent** | yes |
| `reach` | yes | yes (`/reach`) | yes |
| `presence` (`present`/`away`/`busy`) | always | **Missing** | **Missing** (CH-I) |
| `wingsFolded` (bool, always sent) | always | **Missing**; send `false` | ignored; needs the G key and wing rigs |
| `pins[{j,at}]` | yes | — | ignored (comes with ragdoll) |
| `xr` | WebXR only | — | **N/A** to send; rendering others' head and hands is optional |

**Close codes** (net.js:405-469). 4002 = taken over elsewhere, 4003 = bad key or expired session, 4005 = malformed world name, 4006 = kicked or banned; for all four, no retry. Anything else retries at 1.2 s, ×1.6 each time, capped at 15 s. Unreal treats 4000-4099 as a single case (Bridge.cpp:255). See S4.

---

## 2. Chat slash commands

Web sources: `commands/registry.js:17-60` (names, aliases, help, autocomplete) and `commands/handlers.js`. `chat.js:625-665` handles `w`, `r`, `name`, `me`, `who`, `clear` and `audio` itself. An unknown command logs `unknown command /x — try /help`.

Unreal sources: `EidoverseUI.cpp:1257-1270` handles `/w` and `/whisper` and passes every other `/…` to `HandleInteractionCommand` (Interactions.cpp:153-164). That function knows `/use`, `/punt`, `/kick` (mapped to punt!), `/reach`, `/clear_reach`, `/wave`, `/cheer`, `/point` and `/help`. Anything else **falls through to `Say()`**, so a mistyped command is posted publicly to the room. The legacy Slate panel works the same way (Panel.cpp:124).

| Command (aliases) | Args → effect (web) | Web ref | Unreal |
|---|---|---|---|
| `/w` (`/whisper`) | `/w <name> <msg>` → `whisper{to,text}`. With no message, opens a DM tab. | chat.js:628-634 | **Partial**: sends, but duplicates the line; no tab (CH-F) |
| `/r` (`/reply`) | Replies to the last whisper received; no text opens the tab; otherwise "nobody has whispered you yet" | chat.js:635-639 | Missing (CH-F) |
| `/name` (`/rename`) | No args: "you are X — /name <new name> to change it". With args: "renaming mid-session isn't supported yet…" | chat.js:640-643; handlers.js:383-388 | Missing. Add both messages; a rename is a door re-entry. |
| `/me` | `say "* {name} {text}"` | chat.js:644 | Missing (currently posted literally) |
| `/emote <name>` | Unknown name lists `emotes: …`; otherwise plays the emote and sets `pose.emote` | handlers.js:365-372 | **Partial**: only `/wave`, `/cheer`, `/point`. Add `/emote` using `Bridge->Emote(name)` checked against defs emotes. |
| `/sit [thing]` | A declared seat nearby (named thing first), else sit on the ground | handlers.js:360-363 | Missing as a command (the X key exists). Map to `TryMountNearest`, falling back to `Posture="sit"`. |
| `/push` (`/shove`) | `/push <thing>` sends `use{id,action:"push"}`. `/push [person] [power]` (power 0.5–4, default 2.2; nearest person within 2.5 m) sends `puppet{target, ragdoll:{lean:[nx·p,0,nz·p]}}`, where n = unit vector from you to the target in XZ, or your facing direction if closer than 0.05 m. Lines: `you push X` / `X is too far away to push (d m)` / `nobody within reach to push`. | handlers.js:160-202 | Missing |
| `/touch [name\|me] [point words] [left\|right]` | Reach descriptor `{who, point}` on the right hand by default, `shoulder_l` by default. Nearest person within 6 m when no name. Points come from `shared/contact.js` `CONTACT_POINTS` / `canonicalPoint` (`head` → `head_top`). Lines: "you reach for X's point (right hand)…", then after 600 ms a gap report: reached when gap ≤ `TOUCH_GAP` (shared/reachwire.js). | handlers.js:216-276 | **Partial** as `/reach <person> <point> [left]` (Interactions.cpp:158). Rename to `/touch` and add the defaults, nearest-person search, point canonicalisation and narration. |
| `/letgo` (`/release`) `[left\|right]` | Clears one or both reaching hands; "you let go" / "you lower your left hand" | handlers.js:278-283 | **Partial** as `/clear_reach` (no per-hand form) |
| `/eyes close\|open` | Local eyelid bones; "this body has no eyelid bones…" | handlers.js:54-63 | Missing (local only, low priority) |
| `/pushable on\|off` | Consent toggle, saved in `ew-pushable`. Reports "shoves and blasts CAN/can NOT knock you over". | handlers.js:284-288 | Missing (S11 console variable) |
| `/boom` (`/blast`) `[power] [radius]` | `force{at: your position, power?, radius?}` | handlers.js:290-301 | Missing |
| `/kick <name> [reason]` | **Moderation**: `kick{id, reason?}`. No name: "usage: /kick <name> [reason] — they can rejoin; /ban keeps them out". | handlers.js:93-107 | **Wrong**: Unreal maps `/kick` to the punt physics (Interactions.cpp:157). Fix to moderation. |
| `/punt` (`/boot`) `[thing\|person] [power]` | Things win the name lookup; a person's name is the shove (`puppet` ragdoll, power 0.5–4, default 2.6); no name punts the nearest thing (BT-H) | handlers.js:108-119; physobj.js:236-287 | **Partial** (name required) |
| `/epoch [tickMs\|off]` | `epoch{sim:SIM_ID, tickMs (default 66)}` or `epoch{sim:null}`, with an explanatory line | handlers.js:127-140 | Missing. `SIM_ID` comes from shared/sim.js; read it from the server or hardcode it with the protocol version. |
| `/who` | `here now: a, b, …` or "just you" | chat.js:645-648 | Missing |
| `/panels <0.3–1>` | Panel opacity; usage line when out of range | handlers.js:43-48 | Missing as a command; the setting exists (Settings Panel opacity). Set `Eidoverse.UI.PanelOpacity`. |
| `/audio [stt\|say]` | Voice and STT diagnostics | chat.js:653-661 | **N/A** (inspects WebRTC and SpeechRecognition) |
| `/role [name]` | "this world is open — everyone here can build" / `name: role +gen` / `name holds no role here (visitor)` | handlers.js:65-73 | Missing. Own role from `WorldRole`/`bWorldOpen`; other people need the roles map (not in render-scene), so answer only for yourself, or extend `inspect`. |
| `/grant <name> owner\|builder\|visitor [+gen\|-gen] [+fly\|-fly]` | `grant{id, role?, gen?, fly?}`; usage line when the role and both flags are missing | handlers.js:75-91 | Missing |
| `/ban <name> [reason]` | `ban{id, reason?}` | handlers.js:120 | Missing |
| `/unban <name>` | `unban{id}` | handlers.js:142-146 | Missing |
| `/bans` | `{type:"world-bans"}`; the reply arrives as `mod` | handlers.js:148 | Missing |
| `/gban <name> [reason]`, `/gunban <name>`, `/gbans` | `global-ban{id,reason?}`, `global-unban{id}`, `global-bans{}` | handlers.js:151-158 | Missing |
| `/fork` (`/copy`) `<new-name>` | Name check `^[a-z0-9_-]{1,64}$`i, then `world-fork{to}` | handlers.js:303-310 | Missing |
| `/reset` (`/erase`) `[worldname]` | Without the exact world name, prints a warning telling you to type `/reset <world>`; with it, sends `world-reset{name}` | handlers.js:312-321 | Missing |
| `/goto <name>` | Hint "`X is N m away, bearing NE`" (8-point compass from atan2(dx,dz)) | handlers.js:374-394 | Missing |
| `/clear` | Clears the chat log | chat.js:649-652 | Missing |
| `/flight` | Flight diagnostic lines | handlers.js:38 | Missing (comes with flight) |
| `/help` | Toggles the help sheet | handlers.js:34 | **Partial**: Unreal prints an interaction summary. Open the help overlay instead. |
| `/use` (`/pull`, `/ring`, `/open`; unlisted) `<thing> [action]` | `use{id, action || "use"}`. Typing an alias *is* the action: `/pull lever1` sends `use{id:"lever1", action:"pull"}`. Line "nothing here called X" for an unknown thing. | handlers.js:351-358; chat.js:658-661 | **Partial**: `/use` requires the action; no aliases or defaults |
| `/mount` (`/attach`) `<thing> <onto> [slot]`, `/dismount` (`/detach`) `<thing>` (unlisted) | See BT-F | handlers.js:336-349 | Missing |
| `/debug [n]` (unlisted) | `debug{limit: n (1–50, default 12)}`; each event shown as `HH:MM:SS [kind] k=v …` or "flight recorder is empty…" | handlers.js:323-334 | Missing |
| Unreal-only: `/wave`, `/cheer`, `/point`, `/reach`, `/clear_reach` | — | — | Keep as aliases of `/emote X`, `/touch`, `/letgo`. |

**Command dispatcher spec.**
- Replace the fall-through to `Say` with a registry table `{name, aliases[], help, listed}` matching registry.js:17-60.
- Resolve the command via name or alias. An unknown command logs `unknown command /x — try /help` and is **never** posted.
- `aliasAsAction` for `/use`: `/pull X` → `/use X pull`.
- Autocomplete reads the same table (CH-G).

---

## 3. UI panels, tools and keybindings

### 3.1 Chat, HUD shell and keybindings

**Feature table** (ordered by impact)

| # | Web feature | Web ref | Unreal |
|---|---|---|---|
| 1 | Line rendering: 400-line cap; `HH:MM` timestamp; coloured name; lines from the same author within 90 s grouped; system lines don't break a group; spoken merge; `t0` placement | chat.js:25, 206-358 | **Partial**: 300 lines, no timestamps or grouping (UI.cpp:652-682) |
| 2 | Name colours: 12-colour palette with hash assignment (CH-D) | base.js:168-215; chat.js:361-381 | **Missing** (all names in the brand colour) |
| 3 | Mentions: `@name` or the bare name; amber; frame flashes 1.4 s; mentions tab | chat.js:50-56, 1236 | **Done**, apart from the flash. Unreal treats `_` as a word boundary; the web does not. |
| 4 | Links and markdown (`**b**`, `*i*`, `_i_`, `` `code` ``); setting `ew-chat-md` | chat.js:62-117, 802 | Missing |
| 5 | Whisper DM tabs with unread count, close, per-tab drafts; `/r` | chat.js:1045-1200 | Missing (only all / mentions / system tabs) |
| 6 | History paging | chat.js:162-196 | Missing |
| 7 | Unread count and "N new · M ✱ ↓" jump button | chat.js:290-299, 405-413 | Missing |
| 8 | Autocomplete for `/` and `@` | chat.js:439-492 | Missing |
| 9 | Composer: ↑/↓ recall (50 kept, 20 persisted in `ew-chat-history`) | chat.js:950-998 | **Partial** |
| 10 | Typing indicator | chat.js:932-938, 1217-1235 | **Done** (UI.cpp:1298-1308) |
| 11 | Chat options gear: text size 11–20 (`ew-chat-fs`), markdown, people pane side | chat.js:796-850 | Missing |
| 12 | People Here side pane: presence dot, coloured name, distance; double-click opens a DM; width 72–260 px (`ew-chat-side`) | chat.js:700-770 | **Partial**: a separate "people" frame (UI.cpp:684-712) |
| 13 | Presence present/away/busy (auto-away after 180 s idle); sent as `pose.presence`; profile menu | presence.js:3-30; profile.js:73-97 | Missing |
| 14 | Toasts: info/warn/err for 5 s / 9 s; duplicates coalesce with ×N; at most 5; click to dismiss | ui.js:46-76 | **Partial** (UI.cpp:1333-1349) |
| 15 | Hint bar, with a persistent "ambient" hint layer underneath | ui.js:93-124 | **Partial** |
| 16 | Frames: drag from title or empty panel area; Alt+drag from anywhere; resize from 8 zones; snap 11 px; persisted in `ew-frame-<id>` | frames.js | **Partial** (title drag and bottom-right corner only; UI.cpp:238-285, 443-503) |
| 17 | Esc ladder, including "close all frames / reopen them" | frames.js:919-937 | **Partial** |
| 18 | ∃ menu: voice rows, pin per window, edit row greyed without rights, layout lock/reset, position saved | ui.js:586-816 | **Partial** (UI.cpp:524-570) |
| 19 | Rail: shows windows that are open or pinned (`ew-dock-pins`); edge-draggable; pulses while disconnected | ui.js:311-580 | **Partial** (fixed left column, all icons) |
| 20 | Emote bar: 3 posture tiles plus listed emotes with icons; 1–9 columns | emotebar.js; emotedefs.js:6-16 | **Partial** (UI.cpp:771-783, 935-950) |
| 21 | Front door: avatar grid with thumbnails, door-key field, "enter as name · avatar" | ui.js:870-947 | **Partial** (UI.cpp:785-888) |
| 22 | Help overlay from `defs.uiHelp`; opened with ? or H | ui.js:841-867 | **Done**; the H key is missing |
| 23 | Style: colour tokens and opacity (`ew-style-tokens`) | stylepanel.js | **Partial** (opacity only, plus Unreal-only settings) |
| 24 | Profile frame (avatars tab; satchel/worlds/friends are stubs) | profile.js | **Partial** |
| 25 | Field-schema panels, dropdown skin, icons, VR pointer, VR chat quad | panels.js, dropdown.js, icons.js, pointer.js, chat.js:1246 | **N/A**: DOM/WebXR plumbing; UMG equivalents already exist |

**Complete keybinding list** (web vs Unreal)

| Key | Web | Ref | Unreal |
|---|---|---|---|
| WASD **and arrows** | Move; walk 1.55 m/s, Shift run 4.0, Alt creep 0.55 | controller.js:274-275, 617-623 | WASD **Done** (Scene.cpp:386-391); arrows **Missing** outside build mode |
| Space | Jump; climbs onto low ledges | controller.js:753 | **Done** |
| Space / C or Ctrl / Shift in water | Rise / dive / faster | controller.js:540-548 | **Done** (Scene.cpp:423-446) |
| E | Grab a ladder / take a helm | controller.js:313-316 | **Conflict** (use object) |
| F | Toggle flight (needs the `fly` right) | controller.js:318 | **Conflict** (punt); flight missing |
| G | Fold or unfold wings | controller.js:320 | Missing |
| X | Sit (seat-aware) / stand | controller.js:317 | **Done** |
| Z (no Ctrl) | Lie down / stand | controller.js:319 | **Done** |
| R (not editing) | Go limp / get up | main.js:435-437 | Missing (needs ragdoll) |
| 1–9 | Emotes | main.js:442-450 | **Done** |
| Enter | Focus chat | controller.js:291 | **Done** |
| Tab | Open People Here | main.js:429 | **Partial** |
| ? and H | Help | main.js:427-428 | ? **Done**, H missing |
| B | Edit mode | main.js:430 | **Done** (build frame) |
| M | Toggle mouselook | controller.js:406-411 | **Done** |
| Esc | Ladder (row 17) | — | **Partial** |
| P | Photo mode (free camera) | main.js:431; controller.js:926-962 | Missing |
| F1 | Hide UI | main.js:432 | **Done** |
| F2 | Screenshot `eidoverse-<world>-<ISO>.png` | main.js:433; handlers.js:396-405 | Missing (F2 is the editor's unlit view) |
| F3 | Debug overlay | main.js:434 | Missing; console commands exist instead |
| V | Mic toggle / push-to-talk | mictoggle.js:369-393 | **N/A** until voice exists |
| Alt (held) | Grab cursor; drag panels from anywhere | ui.js:582-584 | Missing |
| Ctrl/Cmd+Z | Undo your own edits, even outside edit mode | build.js:525 | **Partial** (only with the build frame open) |
| Build: Q/E, `,` / `.`, R/F, Del | Turn / scale / raise / remove | build.js:560-571 | **Done** |
| Mouse drag (not mouselook) | Look: yaw 0.005 rad/px, pitch 0.004 rad/px, pitch clamped −0.9 to 1.2 | controller.js:355-360, 426-433 | Missing |
| Wheel | Zoom 0–16 m; first person below 0.4 m | controller.js:435-444 | **Done** |

**Chat and HUD specs**

**CH-A. Line model.** Change `FLine` to `{Sender, Text, Ts(ms), Seq(int64, -1 = none), Kind(chat|mention|system|whisper), Convo, bOld, Utt(-1), bSpoken, T0}`.
- Dedupe on `Seq` with a `TSet<int64>`; cap at 400 lines.
- Group: hide the name when the previous non-system line has the same author and is less than 90 s older.
- Spoken merge: if `spoken` is true, `Utt ≥ 0`, and the last line has the same author and Utt, append `" " + text` to it.
- `t0` placement: if spoken and `ts−300000 ≤ T0 ≤ ts`, insert the line before trailing lines whose `Ts > T0`.
- Show `HH:MM` before the name, with the full date as tooltip.

**CH-B. Chat on join.**
- On `snapshot`, add each item of `state.recentChat[]` (`{actor, text, ts, seq}`) in order, with dedupe.
- If `state.chatTotal > N`, prepend a dimmed line: `showing N of M messages` + (` — the last Ss of talking` only when the span from first to last `ts` is under 120 s and N > 1) + `. Scroll up for earlier.` If `chatTotal` is unknown, use `scroll up for earlier messages`.
- Track `OldestSeq` = the smallest seq ≥ 0 seen.

**CH-C. Scroll-back.**
- Trigger: chat scroll offset under 60 px from the top, no request in flight, and the "no more" flag not set.
- Send `{type:"history", reqId:"h<n>", before:OldestSeq (omit if none), limit:40, verbs:["say"]}` and show `… loading earlier messages`.
- Time out after 8 s and treat that as an empty reply.
- Prepend the entries oldest-first, dimmed and ungrouped, and keep the reader's scroll position.
- An empty reply shows `— the beginning of this world —` and sets "no more"; so does `hasMore:false`.

**CH-D. Name colours.**
- Palette: `#7cc4ff #ff9fc4 #b8e06a #ffab6b #c4a6ff #6ee0d0 #ff9a8f #9fb8ff #e9a6ff #7fe0a0 #d8c470 #62d3f0`.
- Preferred colour: FNV-1a over the lower-cased name (`h=0x811c9dc5`; for each char `h^=c; h=imul(h,0x01000193)>>>0`), index `h%12`.
- On snapshot, arrive and leave: sort the present names plus your own; each takes its preferred colour, and on a collision steps to the next free index.
- Assignments stay after a person leaves.
- Apply to senders, people rows and mentions inside text. Whisper rows keep the whisper colour; mentions of you stay amber.

**CH-E. Links and markdown.**
- Use URichTextBlock decorators.
- Links: `https?://\S+`, trimming trailing characters from `)]}>.,;:!?'"»`. Open with `LaunchURL`, http(s) only.
- Markdown: `**b**`, `` `c` ``, `*i*` (not next to a word character or `*`), `_i_` (not next to a word character).
- Escape `<` and `>` in user text first.
- Setting `Eidoverse.Chat.Markdown`, default 1.

**CH-F. Whispers.**
- Remove the local adds at Bridge.cpp:274 and UI.cpp:1262.
- On receive: `other = echo ? to : from`; label the line `"to "+to` when echo, otherwise `from`.
- When not an echo: `LastWhisperFrom = from`, increment that conversation's unread count unless its tab is active, and show a toast.
- Tabs: `all · mentions · system · @name…`. × or right-click closes a tab; one draft per tab.
- With an `@name` tab active: Enter whispers that person, the placeholder reads `whisper to X…`, and typing sends `typing{to:X}`.
- `/w X` with no message opens the tab. `/r [msg]` replies to `LastWhisperFrom`.

**CH-G. Composer.**
- ↑ recalls only when the draft is empty or recall is already active; ↓ moves forward.
- Keep 50 in memory; persist the last 20 as a JSON array in `[/Script/EidoverseClient.Chat] History`.
- Autocomplete: `^/(\w*)$` lists the listed commands with help text; `(?:^|\s)@([\w-]*)$` lists up to 8 present ids (excluding you), tagged agent/here.
- ↑/↓ move, Tab or Enter accept (inserts the value plus a space), Esc closes the list.

**CH-H. Unread.**
- When not within 48 px of the bottom, or chat is hidden: `Unread++` (not for system lines or merges); `UnreadMentions++` on a ping.
- Jump button text `"{U} new · {M} ✱ ↓"`; the rail chat icon shows a badge.
- Stop scrolling to the end unconditionally.

**CH-I. Presence.**
- `Presence = Manual ?? Auto`. Auto becomes `away` after 180 s without input (checked every 5 s) or while the window is unfocused; otherwise `present`.
- Add `"presence"` to `SendPose`. In `Pose()`, read it into `FEidoversePeer::Presence`.
- Profile menu entries: "present · here and active" / "away · idle or elsewhere" / "busy · here, not to be disturbed".

**CH-J. People pane.**
- Rows: presence dot (present = brand colour, away = dim, busy = attention) · coloured name, with "(you)" for yourself · distance as `%.0fm`.
- Header: `just you` or `N other(s) here`.
- Double-click opens a DM tab. Tab only opens the pane, never closes it. Repaint every 2 s.

**CH-K. Toasts.** `Toast(Text, Info|Warn|Err)`.
- Lifetime 5 s; 9 s for Err.
- Identical kind and text while one is visible: show ×N and restart its timer.
- At most 5 visible, dropping the oldest. Click dismisses.

**CH-L. Ambient hint.** Add `SetAmbientHint(Text)`. A timed `Hint()` shows its text for its duration, then returns to the ambient hint. Feed it from seat proximity (`"<thing> — X to sit"`).

**CH-M. Esc order.**
1. Close help.
2. Cancel the placement ghost.
3. Deselect.
4. Close the ∃ menu.
5. If any frames are open, hide them all and remember which; otherwise reopen the remembered set.

Freeing the pointer happens only on Esc while in mouselook.

**CH-N. Frames.**
- Drag from any empty panel area; Alt+drag from anywhere.
- Resize from 8 zones (4 px band, 15 px corners).
- Snap within 11 px of viewport edges and other frames.
- Minimum sizes: chat 240×100, world 200 wide, settings 210 wide, profile 320×300.

**CH-O. Rail and menu.**
- `Pins=profile,world,chat,emotes,settings,build` in the Layout section. The rail shows a window icon if the window is open or pinned.
- Each menu row gets a pin toggle.
- The build row is greyed with "needs build rights in this world" when `!CanBuild()`.
- Auto-pin build when `CanBuild()` becomes true; unpin when it becomes false.

**CH-P. Emote bar.**
- Build the list from `defs.emotes` in key order; skip entries without `clip` and entries with `listed:false` (unlisted names still work for `/emote`).
- Lead with sit, stand and lie tiles.
- Use the def `icon` glyph; tooltip `name — key N`; highlight the tile for 1.5 s after use.

**CH-Q. Door.**
- Avatar grid from the `/avatars` roster, images from `<http>/thumb/<urlencoded name>.png`, 🧍 placeholder.
- Remember the choice as `AvatarName` in ClientSettings.
- Button `enter as name · avatar`; Enter in the name field submits; name capped at 48 chars.
- Door-key password field stored in `JoinToken` (transient, never saved).

**CH-R. Missing keys.**
- **H**: help.
- **Arrow keys**: move when nothing is selected or ghosted.
- **P**: photo mode. Detach the camera; WASD moves along the view, E/Q up/down; Shift ×3.5, Alt fine; `[` / `]` change FOV between 12 and 95 at 30°/s; damped look and movement; Esc or P exits and restores FOV 55.
- **F2**: `FScreenshotRequest::RequestScreenshot("eidoverse-<world>-<ts>.png", false, false)` plus hint "saved". Outside the editor only; use F12 inside it.
- **Drag-to-look in pointer mode**: LMB when not in build mode, or RMB; constants as in the key table above.
- **Ctrl+Z**: works without the build frame open.
- **R / G / F** (ragdoll, wings, flight) belong to the body/locomotion slice.

**CH-S. E/F conflict.** Either move use and punt off E/F (click an entity in pointer mode, plus `/use` and `/punt`), or route E to "use" only when the aimed entity declares an interaction and nothing climbable is near. Either way, document it in the help table (UI.cpp:606-618).

**CH-T.** Arrive/leave and narration lines: see S3 and S5 below.

**CH-U. Style.**
- Colour tokens (panel RGB, accent, attention, text) applied through `FEidoverseTheme`, with reset to defaults.
- Chat text size 11–20 px.
- `/panels` replies as on the web.

### 3.2 World and environment panels (World frame and Settings)

| # | Web control | Verb / args, constants | Web ref | Unreal |
|---|---|---|---|---|
| 1 | Place a light | `light{id, pos:[x,y+0.9,z], color:0xffd9a0, intensity:16, range:10}`; undo is `remove` | palette.js:45-49; build.js:350-355 | **Missing** |
| 2 | Light editor | Partial `light{id, field}`; colour int; brightness 0..max(64, current); range 1..max(40, current); `keep`; "burns at noon" checked → `day:false`. Sends throttled to 350 ms with a trailing send; same-value dedupe for 5 s; rollback on error. | lights.js:107-217 | **Missing** |
| 3 | Structure editor (room, wall, door, window, floor, erase, new, undo ×40) | `comp{id,type:"structure",data}`; new: `spawn house-<x>_<z>` with `crate_large_red.glb` | structure_ui.js:13-268 | **Missing** (renders only) |
| 4 | Sky sliders: hours 0–24/0.1/12, rate 0–48/0.5/0, azimuth 0–360/5/180, sun / ambient / fill 0–2.5/.05/1, exposure 0.3–1.8/.05/1, fog 0–3/.05/1 | — | skypanel.js:23-32 | **Partial** (hour buttons only, UIWorld.cpp:114-116, 385-391); the renderer reads only hour and weather (Environment.cpp:131-150) |
| 5 | Live sky preview | — | skypanel.js:47-51 | Missing (`SetPreviewHour` exists but is never called) |
| 6 | Sky commit | `sky{…}`, always with `hours` and `azimuth` | skypanel.js:281-289 | **Done** (UIWorld.cpp:395-403) |
| 7 | Weather: clear, fair, sunshower, overcast, rain, storm, cyclone, darkstorm | preview adds `weatherSeconds:12` | skypanel.js:100-102; shared/forecast.js:20 | **Partial** (no cyclone or darkstorm) |
| 8 | Clouds: clear, cumulus, stratus, cirrus | — | skypanel.js:104-106 | **Done** |
| 9 | Sky presets from `/defs skyPresets` | Preview only | skypanel.js:78-97 | **Partial** (no refresh on `defs-updated`) |
| 10 | Clock: authored, or a real-time city from `/defs skyClocks` | `clock:"real", tz` | skypanel.js:241-279 | Missing |
| 11-15 | Ground: shape (flat 0.2 / hills 2.6 / rugged 6.0), reshuffle seed, tint (meadow / arid / tundra), plant / height (lawn .15, meadow .42, tall .7) / density (sparse .5, normal 1, lush 1.4), grow, mow | terrain and grass args in §1.3 | groundpanel.js:45-110 | Reshuffle and mow **Done**; the others **Partial** (no auto-apply or regrow; height row not hidden for non-blade plantings) |
| 16 | Seat-anchor editor | `comp{id,type:"sockets",data}` | seatedit.js:105-277 | **Missing** (sitting works: Seats.cpp:60, 73) |
| 17 | Avatar picker | Rejoin wearing the new body | palette.js:33 | **Done** (UIWorld.cpp:246-282) |
| 18 | Audio panel: mic, push-to-talk, sensitivity, hear voices, volume (voices / world / self TTS, 0–1 step .05), consent rows, TTS model | — | audiopanel.js | **Missing**: no audio at all. The TTS and STT parts are browser-only (Piper WASM, SpeechRecognition). |
| 19-23 | Video: render scale (auto / 1 / .85 / .7), shadows on/off with 1024 / 2048 / 4096, particles, avatar detail (full / half / low), clouds and grass quality | — | videopanel.js; skypanel.js:116-193 | **Partial** (quality presets only, UI.cpp:700-711) |
| 24-26 | Renderer choice (WebGPU/WebGL), MSAA, sky "world" select, landmarks overlay | — | — | **N/A** (browser backend, single value, developer overlay) |

**World panel specs**

**WP-1/2. Lights.**
- Add a library tile that holds a 0.13 m sphere ghost. Placing sends the `light` verb above and pushes `remove` onto undo.
- When the selected entity's `kind=="light"`, the inspector shows:
  - colour picker → integer `0xRRGGBB`;
  - brightness slider 0..max(64, current), step 1;
  - range slider 1..max(40, current), step 1;
  - "keep lit" → `keep:bool`;
  - "burns at noon": checked → `day:false`, unchecked → `day:true`.
- Preview on the `UPointLightComponent` at once. Send only the changed field.
- Coalesce sends: at most one per 350 ms per light, plus a trailing send with the final value. Skip a value equal to the last one sent within 5 s.
- On an `error` during editing, restore the light from the next render-scene update.

**WP-3. Structure editor.** The comp data shape is:

```
{tile:1.0, wallH:2.8, wallT:0.15, labels:{},
 levels:[{y:0, tiles:[[x,z,mat(,half)]], walls:[[axis,x,z(,mat)]], apertures:[[axis,x,z,kind]]}]}
```

- Wall axes: 0 = edge along x at row z (north side of cell x,z); 1 = edge along z at column x (west side); 2 and 3 = the cell diagonals NW-SE and NE-SW.
- Picking: intersect the camera ray with the plane `y = ent.y + level.y`, then convert to local coordinates: `lx = (dx·cos yaw − dz·sin yaw)/scale`, `lz = (dx·sin yaw + dz·cos yaw)/scale`. Cell = `floor(l/tile)`.
- Edge pick: with `u,v` the fraction inside the cell, candidate distances are axis0 at v·t or (1−v)·t; axis1 at u·t or (1−u)·t; axis2 at |u−v|·√½·t; axis3 at |u+v−1|·√½·t. Take the nearest. Door and window tools take the nearest edge that already has a wall.
- Tools:
  - **room** (drag): floor every cell in the rectangle and add the perimeter walls, skipping walls that already exist.
  - **wall**: add the picked edge; a new diagonal replaces the cell's other diagonal.
  - **door / window**: replace the aperture on that edge.
  - **floor**: set the tile.
  - **erase** (drag): remove the tiles; keep a wall only if a floored cell remains on either side; drop apertures on walls that are gone.
- Each commit sends the whole `data` with `comp`; undo stack of 40.
- "+ new" spawns at the rounded floor hit with yaw 0, then sends `emptyStructure()`.
- Show a translucent ghost of the result and a grid of the footprint ±3 cells.

**WP-4/5/7/9/10. Sky.**
- Replace the hour buttons with the 8 sliders. The draft = the folded sky plus edits; on commit include fields that differ from their defaults, always including `hours` and `azimuth`.
- Preview via `SetPreviewHour`; reset to −1 when reverting, committing or closing.
- Add cyclone and darkstorm; choosing weather adds `weatherSeconds:12`.
- Clock row:
  - Choosing a city sets `clock:"real", tz` and disables hours and rate.
  - Choosing "authored" removes both and restores hours and rate from `sky.dormantRated`.
  - If the current `tz` is not in the list, add an option "real time — <tz>".
- On `defs-updated`, rebuild the presets, clocks and ground palettes.
- The renderer also has to honour sun, ambient, fill, exposure, fog and azimuth.

**WP-11-15. Ground auto-apply.**
- Shape click sends terrain at once.
- Tint change sends terrain, then grass if grass has been grown this session (`bGrassGrown`, set when planting, cleared on mow).
- Height or density changes re-send grass when `bGrassGrown` (height only for blade plantings).
- Hide the height row unless `plantings[cur].blade`.
- Defaults: hills, meadow, meadow height, normal density, seed 7.

**WP-16. Seat anchors.**
- An inspector button "+ seat" arms placement; the click must hit the entity itself.
- `pos` = the hit in the model frame (wire metres, 3 dp). `yaw = atan2(cam.x−hit.x, cam.z−hit.z) − entity world yaw` (3 dp). Optional `part` when the hit is a named part.
- Slot name: the first free of `seat`, `seat2`, `seat3`, …
- Always send the full merged map: `comp{id,type:"sockets",data|null}`.
- Gizmos: an octahedron with a forward cone; selected is `#8fe8c8` at 1.6× scale, others `#f5c96b`.
- Picking a gizmo: ray-to-point distance under `min(0.3, 0.06 + 0.015·camera distance)`.
- With an anchor selected: Q/E turn ±15° (Shift ±0.02 rad), Del removes, a drag of more than 4 px slides it along the entity surface.
- Each gesture pushes the inverse comp onto undo.

**WP-18. Audio.** Until voice exists: "world" and "voices" volume sliders (0–1, default 1) saved in GameUserSettings, plus a disabled row "microphone (not available in this client)".

**WP-19-23. Graphics.**
- Render scale → `r.ScreenPercentage`.
- Shadows toggle; resolution → `r.Shadow.MaxResolution`.
- Grass density ×1 / .6 / .35 / 0 on the ground-cover instance count.
- Cloud quality.
- Avatar animation every 1 / 2 / 4 frames for bodies beyond 15 m.
- Persist each as a console variable, the same way as EidoverseSettings.cpp.

### 3.3 Build, place, inspect, conjure and mods tools

| # | Web feature | Web ref | Unreal |
|---|---|---|---|
| 1 | Mouse drag to move (4 px threshold), sending the live `drag` message; release sends `place` only if moved ≥ 0.005 m | build.js:290-328, 433-504 | **Missing** (click-select only, UIBuild.cpp:455-460) |
| 2 | Shift+drag moves vertically | build.js:291-302 | Missing |
| 3 | Receiving others' `drag` | build.js:331-337 | Missing |
| 4 | Library ghost placement: `spawn{id:8 chars, lib, pos, yaw, scale?}` | build.js:228-278, 347-364 | **Done** (UIBuild.cpp:257-268, 379-400; SceneBuild.cpp:42-98). Small differences in initial facing and lift. |
| 5 | Ghost keys: Q/E ±15° (Shift 0.02 rad); `,` ×0.92 / `.` ×1.087, clamped 0.1–12; R/F ±0.25 m (Shift 0.05) | build.js:563-570 | **Done** |
| 6 | Same keys on a selected object | build.js:528-571 | **Partial**: scale clamp is 0.05–50 (UIBuild.cpp:293); no ground floor for R/F (:288) |
| 7 | Undo depth 40; works outside edit mode | build.js:342-345, 411-419, 525 | **Partial** (build frame only; `build:spawn` never trims the stack) |
| 8 | Edit mode gated by role; Esc ladder | build.js:68-109, 514-521 | **Done** in substance |
| 9-10 | Lights | — | Missing (WP-1/2) |
| 11 | Inspector header "placed by X · last change by Y"; 🔒 lock; 🛡 guard; "+ seat" | build.js:140-186; placer.js:15-41 | **Missing** (only via raw comp JSON) |
| 12 | Client-side lock/guard refusals with hints | build.js:371-405 | Missing |
| 13 | Scene tree: mounted things indented, riders 🧍, badges | scenegraph.js:62-123 | **Partial**: flat list (UIWorld.cpp:321-350) |
| 14 | Numeric transform fields | scenegraph.js:137-154, 224-233 | **Missing** (read-only, UIInspect.cpp:94-97) |
| 15 | Generic component editor | scenegraph.js:163-180, 236-259 | **Done** (UIInspect.cpp:65-233) |
| 16 | Find | scenegraph.js:207-212 | Missing |
| 17 | Attach / detach; `/mount`, `/dismount` | scenegraph.js:213-217, 295-327 | Missing |
| 18 | Remove from the scene panel | scenegraph.js:262-265 | **Done** |
| 19 | 📜 Scripts roster and console via `debug` | scenegraph.js:337-397 | Missing |
| 20 | `/punt` with no name | physobj.js:236-287 | **Partial** |
| 21 | Seat anchors | seatedit.js | Missing (WP-16) |
| 22 | Sound editor; `POST /upload?as=audio` | sounds.js:160-240; shared/sound.js | Missing |
| 23 | Picture editor; `POST /upload?as=image` | pictures.js:236-329; shared/picture.js | Missing |
| 24 | Drop a .glb or .vrm onto the window → `/upload` → `asset{name,path}` | palette.js:142-168 | Missing (drag-and-drop is browser-specific; use a file picker natively) |
| 25 | A live `asset` entry adds to the palette with toast "X joined the world's palette" | palette.js:100-103 | Missing |
| 26 | Library search `GET /library-models?q=` (160 ms debounce; 8 starter models when empty) | palette.js:16-97 | **Done** (UIBuild.cpp:36-46, 145-189; 300 ms) |
| 27 | ✨ Conjure (orrery image→mesh pipeline) | conjure.js | Missing; needs native orrery auth. Low priority. |
| 28 | 🧩 Mods (local JS, `behavior runtime:"client"`) | mods.js | **N/A**: runs arbitrary browser JavaScript. The "object physics" toggle already exists as `bSimulateObjectPhysics`. |

**Build tool specs**

**BT-A. Drag to move.**
- On LMB press over an entity in the build frame (pointer mode, no ghost): select it and record `Before={pos,yaw,scale}`, `StartMouse`, `bVertical=Shift`, `StartPos`, `Depth = distance(camera, entity)`, and `Grab = entityPos − aimPoint` (aim ignoring this entity).
- The drag starts after the cursor moves more than 4 px. If the entity is locked, or guarded by someone else, show the hint from BT-E and stop.
- Each tick:
  - Horizontal: `pos = aim + Grab`. If the aim landed on another entity's upward face (normal.z > 0.6), take its y.
  - Vertical: `y = max(terrainHeight, StartPos.y + (StartMouseY − MouseY)·2·Depth·tan(FOV/2)/viewportHeight)`.
- Move the actor locally (hold its transform) and send `{type:"drag", id, pos, yaw}` at most every 66 ms.
- On release: if any axis moved ≥ 0.005 m, send `place{id,pos,yaw,scale}` and push undo `place{id, ...Before}`.

**BT-B. Receiving drag.** For `drag` from someone else, set the target transform (yaw about wire +Y) and hold it for 0.5 s, the same way remote leases are held. The next render-scene upsert or `place` supersedes it.

**BT-C. Key parity.**
- Selected-object scale clamp 0.1–12.
- R/F: `y = max(terrain(x,z), y+dy)`.
- Ctrl/Cmd+Z works whenever the HUD has game focus.
- Trim undo to 40 on every push.
- Hints "undid move/spawn/removal/light"; undo also clears the selection.

**BT-E. Lock and guard.**
- `mayAuthor` = you are owner, OR (the placer has a `sub` and it equals your `yourSub`), OR (the placer has no `sub` and its id equals yours).
- Lock checkbox → `comp{id,type:"lock",data:true|null}`. Disabled when someone else guards the entity.
- Guard checkbox → `comp{id,type:"guard",data:true|null}`. Enabled only when `mayAuthor`.
- Re-inspect 400 ms after toggling.
- Hints before acting:
  - guarded and not `mayAuthor`: `🛡 guarded by X — only they or the world's owner can move or change it`;
  - locked: `🔒 locked — uncheck lock in the inspector to move or remove it`.
- Unreal needs `placer{id,sub?}` and `actor` per entity, and `yourSub` from the snapshot. If render-scene does not expose them, extend the `inspect` reply.

**BT-F. Scene tree, transforms, find, attach.**
- Indent 14 px per mount level. List riders as `🧍 rider (slot)`.
- Badges: every comp type; `motion` and `motion:*` show `type(<data.type>)`; `sockets` and `reactions` show `type(<keys>)`; bound scripts show `📜<id>`.
- Clicking an entity in the world opens the scene tab with its row selected.
- Transform fields: pos (2 dp, step 0.1), yaw in degrees (step 5), scale (2 dp, step 0.05); lights show pos only; parent-local when mounted, labelled "local pos". Commit with `place`.
- Find: hint `id is N m away at (x, z)` for 5 s.
- Attach to…:
  - Arms a pick ("now click the row of its new parent").
  - Refuse cycles with `that would mount a thing onto its own cargo`.
  - `offset` = the child's position in the parent's local frame (3 dp); `yaw = worldYaw(child) − worldYaw(parent)` (3 dp).
  - Send `mount{id, to, offset, yaw}` and log `child now rides parent — it moves when parent moves`.
- Detach: `dismount{id, pos: world position, yaw: world yaw}`, 3 dp.
- `/mount A B slot` → `mount{id,to,slot}`.

**BT-G. Scripts.**
- Roster: `{type:"debug", reqId, behaviors:true}`; reply `events[kind=="behavior"]` with `{id, status, attach?, timers?}`.
- Rows: `▶ id on attach` when running, otherwise `⏸ … paused`; plus `N⏲` for timers.
- Console for the selected script: `{type:"debug", reqId, behavior:id, limit:40}`; show `HH:MM:SS line` and `status`. Poll every 2.5 s while open.
- Unbind sends `behavior{id, remove:true}`.
- Route `debug` replies by `reqId`, with an 8 s timeout.

**BT-H. `/punt`.**
- With no name: the nearest solid, non-interior entity whose bounds centre is within 2.5 m horizontally.
- Power default 4, clamped 1–10.
- Refusals:
  - more than 3.3 m away → `id is too far away to kick`;
  - has a motion, or is a light → `id is animated by its motion — it can't be kicked loose (yet)`;
  - nothing found → `nothing at your feet to kick`.
- A person's name sends the shove (see `/push`).

**BT-J. Sound and picture.**
- Upload: `POST <http>/upload?as=audio|image&name=<file>[&token=]` with the raw body (plus the session cookie on production). The reply `{path}` must start with `store/audio/` or `store/images/`.
- Accepted extensions: audio .mp3, .ogg, .opus, .wav, .webm, .m4a; images .png, .jpg, .jpeg, .webp.
- Sound:
  - play → `comp sound {src, volume 0–1 (default .8), radius 1–200 (default 12), loop (default true), playing:true, t0: now ms, look? (≤200)}`;
  - pause → `playing:false`, no `t0`;
  - silence → `null`.
- Picture (only for models with named parts): `comp picture {src, part, lit:"scene"|"self", flip, look?}`; take down → `null`.

**BT-K. Model and avatar upload.**
- A file-picker button (.glb / .vrm) → `POST /upload` (for .vrm add `as=avatar&name=`).
- .glb: send `asset{name: basename without .glb, path}`, then hold a placement ghost.
- .vrm: toast that the avatar is now on the roster.
- On a live `asset` log entry: update the library names and toast `name joined the world's palette`.

### 3.4 Infrastructure, consent, capture, debug and boot

These rows are not covered by the protocol table above.

| Web feature | Ref | Unreal |
|---|---|---|
| Loading curtain. Phases and weights: engine 14, connect 6, world 40, body 40. "Go in anyway" after 4 s; forced finish at 45 s; tips rotate every 5.2 s. | boot.js:35-40, 133-201; main.js:497-512 | **Partial** (status text only) |
| Empty-world hint (`press B to build in it`, 14 s toast) | main.js:486-496 | Missing |
| Consent: posable (default off), pushable (default on) | consent.js:15-26 | Missing |
| Spectator follow camera (`?follow=`) | main.js:536 | Missing |
| Sign-in | net.js:311-358 | **Done** natively (device pairing, Keychain storage) |
| Snap framings | net_snap.js:15-54 | **Done** (consent tied to endpoint and world) |
| Shader warm-up, load scheduling, settings persistence | warmqueue.js, loadwork.js, assets.js | **Done** natively |
| Debug panel (F3) | debug.js | **Partial**: console commands `Eidoverse.LoadStats` etc. |
| Capability notices, early socket, lite client, client-side fold, breadcrumbs, `EW.*` devtools | capnotice.js, index.html:1518-1556, lite.js, state.js, debuglog.js | **N/A**: browser-specific (renderer-backend notices, parse-time optimisation, weak-device fallback, browser fold by design, console tooling) |

### 3.5 Infrastructure specs (S-series)

**S1. Errors.**
- On every `error`: `Text = error.Left(300)`; `HUD->Toast(Text, Warn)`; add a system line; set `InteractionStatus = Text`; broadcast `OnVerbRefused(Text)` so previews (lights, ghost, undo) can roll back.
- If the text matches "no voice relay" (case-insensitive), set `bVoiceRelayAbsent`.
- Error frames never carry credentials, but keep secrets out of `UE_LOG`.

**S3. Arrive/leave.** `arrive` (not the snapshot roster) → `* {id} arrived`; `leave` → `* {id} left`, and remove the person from the typing list.

**S4. Close codes.** Also show the last `error` text received within 2 s before the close.
- 4002: `"{name}" is connected elsewhere. This session yielded — reconnect to take over.` Toast for 60 s, no retry.
- 4006: `you were removed from this world — reconnect if you believe you may return` plus the stored reason. No retry.
- 4005: `"{World}" is not a valid world name`. Show the door.
- 4003: if signed in to production, `LogoutProduction()` and show the door with "Your sign-in expired — sign in again"; otherwise show the door with "that door key was refused" and a key field.
- Other codes: backoff 1.2 s ×1.6, capped at 15 s.

**S5. Narration** (in the `log` handler):
- `grant`: `* {actor=="world" ? "the world" : actor} made {id} {role} {+gen|-gen}` (only the parts present).
- `ban`: `* {actor} banned {id}{ — reason}`.
- `unban`: `* {actor} lifted the ban on {id}`.
- `kick`: `* {actor} removed {id}{ — reason}`.

**S6. Spoken say.** Same as CH-A, plus a bubble lasting `min(12, 3 + len·0.03)` s.

**S7. World messages.**
- `world-reset{world, by}`: toast `{by} reset "{world}" to zero — reloading…` for 4 s; system line `{by} reset this world to zero`; clear undo, selection and ghost; after 1.8 s run `Disconnect(); Connect();`.
- `world-forked{from, to}`: system line `world copied — "{from}" → "{to}" — visit: {HttpBase}/?world={to}` and a 12 s toast; optionally a button that calls `SwitchWorld(to)`.
- `mod{text}`: one system line per `\n`-separated part.

**S8. Restore pose.**
- Applies on the first snapshot of a connection, when the explorer has not moved and `restore` is an object.
- `p`: non-finite values become 0. Explorer location = `Origin + FromWirePosition(p) + (0,0,88)`.
- Rotation from `q` if present, else from `yaw`; put the control rotation behind the body.
- `clip` sit or sitchair → `Posture="sit"`; `lie` → `"lie"`; `ragdoll` → set y to 0 and stand.
- When restored, skip `EnvironmentSpawn` (Worlds.cpp:137-139) but still enable walking.

**S9. Name notice.** If `you != ParticipantName`: adopt it, save settings, toast `you're appearing as "X"` for 8 s.

**S10. Verb queue.**
- When `bWanted && !bJoined && !bSpectator`: queue verbs, says and interaction verbs (cap 64) with the system line `queued {verb} — reconnecting…`, and return true.
- Flush in order after the next snapshot.
- `Disconnect()` clears the queue; `Lost()` keeps it.

**S11. Puppet.**
- Add console variables `Eidoverse.Consent.Posable` (default 0) and `Eidoverse.Consent.Pushable` (default 1), with Settings toggles; `/pushable` sets the latter.
- `ragdoll` present:
  - not pushable → toast `{by} tried to knock you over — /pushable on to allow` for 6 s;
  - pushable → knockdown (a fall clip or lie posture) along `FromWirePosition(lean)`.
- Else, if not posable → toast `{by} tried to pose you — enable it in settings to allow` for 6 s.
- Else `pose`:
  - `merge`: per-bone merge into the held pose (a `null` bone removes it);
  - otherwise replace (an empty object releases).
  - Send the held pose as `pose.pose` on every packet (`null` to clear), with hints `{by} posed you` / `{by} released your pose`.
- `anim`: play it on yourself, then re-send `{type:"anim", dur, tracks, loop, replace}`. `replace !== false` clears the held pose.

**S12. Force.**
- On a live `force{at, radius, power}` log entry, if pushable:
  - `d` = horizontal distance from you, in wire metres.
  - If `d < radius`: `mag = power·(1 − d/radius)`.
  - If `mag ≥ FORCE_MIN` (constant from shared/force.js), knock down along `(you − at)` in XZ; at ground zero, along your facing direction.

**S13. Bodydrag.**
- Minimum: on `grab`, reply `{type:"bodydrag", target:by, end:true}`; if not pushable, also toast `{by} tried to drag you — /pushable on to allow`.
- Full support needs a ragdoll and a "downed" state: accept only when downed and unclaimed (first grab wins); reply `{pins}`; apply pose samples; on `end`, apply `pinAt`.

**S14.** Drag: see BT-A / BT-B.

**S15. Caption.** `caption{id, text, utt}` → set that peer's bubble text (the same `utt` replaces it) and set `SpeakingUntil = now + min(12, 3 + len·0.03)` s. Do not add to chat; the final `say` does that.

**S16. Lease feedback.**
- Show `InteractionStatus` changes as 2 s hints.
- Own pending punt `denied` → system line `can't kick {id} — {why}`.
- `lost{to}` → hint `{to} took {id}`.

**S17. Loading curtain.**
- Full-screen overlay after joining: world name, your name, a progress bar, phase label and tips rotating every 5.2 s.
- Phases and weights: connect 6 (snapshot received); world 40 (`1 − pendingModels/initialPending`, minimum 0.15); body 40 (your avatar Ready or Failed); engine 14 (always done).
- Done when the snapshot is in, the first render-scene has been applied, your avatar has settled and nearby models have loaded. Wait two frames, then fade.
- "Go in anyway" after 4 s; forced finish at 45 s.

**S18. Empty world.** After the first render-scene, if `EntityCount == 0`:
- system lines `"{world}" is empty — nothing has ever been built here.` and `press B to start building, or ? for the controls.`;
- toast for 14 s: `"{world}" is an empty world — press B to build in it`.

**S19. Rights line.** On the snapshot and on `your-rights`: if `!open && role != "owner"`, add `this world has an owner — you are a {role}{ +gen} here`.

**S20. defs-updated.** `HUD->ReloadDefs()` (re-fetch `/defs`; rebuild emotes, sky presets and ground palettes) and clear ClipLibrary's emote-file map.

**S21. avatar-updated.** Add the line `avatar "{name}" updated (v{v})`.

**S23. geom.** Keep a `TMap<lib, FBox>` built from `FromWirePosition(min/max)`, with min and max re-sorted because of the axis flip. Size placeholder cubes to box × entity scale.

**S26. Follow camera** (spectators). `-EidoverseFollow=<id>` or a UI picker: eye = peer + up·2.1 m − forward·3.4 m, looking at peer + up·1.2 m.

---

## 4. Browser-only / inapplicable (summary)

- **WebXR**: xr*.js, VR panels and pointer, the `pose.xr` send side, the VR chat quad with canned replies.
- **Browser renderer selection**: WebGPU/WebGL choice, MSAA reload, capability notices.
- **Page and DOM mechanics**: the early socket in index.html, the lite client, `<select>` skinning, the emoji tofu test, pointer lock, layout-version localStorage purge, touch thumbstick UI, drag-and-drop file ingestion (use a file picker natively).
- **Mods**: arbitrary browser JavaScript via a blob `import()`.
- **Browser speech**: `/audio` diagnostics, SpeechRecognition STT, Piper WASM TTS.
- **Developer tooling**: `bc` breadcrumbs, `__why` / `EW.*` console globals, the landmarks overlay.
- **Not needed by design**: the client-side fold and sim fold, because Unreal uses `render-scene`.
- **Server-only or legacy messages**: `voice-moderate` / `voice-moderated`, `caption` / `attest` (sent only by the voice leg), legacy `pose`, server→client `sfu-ice`.