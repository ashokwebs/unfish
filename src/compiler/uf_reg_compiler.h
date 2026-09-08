#ifndef UF_REG_COMPILER_H
#define UF_REG_COMPILER_H

#include "../vm2/uf_regvm.h"
#include "../ast/uf_ast.h"
#include "../runtime/uf_runtime.h"
#include "../common/uf_diagnostic.h"
#include <stdbool.h>

/* Compile an AST Program into a top-level register bytecode function.
 * Returns pointer to UfRegFunction on success, NULL on error. */
UfRegFunction* uf_reg_compile(const UfProgram* program, UfRuntime* rt, UfDiagnosticReporter* reporter);

#endif /* UF_REG_COMPILER_H */
