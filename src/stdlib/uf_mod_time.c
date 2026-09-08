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
        { "timestamp", time_timestamp, 0 }
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
