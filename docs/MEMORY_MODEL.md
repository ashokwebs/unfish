# UNFISH — MEMORY MODEL SPECIFICATION

---

## 1. Feature Status Inventory

| Component | Status | Implementation Reference |
|---|---|---|
| Contiguous Compilation Arena (`UfArena`) | **IMPLEMENTED** | `src/common/uf_arena.c` |
| Tagged Union Value Representation (`UfValue`) | **IMPLEMENTED** | `src/runtime/uf_value.h` |
| Heap Object Tracking Header (`UfObj`) | **IMPLEMENTED** | `src/runtime/uf_object.h` |
| Mark-and-Sweep Garbage Collector | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| Temporary Root Protection Stack (`temp_roots`) | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| Active Block Scope Root Tracking (`current_env`) | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| Dynamic Threshold GC Triggering | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| Generational / Compacting GC | **PLANNED** (Phase 7) | Future VM GC |
| Explicit Memory Buffers (`Buffer`) | **NOT IMPLEMENTED** | Deferred to Phase 10 |
| Raw Pointers & Unsafe Blocks | **NOT IMPLEMENTED** | Deferred to Phase 10 |

---

## 2. Implemented Memory Architecture

The Unfish memory model consists of two cleanly separated allocators:

```
Compilation Stage (Arena Allocator)         Execution Stage (GC Managed Heap)
┌──────────────────────────────────────┐    ┌──────────────────────────────────────┐
│ UfArena                              │    │ UfRuntime                            │
│  - Contiguous 64KB Chunks            │    │  - Tagged union values (on C stack)  │
│  - O(1) Bump Allocation              │    │  - Heap objects linked via UfObj:    │
│  - Tokens, Source Spans, AST Nodes   │    │      * UfStringObject                │
│  - O(1) Bulk Teardown on Parse End   │    │      * UfFunctionObject              │
│                                      │    │      * UfEnv (Lexical frames)        │
└──────────────────────────────────────┘    │  - Mark-and-Sweep Garbage Collector  │
                                            └──────────────────────────────────────┘
```

### 2.1. Compilation Arena (`UfArena`) [IMPLEMENTED]
All compiler artifacts (tokens, interned identifier strings, AST nodes) are allocated linearly inside contiguous chunks. When compilation finishes, the entire arena is reclaimed in O(1) without traversing individual AST nodes.

### 2.2. Runtime Garbage Collection (`uf_gc_collect`) [IMPLEMENTED]
The runtime uses an object-tracked mark-and-sweep garbage collector:
1. **Header**: Every heap-allocated runtime object begins with `UfObj { UfObjKind kind; bool marked; struct UfObj* next; }`.
2. **Root Set**:
   * `rt->global_env`: Global variables and hoisted functions.
   * `rt->current_env`: The currently executing block environment and its parent chain.
   * `rt->frames[i].env`: The activation record environment of each active call frame.
   * `rt->temp_roots`: Temporary evaluation values held in C local variables during expression evaluation.
3. **Circular Closure References**: Handled correctly. Closures retain their enclosing environment, and environments can bind those closures without leaking memory.
4. **Triggering**: Runs automatically when `bytes_allocated > next_gc_threshold`.

---

## 3. Systems Progression Roadmap

* **Level 6 (Pedagogical)**: Students inspect active heap objects, allocation counters, and GC mark-and-sweep cycles via educational hooks.
* **Level 10 (Systems)**: Unfish introduces explicit memory buffers, manual allocations, and pointer visualization.
