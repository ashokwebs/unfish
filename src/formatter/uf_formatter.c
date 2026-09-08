#include "uf_formatter.h"
#include "uf_token.h"
#include "uf_arena.h"
#include "uf_lexer.h"
#include "uf_parser.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    FMT_PREC_NONE = 0,
    FMT_PREC_OR,          /* or */
    FMT_PREC_AND,         /* and */
    FMT_PREC_EQUALITY,    /* ==, != */
    FMT_PREC_COMPARISON,  /* <, <=, >, >= */
    FMT_PREC_TERM,        /* +, - */
    FMT_PREC_FACTOR,      /* *, /, % */
    FMT_PREC_UNARY,       /* -, not */
    FMT_PREC_CALL,        /* call, index, dot */
    FMT_PREC_PRIMARY
} FmtPrec;

static void emit_indent(FILE* out, int level) {
    for (int i = 0; i < level; ++i) {
        fputs("    ", out);
    }
}

static bool is_valid_ident(const char* s) {
    if (!s || !*s) return false;
    if (!isalpha((unsigned char)*s) && *s != '_') return false;
    for (size_t i = 1; s[i]; ++i) {
        if (!isalnum((unsigned char)s[i]) && s[i] != '_') return false;
    }
    static const char* kw[] = {
        "let", "say", "if", "else", "while", "repeat", "times", "for", "in",
        "break", "continue", "function", "return", "try", "catch", "import",
        "from", "as", "struct", "match", "when", "and", "or", "not",
        "true", "false", "null", NULL
    };
    for (int i = 0; kw[i]; ++i) {
        if (strcmp(s, kw[i]) == 0) return false;
    }
    return true;
}

static void emit_string_literal(FILE* out, const char* str) {
    fputc('"', out);
    for (const char* p = str; *p; ++p) {
        switch (*p) {
            case '\n': fputs("\\n", out); break;
            case '\t': fputs("\\t", out); break;
            case '\"': fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            default: fputc(*p, out); break;
        }
    }
    fputc('"', out);
}

static void emit_number(FILE* out, double val) {
    if (val == (double)(int64_t)val && fabs(val) < 9e15) {
        fprintf(out, "%ld", (long)(int64_t)val);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.14g", val);
        fprintf(out, "%s", buf);
    }
}

