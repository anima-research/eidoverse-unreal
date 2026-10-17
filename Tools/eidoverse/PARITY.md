# Unreal / Three.js parity

## Upstream port 2026-10-16 (three.js main through 034c41a; local only — not deployed, not pushed)

Ported from the upstream merge (server side: branch `unreal/native-client`, render-scene projects the
shaped terrain and the one world water level):
- **Water, one level per world** (`waterParams`/waterkind): still/river/ocean cells on a level-water mesh with
  the shared wave surface; wading slows with depth, swimming floats on the swimmer's law, peers show swim/tread.
- **This body**: size 0.5–2 and a nameplate lift on the pose wire (`scale`, `plateY`), per-body prefs; peers'
  `mic`/`hear` (the not-hearing ear on their plate).
- **The lantern** (Ctrl/Cmd+K): one line for talk, /commands and named actions.
- **The ∃ menu waterfall**: Save/Load world (listed, not offered — the upstream explanations), Panels ▸ (voice,
  lantern, windows with pins, pointer, rail edge, lock, reset), Log in/out, Help (H), Keys, About ▸ (client
  version + the server's /version). Closes on mouse-away (450 ms), a press in the world, Esc (flyout first);
  it now hangs beside the rail on either edge.
- **Nameplates** (plateanchor.js): crown measured once at rest; hung over the live hips, rigid while standing
  (idle/walk/run/jump), blended over the head when lying, lifted along the camera's up when seen from above.
- Quiet default (the World panel starts closed); typed names with `< > "` are refused in the server's words;
  under a real sky clock the hour, day-speed and hour steps hide.

Not ported (web-only): status chips (WebGL/GPU caveats), the lite client switch, HUD layout mode (frames here
are always draggable unless locked). Deferred: flora local placement.
Tests: `test_level_water.py`, `test_swim_level.py`, `test_body_size.py`, `test_lantern.py`, `test_menu.py`,
`test_plate_anchor.py` (bots: plate-stand / plate-lie POSE_BOT_CLIP=lie / plate-sit POSE_BOT_CLIP=sit).

## Status 2026-10-06 (local only — not deployed, not pushed)

The full parity backlog lives in `parity/BACKLOG.md` (specs in `parity/*.md`). Landed since
the first slice: protocol and chat (whisper tabs, autocomplete, markdown, history), seats and
held poses, remote and local ragdoll, bodydrag (both halves, nails), flight, climb/helm/mantle,
reach, wings, sky/fog/weather surfaces, parts/pictures/particles/sounds, residency, flora
pushers and keep(d), build tools (lights, drag, lock/guard, seat gizmos, structure editor with
preview, inspector fields, scene Find), voice (WebRTC SFU), TTS endpoints, STT (built; needs
an authorized run), front door roster and key, video settings, frames resize/snap, F3 overlay, rail pins/edge, style tokens.

Open: flora local placement past the server's 12k cap and real species meshes;
pin/edge-drag and style tokens; STT verified by ear.

Tests: `Automation RunTests Eidoverse` (all pass; VoiceLoopback needs an SFU preview on 8994),
and `run_live_tests.sh` for the live ones with bots/fixtures against the local preview.

---

# First world-rendering slice

Implemented locally 2026-09-17. **Not deployed to production.**

## Using it

Open Chat/Login with Enter. Enter a world name. Before joining, enable
**Render server world** for a land scene, or leave it off for the existing
underwater prototype. Land controls: WASD, mouse look, Space to jump.
Disconnect before changing presentation mode. World switches clear scene jobs,
avatars, chat and screenshot consent; no downloaded models or login secrets are
saved. Names are validated before connecting.

On the updated server, browser pairing offers separate buttons for water-only
and all-accessible-worlds approval. Existing sessions remain water-only; sign
out and approve again for multi-world access. No additional rights are granted.
The world-name field is direct navigation, not a discovery directory yet.

## Architecture

The sequencer remains the sole fold. A `renderSceneVersion:1` snapshot capability
enables `render-scene {version:1,reset?:true}` requests. Replies are per-connection
geometry deltas, removals, current effective transforms, and changed environment
data. There are no new authored verbs or persistent render records.

Buildings use the existing `shared/structure.js` plan, including swept/chamfered
walls, diagonal geometry, doors/windows, trim, roofs and independent collision
boxes. Terrain samples the existing height law at 65×65 points. Motion and cargo
mounts use the same effective-transform evaluator as headless perception.
Unreal interpolates sampled root transforms (up to ~3.3 requests/second while
moving); this is not browser-rate local motion evaluation. Static scenes request
updates only on log changes. Pending requests time out after five seconds.

The disposable Unreal scene batches procedural building geometry by material,
uses lit world-space-detail surfaces, loads embedded GLBs with PBR materials,
and suspends/restores prototype actors at runtime. A separate grounded character
explores land scenes. This does not modify the saved underwater level.

`/library/<path>?native=1` negotiates original bytes instead of the browser's
Draco/WebP shadow. No browser default changes. Importing still refuses external
buffers/images, oversized or malformed data, and unsupported compressed payloads.
Downloads are bounded, limited to two concurrent models, cached per scene and
cancelled/retired with their owners. Static model collisions are generated with
CPU-accessible meshes owned by the game-world scene.

## Scope and remaining work

| Area | Current status |
| --- | --- |
| World selection, switching, consent isolation | Implemented; typed names |
| Dynamic model spawn/place/remove | Implemented, original embedded GLB/PBR |
| Structures and collision openings | Shared plans rendered natively |
| Terrain | Shared height law; finite 64×64-cell mesh, no terrain LOD yet |
| Lights | Point lights; shadowless budget, not full browser light policy |
| Root motion and cargo mounts | Sampled canonical transforms with smoothing |
| Avatars/chat/login/screenshots | Existing features retained; land capture labeled `world-projection` |
| Sky/weather/forecast | Shared clock/forecast; native sun/sky, cloud layer, weather haze, bounded rain, surface wetness |
| Grass/biome fields | Shared seeded placements/presets; instanced opaque grass with three mesh LODs, lit GPU wind, 12k plant cap and distance fading; other species use batched geometry |
| Object leases/physics | Canonical server-projected epoch root poses; legacy swept-volume lease simulation only in pre-epoch worlds. Browser cosmetic tumble/slope tilt not yet mirrored |
| Use interactions | E to use, F to punt; explicit actions through chat commands |
| Avatar reach/gestures | Hand/foot IK and wave/cheer/point approximations; no touch attestations |
| Named-part animation, avatar seating/ragdolls/bodydrag | Not yet parity |
| Inspector/building tools, asset browser, particles | Not yet parity |
| Voice/audio, authored panels/media/behaviors | Not yet parity |

Caps are explicit: 512 entities, 64 resident model assets, 16 MiB per model,
two concurrent downloads, 6 MiB scene reply, bounded combined structure geometry.
Unsupported model references remain visible placeholders and failures are
counted. Oversized scenes report a projection error rather than silently
claiming a complete scene. These caps and the lack of streaming/LOD mean this
slice is not yet a large-world performance solution.

Current screenshot rendering retains the inexpensive single-frame capture
path; it can be darker than the full temporal viewport. It is an untrusted
donor view, not an attestation of world truth. Model/terrain geometry is mirrored,
but unsupported components may be absent.

## Validation

Server: `bun test ./tools/render-scene-test.ts ./tools/snapshots-test.ts`,
`bun tools/native-login-test.ts`, `bun tools/native-login-wire-test.ts`,
`bun tools/foldfix-test.ts`, and `bun tools/render-scene-wire-test.ts`.
Wire tests create isolated sequencers and scratch state, never production worlds.

Native fixture: run `bun tools/render-scene-wire-test.ts --native` on loopback
19002. For the two-model visual probe, copy a PNG/JPEG, uncompressed embedded GLB
to the printed scratch `opt/store/crate.glb`, then run
`bun tools/render-scene-control.ts models`. Run `test_dynamic_world.py` in Unreal,
then `probe_dynamic_world.py` and `test_world_switch.py`. A four-entity/two-model
scene, rooted player, aquatic suspension, real snapshot delivery, cross-world
isolation and restoration were checked live. A production crate's original
public asset was used; no production world was joined or mutated.

The material-generation commandlet creates `M_EidoverseSurface` successfully;
the project also reports pre-existing GameFeatureData asset-manager configuration
errors during commandlet startup. Those unrelated project settings were not changed.

Final compiled package: `/Users/olena/unrealtest/build/eidoverse-worlds.MUtqAE`.
Mac arm64+x64 Editor, Development and Shipping compiled successfully; installed
editor binary byte-compared with this build. Final-binary live probes passed
terrain/model/wall collision and a clear doorway, grounded exploration, aquatic
suspension, two loaded PBR models, live place and remove. World switching and
restoration passed on the preceding build (unchanged switching code). The final
HTTP capture is `/Users/olena/unrealtest/eidoverse/dynamic-world-final.png` and
returned `x-eidoverse-scene: world-projection` with Slate UI excluded.
All five `Underwater.Eidoverse` automation tests passed on the final installed
binary. Server checks passed: 14 broker/projection tests (74 assertions), 63
native-login handler checks, 24 fold-conformance checks, and real scene/login/
screenshot HTTP+WebSocket tests. Scene wire tests also verify native original
asset negotiation while ordinary requests retain optimized responses.

An editor shutdown attempted through the console during PIE hit an Unreal
`RestoreEditorWorld` assertion, not a scene renderer stack. Subsequent QA ends
PIE before editor shutdown. No saved map changes were made by the test.

## Weather / flora / interactions — next slice

In a server-rendered world, look at an object within 3m and press **E** to use
its first declared reaction (or `use`), or **F** to request a punt. The server
still checks permissions/proximity. Chat supports:

- `/use entity action`
- `/punt entity [power]` (or `/kick`, translated to the `punt` verb)
- `/reach participant shoulder_l [left]` (canonical contact names)
- `/clear_reach`
- `/wave`, `/cheer`, `/point`, `/help`

`bSimulateObjectPhysics` disables local volunteering, not receipt of another
holder's motion. At most eight local bodies/pending claims; 15Hz streams,
bounded substeps, no persistent per-frame writes. Locked, mounted and authored
moving objects are not volunteered. Bounds over 10m or unloaded colliders are
refused. Terrain/building/model collision supports kicks; shapes are bounding
boxes/spheres, not full Chaos bodies. A lease is released on rest or at 20s.

Weather uses the canonical server clock/forecast. Cloud geometry is a cheap
procedural layer, not volumetric meteorology; no rain audio, lightning or snow.
Grass, dry galleta, Mojave shrubs/yucca, corn and sunflower fields reuse browser
placement and biome definitions, but their native geometry is simplified.
Static object footprints clear plants at scene rebuild; moving-object grass
bending/dynamic clearing, exact row crop variants, all browser surface/placement
overrides and continuous vegetation LOD are not yet parity.

Reach targets use anatomical bone seeds, not measured skin surfaces. No native
`reached:true` is sent without a local hand/skin solve. First-person hands,
palm alignment, full joint/torso constraints, named contacts on the local body,
ragdolls, body dragging, seating/mounts and full streamed body poses are still
missing. Remote puppet/bodydrag requests are not silently accepted.

Materials: run `build_flora_material.py` outside PIE to create/update the lit
foliage and procedural cloud materials. `prepare_interactions_qa.py` does this
and joins only the scratch fixture. `test_interactions.py` verifies weather/
flora and requests use, punt and reach. Keep all fixtures off production.

Validated feature build: `/Users/olena/unrealtest/build/eidoverse-features.QbW5Be`.
Mac arm64+x64 Editor, Development and Shipping succeeded; installed binary
byte-compared. Live scratch checks verified 1,623 plants after object clearings,
lit green foliage and cloud visuals, rain/clear transitions, midnight sun-off,
field removal, native use/punt echoes, physics streams and resting commit,
wire reach receipt, two-bone reach-length clamping (0.01cm tolerance excess),
`/wave` over the actual chat UI and presence wire, and world-switch restoration.
The material generator asserts all graph connections (VertexColor's output is
unnamed, not `RGB`). Protocol checks: 17 projection/broker tests, 70 flora checks,
19 lease checks, 24 fold checks and 63 native-login checks, plus real scene WS
tests, passed. No production writes or deployment.
All five `Underwater.Eidoverse` automation tests also passed on this installed
feature build. Test sessions/servers were stopped and foreground throttling
restored; the saved ocean map was not modified by QA.

## Commons rendering corrections

The first local Commons preview used screen-space GI/reflections because imported
runtime meshes lacked baked distance fields and Lumen surface-cache cards. As of
2026-09-18, hardware-ray-tracing-capable dynamic land scenes use Lumen GI and
reflections with hit lighting. Runtime static meshes generate six-axis Lumen cards
for surface-cache/secondary-bounce support; hit lighting handles detailed surfaces,
skinned avatars and procedural geometry that cannot rely on baked distance fields.
These simple cards are not equivalent to an offline, per-mesh interior card build.
The bridge's `use_lumen_lighting` flag can be disabled before joining for the
cheaper screen-space path; it also falls back automatically when hardware ray
tracing or the hardware-Lumen CVar is disabled. `world_lighting_status` reports
the selected path. No project-wide lighting overrides or server edits are made.

Server-authored lamps and avatar meshes now cast shadows (the current project
uses virtual shadow maps for primary-view direct lighting; Lumen ray-hit shading
traces its own shadow rays). Ground cover remains non-shadow-casting for cost.
This is still not complete Three.js material parity: special VRM material
extensions and transparent-shadow behavior require further work.

### Emissive avatar lamps

Strong emissive material sections now infer companion point lights using the
browser's `6 + 2.6 * glow` intensity rule (40 cd cap). Dim gold trim does not
create a light. Emitter bounds and dominant skin weights locate and attach the
light to the body's own skeleton; no Mythos identity or chest coordinate is
hard-coded. The source VRM is unchanged. The bulb section does not cast shadows
onto its own light, and near-clear transmission sections (factor ≥ .9) do not
seal it inside an opaque shadow shell; opaque body sections retain their shadows.

Each body owns its light components and pulse materials, including hot swaps and
disconnect cleanup. At most two inferred emitters are created per body, with
only four nearby avatar lights enabled across the bridge (12 m activation/range).
Hardware-capable clients use ray-traced companion-light shadows; others use the
project shadow method. `lampglass` follows the browser's 3.4 s / .03–.45 shaped
breath on both glow and illumination, attenuated in daylight. Ordinary emissive
materials do not inherit that special animation. This is a bounded light proxy,
not exact area-light transport or physically coloured transmission through VRM
glass. `check_mythos_emission.py` checks the live attachment; the paired
`preview_emission_shadow.py` / `restore_emission_shadow.py` stage and remove a
runtime-only occlusion test without changing the server world.

Mouse axes now execute in the controller input pass instead of accumulating
rotation from the later pawn tick. Mouse smoothing and the optional extra render
frame are disabled while the land scene is active, and restored on exit. Walking
has quicker acceleration/braking. No project-wide input settings are saved.

Large meadows now stream canonical seeded placements into a deterministic 12k
reservoir (800k candidate budget; structural-plant occupancy retains its 5k limit).
The streaming sink does not consume the browser placement RNG or change its default
output. Ground cover is thinned, not silently omitted. Terrain respects the first
authored layer color and up to 128 cells/side. M_EidoverseTerrain provides a separate
multi-scale world-space ground material; build_terrain_material.py creates it.

69 imported models' vertical bounds were compared to decoded source geometry;
maximum mismatch was 0.000009 cm. This only validates imported geometry, not
dynamic placement: the native projection was missing deterministic sim poses.
That separate gap is corrected below. No blanket snap-to-ground is applied;
tabletop, elevated, and animated props retain their authored transforms.

Async model/avatar jobs use the transient package as their outer. Their explicit
root keeps them alive through worker completion without making a rooted child of
a dying PIE world; scene cancellation still aborts HTTP and discards late results.
Static mesh workers also build in the transient package. Only completed,
uncancelled results are adopted by the weakly supplied scene owner and cooked
for collision on the game thread. PIE cannot destroy a worker's in-flight mesh.

Validated 2026-09-17 using the isolated commons copy: 80 entities, 69 loaded
models (one existing texture-limit fallback), 11,433 grass clumps after clearings.
All five native automation tests and 19 projection/broker tests passed. The final
Mac arm64+x64 Editor, Development and Shipping incremental builds succeeded in
`/Users/olena/unrealtest/build/eidoverse-render-safe.6ruyhA/HostProject`; installed
editor binary byte-compared. Stopping PIE with only three models ready survived
the delayed worker-completion window and restored smoothing/frame-lag settings.
Vertical collision probes hit 63 of 66 non-tiny imported model bounds.

Same-view editor timing (not a packaged/GPU-isolated benchmark): baseline
29.40 ms mean / 41.44 ms p95; updated 16.70 ms mean / 16.97 ms p95 over 480 frames.
Weather advances naturally, so these are practical session measurements, not a
controlled lighting benchmark. No production writes or saved-level changes.

### Commons follow-up: import handedness, asset limits, dynamic simulation

The original cyclic axis permutation preserved handedness and mirrored the
Three.js view. Wire metres now map to UE centimetres as `(z,-x,y)*100`;
quaternion xyz uses the corresponding axial-vector conversion. The GLB importer,
terrain, structures, presence, leases and capture subjects share that basis.
The reflection converts source CCW to Unreal clockwise faces without an extra
index reversal. Tests check an asymmetric imported triangle, outward normals,
all three rotation axes, positive wire yaw and screen-left/right conventions.
Mac mouse look retains the verified legacy AppKit backend via MacEngine.ini.

Static GLBs allow up to 128 embedded images (avatars remain at 32), while retaining
the 32M aggregate pixel, 4096 dimension, 16MiB download and geometry budgets.
The previously rejected 72-texture Commons room is within these limits and now
loads; texture count and aggregate memory rejection have regression tests.

Epoch worlds use server-projected canonical simulation poses, not stale folded
placements or a competing local lease simulation. The server clones its sim cut
and evaluates the existing versioned law without mutating it. Active bodies update
at roughly 15Hz; sleeping scenes return to the prior cadence. Native punts include
direction. Legacy lease physics remains available only before an epoch. This fixes
root-pose transport, not browser-specific cosmetic tumble/slope tilt; static props
are not globally snapped to terrain. No production changes are deployed.

Follow-up validation (2026-09-17): all eight native automation tests passed;
arm64+x64 Editor, Development and Shipping builds succeeded. Local Commons loads
70/70 models with zero failures. The real imported mesh probe reports 19,414
clockwise vs 140 opposite smoothed-face samples (the latter are present in source
geometry). A live native punt was echoed with direction; the client moved the
ball about 89cm, received its canonical resting pose, and opened no physics lease.
The isolated local ball was moved by that test; production was untouched.

Local preview UI: Enter → **Join Local Commons** restores the loopback endpoint
on port 19002, local fixture door, Commons world, local asset library and dynamic
land rendering on every new Play session. It closes the panel to release input,
disables screenshot sharing and excludes inherited production identity tokens.
The compiled preset does not edit saved levels or auto-start a server. The local
Commons server must already be running. `check_local_commons_button.py` verifies
the joined state after clicking the actual button in a fresh PIE session.

## Ground-cover and name-tag follow-up (2026-09-18)

Blade species retain the server's original placements, scale, height and tint,
but use one shared hierarchical-instanced tuft mesh: 36/12/6 blades and
108/36/6 triangles per LOD. Nearby clumps have a wider footprint, curved tips,
root-to-tip shading, per-placement colour variation and vertex-shader wind.
Opaque two-sided leaf shading avoids alpha-card overdraw. Grass shrinks into
the terrain from 28–45 m and is culled beyond that range. It has no collision,
per-plant tick, ray-tracing representation or shadow casting; surrounding objects
still shade it. The 12k placement cap and existing footprint clearings remain.
Other flora retains its batched procedural geometry. This is a bounded quality
improvement, not zero-cost grass or a full streamed vegetation system.

Generate the material with `build_ground_cover_material.py` outside PIE.
`Underwater.Eidoverse.GroundCover` checks runtime mesh LODs and budgets;
`check_ground_cover.py` verifies the live Commons instances and name tag.
`profile_ground_cover.py` runs a 21-second cover/on/off/on frame-time comparison,
restoring visibility afterwards. Wait for its completion before stopping PIE;
keep the camera still and do not compile concurrently. Results are editor frame
times, not isolated GPU or packaged-build benchmarks.

Remote name tags now use the posed humanoid head plus 40 cm world-up clearance,
with visible-body bounds as a fallback for missing head mappings and submarines.
The bottom-aligned tag is smaller and cannot cast a text-shaped shadow. This
works with feet- and capsule-centred protocol roots without a Mythos special case.

### Local night inspection

In a joined local Commons Play session, run `preview_night.py` to hold the
client's lighting at 22:00. `EidoverseScene.set_preview_hour(-1)` restores the
server clock; stopping/rejoining Play also clears the override. Sun, sky fill,
weather colours and avatar emission use the same effective hour. This does not
write a sky verb, alter the world history or affect other clients.
