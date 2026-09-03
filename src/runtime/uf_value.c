#include "uf_value.h"
#include "uf_runtime.h"
#include "uf_env.h"
#include "uf_module.h"
#include "../compiler/uf_chunk.h"
#include <math.h>

UfValue uf_val_null(void) {
    UfValue v;
    v.kind = UF_VAL_NULL;
    v.as.number = 0;
    return v;
}

UfValue uf_val_bool(bool b) {
    UfValue v;
    v.kind = UF_VAL_BOOL;
    v.as.boolean = b;
    return v;
}

UfValue uf_val_number(double n) {
    UfValue v;
    v.kind = UF_VAL_NUMBER;
    v.as.number = n;
    return v;
}

UfValue uf_val_string(UfRuntime* rt, const char* chars, size_t len) {
    size_t size = sizeof(UfStringObject) + len + 1;
    UfStringObject* obj = (UfStringObject*)malloc(size);
    if (!obj) {
        fprintf(stderr, "Fatal error: Out of memory allocating string\n");
        abort();
    }
    obj->obj.kind = UF_OBJ_STRING;
    obj->obj.marked = false;
    obj->obj.next = NULL;
    obj->length = len;
    memcpy(obj->chars, chars, len);
    obj->chars[len] = '\0';

    if (rt) {
        uf_runtime_register_obj(rt, (UfObj*)obj, size);
    }

    UfValue v;
    v.kind = UF_VAL_STRING;
    v.as.string = obj;
    return v;
}

UfValue uf_val_string_take(UfRuntime* rt, char* chars, size_t len) {
    UfValue v = uf_val_string(rt, chars, len);
    free(chars);
    return v;
}

UfValue uf_val_string_cstr(UfRuntime* rt, const char* cstr) {
    return uf_val_string(rt, cstr, strlen(cstr));
}

UfValue uf_val_function(UfRuntime* rt, const char* name, const char** params, size_t param_count, struct UfStmt* body, UfEnv* closure_env) {
    size_t size = sizeof(UfFunctionObject);
    UfFunctionObject* fn = (UfFunctionObject*)malloc(size);
    if (!fn) {
        fprintf(stderr, "Fatal error: Out of memory allocating function\n");
        abort();
    }
    fn->obj.kind = UF_OBJ_FUNCTION;
    fn->obj.marked = false;
    fn->obj.next = NULL;
    fn->name = name;
    fn->params = params;
    fn->param_count = param_count;
    fn->body = body;
    fn->closure_env = closure_env;

    if (rt) {
        uf_runtime_register_obj(rt, (UfObj*)fn, size);
    }

    UfValue v;
    v.kind = UF_VAL_FUNCTION;
    v.as.function = fn;
    return v;
}

UfValue uf_val_native(const char* name, UfNativeFn fn, int arity) {
    UfValue v;
    v.kind = UF_VAL_NATIVE_FN;
    v.as.native_fn.name = name;
    v.as.native_fn.fn = fn;
    v.as.native_fn.arity = arity;
    return v;
}

UfValue uf_val_array(UfRuntime* rt, size_t initial_cap) {
    size_t cap = initial_cap < 4 ? 4 : initial_cap;
    UfArrayObject* arr = (UfArrayObject*)malloc(sizeof(UfArrayObject));
    if (!arr) {
        fprintf(stderr, "Fatal error: Out of memory allocating array\n");
        abort();
    }
    arr->obj.kind = UF_OBJ_ARRAY;
    arr->obj.marked = false;
    arr->obj.next = NULL;
    arr->count = 0;
    arr->capacity = cap;
    arr->elements = (UfValue*)calloc(cap, sizeof(UfValue));
    if (!arr->elements) {
        fprintf(stderr, "Fatal error: Out of memory allocating array elements\n");
        abort();
    }

    if (rt) {
        size_t size = sizeof(UfArrayObject) + cap * sizeof(UfValue);
        uf_runtime_register_obj(rt, (UfObj*)arr, size);
    }

    UfValue v;
    v.kind = UF_VAL_ARRAY;
    v.as.array = arr;
    return v;
}

