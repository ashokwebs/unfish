#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "uf_cache.h"
#include "../runtime/uf_module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define UFC_MAGIC_STACK "UFC\x01"
#define UFC_MAGIC_REG   "UFR\x01"
/* Bump whenever the compilers' output or their accept/reject behaviour changes,
 * so that bytecode cached by an older build is discarded rather than replayed.
 * v3: compilers now reject programs that exceed internal limits instead of
 * silently emitting broken bytecode.
 * v4: compilers properly emit OP_POP_TRY on loop break/continue and early returns.
 * v5: return/break/continue run pending finally blocks, catch clauses are
 *     guarded so finally still runs when they raise, and the register
 *     compiler evaluates a return value before popping its try handler.
 * v6: match patterns check their shape (OP_MATCH_SHAPE / ROP_MATCH_SHAPE)
 *     and nest recursively. */
#define UFC_VERSION     6

uint64_t uf_cache_hash_source(const char* source, size_t length) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < length; ++i) {
        hash ^= (uint8_t)source[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

char* uf_cache_path_for(const char* source_path, bool is_regvm) {
    const char* home = getenv("HOME");
    if (!home || home[0] == '\0') {
        home = ".";
    }

    char cache_dir[PATH_MAX];
    int dirlen = snprintf(cache_dir, sizeof(cache_dir), "%s/.unfish_cache", home);
    if (dirlen < 0 || (size_t)dirlen >= sizeof(cache_dir)) {
        return NULL;
    }
    mkdir(cache_dir, 0755);

    char resolved[PATH_MAX];
    const char* canonical = realpath(source_path, resolved);
    if (!canonical) {
        canonical = source_path;
    }

    uint64_t path_hash = uf_cache_hash_source(canonical, strlen(canonical));

    size_t res_size = (size_t)dirlen + 64;
    char* result = (char*)malloc(res_size);
    if (!result) return NULL;
    snprintf(result, res_size, "%s/%016llx.%s",
             cache_dir, (unsigned long long)path_hash, is_regvm ? "ufrc" : "ufc");
    return result;
}

/* --- Stack VM Serialization --- */

static bool write_bytecode_fn(FILE* f, UfBytecodeFunction* fn) {
    if (!fn) return false;

    uint32_t name_len = fn->name ? (uint32_t)strlen(fn->name) : 0;
    if (fwrite(&name_len, sizeof(uint32_t), 1, f) != 1) return false;
    if (name_len > 0) {
        if (fwrite(fn->name, 1, name_len, f) != name_len) return false;
    }

    uint32_t arity = (uint32_t)fn->arity;
    uint32_t min_arity = (uint32_t)fn->min_arity;
    uint8_t has_rest = fn->has_rest ? 1 : 0;
    uint8_t is_async = fn->is_async ? 1 : 0;
    uint32_t upvalue_count = (uint32_t)fn->upvalue_count;

    if (fwrite(&arity, sizeof(uint32_t), 1, f) != 1) return false;
    if (fwrite(&min_arity, sizeof(uint32_t), 1, f) != 1) return false;
    if (fwrite(&has_rest, sizeof(uint8_t), 1, f) != 1) return false;
    if (fwrite(&is_async, sizeof(uint8_t), 1, f) != 1) return false;
    if (fwrite(&upvalue_count, sizeof(uint32_t), 1, f) != 1) return false;

    uint32_t code_count = (uint32_t)fn->chunk.code_count;
    if (fwrite(&code_count, sizeof(uint32_t), 1, f) != 1) return false;
    if (code_count > 0) {
        if (fwrite(fn->chunk.code, sizeof(uint8_t), code_count, f) != code_count) return false;
        if (fwrite(fn->chunk.lines, sizeof(int), code_count, f) != code_count) return false;
    }

    uint32_t const_count = (uint32_t)fn->chunk.const_count;
    if (fwrite(&const_count, sizeof(uint32_t), 1, f) != 1) return false;

    for (size_t i = 0; i < const_count; ++i) {
        UfValue v = fn->chunk.constants[i];
        if (v.kind == UF_VAL_NULL) {
            uint8_t tag = 0;
            if (fwrite(&tag, sizeof(uint8_t), 1, f) != 1) return false;
        } else if (v.kind == UF_VAL_BOOL) {
            uint8_t tag = 1;
            uint8_t b = v.as.boolean ? 1 : 0;
            if (fwrite(&tag, sizeof(uint8_t), 1, f) != 1) return false;
            if (fwrite(&b, sizeof(uint8_t), 1, f) != 1) return false;
        } else if (v.kind == UF_VAL_NUMBER) {
            uint8_t tag = 2;
            double num = v.as.number;
            if (fwrite(&tag, sizeof(uint8_t), 1, f) != 1) return false;
            if (fwrite(&num, sizeof(double), 1, f) != 1) return false;
        } else if (v.kind == UF_VAL_STRING && v.as.string) {
            uint8_t tag = 3;
            uint32_t slen = (uint32_t)v.as.string->length;
            if (fwrite(&tag, sizeof(uint8_t), 1, f) != 1) return false;
            if (fwrite(&slen, sizeof(uint32_t), 1, f) != 1) return false;
            if (slen > 0) {
                if (fwrite(v.as.string->chars, 1, slen, f) != slen) return false;
            }
        } else if (v.kind == UF_VAL_BYTECODE_FN && v.as.bytecode_fn) {
            uint8_t tag = 4;
            if (fwrite(&tag, sizeof(uint8_t), 1, f) != 1) return false;
            if (!write_bytecode_fn(f, v.as.bytecode_fn)) return false;
        } else {
            return false;
        }
    }
    return true;
}

static bool write_imports(FILE* f, UfProgram* program) {
    uint32_t count = 0;
    if (program) {
        for (size_t i = 0; i < program->count; ++i) {
            UfStmtKind k = program->stmts[i]->kind;
            if (k == UF_STMT_IMPORT || k == UF_STMT_FROM_IMPORT) {
                count++;
            }
        }
    }

    if (fwrite(&count, sizeof(uint32_t), 1, f) != 1) return false;
    if (count == 0 || !program) return true;

    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_IMPORT) {
            uint8_t is_from = 0;
            if (fwrite(&is_from, sizeof(uint8_t), 1, f) != 1) return false;

            const char* mod = stmt->as.import_stmt.module_name ? stmt->as.import_stmt.module_name : "";
            uint32_t mod_len = (uint32_t)strlen(mod);
            if (fwrite(&mod_len, sizeof(uint32_t), 1, f) != 1) return false;
            if (mod_len > 0 && fwrite(mod, 1, mod_len, f) != mod_len) return false;

            const char* alias = stmt->as.import_stmt.alias ? stmt->as.import_stmt.alias : "";
            uint32_t alias_len = (uint32_t)strlen(alias);
            if (fwrite(&alias_len, sizeof(uint32_t), 1, f) != 1) return false;
            if (alias_len > 0 && fwrite(alias, 1, alias_len, f) != alias_len) return false;
        } else if (stmt->kind == UF_STMT_FROM_IMPORT) {
            uint8_t is_from = 1;
            if (fwrite(&is_from, sizeof(uint8_t), 1, f) != 1) return false;

            const char* mod = stmt->as.from_import_stmt.module_name ? stmt->as.from_import_stmt.module_name : "";
            uint32_t mod_len = (uint32_t)strlen(mod);
            if (fwrite(&mod_len, sizeof(uint32_t), 1, f) != 1) return false;
            if (mod_len > 0 && fwrite(mod, 1, mod_len, f) != mod_len) return false;

            uint32_t sym_count = (uint32_t)stmt->as.from_import_stmt.count;
            if (fwrite(&sym_count, sizeof(uint32_t), 1, f) != 1) return false;

            for (size_t s = 0; s < sym_count; ++s) {
                const char* sym = stmt->as.from_import_stmt.symbols[s] ? stmt->as.from_import_stmt.symbols[s] : "";
                uint32_t sym_len = (uint32_t)strlen(sym);
                if (fwrite(&sym_len, sizeof(uint32_t), 1, f) != 1) return false;
                if (sym_len > 0 && fwrite(sym, 1, sym_len, f) != sym_len) return false;

                const char* alias = (stmt->as.from_import_stmt.aliases && stmt->as.from_import_stmt.aliases[s])
                                     ? stmt->as.from_import_stmt.aliases[s] : "";
                uint32_t alias_len = (uint32_t)strlen(alias);
                if (fwrite(&alias_len, sizeof(uint32_t), 1, f) != 1) return false;
                if (alias_len > 0 && fwrite(alias, 1, alias_len, f) != alias_len) return false;
            }
        }
    }
    return true;
}

