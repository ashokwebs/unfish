#include "uf_mod_fs.h"
#include "../runtime/uf_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

static UfValue fs_read_text(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        return uf_val_null();
    }
    const char* path = args[0].as.string->chars;
    FILE* f = fopen(path, "rb");
    if (!f) return uf_val_null();

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0) {
        fclose(f);
        return uf_val_null();
    }

    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return uf_val_null();
    }

    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);

    UfValue res = uf_val_string(rt, buf, rd);
    free(buf);
    return res;
}

static UfValue fs_write_text(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    if (argc < 2 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_STRING) {
        return uf_val_bool(false);
    }
    const char* path = args[0].as.string->chars;
    const char* content = args[1].as.string->chars;
    size_t len = args[1].as.string->length;

    FILE* f = fopen(path, "wb");
    if (!f) return uf_val_bool(false);

    size_t wr = fwrite(content, 1, len, f);
    fclose(f);
    return uf_val_bool(wr == len);
}

static UfValue fs_append_text(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    if (argc < 2 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_STRING) {
        return uf_val_bool(false);
    }
    const char* path = args[0].as.string->chars;
    const char* content = args[1].as.string->chars;
    size_t len = args[1].as.string->length;

    FILE* f = fopen(path, "ab");
    if (!f) return uf_val_bool(false);

    size_t wr = fwrite(content, 1, len, f);
    fclose(f);
    return uf_val_bool(wr == len);
}

static UfValue fs_exists(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        return uf_val_bool(false);
    }
    const char* path = args[0].as.string->chars;
    return uf_val_bool(access(path, F_OK) == 0);
}

static UfValue fs_delete_file(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        return uf_val_bool(false);
    }
    const char* path = args[0].as.string->chars;
    return uf_val_bool(unlink(path) == 0);
}

static UfValue fs_list_dir(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        return uf_val_null();
    }
    const char* path = args[0].as.string->chars;
    DIR* d = opendir(path);
    if (!d) return uf_val_null();

    UfValue arr = uf_val_array(rt, 8);
    uf_runtime_push_temp_root(rt, arr);

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        UfValue s = uf_val_string_cstr(rt, ent->d_name);
        uf_array_push(rt, arr.as.array, s);
    }
    closedir(d);
    uf_runtime_pop_temp_root(rt);
    return arr;
}

static UfValue fs_mkdir(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        return uf_val_bool(false);
    }
    const char* path = args[0].as.string->chars;
    return uf_val_bool(mkdir(path, 0755) == 0);
}

static UfValue fs_remove_dir(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        return uf_val_bool(false);
    }
    const char* path = args[0].as.string->chars;
    return uf_val_bool(rmdir(path) == 0);
}

static UfValue fs_is_file(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        return uf_val_bool(false);
    }
    const char* path = args[0].as.string->chars;
    struct stat st;
    if (stat(path, &st) != 0) return uf_val_bool(false);
    return uf_val_bool(S_ISREG(st.st_mode));
}

static UfValue fs_is_dir(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        return uf_val_bool(false);
    }
    const char* path = args[0].as.string->chars;
    struct stat st;
    if (stat(path, &st) != 0) return uf_val_bool(false);
    return uf_val_bool(S_ISDIR(st.st_mode));
}

static UfValue fs_file_size(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        return uf_val_null();
    }
    const char* path = args[0].as.string->chars;
    struct stat st;
    if (stat(path, &st) != 0) return uf_val_null();
    return uf_val_number((double)st.st_size);
}

UfModuleObject* uf_mod_fs_create(UfRuntime* rt) {
    UfModuleObject* mod = uf_module_create(rt, "fs", "<builtin:fs>");
    uf_runtime_push_temp_root(rt, uf_val_module(rt, mod));

    struct {
        const char* name;
        UfNativeFn fn;
        int arity;
    } fns[] = {
        { "read_text", fs_read_text, 1 },
        { "write_text", fs_write_text, 2 },
        { "append_text", fs_append_text, 2 },
        { "exists", fs_exists, 1 },
        { "delete_file", fs_delete_file, 1 },
        { "list_dir", fs_list_dir, 1 },
        { "mkdir", fs_mkdir, 1 },
        { "remove_dir", fs_remove_dir, 1 },
        { "is_file", fs_is_file, 1 },
        { "is_dir", fs_is_dir, 1 },
        { "file_size", fs_file_size, 1 }
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
