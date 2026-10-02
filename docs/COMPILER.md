# UNFISH — COMPILER ARCHITECTURE, OPTIMIZATION & CACHING MANUAL

---

## 1. Executive Summary & Compilation Pipeline

The Unfish compilation subsystem translates semantically validated Abstract Syntax Trees (ASTs) into high-performance virtual machine bytecode representations:
1. **Stack Bytecode Compiler (`src/compiler/uf_compiler.c`)**: Generates linear 57-opcode instruction chunks (`UfChunk`) for the Stack VM.
2. **Register Bytecode Compiler (`src/compiler/uf_reg_compiler.c`)**: Performs linear-scan register allocation and synthesizes 3-address instructions for the 256-register RegVM.
3. **Bytecode Optimizer (`src/compiler/uf_optimize.c`)**: Performs multi-pass AST and bytecode optimizations including constant folding, dead code elimination, and peephole simplifications.
4. **Bytecode Disk Cache (`src/compiler/uf_cache.c`)**: Serializes compiled chunks into `.ufc` (Stack VM) and `.ufrc` (Register VM) binary files with cryptographic source hashing and timestamp validation.

```
                              ┌───────────────────────────────┐
                              │      Validated Program AST    │
                              │          (UfProgram)          │
                              └──────────────┬────────────────┘
                                             │
                                             ▼
                              ┌───────────────────────────────┐
                              │    AST Constant Folding Pass  │
                              │    (src/compiler/uf_optimize) │
                              └──────────────┬────────────────┘
                                             │
                     ┌───────────────────────┴───────────────────────┐
                     │                                               │
                     ▼                                               ▼
      ┌───────────────────────────────┐               ┌───────────────────────────────┐
      │   Stack Bytecode Compiler     │               │   Register Bytecode Compiler  │
      │   (src/compiler/uf_compiler)  │               │ (src/compiler/uf_reg_compiler)│
      └──────────────┬────────────────┘               └──────────────┬────────────────┘
                     │ Raw Bytecode Chunk                            │ 3-Address Instructions
                     ▼                                               ▼
      ┌───────────────────────────────┐               ┌───────────────────────────────┐
      │     Peephole Optimizer        │               │   Register Window Allocator   │
      │  Dead code, jump chaining     │               │   (256 virtual registers)     │
      └──────────────┬────────────────┘               └──────────────┬────────────────┘
                     │ Optimized Chunk                               │ Optimized RegChunk
                     ▼                                               ▼
      ┌───────────────────────────────┐               ┌───────────────────────────────┐
      │     Bytecode Disk Cache       │               │     Bytecode Disk Cache       │
      │        (*.ufc Cache)          │               │        (*.ufrc Cache)         │
      └───────────────────────────────┘               └───────────────────────────────┘
```

---

## 2. Stack Bytecode Compiler Architecture (`src/compiler/uf_compiler.c`)

### 2.1. The `UfCompiler` State Structure
The single-pass compiler maintains compilation state in a hierarchical `UfCompiler` struct:

```c
typedef struct {
    const char* name;
    size_t length;
    int depth;            /* Lexical scope depth */
    bool is_captured;     /* True if captured as an upvalue by child closure */
} LocalVar;

typedef struct {
    uint8_t index;        /* Slot index in enclosing frame */
    bool is_local;        /* True if capturing direct local, false if capturing outer upvalue */
} UpvalueDesc;

typedef struct UfCompiler {
    struct UfCompiler* enclosing; /* Parent compiler for outer function */
    UfFunctionObject* function;   /* Function object being compiled */
    FunctionType type;            /* TYPE_SCRIPT, TYPE_FUNCTION, TYPE_METHOD */

    LocalVar locals[256];         /* Local variables active in current frame */
    size_t local_count;
    UpvalueDesc upvalues[256];    /* Upvalues captured by this function */
    size_t upvalue_count;

    int scope_depth;              /* Current block nesting depth (0 = top-level) */
    LoopContext* current_loop;    /* Active loop for break/continue patch lists */
} UfCompiler;
```

