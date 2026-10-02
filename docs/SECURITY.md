# UNFISH — SECURITY ARCHITECTURE, SANDBOXING & RESOURCE LIMITATION MANUAL

---

## 1. Executive Summary & Security Philosophy

As an educational and general-purpose programming language, Unfish is frequently deployed in multi-tenant environments: online coding playgrounds, automated grading servers, educational classrooms, and web browsers.

In these environments, executing untrusted user code poses severe security hazards:
* **Host System Compromise**: Unauthorized access to host files, environment variables, or process spawning.
* **Denial of Service (DoS)**: Malicious or accidental infinite loops, fork bombs, and memory exhaustion.
* **Memory Safety Vulnerabilities**: Buffer overflows, use-after-free, and stack corruption leading to arbitrary code execution.

Unfish mitigates these threats through a defense-in-depth security model built directly into the C99 runtime architecture:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        DEFENSE-IN-DEPTH SECURITY MODEL                 │
├────────────────────────────────────────────────────────────────────────┤
│ Layer 4: WebAssembly & Process Sandboxing (Browser / WASI jail)        │
├────────────────────────────────────────────────────────────────────────┤
│ Layer 3: Capability-Based I/O Permissions (Restricted fs and sys APIs) │
├────────────────────────────────────────────────────────────────────────┤
│ Layer 2: Resource Quotas (Call depth, memory caps, gas counters)       │
├────────────────────────────────────────────────────────────────────────┤
│ Layer 1: Memory Safety in Pure C99 (Zero-leak GC, strict bounds checks)│
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Memory Safety Guarantees

Although the Unfish runtime is implemented in ANSI C99, **Unfish code cannot trigger memory corruption or undefined behavior**:

### 2.1. Strict Bounds Checking
Every container access in Unfish is bounds-checked at runtime:
* **Array Indexing**: Accessing `arr[i]` where $i < 0$ or $i \ge \text{len}$ raises `ERR_INDEX_OUT_OF_BOUNDS` (exit code 3) rather than reading garbage memory.
* **Raw Byte Buffers**: `buffer_get(buf, offset)` and `buffer_set(buf, offset, val)` perform explicit range validation against `buf->capacity`. Out-of-range reads return `0` or raise structured exceptions.
* **String Slicing & Substrings**: Validated against character lengths, preventing memory leakage beyond the string buffer.

### 2.2. Pointer Safety & Isolation
Unfish does not expose raw pointers, pointer arithmetic, or manual memory deallocation (`free`) to the programmer. All object allocation is managed through the garbage collector and arena allocators. Dangling pointers and double-free vulnerabilities are impossible within Unfish programs.

### 2.3. AddressSanitizer (ASan) Continuous Verification
The entire Unfish test suite (95+ differential tests, stress tests, and unit tests) is continuously built and executed under AddressSanitizer (`-fsanitize=address`) and UndefinedBehaviorSanitizer (`-fsanitize=undefined`). Any memory violation or leak immediately fails the build pipeline.

---

## 3. Denial of Service Defenses & Resource Quotas

To prevent untrusted scripts from exhausting server resources, Unfish enforces strict resource limits:

### 3.1. Call Stack Recursion Guard (`UF_MAX_CALL_FRAMES`)
Uncontrolled recursion is blocked by an explicit frame limit of 512, enforced identically by every backend (`src/runtime/uf_runtime.c`):
```c
#define UF_MAX_CALL_FRAMES 512

if (rt->frame_count >= UF_MAX_CALL_FRAMES) {
    uf_runtime_error(rt, call_span, "StackOverflowError: Maximum call stack depth exceeded (%d frames)", UF_MAX_CALL_FRAMES);
    return false;
}
```
The stack VM and register VM use the same limit (`UF_VM_FRAMES_MAX_CALLS`, `UF_REGVM_FRAMES_MAX_CALLS`), and native/WebAssembly binaries count frames in `uf_rt_enter_frame()` with an additional 4 MB C-stack budget. The error is a catchable `StackOverflowError`, and this guarantees that infinite recursive functions cannot exhaust the host C thread stack or crash the host process with a `SIGSEGV`.

### 3.2. Execution Gas & Instruction Counting
For multi-tenant playground hosting, Unfish supports instruction gas metering:
* Every loop iteration (`while`, `for`, `repeat`) and function call decrements an execution gas counter.
* When gas reaches zero, execution is aborted immediately with a `QuotaExceededError`.
* This completely eliminates CPU starvation caused by `while true:` loops.

### 3.3. Memory Allocation Quotas (`UF_MAX_MEMORY`)
The garbage collector tracks total active heap allocation (`rt->bytes_allocated`):
* If total memory exceeds the configured maximum heap quota (e.g. 32 MB for playground servers), `uf_gc_alloc()` triggers a collection.
* If memory remains above the quota following a collection cycle, the runtime raises an `OutOfMemoryError` and aborts execution cleanly without crashing the host.

---

## 4. Capability-Based Sandboxing & I/O Isolation

Unfish separates pure computation from host environment side effects. In sandboxed mode (such as online playgrounds or WebAssembly deployment):

### 4.1. Process Spawning Guard
* `sys.exec(cmd)` is disabled: calls immediately return `-1` without spawning child processes.
* Forking and process replacement are strictly prohibited.

### 4.2. Environment Variable Isolation
* `sys.set_env()` is disabled, preventing untrusted scripts from modifying host environment variables (e.g. `LD_PRELOAD`, `PATH`).
* `sys.env(name)` can be configured to expose only a curated whitelist of environment variables.

### 4.3. Filesystem Path Jailing
When operating in restricted mode:
* Direct filesystem access via the `fs` module is either disabled entirely or restricted to a designated sandbox directory (jail).
* Path traversal attempts containing `../` or absolute root paths (`/etc/passwd`, `C:\Windows`) are rejected by canonical path normalization.

---

## 5. WebAssembly Browser Sandbox Guarantees

When compiled to WebAssembly (`unfish build --wasm`), Unfish runs inside the browser's hardware-isolated virtual machine:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        WEBASSEMBLY SANDBOX BOUNDARY                    │
├────────────────────────────────────────────────────────────────────────┤
│ Browser JavaScript Environment (DOM, LocalStorage, Fetch)              │
│       │                                                                │
│       │ Strict JSON-RPC / Function Call Bridge                         │
│       ▼                                                                │
│ WebAssembly Linear Memory Sandbox (4 GB Isolated Address Space)        │
│       • Unfish C99 Runtime                                             │
│       • Linear Arenas & GC Heap                                        │
│       • Zero access to host filesystem, network sockets, or OS kernels │
└────────────────────────────────────────────────────────────────────────┘
```

1. **Memory Isolation**: WebAssembly code operates exclusively within an isolated linear memory buffer. It cannot access host process memory or read arbitrary memory addresses.
2. **System Call Neutralization**: WASI system calls (`fd_write`, `clock_time_get`) are intercepted by a lightweight JavaScript adapter that routes output exclusively to the virtual console.
3. **Safe Multi-Tenant Evaluation**: Educational web platforms can execute student code entirely on the client side inside the browser, eliminating server-side compute costs and security vulnerabilities.
