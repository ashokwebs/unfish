# UNFISH — COMPILER ARCHITECTURE & ROADMAP

---

## 1. Principles: No Fake Compilers

In accordance with Unfish principles:
* We will **never** build a pseudo-compiler that merely wraps the interpreter in a shell script or renames AST nodes as instructions.
* We establish language semantics first via the AST and interpreter.
* We then build genuine intermediate representations (IR), bytecode compilation, and native code generation.

## 2. Compilation Stages (Phase 9 Roadmap)

```
Source (.unfish)
      │
      ▼
Lexer & Parser
      │
      ▼
Typed AST
      │
      ▼
Semantic Analysis (Type Checking, Scope Resolution)
      │
      ▼
High-Level IR (Control Flow Graph & Basic Blocks)
      │
      ▼
Optimization Passes (Constant Folding, Dead Code Elimination)
      │
      ▼
Low-Level Code Generator
      ├── Option A: Bytecode for Unfish VM
      ├── Option B: Standalone C99 Emission (portable native binaries)
      └── Option C: LLVM IR / Native x86_64/ARM64 Machine Code
```

## 3. Educational Code Generation (C99 Backend)
As an educational milestone, Unfish will support `unfish emit-c program.unfish`, emitting human-readable, idiomatic C99 code. This will serve as a bridge teaching students how high-level constructs (functions, closures, dynamically allocated strings) map directly into C data structures, pointers, and function calls.
