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
    expr->span = span;
    uf_expr_set_number(expr, val);
    return expr;
}

void uf_expr_set_number(UfExpr* expr, double val) {
    expr->kind = UF_EXPR_LITERAL_NUMBER;
    expr->as.number_lit.value = val;
    expr->as.number_lit.text = NULL;
    expr->as.number_lit.length = 0;
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

UfExpr* uf_expr_function(UfArena* arena, SourceSpan span, const char* name, const char** params, const char** param_types, struct UfExpr** param_defaults, size_t param_count, size_t min_param_count, bool has_rest, const char* return_type, UfStmt* body) {
    return uf_expr_function_async(arena, span, name, params, param_types, param_defaults, param_count, min_param_count, has_rest, return_type, false, body);
}

UfExpr* uf_expr_function_async(UfArena* arena, SourceSpan span, const char* name, const char** params, const char** param_types, struct UfExpr** param_defaults, size_t param_count, size_t min_param_count, bool has_rest, const char* return_type, bool is_async, UfStmt* body) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_FUNCTION;
    expr->span = span;
    expr->as.fn_expr.name = name;
    expr->as.fn_expr.params = params;
    expr->as.fn_expr.param_types = param_types;
    expr->as.fn_expr.param_defaults = param_defaults;
    expr->as.fn_expr.param_count = param_count;
    expr->as.fn_expr.min_param_count = min_param_count;
    expr->as.fn_expr.has_rest = has_rest;
    expr->as.fn_expr.is_async = is_async;
    expr->as.fn_expr.return_type = return_type;
    expr->as.fn_expr.body = body;
    return expr;
}

UfExpr* uf_expr_string_interp(UfArena* arena, SourceSpan span, UfExpr** parts, size_t count) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_STRING_INTERP;
    expr->span = span;
    expr->as.string_interp.parts = parts;
    expr->as.string_interp.count = count;
    return expr;
}

UfExpr* uf_expr_spread(UfArena* arena, SourceSpan span, UfExpr* operand) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_SPREAD;
    expr->span = span;
    expr->as.spread.operand = operand;
    return expr;
}

UfExpr* uf_expr_await(UfArena* arena, SourceSpan span, UfExpr* value) {
    UfExpr* expr = (UfExpr*)uf_arena_alloc(arena, sizeof(UfExpr));
    expr->kind = UF_EXPR_AWAIT;
    expr->span = span;
    expr->as.await_expr.value = value;
    return expr;
}

UfStmt* uf_stmt_let(UfArena* arena, SourceSpan span, const char* name, const char* type_annotation, UfExpr* init) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_LET;
    stmt->span = span;
    stmt->as.let_stmt.name = name;
    stmt->as.let_stmt.type_annotation = type_annotation;
    stmt->as.let_stmt.init = init;
    stmt->as.let_stmt.pattern = NULL;
    return stmt;
}

UfStmt* uf_stmt_let_pattern(UfArena* arena, SourceSpan span, UfPattern* pattern, UfExpr* init) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_LET;
    stmt->span = span;
    stmt->as.let_stmt.name = NULL;
    stmt->as.let_stmt.type_annotation = NULL;
    stmt->as.let_stmt.init = init;
    stmt->as.let_stmt.pattern = pattern;
    return stmt;
}

UfStmt* uf_stmt_assign(UfArena* arena, SourceSpan span, const char* name, UfExpr* value) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_ASSIGN;
    stmt->span = span;
    stmt->as.assign_stmt.name = name;
    stmt->as.assign_stmt.value = value;
    stmt->as.assign_stmt.pattern = NULL;
    return stmt;
}

UfStmt* uf_stmt_assign_pattern(UfArena* arena, SourceSpan span, UfPattern* pattern, UfExpr* value) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_ASSIGN;
    stmt->span = span;
    stmt->as.assign_stmt.name = NULL;
    stmt->as.assign_stmt.value = value;
    stmt->as.assign_stmt.pattern = pattern;
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

UfStmt* uf_stmt_function(UfArena* arena, SourceSpan span, const char* name, const char** params, const char** param_types, struct UfExpr** param_defaults, size_t param_count, size_t min_param_count, bool has_rest, const char* return_type, UfStmt* body) {
    return uf_stmt_function_async(arena, span, name, params, param_types, param_defaults, param_count, min_param_count, has_rest, return_type, NULL, NULL, 0, false, body);
}

