# UNFISH — SECURITY & EXECUTION SANDBOXING

---

## 1. Threat Model

Unfish is designed to execute educational programs, code authored by beginners, and code shared across collaborative classroom environments.

Consequently:
1. Student code must not have arbitrary access to the host machine's filesystem, network, or process table.
2. Accidental infinite loops or deep recursion must not lock up the host process or cause denial-of-service.
3. Memory corruption vulnerabilities (buffer overflows, use-after-free) in the C runtime must be strictly prevented.

## 2. Resource Limits & Guardrails

The runtime environment enforces configurable execution quotas:
* **Call Stack Depth Limit**: Maximum call depth (default: 500 frames) prevents C stack overflows from unbounded recursion. Exceeding this triggers a clean `StackOverflowError`.
* **Execution Step Quota**: Maximum statement/loop execution counter (default: 1,000,000 steps in educational mode) prevents unresponsive infinite loops (`while true: null`).
* **Memory Allocation Quota**: Total bytes allocated through runtime arenas and heaps are capped. Exceeding the quota triggers an `OutOfMemoryError`.

## 3. Host API Isolation

* File I/O functions in the standard library are sandboxed to a virtual or designated sandbox directory.
* Direct system execution (`system()`, `fork()`, `exec()`) is completely disallowed in standard Unfish.
