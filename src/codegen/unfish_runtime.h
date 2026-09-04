#ifndef UNFISH_RUNTIME_H
#define UNFISH_RUNTIME_H

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>

#ifndef HUGE_VAL
#define HUGE_VAL (__builtin_huge_val())
#endif

typedef enum {
    UF_RT_NULL,
    UF_RT_BOOL,
    UF_RT_NUMBER,
    UF_RT_STRING,
    UF_RT_ARRAY,
    UF_RT_MAP,
    UF_RT_BUFFER,
    UF_RT_CLOSURE
} UfRtKind;

typedef struct UfRtHeader {
    struct UfRtHeader* next;
    UfRtKind kind;
} UfRtHeader;

typedef struct {
    UfRtHeader header;
    size_t length;
    char chars[];
} UfRtString;

typedef struct UfRtArray UfRtArray;
typedef struct UfRtMap UfRtMap;

typedef struct {
    UfRtHeader header;
    size_t size;
    uint8_t* data;
} UfRtBuffer;

typedef struct UfRtVal UfVal;
typedef UfVal (*UfRtNativeFn)(void* env, size_t argc, UfVal* args);

typedef struct {
    UfRtHeader header;
    UfRtNativeFn fn;
    void* env;
    size_t env_size;
} UfRtClosure;

typedef struct UfRtVal {
    UfRtKind kind;
    union {
        bool boolean;
        double number;
        UfRtString* string;
        UfRtArray* array;
        UfRtMap* map;
        UfRtBuffer* buffer;
        UfRtClosure* closure;
        void* ptr;
    } as;
} UfVal;

struct UfRtArray {
    UfRtHeader header;
    UfVal* elements;
    size_t count;
    size_t capacity;
};

typedef struct {
    UfVal key;
    UfVal value;
    bool occupied;
} UfRtMapEntry;

struct UfRtMap {
    UfRtHeader header;
    UfRtMapEntry* entries;
    size_t count;
    size_t capacity;
    UfVal* order_keys;
    size_t order_count;
};

/* Runtime context */
typedef struct {
    UfRtHeader* all_objects;
    int argc;
    char** argv;
} UfRtContext;

static UfRtContext g_uf_rt = { NULL, 0, NULL };

static void* uf_rt_alloc(UfRtKind kind, size_t size) {
    UfRtHeader* obj = (UfRtHeader*)malloc(size);
    if (!obj) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    obj->kind = kind;
    obj->next = g_uf_rt.all_objects;
    g_uf_rt.all_objects = obj;
    return obj;
}

static inline void uf_init(int argc, char** argv) {
    g_uf_rt.all_objects = NULL;
    g_uf_rt.argc = argc;
    g_uf_rt.argv = argv;
}

static inline void uf_cleanup(void) {
    UfRtHeader* curr = g_uf_rt.all_objects;
    while (curr) {
        UfRtHeader* next = curr->next;
        if (curr->kind == UF_RT_ARRAY) {
            UfRtArray* a = (UfRtArray*)curr;
            free(a->elements);
        } else if (curr->kind == UF_RT_MAP) {
            UfRtMap* m = (UfRtMap*)curr;
            free(m->entries);
            free(m->order_keys);
        } else if (curr->kind == UF_RT_BUFFER) {
            UfRtBuffer* b = (UfRtBuffer*)curr;
            free(b->data);
        } else if (curr->kind == UF_RT_CLOSURE) {
            UfRtClosure* cl = (UfRtClosure*)curr;
            free(cl->env);
        }
        free(curr);
        curr = next;
    }
    g_uf_rt.all_objects = NULL;
}

static inline UfVal uf_closure_new(UfRtNativeFn fn, const void* env, size_t env_size) {
    UfRtClosure* cl = (UfRtClosure*)uf_rt_alloc(UF_RT_CLOSURE, sizeof(UfRtClosure));
    cl->fn = fn;
    cl->env_size = env_size;
    if (env_size > 0 && env != NULL) {
        cl->env = malloc(env_size);
        memcpy(cl->env, env, env_size);
    } else {
        cl->env = NULL;
    }
    UfVal v; v.kind = UF_RT_CLOSURE; v.as.closure = cl; return v;
}

static inline UfVal uf_call_val(UfVal callee, size_t argc, ...) {
    va_list va;
    va_start(va, argc);
    UfVal args[16];
    for (size_t i = 0; i < argc && i < 16; ++i) {
        args[i] = va_arg(va, UfVal);
    }
    va_end(va);
    if (callee.kind == UF_RT_CLOSURE && callee.as.closure != NULL) {
        return callee.as.closure->fn(callee.as.closure->env, argc, args);
    }
    fprintf(stderr, "Runtime Error: Attempted to call non-callable value\n");
    exit(3);
}

/* Constructors */
static inline UfVal uf_null(void) {
    UfVal v; v.kind = UF_RT_NULL; v.as.number = 0.0; return v;
}

static inline UfVal uf_bool(bool b) {
    UfVal v; v.kind = UF_RT_BOOL; v.as.boolean = b; return v;
}

static inline UfVal uf_num(double n) {
    UfVal v; v.kind = UF_RT_NUMBER; v.as.number = n; return v;
}

static inline UfVal uf_str(const char* s) {
    size_t len = s ? strlen(s) : 0;
    UfRtString* str = (UfRtString*)uf_rt_alloc(UF_RT_STRING, sizeof(UfRtString) + len + 1);
    str->length = len;
    if (len > 0) memcpy(str->chars, s, len);
    str->chars[len] = '\0';
    UfVal v; v.kind = UF_RT_STRING; v.as.string = str; return v;
}

