# Pressure-vessel redesign

Installed into Test1's `/Game/Underwater/Vessels` and the existing
`BP_Habitat` / `BP_Cyclops` Blueprints in `L_Epipelagic`.

- **PELAGIC 01:** double-skinned curved research habitat, observation windows,
  pressure hoops, landing legs, service deck, reserve tanks, furnished laboratory,
  lighting fixtures and an open central moonpool.
- **NEREID 07:** streamlined expedition submarine, curved glazed bridge, sail,
  periscopes, dive planes, tail fins, ducted propeller, furnished interior and
  retained underside entry.

Original actor positions, entry/exit interaction graphs, lights and moonpool
surfaces are preserved. Old box-wall components are retained by name but have
no mesh or collision. Eight habitat calibration spheres are hidden, not deleted,
under `Legacy/Hidden habitat calibration`.

## Sources

`exports/PressureVessels.blend` contains editable geometry. `build_vessels.py`
generates it and ten grouped FBX meshes in centimeters. FBX import mirrors Blender
Y into Unreal Y. `import_vessels.py` imports meshes and builds the palette;
`install_vessels.py` updates Blueprint templates and existing placed instances;
`verify_vessels.py` checks new spawns and collision openings. Run editor scripts
with PIE stopped. The installer deliberately skips actor root transforms.

## Verification and limits

- Both Blueprints compile; fresh instances contain all five replacement meshes.
- Solid shell line traces block and both moonpool paths are unobstructed.
- Live 35 cm sphere sweeps (the diver's actual radius) clear both entrances.
- Both live interiors register `InsideHabitat=True`.
- Shared underwater controller discovers 15 air boxes without overflow.
- Air-volume bottom faces sit inside floor slabs to avoid optical boundary artifacts.
- Geometry is a modeled first pass, not a new vehicle simulation. Existing movement
  remains; propeller and instrument displays are decorative/static.
- Curved dry interiors are approximated by multiple boxes. The current shader
  capacity is 16 boxes total: adding another complete vessel needs a capacity or
  volume-layout change. No curved analytical fog volume was introduced.
- Collision uses complex-as-simple mesh queries, not production convex hulls for
  rigid-body vessel physics. No LOD/texture-detail optimization pass yet.

Before-change backup: `/Users/olena/Documents/Unreal Projects/Test1-before-vessel-redesign.jIviV7`.

## Refinement: normals, glazing, attachments and attitude control

- Recalculate closed custom fitting normals before beveling (sail and mirrored fins).
- Weld glass topology at generation time so shading normals interpolate between
  polygons. Shape-preserving cubic hull profiles add bow curvature without moving
  the original opening boundaries. Glass uses two-sided complex collision.
- Move reserve tanks off the observation windows onto a roof rack with saddles.
- Connect the deck to the lower hull with cantilever beams, mounting shoes and braces.
- `refine_controls.py` updates only submarine attitude handling: normalize wrapped
  controller pitch before clamping to ±25°, enforce the same pilot-view pitch limit,
  and increase hull rotation interpolation from 0.6 to 2.0. Below 100 cm/s, idle trim
  blends in smoothly; full leveling below 15 cm/s uses interpolation speed 0.6.
  Unoccupied vessels level pitch and roll while retaining heading. Existing default
  pawn movement/input bindings and entry/exit graphs remain unchanged.
- `verify_refinement.py`: six 35 cm sphere sweeps hit `Vessel_Glass`, without starting
  in penetration, on both sides of both habitat windows and the submarine canopy.
- `test_controls_live.py`: PIE checks passed for positive, negative and wrapped
  negative pitch, occupied idle leveling and unoccupied leveling. At rest, a 20°
  starting pitch settled below 0.5° in six seconds. Test mutations are PIE-only.

Refinement backup (Content and generators):
`/Users/olena/Documents/Unreal Projects/Test1-before-vessel-refinement.clUUug`.

## Ocean and surfacing

- `build_ocean.py` creates a 20 km-wide graded grid, with 1 m spacing around
  the playable area and coarse geometry toward the horizon (~609k triangles).
- `setup_ocean.py` installs `/Game/Underwater/Materials/M_OceanSurface` and
  `SM_OceanSurface`, replaces the original blocking plane and enables native
  surface behavior on the Water Optics Controller. `refine_ocean_look.py` removes
  surface caustic projection and artificial foam patches.
- Four directional swells (38/23/12/6 cm amplitudes) physically displace the mesh.
  Seven nonaligned ripple frequencies supply fine normals without a texture tile.
  `OceanWaveData.ush` is shared by C++ and HLSL; the controller supplies the common
  simulation clock. `OceanWaves.ush` supplies analytic height and normal derivatives.
- Space ascends; Left Ctrl or C descends, for both pawns. Reaching the surface
  latches a floating state that follows waves, keeping the camera around 35 cm
  above them. Downward swim input immediately releases floating so the pawn can dive.
  Deep pawns receive no global buoyancy; divers in dry rooms/attached to vessels
  are excluded. This is kinematic surface flotation, not rigid-body buoyancy.
- Ocean geometry is masked out of dry boxes and the active submarine cross-section.
  The additional submarine mask currently supports the one submarine in this level.
- The shared fog integrator clips against the wave surface with a bounded 32-step
  crossing search and bisection in the ±80 cm band; deep rays use a fast path.
  This is an approximation for long, nearly horizontal waterline rays, not a full
  refractive water simulation. Surface rendering is masked/opaque, not see-through.
- Existing 16-case GPU optics and spectral LUT regressions pass. `test_ocean_gpu.py`
  passes 16 wave-height/normal/water-path cases. `test_surfacing_live.py` verifies
  rise → idle float → dive for both pawns using movement input (not teleporting
  across the surface); measured idle camera clearance was 33–37 cm.
- Runtime plugin builds passed Mac Editor, Development and Shipping, arm64+x64.
  No full game packaging or hydrodynamic simulation was added.

Pre-surfacing backup: `/Users/olena/Documents/Unreal Projects/Test1-before-surfacing.0RwFmG`.

## Marine snow and working exterior lights

- `/Game/Underwater/FX/NS_MarineSnow`: one GPU `MarineSnow` emitter based on
  Niagara's `RecycleParticlesInView` template. One burst of 1,800 immortal recycled
  particles, 0.35–1.1 cm sprites, 30–1,800 cm view distance, 1.2× view coverage.
  Recycle velocity is (1.4, 0.6, -1.8) cm/s. Gravity, wind, aerodynamic drag,
  mesh orientation and mesh-aligned sprite modules are disabled. Sprite facing
  is FaceCamera, alignment Unaligned, sort ViewDepth; shadows and motion vectors
  are disabled. Responsive AA reduces trails on these tiny translucent sprites.
- `setup_marine_snow.py` builds the soft lit translucent sprite material and places
  one Niagara actor. Opacity fades at near/far range and geometry intersections,
  excludes all 16 shared dry boxes, and fades out above the animated wave surface.
  The GPU system has fixed bounds covering the ocean; only the local view cloud
  is rendered. This is visual marine snow, not simulated sediment transport.
- `fix_lights.py` repairs the diver's `TorchIntensity` default (was 0, overwritten
  into the light every Tick), setting it to 800 candela. F still toggles visibility.
  The source is now 55 cm forward; the owner-hidden placeholder body sphere no
  longer casts a blocking shadow. Collision and swimming remain unchanged.
