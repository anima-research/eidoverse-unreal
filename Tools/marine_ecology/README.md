# Temperate shelf ecology

Original procedural meshes and materials for the L_Epipelagic setting. No third-party
asset downloads, licenses, purchases, or external services are required at runtime.

## Scene

- A continuous ~1.4 km static-mesh seabed: shallow bank, sand pocket around 8 m,
  research-station bench at 33.32 m, and slopes into the ~200 m basin.
- The original flat Seabed remains in the map, hidden and non-colliding, for rollback.
- The habitat's four feet meet the new terrain. The moonpool and sub exit lanes
  remain open. The previously below-terrain diver start was moved above the shelf.
- 70 kelp plants, 299 understory kelp clumps, 302 low red-algae clumps, 151 seagrass
  tufts in the shallow pocket, 311 rock/rubble instances, 54 urchins, 32 mussel
  clusters, and 45 sea stars. Placement is deterministic, patchy and depth-zoned.
- Five schools of 32 silver fish, two groups of three rockfish, and two skates.
  Stock RotatingMovementComponents animate slow looping routes. Vertex colors drive
  tail/wing motion; plants sway with their bases held in place.
- Ground and large rocks collide. Vegetation and ambient animals do not impede
  swimming. Grouped instancing and distance culling limit draw calls and overdraw.

## Scope and caveats

This is a modeled environmental first pass, not species-identification-grade anatomy
or an animal AI simulation. Fish have authored routes, not schooling/avoidance AI;
plants use shader sway, not fluid dynamics. Terrain is an authored static heightfield,
not an Unreal Landscape with sculpt/paint layers. No production LOD or packaged-build
performance pass has been completed. Existing calibration stations are preserved.

Ecological reference: NOAA describes kelp anchored to rocky bottom in shallow,
sunlit temperate water, with understory algae and benthic animals beneath it:
https://sanctuaries.noaa.gov/visit/ecosystems/kelpdesc.html
https://montereybay.noaa.gov/science/characterization/kelp-forests.html
Seagrass is restricted to a higher sandy pocket, not the deep basin:
https://www.fisheries.noaa.gov/feature-story/importance-eelgrass

## Reproduce / verify

1. Stop PIE; run Blender background with `build_ecology.py`.
2. Run `import_ecology.py` through Unreal Python, then `install_ecology.py`.
   `ECOLOGY_IMPORT_ONLY=ShelfTerrain` scopes geometry reimports and preserves materials.
3. `verify_ecology.py`: ground-height collision checks at all four habitat pads,
   submarine clearance, shallow/deep terrain samples, plant collision/depth limits,
   and a clear diver start. All checks passed.
4. `test_wildlife_live.py` in PIE checks every swimming group changes orientation
   over five seconds. All nine groups passed (6.5–20.5 degrees traveled).

The working authoring folder is `/Users/olena/unrealtest/marine_ecology`; the project
contains a committed mirror under `Tools/marine_ecology`. Generator/export paths
currently target this local workspace. The pre-ecology checkpoint is `936b44e`.
