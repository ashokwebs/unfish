# UNFISH — FORMAL LANGUAGE SPECIFICATION & REFERENCE MANUAL

---

## 1. Introduction & Notation

This document provides the definitive, formal language specification for **Unfish**. It is written for compiler implementors, language designers, educators, and advanced developers.

### Grammar Notation (Extended Backus-Naur Form)
Grammar productions in this specification use standard EBNF notation:
* `Rule = Production ;` defines a grammar rule.
* `[ Item ]` denotes an optional element (zero or one occurrence).
* `{ Item }` denotes repetition (zero or more occurrences).
* `Item1 | Item2` denotes alternation (either Item1 or Item2).
* `'literal'` denotes a terminal token.
* `( Item1 Item2 )` groups items.

---

## 2. Lexical Structure

### 2.1. Source Encoding & Character Set
Unfish source files must be encoded in valid **UTF-8**. Source files may optionally begin with a UTF-8 Byte Order Mark (BOM `0xEF, 0xBB, 0xBF`), which is silently discarded by the lexer.

### 2.2. Whitespace, Newlines & Indentation
Unfish enforces block structure using indentation (the off-side rule), eliminating the need for curly braces `{}` or `begin`/`end` keywords:

* **Whitespace**: Horizontal space characters (`' '` ASCII 32) and tabs (`'\t'` ASCII 9). Inside code lines, whitespace separates tokens.
* **Line Terminators**: Standard Unix newline (`\n` ASCII 10) or Windows CRLF (`\r\n` ASCII 13, 10). Both are normalized to a logical `NEWLINE` token.
* **Blank Lines**: Lines containing only whitespace and/or comments are completely ignored by the indentation scanner and do not emit indentation tokens.
* **Logical Lines**: A single logical statement can span multiple physical lines if enclosed within matching parentheses `()`, brackets `[]`, or braces `{}`, or if an explicit trailing pipe operator `|>` appears at the end of the line.

#### The Indentation Stack Algorithm
The lexer (`src/lexer/uf_lexer.c`) maintains an internal stack of indentation levels (column positions in spaces, with tab width configurable, default 4 spaces).
1. At the beginning of each non-blank physical line, the lexer measures the leading whitespace column count $C$.
2. Let $T$ be the top of the indentation stack.
   * If $C > T$: Push $C$ onto the stack and emit an `INDENT` token.
   * If $C == T$: Emit no indentation tokens.
   * If $C < T$: Pop from the stack repeatedly until $T' == C$. For each popped level, emit a `DEDENT` token. If no matching level is found on the stack, report an `IndentationError` diagnostic.
3. At the end of the source file (`EOF`), the lexer emits a `DEDENT` token for every remaining indentation level on the stack until only level 0 remains, followed by `EOF`.

### 2.3. Comments & Docstrings
Unfish provides two types of comments:
* **Single-Line Comment**: Begins with `#` and continues to the end of the line. Ignored by the parser.
  ```unfish
  # This is a standard comment
  let x = 10 # Inline comment
  ```
* **Documentation Docstring**: Begins with `##` and continues to the end of the line. Attached to the immediately following declaration (function, struct, method, or module) by `unfish doc` and the Language Server Protocol:
  ```unfish
  ## Computes the Euclidean distance between two points.
  ## Takes a Point instance and returns a floating-point number.
  function distance(p1, p2):
      return sqrt((p1.x - p2.x)**2 + (p1.y - p2.y)**2)
  ```

### 2.4. Identifiers & Keywords
* **Identifiers**: Match the regular expression `[a-zA-Z_][a-zA-Z0-9_]*`. Case-sensitive.
* **Reserved Keywords**:
  ```
  and          else         if           null         struct
  async        enum         impl         or           trait
  await        false        import       raise        true
  break        finally      in           repeat       try
  catch        for          let          return       while
  continue     from         match        say          yield
  elif         function     not          self
  ```

