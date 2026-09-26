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
- 478 independently steered fish in 16 schools: sardine-like silver fish, striped
  mackerel, deeper-bodied reef perch and bottom-following skates. The old nine
  rigid looping wildlife actors are retained hidden for rollback.
- Five substrate/habitat treatments: shallow meadow, kelp reef, patchy coarse
  shell/gravel, exposed bedrock and deeper silt. Sand uses domain-warped world-space
  noise, slope/depth blends, irregular gravel, sparse shell flecks and patchy
  ripples. Fine relief fades with pixel footprint rather than aliasing at distance.
- The expansion adds roughly 1,200 instanced details, including crabs, sea
  cucumbers, anemones, open-topped sponges and shell hash. Exact deterministic
  counts and positions are recorded in `expansion_layout.json`.
- A 12 x 10 m piled research platform at (0,6000,0), over the shallow meadow.
  Swim to its yellow ladder at approximately (0,5395,-100) and press **E**.
  The 3.2-second climb ends on the deck; E near the ladder descends again.
  Deck movement is slower and height-constrained; leaving the edge falls to water.
- Ground and large rocks collide. Vegetation and ambient animals do not impede
  swimming. Grouped instancing and distance culling limit draw calls and overdraw.

## Scope and caveats

This is procedural environmental art, not species-identification-grade anatomy.
`AMarineEcosystem` uses seeded schooling, separation/alignment/cohesion, bounded
wandering, player avoidance, cached seabed heights and budgeted obstacle probes.
It is ambient steering, not navigation-mesh AI, a food chain, or physically
colliding fish. Bottom animals are static; plants and fins use shader motion.
Terrain remains the original static heightfield. Scatter uses editor Python and
HISMs, not PCG graphs. Mesh Terrain was intentionally not enabled: UE 5.8 marks it
experimental and requires a World Partition level migration.

Eleven dense plant/benthic meshes have three geometry LODs; instanced scatter has
distance culling. Materials compile and native Editor/Development/Shipping plugin
builds pass on Mac arm64+x64. No packaged-game performance certification has been
performed. Existing calibration stations are preserved.

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
3. For the expansion, rebuild the UnderwaterOptics plugin (new native classes),
   restart the editor and explicitly load L_Epipelagic. Run `import_expansion.py`,
   `enrich_materials.py`, `generate_ecology_lods.py`, and `install_expansion.py`.
   Expansion installation clears its own old instances before collision traces.
   Do not run installers during PIE. Re-running the base installer restores the
   old wildlife loops, so run the expansion installer afterward.
4. `verify_ecology.py`: ground-height collision checks at all four habitat pads,
   submarine clearance, shallow/deep terrain samples, plant collision/depth limits,
   and a clear diver start. All checks passed.
5. `verify_expansion.py`: nonempty scatter, surface-normal alignment, deck collision
   and retired wildlife visibility. `test_expansion_live.py` in PIE checks all
   fish types move, neighbor spacing changes, and boarding reaches the deck.
   `test_platform_down.py` checks return to water and buoyancy-tag cleanup.
   `check_fish_habitat.py` checks all fish against terrain, sea surface and dry rooms.
   The old `test_wildlife_live.py` applies only to the retired first-pass loops.

The working authoring folder is `/Users/olena/unrealtest/marine_ecology`; the project
contains a committed mirror under `Tools/marine_ecology`. Generator/export paths
currently target this local workspace. The pre-ecology checkpoint is `936b44e`;
the initial ecology checkpoint is `3ce3284`.

Epic references for the terrain decision:
https://dev.epicgames.com/documentation/unreal-engine/mesh-terrain-in-unreal-engine
https://dev.epicgames.com/documentation/unreal-engine/crafting-mesh-terrain-in-unreal-engine
