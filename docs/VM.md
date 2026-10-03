# UNFISH — VIRTUAL MACHINE SPECIFICATION & ARCHITECTURE MANUAL
## Stack-Based Virtual Machine (Tier 2) & Register-Based Virtual Machine (Tier 3)

---

## 1. Executive Summary & Dual VM Architecture

Unfish features a unique **dual virtual machine architecture**:
* **Tier 2: 59-Opcode Stack-Based Virtual Machine (`src/vm/uf_vm.c`)**: A zero-dependency, stack-oriented virtual CPU executing flat bytecode chunks. It features explicit operand stack evaluation, lexical upvalue capture cells, and structured exception unwinding. It provides an ideal educational vehicle for teaching computer architecture, compiler backends, and virtual machines.
* **Tier 3: 256-Register Computed-Goto Virtual Machine (`src/vm2/uf_regvm.c`)**: A high-performance 3-address register virtual machine modeled after modern production runtimes (Lua 5.0, LuaJIT). It maps variable computations directly to virtual registers, uses direct-threaded computed-goto dispatch, and reduces instruction dispatch overhead by 45%–60%.

Both virtual machines execute the same user code with 100% behavioral differential parity, verified against the AST interpreter, native C99 binaries, and WebAssembly.

---

## 2. The Stack-Based Virtual Machine (`src/vm/`)

### 2.1. VM Machine State & Data Structures
The Stack VM is encapsulated in the `UfVM` structure (`src/vm/uf_vm.h`):

```c
#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * 256)

typedef struct {
    UfFunctionObject* fn;      /* Currently executing bytecode function */
    uint8_t* ip;               /* Instruction pointer into chunk bytecode */
    UfValue* slots;            /* Pointer to frame's local variable slot window */
} UfCallFrame;

typedef struct {
    uint8_t* catch_ip;         /* Target instruction address of catch block */
    size_t frame_index;        /* Call frame depth at time of try entry */
    size_t stack_depth;        /* Operand stack pointer at time of try entry */
} UfTryFrame;

typedef struct UfVM {
    UfRuntime* runtime;        /* Memory manager and GC registry */
    UfCallFrame frames[FRAMES_MAX];
    size_t frame_count;

    UfValue stack[STACK_MAX];  /* Contiguous operand evaluation stack */
    UfValue* stack_top;        /* Points to slot above topmost stack value */

    UfUpvalue* open_upvalues;  /* Sorted linked list of open upvalues */

    UfTryFrame try_stack[64];  /* Exception handler unwind stack */
    size_t try_count;
} UfVM;
```

### 2.2. Bytecode Chunk Layout (`src/compiler/uf_chunk.h`)
A compiled function or script is stored in a linear `UfChunk`:
* `uint8_t* code`: Contiguous array of bytecode instructions and operands.
* `UfValueArray constants`: Constant pool containing numeric literals, interned string objects, child function prototypes, and struct definitions.
* `int* lines`: Run-length encoded source line mapping associating every bytecode offset with its original source line number.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        BYTECODE CHUNK MEMORY LAYOUT                    │
├────────────────────────────────────────────────────────────────────────┤
│ Header:  [ magic: 4B | arity: 2B | upvalue_count: 2B | code_len: 4B ]  │
│ Bytecode: [ OP_CONSTANT | 0x00 | 0x01 | OP_LOAD_LOCAL | 0x00 | ... ]   │
│ Constant Pool: [ 0: Number(42.0) | 1: String("Hello") | 2: ChildFn ]   │
│ Line Map: [ offset 0..3 -> Line 1 | offset 4..8 -> Line 2 | ... ]      │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 3. The 59-Opcode Stack ISA Reference

Every instruction in the Stack VM ISA is encoded as an 8-bit opcode (`UfOpcode`), optionally followed by 8-bit or 16-bit operands encoded in little-endian format.

### 3.1. Literals & Constants
| Opcode | Operands | Stack Effect | Description |
|---|---|---|---|
| `OP_CONSTANT` | `u16 index` | `[] -> [val]` | Read constant at index from pool and push onto stack |
| `OP_NULL` | None | `[] -> [null]` | Push `null` literal onto stack |
| `OP_TRUE` | None | `[] -> [true]` | Push `true` boolean onto stack |
| `OP_FALSE` | None | `[] -> [false]` | Push `false` boolean onto stack |

