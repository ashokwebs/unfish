# 🐡 Unfish

**Unfish** is a real, general-purpose programming language, runtime, compiler, and educational computing environment written in clean ANSI C99 with zero external dependencies.

It is designed to provide an unbroken path of intellectual ascent: from visual block programming, into clean indentation-based text programming, to algorithms, data structures, runtime architecture, garbage collection, bytecode virtual machines, and low-level systems programming.

> *"Hide complexity until the learner is ready — never hide it forever."*

---

## Features

- **Lexer & Indentation Engine**: Off-side indentation rules (`INDENT`, `DEDENT`, `NEWLINE`) with full UTF-8 support and column-accurate source span tracking.
- **Pratt Expression Parser & Recursive Descent**: Highly deterministic parser producing a strongly-typed Abstract Syntax Tree (AST).
- **Semantic Analysis**: Lexical scope analysis, top-level function hoisting, duplicate declaration detection, loop-depth control flow validation, and Levenshtein distance "Did you mean?" suggestions.
- **Memory Model & Garbage Collector**: 64KB chunk linear arena allocator for compilation; object-tracked mark-and-sweep GC (`UfObj`) with active block scope tracking and temporary evaluation root protection.
- **Values & Data Structures**: 16-byte tagged union (`UfValue`), dynamic strings, first-class closures, dynamic resizable arrays with negative indexing (`arr[-1]`), and in-place assignment.
- **Control Flow**: `if` / `else`, `while`, `repeat N times`, `for item in collection:`, `break`, `continue`, and numeric `range()` generators.
- **Educational Diagnostics**: Visual snippets with line numbers, gutters, column carets (`^~~~~`), and actionable remediation hints.
- **Unified CLI (`unfish`)**: Subcommands `run`, `check`, `ast`, `tokens`, `repl`, `version`.
- **Zero Memory Leaks**: 100% verified clean execution under AddressSanitizer and UndefinedBehaviorSanitizer.

---

## Quick Start

### Building from Source

Requirements: Any standard C99 compiler (`gcc` or `clang`) and `make`.

```bash
# Build optimized binary in bin/unfish
make

# Run an example
./bin/unfish run examples/hello.unfish

# Interactive REPL
./bin/unfish repl
```

### Running Tests

```bash
# Run all unit tests, stress tests, and conformance tests under AddressSanitizer + UBSan
make test-asan
```

---

## Documentation

The project includes an exhaustive technical documentation suite in [`docs/`](docs/):

- [`docs/VISION.md`](docs/VISION.md) — Foundational philosophy, guiding principles, and pedagogical progression.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — Decoupled pipeline architecture and subsystem inventory.
- [`docs/LANGUAGE_SPEC.md`](docs/LANGUAGE_SPEC.md) — Formal EBNF grammar, lexical conventions, and operator precedence table.
- [`docs/TYPE_SYSTEM.md`](docs/TYPE_SYSTEM.md) — 4-tier pedagogical type system progression.
- [`docs/MEMORY_MODEL.md`](docs/MEMORY_MODEL.md) — Dual compilation-arena and GC runtime memory architecture.
- [`docs/RUNTIME.md`](docs/RUNTIME.md) — Environments (`UfEnv`), activation call frames, and runtime execution model.
- [`docs/ERROR_MODEL.md`](docs/ERROR_MODEL.md) — Gradual two-tier error handling model and diagnostics format.
- [`docs/DECISIONS.md`](docs/DECISIONS.md) — Architectural Decision Records (ADRs 001 through 013).
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — Phase-by-phase development roadmap.
- [`docs/PROJECT_STATE.md`](docs/PROJECT_STATE.md) — Comprehensive inventory of current capabilities and health.
- [`plan.md`](plan.md) — Master A-to-Z engineering plan.

---

## License

MIT License. See project headers for details.
