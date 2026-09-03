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
    UF_EXPR_FUNCTION
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
            size_t param_count;
            struct UfStmt* body;
        } fn_expr;
    } as;
};

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
    UF_STMT_TRY_CATCH
} UfStmtKind;

struct UfStmt {
    UfStmtKind kind;
    SourceSpan span;
    union {
        struct {
            const char* name;
            UfExpr* init;
        } let_stmt;

        struct {
            const char* name;
            UfExpr* value;
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
            size_t param_count;
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
        } try_catch;
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
UfExpr* uf_expr_function(UfArena* arena, SourceSpan span, const char* name, const char** params, size_t param_count, struct UfStmt* body);

UfStmt* uf_stmt_let(UfArena* arena, SourceSpan span, const char* name, UfExpr* init);
UfStmt* uf_stmt_assign(UfArena* arena, SourceSpan span, const char* name, UfExpr* value);
UfStmt* uf_stmt_index_assign(UfArena* arena, SourceSpan span, UfExpr* target, UfExpr* index, UfExpr* value);
UfStmt* uf_stmt_say(UfArena* arena, SourceSpan span, UfExpr* expr);
UfStmt* uf_stmt_expr(UfArena* arena, SourceSpan span, UfExpr* expr);
UfStmt* uf_stmt_if(UfArena* arena, SourceSpan span, UfExpr* condition, UfStmt* then_branch, UfStmt* else_branch);
UfStmt* uf_stmt_while(UfArena* arena, SourceSpan span, UfExpr* condition, UfStmt* body);
UfStmt* uf_stmt_repeat(UfArena* arena, SourceSpan span, UfExpr* count_expr, UfStmt* body);
UfStmt* uf_stmt_for(UfArena* arena, SourceSpan span, const char* var_name, UfExpr* iterable, UfStmt* body);
UfStmt* uf_stmt_break(UfArena* arena, SourceSpan span);
UfStmt* uf_stmt_continue(UfArena* arena, SourceSpan span);
UfStmt* uf_stmt_function(UfArena* arena, SourceSpan span, const char* name, const char** params, size_t param_count, UfStmt* body);
UfStmt* uf_stmt_return(UfArena* arena, SourceSpan span, UfExpr* value);
UfStmt* uf_stmt_block(UfArena* arena, SourceSpan span, UfStmt** stmts, size_t count);
UfStmt* uf_stmt_try_catch(UfArena* arena, SourceSpan span, UfStmt* try_block, const char* catch_var, UfStmt* catch_block);

void uf_ast_print(const UfProgram* program, FILE* out);
void uf_ast_print_stmt(const UfStmt* stmt, FILE* out, int indent);
void uf_ast_print_expr(const UfExpr* expr, FILE* out);

#endif /* UF_AST_H */