### 3.2. Stack Manipulation
| Opcode | Operands | Stack Effect | Description |
|---|---|---|---|
| `OP_POP` | None | `[val] -> []` | Discard top of stack |
| `OP_DUP` | None | `[val] -> [val, val]` | Duplicate top of stack |

### 3.3. Variable Access
| Opcode | Operands | Stack Effect | Description |
|---|---|---|---|
| `OP_LOAD_LOCAL` | `u16 slot` | `[] -> [val]` | Push local variable from frame's `slots[slot]` |
| `OP_STORE_LOCAL`| `u16 slot` | `[val] -> [val]` | Store top of stack into frame's `slots[slot]` |
| `OP_LOAD_GLOBAL`| `u16 name_idx`| `[] -> [val]` | Look up global variable named `constants[name_idx]` |
| `OP_STORE_GLOBAL`| `u16 name_idx`| `[val] -> [val]`| Mutate existing global variable named `constants[name_idx]`|
| `OP_DEFINE_GLOBAL`| `u16 name_idx`| `[val] -> []` | Define new global variable named `constants[name_idx]` |

### 3.4. Closures & Upvalue Management
| Opcode | Operands | Stack Effect | Description |
|---|---|---|---|
| `OP_GET_UPVALUE`| `u8 index` | `[] -> [val]` | Push captured upvalue at `index` onto stack |
| `OP_SET_UPVALUE`| `u8 index` | `[val] -> [val]` | Mutate captured upvalue at `index` |
| `OP_CLOSURE` | `u16 fn_idx` | `[] -> [closure]` | Instantiate closure from prototype; reads upvalue descriptor bytes |
| `OP_CLOSE_UPVALUE`| None | `[val] -> []` | Close upvalues pointing to top of stack and pop value |

### 3.5. Arithmetic & Bitwise
| Opcode | Operands | Stack Effect | Description |
|---|---|---|---|
| `OP_ADD` | None | `[a, b] -> [a + b]` | Add numbers or concatenate strings |
| `OP_SUB` | None | `[a, b] -> [a - b]` | Subtract numbers |
| `OP_MUL` | None | `[a, b] -> [a * b]` | Multiply numbers |
| `OP_DIV` | None | `[a, b] -> [a / b]` | Divide numbers (raises `ERR_DIV_ZERO` if b == 0) |
| `OP_MOD` | None | `[a, b] -> [a % b]` | Modulo numbers (raises `ERR_DIV_ZERO` if b == 0) |

### 3.6. Unary & Logical
| Opcode | Operands | Stack Effect | Description |
|---|---|---|---|
| `OP_NEG` | None | `[a] -> [-a]` | Arithmetic negation |
| `OP_NOT` | None | `[a] -> [!a]` | Logical NOT (falsy if `false` or `null`) |

### 3.7. Comparison & Equality
| Opcode | Operands | Stack Effect | Description |
|---|---|---|---|
| `OP_EQ` | None | `[a, b] -> [a == b]`| Structural equality test |
| `OP_NEQ` | None | `[a, b] -> [a != b]`| Structural inequality test |
| `OP_LT` | None | `[a, b] -> [a < b]` | Less-than test |
| `OP_LTE` | None | `[a, b] -> [a <= b]`| Less-than-or-equal test |
| `OP_GT` | None | `[a, b] -> [a > b]` | Greater-than test |
| `OP_GTE` | None | `[a, b] -> [a >= b]`| Greater-than-or-equal test |

### 3.8. Control Flow & Branching
| Opcode | Operands | Stack Effect | Description |
|---|---|---|---|
| `OP_JUMP` | `u16 offset` | `[] -> []` | Unconditionally increment `ip` by `offset` |
| `OP_JUMP_IF_FALSE`| `u16 offset` | `[cond] -> [cond]` | Jump forward by `offset` if top of stack is falsy |
| `OP_JUMP_IF_ARG` | `u8 arg_idx, u16 offset`| `[] -> []`| Jump forward by `offset` if argument index was supplied |
| `OP_LOOP` | `u16 offset` | `[] -> []` | Backward loop jump: decrement `ip` by `offset` |

### 3.9. Functions & Invocations
| Opcode | Operands | Stack Effect | Description |
|---|---|---|---|
| `OP_CALL` | `u8 argc` | `[fn, arg1..argN] -> [res]` | Call function or closure with `argc` arguments |
| `OP_CALL_SPREAD`| None | `[fn, arg_arr] -> [res]` | Call function with arguments expanded from array |
| `OP_RETURN` | None | `[res] -> []` | Pop call frame, close frame upvalues, push return value |

