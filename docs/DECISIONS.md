# UNFISH — ARCHITECTURAL DECISION RECORDS (ADRs)

---

## ADR 001: Core Implementation in ANSI C99/C11
* **Date**: Phase 0
* **Status**: Accepted
* **Context**: We need an implementation language that is fast, portable, minimal in dependencies, and educational for students learning systems programming.
* **Decision**: Implement the core lexer, parser, runtime, and interpreter in clean, warning-free ANSI C99/C11 with zero external libraries.
* **Consequences**: Complete control over memory layout and runtime data structures; enables students to inspect language internals; requires rigorous automated sanitizer testing (ASan/UBSan) to prevent memory bugs.

## ADR 002: Indentation-Based Block Delimitation (Off-Side Rule)
* **Date**: Phase 0
* **Status**: Accepted
* **Context**: Block programming representations map naturally to hierarchical nested containers. In text, noisy curly braces `{}` add cognitive load for beginners.
* **Decision**: Use indentation (`INDENT`, `DEDENT`, `NEWLINE`) with colon `:` block introductions.
* **Consequences**: Syntax is clean and readable; visual blocks map directly to indented AST blocks; lexer handles indentation tracking using a dedicated indentation stack.

## ADR 003: Pratt Parser for Expressions
* **Date**: Phase 0
* **Status**: Accepted
* **Context**: Expression parsing with pure recursive descent leads to deeply nested grammar functions and difficulty extending operators.
* **Decision**: Implement Vaughan Pratt's top-down operator precedence parser for all expressions.
* **Consequences**: Highly compact, table-driven expression parsing; precise operator precedence and associativity; easily extensible.

## ADR 004: Chunk Arena Allocator for AST
* **Date**: Phase 0
* **Status**: Accepted
* **Context**: AST node creation with individual `malloc` calls causes fragmentation, overhead, and complex manual traversal during cleanup.
* **Decision**: Allocate all AST nodes, token strings, and parser structures in a contiguous chunk arena.
* **Consequences**: Extremely fast allocation; O(1) bulk deallocation at the end of compilation/execution; eliminates AST memory leaks by design.

## ADR 005: Tagged Union Runtime Values
* **Date**: Phase 0
* **Status**: Accepted
* **Context**: We need a simple, type-safe runtime value representation that bridges dynamically typed evaluation and future low-level systems inspection.
* **Decision**: Represent all values with a 16-byte tagged union (`UfValue`).
* **Consequences**: Primitives (null, bool, number) fit directly on the stack without heap allocation; strings, functions, and objects are tracked via pointers to heap headers.

## ADR 006: Object-Tracked Mark-and-Sweep Garbage Collection
* **Date**: Phase 2
* **Status**: Accepted
* **Context**: Manual reference counting between first-class functions (closures) and their lexical environments creates circular references (`closure_env <-> function_binding`), causing memory leaks upon runtime shutdown.
* **Decision**: Introduce a `UfObj` header (`src/runtime/uf_object.h`) on all heap objects (`UfStringObject`, `UfFunctionObject`, `UfEnv`) and manage them via an object registry in `UfRuntime`. Implement mark-and-sweep tracing rooted at active call frames and the global environment.
* **Consequences**: Perfectly resolves circular reference cycles; ensures deterministic, leak-free shutdown verified by AddressSanitizer; exposes a real garbage collection implementation for systems-level inspection.

## ADR 007: REPL Persistent Session Arena
* **Date**: Phase 2
* **Status**: Accepted
* **Context**: Resetting the compilation arena after each REPL line caused interned identifier strings and function bodies defined in prior lines to be overwritten by subsequent allocations.
* **Decision**: Maintain a persistent arena across the lifetime of the interactive REPL session, freeing all allocations only when the user exits.
* **Consequences**: Variables, functions, and closures remain valid and callable throughout the interactive session without memory corruption.

## ADR 008: Temporary Root Protection Stack & Active Block Scope Rooting
* **Date**: Engineering Review
* **Status**: Accepted
* **Context**: Under high GC pressure (e.g. loops allocating many objects with small thresholds), temporary values evaluated during complex expressions and local block scope environments (`UfEnv` created in `while`/`if`/`repeat` bodies) were not discovered by the garbage collector because they were neither in global scope nor registered in function activation frames. This led to heap-use-after-free when GC triggered during subexpression evaluation.
* **Decision**: 
  1. Add an evaluation operand root stack `rt->temp_roots` in `UfRuntime` with `uf_runtime_push_temp_root` and `uf_runtime_pop_temp_roots`.
  2. Track the currently active environment frame `rt->current_env` during statement/block execution, restoring it with RAII-like discipline across blocks and calls.
  3. Traverse `rt->current_env` and its parent chain during GC root marking.
* **Consequences**: Completely eliminated use-after-free under heavy GC pressure (verified by ASan in `test_stress`), guaranteeing safety for all arbitrary expression trees and nested block loops.

