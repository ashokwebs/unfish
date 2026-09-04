#ifndef UF_LSP_H
#define UF_LSP_H

#include <stdio.h>
#include <stdbool.h>

/* Runs the Language Server Protocol loop over input and output streams.
 * Returns 0 on clean exit. */
int uf_lsp_run(FILE* in, FILE* out);

#endif /* UF_LSP_H */
