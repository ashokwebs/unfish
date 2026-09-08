#include "uf_lexer.h"
#include <ctype.h>

void uf_lexer_init(UfLexer* lexer,
                   const char* file_name,
                   const char* source,
                   UfArena* arena,
                   UfInterner* interner,
                   UfDiagnosticReporter* reporter) {
    lexer->source = source;
    lexer->file_name = file_name;
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
    lexer->col = 1;
    lexer->start_line = 1;
    lexer->start_col = 1;

    lexer->indent_depth = 0;
    lexer->indent_stack[0] = 0;
    lexer->pending_dedents = 0;
    lexer->at_line_start = true;
    lexer->paren_depth = 0;

    lexer->last_token_kind = (UfTokenKind)0;
    lexer->has_tokens = false;

    lexer->arena = arena;
    lexer->interner = interner;
    lexer->reporter = reporter;
}

static bool is_at_end(const UfLexer* lexer) {
    return *lexer->current == '\0';
}

static char peek(const UfLexer* lexer) {
    return *lexer->current;
}

static char peek_next(const UfLexer* lexer) {
    if (is_at_end(lexer)) return '\0';
    return lexer->current[1];
}

static char advance(UfLexer* lexer) {
    char c = *lexer->current++;
    if (c == '\r') {
        if (*lexer->current == '\n') {
            lexer->current++;
        }
        lexer->line++;
        lexer->col = 1;
        return '\n';
    } else if (c == '\n') {
        lexer->line++;
        lexer->col = 1;
        return '\n';
    } else {
        lexer->col++;
        return c;
    }
}

static bool match(UfLexer* lexer, char expected) {
    if (is_at_end(lexer)) return false;
    if (*lexer->current != expected) return false;
    advance(lexer);
    return true;
}

static SourceSpan make_span(const UfLexer* lexer) {
    SourceLoc start = source_loc_make(lexer->file_name, lexer->start_line, lexer->start_col, (uint32_t)(lexer->start - lexer->source));
    SourceLoc end   = source_loc_make(lexer->file_name, lexer->line, lexer->col, (uint32_t)(lexer->current - lexer->source));
    return source_span_make(start, end);
}

static UfToken finish_token(UfLexer* lexer, UfToken token) {
    lexer->last_token_kind = token.kind;
    lexer->has_tokens = true;
    return token;
}

static UfToken make_token(UfLexer* lexer, UfTokenKind kind) {
    UfToken token;
    token.kind = kind;
    token.span = make_span(lexer);
    token.lexeme = lexer->start;
    token.length = (size_t)(lexer->current - lexer->start);
    token.as.number_val = 0;
    return finish_token(lexer, token);
}

static UfToken error_token(UfLexer* lexer, const char* message, const char* hint) {
    SourceSpan span = make_span(lexer);
    if (lexer->reporter) {
        uf_report_diag(lexer->reporter, UF_DIAG_LEX_ERROR, span, message, hint);
    }
    UfToken token;
    token.kind = UF_TOK_ERROR;
    token.span = span;
    token.lexeme = lexer->start;
    token.length = (size_t)(lexer->current - lexer->start);
    token.as.string_val = message;
    return finish_token(lexer, token);
}

static void skip_horizontal_whitespace_and_comments(UfLexer* lexer) {
    for (;;) {
        char c = peek(lexer);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lexer);
        } else if (c == '#') {
            while (peek(lexer) != '\n' && !is_at_end(lexer)) {
                advance(lexer);
            }
        } else {
            break;
        }
    }
}

