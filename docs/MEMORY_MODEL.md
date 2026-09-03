# UNFISH — MEMORY MODEL SPECIFICATION

---

## 1. Architectural Memory Layout

The Unfish runtime models memory explicitly to serve both execution efficiency and pedagogical inspectability:

```
┌─────────────────────────────────────────────────────────┐
│                    STACK SEGMENT                        │
│  ┌───────────────────────────────────────────────────┐  │
│  │ Call Frame 0 (global)                             │  │
│  │   Env: global_env_ptr                             │  │
│  │   IP: statement 12                                │  │
│  ├───────────────────────────────────────────────────┤  │
│  │ Call Frame 1: greet("World")                      │  │
│  │   Locals: person -> [String: "World"]             │  │
│  │   Return Address: frame 0                         │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                            │ (References)
                            ▼
┌─────────────────────────────────────────────────────────┐
│                     HEAP SEGMENT                        │
│  ┌─────────────────────────┐   ┌─────────────────────┐  │
│  │ UfString                │   │ UfEnvironment       │  │
│  │   ref_count: 1          │   │   parent: ptr       │  │
│  │   length: 5             │   │   bindings: map     │  │
│  │   chars: "World\0"      │   └─────────────────────┘  │
│  └─────────────────────────┘   ┌─────────────────────┐  │
│                                │ UfFunction          │  │
│                                │   closure_env: ptr  │  │
│                                │   ast_node: ptr     │  │
│                                └─────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

## 2. Value Representation (`UfValue`)

To minimize indirection and achieve optimal cache locality while keeping C code clean, values are represented as tagged unions:

```c
typedef struct UfValue {
    UfValueKind kind;
    union {
        bool boolean;
        double number;
        struct UfString* string;
        struct UfFunction* function;
        struct UfNativeFn* native_fn;
        void* obj;
    } as;
} UfValue;
```

* Primitives (`Null`, `Boolean`, `Number`) reside directly inside `UfValue` on the stack or inside environments without heap allocations.
* Reference types (`String`, `Function`, `NativeFunction`, and future `Array`/`Map`) point to heap-allocated headers.

## 3. Allocation Strategies

### 3.1. Compilation Arena Allocator (`UfArena`)
All compiler artifacts (tokens, source snippets, AST expressions, statements, symbol tables) are allocated inside a linear chunk arena.
* **Property**: O(1) allocation without per-node `malloc` overhead.
* **Lifecycle**: When parsing/compilation finishes, the entire arena is reclaimed in a single pass (`uf_arena_free`), eliminating AST memory leaks entirely.

### 3.2. Runtime Environment & Heap Lifecycles
* In Phase 1–4 (Tree-Walk / Initial Engine), the runtime manages environments and heap objects with explicit lifecycle tracking and automatic teardown upon VM / environment release.
* In Phase 7 (VM), a generational or mark-sweep Garbage Collector will trace the call stack, global environments, and active value registers.

## 4. Systems Progression Roadmap

As students progress to advanced computer science:
* **Level 6**: Students inspect the runtime heap and environment frames directly in the debugger visualizer.
* **Level 10**: Unfish introduces explicit memory buffers (`Buffer`), raw pointer representations (`Ptr<T>`), and manual allocation primitives for systems programming education.
