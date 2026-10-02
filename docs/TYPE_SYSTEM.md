# UNFISH — GRADUAL TYPE SYSTEM & SEMANTIC ANALYSIS SPECIFICATION

---

## 1. Executive Summary & Philosophy

Unfish features a **4-Tier Gradual Type System** designed to bridge the pedagogical and practical divide between dynamic scripting and rigorous static verification:

1. **Pedagogical Fluidity**: Novices can write completely dynamic, unannotated code without learning type signatures, type parameters, or generics.
2. **Progressive Rigor**: As programs grow in size or students advance into software engineering topics, explicit type annotations can be added incrementally to critical boundaries (function signatures, struct fields, module exports).
3. **Static & Dynamic Safety**: The semantic analyzer validates type annotations at compile time. In addition, `--strict` mode transforms gradual type inconsistencies into fatal compilation errors, guaranteeing complete type safety prior to execution.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        THE 4-TIER GRADUAL TYPE SPECTRUM                │
├──────────────┬──────────────────┬───────────────────┬──────────────────┤
│ Tier 0       │ Tier 1           │ Tier 2            │ Tier 3           │
│ DYNAMIC      │ INFERRED         │ ANNOTATED         │ STRICT           │
├──────────────┼──────────────────┼───────────────────┼──────────────────┤
│ `let x = 42` │ `let x = 42`     │ `let x: Number = 42` `unfish check   │
│              │ Inferred: Number │ Checked at compile│        --strict` │
│ No checks    │ Local propagation│ time & runtime    │ Zero mismatch    │
│ Pure dynamic │ Fast diagnostics │ Explicit contracts│ Hard error       │
└──────────────┴──────────────────┴───────────────────┴──────────────────┘
```

---

## 2. The Type Hierarchy & Type Lattice

Every value in Unfish belongs to a concrete runtime type, organized under a formal type lattice rooted at the dynamic top type `Any` (conventionally denoted $\star$ in gradual typing literature):

```
                                      ┌─────────┐
                                      │   Any   │  (Dynamic Top Type ★)
                                      └────┬────┘
           ┌───────────────────────────────┼──────────────────────────────┐
           │                               │                              │
           ▼                               ▼                              ▼
    ┌─────────────┐                 ┌─────────────┐                ┌─────────────┐
    │  Primitives │                 │  Composites │                │  Callable   │
    ├─────────────┤                 ├─────────────┤                ├─────────────┤
    │ Null        │                 │ Array<T>    │                │ (T...) -> R │
    │ Boolean     │                 │ Map<K, V>   │                └─────────────┘
    │ Number      │                 │ Struct<T>   │
    │ String      │                 │ Enum<T>     │
    └─────────────┘                 └──────┬──────┘
                                           │
                        ┌──────────────────┴──────────────────┐
                        ▼                                     ▼
                 ┌─────────────┐                       ┌─────────────┐
                 │   Systems   │                       │ Concurrency │
                 ├─────────────┤                       ├─────────────┤
                 │ Buffer      │                       │ Fiber       │
                 │             │                       │ Channel<T>  │
                 │             │                       │ Promise<T>  │
                 └─────────────┘                       └─────────────┘
```

### 2.1. Primitive Types
* **`Null`**: The singleton type inhabited only by `null`.
* **`Boolean`**: Inhabited by `true` and `false`.
* **`Number`**: 64-bit IEEE 754 double precision floating-point numbers. Represents both integers and fractional numbers.
* **`String`**: Immutable, UTF-8 encoded sequence of bytes.

### 2.2. Composite Types
* **`Array<T>`**: Dynamically resizable, ordered sequence of elements of type `T`. When unspecialized, equivalent to `Array<Any>`.
* **`Map<K, V>`**: Hash map associative table mapping keys of type `K` (typically `String` or `Number`) to values of type `V`.
* **`Struct`**: Nominal user-defined record type with named fields and methods.
* **`Enum`**: Nominal sum type (algebraic data type) with named variants and optional payload values.

### 2.3. Systems & Concurrency Types
* **`Buffer`**: Contiguous raw byte array representing unmanaged binary memory.
* **`Fiber`**: Lightweight cooperative coroutine thread of execution.
* **`Channel<T>`**: Synchronous or buffered message queue passing values of type `T`.
* **`Promise<T>`**: Asynchronous computation handle yielding a value of type `T`.

---

## 3. The 4-Tier Gradual Type Model

### Tier 0: Dynamic (Unannotated)
In Tier 0, code is completely unannotated. All variables and expressions have the implicit type `Any`:
```unfish
function calculate(x, y):
    return x * 2 + y

