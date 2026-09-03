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

## ADR 010: Control Flow Primitives: break, continue, for-in Loops, and range()
* **Date**: Milestone 3
* **Status**: Accepted
* **Context**: Algorithmic code requires fine-grained loop control (early loop termination, skipping iterations) and high-level collection traversal without manual index bookkeeping.
* **Decision**:
  1. Add `break` and `continue` keywords and statement AST nodes (`UF_STMT_BREAK`, `UF_STMT_CONTINUE`).
  2. Enforce compile-time semantic validation: using `break` or `continue` outside of a loop produces a semantic error.
  3. Add `for <item> in <iterable>:` syntax (`UF_STMT_FOR`), supporting native traversal over arrays and UTF-8 strings.
  4. Implement `range([start,] end[, step])` built-in native function returning an array of numbers.
* **Consequences**: Unifies counting loops, range iterations, and collection traversals under a consistent, beginner-friendly yet expressive syntax.

## ADR 011: Language-Level Error Handling Architecture: Gradual Two-Tier Model
* **Date**: Milestone 3
* **Status**: Accepted
* **Context**: Unfish needs an error handling model suited for learners transitioning from beginner scripts to systems programming. Exceptions often cause hidden control-flow jumps, while strict Result types can introduce excessive syntactic overhead for introductory programming.
* **Decision**:
  1. **Tier 1 (Value-based failure)**: Standard library query functions return `null` or boolean indicators for expected conditions (e.g. missing map key, EOF), encouraging explicit checks.
  2. **Tier 2 (Structured catchable runtime errors)**: Exceptional situations (e.g. `IndexOutOfBounds`, `DivisionByZero`, `StackOverflowError`, `AssertionFailed`) throw structured runtime errors that can be captured via `try: ... catch err:` blocks.
  3. Custom user errors are raised via `panic(msg)` or `error(msg)`.
* **Consequences**: Novices write clean, sequential code without boilerplate; educational test harnesses can evaluate student code defensively without crashing the test runner; advanced learners learn stack unwinding.

## ADR 012: Module Resolution, Namespaces, and Singleton Lifecycle
* **Date**: Milestone 3
* **Status**: Accepted
* **Context**: Unfish programs must scale beyond single files while avoiding namespace pollution and cyclic initialization deadlocks.
* **Decision**:
  1. **Syntax**: `import <module>` binds module exports into a dedicated namespace object `<module>`. Selective imports `from <module> import a, b` copy exported symbols into the current scope.
  2. **Resolution Order**: (1) Built-in core modules (`math`, `sys`, `fs`), (2) File-relative path `./<module>.unfish`, (3) Project module path `UNFISH_PATH`.
  3. **Lifecycle**: Modules are executed once as singletons and cached in `rt->modules`. Circular dependencies transition through states (`UNLOADED` -> `LOADING` -> `LOADED`) and report compile-time `CyclicImportError` if mutually dependent.
* **Consequences**: Clean separation of namespaces, deterministic single-evaluation semantics, and zero global scope pollution.

## ADR 013: Bytecode Virtual Machine Pipeline & Intermediate Representation (IR) Contract
* **Date**: Milestone 3
* **Status**: Accepted
* **Context**: The tree-walking interpreter serves as the semantic reference. To achieve high performance, serialization, and compilation to web/native, Unfish requires a bytecode VM pipeline (`AST -> IR -> Bytecode -> VM`).
* **Decision**:
  1. Maintain identical semantic contracts between the tree-walking interpreter and the future VM; both must pass the exact same conformance suite.
  2. Define a stack-based instruction set architecture with 1-byte opcodes and 2-byte operand indices.
  3. Opcodes are partitioned into: Literal loading, Local/Global variable access, Closure/Upvalue capture, Binary/Unary operations, Conditional/Unconditional jumps, Collection construction/indexing, and Function call/return.
  4. Bytecode chunks bundle bytecode arrays, constant pools (`UfValue`), and debug line-mapping tables.
