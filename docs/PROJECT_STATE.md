# UNFISH — PROJECT STATE INVENTORY

**Last Updated:** Milestone 5 Complete (Higher-Order Functions, Anonymous Lambdas, Data Pipelines, ADR 015)  
**Project Health:** Healthy, 100% Tests Passing under ASan / UBSan  

---

## 1. Inventory by Status

### COMPLETE
* **Formal Language Specification & Grammar**: EBNF, lexical rules, off-side indentation, operator precedence table (`docs/LANGUAGE_SPEC.md`).
* **Complete Documentation Architecture**: 23 living technical documents in `docs/` covering vision, architecture, memory model, type system, error model, runtime, VM roadmap, compiler roadmap, blocks, tooling, testing, and security.
* **Architectural Decision Records (ADRs)**: ADR 001 through ADR 015 documented in `docs/DECISIONS.md`.
* **Lexer**: UTF-8 scanner emitting typed tokens (`UfToken`) with source spans, indentation stack (`INDENT`, `DEDENT`, `NEWLINE`), string escapes, number scanning, brackets (`[`, `]`), braces (`{`, `}`), dot (`.`), keywords (`let`, `say`, `function`, `return`, `if`, `else`, `while`, `repeat`, `times`, `for`, `in`, `break`, `continue`, `and`, `or`, `not`, `true`, `false`, `null`), and diagnostics.
* **AST Data Structures**: Strongly typed AST nodes with source span preservation (`UfExpr`, `UfStmt`, `UfProgram`).
* **AST Pretty Printer**: S-expression tree printer (`uf_ast_print`) supporting all statements, expressions, array literals, map literals, and anonymous function expressions.
* **Parser**: Pratt parser for binary, unary, call, array literals, map literals (`{key: value}`), dot access (`obj.field`), anonymous functions / lambdas (`function(x): x * 2`), and postfix indexing expressions; recursive descent for statements, assignments, blocks, and loops.
* **Semantic Analysis**: Lexical scope analysis, symbol tables, undefined identifier detection with Levenshtein distance suggestions, duplicate declaration prevention, return-outside-function checks, loop-depth checks, arity checks, anonymous function scope analysis, and top-level function hoisting.
* **Diagnostics Engine**: Colored terminal diagnostics with source code snippets, line numbers, and column-accurate carets (`^~~~~`).
* **Runtime & Value Representation**: Tagged union `UfValue` (`Null`, `Boolean`, `Number`, `String`, `Array`, `Map`, `Function`, `NativeFunction`), deep structural equality for arrays and maps, string conversion, truthiness, and call stack backtraces.
* **First-Class Dynamic Arrays**: Heap-allocated `UfArrayObject` with dynamic resizing, negative index support (`arr[-1]`), string subscripting (`s[0]`), in-place mutation (`arr[i] = v`), `len()`, `push()`, and `pop()`.
* **First-Class Hash Maps**: Open-addressing hash table with linear probing, FNV-1a hashing, deterministic insertion-order preservation, deep structural equality, dot property syntax sugar (`user.name`), `len()`, `keys()`, `values()`, `has_key()`, and `delete()`.
* **First-Class Functions & Lambdas**: Full lexical closure support, anonymous function expressions (`function(x): x * 2`), inline expression bodies with implicit return, multiline closures, immediately invoked function expressions (IIFE), and self-recursive named function expressions.
* **Higher-Order Functions**: Full suite of native functional primitives (`map`, `filter`, `reduce`, `sort` with custom comparators, `reverse`, `find`, `every`, `some`).
* **High-Level Iteration & Control Flow**: `for <item> in <iterable>:` native traversal over arrays, maps (keys), and strings; `break` and `continue` with semantic loop validation; and `range([start,] end[, step])` generator.
* **Lexical Environments**: Linked scope frames (`UfEnv`) supporting variable declaration, lookup, and mutation.
* **Garbage Collection**: Object-tracked mark-and-sweep GC (`UfObj`) supporting circular closure/env references, active block scoping (`current_env`), evaluation temporary roots (`temp_roots`), live GC sweeps during functional pipelines, and leak-free teardown under ASan/UBSan.
* **Tree-Walking Interpreter**: AST evaluator supporting expressions, statements, closures, recursion, loops with early breaks/continues, step quota guards, recursion stack overflow traps, and universal `uf_runtime_call` interface.
* **Standard Library Core**: Built-in native functions `say`, `print`, `type_of`, `len`, `push`, `pop`, `range`, `keys`, `values`, `has_key`, `delete`, `map`, `filter`, `reduce`, `sort`, `reverse`, `find`, `every`, `some`, `clock`, `assert`.
* **CLI (`unfish`)**: Full-featured CLI with commands `run`, `check`, `ast`, `tokens`, `repl`, `version`.
* **Interactive REPL**: Multiline block entry, persistent session arena, direct expression evaluation.
* **Verification Harness**: 4 unit test binaries in C, comprehensive stress test suite (`test_stress`), automated conformance test runner (`tools/run_conformance_tests.sh`) with 30 test suites passing under ASan/UBSan.

### PARTIAL
* **Standard Library Modules**: Core built-ins implemented; external modules (`math`, `strings`, `fs`, `sys`) designed (ADR 012).

### BROKEN
* *None.* (All 4 unit test suites, 10 stress tests, and 30 conformance tests pass with 0 errors).

### MISSING
* **String Standard Library** (Section F: split, join, trim, replace, etc.).
* **Math Standard Library** (Section G).
* **Block ↔ AST Round-Tripping**: Visual block translation (Phase 5).
* **Interactive Debugger**: Breakpoint and step debugging hooks (Phase 6).
* **Bytecode VM**: Stack-based virtual machine (Phase 7, ISA designed in ADR 013).
* **AOT Compiler**: C code emission and native compiler (Phase 9).

### EXPERIMENTAL
* *None.*

---

## 2. Milestone Checkpoint (Section 51)

* **Current Milestone:** Milestone 5 Complete (Higher-Order Functions, Anonymous Lambdas, Data Pipelines, and ADR 015)
* **Status:** VERIFIED & COMPLETE
* **Implemented:**
  - `UF_EXPR_FUNCTION` AST node with parameter list, optional name, and body statement/block.
  - Pratt parser prefix rule for `function` parsing inline expressions (`function(x): x * 2`), inline return (`function(x): return x * 2`), and multiline indented closures.
  - Self-recursive named function expressions binding name in execution scope.
  - Universal decoupled runtime callback protocol (`uf_runtime_call` via `UfCallValueFn`).
  - Native higher-order built-ins: `map`, `filter`, `reduce`, `sort` (with custom comparator support), `reverse`, `find`, `every`, `some`.
  - Conformance test suite expanded to 30 tests (`18_higher_order`, `19_sort_and_search`, `20_anonymous_functions`).
  - Example functional data pipeline (`examples/functional.unfish`).
  - Architectural decision ADR 015 recorded in `docs/DECISIONS.md`.
* **Verified:**
  - Unit tests: `test_lexer`, `test_parser`, `test_semantic`, `test_interpreter`.
  - Stress tests: `test_stress` (10 tests including aggressive GC pressure).
  - Conformance test suite: 30 test cases.
  - 100% clean execution under AddressSanitizer and UndefinedBehaviorSanitizer with zero memory leaks.
* **Next Recommended Task:**
  - Section F: String Standard Library (`split`, `join`, `trim`, `replace`, `to_upper`, `to_lower`, `contains`, etc.).
