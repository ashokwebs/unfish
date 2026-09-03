# UNFISH — PROJECT STATE INVENTORY

**Last Updated:** Milestone 4 Complete (Key-Value Hash Maps, Dot Property Access, Map Iteration, ADR 014)  
**Project Health:** Healthy, 100% Tests Passing under ASan / UBSan  

---

## 1. Inventory by Status

### COMPLETE
* **Formal Language Specification & Grammar**: EBNF, lexical rules, off-side indentation, operator precedence table (`docs/LANGUAGE_SPEC.md`).
* **Complete Documentation Architecture**: 23 living technical documents in `docs/` covering vision, architecture, memory model, type system, error model, runtime, VM roadmap, compiler roadmap, blocks, tooling, testing, and security.
* **Architectural Decision Records (ADRs)**: ADR 001 through ADR 014 documented in `docs/DECISIONS.md`.
* **Lexer**: UTF-8 scanner emitting typed tokens (`UfToken`) with source spans, indentation stack (`INDENT`, `DEDENT`, `NEWLINE`), string escapes, number scanning, brackets (`[`, `]`), braces (`{`, `}`), dot (`.`), keywords (`let`, `say`, `function`, `return`, `if`, `else`, `while`, `repeat`, `times`, `for`, `in`, `break`, `continue`, `and`, `or`, `not`, `true`, `false`, `null`), and diagnostics.
* **AST Data Structures**: Strongly typed AST nodes with source span preservation (`UfExpr`, `UfStmt`, `UfProgram`).
* **AST Pretty Printer**: S-expression tree printer (`uf_ast_print`) supporting all statements, expressions, array literals, and map literals.
* **Parser**: Pratt parser for binary, unary, call, array literals, map literals (`{key: value}`), dot access (`obj.field`), and postfix indexing expressions; recursive descent for statements, assignments (`x = v`, `arr[i] = v`, `obj.field = v`), blocks, and loops (`while`, `repeat`, `for-in`, `break`, `continue`).
* **Semantic Analysis**: Lexical scope analysis, symbol tables, undefined identifier detection with Levenshtein distance suggestions, duplicate declaration prevention, return-outside-function checks, loop-depth checks, arity checks, and top-level function hoisting.
* **Diagnostics Engine**: Colored terminal diagnostics with source code snippets, line numbers, and column-accurate carets (`^~~~~`).
* **Runtime & Value Representation**: Tagged union `UfValue` (`Null`, `Boolean`, `Number`, `String`, `Array`, `Map`, `Function`, `NativeFunction`), deep structural equality for arrays and maps, string conversion, truthiness, and call stack backtraces.
* **First-Class Dynamic Arrays**: Heap-allocated `UfArrayObject` with dynamic resizing, negative index support (`arr[-1]`), string subscripting (`s[0]`), in-place mutation (`arr[i] = v`), `len()`, `push()`, and `pop()`.
* **First-Class Hash Maps**: Open-addressing hash table with linear probing, FNV-1a hashing, deterministic insertion-order preservation, deep structural equality, dot property syntax sugar (`user.name`), `len()`, `keys()`, `values()`, `has_key()`, and `delete()`.
* **High-Level Iteration & Control Flow**: `for <item> in <iterable>:` native traversal over arrays, maps (keys), and strings; `break` and `continue` with semantic loop validation; and `range([start,] end[, step])` generator.
* **Lexical Environments**: Linked scope frames (`UfEnv`) supporting variable declaration, lookup, and mutation.
* **Garbage Collection**: Object-tracked mark-and-sweep GC (`UfObj`) supporting circular closure/env references, active block scoping (`current_env`), evaluation temporary roots (`temp_roots`), maps with closures as values, and leak-free teardown under ASan/UBSan.
* **Tree-Walking Interpreter**: AST evaluator supporting expressions, statements, closures, recursion, loops with early breaks/continues, step quota guards, recursion stack overflow traps, and `uf_call_value` interface.
* **Standard Library Core**: Built-in native functions `say`, `print`, `type_of`, `len`, `push`, `pop`, `range`, `keys`, `values`, `has_key`, `delete`, `clock`, `assert`.
* **CLI (`unfish`)**: Full-featured CLI with commands `run`, `check`, `ast`, `tokens`, `repl`, `version`.
* **Interactive REPL**: Multiline block entry, persistent session arena, direct expression evaluation.
* **Verification Harness**: 4 unit test binaries in C, comprehensive stress test suite (`test_stress`), automated conformance test runner (`tools/run_conformance_tests.sh`) with 27 test suites passing under ASan/UBSan.

### PARTIAL
* **Standard Library Modules**: Core built-ins implemented; external modules (`math`, `strings`, `fs`, `sys`) designed (ADR 012).

### BROKEN
* *None.* (All 4 unit test suites, 10 stress tests, and 27 conformance tests pass with 0 errors).

### MISSING
* **Higher-Order Functions & Lambdas** (Phase 3 Part 3: map, filter, reduce, anonymous functions).
* **Block ↔ AST Round-Tripping**: Visual block translation (Phase 5).
* **Interactive Debugger**: Breakpoint and step debugging hooks (Phase 6).
* **Bytecode VM**: Stack-based virtual machine (Phase 7, ISA designed in ADR 013).
* **AOT Compiler**: C code emission and native compiler (Phase 9).

### EXPERIMENTAL
* *None.*

---

## 2. Milestone Checkpoint (Section 51)

* **Current Milestone:** Milestone 4 Complete (Hash Maps, Dot Property Access, Map Iteration, and ADR 014)
* **Status:** VERIFIED & COMPLETE
* **Implemented:**
  - `UF_TOK_LBRACE`, `UF_TOK_RBRACE`, `UF_TOK_DOT` tokens in lexer.
  - `{key: value}` map literals and `target.prop` dot access in Pratt parser.
  - `UfMapObject` runtime object with FNV-1a hash table and linear probing.
  - Deterministic insertion-order preservation for `keys()`, `values()`, and `for-in` traversal.
  - Deep structural equality for nested maps and arrays.
  - Native built-ins `keys()`, `values()`, `has_key()`, `delete()`, and extended `len()`.
  - Mark-and-sweep GC traversal and sweep for `UF_OBJ_MAP`.
  - Conformance test suite expanded to 27 tests (`15_maps`, `16_map_iteration`, `17_dot_access`, `err_map_invalid_key_type`).
  - Stress test suite expanded to 10 tests (`test_map_gc_stress` with closures stored in maps under live GC collections).
  - Universal `uf_call_value` exported in interpreter.
  - Architectural decision ADR 014 recorded in `docs/DECISIONS.md`.
* **Verified:**
  - Unit tests: `test_lexer`, `test_parser`, `test_semantic`, `test_interpreter`.
  - Stress tests: `test_stress` (10 tests including aggressive GC pressure).
  - Conformance test suite: 27 test cases.
  - 100% clean execution under AddressSanitizer and UndefinedBehaviorSanitizer with zero memory leaks.
* **Next Recommended Task:**
  - Section D & E: Higher-Order Functions (`map`, `filter`, `reduce`, `sort`, etc.) & Anonymous Functions.
