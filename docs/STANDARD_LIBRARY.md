# UNFISH — EXHAUSTIVE STANDARD LIBRARY REFERENCE MANUAL

---

## 1. Principles & Standard Library Architecture

The Unfish standard library is designed according to five foundational principles:
1. **Explicit Boundaries**: All host interactions (filesystem, process spawning, environment variables) are strictly isolated into dedicated modules (`sys`, `fs`, `time`), preventing accidental side effects.
2. **Pedagogical Ergonomics**: Function names are intuitive, readable, and consistent (`say`, `len`, `type_of`, `split`, `join`).
3. **5-Way Parity**: Every function and module produces identical output whether executed in the AST interpreter, the Stack VM, the Register VM, a native C99 binary, or WebAssembly.
4. **Pure ANSI C99 Implementation**: Built-ins are implemented with zero external library dependencies, linking only against standard C libc and libm.
5. **Robust Error Contracts**: Functions validate argument counts and types, raising structured catchable exceptions on error rather than crashing.

---

## 2. Global Core Built-in Functions

Global built-ins are always available without requiring an `import` statement.

### 2.1. `say(value)`
* **Signature**: `say(value: Any): Null`
* **Description**: Converts `value` to its string representation and outputs it to standard output followed by a newline.
* **Returns**: `null`.
* **Example**:
  ```unfish
  say "Hello, Unfish!"
  say 42 + 8
  # Output:
  # Hello, Unfish!
  # 50
  ```

### 2.2. `print(value)`
* **Signature**: `print(value: Any): Null`
* **Description**: Outputs `value` to standard output without appending a trailing newline.
* **Returns**: `null`.
* **Example**:
  ```unfish
  print("Loading: ")
  print("100%\n")
  # Output: Loading: 100%
  ```

### 2.3. `type_of(value)`
* **Signature**: `type_of(value: Any): String`
* **Description**: Returns the runtime type name of `value`.
* **Returns**: `"null"`, `"boolean"`, `"number"`, `"string"`, `"array"`, `"map"`, `"function"`, `"closure"`, `"struct"`, `"instance"`, `"enum"`, `"fiber"`, `"channel"`, `"buffer"`, `"promise"`, or `"error"`.
* **Example**:
  ```unfish
  say type_of(42)          # "number"
  say type_of([1, 2, 3])   # "array"
  say type_of({"a": 1})    # "map"
  ```

### 2.4. `len(container)`
* **Signature**: `len(container: Any): Number`
* **Description**: Returns the number of elements in an array, key-value pairs in a map, bytes in a string, or capacity of a buffer.
* **Errors**: Raises `ERR_TYPE_MISMATCH` if `container` is not an array, map, string, or buffer.
* **Example**:
  ```unfish
  say len("Unfish")        # 6
  say len([10, 20, 30])    # 3
  say len({"x": 1, "y": 2})# 2
  ```

### 2.5. `push(array, element)`
* **Signature**: `push(array: Array<Any>, element: Any): Array<Any>`
* **Description**: Appends `element` to the end of `array`, dynamically expanding its capacity if necessary.
* **Returns**: The mutated `array`.
* **Example**:
  ```unfish
  let items = [1, 2]
  push(items, 3)
  say items # [1, 2, 3]
  ```

### 2.6. `pop(array)`
* **Signature**: `pop(array: Array<Any>): Any`
* **Description**: Removes and returns the final element of `array`. Returns `null` if the array is empty.
* **Example**:
  ```unfish
  let stack = ["first", "second"]
  let top = pop(stack)
  say top   # "second"
  say stack # ["first"]
  ```

### 2.7. `range(start, end, step = 1)`
* **Signature**: `range(start: Number, end: Number, step: Number = 1): Array<Number>`
* **Description**: Returns an array containing the sequence of numbers from `start` up to (but not including) `end`, incremented by `step`. If called with one argument `range(n)`, returns `0` to `n - 1`.
* **Example**:
  ```unfish
  say range(5)          # [0, 1, 2, 3, 4]
  say range(2, 6)       # [2, 3, 4, 5]
  say range(0, 10, 2)   # [0, 2, 4, 6, 8]
  ```

### 2.8. `keys(map)` / `values(map)`
* **Signature**: `keys(map: Map<Any, Any>): Array<Any>`, `values(map: Map<Any, Any>): Array<Any>`
* **Description**: Returns an array containing all keys or values present in the hash map.
* **Example**:
  ```unfish
  let config = {"host": "localhost", "port": 8080}
  say keys(config)   # ["host", "port"]
  say values(config) # ["localhost", 8080]
  ```

