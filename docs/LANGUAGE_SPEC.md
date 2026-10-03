# UNFISH — FORMAL LANGUAGE SPECIFICATION & REFERENCE MANUAL
## Volume I: Lexical Grammar, Syntactic Productions & Operational Semantics
### Version 2.1.0 (Core Standard)

---

## 1. Introduction & Theoretical Foundations

This document provides the definitive, formal language specification for **Unfish**. It is designed as a rigorous reference for compiler engineers, language implementors, programming language theorists, educators, and systems programmers.

### 1.1. Core Design Philosophy
Unfish combines the ergonomic elegance of clean indentation syntax with the rigor of modern programming language theory:
* **Off-side Lexing**: Structural indentation eliminates braces (`{}`) and semicolons (`;`) without syntactic ambiguities.
* **Pratt Operator-Precedence Parsing**: Expressions are parsed using Vaughan Pratt's top-down operator precedence algorithm, ensuring unambiguous precedence resolution across 13 distinct binding tiers.
* **Dual Function Paradigm**: First-class functions can be declared using either block-based `function name(args):` syntax or concise lambda `fn(args): expr` expressions.
* **Structured Exception Safety**: Unwinding semantics guarantee deterministic stack and frame cleanup across nested `try`-`catch`-`finally` blocks, early returns, and loop jumps.
* **Gradual Type System**: Optional type annotations enable a smooth, 4-tier spectrum from dynamic duck typing to strict static verification.

### 1.2. Grammar Notation (EBNF)
Grammar productions in this specification adhere to ISO/IEC 14977 Extended Backus-Naur Form (EBNF):
* `Rule = Production ;` defines a non-terminal rule.
* `[ Item ]` denotes an optional element (zero or one occurrence).
* `{ Item }` denotes repetition (zero or more occurrences).
* `Item1 | Item2` denotes alternation (either Item1 or Item2).
* `'literal'` denotes a terminal lexical token.
* `( Item1 Item2 )` groups items for grouping and precedence.

---

## 2. Lexical Structure & Tokenization

### 2.1. Character Encoding & Normalization
* **Source Encoding**: Unfish source files must be valid UTF-8 sequences.
* **Byte Order Mark (BOM)**: An optional leading UTF-8 BOM (`0xEF, 0xBB, 0xBF`) is recognized and discarded by the scanner prior to tokenization.
* **Line Terminators**: Unfish normalizes both Unix line feeds (`\n`, ASCII 10) and Windows carriage-return line-feeds (`\r\n`, ASCII 13, 10) into a single logical `NEWLINE` token.
* **Ignored Content**: Blank lines and lines consisting entirely of whitespace and comments do not alter the indentation state and produce no tokens.

### 2.2. The Indentation Stack Algorithm
The Unfish lexer (`src/lexer/uf_lexer.c`) maintains an internal indentation stack:
$$\text{Stack} = [S_0, S_1, \dots, S_k], \quad S_0 = 0$$

For each non-empty, non-comment line:
1. The scanner counts the column offset $C$ of the first non-whitespace character (spaces contribute 1; tabs contribute 4 or the configured tab stop).
2. Let $T = S_k$ be the current top of the indentation stack.
   * **Case 1 ($C > T$)**: The scanner pushes $C$ onto the stack and emits an `INDENT` token:
     $$\text{Stack}' = [S_0, \dots, S_k, C], \quad \text{Emit}(\text{INDENT})$$
   * **Case 2 ($C == T$)**: The indentation is unchanged. No indentation tokens are emitted.
   * **Case 3 ($C < T$)**: The scanner searches the stack backwards for an index $j$ such that $S_j = C$.
     * If such a $j$ exists, the scanner pops $k - j$ levels, emitting a `DEDENT` token for each popped level:
       $$\text{Stack}' = [S_0, \dots, S_j], \quad \text{Emit}(\text{DEDENT}) \times (k - j)$$
     * If no such $j$ exists, the indentation does not match any outer enclosing block. The scanner raises an `IndentationError` diagnostic with precise source line/column coordinates.
