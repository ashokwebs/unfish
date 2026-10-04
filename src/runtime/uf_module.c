#include "uf_module.h"
#include "uf_runtime.h"
#include "uf_stdlib.h"
#include "../lexer/uf_lexer.h"
#include "../parser/uf_parser.h"
#include "../semantic/uf_semantic.h"
#include "../interpreter/uf_interpreter.h"
#include "../stdlib/uf_mod_sys.h"
#include "../stdlib/uf_mod_fs.h"
#include "../stdlib/uf_mod_random.h"
#include "../stdlib/uf_mod_time.h"
#include "../stdlib/uf_mod_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

void uf_module_init(UfRuntime* rt) {
    rt->module_cache = NULL;
}

void uf_module_free_all(UfRuntime* rt) {
    UfModuleEntry* curr = rt->module_cache;
    while (curr) {
        UfModuleEntry* next = curr->next;
        free(curr->name);
        free(curr);
        curr = next;
    }
    rt->module_cache = NULL;
}

UfModuleObject* uf_module_create(UfRuntime* rt, const char* name, const char* path) {
    UfModuleObject* mod = (UfModuleObject*)malloc(sizeof(UfModuleObject));
    if (!mod) {
        fprintf(stderr, "Fatal error: Out of memory allocating module\n");
        abort();
    }
    mod->obj.kind = UF_OBJ_MODULE;
    mod->obj.marked = false;
    mod->obj.next = NULL;
    mod->name = strdup(name);
    mod->path = strdup(path ? path : name);
    mod->source_text = NULL;
    mod->state = UF_MOD_UNLOADED;
    mod->env = NULL;
    mod->exports = uf_val_null();

    uf_arena_init(&mod->arena, 4096);
    uf_interner_init(&mod->interner, &mod->arena);

    if (rt) {
        uf_runtime_register_obj(rt, (UfObj*)mod, sizeof(UfModuleObject));
        uf_runtime_push_temp_root(rt, uf_val_module(rt, mod));
    }

    mod->env = uf_env_create(rt, rt->global_env);
    mod->exports = uf_val_map(rt, 16);

    if (rt) {
        uf_runtime_pop_temp_root(rt);
    }
    return mod;
}

UfModuleObject* uf_module_find_cached(UfRuntime* rt, const char* name) {
    UfModuleEntry* curr = rt->module_cache;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            return curr->module;
        }
        curr = curr->next;
    }
    return NULL;
}

void uf_module_cache_add(UfRuntime* rt, const char* name, UfModuleObject* mod) {
    UfModuleEntry* entry = (UfModuleEntry*)malloc(sizeof(UfModuleEntry));
    if (!entry) {
        fprintf(stderr, "Fatal error: Out of memory allocating module cache entry\n");
        abort();
    }
    entry->name = strdup(name);
    entry->module = mod;
    entry->next = rt->module_cache;
    rt->module_cache = entry;
}

void uf_module_cache_remove(UfRuntime* rt, const char* name) {
    UfModuleEntry** curr = &rt->module_cache;
    while (*curr) {
        if (strcmp((*curr)->name, name) == 0) {
            UfModuleEntry* to_free = *curr;
            *curr = (*curr)->next;
            free(to_free->name);
            free(to_free);
            return;
        }
        curr = &(*curr)->next;
    }
}

/* Directory containing the running executable, or NULL if it cannot be
 * determined. Used to locate the bundled stdlib modules (e.g. `testing`)
 * relative to the binary rather than the current working directory, so that
 * `import testing` works from a user's own package directory and not only
 * from inside the Unfish source tree. */
static const char* executable_dir(void) {
    static char dir[1024];
    static int resolved = 0;

    if (resolved) return dir[0] ? dir : NULL;
    resolved = 1;
    dir[0] = '\0';

#if defined(__linux__)
    char buf[1024];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        char* last_slash = strrchr(buf, '/');
        if (last_slash) {
            size_t len = (size_t)(last_slash - buf);
            if (len < sizeof(dir)) {
                memcpy(dir, buf, len);
                dir[len] = '\0';
            }
        }
    }
