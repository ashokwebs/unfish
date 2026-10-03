# UNFISH — DEVELOPER TOOLING, CLI ECOSYSTEM & LSP MANUAL

---

## 1. Executive Summary & Tooling Philosophy

Modern programming language adoption depends as much on developer tooling and ergonomics as on core language syntax. Unfish provides an integrated, first-class developer tooling suite built directly into the unified `unfish` CLI:

* **Zero External Tooling Dependencies**: The language server, code formatter, step debugger, profiler, package manager, and documentation generator are built directly into the single ANSI C99 binary.
* **Unified Developer Interface**: A single binary `unfish` provides commands across the entire software development lifecycle.
* **Multi-Platform Web Ecosystem**: Built-in HTTP services (`unfish studio`, `unfish playground`, `unfish learn`) bridge CLI workflows to browser-based visual programming and interactive education.

---

## 2. Complete CLI Reference Manual

The `unfish` CLI binary accepts 22 primary subcommands:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        UNIFIED CLI SUBCOMMANDS                         │
├────────────────────┬───────────────────────────────────────────────────┤
│ Category           │ Commands                                          │
├────────────────────┼───────────────────────────────────────────────────┤
│ Execution          │ `run`, `repl`                                     │
│ Validation         │ `check`, `tokens`, `ast`, `trace`                 │
│ Compilation        │ `compile`, `disasm`, `emit-c`, `build`            │
│ Developer Tools    │ `debug`, `format`, `lsp`, `profile`, `doc`        │
│ Package & Testing  │ `pkg`, `test`                                     │
│ Visual & Web       │ `blocks-export`, `blocks-import`, `studio`,       │
│                    │ `playground`, `learn`                             │
│ General            │ `version`                                         │
└────────────────────┴───────────────────────────────────────────────────┘
```

### 2.1. Program Execution: `unfish run`
```bash
unfish run [options] <file.unfish>
```
Executes an Unfish source script. If no execution flag is specified, the script is evaluated by the AST Tree-Walking Interpreter.

#### Flags:
* `--vm`: Execute script via the high-speed 59-opcode Stack Bytecode Virtual Machine.
* `--regvm`: Execute script via the 256-register computed-goto Bytecode Virtual Machine.
* `--wasm`: Compile to WebAssembly on the fly and execute via the host WASI runtime.
* `--strict`: Enforce strict type annotation consistency; any type mismatch aborts execution with exit code 2.
* `--profile`: Activate execution profiler, outputting function call counts, execution times, and memory allocations upon completion.
* `--debug`: Run with instruction tracing and visual stack dumps before each opcode.
* `--no-cache`: Bypass and do not update bytecode disk caches (`.ufc`, `.ufrc`).

### 2.2. Static Validation: `unfish check`
```bash
unfish check [--strict] <file.unfish>
```
Scans, parses, and semantically analyzes the script without executing it. Validates variable scope bindings, detects duplicate declarations, checks struct constructor arities, and validates trait bounds.
* Returns exit code `0` on clean verification.
* Returns exit code `1` on syntax/lexical error.
* Returns exit code `2` on semantic/type error.

### 2.3. Canonical Code Formatter: `unfish format`
```bash
unfish format [-i|--in-place] [--check] <file.unfish>
```
Formats source code according to the canonical Unfish style specification (`src/formatter/uf_formatter.c`):
* Enforces standard 4-space indentation.
* Normalizes whitespace around operators, colons, and commas.
* Preserves single-line comments and docstrings.
* `-i`, `--in-place`: Rewrites the target file in place.
* `--check`: Checks if file is formatted without modifying it. Returns exit code `0` if formatted, `1` if formatting is required (ideal for CI pipelines).

### 2.4. Interactive Step Debugger: `unfish debug`
```bash
unfish debug <file.unfish>
```
Launches an interactive command-line debugging session with breakpoints, stepping, variable inspection, and call stack tracing. (See [Chapter 18: Interactive Debugger](DEBUGGER.md)).

### 2.5. Visual Block Bridges: `blocks-export` & `blocks-import`
```bash
unfish blocks-export <file.unfish> [-o blocks.json]
unfish blocks-import <blocks.json> [-o file.unfish]
```
Bidirectionally serializes between Unfish AST and standardized visual block JSON format for visual drag-and-drop programming environments. (See [Chapter 19: Visual Blocks](BLOCKS.md)).

### 2.6. Compilation & Disassembly
* `unfish compile [--regvm] [-o out.ufc] <file.unfish>`: Compiles script into a binary bytecode chunk on disk.
* `unfish disasm <file.unfish>`: Disassembles the script and all nested function prototypes into human-readable bytecode instructions with source line annotations.
* `unfish emit-c [--embedded] [-o out.c] <file.unfish>`: Transpiles the script into standalone ANSI C99 source code.
* `unfish build [--wasm] [--embedded] [--arm] [-o binary] <file.unfish>`: Compiles the script into an optimized native machine code binary, WebAssembly module, or bare-metal ARM firmware.

### 2.7. Package Management: `unfish pkg`
```bash
unfish pkg <init|check|run|test|build>
```
* `init`: Scaffolds a new Unfish package with `unfish.toml`, `src/main.unfish`, and `tests/`.
* `check`: Runs semantic analysis across all package source files.
* `run`: Executes the entry point specified in `unfish.toml`.
* `test`: Discovers and executes all test suites in `tests/`.
* `build`: Compiles the entire package to a standalone native executable.

### 2.8. Test Discovery Runner: `unfish test`
```bash
unfish test [path] [--vm] [--regvm] [--strict] [--filter <pattern>]
```
Automatically discovers all `.unfish` test files in the specified directory or `tests/`. Evaluates tests, captures assertions, and emits standardized test reports with execution timings.

### 2.9. Documentation Generator: `unfish doc`
```bash
unfish doc [path] [-o output_dir] [--format md|html]
```
Extracts `##` docstrings from functions, structs, traits, and modules, generating clean Markdown or static HTML documentation with syntax highlighting.

