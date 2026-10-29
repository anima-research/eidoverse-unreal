// G3 confined: only src/main.ts knows the handshake; the engine and the renderer (but its launcher) never name the bridge (ADAPTER-DESIGN §5).
import { test, expect } from "bun:test";
import fs from "node:fs";
import path from "node:path";
import { ROOT } from "./harness.ts";

const SRC = process.env.G3_SRC ? path.resolve(process.env.G3_SRC) : path.join(ROOT, "src");
const UNREAL = process.env.UNREAL_DIR ?? "/home/claude/eido/ref/eidoverse-unreal";
const files = (d: string): string[] => fs.readdirSync(d, { withFileTypes: true }).flatMap((e) => e.isDirectory() ? (e.name === "node_modules" || e.name === ".git" ? [] : files(path.join(d, e.name))) : [path.join(d, e.name)]);
const HANDSHAKE = [/eido-bridge\./, /\b(Bun|process)\.stdin\b/, /sec-websocket-protocol/i];

test("G3a only src/main.ts knows the launch handshake", () => {
  const bad = files(SRC).filter((f) => path.basename(f) !== "main.ts")
    .flatMap((f) => fs.readFileSync(f, "utf8").split("\n").map((l, i) => [f, i + 1, l] as const))
    .filter(([, , l]) => HANDSHAKE.some((re) => re.test(l))).map(([f, n, l]) => `${path.relative(SRC, f)}:${n}: ${l.trim().slice(0, 80)}`);
  expect(bad).toEqual([]);
  expect(HANDSHAKE.every((re) => re.test(fs.readFileSync(path.join(SRC, "main.ts"), "utf8")))).toBe(true);
});

test("G3b the engine knows nothing of the bridge", () => {
  const r = Bun.spawnSync(["git", "-C", "engine", "grep", "-n", "-I", "-i", "eido-bridge"], { cwd: ROOT });
  expect(r.stdout.toString()).toBe("");
});

test.skipIf(!fs.existsSync(UNREAL))("G3c in the renderer's tree, only the launcher knows the bridge (EidoverseSidecar.*, Plugins/Eidoverse/Bridge/)", () => {
  const r = Bun.spawnSync(["git", "-C", UNREAL, "grep", "-l", "-I", "-i", "-e", "eido-bridge", "-e", "eidoverse-bridge"], { cwd: ROOT });
  const hits = r.stdout.toString().split("\n").filter(Boolean).filter((f) => !/(^|\/)EidoverseSidecar\.(h|cpp)$/.test(f) && !f.startsWith("Plugins/Eidoverse/Bridge/"));
  expect(hits).toEqual([]);
});
