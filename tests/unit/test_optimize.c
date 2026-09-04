#include "../../src/compiler/uf_optimize.h"
#include "../../src/compiler/uf_compiler.h"
#include "../../src/compiler/uf_opcode.h"
#include "../../src/vm/uf_disasm.h"
#include "../../src/parser/uf_parser.h"
#include "../../src/lexer/uf_lexer.h"
#include "../../src/semantic/uf_semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static UfProgram* parse_test_source(const char* source, UfArena* arena, UfInterner* interner, UfDiagnosticReporter* reporter) {
    UfLexer lexer;
    uf_lexer_init(&lexer, "<test>", source, arena, interner, reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, arena, reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog != NULL);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, arena, reporter);
    bool ok = uf_analyze_program(&sema, prog);
    assert(ok);
    return prog;
}

static void test_constant_folding_numbers(void) {
    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    const char* src = "let x = 3 + 4 * 2\n";
    UfProgram* prog = parse_test_source(src, &arena, &interner, &reporter);

    uf_optimize_ast(prog, &rt);

    UfStmt* stmt = prog->stmts[0];
    assert(stmt->kind == UF_STMT_LET);
    assert(stmt->as.let_stmt.init->kind == UF_EXPR_LITERAL_NUMBER);
    assert(stmt->as.let_stmt.init->as.number_val == 11.0);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_constant_folding_numbers passed!\n");
}

static void test_constant_folding_strings(void) {
    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    const char* src = "let msg = \"Hello, \" + \"Unfish!\"\n";
    UfProgram* prog = parse_test_source(src, &arena, &interner, &reporter);

    uf_optimize_ast(prog, &rt);

    UfStmt* stmt = prog->stmts[0];
    assert(stmt->kind == UF_STMT_LET);
    assert(stmt->as.let_stmt.init->kind == UF_EXPR_LITERAL_STRING);
    assert(strcmp(stmt->as.let_stmt.init->as.string_val, "Hello, Unfish!") == 0);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_constant_folding_strings passed!\n");
}

static void test_constant_folding_booleans(void) {
    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    const char* src = "let flag = not (true and false)\n";
    UfProgram* prog = parse_test_source(src, &arena, &interner, &reporter);

    uf_optimize_ast(prog, &rt);

    UfStmt* stmt = prog->stmts[0];
    assert(stmt->kind == UF_STMT_LET);
    assert(stmt->as.let_stmt.init->kind == UF_EXPR_LITERAL_BOOL);
    assert(stmt->as.let_stmt.init->as.bool_val == true);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_constant_folding_booleans passed!\n");
}

static void test_dead_code_elimination(void) {
    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    const char* src =
        "function foo():\n"
        "    return 42\n"
        "    say \"unreachable\"\n"
        "    let dead = 100\n";
    UfProgram* prog = parse_test_source(src, &arena, &interner, &reporter);

    uf_optimize_ast(prog, &rt);

    UfStmt* fn_stmt = prog->stmts[0];
    assert(fn_stmt->kind == UF_STMT_FUNCTION);
    UfStmt* body = fn_stmt->as.function_stmt.body;
    assert(body->kind == UF_STMT_BLOCK);
    /* After return 42, unreachable statements were pruned */
    assert(body->as.block.count == 1);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_dead_code_elimination passed!\n");
}

static void test_optimized_bytecode(void) {
    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    const char* src = "say 10 + 20 + 30\n";
    UfProgram* prog = parse_test_source(src, &arena, &interner, &reporter);

    UfBytecodeFunction* fn = uf_compile(prog, &rt, &reporter);
    assert(fn != NULL);

    FILE* stream = tmpfile();
    assert(stream != NULL);
    uf_disasm_function_tree(fn, stream);
    rewind(stream);

    char buf[4096];
    size_t len = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[len] = '\0';
    fclose(stream);

    /* 10 + 20 + 30 was folded to single constant 60 */
    assert(strstr(buf, "'60'") != NULL);
    /* There should be NO OP_ADD in the generated bytecode */
    assert(strstr(buf, "OP_ADD") == NULL);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_optimized_bytecode passed!\n");
}

static void test_dead_branch_pruning(void) {
    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    const char* src =
        "let x = 1\n"
        "if false:\n"
        "    x = 99\n"
        "else:\n"
        "    x = 42\n"
        "while false:\n"
        "    x = 100\n";
    UfProgram* prog = parse_test_source(src, &arena, &interner, &reporter);

    uf_optimize_ast(prog, &rt);

    /* The if false was pruned to just its else branch (a block containing x = 42) */
    assert(prog->stmts[1]->kind == UF_STMT_BLOCK);
    assert(prog->stmts[1]->as.block.count == 1);
    UfStmt* assign = prog->stmts[1]->as.block.stmts[0];
    assert(assign->kind == UF_STMT_ASSIGN);
    assert(strcmp(assign->as.assign_stmt.name, "x") == 0);
    assert(assign->as.assign_stmt.value->as.number_val == 42.0);

    /* The while false was pruned to an empty block */
    assert(prog->stmts[2]->kind == UF_STMT_BLOCK);
    assert(prog->stmts[2]->as.block.count == 0);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_dead_branch_pruning passed!\n");
}

int main(void) {
    printf("Running optimization pass unit tests...\n");
    test_constant_folding_numbers();
    test_constant_folding_strings();
    test_constant_folding_booleans();
    test_dead_code_elimination();
    test_optimized_bytecode();
    test_dead_branch_pruning();
    printf("All optimization pass unit tests passed successfully!\n");
    return 0;
}
