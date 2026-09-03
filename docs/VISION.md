# UNFISH — VISION AND PHILOSOPHY

> "A learner can begin programming visually in Unfish, transition naturally into textual Unfish, and eventually use the same language to learn advanced programming, algorithms, runtime systems, compilers, operating systems, networking, and low-level computer concepts."

---

## 1. What Unfish Is

Unfish is a serious, general-purpose programming language and runtime system designed from first principles. It bridges the chasm between introductory visual programming (such as Scratch or Blockly) and rigorous, professional systems and application programming (such as C, Rust, or Python).

Unfish is **not** a toy DSL, **not** a wrapper around Python or JavaScript, and **not** a fake interpreter that pattern-matches command strings. It is a genuine language with:

1. A formally specified grammar and lexical model.
2. A lexer emitting detailed token streams with complete source metadata.
3. A deterministic Pratt / Recursive Descent parser generating a typed Abstract Syntax Tree (AST).
4. A semantic analysis pipeline performing lexical scope binding, symbol resolution, arity checking, and compile-time validation.
5. A robust runtime environment featuring first-class functions, lexical closures, dynamic/gradual value types, and deterministic memory semantics.
6. An extensible interpreter stepping stone toward a register/stack-based bytecode Virtual Machine (VM) and ahead-of-time (AOT) native compilation.

## 2. The Core Pedagogical Arc

Introductory computer science education is broken: students start in block-based environments with artificial restrictions, then experience a jarring cliff when thrown into industrial text languages with confusing syntax errors, cryptic compiler output, and uninspectable runtime behavior.

Unfish removes this cliff by providing an unbroken path of intellectual ascent:

```
Level 1: Visual Blocks (Scratch/Blockly-style visual frontend)
   ↓ (1:1 AST mapping with zero semantic loss)
Level 2: Beginner Textual Code (clean, indentation-based syntax: 'say', simple expressions)
   ↓
Level 3: Structured Programming (lexical variables, conditionals, loops, functions)
   ↓
Level 4: Data Structures & Algorithms (arrays, maps, recursion, higher-order functions)
   ↓
Level 5: Software Engineering (modules, error handling, unit testing, deterministic interfaces)
   ↓
Level 6: Runtime Systems (inspecting the environment, stack frames, heap allocation, GC)
   ↓
Level 7: Virtual Machines & Compilers (inspecting the AST, IR, bytecode, VM dispatch loops)
   ↓
Level 8: Low-Level & Systems Concepts (pointers, memory layout, C FFI, operating system primitives)
```

At every level, the learner is writing the **exact same language**, inspecting the **exact same runtime**, using tools that progressively reveal reality rather than concealing it behind magic.

## 3. Guiding Principles

### 3.1. Language First, Blocks Second
The language semantics and AST are the ground truth. Visual blocks are a projection of the AST; textual syntax is a projection of the AST. Round-tripping between visual blocks and text code must be lossless and semantically identical.

### 3.2. Do Not Infantilize the Learner
We do not use cartoonish metaphors or fake difficulty. We introduce real computer science terminology as concepts arise:
* "This is a variable."
* "This is a lexical binding within an environment frame."
* "Let's inspect the activation record on the call stack."
* "Here is the memory layout of this string on the heap."

### 3.3. Zero-Magic, Clean C Implementation
The core runtime and compiler are written in clean, modern ANSI C99/C11. The implementation is deliberately kept readable and free of massive third-party dependencies so that an advanced learner can open `src/runtime/`, `src/parser/`, or `src/lexer/` and study how a real programming language works.

### 3.4. Rigorous Error Model and Diagnostics
Beginners quit because error messages are unintelligible. Unfish treats diagnostics as a first-class user interface: every error highlights the exact source location, displays context, explains *why* the error occurred, and suggests concrete remediations.

### 3.5. Ten-Year Engineering Standard
We do not optimize for a shallow weekend hackathon demo. We engineer Unfish with memory safety sanitizers (ASan, UBSan), unit tests, conformance test suites, fuzz testing, and formal documentation to serve as a durable platform for years to come.