bool uf_cache_write_stack(const char* cache_path, UfProgram* program, UfBytecodeFunction* fn, uint64_t source_hash) {
    if (!cache_path || !fn) return false;

    FILE* f = fopen(cache_path, "wb");
    if (!f) return false;

    if (fwrite(UFC_MAGIC_STACK, 1, 4, f) != 4) { fclose(f); return false; }
    uint32_t ver = UFC_VERSION;
    if (fwrite(&ver, sizeof(uint32_t), 1, f) != 1) { fclose(f); return false; }
    if (fwrite(&source_hash, sizeof(uint64_t), 1, f) != 1) { fclose(f); return false; }

    if (!write_imports(f, program)) { fclose(f); return false; }
    bool ok = write_bytecode_fn(f, fn);
    fclose(f);
    return ok;
}

static UfBytecodeFunction* read_bytecode_fn(FILE* f, UfRuntime* rt) {
    uint32_t name_len = 0;
    if (fread(&name_len, sizeof(uint32_t), 1, f) != 1) return NULL;

    char* name_buf = NULL;
    if (name_len > 0) {
        name_buf = (char*)malloc(name_len + 1);
        if (!name_buf) return NULL;
        if (fread(name_buf, 1, name_len, f) != name_len) {
            free(name_buf);
            return NULL;
        }
        name_buf[name_len] = '\0';
    }

    uint32_t arity = 0;
    uint32_t min_arity = 0;
    uint8_t has_rest = 0;
    uint8_t is_async = 0;
    uint32_t upvalue_count = 0;

    if (fread(&arity, sizeof(uint32_t), 1, f) != 1) { free(name_buf); return NULL; }
    if (fread(&min_arity, sizeof(uint32_t), 1, f) != 1) { free(name_buf); return NULL; }
    if (fread(&has_rest, sizeof(uint8_t), 1, f) != 1) { free(name_buf); return NULL; }
    if (fread(&is_async, sizeof(uint8_t), 1, f) != 1) { free(name_buf); return NULL; }
    if (fread(&upvalue_count, sizeof(uint32_t), 1, f) != 1) { free(name_buf); return NULL; }

    /* For name, intern it into runtime if possible */
    const char* interned_name = name_buf;
    if (name_buf && rt) {
        UfValue sval = uf_val_string(rt, name_buf, name_len);
        interned_name = sval.as.string ? sval.as.string->chars : NULL;
        free(name_buf);
        name_buf = NULL;
    }

    UfBytecodeFunction* fn = uf_bytecode_fn_new(rt, interned_name, arity, min_arity, has_rest != 0);
    if (!fn) {
        if (name_buf) free(name_buf);
        return NULL;
    }
    fn->is_async = (is_async != 0);
    fn->upvalue_count = upvalue_count;

    uint32_t code_count = 0;
    if (fread(&code_count, sizeof(uint32_t), 1, f) != 1) return NULL;

    if (code_count > 0) {
        fn->chunk.code = (uint8_t*)malloc(code_count * sizeof(uint8_t));
        fn->chunk.lines = (int*)malloc(code_count * sizeof(int));
        if (!fn->chunk.code || !fn->chunk.lines) return NULL;

        if (fread(fn->chunk.code, sizeof(uint8_t), code_count, f) != code_count) return NULL;
        if (fread(fn->chunk.lines, sizeof(int), code_count, f) != code_count) return NULL;
        fn->chunk.code_count = code_count;
        fn->chunk.code_capacity = code_count;
    }

    uint32_t const_count = 0;
    if (fread(&const_count, sizeof(uint32_t), 1, f) != 1) return NULL;

    for (size_t i = 0; i < const_count; ++i) {
        uint8_t tag = 0;
        if (fread(&tag, sizeof(uint8_t), 1, f) != 1) return NULL;

        if (tag == 0) {
            uf_chunk_add_constant(&fn->chunk, uf_val_null());
        } else if (tag == 1) {
            uint8_t b = 0;
            if (fread(&b, sizeof(uint8_t), 1, f) != 1) return NULL;
            uf_chunk_add_constant(&fn->chunk, uf_val_bool(b != 0));
        } else if (tag == 2) {
            double num = 0.0;
            if (fread(&num, sizeof(double), 1, f) != 1) return NULL;
            uf_chunk_add_constant(&fn->chunk, uf_val_number(num));
        } else if (tag == 3) {
            uint32_t slen = 0;
            if (fread(&slen, sizeof(uint32_t), 1, f) != 1) return NULL;
            char* sbuf = (char*)malloc(slen + 1);
            if (!sbuf) return NULL;
            if (slen > 0) {
                if (fread(sbuf, 1, slen, f) != slen) { free(sbuf); return NULL; }
            }
            sbuf[slen] = '\0';
            UfValue sval = uf_val_string(rt, sbuf, slen);
            free(sbuf);
            uf_chunk_add_constant(&fn->chunk, sval);
        } else if (tag == 4) {
            UfBytecodeFunction* child = read_bytecode_fn(f, rt);
            if (!child) return NULL;
            uf_chunk_add_constant(&fn->chunk, uf_val_bytecode_fn(rt, child));
        } else {
            uf_chunk_add_constant(&fn->chunk, uf_val_null());
        }
    }

    return fn;
}

