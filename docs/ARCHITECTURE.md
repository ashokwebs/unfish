# UNFISH — SYSTEM ARCHITECTURE & ENGINE SPECIFICATION

---

## 1. Executive Architectural Overview

**Unfish** is engineered as a modular, layered language platform written in clean ANSI C99 with zero external dependencies.

Unlike traditional educational languages that sacrifice runtime rigor, or industrial languages that hide their internal machinery behind complex abstractions, Unfish provides a completely transparent, decoupled multi-backend pipeline. A single source program can be lexed, parsed, analyzed, and then routed to any of **five distinct execution backends**, all maintained in lockstep parity by a 100% differential verification test harness:

```
                                  ┌───────────────────────────────┐
                                  │      Unfish Source Code       │
                                  │         (*.unfish)            │
                                  └──────────────┬────────────────┘
                                                 │
                                                 ▼
                                  ┌───────────────────────────────┐
                                  │   Indentation Lexer Scanner   │  ◄── Arena Allocator
                                  │      (src/lexer/uf_lexer.c)   │  ◄── String Interner
                                  └──────────────┬────────────────┘
                                                 │ Token Stream (INDENT, DEDENT, ...)
                                                 ▼
                                  ┌───────────────────────────────┐
                                  │     Pratt Top-Down Parser     │  ◄── AST Node Arena
                                  │     (src/parser/uf_parser.c)  │
                                  └──────────────┬────────────────┘
                                                 │ Typed Abstract Syntax Tree (UfProgram)
                                                 ▼
                                  ┌───────────────────────────────┐
                                  │   Semantic & Scope Analyzer   │  ◄── Symbol Tables
                                  │   (src/semantic/uf_semantic.c)│  ◄── Gradual Type Checker
                                  └──────────────┬────────────────┘
                                                 │ Resolved & Validated AST
        ┌────────────────────────────────────────┼────────────────────────────────────────┐
        │                                        │                                        │
        ▼                                        ▼                                        ▼
┌───────────────────────┐            ┌───────────────────────┐            ┌───────────────────────┐
│  AST Tree-Walking     │            │   Bytecode Compiler   │            │   Native C99 Transpiler│
│  Interpreter          │            │(src/compiler/uf_*.c)  │            │(src/codegen/uf_emit_c)│
│(src/interpreter/)     │            └───────────┬───────────┘            └───────────┬───────────┘
└───────────┬───────────┘                        │                                    │
            │                                    │                                    ├───────────────────────┐
            │                     ┌──────────────┴──────────────┐                     │                       │
            │                     ▼                             ▼                     ▼                       ▼
            │          ┌─────────────────────┐       ┌─────────────────────┐   ┌───────────────┐     ┌────────────────┐
            │          │ Stack Bytecode VM   │       │ Register Bytecode VM│   │ GCC / Clang   │     │ Clang / WASI   │
            │          │ (src/vm/uf_vm.c)    │       │ (src/vm2/uf_regvm.c)│   │ Native Binary │     │ WebAssembly    │
            │          └──────────┬──────────┘       └──────────┬──────────┘   └───────┬───────┘     └────────┬───────┘
            │                     │                             │                      │                      │
            ▼                     ▼                             ▼                      ▼                      ▼
  ===================================================================================================================
                                      OBSERVABLE RUNTIME OUTPUT (100% PARITY)
  ===================================================================================================================
```

---

## 2. Complete Subsystem Inventory

Every subsystem in the Unfish codebase is implemented with zero external library requirements:

| Subsystem | Source Path | Header File | Status | Primary Responsibilities |
|---|---|---|---|---|
| **Linear Arena Allocator** | `src/common/uf_arena.c` | `src/common/uf_arena.h` | **100% IMPLEMENTED** | High-speed bump allocation, chunk chaining, $O(1)$ bulk deallocation |
| **String Interning Pool** | `src/common/uf_string.c` | `src/common/uf_string.h` | **100% IMPLEMENTED** | Global deduplication of identifiers, strings, and symbols |
| **Diagnostic Reporter** | `src/common/uf_diagnostic.c` | `src/common/uf_diagnostic.h` | **100% IMPLEMENTED** | 2-D coordinates, ANSI color highlights, caret squiggles, suggestions |
| **Source Tracking** | `src/common/uf_source.h` | `src/common/uf_source.h` | **100% IMPLEMENTED** | Byte offset, line, column, and file origin mapping |
| **Dynamic Arrays (C)** | `src/common/uf_array.h` | `src/common/uf_array.h` | **100% IMPLEMENTED** | Type-safe generic macro-based dynamic arrays in ANSI C |
| **Indentation Lexer** | `src/lexer/uf_lexer.c` | `src/lexer/uf_lexer.h` | **100% IMPLEMENTED** | Pythonic off-side rule, `INDENT`/`DEDENT` synthesis, escape sequences |
| **Token Representation** | `src/lexer/uf_token.c` | `src/lexer/uf_token.h` | **100% IMPLEMENTED** | 80+ token kinds, literal values, string representations |
| **Pratt Parser** | `src/parser/uf_parser.c` | `src/parser/uf_parser.h` | **100% IMPLEMENTED** | Precedence-climbing expression parser, block structure analysis |
| **Abstract Syntax Tree** | `src/ast/uf_ast.c` | `src/ast/uf_ast.h` | **100% IMPLEMENTED** | Strongly typed AST node definitions for statements and expressions |
| **Semantic Analyzer** | `src/semantic/uf_semantic.c` | `src/semantic/uf_semantic.h` | **100% IMPLEMENTED** | Scope tree, symbol resolution, gradual type checking, trait bounds |
| **Value & Object Model**| `src/runtime/uf_value.c` | `src/runtime/uf_value.h` | **100% IMPLEMENTED** | 16-byte tagged union (`UfValue`), heap object headers (`UfObj`) |
| **Runtime Environment** | `src/runtime/uf_env.c` | `src/runtime/uf_env.h` | **100% IMPLEMENTED** | Lexical scopes (`UfEnv`), binding hash tables, parent chain navigation |
| **Garbage Collector** | `src/runtime/uf_runtime.c` | `src/runtime/uf_object.h` | **100% IMPLEMENTED** | Object-tracked mark-and-sweep GC with temporary root stack |
| **AST Interpreter** | `src/interpreter/uf_interpreter.c` | `src/interpreter/uf_interpreter.h` | **100% IMPLEMENTED** | Direct AST tree-walker, interactive REPL engine, execution hooks |
| **Bytecode Chunk** | `src/compiler/uf_chunk.c` | `src/compiler/uf_chunk.h` | **100% IMPLEMENTED** | Linear instruction streams, constant pools, source line mapping |
| **Bytecode Compiler** | `src/compiler/uf_compiler.c` | `src/compiler/uf_compiler.h` | **100% IMPLEMENTED** | Single-pass AST-to-bytecode compiler, jump patching, upvalue capture |
| **Stack Bytecode VM** | `src/vm/uf_vm.c` | `src/vm/uf_vm.h` | **100% IMPLEMENTED** | 59-opcode virtual machine, operand stack, call frames, unwinding |
| **Bytecode Disassembler**| `src/vm/uf_disasm.c` | `src/vm/uf_disasm.h` | **100% IMPLEMENTED** | Human-readable instruction dumping with source coordinates |
| **Register Compiler** | `src/compiler/uf_reg_compiler.c`| `src/compiler/uf_reg_compiler.h` | **100% IMPLEMENTED** | Linear scan register allocation, 3-address instruction synthesis |
| **Register Bytecode VM**| `src/vm2/uf_regvm.c` | `src/vm2/uf_regvm.h` | **100% IMPLEMENTED** | 256-register computed-goto VM, register windowing |
| **Bytecode Optimizer** | `src/compiler/uf_optimize.c` | `src/compiler/uf_optimize.h` | **100% IMPLEMENTED** | Constant folding, dead code elimination, jump-to-jump chaining |
| **Bytecode Cache (.ufc)**| `src/compiler/uf_cache.c` | `src/compiler/uf_cache.h` | **100% IMPLEMENTED** | Binary serialization, checksumming, timestamp validation |
| **Native C99 Transpiler**| `src/codegen/uf_emit_c.c` | `src/codegen/uf_emit_c.h` | **100% IMPLEMENTED** | AOT code generation, freestanding C runtime embedding |
| **Standalone C Runtime** | `src/codegen/unfish_runtime.h` | `src/codegen/unfish_runtime.h` | **100% IMPLEMENTED** | Single-header complete C runtime for transpiled binaries |
| **Cooperative Fibers** | `src/runtime/uf_fiber.c` | `src/runtime/uf_fiber.h` | **100% IMPLEMENTED** | User-space lightweight threads, FIFO scheduler, call stack slices |
| **Channels & Promises** | `src/runtime/uf_fiber.c` | `src/runtime/uf_value.h` | **100% IMPLEMENTED** | FIFO message passing channels, async/await resolution |
| **Module System** | `src/runtime/uf_module.c` | `src/runtime/uf_module.h` | **100% IMPLEMENTED** | Module registry, circular dependency detector, import caching |
| **Standard Library** | `src/runtime/uf_stdlib.c` | `src/runtime/uf_stdlib.h` | **100% IMPLEMENTED** | 60+ built-in functions, strings, arrays, math, systems buffers |
| **Modules: sys, fs, ...**| `src/stdlib/uf_mod_*.c` | `src/stdlib/uf_mod_*.h` | **100% IMPLEMENTED** | `sys`, `fs`, `time`, `random`, `json`, `testing` modules |
| **Visual Block Bridge** | `src/blocks/uf_blocks_*.c` | `src/blocks/uf_blocks.h` | **100% IMPLEMENTED** | Bidirectional AST ↔ JSON visual block serialization |
| **Language Server (LSP)**| `src/lsp/uf_lsp.c` | `src/lsp/uf_lsp.h` | **100% IMPLEMENTED** | JSON-RPC 2.0 LSP 3.17 server for VS Code and modern IDEs |
| **CLI Step Debugger** | `src/debugger/uf_debugger.c` | `src/debugger/uf_debugger.h` | **100% IMPLEMENTED** | Breakpoints, stepping, local variables, call stacks, disasm |
| **Canonical Formatter** | `src/formatter/uf_formatter.c` | `src/formatter/uf_formatter.h` | **100% IMPLEMENTED** | AST pretty-printer, canonical 4-space indentation, comments |
| **Package Manager** | `src/tooling/uf_pkg.c` | `src/tooling/uf_pkg.h` | **100% IMPLEMENTED** | `unfish.toml` manifest, package initialization, builds, tasks |
| **Test Discovery Runner**| `src/tooling/uf_test_runner.c`| `src/tooling/uf_test_runner.h` | **100% IMPLEMENTED** | Automated discovery, filter patterns, TAP output |
| **Execution Profiler** | `src/tooling/uf_profiler.c` | `src/tooling/uf_profiler.h` | **100% IMPLEMENTED** | Function timing, call counts, memory allocation profiling |
| **Documentation Gen** | `src/tooling/uf_doc.c` | `src/tooling/uf_doc.h` | **100% IMPLEMENTED** | Extract `##` docstrings to Markdown and interactive HTML |
| **Interactive Tutorial** | `src/tooling/uf_learn.c` | `src/tooling/uf_learn.h` | **100% IMPLEMENTED** | 22-chapter progressive CLI tutorial and web course |
| **Playground & Studio** | `src/tooling/uf_playground.c` | `src/tooling/uf_playground.h` | **100% IMPLEMENTED** | Built-in HTTP server, REST execution API, browser IDE |
| **Unified CLI Driver** | `src/cli/main.c` | — | **100% IMPLEMENTED** | Master command-line interface dispatching all 22 subcommands |

