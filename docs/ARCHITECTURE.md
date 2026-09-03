# UNFISH — SYSTEM ARCHITECTURE

---

## 1. High-Level Architectural Pipeline

The core Unfish system is designed as a clean, decoupled series of stages. Each stage communicates strictly through well-defined data structures with zero global state.

```
       Visual Blocks (Frontend)        Source Code (.unfish)
                  │                              │
                  ▼                              ▼
          [ Block Reader ]                [ Lexical Scanner ]
                  │                              │
                  │                              ▼
                  │                        Token Stream (with Spans)
                  │                              │
                  ▼                              ▼
             AST Builder ──────────────► [ Pratt / RD Parser ]
                                                 │
                                                 ▼
                                     Abstract Syntax Tree (AST)
                                                 │
                                                 ▼
                                     [ Semantic Analyzer ]
                                                 │
                                   ┌─────────────┴─────────────┐
                                   ▼                           ▼
                        [ Tree-Walk Evaluator ]       [ Bytecode Compiler ]
                                   │                           │
                                   ▼                           ▼
                            Runtime Values               Bytecode Chunk
                            Environments                       │
                              (Heap/GC)                        ▼
                                   │                     [ Unfish VM ]
                                   ▼                           │
                                Output /                Operand Stack
                             Visual Events               Call Frames
```

---

## 2. Subsystem Descriptions

### 2.1. Common Core (`src/common/`)
Houses fundamental data types shared across the compiler and runtime:
* **Source Location & Spans** (`uf_source.h`): Tracks `file_name`, `line`, `column`, and `offset` for both tokens and AST nodes. Enables high-resolution error reporting and debugger stepping.
* **Arena Allocator** (`uf_arena.h`): Fast, contiguous memory pool allocator for AST nodes, tokens, and temporary compiler strings. Simplifies memory management by freeing entire parse stages in O(1) time.
* **Dynamic Arrays & Hash Tables** (`uf_array.h`, `uf_map.h`): Type-safe, reusable data structures in C.
* **String Interner** (`uf_intern.h`): Ensures identifier strings are interned for O(1) equality comparisons during parsing and symbol lookup.
* **Diagnostics Engine** (`uf_diagnostic.h`): Renders rich terminal diagnostics with source snippets, colored carets, error classifications, and remediation hints.

### 2.2. Lexer (`src/lexer/`)
Converts raw UTF-8 source text into a stream of tokens:
* Handles indentation off-side rules with an internal indentation stack, emitting `INDENT`, `DEDENT`, and `NEWLINE` tokens.
* Detects malformed numeric literals, unterminated strings, and invalid escape sequences with immediate source-accurate diagnostics.
* Retains full source coordinate spans for every token.

### 2.3. Abstract Syntax Tree (`src/ast/`)
Represents the program's structural semantics:
* Distinct node categories: Declarations, Statements, Expressions.
* Every node carries a `SourceSpan`.
* Tree nodes are immutable once constructed and allocated via the AST arena.
* Includes an AST Formatter/Printer for human-readable tree inspection.

### 2.4. Parser (`src/parser/`)
Translates tokens into the AST:
* Uses a **Pratt Parser** (Top-Down Operator Precedence) for expressions to cleanly handle binary operator precedence, associativity, prefix operators, and call expressions without recursion bloat.
* Uses **Recursive Descent** for statements and declarations.
* Indentation tokens (`INDENT` / `DEDENT`) govern block boundaries, eliminating ambiguous braces while maintaining strict hierarchy.

### 2.5. Semantic Analysis (`src/semantic/`)
Validates program semantics prior to execution:
* Constructs symbol tables and tracks lexical scoping (`global`, `function`, `block`).
* Detects undeclared variables with "did you mean?" fuzzy matching.
* Enforces single declaration per scope for `let`.
* Validates function arity and parameter uniqueness.
* Ensures `return` statements only occur inside function bodies.

### 2.6. Runtime & Values (`src/runtime/`)
Manages execution state and data:
* **Value Representation** (`uf_value.h`): Tagged union supporting `Null`, `Boolean`, `Number` (double), `String` (heap-allocated reference), `Function` (AST pointer + captured environment), and `NativeFunction` (C function pointer).
* **Environments** (`uf_env.h`): Lexical scope frames maintaining variable bindings with parent links.
* **Call Stack** (`uf_stack.h`): Tracks active stack frames for backtraces and debugger inspection.

### 2.7. Evaluator / Interpreter (`src/interpreter/`)
Executes the validated AST:
* Implements the tree-walking evaluation loop.
* Intercepts execution events (statement step, expression eval, function enter/exit) via an observer interface for educational visualizers and debuggers.

### 2.8. CLI & REPL (`src/cli/`)
User-facing entry point:
* Provides commands: `unfish run`, `unfish check`, `unfish ast`, `unfish tokens`, `unfish repl`.
* Implements REPL with continuous environment persistence.

---

## 3. Host Isolation & Memory Safety Principles

1. **Explicit Ownership**: Memory allocation is tracked. AST memory is arena-allocated and released en masse. Runtime heap objects are managed by the runtime environment.
2. **Zero Undefined Behavior**: Strict compilation under `-Wall -Wextra -Werror -pedantic -fsanitize=address,undefined`.
3. **No Unrestricted Host Access**: File operations, networking, and system calls are strictly mediated through sandboxed standard library layers.