## ADR 009: First-Class Dynamic Arrays and In-Place Index Assignment
* **Date**: Milestone 2 (Phase 3)
* **Status**: Accepted
* **Context**: General-purpose algorithmic programming (sorting, data structures, lookup buffers) requires first-class sequential collections and subscript access.
* **Decision**: 
  1. Add `UF_TOK_LBRACKET` (`[`) and `UF_TOK_RBRACKET` (`]`) with paren-depth tracking in the lexer.
  2. Implement prefix `[` for array literals and infix `[` with `PREC_CALL` for subscripting in the Pratt parser.
  3. Introduce `UfArrayObject` (`UF_OBJ_ARRAY` / `UF_VAL_ARRAY`) containing dynamically resizable `elements` buffers tracked by GC.
  4. Support Python-style negative indexing (`arr[-1]` accesses the last element).
  5. Introduce `UF_STMT_INDEX_ASSIGN` allowing in-place assignment (`arr[i] = val`).
  6. Extend `len()`, `push()`, and `pop()` built-ins.
* **Consequences**: Enables implementing standard algorithms (e.g. `bubble_sort`, search) natively in Unfish, fully integrated with garbage collection and memory safety under ASan.

## ADR 010: Control Flow Primitives: break, continue, for-in Loops, and range()
* **Date**: Milestone 3
* **Status**: Accepted
* **Context**: Algorithmic code requires fine-grained loop control (early loop termination, skipping iterations) and high-level collection traversal without manual index bookkeeping.
* **Decision**:
  1. Add `break` and `continue` keywords and statement AST nodes (`UF_STMT_BREAK`, `UF_STMT_CONTINUE`).
  2. Enforce compile-time semantic validation: using `break` or `continue` outside of a loop produces a semantic error.
  3. Add `for <item> in <iterable>:` syntax (`UF_STMT_FOR`), supporting native traversal over arrays and UTF-8 strings.
  4. Implement `range([start,] end[, step])` built-in native function returning an array of numbers.
* **Consequences**: Unifies counting loops, range iterations, and collection traversals under a consistent, beginner-friendly yet expressive syntax.

## ADR 011: Language-Level Error Handling Architecture: Gradual Two-Tier Model
* **Date**: Milestone 3
* **Status**: Accepted
* **Context**: Unfish needs an error handling model suited for learners transitioning from beginner scripts to systems programming. Exceptions often cause hidden control-flow jumps, while strict Result types can introduce excessive syntactic overhead for introductory programming.
* **Decision**:
  1. **Tier 1 (Value-based failure)**: Standard library query functions return `null` or boolean indicators for expected conditions (e.g. missing map key, EOF), encouraging explicit checks.
  2. **Tier 2 (Structured catchable runtime errors)**: Exceptional situations (e.g. `IndexOutOfBounds`, `DivisionByZero`, `StackOverflowError`, `AssertionFailed`) throw structured runtime errors that can be captured via `try: ... catch err:` blocks.
  3. Custom user errors are raised via `panic(msg)` or `error(msg)`.
* **Consequences**: Novices write clean, sequential code without boilerplate; educational test harnesses can evaluate student code defensively without crashing the test runner; advanced learners learn stack unwinding.

## ADR 012: Module Resolution, Namespaces, and Singleton Lifecycle
* **Date**: Milestone 3 (Designed), Milestone 8 (Implemented)
* **Status**: Implemented
* **Context**: Unfish programs must scale beyond single files while avoiding namespace pollution and cyclic initialization deadlocks.
* **Decision**:
  1. **Syntax**: `import <module> [as <alias>]` binds module exports into a dedicated namespace object `<module>` (`UF_OBJ_MODULE` / `UF_VAL_MODULE`). Selective imports `from <module> import a [as a1], b [as b1]` copy exported symbols into the current scope.
  2. **Resolution Order**:
     - Built-in core modules (`math`, `strings`) with on-demand lazy initialization.
     - Relative path to importing file: `<caller_dir>/<module>.unfish` or `<caller_dir>/<module>`.
     - Working directory relative path: `./<module>.unfish` or `./<module>`.
     - Environment variable `UNFISH_PATH` search directories.
  3. **Lifecycle & State Machine**:
     - Modules are executed once as singletons and cached in `rt->module_cache`.
     - Transitions through states: `UNLOADED` -> `LOADING` -> `LOADED` (or `ERROR`).
     - Circular dependency detection: encountering a module in `LOADING` state during import raises `CircularImportError` with source line traces and zero memory leaks.
  4. **Memory Model & Isolation**:
     - Each file module owns its own `UfArena` and `UfInterner`, guaranteeing that function AST bodies and string literals persist safely for the module's lifetime.
     - Modules execute in an isolated environment (`UfEnv`) whose parent is `rt->global_env`.
     - Non-internal top-level bindings in the module's environment populate its `exports` map for dot access (`mod.func()`).
  5. **Garbage Collection Integration**:
     - `UF_OBJ_MODULE` is tracked by mark-and-sweep GC. All entries in `rt->module_cache` are rooted during GC marking. Module cleanup during GC sweep cleanly frees arena, interner, source text, path, and name.
* **Consequences**: Clean separation of namespaces, deterministic single-evaluation semantics, safe circular import traps, and zero global scope pollution.

