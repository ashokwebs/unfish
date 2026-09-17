#include "uf_doc.h"
#include "../ast/uf_ast.h"
#include "../parser/uf_parser.h"
#include "../lexer/uf_lexer.h"
#include "../common/uf_arena.h"
#include "../common/uf_diagnostic.h"
#include "../common/uf_string.h"

#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_DOC_LINES 4096

typedef struct {
    uint32_t target_line; /* The line this docstring applies to */
    char* text;
} DocEntry;

typedef struct {
    char* module_doc;
    DocEntry entries[MAX_DOC_LINES];
    size_t count;
} DocIndex;

static char* read_file_content(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0L, SEEK_END);
    long size = ftell(file);
    rewind(file);
    if (size < 0) { fclose(file); return NULL; }
    char* buf = (char*)malloc(size + 1);
    if (!buf) { fclose(file); return NULL; }
    size_t r = fread(buf, 1, size, file);
    buf[r] = '\0';
    fclose(file);
    return buf;
}

static void doc_index_free(DocIndex* idx) {
    if (idx->module_doc) free(idx->module_doc);
    for (size_t i = 0; i < idx->count; ++i) {
        if (idx->entries[i].text) free(idx->entries[i].text);
    }
}

static const char* doc_index_lookup(const DocIndex* idx, uint32_t line) {
    for (size_t i = 0; i < idx->count; ++i) {
        if (idx->entries[i].target_line == line) {
            return idx->entries[i].text;
        }
    }
    return NULL;
}

static void parse_doc_comments(const char* source, DocIndex* idx) {
    idx->module_doc = NULL;
    idx->count = 0;

    /* Split lines */
    size_t line_count = 0;
    const char* p = source;
    while (*p) {
        if (*p == '\n') line_count++;
        p++;
    }
    line_count += 2;

    char** lines = (char**)calloc(line_count, sizeof(char*));
    size_t total_lines = 0;

    const char* start = source;
    p = source;
    while (*p) {
        if (*p == '\n') {
            size_t len = p - start;
            if (len > 0 && *(p - 1) == '\r') len--;
            char* line = (char*)malloc(len + 1);
            memcpy(line, start, len);
            line[len] = '\0';
            lines[total_lines++] = line;
            start = p + 1;
        }
        p++;
    }
    if (*start) {
        size_t len = strlen(start);
        if (len > 0 && start[len - 1] == '\r') len--;
        char* line = (char*)malloc(len + 1);
        memcpy(line, start, len);
        line[len] = '\0';
        lines[total_lines++] = line;
    }

    bool seen_code = false;
    size_t i = 0;
    while (i < total_lines) {
        /* Check if line starts with ## */
        const char* l = lines[i];
        while (*l == ' ' || *l == '\t') l++;

        if (strncmp(l, "##", 2) == 0) {
            /* Collect continuous ## block */
            UfStrBuf doc_buf;
            uf_strbuf_init(&doc_buf);

            while (i < total_lines) {
                const char* cur = lines[i];
                while (*cur == ' ' || *cur == '\t') cur++;
                if (strncmp(cur, "##", 2) != 0) break;
                cur += 2;
                if (*cur == ' ') cur++;
                uf_strbuf_append(&doc_buf, cur);
                uf_strbuf_append_char(&doc_buf, '\n');
                i++;
            }

            char* doc_text = uf_strbuf_detach(&doc_buf);

            /* A leading '##' block belongs to the declaration it sits directly
             * on top of; it is the module's own docstring only when a blank
             * line separates it from whatever follows. This test used to be
             * computed and then discarded, so the first documented declaration
             * in any file without a separate module docstring silently lost
             * its documentation to the file header. */
            bool attached_to_decl = false;
            if (i < total_lines) {
                const char* next = lines[i];
                while (*next == ' ' || *next == '\t') next++;
                if (*next != '\0' && *next != '#') attached_to_decl = true;
            }

            if (!seen_code && idx->module_doc == NULL && !attached_to_decl) {
                idx->module_doc = doc_text;
            } else {
                /* Associate with next non-blank, non-comment line */
                size_t next_line = i;
                while (next_line < total_lines) {
                    const char* nl = lines[next_line];
                    while (*nl == ' ' || *nl == '\t') nl++;
                    if (*nl != '\0' && *nl != '#') break;
                    next_line++;
                }
                if (next_line < total_lines && idx->count < MAX_DOC_LINES) {
                    idx->entries[idx->count].target_line = (uint32_t)(next_line + 1);
                    idx->entries[idx->count].text = doc_text;
                    idx->count++;
                } else {
                    free(doc_text);
                }
            }
        } else {
            if (*l != '\0' && *l != '#') {
                seen_code = true;
            }
            i++;
        }
    }

    for (size_t l = 0; l < total_lines; ++l) {
        free(lines[l]);
    }
    free(lines);
}

