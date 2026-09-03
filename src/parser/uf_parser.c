#include "uf_parser.h"

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  /* or */
    PREC_OR,          /* or */
    PREC_AND,         /* and */
    PREC_EQUALITY,    /* == != */
    PREC_COMPARISON,  /* < <= > >= */
    PREC_TERM,        /* + - */
    PREC_FACTOR,      /* * / % */
    PREC_UNARY,       /* - not */
    PREC_CALL,        /* () */
    PREC_PRIMARY
} Precedence;

typedef UfExpr* (*PrefixParseFn)(UfParser* parser);
typedef UfExpr* (*InfixParseFn)(UfParser* parser, UfExpr* left);

typedef struct {
    PrefixParseFn prefix;
    InfixParseFn infix;
    Precedence precedence;
} ParseRule;

static const ParseRule* get_rule(UfTokenKind kind);
static UfExpr* parse_precedence(UfParser* parser, Precedence precedence);
static UfStmt* parse_statement(UfParser* parser);
static UfStmt* parse_block(UfParser* parser);

/* Advance helper maintaining lookahead */
static void advance(UfParser* parser) {
    parser->previous = parser->current;

    for (;;) {
        parser->current = uf_lexer_next_token(parser->lexer);
        if (parser->current.kind != UF_TOK_ERROR) {
            break;
        }
        parser->had_error = true;
    }
}

static bool check(const UfParser* parser, UfTokenKind kind) {
    return parser->current.kind == kind;
}

static bool match(UfParser* parser, UfTokenKind kind) {
    if (!check(parser, kind)) return false;
    advance(parser);
    return true;
}

static void error_at(UfParser* parser, const UfToken* token, const char* message, const char* hint) {
    if (parser->panic_mode) return;
    parser->panic_mode = true;
    parser->had_error = true;

    if (parser->reporter) {
        uf_report_diag(parser->reporter, UF_DIAG_SYNTAX_ERROR, token->span, message, hint);
    }
}

static void error_current(UfParser* parser, const char* message, const char* hint) {
    error_at(parser, &parser->current, message, hint);
}

static void consume(UfParser* parser, UfTokenKind kind, const char* message, const char* hint) {
    if (parser->current.kind == kind) {
        advance(parser);
        return;
    }
    error_current(parser, message, hint);
}

static void synchronize(UfParser* parser) {
    parser->panic_mode = false;

    while (parser->current.kind != UF_TOK_EOF) {
        if (parser->previous.kind == UF_TOK_NEWLINE) return;

        switch (parser->current.kind) {
            case UF_TOK_FUNCTION:
            case UF_TOK_LET:
            case UF_TOK_IF:
            case UF_TOK_WHILE:
            case UF_TOK_REPEAT:
            case UF_TOK_RETURN:
            case UF_TOK_SAY:
            case UF_TOK_DEDENT:
                return;
            default:
                break;
        }

        advance(parser);
    }
}

/* --- Pratt Parser Expression Rules --- */

static UfExpr* parse_literal(UfParser* parser) {
    UfToken tok = parser->previous;
    switch (tok.kind) {
        case UF_TOK_NULL:
            return uf_expr_literal_null(parser->arena, tok.span);
        case UF_TOK_TRUE:
            return uf_expr_literal_bool(parser->arena, tok.span, true);
        case UF_TOK_FALSE:
            return uf_expr_literal_bool(parser->arena, tok.span, false);
        case UF_TOK_NUMBER:
            return uf_expr_literal_number(parser->arena, tok.span, tok.as.number_val);
        case UF_TOK_STRING:
            return uf_expr_literal_string(parser->arena, tok.span, tok.as.string_val);
        default:
            return NULL;
    }
}

static UfExpr* parse_identifier(UfParser* parser) {
    UfToken tok = parser->previous;
    return uf_expr_identifier(parser->arena, tok.span, tok.as.string_val);
}

static UfExpr* parse_grouping(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    UfExpr* inner = uf_parse_expression(parser);
    consume(parser, UF_TOK_RPAREN, "Expected ')' after expression", "Ensure all opened parentheses are closed");
    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_expr_grouping(parser->arena, span, inner);
}

static UfExpr* parse_unary(UfParser* parser) {
    UfToken op_tok = parser->previous;
    UfExpr* operand = parse_precedence(parser, PREC_UNARY);
    SourceSpan span = source_span_join(op_tok.span, operand ? operand->span : op_tok.span);
    return uf_expr_unary(parser->arena, span, op_tok.kind, operand);
}