static inline UfVal uf_array_new(size_t capacity) {
    UfRtArray* arr = (UfRtArray*)uf_rt_alloc(UF_RT_ARRAY, sizeof(UfRtArray));
    arr->count = 0;
    arr->capacity = capacity < 4 ? 4 : capacity;
    arr->elements = (UfVal*)malloc(sizeof(UfVal) * arr->capacity);
    UfVal v; v.kind = UF_RT_ARRAY; v.as.array = arr; return v;
}

static inline void uf_array_push(UfVal arr, UfVal item) {
    if (arr.kind != UF_RT_ARRAY) return;
    UfRtArray* a = arr.as.array;
    if (a->count >= a->capacity) {
        a->capacity *= 2;
        a->elements = (UfVal*)realloc(a->elements, sizeof(UfVal) * a->capacity);
    }
    a->elements[a->count++] = item;
}

static inline UfVal uf_map_new(size_t capacity) {
    UfRtMap* m = (UfRtMap*)uf_rt_alloc(UF_RT_MAP, sizeof(UfRtMap));
    m->count = 0;
    m->capacity = capacity < 8 ? 8 : capacity;
    m->entries = (UfRtMapEntry*)calloc(m->capacity, sizeof(UfRtMapEntry));
    m->order_keys = (UfVal*)malloc(sizeof(UfVal) * m->capacity);
    m->order_count = 0;
    UfVal v; v.kind = UF_RT_MAP; v.as.map = m; return v;
}

static inline void uf_set(UfVal target, UfVal index, UfVal value);

static inline UfVal uf_make_array(size_t count, ...) {
    va_list args;
    va_start(args, count);
    UfVal res = uf_array_new(count);
    for (size_t i = 0; i < count; ++i) {
        UfVal item = va_arg(args, UfVal);
        uf_array_push(res, item);
    }
    va_end(args);
    return res;
}

static inline UfVal uf_make_map(size_t pair_count, ...) {
    va_list args;
    va_start(args, pair_count);
    UfVal res = uf_map_new(pair_count);
    for (size_t i = 0; i < pair_count; ++i) {
        UfVal k = va_arg(args, UfVal);
        UfVal v = va_arg(args, UfVal);
        uf_set(res, k, v);
    }
    va_end(args);
    return res;
}

/* Comparisons and Operations */
static inline bool uf_truthy(UfVal v) {
    switch (v.kind) {
        case UF_RT_NULL: return false;
        case UF_RT_BOOL: return v.as.boolean;
        case UF_RT_NUMBER: return v.as.number != 0.0;
        case UF_RT_STRING: return v.as.string->length > 0;
        case UF_RT_ARRAY: return v.as.array->count > 0;
        case UF_RT_MAP: return v.as.map->count > 0;
        case UF_RT_BUFFER: return v.as.buffer->size > 0;
        case UF_RT_CLOSURE: return true;
        default: return false;
    }
}

static inline char* uf_to_str(UfVal v) {
    char buf[128];
    switch (v.kind) {
        case UF_RT_NULL: return strdup("null");
        case UF_RT_BOOL: return strdup(v.as.boolean ? "true" : "false");
        case UF_RT_NUMBER: {
            if (floor(v.as.number) == v.as.number && !isnan(v.as.number) && !isinf(v.as.number)) {
                snprintf(buf, sizeof(buf), "%.0f", v.as.number);
            } else {
                snprintf(buf, sizeof(buf), "%.14g", v.as.number);
            }
            return strdup(buf);
        }
        case UF_RT_STRING: return strdup(v.as.string->chars);
        case UF_RT_BUFFER: {
            char b_buf[64];
            snprintf(b_buf, sizeof(b_buf), "<buffer size=%zu>", v.as.buffer->size);
            return strdup(b_buf);
        }
        case UF_RT_ARRAY: {
            size_t cap = 64;
            char* res = (char*)malloc(cap);
            strcpy(res, "[");
            for (size_t i = 0; i < v.as.array->count; ++i) {
                if (i > 0) {
                    if (strlen(res) + 3 >= cap) { cap *= 2; res = (char*)realloc(res, cap); }
                    strcat(res, ", ");
                }
                char* s = uf_to_str(v.as.array->elements[i]);
                if (strlen(res) + strlen(s) + 4 >= cap) {
                    cap = (cap + strlen(s)) * 2;
                    res = (char*)realloc(res, cap);
                }
                strcat(res, s);
                free(s);
            }
            strcat(res, "]");
            return res;
        }
        case UF_RT_MAP: {
            size_t cap = 64;
            char* res = (char*)malloc(cap);
            strcpy(res, "{");
            for (size_t i = 0; i < v.as.map->order_count; ++i) {
                if (i > 0) {
                    if (strlen(res) + 3 >= cap) { cap *= 2; res = (char*)realloc(res, cap); }
                    strcat(res, ", ");
                }
                char* k = uf_to_str(v.as.map->order_keys[i]);
                if (strlen(res) + strlen(k) + 6 >= cap) {
                    cap = (cap + strlen(k)) * 2;
                    res = (char*)realloc(res, cap);
                }
                strcat(res, k);
                strcat(res, ": ");
                free(k);
                /* Find value */
                for (size_t j = 0; j < v.as.map->capacity; ++j) {
                    if (v.as.map->entries[j].occupied &&
                        strcmp(v.as.map->entries[j].key.as.string->chars, v.as.map->order_keys[i].as.string->chars) == 0) {
                        char* val_s = uf_to_str(v.as.map->entries[j].value);
                        if (strlen(res) + strlen(val_s) + 4 >= cap) {
                            cap = (cap + strlen(val_s)) * 2;
                            res = (char*)realloc(res, cap);
                        }
                        strcat(res, val_s);
                        free(val_s);
                        break;
                    }
                }
            }
            strcat(res, "}");
            return res;
        }
        case UF_RT_CLOSURE: return strdup("<function>");
        default: return strdup("<object>");
    }
}

