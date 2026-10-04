#ifndef UF_MODULE_H
#define UF_MODULE_H

#include "../common/uf_common.h"
#include "../common/uf_source.h"
#include "../common/uf_arena.h"
#include "../common/uf_string.h"
#include "uf_object.h"
#include "uf_value.h"
#include "uf_env.h"

typedef enum {
    UF_MOD_UNLOADED,
    UF_MOD_LOADING,
    UF_MOD_LOADED,
    UF_MOD_ERROR
} UfModuleState;

struct UfModuleObject {
    UfObj obj;
    char* name;
    char* path;
    char* source_text;
    UfModuleState state;
    UfArena arena;
    UfInterner interner;
    UfEnv* env;
    UfValue exports; /* UF_VAL_MAP */
};

typedef struct UfModuleEntry {
    char* name;
    UfModuleObject* module;
    struct UfModuleEntry* next;
} UfModuleEntry;

void uf_module_init(UfRuntime* rt);
void uf_module_free_all(UfRuntime* rt);

UfModuleObject* uf_module_create(UfRuntime* rt, const char* name, const char* path);
UfModuleObject* uf_module_find_cached(UfRuntime* rt, const char* name);
void uf_module_cache_add(UfRuntime* rt, const char* name, UfModuleObject* mod);
void uf_module_cache_remove(UfRuntime* rt, const char* name);

UfModuleObject* uf_module_load(UfRuntime* rt, const char* name, SourceSpan span);

/* Path of a stdlib module written in Unfish (e.g. `testing`) bundled with
 * this executable, in a source checkout or an installed layout, or NULL.
 * The caller frees it. Shared by the runtime and the native compiler. */
char* uf_module_find_bundled_stdlib(const char* name);

#endif /* UF_MODULE_H */
