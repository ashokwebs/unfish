# UNFISH — MEMORY MODEL, GARBAGE COLLECTION & SYSTEMS PROGRAMMING SPECIFICATION

---

## 1. Executive Memory Architecture

Unfish employs a **hybrid two-phase memory management architecture** engineered to eliminate allocation fragmentation during compilation while providing deterministic, cycle-safe reclamation during execution:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        TWO-PHASE MEMORY ARCHITECTURE                   │
├───────────────────────────────────┬────────────────────────────────────┤
│ Phase A: Compilation Phase        │ Phase B: Execution Runtime         │
│ (Linear Arena Allocator)          │ (Mark-and-Sweep Garbage Collector) │
├───────────────────────────────────┼────────────────────────────────────┤
│ • AST Nodes (UfStmt, UfExpr)      │ • Strings (UfStringObject)         │
│ • Lexer Tokens & String Interning │ • Dynamic Arrays (UfArrayObject)   │
│ • Semantic Scope Symbol Tables    │ • Hash Maps (UfMapObject)          │
│ • Bytecode Compiler Buffers       │ • Structs, Enums, Closures, Fibers │
│ • O(1) bulk deallocation          │ • Raw Byte Buffers (UfBufferObject)│
│ • Zero fragmentation risk         │ • Cycle-safe graph traversal       │
└───────────────────────────────────┴────────────────────────────────────┘
```

---

## 2. Compilation Arenas (`src/common/uf_arena.c`)

### 2.1. Chained Bump Allocation Design
During compilation, a source file is converted into thousands of small structures (AST nodes, tokens, parameter lists, type annotations). Allocating these structures with standard C `malloc` creates severe heap fragmentation and requires a complex, error-prone traversal to free them.

Instead, Unfish uses a chained chunk arena (`UfArena`):

```c
typedef struct UfArenaChunk {
    struct UfArenaChunk* next;
    size_t capacity;
    size_t used;
    char data[]; /* Flexible array member */
} UfArenaChunk;

typedef struct {
    UfArenaChunk* first;
    UfArenaChunk* current;
    size_t default_chunk_size;
} UfArena;
```

#### Allocation Mechanics:
1. **Pointer Bump**: Allocation within the current chunk is a simple pointer addition (`current->data + current->used`), executing in a few CPU cycles.
2. **8-Byte Alignment**: Every allocation is rounded up to the nearest 8-byte boundary (`(size + 7) & ~7`), guaranteeing natural hardware alignment for 64-bit pointers and floating-point values.
3. **Chunk Chaining**: If an allocation exceeds the remaining capacity of the current chunk, a new chunk is allocated (default 8 KB, or larger if a single allocation exceeds 8 KB) and linked to the list.
4. **$O(1)$ Bulk Free**: When compilation finishes, `uf_arena_free()` traverses the chunk linked list and frees each 8 KB chunk in sequence. The entire AST and all intermediate compiler structures are reclaimed in $O(1)$ time without visiting individual nodes.

---

## 3. Mark-and-Sweep Garbage Collector (`src/runtime/uf_runtime.c`)

During program execution, values and objects are dynamically allocated, shared, and referenced. Unfish manages heap memory via a precise **Mark-and-Sweep Garbage Collector**.

### 3.1. Heap Object Intrusive Header (`src/runtime/uf_object.h`)
Every heap object begins with an intrusive `UfObj` header:

```c
struct UfObj {
    UfObjType type;
    bool is_marked;        /* GC mark bit (false = white, true = black) */
    struct UfObj* next;    /* Pointer in global singly-linked list */
};
```

When an object is allocated, it is inserted at the head of `rt->all_objects`.

### 3.2. GC Triggers & Dynamic Threshold Scaling
The runtime tracks dynamic memory pressure:
* `rt->bytes_allocated`: Total bytes currently allocated on the heap.
* `rt->next_gc_threshold`: Allocation threshold that triggers the next collection (default initial value: 1 MB).

When `bytes_allocated >= next_gc_threshold`, the runtime pauses execution and initiates a garbage collection cycle:

```c
void* uf_gc_alloc(UfRuntime* rt, size_t size, UfObjType type) {
    if (rt->bytes_allocated + size >= rt->next_gc_threshold) {
        uf_gc_collect(rt);
    }
    UfObj* obj = (UfObj*)malloc(size);
    obj->type = type;
    obj->is_marked = false;
    obj->next = rt->all_objects;
    rt->all_objects = obj;
    rt->bytes_allocated += size;
    return obj;
}
```

After each collection, `next_gc_threshold` is scaled dynamically based on surviving heap size:
$$\text{next\_gc\_threshold} = \max(\text{surviving\_bytes} \times \text{GC\_GROWTH\_FACTOR}, \text{MIN\_THRESHOLD})$$

### 3.3. Root Set Discovery & Traversal
The mark phase traverses all live references starting from the root set:

```
                                  ┌───────────────────────────┐
                                  │      THE ROOT SET         │
                                  └─────────────┬─────────────┘
           ┌──────────────────────┬─────────────┴─────────────┬──────────────────────┐
           ▼                      ▼                           ▼                      ▼
    ┌─────────────┐        ┌─────────────┐             ┌─────────────┐        ┌─────────────┐
    │ Global Env  │        │ Active Call │             │ VM Operand  │        │ Temp Root   │
    │  Bindings   │        │ Stack Frame │             │    Stack    │        │    Stack    │
    └─────────────┘        └─────────────┘             └─────────────┘        └─────────────┘
           │                      │                           │                      │
           ▼                      ▼                           ▼                      ▼
    ┌─────────────┐        ┌─────────────┐             ┌─────────────┐        ┌─────────────┐
    │ Open Upvalue│        │ Active Fiber│             │ Channel     │        │ Module      │
    │ Linked List │        │ Run Queues  │             │ Ring Buffers│        │ Registry    │
    └─────────────┘        └─────────────┘             └─────────────┘        └─────────────┘
