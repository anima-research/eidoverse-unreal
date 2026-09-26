# Spectral ocean prototype

Open `/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype` and Play.
The original `/Game/Underwater/Maps/L_Epipelagic` remains on the previous water.
This is a separately reviewable replacement foundation, not a promoted release.

2026-09-17 editor-surface correction: `MakeField` now zero-initializes mip zero
as well as every smaller mip before creating the GPU resource. `CreateTransient`
only allocates the base image; the editor never runs `UpdateWake`, so the old
field exposed uninitialized float displacement/foam and black square patches.
Excluding the wake region removed the artifact in a controlled view. The fix
keeps the wake, reflections and shadow settings enabled; a sampling regression
checks all mip levels start at neutral values.

## What changed

- Three deterministic, evolving FFT bands: 2048 m / 128², 128 m / 128², and
  8 m / 64². Directionally weighted, band-limited Phillips-style spectra, with
  vertical and choppy horizontal displacement. Wind-ripple geometry and normals
  derive from this field; there are no independently panning normal layers.
- A camera-following, graded 434,312-triangle grid, with 50 cm cells in the
  central 120 m square. This is a graded grid, not a quadtree implementation.
  Displacement is filtered to mesh density; normal detail is filtered to pixels.
- Single Layer Water for the air-side view, with absorption/scattering, scene
  refraction, reflection and depth visibility. A separate translucent underside
  uses the same displaced mesh, water-to-air refraction and critical-angle
  reflection. An actual scene capture supplies the underwater reflection fallback
  (128² HDR/depth cube, refreshed at 2 Hz), rather than a hard-coded painted sky.
  Captured depth approximates the reflected water path for colour attenuation;
  the underside is unlit to avoid reflecting an air-side skylight underwater.
  The prototype also restores skylight intensity to 1 (the original is 0) and
  uses a 1.5 wave-amplitude multiplier.
- A moving 64 m / 128² local wave-equation grid around the submarine. Movement
  injects pressure disturbances, waves propagate and damp, and contact foam
  persists then decays. Recentring preserves the world-space wave history.
- The controller's optional `SpectralOcean` reference routes diver/sub flotation
  through the same spectral fields. Height lookup inverts horizontal displacement
  with three fixed-point iterations. View fog terminates at the rasterized
  interface; local-light transmission uses a mean-level boundary approximation.

## Implementation / limits

The FFT is currently a **CPU reference implementation**, with mipmapped float
displacement textures uploaded for rendering. This avoids GPU readback lag in
buoyancy and provides a correctness baseline for a future compute implementation.
A measured runtime snapshot was about 2.3 ms for the update on this machine;
this excludes GPU rendering/capture costs and is not a complete performance claim.

## Composition and performance correction (2026-09-09)

The underside previously rendered After DOF, over the Before DOF water-fog pass.
It now renders Before DOF and writes the interface custom depth/stencil. The fog
endpoint is reconstructed from axial depth and the camera-forward dot product;
scaling a sky world position by a depth ratio was invalid at the infinite far plane.
For a submerged camera viewing the interface, the camera-to-interface interval is
water (minus the existing air-volume union). Its physical sea-level reference is
not moved to the far/sky endpoint. Nearby reflections remain visible; distant
surface detail fades into the same medium as opaque objects.

The view-fog pass no longer performs a 32-step FFT intersection search per pixel.
It uses raster interface depth, retaining the existing 24-step scattering integral
and air volumes. Local-light transmission also avoids FFT intersection tracing,
using mean sea level as a documented near-wave approximation. No global screen
percentage, shadow quality, or lighting scalability was lowered.

The 2 Hz reflection cube is now a bounded 180 m, coarser-LOD fallback, without
light functions, dynamic shadows, volumetric fog or Lumen GI/reflection passes.
These simplifications apply only to the capture, not the main scene. Light-function
material discovery is cached for one second rather than scanning every actor each tick.

Fixed-camera PIE A/B at a 2744×1684 viewport, 30 warmup + 90 sampled frames,
with background throttling disabled temporarily and restored afterwards:

| Metric | Before | After |
|---|---:|---:|
| Median frame interval | 125.0 ms | 62.6 ms |
| Mean frame interval | 152.7 ms | 63.6 ms |
| 95th percentile | 246.8 ms | 71.5 ms |
| Median ocean CPU update | 3.26 ms | 2.56 ms |

These include editor overhead and are not standalone-game benchmarks. GPU profile
snapshots put the fog material at 32.7 ms before and 4.9 ms after; a sampled local
light function fell from 12.0 ms to 2.7 ms. Performance is still **not a 60 fps
result**: remaining lighting, shadows, Lumen, translucency and TSR costs are
substantial. The six spectral sun lights are a next profiling/optimization target.

Validated shallow/upward, 30 m and 90 m views in
`demo_reel/spectral_attenuation_after_03/`, and the original four review angles in
`demo_reel/spectral_checks_09/`. The previous film predates these fixes.
Timing reports: `demo_reel/spectral_profile_before.json` and
`demo_reel/spectral_profile_after.json`. `profile_live.py` is a repeatable temporary
PIE A/B; `profile_after.py` starts its post-change run. `check_attenuation.py`
builds/renders the depth checks. Use a fresh output tag for each render.

This is not a full Navier–Stokes simulation. There is no overturning/breaking
geometry, shoreline shoaling, or spray particle simulation. Wave bands are
periodic by construction; different scales and non-overlapping frequency ranges
reduce repetition but do not make the ocean mathematically aperiodic.

The near geometry filters sub-cell ripples, while physics samples the full field.
Refraction is screen-space; the underside cubemap is a low-frequency fallback,
not a ray-traced reflection solution. Reflected-path attenuation is approximate
and does not integrate habitat air volumes. Surface/underside visibility switches
at the camera's sampled height. Existing sun-light functions, glass materials,
and marine-snow boundaries retain their legacy near-surface approximations; these and lighting
balance should be assessed before promoting the prototype into the main map.

## Rebuild / reproduce

### Second lighting performance pass (2026-09-09)

The prototype opts out of `r.TranslucencyLightingVolume` during Play: its glass
and pool surface use per-pixel forward lighting, and its snow/ocean underside
are unlit, so these materials do not consume the volume grid. The actor option
`SkipUnusedTranslucencyVolume` defaults off; turn it off if adding volume-lit
translucent materials. The unlit underside also opts out of front-layer reflection
eligibility; glass keeps its reflection eligibility. No main-view light, shadow
resolution or rendering resolution was removed/reduced.

`ShadowPageBudget=8192` addresses observed physical shadow-page exhaustion at
4096, trading additional GPU memory for shadow reliability. Both console changes
use tagged temporary layers and are removed on EndPlay. They are process-wide
while this prototype is running, not permanent project settings. They respect
higher-priority explicit console overrides.

Full-resolution fixed-camera PIE A/B (2744×1684, 100% screen percentage, 30 warmup
and 90 measured frames per phase, 8192 shadow pages in all phases):

| Metric | Volume on | Volume off |
|---|---:|---:|
| Median frame interval | 46.18 ms | 41.49 ms |
| Mean frame interval | 46.79 ms | 41.99 ms |
| 95th percentile | 54.12 ms | 48.23 ms |
| Median ocean CPU update | 2.40 ms | 2.41 ms |

The repeat off phase measured 43.22 ms: roughly 3–5 ms saved in this editor
view, with run-to-run variation. Front-layer Lumen reflections were already off
globally, so their separate toggle showed no gain. An earlier automatic-resolution
run was 27.16→22.40 ms; do not compare these timings to the earlier composition
pass as though they share identical render settings. This is still not 60 fps.

Five matched 960×540 checks cover habitat glass, submarine glass, near-surface,
platform and upward underwater views. SSIM values respectively: 0.994660,
0.994047, 0.983444, 0.997934, 0.998440; simulation/capture timing makes the frames
not pixel-identical. Visual review found no material lighting loss in the checked
habitat and surface views. Outputs: `demo_reel/lighting_quality_reference/` and
`demo_reel/lighting_quality_optimized/`. Timings:
`demo_reel/spectral_profile_lighting_fullres_ab.json`.

