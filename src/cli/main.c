#include "../common/uf_common.h"
#include "../common/uf_source.h"
#include "../common/uf_arena.h"
#include "../common/uf_string.h"
#include "../common/uf_diagnostic.h"
#include "../lexer/uf_token.h"
#include "../lexer/uf_lexer.h"
#include "../ast/uf_ast.h"
#include "../parser/uf_parser.h"
#include "../semantic/uf_semantic.h"
#include "../runtime/uf_runtime.h"
#include "../interpreter/uf_interpreter.h"

static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not open file '%s'\n", path);
        return NULL;
    }

    fseek(file, 0L, SEEK_END);
    long size = ftell(file);
    rewind(file);

    if (size < 0) {
        fclose(file);
        return NULL;
    }

    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fprintf(stderr, "Error: Out of memory reading file '%s'\n", path);
        fclose(file);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, size, file);
    buffer[read_bytes] = '\0';
    fclose(file);
    return buffer;
}

static void print_usage(const char* prog) {
    printf("Unfish — Serious Programming Language & Runtime (v%s)\n\n", UF_VERSION_STRING);
    printf("Usage:\n");
    printf("  %s run <file.unfish>     Execute an Unfish program\n", prog);
    printf("  %s check <file.unfish>   Check program syntax and semantic analysis\n", prog);
    printf("  %s ast <file.unfish>     Dump parsed Abstract Syntax Tree\n", prog);
    printf("  %s tokens <file.unfish>  Scan and print token stream\n", prog);
    printf("  %s repl                  Launch interactive REPL\n", prog);
    printf("  %s version               Display version and build information\n", prog);
    printf("  %s <file.unfish>         Shorthand for 'run <file.unfish>'\n", prog);
}

static int cmd_tokens(const char* file_path) {
    char* source = read_file(file_path);
    if (!source) return 1;

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, file_path, source);

    UfLexer lexer;
    uf_lexer_init(&lexer, file_path, source, &arena, &interner, &reporter);

    for (;;) {
        UfToken tok = uf_lexer_next_token(&lexer);
        uf_token_print(&tok, stdout);
        if (tok.kind == UF_TOK_EOF) break;
    }

    int exit_code = (reporter.error_count > 0) ? 1 : 0;
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    free(source);
    return exit_code;
}

static int cmd_ast(const char* file_path) {
    char* source = read_file(file_path);
    if (!source) return 1;

    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, file_path, source);

    UfLexer lexer;
    uf_lexer_init(&lexer, file_path, source, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);

    UfProgram* program = uf_parse_program(&parser);
    int exit_code = 0;

    if (parser.had_error || reporter.error_count > 0) {
        exit_code = 1;
    } else {
        uf_ast_print(program, stdout);
    }

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    free(source);
    return exit_code;
}

static int cmd_check(const char* file_path) {
    char* source = read_file(file_path);
    if (!source) return 1;

    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, file_path, source);

    UfLexer lexer;
    uf_lexer_init(&lexer, file_path, source, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);

    UfProgram* program = uf_parse_program(&parser);
    if (parser.had_error || reporter.error_count > 0) {
        uf_interner_free(&interner);
        uf_arena_free(&arena);
        free(source);
        return 1;
    }

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool ok = uf_analyze_program(&sema, program);

    int exit_code = (ok && reporter.error_count == 0) ? 0 : 2;
    if (exit_code == 0) {
        printf("✓ Check succeeded: %s is syntactically and semantically valid.\n", file_path);
    }

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    free(source);
    return exit_code;
}

static int cmd_run(const char* file_path, int script_argc, char** script_argv) {
    char* source = read_file(file_path);
    if (!source) {
        fprintf(stderr, "Error: Could not open or read file '%s'\n", file_path);
        return 66;
    }

    UfArena arena;
    uf_arena_init(&arena, 65536);
    UfInterner interner;
    uf_interner_init(&interner, &arena);

    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, file_path, source);

    UfLexer lexer;
    uf_lexer_init(&lexer, file_path, source, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);

    UfProgram* program = uf_parse_program(&parser);
    if (parser.had_error || reporter.error_count > 0) {
        uf_interner_free(&interner);
        uf_arena_free(&arena);
        free(source);
        return 1;
    }

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    if (!uf_analyze_program(&sema, program) || reporter.error_count > 0) {
        uf_interner_free(&interner);
        uf_arena_free(&arena);
        free(source);
        return 2;
    }

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);
    uf_runtime_set_args(&rt, script_argc, script_argv);

    UfInterpretResult result = uf_interpret_program(&rt, program);
    int exit_code = (result == UF_INTERPRET_OK && !rt.had_runtime_error) ? 0 : 3;

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    free(source);
    return exit_code;
}

