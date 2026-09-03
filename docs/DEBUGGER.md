# UNFISH — DEBUGGER & EXECUTION VISUALIZATION

---

## 1. Vision: Transparent Execution

In typical languages, execution is an invisible black box. For educational clarity, Unfish makes the runtime state inspectable at every step.

```
       Source Code                     Visualizer / Debugger UI
 ┌──────────────────────┐              ┌───────────────────────────┐
 │ let x = 10           │              │ Call Stack:               │
 │ let y = x + 5   <────┼──[Event]────►│   frame 0: <main>         │
 │ say y                │              │ Variables:                │
 └──────────────────────┘              │   x: 10                   │
                                       │   y: 15                   │
                                       └───────────────────────────┘
```

## 2. Debugger Event Protocol

The interpreter and VM incorporate hook interfaces that emit structured lifecycle events without contaminating language semantics:

```c
typedef enum {
    UF_DEBUG_STEP_STATEMENT,
    UF_DEBUG_CALL_ENTER,
    UF_DEBUG_CALL_EXIT,
    UF_DEBUG_VAR_BIND,
    UF_DEBUG_VAR_ASSIGN,
    UF_DEBUG_ERROR
} UfDebugEventType;

typedef void (*UfDebugHook)(UfVM* vm, UfDebugEventType event, void* event_data, void* user_ctx);
```

## 3. Core Debugger Capabilities (Phase 6 Roadmap)
1. **Breakpoints**: Line-based or conditional breakpoints on expressions.
2. **Stepping**:
   * *Step Over*: Execute current statement without entering nested function calls.
   * *Step Into*: Step into function invocations.
   * *Step Out*: Run until current activation record completes.
3. **Variable Inspection**: Query bindings across lexical environment frames.
4. **Call Stack Backtrace**: Inspect caller chain and activation frames.
