#include "uf_emit_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void emit_indent(FILE* out, int indent) {
    for (int i = 0; i < indent; ++i) fputs("    ", out);
}

static void emit_escaped_string(FILE* out, const char* s) {
    fputc('"', out);
    if (s) {
        for (const char* p = s; *p; ++p) {
            switch (*p) {
                case '\n': fputs("\\n", out); break;
                case '\t': fputs("\\t", out); break;
                case '\r': fputs("\\r", out); break;
                case '\\': fputs("\\\\", out); break;
                case '"':  fputs("\\\"", out); break;
                default:   fputc(*p, out); break;
            }
        }
    }
    fputc('"', out);
}

typedef struct {
    const UfExpr* fn_expr;
    int id;
    const char* captures[32];
    size_t capture_count;
} UfLambdaInfo;

static const char* g_declared_fns[256];
static size_t g_declared_fn_count = 0;

static UfLambdaInfo g_lambdas[256];
static size_t g_lambda_count = 0;

static bool is_declared_function(const char* name) {
    for (size_t i = 0; i < g_declared_fn_count; ++i) {
        if (strcmp(g_declared_fns[i], name) == 0) return true;
    }
    return false;
}

static int find_lambda_id(const UfExpr* expr) {
    for (size_t i = 0; i < g_lambda_count; ++i) {
        if (g_lambdas[i].fn_expr == expr) return g_lambdas[i].id;
    }
    return -1;
}

static bool is_in_list(const char* name, const char** list, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(name, list[i]) == 0) return true;
    }
    return false;
}

static bool is_builtin_name(const char* name) {
    static const char* builtins[] = {
        "say", "print", "len", "type_of", "push",
        "abs", "floor", "ceil", "round", "sqrt", "pow", "min", "max", "log", "sin", "cos", "tan", "random", "random_int",
        "trim", "to_upper", "to_lower", "contains", "starts_with", "ends_with", "char_at", "to_number", "to_string",
        "repeat_string", "substring", "index_of", "split", "join", "replace",
        "buffer", "buffer_from_string", "buffer_to_string", "buffer_size", "buffer_get", "buffer_set",
        "buffer_fill", "buffer_slice", "buffer_read_u16_le", "buffer_write_u16_le",
        "buffer_read_u32_le", "buffer_write_u32_le", "buffer_read_i32_le", "buffer_write_i32_le",
        "u8", "i8", "u16", "i16", "u32", "i32",
        "band", "bor", "bxor", "bnot", "shl", "shr", "sar", "to_hex", "from_hex", "buffer_to_hex", "buffer_from_hex",
        "PI", "E", "INFINITY"
    };
    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); ++i) {
        if (strcmp(name, builtins[i]) == 0) return true;
    }
    return false;
}

static void collect_lambdas_expr(const UfExpr* expr);
static void collect_lambdas_stmt(const UfStmt* stmt);

static void collect_lambdas_expr(const UfExpr* expr) {
    if (!expr) return;
    if (expr->kind == UF_EXPR_FUNCTION) {
        if (g_lambda_count < 256) {
            g_lambdas[g_lambda_count].fn_expr = expr;
            g_lambdas[g_lambda_count].id = (int)g_lambda_count;
            g_lambdas[g_lambda_count].capture_count = 0;
            g_lambda_count++;
        }
        collect_lambdas_stmt(expr->as.fn_expr.body);
        return;
    }
    switch (expr->kind) {
        case UF_EXPR_UNARY:
            collect_lambdas_expr(expr->as.unary.operand);
            break;
        case UF_EXPR_BINARY:
            collect_lambdas_expr(expr->as.binary.left);
            collect_lambdas_expr(expr->as.binary.right);
            break;
        case UF_EXPR_CALL:
            collect_lambdas_expr(expr->as.call.callee);
            for (size_t i = 0; i < expr->as.call.argc; ++i) {
                collect_lambdas_expr(expr->as.call.args[i]);
            }
            break;
        case UF_EXPR_GROUPING:
            collect_lambdas_expr(expr->as.grouping.inner);
            break;
        case UF_EXPR_ARRAY:
            for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                collect_lambdas_expr(expr->as.array_lit.elements[i]);
            }
            break;
        case UF_EXPR_MAP:
            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                collect_lambdas_expr(expr->as.map_lit.keys[i]);
                collect_lambdas_expr(expr->as.map_lit.values[i]);
            }
            break;
        case UF_EXPR_INDEX:
            collect_lambdas_expr(expr->as.index_expr.target);
            collect_lambdas_expr(expr->as.index_expr.index);
            break;
        default:
            break;
    }
}