#endif

    return dir[0] ? dir : NULL;
}

static char* resolve_module_path(const char* name, SourceSpan span) {
    char path[1024];

    /* 1. Relative to caller file */
    if (span.start.file && strcmp(span.start.file, "<stdin>") != 0) {
        const char* last_slash = strrchr(span.start.file, '/');
        if (last_slash) {
            size_t dir_len = (size_t)(last_slash - span.start.file);
            snprintf(path, sizeof(path), "%.*s/%s.unfish", (int)dir_len, span.start.file, name);
            if (access(path, R_OK) == 0) return strdup(path);

            snprintf(path, sizeof(path), "%.*s/%s", (int)dir_len, span.start.file, name);
            if (access(path, R_OK) == 0) return strdup(path);
        }
    }

    /* 2. Relative to working directory */
    snprintf(path, sizeof(path), "%s.unfish", name);
    if (access(path, R_OK) == 0) return strdup(path);

    snprintf(path, sizeof(path), "%s", name);
    if (access(path, R_OK) == 0) return strdup(path);

    /* 3. UNFISH_PATH */
    const char* env_path = getenv("UNFISH_PATH");
    if (env_path) {
        char* copy = strdup(env_path);
        char* token = strtok(copy, ":");
        while (token) {
            snprintf(path, sizeof(path), "%s/%s.unfish", token, name);
            if (access(path, R_OK) == 0) {
                free(copy);
                return strdup(path);
            }
            token = strtok(NULL, ":");
        }
        free(copy);
    }

    /* 4. Built-in stdlib Unfish modules (e.g. testing). */
    char* bundled = uf_module_find_bundled_stdlib(name);
    if (bundled) return bundled;

    /* 5. Finally the historical working-directory-relative location. */
    snprintf(path, sizeof(path), "src/stdlib/%s.unfish", name);
    if (access(path, R_OK) == 0) return strdup(path);

    return NULL;
}

char* uf_module_find_bundled_stdlib(const char* name) {
    /* Located relative to the executable, so they resolve wherever the
     * binary is run from. */
    char path[1024];
    const char* exe_dir = executable_dir();
    if (exe_dir) {
        /* Running from the source tree: <repo>/bin/unfish -> <repo>/src/stdlib */
        snprintf(path, sizeof(path), "%s/../src/stdlib/%s.unfish", exe_dir, name);
        if (access(path, R_OK) == 0) return strdup(path);

        /* Installed layouts: <prefix>/bin/unfish -> <prefix>/stdlib (what
         * install.sh and the release tarball lay out under ~/.unfish),
         * <prefix>/lib/unfish/stdlib, or a stdlib directory beside the
         * binary. */
        snprintf(path, sizeof(path), "%s/../stdlib/%s.unfish", exe_dir, name);
        if (access(path, R_OK) == 0) return strdup(path);

        snprintf(path, sizeof(path), "%s/../lib/unfish/stdlib/%s.unfish", exe_dir, name);
        if (access(path, R_OK) == 0) return strdup(path);

        snprintf(path, sizeof(path), "%s/stdlib/%s.unfish", exe_dir, name);
        if (access(path, R_OK) == 0) return strdup(path);
    }
    return NULL;
}

static char* read_file_content(const char* filepath) {
    FILE* file = fopen(filepath, "rb");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0) {
        fclose(file);
        return NULL;
    }

    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, size, file);
    buffer[read_bytes] = '\0';
    fclose(file);
    return buffer;
}

