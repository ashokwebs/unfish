# Volume VIII: The Unfish Systems & Low-Level Programming Manual

> **Document Status**: Production Complete • **Specification Level**: Bare-Metal, ABI & Systems  
> **Target Audience**: Systems Programmers, Embedded Engineers, Protocol Authors, Driver Developers  
> **Related Manuals**: [NATIVE_COMPILER.md](NATIVE_COMPILER.md) • [MEMORY_MODEL.md](MEMORY_MODEL.md) • [VM.md](VM.md) • [STANDARD_LIBRARY.md](STANDARD_LIBRARY.md)

---

## Table of Contents

1. [The Systems Programming Vision in Unfish](#1-the-systems-programming-vision-in-unfish)
2. [The `Buffer` Object Architecture](#2-the-buffer-object-architecture)
3. [Byte Manipulation & Endianness Primitives](#3-byte-manipulation--endianness-primitives)
4. [Bitwise Operators & Bitmask Manipulation](#4-bitwise-operators--bitmask-manipulation)
5. [C99 Transpilation & The Native C ABI](#5-c99-transpilation--the-native-c-abi)
6. [Bare-Metal Microcontrollers (ARM Cortex-M)](#6-bare-metal-microcontrollers-arm-cortex-m)
7. [WebAssembly (WASM) Linear Memory & Host Interop](#7-webassembly-wasm-linear-memory--host-interop)
8. [Safety, Bounds Checking & Zero-Leak Guarantees](#8-safety-bounds-checking--zero-leak-guarantees)
9. [Comprehensive Systems Programming Examples](#9-comprehensive-systems-programming-examples)

---

## 1. The Systems Programming Vision in Unfish

Traditional high-level languages abstract away memory, rendering them unsuitable for low-level systems tasks like network packet crafting, binary file deserialization, cryptographic hashing, and microcontroller firmware.

Unfish provides a unique balance:
- **Clean high-level ergonomics**: Indentation syntax, first-class functions, pattern matching, and traits.
- **Direct byte-level control**: Raw contiguous memory buffers (`buffer`), arbitrary integer widths (`u8`, `i8`, `u16`, `i16`, `u32`, `i32`), explicit endianness codecs, and bitwise arithmetic.
- **Zero-overhead deployment**: Transpile to a single ANSI C99 source file (`unfish emit-c`) or compile directly to bare-metal ARM firmware (`unfish build --arm`).

```
+-----------------------------------------------------------------------+
|                         High-Level Scripting                          |
|         let packet = buffer(64) |> configure_header |> serialize      |
+-----------------------------------+-----------------------------------+
                                    |
                    Direct Byte-Level Representations
                                    v
+-----------------------------------------------------------------------+
|  Offset  | 0x00..0x03 | 0x04..0x05 | 0x06..0x09 | 0x0A..0x3F          |
|  Field   |   MAGIC    |   OPCODE   |   LENGTH   | PAYLOAD             |
|  Type    |   u32_le   |   u16_le   |   u32_le   | Raw Bytes           |
+-----------------------------------+-----------------------------------+
                                    |
             Compilation Target Selection via Unfish Toolchain
                                    v
+-----------------------+-----------------------+-----------------------+
| Native C99 Source     | Bare-Metal ARM        | WebAssembly Linear    |
| (gcc/clang standalone)| (Cortex-M0/M4 MMIO)   | (Browser / Node.js)   |
+-----------------------+-----------------------+-----------------------+
```

---

## 2. The `Buffer` Object Architecture

The fundamental unit of low-level data manipulation in Unfish is the `Buffer`.

### C Representation (`UfBufferObject`)

```c
typedef struct UfBufferObject {
    UfObj       obj;        /* Standard GC header */
    size_t      size;       /* Byte length */
    uint8_t*    bytes;      /* Contiguous byte array */
} UfBufferObject;
```

### Buffer Lifecycle & Allocation

1. **Allocation**: `buffer(size)` allocates a contiguous block of `size` bytes, initialized to zero.
2. **String Conversion**:
   - `buffer_from_string(str)` copies UTF-8 bytes into a new buffer.
   - `buffer_to_string(buf)` reads bytes as a null-terminated UTF-8 string.
3. **Hex Codecs**:
   - `buffer_to_hex(buf)` encodes bytes as a lowercase hexadecimal string.
   - `buffer_from_hex(hex_str)` decodes a hexadecimal string into raw bytes.
4. **Slicing**:
   - `buffer_slice(buf, start, end)` creates a new buffer containing bytes from `start` up to (exclusive) `end`. Bounds are strictly verified.
5. **Memory Inspection**:
   - `inspect(buf)` prints a formatted hex-dump with ASCII sidebar, ideal for debugging wire protocols.

---

## 3. Byte Manipulation & Endianness Primitives

Unfish supports explicit endianness operations to eliminate cross-platform architecture discrepancies.

### Complete Buffer Built-in API

| Function | Signature | Return | Description |
|---|---|---|---|
| `buffer` | `buffer(size: Number)` | `Buffer` | Allocates zeroed buffer of `size` bytes. |
| `buffer_size` | `buffer_size(buf: Buffer)` | `Number` | Returns buffer byte length. |
| `buffer_get` | `buffer_get(buf: Buffer, offset: Number)` | `Number` | Reads single unsigned byte (`0..255`). |
| `buffer_set` | `buffer_set(buf: Buffer, offset: Number, val: Number)` | `Null` | Writes single byte (`val & 0xFF`). |
| `buffer_fill` | `buffer_fill(buf: Buffer, byte_val: Number)` | `Null` | Fills entire buffer with byte value. |
| `buffer_slice` | `buffer_slice(buf: Buffer, start: Number, end?: Number)` | `Buffer` | Creates sub-slice buffer. |
| `buffer_read_u16_le` | `buffer_read_u16_le(buf: Buffer, offset: Number)` | `Number` | Reads 16-bit unsigned integer (Little-Endian). |
| `buffer_write_u16_le` | `buffer_write_u16_le(buf: Buffer, offset: Number, val: Number)` | `Null` | Writes 16-bit unsigned integer (Little-Endian). |
| `buffer_read_u32_le` | `buffer_read_u32_le(buf: Buffer, offset: Number)` | `Number` | Reads 32-bit unsigned integer (Little-Endian). |
| `buffer_write_u32_le` | `buffer_write_u32_le(buf: Buffer, offset: Number, val: Number)` | `Null` | Writes 32-bit unsigned integer (Little-Endian). |
| `buffer_read_i32_le` | `buffer_read_i32_le(buf: Buffer, offset: Number)` | `Number` | Reads 32-bit signed integer (Little-Endian). |
| `buffer_write_i32_le` | `buffer_write_i32_le(buf: Buffer, offset: Number, val: Number)` | `Null` | Writes 32-bit signed integer (Little-Endian). |

### Integer Truncation & Coercion Helpers

- `u8(v)`: Masks `v` to `0..255` (`v & 0xFF`).
- `i8(v)`: Sign-extends 8-bit integer (`-128..127`).
- `u16(v)`: Masks `v` to `0..65535` (`v & 0xFFFF`).
- `i16(v)`: Sign-extends 16-bit integer (`-32768..32767`).
- `u32(v)`: Masks `v` to `0..4294967295` (`v & 0xFFFFFFFF`).
- `i32(v)`: Sign-extends 32-bit integer (`-2147483648..2147483647`).

---

## 4. Bitwise Operators & Bitmask Manipulation

Unfish provides 64-bit integer bitwise operations via standard built-ins:

```unfish
let a = 0b1100 # 12
let b = 0b1010 # 10

say band(a, b) # Bitwise AND: 0b1000 = 8
say bor(a, b)  # Bitwise OR:  0b1110 = 14
say bxor(a, b) # Bitwise XOR: 0b0110 = 6
say bnot(a)    # Bitwise NOT: -13 (two's complement)
say shl(1, 4)  # Logical Shift Left: 16
say shr(16, 2) # Logical Shift Right: 4
say sar(-16, 2)# Arithmetic Shift Right (preserves sign): -4
```

---

## 5. C99 Transpilation & The Native C ABI

Unfish programs can be compiled to standalone, dependency-free ANSI C99:

```bash
unfish emit-c -o output.c program.unfish
gcc -O3 -std=c99 output.c -lm -o program
```

### Generated C Code Structure

The generated C file bundles:
1. `unfish_runtime.h`: Complete minimal runtime engine including `UfValue` representation, GC, and string tables.
2. Function declarations: Every Unfish function becomes a static C function `static UfValue uf_fn_name(UfRuntime* rt, int argc, UfValue* args)`.
3. Constants table: Deduped strings, numbers, and identifiers.
4. Entry point: Standard `int main(int argc, char** argv)` initializing the runtime, setting up CLI args, and invoking top-level statements.

### Calling C from Unfish & Embedding Unfish in C

Embedding Unfish in a host C application:

```c
#include "unfish.h"

int main() {
    UfRuntime* rt = uf_runtime_new();
    
    // Register custom C native host function
    uf_env_declare(rt->global_env, "host_hardware_read", 
                   uf_val_native("host_hardware_read", my_c_reader, 1));
    
    // Execute script
    UfValue result = uf_runtime_eval_string(rt, "let x = host_hardware_read(0x400); say x");
    
    uf_runtime_free(rt);
    return 0;
}
```

---

## 6. Bare-Metal Microcontrollers (ARM Cortex-M)

Unfish supports embedded environments with strict memory constraints:
- Zero dynamic allocation mode (`--no-gc`).
- Static heap sizing via compile-time pre-allocated arena.
- Memory-Mapped I/O (MMIO) access for hardware peripheral registers.

### Compiling for ARM Cortex-M

```bash
unfish build --arm -o firmware.elf firmware.unfish
```

Invokes the cross-compilation pipeline:
`unfish emit-c --embedded` $\rightarrow$ `arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -nostartfiles`.

### MMIO Peripheral Driver Pattern

```unfish
# Simulated Memory-Mapped Register Driver
let GPIOA_BASE = 0x40020000
let MODER_OFFSET = 0x00
let ODR_OFFSET   = 0x14

fn configure_gpio_pin(pin_index, is_output):
    let moder_reg = GPIOA_BASE + MODER_OFFSET
    # Configure 2 bits per pin
    let shift = pin_index * 2
    let mask = bnot(shl(3, shift))
    let mode_val = 0
    if is_output:
        mode_val = 1
    # Update register
    say f"MMIO Write: [0x{to_hex(moder_reg)}] <= Mode {mode_val}"

configure_gpio_pin(5, true) # Set PA5 (LED) as output
```

---

## 7. WebAssembly (WASM) Linear Memory & Host Interop

In WebAssembly environments, Unfish interacts with the browser JavaScript runtime via shared linear memory:

```
+-------------------------------------------------------------------+
|                     WebAssembly Linear Memory                     |
|                                                                   |
| [ 0x0000 .. 0x0FFF ] Runtime Control Structures & Stack           |
| [ 0x1000 .. 0x7FFF ] Static Heap & Constant String Table          |
| [ 0x8000 .. 0xFFFF ] Dynamic Buffer Region (Exported to JS)       |
+---------------------------------+---------------------------------+
                                  | Zero-Copy Uint8Array Wrap
                                  v
+-------------------------------------------------------------------+
|               Browser / Node.js JavaScript Runtime                |
|  const view = new Uint8Array(wasmMemory.buffer, 0x8000, 1024);     |
|  canvasContext.putImageData(view, 0, 0);                          |
+-------------------------------------------------------------------+
```

### Exporting Buffers to JavaScript

```unfish
fn generate_image_scanline(width):
    let buf = buffer(width * 4) # RGBA
    for i in range(width):
        let off = i * 4
        buffer_set(buf, off + 0, 255) # Red
        buffer_set(buf, off + 1, 128) # Green
        buffer_set(buf, off + 2, 0)   # Blue
        buffer_set(buf, off + 3, 255) # Alpha
    return buf
```

In JavaScript:

```javascript
const bufPtr = wasmInstance.exports.generate_image_scanline(800);
const pixelData = new Uint8ClampedArray(wasmInstance.exports.memory.buffer, bufPtr, 800 * 4);
```

---

## 8. Safety, Bounds Checking & Zero-Leak Guarantees

Unlike raw C, Unfish buffers are **memory-safe by default**:
1. **Strict Bounds Checking**: Any attempt to read or write past `buffer_size(b)` raises a catchable runtime error instead of inducing undefined behavior or memory corruption.
2. **Buffer Lifetime Tracking**: Buffers are tracked by the garbage collector; when no references remain, their underlying C memory is freed deterministically.
3. **No Buffer Overruns**: Checked and validated against AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan).

---

## 9. Comprehensive Systems Programming Examples

### Example 1: Bitmap (.BMP) Image Header Generator

Crafting a valid 54-byte BMP header for a 100x100 24-bit RGB bitmap:

```unfish
fn create_bmp_header(width, height):
    let file_header_size = 14
    let info_header_size = 40
    let row_size = floor((width * 3 + 3) / 4) * 4 # 4-byte row alignment
    let image_data_size = row_size * height
    let total_file_size = file_header_size + info_header_size + image_data_size

    let buf = buffer(54)

    # File Header (14 bytes)
    buffer_set(buf, 0, 0x42) # 'B'
    buffer_set(buf, 1, 0x4D) # 'M'
    buffer_write_u32_le(buf, 2, total_file_size)
    buffer_write_u16_le(buf, 6, 0) # Reserved 1
    buffer_write_u16_le(buf, 8, 0) # Reserved 2
    buffer_write_u32_le(buf, 10, 54) # Pixel offset

    # DIB Header (BITMAPINFOHEADER - 40 bytes)
    buffer_write_u32_le(buf, 14, 40) # Header size
    buffer_write_u32_le(buf, 18, width)
    buffer_write_u32_le(buf, 22, height)
    buffer_write_u16_le(buf, 26, 1)  # Color planes
    buffer_write_u16_le(buf, 28, 24) # Bits per pixel (24-bit RGB)
    buffer_write_u32_le(buf, 30, 0)  # BI_RGB (uncompressed)
    buffer_write_u32_le(buf, 34, image_data_size)
    buffer_write_u32_le(buf, 38, 2835) # Horizontal resolution (72 DPI)
    buffer_write_u32_le(buf, 42, 2835) # Vertical resolution (72 DPI)
    buffer_write_u32_le(buf, 46, 0)    # Colors in palette
    buffer_write_u32_le(buf, 50, 0)    # Important colors

    return buf

let bmp = create_bmp_header(640, 480)
say f"BMP Header (54 bytes): {buffer_to_hex(bmp)}"
say f"File size specified in header: {buffer_read_u32_le(bmp, 2)} bytes"
```

### Example 2: CRC-32 Checksum Calculator

Computing an IEEE 802.3 32-bit Cyclic Redundancy Check:

```unfish
fn make_crc32_table():
    let table = []
    for i in range(256):
        let crc = i
        repeat 8 times:
            if band(crc, 1) != 0:
                crc = bxor(shr(crc, 1), 0xEDB88320)
            else:
                crc = shr(crc, 1)
        push(table, crc)
    return table

let CRC_TABLE = make_crc32_table()

fn calculate_crc32(buf):
    let crc = 0xFFFFFFFF
    let size = buffer_size(buf)
    for i in range(size):
        let byte_val = buffer_get(buf, i)
        let table_idx = band(bxor(crc, byte_val), 0xFF)
        crc = bxor(shr(crc, 8), CRC_TABLE[table_idx])
    return bxor(crc, 0xFFFFFFFF)

let test_buf = buffer_from_string("123456789")
let crc = calculate_crc32(test_buf)
say f"CRC-32 of '123456789': 0x{to_hex(crc)} (Expected: 0xcbf43926)"
```
