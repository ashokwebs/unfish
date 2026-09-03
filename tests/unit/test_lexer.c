#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../../src/common/uf_arena.h"
#include "../../src/common/uf_string.h"
#include "../../src/common/uf_diagnostic.h"
#include "../../src/lexer/uf_lexer.h"

static void test_vertical_slice_lexing(void) {
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
    uf_diag_reporter_init(&reporter, "test.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "test.unfish", src, &arena, &interner, &reporter);

    UfTokenKind expected[] = {
        UF_TOK_LET, UF_TOK_IDENTIFIER, UF_TOK_EQUAL, UF_TOK_STRING, UF_TOK_NEWLINE,
        UF_TOK_FUNCTION, UF_TOK_IDENTIFIER, UF_TOK_LPAREN, UF_TOK_IDENTIFIER, UF_TOK_RPAREN, UF_TOK_COLON, UF_TOK_NEWLINE,
        UF_TOK_INDENT,
        UF_TOK_SAY, UF_TOK_STRING, UF_TOK_PLUS, UF_TOK_IDENTIFIER, UF_TOK_NEWLINE,
        UF_TOK_DEDENT,
        UF_TOK_IDENTIFIER, UF_TOK_LPAREN, UF_TOK_IDENTIFIER, UF_TOK_RPAREN, UF_TOK_NEWLINE,
        UF_TOK_EOF
    };

    size_t count = sizeof(expected) / sizeof(expected[0]);
    for (size_t i = 0; i < count; ++i) {
        UfToken tok = uf_lexer_next_token(&lexer);
        if (tok.kind != expected[i]) {
            fprintf(stderr, "Token mismatch at step %zu: got %s, expected %s\n",
                    i, uf_token_kind_name(tok.kind), uf_token_kind_name(expected[i]));
            assert(tok.kind == expected[i]);
        }
    }

    assert(reporter.error_count == 0);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_vertical_slice_lexing passed!\n");
}

static void test_string_escapes(void) {
    const char* src = "\"Hello,\\tWorld!\\nEscapes: \\\"\\\\\"\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "string_test.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "string_test.unfish", src, &arena, &interner, &reporter);

    UfToken tok = uf_lexer_next_token(&lexer);
    assert(tok.kind == UF_TOK_STRING);
    assert(strcmp(tok.as.string_val, "Hello,\tWorld!\nEscapes: \"\\") == 0);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_string_escapes passed!\n");
}

static void test_nested_indentation(void) {
    const char* src =
        "if true:\n"
        "    if true:\n"
        "        say 1\n"
        "    say 2\n"
        "say 3\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "indent_test.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "indent_test.unfish", src, &arena, &interner, &reporter);

    UfTokenKind expected[] = {
        UF_TOK_IF, UF_TOK_TRUE, UF_TOK_COLON, UF_TOK_NEWLINE,
        UF_TOK_INDENT,
        UF_TOK_IF, UF_TOK_TRUE, UF_TOK_COLON, UF_TOK_NEWLINE,
        UF_TOK_INDENT,
        UF_TOK_SAY, UF_TOK_NUMBER, UF_TOK_NEWLINE,
        UF_TOK_DEDENT,
        UF_TOK_SAY, UF_TOK_NUMBER, UF_TOK_NEWLINE,
        UF_TOK_DEDENT,
        UF_TOK_SAY, UF_TOK_NUMBER, UF_TOK_NEWLINE,
        UF_TOK_EOF
    };

    size_t count = sizeof(expected) / sizeof(expected[0]);
    for (size_t i = 0; i < count; ++i) {
        UfToken tok = uf_lexer_next_token(&lexer);
        assert(tok.kind == expected[i]);
    }

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_nested_indentation passed!\n");
}

int main(void) {
    printf("Running lexer tests...\n");
    test_vertical_slice_lexing();
    test_string_escapes();
    test_nested_indentation();
    printf("All lexer unit tests passed successfully!\n");
    return 0;
}