static const char* token_op_str(UfTokenKind op) {
    switch (op) {
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

static FmtPrec expr_precedence(const UfExpr* expr) {
    if (!expr) return FMT_PREC_NONE;
    switch (expr->kind) {
        case UF_EXPR_BINARY: {
            switch (expr->as.binary.op) {
                case UF_TOK_OR: return FMT_PREC_OR;
                case UF_TOK_AND: return FMT_PREC_AND;
                case UF_TOK_EQEQ:
                case UF_TOK_BANGEQ: return FMT_PREC_EQUALITY;
                case UF_TOK_LT:
                case UF_TOK_LTEQ:
                case UF_TOK_GT:
                case UF_TOK_GTEQ: return FMT_PREC_COMPARISON;
                case UF_TOK_PLUS:
                case UF_TOK_MINUS: return FMT_PREC_TERM;
                case UF_TOK_STAR:
                case UF_TOK_SLASH:
                case UF_TOK_PERCENT: return FMT_PREC_FACTOR;
                default: return FMT_PREC_NONE;
            }
        }
        case UF_EXPR_UNARY:
        case UF_EXPR_SPREAD:
        case UF_EXPR_AWAIT: return FMT_PREC_UNARY;
        case UF_EXPR_CALL:
        case UF_EXPR_INDEX: return FMT_PREC_CALL;
        default: return FMT_PREC_PRIMARY;
    }
}

static void emit_expr(FILE* out, const UfExpr* expr, int indent);
static void emit_statement(FILE* out, const UfStmt* stmt, int indent);
static void emit_block(FILE* out, const UfStmt* stmt, int indent);
static void emit_pattern(FILE* out, const UfPattern* pat, int indent);

static void emit_expr(FILE* out, const UfExpr* expr, int indent) {
    if (!expr) return;

    switch (expr->kind) {
        case UF_EXPR_LITERAL_NULL:
            fputs("null", out);
            break;
        case UF_EXPR_LITERAL_BOOL:
            fputs(expr->as.bool_val ? "true" : "false", out);
            break;
        case UF_EXPR_LITERAL_NUMBER:
            emit_number(out, expr->as.number_val);
            break;
        case UF_EXPR_LITERAL_STRING:
            emit_string_literal(out, expr->as.string_val);
            break;
        case UF_EXPR_IDENTIFIER:
            fputs(expr->as.identifier_name, out);
            break;
        case UF_EXPR_UNARY: {
            const char* op = token_op_str(expr->as.unary.op);
            fputs(op, out);
            if (expr->as.unary.op == UF_TOK_NOT) {
                fputc(' ', out);
            }
            FmtPrec child_prec = expr_precedence(expr->as.unary.operand);
            bool paren = (child_prec < FMT_PREC_UNARY);
            if (paren) fputc('(', out);
            emit_expr(out, expr->as.unary.operand, indent);
            if (paren) fputc(')', out);
            break;
        }
        case UF_EXPR_BINARY: {
            FmtPrec my_prec = expr_precedence(expr);
            FmtPrec left_prec = expr_precedence(expr->as.binary.left);
            FmtPrec right_prec = expr_precedence(expr->as.binary.right);

            bool left_paren = (left_prec < my_prec);
            bool right_paren = (right_prec <= my_prec);

            if (left_paren) fputc('(', out);
            emit_expr(out, expr->as.binary.left, indent);
            if (left_paren) fputc(')', out);

            fprintf(out, " %s ", token_op_str(expr->as.binary.op));

            if (right_paren) fputc('(', out);
            emit_expr(out, expr->as.binary.right, indent);
            if (right_paren) fputc(')', out);
            break;
        }
        case UF_EXPR_CALL: {
            emit_expr(out, expr->as.call.callee, indent);
            fputc('(', out);
            for (size_t i = 0; i < expr->as.call.argc; ++i) {
                if (i > 0) fputs(", ", out);
                emit_expr(out, expr->as.call.args[i], indent);
            }
            fputc(')', out);
            break;
        }
        case UF_EXPR_GROUPING:
            fputc('(', out);
            emit_expr(out, expr->as.grouping.inner, indent);
            fputc(')', out);
            break;
        case UF_EXPR_ARRAY: {
            fputc('[', out);
            for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                if (i > 0) fputs(", ", out);
                emit_expr(out, expr->as.array_lit.elements[i], indent);
            }
            fputc(']', out);
            break;
        }
        case UF_EXPR_INDEX: {
            emit_expr(out, expr->as.index_expr.target, indent);
            if (expr->as.index_expr.index->kind == UF_EXPR_LITERAL_STRING &&
                is_valid_ident(expr->as.index_expr.index->as.string_val)) {
                fprintf(out, ".%s", expr->as.index_expr.index->as.string_val);
            } else {
                fputc('[', out);
                emit_expr(out, expr->as.index_expr.index, indent);
                fputc(']', out);
            }
            break;
        }
        case UF_EXPR_MAP: {
            fputc('{', out);
            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                if (i > 0) fputs(", ", out);
                if (expr->as.map_lit.values[i] == NULL) {
                    emit_expr(out, expr->as.map_lit.keys[i], indent);
                } else {
                    emit_expr(out, expr->as.map_lit.keys[i], indent);
                    fputs(": ", out);
                    emit_expr(out, expr->as.map_lit.values[i], indent);
                }
            }
            fputc('}', out);
            break;
        }
        case UF_EXPR_FUNCTION: {
            if (expr->as.fn_expr.is_async) {
                fputs("async ", out);
            }
            fputs("function", out);
            if (expr->as.fn_expr.name) {
                fprintf(out, " %s", expr->as.fn_expr.name);
            }
            fputc('(', out);
            for (size_t i = 0; i < expr->as.fn_expr.param_count; ++i) {
                if (i > 0) fputs(", ", out);
                if (i == expr->as.fn_expr.param_count - 1 && expr->as.fn_expr.has_rest) {
                    fputs("...", out);
                }
                fputs(expr->as.fn_expr.params[i], out);
                if (expr->as.fn_expr.param_types && expr->as.fn_expr.param_types[i]) {
                    fprintf(out, ": %s", expr->as.fn_expr.param_types[i]);
                }
                if (expr->as.fn_expr.param_defaults && expr->as.fn_expr.param_defaults[i]) {
                    fputs(" = ", out);
                    emit_expr(out, expr->as.fn_expr.param_defaults[i], indent);
                }
            }
            fputc(')', out);
            if (expr->as.fn_expr.return_type) {
                fprintf(out, ": %s:", expr->as.fn_expr.return_type);
            } else {
                fputc(':', out);
            }
            if (expr->as.fn_expr.body) {
                fputc('\n', out);
                emit_block(out, expr->as.fn_expr.body, indent + 1);
            }
            break;
        }
        case UF_EXPR_AWAIT: {
            fputs("await ", out);
            FmtPrec child_prec = expr_precedence(expr->as.await_expr.value);
            bool paren = (child_prec < FMT_PREC_UNARY);
            if (paren) fputc('(', out);
            emit_expr(out, expr->as.await_expr.value, indent);
            if (paren) fputc(')', out);
            break;
        }
        case UF_EXPR_SPREAD:
            fputs("...", out);
            emit_expr(out, expr->as.spread.operand, indent);
            break;
        case UF_EXPR_STRING_INTERP: {
            fputs("f\"", out);
            for (size_t i = 0; i < expr->as.string_interp.count; ++i) {
                UfExpr* part = expr->as.string_interp.parts[i];
                if (part->kind == UF_EXPR_LITERAL_STRING) {
                    for (const char* p = part->as.string_val; *p; ++p) {
                        if (*p == '{') fputs("{{", out);
                        else if (*p == '}') fputs("}}", out);
                        else if (*p == '\"') fputs("\\\"", out);
                        else if (*p == '\n') fputs("\\n", out);
                        else if (*p == '\t') fputs("\\t", out);
                        else if (*p == '\\') fputs("\\\\", out);
                        else fputc(*p, out);
                    }
                } else {
                    fputc('{', out);
                    emit_expr(out, part, indent);
                    fputc('}', out);
                }
            }
            fputc('\"', out);
            break;
        }
    }
}

