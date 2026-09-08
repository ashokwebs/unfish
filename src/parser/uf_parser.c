#include "uf_parser.h"

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  /* = */
    PREC_PIPE,        /* |> */
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
static UfPattern* parse_pattern(UfParser* parser);
static UfPattern* expr_to_pattern(UfArena* arena, const UfExpr* expr);

static inline UfToken peek(UfParser* parser) {
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

static UfExpr* parse_fstring(UfParser* parser) {
    UfToken tok = parser->previous;
    const char* raw = tok.as.string_val ? tok.as.string_val : "";
    SourceSpan fstring_span = tok.span;

    UfExpr* parts[256];
    size_t count = 0;

    UfStrBuf lit;
    uf_strbuf_init(&lit);

    const char* p = raw;
    while (*p) {
        if (*p == '{') {
            if (p[1] == '{') {
                uf_strbuf_append_char(&lit, '{');
                p += 2;
                continue;
            }

            if (lit.length > 0) {
                char* s = uf_arena_strdup(parser->arena, lit.data ? lit.data : "");
                if (count < 256) {
                    parts[count++] = uf_expr_literal_string(parser->arena, fstring_span, s);
                }
                uf_strbuf_reset(&lit);
            }

            p++;
            const char* expr_start = p;
            int depth = 1;
            bool in_str = false;
            while (*p && depth > 0) {
                if (*p == '"') {
                    in_str = !in_str;
                } else if (!in_str) {
                    if (*p == '{') {
                        depth++;
                    } else if (*p == '}') {
                        depth--;
                        if (depth == 0) break;
                    }
                } else if (in_str && *p == '\\' && p[1] != '\0') {
                    p++;
                }
                p++;
            }

            if (depth != 0) {
                uf_strbuf_free(&lit);
                error_at(parser, &parser->previous, "Unclosed '{' in f-string", "Ensure every '{' has a matching '}'");
                return NULL;
            }

            const char* expr_end = p;
            size_t expr_len = (size_t)(expr_end - expr_start);
            char* expr_buf = (char*)malloc(expr_len + 1);
            memcpy(expr_buf, expr_start, expr_len);
            expr_buf[expr_len] = '\0';
            p++;

            UfLexer sub_lexer;
            uf_lexer_init(&sub_lexer, fstring_span.start.file, expr_buf, parser->arena,
                          parser->lexer ? parser->lexer->interner : NULL, parser->reporter);
            UfParser sub_parser;
            uf_parser_init(&sub_parser, &sub_lexer, parser->arena, parser->reporter);
            UfExpr* sub_expr = uf_parse_expression(&sub_parser);
            free(expr_buf);

            if (sub_expr && count < 256) {
                parts[count++] = sub_expr;
            }
        } else if (*p == '}') {
            if (p[1] == '}') {
                uf_strbuf_append_char(&lit, '}');
                p += 2;
                continue;
            }
            uf_strbuf_free(&lit);
            error_at(parser, &parser->previous, "Single '}' not allowed in f-string", "Use '}}' to produce a literal '}'");
            return NULL;
        } else if (*p == '\\') {
            p++;
            if (*p) {
                switch (*p) {
                    case 'n': uf_strbuf_append_char(&lit, '\n'); break;
                    case 't': uf_strbuf_append_char(&lit, '\t'); break;
                    case '"': uf_strbuf_append_char(&lit, '"'); break;
                    case '\\': uf_strbuf_append_char(&lit, '\\'); break;
                    default:
                        uf_strbuf_append_char(&lit, '\\');
                        uf_strbuf_append_char(&lit, *p);
                        break;
                }
                p++;
            }
        } else {
            uf_strbuf_append_char(&lit, *p);
            p++;
        }
    }

    if (lit.length > 0 || count == 0) {
        char* s = uf_arena_strdup(parser->arena, lit.data ? lit.data : "");
        if (count < 256) {
            parts[count++] = uf_expr_literal_string(parser->arena, fstring_span, s);
        }
    }
    uf_strbuf_free(&lit);

    UfExpr** final_parts = (UfExpr**)uf_arena_alloc(parser->arena, sizeof(UfExpr*) * (count > 0 ? count : 1));
    for (size_t i = 0; i < count; ++i) {
        final_parts[i] = parts[i];
    }

    return uf_expr_string_interp(parser->arena, fstring_span, final_parts, count);
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
    if (!left) return NULL;
    UfToken op_tok = parser->previous;
    const ParseRule* rule = get_rule(op_tok.kind);
    /* Left-associative operators pass precedence + 1 */
    UfExpr* right = parse_precedence(parser, (Precedence)(rule->precedence + 1));
    SourceSpan span = source_span_join(left->span, right ? right->span : op_tok.span);
    return uf_expr_binary(parser->arena, span, op_tok.kind, left, right);
}

static UfExpr* parse_pipe(UfParser* parser, UfExpr* left) {
    if (!left) return NULL;
    UfToken pipe_tok = parser->previous; /* UF_TOK_PIPE_RIGHT */
    UfExpr* right = parse_precedence(parser, PREC_CALL);
    if (!right) {
        error_at(parser, &pipe_tok, "Expected function or call expression after '|>'", "Provide a function to pipe into, e.g., 'x |> f' or 'x |> f(y)'");
        return NULL;
    }

    SourceSpan span = source_span_join(left->span, right->span);

    if (right->kind == UF_EXPR_CALL) {
        /* Desugar: left |> f(a, b) -> f(left, a, b) */
        size_t old_argc = right->as.call.argc;
        size_t new_argc = old_argc + 1;
        UfExpr** new_args = (UfExpr**)uf_arena_alloc(parser->arena, new_argc * sizeof(UfExpr*));
        new_args[0] = left;
        for (size_t i = 0; i < old_argc; i++) {
            new_args[i + 1] = right->as.call.args[i];
        }
        return uf_expr_call(parser->arena, span, right->as.call.callee, new_args, new_argc);
    } else {
        /* Desugar: left |> f -> f(left) */
        UfExpr** new_args = (UfExpr**)uf_arena_alloc(parser->arena, sizeof(UfExpr*));
        new_args[0] = left;
        return uf_expr_call(parser->arena, span, right, new_args, 1);
    }
}

static UfExpr* parse_call(UfParser* parser, UfExpr* left) {
    if (!left) return NULL;
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

typedef struct {
    const char* var_name;
    UfExpr* iterable;
    UfExpr* condition;
} UfCompClause;

static UfExpr* parse_array(UfParser* parser) {
    SourceSpan start_span = parser->previous.span;
    if (match(parser, UF_TOK_RBRACKET)) {
        SourceSpan span = source_span_join(start_span, parser->previous.span);
        return uf_expr_array(parser->arena, span, NULL, 0);
    }

    UfExpr* first = uf_parse_expression(parser);
    if (!first) return NULL;

    if (match(parser, UF_TOK_FOR)) {
        /* List comprehension: [first for var in iter (if cond)...] */
        static uint64_t s_list_comp_id = 0;
        char res_buf[32];
        snprintf(res_buf, sizeof(res_buf), "__uf_lcomp_%llu", (unsigned long long)++s_list_comp_id);
        const char* res_name = uf_intern(parser->lexer->interner, res_buf, strlen(res_buf));

        UfCompClause clauses[8];
        size_t clause_count = 0;

        do {
            if (clause_count >= 8) {
                error_current(parser, "Comprehension exceeds maximum nested clauses (8)", NULL);
                break;
            }
            consume(parser, UF_TOK_IDENTIFIER, "Expected variable name after 'for'", "Syntax: '[expr for item in iterable]'");
            const char* var_name = parser->previous.as.string_val;
            consume(parser, UF_TOK_IN, "Expected 'in' after variable in comprehension", "Syntax: '[expr for item in iterable]'");
            UfExpr* iterable = uf_parse_expression(parser);
            UfExpr* condition = NULL;
            if (match(parser, UF_TOK_IF)) {
                condition = uf_parse_expression(parser);
            }
            clauses[clause_count].var_name = var_name;
            clauses[clause_count].iterable = iterable;
            clauses[clause_count].condition = condition;
            clause_count++;
        } while (match(parser, UF_TOK_FOR));

        consume(parser, UF_TOK_RBRACKET, "Expected ']' at end of list comprehension", "Close comprehension with ']'");
        SourceSpan comp_span = source_span_join(start_span, parser->previous.span);

        /* Desugar:
         * (function():
         *     let __uf_lcomp_N = []
         *     for ...:
         *         push(__uf_lcomp_N, first)
         *     return __uf_lcomp_N
         * )()
         */
        UfExpr* empty_arr = uf_expr_array(parser->arena, comp_span, NULL, 0);
        UfStmt* let_res = uf_stmt_let(parser->arena, comp_span, res_name, NULL, empty_arr);

        UfExpr* push_callee = uf_expr_identifier(parser->arena, comp_span, "push");
        UfExpr* res_ref = uf_expr_identifier(parser->arena, comp_span, res_name);
        UfExpr** push_args = (UfExpr**)uf_arena_alloc(parser->arena, 2 * sizeof(UfExpr*));
        push_args[0] = res_ref;
        push_args[1] = first;
        UfExpr* push_call = uf_expr_call(parser->arena, comp_span, push_callee, push_args, 2);
        UfStmt* inner_stmt = uf_stmt_expr(parser->arena, comp_span, push_call);

        for (int i = (int)clause_count - 1; i >= 0; i--) {
            if (clauses[i].condition) {
                UfStmt** if_stmts = (UfStmt**)uf_arena_alloc(parser->arena, sizeof(UfStmt*));
                if_stmts[0] = inner_stmt;
                UfStmt* if_body = uf_stmt_block(parser->arena, comp_span, if_stmts, 1);
                inner_stmt = uf_stmt_if(parser->arena, comp_span, clauses[i].condition, if_body, NULL);
            }
            UfStmt** for_stmts = (UfStmt**)uf_arena_alloc(parser->arena, sizeof(UfStmt*));
            for_stmts[0] = inner_stmt;
            UfStmt* for_body = uf_stmt_block(parser->arena, comp_span, for_stmts, 1);
            inner_stmt = uf_stmt_for(parser->arena, comp_span, clauses[i].var_name, clauses[i].iterable, for_body);
        }

        UfExpr* ret_ref = uf_expr_identifier(parser->arena, comp_span, res_name);
        UfStmt* ret_stmt = uf_stmt_return(parser->arena, comp_span, ret_ref);

        UfStmt** body_stmts = (UfStmt**)uf_arena_alloc(parser->arena, 3 * sizeof(UfStmt*));
        body_stmts[0] = let_res;
        body_stmts[1] = inner_stmt;
        body_stmts[2] = ret_stmt;
        UfStmt* fn_body = uf_stmt_block(parser->arena, comp_span, body_stmts, 3);

        UfExpr* fn_expr = uf_expr_function(parser->arena, comp_span, NULL, NULL, NULL, NULL, 0, 0, false, NULL, fn_body);
        return uf_expr_call(parser->arena, comp_span, fn_expr, NULL, 0);
    }

    /* Regular array literal */
    UfExpr* elements[256];
    elements[0] = first;
    size_t count = 1;

    while (match(parser, UF_TOK_COMMA)) {
        if (check(parser, UF_TOK_RBRACKET)) break;
        if (count >= 256) {
            error_current(parser, "Array literal exceeds 256 elements", NULL);
            break;
        }
        elements[count++] = uf_parse_expression(parser);
    }

    consume(parser, UF_TOK_RBRACKET, "Expected ']' after array elements", "Close array literal with ']'");

    UfExpr** elems_copy = (UfExpr**)uf_arena_alloc(parser->arena, count * sizeof(UfExpr*));
    memcpy(elems_copy, elements, count * sizeof(UfExpr*));

    SourceSpan span = source_span_join(start_span, parser->previous.span);
    return uf_expr_array(parser->arena, span, elems_copy, count);
}

static UfExpr* parse_index(UfParser* parser, UfExpr* left) {
    if (!left) return NULL;
    /* parser->previous is UF_TOK_LBRACKET */
    UfExpr* index = uf_parse_expression(parser);
    consume(parser, UF_TOK_RBRACKET, "Expected ']' after index", "Close index expression with ']'");
    SourceSpan span = source_span_join(left->span, parser->previous.span);
    return uf_expr_index(parser->arena, span, left, index);
}

static void parse_map_entry(UfParser* parser, UfExpr** out_key, UfExpr** out_val) {
    if (check(parser, UF_TOK_IDENTIFIER)) {
        UfToken next = peek(parser);
        if (next.kind == UF_TOK_COLON) {
            advance(parser);
            const char* id_name = parser->previous.as.string_val;
            SourceSpan id_span = parser->previous.span;
            *out_key = uf_expr_literal_string(parser->arena, id_span, id_name);
            advance(parser); /* consume ':' */
            *out_val = uf_parse_expression(parser);
            return;
        } else if (next.kind == UF_TOK_COMMA || next.kind == UF_TOK_RBRACE) {
            advance(parser);
            const char* id_name = parser->previous.as.string_val;
            SourceSpan id_span = parser->previous.span;
            *out_key = uf_expr_literal_string(parser->arena, id_span, id_name);
            *out_val = uf_expr_identifier(parser->arena, id_span, id_name);
            return;
        }
    }
    *out_key = uf_parse_expression(parser);
    consume(parser, UF_TOK_COLON, "Expected ':' after map key", "Syntax: '{ key: value }'");
    *out_val = uf_parse_expression(parser);
}

static UfExpr* parse_map(UfParser* parser) {
    SourceSpan start_span = parser->previous.span;
    if (match(parser, UF_TOK_RBRACE)) {
        SourceSpan span = source_span_join(start_span, parser->previous.span);
        return uf_expr_map(parser->arena, span, NULL, NULL, 0);
    }

    UfExpr* keys[256];
    UfExpr* values[256];
    size_t count = 0;

    /* Parse first entry */
    if (check(parser, UF_TOK_DOTDOTDOT)) {
        UfExpr* spread_expr = uf_parse_expression(parser);
        keys[count] = spread_expr;
        values[count] = NULL;
        count++;
    } else {
        UfExpr* key = NULL;
        UfExpr* val = NULL;
        parse_map_entry(parser, &key, &val);

        if (match(parser, UF_TOK_FOR)) {
            /* Map comprehension: {k: v for var in iter (if cond)...} */
            static uint64_t s_map_comp_id = 0;
            char res_buf[32];
            snprintf(res_buf, sizeof(res_buf), "__uf_mcomp_%llu", (unsigned long long)++s_map_comp_id);
            const char* res_name = uf_intern(parser->lexer->interner, res_buf, strlen(res_buf));

            UfCompClause clauses[8];
            size_t clause_count = 0;

            do {
                if (clause_count >= 8) {
                    error_current(parser, "Comprehension exceeds maximum nested clauses (8)", NULL);
                    break;
                }
                consume(parser, UF_TOK_IDENTIFIER, "Expected variable name after 'for'", "Syntax: '{k: v for item in iterable}'");
                const char* var_name = parser->previous.as.string_val;
                consume(parser, UF_TOK_IN, "Expected 'in' after variable in comprehension", "Syntax: '{k: v for item in iterable}'");
                UfExpr* iterable = uf_parse_expression(parser);
                UfExpr* condition = NULL;
                if (match(parser, UF_TOK_IF)) {
                    condition = uf_parse_expression(parser);
                }
                clauses[clause_count].var_name = var_name;
                clauses[clause_count].iterable = iterable;
                clauses[clause_count].condition = condition;
                clause_count++;
            } while (match(parser, UF_TOK_FOR));

            consume(parser, UF_TOK_RBRACE, "Expected '}' at end of map comprehension", "Close map comprehension with '}'");
            SourceSpan comp_span = source_span_join(start_span, parser->previous.span);

            /* Desugar:
             * (function():
             *     let __uf_mcomp_N = {}
             *     for ...:
             *         __uf_mcomp_N[key] = val
             *     return __uf_mcomp_N
             * )()
             */
            UfExpr* empty_map = uf_expr_map(parser->arena, comp_span, NULL, NULL, 0);
            UfStmt* let_res = uf_stmt_let(parser->arena, comp_span, res_name, NULL, empty_map);

            UfExpr* res_target = uf_expr_identifier(parser->arena, comp_span, res_name);
            UfStmt* inner_stmt = uf_stmt_index_assign(parser->arena, comp_span, res_target, key, val);

            for (int i = (int)clause_count - 1; i >= 0; i--) {
                if (clauses[i].condition) {
                    UfStmt** if_stmts = (UfStmt**)uf_arena_alloc(parser->arena, sizeof(UfStmt*));
                    if_stmts[0] = inner_stmt;
                    UfStmt* if_body = uf_stmt_block(parser->arena, comp_span, if_stmts, 1);
                    inner_stmt = uf_stmt_if(parser->arena, comp_span, clauses[i].condition, if_body, NULL);
                }
                UfStmt** for_stmts = (UfStmt**)uf_arena_alloc(parser->arena, sizeof(UfStmt*));
                for_stmts[0] = inner_stmt;
                UfStmt* for_body = uf_stmt_block(parser->arena, comp_span, for_stmts, 1);
                inner_stmt = uf_stmt_for(parser->arena, comp_span, clauses[i].var_name, clauses[i].iterable, for_body);
            }

            UfExpr* ret_ref = uf_expr_identifier(parser->arena, comp_span, res_name);
            UfStmt* ret_stmt = uf_stmt_return(parser->arena, comp_span, ret_ref);

            UfStmt** body_stmts = (UfStmt**)uf_arena_alloc(parser->arena, 3 * sizeof(UfStmt*));
            body_stmts[0] = let_res;
            body_stmts[1] = inner_stmt;
            body_stmts[2] = ret_stmt;
            UfStmt* fn_body = uf_stmt_block(parser->arena, comp_span, body_stmts, 3);

            UfExpr* fn_expr = uf_expr_function(parser->arena, comp_span, NULL, NULL, NULL, NULL, 0, 0, false, NULL, fn_body);
            return uf_expr_call(parser->arena, comp_span, fn_expr, NULL, 0);
        }

        keys[0] = key;
        values[0] = val;
        count = 1;
    }

    /* Regular map entries */
    while (match(parser, UF_TOK_COMMA)) {
        if (check(parser, UF_TOK_RBRACE)) break;
        if (count >= 256) {
            error_current(parser, "Cannot have more than 256 entries in map literal", NULL);
            break;
        }
        if (check(parser, UF_TOK_DOTDOTDOT)) {
            UfExpr* spread_expr = uf_parse_expression(parser);
            keys[count] = spread_expr;
            values[count] = NULL;
            count++;
        } else {
            UfExpr* key = NULL;
            UfExpr* val = NULL;
            parse_map_entry(parser, &key, &val);
            keys[count] = key;
            values[count] = val;
            count++;
        }
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

static bool is_identifier_or_keyword(UfTokenKind kind) {
    return kind == UF_TOK_IDENTIFIER || (kind >= UF_TOK_LET && kind <= UF_TOK_NULL);
}

static UfExpr* parse_dot(UfParser* parser, UfExpr* left) {
    if (!left) return NULL;
    /* parser->previous is UF_TOK_DOT */
    if (is_identifier_or_keyword(parser->current.kind)) {
        advance(parser);
    } else {
        consume(parser, UF_TOK_IDENTIFIER, "Expected property name after '.'", "Property names must be identifiers, e.g., obj.field");
        return NULL;
    }
    UfToken prop_tok = parser->previous;
    SourceSpan span = source_span_join(left->span, prop_tok.span);
    const char* prop_name = prop_tok.as.string_val;
    if (!prop_name && prop_tok.lexeme) {
        prop_name = uf_intern(parser->lexer->interner, prop_tok.lexeme, prop_tok.length);
    }
    UfExpr* index = uf_expr_literal_string(parser->arena, prop_tok.span, prop_name);
    return uf_expr_index(parser->arena, span, left, index);
}

static const char* parse_type_annotation(UfParser* parser) {
    if (!check(parser, UF_TOK_IDENTIFIER)) {
        error_current(parser, "Expected type name", "Valid types include Number, String, Boolean, Array, Map, Function, Null, Any");
        return NULL;
    }
    advance(parser);
    const char* base_type = parser->previous.as.string_val;

    /* Check for generic type arguments: <T1, T2> */
    if (match(parser, UF_TOK_LT)) {
        char buffer[256];
        size_t len = 0;
        len += snprintf(buffer + len, sizeof(buffer) - len, "%s<", base_type);

        bool first = true;
        while (!check(parser, UF_TOK_GT) && !check(parser, UF_TOK_EOF)) {
            if (!first) {
                consume(parser, UF_TOK_COMMA, "Expected ',' between type arguments", NULL);
                if (len < sizeof(buffer) - 3) {
                    len += snprintf(buffer + len, sizeof(buffer) - len, ", ");
                }
            }
            first = false;
            const char* inner = parse_type_annotation(parser);
            if (!inner) break;
            if (len < sizeof(buffer) - strlen(inner) - 2) {
                len += snprintf(buffer + len, sizeof(buffer) - len, "%s", inner);
            }
        }
        consume(parser, UF_TOK_GT, "Expected '>' to close type arguments", "Close type arguments with '>'");
        if (len < sizeof(buffer) - 2) {
            snprintf(buffer + len, sizeof(buffer) - len, ">");
        }
        return uf_arena_strdup(parser->arena, buffer);
    }

    return base_type;
}

static size_t parse_type_parameters(UfParser* parser, const char*** out_params, const char*** out_bounds) {
    *out_params = NULL;
    *out_bounds = NULL;
    if (!match(parser, UF_TOK_LT)) {
        return 0;
    }
    const char* params[16];
    const char* bounds[16];
    size_t count = 0;
    while (!check(parser, UF_TOK_GT) && !check(parser, UF_TOK_EOF)) {
        if (count >= 16) {
            error_current(parser, "Exceeded maximum of 16 type parameters", NULL);
            break;
        }
        consume(parser, UF_TOK_IDENTIFIER, "Expected type parameter name", "e.g., 'T' in '<T>'");
        params[count] = parser->previous.as.string_val;
        bounds[count] = NULL;
        if (match(parser, UF_TOK_COLON)) {
            consume(parser, UF_TOK_IDENTIFIER, "Expected trait bound name after ':'", "e.g., '<T: Describable>'");
            bounds[count] = parser->previous.as.string_val;
        }
        count++;
        if (!match(parser, UF_TOK_COMMA)) break;
    }
    consume(parser, UF_TOK_GT, "Expected '>' to close type parameters", "Close type parameters with '>'");

    if (count > 0) {
        const char** p_copy = (const char**)uf_arena_alloc(parser->arena, count * sizeof(const char*));
        const char** b_copy = (const char**)uf_arena_alloc(parser->arena, count * sizeof(const char*));
        memcpy(p_copy, params, count * sizeof(const char*));
        memcpy(b_copy, bounds, count * sizeof(const char*));
        *out_params = p_copy;
        *out_bounds = b_copy;
    }
    return count;
}

static UfExpr* parse_function_expr_async(UfParser* parser, bool is_async, SourceLoc start) {
    const char* fn_name = NULL;
    if (check(parser, UF_TOK_IDENTIFIER)) {
        advance(parser);
        fn_name = parser->previous.as.string_val;
    }

    consume(parser, UF_TOK_LPAREN, "Expected '(' after 'function'", NULL);

    const char* params[32];
    const char* param_types[32];
    UfExpr* param_defaults[32];
    size_t param_count = 0;
    size_t min_param_count = 0;
    bool has_defaults = false;
    bool has_rest = false;

    if (!check(parser, UF_TOK_RPAREN)) {
        do {
            if (check(parser, UF_TOK_RPAREN)) break;
            if (param_count >= 32) {
                error_current(parser, "Functions cannot have more than 32 parameters", NULL);
                break;
            }
            if (has_rest) {
                error_current(parser, "Rest parameter must be the last parameter", NULL);
                break;
            }
            bool is_rest = false;
            if (match(parser, UF_TOK_DOTDOTDOT)) {
                is_rest = true;
                has_rest = true;
            }
            consume(parser, UF_TOK_IDENTIFIER, "Expected parameter name", NULL);
            params[param_count] = parser->previous.as.string_val;
            param_types[param_count] = NULL;
            param_defaults[param_count] = NULL;
            if (match(parser, UF_TOK_COLON)) {
                param_types[param_count] = parse_type_annotation(parser);
            }
            if (is_rest) {
                if (match(parser, UF_TOK_EQUAL)) {
                    error_current(parser, "Rest parameter cannot have a default value", NULL);
                }
            } else if (match(parser, UF_TOK_EQUAL)) {
                param_defaults[param_count] = uf_parse_expression(parser);
                has_defaults = true;
            } else {
                if (has_defaults) {
                    error_current(parser, "Non-default parameter cannot follow a default parameter", "Specify a default value with '= value'");
                } else {
                    min_param_count++;
                }
            }
            param_count++;
        } while (match(parser, UF_TOK_COMMA));
    }

    consume(parser, UF_TOK_RPAREN, "Expected ')' after parameters", NULL);

    const char* return_type = NULL;
    if (match(parser, UF_TOK_MINUS)) {
        consume(parser, UF_TOK_GT, "Expected '>' after '-' in return type annotation", NULL);
        return_type = parse_type_annotation(parser);
    }

    consume(parser, UF_TOK_COLON, "Expected ':' after function signature", NULL);

    if (!return_type && check(parser, UF_TOK_IDENTIFIER) && (peek(parser).kind == UF_TOK_COLON || peek(parser).kind == UF_TOK_NEWLINE)) {
        return_type = parse_type_annotation(parser);
        match(parser, UF_TOK_COLON);
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
    UfExpr** param_defaults_copy = NULL;
    if (param_count > 0) {
        params_copy = (const char**)uf_arena_alloc(parser->arena, param_count * sizeof(const char*));
        memcpy(params_copy, params, param_count * sizeof(const char*));

        param_types_copy = (const char**)uf_arena_alloc(parser->arena, param_count * sizeof(const char*));
        memcpy(param_types_copy, param_types, param_count * sizeof(const char*));

        if (has_defaults) {
            param_defaults_copy = (UfExpr**)uf_arena_alloc(parser->arena, param_count * sizeof(UfExpr*));
            memcpy(param_defaults_copy, param_defaults, param_count * sizeof(UfExpr*));
        }
    }

    return uf_expr_function_async(parser->arena, span, fn_name, params_copy, param_types_copy, param_defaults_copy, param_count, min_param_count, has_rest, return_type, is_async, body);
}

static UfExpr* parse_function_expr(UfParser* parser) {
    return parse_function_expr_async(parser, false, parser->previous.span.start);
}

static UfExpr* parse_async_expr(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    if (match(parser, UF_TOK_FUNCTION)) {
        return parse_function_expr_async(parser, true, start);
    }
    if (check(parser, UF_TOK_IDENTIFIER) && parser->current.as.string_val &&
        strcmp(parser->current.as.string_val, "fn") == 0) {
        advance(parser);
        return parse_function_expr_async(parser, true, start);
    }
    error_current(parser, "Expected 'function' after 'async'", NULL);
    return NULL;
}

static UfExpr* parse_await(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    UfExpr* operand = parse_precedence(parser, PREC_UNARY);
    SourceSpan span = source_span_make(start, operand ? operand->span.end : parser->previous.span.end);
    return uf_expr_await(parser->arena, span, operand);
}

static UfExpr* parse_spread(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    UfExpr* operand = parse_precedence(parser, PREC_UNARY);
    SourceSpan span = source_span_make(start, operand ? operand->span.end : parser->previous.span.end);
    return uf_expr_spread(parser->arena, span, operand);
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
    [UF_TOK_FSTRING]    = { parse_fstring,        NULL,         PREC_NONE },
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
    [UF_TOK_ENUM]       = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_TRAIT]      = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_IMPL]       = { NULL,                 NULL,         PREC_NONE },
    [UF_TOK_ASYNC]      = { parse_async_expr,     NULL,         PREC_NONE },
    [UF_TOK_AWAIT]      = { parse_await,          NULL,         PREC_UNARY },
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
    [UF_TOK_DOTDOTDOT]  = { parse_spread,     NULL,         PREC_UNARY },
    [UF_TOK_PIPE]       = { NULL,             NULL,         PREC_NONE },
    [UF_TOK_PIPE_RIGHT] = { NULL,             parse_pipe,   PREC_PIPE },
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
    if (!expr) return NULL;

    while (precedence <= get_rule(parser->current.kind)->precedence) {
        advance(parser);
        InfixParseFn infix_rule = get_rule(parser->previous.kind)->infix;
        if (infix_rule) {
            expr = infix_rule(parser, expr);
            if (!expr) break;
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
        if (parser->panic_mode || !stmt) {
            synchronize(parser);
        } else {
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

    if (check(parser, UF_TOK_LBRACKET) || check(parser, UF_TOK_LBRACE)) {
        UfPattern* pat = parse_pattern(parser);
        consume(parser, UF_TOK_EQUAL, "Expected '=' after destructuring pattern", "Syntax: 'let [a, b] = arr' or 'let {x, y} = obj'");
        UfExpr* init = uf_parse_expression(parser);
        consume(parser, UF_TOK_NEWLINE, "Expected newline after variable declaration", NULL);
        SourceSpan span = source_span_make(start, parser->previous.span.end);
        return uf_stmt_let_pattern(parser->arena, span, pat, init);
    }

    consume(parser, UF_TOK_IDENTIFIER, "Expected variable name after 'let'", "Provide an identifier name, e.g., 'let count = 0'");
    const char* name = parser->previous.as.string_val;

    const char* type_annotation = NULL;
    if (match(parser, UF_TOK_COLON)) {
        type_annotation = parse_type_annotation(parser);
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

static UfStmt* parse_function_statement_async(UfParser* parser, bool is_async, SourceLoc start) {
    consume(parser, UF_TOK_IDENTIFIER, "Expected function name", "Syntax: 'function <name>(<parameters>):'");
    const char* fn_name = parser->previous.as.string_val;

    const char** type_params = NULL;
    const char** type_param_bounds = NULL;
    size_t type_param_count = parse_type_parameters(parser, &type_params, &type_param_bounds);

    consume(parser, UF_TOK_LPAREN, "Expected '(' after function name", NULL);

    const char* params[32];
    const char* param_types[32];
    UfExpr* param_defaults[32];
    size_t param_count = 0;
    size_t min_param_count = 0;
    bool has_defaults = false;
    bool has_rest = false;

    if (!check(parser, UF_TOK_RPAREN)) {
        do {
            if (check(parser, UF_TOK_RPAREN)) break;
            if (param_count >= 32) {
                error_current(parser, "Functions cannot have more than 32 parameters", NULL);
                break;
            }
            if (has_rest) {
                error_current(parser, "Rest parameter must be the last parameter", NULL);
                break;
            }
            bool is_rest = false;
            if (match(parser, UF_TOK_DOTDOTDOT)) {
                is_rest = true;
                has_rest = true;
            }
            consume(parser, UF_TOK_IDENTIFIER, "Expected parameter name", NULL);
            params[param_count] = parser->previous.as.string_val;
            param_types[param_count] = NULL;
            param_defaults[param_count] = NULL;
            if (match(parser, UF_TOK_COLON)) {
                param_types[param_count] = parse_type_annotation(parser);
            }
            if (is_rest) {
                if (match(parser, UF_TOK_EQUAL)) {
                    error_current(parser, "Rest parameter cannot have a default value", NULL);
                }
            } else if (match(parser, UF_TOK_EQUAL)) {
                param_defaults[param_count] = uf_parse_expression(parser);
                has_defaults = true;
            } else {
                if (has_defaults) {
                    error_current(parser, "Non-default parameter cannot follow a default parameter", "Specify a default value with '= value'");
                } else {
                    min_param_count++;
                }
            }
            param_count++;
        } while (match(parser, UF_TOK_COMMA));
    }

    consume(parser, UF_TOK_RPAREN, "Expected ')' after parameters", NULL);

    const char* return_type = NULL;
    if (match(parser, UF_TOK_MINUS)) {
        consume(parser, UF_TOK_GT, "Expected '>' after '-' in return type annotation", NULL);
        return_type = parse_type_annotation(parser);
    }

    consume(parser, UF_TOK_COLON, "Expected ':' after function signature", NULL);

    if (!return_type && check(parser, UF_TOK_IDENTIFIER) && (peek(parser).kind == UF_TOK_COLON || peek(parser).kind == UF_TOK_NEWLINE)) {
        return_type = parse_type_annotation(parser);
        match(parser, UF_TOK_COLON);
    }

    UfStmt* body = parse_block_body(parser, start);
    SourceSpan span = source_span_make(start, parser->previous.span.end);

    const char** params_copy = NULL;
    const char** param_types_copy = NULL;
    UfExpr** param_defaults_copy = NULL;
    if (param_count > 0) {
        params_copy = (const char**)uf_arena_alloc(parser->arena, param_count * sizeof(const char*));
        memcpy(params_copy, params, param_count * sizeof(const char*));

        param_types_copy = (const char**)uf_arena_alloc(parser->arena, param_count * sizeof(const char*));
        memcpy(param_types_copy, param_types, param_count * sizeof(const char*));

        if (has_defaults) {
            param_defaults_copy = (UfExpr**)uf_arena_alloc(parser->arena, param_count * sizeof(UfExpr*));
            memcpy(param_defaults_copy, param_defaults, param_count * sizeof(UfExpr*));
        }
    }

    return uf_stmt_function_async(parser->arena, span, fn_name, params_copy, param_types_copy, param_defaults_copy, param_count, min_param_count, has_rest, return_type, type_params, type_param_bounds, type_param_count, is_async, body);
}

static UfStmt* parse_function_statement(UfParser* parser) {
    return parse_function_statement_async(parser, false, parser->previous.span.start);
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
        } else if (expr && (expr->kind == UF_EXPR_ARRAY || expr->kind == UF_EXPR_MAP)) {
            UfPattern* pat = expr_to_pattern(parser->arena, expr);
            if (!pat) {
                error_at(parser, &parser->previous, "Invalid destructuring assignment target", "Left side of '=' must be a valid destructuring pattern");
            }
            UfExpr* val = uf_parse_expression(parser);
            consume(parser, UF_TOK_NEWLINE, "Expected newline after assignment", NULL);
            SourceSpan span = source_span_make(start, parser->previous.span.end);
            return uf_stmt_assign_pattern(parser->arena, span, pat, val);
        } else {
            error_at(parser, &parser->previous, "Invalid assignment target", "Left side of '=' must be a variable, index expression, or destructuring pattern");
        }
    }

    consume(parser, UF_TOK_NEWLINE, "Expected newline after expression", NULL);
    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_expr(parser->arena, span, expr);
}

static UfStmt* parse_try_catch_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start; /* 'try' token */
    UfStmt* try_block = parse_block(parser);

    const char* catch_var = NULL;
    UfStmt* catch_block = NULL;
    UfStmt* finally_block = NULL;

    if (match(parser, UF_TOK_CATCH)) {
        if (check(parser, UF_TOK_IDENTIFIER)) {
            advance(parser);
            catch_var = parser->previous.as.string_val;
        } else {
            error_current(parser, "Expected error variable name after 'catch'", "e.g., 'catch err:'");
        }
        catch_block = parse_block(parser);
    }

    if (match(parser, UF_TOK_FINALLY)) {
        finally_block = parse_block(parser);
    }

    if (!catch_block && !finally_block) {
        error_current(parser, "Expected 'catch' or 'finally' after 'try' block", "Syntax: 'try:\\n    <block>\\ncatch <err>:\\n    <block>\\nfinally:\\n    <block>'");
    }

    SourceSpan span = source_span_make(start, parser->previous.span.end);
    return uf_stmt_try_catch(parser->arena, span, try_block, catch_var, catch_block, finally_block);
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

    const char** type_params = NULL;
    const char** type_param_bounds = NULL;
    size_t type_param_count = parse_type_parameters(parser, &type_params, &type_param_bounds);

    consume(parser, UF_TOK_COLON, "Expected ':' after struct name", "Syntax: 'struct <Name>:'");
    consume(parser, UF_TOK_NEWLINE, "Expected newline after ':' in struct declaration", NULL);
    consume(parser, UF_TOK_INDENT, "Expected indented block for struct fields", "Indent the fields of the struct with 4 spaces");

    const char* field_names[64];
    const char* field_types[64];
    size_t field_count = 0;
    UfStmt* methods[64];
    size_t method_count = 0;
    UfStmt* impl_blocks[32];
    size_t impl_block_count = 0;

    while (!check(parser, UF_TOK_DEDENT) && !check(parser, UF_TOK_EOF)) {
        if (match(parser, UF_TOK_NEWLINE)) continue;

        if (match(parser, UF_TOK_IMPL)) {
            SourceLoc impl_start = parser->previous.span.start;
            const char* trait_name = parse_type_annotation(parser);
            consume(parser, UF_TOK_COLON, "Expected ':' after trait name in impl block", NULL);
            consume(parser, UF_TOK_NEWLINE, "Expected newline after ':' in impl block", NULL);
            consume(parser, UF_TOK_INDENT, "Expected indented block for impl methods", NULL);

            UfStmt* impl_methods[32];
            size_t impl_method_count = 0;

            while (!check(parser, UF_TOK_DEDENT) && !check(parser, UF_TOK_EOF)) {
                if (match(parser, UF_TOK_NEWLINE)) continue;
                bool is_async_m = false;
                SourceLoc m_start = parser->current.span.start;
                if (match(parser, UF_TOK_ASYNC)) is_async_m = true;
                if (match(parser, UF_TOK_FUNCTION) ||
                    (check(parser, UF_TOK_IDENTIFIER) && parser->current.as.string_val &&
                     strcmp(parser->current.as.string_val, "fn") == 0 &&
                     peek(parser).kind == UF_TOK_IDENTIFIER && (advance(parser), true))) {
                    if (impl_method_count >= 32) {
                        error_current(parser, "Method limit exceeded in impl block", NULL);
                        break;
                    }
                    UfStmt* mstmt = parse_function_statement_async(parser, is_async_m, m_start);
                    impl_methods[impl_method_count++] = mstmt;
                    continue;
                }
                if (is_async_m) {
                    error_current(parser, "Expected 'function' after 'async'", NULL);
                    break;
                }
                error_current(parser, "Expected method definition in impl block", NULL);
                break;
            }
            consume(parser, UF_TOK_DEDENT, "Expected unindent to close impl block", NULL);

            SourceSpan impl_span = source_span_make(impl_start, parser->previous.span.end);
            UfStmt** impl_methods_copy = NULL;
            if (impl_method_count > 0) {
                impl_methods_copy = (UfStmt**)uf_arena_alloc(parser->arena, impl_method_count * sizeof(UfStmt*));
                memcpy(impl_methods_copy, impl_methods, impl_method_count * sizeof(UfStmt*));
            }
            if (impl_block_count < 32) {
                impl_blocks[impl_block_count++] = uf_stmt_impl(parser->arena, impl_span, trait_name, name, impl_methods_copy, impl_method_count);
            }
            continue;
        }

        bool is_struct_m_async = false;
        SourceLoc sm_start = parser->current.span.start;
        if (match(parser, UF_TOK_ASYNC)) is_struct_m_async = true;

        if (match(parser, UF_TOK_FUNCTION) ||
            (check(parser, UF_TOK_IDENTIFIER) && parser->current.as.string_val &&
             strcmp(parser->current.as.string_val, "fn") == 0 &&
             peek(parser).kind == UF_TOK_IDENTIFIER && (advance(parser), true))) {
            if (method_count >= 64) {
                error_current(parser, "Struct exceeds maximum method limit (64)", NULL);
                break;
            }
            methods[method_count++] = parse_function_statement_async(parser, is_struct_m_async, sm_start);
            continue;
        } else if (is_struct_m_async) {
            error_current(parser, "Expected 'function' after 'async'", NULL);
            break;
        }

        if (field_count >= 64) {
            error_current(parser, "Struct exceeds maximum field limit (64)", NULL);
            break;
        }

        consume(parser, UF_TOK_IDENTIFIER, "Expected field name or method in struct declaration", NULL);
        field_names[field_count] = parser->previous.as.string_val;
        field_types[field_count] = NULL;

        if (match(parser, UF_TOK_COLON)) {
            field_types[field_count] = parse_type_annotation(parser);
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

    UfStmt** methods_copy = NULL;
    if (method_count > 0) {
        methods_copy = (UfStmt**)uf_arena_alloc(parser->arena, method_count * sizeof(UfStmt*));
        memcpy(methods_copy, methods, method_count * sizeof(UfStmt*));
    }

    UfStmt** impl_blocks_copy = NULL;
    if (impl_block_count > 0) {
        impl_blocks_copy = (UfStmt**)uf_arena_alloc(parser->arena, impl_block_count * sizeof(UfStmt*));
        memcpy(impl_blocks_copy, impl_blocks, impl_block_count * sizeof(UfStmt*));
    }

    return uf_stmt_struct_with_generics(parser->arena, span, name, field_names_copy, field_types_copy, field_count, methods_copy, method_count, impl_blocks_copy, impl_block_count, type_params, type_param_bounds, type_param_count);
}

static UfStmt* parse_trait_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    consume(parser, UF_TOK_IDENTIFIER, "Expected trait name after 'trait'", "Syntax: 'trait <Name>:'");
    const char* name = parser->previous.as.string_val;

    const char** type_params = NULL;
    const char** type_param_bounds = NULL;
    size_t type_param_count = parse_type_parameters(parser, &type_params, &type_param_bounds);

    consume(parser, UF_TOK_COLON, "Expected ':' after trait name", "Syntax: 'trait <Name>:'");
    consume(parser, UF_TOK_NEWLINE, "Expected newline after ':' in trait declaration", NULL);
    consume(parser, UF_TOK_INDENT, "Expected indented block for trait methods", "Indent method signatures with 4 spaces");

    const char* method_names[32];
    size_t method_param_counts[32];
    const char** method_param_names[32];
    const char** method_param_types[32];
    const char* method_return_types[32];
    size_t method_count = 0;

    while (!check(parser, UF_TOK_DEDENT) && !check(parser, UF_TOK_EOF)) {
        if (match(parser, UF_TOK_NEWLINE)) continue;

        if (match(parser, UF_TOK_FUNCTION) ||
            (check(parser, UF_TOK_IDENTIFIER) && parser->current.as.string_val &&
             strcmp(parser->current.as.string_val, "fn") == 0 &&
             peek(parser).kind == UF_TOK_IDENTIFIER && (advance(parser), true))) {
            if (method_count >= 32) {
                error_current(parser, "Trait exceeds maximum method limit (32)", NULL);
                break;
            }
            consume(parser, UF_TOK_IDENTIFIER, "Expected method name in trait", NULL);
            const char* mname = parser->previous.as.string_val;

            consume(parser, UF_TOK_LPAREN, "Expected '(' after method name", NULL);

            const char* pnames[32];
            const char* ptypes[32];
            size_t pcount = 0;

            if (!check(parser, UF_TOK_RPAREN)) {
                do {
                    if (check(parser, UF_TOK_RPAREN)) break;
                    if (pcount >= 32) {
                        error_current(parser, "Method cannot have more than 32 parameters", NULL);
                        break;
                    }
                    consume(parser, UF_TOK_IDENTIFIER, "Expected parameter name", NULL);
                    pnames[pcount] = parser->previous.as.string_val;
                    ptypes[pcount] = NULL;
                    if (match(parser, UF_TOK_COLON)) {
                        ptypes[pcount] = parse_type_annotation(parser);
                    }
                    pcount++;
                } while (match(parser, UF_TOK_COMMA));
            }

            consume(parser, UF_TOK_RPAREN, "Expected ')' after parameters", NULL);

            const char* ret_type = NULL;
            if (match(parser, UF_TOK_MINUS)) {
                consume(parser, UF_TOK_GT, "Expected '>' after '-' in return type annotation", NULL);
                ret_type = parse_type_annotation(parser);
            } else if (match(parser, UF_TOK_COLON)) {
                if (check(parser, UF_TOK_IDENTIFIER)) {
                    ret_type = parse_type_annotation(parser);
                }
            }

            consume(parser, UF_TOK_NEWLINE, "Expected newline after method signature in trait", NULL);

            method_names[method_count] = mname;
            method_param_counts[method_count] = pcount;
            method_return_types[method_count] = ret_type;

            const char** pnames_copy = NULL;
            const char** ptypes_copy = NULL;
            if (pcount > 0) {
                pnames_copy = (const char**)uf_arena_alloc(parser->arena, pcount * sizeof(const char*));
                memcpy(pnames_copy, pnames, pcount * sizeof(const char*));
                ptypes_copy = (const char**)uf_arena_alloc(parser->arena, pcount * sizeof(const char*));
                memcpy(ptypes_copy, ptypes, pcount * sizeof(const char*));
            }
            method_param_names[method_count] = pnames_copy;
            method_param_types[method_count] = ptypes_copy;
            method_count++;
            continue;
        }

        error_current(parser, "Expected method signature in trait declaration", "Syntax: 'function <name>(<params>): <ReturnType>'");
        break;
    }

    consume(parser, UF_TOK_DEDENT, "Expected unindent to close trait declaration", NULL);

    SourceSpan span = source_span_make(start, parser->previous.span.end);

    const char** mnames_copy = NULL;
    size_t* mpcounts_copy = NULL;
    const char*** mpnames_copy = NULL;
    const char*** mptypes_copy = NULL;
    const char** mret_copy = NULL;

    if (method_count > 0) {
        mnames_copy = (const char**)uf_arena_alloc(parser->arena, method_count * sizeof(const char*));
        memcpy(mnames_copy, method_names, method_count * sizeof(const char*));

        mpcounts_copy = (size_t*)uf_arena_alloc(parser->arena, method_count * sizeof(size_t));
        memcpy(mpcounts_copy, method_param_counts, method_count * sizeof(size_t));

        mpnames_copy = (const char***)uf_arena_alloc(parser->arena, method_count * sizeof(const char**));
        memcpy(mpnames_copy, method_param_names, method_count * sizeof(const char**));

        mptypes_copy = (const char***)uf_arena_alloc(parser->arena, method_count * sizeof(const char**));
        memcpy(mptypes_copy, method_param_types, method_count * sizeof(const char**));

        mret_copy = (const char**)uf_arena_alloc(parser->arena, method_count * sizeof(const char*));
        memcpy(mret_copy, method_return_types, method_count * sizeof(const char*));
    }

    return uf_stmt_trait_with_generics(parser->arena, span, name, mnames_copy, mpcounts_copy, mpnames_copy, mptypes_copy, mret_copy, method_count, type_params, type_param_bounds, type_param_count);
}

static UfStmt* parse_impl_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start; /* 'impl' */
    const char* trait_name = parse_type_annotation(parser);

    consume(parser, UF_TOK_FOR, "Expected 'for' after trait name in top-level impl declaration", "Syntax: 'impl <Trait> for <Struct>:'");
    const char* struct_name = parse_type_annotation(parser);

    consume(parser, UF_TOK_COLON, "Expected ':' after struct name in impl declaration", NULL);
    consume(parser, UF_TOK_NEWLINE, "Expected newline after ':' in impl declaration", NULL);
    consume(parser, UF_TOK_INDENT, "Expected indented block for impl methods", NULL);

    UfStmt* methods[32];
    size_t method_count = 0;

    while (!check(parser, UF_TOK_DEDENT) && !check(parser, UF_TOK_EOF)) {
        if (match(parser, UF_TOK_NEWLINE)) continue;
        bool is_impl_m_async = false;
        SourceLoc im_start = parser->current.span.start;
        if (match(parser, UF_TOK_ASYNC)) is_impl_m_async = true;

        if (match(parser, UF_TOK_FUNCTION) ||
            (check(parser, UF_TOK_IDENTIFIER) && parser->current.as.string_val &&
             strcmp(parser->current.as.string_val, "fn") == 0 &&
             peek(parser).kind == UF_TOK_IDENTIFIER && (advance(parser), true))) {
            if (method_count >= 32) {
                error_current(parser, "Method limit exceeded in impl block (32)", NULL);
                break;
            }
            methods[method_count++] = parse_function_statement_async(parser, is_impl_m_async, im_start);
            continue;
        } else if (is_impl_m_async) {
            error_current(parser, "Expected 'function' after 'async'", NULL);
            break;
        }
        error_current(parser, "Expected method definition in impl block", NULL);
        break;
    }

    consume(parser, UF_TOK_DEDENT, "Expected unindent to close impl block", NULL);

    SourceSpan span = source_span_make(start, parser->previous.span.end);
    UfStmt** methods_copy = NULL;
    if (method_count > 0) {
        methods_copy = (UfStmt**)uf_arena_alloc(parser->arena, method_count * sizeof(UfStmt*));
        memcpy(methods_copy, methods, method_count * sizeof(UfStmt*));
    }
    return uf_stmt_impl(parser->arena, span, trait_name, struct_name, methods_copy, method_count);
}

static UfStmt* parse_enum_statement(UfParser* parser) {
    SourceLoc start = parser->previous.span.start;
    consume(parser, UF_TOK_IDENTIFIER, "Expected enum name after 'enum'", "Syntax: 'enum <Name>:'");
    const char* name = parser->previous.as.string_val;

    consume(parser, UF_TOK_COLON, "Expected ':' after enum name", "Syntax: 'enum <Name>:'");

    UfEnumVariant variants[128];
    size_t variant_count = 0;

    if (match(parser, UF_TOK_NEWLINE)) {
        consume(parser, UF_TOK_INDENT, "Expected indented block for enum variants", "Indent the variants of the enum with 4 spaces");
        while (!check(parser, UF_TOK_DEDENT) && !check(parser, UF_TOK_EOF)) {
            if (match(parser, UF_TOK_NEWLINE)) continue;

            if (variant_count >= 128) {
                error_current(parser, "Enum exceeds maximum variant limit (128)", NULL);
                break;
            }

            consume(parser, UF_TOK_IDENTIFIER, "Expected variant name in enum declaration", NULL);
            const char* vname = parser->previous.as.string_val;
            SourceLoc vstart = parser->previous.span.start;

            const char* field_names[32];
            const char* field_types[32];
            size_t field_count = 0;

            if (match(parser, UF_TOK_LPAREN)) {
                if (!check(parser, UF_TOK_RPAREN)) {
                    do {
                        if (field_count >= 32) {
                            error_current(parser, "Variant exceeds maximum field limit (32)", NULL);
                            break;
                        }
                        consume(parser, UF_TOK_IDENTIFIER, "Expected field name in variant", NULL);
                        field_names[field_count] = parser->previous.as.string_val;
                        field_types[field_count] = NULL;
                        if (match(parser, UF_TOK_COLON)) {
                            field_types[field_count] = parse_type_annotation(parser);
                        }
                        field_count++;
                    } while (match(parser, UF_TOK_COMMA));
                }
                consume(parser, UF_TOK_RPAREN, "Expected ')' after variant fields", NULL);
            }

            const char** fnames_copy = NULL;
            const char** ftypes_copy = NULL;
            if (field_count > 0) {
                fnames_copy = (const char**)uf_arena_alloc(parser->arena, field_count * sizeof(const char*));
                memcpy((void*)fnames_copy, field_names, field_count * sizeof(const char*));
                ftypes_copy = (const char**)uf_arena_alloc(parser->arena, field_count * sizeof(const char*));
                memcpy((void*)ftypes_copy, field_types, field_count * sizeof(const char*));
            }

            variants[variant_count].name = vname;
            variants[variant_count].field_names = fnames_copy;
            variants[variant_count].field_types = ftypes_copy;
            variants[variant_count].field_count = field_count;
            variants[variant_count].tag = (int)variant_count;
            variants[variant_count].span = source_span_make(vstart, parser->previous.span.end);
            variant_count++;

            match(parser, UF_TOK_NEWLINE);
        }
        consume(parser, UF_TOK_DEDENT, "Expected unindent to close enum declaration", NULL);
    } else {
        /* Inline comma-separated: enum Color: Red, Green, Blue */
        do {
            if (variant_count >= 128) {
                error_current(parser, "Enum exceeds maximum variant limit (128)", NULL);
                break;
            }
            consume(parser, UF_TOK_IDENTIFIER, "Expected variant name in enum declaration", NULL);
            const char* vname = parser->previous.as.string_val;
            SourceSpan vspan = parser->previous.span;

            variants[variant_count].name = vname;
            variants[variant_count].field_names = NULL;
            variants[variant_count].field_types = NULL;
            variants[variant_count].field_count = 0;
            variants[variant_count].tag = (int)variant_count;
            variants[variant_count].span = vspan;
            variant_count++;
        } while (match(parser, UF_TOK_COMMA));

        if (!check(parser, UF_TOK_EOF)) {
            consume(parser, UF_TOK_NEWLINE, "Expected newline after inline enum declaration", NULL);
        }
    }

    SourceSpan span = source_span_make(start, parser->previous.span.end);
    UfEnumVariant* vars_copy = NULL;
    if (variant_count > 0) {
        vars_copy = (UfEnumVariant*)uf_arena_alloc(parser->arena, variant_count * sizeof(UfEnumVariant));
        memcpy((void*)vars_copy, variants, variant_count * sizeof(UfEnumVariant));
    }

    return uf_stmt_enum(parser->arena, span, name, vars_copy, variant_count);
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

    /* Rest pattern: ...subpattern */
    if (match(parser, UF_TOK_DOTDOTDOT)) {
        UfPattern* sub = NULL;
        if (check(parser, UF_TOK_IDENTIFIER)) {
            advance(parser);
            const char* name = parser->previous.as.string_val;
            if (strcmp(name, "_") == 0) {
                sub = uf_pattern_wildcard(parser->arena, parser->previous.span);
            } else {
                sub = uf_pattern_variable(parser->arena, parser->previous.span, name);
            }
        } else if (check(parser, UF_TOK_COMMA) || check(parser, UF_TOK_RBRACKET) || check(parser, UF_TOK_RBRACE)) {
            sub = uf_pattern_wildcard(parser->arena, parser->previous.span);
        } else {
            sub = parse_pattern(parser);
        }
        return uf_pattern_rest(parser->arena, source_span_make(start, parser->previous.span.end), sub);
    }

    /* Array pattern: [a, b, ...rest] */
    if (match(parser, UF_TOK_LBRACKET)) {
        UfPattern* elements[64];
        size_t count = 0;
        bool has_rest = false;
        if (!check(parser, UF_TOK_RBRACKET)) {
            do {
                if (check(parser, UF_TOK_RBRACKET)) break;
                if (count >= 64) {
                    error_current(parser, "Array pattern exceeds 64 elements", NULL);
                    break;
                }
                if (has_rest) {
                    error_current(parser, "Rest pattern must be the last element in array pattern", NULL);
                    break;
                }
                if (match(parser, UF_TOK_DOTDOTDOT)) {
                    has_rest = true;
                    SourceLoc rest_start = parser->previous.span.start;
                    UfPattern* subpat = NULL;
                    if (check(parser, UF_TOK_IDENTIFIER)) {
                        advance(parser);
                        const char* rest_name = parser->previous.as.string_val;
                        if (strcmp(rest_name, "_") == 0) {
                            subpat = uf_pattern_wildcard(parser->arena, parser->previous.span);
                        } else {
                            subpat = uf_pattern_variable(parser->arena, parser->previous.span, rest_name);
                        }
                    } else if (check(parser, UF_TOK_COMMA) || check(parser, UF_TOK_RBRACKET)) {
                        subpat = uf_pattern_wildcard(parser->arena, parser->previous.span);
                    } else {
                        subpat = parse_pattern(parser);
                    }
                    elements[count++] = uf_pattern_rest(parser->arena, source_span_make(rest_start, parser->previous.span.end), subpat);
                } else {
                    elements[count++] = parse_pattern(parser);
                }
            } while (match(parser, UF_TOK_COMMA));
        }
        consume(parser, UF_TOK_RBRACKET, "Expected ']' after array pattern elements", NULL);
        SourceSpan span = source_span_make(start, parser->previous.span.end);
        UfPattern** elements_copy = NULL;
        if (count > 0) {
            elements_copy = (UfPattern**)uf_arena_alloc(parser->arena, count * sizeof(UfPattern*));
            memcpy(elements_copy, elements, count * sizeof(UfPattern*));
        }
        return uf_pattern_array(parser->arena, span, elements_copy, count, has_rest);
    }

    /* Map pattern: {x, y} or {x: a, y: b} or {x, ...rest} */
    if (match(parser, UF_TOK_LBRACE)) {
        const char* keys[64];
        UfPattern* values[64];
        size_t count = 0;
        bool has_rest = false;
        UfPattern* rest_pat = NULL;
        if (!check(parser, UF_TOK_RBRACE)) {
            do {
                if (check(parser, UF_TOK_RBRACE)) break;
                if (count >= 64) {
                    error_current(parser, "Map pattern exceeds 64 entries", NULL);
                    break;
                }
                if (has_rest) {
                    error_current(parser, "Rest pattern must be the last entry in map pattern", NULL);
                    break;
                }
                if (match(parser, UF_TOK_DOTDOTDOT)) {
                    has_rest = true;
                    SourceLoc rest_start = parser->previous.span.start;
                    UfPattern* subpat = NULL;
                    if (check(parser, UF_TOK_IDENTIFIER)) {
                        advance(parser);
                        const char* rest_name = parser->previous.as.string_val;
                        if (strcmp(rest_name, "_") == 0) {
                            subpat = uf_pattern_wildcard(parser->arena, parser->previous.span);
                        } else {
                            subpat = uf_pattern_variable(parser->arena, parser->previous.span, rest_name);
                        }
                    } else if (check(parser, UF_TOK_COMMA) || check(parser, UF_TOK_RBRACE)) {
                        subpat = uf_pattern_wildcard(parser->arena, parser->previous.span);
                    } else {
                        subpat = parse_pattern(parser);
                    }
                    rest_pat = uf_pattern_rest(parser->arena, source_span_make(rest_start, parser->previous.span.end), subpat);
                } else {
                    const char* key_str = NULL;
                    if (match(parser, UF_TOK_IDENTIFIER)) {
                        key_str = parser->previous.as.string_val;
                    } else if (match(parser, UF_TOK_STRING)) {
                        key_str = parser->previous.as.string_val;
                    } else if (is_identifier_or_keyword(parser->current.kind)) {
                        advance(parser);
                        key_str = parser->previous.as.string_val;
                    } else {
                        error_current(parser, "Expected key in map pattern", NULL);
                        break;
                    }

                    UfPattern* val_pat = NULL;
                    if (match(parser, UF_TOK_COLON)) {
                        val_pat = parse_pattern(parser);
                    } else {
                        /* Shorthand {x}: key is "x", val pattern is variable "x" */
                        val_pat = uf_pattern_variable(parser->arena, parser->previous.span, key_str);
                    }
                    keys[count] = key_str;
                    values[count] = val_pat;
                    count++;
                }
            } while (match(parser, UF_TOK_COMMA));
        }
        consume(parser, UF_TOK_RBRACE, "Expected '}' after map pattern entries", NULL);
        SourceSpan span = source_span_make(start, parser->previous.span.end);
        const char** keys_copy = NULL;
        UfPattern** values_copy = NULL;
        if (count > 0) {
            keys_copy = (const char**)uf_arena_alloc(parser->arena, count * sizeof(const char*));
            memcpy(keys_copy, keys, count * sizeof(const char*));
            values_copy = (UfPattern**)uf_arena_alloc(parser->arena, count * sizeof(UfPattern*));
            memcpy(values_copy, values, count * sizeof(UfPattern*));
        }
        return uf_pattern_map(parser->arena, span, keys_copy, values_copy, count, has_rest, rest_pat);
    }

    /* Identifier: either variable binding or struct/enum pattern Point(...) / Enum.Variant */
    if (match(parser, UF_TOK_IDENTIFIER)) {
        const char* name = parser->previous.as.string_val;
        if (match(parser, UF_TOK_DOT)) {
            consume(parser, UF_TOK_IDENTIFIER, "Expected variant name after '.' in pattern", NULL);
            const char* vname = parser->previous.as.string_val;
            if (match(parser, UF_TOK_LPAREN)) {
                UfPattern* subpats[32];
                size_t subcount = 0;
                if (!check(parser, UF_TOK_RPAREN)) {
                    do {
                        if (subcount >= 32) {
                            error_current(parser, "Pattern exceeds 32 fields", NULL);
                            break;
                        }
                        subpats[subcount++] = parse_pattern(parser);
                    } while (match(parser, UF_TOK_COMMA));
                }
                consume(parser, UF_TOK_RPAREN, "Expected ')' after pattern fields", NULL);
                SourceSpan span = source_span_make(start, parser->previous.span.end);
                UfPattern** subpats_copy = NULL;
                if (subcount > 0) {
                    subpats_copy = (UfPattern**)uf_arena_alloc(parser->arena, subcount * sizeof(UfPattern*));
                    memcpy(subpats_copy, subpats, subcount * sizeof(UfPattern*));
                }
                return uf_pattern_struct(parser->arena, span, vname, subpats_copy, subcount);
            } else {
                SourceSpan span = source_span_make(start, parser->previous.span.end);
                return uf_pattern_struct(parser->arena, span, vname, NULL, 0);
            }
        } else if (match(parser, UF_TOK_LPAREN)) {
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

    error_current(parser, "Expected pattern", "Patterns can be literals, variables, '_', structs, arrays, or maps");
    return uf_pattern_wildcard(parser->arena, parser->current.span);
}

static UfPattern* expr_to_pattern(UfArena* arena, const UfExpr* expr) {
    if (!expr) return NULL;
    switch (expr->kind) {
        case UF_EXPR_IDENTIFIER: {
            if (strcmp(expr->as.identifier_name, "_") == 0) {
                return uf_pattern_wildcard(arena, expr->span);
            }
            return uf_pattern_variable(arena, expr->span, expr->as.identifier_name);
        }
        case UF_EXPR_SPREAD: {
            UfPattern* sub = expr_to_pattern(arena, expr->as.spread.operand);
            return uf_pattern_rest(arena, expr->span, sub);
        }
        case UF_EXPR_ARRAY: {
            size_t count = expr->as.array_lit.count;
            UfPattern** elements = NULL;
            bool has_rest = false;
            if (count > 0) {
                elements = (UfPattern**)uf_arena_alloc(arena, count * sizeof(UfPattern*));
                for (size_t i = 0; i < count; ++i) {
                    elements[i] = expr_to_pattern(arena, expr->as.array_lit.elements[i]);
                    if (!elements[i]) return NULL;
                    if (elements[i]->kind == UF_PAT_REST) {
                        has_rest = true;
                        if (i != count - 1) {
                            return NULL;
                        }
                    }
                }
            }
            return uf_pattern_array(arena, expr->span, elements, count, has_rest);
        }
        case UF_EXPR_MAP: {
            size_t count = 0;
            bool has_rest = false;
            UfPattern* rest_pat = NULL;
            const char* keys[256];
            UfPattern* values[256];
            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                if (expr->as.map_lit.values[i] == NULL) {
                    has_rest = true;
                    UfExpr* k = expr->as.map_lit.keys[i];
                    if (k && k->kind == UF_EXPR_SPREAD) {
                        rest_pat = expr_to_pattern(arena, k->as.spread.operand);
                    } else {
                        rest_pat = expr_to_pattern(arena, k);
                    }
                } else {
                    UfExpr* k = expr->as.map_lit.keys[i];
                    const char* key_name = NULL;
                    if (k->kind == UF_EXPR_LITERAL_STRING) {
                        key_name = k->as.string_val;
                    } else if (k->kind == UF_EXPR_IDENTIFIER) {
                        key_name = k->as.identifier_name;
                    } else {
                        return NULL;
                    }
                    keys[count] = key_name;
                    values[count] = expr_to_pattern(arena, expr->as.map_lit.values[i]);
                    if (!values[count]) return NULL;
                    count++;
                }
            }
            const char** keys_copy = NULL;
            UfPattern** values_copy = NULL;
            if (count > 0) {
                keys_copy = (const char**)uf_arena_alloc(arena, count * sizeof(const char*));
                memcpy(keys_copy, keys, count * sizeof(const char*));
                values_copy = (UfPattern**)uf_arena_alloc(arena, count * sizeof(UfPattern*));
                memcpy(values_copy, values, count * sizeof(UfPattern*));
            }
            return uf_pattern_map(arena, expr->span, keys_copy, values_copy, count, has_rest, rest_pat);
        }
        default:
            return NULL;
    }
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
    if (match(parser, UF_TOK_ASYNC)) {
        SourceLoc start = parser->previous.span.start;
        if (match(parser, UF_TOK_FUNCTION)) return parse_function_statement_async(parser, true, start);
        if (check(parser, UF_TOK_IDENTIFIER) && parser->current.as.string_val &&
            strcmp(parser->current.as.string_val, "fn") == 0 &&
            peek(parser).kind == UF_TOK_IDENTIFIER) {
            advance(parser);
            return parse_function_statement_async(parser, true, start);
        }
        error_current(parser, "Expected 'function' after 'async'", NULL);
        return NULL;
    }
    if (match(parser, UF_TOK_FUNCTION)) return parse_function_statement(parser);
    if (check(parser, UF_TOK_IDENTIFIER) && parser->current.as.string_val &&
        strcmp(parser->current.as.string_val, "fn") == 0 &&
        peek(parser).kind == UF_TOK_IDENTIFIER) {
        advance(parser);
        return parse_function_statement(parser);
    }
    if (match(parser, UF_TOK_STRUCT))   return parse_struct_statement(parser);
    if (match(parser, UF_TOK_ENUM))     return parse_enum_statement(parser);
    if (match(parser, UF_TOK_TRAIT))    return parse_trait_statement(parser);
    if (match(parser, UF_TOK_IMPL))     return parse_impl_statement(parser);
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
        if (parser->panic_mode || !stmt) {
            synchronize(parser);
        } else {
            stmts[count++] = stmt;
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

UfStmt* uf_parse_statement(UfParser* parser) {
    return parse_statement(parser);
}
