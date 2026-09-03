# UNFISH — TYPE SYSTEM SPECIFICATION

---

## 1. Feature Status Inventory

| Tier / Feature | Status | Implementation Reference |
|---|---|---|
| Dynamic Tagged Union Representation (`UfValue`) | **IMPLEMENTED** | `src/runtime/uf_value.h` |
| Primitive Types (`Null`, `Boolean`, `Number`) | **IMPLEMENTED** | `src/runtime/uf_value.c` |
| UTF-8 Dynamic Strings (`String`) | **IMPLEMENTED** | `src/runtime/uf_value.c` |
| First-Class Functions & Closures (`Function`) | **IMPLEMENTED** | `src/runtime/uf_value.c`, `src/runtime/uf_env.c` |
| Native Host Functions (`NativeFunction`) | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| String Concatenation Coercion in `+` | **IMPLEMENTED** | `src/interpreter/uf_interpreter.c` |
| Runtime Type Query (`type_of()`) | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| Collections (`Array`, `Map`) | **PLANNED** (Phase 3) | Next milestone |
| Gradual Type Annotations (`let x: Number`) | **PLANNED** (Phase 4) | Static checker pass |
| User-Defined Structs / Records | **PLANNED** (Phase 4) | Record system |
| Generic Types (`Array<T>`, `Result<T, E>`) | **NOT IMPLEMENTED** | Deferred to Phase 9 |
| Low-Level Systems Types (`i32`, `u8`, `Ptr<T>`) | **NOT IMPLEMENTED** | Deferred to Phase 10 |

---

## 2. Currently Implemented Type Model

Unfish currently implements a strongly checked dynamic type model where types are properties of values:

### 2.1. Value Representation (`UfValue`) [IMPLEMENTED]
All values fit into a 16-byte tagged union:
```c
typedef enum {
    UF_VAL_NULL,
    UF_VAL_BOOL,
    UF_VAL_NUMBER,
    UF_VAL_STRING,
    UF_VAL_FUNCTION,
    UF_VAL_NATIVE_FN
} UfValueKind;

struct UfValue {
    UfValueKind kind;
    union {
        bool boolean;
        double number;
        UfStringObject* string;
        UfFunctionObject* function;
        UfNativeObject native_fn;
    } as;
};
```

### 2.2. Semantics & Coercion Rules [IMPLEMENTED]
* **Implicit Coercion**: Minimal by design. The only implicit coercion permitted is in binary `+`: if either operand is a string, the other operand is converted to a string and concatenated.
* **Truthiness**: Only `false`, `null`, `0`, and empty string `""` are falsy. All other values are truthy.
* **Strict Type Safety**: Incompatible operations (e.g. `true * 5` or `"str" - 2`) trigger an explicit `TypeError` at runtime.

---

## 3. Evolutionary Roadmap

1. **Phase 3 (Next)**: Dynamic `Array` and `Map` collections.
2. **Phase 4**: Gradual optional type annotations enforced by the semantic analyzer.
3. **Phase 10**: Controlled low-level primitive types for systems programming.
