#include "uf_emit_c.h"
#include "../common/uf_common.h"
#include "../lexer/uf_lexer.h"
#include "../parser/uf_parser.h"
#include "../semantic/uf_semantic.h"
#include "../common/uf_diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void emit_indent(FILE* out, int indent) {
    for (int i = 0; i < indent; ++i) fputs("    ", out);
}

/* The emitter tracks lambdas, declared functions/structs/variables, and
 * per-lambda captures/locals in fixed-capacity arrays sized generously for
 * ordinary programs. If a program is large enough to exceed one, silently
 * dropping the overflow would produce wrong-but-compiling C output (e.g. a
 * closure the emitter lost track of silently becomes `uf_null()`), so any
 * overflow instead marks the compile as failed with a specific reason,
 * checked once emission finishes. */
static bool g_emit_limit_exceeded = false;
static char g_emit_limit_reason[256];

static void note_limit_exceeded(const char* what, size_t limit) {
    if (!g_emit_limit_exceeded) {
        g_emit_limit_exceeded = true;
        snprintf(g_emit_limit_reason, sizeof(g_emit_limit_reason),
                 "Error: Program exceeds native C99 compiler limit: %s (max %zu). "
                 "Split the program into smaller functions/modules to work around this.",
                 what, limit);
    }
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
    const UfStmt* fn_stmt;
    int id;
    int parent_id;              /* id of nearest enclosing lambda, or -1 if
                                  * nested directly in a top-level function */
    const char* captures[32];
    size_t capture_count;
    const char* own_locals[64]; /* names let/for/catch-bound directly in this
                                  * lambda's own body (not in nested lambdas) */
    size_t own_local_count;
} UfLambdaInfo;

static const char* lambda_name(const UfLambdaInfo* l) {
    if (l->fn_expr) return l->fn_expr->as.fn_expr.name;
    if (l->fn_stmt) return l->fn_stmt->as.function_stmt.name;
    return NULL;
}

static const char** lambda_params(const UfLambdaInfo* l) {
    if (l->fn_expr) return l->fn_expr->as.fn_expr.params;
    if (l->fn_stmt) return l->fn_stmt->as.function_stmt.params;
    return NULL;
}

static size_t lambda_param_count(const UfLambdaInfo* l) {
    if (l->fn_expr) return l->fn_expr->as.fn_expr.param_count;
    if (l->fn_stmt) return l->fn_stmt->as.function_stmt.param_count;
    return 0;
}

static const UfStmt* lambda_body(const UfLambdaInfo* l) {
    if (l->fn_expr) return l->fn_expr->as.fn_expr.body;
    if (l->fn_stmt) return l->fn_stmt->as.function_stmt.body;
    return NULL;
}

typedef struct {
    const char* name;
    const char** field_names;
    size_t field_count;
} DeclaredStruct;

typedef struct UfEmitContext {
    const char* prefix;      /* "uf_" for main, "uf_m_<safe_name>_" for module */
    const char* mod_name;    /* NULL for main, module name string for module */
    UfLambdaInfo lambdas[256];
    size_t lambda_count;
    const char* declared_fns[512];
    size_t declared_fn_count;
    DeclaredStruct declared_structs[128];
    size_t declared_struct_count;
    const char* declared_vars[256];
    size_t declared_var_count;
    const char* boxed_names[256];
    size_t boxed_name_count;
} UfEmitContext;

static UfEmitContext* g_ctx = NULL;
static size_t g_match_id = 0;

static int find_stmt_lambda_id(const UfStmt* stmt) {
    if (!g_ctx) return -1;
    for (size_t i = 0; i < g_ctx->lambda_count; ++i) {
        if (g_ctx->lambdas[i].fn_stmt == stmt) return g_ctx->lambdas[i].id;
    }
    return -1;
}

static void collect_structs_stmt(const UfStmt* stmt) {
    if (!stmt || !g_ctx) return;
    switch (stmt->kind) {
        case UF_STMT_STRUCT:
            if (g_ctx->declared_struct_count < 128) {
                g_ctx->declared_structs[g_ctx->declared_struct_count].name = stmt->as.struct_stmt.name;
                g_ctx->declared_structs[g_ctx->declared_struct_count].field_names = stmt->as.struct_stmt.field_names;
                g_ctx->declared_structs[g_ctx->declared_struct_count].field_count = stmt->as.struct_stmt.field_count;
                g_ctx->declared_struct_count++;
            } else {
                note_limit_exceeded("more than 128 struct definitions", 128);
            }
            break;
        case UF_STMT_IF:
            collect_structs_stmt(stmt->as.if_stmt.then_branch);
            if (stmt->as.if_stmt.else_branch) collect_structs_stmt(stmt->as.if_stmt.else_branch);
            break;
        case UF_STMT_WHILE:
            collect_structs_stmt(stmt->as.while_stmt.body);
            break;
        case UF_STMT_REPEAT:
            collect_structs_stmt(stmt->as.repeat_stmt.body);
            break;
        case UF_STMT_FOR:
            collect_structs_stmt(stmt->as.for_stmt.body);
            break;
        case UF_STMT_FUNCTION:
            collect_structs_stmt(stmt->as.function_stmt.body);
            break;
        case UF_STMT_BLOCK:
            for (size_t i = 0; i < stmt->as.block.count; ++i) {
                collect_structs_stmt(stmt->as.block.stmts[i]);
            }
            break;
        case UF_STMT_MATCH:
            for (size_t i = 0; i < stmt->as.match_stmt.arm_count; ++i) {
                collect_structs_stmt(stmt->as.match_stmt.arms[i].body);
            }
            if (stmt->as.match_stmt.else_branch) {
                collect_structs_stmt(stmt->as.match_stmt.else_branch);
            }
            break;
        default:
            break;
    }
}

static bool is_declared_function(const char* name) {
    if (!g_ctx) return false;
    for (size_t i = 0; i < g_ctx->declared_fn_count; ++i) {
        if (strcmp(g_ctx->declared_fns[i], name) == 0) return true;
    }
    return false;
}

static bool is_declared_var(const char* name) {
    if (!g_ctx) return false;
    for (size_t i = 0; i < g_ctx->declared_var_count; ++i) {
        if (strcmp(g_ctx->declared_vars[i], name) == 0) return true;
    }
    return false;
}

/* A "boxed" variable is any local/parameter name that some closure captures
 * by that name. Boxed variables are stored as a heap-allocated UfVal* shared
 * between the defining scope and every invocation of every closure that
 * captures it, so assignments made from inside a closure (e.g. a counter
 * incrementing a variable owned by its enclosing function) are visible on
 * subsequent calls and to the enclosing scope, matching the AST
 * interpreter/VM's shared-environment closure semantics. */
static bool is_boxed_name(const char* name) {
    if (!g_ctx) return false;
    for (size_t i = 0; i < g_ctx->boxed_name_count; ++i) {
        if (strcmp(g_ctx->boxed_names[i], name) == 0) return true;
    }
    return false;
}

static void add_boxed_name(const char* name) {
    if (!g_ctx || is_boxed_name(name)) return;
    if (g_ctx->boxed_name_count < 256) {
        g_ctx->boxed_names[g_ctx->boxed_name_count++] = name;
    } else {
        note_limit_exceeded("more than 256 distinct captured-variable names", 256);
    }
}