static bool read_imports(FILE* f, UfRuntime* rt) {
    uint32_t count = 0;
    if (fread(&count, sizeof(uint32_t), 1, f) != 1) return false;
    if (count == 0) return true;

    SourceSpan dummy_span;
    memset(&dummy_span, 0, sizeof(dummy_span));
    dummy_span.start.file = "<import>";
    dummy_span.start.line = 1;
    dummy_span.end.file = "<import>";
    dummy_span.end.line = 1;

    for (uint32_t i = 0; i < count; ++i) {
        uint8_t is_from = 0;
        if (fread(&is_from, sizeof(uint8_t), 1, f) != 1) return false;

        uint32_t mod_len = 0;
        if (fread(&mod_len, sizeof(uint32_t), 1, f) != 1) return false;
        char* mod_buf = (char*)malloc(mod_len + 1);
        if (!mod_buf) return false;
        if (mod_len > 0 && fread(mod_buf, 1, mod_len, f) != mod_len) { free(mod_buf); return false; }
        mod_buf[mod_len] = '\0';

        if (is_from == 0) {
            uint32_t alias_len = 0;
            if (fread(&alias_len, sizeof(uint32_t), 1, f) != 1) { free(mod_buf); return false; }
            char* alias_buf = (char*)malloc(alias_len + 1);
            if (!alias_buf) { free(mod_buf); return false; }
            if (alias_len > 0 && fread(alias_buf, 1, alias_len, f) != alias_len) {
                free(mod_buf); free(alias_buf); return false;
            }
            alias_buf[alias_len] = '\0';

            UfModuleObject* mod = uf_module_load(rt, mod_buf, dummy_span);
            if (!mod || rt->had_runtime_error) {
                rt->had_runtime_error = false;
                free(mod_buf); free(alias_buf); return false;
            }

            const char* bound = (alias_len > 0) ? alias_buf : mod_buf;
            UfValue name_val = uf_val_string(rt, bound, strlen(bound));
            uf_env_declare(rt->global_env, name_val.as.string->chars, uf_val_module(rt, mod));

            free(mod_buf);
            free(alias_buf);
        } else {
            uint32_t sym_count = 0;
            if (fread(&sym_count, sizeof(uint32_t), 1, f) != 1) { free(mod_buf); return false; }

            UfModuleObject* mod = uf_module_load(rt, mod_buf, dummy_span);
            if (!mod || rt->had_runtime_error) {
                rt->had_runtime_error = false;
                free(mod_buf); return false;
            }

            for (uint32_t s = 0; s < sym_count; ++s) {
                uint32_t sym_len = 0;
                if (fread(&sym_len, sizeof(uint32_t), 1, f) != 1) { free(mod_buf); return false; }
                char* sym_buf = (char*)malloc(sym_len + 1);
                if (!sym_buf) { free(mod_buf); return false; }
                if (sym_len > 0 && fread(sym_buf, 1, sym_len, f) != sym_len) {
                    free(mod_buf); free(sym_buf); return false;
                }
                sym_buf[sym_len] = '\0';

                uint32_t alias_len = 0;
                if (fread(&alias_len, sizeof(uint32_t), 1, f) != 1) {
                    free(mod_buf); free(sym_buf); return false;
                }
                char* alias_buf = (char*)malloc(alias_len + 1);
                if (!alias_buf) { free(mod_buf); free(sym_buf); return false; }
                if (alias_len > 0 && fread(alias_buf, 1, alias_len, f) != alias_len) {
                    free(mod_buf); free(sym_buf); free(alias_buf); return false;
                }
                alias_buf[alias_len] = '\0';

                UfValue sym_key = uf_val_string(rt, sym_buf, sym_len);
                if (!uf_map_has(mod->exports.as.map, sym_key)) {
                    free(mod_buf); free(sym_buf); free(alias_buf); return false;
                }
                UfValue val = uf_map_get(mod->exports.as.map, sym_key);
                const char* bound = (alias_len > 0) ? alias_buf : sym_buf;
                UfValue name_val = uf_val_string(rt, bound, strlen(bound));
                uf_env_declare(rt->global_env, name_val.as.string->chars, val);

                free(sym_buf);
                free(alias_buf);
            }
            free(mod_buf);
        }
    }
    return true;
}