UfStmt* uf_stmt_function_generic(UfArena* arena, SourceSpan span, const char* name, const char** params, const char** param_types, struct UfExpr** param_defaults, size_t param_count, size_t min_param_count, bool has_rest, const char* return_type, const char** type_params, const char** type_param_bounds, size_t type_param_count, UfStmt* body) {
    return uf_stmt_function_async(arena, span, name, params, param_types, param_defaults, param_count, min_param_count, has_rest, return_type, type_params, type_param_bounds, type_param_count, false, body);
}

UfStmt* uf_stmt_function_async(UfArena* arena, SourceSpan span, const char* name, const char** params, const char** param_types, struct UfExpr** param_defaults, size_t param_count, size_t min_param_count, bool has_rest, const char* return_type, const char** type_params, const char** type_param_bounds, size_t type_param_count, bool is_async, UfStmt* body) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_FUNCTION;
    stmt->span = span;
    stmt->as.function_stmt.name = name;
    stmt->as.function_stmt.params = params;
    stmt->as.function_stmt.param_types = param_types;
    stmt->as.function_stmt.param_defaults = param_defaults;
    stmt->as.function_stmt.param_count = param_count;
    stmt->as.function_stmt.min_param_count = min_param_count;
    stmt->as.function_stmt.has_rest = has_rest;
    stmt->as.function_stmt.return_type = return_type;
    stmt->as.function_stmt.type_params = type_params;
    stmt->as.function_stmt.type_param_bounds = type_param_bounds;
    stmt->as.function_stmt.type_param_count = type_param_count;
    stmt->as.function_stmt.is_async = is_async;
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

UfStmt* uf_stmt_try_catch(UfArena* arena, SourceSpan span, UfStmt* try_block, const char* catch_var, UfStmt* catch_block, UfStmt* finally_block) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_TRY_CATCH;
    stmt->span = span;
    stmt->as.try_catch.try_block = try_block;
    stmt->as.try_catch.catch_var = catch_var;
    stmt->as.try_catch.catch_block = catch_block;
    stmt->as.try_catch.finally_block = finally_block;
    return stmt;
}

UfStmt* uf_stmt_import(UfArena* arena, SourceSpan span, const char* module_name, const char* alias) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_IMPORT;
    stmt->span = span;
    stmt->as.import_stmt.module_name = module_name;
    stmt->as.import_stmt.alias = alias;
    return stmt;
}

UfStmt* uf_stmt_from_import(UfArena* arena, SourceSpan span, const char* module_name, const char** symbols, const char** aliases, size_t count) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_FROM_IMPORT;
    stmt->span = span;
    stmt->as.from_import_stmt.module_name = module_name;
    stmt->as.from_import_stmt.symbols = symbols;
    stmt->as.from_import_stmt.aliases = aliases;
    stmt->as.from_import_stmt.count = count;
    return stmt;
}

UfStmt* uf_stmt_struct(UfArena* arena, SourceSpan span, const char* name, const char** field_names, const char** field_types, size_t field_count, struct UfStmt** methods, size_t method_count) {
    return uf_stmt_struct_with_impls(arena, span, name, field_names, field_types, field_count, methods, method_count, NULL, 0);
}

UfStmt* uf_stmt_struct_with_impls(UfArena* arena, SourceSpan span, const char* name, const char** field_names, const char** field_types, size_t field_count, struct UfStmt** methods, size_t method_count, struct UfStmt** impl_blocks, size_t impl_block_count) {
    return uf_stmt_struct_with_generics(arena, span, name, field_names, field_types, field_count, methods, method_count, impl_blocks, impl_block_count, NULL, NULL, 0);
}

UfStmt* uf_stmt_struct_with_generics(UfArena* arena, SourceSpan span, const char* name, const char** field_names, const char** field_types, size_t field_count, struct UfStmt** methods, size_t method_count, struct UfStmt** impl_blocks, size_t impl_block_count, const char** type_params, const char** type_param_bounds, size_t type_param_count) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_STRUCT;
    stmt->span = span;
    stmt->as.struct_stmt.name = name;
    stmt->as.struct_stmt.field_names = field_names;
    stmt->as.struct_stmt.field_types = field_types;
    stmt->as.struct_stmt.field_count = field_count;
    stmt->as.struct_stmt.methods = methods;
    stmt->as.struct_stmt.method_count = method_count;
    stmt->as.struct_stmt.impl_blocks = impl_blocks;
    stmt->as.struct_stmt.impl_block_count = impl_block_count;
    stmt->as.struct_stmt.type_params = type_params;
    stmt->as.struct_stmt.type_param_bounds = type_param_bounds;
    stmt->as.struct_stmt.type_param_count = type_param_count;
    return stmt;
}