static int find_lambda_id(const UfExpr* expr) {
    if (!g_ctx) return -1;
    for (size_t i = 0; i < g_ctx->lambda_count; ++i) {
        if (g_ctx->lambdas[i].fn_expr == expr) return g_ctx->lambdas[i].id;
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
        "say", "print", "len", "type_of", "push", "pop",
        "range", "keys", "values", "has_key", "delete",
        "map", "filter", "reduce", "sort", "reverse", "find", "every", "some",
        "clock", "assert", "error",
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

static void collect_lambdas_expr(const UfExpr* expr, int parent_id);
static void collect_lambdas_stmt(const UfStmt* stmt, bool is_toplevel, int parent_id);

static void collect_lambdas_expr(const UfExpr* expr, int parent_id) {
    if (!expr || !g_ctx) return;
    if (expr->kind == UF_EXPR_FUNCTION) {
        int this_id = -1;
        if (g_ctx->lambda_count < 256) {
            this_id = (int)g_ctx->lambda_count;
            g_ctx->lambdas[g_ctx->lambda_count].fn_expr = expr;
            g_ctx->lambdas[g_ctx->lambda_count].fn_stmt = NULL;
            g_ctx->lambdas[g_ctx->lambda_count].id = this_id;
            g_ctx->lambdas[g_ctx->lambda_count].parent_id = parent_id;
            g_ctx->lambdas[g_ctx->lambda_count].capture_count = 0;
            g_ctx->lambdas[g_ctx->lambda_count].own_local_count = 0;
            g_ctx->lambda_count++;
        } else {
            note_limit_exceeded("more than 256 closures/lambdas in one program", 256);
        }
        collect_lambdas_stmt(expr->as.fn_expr.body, false, this_id);
        return;
    }
    switch (expr->kind) {
        case UF_EXPR_UNARY:
            collect_lambdas_expr(expr->as.unary.operand, parent_id);
            break;
        case UF_EXPR_BINARY:
            collect_lambdas_expr(expr->as.binary.left, parent_id);
            collect_lambdas_expr(expr->as.binary.right, parent_id);
            break;
        case UF_EXPR_CALL:
            collect_lambdas_expr(expr->as.call.callee, parent_id);
            for (size_t i = 0; i < expr->as.call.argc; ++i) {
                collect_lambdas_expr(expr->as.call.args[i], parent_id);
            }
            break;
        case UF_EXPR_GROUPING:
            collect_lambdas_expr(expr->as.grouping.inner, parent_id);
            break;
        case UF_EXPR_ARRAY:
            for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                collect_lambdas_expr(expr->as.array_lit.elements[i], parent_id);
            }
            break;
        case UF_EXPR_MAP:
            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                collect_lambdas_expr(expr->as.map_lit.keys[i], parent_id);
                collect_lambdas_expr(expr->as.map_lit.values[i], parent_id);
            }
            break;
        case UF_EXPR_INDEX:
            collect_lambdas_expr(expr->as.index_expr.target, parent_id);
            collect_lambdas_expr(expr->as.index_expr.index, parent_id);
            break;
        default:
            break;
    }
}

static void collect_lambdas_stmt(const UfStmt* stmt, bool is_toplevel, int parent_id) {
    if (!stmt || !g_ctx) return;
    switch (stmt->kind) {
        case UF_STMT_LET:
            collect_lambdas_expr(stmt->as.let_stmt.init, parent_id);
            break;
        case UF_STMT_ASSIGN:
            collect_lambdas_expr(stmt->as.assign_stmt.value, parent_id);
            break;
        case UF_STMT_INDEX_ASSIGN:
            collect_lambdas_expr(stmt->as.index_assign.target, parent_id);
            collect_lambdas_expr(stmt->as.index_assign.index, parent_id);
            collect_lambdas_expr(stmt->as.index_assign.value, parent_id);
            break;
        case UF_STMT_SAY:
            collect_lambdas_expr(stmt->as.say_stmt.expr, parent_id);
            break;
        case UF_STMT_EXPR:
            collect_lambdas_expr(stmt->as.expr_stmt.expr, parent_id);
            break;
        case UF_STMT_IF:
            collect_lambdas_expr(stmt->as.if_stmt.condition, parent_id);
            collect_lambdas_stmt(stmt->as.if_stmt.then_branch, is_toplevel, parent_id);
            if (stmt->as.if_stmt.else_branch) collect_lambdas_stmt(stmt->as.if_stmt.else_branch, is_toplevel, parent_id);
            break;
        case UF_STMT_WHILE:
            collect_lambdas_expr(stmt->as.while_stmt.condition, parent_id);
            collect_lambdas_stmt(stmt->as.while_stmt.body, is_toplevel, parent_id);
            break;
        case UF_STMT_REPEAT:
            collect_lambdas_expr(stmt->as.repeat_stmt.count_expr, parent_id);
            collect_lambdas_stmt(stmt->as.repeat_stmt.body, is_toplevel, parent_id);
            break;
        case UF_STMT_FOR:
            collect_lambdas_expr(stmt->as.for_stmt.iterable, parent_id);
            collect_lambdas_stmt(stmt->as.for_stmt.body, is_toplevel, parent_id);
            break;
        case UF_STMT_FUNCTION: {
            int this_id = is_toplevel ? -1 : parent_id;
            if (!is_toplevel) {
                if (g_ctx->lambda_count < 256) {
                    this_id = (int)g_ctx->lambda_count;
                    g_ctx->lambdas[g_ctx->lambda_count].fn_expr = NULL;
                    g_ctx->lambdas[g_ctx->lambda_count].fn_stmt = stmt;
                    g_ctx->lambdas[g_ctx->lambda_count].id = this_id;
                    g_ctx->lambdas[g_ctx->lambda_count].parent_id = parent_id;
                    g_ctx->lambdas[g_ctx->lambda_count].capture_count = 0;
                    g_ctx->lambdas[g_ctx->lambda_count].own_local_count = 0;
                    g_ctx->lambda_count++;
                } else {
                    note_limit_exceeded("more than 256 closures/lambdas in one program", 256);
                }
            }
            /* A top-level function is never itself a lambda (it has real C
             * parameters, not an env capture), so its body resets the
             * nearest-enclosing-lambda chain to none. */
            collect_lambdas_stmt(stmt->as.function_stmt.body, false, is_toplevel ? -1 : this_id);
            break;
        }
        case UF_STMT_RETURN:
            if (stmt->as.return_stmt.value) collect_lambdas_expr(stmt->as.return_stmt.value, parent_id);
            break;
        case UF_STMT_BLOCK:
            for (size_t i = 0; i < stmt->as.block.count; ++i) {
                collect_lambdas_stmt(stmt->as.block.stmts[i], is_toplevel, parent_id);
            }
            break;
        case UF_STMT_MATCH:
            collect_lambdas_expr(stmt->as.match_stmt.expr, parent_id);
            for (size_t i = 0; i < stmt->as.match_stmt.arm_count; ++i) {
                if (stmt->as.match_stmt.arms[i].guard) {
                    collect_lambdas_expr(stmt->as.match_stmt.arms[i].guard, parent_id);
                }
                collect_lambdas_stmt(stmt->as.match_stmt.arms[i].body, is_toplevel, parent_id);
            }
            if (stmt->as.match_stmt.else_branch) {
                collect_lambdas_stmt(stmt->as.match_stmt.else_branch, is_toplevel, parent_id);
            }
            break;
        case UF_STMT_TRY_CATCH:
            collect_lambdas_stmt(stmt->as.try_catch.try_block, is_toplevel, parent_id);
            collect_lambdas_stmt(stmt->as.try_catch.catch_block, is_toplevel, parent_id);
            break;
        default:
            break;
    }
}

static void find_captures_expr(const UfExpr* expr, const char** locals, size_t local_count, const char** params, size_t param_count, UfLambdaInfo* info);
static void find_captures_stmt(const UfStmt* stmt, const char** locals, size_t* p_local_count, const char** params, size_t param_count, UfLambdaInfo* info);

/* Match patterns bind names (`when x:`, `when Point(a, b):`) that are local
 * to the arm they appear in, just like a let/for/catch binding — record
 * them so capture analysis doesn't mistake them for free variables that
 * need to come from an enclosing scope. */
static void collect_pattern_locals(const UfPattern* pat, const char** locals, size_t* p_local_count) {
    if (!pat) return;
    switch (pat->kind) {
        case UF_PAT_WILDCARD:
        case UF_PAT_LITERAL:
            break;
        case UF_PAT_VARIABLE:
            if (*p_local_count < 64) {
                locals[(*p_local_count)++] = pat->as.var_name;
            } else {
                note_limit_exceeded("more than 64 local bindings in one closure body", 64);
            }
            break;
        case UF_PAT_STRUCT:
            for (size_t i = 0; i < pat->as.struct_pat.field_count; ++i) {
                collect_pattern_locals(pat->as.struct_pat.field_patterns[i], locals, p_local_count);
            }
            break;
    }
}

