#!/usr/bin/env node
// Bundles the stdlib modules written in Unfish (src/stdlib/*.unfish) into
// web/unfish_stdlib.json, keyed by the path the module loader looks them up
// under, so `import testing` works in the browser's in-memory filesystem.
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..', '..');
const dir = path.join(root, 'src', 'stdlib');
const out = {};
for (const f of fs.readdirSync(dir).filter((f) => f.endsWith('.unfish')).sort()) {
  out[`src/stdlib/${f}`] = fs.readFileSync(path.join(dir, f), 'utf8');
}
fs.writeFileSync(path.join(root, 'web', 'unfish_stdlib.json'), JSON.stringify(out) + '\n');
