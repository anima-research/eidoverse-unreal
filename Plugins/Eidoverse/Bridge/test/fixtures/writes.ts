// test/fixtures/writes.ts — the renderer's build and world writes as bridge/do, one per main-era send site it replaces (eidoverse-unreal@59df129, …/EidoverseClient/Private/; ADAPTER-DESIGN §3.5).

export type Do = { src: string; was: string; m: Record<string, any> };
const d = (src: string, was: string, m: Record<string, any>): Do => ({ src, was, m: { type: "bridge/do", ...m } });
const LAMP = "eidoverse/assets/models/lamp.glb", CRATE = "eidoverse/assets/models/crate_large_red.glb";

/** Creates: each names a placeholder `$b<n>` (the ghost's key until the reply's ids re-key it). */
export const creates = (n: number): Do[] => [
  d("EidoverseUIBuild.cpp:262", "spawn {id, lib, pos, yaw}", { req: `r${n}a`, fn: "create", ref: `$b${n}`, model: LAMP, transform: { pos: [2, 0, 3], q: [0, 0.3826834, 0, 0.9238795] } }),
  d("EidoverseUIBuild.cpp:416", "spawn {id, lib, pos, yaw, scale}", { req: `r${n}b`, fn: "create", ref: `$b${n + 1}`, model: LAMP, transform: { pos: [-2, 0, 3], scale: [1.5, 1.5, 1.5] } }),
  d("EidoverseUIBuild.cpp:402", "light {id, pos, color, intensity, range}", { req: `r${n}c`, fn: "create", ref: `$b${n + 2}`, transform: { pos: [0, 0.9, 4] }, components: { light: { color: 16767392, intensity: 16, range: 10 } } }),
  d("EidoverseUIStructure.cpp:172-173", "spawn {id: house-…, lib, pos, yaw} + comp structure {}", { req: `r${n}d`, fn: "create", ref: `$b${n + 3}`, model: CRATE, transform: { pos: [6, 0, 6] }, components: { structure: { levels: [] } } }),
];

/** Edits on an existing thing `id` (view spelling). */
export const edits = (id: string): Do[] => [
  d("EidoverseUIBuild.cpp:230", "place {id, pos, yaw, scale}", { req: "e1", fn: "setTransform", id, patch: { pos: [2.5, 0, 3], yaw: 1.5708, scale: 1.2 } }),
  d("EidoverseUIBuildTools.cpp:100", "place {id, pos, yaw, scale} (drag release)", { req: "e2", fn: "setTransform", id, patch: { pos: [3, 0, 3] } }),
  d("EidoverseUIInspect.cpp:75", "comp {id, type, data}", { req: "e3", fn: "set", id, component: "label", value: "reading lamp" }),
  d("EidoverseUIBuildTools.cpp:382", "comp {id, type, data} (SendComp)", { req: "e4", fn: "set", id, component: "openable", value: { open: false } }),
  d("EidoverseUIBuildTools.cpp:169", "comp {id, type, data: null}", { req: "e5", fn: "unset", id, component: "openable" }),
];
/** An edit on a light (a thing with no model: the authority keeps a light off a model, 05-lightpolicy). */
export const lightEdit = (id: string) => d("EidoverseUIBuildTools.cpp:277", "light {id, intensity}", { req: "e6", fn: "set", id, component: "light", value: { intensity: 20 }, merge: true });
export const asset = d("EidoverseUIBuildTools.cpp:257", "asset {name, path}", { req: "a1", fn: "asset", name: "My Lamp", path: "store/abcd0000abcd0000.glb" });
export const removeOf = (id: string, req = "x1") => d("EidoverseUIBuild.cpp:325", "remove {id}", { req, fn: "remove", id });

/** World writes (owner, or editWorld). */
export const worldWrites: Do[] = [
  d("EidoverseUIWorld.cpp:512", "sky {hours, azimuth, …}", { req: "w1", fn: "sky", value: { hours: 12, azimuth: 3.1416, rate: 0 } }),   // main sent azimuth 180 (degrees); the sky takes radians
  d("EidoverseUIWorld.cpp:529", "terrain {seed, size, segments, amplitude, flatRadius, layers}", { req: "w2", fn: "terrain", value: { kind: "noise", seed: 3, size: 160, segments: 200, amplitude: 2, flatRadius: 16, layers: [{ color: 0x5a8a3a, repeat: 16 }] } }),   // main sent a layer name; the terrain takes 0xRRGGBB
  d("EidoverseUIWorld.cpp:565", "grass {…planting, density}", { req: "w3", fn: "grass", value: { width: 20, depth: 20, height: 0.4, density: 0.5 } }),
  d("EidoverseUIWorld.cpp:544", "grass {clear: true}", { req: "w4", fn: "grass", value: null }),
  d("EidoverseCommands.cpp:344", "epoch {sim, tickMs}", { req: "w5", fn: "epoch", engine: "eidosim", tickMs: 66 }),
  d("EidoverseCommands.cpp:344", "epoch {sim: null}", { req: "w6", fn: "epoch", engine: null }),
];

/** Moderation by name (Commands.cpp: `/kick <name>` etc.); `who` is a display name or an identity. */
export const grant = (who: string, role: string) => d("EidoverseCommands.cpp:331", "grant {id, role}", { req: `g-${who}`, fn: "grant", who, role });
export const revoke = (who: string) => d("EidoverseCommands.cpp:338", "grant {id, revoke: true}", { req: `rv-${who}`, fn: "revoke", who });
export const kick = (who: string) => d("EidoverseCommands.cpp:252", "kick {id, reason}", { req: `k-${who}`, fn: "kick", who, reason: "testing" });
export const ban = (who: string) => d("EidoverseCommands.cpp:252", "ban {id, reason}", { req: `b-${who}`, fn: "ban", who, days: 1, reason: "testing" });
export const unban = (who: string) => d("EidoverseCommands.cpp:254", "unban {id}", { req: `u-${who}`, fn: "unban", who });
export const boom = d("EidoverseCommands.cpp:241-247", "force {at, power, radius}", { req: "boom", fn: "force", at: [0, 1, 0], power: 8, radius: 4 });
/** A script on a thing: upload its source, bind it (Commands.cpp:405 /script was behavior {id, remove} on main). */
export const script = (id: string) => d("EidoverseCommands.cpp:405", "behavior {id, …}", { req: "sc1", fn: "script", id, name: "bell",
  text: 'world.on("use", () => { world.set("self", "t/bell", { rung: (world.get("self", "t/bell")?.rung ?? 0) + 1 }); });', value: { scope: { ids: "self", comps: ["t/bell"] }, on: ["use"] } });
