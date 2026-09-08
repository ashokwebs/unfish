#define _POSIX_C_SOURCE 200809L
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
#include "uf_regvm.h"
#include "uf_reg_compiler.h"
#include "uf_cache.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static UfProgram* parse_and_check(const char* src, const char* name, UfArena* arena, UfInterner* interner, UfDiagnosticReporter* reporter) {
    UfLexer lexer;
    uf_lexer_init(&lexer, name, src, arena, interner, reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, arena, reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog && !parser.had_error);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, arena, reporter);
    bool ok = uf_analyze_program(&sema, prog);
    assert(ok && reporter->error_count == 0);
    return prog;
}

static void test_cache_hashing(void) {
    const char* src1 = "let x = 10\nsay x\n";
    const char* src2 = "let x = 10\nsay x\n";
    const char* src3 = "let x = 20\nsay x\n";

    uint64_t h1 = uf_cache_hash_source(src1, strlen(src1));
    uint64_t h2 = uf_cache_hash_source(src2, strlen(src2));
    uint64_t h3 = uf_cache_hash_source(src3, strlen(src3));

    assert(h1 == h2);
    assert(h1 != h3);
    printf("test_cache_hashing passed!\n");
}

static void test_cache_stack_roundtrip(void) {
    const char* src =
        "fn add(a, b):\n"
        "    return a + b\n"
        "\n"
        "let res = add(15, 27)\n"
        "say res\n";

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "test_stack.unfish", src);

    UfProgram* prog = parse_and_check(src, "test_stack.unfish", &arena, &interner, &reporter);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfBytecodeFunction* fn = uf_compile(prog, &rt, &reporter);
    assert(fn != NULL);

    uint64_t source_hash = uf_cache_hash_source(src, strlen(src));
    const char* tmp_path = "/tmp/test_unfish_cache.ufc";

    bool write_ok = uf_cache_write_stack(tmp_path, prog, fn, source_hash);
    assert(write_ok);

    /* Read back from cache in fresh runtime */
    UfDiagnosticReporter reporter2;
    uf_diag_reporter_init(&reporter2, "test_stack.unfish", src);
    UfRuntime rt2;
    uf_runtime_init(&rt2, &reporter2);

    UfBytecodeFunction* loaded_fn = uf_cache_read_stack(&rt2, tmp_path, source_hash);
    assert(loaded_fn != NULL);
    assert(loaded_fn->chunk.code_count == fn->chunk.code_count);
    assert(loaded_fn->chunk.const_count == fn->chunk.const_count);

    /* Execute the cached bytecode */
    UfVM vm;
    uf_vm_init(&vm, &rt2);
    UfInterpretResult result = uf_vm_run(&vm, loaded_fn);
    assert(result == UF_INTERPRET_OK);
    assert(!rt2.had_runtime_error);

    uf_vm_free(&vm);
    uf_runtime_free(&rt2);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    unlink(tmp_path);

    printf("test_cache_stack_roundtrip passed!\n");
}

static void test_cache_reg_roundtrip(void) {
    const char* src =
        "fn multiply(a, b):\n"
        "    return a * b\n"
        "\n"
        "let total = multiply(7, 6)\n"
        "say total\n";

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "test_reg.unfish", src);

    UfProgram* prog = parse_and_check(src, "test_reg.unfish", &arena, &interner, &reporter);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfRegFunction* fn = uf_reg_compile(prog, &rt, &reporter);
    assert(fn != NULL);

    uint64_t source_hash = uf_cache_hash_source(src, strlen(src));
    const char* tmp_path = "/tmp/test_unfish_cache.ufrc";

    bool write_ok = uf_cache_write_reg(tmp_path, prog, fn, source_hash);
    assert(write_ok);

    /* Read back from cache in fresh runtime */
    UfDiagnosticReporter reporter2;
    uf_diag_reporter_init(&reporter2, "test_reg.unfish", src);
    UfRuntime rt2;
    uf_runtime_init(&rt2, &reporter2);

    UfRegFunction* loaded_fn = uf_cache_read_reg(&rt2, tmp_path, source_hash);
    assert(loaded_fn != NULL);
    assert(loaded_fn->chunk.code_count == fn->chunk.code_count);
    assert(loaded_fn->chunk.const_count == fn->chunk.const_count);
    assert(loaded_fn->max_regs == fn->max_regs);

    /* Execute the cached register bytecode */
    UfRegVM vm;
    uf_regvm_init(&vm, &rt2);
    UfInterpretResult result = uf_regvm_run(&vm, loaded_fn);
    assert(result == UF_INTERPRET_OK);
    assert(!rt2.had_runtime_error);

    uf_regvm_free(&vm);
    uf_runtime_free(&rt2);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    unlink(tmp_path);

    printf("test_cache_reg_roundtrip passed!\n");
}

