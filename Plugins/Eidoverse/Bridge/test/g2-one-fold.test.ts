// G2 one fold: src/ imports fold/evaluator code only from engine/, defines none of it (AST scan), engine/ pinned + clean (ADAPTER-DESIGN §5).
import { test, expect } from "bun:test";
import fs from "node:fs";
import path from "node:path";
import ts from "typescript";
import { ROOT, ENGINE } from "./harness.ts";

const SRC = process.env.G2_SRC ? path.resolve(process.env.G2_SRC) : path.join(ROOT, "src");
const files = (d: string): string[] => fs.readdirSync(d, { withFileTypes: true }).flatMap((e) => e.isDirectory() ? files(path.join(d, e.name)) : /\.(ts|js|mjs)$/.test(e.name) ? [path.join(d, e.name)] : []);
const git = (...a: string[]) => { const r = Bun.spawnSync(["git", ...a], { cwd: ROOT }); return { code: r.exitCode, out: r.stdout.toString().trim(), err: r.stderr.toString() }; };

/** The fold's own names, and every export of the modules the design lists as the evaluators the bridge reads (§1). */
const CORE = ["applyInPlace", "opsOf", "makeReplica", "viewState", "diffView"];
const EVALUATORS = ["shared/protocol/replica.js", "shared/protocol/fold.js", "shared/protocol/view.js", "shared/protocol/viewdiff.js", "shared/protocol/simlayer.js",
  "shared/structure.js", "shared/terrainmath.js", "shared/terrainshape.js", "shared/waterkind.js", "shared/forecast.js", "shared/sim.js", "shared/water.js",
  "shared/particle_effects.js", "shared/picture.js", "shared/sound.js", "client/lib/motioneval.js", "mcpl/effective.ts"];
function forbidden() {
  const t = new Bun.Transpiler({ loader: "ts" }); const out = new Map<string, string>(CORE.map((n) => [n, "the fold"]));
  for (const f of EVALUATORS) for (const n of t.scan(fs.readFileSync(path.join(ENGINE, f), "utf8")).exports) if (!out.has(n)) out.set(n, f);
  return out;
}

/** Every name a file declares (functions, classes, methods, variables incl. destructured, function-valued keys). */
function declared(file: string) {
  const sf = ts.createSourceFile(file, fs.readFileSync(file, "utf8"), ts.ScriptTarget.Latest, true);
  const names: { name: string; line: number }[] = [];
  const add = (n: ts.Node | undefined) => { if (n && (ts.isIdentifier(n) || ts.isStringLiteral(n))) names.push({ name: n.text, line: sf.getLineAndCharacterOfPosition(n.getStart()).line + 1 }); };
  const bind = (b: ts.BindingName) => { if (ts.isIdentifier(b)) add(b); else for (const e of b.elements) if (!ts.isOmittedExpression(e)) bind(e.name); };
  const walk = (n: ts.Node) => {
    if (ts.isFunctionDeclaration(n) || ts.isClassDeclaration(n) || ts.isMethodDeclaration(n) || ts.isFunctionExpression(n)) add(n.name);
    if (ts.isVariableDeclaration(n)) bind(n.name);
    if (ts.isPropertyAssignment(n) && n.initializer && (ts.isArrowFunction(n.initializer) || ts.isFunctionExpression(n.initializer))) add(n.name);
    ts.forEachChild(n, walk);
  };
  walk(sf);
  return names;
}

test("G2a src/ imports only node:/bun builtins, its own files, and engine/", () => {
  const t = new Bun.Transpiler({ loader: "ts" }); const bad: string[] = [];
  for (const f of files(SRC)) for (const im of t.scanImports(fs.readFileSync(f, "utf8"))) {
    const p = im.path;
    if (p.startsWith("node:") || p === "bun") continue;
    if (!p.startsWith(".")) { bad.push(`${path.relative(ROOT, f)}: ${p} (a package)`); continue; }
    const to = path.resolve(path.dirname(f), p);
    if (!(to.startsWith(SRC + path.sep) || to.startsWith(ENGINE + path.sep) || to === path.join(ROOT, "engine.pin"))) bad.push(`${path.relative(ROOT, f)}: ${p}`);
  }
  expect(bad).toEqual([]);
  expect(files(SRC).length).toBeGreaterThan(0);
});

test("G2b src/ defines none of the fold's or the evaluators' names (AST scan)", () => {
  const F = forbidden(); const bad: string[] = [];
  expect(F.size).toBeGreaterThan(100);   // the scan read the engine's exports
  for (const f of files(SRC)) for (const d of declared(f)) if (F.has(d.name)) bad.push(`${path.relative(ROOT, f)}:${d.line} defines ${d.name} (${F.get(d.name)})`);
  expect(bad).toEqual([]);
});

test("G2c the bridge runs the engine's replica (imports it), not one of its own", () => {
  const t = new Bun.Transpiler({ loader: "ts" });
  const im = files(SRC).flatMap((f) => t.scanImports(fs.readFileSync(f, "utf8")).map((i) => path.resolve(path.dirname(f), i.path)));
  expect(im).toContain(path.join(ENGINE, "shared/protocol/replica.js"));
});

test("G2d engine/ is the pinned upstream commit, unmodified", () => {
  const pin = fs.readFileSync(path.join(ROOT, "engine.pin"), "utf8").trim();
  expect(pin).toMatch(/^[0-9a-f]{40}$/);
  expect(git("-C", "engine", "rev-parse", "HEAD").out).toBe(pin);
  expect(git("ls-files", "-s", "engine").out.split(/\s+/)[1]).toBe(pin);   // the submodule's gitlink says the same
  expect(git("-C", "engine", "diff", "--quiet").code).toBe(0);
  expect(git("-C", "engine", "diff", "--cached", "--quiet").code).toBe(0);
  expect(git("-C", "engine", "status", "--porcelain", "--untracked-files=all").out).toBe("");
  expect(git("-C", "engine", "branch", "-r", "--contains", pin).out).not.toBe("");   // an upstream commit, not a local one
});
