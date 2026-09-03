#include "uf_semantic.h"
#include "../runtime/uf_stdlib.h"

#define SCOPE_BUCKETS 64

static uint32_t hash_symbol(const char* name) {
    uint32_t h = 2166136261u;
    while (*name) {
        h ^= (uint8_t)*name++;
        h *= 16777619u;
    }
    return h;
}

static UfScope* push_scope(UfSemanticAnalyzer* analyzer, bool is_function) {
    UfScope* scope = (UfScope*)uf_arena_alloc(analyzer->arena, sizeof(UfScope));
    scope->parent = analyzer->current_scope;
    scope->is_function = is_function;
    scope->bucket_count = SCOPE_BUCKETS;
    scope->buckets = (UfSymbol**)uf_arena_alloc(analyzer->arena, SCOPE_BUCKETS * sizeof(UfSymbol*));
    memset(scope->buckets, 0, SCOPE_BUCKETS * sizeof(UfSymbol*));

    analyzer->current_scope = scope;
    if (is_function) {
        analyzer->function_depth++;
    }
    return scope;
}

static void pop_scope(UfSemanticAnalyzer* analyzer) {
    if (analyzer->current_scope) {
        if (analyzer->current_scope->is_function) {
            analyzer->function_depth--;
        }
        analyzer->current_scope = analyzer->current_scope->parent;
    }
}

static UfSymbol* find_symbol_in_scope(const UfScope* scope, const char* name) {
    uint32_t h = hash_symbol(name);
    size_t idx = h & (scope->bucket_count - 1);
    UfSymbol* sym = scope->buckets[idx];
    while (sym) {
        if (strcmp(sym->name, name) == 0) {
            return sym;
        }
        sym = sym->next;
    }
    return NULL;
}

static UfSymbol* resolve_symbol(const UfScope* start_scope, const char* name) {
    const UfScope* scope = start_scope;
    while (scope) {
        UfSymbol* sym = find_symbol_in_scope(scope, name);
        if (sym) return sym;
        scope = scope->parent;
    }
    return NULL;
}

void uf_semantic_add_symbol(UfSemanticAnalyzer* analyzer, const char* name, UfSymbolKind kind, SourceSpan span, int arity) {
    UfScope* scope = analyzer->current_scope;
    uint32_t h = hash_symbol(name);
    size_t idx = h & (scope->bucket_count - 1);

    UfSymbol* sym = (UfSymbol*)uf_arena_alloc(analyzer->arena, sizeof(UfSymbol));
    sym->name = name;
    sym->kind = kind;
    sym->span = span;
    sym->arity = arity;
    sym->next = scope->buckets[idx];
    scope->buckets[idx] = sym;
}

static void add_symbol(UfSemanticAnalyzer* analyzer, const char* name, UfSymbolKind kind, SourceSpan span, int arity) {
    uf_semantic_add_symbol(analyzer, name, kind, span, arity);
}

/* Levenshtein distance for helpful "Did you mean?" suggestions */
static int min3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
}

static int levenshtein(const char* s1, const char* s2) {
    int len1 = (int)strlen(s1);
    int len2 = (int)strlen(s2);
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;

    int d[64][64];
    if (len1 >= 64) len1 = 63;
    if (len2 >= 64) len2 = 63;

    for (int i = 0; i <= len1; i++) d[i][0] = i;
    for (int j = 0; j <= len2; j++) d[0][j] = j;

    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            d[i][j] = min3(d[i - 1][j] + 1,
                           d[i][j - 1] + 1,
                           d[i - 1][j - 1] + cost);
        }
    }
    return d[len1][len2];
}

