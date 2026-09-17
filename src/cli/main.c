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
#include "../formatter/uf_formatter.h"
#include "../debugger/uf_debugger.h"
#include "../blocks/uf_blocks.h"
#include "../compiler/uf_compiler.h"
#include "../vm/uf_vm.h"
#include "../vm/uf_disasm.h"
#include "../codegen/uf_emit_c.h"
#include "../lsp/uf_lsp.h"
#include "../tooling/uf_test_runner.h"
#include "../tooling/uf_doc.h"
#include "../tooling/uf_pkg.h"
#include "../tooling/uf_learn.h"
#include "../tooling/uf_playground.h"
#include "../vm2/uf_regvm.h"
#include "../compiler/uf_reg_compiler.h"
#include "../compiler/uf_cache.h"
#include "../tooling/uf_profiler.h"
#include <unistd.h>
#include <sys/wait.h>


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
        fprintf(stderr, "Error: Could not determine size of '%s'\n", path);
        return NULL;
    }

    char* buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        fprintf(stderr, "Error: Out of memory reading '%s'\n", path);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, (size_t)size, file);
    buffer[bytes_read] = '\0';
    fclose(file);
    return buffer;
}

static void print_usage(const char* prog) {
    printf("Unfish — Serious Programming Language & Runtime (v%s)\n\n", UF_VERSION_STRING);
    printf("Usage:\n");
    printf("  %s run [--strict] [--vm] [--regvm] [--wasm] [--profile] [--no-cache] [--debug] <file.unfish> Execute program (AST, VM, RegVM, or WASM)\n", prog);
    printf("  %s check [--strict] <file.unfish> Check program syntax and semantic analysis\n", prog);
    printf("  %s format [-i|--in-place] [--check] <file.unfish> Format source code\n", prog);
    printf("  %s debug <file.unfish>            Run interactive step debugger\n", prog);
    printf("  %s trace <file.unfish>            Emit JSON execution trace\n", prog);
    printf("  %s blocks-export <file.unfish>    Export AST to visual JSON blocks\n", prog);
    printf("  %s blocks-import <file.json>      Import visual JSON blocks to source\n", prog);
    printf("  %s compile [--regvm] [--cache] [-o <out>] <file.unfish> Compile program to bytecode (.ufc or .ufrc)\n", prog);
    printf("  %s disasm <file.unfish>           Disassemble bytecode for file and child functions\n", prog);
    printf("  %s emit-c [--embedded] [-o <out.c>] <file.unfish> Transpile program to standalone C99\n", prog);
    printf("  %s build [--wasm] [--embedded] [--arm] [-o <output>] <file.unfish> Compile program to native, WebAssembly, or ARM executable\n", prog);
    printf("  %s ast <file.unfish>              Dump parsed Abstract Syntax Tree\n", prog);
    printf("  %s tokens <file.unfish>           Scan and print token stream\n", prog);
    printf("  %s test [path] [--vm] [--regvm] [--strict] [--filter <pat>] Run test suites\n", prog);
    printf("  %s doc [path] [-o <out>] [--format md|html] Generate API documentation\n", prog);
    printf("  %s pkg <init|check|run|test|build> Manage Unfish packages\n", prog);
    printf("  %s studio [--port <p>]            Launch Unfish Studio browser IDE\n", prog);
    printf("  %s learn [--serve|--web] [id]     Launch interactive tutorial (CLI or web)\n", prog);
    printf("  %s playground [--port <p>]        Launch interactive Web Playground\n", prog);
    printf("  %s lsp                            Launch Language Server Protocol (LSP) server\n", prog);
    printf("  %s repl                           Launch interactive REPL\n", prog);
    printf("  %s version                        Display version and build information\n", prog);
    printf("  %s [--strict] [--vm] [--regvm] [--wasm] [--profile] [--no-cache] [--debug] <file.unfish> Shorthand for 'run <file.unfish>'\n", prog);
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

static int cmd_check(const char* file_path, bool strict) {
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
    sema.strict_mode = strict;
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

static int cmd_format(int argc, char** argv) {
    bool in_place = false;
    bool check_only = false;
    const char* file_path = NULL;

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--in-place") == 0) {
            in_place = true;
        } else if (strcmp(argv[i], "--check") == 0) {
            check_only = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown format option '%s'\n", argv[i]);
            return 64;
        } else {
            file_path = argv[i];
        }
    }

    if (!file_path) {
        fprintf(stderr, "Error: Expected file path for 'format'\n");
        return 64;
    }

    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, file_path, "");

    bool ok = uf_format_file(file_path, in_place, check_only, &reporter);
    if (!ok) {
        if (check_only) {
            fprintf(stderr, "Unformatted: %s\n", file_path);
        }
        return 1;
    }
    if (check_only) {
        printf("✓ Cleanly formatted: %s\n", file_path);
    }
    return 0;
}

