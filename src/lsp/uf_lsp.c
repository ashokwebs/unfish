#define _POSIX_C_SOURCE 200809L

#include "uf_lsp.h"
#include "../lexer/uf_lexer.h"
#include "../parser/uf_parser.h"
#include "../semantic/uf_semantic.h"
#include "../formatter/uf_formatter.h"
#include "../common/uf_diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define MAX_DOCS 64

typedef struct {
    char* uri;
    char* text;
    int version;
} LspDoc;

static LspDoc g_docs[MAX_DOCS];
static size_t g_doc_count = 0;

static LspDoc* find_doc(const char* uri) {
    for (size_t i = 0; i < g_doc_count; ++i) {
        if (strcmp(g_docs[i].uri, uri) == 0) return &g_docs[i];
    }
    return NULL;
}

static void upsert_doc(const char* uri, const char* text, int version) {
    LspDoc* d = find_doc(uri);
    if (d) {
        free(d->text);
        d->text = strdup(text);
        d->version = version;
        return;
    }
    if (g_doc_count < MAX_DOCS) {
        g_docs[g_doc_count].uri = strdup(uri);
        g_docs[g_doc_count].text = strdup(text);
        g_docs[g_doc_count].version = version;
        g_doc_count++;
    }
}

static void close_doc(const char* uri) {
    for (size_t i = 0; i < g_doc_count; ++i) {
        if (strcmp(g_docs[i].uri, uri) == 0) {
            free(g_docs[i].uri);
            free(g_docs[i].text);
            g_docs[i] = g_docs[g_doc_count - 1];
            g_doc_count--;
            return;
        }
    }
}

static void free_all_docs(void) {
    for (size_t i = 0; i < g_doc_count; ++i) {
        free(g_docs[i].uri);
        free(g_docs[i].text);
    }
    g_doc_count = 0;
}

/* Framing */
static char* lsp_read_message(FILE* in) {
    char header_buf[256];
    size_t content_length = 0;

    while (fgets(header_buf, sizeof(header_buf), in)) {
        if (strcmp(header_buf, "\r\n") == 0 || strcmp(header_buf, "\n") == 0) {
            break;
        }
        if (strncasecmp(header_buf, "Content-Length:", 15) == 0) {
            content_length = (size_t)strtoul(header_buf + 15, NULL, 10);
        }
    }

    if (content_length == 0) return NULL;

    char* body = (char*)malloc(content_length + 1);
    size_t read_bytes = fread(body, 1, content_length, in);
    body[read_bytes] = '\0';
    return body;
}

static void lsp_send_message(FILE* out, const char* json) {
    size_t len = strlen(json);
    fprintf(out, "Content-Length: %zu\r\n\r\n%s", len, json);
    fflush(out);
}

/* Diagnostic recording */
typedef struct {
    UfDiagKind kind;
    SourceSpan span;
    char message[256];
} RecordedDiag;

typedef struct {
    RecordedDiag diags[128];
    size_t count;
} DiagList;

static void lsp_diag_callback(void* user_data, UfDiagKind kind, SourceSpan span, const char* message, const char* hint) {
    DiagList* list = (DiagList*)user_data;
    if (list->count < 128) {
        RecordedDiag* d = &list->diags[list->count++];
        d->kind = kind;
        d->span = span;
        if (hint && strlen(hint) > 0) {
            snprintf(d->message, sizeof(d->message), "%s (Hint: %s)", message, hint);
        } else {
            snprintf(d->message, sizeof(d->message), "%s", message);
        }
    }
}

static void escape_json_string(const char* in, char* out, size_t out_max) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 2 < out_max; ++i) {
        char c = in[i];
        if (c == '"') { out[o++] = '\\'; out[o++] = '"'; }
        else if (c == '\\') { out[o++] = '\\'; out[o++] = '\\'; }
        else if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (c == '\r') { out[o++] = '\\'; out[o++] = 'r'; }
        else if (c == '\t') { out[o++] = '\\'; out[o++] = 't'; }
        else { out[o++] = c; }
    }
    out[o] = '\0';
}

