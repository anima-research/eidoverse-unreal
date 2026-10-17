# Eidoverse underwater bridge

First native integration: presence and live text chat, with local Unreal physics
and scenery. It is **not** a full Eidoverse world/fold client.

## Local demo

The spectral prototype contains an `Eidoverse | Presence and chat (opt-in)` actor.
Auto-connect is off; merely opening the project or pressing Play does not connect.

## In-game chat and production login

Press **Enter** (or click Chat / Login) to open the native client panel; **Esc**
closes it and returns mouse/movement control to the diver or submarine. Public
and private chat have distinct labels. Leave the recipient blank for public
chat, or enter a participant's exact name for a whisper. Public messages appear
when echoed by the server; private sends are labeled locally but have no delivery
acknowledgment. The panel retains up to 150 messages in memory, does not write a
local transcript, and clears history when starting a new connection. Spectators
can read but cannot send. The existing configured-server button remains available
for local development.

F2 is deliberately not used: Unreal binds it to Unlit view mode in development
players. Public sends now show confirmation only after a matching server echo.
If confirmation is absent for ten seconds (or the connection drops), delivery is
marked unknown and the message is restored when the draft is empty; retry is
never automatic. Check history before retrying to avoid duplicates.

Delivery/rendering regression pass (2026-09-10): the production log confirmed
the user's message was stored; the old UI did not clear its waiting label. The
new `test_chat_delivery.py` passed confirmed echo, ten-second missing-echo timeout
with draft recovery, private chat, input restoration, and live ocean seam samples.
All six `Underwater.` automation tests and Mac Editor/Development/Shipping builds
passed. `Underwater.Ocean.SamplingBoundaries` covers tiny negative coordinates
that previously rounded to the field size and caused an out-of-bounds crash.

The public server is `wss://eidoverse.animalabs.ai/ws`, with every world on it (commons, water and the rest).
Logging in works as the browser's does, with one pairing step a native client needs: choose **Log in**
(the ∃ menu, the door, or the World panel). Unreal shows a five-minute pairing code and opens the
approval page in your normal browser; log in with Discord there if asked, check that the code matches your
own Unreal client, and choose **Approve**. Unreal is then signed in as you, as the browser is, for any world
your account may join. At the door, pressing join on the public server while logged out starts this sign-in
and enters the chosen world once you approve. Joining shares live presence and may replace another embodied
session for the same identity in that world. Public chat is persisted by the server.

Travel: the World panel's Here page lists the server's worlds (`GET /worlds`) with who is in each; a click
travels there (leave this world, join that one, as the same person). Type any name to go to a world not on
the list (an empty one is yours to start), or use `/go <world>` or the lantern's "travel to another world".
The door shows the same worlds as chips under its World field.

The session cookie is kept only in native memory (and the Keychain, to survive restarts), sent only to the
exact production endpoint, and is not placed in URLs or project assets. HTTP cookie persistence is disabled
for these requests. Log out disconnects, clears the credential, and requests session revocation; ending Play
does the same. Revocation is best-effort if offline. The native session lasts at most 12 hours, bounded by
the approving browser session's remaining lifetime; logging out of Unreal does not log you out of the website.
Pending sign-in can be cancelled; **Reopen browser** restores the approval page if needed.

Until 2026-10-17 native sessions were limited to the water world unless "all accessible worlds" was approved;
the server change that makes them ordinary sign-ins is eidoverse-worlds `login/native-normal-flow`.

This requires the accompanying local Eidoverse server patch (`server/native-login.ts`,
routes/auth integration and the native-world join guard). **Deployed to production
on 2026-09-10 as targeted commit `097ab71`.** HTTPS pairing smoke checks passed;
the user still needs to complete the real Discord sign-in and approval.
There is no manual-token fallback; do not extract browser cookies or give credentials
to an assistant. No identity-server or Discord application change is needed.

