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

static void test_vertical_slice_end_to_end(void) {
    const char* src =
        "let name = \"World\"\n"
        "\n"
        "function greet(person):\n"
        "    say \"Hello, \" + person\n"
        "\n"
        "greet(name)\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "vertical_slice.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "vertical_slice.unfish", src, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool sema_ok = uf_analyze_program(&sema, program);
    assert(sema_ok);

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
    assert(strcmp(out_buf, "Hello, World\n") == 0);

    free(out_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_vertical_slice_end_to_end passed!\n");
}

static void test_recursion_fibonacci(void) {
    const char* src =
        "function fib(n):\n"
        "    if n <= 1:\n"
        "        return n\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "\n"
        "say fib(10)\n";

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "fib.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "fib.unfish", src, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool sema_ok = uf_analyze_program(&sema, program);
    assert(sema_ok);

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
    assert(strcmp(out_buf, "55\n") == 0);

    free(out_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_recursion_fibonacci passed!\n");
}

static void test_lexical_closure(void) {
    const char* src =
        "function make_multiplier(factor):\n"
        "    function multiply(x):\n"
        "        return x * factor\n"
        "    return multiply\n"
        "\n"
        "let triple = make_multiplier(3)\n"
        "say triple(7)\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "closure.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "closure.unfish", src, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool sema_ok = uf_analyze_program(&sema, program);
    assert(sema_ok);

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
    assert(strcmp(out_buf, "21\n") == 0);

    free(out_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_lexical_closure passed!\n");
}

static void test_repeat_loop(void) {
    const char* src =
        "let total = 0\n"
        "repeat 5 times:\n"
        "    total = total + 10\n"
        "say total\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "repeat.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "repeat.unfish", src, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool sema_ok = uf_analyze_program(&sema, program);
    assert(sema_ok);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    char* out_buf = NULL;
    size_t out_len = 0;
    FILE* mem = open_memstream(&out_buf, &out_len);
    rt.out_stream = mem;

    UfInterpretResult res = uf_interpret_program(&rt, program);
    fclose(mem);

    assert(res == UF_INTERPRET_OK);
    assert(strcmp(out_buf, "50\n") == 0);

    free(out_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_repeat_loop passed!\n");
}

static void test_runtime_division_by_zero(void) {
    const char* src =
        "let x = 10 / 0\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "divzero.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "divzero.unfish", src, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    uf_analyze_program(&sema, program);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfInterpretResult res = uf_interpret_program(&rt, program);
    assert(res == UF_INTERPRET_RUNTIME_ERROR);
    assert(rt.had_runtime_error);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_runtime_division_by_zero passed!\n");
}

int main(void) {
    printf("Running interpreter tests...\n");
    test_vertical_slice_end_to_end();
    test_recursion_fibonacci();
    test_lexical_closure();
    test_repeat_loop();
    test_runtime_division_by_zero();
    printf("All interpreter tests passed successfully!\n");
    return 0;
}