void uf_array_push(UfRuntime* rt, UfArrayObject* arr, UfValue val) {
    if (arr->count + 1 > arr->capacity) {
        size_t new_cap = arr->capacity * 2;
        UfValue* new_elems = (UfValue*)realloc(arr->elements, new_cap * sizeof(UfValue));
        if (!new_elems) {
            fprintf(stderr, "Fatal error: Out of memory growing array\n");
            abort();
        }
        if (rt) {
            rt->bytes_allocated += (new_cap - arr->capacity) * sizeof(UfValue);
        }
        arr->elements = new_elems;
        arr->capacity = new_cap;
    }
    arr->elements[arr->count++] = val;
}

UfValue uf_array_pop(UfArrayObject* arr) {
    if (arr->count == 0) return uf_val_null();
    return arr->elements[--arr->count];
}

UfValue uf_array_get(UfArrayObject* arr, size_t index) {
    if (index >= arr->count) return uf_val_null();
    return arr->elements[index];
}

void uf_array_set(UfArrayObject* arr, size_t index, UfValue val) {
    if (index < arr->count) {
        arr->elements[index] = val;
    }
}

static uint32_t hash_value(UfValue val) {
    switch (val.kind) {
        case UF_VAL_STRING: {
            uint32_t hash = 2166136261u;
            const char* s = val.as.string->chars;
            size_t len = val.as.string->length;
            for (size_t i = 0; i < len; ++i) {
                hash ^= (uint8_t)s[i];
                hash *= 16777619u;
            }
            return hash;
        }
        case UF_VAL_NUMBER: {
            union { double d; uint64_t u; } u;
            u.d = (val.as.number == 0.0) ? 0.0 : val.as.number;
            uint32_t h = (uint32_t)(u.u ^ (u.u >> 32));
            h ^= 2166136261u;
            h *= 16777619u;
            return h;
        }
        case UF_VAL_BOOL:
            return val.as.boolean ? 1231u : 1237u;
        case UF_VAL_NULL:
            return 0u;
        default:
            return (uint32_t)(uintptr_t)val.as.string;
    }
}

UfValue uf_val_map(UfRuntime* rt, size_t initial_cap) {
    size_t cap = initial_cap < 8 ? 8 : initial_cap;
    size_t p = 8;
    while (p < cap) p *= 2;
    cap = p;

    UfMapObject* map = (UfMapObject*)malloc(sizeof(UfMapObject));
    if (!map) {
        fprintf(stderr, "Fatal error: Out of memory allocating map\n");
        abort();
    }
    map->obj.kind = UF_OBJ_MAP;
    map->obj.marked = false;
    map->obj.next = NULL;
    map->count = 0;
    map->capacity = cap;
    map->entries = (UfMapEntry*)calloc(cap, sizeof(UfMapEntry));
    if (!map->entries) {
        fprintf(stderr, "Fatal error: Out of memory allocating map entries\n");
        abort();
    }
    map->order_count = 0;
    map->order_capacity = cap;
    map->order_keys = (UfValue*)calloc(cap, sizeof(UfValue));
    if (!map->order_keys) {
        fprintf(stderr, "Fatal error: Out of memory allocating map order_keys\n");
        abort();
    }

    if (rt) {
        size_t size = sizeof(UfMapObject) + cap * sizeof(UfMapEntry) + cap * sizeof(UfValue);
        uf_runtime_register_obj(rt, (UfObj*)map, size);
    }

    UfValue v;
    v.kind = UF_VAL_MAP;
    v.as.map = map;
    return v;
}

static void map_resize(UfRuntime* rt, UfMapObject* map, size_t new_cap) {
    UfMapEntry* old_entries = map->entries;
    size_t old_cap = map->capacity;

    UfMapEntry* new_entries = (UfMapEntry*)calloc(new_cap, sizeof(UfMapEntry));
    if (!new_entries) {
        fprintf(stderr, "Fatal error: Out of memory resizing map\n");
        abort();
    }

    for (size_t i = 0; i < old_cap; ++i) {
        if (old_entries[i].occupied && !old_entries[i].tombstone) {
            uint32_t h = hash_value(old_entries[i].key);
            size_t idx = h & (new_cap - 1);
            while (new_entries[idx].occupied) {
                idx = (idx + 1) & (new_cap - 1);
            }
            new_entries[idx] = old_entries[i];
        }
    }

    if (rt) {
        rt->bytes_allocated += (new_cap - old_cap) * sizeof(UfMapEntry);
    }

    free(old_entries);
    map->entries = new_entries;
    map->capacity = new_cap;
}