For local UI testing, run `bun Tools/eidoverse/chat_fixture_server.ts` and execute
`test_chat.py` outside PIE. The fixture uses only `127.0.0.1:18995`, sends a public
message, a duplicate, and a private message, and echoes public/private test sends.
No production connection is made by this test. `Underwater.Eidoverse.LoginSafety`
covers session-cookie parsing and rejection of malformed/injected values.
In development builds, `Eidoverse.TestChatUI` and, after a short wait,
`Eidoverse.TestChatUI private` exercise real Slate character/Enter events against
that exact loopback fixture only. They cannot send to production. The fixture's
`/report` reports send counts without recording message contents.

## Multiple worlds and server-authored scenes

The local client now supports typed world selection and a server-world land
renderer. See [PARITY.md](PARITY.md) for usage, architecture, tests, limits and
the explicit remaining Three.js parity gaps. This next server update is **not
deployed to production**; existing production presence/login/screenshot support
remains as described below.

## Remote screenshots for headless clients

The Enter → Chat/Login panel now has **Share scene screenshots with headless
clients** (off by default, session-only and scoped to this server/world). With
the matching server patch, agents can call `snapshot` with first, third or selfie
views, optionally `renderer:"unreal"`. You stay embodied while donating renders.
Only the 3-D scene is captured, never desktop, HUD, chat or sign-in UI. The
existing public screenshot API can request views from any present participant;
do not enable sharing if you do not want to serve those scene views.

Capture is 960×540, at most once per two seconds. It uses a separate camera and
temporarily scopes the ocean's camera-dependent grid/reflection/medium settings,
restoring them before returning. Readback can briefly hitch; no continuous capture
runs when idle. Camera cuts reset exposure adaptation; single-frame rendering
and cheaper GI/reflection settings can differ from the player's temporally
accumulated viewport. Local prototype scenery is not the Eidoverse
persistent building fold; the response is labeled accordingly.

Local native test: run `bun tools/snapshots-wire-test.ts --native` in the Eidoverse
repo, then run `test_remote_capture.py` from the Unreal editor outside PIE, then
`bun Tools/eidoverse/capture_remote_views.ts` externally. Test images go to a fresh
temporary directory. This never connects to production.

Native validation (2026-09-10): first/third/selfie, surface and pitched-underwater
PNG capture passed through the real scratch sequencer. The camera/ocean-state
monitor passed; visible chat was excluded; opting out immediately stopped
requests. Persistent view-state is deliberately enabled for the capture so
Unreal includes the water post-process material (continuous capture stays off).
Run `watch_capture_state.py` before external capture cases and
`finish_capture_test.py` afterward to revoke sharing and restore editor throttling.
Production screenshot support deployed with operator approval on 2026-09-10
(03:36 UTC September 11), commit `2bd5e24`, preserving native login. Sequencer
and MCPL services are healthy after restart; the production Bun broker tests
and both service build checks passed. Open the client, join production `water`,
then enable sharing to make it available to headless clients. A live production
native screenshot has not yet been exercised; the native visual tests above
used an isolated sequencer. Screenshot access uses the public API described above.

## Console-based local demo

1. In a terminal with Bun on PATH, run:

   ```sh
   sh Tools/eidoverse/start_local_server.sh
   ```

   This starts the reference server on **127.0.0.1:8993**, using a fresh scratch
   world directory printed at startup. No existing world data is modified.
   Override `EIDOVERSE_SOURCE` if the server checkout is elsewhere. Install its
   dependencies with `bun install --frozen-lockfile` first if necessary.

2. Open `/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype` and Play.
   In the game console:

   ```text
   Eidoverse.Connect
   Eidoverse.Say Hello from underwater
   Eidoverse.Whisper other-participant Hello
   Eidoverse.Disconnect
   ```

3. For a moving remote test peer, run `bun Tools/eidoverse/probe.ts` from this
   project directory. It joins only the local scratch world and exits after five
   minutes. It writes a wire report under `/Users/olena/unrealtest/eidoverse/`.

Stop the terminal server with Ctrl-C when finished. Its temporary world data is
retained for inspection. `install_bridge.py` can reinstall the opt-in actor into
the prototype map outside PIE if needed.