static inline void uf_say(UfVal v) {
    char* s = uf_to_str(v);
    printf("%s\n", s ? s : "null");
    free(s);
}

static inline void uf_print(UfVal v) {
    char* s = uf_to_str(v);
    printf("%s", s ? s : "null");
    free(s);
}

static inline bool uf_eq_bool(UfVal a, UfVal b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case UF_RT_NULL: return true;
        case UF_RT_BOOL: return a.as.boolean == b.as.boolean;
        case UF_RT_NUMBER: return a.as.number == b.as.number;
        case UF_RT_STRING: return strcmp(a.as.string->chars, b.as.string->chars) == 0;
        default: return a.as.ptr == b.as.ptr;
    }
}

static inline UfVal uf_eq(UfVal a, UfVal b) { return uf_bool(uf_eq_bool(a, b)); }
static inline UfVal uf_neq(UfVal a, UfVal b) { return uf_bool(!uf_eq_bool(a, b)); }

static inline UfVal uf_add(UfVal a, UfVal b) {
    if (a.kind == UF_RT_NUMBER && b.kind == UF_RT_NUMBER) {
        return uf_num(a.as.number + b.as.number);
    }
    if (a.kind == UF_RT_STRING || b.kind == UF_RT_STRING) {
        char* sa = uf_to_str(a);
        char* sb = uf_to_str(b);
        size_t len_a = strlen(sa);
        size_t len_b = strlen(sb);
        char* combined = (char*)malloc(len_a + len_b + 1);
        memcpy(combined, sa, len_a);
        memcpy(combined + len_a, sb, len_b);
        combined[len_a + len_b] = '\0';
        UfVal res = uf_str(combined);
        free(combined);
        free(sa);
        free(sb);
        return res;
    }
    if (a.kind == UF_RT_ARRAY && b.kind == UF_RT_ARRAY) {
        UfVal res = uf_array_new(a.as.array->count + b.as.array->count);
        for (size_t i = 0; i < a.as.array->count; ++i) uf_array_push(res, a.as.array->elements[i]);
        for (size_t i = 0; i < b.as.array->count; ++i) uf_array_push(res, b.as.array->elements[i]);
        return res;
    }
    fprintf(stderr, "Runtime Error: Cannot add types\n");
    exit(3);
}

static inline UfVal uf_sub(UfVal a, UfVal b) {
    if (a.kind == UF_RT_NUMBER && b.kind == UF_RT_NUMBER) return uf_num(a.as.number - b.as.number);
    fprintf(stderr, "Runtime Error: Operands to '-' must be numbers\n"); exit(3);
}

static inline UfVal uf_mul(UfVal a, UfVal b) {
    if (a.kind == UF_RT_NUMBER && b.kind == UF_RT_NUMBER) return uf_num(a.as.number * b.as.number);
    fprintf(stderr, "Runtime Error: Operands to '*' must be numbers\n"); exit(3);
}

static inline UfVal uf_div(UfVal a, UfVal b) {
    if (a.kind == UF_RT_NUMBER && b.kind == UF_RT_NUMBER) {
        if (b.as.number == 0.0) { fprintf(stderr, "Runtime Error: Division by zero\n"); exit(3); }
        return uf_num(a.as.number / b.as.number);
    }
    fprintf(stderr, "Runtime Error: Operands to '/' must be numbers\n"); exit(3);
}

static inline UfVal uf_mod(UfVal a, UfVal b) {
    if (a.kind == UF_RT_NUMBER && b.kind == UF_RT_NUMBER) {
        if (b.as.number == 0.0) { fprintf(stderr, "Runtime Error: Division by zero\n"); exit(3); }
        return uf_num(fmod(a.as.number, b.as.number));
    }
    fprintf(stderr, "Runtime Error: Operands to '%%' must be numbers\n"); exit(3);
}

static inline UfVal uf_neg(UfVal a) {
    if (a.kind == UF_RT_NUMBER) return uf_num(-a.as.number);
    fprintf(stderr, "Runtime Error: Operand to '-' must be a number\n"); exit(3);
}

static inline UfVal uf_not(UfVal a) {
    return uf_bool(!uf_truthy(a));
}

static inline UfVal uf_lt(UfVal a, UfVal b) {
    if (a.kind == UF_RT_NUMBER && b.kind == UF_RT_NUMBER) return uf_bool(a.as.number < b.as.number);
    if (a.kind == UF_RT_STRING && b.kind == UF_RT_STRING) return uf_bool(strcmp(a.as.string->chars, b.as.string->chars) < 0);
    fprintf(stderr, "Runtime Error: Operands must be comparable\n"); exit(3);
}

static inline UfVal uf_lte(UfVal a, UfVal b) {
    if (a.kind == UF_RT_NUMBER && b.kind == UF_RT_NUMBER) return uf_bool(a.as.number <= b.as.number);
    if (a.kind == UF_RT_STRING && b.kind == UF_RT_STRING) return uf_bool(strcmp(a.as.string->chars, b.as.string->chars) <= 0);
    fprintf(stderr, "Runtime Error: Operands must be comparable\n"); exit(3);
}

