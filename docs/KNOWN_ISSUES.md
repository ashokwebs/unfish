# UNFISH — KNOWN LIMITATIONS, EDGE CASES & WORKAROUND GUIDE

---

## 1. Executive Summary & Transparency Contract

In accordance with the Unfish engineering philosophy of **Glass Box Transparency**, this document provides an honest, comprehensive account of current language limitations, edge cases, implementation boundaries, and recommended workarounds.

Rather than hiding design trade-offs, we document them thoroughly so educators, students, and engineers can navigate them effectively.

---

## 2. Concurrency: Fiber Scheduling & Channel Semantics

### 2.1. Run-to-Completion Fiber Execution
* **Current Behavior**: The cooperative fiber scheduler (`run_scheduler()`) currently executes spawned fibers to completion in spawn order. While `yield()` is recognized as a scheduling point, the current runtime does not preemptively interleave execution across active fibers unless explicit channel reads (`recv`) or promise awaits occur.
* **Architectural Trade-off**: Preemptive multitasking introduces race conditions, memory synchronization overhead, and non-deterministic execution paths that complicate learning for introductory students.
* **Mental Model**: Treat Unfish fibers as **cooperative synchronous generators** with FIFO message passing queues rather than OS-preempted threads.
* **Workaround**: Structure asynchronous workflows using message channels (`channel`, `send`, `recv`) or promises (`async function`, `await`), which yield control deterministically.

### 2.2. Non-Blocking Channel Backpressure
* **Current Behavior**: The capacity argument in `channel(capacity)` acts as an initial ring buffer allocation hint. It is not enforced as a blocking backpressure limit. A call to `send(ch, val)` appends to the buffer and never blocks the sender.
* **Impact**: In a producer-consumer scenario where a producer generates data much faster than a consumer processes it, the channel buffer will grow dynamically in heap memory rather than pausing the producer.
* **Workaround**: When buffering large data streams, implement explicit acknowledgment tokens or chunking rather than assuming bounded backpressure.

---

## 3. Numeric Precision & Integer Arithmetic

### 3.1. IEEE 754 Floating-Point Representation
* **Current Behavior**: All numbers in Unfish are represented internally as 64-bit IEEE 754 double-precision floating-point values (`double`).
* **Integer Bounds**: Exact integer arithmetic without precision loss is guaranteed up to $2^{53} - 1$ ($\pm 9,007,199,254,740,991$). Integers exceeding this threshold lose least significant bits due to mantissa limits.
* **Floating-Point Imprecision**: Classic IEEE 754 fractional representations apply:
  ```unfish
  say 0.1 + 0.2 == 0.3 # false (evaluates to 0.30000000000000004)
  ```
* **Workaround**: When comparing floating-point values, use an epsilon comparison:
  ```unfish
  function approx_eq(a, b, eps = 1e-9):
      return abs(a - b) < eps
  ```

### 3.2. Bitwise Operation Truncation
* **Current Behavior**: All bitwise operations (`band`, `bor`, `bxor`, `shl`, `shr`, `sar`) and integer casting functions (`u8` through `u32`, `i8` through `i32`) convert the underlying `double` to 32-bit signed or unsigned integers (`int32_t` / `uint32_t`).
* **Impact**: Bit shifts and bitwise masking cannot operate on 64-bit bit patterns in a single operation.
* **Workaround**: For 64-bit binary data, use raw contiguous byte buffers (`buffer(8)`) and explicit little-endian multi-byte operations (`buffer_read_u32_le` for lower and upper 32-bit words).

---

## 4. Recursion & Tail Call Optimization (TCO)

### 4.1. Recursion Depth Limit (512 frames)
* **Current Behavior**: To prevent host process stack overflows and crashes, every backend — AST interpreter (`UF_MAX_CALL_FRAMES`), both bytecode VMs, native C99 and WebAssembly — enforces the same maximum call depth of **512 frames**. A call chain up to 512 frames deep runs identically everywhere; the 513th nested call raises a catchable `StackOverflowError`. (Native and WebAssembly binaries additionally stop at 4 MB of C stack, which only matters for unusually large frames.)
* **Tail Call Optimization Status**: Unfish does not currently perform automatic tail call elimination (TCO) in either the AST interpreter or the bytecode virtual machines.
* **Impact**: Deeply recursive functions (such as naive traversal of a 10,000-element linked list or deep tree) will trigger a runtime error:
  ```
  Runtime Error: StackOverflowError: Maximum call stack depth exceeded (512 frames)
  ```
  The error can be caught like any other: `catch err:` sees `err.kind == "StackOverflowError"`.
* **Workaround**: Rewrite deeply recursive algorithms using iterative loops (`while` or `for`) and an explicit heap-allocated array stack:
  ```unfish
  # Instead of deep recursion:
  function process_iterative(root):
      let stack = [root]
      while len(stack) > 0:
          let node = pop(stack)
          # Process node and push children...
  ```

---

## 5. Gradual Typing Dynamic Boundaries

### 5.1. Runtime Blame Tracking
* **Current Behavior**: In gradual typing without `--strict`, passing an unannotated (`Any`) value into a typed function signature (`function f(x: Number)`) performs an immediate type check when entering the function. However, Unfish does not construct higher-order dynamic proxy wrappers (full blame calculus) around function arguments passed across typed/untyped boundaries.
* **Workaround**: To guarantee absolute compile-time and runtime type safety across all boundaries, always invoke the compiler or check tool with the `--strict` flag (`unfish check --strict`).

---

## 6. Filesystem Path Delimiters

### 6.1. Cross-Platform Module Resolution
* **Current Behavior**: Unfish supports Linux, macOS, and Windows. While Windows uses backslashes (`\`) for native filesystem paths, Unfish module import paths should always use canonical forward slashes (`/`):
  ```unfish
  # Recommended across all platforms:
  import "./utils/helpers.unfish" as helpers
  ```
  The module resolver automatically normalizes forward slashes to native Windows separators when running on Windows hosts.

---

## 7. The Browser Engine (Website Playground)

The website runs programs on the real interpreter and VMs compiled to WebAssembly (`web/unfish.wasm`), inside a sandbox. Output matches `unfish run`, with these differences.

### 7.1. Interpreter Recursion Depth
* **Current Behavior**: The tree-walking interpreter recurses in C for every Unfish call, and a browser worker's native stack runs out at about 250 levels in Chrome, before the 512-frame language limit. The page then reports `StackOverflowError` and suggests the VM.
* **Workaround**: Choose the stack VM or register VM engine, which keep their call stack in memory and allow the full 512 frames. The quick-run buttons use the stack VM by default.

### 7.2. Sandbox
* **Current Behavior**: `sys.exec` returns -1, `time.sleep` returns at once, stdin is empty, and files written with the `fs` module live in an in-memory filesystem that lasts for one run. Programs still stopped after 10 seconds are terminated. `inspect()` reports 32-bit object sizes.
* **Workaround**: Use the command-line `unfish` for programs that need processes, real time, input or lasting files.