## ADR 013: Bytecode Virtual Machine Pipeline & Intermediate Representation (IR) Contract
* **Date**: Milestone 3
* **Status**: Accepted
* **Context**: The tree-walking interpreter serves as the semantic reference. To achieve high performance, serialization, and compilation to web/native, Unfish requires a bytecode VM pipeline (`AST -> IR -> Bytecode -> VM`).
* **Decision**:
  1. Maintain identical semantic contracts between the tree-walking interpreter and the future VM; both must pass the exact same conformance suite.
  2. Define a stack-based instruction set architecture with 1-byte opcodes and 2-byte operand indices.
  3. Opcodes are partitioned into: Literal loading, Local/Global variable access, Closure/Upvalue capture, Binary/Unary operations, Conditional/Unconditional jumps, Collection construction/indexing, and Function call/return.
  4. Bytecode chunks bundle bytecode arrays, constant pools (`UfValue`), and debug line-mapping tables.
* **Consequences**: Establishes a concrete contract for Phase 7 VM implementation without breaking the current AST interpreter.

## ADR 014: Hash Map Data Structure, Dot Property Access, and Iteration
* **Date**: Milestone 4 (Phase 3 Part 2)
* **Status**: Accepted
* **Context**: Algorithmic and general-purpose programming requires first-class associative key-value dictionaries. Additionally, ergonomic record manipulation benefits from property-style dot syntax (`user.name`).
* **Decision**:
  1. **Data Structure**: Implement `UfMapObject` (`UF_OBJ_MAP` / `UF_VAL_MAP`) as an open-addressing hash table with linear probing and power-of-two capacities, resizing at 75% load factor.
  2. **Hashing**: Use the 32-bit FNV-1a hash algorithm for strings and bitwise IEEE 754 float hashing for numbers.
  3. **Order Preservation**: Maintain a parallel `order_keys` array in `UfMapObject` to guarantee deterministic insertion-order iteration for `keys()`, `values()`, and `for-in` traversal.
  4. **Syntax**: Support `{key: value}` literals with both string expressions and identifier keys (`{name: "Alice"}`).
  5. **Dot Property Sugar**: In the Pratt parser, desugar `target.field` into `target["field"]` (`UF_EXPR_INDEX`) and `target.field = val` into `target["field"] = val` (`UF_STMT_INDEX_ASSIGN`).
  6. **Error Semantics**: Adhere to Tier 1 of the Error Model (ADR 011) — querying a nonexistent key returns `null` rather than throwing an unhandled runtime error. Provide `has_key(map, key)` for explicit containment checks and `delete(map, key)` for deletion.
  7. **Garbage Collection**: Integrate `UF_OBJ_MAP` directly into mark-and-sweep GC by traversing both `entries` (keys and values) and `order_keys`.
* **Consequences**: Unifies object-like records and dictionaries under a single high-performance, memory-safe data structure with zero external dependencies, fully verified under ASan/UBSan.

## ADR 015: Higher-Order Functions, Anonymous Lambdas, and Runtime Invocation Protocol
* **Date**: Milestone 5 (Phase 3 Part 3)
* **Status**: Accepted
* **Context**: Functional programming idioms (`map`, `filter`, `reduce`, `sort`) require first-class functions that can be constructed inline as anonymous lambdas and passed to native standard library procedures.
* **Decision**:
  1. **Runtime Invocability**: Export a decoupled callback interface `uf_runtime_call` via `UfCallValueFn call_fn` in `UfRuntime`, enabling C native functions to invoke user-defined closures and native functions uniformly.
  2. **Core Functional Built-ins**: Implement 8 native functions operating on arrays:
     - `map(arr, fn)`: transforms every element.
     - `filter(arr, fn)`: retains truthy predicate matches.
     - `reduce(arr, fn, [init])`: left fold with optional accumulator initialization.
     - `sort(arr, [cmp])`: stable sorting with default or custom binary comparator.
     - `reverse(arr)`: reversed array shallow copy.
     - `find(arr, fn)`: first predicate match or `null`.
     - `every(arr, fn)`: universal quantifier predicate check.
     - `some(arr, fn)`: existential quantifier predicate check.
  3. **Anonymous Function Syntax**: Support `function([params]): body` in expression positions.
     - Support inline expression bodies with implicit return: `function(x): x * 2`.
     - Support inline statements with explicit return: `function(x): return x * 2`.
     - Support full multiline indented blocks: `function(x):\n    let y = x + 1\n    return y`.
  4. **Self-Recursive Named Function Expressions**: Named function expressions (`let fact = function factorial(n): ...`) bind the function name inside its own call scope, permitting self-recursion without polluting the outer namespace.
* **Consequences**: Enables clean, modern data pipeline construction and functional algorithms with zero external dependencies, 100% verified under ASan/UBSan.