### 2.10. Web Environments: `studio`, `learn`, `playground`
* `unfish studio [--port 8080]`: Launches the Unfish Studio IDE in the browser, featuring multi-file project exploration, live visual blocks, and interactive debugging.
* `unfish learn [--serve|--web] [lesson_id]`: Launches the interactive 22-chapter programming tutorial.
* `unfish playground [--port 8080]`: Starts an interactive live execution web server.

---

## 3. Language Server Protocol (LSP 3.17) Implementation (`src/lsp/uf_lsp.c`)

Unfish implements a full Language Server Protocol (LSP 3.17) server communicating over JSON-RPC 2.0 via standard I/O:

```bash
unfish lsp
```

```
┌────────────────────────┐      JSON-RPC 2.0 (stdio)      ┌────────────────────────┐
│ VS Code / Neovim / IDE │ ◄────────────────────────────► │   unfish lsp Server    │
│  Editor Client         │                                │  (src/lsp/uf_lsp.c)    │
└────────────────────────┘                                └───────────┬────────────┘
                                                                      │
                                     ┌────────────────────────────────┼────────────────────────────────┐
                                     ▼                                ▼                                ▼
                              ┌─────────────┐                  ┌─────────────┐                  ┌─────────────┐
                              │ Diagnostics │                  │ Completions │                  │ Definitions │
                              └─────────────┘                  └─────────────┘                  └─────────────┘
```

### 3.1. Supported LSP Methods
| LSP Method | Direction | Unfish Subsystem Integration |
|---|---|---|
| `initialize` | Client ➔ Server | Reports server capabilities and language features |
| `textDocument/didOpen` | Client ➔ Server | Ingests file content and runs initial diagnostics |
| `textDocument/didChange` | Client ➔ Server | Incrementally updates buffer, re-lexes and re-checks |
| `textDocument/publishDiagnostics` | Server ➔ Client | Pushes 2-D syntax and type errors directly to editor gutter |
| `textDocument/hover` | Client ➔ Server | Returns markdown hover info with type signature and docstring |
| `textDocument/completion` | Client ➔ Server | Context-aware auto-completion (keywords, symbols, methods) |
| `textDocument/formatting` | Client ➔ Server | Formats document in place via `uf_formatter` |
| `textDocument/definition` | Client ➔ Server | Jumps to source declaration of variable, function, or struct |
| `shutdown` / `exit` | Client ➔ Server | Cleanly terminates language server session |

### 3.2. VS Code Extension Integration
The Unfish repository includes a complete VS Code extension in `editors/vscode/`:
* Syntax grammar definition (`syntaxes/unfish.tmLanguage.json`).
* Language configuration (indentation rules, comment toggles, bracket matching).
* Extension client launching `unfish lsp` automatically when opening `.unfish` files.

---

## 4. Execution Profiler (`src/tooling/uf_profiler.c`)

When running with `unfish run --profile <file.unfish>`, the profiler intercepts function activations, measuring performance with microsecond precision:

```
================================================================================
                           UNFISH EXECUTION PROFILE
================================================================================
Function              Calls    Self Time (ms)    Total Time (ms)    Allocs (KB)
--------------------------------------------------------------------------------
fibonacci             121,393  142.34 ms (82%)   172.10 ms (100%)   0.00 KB
calculate_sum         10       12.18 ms (7%)     12.18 ms (7%)      64.00 KB
format_output         1        0.42 ms (0.2%)    0.42 ms (0.2%)     12.50 KB
<main>                1        17.16 ms (10%)    172.10 ms (100%)   8.20 KB
--------------------------------------------------------------------------------
Total Execution Time: 172.10 ms | Total Memory Allocated: 84.70 KB
================================================================================
```

* **Self Time**: Execution time spent directly inside the function body, excluding sub-calls.
* **Total Time**: Cumulative execution time from function entry to exit.
* **Allocs**: Memory volume allocated on the GC heap by the function.
