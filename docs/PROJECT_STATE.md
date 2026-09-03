# UNFISH — PROJECT STATE INVENTORY

**Last Updated:** Milestone 9 Complete (Standard Library Modules: sys, fs, random, time, json, testing, ADR 019)  
**Project Health:** Healthy, 100% Tests Passing under ASan / UBSan  

---

## 1. Inventory by Status

### COMPLETE
* **Formal Language Specification & Grammar**: EBNF, lexical rules, off-side indentation, operator precedence table (`docs/LANGUAGE_SPEC.md`).
* **Complete Documentation Architecture**: 23 living technical documents in `docs/` covering vision, architecture, memory model, type system, error model, runtime, VM roadmap, compiler roadmap, blocks, tooling, testing, and security.
* **Architectural Decision Records (ADRs)**: ADR 001 through ADR 019 documented in `docs/DECISIONS.md`.
* **Lexer**: UTF-8 scanner emitting typed tokens (`UfToken`) with source spans, indentation stack (`INDENT`, `DEDENT`, `NEWLINE`), string escapes, number scanning, brackets (`[`, `]`), braces (`{`, `}`), dot (`.`), keywords (`let`, `say`, `function`, `return`, `if`, `else`, `while`, `repeat`, `times`, `for`, `in`, `break`, `continue`, `try`, `catch`, `import`, `from`, `as`, `and`, `or`, `not`, `true`, `false`, `null`), and diagnostics.
* **AST Data Structures**: Strongly typed AST nodes with source span preservation (`UfExpr`, `UfStmt`, `UfProgram`).
* **AST Pretty Printer**: S-expression tree printer (`uf_ast_print`) supporting all statements, expressions, array literals, map literals, anonymous function expressions, try-catch blocks, and import/from-import statements.
* **Parser**: Pratt parser for expressions; recursive descent for statements, assignments, blocks, loops, functions, try/catch recovery blocks, and import statements (`import <mod> [as <alias>]`, `from <mod> import <syms>`).
* **Semantic Analysis**: Lexical scope analysis, symbol tables, undefined identifier detection with Levenshtein suggestions, duplicate declaration prevention, return-outside-function checks, loop-depth checks, arity checks, anonymous function scope analysis, try-catch variable scoping, module import scoping, standard library symbol table registration, and top-level function hoisting.
* **Diagnostics Engine**: Colored terminal diagnostics with source code snippets, line numbers, and column-accurate carets (`^~~~~`).
* **Runtime & Value Representation**: Tagged union `UfValue` (`Null`, `Boolean`, `Number`, `String`, `Array`, `Map`, `Function`, `NativeFunction`, `Error`, `Module`), deep structural equality for arrays and maps, string conversion, truthiness, and call stack backtraces.
* **Module System**: First-class `UfModuleObject` (`UF_OBJ_MODULE` / `UF_VAL_MODULE`), isolated `UfArena` and `UfInterner` lifetimes per module, singleton module caching (`rt->module_cache`), cycle detection (`CircularImportError`), lazy standard module initialization, and multi-tier path resolution.
* **Standard Library Modules**: Full suite of 6 standard library modules:
  - `sys`: Process exit, command-line arguments, host platform detection, environment variables.
  - `fs`: Sandboxed filesystem operations (`read_text`, `write_text`, `exists`, `delete_file`).
  - `random`: Uniform pseudo-random floats, bounded integers, array element choice, Fisher-Yates array shuffling.
  - `time`: High-resolution monotonic clock, sleep delay, UNIX epoch timestamp.
  - `json`: Native recursive-descent JSON parser and compliant stringifier with character escaping.
  - `testing`: Standard testing module written in Unfish (`src/stdlib/testing.unfish`) with assertion helpers and isolated test suite runner.
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
* **Garbage Collection**: Object-tracked mark-and-sweep GC (`UfObj`) supporting circular closure/env references, active block scoping (`current_env`), evaluation temporary roots (`temp_roots`), in-flight exception root preservation (`rt->current_error`), module cache root preservation (`rt->module_cache`), live GC sweeps during functional pipelines and error recovery, and leak-free teardown under ASan/UBSan.
* **Tree-Walking Interpreter**: AST evaluator supporting expressions, statements, closures, recursion, loops with early breaks/continues, step quota guards, recursion stack overflow traps, and universal `uf_runtime_call` interface.
* **CLI (`unfish`)**: Full-featured CLI with commands `run`, `check`, `ast`, `tokens`, `repl`, `version`.
* **Interactive REPL**: Multiline block entry, persistent session arena, direct expression evaluation.
* **Verification Harness**: 4 unit test binaries in C, comprehensive stress test suite (`test_stress`, 13 tests including JSON/stdlib GC stress), automated conformance test runner (`tools/run_conformance_tests.sh`) with 46 test suites passing under ASan/UBSan.

### PARTIAL
* *None.*

### BROKEN
* *None.* (All 4 unit test suites, 13 stress tests, and 46 conformance tests pass with 0 errors).

### MISSING
* **Type Annotations & Gradual Checking** (Section K).
* **Block ↔ AST Round-Tripping**: Visual block translation (Section L / Phase 5).
* **Interactive Debugger**: Breakpoint and step debugging hooks (Section M / Phase 6).
* **Bytecode VM**: Stack-based virtual machine (Section N / Phase 7, ISA designed in ADR 013).
* **AOT Compiler**: C code emission and native compiler (Section P / Phase 9).

### EXPERIMENTAL
* *None.*

---

## 2. Milestone Checkpoint (Section 51)

* **Current Milestone:** Milestone 9 Complete (Standard Library Modules: sys, fs, random, time, json, testing, ADR 019)
* **Status:** VERIFIED & COMPLETE
* **Implemented:**
  - `sys` module: `exit`, `args`, `platform`, `env`.
  - `fs` module: `read_text`, `write_text`, `exists`, `delete_file`.
  - `random` module: `random`, `random_int`, `choice`, `shuffle`.
  - `time` module: `clock`, `sleep`, `timestamp`.
  - `json` module: `parse`, `stringify`.
  - `testing` module: `assert_equal`, `assert_true`, `assert_throws`, `run_tests` in `src/stdlib/testing.unfish`.
  - Lazy built-in module registration and fallback loading for `src/stdlib/*.unfish`.
  - CLI script argument forwarding into `UfRuntime` (`uf_runtime_set_args`).
  - Conformance test suite expanded to 46 tests (`28_stdlib_sys`, `29_stdlib_fs`, `30_stdlib_random_time`, `31_stdlib_json`, `32_stdlib_testing`).
  - Stress test suite expanded to 13 tests (`test_json_and_stdlib_stress`).
  - Architectural decision ADR 019 recorded in `docs/DECISIONS.md`.
* **Verified:**
  - Unit tests: `test_lexer`, `test_parser`, `test_semantic`, `test_interpreter`.
  - Stress tests: `test_stress` (13 tests including JSON stdlib GC stress).
  - Conformance test suite: 46 test cases.
  - 100% clean execution under AddressSanitizer and UndefinedBehaviorSanitizer with zero memory leaks.
* **Next Recommended Task:**
  - Section K: Type Annotations & Gradual Checking.