```

1. **Global Scope**: All variables declared in `rt->global_env`.
2. **Active Call Frames**: Local variables across all call frames on the call stack.
3. **Active VM Operand Stack**: Unpopped intermediate evaluation values on `vm->stack`.
4. **Temporary Root Protection Stack (`rt->temp_roots`)**: Scratch objects created during native function execution or complex expression evaluation (ADR 008).
5. **Open Upvalues**: All stack variables captured by open upvalues in `vm->open_upvalues`.
6. **Fibers & Channels**: All active and suspended fibers in the scheduler queue, along with messages buffered inside open channels.
7. **Module Registry**: All loaded module singleton instances.

### 3.4. Cycle-Safe Graph Marking
Unfish safely collects circular data structures (e.g. mutual closure environments, self-referential arrays, or maps referencing themselves):
* When an object is visited, its `is_marked` flag is set to `true`.
* If an object is already marked, traversal immediately returns, preventing infinite recursion on cycles.
* Traversal recursively marks child objects: array elements, map keys and values, closure functions and upvalues, struct fields, and enclosing environments.

### 3.5. Sweep Phase
Once marking completes, the sweeper traverses `rt->all_objects`:
1. If `obj->is_marked == true`: The object survived. Its mark flag is reset to `false` for the next cycle.
2. If `obj->is_marked == false`: The object is unreachable. It is unlinked from `rt->all_objects`, its internal buffer (if dynamic array or map) is freed, and its `UfObj` allocation is reclaimed via `free()`.

---

## 4. Systems Programming & Low-Level Primitives

Unfish bridges the gap between high-level scripting and low-level systems programming by exposing contiguous, unmanaged byte buffers and endian-explicit binary serialization:

```unfish
# Allocate a 16-byte contiguous binary buffer
let buf = buffer(16)

# Write structured little-endian header data
buffer_write_u16_le(buf, 0, 0x55AA)     # Magic header bytes
buffer_write_u32_le(buf, 2, 1024)       # Payload length
buffer_write_i32_le(buf, 6, -42)        # Signed integer status

# Read back fields
let magic = buffer_read_u16_le(buf, 0)
let length = buffer_read_u32_le(buf, 2)
let status = buffer_read_i32_le(buf, 6)

