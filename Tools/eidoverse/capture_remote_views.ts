// Local fixture only. Runs OUTSIDE Unreal so HTTP cannot block its game thread.
import { strict as assert } from "node:assert";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
const out = mkdtempSync(join(tmpdir(), "unreal-remote-views-"));
const cases = [
  { name: "underwater-first", p: [15, -29, 65], q: [0,0,0,1], view: "first" },
  { name: "underwater-third", p: [15, -29, 65], q: [0,0,0,1], view: "third" },
  { name: "underwater-selfie", p: [15, -29, 65], q: [0,0,0,1], view: "selfie" },
  { name: "surface-first", p: [15, 2, 65], q: [.259,0,0,.966], view: "first" },
  { name: "underwater-looking-up", p: [15, -12, 65], q: [-.5,0,0,.866], view: "first" },
];
for (const c of cases) {
  const pose = await fetch("http://127.0.0.1:18994/pose", { method: "POST", body: JSON.stringify(c) }); assert.ok(pose.ok);
  await Bun.sleep(2600);
  const start = performance.now();
  const r = await fetch(`http://127.0.0.1:18998/snap?world=snapshot-test&follow=snapshot-agent&renderer=unreal&view=${c.view}`);
  if (!r.ok) throw Error(`${c.name}: ${r.status} ${await r.text()}`);
  assert.equal(r.headers.get("x-eidoverse-renderer"), "unreal");
  const bytes = Buffer.from(await r.arrayBuffer());
  assert.equal(bytes.readUInt32BE(16), 960); assert.equal(bytes.readUInt32BE(20), 540);
  await Bun.write(join(out, c.name + ".png"), bytes);
  console.log(`${c.name}: ${bytes.length} bytes, ${Math.round(performance.now()-start)} ms`);
}
console.log("REMOTE_CAPTURE_IMAGES_PASS " + out);
