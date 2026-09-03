#ifndef UF_FORMATTER_H
#define UF_FORMATTER_H

#include "uf_ast.h"
#include "uf_diagnostic.h"
#include <stdio.h>
#include <stdbool.h>

/* Format an entire AST program into a newly-allocated string.
 * The caller must free() the returned buffer. */
char* uf_format_program(const UfProgram* program);

/* Format an AST program directly into a stream (e.g. stdout or file). */
void uf_format_program_stream(const UfProgram* program, FILE* out);

/* Format a file in-place or check if it is canonically formatted.
 * If in_place is true, writes to file if modified.
 * Returns true if the file was already cleanly formatted (or successfully formatted). */
bool uf_format_file(const char* filename, bool in_place, bool check_only, UfDiagnosticReporter* reporter);

#endif /* UF_FORMATTER_H */
