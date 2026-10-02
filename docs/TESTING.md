# UNFISH — TESTING STRATEGY, DIFFERENTIAL VERIFICATION & QA MANUAL

---

## 1. Executive Quality Assurance Philosophy

A programming language implementation that powers educational curricula, compilers courses, and production applications cannot tolerate subtle backend divergences or memory corruption.

Unfish enforces a **Zero-Compromise Verification Philosophy** built around four rigorous testing pillars:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        THE 4-PILLAR TESTING HIERARCHY                  │
├────────────────────────────────────────────────────────────────────────┤
│ Pillar 4: Fuzzing & Stress Testing (GC churn, deep recursion, AFL)     │
├────────────────────────────────────────────────────────────────────────┤
│ Pillar 3: Memory Safety Sanitizers (ASan, LSan, UBSan zero-leak rule)  │
├────────────────────────────────────────────────────────────────────────┤
│ Pillar 2: 5-Way Differential Parity Verification (95/95 conformance)   │
├────────────────────────────────────────────────────────────────────────┤
│ Pillar 1: Modular C Unit Tests (20+ isolated test suites in tests/unit)│
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Pillar 1: Modular C Unit Testing (`tests/unit/`)

Individual C modules are tested in isolation using lightweight, standalone test drivers in `tests/unit/`. Each unit test binary is compiled directly with `make test` and verifies module contracts without launching the full CLI:

| Unit Test File | Target Subsystem | Key Verification Invariants |
|---|---|---|
| `test_lexer.c` | `src/lexer/uf_lexer.c` | Token stream generation, indentation off-side rules, escape sequences |
| `test_parser.c` | `src/parser/uf_parser.c` | Pratt operator precedence, block structure parsing, AST validity |
| `test_semantic.c`| `src/semantic/uf_semantic.c` | Scope binding, variable hoisting, gradual typing, trait checking |
| `test_interpreter.c`| `src/interpreter/` | Direct AST evaluation, environment resolution, runtime errors |
| `test_chunk.c` | `src/compiler/uf_chunk.c` | Instruction emission, constant pool management, line mapping |
| `test_compiler.c`| `src/compiler/uf_compiler.c` | Bytecode generation, local/upvalue resolution, jump patching |
| `test_vm.c` | `src/vm/uf_vm.c` | 57-opcode Stack VM execution, operand stack bounds, unwinding |
| `test_regvm.c` | `src/vm2/uf_regvm.c` | 256-register RegVM execution, computed-goto dispatch, 3-address ISA |
| `test_optimize.c`| `src/compiler/uf_optimize.c`| Constant folding, dead code elimination, peephole optimizations |
| `test_cache.c` | `src/compiler/uf_cache.c` | `.ufc` / `.ufrc` binary cache serialization and checksum verification |
| `test_emit_c.c` | `src/codegen/uf_emit_c.c` | C99 source code generation, standalone runtime linking |
| `test_wasm.c` | `src/codegen/` | WebAssembly compilation and WASI interface conformance |
| `test_fiber.c` | `src/runtime/uf_fiber.c` | Cooperative fiber scheduling, FIFO queue, channel send/recv |
| `test_systems.c`| `src/runtime/uf_stdlib.c` | Raw byte buffers, endian-explicit reads/writes, bitwise casts |
| `test_debugger.c`| `src/debugger/uf_debugger.c`| Breakpoint registration, single-stepping, stack backtraces |
| `test_formatter.c`| `src/formatter/uf_formatter.c`| Canonical code formatting, idempotency, comment preservation |
| `test_lsp.c` | `src/lsp/uf_lsp.c` | JSON-RPC 2.0 requests, hover information, auto-completion |
| `test_blocks.c` | `src/blocks/` | Bidirectional AST ↔ JSON block round-trip serialization |
| `test_tooling.c`| `src/tooling/` | Package manager, documentation generator, test discovery runner |
| `test_stress.c` | Runtime Subsystems | High-pressure memory allocations and boundary conditions |

---

