# The Unfish Programming Language: Architecture, Design, and Implementation
## A Comprehensive Technical Manual and Systems Reference Compendium

---

## Welcome to the Unfish Documentation Suite

**Unfish** is a general-purpose programming language, multi-tier execution runtime, optimizing compiler, and educational computing platform written in pure ANSI C99 with zero external dependencies.

This documentation suite is organized as a multi-volume, book-length technical compendium covering language theory, grammar specifications, virtual machine design, compiler construction, garbage collection, gradual type theory, developer tooling, and systems programming. Whether you are an educator, student, compiler engineer, or systems programmer, this compendium provides an unbroken path of intellectual ascent from visual blocks to low-level systems programming.

---

## Table of Contents

```
================================================================================
                    THE UNFISH TECHNICAL COMPENDIUM
================================================================================
```

### Volume I: Philosophy, Vision & System Architecture
* [**Chapter 1: Vision & Pedagogical Philosophy**](file:///home/charizard/unfish/docs/VISION.md)
  * The crisis in introductory computer science education
  * The Principle of Intellectual Ascent (Blocks → Text → Architecture → Compilers → Systems)
  * The "Glass Box" pedagogical paradigm
  * Detailed comparative analysis: Unfish vs. Scratch, Python, Lua, Wren, Rust, C
  * Curriculum integration guide from primary education to graduate compiler courses

* [**Chapter 2: System Architecture Overview**](file:///home/charizard/unfish/docs/ARCHITECTURE.md)
  * The decoupled multi-stage translation pipeline
  * Detailed review of the five execution backends (AST Interpreter, Stack VM, Register VM, Native C99, WebAssembly)
  * Subsystem interaction diagrams and memory boundaries
  * The zero-dependency ANSI C99 engineering contract
  * Five-way differential parity guarantee and verification architecture

* [**Chapter 3: Theoretical & Academic Foundations**](file:///home/charizard/unfish/docs/RESEARCH.md)
  * Pratt Top-Down Operator Precedence parsing (Pratt, 1973)
  * Lexical upvalue capture and open/closed upvalue lists (Ierusalimschy et al., Lua 5.0)
  * Register-based vs. Stack-based virtual machine architectures (Davis et al., Shi et al.)
  * Gradual typing semantics and dynamic consistency relations (Siek & Taha, Thatte)
  * Dual visual/textual representations and cognitive continuity (Resnick et al., Repenning)
  * Communicating Sequential Processes and cooperative fibers (Hoare, 1978)

* [**Chapter 4: Architectural Decision Records (ADRs)**](file:///home/charizard/unfish/docs/DECISIONS.md)
  * Complete repository of 38 formal Architectural Decision Records
  * Trade-off analyses, alternatives considered, and downstream consequences
  * Evolution history from Phase 0 to Phase 10 (v2.1.0)

---

### Volume II: Language Specification, Type System & Errors
* [**Chapter 5: Formal Language Specification**](file:///home/charizard/unfish/docs/LANGUAGE_SPEC.md)
  * Lexical conventions, UTF-8 encoding, indentation off-side rules (`INDENT`, `DEDENT`, `NEWLINE`)
  * Complete, disambiguated formal EBNF grammar
  * Literals, expressions, 13-tier operator precedence table, and pipe operators (`|>`)
  * Control flow statements (`if`/`elif`/`else`, `while`, `for ... in`, `repeat`, `break`, `continue`)
  * Functions, lexical closures, default arguments, rest parameters, and spread calls
  * Data structures: dynamic arrays and open-addressed hash maps
  * Object-oriented programming: `struct`, methods, constructors, and `self`
  * Algebraic data types: `enum` definitions, variants, and payload data
  * Comprehensive pattern matching: literals, variables, wildcards, destructuring, and guards
  * Traits and polymorphism: `trait` definitions, `impl` blocks, and generic bounds

* [**Chapter 6: Gradual Type System & Semantic Analysis**](file:///home/charizard/unfish/docs/TYPE_SYSTEM.md)
  * The 4-tier gradual typing model (Dynamic, Inferred, Annotated, Strict)
  * Type lattice, primitive types, composite types, and callable types
  * Parametric generics (`<T: Bound>`) and trait bound resolution
  * Bidirectional type checking algorithms and type inference in `uf_semantic.c`
  * Symbol resolution, lexical scoping, hoisting, and mutual recursion analysis
  * Strict mode enforcement (`--strict`) and static safety guarantees

* [**Chapter 7: Error Model, Diagnostics & Exception Handling**](file:///home/charizard/unfish/docs/ERROR_MODEL.md)
  * Compile-time diagnostics engine: source spans, 2-D coordinates, ANSI color rendering
  * Caret and squiggly underlines (`^~~~`), intelligent error messages, and fix suggestions
  * Runtime exception architecture: `try`, `catch`, `finally`, `raise`, and `error()`
  * Stack unwinding across all five backends (AST, Stack VM, RegVM, Native, WASM)
  * Panic handling, uncaught exceptions, and stack trace generation

---

### Volume III: Virtual Machines, Compilers & Code Generation
* [**Chapter 8: Virtual Machine Specification & Instruction Sets**](file:///home/charizard/unfish/docs/VM.md)
  * Stack-based VM: execution loop, operand stack, call frames, and instruction dispatch
  * Complete 57-opcode Stack VM ISA reference table with stack effects and formal semantics
  * Lexical upvalue capture implementation: open upvalue linked list and closing mechanism
  * Register-based VM (RegVM): 256-register windowing, 3-address instructions, computed-goto dispatch
  * Complete 40+ opcode Register VM ISA reference table and register allocation strategy
  * Comparative architectural and performance analysis: Stack VM vs. Register VM

* [**Chapter 9: Bytecode Compiler, Optimization & Caching**](file:///home/charizard/unfish/docs/COMPILER.md)
  * Single-pass bytecode compilation pipeline (`uf_compiler.c`)
  * Scope resolution, local slot allocation, and upvalue resolution
  * Jump patching, forward jump backpatching, and loop control resolution
  * Bytecode optimizer (`uf_optimize.c`): constant folding, dead code elimination, peephole optimizations
  * Register code generation (`uf_reg_compiler.c`)
  * Bytecode serialization and disk caching: `.ufc` and `.ufrc` binary format specifications

* [**Chapter 10: Native C99 Transpiler, WebAssembly & Embedded**](file:///home/charizard/unfish/docs/NATIVE_COMPILER.md)
  * Ahead-of-Time (AOT) C99 transpilation pipeline (`uf_emit_c.c`)
  * The single-header standalone runtime engine (`unfish_runtime.h`)
  * Dynamic values, collections, and closure lifting in pure ANSI C99
  * Native binary compilation with GCC and Clang (`-O3`)
  * WebAssembly target compilation (`--wasm`) via WASI sysroot integration
  * Embedded and bare-metal compilation (`-DUF_EMBEDDED`) and ARM Cortex-M cross-compilation

* [**Chapter 11: The Hacker's Guide to Compiler & VM Internals**](file:///home/charizard/unfish/docs/INTERNALS_GUIDE.md)
  * Architectural overview and subsystem dependency graph
  * Memory arenas, string interning, and tri-color mark-sweep garbage collection
  * Hand-crafted scanner and indentation stack mechanics
  * Pratt parsing dispatch loop and binding power tables
  * Step-by-step tutorial: adding a new built-in function, keyword, or bytecode opcode

* [**Chapter 12: Runtime Architecture, Values & Concurrency**](file:///home/charizard/unfish/docs/RUNTIME.md)
  * Tagged union representation (`UfValue`) and heap object headers (`UfObj`)
  * Environment chains (`UfEnv`), activation records, and call stack boundaries
  * Cooperative concurrency subsystem: `UfFiber`, fiber states, and scheduling queues
  * FIFO cooperative scheduler semantics (`run_scheduler`, `spawn`, `yield`)
  * Communication channels (`UfChannel`): FIFO message passing and synchronization
  * Promises and asynchronous programming (`Promise`, `async`, `await`)

---

### Volume IV: Memory Management, Concurrency & Systems Programming
* [**Chapter 13: Memory Model, Arenas & Garbage Collection**](file:///home/charizard/unfish/docs/MEMORY_MODEL.md)
  * Two-phase memory model: compilation arenas vs. runtime heap
  * Chunk arena allocator (`uf_arena.c`): bump allocation, 8-byte alignment, zero-fragmentation
  * Mark-and-sweep garbage collector: object tracking, heap linked lists, allocation triggers
  * Root set discovery: global environment, call stack, operand stack, temporary roots, open upvalues, fibers
  * Cycle-safe graph marking and memory sweeping
  * Zero-leak verification under AddressSanitizer (ASan) and LeakSanitizer (LSan)

* [**Chapter 14: Systems & Low-Level Programming Manual**](file:///home/charizard/unfish/docs/SYSTEMS_PROGRAMMING.md)
  * Contiguous raw byte buffers (`buffer`, `buffer_get`, `buffer_set`, `buffer_fill`)
  * Endian-explicit multi-byte access (`buffer_read_u16_le`, `buffer_write_u32_le`, etc.)
  * Bitwise arithmetic: `band`, `bor`, `bxor`, `bnot`, `shl`, `shr`, `sar`
  * Integer bit casting: `u8`, `i8`, `u16`, `i16`, `u32`, `i32`
  * Memory-mapped I/O (MMIO), bare-metal microcontrollers, and WebAssembly linear memory
  * Memory safety guarantees, bounds checking, and ASan validation

* [**Chapter 15: Concurrency & Async Architecture Manual**](file:///home/charizard/unfish/docs/CONCURRENCY.md)
  * Concurrency philosophy: M:1 cooperative fibers vs preemptive OS threads
  * Fiber lifecycle state machine: `READY`, `RUNNING`, `WAITING_SEND`, `WAITING_RECV`, `DEAD`
  * CSP channels: unbuffered rendezvous vs bounded ring buffer queues
  * Central cooperative scheduler mechanics and deadlock detection
  * Async / await syntactic sugar transformation to fibers and futures
  * Garbage collection root scanning across concurrent fiber stacks

* [**Chapter 16: Security Model, Sandboxing & Resource Limits**](file:///home/charizard/unfish/docs/SECURITY.md)
  * Threat model: untrusted educational code execution and multi-tenant hosting
  * Memory safety guarantees in pure C99 with bounds checking
  * Resource quotas: recursion depth limits (`UF_MAX_CALL_DEPTH`), memory allocation caps
  * Execution gas and instruction count limits
  * Capability-based I/O permissions and filesystem path jailing
  * WebAssembly browser sandbox guarantees

* [**Chapter 17: Exhaustive Standard Library Reference**](file:///home/charizard/unfish/docs/STANDARD_LIBRARY.md)
  * Core built-in functions: `say`, `print`, `type_of`, `len`, `push`, `pop`, `range`, `keys`, `values`, `has_key`, `delete`, `map`, `filter`, `reduce`, `sort`, `reverse`, `find`, `every`, `some`, `clock`, `assert`
  * String library: 20 functions (`split`, `join`, `trim`, `replace`, `to_upper`, `pad_start`, `chars`, etc.)
  * Array library: `concat`, `flatten`, `fill`, `zip`
  * Math library: `abs`, `floor`, `ceil`, `sqrt`, `pow`, `min`, `max`, `log`, `sin`, `cos`, `tan`, `random`, `random_int`, constants `PI`, `E`, `INFINITY`
  * Module `sys`: `argv`, `platform`, `version`, `exit`, `env`, `set_env`, `cwd`, `exec`
  * Module `fs`: `read_file`, `write_file`, `append_file`, `exists`, `remove`, `list_dir`, `mkdir`, `is_file`, `is_dir`, `file_size`
  * Module `time`: `now`, `sleep`, `format`, `iso`, `parse`, `diff_ms`
  * Module `random`: `random`, `random_int`, `choice`, `shuffle`, `seed`
  * Module `json`: `parse`, `stringify` with cycle detection and number formatting
  * Module `testing`: `describe`, `test`, `assert_eq`, `assert_ne`, `run_tests`

* [**Chapter 18: Module System & Package Architecture**](file:///home/charizard/unfish/docs/MODULES.md)
  * Module syntax: `import <mod>` and `from <mod> import <sym1>, <sym2>`
  * Module resolution algorithm across native built-ins, relative paths, and package roots
  * Circular dependency detection and cycle reporting
  * Module caching and singleton module instances
  * Package manager (`unfish pkg`): `unfish.toml` manifest specification and dependency resolution

---

### Volume V: Tooling, Visual Programming & Quality Assurance
* [**Chapter 19: Developer Tooling & CLI Ecosystem**](file:///home/charizard/unfish/docs/TOOLING.md)
  * Unified CLI reference: 22 subcommands and execution flags
  * Language Server Protocol (LSP 3.17) implementation (`uf_lsp.c`): hover, completion, diagnostics, formatting
  * Canonical code formatter (`uf_formatter.c`): AST pretty-printing, canonical indentation, in-place formatting
  * Package manager (`uf_pkg.c`), Test runner (`uf_test_runner.c`), Profiler (`uf_profiler.c`), Doc generator (`uf_doc.c`)
  * Interactive REPL with multiline editing and persistent session arena
  * Unfish Studio browser IDE (`web/studio.html`) and Unfish Learn platform (`web/learn.html`)

* [**Chapter 20: Interactive CLI Debugger**](file:///home/charizard/unfish/docs/DEBUGGER.md)
  * Debugger architecture (`uf_debugger.c`): breakpoint management, step hooks, execution states
  * Command reference: `break`, `clear`, `step`, `next`, `finish`, `continue`, `print`, `locals`, `stack`, `disasm`
  * Call frame inspection, scope chain navigation, and bytecode disassembly stepping

* [**Chapter 21: Visual Block Programming & Bidirectional Round-Tripping**](file:///home/charizard/unfish/docs/BLOCKS.md)
  * Visual block architecture: AST-to-JSON and JSON-to-AST transformation pipelines
  * Complete JSON Block Schema specification for statements, expressions, and containers
  * Bidirectional round-tripping fidelity guarantees and verification
  * Scratch and Blockly compatibility bridge

* [**Chapter 22: Testing Strategy & Five-Way Differential Verification**](file:///home/charizard/unfish/docs/TESTING.md)
  * Multi-tiered verification hierarchy: unit tests, conformance tests, differential tests, stress tests
  * The 5-way differential testing harness (`tools/run_differential_tests.sh`)
  * Conformance test suite structure and error verification methodology
  * AddressSanitizer, LeakSanitizer, and UndefinedBehaviorSanitizer integration
  * Mutation and property-based fuzz testing

* [**Chapter 23: Performance Engineering & Benchmarking**](file:///home/charizard/unfish/docs/PERFORMANCE.md)
  * Benchmark methodology across 13 diverse computational workloads
  * Comparative performance analysis: AST vs. Stack VM vs. RegVM vs. Native C99 vs. WASM vs. CPython vs. Lua
  * Instruction dispatch overhead, cache locality, and memory footprint
  * Garbage collection pause times and throughput benchmarks
  * Hardware performance counter profiling (cache misses, branch mispredictions)

* [**Chapter 24: Known Limitations, Edge Cases & Workarounds**](file:///home/charizard/unfish/docs/KNOWN_ISSUES.md)
  * Current scheduling semantics for fibers and non-preemptive coroutines
  * Unbounded channel capacity and non-blocking backpressure semantics
  * Floating-point precision bounds and 64-bit integer ranges
  * Tail call optimization status and deep recursion workarounds
  * Gradual typing dynamic boundary checks

* [**Chapter 25: Engineering Roadmap & Future Horizons**](file:///home/charizard/unfish/docs/ROADMAP.md)
  * Milestone retrospective: Phase 0 through Phase 10 completion
  * Future architectures: Just-In-Time (JIT) compilation, M:N preemptive threading, self-hosting compiler
  * Foreign Function Interface (FFI) and formal verification tools
  * Semantic versioning policy and stability guarantees

* [**Chapter 26: Codebase Inventory, Census & Health Metrics**](file:///home/charizard/unfish/docs/PROJECT_STATE.md)
  * Complete source code census: lines of code, file counts, and component breakdown
  * Subsystem completion matrix: 100% verified status across all components
  * Conformance test results (91/91 passing differential tests)
  * Target platform support: Linux x86_64/ARM64, macOS, Windows MinGW, WASI WebAssembly, ARM Cortex-M

---

### Volume VI: Practical Cookbooks, Design Patterns & Idioms
* [**Chapter 27: The Unfish Practical Cookbook**](file:///home/charizard/unfish/docs/COOKBOOK.md)
  * 25 production-grade recipes across algorithms, systems, networking, and concurrency
  * CLI flag parsing, streaming log analysis, structured JSON validation
  * Data wrangling with `|>` pipelines and list comprehensions
  * High-performance algorithms: LRU cache, tries, binary min-heaps, disjoint set union
  * Systems & low-level: binary frame packing/unpacking, CRC-32, BMP headers, bitmask permission sets
  * Concurrency: CSP producer-consumer pipelines, fiber worker pools, observer pattern, circular ring buffers

---

## Quick Navigation

| Topic | Primary Reference Document | Source Implementation |
|---|---|---|
| **Grammar & Syntax** | [docs/LANGUAGE_SPEC.md](file:///home/charizard/unfish/docs/LANGUAGE_SPEC.md) | `src/parser/uf_parser.c` |
| **System Architecture** | [docs/ARCHITECTURE.md](file:///home/charizard/unfish/docs/ARCHITECTURE.md) | `src/cli/main.c` |
| **Cookbook Recipes** | [docs/COOKBOOK.md](file:///home/charizard/unfish/docs/COOKBOOK.md) | Practical Examples |
| **Concurrency & Async** | [docs/CONCURRENCY.md](file:///home/charizard/unfish/docs/CONCURRENCY.md) | `src/runtime/uf_fiber.c` |
| **Systems & Buffers** | [docs/SYSTEMS_PROGRAMMING.md](file:///home/charizard/unfish/docs/SYSTEMS_PROGRAMMING.md) | `src/runtime/uf_stdlib.c` |
| **Compiler Internals** | [docs/INTERNALS_GUIDE.md](file:///home/charizard/unfish/docs/INTERNALS_GUIDE.md) | `src/compiler/uf_compiler.c` |
| **Stack Bytecode VM** | [docs/VM.md](file:///home/charizard/unfish/docs/VM.md) | `src/vm/uf_vm.c`, `src/compiler/uf_compiler.c` |
| **Register Bytecode VM** | [docs/VM.md](file:///home/charizard/unfish/docs/VM.md) | `src/vm2/uf_regvm.c`, `src/compiler/uf_reg_compiler.c` |
| **Native C99 Transpiler** | [docs/NATIVE_COMPILER.md](file:///home/charizard/unfish/docs/NATIVE_COMPILER.md) | `src/codegen/uf_emit_c.c`, `src/codegen/unfish_runtime.h` |
| **Memory & GC** | [docs/MEMORY_MODEL.md](file:///home/charizard/unfish/docs/MEMORY_MODEL.md) | `src/common/uf_arena.c`, `src/runtime/uf_runtime.c` |
| **Standard Library** | [docs/STANDARD_LIBRARY.md](file:///home/charizard/unfish/docs/STANDARD_LIBRARY.md) | `src/runtime/uf_stdlib.c`, `src/stdlib/` |
| **Gradual Type System** | [docs/TYPE_SYSTEM.md](file:///home/charizard/unfish/docs/TYPE_SYSTEM.md) | `src/semantic/uf_semantic.c` |
| **Diagnostics & Errors** | [docs/ERROR_MODEL.md](file:///home/charizard/unfish/docs/ERROR_MODEL.md) | `src/common/uf_diagnostic.c` |
| **Tooling & LSP** | [docs/TOOLING.md](file:///home/charizard/unfish/docs/TOOLING.md) | `src/lsp/uf_lsp.c`, `src/tooling/` |
| **CLI Debugger** | [docs/DEBUGGER.md](file:///home/charizard/unfish/docs/DEBUGGER.md) | `src/debugger/uf_debugger.c` |
| **Visual Blocks** | [docs/BLOCKS.md](file:///home/charizard/unfish/docs/BLOCKS.md) | `src/blocks/uf_blocks_export.c`, `src/blocks/uf_blocks_import.c` |
| **Testing & Parity** | [docs/TESTING.md](file:///home/charizard/unfish/docs/TESTING.md) | `tests/conformance/`, `tools/` |
| **Performance & Benchmarks**| [docs/PERFORMANCE.md](file:///home/charizard/unfish/docs/PERFORMANCE.md) | `tests/benchmarks/` |
| **Security & Sandbox** | [docs/SECURITY.md](file:///home/charizard/unfish/docs/SECURITY.md) | `src/runtime/uf_runtime.c` |
| **Architectural Decisions** | [docs/DECISIONS.md](file:///home/charizard/unfish/docs/DECISIONS.md) | Architecture Decision Records 001–038 |
| **Theory & Research** | [docs/RESEARCH.md](file:///home/charizard/unfish/docs/RESEARCH.md) | Academic Foundations |