static UfExpr* parse_binary(UfParser* parser, UfExpr* left) {
    UfToken op_tok = parser->previous;
    const ParseRule* rule = get_rule(op_tok.kind);
    /* Left-associative operators pass precedence + 1 */
    UfExpr* right = parse_precedence(parser, (Precedence)(rule->precedence + 1));
    SourceSpan span = source_span_join(left->span, right ? right->span : op_tok.span);
    return uf_expr_binary(parser->arena, span, op_tok.kind, left, right);
}

static UfExpr* parse_call(UfParser* parser, UfExpr* left) {
    /* parser->previous is UF_TOK_LPAREN */
    UfExpr* args[64];
    size_t argc = 0;

    if (!check(parser, UF_TOK_RPAREN)) {
        do {
            if (argc >= 64) {
                error_current(parser, "Cannot have more than 64 arguments in function call", NULL);
                break;
            }
            args[argc++] = uf_parse_expression(parser);
        } while (match(parser, UF_TOK_COMMA));
    }

    consume(parser, UF_TOK_RPAREN, "Expected ')' after function arguments", "Close argument list with ')'");

    SourceSpan span = source_span_join(left->span, parser->previous.span);

    UfExpr** args_copy = NULL;
    if (argc > 0) {
        args_copy = (UfExpr**)uf_arena_alloc(parser->arena, argc * sizeof(UfExpr*));
        memcpy(args_copy, args, argc * sizeof(UfExpr*));
    }

    return uf_expr_call(parser->arena, span, left, args_copy, argc);
}

static const ParseRule rules[] = {
    [UF_TOK_EOF]        = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_ERROR]      = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_NEWLINE]    = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_INDENT]     = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_DEDENT]     = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_IDENTIFIER] = { parse_identifier, NULL,         PREC_NONE },
    [UF_TOK_NUMBER]     = { parse_literal,    NULL,         PREC_NONE },
    [UF_TOK_STRING]     = { parse_literal,    NULL,         PREC_NONE },
    [UF_TOK_LET]        = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_SAY]        = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_FUNCTION]   = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_RETURN]     = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_IF]         = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_ELSE]       = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_WHILE]      = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_REPEAT]     = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_TIMES]      = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_AND]        = { NULL,             parse_binary, PREC_AND },
    [UF_TOK_OR]         = { NULL,             parse_binary, PREC_OR },
    [UF_TOK_NOT]        = { parse_unary,      NULL,         PREC_UNARY },
    [UF_TOK_TRUE]       = { parse_literal,    NULL,         PREC_NONE },
    [UF_TOK_FALSE]      = { parse_literal,    NULL,         PREC_NONE },
    [UF_TOK_NULL]       = { parse_literal,    NULL,         PREC_NONE },
    [UF_TOK_PLUS]       = { NULL,             parse_binary, PREC_TERM },
    [UF_TOK_MINUS]      = { parse_unary,      parse_binary, PREC_TERM },
    [UF_TOK_STAR]       = { NULL,             parse_binary, PREC_FACTOR },
    [UF_TOK_SLASH]      = { NULL,             parse_binary, PREC_FACTOR },
    [UF_TOK_PERCENT]    = { NULL,             parse_binary, PREC_FACTOR },
    [UF_TOK_EQUAL]      = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_EQEQ]       = { NULL,             parse_binary, PREC_EQUALITY },
    [UF_TOK_BANGEQ]     = { NULL,             parse_binary, PREC_EQUALITY },
    [UF_TOK_LT]         = { NULL,             parse_binary, PREC_COMPARISON },
    [UF_TOK_LTEQ]       = { NULL,             parse_binary, PREC_COMPARISON },
    [UF_TOK_GT]         = { NULL,             parse_binary, PREC_COMPARISON },
    [UF_TOK_GTEQ]       = { NULL,             parse_binary, PREC_COMPARISON },
    [UF_TOK_LPAREN]     = { parse_grouping,   parse_call,   PREC_CALL },
    [UF_TOK_RPAREN]     = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_COLON]      = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_COMMA]      = { NULL,             NULL,         PREC_NONE },
};

static const ParseRule* get_rule(UfTokenKind kind) {
    if ((size_t)kind < sizeof(rules) / sizeof(rules[0])) {
        return &rules[kind];
    }
    return &rules[UF_TOK_ERROR];
}

static UfExpr* parse_precedence(UfParser* parser, Precedence precedence) {
    advance(parser);
    PrefixParseFn prefix_rule = get_rule(parser->previous.kind)->prefix;
    if (!prefix_rule) {
        error_at(parser, &parser->previous, "Expected expression", "Provide a variable, literal value, or subexpression");
        return NULL;
    }

    UfExpr* expr = prefix_rule(parser);

    while (precedence <= get_rule(parser->current.kind)->precedence) {
        advance(parser);
        InfixParseFn infix_rule = get_rule(parser->previous.kind)->infix;
        if (infix_rule) {
            expr = infix_rule(parser, expr);
        }
    }

    return expr;
}

