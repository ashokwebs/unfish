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

static UfToken peek(UfParser* parser) {
    if (!parser->has_peek) {
        for (;;) {
            parser->peek_token = uf_lexer_next_token(parser->lexer);
            if (parser->peek_token.kind != UF_TOK_ERROR) {
                break;
            }
            parser->had_error = true;
        }
        parser->has_peek = true;
    }
    return parser->peek_token;
}

/* Advance helper maintaining lookahead */
static void advance(UfParser* parser) {
    parser->previous = parser->current;

    if (parser->has_peek) {
        parser->current = parser->peek_token;
        parser->has_peek = false;
        return;
    }

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
            case UF_TOK_TRY:
            case UF_TOK_CATCH:
            case UF_TOK_IMPORT:
            case UF_TOK_FROM:
            case UF_TOK_DEDENT:
                return;
            default:
                break;
        }

        advance(parser);
    }
}

/* --- Pratt Parser Expression Rules --- */

static UfStmt* parse_statement(UfParser* parser);

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

static UfExpr* parse_array(UfParser* parser) {
    SourceSpan start_span = parser->previous.span;
    UfExpr* elements[256];
    size_t count = 0;

    if (!check(parser, UF_TOK_RBRACKET)) {
        do {
            if (check(parser, UF_TOK_RBRACKET)) break;
            elements[count++] = uf_parse_expression(parser);
        } while (match(parser, UF_TOK_COMMA));
    }

    consume(parser, UF_TOK_RBRACKET, "Expected ']' after array elements", "Close array literal with ']'");

    UfExpr** elems_copy = NULL;
    if (count > 0) {
        elems_copy = (UfExpr**)uf_arena_alloc(parser->arena, count * sizeof(UfExpr*));
        memcpy(elems_copy, elements, count * sizeof(UfExpr*));
    }

    SourceSpan span = source_span_join(start_span, parser->previous.span);
    return uf_expr_array(parser->arena, span, elems_copy, count);
}

static UfExpr* parse_index(UfParser* parser, UfExpr* left) {
    /* parser->previous is UF_TOK_LBRACKET */
    UfExpr* index = uf_parse_expression(parser);
    consume(parser, UF_TOK_RBRACKET, "Expected ']' after index", "Close index expression with ']'");
    SourceSpan span = source_span_join(left->span, parser->previous.span);
    return uf_expr_index(parser->arena, span, left, index);
}

static UfExpr* parse_map(UfParser* parser) {
    SourceSpan start_span = parser->previous.span;
    UfExpr* keys[256];
    UfExpr* values[256];
    size_t count = 0;

    if (!check(parser, UF_TOK_RBRACE)) {
        do {
            if (check(parser, UF_TOK_RBRACE)) break;
            if (count >= 256) {
                error_current(parser, "Cannot have more than 256 entries in map literal", NULL);
                break;
            }
            UfExpr* key = NULL;
            if (check(parser, UF_TOK_IDENTIFIER)) {
                advance(parser);
                key = uf_expr_literal_string(parser->arena, parser->previous.span, parser->previous.as.string_val);
            } else {
                key = uf_parse_expression(parser);
            }

            consume(parser, UF_TOK_COLON, "Expected ':' after map key", "Syntax: '{ key: value }'");
            UfExpr* val = uf_parse_expression(parser);

            keys[count] = key;
            values[count] = val;
            count++;
        } while (match(parser, UF_TOK_COMMA));
    }

    consume(parser, UF_TOK_RBRACE, "Expected '}' after map entries", "Close map literal with '}'");

    UfExpr** keys_copy = NULL;
    UfExpr** vals_copy = NULL;
    if (count > 0) {
        keys_copy = (UfExpr**)uf_arena_alloc(parser->arena, count * sizeof(UfExpr*));
        memcpy(keys_copy, keys, count * sizeof(UfExpr*));
        vals_copy = (UfExpr**)uf_arena_alloc(parser->arena, count * sizeof(UfExpr*));
        memcpy(vals_copy, values, count * sizeof(UfExpr*));
    }

    SourceSpan span = source_span_join(start_span, parser->previous.span);
    return uf_expr_map(parser->arena, span, keys_copy, vals_copy, count);
}

