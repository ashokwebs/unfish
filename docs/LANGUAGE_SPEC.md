# UNFISH — FORMAL LANGUAGE SPECIFICATION

**Version:** 0.1.0-draft  
**Status:** Canonical Reference

---

## 1. Lexical Grammar

### 1.1. Character Set & Encoding
Source files must be encoded in UTF-8.

### 1.2. Whitespace & Newlines
* **Horizontal Whitespace**: Space (`U+0020`) and horizontal tab (`U+0009`). Spaces are standard (4 spaces per indentation level recommended). Mixed tabs and spaces on the same indentation line are disallowed.
* **Line Terminators**: LF (`\n`, `U+000A`) or CRLF (`\r\n`).
* **Blank Lines**: Lines containing only whitespace and/or comments are ignored and do not produce `NEWLINE` tokens.

### 1.3. Comments
Comments begin with a hash character `#` and extend to the end of the current physical line:
```unfish
# This is a comment
let x = 42 # Inline comment
```

### 1.4. Indentation (Off-Side Rule)
Blocks of code are delimited by indentation changes. The lexer maintains an internal indentation stack:
* At the beginning of each non-blank logical line, the lexer measures the column indentation.
* If indentation is greater than the stack top: push current level, emit `INDENT`.
* If indentation is equal to the stack top: emit no indentation token.
* If indentation is less than the stack top: pop levels until a match is found. For each popped level, emit `DEDENT`. If the current indentation matches no previous level on the stack, an `IndentationError` is raised.
* At the end of input (EOF), the lexer emits a `NEWLINE` (if the file did not terminate with one) followed by `DEDENT` tokens for every remaining level on the stack until level 0 is reached.

### 1.5. Identifiers
An identifier begins with an ASCII letter (`a-z`, `A-Z`) or an underscore (`_`), followed by any number of ASCII letters, digits (`0-9`), or underscores:
```ebnf
letter        = "a" ... "z" | "A" ... "Z" | "_" ;
digit         = "0" ... "9" ;
identifier    = letter , { letter | digit } ;
```

### 1.6. Keywords
The following identifiers are reserved keywords:
```
and         break       continue    else        false
function    if          let         not         null
or          repeat      return      say         times
true        while
```

### 1.7. Literals

#### Numeric Literals
Numbers may be decimal integers or floating-point:
```ebnf
number_literal = digit , { digit } , [ "." , digit , { digit } ] ;
```
Examples: `0`, `42`, `3.14159`, `0.005`.

#### String Literals
Strings are delimited by double quotes `"..."` and support the following escape sequences:
* `\n`: newline (ASCII 10)
* `\t`: tab (ASCII 9)
* `\\`: backslash
* `\"`: double quote
```ebnf
escape_seq     = "\" , ( "n" | "t" | "\" | '"' ) ;
string_char    = ? any UTF-8 character except '"' or '\' or newline ? | escape_seq ;
string_literal = '"' , { string_char } , '"' ;
```

#### Boolean Literals
`true` and `false`.

#### Null Literal
`null`.

---

## 2. Syntactic Grammar (EBNF)

```ebnf
Program        = { Statement | NEWLINE } , EOF ;

Statement      = LetStmt
               | AssignStmt
               | SayStmt
               | IfStmt
               | WhileStmt
               | RepeatStmt
               | FunctionStmt
               | ReturnStmt
               | ExprStmt ;

Block          = ":" , NEWLINE , INDENT , { Statement | NEWLINE } , DEDENT ;

LetStmt        = "let" , identifier , [ "=" , Expression ] , NEWLINE ;
AssignStmt     = identifier , "=" , Expression , NEWLINE ;
SayStmt        = "say" , Expression , NEWLINE ;
ReturnStmt     = "return" , [ Expression ] , NEWLINE ;
ExprStmt       = Expression , NEWLINE ;

IfStmt         = "if" , Expression , Block , [ "else" , ( Block | IfStmt ) ] ;
WhileStmt      = "while" , Expression , Block ;
RepeatStmt     = "repeat" , Expression , "times" , Block ;
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
Unary          = ( "-" | "not" ) , Unary | Call ;
Call           = Primary , { "(" , [ ArgList ] , ")" } ;

Primary        = number_literal
               | string_literal
               | "true"
               | "false"
               | "null"
               | identifier
               | "(" , Expression , ")" ;
```

---

## 3. Operator Precedence & Associativity

From lowest precedence to highest:

| Precedence | Operator | Description | Associativity |
|---|---|---|---|
| 1 | `or` | Logical OR | Left |
| 2 | `and` | Logical AND | Left |
| 3 | `==`, `!=` | Equality | Non-associative / Left |
| 4 | `<`, `<=`, `>`, `>=` | Relational | Non-associative / Left |
| 5 | `+`, `-` | Addition, Subtraction, String Concatenation | Left |
| 6 | `*`, `/`, `%` | Multiplication, Division, Modulo | Left |
| 7 | `-` (prefix), `not` | Unary negation, Logical NOT | Right |
| 8 | `()` (call) | Function Call | Left |
| 9 | Literals, `(expr)` | Grouping, Primaries | N/A |

---

## 4. Evaluation Semantics

1. **Short-Circuit Evaluation**:
   * `a and b`: if `a` is falsy, returns `a` without evaluating `b`.
   * `a or b`: if `a` is truthy, returns `a` without evaluating `b`.
2. **Truthiness**:
   * Falsy values: `false`, `null`, `0`, `""`.
   * All other values are truthy.
3. **String Concatenation**:
   * The `+` operator performs string concatenation when either operand is a string. If one operand is not a string, it is automatically converted to its string representation.
   * If both operands are numbers, `+` performs arithmetic addition.
   * If operands are non-string, non-numeric combinations (e.g. `true + null`), a runtime `TypeError` is raised.
4. **Division**:
   * Division by zero produces a `DivisionByZeroError`.
5. **Scopes & Environments**:
   * `let x = ...` creates a binding in the current lexical environment.
   * A variable cannot be declared twice in the exact same scope (`let x = 1` followed by `let x = 2` in the same block produces a compile-time `SemanticError`).
   * Inner scopes may shadow variables from outer scopes.
   * Assignments `x = expr` update the nearest enclosing binding of `x`. If no binding exists, a `SemanticError` or `RuntimeError` is raised.