3. **End of File (EOF)**: Upon reaching the end of the input stream, the scanner emits a `DEDENT` token for every remaining indentation level on the stack until only $S_0 = 0$ remains, followed by the terminal `EOF` token.

### 2.3. Multi-Line Expression Continuation
A logical statement automatically spans multiple physical lines without triggering `NEWLINE` or `INDENT`/`DEDENT` tokens when:
1. The lexer is currently inside unclosed grouping delimiters: parentheses `()`, brackets `[]`, or braces `{}`.
2. A binary pipe operator `|>` appears as the final non-whitespace token of a physical line, indicating explicit forward continuation.

### 2.4. Comments & Documentation Docstrings
* **Standard Line Comment**: Initiated by a single hash `#` not enclosed within a string literal. Extends to the end of the physical line and is discarded by the lexer:
  ```unfish
  # This is a standard single-line comment
  let count = 42 # Trailing comment
  ```
* **Documentation Docstring**: Initiated by a double hash `##`. Docstrings are preserved by the compiler and attached to the immediately subsequent declaration (`function`, `struct`, `trait`, or `enum`), making them available to `unfish doc` and the Language Server Protocol (LSP):
  ```unfish
  ## Computes the magnitude of a 2-dimensional vector.
  ## Parameters:
  ##   self - The Vector2 instance.
  ## Returns:
  ##   The Euclidean norm as a floating-point number.
  fn magnitude(self):
      return sqrt(self.x * self.x + self.y * self.y)
  ```

### 2.5. Identifiers & Reserved Keywords
* **Identifiers**: Defined by the regular expression `[a-zA-Z_][a-zA-Z0-9_]*`.
* **Reserved Keywords Table**:
  ```
  and          else         if           null         struct
  async        enum         impl         or           trait
  await        false        import       raise        true
  break        finally      in           repeat       try
  catch        fn           let          return       while
  continue     for          match        say          yield
  elif         from         not          self
  ```

### 2.6. Numeric Literals
Numbers in Unfish are represented internally as IEEE 754 64-bit double-precision floating-point values (`double`), providing 53 bits of mantissa precision ($\pm 9,007,199,254,740,991$ exact integer range):
* **Decimal Integers**: `0`, `42`, `1000000`
* **Floating-Point**: `3.141592653589793`, `0.5`, `0.125` (a digit is required before the point, since `.` also introduces member access)
* **Scientific Exponential**: `1e6`, `2.5e-3`, `6.022e23`, `1.0E+10`
* **Hexadecimal**: Prefixed with `0x` or `0X` (`0xFF`, `0xDEADBEEF`, `0x1A2B`)
* **Binary**: Prefixed with `0b` or `0B` (`0b1010`, `0b11110000`)
* **Octal**: Prefixed with `0o` or `0O` (`0o755`, `0o644`)

### 2.7. String Literals & String Interpolation
* **Single-Line Strings**: Delimited by double quotes `"..."`.
* **Standard Escape Sequences**:
  * `\n`: Line feed (U+000A)
  * `\r`: Carriage return (U+000D)
  * `\t`: Tab (U+0009)
  * `\"`: Double quote (U+0022)
  * `\\`: Backslash (U+005C)
  * `\0`: Null byte (U+0000)
  * `\xHH`: Exact 8-bit byte value in hexadecimal notation
* **Multiline Strings**: Delimited by triple double quotes `"""..."""`. Preserves internal newlines and indentation verbatim.
* **Interpolated Formatted Strings (`f"..."`)**: Prefixing a string with `f` activates string interpolation. Expressions enclosed within `{...}` are evaluated at runtime and converted to string representations:
  ```unfish
  let name = "Lagoon"
  let depth = 45.2
  say f"Location: {name}, Depth: {depth}m, Status: {if depth > 30: 'deep' else: 'shallow'}"
  ```
  * Literal braces inside f-strings are escaped by doubling: `{{` produces `{` and `}}` produces `}`.

---