## Connection and privacy

`Eidoverse.Connect endpoint world participant` overrides the actor's connection
settings for the current play session. Remote endpoints must use `wss://`; clear
text is accepted only for localhost/127.0.0.1 with an explicit port. URL query
parameters and embedded credentials are refused.

For an authorized private world, set `EIDOVERSE_JOIN_TOKEN` and, if required,
`EIDOVERSE_IDENTITY_TOKEN` in the environment **before launching Unreal**. Tokens
are transient, not saved in the map. Do not put them in console commands. Joining
shares your participant name, avatar reference and live pose. Public chat is
persisted by the server; movement recording depends on server configuration.
The local launcher disables frame recording. No production world was joined or
changed during development, and the server changes have not been deployed.

## Implemented

- Existing join/snapshot/arrive/leave/frame messages; live `say` and `whisper`.
- Controlled-pawn poses capped at 15 Hz; interpolated, non-colliding remote
  proxies; connection backoff and cleanup on departure, disconnect and EndPlay.
- Coordinate conversion: UE centimeters `(X,Y,Z)` to wire meters `(-Y,Z,X)/100`.
  The reflected axis is required for Three.js/Unreal handedness; quaternion
  conversion uses the corresponding axial-vector mapping, not a plain swap.
  `OriginCentimeters` defines the shared origin; negative wire Y is underwater.
- Optional quaternion `q:[x,y,z,w]` for pitch/roll and a `locomotion` object with
  `mode`, `body`, `medium`. Legacy position/yaw/clip remain present.
- Diver/submarine identification and swim/pilot/surface/platform walk/climb
  metadata. These describe the existing controller; they do not add locomotion.
- Runtime remote avatars from snapshot/arrive references, plus versioned
  `avatar-updated` hot reload. No editor import is needed. Shared meshes/materials
  are cached in memory for the connection; every participant has a separate pose.
- VRM 0.x/1.0 humanoid bone mapping, skeletal meshes, rigid bone attachments,
  and procedural idle/swim/walk/climb poses. Submarine presence temporarily
  hides the avatar and displays the prototype hull; exiting restores the avatar.

### Swimming and diving

Remote humanoid avatars now blend between upright hovering with hand sculling,
forward swimming with reach/pull arm strokes and alternating flutter kicks,
streamlined descending/ascending, and upright surface swimming. Body trim follows
observed 3D motion in the participant's local frame; neck/head counter-rotation
keeps the gaze forward. Swimming rolls gently with the stroke and lateral travel.
The skeleton rotates around its hips without changing the network position.

Speed and mode changes preserve stroke phase and smoothly blend trim and joints.
Stale presence falls back to hovering; large teleports and long packet gaps are
excluded from velocity estimation. Existing `swim`/`surface` metadata and 3D
positions suffice: no server update or authored animation upload is required.
These are runtime procedural gaits, not motion-capture clips or first-person
diver hands. Unmapped bones remain in the avatar's reference pose.

To exercise the state transitions, run `test_swimming.py` after the local
`test_avatars.py` setup has succeeded. The fixture also accepts POST controls at
`/step/hover`, `/step/swim`, `/step/dive`, `/step/ascend`, `/step/surface`, and
`/step/walk`. `Underwater.Eidoverse.Swimming` covers the motion-target rules.

## Dynamic avatars

`Load Remote Avatars` is enabled by default on the bridge. `Avatar Library Base`
defaults to `https://eidoverse.animalabs.ai/library/`. Connecting to a local world
can therefore still download its referenced avatar files from production; this
does not join a production world. Disable the option before Play for offline or
placeholder-only sessions. Authentication tokens are never sent with asset GETs.

