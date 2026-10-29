// test/fixtures/access.ts — the access tab's grants (EidoverseUIAccess.cpp:267-275 AccessGrant → world-access {id, role?, for?, revoke?}) in the protocol's form.
// The door was the wildcard's rung; it is @world access + default-role now (shared/protocol/access.js DOORS, doorWrites).
import { BASE } from "../../engine/shared/protocol/roles.js";

let n = 0;
const request = (src: string, verb: string, args: Record<string, unknown>) => { const id = `ax-${++n}`; return { id, src, raw: JSON.stringify({ type: "request", id, verb, args }) }; };
const set = (component: string, value: unknown) => ({ verb: "set", args: { id: "@world", component, value } });
const batch = (...ops: any[]) => ops.length === 1 ? ops[0] : { verb: "batch", args: { ops } };

/** The tab's four doors (EidoverseUIAccess.cpp:24-29: builder, visitor, spectator, none) → the protocol's (open, public, watch, invite-only). */
export const DOOR: Record<string, { to: string; req: () => { id: string; src: string; raw: string } }> = {
  builder: { to: "open", req: () => { const b = batch(set("access", { join: "anyone" }), set("default-role", { role: "builder" })); return request("EidoverseUIAccess.cpp:296 (door:builder)", b.verb, b.args); } },
  visitor: { to: "public", req: () => { const b = batch(set("access", { join: "anyone" }), set("default-role", { role: BASE })); return request("EidoverseUIAccess.cpp:296 (door:visitor)", b.verb, b.args); } },
  spectator: { to: "watch", req: () => { const s = set("access", { join: "listed", watch: "anyone" }); return request("EidoverseUIAccess.cpp:296 (door:spectator)", s.verb, s.args); } },
  none: { to: "invite-only", req: () => { const s = set("access", { join: "listed" }); return request("EidoverseUIAccess.cpp:296 (door:none)", s.verb, s.args); } },
};
/** setrole + setspan (EidoverseUIAccess.cpp:299-300): a role, until when (the C++ turns "1h" into ms on the server's clock). */
export const grant = (id: string, role: string, until?: number) => { const s = set(`roles:${id}`, { role, ...(until ? { until } : {}) }); return request("EidoverseUIAccess.cpp:299-300", s.verb, s.args); };
/** rm (EidoverseUIAccess.cpp:301, revoke: true) → delete the grant. */
export const revoke = (id: string) => request("EidoverseUIAccess.cpp:301", "delete", { id: "@world", component: `roles:${id}` });
