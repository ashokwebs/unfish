#include "uf_common.h"
#include "uf_arena.h"
#include "uf_string.h"
#include "uf_diagnostic.h"
#include "uf_token.h"
#include "uf_lexer.h"
#include "uf_ast.h"
#include "uf_parser.h"
#include "uf_formatter.h"
#include "uf_blocks.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* read_file_contents(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* b = (char*)malloc(sz + 1);
    if (!b) { fclose(f); return NULL; }
    size_t rd = fread(b, 1, sz, f);
    b[rd] = '\0';
    fclose(f);
    return b;
}

static void test_blocks_basic(void) {
    const char* src =
        "let x = 42\n"
        "say x + 1\n";

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

    char* json = uf_blocks_export_string(prog);
    assert(json != NULL);
    assert(strstr(json, "\"schema\": \"unfish_blocks_v1\"") != NULL);
    assert(strstr(json, "\"kind\": \"let\"") != NULL);
    assert(strstr(json, "\"name\": \"x\"") != NULL);

    UfProgram* imported = uf_blocks_import_string(json, &arena, &interner, &reporter);
    assert(imported != NULL);
    assert(imported->count == 2);

    char* fmt_orig = uf_format_program(prog);
    char* fmt_imp = uf_format_program(imported);
    assert(strcmp(fmt_orig, fmt_imp) == 0);

    free(json);
    free(fmt_orig);
    free(fmt_imp);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_blocks_basic passed!\n");
}

static void test_roundtrip_file(const char* filepath) {
    char* src = read_file_contents(filepath);
    assert(src != NULL);

    UfArena arena;
    uf_arena_init(&arena, 65536);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, filepath, src);

    UfLexer lexer;
    uf_lexer_init(&lexer, filepath, src, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* prog1 = uf_parse_program(&parser);
    assert(prog1 && !parser.had_error);

    char* orig_fmt = uf_format_program(prog1);
    char* json = uf_blocks_export_string(prog1);
    assert(json != NULL);

    UfProgram* prog2 = uf_blocks_import_string(json, &arena, &interner, &reporter);
    assert(prog2 != NULL);

    char* roundtrip_fmt = uf_format_program(prog2);
    assert(roundtrip_fmt != NULL);

    if (strcmp(orig_fmt, roundtrip_fmt) != 0) {
        fprintf(stderr, "Mismatch in round-trip for %s!\nORIGINAL FORMATTED:\n%s\nROUNDTRIP FORMATTED:\n%s\n",
                filepath, orig_fmt, roundtrip_fmt);
        assert(false);
    }

    free(src);
    free(orig_fmt);
    free(json);
    free(roundtrip_fmt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
}

static void test_invalid_json(void) {
    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<invalid>", "");

    assert(uf_blocks_import_string(NULL, &arena, &interner, &reporter) == NULL);
    assert(uf_blocks_import_string("not json", &arena, &interner, &reporter) == NULL);
    assert(uf_blocks_import_string("{}", &arena, &interner, &reporter) == NULL);
    assert(uf_blocks_import_string("{\"statements\": \"bad\"}", &arena, &interner, &reporter) == NULL);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_invalid_json passed!\n");
}

int main(void) {
    printf("Running blocks round-trip unit tests...\n");
    test_blocks_basic();
    test_invalid_json();

    const char* test_files[] = {
        "tests/conformance/01_hello.unfish",
        "tests/conformance/03_conditions.unfish",
        "tests/conformance/04_loops.unfish",
        "tests/conformance/05_functions.unfish",
        "tests/conformance/11_arrays.unfish",
        "tests/conformance/15_maps.unfish",
        "tests/conformance/33_type_annotations.unfish",
        "tests/conformance/34_structs.unfish",
        "tests/conformance/35_pattern_matching.unfish"
    };
    size_t num_files = sizeof(test_files) / sizeof(test_files[0]);

    for (size_t i = 0; i < num_files; ++i) {
        test_roundtrip_file(test_files[i]);
    }
    printf("All %zu conformance files round-tripped losslessly: Text -> AST -> JSON -> AST -> Text!\n", num_files);
    printf("All blocks unit tests passed successfully!\n");
    return 0;
}
