# UNFISH — PROJECT STATE INVENTORY

**Last Updated:** Milestone 7 Complete (try/catch Error Recovery, First-Class Errors, Stack Unwinding, ADR 017)  
**Project Health:** Healthy, 100% Tests Passing under ASan / UBSan  

---

## 1. Inventory by Status

### COMPLETE
* **Formal Language Specification & Grammar**: EBNF, lexical rules, off-side indentation, operator precedence table (`docs/LANGUAGE_SPEC.md`).
* **Complete Documentation Architecture**: 23 living technical documents in `docs/` covering vision, architecture, memory model, type system, error model, runtime, VM roadmap, compiler roadmap, blocks, tooling, testing, and security.
* **Architectural Decision Records (ADRs)**: ADR 001 through ADR 017 documented in `docs/DECISIONS.md`.
* **Lexer**: UTF-8 scanner emitting typed tokens (`UfToken`) with source spans, indentation stack (`INDENT`, `DEDENT`, `NEWLINE`), string escapes, number scanning, brackets (`[`, `]`), braces (`{`, `}`), dot (`.`), keywords (`let`, `say`, `function`, `return`, `if`, `else`, `while`, `repeat`, `times`, `for`, `in`, `break`, `continue`, `try`, `catch`, `and`, `or`, `not`, `true`, `false`, `null`), and diagnostics.
* **AST Data Structures**: Strongly typed AST nodes with source span preservation (`UfExpr`, `UfStmt`, `UfProgram`).
* **AST Pretty Printer**: S-expression tree printer (`uf_ast_print`) supporting all statements, expressions, array literals, map literals, anonymous function expressions, and try-catch blocks.
* **Parser**: Pratt parser for expressions; recursive descent for statements, assignments, blocks, loops, functions, and try/catch recovery blocks (`try: ... catch <err>: ...`).
* **Semantic Analysis**: Lexical scope analysis, symbol tables, undefined identifier detection with Levenshtein suggestions, duplicate declaration prevention, return-outside-function checks, loop-depth checks, arity checks, anonymous function scope analysis, try-catch variable scoping, standard library symbol table registration, and top-level function hoisting.
* **Diagnostics Engine**: Colored terminal diagnostics with source code snippets, line numbers, and column-accurate carets (`^~~~~`).
* **Runtime & Value Representation**: Tagged union `UfValue` (`Null`, `Boolean`, `Number`, `String`, `Array`, `Map`, `Function`, `NativeFunction`, `Error`), deep structural equality for arrays and maps, string conversion, truthiness, and call stack backtraces.
* **Structured Exception Recovery (try/catch)**: First-class `UfErrorObject` with `.message`, `.kind`, `.line`, and `.file` property introspection; `setjmp`/`longjmp` stack unwinding restoring scope frames, call frames, and evaluation roots without memory leaks.
* **User Exception Built-in (`error`)**: `error(message, [kind])` standard library procedure for domain assertions and custom error reporting.
* **First-Class Dynamic Arrays**: Heap-allocated `UfArrayObject` with dynamic resizing, negative index support (`arr[-1]`), string subscripting (`s[0]`), in-place mutation (`arr[i] = v`), `len()`, `push()`, and `pop()`.
* **First-Class Hash Maps**: Open-addressing hash table with linear probing, FNV-1a hashing, deterministic insertion-order preservation, deep structural equality, dot property syntax sugar (`user.name`), `len()`, `keys()`, `values()`, `has_key()`, and `delete()`.
* **First-Class Functions & Lambdas**: Full lexical closure support, anonymous function expressions (`function(x): x * 2`), inline expression bodies with implicit return, multiline closures, immediately invoked function expressions (IIFE), and self-recursive named function expressions.
* **Higher-Order Functions**: Full suite of native functional primitives (`map`, `filter`, `reduce`, `sort` with custom comparators, `reverse`, `find`, `every`, `some`).
* **String Standard Library**: Full suite of 15 string functions: `split`, `join`, `trim`, `replace`, `to_upper`, `to_lower`, `contains`, `starts_with`, `ends_with`, `char_at`, `to_number`, `to_string`, `repeat_string`, `substring`, `index_of`.
* **Math Standard Library**: Full suite of mathematical functions (`abs`, `floor`, `ceil`, `round`, `sqrt`, `pow`, `min`, `max`, `log`, `sin`, `cos`, `tan`, `random`, `random_int`) and mathematical constants (`PI`, `E`, `INFINITY`).
* **High-Level Iteration & Control Flow**: `for <item> in <iterable>:` native traversal over arrays, maps (keys), and strings; `break` and `continue` with semantic loop validation; and `range([start,] end[, step])` generator.
* **Lexical Environments**: Linked scope frames (`UfEnv`) supporting variable declaration, lookup, and mutation.
* **Garbage Collection**: Object-tracked mark-and-sweep GC (`UfObj`) supporting circular closure/env references, active block scoping (`current_env`), evaluation temporary roots (`temp_roots`), in-flight exception root preservation (`rt->current_error`), live GC sweeps during functional pipelines and error recovery, and leak-free teardown under ASan/UBSan.
* **Tree-Walking Interpreter**: AST evaluator supporting expressions, statements, closures, recursion, loops with early breaks/continues, step quota guards, recursion stack overflow traps, and universal `uf_runtime_call` interface.
* **CLI (`unfish`)**: Full-featured CLI with commands `run`, `check`, `ast`, `tokens`, `repl`, `version`.
* **Interactive REPL**: Multiline block entry, persistent session arena, direct expression evaluation.
* **Verification Harness**: 4 unit test binaries in C, comprehensive stress test suite (`test_stress`, 11 tests including GC collection during try/catch unwinding), automated conformance test runner (`tools/run_conformance_tests.sh`) with 36 test suites passing under ASan/UBSan.

