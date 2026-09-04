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
#include <time.h>
#include <ctype.h>
#include <unistd.h>

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
    UF_RT_CLOSURE,
    UF_RT_INSTANCE,
    UF_RT_ERROR
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
typedef struct UfRtVal UfVal;
typedef struct UfRtError UfRtError;

typedef struct {
    UfRtHeader header;
    const char* name;
    const char** field_names;
    size_t field_count;
    UfVal* fields;
} UfRtInstance;

typedef struct {
    UfRtHeader header;
    size_t size;
    uint8_t* data;
} UfRtBuffer;

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
        UfRtInstance* instance;
        UfRtError* error;
        void* ptr;
    } as;
} UfVal;

struct UfRtError {
    UfRtHeader header;
    UfVal message;
    UfVal kind;
};

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

/* A heap cell for a captured mutable variable, tracked separately from
 * UfRtHeader objects (it is not a first-class UfVal itself, just backing
 * storage a UfVal* points into) so it can still be freed at program exit. */
typedef struct UfRtBoxNode {
    struct UfRtBoxNode* next;
    UfVal value;
} UfRtBoxNode;

/* Runtime context */
typedef struct {
    UfRtHeader* all_objects;
    int argc;
    char** argv;
    UfRtBoxNode* all_boxes;
} UfRtContext;

static UfRtContext g_uf_rt = { NULL, 0, NULL, NULL };

typedef struct UfCatchFrame {
    jmp_buf buf;
    UfVal error;
    struct UfCatchFrame* prev;
} UfCatchFrame;

static UfCatchFrame* g_catch_stack = NULL;

static inline void uf_catch_push(UfCatchFrame* frame) {
    frame->prev = g_catch_stack;
    frame->error.kind = UF_RT_NULL;
    g_catch_stack = frame;
}

static inline void uf_catch_pop(void) {
    if (g_catch_stack) {
        g_catch_stack = g_catch_stack->prev;
    }
}

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
    g_uf_rt.all_boxes = NULL;
    g_catch_stack = NULL;
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
        } else if (curr->kind == UF_RT_INSTANCE) {
            UfRtInstance* inst = (UfRtInstance*)curr;
            free(inst->fields);
        }
        free(curr);
        curr = next;
    }
    g_uf_rt.all_objects = NULL;

    UfRtBoxNode* bcurr = g_uf_rt.all_boxes;
    while (bcurr) {
        UfRtBoxNode* bnext = bcurr->next;
        free(bcurr);
        bcurr = bnext;
    }
    g_uf_rt.all_boxes = NULL;

    g_catch_stack = NULL;
}

/* Heap cell for a captured mutable variable, shared by reference between a
 * closure's defining scope and every invocation of that closure (and any
 * further closures nested inside it), so writes to the variable from one
 * side are visible on the other. Tracked in g_uf_rt.all_boxes and freed by
 * uf_cleanup. */
static inline UfVal* uf_box_new(UfVal v) {
    UfRtBoxNode* node = (UfRtBoxNode*)malloc(sizeof(UfRtBoxNode));
    if (!node) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    node->value = v;
    node->next = g_uf_rt.all_boxes;
    g_uf_rt.all_boxes = node;
    return &node->value;
}

static inline UfVal uf_instance_new(const char* name, const char** field_names, size_t field_count, size_t argc, UfVal* args) {
    if (argc != field_count) {
        fprintf(stderr, "Runtime Error: Struct '%s' expects %zu fields, but %zu provided\n", name, field_count, argc);
        exit(3);
    }
    UfRtInstance* inst = (UfRtInstance*)uf_rt_alloc(UF_RT_INSTANCE, sizeof(UfRtInstance));
    inst->name = name;
    inst->field_names = field_names;
    inst->field_count = field_count;
    if (field_count > 0) {
        inst->fields = (UfVal*)malloc(sizeof(UfVal) * field_count);
        for (size_t i = 0; i < field_count; ++i) {
            inst->fields[i] = args[i];
        }
    } else {
        inst->fields = NULL;
    }
    UfVal v; v.kind = UF_RT_INSTANCE; v.as.instance = inst; return v;
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
    UfVal stack_args[16];
    UfVal* args = stack_args;
    if (argc > 16) {
        args = (UfVal*)malloc(sizeof(UfVal) * argc);
    }
    for (size_t i = 0; i < argc; ++i) {
        args[i] = va_arg(va, UfVal);
    }
    va_end(va);
    if (callee.kind == UF_RT_CLOSURE && callee.as.closure != NULL) {
        UfVal result = callee.as.closure->fn(callee.as.closure->env, argc, args);
        if (args != stack_args) free(args);
        return result;
    }
    if (args != stack_args) free(args);
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

static inline UfVal uf_val_error(UfVal message, UfVal kind) {
    UfRtError* err = (UfRtError*)uf_rt_alloc(UF_RT_ERROR, sizeof(UfRtError));
    err->message = message;
    err->kind = kind;
    UfVal v;
    v.kind = UF_RT_ERROR;
    v.as.error = err;
    return v;
}

static inline void uf_throw(UfVal err) {
    if (g_catch_stack) {
        UfCatchFrame* target = g_catch_stack;
        g_catch_stack = target->prev;
        target->error = err;
        longjmp(target->buf, 1);
    }
    if (err.kind == UF_RT_ERROR) {
        UfRtError* e = err.as.error;
        const char* msg = (e->message.kind == UF_RT_STRING) ? e->message.as.string->chars : "Error";
        fprintf(stderr, "Runtime Error: %s\n", msg);
    } else {
        fprintf(stderr, "Runtime Error: Uncaught exception\n");
    }
    exit(3);
}

static inline void uf_raise(const char* msg, const char* kind) {
    UfVal m = uf_str(msg ? msg : "");
    UfVal k = uf_str(kind ? kind : "RuntimeError");
    uf_throw(uf_val_error(m, k));
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
        case UF_RT_INSTANCE: return true;
        default: return false;
    }
}

/* Tracks the array/map/instance object pointers currently being
 * stringified on the current recursive call chain, so a self-referential
 * structure (e.g. `let m = {}; m["self"] = m`) prints as "{...}" for the
 * repeated container instead of recursing forever and crashing with a
 * stack overflow. */
typedef struct {
    const void** ptrs;
    size_t count;
    size_t cap;
} UfToStrVisited;

static inline bool uf_to_str_visit_enter(UfToStrVisited* vis, const void* ptr) {
    for (size_t i = 0; i < vis->count; ++i) {
        if (vis->ptrs[i] == ptr) return false;
    }
    if (vis->count == vis->cap) {
        size_t new_cap = vis->cap == 0 ? 8 : vis->cap * 2;
        const void** new_ptrs = (const void**)realloc((void*)vis->ptrs, new_cap * sizeof(const void*));
        if (!new_ptrs) return false;
        vis->ptrs = new_ptrs;
        vis->cap = new_cap;
    }
    vis->ptrs[vis->count++] = ptr;
    return true;
}

static inline void uf_to_str_visit_leave(UfToStrVisited* vis) {
    if (vis->count > 0) vis->count--;
}

