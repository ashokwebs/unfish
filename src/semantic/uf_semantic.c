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

void uf_semantic_add_symbol_with_type(UfSemanticAnalyzer* analyzer,
                                      const char* name,
                                      UfSymbolKind kind,
                                      SourceSpan span,
                                      int arity,
                                      const char* type_annotation,
                                      const char* return_type,
                                      const char** param_types) {
    UfScope* scope = analyzer->current_scope;
    uint32_t h = hash_symbol(name);
    size_t idx = h & (scope->bucket_count - 1);

    UfSymbol* sym = (UfSymbol*)uf_arena_alloc(analyzer->arena, sizeof(UfSymbol));
    sym->name = name;
    sym->kind = kind;
    sym->span = span;
    sym->arity = arity;
    sym->type_annotation = type_annotation;
    sym->return_type = return_type;
    sym->param_types = param_types;
    sym->next = scope->buckets[idx];
    scope->buckets[idx] = sym;
}

void uf_semantic_add_symbol(UfSemanticAnalyzer* analyzer, const char* name, UfSymbolKind kind, SourceSpan span, int arity) {
    uf_semantic_add_symbol_with_type(analyzer, name, kind, span, arity, NULL, NULL, NULL);
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
    analyzer->strict_mode = false;
    analyzer->current_fn_return_type = NULL;

    /* Create global scope and register built-in symbols */
    analyzer->global_scope = push_scope(analyzer, false);
    register_builtins(analyzer);
    uf_stdlib_register_semantic(analyzer);
}

static bool is_valid_type_name(UfSemanticAnalyzer* analyzer, const char* name) {
    if (!name) return false;
    if (strcmp(name, "Number") == 0 ||
        strcmp(name, "String") == 0 ||
        strcmp(name, "Boolean") == 0 ||
        strcmp(name, "Array") == 0 ||
        strcmp(name, "Map") == 0 ||
        strcmp(name, "Function") == 0 ||
        strcmp(name, "Null") == 0 ||
        strcmp(name, "Any") == 0 ||
        strcmp(name, "Error") == 0) {
        return true;
    }
    UfSymbol* sym = resolve_symbol(analyzer->current_scope, name);
    if (sym && sym->kind == UF_SYM_STRUCT) {
        return true;
    }
    return false;
}

static bool types_compatible(const char* expected, const char* actual) {
    if (!expected || !actual) return true;
    if (strcmp(expected, "Any") == 0 || strcmp(actual, "Any") == 0) return true;
    return strcmp(expected, actual) == 0;
}

static void report_type_mismatch(UfSemanticAnalyzer* analyzer, SourceSpan span, const char* msg) {
    if (analyzer->strict_mode) {
        analyzer->had_error = true;
        uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, span, msg, NULL);
    } else {
        uf_report_diag(analyzer->reporter, UF_DIAG_WARNING, span, msg, NULL);
    }
}

static const char* infer_expr_type(UfSemanticAnalyzer* analyzer, const UfExpr* expr);
static void analyze_expr(UfSemanticAnalyzer* analyzer, UfExpr* expr);
static void analyze_stmt(UfSemanticAnalyzer* analyzer, UfStmt* stmt);