static inline UfVal uf_gt(UfVal a, UfVal b) {
    if (a.kind == UF_RT_NUMBER && b.kind == UF_RT_NUMBER) return uf_bool(a.as.number > b.as.number);
    if (a.kind == UF_RT_STRING && b.kind == UF_RT_STRING) return uf_bool(strcmp(a.as.string->chars, b.as.string->chars) > 0);
    fprintf(stderr, "Runtime Error: Operands must be comparable\n"); exit(3);
}

static inline UfVal uf_gte(UfVal a, UfVal b) {
    if (a.kind == UF_RT_NUMBER && b.kind == UF_RT_NUMBER) return uf_bool(a.as.number >= b.as.number);
    if (a.kind == UF_RT_STRING && b.kind == UF_RT_STRING) return uf_bool(strcmp(a.as.string->chars, b.as.string->chars) >= 0);
    fprintf(stderr, "Runtime Error: Operands must be comparable\n"); exit(3);
}

static inline UfVal uf_len(UfVal v) {
    if (v.kind == UF_RT_STRING) return uf_num((double)v.as.string->length);
    if (v.kind == UF_RT_ARRAY) return uf_num((double)v.as.array->count);
    if (v.kind == UF_RT_MAP) return uf_num((double)v.as.map->count);
    if (v.kind == UF_RT_BUFFER) return uf_num((double)v.as.buffer->size);
    fprintf(stderr, "Runtime Error: 'len()' expects string, array, map, or buffer\n"); exit(3);
}

static inline UfVal uf_type_of(UfVal v) {
    switch (v.kind) {
        case UF_RT_NULL: return uf_str("null");
        case UF_RT_BOOL: return uf_str("bool");
        case UF_RT_NUMBER: return uf_str("number");
        case UF_RT_STRING: return uf_str("string");
        case UF_RT_ARRAY: return uf_str("array");
        case UF_RT_MAP: return uf_str("map");
        case UF_RT_BUFFER: return uf_str("buffer");
        case UF_RT_CLOSURE: return uf_str("function");
        default: return uf_str("object");
    }
}

static inline UfVal uf_get(UfVal target, UfVal index) {
    if (target.kind == UF_RT_ARRAY) {
        if (index.kind != UF_RT_NUMBER) { fprintf(stderr, "Runtime Error: Array index must be a number\n"); exit(3); }
        long idx = (long)index.as.number;
        if (idx < 0) idx += target.as.array->count;
        if (idx < 0 || (size_t)idx >= target.as.array->count) {
            fprintf(stderr, "Runtime Error: IndexOutOfBounds: Index %ld out of bounds for array of length %zu\n", idx, target.as.array->count);
            exit(3);
        }
        return target.as.array->elements[idx];
    }
    if (target.kind == UF_RT_STRING) {
        if (index.kind != UF_RT_NUMBER) { fprintf(stderr, "Runtime Error: String index must be a number\n"); exit(3); }
        long idx = (long)index.as.number;
        if (idx < 0) idx += target.as.string->length;
        if (idx < 0 || (size_t)idx >= target.as.string->length) {
            fprintf(stderr, "Runtime Error: String index out of bounds\n"); exit(3);
        }
        char ch[2] = { target.as.string->chars[idx], '\0' };
        return uf_str(ch);
    }
    if (target.kind == UF_RT_BUFFER) {
        if (index.kind != UF_RT_NUMBER) { fprintf(stderr, "Runtime Error: Buffer index must be a number\n"); exit(3); }
        long idx = (long)index.as.number;
        if (idx < 0) idx += target.as.buffer->size;
        if (idx < 0 || (size_t)idx >= target.as.buffer->size) {
            fprintf(stderr, "Runtime Error: IndexOutOfBounds: Index %ld out of bounds for buffer of size %zu\n", idx, target.as.buffer->size);
            exit(3);
        }
        return uf_num((double)target.as.buffer->data[idx]);
    }
    if (target.kind == UF_RT_MAP) {
        if (index.kind != UF_RT_STRING) return uf_null();
        for (size_t i = 0; i < target.as.map->capacity; ++i) {
            if (target.as.map->entries[i].occupied &&
                strcmp(target.as.map->entries[i].key.as.string->chars, index.as.string->chars) == 0) {
                return target.as.map->entries[i].value;
            }
        }
        return uf_null();
    }
    fprintf(stderr, "Runtime Error: Cannot index value of this type\n"); exit(3);
}

