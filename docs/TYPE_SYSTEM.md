# UNFISH — TYPE SYSTEM SPECIFICATION

---

## 1. Type Philosophy: The Progression from Dynamic to Gradual

Unfish adopts an educational and evolutionary type philosophy:
1. **Initial Tier (Phase 1–4)**: Dynamically typed with strong runtime safety. Types are properties of values, not variable declarations. Implicit type coercions are minimized (only string conversion in `+` with string operands is permitted).
2. **Intermediate Tier (Phase 5–7)**: Optional gradual type annotations (e.g. `let count: Number = 0`, `function add(a: Number, b: Number) -> Number:`). Semantic analysis enforces type consistency where annotations exist.
3. **Advanced Tier (Phase 8–10)**: Structural types, interfaces/traits, generics (`Array<T>`, `Result<T, E>`), and fixed-size systems types (`i32`, `u8`, `Ptr<T>`) for low-level systems programming.

## 2. Fundamental Types (Phase 1)

### 2.1. Primitive Types
* **`Null`**: Represents absence of value. Literal: `null`.
* **`Boolean`**: Truth values. Literals: `true`, `false`.
* **`Number`**: 64-bit IEEE 754 double precision floating-point (`double` in C). Whole numbers format without trailing decimal points (e.g., `42` rather than `42.0`).
* **`String`**: UTF-8 encoded, immutable character sequences. Length-prefixed and null-terminated for C interoperability.
* **`Function`**: First-class user-defined callable consisting of an AST parameter list, body block, and a captured lexical environment (closure).
* **`NativeFunction`**: First-class callable implemented in C conforming to `UfValue (*UfNativeFn)(UfVM* vm, int argc, UfValue* args)`.

### 2.2. Value Discriminant (`UfValueKind`)
```c
typedef enum {
    UF_VAL_NULL,
    UF_VAL_BOOL,
    UF_VAL_NUMBER,
    UF_VAL_STRING,
    UF_VAL_FUNCTION,
    UF_VAL_NATIVE_FN
} UfValueKind;
```

## 3. Type Checking & Coercion Rules

| Expression | Operands | Rule | Result Type |
|---|---|---|---|
| `a + b` | Number, Number | Numeric addition | Number |
| `a + b` | String, Any | String concatenation (converts `b` to string) | String |
| `a + b` | Any, String | String concatenation (converts `a` to string) | String |
| `a - b`, `*`, `/`, `%` | Number, Number | Arithmetic | Number |
| `a < b`, `<=`, `>`, `>=` | Number, Number | Numeric ordering | Boolean |
| `a == b`, `!=` | Any, Any | Identity / value equality | Boolean |
| `not a` | Any | Logical negation of truthiness | Boolean |
| `a and b`, `a or b` | Any, Any | Short-circuit return of evaluated operand | Any |

Any other arithmetic operation on incompatible types (such as `true * 5` or `null / 2`) triggers a compile-time semantic error if determinable, or an explicit runtime `TypeError`.