UfStmt* uf_stmt_trait(UfArena* arena, SourceSpan span, const char* name, const char** method_names, size_t* method_param_counts, const char*** method_param_names, const char*** method_param_types, const char** method_return_types, size_t method_count) {
    return uf_stmt_trait_with_generics(arena, span, name, method_names, method_param_counts, method_param_names, method_param_types, method_return_types, method_count, NULL, NULL, 0);
}

UfStmt* uf_stmt_trait_with_generics(UfArena* arena, SourceSpan span, const char* name, const char** method_names, size_t* method_param_counts, const char*** method_param_names, const char*** method_param_types, const char** method_return_types, size_t method_count, const char** type_params, const char** type_param_bounds, size_t type_param_count) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_TRAIT;
    stmt->span = span;
    stmt->as.trait_stmt.name = name;
    stmt->as.trait_stmt.method_names = method_names;
    stmt->as.trait_stmt.method_param_counts = method_param_counts;
    stmt->as.trait_stmt.method_param_names = method_param_names;
    stmt->as.trait_stmt.method_param_types = method_param_types;
    stmt->as.trait_stmt.method_return_types = method_return_types;
    stmt->as.trait_stmt.method_count = method_count;
    stmt->as.trait_stmt.type_params = type_params;
    stmt->as.trait_stmt.type_param_bounds = type_param_bounds;
    stmt->as.trait_stmt.type_param_count = type_param_count;
    return stmt;
}

UfStmt* uf_stmt_impl(UfArena* arena, SourceSpan span, const char* trait_name, const char* struct_name, struct UfStmt** methods, size_t method_count) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_IMPL;
    stmt->span = span;
    stmt->as.impl_stmt.trait_name = trait_name;
    stmt->as.impl_stmt.struct_name = struct_name;
    stmt->as.impl_stmt.methods = methods;
    stmt->as.impl_stmt.method_count = method_count;
    return stmt;
}

UfStmt* uf_stmt_enum(UfArena* arena, SourceSpan span, const char* name, UfEnumVariant* variants, size_t variant_count) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_ENUM;
    stmt->span = span;
    stmt->as.enum_stmt.name = name;
    stmt->as.enum_stmt.variants = variants;
    stmt->as.enum_stmt.variant_count = variant_count;
    return stmt;
}

UfPattern* uf_pattern_literal(UfArena* arena, SourceSpan span, UfExpr* literal) {
    UfPattern* pat = (UfPattern*)uf_arena_alloc(arena, sizeof(UfPattern));
    pat->kind = UF_PAT_LITERAL;
    pat->span = span;
    pat->as.literal = literal;
    return pat;
}

UfPattern* uf_pattern_variable(UfArena* arena, SourceSpan span, const char* var_name) {
    UfPattern* pat = (UfPattern*)uf_arena_alloc(arena, sizeof(UfPattern));
    pat->kind = UF_PAT_VARIABLE;
    pat->span = span;
    pat->as.var_name = var_name;
    return pat;
}

UfPattern* uf_pattern_wildcard(UfArena* arena, SourceSpan span) {
    UfPattern* pat = (UfPattern*)uf_arena_alloc(arena, sizeof(UfPattern));
    pat->kind = UF_PAT_WILDCARD;
    pat->span = span;
    return pat;
}

UfPattern* uf_pattern_struct(UfArena* arena, SourceSpan span, const char* struct_name, UfPattern** field_patterns, size_t field_count) {
    UfPattern* pat = (UfPattern*)uf_arena_alloc(arena, sizeof(UfPattern));
    pat->kind = UF_PAT_STRUCT;
    pat->span = span;
    pat->as.struct_pat.struct_name = struct_name;
    pat->as.struct_pat.field_patterns = field_patterns;
    pat->as.struct_pat.field_count = field_count;
    return pat;
}

UfPattern* uf_pattern_array(UfArena* arena, SourceSpan span, UfPattern** elements, size_t count, bool has_rest) {
    UfPattern* pat = (UfPattern*)uf_arena_alloc(arena, sizeof(UfPattern));
    pat->kind = UF_PAT_ARRAY;
    pat->span = span;
    pat->as.array_pat.elements = elements;
    pat->as.array_pat.count = count;
    pat->as.array_pat.has_rest = has_rest;
    return pat;
}