static void find_captures_expr(const UfExpr* expr, const char** locals, size_t local_count, const char** params, size_t param_count, UfLambdaInfo* info) {
    if (!expr) return;
    if (expr->kind == UF_EXPR_IDENTIFIER) {
        const char* name = expr->as.identifier_name;
        const char* sname = lambda_name(info);
        if (sname && strcmp(name, sname) == 0) {
            return;
        }
        if (!is_in_list(name, params, param_count) &&
            !is_in_list(name, locals, local_count) &&
            !is_declared_function(name) &&
            !is_declared_var(name) &&
            !is_builtin_name(name) &&
            !is_in_list(name, info->captures, info->capture_count)) {
            if (info->capture_count < 32) {
                info->captures[info->capture_count++] = name;
            } else {
                note_limit_exceeded("more than 32 captured variables in one closure", 32);
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
            } else {
                note_limit_exceeded("more than 64 local bindings in one closure body", 64);
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
            } else {
                note_limit_exceeded("more than 64 local bindings in one closure body", 64);
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
        case UF_STMT_FUNCTION:
            if (*p_local_count < 64) {
                locals[(*p_local_count)++] = stmt->as.function_stmt.name;
            } else {
                note_limit_exceeded("more than 64 local bindings in one closure body", 64);
            }
            break;
        case UF_STMT_MATCH:
            find_captures_expr(stmt->as.match_stmt.expr, locals, *p_local_count, params, param_count, info);
            for (size_t i = 0; i < stmt->as.match_stmt.arm_count; ++i) {
                collect_pattern_locals(stmt->as.match_stmt.arms[i].pattern, locals, p_local_count);
                if (stmt->as.match_stmt.arms[i].guard) {
                    find_captures_expr(stmt->as.match_stmt.arms[i].guard, locals, *p_local_count, params, param_count, info);
                }
                find_captures_stmt(stmt->as.match_stmt.arms[i].body, locals, p_local_count, params, param_count, info);
            }
            if (stmt->as.match_stmt.else_branch) {
                find_captures_stmt(stmt->as.match_stmt.else_branch, locals, p_local_count, params, param_count, info);
            }
            break;
        case UF_STMT_TRY_CATCH:
            find_captures_stmt(stmt->as.try_catch.try_block, locals, p_local_count, params, param_count, info);
            if (stmt->as.try_catch.catch_var) {
                if (*p_local_count < 64) {
                    locals[(*p_local_count)++] = stmt->as.try_catch.catch_var;
                } else {
                    note_limit_exceeded("more than 64 local bindings in one closure body", 64);
                }
            }
            find_captures_stmt(stmt->as.try_catch.catch_block, locals, p_local_count, params, param_count, info);
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
                fprintf(out, "%swrap_fn_%s()", g_ctx->prefix, expr->as.identifier_name);
            } else if (is_declared_var(expr->as.identifier_name)) {
                fprintf(out, "%svar_%s", g_ctx->prefix, expr->as.identifier_name);
            } else if (is_boxed_name(expr->as.identifier_name)) {
                fprintf(out, "(*uf_var_%s)", expr->as.identifier_name);
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
                    fputs("__extension__({ UfVal _l = ", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs("; !uf_truthy(_l) ? _l : (", out);
                    emit_expr(out, expr->as.binary.right);
                    fputs("); })", out);
                    break;
                case UF_TOK_OR:
                    fputs("__extension__({ UfVal _l = ", out);
                    emit_expr(out, expr->as.binary.left);
                    fputs("; uf_truthy(_l) ? _l : (", out);
                    emit_expr(out, expr->as.binary.right);
                    fputs("); })", out);
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
                if (strcmp(fn_name, "pop") == 0) {
                    fputs("uf_array_pop(", out); UF_ARG(0); fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "range") == 0) {
                    fprintf(out, "uf_range(%zu", argc);
                    for (size_t i = 0; i < argc; ++i) {
                        fputs(", ", out);
                        emit_expr(out, expr->as.call.args[i]);
                    }
                    fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "keys") == 0) {
                    fputs("uf_map_keys(", out); UF_ARG(0); fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "values") == 0) {
                    fputs("uf_map_values(", out); UF_ARG(0); fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "has_key") == 0) {
                    fputs("uf_map_has_key(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "delete") == 0) {
                    fputs("uf_map_delete(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "map") == 0) {
                    fputs("uf_array_map(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "filter") == 0) {
                    fputs("uf_array_filter(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "reduce") == 0) {
                    fprintf(out, "uf_array_reduce(%zu", argc);
                    for (size_t i = 0; i < argc; ++i) {
                        fputs(", ", out);
                        emit_expr(out, expr->as.call.args[i]);
                    }
                    fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "sort") == 0) {
                    fprintf(out, "uf_array_sort(%zu", argc);
                    for (size_t i = 0; i < argc; ++i) {
                        fputs(", ", out);
                        emit_expr(out, expr->as.call.args[i]);
                    }
                    fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "reverse") == 0) {
                    fputs("uf_array_reverse(", out); UF_ARG(0); fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "find") == 0) {
                    fputs("uf_array_find(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "every") == 0) {
                    fputs("uf_array_every(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "some") == 0) {
                    fputs("uf_array_some(", out); UF_ARG(0); fputs(", ", out); UF_ARG(1); fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "clock") == 0) {
                    fputs("uf_sys_clock()", out);
                    return;
                }
                if (strcmp(fn_name, "assert") == 0) {
                    fprintf(out, "uf_sys_assert(%zu", argc);
                    for (size_t i = 0; i < argc; ++i) {
                        fputs(", ", out);
                        emit_expr(out, expr->as.call.args[i]);
                    }
                    fputc(')', out);
                    return;
                }
                if (strcmp(fn_name, "error") == 0) {
                    fprintf(out, "uf_builtin_error(%zu", argc);
                    for (size_t i = 0; i < argc; ++i) {
                        fputs(", ", out);
                        emit_expr(out, expr->as.call.args[i]);
                    }
                    fputc(')', out);
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
                    fprintf(out, "%sfn_%s(", g_ctx->prefix, fn_name);
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
                fprintf(out, "%smake_lambda_%d(", g_ctx->prefix, id);
                for (size_t c = 0; c < g_ctx->lambdas[id].capture_count; ++c) {
                    if (c > 0) fputs(", ", out);
                    fprintf(out, "uf_var_%s", g_ctx->lambdas[id].captures[c]);
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

static bool pattern_has_condition(const UfPattern* pat) {
    if (!pat) return false;
    switch (pat->kind) {
        case UF_PAT_WILDCARD:
        case UF_PAT_VARIABLE:
            return false;
        case UF_PAT_LITERAL:
            return true;
        case UF_PAT_STRUCT:
            return true;
    }
    return false;
}

static void emit_pattern_condition(FILE* out, const UfPattern* pat, const char* val_expr) {
    if (!pat) return;
    switch (pat->kind) {
        case UF_PAT_WILDCARD:
        case UF_PAT_VARIABLE:
            fputs("1", out);
            break;
        case UF_PAT_LITERAL:
            fprintf(out, "uf_eq_bool(%s, ", val_expr);
            emit_expr(out, pat->as.literal);
            fputc(')', out);
            break;
        case UF_PAT_STRUCT:
            fprintf(out, "(%s.kind == UF_RT_INSTANCE && strcmp(%s.as.instance->name, \"%s\") == 0 && %s.as.instance->field_count == %zu",
                    val_expr, val_expr, pat->as.struct_pat.struct_name, val_expr, pat->as.struct_pat.field_count);
            for (size_t i = 0; i < pat->as.struct_pat.field_count; ++i) {
                if (pattern_has_condition(pat->as.struct_pat.field_patterns[i])) {
                    char field_expr[256];
                    snprintf(field_expr, sizeof(field_expr), "%s.as.instance->fields[%zu]", val_expr, i);
                    fputs(" && ", out);
                    emit_pattern_condition(out, pat->as.struct_pat.field_patterns[i], field_expr);
                }
            }
            fputc(')', out);
            break;
    }
}

static void emit_pattern_bindings(FILE* out, const UfPattern* pat, const char* val_expr, int indent) {
    if (!pat) return;
    switch (pat->kind) {
        case UF_PAT_WILDCARD:
        case UF_PAT_LITERAL:
            break;
        case UF_PAT_VARIABLE:
            emit_indent(out, indent);
            if (is_boxed_name(pat->as.var_name)) {
                fprintf(out, "UfVal* uf_var_%s = uf_box_new(%s);\n", pat->as.var_name, val_expr);
            } else {
                fprintf(out, "UfVal uf_var_%s = %s;\n", pat->as.var_name, val_expr);
            }
            break;
        case UF_PAT_STRUCT:
            for (size_t i = 0; i < pat->as.struct_pat.field_count; ++i) {
                char field_expr[256];
                snprintf(field_expr, sizeof(field_expr), "%s.as.instance->fields[%zu]", val_expr, i);
                emit_pattern_bindings(out, pat->as.struct_pat.field_patterns[i], field_expr, indent);
            }
            break;
    }
}

static void emit_stmt(FILE* out, const UfStmt* stmt, int indent, bool is_toplevel) {
    if (!stmt) return;

    switch (stmt->kind) {
        case UF_STMT_LET: {
            emit_indent(out, indent);
            bool let_boxed = !is_toplevel && is_boxed_name(stmt->as.let_stmt.name);
            if (is_toplevel) {
                fprintf(out, "%svar_%s = ", g_ctx->prefix, stmt->as.let_stmt.name);
            } else if (let_boxed) {
                fprintf(out, "UfVal* uf_var_%s = uf_box_new(", stmt->as.let_stmt.name);
            } else {
                fprintf(out, "UfVal uf_var_%s = ", stmt->as.let_stmt.name);
            }
            if (stmt->as.let_stmt.init) {
                emit_expr(out, stmt->as.let_stmt.init);
            } else {
                fputs("uf_null()", out);
            }
            if (let_boxed) fputc(')', out);
            fputs(";\n", out);
            break;
        }
        case UF_STMT_ASSIGN:
            emit_indent(out, indent);
            if (is_declared_var(stmt->as.assign_stmt.name)) {
                fprintf(out, "%svar_%s = ", g_ctx->prefix, stmt->as.assign_stmt.name);
            } else if (is_boxed_name(stmt->as.assign_stmt.name)) {
                fprintf(out, "*uf_var_%s = ", stmt->as.assign_stmt.name);
            } else {
                fprintf(out, "uf_var_%s = ", stmt->as.assign_stmt.name);
            }
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
            if (is_boxed_name(stmt->as.for_stmt.var_name)) {
                fprintf(out, "UfVal* uf_var_%s = uf_box_new(uf_iter_get(_iter, _i));\n", stmt->as.for_stmt.var_name);
            } else {
                fprintf(out, "UfVal uf_var_%s = uf_iter_get(_iter, _i);\n", stmt->as.for_stmt.var_name);
            }
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
            if (is_toplevel) {
                if (g_ctx && g_ctx->mod_name) {
                    fputs("g_catch_stack = _fn_catch_entry; return _exports;\n", out);
                } else {
                    fputs("uf_cleanup(); return 0;\n", out);
                }
            } else {
                fputs("{\n", out);
                emit_indent(out, indent + 1);
                fputs("UfVal _ret_val = ", out);
                if (stmt->as.return_stmt.value) emit_expr(out, stmt->as.return_stmt.value);
                else fputs("uf_null()", out);
                fputs(";\n", out);
                emit_indent(out, indent + 1);
                fputs("g_catch_stack = _fn_catch_entry;\n", out);
                emit_indent(out, indent + 1);
                fputs("return _ret_val;\n", out);
                emit_indent(out, indent);
                fputs("}\n", out);
            }
            break;
        case UF_STMT_BLOCK:
            for (size_t i = 0; i < stmt->as.block.count; ++i) {
                emit_stmt(out, stmt->as.block.stmts[i], indent, false);
            }
            break;
        case UF_STMT_FUNCTION:
            if (!is_toplevel) {
                int id = find_stmt_lambda_id(stmt);
                if (id >= 0) {
                    emit_indent(out, indent);
                    bool fn_boxed = is_boxed_name(stmt->as.function_stmt.name);
                    if (fn_boxed) {
                        fprintf(out, "UfVal* uf_var_%s = uf_box_new(%smake_lambda_%d(", stmt->as.function_stmt.name, g_ctx->prefix, id);
                    } else {
                        fprintf(out, "UfVal uf_var_%s = %smake_lambda_%d(", stmt->as.function_stmt.name, g_ctx->prefix, id);
                    }
                    for (size_t c = 0; c < g_ctx->lambdas[id].capture_count; ++c) {
                        if (c > 0) fputs(", ", out);
                        fprintf(out, "uf_var_%s", g_ctx->lambdas[id].captures[c]);
                    }
                    if (fn_boxed) fputc(')', out);
                    fputs(");\n", out);
                }
            }
            break;
        case UF_STMT_STRUCT:
            /* Struct constructors and metadata are emitted globally */
            break;
        case UF_STMT_MATCH: {
            size_t mid = g_match_id++;
            emit_indent(out, indent);
            fputs("{\n", out);

            emit_indent(out, indent + 1);
            fprintf(out, "UfVal _mval_%zu = ", mid);
            emit_expr(out, stmt->as.match_stmt.expr);
            fputs(";\n", out);

            emit_indent(out, indent + 1);
            fprintf(out, "bool _matched_%zu = false;\n", mid);

            char mval_str[64];
            snprintf(mval_str, sizeof(mval_str), "_mval_%zu", mid);

            for (size_t a = 0; a < stmt->as.match_stmt.arm_count; ++a) {
                const UfMatchArm* arm = &stmt->as.match_stmt.arms[a];
                emit_indent(out, indent + 1);
                fprintf(out, "if (!_matched_%zu", mid);
                if (pattern_has_condition(arm->pattern)) {
                    fputs(" && ", out);
                    emit_pattern_condition(out, arm->pattern, mval_str);
                }
                fputs(") {\n", out);

                emit_pattern_bindings(out, arm->pattern, mval_str, indent + 2);

                if (arm->guard) {
                    emit_indent(out, indent + 2);
                    fputs("if (uf_truthy(", out);
                    emit_expr(out, arm->guard);
                    fputs(")) {\n", out);

                    emit_indent(out, indent + 3);
                    fprintf(out, "_matched_%zu = true;\n", mid);
                    emit_indent(out, indent + 3);
                    fputs("{\n", out);
                    emit_stmt(out, arm->body, indent + 4, false);
                    emit_indent(out, indent + 3);
                    fputs("}\n", out);

                    emit_indent(out, indent + 2);
                    fputs("}\n", out);
                } else {
                    emit_indent(out, indent + 2);
                    fprintf(out, "_matched_%zu = true;\n", mid);
                    emit_indent(out, indent + 2);
                    fputs("{\n", out);
                    emit_stmt(out, arm->body, indent + 3, false);
                    emit_indent(out, indent + 2);
                    fputs("}\n", out);
                }

                emit_indent(out, indent + 1);
                fputs("}\n", out);
            }

            if (stmt->as.match_stmt.else_branch) {
                emit_indent(out, indent + 1);
                fprintf(out, "if (!_matched_%zu) {\n", mid);
                emit_stmt(out, stmt->as.match_stmt.else_branch, indent + 2, false);
                emit_indent(out, indent + 1);
                fputs("}\n", out);
            }

            emit_indent(out, indent);
            fputs("}\n", out);
            break;
        }
        case UF_STMT_TRY_CATCH: {
            emit_indent(out, indent);
            fputs("{\n", out);
            emit_indent(out, indent + 1);
            fputs("UfCatchFrame _frame;\n", out);
            emit_indent(out, indent + 1);
            fputs("uf_catch_push(&_frame);\n", out);
            emit_indent(out, indent + 1);
            fputs("if (setjmp(_frame.buf) == 0) {\n", out);
            emit_stmt(out, stmt->as.try_catch.try_block, indent + 2, false);
            emit_indent(out, indent + 2);
            fputs("uf_catch_pop();\n", out);
            emit_indent(out, indent + 1);
            fputs("} else {\n", out);
            if (stmt->as.try_catch.catch_var) {
                emit_indent(out, indent + 2);
                if (is_boxed_name(stmt->as.try_catch.catch_var)) {
                    fprintf(out, "UfVal* uf_var_%s = uf_box_new(_frame.error);\n", stmt->as.try_catch.catch_var);
                } else {
                    fprintf(out, "UfVal uf_var_%s = _frame.error;\n", stmt->as.try_catch.catch_var);
                }
            }
            emit_stmt(out, stmt->as.try_catch.catch_block, indent + 2, false);
            emit_indent(out, indent + 1);
            fputs("}\n", out);
            emit_indent(out, indent);
            fputs("}\n", out);
            break;
        }
        case UF_STMT_IMPORT: {
            emit_indent(out, indent);
            const char* bound = stmt->as.import_stmt.alias ? stmt->as.import_stmt.alias : stmt->as.import_stmt.module_name;
            if (is_toplevel) {
                fprintf(out, "%svar_%s = uf_load_module(\"%s\");\n", g_ctx->prefix, bound, stmt->as.import_stmt.module_name);
            } else if (is_boxed_name(bound)) {
                fprintf(out, "UfVal* uf_var_%s = uf_box_new(uf_load_module(\"%s\"));\n", bound, stmt->as.import_stmt.module_name);
            } else {
                fprintf(out, "UfVal uf_var_%s = uf_load_module(\"%s\");\n", bound, stmt->as.import_stmt.module_name);
            }
            break;
        }
        case UF_STMT_FROM_IMPORT: {
            emit_indent(out, indent);
            fputs("{\n", out);
            emit_indent(out, indent + 1);
            fprintf(out, "UfVal _mod = uf_load_module(\"%s\");\n", stmt->as.from_import_stmt.module_name);
            for (size_t i = 0; i < stmt->as.from_import_stmt.count; ++i) {
                const char* sym = stmt->as.from_import_stmt.symbols[i];
                const char* bound = (stmt->as.from_import_stmt.aliases && stmt->as.from_import_stmt.aliases[i])
                                    ? stmt->as.from_import_stmt.aliases[i] : sym;
                emit_indent(out, indent + 1);
                if (is_toplevel) {
                    fprintf(out, "%svar_%s = uf_import_symbol(_mod, \"%s\", \"%s\");\n", g_ctx->prefix, bound, stmt->as.from_import_stmt.module_name, sym);
                } else if (is_boxed_name(bound)) {
                    fprintf(out, "UfVal* uf_var_%s = uf_box_new(uf_import_symbol(_mod, \"%s\", \"%s\"));\n", bound, stmt->as.from_import_stmt.module_name, sym);
                } else {
                    fprintf(out, "UfVal uf_var_%s = uf_import_symbol(_mod, \"%s\", \"%s\");\n", bound, stmt->as.from_import_stmt.module_name, sym);
                }
            }
            emit_indent(out, indent);
            fputs("}\n", out);
            break;
        }
        default:
            break;
    }
}

static bool context_has_fn(const UfEmitContext* ctx, const char* name) {
    for (size_t i = 0; i < ctx->declared_fn_count; ++i) {
        if (strcmp(ctx->declared_fns[i], name) == 0) return true;
    }
    return false;
}

static bool context_has_var(const UfEmitContext* ctx, const char* name) {
    for (size_t i = 0; i < ctx->declared_var_count; ++i) {
        if (strcmp(ctx->declared_vars[i], name) == 0) return true;
    }
    return false;
}

static void init_emit_context(UfEmitContext* ctx, const UfProgram* program, const char* prefix, const char* mod_name) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->prefix = prefix;
    ctx->mod_name = mod_name;
    g_ctx = ctx;

    /* Populate declared structs */
    for (size_t i = 0; i < program->count; ++i) {
        collect_structs_stmt(program->stmts[i]);
    }

    /* Populate top-level functions and struct constructor symbols */
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_FUNCTION && !context_has_fn(ctx, stmt->as.function_stmt.name)) {
            if (ctx->declared_fn_count < 512) {
                ctx->declared_fns[ctx->declared_fn_count++] = stmt->as.function_stmt.name;
            } else {
                note_limit_exceeded("more than 512 top-level functions/structs in one module", 512);
            }
        }
    }
    for (size_t i = 0; i < ctx->declared_struct_count; ++i) {
        if (!context_has_fn(ctx, ctx->declared_structs[i].name)) {
            if (ctx->declared_fn_count < 512) {
                ctx->declared_fns[ctx->declared_fn_count++] = ctx->declared_structs[i].name;
            } else {
                note_limit_exceeded("more than 512 top-level functions/structs in one module", 512);
            }
        }
    }

    /* Populate top-level variables (from let, import, from-import) */
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_LET) {
            if (!context_has_var(ctx, stmt->as.let_stmt.name)) {
                if (ctx->declared_var_count < 256) {
                    ctx->declared_vars[ctx->declared_var_count++] = stmt->as.let_stmt.name;
                } else {
                    note_limit_exceeded("more than 256 top-level variables in one module", 256);
                }
            }
        } else if (stmt->kind == UF_STMT_IMPORT) {
            const char* bound = stmt->as.import_stmt.alias ? stmt->as.import_stmt.alias : stmt->as.import_stmt.module_name;
            if (!context_has_var(ctx, bound)) {
                if (ctx->declared_var_count < 256) {
                    ctx->declared_vars[ctx->declared_var_count++] = bound;
                } else {
                    note_limit_exceeded("more than 256 top-level variables in one module", 256);
                }
            }
        } else if (stmt->kind == UF_STMT_FROM_IMPORT) {
            for (size_t s = 0; s < stmt->as.from_import_stmt.count; ++s) {
                const char* sym = stmt->as.from_import_stmt.symbols[s];
                const char* bound = (stmt->as.from_import_stmt.aliases && stmt->as.from_import_stmt.aliases[s])
                                    ? stmt->as.from_import_stmt.aliases[s] : sym;
                if (!context_has_var(ctx, bound)) {
                    if (ctx->declared_var_count < 256) {
                        ctx->declared_vars[ctx->declared_var_count++] = bound;
                    } else {
                        note_limit_exceeded("more than 256 top-level variables in one module", 256);
                    }
                }
            }
        }
    }

    /* Populate lambdas */
    for (size_t i = 0; i < program->count; ++i) {
        collect_lambdas_stmt(program->stmts[i], true, -1);
    }
    for (size_t i = 0; i < ctx->lambda_count; ++i) {
        const char* locals[64];
        size_t local_count = 0;
        find_captures_stmt(lambda_body(&ctx->lambdas[i]), locals, &local_count,
                           lambda_params(&ctx->lambdas[i]),
                           lambda_param_count(&ctx->lambdas[i]),
                           &ctx->lambdas[i]);
        for (size_t j = 0; j < local_count && j < 64; ++j) {
            ctx->lambdas[i].own_locals[j] = locals[j];
        }
        ctx->lambdas[i].own_local_count = local_count;
    }

    /* Propagate captures upward through the closure nesting chain: if a
     * lambda C nested inside lambda P reads a name that isn't resolvable
     * within P's own scope (not P's local/param/self-name, and not global),
     * P must also capture that name itself so it has something to pass down
     * when constructing C. Lambdas are recorded in pre-order (a lambda's id
     * is always lower than any lambda nested inside it), so a single
     * highest-to-lowest pass finalizes each lambda's own capture set
     * (including anything propagated up from its children) before it is
     * used to propagate further up to its own parent. */
    for (size_t ii = ctx->lambda_count; ii > 0; --ii) {
        size_t i = ii - 1;
        int pid = ctx->lambdas[i].parent_id;
        if (pid < 0) continue;
        UfLambdaInfo* parent = &ctx->lambdas[(size_t)pid];
        for (size_t c = 0; c < ctx->lambdas[i].capture_count; ++c) {
            const char* name = ctx->lambdas[i].captures[c];
            if (is_in_list(name, parent->own_locals, parent->own_local_count)) continue;
            if (is_in_list(name, lambda_params(parent), lambda_param_count(parent))) continue;
            const char* pname = lambda_name(parent);
            if (pname && strcmp(name, pname) == 0) continue;
            if (is_declared_function(name) || is_declared_var(name) || is_builtin_name(name)) continue;
            if (!is_in_list(name, parent->captures, parent->capture_count)) {
                if (parent->capture_count < 32) {
                    parent->captures[parent->capture_count++] = name;
                } else {
                    note_limit_exceeded("more than 32 captured variables in one closure", 32);
                }
            }
        }
    }

    /* Any name captured by any closure must be boxed at every site where it
     * is declared, so that the declaration's storage class (UfVal vs boxed
     * UfVal*) agrees with how captures/reads/assignments reference it. */
    for (size_t i = 0; i < ctx->lambda_count; ++i) {
        for (size_t c = 0; c < ctx->lambdas[i].capture_count; ++c) {
            add_boxed_name(ctx->lambdas[i].captures[c]);
        }
    }
}

