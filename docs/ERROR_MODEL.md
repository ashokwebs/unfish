# UNFISH — ERROR MODEL & DIAGNOSTICS SPECIFICATION

---

## 1. Design Philosophy

Error messages are often the primary user interface for a student learning to program. Unfish rejects opaque error messages like `Segmentation fault` or `Syntax error at line 4: parse error`.

Instead, Unfish diagnostics adhere to four rules:
1. **Precise Classification**: Is this a Syntax Error, Semantic Error, or Runtime Error?
2. **Visual Context**: Render the exact line of code with a column-accurate caret indicator (`^~~~~`).
3. **Plain English Explanation**: Describe clearly what went wrong without jargon overload.
4. **Actionable Remediation**: Provide an educational "Hint" or "Did you mean?" suggestion.

## 2. Error Categories

```
UfError
├── LexicalError      (unexpected character, unterminated string, invalid number)
├── SyntaxError       (unexpected token, missing colon, mismatched indentation)
├── SemanticError     (undefined variable, duplicate declaration, return outside function)
└── RuntimeError      (type mismatch, division by zero, stack overflow, assertion failed)
```

## 3. Diagnostic Format Specification

When an error occurs, the diagnostics engine formats output according to this schema:

```
[Error Category]: [Concise description]
  --> [file_path]:[line]:[column]
   |
 8 |     say score
   |         ^^^^^
   |
Hint: 'score' is not defined. Did you mean 'scores'?
```

For runtime errors occurring inside nested functions:
```
RuntimeError: Division by zero
  --> math_utils.unfish:14:12
   |
14 |     return numerator / denominator
   |            ^^^^^^^^^^^^^^^^^^^^^^^
Traceback (most recent call first):
  frame 2: divide(10, 0) at math_utils.unfish:14
  frame 1: compute_average(data) at app.unfish:25
  frame 0: <main> at app.unfish:40
```

## 4. Educational Leveled Diagnostics

In future IDE and CLI modes, students can adjust diagnostic verbosity:
* **Beginner Mode**: Highlights common mental model misconceptions (e.g. assigning `=` vs comparing `==`).
* **Advanced / Systems Mode**: Displays underlying runtime details, AST node types, and bytecode offsets.