### 2.2. Variable Scope Resolution Algorithm
When an identifier is referenced in an expression, the compiler resolves its storage location using a three-tier hierarchical lookup:

```
Identifier: "counter"
    │
    ▼
1. Is it a Local Variable in the current function? (resolve_local)
    ├── Scan compiler->locals from top to bottom
    └── Found? Return slot index (Emit OP_LOAD_LOCAL / OP_STORE_LOCAL)
    │
    ▼ Not found
2. Is it in an Enclosing Function? (resolve_upvalue)
    ├── Recursively inspect compiler->enclosing
    ├── Found in enclosing locals? Mark local as is_captured = true
    ├── Add upvalue descriptor to current compiler->upvalues
    └── Return upvalue index (Emit OP_GET_UPVALUE / OP_SET_UPVALUE)
    │
    ▼ Not found
3. Treat as Global Symbol (resolve_global)
    ├── Intern identifier string in chunk's constant pool
    └── Return constant index (Emit OP_LOAD_GLOBAL / OP_STORE_GLOBAL)
```

### 2.3. Forward Jump Patching
Control flow constructs (`if`, `while`, `or`, `and`) require jumping over bytecode instructions before the target instruction offset is known.

The compiler uses a **two-phase backpatching algorithm**:
1. **Emit Jump**: Emits the jump opcode (`OP_JUMP` or `OP_JUMP_IF_FALSE`) with a 16-bit dummy operand (`0xFFFF`). Records the byte offset of the operand.
   ```c
   size_t jump_offset = emit_jump(compiler, OP_JUMP_IF_FALSE);
   ```
2. **Compile Target Block**: Compiles the conditional statement or loop body.
3. **Patch Jump**: Calculates the relative distance from the jump instruction to the current end of the bytecode chunk, writing the real 16-bit offset into the operand slot:
   ```c
   patch_jump(compiler, jump_offset);
   ```

### 2.4. Loop Context & Break/Continue Patching
Loops (`while`, `for`, `repeat`) maintain a `LoopContext`:
* `loop_start`: Bytecode offset of the loop condition test.
* `continue_target`: Bytecode offset for `continue` statements (loop increment or re-test).
* `break_jumps[]`: Array of forward jumps emitted by `break` statements. When the loop body finishes, all entries in `break_jumps` are patched to the loop exit address.

---

## 3. Bytecode Optimization Pipeline (`src/compiler/uf_optimize.c`)

Unfish implements a multi-pass optimization engine designed to maximize runtime execution speed without noticeably slowing down compilation:

### 3.1. Constant Folding
Arithmetic, logical, and string expressions containing purely literal operands are evaluated at compile time:

| Source Code | Unoptimized Bytecode | Optimized Bytecode |
|---|---|---|
| `let x = 10 + 32` | `OP_CONSTANT 10, OP_CONSTANT 32, OP_ADD` | `OP_CONSTANT 42` |
| `let s = "A" + "B"` | `OP_CONSTANT "A", OP_CONSTANT "B", OP_ADD` | `OP_CONSTANT "AB"` |
| `let b = not false` | `OP_FALSE, OP_NOT` | `OP_TRUE` |
| `let n = 2 * 3 * 4` | 3 loads, 2 multiplications | `OP_CONSTANT 24` |

### 3.2. Dead Code Elimination
1. **Unreachable Code After Terminal Statements**: Any instructions emitted after an unconditional `return`, `break`, `continue`, or `raise` within the same block are pruned before chunk finalization.
2. **Static Branch Elimination**:
   ```unfish
   if false:
       say "Never executed"
   ```
   The condition `false` is evaluated at compile time. The entire `if` branch bytecode is omitted from the chunk, eliminating jump overhead.

### 3.3. Peephole Optimizations
The compiler performs sliding-window pattern matching over the raw bytecode array:
* **Load/Store Elimination**:
  ```
  OP_LOAD_LOCAL 3
  OP_STORE_LOCAL 3
  ==> [Eliminated: No-op]
  ```
