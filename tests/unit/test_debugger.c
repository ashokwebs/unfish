#define _POSIX_C_SOURCE 200809L
#include "uf_common.h"
#include "uf_arena.h"
#include "uf_string.h"
#include "uf_diagnostic.h"
#include "uf_token.h"
#include "uf_lexer.h"
#include "uf_ast.h"
#include "uf_parser.h"
#include "uf_semantic.h"
#include "uf_runtime.h"
#include "uf_interpreter.h"
#include "uf_debugger.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_trace_events(void) {
    const char* src =
        "let x = 10\n"
        "function add(y):\n"
        "    return x + y\n"
        "let z = add(5)\n";

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "trace_test.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "trace_test.unfish", src, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    assert(uf_analyze_program(&sema, prog));

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    char* trace_buf = NULL;
    size_t trace_len = 0;
    FILE* mem = open_memstream(&trace_buf, &trace_len);

    uf_debugger_attach_tracer(&rt, mem);
    UfInterpretResult res = uf_interpret_program(&rt, prog);
    uf_debugger_detach(&rt);
    fclose(mem);

    assert(res == UF_INTERPRET_OK);
    assert(trace_buf != NULL);
    assert(strstr(trace_buf, "\"event\":\"step\"") != NULL);
    assert(strstr(trace_buf, "\"event\":\"var_bind\"") != NULL);
    assert(strstr(trace_buf, "\"event\":\"call_enter\"") != NULL);
    assert(strstr(trace_buf, "\"event\":\"call_exit\"") != NULL);

    free(trace_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_trace_events passed!\n");
}

static void test_interactive_scripted(void) {
    const char* src =
        "let a = 1\n"
        "let b = 2\n"
        "let c = a + b\n";

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "debug_test.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "debug_test.unfish", src, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    assert(uf_analyze_program(&sema, prog));

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    const char* cmds = "step\nprint a\nnext\ncontinue\n";
    FILE* in = fmemopen((void*)cmds, strlen(cmds), "r");

    char* out_buf = NULL;
    size_t out_len = 0;
    FILE* out = open_memstream(&out_buf, &out_len);

    uf_debugger_attach_interactive(&rt, in, out);
    UfInterpretResult res = uf_interpret_program(&rt, prog);
    uf_debugger_detach(&rt);

    fclose(in);
    fclose(out);

    assert(res == UF_INTERPRET_OK);
    assert(out_buf != NULL);
    assert(strstr(out_buf, "(ufdb)") != NULL);
    assert(strstr(out_buf, "a = 1") != NULL);

    free(out_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_interactive_scripted passed!\n");
}

int main(void) {
    printf("Running debugger unit tests...\n");
    test_trace_events();
    test_interactive_scripted();
    printf("All debugger unit tests passed successfully!\n");
    return 0;
}
