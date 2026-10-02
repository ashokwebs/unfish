# UNFISH — RUNTIME ARCHITECTURE, VALUE MODEL & CONCURRENCY SPECIFICATION

---

## 1. Executive Runtime Architecture

The Unfish runtime system (`src/runtime/`) manages program execution, value representations, dynamic memory, call stacks, lexical environments, and cooperative concurrency.

Every execution engine in Unfish (the AST interpreter, the Stack VM, and the Register VM) shares the exact same runtime data structures and value representations, ensuring 100% interoperability and parity across all execution tiers.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        THE UfRuntime STRUCTURE                         │
├────────────────────────────────────────────────────────────────────────┤
│ • Memory Manager: Global heap object linked list (all UfObj allocations)│
│ • Garbage Collector: bytes_allocated, next_gc_threshold, mark stack    │
│ • Root Stacks: rt->temp_roots (evaluation scratch root protection)     │
│ • Scope Context: rt->global_env (top-level), rt->current_env (active)  │
│ • Call Depth Counter: recursion guard tracking UF_MAX_CALL_DEPTH       │
│ • Fiber Scheduler: Run queue of cooperative UfFiber instances          │
│ • Module Registry: Cache of imported module instances                  │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Value Representation: Tagged Union Architecture (`src/runtime/uf_value.h`)

### 2.1. The 16-Byte `UfValue`
In Unfish, all values are represented uniformly by a 16-byte tagged union:

```c
typedef enum {
    UF_VAL_NULL,
    UF_VAL_BOOL,
    UF_VAL_NUMBER,
    UF_VAL_STRING,
    UF_VAL_FUNCTION,
    UF_VAL_CLOSURE,
    UF_VAL_NATIVE,
    UF_VAL_ARRAY,
    UF_VAL_MAP,
    UF_VAL_STRUCT_DEF,
    UF_VAL_INSTANCE,
    UF_VAL_ENUM_DEF,
    UF_VAL_ENUM_VAL,
    UF_VAL_MODULE,
    UF_VAL_FIBER,
    UF_VAL_CHANNEL,
    UF_VAL_PROMISE,
    UF_VAL_BUFFER,
    UF_VAL_ERROR
} UfValueKind;

typedef struct {
    UfValueKind kind;
    union {
        bool boolean;
        double number;
        struct UfObj* obj;
        struct {
            const char* name;
            UfNativeFn fn;
            int arity;
        } native;
    } as;
} UfValue;
```

### 2.2. Tagged Union vs. NaN-Boxing: Architectural Rationale
Many production dynamically typed languages (such as LuaJIT, SpiderMonkey, or Wren) employ **NaN-boxing**, packing 64-bit pointers and tags into the unused payload bits of IEEE 754 quiet NaNs.

Unfish intentionally chose a **16-byte Tagged Union**:
1. **Strict ANSI C99 Standard Compliance**: NaN-boxing relies on pointer-to-integer casts and bit manipulations that trigger undefined behavior or pointer aliasing violations under strict ISO C.
2. **Portability Across Architectures**: Tagged unions compile cleanly on 32-bit x86, 64-bit x86-64, 32-bit ARM Cortex-M, 64-bit AArch64, RISC-V, and WebAssembly without target-specific endianness or pointer-size adjustments.
3. **Pedagogical Inspectability**: In GDB, LLDB, or the Unfish interactive debugger, a student can print `value.kind` and `value.as.number` directly, observing clear, human-readable fields without bitwise decoding masks.

---

## 3. Heap Object Model (`src/runtime/uf_object.h`)

All dynamic, heap-allocated data structures in Unfish inherit from a common intrusive header (`UfObj`):

```c
typedef enum {
    UF_OBJ_STRING,
    UF_OBJ_FUNCTION,
    UF_OBJ_CLOSURE,
    UF_OBJ_UPVALUE,
    UF_OBJ_ARRAY,
    UF_OBJ_MAP,
    UF_OBJ_STRUCT_DEF,
    UF_OBJ_INSTANCE,
    UF_OBJ_ENUM_DEF,
    UF_OBJ_ENUM_VAL,
    UF_OBJ_ENV,
    UF_OBJ_MODULE,
    UF_OBJ_FIBER,
    UF_OBJ_CHANNEL,
    UF_OBJ_PROMISE,
    UF_OBJ_BUFFER,
    UF_OBJ_ERROR
} UfObjType;

struct UfObj {
    UfObjType type;
    bool is_marked;        /* GC mark flag (0 = white/unreached, 1 = black/marked) */
    struct UfObj* next;    /* Intrusive global linked list pointer */
};
```

### 3.1. Concrete Object Layouts

#### Dynamic Strings (`UfStringObject`)
```c
typedef struct {
    UfObj obj;
    size_t length;
    uint32_t hash;        /* Precomputed FNV-1a hash */
    char chars[];         /* Flexible array member containing null-terminated UTF-8 */
} UfStringObject;
```

