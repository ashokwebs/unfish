# UNFISH — NATIVE C99 TRANSPILER, WEBASSEMBLY & EMBEDDED ARCHITECTURE MANUAL

---

## 1. Executive Summary & Philosophy

In addition to its high-speed bytecode virtual machines, Unfish provides an Ahead-of-Time (AOT) compilation pipeline that transpiles Unfish source code directly into **pure, standalone ANSI C99**:

* **Native Native Performance**: Transpiled C code compiles with standard C compilers (`gcc`, `clang`) at `-O3`, delivering execution speeds **25× to 60× faster** than the tree-walking interpreter.
* **Zero Runtime Dependencies**: The generated C code embeds `src/codegen/unfish_runtime.h`, a single-header runtime implementing dynamic values, arrays, hash maps, garbage collection, closures, fibers, and standard library functions without requiring `bin/unfish` or any external shared libraries.
* **Universal Portability**: The transpilation strategy enables Unfish programs to target:
  1. Native desktop and server binaries (Linux x86-64/ARM64, macOS, Windows MinGW).
  2. WebAssembly (`.wasm`) for high-speed browser execution via WASI.
  3. Embedded and bare-metal environments (ARM Cortex-M microcontrollers) via freestanding C compilation.

```
                              ┌───────────────────────────────┐
                              │      Unfish Source Code       │
                              │          (*.unfish)           │
                              └──────────────┬────────────────┘
                                             │
                                             ▼
                              ┌───────────────────────────────┐
                              │    C99 Code Generator         │
                              │    (src/codegen/uf_emit_c.c)  │
                              └──────────────┬────────────────┘
                                             │ Formatted ANSI C99 Code
                                             │ + unfish_runtime.h
                                             ▼
                     ┌───────────────────────────────────────────────┐
                     │            Host C Compiler Toolchain          │
                     └───────┬───────────────────────┬───────────────┘
                             │                       │
           ┌─────────────────┴─────────┐   ┌─────────┴─────────────────┐
           ▼                           ▼   ▼                           ▼
    ┌──────────────┐            ┌──────────────┐                ┌──────────────┐
    │ GCC / Clang  │            │ Clang / WASI │                │ ARM GCC Tool │
    │   Host -O3   │            │   --wasm     │                │   Cortex-M   │
    └──────┬───────┘            └──────┬───────┘                └──────┬───────┘
           │                           │                               │
           ▼                           ▼                               ▼
    Native Machine Code         WebAssembly Binary              Bare-Metal Firmware
    (ELF / Mach-O / PE)             (*.wasm)                        (*.elf)
```

---

## 2. Ahead-of-Time Transpilation Pipeline (`src/codegen/uf_emit_c.c`)

### 2.1. C99 Transpilation Strategy
The C99 code generator traverses the validated AST, translating Unfish language constructs into equivalent C idioms:

1. **Top-Level Declarations**: Function prototypes and struct definitions are emitted first to ensure all symbols are visible before use, preserving Unfish's function hoisting semantics.
2. **First-Class Closures**: Nested functions capturing local variables are lifted into top-level static C functions accepting an explicit environment pointer parameter (`void* __env`). Escaping variables are packed into heap-allocated environment structs.
3. **Control Flow**: Unfish loops (`while`, `for`, `repeat`) and conditionals (`if`, `elif`, `else`) map directly to native C control structures with zero overhead.
4. **Gradual Type Erasure & Specialization**: In `--strict` mode, verified numeric operations are emitted as direct C `double` arithmetic, avoiding tagged union boxing where possible.

### 2.2. Transpilation Example

#### Unfish Source:
```unfish
function fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

say fibonacci(30)
```

#### Transpiled C99 Output (`unfish emit-c`):
```c
#include "unfish_runtime.h"

static UfValue fn_fibonacci(UfRuntime* __rt, void* __env, int __argc, UfValue* __args) {
    if (__argc < 1) return uf_val_null();
    UfValue n = __args[0];

    /* if n <= 1: return n */
    if (uf_val_is_truthy(uf_val_lte(n, uf_val_number(1.0)))) {
        return n;
    }

    /* return fibonacci(n - 1) + fibonacci(n - 2) */
    UfValue _arg1[1] = { uf_val_sub(n, uf_val_number(1.0)) };
    UfValue _call1 = fn_fibonacci(__rt, NULL, 1, _arg1);

    UfValue _arg2[1] = { uf_val_sub(n, uf_val_number(2.0)) };
    UfValue _call2 = fn_fibonacci(__rt, NULL, 1, _arg2);

    return uf_val_add(__rt, _call1, _call2);
}

int main(int argc, char** argv) {
    UfRuntime* rt = uf_runtime_init();

    UfValue args[1] = { uf_val_number(30.0) };
    UfValue result = fn_fibonacci(rt, NULL, 1, args);
    uf_builtin_say(result);

    uf_runtime_free(rt);
    return 0;
}
```

