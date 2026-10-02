# UNFISH — VISUAL BLOCK PROGRAMMING & BIDIRECTIONAL AST SPECIFICATION

---

## 1. Executive Summary & Pedagogical Mission

Visual block-based environments (like Scratch and Blockly) excel at introducing young learners to programming by eliminating syntax errors and punctuation anxiety. However, traditional block languages are architectural dead ends: they lack real data structures, lexical closures, recursion with hoisting, and systems concepts.

Unfish solves this fundamental divide through **Bidirectional AST Round-Tripping**:

```
┌────────────────────────┐      unfish blocks-export       ┌────────────────────────┐
│   Unfish Source Text   │ ──────────────────────────────► │  Visual Block JSON AST │
│       (*.unfish)       │ ◄────────────────────────────── │     (*.blocks.json)    │
└────────────────────────┘      unfish blocks-import       └────────────────────────┘
            │                                                           │
            ▼                                                           ▼
┌────────────────────────┐                                 ┌────────────────────────┐
│   Text Editor / LSP    │       Live Bidirectional Sync   │   Visual Block Canvas  │
│      (Unfish Studio)   │ ◄─────────────────────────────► │      (Unfish Studio)   │
└────────────────────────┘                                 └────────────────────────┘
```

1. **Zero Semantic Degradation**: Blocks are not an abstracted subset of Unfish. Every valid Unfish text construct (including pattern matching, structs, traits, closures, and fibers) maps to a visual block representation.
2. **Instant Bidirectional Synchronization**: In the Unfish Studio IDE (`web/studio.html`), typing text immediately renders corresponding visual blocks; dragging or configuring visual blocks immediately updates the source text in the editor.
3. **The Unbroken Ascent**: Learners begin by dragging visual blocks, observe the synchronized textual code, begin editing text directly, and eventually graduate into pure text without changing languages.

---

## 2. Bidirectional Round-Trip Invariant

The core integrity contract of the block subsystem is formal **Round-Trip Fidelity**:

$$\text{AST}(\text{Source}) \equiv \text{AST}(\text{Import}(\text{Export}(\text{AST}(\text{Source}))))$$

Any valid Unfish source file, when exported to JSON blocks and re-imported back to text, produces an Abstract Syntax Tree that is structurally identical to the original AST and generates byte-for-byte identical output across all five runtime engines.

---

## 3. JSON Block Schema Specification (`src/blocks/uf_blocks.h`)

Visual blocks are serialized in standardized JSON matching modern block editors (Blockly / Scratch):

### 3.1. Top-Level Program Schema
```json
{
  "unfish_version": "2.1.0",
  "blocks": [
    {
      "id": "block_001",
      "type": "stmt_let",
      "fields": {
        "NAME": "total"
      },
      "inputs": {
        "VALUE": {
          "type": "expr_literal_number",
          "fields": { "VALUE": 0 }
        }
      },
      "next": {
        "id": "block_002",
        "type": "stmt_for_in",
        "fields": {
          "VARIABLE": "num"
        },
        "inputs": {
          "ITERABLE": {
            "type": "expr_call",
            "fields": { "FUNCTION": "range" },
            "inputs": {
              "ARG_0": { "type": "expr_literal_number", "fields": { "VALUE": 10 } }
            }
          }
        },
        "statements": {
          "BODY": [
            {
              "id": "block_003",
              "type": "stmt_assign",
              "fields": { "TARGET": "total", "OPERATOR": "+=" },
              "inputs": {
                "VALUE": { "type": "expr_variable", "fields": { "NAME": "num" } }
              }
            }
          ]
        },
        "next": {
          "id": "block_004",
          "type": "stmt_say",
          "inputs": {
            "EXPR": { "type": "expr_variable", "fields": { "NAME": "total" } }
          }
        }
      }
    }
  ]
}
```

### 3.2. Core Block Types
| Block Category | JSON `type` Identifier | Corresponds To Unfish Syntax |
|---|---|---|
| **Variables** | `stmt_let`, `stmt_assign`, `expr_variable` | `let x = ...`, `x += ...`, `x` |
| **Output** | `stmt_say`, `stmt_print` | `say ...`, `print(...)` |
| **Logic** | `stmt_if`, `expr_logical`, `expr_compare` | `if/elif/else`, `and/or`, `==, !=, <, >` |
| **Loops** | `stmt_while`, `stmt_for_in`, `stmt_repeat` | `while ...:`, `for x in arr:`, `repeat n:` |
| **Functions** | `decl_function`, `stmt_return`, `expr_call` | `function name(args):`, `return`, `f(x)` |
| **Collections** | `expr_array`, `expr_map`, `expr_index` | `[a, b]`, `{k: v}`, `arr[i]` |
| **Patterns** | `stmt_match`, `pattern_arm` | `match x: case => ...` |
| **Errors** | `stmt_try_catch`, `stmt_raise` | `try / catch / finally`, `raise ...` |

---

## 4. CLI Tools: `blocks-export` and `blocks-import`

### Exporting Source Code to Visual Blocks
```bash
unfish blocks-export program.unfish -o program.blocks.json
```
1. Parses `program.unfish` into a strongly typed AST.
2. Traverses the AST in `src/blocks/uf_blocks_export.c`, emitting formatted, schema-compliant JSON blocks.

### Importing Visual Blocks to Source Code
```bash
unfish blocks-import program.blocks.json -o program.unfish
```
1. Parses the JSON block tree in `src/blocks/uf_blocks_import.c`.
2. Validates block connectors, field names, and statement sequences.
3. Pretty-prints idiomatic, canonically formatted Unfish text using `uf_formatter`.

---

## 5. Unfish Studio Visual Integration (`web/studio.html`)

In the Unfish Studio IDE:
* The left panel hosts the **Block Palette**, organized by color-coded categories:
  * 🟣 Variables & State (`let`, assignment)
  * 🔵 Control Flow & Logic (`if`, `while`, `for`, `repeat`)
  * 🟢 Functions & Closures (`function`, `return`)
  * 🟡 Collections (`array`, `map`, indexing)
  * 🔴 Systems & Buffers (`buffer`, `read_u16_le`)
* As the user drags blocks onto the workspace canvas, the code editor updates live.
* Errors in the textual code are projected onto the visual canvas as glowing warning badges on the invalid block, preserving a unified mental model.