static const char* infer_expr_type(UfSemanticAnalyzer* analyzer, const UfExpr* expr) {
    if (!expr) return "Any";

    switch (expr->kind) {
        case UF_EXPR_LITERAL_NULL:   return "Null";
        case UF_EXPR_LITERAL_BOOL:   return "Boolean";
        case UF_EXPR_LITERAL_NUMBER: return "Number";
        case UF_EXPR_LITERAL_STRING: return "String";
        case UF_EXPR_ARRAY:          return "Array";
        case UF_EXPR_MAP:            return "Map";
        case UF_EXPR_FUNCTION:       return "Function";
        case UF_EXPR_GROUPING:
            return infer_expr_type(analyzer, expr->as.grouping.inner);

        case UF_EXPR_IDENTIFIER: {
            UfSymbol* sym = resolve_symbol(analyzer->current_scope, expr->as.identifier_name);
            if (sym && sym->type_annotation) {
                return sym->type_annotation;
            }
            return "Any";
        }

        case UF_EXPR_UNARY:
            if (expr->as.unary.op == UF_TOK_MINUS) {
                return "Number";
            }
            if (expr->as.unary.op == UF_TOK_NOT) {
                return "Boolean";
            }
            return "Any";

        case UF_EXPR_BINARY: {
            switch (expr->as.binary.op) {
                case UF_TOK_PLUS: {
                    const char* lt = infer_expr_type(analyzer, expr->as.binary.left);
                    const char* rt = infer_expr_type(analyzer, expr->as.binary.right);
                    if (strcmp(lt, "String") == 0 || strcmp(rt, "String") == 0) {
                        return "String";
                    }
                    if (strcmp(lt, "Number") == 0 && strcmp(rt, "Number") == 0) {
                        return "Number";
                    }
                    return "Any";
                }
                case UF_TOK_MINUS:
                case UF_TOK_STAR:
                case UF_TOK_SLASH:
                case UF_TOK_PERCENT:
                    return "Number";

                case UF_TOK_EQEQ:
                case UF_TOK_BANGEQ:
                case UF_TOK_LT:
                case UF_TOK_LTEQ:
                case UF_TOK_GT:
                case UF_TOK_GTEQ:
                    return "Boolean";

                case UF_TOK_AND:
                case UF_TOK_OR: {
                    const char* lt = infer_expr_type(analyzer, expr->as.binary.left);
                    const char* rt = infer_expr_type(analyzer, expr->as.binary.right);
                    if (strcmp(lt, rt) == 0) return lt;
                    return "Any";
                }

                default:
                    return "Any";
            }
        }

        case UF_EXPR_CALL: {
            if (expr->as.call.callee->kind == UF_EXPR_IDENTIFIER) {
                const char* name = expr->as.call.callee->as.identifier_name;
                UfSymbol* sym = resolve_symbol(analyzer->current_scope, name);
                if (sym) {
                    if (sym->kind == UF_SYM_STRUCT) {
                        return sym->name;
                    }
                    if (sym->return_type) {
                        return sym->return_type;
                    }
                }
                if (strcmp(name, "len") == 0 || strcmp(name, "index_of") == 0 ||
                    strcmp(name, "to_number") == 0 || strcmp(name, "abs") == 0 ||
                    strcmp(name, "floor") == 0 || strcmp(name, "ceil") == 0 ||
                    strcmp(name, "round") == 0 || strcmp(name, "sqrt") == 0 ||
                    strcmp(name, "pow") == 0 || strcmp(name, "min") == 0 ||
                    strcmp(name, "max") == 0 || strcmp(name, "log") == 0 ||
                    strcmp(name, "sin") == 0 || strcmp(name, "cos") == 0 ||
                    strcmp(name, "tan") == 0 || strcmp(name, "random") == 0 ||
                    strcmp(name, "random_int") == 0) {
                    return "Number";
                }
                if (strcmp(name, "to_string") == 0 || strcmp(name, "to_upper") == 0 ||
                    strcmp(name, "to_lower") == 0 || strcmp(name, "trim") == 0 ||
                    strcmp(name, "replace") == 0 || strcmp(name, "char_at") == 0 ||
                    strcmp(name, "repeat_string") == 0 || strcmp(name, "substring") == 0 ||
                    strcmp(name, "type_of") == 0) {
                    return "String";
                }
                if (strcmp(name, "has_key") == 0 || strcmp(name, "contains") == 0 ||
                    strcmp(name, "starts_with") == 0 || strcmp(name, "ends_with") == 0 ||
                    strcmp(name, "every") == 0 || strcmp(name, "some") == 0) {
                    return "Boolean";
                }
                if (strcmp(name, "split") == 0 || strcmp(name, "keys") == 0 ||
                    strcmp(name, "values") == 0 || strcmp(name, "map") == 0 ||
                    strcmp(name, "filter") == 0 || strcmp(name, "sort") == 0 ||
                    strcmp(name, "reverse") == 0) {
                    return "Array";
                }
            }
            return "Any";
        }

        case UF_EXPR_INDEX: {
            if (expr->as.index_expr.index->kind == UF_EXPR_LITERAL_STRING) {
                const char* prop = expr->as.index_expr.index->as.string_val;
                if (strcmp(prop, "message") == 0 || strcmp(prop, "kind") == 0 || strcmp(prop, "file") == 0) {
                    return "String";
                }
                if (strcmp(prop, "line") == 0) {
                    return "Number";
                }
            }
            return "Any";
        }

        default:
            return "Any";
    }
}

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

            /* Arity and type check if callee is a direct identifier */
            if (expr->as.call.callee->kind == UF_EXPR_IDENTIFIER) {
                const char* fn_name = expr->as.call.callee->as.identifier_name;
                UfSymbol* sym = resolve_symbol(analyzer->current_scope, fn_name);
                if (sym && (sym->kind == UF_SYM_FUNCTION || sym->kind == UF_SYM_BUILTIN || sym->kind == UF_SYM_STRUCT)) {
                    if (sym->arity >= 0 && (int)expr->as.call.argc != sym->arity) {
                        analyzer->had_error = true;
                        char msg[256];
                        if (sym->kind == UF_SYM_STRUCT) {
                            snprintf(msg, sizeof(msg), "Struct '%s' constructor expects %d argument%s, but %zu %s provided",
                                     fn_name, sym->arity, sym->arity == 1 ? "" : "s",
                                     expr->as.call.argc, expr->as.call.argc == 1 ? "was" : "were");
                        } else {
                            snprintf(msg, sizeof(msg), "Function '%s' expects %d argument%s, but %zu %s provided",
                                     fn_name, sym->arity, sym->arity == 1 ? "" : "s",
                                     expr->as.call.argc, expr->as.call.argc == 1 ? "was" : "were");
                        }
                        uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, expr->span, msg, NULL);
                    } else if (sym->param_types) {
                        for (size_t i = 0; i < expr->as.call.argc && i < (size_t)sym->arity; ++i) {
                            const char* expected_pt = sym->param_types[i];
                            if (expected_pt && is_valid_type_name(analyzer, expected_pt)) {
                                const char* arg_type = infer_expr_type(analyzer, expr->as.call.args[i]);
                                if (!types_compatible(expected_pt, arg_type)) {
                                    char msg[256];
                                    snprintf(msg, sizeof(msg), "Type mismatch in argument %zu of call to '%s': expected '%s', got '%s'",
                                             i + 1, fn_name, expected_pt, arg_type);
                                    report_type_mismatch(analyzer, expr->as.call.args[i]->span, msg);
                                }
                            }
                        }
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
            const char* return_type = expr->as.fn_expr.return_type;
            const char** param_types = expr->as.fn_expr.param_types;

            if (return_type && !is_valid_type_name(analyzer, return_type)) {
                analyzer->had_error = true;
                char msg[256];
                snprintf(msg, sizeof(msg), "Unknown return type '%s' in function expression", return_type);
                uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, expr->span, msg,
                               "Valid types include Number, String, Boolean, Array, Map, Function, Null, Any, Error");
            }

            if (param_types) {
                for (size_t i = 0; i < expr->as.fn_expr.param_count; ++i) {
                    if (param_types[i] && !is_valid_type_name(analyzer, param_types[i])) {
                        analyzer->had_error = true;
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Unknown type '%s' for parameter '%s' in function expression",
                                 param_types[i], expr->as.fn_expr.params[i]);
                        uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, expr->span, msg,
                                       "Valid types include Number, String, Boolean, Array, Map, Function, Null, Any, Error");
                    }
                }
            }

            if (fn_name) {
                uf_semantic_add_symbol_with_type(analyzer, fn_name, UF_SYM_FUNCTION, expr->span,
                                                (int)expr->as.fn_expr.param_count, "Function", return_type, param_types);
            }

            push_scope(analyzer, true);
            const char* prev_fn_return_type = analyzer->current_fn_return_type;
            analyzer->current_fn_return_type = return_type;

            for (size_t i = 0; i < expr->as.fn_expr.param_count; ++i) {
                const char* param = expr->as.fn_expr.params[i];
                const char* ptype = param_types ? param_types[i] : NULL;
                UfSymbol* existing = find_symbol_in_scope(analyzer->current_scope, param);
                if (existing) {
                    analyzer->had_error = true;
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Duplicate parameter name '%s' in function", param);
                    uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, expr->span, msg, NULL);
                } else {
                    uf_semantic_add_symbol_with_type(analyzer, param, UF_SYM_VAR, expr->span, -1, ptype, NULL, NULL);
                }
            }

            analyze_stmt(analyzer, expr->as.fn_expr.body);
            analyzer->current_fn_return_type = prev_fn_return_type;
            pop_scope(analyzer);
            break;
        }
    }
}

