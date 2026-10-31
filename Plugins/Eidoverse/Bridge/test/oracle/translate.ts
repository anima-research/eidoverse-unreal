// test/oracle/translate.ts — a first-protocol world translated as the operator does (translateV1World); usage: bun translate.ts <v1 dir> <name>
// @ts-nocheck
import path from "node:path";
const [src, name] = process.argv.slice(2);
const { translateV1World } = await import("../../engine/tools/protocol/import-v1/world.ts");
const r = translateV1World(path.resolve(src), path.join(process.env.WORLDS_DIR, name));
process.stdout.write(JSON.stringify({ entries: r?.entries ?? null }));
