#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "uf_mod_time.h"
#include "../runtime/uf_runtime.h"
#include <time.h>
#include <unistd.h>

static UfValue time_clock(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    UF_UNUSED(argc);
    UF_UNUSED(args);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double sec = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    return uf_val_number(sec);
}

static UfValue time_sleep(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    if (argc > 0 && args[0].kind == UF_VAL_NUMBER) {
        double sec = args[0].as.number;
        if (sec > 0.0) {
            struct timespec req;
            req.tv_sec = (time_t)sec;
            req.tv_nsec = (long)((sec - (double)req.tv_sec) * 1e9);
            nanosleep(&req, NULL);
        }
    }
    return uf_val_null();
}

static UfValue time_timestamp(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    UF_UNUSED(argc);
    UF_UNUSED(args);
    return uf_val_number((double)time(NULL));
}

static UfValue time_format(UfRuntime* rt, int argc, UfValue* args) {
    time_t t;
    if (argc > 0 && args[0].kind == UF_VAL_NUMBER) {
        t = (time_t)args[0].as.number;
    } else {
        t = time(NULL);
    }
    const char* fmt = "%Y-%m-%d %H:%M:%S";
    if (argc > 1 && args[1].kind == UF_VAL_STRING) {
        fmt = args[1].as.string->chars;
    }
    struct tm* tm_info = localtime(&t);
    if (!tm_info) return uf_val_string_cstr(rt, "");
    char buf[128];
    size_t len = strftime(buf, sizeof(buf), fmt, tm_info);
    return uf_val_string(rt, buf, len);
}

static UfValue time_iso(UfRuntime* rt, int argc, UfValue* args) {
    time_t t;
    if (argc > 0 && args[0].kind == UF_VAL_NUMBER) {
        t = (time_t)args[0].as.number;
    } else {
        t = time(NULL);
    }
    struct tm* tm_info = gmtime(&t);
    if (!tm_info) return uf_val_string_cstr(rt, "");
    char buf[64];
    size_t len = strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    return uf_val_string(rt, buf, len);
}

UfModuleObject* uf_mod_time_create(UfRuntime* rt) {
    UfModuleObject* mod = uf_module_create(rt, "time", "<builtin:time>");
    uf_runtime_push_temp_root(rt, uf_val_module(rt, mod));

    struct {
        const char* name;
        UfNativeFn fn;
        int arity;
    } fns[] = {
        { "clock", time_clock, 0 },
        { "sleep", time_sleep, 1 },
        { "timestamp", time_timestamp, 0 },
        { "format", time_format, -1 },
        { "iso", time_iso, -1 }
    };

    for (size_t i = 0; i < sizeof(fns) / sizeof(fns[0]); ++i) {
        UfValue val = uf_val_native(fns[i].name, fns[i].fn, fns[i].arity);
        UfValue k = uf_val_string_cstr(rt, fns[i].name);
        uf_map_set(rt, mod->exports.as.map, k, val);
        uf_env_declare(mod->env, fns[i].name, val);
    }

    uf_runtime_pop_temp_root(rt);
    mod->state = UF_MOD_LOADED;
    return mod;
}
