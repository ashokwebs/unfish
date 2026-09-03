#include "uf_ast.h"

UfExpr* uf_expr_literal_null(UfArena* arena, SourceSpan span) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_LITERAL_NULL;
    expr->span = span;
    return expr;
}

UfExpr* uf_expr_literal_bool(UfArena* arena, SourceSpan span, bool val) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_LITERAL_BOOL;
    expr->span = span;
    expr->as.bool_val = val;
    return expr;
}

UfExpr* uf_expr_literal_number(UfArena* arena, SourceSpan span, double val) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_LITERAL_NUMBER;
    expr->span = span;
    expr->as.number_val = val;
    return expr;
}

UfExpr* uf_expr_literal_string(UfArena* arena, SourceSpan span, const char* str) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_LITERAL_STRING;
    expr->span = span;
    expr->as.string_val = str;
    return expr;
}

UfExpr* uf_expr_identifier(UfArena* arena, SourceSpan span, const char* name) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_IDENTIFIER;
    expr->span = span;
    expr->as.identifier_name = name;
    return expr;
}

UfExpr* uf_expr_unary(UfArena* arena, SourceSpan span, UfTokenKind op, UfExpr* operand) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_UNARY;
    expr->span = span;
    expr->as.unary.op = op;
    expr->as.unary.operand = operand;
    return expr;
}

UfExpr* uf_expr_binary(UfArena* arena, SourceSpan span, UfTokenKind op, UfExpr* left, UfExpr* right) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_BINARY;
    expr->span = span;
    expr->as.binary.op = op;
    expr->as.binary.left = left;
    expr->as.binary.right = right;
    return expr;
}

UfExpr* uf_expr_call(UfArena* arena, SourceSpan span, UfExpr* callee, UfExpr** args, size_t argc) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_CALL;
    expr->span = span;
    expr->as.call.callee = callee;
    expr->as.call.args = args;
    expr->as.call.argc = argc;
    return expr;
}

UfExpr* uf_expr_grouping(UfArena* arena, SourceSpan span, UfExpr* inner) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_GROUPING;
    expr->span = span;
    expr->as.grouping.inner = inner;
    return expr;
}

UfExpr* uf_expr_array(UfArena* arena, SourceSpan span, UfExpr** elements, size_t count) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_ARRAY;
    expr->span = span;
    expr->as.array_lit.elements = elements;
    expr->as.array_lit.count = count;
    return expr;
}

UfExpr* uf_expr_index(UfArena* arena, SourceSpan span, UfExpr* target, UfExpr* index) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_INDEX;
    expr->span = span;
    expr->as.index_expr.target = target;
    expr->as.index_expr.index = index;
    return expr;
}

UfExpr* uf_expr_map(UfArena* arena, SourceSpan span, UfExpr** keys, UfExpr** values, size_t count) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_MAP;
    expr->span = span;
    expr->as.map_lit.keys = keys;
    expr->as.map_lit.values = values;
    expr->as.map_lit.count = count;
    return expr;
}

UfExpr* uf_expr_function(UfArena* arena, SourceSpan span, const char* name, const char** params, size_t param_count, UfStmt* body) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_FUNCTION;
    expr->span = span;
    expr->as.fn_expr.name = name;
    expr->as.fn_expr.params = params;
    expr->as.fn_expr.param_count = param_count;
    expr->as.fn_expr.body = body;
    return expr;
}

UfStmt* uf_stmt_let(UfArena* arena, SourceSpan span, const char* name, UfExpr* init) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_LET;
    stmt->span = span;
    stmt->as.let_stmt.name = name;
    stmt->as.let_stmt.init = init;
    return stmt;
}

UfStmt* uf_stmt_assign(UfArena* arena, SourceSpan span, const char* name, UfExpr* value) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_ASSIGN;
    stmt->span = span;
    stmt->as.assign_stmt.name = name;
    stmt->as.assign_stmt.value = value;
    return stmt;
}

UfStmt* uf_stmt_index_assign(UfArena* arena, SourceSpan span, UfExpr* target, UfExpr* index, UfExpr* value) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_INDEX_ASSIGN;
    stmt->span = span;
    stmt->as.index_assign.target = target;
    stmt->as.index_assign.index = index;
    stmt->as.index_assign.value = value;
    return stmt;
}

