#ifndef UF_RUNTIME_H
#define UF_RUNTIME_H

#include "../common/uf_common.h"
#include "../common/uf_source.h"
#include "../common/uf_diagnostic.h"
#include "uf_object.h"
#include "uf_value.h"
#include "uf_env.h"

#define UF_MAX_CALL_FRAMES 512

typedef struct {
    const char* fn_name;
    SourceSpan call_span;
    UfEnv* env;
} UfCallFrame;

struct UfRuntime {
    UfEnv* global_env;
    UfObj* all_objects;

    UfCallFrame frames[UF_MAX_CALL_FRAMES];
    size_t frame_count;

    uint64_t step_count;
    uint64_t max_steps;

    bool had_runtime_error;

    FILE* out_stream;
    FILE* err_stream;
    UfDiagnosticReporter* reporter;
};

void uf_runtime_init(UfRuntime* rt, UfDiagnosticReporter* reporter);
void uf_runtime_free(UfRuntime* rt);

void uf_runtime_register_obj(UfRuntime* rt, UfObj* obj);
void uf_gc_mark_value(UfValue val);
void uf_gc_mark_env(UfEnv* env);
void uf_gc_collect(UfRuntime* rt);

bool uf_runtime_push_frame(UfRuntime* rt, const char* fn_name, SourceSpan call_span, UfEnv* env);
void uf_runtime_pop_frame(UfRuntime* rt);

void uf_runtime_error(UfRuntime* rt, SourceSpan span, const char* fmt, ...);

#endif /* UF_RUNTIME_H */
