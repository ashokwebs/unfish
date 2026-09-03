#define _POSIX_C_SOURCE 200809L
#include "uf_blocks.h"
#include "../lexer/uf_token.h"
#include <stdlib.h>
#include <string.h>

static void json_print_escaped(FILE* out, const char* str) {
    if (!str) {
        fputs("null", out);
        return;
    }
    fputc('\"', out);
    for (const char* p = str; *p; ++p) {
        switch (*p) {
            case '\"': fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if ((unsigned char)*p < 32) {
                    fprintf(out, "\\u%04x", (unsigned char)*p);
                } else {
                    fputc(*p, out);
                }
                break;
        }
    }
    fputc('\"', out);
}

static const char* token_type_name(UfTokenKind type) {
    switch (type) {
        case UF_TOK_PLUS: return "+";
        case UF_TOK_MINUS: return "-";
        case UF_TOK_STAR: return "*";
        case UF_TOK_SLASH: return "/";
        case UF_TOK_PERCENT: return "%";
        case UF_TOK_EQEQ: return "==";
        case UF_TOK_BANGEQ: return "!=";
        case UF_TOK_LT: return "<";
        case UF_TOK_LTEQ: return "<=";
        case UF_TOK_GT: return ">";
        case UF_TOK_GTEQ: return ">=";
        case UF_TOK_AND: return "and";
        case UF_TOK_OR: return "or";
        case UF_TOK_NOT: return "not";
        default: return "?";
    }
}

static void export_expr(const UfExpr* expr, FILE* out, int indent);
static void export_stmt(const UfStmt* stmt, FILE* out, int indent);
static void export_pattern(const UfPattern* pat, FILE* out, int indent);

static void print_indent(FILE* out, int indent) {
    for (int i = 0; i < indent; ++i) fputs("  ", out);
}

static void export_expr(const UfExpr* expr, FILE* out, int indent) {
    if (!expr) {
        fputs("null", out);
        return;
    }

    switch (expr->kind) {
        case UF_EXPR_LITERAL_NULL:
            fputs("{\"kind\": \"literal_null\"}", out);
            break;
        case UF_EXPR_LITERAL_BOOL:
            fprintf(out, "{\"kind\": \"literal_bool\", \"value\": %s}", expr->as.bool_val ? "true" : "false");
            break;
        case UF_EXPR_LITERAL_NUMBER:
            fprintf(out, "{\"kind\": \"literal_number\", \"value\": %.14g}", expr->as.number_val);
            break;
        case UF_EXPR_LITERAL_STRING:
            fputs("{\"kind\": \"literal_string\", \"value\": ", out);
            json_print_escaped(out, expr->as.string_val);
            fputc('}', out);
            break;
        case UF_EXPR_IDENTIFIER:
            fputs("{\"kind\": \"identifier\", \"name\": ", out);
            json_print_escaped(out, expr->as.identifier_name);
            fputc('}', out);
            break;
        case UF_EXPR_UNARY:
            fprintf(out, "{\"kind\": \"unary\", \"op\": \"%s\", \"operand\": ", token_type_name(expr->as.unary.op));
            export_expr(expr->as.unary.operand, out, indent);
            fputc('}', out);
            break;
        case UF_EXPR_BINARY:
            fprintf(out, "{\"kind\": \"binary\", \"op\": \"%s\", \"left\": ", token_type_name(expr->as.binary.op));
            export_expr(expr->as.binary.left, out, indent);
            fputs(", \"right\": ", out);
            export_expr(expr->as.binary.right, out, indent);
            fputc('}', out);
            break;
        case UF_EXPR_GROUPING:
            fputs("{\"kind\": \"grouping\", \"inner\": ", out);
            export_expr(expr->as.grouping.inner, out, indent);
            fputc('}', out);
            break;
        case UF_EXPR_CALL:
            fputs("{\"kind\": \"call\", \"callee\": ", out);
            export_expr(expr->as.call.callee, out, indent);
            fputs(", \"args\": [", out);
            for (size_t i = 0; i < expr->as.call.argc; ++i) {
                if (i > 0) fputs(", ", out);
                export_expr(expr->as.call.args[i], out, indent);
            }
            fputs("]}", out);
            break;
        case UF_EXPR_ARRAY:
            fputs("{\"kind\": \"array\", \"elements\": [", out);
            for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                if (i > 0) fputs(", ", out);
                export_expr(expr->as.array_lit.elements[i], out, indent);
            }
            fputs("]}", out);
            break;
        case UF_EXPR_INDEX:
            fputs("{\"kind\": \"index\", \"target\": ", out);
            export_expr(expr->as.index_expr.target, out, indent);
            fputs(", \"index\": ", out);
            export_expr(expr->as.index_expr.index, out, indent);
            fputc('}', out);
            break;
        case UF_EXPR_MAP:
            fputs("{\"kind\": \"map\", \"keys\": [", out);
            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                if (i > 0) fputs(", ", out);
                export_expr(expr->as.map_lit.keys[i], out, indent);
            }
            fputs("], \"values\": [", out);
            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                if (i > 0) fputs(", ", out);
                export_expr(expr->as.map_lit.values[i], out, indent);
            }
            fputs("]}", out);
            break;
        case UF_EXPR_FUNCTION:
            fputs("{\"kind\": \"function\", \"name\": ", out);
            json_print_escaped(out, expr->as.fn_expr.name);
            fputs(", \"params\": [", out);
            for (size_t i = 0; i < expr->as.fn_expr.param_count; ++i) {
                if (i > 0) fputs(", ", out);
                json_print_escaped(out, expr->as.fn_expr.params[i]);
            }
            fputs("], \"param_types\": [", out);
            for (size_t i = 0; i < expr->as.fn_expr.param_count; ++i) {
                if (i > 0) fputs(", ", out);
                json_print_escaped(out, expr->as.fn_expr.param_types ? expr->as.fn_expr.param_types[i] : NULL);
            }
            fputs("], \"return_type\": ", out);
            json_print_escaped(out, expr->as.fn_expr.return_type);
            fputs(", \"body\": ", out);
            export_stmt(expr->as.fn_expr.body, out, indent);
            fputc('}', out);
            break;
    }
}