## 3. Operator Hierarchy & Precedence Table

Unfish expressions are parsed using Pratt's top-down operator precedence algorithm across 13 distinct precedence tiers:

| Precedence | Tier Name | Operators | Associativity | Description |
|---|---|---|---|---|
| **1 (Lowest)** | Pipe | `\|>` | Left | Forward pipeline dataflow |
| **2** | Logical OR | `or` | Left | Short-circuiting logical disjunction |
| **3** | Logical AND | `and` | Left | Short-circuiting logical conjunction |
| **4** | Equality | `==`, `!=` | None | Structural equality / inequality |
| **5** | Relational | `<`, `<=`, `>`, `>=` | None | Numerical / string ordering |
| **6** | Bitwise OR | `bor(...)` (builtin) | Left | Bitwise OR operation |
| **7** | Bitwise XOR | `bxor(...)` (builtin) | Left | Bitwise XOR operation |
| **8** | Bitwise AND | `band(...)` (builtin) | Left | Bitwise AND operation |
| **9** | Bit Shifts | `shl`, `shr`, `sar` | Left | Bitwise logical/arithmetic shifts |
| **10** | Additive | `+`, `-` | Left | Addition / String Concat, Subtraction |
| **11** | Multiplicative | `*`, `/`, `%` | Left | Multiplication, Division, Modulo |
| **12** | Unary Prefix | `-`, `not`, `bnot`, `await` | Right | Negation, Logical NOT, Bitwise NOT |
| **13 (Highest)**| Primary & Postfix | `()`, `[]`, `.`, `f"..."` | Left | Function calls, Indexing, Member access |

---

## 4. Formal EBNF Grammar Specification