### 2.5. Literals

#### Numbers
Numbers in Unfish are represented internally as IEEE 754 64-bit double-precision floating-point values (`double`), capable of precisely representing integers up to $2^{53} - 1$ ($\pm 9,007,199,254,740,991$):
* **Decimal Integer**: `0`, `42`, `1000000`
* **Floating-Point**: `3.14159`, `0.5`, `.25`
* **Scientific Notation**: `1e6`, `2.5e-3`, `1.0E+10`
* **Hexadecimal**: `0x1A`, `0xFF00`, `0xDEADBEEF`
* **Binary**: `0b1010`, `0b11110000`
* **Octal**: `0o77`, `0o755`

#### Strings
Strings are UTF-8 encoded sequences of bytes:
* **Single-Line String**: Enclosed in double quotes `"..."`.
* **Escape Sequences**:
  * `\n`: Line feed (0x0A)
  * `\t`: Horizontal tab (0x09)
  * `\r`: Carriage return (0x0D)
  * `\"`: Double quote (0x22)
  * `\\`: Backslash (0x5C)
  * `\0`: Null byte (0x00)
  * `\xHH`: Hexadecimal byte value (e.g. `\x1B` for ESC)
* **String Interpolation (`f"..."`)**: Formatted string literals prefixed with `f` support dynamic expression embedding via `{expression}` and literal brace escaping via `{{` and `}}`:
  ```unfish
  let user = "Alice"
  let score = 95
  say f"User: {user}, Score: {score + 5}"
  # Emits: User: Alice, Score: 100

  # Multiline f-strings:
  let multiline = f"""Results for {user}:
  Score: {score}"""
  ```

#### Booleans & Null
* `true`: Logical true.
* `false`: Logical false.
* `null`: Represents the absence of a value.

---

## 3. Operator Precedence & Associativity

The Unfish Pratt expression parser resolves operators according to 13 strict precedence tiers (from lowest to highest):

| Precedence | Level Name | Operators | Associativity | Description |
|---|---|---|---|---|
| **1** | Pipe | `\|>` | Left | Forward pipeline data flow |
| **2** | Logical OR | `or` | Left | Short-circuiting logical disjunction |
| **3** | Logical AND | `and` | Left | Short-circuiting logical conjunction |
| **4** | Equality | `==`, `!=` | None | Structural equality and inequality |
| **5** | Relational | `<`, `<=`, `>`, `>=` | None | Comparison tests |
| **6** | Bitwise OR | `bor` (fn) | Left | Bitwise inclusive OR |
| **7** | Bitwise XOR | `bxor` (fn) | Left | Bitwise exclusive OR |
| **8** | Bitwise AND | `band` (fn) | Left | Bitwise AND |
| **9** | Bit Shifts | `shl`, `shr`, `sar` (fn)| Left | Bitwise shifts |
| **10** | Additive | `+`, `-` | Left | Addition / String Concat, Subtraction |
| **11** | Multiplicative | `*`, `/`, `%` | Left | Multiplication, Division, Modulo |
| **12** | Unary Prefix | `-`, `not`, `bnot` | Right | Arithmetic negation, Logical NOT |
| **13** | Primary / Call | `()`, `[]`, `.`, `await`| Left | Function calls, Indexing, Member access |

---

## 4. Formal EBNF Grammar

