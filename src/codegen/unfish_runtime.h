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
#if !defined(UF_EMBEDDED)
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifndef HUGE_VAL
#define HUGE_VAL (__builtin_huge_val())
#endif

#if defined(__wasm__) || defined(__wasi__)
/* Builtin compiler-rt helpers needed by WASI libc (intscan.o, clock_nanosleep.o) when linking with -nodefaultlibs */
typedef unsigned __int128 __uf_u128;
typedef __int128 __uf_i128;

__uf_i128 __multi3(__uf_i128 a, __uf_i128 b) {
    __uf_u128 ua = (__uf_u128)a;
    __uf_u128 ub = (__uf_u128)b;
    unsigned long long a_lo = (unsigned long long)ua;
    unsigned long long a_hi = (unsigned long long)(ua >> 64);
    unsigned long long b_lo = (unsigned long long)ub;
    unsigned long long b_hi = (unsigned long long)(ub >> 64);

    unsigned int a0 = (unsigned int)a_lo;
    unsigned int a1 = (unsigned int)(a_lo >> 32);
    unsigned int b0 = (unsigned int)b_lo;
    unsigned int b1 = (unsigned int)(b_lo >> 32);

    unsigned long long p0 = (unsigned long long)a0 * b0;
    unsigned long long p1 = (unsigned long long)a0 * b1;
    unsigned long long p2 = (unsigned long long)a1 * b0;
    unsigned long long p3 = (unsigned long long)a1 * b1;

    unsigned long long mid = p1 + (p0 >> 32);
    mid += p2;
    unsigned long long lo = (p0 & 0xFFFFFFFFULL) | (mid << 32);
    unsigned long long hi = p3 + (mid >> 32) + (a_lo * b_hi) + (a_hi * b_lo);

    return (__uf_i128)(((__uf_u128)hi << 64) | lo);
}

__uf_i128 __muloti4(__uf_i128 a, __uf_i128 b, int* overflow) {
    *overflow = 0;
    return __multi3(a, b);
}
#endif

#if defined(UF_EMBEDDED)
#ifndef UF_EMBEDDED_HEAP_SIZE
#define UF_EMBEDDED_HEAP_SIZE (256 * 1024)
#endif

static uint8_t g_uf_embedded_heap[UF_EMBEDDED_HEAP_SIZE];
static size_t g_uf_embedded_heap_offset = 0;

static inline void* uf_embedded_malloc(size_t sz) {
    size_t aligned = (sz + 7) & ~((size_t)7);
    if (g_uf_embedded_heap_offset + aligned > UF_EMBEDDED_HEAP_SIZE) {
        return NULL;
    }
    void* ptr = &g_uf_embedded_heap[g_uf_embedded_heap_offset];
    g_uf_embedded_heap_offset += aligned;
    return ptr;
}

static inline void* uf_embedded_realloc(void* ptr, size_t new_sz) {
    if (!ptr) return uf_embedded_malloc(new_sz);
    void* new_ptr = uf_embedded_malloc(new_sz);
    if (new_ptr) {
        memcpy(new_ptr, ptr, new_sz);
    }
    return new_ptr;
}

static inline void uf_embedded_free(void* ptr) {
    (void)ptr;
}

static inline void uf_embedded_reset_heap(void) {
    g_uf_embedded_heap_offset = 0;
}

static inline size_t uf_embedded_heap_used(void) {
    return g_uf_embedded_heap_offset;
}

#define malloc(sz) uf_embedded_malloc(sz)
#define realloc(ptr, sz) uf_embedded_realloc(ptr, sz)
#define free(ptr) uf_embedded_free(ptr)

typedef void (*UfPutCharFn)(char c);
static UfPutCharFn g_uf_putchar = NULL;

static inline void uf_set_putchar(UfPutCharFn fn) {
    g_uf_putchar = fn;
}

static inline void uf_embedded_putchar(char c) {
    if (g_uf_putchar) {
        g_uf_putchar(c);
    } else {
#if !defined(__arm__) || defined(__linux__)
        fputc(c, stdout);
#endif
    }
}

#if defined(__arm__) && !defined(__linux__)
int _write(int file, char *ptr, int len) {
    (void)file;
    for (int i = 0; i < len; i++) {
        uf_embedded_putchar(ptr[i]);
    }
    return len;
}
int _read(int file, char *ptr, int len) { (void)file; (void)ptr; (void)len; return 0; }
int _close(int file) { (void)file; return -1; }
int _lseek(int file, int ptr, int dir) { (void)file; (void)ptr; (void)dir; return 0; }
int _fstat(int file, void *st) { (void)file; (void)st; return 0; }
int _isatty(int file) { (void)file; return 1; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
void* _sbrk(int incr) { (void)incr; return (void*)-1; }
#endif

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
    UF_RT_ERROR,
    UF_RT_CHANNEL,
    UF_RT_FIBER,
    UF_RT_BOUND_METHOD,
    UF_RT_ENUM_DEF,
    UF_RT_ENUM_VAL,
    UF_RT_PROMISE
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
typedef struct UfRtChannel UfRtChannel;
typedef struct UfRtFiber UfRtFiber;
typedef struct UfRtBoundMethod UfRtBoundMethod;
typedef struct UfRtEnumDef UfRtEnumDef;
typedef struct UfRtEnumVal UfRtEnumVal;
typedef struct UfRtPromise UfRtPromise;

struct UfRtEnumDef {
    UfRtHeader header;
    const char* name;
    size_t variant_count;
    const char** variant_names;
    size_t* variant_field_counts;
    const char*** variant_field_names;
    UfVal* variant_templates;
};

struct UfRtEnumVal {
    UfRtHeader header;
    UfRtEnumDef* def;
    int tag;
    const char* variant_name;
    size_t field_count;
    UfVal* fields;
};

typedef struct {
    UfRtHeader header;
    const char* name;
    const char** field_names;
    size_t field_count;
    UfVal* fields;
    const char** method_names;
    struct UfRtClosure** method_closures;
    size_t method_count;
} UfRtInstance;

typedef struct {
    UfRtHeader header;
    size_t size;
    uint8_t* data;
} UfRtBuffer;

typedef UfVal (*UfRtNativeFn)(void* env, size_t argc, UfVal* args);

typedef struct UfRtClosure {
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
        UfRtChannel* channel;
        UfRtFiber* fiber;
        UfRtBoundMethod* bound_method;
        UfRtEnumDef* enum_def;
        UfRtEnumVal* enum_val;
        UfRtPromise* promise;
        void* ptr;
    } as;
} UfVal;

struct UfRtBoundMethod {
    UfRtHeader header;
    UfVal receiver;
    UfRtClosure* closure;
};

struct UfRtError {
    UfRtHeader header;
    UfVal message;
    UfVal kind;
};

struct UfRtPromise {
    UfRtHeader header;
    int state; /* 0: pending, 1: resolved, 2: rejected */
    UfVal result;
    UfVal error;
    UfRtFiber** waiters;
    size_t waiter_count;
    size_t waiter_capacity;
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
    uint32_t hash;   /* cached key hash, valid while `occupied` */
    bool occupied;   /* slot holds a live entry */
    bool tombstone;  /* slot held an entry that was deleted; probing continues */
} UfRtMapEntry;

struct UfRtMap {
    UfRtHeader header;
    UfRtMapEntry* entries;
    size_t count;
    size_t capacity;      /* always a power of two, so hash & (capacity-1) indexes */
    size_t tombstones;
    UfVal* order_keys;
    size_t order_count;
};

struct UfRtFiber {
    UfRtHeader header;
    uint32_t id;
    UfVal callable;
    size_t argc;
    UfVal* args;
    UfVal result;
    UfRtFiber* next;
    UfRtFiber* prev;
};

struct UfRtChannel {
    UfRtHeader header;
    size_t capacity;
    size_t count;
    size_t head;
    size_t tail;
    UfVal* buffer;
    bool closed;
    UfRtFiber* wait_recv_head;
    UfRtFiber* wait_recv_tail;
};

typedef struct {
    UfRtFiber* run_head;
    UfRtFiber* run_tail;
    UfRtFiber* current;
    int fiber_count;
    uint32_t next_id;
} UfRtScheduler;

static UfRtScheduler g_scheduler;

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
    int call_depth; /* g_uf_call_depth when the frame was pushed */
} UfCatchFrame;

static UfCatchFrame* g_catch_stack = NULL;

/* --- Recursion depth guard ---------------------------------------------
 * Native code recurses on the real C stack, so runaway recursion would
 * segfault the process instead of raising the catchable StackOverflowError
 * that the interpreter and both VMs produce. Every generated Unfish function
 * calls uf_rt_check_stack() on entry.
 *
 * We measure how far the stack has grown from main() rather than counting
 * frames: a counter would need a matching decrement on every exit path, and
 * `uf_throw` longjmps straight out of nested calls without running any
 * epilogue, which would leave the count permanently skewed. Comparing
 * addresses as uintptr_t also keeps us clear of the undefined behaviour of
 * relational operators on unrelated pointers. */
static uintptr_t g_uf_stack_base = 0;

/* Well under the usual 8 MB default so the guard fires before the OS does,
 * with room left for uf_raise() itself to build and throw the error. */
#define UF_RT_STACK_LIMIT_BYTES ((uintptr_t)4 * 1024 * 1024)

/* Call depth, capped at the interpreter's and VMs' limit so a program's
 * recursion limit does not depend on the backend. The byte budget above is
 * not enough on its own: under WebAssembly most of a frame lives on the
 * engine's native stack rather than the linear-memory stack it measures, so
 * the engine aborts the module long before the budget is reached.
 *
 * A counter needs a decrement on every exit path. Normal returns get it from
 * a cleanup variable (UF_RT_ENTER_FRAME); uf_throw longjmps past those, so
 * each catch frame records the depth it was pushed at and uf_throw restores
 * it. Compilers without the cleanup attribute keep only the byte budget. */
#define UF_RT_MAX_CALL_DEPTH 512
static int g_uf_call_depth = 0;

