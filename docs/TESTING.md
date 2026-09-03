# UNFISH — TESTING STRATEGY & SPECIFICATION

---

## 1. Multi-Tiered Verification

Unfish enforces a zero-compromise testing philosophy:
1. **Unit Tests (C Level)**: Direct validation of individual modules: arena allocation, lexer token stream, Pratt parser expression trees, semantic symbol tables, and value operations.
2. **Language Conformance Tests (`tests/conformance/`)**: End-to-end tests consisting of valid and intentionally invalid `.unfish` source files with expected outputs or expected diagnostic errors.
3. **Memory Safety & Sanitizer Validation**: Continuous execution under AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan).
4. **Fuzz Testing**: Property-based and mutation-based fuzzing of the lexer and parser.

## 2. Conformance Test Structure

Each conformance test file contains embedded metadata comments declaring expected stdout or expected error:

```unfish
# test: functions/greet.unfish
# expect: Hello, World!

let name = "World"

function greet(person):
    say "Hello, " + person

greet(name)
```

For negative test cases:

```unfish
# test: errors/undefined_var.unfish
# expect-error: SemanticError
# expect-line: 3

say missing_var
```

## 3. Automated Test Runner

The project Makefile provides standard test targets:
* `make test`: Builds and runs all unit tests and conformance tests.
* `make test-asan`: Runs the entire test suite compiled with `-fsanitize=address,undefined`.
* `make test-valgrind`: Runs tests under Valgrind (when installed).
* `make fuzz`: Runs the parser against randomized and malformed byte sequences.