```ebnf
(* ========================================================================= *)
(* 1. PROGRAM ROOT & TOP-LEVEL DECLARATIONS                                  *)
(* ========================================================================= *)

Program             = { TopLevelDeclaration | Statement } EOF ;

TopLevelDeclaration = FunctionDeclaration
                    | StructDeclaration
                    | EnumDeclaration
                    | TraitDeclaration
                    | ImplDeclaration
                    | ImportStatement ;

Block               = ':' NEWLINE INDENT Statement { Statement } DEDENT ;

(* ========================================================================= *)
(* 2. STATEMENTS                                                             *)
(* ========================================================================= *)

Statement           = LetStatement
                    | AssignmentStatement
                    | IfStatement
                    | WhileStatement
                    | ForInStatement
                    | RepeatStatement
                    | ReturnStatement
                    | BreakStatement
                    | ContinueStatement
                    | RaiseStatement
                    | TryCatchFinallyStatement
                    | MatchStatement
                    | SayStatement
                    | PrintStatement
                    | ExpressionStatement ;

LetStatement        = 'let' Pattern [ ':' TypeAnnotation ] '=' Expression NEWLINE ;

AssignmentStatement = LValue AssignmentOperator Expression NEWLINE ;
AssignmentOperator  = '=' | '+=' | '-=' | '*=' | '/=' | '%=' ;

LValue              = IDENTIFIER
                    | PrimaryExpression '[' Expression ']'
                    | PrimaryExpression '.' IDENTIFIER ;

IfStatement         = 'if' Expression Block
                      { 'elif' Expression Block }
                      [ 'else' Block ] ;

WhileStatement      = 'while' Expression Block ;

ForInStatement      = 'for' ( IDENTIFIER | DestructurePattern ) 'in' Expression Block ;

RepeatStatement     = 'repeat' Expression Block ;

ReturnStatement     = 'return' [ Expression ] NEWLINE ;

BreakStatement      = 'break' NEWLINE ;

ContinueStatement   = 'continue' NEWLINE ;

RaiseStatement      = 'raise' Expression NEWLINE ;

TryCatchFinallyStatement = 'try' Block
                           'catch' IDENTIFIER Block
                           [ 'finally' Block ] ;

MatchStatement      = 'match' Expression ':' NEWLINE INDENT
                          MatchArm { MatchArm }
                      DEDENT ;

MatchArm            = MatchPattern [ 'if' Expression ] '=>' ( Statement | Block ) ;

SayStatement        = 'say' Expression NEWLINE ;

PrintStatement      = 'print' Expression NEWLINE ;

ExpressionStatement = Expression NEWLINE ;

(* ========================================================================= *)
(* 3. DECLARATIONS: FUNCTIONS, STRUCTS, ENUMS, TRAITS                        *)
(* ========================================================================= *)

FunctionDeclaration = [ 'async' ] ( 'function' | 'fn' ) IDENTIFIER [ GenericParameters ]
                      '(' [ ParameterList ] ')' [ ':' TypeAnnotation ] Block ;

ParameterList       = Parameter { ',' Parameter } [ ',' RestParameter ] ;
Parameter           = IDENTIFIER [ ':' TypeAnnotation ] [ '=' Expression ] ;
RestParameter       = '...' IDENTIFIER ;

StructDeclaration   = 'struct' IDENTIFIER [ GenericParameters ] ':' NEWLINE INDENT
                          { StructMember }
                      DEDENT ;

StructMember        = StructField | MethodDeclaration ;
StructField         = IDENTIFIER [ ':' TypeAnnotation ] NEWLINE ;
MethodDeclaration   = ( 'function' | 'fn' ) IDENTIFIER '(' [ ParameterList ] ')' [ ':' TypeAnnotation ] Block ;

EnumDeclaration     = 'enum' IDENTIFIER ':' NEWLINE INDENT
                          EnumVariant { EnumVariant }
                      DEDENT ;

EnumVariant         = IDENTIFIER [ '(' [ ParameterList ] ')' ] NEWLINE ;

TraitDeclaration    = 'trait' IDENTIFIER [ GenericParameters ] ':' NEWLINE INDENT
                          { TraitMethodSignature }
                      DEDENT ;

TraitMethodSignature = ( 'function' | 'fn' ) IDENTIFIER '(' [ ParameterList ] ')' [ ':' TypeAnnotation ] NEWLINE ;

ImplDeclaration     = 'impl' IDENTIFIER [ GenericParameters ] 'for' IDENTIFIER ':' NEWLINE INDENT
                          { MethodDeclaration }
                      DEDENT ;

ImportStatement     = 'import' ModulePath [ 'as' IDENTIFIER ] NEWLINE
                    | 'from' ModulePath 'import' ImportSymbolList NEWLINE ;

ModulePath          = IDENTIFIER { '.' IDENTIFIER } | STRING_LITERAL ;
ImportSymbolList    = IDENTIFIER [ 'as' IDENTIFIER ] { ',' IDENTIFIER [ 'as' IDENTIFIER ] } ;

(* ========================================================================= *)
(* 4. EXPRESSIONS & OPERATORS                                                *)
(* ========================================================================= *)

Expression          = PipeExpression ;

PipeExpression      = LogicalOrExpression { '|>' LogicalOrExpression } ;

LogicalOrExpression = LogicalAndExpression { 'or' LogicalAndExpression } ;

LogicalAndExpression= EqualityExpression { 'and' EqualityExpression } ;

EqualityExpression  = RelationalExpression { ( '==' | '!=' ) RelationalExpression } ;

RelationalExpression= AdditiveExpression { ( '<' | '<=' | '>' | '>=' ) AdditiveExpression } ;

AdditiveExpression  = MultiplicativeExpression { ( '+' | '-' ) MultiplicativeExpression } ;

MultiplicativeExpression = UnaryExpression { ( '*' | '/' | '%' ) UnaryExpression } ;

UnaryExpression     = ( '-' | 'not' | 'bnot' | 'await' ) UnaryExpression
                    | PostfixExpression ;

PostfixExpression   = PrimaryExpression { CallSuffix | IndexSuffix | DotSuffix } ;

CallSuffix          = '(' [ ArgumentList ] ')' ;
IndexSuffix         = '[' Expression ']' ;
DotSuffix           = '.' IDENTIFIER ;

ArgumentList        = ArgumentItem { ',' ArgumentItem } ;
ArgumentItem        = [ '...' ] Expression ;

PrimaryExpression   = NUMBER_LITERAL
                    | STRING_LITERAL
                    | INTERPOLATED_STRING
                    | 'true'
                    | 'false'
                    | 'null'
                    | 'self'
                    | IDENTIFIER
                    | '(' Expression ')'
                    | ArrayLiteral
                    | MapLiteral
                    | ComprehensionExpression
                    | LambdaExpression ;

ArrayLiteral        = '[' [ ArgumentList ] ']' ;

MapLiteral          = '{' [ MapEntryList ] '}' ;
MapEntryList        = MapEntry { ',' MapEntry } ;
MapEntry            = ( IDENTIFIER | STRING_LITERAL | '[' Expression ']' ) ':' Expression
                    | '...' Expression ;

ComprehensionExpression = '[' Expression 'for' IDENTIFIER 'in' Expression [ 'if' Expression ] ']' ;

LambdaExpression    = ( 'function' | 'fn' ) '(' [ ParameterList ] ')' [ ':' TypeAnnotation ] ( Block | ':' Expression ) ;

(* ========================================================================= *)
(* 5. PATTERNS & DESTRUCTURING                                               *)
(* ========================================================================= *)

Pattern             = IDENTIFIER | DestructurePattern ;

DestructurePattern  = ArrayPattern | MapPattern ;

MatchPattern        = LiteralPattern
                    | VariablePattern
                    | WildcardPattern
                    | ArrayPattern
                    | MapPattern
                    | EnumPattern ;

LiteralPattern      = NUMBER_LITERAL | STRING_LITERAL | 'true' | 'false' | 'null' ;
VariablePattern     = IDENTIFIER ;
WildcardPattern     = '_' ;
ArrayPattern        = '[' [ PatternList ] ']' ;
MapPattern          = '{' [ MapPatternList ] '}' ;
EnumPattern         = IDENTIFIER '.' IDENTIFIER [ '(' [ PatternList ] ')' ] ;

PatternList         = MatchPattern { ',' MatchPattern } ;
MapPatternList      = MapPatternEntry { ',' MapPatternEntry } ;
MapPatternEntry     = IDENTIFIER [ ':' MatchPattern ] ;

(* ========================================================================= *)
(* 6. GRADUAL TYPE ANNOTATIONS                                               *)
(* ========================================================================= *)

TypeAnnotation      = BasicType
                    | GenericType
                    | FunctionType
                    | UnionType ;

BasicType           = 'Int' | 'Float' | 'Number' | 'String' | 'Boolean' | 'Null' | 'Any' | IDENTIFIER ;
GenericType         = IDENTIFIER '<' TypeAnnotation { ',' TypeAnnotation } '>' ;
FunctionType        = 'Function' '<' '(' [ TypeAnnotation { ',' TypeAnnotation } ] ')' '->' TypeAnnotation '>' ;
UnionType           = TypeAnnotation '|' TypeAnnotation ;
```

