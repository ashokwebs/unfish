# UNFISH — ERROR MODEL, DIAGNOSTICS & EXCEPTION ARCHITECTURE

---

## 1. Philosophical Foundations of Error Handling

In introductory and systems programming, error messages are often a primary point of failure:
* **The "Syntax Error at Line 1" Syndrome**: Many educational languages emit terse, unhelpful error messages that leave novices bewildered.
* **The "Silent Fault" Syndrome**: Some dynamic languages attempt to coerce invalid types or return `undefined`, silently propagating corrupted state until catastrophic failure occurs far from the origin of the bug.
* **The "Segfault" Syndrome**: Low-level languages crash with cryptic memory faults without any stack context.

Unfish approaches error handling through two uncompromising architectural pillars:
1. **The Diagnostic Compass**: At compile time, every error is treated as a pedagogical opportunity. Diagnostics must pinpoint the exact character span, print the relevant source excerpt with gutter line numbers, underline the offending token with ANSI-colored squiggles, explain *why* the construct is invalid, and provide actionable suggestions for remediation.
2. **Deterministic Unwinding**: At runtime, unexpected conditions trigger structured, catchable exceptions. If uncaught, the runtime unwinds cleanly, closes all active upvalues, reclaims temporary allocations, and prints a pristine, human-readable stack trace with source coordinates across all five execution engines.

---

## 2. Compile-Time Diagnostics Engine (`src/common/uf_diagnostic.c`)

### 2.1. Coordinate Tracking & Source Spans
Every lexical token, AST node, and semantic symbol in Unfish is tagged with a precise `SourceSpan` (`src/common/uf_source.h`):

```c
typedef struct {
    const char* filename;
    uint32_t line;       /* 1-indexed */
    uint32_t column;     /* 1-indexed */
    uint32_t offset;     /* 0-indexed byte offset in source file */
} SourceLoc;

typedef struct {
    SourceLoc start;
    SourceLoc end;
} SourceSpan;
```

Because source spans preserve both the start and end character positions, diagnostics can highlight exact token ranges rather than ambiguous point coordinates.

### 2.2. ANSI Diagnostic Rendering Architecture
When a syntax, parsing, or semantic error occurs, `UfDiagnosticReporter` renders an informative, compiler-grade diagnostic box:

```
[Semantic Error] tests/conformance/err_undefined_var.unfish:4:11: Undefined variable 'counter'
   |
 3 | function increment():
 4 |     return counter + 1
   |            ^^^^^^^ Identifier 'counter' is not defined in this scope.
   |
   = Help: Did you mean to declare 'let counter = 0' before using it?
```

#### Diagnostic Rendering Features:
* **ANSI Color Highlighting**:
  * Errors are highlighted in bold red (`\033[1;31m`).
  * Warnings are highlighted in bold yellow (`\033[1;33m`).
  * Source gutters and line numbers are rendered in muted cyan (`\033[36m`).
  * Squiggles (`^^^^^`) underline the exact offending token span in bright red.
* **Multi-Span Context**: For errors involving relationships between two locations (e.g. duplicate variable declaration, trait parameter mismatch, or arity mismatch), the reporter prints both the site of the error and the original definition site.
* **Intelligent Suggestions ("Did you mean...?")**: For unrecognized identifiers or method names, the reporter runs a Levenshtein distance calculation over visible in-scope symbols to suggest likely typos.

---

## 3. Exit Code Convention

To ensure deterministic scripting, CI/CD automation, and differential testing, Unfish adheres to a strict standardized exit code contract:

| Exit Code | Classification | Trigger Scenarios |
|---|---|---|
| **`0`** | **Success** | Program ran to completion without errors; tests passed |
| **`1`** | **Lexical / Syntax Error** | Unterminated string, invalid token, unexpected indentation, parsing error |
| **`2`** | **Semantic / Type Error** | Undefined symbol, duplicate variable, arity mismatch, strict type failure, trait mismatch |
| **`3`** | **Runtime Exception** | Uncaught `raise`, division by zero, out of bounds index, cyclic JSON serialization |

All five execution backends (Interpreter, Stack VM, Register VM, Native C99, and WebAssembly) strictly reproduce these exact exit codes under identical failure conditions.

---