## ADR 016: String and Math Standard Libraries Architecture
* **Date**: Milestone 6 (Phase 3 Part 4)
* **Status**: Accepted
* **Context**: Practical programming, algorithmic problem solving, and educational computational curricula require comprehensive string processing and mathematical functions and constants.
* **Decision**:
  1. **Modular Architecture**: House the standard library extensions in a dedicated compilation unit (`src/runtime/uf_stdlib.c` and `uf_stdlib.h`), exposing `uf_stdlib_register_runtime` and `uf_stdlib_register_semantic`.
  2. **15 String Functions**:
     - `split(str, delim)`: array of token substrings (handles empty delimiter as char splitter).
     - `join(arr, sep)`: joined string with delimiter.
     - `trim(str)`: whitespace-stripped string.
     - `replace(str, old, new)`: substring substitution.
     - `to_upper(str)` and `to_lower(str)`: ASCII case conversion.
     - `contains(str, sub)`, `starts_with(str, pfx)`, `ends_with(str, sfx)`: boolean matchers.
     - `char_at(str, idx)`: character at index with negative indexing support.
     - `to_number(str)`: string-to-number parser (returns `null` on invalid).
     - `to_string(val)`: universal string formatter for any runtime value.
     - `repeat_string(str, n)`: string repetition with size safeguards.
     - `substring(str, start, [end])`: substring extraction with clamping and negative indexing.
     - `index_of(str, sub)`: search index or -1.
  3. **Math Functions & Constants**:
     - Functions: `abs`, `floor`, `ceil`, `round`, `sqrt`, `pow`, `min`, `max`, `log`, `sin`, `cos`, `tan`, `random`, `random_int`.
     - Constants: `PI` (3.14159...), `E` (2.71828...), `INFINITY` (IEEE 754 positive infinity).
  4. **Error Handling**: Tier 1 errors return `null` (e.g. `to_number("invalid") -> null`, `char_at` out-of-bounds -> `""`); math domain violations (e.g. `sqrt(-1)`, `log(0)`) emit runtime diagnostic errors.
* **Consequences**: Provides complete string and math capabilities out of the box with zero third-party dependencies, adhering to C99 standards and tested under memory sanitizers.

## ADR 017: Structured Exception Recovery with try/catch and Stack Unwinding
* **Date**: Milestone 7 (Phase 3 Part 5)
* **Status**: Accepted
* **Context**: ADR 011 establishes a tiered error recovery strategy. Real programs and robust scripts need the ability to catch runtime anomalies (division by zero, index out of bounds, invalid JSON/CSV data) and raise application-level exceptions (`error(msg, [kind])`) without crashing the entire runtime.
* **Decision**:
  1. **Syntax**: Block-structured `try: <block> catch <ident>: <block>`. Catch variable is scoped exclusively to the catch block.
  2. **First-Class Error Object**: `UF_OBJ_ERROR` / `UF_VAL_ERROR` (`UfErrorObject`) storing `message` (`UfStringObject*`), `kind` (`UfStringObject*`), `line`, and source `file`.
  3. **Object Properties**: Subscript and dot access desugaring (`err.message`, `err.kind`, `err.line`, `err.file`) allows inspectable exception metadata in user code.
  4. **Stack Unwinding via `setjmp`/`longjmp`**:
     - `UfRuntime` maintains a handler stack `UfTryHandler try_handlers[UF_MAX_TRY_HANDLERS]`.
     - Each handler saves `jmp_buf`, calling `env`, `frame_count`, and `temp_root_count`.
     - When `uf_runtime_raise` or `uf_runtime_error` triggers inside an active try block, runtime state (`frame_count`, `temp_root_count`, `current_env`) is restored, and `longjmp` transfers control to the catch block handler.
     - Because all heap values are tracked on `rt->all_objects` in the mark-and-sweep GC and program structures in arenas, stack unwinding does NOT leak memory or leave orphaned heap blocks.
  5. **Native `error(message, [kind])`**: Standard library built-in enabling user scripts to raise custom exceptions with arbitrary error kinds (defaulting to `"UserError"`).
* **Consequences**: Complete, structured, and leak-free exception handling across nested function calls, verified under AddressSanitizer and UndefinedBehaviorSanitizer.

## ADR 019: Standard Library Modularization and Sandboxed Core Modules
* **Date**: Milestone 9 (Phase 4 Part 2)
* **Status**: Accepted
* **Context**: Following the introduction of the Module System in ADR 012, standard library capabilities must be partitioned into importable modules (`sys`, `fs`, `random`, `time`, `json`, `testing`) rather than continuing to pollute the global namespace.
* **Decision**:
  1. **Partitioning**:
     - `sys`: Process termination (`sys.exit`), CLI argument introspection (`sys.args`), host platform identification (`sys.platform`), and environment variables (`sys.env`).
     - `fs`: Sandboxed filesystem operations with tier 1 error reporting (`fs.read_text`, `fs.write_text`, `fs.exists`, `fs.delete_file`).
     - `random`: Pseudo-random number generation (`random.random`, `random.random_int`), sequence sampling (`random.choice`), and Fisher-Yates array permutation (`random.shuffle`).
     - `time`: High-resolution monotonic timer (`time.clock`), process pause (`time.sleep`), and UNIX timestamp (`time.timestamp`).
     - `json`: Native recursive-descent parser (`json.parse`) translating valid JSON directly into Unfish primitive and composite values, and complete serializer (`json.stringify`) with character escaping.
     - `testing`: Written in Unfish (`src/stdlib/testing.unfish`), providing assertions (`assert_equal`, `assert_true`, `assert_throws`) and an isolated test runner (`run_tests`).
  2. **Lazy Built-in Resolution**:
     - Built-in modules (`math`, `strings`, `sys`, `fs`, `random`, `time`, `json`) are instantiated lazily on their first `import` or `from ... import`, keeping baseline runtime allocation minimal.
     - Filesystem module resolution automatically checks `src/stdlib/<name>.unfish` as a fallback, allowing standard library modules written in Unfish to be imported cleanly without explicit path prefixes.
