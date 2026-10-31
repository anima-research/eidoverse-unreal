// src/slices.ts — the bridge/* handler table: one file per slice, registered here once (ADAPTER-DESIGN §6).
import type { Slice } from "./slice.ts";
import scene from "./scene.ts";
import chat from "./chat.ts";
import writes from "./writes.ts";
import bodies from "./bodies.ts";
import access from "./access.ts";

const CORE = new Set(["bridge/hello", "bridge/status", "bridge/ready", "bridge/error"]);
const NOT_TAPPABLE = new Set(["snapshot", "log"]);

/** The table, checked: every local type is bridge/* and owned once; nobody taps snapshot or log. */
export function table(slices: Slice[]) {
  const local = new Map<string, { slice: Slice; fn: NonNullable<Slice["local"]>[string] }>();
  const server = new Map<string, { slice: Slice; fn: NonNullable<Slice["server"]>[string] }[]>();
  for (const s of slices) {
    for (const [t, fn] of Object.entries(s.local ?? {})) {
      if (!t.startsWith("bridge/")) throw new Error(`${s.name}: a local message is named bridge/*, not ${t}`);
      if (CORE.has(t) || local.has(t)) throw new Error(`${s.name}: ${t} is already ${CORE.has(t) ? "the core's" : local.get(t)!.slice.name + "'s"}`);
      local.set(t, { slice: s, fn });
    }
    for (const [t, fn] of Object.entries(s.server ?? {})) {
      if (NOT_TAPPABLE.has(t) || t.startsWith("bridge/")) throw new Error(`${s.name}: can't tap ${t}`);
      server.set(t, [...(server.get(t) ?? []), { slice: s, fn }]);
    }
  }
  return { slices, local, server };
}

export const SLICES: Slice[] = [scene, chat, writes, bodies, access];