- Sub floodlights now use 6,000 candela / 45 m attenuation radius and sit at
  (232, ±205, -135) cm, ahead of new external housings/lenses with attached support
  arms. Hull shadows and per-source underwater light attenuation are preserved.
  Only `SM_Submarine_Fittings` was reimported; `VESSEL_IMPORT_ONLY` can scope future
  imports. `install_vessels.py` uses the updated source positions as well.
- `test_lights_live.py` verifies Tick-driven torch output, both headlights' output,
  33 visibility rays across each floodlight cone, and the active Niagara system.
  It then positions the diver for a torch-on/off visual check (PIE changes only).

Before-change Content/source backup:
`/Users/olena/Documents/Unreal Projects/Test1-before-particles-lights.33c9JW`.

## Translucency follow-up: moving particles and distant glass

- Supersedes the initial snow temporal settings above: sprite size is now
  0.65–1.6 cm, view-recycler rejection attempts 16 (was 1), renderer motion
  setting Precise. `M_MarineSnow` renders After Motion Blur, after temporal
  reconstruction. It no longer needs a stationary image history to become visible.
  Material velocity output remains OFF: enabling it disturbed the custom fog's
  depth buffer and produced dark specks. DepthFade still rejects particles behind
  opaque scene geometry. No global antialiasing or motion-blur settings changed.
- `refine_translucency.py` preserves the original glass surface/reflections and
  multiplies its opacity by shared camera-to-pane water-path transmission. This
  includes the spectral LUT, depth-varying turbidity, waves and all dry boxes.
  Refraction approaches its neutral IOR as the pane becomes obscured. The glass
  remains after-DOF so the already-fogged background isn't integrated twice.
  This is a scalar-luminance contrast approximation for default-lit transparency,
  not a full spectral multilayer glass/fog compositor.
- `test_particles_motion_live.py`: 60 seconds of continuous 30°/s camera yaw and
  translation, with particles visibly present during motion. Changes are PIE-only.

Pre-fix FX/glass backup:
`/Users/olena/Documents/Unreal Projects/Test1-before-translucency-fix.isyMBW`.