static void sanitize_module_name(const char* name, char* out, size_t out_size) {
    size_t j = 0;
    for (size_t i = 0; name[i] && j + 1 < out_size; ++i) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            out[j++] = c;
        } else {
            out[j++] = '_';
        }
    }
    out[j] = '\0';
}

static void emit_module_unit(FILE* out, UfEmitContext* ctx, const UfProgram* program, bool is_main) {
    g_ctx = ctx;

    /* 1. Declare top-level variables */
    fprintf(out, "/* Top-level global variables for %s */\n", ctx->mod_name ? ctx->mod_name : "main");
    for (size_t i = 0; i < ctx->declared_var_count; ++i) {
        fprintf(out, "static UfVal %svar_%s;\n", ctx->prefix, ctx->declared_vars[i]);
    }
    fputs("\n", out);

    /* 2. Lambda environment definitions & forward declarations */
    if (ctx->lambda_count > 0) {
        fprintf(out, "/* Lambda environments for %s */\n", ctx->mod_name ? ctx->mod_name : "main");
        for (size_t i = 0; i < ctx->lambda_count; ++i) {
            fprintf(out, "typedef struct {\n");
            if (ctx->lambdas[i].capture_count > 0) {
                for (size_t c = 0; c < ctx->lambdas[i].capture_count; ++c) {
                    const char* cname = ctx->lambdas[i].captures[c];
                    fprintf(out, "    %s uf_var_%s;\n", is_boxed_name(cname) ? "UfVal*" : "UfVal", cname);
                }
            } else {
                fputs("    int _dummy;\n", out);
            }
            fprintf(out, "} %senv_lambda_%zu;\n\n", ctx->prefix, i);

            fprintf(out, "static UfVal %slambda_%zu(void* _raw_env, size_t _argc, UfVal* _args);\n\n", ctx->prefix, i);

            fprintf(out, "static inline UfVal %smake_lambda_%zu(", ctx->prefix, i);
            if (ctx->lambdas[i].capture_count > 0) {
                for (size_t c = 0; c < ctx->lambdas[i].capture_count; ++c) {
                    if (c > 0) fputs(", ", out);
                    const char* cname = ctx->lambdas[i].captures[c];
                    fprintf(out, "%s uf_var_%s", is_boxed_name(cname) ? "UfVal*" : "UfVal", cname);
                }
            } else {
                fputs("void", out);
            }
            fprintf(out, ") {\n");
            fprintf(out, "    %senv_lambda_%zu _env;\n", ctx->prefix, i);
            if (ctx->lambdas[i].capture_count > 0) {
                for (size_t c = 0; c < ctx->lambdas[i].capture_count; ++c) {
                    fprintf(out, "    _env.uf_var_%s = uf_var_%s;\n", ctx->lambdas[i].captures[c], ctx->lambdas[i].captures[c]);
                }
                fprintf(out, "    return uf_closure_new(%slambda_%zu, &_env, sizeof(_env));\n", ctx->prefix, i);
            } else {
                fprintf(out, "    (void)_env;\n");
                fprintf(out, "    return uf_closure_new(%slambda_%zu, NULL, 0);\n", ctx->prefix, i);
            }
            fprintf(out, "}\n\n");
        }
    }

    /* 3. Struct constructor declarations and first-class wrappers */
    if (ctx->declared_struct_count > 0) {
        fprintf(out, "/* Structs for %s */\n", ctx->mod_name ? ctx->mod_name : "main");
        for (size_t i = 0; i < ctx->declared_struct_count; ++i) {
            const DeclaredStruct* s = &ctx->declared_structs[i];
            if (s->field_count > 0) {
                fprintf(out, "static const char* %sstruct_fields_%s[] = { ", ctx->prefix, s->name);
                for (size_t f = 0; f < s->field_count; ++f) {
                    if (f > 0) fputs(", ", out);
                    fprintf(out, "\"%s\"", s->field_names[f]);
                }
                fputs(" };\n", out);

                fprintf(out, "static UfVal %sfn_%s(", ctx->prefix, s->name);
                for (size_t f = 0; f < s->field_count; ++f) {
                    if (f > 0) fputs(", ", out);
                    fprintf(out, "UfVal _f%zu", f);
                }
                fputs(") {\n", out);
                fprintf(out, "    UfVal _args[%zu] = { ", s->field_count);
                for (size_t f = 0; f < s->field_count; ++f) {
                    if (f > 0) fputs(", ", out);
                    fprintf(out, "_f%zu", f);
                }
                fputs(" };\n", out);
                fprintf(out, "    return uf_instance_new(\"%s\", %sstruct_fields_%s, %zu, %zu, _args);\n",
                        s->name, ctx->prefix, s->name, s->field_count, s->field_count);
                fputs("}\n", out);
            } else {
                fprintf(out, "static UfVal %sfn_%s(void) {\n", ctx->prefix, s->name);
                fprintf(out, "    return uf_instance_new(\"%s\", NULL, 0, 0, NULL);\n", s->name);
                fputs("}\n", out);
            }

            fprintf(out, "static UfVal %swrapper_%s(void* env, size_t argc, UfVal* args) {\n", ctx->prefix, s->name);
            fprintf(out, "    (void)env;\n");
            if (s->field_count > 0) {
                fprintf(out, "    return uf_instance_new(\"%s\", %sstruct_fields_%s, %zu, argc, args);\n",
                        s->name, ctx->prefix, s->name, s->field_count);
            } else {
                fprintf(out, "    return uf_instance_new(\"%s\", NULL, 0, argc, args);\n", s->name);
            }
            fputs("}\n", out);

            fprintf(out, "static inline UfVal %swrap_fn_%s(void) {\n", ctx->prefix, s->name);
            fprintf(out, "    return uf_closure_new(%swrapper_%s, NULL, 0);\n", ctx->prefix, s->name);
            fputs("}\n\n", out);
        }
    }

    /* 4. Function declarations and first-class wrappers */
    fprintf(out, "/* Functions for %s */\n", ctx->mod_name ? ctx->mod_name : "main");
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_FUNCTION) {
            const char* fname = stmt->as.function_stmt.name;
            size_t pcount = stmt->as.function_stmt.param_count;
            fprintf(out, "static UfVal %sfn_%s(", ctx->prefix, fname);
            for (size_t p = 0; p < pcount; ++p) {
                if (p > 0) fputs(", ", out);
                fprintf(out, "UfVal uf_var_%s", stmt->as.function_stmt.params[p]);
            }
            if (pcount == 0) fputs("void", out);
            fputs(");\n", out);

            fprintf(out, "static UfVal %swrapper_%s(void* env, size_t argc, UfVal* args) {\n", ctx->prefix, fname);
            fprintf(out, "    (void)env;\n");
            fprintf(out, "    return %sfn_%s(", ctx->prefix, fname);
            for (size_t p = 0; p < pcount; ++p) {
                if (p > 0) fputs(", ", out);
                fprintf(out, "(argc > %zu ? args[%zu] : uf_null())", p, p);
            }
            fputs(");\n}\n", out);

            fprintf(out, "static inline UfVal %swrap_fn_%s(void) {\n", ctx->prefix, fname);
            fprintf(out, "    return uf_closure_new(%swrapper_%s, NULL, 0);\n", ctx->prefix, fname);
            fprintf(out, "}\n\n");
        }
    }

    /* 5. Lambda implementations */
    if (ctx->lambda_count > 0) {
        for (size_t i = 0; i < ctx->lambda_count; ++i) {
            fprintf(out, "static UfVal %slambda_%zu(void* _raw_env, size_t _argc, UfVal* _args) {\n", ctx->prefix, i);
            fprintf(out, "    %senv_lambda_%zu* _env = (%senv_lambda_%zu*)_raw_env;\n", ctx->prefix, i, ctx->prefix, i);
            fprintf(out, "    (void)_env; (void)_argc; (void)_args;\n");
            fprintf(out, "    UfCatchFrame* _fn_catch_entry = g_catch_stack;\n");
            fprintf(out, "    (void)_fn_catch_entry;\n");
            for (size_t c = 0; c < ctx->lambdas[i].capture_count; ++c) {
                const char* cname = ctx->lambdas[i].captures[c];
                const char* ctype = is_boxed_name(cname) ? "UfVal*" : "UfVal";
                fprintf(out, "    %s uf_var_%s = _env->uf_var_%s;\n", ctype, cname, cname);
            }
            const char* lname = lambda_name(&ctx->lambdas[i]);
            if (lname) {
                if (is_boxed_name(lname)) {
                    fprintf(out, "    UfVal* uf_var_%s = uf_box_new(uf_closure_new(%slambda_%zu, _raw_env, sizeof(%senv_lambda_%zu)));\n", lname, ctx->prefix, i, ctx->prefix, i);
                } else {
                    fprintf(out, "    UfVal uf_var_%s = uf_closure_new(%slambda_%zu, _raw_env, sizeof(%senv_lambda_%zu));\n", lname, ctx->prefix, i, ctx->prefix, i);
                }
            }
            size_t pcount = lambda_param_count(&ctx->lambdas[i]);
            const char** params = lambda_params(&ctx->lambdas[i]);
            for (size_t p = 0; p < pcount; ++p) {
                if (is_boxed_name(params[p])) {
                    fprintf(out, "    UfVal* uf_var_%s = uf_box_new((_argc > %zu) ? _args[%zu] : uf_null());\n",
                            params[p], p, p);
                } else {
                    fprintf(out, "    UfVal uf_var_%s = (_argc > %zu) ? _args[%zu] : uf_null();\n",
                            params[p], p, p);
                }
            }
            emit_stmt(out, lambda_body(&ctx->lambdas[i]), 1, false);
            fprintf(out, "    g_catch_stack = _fn_catch_entry;\n");
            fprintf(out, "    return uf_null();\n");
            fprintf(out, "}\n\n");
        }
    }

    /* 6. Function definitions */
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_FUNCTION) {
            fprintf(out, "static UfVal %sfn_%s(", ctx->prefix, stmt->as.function_stmt.name);
            for (size_t p = 0; p < stmt->as.function_stmt.param_count; ++p) {
                if (p > 0) fputs(", ", out);
                const char* pname = stmt->as.function_stmt.params[p];
                if (is_boxed_name(pname)) {
                    fprintf(out, "UfVal uf_param_%s", pname);
                } else {
                    fprintf(out, "UfVal uf_var_%s", pname);
                }
            }
            if (stmt->as.function_stmt.param_count == 0) fputs("void", out);
            fputs(") {\n", out);
            fputs("    UfCatchFrame* _fn_catch_entry = g_catch_stack;\n", out);
            fputs("    (void)_fn_catch_entry;\n", out);
            for (size_t p = 0; p < stmt->as.function_stmt.param_count; ++p) {
                const char* pname = stmt->as.function_stmt.params[p];
                if (is_boxed_name(pname)) {
                    fprintf(out, "    UfVal* uf_var_%s = uf_box_new(uf_param_%s);\n", pname, pname);
                }
            }
            emit_stmt(out, stmt->as.function_stmt.body, 1, false);
            fputs("    g_catch_stack = _fn_catch_entry;\n", out);
            fputs("    return uf_null();\n", out);
            fputs("}\n\n", out);
        }
    }

    /* 7. Module Initializer (if module) */
    if (!is_main) {
        char safe_name[256];
        sanitize_module_name(ctx->mod_name, safe_name, sizeof(safe_name));
        fprintf(out, "static UfVal uf_init_mod_%s(void) {\n", safe_name);
        fputs("    UfCatchFrame* _fn_catch_entry = g_catch_stack;\n", out);
        fputs("    (void)_fn_catch_entry;\n", out);
        fputs("    UfVal _exports = uf_map_new(16);\n\n", out);

        for (size_t i = 0; i < program->count; ++i) {
            UfStmt* stmt = program->stmts[i];
            if (stmt->kind != UF_STMT_FUNCTION && stmt->kind != UF_STMT_STRUCT) {
                emit_stmt(out, stmt, 1, true);
            }
        }

        /* Export top-level variables */
        for (size_t i = 0; i < ctx->declared_var_count; ++i) {
            fprintf(out, "    uf_set(_exports, uf_str(\"%s\"), %svar_%s);\n",
                    ctx->declared_vars[i], ctx->prefix, ctx->declared_vars[i]);
        }

        /* Export functions */
        for (size_t i = 0; i < program->count; ++i) {
            UfStmt* stmt = program->stmts[i];
            if (stmt->kind == UF_STMT_FUNCTION) {
                const char* fname = stmt->as.function_stmt.name;
                fprintf(out, "    uf_set(_exports, uf_str(\"%s\"), %swrap_fn_%s());\n",
                        fname, ctx->prefix, fname);
            }
        }

        /* Export structs */
        for (size_t i = 0; i < ctx->declared_struct_count; ++i) {
            const char* sname = ctx->declared_structs[i].name;
            fprintf(out, "    uf_set(_exports, uf_str(\"%s\"), %swrap_fn_%s());\n",
                    sname, ctx->prefix, sname);
        }

        fputs("\n    g_catch_stack = _fn_catch_entry;\n", out);
        fputs("    return _exports;\n", out);
        fputs("}\n\n", out);
    }
}