UfModuleObject* uf_module_load(UfRuntime* rt, const char* name, SourceSpan span) {
    /* 1. Check module cache */
    UfModuleObject* cached = uf_module_find_cached(rt, name);
    if (cached) {
        if (cached->state == UF_MOD_LOADING) {
            uf_runtime_raise(rt, "CircularImportError", span, "Circular dependency detected while importing module '%s'", name);
            return NULL;
        }
        return cached;
    }

    /* 2. Built-in modules (lazy initialization) */
    if (strcmp(name, "math") == 0) {
        UfModuleObject* math_mod = uf_module_create(rt, "math", "<builtin:math>");
        uf_runtime_push_temp_root(rt, uf_val_module(rt, math_mod));
        const char* math_symbols[] = {
            "abs", "floor", "ceil", "round", "sqrt", "pow", "min", "max",
            "log", "sin", "cos", "tan", "random", "random_int", "PI", "E", "INFINITY"
        };
        for (size_t i = 0; i < sizeof(math_symbols) / sizeof(math_symbols[0]); ++i) {
            UfValue val;
            if (uf_env_lookup(rt->global_env, math_symbols[i], &val)) {
                UfValue k = uf_val_string(rt, math_symbols[i], strlen(math_symbols[i]));
                uf_map_set(rt, math_mod->exports.as.map, k, val);
                uf_env_declare(math_mod->env, math_symbols[i], val);
            }
        }
        uf_runtime_pop_temp_root(rt);
        math_mod->state = UF_MOD_LOADED;
        uf_module_cache_add(rt, "math", math_mod);
        return math_mod;
    }

    if (strcmp(name, "strings") == 0) {
        UfModuleObject* str_mod = uf_module_create(rt, "strings", "<builtin:strings>");
        uf_runtime_push_temp_root(rt, uf_val_module(rt, str_mod));
        const char* str_symbols[] = {
            "split", "join", "trim", "replace", "to_upper", "to_lower",
            "contains", "starts_with", "ends_with", "char_at", "to_number",
            "to_string", "repeat_string", "substring", "index_of",
            "pad_start", "pad_end", "trim_start", "trim_end", "chars", "count"
        };
        for (size_t i = 0; i < sizeof(str_symbols) / sizeof(str_symbols[0]); ++i) {
            UfValue val;
            if (uf_env_lookup(rt->global_env, str_symbols[i], &val)) {
                UfValue k = uf_val_string(rt, str_symbols[i], strlen(str_symbols[i]));
                uf_map_set(rt, str_mod->exports.as.map, k, val);
                uf_env_declare(str_mod->env, str_symbols[i], val);
            }
        }
        uf_runtime_pop_temp_root(rt);
        str_mod->state = UF_MOD_LOADED;
        uf_module_cache_add(rt, "strings", str_mod);
        return str_mod;
    }

    if (strcmp(name, "sys") == 0) {
        UfModuleObject* mod = uf_mod_sys_create(rt);
        uf_module_cache_add(rt, "sys", mod);
        return mod;
    }

    if (strcmp(name, "fs") == 0) {
        UfModuleObject* mod = uf_mod_fs_create(rt);
        uf_module_cache_add(rt, "fs", mod);
        return mod;
    }

    if (strcmp(name, "random") == 0) {
        UfModuleObject* mod = uf_mod_random_create(rt);
        uf_module_cache_add(rt, "random", mod);
        return mod;
    }

    if (strcmp(name, "time") == 0) {
        UfModuleObject* mod = uf_mod_time_create(rt);
        uf_module_cache_add(rt, "time", mod);
        return mod;
    }

    if (strcmp(name, "json") == 0) {
        UfModuleObject* mod = uf_mod_json_create(rt);
        uf_module_cache_add(rt, "json", mod);
        return mod;
    }

    /* 3. Resolve module file path */
    char* resolved_path = resolve_module_path(name, span);
    if (!resolved_path) {
        uf_runtime_raise(rt, "ModuleNotFoundError", span, "Module '%s' not found", name);
        return NULL;
    }

    /* 4. Read file */
    char* source = read_file_content(resolved_path);
    if (!source) {
        uf_runtime_raise(rt, "ModuleLoadError", span, "Failed to read module file '%s'", resolved_path);
        free(resolved_path);
        return NULL;
    }

    /* 5. Create module object and register as LOADING */
    UfModuleObject* mod = uf_module_create(rt, name, resolved_path);
    mod->source_text = source;
    mod->state = UF_MOD_LOADING;
    uf_module_cache_add(rt, name, mod);
    uf_runtime_push_temp_root(rt, uf_val_module(rt, mod));
    free(resolved_path);

    /* 6. Compile: Lex, Parse, Semantic Analysis */
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, mod->path, mod->source_text);

    UfLexer lexer;
    uf_lexer_init(&lexer, mod->path, mod->source_text, &mod->arena, &mod->interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &mod->arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);

    if (!program || parser.had_error) {
        mod->state = UF_MOD_ERROR;
        uf_module_cache_remove(rt, name);
        uf_runtime_pop_temp_roots(rt, 1);
        uf_runtime_raise(rt, "ModuleLoadError", span, "Syntax error in module '%s'", name);
        return NULL;
    }

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &mod->arena, &reporter);
    bool sema_ok = uf_analyze_program(&sema, program);
    if (!sema_ok || sema.had_error) {
        mod->state = UF_MOD_ERROR;
        uf_module_cache_remove(rt, name);
        uf_runtime_pop_temp_roots(rt, 1);
        uf_runtime_raise(rt, "ModuleLoadError", span, "Semantic error in module '%s'", name);
        return NULL;
    }

    /* 7. Execute in isolated module environment */
    UfEnv* prev_env = rt->current_env;
    rt->current_env = mod->env;

    bool had_err = false;
    bool caught_by_longjmp = false;
    if (rt->try_handler_count < UF_MAX_TRY_HANDLERS) {
        UfTryHandler* h = &rt->try_handlers[rt->try_handler_count++];
        h->scope_env = prev_env;
        h->frame_count = rt->frame_count;
        h->temp_root_count = rt->temp_root_count;
        if (setjmp(h->jmp) == 0) {
            UfInterpretResult res = uf_interpret_program(rt, program);
            if (res != UF_INTERPRET_OK || rt->had_runtime_error) {
                had_err = true;
            }
            rt->try_handler_count--;
        } else {
            had_err = true;
            caught_by_longjmp = true;
        }
    } else {
        UfInterpretResult res = uf_interpret_program(rt, program);
        if (res != UF_INTERPRET_OK || rt->had_runtime_error) {
            had_err = true;
        }
    }

    rt->current_env = prev_env;
    uf_runtime_pop_temp_roots(rt, 1);

    if (had_err) {
        mod->state = UF_MOD_ERROR;
        uf_module_cache_remove(rt, name);
        if (caught_by_longjmp && rt->current_error.kind == UF_VAL_ERROR) {
            if (rt->try_handler_count > 0) {
                UfTryHandler* outer_h = &rt->try_handlers[--rt->try_handler_count];
                rt->frame_count = outer_h->frame_count;
                rt->temp_root_count = outer_h->temp_root_count;
                rt->current_env = outer_h->scope_env;
                longjmp(outer_h->jmp, 1);
            } else {
                rt->had_runtime_error = true;
                const char* msg = rt->current_error.as.error->message ? rt->current_error.as.error->message->chars : "Runtime error";
                SourceSpan err_span = { { rt->current_error.as.error->file, (uint32_t)rt->current_error.as.error->line, 1, 0 },
                                        { rt->current_error.as.error->file, (uint32_t)rt->current_error.as.error->line, 1, 0 } };
                if (rt->reporter) {
                    uf_report_diag(rt->reporter, UF_DIAG_RUNTIME_ERROR, err_span, msg, NULL);
                } else {
                    fprintf(rt->err_stream, "Runtime Error: %s\n", msg);
                }
            }
        }
        return NULL;
    }

    /* 8. Populate exports map from module environment */
    for (size_t b = 0; b < mod->env->bucket_count; ++b) {
        UfEnvBinding* bind = mod->env->buckets[b];
        while (bind) {
            UfValue k = uf_val_string(rt, bind->name, strlen(bind->name));
            uf_map_set(rt, mod->exports.as.map, k, bind->value);
            bind = bind->next;
        }
    }

    mod->state = UF_MOD_LOADED;
    return mod;
}