* **Consequences**: Programs can import only the capabilities they require, keeping educational sandboxes lean and preventing unexpected system interactions.

## ADR 020: Optional Type Annotations and Gradual Type Checking
* **Date**: Milestone 10 (Phase 4 Part 3)
* **Status**: Accepted
* **Context**: Educational programming progression requires moving from untyped, exploratory prototyping to explicit interface contracts and type discipline without breaking backwards compatibility or runtime performance.
* **Decision**:
  1. **Syntax**:
     - Variable declarations: `let <name>: <Type> [= <init>]`
     - Function parameters: `function <name>(<param>: <Type>, ...): <ReturnType>:`
     - Anonymous lambda expressions: `function(<param>: <Type>, ...): <ReturnType>: <expr>`
  2. **Core Types**: `Number`, `String`, `Boolean`, `Array`, `Map`, `Function`, `Null`, `Any`, `Error`. Unannotated symbols default to `Any`.
  3. **Static Gradual Analysis**:
     - Expression type inference for literals, variables, arithmetic, comparisons, logic, and calls.
     - Type compatibility is checked during `uf_analyze_program` and `uf_typecheck_program`.
     - In standard mode, type mismatches produce informative yellow compiler warnings (`UF_DIAG_WARNING`).
     - In `--strict` mode, type mismatches are promoted to semantic compilation errors (`UF_DIAG_SEMANTIC_ERROR`), halting execution.
  4. **Zero Runtime Overhead**: Type annotations are erased after semantic analysis, preserving full dynamic execution speed and memory compactness in `UfValue`.
* **Consequences**: Enables progressive pedagogical adoption of static contracts while maintaining 100% test compatibility and memory safety under AddressSanitizer/UBSan.

## ADR 021: User-Defined Structs and Records
* **Date**: Milestone 11 (Phase 4 Part 4)
* **Status**: Accepted
* **Context**: Programs need custom structured records with named fields rather than generic dictionaries or untyped tuples, enabling data modeling, strong encapsulation, and type contracts.
* **Decision**:
  1. **Declaration Syntax**:
     ```unfish
     struct <Name>:
         <field1>: <Type>
         <field2>: <Type>
     ```
     Fields may optionally omit type annotations (defaulting to `Any`).
  2. **First-Class Constructors**:
     The struct name `<Name>` is bound as a callable constructor taking exactly `field_count` positional arguments in declaration order.
  3. **Dot Member Access & Mutation**:
     Field read (`inst.field`) and field write (`inst.field = val`) desugar through the parser into index read/write expressions (`inst["field"]`). Runtime checks verify field presence and fail with descriptive errors if unknown fields are accessed.
  4. **Value & GC Representation**:
     - `UfStructDefObject`: Holds struct name, field names, and field type descriptors.
     - `UfInstanceObject`: GC-managed object referencing its `UfStructDefObject` and a dynamically allocated array of `UfValue fields`.
     - GC sweep frees both the instance shell and its underlying value array, preventing memory leaks.
  5. **Pedagogical Output & Equality**:
     - `uf_val_to_string` formats instances as `Point(x: 10, y: 20)`.
     - `uf_val_equal` provides recursive structural equality across matching struct definitions.
* **Consequences**: Unfish developers can declare clean domain models with full static and runtime validation, completely memory safe under AddressSanitizer.

## ADR 022: Pattern Matching with Guards and Destructuring
* **Date**: Milestone 12 (Phase 4 Part 5)
* **Status**: Accepted
* **Context**: Complex condition branching with nested `if/else` checks, type inspection, and record property extractions creates cluttered, error-prone code. A dedicated pattern matching construct enables concise, expressive, and type-safe multi-way dispatch.
* **Decision**:
  1. **Syntax**:
     ```unfish
     match <expression>:
         when <pattern> [if <guard>]:
             <statement_block>
         else:
             <statement_block>
     ```
  2. **Pattern Kinds**:
     - **Literals**: Numbers (positive and negative), strings, booleans, and `null` matching via `uf_val_equal`.
     - **Wildcard**: `_` matching any value without binding.
     - **Variable Binding**: Identifiers (e.g. `n`, `x`) binding the matched value within the arm's lexical scope.
     - **Struct Destructuring**: `StructName(p1, p2, ...)` verifying instance type and recursively matching field patterns against instance properties.
  3. **Guards**:
     Optional `if <condition>` expressions evaluated in the arm's binding environment; if false, matching continues to the next arm.
  4. **Control Flow**:
     Arms evaluate in lexical order. Returns, breaks, and continues within an arm cleanly propagate to enclosing functions and loops.