static UfToken scan_multiline_string(UfLexer* lexer) {
    UfStrBuf buf;
    uf_strbuf_init(&buf);

    while (!is_at_end(lexer)) {
        if (peek(lexer) == '"' && peek_next(lexer) == '"' &&
            lexer->current[1] != '\0' && lexer->current[2] == '"') {
            advance(lexer);
            advance(lexer);
            advance(lexer);

            char* str_copy = uf_arena_strdup(lexer->arena, buf.data ? buf.data : "");
            uf_strbuf_free(&buf);

            UfToken token;
            token.kind = UF_TOK_STRING;
            token.span = make_span(lexer);
            token.lexeme = lexer->start;
            token.length = (size_t)(lexer->current - lexer->start);
            token.as.string_val = str_copy;
            return finish_token(lexer, token);
        }

        char c = advance(lexer);
        if (c == '\\') {
            if (is_at_end(lexer)) {
                uf_strbuf_free(&buf);
                return error_token(lexer, "Unterminated escape sequence in string", NULL);
            }
            char esc = advance(lexer);
            switch (esc) {
                case 'n': uf_strbuf_append_char(&buf, '\n'); break;
                case 't': uf_strbuf_append_char(&buf, '\t'); break;
                case '"': uf_strbuf_append_char(&buf, '"'); break;
                case '\\': uf_strbuf_append_char(&buf, '\\'); break;
                case '\n': break;
                default:
                    uf_strbuf_append_char(&buf, '\\');
                    uf_strbuf_append_char(&buf, esc);
                    break;
            }
        } else {
            uf_strbuf_append_char(&buf, c);
        }
    }

    uf_strbuf_free(&buf);
    return error_token(lexer, "Unterminated multi-line string literal: missing closing '\"\"\"'", "Add '\"\"\"' to close the string");
}

static UfToken scan_string(UfLexer* lexer) {
    UfStrBuf buf;
    uf_strbuf_init(&buf);

    while (!is_at_end(lexer) && peek(lexer) != '"') {
        char c = advance(lexer);
        if (c == '\n') {
            uf_strbuf_free(&buf);
            return error_token(lexer, "Unterminated string literal: string cannot span multiple physical lines without escape", "Close the string with '\"'");
        }
        if (c == '\\') {
            if (is_at_end(lexer)) {
                uf_strbuf_free(&buf);
                return error_token(lexer, "Unterminated escape sequence in string", NULL);
            }
            char esc = advance(lexer);
            switch (esc) {
                case 'n': uf_strbuf_append_char(&buf, '\n'); break;
                case 't': uf_strbuf_append_char(&buf, '\t'); break;
                case '"': uf_strbuf_append_char(&buf, '"'); break;
                case '\\': uf_strbuf_append_char(&buf, '\\'); break;
                default: {
                    while (!is_at_end(lexer) && peek(lexer) != '"' && peek(lexer) != '\n') {
                        advance(lexer);
                    }
                    if (!is_at_end(lexer) && peek(lexer) == '"') {
                        advance(lexer);
                    }
                    uf_strbuf_free(&buf);
                    return error_token(lexer, "Invalid escape sequence in string", "Valid escapes are \\n, \\t, \\\", \\\\");
                }
            }
        } else {
            uf_strbuf_append_char(&buf, c);
        }
    }

    if (is_at_end(lexer)) {
        uf_strbuf_free(&buf);
        return error_token(lexer, "Unterminated string literal: missing closing '\"'", "Add '\"' at the end of the string");
    }

    /* Consume closing quote */
    advance(lexer);

    char* str_copy = uf_arena_strdup(lexer->arena, buf.data ? buf.data : "");
    uf_strbuf_free(&buf);

    UfToken token;
    token.kind = UF_TOK_STRING;
    token.span = make_span(lexer);
    token.lexeme = lexer->start;
    token.length = (size_t)(lexer->current - lexer->start);
    token.as.string_val = str_copy;
    return finish_token(lexer, token);
}