static const char* find_closest_symbol(const UfScope* start_scope, const char* name) {
    const char* best_match = NULL;
    int best_dist = 4; /* Maximum edit distance considered */

    for (const UfScope* s = start_scope; s != NULL; s = s->parent) {
        for (size_t i = 0; i < s->bucket_count; ++i) {
            for (UfSymbol* sym = s->buckets[i]; sym != NULL; sym = sym->next) {
                int dist = levenshtein(name, sym->name);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_match = sym->name;
                }
            }
        }
    }
    return best_match;
}

static void register_builtins(UfSemanticAnalyzer* analyzer) {
    SourceLoc loc = source_loc_make("<builtin>", 0, 0, 0);
    SourceSpan span = source_span_make(loc, loc);

    add_symbol(analyzer, "say",     UF_SYM_BUILTIN, span, 1);
    add_symbol(analyzer, "print",   UF_SYM_BUILTIN, span, 1);
    add_symbol(analyzer, "type_of", UF_SYM_BUILTIN, span, 1);
    add_symbol(analyzer, "len",     UF_SYM_BUILTIN, span, 1);
    add_symbol(analyzer, "push",    UF_SYM_BUILTIN, span, 2);
    add_symbol(analyzer, "pop",     UF_SYM_BUILTIN, span, 1);
    add_symbol(analyzer, "range",   UF_SYM_BUILTIN, span, -1);
    add_symbol(analyzer, "keys",    UF_SYM_BUILTIN, span, 1);
    add_symbol(analyzer, "values",  UF_SYM_BUILTIN, span, 1);
    add_symbol(analyzer, "has_key", UF_SYM_BUILTIN, span, 2);
    add_symbol(analyzer, "delete",  UF_SYM_BUILTIN, span, 2);
    add_symbol(analyzer, "map",     UF_SYM_BUILTIN, span, 2);
    add_symbol(analyzer, "filter",  UF_SYM_BUILTIN, span, 2);
    add_symbol(analyzer, "reduce",  UF_SYM_BUILTIN, span, -1);
    add_symbol(analyzer, "sort",    UF_SYM_BUILTIN, span, -1);
    add_symbol(analyzer, "reverse", UF_SYM_BUILTIN, span, 1);
    add_symbol(analyzer, "find",    UF_SYM_BUILTIN, span, 2);
    add_symbol(analyzer, "every",   UF_SYM_BUILTIN, span, 2);
    add_symbol(analyzer, "some",    UF_SYM_BUILTIN, span, 2);
    add_symbol(analyzer, "clock",   UF_SYM_BUILTIN, span, 0);
    add_symbol(analyzer, "assert",  UF_SYM_BUILTIN, span, -1);
}

void uf_semantic_init(UfSemanticAnalyzer* analyzer,
                      UfArena* arena,
                      UfDiagnosticReporter* reporter) {
    analyzer->arena = arena;
    analyzer->reporter = reporter;
    analyzer->current_scope = NULL;
    analyzer->global_scope = NULL;
    analyzer->function_depth = 0;
    analyzer->loop_depth = 0;
    analyzer->had_error = false;

    /* Create global scope and register built-in symbols */
    analyzer->global_scope = push_scope(analyzer, false);
    register_builtins(analyzer);
    uf_stdlib_register_semantic(analyzer);
}

static void analyze_expr(UfSemanticAnalyzer* analyzer, UfExpr* expr);
static void analyze_stmt(UfSemanticAnalyzer* analyzer, UfStmt* stmt);

