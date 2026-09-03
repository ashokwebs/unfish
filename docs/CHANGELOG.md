# UNFISH — CHANGELOG

All notable changes to the Unfish programming language will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.8.0-alpha] - 2026-09-04

### Added
- **Module System Architecture**: Full support for `import <module> [as <alias>]` and selective `from <module> import <symbols>` (ADR 012).
- **First-Class Module Objects**: Added `UF_OBJ_MODULE` and `UF_VAL_MODULE` (`UfModuleObject`) with namespace dot access (`math.sqrt`, `calc.add`).
- **Arena-Isolated Module Lifetime**: Each module file owns its own `UfArena` and `UfInterner`, ensuring function bodies, parameter names, and AST nodes safely outlive import execution without memory leaks or use-after-free bugs.
- **Singleton Module Cache**: Runtime module cache in `UfRuntime` ensuring deterministic single evaluation per module across import graphs.
- **Circular Dependency Detection**: `CircularImportError` raised if cyclic module dependencies occur during loading, cleanly unwinding runtime frames.
- **Multi-Source Path Resolution**: Module resolution supporting relative imports relative to caller file (`<caller_dir>/<mod>.unfish`), working directory, and `UNFISH_PATH` environment variable.
- **Lazy Standard Library Built-in Modules**: On-demand lazy instantiation of built-in `math` and `strings` modules upon import.
- **Module Conformance & Stress Tests**: Added `26_import_module.unfish`, `27_from_import.unfish`, `err_circular_import.unfish`, `err_module_not_found.unfish`, and `err_import_symbol_not_found.unfish`, expanding conformance suite to 41 passing tests. Added `test_module_stress` to `test_stress` under aggressive GC pressure.
- **Example Applications**: Added `examples/geometry.unfish` and `examples/modules_demo.unfish` showcasing modular application organization.
- **Documentation**: Updated ADR 012 in `docs/DECISIONS.md`; updated `docs/LANGUAGE_SPEC.md` and `docs/PROJECT_STATE.md`.

---

## [0.7.0-alpha] - 2026-09-04

### Added
- **Structured Exception Recovery (try/catch)**: `try: <block> catch <ident>: <block>` syntax (`UF_STMT_TRY_CATCH`) supporting block indentation and nested error handling (ADR 017).
- **First-Class Error Objects**: Added `UF_OBJ_ERROR` and `UF_VAL_ERROR` (`UfErrorObject`) storing error message, exception kind, source line, and source file.
- **Error Object Property Introspection**: Dot property access desugaring for `err.message`, `err.kind`, `err.line`, and `err.file`.
- **Non-Local Stack Unwinding via setjmp/longjmp**: Runtime try handler stack in `UfRuntime` that cleanly restores frame counts, temp roots, and environment pointers without memory leaks under mark-and-sweep GC.
- **User Exception Built-in (`error`)**: `error(message, [kind])` standard library procedure for domain validations, raising catchable exceptions with custom kind strings (defaults to `"UserError"`).
- **GC Root In-Flight Preservation**: Added GC root marking for `rt->current_error` to protect error objects during garbage collection cycles inside catch handlers.
- **Exception Conformance & Stress Tests**: Added `23_try_catch.unfish`, `24_nested_try_catch.unfish`, `25_user_errors.unfish`, and `err_uncaught_error.unfish`, bringing conformance suite to 36 passing tests. Added `test_try_catch_gc_stress` to `test_stress`.
- **Example Applications**: Added `examples/error_handling.unfish` demonstrating safe division and multi-field JSON-like object validation.
- **Documentation**: Recorded ADR 017 in `docs/DECISIONS.md`; updated `LANGUAGE_SPEC.md`, `STANDARD_LIBRARY.md`, and `PROJECT_STATE.md`.

---

## [0.6.0-alpha] - 2026-09-04

