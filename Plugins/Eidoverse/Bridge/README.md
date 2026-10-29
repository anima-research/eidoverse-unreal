# eido-bridge

A small local process that lets a renderer that doesn't run JavaScript (the Unreal client) join a world. The bridge is
the client as far as the server is concerned: it joins as a replica, folds the world's log with the engine's own code
(`engine/shared/protocol/replica.js`, the same files the browser and the agent run), and writes in the engine's own
request vocabulary. The renderer talks to the bridge over a loopback socket in a small local protocol (`bridge/*`).
Everything else the renderer sends or gets passes through untouched.

Design: `ADAPTER-DESIGN.md` §1 (the shape), §1.1 (process and local socket), §5 (tests), §6 (slices). The server is not
changed. Nothing here is copied from the engine: `engine/` is a git submodule pinned at the sha in `engine.pin`, and
`test/g2-one-fold.test.ts` refuses any fold or view code defined here.

## Run

```sh
git submodule update --init engine
ln -s <an engine checkout>/node_modules engine/node_modules   # the engine's own deps (or: cd engine && bun install)
echo "<64 hex token>" | bun src/main.ts                       # prints {"port": P}; exits when stdin closes
```

The launcher (the Unreal sidecar) writes a random token on stdin and reads `{"port": P}` from stdout. A local
connection must offer `Sec-WebSocket-Protocol: eido-bridge.<token>` and carry no `Origin`. Its first message is
`bridge/hello {server: "wss://host/ws", cookie?: "<ew_sess value>"}`; then the renderer sends its usual `join`.

| local message | way | what |
|---|---|---|
| `bridge/hello {server, cookie?}` | → | dial the server (cookie as `Cookie: ew_sess=…`); answered `bridge/hello {ok, protocol}` |
| `bridge/ready {world, worldId, head, you}` | ← | the join's cut is adopted: the world can be drawn |
| `bridge/status` | ↔ | the fold's head, entity count, stats, a digest of the view |
| `bridge/scene {version: 2, reset?}` | ↔ | the render model (the body `EidoverseScene.cpp` Apply reads), from the fold here; nothing goes to the network |
| `bridge/do {req?, fn, …}` | → | a write (`src/writes.ts`): `fn` names an engine `requests.js` builder or a §3.5 row; ids in view spelling. Answered `bridge/done {req, fn, reply}` (the authority's reply, `ids` decoded) or `bridge/error {for: "bridge/do", req, fn, code, reason, fix?}` |
| `bridge/say {seq, actor, name, text, to?, spoken?, utt?, t0?, ts}` | ← | a line said here, from the fold (speech is arrival-only) |
| `bridge/narration {text, cause}` | ← | a logged role grant or revoke, or a new asset (kick/ban come as the server's own `moderated`) |
| `bridge/scene` `transforms[].held {by, hand}`, `seats[]` | ← | a held thing at its holder's hand (frames + HAND_Y); every seat with its occupant or thing |
| `bridge/ride {req?, to, socket?, id?}`, `bridge/unride {req?, pos, id?}` | → | sit / stand (use, move), or a thing rides / stops (requests.js parent, unparent); answered `bridge/ridden {req, fn, reply}` |
| `bridge/access-state` | ↔ | the access tab from `@world` access, default-role, roles:*; pushed again when they change |
| `bridge/access-directory {q}` | ↔ | whois (people this world has seen); a miss says search by name isn't available |
| `bridge/error {for, code, reason}` | ← | a local refusal (never a server message) |

## Test

```sh
flock /home/claude/eido/.testlock bun run test          # every suite; each starts its own server from engine/ on a scratch worlds dir
```

| suite | what |
|---|---|
| `t0-join` | the renderer's join is refused as is and admitted through the bridge; token, Origin, stdin EOF, close codes |
| `t1-passthrough` | the PASS lanes, byte for byte, both ways (fixtures name their C++ file:line) |
| `t2-resync` | a lost log message or a purge: one re-join, one snapshot, the same fold and scene as a fresh joiner |
| `h-table` | the `bridge/*` table and the bridge's own requests |
| `t6-build` | build writes: the renderer's build sites (`test/fixtures/writes.ts`) as `bridge/do` → `requests.js` requests; `$b<n>` → minted ids; refusals are the authority's |
| `t7-world` | world and moderation writes: the world panels and commands as `bridge/do`; names resolve to identities in the bridge, rights stay the authority's; `/boom` is a GAP |
| `t3-scene` | the render model against main's `render-scene.ts` on main's fold (`MAIN_DIR`, default `/home/claude/eido/wt-main-ccd5`), for `engine/spec/fixtures`, `test/fixtures/worlds` and `ORACLE_WORLDS`; and the key pin against the renderer's C++ (`UNREAL_DIR`; `test/fixtures/renderer-keys.json`, refreshed by `bun test/renderer-keys.ts <unreal dir>`) |
| `t4-chat` | says → `bridge/say`, spoken lines, arrival-only history, whisper refused by policy, narration |
| `t5-rights` | your-rights and refusals are the authority's, byte for byte; the bridge never refuses or grants |
| `t8-holding` | grab/put/throw/drop/punt answered by the authority; `held` at the holder's hand; holds from the join cut |
| `t9-seats` | `seats[]`, bridge/ride and unride, far and taken seats refused by the authority |
| `t10-access` | bridge/access-state and its pushes, the tab's grants as requests, 4010 mirrored, the directory gap |
| `g2-one-fold`, `g3-confined` | no fold or evaluator code here; the engine pinned and clean; the handshake in `main.ts` only |

## Remove

The bridge is this directory. Delete it, and the renderer's launcher of it (the Unreal side's `EidoverseSidecar.*`).
Nothing else knows it exists.