/* ========================================================================= */
/* Module Collection & Multi-File Compilation                               */
/* ========================================================================= */

#define UF_MAX_COMPILED_MODULES 64

typedef struct {
    char name[128];
    char safe_name[128];
    char* resolved_path;
    char* source_text;
    UfArena arena;
    UfInterner interner;
    UfProgram* program;
    UfEmitContext ctx;
} UfCompiledModule;

typedef struct {
    UfCompiledModule modules[UF_MAX_COMPILED_MODULES];
    size_t count;
} UfModuleCollection;

static bool is_builtin_module(const char* name) {
    return (strcmp(name, "math") == 0 ||
            strcmp(name, "strings") == 0 ||
            strcmp(name, "sys") == 0 ||
            strcmp(name, "fs") == 0 ||
            strcmp(name, "random") == 0 ||
            strcmp(name, "time") == 0 ||
            strcmp(name, "json") == 0);
}

static char* resolve_module_file_path(const char* mod_name, const char* caller_path) {
    char path[1024];

    /* 1. Relative to caller file */
    if (caller_path && strcmp(caller_path, "<stdin>") != 0 && strcmp(caller_path, "<test>") != 0) {
        const char* last_slash = strrchr(caller_path, '/');
        if (last_slash) {
            size_t dir_len = (size_t)(last_slash - caller_path);
            snprintf(path, sizeof(path), "%.*s/%s.unfish", (int)dir_len, caller_path, mod_name);
            if (access(path, R_OK) == 0) return strdup(path);

            snprintf(path, sizeof(path), "%.*s/%s", (int)dir_len, caller_path, mod_name);
            if (access(path, R_OK) == 0) return strdup(path);
        }
    }

    /* 2. Relative to current working directory */
    snprintf(path, sizeof(path), "%s.unfish", mod_name);
    if (access(path, R_OK) == 0) return strdup(path);

    snprintf(path, sizeof(path), "%s", mod_name);
    if (access(path, R_OK) == 0) return strdup(path);

    /* 3. Standard library path: src/stdlib/<name>.unfish */
    snprintf(path, sizeof(path), "src/stdlib/%s.unfish", mod_name);
    if (access(path, R_OK) == 0) return strdup(path);

    snprintf(path, sizeof(path), "src/stdlib/%s", mod_name);
    if (access(path, R_OK) == 0) return strdup(path);

    /* 4. UNFISH_PATH */
    const char* env_path = getenv("UNFISH_PATH");
    if (env_path) {
        char* copy = strdup(env_path);
        char* token = strtok(copy, ":");
        while (token) {
            snprintf(path, sizeof(path), "%s/%s.unfish", token, mod_name);
            if (access(path, R_OK) == 0) {
                free(copy);
                return strdup(path);
            }
            token = strtok(NULL, ":");
        }
        free(copy);
    }

    return NULL;
}

