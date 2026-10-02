# UNFISH — EXHAUSTIVE STANDARD LIBRARY REFERENCE MANUAL
## Volume II: Core Built-ins, Mathematical Functions, Systems Buffers & Modules
### Version 2.1.0

---

## 1. Architectural Principles of the Standard Library

The Unfish Standard Library is engineered around five strict architectural principles:
1. **Zero External Dependencies**: Every function is implemented in pure ANSI C99 linking only against the standard C runtime library (`libc`) and math library (`libm`).
2. **100% 5-Way Differential Parity**: Every function produces bit-for-bit identical outputs across the AST Interpreter, the Stack VM, the Register VM, Native C99 binaries, and WebAssembly.
3. **Structured Sandboxed Isolation**: All host interactions (filesystem I/O, process spawning, environment variables) are encapsulated within standard modules (`sys`, `fs`, `time`), preventing unintentional side-effects in core computation.
4. **Resilient Error Contracts**: Invalid arguments, domain errors, and out-of-bounds access raise structured catchable exceptions (`ERR_TYPE_MISMATCH`, `ERR_INDEX_OUT_OF_BOUNDS`, `ERR_MATH_DOMAIN`) rather than crashing the runtime.
5. **Memory Predictability**: All dynamically allocated strings and collections are tracked by the runtime memory manager, supporting deterministic reclamation without memory leaks.

---

## 2. Core Global Built-in Functions

Core built-ins are registered in the global environment at runtime initialization and are available in all Unfish scopes without an `import` statement.

### 2.1. Output, Debugging & Introspection

#### `say(value)`
* **Type Signature**: `say(value: Any): Null`
* **Description**: Converts `value` to its standard string representation and outputs it to standard output (`stdout`) followed by a newline character.
* **Return Value**: `null`.
* **Example**:
  ```unfish
  say "Hello, Unfish!"
  say 42 + 8
  say [1, 2, 3]
  ```

#### `print(value)`
* **Type Signature**: `print(value: Any): Null`
* **Description**: Outputs `value` to standard output without appending a trailing newline.
* **Return Value**: `null`.
* **Example**:
  ```unfish
  print("Processing: ")
  print("Done.\n")
  ```

#### `type_of(value)`
* **Type Signature**: `type_of(value: Any): String`
* **Description**: Performs runtime type reflection, returning the canonical string type descriptor of `value`.
* **Return Values**:
  * `"null"`, `"boolean"`, `"number"`, `"string"`, `"array"`, `"map"`, `"function"`, `"closure"`, `"struct"`, `"instance"`, `"enum"`, `"fiber"`, `"channel"`, `"buffer"`, `"promise"`, or `"error"`.
* **Example**:
  ```unfish
  say type_of(42)           # "number"
  say type_of("Lagoon")     # "string"
  say type_of([1, 2])       # "array"
  say type_of({"a": 1})     # "map"
  ```

#### `len(container)`
* **Type Signature**: `len(container: Array | Map | String | Buffer): Number`
* **Description**: Returns the length or element count of `container`:
  * **Array**: Total number of contained elements.
  * **Map**: Total number of key-value associations.
  * **String**: Total count of UTF-8 encoded bytes.
  * **Buffer**: Total allocated capacity in bytes.
* **Errors**: Raises `ERR_TYPE_MISMATCH` if `container` is not an indexable type.
* **Example**:
  ```unfish
  say len("Unfish")         # 6
  say len([10, 20, 30])     # 3
  say len({"x": 1, "y": 2}) # 2
  ```

#### `assert(condition, message = "Assertion failed")`
* **Type Signature**: `assert(condition: Any, message: String = "Assertion failed"): Null`
* **Description**: Asserts that `condition` is truthy (not `false` and not `null`). If falsy, immediately raises a runtime error with `message`.
* **Example**:
  ```unfish
  assert(2 + 2 == 4, "Arithmetic inconsistency detected")
  ```

#### `error(message)`
* **Type Signature**: `error(message: String): Null`
* **Description**: Constructs and raises an unrecoverable user exception with the specified `message`.
* **Example**:
  ```unfish
  if balance < 0:
      error("Negative balance invariant violated")
  ```