static inline char* uf_to_str_impl(UfVal v, UfToStrVisited* vis) {
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
        case UF_RT_INSTANCE: {
            UfRtInstance* inst = v.as.instance;
            if (!uf_to_str_visit_enter(vis, inst)) {
                size_t cap = strlen(inst->name) + 6;
                char* res = (char*)malloc(cap);
                snprintf(res, cap, "%s(...)", inst->name);
                return res;
            }
            size_t cap = 128;
            char* res = (char*)malloc(cap);
            snprintf(res, cap, "%s(", inst->name);
            for (size_t i = 0; i < inst->field_count; ++i) {
                if (i > 0) {
                    if (strlen(res) + 3 >= cap) { cap *= 2; res = (char*)realloc(res, cap); }
                    strcat(res, ", ");
                }
                char* s = uf_to_str_impl(inst->fields[i], vis);
                const char* fname = inst->field_names ? inst->field_names[i] : "?";
                size_t need = strlen(fname) + strlen(s) + 4;
                if (strlen(res) + need >= cap) {
                    cap = (cap + need) * 2;
                    res = (char*)realloc(res, cap);
                }
                strcat(res, fname);
                strcat(res, ": ");
                strcat(res, s);
                free(s);
            }
            strcat(res, ")");
            uf_to_str_visit_leave(vis);
            return res;
        }
        case UF_RT_ARRAY: {
            if (!uf_to_str_visit_enter(vis, v.as.array)) return strdup("[...]");
            size_t cap = 64;
            char* res = (char*)malloc(cap);
            strcpy(res, "[");
            for (size_t i = 0; i < v.as.array->count; ++i) {
                if (i > 0) {
                    if (strlen(res) + 3 >= cap) { cap *= 2; res = (char*)realloc(res, cap); }
                    strcat(res, ", ");
                }
                char* s = uf_to_str_impl(v.as.array->elements[i], vis);
                if (strlen(res) + strlen(s) + 4 >= cap) {
                    cap = (cap + strlen(s)) * 2;
                    res = (char*)realloc(res, cap);
                }
                strcat(res, s);
                free(s);
            }
            strcat(res, "]");
            uf_to_str_visit_leave(vis);
            return res;
        }
        case UF_RT_MAP: {
            if (!uf_to_str_visit_enter(vis, v.as.map)) return strdup("{...}");
            size_t cap = 64;
            char* res = (char*)malloc(cap);
            strcpy(res, "{");
            for (size_t i = 0; i < v.as.map->order_count; ++i) {
                if (i > 0) {
                    if (strlen(res) + 3 >= cap) { cap *= 2; res = (char*)realloc(res, cap); }
                    strcat(res, ", ");
                }
                /* Quote string keys to match interpreter format */
                UfVal order_key = v.as.map->order_keys[i];
                char* k;
                if (order_key.kind == UF_RT_STRING) {
                    size_t klen = order_key.as.string->length + 3;
                    k = (char*)malloc(klen);
                    snprintf(k, klen, "\"%s\"", order_key.as.string->chars);
                } else {
                    k = uf_to_str_impl(order_key, vis);
                }
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
                        char* val_s = uf_to_str_impl(v.as.map->entries[j].value, vis);
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
            uf_to_str_visit_leave(vis);
            return res;
        }
        case UF_RT_CLOSURE: return strdup("<function>");
        case UF_RT_ERROR: {
            char e_buf[256];
            const char* msg = (v.as.error->message.kind == UF_RT_STRING) ? v.as.error->message.as.string->chars : "";
            snprintf(e_buf, sizeof(e_buf), "<error: %s>", msg);
            return strdup(e_buf);
        }
        default: return strdup("<object>");
    }
}

static inline char* uf_to_str(UfVal v) {
    UfToStrVisited vis = {0};
    char* result = uf_to_str_impl(v, &vis);
    free((void*)vis.ptrs);
    return result;
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

static inline UfVal uf_get(UfVal target, UfVal index);
static inline UfVal uf_map_has_key(UfVal target, UfVal key);

/* Tracks the (a, b) object-pointer pairs currently being compared on the
 * current recursive call chain, so comparing two distinct-but-cyclic
 * structures doesn't recurse forever and crash with a stack overflow. A
 * pair re-encountered while already being compared is treated as equal
 * (the standard co-inductive convention for equality on cyclic structures).
 */
typedef struct {
    const void* a;
    const void* b;
} UfEqPair;

typedef struct {
    UfEqPair* pairs;
    size_t count;
    size_t cap;
} UfEqVisited;

static inline bool uf_eq_visit_enter(UfEqVisited* vis, const void* a, const void* b) {
    for (size_t i = 0; i < vis->count; ++i) {
        if (vis->pairs[i].a == a && vis->pairs[i].b == b) return false;
    }
    if (vis->count == vis->cap) {
        size_t new_cap = vis->cap == 0 ? 8 : vis->cap * 2;
        UfEqPair* new_pairs = (UfEqPair*)realloc(vis->pairs, new_cap * sizeof(UfEqPair));
        if (!new_pairs) return false;
        vis->pairs = new_pairs;
        vis->cap = new_cap;
    }
    vis->pairs[vis->count].a = a;
    vis->pairs[vis->count].b = b;
    vis->count++;
    return true;
}

static inline void uf_eq_visit_leave(UfEqVisited* vis) {
    if (vis->count > 0) vis->count--;
}

static inline bool uf_eq_bool_impl(UfVal a, UfVal b, UfEqVisited* vis) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case UF_RT_NULL: return true;
        case UF_RT_BOOL: return a.as.boolean == b.as.boolean;
        case UF_RT_NUMBER: return a.as.number == b.as.number;
        case UF_RT_STRING: return strcmp(a.as.string->chars, b.as.string->chars) == 0;
        case UF_RT_ERROR: return a.as.error == b.as.error;
        case UF_RT_ARRAY: {
            if (a.as.array == b.as.array) return true;
            if (a.as.array->count != b.as.array->count) return false;
            if (!uf_eq_visit_enter(vis, a.as.array, b.as.array)) return true;
            for (size_t i = 0; i < a.as.array->count; ++i) {
                if (!uf_eq_bool_impl(a.as.array->elements[i], b.as.array->elements[i], vis)) {
                    uf_eq_visit_leave(vis);
                    return false;
                }
            }
            uf_eq_visit_leave(vis);
            return true;
        }
        case UF_RT_MAP: {
            if (a.as.map == b.as.map) return true;
            if (a.as.map->count != b.as.map->count) return false;
            if (!uf_eq_visit_enter(vis, a.as.map, b.as.map)) return true;
            for (size_t i = 0; i < a.as.map->order_count; ++i) {
                UfVal key = a.as.map->order_keys[i];
                if (!uf_truthy(uf_map_has_key(b, key))) { uf_eq_visit_leave(vis); return false; }
                UfVal val_a = uf_get(a, key);
                UfVal val_b = uf_get(b, key);
                if (!uf_eq_bool_impl(val_a, val_b, vis)) { uf_eq_visit_leave(vis); return false; }
            }
            uf_eq_visit_leave(vis);
            return true;
        }
        case UF_RT_INSTANCE: {
            if (a.as.instance == b.as.instance) return true;
            if (strcmp(a.as.instance->name, b.as.instance->name) != 0) return false;
            if (a.as.instance->field_count != b.as.instance->field_count) return false;
            if (!uf_eq_visit_enter(vis, a.as.instance, b.as.instance)) return true;
            for (size_t i = 0; i < a.as.instance->field_count; ++i) {
                if (!uf_eq_bool_impl(a.as.instance->fields[i], b.as.instance->fields[i], vis)) {
                    uf_eq_visit_leave(vis);
                    return false;
                }
            }
            uf_eq_visit_leave(vis);
            return true;
        }
        default: return a.as.ptr == b.as.ptr;
    }
}

static inline bool uf_eq_bool(UfVal a, UfVal b) {
    UfEqVisited vis = {0};
    bool result = uf_eq_bool_impl(a, b, &vis);
    free(vis.pairs);
    return result;
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
        if (b.as.number == 0.0) { uf_raise("Division by zero", "DivisionByZero"); return uf_null(); }
        return uf_num(a.as.number / b.as.number);
    }
    uf_raise("Operands to '/' must be numbers", "TypeError");
    return uf_null();
}

