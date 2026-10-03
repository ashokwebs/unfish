/**
 * Unfish WebAssembly worker: runs programs on the real C interpreter and VMs
 * compiled to WebAssembly (unfish.wasm, built by `make wasm-web`), so the site
 * executes exactly what `unfish run` executes.
 *
 * Protocol (from unfish_wasm.js):
 *   in:  { type: 'run', id, code, engine, files, wasmUrl, stdlibUrl }
 *          engine: 'interp' | 'vm' | 'regvm'; files: { "path": "text" }
 *   out: { type: 'out', id, fd, text }    streamed stdout (1) / stderr (2)
 *        { type: 'done', id, exitCode }
 *        { type: 'fail', id, error }      the module could not load or crashed
 *
 * The module is compiled once per worker and instantiated afresh for every
 * run, with a fresh in-memory filesystem holding the bundled stdlib modules,
 * the program as /main.unfish and any extra project files, so no state
 * survives between runs. The same file loads under Node (module.exports) for
 * the test harness.
 */
(function (global) {
  'use strict';

  const ENGINES = { interp: 0, vm: 1, regvm: 2 };
  const MAIN_FILE = 'main.unfish';

  // WASI errno values.
  const E = {
    SUCCESS: 0, BADF: 8, EXIST: 20, INVAL: 28, ISDIR: 31, NOENT: 44,
    NOTDIR: 54, NOTEMPTY: 55, SPIPE: 70, NOTCAPABLE: 76,
  };
  // WASI file types.
  const FT_CHAR = 2, FT_DIR = 3, FT_FILE = 4;
  // path_open oflags / fdflags.
  const O_CREAT = 1, O_DIRECTORY = 2, O_EXCL = 4, O_TRUNC = 8;
  const FDFLAG_APPEND = 1;
  const PREOPEN_FD = 3;

  class ProcExit {
    constructor(code) { this.code = code; }
  }

  /** A per-run in-memory filesystem: paths are "/"-separated, relative to "/". */
  class MemFS {
    constructor(files) {
      this.files = new Map(); // path -> Uint8Array
      this.dirs = new Set(['']);
      const enc = new TextEncoder();
      for (const [path, text] of Object.entries(files || {})) {
        const p = MemFS.normalize('', path);
        if (p === null || p === '') continue;
        this.mkdirs(MemFS.parent(p));
        this.files.set(p, typeof text === 'string' ? enc.encode(text) : text);
      }
    }

    static normalize(base, path) {
      const parts = path.startsWith('/') ? [] : (base ? base.split('/') : []);
      for (const seg of path.split('/')) {
        if (seg === '' || seg === '.') continue;
        if (seg === '..') { if (parts.length === 0) return null; parts.pop(); }
        else parts.push(seg);
      }
      return parts.join('/');
    }

    static parent(p) {
      const i = p.lastIndexOf('/');
      return i < 0 ? '' : p.slice(0, i);
    }

    mkdirs(p) {
      if (p === '') return;
      this.mkdirs(MemFS.parent(p));
      this.dirs.add(p);
    }

    children(dir) {
      const prefix = dir === '' ? '' : dir + '/';
      const names = new Set();
      for (const p of [...this.files.keys(), ...this.dirs]) {
        if (p !== dir && p.startsWith(prefix) && p.length > prefix.length) {
          names.add(p.slice(prefix.length).split('/')[0]);
        }
      }
      return [...names].sort();
    }
  }

  /**
   * wasi_snapshot_preview1 for a sandboxed run: stdout/stderr go to
   * `write(fd, text)`, stdin is empty, the filesystem is a MemFS preopened as
   * "/" (also the working directory), clocks are real, and sleeping returns
   * at once rather than blocking the worker.
   */
  function makeWasi(getMemory, write, fs, args, env) {
    const decoders = { 1: new TextDecoder('utf-8'), 2: new TextDecoder('utf-8') };
    const enc = new TextEncoder();
    const view = () => new DataView(getMemory().buffer);
    const bytes = () => new Uint8Array(getMemory().buffer);
    const readStr = (ptr, len) => new TextDecoder().decode(bytes().subarray(ptr, ptr + len));

    // Open descriptors beyond stdio and the preopen: { kind: 'file'|'dir', path, pos, append }
    const fds = new Map();
    fds.set(PREOPEN_FD, { kind: 'dir', path: '' });
    let nextFd = PREOPEN_FD + 1;

    const dirOf = (fd) => {
      const d = fds.get(fd);
      return d && d.kind === 'dir' ? d.path : null;
    };
    const resolve = (fd, ptr, len) => {
      const base = dirOf(fd);
      if (base === null) return { err: E.BADF };
      const p = MemFS.normalize(base, readStr(ptr, len));
      return p === null ? { err: E.NOTCAPABLE } : { path: p };
    };

    // Writes a list of byte strings into iovec-style memory (argv/environ).
    const writeList = (list, ptrsPtr, bufPtr) => {
      const dv = view();
      const mem = bytes();
      let off = bufPtr;
      list.forEach((s, i) => {
        const b = enc.encode(s);
        dv.setUint32(ptrsPtr + i * 4, off, true);
        mem.set(b, off);
        mem[off + b.length] = 0;
        off += b.length + 1;
      });
      return E.SUCCESS;
    };
    const writeSizes = (list, countPtr, sizePtr) => {
      const dv = view();
      dv.setUint32(countPtr, list.length, true);
      dv.setUint32(sizePtr, list.reduce((n, s) => n + enc.encode(s).length + 1, 0), true);
      return E.SUCCESS;
    };
    const envList = Object.entries(env).map(([k, v]) => `${k}=${v}`);

    const writeFilestat = (ptr, type, size) => {
      const dv = view();
      for (let i = 0; i < 64; i += 8) dv.setBigUint64(ptr + i, 0n, true);
      dv.setUint8(ptr + 16, type);
      dv.setBigUint64(ptr + 24, 1n, true);
      dv.setBigUint64(ptr + 32, BigInt(size), true);
    };

    return {
      args_sizes_get: (c, s) => writeSizes(args, c, s),
      args_get: (p, b) => writeList(args, p, b),
      environ_sizes_get: (c, s) => writeSizes(envList, c, s),
      environ_get: (p, b) => writeList(envList, p, b),

      fd_write(fd, iovs, iovsLen, nwrittenPtr) {
        const dv = view();
        const file = fds.get(fd);
        let total = 0;
        for (let i = 0; i < iovsLen; i++) {
          const ptr = dv.getUint32(iovs + i * 8, true);
          const len = dv.getUint32(iovs + i * 8 + 4, true);
          const chunk = bytes().subarray(ptr, ptr + len);
          if (fd === 1 || fd === 2) {
            const text = decoders[fd].decode(chunk, { stream: true });
            if (text) write(fd, text);
          } else if (file && file.kind === 'file') {
            const data = fs.files.get(file.path) || new Uint8Array(0);
            const at = file.append ? data.length : file.pos;
            const grown = new Uint8Array(Math.max(data.length, at + len));
            grown.set(data);
            grown.set(chunk, at);
            fs.files.set(file.path, grown);
            file.pos = at + len;
          } else {
            return E.BADF;
          }
          total += len;
        }
        dv.setUint32(nwrittenPtr, total, true);
        return E.SUCCESS;
      },
      fd_read(fd, iovs, iovsLen, nreadPtr) {
        const dv = view();
        const file = fds.get(fd);
        let total = 0;
        if (fd !== 0) {
          if (!file || file.kind !== 'file') return E.BADF;
          const data = fs.files.get(file.path) || new Uint8Array(0);
          for (let i = 0; i < iovsLen; i++) {
            const ptr = dv.getUint32(iovs + i * 8, true);
            const len = dv.getUint32(iovs + i * 8 + 4, true);
            const n = Math.max(0, Math.min(len, data.length - file.pos));
            bytes().set(data.subarray(file.pos, file.pos + n), ptr);
            file.pos += n;
            total += n;
            if (n < len) break;
          }
        } // stdin is always at EOF
        dv.setUint32(nreadPtr, total, true);
        return E.SUCCESS;
      },
      fd_seek(fd, offset, whence, newOffsetPtr) {
        const file = fds.get(fd);
        if (!file || file.kind !== 'file') return fd <= 2 ? E.SPIPE : E.BADF;
        const size = (fs.files.get(file.path) || []).length;
        const base = whence === 0 ? 0 : whence === 1 ? file.pos : size;
        const pos = base + Number(offset);
        if (pos < 0) return E.INVAL;
        file.pos = pos;
        view().setBigUint64(newOffsetPtr, BigInt(pos), true);
        return E.SUCCESS;
      },
      fd_close(fd) {
        if (fd === PREOPEN_FD || fd <= 2) return E.SUCCESS;
        return fds.delete(fd) ? E.SUCCESS : E.BADF;
      },
      fd_fdstat_get(fd, statPtr) {
        const d = fds.get(fd);
        if (fd > 2 && !d) return E.BADF;
        const dv = view();
        dv.setUint8(statPtr, fd <= 2 ? FT_CHAR : d.kind === 'dir' ? FT_DIR : FT_FILE);
        dv.setUint16(statPtr + 2, d && d.append ? FDFLAG_APPEND : 0, true);
        dv.setBigUint64(statPtr + 8, 0xffffffffffffffffn, true);
        dv.setBigUint64(statPtr + 16, 0xffffffffffffffffn, true);
        return E.SUCCESS;
      },
      fd_fdstat_set_flags() { return E.SUCCESS; },
      fd_prestat_get(fd, ptr) {
        if (fd !== PREOPEN_FD) return E.BADF;
        const dv = view();
        dv.setUint8(ptr, 0); // directory
        dv.setUint32(ptr + 4, 1, true); // "/"
        return E.SUCCESS;
      },
      fd_prestat_dir_name(fd, ptr, len) {
        if (fd !== PREOPEN_FD || len < 1) return E.BADF;
        bytes()[ptr] = 0x2f; // "/"
        return E.SUCCESS;
      },
      fd_readdir(fd, buf, bufLen, cookie, bufUsedPtr) {
        const dir = dirOf(fd);
        if (dir === null) return E.BADF;
        const dv = view();
        const mem = bytes();
        const names = fs.children(dir);
        let used = 0;
        for (let i = Number(cookie); i < names.length && used < bufLen; i++) {
          const nameBytes = enc.encode(names[i]);
          const child = dir === '' ? names[i] : `${dir}/${names[i]}`;
          const header = new Uint8Array(24);
          const hv = new DataView(header.buffer);
          hv.setBigUint64(0, BigInt(i + 1), true);
          hv.setBigUint64(8, BigInt(i + 1), true);
          hv.setUint32(16, nameBytes.length, true);
          hv.setUint8(20, fs.dirs.has(child) ? FT_DIR : FT_FILE);
          const entry = new Uint8Array(24 + nameBytes.length);
          entry.set(header);
          entry.set(nameBytes, 24);
          // A truncated final entry tells libc to retry with a larger buffer.
          const n = Math.min(entry.length, bufLen - used);
          mem.set(entry.subarray(0, n), buf + used);
          used += n;
        }
        dv.setUint32(bufUsedPtr, used, true);
        return E.SUCCESS;
      },
      path_open(dirFd, _dirflags, pathPtr, pathLen, oflags, _rightsBase, _rightsInh, fdflags, fdOutPtr) {
        const r = resolve(dirFd, pathPtr, pathLen);
        if (r.err) return r.err;
        const p = r.path;
        const isDir = fs.dirs.has(p);
        const isFile = fs.files.has(p);
        if (oflags & O_DIRECTORY) {
          if (!isDir) return isFile ? E.NOTDIR : E.NOENT;
        } else if (isDir) {
          if (oflags & (O_CREAT | O_TRUNC)) return E.ISDIR;
        } else if (!isFile) {
          if (!(oflags & O_CREAT)) return E.NOENT;
          if (!fs.dirs.has(MemFS.parent(p))) return E.NOENT;
          fs.files.set(p, new Uint8Array(0));
        } else if ((oflags & O_CREAT) && (oflags & O_EXCL)) {
          return E.EXIST;
        }
        if (isFile && (oflags & O_TRUNC)) fs.files.set(p, new Uint8Array(0));
        const fd = nextFd++;
        fds.set(fd, isDir ? { kind: 'dir', path: p }
                          : { kind: 'file', path: p, pos: 0, append: !!(fdflags & FDFLAG_APPEND) });
        view().setUint32(fdOutPtr, fd, true);
        return E.SUCCESS;
      },
      path_filestat_get(dirFd, _flags, pathPtr, pathLen, statPtr) {
        const r = resolve(dirFd, pathPtr, pathLen);
        if (r.err) return r.err;
        if (fs.dirs.has(r.path)) writeFilestat(statPtr, FT_DIR, 0);
        else if (fs.files.has(r.path)) writeFilestat(statPtr, FT_FILE, fs.files.get(r.path).length);
        else return E.NOENT;
        return E.SUCCESS;
      },
      path_create_directory(dirFd, pathPtr, pathLen) {
        const r = resolve(dirFd, pathPtr, pathLen);
        if (r.err) return r.err;
        if (fs.dirs.has(r.path) || fs.files.has(r.path)) return E.EXIST;
        if (!fs.dirs.has(MemFS.parent(r.path))) return E.NOENT;
        fs.dirs.add(r.path);
        return E.SUCCESS;
      },
      path_remove_directory(dirFd, pathPtr, pathLen) {
        const r = resolve(dirFd, pathPtr, pathLen);
        if (r.err) return r.err;
        if (!fs.dirs.has(r.path)) return fs.files.has(r.path) ? E.NOTDIR : E.NOENT;
        if (r.path === '') return E.INVAL;
        if (fs.children(r.path).length > 0) return E.NOTEMPTY;
        fs.dirs.delete(r.path);
        return E.SUCCESS;
      },
      path_unlink_file(dirFd, pathPtr, pathLen) {
        const r = resolve(dirFd, pathPtr, pathLen);
        if (r.err) return r.err;
        if (fs.dirs.has(r.path)) return E.ISDIR;
        return fs.files.delete(r.path) ? E.SUCCESS : E.NOENT;
      },

      clock_time_get(id, _precision, timePtr) {
        const ns = id === 0
          ? BigInt(Date.now()) * 1000000n
          : BigInt(Math.round(performance.now() * 1e6));
        view().setBigUint64(timePtr, ns, true);
        return E.SUCCESS;
      },
      poll_oneoff(inPtr, outPtr, nsubs, neventsPtr) {
        // Report every subscription (in practice a sleep's clock timeout) as
        // ready immediately: blocking would freeze the worker for no benefit.
        const dv = view();
        for (let i = 0; i < nsubs; i++) {
          const sub = inPtr + i * 48;
          const ev = outPtr + i * 32;
          dv.setBigUint64(ev, dv.getBigUint64(sub, true), true);
          dv.setUint16(ev + 8, E.SUCCESS, true);
          dv.setUint8(ev + 10, dv.getUint8(sub + 8));
        }
        dv.setUint32(neventsPtr, nsubs, true);
        return E.SUCCESS;
      },
      proc_exit(code) { throw new ProcExit(code); },
    };
  }

  /**
   * Runs `code` on a fresh instance of `module` and returns the exit status
   * `unfish run` would give. `files` maps extra paths (relative to "/") to
   * their text: the bundled stdlib and any other project files.
   */
  async function runModule(module, code, engine, write, files) {
    let memory = null;
    const fs = new MemFS(Object.assign({}, files, { [MAIN_FILE]: code }));
    const env = { HOME: '/', PATH: '/bin', USER: 'unfish', PWD: '/' };
    const wasi = makeWasi(() => memory, write, fs, [MAIN_FILE], env);
    const instance = await WebAssembly.instantiate(module, { wasi_snapshot_preview1: wasi });
    const ex = instance.exports;
    memory = ex.memory;
    try {
      ex._initialize();
      const src = new TextEncoder().encode(code);
      const ptr = ex.uf_wasm_alloc(src.length + 1);
      const mem = new Uint8Array(memory.buffer);
      mem.set(src, ptr);
      mem[ptr + src.length] = 0;
      return ex.uf_wasm_run(ptr, ENGINES[engine] !== undefined ? ENGINES[engine] : 0);
    } catch (e) {
      if (e instanceof ProcExit) return e.code;
      // The interpreter recurses in C for every Unfish call, and the browser's
      // own stack can run out before Unfish's 512-frame limit is reached.
      // Report that as the program's error rather than crashing the page.
      if (e instanceof RangeError && /call stack/i.test(e.message)) {
        write(2, 'StackOverflowError: the browser ran out of stack for this recursion depth ' +
                 '(the stack VM engine allows the full 512 frames)\n');
        return 3;
      }
      throw e;
    }
  }

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = { runModule, MemFS };
    return;
  }

  // ---- Web Worker side ----
  let modulePromise = null;
  let stdlibPromise = null;

  const fetchBytes = (url) => fetch(url).then((r) => {
    if (!r.ok) throw new Error(`HTTP ${r.status} loading ${url}`);
    return r.arrayBuffer();
  });

  function loadModule(url) {
    if (!modulePromise) {
      modulePromise = WebAssembly.compileStreaming
        ? WebAssembly.compileStreaming(fetch(url)).catch(() => fetchBytes(url).then((b) => WebAssembly.compile(b)))
        : fetchBytes(url).then((b) => WebAssembly.compile(b));
      modulePromise.catch(() => { modulePromise = null; });
    }
    return modulePromise;
  }

  function loadStdlib(url) {
    if (!stdlibPromise) {
      stdlibPromise = url
        ? fetch(url).then((r) => (r.ok ? r.json() : {})).catch(() => ({}))
        : Promise.resolve({});
    }
    return stdlibPromise;
  }

  global.onmessage = async (event) => {
    const msg = event.data;
    if (msg && msg.type === 'preload') {
      loadModule(msg.wasmUrl).catch(() => {});
      loadStdlib(msg.stdlibUrl);
      return;
    }
    if (!msg || msg.type !== 'run') return;
    const { id } = msg;

    // Batch output so a chatty program doesn't flood the page with messages.
    const pending = { 1: '', 2: '' };
    let lastFlush = Date.now();
    const flush = () => {
      for (const fd of [1, 2]) {
        if (pending[fd]) {
          global.postMessage({ type: 'out', id, fd, text: pending[fd] });
          pending[fd] = '';
        }
      }
      lastFlush = Date.now();
    };
    const write = (fd, text) => {
      pending[fd] += text;
      if (pending[fd].length > 8192 || Date.now() - lastFlush > 50) flush();
    };

    try {
      const [module, stdlib] = await Promise.all([loadModule(msg.wasmUrl), loadStdlib(msg.stdlibUrl)]);
      const files = Object.assign({}, stdlib, msg.files || {});
      const exitCode = await runModule(module, msg.code, msg.engine, write, files);
      flush();
      global.postMessage({ type: 'done', id, exitCode });
    } catch (e) {
      flush();
      global.postMessage({ type: 'fail', id, error: String((e && e.message) || e) });
    }
  };
})(typeof self !== 'undefined' ? self : globalThis);