static void bind_pattern_variables(UfSemanticAnalyzer* analyzer, UfPattern* pat) {
    if (!pat) return;
    switch (pat->kind) {
        case UF_PAT_LITERAL:
            analyze_expr(analyzer, pat->as.literal);
            break;
        case UF_PAT_WILDCARD:
            break;
        case UF_PAT_VARIABLE:
            add_symbol(analyzer, pat->as.var_name, UF_SYM_VAR, pat->span, -1);
            break;
        case UF_PAT_STRUCT: {
            const char* sname = pat->as.struct_pat.struct_name;
            UfSymbol* s = resolve_symbol(analyzer->current_scope, sname);
            if (!s || s->kind != UF_SYM_STRUCT) {
                analyzer->had_error = true;
                char msg[256];
                snprintf(msg, sizeof(msg), "Unknown struct '%s' in pattern", sname);
                uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, pat->span, msg, NULL);
            } else if (s->arity >= 0 && (int)pat->as.struct_pat.field_count != s->arity) {
                analyzer->had_error = true;
                char msg[256];
                snprintf(msg, sizeof(msg), "Struct '%s' pattern expects %d field%s, but %zu provided",
                         s->name, s->arity, s->arity == 1 ? "" : "s", pat->as.struct_pat.field_count);
                uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, pat->span, msg, NULL);
            }
            for (size_t i = 0; i < pat->as.struct_pat.field_count; ++i) {
                bind_pattern_variables(analyzer, pat->as.struct_pat.field_patterns[i]);
            }
            break;
        }
    }
}