UfBytecodeFunction* uf_cache_read_stack(UfRuntime* rt, const char* cache_path, uint64_t source_hash) {
    if (!cache_path) return NULL;

    FILE* f = fopen(cache_path, "rb");
    if (!f) return NULL;

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, UFC_MAGIC_STACK, 4) != 0) {
        fclose(f);
        return NULL;
    }

    uint32_t ver = 0;
    if (fread(&ver, sizeof(uint32_t), 1, f) != 1 || ver != UFC_VERSION) {
        fclose(f);
        return NULL;
    }

    uint64_t file_hash = 0;
    if (fread(&file_hash, sizeof(uint64_t), 1, f) != 1 || file_hash != source_hash) {
        fclose(f);
        return NULL;
    }

    if (!read_imports(f, rt)) {
        fclose(f);
        return NULL;
    }

    /* The functions and constants being read aren't reachable from any root
     * until the caller runs them, so a collection here would free them. */
    uf_gc_pause(rt);
    UfBytecodeFunction* fn = read_bytecode_fn(f, rt);
    uf_gc_resume(rt);
    fclose(f);
    return fn;
}

/* --- Register VM Serialization --- */

static bool write_reg_fn(FILE* f, UfRegFunction* fn) {
    if (!fn) return false;

    uint32_t name_len = fn->name ? (uint32_t)strlen(fn->name) : 0;
    if (fwrite(&name_len, sizeof(uint32_t), 1, f) != 1) return false;
    if (name_len > 0) {
        if (fwrite(fn->name, 1, name_len, f) != name_len) return false;
    }

    uint32_t arity = (uint32_t)fn->arity;
    uint32_t min_arity = (uint32_t)fn->min_arity;
    uint8_t has_rest = fn->has_rest ? 1 : 0;
    uint8_t is_async = fn->is_async ? 1 : 0;
    uint8_t max_regs = fn->max_regs;
    uint32_t upvalue_count = (uint32_t)fn->upvalue_count;

    if (fwrite(&arity, sizeof(uint32_t), 1, f) != 1) return false;
    if (fwrite(&min_arity, sizeof(uint32_t), 1, f) != 1) return false;
    if (fwrite(&has_rest, sizeof(uint8_t), 1, f) != 1) return false;
    if (fwrite(&is_async, sizeof(uint8_t), 1, f) != 1) return false;
    if (fwrite(&max_regs, sizeof(uint8_t), 1, f) != 1) return false;
    if (fwrite(&upvalue_count, sizeof(uint32_t), 1, f) != 1) return false;

    for (size_t i = 0; i < upvalue_count; ++i) {
        uint8_t is_local = fn->upvalues[i].is_local;
        uint8_t idx = fn->upvalues[i].index;
        if (fwrite(&is_local, sizeof(uint8_t), 1, f) != 1) return false;
        if (fwrite(&idx, sizeof(uint8_t), 1, f) != 1) return false;
    }

    uint32_t code_count = (uint32_t)fn->chunk.code_count;
    if (fwrite(&code_count, sizeof(uint32_t), 1, f) != 1) return false;
    if (code_count > 0) {
        if (fwrite(fn->chunk.code, sizeof(uint32_t), code_count, f) != code_count) return false;
        if (fwrite(fn->chunk.lines, sizeof(int), code_count, f) != code_count) return false;
    }

    uint32_t const_count = (uint32_t)fn->chunk.const_count;
    if (fwrite(&const_count, sizeof(uint32_t), 1, f) != 1) return false;

    for (size_t i = 0; i < const_count; ++i) {
        UfValue v = fn->chunk.constants[i];
        if (v.kind == UF_VAL_NULL) {
            uint8_t tag = 0;
            if (fwrite(&tag, sizeof(uint8_t), 1, f) != 1) return false;
        } else if (v.kind == UF_VAL_BOOL) {
            uint8_t tag = 1;
            uint8_t b = v.as.boolean ? 1 : 0;
            if (fwrite(&tag, sizeof(uint8_t), 1, f) != 1) return false;
            if (fwrite(&b, sizeof(uint8_t), 1, f) != 1) return false;
        } else if (v.kind == UF_VAL_NUMBER) {
            uint8_t tag = 2;
            double num = v.as.number;
            if (fwrite(&tag, sizeof(uint8_t), 1, f) != 1) return false;
            if (fwrite(&num, sizeof(double), 1, f) != 1) return false;
        } else if (v.kind == UF_VAL_STRING && v.as.string) {
            uint8_t tag = 3;
            uint32_t slen = (uint32_t)v.as.string->length;
            if (fwrite(&tag, sizeof(uint8_t), 1, f) != 1) return false;
            if (fwrite(&slen, sizeof(uint32_t), 1, f) != 1) return false;
            if (slen > 0) {
                if (fwrite(v.as.string->chars, 1, slen, f) != slen) return false;
            }
        } else if (v.kind == UF_VAL_REG_FN && v.as.reg_fn) {
            uint8_t tag = 5;
            if (fwrite(&tag, sizeof(uint8_t), 1, f) != 1) return false;
            if (!write_reg_fn(f, v.as.reg_fn)) return false;
        } else {
            return false;
        }
    }
    return true;
}

