/**
 * UnfishRunner: runs Unfish programs in the browser on the real engines
 * compiled to WebAssembly (see unfish_worker.js), in a Web Worker so a long
 * or infinite loop never freezes the page.
 *
 *   const res = await UnfishRunner.run(code, { engine: 'vm' });
 *   // res: { exit_code, stdout, stderr, engine: 'wasm' | 'js', timed_out }
 *
 * Options: engine ('interp' | 'vm' | 'regvm', default 'vm'), files (extra
 * project files, { path: text }), timeoutMs (default 10000), onOutput(fd, text)
 * for streaming. Where workers or WebAssembly are unavailable (e.g. a page
 * opened from file://), it falls back to the JavaScript engine in
 * unfish_engine.js, if loaded, and reports engine: 'js'.
 */
(function (global) {
  'use strict';

  const script = document.currentScript;
  const base = new URL('.', script && script.src ? script.src : global.location.href);
  const WORKER_URL = new URL('unfish_worker.js', base).href;
  const WASM_URL = new URL('unfish.wasm', base).href;
  const STDLIB_URL = new URL('unfish_stdlib.json', base).href;

  const DEFAULT_TIMEOUT_MS = 10000;
  const MAX_OUTPUT_CHARS = 1000000;

  let worker = null;
  let nextId = 1;
  const pending = new Map(); // id -> run state
  let wasmUnavailable = !(typeof Worker === 'function' && typeof WebAssembly === 'object') ||
                        global.location.protocol === 'file:';

  function finish(id, exitCode, extra) {
    const state = pending.get(id);
    if (!state) return;
    pending.delete(id);
    clearTimeout(state.timer);
    state.resolve(Object.assign({
      exit_code: exitCode,
      stdout: state.out[1],
      stderr: state.out[2],
      engine: 'wasm',
      timed_out: false,
    }, extra || {}));
  }

  function killWorker(exitCode, message, extra) {
    if (worker) worker.terminate();
    worker = null;
    for (const [id, state] of [...pending]) {
      state.out[2] += message;
      finish(id, exitCode, extra);
    }
  }

  function getWorker() {
    if (worker) return worker;
    worker = new Worker(WORKER_URL);
    worker.onmessage = (event) => {
      const msg = event.data;
      const state = pending.get(msg.id);
      if (!state) return;
      if (msg.type === 'out') {
        const room = MAX_OUTPUT_CHARS - (state.out[1].length + state.out[2].length);
        if (room <= 0) return;
        const text = msg.text.length > room ? msg.text.slice(0, room) + '\n[output truncated]\n' : msg.text;
        state.out[msg.fd] += text;
        if (state.onOutput) state.onOutput(msg.fd, text);
      } else if (msg.type === 'done') {
        finish(msg.id, msg.exitCode);
      } else if (msg.type === 'fail') {
        pending.delete(msg.id);
        clearTimeout(state.timer);
        state.reject(new Error(msg.error));
      }
    };
    worker.onerror = (event) => {
      event.preventDefault();
      killWorker(3, `\nInternal error: ${event.message || 'the WebAssembly worker crashed'}\n`);
    };
    return worker;
  }

  function runWithJsEngine(code) {
    if (typeof global.UnfishEngine !== 'function') {
      return { exit_code: 1, stdout: '', stderr: 'The Unfish engine could not be loaded in this browser.', engine: 'js', timed_out: false };
    }
    const res = new global.UnfishEngine().run(code);
    return { exit_code: res.exit_code, stdout: res.stdout || '', stderr: res.stderr || '', engine: 'js', timed_out: false };
  }

  function runWithWasm(code, opts) {
    return new Promise((resolve, reject) => {
      const id = nextId++;
      const timeoutMs = opts.timeoutMs || DEFAULT_TIMEOUT_MS;
      const state = { resolve, reject, out: { 1: '', 2: '' }, onOutput: opts.onOutput, timer: null };
      pending.set(id, state);
      state.timer = setTimeout(() => {
        killWorker(124, `\nStopped: the program was still running after ${timeoutMs / 1000} s ` +
                        '(an infinite loop?).\n', { timed_out: true });
      }, timeoutMs);
      getWorker().postMessage({
        type: 'run', id, code,
        engine: opts.engine || 'vm',
        files: opts.files || null,
        wasmUrl: WASM_URL,
        stdlibUrl: STDLIB_URL,
      });
    });
  }

  const UnfishRunner = {
    /** Runs a program; resolves with { exit_code, stdout, stderr, engine, timed_out }. */
    async run(code, opts) {
      opts = opts || {};
      if (!wasmUnavailable) {
        try {
          return await runWithWasm(code, opts);
        } catch (e) {
          // The module failed to load (offline, blocked, very old browser):
          // keep the page usable with the JavaScript engine.
          console.warn('Unfish WebAssembly engine unavailable, using the JavaScript engine:', e);
          wasmUnavailable = true;
          killWorker(1, '');
        }
      }
      return runWithJsEngine(code);
    },

    /** Stops the running program, if any. */
    stop() {
      killWorker(130, '\nStopped.\n');
    },

    /** Starts downloading and compiling the engine ahead of the first run. */
    preload() {
      if (wasmUnavailable) return;
      try {
        getWorker().postMessage({ type: 'preload', wasmUrl: WASM_URL, stdlibUrl: STDLIB_URL });
      } catch (e) {
        wasmUnavailable = true;
      }
    },

    /** True when runs use the WebAssembly engine rather than the JS fallback. */
    get usesWasm() { return !wasmUnavailable; },
  };

  global.UnfishRunner = UnfishRunner;
})(window);
