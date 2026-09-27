Copies of `eidoverse-worlds/shared/conformance/*.json`: reference outputs of the
shared JavaScript evaluators that the native ports must reproduce.

Refresh after the shared evaluator changes:

    cd eidoverse-worlds && bun tools/motion-vectors.ts && bun tools/water-vectors.ts
    cp shared/conformance/{motion,water}.json <Test1>/Plugins/Eidoverse/Resources/Conformance/

Then run the `Eidoverse.MotionConformance` and `Eidoverse.WaterConformance` automation tests.
