#!/usr/bin/env node
// Runs every conformance test through web/unfish.wasm exactly as the website
// does (web/unfish_worker.js's runModule, with the bundled stdlib and an
// in-memory filesystem) on all three engines, and checks that stdout and the
// exit status match bin/unfish. Used by `make test-wasm-web`.
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const { runModule } = require('../../web/unfish_worker.js');

const root = path.join(__dirname, '..', '..');
const testsDir = path.join(root, 'tests', 'conformance');
const bin = path.join(root, 'bin', 'unfish');

// Differences inherent to the browser sandbox rather than bugs.
const EXPECTED_DIFFERENCES = new Set([
  // inspect() reports sizeof() of runtime structs, which differ on wasm32.
  '46_systems_buffers.unfish',
]);
// The tree-walking interpreter recurses in C per Unfish call, so near the
// 512-frame limit it can exhaust the JS engine's native stack (~1 MB) once
// V8 optimizes the module; the worker then reports StackOverflowError.
const EXPECTED_INTERP_DIFFERENCES = new Set(['71_deep_recursion.unfish']);

function native(file, flag) {
  try {
    const stdout = execFileSync(bin, ['run', '--no-cache', ...(flag ? [flag] : []), file],
      { cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'], timeout: 60000 });
    return { stdout, exit: 0 };
  } catch (e) {
    return { stdout: e.stdout || '', exit: e.status };
  }
}

(async () => {
  const module = await WebAssembly.compile(fs.readFileSync(path.join(root, 'web', 'unfish.wasm')));
  const stdlib = JSON.parse(fs.readFileSync(path.join(root, 'web', 'unfish_stdlib.json'), 'utf8'));

  // Sibling files are mounted both beside main.unfish (for imports) and at
  // their repo-relative path (for tests that name files from the repo root).
  const tests = fs.readdirSync(testsDir).filter((f) => f.endsWith('.unfish')).sort();
  const files = Object.assign({}, stdlib);
  for (const f of tests) {
    const text = fs.readFileSync(path.join(testsDir, f), 'utf8');
    files[f] = text;
    files[`tests/conformance/${f}`] = text;
  }

  let passed = 0;
  let failed = 0;
  let skipped = 0;
  for (const [engine, flag] of [['interp', null], ['vm', '--vm'], ['regvm', '--regvm']]) {
    for (const f of tests) {
      const code = fs.readFileSync(path.join(testsDir, f), 'utf8');
      if (f.startsWith('helper_') || /^# flags:/m.test(code) || EXPECTED_DIFFERENCES.has(f) ||
          (engine === 'interp' && EXPECTED_INTERP_DIFFERENCES.has(f))) {
        skipped++;
        continue;
      }
      const want = native(path.join('tests', 'conformance', f), flag);
      let stdout = '';
      let exit;
      try {
        exit = await runModule(module, code, engine, (fd, text) => { if (fd === 1) stdout += text; }, files);
      } catch (e) {
        exit = `crash: ${e.message}`;
      }
      if (stdout === want.stdout && exit === want.exit) {
        passed++;
      } else {
        failed++;
        console.log(`FAIL [${engine}] ${f}: exit ${exit} (native ${want.exit})`);
        if (stdout !== want.stdout) {
          console.log(`  wasm:   ${JSON.stringify(stdout.slice(-200))}`);
          console.log(`  native: ${JSON.stringify(want.stdout.slice(-200))}`);
        }
      }
    }
  }
  console.log(`WebAssembly Engine Results: ${passed} passed, ${failed} failed, ${skipped} skipped.`);
  process.exit(failed === 0 ? 0 : 1);
})();
