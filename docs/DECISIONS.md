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
