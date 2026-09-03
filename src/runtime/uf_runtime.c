#include "uf_runtime.h"
#include "uf_stdlib.h"
#include "uf_module.h"
#include <stdarg.h>
#include <time.h>
#include <math.h>

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
    if (argc == 0) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'len()' expects 1 argument");
        return uf_val_null();
    }
    if (args[0].kind == UF_VAL_STRING) {
        return uf_val_number((double)args[0].as.string->length);
    }
    if (args[0].kind == UF_VAL_ARRAY) {
        return uf_val_number((double)args[0].as.array->count);
    }
    if (args[0].kind == UF_VAL_MAP) {
        return uf_val_number((double)args[0].as.map->count);
    }
    SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
    uf_runtime_error(rt, source_span_make(loc, loc), "'len()' argument must be a string, array, or map, got '%s'", uf_val_type_name(args[0]));
    return uf_val_null();
}

static UfValue native_push(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'push()' expects an array and a value");
        return uf_val_null();
    }
    uf_array_push(rt, args[0].as.array, args[1]);
    return uf_val_number((double)args[0].as.array->count);
}

static UfValue native_pop(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'pop()' expects an array");
        return uf_val_null();
    }
    return uf_array_pop(args[0].as.array);
}

static UfValue native_range(UfRuntime* rt, int argc, UfValue* args) {
    if (argc == 0) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'range()' expects at least 1 argument");
        return uf_val_null();
    }
    double start = 0;
    double end = 0;
    double step = 1;

    if (argc == 1) {
        if (args[0].kind != UF_VAL_NUMBER) {
            SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
            uf_runtime_error(rt, source_span_make(loc, loc), "'range()' argument must be a number");
            return uf_val_null();
        }
        end = args[0].as.number;
    } else {
        if (args[0].kind != UF_VAL_NUMBER || args[1].kind != UF_VAL_NUMBER) {
            SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
            uf_runtime_error(rt, source_span_make(loc, loc), "'range()' arguments must be numbers");
            return uf_val_null();
        }
        start = args[0].as.number;
        end = args[1].as.number;
        if (argc >= 3) {
            if (args[2].kind != UF_VAL_NUMBER || args[2].as.number == 0) {
                SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
                uf_runtime_error(rt, source_span_make(loc, loc), "'range()' step must be a non-zero number");
                return uf_val_null();
            }
            step = args[2].as.number;
        }
    }

    size_t count = 0;
    if (step > 0 && start < end) {
        count = (size_t)ceil((end - start) / step);
    } else if (step < 0 && start > end) {
        count = (size_t)ceil((start - end) / (-step));
    }

    if (count > 1000000) count = 1000000;

    UfValue arr = uf_val_array(rt, count);
    uf_runtime_push_temp_root(rt, arr);

    double current = start;
    for (size_t i = 0; i < count; ++i) {
        uf_array_push(rt, arr.as.array, uf_val_number(current));
        current += step;
    }

    uf_runtime_pop_temp_roots(rt, 1);
    return arr;
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

static UfValue native_keys(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_MAP) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'keys()' expects a map");
        return uf_val_null();
    }
    UfMapObject* map = args[0].as.map;
    UfValue arr = uf_val_array(rt, map->order_count);
    uf_runtime_push_temp_root(rt, arr);
    for (size_t i = 0; i < map->order_count; ++i) {
        uf_array_push(rt, arr.as.array, map->order_keys[i]);
    }
    uf_runtime_pop_temp_roots(rt, 1);
    return arr;
}

static UfValue native_values(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_MAP) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'values()' expects a map");
        return uf_val_null();
    }
    UfMapObject* map = args[0].as.map;
    UfValue arr = uf_val_array(rt, map->order_count);
    uf_runtime_push_temp_root(rt, arr);
    for (size_t i = 0; i < map->order_count; ++i) {
        UfValue val = uf_map_get(map, map->order_keys[i]);
        uf_array_push(rt, arr.as.array, val);
    }
    uf_runtime_pop_temp_roots(rt, 1);
    return arr;
}

static UfValue native_has_key(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_MAP) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'has_key()' expects a map and a key");
        return uf_val_null();
    }
    return uf_val_bool(uf_map_has(args[0].as.map, args[1]));
}

static UfValue native_delete(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_MAP) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'delete()' expects a map and a key");
        return uf_val_null();
    }
    return uf_val_bool(uf_map_delete(args[0].as.map, args[1]));
}

