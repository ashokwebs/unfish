# Changelog

All notable changes to the **Unfish** programming language and runtime environment are documented in this file.
The project adheres to [Semantic Versioning](https://semver.org/).

---

## [1.1.0] - 2026-09-04

### Added
- **Multi-Module Native C99 Compiler (Milestone 25)**:
  - Recursive compile-time module discovery with automatic dependency resolution across `import` and `from ... import` statements.
  - Namespaced symbol generation (`uf_m_<name>_...`) for variables, functions, lambdas, and structs per module, preventing symbol collisions.
  - Module initializers (`uf_init_mod_<name>`) exporting variables, functions, and structs into runtime exports map.
  - Runtime module registry with `uf_register_module` / `uf_import_symbol` in `unfish_runtime.h`.
  - Circular import detection (`CircularImportError`) with `is_loading` guard in module registry.
  - Module search path resolution: caller directory → current directory → `src/stdlib/<name>.unfish` → `UNFISH_PATH`.
  - CLI `_with_path` API variants (`uf_emit_c_program_with_path`, `uf_build_native_with_path`).
  - Positional output argument support for `build` and `emit-c` commands (`unfish build foo.unfish output_binary`).
- **3-Way Differential Parity Testing**:
  - Extended `tools/run_differential_tests.sh` to verify AST Interpreter == Bytecode VM == Native C99 binary for all 44 positive conformance tests.
  - All 60 tests (44 positive + 16 negative) pass with 100% parity under ASan/UBSan.
- **Scientific/Exponent Notation Number Literals**: `1e10`, `6.022e23`, `1.5e-10`, `2.5E+3` now lex as a single number token (previously only plain decimal integers/floats were supported — `1e10` was a syntax error). The lexer speculatively consumes `e`/`E` + optional sign + digits and backs out if no digit follows, so `1e` immediately followed by an identifier still lexes as the number `1` followed by that identifier rather than erroring.
- New conformance tests: `36_large_map.unfish` (map resize stress), `37_closure_patterns.unfish` (closures, nested closures, spread patterns), `38_deep_closures.unfish` (multi-level transitive closure capture, closures over match-pattern bindings, mutable-capture chains), `39_closure_edge_cases.unfish` (per-iteration `for`-loop closures, recursive local closures with self-mutation, 4-level transitive capture with mutation, closures over struct-pattern fields), `40_cyclic_structures.unfish` (self- and mutually-referential arrays/maps/structs, plus equality on them), `41_json_cycle_detection.unfish` (json.stringify raises a catchable error on a cyclic structure), `42_scientific_notation.unfish`, `43_json_number_formatting.unfish`, and `44_builtin_name_shadowing.unfish` (a local variable/closure capture named after a builtin function).

### Fixed
- **Native C99 Codegen — Mutable Closure Captures**: Closures compiled to native C99 captured variables by value (a `memcpy` into the closure's environment struct at creation time), so a variable mutated from inside a closure (e.g. a `make_counter()`-style counter incrementing its own state on each call) would reset to its original value on every invocation instead of persisting, diverging from the AST interpreter and bytecode VM's shared-environment semantics.
  - Any variable captured by a closure is now heap-boxed (`UfVal*`, allocated via `uf_box_new`) and shared by reference between its defining scope and every closure invocation that captures it, so mutations are visible on subsequent calls and in the enclosing scope, matching interpreter/VM behavior. Boxing applies uniformly at every declaration site a captured name can originate from (`let`, function/lambda parameters, `for` loop variables, `try/catch` bindings, `match` pattern bindings, `import`/`from...import` bindings) and at every read/write site, so declaration and usage always agree on storage class.
  - Boxes are tracked in a dedicated list (`g_uf_rt.all_boxes`) and freed by `uf_cleanup()`, so the fix introduces no memory leaks (verified leak-free under AddressSanitizer/LeakSanitizer).
- **Native C99 Codegen — Transitive Closure Capture**: A closure nested two or more levels deep (e.g. `outer` → `middle` → `inner`, where `inner` reads a variable owned by `outer`) failed to compile (`'uf_var_x' undeclared`) because the intermediate closure (`middle`) never itself captured the name it needed to relay downward. Capture analysis now propagates captured names up through the closure nesting chain in a single reverse-dependency-order pass, so every closure in the chain captures and forwards whatever its descendants need.
- **Native C99 Codegen — `match`-Pattern Variable Capture**: A closure defined inside a `match` arm that reads the arm's pattern-bound variable (`when x if x > 0: function show(): ... x ...`) was misanalyzed as capturing an outer-scope variable named `x` rather than recognizing it as a binding local to the arm; when that arm's closure sat inside another closure (not a top-level function directly), this produced the same `'uf_var_x' undeclared` compile failure. Pattern-bound names (including nested struct-pattern fields) are now recorded as locals for capture analysis, matching how `let`, `for`, and `catch` bindings are already handled.
- **Native C99 Codegen — Stack Overflow on Compile**: `uf_emit_c_program_with_path` held its module-collection and main emission context (each holding up to 256 per-lambda capture-analysis records) as plain by-value local variables; combined with the fixed-size, 64-module module-collection array this comfortably exceeded a typical 8MB thread stack, crashing the compiler (`AddressSanitizer: stack-overflow`) on any program with closures, before it could emit a single line of output. These are now heap-allocated.

- **Runtime (Interpreter, VM, and Native C99) — Stack Overflow on Cyclic Structures**: Stringifying a value (`say`, `print`, `to_string`, the REPL's auto-printed result, `uf_val_repr`/`uf_val_to_string` in the shared runtime, and `uf_to_str` in the native runtime) recursed into array elements, map values, and struct fields with no cycle detection, so a self- or mutually-referential structure (`let m = {}; m["self"] = m`) recursed forever and crashed with a stack overflow — reproducible from ordinary user code, not just the REPL feature that surfaced it. All four stringification paths (interpreter/VM's `uf_val_to_string` and `uf_val_repr`, and the native runtime's `uf_to_str`) now track the container pointers currently being printed on the call chain and print `[...]`/`{...}`/`Name(...)` for a repeated container instead of recursing into it again, matching the convention Python's `repr()` uses for cycles. Verified leak-free under AddressSanitizer/LeakSanitizer and identical across all three execution backends.
- **Equality (`==`) on Cyclic Structures — Same Stack Overflow**: `uf_val_equal` (interpreter and VM, which share it) and the native runtime's `uf_eq_bool` compared array elements/map values/struct fields recursively with no cycle detection. `m == m` for a self-referential map was already accidentally safe in the interpreter (an existing same-pointer shortcut), but comparing two *distinct* structures that are each independently self-referential (`m1["self"] = m1; m2["self"] = m2; m1 == m2`) recursed forever in all three backends, and the native runtime didn't even have the same-pointer shortcut, so `m == m` crashed there too. Both now track the `(a, b)` pointer pairs currently being compared and treat a pair re-encountered mid-comparison as equal — the standard co-inductive convention Python's list/dict equality uses for cycles — so both the self-identity and distinct-cyclic-structures cases now terminate and agree across all three backends.
- **`json.stringify` — Same Cyclic-Structure Stack Overflow**: `json.stringify` (both the interpreter/VM's `stdlib/uf_mod_json.c` and the native runtime's `_json_stringify_val`) had the identical unbounded-recursion bug independently. Since JSON has no syntax for a cycle, it now raises a catchable `TypeError: Converting circular structure to JSON` instead (matching JS's `JSON.stringify` behavior) rather than printing a `"..."` placeholder that wouldn't parse back as valid JSON. Both stringifiers propagate a plain success/failure result up to their top-level caller before raising, rather than raising from inside the recursive descent, so the error's `setjmp`/`longjmp`-based unwind (when caught by a `try`/`catch`) can't skip past — and leak — the in-progress output buffer.
- **Native C99 Runtime — Garbled Output for Large Whole Numbers**: The native runtime's `uf_to_str` formatted any whole-valued number with `%.0f` unconditionally (`floor(n) == n`), unlike the interpreter/VM's equivalent which only does that below `1e15` and falls back to `%.14g` (scientific notation) above it. A value like `1.5e300` (a "whole number" at that magnitude, since every double past a certain size is) tried to print ~300 digits into a 128-byte stack buffer — not a memory-safety bug (`snprintf` is bounds-checked) but silently truncated, garbled output that disagreed with the interpreter/VM. Added the same `fabs(n) < 1e15` magnitude guard.
- **`json.stringify` — Undefined Behavior on Out-of-Range Numbers**: Both JSON stringifiers (interpreter/VM and native) tested whether a number was a whole number via `val == (double)(int64_t)val`, but converting a `double` outside `int64_t`'s range (or NaN/infinity, in the interpreter's case, which didn't filter those first) to `int64_t` is undefined behavior in C, not just wrong — it's not guaranteed to produce any particular value. Replaced with the same NaN/infinity/`fabs(n) < 1e15` guard as the other stringifiers, checked *before* the now-safe `int64_t` cast.
- **Native C99 Codegen — Local Variables Named After a Builtin**: A local variable or closure parameter sharing a name with any of the ~80 builtin functions (`log`, `min`, `max`, `sort`, `keys`, `filter`, `find`, ...) — an entirely ordinary, legal choice, and one the AST interpreter and bytecode VM already handled correctly via normal lexical shadowing — failed to compile in the native backend (`'uf_var_log' undeclared`) whenever a nested closure read it as a value, because capture analysis treated any name matching a builtin as always resolved to that builtin and never considered it capturable. Fixed by only skipping capture analysis for a builtin-matching name when it's the direct callee of a call (the one case where emit_expr genuinely always builtin-dispatches it, matching runtime behavior) — everywhere else, a builtin-shaped name is now analyzed like any other identifier. Known remaining gap: calling a variable that shadows a builtin *by that name* (`let log = fn ...; log(5)`) still dispatches to the builtin rather than the shadowing value in the native backend, unlike the interpreter/VM — the call-site fast path has no lexical-scope awareness to detect that narrower case. Reading such a variable as a plain value (not calling it) is unaffected and correct.

### Changed
- **Native C99 Codegen — Fixed-Capacity Limits Now Fail Loudly**: The emitter's internal bookkeeping (closures per program, captures per closure, local bindings per closure body, top-level functions/structs/variables per module) uses fixed-capacity arrays sized generously for ordinary programs. Previously, exceeding one silently dropped the overflow, which could produce a wrong-but-compiling binary (e.g. a closure the emitter lost track of silently became `uf_null()`) rather than a build error. Any overflow of these compiler-internal limits now fails the build with a specific, actionable error message instead; the AST interpreter and bytecode VM have no such limits and are unaffected.

## [1.0.0] - 2026-09-04

### Added
- **Systems Programming & Hardware Bridge (Section Z)**:
  - Low-level raw byte buffers (`buffer(size)`, `buffer_from_string`, `buffer_to_string`, `buffer_size`, `buffer_slice`, `buffer_fill`).
  - Byte-level and multi-byte little-endian access primitives (`buffer_get`, `buffer_set`, `buffer_read_u16_le`, `buffer_write_u16_le`, `buffer_read_u32_le`, `buffer_write_u32_le`, `buffer_read_i32_le`, `buffer_write_i32_le`).
  - Fixed-width integer conversion and masking helpers (`u8`, `i8`, `u16`, `i16`, `u32`, `i32`).
  - Runtime memory layout inspector (`inspect(val)`).
  - Unit test suite `tests/unit/test_systems.c` and ADR 034.
- **Concurrency, Fibers & Channels (Section Y)**:
  - Cooperative coroutine fibers (`UfFiber`, `UF_VAL_FIBER`, `spawn`, `yield`).
  - Communicating Sequential Processes (CSP) channels (`UfChannel`, `UF_VAL_CHANNEL`, `channel`, `send`, `recv`, `close_channel`).
  - Embedded round-robin scheduler (`run_scheduler()`) with automatic GC root tracking.
  - Unit test suite `tests/unit/test_fiber.c` and ADR 033.
- **Language Server Protocol Engine (Section X)**:
  - Built-in JSON-RPC 2.0 language server (`unfish lsp`) with `Content-Length` framing.
  - Full LSP method support: diagnostics, hover, definition, completion, and document formatting.
  - VS Code extension package in `editors/vscode/`.
  - Unit test suite `tests/unit/test_lsp.c` and ADR 032.
- **Optimization Passes (Section W)**:
  - Compile-time AST constant folding for arithmetic, strings, and booleans.
  - Dead-code elimination after unconditional returns.
  - Bytecode peephole optimization pass.
  - Unit test suite `tests/unit/test_optimize.c` and ADR 031.
- **Native C99 Code Generation & Compilation (Section V)**:
  - Standalone single-header C99 runtime `unfish_runtime.h`.
  - Transpilation CLI command `unfish emit-c [-o <out.c>] <file.unfish>`.
  - Native executable binary compiler `unfish build [-o <binary>] <file.unfish>`.
  - Unit test suite `tests/unit/test_emit_c.c` and ADR 030.
- **Bytecode Virtual Machine & Differential Parity (Section R, S, T, U)**:
  - 36-opcode stack-based virtual machine (`unfish run --vm`).
  - Full AST-to-bytecode compiler (`unfish compile`).
  - Open/closed upvalue closure capture cells (`UfUpvalueCell`).
  - Exception handling unwinding with `OP_PUSH_TRY` / `OP_POP_TRY`.
  - Recursive disassembler (`unfish disasm`) and instruction tracer (`--debug`).
  - Differential parity runner `tools/run_differential_tests.sh` with 51/51 identical passes.
- **Visual Block JSON Round-Tripping (Section Q)**:
  - Two-way translation between visual block-based programs and Unfish ASTs (`unfish blocks-export` / `unfish blocks-import`).
- **Interactive CLI Debugger (Section P)**:
  - Breakpoints, stepping (`step`, `next`), backtraces, variable printing (`unfish debug`).
- **Source Code Formatter (Section N)**:
  - Deterministic AST-to-source formatter enforcing canonical layout (`unfish fmt`).
- **Type Annotations, Structs & Pattern Matching (Section K, L, M)**:
  - Nominal struct definitions, instantiation, and field access.
  - Structural pattern matching (`match` / `case`) with destructuring.
  - Gradual type annotations with optional `--strict` type checking.
- **Standard Library Modules & Collections (Section B–J)**:
  - Standard modules: `sys`, `fs`, `random`, `time`, `json`, `testing`.
  - First-class hash maps with dot property syntax.
  - First-class dynamic arrays and higher-order functional primitives (`map`, `filter`, `reduce`, `sort`, `find`, `every`, `some`).
  - String and Math standard libraries.
  - Structured exception recovery (`try` / `catch`).
