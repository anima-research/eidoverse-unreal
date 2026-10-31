// src/people.ts — who is here, by connection id and identity, kept from the join and arrive/leave (bodies.ts and access.ts each keep one).

/** connection id (frames, present[], arrive/leave) → identity (u-…, holds and roles), or null when the server didn't say. */
export type Here = Map<string, string | null>;

/** The join's roster: everyone present, and this connection (snapshot.you is its id, the cut's `you` its identity). */
export function hereAtJoin(snap: any, cut: any): Here {
  const h: Here = new Map();
  for (const p of Array.isArray(snap?.present) ? snap.present : []) if (typeof p?.id === "string") h.set(p.id, typeof p.actorId === "string" ? p.actorId : null);
  if (typeof snap?.you === "string") h.set(snap.you, typeof cut?.you === "string" ? cut.you : null);
  return h;
}
export const arrived = (h: Here, m: any) => { if (typeof m?.id === "string") h.set(m.id, typeof m.actorId === "string" ? m.actorId : null); };
export const left = (h: Here, m: any) => { if (typeof m?.id === "string") h.delete(m.id); };
/** The connection id an identity is here as (the first, if several), or null. */
export const connOf = (h: Here, identity: string) => { for (const [k, v] of h) if (v === identity) return k; return null; };