#### Dynamic Arrays (`UfArrayObject`)
```c
typedef struct {
    UfObj obj;
    size_t count;
    size_t capacity;
    UfValue* elements;    /* Dynamically resized array of UfValue elements */
} UfArrayObject;
```

#### Hash Maps (`UfMapObject`)
```c
typedef struct {
    UfValue key;
    UfValue value;
    bool is_deleted;      /* Tombstone flag for open addressing */
} UfMapEntry;

typedef struct {
    UfObj obj;
    size_t count;
    size_t capacity;
    UfMapEntry* entries;  /* Open-addressed table with quadratic probing */
} UfMapObject;
```

#### User Struct Definitions & Instances
```c
typedef struct {
    UfObj obj;
    UfStringObject* name;
    size_t field_count;
    char** field_names;
    UfMapObject* methods; /* Method dispatch table */
} UfStructDefObject;

typedef struct {
    UfObj obj;
    UfStructDefObject* def;
    UfValue fields[];     /* Contiguous field storage */
} UfInstanceObject;
```

---

## 4. Lexical Environments & Scope Management (`src/runtime/uf_env.h`)

In the AST interpreter, variable scopes are managed via tree-structured `UfEnv` objects:

```c
typedef struct {
    char* name;
    UfValue value;
} UfBinding;

struct UfEnv {
    UfObj obj;
    struct UfEnv* enclosing; /* Pointer to parent scope (NULL for global) */
    UfBinding* bindings;     /* Dynamically resized array of bindings */
    size_t count;
    size_t capacity;
};
```

### Scope Resolution Operations:
1. `uf_env_declare(env, name, value)`: Inserts a new binding into the current scope.
2. `uf_env_lookup(env, name, &out_val)`: Searches for `name` in `env`. If not found, recursively traverses `env->enclosing`. Returns `false` if symbol is unbound.
3. `uf_env_assign(env, name, value)`: Searches up the enclosing chain for the existing binding of `name` and mutates its value in place.

---

## 5. Cooperative Concurrency Subsystem (`src/runtime/uf_fiber.c`)

Unfish implements a user-space cooperative multitasking subsystem built on lightweight **fibers**:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        FIBER SCHEDULING RUNTIME                        │
├────────────────────────────────────────────────────────────────────────┤
│ Scheduler Queue:                                                       │
│   [ Fiber 1 (RUNNING) ] ──► [ Fiber 2 (SUSPENDED) ] ──► [ Fiber 3 ]    │
│                                                                        │
│ Operations:                                                            │
│   • spawn(fn): Creates fiber and appends to scheduler queue            │
│   • yield(): Relinquishes control; moves current fiber to queue tail   │
│   • run_scheduler(): Loops until all fibers run to completion          │
└────────────────────────────────────────────────────────────────────────┘
```

### 5.1. Fiber Data Structure (`UfFiberObject`)
```c
typedef enum {
    UF_FIBER_READY,
    UF_FIBER_RUNNING,
    UF_FIBER_SUSPENDED,
    UF_FIBER_COMPLETED,
    UF_FIBER_FAILED
} UfFiberState;

typedef struct {
    UfObj obj;
    UfFiberState state;
    UfValue function;           /* Fiber entry function */
    UfValue value_stack[1024];  /* Dedicated fiber operand stack */
    size_t stack_top;
    UfCallFrame frames[64];     /* Dedicated fiber call stack */
    size_t frame_count;
    struct UfFiberObject* next; /* Scheduler queue link */
} UfFiberObject;
```

### 5.2. Communication Channels (`UfChannelObject`)
Channels provide thread-safe FIFO message queues for passing values between fibers:

```c
typedef struct {
    UfObj obj;
    size_t capacity;
    size_t count;
    size_t head;
    size_t tail;
    bool is_closed;
    UfValue* buffer;            /* Ring buffer */
} UfChannelObject;
```

* `channel(capacity)`: Initializes a FIFO channel with an optional buffer capacity hint.
* `send(ch, value)`: Enqueues `value` onto the channel.
* `recv(ch)`: Dequeues and returns the next value. Returns `null` if the channel is empty and closed.
* `close_channel(ch)`: Closes the channel to further `send` operations.

### 5.3. Promises & Asynchronous Execution (`UfPromiseObject`)
Asynchronous functions (`async function`) return a `Promise`:
* **States**: `UF_PROMISE_PENDING`, `UF_PROMISE_RESOLVED`, `UF_PROMISE_REJECTED`.
* **Resolution**: An `await` expression checks the promise state:
  * If resolved, returns the unwrapped result immediately.
  * If pending, cooperatively suspends the active fiber until the promise is fulfilled by the event loop.
