#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../../src/common/uf_arena.h"
#include "../../src/common/uf_string.h"
#include "../../src/common/uf_diagnostic.h"
#include "../../src/lexer/uf_lexer.h"
#include "../../src/ast/uf_ast.h"
#include "../../src/parser/uf_parser.h"

static void test_vertical_slice_parser(void) {
    const char* src =
        "let name = \"World\"\n"
        "\n"
        "function greet(person):\n"
        "    say \"Hello, \" + person\n"
        "\n"
        "greet(name)\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "test_vertical_slice.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "test_vertical_slice.unfish", src, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);

    UfProgram* program = uf_parse_program(&parser);
    assert(program != NULL);
    assert(!parser.had_error);
    assert(reporter.error_count == 0);
    assert(program->count == 3);

    /* Stmt 0: let name = "World" */
    assert(program->stmts[0]->kind == UF_STMT_LET);
    assert(strcmp(program->stmts[0]->as.let_stmt.name, "name") == 0);
    assert(program->stmts[0]->as.let_stmt.init->kind == UF_EXPR_LITERAL_STRING);
    assert(strcmp(program->stmts[0]->as.let_stmt.init->as.string_val, "World") == 0);

    /* Stmt 1: function greet(person): ... */
    assert(program->stmts[1]->kind == UF_STMT_FUNCTION);
    assert(strcmp(program->stmts[1]->as.function_stmt.name, "greet") == 0);
    assert(program->stmts[1]->as.function_stmt.param_count == 1);
    assert(strcmp(program->stmts[1]->as.function_stmt.params[0], "person") == 0);
    UfStmt* body = program->stmts[1]->as.function_stmt.body;
    assert(body->kind == UF_STMT_BLOCK);
    assert(body->as.block.count == 1);
    assert(body->as.block.stmts[0]->kind == UF_STMT_SAY);
    UfExpr* say_expr = body->as.block.stmts[0]->as.say_stmt.expr;
    assert(say_expr->kind == UF_EXPR_BINARY);
    assert(say_expr->as.binary.op == UF_TOK_PLUS);
    assert(say_expr->as.binary.left->kind == UF_EXPR_LITERAL_STRING);
    assert(say_expr->as.binary.right->kind == UF_EXPR_IDENTIFIER);

    /* Stmt 2: greet(name) */
    assert(program->stmts[2]->kind == UF_STMT_EXPR);
    UfExpr* call_expr = program->stmts[2]->as.expr_stmt.expr;
    assert(call_expr->kind == UF_EXPR_CALL);
    assert(call_expr->as.call.callee->kind == UF_EXPR_IDENTIFIER);
    assert(strcmp(call_expr->as.call.callee->as.identifier_name, "greet") == 0);
    assert(call_expr->as.call.argc == 1);
    assert(call_expr->as.call.args[0]->kind == UF_EXPR_IDENTIFIER);
    assert(strcmp(call_expr->as.call.args[0]->as.identifier_name, "name") == 0);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_vertical_slice_parser passed!\n");
}

static void test_operator_precedence(void) {
    const char* src = "let res = 1 + 2 * 3\n";

    UfArena arena;
    uf_arena_init(&arena, 4096);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "precedence.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "precedence.unfish", src, &arena, &interner, &reporter);

    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);

    UfProgram* program = uf_parse_program(&parser);
    assert(program != NULL);
    assert(!parser.had_error);
    assert(program->count == 1);

    UfExpr* init = program->stmts[0]->as.let_stmt.init;
    assert(init->kind == UF_EXPR_BINARY);
    assert(init->as.binary.op == UF_TOK_PLUS);
    assert(init->as.binary.left->kind == UF_EXPR_LITERAL_NUMBER);
    assert(init->as.binary.left->as.number_val == 1.0);
    assert(init->as.binary.right->kind == UF_EXPR_BINARY);
    assert(init->as.binary.right->as.binary.op == UF_TOK_STAR);
    assert(init->as.binary.right->as.binary.left->as.number_val == 2.0);
    assert(init->as.binary.right->as.binary.right->as.number_val == 3.0);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_operator_precedence passed!\n");
}

int main(void) {
    printf("Running parser tests...\n");
    test_vertical_slice_parser();
    test_operator_precedence();
    printf("All parser unit tests passed successfully!\n");
    return 0;
}