---

### 2.2. Array Manipulation & Higher-Order Combinators

#### `push(array, element)`
* **Type Signature**: `push(array: Array<T>, element: T): Array<T>`
* **Description**: Appends `element` to the end of `array`, dynamically resizing internal storage if needed.
* **Returns**: The mutated `array`.
* **Example**:
  ```unfish
  let items = [1, 2]
  push(items, 3)
  say items # [1, 2, 3]
  ```

#### `pop(array)`
* **Type Signature**: `pop(array: Array<T>): T | Null`
* **Description**: Removes and returns the final element of `array`. Returns `null` if the array is empty.
* **Example**:
  ```unfish
  let stack = ["first", "second"]
  let top = pop(stack) # "second"
  say stack            # ["first"]
  ```

#### `range(start, end, step = 1)`
* **Type Signature**:
  * `range(end: Number): Array<Number>`
  * `range(start: Number, end: Number, step: Number = 1): Array<Number>`
* **Description**: Generates an array containing the sequence of numbers from `start` up to (but excluding) `end`, advancing by `step`.
* **Example**:
  ```unfish
  say range(4)        # [0, 1, 2, 3]
  say range(1, 5)     # [1, 2, 3, 4]
  say range(0, 10, 2) # [0, 2, 4, 6, 8]
  ```

#### `map(array, transform_fn)`
* **Type Signature**: `map(array: Array<T>, transform_fn: Function<(T) -> R>): Array<R>`
* **Description**: Constructs a new array by applying `transform_fn` to every element in `array`.
* **Example**:
  ```unfish
  let nums = [1, 2, 3]
  let doubled = map(nums, fn(x): x * 2)
  say doubled # [2, 4, 6]
  ```

#### `filter(array, predicate_fn)`
* **Type Signature**: `filter(array: Array<T>, predicate_fn: Function<(T) -> Boolean>): Array<T>`
* **Description**: Constructs a new array containing all elements of `array` for which `predicate_fn(item)` evaluates to truthy.
* **Example**:
  ```unfish
  let nums = [1, 2, 3, 4, 5, 6]
  let evens = filter(nums, fn(x): x % 2 == 0)
  say evens # [2, 4, 6]
  ```

#### `reduce(array, accumulator_fn, initial_value)`
* **Type Signature**: `reduce(array: Array<T>, accumulator_fn: Function<(Acc, T) -> Acc>, initial_value: Acc): Acc`
* **Description**: Folds elements of `array` into a single accumulated result from left to right.
* **Example**:
  ```unfish
  let nums = [1, 2, 3, 4]
  let sum = reduce(nums, fn(acc, x): acc + x, 0)
  say sum # 10
  ```

#### `sort(array, comparator = null)`
* **Type Signature**: `sort(array: Array<T>, comparator: Function<(T, T) -> Number> = null): Array<T>`
* **Description**: Sorts `array` in place using an adaptive introsort/quicksort. If `comparator` is omitted, numbers and strings are sorted in natural ascending order.
* **Returns**: The sorted `array`.
* **Example**:
  ```unfish
  let words = ["zebra", "apple", "mango"]
  sort(words)
  say words # ["apple", "mango", "zebra"]
  ```

#### `reverse(array)`
* **Type Signature**: `reverse(array: Array<T>): Array<T>`
* **Description**: Reverses the order of elements in `array` in place.
* **Returns**: The reversed `array`.

#### `find(array, predicate)` / `every(array, predicate)` / `some(array, predicate)`
* `find(arr, fn)`: Returns the first element satisfying `fn(x)`, or `null` if none found.
* `every(arr, fn)`: Returns `true` if every element satisfies `fn(x)`, otherwise `false`.
* `some(arr, fn)`: Returns `true` if at least one element satisfies `fn(x)`.

---

### 2.3. Hash Map Operations