say calculate(10, 5) # 25
```
Dynamic type errors (e.g. attempting to call a number as a function, or indexing `null`) are caught at runtime and raise clear, structured exceptions.

### Tier 1: Local Type Inference
The semantic analyzer (`src/semantic/uf_semantic.c`) performs intra-procedural type inference:
```unfish
let count = 0        # Inferred: Number
let title = "Unfish" # Inferred: String
let items = [1, 2]   # Inferred: Array<Number>
```
If an inferred variable is reassigned to an incompatible type within the same lexical scope, the analyzer warns the developer during `unfish check`.

### Tier 2: Explicit Gradual Type Annotations
Developers can explicitly declare types for variable bindings, function parameters, return values, and struct fields:
```unfish
let port: Number = 8080
let hostname: String = "localhost"

function send_request(host: String, port: Number): Boolean:
    # Compile-time checker validates that arguments match host: String and port: Number
    return true
```

### Tier 3: Strict Mode (`unfish check --strict`)
When the `--strict` flag is passed to the compiler or CLI:
1. Implicit conversions between conflicting concrete types are rejected.
2. Unannotated function parameters trigger fatal errors if strict annotation enforcement is enabled.
3. Struct field assignments must strictly match declared field types.
4. Trait bounds and generic constraints are rigorously checked at compile time.

---

## 4. Generics & Trait Bounds

Unfish supports parametric polymorphism with trait bounds:

### 4.1. Generic Structs & Functions
```unfish
struct Pair<T, U>:
    first: T
    second: U

    function swap(self): Pair<U, T>:
        return Pair(self.second, self.first)

function identity<T>(item: T): T:
    return item
```

### 4.2. Trait Definitions & Bounds
A `trait` defines a set of required method signatures:
```unfish
trait Serializable:
    function serialize(self): String

trait Printable:
    function to_string(self): String
```

Structs implement traits using explicit `impl` declarations:
```unfish
struct User:
    id: Number
    name: String

impl Serializable for User:
    function serialize(self): String:
        return '{"id": ' + str(self.id) + ', "name": "' + self.name + '"}'

impl Printable for User:
    function to_string(self): String:
        return self.name + " (#" + str(self.id) + ")"
```

### 4.3. Bound Checking in Semantic Analysis
When a generic function specifies a trait bound (`<T: Serializable>`), the semantic analyzer checks that any concrete type supplied to `T` implements the required trait:
```unfish
function write_to_disk<T: Serializable>(item: T, path: String):
    let data: String = item.serialize()
    # Writes serialized data to disk...
```
If a developer passes a struct that does not implement `Serializable`, the semantic analyzer emits a detailed diagnostic:
```
Error: Trait bound mismatch for generic parameter 'T'
   |
   | write_to_disk(non_serializable_item, "out.json")
   |               ^^^^^^^^^^^^^^^^^^^^^ Expected type implementing trait 'Serializable'
