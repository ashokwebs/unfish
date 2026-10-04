# Engineering Report: Backend Parity, GC Safety and a Real-Engine Website

*October 3, 2026 · commits `165b256` through `00939a2` on `main`*

Ten commits are on `main` and the website is live. All four engines now agree, the garbage collector no longer frees live objects, and the site runs the real interpreter in the browser. Every test suite passes, and the fixes ship to command-line users as [v2.1.0](https://github.com/ashokwebs/unfish/releases/tag/v2.1.0).

---

## 1. Shipped

All ten commits landed on 2026-10-03. The website was redeployed from `web/` at `00939a2`.

| Commit | Area | Change |
| --- | --- | --- |
| `00939a2` | Website | Run the site's code on the real engines via WebAssembly; fix invalid examples and stale facts |
| `2119347` | Runtime | Stop truncating large integers on 32-bit targets |
| `3fa29db` | Lexer | Accept the `0x`/`0b`/`0o` literals the spec documents |
| `38d53e0` | Docs | Document the GC rooting fixes and `make test-gc-stress` |
| `901c05b` | GC | Close rooting holes found by a new GC-stress mode |
| `b5ca339` | All engines | Give `match` patterns interpreter semantics on both VMs and native |
| `ec1256b` | Native | Make runtime errors catchable instead of exiting |
| `d6699a4` | All engines | Give every backend the same 512-frame recursion limit |
| `ddd8ac3` | All engines | Run `finally` on `return`/`break`/`continue` and when `catch` raises |
| `165b256` | Both VMs | Stop at an uncaught builtin error instead of running on |

---

## 2. Backend Parity

The same program now gives the same output and exit code on the AST interpreter, the stack VM, the register VM and native C. The gaps were found by running small snippets on all four engines and diffing stdout and the first line of stderr.

| Behaviour | What diverged | Fix | Regression test |
| --- | --- | --- | --- |
| Uncaught builtin error | Both VMs printed the error, then ran the rest of the program and exited 3 at the end: `say len(5)` then `say "after"` printed both | Both VMs stop at the call boundary, as the interpreter does | `err_builtin_error_halts` |
| `finally` | `return`/`break`/`continue` inside `try` skipped `finally` on three backends; an error raised in `catch` skipped it everywhere | Compilers inline pending `finally` blocks at early exits; `catch` runs under a guard that runs `finally` and rethrows | `69_finally_control_flow`, `70_return_value_in_try` |
| Recursion limit | Interpreter 512 frames, VMs 256, native about 520; WebAssembly crashed on deep recursion | Every backend allows exactly 512 and raises the same `StackOverflowError` | `71_deep_recursion` |
| Native runtime errors | 48 error paths called `exit(3)`, so `try`/`catch` was ignored once compiled | They throw a catchable error with the same `err.kind` | `72_runtime_errors_catchable` |
| `match` patterns | VMs and native checked only top-level literals: `[1, x]` matched `[1, 2, 3]`, and nested patterns were skipped | Patterns lowered recursively with new `MATCH_SHAPE`/`MATCH_FIELD` opcodes; bytecode cache bumped to v6 | `73_match_patterns`, `74_local_shadows_global` |

---

## 3. Garbage Collector

The collector was freeing objects that were still in use, on every engine. In a normal build this only strikes when a large program crosses the 64 KB collection threshold at an unlucky moment, so it showed up as rare crashes or corrupted values.

A new debug mode, `UNFISH_GC_STRESS=1`, runs a full collection on every allocation. Under AddressSanitizer it turned these latent bugs into crashes on the first run: 163 failing runs at the start, about 15 root causes, and zero failures afterwards across every test and example on all three engines.

| Where | Bug | Fix |
| --- | --- | --- |
| Compilers, bytecode cache | The function being compiled was unreachable from any root, so a collection mid-compile freed it | Collection pauses during compilation and cache loading (`uf_gc_pause`); the VMs root the script until its closure exists |
| GC marking | Reaching a struct definition through an instance set its mark bit without tracing its methods, so they were freed | Marking traces the definition |
| GC marking | An object marked before its own registration kept a stale mark bit, so the next collection skipped its children | `uf_runtime_register_obj` clears the bit |
| Errors | An error's message and kind strings were unrooted until the error was registered | Rooted until then |
| Stack VM | `INDEX_GET`, `ITER_GET`, async `RETURN` and `catch` unwinding left the GC's view of the stack top stale | Sync the stack top before popping operands |
| Register VM | A new closure lived only in a C local while upvalues were captured; native callbacks re-exposed registers holding freed objects | Publish the closure first; null registers handed back |
| Interpreter | `match`-arm scopes, `for` over strings, struct and `impl` methods, and return values pending across `finally` or async were unrooted | Each rooted or made current |
| Stdlib | `json.parse` keys and `inspect()` strings were unrooted across an allocation | Rooted |

`make test-gc-stress` now runs the conformance suite in this mode on all three engines in about 20 seconds. It uses its own sanitizer binary, so `bin/unfish` stays a release build. Reverting one fix made it fail, confirming it catches real bugs.

---

## 4. Website

The [website](https://ashokwebs.github.io/unfish/) now runs the real interpreter and both VMs, compiled to WebAssembly, and matches `unfish run` on all 305 checked runs. Before, every page ran code on `unfish_engine.js`, a separate JavaScript reimplementation that disagreed with the CLI on 75 of 125 test programs and accepted syntax the language rejects.

```
 Web pages                 unfish_wasm.js                 Web Worker (fresh per run)
 Playground, Play,  --->   run(code, engine)    --run-->  +------------------------------+
 Studio, Learn,            10 s timeout, stop() <-output- | unfish.wasm                  |
 Docs                            :                        |   C interpreter, stack VM,   |
                                 : only without           |   register VM (1.1 MB)       |
                                 : WebAssembly            | WASI shim                    |
                                 v                        |   stdout, stderr, clocks     |
                           unfish_engine.js               | In-memory filesystem         |
                           JS fallback; also draws        |   stdlib + project files,    |
                           tokens, AST and blocks         |   fs writes last one run     |
                                                          +------------------------------+
```

The page never runs code itself: each run gets a fresh engine in the worker, so a runaway program is killed without freezing the tab.

- **Engine:** `make wasm-web` compiles the C front end, interpreter and VMs to `web/unfish.wasm` (1.1 MB). A small entry point (`src/wasm/uf_wasm_entry.c`) exposes `run(source, engine)`.
- **Isolation:** each run gets a fresh instance in a Web Worker, so nothing leaks between runs. A program still running after 10 seconds is stopped.
- **Files:** each run has an in-memory filesystem holding the bundled stdlib and, in Studio, the other project files. `import testing`, imports between project files and the `fs` module all work in the browser.
- **Pages:** the playground, Play, Studio, Learn and Docs pages all use it. The Play page's engine selector now actually picks the engine; before, every option ran the same JavaScript.
- **Fallback:** the JavaScript engine still drives the token, AST and blocks views, and runs code only where WebAssembly or workers are unavailable (for example, a page opened from `file://`).
- **Deploy:** the `gh-pages` branch is a copy of `web/`, built by GitHub Pages. `unfish.wasm` is served as `application/wasm`.

---

## 5. Language and Content Fixes

Compiling for the browser and running every example on the real engine exposed problems the JavaScript engine had hidden.

- **Hex, binary and octal literals:** the spec promised `0xFF`, `0b1010` and `0o755`, and the docs used them, but they were a syntax error on every backend. They now work everywhere, a bad digit gets a clear error with a hint, and `unfish format` keeps how you wrote them instead of printing `65280`.
- **Large integers on 32-bit targets:** `json.stringify`, `to_hex()` and the formatter printed through C `long`, which is 32 bits on WebAssembly and 32-bit ARM, so `10000000000` printed as `1410065408`. They now use `long long`.
- **Invalid examples:** 12 runnable examples and doc snippets only worked on the lenient JavaScript engine: `print "x"` as a statement, single-quoted strings, a `^` operator, a one-line `if`, `spawn function():` blocks, a multi-line lambda in `return`, and `max()` over a list. All 63 examples and snippets on the site now run on every engine.
- **Stale facts:** the stack VM has 59 opcodes since the `match` fix, not 57; the two new opcodes are now in the ISA tables. Test counts said 91/91 or 95/95; the suite is now 104 tests. The spec's leading-dot floats (`.125`) were never supported and clash with member access, so the spec now requires a leading digit.

---

## 6. Verification

Every suite passed on the final build, run one at a time (running `test-asan` alongside other suites causes spurious failures).

| Check | Command | Result |
| --- | --- | --- |
| Unit, stress, conformance, differential | `make test` | 104/104 conformance, 104/104 identical across engines, all unit and stress suites pass |
| Sanitizers | `make test-asan` | Clean under ASan and UBSan |
| GC stress | `make test-gc-stress` | 104/104 on each of interpreter, stack VM, register VM |
| Browser engine | `make test-wasm-web` | 305 runs identical to `bin/unfish` (16 skipped by design) |
| Native backend | Built and ran every applicable test | 75/75 identical to the interpreter |
| Site examples | Every example and snippet on all engines | 63/63 run cleanly |
| Live site | Chrome on ashokwebs.github.io | Pages load without console errors; runs use WebAssembly; timeout and recovery work |
| Performance | 13 benchmarks, before vs after | Within noise on both VMs |

The 128-bit multiply helpers written for the WebAssembly build were checked against native `__int128` arithmetic on 5 million random inputs, with no mismatches.

---

## 7. Release and Known Limits

Everything above ships as **v2.1.0** (2026-10-04): a minor bump, since hex literals and the browser engine are new features. The release carries the Linux x86_64 tarball, the VS Code extension (which now highlights `0x`/`0b`/`0o` literals), `unfish_runtime.h` and the installer, and `curl -fsSL https://ashokwebs.github.io/unfish/install.sh | bash` now installs it.

Limits that remain (also recorded in [KNOWN_ISSUES.md](KNOWN_ISSUES.md) §7):

- **Interpreter recursion in the browser:** Chrome's worker stack lets the tree-walking interpreter recurse only about 250 levels deep. Past that, the page shows a clear `StackOverflowError` that points to the VM engine, which allows the full 512. The quick-run buttons use the stack VM by default. Raising this would mean splitting the interpreter's large functions to shrink each call's stack use.
- **Imports at compile time:** the collector pauses while a program compiles, and imports run their module's code during compilation, so garbage from a module's top-level code is freed only once compilation finishes. Modules that only define things are unaffected.
- **Browser sandbox:** `sys.exec` returns -1, `time.sleep` returns at once, stdin is empty, and files written with `fs` last for one run. `inspect()` reports 32-bit object sizes.