static inline void uf_catch_push(UfCatchFrame* frame) {
    frame->prev = g_catch_stack;
    frame->error.kind = UF_RT_NULL;
    frame->call_depth = g_uf_call_depth;
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
    char _stack_probe;
    /* Only ever used for pointer arithmetic, never dereferenced, so it stays
     * valid as a reference point after uf_init returns. */
    g_uf_stack_base = (uintptr_t)&_stack_probe;
    g_uf_rt.all_objects = NULL;
    g_uf_rt.argc = argc;
    g_uf_rt.argv = argv;
    g_uf_rt.all_boxes = NULL;
    g_catch_stack = NULL;
    g_scheduler.run_head = NULL;
    g_scheduler.run_tail = NULL;
    g_scheduler.current = NULL;
    g_scheduler.fiber_count = 0;
    g_scheduler.next_id = 1;
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
        } else if (curr->kind == UF_RT_CHANNEL) {
            UfRtChannel* ch = (UfRtChannel*)curr;
            free(ch->buffer);
        } else if (curr->kind == UF_RT_FIBER) {
            UfRtFiber* fib = (UfRtFiber*)curr;
            free(fib->args);
        } else if (curr->kind == UF_RT_ENUM_DEF) {
            UfRtEnumDef* ed = (UfRtEnumDef*)curr;
            free(ed->variant_templates);
        } else if (curr->kind == UF_RT_ENUM_VAL) {
            UfRtEnumVal* ev = (UfRtEnumVal*)curr;
            free(ev->fields);
        } else if (curr->kind == UF_RT_PROMISE) {
            UfRtPromise* p = (UfRtPromise*)curr;
            if (p->waiters) free(p->waiters);
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

static inline UfVal uf_instance_new(const char* name, const char** field_names, size_t field_count, const char** method_names, struct UfRtClosure** method_closures, size_t method_count, size_t argc, UfVal* args) {
    if (argc != field_count) {
        fprintf(stderr, "Runtime Error: Struct '%s' expects %zu fields, but %zu provided\n", name, field_count, argc);
        exit(3);
    }
    UfRtInstance* inst = (UfRtInstance*)uf_rt_alloc(UF_RT_INSTANCE, sizeof(UfRtInstance));
    inst->name = name;
    inst->field_names = field_names;
    inst->field_count = field_count;
    inst->method_names = method_names;
    inst->method_closures = method_closures;
    inst->method_count = method_count;
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

static inline UfVal uf_enum_def_new(const char* name, size_t variant_count, const char** variant_names, size_t* variant_field_counts, const char*** variant_field_names) {
    UfRtEnumDef* def = (UfRtEnumDef*)uf_rt_alloc(UF_RT_ENUM_DEF, sizeof(UfRtEnumDef));
    def->name = name;
    def->variant_count = variant_count;
    def->variant_names = variant_names;
    def->variant_field_counts = variant_field_counts;
    def->variant_field_names = variant_field_names;
    def->variant_templates = (UfVal*)malloc(sizeof(UfVal) * variant_count);
    UfVal v; v.kind = UF_RT_ENUM_DEF; v.as.enum_def = def;
    return v;
}

static inline UfVal uf_enum_val_new(UfRtEnumDef* def, int tag, const char* variant_name, size_t field_count, UfVal* fields) {
    UfRtEnumVal* ev = (UfRtEnumVal*)uf_rt_alloc(UF_RT_ENUM_VAL, sizeof(UfRtEnumVal));
    ev->def = def;
    ev->tag = tag;
    ev->variant_name = variant_name;
    ev->field_count = field_count;
    if (field_count > 0 && fields != NULL) {
        ev->fields = (UfVal*)malloc(sizeof(UfVal) * field_count);
        for (size_t i = 0; i < field_count; ++i) {
            ev->fields[i] = fields[i];
        }
    } else {
        ev->fields = NULL;
    }
    UfVal v; v.kind = UF_RT_ENUM_VAL; v.as.enum_val = ev;
    return v;
}

static inline UfVal uf_bound_method_new(UfVal receiver, struct UfRtClosure* closure) {
    UfRtBoundMethod* bm = (UfRtBoundMethod*)uf_rt_alloc(UF_RT_BOUND_METHOD, sizeof(UfRtBoundMethod));
    bm->receiver = receiver;
    bm->closure = closure;
    UfVal v; v.kind = UF_RT_BOUND_METHOD; v.as.bound_method = bm; return v;
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
    if (callee.kind == UF_RT_BOUND_METHOD && callee.as.bound_method != NULL) {
        UfRtBoundMethod* bm = callee.as.bound_method;
        size_t new_argc = argc + 1;
        UfVal method_stack_args[17];
        UfVal* new_args = (new_argc <= 17) ? method_stack_args : (UfVal*)malloc(sizeof(UfVal) * new_argc);
        new_args[0] = bm->receiver;
        for (size_t i = 0; i < argc; ++i) {
            new_args[i + 1] = args[i];
        }
        UfVal result = bm->closure->fn(bm->closure->env, new_argc, new_args);
        if (new_args != method_stack_args) free(new_args);
        if (args != stack_args) free(args);
        return result;
    }
    if (callee.kind == UF_RT_ENUM_VAL && callee.as.enum_val != NULL) {
        UfRtEnumVal* ev = callee.as.enum_val;
        if (ev->def && (size_t)ev->tag < ev->def->variant_count) {
            size_t expected = ev->def->variant_field_counts[ev->tag];
            if (argc != expected) {
                if (args != stack_args) free(args);
                fprintf(stderr, "TypeError: Enum variant '%s' expects %zu argument%s, but %zu provided\n",
                        ev->variant_name, expected, expected == 1 ? "" : "s", argc);
                exit(3);
            }
            UfVal result = uf_enum_val_new(ev->def, ev->tag, ev->variant_name, argc, args);
            if (args != stack_args) free(args);
            return result;
        }
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

static inline UfVal uf_str_l(const char* s, size_t len) {
    UfRtString* str = (UfRtString*)uf_rt_alloc(UF_RT_STRING, sizeof(UfRtString) + len + 1);
    str->length = len;
    if (len > 0 && s) memcpy(str->chars, s, len);
    str->chars[len] = '\0';
    UfVal v; v.kind = UF_RT_STRING; v.as.string = str; return v;
}

static inline UfVal uf_str(const char* s) {
    return uf_str_l(s, s ? strlen(s) : 0);
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
        g_uf_call_depth = target->call_depth;
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

static inline void uf_rt_check_stack(void) {
    char probe;
    uintptr_t here = (uintptr_t)&probe;
    uintptr_t used = (here < g_uf_stack_base) ? (g_uf_stack_base - here)
                                              : (here - g_uf_stack_base);
    if (g_uf_stack_base != 0 && used > UF_RT_STACK_LIMIT_BYTES) {
        uf_raise("StackOverflowError: Maximum call stack depth exceeded",
                 "StackOverflowError");
    }
}

static inline int uf_rt_enter_frame(void) {
    uf_rt_check_stack();
    if (g_uf_call_depth >= UF_RT_MAX_CALL_DEPTH) {
        uf_raise("StackOverflowError: Maximum call stack depth exceeded (512 frames)",
                 "StackOverflowError");
    }
    g_uf_call_depth++;
    return 0;
}

#if defined(__GNUC__) || defined(__clang__)
static inline void uf_rt_leave_frame(int* token) {
    (void)token;
    g_uf_call_depth--;
}
#define UF_RT_ENTER_FRAME() \
    int _uf_frame_token __attribute__((cleanup(uf_rt_leave_frame), unused)) = uf_rt_enter_frame()
#else
#define UF_RT_ENTER_FRAME() uf_rt_check_stack()
#endif

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

/* --- Map hashing -------------------------------------------------------
 * Map keys are always strings. Entries live in an open-addressed table
 * probed linearly from the key's hash; `capacity` is kept a power of two so
 * the modulo is a mask. Deletion leaves a tombstone so that probe chains
 * running through the removed slot stay intact. */

static inline size_t uf_map_round_capacity(size_t requested) {
    size_t cap = 8;
    /* Stop before the shift overflows to 0 and spins forever. Callers pass
     * literal/collection sizes, so this ceiling is never reached in practice. */
    size_t max_cap = ((size_t)1) << (sizeof(size_t) * 8 - 2);
    while (cap < requested && cap < max_cap) cap <<= 1;
    return cap;
}

static inline uint32_t uf_rt_str_hash(const char* chars, size_t length) {
    /* FNV-1a */
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i) {
        hash ^= (uint8_t)chars[i];
        hash *= 16777619u;
    }
    return hash;
}

static inline uint32_t uf_map_key_hash(UfVal key) {
    if (key.kind != UF_RT_STRING) return 0;
    return uf_rt_str_hash(key.as.string->chars, key.as.string->length);
}

static inline bool uf_map_key_eq(UfVal entry_key, const char* chars, size_t length) {
    if (entry_key.kind != UF_RT_STRING) return false;
    UfRtString* s = entry_key.as.string;
    return s->length == length && memcmp(s->chars, chars, length) == 0;
}

/* Locate `chars` in the table. On a hit, sets *found and returns the live
 * slot. On a miss, returns the slot the key should be inserted into (the
 * first tombstone passed, else the terminating empty slot). */
static inline size_t uf_map_find_slot(UfRtMap* m, const char* chars, size_t length,
                                      uint32_t hash, bool* found) {
    size_t mask = m->capacity - 1;
    size_t idx = (size_t)hash & mask;
    size_t insert_at = m->capacity; /* sentinel: no tombstone seen yet */
    *found = false;
    for (size_t probed = 0; probed < m->capacity; ++probed) {
        UfRtMapEntry* e = &m->entries[idx];
        if (e->occupied) {
            if (e->hash == hash && uf_map_key_eq(e->key, chars, length)) {
                *found = true;
                return idx;
            }
        } else if (e->tombstone) {
            if (insert_at == m->capacity) insert_at = idx;
        } else {
            return (insert_at == m->capacity) ? idx : insert_at;
        }
        idx = (idx + 1) & mask;
    }
    return (insert_at == m->capacity) ? 0 : insert_at;
}

static inline UfVal uf_map_new(size_t capacity) {
    UfRtMap* m = (UfRtMap*)uf_rt_alloc(UF_RT_MAP, sizeof(UfRtMap));
    m->count = 0;
    m->tombstones = 0;
    m->capacity = uf_map_round_capacity(capacity);
    m->entries = (UfRtMapEntry*)calloc(m->capacity, sizeof(UfRtMapEntry));
    m->order_keys = (UfVal*)malloc(sizeof(UfVal) * m->capacity);
    m->order_count = 0;
    UfVal v; v.kind = UF_RT_MAP; v.as.map = m; return v;
}

/* Look up a C string key; returns NULL when absent. */
static inline UfRtMapEntry* uf_map_lookup(UfRtMap* m, const char* chars, size_t length) {
    bool found = false;
    size_t slot = uf_map_find_slot(m, chars, length, uf_rt_str_hash(chars, length), &found);
    return found ? &m->entries[slot] : NULL;
}

static inline void uf_map_grow(UfRtMap* m) {
    size_t old_cap = m->capacity;
    UfRtMapEntry* old_entries = m->entries;

    m->capacity = old_cap * 2;
    m->entries = (UfRtMapEntry*)calloc(m->capacity, sizeof(UfRtMapEntry));
    m->count = 0;
    m->tombstones = 0; /* tombstones are dropped by the rehash */

    size_t mask = m->capacity - 1;
    for (size_t i = 0; i < old_cap; ++i) {
        if (!old_entries[i].occupied) continue;
        size_t idx = (size_t)old_entries[i].hash & mask;
        while (m->entries[idx].occupied) idx = (idx + 1) & mask;
        m->entries[idx] = old_entries[i];
        m->count++;
    }
    free(old_entries);
    m->order_keys = (UfVal*)realloc(m->order_keys, sizeof(UfVal) * m->capacity);
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
        case UF_RT_CHANNEL: return v.as.channel && (!v.as.channel->closed || v.as.channel->count > 0);
        case UF_RT_FIBER: return true;
        case UF_RT_BOUND_METHOD: return true;
        case UF_RT_ENUM_DEF: return true;
        case UF_RT_ENUM_VAL: return true;
        case UF_RT_PROMISE: return true;
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
            if (isnan(v.as.number)) return strdup("nan");
            if (isinf(v.as.number)) return strdup(v.as.number > 0 ? "inf" : "-inf");
            if (floor(v.as.number) == v.as.number && fabs(v.as.number) < 1e15) {
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
        case UF_RT_CHANNEL: {
            char cbuf[128];
            snprintf(cbuf, sizeof(cbuf), "<channel cap=%zu len=%zu>",
                     v.as.channel ? v.as.channel->capacity : 0,
                     v.as.channel ? v.as.channel->count : 0);
            return strdup(cbuf);
        }
        case UF_RT_FIBER: {
            char fbuf[128];
            snprintf(fbuf, sizeof(fbuf), "<fiber #%lu>",
                     (unsigned long)(v.as.fiber ? v.as.fiber->id : 0));
            return strdup(fbuf);
        }
        case UF_RT_BOUND_METHOD: return strdup("<bound method>");
        case UF_RT_PROMISE: {
            const char* st = "pending";
            if (v.as.promise) {
                if (v.as.promise->state == 1) st = "resolved";
                else if (v.as.promise->state == 2) st = "rejected";
            }
            char pbuf[128];
            snprintf(pbuf, sizeof(pbuf), "<promise (%s)>", st);
            return strdup(pbuf);
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
                {
                    UfVal _ok = v.as.map->order_keys[i];
                    UfRtMapEntry* _e = uf_map_lookup(v.as.map, _ok.as.string->chars,
                                                     _ok.as.string->length);
                    if (_e) {
                        char* val_s = uf_to_str_impl(_e->value, vis);
                        if (strlen(res) + strlen(val_s) + 4 >= cap) {
                            cap = (cap + strlen(val_s)) * 2;
                            res = (char*)realloc(res, cap);
                        }
                        strcat(res, val_s);
                        free(val_s);
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
        case UF_RT_ENUM_DEF: {
            char ebuf[256];
            snprintf(ebuf, sizeof(ebuf), "<enum %s>",
                     (v.as.enum_def && v.as.enum_def->name) ? v.as.enum_def->name : "anonymous");
            return strdup(ebuf);
        }
        case UF_RT_ENUM_VAL: {
            UfRtEnumVal* ev = v.as.enum_val;
            if (!ev) return strdup("EnumVal");
            if (ev->field_count == 0) {
                char vbuf[256];
                snprintf(vbuf, sizeof(vbuf), "%s.%s", (ev->def && ev->def->name) ? ev->def->name : "", ev->variant_name ? ev->variant_name : "");
                return strdup(vbuf);
            }
            size_t cap = 64;
            char* res = (char*)malloc(cap);
            snprintf(res, cap, "%s(", ev->variant_name ? ev->variant_name : "");
            size_t len = strlen(res);
            for (size_t i = 0; i < ev->field_count; ++i) {
                if (i > 0) {
                    if (len + 3 >= cap) { cap *= 2; res = (char*)realloc(res, cap); }
                    memcpy(res + len, ", ", 2);
                    len += 2;
                    res[len] = '\0';
                }
                char* s = uf_to_str_impl(ev->fields[i], vis);
                size_t slen = strlen(s);
                while (len + slen + 2 >= cap) { cap *= 2; res = (char*)realloc(res, cap); }
                memcpy(res + len, s, slen);
                len += slen;
                res[len] = '\0';
                free(s);
            }
            res[len++] = ')';
            res[len] = '\0';
            return res;
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
#if defined(UF_EMBEDDED)
    const char* p = s ? s : "null";
    while (*p) uf_embedded_putchar(*p++);
    uf_embedded_putchar('\n');
#else
    printf("%s\n", s ? s : "null");
#endif
    free(s);
}

static inline void uf_print(UfVal v) {
    char* s = uf_to_str(v);
#if defined(UF_EMBEDDED)
    const char* p = s ? s : "null";
    while (*p) uf_embedded_putchar(*p++);
#else
    printf("%s", s ? s : "null");
#endif
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
        case UF_RT_BOUND_METHOD: {
            if (a.as.bound_method == b.as.bound_method) return true;
            if (!a.as.bound_method || !b.as.bound_method) return false;
            return uf_eq_bool_impl(a.as.bound_method->receiver, b.as.bound_method->receiver, vis) &&
                   a.as.bound_method->closure == b.as.bound_method->closure;
        }
        case UF_RT_ENUM_DEF:
            return a.as.enum_def == b.as.enum_def;
        case UF_RT_ENUM_VAL: {
            UfRtEnumVal* ea = a.as.enum_val;
            UfRtEnumVal* eb = b.as.enum_val;
            if (ea == eb) return true;
            if (!ea || !eb) return false;
            if (ea->def != eb->def) return false;
            if (ea->tag != eb->tag) return false;
            if (ea->field_count != eb->field_count) return false;
            for (size_t i = 0; i < ea->field_count; ++i) {
                if (!uf_eq_bool_impl(ea->fields[i], eb->fields[i], vis)) return false;
            }
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
        case UF_RT_BOUND_METHOD: return uf_str("function");
        case UF_RT_INSTANCE: return uf_str(v.as.instance->name);
        case UF_RT_ENUM_DEF: return uf_str("enum");
        case UF_RT_ENUM_VAL: return uf_str((v.as.enum_val && v.as.enum_val->def && v.as.enum_val->def->name) ? v.as.enum_val->def->name : "enum_val");
        case UF_RT_ERROR: return uf_str("error");
        case UF_RT_CHANNEL: return uf_str("channel");
        case UF_RT_FIBER: return uf_str("fiber");
        case UF_RT_PROMISE: return uf_str("promise");
        default: return uf_str("object");
    }
}

static inline UfVal uf_get(UfVal target, UfVal index) {
    if (target.kind == UF_RT_ENUM_DEF) {
        if (index.kind == UF_RT_STRING) {
            const char* prop = index.as.string->chars;
            UfRtEnumDef* ed = target.as.enum_def;
            for (size_t i = 0; i < ed->variant_count; ++i) {
                if (strcmp(ed->variant_names[i], prop) == 0) {
                    return ed->variant_templates[i];
                }
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "Enum '%s' has no variant '%s'", ed->name ? ed->name : "", prop);
            uf_raise(buf, "AttributeError");
            return uf_null();
        }
    }
    if (target.kind == UF_RT_ENUM_VAL) {
        UfRtEnumVal* ev = target.as.enum_val;
        if (index.kind == UF_RT_NUMBER) {
            long idx = (long)index.as.number;
            if (idx >= 0 && (size_t)idx < ev->field_count) {
                return ev->fields[idx];
            }
            return uf_null();
        }
        if (index.kind == UF_RT_STRING) {
            const char* prop = index.as.string->chars;
            if (strcmp(prop, "tag") == 0) return uf_num((double)ev->tag);
            if (strcmp(prop, "name") == 0) return uf_str(ev->variant_name ? ev->variant_name : "");
            if (ev->def && (size_t)ev->tag < ev->def->variant_count && ev->def->variant_field_names) {
                const char** fnames = ev->def->variant_field_names[ev->tag];
                if (fnames) {
                    for (size_t f = 0; f < ev->field_count; ++f) {
                        if (strcmp(fnames[f], prop) == 0) {
                            return ev->fields[f];
                        }
                    }
                }
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "Enum variant '%s' has no field '%s'", ev->variant_name ? ev->variant_name : "", prop);
            uf_raise(buf, "AttributeError");
            return uf_null();
        }
    }
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
        for (size_t i = 0; i < inst->method_count; ++i) {
            if (strcmp(inst->method_names[i], fname) == 0) {
                return uf_bound_method_new(target, inst->method_closures[i]);
            }
        }
        fprintf(stderr, "Runtime Error: Struct '%s' has no field or method '%s'\n", inst->name, fname);
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
        UfRtMapEntry* e = uf_map_lookup(target.as.map, index.as.string->chars,
                                        index.as.string->length);
        return e ? e->value : uf_null();
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
        uint32_t hash = uf_map_key_hash(index);
        const char* kchars = index.as.string->chars;
        size_t klen = index.as.string->length;

        /* Update in place if the key is already present. */
        bool found = false;
        size_t slot = uf_map_find_slot(m, kchars, klen, hash, &found);
        if (found) {
            m->entries[slot].value = value;
            return;
        }

        /* Grow at a 75% load factor, counting tombstones: they occupy slots
         * and lengthen probe chains just as live entries do. */
        if ((m->count + m->tombstones + 1) * 4 >= m->capacity * 3) {
            uf_map_grow(m);
            slot = uf_map_find_slot(m, kchars, klen, hash, &found);
        }

        if (m->entries[slot].tombstone) m->tombstones--;
        m->entries[slot].occupied = true;
        m->entries[slot].tombstone = false;
        m->entries[slot].hash = hash;
        m->entries[slot].key = index;
        m->entries[slot].value = value;
        m->count++;
        m->order_keys[m->order_count++] = index;
        return;
    }
    fprintf(stderr, "Runtime Error: Cannot assign to index of this type\n"); exit(3);
}

/* Spread and Rest Helpers */
static inline const char* uf_type_name_cstr(UfVal v) {
    switch (v.kind) {
        case UF_RT_NULL: return "null";
        case UF_RT_BOOL: return "boolean";
        case UF_RT_NUMBER: return "number";
        case UF_RT_STRING: return "string";
        case UF_RT_ARRAY: return "array";
        case UF_RT_MAP: return "map";
        case UF_RT_BUFFER: return "buffer";
        case UF_RT_CLOSURE: return "function";
        case UF_RT_BOUND_METHOD: return "function";
        case UF_RT_INSTANCE: return (v.as.instance && v.as.instance->name) ? v.as.instance->name : "instance";
        case UF_RT_ENUM_DEF: return "enum";
        case UF_RT_ENUM_VAL: return (v.as.enum_val && v.as.enum_val->def && v.as.enum_val->def->name) ? v.as.enum_val->def->name : "enum_val";
        case UF_RT_ERROR: return "error";
        case UF_RT_CHANNEL: return "channel";
        case UF_RT_FIBER: return "fiber";
        case UF_RT_PROMISE: return "promise";
        default: return "<unknown>";
    }
}

static inline void uf_array_extend(UfVal target, UfVal src) {
    if (target.kind != UF_RT_ARRAY) return;
    if (src.kind != UF_RT_ARRAY) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Spread operand in array literal must be an array, got '%s'", uf_type_name_cstr(src));
        uf_raise(buf, "TypeError");
        return;
    }
    UfRtArray* s = src.as.array;
    for (size_t i = 0; i < s->count; ++i) {
        uf_array_push(target, s->elements[i]);
    }
}

static inline void uf_map_extend(UfVal target, UfVal src) {
    if (target.kind != UF_RT_MAP) return;
    if (src.kind != UF_RT_MAP) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Spread operand in map literal must be a map, got '%s'", uf_type_name_cstr(src));
        uf_raise(buf, "TypeError");
        return;
    }
    UfRtMap* s = src.as.map;
    for (size_t i = 0; i < s->order_count; ++i) {
        UfVal k = s->order_keys[i];
        UfVal v = uf_get(src, k);
        uf_set(target, k, v);
    }
}

static inline UfVal uf_make_rest_array(size_t argc, const UfVal* args, size_t fixed_count) {
    size_t rest_count = (argc > fixed_count) ? (argc - fixed_count) : 0;
    UfVal arr = uf_array_new(rest_count);
    for (size_t i = 0; i < rest_count; ++i) {
        uf_array_push(arr, args[fixed_count + i]);
    }
    return arr;
}

static inline UfVal uf_make_array_spread(size_t count, ...) {
    va_list va;
    va_start(va, count);
    UfVal res = uf_array_new(count);
    for (size_t i = 0; i < count; ++i) {
        int is_spread = va_arg(va, int);
        UfVal item = va_arg(va, UfVal);
        if (is_spread) {
            uf_array_extend(res, item);
        } else {
            uf_array_push(res, item);
        }
    }
    va_end(va);
    return res;
}

static inline UfVal uf_make_map_spread(size_t count, ...) {
    va_list va;
    va_start(va, count);
    UfVal res = uf_map_new(count);
    for (size_t i = 0; i < count; ++i) {
        int is_spread = va_arg(va, int);
        UfVal k = va_arg(va, UfVal);
        UfVal v = va_arg(va, UfVal);
        if (is_spread) {
            uf_map_extend(res, k);
        } else {
            uf_set(res, k, v);
        }
    }
    va_end(va);
    return res;
}

/* ---- Destructuring Helpers ---- */

static inline void uf_assert_array_destructure(UfVal val) {
    if (val.kind != UF_RT_ARRAY) {
        char buf[128];
        snprintf(buf, sizeof(buf), "TypeError: Cannot destructure non-array value of type '%s'", uf_type_name_cstr(val));
        uf_raise(buf, "TypeError");
    }
}

static inline void uf_assert_map_destructure(UfVal val) {
    if (val.kind != UF_RT_MAP && val.kind != UF_RT_INSTANCE) {
        char buf[128];
        snprintf(buf, sizeof(buf), "TypeError: Cannot destructure non-map value of type '%s'", uf_type_name_cstr(val));
        uf_raise(buf, "TypeError");
    }
}

static inline UfVal uf_array_get_safe(UfVal arr, size_t idx) {
    if (arr.kind != UF_RT_ARRAY) {
        char buf[128];
        snprintf(buf, sizeof(buf), "TypeError: Cannot destructure non-array value of type '%s'", uf_type_name_cstr(arr));
        uf_raise(buf, "TypeError");
        return uf_null();
    }
    if (idx < arr.as.array->count) {
        return arr.as.array->elements[idx];
    }
    return uf_null();
}

static inline UfVal uf_array_slice(UfVal arr, size_t start_idx) {
    if (arr.kind != UF_RT_ARRAY) {
        char buf[128];
        snprintf(buf, sizeof(buf), "TypeError: Cannot destructure non-array value of type '%s'", uf_type_name_cstr(arr));
        uf_raise(buf, "TypeError");
        return uf_null();
    }
    size_t n = arr.as.array->count;
    size_t rest_len = (n > start_idx) ? (n - start_idx) : 0;
    UfVal res = uf_array_new(rest_len);
    for (size_t i = 0; i < rest_len; ++i) {
        uf_array_push(res, arr.as.array->elements[start_idx + i]);
    }
    return res;
}

static inline UfVal uf_destructure_get_key(UfVal target, const char* key) {
    if (target.kind == UF_RT_MAP) {
        UfRtMapEntry* e = uf_map_lookup(target.as.map, key, strlen(key));
        return e ? e->value : uf_null();
    } else if (target.kind == UF_RT_INSTANCE) {
        UfRtInstance* inst = target.as.instance;
        for (size_t i = 0; i < inst->field_count; ++i) {
            if (strcmp(inst->field_names[i], key) == 0) {
                return inst->fields[i];
            }
        }
        return uf_null();
    }
    return uf_null();
}

static inline UfVal uf_map_rest(UfVal target, size_t exclude_count, const char** exclude_keys) {
    UfVal res = uf_map_new(4);
    if (target.kind != UF_RT_MAP) return res;
    UfRtMap* m = target.as.map;
    for (size_t i = 0; i < m->order_count; ++i) {
        UfVal k = m->order_keys[i];
        if (k.kind != UF_RT_STRING) continue;
        bool excluded = false;
        for (size_t j = 0; j < exclude_count; ++j) {
            if (strcmp(k.as.string->chars, exclude_keys[j]) == 0) {
                excluded = true;
                break;
            }
        }
        if (!excluded) {
            UfVal v = uf_get(target, k);
            uf_set(res, k, v);
        }
    }
    return res;
}

/* ---- End Destructuring Helpers ---- */

static inline bool uf_pat_match_variant(UfVal val, const char* name, size_t field_count) {
    if (val.kind == UF_RT_INSTANCE && val.as.instance != NULL) {
        return strcmp(val.as.instance->name, name) == 0 && val.as.instance->field_count == field_count;
    }
    if (val.kind == UF_RT_ENUM_VAL && val.as.enum_val != NULL) {
        return strcmp(val.as.enum_val->variant_name, name) == 0 && val.as.enum_val->field_count == field_count;
    }
    return false;
}

static inline UfVal uf_pat_get_field(UfVal val, size_t idx) {
    if (val.kind == UF_RT_INSTANCE && val.as.instance != NULL) {
        if (idx < val.as.instance->field_count) return val.as.instance->fields[idx];
    }
    if (val.kind == UF_RT_ENUM_VAL && val.as.enum_val != NULL) {
        if (idx < val.as.enum_val->field_count) return val.as.enum_val->fields[idx];
    }
    return uf_null();
}

static inline UfVal uf_call_val_spread(UfVal callee, UfVal args_arr) {
    if (args_arr.kind != UF_RT_ARRAY) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Spread operand in function call must be an array, got '%s'", uf_type_name_cstr(args_arr));
        uf_raise(buf, "TypeError");
        return uf_null();
    }
    UfRtArray* a = args_arr.as.array;
    if (callee.kind == UF_RT_CLOSURE && callee.as.closure != NULL) {
        return callee.as.closure->fn(callee.as.closure->env, a->count, a->elements);
    }
    if (callee.kind == UF_RT_BOUND_METHOD && callee.as.bound_method != NULL) {
        UfRtBoundMethod* bm = callee.as.bound_method;
        size_t new_argc = a->count + 1;
        UfVal method_stack_args[17];
        UfVal* new_args = (new_argc <= 17) ? method_stack_args : (UfVal*)malloc(sizeof(UfVal) * new_argc);
        new_args[0] = bm->receiver;
        for (size_t i = 0; i < a->count; ++i) {
            new_args[i + 1] = a->elements[i];
        }
        UfVal result = bm->closure->fn(bm->closure->env, new_argc, new_args);
        if (new_args != method_stack_args) free(new_args);
        return result;
    }
    if (callee.kind == UF_RT_ENUM_VAL && callee.as.enum_val != NULL) {
        UfRtEnumVal* ev = callee.as.enum_val;
        if (ev->def && (size_t)ev->tag < ev->def->variant_count) {
            size_t expected = ev->def->variant_field_counts[ev->tag];
            if (a->count != expected) {
                fprintf(stderr, "TypeError: Enum variant '%s' expects %zu argument%s, but %zu provided\n",
                        ev->variant_name, expected, expected == 1 ? "" : "s", a->count);
                exit(3);
            }
            return uf_enum_val_new(ev->def, ev->tag, ev->variant_name, a->count, a->elements);
        }
    }
    fprintf(stderr, "Runtime Error: Attempted to call non-callable value\n");
    exit(3);
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

static inline UfVal uf_buffer_slice(UfVal b, UfVal start_v, UfVal len_v) {
    if (b.kind != UF_RT_BUFFER) return uf_null();
    UfRtBuffer* buf = b.as.buffer;
    long start = (start_v.kind == UF_RT_NUMBER) ? (long)start_v.as.number : 0;
    if (start < 0) start = 0;
    if ((size_t)start > buf->size) start = (long)buf->size;

    size_t length = buf->size - (size_t)start;
    if (len_v.kind == UF_RT_NUMBER) {
        long user_len = (long)len_v.as.number;
        if (user_len < 0) user_len = 0;
        if ((size_t)user_len < length) length = (size_t)user_len;
    }
    UfVal res = uf_buffer_new(uf_num((double)length));
    if (length > 0 && buf->data) memcpy(res.as.buffer->data, buf->data + start, length);
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

static inline int64_t _uf_safe_to_i64(double d) {
    if (isnan(d) || isinf(d)) return 0;
    if (d > 9223372036854775807.0) return 9223372036854775807LL;
    if (d < -9223372036854775808.0) return (-9223372036854775807LL - 1);
    return (int64_t)d;
}

static inline UfVal uf_u8(UfVal n) { return uf_num((double)((uint8_t)_uf_safe_to_i64(n.as.number))); }
static inline UfVal uf_i8(UfVal n) { return uf_num((double)((int8_t)_uf_safe_to_i64(n.as.number))); }
static inline UfVal uf_u16(UfVal n) { return uf_num((double)((uint16_t)_uf_safe_to_i64(n.as.number))); }
static inline UfVal uf_i16(UfVal n) { return uf_num((double)((int16_t)_uf_safe_to_i64(n.as.number))); }
static inline UfVal uf_u32(UfVal n) { return uf_num((double)((uint32_t)_uf_safe_to_i64(n.as.number))); }
static inline UfVal uf_i32(UfVal n) { return uf_num((double)((int32_t)_uf_safe_to_i64(n.as.number))); }

static inline UfVal uf_band(UfVal a, UfVal b) { return uf_num((double)(((uint32_t)_uf_safe_to_i64(a.as.number)) & ((uint32_t)_uf_safe_to_i64(b.as.number)))); }
static inline UfVal uf_bor(UfVal a, UfVal b) { return uf_num((double)(((uint32_t)_uf_safe_to_i64(a.as.number)) | ((uint32_t)_uf_safe_to_i64(b.as.number)))); }
static inline UfVal uf_bxor(UfVal a, UfVal b) { return uf_num((double)(((uint32_t)_uf_safe_to_i64(a.as.number)) ^ ((uint32_t)_uf_safe_to_i64(b.as.number)))); }
static inline UfVal uf_bnot(UfVal a) { return uf_num((double)(~((uint32_t)_uf_safe_to_i64(a.as.number)))); }
static inline UfVal uf_shl(UfVal a, UfVal b) { return uf_num((double)(((uint32_t)_uf_safe_to_i64(a.as.number)) << (((uint32_t)_uf_safe_to_i64(b.as.number)) & 31))); }
static inline UfVal uf_shr(UfVal a, UfVal b) { return uf_num((double)(((uint32_t)_uf_safe_to_i64(a.as.number)) >> (((uint32_t)_uf_safe_to_i64(b.as.number)) & 31))); }
static inline UfVal uf_sar(UfVal a, UfVal b) { return uf_num((double)(((int32_t)_uf_safe_to_i64(a.as.number)) >> (((uint32_t)_uf_safe_to_i64(b.as.number)) & 31))); }

static inline UfVal uf_to_hex(UfVal n) {
    char hbuf[32];
    snprintf(hbuf, sizeof(hbuf), "%lx", (unsigned long)(uint64_t)_uf_safe_to_i64(n.as.number));
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

/* Concurrency Primitives */
static inline UfVal uf_val_fiber(UfRtFiber* f) {
    UfVal v; v.kind = UF_RT_FIBER; v.as.fiber = f; return v;
}

static inline UfVal uf_val_channel(UfRtChannel* ch) {
    UfVal v; v.kind = UF_RT_CHANNEL; v.as.channel = ch; return v;
}

static inline void uf_scheduler_enqueue(UfRtFiber* f) {
    f->next = NULL;
    f->prev = g_scheduler.run_tail;
    if (g_scheduler.run_tail) {
        g_scheduler.run_tail->next = f;
        g_scheduler.run_tail = f;
    } else {
        g_scheduler.run_head = f;
        g_scheduler.run_tail = f;
    }
    g_scheduler.fiber_count++;
}

static inline UfVal uf_channel(UfVal cap_val) {
    size_t cap = 0;
    if (cap_val.kind == UF_RT_NUMBER && cap_val.as.number > 0) {
        cap = (size_t)cap_val.as.number;
    }
    UfRtChannel* ch = (UfRtChannel*)uf_rt_alloc(UF_RT_CHANNEL, sizeof(UfRtChannel));
    ch->capacity = cap;
    ch->count = 0;
    ch->head = 0;
    ch->tail = 0;
    ch->closed = false;
    ch->wait_recv_head = NULL;
    ch->wait_recv_tail = NULL;
    ch->buffer = (cap > 0) ? (UfVal*)malloc(sizeof(UfVal) * cap) : NULL;
    return uf_val_channel(ch);
}

static inline UfVal uf_close_channel(UfVal ch_val) {
    if (ch_val.kind == UF_RT_CHANNEL && ch_val.as.channel) {
        ch_val.as.channel->closed = true;
    }
    return uf_null();
}

static inline UfVal uf_send(UfVal ch_val, UfVal val) {
    if (ch_val.kind != UF_RT_CHANNEL || !ch_val.as.channel) return uf_bool(false);
    UfRtChannel* ch = ch_val.as.channel;
    if (ch->closed) {
        fprintf(stderr, "Runtime Error: Cannot send on closed channel\n");
        exit(3);
    }
    if (ch->capacity > 0 && ch->count < ch->capacity) {
        ch->buffer[ch->tail] = val;
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        return uf_bool(true);
    }
    if (ch->wait_recv_head) {
        UfRtFiber* receiver = ch->wait_recv_head;
        ch->wait_recv_head = receiver->next;
        if (!ch->wait_recv_head) ch->wait_recv_tail = NULL;
        receiver->result = val;
        uf_scheduler_enqueue(receiver);
        return uf_bool(true);
    }
    size_t new_cap = ch->capacity == 0 ? 4 : ch->capacity * 2;
    UfVal* new_buf = (UfVal*)malloc(sizeof(UfVal) * new_cap);
    if (ch->capacity > 0) {
        for (size_t i = 0; i < ch->count; ++i) {
            new_buf[i] = ch->buffer[(ch->head + i) % ch->capacity];
        }
    }
    new_buf[ch->count] = val;
    free(ch->buffer);
    ch->buffer = new_buf;
    ch->head = 0;
    ch->count++;
    ch->tail = ch->count % new_cap;
    ch->capacity = new_cap;
    return uf_bool(true);
}

static inline UfVal uf_recv(UfVal ch_val) {
    if (ch_val.kind != UF_RT_CHANNEL || !ch_val.as.channel) return uf_null();
    UfRtChannel* ch = ch_val.as.channel;
    if (ch->count > 0 && ch->capacity > 0) {
        UfVal res = ch->buffer[ch->head];
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
        return res;
    }
    if (ch->closed) {
        return uf_null();
    }
    return uf_null();
}

static inline UfVal uf_yield(UfVal val) {
    if (g_scheduler.current) {
        g_scheduler.current->result = val;
    }
    return val;
}

static inline UfVal uf_spawn(size_t total_args, ...) {
    if (total_args < 1) return uf_null();
    va_list va;
    va_start(va, total_args);
    UfVal callable = va_arg(va, UfVal);
    size_t argc = total_args - 1;
    UfVal* args = NULL;
    if (argc > 0) {
        args = (UfVal*)malloc(sizeof(UfVal) * argc);
        for (size_t i = 0; i < argc; ++i) {
            args[i] = va_arg(va, UfVal);
        }
    }
    va_end(va);
    UfRtFiber* fib = (UfRtFiber*)uf_rt_alloc(UF_RT_FIBER, sizeof(UfRtFiber));
    fib->id = g_scheduler.next_id++;
    fib->callable = callable;
    fib->argc = argc;
    fib->args = args;
    fib->result = uf_null();
    fib->next = NULL;
    fib->prev = NULL;
    uf_scheduler_enqueue(fib);
    return uf_val_fiber(fib);
}

static inline UfVal uf_run_scheduler(void) {
    int completed = 0;
    while (g_scheduler.run_head) {
        UfRtFiber* fib = g_scheduler.run_head;
        g_scheduler.run_head = fib->next;
        if (g_scheduler.run_head) {
            g_scheduler.run_head->prev = NULL;
        } else {
            g_scheduler.run_tail = NULL;
        }
        fib->next = NULL;
        fib->prev = NULL;
        g_scheduler.current = fib;
        UfVal res = uf_null();
        if (fib->callable.kind == UF_RT_CLOSURE && fib->callable.as.closure) {
            res = fib->callable.as.closure->fn(fib->callable.as.closure->env, fib->argc, fib->args);
        }
        fib->result = res;
        completed++;
        if (g_scheduler.fiber_count > 0) g_scheduler.fiber_count--;
        g_scheduler.current = NULL;
    }
    return uf_num((double)completed);
}

static inline UfVal uf_val_promise(UfRtPromise* p) {
    UfVal v; v.kind = UF_RT_PROMISE; v.as.promise = p; return v;
}

static inline UfRtPromise* uf_promise_create_c(void) {
    UfRtPromise* p = (UfRtPromise*)uf_rt_alloc(UF_RT_PROMISE, sizeof(UfRtPromise));
    p->state = 0; /* pending */
    p->result = uf_null();
    p->error = uf_null();
    p->waiters = NULL;
    p->waiter_count = 0;
    p->waiter_capacity = 0;
    return p;
}

static inline UfVal uf_promise_resolved(UfVal val) {
    UfRtPromise* p = uf_promise_create_c();
    p->state = 1; /* resolved */
    p->result = val;
    return uf_val_promise(p);
}

static inline UfVal uf_promise_await_c(UfRtPromise* p) {
    if (!p) return uf_null();
    if (p->state == 1) return p->result;
    if (p->state == 2) {
        char* err = uf_to_str(p->error);
        fprintf(stderr, "Runtime Error: Unhandled promise rejection: %s\n", err ? err : "error");
        if (err) free(err);
        exit(3);
    }
    while (p->state == 0 && g_scheduler.run_head) {
        uf_run_scheduler();
    }
    if (p->state == 1) return p->result;
    if (p->state == 2) {
        char* err = uf_to_str(p->error);
        fprintf(stderr, "Runtime Error: Unhandled promise rejection: %s\n", err ? err : "error");
        if (err) free(err);
        exit(3);
    }
    return uf_null();
}

static inline UfVal uf_await(UfVal val) {
    if (val.kind == UF_RT_PROMISE && val.as.promise) {
        return uf_promise_await_c(val.as.promise);
    }
    return val;
}

static inline UfVal uf_run_async(size_t total_args, ...) {
    if (total_args < 1) return uf_null();
    va_list va;
    va_start(va, total_args);
    UfVal callable = va_arg(va, UfVal);
    size_t argc = total_args - 1;
    UfVal* args = NULL;
    if (argc > 0) {
        args = (UfVal*)malloc(sizeof(UfVal) * argc);
        for (size_t i = 0; i < argc; ++i) {
            args[i] = va_arg(va, UfVal);
        }
    }
    va_end(va);
    UfVal res = uf_null();
    if (callable.kind == UF_RT_PROMISE && callable.as.promise) {
        res = uf_promise_await_c(callable.as.promise);
    } else if (callable.kind == UF_RT_CLOSURE && callable.as.closure) {
        res = callable.as.closure->fn(callable.as.closure->env, argc, args);
        if (res.kind == UF_RT_PROMISE && res.as.promise) {
            res = uf_promise_await_c(res.as.promise);
        }
    }
    if (args) free(args);
    uf_run_scheduler();
    return res;
}

/* Math Primitives */
static inline UfVal uf_math_abs(UfVal a) { return uf_num(fabs(a.as.number)); }
static inline UfVal uf_math_floor(UfVal a) { return uf_num(floor(a.as.number)); }
static inline UfVal uf_math_ceil(UfVal a) { return uf_num(ceil(a.as.number)); }
static inline UfVal uf_math_round(UfVal a) { return uf_num(round(a.as.number)); }
/* sqrt/log validate their domain here for the same reason the interpreter and
 * both VMs do: returning a silent nan/-inf from the native tier would mean a
 * program that reports a clean, catchable error in development quietly
 * produces garbage once compiled. Messages match uf_stdlib.c exactly. */
static inline UfVal uf_math_sqrt(UfVal a) {
    if (a.as.number < 0) {
        uf_raise("'sqrt()' domain error: cannot compute square root of negative number", "DomainError");
    }
    return uf_num(sqrt(a.as.number));
}
static inline UfVal uf_math_pow(UfVal a, UfVal b) { return uf_num(pow(a.as.number, b.as.number)); }
static inline UfVal uf_math_min(UfVal a, UfVal b) { return uf_num(fmin(a.as.number, b.as.number)); }
static inline UfVal uf_math_max(UfVal a, UfVal b) { return uf_num(fmax(a.as.number, b.as.number)); }
static inline UfVal uf_math_log(UfVal a) {
    if (a.as.number <= 0) {
        uf_raise("'log()' domain error: argument must be positive", "DomainError");
    }
    return uf_num(log(a.as.number));
}
static inline UfVal uf_math_sin(UfVal a) { return uf_num(sin(a.as.number)); }
static inline UfVal uf_math_cos(UfVal a) { return uf_num(cos(a.as.number)); }
static inline UfVal uf_math_tan(UfVal a) { return uf_num(tan(a.as.number)); }
static inline UfVal uf_math_random(void) { return uf_num((double)rand() / ((double)RAND_MAX + 1.0)); }
static inline UfVal uf_math_random_int(UfVal min_v, UfVal max_v) {
    long mn = (long)min_v.as.number, mx = (long)max_v.as.number;
    if (mx < mn) { long t = mn; mn = mx; mx = t; }
    long span = mx - mn + 1;
    if (span <= 0) return uf_num((double)mn);
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
    /* char_at() is deliberately NOT string indexing: a negative index counts
     * from the end and an out-of-range index yields "" rather than raising,
     * matching std_char_at() in uf_stdlib.c. Delegating to uf_get() instead
     * aborted the program on any out-of-range index. */
    if (s.kind != UF_RT_STRING || idx.kind != UF_RT_NUMBER) return uf_str("");
    UfRtString* str = s.as.string;
    long i = (long)idx.as.number;
    if (i < 0) i += (long)str->length;
    if (i < 0 || (size_t)i >= str->length) return uf_str("");
    char ch[2];
    ch[0] = str->chars[i];
    ch[1] = '\0';
    return uf_str(ch);
}

static inline UfVal uf_str_to_number(UfVal s) {
    if (s.kind != UF_RT_STRING) return uf_null();
    const char* str = s.as.string->chars;
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return uf_null();

    char* endptr = NULL;
    double d = strtod(str, &endptr);
    if (endptr == str) return uf_null();
    /* Trailing garbage makes the whole conversion fail, matching uf_stdlib.c:
     * to_number("12abc") is null, not 12. */
    while (isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') return uf_null();

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
    /* Clamping order mirrors std_substring() in uf_stdlib.c: a negative index
     * counts back from the end *first*, and only then is the range clamped.
     * Clamping a negative start straight to 0 made substring("hello", -1, 3)
     * return "hel" natively where every other backend returns "". */
    if (start < 0) start += (long)str->length;
    if (end < 0) end += (long)str->length;
    if (start < 0) start = 0;
    if (start > (long)str->length) start = (long)str->length;
    if (end < start) end = start;
    if (end > (long)str->length) end = (long)str->length;
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

static inline UfVal uf_str_trim_start(UfVal s) {
    if (s.kind != UF_RT_STRING) return uf_str("");
    const char* str = s.as.string->chars;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    return uf_str(str);
}

static inline UfVal uf_str_trim_end(UfVal s) {
    if (s.kind != UF_RT_STRING) return uf_str("");
    if (s.as.string->length == 0) return s;
    const char* start = s.as.string->chars;
    const char* end = start + s.as.string->length - 1;
    while (end >= start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    if (end < start) return uf_str("");
    size_t len = (size_t)(end - start + 1);
    return uf_str_l(start, len);
}

static inline UfVal uf_str_pad_start(UfVal s, UfVal target_len_v, UfVal pad_char_v) {
    if (s.kind != UF_RT_STRING || target_len_v.kind != UF_RT_NUMBER) return s;
    long target = (long)target_len_v.as.number;
    if (target <= 0 || (size_t)target <= s.as.string->length) return s;
    const char* pad_chars = " ";
    size_t pad_len = 1;
    if (pad_char_v.kind == UF_RT_STRING && pad_char_v.as.string->length > 0) {
        pad_chars = pad_char_v.as.string->chars;
        pad_len = pad_char_v.as.string->length;
    }
    size_t total = (size_t)target;
    size_t str_len = s.as.string->length;
    size_t pad_needed = total - str_len;
    char* buf = (char*)malloc(total + 1);
    for (size_t i = 0; i < pad_needed; ++i) {
        buf[i] = pad_chars[i % pad_len];
    }
    memcpy(buf + pad_needed, s.as.string->chars, str_len);
    buf[total] = '\0';
    UfVal res = uf_str_l(buf, total);
    free(buf);
    return res;
}

static inline UfVal uf_str_pad_end(UfVal s, UfVal target_len_v, UfVal pad_char_v) {
    if (s.kind != UF_RT_STRING || target_len_v.kind != UF_RT_NUMBER) return s;
    long target = (long)target_len_v.as.number;
    if (target <= 0 || (size_t)target <= s.as.string->length) return s;
    const char* pad_chars = " ";
    size_t pad_len = 1;
    if (pad_char_v.kind == UF_RT_STRING && pad_char_v.as.string->length > 0) {
        pad_chars = pad_char_v.as.string->chars;
        pad_len = pad_char_v.as.string->length;
    }
    size_t total = (size_t)target;
    size_t str_len = s.as.string->length;
    size_t pad_needed = total - str_len;
    char* buf = (char*)malloc(total + 1);
    memcpy(buf, s.as.string->chars, str_len);
    for (size_t i = 0; i < pad_needed; ++i) {
        buf[str_len + i] = pad_chars[i % pad_len];
    }
    buf[total] = '\0';
    UfVal res = uf_str_l(buf, total);
    free(buf);
    return res;
}

static inline UfVal uf_str_chars(UfVal s) {
    if (s.kind != UF_RT_STRING) return uf_make_array(0);
    UfVal res = uf_array_new(s.as.string->length > 0 ? s.as.string->length : 1);
    for (size_t i = 0; i < s.as.string->length; ++i) {
        char ch[2] = { s.as.string->chars[i], '\0' };
        uf_array_push(res, uf_str(ch));
    }
    return res;
}

static inline UfVal uf_str_count(UfVal s, UfVal sub) {
    if (s.kind != UF_RT_STRING || sub.kind != UF_RT_STRING) return uf_num(0);
    if (sub.as.string->length == 0 || s.as.string->length < sub.as.string->length) return uf_num(0);
    size_t cnt = 0;
    const char* p = s.as.string->chars;
    const char* end = p + s.as.string->length;
    size_t sub_len = sub.as.string->length;
    while (p < end) {
        const char* m = strstr(p, sub.as.string->chars);
        if (!m) break;
        cnt++;
        p = m + sub_len;
    }
    return uf_num((double)cnt);
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
        UfRtMapEntry* e = uf_map_lookup(m, key.as.string->chars, key.as.string->length);
        if (e) uf_array_push(arr, e->value);
    }
    return arr;
}

static inline UfVal uf_map_has_key(UfVal target, UfVal key) {
    if (target.kind != UF_RT_MAP || key.kind != UF_RT_STRING) return uf_bool(false);
    UfRtMap* m = target.as.map;
    return uf_bool(uf_map_lookup(m, key.as.string->chars, key.as.string->length) != NULL);
}

static inline UfVal uf_map_delete(UfVal target, UfVal key) {
    if (target.kind != UF_RT_MAP || key.kind != UF_RT_STRING) return uf_bool(false);
    UfRtMap* m = target.as.map;
    UfRtMapEntry* e = uf_map_lookup(m, key.as.string->chars, key.as.string->length);
    if (!e) return uf_bool(false);

    e->occupied = false;
    e->tombstone = true; /* keep probe chains through this slot intact */
    m->count--;
    m->tombstones++;
    for (size_t k = 0; k < m->order_count; ++k) {
        if (uf_map_key_eq(m->order_keys[k], key.as.string->chars, key.as.string->length)) {
            for (size_t p = k; p + 1 < m->order_count; ++p) {
                m->order_keys[p] = m->order_keys[p + 1];
            }
            m->order_count--;
            break;
        }
    }
    return uf_bool(true);
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

static inline UfVal uf_array_concat(UfVal a1, UfVal a2) {
    if (a1.kind != UF_RT_ARRAY || a2.kind != UF_RT_ARRAY) {
        fprintf(stderr, "Runtime Error: 'concat()' expects two arrays\n");
        exit(3);
    }
    UfRtArray* arr1 = a1.as.array;
    UfRtArray* arr2 = a2.as.array;
    UfVal res = uf_array_new(arr1->count + arr2->count);
    for (size_t i = 0; i < arr1->count; ++i) uf_array_push(res, arr1->elements[i]);
    for (size_t i = 0; i < arr2->count; ++i) uf_array_push(res, arr2->elements[i]);
    return res;
}

static inline UfVal uf_array_flatten(UfVal a) {
    if (a.kind != UF_RT_ARRAY) {
        fprintf(stderr, "Runtime Error: 'flatten()' expects an array\n");
        exit(3);
    }
    UfRtArray* arr = a.as.array;
    UfVal res = uf_array_new(arr->count);
    for (size_t i = 0; i < arr->count; ++i) {
        if (arr->elements[i].kind == UF_RT_ARRAY) {
            UfRtArray* inner = arr->elements[i].as.array;
            for (size_t j = 0; j < inner->count; ++j) {
                uf_array_push(res, inner->elements[j]);
            }
        } else {
            uf_array_push(res, arr->elements[i]);
        }
    }
    return res;
}

static inline UfVal uf_array_fill(UfVal a, UfVal val) {
    if (a.kind != UF_RT_ARRAY) {
        fprintf(stderr, "Runtime Error: 'fill()' expects an array\n");
        exit(3);
    }
    UfRtArray* arr = a.as.array;
    for (size_t i = 0; i < arr->count; ++i) {
        arr->elements[i] = val;
    }
    return a;
}

static inline UfVal uf_array_zip(UfVal a1, UfVal a2) {
    if (a1.kind != UF_RT_ARRAY || a2.kind != UF_RT_ARRAY) {
        fprintf(stderr, "Runtime Error: 'zip()' expects two arrays\n");
        exit(3);
    }
    UfRtArray* arr1 = a1.as.array;
    UfRtArray* arr2 = a2.as.array;
    size_t n = arr1->count < arr2->count ? arr1->count : arr2->count;
    UfVal res = uf_array_new(n);
    for (size_t i = 0; i < n; ++i) {
        UfVal pair = uf_array_new(2);
        uf_array_push(pair, arr1->elements[i]);
        uf_array_push(pair, arr2->elements[i]);
        uf_array_push(res, pair);
    }
    return res;
}

static inline UfVal uf_sys_clock(void) {
#if defined(UF_EMBEDDED)
    static double g_embedded_tick = 0.0;
    g_embedded_tick += 0.001;
    return uf_num(g_embedded_tick);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uf_num((double)ts.tv_sec + (double)ts.tv_nsec * 1e-9);
#endif
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
static inline UfVal _wrap_str_trim_start(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_str_trim_start(a[0]) : uf_null(); }
static inline UfVal _wrap_str_trim_end(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_str_trim_end(a[0]) : uf_null(); }
static inline UfVal _wrap_str_pad_start(void* e, size_t n, UfVal* a) { (void)e; return n > 2 ? uf_str_pad_start(a[0], a[1], a[2]) : (n > 1 ? uf_str_pad_start(a[0], a[1], uf_null()) : uf_null()); }
static inline UfVal _wrap_str_pad_end(void* e, size_t n, UfVal* a) { (void)e; return n > 2 ? uf_str_pad_end(a[0], a[1], a[2]) : (n > 1 ? uf_str_pad_end(a[0], a[1], uf_null()) : uf_null()); }
static inline UfVal _wrap_str_chars(void* e, size_t n, UfVal* a) { (void)e; return n > 0 ? uf_str_chars(a[0]) : uf_null(); }
static inline UfVal _wrap_str_count(void* e, size_t n, UfVal* a) { (void)e; return n > 1 ? uf_str_count(a[0], a[1]) : uf_null(); }
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
    UfVal m = uf_map_new(32);
    uf_set(m, uf_str("split"), uf_closure_new(_wrap_str_split, NULL, 0));
    uf_set(m, uf_str("join"), uf_closure_new(_wrap_str_join, NULL, 0));
    uf_set(m, uf_str("trim"), uf_closure_new(_wrap_str_trim, NULL, 0));
    uf_set(m, uf_str("trim_start"), uf_closure_new(_wrap_str_trim_start, NULL, 0));
    uf_set(m, uf_str("trim_end"), uf_closure_new(_wrap_str_trim_end, NULL, 0));
    uf_set(m, uf_str("pad_start"), uf_closure_new(_wrap_str_pad_start, NULL, 0));
    uf_set(m, uf_str("pad_end"), uf_closure_new(_wrap_str_pad_end, NULL, 0));
    uf_set(m, uf_str("chars"), uf_closure_new(_wrap_str_chars, NULL, 0));
    uf_set(m, uf_str("count"), uf_closure_new(_wrap_str_count, NULL, 0));
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
    #elif defined(__wasm__) || defined(__wasi__)
    return uf_str("wasi");
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

static inline UfVal _wrap_sys_cwd(void* e, size_t n, UfVal* a) {
    (void)e; (void)n; (void)a;
#if defined(UF_EMBEDDED)
    return uf_null();
#else
    char buf[1024];
    if (getcwd(buf, sizeof(buf))) return uf_str(buf);
    return uf_null();
#endif
}

static inline UfVal _wrap_sys_set_env(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 2 || a[0].kind != UF_RT_STRING || a[1].kind != UF_RT_STRING) return uf_bool(false);
#if defined(_WIN32)
    return uf_bool(_putenv_s(a[0].as.string->chars, a[1].as.string->chars) == 0);
#elif defined(UF_EMBEDDED) || defined(__wasm__) || defined(__wasi__)
    return uf_bool(false);
#else
    return uf_bool(setenv(a[0].as.string->chars, a[1].as.string->chars, 1) == 0);
#endif
}

static inline UfVal _wrap_sys_exec(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_STRING) return uf_num(-1);
#if defined(UF_EMBEDDED) || defined(__wasm__) || defined(__wasi__)
    return uf_num(-1);
#else
    int res = system(a[0].as.string->chars);
    return uf_num((double)res);
#endif
}

static inline UfVal uf_mod_sys(void) {
    UfVal m = uf_map_new(16);
    uf_set(m, uf_str("platform"), uf_closure_new(_wrap_sys_platform, NULL, 0));
    uf_set(m, uf_str("args"), uf_closure_new(_wrap_sys_args, NULL, 0));
    uf_set(m, uf_str("env"), uf_closure_new(_wrap_sys_env, NULL, 0));
    uf_set(m, uf_str("exit"), uf_closure_new(_wrap_sys_exit, NULL, 0));
    uf_set(m, uf_str("cwd"), uf_closure_new(_wrap_sys_cwd, NULL, 0));
    uf_set(m, uf_str("set_env"), uf_closure_new(_wrap_sys_set_env, NULL, 0));
    uf_set(m, uf_str("exec"), uf_closure_new(_wrap_sys_exec, NULL, 0));
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

static inline UfVal _wrap_fs_append_text(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 2 || a[0].kind != UF_RT_STRING || a[1].kind != UF_RT_STRING) return uf_bool(false);
    FILE* f = fopen(a[0].as.string->chars, "ab");
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
#if defined(UF_EMBEDDED)
    FILE* f = fopen(a[0].as.string->chars, "rb");
    if (f) { fclose(f); return uf_bool(true); }
    return uf_bool(false);
#else
    return uf_bool(access(a[0].as.string->chars, F_OK) == 0);
#endif
}

static inline UfVal _wrap_fs_delete_file(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_STRING) return uf_bool(false);
    return uf_bool(remove(a[0].as.string->chars) == 0);
}

static inline UfVal _wrap_fs_list_dir(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_STRING) return uf_null();
#if defined(UF_EMBEDDED)
    return uf_null();
#else
    DIR* d = opendir(a[0].as.string->chars);
    if (!d) return uf_null();
    UfVal arr = uf_array_new(8);
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        uf_array_push(arr, uf_str(ent->d_name));
    }
    closedir(d);
    return arr;
#endif
}

static inline UfVal _wrap_fs_mkdir(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_STRING) return uf_bool(false);
#if defined(UF_EMBEDDED)
    return uf_bool(false);
#else
    return uf_bool(mkdir(a[0].as.string->chars, 0755) == 0);
#endif
}

static inline UfVal _wrap_fs_remove_dir(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_STRING) return uf_bool(false);
#if defined(UF_EMBEDDED)
    return uf_bool(false);
#else
    return uf_bool(rmdir(a[0].as.string->chars) == 0);
#endif
}

static inline UfVal _wrap_fs_is_file(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_STRING) return uf_bool(false);
#if defined(UF_EMBEDDED)
    FILE* f = fopen(a[0].as.string->chars, "rb");
    if (f) { fclose(f); return uf_bool(true); }
    return uf_bool(false);
#else
    struct stat st;
    if (stat(a[0].as.string->chars, &st) != 0) return uf_bool(false);
    return uf_bool(S_ISREG(st.st_mode));
#endif
}

static inline UfVal _wrap_fs_is_dir(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_STRING) return uf_bool(false);
#if defined(UF_EMBEDDED)
    return uf_bool(false);
#else
    struct stat st;
    if (stat(a[0].as.string->chars, &st) != 0) return uf_bool(false);
    return uf_bool(S_ISDIR(st.st_mode));
#endif
}

static inline UfVal _wrap_fs_file_size(void* e, size_t n, UfVal* a) {
    (void)e;
    if (n < 1 || a[0].kind != UF_RT_STRING) return uf_null();
#if defined(UF_EMBEDDED)
    FILE* f = fopen(a[0].as.string->chars, "rb");
    if (!f) return uf_null();
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return sz >= 0 ? uf_num((double)sz) : uf_null();
#else
    struct stat st;
    if (stat(a[0].as.string->chars, &st) != 0) return uf_null();
    return uf_num((double)st.st_size);
#endif
}

static inline UfVal uf_mod_fs(void) {
    UfVal m = uf_map_new(16);
    uf_set(m, uf_str("write_text"), uf_closure_new(_wrap_fs_write_text, NULL, 0));
    uf_set(m, uf_str("append_text"), uf_closure_new(_wrap_fs_append_text, NULL, 0));
    uf_set(m, uf_str("read_text"), uf_closure_new(_wrap_fs_read_text, NULL, 0));
    uf_set(m, uf_str("exists"), uf_closure_new(_wrap_fs_exists, NULL, 0));
    uf_set(m, uf_str("delete_file"), uf_closure_new(_wrap_fs_delete_file, NULL, 0));
    uf_set(m, uf_str("list_dir"), uf_closure_new(_wrap_fs_list_dir, NULL, 0));
    uf_set(m, uf_str("mkdir"), uf_closure_new(_wrap_fs_mkdir, NULL, 0));
    uf_set(m, uf_str("remove_dir"), uf_closure_new(_wrap_fs_remove_dir, NULL, 0));
    uf_set(m, uf_str("is_file"), uf_closure_new(_wrap_fs_is_file, NULL, 0));
    uf_set(m, uf_str("is_dir"), uf_closure_new(_wrap_fs_is_dir, NULL, 0));
    uf_set(m, uf_str("file_size"), uf_closure_new(_wrap_fs_file_size, NULL, 0));
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
        double s = a[0].as.number;
#if defined(UF_EMBEDDED)
        volatile uint32_t count = (uint32_t)(s * 1000000.0);
        while (count--) {
#if defined(__arm__)
            __asm__ volatile("nop");
#endif
        }
#else
        struct timespec req;
        req.tv_sec = (time_t)s;
        req.tv_nsec = (long)((s - (time_t)s) * 1e9);
        nanosleep(&req, NULL);
#endif
    }
    return uf_null();
}
static inline UfVal _wrap_time_timestamp(void* e, size_t n, UfVal* a) {
    (void)e; (void)n; (void)a;
#if defined(UF_EMBEDDED)
    return uf_sys_clock();
#else
    return uf_num((double)time(NULL));
#endif
}

static inline UfVal _wrap_time_format(void* e, size_t n, UfVal* a) {
    (void)e;
    time_t t = (n > 0 && a[0].kind == UF_RT_NUMBER) ? (time_t)a[0].as.number : time(NULL);
    const char* fmt = (n > 1 && a[1].kind == UF_RT_STRING) ? a[1].as.string->chars : "%Y-%m-%d %H:%M:%S";
    struct tm* tm_info = localtime(&t);
    if (!tm_info) return uf_str("");
    char buf[128];
    size_t len = strftime(buf, sizeof(buf), fmt, tm_info);
    return uf_str_l(buf, len);
}

static inline UfVal _wrap_time_iso(void* e, size_t n, UfVal* a) {
    (void)e;
    time_t t = (n > 0 && a[0].kind == UF_RT_NUMBER) ? (time_t)a[0].as.number : time(NULL);
    struct tm* tm_info = gmtime(&t);
    if (!tm_info) return uf_str("");
    char buf[64];
    size_t len = strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    return uf_str_l(buf, len);
}

static inline UfVal uf_mod_time(void) {
    UfVal m = uf_map_new(16);
    uf_set(m, uf_str("clock"), uf_closure_new(_wrap_time_clock, NULL, 0));
    uf_set(m, uf_str("sleep"), uf_closure_new(_wrap_time_sleep, NULL, 0));
    uf_set(m, uf_str("timestamp"), uf_closure_new(_wrap_time_timestamp, NULL, 0));
    uf_set(m, uf_str("format"), uf_closure_new(_wrap_time_format, NULL, 0));
    uf_set(m, uf_str("iso"), uf_closure_new(_wrap_time_iso, NULL, 0));
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
                case 'u': {
                    if (p->pos + 4 <= p->len) {
                        int d0 = uf_hex_char(p->src[p->pos]);
                        int d1 = uf_hex_char(p->src[p->pos + 1]);
                        int d2 = uf_hex_char(p->src[p->pos + 2]);
                        int d3 = uf_hex_char(p->src[p->pos + 3]);
                        if (d0 >= 0 && d1 >= 0 && d2 >= 0 && d3 >= 0) {
                            p->pos += 4;
                            uint32_t cp = (uint32_t)((d0 << 12) | (d1 << 8) | (d2 << 4) | d3);
                            if (cp >= 0xD800 && cp <= 0xDBFF && p->pos + 6 <= p->len &&
                                p->src[p->pos] == '\\' && p->src[p->pos + 1] == 'u') {
                                int s0 = uf_hex_char(p->src[p->pos + 2]);
                                int s1 = uf_hex_char(p->src[p->pos + 3]);
                                int s2 = uf_hex_char(p->src[p->pos + 4]);
                                int s3 = uf_hex_char(p->src[p->pos + 5]);
                                if (s0 >= 0 && s1 >= 0 && s2 >= 0 && s3 >= 0) {
                                    uint32_t low = (uint32_t)((s0 << 12) | (s1 << 8) | (s2 << 4) | s3);
                                    if (low >= 0xDC00 && low <= 0xDFFF) {
                                        p->pos += 6;
                                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                                    }
                                }
                            }
                            if (len + 4 >= cap) {
                                cap = (cap * 2) + 8;
                                buf = (char*)realloc(buf, cap);
                            }
                            if (cp <= 0x7F) {
                                buf[len++] = (char)cp;
                            } else if (cp <= 0x7FF) {
                                buf[len++] = (char)(0xC0 | ((cp >> 6) & 0x1F));
                                buf[len++] = (char)(0x80 | (cp & 0x3F));
                            } else if (cp <= 0xFFFF) {
                                buf[len++] = (char)(0xE0 | ((cp >> 12) & 0x0F));
                                buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                buf[len++] = (char)(0x80 | (cp & 0x3F));
                            } else {
                                buf[len++] = (char)(0xF0 | ((cp >> 18) & 0x07));
                                buf[len++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                                buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                buf[len++] = (char)(0x80 | (cp & 0x3F));
                            }
                            continue;
                        }
                    }
                    c = esc;
                    break;
                }
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

static void _json_stringify_string(UfJsonSb* sb, const char* s, size_t slen) {
    _json_sb_append(sb, "\"", 1);
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
            /* fabs(...) < 1e15 must be checked before the int64_t cast below:
             * converting a double outside int64_t's range is undefined
             * behavior in C, so the magnitude has to be known-safe first. */
            if (!isnan(val.as.number) && !isinf(val.as.number) &&
                fabs(val.as.number) < 1e15 && val.as.number == floor(val.as.number)) {
                snprintf(num_buf, sizeof(num_buf), "%lld", (long long)val.as.number);
            } else {
                snprintf(num_buf, sizeof(num_buf), "%.14g", val.as.number);
            }
            _json_sb_append(sb, num_buf, strlen(num_buf));
            return true;
        }
        case UF_RT_STRING: {
            _json_stringify_string(sb, val.as.string->chars, val.as.string->length);
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
                const char* ks = (k.kind == UF_RT_STRING) ? k.as.string->chars : "";
                size_t klen = (k.kind == UF_RT_STRING) ? k.as.string->length : 0;
                _json_stringify_string(sb, ks, klen);
                _json_sb_append(sb, ": ", 2);
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
    {
        UfRtMapEntry* e = uf_map_lookup(m, sym, strlen(sym));
        if (e) return e->value;
    }
    char err[128];
    snprintf(err, sizeof(err), "Cannot import name '%s' from module '%s'", sym, mod_name);
    uf_raise(err, "ImportError");
    return uf_null();
}

#endif /* UNFISH_RUNTIME_H */