UfStmt* uf_stmt_say(UfArena* arena, SourceSpan span, UfExpr* expr) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_SAY;
    stmt->span = span;
    stmt->as.say_stmt.expr = expr;
    return stmt;
}

UfStmt* uf_stmt_expr(UfArena* arena, SourceSpan span, UfExpr* expr) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_EXPR;
    stmt->span = span;
    stmt->as.expr_stmt.expr = expr;
    return stmt;
}

UfStmt* uf_stmt_if(UfArena* arena, SourceSpan span, UfExpr* condition, UfStmt* then_branch, UfStmt* else_branch) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_IF;
    stmt->span = span;
    stmt->as.if_stmt.condition = condition;
    stmt->as.if_stmt.then_branch = then_branch;
    stmt->as.if_stmt.else_branch = else_branch;
    return stmt;
}

UfStmt* uf_stmt_while(UfArena* arena, SourceSpan span, UfExpr* condition, UfStmt* body) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_WHILE;
    stmt->span = span;
    stmt->as.while_stmt.condition = condition;
    stmt->as.while_stmt.body = body;
    return stmt;
}

UfStmt* uf_stmt_repeat(UfArena* arena, SourceSpan span, UfExpr* count_expr, UfStmt* body) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_REPEAT;
    stmt->span = span;
    stmt->as.repeat_stmt.count_expr = count_expr;
    stmt->as.repeat_stmt.body = body;
    return stmt;
}

UfStmt* uf_stmt_for(UfArena* arena, SourceSpan span, const char* var_name, UfExpr* iterable, UfStmt* body) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_FOR;
    stmt->span = span;
    stmt->as.for_stmt.var_name = var_name;
    stmt->as.for_stmt.iterable = iterable;
    stmt->as.for_stmt.body = body;
    return stmt;
}

UfStmt* uf_stmt_break(UfArena* arena, SourceSpan span) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_BREAK;
    stmt->span = span;
    return stmt;
}

UfStmt* uf_stmt_continue(UfArena* arena, SourceSpan span) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_CONTINUE;
    stmt->span = span;
    return stmt;
}

UfStmt* uf_stmt_function(UfArena* arena, SourceSpan span, const char* name, const char** params, size_t param_count, UfStmt* body) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_FUNCTION;
    stmt->span = span;
    stmt->as.function_stmt.name = name;
    stmt->as.function_stmt.params = params;
    stmt->as.function_stmt.param_count = param_count;
    stmt->as.function_stmt.body = body;
    return stmt;
}

UfStmt* uf_stmt_return(UfArena* arena, SourceSpan span, UfExpr* value) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_RETURN;
    stmt->span = span;
    stmt->as.return_stmt.value = value;
    return stmt;
}

UfStmt* uf_stmt_block(UfArena* arena, SourceSpan span, UfStmt** stmts, size_t count) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_BLOCK;
    stmt->span = span;
    stmt->as.block.stmts = stmts;
    stmt->as.block.count = count;
    return stmt;
}

UfStmt* uf_stmt_try_catch(UfArena* arena, SourceSpan span, UfStmt* try_block, const char* catch_var, UfStmt* catch_block) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_TRY_CATCH;
    stmt->span = span;
    stmt->as.try_catch.try_block = try_block;
    stmt->as.try_catch.catch_var = catch_var;
    stmt->as.try_catch.catch_block = catch_block;
    return stmt;
}

static void print_indent(FILE* out, int indent) {
    for (int i = 0; i < indent; ++i) {
        fprintf(out, "  ");
    }
}