## 4. Runtime Exception Architecture

### 4.1. The `try` / `catch` / `finally` Control Flow
Unfish provides structured exception handling:

```unfish
try:
    let file = fs.read_file("config.json")
    let data = json.parse(file)
    say "Configuration loaded: " + str(data.version)
catch err:
    say "Failed to load configuration: " + err.message
    say "Error code: " + str(err.code)
finally:
    say "Execution cleanup complete."
```

#### Operational Semantics:
1. **Try Block**: The body of the `try` block executes. If no exception occurs, control flows directly to the `finally` block (if present).
2. **Catch Block**: If an exception occurs inside the `try` block, execution immediately aborts at the failure site. The active environment and call stack unwind to the enclosing `try` frame. The exception object is bound to the named catch identifier (`err`), and the `catch` block executes.
3. **Finally Block**: The `finally` block is guaranteed to execute regardless of whether an exception occurred, whether it was caught, or whether a `return` or `break` statement was executed inside `try` or `catch`.

### 4.2. User Exceptions: `raise` and `error()`
Developers can explicitly raise exceptions using the `raise` statement or the `error()` standard library function:

```unfish
function divide(a, b):
    if b == 0:
        raise error("Division by zero is undefined", "ERR_DIV_ZERO")
    return a / b
```

The `error(message, code)` function constructs an `Error` object containing:
* `message`: Human-readable error string.
* `code`: String error code constant (e.g. `"ERR_DIV_ZERO"`, `"ERR_FILE_NOT_FOUND"`).
* `stack`: Formatted call stack trace captured at the time of creation.

---

## 5. Unwinding Architecture Across Backends

The Unfish runtime engines handle stack unwinding differently according to their execution architectures:

```
┌────────────────────────────────────────────────────────────────────────┐
│                     STACK UNWINDING IMPLEMENTATIONS                    │
├────────────────────┬───────────────────────────────────────────────────┤
│ Execution Engine   │ Unwinding Mechanism                               │
├────────────────────┼───────────────────────────────────────────────────┤
│ 1. AST Interpreter │ Dual-stack setjmp/longjmp handler stack on runtime│
│ 2. Stack VM        │ OP_PUSH_TRY / OP_POP_TRY synchronized with setjmp │
│ 3. Register VM     │ ROP_PUSH_TRY / ROP_POP_TRY synchronized handlers   │
│ 4. Native C99 AOT  │ uf_catch_push / uf_catch_pop linked frame list    │
│ 5. WebAssembly     │ WASI longjmp / Emscripten exception bridge        │
└────────────────────┴───────────────────────────────────────────────────┘
```

### 5.1. Dual-Stack Handler Synchronization

Unfish programs can invoke native C functions (`UfNativeFn`) from bytecode, and bytecode functions can invoke other bytecode functions. When an error occurs—such as division by zero, array index out of bounds, or an explicit `error()` call inside a native builtin—the C runtime needs a reliable way to unwind back to the enclosing `try:` block without aborting the host process.

To achieve this with 100% deterministic safety, Unfish maintains two parallel, synchronized stacks:
1. **The VM Handler Stack (`vm->handlers`)**:
   Tracks the bytecode-level state for each active `try:` block:
   * `frame_index`: Call frame index where the `try:` block was entered.
   * `catch_ip`: Instruction pointer pointing directly to the catch block or finally block.
   * `stack_top`: Operand stack pointer to restore when catching an exception.
2. **The Runtime Handler Stack (`vm->rt->try_handlers`)**:
   Tracks the C execution context using `setjmp`/`longjmp`:
   * `jmp_buf jmp`: Environment buffer for `longjmp`.
   * `scope_env`: Active environment pointer for lexical scoping and GC root tracing.
   * `frame_count`: Call frame count on `rt`.
   * `temp_root_count`: Number of GC temporary roots active at entry.

Whenever `OP_PUSH_TRY` executes, both stacks push an entry. When `OP_POP_TRY` executes, both stacks pop an entry. When an error occurs:
* `uf_runtime_raise` or `uf_runtime_error` sets `rt->current_error` and executes `longjmp(th->jmp, 1)`.
* Control resumes inside `OP_PUSH_TRY` at `if (setjmp(th->jmp) != 0)`.
* Any open upvalues pointing to call frame slots that are being discarded are closed via `close_upvalues(vm, cur_h->stack_top)`.
* Call frames are popped down to `cur_h->frame_index`.
* The operand stack top is restored to `cur_h->stack_top`, the error object is pushed, and `ip` jumps directly to `cur_h->catch_ip`.