static UfExpr* parse_dot(UfParser* parser, UfExpr* left) {
    /* parser->previous is UF_TOK_DOT */
    consume(parser, UF_TOK_IDENTIFIER, "Expected property name after '.'", "Property names must be identifiers, e.g., obj.field");
    UfToken prop_tok = parser->previous;
    SourceSpan span = source_span_join(left->span, prop_tok.span);
    UfExpr* index = uf_expr_literal_string(parser->arena, prop_tok.span, prop_tok.as.string_val);
    return uf_expr_index(parser->arena, span, left, index);
}

static UfExpr* parse_function_expr(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    const char* fn_name = NULL;
    if (check(parser, UF_TOK_IDENTIFIER)) {
        advance(parser);
        fn_name = parser->previous.as.string_val;
    }

    consume(parser, UF_TOK_LPAREN, "Expected '(' after 'function'", NULL);

    const char* params[32];
    const char* param_types[32];
    size_t param_count = 0;

    if (!check(parser, UF_TOK_RPAREN)) {
        do {
            if (param_count >= 32) {
                error_current(parser, "Functions cannot have more than 32 parameters", NULL);
                break;
            }
            consume(parser, UF_TOK_IDENTIFIER, "Expected parameter name", NULL);
            params[param_count] = parser->previous.as.string_val;
            param_types[param_count] = NULL;
            if (match(parser, UF_TOK_COLON)) {
                consume(parser, UF_TOK_IDENTIFIER, "Expected parameter type after ':'", NULL);
                param_types[param_count] = parser->previous.as.string_val;
            }
            param_count++;
        } while (match(parser, UF_TOK_COMMA));
    }

    consume(parser, UF_TOK_RPAREN, "Expected ')' after parameters", NULL);
    consume(parser, UF_TOK_COLON, "Expected ':' after function signature", NULL);

    const char* return_type = NULL;
    if (check(parser, UF_TOK_IDENTIFIER) && peek(parser).kind == UF_TOK_COLON) {
        advance(parser);
        return_type = parser->previous.as.string_val;
        consume(parser, UF_TOK_COLON, "Expected ':' after return type", NULL);
    }

    UfStmt* body = NULL;
    if (match(parser, UF_TOK_NEWLINE)) {
        consume(parser, UF_TOK_INDENT, "Expected indented block", "Indent the body of the function with 4 spaces");
        UfStmt* stmts[256];
        size_t count = 0;
        while (!check(parser, UF_TOK_DEDENT) && !check(parser, UF_TOK_EOF)) {
            if (match(parser, UF_TOK_NEWLINE)) continue;
            if (count >= 256) {
                error_current(parser, "Block exceeds maximum statement limit (256)", NULL);
                break;
            }
            UfStmt* stmt = parse_statement(parser);
            if (stmt) stmts[count++] = stmt;
        }
        consume(parser, UF_TOK_DEDENT, "Expected unindent to close block", NULL);
        UfStmt** stmts_copy = NULL;
        if (count > 0) {
            stmts_copy = (UfStmt**)uf_arena_alloc(parser->arena, count * sizeof(UfStmt*));
            memcpy(stmts_copy, stmts, count * sizeof(UfStmt*));
        }
        body = uf_stmt_block(parser->arena, source_span_make(start, parser->previous.span.end), stmts_copy, count);
    } else {
        SourceLoc stmt_start = parser->current.span.start;
        UfStmt* inner_stmt = NULL;
        if (match(parser, UF_TOK_RETURN)) {
            UfExpr* val = NULL;
            if (!check(parser, UF_TOK_COMMA) && !check(parser, UF_TOK_RPAREN) && !check(parser, UF_TOK_RBRACKET) &&
                !check(parser, UF_TOK_RBRACE) && !check(parser, UF_TOK_NEWLINE) && !check(parser, UF_TOK_EOF)) {
                val = uf_parse_expression(parser);
            }
            SourceSpan sspan = source_span_make(stmt_start, parser->previous.span.end);
            inner_stmt = uf_stmt_return(parser->arena, sspan, val);
        } else {
            UfExpr* val = uf_parse_expression(parser);
            SourceSpan sspan = source_span_make(stmt_start, parser->previous.span.end);
            inner_stmt = uf_stmt_return(parser->arena, sspan, val);
        }
        UfStmt** stmts_copy = (UfStmt**)uf_arena_alloc(parser->arena, sizeof(UfStmt*));
        stmts_copy[0] = inner_stmt;
        body = uf_stmt_block(parser->arena, inner_stmt->span, stmts_copy, 1);
    }

    SourceSpan span = source_span_make(start, body->span.end);
    const char** params_copy = NULL;
    const char** param_types_copy = NULL;
    if (param_count > 0) {
        params_copy = (const char**)uf_arena_alloc(parser->arena, param_count * sizeof(const char*));
        memcpy(params_copy, params, param_count * sizeof(const char*));

        param_types_copy = (const char**)uf_arena_alloc(parser->arena, param_count * sizeof(const char*));
        memcpy(param_types_copy, param_types, param_count * sizeof(const char*));
    }

    return uf_expr_function(parser->arena, span, fn_name, params_copy, param_types_copy, param_count, return_type, body);
}