bool uf_map_set(UfRuntime* rt, UfMapObject* map, UfValue key, UfValue val) {
    if ((map->count + 1) * 4 >= map->capacity * 3) {
        map_resize(rt, map, map->capacity * 2);
    }

    uint32_t h = hash_value(key);
    size_t idx = h & (map->capacity - 1);
    size_t tombstone_idx = (size_t)-1;

    while (map->entries[idx].occupied) {
        if (map->entries[idx].tombstone) {
            if (tombstone_idx == (size_t)-1) {
                tombstone_idx = idx;
            }
        } else if (uf_val_equal(map->entries[idx].key, key)) {
            map->entries[idx].value = val;
            return false;
        }
        idx = (idx + 1) & (map->capacity - 1);
    }

    size_t target_idx = (tombstone_idx != (size_t)-1) ? tombstone_idx : idx;
    map->entries[target_idx].key = key;
    map->entries[target_idx].value = val;
    map->entries[target_idx].occupied = true;
    map->entries[target_idx].tombstone = false;
    map->count++;

    if (map->order_count + 1 > map->order_capacity) {
        size_t new_cap = map->order_capacity * 2;
        UfValue* new_order = (UfValue*)realloc(map->order_keys, new_cap * sizeof(UfValue));
        if (!new_order) {
            fprintf(stderr, "Fatal error: Out of memory growing map order\n");
            abort();
        }
        if (rt) {
            rt->bytes_allocated += (new_cap - map->order_capacity) * sizeof(UfValue);
        }
        map->order_keys = new_order;
        map->order_capacity = new_cap;
    }
    map->order_keys[map->order_count++] = key;
    return true;
}

UfValue uf_map_get(UfMapObject* map, UfValue key) {
    if (!map || map->count == 0) return uf_val_null();

    uint32_t h = hash_value(key);
    size_t idx = h & (map->capacity - 1);

    while (map->entries[idx].occupied) {
        if (!map->entries[idx].tombstone && uf_val_equal(map->entries[idx].key, key)) {
            return map->entries[idx].value;
        }
        idx = (idx + 1) & (map->capacity - 1);
    }
    return uf_val_null();
}

bool uf_map_has(UfMapObject* map, UfValue key) {
    if (!map || map->count == 0) return false;

    uint32_t h = hash_value(key);
    size_t idx = h & (map->capacity - 1);

    while (map->entries[idx].occupied) {
        if (!map->entries[idx].tombstone && uf_val_equal(map->entries[idx].key, key)) {
            return true;
        }
        idx = (idx + 1) & (map->capacity - 1);
    }
    return false;
}

bool uf_map_delete(UfMapObject* map, UfValue key) {
    if (!map || map->count == 0) return false;

    uint32_t h = hash_value(key);
    size_t idx = h & (map->capacity - 1);

    while (map->entries[idx].occupied) {
        if (!map->entries[idx].tombstone && uf_val_equal(map->entries[idx].key, key)) {
            map->entries[idx].tombstone = true;
            map->count--;
            for (size_t i = 0; i < map->order_count; ++i) {
                if (uf_val_equal(map->order_keys[i], key)) {
                    for (size_t j = i; j + 1 < map->order_count; ++j) {
                        map->order_keys[j] = map->order_keys[j + 1];
                    }
                    map->order_count--;
                    break;
                }
            }
            return true;
        }
        idx = (idx + 1) & (map->capacity - 1);
    }
    return false;
}

UfValue uf_val_error(UfRuntime* rt, const char* message, const char* kind, SourceSpan span) {
    UfErrorObject* err = (UfErrorObject*)malloc(sizeof(UfErrorObject));
    if (!err) {
        fprintf(stderr, "Fatal error: Out of memory allocating error object\n");
        abort();
    }
    err->obj.kind = UF_OBJ_ERROR;
    err->obj.marked = false;
    err->obj.next = NULL;

    UfValue msg_val = uf_val_string(rt, message ? message : "", message ? strlen(message) : 0);
    err->message = msg_val.as.string;

    const char* k = kind ? kind : "Error";
    UfValue kind_val = uf_val_string(rt, k, strlen(k));
    err->kind = kind_val.as.string;

    err->line = (int)span.start.line;
    err->file = span.start.file ? span.start.file : "<unknown>";

    if (rt) {
        uf_runtime_register_obj(rt, (UfObj*)err, sizeof(UfErrorObject));
    }

    UfValue v;
    v.kind = UF_VAL_ERROR;
    v.as.error = err;
    return v;
}

