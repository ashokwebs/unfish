# UNFISH — MODULE SYSTEM & PACKAGE RESOLUTION SPECIFICATION

---

## 1. Executive Summary & Design Principles

The Unfish module system (`src/runtime/uf_module.c`) enables modular software architecture through structured code organization, encapsulation, and dependency resolution:
1. **Explicit Symbol Binding**: Symbols imported into a file are cleanly bound to specific namespaces or identifiers, preventing global namespace pollution.
2. **Deterministic Resolution**: Module paths are resolved unambiguously across native modules, relative paths, and package roots.
3. **Singleton Evaluation**: A module is evaluated at most once per runtime session, guaranteeing that side effects and top-level state are shared deterministically.
4. **Cycle Prevention**: Circular dependencies are detected statically or at load time, terminating with informative cycle traces rather than hanging in infinite recursion.

---

## 2. Module Syntax & Import Semantics

Unfish provides two primary import forms: **Full Module Import** and **Selective Symbol Import**.

### 2.1. Full Module Import (`import`)
```unfish
import sys
import fs as filesystem
import "./math/geometry.unfish" as geom

say sys.platform
filesystem.write_file("output.txt", "Data")
let p = geom.Point(10, 20)
```
* **Binding**: Binds the entire module namespace to a local variable named after the module (or the alias specified with `as`).
* **Access**: Exported functions, structs, and variables are accessed via dot notation (`geom.Point`).

### 2.2. Selective Symbol Import (`from ... import`)
```unfish
from json import parse, stringify
from math import PI as P, sin, cos
from "./vector.unfish" import Vector2

let data = parse('{"x": 10}')
say P
let v = Vector2(3, 4)
```
* **Binding**: Binds only the explicitly listed exported symbols into the current lexical scope.
* **Aliasing**: Individual imported symbols can be renamed locally using `as`.

---

## 3. Module Resolution Algorithm (`src/runtime/uf_module.c`)

When an `import <target>` statement is encountered, the runtime resolves `<target>` using a four-tier hierarchical algorithm:

```
Target: "utils" or "./utils.unfish"
    │
    ▼
1. Is it a Built-in Standard Module? (sys, fs, time, random, json, testing)
    ├── Match name against internal native module table
    └── Return built-in native module singleton
    │
    ▼ No
2. Is it a Relative File Path? (Starts with "./" or "../")
    ├── Canonicalize path relative to directory of importing source file
    ├── Append ".unfish" extension if omitted
    └── Found file on disk? Return canonical path
    │
    ▼ No
3. Search Package Directories (UNFISH_PATH & src/)
    ├── Check $UNFISH_PATH/target.unfish
    ├── Check ./src/target.unfish
    └── Found file on disk? Return canonical path
    │
    ▼ No
4. Resolution Failure
    └── Raise ERR_MODULE_NOT_FOUND (Exit Code 3)
```

### Path Normalization & Canonicalization
All file paths are converted to absolute canonical paths (`realpath` on POSIX, `_fullpath` on Windows) before caching. This guarantees that importing `./foo.unfish` and `../current/foo.unfish` resolves to the identical module instance.

---

## 4. Module Registry & Singleton Caching

Module execution is managed by the `UfModuleRegistry` in `UfRuntime`:

```c
typedef enum {
    MOD_UNLOADED,
    MOD_LOADING,
    MOD_LOADED
} UfModuleState;

typedef struct UfModuleObject {
    UfObj obj;
    UfStringObject* path;      /* Canonical absolute file path */
    UfModuleState state;       /* Cycle detection state */
    UfEnv* env;                /* Lexical environment containing exports */
} UfModuleObject;
```

### Execution Lifecycle:
1. **Cache Inspection**: The registry checks if `path` is already present in `rt->modules`.
   * If `state == MOD_LOADED`: The cached `UfModuleObject` is returned immediately without re-reading or re-executing the file.
   * If `state == MOD_LOADING`: A circular dependency has been detected (see Section 5).
2. **Initialization**: A new `UfModuleObject` is allocated with `state = MOD_LOADING` and inserted into the registry.
3. **Parsing & Compilation**: The module source file is read, lexed, parsed into an AST, and compiled into bytecode or evaluated by the interpreter.
4. **Execution**: The module's top-level code executes in its own fresh `UfEnv` scope.
5. **Finalization**: `state` transitions to `MOD_LOADED`. All declared symbols remaining in the module's environment become the module's public exports.

---

## 5. Circular Dependency Detection

Circular imports occur when module `A` imports module `B`, and module `B` directly or transitively imports module `A`:

```
┌──────────────┐          import B          ┌──────────────┐
│   Module A   │ ────────────────────────►  │   Module B   │
│ (MOD_LOADING)│ ◄────────────────────────  │ (MOD_LOADING)│
└──────────────┘          import A          └──────────────┘
```

When `Module B` attempts to import `Module A`, the registry observes that `Module A` is currently in state `MOD_LOADING`.

Rather than entering an infinite mutual recursion loop or creating partially-initialized bindings, the runtime immediately aborts execution and reports an `ERR_CIRCULAR_IMPORT` diagnostic (exit code 3):

```
Runtime Error: Circular dependency detected while importing module 'helper_circ_a.unfish'
   Cycle: helper_circ_a.unfish -> helper_circ_b.unfish -> helper_circ_a.unfish
```

---

## 6. Symbol Visibility & Export Semantics

Unfish implements a clean, convention-free export model:
* **All Top-Level Declarations are Exported**: Any `let` variable, `function`, `struct`, or `enum` declared at the top level of a module is part of that module's public API.
* **Module Encapsulation**: Inner variables declared inside functions, blocks, or loops are strictly private and cannot be accessed externally.
* **Import Symbol Validation**: When using `from mod import sym`, if `sym` does not exist in `mod`, the runtime raises `ERR_IMPORT_SYMBOL` (exit code 3), displaying available exports.

---

## 7. Package Architecture & `unfish.toml`

For multi-file projects and libraries, Unfish provides package management via `unfish pkg`:

### 7.1. Package Directory Layout
```
my_project/
├── unfish.toml           # Package manifest
├── src/                  # Library and application source files
│   ├── main.unfish       # Application entry point
│   ├── utils.unfish      # Project module
│   └── models/
│       └── user.unfish   # Sub-module
└── tests/                # Automated test suites
    └── test_utils.unfish # Test files
```

### 7.2. Package Manifest (`unfish.toml`)
```toml
[package]
name = "my_project"
version = "1.0.0"
entry = "src/main.unfish"
description = "A production Unfish application"

[dependencies]
# Future package repository dependencies
```

The package manager CLI provides commands to manage the package lifecycle:
* `unfish pkg init`: Scaffolds a new package.
* `unfish pkg check`: Semantically validates all files in `src/`.
* `unfish pkg run`: Executes the package entry point.
* `unfish pkg test`: Runs the test suite in `tests/`.
* `unfish pkg build`: Compiles the package to a standalone native binary.
