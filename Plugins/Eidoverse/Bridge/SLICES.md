# Who owns what: the client's port to the current protocol, through the bridge

ADAPTER-DESIGN §6 splits the port into slices. Slice 0 (this commit series) built the foundation. Slices A–D can now run in
parallel, in separate worktrees. Each slice owns the functions and files listed for it below, so no two slices' hunks overlap.

The bridge's own half of every slice (`src/scene.ts`, `chat.ts`, `writes.ts`, `bodies.ts`, `access.ts`) is already built. Its
suites are green at the `engine.pin` sha: T3–T10, run with `bun run test`. What is left in each slice is the C++ half, plus
the C++ game tests (T11).

## Slice 0: foundation (done)

| where | what |
|---|---|
| `Private/EidoverseSidecar.{h,cpp}` | Launches the bridge: the packaged `Binaries/<Platform>/Bridge/eido-bridge[.exe]`, or `bun Bridge/src/main.ts` in a development tree. Writes the token on stdin, reads `{"port"}`, restarts the bridge if it dies, and stops it on EndPlay. **It is the only C++ that knows the handshake (G3).** |
| `EidoverseBridge.cpp` core | `Connect` → `BindSocket`, `SendBridgeHello`, `SendJoin` (unchanged: the bridge adds `replica`/`clock`), the `Receive` dispatch, `OnBridgeMessage`, `Request` / `BridgeDo` / `OnReply` / `ShowRefusal`. The `bUseBridge` switch: `-EidoverseBridge`, or `EIDOVERSE_BRIDGE=1\|dev` (`dev` tries bun on the sources first). |
| `EidoverseProtocol.cpp` | `CloseReason`: 4005 now gives the server's words (a refused join, no longer "not a valid world name"); 4011 retries. |
| `EidoverseWorlds.cpp` `TickDynamicScene` | When bridged, polls `bridge/scene {version: 2}` instead of `render-scene v1`. |
| `EidoverseBridgeScene.cpp` `OnSceneMsg` | A minimal seam: hands `bridge/scene` to `ReceiveDynamicScene` as it is. |
| `EidoverseClient.Build.cs` | Stages `Binaries/<Platform>/Bridge/*` when it has been built (`Bridge/build.sh`). |
| `EidoverseLiveV4JoinTests.cpp`, `Tools/eidoverse/run_v4_join.sh`, `v4_seed.ts` | T0 in the real client, against a scratch current-protocol server. |
| bridge `test/g1-main-wire.test.ts` + `fixtures/main-wire.json` | G1: the renderer's main-wire sites, counted per file and token. |

## The split: each slice's named functions and files

Paths are under `Plugins/Eidoverse/Source/EidoverseClient/`. In `Public/EidoverseBridge.h`, each slice's hook is declared on
its own line with a blank line after it. Add that slice's new members right under its hook.

### A. Render model

- **Hook:** `OnSceneMsg` (`Private/EidoverseBridgeScene.cpp`). Replace slice 0's pass-through: `held` items skip
  `transforms`, and `seats[]` go to slice D through a call, not a read of D's state.
- `ReceiveSceneWire` (`EidoverseBridge.cpp`): `render-scene` goes away.
- `Private/EidoverseWorlds.cpp`: all of it except slice 0's poll line.
- `Private/EidoverseScene*.cpp`: key fixes. Refresh the key pin with `bun test/renderer-keys.ts <repo>`, against the tree
  **without** the local MSVC commit: T3b pins line numbers.
- Tests: T3 (bridge); T11 world rendering (`test_dynamic_world`, `test_sky`, `test_level_water`, `test_particles` …).
- G1 tokens it retires: `render-scene`.

### B. Chat, rights, small renames

- **Hook:** `OnChatMsg` (`Private/EidoverseBridgeChat.cpp`). `bridge/say` → `AddChatLine` / `NoteSpeaker` /
  `AttentionGate.Say`; `bridge/narration` → `Narrate`.
- `ReceiveChatWire` (`EidoverseBridge.cpp`): the `log`, `whisper` and `snap` branches.
- `Say`, `Whisper`, `SendPose` (the `emote` → `pose.anim` line) and `Pose` (reading `anim` first, then `emote`), in
  `EidoverseBridge.cpp`.