### 2.9. `has_key(map, key)` / `delete(map, key)`
* **Signature**: `has_key(map: Map<Any, Any>, key: Any): Boolean`, `delete(map: Map<Any, Any>, key: Any): Boolean`
* **Description**: `has_key` tests if `key` exists in `map`. `delete` removes `key` from `map`, returning `true` if found and removed.
* **Example**:
  ```unfish
  let user = {"id": 101, "role": "admin"}
  say has_key(user, "role") # true
  delete(user, "role")
  say has_key(user, "role") # false
  ```

### 2.10. High-Order Collection Functions: `map`, `filter`, `reduce`
* `map(arr, fn)`: Transforms each element with `fn(item)`.
* `filter(arr, fn)`: Retains elements where `fn(item)` is truthy.
* `reduce(arr, fn, initial)`: Accumulates values via `fn(accumulator, item)`.
* **Example**:
  ```unfish
  let numbers = [1, 2, 3, 4, 5]
  let squares = map(numbers, function(x): return x * x)
  let evens = filter(squares, function(x): return x % 2 == 0)
  let sum = reduce(evens, function(acc, x): return acc + x, 0)
  say sum # 20 (4 + 16)
  ```

### 2.11. `sort(array, comparator = null)`
* **Signature**: `sort(array: Array<Any>, comparator: Function = null): Array<Any>`
* **Description**: Sorts `array` in place using Quicksort. If `comparator` is omitted, numbers and strings are sorted in ascending order.
* **Example**:
  ```unfish
  let fruits = ["banana", "apple", "cherry"]
  sort(fruits)
  say fruits # ["apple", "cherry", "banana"]
  ```

### 2.12. `reverse(array)`
* **Signature**: `reverse(array: Array<Any>): Array<Any>`
* **Description**: Reverses the order of elements in `array` in place.

### 2.13. `find(array, predicate)` / `every(array, predicate)` / `some(array, predicate)`
* `find(arr, fn)`: Returns first element satisfying `fn(item)`, or `null`.
* `every(arr, fn)`: Returns `true` if all elements satisfy `fn(item)`.
* `some(arr, fn)`: Returns `true` if at least one element satisfies `fn(item)`.

### 2.14. `assert(condition, message = "Assertion failed")`
* **Signature**: `assert(condition: Any, message: String = "Assertion failed"): Null`
* **Description**: If `condition` is falsy (`false` or `null`), raises an assertion error with `message`.

---

## 3. String Processing Built-ins

| Function | Signature | Description |
|---|---|---|
| `split` | `(str: String, delim: String) -> Array<String>` | Splits string by delimiter |
| `join` | `(arr: Array<Any>, delim: String) -> String` | Joins array elements into a string |
| `trim` | `(str: String) -> String` | Removes leading and trailing whitespace |
| `trim_start` | `(str: String) -> String` | Removes leading whitespace |
| `trim_end` | `(str: String) -> String` | Removes trailing whitespace |
| `replace` | `(str: String, old: String, new: String) -> String`| Replaces all occurrences of `old` with `new` |
| `to_upper` | `(str: String) -> String` | Converts string to uppercase ASCII |
| `to_lower` | `(str: String) -> String` | Converts string to lowercase ASCII |
| `contains` | `(str: String, needle: String) -> Boolean` | Returns `true` if `needle` is found |
| `starts_with`| `(str: String, prefix: String) -> Boolean` | Returns `true` if string starts with `prefix` |
| `ends_with` | `(str: String, suffix: String) -> Boolean` | Returns `true` if string ends with `suffix` |
| `char_at` | `(str: String, index: Number) -> String` | Returns single character at index |
| `chars` | `(str: String) -> Array<String>` | Returns array of single-character strings |
| `count` | `(str: String, sub: String) -> Number` | Returns count of non-overlapping occurrences |
| `to_number` | `(str: String) -> Number` | Parses string as a number |
| `to_string` | `(val: Any) -> String` | Converts value to string representation |
| `repeat_string`| `(str: String, n: Number) -> String` | Repeats string `n` times |
| `substring` | `(str: String, start: Number, len: Number) -> String`| Extracts substring of length `len` from `start` |
| `index_of` | `(str: String, sub: String) -> Number` | Returns 0-indexed position, or `-1` |
| `pad_start` | `(str: String, len: Number, pad: String = " ") -> String` | Pads string on the left to target length |
| `pad_end` | `(str: String, len: Number, pad: String = " ") -> String` | Pads string on the right to target length |

---

## 4. Mathematics Built-ins & Constants

### Constants
* `PI`: `3.141592653589793`
* `E`: `2.718281828459045`
* `INFINITY`: IEEE 754 positive infinity ($+\infty$)