static void test_cache_invalidation_and_corrupt(void) {
    const char* src = "let x = 42\nsay x\n";
    uint64_t source_hash = uf_cache_hash_source(src, strlen(src));

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "test_inv.unfish", src);

    UfProgram* prog = parse_and_check(src, "test_inv.unfish", &arena, &interner, &reporter);
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfBytecodeFunction* fn = uf_compile(prog, &rt, &reporter);
    assert(fn != NULL);

    const char* tmp_path = "/tmp/test_unfish_inv.ufc";
    assert(uf_cache_write_stack(tmp_path, prog, fn, source_hash));

    /* Attempt to read with different hash: must return NULL */
    UfBytecodeFunction* stale = uf_cache_read_stack(&rt, tmp_path, source_hash + 1);
    assert(stale == NULL);

    /* Attempt to read corrupt file */
    const char* corrupt_path = "/tmp/test_unfish_corrupt.ufc";
    FILE* cf = fopen(corrupt_path, "wb");
    assert(cf != NULL);
    const char* garbage = "BADMAGIC_CORRUPTED_BYTES_HERE";
    fwrite(garbage, 1, strlen(garbage), cf);
    fclose(cf);

    UfBytecodeFunction* bad_stack = uf_cache_read_stack(&rt, corrupt_path, source_hash);
    assert(bad_stack == NULL);
    UfRegFunction* bad_reg = uf_cache_read_reg(&rt, corrupt_path, source_hash);
    assert(bad_reg == NULL);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    unlink(tmp_path);
    unlink(corrupt_path);

    printf("test_cache_invalidation_and_corrupt passed!\n");
}

static void test_cache_module_import_roundtrip(void) {
    const char* src =
        "from testing import assert_equal\n"
        "assert_equal(10 + 20, 30, \"check\")\n";

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "test_import.unfish", src);

    UfProgram* prog = parse_and_check(src, "test_import.unfish", &arena, &interner, &reporter);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfRegFunction* fn = uf_reg_compile(prog, &rt, &reporter);
    assert(fn != NULL);

    uint64_t source_hash = uf_cache_hash_source(src, strlen(src));
    const char* tmp_path = "/tmp/test_unfish_import.ufrc";

    bool write_ok = uf_cache_write_reg(tmp_path, prog, fn, source_hash);
    assert(write_ok);

    /* Read back into fresh runtime */
    UfDiagnosticReporter reporter2;
    uf_diag_reporter_init(&reporter2, "test_import.unfish", src);
    UfRuntime rt2;
    uf_runtime_init(&rt2, &reporter2);

    UfRegFunction* loaded_fn = uf_cache_read_reg(&rt2, tmp_path, source_hash);
    assert(loaded_fn != NULL);

    /* Execute the cached bytecode */
    UfRegVM vm;
    uf_regvm_init(&vm, &rt2);
    UfInterpretResult result = uf_regvm_run(&vm, loaded_fn);
    assert(result == UF_INTERPRET_OK);
    assert(!rt2.had_runtime_error);

    uf_regvm_free(&vm);
    uf_runtime_free(&rt2);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    unlink(tmp_path);

    printf("test_cache_module_import_roundtrip passed!\n");
}

static void test_cache_path_generation(void) {
    char* path_stack = uf_cache_path_for("test_script.unfish", false);
    assert(path_stack != NULL);
    assert(strstr(path_stack, ".ufc") != NULL);

    char* path_reg = uf_cache_path_for("test_script.unfish", true);
    assert(path_reg != NULL);
    assert(strstr(path_reg, ".ufrc") != NULL);

    free(path_stack);
    free(path_reg);
    printf("test_cache_path_generation passed!\n");
}

int main(void) {
    printf("=== Running Cache Unit Tests ===\n");
    test_cache_hashing();
    test_cache_path_generation();
    test_cache_stack_roundtrip();
    test_cache_reg_roundtrip();
    test_cache_invalidation_and_corrupt();
    test_cache_module_import_roundtrip();
    printf("All Cache unit tests passed!\n");
    return 0;
}