---

## 3. The Standalone Single-Header Runtime (`src/codegen/unfish_runtime.h`)

The emitted C source code does not link against the Unfish compiler or CLI binary. Instead, it embeds `src/codegen/unfish_runtime.h`.

This header is a self-contained systems runtime comprising:
* **Tagged Union Engine**: 16-byte `UfValue` representation with floating-point numbers, booleans, null, strings, arrays, maps, structs, and native function pointers.
* **Memory Management**: An arena memory manager and an intrusive mark-and-sweep garbage collector tracking all heap objects.
* **Dynamic Collections**: Resizable arrays and open-addressed quadratic probing hash maps.
* **Standard Built-ins**: Complete implementations of `say`, `print`, `len`, `type_of`, `split`, `join`, math functions, and buffer primitives.
* **Exception Unwinding**: A setjmp/longjmp exception stack supporting `try`, `catch`, `finally`, and `raise`.

---

## 4. Native Binary Compilation (`unfish build`)

The command:
```bash
unfish build [-o <binary_name>] program.unfish
```
Executes the following automated toolchain workflow:

1. Lexes, parses, and validates `program.unfish`.
2. Emits standalone C99 source code into a temporary file.
3. Invokes the system C compiler (`gcc` or `clang`) with aggressive optimization:
   ```bash
   gcc -O3 -std=c99 -D_POSIX_C_SOURCE=200809L out.c -lm -o binary_name
   ```
4. Cleans up intermediate C files and produces an optimized, standalone native binary.

### Benchmark Comparison: Recursive Fibonacci (n=30)
| Target | Execution Time | Relative Speed |
|---|---|---|
| AST Interpreter (`unfish run`) | 2,420 ms | 1.0× (Baseline) |
| Stack Bytecode VM (`unfish run --vm`) | 680 ms | 3.6× Faster |
| Register Bytecode VM (`unfish run --regvm`)| 490 ms | 4.9× Faster |
| Native Binary (`unfish build -O3`) | **52 ms** | **46.5× Faster** |

---

## 5. WebAssembly Compilation Pipeline (`build --wasm`)

Unfish can compile any script directly to WebAssembly for execution in web browsers, Node.js, or serverless edge runtimes:

```bash
unfish build --wasm [-o output.wasm] program.unfish
```

### 5.1. Compilation Architecture
1. The compiler generates standalone C99 source code.
2. The compiler invokes `clang` with the WebAssembly target and links against the WebAssembly System Interface (WASI) sysroot:
   ```bash
   clang --target=wasm32-wasi --sysroot=scratch/wasi-sysroot -O3 -nostartfiles \
         -Wl,--no-entry -Wl,--export-all -o output.wasm out.c
   ```
3. The resulting `.wasm` binary has zero host dependencies and interacts with the host environment through standardized WASI system calls (`fd_write`, `clock_time_get`).

### 5.2. Running Under Node.js
```bash
node --experimental-wasi-unstable-preview1 runner.js output.wasm
```

### 5.3. Running in Modern Web Browsers
The Unfish Studio web environment loads the compiled `.wasm` module directly using the WebAssembly JavaScript API, routing standard output to the Studio terminal emulator with millisecond latency.

---

## 6. Embedded & Bare-Metal Compilation (`--embedded`, `--arm`)

For microcontrollers and resource-constrained systems, Unfish provides an embedded compilation profile:

```bash
# Compile for embedded testing with host GCC:
unfish build --embedded program.unfish

# Cross-compile for ARM Cortex-M microcontrollers:
unfish build --embedded --arm -o firmware.elf program.unfish
```

### 6.1. The Freestanding Embedded Profile (`-DUF_EMBEDDED`)
When compiling with `-DUF_EMBEDDED`:
1. **No POSIX Dependencies**: Removes calls to `fork`, `exec`, `getenv`, and filesystem directories.
2. **Fixed Heap Sizing**: The garbage collector operates within a statically allocated memory pool (e.g. 64 KB or 256 KB) defined at compile time, eliminating external `malloc` fragmentation.
3. **Deterministic Gas Limiting**: Every loop iteration and function call decrements an instruction counter, preventing infinite loops from hanging embedded microcontrollers.

### 6.2. ARM Cortex-M Cross-Compilation
When `--arm` is specified, Unfish invokes the GNU Arm Embedded Toolchain:
```bash
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -O2 -DUF_EMBEDDED \
                  -specs=nosys.specs -o firmware.elf out.c
```
This enables students and embedded developers to write algorithms in clean Unfish and deploy them directly onto STM32, Nordic nRF, or Raspberry Pi Pico microcontrollers.