static void publish_diagnostics(FILE* out, const char* uri, const char* text) {
    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);

    DiagList list;
    list.count = 0;

    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, uri, text);
    reporter.callback = lsp_diag_callback;
    reporter.user_data = &list;

    UfLexer lexer;
    uf_lexer_init(&lexer, uri, text, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* program = uf_parse_program(&parser);

    if (program && !parser.had_error) {
        UfSemanticAnalyzer sema;
        uf_semantic_init(&sema, &arena, &reporter);
        uf_analyze_program(&sema, program);
    }

    /* Build JSON notification */
    size_t buf_cap = 4096;
    char* json = (char*)malloc(buf_cap);
    snprintf(json, buf_cap,
             "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"%s\",\"diagnostics\":[",
             uri);

    for (size_t i = 0; i < list.count; ++i) {
        RecordedDiag* d = &list.diags[i];
        int severity = (d->kind == UF_DIAG_WARNING) ? 2 : 1;
        uint32_t line = d->span.start.line > 0 ? d->span.start.line - 1 : 0;
        uint32_t col = d->span.start.col > 0 ? d->span.start.col - 1 : 0;
        uint32_t end_line = d->span.end.line > 0 ? d->span.end.line - 1 : line;
        uint32_t end_col = d->span.end.col > 0 ? d->span.end.col - 1 : col + 1;

        char escaped[512];
        escape_json_string(d->message, escaped, sizeof(escaped));

        char item[1024];
        snprintf(item, sizeof(item),
                 "%s{\"range\":{\"start\":{\"line\":%u,\"character\":%u},\"end\":{\"line\":%u,\"character\":%u}},\"severity\":%d,\"source\":\"unfish\",\"message\":\"%s\"}",
                 (i > 0) ? "," : "",
                 line, col, end_line, end_col, severity, escaped);

        if (strlen(json) + strlen(item) + 16 >= buf_cap) {
            buf_cap *= 2;
            json = (char*)realloc(json, buf_cap);
        }
        strcat(json, item);
    }

    strcat(json, "]}}");
    lsp_send_message(out, json);
    free(json);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
}

static char* get_word_at_pos(const char* text, uint32_t target_line, uint32_t target_char) {
    uint32_t line = 0;
    const char* p = text;
    while (*p && line < target_line) {
        if (*p == '\n') line++;
        p++;
    }
    if (line != target_line) return NULL;

    const char* line_start = p;
    size_t col = 0;
    while (*p && *p != '\n' && col < target_char) {
        p++;
        col++;
    }

    if (!isalnum((unsigned char)*p) && *p != '_') {
        if (p > line_start && (isalnum((unsigned char)*(p - 1)) || *(p - 1) == '_')) {
            p--;
        } else {
            return NULL;
        }
    }

    const char* start = p;
    while (start > line_start && (isalnum((unsigned char)*(start - 1)) || *(start - 1) == '_')) {
        start--;
    }

    const char* end = p;
    while (*end && (isalnum((unsigned char)*end) || *end == '_')) {
        end++;
    }

    size_t len = (size_t)(end - start);
    if (len == 0) return NULL;

    char* word = (char*)malloc(len + 1);
    memcpy(word, start, len);
    word[len] = '\0';
    return word;
}

