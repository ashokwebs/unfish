# UNFISH — CHANGELOG

All notable changes to the Unfish programming language will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.2.0-alpha] - 2026-09-03

### Added
- **First-Class Dynamic Arrays**: `[e1, e2, ...]` array literals, dynamically resizable `UfArrayObject` heap objects tracked by GC.
- **Subscript Indexing**: `target[index]` supporting both array and string indexing, with negative offset support (`arr[-1]`).
- **In-Place Index Assignment**: `arr[index] = value` syntax and evaluation.
- **Array Built-ins**: `len(arr)` returning element count, `push(arr, val)` appending elements, and `pop(arr)` popping elements.
- **Structural Array Equality**: Deep element-wise equality comparison for nested arrays.
- **Comprehensive Stress Test Suite**: `tests/unit/test_stress.c` verifying 4-level closures, mutual recursion, variable shadowing, GC stress under 1KB threshold with multiple active collections, 60-level nested expressions, 512-frame stack overflow limits, step quotas, and 13 malformed fuzz inputs.
- **Conformance Suite Expansion**: Conformance test cases expanded to 19 tests, including sorting algorithms (`bubble_sort.unfish`).
- **Examples**: `examples/arrays.unfish` and `examples/sorting.unfish`.

### Changed / Fixed
- **Function Hoisting Inconsistency**: Fixed top-level function hoisting in the interpreter (`uf_interpret_program`) to mirror semantic analysis pass, resolving undefined identifier errors on forward calls.
- **GC Temporary Roots**: Implemented `rt->temp_roots` evaluation stack to guarantee all intermediate subexpression values are rooted during GC collections.
- **GC Active Block Scope Rooting**: Implemented `rt->current_env` tracking across block statements (`while`, `repeat`, `if`) and call frames to ensure executing block environments are never swept during loop allocations.

---

## [0.1.0-alpha] - 2026-09-03

### Added
- Initial language architecture, documentation suite (22 files), lexer, parser, semantic analyzer, runtime, interpreter, and CLI.
