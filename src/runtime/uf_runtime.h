#ifndef UF_RUNTIME_H
#define UF_RUNTIME_H

#include "../common/uf_common.h"
#include "../common/uf_source.h"
#include "../common/uf_diagnostic.h"
#include "uf_object.h"
#include "uf_value.h"
#include "uf_env.h"

#include <setjmp.h>

#define UF_MAX_CALL_FRAMES 512
#define UF_MAX_TEMP_ROOTS 4096
#define UF_MAX_TRY_HANDLERS 64
#define UF_GC_INITIAL_THRESHOLD (64 * 1024) /* 64 KB */

typedef struct {
    const char* fn_name;
    SourceSpan call_span;
    UfEnv* env;
    UfEnv* caller_env;
} UfCallFrame;

typedef struct {
    jmp_buf jmp;
    UfEnv* scope_env;
    size_t frame_count;
    size_t temp_root_count;
} UfTryHandler;

typedef UfValue (*UfCallValueFn)(UfRuntime* rt, UfValue callee, size_t argc, UfValue* args, SourceSpan span);

typedef enum {
    UF_DEBUG_EVENT_STEP,
    UF_DEBUG_EVENT_CALL_ENTER,
    UF_DEBUG_EVENT_CALL_EXIT,
    UF_DEBUG_EVENT_VAR_BIND,
    UF_DEBUG_EVENT_VAR_ASSIGN,
    UF_DEBUG_EVENT_ERROR,
    UF_DEBUG_EVENT_GC_START,
    UF_DEBUG_EVENT_GC_END
} UfDebugEventType;

typedef struct {
    UfDebugEventType type;
    SourceSpan span;
    const char* fn_name;
    const char* var_name;
    UfValue val;
    size_t gc_bytes;
} UfDebugEvent;

typedef void (*UfDebugHook)(UfRuntime* rt, const UfDebugEvent* event, void* user_ctx);

struct UfRuntime {
    UfEnv* global_env;
    UfEnv* current_env;
    UfObj* all_objects;

    UfValue temp_roots[UF_MAX_TEMP_ROOTS];
    size_t temp_root_count;

    size_t bytes_allocated;
    size_t next_gc_threshold;
    size_t gc_count;
    /* While non-zero, allocation never triggers a collection (see
     * uf_gc_pause). */
    size_t gc_pause_depth;

    UfCallFrame frames[UF_MAX_CALL_FRAMES];
    size_t frame_count;

    UfTryHandler try_handlers[UF_MAX_TRY_HANDLERS];
    size_t try_handler_count;
    UfValue current_error;

    struct UfModuleEntry* module_cache;

    uint64_t step_count;
    uint64_t max_steps;

    bool had_runtime_error;

    UfCallValueFn call_fn;
    void* active_vm;
    void* active_regvm;
    void* scheduler;

    FILE* out_stream;
    FILE* err_stream;
    UfDiagnosticReporter* reporter;

    UfDebugHook debug_hook;
    void* debug_user_ctx;

    struct UfProfiler* profiler;

    int argc;
    char** argv;
};

void uf_runtime_emit_debug(UfRuntime* rt, const UfDebugEvent* event);

void uf_runtime_init(UfRuntime* rt, UfDiagnosticReporter* reporter);
void uf_runtime_set_args(UfRuntime* rt, int argc, char** argv);
void uf_runtime_free(UfRuntime* rt);

void uf_runtime_register_obj(UfRuntime* rt, UfObj* obj, size_t size);
void uf_runtime_push_temp_root(UfRuntime* rt, UfValue val);
void uf_runtime_pop_temp_root(UfRuntime* rt);
void uf_runtime_pop_temp_roots(UfRuntime* rt, size_t count);

void uf_gc_mark_value(UfValue val);
void uf_gc_mark_env(UfEnv* env);
void uf_gc_collect(UfRuntime* rt);

/* Suspend collection while building object graphs the GC cannot see yet,
 * such as a compiler's half-built functions or a function being loaded from
 * the bytecode cache. Calls nest and must be balanced. */
void uf_gc_pause(UfRuntime* rt);
void uf_gc_resume(UfRuntime* rt);

bool uf_runtime_push_frame(UfRuntime* rt, const char* fn_name, SourceSpan call_span, UfEnv* env);
void uf_runtime_pop_frame(UfRuntime* rt);

UfValue uf_runtime_call(UfRuntime* rt, UfValue callee, size_t argc, UfValue* args, SourceSpan span);

void uf_runtime_error(UfRuntime* rt, SourceSpan span, const char* fmt, ...);
void uf_runtime_raise(UfRuntime* rt, const char* kind, SourceSpan span, const char* fmt, ...);

#endif /* UF_RUNTIME_H */
