#include "uf_mod_sys.h"
#include "../runtime/uf_runtime.h"
#include <stdlib.h>
#include <string.h>

static UfValue sys_exit(UfRuntime* rt, int argc, UfValue* args) {
    int code = 0;
    if (argc > 0 && args[0].kind == UF_VAL_NUMBER) {
        code = (int)args[0].as.number;
    }
    UF_UNUSED(rt);
    exit(code);
    return uf_val_null();
}

static UfValue sys_args(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(argc);
    UF_UNUSED(args);
    UfValue arr = uf_val_array(rt, rt->argc > 0 ? (size_t)rt->argc : 0);
    uf_runtime_push_temp_root(rt, arr);
    for (int i = 0; i < rt->argc; ++i) {
        UfValue str = uf_val_string_cstr(rt, rt->argv[i]);
        uf_array_push(rt, arr.as.array, str);
    }
    uf_runtime_pop_temp_root(rt);
    return arr;
}

static UfValue sys_platform(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(argc);
    UF_UNUSED(args);
#if defined(_WIN32) || defined(_WIN64)
    return uf_val_string_cstr(rt, "windows");
#elif defined(__APPLE__) || defined(__MACH__)
    return uf_val_string_cstr(rt, "darwin");
#elif defined(__linux__)
    return uf_val_string_cstr(rt, "linux");
#else
    return uf_val_string_cstr(rt, "unknown");
#endif
}

static UfValue sys_env(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        return uf_val_null();
    }
    const char* val = getenv(args[0].as.string->chars);
    if (!val) return uf_val_null();
    return uf_val_string_cstr(rt, val);
}

UfModuleObject* uf_mod_sys_create(UfRuntime* rt) {
    UfModuleObject* mod = uf_module_create(rt, "sys", "<builtin:sys>");
    uf_runtime_push_temp_root(rt, uf_val_module(rt, mod));

    struct {
        const char* name;
        UfNativeFn fn;
        int arity;
    } fns[] = {
        { "exit", sys_exit, 1 },
        { "args", sys_args, 0 },
        { "platform", sys_platform, 0 },
        { "env", sys_env, 1 }
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