UfExpr* uf_parse_expression(UfParser* parser) {
    return parse_precedence(parser, PREC_ASSIGNMENT);
}

/* --- Statement Parsing --- */

static UfStmt* parse_block(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    consume(parser, UF_TOK_COLON, "Expected ':' before block", "Add ':' after the statement header");
    consume(parser, UF_TOK_NEWLINE, "Expected newline after ':'", NULL);
    consume(parser, UF_TOK_INDENT, "Expected indented block", "Indent the body of the block with 4 spaces");

    UfStmt* stmts[256];
    size_t count = 0;

    while (!check(parser, UF_TOK_DEDENT) && !check(parser, UF_TOK_EOF)) {
        if (match(parser, UF_TOK_NEWLINE)) {
            continue;
        }
        if (count >= 256) {
            error_current(parser, "Block exceeds maximum statement limit (256)", NULL);
            break;
        }
        UfStmt* stmt = parse_statement(parser);
        if (stmt) {
            stmts[count++] = stmt;
        }
    }

    consume(parser, UF_TOK_DEDENT, "Expected unindent to close block", NULL);

    SourceSpan span = source_span_make(start, parser->previous.span.end);
    UfStmt** stmts_copy = NULL;
    if (count > 0) {
        stmts_copy = (UfStmt**)uf_arena_alloc(parser->arena, count * sizeof(UfStmt*));
        memcpy(stmts_copy, stmts, count * sizeof(UfStmt*));
    }

    return uf_stmt_block(parser->arena, span, stmts_copy, count);
}

static UfStmt* parse_let_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    consume(parser, UF_TOK_IDENTIFIER, "Expected variable name after 'let'", "Provide an identifier name, e.g., 'let count = 0'");
    const char* name = parser->previous.as.string_val;

    UfExpr* init = NULL;
    if (match(parser, UF_TOK_EQUAL)) {
        init = uf_parse_expression(parser);
    }

    consume(parser, UF_TOK_NEWLINE, "Expected newline after variable declaration", NULL);
    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_let(parser->arena, span, name, init);
}

static UfStmt* parse_say_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    UfExpr* expr = uf_parse_expression(parser);
    consume(parser, UF_TOK_NEWLINE, "Expected newline after 'say' statement", NULL);
    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_say(parser->arena, span, expr);
}

static UfStmt* parse_return_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    UfExpr* val = NULL;
    if (!check(parser, UF_TOK_NEWLINE) && !check(parser, UF_TOK_EOF)) {
        val = uf_parse_expression(parser);
    }
    consume(parser, UF_TOK_NEWLINE, "Expected newline after 'return' statement", NULL);
    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_return(parser->arena, span, val);
}

static UfStmt* parse_if_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    UfExpr* condition = uf_parse_expression(parser);
    UfStmt* then_branch = parse_block(parser);

    UfStmt* else_branch = NULL;
    if (match(parser, UF_TOK_ELSE)) {
        if (match(parser, UF_TOK_IF)) {
            else_branch = parse_if_statement(parser);
        } else {
            else_branch = parse_block(parser);
        }
    }

    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_if(parser->arena, span, condition, then_branch, else_branch);
}

static UfStmt* parse_while_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    UfExpr* condition = uf_parse_expression(parser);
    UfStmt* body = parse_block(parser);
    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_while(parser->arena, span, condition, body);
}

static UfStmt* parse_repeat_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    UfExpr* count_expr = uf_parse_expression(parser);
    consume(parser, UF_TOK_TIMES, "Expected 'times' after repeat count", "Syntax: 'repeat <number> times:'");
    UfStmt* body = parse_block(parser);
    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_repeat(parser->arena, span, count_expr, body);
}

