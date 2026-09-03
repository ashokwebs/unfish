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

## 3. Future Standard Library Modules (Phase 8 Roadmap)

* `math`: `sin`, `cos`, `sqrt`, `floor`, `ceil`, `abs`, `min`, `max`, `PI`, `E`.
* `strings`: `split`, `join`, `trim`, `replace`, `to_upper`, `to_lower`, `contains`.
* `collections`: `List`, `Map`, `Set`, `Stack`, `Queue`.
* `fs`: Sandboxed filesystem access (`read_text`, `write_text`, `exists`).
* `random`: `random()`, `random_int(min, max)`, `choice(list)`.
* `time`: `sleep(seconds)`, `timestamp()`.