---

## 3. The Decoupled Execution Pipeline

The Unfish architecture strictly separates compilation into distinct, independent phases. No phase has implicit knowledge of downstream consumers:

### 3.1. Phase 1: Lexical Analysis (`src/lexer/`)
The lexer converts a raw UTF-8 string buffer into a clean token stream.
* **Indentation Stack**: Unfish uses Python-style off-side indentation delimitation. The lexer maintains an internal stack of indentation column depths. When indentation increases, an `UF_TOK_INDENT` token is emitted. When indentation decreases, one or more `UF_TOK_DEDENT` tokens are synthesized.
* **Exact Coordinates**: Every token carries a `SourceSpan` containing start and end coordinates: file name, 0-indexed byte offset, 1-indexed line number, and 1-indexed column number.
* **String Processing**: String literals are scanned with full support for escape sequences (`\n`, `\t`, `\r`, `\"`, `\\`, `\0`, `\xHH`), multiline strings (`"""`), and interpolation expressions (`"${x}"`).

### 3.2. Phase 2: Pratt & Recursive Descent Parser (`src/parser/`)
The parser accepts the token stream and builds a strongly typed Abstract Syntax Tree (AST).
* **Statements**: Parsed via structured recursive descent (`uf_parse_stmt`). Statements include variable bindings (`let`), assignments, control flow (`if`, `while`, `for`, `repeat`), functions, structs, traits, enums, pattern matches, try/catch/finally, imports, and expressions.
* **Expressions**: Parsed using Vaughan Pratt's top-down operator precedence algorithm (`uf_parse_expr_precedence`). Precedence levels range from comma (lowest) to call/subscript (highest).
* **Arena Memory**: All AST nodes (`UfStmt`, `UfExpr`, parameter lists, pattern structures) are allocated in a linear `UfArena`. No node requires individual manual `free()` calls.