say "Magic: " + to_hex(magic) + ", Length: " + str(length) + ", Status: " + str(status)
# Emits: Magic: 0x55aa, Length: 1024, Status: -42
```

### 4.1. Complete Systems Buffer Built-in API
| Function | Signature | Description |
|---|---|---|
| `buffer` | `(size: Number) -> Buffer` | Allocates a raw byte buffer initialized to zero |
| `buffer_size` | `(buf: Buffer) -> Number` | Returns the total byte capacity of the buffer |
| `buffer_get` | `(buf: Buffer, offset: Number) -> Number` | Reads a single unsigned byte `[0, 255]` |
| `buffer_set` | `(buf: Buffer, offset: Number, val: Number) -> Null` | Writes a single byte |
| `buffer_fill` | `(buf: Buffer, val: Number) -> Null` | Fills entire buffer with a byte value |
| `buffer_slice` | `(buf: Buffer, start: Number, len: Number) -> Buffer` | Extracts a sub-buffer slice |
| `buffer_from_string` | `(s: String) -> Buffer` | Converts UTF-8 string to a byte buffer |
| `buffer_to_string` | `(buf: Buffer) -> String` | Decodes buffer bytes as a UTF-8 string |
| `buffer_to_hex` | `(buf: Buffer) -> String` | Encodes buffer as a hexadecimal string |
| `buffer_from_hex` | `(hex: String) -> Buffer` | Decodes hexadecimal string into a buffer |
| `buffer_read_u16_le` | `(buf: Buffer, offset: Number) -> Number` | Reads unsigned 16-bit integer (little-endian) |
| `buffer_write_u16_le`| `(buf: Buffer, offset: Number, val: Number) -> Null` | Writes unsigned 16-bit integer (little-endian) |
| `buffer_read_u32_le` | `(buf: Buffer, offset: Number) -> Number` | Reads unsigned 32-bit integer (little-endian) |
| `buffer_write_u32_le`| `(buf: Buffer, offset: Number, val: Number) -> Null` | Writes unsigned 32-bit integer (little-endian) |
| `buffer_read_i32_le` | `(buf: Buffer, offset: Number) -> Number` | Reads signed 32-bit integer (little-endian) |
| `buffer_write_i32_le`| `(buf: Buffer, offset: Number, val: Number) -> Null` | Writes signed 32-bit integer (little-endian) |

### 4.2. Bitwise Arithmetic & Cast Operators
Unfish provides bitwise manipulation primitives:
* **Operators**: `band(a, b)`, `bor(a, b)`, `bxor(a, b)`, `bnot(a)`, `shl(a, bits)`, `shr(a, bits)` (logical right shift), `sar(a, bits)` (arithmetic right shift).
* **Integer Bit Casts**: `u8(n)`, `i8(n)`, `u16(n)`, `i16(n)`, `u32(n)`, `i32(n)`.
* **Hex Conversion**: `to_hex(number)` and `from_hex(hex_string)`.
* **Memory Introspection**: `inspect(value)` dumps the internal runtime memory representation, object address, and tag layout of any value.

---

## 5. Sanitizer Validation & Safety Guarantees

Unfish's memory architecture is rigorously verified under automated compiler sanitizers:
1. **AddressSanitizer (ASan)**: Validates that no buffer out-of-bounds accesses, heap-use-after-free, or stack corruption occur across the entire test suite.
2. **LeakSanitizer (LSan)**: Enforces that when `uf_runtime_free()` is invoked upon shutdown, all heap allocations are 100% reclaimed with zero memory leaks.
3. **UndefinedBehaviorSanitizer (UBSan)**: Verifies that signed integer overflows, misaligned pointer accesses, and invalid bit shifts are completely absent.

Run sanitizer tests with:
```bash
make test-asan
```

`make test-gc-stress` additionally runs the conformance suite with `UNFISH_GC_STRESS=1`, which collects on every allocation, so any object left unrooted across an allocation is caught deterministically (see `docs/TESTING.md`).