Only library-relative `.vrm`/`.glb` names with optional numeric `?v=` are accepted.
Absolute URLs, traversal, extra query parameters, archives and external glTF
buffers/textures are rejected. Avatar downloads use a bounded 24 MiB receive stream
(world props remain 16 MiB). Avatars accept up to 48 MiPixels of source textures
for bodies such as Mythos's three-4K-map winged VRM, with GPU maps still capped
to 2048px and geometry/image-count limits unchanged. Downloads retain a
30-second timeout, GLB preflight and geometry/texture limits. Decoding uses
glTFRuntime workers; mesh finalization and rigid attachments still have game-thread
cost. Loads are serialized, textures are capped at 2048 on import, and at most 16
unique asset versions are cached. Unsupported or failed avatars retain a proxy
(or the previous mesh during a failed hot reload). The library is a trusted asset
source, not a sandbox for arbitrary hostile model files.

No avatar files or converted avatar assets are saved to the project. The loader
dependency and its license are documented in [THIRD_PARTY.md](THIRD_PARTY.md).

### Runtime lit materials

Incoming unlit and MToon materials are adapted to Unreal Default Lit materials
at load time, including subsequent avatar swaps and version updates. This is
per-material conversion, not an avatar-name-specific replacement. Embedded
textures, base color/alpha, normal maps, UV parameters, authored emission and
glTF PBR factors are retained by the loader. Unlit/toon materials that omit
metallic/roughness factors receive matte defaults (metallic 0, roughness 0.8).
Explicit authored factors take precedence. Full MToon shade/rim/outline behavior
is not reproduced; its glTF-compatible fallback appearance is used.

Opaque, masked and translucent variants preserve alpha mode and double-sidedness.
Transparent fallback materials use surface-forward lighting so they can respond
to local lights without enabling the ocean's disabled translucency lighting
volumes. Existing opaque PBR materials retain the loader's normal material path.
Emissive content remains emissive by design.

The reusable templates in `/Game/Underwater/Avatars/Materials` are cooked
with the project. `build_lit_materials.py` regenerates them outside PIE from the
vendored glTFRuntime material assets. The downloaded avatar bytes are never
rewritten, and runtime textures/material instances are not saved as avatar assets.
`Underwater.Eidoverse.LitMaterials` verifies the six adapted variants, injected
texture and material parameters, PBR preservation, and unchanged input JSON.
Run `test_lit_avatars.py` after the fixture has loaded to inspect the skeletal
and rigid-part material slots; it can be repeated after avatar/version updates.

For development, run `bun Tools/eidoverse/avatar_fixture_server.ts`, then execute
`test_avatars.py` through Unreal's Python console outside PIE. This protocol
fixture binds only `127.0.0.1:18994`; it exercises two peers sharing a production
VRM plus a missing-avatar fallback. Its local `POST /step/swap`, `/step/update`,
`/step/sub`, `/step/diver` and `/step/leave` endpoints drive lifecycle checks.

The server already relays 3D positions. The accompanying Eidoverse checkout adds
validation for optional orientation/locomotion fields, a loopback HOST override,
protocol notes and regression tests. There are no new log verbs or fold changes.

## Deliberate limits

- This is not full VRM conformance: MToon-specific shading, spring bones, facial
  expressions, morph animation, authored animation clips, tracked hands and remote
  per-bone pose/reach/IK are not implemented. glTF-compatible materials are used.
  Non-humanoid skinned GLBs can load but lack humanoid procedural animation.
- Buildings, terrain, entities, persistent-world fold/replay, agent `swim_to`,
  voice and puppeting are not implemented. Server restore poses do not teleport
  the local pawn. Shared environmental alignment is configured manually.
- Existing browser clients can consume legacy fields, but need renderer changes
  to display full quaternion orientation, swimming animations and submarine bodies.
- Dry habitat interiors are not yet classified separately from swimming; surface
  detection uses `SurfaceZ` and a fixed threshold, not instantaneous wave height.
  Platform climbing currently reports air even during the submerged ladder segment.
- The unchanged production Claude VRM from the first integration remains at
  `/Users/olena/unrealtest/eidoverse/claude.vrm`, outside project content. Its embedded
  metadata restricts modification/redistribution; runtime display does not create
  a converted or bundled copy. Select appropriately licensed content before shipping
  avatar assets. Avatar-profile/seat metadata is not consumed by this renderer.