static void emit_pattern(FILE* out, const UfPattern* pat, int indent) {
    if (!pat) {
        fputs("_", out);
        return;
    }
    switch (pat->kind) {
        case UF_PAT_WILDCARD:
            fputs("_", out);
            break;
        case UF_PAT_VARIABLE:
            fputs(pat->as.var_name, out);
            break;
        case UF_PAT_LITERAL:
            emit_expr(out, pat->as.literal, indent);
            break;
        case UF_PAT_STRUCT:
            fprintf(out, "%s(", pat->as.struct_pat.struct_name);
            for (size_t i = 0; i < pat->as.struct_pat.field_count; ++i) {
                if (i > 0) fputs(", ", out);
                emit_pattern(out, pat->as.struct_pat.field_patterns[i], indent);
            }
            fputc(')', out);
            break;
        case UF_PAT_ARRAY:
            fputc('[', out);
            for (size_t i = 0; i < pat->as.array_pat.count; ++i) {
                if (i > 0) fputs(", ", out);
                emit_pattern(out, pat->as.array_pat.elements[i], indent);
            }
            fputc(']', out);
            break;
        case UF_PAT_MAP:
            fputc('{', out);
            for (size_t i = 0; i < pat->as.map_pat.count; ++i) {
                if (i > 0) fputs(", ", out);
                UfPattern* val = pat->as.map_pat.values[i];
                if (val && val->kind == UF_PAT_VARIABLE && strcmp(val->as.var_name, pat->as.map_pat.keys[i]) == 0) {
                    /* Shorthand {x} */
                    fputs(pat->as.map_pat.keys[i], out);
                } else {
                    fprintf(out, "%s: ", pat->as.map_pat.keys[i]);
                    emit_pattern(out, val, indent);
                }
            }
            if (pat->as.map_pat.has_rest && pat->as.map_pat.rest_pattern) {
                if (pat->as.map_pat.count > 0) fputs(", ", out);
                fputs("...", out);
                emit_pattern(out, pat->as.map_pat.rest_pattern, indent);
            }
            fputc('}', out);
            break;
        case UF_PAT_REST:
            fputs("...", out);
            if (pat->as.rest_pat.subpattern) {
                emit_pattern(out, pat->as.rest_pat.subpattern, indent);
            }
            break;
    }
}

static void emit_block(FILE* out, const UfStmt* stmt, int indent) {
    if (!stmt) return;
    if (stmt->kind == UF_STMT_BLOCK) {
        for (size_t i = 0; i < stmt->as.block.count; ++i) {
            emit_statement(out, stmt->as.block.stmts[i], indent);
        }
    } else {
        emit_statement(out, stmt, indent);
    }
}