#### `keys(map)` / `values(map)`
* **Type Signatures**: `keys(map: Map<K, V>): Array<K>`, `values(map: Map<K, V>): Array<V>`
* **Description**: Returns a dynamic array containing all keys or all values present in `map`.
* **Example**:
  ```unfish
  let m = {"id": 1, "name": "Lagoon"}
  say keys(m)   # ["id", "name"]
  say values(m) # [1, "Lagoon"]
  ```

#### `has_key(map, key)`
* **Type Signature**: `has_key(map: Map<K, V>, key: K): Boolean`
* **Description**: Tests whether `key` is present in `map` in $O(1)$ average time.

#### `delete(map, key)`
* **Type Signature**: `delete(map: Map<K, V>, key: K): Boolean`
* **Description**: Removes `key` and its associated value from `map`. Returns `true` if the key was present, `false` otherwise.

---

### 2.4. String Manipulation Built-ins

| Function | Signature | Operational Description |
|---|---|---|
| `split` | `(str: String, delim: String) -> Array<String>` | Splits string into array of substrings separated by `delim` |
| `join` | `(arr: Array<Any>, delim: String) -> String` | Concatenates elements of `arr` separated by `delim` |
| `trim` | `(str: String) -> String` | Strips leading and trailing ASCII whitespace |
| `trim_start` | `(str: String) -> String` | Strips leading whitespace |
| `trim_end` | `(str: String) -> String` | Strips trailing whitespace |
| `replace` | `(str: String, old: String, new: String) -> String`| Replaces all occurrences of `old` with `new` |
| `to_upper` | `(str: String) -> String` | Converts ASCII characters to uppercase |
| `to_lower` | `(str: String) -> String` | Converts ASCII characters to lowercase |
| `contains` | `(str: String, needle: String) -> Boolean` | Returns `true` if `needle` is a substring of `str` |
| `starts_with`| `(str: String, prefix: String) -> Boolean` | Returns `true` if `str` begins with `prefix` |
| `ends_with` | `(str: String, suffix: String) -> Boolean` | Returns `true` if `str` terminates with `suffix` |
| `char_at` | `(str: String, index: Number) -> String` | Returns 1-character string at 0-indexed position |
| `chars` | `(str: String) -> Array<String>` | Splits string into array of individual 1-character strings |
| `count` | `(str: String, sub: String) -> Number` | Counts non-overlapping occurrences of `sub` |
| `to_number` | `(str: String) -> Number` | Parses numeric string (supports decimals and scientific notation) |
| `to_string` | `(val: Any) -> String` | Converts any value to its string representation |
| `repeat_string`| `(str: String, count: Number) -> String` | Returns string concatenated with itself `count` times |
| `substring` | `(str: String, start: Number, len: Number) -> String`| Extracts slice of length `len` starting at `start` |
| `index_of` | `(str: String, sub: String) -> Number` | Returns index of first occurrence of `sub`, or `-1` |
| `pad_start` | `(str: String, len: Number, pad: String = " ") -> String` | Pads string on the left up to `len` characters |
| `pad_end` | `(str: String, len: Number, pad: String = " ") -> String` | Pads string on the right up to `len` characters |

---

### 2.5. Mathematical Built-ins & Constants

#### Mathematical Constants
* `PI`: $\pi \approx 3.14159265358979323846$
* `E`: $e \approx 2.71828182845904523536$
* `INFINITY`: $+\infty$ (IEEE 754 positive infinity)

#### Mathematical Functions
* `abs(x)`: Computes the absolute value $|x|$.
* `floor(x)`: Rounds down to the nearest integer $\lfloor x \rfloor$.
* `ceil(x)`: Rounds up to the nearest integer $\lceil x \rceil$.
* `round(x)`: Rounds to the nearest integer, breaking ties away from zero.
* `sqrt(x)`: Computes the principal square root $\sqrt{x}$. Raises `ERR_MATH_DOMAIN` if $x < 0$.
* `pow(base, exp)`: Computes $base^{exp}$.
* `min(a, b)` / `max(a, b)`: Computes minimum or maximum of two numbers.
* `log(x)`: Computes natural logarithm $\ln(x)$.
* `sin(x)`, `cos(x)`, `tan(x)`: Trigonometric functions with angle in radians.
* `random()`: Generates uniform pseudorandom double in range $[0.0, 1.0)$.
* `random_int(min, max)`: Generates uniform pseudorandom integer in range $[min, max]$.

