// Render the web client's own UI icons to PNGs for the Unreal UMG shell.
//
//   bun Tools/eidoverse/build_ui_icons.ts [CLIENT_URL] [OUT_DIR]
//
// CLIENT_URL: a running eidoverse-worlds server (e.g. tools/water/preview.ts),
// default http://127.0.0.1:19350. Icons come from client/lib/icons.js (Phosphor
// fill, MIT; Lucide, ISC) and the ∃ mark from index.html, so both clients share
// one set. Output: white glyphs on transparent 128x128 PNGs, tinted in UMG.
// Then import with import_ui_icons.py.
import { chromium } from 'playwright';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const url = process.argv[2] ?? 'http://127.0.0.1:19350';
const out = resolve(process.argv[3] ?? 'Plugins/Eidoverse/Resources/UI/Icons');
// UE name -> [set, source name]. 'fill' = Phosphor fill (the rail), 'line' = Lucide.
const ICONS: Record<string, [string, string]> = {
  Profile: ['fill', 'user-circle'], Chat: ['fill', 'chat-circle'], World: ['fill', 'planet'],
  Emotes: ['fill', 'hand-waving'], Debug: ['fill', 'bug'], Settings: ['fill', 'gear-six'],
  Edit: ['fill', 'wrench'], People: ['fill', 'users'], Pin: ['fill', 'push-pin'],
  Lock: ['fill', 'lock-simple'], Unlock: ['fill', 'lock-simple-open'], Reset: ['fill', 'sparkle'],
  Search: ['fill', 'magnifying-glass'], Build: ['fill', 'hammer'], Scripts: ['fill', 'scroll'],
  Mods: ['fill', 'puzzle-piece'], Sun: ['fill', 'sun'], Plant: ['fill', 'plant'],
  Eye: ['fill', 'eye'], EyeOff: ['fill', 'eye-slash'], Speaker: ['fill', 'speaker-high'],
  Mic: ['line', 'mic'], Ear: ['line', 'ear'], Hand: ['line', 'hand'], Seat: ['line', 'armchair'],
  Stand: ['line', 'personStanding'], Lie: ['line', 'bed'],
};

const browser = await chromium.launch({ headless: true, channel: 'chrome' });
const page = await browser.newPage({ viewport: { width: 160, height: 160 } });
await page.goto(url + '/?spectate=1&name=icon-render');
const svgs: Record<string, string> = await page.evaluate(async (icons) => {
  const m: any = await import('/lib/icons.js');
  const result: Record<string, string> = {};
  for (const [name, [set, source]] of Object.entries(icons as Record<string, [string, string]>)) {
    const markup = set === 'fill' ? m.fsvg(source, 128) : m.svg(source, 128);
    if (markup) result[name] = markup;
  }
  const mark = document.querySelector('#hud svg');
  if (mark) {
    const clone = mark.cloneNode(true) as SVGElement;
    clone.setAttribute('width', '128'); clone.setAttribute('height', '128');
    result.Mark = clone.outerHTML;
  }
  return result;
}, ICONS);
mkdirSync(out, { recursive: true });
const missing = Object.keys(ICONS).filter(n => !svgs[n]);
for (const [name, markup] of Object.entries(svgs)) {
  await page.setContent(`<html><body style="margin:0;background:transparent">
    <div id="i" style="width:128px;height:128px;color:#fff;display:flex">${markup}</div></body></html>`);
  const png = await page.locator('#i').screenshot({ omitBackground: true });
  writeFileSync(resolve(out, `T_Icon_${name}.png`), png);
}
await browser.close();
console.log(JSON.stringify({ out, rendered: Object.keys(svgs).length, missing }));