static void emit_statement(FILE* out, const UfStmt* stmt, int indent) {
    if (!stmt) return;

    switch (stmt->kind) {
        case UF_STMT_LET:
            emit_indent(out, indent);
            fputs("let ", out);
            if (stmt->as.let_stmt.pattern) {
                emit_pattern(out, stmt->as.let_stmt.pattern, indent);
            } else {
                fputs(stmt->as.let_stmt.name ? stmt->as.let_stmt.name : "_", out);
                if (stmt->as.let_stmt.type_annotation) {
                    fprintf(out, ": %s", stmt->as.let_stmt.type_annotation);
                }
            }
            if (stmt->as.let_stmt.init) {
                fputs(" = ", out);
                emit_expr(out, stmt->as.let_stmt.init, indent);
            }
            fputc('\n', out);
            break;

        case UF_STMT_ASSIGN:
            emit_indent(out, indent);
            if (stmt->as.assign_stmt.pattern) {
                emit_pattern(out, stmt->as.assign_stmt.pattern, indent);
                fputs(" = ", out);
            } else {
                fprintf(out, "%s = ", stmt->as.assign_stmt.name ? stmt->as.assign_stmt.name : "_");
            }
            emit_expr(out, stmt->as.assign_stmt.value, indent);
            fputc('\n', out);
            break;

        case UF_STMT_INDEX_ASSIGN:
            emit_indent(out, indent);
            emit_expr(out, stmt->as.index_assign.target, indent);
            if (stmt->as.index_assign.index->kind == UF_EXPR_LITERAL_STRING &&
                is_valid_ident(stmt->as.index_assign.index->as.string_val)) {
                fprintf(out, ".%s = ", stmt->as.index_assign.index->as.string_val);
            } else {
                fputc('[', out);
                emit_expr(out, stmt->as.index_assign.index, indent);
                fputs("] = ", out);
            }
            emit_expr(out, stmt->as.index_assign.value, indent);
            fputc('\n', out);
            break;

        case UF_STMT_SAY:
            emit_indent(out, indent);
            fputs("say ", out);
            emit_expr(out, stmt->as.say_stmt.expr, indent);
            fputc('\n', out);
            break;

        case UF_STMT_EXPR:
            emit_indent(out, indent);
            emit_expr(out, stmt->as.expr_stmt.expr, indent);
            fputc('\n', out);
            break;

        case UF_STMT_IF: {
            emit_indent(out, indent);
            fputs("if ", out);
            emit_expr(out, stmt->as.if_stmt.condition, indent);
            fputs(":\n", out);
            emit_block(out, stmt->as.if_stmt.then_branch, indent + 1);

            const UfStmt* else_ptr = stmt->as.if_stmt.else_branch;
            while (else_ptr) {
                const UfStmt* inner_if = else_ptr;
                while (inner_if && inner_if->kind == UF_STMT_BLOCK && inner_if->as.block.count == 1 &&
                       inner_if->as.block.stmts[0]->kind == UF_STMT_IF) {
                    inner_if = inner_if->as.block.stmts[0];
                }

                if (inner_if && inner_if->kind == UF_STMT_IF) {
                    emit_indent(out, indent);
                    fputs("else if ", out);
                    emit_expr(out, inner_if->as.if_stmt.condition, indent);
                    fputs(":\n", out);
                    emit_block(out, inner_if->as.if_stmt.then_branch, indent + 1);
                    else_ptr = inner_if->as.if_stmt.else_branch;
                } else {
                    emit_indent(out, indent);
                    fputs("else:\n", out);
                    emit_block(out, else_ptr, indent + 1);
                    break;
                }
            }
            break;
        }

        case UF_STMT_WHILE:
            emit_indent(out, indent);
            fputs("while ", out);
            emit_expr(out, stmt->as.while_stmt.condition, indent);
            fputs(":\n", out);
            emit_block(out, stmt->as.while_stmt.body, indent + 1);
            break;

        case UF_STMT_REPEAT:
            emit_indent(out, indent);
            fputs("repeat ", out);
            emit_expr(out, stmt->as.repeat_stmt.count_expr, indent);
            fputs(" times:\n", out);
            emit_block(out, stmt->as.repeat_stmt.body, indent + 1);
            break;

        case UF_STMT_FOR:
            emit_indent(out, indent);
            fprintf(out, "for %s in ", stmt->as.for_stmt.var_name);
            emit_expr(out, stmt->as.for_stmt.iterable, indent);
            fputs(":\n", out);
            emit_block(out, stmt->as.for_stmt.body, indent + 1);
            break;

        case UF_STMT_BREAK:
            emit_indent(out, indent);
            fputs("break\n", out);
            break;

        case UF_STMT_CONTINUE:
            emit_indent(out, indent);
            fputs("continue\n", out);
            break;

        case UF_STMT_FUNCTION:
            emit_indent(out, indent);
            if (stmt->as.function_stmt.is_async) {
                fputs("async ", out);
            }
            fprintf(out, "function %s", stmt->as.function_stmt.name);
            if (stmt->as.function_stmt.type_param_count > 0) {
                fputc('<', out);
                for (size_t tp = 0; tp < stmt->as.function_stmt.type_param_count; ++tp) {
                    if (tp > 0) fputs(", ", out);
                    fputs(stmt->as.function_stmt.type_params[tp], out);
                    if (stmt->as.function_stmt.type_param_bounds && stmt->as.function_stmt.type_param_bounds[tp]) {
                        fprintf(out, ": %s", stmt->as.function_stmt.type_param_bounds[tp]);
                    }
                }
                fputc('>', out);
            }
            fputc('(', out);
            for (size_t i = 0; i < stmt->as.function_stmt.param_count; ++i) {
                if (i > 0) fputs(", ", out);
                if (i == stmt->as.function_stmt.param_count - 1 && stmt->as.function_stmt.has_rest) {
                    fputs("...", out);
                }
                fputs(stmt->as.function_stmt.params[i], out);
                if (stmt->as.function_stmt.param_types && stmt->as.function_stmt.param_types[i]) {
                    fprintf(out, ": %s", stmt->as.function_stmt.param_types[i]);
                }
                if (stmt->as.function_stmt.param_defaults && stmt->as.function_stmt.param_defaults[i]) {
                    fputs(" = ", out);
                    emit_expr(out, stmt->as.function_stmt.param_defaults[i], indent);
                }
            }
            fputc(')', out);
            if (stmt->as.function_stmt.return_type) {
                fprintf(out, ": %s:\n", stmt->as.function_stmt.return_type);
            } else {
                fputs(":\n", out);
            }
            emit_block(out, stmt->as.function_stmt.body, indent + 1);
            break;

        case UF_STMT_RETURN:
            emit_indent(out, indent);
            fputs("return", out);
            if (stmt->as.return_stmt.value) {
                fputc(' ', out);
                emit_expr(out, stmt->as.return_stmt.value, indent);
            }
            fputc('\n', out);
            break;

        case UF_STMT_BLOCK:
            emit_block(out, stmt, indent);
            break;

        case UF_STMT_TRY_CATCH:
            emit_indent(out, indent);
            fputs("try:\n", out);
            emit_block(out, stmt->as.try_catch.try_block, indent + 1);
            if (stmt->as.try_catch.catch_block) {
                emit_indent(out, indent);
                if (stmt->as.try_catch.catch_var) {
                    fprintf(out, "catch %s:\n", stmt->as.try_catch.catch_var);
                } else {
                    fputs("catch:\n", out);
                }
                emit_block(out, stmt->as.try_catch.catch_block, indent + 1);
            }
            if (stmt->as.try_catch.finally_block) {
                emit_indent(out, indent);
                fputs("finally:\n", out);
                emit_block(out, stmt->as.try_catch.finally_block, indent + 1);
            }
            break;

        case UF_STMT_IMPORT:
            emit_indent(out, indent);
            fprintf(out, "import %s", stmt->as.import_stmt.module_name);
            if (stmt->as.import_stmt.alias) {
                fprintf(out, " as %s", stmt->as.import_stmt.alias);
            }
            fputc('\n', out);
            break;

        case UF_STMT_FROM_IMPORT:
            emit_indent(out, indent);
            fprintf(out, "from %s import ", stmt->as.from_import_stmt.module_name);
            for (size_t i = 0; i < stmt->as.from_import_stmt.count; ++i) {
                if (i > 0) fputs(", ", out);
                fputs(stmt->as.from_import_stmt.symbols[i], out);
                if (stmt->as.from_import_stmt.aliases && stmt->as.from_import_stmt.aliases[i]) {
                    fprintf(out, " as %s", stmt->as.from_import_stmt.aliases[i]);
                }
            }
            fputc('\n', out);
            break;

        case UF_STMT_STRUCT:
            emit_indent(out, indent);
            fprintf(out, "struct %s", stmt->as.struct_stmt.name);
            if (stmt->as.struct_stmt.type_param_count > 0) {
                fputc('<', out);
                for (size_t tp = 0; tp < stmt->as.struct_stmt.type_param_count; ++tp) {
                    if (tp > 0) fputs(", ", out);
                    fputs(stmt->as.struct_stmt.type_params[tp], out);
                    if (stmt->as.struct_stmt.type_param_bounds && stmt->as.struct_stmt.type_param_bounds[tp]) {
                        fprintf(out, ": %s", stmt->as.struct_stmt.type_param_bounds[tp]);
                    }
                }
                fputc('>', out);
            }
            fputs(":\n", out);
            for (size_t i = 0; i < stmt->as.struct_stmt.field_count; ++i) {
                emit_indent(out, indent + 1);
                fputs(stmt->as.struct_stmt.field_names[i], out);
                if (stmt->as.struct_stmt.field_types && stmt->as.struct_stmt.field_types[i]) {
                    fprintf(out, ": %s", stmt->as.struct_stmt.field_types[i]);
                }
                fputc('\n', out);
            }
            for (size_t i = 0; i < stmt->as.struct_stmt.method_count; ++i) {
                emit_statement(out, stmt->as.struct_stmt.methods[i], indent + 1);
            }
            for (size_t i = 0; i < stmt->as.struct_stmt.impl_block_count; ++i) {
                emit_statement(out, stmt->as.struct_stmt.impl_blocks[i], indent + 1);
            }
            break;

        case UF_STMT_TRAIT:
            emit_indent(out, indent);
            fprintf(out, "trait %s", stmt->as.trait_stmt.name);
            if (stmt->as.trait_stmt.type_param_count > 0) {
                fputc('<', out);
                for (size_t tp = 0; tp < stmt->as.trait_stmt.type_param_count; ++tp) {
                    if (tp > 0) fputs(", ", out);
                    fputs(stmt->as.trait_stmt.type_params[tp], out);
                    if (stmt->as.trait_stmt.type_param_bounds && stmt->as.trait_stmt.type_param_bounds[tp]) {
                        fprintf(out, ": %s", stmt->as.trait_stmt.type_param_bounds[tp]);
                    }
                }
                fputc('>', out);
            }
            fputs(":\n", out);
            for (size_t i = 0; i < stmt->as.trait_stmt.method_count; ++i) {
                emit_indent(out, indent + 1);
                fprintf(out, "fn %s(", stmt->as.trait_stmt.method_names[i]);
                size_t pcount = stmt->as.trait_stmt.method_param_counts[i];
                for (size_t p = 0; p < pcount; ++p) {
                    if (p > 0) fputs(", ", out);
                    fputs(stmt->as.trait_stmt.method_param_names[i][p], out);
                    if (stmt->as.trait_stmt.method_param_types && stmt->as.trait_stmt.method_param_types[i] && stmt->as.trait_stmt.method_param_types[i][p]) {
                        fprintf(out, ": %s", stmt->as.trait_stmt.method_param_types[i][p]);
                    }
                }
                fputc(')', out);
                if (stmt->as.trait_stmt.method_return_types && stmt->as.trait_stmt.method_return_types[i]) {
                    fprintf(out, " -> %s", stmt->as.trait_stmt.method_return_types[i]);
                }
                fputc('\n', out);
            }
            break;

        case UF_STMT_IMPL:
            emit_indent(out, indent);
            if (stmt->as.impl_stmt.struct_name) {
                fprintf(out, "impl %s for %s:\n", stmt->as.impl_stmt.trait_name, stmt->as.impl_stmt.struct_name);
            } else {
                fprintf(out, "impl %s:\n", stmt->as.impl_stmt.trait_name);
            }
            for (size_t i = 0; i < stmt->as.impl_stmt.method_count; ++i) {
                emit_statement(out, stmt->as.impl_stmt.methods[i], indent + 1);
            }
            break;

        case UF_STMT_ENUM:
            emit_indent(out, indent);
            fprintf(out, "enum %s:\n", stmt->as.enum_stmt.name);
            for (size_t i = 0; i < stmt->as.enum_stmt.variant_count; ++i) {
                const UfEnumVariant* v = &stmt->as.enum_stmt.variants[i];
                emit_indent(out, indent + 1);
                fputs(v->name, out);
                if (v->field_count > 0) {
                    fputc('(', out);
                    for (size_t j = 0; j < v->field_count; ++j) {
                        if (j > 0) fputs(", ", out);
                        fputs(v->field_names[j], out);
                        if (v->field_types && v->field_types[j]) {
                            fprintf(out, ": %s", v->field_types[j]);
                        }
                    }
                    fputc(')', out);
                }
                fputc('\n', out);
            }
            break;

        case UF_STMT_MATCH:
            emit_indent(out, indent);
            fputs("match ", out);
            emit_expr(out, stmt->as.match_stmt.expr, indent);
            fputs(":\n", out);
            for (size_t i = 0; i < stmt->as.match_stmt.arm_count; ++i) {
                emit_indent(out, indent + 1);
                fputs("when ", out);
                emit_pattern(out, stmt->as.match_stmt.arms[i].pattern, indent + 1);
                if (stmt->as.match_stmt.arms[i].guard) {
                    fputs(" if ", out);
                    emit_expr(out, stmt->as.match_stmt.arms[i].guard, indent + 1);
                }
                fputs(":\n", out);
                emit_block(out, stmt->as.match_stmt.arms[i].body, indent + 2);
            }
            if (stmt->as.match_stmt.else_branch) {
                emit_indent(out, indent + 1);
                fputs("else:\n", out);
                emit_block(out, stmt->as.match_stmt.else_branch, indent + 2);
            }
            break;
    }
}