void uf_ast_print_expr(const UfExpr* expr, FILE* out) {
    if (!expr) {
        fprintf(out, "<null-expr>");
        return;
    }

    switch (expr->kind) {
        case UF_EXPR_LITERAL_NULL:
            fprintf(out, "null");
            break;
        case UF_EXPR_LITERAL_BOOL:
            fprintf(out, "%s", expr->as.bool_val ? "true" : "false");
            break;
        case UF_EXPR_LITERAL_NUMBER:
            fprintf(out, "%.14g", expr->as.number_val);
            break;
        case UF_EXPR_LITERAL_STRING:
            fprintf(out, "\"%s\"", expr->as.string_val);
            break;
        case UF_EXPR_IDENTIFIER:
            fprintf(out, "%s", expr->as.identifier_name);
            break;
        case UF_EXPR_UNARY:
            fprintf(out, "(%s ", uf_token_kind_name(expr->as.unary.op));
            uf_ast_print_expr(expr->as.unary.operand, out);
            fprintf(out, ")");
            break;
        case UF_EXPR_BINARY:
            fprintf(out, "(%s ", uf_token_kind_name(expr->as.binary.op));
            uf_ast_print_expr(expr->as.binary.left, out);
            fprintf(out, " ");
            uf_ast_print_expr(expr->as.binary.right, out);
            fprintf(out, ")");
            break;
        case UF_EXPR_CALL:
            fprintf(out, "(call ");
            uf_ast_print_expr(expr->as.call.callee, out);
            for (size_t i = 0; i < expr->as.call.argc; ++i) {
                fprintf(out, " ");
                uf_ast_print_expr(expr->as.call.args[i], out);
            }
            fprintf(out, ")");
            break;
        case UF_EXPR_GROUPING:
            uf_ast_print_expr(expr->as.grouping.inner, out);
            break;
        case UF_EXPR_ARRAY:
            fprintf(out, "(array");
            for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                fprintf(out, " ");
                uf_ast_print_expr(expr->as.array_lit.elements[i], out);
            }
            fprintf(out, ")");
            break;
        case UF_EXPR_INDEX:
            fprintf(out, "(index ");
            uf_ast_print_expr(expr->as.index_expr.target, out);
            fprintf(out, " ");
            uf_ast_print_expr(expr->as.index_expr.index, out);
            fprintf(out, ")");
            break;
        case UF_EXPR_MAP:
            fprintf(out, "(map");
            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                fprintf(out, " (pair ");
                uf_ast_print_expr(expr->as.map_lit.keys[i], out);
                fprintf(out, " ");
                uf_ast_print_expr(expr->as.map_lit.values[i], out);
                fprintf(out, ")");
            }
            fprintf(out, ")");
            break;
        case UF_EXPR_FUNCTION:
            fprintf(out, "(fn %s (params", expr->as.fn_expr.name ? expr->as.fn_expr.name : "<anonymous>");
            for (size_t i = 0; i < expr->as.fn_expr.param_count; ++i) {
                fprintf(out, " %s", expr->as.fn_expr.params[i]);
            }
            fprintf(out, ") ");
            uf_ast_print_stmt(expr->as.fn_expr.body, out, 0);
            fprintf(out, ")");
            break;
    }
}