static void emit_markdown(FILE* out, const char* title, const DocIndex* doc_idx, const UfProgram* program) {
    fprintf(out, "# %s\n\n", title ? title : "API Reference");

    if (doc_idx->module_doc) {
        fprintf(out, "%s\n\n", doc_idx->module_doc);
    }

    /* Collect symbols */
    size_t struct_count = 0;
    size_t enum_count = 0;
    size_t fn_count = 0;

    for (size_t i = 0; i < program->count; ++i) {
        if (program->stmts[i]->kind == UF_STMT_STRUCT) struct_count++;
        else if (program->stmts[i]->kind == UF_STMT_ENUM) enum_count++;
        else if (program->stmts[i]->kind == UF_STMT_FUNCTION) fn_count++;
    }

    fprintf(out, "## Table of Contents\n\n");
    if (struct_count > 0) {
        fprintf(out, "- [Structs](#structs)\n");
        for (size_t i = 0; i < program->count; ++i) {
            if (program->stmts[i]->kind == UF_STMT_STRUCT) {
                fprintf(out, "  - [`%s`](#struct-%s)\n", program->stmts[i]->as.struct_stmt.name, program->stmts[i]->as.struct_stmt.name);
            }
        }
    }
    if (enum_count > 0) {
        fprintf(out, "- [Enums](#enums)\n");
        for (size_t i = 0; i < program->count; ++i) {
            if (program->stmts[i]->kind == UF_STMT_ENUM) {
                fprintf(out, "  - [`%s`](#enum-%s)\n", program->stmts[i]->as.enum_stmt.name, program->stmts[i]->as.enum_stmt.name);
            }
        }
    }
    if (fn_count > 0) {
        fprintf(out, "- [Functions](#functions)\n");
        for (size_t i = 0; i < program->count; ++i) {
            if (program->stmts[i]->kind == UF_STMT_FUNCTION) {
                fprintf(out, "  - [`%s()`](#function-%s)\n", program->stmts[i]->as.function_stmt.name, program->stmts[i]->as.function_stmt.name);
            }
        }
    }
    fprintf(out, "\n---\n\n");

    /* Structs */
    if (struct_count > 0) {
        fprintf(out, "## Structs\n\n");
        for (size_t i = 0; i < program->count; ++i) {
            UfStmt* stmt = program->stmts[i];
            if (stmt->kind != UF_STMT_STRUCT) continue;

            fprintf(out, "### `struct %s`\n\n", stmt->as.struct_stmt.name);
            const char* doc = doc_index_lookup(doc_idx, stmt->span.start.line);
            if (doc) fprintf(out, "%s\n", doc);

            if (stmt->as.struct_stmt.field_count > 0) {
                fprintf(out, "#### Fields\n\n");
                fprintf(out, "| Field | Type |\n|---|---|\n");
                for (size_t f = 0; f < stmt->as.struct_stmt.field_count; ++f) {
                    const char* ftype = stmt->as.struct_stmt.field_types[f];
                    fprintf(out, "| `%s` | `%s` |\n", stmt->as.struct_stmt.field_names[f], ftype ? ftype : "any");
                }
                fprintf(out, "\n");
            }

            if (stmt->as.struct_stmt.method_count > 0) {
                fprintf(out, "#### Methods\n\n");
                for (size_t m = 0; m < stmt->as.struct_stmt.method_count; ++m) {
                    UfStmt* mstmt = stmt->as.struct_stmt.methods[m];
                    fprintf(out, "##### `fn %s(", mstmt->as.function_stmt.name);
                    for (size_t p = 0; p < mstmt->as.function_stmt.param_count; ++p) {
                        if (p > 0) fprintf(out, ", ");
                        fprintf(out, "%s", mstmt->as.function_stmt.params[p]);
                        if (mstmt->as.function_stmt.param_types[p]) {
                            fprintf(out, ": %s", mstmt->as.function_stmt.param_types[p]);
                        }
                    }
                    fprintf(out, ")`");
                    if (mstmt->as.function_stmt.return_type) {
                        fprintf(out, " -> `%s`", mstmt->as.function_stmt.return_type);
                    }
                    fprintf(out, "\n\n");
                    const char* mdoc = doc_index_lookup(doc_idx, mstmt->span.start.line);
                    if (mdoc) fprintf(out, "%s\n", mdoc);
                }
            }
            fprintf(out, "\n---\n\n");
        }
    }

    /* Enums */
    if (enum_count > 0) {
        fprintf(out, "## Enums\n\n");
        for (size_t i = 0; i < program->count; ++i) {
            UfStmt* stmt = program->stmts[i];
            if (stmt->kind != UF_STMT_ENUM) continue;

            fprintf(out, "### `enum %s`\n\n", stmt->as.enum_stmt.name);
            const char* doc = doc_index_lookup(doc_idx, stmt->span.start.line);
            if (doc) fprintf(out, "%s\n", doc);

            fprintf(out, "#### Variants\n\n");
            for (size_t v = 0; v < stmt->as.enum_stmt.variant_count; ++v) {
                const UfEnumVariant* var = &stmt->as.enum_stmt.variants[v];
                fprintf(out, "- **`%s`**", var->name);
                if (var->field_count > 0) {
                    fprintf(out, "(");
                    for (size_t f = 0; f < var->field_count; ++f) {
                        if (f > 0) fprintf(out, ", ");
                        fprintf(out, "%s", var->field_names[f]);
                        if (var->field_types && var->field_types[f]) {
                            fprintf(out, ": %s", var->field_types[f]);
                        }
                    }
                    fprintf(out, ")");
                }
                fprintf(out, "\n");
            }
            fprintf(out, "\n---\n\n");
        }
    }

    /* Functions */
    if (fn_count > 0) {
        fprintf(out, "## Functions\n\n");
        for (size_t i = 0; i < program->count; ++i) {
            UfStmt* stmt = program->stmts[i];
            if (stmt->kind != UF_STMT_FUNCTION) continue;

            fprintf(out, "### `function %s(", stmt->as.function_stmt.name);
            for (size_t p = 0; p < stmt->as.function_stmt.param_count; ++p) {
                if (p > 0) fprintf(out, ", ");
                if (stmt->as.function_stmt.has_rest && p == stmt->as.function_stmt.param_count - 1) {
                    fprintf(out, "...");
                }
                fprintf(out, "%s", stmt->as.function_stmt.params[p]);
                if (stmt->as.function_stmt.param_types[p]) {
                    fprintf(out, ": %s", stmt->as.function_stmt.param_types[p]);
                }
            }
            fprintf(out, ")`");
            if (stmt->as.function_stmt.return_type) {
                fprintf(out, " -> `%s`", stmt->as.function_stmt.return_type);
            }
            fprintf(out, "\n\n");

            const char* doc = doc_index_lookup(doc_idx, stmt->span.start.line);
            if (doc) fprintf(out, "%s\n", doc);
            fprintf(out, "\n---\n\n");
        }
    }
}