### 5.2. Edge Cases and Hardened Invariants

#### 5.2.1. Early Return from Inside `try:` or `catch:`
When a function executes `return <expr>` from within an active `try:` block:
```unfish
fn compute():
    try:
        return 42
    catch e:
        return -1
```
The compiler statically tracks the current `try_depth`. Before emitting `OP_RETURN` or `ROP_RETURN`, the compiler emits an `OP_POP_TRY` / `ROP_POP_TRY` for every active try block (`t < c->try_depth`). Furthermore, as a defensive runtime guarantee, `OP_RETURN` and `ROP_RETURN` verify that any remaining handlers belonging to the returning frame are purged:
```c
while (vm->handler_count > 0 && vm->handlers[vm->handler_count - 1].frame_index >= vm->frame_count - 1) {
    vm->handler_count--;
    if (vm->rt && vm->rt->try_handler_count > 0) vm->rt->try_handler_count--;
}
```
This guarantees that returning from a `try:` block never leaves dangling handlers on the stack that could erroneously intercept future errors in callers (verified in `tests/conformance/65_try_return.unfish`).

#### 5.2.2. Loop Control Flow: `break` and `continue` Inside `try:`
When `break` or `continue` is invoked from inside a `try:` block located within a loop:
```unfish
while i < 10:
    i = i + 1
    try:
        if i == 5:
            break
    catch e:
        say "caught"
```
The compiler records `loop.try_depth` at loop initialization. When emitting `UF_STMT_BREAK` or `UF_STMT_CONTINUE`, the compiler emits `OP_POP_TRY` for every try block between `c->current_loop->try_depth` and `c->try_depth`. This ensures:
1. Try handlers are cleanly popped before jumping to the break or continue target.
2. In the Register VM, registers allocated for try handlers are not leaked.
3. Subsequent statements after the loop execute with pristine exception stacks (verified in `tests/conformance/66_try_loop_control.unfish`).

#### 5.2.3. Guaranteed `finally:` Execution
Unfish guarantees that `finally:` blocks execute under all execution paths:
1. **Normal Flow**: When the `try:` block completes without errors, control falls into the `finally:` block.
2. **Handled Exception**: When an exception occurs and is caught by `catch:`, the `catch:` block completes and control flows into `finally:`.
3. **Unhandled Exception**: When a `try:` block has a `finally:` but no `catch:` (or when `catch:` re-raises), the exception is temporarily captured, the `finally:` block executes, and then the original exception is rethrown (`OP_RETHROW` / `ROP_RETHROW`) to the next enclosing handler or top-level reporter.
4. **Early Return**: Returning inside `try:` with an active `finally:` executes the `finally:` before the return value is handed back to the caller.

---

## 6. Comprehensive Error Code Catalog

The following table catalogs the core error diagnostics verified in the Unfish test suite:

### 6.1. Lexical & Syntax Errors (Exit Code 1)
| Error Identifier | Conformance Test | Trigger Condition |
|---|---|---|
| `ERR_UNTERMINATED_STRING`| `err_unterminated_string.unfish` | String literal reaches newline or EOF without closing quote |
| `ERR_REST_NOT_LAST` | `err_rest_not_last.unfish` | Rest parameter `...rest` is followed by additional parameters |
| `ERR_REST_DEFAULT` | `err_rest_default.unfish` | Rest parameter cannot declare a default value (`...rest = []`) |
| `ERR_INVALID_INDENT` | Conformance suite | Inconsistent indentation column not matching indentation stack |