static void collect_lambdas_stmt(const UfStmt* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case UF_STMT_LET:
            collect_lambdas_expr(stmt->as.let_stmt.init);
            break;
        case UF_STMT_ASSIGN:
            collect_lambdas_expr(stmt->as.assign_stmt.value);
            break;
        case UF_STMT_INDEX_ASSIGN:
            collect_lambdas_expr(stmt->as.index_assign.target);
            collect_lambdas_expr(stmt->as.index_assign.index);
            collect_lambdas_expr(stmt->as.index_assign.value);
            break;
        case UF_STMT_SAY:
            collect_lambdas_expr(stmt->as.say_stmt.expr);
            break;
        case UF_STMT_EXPR:
            collect_lambdas_expr(stmt->as.expr_stmt.expr);
            break;
        case UF_STMT_IF:
            collect_lambdas_expr(stmt->as.if_stmt.condition);
            collect_lambdas_stmt(stmt->as.if_stmt.then_branch);
            if (stmt->as.if_stmt.else_branch) collect_lambdas_stmt(stmt->as.if_stmt.else_branch);
            break;
        case UF_STMT_WHILE:
            collect_lambdas_expr(stmt->as.while_stmt.condition);
            collect_lambdas_stmt(stmt->as.while_stmt.body);
            break;
        case UF_STMT_REPEAT:
            collect_lambdas_expr(stmt->as.repeat_stmt.count_expr);
            collect_lambdas_stmt(stmt->as.repeat_stmt.body);
            break;
        case UF_STMT_FOR:
            collect_lambdas_expr(stmt->as.for_stmt.iterable);
            collect_lambdas_stmt(stmt->as.for_stmt.body);
            break;
        case UF_STMT_FUNCTION:
            collect_lambdas_stmt(stmt->as.function_stmt.body);
            break;
        case UF_STMT_RETURN:
            if (stmt->as.return_stmt.value) collect_lambdas_expr(stmt->as.return_stmt.value);
            break;
        case UF_STMT_BLOCK:
            for (size_t i = 0; i < stmt->as.block.count; ++i) {
                collect_lambdas_stmt(stmt->as.block.stmts[i]);
            }
            break;
        default:
            break;
    }
}

static void find_captures_expr(const UfExpr* expr, const char** locals, size_t local_count, const char** params, size_t param_count, UfLambdaInfo* info);
static void find_captures_stmt(const UfStmt* stmt, const char** locals, size_t* p_local_count, const char** params, size_t param_count, UfLambdaInfo* info);

static void find_captures_expr(const UfExpr* expr, const char** locals, size_t local_count, const char** params, size_t param_count, UfLambdaInfo* info) {
    if (!expr) return;
    if (expr->kind == UF_EXPR_IDENTIFIER) {
        const char* name = expr->as.identifier_name;
        if (!is_in_list(name, params, param_count) &&
            !is_in_list(name, locals, local_count) &&
            !is_declared_function(name) &&
            !is_builtin_name(name) &&
            !is_in_list(name, info->captures, info->capture_count)) {
            if (info->capture_count < 32) {
                info->captures[info->capture_count++] = name;
            }
        }
        return;
    }
    if (expr->kind == UF_EXPR_FUNCTION) {
        return;
    }
    switch (expr->kind) {
        case UF_EXPR_UNARY:
            find_captures_expr(expr->as.unary.operand, locals, local_count, params, param_count, info);
            break;
        case UF_EXPR_BINARY:
            find_captures_expr(expr->as.binary.left, locals, local_count, params, param_count, info);
            find_captures_expr(expr->as.binary.right, locals, local_count, params, param_count, info);
            break;
        case UF_EXPR_CALL:
            find_captures_expr(expr->as.call.callee, locals, local_count, params, param_count, info);
            for (size_t i = 0; i < expr->as.call.argc; ++i) {
                find_captures_expr(expr->as.call.args[i], locals, local_count, params, param_count, info);
            }
            break;
        case UF_EXPR_GROUPING:
            find_captures_expr(expr->as.grouping.inner, locals, local_count, params, param_count, info);
            break;
        case UF_EXPR_ARRAY:
            for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                find_captures_expr(expr->as.array_lit.elements[i], locals, local_count, params, param_count, info);
            }
            break;
        case UF_EXPR_MAP:
            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                find_captures_expr(expr->as.map_lit.keys[i], locals, local_count, params, param_count, info);
                find_captures_expr(expr->as.map_lit.values[i], locals, local_count, params, param_count, info);
            }
            break;
        case UF_EXPR_INDEX:
            find_captures_expr(expr->as.index_expr.target, locals, local_count, params, param_count, info);
            find_captures_expr(expr->as.index_expr.index, locals, local_count, params, param_count, info);
            break;
        default:
            break;
    }
}