static void emit_html(FILE* out, const char* title, const DocIndex* doc_idx, const UfProgram* program) {
    fprintf(out, "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n");
    fprintf(out, "<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n");
    fprintf(out, "<title>%s</title>\n", title ? title : "Unfish Documentation");
    fprintf(out, "<style>\n");
    fprintf(out, "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; line-height: 1.6; color: #e2e8f0; background: #0f172a; margin: 0; padding: 2rem; }\n");
    fprintf(out, ".container { max-width: 900px; margin: 0 auto; }\n");
    fprintf(out, "h1, h2, h3, h4 { color: #38bdf8; }\n");
    fprintf(out, "code { background: #1e293b; padding: 0.2rem 0.4rem; border-radius: 4px; font-family: 'Fira Code', monospace; color: #a5f3fc; }\n");
    fprintf(out, "pre { background: #1e293b; padding: 1rem; border-radius: 8px; overflow-x: auto; }\n");
    fprintf(out, "table { width: 100%%; border-collapse: collapse; margin: 1rem 0; }\n");
    fprintf(out, "th, td { border: 1px solid #334155; padding: 0.5rem 1rem; text-align: left; }\n");
    fprintf(out, "th { background: #1e293b; color: #94a3b8; }\n");
    fprintf(out, "hr { border: 0; border-top: 1px solid #334155; margin: 2rem 0; }\n");
    fprintf(out, "a { color: #38bdf8; text-decoration: none; }\n");
    fprintf(out, "a:hover { text-decoration: underline; }\n");
    fprintf(out, "</style>\n</head>\n<body>\n<div class=\"container\">\n");

    fprintf(out, "<h1>%s</h1>\n", title ? title : "Unfish Documentation");
    if (doc_idx->module_doc) {
        fprintf(out, "<p>%s</p>\n", doc_idx->module_doc);
    }

    fprintf(out, "<hr>\n");

    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_STRUCT) {
            fprintf(out, "<h2>struct <code>%s</code></h2>\n", stmt->as.struct_stmt.name);
            const char* doc = doc_index_lookup(doc_idx, stmt->span.start.line);
            if (doc) fprintf(out, "<p>%s</p>\n", doc);
            if (stmt->as.struct_stmt.field_count > 0) {
                fprintf(out, "<table><tr><th>Field</th><th>Type</th></tr>\n");
                for (size_t f = 0; f < stmt->as.struct_stmt.field_count; ++f) {
                    const char* ft = stmt->as.struct_stmt.field_types[f];
                    fprintf(out, "<tr><td><code>%s</code></td><td><code>%s</code></td></tr>\n",
                            stmt->as.struct_stmt.field_names[f], ft ? ft : "any");
                }
                fprintf(out, "</table>\n");
            }
        } else if (stmt->kind == UF_STMT_ENUM) {
            fprintf(out, "<h2>enum <code>%s</code></h2>\n", stmt->as.enum_stmt.name);
            const char* doc = doc_index_lookup(doc_idx, stmt->span.start.line);
            if (doc) fprintf(out, "<p>%s</p>\n", doc);
            fprintf(out, "<ul>\n");
            for (size_t v = 0; v < stmt->as.enum_stmt.variant_count; ++v) {
                const UfEnumVariant* var = &stmt->as.enum_stmt.variants[v];
                fprintf(out, "<li><code>%s</code></li>\n", var->name);
            }
            fprintf(out, "</ul>\n");
        } else if (stmt->kind == UF_STMT_FUNCTION) {
            fprintf(out, "<h2>function <code>%s()</code></h2>\n", stmt->as.function_stmt.name);
            const char* doc = doc_index_lookup(doc_idx, stmt->span.start.line);
            if (doc) fprintf(out, "<p>%s</p>\n", doc);
        }
    }

    fprintf(out, "</div>\n</body>\n</html>\n");
}

