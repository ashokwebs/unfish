# UNFISH — ARCHITECTURE OVERVIEW

---

## 1. Subsystem Status Inventory

| Subsystem | Status | Implementation Reference |
|---|---|---|
| Linear Arena Memory Manager | **IMPLEMENTED** | `src/common/uf_arena.c` |
| String Interning & Dynamic Buffers | **IMPLEMENTED** | `src/common/uf_string.c` |
| Diagnostic & Source Span Engine | **IMPLEMENTED** | `src/common/uf_diagnostic.c` |
| Indentation Lexical Scanner | **IMPLEMENTED** | `src/lexer/uf_lexer.c` |
| Pratt Expression & Block Parser | **IMPLEMENTED** | `src/parser/uf_parser.c` |
| Typed Abstract Syntax Tree | **IMPLEMENTED** | `src/ast/uf_ast.c` |
| Semantic Analyzer & Scope Resolver | **IMPLEMENTED** | `src/semantic/uf_semantic.c` |
| Mark-and-Sweep Garbage Collection | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| AST Tree-Walking Interpreter | **IMPLEMENTED** | `src/interpreter/uf_interpreter.c` |
| Command-Line Tool (`unfish`) | **IMPLEMENTED** | `src/cli/main.c` |
| Intermediate Representation (IR) | **PLANNED** (Phase 7) | Target for VM compiler |
| Bytecode Virtual Machine (VM) | **PLANNED** (Phase 7) | Stack-based VM |
| C99 Code Generator / Native AOT | **PLANNED** (Phase 9) | Native compiler |
| Visual Block ↔ AST Bridge | **PLANNED** (Phase 5) | Block editor |

---

## 2. Decoupled Pipeline Design

Unfish maintains strict boundary decoupling between stages:

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐     ┌──────────────────┐
│ Source Text  │ ──► │ Lexer        │ ──► │ Parser       │ ──► │ Semantic Checker │
└──────────────┘     └──────────────┘     └──────────────┘     └──────────────────┘
                                                                        │
                                                                        ▼
                                                               ┌──────────────────┐
                                                               │ Strongly Typed   │
                                                               │ AST Representation│
                                                               └──────────────────┘
                                                                        │
                                            ┌───────────────────────────┴───────────────────────────┐
                                            ▼                                                       ▼
                                   ┌──────────────────┐                                   ┌──────────────────┐
                                   │ AST Interpreter  │                                   │ Bytecode VM      │
                                   │ [IMPLEMENTED]    │                                   │ [PLANNED]        │
                                   └──────────────────┘                                   └──────────────────┘
```

### Architectural Guarantees:
1. **Zero External Dependencies**: Implemented in clean ANSI C99.
2. **Deterministic Dual Execution Target**: When the Bytecode VM is built, both the AST interpreter and VM will execute identical programs with identical observable output, verified by the conformance suite.
3. **Memory Safety**: Clean execution under AddressSanitizer and UndefinedBehaviorSanitizer with zero memory leaks.
