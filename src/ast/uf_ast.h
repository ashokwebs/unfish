#ifndef UF_AST_H
#define UF_AST_H

#include "../common/uf_common.h"
#include "../common/uf_source.h"
#include "../common/uf_arena.h"
#include "../lexer/uf_token.h"

/* Forward declarations */
typedef struct UfExpr UfExpr;
typedef struct UfStmt UfStmt;

/* --- Expressions --- */

typedef enum {
    UF_EXPR_LITERAL_NULL,
    UF_EXPR_LITERAL_BOOL,
    UF_EXPR_LITERAL_NUMBER,
    UF_EXPR_LITERAL_STRING,
    UF_EXPR_IDENTIFIER,
    UF_EXPR_UNARY,
    UF_EXPR_BINARY,
    UF_EXPR_CALL,
    UF_EXPR_GROUPING,
    UF_EXPR_ARRAY,
    UF_EXPR_INDEX,
    UF_EXPR_MAP,
    UF_EXPR_FUNCTION,
    UF_EXPR_STRING_INTERP,
    UF_EXPR_SPREAD,
    UF_EXPR_AWAIT
} UfExprKind;

struct UfExpr {
    UfExprKind kind;
    SourceSpan span;
    union {
        bool bool_val;
        double number_val;
        const char* string_val;
        const char* identifier_name;

        struct {
            UfTokenKind op;
            UfExpr* operand;
        } unary;

        struct {
            UfTokenKind op;
            UfExpr* left;
            UfExpr* right;
        } binary;

        struct {
            UfExpr* callee;
            UfExpr** args;
            size_t argc;
        } call;

        struct {
            UfExpr* inner;
        } grouping;

        struct {
            UfExpr** elements;
            size_t count;
        } array_lit;

        struct {
            UfExpr* target;
            UfExpr* index;
        } index_expr;

        struct {
            UfExpr** keys;
            UfExpr** values;
            size_t count;
        } map_lit;

        struct {
            const char* name; /* Optional, may be NULL */
            const char** params;
            const char** param_types; /* May be NULL */
            struct UfExpr** param_defaults; /* May be NULL */
            size_t param_count;
            size_t min_param_count;
            bool has_rest;
            bool is_async;
            const char* return_type; /* May be NULL */
            struct UfStmt* body;
        } fn_expr;

        struct {
            struct UfExpr** parts;
            size_t count;
        } string_interp;

        struct {
            struct UfExpr* operand;
        } spread;

        struct {
            struct UfExpr* value;
        } await_expr;
    } as;
};

/* --- Patterns for Pattern Matching --- */

typedef enum {
    UF_PAT_LITERAL,    /* Literal value: Number, String, Bool, Null */
    UF_PAT_VARIABLE,   /* Variable binding: e.g. x */
    UF_PAT_WILDCARD,   /* Wildcard: _ */
    UF_PAT_STRUCT,     /* Struct deconstruction: Point(x, y) */
    UF_PAT_ARRAY,      /* Array destructuring: [a, b, ...rest] */
    UF_PAT_MAP,        /* Map destructuring: {x, y} or {x: a, y: b, ...rest} */
    UF_PAT_REST        /* Rest pattern: ...rest */
} UfPatternKind;

typedef struct UfPattern {
    UfPatternKind kind;
    SourceSpan span;
    union {
        UfExpr* literal;
        const char* var_name;
        struct {
            const char* struct_name;
            struct UfPattern** field_patterns;
            size_t field_count;
        } struct_pat;
        struct {
            struct UfPattern** elements;
            size_t count;
            bool has_rest;
        } array_pat;
        struct {
            const char** keys;
            struct UfPattern** values;
            size_t count;
            bool has_rest;
            struct UfPattern* rest_pattern; /* May be NULL */
        } map_pat;
        struct {
            struct UfPattern* subpattern;
        } rest_pat;
    } as;
} UfPattern;

typedef struct {
    UfPattern* pattern;
    UfExpr* guard; /* Optional if-guard expression, or NULL */
    struct UfStmt* body;
    SourceSpan span;
} UfMatchArm;

typedef struct {
    const char* name;
    const char** field_names;
    const char** field_types; /* May be NULL */
    size_t field_count;
    int tag;
    SourceSpan span;
} UfEnumVariant;

/* --- Statements --- */

