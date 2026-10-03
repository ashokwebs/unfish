/* Browser entry point: the real Unfish front end and engines compiled to
 * WebAssembly (wasm32-wasi, reactor model) for the website playground, so the
 * browser runs exactly what `unfish run` runs instead of a separate
 * JavaScript reimplementation. Built by `make wasm-web`.
 *
 * The host copies UTF-8 source into a buffer from uf_wasm_alloc and calls
 * uf_wasm_run. Program output reaches the host through WASI fd_write on
 * stdout/stderr. The host instantiates a fresh module per run, so no state
 * leaks between runs. */

#include "../common/uf_arena.h"
#include "../common/uf_string.h"
#include "../common/uf_diagnostic.h"
#include "../lexer/uf_lexer.h"
#include "../parser/uf_parser.h"
#include "../semantic/uf_semantic.h"
#include "../runtime/uf_runtime.h"
#include "../interpreter/uf_interpreter.h"
#include "../compiler/uf_compiler.h"
#include "../compiler/uf_reg_compiler.h"
#include "../vm/uf_vm.h"
#include "../vm2/uf_regvm.h"

#include <stdio.h>
#include <stdlib.h>

#define UF_WASM_EXPORT(name) __attribute__((export_name(name)))

enum { UF_WASM_INTERP = 0, UF_WASM_VM = 1, UF_WASM_REGVM = 2 };

UF_WASM_EXPORT("uf_wasm_alloc")
void* uf_wasm_alloc(size_t size) {
    return malloc(size);
}

UF_WASM_EXPORT("uf_wasm_free")
void uf_wasm_free(void* ptr) {
    free(ptr);
}

static int run_program(UfProgram* program, UfDiagnosticReporter* reporter, int engine) {
    /* As `unfish run main.unfish` would see it: sys.args() is the script. */
    static char script_name[] = "main.unfish";
    static char* script_argv[] = { script_name, NULL };

    UfRuntime rt;
    uf_runtime_init(&rt, reporter);
    uf_runtime_set_args(&rt, 1, script_argv);

    int exit_code = 0;
    if (engine == UF_WASM_REGVM) {
        UfRegFunction* fn = uf_reg_compile(program, &rt, reporter);
        if (!fn || reporter->error_count > 0 || rt.had_runtime_error) {
            exit_code = rt.had_runtime_error ? 3 : 1;
        } else {
            UfRegVM vm;
            uf_regvm_init(&vm, &rt);
            UfInterpretResult result = uf_regvm_run(&vm, fn);
            exit_code = (result == UF_INTERPRET_OK && !rt.had_runtime_error && !vm.had_error) ? 0 : 3;
            uf_regvm_free(&vm);
        }
    } else if (engine == UF_WASM_VM) {
        UfBytecodeFunction* fn = uf_compile(program, &rt, reporter);
        if (!fn || reporter->error_count > 0 || rt.had_runtime_error) {
            exit_code = rt.had_runtime_error ? 3 : 1;
        } else {
            UfVM vm;
            uf_vm_init(&vm, &rt);
            UfInterpretResult result = uf_vm_run(&vm, fn);
            exit_code = (result == UF_INTERPRET_OK && !rt.had_runtime_error && !vm.had_error) ? 0 : 3;
            uf_vm_free(&vm);
        }
    } else {
        UfInterpretResult result = uf_interpret_program(&rt, program);
        exit_code = (result == UF_INTERPRET_OK && !rt.had_runtime_error) ? 0 : 3;
    }

    uf_runtime_free(&rt);
    return exit_code;
}

/* Runs NUL-terminated `source` on the given engine (0 interpreter, 1 stack
 * VM, 2 register VM). Returns the exit status `unfish run` would: 0 success,
 * 1 syntax error, 2 semantic error, 3 runtime error. */
UF_WASM_EXPORT("uf_wasm_run")
int uf_wasm_run(const char* source, int engine) {
    /* The host also places the source at /main.unfish, the working
     * directory, so imports of sibling files resolve as they would on disk. */
    const char* file_name = "main.unfish";

    UfArena arena;
    uf_arena_init(&arena, 65536);
    UfInterner interner;
    uf_interner_init(&interner, &arena);

    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, file_name, source);

    UfLexer lexer;
    uf_lexer_init(&lexer, file_name, source, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);

    int exit_code = 1;
    UfProgram* program = uf_parse_program(&parser);
    if (!parser.had_error && reporter.error_count == 0) {
        UfSemanticAnalyzer sema;
        uf_semantic_init(&sema, &arena, &reporter);
        if (uf_analyze_program(&sema, program) && reporter.error_count == 0) {
            exit_code = run_program(program, &reporter, engine);
        } else {
            exit_code = 2;
        }
    }

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    fflush(stdout);
    fflush(stderr);
    return exit_code;
}