static int cmd_trace(const char* file_path) {
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

    if (parser.had_error || !program) {
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
    uf_debugger_attach_tracer(&rt, stdout);

    UfInterpretResult result = uf_interpret_program(&rt, program);
    uf_debugger_detach(&rt);

    int exit_code = (result == UF_INTERPRET_OK && !rt.had_runtime_error) ? 0 : 70;
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    free(source);
    return exit_code;
}

static int cmd_debug(const char* file_path) {
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

    if (parser.had_error || !program) {
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
    uf_debugger_attach_interactive(&rt, stdin, stdout);

    printf("Unfish Debugger (ufdb) started for '%s'. Type 'h' or 'help' for commands.\n", file_path);
    UfInterpretResult result = uf_interpret_program(&rt, program);
    uf_debugger_detach(&rt);

    int exit_code = (result == UF_INTERPRET_OK && !rt.had_runtime_error) ? 0 : 70;
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    free(source);
    return exit_code;
}

static int cmd_blocks_export(const char* file_path) {
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

    if (parser.had_error || !program) {
        uf_interner_free(&interner);
        uf_arena_free(&arena);
        free(source);
        return 1;
    }

    uf_blocks_export_stream(program, stdout);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    free(source);
    return 0;
}

static int cmd_blocks_import(const char* file_path) {
    char* source = read_file(file_path);
    if (!source) return 1;

    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, file_path, source);

    UfProgram* program = uf_blocks_import_string(source, &arena, &interner, &reporter);
    if (!program) {
        fprintf(stderr, "Error: Failed to import blocks JSON from '%s'\n", file_path);
        uf_interner_free(&interner);
        uf_arena_free(&arena);
        free(source);
        return 1;
    }

    uf_format_program_stream(program, stdout);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    free(source);
    return 0;
}

static int cmd_compile(int argc, char** argv) {
    bool use_regvm = false;
    bool write_cache = false;
    const char* out_path = NULL;
    const char* file_path = NULL;

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "--regvm") == 0) {
            use_regvm = true;
        } else if (strcmp(argv[i], "--cache") == 0) {
            write_cache = true;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown compile option '%s'\n", argv[i]);
            return 64;
        } else {
            file_path = argv[i];
        }
    }

    if (!file_path) {
        fprintf(stderr, "Error: Expected file path for 'compile'\n");
        return 64;
    }

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

    if (parser.had_error || !program) {
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

    int exit_code = 0;
    uint64_t source_hash = uf_cache_hash_source(source, strlen(source));

    if (use_regvm) {
        UfRegFunction* fn = uf_reg_compile(program, &rt, &reporter);
        if (!fn || reporter.error_count > 0) {
            exit_code = 1;
        } else {
            if (out_path || write_cache) {
                char* target = out_path ? strdup(out_path) : uf_cache_path_for(file_path, true);
                if (target) {
                    if (uf_cache_write_reg(target, program, fn, source_hash)) {
                        printf("Compiled register bytecode saved to %s\n", target);
                    } else {
                        fprintf(stderr, "Error: Failed to write register bytecode to %s\n", target);
                        exit_code = 1;
                    }
                    free(target);
                }
            } else {
                printf("== RegVM Bytecode for %s ==\n", file_path);
                printf("Registers used: %u, Instructions: %zu\n", (unsigned)fn->max_regs, fn->chunk.code_count);
            }
        }
    } else {
        UfBytecodeFunction* fn = uf_compile(program, &rt, &reporter);
        if (!fn || reporter.error_count > 0) {
            exit_code = 1;
        } else {
            if (out_path || write_cache) {
                char* target = out_path ? strdup(out_path) : uf_cache_path_for(file_path, false);
                if (target) {
                    if (uf_cache_write_stack(target, program, fn, source_hash)) {
                        printf("Compiled bytecode saved to %s\n", target);
                    } else {
                        fprintf(stderr, "Error: Failed to write bytecode to %s\n", target);
                        exit_code = 1;
                    }
                    free(target);
                }
            } else {
                uf_disasm_function_tree(fn, stdout);
            }
        }
    }

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    free(source);
    return exit_code;
}

