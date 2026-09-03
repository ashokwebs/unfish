#include "uf_common.h"
#include "uf_arena.h"
#include "uf_string.h"
#include "uf_diagnostic.h"
#include "uf_token.h"
#include "uf_lexer.h"
#include "uf_ast.h"
#include "uf_parser.h"
#include "uf_semantic.h"
#include "uf_runtime.h"
#include "uf_compiler.h"
#include "uf_vm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void run_vm_source(const char* src) {
    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "test.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "test.unfish", src, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool sema_ok = uf_analyze_program(&sema, prog);
    assert(sema_ok && reporter.error_count == 0);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfBytecodeFunction* fn = uf_compile(prog, &rt, &reporter);
    assert(fn != NULL);

    UfVM vm;
    uf_vm_init(&vm, &rt);
    UfInterpretResult res = uf_vm_run(&vm, fn);
    assert(res == UF_INTERPRET_OK);

    uf_vm_free(&vm);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
}

static void test_vm_arithmetic(void) {
    const char* src =
        "let a = 10\n"
        "let b = 20\n"
        "let c = a + b * 2\n"
        "say c\n";
    run_vm_source(src);
    printf("test_vm_arithmetic passed!\n");
}

static void test_vm_recursion(void) {
    const char* src =
        "function fib(n):\n"
        "    if n <= 1:\n"
        "        return n\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "say fib(10)\n";
    run_vm_source(src);
    printf("test_vm_recursion passed!\n");
}

static void test_vm_closure(void) {
    const char* src =
        "function make_adder(n):\n"
        "    let adder = function(x):\n"
        "        return n + x\n"
        "    return adder\n"
        "let add5 = make_adder(5)\n"
        "say add5(10)\n";
    run_vm_source(src);
    printf("test_vm_closure passed!\n");
}

static void test_vm_arrays_and_loops(void) {
    const char* src =
        "let arr = [1, 2, 3, 4, 5]\n"
        "let total = 0\n"
        "for x in arr:\n"
        "    total = total + x\n"
        "say total\n";
    run_vm_source(src);
    printf("test_vm_arrays_and_loops passed!\n");
}

int main(void) {
    printf("Running VM unit tests...\n");
    test_vm_arithmetic();
    test_vm_recursion();
    test_vm_closure();
    test_vm_arrays_and_loops();
    printf("All VM unit tests passed successfully!\n");
    return 0;
}
