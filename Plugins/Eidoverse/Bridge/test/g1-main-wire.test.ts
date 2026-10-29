// G1 no main wire in the renderer: its C++'s main-only message names, counted per file, are the checked-in list; each slice shrinks it (ADAPTER-DESIGN §5; SLICES.md).
import { test, expect } from "bun:test";
import fs from "node:fs";
import path from "node:path";
import { ROOT } from "./harness.ts";
import { scanWire, PRIVATE } from "./main-wire.ts";

const UNREAL = process.env.UNREAL_DIR ?? path.resolve(ROOT, "../../..");

test.skipIf(!fs.existsSync(path.join(UNREAL, PRIVATE)))("G1 the renderer's main-wire sites are the checked-in list (a slice that retires some refreshes it: bun test/main-wire.ts <unreal dir>)", () => {
  expect(scanWire(UNREAL)).toEqual(JSON.parse(fs.readFileSync(path.join(ROOT, "test/fixtures/main-wire.json"), "utf8")));
});