### Added
- **Modular Standard Library Architecture**: Created standalone `src/runtime/uf_stdlib.c` and `uf_stdlib.h` with dual runtime registration and semantic symbol validation (ADR 016).
- **String Standard Library (15 Functions)**:
  * `split(str, delim)`: tokenize string into array of strings (supports empty delimiter for char splitting).
  * `join(arr, sep)`: join array elements into single string with delimiter.
  * `trim(str)`: strip leading/trailing whitespace.
  * `replace(str, old, new)`: substring replacement.
  * `to_upper(str)` and `to_lower(str)`: case conversions.
  * `contains(str, sub)`, `starts_with(str, pfx)`, `ends_with(str, sfx)`: substring predicate tests.
  * `char_at(str, idx)`: character indexing with negative offset support.
  * `to_number(str)`: robust string-to-number parser returning `null` on invalid strings.
  * `to_string(val)`: universal value-to-string formatter.
  * `repeat_string(str, n)`: string repetition with safety bounds.
  * `substring(str, start, [end])`: string slicing with boundary clamping and negative indices.
  * `index_of(str, sub)`: search index or `-1`.
- **Math Standard Library (14 Functions & 3 Constants)**:
  * Constants: `PI`, `E`, `INFINITY`.
  * Functions: `abs`, `floor`, `ceil`, `round`, `sqrt`, `pow`, `min`, `max`, `log`, `sin`, `cos`, `tan`, `random`, `random_int`.
- **Stdlib Conformance Tests**: Added `21_string_operations.unfish` and `22_math_builtins.unfish`, expanding the conformance suite to 32 tests.
- **Example Applications**: Added `examples/strings.unfish` showcasing CSV parsing, report generation, and ASCII banner design.
- **Documentation**: Recorded ADR 016 in `docs/DECISIONS.md`; updated `STANDARD_LIBRARY.md`, `PROJECT_STATE.md`.

---

## [0.5.0-alpha] - 2026-09-04

### Added
- **First-Class Anonymous Functions / Lambdas**: `function([params]): body` expression syntax (`UF_EXPR_FUNCTION`) supporting inline expressions with implicit return (`function(x): x * 2`), inline explicit return (`function(x): return x * 2`), and multiline indented closure bodies (ADR 015).
- **Self-Recursive Named Function Expressions**: Named lambda expressions (`let fact = function factorial(n): ...`) bind the function identifier within the closure's call scope for self-recursion.
- **Universal Runtime Call Callback**: Decoupled `uf_runtime_call` via `UfCallValueFn` pointer in `UfRuntime`, allowing C native functions to invoke user-defined closures seamlessly.
- **Higher-Order Standard Library Functions**:
  * `map(arr, fn)`: element-wise array transformation.
  * `filter(arr, fn)`: predicate-based element retention.
  * `reduce(arr, fn, [init])`: left fold reduction with optional initial accumulator.
  * `sort(arr, [cmp])`: stable array sorting supporting numbers, strings, and custom comparator callbacks `cmp(a, b)`.
  * `reverse(arr)`: reversed array shallow copy.
  * `find(arr, fn)`: first predicate match or `null`.
  * `every(arr, fn)`: universal quantification check.
  * `some(arr, fn)`: existential quantification check.
- **Functional Conformance Tests**: Added `18_higher_order.unfish`, `19_sort_and_search.unfish`, `20_anonymous_functions.unfish`, expanding the conformance suite to 30 tests.
- **Functional Pipeline Example**: `examples/functional.unfish` demonstrating map/filter/reduce, predicate searches, and custom object sorting.
- **Documentation**: Recorded ADR 015 in `docs/DECISIONS.md`; updated `LANGUAGE_SPEC.md`, `STANDARD_LIBRARY.md`, `PROJECT_STATE.md`.

---

## [0.4.0-alpha] - 2026-09-04

### Added
- **First-Class Hash Maps**: Key-value associative collections (`UF_OBJ_MAP` / `UF_VAL_MAP`) implemented via open-addressing hash table with linear probing and 75% load factor (ADR 014).
- **Map Literal Syntax**: `{key: value, ...}` syntax supporting both string expression keys and unquoted identifier keys (`{name: "Alice"}`).
- **Dot Property Access Sugar**: Pratt parser desugars `target.prop` into `target["prop"]` (`UF_EXPR_INDEX`) and `target.prop = val` into `target["prop"] = val` (`UF_STMT_INDEX_ASSIGN`).
- **Insertion-Order Iteration**: Parallel `order_keys` array maintains deterministic key insertion order across `keys()`, `values()`, and direct `for key in map:` traversal.
- **Deep Structural Equality**: Full recursive structural equality check across nested maps, arrays, strings, and primitives.
- **Map Built-ins**: Added `keys(map)`, `values(map)`, `has_key(map, key)`, `delete(map, key)`, and extended `len(map)` to return map entry count.
- **Map GC Integration**: Full mark-and-sweep tracking of keys, values, and order buffers with zero memory leaks.
- **Stress & Conformance Tests**: Added `test_map_gc_stress` in unit tests, and 4 new conformance tests (`15_maps`, `16_map_iteration`, `17_dot_access`, `err_map_invalid_key_type`), bringing the conformance suite to 27 tests.
- **Universal Value Call Interface**: Exported `uf_call_value` in `uf_interpreter.h` to enable clean native call dispatch.
- **Documentation**: Recorded ADR 014 in `docs/DECISIONS.md`; updated `LANGUAGE_SPEC.md`, `TYPE_SYSTEM.md`, `STANDARD_LIBRARY.md`, `PROJECT_STATE.md`.

