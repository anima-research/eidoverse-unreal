// src/access.ts — slice D: bridge/access-state from @world's access, default-role and roles:*; bridge/access-directory through whois (ADAPTER-DESIGN §3.3, §7.2; T10).
import type { Ctx, Slice } from "./slice.ts";
import { doorOf, listedIn, DOORS } from "../engine/shared/protocol/access.js";
import { actorName } from "../engine/shared/protocol/names.js";
import { hereAtJoin, arrived, left, type Here } from "./people.ts";

/** ADAPTER-DESIGN §3.3 access-directory row: the protocol has no directory search; whois knows only the people this world has seen. */
export const DIRECTORY_GAP = "search by name isn't available on this server: pick someone here, or paste their identity (u-…)";
const ACCESS_KEYS = (k: string) => k === "access" || k === "default-role" || k === "role-defs" || k.startsWith("roles:");
const IDENTITY = /^u-[a-z2-7]{20}$/;

type State = { here: Here; rights: any; asked: boolean };
const stateOf = (ctx: Ctx) => ctx.own<State>(() => ({ here: new Map(), rights: null, asked: false }));

/** The access tab's facts, as the fold has them now (access.js reads them the same way for the server's door). */
export function accessState(ctx: Ctx, st: State) {
  const wc = ctx.replica?.st?.entities?.["@world"]?.c ?? {}, now = Date.now(), you = ctx.replica?.you ?? null;
  const ids = new Set(st.here.values());
  const members = Object.keys(wc).filter((k) => k.startsWith("roles:") && listedIn(wc, k.slice(6), now)).map((k) => {
    const id = k.slice(6), g = wc[k];
    return { id, name: actorName(id), role: g.role, ...(Number.isSafeInteger(g.until) ? { until: g.until } : {}), here: ids.has(id) };
  });
  const here = [...st.here].filter(([, id]) => !id || !listedIn(wc, id, now)).map(([conn, id]) => ({ id: conn, identity: id, name: id && actorName(id) !== id ? actorName(id) : conn }));
  const mine = you && listedIn(wc, you, now) ? wc[`roles:${you}`] : null;
  return { type: "bridge/access-state", world: String(ctx.join?.world ?? ""), door: doorOf(wc), doors: DOORS, access: wc.access ?? null,
    defaultRole: wc["default-role"]?.role ?? "inhabitant", members, here,
    you: { id: you, role: st.rights?.role ?? mine?.role ?? null, ...(Number.isSafeInteger(mine?.until) ? { until: mine.until } : {}) }, rights: st.rights };
}
const push = (ctx: Ctx) => { const st = stateOf(ctx); if (st.asked && ctx.replica?.active) ctx.send(accessState(ctx, st)); };

/** bridge/access-directory {q}: whois (the people this world has seen, or one identity); nobody found is the §3.3 gap. */
async function directory(ctx: Ctx, m: any) {
  const q = typeof m.q === "string" ? m.q.trim().slice(0, 64) : "";
  if (!q) return ctx.send({ type: "bridge/access-directory", q: m.q ?? "", results: [] });
  const r = await ctx.request("whois", IDENTITY.test(q) ? { id: q } : { name: q });
  if (!r?.ok) return ctx.send({ type: "bridge/access-directory", q: m.q, results: [], error: r?.refused?.reason ?? "couldn't look that up just now" });
  const results = (r.people ?? []).map((p: any) => ({ name: p.name, sub: p.id, id: p.id, kind: p.declares ?? "person", role: p.role, here: !!p.here, ...(p.guest ? { guest: true } : {}), ...(p.banned ? { banned: true } : {}) }));
  ctx.send({ type: "bridge/access-directory", q: m.q, results, ...(results.length ? {} : { error: DIRECTORY_GAP }) });
}

export default {
  name: "access",
  local: {
    "bridge/access-state": (ctx) => { const st = stateOf(ctx); st.asked = true; if (!ctx.replica?.active) return ctx.error("bridge/access-state", "not-joined", "join first"); ctx.send(accessState(ctx, st)); },
    "bridge/access-directory": directory,
  },
  adopted(ctx, snap, cut) { const st = stateOf(ctx); st.here = hereAtJoin(snap, cut); st.rights = snap?.yourRights ?? st.rights; push(ctx); },
  ingested(ctx, _r, msg) {
    const touched = (msg.entries ?? []).some((e: any) => e?.kind === "change" && (e.body?.id === "@world" && ACCESS_KEYS(String(e.body?.comp ?? ""))
      || (Array.isArray(e.body?.ops) && e.body.ops.some((o: any) => o?.id === "@world" && ACCESS_KEYS(String(o?.comp ?? ""))))));
    if (touched) push(ctx);
  },
  server: {
    "your-rights": (ctx, m) => { stateOf(ctx).rights = m.rights ?? null; push(ctx); },
    arrive: (ctx, m) => { arrived(stateOf(ctx).here, m); push(ctx); },
    leave: (ctx, m) => { left(stateOf(ctx).here, m); push(ctx); },
  },
} satisfies Slice;