* **Consequences**: Provides pedagogical clarity for algorithms, tree traversals, and state machines while maintaining zero overhead when unused.

## ADR 023: Canonical Source Code Formatter (`unfish format`)
* **Date**: Milestone 13 (Phase 4 Part 6)
* **Status**: Accepted
* **Context**: Consistent code style across codebases, teaching materials, and repositories eliminates stylistic bike-shedding and improves readability. A deterministic, idempotency-guaranteed formatter ensures canonical presentation.
* **Decision**:
  1. **Canonical Formatting Rules**:
     - Indentation: Exactly 4 spaces per nesting level.
     - Spacing: Binary operators spaced (`a + b * c`), commas followed by 1 space, colons followed by 1 space in annotations and block headers.
     - Newlines: Maximum 1 blank line between top-level declarations (functions, structs). No trailing spaces.
     - Minimal Parentheses: Precedence-aware expression emission only inserts parentheses when sub-expression precedence is lower than or equal to surrounding operators.
     - Property Access: Desugars string index operations matching identifier rules to dot syntax (`inst.field`).
  2. **CLI Interface**:
     - `unfish format <file>`: Writes formatted source to standard output.
     - `unfish format -i <file>` / `--in-place`: Rewrites source file in-place if changes were necessary.
     - `unfish format --check <file>`: Verification mode for CI; exits with 0 if already canonical, 1 otherwise.
  3. **Idempotency Guarantee**:
     Formally verified: `format(format(x)) == format(x)` across all grammar productions and test suites.
* **Consequences**: Standardized tooling ecosystem with zero external dependencies.

## ADR 024: Debugger Hook Architecture & Execution Visualization
* **Date**: Milestone 14 (Phase 4 Part 7)
* **Status**: Accepted
* **Context**: Educational inspection, step-through debugging, and visual execution traces require runtime hooks without degrading performance during normal execution.
* **Decision**:
  1. **Zero-Overhead Hook Model**:
     `UfRuntime` stores an optional `debug_hook` callback function pointer and `debug_user_ctx`. When `debug_hook == NULL`, standard execution proceeds at full speed without extra allocations.
  2. **Event Model**:
     - `UF_DEBUG_EVENT_STEP`: Emitted before executing every non-block statement.
     - `UF_DEBUG_EVENT_CALL_ENTER` / `CALL_EXIT`: Emitted on function invocation and return with argument and return value inspection.
     - `UF_DEBUG_EVENT_VAR_BIND` / `VAR_ASSIGN`: Emitted when variables are created (`let`) or mutated (`=`).
     - `UF_DEBUG_EVENT_GC_START` / `GC_END`: Emitted during garbage collection with memory metrics.
  3. **Visual Execution Tracer (`unfish trace <file>`)**:
     Streams newline-delimited JSON events to stdout for consumption by visualizers, IDEs, or automated verification tools.
  4. **Interactive CLI Debugger (`unfish debug <file>`)**:
     Provides an interactive REPL (`ufdb`) with breakpoints (`b <line>`), single-stepping (`s`), step-over (`n`), continue (`c`), backtraces (`stack`), and scope inspection (`p <var>`, `env`, `heap`).
* **Consequences**: First-class developer tooling embedded directly into the core runtime without third-party dependencies.

## ADR 025: Visual Block <-> AST <-> Text Round-Tripping (`blocks-export` / `blocks-import`)
* **Date**: Milestone 15 (Phase 4 Part 8)
* **Status**: Accepted
* **Context**: Educational programming environments (such as Blockly, Scratch, and visual block IDEs) require bidirectional translation between graphical node representations and textual source code without syntactic or semantic information loss.
* **Decision**:
  1. **Canonical JSON Block Schema (`unfish_blocks_v1`)**:
     A declarative JSON schema representing 100% of Unfish AST nodes:
     - Statements: `let`, `assign`, `index_assign`, `say`, `expr`, `if`, `while`, `repeat`, `for_in`, `break`, `continue`, `function`, `return`, `block`, `try_catch`, `import`, `from_import`, `struct`, `match`.
     - Expressions: `literal_null`, `literal_bool`, `literal_number`, `literal_string`, `identifier`, `unary`, `binary`, `grouping`, `call`, `array`, `index`, `map`, `function`.
     - Match Patterns: `pattern_literal`, `pattern_variable`, `pattern_wildcard`, `pattern_struct`.
  2. **Bi-Directional Tooling**:
     - `unfish blocks-export <file.unfish>`: Parses Unfish textual source into AST and emits canonical JSON blocks.
     - `unfish blocks-import <file.json>`: Deserializes JSON blocks directly into typed AST nodes and canonicalizes back to Unfish source code via `uf_format_program`.
  3. **Lossless Round-Trip Guarantee**:
     Formally verified: `Text -> AST -> JSON -> AST -> Text == original` across the entire language conformance test suite including structs, pattern matching, closures, and collections.
