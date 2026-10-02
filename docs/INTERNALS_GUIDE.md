# Volume IX: The Unfish Hacker's Guide to Compiler & VM Internals

> **Document Status**: Production Complete • **Specification Level**: Core Engine & Compiler Hacking  
> **Target Audience**: Compiler Writers, Language Implementers, Runtime Hackers, Contributors  
> **Related Manuals**: [ARCHITECTURE.md](ARCHITECTURE.md) • [COMPILER.md](COMPILER.md) • [VM.md](VM.md) • [TESTING.md](TESTING.md)

---

## Table of Contents

1. [Architectural Overview & Subsystem Map](#1-architectural-overview--subsystem-map)
2. [Memory Management: Arenas & Garbage Collection](#2-memory-management-arenas--garbage-collection)
3. [Lexical Analysis & Indentation Synthesis](#3-lexical-analysis--indentation-synthesis)
4. [Pratt Parsing: Top-Down Operator Precedence](#4-pratt-parsing-top-down-operator-precedence)
5. [Semantic Analysis, Scope Hoisting & Upvalue Resolution](#5-semantic-analysis-scope-hoisting--upvalue-resolution)
6. [Dual Bytecode Compilers (Stack vs Register)](#6-dual-bytecode-compilers-stack-vs-register)
7. [Dual Execution Engines (Stack VM vs 256-Register VM)](#7-dual-execution-engines-stack-vm-vs-256-register-vm)
8. [The Native C99 AOT Code Generator](#8-the-native-c99-aot-code-generator)
9. [5-Way Differential Parity Harness](#9-5-way-differential-parity-harness)
10. [Tutorial: Adding a New Language Feature Step-by-Step](#10-tutorial-adding-a-new-language-feature-step-by-step)

---

## 1. Architectural Overview & Subsystem Map

The Unfish engine is implemented in strictly compliant ANSI C99 with **zero external dependencies**.

```
Source Code (.unfish)
        |
        v
 [ uf_lexer.c ]  --> Indentation stack synthesizes INDENT / DEDENT / NEWLINE tokens
        |
        v
 [ uf_parser.c ] --> Top-Down Operator Precedence (Pratt) AST construction
        |
        v
 [ uf_semantic.c ] -> Scope resolution, hoisting, type annotation checking
        |
        +-----------------------+-----------------------+-----------------------+
        |                       |                       |                       |
        v                       v                       v                       v
 [ uf_compiler.c ]      [ uf_reg_compiler.c ]   [ uf_emit_c.c ]        [ uf_interpreter.c ]
  Stack Bytecode         Register Bytecode       Standalone C99         Tree-Walk AST Eval
        |                       |                       |                       |
        v                       v                       v                       |
 [ uf_vm.c ]             [ uf_regvm.c ]          [ gcc / clang ]                |
  57-Opcode Stack VM      256-Reg 3-Address VM    Native Machine Code           |
        |                       |                       |                       |
        +-----------------------+-----------------------+-----------------------+
                                |
                                v
                Differential Conformance Verification
                   (100% Differential Parity)
```

### Directory Anatomy

| Subsystem Path | Primary Responsibilities | Key C Headers |
|---|---|---|
| `src/common/` | Memory arena, dynamic strings, diagnostics | `uf_arena.h`, `uf_string.h`, `uf_diagnostic.h` |
| `src/lexer/` | Hand-crafted scanner, indentation stack, tokens | `uf_token.h`, `uf_lexer.h` |
| `src/ast/` | AST node structures and tree allocation | `uf_ast.h` |
| `src/parser/` | Pratt expression parser, statement grammar | `uf_parser.h` |
| `src/semantic/` | Symbol tables, variable hoisting, type checking | `uf_semantic.h` |
| `src/compiler/` | Stack bytecode emitter, peephole optimizer | `uf_chunk.h`, `uf_compiler.h`, `uf_optimize.h` |
| `src/vm/` | 57-opcode Stack Virtual Machine | `uf_vm.h`, `uf_disasm.h` |
| `src/compiler/uf_reg_compiler.c` | Register bytecode emitter (3-address format) | `uf_reg_compiler.h` |
| `src/vm2/` | 256-register Virtual Machine | `uf_regvm.h` |
| `src/codegen/` | Standalone ANSI C99 transpiler | `uf_emit_c.h` |
| `src/runtime/` | Object layouts, GC mark-sweep, fiber scheduler | `uf_value.h`, `uf_runtime.h`, `uf_fiber.h` |
| `src/stdlib/` | Modules: `sys`, `fs`, `time`, `random`, `json`, `testing` | `uf_stdlib.h`, `uf_module.h` |
| `src/tooling/` | CLI test runner, doc generator, package manager, learn | `uf_test_runner.h`, `uf_doc.h`, `uf_learn.h` |
| `src/debugger/` | Interactive step debugger and trace emitter | `uf_debugger.h` |
| `src/lsp/` | Language Server Protocol JSON-RPC daemon | `uf_lsp.h` |

---

## 2. Memory Management: Arenas & Garbage Collection

Unfish separates compilation memory from runtime memory:

1. **Compilation Phase (`UfArena`)**:
   - The lexer, parser, and semantic analyzer allocate AST nodes, tokens, and symbol tables inside contiguous arena memory blocks.
   - AST nodes have **zero per-node free overhead**.
   - Upon compilation completion, `uf_arena_free()` reclaims all compiler memory in a single $O(1)$ release.

2. **Runtime Execution Phase (Tri-Color Mark-Sweep GC)**:
   - All runtime objects (`UfStringObject`, `UfArrayObject`, `UfMapObject`, `UfStructObject`, `UfBufferObject`, `UfFiber`) share a common header:
     ```c
     typedef struct UfObj {
         UfObjKind kind;
         bool marked;
         size_t size;
         struct UfObj* next;
     } UfObj;
     ```
   - **Root Scanning**: Stack frames, global environment, fiber stacks, and temporary roots are traversed recursively.
   - **Sweep Phase**: All unreached objects are freed via `free()` and unlinked from `rt->all_objects`.
   - **Dynamic Thresholding**: GC triggers when `bytes_allocated > next_gc_threshold` (growing by a factor of 1.75x).

---

## 3. Lexical Analysis & Indentation Synthesis

Unfish uses an indentation-sensitive Pythonic syntax without requiring curly braces.

### The Indentation Stack

`uf_lexer.c` maintains an internal indentation column stack:

```c
#define UF_MAX_INDENT_LEVELS 256
typedef struct {
    int levels[UF_MAX_INDENT_LEVELS];
    int top;
} UfIndentStack;
```

When beginning a new logical line:
1. Count leading spaces (tabs trigger an error or are expanded to 4 spaces).
2. If `current_indent > indent_stack[top]`:
   - Push `current_indent`.
   - Emit synthetic `UF_TOKEN_INDENT`.
3. If `current_indent < indent_stack[top]`:
   - Pop levels until matching an earlier indentation.
   - Emit one synthetic `UF_TOKEN_DEDENT` per popped level.
   - If indentation matches no previous level, emit a syntax diagnostic error (`IndentationError: unindent does not match any outer indentation level`).

### String Interpolation Lexing

Strings prefixed with `f"..."` are split by the scanner into alternating tokens:
- `UF_TOKEN_STRING` for raw text fragments.
- Expressions inside `{...}` are lexed as standard expression tokens enclosed in synthetic concatenation nodes.

---

## 4. Pratt Parsing: Top-Down Operator Precedence

Expression parsing in `uf_parser.c` is implemented using Vaughan Pratt's algorithm, avoiding complex recursive descent grammar ambiguities.

### Binding Power Table

| Precedence Level | Operators | Associativity |
|---|---|---|
| `PREC_ASSIGN` (1) | `=`, `+=`, `-=`, `*=`, `/=` | Right |
| `PREC_PIPE` (2) | `\|>` | Left |
| `PREC_LOGICAL_OR` (3) | `or` | Left |
| `PREC_LOGICAL_AND` (4) | `and` | Left |
| `PREC_EQUALITY` (5) | `==`, `!=` | Left |
| `PREC_COMPARISON` (6) | `<`, `<=`, `>`, `>=` | Left |
| `PREC_TERM` (7) | `+`, `-` | Left |
| `PREC_FACTOR` (8) | `*`, `/`, `%` | Left |
| `PREC_UNARY` (9) | `not`, `-`, `~` | Right |
| `PREC_CALL` (10) | `(args)`, `[index]`, `.property` | Left |

### Pratt Dispatch Function

```c
static UfExpr* parse_precedence(UfParser* p, UfPrecedence prec) {
    p->prev = p->curr;
    p->curr = uf_lexer_next_token(p->lexer);

    ParsePrefixFn prefix_rule = get_rule(p->prev.kind)->prefix;
    if (!prefix_rule) {
        uf_parser_error(p, "Expected expression");
        return NULL;
    }

    UfExpr* left = prefix_rule(p);

    while (prec <= get_rule(p->curr.kind)->precedence) {
        p->prev = p->curr;
        p->curr = uf_lexer_next_token(p->lexer);
        ParseInfixFn infix_rule = get_rule(p->prev.kind)->infix;
        left = infix_rule(p, left);
    }

    return left;
}
```

---

## 5. Semantic Analysis, Scope Hoisting & Upvalue Resolution

The semantic pass (`uf_semantic.c`) verifies the AST before bytecode generation:

1. **Mutual Recursion Hoisting**:
   - In any lexical block, top-level function declarations are scanned and added to the scope symbol table in **Pass 1**.
   - Function bodies are analyzed in **Pass 2**.
   - This permits mutual recursion (`fn is_even(n): is_odd(n-1)`) without forward declarations.
2. **Upvalue Resolution**:
   - When an inner function references an identifier in an enclosing outer function's scope, the compiler allocates an `Upvalue` slot.
   - Upvalues are marked as either `is_local = true` (pointing directly to an operand stack slot of the immediate enclosing frame) or `is_local = false` (capturing an existing upvalue from an outer frame).

---

## 6. Dual Bytecode Compilers (Stack vs Register)

Unfish features two distinct bytecode compilers for performance experimentation:

### 1. The Stack Compiler (`uf_compiler.c`)

- Emits 1-byte opcode instructions operating on a LIFO operand stack.
- Zero register allocation complexity.
- Backpatches jump offsets for `OP_JUMP_IF_FALSE` and `OP_JUMP`.
- Handles pattern matching via two-phase testing to ensure stack slots remain clean upon guard failures.

### 2. The Register Compiler (`uf_reg_compiler.c`)

- Emits fixed 32-bit instructions in 3-address format: `OPCODE dst, src1, src2`.
- Employs a linear register allocator mapping virtual local variables to 256 physical registers.
- Eliminates redundant push/pop traffic, executing up to 35% fewer VM dispatch cycles.

---

## 7. Dual Execution Engines (Stack VM vs 256-Register VM)

### Stack VM Dispatch Loop (`uf_vm.c`)

```c
for (;;) {
    uint8_t op = *frame->ip++;
    switch (op) {
        case OP_ADD: {
            UfValue b = *(--vm->sp);
            UfValue a = *(--vm->sp);
            *(vm->sp++) = uf_val_number(a.as.number + b.as.number);
            break;
        }
        case OP_RETURN: {
            UfValue res = *(--vm->sp);
            if (--vm->frame_count == 0) return res;
            frame = &vm->frames[vm->frame_count - 1];
            break;
        }
        /* ... 55 remaining opcodes ... */
    }
}
```

### 256-Register VM Dispatch Loop (`uf_regvm.c`)

```c
for (;;) {
    uint32_t inst = *frame->ip++;
    uint8_t op = (inst >> 24) & 0xFF;
    uint8_t rA = (inst >> 16) & 0xFF;
    uint8_t rB = (inst >> 8) & 0xFF;
    uint8_t rC = inst & 0xFF;

    switch (op) {
        case ROP_ADD:
            frame->regs[rA] = uf_val_number(frame->regs[rB].as.number + frame->regs[rC].as.number);
            break;
        /* ... 3-address register dispatch ... */
    }
}
```

---

## 8. The Native C99 AOT Code Generator

The native compiler (`src/codegen/uf_emit_c.c`) translates Unfish ASTs directly into standalone, high-performance C99:

1. **Header Inclusion**: Embeds `unfish_runtime.h` containing all data types, memory operations, and GC primitives.
2. **String Deduplication**: Scans source for all unique string literals and emits a static table `static const char* UF_STRINGS[]`.
3. **Expression Linearization**: Complex expressions are decomposed into C statements storing temporary values in `UfValue` local variables.
4. **Direct Tail Calls**: Translates self-recursive calls into `goto` labels to guarantee $O(1)$ stack space.

---

## 9. 5-Way Differential Parity Harness

Every commit in Unfish is validated using `tools/run_differential_tests.sh`.

```
                  +---------------------------+
                  |  Test Suite (.unfish)     |
                  +-------------+-------------+
                                |
       +-----------+------------+------------+-----------+
       |           |            |            |           |
       v           v            v            v           v
  Interpreter   Stack VM    Register VM   Native C99    WASM
       |           |            |            |           |
  Exit & Out  Exit & Out   Exit & Out   Exit & Out  Exit & Out
       \___________|____________|____________|___________/
                                |
                                v
                    Diff Check (Exact Match)
              Pass: 0 divergences across all engines
```

---

## 10. Tutorial: Adding a New Language Feature Step-by-Step

Let's walk through adding a hypothetical built-in function `clz32(val)` (Count Leading Zeros in 32-bit integer).

### Step 1: Implement C Native Handler in `src/runtime/uf_stdlib.c`

```c
static UfValue std_clz32(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'clz32()' expects a number argument");
        return uf_val_null();
    }
    uint32_t v = (uint32_t)args[0].as.number;
    int count = 0;
    if (v == 0) return uf_val_number(32);
    while ((v & 0x80000000) == 0) {
        count++;
        v <<= 1;
    }
    return uf_val_number(count);
}
```

### Step 2: Register in Symbol Environment

In `src/runtime/uf_stdlib.c` inside `uf_stdlib_register()`:

```c
uf_env_declare(rt->global_env, "clz32", uf_val_native("clz32", std_clz32, 1));
```

### Step 3: Register in Native C Transpiler (`src/codegen/uf_emit_c.c`)

Add the standard C implementation into `unfish_runtime.h` so `unfish emit-c` supports the new function.

### Step 4: Write Differential Test Case

Create `tests/conformance/65_clz32.unfish`:

```unfish
say clz32(0)          # 32
say clz32(1)          # 31
say clz32(0x80000000) # 0
```

### Step 5: Verify 5-Way Parity

```bash
make test
```

When all 5 engines output identical results, the feature is verified and ready for production!
