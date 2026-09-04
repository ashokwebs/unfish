# Changelog

All notable changes to the **Unfish** programming language and runtime environment are documented in this file.
The project adheres to [Semantic Versioning](https://semver.org/).

---

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