static inline UfVal uf_mod(UfVal a, UfVal b) {
    if (a.kind == UF_RT_NUMBER && b.kind == UF_RT_NUMBER) {
        if (b.as.number == 0.0) { uf_raise("Division by zero in modulo", "DivisionByZero"); return uf_null(); }
        return uf_num(fmod(a.as.number, b.as.number));
    }
    uf_raise("Operands to '%' must be numbers", "TypeError");
    return uf_null();
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
        case UF_RT_BOOL: return uf_str("boolean");
        case UF_RT_NUMBER: return uf_str("number");
        case UF_RT_STRING: return uf_str("string");
        case UF_RT_ARRAY: return uf_str("array");
        case UF_RT_MAP: return uf_str("map");
        case UF_RT_BUFFER: return uf_str("buffer");
        case UF_RT_CLOSURE: return uf_str("function");
        case UF_RT_INSTANCE: return uf_str(v.as.instance->name);
        case UF_RT_ERROR: return uf_str("error");
        default: return uf_str("object");
    }
}

static inline UfVal uf_get(UfVal target, UfVal index) {
    if (target.kind == UF_RT_ERROR) {
        if (index.kind == UF_RT_STRING) {
            const char* fname = index.as.string->chars;
            if (strcmp(fname, "message") == 0) return target.as.error->message;
            if (strcmp(fname, "kind") == 0) return target.as.error->kind;
        }
        return uf_null();
    }
    if (target.kind == UF_RT_INSTANCE) {
        if (index.kind != UF_RT_STRING) {
            fprintf(stderr, "Runtime Error: Struct field access expects a string name\n");
            exit(3);
        }
        const char* fname = index.as.string->chars;
        UfRtInstance* inst = target.as.instance;
        for (size_t i = 0; i < inst->field_count; ++i) {
            if (strcmp(inst->field_names[i], fname) == 0) {
                return inst->fields[i];
            }
        }
        fprintf(stderr, "Runtime Error: Struct '%s' has no field '%s'\n", inst->name, fname);
        exit(3);
    }
    if (target.kind == UF_RT_ARRAY) {
        if (index.kind != UF_RT_NUMBER) { uf_raise("Array index must be a number", "TypeError"); return uf_null(); }
        long idx = (long)index.as.number;
        if (idx < 0) idx += target.as.array->count;
        if (idx < 0 || (size_t)idx >= target.as.array->count) {
            char buf[128];
            snprintf(buf, sizeof(buf), "IndexOutOfBounds: Index %ld out of bounds for array of length %zu", idx, target.as.array->count);
            uf_raise(buf, "IndexOutOfBounds");
            return uf_null();
        }
        return target.as.array->elements[idx];
    }
    if (target.kind == UF_RT_STRING) {
        if (index.kind != UF_RT_NUMBER) { uf_raise("String index must be a number", "TypeError"); return uf_null(); }
        long idx = (long)index.as.number;
        if (idx < 0) idx += target.as.string->length;
        if (idx < 0 || (size_t)idx >= target.as.string->length) {
            uf_raise("String index out of bounds", "IndexOutOfBounds");
            return uf_null();
        }
        char ch[2] = { target.as.string->chars[idx], '\0' };
        return uf_str(ch);
    }
    if (target.kind == UF_RT_BUFFER) {
        if (index.kind != UF_RT_NUMBER) { uf_raise("Buffer index must be a number", "TypeError"); return uf_null(); }
        long idx = (long)index.as.number;
        if (idx < 0) idx += target.as.buffer->size;
        if (idx < 0 || (size_t)idx >= target.as.buffer->size) {
            char buf[128];
            snprintf(buf, sizeof(buf), "IndexOutOfBounds: Index %ld out of bounds for buffer of size %zu", idx, target.as.buffer->size);
            uf_raise(buf, "IndexOutOfBounds");
            return uf_null();
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
    if (target.kind == UF_RT_INSTANCE) {
        if (index.kind != UF_RT_STRING) {
            fprintf(stderr, "Runtime Error: Struct field name must be a string\n");
            exit(3);
        }
        const char* fname = index.as.string->chars;
        UfRtInstance* inst = target.as.instance;
        for (size_t i = 0; i < inst->field_count; ++i) {
            if (strcmp(inst->field_names[i], fname) == 0) {
                inst->fields[i] = value;
                return;
            }
        }
        fprintf(stderr, "Runtime Error: Struct '%s' has no field '%s'\n", inst->name, fname);
        exit(3);
    }
    if (target.kind == UF_RT_ARRAY) {
        if (index.kind != UF_RT_NUMBER) { uf_raise("Array index must be a number", "TypeError"); return; }
        long idx = (long)index.as.number;
        if (idx < 0) idx += target.as.array->count;
        if (idx < 0 || (size_t)idx >= target.as.array->count) {
            char buf[128];
            snprintf(buf, sizeof(buf), "IndexOutOfBounds: Index %ld out of bounds for array of length %zu", idx, target.as.array->count);
            uf_raise(buf, "IndexOutOfBounds");
            return;
        }
        target.as.array->elements[idx] = value;
        return;
    }
    if (target.kind == UF_RT_BUFFER) {
        if (index.kind != UF_RT_NUMBER) { uf_raise("Buffer index must be a number", "TypeError"); return; }
        if (value.kind != UF_RT_NUMBER) { uf_raise("Buffer byte value must be a number", "TypeError"); return; }
        long idx = (long)index.as.number;
        if (idx < 0) idx += target.as.buffer->size;
        if (idx < 0 || (size_t)idx >= target.as.buffer->size) {
            char buf[128];
            snprintf(buf, sizeof(buf), "IndexOutOfBounds: Index %ld out of bounds for buffer of size %zu", idx, target.as.buffer->size);
            uf_raise(buf, "IndexOutOfBounds");
            return;
        }
        target.as.buffer->data[idx] = (uint8_t)(int64_t)value.as.number;
        return;
    }
    if (target.kind == UF_RT_MAP) {
        if (index.kind != UF_RT_STRING) { fprintf(stderr, "Runtime Error: Map key must be string\n"); exit(3); }
        UfRtMap* m = target.as.map;
        /* Update existing key */
        for (size_t i = 0; i < m->capacity; ++i) {
            if (m->entries[i].occupied && strcmp(m->entries[i].key.as.string->chars, index.as.string->chars) == 0) {
                m->entries[i].value = value;
                return;
            }
        }
        /* Grow if at 75% load factor or completely full */
        if (m->count * 4 >= m->capacity * 3) {
            size_t old_cap = m->capacity;
            UfRtMapEntry* old_entries = m->entries;
            m->capacity = old_cap * 2;
            m->entries = (UfRtMapEntry*)calloc(m->capacity, sizeof(UfRtMapEntry));
            m->count = 0;
            /* Reinsert existing entries */
            for (size_t i = 0; i < old_cap; ++i) {
                if (old_entries[i].occupied) {
                    /* Find empty slot in new table */
                    for (size_t j = 0; j < m->capacity; ++j) {
                        if (!m->entries[j].occupied) {
                            m->entries[j] = old_entries[i];
                            m->count++;
                            break;
                        }
                    }
                }
            }
            free(old_entries);
            /* Grow order_keys too */
            m->order_keys = (UfVal*)realloc(m->order_keys, sizeof(UfVal) * m->capacity);
        }
        /* Insert into first empty slot */
        for (size_t i = 0; i < m->capacity; ++i) {
            if (!m->entries[i].occupied) {
                m->entries[i].occupied = true;
                m->entries[i].key = index;
                m->entries[i].value = value;
                m->count++;
                if (m->order_count >= m->capacity) {
                    m->capacity *= 2;
                    m->entries = (UfRtMapEntry*)realloc(m->entries, sizeof(UfRtMapEntry) * m->capacity);
                    memset(&m->entries[m->capacity / 2], 0, sizeof(UfRtMapEntry) * (m->capacity / 2));
                    m->order_keys = (UfVal*)realloc(m->order_keys, sizeof(UfVal) * m->capacity);
                }
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

static inline UfVal uf_builtin_error(size_t argc, ...) {
    va_list va;
    va_start(va, argc);
    UfVal msg = argc > 0 ? va_arg(va, UfVal) : uf_str("");
    UfVal kind = argc > 1 ? va_arg(va, UfVal) : uf_str("UserError");
    va_end(va);

    if (msg.kind != UF_RT_STRING) {
        msg = uf_str_to_string(msg);
    }
    if (kind.kind != UF_RT_STRING) {
        kind = uf_str("UserError");
    }
    uf_throw(uf_val_error(msg, kind));
    return uf_null();
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

/* Collection and Array Builtins */
static inline UfVal uf_array_pop(UfVal arr) {
    if (arr.kind != UF_RT_ARRAY) {
        fprintf(stderr, "Runtime Error: 'pop()' expects an array\n");
        exit(3);
    }
    UfRtArray* a = arr.as.array;
    if (a->count == 0) return uf_null();
    return a->elements[--a->count];
}

static inline UfVal uf_range(size_t argc, ...) {
    if (argc == 0) {
        fprintf(stderr, "Runtime Error: 'range()' expects at least 1 argument\n");
        exit(3);
    }
    va_list va;
    va_start(va, argc);
    UfVal args[3];
    for (size_t i = 0; i < argc && i < 3; ++i) {
        args[i] = va_arg(va, UfVal);
    }
    va_end(va);

    double start = 0;
    double end = 0;
    double step = 1;
    if (argc == 1) {
        if (args[0].kind != UF_RT_NUMBER) { fprintf(stderr, "Runtime Error: 'range()' argument must be a number\n"); exit(3); }
        end = args[0].as.number;
    } else {
        if (args[0].kind != UF_RT_NUMBER || args[1].kind != UF_RT_NUMBER) { fprintf(stderr, "Runtime Error: 'range()' arguments must be numbers\n"); exit(3); }
        start = args[0].as.number;
        end = args[1].as.number;
        if (argc >= 3) {
            if (args[2].kind != UF_RT_NUMBER || args[2].as.number == 0) { fprintf(stderr, "Runtime Error: 'range()' step must be a non-zero number\n"); exit(3); }
            step = args[2].as.number;
        }
    }
    size_t count = 0;
    if (step > 0 && start < end) {
        count = (size_t)ceil((end - start) / step);
    } else if (step < 0 && start > end) {
        count = (size_t)ceil((start - end) / (-step));
    }
    UfVal arr = uf_array_new(count);
    double curr = start;
    if (step > 0) {
        while (curr < end) {
            uf_array_push(arr, uf_num(curr));
            curr += step;
        }
    } else {
        while (curr > end) {
            uf_array_push(arr, uf_num(curr));
            curr += step;
        }
    }
    return arr;
}

static inline UfVal uf_map_keys(UfVal target) {
    if (target.kind != UF_RT_MAP) {
        fprintf(stderr, "Runtime Error: 'keys()' expects a map\n");
        exit(3);
    }
    UfRtMap* m = target.as.map;
    UfVal arr = uf_array_new(m->order_count);
    for (size_t i = 0; i < m->order_count; ++i) {
        uf_array_push(arr, m->order_keys[i]);
    }
    return arr;
}

static inline UfVal uf_map_values(UfVal target) {
    if (target.kind != UF_RT_MAP) {
        fprintf(stderr, "Runtime Error: 'values()' expects a map\n");
        exit(3);
    }
    UfRtMap* m = target.as.map;
    UfVal arr = uf_array_new(m->order_count);
    for (size_t i = 0; i < m->order_count; ++i) {
        UfVal key = m->order_keys[i];
        for (size_t j = 0; j < m->capacity; ++j) {
            if (m->entries[j].occupied && strcmp(m->entries[j].key.as.string->chars, key.as.string->chars) == 0) {
                uf_array_push(arr, m->entries[j].value);
                break;
            }
        }
    }
    return arr;
}

static inline UfVal uf_map_has_key(UfVal target, UfVal key) {
    if (target.kind != UF_RT_MAP || key.kind != UF_RT_STRING) return uf_bool(false);
    UfRtMap* m = target.as.map;
    for (size_t i = 0; i < m->capacity; ++i) {
        if (m->entries[i].occupied && strcmp(m->entries[i].key.as.string->chars, key.as.string->chars) == 0) {
            return uf_bool(true);
        }
    }
    return uf_bool(false);
}

static inline UfVal uf_map_delete(UfVal target, UfVal key) {
    if (target.kind != UF_RT_MAP || key.kind != UF_RT_STRING) return uf_bool(false);
    UfRtMap* m = target.as.map;
    for (size_t i = 0; i < m->capacity; ++i) {
        if (m->entries[i].occupied && strcmp(m->entries[i].key.as.string->chars, key.as.string->chars) == 0) {
            m->entries[i].occupied = false;
            m->count--;
            for (size_t k = 0; k < m->order_count; ++k) {
                if (strcmp(m->order_keys[k].as.string->chars, key.as.string->chars) == 0) {
                    for (size_t p = k; p + 1 < m->order_count; ++p) {
                        m->order_keys[p] = m->order_keys[p + 1];
                    }
                    m->order_count--;
                    break;
                }
            }
            return uf_bool(true);
        }
    }
    return uf_bool(false);
}

static inline UfVal uf_iter_get(UfVal target, long idx) {
    if (target.kind == UF_RT_MAP) {
        if (idx >= 0 && (size_t)idx < target.as.map->order_count) {
            return target.as.map->order_keys[idx];
        }
        return uf_null();
    }
    return uf_get(target, uf_num((double)idx));
}

static inline UfVal uf_array_map(UfVal arr, UfVal fn) {
    if (arr.kind != UF_RT_ARRAY) {
        fprintf(stderr, "Runtime Error: 'map()' expects an array\n");
        exit(3);
    }
    UfRtArray* a = arr.as.array;
    UfVal res = uf_array_new(a->count);
    for (size_t i = 0; i < a->count; ++i) {
        UfVal item = a->elements[i];
        uf_array_push(res, uf_call_val(fn, 1, item));
    }
    return res;
}

static inline UfVal uf_array_filter(UfVal arr, UfVal fn) {
    if (arr.kind != UF_RT_ARRAY) {
        fprintf(stderr, "Runtime Error: 'filter()' expects an array\n");
        exit(3);
    }
    UfRtArray* a = arr.as.array;
    UfVal res = uf_array_new(a->count);
    for (size_t i = 0; i < a->count; ++i) {
        UfVal item = a->elements[i];
        if (uf_truthy(uf_call_val(fn, 1, item))) {
            uf_array_push(res, item);
        }
    }
    return res;
}

static inline UfVal uf_array_reduce(size_t argc, ...) {
    if (argc < 2) {
        fprintf(stderr, "Runtime Error: 'reduce()' expects an array and a function\n");
        exit(3);
    }
    va_list va;
    va_start(va, argc);
    UfVal args[3];
    for (size_t i = 0; i < argc && i < 3; ++i) {
        args[i] = va_arg(va, UfVal);
    }
    va_end(va);

    if (args[0].kind != UF_RT_ARRAY) {
        fprintf(stderr, "Runtime Error: 'reduce()' expects an array and a function\n");
        exit(3);
    }
    UfRtArray* a = args[0].as.array;
    UfVal fn = args[1];
    if (a->count == 0) {
        if (argc >= 3) return args[2];
        return uf_null();
    }
    size_t start_idx = 0;
    UfVal acc;
    if (argc >= 3) {
        acc = args[2];
    } else {
        acc = a->elements[0];
        start_idx = 1;
    }
    for (size_t i = start_idx; i < a->count; ++i) {
        acc = uf_call_val(fn, 2, acc, a->elements[i]);
    }
    return acc;
}

static inline int uf_default_compare(UfVal a, UfVal b) {
    if (a.kind == UF_RT_NUMBER && b.kind == UF_RT_NUMBER) {
        return (a.as.number > b.as.number) - (a.as.number < b.as.number);
    }
    if (a.kind == UF_RT_STRING && b.kind == UF_RT_STRING) {
        return strcmp(a.as.string->chars, b.as.string->chars);
    }
    return 0;
}

static inline UfVal uf_array_sort(size_t argc, ...) {
    if (argc < 1) {
        fprintf(stderr, "Runtime Error: 'sort()' expects an array\n");
        exit(3);
    }
    va_list va;
    va_start(va, argc);
    UfVal args[2];
    for (size_t i = 0; i < argc && i < 2; ++i) {
        args[i] = va_arg(va, UfVal);
    }
    va_end(va);

    if (args[0].kind != UF_RT_ARRAY) {
        fprintf(stderr, "Runtime Error: 'sort()' expects an array\n");
        exit(3);
    }
    UfRtArray* src = args[0].as.array;
    UfVal res = uf_array_new(src->count);
    for (size_t i = 0; i < src->count; ++i) {
        uf_array_push(res, src->elements[i]);
    }
    bool has_cmp = (argc >= 2 && args[1].kind != UF_RT_NULL);
    UfVal cmp_fn = has_cmp ? args[1] : uf_null();
    UfRtArray* r = res.as.array;
    for (size_t i = 1; i < r->count; ++i) {
        UfVal key = r->elements[i];
        size_t j = i;
        while (j > 0) {
            int cmp = 0;
            if (has_cmp) {
                UfVal cres = uf_call_val(cmp_fn, 2, r->elements[j - 1], key);
                if (cres.kind == UF_RT_NUMBER) {
                    cmp = (cres.as.number > 0) ? 1 : ((cres.as.number < 0) ? -1 : 0);
                } else if (cres.kind == UF_RT_BOOL) {
                    cmp = cres.as.boolean ? -1 : 1;
                }
            } else {
                cmp = uf_default_compare(r->elements[j - 1], key);
            }
            if (cmp > 0) {
                r->elements[j] = r->elements[j - 1];
                j--;
            } else {
                break;
            }
        }
        r->elements[j] = key;
    }
    return res;
}

static inline UfVal uf_array_reverse(UfVal arr) {
    if (arr.kind != UF_RT_ARRAY) {
        fprintf(stderr, "Runtime Error: 'reverse()' expects an array\n");
        exit(3);
    }
    UfRtArray* src = arr.as.array;
    UfVal res = uf_array_new(src->count);
    for (size_t i = src->count; i > 0; --i) {
        uf_array_push(res, src->elements[i - 1]);
    }
    return res;
}

static inline UfVal uf_array_find(UfVal arr, UfVal fn) {
    if (arr.kind != UF_RT_ARRAY) {
        fprintf(stderr, "Runtime Error: 'find()' expects an array\n");
        exit(3);
    }
    UfRtArray* a = arr.as.array;
    for (size_t i = 0; i < a->count; ++i) {
        if (uf_truthy(uf_call_val(fn, 1, a->elements[i]))) {
            return a->elements[i];
        }
    }
    return uf_null();
}

static inline UfVal uf_array_every(UfVal arr, UfVal fn) {
    if (arr.kind != UF_RT_ARRAY) {
        fprintf(stderr, "Runtime Error: 'every()' expects an array\n");
        exit(3);
    }
    UfRtArray* a = arr.as.array;
    for (size_t i = 0; i < a->count; ++i) {
        if (!uf_truthy(uf_call_val(fn, 1, a->elements[i]))) {
            return uf_bool(false);
        }
    }
    return uf_bool(true);
}

static inline UfVal uf_array_some(UfVal arr, UfVal fn) {
    if (arr.kind != UF_RT_ARRAY) {
        fprintf(stderr, "Runtime Error: 'some()' expects an array\n");
        exit(3);
    }
    UfRtArray* a = arr.as.array;
    for (size_t i = 0; i < a->count; ++i) {
        if (uf_truthy(uf_call_val(fn, 1, a->elements[i]))) {
            return uf_bool(true);
        }
    }
    return uf_bool(false);
}

static inline UfVal uf_sys_clock(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uf_num((double)ts.tv_sec + (double)ts.tv_nsec * 1e-9);
}

static inline UfVal uf_sys_assert(size_t argc, ...) {
    if (argc < 1) {
        uf_raise("Assertion failed", "AssertionError");
        return uf_null();
    }
    va_list va;
    va_start(va, argc);
    UfVal args[2];
    for (size_t i = 0; i < argc && i < 2; ++i) {
        args[i] = va_arg(va, UfVal);
    }
    va_end(va);

    if (!uf_truthy(args[0])) {
        const char* msg = (argc >= 2 && args[1].kind == UF_RT_STRING) ? args[1].as.string->chars : "Assertion failed";
        uf_raise(msg, "AssertionError");
        return uf_null();
    }
    return uf_null();
}

/* ========================================================================= */
/* BUILT-IN STDLIB MODULES                                                   */
/* ========================================================================= */

static inline UfVal _wrap_math_abs(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_math_abs(a[0]) : uf_null(); }
static inline UfVal _wrap_math_floor(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_math_floor(a[0]) : uf_null(); }
static inline UfVal _wrap_math_ceil(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_math_ceil(a[0]) : uf_null(); }
static inline UfVal _wrap_math_round(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_math_round(a[0]) : uf_null(); }
static inline UfVal _wrap_math_sqrt(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_math_sqrt(a[0]) : uf_null(); }
static inline UfVal _wrap_math_pow(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_math_pow(a[0], a[1]) : uf_null(); }
static inline UfVal _wrap_math_min(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_math_min(a[0], a[1]) : uf_null(); }
static inline UfVal _wrap_math_max(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_math_max(a[0], a[1]) : uf_null(); }
static inline UfVal _wrap_math_log(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_math_log(a[0]) : uf_null(); }
static inline UfVal _wrap_math_sin(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_math_sin(a[0]) : uf_null(); }
static inline UfVal _wrap_math_cos(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_math_cos(a[0]) : uf_null(); }
static inline UfVal _wrap_math_tan(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_math_tan(a[0]) : uf_null(); }
static inline UfVal _wrap_math_random(void* e, size_t n, UfVal* a) { (void)e; (void)n; (void)a; return uf_math_random(); }
static inline UfVal _wrap_math_random_int(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_math_random_int(a[0], a[1]) : uf_null(); }

static inline UfVal uf_mod_math(void) {
    UfVal m = uf_map_new(20);
    uf_set(m, uf_str("PI"), uf_num(3.14159265358979323846));
    uf_set(m, uf_str("E"), uf_num(2.71828182845904523536));
    uf_set(m, uf_str("INFINITY"), uf_num(HUGE_VAL));
    uf_set(m, uf_str("abs"), uf_closure_new(_wrap_math_abs, NULL, 0));
    uf_set(m, uf_str("floor"), uf_closure_new(_wrap_math_floor, NULL, 0));
    uf_set(m, uf_str("ceil"), uf_closure_new(_wrap_math_ceil, NULL, 0));
    uf_set(m, uf_str("round"), uf_closure_new(_wrap_math_round, NULL, 0));
    uf_set(m, uf_str("sqrt"), uf_closure_new(_wrap_math_sqrt, NULL, 0));
    uf_set(m, uf_str("pow"), uf_closure_new(_wrap_math_pow, NULL, 0));
    uf_set(m, uf_str("min"), uf_closure_new(_wrap_math_min, NULL, 0));
    uf_set(m, uf_str("max"), uf_closure_new(_wrap_math_max, NULL, 0));
    uf_set(m, uf_str("log"), uf_closure_new(_wrap_math_log, NULL, 0));
    uf_set(m, uf_str("sin"), uf_closure_new(_wrap_math_sin, NULL, 0));
    uf_set(m, uf_str("cos"), uf_closure_new(_wrap_math_cos, NULL, 0));
    uf_set(m, uf_str("tan"), uf_closure_new(_wrap_math_tan, NULL, 0));
    uf_set(m, uf_str("random"), uf_closure_new(_wrap_math_random, NULL, 0));
    uf_set(m, uf_str("random_int"), uf_closure_new(_wrap_math_random_int, NULL, 0));
    return m;
}

static inline UfVal _wrap_str_split(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_str_split(a[0], a[1]) : uf_null(); }
static inline UfVal _wrap_str_join(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_str_join(a[0], a[1]) : uf_null(); }
static inline UfVal _wrap_str_trim(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_str_trim(a[0]) : uf_null(); }
static inline UfVal _wrap_str_replace(void* e, size_t n, UfVal* a) { (void)e; return n > 2 ? uf_str_replace(a[0], a[1], a[2]) : uf_null(); }
static inline UfVal _wrap_str_to_upper(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_str_to_upper(a[0]) : uf_null(); }
static inline UfVal _wrap_str_to_lower(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_str_to_lower(a[0]) : uf_null(); }
static inline UfVal _wrap_str_contains(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_str_contains(a[0], a[1]) : uf_null(); }
static inline UfVal _wrap_str_starts_with(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_str_starts_with(a[0], a[1]) : uf_null(); }
static inline UfVal _wrap_str_ends_with(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_str_ends_with(a[0], a[1]) : uf_null(); }
static inline UfVal _wrap_str_char_at(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_str_char_at(a[0], a[1]) : uf_null(); }
static inline UfVal _wrap_str_to_number(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_str_to_number(a[0]) : uf_null(); }
static inline UfVal _wrap_str_to_string(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_str_to_string(a[0]) : uf_null(); }
static inline UfVal _wrap_str_repeat(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_str_repeat(a[0], a[1]) : uf_null(); }
static inline UfVal _wrap_str_substring(void* e, size_t n, UfVal* a) { (void)e; return n > 2 ? uf_str_substring(a[0], a[1], a[2]) : (n > 1 ? uf_str_substring(a[0], a[1], uf_null()) : uf_null()); }
static inline UfVal _wrap_str_index_of(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_str_index_of(a[0], a[1]) : uf_null(); }

static inline UfVal uf_mod_strings(void) {
    UfVal m = uf_map_new(20);
    uf_set(m, uf_str("split"), uf_closure_new(_wrap_str_split, NULL, 0));
    uf_set(m, uf_str("join"), uf_closure_new(_wrap_str_join, NULL, 0));
    uf_set(m, uf_str("trim"), uf_closure_new(_wrap_str_trim, NULL, 0));
    uf_set(m, uf_str("replace"), uf_closure_new(_wrap_str_replace, NULL, 0));
    uf_set(m, uf_str("to_upper"), uf_closure_new(_wrap_str_to_upper, NULL, 0));
    uf_set(m, uf_str("to_lower"), uf_closure_new(_wrap_str_to_lower, NULL, 0));
    uf_set(m, uf_str("contains"), uf_closure_new(_wrap_str_contains, NULL, 0));
    uf_set(m, uf_str("starts_with"), uf_closure_new(_wrap_str_starts_with, NULL, 0));
    uf_set(m, uf_str("ends_with"), uf_closure_new(_wrap_str_ends_with, NULL, 0));
    uf_set(m, uf_str("char_at"), uf_closure_new(_wrap_str_char_at, NULL, 0));
    uf_set(m, uf_str("to_number"), uf_closure_new(_wrap_str_to_number, NULL, 0));
    uf_set(m, uf_str("to_string"), uf_closure_new(_wrap_str_to_string, NULL, 0));
    uf_set(m, uf_str("repeat_string"), uf_closure_new(_wrap_str_repeat, NULL, 0));
    uf_set(m, uf_str("substring"), uf_closure_new(_wrap_str_substring, NULL, 0));
    uf_set(m, uf_str("index_of"), uf_closure_new(_wrap_str_index_of, NULL, 0));
    return m;
}

static inline UfVal _wrap_sys_platform(void* e, size_t n, UfVal* a) {
    (void)e; (void)n; (void)a;
    #if defined(_WIN32)
    return uf_str("windows");
    #elif defined(__APPLE__)
    return uf_str("macos");
    #elif defined(__linux__)
    return uf_str("linux");
    #else
    return uf_str("unknown");
    #endif
}

static inline UfVal _wrap_sys_args(void* e, size_t n, UfVal* a) {
    (void)e; (void)n; (void)a;
    UfVal arr = uf_array_new(g_uf_rt.argc > 0 ? (size_t)g_uf_rt.argc : 1);
    for (int i = 0; i < g_uf_rt.argc; ++i) {
        uf_array_push(arr, uf_str(g_uf_rt.argv[i]));
    }
    if (g_uf_rt.argc == 0) {
        uf_array_push(arr, uf_str("unfish"));
    }
    return arr;
}

static inline UfVal _wrap_sys_env(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_STRING) return uf_null();
    const char* val = getenv(a[0].as.string->chars);
    return val ? uf_str(val) : uf_null();
}

static inline UfVal _wrap_sys_exit(void* e, size_t n, UfVal* a) {
    (void)e;
    int code = 0;
    if (n > 0 && a[0].kind == UF_RT_NUMBER) code = (int)a[0].as.number;
    exit(code);
    return uf_null();
}

static inline UfVal uf_mod_sys(void) {
    UfVal m = uf_map_new(8);
    uf_set(m, uf_str("platform"), uf_closure_new(_wrap_sys_platform, NULL, 0));
    uf_set(m, uf_str("args"), uf_closure_new(_wrap_sys_args, NULL, 0));
    uf_set(m, uf_str("env"), uf_closure_new(_wrap_sys_env, NULL, 0));
    uf_set(m, uf_str("exit"), uf_closure_new(_wrap_sys_exit, NULL, 0));
    return m;
}

static inline UfVal _wrap_fs_write_text(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 2 || a[0].kind != UF_RT_STRING || a[1].kind != UF_RT_STRING) return uf_bool(false);
    FILE* f = fopen(a[0].as.string->chars, "wb");
    if (!f) return uf_bool(false);
    size_t len = a[1].as.string->length;
    size_t written = fwrite(a[1].as.string->chars, 1, len, f);
    fclose(f);
    return uf_bool(written == len);
}

static inline UfVal _wrap_fs_read_text(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_STRING) return uf_null();
    FILE* f = fopen(a[0].as.string->chars, "rb");
    if (!f) return uf_null();
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return uf_null(); }
    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return uf_null(); }
    size_t r = fread(buf, 1, (size_t)size, f);
    buf[r] = '\0';
    fclose(f);
    UfVal res = uf_str(buf);
    free(buf);
    return res;
}

static inline UfVal _wrap_fs_exists(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_STRING) return uf_bool(false);
    return uf_bool(access(a[0].as.string->chars, F_OK) == 0);
}

static inline UfVal _wrap_fs_delete_file(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_STRING) return uf_bool(false);
    return uf_bool(remove(a[0].as.string->chars) == 0);
}