---

## 5. Operational Semantics & Execution Models

### 5.1. Variables, Scoping & Upvalue Capture
1. **Lexical Binding (`let`)**: `let x = expr` binds identifier `x` in the immediate lexical block scope. A duplicate declaration of `x` within the identical scope produces a compile-time semantic error.
2. **Lexical Scope Resolution**: Variable lookup traverses parent lexical environments from innermost to outermost, ending at the global environment.
3. **Identifier Shadowing**: Inner scopes may shadow variables of identical names from enclosing scopes or global builtins without mutating the outer binding.
4. **Function Hoisting**: Named top-level function declarations are hoisted to the head of their enclosing block or module, permitting mutual recursion without forward declarations.
5. **Lexical Closures (Upvalues)**: When a function captures a local variable from an enclosing scope:
   * While the enclosing frame remains active on the call stack, the variable is accessed via an *open upvalue* referencing the stack slot.
   * When the enclosing frame exits, the VM executes `OP_CLOSE_UPVALUE`, copying the stack value into heap storage (*closed upvalue*). The closure retains full read/write access for its entire lifetime.

### 5.2. Control Flow & Truthiness Semantics
* **Strict Truthiness**: In Unfish, only `false` and `null` evaluate to falsy in conditional contexts (`if`, `while`, logical `and`/`or`). All other values—including numeric `0`, empty strings `""`, empty arrays `[]`, and empty maps `{}`—are strictly truthy.
* **Short-Circuit Evaluation**:
  * `a or b`: Evaluates `a`. If `a` is truthy, returns `a` immediately without evaluating `b`. Otherwise, returns `b`.
  * `a and b`: Evaluates `a`. If `a` is falsy, returns `a` immediately without evaluating `b`. Otherwise, returns `b`.
