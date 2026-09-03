# UNFISH — CHANGELOG

All notable changes to the Unfish programming language will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.1.0-alpha] - 2026-09-03

### Added
- **Formal Documentation Suite**: 22 comprehensive technical documents in `docs/` covering language specification, type system, memory model, architecture, roadmap, error model, runtime, VM, compiler, and blocks.
- **Common Core Infrastructure**: Contiguous chunk memory arena (`uf_arena`), dynamic strings (`uf_string`), string interning (`uf_intern`), source coordinates & spans (`uf_source`), and diagnostics engine (`uf_diagnostic`).
- **Indentation Lexer**: Off-side rule scanner producing `INDENT`, `DEDENT`, and `NEWLINE` tokens with complete source spans, string escapes, number literals, and comments.
- **AST Architecture**: Strongly typed AST nodes with source span preservation (`uf_ast`) and S-expression tree pretty printer (`uf_ast_print`).
- **Pratt & Recursive Descent Parser**: Precedence-driven expression parsing with Pratt algorithm and indentation block parsing.
- **Semantic Analysis Pass**: Scope resolution, symbol tables, undefined identifier detection with Levenshtein fuzzy matching, duplicate declaration prevention, function arity checks, and return statement validation.
- **Runtime & Value System**: Tagged union `UfValue` representation, dynamic strings, first-class functions, lexical closures, and native C function bindings.
- **Garbage Collection**: Object-tracked mark-and-sweep garbage collection resolving closure cyclic dependencies with zero leaks.
- **Tree-Walking Interpreter**: AST evaluator supporting arithmetic, strings, conditionals (`if/else/elif`), loops (`while`, `repeat`), functions, recursion, and step execution quotas.
- **Standard Library Core**: Built-ins `say`, `print`, `type_of`, `len`, `clock`, and `assert`.
- **Command-Line Interface (`unfish`)**: Full CLI supporting `run`, `check`, `ast`, `tokens`, `repl`, and `version`.
- **Automated Verification**: C unit test binaries and automated conformance test runner verified under AddressSanitizer and UndefinedBehaviorSanitizer.
