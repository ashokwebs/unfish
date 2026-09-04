#define _POSIX_C_SOURCE 200809L
#include "../../src/runtime/uf_fiber.h"
#include "../../src/runtime/uf_runtime.h"
#include "../../src/runtime/uf_env.h"
#include "../../src/runtime/uf_stdlib.h"
#include "../../src/interpreter/uf_interpreter.h"
#include "../../src/parser/uf_parser.h"
#include "../../src/lexer/uf_lexer.h"
#include "../../src/semantic/uf_semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static UfValue native_fib_task(UfRuntime* rt, int argc, UfValue* args) {
    (void)rt;
    double sum = 0;
    for (int i = 0; i < argc; ++i) {
        if (args[i].kind == UF_VAL_NUMBER) {
            sum += args[i].as.number;
        }
    }
    return uf_val_number(sum);
}

static void test_fiber_creation_and_run(void) {
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfValue task_fn = uf_val_native("task", native_fib_task, 2);
    UfValue args[2] = { uf_val_number(15), uf_val_number(27) };
    UfFiber* fib = uf_fiber_create(&rt, task_fn, 2, args);
    assert(fib != NULL);
    assert(fib->state == UF_FIBER_NEW);
    assert(fib->id == 1);

    uf_scheduler_spawn(&rt, fib);
    assert(fib->state == UF_FIBER_RUNNABLE);

    int completed = uf_scheduler_run(&rt);
    assert(completed == 1);
    assert(fib->state == UF_FIBER_DEAD);
    assert(fib->result.kind == UF_VAL_NUMBER);
    assert(fib->result.as.number == 42.0);

    uf_runtime_free(&rt);
    printf("test_fiber_creation_and_run passed!\n");
}

static void test_channel_buffered(void) {
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfChannel* ch = uf_channel_create(&rt, 3);
    assert(ch != NULL);
    assert(ch->capacity == 3);
    assert(ch->count == 0);

    bool ok1 = uf_channel_send(&rt, ch, uf_val_number(100));
    bool ok2 = uf_channel_send(&rt, ch, uf_val_number(200));
    bool ok3 = uf_channel_send(&rt, ch, uf_val_number(300));
    assert(ok1 && ok2 && ok3);
    assert(ch->count == 3);

    UfValue v;
    bool r1 = uf_channel_recv(&rt, ch, &v);
    assert(r1 && v.kind == UF_VAL_NUMBER && v.as.number == 100.0);

    bool r2 = uf_channel_recv(&rt, ch, &v);
    assert(r2 && v.kind == UF_VAL_NUMBER && v.as.number == 200.0);

    bool r3 = uf_channel_recv(&rt, ch, &v);
    assert(r3 && v.kind == UF_VAL_NUMBER && v.as.number == 300.0);

    assert(ch->count == 0);

    /* Closing channel */
    uf_channel_close(&rt, ch);
    assert(ch->closed);

    bool r4 = uf_channel_recv(&rt, ch, &v);
    assert(!r4 && v.kind == UF_VAL_NULL);

    uf_runtime_free(&rt);
    printf("test_channel_buffered passed!\n");
}

static void test_channel_grow_and_overflow(void) {
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfChannel* ch = uf_channel_create(&rt, 2);
    for (int i = 0; i < 10; ++i) {
        bool ok = uf_channel_send(&rt, ch, uf_val_number((double)i));
        assert(ok);
    }
    assert(ch->count == 10);

    for (int i = 0; i < 10; ++i) {
        UfValue v;
        bool ok = uf_channel_recv(&rt, ch, &v);
        assert(ok);
        assert(v.kind == UF_VAL_NUMBER);
        assert(v.as.number == (double)i);
    }
    assert(ch->count == 0);

    uf_runtime_free(&rt);
    printf("test_channel_grow_and_overflow passed!\n");
}

static void test_concurrency_script_execution(void) {
    const char* source =
        "let ch = channel(4)\n"
        "send(ch, 11)\n"
        "send(ch, 22)\n"
        "let a = recv(ch)\n"
        "let b = recv(ch)\n"
        "let total = a + b\n";

    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", source);
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfLexer lexer;
    uf_lexer_init(&lexer, "<test>", source, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog != NULL);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool ok = uf_analyze_program(&sema, prog);
    assert(ok);

    UfInterpretResult res = uf_interpret_program(&rt, prog);
    assert(res == UF_INTERPRET_OK);
    assert(!rt.had_runtime_error);

    UfValue total_val;
    bool found = uf_env_lookup(rt.global_env, "total", &total_val);
    assert(found);
    assert(total_val.kind == UF_VAL_NUMBER);
    assert(total_val.as.number == 33.0);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_concurrency_script_execution passed!\n");
}

static void test_fiber_spawn_and_run_script(void) {
    const char* source =
        "let ch = channel(4)\n"
        "function worker():\n"
        "    send(ch, 100)\n"
        "    send(ch, 200)\n"
        "\n"
        "let fib = spawn(worker)\n"
        "let done = run_scheduler()\n"
        "let v1 = recv(ch)\n"
        "let v2 = recv(ch)\n"
        "let total = v1 + v2\n";

    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", source);
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfLexer lexer;
    uf_lexer_init(&lexer, "<test>", source, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog != NULL);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool ok = uf_analyze_program(&sema, prog);
    assert(ok);

    UfInterpretResult res = uf_interpret_program(&rt, prog);
    assert(res == UF_INTERPRET_OK);
    assert(!rt.had_runtime_error);

    UfValue done_val;
    bool f1 = uf_env_lookup(rt.global_env, "done", &done_val);
    assert(f1);
    assert(done_val.kind == UF_VAL_NUMBER);
    assert(done_val.as.number == 1.0);

    UfValue total_val;
    bool f2 = uf_env_lookup(rt.global_env, "total", &total_val);
    assert(f2);
    assert(total_val.kind == UF_VAL_NUMBER);
    assert(total_val.as.number == 300.0);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_fiber_spawn_and_run_script passed!\n");
}

static void test_fiber_gc_marking(void) {
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfChannel* ch = uf_channel_create(&rt, 2);
    uf_runtime_push_temp_root(&rt, uf_val_channel(&rt, ch));

    uf_channel_send(&rt, ch, uf_val_string_cstr(&rt, "gc_test_1"));
    uf_channel_send(&rt, ch, uf_val_string_cstr(&rt, "gc_test_2"));

    /* Trigger GC collection */
    uf_gc_collect(&rt);

    UfValue v;
    bool ok1 = uf_channel_recv(&rt, ch, &v);
    assert(ok1);
    assert(v.kind == UF_VAL_STRING);
    assert(strcmp(v.as.string->chars, "gc_test_1") == 0);

    bool ok2 = uf_channel_recv(&rt, ch, &v);
    assert(ok2);
    assert(v.kind == UF_VAL_STRING);
    assert(strcmp(v.as.string->chars, "gc_test_2") == 0);

    uf_runtime_pop_temp_roots(&rt, 1);
    uf_runtime_free(&rt);
    printf("test_fiber_gc_marking passed!\n");
}

int main(void) {
    printf("=== Running Fiber & Concurrency Unit Tests ===\n");
    test_fiber_creation_and_run();
    test_channel_buffered();
    test_channel_grow_and_overflow();
    test_concurrency_script_execution();
    test_fiber_spawn_and_run_script();
    test_fiber_gc_marking();
    printf("All fiber and concurrency unit tests passed!\n");
    return 0;
}