* **Consequences**: Enables seamless visual block IDE integration, visual code editors, and automated code transformations with zero external dependencies.

## ADR 026: Bytecode Instruction Set & Chunk Format
* **Date**: Milestone 16 (Phase 5 Part 1)
* **Status**: Accepted
* **Context**: Transitioning from tree-walk AST interpretation to a high-performance stack-based virtual machine requires a formal opcode specification, instruction encoding format, constant pool management, and disassembler.
* **Decision**:
  1. **Opcode Architecture (36 instructions)**:
     - Literals & Constants: `OP_CONSTANT` (16-bit operand), `OP_NULL`, `OP_TRUE`, `OP_FALSE`.
     - Stack Ops: `OP_POP`, `OP_DUP`.
     - Variables: `OP_LOAD_LOCAL`, `OP_STORE_LOCAL` (16-bit slot), `OP_LOAD_GLOBAL`, `OP_STORE_GLOBAL`, `OP_DEFINE_GLOBAL` (16-bit constant string name).
     - Closures: `OP_GET_UPVALUE`, `OP_SET_UPVALUE` (8-bit index), `OP_CLOSURE`, `OP_CLOSE_UPVALUE`.
     - Arithmetic & Logic: `OP_ADD`, `OP_SUB`, `OP_MUL`, `OP_DIV`, `OP_MOD`, `OP_NEG`, `OP_NOT`.
     - Comparisons: `OP_EQ`, `OP_NEQ`, `OP_LT`, `OP_LTE`, `OP_GT`, `OP_GTE`.
     - Control Flow: `OP_JUMP` (16-bit forward), `OP_JUMP_IF_FALSE` (16-bit forward), `OP_LOOP` (16-bit backward).
     - Subroutines: `OP_CALL` (8-bit arity), `OP_RETURN`.
     - Collections: `OP_BUILD_ARRAY` (16-bit count), `OP_BUILD_MAP` (16-bit count), `OP_INDEX_GET`, `OP_INDEX_SET`.
     - Built-in & Records: `OP_SAY`, `OP_STRUCT_DEF`, `OP_INSTANCE`.
  2. **Chunk Representation (`UfChunk`)**:
     Dynamic flat byte array for instructions (`code`), dynamic array of `UfValue` for constants (`constants`), and parallel source line mappings (`lines`).
  3. **Disassembler**:
     Formatted bytecode dumper (`uf_chunk_disassemble`) mapping offsets, line breaks, opcode mnemonics, operands, jump targets, and constant pool entries.
* **Consequences**: Provides the foundational intermediate representation for the bytecode compiler (Section S) and virtual machine (Section T).

## ADR 027: AST-to-Bytecode Compiler Architecture
* **Date**: Milestone 17 (Phase 5 Part 2)
* **Status**: Accepted
* **Context**: Executing Unfish programs at high performance requires compiling the high-level AST into flat, linear bytecode instructions with lexical scope tracking, local variable slot allocation, upvalue capture resolution for closures, and jump backpatching.
* **Decision**:
  1. **Lexical Scope & Local Resolution**:
     - Scopes track variable depth. Local variables in nested blocks are bound to contiguous 16-bit stack slots (`OP_LOAD_LOCAL`, `OP_STORE_LOCAL`).
     - Exiting a lexical block emits `OP_POP` or `OP_CLOSE_UPVALUE` for any captured variables.
  2. **Upvalue Capture Model**:
     - Closures capture outer local variables or enclosing upvalues through two-pass resolution.
     - `OP_CLOSURE` instruction encodes the prototype function constant followed by `(is_local, index)` descriptor pairs, enabling runtime closure allocation with shared upvalue cells.
  3. **Control Flow & Backpatching**:
     - `if`, `while`, `repeat`, `for_in`, `break`, and `continue` statements compile into conditional and unconditional forward jumps (`OP_JUMP_IF_FALSE`, `OP_JUMP`) and backward loops (`OP_LOOP`).
     - Jump offsets are backpatched after sub-statement emission with 16-bit displacement bounds checking.
  4. **CLI Integration (`unfish compile <file>`)**:
     - Full front-end pipeline (parsing, diagnostics, semantic analysis) followed by bytecode compilation and recursive disassembly of top-level code and nested functions.
* **Consequences**: Unlocks the stack-based virtual machine execution pipeline (Section T).









