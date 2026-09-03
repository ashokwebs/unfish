# UNFISH — PERFORMANCE BENCHMARKING & TARGETS

---

## 1. Measurement Philosophy: Measure, Don't Guess

We do not prematurely optimize at the expense of simplicity or educational clarity. However, we avoid architecting fundamental bottlenecks into the runtime.

## 2. Performance Baseline Targets

| Component | Target Metric | Rationale |
|---|---|---|
| **Lexer** | > 10 MB/s source scanning | Immediate tokenization of multi-thousand line files |
| **Parser** | > 5 MB/s AST construction | Instant syntax analysis in editor and CLI |
| **Arena Allocation** | < 5ns per AST node | Zero per-node heap fragmentation |
| **Interpreter Dispatch** | < 50ns per simple AST node | Smooth execution for classroom simulation and games |
| **Startup Overhead** | < 10ms process start-to-finish | Instant CLI feedback loop |

## 3. Canonical Benchmarks (`tests/benchmarks/`)

1. **`fib_recursive.unfish`**: Measures function call overhead, stack frame allocation, and recursion.
2. **`loop_counter.unfish`**: Measures arithmetic dispatch and loop condition evaluation.
3. **`string_concat.unfish`**: Measures heap allocation, string buffer reallocation, and garbage collection pressure.