* **Store-Pop Coalescing**: When an expression statement assigns a variable without using the value, `OP_STORE_LOCAL` followed by `OP_POP` is collapsed to write directly to the slot without redundant stack manipulation.
* **Jump-to-Jump Chaining**: If a jump instruction targets another jump instruction, the first jump is rewritten to branch directly to the final target address, eliminating indirect jump chains.

---

## 4. Register Bytecode Compiler (`src/compiler/uf_reg_compiler.c`)

The Register Compiler translates AST expressions into 3-address `UfRegOpcode` instructions:

### 4.1. Linear Scan Register Allocation
* **Register Window**: Each frame has up to 256 virtual registers ($R_0 \dots R_{255}$).
* **Local Variable Mapping**: The first $N$ registers ($R_0 \dots R_{N-1}$) are permanently assigned to local variables and function parameters.
* **Temporary Register Pool**: Registers above $N$ ($R_N \dots R_{255}$) are dynamically allocated for intermediate subexpression evaluation and immediately returned to the pool once consumed.

### 4.2. Expression Synthesis Example
Consider the statement:
```unfish
let result = (a + b) * (c - d)
```

Assuming `a`, `b`, `c`, `d`, `result` occupy registers $R_0 \dots R_4$:
```
ROP_ADD R5, R0, R1    ; Temp R5 = a + b
ROP_SUB R6, R2, R3    ; Temp R6 = c - d
ROP_MUL R4, R5, R6    ; result (R4) = R5 * R6
```
Total: 3 instructions. The temporary registers $R_5$ and $R_6$ are recycled for subsequent statements.

---

## 5. Bytecode Serialization & Disk Caching (`src/compiler/uf_cache.c`)

Unfish supports ahead-of-time bytecode compilation and caching via `unfish compile`:
* Stack VM cache: `<source>.ufc`
* Register VM cache: `<source>.ufrc`

### 5.1. Binary Cache File Format Specification

```
┌────────────────────────────────────────────────────────────────────────┐
│                        UFC BINARY CACHE FILE FORMAT                    │
├─────────────────┬──────────┬───────────────────────────────────────────┤
│ Field           │ Size     │ Description                               │
├─────────────────┼──────────┼───────────────────────────────────────────┤
│ Magic Header    │ 4 Bytes  │ ASCII "UFC\1" (Stack) or "UFR\1" (RegVM)  │
│ Compiler Version│ 4 Bytes  │ Monotonically increasing engine version   │
│ Source SHA-256  │ 32 Bytes │ SHA-256 cryptographic hash of source file │
│ Source Timestamp│ 8 Bytes  │ 64-bit Unix epoch modification timestamp  │
│ Constant Count  │ 4 Bytes  │ Number of entries in constant pool        │
│ Constant Pool   │ Variable │ Serialized values (numbers, strings, etc.)│
│ Bytecode Length │ 4 Bytes  │ Number of bytes in instruction stream     │
│ Bytecode Stream │ Variable │ Raw bytecode instructions and operands    │
│ Line Map Length │ 4 Bytes  │ Number of entries in line mapping table   │
│ Line Map Data   │ Variable │ Run-length encoded line coordinates       │
└─────────────────┴──────────┴───────────────────────────────────────────┘
```

### 5.2. Cache Validation & Invalidation Strategy
When executing `unfish run <file.unfish>` (unless `--no-cache` is specified):
1. The engine checks for an existing `<file>.ufc` or `<file>.ufrc` cache file.
2. If present, the engine compares the cached `Source Timestamp` and `Source SHA-256` against the on-disk source file.
3. If both match, parsing, semantic analysis, and compilation are skipped entirely, and the precompiled bytecode is loaded directly into the VM in sub-millisecond time.
4. If the source file has been modified, the cache is automatically invalidated, recompiled, and rewritten.
