#ifndef UF_BLOCKS_H
#define UF_BLOCKS_H

#include "../ast/uf_ast.h"
#include "../common/uf_arena.h"
#include "../common/uf_string.h"
#include "../common/uf_diagnostic.h"
#include <stdio.h>
#include <stdbool.h>

/* Export an AST Program to JSON blocks representation.
 * Returns dynamically allocated JSON string (caller frees). */
char* uf_blocks_export_string(const UfProgram* program);

/* Export an AST Program to a file or stream. */
void uf_blocks_export_stream(const UfProgram* program, FILE* out);

/* Import JSON blocks representation into an AST Program.
 * Allocates nodes in arena and interns strings with interner. */
UfProgram* uf_blocks_import_string(const char* json_str, UfArena* arena, UfInterner* interner, UfDiagnosticReporter* reporter);

#endif /* UF_BLOCKS_H */
