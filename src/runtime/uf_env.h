#ifndef UF_ENV_H
#define UF_ENV_H

#include "../common/uf_common.h"
#include "uf_value.h"
#include "uf_object.h"

#define UF_ENV_INITIAL_BUCKETS 16

typedef struct UfEnvBinding {
    const char* name;
    UfValue value;
    struct UfEnvBinding* next;
} UfEnvBinding;

struct UfEnv {
    UfObj obj;
    struct UfEnv* parent;
    UfEnvBinding** buckets;
    size_t bucket_count;
    size_t count;
};

UfEnv* uf_env_create(UfRuntime* rt, UfEnv* parent);
void uf_env_clear(UfEnv* env);
void uf_env_free(UfEnv* env);

void uf_env_declare(UfEnv* env, const char* name, UfValue value);
bool uf_env_assign(UfEnv* env, const char* name, UfValue value);
bool uf_env_lookup(UfEnv* env, const char* name, UfValue* out_value);

#endif /* UF_ENV_H */
