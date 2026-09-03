# UNFISH — RUNTIME ARCHITECTURE

---

## 1. Overview

The Unfish runtime (`src/runtime/`) manages all state during program execution. It remains completely decoupled from the parser, AST, and CLI.

## 2. Core Runtime Components

### 2.1. Environments (`UfEnv`)
Lexical scoping is modeled as a hierarchy of environment frames:
```c
typedef struct UfEnv {
    struct UfEnv* parent;       // Enclosing scope pointer
    UfSymbolTable bindings;     // Key-value map of variable names to UfValue
    bool is_function_root;      // Flag denoting function call boundary
} UfEnv;
```
* **Declaration (`let x = v`)**: Inserts `x` into the current environment.
* **Lookup (`x`)**: Searches the current environment; if not found, recurses up `parent`.
* **Assignment (`x = v`)**: Searches upward for the nearest frame binding `x` and updates it.

### 2.2. Call Stack & Activation Records
Function calls create an activation record on the runtime call stack:
```c
typedef struct UfCallFrame {
    const char* function_name;  // Function identifier (or "<script>")
    SourceLoc call_site;        // Source position where call was issued
    UfEnv* env;                 // Function local environment
} UfCallFrame;
```
When an error occurs, the call stack formats a clean stack backtrace showing the exact file, line, and function call chain.

### 2.3. Native Functions (`UfNativeFn`)
The runtime exposes host capabilities strictly via native function bindings:
```c
typedef UfValue (*UfNativeFnPtr)(struct UfVM* vm, int argc, UfValue* args);
```
Standard built-ins (`say`, `print`, `type_of`, `len`, `clock`) are registered into the global environment during runtime initialization.

## 3. Execution Observers & Tracing Hooks
To support the educational visualizer and interactive debugger, the runtime evaluator triggers event callbacks:
* `on_statement_step(SourceSpan span, UfEnv* env)`
* `on_variable_binding(const char* name, UfValue val)`
* `on_function_enter(const char* name, int argc, UfValue* args)`
* `on_function_exit(const char* name, UfValue return_val)`