## Verification

- Plugin built successfully for Mac arm64/x64 Editor, Development and Shipping.
- `bun tools/underwater-presence-test.ts` and `bun tools/underwater-wire-test.ts`
  pass in the Eidoverse repo (validation, 3D/orientation relay, late join, invalid
  sample rejection, spectator, chat, leave, no movement log writes).
- Existing fold regression suite: 24 passed, 0 failed.
- `test_live.py` in Unreal PIE passed a real native join, remote presence, outgoing
  underwater pose and two-way public chat against the isolated sequencer/probe.
  The captured wire position was `[-21.5,-7,79]` for UE `(7900,-2150,-700)`.
- Remote departure and explicit native disconnect left zero remote actors.
- Native coordinate unit test passed: `Automation RunTests Underwater.Eidoverse`.
- Dynamic avatar pass (2026-09-10): Mac arm64/x64 Editor/Development/Shipping
  builds passed for both plugins. `Underwater.Eidoverse.AvatarSafety` passed.
  In PIE, production Claude (53 bones, five rigid parts) and Claude suit (111
  bones, two rigid parts) loaded from the HTTPS library. Two peers shared one
  mesh; live replacement, versioned hot reload, procedural bone motion,
  submarine hiding/diver restoration, missing-file fallback, departure and
  disconnect cleanup were checked. Only VRM 1.0 production assets were visually
  tested in this pass; the VRM 0.x mapping path still needs representative QA.
- Swimming pass (2026-09-10): Mac arm64/x64 Editor/Development/Shipping build
  passed; all three `Underwater.Eidoverse` automation tests passed. Live state
  transitions passed with body lean approximately 8° hovering, 77° swimming,
  128° diving, 28° ascending, 35° at the surface, and 0° walking. The live check
  also rejects abrupt trim jumps. Procedural poses were inspected underwater.
- Lit material pass (2026-09-10): Mac arm64/x64 Editor/Development/Shipping
  builds passed; all four `Underwater.Eidoverse` automation tests passed.
  The material test decodes an embedded PNG into ten runtime material cases,
  checking lit shading, opaque/masked/translucent modes, two-sidedness,
  forward-lit transparency, original color/alpha/emission, matte defaults for
  unlit inputs, and unchanged authored/default PBR factors.
  Live Claude and Claude suit downloads adapted 9 and 11 materials respectively;
  skeletal and rigid-part slots passed inspection after replacement and versioned
  reload. A scene-light on/off comparison confirmed lighting response underwater.
  Editor UI throttling can stall PIE while interacting with the console; for
  automated visual QA, temporarily use `Slate.bAllowThrottling 0` and restore it
  to `1` afterward. The production server was not changed or joined.

No performance benchmark or production connectivity claim is implied by these tests.

Chat/login UI pass (2026-09-10): Mac arm64/x64 Editor, Development and Shipping
builds passed, as did all five `Underwater.Eidoverse` automation tests. The local
chat fixture verified public/private reception, duplicate sequence suppression,
blank rejection, native keyboard typing and Enter submission for both channels,
compose clearing, private labeling, and Escape restoring movement/look input.
The panel was visually checked in PIE. This pass originally used manual-token
login, superseded by browser-approved Discord pairing. Successful production
Discord pairing and joining `water` require deployment of the local server patch;
no production world was joined, and no production server was modified.

Discord pairing pass (2026-09-10): rebuilt and installed Mac arm64/x64 Editor,
Development and Shipping; all five Unreal automation tests passed. The isolated
server suite passed 63 handler checks plus a real scratch HTTP/WS authenticated
water join. Native PIE and Chrome tested matching-code approval, polling into a
session, cancellation, denial, logout, and no automatic world switch. Reopening
the browser approval page refreshes the code even on same-tab fragment navigation.
Both native public/private keyboard chat probes still passed. Fixtures used fake
identities on loopback only; real Discord OAuth and production remain untested.
