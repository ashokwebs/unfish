# UNFISH — TYPE SYSTEM SPECIFICATION

---

## 1. Feature Status Inventory

| Tier / Feature | Status | Implementation Reference |
|---|---|---|
| Dynamic Tagged Union Representation (`UfValue`) | **IMPLEMENTED** | `src/runtime/uf_value.h` |
| Primitive Types (`Null`, `Boolean`, `Number`) | **IMPLEMENTED** | `src/runtime/uf_value.c` |
| UTF-8 Dynamic Strings (`String`) | **IMPLEMENTED** | `src/runtime/uf_value.c` |
| First-Class Functions & Closures (`Function`, `Closure`) | **IMPLEMENTED** | `src/runtime/uf_value.c`, `src/runtime/uf_env.c` |
| Native Host Functions (`NativeFunction`) | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| Bytecode Functions (`BytecodeFunction`) | **IMPLEMENTED** | `src/compiler/uf_compiler.c`, `src/vm/uf_vm.c` |
| Collections (`Array`, `Map`) | **IMPLEMENTED** | `src/runtime/uf_value.c` |
| Error Objects (`Error`) | **IMPLEMENTED** | `src/runtime/uf_value.c` |
| Modules (`Module`) | **IMPLEMENTED** | `src/runtime/uf_module.c` |
| User-Defined Structs & Instances (`StructDef`, `Instance`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/runtime/uf_value.c` |
| Cooperative Concurrency (`Fiber`, `Channel`) | **IMPLEMENTED** | `src/runtime/uf_fiber.c`, `src/runtime/uf_value.c` |
| Raw Contiguous Byte Buffers (`Buffer`) | **IMPLEMENTED** | `src/runtime/uf_stdlib.c`, `src/runtime/uf_value.c` |
| Gradual Type Annotations (`let x: Number`) | **IMPLEMENTED** | `src/semantic/uf_semantic.c` |
| Generic Types (`Array<T>`, `Result<T, E>`) | **NOT IMPLEMENTED** | Deferred to Phase 9 |

---

## 2. Currently Implemented Type Model

Unfish currently implements a strongly checked dynamic type model where types are properties of values:

### 2.1. Value Representation (`UfValue`) [IMPLEMENTED]
All values fit into a 16-byte tagged union representing 17 distinct runtime kinds:
```c
typedef enum {
    UF_VAL_NULL,
    UF_VAL_BOOL,
    UF_VAL_NUMBER,
    UF_VAL_STRING,
    UF_VAL_FUNCTION,
    UF_VAL_NATIVE_FN,
    UF_VAL_ARRAY,
    UF_VAL_MAP,
    UF_VAL_ERROR,
    UF_VAL_MODULE,
    UF_VAL_STRUCT_DEF,
    UF_VAL_INSTANCE,
    UF_VAL_BYTECODE_FN,
    UF_VAL_CLOSURE,
    UF_VAL_FIBER,
    UF_VAL_CHANNEL,
    UF_VAL_BUFFER
} UfValueKind;

struct UfValue {
    UfValueKind kind;
    union {
        bool boolean;
        double number;
        UfStringObject* string;
        UfFunctionObject* function;
        UfNativeObject native_fn;
        UfArrayObject* array;
        UfMapObject* map;
        UfErrorObject* error;
        UfModuleObject* module;
        UfStructDefObject* struct_def;
        UfInstanceObject* instance;
        UfBytecodeFunction* bytecode_fn;
        UfClosureObject* closure;
        UfFiber* fiber;
        UfChannel* channel;
        UfBufferObject* buffer;
    } as;
};
```

### 2.2. Semantics & Coercion Rules [IMPLEMENTED]
* **Implicit Coercion**: Minimal by design. The only implicit coercion permitted is in binary `+`: if either operand is a string, the other operand is converted to a string and concatenated.
* **Truthiness**: Only `false`, `null`, `0`, empty string `""`, empty array `[]`, and empty map `{}` are falsy. All other values are truthy.
* **Strict Type Safety**: Incompatible operations (e.g. `true * 5` or `"str" - 2`) trigger an explicit `TypeError` at runtime.

---

## 3. Pedagogical Type System Progression [DESIGN / ROADMAP]

Unfish is designed to accompany learners from elementary programming to professional software engineering and compiler construction. Its type system progresses intentionally across four distinct tiers:

### Tier 1: Pure Dynamic Typing [IMPLEMENTED]
* **Target Audience**: Absolute beginners, exploratory prototyping.
* **Syntax**: `let x = 42`, `function add(a, b): return a + b`.
* **Semantics**: Variables are untyped slots; values carry runtime tags (`UfValueKind`).
* **Tooling**: Semantic analyzer verifies variable scope, undefined identifiers, and function arity; runtime traps type mismatches.

### Tier 2: Optional Type Annotations & Local Inference [DESIGN / PHASE 4]
* **Target Audience**: Students learning contract design and code clarity.
* **Syntax**:
  ```unfish
  let count: Number = 0
  let name: String = "Alice"
  function greet(user: String): String:
      return "Hello, " + user
  ```
* **Semantics**: Type annotations are checked during `uf_analyze_program`. Code with annotations still executes identically on the runtime.
* **Pedagogical Benefit**: Introduces the concept of function signatures and invariant specification without compiler errors blocking visual-to-text transitions.

### Tier 3: Gradual Type Checking & Structural Interfaces [PLANNED / PHASE 4-5]
* **Target Audience**: Advanced students building multi-file projects, algorithms, and data structures.
* **Syntax**:
  ```unfish
  struct Point:
      x: Number
      y: Number

  function distance(p1: Point, p2: Point): Number:
      let dx = p1.x - p2.x
      let dy = p1.y - p2.y
      return math.sqrt(dx * dx + dy * dy)
  ```
* **Semantics**: Static type checker rejects incompatible types at compile time (`unfish check`). Unannotated code remains dynamically typed (`Any`), allowing gradual migration.

### Tier 4: Compile-Time Monomorphization & Ahead-of-Time Specialization [PLANNED / PHASE 9-10]
* **Target Audience**: Systems programming, operating systems, compiler construction.
* **Semantics**: Fully typed functions are monomorphized and compiled directly to unboxed native machine code or specialized bytecode instructions, demonstrating how types eliminate runtime overhead.