typedef enum {
    UF_STMT_LET,
    UF_STMT_ASSIGN,
    UF_STMT_INDEX_ASSIGN,
    UF_STMT_SAY,
    UF_STMT_EXPR,
    UF_STMT_IF,
    UF_STMT_WHILE,
    UF_STMT_REPEAT,
    UF_STMT_FOR,
    UF_STMT_BREAK,
    UF_STMT_CONTINUE,
    UF_STMT_FUNCTION,
    UF_STMT_RETURN,
    UF_STMT_BLOCK,
    UF_STMT_TRY_CATCH,
    UF_STMT_IMPORT,
    UF_STMT_FROM_IMPORT,
    UF_STMT_STRUCT,
    UF_STMT_ENUM,
    UF_STMT_MATCH,
    UF_STMT_TRAIT,
    UF_STMT_IMPL
} UfStmtKind;

struct UfStmt {
    UfStmtKind kind;
    SourceSpan span;
    union {
        struct {
            const char* name; /* May be NULL if pattern is set */
            const char* type_annotation; /* May be NULL */
            UfExpr* init;
            struct UfPattern* pattern; /* May be NULL */
        } let_stmt;

        struct {
            const char* name; /* May be NULL if pattern is set */
            UfExpr* value;
            struct UfPattern* pattern; /* May be NULL */
        } assign_stmt;

        struct {
            UfExpr* target;
            UfExpr* index;
            UfExpr* value;
        } index_assign;

        struct {
            UfExpr* expr;
        } say_stmt;

        struct {
            UfExpr* expr;
        } expr_stmt;

        struct {
            UfExpr* condition;
            UfStmt* then_branch;
            UfStmt* else_branch; /* May be NULL, or another IF, or BLOCK */
        } if_stmt;

        struct {
            UfExpr* condition;
            UfStmt* body;
        } while_stmt;

        struct {
            UfExpr* count_expr;
            UfStmt* body;
        } repeat_stmt;

        struct {
            const char* var_name;
            UfExpr* iterable;
            UfStmt* body;
        } for_stmt;

        struct {
            const char* name;
            const char** params;
            const char** param_types; /* May be NULL */
            struct UfExpr** param_defaults; /* May be NULL */
            size_t param_count;
            size_t min_param_count;
            bool has_rest;
            const char* return_type; /* May be NULL */
            const char** type_params; /* May be NULL */
            const char** type_param_bounds; /* May be NULL */
            size_t type_param_count;
            bool is_async;
            UfStmt* body;
        } function_stmt;

        struct {
            UfExpr* value; /* May be NULL */
        } return_stmt;

        struct {
            UfStmt** stmts;
            size_t count;
        } block;

        struct {
            UfStmt* try_block;
            const char* catch_var;
            UfStmt* catch_block;
            UfStmt* finally_block;
        } try_catch;

        struct {
            const char* module_name;
            const char* alias;
        } import_stmt;

        struct {
            const char* module_name;
            const char** symbols;
            const char** aliases;
            size_t count;
        } from_import_stmt;

        struct {
            const char* name;
            const char** field_names;
            const char** field_types; /* May be NULL */
            size_t field_count;
            struct UfStmt** methods; /* May be NULL */
            size_t method_count;
            struct UfStmt** impl_blocks; /* May be NULL */
            size_t impl_block_count;
            const char** type_params; /* May be NULL */
            const char** type_param_bounds; /* May be NULL */
            size_t type_param_count;
        } struct_stmt;

        struct {
            const char* name;
            UfEnumVariant* variants;
            size_t variant_count;
        } enum_stmt;

        struct {
            UfExpr* expr;
            UfMatchArm* arms;
            size_t arm_count;
            struct UfStmt* else_branch; /* May be NULL */
        } match_stmt;

        struct {
            const char* name;
            const char** method_names;
            size_t* method_param_counts;
            const char*** method_param_names;
            const char*** method_param_types;
            const char** method_return_types;
            size_t method_count;
            const char** type_params; /* May be NULL */
            const char** type_param_bounds; /* May be NULL */
            size_t type_param_count;
        } trait_stmt;

        struct {
            const char* trait_name;
            const char* struct_name; /* May be NULL if inside struct */
            struct UfStmt** methods;
            size_t method_count;
        } impl_stmt;
    } as;
};

/* --- Program --- */

