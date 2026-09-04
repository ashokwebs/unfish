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

static void emit_expr(FILE* out, const UfExpr* expr);

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
            fprintf(out, "uf_var_%s", expr->as.identifier_name);
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
                if (strcmp(fn_name, "say") == 0) {
                    fputs("(uf_say(", out);
                    if (expr->as.call.argc > 0) emit_expr(out, expr->as.call.args[0]);
                    else fputs("uf_null()", out);
                    fputs("), uf_null())", out);
                    return;
                }
                if (strcmp(fn_name, "print") == 0) {
                    fputs("(uf_print(", out);
                    if (expr->as.call.argc > 0) emit_expr(out, expr->as.call.args[0]);
                    else fputs("uf_null()", out);
                    fputs("), uf_null())", out);
                    return;
                }
                if (strcmp(fn_name, "len") == 0) {
                    fputs("uf_len(", out);
                    if (expr->as.call.argc > 0) emit_expr(out, expr->as.call.args[0]);
                    else fputs("uf_null()", out);
                    fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "type_of") == 0) {
                    fputs("uf_type_of(", out);
                    if (expr->as.call.argc > 0) emit_expr(out, expr->as.call.args[0]);
                    else fputs("uf_null()", out);
                    fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "push") == 0) {
                    fputs("(uf_array_push(", out);
                    emit_expr(out, expr->as.call.args[0]);
                    fputs(", ", out);
                    emit_expr(out, expr->as.call.args[1]);
                    fputs("), uf_null())", out);
                    return;
                }
                fprintf(out, "uf_fn_%s(", fn_name);
                for (size_t i = 0; i < expr->as.call.argc; ++i) {
                    if (i > 0) fputs(", ", out);
                    emit_expr(out, expr->as.call.args[i]);
                }
                fputc(')', out);
                return;
            }
            fputs("uf_null()", out);
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

    /* 2. Forward declare top-level functions */
    fputs("/* Function declarations */\n", out);
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_FUNCTION) {
            fprintf(out, "static UfVal uf_fn_%s(", stmt->as.function_stmt.name);
            for (size_t p = 0; p < stmt->as.function_stmt.param_count; ++p) {
                if (p > 0) fputs(", ", out);
                fprintf(out, "UfVal uf_var_%s", stmt->as.function_stmt.params[p]);
            }
            if (stmt->as.function_stmt.param_count == 0) fputs("void", out);
            fputs(");\n", out);
        }
    }
    fputs("\n", out);

    /* 3. Emit function definitions */
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

    /* 4. Emit main() */
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