```ebnf
(* ========================================================================= *)
(* PROGRAM & TOP-LEVEL STRUCTURE                                             *)
(* ========================================================================= *)

Program         = { TopLevelItem } EOF ;

TopLevelItem    = Stmt
                | FunctionDecl
                | StructDecl
                | EnumDecl
                | TraitDecl
                | ImplDecl
                | ImportStmt ;

Block           = ':' NEWLINE INDENT { Stmt } DEDENT ;

(* ========================================================================= *)
(* STATEMENTS & DECLARATIONS                                                 *)
(* ========================================================================= *)

Stmt            = LetStmt
                | AssignStmt
                | IfStmt
                | WhileStmt
                | ForInStmt
                | RepeatStmt
                | ReturnStmt
                | BreakStmt
                | ContinueStmt
                | RaiseStmt
                | TryCatchStmt
                | MatchStmt
                | SayStmt
                | ExprStmt ;

LetStmt         = 'let' Pattern [ ':' TypeAnnotation ] '=' Expr NEWLINE ;

AssignStmt      = LValue ( '=' | '+=' | '-=' | '*=' | '/=' | '%=' ) Expr NEWLINE ;

LValue          = IDENTIFIER
                | Expr '[' Expr ']'
                | Expr '.' IDENTIFIER ;

IfStmt          = 'if' Expr Block
                  { 'elif' Expr Block }
                  [ 'else' Block ] ;

WhileStmt       = 'while' Expr Block ;

ForInStmt       = 'for' ( IDENTIFIER | DestructurePattern ) 'in' Expr Block ;

RepeatStmt      = 'repeat' Expr Block ;

ReturnStmt      = 'return' [ Expr ] NEWLINE ;

BreakStmt       = 'break' NEWLINE ;

ContinueStmt    = 'continue' NEWLINE ;

RaiseStmt       = 'raise' Expr NEWLINE ;

TryCatchStmt    = 'try' Block
                  'catch' IDENTIFIER Block
                  [ 'finally' Block ] ;

MatchStmt       = 'match' Expr ':' NEWLINE INDENT { MatchArm } DEDENT ;

MatchArm        = MatchPattern [ 'if' Expr ] '=>' ( Stmt | Block ) ;

SayStmt         = 'say' Expr NEWLINE ;

ExprStmt        = Expr NEWLINE ;

(* ========================================================================= *)
(* FUNCTIONS, STRUCTS, TRAITS & ENUMS                                        *)
(* ========================================================================= *)

FunctionDecl    = [ 'async' ] 'function' IDENTIFIER [ GenericParams ] '(' [ ParamList ] ')' [ ':' TypeAnnotation ] Block ;

ParamList       = Param { ',' Param } [ ',' RestParam ] ;
Param           = IDENTIFIER [ ':' TypeAnnotation ] [ '=' Expr ] ;
RestParam       = '...' IDENTIFIER ;

StructDecl      = 'struct' IDENTIFIER [ GenericParams ] ':' NEWLINE INDENT
                      { StructField | FunctionDecl }
                  DEDENT ;

StructField     = IDENTIFIER [ ':' TypeAnnotation ] NEWLINE ;

EnumDecl        = 'enum' IDENTIFIER ':' NEWLINE INDENT
                      { EnumVariant }
                  DEDENT ;

EnumVariant     = IDENTIFIER [ '(' [ ParamList ] ')' ] NEWLINE ;

TraitDecl       = 'trait' IDENTIFIER [ GenericParams ] ':' NEWLINE INDENT
                      { TraitMethod }
                  DEDENT ;

TraitMethod     = 'function' IDENTIFIER '(' [ ParamList ] ')' [ ':' TypeAnnotation ] NEWLINE ;

ImplDecl        = 'impl' IDENTIFIER [ GenericParams ] 'for' IDENTIFIER ':' NEWLINE INDENT
                      { FunctionDecl }
                  DEDENT ;

ImportStmt      = 'import' ModulePath [ 'as' IDENTIFIER ] NEWLINE
                | 'from' ModulePath 'import' ImportSymbolList NEWLINE ;

ModulePath      = IDENTIFIER { '.' IDENTIFIER } | STRING_LITERAL ;
ImportSymbolList = IDENTIFIER [ 'as' IDENTIFIER ] { ',' IDENTIFIER [ 'as' IDENTIFIER ] } ;

(* ========================================================================= *)
(* EXPRESSIONS                                                               *)
(* ========================================================================= *)

Expr            = PipeExpr ;

PipeExpr        = LogicalOrExpr { '|>' LogicalOrExpr } ;

LogicalOrExpr   = LogicalAndExpr { 'or' LogicalAndExpr } ;

LogicalAndExpr  = EqualityExpr { 'and' EqualityExpr } ;

EqualityExpr    = RelationalExpr { ( '==' | '!=' ) RelationalExpr } ;

RelationalExpr  = AdditiveExpr { ( '<' | '<=' | '>' | '>=' ) AdditiveExpr } ;

AdditiveExpr    = MultiplicativeExpr { ( '+' | '-' ) MultiplicativeExpr } ;

MultiplicativeExpr = UnaryExpr { ( '*' | '/' | '%' ) UnaryExpr } ;

UnaryExpr       = ( '-' | 'not' ) UnaryExpr
                | 'await' UnaryExpr
                | CallIndexExpr ;

CallIndexExpr   = PrimaryExpr { CallSuffix | IndexSuffix | DotSuffix } ;

CallSuffix      = '(' [ ArgList ] ')' ;
IndexSuffix     = '[' Expr ']' ;
DotSuffix       = '.' IDENTIFIER ;

ArgList         = ArgItem { ',' ArgItem } ;
ArgItem         = [ '...' ] Expr ;

PrimaryExpr     = NUMBER
                | STRING
                | 'true'
                | 'false'
                | 'null'
                | 'self'
                | IDENTIFIER
                | '(' Expr ')'
                | ArrayLiteral
                | MapLiteral
                | ComprehensionExpr
                | AnonymousFunction ;

ArrayLiteral    = '[' [ ArgList ] ']' ;

MapLiteral      = '{' [ MapEntryList ] '}' ;
MapEntryList    = MapEntry { ',' MapEntry } ;
MapEntry        = ( IDENTIFIER | STRING | '[' Expr ']' ) ':' Expr
                | '...' Expr ;

ComprehensionExpr = '[' Expr 'for' IDENTIFIER 'in' Expr [ 'if' Expr ] ']' ;

AnonymousFunction = 'function' '(' [ ParamList ] ')' [ ':' TypeAnnotation ] Block ;

(* ========================================================================= *)
(* PATTERNS & DESTRUCTURING                                                  *)
(* ========================================================================= *)

MatchPattern    = LiteralPattern
                | VariablePattern
                | WildcardPattern
                | ArrayPattern
                | MapPattern
                | EnumPattern ;

LiteralPattern  = NUMBER | STRING | 'true' | 'false' | 'null' ;
VariablePattern = IDENTIFIER ;
WildcardPattern = '_' ;
ArrayPattern    = '[' [ PatternList ] ']' ;
MapPattern      = '{' [ MapPatternList ] '}' ;
EnumPattern     = IDENTIFIER '.' IDENTIFIER [ '(' [ PatternList ] ')' ] ;

DestructurePattern = ArrayPattern | MapPattern ;
```

