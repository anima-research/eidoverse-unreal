Copies of `eidoverse-worlds/shared/conformance/*.json`: reference outputs of the
shared JavaScript evaluators that the native ports must reproduce.

Refresh after the shared evaluator changes:

    cd eidoverse-worlds && bun tools/motion-vectors.ts && bun tools/water-vectors.ts
    cp shared/conformance/{motion,water}.json <Test1>/Plugins/Eidoverse/Resources/Conformance/

Then run the `Eidoverse.MotionConformance` and `Eidoverse.WaterConformance` automation tests.

`flight.json` and `ragdoll.json` are generated here instead, from the server checkout named by `EIDOVERSE_REPO`
(default `~/connectome-local/eidoverse-worlds-native`):

    BUN_RUNTIME_TRANSPILER_CACHE_PATH=0 bun Tools/eidoverse/flight_vectors.ts
    bun Tools/eidoverse/ragdoll_vectors.ts

Then run `Eidoverse.FlightConformance` and `Eidoverse.RagdollConformance`.
