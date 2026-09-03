#ifndef UF_PARSER_H
#define UF_PARSER_H

#include "../common/uf_common.h"
#include "../common/uf_arena.h"
#include "../common/uf_diagnostic.h"
#include "../lexer/uf_lexer.h"
#include "../ast/uf_ast.h"

typedef struct {
    UfLexer* lexer;
    UfToken previous;
    UfToken current;
    bool had_error;
    bool panic_mode;

    UfArena* arena;
    UfDiagnosticReporter* reporter;
} UfParser;

void uf_parser_init(UfParser* parser,
                    UfLexer* lexer,
                    UfArena* arena,
                    UfDiagnosticReporter* reporter);

UfProgram* uf_parse_program(UfParser* parser);
UfStmt* uf_parse_statement(UfParser* parser);
UfExpr* uf_parse_expression(UfParser* parser);

#endif /* UF_PARSER_H */