void uf_ast_print_stmt(const UfStmt* stmt, FILE* out, int indent) {
    if (!stmt) return;

    print_indent(out, indent);

    switch (stmt->kind) {
        case UF_STMT_LET:
            fprintf(out, "(let %s", stmt->as.let_stmt.name);
            if (stmt->as.let_stmt.init) {
                fprintf(out, " ");
                uf_ast_print_expr(stmt->as.let_stmt.init, out);
            }
            fprintf(out, ")\n");
            break;

        case UF_STMT_ASSIGN:
            fprintf(out, "(assign %s ", stmt->as.assign_stmt.name);
            uf_ast_print_expr(stmt->as.assign_stmt.value, out);
            fprintf(out, ")\n");
            break;

        case UF_STMT_INDEX_ASSIGN:
            fprintf(out, "(index-assign ");
            uf_ast_print_expr(stmt->as.index_assign.target, out);
            fprintf(out, " ");
            uf_ast_print_expr(stmt->as.index_assign.index, out);
            fprintf(out, " ");
            uf_ast_print_expr(stmt->as.index_assign.value, out);
            fprintf(out, ")\n");
            break;

        case UF_STMT_SAY:
            fprintf(out, "(say ");
            uf_ast_print_expr(stmt->as.say_stmt.expr, out);
            fprintf(out, ")\n");
            break;

        case UF_STMT_EXPR:
            fprintf(out, "(expr ");
            uf_ast_print_expr(stmt->as.expr_stmt.expr, out);
            fprintf(out, ")\n");
            break;

        case UF_STMT_IF:
            fprintf(out, "(if ");
            uf_ast_print_expr(stmt->as.if_stmt.condition, out);
            fprintf(out, "\n");
            uf_ast_print_stmt(stmt->as.if_stmt.then_branch, out, indent + 1);
            if (stmt->as.if_stmt.else_branch) {
                print_indent(out, indent);
                fprintf(out, "else\n");
                uf_ast_print_stmt(stmt->as.if_stmt.else_branch, out, indent + 1);
            }
            print_indent(out, indent);
            fprintf(out, ")\n");
            break;

        case UF_STMT_WHILE:
            fprintf(out, "(while ");
            uf_ast_print_expr(stmt->as.while_stmt.condition, out);
            fprintf(out, "\n");
            uf_ast_print_stmt(stmt->as.while_stmt.body, out, indent + 1);
            print_indent(out, indent);
            fprintf(out, ")\n");
            break;

        case UF_STMT_REPEAT:
            fprintf(out, "(repeat ");
            uf_ast_print_expr(stmt->as.repeat_stmt.count_expr, out);
            fprintf(out, " times\n");
            uf_ast_print_stmt(stmt->as.repeat_stmt.body, out, indent + 1);
            print_indent(out, indent);
            fprintf(out, ")\n");
            break;

        case UF_STMT_FOR:
            fprintf(out, "(for %s in ", stmt->as.for_stmt.var_name);
            uf_ast_print_expr(stmt->as.for_stmt.iterable, out);
            fprintf(out, "\n");
            uf_ast_print_stmt(stmt->as.for_stmt.body, out, indent + 1);
            print_indent(out, indent);
            fprintf(out, ")\n");
            break;

        case UF_STMT_BREAK:
            fprintf(out, "(break)\n");
            break;

        case UF_STMT_CONTINUE:
            fprintf(out, "(continue)\n");
            break;

        case UF_STMT_FUNCTION:
            fprintf(out, "(function %s (", stmt->as.function_stmt.name);
            for (size_t i = 0; i < stmt->as.function_stmt.param_count; ++i) {
                fprintf(out, "%s%s", stmt->as.function_stmt.params[i],
                        (i + 1 < stmt->as.function_stmt.param_count) ? ", " : "");
            }
            fprintf(out, ")\n");
            uf_ast_print_stmt(stmt->as.function_stmt.body, out, indent + 1);
            print_indent(out, indent);
            fprintf(out, ")\n");
            break;

        case UF_STMT_RETURN:
            fprintf(out, "(return");
            if (stmt->as.return_stmt.value) {
                fprintf(out, " ");
                uf_ast_print_expr(stmt->as.return_stmt.value, out);
            }
            fprintf(out, ")\n");
            break;

        case UF_STMT_BLOCK:
            fprintf(out, "(block\n");
            for (size_t i = 0; i < stmt->as.block.count; ++i) {
                uf_ast_print_stmt(stmt->as.block.stmts[i], out, indent + 1);
            }
            print_indent(out, indent);
            fprintf(out, ")\n");
            break;

        case UF_STMT_TRY_CATCH:
            fprintf(out, "(try\n");
            uf_ast_print_stmt(stmt->as.try_catch.try_block, out, indent + 1);
            print_indent(out, indent);
            fprintf(out, " catch %s\n", stmt->as.try_catch.catch_var ? stmt->as.try_catch.catch_var : "_");
            uf_ast_print_stmt(stmt->as.try_catch.catch_block, out, indent + 1);
            print_indent(out, indent);
            fprintf(out, ")\n");
            break;
    }
}

void uf_ast_print(const UfProgram* program, FILE* out) {
    fprintf(out, "(program\n");
    for (size_t i = 0; i < program->count; ++i) {
        uf_ast_print_stmt(program->stmts[i], out, 1);
    }
    fprintf(out, ")\n");
}
