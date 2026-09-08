# UNFISH — FORMAL LANGUAGE SPECIFICATION

**Version:** 1.7.0  
**Status:** Canonical Reference

---

## 1. Feature Status Inventory

| Feature | Status | Implementation Reference |
|---|---|---|
| UTF-8 Source Encoding | **IMPLEMENTED** | `src/lexer/uf_lexer.c` |
| Comments (`# ...`) | **IMPLEMENTED** | `src/lexer/uf_lexer.c` |
| Indentation (Off-Side Rule) | **IMPLEMENTED** | `src/lexer/uf_lexer.c` (`INDENT`, `DEDENT`, `NEWLINE`) |
| Identifiers & Keywords | **IMPLEMENTED** | `src/lexer/uf_token.c` |
| Numeric Literals (Integers, Floats, Scientific) | **IMPLEMENTED** | `src/lexer/uf_lexer.c` (64-bit IEEE 754) |
| String Literals (`"..."` with `\n`, `\t`, `\"`, `\\`, `\uXXXX`) | **IMPLEMENTED** | `src/lexer/uf_lexer.c` |
| Multi-line Strings (`"""..."""`) | **IMPLEMENTED** | `src/lexer/uf_lexer.c` |
| String Interpolation (`f"Hello {name}"`) | **IMPLEMENTED** | `src/lexer/uf_lexer.c`, `src/parser/uf_parser.c` |
| Boolean Literals (`true`, `false`) | **IMPLEMENTED** | `src/lexer/uf_token.c` |
| Null Literal (`null`) | **IMPLEMENTED** | `src/lexer/uf_token.c` |
| Arithmetic Ops (`+`, `-`, `*`, `/`, `%`) | **IMPLEMENTED** | `src/interpreter/uf_interpreter.c`, `src/vm/uf_vm.c`, `src/codegen/uf_emit_c.c` |
| Equality & Relational Ops (`==`, `!=`, `<`, `<=`, `>`, `>=`) | **IMPLEMENTED** | `src/interpreter/uf_interpreter.c`, `src/vm/uf_vm.c`, `src/codegen/uf_emit_c.c` |
| Logical Operators (`and`, `or`, `not` with short-circuit) | **IMPLEMENTED** | `src/interpreter/uf_interpreter.c`, `src/vm/uf_vm.c`, `src/codegen/uf_emit_c.c` |
| Variable Declarations (`let x = ...`, `let x: T = ...`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/semantic/uf_semantic.c` |
| Variable Assignment (`x = ...`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| Destructuring (`let [a, b] = arr`, `let {x, y} = obj`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c`, `src/codegen/uf_emit_c.c` |
| Output Statement (`say <expr>`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| Conditionals (`if / else / else if`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| While Loops (`while <cond>:`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| Repeat Loops (`repeat <n> times:`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| For-In Loops (`for x in arr:`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| Functions & Lexical Closures | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/runtime/uf_runtime.c` |
| Function Hoisting (Top-level & Mutually Recursive) | **IMPLEMENTED** | `src/semantic/uf_semantic.c`, `src/interpreter/uf_interpreter.c` |
| Default Parameters (`fn greet(name="World"):`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/runtime/uf_runtime.c` |
| Return Statements (`return [<expr>]`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c` |
| Exception Handling (`try / catch / finally`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c`, `src/vm/uf_vm.c` |
| Arrays & Indexing (`[1, 2, 3]`, `arr[0]`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/runtime/uf_value.c` |
| Hash Maps (`{"k": v}`, `m.k`, `m["k"]`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/runtime/uf_value.c` |
| Modules & Imports (`import m`, `from m import a, b`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/runtime/uf_module.c` |
| Gradual Type Annotations (`x: Number`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/semantic/uf_semantic.c` |
| Struct Declarations & Methods (`struct Point: fn dist(self):`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/runtime/uf_value.c`, `src/codegen/uf_emit_c.c` |
| Enums & Sum Types / ADTs (`enum Result: Ok(val), Err(msg)`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/runtime/uf_value.c`, `src/codegen/uf_emit_c.c` |
| Pattern Matching (`match val: when pat: ...`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/interpreter/uf_interpreter.c`, `src/compiler/uf_compiler.c` |
| Cooperative Concurrency (`spawn`, `yield`, `channel`) | **IMPLEMENTED** | `src/runtime/uf_fiber.c`, `src/runtime/uf_stdlib.c`, `src/codegen/unfish_runtime.h` |
| Async / Await & Promises (`async function`, `await`, `run_async`) | **IMPLEMENTED** | `src/runtime/uf_fiber.c`, `src/interpreter/uf_interpreter.c`, `src/vm/uf_vm.c`, `src/vm2/uf_regvm.c`, `src/codegen/uf_emit_c.c` |
| Systems & Byte Buffers (`buffer`, `u16`, `u32`, bitwise) | **IMPLEMENTED** | `src/runtime/uf_stdlib.c`, `src/codegen/unfish_runtime.h` |
| Spread / Rest Operator (`...args`, `...arr`, `...map`) | **IMPLEMENTED** | `src/parser/uf_parser.c`, `src/compiler/uf_compiler.c`, `src/interpreter/uf_interpreter.c`, `src/codegen/uf_emit_c.c` |
| Standard Library Built-ins & Modules | **IMPLEMENTED** | `src/stdlib/uf_mod_sys.c`, `uf_mod_fs.c`, `uf_mod_random.c`, `uf_mod_time.c`, `uf_mod_json.c` |

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
and         as          break       catch       continue
else        enum        false       fn          for
from        function    if          import      in
let         match       not         null        or
repeat      return      say         struct      times
true        try         when        while
```

### 2.7. Literals [IMPLEMENTED]
* **Number**: decimal integers or floats, with an optional scientific/exponent suffix (e.g., `0`, `42`, `3.14159`, `1e10`, `6.022e23`, `1.5e-10`, `2.5E+3`).
* **String**: delimited by `"..."` supporting `\n`, `\t`, `\"`, `\\`, and `\uXXXX` unicode code points.
* **Multi-line String**: delimited by `"""..."""` preserving internal newlines and indentation.
* **Format String**: `f"..."` supporting embedded `{expression}` interpolation.
* **Boolean**: `true`, `false`.
* **Null**: `null`.

### 2.8. Destructuring [IMPLEMENTED]
Destructuring allows binding variables to elements of arrays or properties of structs/maps.
- **Array Destructuring**: `let [x, y] = array`
- **Map/Struct Destructuring**: `let {a, b} = object`
- **Rest Patterns**: `let [head, ...tail] = array`
- **Wildcard Patterns**: `let [x, _, z] = array`
- **Nested Patterns**: `let [[a, b], c] = [[1, 2], 3]`
- **Match Integration**: `when [1, ...rest]:`
- **Assignment**: `[a, b] = [b, a]`

### 2.9. Enums & Sum Types [IMPLEMENTED]
Enums declare enumerated types or algebraic sum types (tagged unions):
- **Inline Syntax**: `enum Color: Red, Green, Blue`
- **Sum Types with Payloads**:
  ```unfish
  enum Result:
      Ok(value)
      Err(message)
  ```
- **Namespaced & Bare Construction**: `Color.Red`, `Result.Ok(42)`, `Red`, `Ok(42)`
- **Pattern Matching**:
  ```unfish
  match r:
      when Ok(val):
          say val
      when Err(msg):
          say "Error: " + msg
  ```
- **Introspection & Indexing**: `.tag`, `.name`, named payload properties (`r.value`), and numeric index access (`r[0]`).
- **Structural Equality**: Deep recursive equality comparing definition identity, variant tag, and payload values.

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
               | TryCatchStmt
               | StructStmt
               | EnumStmt
               | MatchStmt
               | ImportStmt
               | FromImportStmt
               | ExprStmt ;

Block          = ":" , NEWLINE , INDENT , { Statement | NEWLINE } , DEDENT ;

LetStmt        = "let" , ( identifier | DestructurePattern ) , [ ":" , TypeAnnotation ] , [ "=" , Expression ] , NEWLINE ;
DestructurePattern = ArrayPattern | MapPattern ;
ArrayPattern   = "[" , [ PatternElem , { "," , PatternElem } ] , "]" ;
MapPattern     = "{" , [ MapPatternElem , { "," , MapPatternElem } ] , "}" ;
PatternElem    = identifier | "_" | ( "..." , identifier ) | DestructurePattern ;
MapPatternElem = identifier | ( "..." , identifier ) ;
AssignStmt     = ( identifier | DestructurePattern ) , "=" , Expression , NEWLINE ;
IndexAssignStmt= Expression , "[" , Expression , "]" , "=" , Expression , NEWLINE ;
SayStmt        = "say" , Expression , NEWLINE ;
BreakStmt      = "break" , NEWLINE ;
ContinueStmt   = "continue" , NEWLINE ;
ReturnStmt     = "return" , [ Expression ] , NEWLINE ;
ImportStmt     = "import" , identifier , [ "as" , identifier ] , NEWLINE ;
FromImportStmt = "from" , identifier , "import" , ImportSpecList , NEWLINE ;
ImportSpecList = ImportSpec , { "," , ImportSpec } ;
ImportSpec     = identifier , [ "as" , identifier ] ;
ExprStmt       = Expression , NEWLINE ;

StructStmt     = "struct" , identifier , ":" , NEWLINE , INDENT , { StructMember } , DEDENT ;
StructMember   = StructField | MethodDecl ;
StructField    = identifier , [ ":" , TypeAnnotation ] , NEWLINE ;
MethodDecl     = ( "function" | "fn" ) , identifier , "(" , "self" , [ "," , ParamList ] , ")" , [ ":" , TypeAnnotation ] , Block ;
EnumStmt       = "enum" , identifier , ":" , ( InlineVariants | BlockVariants ) ;
InlineVariants = EnumVariant , { "," , EnumVariant } , NEWLINE ;
BlockVariants  = NEWLINE , INDENT , { EnumVariant , NEWLINE } , DEDENT ;
EnumVariant    = identifier , [ "(" , [ VariantFieldList ] , ")" ] ;
VariantFieldList = VariantField , { "," , VariantField } ;
VariantField   = identifier , [ ":" , TypeAnnotation ] ;
MatchStmt      = "match" , Expression , ":" , NEWLINE , INDENT , { MatchArm } , DEDENT ;
MatchArm       = "when" , Expression , Block ;

IfStmt         = "if" , Expression , Block , [ "else" , ( Block | IfStmt ) ] ;
WhileStmt      = "while" , Expression , Block ;
RepeatStmt     = "repeat" , Expression , "times" , Block ;
ForStmt        = "for" , identifier , "in" , Expression , Block ;
FunctionStmt   = ( "function" | "fn" ) , identifier , "(" , [ ParamList ] , ")" , [ ":" , TypeAnnotation ] , Block ;
TryCatchStmt   = "try" , Block , "catch" , identifier , Block ;

ParamList      = ( Param , { "," , Param } , [ "," , RestParam ] ) | RestParam ;
Param          = identifier , [ ":" , TypeAnnotation ] , [ "=" , Expression ] ;
RestParam      = "..." , identifier , [ ":" , TypeAnnotation ] ;
TypeAnnotation = identifier ;
ArgList        = Argument , { "," , Argument } ;
Argument       = Expression | SpreadExpr ;
SpreadExpr     = "..." , Expression ;

Expression     = LogicOr ;
LogicOr        = LogicAnd , { "or" , LogicAnd } ;
LogicAnd       = Equality , { "and" , Equality } ;
Equality       = Comparison , { ( "==" | "!=" ) , Comparison } ;
Comparison     = Term , { ( "<" | "<=" | ">" | ">=" ) , Term } ;
Term           = Factor , { ( "+" | "-" ) , Factor } ;
Factor         = Unary , { ( "*" | "/" | "%" ) , Unary } ;
Unary          = ( "-" | "not" | "..." | "await" ) , Unary | Postfix ;
Postfix        = Primary , { ( "(" , [ ArgList ] , ")" ) | ( "[" , Expression , "]" ) | ( "." , identifier ) } ;

Primary        = number_literal
               | string_literal
               | "true"
               | "false"
               | "null"
               | identifier
               | ArrayLiteral
               | MapLiteral
               | FunctionExpr
               | "(" , Expression , ")" ;

ArrayLiteral   = "[" , [ ArrayElement , { "," , ArrayElement } ] , [ "," ] , "]" ;
ArrayElement   = Expression | SpreadExpr ;
MapLiteral     = "{" , [ MapEntry , { "," , MapEntry } ] , [ "," ] , "}" ;
MapEntry       = ( ( identifier | Expression ) , ":" , Expression ) | SpreadExpr ;
FunctionExpr   = [ "async" ] , "function" , [ identifier ] , "(" , [ ParamList ] , ")" , ":" , ( Block | Expression | ReturnStmt ) ;
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
| 7 | `-` (prefix), `not`, `...` (spread), `await` | Unary negation, Logical NOT, Spread, Await | Right |
| 8 | `()` (call), `[]` (index), `.` (dot) | Function Call, Subscript Indexing, Property Access | Left |
| 9 | Literals, `[]`, `{}`, `(expr)` | Array/Map Literals, Grouping, Primaries | N/A |