typedef struct {
    UfStmt** stmts;
    size_t count;
    SourceSpan span;
} UfProgram;

/* Constructors allocating within UfArena */
UfExpr* uf_expr_literal_null(UfArena* arena, SourceSpan span);
UfExpr* uf_expr_literal_bool(UfArena* arena, SourceSpan span, bool val);
UfExpr* uf_expr_literal_number(UfArena* arena, SourceSpan span, double val);
UfExpr* uf_expr_literal_string(UfArena* arena, SourceSpan span, const char* str);
UfExpr* uf_expr_identifier(UfArena* arena, SourceSpan span, const char* name);
UfExpr* uf_expr_unary(UfArena* arena, SourceSpan span, UfTokenKind op, UfExpr* operand);
UfExpr* uf_expr_binary(UfArena* arena, SourceSpan span, UfTokenKind op, UfExpr* left, UfExpr* right);
UfExpr* uf_expr_call(UfArena* arena, SourceSpan span, UfExpr* callee, UfExpr** args, size_t argc);
UfExpr* uf_expr_grouping(UfArena* arena, SourceSpan span, UfExpr* inner);
UfExpr* uf_expr_array(UfArena* arena, SourceSpan span, UfExpr** elements, size_t count);
UfExpr* uf_expr_index(UfArena* arena, SourceSpan span, UfExpr* target, UfExpr* index);
UfExpr* uf_expr_map(UfArena* arena, SourceSpan span, UfExpr** keys, UfExpr** values, size_t count);
UfExpr* uf_expr_function(UfArena* arena, SourceSpan span, const char* name, const char** params, const char** param_types, struct UfExpr** param_defaults, size_t param_count, size_t min_param_count, bool has_rest, const char* return_type, struct UfStmt* body);
UfExpr* uf_expr_function_async(UfArena* arena, SourceSpan span, const char* name, const char** params, const char** param_types, struct UfExpr** param_defaults, size_t param_count, size_t min_param_count, bool has_rest, const char* return_type, bool is_async, struct UfStmt* body);
UfExpr* uf_expr_string_interp(UfArena* arena, SourceSpan span, UfExpr** parts, size_t count);
UfExpr* uf_expr_spread(UfArena* arena, SourceSpan span, UfExpr* operand);
UfExpr* uf_expr_await(UfArena* arena, SourceSpan span, UfExpr* value);

UfPattern* uf_pattern_literal(UfArena* arena, SourceSpan span, UfExpr* literal);
UfPattern* uf_pattern_variable(UfArena* arena, SourceSpan span, const char* var_name);
UfPattern* uf_pattern_wildcard(UfArena* arena, SourceSpan span);
UfPattern* uf_pattern_struct(UfArena* arena, SourceSpan span, const char* struct_name, UfPattern** field_patterns, size_t field_count);
UfPattern* uf_pattern_array(UfArena* arena, SourceSpan span, UfPattern** elements, size_t count, bool has_rest);
UfPattern* uf_pattern_map(UfArena* arena, SourceSpan span, const char** keys, UfPattern** values, size_t count, bool has_rest, UfPattern* rest_pattern);
UfPattern* uf_pattern_rest(UfArena* arena, SourceSpan span, UfPattern* subpattern);

