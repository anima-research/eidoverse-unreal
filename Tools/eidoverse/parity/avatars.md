# Avatars, bodies and locomotion: web client vs Unreal client

Web root: `~/connectome-local/eidoverse-worlds-native/client` (web references are `lib/<file>:<line>` unless marked otherwise). Unreal root: `Plugins/Eidoverse/Source/EidoverseClient/Private/`. Server shapes come from `server/messages.ts` and `server/posecheck.ts`. I edited no files.

## Already-implemented items (quick check)

These are present and match the web closely enough. Small gaps are noted inline.

- **VRM load:** `EidoverseAvatar.cpp`.
- **Clip retargeting and slot rules:** `EidoverseClips.cpp` (`EidoClipRules::FileForSlot` / `Fallback` / `RateFor` / `SelectLocomotion`) and `EidoverseAvatarClips.cpp` (`AnimateClips`).
- **Emotes as one-shot clips:** `UEidoverseAvatarMesh::PlayOneShot` and `AnimateWithClips`. The emote cancels when speed > 0.05, the same as web.
- **Phase sync:** `SyncClipPhase` and `SendPose` (it sends `clipTime` / `clipTimeSlot` / `clipRate` only while no one-shot is playing, same as web `net.js:244`).
- **Spring bones:** `EidoverseSprings.cpp`.
- **Head pitch:** `SelfHeadPitch` uses `clamp(camPitch-0.32, -0.45, 0.55)`, and is 0 in first person.
- **Held `pose`:** `SetHeldPose` / `ApplyNormalizedLayers` / `ApplyRawLayers`, eased at a rate of 12/s.
- **`anim`:** `ReceiveAgentAnimation` / `PlayAgentAnimation`.
- **Look-at, blink, chat mouth (`aa` envelope), expressions:** `EidoverseAvatarLayers.cpp`.
- **Seating:** mount/dismount, furniture `sitchair`, ground sit (`EidoverseSeats.cpp` `TickSeating` / `TryMountNearest` / `TrySitOnFurniture` / `StandUp`).
- **Reach:** `EidoverseReach.cpp`. This has real gaps; see section 11.
- **Swimming:** `AEidoverseExplorer::TickSwimming`.
- **Follow camera and first person:** `UpdateCamera` / `SetCameraDistance`. The 0.4 / 0.7 m hysteresis matches web.

## A likely cross-client bug to check first

In `AEidoverseBridge::Pose`, `bPawnAnchor = body=="diver"||"submarine"`, and `TickAvatars` then lowers the mesh by 85 cm.

The web sends `locomotion.body:'diver'` with **feet** coordinates in three cases:
- swimming (`controller.js:571`)
- climbing a route (`controller.js:603`)
- piloting a vehicle (`vehicles.js:69`)

Unreal's own swimmer also sends feet coordinates with body `diver` (`SendPose` subtracts 88 cm, then sets body to "diver").

So, as far as I can tell from the code, every swimmer, climber or pilot is drawn about 85 cm too low on Unreal. The offset only suits the legacy ocean-diver pawn, which sends its centre point. I haven't checked this in a running client. A possible fix is to apply the −85 cm only when the sender is not a feet-anchored humanoid, e.g. only for `submarine` or when the mode is a non-explorer pawn.

---

## Features, ordered by user-visible impact

### 1. Remote limp/ragdoll rendering (`clip:'ragdoll'` plus a streamed pose)
- **Web behaviour:**
  - A ragdolling owner sends `clip:'ragdoll'` and a sparse `pose` every 66 ms. The pose is `{hips,spine,chest,neck,leftUpperArm,leftLowerArm,rightUpperArm,rightLowerArm,leftUpperLeg,leftLowerLeg,rightUpperLeg,rightLowerLeg: [x,y,z,w]}`, rounded to 4 decimals. These are DRIVEN_BONES (`rigmeasure.js` SEGMENTS), as normalized-bone local quaternions. `p` is the root, which follows the hips.
  - On a receiver, clip `'ragdoll'` calls `avatar.setLimp(true)` (`remotes.js:332`, `avatar.js:1528`), which does the following:
    - cancels the emote and drops all `anim` layers;
    - parks every humanoid bone **not** in DRIVEN_BONES at identity (rest) **every frame, after the mixer** (`_park`, `avatar.js:1925`). That includes head, upperChest, shoulders, hands, feet, fingers and eyes;
    - makes the held pose apply at weight 1 immediately (`setPose`: `weight = _limp ? 1 : 0`, `avatar.js:1262`);
    - skips head pitch and clears reaches;
    - eases the eyes shut (`setEyes(true)`; the lid amount eases at 12/s);
    - sets limp springs (section 17);
    - stops the wing flap.
  - On un-limp it restores the parked rotations, slerps the wings back over 0.5 s and re-combs the hair.
  - **Inter-sample pose blending** (`remotes.js:235-309`):
    - For each bone it slerps the quaternion from sample a to sample b with the same k as root interpolation; `t` and `s` are lerped.
    - A bone present in only one sample holds that value; it does not blend toward rest.
    - With a single sample, it applies that sample directly.
- **Unreal status: Partial.**
  - `TickAvatars` maps `ragdoll` to walk/idle and plays that clip. The held pose then eases in at 12/s over a still-animating clip, and non-driven bones keep animating.
  - `Pose()` only swaps `HeldPose` when its JSON key changes, so it steps at 15 Hz with no blending between samples.
