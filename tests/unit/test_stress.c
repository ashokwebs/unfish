#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../../src/common/uf_arena.h"
#include "../../src/common/uf_string.h"
#include "../../src/common/uf_diagnostic.h"
#include "../../src/lexer/uf_lexer.h"
#include "../../src/ast/uf_ast.h"
#include "../../src/parser/uf_parser.h"
#include "../../src/semantic/uf_semantic.h"
#include "../../src/runtime/uf_runtime.h"
#include "../../src/interpreter/uf_interpreter.h"

static void test_deeply_nested_closures(void) {
    const char* src =
        "function f1(a):\n"
        "    function f2(b):\n"
        "        function f3(c):\n"
        "            function f4(d):\n"
        "                return a * 1000 + b * 100 + c * 10 + d\n"
        "            return f4\n"
        "        return f3\n"
        "    return f2\n"
        "\n"
        "let step1 = f1(5)\n"
        "let step2 = step1(4)\n"
        "let step3 = step2(3)\n"
        "say step3(2)\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "closures.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "closures.unfish", src, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    assert(uf_analyze_program(&sema, program));

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    char* out_buf = NULL;
    size_t out_len = 0;
    FILE* mem = open_memstream(&out_buf, &out_len);
    rt.out_stream = mem;

    UfInterpretResult res = uf_interpret_program(&rt, program);
    fclose(mem);

    assert(res == UF_INTERPRET_OK);
    assert(!rt.had_runtime_error);
    assert(strcmp(out_buf, "5432\n") == 0);

    free(out_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_deeply_nested_closures passed!\n");
}

static void test_mutually_recursive_functions(void) {
    const char* src =
        "function is_even(n):\n"
        "    if n == 0:\n"
        "        return true\n"
        "    return is_odd(n - 1)\n"
        "\n"
        "function is_odd(n):\n"
        "    if n == 0:\n"
        "        return false\n"
        "    return is_even(n - 1)\n"
        "\n"
        "say is_even(20)\n"
        "say is_odd(20)\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "mutual_rec.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "mutual_rec.unfish", src, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    assert(uf_analyze_program(&sema, program));

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    char* out_buf = NULL;
    size_t out_len = 0;
    FILE* mem = open_memstream(&out_buf, &out_len);
    rt.out_stream = mem;

    UfInterpretResult res = uf_interpret_program(&rt, program);
    fclose(mem);

    assert(res == UF_INTERPRET_OK);
    assert(!rt.had_runtime_error);
    assert(strcmp(out_buf, "true\nfalse\n") == 0);

    free(out_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_mutually_recursive_functions passed!\n");
}

static void test_variable_shadowing(void) {
    const char* src =
        "let x = 10\n"
        "function test_shadow():\n"
        "    let x = 20\n"
        "    if true:\n"
        "        let x = 30\n"
        "        say x\n"
        "    say x\n"
        "\n"
        "test_shadow()\n"
        "say x\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "shadow.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "shadow.unfish", src, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    assert(uf_analyze_program(&sema, program));

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    char* out_buf = NULL;
    size_t out_len = 0;
    FILE* mem = open_memstream(&out_buf, &out_len);
    rt.out_stream = mem;

    UfInterpretResult res = uf_interpret_program(&rt, program);
    fclose(mem);

    assert(res == UF_INTERPRET_OK);
    assert(strcmp(out_buf, "30\n20\n10\n") == 0);

    free(out_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_variable_shadowing passed!\n");
}

static void test_gc_stress_collection_under_pressure(void) {
    const char* src =
        "let i = 0\n"
        "let result = \"start\"\n"
        "while i < 100:\n"
        "    result = \"step:\" + i + \"_\" + result\n"
        "    i = i + 1\n"
        "say len(result) > 100\n";

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "gc_stress.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "gc_stress.unfish", src, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    assert(uf_analyze_program(&sema, program));

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);
    /* Force aggressive GC threshold: collect every 512 bytes! */
    rt.next_gc_threshold = 512;

    char* out_buf = NULL;
    size_t out_len = 0;
    FILE* mem = open_memstream(&out_buf, &out_len);
    rt.out_stream = mem;

    UfInterpretResult res = uf_interpret_program(&rt, program);
    fclose(mem);

    assert(res == UF_INTERPRET_OK);
    assert(!rt.had_runtime_error);
    assert(strcmp(out_buf, "true\n") == 0);
    assert(rt.gc_count >= 5); /* Verify GC triggered repeatedly during execution */

    free(out_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_gc_stress_collection_under_pressure passed! (GC ran %zu times)\n", rt.gc_count);
}

static void test_deeply_nested_expressions(void) {
    /* Build a string of 60 nested parentheses: (1 + (1 + (1 + ... + (1 + 0)...))) */
    char buf[1024];
    buf[0] = '\0';
    strcat(buf, "let total = ");
    for (int i = 0; i < 60; i++) {
        strcat(buf, "(1 + ");
    }
    strcat(buf, "0");
    for (int i = 0; i < 60; i++) {
        strcat(buf, ")");
    }
    strcat(buf, "\nsay total\n");

    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "nested_expr.unfish", buf);

    UfLexer lexer;
    uf_lexer_init(&lexer, "nested_expr.unfish", buf, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    assert(uf_analyze_program(&sema, program));

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    char* out_buf = NULL;
    size_t out_len = 0;
    FILE* mem = open_memstream(&out_buf, &out_len);
    rt.out_stream = mem;

    UfInterpretResult res = uf_interpret_program(&rt, program);
    fclose(mem);

    assert(res == UF_INTERPRET_OK);
    assert(!rt.had_runtime_error);
    assert(strcmp(out_buf, "60\n") == 0);

    free(out_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_deeply_nested_expressions passed!\n");
}

static void test_recursion_stack_overflow_limit(void) {
    const char* src =
        "function recurse():\n"
        "    return recurse()\n"
        "\n"
        "recurse()\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "overflow.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "overflow.unfish", src, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    assert(uf_analyze_program(&sema, program));

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    /* Direct error output away from stdout */
    char* err_buf = NULL;
    size_t err_len = 0;
    FILE* err_mem = open_memstream(&err_buf, &err_len);
    rt.err_stream = err_mem;

    UfInterpretResult res = uf_interpret_program(&rt, program);
    fclose(err_mem);

    assert(res == UF_INTERPRET_RUNTIME_ERROR);
    assert(rt.had_runtime_error);

    free(err_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_recursion_stack_overflow_limit passed!\n");
}

static void test_step_limit_quota(void) {
    const char* src =
        "let c = 0\n"
        "while true:\n"
        "    c = c + 1\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "quota.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "quota.unfish", src, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    assert(uf_analyze_program(&sema, program));

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);
    rt.max_steps = 1000; /* Limit to 1000 steps */

    char* err_buf = NULL;
    size_t err_len = 0;
    FILE* err_mem = open_memstream(&err_buf, &err_len);
    rt.err_stream = err_mem;

    UfInterpretResult res = uf_interpret_program(&rt, program);
    fclose(err_mem);

    assert(res == UF_INTERPRET_RUNTIME_ERROR);
    assert(rt.had_runtime_error);
    assert(rt.step_count >= 1000);

    free(err_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_step_limit_quota passed!\n");
}

static void test_malformed_fuzz_inputs(void) {
    const char* bad_inputs[] = {
        "",
        "   \t  \r\n   ",
        "######## comment only",
        "let = 5",
        "say ((((1 + 2)",
        "function ():",
        "if:",
        "if true:\n",
        "repeat -5 times:\n    say 1\n",
        "let x = \"unterminated string",
        "let y = \"bad escape \\q\"",
        "12345678901234567890123456789012345678901234567890",
        "@#$%^&*~`"
    };

    size_t count = sizeof(bad_inputs) / sizeof(bad_inputs[0]);
    for (size_t i = 0; i < count; i++) {
        UfArena arena;
        uf_arena_init(&arena, 2048);
        UfInterner interner;
        uf_interner_init(&interner, &arena);
        UfDiagnosticReporter reporter;
        uf_diag_reporter_init(&reporter, "<fuzz>", bad_inputs[i]);

        UfLexer lexer;
        uf_lexer_init(&lexer, "<fuzz>", bad_inputs[i], &arena, &interner, &reporter);
        UfParser parser;
        uf_parser_init(&parser, &lexer, &arena, &reporter);

        /* Parsing bad input must cleanly fail without crash or memory fault */
        UfProgram* program = uf_parse_program(&parser);
        if (program && !parser.had_error) {
            UfSemanticAnalyzer sema;
            uf_semantic_init(&sema, &arena, &reporter);
            uf_analyze_program(&sema, program);
        }

        uf_interner_free(&interner);
        uf_arena_free(&arena);
    }

    printf("test_malformed_fuzz_inputs passed! (All %zu malformed inputs cleanly handled)\n", count);
}

int main(void) {
    printf("Running comprehensive stress tests...\n");
    test_deeply_nested_closures();
    test_mutually_recursive_functions();
    test_variable_shadowing();
    test_gc_stress_collection_under_pressure();
    test_deeply_nested_expressions();
    test_recursion_stack_overflow_limit();
    test_step_limit_quota();
    test_malformed_fuzz_inputs();
    printf("All stress tests passed successfully!\n");
    return 0;
}