static const ParseRule rules[] = {
    [UF_TOK_EOF]        = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_ERROR]      = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_NEWLINE]    = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_INDENT]     = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_DEDENT]     = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_IDENTIFIER] = { parse_identifier,     NULL,         PREC_NONE },
    [UF_TOK_NUMBER]     = { parse_literal,        NULL,         PREC_NONE },
    [UF_TOK_STRING]     = { parse_literal,        NULL,         PREC_NONE },
    [UF_TOK_LET]        = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_SAY]        = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_FUNCTION]   = { parse_function_expr,  NULL,         PREC_NONE },
    [UF_TOK_RETURN]     = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_IF]         = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_ELSE]       = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_WHILE]      = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_REPEAT]     = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_TIMES]      = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_FOR]        = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_IN]         = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_BREAK]      = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_CONTINUE]   = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_TRY]        = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_CATCH]      = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_IMPORT]     = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_FROM]       = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_AS]         = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_STRUCT]     = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_MATCH]      = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_WHEN]       = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_AND]        = { NULL,                 parse_binary, PREC_AND },
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
    [UF_TOK_LBRACKET]   = { parse_array,      parse_index,  PREC_CALL },
    [UF_TOK_RBRACKET]   = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_LBRACE]     = { parse_map,        NULL,         PREC_NONE },
    [UF_TOK_RBRACE]     = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_DOT]        = { NULL,             parse_dot,    PREC_CALL },
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

static UfStmt* parse_block_body(UfParser* parser, SourceLoc start) {
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

static UfStmt* parse_block(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    consume(parser, UF_TOK_COLON, "Expected ':' before block", "Add ':' after the statement header");
    return parse_block_body(parser, start);
}

static UfStmt* parse_let_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    consume(parser, UF_TOK_IDENTIFIER, "Expected variable name after 'let'", "Provide an identifier name, e.g., 'let count = 0'");
    const char* name = parser->previous.as.string_val;

    const char* type_annotation = NULL;
    if (match(parser, UF_TOK_COLON)) {
        consume(parser, UF_TOK_IDENTIFIER, "Expected type name after ':' in type annotation", "Valid types include Number, String, Boolean, Array, Map, Function, Null, Any");
        type_annotation = parser->previous.as.string_val;
    }

    UfExpr* init = NULL;
    if (match(parser, UF_TOK_EQUAL)) {
        init = uf_parse_expression(parser);
    }

    if (check(parser, UF_TOK_NEWLINE)) {
        advance(parser);
    } else if (!init || init->kind != UF_EXPR_FUNCTION || parser->previous.kind != UF_TOK_DEDENT) {
        consume(parser, UF_TOK_NEWLINE, "Expected newline after variable declaration", NULL);
    }
    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_let(parser->arena, span, name, type_annotation, init);
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

static UfStmt* parse_for_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    consume(parser, UF_TOK_IDENTIFIER, "Expected loop variable name after 'for'", "Syntax: 'for <item> in <collection>:'");
    const char* var_name = parser->previous.as.string_val;

    consume(parser, UF_TOK_IN, "Expected 'in' after loop variable", "Syntax: 'for <item> in <collection>:'");
    UfExpr* iterable = uf_parse_expression(parser);

    UfStmt* body = parse_block(parser);
    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_for(parser->arena, span, var_name, iterable, body);
}

