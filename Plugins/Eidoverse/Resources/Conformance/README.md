Copies of `eidoverse-worlds/shared/conformance/*.json`: reference outputs of the
shared JavaScript evaluators that the native ports must reproduce.

Refresh after the shared evaluator changes:

    cd eidoverse-worlds && bun tools/motion-vectors.ts
    cp shared/conformance/motion.json <Test1>/Plugins/Eidoverse/Resources/Conformance/

Then run the `Eidoverse.MotionConformance` automation test.
