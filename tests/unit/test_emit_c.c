#include "../../src/codegen/uf_emit_c.h"
#include "../../src/ast/uf_ast.h"
#include "../../src/parser/uf_parser.h"
#include "../../src/lexer/uf_lexer.h"
#include "../../src/semantic/uf_semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

static UfProgram* parse_string(const char* source, UfArena* arena, UfInterner* interner, UfDiagnosticReporter* reporter) {
    UfLexer lexer;
    uf_lexer_init(&lexer, "<test>", source, arena, interner, reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, arena, reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog != NULL);
    assert(reporter->error_count == 0);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, arena, reporter);
    bool ok = uf_analyze_program(&sema, prog);
    assert(ok);
    return prog;
}

static void test_emit_c_code(void) {
    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");

    const char* code =
        "let x = 10\n"
        "let y = 20\n"
        "say x + y\n";

    UfProgram* prog = parse_string(code, &arena, &interner, &reporter);

    FILE* stream = tmpfile();
    assert(stream != NULL);
    bool ok = uf_emit_c_program(prog, stream);
    assert(ok);

    rewind(stream);
    char buf[4096];
    size_t len = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[len] = '\0';
    fclose(stream);

    assert(strstr(buf, "#include \"unfish_runtime.h\"") != NULL);
    assert(strstr(buf, "static UfVal uf_var_x;") != NULL);
    assert(strstr(buf, "static UfVal uf_var_y;") != NULL);
    assert(strstr(buf, "uf_add(uf_var_x, uf_var_y)") != NULL);
    assert(strstr(buf, "int main(") != NULL);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_emit_c_code passed!\n");
}

static void test_build_and_execute_native(void) {
    UfArena arena;
    uf_arena_init(&arena, 32768);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");

    const char* code =
        "function square(n):\n"
        "    return n * n\n"
        "let arr = [1, 2, 3]\n"
        "let total = 0\n"
        "for x in arr:\n"
        "    total = total + square(x)\n"
        "say total\n";

    UfProgram* prog = parse_string(code, &arena, &interner, &reporter);

    const char* bin_path = "/tmp/test_uf_native_square";
    bool built = uf_build_native(prog, bin_path);
    assert(built);

    /* Execute native binary */
    FILE* pipe = popen(bin_path, "r");
    assert(pipe != NULL);
    char out_buf[128];
    char* s = fgets(out_buf, sizeof(out_buf), pipe);
    assert(s != NULL);
    pclose(pipe);
    remove(bin_path);

    /* 1^2 + 2^2 + 3^2 = 1 + 4 + 9 = 14 */
    assert(atoi(out_buf) == 14);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_build_and_execute_native passed!\n");
}

int main(void) {
    printf("Running C99 code emission & native compilation tests...\n");
    test_emit_c_code();
    test_build_and_execute_native();
    printf("All C99 emission tests passed successfully!\n");
    return 0;
}