static void analyze_stmt(UfSemanticAnalyzer* analyzer, UfStmt* stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
        case UF_STMT_LET: {
            const char* name = stmt->as.let_stmt.name;
            const char* type_annot = stmt->as.let_stmt.type_annotation;

            /* Check if duplicate declaration in current scope */
            UfSymbol* existing = find_symbol_in_scope(analyzer->current_scope, name);
            if (existing) {
                analyzer->had_error = true;
                char msg[256];
                snprintf(msg, sizeof(msg), "Identifier '%s' has already been declared in this scope", name);
                uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, stmt->span, msg, "Use assignment without 'let' to modify an existing variable");
            }

            if (type_annot && !is_valid_type_name(analyzer, type_annot)) {
                analyzer->had_error = true;
                char msg[256];
                snprintf(msg, sizeof(msg), "Unknown type '%s' in type annotation", type_annot);
                uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, stmt->span, msg,
                               "Valid types include Number, String, Boolean, Array, Map, Function, Null, Any, Error");
            }

            if (stmt->as.let_stmt.init) {
                analyze_expr(analyzer, stmt->as.let_stmt.init);

                if (type_annot && is_valid_type_name(analyzer, type_annot)) {
                    const char* init_type = infer_expr_type(analyzer, stmt->as.let_stmt.init);
                    if (!types_compatible(type_annot, init_type)) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Type mismatch in variable declaration: expected '%s', got '%s'", type_annot, init_type);
                        report_type_mismatch(analyzer, stmt->span, msg);
                    }
                }
            }

            uf_semantic_add_symbol_with_type(analyzer, name, UF_SYM_VAR, stmt->span, -1, type_annot, NULL, NULL);
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
            } else if (sym->type_annotation && is_valid_type_name(analyzer, sym->type_annotation)) {
                analyze_expr(analyzer, stmt->as.assign_stmt.value);
                const char* val_type = infer_expr_type(analyzer, stmt->as.assign_stmt.value);
                if (!types_compatible(sym->type_annotation, val_type)) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Type mismatch in assignment: expected '%s', got '%s'", sym->type_annotation, val_type);
                    report_type_mismatch(analyzer, stmt->span, msg);
                }
                break;
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
            const char* return_type = stmt->as.function_stmt.return_type;
            const char** param_types = stmt->as.function_stmt.param_types;

            /* Check return type validity */
            if (return_type && !is_valid_type_name(analyzer, return_type)) {
                analyzer->had_error = true;
                char msg[256];
                snprintf(msg, sizeof(msg), "Unknown return type '%s' in function '%s'", return_type, fn_name);
                uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, stmt->span, msg,
                               "Valid types include Number, String, Boolean, Array, Map, Function, Null, Any, Error");
            }

            /* Check param types validity */
            if (param_types) {
                for (size_t i = 0; i < stmt->as.function_stmt.param_count; ++i) {
                    if (param_types[i] && !is_valid_type_name(analyzer, param_types[i])) {
                        analyzer->had_error = true;
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Unknown type '%s' for parameter '%s' in function '%s'",
                                 param_types[i], stmt->as.function_stmt.params[i], fn_name);
                        uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, stmt->span, msg,
                                       "Valid types include Number, String, Boolean, Array, Map, Function, Null, Any, Error");
                    }
                }
            }

            /* Register function in enclosing scope first to permit recursion */
            uf_semantic_add_symbol_with_type(analyzer, fn_name, UF_SYM_FUNCTION, stmt->span,
                                            (int)stmt->as.function_stmt.param_count, "Function", return_type, param_types);

            /* Push function scope */
            push_scope(analyzer, true);
            const char* prev_fn_return_type = analyzer->current_fn_return_type;
            analyzer->current_fn_return_type = return_type;

            /* Check parameter uniqueness */
            for (size_t i = 0; i < stmt->as.function_stmt.param_count; ++i) {
                const char* param = stmt->as.function_stmt.params[i];
                const char* ptype = param_types ? param_types[i] : NULL;
                UfSymbol* existing = find_symbol_in_scope(analyzer->current_scope, param);
                if (existing) {
                    analyzer->had_error = true;
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Duplicate parameter name '%s' in function '%s'", param, fn_name);
                    uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, stmt->span, msg, NULL);
                } else {
                    uf_semantic_add_symbol_with_type(analyzer, param, UF_SYM_VAR, stmt->span, -1, ptype, NULL, NULL);
                }
            }

            analyze_stmt(analyzer, stmt->as.function_stmt.body);
            analyzer->current_fn_return_type = prev_fn_return_type;
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
            if (analyzer->current_fn_return_type && is_valid_type_name(analyzer, analyzer->current_fn_return_type)) {
                const char* val_type = stmt->as.return_stmt.value ? infer_expr_type(analyzer, stmt->as.return_stmt.value) : "Null";
                if (!types_compatible(analyzer->current_fn_return_type, val_type)) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Type mismatch in return statement: expected '%s', got '%s'",
                             analyzer->current_fn_return_type, val_type);
                    report_type_mismatch(analyzer, stmt->span, msg);
                }
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

        case UF_STMT_STRUCT: {
            const char* name = stmt->as.struct_stmt.name;
            if (stmt->as.struct_stmt.field_types) {
                for (size_t i = 0; i < stmt->as.struct_stmt.field_count; ++i) {
                    const char* ftype = stmt->as.struct_stmt.field_types[i];
                    if (ftype && !is_valid_type_name(analyzer, ftype)) {
                        analyzer->had_error = true;
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Unknown type '%s' for field '%s' in struct '%s'",
                                 ftype, stmt->as.struct_stmt.field_names[i], name);
                        uf_report_diag(analyzer->reporter, UF_DIAG_SEMANTIC_ERROR, stmt->span, msg,
                                       "Valid types include Number, String, Boolean, Array, Map, Function, Null, Any, Error, and declared structs");
                    }
                }
            }
            uf_semantic_add_symbol_with_type(analyzer, name, UF_SYM_STRUCT, stmt->span,
                                            (int)stmt->as.struct_stmt.field_count, name, name,
                                            stmt->as.struct_stmt.field_types);
            break;
        }

        case UF_STMT_MATCH: {
            analyze_expr(analyzer, stmt->as.match_stmt.expr);
            for (size_t i = 0; i < stmt->as.match_stmt.arm_count; ++i) {
                push_scope(analyzer, false);
                bind_pattern_variables(analyzer, stmt->as.match_stmt.arms[i].pattern);
                if (stmt->as.match_stmt.arms[i].guard) {
                    analyze_expr(analyzer, stmt->as.match_stmt.arms[i].guard);
                }
                analyze_stmt(analyzer, stmt->as.match_stmt.arms[i].body);
                pop_scope(analyzer);
            }
            if (stmt->as.match_stmt.else_branch) {
                analyze_stmt(analyzer, stmt->as.match_stmt.else_branch);
            }
            break;
        }
    }
}

