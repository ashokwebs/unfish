#ifndef UF_EMIT_C_H
#define UF_EMIT_C_H

#include "../ast/uf_ast.h"
#include <stdio.h>
#include <stdbool.h>

/* Emits formatted C99 source code from an AST program */
bool uf_emit_c_program(const UfProgram* program, FILE* out);

/* Emits C99 source code directly to a destination file */
bool uf_emit_c_to_file(const UfProgram* program, const char* out_c_path);

/* Emits C99 and compiles with gcc to a standalone native ELF binary */
bool uf_build_native(const UfProgram* program, const char* out_bin_path);

#endif /* UF_EMIT_C_H */
