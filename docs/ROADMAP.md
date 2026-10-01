# UNFISH — DEVELOPMENT ROADMAP

This roadmap defines the engineering progression of Unfish from initial architecture to a high-performance, systems-capable language and development environment.

---

## Phase 0: Architecture, Formal Specification & Documentation
- [x] Project state inventory (`PROJECT_STATE.md`).
- [x] Architecture design (`ARCHITECTURE.md`).
- [x] Language grammar and formal specification (`LANGUAGE_SPEC.md`).
- [x] Type system and memory model specification (`TYPE_SYSTEM.md`, `MEMORY_MODEL.md`).
- [x] Error model and diagnostic reporting specification (`ERROR_MODEL.md`).
- [x] Repository build harness (Makefiles, sanitizers, test runner).

## Phase 1: Core Lexer, Parser & AST
- [x] Source location tracking system (`SourceLoc`, `SourceSpan`, line/column/offset).
- [x] Memory arena and string interning for AST nodes.
- [x] Lexer with indentation engine (`INDENT`, `DEDENT`, `NEWLINE` off-side rules).
- [x] Rich token diagnostics with source snippet carets.
- [x] Strongly typed AST definitions for declarations, statements, and expressions.
- [x] Pratt parser for expressions (`+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `and`, `or`, `not`, groupings, function calls).
- [x] Recursive descent parser for statements (`let`, `say`, `if`, `while`, `repeat`, `function`, `return`).
- [x] AST printer / formatter for debugging and validation.

## Phase 2: Semantic Analysis & Tree-Walking Interpreter
- [x] Lexical scoping and environment chain model.
- [x] Semantic analysis pass (scope resolution, variable shadowing, duplicate declarations, arity checks, return-outside-function validation).
- [x] Tagged union runtime values (`Number`, `String`, `Boolean`, `Null`, `Function`, `NativeFunction`).
- [x] Tree-walking interpreter with execution hooks.
- [x] Call stack tracking with activation records and stack traces.
- [x] Clean runtime error propagation.
- [x] First end-to-end vertical slice program verified.

## Phase 3: First-Class Functions, Closures & Collections
- [x] Lexical closures capturing enclosing scope.
- [x] First-class function expressions / anonymous functions (`fn(x): x * 2`).
- [x] Array collections (`[elem1, elem2]`, indexing, length, push, pop, slice).
- [x] Map / dictionary collections (`{"key": val}`, indexing, keys, values).
- [x] Higher-order functions (`map`, `filter`, `reduce`, `every`, `some`).

## Phase 4: CLI, REPL & Developer Tooling
- [x] Unified CLI executable: `unfish [run|check|ast|tokens|repl|version]`.
- [x] Interactive REPL with persistent environment and multiline support.
- [x] Formatted diagnostic reporter with colorized output and educational hints.
- [x] Source code formatter (`unfish format`).

## Phase 5: Block ↔ AST ↔ Text Bi-Directional Representation
- [x] Formal block JSON/schema mapping to AST nodes (`unfish_blocks_v1`).
- [x] Lossless block-to-AST translator (`uf_blocks_import`).
- [x] Lossless AST-to-block generator (`uf_blocks_export`).
- [x] Automated round-trip test harness (`test_blocks`).

## Phase 6: Execution Visualization & Debugger
- [x] Runtime event subscription interface (trace events: step, call, return, bind, assign).
- [x] Debugger core: step over, step into, step out, breakpoints, variable inspection (`unfish debug`).
- [x] Execution visualizer data model (`unfish trace`).

## Phase 7: Bytecode Compiler & Virtual Machine (VM)
- [x] Intermediate Representation (IR) / 57-opcode instruction set.
- [x] AST-to-Bytecode compiler with backpatching and constant pooling.
- [x] Stack-based virtual machine (`unfish run --vm`) in C99.
- [x] VM call frames, operand stack, constant pool, upvalue closures.
- [x] Conformance testing verifying 100% parity between AST interpreter and VM.

## Phase 8: Standard Library Expansion & Module System
- [x] Module system: `import module`, `from module import symbol`, circular import detection.
- [x] Standard library modules: `sys`, `fs`, `time`, `random`, `json`, `testing`.
- [x] Sandboxed IO permissions and error reporting.

## Phase 9: Native Compilation & Optimizations
- [x] AST constant folding, dead code elimination, peephole optimizations.
- [x] Native C99 code generator (`unfish emit-c`).
- [x] Standalone multi-module binary compiler (`unfish build`).
- [x] 100% 3-way differential parity (AST Interpreter == VM == Native C99).

## Phase 10: Systems Programming & Educational Hardware Bridge
- [x] Controlled low-level constructs (fixed-size integers `u8`..`i32`, byte buffers).
- [x] Multi-byte little-endian access (`u16`, `u32`, `i32`).
- [x] Memory layout inspector (`inspect(val)`).

## Phase 11: Developer Ecosystem & Tooling (v1.7.0)
- [x] First-class test runner (`unfish test`) with discovery and test annotations.
- [x] Automated doc generator (`unfish doc`) for Markdown and dark-theme HTML.
- [x] Package manager (`unfish pkg`) with `unfish.toml`, project scaffolding, and build.
- [x] Interactive terminal tutorial (`unfish learn`) with 10 progressive lessons.
- [x] Interactive Web Playground (`unfish playground`) with embedded server and visual blocks.

## Phase 12: High-Performance Execution & WebAssembly Target (v1.9.0)
- [x] 3-address register virtual machine (`unfish run --regvm`) with 256 virtual registers and computed-goto dispatch.
- [x] Persistent bytecode caching (`.ufc` and `.ufrc`) with cryptographic source hashing and version validation.
- [x] WebAssembly backend (`unfish build --wasm`) targeting modern browsers and Node.js.
- [x] 5-way differential parity across all five execution targets.

## Phase 13: Unfish 2.0 Modern Systems & Tooling (v2.0.0)
- [x] Interfaces and traits (`trait`, `impl Trait for Struct`, dynamic dispatch vtables).
- [x] Parametric generics (`<T: Bound>`, compile-time monomorphization).
- [x] Asynchronous concurrency (`async`, `await`, `Promise` built upon fibers and scheduler).
- [x] Interactive Visual Block Studio (`unfish playground` GUI with live palette, block hierarchy manipulation, and real-time bidirectional sync).
- [x] Embedded ARM Runtime Profile (`--embedded`, `--arm`, bare-metal freestanding profile for ARM Cortex-M).


