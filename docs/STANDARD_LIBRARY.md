# UNFISH — STANDARD LIBRARY SPECIFICATION

---

## 1. Principles

1. **Explicit Boundaries**: Unfish code never has unrestricted, accidental access to the host operating system. All host interactions are explicitly routed through sandboxed native interfaces.
2. **Minimal Initial Core**: Keep the initial runtime core small, clean, and robust before adding modules.
3. **Approachable & Expressive**: Names are intuitive and clear (`say` for printing with a newline, `len` for length, `type_of` for inspecting types).

## 2. Built-in Core Functions (Phase 1–2)

### `say(value)`
* **Description**: Evaluates `value`, converts it to a string, and outputs it to standard output followed by a newline.
* **Returns**: `null`.
* **Note**: Primary beginner-friendly output mechanism. Also supported as a dedicated statement `say <expr>`.

### `print(value)`
* **Description**: Outputs `value` without appending a trailing newline.
* **Returns**: `null`.

### `type_of(value)`
* **Description**: Returns a string describing the runtime type of `value`: `"null"`, `"boolean"`, `"number"`, `"string"`, `"function"`.
* **Returns**: `String`.

### `len(collection_or_string)`
* **Description**: Returns the number of characters in a string or elements in an array.
* **Returns**: `Number`.

### `push(array, value)`
* **Description**: Appends `value` to the end of `array`, dynamically growing its capacity if needed.
* **Returns**: `null`.

### `pop(array)`
* **Description**: Removes and returns the last element from `array`. Returns `null` if the array is empty.
* **Returns**: `Any` (the popped value or `null`).

### `range([start,] end[, step])`
* **Description**: Generates an array of numbers from `start` (default: 0) up to (exclusive) `end` with step `step` (default: 1). Capped at 1,000,000 elements for safety.
* **Returns**: `Array`.

### `keys(map)`
* **Description**: Returns an array of all keys in `map` in insertion order.
* **Returns**: `Array`.

### `values(map)`
* **Description**: Returns an array of all values in `map` in insertion order.
* **Returns**: `Array`.

### `has_key(map, key)`
* **Description**: Returns `true` if `key` exists in `map`, or `false` otherwise.
* **Returns**: `Boolean`.

### `delete(map, key)`
* **Description**: Removes `key` and its associated value from `map`. Returns `true` if the key was present and deleted, or `false` otherwise.
* **Returns**: `Boolean`.

### `map(array, function)`
* **Description**: Returns a new array resulting from applying `function(element)` to each element of `array`.
* **Returns**: `Array`.

### `filter(array, function)`
* **Description**: Returns a new array containing all elements of `array` for which `function(element)` evaluates to a truthy value.
* **Returns**: `Array`.

### `reduce(array, function, [initial])`
* **Description**: Applies `function(accumulator, element)` across `array` from left to right to reduce it to a single value. If `initial` is omitted, the first element is used as the initial accumulator.
* **Returns**: `Any`.

### `sort(array, [comparator])`
* **Description**: Returns a new sorted array. If `comparator(a, b)` is omitted, sorts numbers and strings in natural ascending order. If provided, sorts using the comparator function (returning a negative number if `a < b`, positive if `a > b`, or 0 if equal).
* **Returns**: `Array`.

### `reverse(array)`
* **Description**: Returns a new array with elements in reversed order.
* **Returns**: `Array`.

### `find(array, function)`
* **Description**: Returns the first element in `array` for which `function(element)` evaluates to truthy, or `null` if no element matches.
* **Returns**: `Any` or `null`.

### `every(array, function)`
* **Description**: Returns `true` if `function(element)` evaluates to truthy for every element in `array`, or `false` otherwise. Returns `true` for empty arrays.
* **Returns**: `Boolean`.

### `some(array, function)`
* **Description**: Returns `true` if `function(element)` evaluates to truthy for at least one element in `array`, or `false` otherwise.
* **Returns**: `Boolean`.

### `clock()`
* **Description**: Returns elapsed wall-clock time in seconds as a high-resolution `Number`. Useful for benchmarking student algorithms.
* **Returns**: `Number`.

### `assert(condition, [message])`
* **Description**: Raises an `AssertionError` if `condition` is falsy.
* **Returns**: `null`.

### `error(message, [kind])`
* **Description**: Raises an exception with the given `message` and optional `kind` string (defaults to `"UserError"`). Can be caught by enclosing `try ... catch <err>:` blocks. The resulting error object exposes properties:
  - `err.message`: Error message (`String`).
  - `err.kind`: Exception category name (`String`).
  - `err.line`: Source code line number (`Number`).
  - `err.file`: Source code file name (`String`).
* **Returns**: Never returns normally; triggers stack unwinding via `longjmp`.

## 3. String Standard Library

