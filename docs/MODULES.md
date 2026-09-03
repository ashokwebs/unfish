# UNFISH — MODULE SYSTEM SPECIFICATION

---

## 1. Overview & Syntax (Phase 8 Roadmap)

Real-world projects require a robust module system. Unfish supports clean, deterministic module importing:

```unfish
import math
let hyp = math.sqrt(a * a + b * b)

from strings import to_upper, trim
let clean_title = to_upper(trim(raw_input))
```

## 2. Resolution Strategy

Modules are resolved deterministically:
1. **Built-in / Core Modules**: Checked first (e.g. `math`, `strings`, `sys`).
2. **Local Project Modules**: Relative to current file (`./utils.unfish` or `utils.unfish` in same directory).
3. **Module Search Path**: Configured via `UNFISH_PATH` environment variable or project root `unfish.json`.

## 3. Circular Dependency Handling

* Every module is loaded once and cached in the global module registry (`loaded_modules`).
* When a module begins evaluation, it is placed in an `evaluating_modules` set.
* If an import cycle is detected (`A -> B -> A`), the compiler raises a clear `CircularDependencyError` displaying the exact dependency cycle.

## 4. Namespace Rules
* Each module executes in its own isolated top-level `UfEnv`.
* Only explicitly exported identifiers (or all top-level symbols by default) are exposed on the imported module namespace object.