### 3.3. Phase 3: Semantic Analysis & Scope Binding (`src/semantic/`)
Before execution or compilation, the AST passes through semantic validation:
* **Lexical Scope Tree**: Tracks nested scopes (`UfScope`), resolving every identifier to its definition: local variable, function parameter, global binding, or built-in function.
* **Hoisting & Mutual Recursion**: Top-level function declarations are hoisted to the parent scope, allowing mutually recursive functions to reference each other regardless of declaration order.
* **Gradual Type Checking**: When type annotations are present (`let x: Number = 42`), the semantic analyzer verifies type compatibility. In `--strict` mode, type violations trigger fatal compilation errors.
* **Trait Bound Verification**: Validates that structs implementing traits satisfy all required method signatures and arities.

---

## 4. The Five Execution Backends

Once the AST is semantically validated, it can be executed across any of five backends:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                 THE FIVE BACKENDS                                      │
├────────────────────┬────────────────────┬────────────────────┬─────────────────────────┤
│ Backend            │ Execution Mode     │ Typical Speedup    │ Primary Target          │
├────────────────────┼────────────────────┼────────────────────┼─────────────────────────┤
│ 1. AST Interpreter │ Direct Tree-Walk   │ 1.0x (Baseline)    │ REPL, Debugger, Blocks  │
│ 2. Stack Bytecode  │ 59-Opcode Stack VM │ 2.0x – 6.0x        │ Standard Execution      │
│ 3. Register VM     │ 256-Reg Computed-G │ 3.0x – 9.0x        │ High-Speed Interpretation│
│ 4. Native C99 AOT  │ Compiled Machine   │ 25x – 60x          │ Production Native Apps  │
│ 5. WebAssembly     │ WASI / Browser Wasm│ 15x – 40x          │ Web Apps, Sandboxes     │
└────────────────────┴────────────────────┴────────────────────┴─────────────────────────┘
```

### Backend 1: Tree-Walking AST Interpreter (`src/interpreter/`)
* **Mechanism**: Direct recursive evaluation of AST statement and expression nodes.
* **Strengths**: Zero compilation overhead, immediate startup, direct mapping to source nodes, effortless integration with interactive step debuggers and visual blocks.
* **Scope Model**: Tree-structured environment objects (`UfEnv`), where each lexical block creates a new environment pointing to its parent enclosing environment.

### Backend 2: Stack-Based Bytecode Virtual Machine (`src/vm/`)
* **Mechanism**: Flat byte-oriented virtual CPU executing linear chunks of instructions.
* **Operand Stack**: Fixed-size or dynamically expanding evaluation stack of `UfValue` objects.
* **ISA**: 57 compact opcodes (`OP_CONSTANT`, `OP_LOAD_LOCAL`, `OP_ADD`, `OP_CLOSURE`, `OP_CALL`, etc.).
* **Upvalue Capture**: Stack variables captured by nested closures are managed via open upvalue linked lists. When a stack frame exits, open upvalues are closed into heap storage.
* **Exception Unwinding**: Structured try-catch unwinding tables restore the stack and frame pointers upon runtime errors.

### Backend 3: Register-Based Bytecode Virtual Machine (`src/vm2/`)
* **Mechanism**: 3-address register virtual machine modeled after modern high-performance runtimes like Lua 5.0 and LuaJIT.
* **Register Windowing**: Each call frame allocates a contiguous window of up to 256 virtual registers directly on the execution stack.
* **Dispatch**: Computed `goto` indirect threaded dispatch table (under GCC/Clang), minimizing CPU branch mispredictions.
* **Efficiency**: Eliminates up to 65% of instruction dispatches compared to the stack VM by performing operations directly between registers (`ROP_ADD: dest = src1 + src2`).

### Backend 4: Native C99 Ahead-of-Time Transpiler (`src/codegen/`)
* **Mechanism**: The AST is directly transpiled into clean, ANSI C99 source code.
* **Standalone Single Header**: The generated C code embeds `src/codegen/unfish_runtime.h`, a complete standalone runtime implementing values, garbage collection, collections, closures, and standard library functions in pure C99.
* **Compilation**: The emitted C file is compiled with native compilers (`gcc -O3` or `clang -O3`), generating native machine code binaries with zero runtime dependencies on `bin/unfish`.

### Backend 5: WebAssembly Backend (`build --wasm`)
* **Mechanism**: Leverages the C99 code generator to compile against the WebAssembly System Interface (WASI) sysroot using `clang --target=wasm32-wasi`.
* **Deployment**: The resulting `.wasm` binary can execute standalone in Node.js or directly in modern web browsers alongside a small JavaScript bridge.

---

## 5. Memory Management Architecture

Unfish employs a hybrid two-phase memory architecture that eliminates memory fragmentation during compilation while ensuring deterministic memory reclamation during program execution:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                             TWO-PHASE MEMORY ARCHITECTURE                              │
├───────────────────────────────────────────┬────────────────────────────────────────────┤
│         PHASE A: COMPILATION TIME         │           PHASE B: RUNTIME EXECUTION       │
│        (Linear Arena Allocators)          │     (Object-Tracked Mark-and-Sweep GC)     │
├───────────────────────────────────────────┼────────────────────────────────────────────┤
│ • AST Nodes (UfStmt, UfExpr)              │ • Dynamic Strings (UfStringObject)         │
│ • Token string buffers                    │ • First-Class Functions & Closures         │
│ • Symbol tables and scope records         │ • Dynamic Arrays (UfArrayObject)           │
│ • Bytecode compiler scratch structures    │ • Hash Maps (UfMapObject)                  │
│ • O(1) bulk disposal via uf_arena_free()  │ • Struct Definitions & Instances           │
│ • Zero fragmentation, zero leak risk      │ • Cooperative Fibers & Channel Buffers     │
└───────────────────────────────────────────┴────────────────────────────────────────────┘
```