bool uf_analyze_program(UfSemanticAnalyzer* analyzer, UfProgram* program) {
    /* Pass 1: Hoist top-level function and struct declarations */
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_FUNCTION) {
            uf_semantic_add_symbol_with_type(analyzer, stmt->as.function_stmt.name, UF_SYM_FUNCTION, stmt->span,
                                            (int)stmt->as.function_stmt.param_count, "Function",
                                            stmt->as.function_stmt.return_type,
                                            stmt->as.function_stmt.param_types);
        } else if (stmt->kind == UF_STMT_STRUCT) {
            uf_semantic_add_symbol_with_type(analyzer, stmt->as.struct_stmt.name, UF_SYM_STRUCT, stmt->span,
                                            (int)stmt->as.struct_stmt.field_count, stmt->as.struct_stmt.name,
                                            stmt->as.struct_stmt.name,
                                            stmt->as.struct_stmt.field_types);
        }
    }

    /* Pass 2: Analyze all statements */
    for (size_t i = 0; i < program->count; ++i) {
        analyze_stmt(analyzer, program->stmts[i]);
    }

    return !analyzer->had_error;
}

bool uf_typecheck_program(UfSemanticAnalyzer* analyzer, UfProgram* program) {
    (void)program;
    return !analyzer->had_error;
}