static void analyze_expr(UfSemanticAnalyzer* analyzer, UfExpr* expr) {
    if (!expr) return;

    switch (expr->kind) {
        case UF_EXPR_LITERAL_NULL:
        case UF_EXPR_LITERAL_BOOL:
        case UF_EXPR_LITERAL_NUMBER:
        case UF_EXPR_LITERAL_STRING:
            break;

        case UF_EXPR_IDENTIFIER: {
            const char* name = expr->as.identifier_name;
            UfSymbol* sym = resolve_symbol(analyzer->current_scope, name);
            if (!sym) {
                analyzer->had_error = true;
                const char* closest = find_closest_symbol(analyzer->current_scope, name);
                char hint_buf[256] = {0};
                if (closest) {
                    snprintf(hint_buf, sizeof(hint_buf), "Did you mean '%s'?", closest);
                } else {
                    snprintf(hint_buf, sizeof(hint_buf), "Ensure '%s' is declared with 'let %s = ...' before use", name, name);
                }
                char msg[256];
                snprintf(msg, sizeof(msg), "'%s' has not been defined", name);
                uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, expr->span, msg, hint_buf);
            }
            break;
        }

        case UF_EXPR_UNARY:
            analyze_expr(analyzer, expr->as.unary.operand);
            break;

        case UF_EXPR_BINARY:
            analyze_expr(analyzer, expr->as.binary.left);
            analyze_expr(analyzer, expr->as.binary.right);
            break;

        case UF_EXPR_CALL: {
            analyze_expr(analyzer, expr->as.call.callee);
            for (size_t i = 0; i < expr->as.call.argc; ++i) {
                analyze_expr(analyzer, expr->as.call.args[i]);
            }

            /* Arity check if callee is a direct identifier */
            if (expr->as.call.callee->kind == UF_EXPR_IDENTIFIER) {
                const char* fn_name = expr->as.call.callee->as.identifier_name;
                UfSymbol* sym = resolve_symbol(analyzer->current_scope, fn_name);
                if (sym && (sym->kind == UF_SYM_FUNCTION || sym->kind == UF_SYM_BUILTIN)) {
                    if (sym->arity >= 0 && (int)expr->as.call.argc != sym->arity) {
                        analyzer->had_error = true;
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Function '%s' expects %d argument%s, but %zu %s provided",
                                 fn_name, sym->arity, sym->arity == 1 ? "" : "s",
                                 expr->as.call.argc, expr->as.call.argc == 1 ? "was" : "were");
                        uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, expr->span, msg, NULL);
                    }
                }
            }
            break;
        }

        case UF_EXPR_GROUPING:
            analyze_expr(analyzer, expr->as.grouping.inner);
            break;

        case UF_EXPR_ARRAY:
            for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                analyze_expr(analyzer, expr->as.array_lit.elements[i]);
            }
            break;

        case UF_EXPR_INDEX:
            analyze_expr(analyzer, expr->as.index_expr.target);
            analyze_expr(analyzer, expr->as.index_expr.index);
            break;

        case UF_EXPR_MAP:
            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                analyze_expr(analyzer, expr->as.map_lit.keys[i]);
                analyze_expr(analyzer, expr->as.map_lit.values[i]);
            }
            break;

        case UF_EXPR_FUNCTION: {
            const char* fn_name = expr->as.fn_expr.name;
            if (fn_name) {
                add_symbol(analyzer, fn_name, UF_SYM_FUNCTION, expr->span, (int)expr->as.fn_expr.param_count);
            }

            push_scope(analyzer, true);

            for (size_t i = 0; i < expr->as.fn_expr.param_count; ++i) {
                const char* param = expr->as.fn_expr.params[i];
                UfSymbol* existing = find_symbol_in_scope(analyzer->current_scope, param);
                if (existing) {
                    analyzer->had_error = true;
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Duplicate parameter name '%s' in function", param);
                    uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, expr->span, msg, NULL);
                } else {
                    add_symbol(analyzer, param, UF_SYM_VAR, expr->span, -1);
                }
            }

            analyze_stmt(analyzer, expr->as.fn_expr.body);
            pop_scope(analyzer);
            break;
        }
    }
}