static void cmd_repl(void) {
    printf("Unfish %s REPL (type 'exit' or Ctrl+D to quit)\n", UF_VERSION_STRING);

    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<repl>", NULL);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfArena arena;
    uf_arena_init(&arena, 65536);
    UfInterner interner;
    uf_interner_init(&interner, &arena);

    char line[1024];

    for (;;) {
        printf("unfish> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        /* Check for exit */
        if (strncmp(line, "exit", 4) == 0 && (line[4] == '\n' || line[4] == '\0')) {
            break;
        }

        /* Check if block input required (ends with :) */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ')) {
            len--;
        }

        UfStrBuf input_buf;
        uf_strbuf_init(&input_buf);
        uf_strbuf_append(&input_buf, line);

        if (len > 0 && line[len - 1] == ':') {
            /* Multiline block entry */
            char block_line[1024];
            for (;;) {
                printf("  ... > ");
                fflush(stdout);
                if (!fgets(block_line, sizeof(block_line), stdin)) break;
                if (block_line[0] == '\n' || block_line[0] == '\r') {
                    break;
                }
                uf_strbuf_append(&input_buf, block_line);
            }
        }

        char* persistent_src = uf_arena_strdup(&arena, input_buf.data ? input_buf.data : "");
        uf_strbuf_free(&input_buf);

        reporter.source_text = persistent_src;
        reporter.error_count = 0;
        rt.had_runtime_error = false;

        UfLexer lexer;
        uf_lexer_init(&lexer, "<repl>", persistent_src, &arena, &interner, &reporter);

        UfParser parser;
        uf_parser_init(&parser, &lexer, &arena, &reporter);

        UfProgram* program = uf_parse_program(&parser);

        if (!parser.had_error && reporter.error_count == 0) {
            /* If single expression statement, evaluate and display value directly */
            if (program->count == 1 && program->stmts[0]->kind == UF_STMT_EXPR) {
                UfValue val = uf_evaluate_expression(&rt, rt.global_env, program->stmts[0]->as.expr_stmt.expr);
                if (!rt.had_runtime_error && val.kind != UF_VAL_NULL) {
                    char* val_str = uf_val_to_string(val);
                    printf("=> %s\n", val_str);
                    free(val_str);
                }
            } else {
                uf_interpret_program(&rt, program);
            }
        }
    }

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage("unfish");
        return 64;
    }

    const char* cmd = argv[1];

    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
        printf("unfish version %s (x86_64-linux, ANSI C99)\n", UF_VERSION_STRING);
        return 0;
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage("unfish");
        return 0;
    }

    if (strcmp(cmd, "repl") == 0) {
        cmd_repl();
        return 0;
    }

    if (strcmp(cmd, "tokens") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Expected file path for 'tokens'\n");
            return 64;
        }
        return cmd_tokens(argv[2]);
    }

    if (strcmp(cmd, "ast") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Expected file path for 'ast'\n");
            return 64;
        }
        return cmd_ast(argv[2]);
    }

    if (strcmp(cmd, "check") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Expected file path for 'check'\n");
            return 64;
        }
        return cmd_check(argv[2]);
    }

    if (strcmp(cmd, "run") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Expected file path for 'run'\n");
            return 64;
        }
        return cmd_run(argv[2], argc - 2, argv + 2);
    }

    /* Shorthand: unfish <file.unfish> */
    if (argv[1][0] != '-') {
        return cmd_run(argv[1], argc - 1, argv + 1);
    }

    fprintf(stderr, "Error: Unknown command or option '%s'\n", argv[1]);
    print_usage("unfish");
    return 64;
}