static inline UfVal uf_mod_fs(void) {
    UfVal m = uf_map_new(8);
    uf_set(m, uf_str("write_text"), uf_closure_new(_wrap_fs_write_text, NULL, 0));
    uf_set(m, uf_str("read_text"), uf_closure_new(_wrap_fs_read_text, NULL, 0));
    uf_set(m, uf_str("exists"), uf_closure_new(_wrap_fs_exists, NULL, 0));
    uf_set(m, uf_str("delete_file"), uf_closure_new(_wrap_fs_delete_file, NULL, 0));
    return m;
}

static inline UfVal _wrap_mod_random_random(void* e, size_t n, UfVal* a) { (void)e; (void)n; (void)a; return uf_math_random(); }
static inline UfVal _wrap_mod_random_random_int(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_math_random_int(a[0], a[1]) : uf_num(0); }
static inline UfVal _wrap_mod_random_choice(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_ARRAY || a[0].as.array->count == 0) return uf_null();
    size_t idx = (size_t)(rand() % a[0].as.array->count);
    return a[0].as.array->elements[idx];
}
static inline UfVal _wrap_mod_random_shuffle(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_ARRAY) return uf_array_new(0);
    UfRtArray* src = a[0].as.array;
    UfVal res = uf_array_new(src->count);
    for (size_t i = 0; i < src->count; ++i) {
        uf_array_push(res, src->elements[i]);
    }
    UfRtArray* dst = res.as.array;
    for (size_t i = dst->count; i > 1; --i) {
        size_t j = (size_t)(rand() % i);
        UfVal tmp = dst->elements[i - 1];
        dst->elements[i - 1] = dst->elements[j];
        dst->elements[j] = tmp;
    }
    return res;
}