static UfValue native_map(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'map()' expects an array and a function");
        return uf_val_null();
    }
    UfArrayObject* src = args[0].as.array;
    UfValue fn = args[1];
    UfValue res = uf_val_array(rt, src->count);
    uf_runtime_push_temp_root(rt, res);
    SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
    SourceSpan span = source_span_make(loc, loc);

    for (size_t i = 0; i < src->count; ++i) {
        UfValue arg = src->elements[i];
        UfValue item = uf_runtime_call(rt, fn, 1, &arg, span);
        if (rt->had_runtime_error) {
            uf_runtime_pop_temp_roots(rt, 1);
            return uf_val_null();
        }
        uf_array_push(rt, res.as.array, item);
    }
    uf_runtime_pop_temp_roots(rt, 1);
    return res;
}

static UfValue native_filter(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'filter()' expects an array and a function");
        return uf_val_null();
    }
    UfArrayObject* src = args[0].as.array;
    UfValue fn = args[1];
    UfValue res = uf_val_array(rt, src->count);
    uf_runtime_push_temp_root(rt, res);
    SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
    SourceSpan span = source_span_make(loc, loc);

    for (size_t i = 0; i < src->count; ++i) {
        UfValue arg = src->elements[i];
        UfValue keep = uf_runtime_call(rt, fn, 1, &arg, span);
        if (rt->had_runtime_error) {
            uf_runtime_pop_temp_roots(rt, 1);
            return uf_val_null();
        }
        if (uf_val_is_truthy(keep)) {
            uf_array_push(rt, res.as.array, arg);
        }
    }
    uf_runtime_pop_temp_roots(rt, 1);
    return res;
}

static UfValue native_reduce(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'reduce()' expects an array, a function, and an optional initial value");
        return uf_val_null();
    }
    UfArrayObject* src = args[0].as.array;
    UfValue fn = args[1];
    SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
    SourceSpan span = source_span_make(loc, loc);

    if (src->count == 0 && argc < 3) {
        uf_runtime_error(rt, span, "'reduce()' of empty array with no initial value");
        return uf_val_null();
    }

    size_t start_idx = 0;
    UfValue acc;
    if (argc >= 3) {
        acc = args[2];
    } else {
        acc = src->elements[0];
        start_idx = 1;
    }
    uf_runtime_push_temp_root(rt, acc);

    for (size_t i = start_idx; i < src->count; ++i) {
        UfValue call_args[2] = { acc, src->elements[i] };
        acc = uf_runtime_call(rt, fn, 2, call_args, span);
        if (rt->had_runtime_error) {
            uf_runtime_pop_temp_roots(rt, 1);
            return uf_val_null();
        }
        rt->temp_roots[rt->temp_root_count - 1] = acc;
    }
    uf_runtime_pop_temp_roots(rt, 1);
    return acc;
}

static int default_compare_values(UfValue a, UfValue b) {
    if (a.kind == UF_VAL_NUMBER && b.kind == UF_VAL_NUMBER) {
        if (a.as.number < b.as.number) return -1;
        if (a.as.number > b.as.number) return 1;
        return 0;
    }
    if (a.kind == UF_VAL_STRING && b.kind == UF_VAL_STRING) {
        return strcmp(a.as.string->chars, b.as.string->chars);
    }
    if (a.kind == UF_VAL_BOOL && b.kind == UF_VAL_BOOL) {
        return (int)a.as.boolean - (int)b.as.boolean;
    }
    return (int)a.kind - (int)b.kind;
}

static UfValue native_sort(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'sort()' expects an array");
        return uf_val_null();
    }
    UfArrayObject* src = args[0].as.array;
    UfValue res = uf_val_array(rt, src->count);
    uf_runtime_push_temp_root(rt, res);
    for (size_t i = 0; i < src->count; ++i) {
        uf_array_push(rt, res.as.array, src->elements[i]);
    }

    bool has_cmp = (argc >= 2 && args[1].kind != UF_VAL_NULL);
    UfValue cmp_fn = has_cmp ? args[1] : uf_val_null();
    SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
    SourceSpan span = source_span_make(loc, loc);

    for (size_t i = 1; i < res.as.array->count; ++i) {
        UfValue key = res.as.array->elements[i];
        size_t j = i;
        while (j > 0) {
            int cmp = 0;
            if (has_cmp) {
                UfValue call_args[2] = { res.as.array->elements[j - 1], key };
                UfValue cres = uf_runtime_call(rt, cmp_fn, 2, call_args, span);
                if (rt->had_runtime_error) {
                    uf_runtime_pop_temp_roots(rt, 1);
                    return uf_val_null();
                }
                if (cres.kind == UF_VAL_NUMBER) {
                    cmp = (cres.as.number > 0) ? 1 : ((cres.as.number < 0) ? -1 : 0);
                } else if (cres.kind == UF_VAL_BOOL) {
                    cmp = cres.as.boolean ? -1 : 1;
                }
            } else {
                cmp = default_compare_values(res.as.array->elements[j - 1], key);
            }

            if (cmp > 0) {
                res.as.array->elements[j] = res.as.array->elements[j - 1];
                j--;
            } else {
                break;
            }
        }
        res.as.array->elements[j] = key;
    }

    uf_runtime_pop_temp_roots(rt, 1);
    return res;
}