static int cmd_emit_c(const char* file_path, const char* out_c_path, bool is_embedded) {
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

    if (parser.had_error || !program) {
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

    bool ok = false;
    if (is_embedded) {
        if (out_c_path) {
            ok = uf_emit_c_to_file_embedded(program, file_path, out_c_path);
        } else {
            ok = uf_emit_c_program_embedded(program, file_path, stdout);
        }
    } else {
        if (out_c_path) {
            ok = uf_emit_c_to_file(program, out_c_path);
        } else {
            ok = uf_emit_c_program(program, stdout);
        }
    }

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    free(source);
    return ok ? 0 : 1;
}

static int cmd_build(const char* file_path, const char* out_bin_path, bool is_wasm, bool is_embedded, bool is_arm) {
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

    if (parser.had_error || !program) {
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

    char default_bin[256];
    if (!out_bin_path) {
        const char* base = strrchr(file_path, '/');
        base = base ? base + 1 : file_path;
        snprintf(default_bin, sizeof(default_bin), "%s", base);
        char* dot = strrchr(default_bin, '.');
        if (dot && strcmp(dot, ".unfish") == 0) *dot = '\0';
        if (is_wasm) {
            strncat(default_bin, ".wasm", sizeof(default_bin) - strlen(default_bin) - 1);
        } else if (is_arm) {
            strncat(default_bin, ".elf", sizeof(default_bin) - strlen(default_bin) - 1);
        }
        out_bin_path = default_bin;
    } else {
        const char* dot = strrchr(out_bin_path, '.');
        if (dot && strcmp(dot, ".elf") == 0) {
            is_arm = true;
            is_embedded = true;
        }
    }

    bool ok = false;
    const char* target_name = "native binary";
    if (is_arm) {
        target_name = "ARM Cortex-M ELF binary";
        ok = uf_build_embedded_arm(program, file_path, out_bin_path);
    } else if (is_embedded) {
        target_name = "embedded profile binary";
        ok = uf_build_embedded(program, file_path, out_bin_path);
    } else if (is_wasm) {
        target_name = "WebAssembly binary";
        ok = uf_build_wasm_with_path(program, file_path, out_bin_path);
    } else {
        target_name = "native binary";
        ok = uf_build_native_with_path(program, file_path, out_bin_path);
    }

    if (ok) {
        printf("Built %s: %s\n", target_name, out_bin_path);
    } else {
        fprintf(stderr, "Error: Failed to build %s '%s'\n", target_name, out_bin_path);
    }

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    free(source);
    return ok ? 0 : 1;
}

static int cmd_run(const char* file_path, int script_argc, char** script_argv, bool strict, bool use_vm, bool use_regvm, bool use_wasm, bool debug_vm, bool no_cache, bool profile) {
    char* source = read_file(file_path);
    if (!source) {
        fprintf(stderr, "Error: Could not open or read file '%s'\n", file_path);
        return 66;
    }

    UfProfiler prof;
    if (profile) {
        uf_profiler_init(&prof);
        no_cache = true;
    }

    /* Fast path: Check bytecode cache if using Stack VM or Register VM */
    if ((use_vm || use_regvm) && !no_cache) {
        uint64_t source_hash = uf_cache_hash_source(source, strlen(source));
        char* cache_path = uf_cache_path_for(file_path, use_regvm);
        if (cache_path) {
            UfDiagnosticReporter reporter;
            uf_diag_reporter_init(&reporter, file_path, source);
            UfRuntime rt;
            uf_runtime_init(&rt, &reporter);
            uf_runtime_set_args(&rt, script_argc, script_argv);

            if (use_regvm) {
                UfRegFunction* fn = uf_cache_read_reg(&rt, cache_path, source_hash);
                if (fn) {
                    UfRegVM vm;
                    uf_regvm_init(&vm, &rt);
                    UfInterpretResult result = uf_regvm_run(&vm, fn);
                    int exit_code = (result == UF_INTERPRET_OK && !rt.had_runtime_error && !vm.had_error) ? 0 : 3;
                    if (debug_vm) {
                        printf("=== Register VM Execution Statistics (Cached) ===\n");
                        printf("Total instructions executed: %lu\n", (unsigned long)vm.total_instructions);
                    }
                    uf_regvm_free(&vm);
                    uf_runtime_free(&rt);
                    free(cache_path);
                    free(source);
                    return exit_code;
                }
            } else if (use_vm) {
                UfBytecodeFunction* fn = uf_cache_read_stack(&rt, cache_path, source_hash);
                if (fn) {
                    UfVM vm;
                    uf_vm_init(&vm, &rt);
                    vm.trace_execution = debug_vm;
                    UfInterpretResult result = uf_vm_run(&vm, fn);
                    int exit_code = (result == UF_INTERPRET_OK && !rt.had_runtime_error && !vm.had_error) ? 0 : 3;
                    if (debug_vm) {
                        printf("=== VM Execution Statistics (Cached) ===\n");
                        printf("Total instructions executed: %lu\n", (unsigned long)vm.total_instructions);
                        printf("Peak evaluation stack depth: %zu\n", vm.peak_stack_depth);
                        printf("Peak call frame depth: %zu\n", vm.peak_frame_depth);
                    }
                    uf_vm_free(&vm);
                    uf_runtime_free(&rt);
                    free(cache_path);
                    free(source);
                    return exit_code;
                }
            }

            uf_runtime_free(&rt);
            free(cache_path);
        }
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
    sema.strict_mode = strict;
    if (!uf_analyze_program(&sema, program) || reporter.error_count > 0) {
        uf_interner_free(&interner);
        uf_arena_free(&arena);
        free(source);
        return 2;
    }

    if (use_wasm) {
        char temp_wasm[256];
        snprintf(temp_wasm, sizeof(temp_wasm), "/tmp/unfish_run_%d.wasm", (int)getpid());
        if (!uf_build_wasm_with_path(program, file_path, temp_wasm)) {
            uf_interner_free(&interner);
            uf_arena_free(&arena);
            free(source);
            return 1;
        }
        uf_interner_free(&interner);
        uf_arena_free(&arena);
        free(source);

        char cmd[2048];
        char args_str[1024] = "";
        for (int i = 1; i < script_argc; ++i) {
            strncat(args_str, " \"", sizeof(args_str) - strlen(args_str) - 1);
            strncat(args_str, script_argv[i], sizeof(args_str) - strlen(args_str) - 1);
            strncat(args_str, "\"", sizeof(args_str) - strlen(args_str) - 1);
        }

        if (access("tools/wasm/run_wasm.js", R_OK) == 0) {
            snprintf(cmd, sizeof(cmd), "node --no-warnings --experimental-wasi-unstable-preview1 tools/wasm/run_wasm.js \"%s\"%s", temp_wasm, args_str);
        } else {
            snprintf(cmd, sizeof(cmd),
                     "node --no-warnings --experimental-wasi-unstable-preview1 -e \""
                     "const fs=require('fs'),{WASI}=require('wasi');"
                     "const w=new WASI({version:'preview1',args:['%s'%s],env:process.env,preopens:{'.':'.'}});"
                     "WebAssembly.instantiate(fs.readFileSync('%s'),{wasi_snapshot_preview1:w.wasiImport})"
                     ".then(r=>{try{const c=w.start(r.instance);process.exit(c!==undefined?c:0)}catch(e){process.exit(e&&typeof e.code==='number'?e.code:0)}});\"",
                     file_path, args_str, temp_wasm);
        }
        int res = system(cmd);
        remove(temp_wasm);
        return (res >= 0 && WIFEXITED(res)) ? WEXITSTATUS(res) : res;
    }

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);
    uf_runtime_set_args(&rt, script_argc, script_argv);
    if (profile) {
        rt.profiler = &prof;
    }

    int exit_code = 0;
    if (use_regvm) {
        UfRegFunction* fn = uf_reg_compile(program, &rt, &reporter);
        if (!fn || reporter.error_count > 0 || rt.had_runtime_error) {
            exit_code = rt.had_runtime_error ? 3 : 1;
        } else {
            if (!no_cache) {
                uint64_t source_hash = uf_cache_hash_source(source, strlen(source));
                char* cache_path = uf_cache_path_for(file_path, true);
                if (cache_path) {
                    uf_cache_write_reg(cache_path, program, fn, source_hash);
                    free(cache_path);
                }
            }
            UfRegVM vm;
            uf_regvm_init(&vm, &rt);
            UfInterpretResult result = uf_regvm_run(&vm, fn);
            exit_code = (result == UF_INTERPRET_OK && !rt.had_runtime_error && !vm.had_error) ? 0 : 3;
            if (debug_vm) {
                printf("=== Register VM Execution Statistics ===\n");
                printf("Total instructions executed: %lu\n", (unsigned long)vm.total_instructions);
            }
            uf_regvm_free(&vm);
        }
    } else if (use_vm) {
        UfBytecodeFunction* fn = uf_compile(program, &rt, &reporter);
        if (!fn || reporter.error_count > 0 || rt.had_runtime_error) {
            exit_code = rt.had_runtime_error ? 3 : 1;
        } else {
            if (!no_cache) {
                uint64_t source_hash = uf_cache_hash_source(source, strlen(source));
                char* cache_path = uf_cache_path_for(file_path, false);
                if (cache_path) {
                    uf_cache_write_stack(cache_path, program, fn, source_hash);
                    free(cache_path);
                }
            }
            UfVM vm;
            uf_vm_init(&vm, &rt);
            vm.trace_execution = debug_vm;
            UfInterpretResult result = uf_vm_run(&vm, fn);
            exit_code = (result == UF_INTERPRET_OK && !rt.had_runtime_error && !vm.had_error) ? 0 : 3;
            if (debug_vm) {
                printf("=== VM Execution Statistics ===\n");
                printf("Total instructions executed: %lu\n", (unsigned long)vm.total_instructions);
                printf("Peak evaluation stack depth: %zu\n", vm.peak_stack_depth);
                printf("Peak call frame depth: %zu\n", vm.peak_frame_depth);
            }
            uf_vm_free(&vm);
        }
    } else {
        UfInterpretResult result = uf_interpret_program(&rt, program);
        exit_code = (result == UF_INTERPRET_OK && !rt.had_runtime_error) ? 0 : 3;
    }

    if (profile) {
        uf_profiler_report(&prof, stdout);
    }

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    free(source);
    return exit_code;
}

static int count_open_delimiters(const char* s) {
    if (!s) return 0;
    int count = 0;
    bool in_str = false;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        char c = s[i];
        if (c == '\\' && in_str && s[i + 1] != '\0') {
            i++;
            continue;
        }
        if (c == '"') {
            in_str = !in_str;
            continue;
        }
        if (!in_str) {
            if (c == '(' || c == '[' || c == '{') {
                count++;
            } else if (c == ')' || c == ']' || c == '}') {
                if (count > 0) count--;
            }
        }
    }
    return count;
}

static void cmd_repl(void) {
    printf("Unfish %s REPL (type ':help' for commands, 'exit' to quit)\n", UF_VERSION_STRING);

    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<repl>", NULL);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfArena arena;
    uf_arena_init(&arena, 65536);
    UfInterner interner;
    uf_interner_init(&interner, &arena);

    char line[1024];

    while (true) {
        printf("unfish> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (strcmp(line, ":exit") == 0 || strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            break;
        }

        if (strcmp(line, ":help") == 0 || strcmp(line, "help") == 0) {
            printf("Unfish REPL Commands:\n");
            printf("  :help          Show this help message\n");
            printf("  :vars          List all declared variables and their values\n");
            printf("  :reset         Reset runtime environment and clear all variables\n");
            printf("  :gc            Run garbage collector and report heap usage\n");
            printf("  :exit / quit   Exit the REPL\n");
            continue;
        }

        if (strcmp(line, ":reset") == 0) {
            uf_runtime_free(&rt);
            uf_interner_free(&interner);
            uf_arena_free(&arena);
            uf_arena_init(&arena, 65536);
            uf_interner_init(&interner, &arena);
            uf_diag_reporter_init(&reporter, "<repl>", NULL);
            uf_runtime_init(&rt, &reporter);
            printf("Environment reset.\n");
            continue;
        }

        if (strcmp(line, ":gc") == 0) {
            size_t before = rt.bytes_allocated;
            uf_gc_collect(&rt);
            size_t after = rt.bytes_allocated;
            printf("GC ran: freed %zu bytes (current heap: %zu bytes)\n",
                   before > after ? before - after : 0, after);
            continue;
        }

        if (strcmp(line, ":vars") == 0) {
            printf("Global variables:\n");
            size_t var_count = 0;
            if (rt.global_env) {
                for (size_t b = 0; b < rt.global_env->bucket_count; ++b) {
                    UfEnvBinding* bind = rt.global_env->buckets[b];
                    while (bind) {
                        if (bind->value.kind != UF_VAL_NATIVE_FN) {
                            char* repr = uf_val_repr(bind->value);
                            printf("  %s: %s = %s\n", bind->name, uf_val_type_name(bind->value), repr);
                            free(repr);
                            var_count++;
                        }
                        bind = bind->next;
                    }
                }
            }
            if (var_count == 0) {
                printf("  (no user variables defined)\n");
            }
            continue;
        }

        if (len == 0) {
            continue;
        }

        UfStrBuf input_buf;
        uf_strbuf_init(&input_buf);
        uf_strbuf_append(&input_buf, line);

        int open_delim = count_open_delimiters(input_buf.data);
        bool has_colon = (len > 0 && line[len - 1] == ':');

        if (open_delim > 0 || has_colon) {
            char block_line[1024];
            for (;;) {
                printf("  ... > ");
                fflush(stdout);
                if (!fgets(block_line, sizeof(block_line), stdin)) break;
                size_t blen = strlen(block_line);
                while (blen > 0 && (block_line[blen - 1] == '\n' || block_line[blen - 1] == '\r')) {
                    block_line[--blen] = '\0';
                }
                if (blen == 0 && count_open_delimiters(input_buf.data) <= 0) {
                    break;
                }
                uf_strbuf_append(&input_buf, "\n");
                uf_strbuf_append(&input_buf, block_line);
                if (count_open_delimiters(input_buf.data) <= 0 && !has_colon) {
                    break;
                }
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
                if (!rt.had_runtime_error) {
                    char* repr = uf_val_repr(val);
                    printf("=> %s\n", repr);
                    free(repr);
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

    bool strict = false;
    bool use_vm = false;
    bool use_regvm = false;
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        if (strcmp(argv[arg_idx], "--strict") == 0) {
            strict = true;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--regvm") == 0) {
            use_regvm = true;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--vm") == 0) {
            use_vm = true;
            arg_idx++;
        } else {
            break;
        }
    }

    if (arg_idx >= argc) {
        print_usage("unfish");
        return 64;
    }

    const char* cmd = argv[arg_idx];

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
        if (arg_idx + 1 >= argc) {
            fprintf(stderr, "Error: Expected file path for 'tokens'\n");
            return 64;
        }
        return cmd_tokens(argv[arg_idx + 1]);
    }

    if (strcmp(cmd, "ast") == 0) {
        if (arg_idx + 1 >= argc) {
            fprintf(stderr, "Error: Expected file path for 'ast'\n");
            return 64;
        }
        return cmd_ast(argv[arg_idx + 1]);
    }

    if (strcmp(cmd, "check") == 0) {
        arg_idx++;
        if (arg_idx < argc && strcmp(argv[arg_idx], "--strict") == 0) {
            strict = true;
            arg_idx++;
        }
        if (arg_idx >= argc) {
            fprintf(stderr, "Error: Expected file path for 'check'\n");
            return 64;
        }
        return cmd_check(argv[arg_idx], strict);
    }

    if (strcmp(cmd, "format") == 0) {
        return cmd_format(argc - (arg_idx + 1), argv + arg_idx + 1);
    }

    if (strcmp(cmd, "trace") == 0) {
        if (arg_idx + 1 >= argc) {
            fprintf(stderr, "Error: Expected file path for 'trace'\n");
            return 64;
        }
        return cmd_trace(argv[arg_idx + 1]);
    }

    if (strcmp(cmd, "debug") == 0) {
        if (arg_idx + 1 >= argc) {
            fprintf(stderr, "Error: Expected file path for 'debug'\n");
            return 64;
        }
        return cmd_debug(argv[arg_idx + 1]);
    }

    if (strcmp(cmd, "blocks-export") == 0) {
        if (arg_idx + 1 >= argc) {
            fprintf(stderr, "Error: Expected file path for 'blocks-export'\n");
            return 64;
        }
        return cmd_blocks_export(argv[arg_idx + 1]);
    }

    if (strcmp(cmd, "blocks-import") == 0) {
        if (arg_idx + 1 >= argc) {
            fprintf(stderr, "Error: Expected file path for 'blocks-import'\n");
            return 64;
        }
        return cmd_blocks_import(argv[arg_idx + 1]);
    }

    if (strcmp(cmd, "compile") == 0 || strcmp(cmd, "disasm") == 0 || strcmp(cmd, "dis") == 0) {
        return cmd_compile(argc - (arg_idx + 1), argv + arg_idx + 1);
    }

    if (strcmp(cmd, "emit-c") == 0) {
        arg_idx++;
        const char* file_path = NULL;
        const char* out_c_path = NULL;
        bool is_embedded = false;
        while (arg_idx < argc) {
            if (strcmp(argv[arg_idx], "--embedded") == 0 || strcmp(argv[arg_idx], "-e") == 0) {
                is_embedded = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "-o") == 0 && arg_idx + 1 < argc) {
                out_c_path = argv[arg_idx + 1];
                arg_idx += 2;
            } else if (!file_path && argv[arg_idx][0] != '-') {
                file_path = argv[arg_idx++];
            } else if (!out_c_path && argv[arg_idx][0] != '-') {
                out_c_path = argv[arg_idx++];
            } else {
                break;
            }
        }
        if (!file_path) {
            fprintf(stderr, "Error: Expected file path for 'emit-c'\n");
            return 64;
        }
        return cmd_emit_c(file_path, out_c_path, is_embedded);
    }

    if (strcmp(cmd, "build") == 0) {
        arg_idx++;
        const char* file_path = NULL;
        const char* out_bin_path = NULL;
        bool is_wasm = false;
        bool is_embedded = false;
        bool is_arm = false;
        while (arg_idx < argc) {
            if (strcmp(argv[arg_idx], "--wasm") == 0) {
                is_wasm = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--embedded") == 0) {
                is_embedded = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--arm") == 0) {
                is_arm = true;
                is_embedded = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "-o") == 0 && arg_idx + 1 < argc) {
                out_bin_path = argv[arg_idx + 1];
                arg_idx += 2;
            } else if (!file_path && argv[arg_idx][0] != '-') {
                file_path = argv[arg_idx++];
            } else if (!out_bin_path && argv[arg_idx][0] != '-') {
                out_bin_path = argv[arg_idx++];
            } else {
                break;
            }
        }
        if (!file_path) {
            fprintf(stderr, "Error: Expected file path for 'build'\n");
            return 64;
        }
        if (out_bin_path && strlen(out_bin_path) > 5 && strcmp(out_bin_path + strlen(out_bin_path) - 5, ".wasm") == 0) {
            is_wasm = true;
        }
        if (out_bin_path && strlen(out_bin_path) > 4 && strcmp(out_bin_path + strlen(out_bin_path) - 4, ".elf") == 0) {
            is_arm = true;
            is_embedded = true;
        }
        return cmd_build(file_path, out_bin_path, is_wasm, is_embedded, is_arm);
    }


    if (strcmp(cmd, "lsp") == 0) {
        return uf_lsp_run(stdin, stdout);
    }

    if (strcmp(cmd, "test") == 0) {
        arg_idx++;
        UfTestOptions opts;
        memset(&opts, 0, sizeof(opts));
        const char* target_path = NULL;
        while (arg_idx < argc) {
            if (strcmp(argv[arg_idx], "--strict") == 0) {
                opts.strict = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--regvm") == 0) {
                opts.use_regvm = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--vm") == 0) {
                opts.use_vm = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--verbose") == 0 || strcmp(argv[arg_idx], "-v") == 0) {
                opts.verbose = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--filter") == 0 && arg_idx + 1 < argc) {
                opts.filter = argv[arg_idx + 1];
                arg_idx += 2;
            } else if (!target_path && argv[arg_idx][0] != '-') {
                target_path = argv[arg_idx++];
            } else {
                break;
            }
        }
        return uf_test_run(target_path, &opts);
    }

    if (strcmp(cmd, "doc") == 0) {
        arg_idx++;
        UfDocOptions opts;
        memset(&opts, 0, sizeof(opts));
        opts.format = UF_DOC_FORMAT_MARKDOWN;
        const char* target_path = NULL;
        while (arg_idx < argc) {
            if (strcmp(argv[arg_idx], "-o") == 0 && arg_idx + 1 < argc) {
                opts.output_path = argv[arg_idx + 1];
                arg_idx += 2;
            } else if (strcmp(argv[arg_idx], "--title") == 0 && arg_idx + 1 < argc) {
                opts.title = argv[arg_idx + 1];
                arg_idx += 2;
            } else if ((strcmp(argv[arg_idx], "--format") == 0 || strcmp(argv[arg_idx], "-f") == 0) && arg_idx + 1 < argc) {
                if (strcmp(argv[arg_idx + 1], "html") == 0) opts.format = UF_DOC_FORMAT_HTML;
                else opts.format = UF_DOC_FORMAT_MARKDOWN;
                arg_idx += 2;
            } else if (!target_path && argv[arg_idx][0] != '-') {
                target_path = argv[arg_idx++];
            } else {
                break;
            }
        }
        if (!target_path) {
            fprintf(stderr, "Error: Expected file or path for 'doc'\n");
            return 64;
        }
        return uf_doc_generate(target_path, &opts);
    }

    if (strcmp(cmd, "pkg") == 0) {
        arg_idx++;
        if (arg_idx >= argc) {
            fprintf(stderr, "Error: Expected pkg subcommand: init, check, run, test, build\n");
            return 64;
        }
        const char* sub = argv[arg_idx++];
        if (strcmp(sub, "init") == 0) {
            const char* name = (arg_idx < argc) ? argv[arg_idx] : NULL;
            return uf_pkg_init(name);
        } else if (strcmp(sub, "check") == 0) {
            return uf_pkg_check();
        } else if (strcmp(sub, "run") == 0) {
            return uf_pkg_run(argc - arg_idx, argv + arg_idx);
        } else if (strcmp(sub, "test") == 0) {
            return uf_pkg_test();
        } else if (strcmp(sub, "build") == 0) {
            const char* out_bin = (arg_idx < argc) ? argv[arg_idx] : NULL;
            return uf_pkg_build(out_bin);
        } else {
            fprintf(stderr, "Error: Unknown pkg subcommand '%s'\n", sub);
            return 64;
        }
    }

    if (strcmp(cmd, "learn") == 0) {
        arg_idx++;
        bool serve_web = false;
        int port = 8080;
        while (arg_idx < argc && (strcmp(argv[arg_idx], "--serve") == 0 || strcmp(argv[arg_idx], "--web") == 0 || strcmp(argv[arg_idx], "--port") == 0 || strcmp(argv[arg_idx], "-p") == 0)) {
            if ((strcmp(argv[arg_idx], "--port") == 0 || strcmp(argv[arg_idx], "-p") == 0) && arg_idx + 1 < argc) {
                port = atoi(argv[arg_idx + 1]);
                arg_idx += 2;
                serve_web = true;
            } else {
                serve_web = true;
                arg_idx++;
            }
        }
        if (serve_web) {
            UfPlaygroundOptions opts;
            memset(&opts, 0, sizeof(opts));
            opts.port = port;
            opts.web_root = "web";
            opts.open_browser = true;
            return uf_playground_start(&opts);
        }
        if (arg_idx < argc && (strcmp(argv[arg_idx], "--list") == 0 || strcmp(argv[arg_idx], "list") == 0 || strcmp(argv[arg_idx], "-l") == 0)) {
            uf_learn_list();
            return 0;
        }
        int lesson_num = 1;
        if (arg_idx < argc) {
            lesson_num = atoi(argv[arg_idx]);
            if (lesson_num <= 0) lesson_num = 1;
        }
        return uf_learn_start(lesson_num);
    }

    if (strcmp(cmd, "studio") == 0 || strcmp(cmd, "playground") == 0) {
        arg_idx++;
        UfPlaygroundOptions opts;
        memset(&opts, 0, sizeof(opts));
        opts.port = 8080;
        opts.web_root = "web";
        opts.open_browser = true;

        while (arg_idx < argc) {
            if ((strcmp(argv[arg_idx], "--port") == 0 || strcmp(argv[arg_idx], "-p") == 0) && arg_idx + 1 < argc) {
                opts.port = atoi(argv[arg_idx + 1]);
                arg_idx += 2;
            } else if (strcmp(argv[arg_idx], "--no-open") == 0) {
                opts.open_browser = false;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--dir") == 0 && arg_idx + 1 < argc) {
                opts.web_root = argv[arg_idx + 1];
                arg_idx += 2;
            } else {
                break;
            }
        }
        return uf_playground_start(&opts);
    }

    if (strcmp(cmd, "run") == 0) {
        bool debug_vm = false;
        bool no_cache = false;
        bool use_wasm = false;
        bool profile = false;
        arg_idx++;
        while (arg_idx < argc && argv[arg_idx][0] == '-') {
            if (strcmp(argv[arg_idx], "--strict") == 0) {
                strict = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--regvm") == 0) {
                use_regvm = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--vm") == 0) {
                use_vm = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--wasm") == 0) {
                use_wasm = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--debug") == 0) {
                use_vm = true;
                debug_vm = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--profile") == 0) {
                profile = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--no-cache") == 0) {
                no_cache = true;
                arg_idx++;
            } else {
                break;
            }
        }
        if (arg_idx >= argc) {
            fprintf(stderr, "Error: Expected file path for 'run'\n");
            return 64;
        }
        return cmd_run(argv[arg_idx], argc - arg_idx, argv + arg_idx, strict, use_vm, use_regvm, use_wasm, debug_vm, no_cache, profile);
    }

    /* Shorthand: unfish [--strict] [--vm] [--regvm] [--wasm] [--profile] [--no-cache] [--debug] <file.unfish> */
    bool debug_vm = false;
    bool no_cache = false;
    bool use_wasm = false;
    bool profile = false;
    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        if (strcmp(argv[arg_idx], "--strict") == 0) {
            strict = true;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--regvm") == 0) {
            use_regvm = true;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--vm") == 0) {
            use_vm = true;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--wasm") == 0) {
            use_wasm = true;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--debug") == 0) {
            use_vm = true;
            debug_vm = true;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--profile") == 0) {
            profile = true;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--no-cache") == 0) {
            no_cache = true;
            arg_idx++;
        } else {
            break;
        }
    }

    if (arg_idx < argc && argv[arg_idx][0] != '-') {
        return cmd_run(argv[arg_idx], argc - arg_idx, argv + arg_idx, strict, use_vm, use_regvm, use_wasm, debug_vm, no_cache, profile);
    }


    fprintf(stderr, "Error: Unknown command or option '%s'\n", argv[arg_idx < argc ? arg_idx : argc - 1]);
    print_usage("unfish");
    return 64;
}
