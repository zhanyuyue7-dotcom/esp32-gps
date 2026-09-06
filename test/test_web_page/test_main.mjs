import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const html = readFileSync(resolve(here, "../../main/web_page.html"), "utf8");

assert.match(html, /<meta name="viewport"/);
assert.match(html, /--background:#faf9f5/);
assert.match(html, /@media\(max-width:680px\)/);
assert.match(html, /id="connection"/);
assert.match(html, /id="liveDistance"/);
assert.match(html, /id="activities"/);
assert.match(html, /id="activityDetail"/);
assert.match(html, /id="route"[^>]*<\/svg>|id="route"/);
assert.match(html, /fetch\('\/api\/status'/);
assert.match(html, /fetch\('\/api\/activities'/);
assert.match(html, /\/api\/activity\?id=/);
assert.match(html, /\/api\/download\?id=.*format=csv/);
assert.match(html, /\/api\/download\?id=.*format=gpx/);
assert.match(html, /method:'DELETE'/);
assert.match(html, /confirm\(/);
assert.match(html, /viewBox/);
assert.match(html, /setInterval\(pollStatus,1000\)/);
assert.doesNotMatch(html, /<(?:script|link)[^>]+(?:src|href)=["']https?:\/\//);
console.log("GPS dashboard source contract passed");