| Function | Signature | Description |
|---|---|---|
| `split(str, delim)` | `(String, String) -> Array` | Splits `str` around delimiter `delim`. If `delim` is `""`, splits into characters. |
| `join(arr, sep)` | `(Array, String) -> String` | Concatenates elements of `arr` separated by `sep`. |
| `trim(str)` | `(String) -> String` | Removes leading and trailing whitespace. |
| `replace(str, old, new)` | `(String, String, String) -> String` | Replaces occurrences of `old` with `new`. |
| `to_upper(str)` | `(String) -> String` | Converts string to uppercase. |
| `to_lower(str)` | `(String) -> String` | Converts string to lowercase. |
| `contains(str, sub)` | `(String, String) -> Boolean` | Returns `true` if `sub` is found inside `str`. |
| `starts_with(str, pfx)` | `(String, String) -> Boolean` | Returns `true` if `str` begins with prefix `pfx`. |
| `ends_with(str, sfx)` | `(String, String) -> Boolean` | Returns `true` if `str` ends with suffix `sfx`. |
| `char_at(str, idx)` | `(String, Number) -> String` | Character at index with negative indexing support (`""` if out of bounds). |
| `to_number(str)` | `(String) -> Number or null` | Parses numeric string, returning `null` on invalid input. |
| `to_string(val)` | `(Any) -> String` | Converts any runtime value to string. |
| `repeat_string(str, n)` | `(String, Number) -> String` | Repeats `str` `n` times. |
| `substring(str, start, [end])` | `(String, Number, [Number]) -> String` | Slices string from `start` to `end` with negative indexing support. |
| `index_of(str, sub)` | `(String, String) -> Number` | 0-based index of first occurrence, or `-1` if not found. |

## 4. Math Standard Library

### Constants
* `PI`: `3.14159265358979323846`
* `E`: `2.71828182845904523536`
* `INFINITY`: IEEE 754 positive infinity

### Functions
* `abs(x)`: Absolute value of `x`.
* `floor(x)`: Greatest integer less than or equal to `x`.
* `ceil(x)`: Smallest integer greater than or equal to `x`.
* `round(x)`: Nearest integer to `x`.
* `sqrt(x)`: Square root of `x` (domain error if `x < 0`).
* `pow(base, exp)`: `base` raised to `exp`.
* `min(a, b)`: Minimum of two numbers.
* `max(a, b)`: Maximum of two numbers.
* `log(x)`: Natural logarithm of `x` (domain error if `x <= 0`).
* `sin(x)`: Sine of `x` in radians.
* `cos(x)`: Cosine of `x` in radians.
* `tan(x)`: Tangent of `x` in radians.
* `random()`: Random floating point number in `[0.0, 1.0)`.
* `random_int(min, max)`: Random integer in `[min, max]` inclusive.

---

## 5. Built-in Standard Library Modules (Milestone 9)

### 5.1. `sys` Module
* **Import**: `import sys` or `from sys import exit, args, platform, env`
* **Functions**:
  - `sys.exit(code)`: Immediately terminates program execution with integer exit code `code`.
  - `sys.args()`: Returns an array of strings representing command line arguments passed to the Unfish script.
  - `sys.platform()`: Returns the host operating system identifier string: `"linux"`, `"darwin"`, `"windows"`, or `"unknown"`.
  - `sys.env(name)`: Returns the string value of the environment variable `name`, or `null` if unset.

### 5.2. `fs` Module (Sandboxed Filesystem)
* **Import**: `import fs` or `from fs import read_text, write_text, exists, delete_file`
* **Functions**:
  - `fs.read_text(path)`: Reads the entire contents of file at `path` as a UTF-8 string. Returns `null` if the file cannot be opened or read.
  - `fs.write_text(path, content)`: Writes `content` string to file at `path`. Returns `true` on success, `false` on failure.
  - `fs.exists(path)`: Returns `true` if a file or directory exists at `path`, `false` otherwise.
  - `fs.delete_file(path)`: Deletes the file at `path`. Returns `true` on success, `false` on failure.

### 5.3. `random` Module
* **Import**: `import random` or `from random import random, random_int, choice, shuffle`
* **Functions**:
  - `random.random()`: Returns a pseudo-random floating point number in `[0.0, 1.0)`.
  - `random.random_int(min, max)`: Returns a pseudo-random integer in `[min, max]` inclusive.
  - `random.choice(array)`: Returns a randomly selected element from `array`, or `null` if empty.
  - `random.shuffle(array)`: Returns a new array with elements of `array` randomly permuted (Fisher-Yates shuffle).

### 5.4. `time` Module
* **Import**: `import time` or `from time import clock, sleep, timestamp`
* **Functions**:
  - `time.clock()`: Returns a high-resolution monotonic timer value in seconds as a `Number`.
  - `time.sleep(seconds)`: Pauses process execution for `seconds` floating-point seconds. Returns `null`.
  - `time.timestamp()`: Returns the current UNIX epoch timestamp in seconds as a `Number`.

### 5.5. `json` Module
* **Import**: `import json` or `from json import parse, stringify`
* **Functions**:
  - `json.parse(json_string)`: Parses valid JSON text into Unfish values (`null`, `boolean`, `number`, `string`, `array`, `map`). Returns `null` if the input is malformed.
  - `json.stringify(value)`: Serializes any Unfish value into a formatted JSON string.

### 5.6. `testing` Module
* **Import**: `import testing` or `from testing import assert_equal, assert_true, assert_throws, run_tests`
* **Location**: `src/stdlib/testing.unfish`
* **Functions**:
  - `assert_equal(actual, expected, [message])`: Raises `AssertionError` if `actual != expected`.
  - `assert_true(condition, [message])`: Raises `AssertionError` if `condition` is falsy.
  - `assert_throws(fn, [message])`: Executes `fn()` inside a `try/catch` block and asserts that an exception was raised.
  - `run_tests(suite_map)`: Takes a map of test names to nullary functions, executes each with error isolation, prints per-test PASS/FAIL logs, and returns `true` if all passed.