static UfStmt* parse_break_statement(UfParser* parser) {
    SourceSpan span = parser->previous.span;
    consume(parser, UF_TOK_NEWLINE, "Expected newline after 'break'", NULL);
    return uf_stmt_break(parser->arena, span);
}

static UfStmt* parse_continue_statement(UfParser* parser) {
    SourceSpan span = parser->previous.span;
    consume(parser, UF_TOK_NEWLINE, "Expected newline after 'continue'", NULL);
    return uf_stmt_continue(parser->arena, span);
}

static UfStmt* parse_function_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    consume(parser, UF_TOK_IDENTIFIER, "Expected function name", "Syntax: 'function <name>(<parameters>):'");
    const char* fn_name = parser->previous.as.string_val;

    consume(parser, UF_TOK_LPAREN, "Expected '(' after function name", NULL);

    const char* params[32];
    const char* param_types[32];
    size_t param_count = 0;

    if (!check(parser, UF_TOK_RPAREN)) {
        do {
            if (param_count >= 32) {
                error_current(parser, "Functions cannot have more than 32 parameters", NULL);
                break;
            }
            consume(parser, UF_TOK_IDENTIFIER, "Expected parameter name", NULL);
            params[param_count] = parser->previous.as.string_val;
            param_types[param_count] = NULL;
            if (match(parser, UF_TOK_COLON)) {
                consume(parser, UF_TOK_IDENTIFIER, "Expected parameter type after ':'", NULL);
                param_types[param_count] = parser->previous.as.string_val;
            }
            param_count++;
        } while (match(parser, UF_TOK_COMMA));
    }

    consume(parser, UF_TOK_RPAREN, "Expected ')' after parameters", NULL);
    consume(parser, UF_TOK_COLON, "Expected ':' after function signature", NULL);

    const char* return_type = NULL;
    if (check(parser, UF_TOK_IDENTIFIER) && peek(parser).kind == UF_TOK_COLON) {
        advance(parser);
        return_type = parser->previous.as.string_val;
        consume(parser, UF_TOK_COLON, "Expected ':' after return type", NULL);
    }

    UfStmt* body = parse_block_body(parser, start);
    SourceSpan span = source_span_make(start, parser->previous.span.end);

    const char** params_copy = NULL;
    const char** param_types_copy = NULL;
    if (param_count > 0) {
        params_copy = (const char**)uf_arena_alloc(parser->arena, param_count * sizeof(const char*));
        memcpy(params_copy, params, param_count * sizeof(const char*));

        param_types_copy = (const char**)uf_arena_alloc(parser->arena, param_count * sizeof(const char*));
        memcpy(param_types_copy, param_types, param_count * sizeof(const char*));
    }

    return uf_stmt_function(parser->arena, span, fn_name, params_copy, param_types_copy, param_count, return_type, body);
}

static UfStmt* parse_expression_or_assignment_statement(UfParser* parser) {
    SourceLoc start = parser->current.span.start;
    UfExpr* expr = uf_parse_expression(parser);

    if (match(parser, UF_TOK_EQUAL)) {
        if (expr && expr->kind == UF_EXPR_IDENTIFIER) {
            const char* id_name = expr->as.identifier_name;
            UfExpr* val = uf_parse_expression(parser);
            consume(parser, UF_TOK_NEWLINE, "Expected newline after assignment", NULL);
            SourceSpan span = source_span_make(start, parser->previous.span.end);
            return uf_stmt_assign(parser->arena, span, id_name, val);
        } else if (expr && expr->kind == UF_EXPR_INDEX) {
            UfExpr* val = uf_parse_expression(parser);
            consume(parser, UF_TOK_NEWLINE, "Expected newline after assignment", NULL);
            SourceSpan span = source_span_make(start, parser->previous.span.end);
            return uf_stmt_index_assign(parser->arena, span, expr->as.index_expr.target, expr->as.index_expr.index, val);
        } else {
            error_at(parser, &parser->previous, "Invalid assignment target", "Left side of '=' must be a variable or index expression");
        }
    }

    consume(parser, UF_TOK_NEWLINE, "Expected newline after expression", NULL);
    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_expr(parser->arena, span, expr);
}