static UfToken scan_fstring(UfLexer* lexer, bool is_multiline) {
    UfStrBuf buf;
    uf_strbuf_init(&buf);

    int brace_depth = 0;
    bool in_expr_string = false;

    while (!is_at_end(lexer)) {
        if (brace_depth == 0) {
            if (is_multiline) {
                if (peek(lexer) == '"' && peek_next(lexer) == '"' &&
                    lexer->current[1] != '\0' && lexer->current[2] == '"') {
                    advance(lexer);
                    advance(lexer);
                    advance(lexer);

                    char* str_copy = uf_arena_strdup(lexer->arena, buf.data ? buf.data : "");
                    uf_strbuf_free(&buf);

                    UfToken token;
                    token.kind = UF_TOK_FSTRING;
                    token.span = make_span(lexer);
                    token.lexeme = lexer->start;
                    token.length = (size_t)(lexer->current - lexer->start);
                    token.as.string_val = str_copy;
                    return finish_token(lexer, token);
                }
            } else {
                if (peek(lexer) == '"') {
                    advance(lexer);

                    char* str_copy = uf_arena_strdup(lexer->arena, buf.data ? buf.data : "");
                    uf_strbuf_free(&buf);

                    UfToken token;
                    token.kind = UF_TOK_FSTRING;
                    token.span = make_span(lexer);
                    token.lexeme = lexer->start;
                    token.length = (size_t)(lexer->current - lexer->start);
                    token.as.string_val = str_copy;
                    return finish_token(lexer, token);
                }
                if (peek(lexer) == '\n') {
                    uf_strbuf_free(&buf);
                    return error_token(lexer, "Unterminated f-string literal: string cannot span multiple physical lines without escape", "Close the f-string with '\"'");
                }
            }
        }

        char c = advance(lexer);

        if (c == '\\') {
            if (is_at_end(lexer)) {
                uf_strbuf_free(&buf);
                return error_token(lexer, "Unterminated escape sequence in f-string", NULL);
            }
            char esc = advance(lexer);
            if (brace_depth > 0 && esc == '"') {
                in_expr_string = !in_expr_string;
                uf_strbuf_append_char(&buf, '"');
            } else {
                uf_strbuf_append_char(&buf, '\\');
                uf_strbuf_append_char(&buf, esc);
            }
            continue;
        }

        if (brace_depth > 0) {
            if (c == '"') {
                in_expr_string = !in_expr_string;
                uf_strbuf_append_char(&buf, '"');
                continue;
            }
            if (!in_expr_string) {
                if (c == '{') brace_depth++;
                else if (c == '}') brace_depth--;
            }
            uf_strbuf_append_char(&buf, c);
            continue;
        }

        if (c == '{') {
            if (peek(lexer) == '{') {
                advance(lexer);
                uf_strbuf_append(&buf, "{{");
                continue;
            }
            brace_depth = 1;
            in_expr_string = false;
            uf_strbuf_append_char(&buf, '{');
            continue;
        }

        if (c == '}') {
            if (peek(lexer) == '}') {
                advance(lexer);
                uf_strbuf_append(&buf, "}}");
                continue;
            }
            uf_strbuf_append_char(&buf, '}');
            continue;
        }

        uf_strbuf_append_char(&buf, c);
    }

    uf_strbuf_free(&buf);
    return error_token(lexer, is_multiline ?
                       "Unterminated multi-line f-string literal: missing closing '\"\"\"'" :
                       "Unterminated f-string literal: missing closing '\"'", NULL);
}

static UfToken scan_number(UfLexer* lexer) {
    while (isdigit((unsigned char)peek(lexer))) {
        advance(lexer);
    }

    /* Fractional part */
    if (peek(lexer) == '.' && isdigit((unsigned char)peek_next(lexer))) {
        advance(lexer); /* Consume '.' */
        while (isdigit((unsigned char)peek(lexer))) {
            advance(lexer);
        }
    }

    /* Exponent part: e/E, optional sign, then at least one digit
     * (e.g. 1e10, 6.022e23, 1.5e-10). Speculatively consume and back out if
     * it turns out not to be followed by a digit, so `1e` followed by an
     * identifier (unusual, but not our call to reject) still lexes as the
     * number `1` followed by an `e...` identifier rather than erroring. */
    if (peek(lexer) == 'e' || peek(lexer) == 'E') {
        const char* save_current = lexer->current;
        uint32_t save_col = lexer->col;
        advance(lexer); /* consume 'e'/'E' */
        if (peek(lexer) == '+' || peek(lexer) == '-') {
            advance(lexer);
        }
        if (isdigit((unsigned char)peek(lexer))) {
            while (isdigit((unsigned char)peek(lexer))) {
                advance(lexer);
            }
        } else {
            lexer->current = save_current;
            lexer->col = save_col;
        }
    }

    size_t len = (size_t)(lexer->current - lexer->start);
    char num_buf[64];
    char* buf = num_buf;
    if (len >= sizeof(num_buf)) {
        buf = (char*)malloc(len + 1);
    }
    memcpy(buf, lexer->start, len);
    buf[len] = '\0';

    char* endptr = NULL;
    double val = strtod(buf, &endptr);
    if (buf != num_buf) {
        free(buf);
    }

    UfToken token;
    token.kind = UF_TOK_NUMBER;
    token.span = make_span(lexer);
    token.lexeme = lexer->start;
    token.length = len;
    token.as.number_val = val;
    return finish_token(lexer, token);
}

