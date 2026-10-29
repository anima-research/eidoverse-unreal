// src/bodies.ts — slice D: holds → `held` on bridge/scene transforms at the holder's hand, seats[], bridge/ride (ADAPTER-DESIGN §3.6; T8, T9).
import type { Ctx, Slice } from "./slice.ts";
import { HAND_Y } from "../engine/shared/protocol/physconst.js";
import { parent, unparent } from "../engine/shared/protocol/requests.js";
import { socketsIn } from "../engine/shared/sockbody.js";
import { hereAtJoin, arrived, left, connOf, type Here } from "./people.ts";

type State = { holds: Record<string, any>; poses: Map<string, any>; here: Here };
// keyed by the replica each adopt makes, which scene.ts's ctx sees too (a slice's own state is per slice)
const byReplica = new WeakMap<object, State>();
const stateOf = (ctx: Ctx) => ctx.own<State>(() => ({ holds: {}, poses: new Map(), here: new Map() }));

/** Where a held thing is drawn: its holder's hand (the authority's grip point, HAND_Y above the feet), or null when no frame has said where they are. */
function handOf(st: State, by: string) {
  const who = connOf(st.here, by), p = who ? st.poses.get(who)?.p : null;
  return Array.isArray(p) && p.length === 3 && p.every(Number.isFinite) ? [p[0], p[1] + HAND_Y, p[2]] : null;
}

const isObj = (v: any): v is Record<string, any> => !!v && typeof v === "object" && !Array.isArray(v);
/** Every seat: a socket child with a station (sockbody.js), its host, and who or what is in it (a body by its frames' pose.seat, a thing by its parent edge). */
export function seatsOf(view: any, st: State) {
  const E = view.entities, out: any[] = [];
  const sitting = new Map<string, string>();
  for (const [who, p] of st.poses) if (typeof p?.seat?.to === "string" && st.here.has(who)) sitting.set(p.seat.to, who);
  for (const [id, r] of Object.entries<any>(E)) {
    if (!isObj(r.socket) || !isObj(r.station) || !r.parent?.to) continue;
    const thing = Object.keys(E).find((k) => E[k].parent?.to === id) ?? null;
    out.push({ seat: id, host: r.parent.to, name: String(r.socket.name ?? id), action: r.station.action ?? "sit", pose: r.station.anim ?? r.station.pose ?? null,
      occupant: sitting.get(id) ?? null, thing });
  }
  return out;
}

/** scene.ts's one call: mark held things and list the seats on a render model about to be sent. */
export function addBodies(ctx: Ctx, msg: any) {
  const st = ctx.replica ? byReplica.get(ctx.replica) : null; if (!st) return;
  for (const t of msg.transforms ?? []) {
    const h = st.holds[t.id]; if (!h || typeof h.by !== "string") continue;
    t.held = { by: h.by, hand: h.hand ?? "right", ...(h.grip ? { grip: h.grip } : {}), ...(h.offset ? { offset: h.offset } : {}), ...(h.second ? { second: h.second } : {}) };
    const at = handOf(st, h.by); if (at) { t.p = at; delete t.motion; delete t.sim; }
  }
  msg.seats = seatsOf(ctx.view(), st);
}

/** bridge/ride {req?, to, socket?, action?} sits this body (use); with `id`, that thing rides `to` (requests.js parent). The authority decides. */
async function ride(ctx: Ctx, m: any) {
  const view = ctx.view(); if (!view) return ctx.error(m.type, "not-joined", "join first");
  if (typeof m.to !== "string") return ctx.error(m.type, "shape", "bridge/ride {to, socket?} (your body), or {id, to, socket?, pos?, yaw?} (a thing)");
  let q: { verb: string; args: any };
  if (typeof m.id === "string") q = parent(view, m.id, m.to, { socket: m.socket ?? null, pos: m.pos ?? null, yaw: m.yaw ?? null });
  else {
    const socks = socketsIn(view.entities, m.to).filter((x: any) => m.socket == null || x.name === String(m.socket));
    const action = typeof m.action === "string" ? m.action : view.entities[socks[0]?.id]?.station?.action ?? "sit";
    q = { verb: "use", args: { id: m.to, action, ...(m.socket != null ? { args: { socket: String(m.socket) } } : {}) } };
  }
  ctx.send({ type: "bridge/ridden", req: m.req ?? null, fn: "ride", reply: await ctx.request(q.verb, q.args) });
}
/** bridge/unride {req?, pos, yaw?} walks this body off to `pos` (its frames stop naming the seat); with `id`, that thing stops riding where it stands. */
async function unride(ctx: Ctx, m: any) {
  const view = ctx.view(); if (!view) return ctx.error(m.type, "not-joined", "join first");
  const q = typeof m.id === "string" ? unparent(view.entities[m.id], m.id, { pos: m.pos, ...(Number.isFinite(m.yaw) ? { yaw: m.yaw } : {}) }) : { verb: "move", args: { to: m.pos } };
  ctx.send({ type: "bridge/ridden", req: m.req ?? null, fn: "unride", reply: await ctx.request(q.verb, q.args) });
}

export default {
  name: "bodies",
  local: { "bridge/ride": ride, "bridge/unride": unride },
  adopted(ctx, snap, cut) {
    const st = stateOf(ctx);
    st.holds = cut?.holds && typeof cut.holds === "object" ? cut.holds : {};
    st.here = hereAtJoin(snap, cut);
    for (const p of snap?.present ?? []) if (p?.pose && typeof p.id === "string") st.poses.set(p.id, p.pose);
    if (ctx.replica) byReplica.set(ctx.replica, st);
  },
  server: {
    holds(ctx, m) { stateOf(ctx).holds = m.holds && typeof m.holds === "object" ? m.holds : {}; },
    frame(ctx, m) { const st = stateOf(ctx); for (const [who, p] of Object.entries<any>(m.poses ?? {})) if (p && typeof p === "object") st.poses.set(who, p); },
    arrive(ctx, m) { arrived(stateOf(ctx).here, m); },
    leave(ctx, m) { const st = stateOf(ctx); left(st.here, m); st.poses.delete(m.id); },
  },
} satisfies Slice;