UfValue uf_val_module(UfRuntime* rt, UfModuleObject* mod) {
    UF_UNUSED(rt);
    UfValue v;
    v.kind = UF_VAL_MODULE;
    v.as.module = mod;
    return v;
}

UfValue uf_val_struct_def(UfRuntime* rt, const char* name, const char** field_names, const char** field_types, size_t field_count) {
    size_t size = sizeof(UfStructDefObject);
    UfStructDefObject* sdef = (UfStructDefObject*)malloc(size);
    if (!sdef) {
        fprintf(stderr, "Fatal error: Out of memory allocating struct definition\n");
        abort();
    }
    sdef->obj.kind = UF_OBJ_STRUCT_DEF;
    sdef->obj.marked = false;
    sdef->obj.next = NULL;
    sdef->name = name;
    sdef->field_names = field_names;
    sdef->field_types = field_types;
    sdef->field_count = field_count;

    if (rt) {
        uf_runtime_register_obj(rt, (UfObj*)sdef, size);
    }

    UfValue v;
    v.kind = UF_VAL_STRUCT_DEF;
    v.as.struct_def = sdef;
    return v;
}

UfValue uf_val_instance(UfRuntime* rt, UfStructDefObject* def, UfValue* fields, size_t count) {
    size_t size = sizeof(UfInstanceObject);
    UfInstanceObject* inst = (UfInstanceObject*)malloc(size);
    if (!inst) {
        fprintf(stderr, "Fatal error: Out of memory allocating struct instance\n");
        abort();
    }
    inst->obj.kind = UF_OBJ_INSTANCE;
    inst->obj.marked = false;
    inst->obj.next = NULL;
    inst->def = def;
    inst->field_count = count;
    inst->fields = NULL;
    if (count > 0) {
        inst->fields = (UfValue*)malloc(count * sizeof(UfValue));
        if (!inst->fields) {
            fprintf(stderr, "Fatal error: Out of memory allocating struct instance fields\n");
            abort();
        }
        memcpy(inst->fields, fields, count * sizeof(UfValue));
    }

    if (rt) {
        uf_runtime_register_obj(rt, (UfObj*)inst, size + count * sizeof(UfValue));
    }

    UfValue v;
    v.kind = UF_VAL_INSTANCE;
    v.as.instance = inst;
    return v;
}

bool uf_val_is_truthy(UfValue val) {
    switch (val.kind) {
        case UF_VAL_NULL:
            return false;
        case UF_VAL_BOOL:
            return val.as.boolean;
        case UF_VAL_NUMBER:
            return val.as.number != 0.0 && !isnan(val.as.number);
        case UF_VAL_STRING:
            return val.as.string->length > 0;
        case UF_VAL_FUNCTION:
        case UF_VAL_NATIVE_FN:
            return true;
        case UF_VAL_ARRAY:
            return val.as.array->count > 0;
        case UF_VAL_MAP:
            return val.as.map->count > 0;
        case UF_VAL_ERROR:
        case UF_VAL_MODULE:
        case UF_VAL_STRUCT_DEF:
        case UF_VAL_INSTANCE:
        case UF_VAL_BYTECODE_FN:
            return true;
    }
    return false;
}

bool uf_val_equal(UfValue a, UfValue b) {
    if (a.kind != b.kind) return false;

    switch (a.kind) {
        case UF_VAL_NULL:
            return true;
        case UF_VAL_BOOL:
            return a.as.boolean == b.as.boolean;
        case UF_VAL_NUMBER:
            return a.as.number == b.as.number;
        case UF_VAL_STRING:
            if (a.as.string->length != b.as.string->length) return false;
            return memcmp(a.as.string->chars, b.as.string->chars, a.as.string->length) == 0;
        case UF_VAL_FUNCTION:
            return a.as.function == b.as.function;
        case UF_VAL_NATIVE_FN:
            return a.as.native_fn.fn == b.as.native_fn.fn;
        case UF_VAL_ARRAY: {
            if (a.as.array == b.as.array) return true;
            if (a.as.array->count != b.as.array->count) return false;
            for (size_t i = 0; i < a.as.array->count; ++i) {
                if (!uf_val_equal(a.as.array->elements[i], b.as.array->elements[i])) {
                    return false;
                }
            }
            return true;
        }
        case UF_VAL_MAP: {
            if (a.as.map == b.as.map) return true;
            if (a.as.map->count != b.as.map->count) return false;
            for (size_t i = 0; i < a.as.map->order_count; ++i) {
                UfValue k = a.as.map->order_keys[i];
                if (!uf_map_has(b.as.map, k)) return false;
                UfValue va = uf_map_get(a.as.map, k);
                UfValue vb = uf_map_get(b.as.map, k);
                if (!uf_val_equal(va, vb)) return false;
            }
            return true;
        }
        case UF_VAL_ERROR:
            return a.as.error == b.as.error;
        case UF_VAL_MODULE:
            return a.as.module == b.as.module;
        case UF_VAL_STRUCT_DEF:
            return a.as.struct_def == b.as.struct_def;
        case UF_VAL_INSTANCE: {
            if (a.as.instance->def != b.as.instance->def) return false;
            if (a.as.instance->field_count != b.as.instance->field_count) return false;
            for (size_t i = 0; i < a.as.instance->field_count; ++i) {
                if (!uf_val_equal(a.as.instance->fields[i], b.as.instance->fields[i])) {
                    return false;
                }
            }
            return true;
        }
        case UF_VAL_BYTECODE_FN:
            return a.as.bytecode_fn == b.as.bytecode_fn;
    }
    return false;
}

