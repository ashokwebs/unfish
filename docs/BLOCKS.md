# UNFISH — VISUAL BLOCK SPECIFICATION & ROUND-TRIPPING

---

## 1. Non-Negotiable Principle: The Language Comes First

Unfish is not a block-based toy language that exports code snippets. The formal language specification and the Abstract Syntax Tree (AST) are the ground truth.

```
       Blocks (Visual UI)
               │
               ▼ (1:1 Schema Mapping)
              AST ◄────────────► Text Source
               │
               ▼
       Semantic Analysis
               │
               ▼
            Runtime
```

Visual blocks are strictly a projection of the AST onto an interactive visual surface.

## 2. Block-to-AST Mapping

Every visual block corresponds directly to an AST node kind:

| Visual Block | AST Node Kind | Text Representation |
|---|---|---|
| Say Block | `UF_STMT_SAY` | `say <expr>` |
| Let Variable Block | `UF_STMT_LET` | `let <id> = <expr>` |
| Set Variable Block | `UF_STMT_ASSIGN` | `<id> = <expr>` |
| If / Else Block | `UF_STMT_IF` | `if <cond>:\n    ...` |
| Repeat Block | `UF_STMT_REPEAT` | `repeat <n> times:\n    ...` |
| While Block | `UF_STMT_WHILE` | `while <cond>:\n    ...` |
| Function Define Block | `UF_STMT_FUNCTION` | `function <name>(<params>):\n    ...` |
| Return Block | `UF_STMT_RETURN` | `return <expr>` |
| Math / Logic Op Block | `UF_EXPR_BINARY` | `<left> <op> <right>` |
| Call Block | `UF_EXPR_CALL` | `<fn>(<args>)` |

## 3. Lossless Round-Tripping (Phase 5 Roadmap)

The round-tripping property requires that:
1. Converting valid visual blocks to an AST, then formatting to text, then parsing back to an AST yields an identical, isomorphic AST:
   $$\text{AST}(\text{Text}(\text{AST}(\text{Blocks}))) \equiv \text{AST}(\text{Blocks})$$
2. Converting valid textual Unfish code to an AST and exporting to visual blocks preserves all structural logic, variable names, and control flow.

## 4. Round-Trip Testing Harness
Automated tests in Phase 5 will parse canonical test programs, export their AST to JSON block representations, re-import the blocks to AST, re-serialize to text, and verify byte-for-byte semantic parity.