static inline UfVal uf_mod_random(void) {
    UfVal m = uf_map_new(8);
    uf_set(m, uf_str("random"), uf_closure_new(_wrap_mod_random_random, NULL, 0));
    uf_set(m, uf_str("random_int"), uf_closure_new(_wrap_mod_random_random_int, NULL, 0));
    uf_set(m, uf_str("choice"), uf_closure_new(_wrap_mod_random_choice, NULL, 0));
    uf_set(m, uf_str("shuffle"), uf_closure_new(_wrap_mod_random_shuffle, NULL, 0));
    return m;
}

static inline UfVal _wrap_time_clock(void* e, size_t n, UfVal* a) { (void)e; (void)n; (void)a; return uf_sys_clock(); }
static inline UfVal _wrap_time_sleep(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n > 0 && a[0].kind == UF_RT_NUMBER && a[0].as.number > 0) {
        struct timespec req;
        double s = a[0].as.number;
        req.tv_sec = (time_t)s;
        req.tv_nsec = (long)((s - (time_t)s) * 1e9);
        nanosleep(&req, NULL);
    }
    return uf_null();
}
static inline UfVal _wrap_time_timestamp(void* e, size_t n, UfVal* a) { (void)e; (void)n; (void)a; return uf_num((double)time(NULL)); }

