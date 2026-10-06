# Eidoverse world rendering and simulation: web vs Unreal parity audit

The Unreal client covers the world's basic structure: terrain from the height law, structures from the shared plan, whole-entity closed-form motion, whole-entity mounts, water and air volumes, the shared clock and forecast, and grass that is instanced and LODed but capped. The largest gaps are named-part animation, particles, pictures and world sounds (none exist natively), plus distance fog, placed-light behaviour, grass reach and density, and weather effects on surfaces. Of those, only fog, lights, grass and weather are cases where the native client already has the data and just renders it differently.

All paths below are relative to `~/connectome-local/eidoverse-worlds-native/` (web, shared, server) or to `/Users/olena/Documents/Unreal Projects/Test1/Plugins/Eidoverse/Source/EidoverseClient/` (Unreal, prefixed `UE:`). This was a read-only audit; no files were changed.

## What render-scene carries

**Data path baseline** (`server/render-scene.ts`):
- **Each entity (`projectEntity`, :54–68):** `{id, lib, kind, structure{boxes,parts,sweeps}, color, intensity, range, collide (default "exact"), components:[names only], data, interact{actions, sockets, locked, mounted, moving}, revision}`.
- **`data` (`projectComponentData`, :37–50):** only `water` (normalized), `air.boxes`, and pass-through copies of `environment`, `vehicle`, `traversal` and `collision`.
- **`transforms`:** `{id, p, q, scale, motion?, mount?, leased?}`.
  - `localMotion` (:75–80) returns null for part motions and for sim bodies.
  - `localMount` (:81–87) returns null for sockets that name a `part`.
- **`environment`:** `{terrain{size, segments 64–128, heights, color = first layer color, authored}, sky (raw), grass (raw), flora{strokes:[{species, archetype, height, wind, color, placements ≤12000 total}]}}`.
- **`atmosphere`:** `{clock (effectiveClock), weather (effectiveSky), time}`.
- **Warnings:** components named `particles`, `flora`, `panel`, `physics` or `motion:*` are reported as unsupported (:144).

**The native client already gets more than render-scene.** It receives the full `snapshot` (`server/server.ts:629–653`, including `state.entities[*].comp` bags) and every `log` entry (`UE: EidoverseBridge.cpp:Receive`, :523/:574). So every component bag reaches Unreal today. Reading it from there would be a second fold, which PARITY.md rules out. The specs below therefore name a render-scene addition for each gap.

**Unreal static models are merged.** `UE: EidoverseAvatar.cpp:FinishDecode` (:370) calls `LoadStaticMeshRecursiveAsync`, which flattens every GLB node into one `UStaticMesh`. Each entity is one `UStaticMeshComponent` (`UE: EidoverseScene.cpp:Apply` :144–150). There is no node hierarchy, so anything addressed to a named part cannot work. This one decision is behind features 1, 6 and part of 13.

## Summary (highest user-visible impact first)