### PARTIAL
* **Filesystem & System Modules**: Sandboxed I/O planned for Phase 8.

### BROKEN
* *None.* (All 4 unit test suites, 11 stress tests, and 36 conformance tests pass with 0 errors).

### MISSING
* **Module System** (`import` / `export` - Section I).
* **Block ↔ AST Round-Tripping**: Visual block translation (Phase 5).
* **Interactive Debugger**: Breakpoint and step debugging hooks (Phase 6).
* **Bytecode VM**: Stack-based virtual machine (Phase 7, ISA designed in ADR 013).
* **AOT Compiler**: C code emission and native compiler (Phase 9).

### EXPERIMENTAL
* *None.*

---

## 2. Milestone Checkpoint (Section 51)

* **Current Milestone:** Milestone 7 Complete (Structured Exception Recovery with try/catch, Stack Unwinding, First-Class Errors, and ADR 017)
* **Status:** VERIFIED & COMPLETE
* **Implemented:**
  - `try` and `catch` lexer tokens with source span preservation.
  - `UF_STMT_TRY_CATCH` AST node with S-expression pretty printer support.
  - Parser grammar rule: `try: <block> catch <ident>: <block>`.
  - Semantic scope analysis for catch error variable isolation.
  - First-class `UF_OBJ_ERROR` / `UF_VAL_ERROR` representation (`UfErrorObject`) with `.message`, `.kind`, `.line`, `.file` property introspection.
  - Non-local stack unwinding via `setjmp`/`longjmp` handler stack with frame/scope/root restoration in `UfRuntime`.
  - Standard library `error(message, [kind])` built-in function.
  - GC root preservation for in-flight error objects (`rt->current_error`).
  - Conformance test suite expanded to 36 tests (`23_try_catch`, `24_nested_try_catch`, `25_user_errors`, `err_uncaught_error`).
  - Stress test suite expanded to 11 tests (`test_try_catch_gc_stress` under aggressive GC pressure).
  - Example demonstration in `examples/error_handling.unfish`.
  - Architectural decision ADR 017 recorded in `docs/DECISIONS.md`.
* **Verified:**
  - Unit tests: `test_lexer`, `test_parser`, `test_semantic`, `test_interpreter`.
  - Stress tests: `test_stress` (11 tests including try/catch GC unwinding).
  - Conformance test suite: 36 test cases.
  - 100% clean execution under AddressSanitizer and UndefinedBehaviorSanitizer with zero memory leaks.
* **Next Recommended Task:**
  - Section I: Module System (`import` and `from ... import`, ADR 012).
