#include "uf_env.h"
#include "uf_runtime.h"

static uint32_t hash_name(const char* name) {
    uint32_t h = 2166136261u;
    while (*name) {
        h ^= (uint8_t)*name++;
        h *= 16777619u;
    }
    return h;
}

UfEnv* uf_env_create(UfRuntime* rt, UfEnv* parent) {
    UfEnv* env = (UfEnv*)malloc(sizeof(UfEnv));
    if (!env) {
        fprintf(stderr, "Fatal error: Out of memory creating environment\n");
        abort();
    }
    env->obj.kind = UF_OBJ_ENV;
    env->obj.marked = false;
    env->obj.next = NULL;
    env->parent = parent;
    env->bucket_count = UF_ENV_INITIAL_BUCKETS;
    env->count = 0;
    env->buckets = (UfEnvBinding**)calloc(env->bucket_count, sizeof(UfEnvBinding*));
    if (!env->buckets) {
        fprintf(stderr, "Fatal error: Out of memory creating environment buckets\n");
        abort();
    }

    if (rt) {
        size_t size = sizeof(UfEnv) + env->bucket_count * sizeof(UfEnvBinding*);
        uf_runtime_register_obj(rt, (UfObj*)env, size);
    }
    return env;
}

void uf_env_clear(UfEnv* env) {
    if (!env) return;
    for (size_t i = 0; i < env->bucket_count; ++i) {
        UfEnvBinding* b = env->buckets[i];
        while (b) {
            UfEnvBinding* next = b->next;
            free(b);
            b = next;
        }
        env->buckets[i] = NULL;
    }
    env->count = 0;
}

void uf_env_free(UfEnv* env) {
    if (!env) return;
    uf_env_clear(env);
    free(env->buckets);
    free(env);
}

static void env_resize(UfEnv* env) {
    size_t new_cap = env->bucket_count * 2;
    UfEnvBinding** new_buckets = (UfEnvBinding**)calloc(new_cap, sizeof(UfEnvBinding*));
    if (!new_buckets) return;

    for (size_t i = 0; i < env->bucket_count; ++i) {
        UfEnvBinding* b = env->buckets[i];
        while (b) {
            UfEnvBinding* next = b->next;
            uint32_t h = hash_name(b->name);
            size_t idx = h & (new_cap - 1);
            b->next = new_buckets[idx];
            new_buckets[idx] = b;
            b = next;
        }
    }
    free(env->buckets);
    env->buckets = new_buckets;
    env->bucket_count = new_cap;
}

void uf_env_declare(UfEnv* env, const char* name, UfValue value) {
    uint32_t h = hash_name(name);
    size_t idx = h & (env->bucket_count - 1);

    UfEnvBinding* b = env->buckets[idx];
    while (b) {
        if (strcmp(b->name, name) == 0) {
            b->value = value;
            return;
        }
        b = b->next;
    }

    if (env->count + 1 > env->bucket_count * 0.75) {
        env_resize(env);
        idx = h & (env->bucket_count - 1);
    }

    UfEnvBinding* new_b = (UfEnvBinding*)malloc(sizeof(UfEnvBinding));
    new_b->name = name;
    new_b->value = value;
    new_b->next = env->buckets[idx];
    env->buckets[idx] = new_b;
    env->count++;
}

bool uf_env_assign(UfEnv* env, const char* name, UfValue value) {
    UfEnv* curr = env;
    while (curr) {
        uint32_t h = hash_name(name);
        size_t idx = h & (curr->bucket_count - 1);
        UfEnvBinding* b = curr->buckets[idx];
        while (b) {
            if (strcmp(b->name, name) == 0) {
                b->value = value;
                return true;
            }
            b = b->next;
        }
        curr = curr->parent;
    }
    return false;
}

bool uf_env_lookup(UfEnv* env, const char* name, UfValue* out_value) {
    UfEnv* curr = env;
    while (curr) {
        uint32_t h = hash_name(name);
        size_t idx = h & (curr->bucket_count - 1);
        UfEnvBinding* b = curr->buckets[idx];
        while (b) {
            if (strcmp(b->name, name) == 0) {
                if (out_value) {
                    *out_value = b->value;
                }
                return true;
            }
            b = b->next;
        }
        curr = curr->parent;
    }
    return false;
}