---

### 2.6. Bitwise Manipulation Built-ins

Unfish provides bitwise operators as pure functions operating on 32-bit signed two's-complement integers:
* `band(a, b)`: Bitwise AND ($a \ \& \ b$).
* `bor(a, b)`: Bitwise OR ($a \ | \ b$).
* `bxor(a, b)`: Bitwise XOR ($a \oplus b$).
* `bnot(a)`: Bitwise NOT ($\sim a$).
* `shl(a, bits)`: Logical left shift ($a \ll bits$).
* `shr(a, bits)`: Logical zero-fill right shift ($a \ggg bits$).
* `sar(a, bits)`: Arithmetic sign-propagating right shift ($a \gg bits$).

---

### 2.7. Systems Memory Buffers (`buffer_*`)

The Unfish systems buffer API provides direct, byte-level manipulation of contiguous memory allocations for systems programming, protocol parsers, and bare-metal serialization:

| Function | Signature | Description |
|---|---|---|
| `buffer_create` | `(capacity: Number) -> Buffer` | Allocates a zero-initialized memory buffer of `capacity` bytes |
| `buffer_len` | `(buf: Buffer) -> Number` | Returns allocated capacity in bytes |
| `buffer_read_u8`| `(buf: Buffer, offset: Number) -> Number` | Reads unsigned 8-bit integer at `offset` |
| `buffer_write_u8`| `(buf: Buffer, offset: Number, val: Number) -> Null`| Writes unsigned 8-bit integer at `offset` |
| `buffer_read_u16`| `(buf: Buffer, offset: Number, le: Boolean = true) -> Number` | Reads unsigned 16-bit integer (little/big endian) |
| `buffer_write_u16`| `(buf: Buffer, offset: Number, val: Number, le: Boolean = true) -> Null` | Writes unsigned 16-bit integer |
| `buffer_read_u32`| `(buf: Buffer, offset: Number, le: Boolean = true) -> Number` | Reads unsigned 32-bit integer |
| `buffer_write_u32`| `(buf: Buffer, offset: Number, val: Number, le: Boolean = true) -> Null` | Writes unsigned 32-bit integer |
| `buffer_read_f64`| `(buf: Buffer, offset: Number, le: Boolean = true) -> Number` | Reads 64-bit IEEE 754 float |
| `buffer_write_f64`| `(buf: Buffer, offset: Number, val: Number, le: Boolean = true) -> Null` | Writes 64-bit IEEE 754 float |

---

## 3. Standard Library Modules

Standard library modules are imported via `import <mod>` or `from <mod> import <symbols>`.

### 3.1. `sys` — System Environment & Process Control
```unfish
import sys

say f"Platform: {sys.platform}"
say f"Unfish Version: {sys.version}"
say f"CWD: {sys.cwd()}"
say f"Arguments: {sys.argv}"
```
* `sys.argv: Array<String>`: Command-line arguments.
* `sys.platform: String`: Host OS (`"linux"`, `"darwin"`, `"windows"`, `"wasm"`).
* `sys.version: String`: Runtime version (`"2.1.0"`).
* `sys.cwd(): String`: Returns absolute path to current working directory.
* `sys.exit(code: Number = 0)`: Terminates host process immediately with exit code.
* `sys.env(key: String): String | Null`: Reads host environment variable.
* `sys.set_env(key: String, value: String): Boolean`: Sets host environment variable.
* `sys.exec(command: String): Number`: Executes shell command synchronously and returns exit code.

---