bool uf_cache_write_reg(const char* cache_path, UfProgram* program, UfRegFunction* fn, uint64_t source_hash) {
    if (!cache_path || !fn) return false;

    FILE* f = fopen(cache_path, "wb");
    if (!f) return false;

    if (fwrite(UFC_MAGIC_REG, 1, 4, f) != 4) { fclose(f); return false; }
    uint32_t ver = UFC_VERSION;
    if (fwrite(&ver, sizeof(uint32_t), 1, f) != 1) { fclose(f); return false; }
    if (fwrite(&source_hash, sizeof(uint64_t), 1, f) != 1) { fclose(f); return false; }

    if (!write_imports(f, program)) { fclose(f); return false; }
    bool ok = write_reg_fn(f, fn);
    fclose(f);
    return ok;
}

static UfRegFunction* read_reg_fn(FILE* f, UfRuntime* rt) {
    uint32_t name_len = 0;
    if (fread(&name_len, sizeof(uint32_t), 1, f) != 1) return NULL;

    char* name_buf = NULL;
    if (name_len > 0) {
        name_buf = (char*)malloc(name_len + 1);
        if (!name_buf) return NULL;
        if (fread(name_buf, 1, name_len, f) != name_len) {
            free(name_buf);
            return NULL;
        }
        name_buf[name_len] = '\0';
    }

    uint32_t arity = 0;
    uint32_t min_arity = 0;
    uint8_t has_rest = 0;
    uint8_t is_async = 0;
    uint8_t max_regs = 0;
    uint32_t upvalue_count = 0;

    if (fread(&arity, sizeof(uint32_t), 1, f) != 1) { free(name_buf); return NULL; }
    if (fread(&min_arity, sizeof(uint32_t), 1, f) != 1) { free(name_buf); return NULL; }
    if (fread(&has_rest, sizeof(uint8_t), 1, f) != 1) { free(name_buf); return NULL; }
    if (fread(&is_async, sizeof(uint8_t), 1, f) != 1) { free(name_buf); return NULL; }
    if (fread(&max_regs, sizeof(uint8_t), 1, f) != 1) { free(name_buf); return NULL; }
    if (fread(&upvalue_count, sizeof(uint32_t), 1, f) != 1) { free(name_buf); return NULL; }

    UfRegFunction* fn = uf_reg_fn_new(rt, name_buf, arity, min_arity, has_rest != 0);
    if (name_buf) free(name_buf);
    if (!fn) return NULL;

    fn->is_async = (is_async != 0);
    fn->max_regs = max_regs;
    fn->upvalue_count = upvalue_count;

    if (upvalue_count > 0) {
        fn->upvalues = (UfRegUpvalueDesc*)malloc(upvalue_count * sizeof(UfRegUpvalueDesc));
        if (!fn->upvalues) return NULL;
        for (size_t i = 0; i < upvalue_count; ++i) {
            uint8_t is_local = 0;
            uint8_t idx = 0;
            if (fread(&is_local, sizeof(uint8_t), 1, f) != 1) return NULL;
            if (fread(&idx, sizeof(uint8_t), 1, f) != 1) return NULL;
            fn->upvalues[i].is_local = is_local;
            fn->upvalues[i].index = idx;
        }
    }

    uint32_t code_count = 0;
    if (fread(&code_count, sizeof(uint32_t), 1, f) != 1) return NULL;

    if (code_count > 0) {
        fn->chunk.code = (uint32_t*)malloc(code_count * sizeof(uint32_t));
        fn->chunk.lines = (int*)malloc(code_count * sizeof(int));
        if (!fn->chunk.code || !fn->chunk.lines) return NULL;

        if (fread(fn->chunk.code, sizeof(uint32_t), code_count, f) != code_count) return NULL;
        if (fread(fn->chunk.lines, sizeof(int), code_count, f) != code_count) return NULL;
        fn->chunk.code_count = code_count;
        fn->chunk.code_capacity = code_count;
    }

    uint32_t const_count = 0;
    if (fread(&const_count, sizeof(uint32_t), 1, f) != 1) return NULL;

    for (size_t i = 0; i < const_count; ++i) {
        uint8_t tag = 0;
        if (fread(&tag, sizeof(uint8_t), 1, f) != 1) return NULL;

        if (tag == 0) {
            uf_reg_chunk_add_constant(&fn->chunk, uf_val_null());
        } else if (tag == 1) {
            uint8_t b = 0;
            if (fread(&b, sizeof(uint8_t), 1, f) != 1) return NULL;
            uf_reg_chunk_add_constant(&fn->chunk, uf_val_bool(b != 0));
        } else if (tag == 2) {
            double num = 0.0;
            if (fread(&num, sizeof(double), 1, f) != 1) return NULL;
            uf_reg_chunk_add_constant(&fn->chunk, uf_val_number(num));
        } else if (tag == 3) {
            uint32_t slen = 0;
            if (fread(&slen, sizeof(uint32_t), 1, f) != 1) return NULL;
            char* sbuf = (char*)malloc(slen + 1);
            if (!sbuf) return NULL;
            if (slen > 0) {
                if (fread(sbuf, 1, slen, f) != slen) { free(sbuf); return NULL; }
            }
            sbuf[slen] = '\0';
            UfValue sval = uf_val_string(rt, sbuf, slen);
            free(sbuf);
            uf_reg_chunk_add_constant(&fn->chunk, sval);
        } else if (tag == 5) {
            UfRegFunction* child = read_reg_fn(f, rt);
            if (!child) return NULL;
            uf_reg_chunk_add_constant(&fn->chunk, uf_val_reg_fn(rt, child));
        } else {
            uf_reg_chunk_add_constant(&fn->chunk, uf_val_null());
        }
    }

    return fn;
}

UfRegFunction* uf_cache_read_reg(UfRuntime* rt, const char* cache_path, uint64_t source_hash) {
    if (!cache_path) return NULL;

    FILE* f = fopen(cache_path, "rb");
    if (!f) return NULL;

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, UFC_MAGIC_REG, 4) != 0) {
        fclose(f);
        return NULL;
    }

    uint32_t ver = 0;
    if (fread(&ver, sizeof(uint32_t), 1, f) != 1 || ver != UFC_VERSION) {
        fclose(f);
        return NULL;
    }

    uint64_t file_hash = 0;
    if (fread(&file_hash, sizeof(uint64_t), 1, f) != 1 || file_hash != source_hash) {
        fclose(f);
        return NULL;
    }

    if (!read_imports(f, rt)) {
        fclose(f);
        return NULL;
    }

    /* The functions and constants being read aren't reachable from any root
     * until the caller runs them, so a collection here would free them. */
    uf_gc_pause(rt);
    UfRegFunction* fn = read_reg_fn(f, rt);
    uf_gc_resume(rt);
    fclose(f);
    return fn;
}