static UfTokenKind check_keyword(const char* word, size_t len) {
    switch (len) {
        case 2:
            if (memcmp(word, "if", 2) == 0) return UF_TOK_IF;
            if (memcmp(word, "or", 2) == 0) return UF_TOK_OR;
            if (memcmp(word, "in", 2) == 0) return UF_TOK_IN;
            if (memcmp(word, "as", 2) == 0) return UF_TOK_AS;
            break;
        case 3:
            if (memcmp(word, "let", 3) == 0) return UF_TOK_LET;
            if (memcmp(word, "say", 3) == 0) return UF_TOK_SAY;
            if (memcmp(word, "and", 3) == 0) return UF_TOK_AND;
            if (memcmp(word, "not", 3) == 0) return UF_TOK_NOT;
            if (memcmp(word, "for", 3) == 0) return UF_TOK_FOR;
            if (memcmp(word, "try", 3) == 0) return UF_TOK_TRY;
            break;
        case 4:
            if (memcmp(word, "else", 4) == 0) return UF_TOK_ELSE;
            if (memcmp(word, "enum", 4) == 0) return UF_TOK_ENUM;
            if (memcmp(word, "impl", 4) == 0) return UF_TOK_IMPL;
            if (memcmp(word, "true", 4) == 0) return UF_TOK_TRUE;
            if (memcmp(word, "null", 4) == 0) return UF_TOK_NULL;
            if (memcmp(word, "from", 4) == 0) return UF_TOK_FROM;
            if (memcmp(word, "when", 4) == 0) return UF_TOK_WHEN;
            break;
        case 5:
            if (memcmp(word, "while", 5) == 0) return UF_TOK_WHILE;
            if (memcmp(word, "times", 5) == 0) return UF_TOK_TIMES;
            if (memcmp(word, "trait", 5) == 0) return UF_TOK_TRAIT;
            if (memcmp(word, "async", 5) == 0) return UF_TOK_ASYNC;
            if (memcmp(word, "await", 5) == 0) return UF_TOK_AWAIT;
            if (memcmp(word, "false", 5) == 0) return UF_TOK_FALSE;
            if (memcmp(word, "break", 5) == 0) return UF_TOK_BREAK;
            if (memcmp(word, "catch", 5) == 0) return UF_TOK_CATCH;
            if (memcmp(word, "match", 5) == 0) return UF_TOK_MATCH;
            break;
        case 6:
            if (memcmp(word, "return", 6) == 0) return UF_TOK_RETURN;
            if (memcmp(word, "repeat", 6) == 0) return UF_TOK_REPEAT;
            if (memcmp(word, "import", 6) == 0) return UF_TOK_IMPORT;
            if (memcmp(word, "struct", 6) == 0) return UF_TOK_STRUCT;
            break;
        case 7:
            if (memcmp(word, "finally", 7) == 0) return UF_TOK_FINALLY;
            break;
        case 8:
            if (memcmp(word, "function", 8) == 0) return UF_TOK_FUNCTION;
            if (memcmp(word, "continue", 8) == 0) return UF_TOK_CONTINUE;
            break;
    }
    return UF_TOK_IDENTIFIER;
}

static UfToken scan_identifier_or_keyword(UfLexer* lexer) {
    while (isalnum((unsigned char)peek(lexer)) || peek(lexer) == '_') {
        advance(lexer);
    }

    size_t len = (size_t)(lexer->current - lexer->start);
    UfTokenKind kind = check_keyword(lexer->start, len);

    UfToken token;
    token.kind = kind;
    token.span = make_span(lexer);
    token.lexeme = lexer->start;
    token.length = len;
    token.as.number_val = 0;
    token.as.string_val = uf_intern(lexer->interner, lexer->start, len);
    return finish_token(lexer, token);
}

