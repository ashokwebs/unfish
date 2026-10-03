#include "uf_optimize.h"
#include "uf_opcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void fold_expr(UfExpr* expr, UfRuntime* rt);
static void fold_stmt(UfStmt* stmt, UfRuntime* rt);

static void fold_expr(UfExpr* expr, UfRuntime* rt) {
    if (!expr) return;

    switch (expr->kind) {
        case UF_EXPR_UNARY: {
            fold_expr(expr->as.unary.operand, rt);
            UfExpr* op = expr->as.unary.operand;
            if (expr->as.unary.op == UF_TOK_MINUS && op->kind == UF_EXPR_LITERAL_NUMBER) {
                uf_expr_set_number(expr, -op->as.number_val);
            } else if (expr->as.unary.op == UF_TOK_NOT && op->kind == UF_EXPR_LITERAL_BOOL) {
                expr->kind = UF_EXPR_LITERAL_BOOL;
                expr->as.bool_val = !op->as.bool_val;
            }
            break;
        }
        case UF_EXPR_BINARY: {
            fold_expr(expr->as.binary.left, rt);
            fold_expr(expr->as.binary.right, rt);
            UfExpr* left = expr->as.binary.left;
            UfExpr* right = expr->as.binary.right;

            if (left->kind == UF_EXPR_LITERAL_NUMBER && right->kind == UF_EXPR_LITERAL_NUMBER) {
                double a = left->as.number_val;
                double b = right->as.number_val;
                switch (expr->as.binary.op) {
                    case UF_TOK_PLUS:
                        uf_expr_set_number(expr, a + b);
                        break;
                    case UF_TOK_MINUS:
                        uf_expr_set_number(expr, a - b);
                        break;
                    case UF_TOK_STAR:
                        uf_expr_set_number(expr, a * b);
                        break;
                    case UF_TOK_SLASH:
                        if (b != 0.0) {
                            uf_expr_set_number(expr, a / b);
                        }
                        break;
                    case UF_TOK_PERCENT:
                        if (b != 0.0) {
                            uf_expr_set_number(expr, fmod(a, b));
                        }
                        break;
                    case UF_TOK_EQEQ:
                        expr->kind = UF_EXPR_LITERAL_BOOL;
                        expr->as.bool_val = (a == b);
                        break;
                    case UF_TOK_BANGEQ:
                        expr->kind = UF_EXPR_LITERAL_BOOL;
                        expr->as.bool_val = (a != b);
                        break;
                    case UF_TOK_LT:
                        expr->kind = UF_EXPR_LITERAL_BOOL;
                        expr->as.bool_val = (a < b);
                        break;
                    case UF_TOK_LTEQ:
                        expr->kind = UF_EXPR_LITERAL_BOOL;
                        expr->as.bool_val = (a <= b);
                        break;
                    case UF_TOK_GT:
                        expr->kind = UF_EXPR_LITERAL_BOOL;
                        expr->as.bool_val = (a > b);
                        break;
                    case UF_TOK_GTEQ:
                        expr->kind = UF_EXPR_LITERAL_BOOL;
                        expr->as.bool_val = (a >= b);
                        break;
                    default:
                        break;
                }
            } else if (left->kind == UF_EXPR_LITERAL_STRING && right->kind == UF_EXPR_LITERAL_STRING) {
                if (expr->as.binary.op == UF_TOK_PLUS) {
                    size_t len_a = strlen(left->as.string_val);
                    size_t len_b = strlen(right->as.string_val);
                    char* merged = (char*)malloc(len_a + len_b + 1);
                    memcpy(merged, left->as.string_val, len_a);
                    memcpy(merged + len_a, right->as.string_val, len_b);
                    merged[len_a + len_b] = '\0';
                    UfValue str_val = uf_val_string_take(rt, merged, len_a + len_b);
                    expr->kind = UF_EXPR_LITERAL_STRING;
                    expr->as.string_val = str_val.as.string->chars;
                } else if (expr->as.binary.op == UF_TOK_EQEQ) {
                    expr->kind = UF_EXPR_LITERAL_BOOL;
                    expr->as.bool_val = (strcmp(left->as.string_val, right->as.string_val) == 0);
                } else if (expr->as.binary.op == UF_TOK_BANGEQ) {
                    expr->kind = UF_EXPR_LITERAL_BOOL;
                    expr->as.bool_val = (strcmp(left->as.string_val, right->as.string_val) != 0);
                }
            } else if (left->kind == UF_EXPR_LITERAL_BOOL && right->kind == UF_EXPR_LITERAL_BOOL) {
                bool a = left->as.bool_val;
                bool b = right->as.bool_val;
                if (expr->as.binary.op == UF_TOK_AND) {
                    expr->kind = UF_EXPR_LITERAL_BOOL;
                    expr->as.bool_val = a && b;
                } else if (expr->as.binary.op == UF_TOK_OR) {
                    expr->kind = UF_EXPR_LITERAL_BOOL;
                    expr->as.bool_val = a || b;
                } else if (expr->as.binary.op == UF_TOK_EQEQ) {
                    expr->kind = UF_EXPR_LITERAL_BOOL;
                    expr->as.bool_val = (a == b);
                } else if (expr->as.binary.op == UF_TOK_BANGEQ) {
                    expr->kind = UF_EXPR_LITERAL_BOOL;
                    expr->as.bool_val = (a != b);
                }
            } else if (expr->as.binary.op == UF_TOK_AND && left->kind == UF_EXPR_LITERAL_BOOL && !left->as.bool_val) {
                expr->kind = UF_EXPR_LITERAL_BOOL;
                expr->as.bool_val = false;
            } else if (expr->as.binary.op == UF_TOK_OR && left->kind == UF_EXPR_LITERAL_BOOL && left->as.bool_val) {
                expr->kind = UF_EXPR_LITERAL_BOOL;
                expr->as.bool_val = true;
            }
            break;
        }
        case UF_EXPR_GROUPING:
            fold_expr(expr->as.grouping.inner, rt);
            if (expr->as.grouping.inner->kind == UF_EXPR_LITERAL_NUMBER ||
                expr->as.grouping.inner->kind == UF_EXPR_LITERAL_STRING ||
                expr->as.grouping.inner->kind == UF_EXPR_LITERAL_BOOL ||
                expr->as.grouping.inner->kind == UF_EXPR_LITERAL_NULL) {
                *expr = *expr->as.grouping.inner;
            }
            break;
        case UF_EXPR_CALL:
            for (size_t i = 0; i < expr->as.call.argc; ++i) {
                fold_expr(expr->as.call.args[i], rt);
            }
            break;
        case UF_EXPR_ARRAY:
            for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                fold_expr(expr->as.array_lit.elements[i], rt);
            }
            break;
        case UF_EXPR_MAP:
            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                fold_expr(expr->as.map_lit.keys[i], rt);
                if (expr->as.map_lit.values[i]) {
                    fold_expr(expr->as.map_lit.values[i], rt);
                }
            }
            break;
        case UF_EXPR_SPREAD:
            fold_expr(expr->as.spread.operand, rt);
            break;
        case UF_EXPR_INDEX:
            fold_expr(expr->as.index_expr.target, rt);
            fold_expr(expr->as.index_expr.index, rt);
            break;
        case UF_EXPR_STRING_INTERP:
            for (size_t i = 0; i < expr->as.string_interp.count; ++i) {
                fold_expr(expr->as.string_interp.parts[i], rt);
            }
            break;
        case UF_EXPR_FUNCTION:
            if (expr->as.fn_expr.param_defaults) {
                for (size_t i = 0; i < expr->as.fn_expr.param_count; ++i) {
                    if (expr->as.fn_expr.param_defaults[i]) {
                        fold_expr(expr->as.fn_expr.param_defaults[i], rt);
                    }
                }
            }
            if (expr->as.fn_expr.body) {
                fold_stmt(expr->as.fn_expr.body, rt);
            }
            break;
        default:
            break;
    }
}