## ADR 028: Stack-Based Bytecode Virtual Machine Architecture
* **Date**: Milestone 18 (Phase 5 Part 3)
* **Status**: Accepted
* **Context**: High-performance execution requires a stack-based virtual machine executing the 39 bytecode opcodes produced by the Unfish compiler, with complete semantic parity against the AST tree-walk interpreter, proper upvalue cell capture/closing lifecycles, exception handling with stack unwinding, and bi-directional interoperability between AST interpreter and VM.
* **Decision**:
  1. **Stack & Call Frame Architecture (`UfVM`)**:
     - Evaluation stack with up to 4096 value slots and 256 call frames (`UfVMFrame`).
     - Fixed-size internal footprint (~105 KB) allowing zero dynamic allocations per VM instance and clean unwind under longjmp without heap leaks.
     - Call frame slots point to contiguous stack base pointers, facilitating fast relative local access (`OP_LOAD_LOCAL`, `OP_STORE_LOCAL`).
  2. **Upvalue Management**:
     - Implemented open upvalue linked list sorted by stack address.
     - Multiple closures referencing the same stack variable share a single `UfUpvalueCell`.
     - `close_upvalues` moves the stack value into the heap-allocated cell when a local variable exits scope or when a frame returns.
     - Upvalues and closures are fully integrated into mark-sweep GC roots and traversal.
  3. **Bi-directional Engine Interoperability**:
     - AST interpreter functions called from the VM execute via `uf_runtime_call`.
     - Bytecode closures passed to tree-walk functions (e.g. `run_tests` in stdlib) execute on the thread's active `UfVM` via `uf_vm_run_closure` with zero heap allocation.
  4. **Exception Handling & Unwinding (`OP_PUSH_TRY` / `OP_POP_TRY`)**:
     - `OP_PUSH_TRY` registers an exception handler with stack depth, frame index, and catch IP.
     - Runtime errors trigger stack and frame unwinding, closing any open upvalues above the handler stack top, pushing the caught error, and jumping to the catch block.
  5. **100% Differential Conformance Verification**:
     - Integrated `tools/run_differential_tests.sh` testing all 51 conformance tests through both AST interpreter and VM.
     - Verified 100% identical stdout, stderr, and exit codes across the entire suite under ASan and UBSan.
* **Consequences**: Enables native bytecode execution with the `--vm` flag (`unfish run --vm <file>`, `unfish --vm <file>`), achieving complete behavioral equivalence with the AST interpreter.

## ADR 029: Bytecode Disassembler & VM Debugger Architecture
* **Date**: Milestone 19 (Phase 5 Part 4)
* **Status**: Accepted
* **Context**: Inspecting bytecode generation, diagnosing compiler/VM bugs, and profiling execution dynamics requires a full recursive bytecode disassembler and VM runtime instruction tracer.
* **Decision**:
  1. **Recursive Function Disassembler (`uf_disasm_function_tree`)**:
     - Disassembles bytecode chunks with formatted headers showing arity, upvalue counts, and byte sizes.
     - Formats offsets, line numbers (with `|` repetitions), opcode mnemonics, constants with decoded values, jump targets with absolute destinations, and `OP_CLOSURE` upvalue capture descriptors.
     - Recursively traverses constant pools to disassemble all enclosed child function prototypes.
  2. **CLI Commands (`unfish disasm <file>`, `unfish dis <file>`)**:
     - Added dedicated disassembly command and shorthand alias to the CLI.
  3. **VM Instruction Tracing & Execution Profiler (`--debug`)**:
     - Added `--debug` execution flag (`unfish run --vm --debug <file>`, `unfish --vm --debug <file>`).
     - Emits real-time visual stack dumps before each instruction execution (`[ 10 ][ 20 ] -> OP_ADD`).
     - Tracks VM execution metrics: total instruction count, peak evaluation stack depth, and peak call frame depth.
* **Consequences**: Provides powerful tooling for compiler development, educational inspection of bytecode internals, and VM performance optimization.

## ADR 030: Native C99 Code Generation & Binary Compilation Pipeline
* **Date**: Milestone 20 (Phase 5 Part 5)
* **Status**: Accepted
* **Context**: For maximal execution performance, portability, and deployment independence, Unfish programs should be able to compile directly into native standalone C99 source code and machine binaries without requiring the Unfish interpreter binary or runtime at execution time.
* **Decision**:
  1. **Single-Header Native C99 Runtime (`unfish_runtime.h`)**:
     - Standalone, portable C99 runtime providing tagged `UfVal` types, arithmetic/logical operators, variadic collections (`uf_make_array`, `uf_make_map`), built-in I/O (`uf_say`, `uf_print`), and auto-cleanup tracking.
  2. **AST-to-C99 Transpiler (`uf_emit_c.c`)**:
     - Translates AST statements, expressions, loops, conditionals, functions, and arrays into readable, standard C99.
     - Top-level variables are safely scoped with `uf_var_` prefixes to prevent C keyword collisions.
     - Top-level functions are forward-declared and emitted before `main()`.
  3. **Native Compiler CLI Commands (`unfish emit-c` & `unfish build`)**:
     - `unfish emit-c [-o <out.c>] <file.unfish>`: Transpiles Unfish code to readable standalone C99.
     - `unfish build [-o <binary>] <file.unfish>`: Compiles Unfish code directly to a native executable ELF binary using `gcc -O2`.
  4. **Verification**:
     - Added comprehensive unit test suite `tests/unit/test_emit_c.c` testing emission, gcc compilation, and native execution.
* **Consequences**: Enables Unfish to target native executable binaries, delivering maximum performance and standalone distribution capabilities.
