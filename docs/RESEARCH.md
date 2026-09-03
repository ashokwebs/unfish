# UNFISH — COMPARATIVE LANGUAGE RESEARCH

---

## 1. Analysis of Existing Languages

| Language | Strengths | Weaknesses for Our Mission | Unfish Differentiation |
|---|---|---|---|
| **Python** | Readable, popular, large ecosystem | Hidden magic, global interpreter lock, complex C API, opaque runtime details for systems education | Explicit memory model, visual block parity, transparent runtime architecture |
| **Lua** | Minimal C runtime, fast, clean design | 1-indexed arrays, lack of formal block structure for visual programming, limited type safety | 0-indexed, indentation-driven, structured error diagnostics, explicit educational hooks |
| **Scheme / Lisp**| Elegant, minimal syntax, homoiconic | Prefix syntax alienates beginners; parenthesis matching is difficult for visual blocks | Indentation-based block syntax that maps naturally to visual block nesting |
| **Scratch** | Great visual UI, highly engaging for children | Toy DSL, lacks textual syntax, impossible to scale to algorithms or systems programming | Real programming language where blocks are merely one frontend to the AST |
| **Rust** | Memory safety, zero-cost abstractions | Prohibitive learning curve for beginners; borrow checker overwhelms novices | Gradual progression: start dynamic, introduce lexical scopes, then move to systems concepts |

## 2. Key Synthesis

Unfish synthesizes the syntactic clarity of Python, the embeddable C runtime simplicity of Lua, the block-friendly visual mapping of Scratch, and the long-term systems inspectability of C/Rust.