char* uf_val_to_string(UfValue val) {
    char buf[128];
    switch (val.kind) {
        case UF_VAL_NULL:
            return strdup("null");
        case UF_VAL_BOOL:
            return strdup(val.as.boolean ? "true" : "false");
        case UF_VAL_NUMBER: {
            double n = val.as.number;
            if (isnan(n)) {
                return strdup("nan");
            }
            if (isinf(n)) {
                return strdup(n > 0 ? "inf" : "-inf");
            }
            if (floor(n) == n && fabs(n) < 1e15) {
                snprintf(buf, sizeof(buf), "%.0f", n);
            } else {
                snprintf(buf, sizeof(buf), "%.14g", n);
            }
            return strdup(buf);
        }
        case UF_VAL_STRING:
            return strdup(val.as.string->chars);
        case UF_VAL_FUNCTION:
            snprintf(buf, sizeof(buf), "<function %s>", val.as.function->name ? val.as.function->name : "anonymous");
            return strdup(buf);
        case UF_VAL_NATIVE_FN:
            snprintf(buf, sizeof(buf), "<native function %s>", val.as.native_fn.name);
            return strdup(buf);
        case UF_VAL_ARRAY: {
            UfArrayObject* arr = val.as.array;
            size_t cap = 64;
            char* out = (char*)malloc(cap);
            out[0] = '[';
            out[1] = '\0';
            size_t len = 1;

            for (size_t i = 0; i < arr->count; ++i) {
                if (i > 0) {
                    if (len + 3 >= cap) {
                        cap *= 2;
                        out = (char*)realloc(out, cap);
                    }
                    strcat(out, ", ");
                    len += 2;
                }
                char* elem_s = uf_val_to_string(arr->elements[i]);
                size_t elen = strlen(elem_s);
                if (len + elen + 2 >= cap) {
                    cap = (len + elen + 2) * 2;
                    out = (char*)realloc(out, cap);
                }
                strcat(out, elem_s);
                len += elen;
                free(elem_s);
            }
            if (len + 2 >= cap) {
                cap += 2;
                out = (char*)realloc(out, cap);
            }
            strcat(out, "]");
            return out;
        }
        case UF_VAL_MAP: {
            UfMapObject* map = val.as.map;
            size_t cap = 64;
            char* out = (char*)malloc(cap);
            out[0] = '{';
            out[1] = '\0';
            size_t len = 1;

            for (size_t i = 0; i < map->order_count; ++i) {
                if (i > 0) {
                    if (len + 3 >= cap) {
                        cap *= 2;
                        out = (char*)realloc(out, cap);
                    }
                    strcat(out, ", ");
                    len += 2;
                }
                UfValue k = map->order_keys[i];
                UfValue v = uf_map_get(map, k);
                char* ks = (k.kind == UF_VAL_STRING) ? strdup(k.as.string->chars) : uf_val_to_string(k);
                char* vs = uf_val_to_string(v);

                size_t need = strlen(ks) + strlen(vs) + 8;
                if (len + need >= cap) {
                    cap = (len + need) * 2;
                    out = (char*)realloc(out, cap);
                }
                if (k.kind == UF_VAL_STRING) {
                    strcat(out, "\"");
                    strcat(out, ks);
                    strcat(out, "\": ");
                    len += strlen(ks) + 4;
                } else {
                    strcat(out, ks);
                    strcat(out, ": ");
                    len += strlen(ks) + 2;
                }
                strcat(out, vs);
                len += strlen(vs);

                free(ks);
                free(vs);
            }
            if (len + 2 >= cap) {
                cap += 2;
                out = (char*)realloc(out, cap);
            }
            strcat(out, "}");
            return out;
        }
        case UF_VAL_ERROR: {
            char ebuf[512];
            snprintf(ebuf, sizeof(ebuf), "[%s: %s]",
                     val.as.error->kind ? val.as.error->kind->chars : "Error",
                     val.as.error->message ? val.as.error->message->chars : "");
            return strdup(ebuf);
        }
        case UF_VAL_MODULE: {
            char mbuf[256];
            snprintf(mbuf, sizeof(mbuf), "<module '%s'>",
                     (val.as.module && val.as.module->name) ? val.as.module->name : "anonymous");
            return strdup(mbuf);
        }
        case UF_VAL_STRUCT_DEF: {
            char sbuf[256];
            snprintf(sbuf, sizeof(sbuf), "<struct %s>",
                     (val.as.struct_def && val.as.struct_def->name) ? val.as.struct_def->name : "anonymous");
            return strdup(sbuf);
        }
        case UF_VAL_INSTANCE: {
            UfInstanceObject* inst = val.as.instance;
            const char* sname = (inst->def && inst->def->name) ? inst->def->name : "Instance";
            size_t cap = 128;
            char* out = (char*)malloc(cap);
            snprintf(out, cap, "%s(", sname);
            size_t len = strlen(out);

            for (size_t i = 0; i < inst->field_count; ++i) {
                if (i > 0) {
                    if (len + 3 >= cap) {
                        cap *= 2;
                        out = (char*)realloc(out, cap);
                    }
                    strcat(out, ", ");
                    len += 2;
                }
                const char* fname = (inst->def && inst->def->field_names) ? inst->def->field_names[i] : "?";
                char* fval_s = uf_val_to_string(inst->fields[i]);
                size_t need = strlen(fname) + strlen(fval_s) + 4;
                if (len + need >= cap) {
                    cap = (len + need) * 2;
                    out = (char*)realloc(out, cap);
                }
                strcat(out, fname);
                strcat(out, ": ");
                strcat(out, fval_s);
                len += strlen(fname) + 2 + strlen(fval_s);
                free(fval_s);
            }
            if (len + 2 >= cap) {
                cap += 2;
                out = (char*)realloc(out, cap);
            }
            strcat(out, ")");
            return out;
        }
        case UF_VAL_BYTECODE_FN: {
            char fbuf[128];
            snprintf(fbuf, sizeof(fbuf), "<bytecode fn %s>",
                     (val.as.bytecode_fn && val.as.bytecode_fn->name) ? val.as.bytecode_fn->name : "anonymous");
            return strdup(fbuf);
        }
    }
    return strdup("<unknown>");
}

