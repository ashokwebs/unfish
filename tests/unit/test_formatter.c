#define _POSIX_C_SOURCE 200809L
#include "uf_common.h"
#include "uf_arena.h"
#include "uf_string.h"
#include "uf_diagnostic.h"
#include "uf_token.h"
#include "uf_lexer.h"
#include "uf_ast.h"
#include "uf_parser.h"
#include "uf_formatter.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* format_code(const char* src) {
    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "test.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "test.unfish", src, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog && !parser.had_error);

    char* formatted = uf_format_program(prog);
    assert(formatted != NULL);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    return formatted;
}

static void test_basic_formatting(void) {
    const char* src =
        "let x=10\n"
        "let y:Number=20\n"
        "say x+y*2\n";

    char* out = format_code(src);
    const char* expected =
        "let x = 10\n"
        "let y: Number = 20\n"
        "say x + y * 2\n";

    assert(strcmp(out, expected) == 0);
    free(out);
    printf("test_basic_formatting passed!\n");
}

static void test_idempotency(void) {
    const char* src =
        "function compute(a: Number, b: Number): Number:\n"
        "    if a > b:\n"
        "        return a - b\n"
        "    else:\n"
        "        return b - a\n"
        "\n"
        "let res = compute(10, 20)\n"
        "say res\n";

    char* out1 = format_code(src);
    char* out2 = format_code(out1);

    assert(strcmp(out1, out2) == 0);
    free(out1);
    free(out2);
    printf("test_idempotency passed!\n");
}

static void test_struct_and_match_formatting(void) {
    const char* src =
        "struct Point:\n"
        "    x: Number\n"
        "    y: Number\n"
        "\n"
        "function inspect(p):\n"
        "    match p:\n"
        "        when Point(0, 0):\n"
        "            say \"origin\"\n"
        "        when Point(x, y) if x > 0:\n"
        "            say \"positive x\"\n"
        "        else:\n"
        "            say \"other\"\n";

    char* out1 = format_code(src);
    char* out2 = format_code(out1);

    assert(strcmp(out1, out2) == 0);
    free(out1);
    free(out2);
    printf("test_struct_and_match_formatting passed!\n");
}

static void test_roundtrip_conformance_files(void) {
    const char* files[] = {
        "tests/conformance/01_hello.unfish",
        "tests/conformance/05_functions.unfish",
        "tests/conformance/34_structs.unfish",
        "tests/conformance/35_pattern_matching.unfish",
        NULL
    };

    for (int i = 0; files[i]; ++i) {
        FILE* f = fopen(files[i], "rb");
        assert(f != NULL);
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char* buf = (char*)malloc(sz + 1);
        size_t rd = fread(buf, 1, sz, f);
        buf[rd] = '\0';
        fclose(f);

        char* f1 = format_code(buf);
        char* f2 = format_code(f1);
        assert(strcmp(f1, f2) == 0);

        free(buf);
        free(f1);
        free(f2);
    }

    printf("test_roundtrip_conformance_files passed!\n");
}

int main(void) {
    printf("Running formatter unit tests...\n");
    test_basic_formatting();
    test_idempotency();
    test_struct_and_match_formatting();
    test_roundtrip_conformance_files();
    printf("All formatter unit tests passed successfully!\n");
    return 0;
}
