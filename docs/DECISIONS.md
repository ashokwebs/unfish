# UNFISH — ARCHITECTURAL DECISION RECORDS (ADRs)

---

## ADR 001: Core Implementation in ANSI C99/C11
* **Date**: Phase 0
* **Status**: Accepted
* **Context**: We need an implementation language that is fast, portable, minimal in dependencies, and educational for students learning systems programming.
* **Decision**: Implement the core lexer, parser, runtime, and interpreter in clean, warning-free ANSI C99/C11 with zero external libraries.
* **Consequences**: Complete control over memory layout and runtime data structures; enables students to inspect language internals; requires rigorous automated sanitizer testing (ASan/UBSan) to prevent memory bugs.

## ADR 002: Indentation-Based Block Delimitation (Off-Side Rule)
* **Date**: Phase 0
* **Status**: Accepted
* **Context**: Block programming representations map naturally to hierarchical nested containers. In text, noisy curly braces `{}` add cognitive load for beginners.
* **Decision**: Use indentation (`INDENT`, `DEDENT`, `NEWLINE`) with colon `:` block introductions.
* **Consequences**: Syntax is clean and readable; visual blocks map directly to indented AST blocks; lexer handles indentation tracking using a dedicated indentation stack.

## ADR 003: Pratt Parser for Expressions
* **Date**: Phase 0
* **Status**: Accepted
* **Context**: Expression parsing with pure recursive descent leads to deeply nested grammar functions and difficulty extending operators.
* **Decision**: Implement Vaughan Pratt's top-down operator precedence parser for all expressions.
* **Consequences**: Highly compact, table-driven expression parsing; precise operator precedence and associativity; easily extensible.

## ADR 004: Chunk Arena Allocator for AST
* **Date**: Phase 0
* **Status**: Accepted
* **Context**: AST node creation with individual `malloc` calls causes fragmentation, overhead, and complex manual traversal during cleanup.
* **Decision**: Allocate all AST nodes, token strings, and parser structures in a contiguous chunk arena.
* **Consequences**: Extremely fast allocation; O(1) bulk deallocation at the end of compilation/execution; eliminates AST memory leaks by design.

## ADR 005: Tagged Union Runtime Values
* **Date**: Phase 0
* **Status**: Accepted
* **Context**: We need a simple, type-safe runtime value representation that bridges dynamically typed evaluation and future low-level systems inspection.
* **Decision**: Represent all values with a 16-byte tagged union (`UfValue`).
* **Consequences**: Primitives (null, bool, number) fit directly on the stack without heap allocation; strings, functions, and objects are tracked via pointers to heap headers.

## ADR 006: Object-Tracked Mark-and-Sweep Garbage Collection
* **Date**: Phase 2
* **Status**: Accepted
* **Context**: Manual reference counting between first-class functions (closures) and their lexical environments creates circular references (`closure_env <-> function_binding`), causing memory leaks upon runtime shutdown.
* **Decision**: Introduce a `UfObj` header (`src/runtime/uf_object.h`) on all heap objects (`UfStringObject`, `UfFunctionObject`, `UfEnv`) and manage them via an object registry in `UfRuntime`. Implement mark-and-sweep tracing rooted at active call frames and the global environment.
* **Consequences**: Perfectly resolves circular reference cycles; ensures deterministic, leak-free shutdown verified by AddressSanitizer; exposes a real garbage collection implementation for systems-level inspection.

## ADR 007: REPL Persistent Session Arena
* **Date**: Phase 2
* **Status**: Accepted
* **Context**: Resetting the compilation arena after each REPL line caused interned identifier strings and function bodies defined in prior lines to be overwritten by subsequent allocations.
* **Decision**: Maintain a persistent arena across the lifetime of the interactive REPL session, freeing all allocations only when the user exits.
* **Consequences**: Variables, functions, and closures remain valid and callable throughout the interactive session without memory corruption.

## ADR 008: Temporary Root Protection Stack & Active Block Scope Rooting
* **Date**: Engineering Review
* **Status**: Accepted
* **Context**: Under high GC pressure (e.g. loops allocating many objects with small thresholds), temporary values evaluated during complex expressions and local block scope environments (`UfEnv` created in `while`/`if`/`repeat` bodies) were not discovered by the garbage collector because they were neither in global scope nor registered in function activation frames. This led to heap-use-after-free when GC triggered during subexpression evaluation.
* **Decision**: 
  1. Add an evaluation operand root stack `rt->temp_roots` in `UfRuntime` with `uf_runtime_push_temp_root` and `uf_runtime_pop_temp_roots`.
  2. Track the currently active environment frame `rt->current_env` during statement/block execution, restoring it with RAII-like discipline across blocks and calls.
  3. Traverse `rt->current_env` and its parent chain during GC root marking.
* **Consequences**: Completely eliminated use-after-free under heavy GC pressure (verified by ASan in `test_stress`), guaranteeing safety for all arbitrary expression trees and nested block loops.

## ADR 009: First-Class Dynamic Arrays and In-Place Index Assignment
* **Date**: Milestone 2 (Phase 3)
* **Status**: Accepted
* **Context**: General-purpose algorithmic programming (sorting, data structures, lookup buffers) requires first-class sequential collections and subscript access.
* **Decision**: 
  1. Add `UF_TOK_LBRACKET` (`[`) and `UF_TOK_RBRACKET` (`]`) with paren-depth tracking in the lexer.
  2. Implement prefix `[` for array literals and infix `[` with `PREC_CALL` for subscripting in the Pratt parser.
  3. Introduce `UfArrayObject` (`UF_OBJ_ARRAY` / `UF_VAL_ARRAY`) containing dynamically resizable `elements` buffers tracked by GC.
  4. Support Python-style negative indexing (`arr[-1]` accesses the last element).
  5. Introduce `UF_STMT_INDEX_ASSIGN` allowing in-place assignment (`arr[i] = val`).
  6. Extend `len()`, `push()`, and `pop()` built-ins.
* **Consequences**: Enables implementing standard algorithms (e.g. `bubble_sort`, search) natively in Unfish, fully integrated with garbage collection and memory safety under ASan.