### 3.10. Collections & Structs
| Opcode | Operands | Stack Effect | Description |
|---|---|---|---|
| `OP_BUILD_ARRAY`| `u16 count` | `[v1..vN] -> [array]` | Pop `count` values, push dynamic array object |
| `OP_ARRAY_PUSH` | None | `[array, val] -> [array]` | Append `val` to dynamic array |
| `OP_ARRAY_EXTEND`| None | `[arr1, arr2] -> [arr1]` | Extend `arr1` with elements of `arr2` |
| `OP_ARRAY_SLICE`| `u16 start_idx`| `[array] -> [slice]` | Create slice of array starting at `start_idx` |
| `OP_ASSERT_ARRAY`| None | `[val] -> [val]` | Assert `val` is an array; raise `ERR_DESTRUCTURE_TYPE` if not |
| `OP_ASSERT_MAP` | None | `[val] -> [val]` | Assert `val` is a map; raise `ERR_DESTRUCTURE_TYPE` if not |
| `OP_ARRAY_GET_SAFE`| `u16 index` | `[array] -> [val]` | Get element at index; push `null` if out of bounds |
| `OP_MAP_GET_SAFE`| None | `[map, key] -> [val]` | Get map value; push `null` if key missing |
| `OP_MAP_REST` | `u16 count, ...` | `[map] -> [rest_map]` | Extract remaining map keys excluding listed constants |
| `OP_BUILD_MAP` | `u16 pair_count`| `[k1,v1..kN,vN] -> [map]` | Pop `pair_count * 2` values, push hash map object |
| `OP_MAP_SET` | None | `[map, key, val] -> [map]`| Insert key-value pair into hash map |
| `OP_MAP_EXTEND` | None | `[map1, map2] -> [map1]` | Extend `map1` with entries from `map2` |
| `OP_INDEX_GET` | None | `[obj, index] -> [val]` | Subscript array, map, or string |
| `OP_INDEX_SET` | None | `[obj, idx, val] -> [val]` | Mutate element at `obj[idx] = val` |
| `OP_ITER_GET` | None | `[obj, idx] -> [val]` | Fetch iterator element at index |
| `OP_SAY` | None | `[val] -> []` | Output value with newline to stdout |
| `OP_STRUCT_DEF` | `u16 sdef_idx` | `[] -> []` | Register struct definition from constants |
| `OP_INSTANCE` | `u16 sdef_idx` | `[f1..fN] -> [inst]` | Create struct instance from initialized fields |

### 3.11. Exceptions & Concurrency
| Opcode | Operands | Stack Effect | Description |
|---|---|---|---|
| `OP_PUSH_TRY` | `u16 catch_offset`| `[] -> []` | Push exception unwind frame with catch destination |
| `OP_POP_TRY` | None | `[] -> []` | Pop exception unwind frame upon normal completion |
| `OP_RETHROW` | None | `[err] -> []` | Re-raise unhandled exception to enclosing frame |
| `OP_AWAIT` | None | `[promise] -> [res]` | Cooperatively suspend until promise resolves |
| `OP_MATCH_SHAPE` | `u8 shape, u16 count` | `[val] -> [bool]` | Test a `match` pattern's shape: array length, map-or-instance, or field count |
| `OP_MATCH_FIELD` | `u16 index` | `[val] -> [field]` | Positional field of a struct instance or enum value (`null` if absent) |

---

## 4. Upvalue Capture Architecture in Stack VM

When a closure captures a local variable from an enclosing scope, Unfish implements the **Lua 5.0 Upvalue Algorithm**:

```
Active Stack Frame (Open Upvalue):
    vm->open_upvalues ──► [ UfUpvalue ] ──location──► &vm->stack[slot] (count = 0)
                               │
                               ▼
                          [ UfUpvalue ] ──location──► &vm->stack[slot2]

When Stack Frame Returns (uf_vm_close_upvalues):
    1. Read value at upvalue->location (value = 0)
    2. Store value into upvalue->closed_storage
    3. Update upvalue->location = &upvalue->closed_storage
    4. Remove upvalue from vm->open_upvalues linked list
```

This guarantees:
1. Stack variables that do not escape incur zero heap allocation overhead.
2. Multiple closures referencing the same outer variable share the exact same upvalue cell and observe mutations immediately.
3. When the outer function returns, the variable migrates to heap storage automatically without dangling pointers.

