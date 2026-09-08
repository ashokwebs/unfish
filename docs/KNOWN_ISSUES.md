# UNFISH — KNOWN ISSUES & LIMITATIONS

**Last Updated:** Phase 1 Hardening Complete (v1.1.0)

---

## 1. Resolved Limitations (Historical)

1. **Bytecode VM & Native Compilation**: Completed. Unfish features a stack-based Bytecode VM (`--vm`) and a native C99 backend (`build`) with 100% differential parity against the AST tree-walk interpreter.
2. **Gradual Type Checking**: Completed. Optional gradual types (`Number`, `String`, `Boolean`, `Array`, `Map`, `Function`, `Null`, `Any`, `Error`, `Buffer`, `Channel`, `Fiber`) are verified in semantic analysis (`check --strict`).
3. **Key-Value Hash Maps**: Completed. First-class Robin Hood hashed maps with JSON serialization, cycle detection, and tombstone compaction.
4. **Cooperative Concurrency**: Completed. Cooperative lightweight fibers (`spawn`, `yield`, `run_scheduler`) and channel-based communication (`channel`, `send`, `recv`, `close_channel`).

## 2. Active Considerations & Architecture Notes

1. **Fiber Scheduling Model**: Concurrency is single-threaded and cooperative (green threads / coroutines). Scheduling switches occur deterministically at explicit `yield()` points and channel blocking/queue boundaries; there is no preemptive OS-level time-slicing.
2. **C99 Portability**: All code compiles cleanly with `-Wall -Wextra -Werror -pedantic -std=c99` with zero external dependencies beyond libc and `-lm`.
3. **Memory Management**: Exact mark-sweep GC runs across environments, evaluation frames, temp roots, and allocated heap objects.

## 3. Verified Edge Cases
* Cyclic JSON graph serialization is detected and raises an error instead of infinite loops.
* Floating-point to integer conversions clamp and guard against NaN / Inf undefined behavior across backends.
* Multi-byte little-endian buffer reads/writes bounds-checked against buffer capacity.
* Shadowing of builtin names across nested lexical scopes is properly resolved.