* **Loop Control (`break` / `continue`)**:
  * `break`: Immediately exits the innermost loop.
  * `continue`: Aborts the current iteration and jumps to the loop header/increment test.
  * Using `break` or `continue` outside an enclosing loop triggers a compile-time syntax error.

### 5.3. Structured Exception Unwinding Semantics
Unfish provides full structured exception handling via `try`, `catch`, and optional `finally`:

```unfish
try:
    risky_operation()
catch err:
    say f"Caught error: {err}"
finally:
    cleanup_resources()
```

#### The Dual-Stack Exception Invariants
1. **Handler Registration (`OP_PUSH_TRY`)**:
   * Upon entering a `try` block, the VM pushes a `UfTryFrame` recording:
     * `catch_ip`: Bytecode address of the `catch` handler.
     * `frame_index`: Current call frame depth.
     * `stack_depth`: Current operand evaluation stack height.
2. **Exception Propagation (`raise` / `error()`)**:
   * When an exception is raised, the VM inspects the `try_stack`:
     * If handlers exist, the VM unwinds all call frames created after the `try` block entered (`vm->frame_count = handler->frame_index + 1`).
     * The operand stack is restored to `handler->stack_depth`.
     * The error value is pushed onto the restored stack.
     * Control jumps directly to `handler->catch_ip`.
3. **Structured Cleanup (`finally`)**:
   * Code in a `finally` block is guaranteed to execute whether the `try` block completes normally, raises an exception caught by `catch`, or attempts an early return.
4. **Early Returns & Loop Escapes Inside `try`**:
   * When `return`, `break`, or `continue` is executed within a `try` block, the compiler emits explicit `OP_POP_TRY` instructions for each active enclosing `try` block up to the target scope boundary, preventing stale handler stack corruption.

### 5.4. Structs, Methods & Dynamic Dispatch
* **Struct Instantiation**: Calling a struct name `Fish(...)` invokes its constructor, allocating an instance object initialized with field arguments in declaration order.
* **Method Invocation**: Method calls `instance.method(arg1, arg2)` bind `instance` as the explicit first parameter `self`.
* **Field Mutability**: Struct fields are mutable via dot-assignment `instance.field = value`.
* **Dynamic Reflection**: `has_field(inst, "field")` and `fields(inst)` permit dynamic structural reflection.

---

## 6. Gradual Type System Tiers

Unfish implements a multi-tier gradual type system that provides educational progression from pure dynamic scripting to strict compile-time type verification:

* **Tier 0: Untyped Dynamic (Default)**: Variables and parameters carry no type annotations. Full dynamic duck-typing is preserved.
* **Tier 1: Soft Annotations**: Types can be written (`let x: Number = 42`, `fn add(a: Int, b: Int): Int`) to serve as self-documenting code and LSP editor hints.
* **Tier 2: Gradual Consistency (`unfish check`)**: The semantic analyzer verifies local type assignments and warns on incompatible primitive operations while allowing `Any` escape hatches.
* **Tier 3: Strict Static Mode (`unfish check --strict`)**: Type consistency is strictly enforced. Implicit type conversions are rejected, missing trait implementations trigger compile-time errors, and exhaustiveness checking is mandated on all `match` expressions.