static const char* get_hover_doc(const char* word) {
    /* Builtins */
    if (strcmp(word, "say") == 0) return "**function** `say(value: Any): Null`\\n\\nPrints value to standard output followed by a newline.";
    if (strcmp(word, "print") == 0) return "**function** `print(value: Any): Null`\\n\\nPrints value to standard output without a trailing newline.";
    if (strcmp(word, "len") == 0) return "**function** `len(collection: Array|Map|String): Number`\\n\\nReturns number of elements or character count.";
    if (strcmp(word, "type_of") == 0) return "**function** `type_of(value: Any): String`\\n\\nReturns runtime type name string.";
    if (strcmp(word, "push") == 0) return "**function** `push(arr: Array, item: Any): Null`\\n\\nAppends an element to the end of an array.";
    if (strcmp(word, "pop") == 0) return "**function** `pop(arr: Array): Any`\\n\\nRemoves and returns the last element of an array.";
    if (strcmp(word, "abs") == 0) return "**function** `abs(x: Number): Number`\\n\\nReturns absolute value.";
    if (strcmp(word, "sqrt") == 0) return "**function** `sqrt(x: Number): Number`\\n\\nReturns square root.";
    if (strcmp(word, "floor") == 0) return "**function** `floor(x: Number): Number`\\n\\nFloors number to integer.";
    if (strcmp(word, "ceil") == 0) return "**function** `ceil(x: Number): Number`\\n\\nCeils number to integer.";
    if (strcmp(word, "round") == 0) return "**function** `round(x: Number): Number`\\n\\nRounds number to nearest integer.";
    if (strcmp(word, "min") == 0) return "**function** `min(a: Number, b: Number): Number`\\n\\nReturns minimum of two numbers.";
    if (strcmp(word, "max") == 0) return "**function** `max(a: Number, b: Number): Number`\\n\\nReturns maximum of two numbers.";
    if (strcmp(word, "pow") == 0) return "**function** `pow(base: Number, exp: Number): Number`\\n\\nCalculates exponential power.";

    /* Keywords */
    if (strcmp(word, "let") == 0) return "**keyword** `let`\\n\\nDeclares a local or global variable with optional type annotation.";
    if (strcmp(word, "function") == 0) return "**keyword** `function`\\n\\nDefines a named function or anonymous closure.";
    if (strcmp(word, "return") == 0) return "**keyword** `return`\\n\\nExits the current function with an optional return value.";
    if (strcmp(word, "if") == 0) return "**keyword** `if`\\n\\nConditional execution based on truthiness.";
    if (strcmp(word, "else") == 0) return "**keyword** `else`\\n\\nAlternative branch when if condition is false.";
    if (strcmp(word, "while") == 0) return "**keyword** `while`\\n\\nLoops while condition remains truthy.";
    if (strcmp(word, "repeat") == 0) return "**keyword** `repeat`\\n\\nExecutes block a specified integer number of times.";
    if (strcmp(word, "for") == 0) return "**keyword** `for`\\n\\nIterates over items in an array, map, or string.";
    if (strcmp(word, "in") == 0) return "**keyword** `in`\\n\\nSpecifies iterable target in for-in loop.";
    if (strcmp(word, "break") == 0) return "**keyword** `break`\\n\\nTerminates the innermost loop.";
    if (strcmp(word, "continue") == 0) return "**keyword** `continue`\\n\\nSkips to the next iteration of the loop.";
    if (strcmp(word, "try") == 0) return "**keyword** `try`\\n\\nProtected block catching runtime errors.";
    if (strcmp(word, "catch") == 0) return "**keyword** `catch`\\n\\nHandler receiving caught error.";
    if (strcmp(word, "struct") == 0) return "**keyword** `struct`\\n\\nDeclares an immutable record structure definition.";
    if (strcmp(word, "match") == 0) return "**keyword** `match`\\n\\nPattern matching against literals, bindings, and structs.";
    if (strcmp(word, "when") == 0) return "**keyword** `when`\\n\\nGuarded condition on match pattern arm.";
    if (strcmp(word, "import") == 0) return "**keyword** `import`\\n\\nImports module namespace.";
    if (strcmp(word, "from") == 0) return "**keyword** `from`\\n\\nImports specific symbols from module.";
    if (strcmp(word, "as") == 0) return "**keyword** `as`\\n\\nAliasing for imported modules or symbols.";
    if (strcmp(word, "true") == 0) return "**boolean** `true`";
    if (strcmp(word, "false") == 0) return "**boolean** `false`";
    if (strcmp(word, "null") == 0) return "**null** `null`";
    if (strcmp(word, "and") == 0) return "**operator** `and` (Logical AND)";
    if (strcmp(word, "or") == 0) return "**operator** `or` (Logical OR)";
    if (strcmp(word, "not") == 0) return "**operator** `not` (Logical NOT)";

    return NULL;
}