static bool is_decl_stmt(const UfStmt* stmt) {
    if (!stmt) return false;
    return (stmt->kind == UF_STMT_FUNCTION || stmt->kind == UF_STMT_STRUCT || stmt->kind == UF_STMT_ENUM || stmt->kind == UF_STMT_TRAIT || stmt->kind == UF_STMT_IMPL);
}

void uf_format_program_stream(const UfProgram* program, FILE* out) {
    if (!program) return;

    for (size_t i = 0; i < program->count; ++i) {
        if (i > 0) {
            const UfStmt* prev = program->stmts[i - 1];
            const UfStmt* curr = program->stmts[i];
            if (is_decl_stmt(prev) || is_decl_stmt(curr)) {
                fputc('\n', out);
            }
        }
        emit_statement(out, program->stmts[i], 0);
    }
}

char* uf_format_program(const UfProgram* program) {
    char* buf = NULL;
    size_t len = 0;
    FILE* mem = open_memstream(&buf, &len);
    if (!mem) return NULL;
    uf_format_program_stream(program, mem);
    fclose(mem);
    return buf;
}

bool uf_format_file(const char* filename, bool in_place, bool check_only, UfDiagnosticReporter* reporter) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        if (reporter) {
            SourceSpan empty = { {filename, 1, 1, 0}, {filename, 1, 1, 0} };
            uf_report_diag(reporter, UF_DIAG_LEX_ERROR, empty, "Could not open file for formatting", NULL);
        }
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* src = (char*)malloc(sz + 1);
    if (!src) { fclose(f); return false; }
    size_t read_bytes = fread(src, 1, sz, f);
    src[read_bytes] = '\0';
    fclose(f);

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);

    UfLexer lexer;
    uf_lexer_init(&lexer, filename, src, &arena, &interner, reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, reporter);
    UfProgram* prog = uf_parse_program(&parser);

    if (parser.had_error || !prog) {
        free(src);
        uf_interner_free(&interner);
        uf_arena_free(&arena);
        return false;
    }

    char* formatted = uf_format_program(prog);

    bool identical = (strcmp(src, formatted) == 0);

    if (check_only) {
        free(src);
        free(formatted);
        uf_interner_free(&interner);
        uf_arena_free(&arena);
        return identical;
    }

    if (in_place) {
        if (!identical) {
            FILE* out_f = fopen(filename, "wb");
            if (out_f) {
                fputs(formatted, out_f);
                fclose(out_f);
            }
        }
    } else {
        fputs(formatted, stdout);
    }

    free(src);
    free(formatted);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    return true;
}
