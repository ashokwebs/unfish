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
    UF_RT_MAP
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

typedef struct UfRtVal {
    UfRtKind kind;
    union {
        bool boolean;
        double number;
        UfRtString* string;
        UfRtArray* array;
        UfRtMap* map;
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
        }
        free(curr);
        curr = next;
    }
    g_uf_rt.all_objects = NULL;
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
    fprintf(stderr, "Runtime Error: 'len()' expects string, array, or map\n"); exit(3);
}

static inline UfVal uf_type_of(UfVal v) {
    switch (v.kind) {
        case UF_RT_NULL: return uf_str("null");
        case UF_RT_BOOL: return uf_str("bool");
        case UF_RT_NUMBER: return uf_str("number");
        case UF_RT_STRING: return uf_str("string");
        case UF_RT_ARRAY: return uf_str("array");
        case UF_RT_MAP: return uf_str("map");
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

#endif /* UNFISH_RUNTIME_H */
