# UNFISH — PROJECT STATE INVENTORY

**Last Updated:** Phase 1 & 2 Completion (Vertical Slice 1 Complete)  
**Project Health:** Healthy, 100% Tests Passing under ASan / UBSan  

---

## 1. Inventory by Status

### COMPLETE
* **Formal Language Specification & Grammar**: EBNF, lexical rules, off-side indentation, operator precedence table (`docs/LANGUAGE_SPEC.md`).
* **Complete Documentation Architecture**: 22 living technical documents in `docs/` covering vision, architecture, memory model, type system, error model, runtime, VM roadmap, compiler roadmap, blocks, tooling, testing, and security.
* **Lexer**: UTF-8 scanner emitting typed tokens (`UfToken`) with full source locations (`SourceLoc`, `SourceSpan`), indentation stack (`INDENT`, `DEDENT`, `NEWLINE`), string escapes (`\n`, `\t`, `\"`, `\\`), number scanning, comments, and diagnostics.
* **AST Data Structures**: Strongly typed AST nodes for expressions, statements, and blocks with source spans (`UfExpr`, `UfStmt`, `UfProgram`).
* **AST Pretty Printer**: S-expression tree printer (`uf_ast_print`) for inspection and debugging.
* **Parser**: Pratt parser for binary and unary expressions; recursive descent for statements and indentation-delimited blocks.
* **Semantic Analysis**: Lexical scope analysis, symbol tables, undefined identifier detection with Levenshtein distance "Did you mean?" suggestions, duplicate declaration prevention, return-outside-function checks, arity checks, and top-level function hoisting.
* **Diagnostics Engine**: Colored terminal diagnostics with source code snippets, line numbers, and column-accurate carets (`^~~~~`).
* **Runtime & Value Representation**: Tagged union `UfValue` (`Null`, `Boolean`, `Number`, `String`, `Function`, `NativeFunction`), string operations, truthiness, equality, and call stack backtraces.
* **Lexical Environments**: Linked scope frames (`UfEnv`) supporting variable declaration, lookup, and mutation.
* **Garbage Collection**: Object-tracked mark-and-sweep GC (`UfObj`) supporting circular references in closures and leak-free teardown.
* **Tree-Walking Interpreter**: AST evaluator supporting expressions, `let`, variable assignment, `say`, `if / else / elif`, `while`, `repeat <n> times`, functions, recursion, lexical closures, and execution step quotas.
* **Standard Library Core**: Built-in native functions `say`, `print`, `type_of`, `len`, `clock`, `assert`.
* **CLI (`unfish`)**: Full-featured CLI with commands `run`, `check`, `ast`, `tokens`, `repl`, `version`.
* **Interactive REPL**: Multiline block entry, persistent environment, direct expression evaluation (`=> <value>`).
* **Verification Harness**: Unit test suite in C, automated conformance test runner (`tools/run_conformance_tests.sh`), Makefile with ASan/UBSan targets.

### PARTIAL
* **Standard Library Modules**: Core built-ins implemented; external modules (`math`, `strings`, `fs`, `sys`) planned for Phase 8.

### BROKEN
* *None.* (All 4 unit test suites and 14 conformance tests pass with 0 errors).

### MISSING
* **First-Class Collections**: Arrays and Maps (Phase 3).
* **Block ↔ AST Round-Tripping**: Visual block translation (Phase 5).
* **Interactive Debugger**: Breakpoint and step debugging hooks (Phase 6).
* **Bytecode VM**: Stack-based virtual machine (Phase 7).
* **AOT Compiler**: C code emission and native compiler (Phase 9).

### EXPERIMENTAL
* *None.*

---

## 2. Milestone Checkpoint (Section 51)

* **Current Milestone:** Phase 1 & 2 Complete (Vertical Slice 1 & Core Language Implementation)
* **Status:** VERIFIED & COMPLETE
* **Implemented:**
  - Common infrastructure: Arena allocator, dynamic strings, symbol interner, diagnostic reporter.
  - Lexer with indentation engine (off-side rule).
  - Pratt & recursive descent parser.
  - AST definitions and S-expression formatter.
  - Semantic analyzer with lexical scoping, symbol tables, arity validation, and fuzzy suggestions.
  - Tagged union runtime values and object-tracked mark-and-sweep GC.
  - Tree-walking interpreter with recursion, closures, loops, and step quotas.
  - Standard library built-ins (`say`, `print`, `type_of`, `len`, `clock`, `assert`).
  - Unified CLI (`unfish run|check|ast|tokens|repl|version`).
* **Verified:**
  - Unit tests: `test_lexer`, `test_parser`, `test_semantic`, `test_interpreter`.
  - Conformance test suite: 14 test cases (8 positive, 6 negative error verification).
  - 100% clean execution under AddressSanitizer and UndefinedBehaviorSanitizer with zero memory leaks.
* **Partially Implemented:**
  - None within Phase 1 & 2 scope.
* **Known Bugs:**
  - None.
* **Known Limitations:**
  - Arrays and maps not yet implemented (scheduled for Phase 3).
  - Execution engine is currently tree-walking; bytecode VM scheduled for Phase 7.
* **Architectural Decisions:**
  - ADR 001: ANSI C99/C11 zero-dependency core.
  - ADR 002: Off-side indentation syntax (`INDENT`, `DEDENT`, `NEWLINE`).
  - ADR 003: Pratt parser for expressions.
  - ADR 004: Contiguous chunk arena allocator for compilation phases.
  - ADR 005: 16-byte tagged union `UfValue` representation.
  - ADR 006: Object-tracked mark-and-sweep garbage collection.
  - ADR 007: REPL persistent session arena.
* **Tests:**
  - 4 C unit test binaries (all passing).
  - 14 automated conformance tests (all passing).
* **Performance:**
  - CLI execution of vertical slice program: < 2ms.
  - 10th Fibonacci calculation: < 1ms.
* **Documentation Updated:**
  - All 22 files in `docs/` updated and verified.
* **Next Recommended Task:**
  - Phase 3: Collections (Arrays `[]`, Maps `{}`), index expressions, and higher-order functions.