static inline UfVal uf_mod_time(void) {
    UfVal m = uf_map_new(8);
    uf_set(m, uf_str("clock"), uf_closure_new(_wrap_time_clock, NULL, 0));
    uf_set(m, uf_str("sleep"), uf_closure_new(_wrap_time_sleep, NULL, 0));
    uf_set(m, uf_str("timestamp"), uf_closure_new(_wrap_time_timestamp, NULL, 0));
    return m;
}

/* JSON Parser and Stringifier */
typedef struct {
    const char* src;
    size_t pos;
    size_t len;
    bool has_error;
} UfJsonParser;

static void uf_json_skip_ws(UfJsonParser* p) {
    while (p->pos < p->len && isspace((unsigned char)p->src[p->pos])) p->pos++;
}

static char uf_json_peek(UfJsonParser* p) {
    uf_json_skip_ws(p);
    return (p->pos < p->len) ? p->src[p->pos] : '\0';
}

static char uf_json_advance(UfJsonParser* p) {
    uf_json_skip_ws(p);
    return (p->pos < p->len) ? p->src[p->pos++] : '\0';
}

static UfVal uf_json_parse_val(UfJsonParser* p);

static UfVal uf_json_parse_str(UfJsonParser* p) {
    if (uf_json_advance(p) != '"') { p->has_error = true; return uf_null(); }
    size_t cap = 64, len = 0;
    char* buf = (char*)malloc(cap);
    while (p->pos < p->len) {
        char c = p->src[p->pos++];
        if (c == '"') {
            buf[len] = '\0';
            UfVal v = uf_str(buf);
            free(buf);
            return v;
        }
        if (c == '\\') {
            if (p->pos >= p->len) break;
            char esc = p->src[p->pos++];
            switch (esc) {
                case '"':  c = '"'; break;
                case '\\': c = '\\'; break;
                case '/':  c = '/'; break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                default:   c = esc; break;
            }
        }
        if (len + 1 >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
        buf[len++] = c;
    }
    free(buf);
    p->has_error = true;
    return uf_null();
}

static UfVal uf_json_parse_num(UfJsonParser* p) {
    uf_json_skip_ws(p);
    char* endptr = NULL;
    double n = strtod(p->src + p->pos, &endptr);
    if (endptr == p->src + p->pos) { p->has_error = true; return uf_null(); }
    p->pos = (size_t)(endptr - p->src);
    return uf_num(n);
}

static UfVal uf_json_parse_arr(UfJsonParser* p) {
    if (uf_json_advance(p) != '[') { p->has_error = true; return uf_null(); }
    UfVal arr = uf_array_new(8);
    uf_json_skip_ws(p);
    if (uf_json_peek(p) == ']') { uf_json_advance(p); return arr; }
    while (!p->has_error && p->pos < p->len) {
        UfVal elem = uf_json_parse_val(p);
        if (p->has_error) break;
        uf_array_push(arr, elem);
        uf_json_skip_ws(p);
        if (uf_json_peek(p) == ',') {
            uf_json_advance(p);
        } else if (uf_json_peek(p) == ']') {
            uf_json_advance(p);
            break;
        } else {
            p->has_error = true;
            break;
        }
    }
    return arr;
}

static UfVal uf_json_parse_obj(UfJsonParser* p) {
    if (uf_json_advance(p) != '{') { p->has_error = true; return uf_null(); }
    UfVal map = uf_map_new(8);
    uf_json_skip_ws(p);
    if (uf_json_peek(p) == '}') { uf_json_advance(p); return map; }
    while (!p->has_error && p->pos < p->len) {
        uf_json_skip_ws(p);
        if (uf_json_peek(p) != '"') { p->has_error = true; break; }
        UfVal key = uf_json_parse_str(p);
        if (p->has_error) break;
        uf_json_skip_ws(p);
        if (uf_json_advance(p) != ':') { p->has_error = true; break; }
        UfVal val = uf_json_parse_val(p);
        if (p->has_error) break;
        uf_set(map, key, val);
        uf_json_skip_ws(p);
        if (uf_json_peek(p) == ',') {
            uf_json_advance(p);
        } else if (uf_json_peek(p) == '}') {
            uf_json_advance(p);
            break;
        } else {
            p->has_error = true;
            break;
        }
    }
    return map;
}

static UfVal uf_json_parse_val(UfJsonParser* p) {
    uf_json_skip_ws(p);
    char c = uf_json_peek(p);
    if (c == '"') return uf_json_parse_str(p);
    if (c == '{') return uf_json_parse_obj(p);
    if (c == '[') return uf_json_parse_arr(p);
    if (c == '-' || isdigit((unsigned char)c)) return uf_json_parse_num(p);
    if (strncmp(p->src + p->pos, "true", 4) == 0) { p->pos += 4; return uf_bool(true); }
    if (strncmp(p->src + p->pos, "false", 5) == 0) { p->pos += 5; return uf_bool(false); }
    if (strncmp(p->src + p->pos, "null", 4) == 0) { p->pos += 4; return uf_null(); }
    p->has_error = true;
    return uf_null();
}

static inline UfVal _wrap_json_parse(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_STRING) return uf_null();
    UfJsonParser p;
    p.src = a[0].as.string->chars;
    p.pos = 0;
    p.len = a[0].as.string->length;
    p.has_error = false;
    UfVal val = uf_json_parse_val(&p);
    uf_json_skip_ws(&p);
    if (p.has_error || p.pos < p.len) return uf_null();
    return val;
}

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} UfJsonSb;

