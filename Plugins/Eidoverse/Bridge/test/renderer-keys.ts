// test/renderer-keys.ts — the JSON keys the renderer's C++ reads off the render model (T3b); usage: bun test/renderer-keys.ts <unreal dir>
import fs from "node:fs";
import path from "node:path";

export const PRIVATE = "Plugins/Eidoverse/Source/EidoverseClient/Private";
const READERS = /^(Num|Str|Obj|Arr|Vec|Bool|Flag|SStr|SNum|SFlag|PicStr|ObjectField|Vector3|CNum|IStr|IFlag|IFNum|Read|Try\w*Field|Get\w*Field|HasField|HasTypedField)$/;
/** Files whose every reader call is a render-model read, and files where only reads off the item's `interact` are. */
const SCENE = /^EidoverseScene\w*\.cpp$/, INTERACT = ["EidoverseInteractions.cpp", "EidoverseUIBuildTools.cpp"];

/** The call a TEXT("…") at `at` is an argument of: its name and its first argument's text. */
function enclosing(src: string, at: number) {
  let depth = 0;
  for (let i = at - 1; i >= 0; i--) {
    const ch = src[i];
    if (ch === ")") depth++;
    else if (ch === "(") { if (depth === 0) { const m = /([A-Za-z_]\w*)\s*(<[^<>]*>)?\s*$/.exec(src.slice(Math.max(0, i - 80), i)); return { name: m?.[1] ?? "", argStart: i + 1 }; } depth--; }
    else if (ch === ";" || ch === "{" || ch === "}") return null;
  }
  return null;
}

export function scanKeys(unreal: string) {
  const dir = path.join(unreal, PRIVATE), out: Record<string, string[]> = {};
  for (const f of fs.readdirSync(dir).sort()) {
    const scene = SCENE.test(f), inter = INTERACT.includes(f); if (!scene && !inter) continue;
    const src = fs.readFileSync(path.join(dir, f), "utf8");
    for (const m of src.matchAll(/TEXT\("([^"]+)"\)/g)) {
      const c = enclosing(src, m.index!); if (!c || !READERS.test(c.name)) continue;
      const first = src.slice(c.argStart, m.index!);
      if (inter && !/Interact|\bInfo\b/.test(first)) continue;
      const line = src.slice(0, m.index!).split("\n").length;
      (out[m[1]] ??= []).push(`${f}:${line}`);
    }
  }
  return Object.fromEntries(Object.entries(out).sort(([a], [b]) => a.localeCompare(b)));
}

if (import.meta.main) console.log(JSON.stringify(scanKeys(process.argv[2]), null, 1));
