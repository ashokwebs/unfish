#include "uf_runtime.h"
#include <stdarg.h>
#include <time.h>

/* --- Built-in Native Functions --- */

static UfValue native_say(UfRuntime* rt, int argc, UfValue* args) {
    if (argc > 0) {
        char* str = uf_val_to_string(args[0]);
        fprintf(rt->out_stream, "%s\n", str);
        free(str);
    } else {
        fprintf(rt->out_stream, "\n");
    }
    fflush(rt->out_stream);
    return uf_val_null();
}

static UfValue native_print(UfRuntime* rt, int argc, UfValue* args) {
    if (argc > 0) {
        char* str = uf_val_to_string(args[0]);
        fprintf(rt->out_stream, "%s", str);
        free(str);
    }
    fflush(rt->out_stream);
    return uf_val_null();
}

static UfValue native_type_of(UfRuntime* rt, int argc, UfValue* args) {
    if (argc == 0) return uf_val_string_cstr(rt, "null");
    const char* type_name = uf_val_type_name(args[0]);
    return uf_val_string_cstr(rt, type_name);
}

static UfValue native_len(UfRuntime* rt, int argc, UfValue* args) {
    if (argc == 0 || args[0].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'len()' argument must be a string");
        return uf_val_null();
    }
    return uf_val_number((double)args[0].as.string->length);
}

static UfValue native_clock(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    UF_UNUSED(argc);
    UF_UNUSED(args);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double sec = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    return uf_val_number(sec);
}

static UfValue native_assert(UfRuntime* rt, int argc, UfValue* args) {
    if (argc == 0 || !uf_val_is_truthy(args[0])) {
        const char* msg = "Assertion failed";
        char* custom_msg = NULL;
        if (argc >= 2) {
            custom_msg = uf_val_to_string(args[1]);
            msg = custom_msg;
        }
        SourceLoc loc = source_loc_make("<assert>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "%s", msg);
        if (custom_msg) free(custom_msg);
    }
    return uf_val_null();
}

static void register_builtins(UfRuntime* rt) {
    uf_env_declare(rt->global_env, "say",     uf_val_native("say",     native_say,     1));
    uf_env_declare(rt->global_env, "print",   uf_val_native("print",   native_print,   1));
    uf_env_declare(rt->global_env, "type_of", uf_val_native("type_of", native_type_of, 1));
    uf_env_declare(rt->global_env, "len",     uf_val_native("len",     native_len,     1));
    uf_env_declare(rt->global_env, "clock",   uf_val_native("clock",   native_clock,   0));
    uf_env_declare(rt->global_env, "assert",  uf_val_native("assert",  native_assert,  -1));
}

void uf_runtime_register_obj(UfRuntime* rt, UfObj* obj) {
    obj->next = rt->all_objects;
    rt->all_objects = obj;
}

void uf_gc_mark_value(UfValue val) {
    if (val.kind == UF_VAL_STRING) {
        if (val.as.string) val.as.string->obj.marked = true;
    } else if (val.kind == UF_VAL_FUNCTION) {
        if (val.as.function && !val.as.function->obj.marked) {
            val.as.function->obj.marked = true;
            uf_gc_mark_env(val.as.function->closure_env);
        }
    }
}

void uf_gc_mark_env(UfEnv* env) {
    if (!env || env->obj.marked) return;

    env->obj.marked = true;
    uf_gc_mark_env(env->parent);

    for (size_t i = 0; i < env->bucket_count; ++i) {
        UfEnvBinding* b = env->buckets[i];
        while (b) {
            uf_gc_mark_value(b->value);
            b = b->next;
        }
    }
}

void uf_gc_collect(UfRuntime* rt) {
    /* Mark roots */
    uf_gc_mark_env(rt->global_env);
    for (size_t i = 0; i < rt->frame_count; ++i) {
        uf_gc_mark_env(rt->frames[i].env);
    }

    /* Sweep */
    UfObj** curr = &rt->all_objects;
    while (*curr) {
        UfObj* obj = *curr;
        if (obj->marked) {
            obj->marked = false;
            curr = &obj->next;
        } else {
            *curr = obj->next;
            if (obj->kind == UF_OBJ_ENV) {
                uf_env_free((UfEnv*)obj);
            } else {
                free(obj);
            }
        }
    }
}

void uf_runtime_init(UfRuntime* rt, UfDiagnosticReporter* reporter) {
    rt->all_objects = NULL;
    rt->global_env = uf_env_create(rt, NULL);
    rt->frame_count = 0;
    rt->step_count = 0;
    rt->max_steps = 10000000;
    rt->had_runtime_error = false;
    rt->out_stream = stdout;
    rt->err_stream = stderr;
    rt->reporter = reporter;

    register_builtins(rt);
}

void uf_runtime_free(UfRuntime* rt) {
    rt->global_env = NULL;

    UfObj* obj = rt->all_objects;
    while (obj) {
        UfObj* next = obj->next;
        if (obj->kind == UF_OBJ_ENV) {
            uf_env_free((UfEnv*)obj);
        } else {
            free(obj);
        }
        obj = next;
    }
    rt->all_objects = NULL;
    rt->frame_count = 0;
}

bool uf_runtime_push_frame(UfRuntime* rt, const char* fn_name, SourceSpan call_span, UfEnv* env) {
    if (rt->frame_count >= UF_MAX_CALL_FRAMES) {
        uf_runtime_error(rt, call_span, "StackOverflowError: Maximum call stack depth exceeded (%d frames)", UF_MAX_CALL_FRAMES);
        return false;
    }
    rt->frames[rt->frame_count].fn_name = fn_name;
    rt->frames[rt->frame_count].call_span = call_span;
    rt->frames[rt->frame_count].env = env;
    rt->frame_count++;
    return true;
}

void uf_runtime_pop_frame(UfRuntime* rt) {
    if (rt->frame_count > 0) {
        rt->frame_count--;
    }
}

void uf_runtime_error(UfRuntime* rt, SourceSpan span, const char* fmt, ...) {
    rt->had_runtime_error = true;

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (rt->reporter) {
        uf_report_diag(rt->reporter, UF_DIAG_RUNTIME_ERROR, span, buffer, NULL);
    } else {
        fprintf(rt->err_stream, "Runtime Error: %s\n", buffer);
    }

    if (rt->frame_count > 0) {
        fprintf(rt->err_stream, "Traceback (most recent call first):\n");
        for (int i = (int)rt->frame_count - 1; i >= 0; --i) {
            fprintf(rt->err_stream, "  frame %d: %s() at %s:%u\n",
                    i, rt->frames[i].fn_name,
                    rt->frames[i].call_span.start.file ? rt->frames[i].call_span.start.file : "<unknown>",
                    rt->frames[i].call_span.start.line);
        }
        fprintf(rt->err_stream, "\n");
    }
}