The 8192-page run did not log physical page-pool exhaustion in these checks, but
a distinct **non-Nanite shadow marking job-queue overflow** remained during live
gameplay. Non-Nanite shadow coverage and the six spectral sun lights remain
profiling targets. RGB light-function atlasing is not directly applicable to the
current world-position-dependent attenuation functions.

`start_lighting_profile.py` runs the isolated A/B. Restart the editor after these
diagnostic console overrides to clear their console-priority layers.
`test_performance_scope.py` verifies settings return to their pre-Play values.
The final clean-restart check passed: `[1, 4096]` before Play, `[0, 8192]`
during Play, and `[1, 4096]` afterward, without the deprecated-CVar warning.
The rebuilt plugin passed Editor/Development/Shipping for Mac arm64/x64;
16 CPU/GPU height samples passed with a maximum discrepancy of 0.046 cm.
Both maps passed the ownership/state audit, and the prototype is left open.

### Build steps

Shadow experiment status (2026-09-09): **not promoted**. Seven Nanite candidate
mesh copies remain under `SpectralPrototype/ShadowMeshes`; the prototype's eleven
affected components were restored to the original mesh bindings. The user reported
an underpowered supply; macOS reported AC connected, battery 5%, not charging.
ABBA timings varied too widely even within the same configuration to establish
a performance gain. `shadow_profile_ab.json` and `nanite_shadow_profile_abba.json`
are inconclusive diagnostics, not before/after performance claims. Repeat only
after stable power is available. `install_nanite_shadows.py` reapplies the candidate;
`pause_shadow_review.py` restores the mesh bindings without deleting candidates.

1. Build the UnderwaterOptics plugin with Unreal 5.8 and restart the editor.
2. Run `build_mesh.py` using Blender in background mode if changing mesh density.
3. Run `prepare_optics_shader.py` to mechanically regenerate the separate
   `SpectralWaterOptics.ush` from the legacy optics implementation.
4. Run `install_prototype.py` in the Unreal Python console. It compiles/saves
   prototype materials and installs only into the separate prototype map.
   `install_map.py` performs just the map setup.
5. Run `test_gpu_field.py` outside PIE after the prototype is initialized.
6. Run `build_checks.py`, then `render_checks.py` or `render_film.py`.
   Use a fresh output directory for a new MRQ run (do not overwrite PNG runs).
7. Run `assemble_film.py` in terminal after the full render finishes.

Production material/shader assets were not replaced. `WaterOpticsController`
defaults to the old path when its new optional reference is unset. No commit or
push was made. The native plugin was built for Mac arm64/x64 Editor, Development,
and Shipping configurations.

## Validation

- All four prototype materials compiled for Metal SM6.
- Sixteen CPU/GPU displacement/height comparisons, including negative positions
  and periodic seams: maximum discrepancy **0.071 cm** in the latest run at the 1.5 amplitude setting.
- Preview captures cover the platform, submarine emergence, and looking upward
  through the surface. The full 26 s review adds a low waterline view.
- `audit_maps.py` verifies the original map has no spectral actor and its old
  water is visible; the prototype has one spectral actor and hides the old water.
- Outputs: `/Users/olena/unrealtest/demo_reel/spectral_film_02/`.

## Map-copy warning resolved

Unreal's `save_map(world, new_path)` copies the level but does not make that copy
the active editing world. An initial setup pass consequently changed the open
original map; the subsequent Save As refused to overwrite it. The precise setup
changes were reversed in the original map, then the prototype was explicitly
loaded before installation. Both states were audited. Baseline recovery copies
remain in `demo_reel/spectral_recovery/`. `restore_map_identity.py` is a historical
one-time repair script, not a normal installation step.

## References

- Tessendorf, *Simulating Ocean Water*: https://people.computing.clemson.edu/~jtessen/reports/papers_files/coursenotes2004.pdf
- Epic, Single Layer Water: https://dev.epicgames.com/documentation/en-us/unreal-engine/single-layer-water-shading-model-in-unreal-engine