---

## 5. Semantic Rules & Execution Semantics

### 5.1. Variables & Scope
1. **Declaration (`let`)**: `let name = expr` introduces a binding into the current lexical scope. Variable redeclaration in the same scope without shadowing is rejected by the semantic analyzer with an error diagnostic.
2. **Lexical Scoping**: Inner scopes can read and mutate bindings from enclosing outer scopes.
3. **Shadowing**: A declaration in an inner scope may shadow a declaration in an outer scope. User variables may cleanly shadow global built-in functions.
4. **Hoisting**: Top-level function declarations are hoisted to the enclosing program or module scope. Functions can be called before their syntactic point of declaration, enabling mutual recursion:
   ```unfish
   function is_even(n):
       if n == 0: return true
       return is_odd(n - 1)

   function is_odd(n):
       if n == 0: return false
       return is_even(n - 1)
   ```

### 5.2. Control Flow Semantics
* **`if` / `elif` / `else`**: Evaluates conditions sequentially. Only values that are strictly `false` or `null` are considered falsy; all other values (including `0`, `""`, and `[]`) are truthy.
* **`while cond:`**: Iterates while `cond` evaluates to a truthy value.
* **`for item in iterable:`**: Iterates over elements of arrays, keys of maps, characters of strings, or ranges produced by `range(start, end)`. Supports pattern destructuring:
  ```unfish
  let pairs = [["a", 1], ["b", 2]]
  for [key, val] in pairs:
      say key + " -> " + str(val)
  ```
