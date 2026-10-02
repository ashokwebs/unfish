#ifndef UF_COMPILER_H
#define UF_COMPILER_H

#include "uf_chunk.h"
#include "../ast/uf_ast.h"
#include "../runtime/uf_runtime.h"
#include "../common/uf_diagnostic.h"
#include <stdbool.h>

typedef struct {
    const char* name;
    int depth;
    bool is_captured;
} UfLocal;

typedef struct {
    uint8_t index;
    bool is_local;
} UfUpvalue;

typedef enum {
    TYPE_SCRIPT,
    TYPE_FUNCTION
} FunctionType;

typedef struct UfLoop {
    int start_ip;
    int scope_depth;
    int try_depth;
    int* break_jumps;
    size_t break_count;
    size_t break_capacity;
    /* `continue` cannot simply jump back to start_ip: for `while` that IS
     * the condition re-check (correct), but `for`/`repeat` re-check a
     * hidden index/counter that only advances in code emitted *after* the
     * loop body, at a point not yet known when a `continue` inside the
     * body is compiled. So `continue` always emits a forward jump here,
     * patched once that per-iteration "advance and re-check" point is
     * reached — which for `while` is immediately before its condition
     * re-check jump, same as start_ip. */
    int* continue_jumps;
    size_t continue_count;
    size_t continue_capacity;
    struct UfLoop* enclosing;
} UfLoop;

typedef struct UfCompiler {
    struct UfCompiler* enclosing;
    FunctionType type;
    const char* fn_name;
    size_t arity;
    size_t min_arity;
    bool has_rest;

    UfChunk* chunk;
    UfBytecodeFunction* function;

    UfLocal locals[256];
    int local_count;
    int scope_depth;
    int try_depth;

    UfUpvalue upvalues[256];
    int upvalue_count;

    UfLoop* current_loop;

    UfRuntime* rt;
    UfDiagnosticReporter* reporter;
    bool had_error;
    /* Line of the statement/expression currently being compiled, so that
     * internal-limit errors raised deep in helpers still point somewhere useful. */
    int current_line;
} UfCompiler;

/* Compile an AST Program into a top-level bytecode function.
 * Returns a pointer to UfBytecodeFunction on success, NULL on error. */
UfBytecodeFunction* uf_compile(const UfProgram* program, UfRuntime* rt, UfDiagnosticReporter* reporter);

#endif /* UF_COMPILER_H */
