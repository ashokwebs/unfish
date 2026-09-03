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
* **Description**: Returns the number of characters in a string (or elements in a future collection).
* **Returns**: `Number`.

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