### 6.2. Semantic & Type Errors (Exit Code 2)
| Error Identifier | Conformance Test | Trigger Condition |
|---|---|---|
| `ERR_UNDEFINED_VAR` | `err_undefined_var.unfish` | Variable identifier referenced before declaration |
| `ERR_DUPLICATE_DECL` | `err_duplicate_decl.unfish` | Variable declared multiple times within the same lexical block |
| `ERR_BREAK_OUTSIDE` | `err_break_outside_loop.unfish` | `break` statement used outside of `while`, `for`, or `repeat` |
| `ERR_CONTINUE_OUTSIDE`| `err_continue_outside_loop.unfish`| `continue` statement used outside of loop constructs |
| `ERR_RETURN_OUTSIDE` | `err_return_outside_fn.unfish` | `return` statement used in top-level module code |
| `ERR_ARITY_MISMATCH` | `err_arity_mismatch.unfish` | Function called with incorrect number of arguments |
| `ERR_STRUCT_ARITY` | `err_struct_arity.unfish` | Struct constructor called with mismatched field count |
| `ERR_STRUCT_NO_SELF` | `err_struct_method_no_self.unfish`| Struct method declared without `self` first parameter |
| `ERR_TRAIT_NOT_FOUND` | `err_trait_not_found.unfish` | `impl` block targets a trait name that does not exist |
| `ERR_TRAIT_PARAM` | `err_trait_param_mismatch.unfish` | Implemented method arity/types conflict with trait definition |
| `ERR_MISSING_METHOD` | `err_missing_trait_method.unfish`| Struct `impl` fails to implement all required trait methods |
| `ERR_GENERIC_BOUND` | `err_generic_bound_mismatch.unfish`| Type argument fails to satisfy declared trait bound |
| `ERR_AWAIT_OUTSIDE` | `err_await_outside_async.unfish` | `await` expression used outside of an `async function` |
| `ERR_TYPE_MISMATCH` | `err_type_mismatch_strict.unfish`| Incompatible type assignment under `--strict` mode |

### 6.3. Runtime Exceptions (Exit Code 3)
| Error Identifier | Conformance Test | Trigger Condition |
|---|---|---|
| `ERR_DIV_ZERO` | `err_div_zero.unfish` | Division or modulo by zero (`x / 0` or `x % 0`) |
| `ERR_INDEX_OUT_OF_BOUNDS`| `err_index_out_of_bounds.unfish` | Array indexing outside valid range `[0, len - 1]` |
| `ERR_MAP_INVALID_KEY` | `err_map_invalid_key_type.unfish`| Non-hashable type (e.g. array or function) used as map key |
| `ERR_MODULE_NOT_FOUND`| `err_module_not_found.unfish` | Target file or package cannot be resolved on import path |
| `ERR_CIRCULAR_IMPORT` | `err_circular_import.unfish` | Mutual circular module import detected |
| `ERR_IMPORT_SYMBOL` | `err_import_symbol_not_found.unfish`| Symbol requested in `from m import x` does not exist in module |
| `ERR_UNCAUGHT_ERROR` | `err_uncaught_error.unfish` | User exception raised with `raise` without enclosing `catch` |
| `ERR_DESTRUCTURE_TYPE`| `err_destructure_type.unfish` | Destructuring non-array as array or non-map as map |
| `ERR_ENUM_ARITY` | `err_enum_arity.unfish` | Enum variant constructed with mismatched payload arity |
| `ERR_RAISE_AFTER_CATCH`| `err_raise_after_catch.unfish`| Exception re-raised within catch block without outer handler |

---

## 7. Conformance Test Matrix for Exception Handling

The Unfish test harness includes automated tests dedicated to verifying every aspect of the exception model:

* **`23_try_catch.unfish`**: Basic `try`/`catch` with standard errors and local variable scoping.
* **`24_nested_try_catch.unfish`**: Nested `try`/`catch` blocks across multiple function frames.
* **`25_user_errors.unfish`**: Custom error generation with `error(msg, kind)` and property extraction.
* **`47_try_catch_finally.unfish`**: Comprehensive matrix of `finally:` combinations (normal, caught, rethrown).
* **`64_stdlib_expanded.unfish`**: Built-in exception safety across filesystem, string, and buffer operations.
* **`65_try_return.unfish`**: Early `return` statements inside `try:`, `catch:`, and nested blocks.
* **`66_try_loop_control.unfish`**: `break` and `continue` inside `try:` across `while`, `repeat`, `for`, and `for-in`.
* **`67_exception_diagnostics.unfish`**: Detailed inspection of `error.kind`, `error.message`, and multi-tier unwinding.