## 3. Pillar 2: 5-Way Differential Parity Verification

The defining technical feature of the Unfish verification suite is the **Five-Way Differential Testing Harness** (`tools/run_differential_tests.sh`).

### 3.1. The Parity Invariant
Every conformance test in `tests/conformance/` is executed against all five backends:
1. **Tree-Walking AST Interpreter**: `bin/unfish run <test>`
2. **Stack Bytecode VM**: `bin/unfish run --vm <test>`
3. **Register Bytecode VM**: `bin/unfish run --regvm <test>`
4. **Native C99 AOT Binary**: `bin/unfish build -o bin/native_test <test> && ./bin/native_test`
5. **WebAssembly Module**: `bin/unfish build --wasm <test> && node run_wasm.js <test>.wasm`

The test harness captures standard output (`stdout`), standard error (`stderr`), and the process exit code from each execution. All five outputs must be **byte-for-byte identical**:

$$\text{stdout}(\text{Interp}) \equiv \text{stdout}(\text{VM}) \equiv \text{stdout}(\text{RegVM}) \equiv \text{stdout}(\text{Native}) \equiv \text{stdout}(\text{WASM})$$
$$\text{exit}(\text{Interp}) \equiv \text{exit}(\text{VM}) \equiv \text{exit}(\text{RegVM}) \equiv \text{exit}(\text{Native}) \equiv \text{exit}(\text{WASM})$$

If a single character, newline, or exit code diverges between any backend, the test harness reports an immediate failure with a unified diff.

### 3.2. Current Conformance Census
As of v2.1.0, **95 out of 95 conformance tests pass with 100% differential parity across all five execution engines**:
* 68 Valid Language Feature Tests (`01_hello.unfish` through `68_fish_simulation.unfish`)
* 27 Intentional Error Tests (`err_arity_mismatch.unfish` through `err_unterminated_string.unfish`)

---

## 4. Pillar 3: Memory Safety Sanitizer Automation

Unfish is engineered to run permanently clean under LLVM and GCC sanitizers:

```bash
make test-asan
```

### Sanitizer Suite:
1. **AddressSanitizer (ASan, `-fsanitize=address`)**: Detects heap out-of-bounds access, stack buffer overflows, global buffer overflows, and heap-use-after-free bugs.
2. **LeakSanitizer (LSan, `-fsanitize=leak`)**: Asserts that during runtime termination (`uf_runtime_free()`), every single heap-allocated `UfObj` and arena chunk is completely freed with **zero memory leaks**.
3. **UndefinedBehaviorSanitizer (UBSan, `-fsanitize=undefined`)**: Detects signed integer overflows, misaligned pointer accesses, null pointer dereferences, and invalid bit shift operations.

---

## 5. Pillar 4: Stress & Fuzz Testing

To ensure robustness under adversarial workloads, Unfish incorporates specialized stress suites:

1. **GC Churn & Pressure Tests (`10_gc_pressure_loop.unfish`, `tests/benchmarks/12_gc_pressure.unfish`)**: Allocates millions of transient objects with garbage collection thresholds set to minimal values (e.g. 1 KB or on every allocation). Verifies that temporary evaluation roots (`rt->temp_roots`) and active block scopes (`rt->current_env`) prevent premature collection of live values.
2. **Hash Map Collision & Churn Stress (`60_map_churn.unfish`, `tests/benchmarks/11_hash_map_stress.unfish`)**: Inserts and deletes 100,000 keys with identical hash prefixes to verify open-addressed quadratic probing, tombstone reuse, and dynamic table resizing.
3. **Deep Call Stack Recursion (`tests/benchmarks/13_deep_recursion.unfish`)**: Recursively calls functions up to the 512-frame `UF_MAX_CALL_FRAMES` limit to verify recursion guards and frame unwinding without stack overflows.
4. **Circular Structure Serialization (`40_cyclic_structures.unfish`, `41_json_cycle_detection.unfish`)**: Verifies that cyclic arrays, maps, and closure graphs do not cause infinite loops in the GC or `json.stringify()`.
