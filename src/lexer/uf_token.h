#ifndef UF_TOKEN_H
#define UF_TOKEN_H

#include "../common/uf_common.h"
#include "../common/uf_source.h"

typedef enum {
    /* Special */
    UF_TOK_EOF,
    UF_TOK_ERROR,
    UF_TOK_NEWLINE,
    UF_TOK_INDENT,
    UF_TOK_DEDENT,

    /* Literals & Identifiers */
    UF_TOK_IDENTIFIER,
    UF_TOK_NUMBER,
    UF_TOK_STRING,

    /* Keywords */
    UF_TOK_LET,
    UF_TOK_SAY,
    UF_TOK_FUNCTION,
    UF_TOK_RETURN,
    UF_TOK_IF,
    UF_TOK_ELSE,
    UF_TOK_WHILE,
    UF_TOK_REPEAT,
    UF_TOK_TIMES,
    UF_TOK_FOR,
    UF_TOK_IN,
    UF_TOK_BREAK,
    UF_TOK_CONTINUE,
    UF_TOK_TRY,
    UF_TOK_CATCH,
    UF_TOK_IMPORT,
    UF_TOK_FROM,
    UF_TOK_AS,
    UF_TOK_STRUCT,
    UF_TOK_MATCH,
    UF_TOK_WHEN,
    UF_TOK_AND,
    UF_TOK_OR,
    UF_TOK_NOT,
    UF_TOK_TRUE,
    UF_TOK_FALSE,
    UF_TOK_NULL,

    /* Operators & Delimiters */
    UF_TOK_PLUS,       /* + */
    UF_TOK_MINUS,      /* - */
    UF_TOK_STAR,       /* * */
    UF_TOK_SLASH,      /* / */
    UF_TOK_PERCENT,    /* % */
    UF_TOK_EQUAL,      /* = */
    UF_TOK_EQEQ,       /* == */
    UF_TOK_BANGEQ,     /* != */
    UF_TOK_LT,         /* < */
    UF_TOK_LTEQ,       /* <= */
    UF_TOK_GT,         /* > */
    UF_TOK_GTEQ,       /* >= */
    UF_TOK_LPAREN,     /* ( */
    UF_TOK_RPAREN,     /* ) */
    UF_TOK_COLON,      /* : */
    UF_TOK_COMMA,      /* , */
    UF_TOK_LBRACKET,   /* [ */
    UF_TOK_RBRACKET,   /* ] */
    UF_TOK_LBRACE,     /* { */
    UF_TOK_RBRACE,     /* } */
    UF_TOK_DOT         /* . */
} UfTokenKind;

typedef struct {
    UfTokenKind kind;
    SourceSpan span;
    const char* lexeme;
    size_t length;
    union {
        double number_val;
        const char* string_val;
    } as;
} UfToken;

const char* uf_token_kind_name(UfTokenKind kind);
void uf_token_print(const UfToken* token, FILE* out);

#endif /* UF_TOKEN_H */