static UfStmt* parse_function_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    consume(parser, UF_TOK_IDENTIFIER, "Expected function name", "Syntax: 'function <name>(<parameters>):'");
    const char* fn_name = parser->previous.as.string_val;

    consume(parser, UF_TOK_LPAREN, "Expected '(' after function name", NULL);

    const char* params[32];
    size_t param_count = 0;

    if (!check(parser, UF_TOK_RPAREN)) {
        do {
            if (param_count >= 32) {
                error_current(parser, "Functions cannot have more than 32 parameters", NULL);
                break;
            }
            consume(parser, UF_TOK_IDENTIFIER, "Expected parameter name", NULL);
            params[param_count++] = parser->previous.as.string_val;
        } while (match(parser, UF_TOK_COMMA));
    }

    consume(parser, UF_TOK_RPAREN, "Expected ')' after parameters", NULL);

    UfStmt* body = parse_block(parser);
    SourceSpan span = source_span_make(start, parser->previous.span.end);

    const char** params_copy = NULL;
    if (param_count > 0) {
        params_copy = (const char**)uf_arena_alloc(parser->arena, param_count * sizeof(const char*));
        memcpy(params_copy, params, param_count * sizeof(const char*));
    }

    return uf_stmt_function(parser->arena, span, fn_name, params_copy, param_count, body);
}

static UfStmt* parse_expression_or_assignment_statement(UfParser* parser) {
    SourceLoc start = parser->current.span.start;

    /* Check if this is an assignment: IDENTIFIER '=' ... */
    if (check(parser, UF_TOK_IDENTIFIER)) {
        /* Advance to identifier */
        advance(parser);
        const char* id_name = parser->previous.as.string_val;
        SourceSpan id_span = parser->previous.span;

        if (match(parser, UF_TOK_EQUAL)) {
            UfExpr* val = uf_parse_expression(parser);
            consume(parser, UF_TOK_NEWLINE, "Expected newline after assignment", NULL);
            SourceSpan span = source_span_make(start, parser->previous.span.end);
            return uf_stmt_assign(parser->arena, span, id_name, val);
        }

        /* It was not an assignment; re-construct expression beginning with this identifier */
        UfExpr* expr = uf_expr_identifier(parser->arena, id_span, id_name);
        while (PREC_ASSIGNMENT <= get_rule(parser->current.kind)->precedence) {
            advance(parser);
            InfixParseFn infix_rule = get_rule(parser->previous.kind)->infix;
            if (infix_rule) {
                expr = infix_rule(parser, expr);
            }
        }

        consume(parser, UF_TOK_NEWLINE, "Expected newline after expression statement", NULL);
        SourceSpan span = source_span_make(start, parser->previous.span.end);
        return uf_stmt_expr(parser->arena, span, expr);
    }

    /* General expression statement */
    UfExpr* expr = uf_parse_expression(parser);
    consume(parser, UF_TOK_NEWLINE, "Expected newline after expression", NULL);
    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_expr(parser->arena, span, expr);
}

static UfStmt* parse_statement(UfParser* parser) {
    if (match(parser, UF_TOK_LET))      return parse_let_statement(parser);
    if (match(parser, UF_TOK_SAY))      return parse_say_statement(parser);
    if (match(parser, UF_TOK_RETURN))   return parse_return_statement(parser);
    if (match(parser, UF_TOK_IF))       return parse_if_statement(parser);
    if (match(parser, UF_TOK_WHILE))    return parse_while_statement(parser);
    if (match(parser, UF_TOK_REPEAT))   return parse_repeat_statement(parser);
    if (match(parser, UF_TOK_FUNCTION)) return parse_function_statement(parser);

    return parse_expression_or_assignment_statement(parser);
}

void uf_parser_init(UfParser* parser,
                    UfLexer* lexer,
                    UfArena* arena,
                    UfDiagnosticReporter* reporter) {
    parser->lexer = lexer;
    parser->arena = arena;
    parser->reporter = reporter;
    parser->had_error = false;
    parser->panic_mode = false;

    /* Prime current token */
    advance(parser);
}

UfProgram* uf_parse_program(UfParser* parser) {
    UfStmt* stmts[1024];
    size_t count = 0;
    SourceLoc start = parser->current.span.start;

    while (!check(parser, UF_TOK_EOF)) {
        if (match(parser, UF_TOK_NEWLINE)) {
            continue;
        }

        if (count >= 1024) {
            error_current(parser, "Program exceeds maximum top-level statement limit (1024)", NULL);
            break;
        }

        UfStmt* stmt = parse_statement(parser);
        if (stmt) {
            stmts[count++] = stmt;
        } else {
            synchronize(parser);
        }
    }

    SourceSpan span = source_span_make(start, parser->previous.span.end);
    UfProgram* program = (UfProgram*)uf_arena_alloc(parser->arena, sizeof(UfProgram));
    program->count = count;
    program->span = span;
    program->stmts = NULL;

    if (count > 0) {
        program->stmts = (UfStmt**)uf_arena_alloc(parser->arena, count * sizeof(UfStmt*));
        memcpy(program->stmts, stmts, count * sizeof(UfStmt*));
    }

    return program;
}