static bool find_ast_symbol_pos(const UfProgram* program, const char* word, SourceSpan* out_span) {
    if (!program || !word) return false;
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* s = program->stmts[i];
        if (s->kind == UF_STMT_FUNCTION && strcmp(s->as.function_stmt.name, word) == 0) {
            *out_span = s->span;
            return true;
        }
        if (s->kind == UF_STMT_LET && strcmp(s->as.let_stmt.name, word) == 0) {
            *out_span = s->span;
            return true;
        }
        if (s->kind == UF_STMT_STRUCT && strcmp(s->as.struct_stmt.name, word) == 0) {
            *out_span = s->span;
            return true;
        }
    }
    return false;
}

int uf_lsp_run(FILE* in, FILE* out) {
    while (true) {
        char* msg = lsp_read_message(in);
        if (!msg) break;

        /* Simple JSON-RPC dispatch */
        if (strstr(msg, "\"method\":\"initialize\"")) {
            char* id_ptr = strstr(msg, "\"id\":");
            long id = id_ptr ? strtol(id_ptr + 5, NULL, 10) : 1;
            char resp[1024];
            snprintf(resp, sizeof(resp),
                     "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{"
                     "\"capabilities\":{"
                     "\"textDocumentSync\":1,"
                     "\"hoverProvider\":true,"
                     "\"definitionProvider\":true,"
                     "\"completionProvider\":{\"triggerCharacters\":[\".\",\"(\"]},"
                     "\"documentFormattingProvider\":true"
                     "}}}", id);
            lsp_send_message(out, resp);
        } else if (strstr(msg, "\"method\":\"initialized\"")) {
            /* No response required */
        } else if (strstr(msg, "\"method\":\"shutdown\"")) {
            char* id_ptr = strstr(msg, "\"id\":");
            long id = id_ptr ? strtol(id_ptr + 5, NULL, 10) : 1;
            char resp[128];
            snprintf(resp, sizeof(resp), "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":null}", id);
            lsp_send_message(out, resp);
        } else if (strstr(msg, "\"method\":\"exit\"")) {
            free(msg);
            break;
        } else if (strstr(msg, "\"method\":\"textDocument/didOpen\"")) {
            char* uri_start = strstr(msg, "\"uri\":\"");
            if (uri_start) {
                uri_start += 7;
                char* uri_end = strchr(uri_start, '"');
                if (uri_end) {
                    size_t uri_len = uri_end - uri_start;
                    char uri[256];
                    if (uri_len < sizeof(uri)) {
                        memcpy(uri, uri_start, uri_len);
                        uri[uri_len] = '\0';
                        char* text_start = strstr(msg, "\"text\":\"");
                        if (text_start) {
                            text_start += 8;
                            /* Unescape text roughly or find ending */
                            char* text_end = strrchr(text_start, '"');
                            if (text_end && text_end > text_start) {
                                size_t tlen = text_end - text_start;
                                char* text = (char*)malloc(tlen + 1);
                                size_t ti = 0;
                                for (size_t si = 0; si < tlen; ++si) {
                                    if (text_start[si] == '\\' && si + 1 < tlen) {
                                        char next = text_start[++si];
                                        if (next == 'n') text[ti++] = '\n';
                                        else if (next == 't') text[ti++] = '\t';
                                        else if (next == 'r') text[ti++] = '\r';
                                        else if (next == '"') text[ti++] = '"';
                                        else if (next == '\\') text[ti++] = '\\';
                                        else text[ti++] = next;
                                    } else {
                                        text[ti++] = text_start[si];
                                    }
                                }
                                text[ti] = '\0';
                                upsert_doc(uri, text, 1);
                                publish_diagnostics(out, uri, text);
                                free(text);
                            }
                        }
                    }
                }
            }
        } else if (strstr(msg, "\"method\":\"textDocument/didChange\"")) {
            char* uri_start = strstr(msg, "\"uri\":\"");
            if (uri_start) {
                uri_start += 7;
                char* uri_end = strchr(uri_start, '"');
                if (uri_end) {
                    size_t uri_len = uri_end - uri_start;
                    char uri[256];
                    if (uri_len < sizeof(uri)) {
                        memcpy(uri, uri_start, uri_len);
                        uri[uri_len] = '\0';
                        char* text_start = strstr(msg, "\"text\":\"");
                        if (text_start) {
                            text_start += 8;
                            char* text_end = strrchr(text_start, '"');
                            if (text_end && text_end > text_start) {
                                size_t tlen = text_end - text_start;
                                char* text = (char*)malloc(tlen + 1);
                                size_t ti = 0;
                                for (size_t si = 0; si < tlen; ++si) {
                                    if (text_start[si] == '\\' && si + 1 < tlen) {
                                        char next = text_start[++si];
                                        if (next == 'n') text[ti++] = '\n';
                                        else if (next == 't') text[ti++] = '\t';
                                        else if (next == 'r') text[ti++] = '\r';
                                        else if (next == '"') text[ti++] = '"';
                                        else if (next == '\\') text[ti++] = '\\';
                                        else text[ti++] = next;
                                    } else {
                                        text[ti++] = text_start[si];
                                    }
                                }
                                text[ti] = '\0';
                                upsert_doc(uri, text, 2);
                                publish_diagnostics(out, uri, text);
                                free(text);
                            }
                        }
                    }
                }
            }
        } else if (strstr(msg, "\"method\":\"textDocument/didClose\"")) {
            char* uri_start = strstr(msg, "\"uri\":\"");
            if (uri_start) {
                uri_start += 7;
                char* uri_end = strchr(uri_start, '"');
                if (uri_end) {
                    size_t uri_len = uri_end - uri_start;
                    char uri[256];
                    if (uri_len < sizeof(uri)) {
                        memcpy(uri, uri_start, uri_len);
                        uri[uri_len] = '\0';
                        close_doc(uri);
                    }
                }
            }
        } else if (strstr(msg, "\"method\":\"textDocument/hover\"")) {
            char* id_ptr = strstr(msg, "\"id\":");
            long id = id_ptr ? strtol(id_ptr + 5, NULL, 10) : 1;
            char* uri_start = strstr(msg, "\"uri\":\"");
            const char* doc_text = NULL;
            if (uri_start) {
                uri_start += 7;
                char* uri_end = strchr(uri_start, '"');
                if (uri_end) {
                    char uri[256];
                    size_t ulen = uri_end - uri_start;
                    if (ulen < sizeof(uri)) {
                        memcpy(uri, uri_start, ulen);
                        uri[ulen] = '\0';
                        LspDoc* d = find_doc(uri);
                        if (d) doc_text = d->text;
                    }
                }
            }

            char* line_ptr = strstr(msg, "\"line\":");
            char* char_ptr = strstr(msg, "\"character\":");
            uint32_t line = line_ptr ? (uint32_t)strtoul(line_ptr + 7, NULL, 10) : 0;
            uint32_t col = char_ptr ? (uint32_t)strtoul(char_ptr + 12, NULL, 10) : 0;

            const char* hover_content = NULL;
            char custom_hover[512];
            custom_hover[0] = '\0';

            if (doc_text) {
                char* word = get_word_at_pos(doc_text, line, col);
                if (word) {
                    hover_content = get_hover_doc(word);
                    if (!hover_content) {
                        snprintf(custom_hover, sizeof(custom_hover), "**identifier** `%s`", word);
                        hover_content = custom_hover;
                    }
                    free(word);
                }
            }

            char resp[1024];
            if (hover_content) {
                snprintf(resp, sizeof(resp),
                         "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{\"contents\":{\"kind\":\"markdown\",\"value\":\"%s\"}}}",
                         id, hover_content);
            } else {
                snprintf(resp, sizeof(resp), "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":null}", id);
            }
            lsp_send_message(out, resp);
        } else if (strstr(msg, "\"method\":\"textDocument/definition\"")) {
            char* id_ptr = strstr(msg, "\"id\":");
            long id = id_ptr ? strtol(id_ptr + 5, NULL, 10) : 1;
            char* uri_start = strstr(msg, "\"uri\":\"");
            char uri[256];
            uri[0] = '\0';
            const char* doc_text = NULL;
            if (uri_start) {
                uri_start += 7;
                char* uri_end = strchr(uri_start, '"');
                if (uri_end) {
                    size_t ulen = uri_end - uri_start;
                    if (ulen < sizeof(uri)) {
                        memcpy(uri, uri_start, ulen);
                        uri[ulen] = '\0';
                        LspDoc* d = find_doc(uri);
                        if (d) doc_text = d->text;
                    }
                }
            }

            char* line_ptr = strstr(msg, "\"line\":");
            char* char_ptr = strstr(msg, "\"character\":");
            uint32_t line = line_ptr ? (uint32_t)strtoul(line_ptr + 7, NULL, 10) : 0;
            uint32_t col = char_ptr ? (uint32_t)strtoul(char_ptr + 12, NULL, 10) : 0;

            SourceSpan def_span;
            bool found = false;
            if (doc_text) {
                char* word = get_word_at_pos(doc_text, line, col);
                if (word) {
                    UfArena arena;
                    uf_arena_init(&arena, 16384);
                    UfInterner interner;
                    uf_interner_init(&interner, &arena);
                    UfDiagnosticReporter reporter;
                    uf_diag_reporter_init(&reporter, uri, doc_text);
                    UfLexer lexer;
                    uf_lexer_init(&lexer, uri, doc_text, &arena, &interner, &reporter);
                    UfParser parser;
                    uf_parser_init(&parser, &lexer, &arena, &reporter);
                    UfProgram* prog = uf_parse_program(&parser);
                    if (prog) {
                        found = find_ast_symbol_pos(prog, word, &def_span);
                    }
                    uf_interner_free(&interner);
                    uf_arena_free(&arena);
                    free(word);
                }
            }

            char resp[512];
            if (found) {
                uint32_t dline = def_span.start.line > 0 ? def_span.start.line - 1 : 0;
                uint32_t dcol = def_span.start.col > 0 ? def_span.start.col - 1 : 0;
                snprintf(resp, sizeof(resp),
                         "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%u,\"character\":%u},\"end\":{\"line\":%u,\"character\":%u}}}}",
                         id, uri, dline, dcol, dline, dcol + 5);
            } else {
                snprintf(resp, sizeof(resp), "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":null}", id);
            }
            lsp_send_message(out, resp);
        } else if (strstr(msg, "\"method\":\"textDocument/completion\"")) {
            char* id_ptr = strstr(msg, "\"id\":");
            long id = id_ptr ? strtol(id_ptr + 5, NULL, 10) : 1;

            const char* items =
                "{\"label\":\"let\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"function\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"return\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"if\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"else\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"while\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"repeat\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"for\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"in\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"break\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"continue\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"try\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"catch\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"struct\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"match\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"when\",\"kind\":14,\"detail\":\"keyword\"},"
                "{\"label\":\"say\",\"kind\":3,\"detail\":\"builtin function: say(val)\"},"
                "{\"label\":\"print\",\"kind\":3,\"detail\":\"builtin function: print(val)\"},"
                "{\"label\":\"len\",\"kind\":3,\"detail\":\"builtin function: len(collection)\"},"
                "{\"label\":\"type_of\",\"kind\":3,\"detail\":\"builtin function: type_of(val)\"},"
                "{\"label\":\"push\",\"kind\":3,\"detail\":\"builtin function: push(arr, item)\"},"
                "{\"label\":\"pop\",\"kind\":3,\"detail\":\"builtin function: pop(arr)\"},"
                "{\"label\":\"abs\",\"kind\":3,\"detail\":\"math: abs(x)\"},"
                "{\"label\":\"sqrt\",\"kind\":3,\"detail\":\"math: sqrt(x)\"},"
                "{\"label\":\"floor\",\"kind\":3,\"detail\":\"math: floor(x)\"},"
                "{\"label\":\"ceil\",\"kind\":3,\"detail\":\"math: ceil(x)\"},"
                "{\"label\":\"round\",\"kind\":3,\"detail\":\"math: round(x)\"},"
                "{\"label\":\"min\",\"kind\":3,\"detail\":\"math: min(a, b)\"},"
                "{\"label\":\"max\",\"kind\":3,\"detail\":\"math: max(a, b)\"},"
                "{\"label\":\"pow\",\"kind\":3,\"detail\":\"math: pow(b, exp)\"}";

            char resp[4096];
            snprintf(resp, sizeof(resp),
                     "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{\"isIncomplete\":false,\"items\":[%s]}}",
                     id, items);
            lsp_send_message(out, resp);
        } else if (strstr(msg, "\"method\":\"textDocument/formatting\"")) {
            char* id_ptr = strstr(msg, "\"id\":");
            long id = id_ptr ? strtol(id_ptr + 5, NULL, 10) : 1;
            char* uri_start = strstr(msg, "\"uri\":\"");
            const char* doc_text = NULL;
            if (uri_start) {
                uri_start += 7;
                char* uri_end = strchr(uri_start, '"');
                if (uri_end) {
                    char uri[256];
                    size_t ulen = uri_end - uri_start;
                    if (ulen < sizeof(uri)) {
                        memcpy(uri, uri_start, ulen);
                        uri[ulen] = '\0';
                        LspDoc* d = find_doc(uri);
                        if (d) doc_text = d->text;
                    }
                }
            }

            char* formatted = NULL;
            size_t line_count = 0;
            if (doc_text) {
                for (const char* p = doc_text; *p; ++p) if (*p == '\n') line_count++;
                line_count++;

                UfArena arena;
                uf_arena_init(&arena, 16384);
                UfInterner interner;
                uf_interner_init(&interner, &arena);
                UfDiagnosticReporter reporter;
                uf_diag_reporter_init(&reporter, "fmt", doc_text);
                UfLexer lexer;
                uf_lexer_init(&lexer, "fmt", doc_text, &arena, &interner, &reporter);
                UfParser parser;
                uf_parser_init(&parser, &lexer, &arena, &reporter);
                UfProgram* prog = uf_parse_program(&parser);
                if (prog && !parser.had_error) {
                    formatted = uf_format_program(prog);
                }
                uf_interner_free(&interner);
                uf_arena_free(&arena);
            }

            if (formatted) {
                size_t flen = strlen(formatted);
                char* esc_fmt = (char*)malloc(flen * 2 + 1);
                escape_json_string(formatted, esc_fmt, flen * 2 + 1);

                size_t resp_cap = flen * 2 + 256;
                char* resp = (char*)malloc(resp_cap);
                snprintf(resp, resp_cap,
                         "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":%zu,\"character\":0}},\"newText\":\"%s\"}]}",
                         id, line_count + 1, esc_fmt);
                lsp_send_message(out, resp);
                free(resp);
                free(esc_fmt);
                free(formatted);
            } else {
                char resp[128];
                snprintf(resp, sizeof(resp), "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":[]}", id);
                lsp_send_message(out, resp);
            }
        } else {
            /* Unknown request: if it has an id, return method not found */
            char* id_ptr = strstr(msg, "\"id\":");
            if (id_ptr) {
                long id = strtol(id_ptr + 5, NULL, 10);
                char resp[128];
                snprintf(resp, sizeof(resp), "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":null}", id);
                lsp_send_message(out, resp);
            }
        }

        free(msg);
    }

    free_all_docs();
    return 0;
}