- `ReceiveHistory`, `QueueWhileReconnecting` and the chat-window half of `ReceiveSnapshotExtras` (`EidoverseProtocol.cpp`).
- `CanBuild` / `ReadRights` (`EidoverseBridge.h` / `.cpp`): `permissions[]` and `rank`.
- `EidoverseUIChat.cpp`, `EidoverseVoiceGlue.cpp`, `EidoverseCapture.cpp` (`capture` / `capture-result`),
  `EidoverseClips.cpp` and `EidoverseUI.cpp` (`/defs.actionbar`), `EidoverseTravel.cpp` (`/worlds` people objects).
- Tests: T4, T5 (bridge); T11 chat (`EidoverseLiveChatTests.cpp`).
- G1 tokens it retires: `whisper`, `snap`, `snap-result`, `emotes`, `verb` in `Say` / `VoiceGlue`.

### C. Build and world writes

- **Hook:** `OnWriteReply` (`Private/EidoverseBridgeWrites.cpp`). It runs after the asker's `BridgeDo` callback. Re-keying
  a `$b<n>` ghost by `reply.ids` belongs here.
- `ReceiveWriteWire` (`inspect`), `RequestInspect`, `SendVerb` and `SendVerbJson` (`EidoverseBridge.cpp`). These go to
  `BridgeDo` / `Request`.
- `EidoverseUIBuild.cpp`, `EidoverseUIBuildTools.cpp`, `EidoverseUIStructure.cpp`, `EidoverseUIInspect.cpp`,
  `EidoverseUIWorld.cpp`, `EidoverseCommands.cpp`. `/boom` is the engine's `force` now (`bridge/do fn: "force"`), not a GAP.
- Tests: T6, T7 (bridge); T11 build (`EidoverseLiveBuildKeysTests.cpp`, `test_build_*`, `test_inspector`,
  `test_world_panels`).
- G1 tokens it retires: `SendVerb(…)` (the build and command sites), `inspect`, `world-access`.

### D. Bodies, seats, access

- **Hooks:** `OnBodiesMsg` (`Private/EidoverseBridgeBodies.cpp`) and `OnAccessMsg` (`Private/EidoverseBridgeAccess.cpp`).
- `ReceiveBodiesWire` (`lease`, `EidoverseBridge.cpp`).
- `ReceiveAccessProtocol`, `ReceiveSnapshotSeats` and `RequestAccessDirectory` (`EidoverseProtocol.cpp`).
- `RequestAccessState` and `SendAccessGrant` (inline, `EidoverseBridge.h`).
- `EidoverseInteractions.cpp`, `EidoverseLocomotion.cpp`, `EidoverseSeats.cpp`, `EidoverseUIAccess.cpp`.
- Tests: T8, T9, T10 (bridge); T11 `EidoverseLiveAccessTests.cpp`, `test_interactions`, `test_seats`, `test_door`.
- G1 tokens it retires: `lease`, `access-state`, `access-directory`, and `SendVerb(…)` in Interactions/Seats.

### Shared, by agreement

- **`test/fixtures/main-wire.json` (G1).** Each slice lowers only its own counts (`bun test/main-wire.ts <repo>`). The one
  shared line is `SendVerb(…)` in `EidoverseBridge.cpp` (B's `Say`, C's `SendVerb`). Whoever lands second rebases and
  re-runs the scan.
- **The `snapshot` branch of `Receive` and `OnBridgeMessage` are core.** Slice 0 / E own them. A slice that needs a new
  `bridge/*` type routed asks for one line there.
- **Slice E** flips `bUseBridge` on by default once the servers are on the current protocol. It then deletes each
  `Receive*Wire` that is left, and empties G1's list.

## Running the receipt

```sh
sh Tools/eidoverse/run_v4_join.sh     # EIDOVERSE_REPO: an engine checkout (default: this directory's engine/); UE_ROOT: the engine install
```

On Windows with Smart App Control on, the unsigned packaged executable is refused by the OS. The sidecar then falls back to
`bun` on the sources (`%USERPROFILE%\.bun\bin\bun.exe`, which is signed). The script's log says which one ran.