static inline void uf_set(UfVal target, UfVal index, UfVal value) {
    if (target.kind == UF_RT_ARRAY) {
        if (index.kind != UF_RT_NUMBER) { fprintf(stderr, "Runtime Error: Array index must be a number\n"); exit(3); }
        long idx = (long)index.as.number;
        if (idx < 0) idx += target.as.array->count;
        if (idx < 0 || (size_t)idx >= target.as.array->count) {
            fprintf(stderr, "Runtime Error: IndexOutOfBounds: Index %ld out of bounds for array of length %zu\n", idx, target.as.array->count);
            exit(3);
        }
        target.as.array->elements[idx] = value;
        return;
    }
    if (target.kind == UF_RT_BUFFER) {
        if (index.kind != UF_RT_NUMBER) { fprintf(stderr, "Runtime Error: Buffer index must be a number\n"); exit(3); }
        if (value.kind != UF_RT_NUMBER) { fprintf(stderr, "Runtime Error: Buffer byte value must be a number\n"); exit(3); }
        long idx = (long)index.as.number;
        if (idx < 0) idx += target.as.buffer->size;
        if (idx < 0 || (size_t)idx >= target.as.buffer->size) {
            fprintf(stderr, "Runtime Error: IndexOutOfBounds: Index %ld out of bounds for buffer of size %zu\n", idx, target.as.buffer->size);
            exit(3);
        }
        target.as.buffer->data[idx] = (uint8_t)(int64_t)value.as.number;
        return;
    }
    if (target.kind == UF_RT_MAP) {
        if (index.kind != UF_RT_STRING) { fprintf(stderr, "Runtime Error: Map key must be string\n"); exit(3); }
        UfRtMap* m = target.as.map;
        for (size_t i = 0; i < m->capacity; ++i) {
            if (m->entries[i].occupied && strcmp(m->entries[i].key.as.string->chars, index.as.string->chars) == 0) {
                m->entries[i].value = value;
                return;
            }
        }
        /* Insert */
        for (size_t i = 0; i < m->capacity; ++i) {
            if (!m->entries[i].occupied) {
                m->entries[i].occupied = true;
                m->entries[i].key = index;
                m->entries[i].value = value;
                m->count++;
                m->order_keys[m->order_count++] = index;
                return;
            }
        }
    }
    fprintf(stderr, "Runtime Error: Cannot assign to index of this type\n"); exit(3);
}

/* Buffer Primitives */
static inline UfVal uf_buffer_new(UfVal size_val) {
    double sz = (size_val.kind == UF_RT_NUMBER) ? size_val.as.number : 0;
    size_t size = sz > 0 ? (size_t)sz : 0;
    UfRtBuffer* buf = (UfRtBuffer*)uf_rt_alloc(UF_RT_BUFFER, sizeof(UfRtBuffer));
    buf->size = size;
    buf->data = (uint8_t*)calloc(size > 0 ? size : 1, sizeof(uint8_t));
    UfVal v; v.kind = UF_RT_BUFFER; v.as.buffer = buf; return v;
}

static inline UfVal uf_buffer_from_string(UfVal s) {
    if (s.kind != UF_RT_STRING) return uf_buffer_new(uf_num(0));
    size_t len = s.as.string->length;
    UfVal b = uf_buffer_new(uf_num((double)len));
    if (len > 0) memcpy(b.as.buffer->data, s.as.string->chars, len);
    return b;
}

static inline UfVal uf_buffer_to_string(UfVal b) {
    if (b.kind != UF_RT_BUFFER) return uf_str("");
    UfRtBuffer* buf = b.as.buffer;
    UfRtString* str = (UfRtString*)uf_rt_alloc(UF_RT_STRING, sizeof(UfRtString) + buf->size + 1);
    str->length = buf->size;
    if (buf->size > 0) memcpy(str->chars, buf->data, buf->size);
    str->chars[buf->size] = '\0';
    UfVal v; v.kind = UF_RT_STRING; v.as.string = str; return v;
}

static inline UfVal uf_buffer_size(UfVal b) {
    if (b.kind != UF_RT_BUFFER) return uf_num(0);
    return uf_num((double)b.as.buffer->size);
}

static inline UfVal uf_buffer_get(UfVal b, UfVal idx) {
    return uf_get(b, idx);
}

static inline UfVal uf_buffer_set(UfVal b, UfVal idx, UfVal val) {
    uf_set(b, idx, val);
    return val;
}

static inline UfVal uf_buffer_fill(UfVal b, UfVal val) {
    if (b.kind != UF_RT_BUFFER || val.kind != UF_RT_NUMBER) return uf_null();
    uint8_t byte = (uint8_t)(int64_t)val.as.number;
    memset(b.as.buffer->data, byte, b.as.buffer->size);
    return b;
}

static inline UfVal uf_buffer_slice(UfVal b, UfVal start_v, UfVal end_v) {
    if (b.kind != UF_RT_BUFFER) return uf_null();
    UfRtBuffer* buf = b.as.buffer;
    long start = (start_v.kind == UF_RT_NUMBER) ? (long)start_v.as.number : 0;
    long end = (end_v.kind == UF_RT_NUMBER) ? (long)end_v.as.number : (long)buf->size;
    if (start < 0) start = 0;
    if (end > (long)buf->size) end = (long)buf->size;
    if (start > end) start = end;
    size_t count = (size_t)(end - start);
    UfVal res = uf_buffer_new(uf_num((double)count));
    if (count > 0) memcpy(res.as.buffer->data, buf->data + start, count);
    return res;
}

static inline UfVal uf_buffer_read_u16_le(UfVal b, UfVal off) {
    if (b.kind != UF_RT_BUFFER || off.kind != UF_RT_NUMBER) return uf_null();
    long o = (long)off.as.number;
    if (o < 0 || (size_t)(o + 2) > b.as.buffer->size) { fprintf(stderr, "IndexOutOfBounds\n"); exit(3); }
    uint16_t v = (uint16_t)(b.as.buffer->data[o] | (b.as.buffer->data[o+1] << 8));
    return uf_num((double)v);
}

static inline UfVal uf_buffer_write_u16_le(UfVal b, UfVal off, UfVal val) {
    if (b.kind != UF_RT_BUFFER || off.kind != UF_RT_NUMBER || val.kind != UF_RT_NUMBER) return uf_null();
    long o = (long)off.as.number;
    if (o < 0 || (size_t)(o + 2) > b.as.buffer->size) { fprintf(stderr, "IndexOutOfBounds\n"); exit(3); }
    uint16_t v = (uint16_t)(int64_t)val.as.number;
    b.as.buffer->data[o] = (uint8_t)(v & 0xff);
    b.as.buffer->data[o+1] = (uint8_t)((v >> 8) & 0xff);
    return val;
}