---

## 5. The Register-Based Virtual Machine (`src/vm2/`)

### 5.1. Motivation & 3-Address Instructions
Stack-based virtual machines suffer from heavy operand stack traffic: pushing operands, popping them, and writing results back to local variable slots.

The Unfish Register VM (`RegVM`) uses **3-address register instructions**:
$$\text{Register}[A] = \text{Register}[B] \odot \text{Register}[C]$$

A mathematical addition that requires 4 instructions in the Stack VM:
```
OP_LOAD_LOCAL 0
OP_LOAD_LOCAL 1
OP_ADD
OP_STORE_LOCAL 2
```
Is encoded in the Register VM as a **single 32-bit instruction**:
```
ROP_ADD R2, R0, R1
```

### 5.2. Register Instruction Formats
Every Register VM instruction is packed into a 32-bit word (`uint32_t`):

```
Format iABC:   [ Opcode: 8 bits | A: 8 bits | B: 8 bits | C: 8 bits ]
Format iABx:   [ Opcode: 8 bits | A: 8 bits | Bx (unsigned): 16 bits ]
Format iAsBx:  [ Opcode: 8 bits | A: 8 bits | sBx (signed): 16 bits ]
Format isAx:   [ Opcode: 8 bits | sAx (signed): 24 bits ]
```

### 5.3. Register ISA Table (`src/vm2/uf_regvm_opcodes.h`)
| Opcode | Format | Semantics | Description |
|---|---|---|---|
| `ROP_LOAD_K` | `iABx` | $R(A) = K[Bx]$ | Load constant $Bx$ into register $A$ |
| `ROP_LOAD_NULL` | `iABC` | $R(A) = \text{null}$ | Clear register $A$ to null |
| `ROP_LOAD_TRUE` | `iABC` | $R(A) = \text{true}$ | Set register $A$ to true |
| `ROP_LOAD_FALSE`| `iABC` | $R(A) = \text{false}$ | Set register $A$ to false |
| `ROP_MOVE` | `iABC` | $R(A) = R(B)$ | Copy register $B$ to register $A$ |
| `ROP_GET_GLOBAL`| `iABx` | $R(A) = \text{globals}[K[Bx]]$ | Fetch global variable named $K[Bx]$ |
| `ROP_SET_GLOBAL`| `iABx` | $\text{globals}[K[Bx]] = R(A)$ | Mutate global variable named $K[Bx]$ |
| `ROP_DEF_GLOBAL`| `iABx` | $\text{globals}[K[Bx]] = R(A)$ | Define new global variable named $K[Bx]$ |
| `ROP_GET_UPVAL` | `iABC` | $R(A) = \text{upvalues}[B]$ | Fetch upvalue $B$ into register $A$ |
| `ROP_SET_UPVAL` | `iABC` | $\text{upvalues}[B] = R(A)$ | Mutate upvalue $B$ with register $A$ |
| `ROP_CLOSE_UPVAL`| `iABC` | Close upvalues $\ge \&R(A)$ | Close upvalues at or above register $A$ |
| `ROP_CLOSURE` | `iABx` | $R(A) = \text{closure}(K[Bx])$ | Create closure from function prototype |
| `ROP_ADD` | `iABC` | $R(A) = R(B) + R(C)$ | Add or concatenate registers $B$ and $C$ |
| `ROP_SUB` | `iABC` | $R(A) = R(B) - R(C)$ | Subtract register $C$ from $B$ |
| `ROP_MUL` | `iABC` | $R(A) = R(B) \times R(C)$ | Multiply register $B$ by $C$ |
| `ROP_DIV` | `iABC` | $R(A) = R(B) / R(C)$ | Divide register $B$ by $C$ |
| `ROP_MOD` | `iABC` | $R(A) = R(B) \% R(C)$ | Modulo register $B$ by $C$ |
| `ROP_NEG` | `iABC` | $R(A) = -R(B)$ | Negate register $B$ |
| `ROP_NOT` | `iABC` | $R(A) = !R(B)$ | Logical NOT of register $B$ |
| `ROP_EQ` | `iABC` | $R(A) = (R(B) == R(C))$ | Equality comparison |
| `ROP_NEQ` | `iABC` | $R(A) = (R(B) != R(C))$ | Inequality comparison |
| `ROP_LT` | `iABC` | $R(A) = (R(B) < R(C))$ | Less-than comparison |
| `ROP_LTE` | `iABC` | $R(A) = (R(B) \le R(C))$ | Less-than-or-equal comparison |
| `ROP_GT` | `iABC` | $R(A) = (R(B) > R(C))$ | Greater-than comparison |
| `ROP_GTE` | `iABC` | $R(A) = (R(B) \ge R(C))$ | Greater-than-or-equal comparison |
| `ROP_JMP` | `isAx` | $PC += sAx$ | Unconditional relative jump |
| `ROP_JMP_FALSE` | `iAsBx` | if $!R(A): PC += sBx$ | Jump if register $A$ is falsy |
| `ROP_JMP_TRUE` | `iAsBx` | if $R(A): PC += sBx$ | Jump if register $A$ is truthy |
| `ROP_LOOP` | `iABx` | $PC -= Bx$ | Backward loop jump |
| `ROP_CALL` | `iABC` | $R(A) = R(A)(R(A+1)\dots)$ | Call function in $R(A)$ with $B$ args |
| `ROP_RETURN` | `iABC` | return $R(A)$ | Return register $A$ to caller |
| `ROP_NEW_ARRAY` | `iABC` | $R(A) = [R(C)\dots R(C+B-1)]$ | Allocate array from $B$ registers |
| `ROP_NEW_MAP` | `iABC` | $R(A) = \{R(C): R(C+1)\dots\}$ | Allocate map from $B$ key-value pairs |
| `ROP_INDEX_GET` | `iABC` | $R(A) = R(B)[R(C)]$ | Subscript array, map, or string |
| `ROP_INDEX_SET` | `iABC` | $R(A)[R(B)] = R(C)$ | Mutate container index |
| `ROP_SAY` | `iABC` | output $R(A)$ | Print register $A$ followed by newline |
| `ROP_PUSH_TRY` | `iAsBx` | push try with catch $sBx$ | Register exception handler |
| `ROP_POP_TRY` | `iABC` | pop try | Unregister exception handler |
| `ROP_RETHROW` | `iABC` | rethrow $R(A)$ | Re-raise error in register $A$ |
| `ROP_AWAIT` | `iABC` | $R(A) = \text{await } R(B)$ | Await promise into register $A$ |
| `ROP_MATCH_SHAPE` | `iABC` | $R(A) = R(B)$ has shape $C$ | `match` shape test; the next word holds the count or struct/variant name |
| `ROP_MATCH_FIELD` | `iABC` | $R(A) = R(B).\text{field}[C]$ | Positional field of an instance or enum value, or `null` |