static UfValue native_reverse(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'reverse()' expects an array");
        return uf_val_null();
    }
    UfArrayObject* src = args[0].as.array;
    UfValue res = uf_val_array(rt, src->count);
    uf_runtime_push_temp_root(rt, res);
    for (size_t i = src->count; i > 0; --i) {
        uf_array_push(rt, res.as.array, src->elements[i - 1]);
    }
    uf_runtime_pop_temp_roots(rt, 1);
    return res;
}

static UfValue native_find(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'find()' expects an array and a function");
        return uf_val_null();
    }
    UfArrayObject* src = args[0].as.array;
    UfValue fn = args[1];
    SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
    SourceSpan span = source_span_make(loc, loc);

    for (size_t i = 0; i < src->count; ++i) {
        UfValue arg = src->elements[i];
        UfValue match_res = uf_runtime_call(rt, fn, 1, &arg, span);
        if (rt->had_runtime_error) return uf_val_null();
        if (uf_val_is_truthy(match_res)) {
            return arg;
        }
    }
    return uf_val_null();
}

static UfValue native_every(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'every()' expects an array and a function");
        return uf_val_null();
    }
    UfArrayObject* src = args[0].as.array;
    UfValue fn = args[1];
    SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
    SourceSpan span = source_span_make(loc, loc);

    for (size_t i = 0; i < src->count; ++i) {
        UfValue arg = src->elements[i];
        UfValue res = uf_runtime_call(rt, fn, 1, &arg, span);
        if (rt->had_runtime_error) return uf_val_null();
        if (!uf_val_is_truthy(res)) {
            return uf_val_bool(false);
        }
    }
    return uf_val_bool(true);
}

static UfValue native_some(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'some()' expects an array and a function");
        return uf_val_null();
    }
    UfArrayObject* src = args[0].as.array;
    UfValue fn = args[1];
    SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
    SourceSpan span = source_span_make(loc, loc);

    for (size_t i = 0; i < src->count; ++i) {
        UfValue arg = src->elements[i];
        UfValue res = uf_runtime_call(rt, fn, 1, &arg, span);
        if (rt->had_runtime_error) return uf_val_null();
        if (uf_val_is_truthy(res)) {
            return uf_val_bool(true);
        }
    }
    return uf_val_bool(false);
}

static void register_builtins(UfRuntime* rt) {
    uf_env_declare(rt->global_env, "say",     uf_val_native("say",     native_say,     1));
    uf_env_declare(rt->global_env, "print",   uf_val_native("print",   native_print,   1));
    uf_env_declare(rt->global_env, "type_of", uf_val_native("type_of", native_type_of, 1));
    uf_env_declare(rt->global_env, "len",     uf_val_native("len",     native_len,     1));
    uf_env_declare(rt->global_env, "push",    uf_val_native("push",    native_push,    2));
    uf_env_declare(rt->global_env, "pop",     uf_val_native("pop",     native_pop,     1));
    uf_env_declare(rt->global_env, "range",   uf_val_native("range",   native_range,   -1));
    uf_env_declare(rt->global_env, "keys",    uf_val_native("keys",    native_keys,    1));
    uf_env_declare(rt->global_env, "values",  uf_val_native("values",  native_values,  1));
    uf_env_declare(rt->global_env, "has_key", uf_val_native("has_key", native_has_key, 2));
    uf_env_declare(rt->global_env, "delete",  uf_val_native("delete",  native_delete,  2));
    uf_env_declare(rt->global_env, "map",     uf_val_native("map",     native_map,     2));
    uf_env_declare(rt->global_env, "filter",  uf_val_native("filter",  native_filter,  2));
    uf_env_declare(rt->global_env, "reduce",  uf_val_native("reduce",  native_reduce,  -1));
    uf_env_declare(rt->global_env, "sort",    uf_val_native("sort",    native_sort,    -1));
    uf_env_declare(rt->global_env, "reverse", uf_val_native("reverse", native_reverse, 1));
    uf_env_declare(rt->global_env, "find",    uf_val_native("find",    native_find,    2));
    uf_env_declare(rt->global_env, "every",   uf_val_native("every",   native_every,   2));
    uf_env_declare(rt->global_env, "some",    uf_val_native("some",    native_some,    2));
    uf_env_declare(rt->global_env, "clock",   uf_val_native("clock",   native_clock,   0));
    uf_env_declare(rt->global_env, "assert",  uf_val_native("assert",  native_assert,  -1));
}