static void _json_sb_init(UfJsonSb* sb) {
    sb->cap = 128;
    sb->len = 0;
    sb->data = (char*)malloc(sb->cap);
    sb->data[0] = '\0';
}

static void _json_sb_append(UfJsonSb* sb, const char* str, size_t len) {
    while (sb->len + len + 1 >= sb->cap) {
        sb->cap *= 2;
        sb->data = (char*)realloc(sb->data, sb->cap);
    }
    memcpy(sb->data + sb->len, str, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
}

/* Returns false (without raising anything itself) on a detected cycle, so
 * the caller can free its StringBuilder/visited-set before raising the
 * catchable error — uf_raise/uf_throw may longjmp out past this function
 * entirely when a try/catch is active, which would otherwise skip that
 * cleanup and leak them. */
static bool _json_stringify_val(UfJsonSb* sb, UfVal val, UfToStrVisited* vis) {
    switch (val.kind) {
        case UF_RT_NULL: _json_sb_append(sb, "null", 4); return true;
        case UF_RT_BOOL:
            if (val.as.boolean) _json_sb_append(sb, "true", 4);
            else _json_sb_append(sb, "false", 5);
            return true;
        case UF_RT_NUMBER: {
            char num_buf[64];
            if (val.as.number == (double)(int64_t)val.as.number && !isnan(val.as.number) && !isinf(val.as.number)) {
                snprintf(num_buf, sizeof(num_buf), "%ld", (long)(int64_t)val.as.number);
            } else {
                snprintf(num_buf, sizeof(num_buf), "%.14g", val.as.number);
            }
            _json_sb_append(sb, num_buf, strlen(num_buf));
            return true;
        }
        case UF_RT_STRING: {
            _json_sb_append(sb, "\"", 1);
            const char* s = val.as.string->chars;
            size_t slen = val.as.string->length;
            for (size_t i = 0; i < slen; ++i) {
                char c = s[i];
                switch (c) {
                    case '"':  _json_sb_append(sb, "\\\"", 2); break;
                    case '\\': _json_sb_append(sb, "\\\\", 2); break;
                    case '\b': _json_sb_append(sb, "\\b", 2); break;
                    case '\f': _json_sb_append(sb, "\\f", 2); break;
                    case '\n': _json_sb_append(sb, "\\n", 2); break;
                    case '\r': _json_sb_append(sb, "\\r", 2); break;
                    case '\t': _json_sb_append(sb, "\\t", 2); break;
                    default:   _json_sb_append(sb, &c, 1); break;
                }
            }
            _json_sb_append(sb, "\"", 1);
            return true;
        }
        case UF_RT_ARRAY: {
            UfRtArray* arr = val.as.array;
            if (!uf_to_str_visit_enter(vis, arr)) return false;
            _json_sb_append(sb, "[", 1);
            for (size_t i = 0; i < arr->count; ++i) {
                if (i > 0) _json_sb_append(sb, ", ", 2);
                if (!_json_stringify_val(sb, arr->elements[i], vis)) {
                    uf_to_str_visit_leave(vis);
                    return false;
                }
            }
            _json_sb_append(sb, "]", 1);
            uf_to_str_visit_leave(vis);
            return true;
        }
        case UF_RT_MAP: {
            UfRtMap* map = val.as.map;
            if (!uf_to_str_visit_enter(vis, map)) return false;
            _json_sb_append(sb, "{", 1);
            for (size_t i = 0; i < map->order_count; ++i) {
                if (i > 0) _json_sb_append(sb, ", ", 2);
                UfVal k = map->order_keys[i];
                _json_sb_append(sb, "\"", 1);
                const char* ks = (k.kind == UF_RT_STRING) ? k.as.string->chars : "";
                _json_sb_append(sb, ks, strlen(ks));
                _json_sb_append(sb, "\": ", 3);
                UfVal v = uf_get(val, k);
                if (!_json_stringify_val(sb, v, vis)) {
                    uf_to_str_visit_leave(vis);
                    return false;
                }
            }
            _json_sb_append(sb, "}", 1);
            uf_to_str_visit_leave(vis);
            return true;
        }
        default:
            _json_sb_append(sb, "null", 4);
            return true;
    }
}

static inline UfVal _wrap_json_stringify(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1) return uf_str("null");
    UfJsonSb sb;
    _json_sb_init(&sb);
    UfToStrVisited vis = {0};
    bool ok = _json_stringify_val(&sb, a[0], &vis);
    free((void*)vis.ptrs);
    if (!ok) {
        free(sb.data);
        uf_raise("Converting circular structure to JSON", "TypeError");
        return uf_null(); /* unreachable: uf_raise always longjmps or exits */
    }
    UfVal res = uf_str(sb.data);
    free(sb.data);
    return res;
}