static void find_captures_stmt(const UfStmt* stmt, const char** locals, size_t* p_local_count, const char** params, size_t param_count, UfLambdaInfo* info) {
    if (!stmt) return;
    switch (stmt->kind) {
        case UF_STMT_LET:
            find_captures_expr(stmt->as.let_stmt.init, locals, *p_local_count, params, param_count, info);
            if (*p_local_count < 64) {
                locals[(*p_local_count)++] = stmt->as.let_stmt.name;
            }
            break;
        case UF_STMT_ASSIGN:
            find_captures_expr(stmt->as.assign_stmt.value, locals, *p_local_count, params, param_count, info);
            break;
        case UF_STMT_INDEX_ASSIGN:
            find_captures_expr(stmt->as.index_assign.target, locals, *p_local_count, params, param_count, info);
            find_captures_expr(stmt->as.index_assign.index, locals, *p_local_count, params, param_count, info);
            find_captures_expr(stmt->as.index_assign.value, locals, *p_local_count, params, param_count, info);
            break;
        case UF_STMT_SAY:
            find_captures_expr(stmt->as.say_stmt.expr, locals, *p_local_count, params, param_count, info);
            break;
        case UF_STMT_EXPR:
            find_captures_expr(stmt->as.expr_stmt.expr, locals, *p_local_count, params, param_count, info);
            break;
        case UF_STMT_IF:
            find_captures_expr(stmt->as.if_stmt.condition, locals, *p_local_count, params, param_count, info);
            find_captures_stmt(stmt->as.if_stmt.then_branch, locals, p_local_count, params, param_count, info);
            if (stmt->as.if_stmt.else_branch) {
                find_captures_stmt(stmt->as.if_stmt.else_branch, locals, p_local_count, params, param_count, info);
            }
            break;
        case UF_STMT_WHILE:
            find_captures_expr(stmt->as.while_stmt.condition, locals, *p_local_count, params, param_count, info);
            find_captures_stmt(stmt->as.while_stmt.body, locals, p_local_count, params, param_count, info);
            break;
        case UF_STMT_REPEAT:
            find_captures_expr(stmt->as.repeat_stmt.count_expr, locals, *p_local_count, params, param_count, info);
            find_captures_stmt(stmt->as.repeat_stmt.body, locals, p_local_count, params, param_count, info);
            break;
        case UF_STMT_FOR:
            find_captures_expr(stmt->as.for_stmt.iterable, locals, *p_local_count, params, param_count, info);
            if (*p_local_count < 64) {
                locals[(*p_local_count)++] = stmt->as.for_stmt.var_name;
            }
            find_captures_stmt(stmt->as.for_stmt.body, locals, p_local_count, params, param_count, info);
            break;
        case UF_STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                find_captures_expr(stmt->as.return_stmt.value, locals, *p_local_count, params, param_count, info);
            }
            break;
        case UF_STMT_BLOCK:
            for (size_t i = 0; i < stmt->as.block.count; ++i) {
                find_captures_stmt(stmt->as.block.stmts[i], locals, p_local_count, params, param_count, info);
            }
            break;
        default:
            break;
    }
}

static void emit_expr(FILE* out, const UfExpr* expr);

static void emit_call_arg(FILE* out, size_t argc, const UfExpr** args, size_t index) {
    if (index < argc && args[index]) {
        emit_expr(out, args[index]);
    } else {
        fputs("uf_null()", out);
    }
}