static UfStmt* parse_try_catch_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start; /* 'try' token */
    UfStmt* try_block = parse_block(parser);

    consume(parser, UF_TOK_CATCH, "Expected 'catch' after 'try' block", "Syntax: 'try:\\n    <block>\\ncatch <err>:\\n    <block>'");

    const char* catch_var = NULL;
    if (check(parser, UF_TOK_IDENTIFIER)) {
        advance(parser);
        catch_var = parser->previous.as.string_val;
    } else {
        error_current(parser, "Expected error variable name after 'catch'", "e.g., 'catch err:'");
    }

    UfStmt* catch_block = parse_block(parser);

    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_try_catch(parser->arena, span, try_block, catch_var, catch_block);
}

static UfStmt* parse_import_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start; /* 'import' */
    consume(parser, UF_TOK_IDENTIFIER, "Expected module name after 'import'", "e.g., 'import math'");
    const char* module_name = parser->previous.as.string_val;

    const char* alias = NULL;
    if (match(parser, UF_TOK_AS)) {
        consume(parser, UF_TOK_IDENTIFIER, "Expected alias name after 'as'", "e.g., 'import math as m'");
        alias = parser->previous.as.string_val;
    }

    consume(parser, UF_TOK_NEWLINE, "Expected newline after 'import' statement", NULL);
    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_import(parser->arena, span, module_name, alias);
}

static UfStmt* parse_from_import_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start; /* 'from' */
    consume(parser, UF_TOK_IDENTIFIER, "Expected module name after 'from'", "e.g., 'from math import sqrt'");
    const char* module_name = parser->previous.as.string_val;

    consume(parser, UF_TOK_IMPORT, "Expected 'import' after module name in 'from' statement", "Syntax: 'from <module> import <symbols>'");

    const char* symbols[64];
    const char* aliases[64];
    size_t count = 0;

    do {
        if (count >= 64) {
            error_current(parser, "Exceeded maximum imported symbols limit (64)", NULL);
            break;
        }
        consume(parser, UF_TOK_IDENTIFIER, "Expected symbol name to import", NULL);
        symbols[count] = parser->previous.as.string_val;
        aliases[count] = NULL;
        if (match(parser, UF_TOK_AS)) {
            consume(parser, UF_TOK_IDENTIFIER, "Expected alias name after 'as'", NULL);
            aliases[count] = parser->previous.as.string_val;
        }
        count++;
    } while (match(parser, UF_TOK_COMMA));

    consume(parser, UF_TOK_NEWLINE, "Expected newline after 'from ... import' statement", NULL);

    const char** symbols_copy = (const char**)uf_arena_alloc(parser->arena, count * sizeof(const char*));
    memcpy(symbols_copy, symbols, count * sizeof(const char*));
    const char** aliases_copy = (const char**)uf_arena_alloc(parser->arena, count * sizeof(const char*));
    memcpy(aliases_copy, aliases, count * sizeof(const char*));

    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_from_import(parser->arena, span, module_name, symbols_copy, aliases_copy, count);
}

static UfStmt* parse_struct_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    consume(parser, UF_TOK_IDENTIFIER, "Expected struct name after 'struct'", "Syntax: 'struct <Name>:'");
    const char* name = parser->previous.as.string_val;

    consume(parser, UF_TOK_COLON, "Expected ':' after struct name", "Syntax: 'struct <Name>:'");
    consume(parser, UF_TOK_NEWLINE, "Expected newline after ':' in struct declaration", NULL);
    consume(parser, UF_TOK_INDENT, "Expected indented block for struct fields", "Indent the fields of the struct with 4 spaces");

    const char* field_names[64];
    const char* field_types[64];
    size_t field_count = 0;

    while (!check(parser, UF_TOK_DEDENT) && !check(parser, UF_TOK_EOF)) {
        if (match(parser, UF_TOK_NEWLINE)) continue;

        if (field_count >= 64) {
            error_current(parser, "Struct exceeds maximum field limit (64)", NULL);
            break;
        }

        consume(parser, UF_TOK_IDENTIFIER, "Expected field name in struct declaration", NULL);
        field_names[field_count] = parser->previous.as.string_val;
        field_types[field_count] = NULL;

        if (match(parser, UF_TOK_COLON)) {
            consume(parser, UF_TOK_IDENTIFIER, "Expected type name after ':' for struct field", NULL);
            field_types[field_count] = parser->previous.as.string_val;
        }

        field_count++;

        if (!check(parser, UF_TOK_DEDENT) && !check(parser, UF_TOK_EOF)) {
            consume(parser, UF_TOK_NEWLINE, "Expected newline after struct field declaration", NULL);
        }
    }

    consume(parser, UF_TOK_DEDENT, "Expected unindent to close struct declaration", NULL);

    SourceSpan span = source_span_make(start, parser->previous.span.end);
    const char** field_names_copy = NULL;
    const char** field_types_copy = NULL;
    if (field_count > 0) {
        field_names_copy = (const char**)uf_arena_alloc(parser->arena, field_count * sizeof(const char*));
        memcpy(field_names_copy, field_names, field_count * sizeof(const char*));

        field_types_copy = (const char**)uf_arena_alloc(parser->arena, field_count * sizeof(const char*));
        memcpy(field_types_copy, field_types, field_count * sizeof(const char*));
    }

    return uf_stmt_struct(parser->arena, span, name, field_names_copy, field_types_copy, field_count);
}