static inline UfVal uf_mod_json(void) {
    UfVal m = uf_map_new(4);
    uf_set(m, uf_str("parse"), uf_closure_new(_wrap_json_parse, NULL, 0));
    uf_set(m, uf_str("stringify"), uf_closure_new(_wrap_json_stringify, NULL, 0));
    return m;
}

/* Module Registry */
typedef UfVal (*UfModuleInitFn)(void);
typedef struct {
    const char* name;
    UfModuleInitFn init_fn;
    UfVal cached_exports;
    bool is_cached;
    bool is_loading;
} UfModuleEntry;

#define UF_MAX_MODULES 64
static UfModuleEntry g_module_registry[UF_MAX_MODULES];
static size_t g_module_registry_count = 0;

static inline void uf_register_module(const char* name, UfModuleInitFn init_fn) {
    if (g_module_registry_count < UF_MAX_MODULES) {
        g_module_registry[g_module_registry_count].name = name;
        g_module_registry[g_module_registry_count].init_fn = init_fn;
        g_module_registry[g_module_registry_count].cached_exports.kind = UF_RT_NULL;
        g_module_registry[g_module_registry_count].is_cached = false;
        g_module_registry[g_module_registry_count].is_loading = false;
        g_module_registry_count++;
    }
}

static inline UfVal uf_load_module(const char* name) {
    for (size_t i = 0; i < g_module_registry_count; ++i) {
        if (strcmp(g_module_registry[i].name, name) == 0) {
            if (g_module_registry[i].is_cached) {
                return g_module_registry[i].cached_exports;
            }
            if (g_module_registry[i].is_loading) {
                char err_buf[128];
                snprintf(err_buf, sizeof(err_buf), "Circular dependency detected while importing module '%s'", name);
                uf_raise(err_buf, "CircularImportError");
                return uf_null();
            }
            g_module_registry[i].is_loading = true;
            UfVal mod = g_module_registry[i].init_fn();
            g_module_registry[i].cached_exports = mod;
            g_module_registry[i].is_cached = true;
            g_module_registry[i].is_loading = false;
            return mod;
        }
    }
    if (strcmp(name, "math") == 0) return uf_mod_math();
    if (strcmp(name, "strings") == 0) return uf_mod_strings();
    if (strcmp(name, "sys") == 0) return uf_mod_sys();
    if (strcmp(name, "fs") == 0) return uf_mod_fs();
    if (strcmp(name, "random") == 0) return uf_mod_random();
    if (strcmp(name, "time") == 0) return uf_mod_time();
    if (strcmp(name, "json") == 0) return uf_mod_json();

    char err_buf[128];
    snprintf(err_buf, sizeof(err_buf), "Module '%s' not found", name);
    uf_raise(err_buf, "ModuleNotFoundError");
    return uf_null();
}

static inline UfVal uf_import_symbol(UfVal mod, const char* mod_name, const char* sym) {
    if (mod.kind != UF_RT_MAP) {
        char err[128];
        snprintf(err, sizeof(err), "Cannot import from non-module '%s'", mod_name);
        uf_raise(err, "ImportError");
        return uf_null();
    }
    UfRtMap* m = mod.as.map;
    for (size_t i = 0; i < m->capacity; ++i) {
        if (m->entries[i].occupied &&
            m->entries[i].key.kind == UF_RT_STRING &&
            strcmp(m->entries[i].key.as.string->chars, sym) == 0) {
            return m->entries[i].value;
        }
    }
    char err[128];
    snprintf(err, sizeof(err), "Cannot import name '%s' from module '%s'", sym, mod_name);
    uf_raise(err, "ImportError");
    return uf_null();
}

#endif /* UNFISH_RUNTIME_H */