* **`repeat count:`**: Dedicated educational construct executing a block exactly `count` times (where `count` must evaluate to a positive integer).
* **`break` / `continue`**: `break` immediately terminates the innermost enclosing loop; `continue` skips the remainder of the current iteration. Using either outside a loop is a compile-time semantic error.

### 5.3. Functions, Closures & Parameters
* **First-Class Citizens**: Functions are runtime values of kind `UF_VAL_FUNCTION` or `UF_VAL_CLOSURE`. They can be stored in variables, passed as arguments, returned from other functions, and stored in collections.
* **Lexical Closures**: A function captures any local variables from enclosing scopes that it references. Captured variables remain accessible and mutable across the closure's entire lifetime even after the enclosing function returns.
* **Default Arguments**: Function parameters may declare default values (`function f(x = 10)`). Default parameters must follow non-default parameters.
* **Rest Parameters (`...rest`)**: Collects all remaining arguments into a dynamically allocated array. Must be the final parameter.
* **Spread Arguments (`f(...args)`)**: Expands an array into individual function arguments at the call site.

### 5.4. Pipe Operator (`|>`)
Unfish provides the forward pipe operator `|>` to facilitate clear, readable data processing pipelines:
```unfish
# Equivalent to: say(to_upper(trim("  unfish  ")))
"  unfish  " |> trim |> to_upper |> say
```
When piped into a function call with multiple arguments, the piped value becomes the first argument:
```unfish
# Equivalent to: filter(map(nums, double_fn), is_even)
nums |> map(double_fn) |> filter(is_even)
```

### 5.5. Structs & Object-Oriented Semantics
* **Declaration**: Structs declare named fields and associated methods.
* **Constructors**: Invoking the struct name as a function constructs a new instance with fields initialized in declaration order:
  ```unfish
  struct Vector2:
      x
      y

      function magnitude(self):
          return sqrt(self.x * self.x + self.y * self.y)

  let v = Vector2(3, 4)
  say v.magnitude() # 5
  ```
* **The `self` Keyword**: The first parameter of every struct method must be named `self`, explicitly binding the instance on method dispatch. Calling a struct method without an explicit `self` parameter declaration triggers a compile-time diagnostic.

### 5.6. Pattern Matching (`match`)
The `match` construct provides multi-way branching based on value shape and type:
```unfish
match shape:
    Circle(r) => PI * r * r
    Rectangle(w, h) => w * h
    Point(x, y) if x == y => 0
    _ => -1
```
* **Exhaustiveness**: A wildcard `_` or variable pattern guarantees exhaustiveness.
* **Guards**: `if <expr>` guards allow arbitrary boolean expressions to restrict pattern matching.

### 5.7. Concurrency: Fibers & Channels
Unfish provides cooperative user-space fibers:
```unfish
let ch = channel()

spawn(function():
    send(ch, "Message from fiber")
)

let msg = recv(ch)
say msg # "Message from fiber"
run_scheduler()
```
* `spawn(fn)` creates a new cooperative fiber.
* `yield()` yields the CPU to the next scheduled fiber.
* `channel(cap)` creates a message passing channel.
* `send(ch, val)` sends a value to the channel.
* `recv(ch)` receives a value from the channel.
* `run_scheduler()` runs all queued fibers to completion.