static inline UfVal uf_buffer_read_u32_le(UfVal b, UfVal off) {
    if (b.kind != UF_RT_BUFFER || off.kind != UF_RT_NUMBER) return uf_null();
    long o = (long)off.as.number;
    if (o < 0 || (size_t)(o + 4) > b.as.buffer->size) { fprintf(stderr, "IndexOutOfBounds\n"); exit(3); }
    uint32_t v = (uint32_t)(b.as.buffer->data[o] | (b.as.buffer->data[o+1] << 8) | (b.as.buffer->data[o+2] << 16) | (b.as.buffer->data[o+3] << 24));
    return uf_num((double)v);
}

static inline UfVal uf_buffer_write_u32_le(UfVal b, UfVal off, UfVal val) {
    if (b.kind != UF_RT_BUFFER || off.kind != UF_RT_NUMBER || val.kind != UF_RT_NUMBER) return uf_null();
    long o = (long)off.as.number;
    if (o < 0 || (size_t)(o + 4) > b.as.buffer->size) { fprintf(stderr, "IndexOutOfBounds\n"); exit(3); }
    uint32_t v = (uint32_t)(int64_t)val.as.number;
    b.as.buffer->data[o] = (uint8_t)(v & 0xff);
    b.as.buffer->data[o+1] = (uint8_t)((v >> 8) & 0xff);
    b.as.buffer->data[o+2] = (uint8_t)((v >> 16) & 0xff);
    b.as.buffer->data[o+3] = (uint8_t)((v >> 24) & 0xff);
    return val;
}

static inline UfVal uf_buffer_read_i32_le(UfVal b, UfVal off) {
    if (b.kind != UF_RT_BUFFER || off.kind != UF_RT_NUMBER) return uf_null();
    long o = (long)off.as.number;
    if (o < 0 || (size_t)(o + 4) > b.as.buffer->size) { fprintf(stderr, "IndexOutOfBounds\n"); exit(3); }
    int32_t v = (int32_t)((uint32_t)(b.as.buffer->data[o] | (b.as.buffer->data[o+1] << 8) | (b.as.buffer->data[o+2] << 16) | (b.as.buffer->data[o+3] << 24)));
    return uf_num((double)v);
}

static inline UfVal uf_buffer_write_i32_le(UfVal b, UfVal off, UfVal val) {
    return uf_buffer_write_u32_le(b, off, val);
}

static inline UfVal uf_u8(UfVal n) { return uf_num((double)((uint8_t)(int64_t)n.as.number)); }
static inline UfVal uf_i8(UfVal n) { return uf_num((double)((int8_t)(int64_t)n.as.number)); }
static inline UfVal uf_u16(UfVal n) { return uf_num((double)((uint16_t)(int64_t)n.as.number)); }
static inline UfVal uf_i16(UfVal n) { return uf_num((double)((int16_t)(int64_t)n.as.number)); }
static inline UfVal uf_u32(UfVal n) { return uf_num((double)((uint32_t)(int64_t)n.as.number)); }
static inline UfVal uf_i32(UfVal n) { return uf_num((double)((int32_t)(int64_t)n.as.number)); }

static inline UfVal uf_band(UfVal a, UfVal b) { return uf_num((double)(((uint32_t)(int64_t)a.as.number) & ((uint32_t)(int64_t)b.as.number))); }
static inline UfVal uf_bor(UfVal a, UfVal b) { return uf_num((double)(((uint32_t)(int64_t)a.as.number) | ((uint32_t)(int64_t)b.as.number))); }
static inline UfVal uf_bxor(UfVal a, UfVal b) { return uf_num((double)(((uint32_t)(int64_t)a.as.number) ^ ((uint32_t)(int64_t)b.as.number))); }
static inline UfVal uf_bnot(UfVal a) { return uf_num((double)(~((uint32_t)(int64_t)a.as.number))); }
static inline UfVal uf_shl(UfVal a, UfVal b) { return uf_num((double)(((uint32_t)(int64_t)a.as.number) << (((uint32_t)(int64_t)b.as.number) & 31))); }
static inline UfVal uf_shr(UfVal a, UfVal b) { return uf_num((double)(((uint32_t)(int64_t)a.as.number) >> (((uint32_t)(int64_t)b.as.number) & 31))); }
static inline UfVal uf_sar(UfVal a, UfVal b) { return uf_num((double)(((int32_t)(int64_t)a.as.number) >> (((uint32_t)(int64_t)b.as.number) & 31))); }

static inline UfVal uf_to_hex(UfVal n) {
    char hbuf[32];
    snprintf(hbuf, sizeof(hbuf), "%lx", (unsigned long)(uint64_t)(int64_t)n.as.number);
    return uf_str(hbuf);
}

static inline UfVal uf_from_hex(UfVal s) {
    if (s.kind != UF_RT_STRING) return uf_num(0);
    const char* p = s.as.string->chars;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    return uf_num((double)strtoull(p, NULL, 16));
}

