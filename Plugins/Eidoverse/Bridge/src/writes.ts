// src/writes.ts — slice C: bridge/do {req?, fn, …} → the engine's requests.js builders → request; bridge/done {req, fn, reply}, ids decoded (ADAPTER-DESIGN §3.5; T6, T7).
import type { Ctx, Slice } from "./slice.ts";
import * as R from "../engine/shared/protocol/requests.js";
import { enc } from "../engine/shared/protocol/names.js";
import { idOut } from "../engine/shared/protocol/view.js";
import { PERSON_ID } from "../engine/shared/protocol/ids.js";
import { REMOVED_VERBS } from "../engine/shared/protocol/verbspec.js";

type Req = { verb: string; args: Record<string, any> };
/** A refusal the bridge answers itself (nothing went up): never a rights call, only what it can't build (§7.2). */
class Local extends Error { constructor(readonly code: string, reason: string, readonly fix?: string) { super(reason); } }

/** Who is here now: display id → identity (snapshot.present, arrive, leave). */
const people = (ctx: Ctx) => ctx.own(() => new Map<string, string>());
/** A name or an identity → the identity roles and moderation key on (§3.5 grant, kick/ban/unban). Never a rights call. */
function identity(ctx: Ctx, who: unknown): string {
  if (typeof who !== "string" || !who) throw new Local("bad-args", "name someone: their name, or their identity (u-…)");
  if (PERSON_ID.test(who)) return who;
  const ids = new Set<string>(), here = people(ctx).get(who);
  if (here) ids.add(here);
  for (const [id, n] of Object.entries(ctx.replica?.names ?? {})) if (n === who && PERSON_ID.test(id)) ids.add(id);   // the log's names (replica.js learn)
  if (ids.size > 1) throw new Local("ambiguous", `more than one person is called ${who}`, `give their identity: ${[...ids].sort().join(" or ")}`);
  if (!ids.size) throw new Local("unknown-person", "who is that? they must be here, or give their identity (u-…)");
  return [...ids][0];
}
const thing = (ctx: Ctx, id: unknown) => {
  const e = typeof id === "string" ? ctx.view()?.entities?.[id] : undefined;
  if (!e) throw new Local("not-here", `there's no ${String(id)} in this world as the bridge folds it`, "pick it again: it may have gone");
  return e;
};
const opt = (o: Record<string, unknown>, keys: string[]) => Object.fromEntries(keys.filter((k) => o[k] !== undefined).map((k) => [k, o[k]]));
const withArgs = (r: Req, extra: Record<string, unknown>) => ({ verb: r.verb, args: { ...r.args, ...extra } });
const world = (component: string, value: unknown) => (value === null ? R.unset("@world", component) : R.set("@world", component, value));

/** fn → the request it sends, from the renderer's message `m` (view spelling: requests.js encodes). */
const FNS: Record<string, (ctx: Ctx, m: any) => Req | Promise<any>> = {
  // build (UIBuild, UIBuildTools, UIStructure, UIInspect)
  create: (_c, m) => R.create(m.ref ?? null, { model: m.model ?? null, transform: m.transform ?? {}, components: m.components ?? {}, from: m.from ?? null }),
  setTransform: (c, m) => withArgs(R.setTransform(thing(c, m.id), m.id, m.patch ?? {}), opt(m, ["base", "dry"])),
  set: (_c, m) => withArgs(R.set(m.id, m.component, m.value, { merge: !!m.merge }), opt(m, ["base", "force", "dry"])),
  unset: (_c, m) => withArgs(R.unset(m.id, m.component), opt(m, ["force", "dry"])),
  remove: (_c, m) => R.remove(m.id),
  use: (_c, m) => R.use(m.id, m.action ?? null, m.args ?? null),
  asset: (_c, m) => R.set("@world", `assets:${enc(String(m.path))}`, { name: m.name, path: m.path }),   // keyed as client/lib/generate.js keys it
  // world (UIWorld, Commands): value null deletes the component
  sky: (_c, m) => ({ verb: "sky", args: m.value ?? {} }),
  terrain: (_c, m) => world("terrain", m.value),
  grass: (_c, m) => world("grass", m.value),
  epoch: (_c, m) => withArgs(world("physics-engine", m.engine == null ? null : { engine: m.engine, ...opt(m, ["tickMs"]) }), opt(m, ["force"])),
  // a script (main's behavior): store the source, then bind it with the reply's src; a refused upload stops there
  script: async (c, m) => {
    const up = await c.request("upload", { text: m.text });
    if (!up?.ok) return up;
    const bind = R.set(m.id, `script:${enc(String(m.name))}`, { ...(m.value ?? {}), src: up.src });
    return { ...(await c.request(bind.verb, bind.args)), src: up.src };
  },
  // moderation (Commands): a standing grant is merged, as client/lib/people.js does
  grant: (c, m) => { const who = identity(c, m.who);
    return R.set("@world", `roles:${who}`, { role: m.role, ...opt(m, ["until", "can", "note"]) }, { merge: !!c.view()?.roles?.[who] }); },
  revoke: (c, m) => R.unset("@world", `roles:${identity(c, m.who)}`),
  kick: (c, m) => ({ verb: "kick", args: { who: identity(c, m.who), ...opt(m, ["minutes", "reason"]) } }),
  ban: (c, m) => ({ verb: "ban", args: { who: identity(c, m.who), ...opt(m, ["days", "reason"]) } }),
  unban: (c, m) => ({ verb: "unban", args: { who: identity(c, m.who) } }),
  // GAP (§7.2): no verb serves it; the fix is the engine's own pointer
  force: () => { throw new Local("gap", "there's no force blast in this protocol", REMOVED_VERBS.force.fix); },
};

/** The reply with its minted ids in view spelling (§3.5: the C++ re-keys its `$b<n>` ghost by them). */
const decoded = (reply: any) => (reply?.ids && typeof reply.ids === "object" ? { ...reply, ids: Object.fromEntries(Object.entries(reply.ids).map(([k, v]) => [k, idOut(String(v))])) } : reply);

async function doFn(ctx: Ctx, m: any) {
  const req = typeof m.req === "string" || typeof m.req === "number" ? m.req : null, fn = m.fn;
  const refuse = (code: string, reason: string, fix?: string) => ctx.send({ type: "bridge/error", for: "bridge/do", req, fn: fn ?? null, code, reason, ...(fix ? { fix } : {}) });
  const f = typeof fn === "string" && Object.hasOwn(FNS, fn) ? FNS[fn] : null;
  if (!f) return refuse("unknown-fn", `bridge/do fn is one of ${Object.keys(FNS).join(", ")}`);
  if (!ctx.replica?.active) return refuse("not-joined", "join a world first");
  let out: any;
  try { out = f(ctx, m); out = typeof out?.then === "function" ? await out : await ctx.request(out.verb, out.args); }
  catch (e) { if (e instanceof Local) return refuse(e.code, e.message, e.fix); throw e; }
  ctx.send({ type: "bridge/done", req, fn, reply: decoded(out) });
}

export default {
  name: "writes",
  local: { "bridge/do": doFn },
  server: {
    arrive: (ctx, m) => { if (typeof m.id === "string" && typeof m.actorId === "string") people(ctx).set(m.id, m.actorId); },
    leave: (ctx, m) => { if (typeof m.id === "string") people(ctx).delete(m.id); },
  },
  adopted: (ctx, snap) => { const here = people(ctx); here.clear();
    for (const p of snap?.present ?? []) if (typeof p?.id === "string" && typeof p.actorId === "string") here.set(p.id, p.actorId); },
} satisfies Slice;