static UfPattern* parse_pattern(UfParser* parser) {
    SourceLoc start = parser->current.span.start;

    /* Wildcard: _ */
    if (check(parser, UF_TOK_IDENTIFIER) && strcmp(parser->current.as.string_val, "_") == 0) {
        advance(parser);
        return uf_pattern_wildcard(parser->arena, source_span_make(start, parser->previous.span.end));
    }

    /* Literal null */
    if (match(parser, UF_TOK_NULL)) {
        UfExpr* lit = uf_expr_literal_null(parser->arena, parser->previous.span);
        return uf_pattern_literal(parser->arena, parser->previous.span, lit);
    }

    /* Literal bool */
    if (match(parser, UF_TOK_TRUE)) {
        UfExpr* lit = uf_expr_literal_bool(parser->arena, parser->previous.span, true);
        return uf_pattern_literal(parser->arena, parser->previous.span, lit);
    }
    if (match(parser, UF_TOK_FALSE)) {
        UfExpr* lit = uf_expr_literal_bool(parser->arena, parser->previous.span, false);
        return uf_pattern_literal(parser->arena, parser->previous.span, lit);
    }

    /* Literal number (or negative number: - <number>) */
    if (match(parser, UF_TOK_MINUS)) {
        consume(parser, UF_TOK_NUMBER, "Expected number after '-' in pattern", NULL);
        UfExpr* lit = uf_expr_literal_number(parser->arena, source_span_make(start, parser->previous.span.end), -parser->previous.as.number_val);
        return uf_pattern_literal(parser->arena, source_span_make(start, parser->previous.span.end), lit);
    }
    if (match(parser, UF_TOK_NUMBER)) {
        UfExpr* lit = uf_expr_literal_number(parser->arena, parser->previous.span, parser->previous.as.number_val);
        return uf_pattern_literal(parser->arena, parser->previous.span, lit);
    }

    /* Literal string */
    if (match(parser, UF_TOK_STRING)) {
        UfExpr* lit = uf_expr_literal_string(parser->arena, parser->previous.span, parser->previous.as.string_val);
        return uf_pattern_literal(parser->arena, parser->previous.span, lit);
    }

    /* Identifier: either variable binding or struct pattern Point(...) */
    if (match(parser, UF_TOK_IDENTIFIER)) {
        const char* name = parser->previous.as.string_val;
        if (match(parser, UF_TOK_LPAREN)) {
            /* Struct pattern */
            UfPattern* subpats[32];
            size_t subcount = 0;
            if (!check(parser, UF_TOK_RPAREN)) {
                do {
                    if (subcount >= 32) {
                        error_current(parser, "Struct pattern exceeds 32 fields", NULL);
                        break;
                    }
                    subpats[subcount++] = parse_pattern(parser);
                } while (match(parser, UF_TOK_COMMA));
            }
            consume(parser, UF_TOK_RPAREN, "Expected ')' after struct pattern fields", NULL);
            SourceSpan span = source_span_make(start, parser->previous.span.end);
            UfPattern** subpats_copy = NULL;
            if (subcount > 0) {
                subpats_copy = (UfPattern**)uf_arena_alloc(parser->arena, subcount * sizeof(UfPattern*));
                memcpy(subpats_copy, subpats, subcount * sizeof(UfPattern*));
            }
            return uf_pattern_struct(parser->arena, span, name, subpats_copy, subcount);
        } else {
            /* Variable pattern */
            return uf_pattern_variable(parser->arena, parser->previous.span, name);
        }
    }

    error_current(parser, "Expected pattern", "Patterns can be literals, variables, '_', or 'Struct(fields...)'");
    return uf_pattern_wildcard(parser->arena, parser->current.span);
}

