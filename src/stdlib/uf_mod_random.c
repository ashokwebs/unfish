#include "uf_mod_random.h"
#include "../runtime/uf_runtime.h"
#include <stdlib.h>
#include <time.h>
#include <math.h>

static bool rng_seeded = false;
static void ensure_seeded(void) {
    if (!rng_seeded) {
        srand((unsigned int)time(NULL));
        rng_seeded = true;
    }
}

static UfValue rnd_random(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    UF_UNUSED(argc);
    UF_UNUSED(args);
    ensure_seeded();
    double r = (double)rand() / ((double)RAND_MAX + 1.0);
    return uf_val_number(r);
}

static UfValue rnd_random_int(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    ensure_seeded();
    if (argc < 2 || args[0].kind != UF_VAL_NUMBER || args[1].kind != UF_VAL_NUMBER) {
        return uf_val_number(0);
    }
    int min_val = (int)args[0].as.number;
    int max_val = (int)args[1].as.number;
    if (min_val > max_val) {
        int tmp = min_val;
        min_val = max_val;
        max_val = tmp;
    }
    int range = max_val - min_val + 1;
    if (range <= 0) return uf_val_number((double)min_val);
    int r = min_val + (rand() % range);
    return uf_val_number((double)r);
}

static UfValue rnd_choice(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    ensure_seeded();
    if (argc < 1 || args[0].kind != UF_VAL_ARRAY || args[0].as.array->count == 0) {
        return uf_val_null();
    }
    UfArrayObject* arr = args[0].as.array;
    size_t idx = (size_t)rand() % arr->count;
    return arr->elements[idx];
}

static UfValue rnd_shuffle(UfRuntime* rt, int argc, UfValue* args) {
    ensure_seeded();
    if (argc < 1 || args[0].kind != UF_VAL_ARRAY) {
        return uf_val_array(rt, 0);
    }
    UfArrayObject* src = args[0].as.array;
    UfValue new_arr = uf_val_array(rt, src->count);
    uf_runtime_push_temp_root(rt, new_arr);

    for (size_t i = 0; i < src->count; ++i) {
        uf_array_push(rt, new_arr.as.array, src->elements[i]);
    }

    /* Fisher-Yates shuffle */
    for (size_t i = new_arr.as.array->count; i > 1; --i) {
        size_t j = (size_t)rand() % i;
        UfValue tmp = new_arr.as.array->elements[i - 1];
        new_arr.as.array->elements[i - 1] = new_arr.as.array->elements[j];
        new_arr.as.array->elements[j] = tmp;
    }

    uf_runtime_pop_temp_root(rt);
    return new_arr;
}

UfModuleObject* uf_mod_random_create(UfRuntime* rt) {
    UfModuleObject* mod = uf_module_create(rt, "random", "<builtin:random>");
    uf_runtime_push_temp_root(rt, uf_val_module(rt, mod));

    struct {
        const char* name;
        UfNativeFn fn;
        int arity;
    } fns[] = {
        { "random", rnd_random, 0 },
        { "random_int", rnd_random_int, 2 },
        { "choice", rnd_choice, 1 },
        { "shuffle", rnd_shuffle, 1 }
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
