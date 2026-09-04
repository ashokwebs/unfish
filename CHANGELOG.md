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
  - Extended `tools/run_differential_tests.sh` to verify AST Interpreter == Bytecode VM == Native C99 binary for all 37 positive conformance tests.
  - All 53 tests (37 positive + 16 negative) pass with 100% parity under ASan/UBSan.
- New conformance tests: `36_large_map.unfish` (map resize stress) and `37_closure_patterns.unfish` (closures, nested closures, spread patterns).

### Fixed
- **Native C99 Codegen — Mutable Closure Captures**: Closures compiled to native C99 captured variables by value (a `memcpy` into the closure's environment struct at creation time), so a variable mutated from inside a closure (e.g. a `make_counter()`-style counter incrementing its own state on each call) would reset to its original value on every invocation instead of persisting, diverging from the AST interpreter and bytecode VM's shared-environment semantics.
  - Any variable captured by a closure is now heap-boxed (`UfVal*`, allocated via `uf_box_new`) and shared by reference between its defining scope and every closure invocation that captures it, so mutations are visible on subsequent calls and in the enclosing scope, matching interpreter/VM behavior. Boxing applies uniformly at every declaration site a captured name can originate from (`let`, function/lambda parameters, `for` loop variables, `try/catch` bindings, `match` pattern bindings, `import`/`from...import` bindings) and at every read/write site, so declaration and usage always agree on storage class.
  - Boxes are tracked in a dedicated list (`g_uf_rt.all_boxes`) and freed by `uf_cleanup()`, so the fix introduces no memory leaks (verified leak-free under AddressSanitizer/LeakSanitizer).

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
