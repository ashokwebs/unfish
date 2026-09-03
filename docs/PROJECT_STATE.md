# UNFISH — PROJECT STATE INVENTORY

**Last Updated:** Milestone 8 Complete (Module System, Singleton Cache, Circular Detection, ADR 012)  
**Project Health:** Healthy, 100% Tests Passing under ASan / UBSan  

---

## 1. Inventory by Status

### COMPLETE
* **Formal Language Specification & Grammar**: EBNF, lexical rules, off-side indentation, operator precedence table (`docs/LANGUAGE_SPEC.md`).
* **Complete Documentation Architecture**: 23 living technical documents in `docs/` covering vision, architecture, memory model, type system, error model, runtime, VM roadmap, compiler roadmap, blocks, tooling, testing, and security.
* **Architectural Decision Records (ADRs)**: ADR 001 through ADR 017 documented in `docs/DECISIONS.md`.
* **Lexer**: UTF-8 scanner emitting typed tokens (`UfToken`) with source spans, indentation stack (`INDENT`, `DEDENT`, `NEWLINE`), string escapes, number scanning, brackets (`[`, `]`), braces (`{`, `}`), dot (`.`), keywords (`let`, `say`, `function`, `return`, `if`, `else`, `while`, `repeat`, `times`, `for`, `in`, `break`, `continue`, `try`, `catch`, `import`, `from`, `as`, `and`, `or`, `not`, `true`, `false`, `null`), and diagnostics.
* **AST Data Structures**: Strongly typed AST nodes with source span preservation (`UfExpr`, `UfStmt`, `UfProgram`).
* **AST Pretty Printer**: S-expression tree printer (`uf_ast_print`) supporting all statements, expressions, array literals, map literals, anonymous function expressions, try-catch blocks, and import/from-import statements.
* **Parser**: Pratt parser for expressions; recursive descent for statements, assignments, blocks, loops, functions, try/catch recovery blocks, and import statements (`import <mod> [as <alias>]`, `from <mod> import <syms>`).
* **Semantic Analysis**: Lexical scope analysis, symbol tables, undefined identifier detection with Levenshtein suggestions, duplicate declaration prevention, return-outside-function checks, loop-depth checks, arity checks, anonymous function scope analysis, try-catch variable scoping, module import scoping, standard library symbol table registration, and top-level function hoisting.
* **Diagnostics Engine**: Colored terminal diagnostics with source code snippets, line numbers, and column-accurate carets (`^~~~~`).
* **Runtime & Value Representation**: Tagged union `UfValue` (`Null`, `Boolean`, `Number`, `String`, `Array`, `Map`, `Function`, `NativeFunction`, `Error`, `Module`), deep structural equality for arrays and maps, string conversion, truthiness, and call stack backtraces.
* **Module System**: First-class `UfModuleObject` (`UF_OBJ_MODULE` / `UF_VAL_MODULE`), isolated `UfArena` and `UfInterner` lifetimes per module, singleton module caching (`rt->module_cache`), cycle detection (`CircularImportError`), lazy standard module initialization (`math`, `strings`), and multi-tier path resolution.
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
* **Verification Harness**: 4 unit test binaries in C, comprehensive stress test suite (`test_stress`, 12 tests including module GC pressure), automated conformance test runner (`tools/run_conformance_tests.sh`) with 41 test suites passing under ASan/UBSan.

### PARTIAL
* **Filesystem & System Modules**: Sandboxed I/O planned for Section J.

### BROKEN
* *None.* (All 4 unit test suites, 12 stress tests, and 41 conformance tests pass with 0 errors).

### MISSING
* **Standard Library Modules**: Full native module expansion (`sys`, `fs`, `random`, `time`, `json`, `testing` - Section J).
* **Block ↔ AST Round-Tripping**: Visual block translation (Phase 5).
* **Interactive Debugger**: Breakpoint and step debugging hooks (Phase 6).
* **Bytecode VM**: Stack-based virtual machine (Phase 7, ISA designed in ADR 013).
* **AOT Compiler**: C code emission and native compiler (Phase 9).

### EXPERIMENTAL
* *None.*

---

## 2. Milestone Checkpoint (Section 51)

* **Current Milestone:** Milestone 8 Complete (Module System, Singleton Cache, Circular Import Detection, ADR 012)
* **Status:** VERIFIED & COMPLETE
* **Implemented:**
  - `import`, `from`, and `as` lexer tokens with source span preservation.
  - `UF_STMT_IMPORT` and `UF_STMT_FROM_IMPORT` AST nodes with S-expression pretty printer support.
  - Parser rules: `import <mod> [as <alias>]` and `from <mod> import <syms>`.
  - Semantic scope analysis registering imported module and symbol bindings.
  - First-class `UF_OBJ_MODULE` / `UF_VAL_MODULE` representation (`UfModuleObject`) with dot access (`mod.field`).
  - Isolated arena and interner lifetimes per module, guaranteeing memory safety for AST nodes and string literals.
  - Module lifecycle state machine (`UNLOADED` -> `LOADING` -> `LOADED`) with circular dependency traps (`CircularImportError`).
  - Runtime module cache (`rt->module_cache`) ensuring single-evaluation singleton semantics.
  - Multi-tier path resolution: relative to caller, relative to working directory, and `UNFISH_PATH`.
  - Lazy on-demand instantiation of standard library modules (`math`, `strings`).
  - Full GC mark-and-sweep integration for modules, their environments, and export maps.
  - Conformance test suite expanded to 41 tests (`26_import_module`, `27_from_import`, `err_circular_import`, `err_module_not_found`, `err_import_symbol_not_found`).
  - Stress test suite expanded to 12 tests (`test_module_stress` with live module calls under aggressive GC pressure).
  - Example demonstrations in `examples/geometry.unfish` and `examples/modules_demo.unfish`.
  - Architectural decision ADR 012 updated in `docs/DECISIONS.md`.
* **Verified:**
  - Unit tests: `test_lexer`, `test_parser`, `test_semantic`, `test_interpreter`.
  - Stress tests: `test_stress` (12 tests including module GC stress).
  - Conformance test suite: 41 test cases.
  - 100% clean execution under AddressSanitizer and UndefinedBehaviorSanitizer with zero memory leaks.
* **Next Recommended Task:**
  - Section J: Standard Library Modules (`sys`, `fs`, `time`, `json`, `testing`).