UfStmt* uf_stmt_let(UfArena* arena, SourceSpan span, const char* name, const char* type_annotation, UfExpr* init);
UfStmt* uf_stmt_let_pattern(UfArena* arena, SourceSpan span, UfPattern* pattern, UfExpr* init);
UfStmt* uf_stmt_assign(UfArena* arena, SourceSpan span, const char* name, UfExpr* value);
UfStmt* uf_stmt_assign_pattern(UfArena* arena, SourceSpan span, UfPattern* pattern, UfExpr* value);
UfStmt* uf_stmt_index_assign(UfArena* arena, SourceSpan span, UfExpr* target, UfExpr* index, UfExpr* value);
UfStmt* uf_stmt_say(UfArena* arena, SourceSpan span, UfExpr* expr);
UfStmt* uf_stmt_expr(UfArena* arena, SourceSpan span, UfExpr* expr);
UfStmt* uf_stmt_if(UfArena* arena, SourceSpan span, UfExpr* condition, UfStmt* then_branch, UfStmt* else_branch);
UfStmt* uf_stmt_while(UfArena* arena, SourceSpan span, UfExpr* condition, UfStmt* body);
UfStmt* uf_stmt_repeat(UfArena* arena, SourceSpan span, UfExpr* count_expr, UfStmt* body);
UfStmt* uf_stmt_for(UfArena* arena, SourceSpan span, const char* var_name, UfExpr* iterable, UfStmt* body);
UfStmt* uf_stmt_break(UfArena* arena, SourceSpan span);
UfStmt* uf_stmt_continue(UfArena* arena, SourceSpan span);
UfStmt* uf_stmt_function(UfArena* arena, SourceSpan span, const char* name, const char** params, const char** param_types, struct UfExpr** param_defaults, size_t param_count, size_t min_param_count, bool has_rest, const char* return_type, UfStmt* body);
UfStmt* uf_stmt_function_generic(UfArena* arena, SourceSpan span, const char* name, const char** params, const char** param_types, struct UfExpr** param_defaults, size_t param_count, size_t min_param_count, bool has_rest, const char* return_type, const char** type_params, const char** type_param_bounds, size_t type_param_count, UfStmt* body);
UfStmt* uf_stmt_function_async(UfArena* arena, SourceSpan span, const char* name, const char** params, const char** param_types, struct UfExpr** param_defaults, size_t param_count, size_t min_param_count, bool has_rest, const char* return_type, const char** type_params, const char** type_param_bounds, size_t type_param_count, bool is_async, UfStmt* body);
UfStmt* uf_stmt_return(UfArena* arena, SourceSpan span, UfExpr* value);
UfStmt* uf_stmt_block(UfArena* arena, SourceSpan span, UfStmt** stmts, size_t count);
UfStmt* uf_stmt_try_catch(UfArena* arena, SourceSpan span, UfStmt* try_block, const char* catch_var, UfStmt* catch_block, UfStmt* finally_block);
UfStmt* uf_stmt_import(UfArena* arena, SourceSpan span, const char* module_name, const char* alias);
UfStmt* uf_stmt_from_import(UfArena* arena, SourceSpan span, const char* module_name, const char** symbols, const char** aliases, size_t count);
UfStmt* uf_stmt_struct(UfArena* arena, SourceSpan span, const char* name, const char** field_names, const char** field_types, size_t field_count, struct UfStmt** methods, size_t method_count);
UfStmt* uf_stmt_struct_with_impls(UfArena* arena, SourceSpan span, const char* name, const char** field_names, const char** field_types, size_t field_count, struct UfStmt** methods, size_t method_count, struct UfStmt** impl_blocks, size_t impl_block_count);
UfStmt* uf_stmt_struct_with_generics(UfArena* arena, SourceSpan span, const char* name, const char** field_names, const char** field_types, size_t field_count, struct UfStmt** methods, size_t method_count, struct UfStmt** impl_blocks, size_t impl_block_count, const char** type_params, const char** type_param_bounds, size_t type_param_count);
UfStmt* uf_stmt_trait(UfArena* arena, SourceSpan span, const char* name, const char** method_names, size_t* method_param_counts, const char*** method_param_names, const char*** method_param_types, const char** method_return_types, size_t method_count);
UfStmt* uf_stmt_trait_with_generics(UfArena* arena, SourceSpan span, const char* name, const char** method_names, size_t* method_param_counts, const char*** method_param_names, const char*** method_param_types, const char** method_return_types, size_t method_count, const char** type_params, const char** type_param_bounds, size_t type_param_count);
UfStmt* uf_stmt_impl(UfArena* arena, SourceSpan span, const char* trait_name, const char* struct_name, struct UfStmt** methods, size_t method_count);
UfStmt* uf_stmt_enum(UfArena* arena, SourceSpan span, const char* name, UfEnumVariant* variants, size_t variant_count);
UfStmt* uf_stmt_match(UfArena* arena, SourceSpan span, UfExpr* expr, UfMatchArm* arms, size_t arm_count, UfStmt* else_branch);

void uf_ast_print(const UfProgram* program, FILE* out);
void uf_ast_print_stmt(const UfStmt* stmt, FILE* out, int indent);
void uf_ast_print_expr(const UfExpr* expr, FILE* out);
void uf_ast_print_pattern(const UfPattern* pat, FILE* out);

#endif /* UF_AST_H */
