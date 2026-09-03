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

static void test_valid_vertical_slice(void) {
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
    uf_diag_reporter_init(&reporter, "valid_slice.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "valid_slice.unfish", src, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool ok = uf_analyze_program(&sema, program);
    assert(ok);
    assert(reporter.error_count == 0);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_valid_vertical_slice passed!\n");
}

static void test_undefined_variable_with_hint(void) {
    const char* src =
        "let score = 100\n"
        "say scoore\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "undef.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "undef.unfish", src, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool ok = uf_analyze_program(&sema, program);
    assert(!ok);
    assert(reporter.error_count == 1);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_undefined_variable_with_hint passed!\n");
}

static void test_duplicate_declaration(void) {
    const char* src =
        "let x = 1\n"
        "let x = 2\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "dup.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "dup.unfish", src, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool ok = uf_analyze_program(&sema, program);
    assert(!ok);
    assert(reporter.error_count == 1);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_duplicate_declaration passed!\n");
}

static void test_return_outside_function(void) {
    const char* src =
        "say 1\n"
        "return 42\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "ret.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "ret.unfish", src, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool ok = uf_analyze_program(&sema, program);
    assert(!ok);
    assert(reporter.error_count == 1);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_return_outside_function passed!\n");
}

static void test_arity_mismatch(void) {
    const char* src =
        "function add(a, b):\n"
        "    return a + b\n"
        "add(1)\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "arity.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "arity.unfish", src, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);
    assert(program && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool ok = uf_analyze_program(&sema, program);
    assert(!ok);
    assert(reporter.error_count == 1);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_arity_mismatch passed!\n");
}

int main(void) {
    printf("Running semantic analyzer tests...\n");
    test_valid_vertical_slice();
    test_undefined_variable_with_hint();
    test_duplicate_declaration();
    test_return_outside_function();
    test_arity_mismatch();
    printf("All semantic analyzer tests passed successfully!\n");
    return 0;
}
