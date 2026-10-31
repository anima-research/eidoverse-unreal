// src/chat.ts — slice B: bridge/say from the fold's says, bridge/narration for logged role and asset acts (ADAPTER-DESIGN §3.4 log row; T4).
import type { Ctx, Slice } from "./slice.ts";
import { opsOf } from "../engine/shared/protocol/fold.js";
import { dec, actorName } from "../engine/shared/protocol/names.js";

const named = (id: unknown) => (id === "system" ? "the world" : actorName(id));
const ROLE = "roles:", ASSET = "assets:";

/** What a logged change says to someone watching (the C++'s NarrateLogEntry, EidoverseProtocol.cpp:289-309), or null. */
export function narrationOf(e: any): string | null {
  if (e?.kind !== "change") return null;
  let ops: any[]; try { ops = opsOf(e); } catch { return null; }
  const lines: string[] = [];
  for (const o of ops) {
    if (o.id !== "@world" || typeof o.comp !== "string") continue;
    if (o.comp.startsWith(ROLE)) {
      const who = named(o.comp.slice(ROLE.length));
      if (o.verb === "delete") lines.push(`${named(e.actor)} took ${who} off the list`);
      else if (o.verb === "set" && typeof o.value?.role === "string")
        lines.push(`${named(e.actor)} made ${who} ${o.value.role}${Number.isSafeInteger(o.value.until) ? ` until ${new Date(o.value.until).toISOString().slice(0, 16).replace("T", " ")} UTC` : ""}`);
    } else if (o.comp.startsWith(ASSET) && o.verb === "set") lines.push(`${String(o.value?.name ?? dec(o.comp.slice(ASSET.length)))} joined the world's palette`);
  }
  return lines.length ? lines.join("; ") : null;
}

const stateOf = (ctx: Ctx) => ctx.own(() => ({ narrated: -1 }));

export default {
  name: "chat",
  adopted(ctx) { stateOf(ctx).narrated = ctx.replica?.head ?? -1; },
  ingested(ctx, r, msg) {
    for (const s of r.says) ctx.send({ type: "bridge/say", seq: s.seq, actor: s.by, name: s.actor, text: s.text,
      ...(s.to ? { to: s.to } : {}), ...(s.spoken ? { spoken: true, utt: s.utt, ...(s.t0 !== undefined ? { t0: s.t0 } : {}) } : {}), ts: s.ts });
    const st = stateOf(ctx), from = Number(msg.from);
    (msg.entries ?? []).forEach((e: any, i: number) => {
      const seq = from + i; if (!(seq > st.narrated) || seq > ctx.replica.head) return;
      st.narrated = seq;
      const text = narrationOf(e); if (text) ctx.send({ type: "bridge/narration", text, cause: Number.isInteger(e.cause) ? e.cause : seq });
    });
  },
} satisfies Slice;