static void fold_stmt(UfStmt* stmt, UfRuntime* rt) {
    if (!stmt) return;

    switch (stmt->kind) {
        case UF_STMT_LET:
            fold_expr(stmt->as.let_stmt.init, rt);
            break;
        case UF_STMT_ASSIGN:
            fold_expr(stmt->as.assign_stmt.value, rt);
            break;
        case UF_STMT_INDEX_ASSIGN:
            fold_expr(stmt->as.index_assign.target, rt);
            fold_expr(stmt->as.index_assign.index, rt);
            fold_expr(stmt->as.index_assign.value, rt);
            break;
        case UF_STMT_SAY:
            fold_expr(stmt->as.say_stmt.expr, rt);
            break;
        case UF_STMT_EXPR:
            fold_expr(stmt->as.expr_stmt.expr, rt);
            break;
        case UF_STMT_IF:
            fold_expr(stmt->as.if_stmt.condition, rt);
            fold_stmt(stmt->as.if_stmt.then_branch, rt);
            if (stmt->as.if_stmt.else_branch) fold_stmt(stmt->as.if_stmt.else_branch, rt);

            if (stmt->as.if_stmt.condition->kind == UF_EXPR_LITERAL_BOOL) {
                if (stmt->as.if_stmt.condition->as.bool_val) {
                    if (stmt->as.if_stmt.then_branch) {
                        *stmt = *stmt->as.if_stmt.then_branch;
                    }
                } else {
                    if (stmt->as.if_stmt.else_branch) {
                        *stmt = *stmt->as.if_stmt.else_branch;
                    } else {
                        stmt->kind = UF_STMT_BLOCK;
                        stmt->as.block.stmts = NULL;
                        stmt->as.block.count = 0;
                    }
                }
            }
            break;
        case UF_STMT_WHILE:
            fold_expr(stmt->as.while_stmt.condition, rt);
            fold_stmt(stmt->as.while_stmt.body, rt);
            if (stmt->as.while_stmt.condition->kind == UF_EXPR_LITERAL_BOOL &&
                !stmt->as.while_stmt.condition->as.bool_val) {
                stmt->kind = UF_STMT_BLOCK;
                stmt->as.block.stmts = NULL;
                stmt->as.block.count = 0;
            }
            break;
        case UF_STMT_REPEAT:
            fold_expr(stmt->as.repeat_stmt.count_expr, rt);
            fold_stmt(stmt->as.repeat_stmt.body, rt);
            break;
        case UF_STMT_FOR:
            fold_expr(stmt->as.for_stmt.iterable, rt);
            fold_stmt(stmt->as.for_stmt.body, rt);
            break;
        case UF_STMT_FUNCTION:
            if (stmt->as.function_stmt.param_defaults) {
                for (size_t i = 0; i < stmt->as.function_stmt.param_count; ++i) {
                    if (stmt->as.function_stmt.param_defaults[i]) {
                        fold_expr(stmt->as.function_stmt.param_defaults[i], rt);
                    }
                }
            }
            fold_stmt(stmt->as.function_stmt.body, rt);
            break;
        case UF_STMT_RETURN:
            if (stmt->as.return_stmt.value) fold_expr(stmt->as.return_stmt.value, rt);
            break;
        case UF_STMT_BLOCK: {
            size_t valid_count = stmt->as.block.count;
            for (size_t i = 0; i < stmt->as.block.count; ++i) {
                fold_stmt(stmt->as.block.stmts[i], rt);
                if (stmt->as.block.stmts[i]->kind == UF_STMT_RETURN && i + 1 < stmt->as.block.count) {
                    /* Dead code elimination after unconditional return */
                    valid_count = i + 1;
                    break;
                }
            }
            stmt->as.block.count = valid_count;
            break;
        }
        case UF_STMT_STRUCT:
            for (size_t i = 0; i < stmt->as.struct_stmt.method_count; ++i) {
                fold_stmt(stmt->as.struct_stmt.methods[i], rt);
            }
            break;
        case UF_STMT_ENUM:
            break;
        default:
            break;
    }
}

void uf_optimize_ast(UfProgram* program, UfRuntime* rt) {
    if (!program) return;
    for (size_t i = 0; i < program->count; ++i) {
        fold_stmt(program->stmts[i], rt);
    }
}

void uf_optimize_chunk(UfChunk* chunk, UfRuntime* rt) {
    (void)rt;
    if (!chunk || chunk->code_count == 0) return;

    /* Peephole pass: dead return elimination */
    for (size_t i = 0; i + 1 < chunk->code_count; ++i) {
        if (chunk->code[i] == OP_RETURN && chunk->code[i + 1] == OP_RETURN) {
            /* Redundant return */
        }
    }
}

void uf_optimize_function_tree(UfBytecodeFunction* fn, UfRuntime* rt) {
    if (!fn) return;

    uf_optimize_chunk(&fn->chunk, rt);

    /* Recursively optimize enclosed functions in constant pool */
    for (size_t i = 0; i < fn->chunk.const_count; ++i) {
        UfValue val = fn->chunk.constants[i];
        if (val.kind == UF_VAL_BYTECODE_FN) {
            uf_optimize_function_tree(val.as.bytecode_fn, rt);
        }
    }
}
