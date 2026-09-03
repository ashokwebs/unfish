# UNFISH — RUNTIME ARCHITECTURE

---

## 1. Feature Status Inventory

| Runtime Subsystem | Status | Implementation Reference |
|---|---|---|
| Lexical Environments (`UfEnv`) | **IMPLEMENTED** | `src/runtime/uf_env.c` |
| Environment Parent Chaining | **IMPLEMENTED** | `src/runtime/uf_env.c` |
| First-Class Functions & Closures | **IMPLEMENTED** | `src/runtime/uf_value.c` |
| Top-Level Function Hoisting | **IMPLEMENTED** | `src/interpreter/uf_interpreter.c` |
| Native Function Bindings | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| Call Stack & Backtrace Tracker | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| Stack Overflow Guard (512 frames) | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| Execution Step Quota Guard | **IMPLEMENTED** | `src/interpreter/uf_interpreter.c` |
| Temporary Root Protection Stack | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| Mark-and-Sweep Garbage Collection | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| Visual Execution Observer Hooks | **PLANNED** (Phase 6) | Debugger milestone |
| Bytecode Virtual Machine Loop | **PLANNED** (Phase 7) | VM milestone |

---

## 2. Core Runtime Components

### 2.1. Environments (`UfEnv`) [IMPLEMENTED]
Lexical scoping is modeled as a hierarchy of environment frames:
```c
struct UfEnv {
    UfObj obj;
    struct UfEnv* parent;       // Enclosing scope pointer
    UfEnvBinding** buckets;     // Hash table of bindings
    size_t bucket_count;
    size_t count;
};
```
* **Declaration (`let x = v`)**: Inserts `x` into the current frame.
* **Lookup (`x`)**: Searches current frame; if not found, recurses up `parent`.
* **Assignment (`x = v`)**: Searches upward for the nearest frame binding `x` and updates it.

### 2.2. Call Stack & Activation Records [IMPLEMENTED]
Function calls create an activation record on the runtime call stack:
```c
typedef struct {
    const char* fn_name;
    SourceSpan call_span;
    UfEnv* env;
} UfCallFrame;
```
If recursion exceeds `UF_MAX_CALL_FRAMES` (512), a `StackOverflowError` is triggered. On any runtime error, the call stack renders a clean traceback with source locations.

### 2.3. Native Functions (`UfNativeFn`) [IMPLEMENTED]
Host capabilities are exposed via native function bindings:
```c
typedef UfValue (*UfNativeFn)(UfRuntime* rt, int argc, UfValue* args);
```
Built-ins (`say`, `print`, `type_of`, `len`, `clock`, `assert`) are registered into `global_env` at runtime initialization.
