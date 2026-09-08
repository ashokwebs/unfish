#ifndef UF_FIBER_H
#define UF_FIBER_H

#include "uf_value.h"
#include "uf_runtime.h"

typedef enum {
    UF_FIBER_NEW,
    UF_FIBER_RUNNABLE,
    UF_FIBER_RUNNING,
    UF_FIBER_WAITING,
    UF_FIBER_DEAD
} UfFiberState;

struct UfFiber {
    UfObj obj;
    uint64_t id;
    UfFiberState state;
    UfValue callable;
    UfValue* args;
    size_t argc;
    UfValue result;
    struct UfFiber* next;
    struct UfFiber* prev;
};

struct UfChannel {
    UfObj obj;
    UfValue* buffer;
    size_t count;
    size_t capacity;
    size_t head;
    size_t tail;
    bool closed;
    UfFiber* wait_send_head;
    UfFiber* wait_send_tail;
    UfFiber* wait_recv_head;
    UfFiber* wait_recv_tail;
};

typedef struct UfScheduler {
    UfFiber* run_head;
    UfFiber* run_tail;
    UfFiber* current;
    size_t fiber_count;
    uint64_t next_id;
} UfScheduler;

void uf_scheduler_init(UfRuntime* rt);
void uf_scheduler_free(UfRuntime* rt);

UfFiber* uf_fiber_create(UfRuntime* rt, UfValue callable, size_t argc, UfValue* args);
void uf_fiber_free(UfFiber* fib);
void uf_scheduler_spawn(UfRuntime* rt, UfFiber* fiber);
UfValue uf_scheduler_yield(UfRuntime* rt, UfValue val);
int uf_scheduler_run(UfRuntime* rt);

UfChannel* uf_channel_create(UfRuntime* rt, size_t capacity);
void uf_channel_free(UfChannel* ch);
bool uf_channel_send(UfRuntime* rt, UfChannel* ch, UfValue val);
bool uf_channel_recv(UfRuntime* rt, UfChannel* ch, UfValue* out_val);
void uf_channel_close(UfRuntime* rt, UfChannel* ch);

UfPromiseObject* uf_promise_create(UfRuntime* rt);
void uf_promise_free(UfPromiseObject* p);
void uf_promise_resolve(UfRuntime* rt, UfPromiseObject* p, UfValue val);
void uf_promise_reject(UfRuntime* rt, UfPromiseObject* p, UfValue err);
UfValue uf_promise_await(UfRuntime* rt, UfPromiseObject* p);

#endif /* UF_FIBER_H */
