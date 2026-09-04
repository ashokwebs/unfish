# 🐡 Unfish

**Unfish** is a real, general-purpose programming language, runtime, compiler, and educational computing environment written in clean ANSI C99 with zero external dependencies.

It is designed to provide an unbroken path of intellectual ascent: from visual block programming, into clean indentation-based text programming, to algorithms, data structures, runtime architecture, garbage collection, bytecode virtual machines, and low-level systems programming.

> *"Hide complexity until the learner is ready — never hide it forever."*

---

## ⚡ Key Highlights & Architecture

Unfish features a unique **three-tier execution engine**:
1. **Tree-Walking AST Interpreter**: Fast startup, direct syntax tree interpretation, ideal for interactive exploration, REPL, and visual blocks.
2. **Bytecode Virtual Machine (`--vm`)**: 36-opcode stack-based VM with lexical upvalue capture cells, exception unwinding, and a 2x–6x execution speedup over AST tree-walking with 100% behavioral differential parity.
3. **Native C99 AOT Compiler (`build` / `emit-c`)**: Direct translation to standalone C99 with single-header runtime, delivering 30x–400x native execution speedups with zero interpreter dependencies.

### Additional Highlights
- **Cooperative Concurrency**: Fibers and Communicating Sequential Processes (CSP) channels (`spawn`, `yield`, `channel`, `send`, `recv`).
- **Systems Programming**: Contiguous byte buffers (`buffer`, `buffer_get`, `buffer_set`), multi-byte little-endian access (`u16`, `u32`, `i32`), and memory layout introspection (`inspect`).
- **Modern Developer Ergonomics**: Built-in Language Server Protocol (`unfish lsp`) with VS Code extension, interactive CLI debugger (`unfish debug`), and canonical code formatter (`unfish fmt`).
- **Bidirectional Block Round-Tripping**: Export AST to visual JSON blocks (`unfish blocks-export`) and import back into executable Unfish (`unfish blocks-import`).
- **Rich Standard Library**: Built-in modules for `sys`, `fs`, `random`, `time`, `json`, and `testing`.

---

## 🛠️ CLI Reference

The unified `unfish` CLI offers commands across the entire software development lifecycle:

| Command | Description |
|---|---|
| `unfish run <file>` | Run script via tree-walking AST interpreter |
| `unfish run --vm <file>` | Run script via high-speed Bytecode Virtual Machine |
| `unfish run --vm --debug <file>` | Run script with VM instruction tracer and visual stack dumps |
| `unfish compile <file>` | Compile script into bytecode chunk and disassemble |
| `unfish disasm <file>` | Disassemble script into human-readable bytecode instructions |
| `unfish emit-c [-o out.c] <file>` | Transpile script into standalone C99 source code |
| `unfish build [-o bin] <file>` | Compile script into native executable binary |
| `unfish fmt [-w] <file>` | Format source code to canonical Unfish style |
| `unfish debug <file>` | Launch interactive CLI debugger with breakpoints and stepping |
| `unfish check [--strict] <file>` | Validate syntax, semantics, and gradual type annotations |
| `unfish blocks-export <file>` | Convert source code to visual JSON block structure |
| `unfish blocks-import <file.json>` | Convert visual JSON block structure back to source code |
| `unfish lsp` | Start JSON-RPC 2.0 Language Server for VS Code & IDEs |
| `unfish repl` | Launch interactive read-eval-print loop session |
| `unfish ast <file>` | Print S-expression AST syntax tree |
| `unfish tokens <file>` | Print lexical tokens with line/column coordinates |

---

## 🚀 Quick Start

### Building from Source

Requirements: Any standard C99 compiler (`gcc` or `clang`) and standard `make`.

```bash
# Build bin/unfish
make

# Run an example
./bin/unfish run examples/hello.unfish

# Run on the Bytecode VM
./bin/unfish run --vm examples/hello.unfish

# Compile to a native binary and execute
./bin/unfish build -o bin/hello examples/hello.unfish
./bin/hello

# Launch the interactive REPL
./bin/unfish repl
```

### Running Tests & Benchmarks

```bash
# Run all unit tests, stress tests, and 51/51 differential tests
make test

# Run full test suite under AddressSanitizer and UndefinedBehaviorSanitizer
make test-asan

# Run multi-tier performance benchmarks comparing AST vs VM vs Native
make bench
```

---

## 📚 Technical Documentation

Exhaustive architectural specifications and design records are documented in [`docs/`](docs/):

- [`docs/VISION.md`](docs/VISION.md) — Pedagogical philosophy and progression.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — Decoupled pipeline architecture.
- [`docs/LANGUAGE_SPEC.md`](docs/LANGUAGE_SPEC.md) — Formal EBNF grammar, lexical conventions, and operator table.
- [`docs/TYPE_SYSTEM.md`](docs/TYPE_SYSTEM.md) — 4-tier gradual type system.
- [`docs/MEMORY_MODEL.md`](docs/MEMORY_MODEL.md) — Compilation arenas and mark-sweep GC.
- [`docs/RUNTIME.md`](docs/RUNTIME.md) — Environments (`UfEnv`), call frames, and error unwinding.
- [`docs/DECISIONS.md`](docs/DECISIONS.md) — Architectural Decision Records (ADRs 001 through 034).
- [`docs/PROJECT_STATE.md`](docs/PROJECT_STATE.md) — Living inventory of all language milestones and health metrics.
- [`CHANGELOG.md`](CHANGELOG.md) — Version history and roadmap releases.
- [`plan.md`](plan.md) — Complete roadmap and implementation milestones.

---

## 📄 License

MIT License. See file headers for details.