UfPattern* uf_pattern_map(UfArena* arena, SourceSpan span, const char** keys, UfPattern** values, size_t count, bool has_rest, UfPattern* rest_pattern) {
    UfPattern* pat = (UfPattern*)uf_arena_alloc(arena, sizeof(UfPattern));
    pat->kind = UF_PAT_MAP;
    pat->span = span;
    pat->as.map_pat.keys = keys;
    pat->as.map_pat.values = values;
    pat->as.map_pat.count = count;
    pat->as.map_pat.has_rest = has_rest;
    pat->as.map_pat.rest_pattern = rest_pattern;
    return pat;
}

UfPattern* uf_pattern_rest(UfArena* arena, SourceSpan span, UfPattern* subpattern) {
    UfPattern* pat = (UfPattern*)uf_arena_alloc(arena, sizeof(UfPattern));
    pat->kind = UF_PAT_REST;
    pat->span = span;
    pat->as.rest_pat.subpattern = subpattern;
    return pat;
}

UfStmt* uf_stmt_match(UfArena* arena, SourceSpan span, UfExpr* expr, UfMatchArm* arms, size_t arm_count, UfStmt* else_branch) {
    UfStmt* stmt = (UfStmt*)uf_arena_alloc(arena, sizeof(UfStmt));
    stmt->kind = UF_STMT_MATCH;
    stmt->span = span;
    stmt->as.match_stmt.expr = expr;
    stmt->as.match_stmt.arms = arms;
    stmt->as.match_stmt.arm_count = arm_count;
    stmt->as.match_stmt.else_branch = else_branch;
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
                if (expr->as.map_lit.values[i]) {
                    fprintf(out, " (pair ");
                    uf_ast_print_expr(expr->as.map_lit.keys[i], out);
                    fprintf(out, " ");
                    uf_ast_print_expr(expr->as.map_lit.values[i], out);
                    fprintf(out, ")");
                } else {
                    fprintf(out, " ");
                    uf_ast_print_expr(expr->as.map_lit.keys[i], out);
                }
            }
            fprintf(out, ")");
            break;
        case UF_EXPR_FUNCTION:
            fprintf(out, "(fn %s (params", expr->as.fn_expr.name ? expr->as.fn_expr.name : "<anonymous>");
            for (size_t i = 0; i < expr->as.fn_expr.param_count; ++i) {
                if (expr->as.fn_expr.has_rest && i == expr->as.fn_expr.param_count - 1) {
                    fprintf(out, " ...%s", expr->as.fn_expr.params[i]);
                } else {
                    fprintf(out, " %s", expr->as.fn_expr.params[i]);
                }
            }
            fprintf(out, ") ");
            uf_ast_print_stmt(expr->as.fn_expr.body, out, 0);
            fprintf(out, ")");
            break;
        case UF_EXPR_STRING_INTERP:
            fprintf(out, "(string-interp");
            for (size_t i = 0; i < expr->as.string_interp.count; ++i) {
                fprintf(out, " ");
                uf_ast_print_expr(expr->as.string_interp.parts[i], out);
            }
            fprintf(out, ")");
            break;
        case UF_EXPR_SPREAD:
            fprintf(out, "(...");
            uf_ast_print_expr(expr->as.spread.operand, out);
            fprintf(out, ")");
            break;
        case UF_EXPR_AWAIT:
            fprintf(out, "(await ");
            uf_ast_print_expr(expr->as.await_expr.value, out);
            fprintf(out, ")");
            break;
    }
}