### Functions
* `abs(x)`: Absolute value.
* `floor(x)`, `ceil(x)`, `round(x)`: Rounding down, up, and to nearest integer.
* `sqrt(x)`: Square root. Raises error if $x < 0$.
* `pow(base, exp)`: Power $base^{exp}$.
* `min(a, b)`, `max(a, b)`: Minimum and maximum of two values.
* `log(x)`: Natural logarithm (base $e$).
* `sin(x)`, `cos(x)`, `tan(x)`: Trigonometric functions (radians).
* `random()`: Pseudorandom float in range $[0.0, 1.0)$.
* `random_int(min, max)`: Uniform integer in range $[min, max]$.

---

## 5. Standard Modules

Standard modules are loaded using `import <module>` or `from <module> import <symbols>`.

### 5.1. Module `sys` (`import sys`)
The `sys` module exposes execution environment state:
* `sys.argv: Array<String>`: List of command-line arguments passed to the script.
* `sys.platform: String`: Platform name (`"linux"`, `"darwin"`, `"windows"`, `"wasm"`).
* `sys.version: String`: Unfish version string (`"2.1.0"`).
* `sys.cwd(): String`: Returns current working directory.
* `sys.exit(code: Number = 0)`: Terminates process immediately with `code`.
* `sys.env(name: String): String`: Reads environment variable (or `null` if unset).
* `sys.set_env(name: String, val: String): Boolean`: Sets environment variable.
* `sys.exec(command: String): Number`: Executes shell command, returns exit code.

### 5.2. Module `fs` (`import fs`)
The `fs` module provides sandboxed filesystem I/O:
* `fs.read_file(path: String): String`: Reads entire file as UTF-8 string.
* `fs.write_file(path: String, content: String): Boolean`: Overwrites file with `content`.
* `fs.append_file(path: String, content: String): Boolean`: Appends `content` to file.
* `fs.exists(path: String): Boolean`: Returns `true` if path exists on disk.
* `fs.remove(path: String): Boolean`: Deletes a file.
* `fs.list_dir(path: String): Array<String>`: Lists file names within directory.
* `fs.mkdir(path: String): Boolean`: Creates directory.
* `fs.remove_dir(path: String): Boolean`: Removes empty directory.
* `fs.is_file(path: String): Boolean`: Returns `true` if path is a regular file.
* `fs.is_dir(path: String): Boolean`: Returns `true` if path is a directory.
* `fs.file_size(path: String): Number`: Returns file size in bytes.

### 5.3. Module `time` (`import time`)
* `time.now(): Number`: Returns Unix epoch timestamp in milliseconds.
* `time.sleep(ms: Number): Null`: Suspends execution for `ms` milliseconds.
* `time.format(ts: Number, fmt: String): String`: Formats timestamp via `strftime`.
* `time.iso(): String`: Returns current ISO 8601 UTC timestamp string.
* `time.parse(iso_str: String): Number`: Parses ISO 8601 string to millisecond timestamp.
* `time.diff_ms(t1: Number, t2: Number): Number`: Computes elapsed milliseconds.

### 5.4. Module `random` (`import random`)
* `random.random(): Number`: Random float in $[0.0, 1.0)$.
* `random.random_int(min: Number, max: Number): Number`: Random integer in $[min, max]$.
* `random.choice(arr: Array<Any>): Any`: Returns randomly chosen element.
* `random.shuffle(arr: Array<Any>): Array<Any>`: Shuffles array in place.
* `random.seed(val: Number): Null`: Seeds the pseudorandom number generator.

### 5.5. Module `json` (`import json`)
The `json` module provides high-speed, cycle-safe JSON serialization:
* `json.parse(text: String): Any`: Parses JSON into Unfish arrays, maps, numbers, booleans, and null.
* `json.stringify(value: Any, indent: Number = 0): String`: Serializes value to JSON. Detects circular references, raising `ERR_JSON_CYCLE` if a cycle is encountered.
* **Example**:
  ```unfish
  from json import parse, stringify

  let raw = '{"name": "Unfish", "version": 2}'
  let data = parse(raw)
  say data.name # "Unfish"

  data.active = true
  say stringify(data) # '{"name":"Unfish","version":2,"active":true}'
  ```

### 5.6. Module `testing` (`import testing`)
The `testing` module provides a declarative unit testing DSL:
```unfish
from testing import describe, test, assert_eq, run_tests

describe("Math Suite", function():
    test("addition test", function():
        assert_eq(2 + 2, 4)
    )
    test("string concat test", function():
        assert_eq("un" + "fish", "unfish")
    )
)

run_tests()
```
* `describe(name: String, fn: Function)`: Declares test suite.
* `test(name: String, fn: Function)`: Declares individual test case.
* `assert_eq(actual, expected)`: Asserts structural equality.
* `assert_ne(actual, unexpected)`: Asserts inequality.
* `assert_true(cond)` / `assert_false(cond)`: Asserts boolean truth.
* `assert_null(val)` / `assert_not_null(val)`: Asserts nullness.
* `run_tests()`: Executes all suites, printing pass/fail counts and timing.
