#include "uf_token.h"

const char* uf_token_kind_name(UfTokenKind kind) {
    switch (kind) {
        case UF_TOK_EOF:        return "EOF";
        case UF_TOK_ERROR:      return "ERROR";
        case UF_TOK_NEWLINE:    return "NEWLINE";
        case UF_TOK_INDENT:     return "INDENT";
        case UF_TOK_DEDENT:     return "DEDENT";
        case UF_TOK_IDENTIFIER: return "IDENTIFIER";
        case UF_TOK_NUMBER:     return "NUMBER";
        case UF_TOK_STRING:     return "STRING";
        case UF_TOK_LET:        return "let";
        case UF_TOK_SAY:        return "say";
        case UF_TOK_FUNCTION:   return "function";
        case UF_TOK_RETURN:     return "return";
        case UF_TOK_IF:         return "if";
        case UF_TOK_ELSE:       return "else";
        case UF_TOK_WHILE:      return "while";
        case UF_TOK_REPEAT:     return "repeat";
        case UF_TOK_TIMES:      return "times";
        case UF_TOK_FOR:        return "for";
        case UF_TOK_IN:         return "in";
        case UF_TOK_BREAK:      return "break";
        case UF_TOK_CONTINUE:   return "continue";
        case UF_TOK_TRY:        return "try";
        case UF_TOK_CATCH:      return "catch";
        case UF_TOK_AND:        return "and";
        case UF_TOK_OR:         return "or";
        case UF_TOK_NOT:        return "not";
        case UF_TOK_TRUE:       return "true";
        case UF_TOK_FALSE:      return "false";
        case UF_TOK_NULL:       return "null";
        case UF_TOK_PLUS:       return "+";
        case UF_TOK_MINUS:      return "-";
        case UF_TOK_STAR:       return "*";
        case UF_TOK_SLASH:      return "/";
        case UF_TOK_PERCENT:    return "%";
        case UF_TOK_EQUAL:      return "=";
        case UF_TOK_EQEQ:       return "==";
        case UF_TOK_BANGEQ:     return "!=";
        case UF_TOK_LT:         return "<";
        case UF_TOK_LTEQ:       return "<=";
        case UF_TOK_GT:         return ">";
        case UF_TOK_GTEQ:       return ">=";
        case UF_TOK_LPAREN:     return "(";
        case UF_TOK_RPAREN:     return ")";
        case UF_TOK_COLON:      return ":";
        case UF_TOK_COMMA:      return ",";
        case UF_TOK_LBRACKET:   return "[";
        case UF_TOK_RBRACKET:   return "]";
        case UF_TOK_LBRACE:     return "{";
        case UF_TOK_RBRACE:     return "}";
        case UF_TOK_DOT:        return ".";
    }
    return "<unknown>";
}

void uf_token_print(const UfToken* token, FILE* out) {
    fprintf(out, "[%u:%u-%u:%u] %-12s",
        token->span.start.line, token->span.start.col,
        token->span.end.line, token->span.end.col,
        uf_token_kind_name(token->kind));

    if (token->kind == UF_TOK_NUMBER) {
        fprintf(out, " (%.14g)", token->as.number_val);
    } else if (token->kind == UF_TOK_STRING) {
        fprintf(out, " (\"%s\")", token->as.string_val);
    } else if (token->lexeme && token->length > 0) {
        fprintf(out, " '%.*s'", (int)token->length, token->lexeme);
    }
    fprintf(out, "\n");
}