static void export_pattern(const UfPattern* pat, FILE* out, int indent) {
    if (!pat) {
        fputs("null", out);
        return;
    }

    switch (pat->kind) {
        case UF_PAT_LITERAL:
            fputs("{\"kind\": \"pattern_literal\", \"expr\": ", out);
            export_expr(pat->as.literal, out, indent);
            fputc('}', out);
            break;
        case UF_PAT_VARIABLE:
            fputs("{\"kind\": \"pattern_variable\", \"name\": ", out);
            json_print_escaped(out, pat->as.var_name);
            fputc('}', out);
            break;
        case UF_PAT_WILDCARD:
            fputs("{\"kind\": \"pattern_wildcard\"}", out);
            break;
        case UF_PAT_STRUCT:
            fputs("{\"kind\": \"pattern_struct\", \"struct_name\": ", out);
            json_print_escaped(out, pat->as.struct_pat.struct_name);
            fputs(", \"field_patterns\": [", out);
            for (size_t i = 0; i < pat->as.struct_pat.field_count; ++i) {
                if (i > 0) fputs(", ", out);
                export_pattern(pat->as.struct_pat.field_patterns[i], out, indent);
            }
            fputs("]}", out);
            break;
    }
}

static void export_stmt(const UfStmt* stmt, FILE* out, int indent) {
    if (!stmt) {
        fputs("null", out);
        return;
    }

    switch (stmt->kind) {
        case UF_STMT_LET:
            fputs("{\"kind\": \"let\", \"name\": ", out);
            json_print_escaped(out, stmt->as.let_stmt.name);
            fputs(", \"type_ann\": ", out);
            json_print_escaped(out, stmt->as.let_stmt.type_annotation);
            fputs(", \"init\": ", out);
            export_expr(stmt->as.let_stmt.init, out, indent);
            fputc('}', out);
            break;

        case UF_STMT_ASSIGN:
            fputs("{\"kind\": \"assign\", \"name\": ", out);
            json_print_escaped(out, stmt->as.assign_stmt.name);
            fputs(", \"value\": ", out);
            export_expr(stmt->as.assign_stmt.value, out, indent);
            fputc('}', out);
            break;

        case UF_STMT_INDEX_ASSIGN:
            fputs("{\"kind\": \"index_assign\", \"target\": ", out);
            export_expr(stmt->as.index_assign.target, out, indent);
            fputs(", \"index\": ", out);
            export_expr(stmt->as.index_assign.index, out, indent);
            fputs(", \"value\": ", out);
            export_expr(stmt->as.index_assign.value, out, indent);
            fputc('}', out);
            break;

        case UF_STMT_SAY:
            fputs("{\"kind\": \"say\", \"value\": ", out);
            export_expr(stmt->as.say_stmt.expr, out, indent);
            fputc('}', out);
            break;

        case UF_STMT_EXPR:
            fputs("{\"kind\": \"expr\", \"expression\": ", out);
            export_expr(stmt->as.expr_stmt.expr, out, indent);
            fputc('}', out);
            break;

        case UF_STMT_IF:
            fputs("{\"kind\": \"if\", \"condition\": ", out);
            export_expr(stmt->as.if_stmt.condition, out, indent);
            fputs(", \"then_branch\": ", out);
            export_stmt(stmt->as.if_stmt.then_branch, out, indent);
            fputs(", \"else_branch\": ", out);
            export_stmt(stmt->as.if_stmt.else_branch, out, indent);
            fputc('}', out);
            break;

        case UF_STMT_WHILE:
            fputs("{\"kind\": \"while\", \"condition\": ", out);
            export_expr(stmt->as.while_stmt.condition, out, indent);
            fputs(", \"body\": ", out);
            export_stmt(stmt->as.while_stmt.body, out, indent);
            fputc('}', out);
            break;

        case UF_STMT_REPEAT:
            fputs("{\"kind\": \"repeat\", \"count\": ", out);
            export_expr(stmt->as.repeat_stmt.count_expr, out, indent);
            fputs(", \"body\": ", out);
            export_stmt(stmt->as.repeat_stmt.body, out, indent);
            fputc('}', out);
            break;

        case UF_STMT_FOR:
            fputs("{\"kind\": \"for_in\", \"var_name\": ", out);
            json_print_escaped(out, stmt->as.for_stmt.var_name);
            fputs(", \"iterable\": ", out);
            export_expr(stmt->as.for_stmt.iterable, out, indent);
            fputs(", \"body\": ", out);
            export_stmt(stmt->as.for_stmt.body, out, indent);
            fputc('}', out);
            break;

        case UF_STMT_BREAK:
            fputs("{\"kind\": \"break\"}", out);
            break;

        case UF_STMT_CONTINUE:
            fputs("{\"kind\": \"continue\"}", out);
            break;

        case UF_STMT_FUNCTION:
            fputs("{\"kind\": \"function\", \"name\": ", out);
            json_print_escaped(out, stmt->as.function_stmt.name);
            fputs(", \"params\": [", out);
            for (size_t i = 0; i < stmt->as.function_stmt.param_count; ++i) {
                if (i > 0) fputs(", ", out);
                json_print_escaped(out, stmt->as.function_stmt.params[i]);
            }
            fputs("], \"param_types\": [", out);
            for (size_t i = 0; i < stmt->as.function_stmt.param_count; ++i) {
                if (i > 0) fputs(", ", out);
                json_print_escaped(out, stmt->as.function_stmt.param_types ? stmt->as.function_stmt.param_types[i] : NULL);
            }
            fputs("], \"return_type\": ", out);
            json_print_escaped(out, stmt->as.function_stmt.return_type);
            fputs(", \"body\": ", out);
            export_stmt(stmt->as.function_stmt.body, out, indent);
            fputc('}', out);
            break;

        case UF_STMT_RETURN:
            fputs("{\"kind\": \"return\", \"value\": ", out);
            export_expr(stmt->as.return_stmt.value, out, indent);
            fputc('}', out);
            break;

        case UF_STMT_BLOCK:
            fputs("{\"kind\": \"block\", \"statements\": [\n", out);
            for (size_t i = 0; i < stmt->as.block.count; ++i) {
                print_indent(out, indent + 1);
                export_stmt(stmt->as.block.stmts[i], out, indent + 1);
                if (i + 1 < stmt->as.block.count) fputc(',', out);
                fputc('\n', out);
            }
            print_indent(out, indent);
            fputs("]}", out);
            break;

        case UF_STMT_TRY_CATCH:
            fputs("{\"kind\": \"try_catch\", \"try_block\": ", out);
            export_stmt(stmt->as.try_catch.try_block, out, indent);
            fputs(", \"error_var\": ", out);
            json_print_escaped(out, stmt->as.try_catch.catch_var);
            fputs(", \"catch_block\": ", out);
            export_stmt(stmt->as.try_catch.catch_block, out, indent);
            fputc('}', out);
            break;

        case UF_STMT_IMPORT:
            fputs("{\"kind\": \"import\", \"module_name\": ", out);
            json_print_escaped(out, stmt->as.import_stmt.module_name);
            fputs(", \"alias\": ", out);
            json_print_escaped(out, stmt->as.import_stmt.alias);
            fputc('}', out);
            break;

        case UF_STMT_FROM_IMPORT:
            fputs("{\"kind\": \"from_import\", \"module_name\": ", out);
            json_print_escaped(out, stmt->as.from_import_stmt.module_name);
            fputs(", \"symbols\": [", out);
            for (size_t i = 0; i < stmt->as.from_import_stmt.count; ++i) {
                if (i > 0) fputs(", ", out);
                json_print_escaped(out, stmt->as.from_import_stmt.symbols[i]);
            }
            fputs("], \"aliases\": [", out);
            for (size_t i = 0; i < stmt->as.from_import_stmt.count; ++i) {
                if (i > 0) fputs(", ", out);
                json_print_escaped(out, stmt->as.from_import_stmt.aliases ? stmt->as.from_import_stmt.aliases[i] : NULL);
            }
            fputs("]}", out);
            break;

        case UF_STMT_STRUCT:
            fputs("{\"kind\": \"struct\", \"name\": ", out);
            json_print_escaped(out, stmt->as.struct_stmt.name);
            fputs(", \"fields\": [", out);
            for (size_t i = 0; i < stmt->as.struct_stmt.field_count; ++i) {
                if (i > 0) fputs(", ", out);
                json_print_escaped(out, stmt->as.struct_stmt.field_names[i]);
            }
            fputs("], \"field_types\": [", out);
            for (size_t i = 0; i < stmt->as.struct_stmt.field_count; ++i) {
                if (i > 0) fputs(", ", out);
                json_print_escaped(out, stmt->as.struct_stmt.field_types ? stmt->as.struct_stmt.field_types[i] : NULL);
            }
            fputs("]}", out);
            break;

        case UF_STMT_MATCH:
            fputs("{\"kind\": \"match\", \"expr\": ", out);
            export_expr(stmt->as.match_stmt.expr, out, indent);
            fputs(", \"arms\": [\n", out);
            for (size_t i = 0; i < stmt->as.match_stmt.arm_count; ++i) {
                print_indent(out, indent + 1);
                fputs("{\"pattern\": ", out);
                export_pattern(stmt->as.match_stmt.arms[i].pattern, out, indent + 1);
                fputs(", \"guard\": ", out);
                export_expr(stmt->as.match_stmt.arms[i].guard, out, indent + 1);
                fputs(", \"body\": ", out);
                export_stmt(stmt->as.match_stmt.arms[i].body, out, indent + 1);
                fputc('}', out);
                if (i + 1 < stmt->as.match_stmt.arm_count) fputc(',', out);
                fputc('\n', out);
            }
            print_indent(out, indent);
            fputs("], \"else_branch\": ", out);
            export_stmt(stmt->as.match_stmt.else_branch, out, indent);
            fputc('}', out);
            break;
    }
}

void uf_blocks_export_stream(const UfProgram* program, FILE* out) {
    if (!out) out = stdout;
    if (!program) {
        fputs("{\"schema\": \"unfish_blocks_v1\", \"statements\": []}\n", out);
        return;
    }

    fprintf(out, "{\n  \"schema\": \"unfish_blocks_v1\",\n  \"statements\": [\n");
    for (size_t i = 0; i < program->count; ++i) {
        print_indent(out, 2);
        export_stmt(program->stmts[i], out, 2);
        if (i + 1 < program->count) fputc(',', out);
        fputc('\n', out);
    }
    fprintf(out, "  ]\n}\n");
}

char* uf_blocks_export_string(const UfProgram* program) {
    char* buf = NULL;
    size_t sz = 0;
    FILE* mem = open_memstream(&buf, &sz);
    if (!mem) return NULL;
    uf_blocks_export_stream(program, mem);
    fclose(mem);
    return buf;
}
