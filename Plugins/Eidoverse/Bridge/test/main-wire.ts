// test/main-wire.ts — the main protocol's message names in the renderer's C++, counted per file (G1); usage: bun test/main-wire.ts <unreal dir>
import fs from "node:fs";
import path from "node:path";

export const PRIVATE = "Plugins/Eidoverse/Source/EidoverseClient/Private";
/** The main protocol's names a ported renderer never sends or reads; `verb` only as a message type. */
export const TOKENS: Record<string, RegExp> = {
  verb: /TEXT\("type"\)\s*,\s*TEXT\("verb"\)/g,
  "SendVerb(…)": /\bSend(Interaction)?Verb(Json)?\s*\(/g,   // the C++'s own verb senders, called from each build and command site
  ...Object.fromEntries(["lease", "render-scene", "whisper", "access-state", "access-directory", "world-access", "snap-result", "snap", "emotes", "inspect"]
    .map((t) => [t, new RegExp(`TEXT\\("${t}"\\)`, "g")])),
};

export function scanWire(unreal: string) {
  const dir = path.join(unreal, PRIVATE), out: Record<string, Record<string, number>> = {};
  for (const f of fs.readdirSync(dir).filter((n) => n.endsWith(".cpp")).sort()) {
    const src = fs.readFileSync(path.join(dir, f), "utf8");
    for (const [t, re] of Object.entries(TOKENS)) { const n = src.match(re)?.length ?? 0; if (n) (out[t] ??= {})[f] = n; }
  }
  return out;
}

if (import.meta.main) {
  const unreal = process.argv[2]; if (!unreal) { console.error("usage: bun test/main-wire.ts <unreal dir>"); process.exit(2); }
  fs.writeFileSync(path.join(import.meta.dir, "fixtures/main-wire.json"), JSON.stringify(scanWire(unreal), null, 1) + "\n");
}
