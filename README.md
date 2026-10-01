# 🐡 Unfish

**Unfish** is a real, general-purpose programming language, runtime, compiler, and educational computing environment written in clean ANSI C99 with zero external dependencies.

It is designed to provide an unbroken path of intellectual ascent: from visual block programming, into clean indentation-based text programming, to algorithms, data structures, runtime architecture, garbage collection, bytecode virtual machines, and low-level systems programming.

> *"Hide complexity until the learner is ready — never hide it forever."*
> 

---

## ⚡ Key Highlights & Architecture

Unfish features a unique **five-backend execution engine**, kept in lockstep by a differential test suite that requires byte-identical output from every one of them:
1. **Tree-Walking AST Interpreter**: Fast startup, direct syntax tree interpretation, ideal for interactive exploration, REPL, and visual blocks.
2. **Bytecode Virtual Machine (`--vm`)**: 57-opcode stack-based VM with lexical upvalue capture cells, exception unwinding, and a 2x–6x execution speedup over AST tree-walking with 100% behavioral differential parity.
3. **Register-Based Bytecode VM (`--regvm`)**: 256-register, 3-address VM with computed-goto dispatch, typically the fastest of the two interpreted VM tiers.
4. **Native C99 AOT Compiler (`build` / `emit-c`)**: Direct translation to standalone C99 with single-header runtime, measured at 25–60× faster than the interpreter with zero interpreter dependencies.
5. **WebAssembly Backend (`build --wasm`)**: The same program compiled to run in a browser or under Node.

### Additional Highlights
- **Fibers & Channels**: `spawn`, `yield`, `channel`, `send`, `recv`, `close_channel`, `run_scheduler`. Note the current scheduling semantics: `run_scheduler` runs each spawned fiber to completion in spawn order, and `yield` marks a scheduling point without suspending the fiber, so fibers do not interleave yet. Channels are FIFO queues that carry values between fibers and across the scheduler; the capacity argument is a hint and is not enforced as backpressure, so `send` never blocks. Treat them as synchronous generators plus a queue rather than as preemptive coroutines.
- **Systems Programming**: Contiguous byte buffers (`buffer`, `buffer_get`, `buffer_set`), multi-byte little-endian access (`u16`, `u32`, `i32`), and memory layout introspection (`inspect`).
- **Modern Developer Ergonomics**: Built-in Language Server Protocol (`unfish lsp`) with VS Code extension, interactive CLI debugger (`unfish debug`), and canonical code formatter (`unfish format`).
- **Bidirectional Block Round-Tripping**: Export AST to visual JSON blocks (`unfish blocks-export`) and import back into executable Unfish (`unfish blocks-import`).
- **Rich Standard Library**: Built-in modules for `sys`, `fs`, `random`, `time`, `json`, and `testing`.

---

## 🛠️ CLI Reference

The unified `unfish` CLI offers commands across the entire software development lifecycle:

| Command | Description |
|---|---|
| `unfish run <file>` | Run script via tree-walking AST interpreter |
| `unfish run --vm <file>` | Run script via high-speed Bytecode Virtual Machine |
| `unfish run --regvm <file>` | Run script via the register-based Bytecode VM |
| `unfish run --vm --debug <file>` | Run script with VM instruction tracer and visual stack dumps |
| `unfish compile <file>` | Compile script into bytecode chunk and disassemble |
| `unfish disasm <file>` | Disassemble script into human-readable bytecode instructions |
| `unfish emit-c [-o out.c] <file>` | Transpile script into standalone C99 source code |
| `unfish build [-o bin] <file>` | Compile script into native executable binary |
| `unfish build --wasm <file>` | Compile script into a WebAssembly module |
| `unfish format [-i] [--check] <file>` | Format source code to canonical Unfish style (`-i` rewrites in place) |
| `unfish debug <file>` | Launch interactive CLI debugger with breakpoints and stepping |
| `unfish check [--strict] <file>` | Validate syntax, semantics, and gradual type annotations |
| `unfish blocks-export <file>` | Convert source code to visual JSON block structure |
| `unfish blocks-import <file.json>` | Convert visual JSON block structure back to source code |
| `unfish lsp` | Start JSON-RPC 2.0 Language Server for VS Code & IDEs |
| `unfish repl` | Launch interactive read-eval-print loop session |
| `unfish ast <file>` | Print S-expression AST syntax tree |
| `unfish tokens <file>` | Print lexical tokens with line/column coordinates |
| `unfish trace <file>` | Emit a JSON execution trace |
| `unfish test [path] [--filter <pat>]` | Discover and run `.unfish` test suites |
| `unfish doc [path] [-o <out>] [--format md\|html]` | Generate API documentation from `##` docstrings |
| `unfish pkg <init\|check\|run\|test\|build>` | Manage Unfish packages via `unfish.toml` |
| `unfish studio [--port <p>]` | Launch Unfish Studio browser IDE with multi-file workspace, visual blocks, and debugging |
| `unfish learn [--serve\|--web] [id]` | Launch interactive tutorial (CLI lesson or web documentation) |
| `unfish playground [--port <p>]` | Launch the browser playground and visual block editor |
| `unfish version` | Display version and build information |

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
# Run all unit tests, stress tests, and 91/91 differential tests
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

---

## 📄 License

MIT License. See [LICENSE](LICENSE) for details.
