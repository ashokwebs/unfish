# UNFISH — TOOLING & CLI SPECIFICATION

---

## 1. Unified Command Line Interface (`unfish`)

The `unfish` CLI is the primary developer interface for running, inspecting, checking, and testing Unfish programs.

### 1.1. Subcommands Overview

```bash
# Execute a program (Interpreter by default, or with --vm)
unfish run <file.unfish> [--vm] [--strict]

# Validate syntax and perform semantic analysis without executing
unfish check <file.unfish>

# Dump the parsed Abstract Syntax Tree for inspection
unfish ast <file.unfish>

# Scan and print lexical token stream with source spans
unfish tokens <file.unfish>

# Launch the interactive Read-Eval-Print-Loop
unfish repl

# Compile bytecode and disassemble instruction stream
unfish disasm <file.unfish>

# Compile Unfish source directly to standalone native C99 executable
unfish build -o <output_binary> <file.unfish>

# Run first-class test runner on files or directories
unfish test [path] [--vm] [--strict] [-v|--verbose] [--filter <pattern>]

# Generate documentation from ## docstrings (Markdown or HTML)
unfish doc <path> [-f|--format markdown|html] [-o <outfile>] [--title <title>]

# Manage packages and dependencies with unfish.toml
unfish pkg init [name]
unfish pkg check
unfish pkg run [args...]
unfish pkg test
unfish pkg build [output_binary]

# Interactive hands-on tutorial
unfish learn [--list | <lesson_number>]

# Format source code cleanly
unfish format <file.unfish> [--in-place] [--check]

# Launch Language Server Protocol daemon
unfish lsp

# Print version and build metadata
unfish version
```

### 1.2. CLI Exit Codes
* `0`: Success (program executed, checks passed, or tests clean).
* `1`: Syntax / Lexical error.
* `2`: Semantic analysis error.
* `3`: Runtime error.
* `4`: Internal compiler/runtime fault.
* `64`: Command-line usage error.

---

## 2. First-Class Test Runner (`unfish test`)

Unfish includes an integrated test runner supporting automated discovery, header annotations, process isolation, and timing statistics:

```bash
unfish test tests/conformance/
unfish test tests/conformance/ --filter hello -v
unfish test --vm tests/conformance/
```

### Supported Directives
Test files can declare expectations in header comments:
* `# expect: <line>`: Specifies an expected line of standard output.
* `# expect-error: <line>`: Specifies an expected error string on standard error.
* `# expect-exit: <code>`: Specifies expected exit code (e.g. `2` or `3`).
* `# flags: --strict --vm`: Passes execution flags to the invocation.

---

## 3. Documentation Generator (`unfish doc`)

The `doc` tool extracts doc comments starting with `##` from module headers, structs, methods, enums, and functions:

```bash
# Generate Markdown documentation to stdout or file
unfish doc src/stdlib/testing.unfish
unfish doc src/stdlib/testing.unfish -o docs/api_testing.md

# Generate responsive HTML documentation with dark theme
unfish doc src/stdlib/testing.unfish -f html -o docs/api_testing.html
```

---

## 4. Package Manager (`unfish pkg`)

Projects are managed using a declarative `unfish.toml` manifest:

```toml
[package]
name = "my_app"
version = "0.1.0"
entry = "src/main.unfish"

[dependencies]
```

### Commands:
* `unfish pkg init [name]`: Scaffolds `unfish.toml`, `src/main.unfish`, and `tests/main_test.unfish`.
* `unfish pkg check`: Verifies manifest integrity and entry point existence.
* `unfish pkg run [args...]`: Executes the package entry point with optional CLI arguments.
* `unfish pkg test`: Discovers and executes all tests in `tests/`.
* `unfish pkg build [output]`: Compiles the package to a native C99 binary in `bin/`.

---

## 5. Interactive Tutorial (`unfish learn`)

Developers can learn Unfish interactively directly in the terminal:

```bash
unfish learn --list        # Display curriculum of 10 progressive lessons
unfish learn               # Start interactive lesson 1
unfish learn 7             # Jump directly to lesson 7 (Structs & Methods)
```

Interactive commands during lessons:
* `:hint`: Display hint for current challenge.
* `:solution`: Reveal solution code.
* `:next` / `:prev`: Navigate between lessons.
* `:list`: Show curriculum progress.
* `:quit`: Exit tutorial.

---

## 6. Interactive REPL
The REPL runs the compiler and interpreter pipeline:
* Retains persistent global environment across evaluations.
* Evaluates expressions directly (e.g. typing `2 + 3` outputs `5`).
* Executes declarations and function definitions interactively.
* Automatically handles multiline block input when a colon `:` terminates a line.

---

## 7. Language Server Protocol (LSP)
The `unfish lsp` command implements JSON-RPC LSP protocol over stdin/stdout:
* Diagnostics on file open and change.
* Hover type information.
* Go to definition for symbols.
* Autocompletion of keywords and symbols.

---

## 8. Interactive Web Playground (`unfish playground`)

Unfish includes a zero-dependency, self-contained local Web Playground:

```bash
# Launch playground server and open browser
unfish playground

# Launch on custom port without opening browser
unfish playground --port 8080 --no-open
```

### Features:
* **Embedded HTTP Server**: Serves static frontend assets (`web/`) with zero external runtime dependencies.
* **Dual Execution Modes**:
  * **Native CLI Backend**: Seamlessly forwards code to `unfish run` (AST Interpreter) and `unfish run --vm` (Bytecode VM) via `/api/run` and `/api/run-vm`.
  * **Standalone Browser Engine**: Includes an offline in-browser fallback runtime (`web/unfish_engine.js`) when disconnected from the CLI server.
* **Visual Blocks Visualizer**: Converts code to `unfish_blocks_v1` JSON structure and renders interactive Scratch/Blockly-style blocks.
* **AST Inspector**: Interactive Abstract Syntax Tree viewer with collapsible syntax nodes.
* **Example Suite**: Includes ready-to-run examples for string interpolation, pattern matching, structs & methods, destructuring, higher-order functions, recursion, and algorithms.


