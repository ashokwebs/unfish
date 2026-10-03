# UNFISH — TECHNICAL ROADMAP, COMPLETED PHASES & FUTURE HORIZONS

---

## 1. Executive Roadmap Summary

The evolution of Unfish is divided into twelve disciplined architectural phases. As of release **v2.1.0**, Phases 0 through 10 are **100% complete, verified by 95/95 passing differential conformance tests, and fully operational across all platforms**.

This document records the completed milestone ledger and outlines future engineering horizons.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        UNFISH ENGINEERING PHASES                       │
├──────────┬──────────────────────────────────────────────┬──────────────┤
│ Phase    │ Description                                  │ Status       │
├──────────┼──────────────────────────────────────────────┼──────────────┤
│ Phase 0  │ Core ANSI C99 Subsystems & Linear Arenas     │ COMPLETED    │
│ Phase 1  │ Indentation Lexer, Pratt Parser & Typed AST  │ COMPLETED    │
│ Phase 2  │ AST Tree-Walking Interpreter & Interactive REPL COMPLETED   │
│ Phase 3  │ Standard Library, Hash Maps & Dynamic Strings│ COMPLETED    │
│ Phase 4  │ Structs, Methods, Traits, Enums & Generics   │ COMPLETED    │
│ Phase 5  │ Pattern Matching, Destructuring & Pipes      │ COMPLETED    │
│ Phase 6  │ 4-Tier Gradual Type System & Semantic Check  │ COMPLETED    │
│ Phase 7  │ 59-Opcode Stack Bytecode VM & Disassembler   │ COMPLETED    │
│ Phase 8  │ 256-Register Computed-Goto Bytecode VM       │ COMPLETED    │
│ Phase 9  │ Native C99 Transpiler, WASM & Bare-Metal ARM │ COMPLETED    │
│ Phase 10 │ Developer Tooling (LSP, Debugger, Studio)    │ COMPLETED    │
│ Phase 11 │ Native Just-In-Time (JIT) Compiler           │ RESEARCH     │
│ Phase 12 │ Preemptive Concurrency & M:N Work Stealing   │ RESEARCH     │
│ Phase 13 │ Self-Hosting Compiler (Unfish-in-Unfish)     │ PLANNED      │
└──────────┴──────────────────────────────────────────────┴──────────────┘
```

---

## 2. Completed Milestones Retrospective (Phases 0–10)

### Phase 0: Foundations & Memory Subsystem
* Contiguous chunk arena allocator (`uf_arena.c`) with 8-byte alignment and $O(1)$ bulk disposal.
* Global string interning table (`uf_string.c`) with FNV-1a hashing.
* Diagnostic engine (`uf_diagnostic.c`) with 2-D coordinates and ANSI color squiggles.
* Generic macro dynamic arrays (`uf_array.h`).

### Phase 1: Lexical & Syntactic Analysis
* Indentation scanner tracking off-side rule with indentation stack (`INDENT`, `DEDENT`, `NEWLINE`).
* Vaughan Pratt top-down operator precedence expression parser with 13 precedence tiers.
* Strongly typed Abstract Syntax Tree node hierarchies (`UfProgram`, `UfStmt`, `UfExpr`).

### Phase 2: Execution & Garbage Collection
* Recursive AST tree-walking interpreter (`uf_interpreter.c`).
* 16-byte tagged union value engine (`UfValue`).
* Object-tracked mark-and-sweep garbage collector with temporary root protection stack (`rt->temp_roots`).
* Interactive Read-Eval-Print Loop (`unfish repl`) with persistent session arena.

### Phase 3: Collections & Standard Library
* Resizable dynamic arrays and open-addressed quadratic probing hash maps.
* 20+ string manipulation functions (`split`, `join`, `trim`, `replace`).
* Core standard modules: `sys`, `fs`, `time`, `random`, `json`, `testing`.

### Phase 4: Object-Oriented & Algebraic Data Types
* User-defined `struct` declarations with constructor generation and method dispatch (`self`).
* Sum-type `enum` declarations with multi-arity payload variants.
* Structural `trait` interfaces and generic trait bounds (`<T: Printable>`).

### Phase 5: Modern Functional Ergonomics
* Structural pattern matching (`match`) with literal, variable, wildcard, array, and enum patterns.
* Array and map destructuring assignments (`let [a, b, ...rest] = arr`).
* Forward pipeline dataflow operator (`|>`).
* List comprehensions (`[x * 2 for x in items if x > 0]`).

### Phase 6: Gradual Type System
* 4-Tier Gradual Type model (Dynamic, Inferred, Annotated, Strict).
* Compile-time type inference and consistency validation in `uf_semantic.c`.
* `--strict` mode turning gradual inconsistencies into fatal compilation errors.

### Phase 7: Stack Bytecode Virtual Machine
* 59-opcode stack-based virtual machine (`uf_vm.c`).
* Single-pass bytecode compiler (`uf_compiler.c`) with jump backpatching.
* Lexical upvalue capture cells with open/closed linked list migration.
* Bytecode disassembler (`unfish disasm`) and step-by-step visual stack tracer.

### Phase 8: Register Bytecode Virtual Machine
* 256-register, 3-address virtual machine (`uf_regvm.c`).
* Direct-threaded computed-goto dispatch loop eliminating branch mispredictions.
* Linear-scan register allocation compiler (`uf_reg_compiler.c`).
* Bytecode disk caching (`.ufc`, `.ufrc`) with cryptographic SHA-256 validation.

### Phase 9: Native AOT Transpiler, WebAssembly & Embedded
* Standalone ANSI C99 code generator (`uf_emit_c.c`).
* Standalone single-header runtime (`unfish_runtime.h`) with zero external dependencies.
* Native executable compilation via system GCC/Clang (`-O3`), delivering 25×–60× speedups.
* WebAssembly backend (`--wasm`) targeting WASI.
* Embedded bare-metal compilation (`-DUF_EMBEDDED`) and ARM Cortex-M cross-compilation.

### Phase 10: Tooling Ecosystem & Web IDE
* JSON-RPC 2.0 Language Server Protocol (LSP 3.17) server (`uf_lsp.c`).
* Source-level interactive step debugger (`uf_debugger.c`).
* Canonical code formatter (`uf_formatter.c`).
* Package manager (`uf_pkg.c`) and test discovery runner (`uf_test_runner.c`).
* Execution profiler (`uf_profiler.c`) and documentation generator (`uf_doc.c`).
* Unfish Studio browser IDE (`web/studio.html`) and Unfish Learn tutorial (`web/learn.html`).

---

## 3. Future Architectural Horizons (Phases 11–13)

### Phase 11: Native Just-In-Time (JIT) Compiler
* **Objective**: Compile hot bytecode loops into native x86-64 and AArch64 machine instructions at runtime.
* **Architecture**: Trace-based JIT or method-based JIT using DynASM or lightweight native machine code emission into executable memory pages (`mprotect` / `VirtualProtect`).
* **Target Speedup**: Approaching C / LuaJIT execution speeds within 1.5×–2.0× of native C.

### Phase 12: Preemptive Concurrency & M:N Work-Stealing
* **Objective**: Evolve cooperative fibers into an industrial-grade M:N threading runtime.
* **Architecture**: A pool of $M$ operating system worker threads executing $N$ user-space fibers using a Chase-Lev work-stealing deque.
* **Features**: Preemptive timer signals interrupting compute-bound fibers, bounded channel backpressure with thread synchronization.

### Phase 13: Self-Hosting Compiler
* **Objective**: Re-implement the Unfish lexer, Pratt parser, semantic analyzer, and code generators directly in pure Unfish source code.
* **Milestone**: The Unfish-written compiler compiles itself using the C99 native transpiler, establishing language maturity and proving standard library completeness.

---

## 4. Semantic Versioning & Backwards Compatibility Commitments

Unfish adheres strictly to Semantic Versioning (SemVer 2.0.0):
1. **Patch Releases (`v2.1.x`)**: Bug fixes, compiler optimizations, diagnostic improvements, and documentation enhancements. Zero syntax or behavioral breaking changes.
2. **Minor Releases (`v2.x.0`)**: Backwards-compatible language additions, new standard library functions, and tooling features.
3. **Major Releases (`v3.0.0`)**: Reserved exclusively for major paradigm shifts, accompanied by automated migration tooling (`unfish migrate`).
