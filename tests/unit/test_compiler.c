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


/* Regression: a nested function that blows an internal compiler limit used to
 * leave the enclosing compiler happily emitting a closure over half-built
 * bytecode. The VM then ran it and printed `null` instead of the real result,
 * with a success exit code and no diagnostic at all. Compilation must fail
 * loudly, and the failure must propagate out of the nested function. */
static void test_compile_reports_internal_limits(void) {
    /* 300 locals accumulated across nested scopes inside one function, which
     * overflows the 256-slot local array while keeping every individual block
     * under the parser's per-block statement limit. */
    UfStrBuf sb;
    uf_strbuf_init(&sb);
    uf_strbuf_append(&sb, "fn f(k):\n");
    int n = 0;
    for (int blk = 0; blk < 6; ++blk) {
        char header[128];
        snprintf(header, sizeof(header), "%*sif k > -1:\n", 4 * (blk + 1), "");
        uf_strbuf_append(&sb, header);
        for (int i = 0; i < 50; ++i) {
            char line[160];
            snprintf(line, sizeof(line), "%*slet v%d = k + %d\n", 4 * (blk + 2), "", n, n);
            uf_strbuf_append(&sb, line);
            n++;
        }
    }
    {
        char tail[128];
        snprintf(tail, sizeof(tail), "%*sreturn 7\n", 4 * 7, "");
        uf_strbuf_append(&sb, tail);
    }
    uf_strbuf_append(&sb, "say(f(1))\n");

    const char* src = sb.data;

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

    /* Must refuse to produce bytecode, and must say why. */
    assert(fn == NULL);
    assert(reporter.error_count > 0);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    uf_strbuf_free(&sb);
    printf("test_compile_reports_internal_limits passed!\n");
}

int main(void) {
    printf("Running bytecode compiler unit tests...\n");
    test_compile_simple();
    test_compile_closure();
    test_compile_loops_and_conditionals();
    test_compile_reports_internal_limits();
    printf("All bytecode compiler unit tests passed successfully!\n");
    return 0;
}