static char* read_module_source(const char* filepath) {
    FILE* file = fopen(filepath, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    char* buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t read_bytes = fread(buffer, 1, (size_t)size, file);
    buffer[read_bytes] = '\0';
    fclose(file);
    return buffer;
}

static int find_collected_module(const UfModuleCollection* col, const char* name) {
    for (size_t i = 0; i < col->count; ++i) {
        if (strcmp(col->modules[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static bool collect_modules_from_program(UfModuleCollection* col, const UfProgram* program, const char* caller_path);

static bool collect_module_by_name(UfModuleCollection* col, const char* name, const char* caller_path) {
    if (is_builtin_module(name)) return true;
    if (find_collected_module(col, name) >= 0) return true;
    if (col->count >= UF_MAX_COMPILED_MODULES) {
        fprintf(stderr, "Error: Maximum number of compiled modules (%d) exceeded\n", UF_MAX_COMPILED_MODULES);
        return false;
    }

    char* resolved_path = resolve_module_file_path(name, caller_path);
    if (!resolved_path) {
        fprintf(stderr, "Error: Cannot resolve module '%s' imported from '%s'\n", name, caller_path ? caller_path : "<main>");
        return false;
    }

    char* source = read_module_source(resolved_path);
    if (!source) {
        fprintf(stderr, "Error: Cannot read module file '%s'\n", resolved_path);
        free(resolved_path);
        return false;
    }

    size_t idx = col->count++;
    UfCompiledModule* mod = &col->modules[idx];
    snprintf(mod->name, sizeof(mod->name), "%s", name);
    sanitize_module_name(name, mod->safe_name, sizeof(mod->safe_name));
    mod->resolved_path = resolved_path;
    mod->source_text = source;

    uf_arena_init(&mod->arena, 16384);
    uf_interner_init(&mod->interner, &mod->arena);

    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, resolved_path, source);

    UfLexer lexer;
    uf_lexer_init(&lexer, resolved_path, source, &mod->arena, &mod->interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &mod->arena, &reporter);
    mod->program = uf_parse_program(&parser);

    if (!mod->program || parser.had_error) {
        fprintf(stderr, "Error: Syntax error while parsing module '%s' (%s)\n", name, resolved_path);
        return false;
    }

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &mod->arena, &reporter);
    bool sema_ok = uf_analyze_program(&sema, mod->program);
    if (!sema_ok || reporter.error_count > 0) {
        fprintf(stderr, "Error: Semantic error in module '%s' (%s)\n", name, resolved_path);
        return false;
    }

    /* Recursively collect any modules imported by this module */
    if (!collect_modules_from_program(col, mod->program, resolved_path)) {
        return false;
    }

    return true;
}

static bool collect_modules_from_stmt(UfModuleCollection* col, const UfStmt* stmt, const char* caller_path) {
    if (!stmt) return true;
    if (stmt->kind == UF_STMT_IMPORT) {
        if (!collect_module_by_name(col, stmt->as.import_stmt.module_name, caller_path)) {
            return false;
        }
    } else if (stmt->kind == UF_STMT_FROM_IMPORT) {
        if (!collect_module_by_name(col, stmt->as.from_import_stmt.module_name, caller_path)) {
            return false;
        }
    } else if (stmt->kind == UF_STMT_IF) {
        if (!collect_modules_from_stmt(col, stmt->as.if_stmt.then_branch, caller_path)) return false;
        if (stmt->as.if_stmt.else_branch) {
            if (!collect_modules_from_stmt(col, stmt->as.if_stmt.else_branch, caller_path)) return false;
        }
    } else if (stmt->kind == UF_STMT_WHILE) {
        if (!collect_modules_from_stmt(col, stmt->as.while_stmt.body, caller_path)) return false;
    } else if (stmt->kind == UF_STMT_REPEAT) {
        if (!collect_modules_from_stmt(col, stmt->as.repeat_stmt.body, caller_path)) return false;
    } else if (stmt->kind == UF_STMT_FOR) {
        if (!collect_modules_from_stmt(col, stmt->as.for_stmt.body, caller_path)) return false;
    } else if (stmt->kind == UF_STMT_FUNCTION) {
        if (!collect_modules_from_stmt(col, stmt->as.function_stmt.body, caller_path)) return false;
    } else if (stmt->kind == UF_STMT_BLOCK) {
        for (size_t i = 0; i < stmt->as.block.count; ++i) {
            if (!collect_modules_from_stmt(col, stmt->as.block.stmts[i], caller_path)) return false;
        }
    } else if (stmt->kind == UF_STMT_TRY_CATCH) {
        if (!collect_modules_from_stmt(col, stmt->as.try_catch.try_block, caller_path)) return false;
        if (!collect_modules_from_stmt(col, stmt->as.try_catch.catch_block, caller_path)) return false;
    }
    return true;
}

static bool collect_modules_from_program(UfModuleCollection* col, const UfProgram* program, const char* caller_path) {
    if (!program) return true;
    for (size_t i = 0; i < program->count; ++i) {
        if (!collect_modules_from_stmt(col, program->stmts[i], caller_path)) {
            return false;
        }
    }
    return true;
}

static void free_module_collection(UfModuleCollection* col) {
    for (size_t i = 0; i < col->count; ++i) {
        UfCompiledModule* mod = &col->modules[i];
        uf_interner_free(&mod->interner);
        uf_arena_free(&mod->arena);
        free(mod->source_text);
        free(mod->resolved_path);
    }
    col->count = 0;
}

bool uf_emit_c_program_with_path(const UfProgram* program, const char* source_path, FILE* out) {
    if (!program || !out) return false;

    if (!source_path && program->count > 0 && program->stmts[0]->span.start.file) {
        source_path = program->stmts[0]->span.start.file;
    }

    g_match_id = 0;
    g_emit_limit_exceeded = false;

    /* UfModuleCollection and UfEmitContext are large fixed-capacity
     * structures (each UfEmitContext alone holds up to 256 UfLambdaInfo
     * entries with nested per-lambda arrays); UfModuleCollection embeds up
     * to UF_MAX_COMPILED_MODULES of them by value. Kept as plain local
     * variables this comfortably exceeds a typical 8MB thread stack, so
     * they are heap-allocated instead. */
    UfModuleCollection* col = (UfModuleCollection*)calloc(1, sizeof(UfModuleCollection));
    UfEmitContext* main_ctx = (UfEmitContext*)calloc(1, sizeof(UfEmitContext));
    char (*prefixes)[256] = (char (*)[256])malloc((size_t)UF_MAX_COMPILED_MODULES * 256);
    if (!col || !main_ctx || !prefixes) {
        fprintf(stderr, "Out of memory\n");
        free(col);
        free(main_ctx);
        free(prefixes);
        return false;
    }

    /* 1. Collect all imported modules */
    if (!collect_modules_from_program(col, program, source_path)) {
        free_module_collection(col);
        free(col);
        free(main_ctx);
        free(prefixes);
        return false;
    }

    /* 2. Initialize emission context for each module */
    for (size_t m = 0; m < col->count; ++m) {
        UfCompiledModule* mod = &col->modules[m];
        snprintf(prefixes[m], 256, "uf_m_%s_", mod->safe_name);
        init_emit_context(&mod->ctx, mod->program, prefixes[m], mod->name);
    }

    /* 3. Initialize emission context for main program */
    init_emit_context(main_ctx, program, "uf_", NULL);

    if (g_emit_limit_exceeded) {
        fprintf(stderr, "%s\n", g_emit_limit_reason);
        free_module_collection(col);
        free(col);
        free(main_ctx);
        free(prefixes);
        return false;
    }

    /* 4. Emit file header */
    fputs("/* ========================================================================= */\n", out);
    fputs("/* Generated automatically by Unfish Native C99 Compiler                   */\n", out);
    fputs("/* ========================================================================= */\n\n", out);
    fputs("#include \"unfish_runtime.h\"\n\n", out);

    /* 5. Forward declare all module initializers */
    if (col->count > 0) {
        fputs("/* Forward declarations of module initializers */\n", out);
        for (size_t m = 0; m < col->count; ++m) {
            fprintf(out, "static UfVal uf_init_mod_%s(void);\n", col->modules[m].safe_name);
        }
        fputs("\n", out);
    }

    /* 6. Emit each compiled module */
    for (size_t m = 0; m < col->count; ++m) {
        UfCompiledModule* mod = &col->modules[m];
        emit_module_unit(out, &mod->ctx, mod->program, false);
    }

    /* 7. Emit main program declarations and functions */
    emit_module_unit(out, main_ctx, program, true);

    /* 8. Emit main() */
    g_ctx = main_ctx;
    fputs("int main(int argc, char** argv) {\n", out);
    fputs("    uf_init(argc, argv);\n", out);
    fputs("    UfCatchFrame* _fn_catch_entry = g_catch_stack;\n", out);
    fputs("    (void)_fn_catch_entry;\n\n", out);

    /* Register modules */
    for (size_t m = 0; m < col->count; ++m) {
        fprintf(out, "    uf_register_module(\"%s\", uf_init_mod_%s);\n",
                col->modules[m].name, col->modules[m].safe_name);
    }

    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind != UF_STMT_FUNCTION && stmt->kind != UF_STMT_STRUCT) {
            emit_stmt(out, stmt, 1, true);
        }
    }

    fputs("\n    uf_cleanup();\n", out);
    fputs("    return 0;\n", out);
    fputs("}\n", out);

    free_module_collection(col);
    free(col);
    free(main_ctx);
    free(prefixes);
    return true;
}

bool uf_emit_c_program(const UfProgram* program, FILE* out) {
    return uf_emit_c_program_with_path(program, NULL, out);
}

bool uf_emit_c_to_file_with_path(const UfProgram* program, const char* source_path, const char* out_c_path) {
    if (!program || !out_c_path) return false;
    FILE* file = fopen(out_c_path, "w");
    if (!file) {
        fprintf(stderr, "Error: Could not open output C file '%s'\n", out_c_path);
        return false;
    }
    bool ok = uf_emit_c_program_with_path(program, source_path, file);
    fclose(file);
    return ok;
}

bool uf_emit_c_to_file(const UfProgram* program, const char* out_c_path) {
    return uf_emit_c_to_file_with_path(program, NULL, out_c_path);
}

bool uf_build_native_with_path(const UfProgram* program, const char* source_path, const char* out_bin_path) {
    if (!program || !out_bin_path) return false;

    char temp_c[256];
    snprintf(temp_c, sizeof(temp_c), "/tmp/unfish_emit_%d.c", (int)getpid());

    if (!uf_emit_c_to_file_with_path(program, source_path, temp_c)) {
        return false;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "gcc -O2 -std=c99 %s -Isrc/codegen -lm -o %s", temp_c, out_bin_path);
    int res = system(cmd);
    remove(temp_c);

    return res == 0;
}

bool uf_build_native(const UfProgram* program, const char* out_bin_path) {
    return uf_build_native_with_path(program, NULL, out_bin_path);
}