* **Consequences**: Establishes a concrete contract for Phase 7 VM implementation without breaking the current AST interpreter.

## ADR 014: Hash Map Data Structure, Dot Property Access, and Iteration
* **Date**: Milestone 4 (Phase 3 Part 2)
* **Status**: Accepted
* **Context**: Algorithmic and general-purpose programming requires first-class associative key-value dictionaries. Additionally, ergonomic record manipulation benefits from property-style dot syntax (`user.name`).
* **Decision**:
  1. **Data Structure**: Implement `UfMapObject` (`UF_OBJ_MAP` / `UF_VAL_MAP`) as an open-addressing hash table with linear probing and power-of-two capacities, resizing at 75% load factor.
  2. **Hashing**: Use the 32-bit FNV-1a hash algorithm for strings and bitwise IEEE 754 float hashing for numbers.
  3. **Order Preservation**: Maintain a parallel `order_keys` array in `UfMapObject` to guarantee deterministic insertion-order iteration for `keys()`, `values()`, and `for-in` traversal.
  4. **Syntax**: Support `{key: value}` literals with both string expressions and identifier keys (`{name: "Alice"}`).
  5. **Dot Property Sugar**: In the Pratt parser, desugar `target.field` into `target["field"]` (`UF_EXPR_INDEX`) and `target.field = val` into `target["field"] = val` (`UF_STMT_INDEX_ASSIGN`).
  6. **Error Semantics**: Adhere to Tier 1 of the Error Model (ADR 011) — querying a nonexistent key returns `null` rather than throwing an unhandled runtime error. Provide `has_key(map, key)` for explicit containment checks and `delete(map, key)` for deletion.
  7. **Garbage Collection**: Integrate `UF_OBJ_MAP` directly into mark-and-sweep GC by traversing both `entries` (keys and values) and `order_keys`.
* **Consequences**: Unifies object-like records and dictionaries under a single high-performance, memory-safe data structure with zero external dependencies, fully verified under ASan/UBSan.

## ADR 015: Higher-Order Functions, Anonymous Lambdas, and Runtime Invocation Protocol
* **Date**: Milestone 5 (Phase 3 Part 3)
* **Status**: Accepted
* **Context**: Functional programming idioms (`map`, `filter`, `reduce`, `sort`) require first-class functions that can be constructed inline as anonymous lambdas and passed to native standard library procedures.
* **Decision**:
  1. **Runtime Invocability**: Export a decoupled callback interface `uf_runtime_call` via `UfCallValueFn call_fn` in `UfRuntime`, enabling C native functions to invoke user-defined closures and native functions uniformly.
  2. **Core Functional Built-ins**: Implement 8 native functions operating on arrays:
     - `map(arr, fn)`: transforms every element.
     - `filter(arr, fn)`: retains truthy predicate matches.
     - `reduce(arr, fn, [init])`: left fold with optional accumulator initialization.
     - `sort(arr, [cmp])`: stable sorting with default or custom binary comparator.
     - `reverse(arr)`: reversed array shallow copy.
     - `find(arr, fn)`: first predicate match or `null`.
     - `every(arr, fn)`: universal quantifier predicate check.
     - `some(arr, fn)`: existential quantifier predicate check.
  3. **Anonymous Function Syntax**: Support `function([params]): body` in expression positions.
     - Support inline expression bodies with implicit return: `function(x): x * 2`.
     - Support inline statements with explicit return: `function(x): return x * 2`.
     - Support full multiline indented blocks: `function(x):\n    let y = x + 1\n    return y`.
  4. **Self-Recursive Named Function Expressions**: Named function expressions (`let fact = function factorial(n): ...`) bind the function name inside its own call scope, permitting self-recursion without polluting the outer namespace.
* **Consequences**: Enables clean, modern data pipeline construction and functional algorithms with zero external dependencies, 100% verified under ASan/UBSan.