static UfStmt* parse_match_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    UfExpr* expr = uf_parse_expression(parser);
    consume(parser, UF_TOK_COLON, "Expected ':' after match expression", "Syntax: 'match <expression>:'");
    consume(parser, UF_TOK_NEWLINE, "Expected newline after ':'", NULL);
    consume(parser, UF_TOK_INDENT, "Expected indented block for match arms", "Indent match arms with 4 spaces");

    UfMatchArm arms[64];
    size_t arm_count = 0;
    UfStmt* else_branch = NULL;

    while (!check(parser, UF_TOK_DEDENT) && !check(parser, UF_TOK_EOF)) {
        if (match(parser, UF_TOK_NEWLINE)) continue;

        if (match(parser, UF_TOK_WHEN)) {
            SourceLoc arm_start = parser->previous.span.start;
            if (arm_count >= 64) {
                error_current(parser, "Match statement exceeds maximum 64 arms", NULL);
                break;
            }
            UfPattern* pat = parse_pattern(parser);
            UfExpr* guard = NULL;
            if (match(parser, UF_TOK_IF)) {
                guard = uf_parse_expression(parser);
            }
            UfStmt* body = parse_block(parser);
            SourceSpan arm_span = source_span_make(arm_start, parser->previous.span.end);
            arms[arm_count].pattern = pat;
            arms[arm_count].guard = guard;
            arms[arm_count].body = body;
            arms[arm_count].span = arm_span;
            arm_count++;
        } else if (match(parser, UF_TOK_ELSE)) {
            if (else_branch != NULL) {
                error_current(parser, "Duplicate 'else' in match statement", NULL);
            }
            else_branch = parse_block(parser);
        } else {
            error_current(parser, "Expected 'when' or 'else' in match block", "Syntax: 'when <pattern> [if <guard>]:' or 'else:'");
            break;
        }
    }

    consume(parser, UF_TOK_DEDENT, "Expected unindent to close match statement", NULL);
    SourceSpan span = source_span_make(start, parser->previous.span.end);

    UfMatchArm* arms_copy = NULL;
    if (arm_count > 0) {
        arms_copy = (UfMatchArm*)uf_arena_alloc(parser->arena, arm_count * sizeof(UfMatchArm));
        memcpy(arms_copy, arms, arm_count * sizeof(UfMatchArm));
    }

    return uf_stmt_match(parser->arena, span, expr, arms_copy, arm_count, else_branch);
}

static UfStmt* parse_statement(UfParser* parser) {
    if (match(parser, UF_TOK_LET))      return parse_let_statement(parser);
    if (match(parser, UF_TOK_SAY))      return parse_say_statement(parser);
    if (match(parser, UF_TOK_RETURN))   return parse_return_statement(parser);
    if (match(parser, UF_TOK_IF))       return parse_if_statement(parser);
    if (match(parser, UF_TOK_WHILE))    return parse_while_statement(parser);
    if (match(parser, UF_TOK_REPEAT))   return parse_repeat_statement(parser);
    if (match(parser, UF_TOK_FOR))      return parse_for_statement(parser);
    if (match(parser, UF_TOK_BREAK))    return parse_break_statement(parser);
    if (match(parser, UF_TOK_CONTINUE)) return parse_continue_statement(parser);
    if (match(parser, UF_TOK_TRY))      return parse_try_catch_statement(parser);
    if (match(parser, UF_TOK_IMPORT))   return parse_import_statement(parser);
    if (match(parser, UF_TOK_FROM))     return parse_from_import_statement(parser);
    if (match(parser, UF_TOK_FUNCTION)) return parse_function_statement(parser);
    if (match(parser, UF_TOK_STRUCT))   return parse_struct_statement(parser);
    if (match(parser, UF_TOK_MATCH))    return parse_match_statement(parser);

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
    parser->has_peek = false;

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