### 5.1. Linear Arena Allocator (`uf_arena.c`)
During lexical scanning, parsing, and semantic analysis, thousands of small structures (AST nodes, token strings, symbol entries) are generated. Allocating each node with individual `malloc` calls would cause severe heap fragmentation and significant deallocation overhead.

Instead, Unfish uses a chained chunk arena (`UfArena`):
* Chunks are allocated in 8 KB blocks.
* Within a chunk, allocations are simple pointer increments (bump allocation), guaranteed to be 8-byte aligned.
* When compilation finishes (or a REPL command evaluates), the entire arena is reclaimed in $O(1)$ time by freeing the chunk list.

### 5.2. Mark-and-Sweep Garbage Collector (`uf_runtime.c`)
During program execution, values and objects are dynamically created and shared. Unfish employs a precise mark-and-sweep garbage collector:
* **Object Header (`UfObj`)**: Every heap-allocated object begins with a common header containing a type tag (`UF_OBJ_STRING`, `UF_OBJ_ARRAY`, etc.), a 1-bit mark flag, and an intrusive `next` pointer forming a global singly-linked list of all live objects.
* **Root Set Traversal**: When GC triggers, the mark phase traverses all active roots:
  1. The global environment (`rt->global_env`).
  2. The currently active block environment (`rt->current_env`).
  3. The active call stack frames and their local variables.
  4. The operand stack (in the Bytecode VM).
  5. The temporary evaluation root stack (`rt->temp_roots`).
  6. The open upvalue linked list.
  7. All active and suspended fibers in the concurrency scheduler.