int uf_doc_generate(const char* target_path, const UfDocOptions* options) {
    if (!target_path) {
        fprintf(stderr, "Error: Expected file path for documentation generation\n");
        return 1;
    }

    char* source = read_file_content(target_path);
    if (!source) {
        fprintf(stderr, "Error: Could not read file '%s'\n", target_path);
        return 1;
    }

    UfArena arena;
    uf_arena_init(&arena, 65536);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, target_path, source);

    UfLexer lexer;
    uf_lexer_init(&lexer, target_path, source, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);

    UfProgram* program = uf_parse_program(&parser);
    if (!program || reporter.error_count > 0) {
        fprintf(stderr, "Error: Could not parse '%s' for documentation\n", target_path);
        uf_interner_free(&interner);
        uf_arena_free(&arena);
        free(source);
        return 1;
    }

    DocIndex doc_idx;
    parse_doc_comments(source, &doc_idx);

    FILE* out = stdout;
    bool must_close = false;
    if (options && options->output_path) {
        out = fopen(options->output_path, "w");
        if (!out) {
            fprintf(stderr, "Error: Could not open output file '%s'\n", options->output_path);
            doc_index_free(&doc_idx);
            uf_interner_free(&interner);
            uf_arena_free(&arena);
            free(source);
            return 1;
        }
        must_close = true;
    }

    const char* title = (options && options->title) ? options->title : target_path;
    UfDocFormat fmt = options ? options->format : UF_DOC_FORMAT_MARKDOWN;

    if (fmt == UF_DOC_FORMAT_HTML) {
        emit_html(out, title, &doc_idx, program);
    } else {
        emit_markdown(out, title, &doc_idx, program);
    }

    if (must_close) {
        fclose(out);
        printf("Documentation successfully generated: %s\n", options->output_path);
    }

    doc_index_free(&doc_idx);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    free(source);
    return 0;
}