const char* uf_val_type_name(UfValue val) {
    switch (val.kind) {
        case UF_VAL_NULL:        return "null";
        case UF_VAL_BOOL:        return "boolean";
        case UF_VAL_NUMBER:      return "number";
        case UF_VAL_STRING:      return "string";
        case UF_VAL_FUNCTION:    return "function";
        case UF_VAL_NATIVE_FN:   return "function";
        case UF_VAL_BYTECODE_FN: return "function";
        case UF_VAL_ARRAY:       return "array";
        case UF_VAL_MAP:         return "map";
        case UF_VAL_ERROR:       return "error";
        case UF_VAL_MODULE:      return "module";
        case UF_VAL_STRUCT_DEF:  return "struct";
        case UF_VAL_INSTANCE:    return (val.as.instance && val.as.instance->def && val.as.instance->def->name) ? val.as.instance->def->name : "instance";
    }
    return "<unknown>";
}

UfValue uf_val_bytecode_fn(UfRuntime* rt, UfBytecodeFunction* fn) {
    (void)rt;
    UfValue v;
    v.kind = UF_VAL_BYTECODE_FN;
    v.as.bytecode_fn = fn;
    return v;
}

void uf_val_print(UfValue val, FILE* out) {
    char* str = uf_val_to_string(val);
    fprintf(out, "%s", str);
    free(str);
}
