# Water surface revision

## Apply (editor Python, outside PIE)

1. Run `update_ocean_surface.py` to embed `OceanSurfaceDetail.ush` in
   `/Game/Underwater/Materials/M_OceanSurface` and compile/save it.
2. Run `fix_ocean_interface_fog.py` to compile/save the narrowly scoped
   air-to-visible-ocean shortcut in `M_WaterOpticsView`.

Both are idempotent and preserve the existing air-volume graph inputs.
If the original `setup_ocean.py` is rerun, run these afterward.

## Changes

- Broadband, phase-warped wind ripples with variable strength, analytic noise
  derivatives, and per-wave pixel-footprint filtering. Unresolved detail adds
  roughness instead of aliased normal variation.
- Displacement fades by the graded ocean mesh's sampling density outside the
  180 m fine-grid region. The shared four-wave physics, buoyancy, and optical
  height functions are unchanged; render heights match them in the playable grid.
- A restrained, approximately 5 cm maximum local heave and pressure ripple around
  the intersecting submarine, with animated, broken foam and matching normal and
  roughness changes. Existing position/orientation MPC values drive it each tick.
  It fades away when the hull is fully submerged or clear of the water.
- The visible opaque ocean surface viewed from air is no longer fogged by
  re-intersecting an ideal analytic ocean behind/in front of the rasterized hit.
  That mismatch was the main source of the long, repeating horizon fans.
  Foreground geometry, underwater cameras, and dry-room handling retain their
  existing optical paths. Reserved stencil 1 identifies the interface.

This is a lightweight contact treatment, not a fluid simulation or a persistent
velocity-driven wake. It does not add spray particles or change vessel handling.
Local cosmetic heave is not included in buoyancy or fog intersection (max 5 cm).

## Verification

- Both production materials compiled and saved without errors.
- Existing 16-case GPU ocean-height/water-crossing regression passed.
- New 16-case `test_surface_detail_gpu.py` passed: nearby rendered/physical
  heights agree, far displacement is filtered, contact fades at depth, and
  cosmetic deformation stays within its bound.
- `demo_reel/render_water_checks.py`: repeatable late surfacing/platform views.
- `demo_reel/render_water_revision.py`: full 1080p surface, boarding, and platform
  cuts, including the underwater-to-air transition.
- `demo_reel/assemble_water_revision.py`: new reel 02, reusing unchanged cuts and
  preserving all original reel 01 output.
