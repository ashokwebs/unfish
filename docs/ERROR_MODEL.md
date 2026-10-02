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
│ 1. AST Interpreter │ Setjmp/longjmp handler stack on UfRuntime         │
│ 2. Stack VM        │ OP_PUSH_TRY / OP_POP_TRY exception frames         │
│ 3. Register VM     │ ROP_PUSH_TRY / ROP_POP_TRY exception frames       │
│ 4. Native C99 AOT  │ uf_try_push / uf_try_pop setjmp unwinding list    │
│ 5. WebAssembly     │ WASI longjmp / JS exception rethrow bridge        │
└────────────────────┴───────────────────────────────────────────────────┘
```

### 5.1. Stack VM Exception Unwinding (`src/vm/uf_vm.c`)
In the bytecode virtual machine, exception handling is implemented via dedicated opcodes:
* `OP_PUSH_TRY [u16 catch_offset]`: Pushes a `TryFrame` onto the VM's exception handler stack. The frame records:
  * Catch instruction address (`ip + catch_offset`).
  * Current call frame index (`frame_count`).
  * Current operand stack pointer (`stack_top`).
  * Current environment scope.
* `OP_POP_TRY`: Discards the topmost `TryFrame` upon normal completion of the `try` block.
* `OP_RETHROW`: Re-raises an exception that was not handled.

#### When a runtime error occurs in the VM:
1. The VM checks if `vm->try_count > 0`.
2. If a `TryFrame` exists:
   * Any open upvalues pointing to stack slots above the `TryFrame`'s saved stack pointer are closed into heap storage via `uf_vm_close_upvalues`.
   * The call frame stack is rewound to the saved frame depth.
   * The operand stack pointer is restored to the saved pointer.
   * The exception value is pushed onto the operand stack.
   * The instruction pointer `ip` jumps to `catch_offset`.
   * Execution resumes seamlessly inside the catch block.
3. If no `TryFrame` exists:
   * The VM prints an uncaught exception stack trace.
   * Execution terminates with exit code `3`.

### 5.2. Register VM Exception Unwinding (`src/vm2/uf_regvm.c`)
In the 256-register VM:
* `ROP_PUSH_TRY [reg_dest, catch_offset]`: Registers a try frame. If an exception triggers, the exception value is deposited directly into register `reg_dest`, and `PC` branches to `catch_offset`.
* `ROP_POP_TRY`: Pops the active handler.
* `ROP_RETHROW [reg_err]`: Re-raises the error stored in register `reg_err`.

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