static inline UfVal uf_buffer_to_hex(UfVal b) {
    if (b.kind != UF_RT_BUFFER) return uf_str("");
    UfRtBuffer* buf = b.as.buffer;
    if (buf->size == 0) return uf_str("");
    char* hex = (char*)malloc(buf->size * 2 + 1);
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < buf->size; ++i) {
        hex[i * 2] = digits[(buf->data[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = digits[buf->data[i] & 0x0F];
    }
    hex[buf->size * 2] = '\0';
    UfVal res = uf_str(hex);
    free(hex);
    return res;
}

static inline int uf_hex_char(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static inline UfVal uf_buffer_from_hex(UfVal s) {
    if (s.kind != UF_RT_STRING) return uf_buffer_new(uf_num(0));
    const char* str = s.as.string->chars;
    size_t len = s.as.string->length;
    size_t buf_len = len / 2;
    UfVal b = uf_buffer_new(uf_num((double)buf_len));
    for (size_t i = 0; i < buf_len; ++i) {
        int hi = uf_hex_char(str[i * 2]);
        int lo = uf_hex_char(str[i * 2 + 1]);
        if (hi < 0 || lo < 0) break;
        b.as.buffer->data[i] = (uint8_t)((hi << 4) | lo);
    }
    return b;
}

/* Math Primitives */
static inline UfVal uf_math_abs(UfVal a) { return uf_num(fabs(a.as.number)); }
static inline UfVal uf_math_floor(UfVal a) { return uf_num(floor(a.as.number)); }
static inline UfVal uf_math_ceil(UfVal a) { return uf_num(ceil(a.as.number)); }
static inline UfVal uf_math_round(UfVal a) { return uf_num(round(a.as.number)); }
static inline UfVal uf_math_sqrt(UfVal a) { return uf_num(sqrt(a.as.number)); }
static inline UfVal uf_math_pow(UfVal a, UfVal b) { return uf_num(pow(a.as.number, b.as.number)); }
static inline UfVal uf_math_min(UfVal a, UfVal b) { return uf_num(fmin(a.as.number, b.as.number)); }
static inline UfVal uf_math_max(UfVal a, UfVal b) { return uf_num(fmax(a.as.number, b.as.number)); }
static inline UfVal uf_math_log(UfVal a) { return uf_num(log(a.as.number)); }
static inline UfVal uf_math_sin(UfVal a) { return uf_num(sin(a.as.number)); }
static inline UfVal uf_math_cos(UfVal a) { return uf_num(cos(a.as.number)); }
static inline UfVal uf_math_tan(UfVal a) { return uf_num(tan(a.as.number)); }
static inline UfVal uf_math_random(void) { return uf_num((double)rand() / ((double)RAND_MAX + 1.0)); }
static inline UfVal uf_math_random_int(UfVal min_v, UfVal max_v) {
    long mn = (long)min_v.as.number, mx = (long)max_v.as.number;
    if (mx < mn) { long t = mn; mn = mx; mx = t; }
    long span = mx - mn + 1;
    return uf_num((double)(mn + (rand() % span)));
}

/* String Primitives */
static inline UfVal uf_str_trim(UfVal s) {
    if (s.kind != UF_RT_STRING) return uf_str("");
    const char* str = s.as.string->chars;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    if (*str == '\0') return uf_str("");
    const char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    size_t len = (size_t)(end - str + 1);
    char* buf = (char*)malloc(len + 1);
    memcpy(buf, str, len);
    buf[len] = '\0';
    UfVal res = uf_str(buf);
    free(buf);
    return res;
}

static inline UfVal uf_str_to_upper(UfVal s) {
    if (s.kind != UF_RT_STRING) return uf_str("");
    size_t len = s.as.string->length;
    char* buf = (char*)malloc(len + 1);
    for (size_t i = 0; i < len; ++i) {
        char c = s.as.string->chars[i];
        buf[i] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
    }
    buf[len] = '\0';
    UfVal res = uf_str(buf);
    free(buf);
    return res;
}

static inline UfVal uf_str_to_lower(UfVal s) {
    if (s.kind != UF_RT_STRING) return uf_str("");
    size_t len = s.as.string->length;
    char* buf = (char*)malloc(len + 1);
    for (size_t i = 0; i < len; ++i) {
        char c = s.as.string->chars[i];
        buf[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
    }
    buf[len] = '\0';
    UfVal res = uf_str(buf);
    free(buf);
    return res;
}

static inline UfVal uf_str_contains(UfVal s, UfVal sub) {
    if (s.kind != UF_RT_STRING || sub.kind != UF_RT_STRING) return uf_bool(false);
    return uf_bool(strstr(s.as.string->chars, sub.as.string->chars) != NULL);
}

static inline UfVal uf_str_starts_with(UfVal s, UfVal prefix) {
    if (s.kind != UF_RT_STRING || prefix.kind != UF_RT_STRING) return uf_bool(false);
    if (prefix.as.string->length > s.as.string->length) return uf_bool(false);
    return uf_bool(memcmp(s.as.string->chars, prefix.as.string->chars, prefix.as.string->length) == 0);
}

static inline UfVal uf_str_ends_with(UfVal s, UfVal suffix) {
    if (s.kind != UF_RT_STRING || suffix.kind != UF_RT_STRING) return uf_bool(false);
    if (suffix.as.string->length > s.as.string->length) return uf_bool(false);
    size_t off = s.as.string->length - suffix.as.string->length;
    return uf_bool(memcmp(s.as.string->chars + off, suffix.as.string->chars, suffix.as.string->length) == 0);
}

static inline UfVal uf_str_char_at(UfVal s, UfVal idx) {
    return uf_get(s, idx);
}

static inline UfVal uf_str_to_number(UfVal s) {
    if (s.kind != UF_RT_STRING) return uf_null();
    char* endptr = NULL;
    double d = strtod(s.as.string->chars, &endptr);
    if (endptr == s.as.string->chars) return uf_null();
    return uf_num(d);
}

static inline UfVal uf_str_to_string(UfVal v) {
    char* s = uf_to_str(v);
    UfVal res = uf_str(s);
    free(s);
    return res;
}

static inline UfVal uf_str_repeat(UfVal s, UfVal count) {
    if (s.kind != UF_RT_STRING || count.kind != UF_RT_NUMBER) return uf_str("");
    long cnt = (long)count.as.number;
    if (cnt <= 0) return uf_str("");
    size_t slen = s.as.string->length;
    char* buf = (char*)malloc(slen * cnt + 1);
    for (long i = 0; i < cnt; ++i) memcpy(buf + i * slen, s.as.string->chars, slen);
    buf[slen * cnt] = '\0';
    UfVal res = uf_str(buf);
    free(buf);
    return res;
}

static inline UfVal uf_str_substring(UfVal s, UfVal start_v, UfVal end_v) {
    if (s.kind != UF_RT_STRING) return uf_str("");
    UfRtString* str = s.as.string;
    long start = (start_v.kind == UF_RT_NUMBER) ? (long)start_v.as.number : 0;
    long end = (end_v.kind == UF_RT_NUMBER) ? (long)end_v.as.number : (long)str->length;
    if (start < 0) start = 0;
    if (end > (long)str->length) end = (long)str->length;
    if (start > end) start = end;
    size_t count = (size_t)(end - start);
    char* buf = (char*)malloc(count + 1);
    if (count > 0) memcpy(buf, str->chars + start, count);
    buf[count] = '\0';
    UfVal res = uf_str(buf);
    free(buf);
    return res;
}

static inline UfVal uf_str_index_of(UfVal s, UfVal sub) {
    if (s.kind != UF_RT_STRING || sub.kind != UF_RT_STRING) return uf_num(-1);
    const char* found = strstr(s.as.string->chars, sub.as.string->chars);
    if (!found) return uf_num(-1);
    return uf_num((double)(found - s.as.string->chars));
}

static inline UfVal uf_str_split(UfVal s, UfVal delim) {
    if (s.kind != UF_RT_STRING || delim.kind != UF_RT_STRING) return uf_make_array(0);
    const char* str = s.as.string->chars;
    const char* d = delim.as.string->chars;
    size_t dlen = delim.as.string->length;
    UfVal res = uf_array_new(4);
    if (dlen == 0) {
        for (size_t i = 0; i < s.as.string->length; ++i) {
            char one[2] = { str[i], '\0' };
            uf_array_push(res, uf_str(one));
        }
        return res;
    }
    const char* start = str;
    const char* match = NULL;
    while ((match = strstr(start, d)) != NULL) {
        size_t part_len = (size_t)(match - start);
        char* part = (char*)malloc(part_len + 1);
        memcpy(part, start, part_len);
        part[part_len] = '\0';
        uf_array_push(res, uf_str(part));
        free(part);
        start = match + dlen;
    }
    uf_array_push(res, uf_str(start));
    return res;
}

static inline UfVal uf_str_join(UfVal arr, UfVal sep) {
    if (arr.kind != UF_RT_ARRAY || sep.kind != UF_RT_STRING) return uf_str("");
    UfRtArray* a = arr.as.array;
    if (a->count == 0) return uf_str("");
    size_t cap = 64;
    char* res = (char*)malloc(cap);
    res[0] = '\0';
    for (size_t i = 0; i < a->count; ++i) {
        if (i > 0) {
            if (strlen(res) + sep.as.string->length + 1 >= cap) {
                cap = (cap + sep.as.string->length) * 2;
                res = (char*)realloc(res, cap);
            }
            strcat(res, sep.as.string->chars);
        }
        char* item_s = uf_to_str(a->elements[i]);
        if (strlen(res) + strlen(item_s) + 1 >= cap) {
            cap = (cap + strlen(item_s)) * 2;
            res = (char*)realloc(res, cap);
        }
        strcat(res, item_s);
        free(item_s);
    }
    UfVal v = uf_str(res);
    free(res);
    return v;
}

static inline UfVal uf_str_replace(UfVal s, UfVal target, UfVal repl) {
    if (s.kind != UF_RT_STRING || target.kind != UF_RT_STRING || repl.kind != UF_RT_STRING) return s;
    if (target.as.string->length == 0) return s;
    const char* str = s.as.string->chars;
    const char* tgt = target.as.string->chars;
    size_t tgt_len = target.as.string->length;
    size_t cap = s.as.string->length + 32;
    char* res = (char*)malloc(cap);
    res[0] = '\0';
    const char* start = str;
    const char* match = NULL;
    while ((match = strstr(start, tgt)) != NULL) {
        size_t part_len = (size_t)(match - start);
        if (strlen(res) + part_len + repl.as.string->length + 1 >= cap) {
            cap = (cap + part_len + repl.as.string->length) * 2;
            res = (char*)realloc(res, cap);
        }
        strncat(res, start, part_len);
        strcat(res, repl.as.string->chars);
        start = match + tgt_len;
    }
    if (strlen(res) + strlen(start) + 1 >= cap) {
        cap = (cap + strlen(start)) * 2;
        res = (char*)realloc(res, cap);
    }
    strcat(res, start);
    UfVal v = uf_str(res);
    free(res);
    return v;
}

#endif /* UNFISH_RUNTIME_H */
