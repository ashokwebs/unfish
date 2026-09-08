#ifndef UF_LEXER_H
#define UF_LEXER_H

#include "../common/uf_common.h"
#include "../common/uf_source.h"
#include "../common/uf_arena.h"
#include "../common/uf_string.h"
#include "../common/uf_diagnostic.h"
#include "uf_token.h"

#define UF_MAX_INDENT_DEPTH 128

typedef struct {
    const char* source;
    const char* file_name;
    const char* start;
    const char* current;
    uint32_t line;
    uint32_t col;
    uint32_t start_col;
    uint32_t start_line;

    uint32_t indent_stack[UF_MAX_INDENT_DEPTH];
    int indent_depth;
    int pending_dedents;
    bool at_line_start;
    int paren_depth;
    int paren_indent_stack[UF_MAX_INDENT_DEPTH];

    UfTokenKind last_token_kind;
    bool has_tokens;

    UfArena* arena;
    UfInterner* interner;
    UfDiagnosticReporter* reporter;
} UfLexer;

void uf_lexer_init(UfLexer* lexer,
                   const char* file_name,
                   const char* source,
                   UfArena* arena,
                   UfInterner* interner,
                   UfDiagnosticReporter* reporter);

UfToken uf_lexer_next_token(UfLexer* lexer);

#endif /* UF_LEXER_H */