static void analyze_stmt(UfSemanticAnalyzer* analyzer, UfStmt* stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
        case UF_STMT_LET: {
            const char* name = stmt->as.let_stmt.name;
            /* Check if duplicate declaration in current scope */
            UfSymbol* existing = find_symbol_in_scope(analyzer->current_scope, name);
            if (existing) {
                analyzer->had_error = true;
                char msg[256];
                snprintf(msg, sizeof(msg), "Identifier '%s' has already been declared in this scope", name);
                uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, stmt->span, msg, "Use assignment without 'let' to modify an existing variable");
            }

            if (stmt->as.let_stmt.init) {
                analyze_expr(analyzer, stmt->as.let_stmt.init);
            }

            add_symbol(analyzer, name, UF_SYM_VAR, stmt->span, -1);
            break;
        }

        case UF_STMT_ASSIGN: {
            const char* name = stmt->as.assign_stmt.name;
            UfSymbol* sym = resolve_symbol(analyzer->current_scope, name);
            if (!sym) {
                analyzer->had_error = true;
                const char* closest = find_closest_symbol(analyzer->current_scope, name);
                char hint_buf[256] = {0};
                if (closest) {
                    snprintf(hint_buf, sizeof(hint_buf), "Did you mean '%s'?", closest);
                } else {
                    snprintf(hint_buf, sizeof(hint_buf), "Declare '%s' with 'let %s = ...' before assigning to it", name, name);
                }
                char msg[256];
                snprintf(msg, sizeof(msg), "Cannot assign to undefined identifier '%s'", name);
                uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, stmt->span, msg, hint_buf);
            }
            analyze_expr(analyzer, stmt->as.assign_stmt.value);
            break;
        }

        case UF_STMT_INDEX_ASSIGN:
            analyze_expr(analyzer, stmt->as.index_assign.target);
            analyze_expr(analyzer, stmt->as.index_assign.index);
            analyze_expr(analyzer, stmt->as.index_assign.value);
            break;

        case UF_STMT_SAY:
            analyze_expr(analyzer, stmt->as.say_stmt.expr);
            break;

        case UF_STMT_EXPR:
            analyze_expr(analyzer, stmt->as.expr_stmt.expr);
            break;

        case UF_STMT_IF:
            analyze_expr(analyzer, stmt->as.if_stmt.condition);
            push_scope(analyzer, false);
            analyze_stmt(analyzer, stmt->as.if_stmt.then_branch);
            pop_scope(analyzer);
            if (stmt->as.if_stmt.else_branch) {
                if (stmt->as.if_stmt.else_branch->kind == UF_STMT_IF) {
                    analyze_stmt(analyzer, stmt->as.if_stmt.else_branch);
                } else {
                    push_scope(analyzer, false);
                    analyze_stmt(analyzer, stmt->as.if_stmt.else_branch);
                    pop_scope(analyzer);
                }
            }
            break;

        case UF_STMT_WHILE:
            analyze_expr(analyzer, stmt->as.while_stmt.condition);
            push_scope(analyzer, false);
            analyzer->loop_depth++;
            analyze_stmt(analyzer, stmt->as.while_stmt.body);
            analyzer->loop_depth--;
            pop_scope(analyzer);
            break;

        case UF_STMT_REPEAT:
            analyze_expr(analyzer, stmt->as.repeat_stmt.count_expr);
            push_scope(analyzer, false);
            analyzer->loop_depth++;
            analyze_stmt(analyzer, stmt->as.repeat_stmt.body);
            analyzer->loop_depth--;
            pop_scope(analyzer);
            break;

        case UF_STMT_FOR:
            analyze_expr(analyzer, stmt->as.for_stmt.iterable);
            push_scope(analyzer, false);
            add_symbol(analyzer, stmt->as.for_stmt.var_name, UF_SYM_VAR, stmt->span, -1);
            analyzer->loop_depth++;
            analyze_stmt(analyzer, stmt->as.for_stmt.body);
            analyzer->loop_depth--;
            pop_scope(analyzer);
            break;

        case UF_STMT_BREAK:
            if (analyzer->loop_depth == 0) {
                analyzer->had_error = true;
                uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, stmt->span,
                               "'break' outside of loop",
                               "'break' can only be used inside a 'while', 'repeat', or 'for' loop");
            }
            break;

        case UF_STMT_CONTINUE:
            if (analyzer->loop_depth == 0) {
                analyzer->had_error = true;
                uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, stmt->span,
                               "'continue' outside of loop",
                               "'continue' can only be used inside a 'while', 'repeat', or 'for' loop");
            }
            break;

        case UF_STMT_FUNCTION: {
            const char* fn_name = stmt->as.function_stmt.name;
            /* Register function in enclosing scope first to permit recursion */
            add_symbol(analyzer, fn_name, UF_SYM_FUNCTION, stmt->span, (int)stmt->as.function_stmt.param_count);

            /* Push function scope */
            push_scope(analyzer, true);

            /* Check parameter uniqueness */
            for (size_t i = 0; i < stmt->as.function_stmt.param_count; ++i) {
                const char* param = stmt->as.function_stmt.params[i];
                UfSymbol* existing = find_symbol_in_scope(analyzer->current_scope, param);
                if (existing) {
                    analyzer->had_error = true;
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Duplicate parameter name '%s' in function '%s'", param, fn_name);
                    uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, stmt->span, msg, NULL);
                } else {
                    add_symbol(analyzer, param, UF_SYM_VAR, stmt->span, -1);
                }
            }

            analyze_stmt(analyzer, stmt->as.function_stmt.body);
            pop_scope(analyzer);
            break;
        }

        case UF_STMT_RETURN:
            if (analyzer->function_depth == 0) {
                analyzer->had_error = true;
                uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, stmt->span,
                               "'return' statement outside of function",
                               "'return' can only be used inside a function body");
            }
            if (stmt->as.return_stmt.value) {
                analyze_expr(analyzer, stmt->as.return_stmt.value);
            }
            break;

        case UF_STMT_BLOCK:
            for (size_t i = 0; i < stmt->as.block.count; ++i) {
                analyze_stmt(analyzer, stmt->as.block.stmts[i]);
            }
            break;

        case UF_STMT_TRY_CATCH:
            analyze_stmt(analyzer, stmt->as.try_catch.try_block);
            push_scope(analyzer, false);
            if (stmt->as.try_catch.catch_var) {
                add_symbol(analyzer, stmt->as.try_catch.catch_var, UF_SYM_VAR, stmt->span, -1);
            }
            analyze_stmt(analyzer, stmt->as.try_catch.catch_block);
            pop_scope(analyzer);
            break;

        case UF_STMT_IMPORT: {
            const char* bound_name = stmt->as.import_stmt.alias ? stmt->as.import_stmt.alias : stmt->as.import_stmt.module_name;
            add_symbol(analyzer, bound_name, UF_SYM_VAR, stmt->span, -1);
            break;
        }

        case UF_STMT_FROM_IMPORT: {
            for (size_t i = 0; i < stmt->as.from_import_stmt.count; ++i) {
                const char* bound_name = (stmt->as.from_import_stmt.aliases && stmt->as.from_import_stmt.aliases[i])
                                         ? stmt->as.from_import_stmt.aliases[i]
                                         : stmt->as.from_import_stmt.symbols[i];
                add_symbol(analyzer, bound_name, UF_SYM_VAR, stmt->span, -1);
            }
            break;
        }
    }
}

bool uf_analyze_program(UfSemanticAnalyzer* analyzer, UfProgram* program) {
    /* Pass 1: Hoist top-level function declarations */
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_FUNCTION) {
            add_symbol(analyzer, stmt->as.function_stmt.name, UF_SYM_FUNCTION, stmt->span, (int)stmt->as.function_stmt.param_count);
        }
    }

    /* Pass 2: Analyze all statements */
    for (size_t i = 0; i < program->count; ++i) {
        analyze_stmt(analyzer, program->stmts[i]);
    }

    return !analyzer->had_error;
}
