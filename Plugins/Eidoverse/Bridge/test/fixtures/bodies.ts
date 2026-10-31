// test/fixtures/bodies.ts — the renderer's holding and seat sends (eidoverse-unreal@59df129, Plugins/Eidoverse/Source/EidoverseClient/Private/) in the protocol's form.
// lease went with client-side physics: the authority holds things now (ADAPTER-DESIGN §3.6). Each today-send names its replacement.

let n = 0;
export const request = (verb: string, args: Record<string, unknown>) => { const id = `bd-${++n}`; return { id, raw: JSON.stringify({ type: "request", id, verb, args }) }; };

/** A pickup (the lease claim of EidoverseInteractions.cpp:88, made for carrying) → grab. */
export const grab = (id: string, hand?: "left" | "right") => request("grab", { id, ...(hand ? { hand } : {}) });
/** The lease release with a pose (EidoverseInteractions.cpp:60-64, p/q) → put {id, pos?, q?}. */
export const put = (id: string, pos?: number[]) => request("put", { id, ...(pos ? { pos } : {}) });
/** A release with the hand's velocity (the lease state stream of EidoverseInteractions.cpp:60, ending) → throw {id, v, pos?, q?, t?}. */
export const throwIt = (id: string, v: number[]) => request("throw", { id, v });
/** Letting go where it is (ClearInteractions, EidoverseInteractions.cpp:67) → drop. */
export const drop = (id: string) => request("drop", { id });
/** A kick (EidoverseInteractions.cpp:56, {id, power, dir}) → punt {id, v = dir·power}. */
export const punt = (id: string, dir: number[], power: number) => request("punt", { id, v: dir.map((x) => x * power) });
/** Where the body is: the authority tracks it from move (presence, never logged). */
export const move = (to: number[]) => request("move", { to });
/** The body's pose (EidoverseBridge.cpp:845-882), with the seat it rides on when seated (client/lib/net.js:444-449 pose.seat). */
export const pose = (p: number[], seat?: { to: string; pose?: string }) => ({ raw: JSON.stringify({ type: "pose", pose: { p, q: [0, 0, 0, 1], yaw: 0, speed: 0, clip: seat ? "sitchair" : "idle", ...(seat ? { seat } : {}) } }) });
/** Sitting (EidoverseSeats.cpp:121-123, mount {id: me, to, slot}) → use {id: to, action, args: {socket: slot}}. */
export const sit = (host: string, socket?: string, action = "sit") => request("use", { id: host, action, ...(socket ? { args: { socket } } : {}) });
// Standing up (EidoverseSeats.cpp:164-175, dismount {id: me, pos, yaw}): the next pose has no seat, then move to the landing spot.
// A helm (the lease of EidoverseLocomotion.cpp:76, 113, 213) → use its helm socket, then drive {id, pos, q}: not exercised here.