void uf_ast_print_pattern(const UfPattern* pat, FILE* out) {
    if (!pat) return;
    switch (pat->kind) {
        case UF_PAT_LITERAL:
            uf_ast_print_expr(pat->as.literal, out);
            break;
        case UF_PAT_VARIABLE:
            fprintf(out, "%s", pat->as.var_name ? pat->as.var_name : "_");
            break;
        case UF_PAT_WILDCARD:
            fprintf(out, "_");
            break;
        case UF_PAT_STRUCT:
            fprintf(out, "%s(", pat->as.struct_pat.struct_name);
            for (size_t i = 0; i < pat->as.struct_pat.field_count; ++i) {
                if (i > 0) fprintf(out, ", ");
                uf_ast_print_pattern(pat->as.struct_pat.field_patterns[i], out);
            }
            fprintf(out, ")");
            break;
        case UF_PAT_ARRAY:
            fprintf(out, "[");
            for (size_t i = 0; i < pat->as.array_pat.count; ++i) {
                if (i > 0) fprintf(out, ", ");
                uf_ast_print_pattern(pat->as.array_pat.elements[i], out);
            }
            fprintf(out, "]");
            break;
        case UF_PAT_MAP:
            fprintf(out, "{");
            for (size_t i = 0; i < pat->as.map_pat.count; ++i) {
                if (i > 0) fprintf(out, ", ");
                fprintf(out, "%s: ", pat->as.map_pat.keys[i]);
                uf_ast_print_pattern(pat->as.map_pat.values[i], out);
            }
            if (pat->as.map_pat.has_rest && pat->as.map_pat.rest_pattern) {
                if (pat->as.map_pat.count > 0) fprintf(out, ", ");
                fprintf(out, "...");
                uf_ast_print_pattern(pat->as.map_pat.rest_pattern, out);
            }
            fprintf(out, "}");
            break;
        case UF_PAT_REST:
            fprintf(out, "...");
            if (pat->as.rest_pat.subpattern) {
                uf_ast_print_pattern(pat->as.rest_pat.subpattern, out);
            }
            break;
    }
}