static void emit_expr(FILE* out, const UfExpr* expr) {
    if (!expr) {
        fputs("uf_null()", out);
        return;
    }

    switch (expr->kind) {
        case UF_EXPR_LITERAL_NULL:
            fputs("uf_null()", out);
            break;
        case UF_EXPR_LITERAL_BOOL:
            fprintf(out, "uf_bool(%s)", expr->as.bool_val ? "true" : "false");
            break;
        case UF_EXPR_LITERAL_NUMBER:
            fprintf(out, "uf_num(%.17g)", expr->as.number_val);
            break;
        case UF_EXPR_LITERAL_STRING:
            fputs("uf_str(", out);
            emit_escaped_string(out, expr->as.string_val);
            fputc(')', out);
            break;
        case UF_EXPR_IDENTIFIER:
            if (strcmp(expr->as.identifier_name, "PI") == 0) {
                fputs("uf_num(3.14159265358979323846)", out);
            } else if (strcmp(expr->as.identifier_name, "E") == 0) {
                fputs("uf_num(2.71828182845904523536)", out);
            } else if (strcmp(expr->as.identifier_name, "INFINITY") == 0) {
                fputs("uf_num(HUGE_VAL)", out);
            } else if (is_declared_function(expr->as.identifier_name)) {
                fprintf(out, "uf_wrap_fn_%s()", expr->as.identifier_name);
            } else {
                fprintf(out, "uf_var_%s", expr->as.identifier_name);
            }
            break;
        case UF_EXPR_UNARY:
            if (expr->as.unary.op == UF_TOK_MINUS) {
                fputs("uf_neg(", out);
                emit_expr(out, expr->as.unary.operand);
                fputc(')', out);
            } else {
                fputs("uf_not(", out);
                emit_expr(out, expr->as.unary.operand);
                fputc(')', out);
            }
            break;
        case UF_EXPR_BINARY: {
            switch (expr->as.binary.op) {
                case UF_TOK_PLUS:
                    fputs("uf_add(", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs(", ", out);
                    emit_expr(out, expr->as.binary.right);
                    fputc(')', out);
                    break;
                case UF_TOK_MINUS:
                    fputs("uf_sub(", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs(", ", out);
                    emit_expr(out, expr->as.binary.right);
                    fputc(')', out);
                    break;
                case UF_TOK_STAR:
                    fputs("uf_mul(", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs(", ", out);
                    emit_expr(out, expr->as.binary.right);
                    fputc(')', out);
                    break;
                case UF_TOK_SLASH:
                    fputs("uf_div(", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs(", ", out);
                    emit_expr(out, expr->as.binary.right);
                    fputc(')', out);
                    break;
                case UF_TOK_PERCENT:
                    fputs("uf_mod(", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs(", ", out);
                    emit_expr(out, expr->as.binary.right);
                    fputc(')', out);
                    break;
                case UF_TOK_EQEQ:
                    fputs("uf_eq(", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs(", ", out);
                    emit_expr(out, expr->as.binary.right);
                    fputc(')', out);
                    break;
                case UF_TOK_BANGEQ:
                    fputs("uf_neq(", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs(", ", out);
                    emit_expr(out, expr->as.binary.right);
                    fputc(')', out);
                    break;
                case UF_TOK_LT:
                    fputs("uf_lt(", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs(", ", out);
                    emit_expr(out, expr->as.binary.right);
                    fputc(')', out);
                    break;
                case UF_TOK_LTEQ:
                    fputs("uf_lte(", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs(", ", out);
                    emit_expr(out, expr->as.binary.right);
                    fputc(')', out);
                    break;
                case UF_TOK_GT:
                    fputs("uf_gt(", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs(", ", out);
                    emit_expr(out, expr->as.binary.right);
                    fputc(')', out);
                    break;
                case UF_TOK_GTEQ:
                    fputs("uf_gte(", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs(", ", out);
                    emit_expr(out, expr->as.binary.right);
                    fputc(')', out);
                    break;
                case UF_TOK_AND:
                    fputs("uf_bool(uf_truthy(", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs(") && uf_truthy(", out);
                    emit_expr(out, expr->as.binary.right);
                    fputs("))", out);
                    break;
                case UF_TOK_OR:
                    fputs("uf_bool(uf_truthy(", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs(") || uf_truthy(", out);
                    emit_expr(out, expr->as.binary.right);
                    fputs("))", out);
                    break;
                default:
                    fputs("uf_null()", out);
                    break;
            }
            break;
        }
        case UF_EXPR_CALL: {
            if (expr->as.call.callee && expr->as.call.callee->kind == UF_EXPR_IDENTIFIER) {
                const char* fn_name = expr->as.call.callee->as.identifier_name;
                size_t argc = expr->as.call.argc;
                const UfExpr** args = (const UfExpr**)expr->as.call.args;

                #define UF_ARG(i) emit_call_arg(out, argc, args, (i))

                if (strcmp(fn_name, "say") == 0) {
                    fputs("(uf_say(", out); UF_ARG(0); fputs("), uf_null())", out);
                    return;
                }
                if (strcmp(fn_name, "print") == 0) {
                    fputs("(uf_print(", out); UF_ARG(0); fputs("), uf_null())", out);
                    return;
                }
                if (strcmp(fn_name, "len") == 0) {
                    fputs("uf_len(", out); UF_ARG(0); fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "type_of") == 0) {
                    fputs("uf_type_of(", out); UF_ARG(0); fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "push") == 0) {
                    fputs("(uf_array_push(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputs("), uf_null())", out);
                    return;
                }

                /* Math builtins */
                if (strcmp(fn_name, "abs") == 0) { fputs("uf_math_abs(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "floor") == 0) { fputs("uf_math_floor(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "ceil") == 0) { fputs("uf_math_ceil(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "round") == 0) { fputs("uf_math_round(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "sqrt") == 0) { fputs("uf_math_sqrt(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "pow") == 0) { fputs("uf_math_pow(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "min") == 0) { fputs("uf_math_min(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "max") == 0) { fputs("uf_math_max(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "log") == 0) { fputs("uf_math_log(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "sin") == 0) { fputs("uf_math_sin(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "cos") == 0) { fputs("uf_math_cos(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "tan") == 0) { fputs("uf_math_tan(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "random") == 0) { fputs("uf_math_random()", out); return; }
                if (strcmp(fn_name, "random_int") == 0) { fputs("uf_math_random_int(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }

                /* String builtins */
                if (strcmp(fn_name, "trim") == 0) { fputs("uf_str_trim(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "to_upper") == 0) { fputs("uf_str_to_upper(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "to_lower") == 0) { fputs("uf_str_to_lower(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "contains") == 0) { fputs("uf_str_contains(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "starts_with") == 0) { fputs("uf_str_starts_with(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "ends_with") == 0) { fputs("uf_str_ends_with(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "char_at") == 0) { fputs("uf_str_char_at(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "to_number") == 0) { fputs("uf_str_to_number(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "to_string") == 0) { fputs("uf_str_to_string(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "repeat_string") == 0) { fputs("uf_str_repeat(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "substring") == 0) { fputs("uf_str_substring(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputs(", ", out); UF_ARG(2); fputc(')', out); return; }
                if (strcmp(fn_name, "index_of") == 0) { fputs("uf_str_index_of(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "split") == 0) { fputs("uf_str_split(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "join") == 0) { fputs("uf_str_join(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "replace") == 0) { fputs("uf_str_replace(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputs(", ", out); UF_ARG(2); fputc(')', out); return; }

                /* Buffer and Systems builtins */
                if (strcmp(fn_name, "buffer") == 0) { fputs("uf_buffer_new(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_from_string") == 0) { fputs("uf_buffer_from_string(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_to_string") == 0) { fputs("uf_buffer_to_string(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_size") == 0) { fputs("uf_buffer_size(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_get") == 0) { fputs("uf_buffer_get(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_set") == 0) { fputs("uf_buffer_set(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputs(", ", out); UF_ARG(2); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_fill") == 0) { fputs("uf_buffer_fill(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_slice") == 0) { fputs("uf_buffer_slice(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputs(", ", out); UF_ARG(2); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_read_u16_le") == 0) { fputs("uf_buffer_read_u16_le(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_write_u16_le") == 0) { fputs("uf_buffer_write_u16_le(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputs(", ", out); UF_ARG(2); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_read_u32_le") == 0) { fputs("uf_buffer_read_u32_le(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_write_u32_le") == 0) { fputs("uf_buffer_write_u32_le(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputs(", ", out); UF_ARG(2); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_read_i32_le") == 0) { fputs("uf_buffer_read_i32_le(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_write_i32_le") == 0) { fputs("uf_buffer_write_i32_le(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputs(", ", out); UF_ARG(2); fputc(')', out); return; }

                if (strcmp(fn_name, "u8") == 0) { fputs("uf_u8(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "i8") == 0) { fputs("uf_i8(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "u16") == 0) { fputs("uf_u16(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "i16") == 0) { fputs("uf_i16(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "u32") == 0) { fputs("uf_u32(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "i32") == 0) { fputs("uf_i32(", out); UF_ARG(0); fputc(')', out); return; }

                if (strcmp(fn_name, "band") == 0) { fputs("uf_band(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "bor") == 0) { fputs("uf_bor(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "bxor") == 0) { fputs("uf_bxor(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "bnot") == 0) { fputs("uf_bnot(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "shl") == 0) { fputs("uf_shl(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "shr") == 0) { fputs("uf_shr(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "sar") == 0) { fputs("uf_sar(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out); return; }
                if (strcmp(fn_name, "to_hex") == 0) { fputs("uf_to_hex(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "from_hex") == 0) { fputs("uf_from_hex(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_to_hex") == 0) { fputs("uf_buffer_to_hex(", out); UF_ARG(0); fputc(')', out); return; }
                if (strcmp(fn_name, "buffer_from_hex") == 0) { fputs("uf_buffer_from_hex(", out); UF_ARG(0); fputc(')', out); return; }

                #undef UF_ARG

                /* If top-level declared function, call statically */
                if (is_declared_function(fn_name)) {
                    fprintf(out, "uf_fn_%s(", fn_name);
                    for (size_t i = 0; i < expr->as.call.argc; ++i) {
                        if (i > 0) fputs(", ", out);
                        emit_expr(out, expr->as.call.args[i]);
                    }
                    fputc(')', out);
                    return;
                }
            }

            /* First-class callable value or arbitrary callee expression */
            fputs("uf_call_val(", out);
            emit_expr(out, expr->as.call.callee);
            fprintf(out, ", %zu", expr->as.call.argc);
            for (size_t i = 0; i < expr->as.call.argc; ++i) {
                fputs(", ", out);
                emit_expr(out, expr->as.call.args[i]);
            }
            fputc(')', out);
            break;
        }
        case UF_EXPR_FUNCTION: {
            int id = find_lambda_id(expr);
            if (id >= 0) {
                fprintf(out, "uf_make_lambda_%d(", id);
                for (size_t c = 0; c < g_lambdas[id].capture_count; ++c) {
                    if (c > 0) fputs(", ", out);
                    fprintf(out, "uf_var_%s", g_lambdas[id].captures[c]);
                }
                fputc(')', out);
            } else {
                fputs("uf_null()", out);
            }
            break;
        }
        case UF_EXPR_GROUPING:
            fputc('(', out);
            emit_expr(out, expr->as.grouping.inner);
            fputc(')', out);
            break;
        case UF_EXPR_ARRAY:
            fprintf(out, "uf_make_array(%zu", expr->as.array_lit.count);
            for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                fputs(", ", out);
                emit_expr(out, expr->as.array_lit.elements[i]);
            }
            fputc(')', out);
            break;
        case UF_EXPR_MAP:
            fprintf(out, "uf_make_map(%zu", expr->as.map_lit.count);
            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                fputs(", ", out);
                emit_expr(out, expr->as.map_lit.keys[i]);
                fputs(", ", out);
                emit_expr(out, expr->as.map_lit.values[i]);
            }
            fputc(')', out);
            break;
        case UF_EXPR_INDEX:
            fputs("uf_get(", out);
            emit_expr(out, expr->as.index_expr.target);
            fputs(", ", out);
            emit_expr(out, expr->as.index_expr.index);
            fputc(')', out);
            break;
        default:
            fputs("uf_null()", out);
            break;
    }
}

static void emit_stmt(FILE* out, const UfStmt* stmt, int indent, bool is_toplevel) {
    if (!stmt) return;

    switch (stmt->kind) {
        case UF_STMT_LET:
            emit_indent(out, indent);
            if (is_toplevel) {
                fprintf(out, "uf_var_%s = ", stmt->as.let_stmt.name);
            } else {
                fprintf(out, "UfVal uf_var_%s = ", stmt->as.let_stmt.name);
            }
            if (stmt->as.let_stmt.init) {
                emit_expr(out, stmt->as.let_stmt.init);
            } else {
                fputs("uf_null()", out);
            }
            fputs(";\n", out);
            break;
        case UF_STMT_ASSIGN:
            emit_indent(out, indent);
            fprintf(out, "uf_var_%s = ", stmt->as.assign_stmt.name);
            emit_expr(out, stmt->as.assign_stmt.value);
            fputs(";\n", out);
            break;
        case UF_STMT_INDEX_ASSIGN:
            emit_indent(out, indent);
            fputs("uf_set(", out);
            emit_expr(out, stmt->as.index_assign.target);
            fputs(", ", out);
            emit_expr(out, stmt->as.index_assign.index);
            fputs(", ", out);
            emit_expr(out, stmt->as.index_assign.value);
            fputs(");\n", out);
            break;
        case UF_STMT_SAY:
            emit_indent(out, indent);
            fputs("uf_say(", out);
            emit_expr(out, stmt->as.say_stmt.expr);
            fputs(");\n", out);
            break;
        case UF_STMT_EXPR:
            emit_indent(out, indent);
            emit_expr(out, stmt->as.expr_stmt.expr);
            fputs(";\n", out);
            break;
        case UF_STMT_IF:
            emit_indent(out, indent);
            fputs("if (uf_truthy(", out);
            emit_expr(out, stmt->as.if_stmt.condition);
            fputs(")) {\n", out);
            emit_stmt(out, stmt->as.if_stmt.then_branch, indent + 1, false);
            if (stmt->as.if_stmt.else_branch) {
                emit_indent(out, indent);
                fputs("} else {\n", out);
                emit_stmt(out, stmt->as.if_stmt.else_branch, indent + 1, false);
            }
            emit_indent(out, indent);
            fputs("}\n", out);
            break;
        case UF_STMT_WHILE:
            emit_indent(out, indent);
            fputs("while (uf_truthy(", out);
            emit_expr(out, stmt->as.while_stmt.condition);
            fputs(")) {\n", out);
            emit_stmt(out, stmt->as.while_stmt.body, indent + 1, false);
            emit_indent(out, indent);
            fputs("}\n", out);
            break;
        case UF_STMT_REPEAT:
            emit_indent(out, indent);
            fputs("{\n", out);
            emit_indent(out, indent + 1);
            fputs("UfVal _rep_cnt = ", out);
            emit_expr(out, stmt->as.repeat_stmt.count_expr);
            fputs(";\n", out);
            emit_indent(out, indent + 1);
            fputs("long _rep_limit = (_rep_cnt.kind == UF_RT_NUMBER) ? (long)_rep_cnt.as.number : 0;\n", out);
            emit_indent(out, indent + 1);
            fputs("for (long _rep = 0; _rep < _rep_limit; ++_rep) {\n", out);
            emit_stmt(out, stmt->as.repeat_stmt.body, indent + 2, false);
            emit_indent(out, indent + 1);
            fputs("}\n", out);
            emit_indent(out, indent);
            fputs("}\n", out);
            break;
        case UF_STMT_FOR:
            emit_indent(out, indent);
            fputs("{\n", out);
            emit_indent(out, indent + 1);
            fputs("UfVal _iter = ", out);
            emit_expr(out, stmt->as.for_stmt.iterable);
            fputs(";\n", out);
            emit_indent(out, indent + 1);
            fputs("long _len = (long)uf_len(_iter).as.number;\n", out);
            emit_indent(out, indent + 1);
            fputs("for (long _i = 0; _i < _len; ++_i) {\n", out);
            emit_indent(out, indent + 2);
            fprintf(out, "UfVal uf_var_%s = uf_get(_iter, uf_num((double)_i));\n", stmt->as.for_stmt.var_name);
            emit_stmt(out, stmt->as.for_stmt.body, indent + 2, false);
            emit_indent(out, indent + 1);
            fputs("}\n", out);
            emit_indent(out, indent);
            fputs("}\n", out);
            break;
        case UF_STMT_BREAK:
            emit_indent(out, indent);
            fputs("break;\n", out);
            break;
        case UF_STMT_CONTINUE:
            emit_indent(out, indent);
            fputs("continue;\n", out);
            break;
        case UF_STMT_RETURN:
            emit_indent(out, indent);
            fputs("return ", out);
            if (stmt->as.return_stmt.value) emit_expr(out, stmt->as.return_stmt.value);
            else fputs("uf_null()", out);
            fputs(";\n", out);
            break;
        case UF_STMT_BLOCK:
            for (size_t i = 0; i < stmt->as.block.count; ++i) {
                emit_stmt(out, stmt->as.block.stmts[i], indent, false);
            }
            break;
        default:
            break;
    }
}

bool uf_emit_c_program(const UfProgram* program, FILE* out) {
    if (!program || !out) return false;

    /* Populate top-level functions */
    g_declared_fn_count = 0;
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_FUNCTION && g_declared_fn_count < 256) {
            g_declared_fns[g_declared_fn_count++] = stmt->as.function_stmt.name;
        }
    }

    /* Populate lambdas */
    g_lambda_count = 0;
    for (size_t i = 0; i < program->count; ++i) {
        collect_lambdas_stmt(program->stmts[i]);
    }
    for (size_t i = 0; i < g_lambda_count; ++i) {
        const char* locals[64];
        size_t local_count = 0;
        find_captures_stmt(g_lambdas[i].fn_expr->as.fn_expr.body, locals, &local_count,
                           g_lambdas[i].fn_expr->as.fn_expr.params,
                           g_lambdas[i].fn_expr->as.fn_expr.param_count,
                           &g_lambdas[i]);
    }

    fputs("/* ========================================================================= */\n", out);
    fputs("/* Generated automatically by Unfish Native C99 Compiler                   */\n", out);
    fputs("/* ========================================================================= */\n\n", out);
    fputs("#include \"unfish_runtime.h\"\n\n", out);

    /* 1. Declare top-level variables at file scope */
    fputs("/* Top-level global variables */\n", out);
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_LET) {
            fprintf(out, "static UfVal uf_var_%s;\n", stmt->as.let_stmt.name);
        }
    }
    fputs("\n", out);

    /* 2. Lambda environment definitions & forward declarations */
    if (g_lambda_count > 0) {
        fputs("/* Lambda environment definitions */\n", out);
        for (size_t i = 0; i < g_lambda_count; ++i) {
            fprintf(out, "typedef struct {\n");
            if (g_lambdas[i].capture_count > 0) {
                for (size_t c = 0; c < g_lambdas[i].capture_count; ++c) {
                    fprintf(out, "    UfVal uf_var_%s;\n", g_lambdas[i].captures[c]);
                }
            } else {
                fputs("    int _dummy;\n", out);
            }
            fprintf(out, "} uf_env_lambda_%zu;\n\n", i);

            fprintf(out, "static UfVal uf_lambda_%zu(void* _raw_env, size_t _argc, UfVal* _args);\n\n", i);

            fprintf(out, "static inline UfVal uf_make_lambda_%zu(", i);
            if (g_lambdas[i].capture_count > 0) {
                for (size_t c = 0; c < g_lambdas[i].capture_count; ++c) {
                    if (c > 0) fputs(", ", out);
                    fprintf(out, "UfVal uf_var_%s", g_lambdas[i].captures[c]);
                }
            } else {
                fputs("void", out);
            }
            fprintf(out, ") {\n");
            fprintf(out, "    uf_env_lambda_%zu _env;\n", i);
            if (g_lambdas[i].capture_count > 0) {
                for (size_t c = 0; c < g_lambdas[i].capture_count; ++c) {
                    fprintf(out, "    _env.uf_var_%s = uf_var_%s;\n", g_lambdas[i].captures[c], g_lambdas[i].captures[c]);
                }
                fprintf(out, "    return uf_closure_new(uf_lambda_%zu, &_env, sizeof(_env));\n", i);
            } else {
                fprintf(out, "    (void)_env;\n");
                fprintf(out, "    return uf_closure_new(uf_lambda_%zu, NULL, 0);\n", i);
            }
            fprintf(out, "}\n\n");
        }
    }

    /* 3. Forward declare top-level functions and first-class wrappers */
    fputs("/* Function declarations and first-class wrappers */\n", out);
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_FUNCTION) {
            const char* fname = stmt->as.function_stmt.name;
            size_t pcount = stmt->as.function_stmt.param_count;
            fprintf(out, "static UfVal uf_fn_%s(", fname);
            for (size_t p = 0; p < pcount; ++p) {
                if (p > 0) fputs(", ", out);
                fprintf(out, "UfVal uf_var_%s", stmt->as.function_stmt.params[p]);
            }
            if (pcount == 0) fputs("void", out);
            fputs(");\n", out);

            fprintf(out, "static UfVal uf_wrapper_%s(void* env, size_t argc, UfVal* args) {\n", fname);
            fprintf(out, "    (void)env;\n");
            fprintf(out, "    return uf_fn_%s(", fname);
            for (size_t p = 0; p < pcount; ++p) {
                if (p > 0) fputs(", ", out);
                fprintf(out, "(argc > %zu ? args[%zu] : uf_null())", p, p);
            }
            fputs(");\n}\n", out);

            fprintf(out, "static inline UfVal uf_wrap_fn_%s(void) {\n", fname);
            fprintf(out, "    return uf_closure_new(uf_wrapper_%s, NULL, 0);\n", fname);
            fprintf(out, "}\n\n");
        }
    }

    /* 4. Emit lambda implementations */
    if (g_lambda_count > 0) {
        fputs("/* Lambda implementations */\n", out);
        for (size_t i = 0; i < g_lambda_count; ++i) {
            fprintf(out, "static UfVal uf_lambda_%zu(void* _raw_env, size_t _argc, UfVal* _args) {\n", i);
            fprintf(out, "    uf_env_lambda_%zu* _env = (uf_env_lambda_%zu*)_raw_env;\n", i, i);
            fprintf(out, "    (void)_env; (void)_argc; (void)_args;\n");
            for (size_t c = 0; c < g_lambdas[i].capture_count; ++c) {
                fprintf(out, "    UfVal uf_var_%s = _env->uf_var_%s;\n", g_lambdas[i].captures[c], g_lambdas[i].captures[c]);
            }
            size_t pcount = g_lambdas[i].fn_expr->as.fn_expr.param_count;
            for (size_t p = 0; p < pcount; ++p) {
                fprintf(out, "    UfVal uf_var_%s = (_argc > %zu) ? _args[%zu] : uf_null();\n",
                        g_lambdas[i].fn_expr->as.fn_expr.params[p], p, p);
            }
            emit_stmt(out, g_lambdas[i].fn_expr->as.fn_expr.body, 1, false);
            fprintf(out, "    return uf_null();\n");
            fprintf(out, "}\n\n");
        }
    }

    /* 5. Emit top-level function definitions */
    fputs("/* Function definitions */\n", out);
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_FUNCTION) {
            fprintf(out, "static UfVal uf_fn_%s(", stmt->as.function_stmt.name);
            for (size_t p = 0; p < stmt->as.function_stmt.param_count; ++p) {
                if (p > 0) fputs(", ", out);
                fprintf(out, "UfVal uf_var_%s", stmt->as.function_stmt.params[p]);
            }
            if (stmt->as.function_stmt.param_count == 0) fputs("void", out);
            fputs(") {\n", out);
            emit_stmt(out, stmt->as.function_stmt.body, 1, false);
            fputs("    return uf_null();\n", out);
            fputs("}\n\n", out);
        }
    }

    /* 6. Emit main() */
    fputs("int main(int argc, char** argv) {\n", out);
    fputs("    uf_init(argc, argv);\n\n", out);

    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind != UF_STMT_FUNCTION) {
            emit_stmt(out, stmt, 1, true);
        }
    }

    fputs("\n    uf_cleanup();\n", out);
    fputs("    return 0;\n", out);
    fputs("}\n", out);

    return true;
}

bool uf_emit_c_to_file(const UfProgram* program, const char* out_c_path) {
    if (!program || !out_c_path) return false;
    FILE* file = fopen(out_c_path, "w");
    if (!file) {
        fprintf(stderr, "Error: Could not open output C file '%s'\n", out_c_path);
        return false;
    }
    bool ok = uf_emit_c_program(program, file);
    fclose(file);
    return ok;
}

bool uf_build_native(const UfProgram* program, const char* out_bin_path) {
    if (!program || !out_bin_path) return false;

    char temp_c[256];
    snprintf(temp_c, sizeof(temp_c), "/tmp/unfish_emit_%d.c", (int)getpid());

    if (!uf_emit_c_to_file(program, temp_c)) {
        return false;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "gcc -O2 -std=c99 %s -Isrc/codegen -lm -o %s", temp_c, out_bin_path);
    int res = system(cmd);
    remove(temp_c);

    return res == 0;
}
