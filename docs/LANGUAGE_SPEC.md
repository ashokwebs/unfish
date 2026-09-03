# UNFISH — FORMAL LANGUAGE SPECIFICATION

**Version:** 0.1.0-alpha  
**Status:** Canonical Reference

---

## 1. Feature Status Inventory

| Feature | Status | Implementation Reference |
|---|---|---|
| UTF-8 Source Encoding | **IMPLEMENTED** | `src/lexer/uf_lexer.c` |
| Comments (`# ...`) | **IMPLEMENTED** | `src/lexer/uf_lexer.c` |
| Indentation (Off-Side Rule) | **IMPLEMENTED** | `src/lexer/uf_lexer.c` (`INDENT`, `DEDENT`, `NEWLINE`) |
| Identifiers & Keywords | **IMPLEMENTED** | `src/lexer/uf_token.c` |
| Numeric Literals (Integers & Floats) | **IMPLEMENTED** | `src/lexer/uf_lexer.c` (64-bit IEEE 754) |
| String Literals (`"..."` with `\n`, `\t`, `\"`, `\\`) | **IMPLEMENTED** | `src/lexer/uf_lexer.c` |
| Boolean Literals (`true`, `false`) | **IMPLEMENTED** | `src/lexer/uf_token.c` |
| Null Literal (`null`) | **IMPLEMENTED** | `src/lexer/uf_token.c` |
| Arithmetic Ops (`+`, `-`, `*`, `/`, `%`) | **IMPLEMENTED** | `src/interpreter/uf_interpreter.c` |
| Equality & Relational Ops (`==`, `!=`, `<`, `<=`, `>`, `>=`) | **IMPLEMENTED** | `src/interpreter/uf_interpreter.c` |
| Logical Operators (`and`, `or`, `not` with short-circuit) | **IMPLEMENTED** | `src/interpreter/uf_interpreter.c` |
| Variable Declarations (`let x = ...`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| Variable Assignment (`x = ...`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| Output Statement (`say <expr>`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| Conditionals (`if / else / else if`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| While Loops (`while <cond>:`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| Repeat Loops (`repeat <n> times:`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| Functions & Lexical Closures | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/runtime/uf_runtime.c` |
| Function Hoisting (Top-level) | **IMPLEMENTED** | `src/semantic/uf_semantic.c`, `src/interpreter/uf_interpreter.c` |
| Return Statements (`return [<expr>]`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| Standard Library Built-ins (`say`, `print`, `type_of`, `len`, `clock`, `assert`) | **IMPLEMENTED** | `src/runtime/uf_runtime.c` |
| Arrays & Indexing (`[1, 2, 3]`, `arr[0]`) | **PLANNED** (Phase 3) | Target next milestone |
| Maps / Dictionaries (`{"k": v}`) | **PLANNED** (Phase 3) | Target next milestone |
| Modules & Imports (`import math`) | **PLANNED** (Phase 8) | Module system milestone |
| Static / Gradual Type Annotations | **PLANNED** (Phase 4) | Gradual type checker |
| Structs / Custom Types | **PLANNED** (Phase 4) | Data types milestone |
| Pattern Matching | **NOT IMPLEMENTED** | Deferred to future phase |
| Asynchronous / Concurrency Constructs | **NOT IMPLEMENTED** | Deferred to Phase 10 |

---

## 2. Lexical Grammar

### 2.1. Character Set & Encoding [IMPLEMENTED]
Source files must be encoded in UTF-8.

### 2.2. Whitespace & Newlines [IMPLEMENTED]
* **Horizontal Whitespace**: Space (`U+0020`) and horizontal tab (`U+0009`). Spaces are standard (4 spaces per indentation level recommended). Mixed tabs and spaces on the same indentation line are strictly rejected with an informative diagnostic.
* **Line Terminators**: LF (`\n`, `U+000A`) or CRLF (`\r\n`).
* **Blank Lines**: Lines containing only whitespace and/or comments are ignored and do not emit `NEWLINE` or indentation tokens.

### 2.3. Comments [IMPLEMENTED]
Comments begin with a hash character `#` and extend to the end of the physical line:
```unfish
# This is a comment
let x = 42 # Inline comment
```

### 2.4. Indentation (Off-Side Rule) [IMPLEMENTED]
Blocks of code are delimited by indentation changes. The lexer maintains an internal indentation stack:
* At the beginning of each non-blank logical line, the lexer measures the column indentation.
* If indentation is greater than the stack top: push current level, emit `INDENT`.
* If indentation is equal to the stack top: emit no indentation token.
* If indentation is less than the stack top: pop levels until a match is found. For each popped level, emit `DEDENT`. If the current indentation matches no previous level on the stack, an `IndentationError` is raised.
* At the end of input (EOF), the lexer emits a `NEWLINE` (if the file did not terminate with one) followed by `DEDENT` tokens for every remaining level on the stack until level 0 is reached.

### 2.5. Identifiers [IMPLEMENTED]
An identifier begins with an ASCII letter (`a-z`, `A-Z`) or an underscore (`_`), followed by any number of ASCII letters, digits (`0-9`), or underscores.

### 2.6. Keywords [IMPLEMENTED]
Reserved keywords:
```
and         break       continue    else        false
function    if          let         not         null
or          repeat      return      say         times
true        while
```

### 2.7. Literals [IMPLEMENTED]
* **Number**: decimal integers or floats (e.g., `0`, `42`, `3.14159`).
* **String**: delimited by `"..."` supporting `\n`, `\t`, `\"`, `\\`.
* **Boolean**: `true`, `false`.
* **Null**: `null`.

---

## 3. Syntactic Grammar (EBNF) [IMPLEMENTED]

```ebnf
Program        = { Statement | NEWLINE } , EOF ;

Statement      = LetStmt
               | AssignStmt
               | IndexAssignStmt
               | SayStmt
               | IfStmt
               | WhileStmt
               | RepeatStmt
               | ForStmt
               | BreakStmt
               | ContinueStmt
               | FunctionStmt
               | ReturnStmt
               | ExprStmt ;

Block          = ":" , NEWLINE , INDENT , { Statement | NEWLINE } , DEDENT ;

LetStmt        = "let" , identifier , [ "=" , Expression ] , NEWLINE ;
AssignStmt     = identifier , "=" , Expression , NEWLINE ;
IndexAssignStmt= Expression , "[" , Expression , "]" , "=" , Expression , NEWLINE ;
SayStmt        = "say" , Expression , NEWLINE ;
BreakStmt      = "break" , NEWLINE ;
ContinueStmt   = "continue" , NEWLINE ;
ReturnStmt     = "return" , [ Expression ] , NEWLINE ;
ExprStmt       = Expression , NEWLINE ;

IfStmt         = "if" , Expression , Block , [ "else" , ( Block | IfStmt ) ] ;
WhileStmt      = "while" , Expression , Block ;
RepeatStmt     = "repeat" , Expression , "times" , Block ;
ForStmt        = "for" , identifier , "in" , Expression , Block ;
FunctionStmt   = "function" , identifier , "(" , [ ParamList ] , ")" , Block ;

ParamList      = identifier , { "," , identifier } ;
ArgList        = Expression , { "," , Expression } ;

Expression     = LogicOr ;
LogicOr        = LogicAnd , { "or" , LogicAnd } ;
LogicAnd       = Equality , { "and" , Equality } ;
Equality       = Comparison , { ( "==" | "!=" ) , Comparison } ;
Comparison     = Term , { ( "<" | "<=" | ">" | ">=" ) , Term } ;
Term           = Factor , { ( "+" | "-" ) , Factor } ;
Factor         = Unary , { ( "*" | "/" | "%" ) , Unary } ;
Unary          = ( "-" | "not" ) , Unary | Postfix ;
Postfix        = Primary , { ( "(" , [ ArgList ] , ")" ) | ( "[" , Expression , "]" ) } ;

Primary        = number_literal
               | string_literal
               | "true"
               | "false"
               | "null"
               | identifier
               | ArrayLiteral
               | "(" , Expression , ")" ;

ArrayLiteral   = "[" , [ Expression , { "," , Expression } ] , "]" ;
```

---

## 4. Operator Precedence Table [IMPLEMENTED]

| Precedence | Operator | Description | Associativity |
|---|---|---|---|
| 1 | `or` | Logical OR (short-circuit) | Left |
| 2 | `and` | Logical AND (short-circuit) | Left |
| 3 | `==`, `!=` | Equality | Left |
| 4 | `<`, `<=`, `>`, `>=` | Relational | Left |
| 5 | `+`, `-` | Addition, Subtraction, String Concatenation | Left |
| 6 | `*`, `/`, `%` | Multiplication, Division, Modulo | Left |
| 7 | `-` (prefix), `not` | Unary negation, Logical NOT | Right |
| 8 | `()` (call), `[]` (index) | Function Call, Subscript Indexing | Left |
| 9 | Literals, `[]`, `(expr)` | Array Literals, Grouping, Primaries | N/A |