### 3.2. `fs` — File System Operations
```unfish
import fs

if not fs.exists("data.txt"):
    fs.write_file("data.txt", "Initial record\n")

fs.append_file("data.txt", "Second record\n")
let content = fs.read_file("data.txt")
say content
```
* `fs.read_file(path: String): String`: Reads entire file as a UTF-8 string.
* `fs.write_file(path: String, content: String): Boolean`: Overwrites file with content.
* `fs.append_file(path: String, content: String): Boolean`: Appends content to file.
* `fs.exists(path: String): Boolean`: Returns `true` if path exists.
* `fs.remove(path: String): Boolean`: Deletes a file.
* `fs.list_dir(path: String): Array<String>`: Lists directory entries.
* `fs.mkdir(path: String): Boolean`: Creates a new directory.
* `fs.remove_dir(path: String): Boolean`: Removes an empty directory.
* `fs.is_file(path: String): Boolean`: Tests if path is a regular file.
* `fs.is_dir(path: String): Boolean`: Tests if path is a directory.
* `fs.file_size(path: String): Number`: Returns file size in bytes.

---

### 3.3. `time` — High-Resolution Clock & Timestamps
```unfish
import time

let start = time.now()
time.sleep(100) # Sleep 100ms
let elapsed = time.diff_ms(start, time.now())
say f"Elapsed time: {elapsed}ms"
say f"Current ISO Timestamp: {time.iso()}"
```
* `time.now(): Number`: Returns Unix epoch timestamp in milliseconds.
* `time.sleep(ms: Number): Null`: Suspends thread execution for `ms` milliseconds.
* `time.format(ts: Number, format: String): String`: Formats timestamp via standard C `strftime`.
* `time.iso(): String`: Returns current UTC timestamp in ISO 8601 format.
* `time.parse(iso_string: String): Number`: Parses ISO 8601 string into millisecond epoch timestamp.
* `time.diff_ms(t1: Number, t2: Number): Number`: Computes millisecond difference between two timestamps.

---

### 3.4. `random` — Pseudorandom Number Generation
```unfish
import random

random.seed(1337)
say random.random() # [0.0, 1.0)
say random.random_int(1, 100)

let deck = ["A", "K", "Q", "J", "10"]
random.shuffle(deck)
say f"Shuffled: {deck}"
say f"Card: {random.choice(deck)}"
```
* `random.seed(seed: Number)`: Seeds the PRNG state.
* `random.random(): Number`: Float in $[0.0, 1.0)$.
* `random.random_int(min: Number, max: Number): Number`: Integer in $[min, max]$.
* `random.choice(array: Array<T>): T`: Picks random element from array.
* `random.shuffle(array: Array<T>): Array<T>`: Performs in-place Fisher-Yates shuffle.

---

### 3.5. `json` — Cycle-Safe JSON Parser & Serializer
```unfish
import json

let raw = '{"school": "Coral Reef", "count": 3, "fishes": [{"id": 1}, {"id": 2}]}'
let data = json.parse(raw)
say data.school # "Coral Reef"

let serialized = json.stringify(data, 2)
say serialized
```
* `json.parse(json_string: String): Any`: Parses valid JSON into Unfish values.
* `json.stringify(value: Any, indent: Number = 0): String`: Serializes Unfish value to JSON string.
  * **Cycle Detection**: Detects cyclic object graphs, raising `ERR_JSON_CYCLE` rather than overflowing the stack.

---

### 3.6. `testing` — Declarative Unit Testing Framework
```unfish
from testing import describe, test, assert_eq, assert_true, run_tests

describe("Vector Math Suite", fn():
    test("vector addition", fn():
        assert_eq(2 + 2, 4)
    )
    test("truthiness check", fn():
        assert_true(len([1, 2]) == 2)
    )
)

run_tests()
```
* `describe(name: String, suite_fn: Function)`: Declares a test suite block.
* `test(name: String, case_fn: Function)`: Declares an individual test case.
* `assert_eq(actual, expected)`: Asserts structural equality.
* `assert_ne(actual, unexpected)`: Asserts structural inequality.
* `assert_true(condition)` / `assert_false(condition)`: Asserts boolean condition.
* `assert_null(val)` / `assert_not_null(val)`: Asserts nullness.
* `run_tests()`: Executes all registered test suites, printing pass/fail stats and execution duration.