* **Sweep Phase**: Sweeps the global object list, freeing unmarked objects and resetting mark bits on survivors.
* **Dynamic Threshold Scaling**: The collector tracks `bytes_allocated` and automatically adjusts the next trigger threshold based on heap growth heuristics.

---

## 6. Concurrency Architecture: Fibers & Channels

Unfish includes a first-class cooperative concurrency model built on lightweight user-space threads (fibers) and message-passing channels:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        COOPERATIVE FIBER SCHEDULER                     │
├────────────────────────────────────────────────────────────────────────┤
│ Fiber Queue: [ Fiber A (Active) ] ──► [ Fiber B ] ──► [ Fiber C ]      │
│                     │                                                  │
│                     │ yield() / recv()                                 │
│                     ▼                                                  │
│        Context Switch (Save Stack, Restore Next Fiber)                 │
└────────────────────────────────────────────────────────────────────────┘
```

1. **Lightweight Fibers (`UfFiber`)**: Fibers possess their own call stacks, instruction pointers, and error handlers, but execute within the same operating system thread.
2. **Cooperative Scheduling**: Scheduling is explicit. A fiber runs until it yields control (`yield()`), awaits a promise (`await`), or yields to the scheduler via `run_scheduler()`.
3. **Synchronous Channels (`UfChannel`)**: Provide thread-safe FIFO message queues for passing data between fibers (`send(ch, val)`, `recv(ch)`).
4. **Promises & Asynchrony**: Asynchronous functions (`async function`) wrap their computation in a `Promise` object that integrates seamlessly with the cooperative fiber runtime.

---

## 7. Five-Way Differential Parity Verification

The hallmark of the Unfish engineering philosophy is **absolute behavioral equivalence**. Every test in the language conformance suite is executed against all five backends:

```bash
unfish run script.unfish              # 1. AST Interpreter
unfish run --vm script.unfish         # 2. Stack Bytecode VM
unfish run --regvm script.unfish      # 3. Register Bytecode VM
unfish build -o bin/script && ./bin/script  # 4. Native C99 Binary
unfish build --wasm -o script.wasm    # 5. WebAssembly under Node/WASI
```

The differential test harness (`tools/run_differential_tests.sh`) captures standard output, standard error, and exit codes across all five backends. If a single byte or error exit code differs between any backend, the test suite halts immediately:

$$\text{Output}(\text{Interp}) \equiv \text{Output}(\text{VM}) \equiv \text{Output}(\text{RegVM}) \equiv \text{Output}(\text{Native}) \equiv \text{Output}(\text{WASM})$$

This rigorous contract guarantees that students and developers can trust that code written for the visual playground or interpreter will behave identically when compiled to native machine code or WebAssembly.
