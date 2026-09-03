# UNFISH — PROJECT STATE INVENTORY

**Last Updated:** Milestone 3 Complete (Control Flow: break, continue, for-in, range, Architectural ADRs 010-013)  
**Project Health:** Healthy, 100% Tests Passing under ASan / UBSan  

---

## 1. Inventory by Status

### COMPLETE
* **Formal Language Specification & Grammar**: EBNF, lexical rules, off-side indentation, operator precedence table (`docs/LANGUAGE_SPEC.md`).
* **Complete Documentation Architecture**: 22 living technical documents in `docs/` covering vision, architecture, memory model, type system, error model, runtime, VM roadmap, compiler roadmap, blocks, tooling, testing, and security.
* **Architectural Decision Records (ADRs)**: ADR 001 through ADR 013 documented in `docs/DECISIONS.md`.
* **Lexer**: UTF-8 scanner emitting typed tokens (`UfToken`) with source spans, indentation stack (`INDENT`, `DEDENT`, `NEWLINE`), string escapes, number scanning, brackets (`[`, `]`), keywords (`let`, `say`, `function`, `return`, `if`, `else`, `while`, `repeat`, `times`, `for`, `in`, `break`, `continue`, `and`, `or`, `not`, `true`, `false`, `null`), and diagnostics.
* **AST Data Structures**: Strongly typed AST nodes with source span preservation (`UfExpr`, `UfStmt`, `UfProgram`).
* **AST Pretty Printer**: S-expression tree printer (`uf_ast_print`) supporting all statements and expressions.
* **Parser**: Pratt parser for binary, unary, call, array literals, and postfix indexing expressions; recursive descent for statements, assignments (`x = v`, `arr[i] = v`), blocks, and loops (`while`, `repeat`, `for-in`, `break`, `continue`).
* **Semantic Analysis**: Lexical scope analysis, symbol tables, undefined identifier detection with Levenshtein distance "Did you mean?" suggestions, duplicate declaration prevention, return-outside-function checks, loop-depth checks (preventing `break`/`continue` outside loops), arity checks, and top-level function hoisting.
* **Diagnostics Engine**: Colored terminal diagnostics with source code snippets, line numbers, and column-accurate carets (`^~~~~`).
* **Runtime & Value Representation**: Tagged union `UfValue` (`Null`, `Boolean`, `Number`, `String`, `Array`, `Function`, `NativeFunction`), structural array equality, string conversion, truthiness, and call stack backtraces.
* **First-Class Dynamic Arrays**: Heap-allocated `UfArrayObject` with dynamic resizing, negative index support (`arr[-1]`), string subscripting (`s[0]`), in-place mutation (`arr[i] = v`), `len()`, `push()`, and `pop()`.
* **High-Level Iteration & Control Flow**: `for <item> in <iterable>:` native traversal over arrays and strings, `break` and `continue` with semantic loop validation, and `range([start,] end[, step])` generator.
* **Lexical Environments**: Linked scope frames (`UfEnv`) supporting variable declaration, lookup, and mutation.
* **Garbage Collection**: Object-tracked mark-and-sweep GC (`UfObj`) supporting circular closure/env references, active block scoping (`current_env`), evaluation temporary roots (`temp_roots`), arrays of closures, closures capturing arrays, and leak-free teardown under ASan/UBSan.
* **Tree-Walking Interpreter**: AST evaluator supporting expressions, statements, closures, recursion, loops with early breaks/continues, step quota guards, and recursion stack overflow traps.
* **Standard Library Core**: Built-in native functions `say`, `print`, `type_of`, `len`, `push`, `pop`, `range`, `clock`, `assert`.
* **CLI (`unfish`)**: Full-featured CLI with commands `run`, `check`, `ast`, `tokens`, `repl`, `version`.
* **Interactive REPL**: Multiline block entry, persistent session arena, direct expression evaluation.
* **Verification Harness**: 4 unit test binaries in C, comprehensive stress test suite (`test_stress`), automated conformance test runner (`tools/run_conformance_tests.sh`) with 23 test suites passing under ASan/UBSan.

### PARTIAL
* **Standard Library Modules**: Core built-ins implemented; external modules (`math`, `strings`, `fs`, `sys`) designed (ADR 012).

### BROKEN
* *None.* (All 4 unit test suites, 9 stress tests, and 23 conformance tests pass with 0 errors).

### MISSING
* **Key-Value Maps / Dictionaries** (Phase 3 Part 2).
* **Block ↔ AST Round-Tripping**: Visual block translation (Phase 5).
* **Interactive Debugger**: Breakpoint and step debugging hooks (Phase 6).
* **Bytecode VM**: Stack-based virtual machine (Phase 7, ISA designed in ADR 013).
* **AOT Compiler**: C code emission and native compiler (Phase 9).

### EXPERIMENTAL
* *None.*

---

## 2. Milestone Checkpoint (Section 51)

* **Current Milestone:** Milestone 3 Complete (Control Flow: break, continue, for-in loops, range, and Architectural ADRs)
* **Status:** VERIFIED & COMPLETE
* **Implemented:**
  - `break` and `continue` keywords, AST nodes, semantic validation, and interpreter support.
  - `for <item> in <iterable>:` statement traversing dynamic arrays and UTF-8 strings.
  - Built-in `range([start,] end[, step])` native generator.
  - GC stress test verifying closures capturing arrays and arrays containing closures undergoing live GC collections.
  - Conformance test suite expanded to 23 tests (including positive and negative loop tests).
  - Architectural specifications: Gradual Two-Tier Error Model (ADR 011), Module Resolution & Singleton Lifecycle (ADR 012), Bytecode VM ISA & IR Contract (ADR 013).
* **Verified:**
  - Unit tests: `test_lexer`, `test_parser`, `test_semantic`, `test_interpreter`.
  - Stress tests: `test_stress` (deep closures, mutual recursion, shadowing, GC pressure with live collections, nested array-closure graphs, 60-nested parens, 512-frame stack overflow guard, execution quota guard, 13 fuzz inputs).
  - Conformance test suite: 23 test cases.
  - 100% clean execution under AddressSanitizer and UndefinedBehaviorSanitizer with zero memory leaks.
* **Partially Implemented:**
  - Key-value maps (scheduled next).
* **Known Bugs:**
  - None.
* **Known Limitations:**
  - Maps (`{}`) scheduled next.
  - Execution engine is currently tree-walking; bytecode VM scheduled for Phase 7.
* **Architectural Decisions:**
  - ADR 001 through ADR 013 accepted and documented.
* **Tests:**
  - 5 C test binaries (all passing).
  - 23 automated conformance tests (all passing).
* **Next Recommended Task:**
  - Phase 3 Part 2: Key-Value Hash Maps (`{ "key": value }`).
