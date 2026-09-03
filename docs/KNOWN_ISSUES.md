# UNFISH — KNOWN ISSUES & LIMITATIONS

**Last Updated:** Phase 0 / Phase 1 Initial Build

---

## 1. Active Limitations (By Design in Phase 1)

1. **Tree-Walking Execution Only**: Phase 1 targets an AST evaluator. Bytecode VM compilation is slated for Phase 7.
2. **Dynamic Typing Only**: Gradual type annotations are recognized in grammar design but not yet type-checked at compile time.
3. **No Dynamic Arrays / Maps Yet**: Collections are scheduled for Phase 3.
4. **Single-Threaded Execution**: Concurrency and asynchronous constructs are deferred to Phase 10.

## 2. Tracked Bugs
* None currently known (greenfield codebase).

## 3. Edge Cases Under Active Testing
* Mixed tabs and spaces in indentation (strict rejection enforced).
* Nested functions with variable shadowing across 3+ lexical scope levels.
* Mutual recursion call stack unwinding.