```

---

## 5. Formal Type Consistency Relation ($\sim$)

Gradual typing in Unfish is governed by the formal **Consistency Relation** ($\sim$):

$$\frac{}{T \sim T} \quad \text{[Reflexivity]}$$

$$\frac{}{\text{Any} \sim T} \quad \frac{}{T \sim \text{Any}} \quad \text{[Dynamic Consistency]}$$

$$\frac{T_1 \sim T_2}{\text{Array}\langle T_1 \rangle \sim \text{Array}\langle T_2 \rangle} \quad \text{[Covariant Array Consistency]}$$

$$\frac{K_1 \sim K_2 \quad V_1 \sim V_2}{\text{Map}\langle K_1, V_1 \rangle \sim \text{Map}\langle K_2, V_2 \rangle} \quad \text{[Covariant Map Consistency]}$$

$$\frac{P_1' \sim P_1 \quad \dots \quad P_n' \sim P_n \quad R \sim R'}{(P_1, \dots, P_n) \to R \sim (P_1', \dots, P_n') \to R'} \quad \text{[Function Consistency]}$$

### Consistency vs. Subtyping
* Consistency is **symmetric**: if $A \sim B$, then $B \sim A$.
* Consistency is **not transitive**: $\text{Number} \sim \text{Any}$ and $\text{Any} \sim \text{String}$, but $\text{Number} \not\sim \text{String}$. This formal property ensures that dynamic flexibility does not compromise static type distinction.

---

## 6. Semantic Analyzer Implementation (`src/semantic/uf_semantic.c`)

The semantic analyzer operates on the parsed Abstract Syntax Tree in four contiguous phases:

```
┌────────────────────────────────────────────────────────────────────────┐
│                      SEMANTIC ANALYSIS PIPELINE                        │
├────────────────────────────────────────────────────────────────────────┤
│ Pass 1: Symbol Table & Scope Tree Construction                         │
│         • Collect module symbols, struct definitions, trait signatures │
│         • Hoist top-level function declarations                        │
├────────────────────────────────────────────────────────────────────────┤
│ Pass 2: Scope Resolution & Binding                                     │
│         • Resolve every identifier to local, parameter, or global slot │
│         • Detect undeclared variables and duplicate declarations       │
├────────────────────────────────────────────────────────────────────────┤
│ Pass 3: Gradual Type Validation & Constraint Solving                   │
│         • Check expression type consistency against target annotations │
│         • Infer expression types upwards through the AST               │
├────────────────────────────────────────────────────────────────────────┤
│ Pass 4: Trait Bound & Struct Arity Verification                        │
│         • Validate all 'impl' blocks against declared traits           │
│         • Verify struct constructor arities and method 'self' rules    │
└────────────────────────────────────────────────────────────────────────┘
```

### Key Diagnostic Codes
| Error Code | Test File | Description |
|---|---|---|
| `ERR_TYPE_MISMATCH` | `tests/conformance/err_type_mismatch_strict.unfish` | Strict mode type mismatch between expression and annotation |
| `ERR_UNDEFINED_VAR` | `tests/conformance/err_undefined_var.unfish` | Referenced identifier is not bound in any enclosing scope |
| `ERR_DUPLICATE_DECL` | `tests/conformance/err_duplicate_decl.unfish` | Duplicate variable declaration within the same scope |
| `ERR_STRUCT_ARITY` | `tests/conformance/err_struct_arity.unfish` | Constructor argument count does not match struct field count |
| `ERR_STRUCT_NO_SELF` | `tests/conformance/err_struct_method_no_self.unfish`| Struct method missing mandatory `self` first parameter |
| `ERR_TRAIT_NOT_FOUND` | `tests/conformance/err_trait_not_found.unfish` | `impl` block references an undeclared trait |
| `ERR_TRAIT_PARAM` | `tests/conformance/err_trait_param_mismatch.unfish` | Implemented method signature differs from trait definition |
| `ERR_MISSING_METHOD` | `tests/conformance/err_missing_trait_method.unfish`| Struct `impl` fails to provide all required trait methods |
| `ERR_GENERIC_BOUND` | `tests/conformance/err_generic_bound_mismatch.unfish`| Type argument fails to satisfy declared trait bound |
