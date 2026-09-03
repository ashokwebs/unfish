#define _POSIX_C_SOURCE 200809L
#include "uf_common.h"
#include "uf_arena.h"
#include "uf_string.h"
#include "uf_diagnostic.h"
#include "uf_token.h"
#include "uf_lexer.h"
#include "uf_ast.h"
#include "uf_parser.h"
#include "uf_runtime.h"
#include "uf_compiler.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static UfProgram* parse_source(const char* src, UfArena* arena, UfInterner* interner, UfDiagnosticReporter* reporter) {
    UfLexer lexer;
    uf_lexer_init(&lexer, "test.unfish", src, arena, interner, reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, arena, reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog && !parser.had_error);
    return prog;
}

static void test_compile_simple(void) {
    const char* src =
        "let a = 10\n"
        "let b = 20\n"
        "say a + b\n";

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "test.unfish", src);

    UfProgram* prog = parse_source(src, &arena, &interner, &reporter);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfBytecodeFunction* fn = uf_compile(prog, &rt, &reporter);
    assert(fn != NULL);
    assert(fn->chunk.code_count > 0);

    char* dis_buf = NULL;
    size_t dis_sz = 0;
    FILE* mem = open_memstream(&dis_buf, &dis_sz);
    assert(mem != NULL);
    uf_chunk_disassemble(&fn->chunk, "test_compile_simple", mem);
    fclose(mem);

    assert(dis_buf != NULL);
    assert(strstr(dis_buf, "OP_CONSTANT") != NULL);
    assert(strstr(dis_buf, "OP_DEFINE_GLOBAL") != NULL);
    assert(strstr(dis_buf, "OP_LOAD_GLOBAL") != NULL);
    assert(strstr(dis_buf, "OP_ADD") != NULL);
    assert(strstr(dis_buf, "OP_SAY") != NULL);
    assert(strstr(dis_buf, "OP_RETURN") != NULL);

    free(dis_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_compile_simple passed!\n");
}

static void test_compile_closure(void) {
    const char* src =
        "function make_adder(n):\n"
        "    let adder = function(x):\n"
        "        return n + x\n"
        "    return adder\n"
        "let add5 = make_adder(5)\n";

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "test.unfish", src);

    UfProgram* prog = parse_source(src, &arena, &interner, &reporter);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfBytecodeFunction* fn = uf_compile(prog, &rt, &reporter);
    assert(fn != NULL);

    char* dis_buf = NULL;
    size_t dis_sz = 0;
    FILE* mem = open_memstream(&dis_buf, &dis_sz);
    assert(mem != NULL);
    uf_chunk_disassemble(&fn->chunk, "test_compile_closure", mem);
    fclose(mem);

    assert(dis_buf != NULL);
    assert(strstr(dis_buf, "OP_CLOSURE") != NULL);

    free(dis_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_compile_closure passed!\n");
}

static void test_compile_loops_and_conditionals(void) {
    const char* src =
        "let x = 0\n"
        "while x < 10:\n"
        "    if x == 5:\n"
        "        break\n"
        "    x = x + 1\n"
        "repeat 3 times:\n"
        "    say x\n";

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "test.unfish", src);

    UfProgram* prog = parse_source(src, &arena, &interner, &reporter);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfBytecodeFunction* fn = uf_compile(prog, &rt, &reporter);
    assert(fn != NULL);

    char* dis_buf = NULL;
    size_t dis_sz = 0;
    FILE* mem = open_memstream(&dis_buf, &dis_sz);
    assert(mem != NULL);
    uf_chunk_disassemble(&fn->chunk, "test_compile_loops", mem);
    fclose(mem);

    assert(dis_buf != NULL);
    assert(strstr(dis_buf, "OP_JUMP_IF_FALSE") != NULL);
    assert(strstr(dis_buf, "OP_LOOP") != NULL);
    assert(strstr(dis_buf, "OP_JUMP") != NULL);

    free(dis_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_compile_loops_and_conditionals passed!\n");
}

int main(void) {
    printf("Running bytecode compiler unit tests...\n");
    test_compile_simple();
    test_compile_closure();
    test_compile_loops_and_conditionals();
    printf("All bytecode compiler unit tests passed successfully!\n");
    return 0;
}
