# UNFISH — TOOLING & CLI SPECIFICATION

---

## 1. Unified Command Line Interface (`unfish`)

The `unfish` CLI is the primary developer interface for running, inspecting, checking, and testing Unfish programs.

### 1.1. Subcommands

```bash
# Execute a program
unfish run <file.unfish>

# Validate syntax and perform semantic analysis without executing
unfish check <file.unfish>

# Dump the parsed Abstract Syntax Tree for inspection
unfish ast <file.unfish>

# Scan and print lexical token stream with source spans
unfish tokens <file.unfish>

# Launch the interactive Read-Eval-Print-Loop
unfish repl

# Run test suites
unfish test [dir]

# Format source code cleanly
unfish format <file.unfish>

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

## 2. Interactive REPL

The REPL runs the exact same compiler and interpreter pipeline as script execution:
* Retains persistent global environment across line evaluations.
* Evaluates expressions directly (e.g. typing `2 + 3` outputs `5`).
* Executes declarations and function definitions interactively.
* Automatically handles multiline block input when a colon `:` terminates a line.

## 3. Language Server Protocol (LSP) Roadmap
Phase 6–8 will introduce `unfish lsp` supporting standard editor features:
* Real-time diagnostics on save / keystroke.
* Hover information with type signatures and documentation.
* Go to definition for variables and functions.
* Autocompletion of identifiers and keywords.