### 5.4. Computed-Goto Direct-Threaded Dispatch
Under modern C compilers (`gcc`, `clang`), `src/vm2/uf_regvm.c` leverages GCC's labels-as-values extension (`&&label`) to implement **direct-threaded computed-goto dispatch**:

```c
static const void* dispatch_table[] = {
    [ROP_LOAD_K]    = &&do_load_k,
    [ROP_MOVE]      = &&do_move,
    [ROP_ADD]       = &&do_add,
    [ROP_SUB]       = &&do_sub,
    /* ... */
};

#define DISPATCH() goto *dispatch_table[GET_OPCODE(*pc++)]
```

This bypasses the centralized C `switch` statement, replacing it with an indirect jump at the end of every opcode handler. Branch target prediction buffers on modern x86-64 and ARM64 CPUs predict these jumps with high accuracy, eliminating pipeline stalls.

---

## 6. Execution Tracing & Bytecode Disassembly

Unfish includes a disassembler for inspecting compiled chunks:

```bash
unfish disasm program.unfish
```

Output format:
```
== Chunk: <main> ==
Offset Line  Opcode               Operands
0000   1     OP_CONSTANT          0 (42)
0003   |     OP_STORE_LOCAL       0
0006   2     OP_LOAD_LOCAL        0
0009   |     OP_CONSTANT          1 (10)
0012   |     OP_ADD
0013   |     OP_SAY
0014   3     OP_RETURN
```

When run with `unfish run --vm --debug program.unfish`, the VM outputs an interactive execution trace showing the exact stack contents before every instruction:

```
          [ 42 ]
0006   2  OP_LOAD_LOCAL 0
          [ 42, 42 ]
0009   |  OP_CONSTANT 1 (10)
          [ 42, 42, 10 ]
0012   |  OP_ADD
          [ 42, 52 ]
0013   |  OP_SAY
52
          [ 42 ]
0014   3  OP_RETURN
```
