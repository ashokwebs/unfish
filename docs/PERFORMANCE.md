# Unfish Performance Benchmarks & Baselines

This document tracks execution speed, memory footprint, and optimization targets across Unfish implementation phases.

## 1. Workload Suite

| Benchmark | Workload & Target Subsystem | Complexity |
|---|---|---|
| `01_fib_recursive` | Recursive `fib(25)` measuring function call frame push/pop overhead, environment allocation, and recursion limits. | Tree recursion ($O(2^n)$ calls) |
| `02_loop_counter` | 100,000 loop iterations measuring tight loop execution, binary addition, and variable assignment. | Linear loop ($10^5$ iterations) |
| `03_string_concat` | 2,000 string concatenations measuring dynamic heap allocations, string interning, and GC sweep cycles. | Heap allocation & GC |
| `04_array_sort` | Bubble sorting a reversed array of 500 integers measuring array indexing, bounds checking, and in-place element swapping. | Quadratic sort ($O(n^2)$ swaps) |
| `05_map_operations` | 5,000 dynamic hash map insertions followed by 5,000 hash map lookups and sum accumulation. | Hash table hashing & collision chaining |
| `06_closure_capture` | 20,000 invocations of a captured closure (`make_adder`) measuring lexical environment traversal. | Closure call dispatch |

---

## 2. Baseline Measurements (Phase 4: Tree-Walk AST Interpreter)

Hardware: x86_64 Linux, GCC 11 / Clang with `-O2`.

| Benchmark | Elapsed Time (Tree-Walk AST) | Status |
|---|---|---|
| `01_fib_recursive` | **0.845 s** | PASS |
| `02_loop_counter` | **0.230 s** | PASS |
| `03_string_concat` | **0.026 s** | PASS |
| `04_array_sort` | **0.843 s** | PASS |
| `05_map_operations` | **0.061 s** | PASS |
| `06_closure_capture` | **0.102 s** | PASS |

---

## 3. Running Benchmarks

Run the benchmark suite at any time via:

```bash
make bench
```

Or execute directly with custom binaries:

```bash
./tools/run_benchmarks.sh bin/unfish
```

---

## 4. Architectural Analysis & Optimization Targets for Bytecode VM (Section Q)

The tree-walk AST interpreter achieves solid baselines for an educational language. Profiling identifies the following primary overheads to be eliminated in the upcoming Bytecode VM:
1. **Pointer chasing through AST node graphs**: Flattening AST statements into a dense 1D bytecode array will eliminate cache misses.
2. **Environment hash table lookups for local variables**: Resolving local variables to fixed stack slots at compile-time will turn variable reads and writes into direct array index accesses.
3. **C call stack frames for Unfish function invocations**: A dedicated value stack with lightweight call frames (`UfCallFrame`) will eliminate C recursion limits and overhead.
