#ifndef UF_DEBUGGER_H
#define UF_DEBUGGER_H

#include "../runtime/uf_runtime.h"
#include <stdio.h>
#include <stdbool.h>

/* Attach tracer: emits newline-delimited JSON events for execution visualization */
void uf_debugger_attach_tracer(UfRuntime* rt, FILE* out_stream);

/* Attach interactive command-line debugger */
void uf_debugger_attach_interactive(UfRuntime* rt, FILE* in_stream, FILE* out_stream);

/* Detach debugger hook and free any internal debugger state */
void uf_debugger_detach(UfRuntime* rt);

#endif /* UF_DEBUGGER_H */