UfToken uf_lexer_next_token(UfLexer* lexer) {
    for (;;) {
        /* If we have pending DEDENT tokens queued, emit them first */
        if (lexer->pending_dedents > 0) {
            lexer->pending_dedents--;
            lexer->indent_depth--;
            UfToken token;
            token.kind = UF_TOK_DEDENT;
            token.span = make_span(lexer);
            token.lexeme = "";
            token.length = 0;
            token.as.number_val = 0;
            return finish_token(lexer, token);
        }

        /* Process indentation at line start */
        if (lexer->at_line_start) {
            lexer->at_line_start = false;

            /* Scan leading whitespace on this line to measure indentation */
            uint32_t spaces = 0;
            bool has_tab = false;
            const char* p = lexer->current;

            while (*p == ' ' || *p == '\t') {
                if (*p == '\t') has_tab = true;
                spaces++;
                p++;
            }

            /* If line is empty or comment-only, skip it */
            if (*p == '\r' || *p == '\n' || *p == '#') {
                lexer->current = p;
                if (*p == '#') {
                    while (*lexer->current != '\n' && !is_at_end(lexer)) {
                        advance(lexer);
                    }
                }
                if (*lexer->current == '\r') advance(lexer);
                if (*lexer->current == '\n') advance(lexer);
                lexer->at_line_start = true;
                continue;
            }

            if (*p == '\0') {
                /* Reached end of file while at line start */
                lexer->current = p;
                /* Fall through to EOF handling below */
            } else {
                if (has_tab) {
                    lexer->start = lexer->current;
                    lexer->start_line = lexer->line;
                    lexer->start_col = lexer->col;
                    return error_token(lexer, "Tab character used for indentation", "Unfish uses spaces for indentation. Please replace tabs with spaces.");
                }

                /* Advance lexer past leading spaces */
                while (lexer->current < p) {
                    advance(lexer);
                }

                uint32_t current_indent = spaces;
                uint32_t prev_indent = lexer->indent_stack[lexer->indent_depth];
                bool allow_indent = (lexer->paren_depth == 0) ||
                                    (lexer->paren_depth > 0 &&
                                     (lexer->indent_depth > lexer->paren_indent_stack[lexer->paren_depth - 1] ||
                                      current_indent > prev_indent));
                if (allow_indent) {
                    if (current_indent > prev_indent) {
                        if (lexer->indent_depth + 1 >= UF_MAX_INDENT_DEPTH) {
                            return error_token(lexer, "Indentation exceeds maximum nesting depth", NULL);
                        }
                        lexer->indent_depth++;
                        lexer->indent_stack[lexer->indent_depth] = current_indent;

                        UfToken token;
                        token.kind = UF_TOK_INDENT;
                        token.span = make_span(lexer);
                        token.lexeme = "";
                        token.length = 0;
                        token.as.number_val = 0;
                        return finish_token(lexer, token);
                    } else if (current_indent < prev_indent) {
                        /* Find target level */
                        int target_depth = -1;
                        for (int i = lexer->indent_depth; i >= 0; --i) {
                            if (lexer->indent_stack[i] == current_indent) {
                                target_depth = i;
                                break;
                            }
                        }

                        if (target_depth == -1) {
                            lexer->start = lexer->current;
                            lexer->start_line = lexer->line;
                            lexer->start_col = lexer->col;
                            return error_token(lexer, "Unindent does not match any outer indentation level", "Ensure consistent indentation (e.g. 4 spaces per block)");
                        }

                        int dedents = lexer->indent_depth - target_depth;
                        lexer->pending_dedents = dedents - 1;
                        lexer->indent_depth--;

                        UfToken token;
                        token.kind = UF_TOK_DEDENT;
                        token.span = make_span(lexer);
                        token.lexeme = "";
                        token.length = 0;
                        token.as.number_val = 0;
                        return finish_token(lexer, token);
                    }
                }
            }
        }

        skip_horizontal_whitespace_and_comments(lexer);

        lexer->start = lexer->current;
        lexer->start_line = lexer->line;
        lexer->start_col = lexer->col;

        if (is_at_end(lexer)) {
            /* Emit NEWLINE if last token was not NEWLINE/DEDENT and we had tokens */
            if (lexer->has_tokens &&
                lexer->last_token_kind != UF_TOK_NEWLINE &&
                lexer->last_token_kind != UF_TOK_DEDENT &&
                lexer->last_token_kind != UF_TOK_EOF) {
                return make_token(lexer, UF_TOK_NEWLINE);
            }
            if (lexer->indent_depth > 0) {
                lexer->indent_depth--;
                UfToken token;
                token.kind = UF_TOK_DEDENT;
                token.span = make_span(lexer);
                token.lexeme = "";
                token.length = 0;
                token.as.number_val = 0;
                return finish_token(lexer, token);
            }
            return make_token(lexer, UF_TOK_EOF);
        }

        char c = advance(lexer);

        if (c == '\n') {
            bool in_callback = (lexer->paren_depth > 0 &&
                (lexer->last_token_kind == UF_TOK_COLON ||
                 lexer->indent_depth > (lexer->paren_depth > 0 ? lexer->paren_indent_stack[lexer->paren_depth - 1] : 0)));
            if (lexer->paren_depth == 0 || in_callback) {
                lexer->at_line_start = true;
                return make_token(lexer, UF_TOK_NEWLINE);
            }
            continue;
        }

        if (c == '\r') {
            if (peek(lexer) == '\n') advance(lexer);
            bool in_callback = (lexer->paren_depth > 0 &&
                (lexer->last_token_kind == UF_TOK_COLON ||
                 lexer->indent_depth > (lexer->paren_depth > 0 ? lexer->paren_indent_stack[lexer->paren_depth - 1] : 0)));
            if (lexer->paren_depth == 0 || in_callback) {
                lexer->at_line_start = true;
                return make_token(lexer, UF_TOK_NEWLINE);
            }
            continue;
        }

        if (isdigit((unsigned char)c)) {
            return scan_number(lexer);
        }

        if ((c == 'f' || c == 'F') && peek(lexer) == '"') {
            advance(lexer);
            if (peek(lexer) == '"' && peek_next(lexer) == '"') {
                advance(lexer);
                advance(lexer);
                return scan_fstring(lexer, true);
            }
            return scan_fstring(lexer, false);
        }

        if (isalpha((unsigned char)c) || c == '_') {
            return scan_identifier_or_keyword(lexer);
        }

        if (c == '"') {
            if (peek(lexer) == '"' && peek_next(lexer) == '"') {
                advance(lexer);
                advance(lexer);
                return scan_multiline_string(lexer);
            }
            return scan_string(lexer);
        }

        switch (c) {
            case '+': return make_token(lexer, UF_TOK_PLUS);
            case '-': return make_token(lexer, UF_TOK_MINUS);
            case '*': return make_token(lexer, UF_TOK_STAR);
            case '/': return make_token(lexer, UF_TOK_SLASH);
            case '%': return make_token(lexer, UF_TOK_PERCENT);
            case ':': return make_token(lexer, UF_TOK_COLON);
            case ',': return make_token(lexer, UF_TOK_COMMA);
            case '(':
                if (lexer->paren_depth < UF_MAX_INDENT_DEPTH) {
                    lexer->paren_indent_stack[lexer->paren_depth] = lexer->indent_depth;
                }
                lexer->paren_depth++;
                return make_token(lexer, UF_TOK_LPAREN);
            case ')':
                if (lexer->paren_depth > 0) lexer->paren_depth--;
                return make_token(lexer, UF_TOK_RPAREN);
            case '[':
                if (lexer->paren_depth < UF_MAX_INDENT_DEPTH) {
                    lexer->paren_indent_stack[lexer->paren_depth] = lexer->indent_depth;
                }
                lexer->paren_depth++;
                return make_token(lexer, UF_TOK_LBRACKET);
            case ']':
                if (lexer->paren_depth > 0) lexer->paren_depth--;
                return make_token(lexer, UF_TOK_RBRACKET);
            case '{':
                if (lexer->paren_depth < UF_MAX_INDENT_DEPTH) {
                    lexer->paren_indent_stack[lexer->paren_depth] = lexer->indent_depth;
                }
                lexer->paren_depth++;
                return make_token(lexer, UF_TOK_LBRACE);
            case '}':
                if (lexer->paren_depth > 0) lexer->paren_depth--;
                return make_token(lexer, UF_TOK_RBRACE);
            case '.':
                if (peek(lexer) == '.' && peek_next(lexer) == '.') {
                    advance(lexer);
                    advance(lexer);
                    return make_token(lexer, UF_TOK_DOTDOTDOT);
                }
                return make_token(lexer, UF_TOK_DOT);
            case '=':
                return make_token(lexer, match(lexer, '=') ? UF_TOK_EQEQ : UF_TOK_EQUAL);
            case '!':
                if (match(lexer, '=')) return make_token(lexer, UF_TOK_BANGEQ);
                return error_token(lexer, "Unexpected character '!' (did you mean '!=' or 'not'?)", "Use 'not' for logical negation, or '!=' for inequality");
            case '<':
                return make_token(lexer, match(lexer, '=') ? UF_TOK_LTEQ : UF_TOK_LT);
            case '>':
                return make_token(lexer, match(lexer, '=') ? UF_TOK_GTEQ : UF_TOK_GT);
            case '|':
                return make_token(lexer, match(lexer, '>') ? UF_TOK_PIPE_RIGHT : UF_TOK_PIPE);
        }


        return error_token(lexer, "Unexpected character in source", "Check for non-ASCII characters or unsupported symbols");
    }
}
