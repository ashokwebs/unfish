# UNFISH — PROJECT STATE INVENTORY

**Last Updated:** Milestone 2 Complete (Phase 1, 2, and 3 Collections)  
**Project Health:** Healthy, 100% Tests Passing under ASan / UBSan  

---

## 1. Inventory by Status

### COMPLETE
* **Formal Language Specification & Grammar**: EBNF, lexical rules, off-side indentation, operator precedence table (`docs/LANGUAGE_SPEC.md`).
* **Complete Documentation Architecture**: 22 living technical documents in `docs/` covering vision, architecture, memory model, type system, error model, runtime, VM roadmap, compiler roadmap, blocks, tooling, testing, and security.
* **Lexer**: UTF-8 scanner emitting typed tokens (`UfToken`) with full source locations (`SourceLoc`, `SourceSpan`), indentation stack (`INDENT`, `DEDENT`, `NEWLINE`), string escapes (`\n`, `\t`, `\"`, `\\`), number scanning, comments, brackets (`[`, `]`), and diagnostics.
* **AST Data Structures**: Strongly typed AST nodes for expressions, statements, and blocks with source spans (`UfExpr`, `UfStmt`, `UfProgram`).
* **AST Pretty Printer**: S-expression tree printer (`uf_ast_print`) for inspection and debugging (`unfish ast`).
* **Parser**: Pratt parser for binary, unary, call, array literals, and postfix indexing expressions; recursive descent for statements, assignments (`x = v`, `arr[i] = v`), and indentation-delimited blocks.
* **Semantic Analysis**: Lexical scope analysis, symbol tables, undefined identifier detection with Levenshtein distance "Did you mean?" suggestions, duplicate declaration prevention, return-outside-function checks, arity checks, and top-level function hoisting.
* **Diagnostics Engine**: Colored terminal diagnostics with source code snippets, line numbers, and column-accurate carets (`^~~~~`).
* **Runtime & Value Representation**: Tagged union `UfValue` (`Null`, `Boolean`, `Number`, `String`, `Function`, `NativeFunction`, `Array`), structural array equality, string conversion, truthiness, and call stack backtraces.
* **First-Class Dynamic Arrays**: Heap-allocated `UfArrayObject` with dynamic resizing, negative index support (`arr[-1]`), string subscripting (`s[0]`), in-place mutation (`arr[i] = v`), `len()`, `push()`, and `pop()`.
* **Lexical Environments**: Linked scope frames (`UfEnv`) supporting variable declaration, lookup, and mutation.
* **Garbage Collection**: Object-tracked mark-and-sweep GC (`UfObj`) supporting circular closure/env references, active block scoping (`current_env`), evaluation temporary roots (`temp_roots`), and leak-free teardown.
* **Tree-Walking Interpreter**: AST evaluator supporting expressions, `let`, variable assignment, `say`, `if / else / elif`, `while`, `repeat <n> times`, functions, recursion, lexical closures, array indexing, and execution step quotas.
* **Standard Library Core**: Built-in native functions `say`, `print`, `type_of`, `len`, `push`, `pop`, `clock`, `assert`.
* **CLI (`unfish`)**: Full-featured CLI with commands `run`, `check`, `ast`, `tokens`, `repl`, `version`.
* **Interactive REPL**: Multiline block entry, persistent session arena, direct expression evaluation (`=> <value>`).
* **Verification Harness**: Unit test suite in C, stress test suite (`test_stress`), automated conformance test runner (`tools/run_conformance_tests.sh`), Makefile with ASan/UBSan targets.

### PARTIAL
* **Standard Library Modules**: Core built-ins implemented; external modules (`math`, `strings`, `fs`, `sys`) planned for Phase 8.

### BROKEN
* *None.* (All 4 unit test suites, 8 stress tests, and 19 conformance tests pass with 0 errors).

### MISSING
* **Key-Value Maps / Dictionaries** (Phase 3 Part 2).
* **Block ↔ AST Round-Tripping**: Visual block translation (Phase 5).
* **Interactive Debugger**: Breakpoint and step debugging hooks (Phase 6).
* **Bytecode VM**: Stack-based virtual machine (Phase 7).
* **AOT Compiler**: C code emission and native compiler (Phase 9).

### EXPERIMENTAL
* *None.*

---

## 2. Milestone Checkpoint (Section 51)

* **Current Milestone:** Milestone 2 Complete (Collections: Dynamic Arrays, Indexing, and Mutability)
* **Status:** VERIFIED & COMPLETE
* **Implemented:**
  - Common infrastructure: Arena allocator, dynamic strings, symbol interner, diagnostic reporter.
  - Lexer with brackets and off-side indentation engine.
  - Pratt parser supporting array literals `[e1, e2]` and postfix subscript `target[index]`.
  - Recursive descent supporting in-place array assignment `target[index] = val`.
  - Semantic analyzer validating array expressions and index assignments.
  - Runtime `UfArrayObject` with dynamic capacity growth, structural equality, and GC traversal.
  - Garbage collection roots: global environment, activation frames, `current_env` block chain, and `temp_roots` evaluation stack.
  - Standard library array built-ins: `len()`, `push()`, `pop()`.
  - Complex algorithmic programs: Bubble Sort (`examples/sorting.unfish`).
* **Verified:**
  - Unit tests: `test_lexer`, `test_parser`, `test_semantic`, `test_interpreter`.
  - Stress tests: `test_stress` (deep closures, mutual recursion, shadowing, GC pressure with 8 live collections, 60-nested parens, 512-frame stack overflow guard, execution quota guard, 13 fuzz inputs).
  - Conformance test suite: 19 test cases.
  - 100% clean execution under AddressSanitizer and UndefinedBehaviorSanitizer with zero memory leaks.
* **Partially Implemented:**
  - Key-value maps (scheduled next).
* **Known Bugs:**
  - None.
* **Known Limitations:**
  - Maps (`{}`) not yet implemented.
  - Slicing (`arr[1:3]`) not yet implemented.
  - Execution engine is currently tree-walking; bytecode VM scheduled for Phase 7.
* **Architectural Decisions:**
  - ADR 001 through ADR 009 accepted and documented.
* **Tests:**
  - 5 C test binaries (all passing).
  - 19 automated conformance tests (all passing).
* **Performance:**
  - Bubble sort of array of numbers: < 2ms.
* **Documentation Updated:**
  - `LANGUAGE_SPEC.md`, `TYPE_SYSTEM.md`, `MEMORY_MODEL.md`, `RUNTIME.md`, `ARCHITECTURE.md`, `DECISIONS.md`, `PROJECT_STATE.md`, `CHANGELOG.md`.
* **Next Recommended Task:**
  - Phase 3 Part 2: Key-Value Hash Maps (`{ "key": value }`).
