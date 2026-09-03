# UNFISH — KNOWN ISSUES & LIMITATIONS

**Last Updated:** Milestone 3 Complete (v0.3.0-alpha / v0.4.0-alpha)

---

## 1. Active Limitations

1. **Tree-Walking Execution Only**: Currently targets an AST evaluator. Bytecode VM compilation is slated for Phase 7 (ISA designed in ADR 013).
2. **Dynamic Typing Only**: Gradual type annotations are recognized in grammar design but not yet type-checked at compile time (Phase 4).
3. **No Key-Value Hash Maps Yet**: Scheduled for Phase 3 Part 2 (arrays and iteration are fully implemented).
4. **Single-Threaded Execution**: Concurrency and asynchronous constructs are deferred to Phase 10.

## 2. Tracked Bugs
* None currently known (greenfield codebase).

## 3. Edge Cases Under Active Testing
* Mixed tabs and spaces in indentation (strict rejection enforced).
* Nested functions with variable shadowing across 3+ lexical scope levels.
* Mutual recursion call stack unwinding.
