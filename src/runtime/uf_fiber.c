#include "uf_fiber.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

UfValue uf_val_fiber(UfRuntime* rt, UfFiber* fiber) {
    (void)rt;
    UfValue v;
    v.kind = UF_VAL_FIBER;
    v.as.fiber = fiber;
    return v;
}

UfValue uf_val_channel(UfRuntime* rt, UfChannel* channel) {
    (void)rt;
    UfValue v;
    v.kind = UF_VAL_CHANNEL;
    v.as.channel = channel;
    return v;
}

void uf_scheduler_init(UfRuntime* rt) {
    if (!rt) return;
    UfScheduler* s = (UfScheduler*)malloc(sizeof(UfScheduler));
    if (!s) return;
    s->run_head = NULL;
    s->run_tail = NULL;
    s->current = NULL;
    s->fiber_count = 0;
    s->next_id = 1;
    rt->scheduler = s;
}

void uf_scheduler_free(UfRuntime* rt) {
    if (!rt || !rt->scheduler) return;
    UfScheduler* s = (UfScheduler*)rt->scheduler;
    free(s);
    rt->scheduler = NULL;
}

UfFiber* uf_fiber_create(UfRuntime* rt, UfValue callable, size_t argc, UfValue* args) {
    UfFiber* fib = (UfFiber*)malloc(sizeof(UfFiber));
    if (!fib) return NULL;
    fib->obj.kind = UF_OBJ_FIBER;
    fib->obj.marked = false;
    fib->obj.next = NULL;

    UfScheduler* s = (UfScheduler*)rt->scheduler;
    fib->id = s ? s->next_id++ : 1;
    fib->state = UF_FIBER_NEW;
    fib->callable = callable;
    fib->argc = argc;
    fib->result = uf_val_null();
    fib->next = NULL;
    fib->prev = NULL;

    if (argc > 0 && args) {
        fib->args = (UfValue*)malloc(sizeof(UfValue) * argc);
        for (size_t i = 0; i < argc; ++i) {
            fib->args[i] = args[i];
        }
    } else {
        fib->args = NULL;
    }

    uf_runtime_register_obj(rt, (UfObj*)fib, sizeof(UfFiber) + (argc * sizeof(UfValue)));
    return fib;
}

void uf_fiber_free(UfFiber* fib) {
    if (!fib) return;
    if (fib->args) {
        free(fib->args);
        fib->args = NULL;
    }
    free(fib);
}

void uf_scheduler_spawn(UfRuntime* rt, UfFiber* fiber) {
    if (!rt || !fiber || !rt->scheduler) return;
    UfScheduler* s = (UfScheduler*)rt->scheduler;
    fiber->state = UF_FIBER_RUNNABLE;
    fiber->next = NULL;
    fiber->prev = s->run_tail;

    if (s->run_tail) {
        s->run_tail->next = fiber;
        s->run_tail = fiber;
    } else {
        s->run_head = fiber;
        s->run_tail = fiber;
    }
    s->fiber_count++;
}

UfValue uf_scheduler_yield(UfRuntime* rt, UfValue val) {
    if (!rt || !rt->scheduler) return val;
    UfScheduler* s = (UfScheduler*)rt->scheduler;
    if (!s->current) return val;

    s->current->result = val;
    return val;
}

int uf_scheduler_run(UfRuntime* rt) {
    if (!rt || !rt->scheduler) return 0;
    UfScheduler* s = (UfScheduler*)rt->scheduler;
    int completed = 0;

    SourceSpan dummy_span = source_span_make(source_loc_make("<fiber>", 0, 0, 0), source_loc_make("<fiber>", 0, 0, 0));

    while (s->run_head) {
        UfFiber* fib = s->run_head;
        s->run_head = fib->next;
        if (s->run_head) {
            s->run_head->prev = NULL;
        } else {
            s->run_tail = NULL;
        }

        fib->next = NULL;
        fib->prev = NULL;
        fib->state = UF_FIBER_RUNNING;
        s->current = fib;

        /* Execute fiber */
        UfValue res = uf_runtime_call(rt, fib->callable, fib->argc, fib->args, dummy_span);
        fib->result = res;
        fib->state = UF_FIBER_DEAD;
        completed++;
        if (s->fiber_count > 0) s->fiber_count--;
        s->current = NULL;
    }

    return completed;
}

UfChannel* uf_channel_create(UfRuntime* rt, size_t capacity) {
    UfChannel* ch = (UfChannel*)malloc(sizeof(UfChannel));
    if (!ch) return NULL;
    ch->obj.kind = UF_OBJ_CHANNEL;
    ch->obj.marked = false;
    ch->obj.next = NULL;

    ch->capacity = capacity;
    ch->count = 0;
    ch->head = 0;
    ch->tail = 0;
    ch->closed = false;
    ch->wait_send_head = NULL;
    ch->wait_send_tail = NULL;
    ch->wait_recv_head = NULL;
    ch->wait_recv_tail = NULL;

    if (capacity > 0) {
        ch->buffer = (UfValue*)malloc(sizeof(UfValue) * capacity);
    } else {
        ch->buffer = NULL;
    }

    uf_runtime_register_obj(rt, (UfObj*)ch, sizeof(UfChannel) + (capacity * sizeof(UfValue)));
    return ch;
}

void uf_channel_free(UfChannel* ch) {
    if (!ch) return;
    if (ch->buffer) {
        free(ch->buffer);
        ch->buffer = NULL;
    }
    free(ch);
}

bool uf_channel_send(UfRuntime* rt, UfChannel* ch, UfValue val) {
    if (!ch) return false;
    if (ch->closed) {
        SourceSpan span = source_span_make(source_loc_make("<channel>", 0, 0, 0), source_loc_make("<channel>", 0, 0, 0));
        uf_runtime_error(rt, span, "Cannot send on closed channel");
        return false;
    }

    if (ch->capacity > 0 && ch->count < ch->capacity) {
        ch->buffer[ch->tail] = val;
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        return true;
    }

    /* Unbuffered / direct delivery if receiver is waiting */
    if (ch->wait_recv_head) {
        UfFiber* receiver = ch->wait_recv_head;
        ch->wait_recv_head = receiver->next;
        if (!ch->wait_recv_head) ch->wait_recv_tail = NULL;
        receiver->result = val;
        uf_scheduler_spawn(rt, receiver);
        return true;
    }

    /* Buffer is full: place in circular buffer expanding if needed for educational fiber runtime */
    size_t new_cap = ch->capacity == 0 ? 4 : ch->capacity * 2;
    UfValue* new_buf = (UfValue*)malloc(sizeof(UfValue) * new_cap);
    for (size_t i = 0; i < ch->count; ++i) {
        new_buf[i] = ch->buffer[(ch->head + i) % ch->capacity];
    }
    new_buf[ch->count] = val;
    free(ch->buffer);
    ch->buffer = new_buf;
    ch->head = 0;
    ch->count++;
    ch->tail = ch->count % new_cap;
    ch->capacity = new_cap;
    return true;
}

bool uf_channel_recv(UfRuntime* rt, UfChannel* ch, UfValue* out_val) {
    (void)rt;
    if (!ch || !out_val) return false;

    if (ch->count > 0) {
        *out_val = ch->buffer[ch->head];
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
        return true;
    }

    if (ch->closed) {
        *out_val = uf_val_null();
        return false;
    }

    *out_val = uf_val_null();
    return false;
}

void uf_channel_close(UfRuntime* rt, UfChannel* ch) {
    (void)rt;
    if (!ch) return;
    ch->closed = true;
}
