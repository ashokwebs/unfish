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
    if (c == '\n') {
        lexer->line++;
        lexer->col = 1;
    } else {
        lexer->col++;
    }
    return c;
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

    size_t len = (size_t)(lexer->current - lexer->start);
    char num_buf[64];
    if (len >= sizeof(num_buf)) len = sizeof(num_buf) - 1;
    memcpy(num_buf, lexer->start, len);
    num_buf[len] = '\0';

    char* endptr = NULL;
    double val = strtod(num_buf, &endptr);

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
            break;
        case 3:
            if (memcmp(word, "let", 3) == 0) return UF_TOK_LET;
            if (memcmp(word, "say", 3) == 0) return UF_TOK_SAY;
            if (memcmp(word, "and", 3) == 0) return UF_TOK_AND;
            if (memcmp(word, "not", 3) == 0) return UF_TOK_NOT;
            break;
        case 4:
            if (memcmp(word, "else", 4) == 0) return UF_TOK_ELSE;
            if (memcmp(word, "true", 4) == 0) return UF_TOK_TRUE;
            if (memcmp(word, "null", 4) == 0) return UF_TOK_NULL;
            break;
        case 5:
            if (memcmp(word, "while", 5) == 0) return UF_TOK_WHILE;
            if (memcmp(word, "times", 5) == 0) return UF_TOK_TIMES;
            if (memcmp(word, "false", 5) == 0) return UF_TOK_FALSE;
            break;
        case 6:
            if (memcmp(word, "return", 6) == 0) return UF_TOK_RETURN;
            if (memcmp(word, "repeat", 6) == 0) return UF_TOK_REPEAT;
            break;
        case 8:
            if (memcmp(word, "function", 8) == 0) return UF_TOK_FUNCTION;
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
    if (kind == UF_TOK_IDENTIFIER) {
        token.as.string_val = uf_intern(lexer->interner, lexer->start, len);
    }
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

                if (lexer->paren_depth == 0) {
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
            if (lexer->paren_depth == 0) {
                lexer->at_line_start = true;
                return make_token(lexer, UF_TOK_NEWLINE);
            }
            continue;
        }

        if (c == '\r') {
            if (peek(lexer) == '\n') advance(lexer);
            if (lexer->paren_depth == 0) {
                lexer->at_line_start = true;
                return make_token(lexer, UF_TOK_NEWLINE);
            }
            continue;
        }

        if (isdigit((unsigned char)c)) {
            return scan_number(lexer);
        }

        if (isalpha((unsigned char)c) || c == '_') {
            return scan_identifier_or_keyword(lexer);
        }

        if (c == '"') {
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
                lexer->paren_depth++;
                return make_token(lexer, UF_TOK_LPAREN);
            case ')':
                if (lexer->paren_depth > 0) lexer->paren_depth--;
                return make_token(lexer, UF_TOK_RPAREN);
            case '[':
                lexer->paren_depth++;
                return make_token(lexer, UF_TOK_LBRACKET);
            case ']':
                if (lexer->paren_depth > 0) lexer->paren_depth--;
                return make_token(lexer, UF_TOK_RBRACKET);
            case '=':
                return make_token(lexer, match(lexer, '=') ? UF_TOK_EQEQ : UF_TOK_EQUAL);
            case '!':
                if (match(lexer, '=')) return make_token(lexer, UF_TOK_BANGEQ);
                return error_token(lexer, "Unexpected character '!' (did you mean '!=' or 'not'?)", "Use 'not' for logical negation, or '!=' for inequality");
            case '<':
                return make_token(lexer, match(lexer, '=') ? UF_TOK_LTEQ : UF_TOK_LT);
            case '>':
                return make_token(lexer, match(lexer, '=') ? UF_TOK_GTEQ : UF_TOK_GT);
        }

        return error_token(lexer, "Unexpected character in source", "Check for non-ASCII characters or unsupported symbols");
    }
}