void uf_ast_print_stmt(const UfStmt* stmt, FILE* out, int indent) {
    if (!stmt) return;

    print_indent(out, indent);

    switch (stmt->kind) {
        case UF_STMT_LET:
            if (stmt->as.let_stmt.pattern) {
                fprintf(out, "(let-destruct ");
                uf_ast_print_pattern(stmt->as.let_stmt.pattern, out);
                if (stmt->as.let_stmt.init) {
                    fprintf(out, " ");
                    uf_ast_print_expr(stmt->as.let_stmt.init, out);
                }
                fprintf(out, ")\n");
            } else {
                fprintf(out, "(let %s", stmt->as.let_stmt.name ? stmt->as.let_stmt.name : "_");
                if (stmt->as.let_stmt.init) {
                    fprintf(out, " ");
                    uf_ast_print_expr(stmt->as.let_stmt.init, out);
                }
                fprintf(out, ")\n");
            }
            break;

        case UF_STMT_ASSIGN:
            if (stmt->as.assign_stmt.pattern) {
                fprintf(out, "(assign-destruct ");
                uf_ast_print_pattern(stmt->as.assign_stmt.pattern, out);
                fprintf(out, " ");
                uf_ast_print_expr(stmt->as.assign_stmt.value, out);
                fprintf(out, ")\n");
            } else {
                fprintf(out, "(assign %s ", stmt->as.assign_stmt.name ? stmt->as.assign_stmt.name : "_");
                uf_ast_print_expr(stmt->as.assign_stmt.value, out);
                fprintf(out, ")\n");
            }
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
            fprintf(out, "(%sfunction %s (", stmt->as.function_stmt.is_async ? "async " : "", stmt->as.function_stmt.name);
            for (size_t i = 0; i < stmt->as.function_stmt.param_count; ++i) {
                if (stmt->as.function_stmt.has_rest && i == stmt->as.function_stmt.param_count - 1) {
                    fprintf(out, "...%s%s", stmt->as.function_stmt.params[i],
                            (i + 1 < stmt->as.function_stmt.param_count) ? ", " : "");
                } else {
                    fprintf(out, "%s%s", stmt->as.function_stmt.params[i],
                            (i + 1 < stmt->as.function_stmt.param_count) ? ", " : "");
                }
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
            if (stmt->as.try_catch.catch_block) {
                print_indent(out, indent);
                fprintf(out, " catch %s\n", stmt->as.try_catch.catch_var ? stmt->as.try_catch.catch_var : "_");
                uf_ast_print_stmt(stmt->as.try_catch.catch_block, out, indent + 1);
            }
            if (stmt->as.try_catch.finally_block) {
                print_indent(out, indent);
                fprintf(out, " finally\n");
                uf_ast_print_stmt(stmt->as.try_catch.finally_block, out, indent + 1);
            }
            print_indent(out, indent);
            fprintf(out, ")\n");
            break;

        case UF_STMT_IMPORT:
            fprintf(out, "(import %s", stmt->as.import_stmt.module_name);
            if (stmt->as.import_stmt.alias) {
                fprintf(out, " as %s", stmt->as.import_stmt.alias);
            }
            fprintf(out, ")\n");
            break;

        case UF_STMT_FROM_IMPORT:
            fprintf(out, "(from %s import", stmt->as.from_import_stmt.module_name);
            for (size_t i = 0; i < stmt->as.from_import_stmt.count; ++i) {
                fprintf(out, " %s", stmt->as.from_import_stmt.symbols[i]);
                if (stmt->as.from_import_stmt.aliases && stmt->as.from_import_stmt.aliases[i]) {
                    fprintf(out, " as %s", stmt->as.from_import_stmt.aliases[i]);
                }
            }
            fprintf(out, ")\n");
            break;

        case UF_STMT_STRUCT:
            fprintf(out, "(struct %s", stmt->as.struct_stmt.name);
            for (size_t i = 0; i < stmt->as.struct_stmt.field_count; ++i) {
                fprintf(out, " (%s", stmt->as.struct_stmt.field_names[i]);
                if (stmt->as.struct_stmt.field_types && stmt->as.struct_stmt.field_types[i]) {
                    fprintf(out, ": %s", stmt->as.struct_stmt.field_types[i]);
                }
                fprintf(out, ")");
            }
            for (size_t i = 0; i < stmt->as.struct_stmt.method_count; ++i) {
                fprintf(out, " ");
                uf_ast_print_stmt(stmt->as.struct_stmt.methods[i], out, indent + 1);
            }
            fprintf(out, ")\n");
            break;

        case UF_STMT_ENUM:
            fprintf(out, "(enum %s", stmt->as.enum_stmt.name);
            for (size_t i = 0; i < stmt->as.enum_stmt.variant_count; ++i) {
                const UfEnumVariant* v = &stmt->as.enum_stmt.variants[i];
                fprintf(out, " (%s", v->name);
                for (size_t j = 0; j < v->field_count; ++j) {
                    fprintf(out, " %s", v->field_names[j]);
                    if (v->field_types && v->field_types[j]) {
                        fprintf(out, ": %s", v->field_types[j]);
                    }
                }
                fprintf(out, ")");
            }
            fprintf(out, ")\n");
            break;

        case UF_STMT_MATCH:
            fprintf(out, "(match ");
            uf_ast_print_expr(stmt->as.match_stmt.expr, out);
            fprintf(out, "\n");
            for (size_t i = 0; i < stmt->as.match_stmt.arm_count; ++i) {
                print_indent(out, indent + 1);
                fprintf(out, "(when ");
                /* print pattern inline */
                if (stmt->as.match_stmt.arms[i].pattern) {
                    uf_ast_print_pattern(stmt->as.match_stmt.arms[i].pattern, out);
                }
                if (stmt->as.match_stmt.arms[i].guard) {
                    fprintf(out, " if ");
                    uf_ast_print_expr(stmt->as.match_stmt.arms[i].guard, out);
                }
                fprintf(out, "\n");
                uf_ast_print_stmt(stmt->as.match_stmt.arms[i].body, out, indent + 2);
                print_indent(out, indent + 1);
                fprintf(out, ")\n");
            }
            if (stmt->as.match_stmt.else_branch) {
                print_indent(out, indent + 1);
                fprintf(out, "(else\n");
                uf_ast_print_stmt(stmt->as.match_stmt.else_branch, out, indent + 2);
                print_indent(out, indent + 1);
                fprintf(out, ")\n");
            }
            print_indent(out, indent);
            fprintf(out, ")\n");
            break;

        case UF_STMT_TRAIT:
            fprintf(out, "(trait %s", stmt->as.trait_stmt.name);
            for (size_t i = 0; i < stmt->as.trait_stmt.method_count; ++i) {
                fprintf(out, " (fn %s", stmt->as.trait_stmt.method_names[i]);
                if (stmt->as.trait_stmt.method_return_types && stmt->as.trait_stmt.method_return_types[i]) {
                    fprintf(out, ": %s", stmt->as.trait_stmt.method_return_types[i]);
                }
                fprintf(out, ")");
            }
            fprintf(out, ")\n");
            break;

        case UF_STMT_IMPL:
            if (stmt->as.impl_stmt.struct_name) {
                fprintf(out, "(impl %s for %s\n", stmt->as.impl_stmt.trait_name, stmt->as.impl_stmt.struct_name);
            } else {
                fprintf(out, "(impl %s\n", stmt->as.impl_stmt.trait_name);
            }
            for (size_t i = 0; i < stmt->as.impl_stmt.method_count; ++i) {
                uf_ast_print_stmt(stmt->as.impl_stmt.methods[i], out, indent + 1);
            }
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
