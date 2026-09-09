# Underwater optics

Installed in Test1 / L_Epipelagic. Original project backup:
`/Users/olena/Documents/Unreal Projects/Test1-before-water-fixes.MAbmnO`

## Ownership

- `Water Optics Controller` discovers BoxComponents named `AirVolume` or tagged `WaterAirVolume`. Actual world position, component axes and scaled extents populate the shared MPC. No habitat slot assignment is required. The current fixed capacity is **16 air boxes**, with an explicit overflow error.
- `M_WaterOpticsView` is the sole underwater view extinction/scattering pass. It clips at the surface, unions overlapping air intervals, and integrates only water. Engine height/volumetric fog is disabled. Solar surface lights remain, but do not inject engine volumetric fog.
- `M_WaterOpticsLocalLight` is assigned dynamically to local lights in PIE/game. Each light has its own source-position parameter; the engine retains its distance falloff and shadows. No torch fraction is estimated from the already-combined scene color. `SkipWaterOptics` opts a light component out. Currently this assignment replaces a pre-existing light function, so tag custom-function lights to preserve their authored function.
- `LF_SunSpectral` retains the existing spectral surface-light parameters, while using the shared air-box registry and corrected depth integral.
- Histogram exposure replaces depth/interior gain multipliers. WaterParams B controls the permitted exposure range; A controls adaptation speed. Zero B locks exposure at EV100 3. The original turbidity and depth-profile controls are retained.

Old fog-density, two-slot habitat and torch-MPC writes were removed from the three Blueprints. Execution chains for movement, interaction, and submarine attach/detach were preserved. The old `PP_Underwater` asset is retained but is no longer the active blendable. Unused legacy graph helper nodes/variables and MPC parameters remain for reference; they no longer drive the new optics.

## Numerical details

Geometry is in centimetres; optical lengths are in metres. Air intervals are sorted and merged, including nested and touching intervals. Near-horizontal rays use a stable profile-integral limit. The bottom turbidity integral continues linearly below 200 m. The HDR spectral lookup continues its terminal log-transmission slope beyond 250 m. Zero-length transmission is exactly one. Scattering uses nonuniform near-camera quadrature and Unreal pre-exposure.

## Verification

- Universal Mac plugin build: arm64 and x86_64 succeeded with UE 5.8.2.
- Metal SM6 materials compiled after migration; affected Blueprints compiled.
- `Content/Python/test_water_optics.py`: 16 actual GPU path cases passed (nested/overlapping/touching/16 boxes, rotated/scaled bounds, parallel and zero-length rays, surface crossings). GPU deep-profile integral checked against analytic values. Real HDR LUT checks passed for zero-distance identity, monotonic RGB and continued blue attenuation at 300/500 m. Run with PIE stopped.
- `Content/Python/test_water_runtime.py`: live PIE checks passed for two discovered volumes, all 13 per-source light materials, disabled global fog, transformed box position/scale/axes, dry camera membership, leaving one of two overlapping rooms, and clearing destroyed-room slots. **This deliberately moves and destroys actors only in the temporary PIE world. Stop PIE to discard those test changes.**
- Visual inspection: exterior haze and clear warm habitat framing with water still visible through its window.

Run scripts via the editor console: `py "<absolute script path>"`. `setup_water_optics.py` is the reproducible material/controller migration; `cleanup_legacy_water.py` is the targeted Blueprint cleanup. After changing `.ush`, run `RecompileShaders Changed` before tests to invalidate shader source caches.

## Remaining rendering limits

This is a corrected analytic water model, not a full multiple-scattering renderer. Solar in-scattering is approximate and does not sample geometry shadows. **Shadowed volumetric torch/headlight beams are not implemented.** Disabling engine volumetric fog removes its old beams as well as its incorrect global medium. A dedicated shared-medium volumetric implementation would be needed to restore physically consistent shafts.

Per-source light functions are scalar luminance transmission, not RGB source-path filtering. View-path transmission remains RGB. Epic's colored light-function atlas cannot sample world position, which these air-volume-aware functions require: https://dev.epicgames.com/documentation/en-us/unreal-engine/using-light-functions-in-unreal-engine

Local-light material assignment is runtime-only; the editor viewport previews solar/view optics but not local source attenuation until PIE. Camera membership compatibility targets this project's single-player setup. Windows/Linux shader compilation, packaged builds, sustained GPU profiling and production performance budgets have not been validated.

## Recovery

Close Unreal before recovering. The backup holds the pre-change `.uproject`, `Content` and `Config`. Restore those as a matched set and remove/disable only the new `UnderwaterOptics` plugin. Do not restore only the plugin flag while leaving materials referencing its shader include.
