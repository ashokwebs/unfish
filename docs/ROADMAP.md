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
- [ ] First-class function expressions / anonymous functions.
- [x] Array collections (`[elem1, elem2]`, indexing, length, push, pop).
- [ ] Map / dictionary collections (`{"key": val}`, indexing, keys, values).
- [ ] Higher-order functions (`map`, `filter`, `reduce`).

## Phase 4: CLI, REPL & Developer Tooling
- [x] Unified CLI executable: `unfish [run|check|ast|tokens|repl|version]`.
- [x] Interactive REPL with persistent environment and multiline support.
- [x] Formatted diagnostic reporter with colorized output and educational hints.
- [ ] Source code formatter (`unfish format`).

## Phase 5: Block ↔ AST ↔ Text Bi-Directional Representation
- [ ] Formal block JSON/schema mapping to AST nodes.
- [ ] Lossless block-to-AST translator.
- [ ] Lossless AST-to-block generator.
- [ ] Automated round-trip test harness (`blocks -> AST -> text -> AST -> blocks`).

## Phase 6: Execution Visualization & Debugger
- [ ] Runtime event subscription interface (trace events: step, call, return, bind, assign).
- [ ] Debugger core: step over, step into, step out, breakpoints, variable inspection.
- [ ] Execution visualizer data model (variable state, stack frames, heap objects).

## Phase 7: Bytecode Compiler & Virtual Machine (VM)
- [ ] Intermediate Representation (IR) / Bytecode instruction set design.
- [ ] AST-to-Bytecode compiler.
- [ ] Register- or stack-based Unfish VM in C.
- [ ] VM call frames, operand stack, constant pool, jump tables.
- [ ] Conformance testing verifying parity between AST interpreter and VM.

## Phase 8: Standard Library Expansion & Module System
- [ ] Module system: `import module`, `from module import symbol`, relative path resolution, circular import detection.
- [ ] Standard library modules: `math`, `strings`, `fs` (sandboxed), `time`, `random`, `sys`.
- [ ] Sandbox permission model (read-only file access, memory quotas, execution timeouts).

## Phase 9: Native Compilation & Optimizations
- [ ] SSA-based IR.
- [ ] Constant folding, dead code elimination, inline caching.
- [ ] C code emission backend and/or LLVM / native machine code backend.

## Phase 10: Systems Programming & Educational Hardware Bridge
- [ ] Controlled low-level constructs (fixed-size integers, explicit memory buffers, pointer visualization).
- [ ] Memory layout inspector for educational visualization.
- [ ] Embedded runtime targets (microcontrollers / WebAssembly).