void uf_runtime_push_temp_root(UfRuntime* rt, UfValue val) {
    if (rt->temp_root_count < UF_MAX_TEMP_ROOTS) {
        rt->temp_roots[rt->temp_root_count++] = val;
    }
}

void uf_runtime_pop_temp_root(UfRuntime* rt) {
    if (rt->temp_root_count > 0) {
        rt->temp_root_count--;
    }
}

void uf_runtime_pop_temp_roots(UfRuntime* rt, size_t count) {
    if (rt->temp_root_count >= count) {
        rt->temp_root_count -= count;
    } else {
        rt->temp_root_count = 0;
    }
}

void uf_runtime_register_obj(UfRuntime* rt, UfObj* obj, size_t size) {
    rt->bytes_allocated += size;

    if (rt->bytes_allocated > rt->next_gc_threshold) {
        uf_gc_collect(rt);
    }

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
    } else if (val.kind == UF_VAL_ARRAY) {
        if (val.as.array && !val.as.array->obj.marked) {
            val.as.array->obj.marked = true;
            for (size_t i = 0; i < val.as.array->count; ++i) {
                uf_gc_mark_value(val.as.array->elements[i]);
            }
        }
    } else if (val.kind == UF_VAL_MAP) {
        if (val.as.map && !val.as.map->obj.marked) {
            val.as.map->obj.marked = true;
            UfMapObject* map = val.as.map;
            for (size_t i = 0; i < map->capacity; ++i) {
                if (map->entries[i].occupied && !map->entries[i].tombstone) {
                    uf_gc_mark_value(map->entries[i].key);
                    uf_gc_mark_value(map->entries[i].value);
                }
            }
            for (size_t i = 0; i < map->order_count; ++i) {
                uf_gc_mark_value(map->order_keys[i]);
            }
        }
    } else if (val.kind == UF_VAL_ERROR) {
        if (val.as.error && !val.as.error->obj.marked) {
            val.as.error->obj.marked = true;
            if (val.as.error->message) val.as.error->message->obj.marked = true;
            if (val.as.error->kind) val.as.error->kind->obj.marked = true;
        }
    } else if (val.kind == UF_VAL_MODULE) {
        if (val.as.module && !val.as.module->obj.marked) {
            val.as.module->obj.marked = true;
            uf_gc_mark_env(val.as.module->env);
            uf_gc_mark_value(val.as.module->exports);
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
    /* 1. Mark roots */
    uf_gc_mark_env(rt->global_env);
    uf_gc_mark_env(rt->current_env);

    for (size_t i = 0; i < rt->frame_count; ++i) {
        uf_gc_mark_env(rt->frames[i].env);
    }

    for (size_t i = 0; i < rt->temp_root_count; ++i) {
        uf_gc_mark_value(rt->temp_roots[i]);
    }

    uf_gc_mark_value(rt->current_error);

    UfModuleEntry* me = rt->module_cache;
    while (me) {
        if (me->module) {
            uf_gc_mark_value(uf_val_module(rt, me->module));
        }
        me = me->next;
    }

    /* 2. Sweep */
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
            } else if (obj->kind == UF_OBJ_ARRAY) {
                UfArrayObject* arr = (UfArrayObject*)obj;
                free(arr->elements);
                free(arr);
            } else if (obj->kind == UF_OBJ_MAP) {
                UfMapObject* map = (UfMapObject*)obj;
                free(map->entries);
                free(map->order_keys);
                free(map);
            } else if (obj->kind == UF_OBJ_MODULE) {
                UfModuleObject* mod = (UfModuleObject*)obj;
                uf_interner_free(&mod->interner);
                uf_arena_free(&mod->arena);
                if (mod->source_text) free(mod->source_text);
                free(mod->name);
                free(mod->path);
                free(mod);
            } else {
                free(obj);
            }
        }
    }

    rt->gc_count++;
    size_t min_thresh = 1024;
    rt->next_gc_threshold = (rt->bytes_allocated * 2 > min_thresh) ? rt->bytes_allocated * 2 : min_thresh;
}

