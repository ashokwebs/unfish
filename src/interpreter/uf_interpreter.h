#ifndef UF_INTERPRETER_H
#define UF_INTERPRETER_H

#include "../common/uf_common.h"
#include "../ast/uf_ast.h"
#include "../runtime/uf_runtime.h"

typedef enum {
    UF_INTERPRET_OK,
    UF_INTERPRET_RUNTIME_ERROR
} UfInterpretResult;

UfInterpretResult uf_interpret_program(UfRuntime* rt, const UfProgram* program);
UfValue uf_evaluate_expression(UfRuntime* rt, UfEnv* env, const UfExpr* expr);
UfValue uf_call_value(UfRuntime* rt, UfValue callee, size_t argc, UfValue* args, SourceSpan span);

#endif /* UF_INTERPRETER_H */