- **Spec:**
  - In `FEidoverseSample`, store each sample's `pose` object.
  - In `Tick`'s interpolation (where alpha is computed), build a blended pose per the rules above and push it with a new `SetHeldPoseImmediate(map, weight)`.
  - Add `UEidoverseAvatarMesh::SetLimp(bool)`:
    - stop clip weights (or keep sampling but skip writing normalized rotations);
    - force `HeldWeight = 1`;
    - in `ApplyNormalizedLayers`, set every non-DRIVEN humanoid rotation to identity;
    - use the rest hips translation (ignore the clip's hips track);
    - skip `HeadPitch`, reach, gestures and gaze-driven eyes;
    - drive lids shut (section 17).
  - Exit limp when the newest sample's clip is not `'ragdoll'`.
- **Effort:** M. **Note:** this alone makes web users' falls, drags and poses look right on Unreal. It needs no physics.

### 2. Puppet requests (pose / anim / shove) and consent
- **Web behaviour:**
  - **Incoming message:** `{type:'puppet', by, pose|null, merge:bool, anim|null, ragdoll: true|{lean:[x,y,z]}|null}` (`server/messages.ts:220-243`).
  - **Handler** (`localbody.js:365-396`):
    - **`ragdoll` set:** if not `pushable()` (default **ON**, localStorage `ew-pushable`), show the toast "`<by>` tried to knock you over — /pushable on to allow" and stop. Otherwise call `applyShove(lean)` (section 3).
    - **Else, if not `posable()`** (default **OFF**, `ew-posable`): toast and stop.
    - **`pose`:**
      - `merge:true` → `mergePose(current, pose)`: a null value deletes that bone, and an empty result becomes null (`shared/humanoid.js:161`).
      - Otherwise, an empty object means release (null); anything else replaces the current pose.
      - The result becomes my held pose, which rides `pose.pose` on every presence packet. Hint text: "`<by>` posed you" or "… released your pose".
    - **`anim`:**
      - `replace` defaults to true; when it's true, also clear my held pose.
      - Play locally, then re-broadcast as `{type:'anim', dur, tracks, loop, replace}` (`net.js:105`).
  - **Outgoing:** `{type:'puppet', target, pose, merge?, anim, ragdoll}` (`net.js:113`).
    - `/push [name] [power]`: power defaults to 2.2, clamped 0.5–4. Range is 2.5 m horizontal; with no name it picks the nearest. It sends `ragdoll:{lean: unitDir(me→them)*power, y=0}`; at distance ≤ 0.05 it uses my facing (`commands/handlers.js:160-202`).
    - `/punt <person>`: power defaults to 2.6.
    - `/pushable on|off` toggles the consent.
  - The held pose ends when my speed reaches ≥ 0.05 (walking away), per `controller.js:780`.
- **Unreal status: Missing.** `Receive` ignores `puppet`. `/help` says "Puppet/body-drag requests are not auto-accepted."
- **Spec:**
  - Add a `puppet` case: two consent settings, `SelfHeldPose` state, and add `pose` (and `pose:null` once on release) to `SendPose`.
  - Apply the self held pose to `SelfBody.AvatarMesh`.
  - Clear it when moving at ≥ 0.05 m/s.
  - Shoves need section 3. Until that exists, refuse with a toast.
- **Effort:** S for pose/anim, plus section 3 for shoves.

### 3. Local ragdoll: go limp, shoves, `force` blasts, getting up
- **Web behaviour:**
  - **R** toggles limp (when not in build mode). Any of W/A/S/D, the arrow keys or Space gets you up when downed (`main.js:435-437`).
  - **goLimp** (`localbody.js:53`):
    - default lean = facing × (0.9 + min(1.2, speed × 0.35)) m/s;
    - clear my reach, `setLimp(true)`, create a sim from rest positions;
    - set clip `'ragdoll'` and the hint "limp — move to get up".
  - **Each frame while downed** (`main.js:538`, `stepRagdoll`):
    - controller input is ignored; `q` and `locomotion` are undefined;
    - `pose = sim.step(dt)` goes to `myState.pose`, and `pos` = avatar root;
    - when the sim is done, `pose = finalPose` and the body stays downed with clip `'ragdoll'`;
    - the camera keeps following.
  - **The sim contract** (`bodyengine.js`, `ragdoll.js:1098`):
    - fixed step of 1/60 s, at most 4 steps per frame;
    - the pose is normalized local quaternions of DRIVEN_BONES;
    - **root follow:** root.xz = hips − (rest hips offset rotated by root yaw); root.y = min(startY, hips.y − hipsOffset), allowed to rise only while pinned;
    - **settle:** Verlet `maxV < 0.06 m/s` for 0.4 s (the clock cancels when `maxV > 0.18`), deadline 8 s; Bullet engine 0.07 m/s and 0.6 rad/s for 0.45 s (`ammodoll.js:216`);
    - `impulse(v)` caps at 8 m/s and resets the settle clock and deadline;
    - topple: each joint's velocity += lean × (y − lowest)/span, so the feet stay and the head gets the full lean;
    - Verlet constants: masses in `ragdoll.js:94`, gravity −9.8, 2 substeps × 3 iterations;
    - snapshot `{j:[names], p:[x,y,z…], v:[…], h?}`.
  - **applyShove** (`localbody.js:317`):
    - ignored while mounted or dragged;
    - lean capped at 6 m/s;
    - downed with a running sim → `impulse`; downed and settled → new sim from the lying pose plus pins; standing → goLimp;
    - hint "`<by>` knocked you over".
  - **Force verb** (`localbody.js:403`, `shared/force.js`): a **live** log entry `verb:'force'`, `args {at:[x,y,z], radius=4, power=3}`, honoured only when `pushable`.
    - d = horizontal distance; outside the radius → nothing.
    - mag = min(6, power × (1 − d/r)). If mag < 0.3 and no sim is running, it's a breeze: ignore.
    - Direction = (dx,dz)/d when d > 0.05; otherwise lean = facing × max(mag, 0.5).
    - Never applied on replay.
    - Sent with `/boom [power] [radius]` as `verb force {at: my feet, power?, radius?}` (builder-gated by the server).
  - **getUp** (`localbody.js:67`):
    - revoke any drag and clear all pins;
    - send `pose:null`, `setLimp(false)`, clip idle;
    - pos = root with y = 0; gravity then resolves the height.
- **Unreal status: Missing.**
- **Spec:** two options.
  - (a) Port Verlet `ragdoll.js` (about 1,200 lines of pure math). Replace `heightAt` / `resolveColliders` with line or sphere traces. Output the same quaternion map.
  - (b) Build a Chaos ragdoll on a hidden `USkeletalMeshComponent` that uses a runtime-generated physics asset: capsules on SEGMENTS, masses from `MASS`, cone and hinge limits from `shared/joints.js`. Each frame, read back the body rotations and convert them to normalized local quaternions with the inverse of the retarget used in `AnimateClips` (raw → `P·raw·rest⁻¹·P⁻¹`).
  - Either way, stream clip `'ragdoll'` + `pose` + root `p`.
- **Effort:** L.

### 4. Remote seated bodies and moving-seat rides
- **Web behaviour:**
  - Mounts are **folded state**, not presence. `snapshot.state.mounts = {riderId: {to, slot?, offset?, yaw?}}`. Log `mount` / `dismount` (`args.id`) add or remove entries, and deleting the parent entity unmounts its riders (`shared/fold.js:305,455-482`).
  - **`mountTransform`** (`world.js:380`):
    - pos = parent.matrixWorld × (`offset ?? sock.pos ?? [0,0.5,0]`);
    - if `sock.part` names a motion-animated part, the point is carried from the part's rest pose to its live transform (a swing seat rides the plank);
    - then the profiled seat correction is applied (section 13);
    - yaw = parentWorldYaw + (`mount.yaw ?? sock.yaw ?? 0`); pose = `sock.pose ?? 'sitchair'`;
    - mounted bodies are upright, yaw only.
  - **Remote mounted bodies** (`remotes.js:377-417`) are placed at the mount transform every frame, ignoring the streamed `p`. They play `setClip(sock.pose)` with phase sync, and still apply the held pose, emotes, reach, XR and wing fold from the newest sample. When the profile doesn't apply, the nameplate shows "name ≈".
- **Unreal status: Partial.**
  - Self only (`MountedSeatTransform`, from my own log entries).
  - Ignores `sock.part`, `mount.yaw` and `snapshot.state.mounts`, so a reconnect loses your own seat.
  - Remote riders follow the 15 Hz stream about 110 ms late, so they visibly lag a swinging seat.
- **Spec:**
  - Keep `TMap<FString, FMount>` filled from `snapshot.state.mounts` plus every mount/dismount log entry.
  - In the peer interpolation loop, if mounted and the parent actor exists, override the actor transform with the socket world transform.
  - Add part riding: find the named part component and apply the part's live transform × inverse of its rest transform. Unreal's motion system (`EidoverseMotion.cpp`) already tracks `part`.
  - Use `mount.yaw` when it's present.
- **Effort:** M.

### 5. Wings: idle flap, folded posture, power stroke
- **Web behaviour:**
  - **Which bones:** any rig with bones matching `^([LR])_Wing_(Upper|Lower)(?:_(\d+))?$` (`avatar.js:783`). Depth is the numeric suffix (0 when absent). side L = +1, R = −1.
  - **Rest:** each wing bone's rest is its spring joint's initial local rotation if it is a spring joint, otherwise its load rotation. Every frame rebuilds from rest, so nothing integrates.
  - **Per frame `_flap(dt)`** (`avatar.js:838`), run after the spring bones:
    - **Effort:** `e += (want − e)(1 − exp(−dt/τ))`, with τ = 0.18 rising and 0.55 falling.
    - **Fold:** `fold → wingsFolded ? 1 : 0` with τ = 0.45.
    - **Dials:** `W = mix(WING_IDLE, WING_POWER, e)`.
      - WING_IDLE: deg 10, hz 1/3.4, bias 0, tip 0.9, lag 0.25, sync true, recover 0.5, sweep 18, sweepPhase 0.25.
      - WING_POWER: deg 8, hz 2.1, bias −8, tip 0.78, lag 0.09, recover 0.55, sweep 17, sweepPhase 0.25.
    - **Phase:** `wingT = frac(wingT + dt·hz)`; `ph = wingT − lag·depth − (sync || !lower ? 0 : 0.5)`.
    - **Flap rotation:** `ang = (deg·tip^depth·sin(2πph) + bias)·side` degrees, about the world forward (+Z) of upperChest, falling back to chest, then spine.
    - **Sweep rotation:** `sw = sweep·tip^depth·sin(2π(ph + sweepPhase))·(−side)` degrees, about that bone's world up.
    - **Combine:** `R = Rsweep·Rflap`; `local = parentWorld⁻¹·R·parentWorld·rest`.
    - **Folding:** `local = slerp(local, rest·WING_FOLDED[name], fold)`. The 11 quaternions are at `avatar.js:201-213`; they are glTF local [x,y,z,w].
    - **After un-limp:** slerp from the saved quaternion over `W.recover` seconds.
    - Not applied while limp.
  - **Wire:** `wingsFolded: bool` rides **every** pose (`shared/wingpresence.js`). The receiver applies it only when it's a boolean. **G** toggles the fold; it needs ≥ 2 chains and ≥ 4 wing bones, except to release an existing fold. It's refused while flying. The restore on join reads `wingsFolded`.
- **Unreal status: Missing.** Mythos-style rigs have rigid wings, or wings left to the spring bones.
- **Spec:** add a raw-bone layer after `UpdateSprings` that implements the above in component space, converting the glTF quaternions with `LayerToBody` the way raw held-pose bones are converted. Add `wingsFolded` to send and receive, plus the G key.
- **Effort:** M.

### 6. Human flight (F)
- **Web behaviour:**
  - **Grant:** `yourRights.fly === true` (from the snapshot `yourRights` and live `your-rights`) **and** a rig profile with core bones (hips, a spine/chest bone, head) plus a wing rig (≥ 2 chains, ≥ 4 wing bones). It is re-checked every frame, and a revoked grant calls `bodyDown` (leaf descent). See `shared/flightcap.js`, `flightbody.js`, and `controller.js:183-237,642-738`.
  - **F toggles:**
    - refused while wings are folded;
    - `takeOff(cfg, state, {groundY: collider-resolved ground under the feet})` gives phase PILOT, sets pos.y = ground + 0.15 + 0.6, and applies a 9 m/s launch shaped as `spool = 1 − exp(−t/0.55)` × `fade = exp(−max(0, t − 1.2)/1.4)`;
    - yaw conversion: `flightYaw = π/2 − worldYaw`.
  - **Input** (`shared/flightpilot.js`):
    - W = nose down, S = nose up (1.5 rad/s, max 0.7);
    - A/D = bank (2.6 rad/s, max 1.15, returns to level at 1.1 rad/s);
    - yawRate = 1.5 × bank;
    - Space = flap (+2.2 m/s climb, −2 stamina/s);
    - Shift = spoil (+2.5 m/s sink).
  - **Physics:** port `shared/flight.js` (955 lines, pure): glide polar, stamina, soft ceiling at 60 m, radius 110 m, ground contact (LANDED, or RAGDOLL from the leaf).
  - **Clip:** `wings=='LIMP'` → `'ragdoll'`; otherwise `flap ? 'fly' : 'soar'`. `wingEffort = flap ? 1 : min(1, launchNow/(9·0.55))` drives section 5.
  - **Landing:** on LANDED/GROUND/RAGDOLL, clip idle and speed 0. If the flight ended before 1.5 s, show a hint with the numbers. Swimming cancels flight. `/flight` prints a diagnostic.
  - **Wire:** no new fields; `p` / `yaw` / `speed` / `clip` only.
- **Unreal status: Missing.** The clip mapping for fly/soar already exists. `ReadRights` doesn't read `fly`.
- **Effort:** L. Most of it is a faithful port of four shared JS files.

### 7. Climb routes, `traversal` component (E)
- **Web behaviour** (`traversal.js`, `controller.js:313-316,598-609`):
  - **Data:** entity comp `traversal.routes`, at most 32 used. Each route is `{path:[[x,y,z]×2..16] in entity-local metres, radius=1.4 clamped 0.3–3, speed=1.3 clamped 0.3–4, name='ladder'}`.
  - **Finding a route:** each end of each route is a candidate (`path[0]` forward, `path[last]` reverse). Pick the nearest with world distance to my feet ≤ radius.
  - **Hint:** "E — climb up/down `<name>`" when the candidate changes.
  - **E (not spectating or photo mode, not seated, not piloting):**
    - if climbing → cancel (you drop under gravity);
    - else start: reverse the path if needed, then prepend my position converted to entity-local.
  - **Each frame:**
    - travel = clamp(dt, 0, 0.1) × speed along the segments;
    - points are converted to world through the entity's **current** matrix, so moving carriers work;
    - facing = (last − path[1]) transformed to world, negated when reversed; yaw = atan2(x, z);
    - finishing the last segment ends the climb.
  - **While climbing:** clip `'climb'`, speed = route speed, `locomotion:{mode:'climb', medium:'air', body:'diver'}`, `q` undefined. Flight, mantle, posture, seat and swim are all cancelled.
  - Cancelled on world reset, disconnect, or the comp disappearing.
- **Unreal status: Missing.** Note that Unreal binds **E** to "use aimed object" (`TickInteractions`). Web priority order is traversal, then vehicle helm (`e.worldHandled`), then other E handlers.
- **Effort:** M.

### 8. Mantle (Space at a ledge)
- **Web behaviour** (`controller.js:747-763`, `colliders.js:819`):
  - `resolveColliders` records `blockedTop` = the top Y of a non-pillar box the standing body (tall 1.9) was pushed out of this frame.
  - **On Space while grounded:** reach = blockedTop − feetY.
    - If 0.3 < reach ≤ 1.7: mantle from = pos, to = pos + facing × 0.6 with y = blockedTop. Advance t += dt/0.55, `e = k²(3 − 2k)`, pos = lerp(from, to, e). Not grounded during it; grounded at k ≥ 1.
    - Otherwise: jump with vy 5.6.
  - Clip is `'climb'` (selectClip `mantle → 'climb'`). Step-up without a mantle is 0.55 m.
- **Unreal status: Missing.** CharacterMovement only steps up (default 45 cm).
- **Spec:** on Space, sweep the capsule forward 0.4 m at chest height. On a hit, trace down from hit + forward 0.3 m starting 1.8 m above the feet to find the top. If 0.3–1.7 m above the feet, run the timed lerp above with movement disabled.
- **Effort:** M.

### 9. Vehicles / helm (E on `vehicle` comp)
- **Web behaviour** (`vehicles.js`):
  - **Comp:** `vehicle: {helm:[x,y,z] local, exit?:[x,y,z] local, speed=4 (0.1–15), turnRate=0.45 (0.1–2), minY?, maxY?, probes?:[[x,y,z]…] local, default [[0,0,0]]}`.
  - **Claiming:** E within 2.5 m of the helm's world position sends `lease claim`.
    - `granted` → piloting, hint "piloting — W/S thrust, A/D steer, Space rise, C dive, E exit".
    - `denied` → `why` or "helm occupied".
  - **Each frame (dt ≤ 0.1):**
    - rotation.y += (A − D) × turnRate × dt;
    - desired = normalize(0, (Space − C/Ctrl) × 0.5, W − S) × speed, rotated by yaw;
    - vel = lerp(vel, desired, 1 − exp(−1.5 dt)); pos += vel·dt;
    - apply movement bounds, then clamp to minY / maxY;
    - for each probe (rotated by yaw, from the old position), raycast along the motion for distance + 0.3, ignoring own collider. On a hit, revert and zero the velocity.
  - **Me while piloting:** at the helm world position, yaw = vessel yaw, `q` = vessel quaternion, clip `'sitchair'`, speed = |vel|, `locomotion {mode:'pilot', medium:'air', body:'diver'}`.
  - **Streaming:** `{type:'lease', op:'state', id, p, yaw, q}` every 66 ms.
  - **Exit (E):** reset the vessel to yaw-only; if `exit` is set, place me at its world position; send `lease release {p, yaw}`.
  - On `lost` / `released` / reset / disconnect: end without sending a release.
- **Unreal status: Missing.** The lease plumbing exists in `SendLease` / `ReceiveLease`.
- **Effort:** M.

### 10. Body drag and pins (dragging limp bodies, nails)
- **Web behaviour** (`bodydrag.js`, `localbody.js:187-302`).
  - **Wire:** `{type:'bodydrag', target, grab?:{joint}, end?:true, pose?, p?, yaw?, sim?:{j,p,v}, pinAt?:{joint, at}, unpin?:{joint}, pins?:[{j,at}]}`. The server adds `by`, checks sim validity (≤ 24 joints), and caps pins at 16 (`server/messages.ts:324-366`).
  - **Dragger side:**
    - Left mouse down (not in build mode) picks the frontmost remote whose last clip is `'ragdoll'`: a ray within 0.3 m of a JOINTS bone and `along > 0`, with a 0.4 m frontmost tie window.
    - The body root must be ≤ 5 m from my feet.
    - Build a local sim on their avatar and keep the joint-to-ray offset. Send `grab`. Their presence is no longer applied while I hold them (`draggedLocal`).
    - Each frame: pin the joint to `rayOrigin + dir·depth + off`, step the sim, send `{pose, p, yaw}` every 66 ms.
    - The mouse wheel changes depth by `−deltaY × 0.01`, clamped 1–30.
    - **P** nails the joint: send `{end, pinAt:{joint, at: jointPos}, pose, sim: snapshot, p, yaw}`.
    - Mouse up sends `{end, pose, sim, p, yaw}`.
    - If the owner's newest clip is not `'ragdoll'`, they broke free: drop the drag.
  - **Owner side:**
    - **grab:** refuse with `{end}` if not pushable (toast), not downed, or already held by someone else. Otherwise: limp, clip ragdoll, hint "`<by>` grabs you — move to break free". Remove the pin on the grabbed joint and send back `{pins}` for any others.
    - **Streamed samples:** apply root `p` (including y), yaw and pose; republish them as my presence; derive per-joint velocity from deltas.
    - **end:** add `pinAt` if present, then start my own sim seeded with `sim` (or the derived velocities) and re-apply pins.
    - 1.2 s of silence from the dragger means release. Any movement key gets up and sends `{end}`.
    - `unpin` (pushable only) removes that pin.
  - **Pins:** at most 8, sent as `pose.pins:[{j, at:[x,y,z]}] | null` on presence. They are enforced as firm sim pins.
  - **Markers:** a sphere r = 0.055, colour `#ffd166`, for each pin in remotes' newest samples and my own. Clicking within 0.13 m of the ray removes my own pin, or sends `{unpin}` for someone else's.
- **Unreal status: Missing.**
- **Spec, staged:**
  - (a) render pin markers from presence. **S.**
  - (b) the owner/dragged half without a local sim: accept the stream, republish it, and hold the final pose at release. This is what headless agents do. **M.**
  - (c) dragger, and owner-side settle. Needs section 3. **L.**

### 11. Reach parity gaps
- **Web behaviour** (`reachnet.js`, `reachbone.js`, `avatar.js:1356-1450`, `landmarks.js`, `shared/reachwire.js`, `shared/contact.js`).
  - **Landmarks:** contact points are **surface points with normals**. They're derived once per body by putting the normalized bones at rest and raycasting the mesh from `contactSeed` along `spec.from` (body frame) within `radius × hips-to-head`; there's a fallback offset if nothing is hit. They're stored bone-local.
  - **Target:** pos = landmark + normal × standoff (default 0.02, max 0.2). The palm is oriented to −normal unless `palm:false`.
  - **Limbs and weights:** leftHand, rightHand, leftFoot, rightFoot. Weight eases at 12/s in and out. Reach is skipped while limp.
  - **Solver:** a pole/elbow solver with continuity hints (`shared/reach.js`). A held pose doesn't overwrite bones a reach owns.
  - **Sender:**
    - my own arm is solved too;
    - `pose.reach = {limb: {t: {who, point, standoff?} | {p, space?}, palm?, reached?}}`;
    - `reached:true` when weight > 0.5 and gap ≤ 0.05 m (`TOUCH_GAP`).
  - **Events:** diffs aimed at me (`t.who` or `t.space` == my id) produce reach / touch / release events (`diffReach`). They're narrated in chat: "X reaches toward your `<point>`", "X's `<limb>` touches your …", "X withdraws …", with a 10 s quiet window per (who, limb). The first sample from a new body is a silent baseline.
  - **Commands:** `/touch [name] [point] [left|right]` (nearest person within 6 m, default `shoulder_l`, right hand), `/letgo [left|right]`, `/eyes`.
- **Unreal status: Partial** (`EidoverseReach.cpp`).
  - It targets **bone joint positions** (`ContactBone`), with no surface, normal or palm.
  - No weight easing (the arm snaps when the descriptor disappears).
  - My own arm is never solved.
  - It never sends `reached`.
  - No touch/reach/release events or narration.
  - `/reach` exists rather than `/touch`.
- **Spec:**
  - Derive landmarks from the skinned mesh's reference-pose vertices. glTFRuntime gives the CPU vertex buffer; a nearest-triangle raycast is enough.
  - Add `ReachWeight` easing, a self solve in `TickSelfBody`, `reached` attestation, and the diff events.
- **Effort:** M.

### 12. Wake where you fell asleep (`snapshot.restore`)
- **Web behaviour** (`net.js:805-810`, `main.js:368-389`):
  - Applies on the first snapshot only, and only if the body hasn't moved (speed == 0). Shape: `restore = {p, yaw, q?, locomotion?, clip, pose?, wingsFolded?}`.
  - pos = finite-or-0 of `p`; yaw; camera yaw = yaw + π (behind you).
  - clip `sit` / `sitchair` → posture sit; `lie` → posture lie.
  - clip `'ragdoll'` → y = 0 and stop there (no pose).
  - Otherwise adopt `pose` as the held pose, and set `wingsFolded`.
  - The server has already stripped `emote` and turned ragdoll into idle without a pose (`server.ts:148`).
- **Unreal status: Missing** (it spawns at the spawn point).
- **Effort:** S.

### 13. Seat details: hints, `/sit`, profiled seat height
- **Web behaviour:**
  - **Ambient hint** every 0.45 s (`localbody.js:163`): "X get up · WASD hop off" when mounted; "X — sit" when a declared socket is within 3.5 m.
  - **`/sit [idPrefix]`**: case-insensitive prefix, any distance (`localbody.js:127-157`). When nothing is within 3.5 m but one is within 30 m, chat says "nearest seat is `<id>` (`<slot>`), `<d>`m away — walk closer, or /sit `<id>`".
  - **Profiled seat correction** (`seats.js`, `seatcore.js:138-220`):
    - GET `/avatars` returns entries `{name, path, height, seat:{pose:'sitchair', status:'missing'|'proposed'|'accepted'|'stale'|'unsupported', clipSha256?, contactY?, refusal?, which?}}` plus the `x-profiles-rev` header.
    - Apply only when **all** of these hold: `sock.seatAnchor === 'surface'`; status accepted; `verdict.pose == sock.pose ?? 'sitchair'`; the playing slot is that pose (not a fallback); and the SHA-256 of the loaded `sitting_normal_chair.vrma` bytes equals `clipSha256`.
    - Then root.y = socketY − contactY × riderScale. Otherwise use root-at-socket and show the "≈" nameplate suffix.
    - Refetch on `avatar-updated` and `avatar-profile-updated {name, rev}`, guarded so older revisions don't win (`makeVerdictCache`, `seatcore.js:296`). On an update event the cached verdict drops to pending immediately.
  - **Emote-bar posture tiles:** see section 22.
- **Unreal status: Partial.** Unreal has a mount hint only; no `/sit`, no far-seat message, no profile correction.
- **Effort:** S for hints and `/sit`; M for profiles.

### 14. Jump walk-off ease
- **Web behaviour:** `selectClip` (`locomotion_clip.js:24-34`).
  - A deliberate jump fades in 0.1 s (linear).
  - Leaving a ledge without a jump (airborne > 0.04 s) fades in **0.5 s with smoothstep**: `in = in0 + (1 − in0)·s(u)` and `out = out0·(1 − s(u))`, where s = u²(3 − 2u). It starts from the current weights. If a new clip arrives mid-ease, the ease continues linearly (`avatar.js:1054-1078`).
  - Jumps start at the clip take-off time. Unreal already does this.
- **Unreal status: Partial.** The jump fade is always 0.1 s (`AnimateWithClips`).
- **Effort:** S.

### 15. First-person eye when seated, lying or posed
- **Web behaviour** (`controller.js:802-839`, `fp_view.js`): if a held pose is active or the clip is not one of idle/walk/run/jump/climb, the eye = live head bone world position + 0.06 m up, then 0.16 m along the camera yaw. Otherwise it's feet + 1.45 m.
- **Unreal status: Partial.** The eye is fixed at the capsule centre + 57 cm + 16 cm, so it's wrong when sitting, lying or ragdolled.
- **Effort:** S.

### 16. Speech-driven mouth and gaze (captions, voice level)
- **Web behaviour:**
  - **`{type:'caption', id, text}`** (presence, never logged) runs `performSpeech`: bubble plus mouth (`speakUntil = now + min(bubble, 1000 + 38·len)`), and gaze priority `noteSpeaking(id, min(12000, 3000 + 30·len))` (`voicemouths.js:95`, `net.js:628`).
  - A logged `say` with `args.spoken:true` is not performed again.
  - Logged says: `noteSpeaking(actor, 4000)` (`net.js:683`).
  - **Voice-level mouth:** `target = min(1, level × 3.2)`, smoothed by 0.55 rising and 0.18 falling; `aa = mouth × 0.8`. The gate is 0.045, and a speaking peer gets `noteSpeaking(600)`.
- **Unreal status: Partial.** The mouth only moves for logged `say`. `caption` and the `spoken` flag are ignored. The voice-level mouth isn't applicable until Unreal has a voice transport.
- **Effort:** S for captions.

### 17. Limp extras: eyes, springs, lids
- **Web behaviour:**
  - **`setEyes(shut)`:** lids ease at 12/s. `/eyes close|open` is local only. Shut while limp.
  - **Lid closed angle:** taken per bone from glTF extras (`avatar.js:962-1000`):
    - if `blink_closed_src=='pose'`, use `blink_closed_x` when 1e-3 < |x| < 1.6;
    - otherwise `blink_closed_x − (limit_min_x + limit_max_x)/2` when in range;
    - otherwise 1.2 for upper, −0.35 for lower lids.
  - **Limp springs** (`avatar.js:682`): stiffness × 0.2, gravityPower = max(g, 0.45), center cleared; restored on standing.
- **Unreal status: Partial.** Unreal blinks only, with fixed 1.2 / −0.35, and has no limp spring mode.
- **Effort:** S.

### 18. Fallback body and retrying failed loads
- **Web behaviour:**
  - When a body fails to load, show a VRM-shaped capsule puppet (`capsulebody.js`), coloured `#8fe8c8`. Hips at 0.93; spine/chest/upperChest at 0.10/0.12/0.12; neck 0.13; head 0.08; shoulders ±0.06; upper arm 0.11 → lower arm 0.28 → hand 0.26; upper legs ±0.09/−0.02, then −0.42 and −0.42. Capsules on each bone. Upper arms rotated by z = −s × 1.35.
  - It takes clips, emotes and pose.
  - It retries the real body up to 3 times at 400 / 1600 / 5000 ms (`remotes.js:58-88`).
- **Unreal status: Partial.** Unreal shows a static diver proxy with no animation, and a failed asset is never retried (`RequestAvatar`, `AvatarCache`).
- **Effort:** S for the retry, M for the capsule rig.

### 19. Presence state: present / away / busy
- **Web behaviour** (`presence.js`): becomes away after 180 s with no input, or when the window is hidden. Busy or away can be chosen manually. Every pose carries `presence:'present'|'away'|'busy'`, and receivers show a dot in People.
- **Unreal status: Missing.** It doesn't send or read the field.
- **Effort:** S.

### 20. Movement bounds
- **Web behaviour** (`controller.js:524-533`): applied to walking, swimming and vehicles.
  - If an entity's `comp.environment.bounds = {min:[x,y,z], max:[x,y,z]}` exists, clamp each finite axis.
  - Else, if there is no environment and no water, clamp to a 78 m horizontal radius.
- **Unreal status: Missing.**
- **Effort:** S.

### 21. Remote XR bodies (`pose.xr`)
- **Web behaviour:** `pose.xr = {h:[q], l?:[px,py,pz,qx,qy,qz,qw], r?:[…], c?:[lIndex, lGrip, rIndex, rGrip]}`, relative to the facing (root) frame (`xrbody.js:286-321`).
  - **Head:** Euler YXZ of `h`, pitch clamped ±0.7, roll ±0.5, eased by `min(1, dt·9)`. Spread over spine/chest/upperChest/neck/head with weights:
    - yaw .12/.12/.16/.25/.35
    - pitch .10/.12/.16/.26/.36
    - roll .08/.10/.14/.28/.40
    - Yaw is faded by cos(pitch).
  - **Arms:** two-bone IK to the root-relative grip transforms, unless an emote is playing. A side missing from the wire relaxes.
  - **Fingers:** curls of 1.22 / 1.57 / 0.96 rad per segment × amount (Index uses the index trigger; the other fingers use grip).
  - Samples are blended between packets (`remotes.js:261-288`).
- **Unreal status: Missing.** Local WebXR itself is browser-only; rendering remote XR users is still applicable.
- **Effort:** M.

### 22. Emote bar
- **Web behaviour** (`emotebar.js`, `main.js:438-449`): the bar starts with posture tiles **sit / stand / lie** (sit = same search as X; stand = dismount or clear posture; lie = posture lie), then the emotes in `EMOTE_ORDER` from `/defs` with `icon` glyphs. Keys 1–9 follow that order; the lit tile follows the current clip or emote.
- **Unreal status: Partial.** `BuildEmoteBar` / `RefreshEmotes` and the number keys are present, and defs-hydrated. It lacks the posture tiles and icons.
- **Effort:** S.

### 23. Gaze differences
- **Web behaviour** (`remotes.js:476-518`): my own body looks **only** at the current speaker, otherwise drifts 1.5 m up and 3 m ahead. Remotes look at the speaker, else the nearest body (me included) within 9 m.
- **Unreal status: Partial.** `TickGaze` also aims my own body at the nearest person.
- **Effort:** S.

### 24. Performance: per-distance update rate, body-first loading
- **Web behaviour:**
  - Remote avatar updates run every frame under 8 m, every 2nd frame under 20 m, otherwise every 4th, × `lodBias` (`remotes.js:354,458`).
  - World model parsing waits until my body is on screen, with a 12 s cap (`bodygate.js`).
- **Unreal status: Missing.** Unreal loads my body first, but doesn't hold back model parsing.
- **Effort:** S each.

### 25. Photo mode and spectator follow
- **Web behaviour** (`controller.js:877-993`):
  - **P** toggles a free camera:
    - WASD + E/Q to fly, speed 6 m/s, Shift × 3.5;
    - Alt is fine mode: 2.5 m/s, look τ 0.5;
    - damping τ: move 0.22, look 0.09, FOV 0.16;
    - `[` / `]` change FOV by 30°/s within 12–95;
    - F1 hides the UI, F2 saves a screenshot.
  - Spectator follow: first person from the followed body's head (eye + 0.16 forward, looking 8 m ahead and 0.6 m down), lerp 20.
- **Unreal status: Missing.** These are camera features; low priority here.
- **Effort:** S–M.

### 26. Wardrobe history and thumbnails
- **Web behaviour:**
  - "My avatars" = the last 24 bodies worn, newest first, filtered to the roster, showing `height` (`bodies.js`).
  - Wearing a body renders and POSTs a 256 px portrait plus height to `/thumb?name&height` if `HEAD /thumb/<name>.png` 404s (`avatar.js:2419`).
- **Unreal status: Partial.** The World › avatar picker is in `EidoverseUIWorld.cpp` and avatar switching in `ChangeAvatar`. There's no worn history and no thumbnails.
- **Effort:** S.

### 27. Seat-anchor authoring (`seatedit.js`)
- **Web behaviour:**
  - In build mode, sockets show as gold gizmos.
  - Arm placement, then click on the entity: `pos` (model frame, 3 decimals, part rest-corrected), `yaw` facing the camera minus the entity yaw, `part` if the hit was on a motion part. Slot names are `seat`, `seat2`, ….
  - Drag (4 px slop) slides the anchor over the entity. Q/E turn it by π/12 (0.02 with Shift). Del removes it.
  - Each gesture commits one `comp {id, type:'sockets', data}` (the merged object, or null), and is undoable.
- **Unreal status: Missing.** This belongs to the build area.
- **Effort:** M.

---

## Pose fields Unreal does not yet send
`SendPose` omits:
- `wingsFolded` (always a boolean on web)
- `presence`
- `pose` (held pose; `null` to clear)
- `pins`
- `reach.reached`
- `xr`

The web sends the whole public body on every packet, so these are additive.

## Browser-only or not applicable to the native Mac client
- **WebXR:** `xrIntent`, the XR rig, the local `xrbody` arm and leg solve, the vignette, the radial menu. There's no headset path in this client; remote XR rendering (section 21) does apply.
- **Touch:** the touch joystick and touch look.
- **Pointer lock:** `pointerlock` M/Esc semantics. Unreal has its own M pointer mode.
- **Choosing the physics engine:** `bodysim.js`'s ammo.js WASM loading and Verlet fallback. Use one native solver (section 3).
- **Blob shadow sprite:** Unreal renders real shadows.
- **localStorage:** consent and worn-history storage map to Unreal config/settings instead.
- **`emojiRenders` canvas probe.**