void uf_runtime_init(UfRuntime* rt, UfDiagnosticReporter* reporter) {
    rt->all_objects = NULL;
    rt->temp_root_count = 0;
    rt->bytes_allocated = 0;
    rt->next_gc_threshold = UF_GC_INITIAL_THRESHOLD;
    rt->gc_count = 0;

    rt->global_env = uf_env_create(rt, NULL);
    rt->current_env = rt->global_env;
    rt->frame_count = 0;
    rt->step_count = 0;
    rt->max_steps = 10000000;
    rt->had_runtime_error = false;
    rt->try_handler_count = 0;
    rt->current_error = uf_val_null();
    rt->call_fn = NULL;
    rt->out_stream = stdout;
    rt->err_stream = stderr;
    rt->reporter = reporter;

    register_builtins(rt);
    uf_stdlib_register_runtime(rt);
    uf_module_init(rt);
}

void uf_runtime_free(UfRuntime* rt) {
    rt->global_env = NULL;
    rt->current_env = NULL;
    rt->temp_root_count = 0;

    uf_module_free_all(rt);

    UfObj* obj = rt->all_objects;
    while (obj) {
        UfObj* next = obj->next;
        if (obj->kind == UF_OBJ_ENV) {
            uf_env_free((UfEnv*)obj);
        } else if (obj->kind == UF_OBJ_ARRAY) {
            UfArrayObject* arr = (UfArrayObject*)obj;
            free(arr->elements);
            free(arr);
        } else if (obj->kind == UF_OBJ_MAP) {
            UfMapObject* map = (UfMapObject*)obj;
            free(map->entries);
            free(map->order_keys);
            free(map);
        } else if (obj->kind == UF_OBJ_MODULE) {
            UfModuleObject* mod = (UfModuleObject*)obj;
            uf_interner_free(&mod->interner);
            uf_arena_free(&mod->arena);
            if (mod->source_text) free(mod->source_text);
            free(mod->name);
            free(mod->path);
            free(mod);
        } else {
            free(obj);
        }
        obj = next;
    }
    rt->all_objects = NULL;
    rt->frame_count = 0;
    rt->bytes_allocated = 0;
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

void uf_runtime_raise(UfRuntime* rt, const char* kind, SourceSpan span, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (rt->try_handler_count > 0) {
        UfValue err = uf_val_error(rt, buffer, kind ? kind : "RuntimeError", span);
        rt->current_error = err;
        UfTryHandler* h = &rt->try_handlers[--rt->try_handler_count];
        rt->frame_count = h->frame_count;
        rt->temp_root_count = h->temp_root_count;
        rt->current_env = h->scope_env;
        longjmp(h->jmp, 1);
    }

    rt->had_runtime_error = true;

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

void uf_runtime_error(UfRuntime* rt, SourceSpan span, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (rt->try_handler_count > 0) {
        const char* kind = "RuntimeError";
        if (strncmp(buffer, "Division by zero", 16) == 0) kind = "DivisionByZero";
        else if (strncmp(buffer, "IndexOutOfBounds", 16) == 0) kind = "IndexOutOfBounds";
        else if (strncmp(buffer, "StackOverflowError", 18) == 0) kind = "StackOverflowError";
        else if (strncmp(buffer, "ExecutionQuotaExceeded", 22) == 0) kind = "ExecutionQuotaExceeded";
        else if (strncmp(buffer, "AssertionError", 14) == 0) kind = "AssertionError";
        else if (strstr(buffer, "domain error") != NULL) kind = "DomainError";
        else if (strstr(buffer, "expects") != NULL || strstr(buffer, "Cannot") != NULL || strstr(buffer, "must be") != NULL) kind = "TypeError";

        UfValue err = uf_val_error(rt, buffer, kind, span);
        rt->current_error = err;
        UfTryHandler* h = &rt->try_handlers[--rt->try_handler_count];
        rt->frame_count = h->frame_count;
        rt->temp_root_count = h->temp_root_count;
        rt->current_env = h->scope_env;
        longjmp(h->jmp, 1);
    }

    rt->had_runtime_error = true;

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

UfValue uf_runtime_call(UfRuntime* rt, UfValue callee, size_t argc, UfValue* args, SourceSpan span) {
    if (!rt->call_fn) {
        uf_runtime_error(rt, span, "Call handler not registered in runtime");
        return uf_val_null();
    }
    return rt->call_fn(rt, callee, argc, args, span);
}