---

## [0.3.0-alpha] - 2026-09-04

### Added
- **Control Flow Primitives (`break` & `continue`)**: Keywords, AST nodes (`UF_STMT_BREAK`, `UF_STMT_CONTINUE`), compile-time loop-depth semantic validation, and early exit / iteration skip execution across `while`, `repeat`, and `for` loops (ADR 010).
- **Collection Traversal (`for-in`)**: `for <item> in <iterable>:` statement (`UF_STMT_FOR`), natively iterating over arrays and UTF-8 strings.
- **Built-in `range()` Generator**: `range([start,] end[, step])` producing number progression arrays up to 1,000,000 elements.
- **Object-Graph GC Stress Testing**: `test_nested_arrays_closures_gc_stress` in `test_stress.c` verifying closures capturing dynamic arrays, arrays containing closures, dynamic reallocation during pushes, and multiple live GC sweeps under low thresholds.
- **Architectural Decision Records**:
  * ADR 010: Control Flow Primitives (`break`, `continue`, `for-in`, `range`).
  * ADR 011: Language-Level Error Handling Model (Gradual Two-Tier Model: Result/Null vs `try/catch`).
  * ADR 012: Module Resolution, Namespaces, and Singleton Lifecycle.
  * ADR 013: Bytecode Virtual Machine Pipeline & Intermediate Representation (IR) Contract.
- **Conformance Suite Expansion**: Conformance test cases expanded to 23 tests, including `13_for_in_loops.unfish`, `14_break_continue.unfish`, `err_break_outside_loop.unfish`, and `err_continue_outside_loop.unfish`.

---

## [0.2.0-alpha] - 2026-09-03

### Added
- **First-Class Dynamic Arrays**: `[e1, e2, ...]` array literals, dynamically resizable `UfArrayObject` heap objects tracked by GC (ADR 009).
- **Subscript Indexing**: `target[index]` supporting both array and string indexing, with negative offset support (`arr[-1]`).
- **In-Place Index Assignment**: `arr[index] = value` syntax and evaluation.
- **Array Built-ins**: `len(arr)` returning element count, `push(arr, val)` appending elements, and `pop(arr)` popping elements.
- **Structural Array Equality**: Deep element-wise equality comparison for nested arrays.
- **Comprehensive Stress Test Suite**: `tests/unit/test_stress.c` verifying 4-level closures, mutual recursion, variable shadowing, GC stress under 1KB threshold with multiple active collections, 60-level nested expressions, 512-frame stack overflow limits, step quotas, and 13 malformed fuzz inputs.
- **Conformance Suite Expansion**: Conformance test cases expanded to 19 tests, including sorting algorithms (`bubble_sort.unfish`).
- **Examples**: `examples/arrays.unfish` and `examples/sorting.unfish`.

### Changed / Fixed
- **Function Hoisting Inconsistency**: Fixed top-level function hoisting in the interpreter (`uf_interpret_program`) to mirror semantic analysis pass, resolving undefined identifier errors on forward calls.
- **GC Temporary Roots**: Implemented `rt->temp_roots` evaluation stack to guarantee all intermediate subexpression values are rooted during GC collections (ADR 008).
- **GC Active Block Scope Rooting**: Implemented `rt->current_env` tracking across block statements (`while`, `repeat`, `if`) and call frames to ensure executing block environments are never swept during loop allocations (ADR 008).

---

## [0.1.0-alpha] - 2026-09-03

### Added
- Initial language architecture, documentation suite (22 files), lexer, parser, semantic analyzer, runtime, interpreter, and CLI.