| # | Feature | Unreal | Server change needed? | Effort |
|---|---|---|---|---|
| 1 | Named-part animation (`motion:<part>`, `motion.part`) and part sockets | missing | yes | L |
| 2 | Particles (`particles` component) | missing | yes | M–L |
| 3 | Distance fog and sky tuning args | partial | no | S |
| 4 | Placed lights: day dimming, keep/day, gizmo, slot budget, decay | partial | yes (keep/day) | S–M |
| 5 | Inferred lamps on emissive placed models | missing (avatars only) | no | S |
| 6 | Flora density, distance, pushers, species fidelity | partial | optional | M–L |
| 7 | Weather surface effects (wetness, puddles, cloud shadows, rain dressing, cloud kind) | partial | no | M |
| 8 | Pictures (`picture` component) | missing | yes | M (after #1) |
| 9 | World-placed sounds (`sound` component) | missing | yes | M |
| 10 | Physics presentation (tick interpolation, tumble, slope tilt) | partial | yes | M |
| 11 | Residency/streaming and sized placeholders | partial | no (`geom` already sent) | M |
| 12 | Collision policy | partial | no | S–M |
| 13 | Structures (finishes, AO bake, palette defs, cutaway) | mostly present | optional | S–M |
| 14 | Terrain (layers, texture) | mostly present | optional | S |
| 15 | Water / air / environment | present (single-surface limit) | no | M |
| 16 | Whole-entity motion | present | — | — |
| 17 | Whole-entity mounts | present | — | — |
| 18 | Day/night light curve | partial | no | S |
| 19 | glTF features | mostly present / parity | no | S |
| 20 | Shadow policy | divergent | no | S |
| 21 | Browser-only modules | n/a | — | — |

---

## 1. Named-part animation and part-riding sockets (HIGH)

**Web behaviour** (`client/lib/motion.js:13–31`, `:65–112`, `:125–191`; `client/lib/world.js:323` `findPart`, `:380` `mountTransform`; `client/lib/realize/models.js:595` `execMount`)

**Which components:**
- Every key `motion:<partName>`, and a `motion` whose data has `part: "<name>"`, animates one named GLB node (`Object3D.name === partName`, first match in a depth-first traversal that excludes the root).
- Several `motion:<part>` keys may coexist on one entity.

**Part rest pose `mbase`:** captured the first time the part animates as `{pos: part.position, quat: part.quaternion}` (local to its parent).

**Evaluation each frame** (time `t = max(0, (serverNow − t0)/1000)`; a missing `t0` is anchored at first evaluation):
- **pendulum:** `theta = amp·e^(−damp·t)·cos(2π/period·t + phase)`.
  - Defaults: `period 3.5`, `damp 0`, `phase 0`, `amp` (alias `amplitude`) 0.
  - Axis defaults to `[1,0,0]`; may be `"x"/"-y"/…` or `[x,y,z]`.
  - **Part pivot defaults to `[0,0,0]`** (the node origin). Whole-entity pivot is `[0,2,0]`.
- **spin:** `theta = phase + rate·π/180·t`, where `rate = degPerSec ?? rpm(6)·6`. Axis default `[0,1,0]`, pivot `[0,0,0]`.
- **Applying a rotation to a part (`rotatePartAtPivot`, :65):**
  - `q = axisAngle(normalize(axis), theta)`
  - `part.quat = mbase.quat · q`
  - `part.pos = mbase.pos + mbase.quat·(pivot − q·pivot)`
- **bob:** `pos = mbase.pos + normalize(axis[0,1,0]) · sin(2π/period(4)·t + phase)·amp(0.3)`. Rotation is untouched.
- **orbit:** `a = phase + degPerSec(12)·π/180·t`; `pos = (c.x + r·sin a, c.y ?? mbase.y, c.z + r·cos a)`.
  - `c = center ?? mbase.pos`. **Part radius defaults to 1** (whole-entity default is 3).
  - If `face !== false`, the part rotation becomes `(0, a + π/2, 0)` as Euler, which replaces the rest rotation.
- **path:** the same arc-length evaluator as whole-entity motion (`motioneval.js:121`), with `base = {pos: mbase.pos, yaw: 0}`. If `rot`, the part rotation becomes `(0, yaw, 0)`.
- **Unknown type or malformed path:** the part keeps its last pose (no error).

**Ending:** when a part's motion key disappears, the part snaps back to `mbase` and `mbase` is forgotten (:181–190).

**Distance gate:** entities more than 90 m from the camera are skipped. Because motion is closed-form, there is no drift (:144).

**Sockets that name a part:** a socket in `sockets[slot].part` makes mounted cargo attach inside that node. `execMount` reparents the child into the node while preserving its world transform. Seated avatars ride the part's displacement (`live · rest⁻¹`) in `mountTransform`.

**Mounted entities:** these skip their own motion; they ride their parent.

**Data path to native:** missing.
- `localMotion` returns null for part motions (`render-scene.ts:77`).
- `effectiveWorldTransform` refuses part sockets (`mcpl/effective.ts:78–83`). That entity then gets **no transform at all** (`render-scene.ts:160`, warning only).
- Server change:
  1. In `projectComponentData`, add `data.parts = {[partName]: motionData}` built from every `motion:*` key plus `motion` when it has `part`. Resolve a missing `t0` exactly as `localMotion` does.
  2. Have `localMount` emit `{to, slot, part, offset, yaw}` for part sockets instead of returning null.
  3. Emit a parent-relative transform rather than dropping the entity.

**Unreal status:** missing.
- `UE: EidoverseMotion.cpp:EvalWholeMotion` returns the base pose for `part` motions.
- There are no nodes to animate (merged static mesh).
- **Bug found:** an entity mounted on a part socket gets no `transforms` entry, so `UE: EidoverseScene.cpp:Apply` never sets `bPlaced`. The actor stays at the world origin.

**Implementation spec:**
1. Import models that carry `parts` data (or every model) as a node hierarchy:
   - glTFRuntime `UglTFRuntimeAsset::LoadStaticMesh(MeshIndex)` per node, attached to `USceneComponent`s named after the glTF node names, with the node TRS applied (see the avatar attachment loop at `UE: EidoverseAvatar.cpp:439`).
   - Alternatively `AglTFRuntimeAssetActor`.
   - Keep the merged mesh for entities with no part data (performance).
2. Port `evalPart` above. Work in wire space with the existing quaternion helpers in `EidoverseMotion`. Convert results with `FromWirePosition`/`FromWireRotation`. Write the result to the component's relative transform.
3. Use the shared server clock (`ServerNowMs()`).
4. Restore rest poses when a key vanishes.
5. Attach cargo with a part socket to that component (keep world on attach).

**Effort:** L (importer change plus evaluator plus server field).

**Notes:** this blocks #8 (pictures) and seating on swings. Collision for animated parts can stay on the root.

---

## 2. Particles (`particles` component) (HIGH)

**Web behaviour** (`client/lib/emitters.js:86–162`, `:176–220`; `shared/particles.js:25–227`)

**Data:** `{preset, seed?, texture?, origin?, quality?, count?, size?, opacity?, speed?, lifetime?, area?}`.

**Presets and default counts:** fire 150, sparks 220, embers 140, smoke 70, dust 160, snow 320, magic 180, stars 420, muzzle 48. An unknown preset renders nothing but is not erased.

**Bounds** (values are clamped to these):

| Field | Range |
|---|---|
| `count` | 1–600 (hard cap 600) |
| `size` | 0.005–4 |
| `opacity` | 0–1 |
| `speed` | 0.05–8 |
| `lifetime` | 0.05–30 s |
| `area` | 0–8, a number or `[x,y,z]` |

**Origin:** entity-relative metres, default `[0,0,0]`, each axis clamped to ±8.

**Texture:** must be a `.png` under `eidoverse/assets/particle_textures/`; otherwise the preset's own sprite is used. Textures are cached and shared, never disposed per emitter.

**Seed:** the authored integer `>>>0`, else `FNV-1a("<entityId>:<preset>")`. The upstream builder draws its per-instance spawn attributes from `Math.random`, which the host replaces with `mulberry32(seed)` for the duration of the build.

**Quality:** tiers `auto 1, high 1, med .5, low .25`. Drawn count is `max(1, round(count · min(tier(authored quality), tier(local governor tier))))`. Thinning changes only the instance count, never a rebuild.

**Lifecycle:**
- The emitter is a **child of the entity** at `origin`, so it rides place, mount and motion.
- It is built when the entity exists; pending otherwise.
- It is retired on remove or demote and rebuilt on spawn/promote.
- No wetness or cloud shadow is applied to it.

**Where the visuals live:** the actual look per preset (velocities, colour ramps, billboarding) is in the upstream `eidoverse/particles.js` (`makeParticles`). It is **not in this repo**; the server serves it at `/library/eidoverse/particles.js`.

**Data path to native:** missing. `particles` is flagged as unsupported at `render-scene.ts:144`. Server change: add `"particles"` to `PASS_THROUGH` (it is already size-bounded by the 16 KB check), or project `normalizeParticles(data, {entityId})` server-side so native gets the normalized emitter, including the seed.

**Unreal status:** missing. No Niagara or particle code exists in the plugin.

**Implementation spec:**
- One Niagara system asset per preset, GPU sprites.
- Spawn it attached to the entity actor's root at `FromWirePosition(origin)`.
- Set `User.Count`, `User.Size`, `User.Opacity`, `User.Speed`, `User.Lifetime`, `User.Area` from the normalized data.
- Use a fixed random seed (Niagara "Deterministic Random" with the seed above) so all clients match.
- Apply the local quality scale through the spawn-count user parameter.
- Derive preset constants from `/library/eidoverse/particles.js`.
- Retire with the actor.

**Effort:** M for the plumbing, plus L of art time for nine presets.

---

## 3. Distance fog and sky tuning args (HIGH: the visual feel of every world)

**Web behaviour:**
- **Fog** (`client/lib/core.js:168`): `scene.fog = new FogExp2(0x0b0f12, 0.018)`. Density each frame is `0.018 · (sky.fog ?? 1)` (`client/lib/sky.js:799–800`), so fog = `1 − exp(−(0.018·d)²)`.
  - That is **50% at about 46 m and 90% at about 84 m.** Worlds fade out under 100 m.
  - The sky dome is exempt from fog.
  - Fallback fog colour: lerp of `HSL(0.6, 0.2, 0.05 + 0.5·day)` and `HSL(0.07, 0.32, 0.04 + 0.45·day)` by `warmth = (1 − day)^1.5`. On Skye's sky the sky drives the colour (:793–797).
- **Sky args** (`sky.js:643` `applyLive`, `:729` `renderSkyMesh`, `:775` `applyTuning`):
  - `sun` and `ambient` multiply sun and hemisphere intensity every frame.
  - `fill` sets a counter-light at `0.95·warmth·fill`.
  - `exposure` sets tone-mapping exposure: `1` on Skye's sky, `(1 − 0.22·warmth)` on the fallback, times `exposure`.
  - `azimuth` (fallback sun azimuth, default 180°).
  - `colors` → `skyApi.setColors`.
  - `clouds ∈ {clear, cumulus, stratus, cirrus}` selects the cloud flavour, and its coverage floor is `CLOUD_COVER = {clear 0, cumulus .38, stratus .6, cirrus .15}` (`materials.js:484`).
  - Tone mapping is ACES filmic (`core.js:158`). There is no bloom or post-processing on web.

**Data path to native:** present. `environment.sky` is the raw sky bag (`render-scene.ts:31`) and `atmosphere.clock`/`atmosphere.weather` arrive every update.

**Unreal status:** partial.
- `UE: EidoverseEnvironment.cpp:ApplyWeather` (:129–161) reads only `clock.hour`, `weather.weather`, `k` and the `seg` transition.
- Fog is ExponentialHeightFog: density `.0008 + cloud·.006 + rain·.009`, start 10 m (:157–158). That is much thinner than web.
- It ignores `sky.fog/sun/ambient/fill/exposure/azimuth/colors/clouds`.
- `EidoverseWorlds.cpp` (:71–72) fixes exposure at 1 and leaves Unreal's default bloom on.

**Implementation spec:**
- Read `environment.sky`.
- **Fog:** height falloff ≈ 0 and start distance 0. Tune density so 50% opacity lands at `46 m / sky.fog` and 90% at `84 m / sky.fog`.
  - Unreal fog is exponential, not exp², so exact parity is impossible. Match those two points: roughly `FogDensity ≈ 0.015·fog` in Unreal units, verified by screenshot.
  - Keep the weather increments on top.
- **Sun and sky fill:** multiply sun intensity by `sky.sun` and sky light by `sky.ambient`.
- **Exposure:** post-process exposure compensation of `log2(sky.exposure)`.
- **Clouds:** `CloudAmount = max(CLOUD_COVER[sky.clouds], weatherCover·k)`. See #7 for the weather cover table.
- **Optional:** disable bloom to match the web's lack of post-processing.

**Effort:** S.

**Notes:** web Skye's sky (upstream, not in this repo) owns sun colour and haze on the main path, so beyond the formulas above only visual matching is possible.

---

## 4. Placed lights (`light` verb entities) (HIGH)

**Web behaviour** (`client/lib/lights.js:35–93`; `client/lib/lightrig.js:352–388`, `:476–484`, `:586–675`, `:801–830`; fold defaults in `shared/fold.js:238–247`)

**Fold fields:** `{pos, color 0xffd9a0, intensity 16, range 10, keep?: true, day?: false, yaw, scale, comp, parent}`.

**Visible gizmo:** a 0.13 m sphere (16×12 segments), unlit and not tone-mapped, in the light's colour. No shadows, no wetness, no camera collision, always shown.

**Point light comes from a fixed pool:**
- `N_SLOTS = 8` (URL-configurable 0–16).
- Ranking: `tier (keep 0 > authored 1 > inferred lamp 2)`, then camera distance (incumbents' distance × 0.85 for hysteresis), then key.
- `keep` requests always get a slot. Others are capped by `slotCap` (governor-sheddable).
- Reassignment happens when dirty, every 600 ms, or when the camera moves more than 1.5 m.
- Losers stay as gizmo only ("glow-only").

**Per-slot values:** `decay = 1.7`, `distance = range`. Intensity is `intensity · (dayAware ? (1 − dayness)² : 1)`, where:
- `dayAware = day !== false`
- `dayness = max(0, sin((hours − 6)/12·π))`, and is 1 when the clock is unset

So **placed lights are nearly off by day**: about 0.086× at 09:00 and 0 at noon.

**Shadows:** only slot 0 can cast. **Placed lights never request shadows**; only avatar lamps do.

**Data path to native:** partial.
- `color`, `intensity` and `range` are sent (`render-scene.ts:63`).
- **`keep` and `day` are not sent.** Server change: in `projectEntity`, add `...(e.kind === "light" ? {keep: !!e.keep, day: e.day !== false} : {})`.
- Note that `revision` hashes `out`, so the hash will include these fields.

**Unreal status:** partial. `UE: EidoverseScene.cpp:Apply` :131–135 creates a `UPointLightComponent`:
- candela intensity equal to the wire value, attenuation `range·100`
- **`SetCastShadows(true)` on every placed light**
- **no day dimming, no gizmo, no budget**

**Implementation spec:**
1. **Gizmo:** an unlit sphere of radius 13 cm with emissive equal to the light colour. No collision, no shadow.
2. **Each weather/atmosphere update:** `L.Intensity = intensity · k_decay · (day ? (1 − dayness)² : 1)`. `dayness` is computed from the same `WeatherHour` already in `ApplyWeather`, preview hour included.
3. **Decay:** web `1/d^1.7` and Unreal inverse-square differ by `d^0.3`. Use `k_decay ≈ (range/2)^0.3` (about 1.62 at range 10) as a compensation, or tune visually. Both engines window by `(1 − (d/r)^4)^2`, so keep `AttenuationRadius = range`.
4. **Shadows:** `SetCastShadows(false)` for placed lights (web parity and cost).
5. **Budget:** enable only the nearest 8 by `(keep, distance)` with 0.85 hysteresis, re-ranked every 0.6 s. Keep the rest visible as gizmo only.

**Effort:** S (client) plus S (server).

---

## 5. Inferred lamps on emissive placed models (MEDIUM)

**Web behaviour** (`lightrig.js:409–457`, called for every realized placed model from `realize/models.js:409`):
- **Which meshes count:** an emissive mesh counts if either:
  - it has no emissive map and `glow = emissiveIntensity·max(emissive.rgb) > 0.5`, or
  - it has an emissive map and `emissiveIntensity > 1`
- **At most 2 per model.**
- **Each lamp:**
  - position: the centre of the mesh's bounding sphere, following the mesh
  - intensity: `min(40, 6 + 2.6·max(glow, emissiveIntensity))`
  - colour: the emissive colour if its saturation (`max − min`) is above 0.25, else `0xffd9a0`
  - range 12, `dayAware: true`, **no shadows**, slot tier 2 (lowest)

**Data path to native:** present (the GLB material).

**Unreal status:** missing for models. `UE: EidoverseAvatarLights.cpp` implements the same rule (`InferredIntensity`, 2 per body) only for avatars. Static models get Lumen emissive at most.

**Implementation spec:** after `StaticMeshReady`, scan the materials' `emissiveFactor`/`emissiveStrength` parameters exactly as `BuildEmissiveLamps` does. Place up to 2 point lights at the section bounds centre with the rule above, no shadows, the same day dimming as #4, at the lowest priority in the #4 budget.

**Effort:** S.

---

## 6. Flora: density, distance, wind, pushers, species (HIGH in meadow worlds)

**Web behaviour** (`client/lib/flora.js:289–295`, `:95–145`, `:578–613`; `client/lib/vegetation/vegetation.js:735–888`; `defs/flora/*.json`)

**Density and distance:**
- Full placement density with no cap. Grass `density 22` gives hundreds of thousands of blades.
- Density by camera distance `d`: `keep(d) = ((90 − d)/(90 − 15))^2.5`, so full density under 15 m and **invisible beyond 90 m**.
- Survivors grow to cover the gaps: `grow = mix(1, 1.7, smoothstep(15, 90, d))`.
- An optional per-blade dither keeps 40% of blades far away.

**Wind** (per instance; vertex displacement in XZ × `h²`, where `h` is 0 at the root and 1 at the tip):
- **Global sway:** `windDir · sin(dot(xz, windDir)·0.5 + 1.2t) · base`.
- **Gusts:** `windDir · sin(dot(xz, windDir)·gustFreq + 2.5t) · gust · smoothstep(.3, .7, gustNoise(xz·0.02 + 0.0375t))`.
- **Flutter:** `(sin(3t + phase), cos(0.7(3t + phase))) · 0.1·flutter`.
- `windDir = normalize(1, 0.3)`. `base/gust/gustFreq/flutter` come from the species def; `base` and `gust` are scaled by the stroke's `wind`.
- Gust noise is a 256² four-octave value-noise texture (start frequency 0.035, amplitude 0.55, halving).

**Pushers** (plants part around bodies):
- Up to 4 pushers: you plus the 3 nearest remotes, each at the body root position with radius **1.1 m**.
- For each pusher: `delta = root.xz − P.xz`; `s = 1 − smoothstep(0, r, min(|delta|, |vertex.xz − P.xz|))`; `push += delta/max(|delta|, 0.3) · s² · 1.35·pushScale`.
- Then `push = push · 0.6·tanh(|push|/0.6)/|push|`.
- Displacement is `wind + push·(0.3 + 0.7h)`.

**Clearing:** plants inside any collider flagged `interior` (exact colliders with a floor, and structures) sink by 4 m via a 256² world mask, plus a 0.15 m apron. The mask is repainted on entity events.

**Species:**
- Real generators for corn, sunflower and shrubs (`vegetation/*_gen.js`).
- PBR map sets per species.
- Per-placement `tx/tz` tilt (`shared/flora-placement.js:119–123`, slope-following).
- Colour from the `colorVar` palette.
- `heading` for sunflowers.

**Data path to native:** present but capped.
- `environment.flora` holds at most 12,000 placements in total, using a reservoir for large meadows (`server/render-flora.ts:19–46`).
- `environment.grass` (raw args) is also sent, and `/defs` serves the species defs. So native could run the placement law itself with no cap and no server change.

**Unreal status:** partial.
- `UE: EidoverseEnvironment.cpp:BuildFlora` (:37–121) has the 12k cap (:55).
- Grass is one HISM tuft mesh (`UE: EidoverseGroundCover.cpp:BuildTuft`, 36/12/6 blades) with **cull at 28–45 m** (:107).
- Wind comes from per-instance custom data in `M_EidoverseGroundCover`.
- There are **no pushers**.
- Clearing is done by ray-casting against entity bounds at build time.
- Non-grass species are simplified procedural cards.
- `tx/tz` tilt is ignored.

**Implementation spec:**
1. **Density:** port `shared/flora-placement.js` (`placeInstances`/`placeRows`, mulberry32) together with `flora_args.js:mapGrassArgs/presetStrokes` and `/defs` species to C++. Generate locally into tiled HISMs (about 28 m tiles). Apply the `keep(d)` law via `InstanceStartCullDistance`/`EndCullDistance` = 15/90 m plus PerInstanceRandom dithering in the material.
   - Cheaper alternative: raise the server cap for native and extend the cull to 90 m.
2. **Pushers:** a material parameter collection with 4 `float4 (x, y, z, r)` (wire→UE converted, r = 110 cm), updated each tick from the local pawn and the 3 nearest peers. Implement the formula above in the WPO of both grass and flora materials. Root position comes from the instance origin (`TransformPosition(0)`).
3. **Wind:** replicate the three layers in WPO with the def constants (custom data already carries `wind`; add `phase`).
4. **Tilt:** apply `tx/tz` as pitch/roll on the instance transform.

**Effort:** M (pushers, distance, wind) / L (local placement and real species meshes).

---

## 7. Weather on surfaces: wetness, puddles, cloud shadows, rain dressing (MEDIUM-HIGH in weather worlds)

**Web behaviour** (`client/lib/materials.js:172–234`, `:482–546`)

**Targets** (derived at 1 Hz):

| Weather | WET | WX_COVER |
|---|---|---|
| clear | 0 | 0 |
| fair | 0 | .18 |
| sunshower | .55 | .35 |
| overcast | .15 | .85 |
| rain | .85 | .9 |
| storm | 1 | .92 |
| cyclone | 1 | .96 |
| darkstorm | 1 | 1 |

- `wetTarget = WET[w]·k`
- `cover = max(CLOUD_COVER[sky.clouds], WX_COVER[w]·k)`
- Wetness eases **toward wet with τ = 10 s and back to dry with τ = 40 s**: `cur += (goal − cur)·min(1, dt/τ)`. Cover eases with τ = 15 s.

**Wetness wrap** (every PBR material: models, terrain, grass):
- `upMask = clamp(normal.y, 0, 1)² · wet`
- `rgb ·= mix(1, 0.68, upMask·0.85)`
- `roughness = mix(rough, 0.10, upMask·0.8)`

**Puddles** (PBR only, not grass, not MToon):
- Mask: `pn = vnoise(xz·0.35 + warp) + 0.6·vnoise(xz·0.09)`, where `warp = (vnoise(xz·.045), vnoise(xz·.045 + (37.7, 11.3))) − .5) · 2.2`.
- `pShape = smoothstep(.97, 1.13, pn) · smoothstep(.985, .998, normal.y) · (1 − smoothstep(160, 450, viewDist))`
- `puddle = smoothstep(.25, .6, pShape) · smoothstep(.35, .9, wet)`
- In puddles: `roughness → 0.045`, `metal → 0`. Colour deepens by `mix(.96, .62, depth)` and is tinted toward `(.84, .91, 1)`.

**Cloud shadows** (PBR):
- `shade = 1 − smoothstep(lo, lo + .22, 0.72·N(p·1/420 + s1) + 0.28·N(p·1/97 + s2)) · strength`
- `p = xz + sunDir.xz·(300/max(.25, sunDir.y))`
- `lo = 1.02 − 0.72·cover`
- `strength = 0.5 · clamp(4·sunDir.y) · (1 − clamp((cover − .8)/.17)) · min(1, 3·cover)`
- Scroll velocities: `s1 += (1.9, .55)·(1 + 2.5·cover)·dt` and `s2 += (−.7, 1.3)·(1 + 2.5·cover)·dt`.
- `N` is a seeded tileable 256² noise texture, so every client sees the same shadows.

**Rain, lightning, audio:** come from the upstream weather system (streaks and splash sprites from `eidoverse/assets/particle_textures`, lightning as adopted lights with intensity up to 12,000, weather audio).

**Data path to native:** present (`atmosphere.weather`, `environment.sky.clouds`).

**Unreal status:** partial.
- `ApplyWeather` only lowers roughness on the structure palette (`.86 − rain·.5`).
- **No model or terrain wetness, no puddles, no cloud shadows.**
- `sky.clouds` is ignored (cover comes from weather only, with different numbers: fair .25, rain/overcast .8, storm .95).
- Rain is 192 cube streaks (`TickWeather` :163–186). No splashes, lightning or thunder.

**Implementation spec:**
- A material parameter collection `MPC_Eidoverse {Wet, CloudLo, CloudStrength, CloudOff, Scroll1, Scroll2}` driven by the table and easing above.
- A material function added to `M_EidoverseSurface`, `M_EidoverseTerrain`, the ground cover material and the glTFRuntime PBR templates (via the MaterialsOverrideMap parents) implementing the wetness, puddle and cloud-shade terms. A light function on the sun is a cheaper alternative for cloud shadows.
- Replace `CloudAmount` with the cover formula.
- Lightning: a brief directional or point flash during storm weathers. Exact parameters are upstream.

**Effort:** M.

---

## 8. Pictures (`picture` component) (MEDIUM)

**Web behaviour** (`client/lib/pictures.js:51–128`; `shared/picture.js:47–96`)

**Data:** `{src, part, look?, lit?: "scene"|"self", flip?: bool}`.

**`src` rules:** library-relative only, under `eidoverse/assets/` or `store/images/`, extension `.png/.jpg/.jpeg/.webp`. Refused if it has any scheme, a leading `/`, a `\`, `%?#`, whitespace or control characters, or an empty, `.` or `..` segment.

**`part`:** 1–64 characters, trimmed.

**Rendering:**
- `findPart(root, part)`. Missing part → warn, nothing hung.
- Clone the part's material (never mutate the shared one).
- `map = image` (sRGB, `flipY = flip === true`; glTF UV convention means no flip by default). Set the base colour to white.
- If `lit === "self"`: emissive white, `emissiveMap = image`, `emissiveIntensity = 1`.
- On removal, restore the original material. Re-hang on promote/spawn.
- A revision counter drops stale loads.

**Data path to native:** missing. Server change: add `"picture"` to `PASS_THROUGH` (or project `normalizePicture`).

**Unreal status:** missing. It needs named parts (#1); merged sections lose node identity.

**Implementation spec:** once #1 provides per-node components:
- Download `/library/<src>` with the existing URL validation and `ImageWrapper`.
- Create a `UMaterialInstanceDynamic` from that node's material and set `baseColorTexture` = image, `baseColorFactor = (1, 1, 1, 1)`.
- For `self`: emissive texture = image, `emissiveFactor = 1`.
- Flip V when `flip`.
- Restore on null.

**Effort:** M (after #1).

---

## 9. World-placed sounds (`sound` component) (MEDIUM)

**Web behaviour** (`client/lib/sounds.js:66–152`; `shared/sound.js:30–96`)

**Data:** `{src, playing = true, loop = true, volume 0–1 (0.8), radius 1–200 (12), t0?, look?}`.

**`src`:** under `eidoverse/assets/` or `store/audio/`; `.mp3/.ogg/.opus/.wav/.webm/.m4a`.

**Audio graph:** `<audio>` → gain(volume) → HRTF panner (`distanceModel "inverse"`, `refDistance 1.5`, `rolloffFactor 1.5`, `maxDistance = radius`) → the "world" bus gain (the listener's world-volume preference) → output.

**Behaviour:**
- **Shared playhead:** with `t0`, seek to `(now − t0)/1000`, modulo duration when looping, else clamped.
- The panner follows the entity's world position each frame. The listener follows the camera.
- An entity missing from the scene means gain 0 (the graph is kept).
- Same `src` → adjust in place. New `src` → rebuild.

**Data path to native:** missing. Server change: add `"sound"` to `PASS_THROUGH`.

**Unreal status:** missing (no audio code in the plugin).

**Implementation spec:**
- A `UAudioComponent` attached to the entity root.
- Decode the downloaded bytes. Built-in runtime decoding covers WAV; use the RuntimeAudioImporter plugin for mp3/ogg.
- Attenuation: Inverse-style falloff with `FalloffDistance = radius·100` and an inner radius of 150 cm. Match `gain = ref/(ref + rolloff·(d − ref))` with ref 1.5 m and rolloff 1.5 by using a custom attenuation curve.
- Seek with `Play(StartTime)` computed from the server clock.
- Multiply by a world-volume setting.

**Effort:** M.

---

## 10. Physics presentation for epoch sim bodies (MEDIUM)

**Web behaviour** (`client/lib/simworld.js:75–212`; `shared/sim.js`)

**Local evaluation:** the client evaluates the deterministic sim locally (`advanceSim` to `tickOf(now)`).

**Interpolation:** it shows the lerp between the previous and current tick positions at `k = (wall − epoch.ts)/tickMs − (tick − 1)`, with no extrapolation and no added latency.

**Cosmetic tumble** (never streamed):
- **Airborne** (`v.y ≠ 0`, `|v.xz| > .05`, visual-centre arm < 0.5 m): rotate about `normalize(v.z, 0, −v.x)` at **2.2 rad/s**.
- **Otherwise:** slerp at rate `min(1, 6dt)` toward upright-at-yaw, leaned onto the terrain normal.
  - The normal comes from central differences with `e = 0.5 m` at the collider-box visual centre `(cx, cz)` rotated by yaw.
  - The lean applies only when the body is on terrain (not `b.on`) and resting or with `v.y == 0`.
- **Composition about the visual centre:** `pos = simPos + (cx, 0, cz)·yawRot − q·(cx, 0, cz)`, plus `lift = height(centre) − height(origin)`.

**Data path to native:** partial.
- render-scene sends `p` and a yaw-only `q`, sampled at about 15 Hz (`updateIntervalMs 66`, `render-scene.ts:146–150, 163`).
- `v`, `resting` and `on` are not sent.
- Server change: for sim bodies, add `sim: {v: b.v, resting: b.resting, on: !!b.on, tick: sim.tick, tickMs: sim.epoch.tickMs}` to the transform.

**Unreal status:** partial.
- Exponential blend toward the sampled target, `1 − e^(−14dt)` (`UE: EidoverseScene.cpp:Tick` :201), so motion lags behind.
- No tumble or slope tilt (also noted in PARITY.md).

**Implementation spec:**
- With `v`: between samples, extrapolate `p + v·Δt` with gravity −9.8 when `v.y ≠ 0`, clamped to the terrain. Or interpolate between the last two samples.
- Implement the tumble/settle above using the actor's bounds centre.
- Full parity would mean porting `shared/sim.js` (563 lines) plus `simmath.js`. Bit-exactness is not required for presentation.

**Effort:** M (server field plus presentation) / L (sim port).

---

## 11. Residency/streaming and placeholders (MEDIUM in large worlds)

**Web behaviour** (`client/lib/realize/models.js:66–100`, `:704–822`)

**Placeholders:** a translucent box (`0x8a9099`, opacity .22, no depth write) sized and centred from the `geom` side-channel (`{lib: {bbox: {center, size}}}`) until the GLB loads.

**Residency:**
- Promote when distance < R and demote when distance > R + 20, where `R = 80 + 4·bboxDiagonal` (12 m diagonal when unknown).
- Distance is `min(camera, avatar)`. The sweep runs at 2 Hz.
- An entity is never demoted if it is a light, mounted, a carrier, has a rider, is under an edit hold, has part motions or part sockets, or has no bbox.
- Retry after 30 s on failure.
- Load priority is by distance.
- LOD tier negotiation is KTX2-only (browser).

**Data path to native:** present. The server already sends `{type: "geom", geom}` to every client after the snapshot (`server/server.ts:345–361`). The Unreal bridge ignores it (`UE: EidoverseBridge.cpp:Receive` has no `geom` case).

**Unreal status:** partial.
- The placeholder is a fixed 25 cm "trim" cube at the root (`UE: EidoverseScene.cpp:149`).
- Loads are nearest-first, up to 4 concurrent, at most 2 appearing per frame.
- **No demotion.** There are hard caps of 512 entities and 64 assets.

**Implementation spec:**
- Handle `geom` and scale/offset the placeholder to the bbox. Use a translucent material with no collision.
- Add the R/hysteresis sweep:
  - Demote: unload the static mesh reference and return to the placeholder.
  - Promote: queue the load.
- Respect the never-demote rules above.

**Effort:** M.

---

## 12. Collision policy (LOW-MEDIUM)

**Web behaviour** (`client/lib/colliders.js:34–58` `buildExact`, `:235–303` `decide`, `:311`; `realize/models_field.js:107–109`)

**Default (auto):**
- An oriented box from the local AABB.
- **Tall slim things** (box height > 2.4 m and not exact) become a centre "pillar" you can walk under.
- **Exact trimesh** only for room-scale (`supportclass.js` thresholds) or "uneven floor-shaped" objects (footprint ≥ 2 m², lie > 0.10 m, height ≤ 1.0 m).

**Overrides and exclusions:**
- `collide: "exact" | "box"` forces the mode.
- **`comp.collision.enabled === false`, `comp.structure`, or being mounted → no collider.**
- Nodes with glTF `extras.collision === false` (non-solid panes, pools) are excluded from exact geometry.
- Lod-tier placements own no collider.

**Data path to native:**
- `collide` is sent, but the server defaults it to `"exact"`, so authored-exact and auto cannot be told apart.
- `data.collision` is passed through.
- glTF extras are in the GLB.

**Unreal status:** partial.
- Every model uses `CTF_UseComplexAsSimple` (full trimesh; `UE: EidoverseAvatar.cpp:StaticMeshReady` :410).
- Only `collide == "none"` disables collision (`Apply` :129).
- **`data.collision.enabled` is ignored. `extras.collision` is ignored.**

**Implementation spec:**
- In `Apply`, read `data.collision.enabled == false` → `NoCollision`.
- At import, skip nodes whose extras have `collision: false` when cooking. This needs per-node meshes (#1) or a second collision-only mesh.
- Optionally implement pillar/box for auto. Server tweak: send `collide: e.collide ?? null` so "auto" is distinguishable.

**Effort:** S (component) / M (extras).

---

## 13. Structures (LOW)

**Web behaviour** (`client/lib/realize/structure.js:65–151`, `:161`, `:203–260`)

**Palette slots:**

| Slot | Colour | Roughness | Finish | Other |
|---|---|---|---|---|
| wall | 0xe4ded1 | .94 | plaster | |
| floor | 0xa8814f | .80 | boards | |
| trim | 0xf2efe8 | .62 | painted | |
| roof | 0x6d6257 | .90 | plaster | |
| glass | 0xcfe2e8 | .06 | — | opacity .22, two-sided |

The palette is overlaid from `/defs` `structure/_palette.json`.

**Procedural finishes:**
- **plaster:** `0.93 + 0.10·fbm(p·.55) + 0.05·fbm(p·6.5)`
- **boards:** planks 0.185 m along X, groove `smoothstep(.8, 1, seam)·.3`, per-plank tone ±0.2
- **painted:** `0.96 + 0.06·fbm(p·3)`

**Ambient-occlusion/daylight shading baked into vertex colours:** `shared/structure.js:844` `makeShader`, using an openness field, wall-corner falloffs, floor and ceiling contact, eave shade and floor bounce. Roof "cutaway" applies in build mode.

**Data path to native:**
- parts and sweeps: present
- level, grid and `level.tiles/walls` needed for the shading bake: not sent
- palette: available via `/defs`

**Unreal status:** mostly present.
- `UE: EidoverseScene.cpp:BuildStructure`/`Material` (:61–95) uses the same slot rule and colours, with collision boxes.
- No palette defs, no finish patterns (it uses world-space detail), no shading bake. Lumen GI largely substitutes for the bake.

**Effort:** S (palette from `/defs`) / M (send levels plus the shading port).

---

## 14. Terrain (LOW)

**Web behaviour:**
- Height law from `shared/terrainmath.js` (`size 80`, `amplitude 3`, `seed 1`, `octaves 4`, `frequency 2.5`, `flatRadius/flatHeight`), meshed by the upstream `makeTerrain`.
- `layers: [{color, repeat 16}]` become value-noise textures (`client/lib/world.js:112–125`).
- Wetness, cloud shade and shadow receiving apply.
- Ground-level entities (|y| < 0.02) are re-seated on the terrain.

**Data path to native:** present: 65²–129² heights and the first layer colour (`render-scene.ts:23–33`). Extra layers and `repeat` are not sent.

**Unreal status:** present (`UE: EidoverseScene.cpp:ApplyEnvironment`). Second and later layers are ignored, and there is no terrain LOD beyond 128 segments.

**Effort:** S (add `layers` to the environment).

---

## 15. Water, air and environment components (present)

**Web behaviour:** `client/lib/water.js` and `shared/water.js`.
- Up to 4 water boxes (`MAX_WATER`), each with its own analytic Gerstner-like surface and per-volume ray-segment fog. Higher stable-id volumes take priority.
- Up to 16 dry `air` boxes are subtracted.
- An environment floor applies.

**Data path to native:** present.

**Unreal status:** present via UnderwaterOptics (`UE: EidoverseSceneWater.cpp`). It renders a **single ocean surface** with no finite-volume clipping (:334–335).

**Effort:** M for multiple, clipped surfaces.

---

## 16. Whole-entity closed-form motion (present)

**Web:** `motioneval.js:169–219` (pendulum, spin, orbit, bob, path; the server clock is used for `t0`).

**Unreal:** present.
- `UE: EidoverseMotion.cpp:EvalWholeMotion` is a direct port with a conformance test (`EidoverseMotionTests.cpp`).
- `EidoverseScene.cpp:LocalTransform` evaluates it at frame rate.
- Unknown types fall back to the sampled transform (web shows rest; equivalent).

---

## 17. Whole-entity mounts / cargo (present)

**Web:** `realize/models.js:595–642`.

**Unreal:** present (`LocalTransform` mount branch, `EidoverseScene.cpp:235–247`). Part sockets are missing (#1).

---

## 18. Day/night lighting curve (LOW-MEDIUM)

**Web behaviour:**
- On Skye's sky the upstream sky drives the sun and hemisphere.
- On the fallback path (`sky.js:743–781`):
  - `day = max(0, sin((h − 6)/12·π))`
  - sun elevation `−8 + 68·day` degrees, azimuth `sky.azimuth ?? 180`
  - `sun = (0.4 + 2.2·day)·sun`, `hemi = (0.6 + 0.4·day)·ambient`
  - colour lerps `0xfff2e0 → 0xffab5e` by warmth
- **Nights are not black** (sun never below 0.4, hemisphere 0.6).

**Unreal:** `ApplyWeather` uses elevation `sin((h − 6)π/12)·75°`, azimuth `(h − 12)·15 − 35`, sun `3.2·Day` (0 at night) and sky fill `.06–.8`. Nights are much darker; no moon or stars.

**Spec:** add a moonlight floor (directional, about 0.15–0.4 intensity, cool colour) and a sky-light minimum near 0.2 to approach web night visibility. Honour `azimuth`.

**Effort:** S.

---

## 19. glTF features

**Embedded animations (birds, tank, …):** web does **not** play GLB animation clips for world models. `AnimationMixer`/`clipAction` appear only in `avatar.js`, and `loadGLB` keeps a `skeletonClone` at rest (`assets.js:552–611`). **Parity: neither client animates them.** Motion comes only from motion components.
- Risk: Unreal's merged static import ignores skins. If a skinned GLB's rest pose differs from its bind pose, the two clients would show different shapes. Verify with the bird and tank assets.

**KHR material extensions:** the web uses three's GLTFLoader (transmission, ior, volume, specular, clearcoat, sheen, iridescence, emissive_strength, texture_transform, unlit). Transmissive materials are upgraded to `MeshPhysicalNodeMaterial`, `transparent = true`, with sharing preserved (`materials.js:275–330`). The glTFRuntime source references all of these extensions. Status: likely present; **verify transmission and texture transform** with a test asset.
- Unreal routes unlit/MToon to lit templates and translucent PBR to `MI_AvatarPBR_*` (`UE: EidoverseAvatar.cpp:PrepareLitMaterials/MaterialConfig` :98–158).

**Alpha modes:** present (Opaque/Mask/Blend × two-sided templates).

**Compression:** web decodes Draco, meshopt and KTX2/WebP. Native requests the original bytes with `?native=1` and refuses `KHR_draco_mesh_compression`, `EXT_texture_webp` and `KHR_texture_basisu`, falling back to a placeholder (`ValidateGlb` :186). OK as long as the store keeps originals.

**`KHR_lights_punctual` in GLBs:** three instantiates these lights. Native ignores them. This is an edge case and low priority.

---

## 20. Shadow policy (divergent)

**Web:**
- Sun shadows follow the camera (±46 m box, 90 m depth, 1024–4096 maps).
- Only the 12 nearest models cast (`setCasterBudget`; `lightrig.js:735–785`). Grass never casts.
- Point-light shadows only on slot 0, for avatar lamps.

**Unreal:** virtual shadow maps for everything, all placed lights cast, ground cover doesn't.

**Spec:** turn off placed-light shadows (#4). The rest is an acceptable quality difference.

**Effort:** S.

---

## 21. Browser-only or not applicable

- **`sky_baked.js`, the cloud-quality tiers in `sky.js`:** a WebGPU workaround that bakes the volumetric march into a dome. Not applicable; use Unreal's VolumetricCloud if real clouds are wanted.
- **`render.js` (renderAside, XR), `draw_batches.js` (UBO instancing), `warmqueue`:** WebGPU pipeline and compile management.
- **`governor.js`, `perf.js`:** browser frame governor. Unreal has its own scalability system.
- **`lod_policy.js`:** KTX2 LOD-tier negotiation. Native negotiates originals.
- **`grass_quality.js`:** a local dial (full 1 / medium .6 / low .35 / off 0). It could become an Unreal setting multiplying the grass instance count (S).
- **`frames.js`:** UI window chrome. **`frame.js`:** the frame-loop system registry. **`domquad.js`/`worldquad.js`:** XR panel rasterisation. These are UI/XR, not world rendering.
- **`mods.js`:** trusted in-page ES modules and client-runtime behaviours. Not portable to native (JavaScript); server-side behaviours still act through verbs.
- **`conjure.js`:** the Orrery generation panel. Its only rendering side is `holdGhost`, which Unreal already has (`UE: EidoverseScene.h:HoldGhost`).
- **`landmarks.js`:** avatar contact surface ray-casts. Avatar/reach area, not world.
- **`scenegraph.js`:** the inspector/scene tree UI. The UI area; Unreal has `EidoverseUIWorld.cpp`.

**Component names asked about that don't exist in this codebase:**
- `emitter` → it is `particles`.
- `light` → a `light` verb/entity kind.
- `frame`, `parts`, `panel`, `physics`, `flora` → no consumers in the web client. render-scene only warns about them.
- `vehicle` and `traversal` are passed through to native; they belong to the locomotion area.

---

## Server changes in one place (`server/render-scene.ts`)

1. **Pass-through components:** add `"particles"`, `"picture"`, `"sound"` to `PASS_THROUGH` (:36), or project the shared normalizers.
2. **Part motions:** add `data.parts` = every `motion:*` key plus `motion` with `part`, with `t0` resolved as in `localMotion`. Make `localMount` emit part sockets instead of returning null, and never drop the transform of entities mounted on part sockets.
3. **Light fields:** in `projectEntity`, add `keep` and `day` for `kind === "light"`.
4. **Sim bodies:** add `sim: {v, resting, on, tick, tickMs}` to their transforms.
5. **Optional:**
   - `collide: e.collide ?? null`
   - `terrain.layers`
   - structure `levels/grid` for the shading bake
   - raise or remove the 12k flora cap for native, or let native place locally from `environment.grass`

**Unreal client-side note:** any change to `revision` makes `UE: EidoverseScene.cpp:Apply` (:126) **destroy and respawn the actor**. Once component data such as sound volume or picture flows through render-scene, update `data` in place when only data or components changed, so those edits don't make the model flicker or reload.