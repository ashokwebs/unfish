#ifndef UF_OPTIMIZE_H
#define UF_OPTIMIZE_H

#include "../ast/uf_ast.h"
#include "../runtime/uf_runtime.h"
#include "uf_chunk.h"

/* Optimizes AST expressions with constant folding and dead code elimination */
void uf_optimize_ast(UfProgram* program, UfRuntime* rt);

/* Peephole bytecode optimization pass on a chunk */
void uf_optimize_chunk(UfChunk* chunk, UfRuntime* rt);

/* Recursively optimizes a bytecode function tree */
void uf_optimize_function_tree(UfBytecodeFunction* fn, UfRuntime* rt);

#endif /* UF_OPTIMIZE_H */
