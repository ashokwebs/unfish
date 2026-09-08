#!/usr/bin/env node
/**
 * Unfish WASI WebAssembly Runner
 * Executes compiled Unfish .wasm binaries in Node.js using standard WASI preview1.
 */

const fs = require('fs');
const path = require('path');
const { WASI } = require('wasi');

const wasmFile = process.argv[2];
if (!wasmFile) {
  console.error('Usage: node run_wasm.js <file.wasm> [args...]');
  process.exit(1);
}

if (!fs.existsSync(wasmFile)) {
  console.error(`Error: File not found: ${wasmFile}`);
  process.exit(1);
}

const wasmBytes = fs.readFileSync(wasmFile);
const wasmArgs = [path.basename(wasmFile), ...process.argv.slice(3)];

const wasi = new WASI({
  version: 'preview1',
  args: wasmArgs,
  env: process.env,
  preopens: {
    '.': '.'
  }
});

WebAssembly.instantiate(wasmBytes, {
  wasi_snapshot_preview1: wasi.wasiImport
}).then(res => {
  try {
    const exitCode = wasi.start(res.instance);
    process.exit(exitCode !== undefined ? exitCode : 0);
  } catch (err) {
    if (err && typeof err.code === 'number') {
      process.exit(err.code);
    }
    console.error(err);
    process.exit(1);
  }
}).catch(err => {
  console.error('WASM Instantiation Error:', err);
  process.exit(1);
});
