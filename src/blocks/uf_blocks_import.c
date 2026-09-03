#include "uf_blocks.h"
#include "../lexer/uf_token.h"
#include "../common/uf_string.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* --- Simple JSON Parser & AST Builder --- */

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonKind;

typedef struct JsonNode JsonNode;

typedef struct {
    char* key;
    JsonNode* val;
} JsonEntry;

struct JsonNode {
    JsonKind kind;
    union {
        bool bool_val;
        double num_val;
        char* str_val;
        struct {
            JsonNode** items;
            size_t count;
            size_t cap;
        } array;
        struct {
            JsonEntry* entries;
            size_t count;
            size_t cap;
        } object;
    } as;
};

static void json_free(JsonNode* node) {
    if (!node) return;
    switch (node->kind) {
        case JSON_STRING:
            free(node->as.str_val);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < node->as.array.count; ++i) {
                json_free(node->as.array.items[i]);
            }
            free(node->as.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < node->as.object.count; ++i) {
                free(node->as.object.entries[i].key);
                json_free(node->as.object.entries[i].val);
            }
            free(node->as.object.entries);
            break;
        default:
            break;
    }
    free(node);
}

typedef struct {
    const char* src;
    size_t pos;
    size_t len;
} JsonLexer;

static void skip_ws(JsonLexer* l) {
    while (l->pos < l->len && isspace((unsigned char)l->src[l->pos])) {
        l->pos++;
    }
}

static char peek_ch(JsonLexer* l) {
    skip_ws(l);
    if (l->pos >= l->len) return '\0';
    return l->src[l->pos];
}

static char next_ch(JsonLexer* l) {
    skip_ws(l);
    if (l->pos >= l->len) return '\0';
    return l->src[l->pos++];
}

static JsonNode* parse_json_value(JsonLexer* l);

static JsonNode* parse_json_string(JsonLexer* l) {
    if (next_ch(l) != '"') return NULL;
    size_t cap = 32;
    size_t len = 0;
    char* buf = (char*)malloc(cap);

    while (l->pos < l->len) {
        char c = l->src[l->pos++];
        if (c == '"') {
            buf[len] = '\0';
            JsonNode* node = (JsonNode*)malloc(sizeof(JsonNode));
            node->kind = JSON_STRING;
            node->as.str_val = buf;
            return node;
        }
        if (c == '\\') {
            if (l->pos >= l->len) break;
            char esc = l->src[l->pos++];
            switch (esc) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                default: break;
            }
        }
        if (len + 2 >= cap) {
            cap *= 2;
            buf = (char*)realloc(buf, cap);
        }
        buf[len++] = c;
    }
    free(buf);
    return NULL;
}

static JsonNode* parse_json_number(JsonLexer* l) {
    skip_ws(l);
    const char* start = &l->src[l->pos];
    char* end = NULL;
    double val = strtod(start, &end);
    if (end == start) return NULL;
    l->pos += (end - start);

    JsonNode* node = (JsonNode*)malloc(sizeof(JsonNode));
    node->kind = JSON_NUMBER;
    node->as.num_val = val;
    return node;
}

static JsonNode* parse_json_array(JsonLexer* l) {
    if (next_ch(l) != '[') return NULL;
    JsonNode* node = (JsonNode*)malloc(sizeof(JsonNode));
    node->kind = JSON_ARRAY;
    node->as.array.count = 0;
    node->as.array.cap = 8;
    node->as.array.items = (JsonNode**)malloc(sizeof(JsonNode*) * node->as.array.cap);

    if (peek_ch(l) == ']') {
        next_ch(l);
        return node;
    }

    while (true) {
        JsonNode* item = parse_json_value(l);
        if (!item) {
            json_free(node);
            return NULL;
        }
        if (node->as.array.count >= node->as.array.cap) {
            node->as.array.cap *= 2;
            node->as.array.items = (JsonNode**)realloc(node->as.array.items, sizeof(JsonNode*) * node->as.array.cap);
        }
        node->as.array.items[node->as.array.count++] = item;

        char c = peek_ch(l);
        if (c == ',') {
            next_ch(l);
        } else if (c == ']') {
            next_ch(l);
            break;
        } else {
            json_free(node);
            return NULL;
        }
    }
    return node;
}

static JsonNode* parse_json_object(JsonLexer* l) {
    if (next_ch(l) != '{') return NULL;
    JsonNode* node = (JsonNode*)malloc(sizeof(JsonNode));
    node->kind = JSON_OBJECT;
    node->as.object.count = 0;
    node->as.object.cap = 8;
    node->as.object.entries = (JsonEntry*)malloc(sizeof(JsonEntry) * node->as.object.cap);

    if (peek_ch(l) == '}') {
        next_ch(l);
        return node;
    }

    while (true) {
        JsonNode* key_node = parse_json_string(l);
        if (!key_node) {
            json_free(node);
            return NULL;
        }
        char* key = key_node->as.str_val;
        free(key_node);

        if (next_ch(l) != ':') {
            free(key);
            json_free(node);
            return NULL;
        }

        JsonNode* val_node = parse_json_value(l);
        if (!val_node) {
            free(key);
            json_free(node);
            return NULL;
        }

        if (node->as.object.count >= node->as.object.cap) {
            node->as.object.cap *= 2;
            node->as.object.entries = (JsonEntry*)realloc(node->as.object.entries, sizeof(JsonEntry) * node->as.object.cap);
        }
        node->as.object.entries[node->as.object.count].key = key;
        node->as.object.entries[node->as.object.count].val = val_node;
        node->as.object.count++;

        char c = peek_ch(l);
        if (c == ',') {
            next_ch(l);
        } else if (c == '}') {
            next_ch(l);
            break;
        } else {
            json_free(node);
            return NULL;
        }
    }
    return node;
}

static JsonNode* parse_json_value(JsonLexer* l) {
    char c = peek_ch(l);
    if (c == '{') return parse_json_object(l);
    if (c == '[') return parse_json_array(l);
    if (c == '"') return parse_json_string(l);
    if ((c >= '0' && c <= '9') || c == '-') return parse_json_number(l);
    if (c == 't' || c == 'f') {
        bool is_true = (c == 't');
        size_t match_len = is_true ? 4 : 5;
        if (l->pos + match_len <= l->len) {
            l->pos += match_len;
            JsonNode* node = (JsonNode*)malloc(sizeof(JsonNode));
            node->kind = JSON_BOOL;
            node->as.bool_val = is_true;
            return node;
        }
    }
    if (c == 'n') {
        if (l->pos + 4 <= l->len && strncmp(&l->src[l->pos], "null", 4) == 0) {
            l->pos += 4;
            JsonNode* node = (JsonNode*)malloc(sizeof(JsonNode));
            node->kind = JSON_NULL;
            return node;
        }
    }
    return NULL;
}

/* Helper getters */
static const JsonNode* obj_get(const JsonNode* node, const char* key) {
    if (!node || node->kind != JSON_OBJECT) return NULL;
    for (size_t i = 0; i < node->as.object.count; ++i) {
        if (strcmp(node->as.object.entries[i].key, key) == 0) {
            return node->as.object.entries[i].val;
        }
    }
    return NULL;
}

static const char* obj_get_str(const JsonNode* node, const char* key) {
    const JsonNode* child = obj_get(node, key);
    if (child && child->kind == JSON_STRING) return child->as.str_val;
    return NULL;
}

static double obj_get_num(const JsonNode* node, const char* key, double def) {
    const JsonNode* child = obj_get(node, key);
    if (child && child->kind == JSON_NUMBER) return child->as.num_val;
    return def;
}

static bool obj_get_bool(const JsonNode* node, const char* key, bool def) {
    const JsonNode* child = obj_get(node, key);
    if (child && child->kind == JSON_BOOL) return child->as.bool_val;
    return def;
}

static UfTokenKind parse_op(const char* str) {
    if (!str) return UF_TOK_EOF;
    if (strcmp(str, "+") == 0) return UF_TOK_PLUS;
    if (strcmp(str, "-") == 0) return UF_TOK_MINUS;
    if (strcmp(str, "*") == 0) return UF_TOK_STAR;
    if (strcmp(str, "/") == 0) return UF_TOK_SLASH;
    if (strcmp(str, "%") == 0) return UF_TOK_PERCENT;
    if (strcmp(str, "==") == 0) return UF_TOK_EQEQ;
    if (strcmp(str, "!=") == 0) return UF_TOK_BANGEQ;
    if (strcmp(str, "<") == 0) return UF_TOK_LT;
    if (strcmp(str, "<=") == 0) return UF_TOK_LTEQ;
    if (strcmp(str, ">") == 0) return UF_TOK_GT;
    if (strcmp(str, ">=") == 0) return UF_TOK_GTEQ;
    if (strcmp(str, "and") == 0) return UF_TOK_AND;
    if (strcmp(str, "or") == 0) return UF_TOK_OR;
    if (strcmp(str, "not") == 0) return UF_TOK_NOT;
    return UF_TOK_EOF;
}

static UfExpr* import_expr(const JsonNode* node, UfArena* arena, UfInterner* interner, SourceSpan span);
static UfStmt* import_stmt(const JsonNode* node, UfArena* arena, UfInterner* interner, SourceSpan span);
static UfPattern* import_pattern(const JsonNode* node, UfArena* arena, UfInterner* interner, SourceSpan span);

static UfExpr* import_expr(const JsonNode* node, UfArena* arena, UfInterner* interner, SourceSpan span) {
    if (!node || node->kind != JSON_OBJECT) return NULL;
    const char* kind = obj_get_str(node, "kind");
    if (!kind) return NULL;

    if (strcmp(kind, "literal_null") == 0) {
        return uf_expr_literal_null(arena, span);
    } else if (strcmp(kind, "literal_bool") == 0) {
        return uf_expr_literal_bool(arena, span, obj_get_bool(node, "value", false));
    } else if (strcmp(kind, "literal_number") == 0) {
        return uf_expr_literal_number(arena, span, obj_get_num(node, "value", 0.0));
    } else if (strcmp(kind, "literal_string") == 0) {
        const char* s = obj_get_str(node, "value");
        return uf_expr_literal_string(arena, span, s ? uf_intern_cstr(interner, s) : "");
    } else if (strcmp(kind, "identifier") == 0) {
        const char* name = obj_get_str(node, "name");
        return uf_expr_identifier(arena, span, name ? uf_intern_cstr(interner, name) : "");
    } else if (strcmp(kind, "unary") == 0) {
        UfTokenKind op = parse_op(obj_get_str(node, "op"));
        UfExpr* operand = import_expr(obj_get(node, "operand"), arena, interner, span);
        return uf_expr_unary(arena, span, op, operand);
    } else if (strcmp(kind, "binary") == 0) {
        UfTokenKind op = parse_op(obj_get_str(node, "op"));
        UfExpr* left = import_expr(obj_get(node, "left"), arena, interner, span);
        UfExpr* right = import_expr(obj_get(node, "right"), arena, interner, span);
        return uf_expr_binary(arena, span, op, left, right);
    } else if (strcmp(kind, "grouping") == 0) {
        UfExpr* inner = import_expr(obj_get(node, "inner"), arena, interner, span);
        return uf_expr_grouping(arena, span, inner);
    } else if (strcmp(kind, "call") == 0) {
        UfExpr* callee = import_expr(obj_get(node, "callee"), arena, interner, span);
        const JsonNode* args_node = obj_get(node, "args");
        size_t argc = (args_node && args_node->kind == JSON_ARRAY) ? args_node->as.array.count : 0;
        UfExpr** args = NULL;
        if (argc > 0) {
            args = (UfExpr**)uf_arena_alloc(arena, sizeof(UfExpr*) * argc);
            for (size_t i = 0; i < argc; ++i) {
                args[i] = import_expr(args_node->as.array.items[i], arena, interner, span);
            }
        }
        return uf_expr_call(arena, span, callee, args, argc);
    } else if (strcmp(kind, "array") == 0) {
        const JsonNode* elems_node = obj_get(node, "elements");
        size_t count = (elems_node && elems_node->kind == JSON_ARRAY) ? elems_node->as.array.count : 0;
        UfExpr** elements = NULL;
        if (count > 0) {
            elements = (UfExpr**)uf_arena_alloc(arena, sizeof(UfExpr*) * count);
            for (size_t i = 0; i < count; ++i) {
                elements[i] = import_expr(elems_node->as.array.items[i], arena, interner, span);
            }
        }
        return uf_expr_array(arena, span, elements, count);
    } else if (strcmp(kind, "index") == 0) {
        UfExpr* target = import_expr(obj_get(node, "target"), arena, interner, span);
        UfExpr* index = import_expr(obj_get(node, "index"), arena, interner, span);
        return uf_expr_index(arena, span, target, index);
    } else if (strcmp(kind, "map") == 0) {
        const JsonNode* keys_node = obj_get(node, "keys");
        const JsonNode* vals_node = obj_get(node, "values");
        size_t count = (keys_node && keys_node->kind == JSON_ARRAY) ? keys_node->as.array.count : 0;
        UfExpr** keys = NULL;
        UfExpr** values = NULL;
        if (count > 0) {
            keys = (UfExpr**)uf_arena_alloc(arena, sizeof(UfExpr*) * count);
            values = (UfExpr**)uf_arena_alloc(arena, sizeof(UfExpr*) * count);
            for (size_t i = 0; i < count; ++i) {
                keys[i] = import_expr(keys_node->as.array.items[i], arena, interner, span);
                values[i] = (vals_node && vals_node->kind == JSON_ARRAY && i < vals_node->as.array.count)
                                ? import_expr(vals_node->as.array.items[i], arena, interner, span)
                                : uf_expr_literal_null(arena, span);
            }
        }
        return uf_expr_map(arena, span, keys, values, count);
    } else if (strcmp(kind, "function") == 0) {
        const char* name = obj_get_str(node, "name");
        const char* ret_type = obj_get_str(node, "return_type");
        const JsonNode* p_node = obj_get(node, "params");
        const JsonNode* pt_node = obj_get(node, "param_types");
        size_t p_count = (p_node && p_node->kind == JSON_ARRAY) ? p_node->as.array.count : 0;
        const char** params = NULL;
        const char** param_types = NULL;
        if (p_count > 0) {
            params = (const char**)uf_arena_alloc(arena, sizeof(char*) * p_count);
            param_types = (const char**)uf_arena_alloc(arena, sizeof(char*) * p_count);
            for (size_t i = 0; i < p_count; ++i) {
                const char* pname = (p_node->as.array.items[i]->kind == JSON_STRING) ? p_node->as.array.items[i]->as.str_val : "";
                params[i] = uf_intern_cstr(interner, pname);
                if (pt_node && pt_node->kind == JSON_ARRAY && i < pt_node->as.array.count && pt_node->as.array.items[i]->kind == JSON_STRING) {
                    param_types[i] = uf_intern_cstr(interner, pt_node->as.array.items[i]->as.str_val);
                } else {
                    param_types[i] = NULL;
                }
            }
        }
        UfStmt* body = import_stmt(obj_get(node, "body"), arena, interner, span);
        return uf_expr_function(arena, span, name ? uf_intern_cstr(interner, name) : NULL,
                                params, param_types, p_count,
                                ret_type ? uf_intern_cstr(interner, ret_type) : NULL, body);
    }
    return NULL;
}

static UfPattern* import_pattern(const JsonNode* node, UfArena* arena, UfInterner* interner, SourceSpan span) {
    if (!node || node->kind != JSON_OBJECT) return NULL;
    const char* kind = obj_get_str(node, "kind");
    if (!kind) return NULL;

    if (strcmp(kind, "pattern_literal") == 0) {
        UfExpr* lit = import_expr(obj_get(node, "expr"), arena, interner, span);
        return uf_pattern_literal(arena, span, lit);
    } else if (strcmp(kind, "pattern_variable") == 0) {
        const char* name = obj_get_str(node, "name");
        return uf_pattern_variable(arena, span, name ? uf_intern_cstr(interner, name) : "");
    } else if (strcmp(kind, "pattern_wildcard") == 0) {
        return uf_pattern_wildcard(arena, span);
    } else if (strcmp(kind, "pattern_struct") == 0) {
        const char* sname = obj_get_str(node, "struct_name");
        const JsonNode* f_node = obj_get(node, "field_patterns");
        size_t f_count = (f_node && f_node->kind == JSON_ARRAY) ? f_node->as.array.count : 0;
        UfPattern** fps = NULL;
        if (f_count > 0) {
            fps = (UfPattern**)uf_arena_alloc(arena, sizeof(UfPattern*) * f_count);
            for (size_t i = 0; i < f_count; ++i) {
                fps[i] = import_pattern(f_node->as.array.items[i], arena, interner, span);
            }
        }
        return uf_pattern_struct(arena, span, sname ? uf_intern_cstr(interner, sname) : "", fps, f_count);
    }
    return NULL;
}

static UfStmt* import_stmt(const JsonNode* node, UfArena* arena, UfInterner* interner, SourceSpan span) {
    if (!node || node->kind != JSON_OBJECT) return NULL;
    const char* kind = obj_get_str(node, "kind");
    if (!kind) return NULL;

    if (strcmp(kind, "let") == 0) {
        const char* name = obj_get_str(node, "name");
        const char* type_ann = obj_get_str(node, "type_ann");
        UfExpr* init = import_expr(obj_get(node, "init"), arena, interner, span);
        return uf_stmt_let(arena, span, name ? uf_intern_cstr(interner, name) : "",
                           type_ann ? uf_intern_cstr(interner, type_ann) : NULL, init);
    } else if (strcmp(kind, "assign") == 0) {
        const char* name = obj_get_str(node, "name");
        UfExpr* val = import_expr(obj_get(node, "value"), arena, interner, span);
        return uf_stmt_assign(arena, span, name ? uf_intern_cstr(interner, name) : "", val);
    } else if (strcmp(kind, "index_assign") == 0) {
        UfExpr* target = import_expr(obj_get(node, "target"), arena, interner, span);
        UfExpr* index = import_expr(obj_get(node, "index"), arena, interner, span);
        UfExpr* val = import_expr(obj_get(node, "value"), arena, interner, span);
        return uf_stmt_index_assign(arena, span, target, index, val);
    } else if (strcmp(kind, "say") == 0) {
        UfExpr* val = import_expr(obj_get(node, "value"), arena, interner, span);
        return uf_stmt_say(arena, span, val);
    } else if (strcmp(kind, "expr") == 0) {
        UfExpr* val = import_expr(obj_get(node, "expression"), arena, interner, span);
        return uf_stmt_expr(arena, span, val);
    } else if (strcmp(kind, "if") == 0) {
        UfExpr* cond = import_expr(obj_get(node, "condition"), arena, interner, span);
        UfStmt* then_b = import_stmt(obj_get(node, "then_branch"), arena, interner, span);
        UfStmt* else_b = import_stmt(obj_get(node, "else_branch"), arena, interner, span);
        return uf_stmt_if(arena, span, cond, then_b, else_b);
    } else if (strcmp(kind, "while") == 0) {
        UfExpr* cond = import_expr(obj_get(node, "condition"), arena, interner, span);
        UfStmt* body = import_stmt(obj_get(node, "body"), arena, interner, span);
        return uf_stmt_while(arena, span, cond, body);
    } else if (strcmp(kind, "repeat") == 0) {
        UfExpr* count = import_expr(obj_get(node, "count"), arena, interner, span);
        UfStmt* body = import_stmt(obj_get(node, "body"), arena, interner, span);
        return uf_stmt_repeat(arena, span, count, body);
    } else if (strcmp(kind, "for_in") == 0) {
        const char* var_name = obj_get_str(node, "var_name");
        UfExpr* iterable = import_expr(obj_get(node, "iterable"), arena, interner, span);
        UfStmt* body = import_stmt(obj_get(node, "body"), arena, interner, span);
        return uf_stmt_for(arena, span, var_name ? uf_intern_cstr(interner, var_name) : "", iterable, body);
    } else if (strcmp(kind, "break") == 0) {
        return uf_stmt_break(arena, span);
    } else if (strcmp(kind, "continue") == 0) {
        return uf_stmt_continue(arena, span);
    } else if (strcmp(kind, "return") == 0) {
        UfExpr* val = import_expr(obj_get(node, "value"), arena, interner, span);
        return uf_stmt_return(arena, span, val);
    } else if (strcmp(kind, "block") == 0) {
        const JsonNode* stmts_node = obj_get(node, "statements");
        size_t count = (stmts_node && stmts_node->kind == JSON_ARRAY) ? stmts_node->as.array.count : 0;
        UfStmt** stmts = NULL;
        if (count > 0) {
            stmts = (UfStmt**)uf_arena_alloc(arena, sizeof(UfStmt*) * count);
            for (size_t i = 0; i < count; ++i) {
                stmts[i] = import_stmt(stmts_node->as.array.items[i], arena, interner, span);
            }
        }
        return uf_stmt_block(arena, span, stmts, count);
    } else if (strcmp(kind, "function") == 0) {
        const char* name = obj_get_str(node, "name");
        const char* ret_type = obj_get_str(node, "return_type");
        const JsonNode* p_node = obj_get(node, "params");
        const JsonNode* pt_node = obj_get(node, "param_types");
        size_t p_count = (p_node && p_node->kind == JSON_ARRAY) ? p_node->as.array.count : 0;
        const char** params = NULL;
        const char** param_types = NULL;
        if (p_count > 0) {
            params = (const char**)uf_arena_alloc(arena, sizeof(char*) * p_count);
            param_types = (const char**)uf_arena_alloc(arena, sizeof(char*) * p_count);
            for (size_t i = 0; i < p_count; ++i) {
                const char* pname = (p_node->as.array.items[i]->kind == JSON_STRING) ? p_node->as.array.items[i]->as.str_val : "";
                params[i] = uf_intern_cstr(interner, pname);
                if (pt_node && pt_node->kind == JSON_ARRAY && i < pt_node->as.array.count && pt_node->as.array.items[i]->kind == JSON_STRING) {
                    param_types[i] = uf_intern_cstr(interner, pt_node->as.array.items[i]->as.str_val);
                } else {
                    param_types[i] = NULL;
                }
            }
        }
        UfStmt* body = import_stmt(obj_get(node, "body"), arena, interner, span);
        return uf_stmt_function(arena, span, name ? uf_intern_cstr(interner, name) : "",
                                params, param_types, p_count,
                                ret_type ? uf_intern_cstr(interner, ret_type) : NULL, body);
    } else if (strcmp(kind, "try_catch") == 0) {
        UfStmt* try_b = import_stmt(obj_get(node, "try_block"), arena, interner, span);
        const char* err_var = obj_get_str(node, "error_var");
        UfStmt* catch_b = import_stmt(obj_get(node, "catch_block"), arena, interner, span);
        return uf_stmt_try_catch(arena, span, try_b, err_var ? uf_intern_cstr(interner, err_var) : "err", catch_b);
    } else if (strcmp(kind, "import") == 0) {
        const char* mod = obj_get_str(node, "module_name");
        const char* alias = obj_get_str(node, "alias");
        return uf_stmt_import(arena, span, mod ? uf_intern_cstr(interner, mod) : "",
                              alias ? uf_intern_cstr(interner, alias) : NULL);
    } else if (strcmp(kind, "from_import") == 0) {
        const char* mod = obj_get_str(node, "module_name");
        const JsonNode* syms_node = obj_get(node, "symbols");
        const JsonNode* alias_node = obj_get(node, "aliases");
        size_t count = (syms_node && syms_node->kind == JSON_ARRAY) ? syms_node->as.array.count : 0;
        const char** syms = NULL;
        const char** aliases = NULL;
        if (count > 0) {
            syms = (const char**)uf_arena_alloc(arena, sizeof(char*) * count);
            aliases = (const char**)uf_arena_alloc(arena, sizeof(char*) * count);
            for (size_t i = 0; i < count; ++i) {
                const char* s = (syms_node->as.array.items[i]->kind == JSON_STRING) ? syms_node->as.array.items[i]->as.str_val : "";
                syms[i] = uf_intern_cstr(interner, s);
                if (alias_node && alias_node->kind == JSON_ARRAY && i < alias_node->as.array.count && alias_node->as.array.items[i]->kind == JSON_STRING) {
                    aliases[i] = uf_intern_cstr(interner, alias_node->as.array.items[i]->as.str_val);
                } else {
                    aliases[i] = NULL;
                }
            }
        }
        return uf_stmt_from_import(arena, span, mod ? uf_intern_cstr(interner, mod) : "", syms, aliases, count);
    } else if (strcmp(kind, "struct") == 0) {
        const char* name = obj_get_str(node, "name");
        const JsonNode* f_node = obj_get(node, "fields");
        const JsonNode* ft_node = obj_get(node, "field_types");
        size_t f_count = (f_node && f_node->kind == JSON_ARRAY) ? f_node->as.array.count : 0;
        const char** fields = NULL;
        const char** field_types = NULL;
        if (f_count > 0) {
            fields = (const char**)uf_arena_alloc(arena, sizeof(char*) * f_count);
            field_types = (const char**)uf_arena_alloc(arena, sizeof(char*) * f_count);
            for (size_t i = 0; i < f_count; ++i) {
                const char* fname = (f_node->as.array.items[i]->kind == JSON_STRING) ? f_node->as.array.items[i]->as.str_val : "";
                fields[i] = uf_intern_cstr(interner, fname);
                if (ft_node && ft_node->kind == JSON_ARRAY && i < ft_node->as.array.count && ft_node->as.array.items[i]->kind == JSON_STRING) {
                    field_types[i] = uf_intern_cstr(interner, ft_node->as.array.items[i]->as.str_val);
                } else {
                    field_types[i] = NULL;
                }
            }
        }
        return uf_stmt_struct(arena, span, name ? uf_intern_cstr(interner, name) : "", fields, field_types, f_count);
    } else if (strcmp(kind, "match") == 0) {
        UfExpr* expr = import_expr(obj_get(node, "expr"), arena, interner, span);
        const JsonNode* arms_node = obj_get(node, "arms");
        size_t arm_count = (arms_node && arms_node->kind == JSON_ARRAY) ? arms_node->as.array.count : 0;
        UfMatchArm* arms = NULL;
        if (arm_count > 0) {
            arms = (UfMatchArm*)uf_arena_alloc(arena, sizeof(UfMatchArm) * arm_count);
            for (size_t i = 0; i < arm_count; ++i) {
                const JsonNode* arm_item = arms_node->as.array.items[i];
                arms[i].pattern = import_pattern(obj_get(arm_item, "pattern"), arena, interner, span);
                arms[i].guard = import_expr(obj_get(arm_item, "guard"), arena, interner, span);
                arms[i].body = import_stmt(obj_get(arm_item, "body"), arena, interner, span);
                arms[i].span = span;
            }
        }
        UfStmt* else_b = import_stmt(obj_get(node, "else_branch"), arena, interner, span);
        return uf_stmt_match(arena, span, expr, arms, arm_count, else_b);
    }
    return NULL;
}

UfProgram* uf_blocks_import_string(const char* json_str, UfArena* arena, UfInterner* interner, UfDiagnosticReporter* reporter) {
    if (!json_str) return NULL;
    (void)reporter;

    JsonLexer l;
    l.src = json_str;
    l.pos = 0;
    l.len = strlen(json_str);

    JsonNode* root = parse_json_value(&l);
    if (!root || root->kind != JSON_OBJECT) {
        if (root) json_free(root);
        return NULL;
    }

    const JsonNode* stmts_node = obj_get(root, "statements");
    if (!stmts_node || stmts_node->kind != JSON_ARRAY) {
        json_free(root);
        return NULL;
    }

    SourceSpan empty_span = { {"<blocks>", 1, 1, 0}, {"<blocks>", 1, 1, 0} };
    size_t count = stmts_node->as.array.count;
    UfStmt** stmts = (UfStmt**)uf_arena_alloc(arena, sizeof(UfStmt*) * (count > 0 ? count : 1));

    for (size_t i = 0; i < count; ++i) {
        stmts[i] = import_stmt(stmts_node->as.array.items[i], arena, interner, empty_span);
    }

    json_free(root);

    UfProgram* program = (UfProgram*)uf_arena_alloc(arena, sizeof(UfProgram));
    program->stmts = stmts;
    program->count = count;
    program->span = empty_span;
    return program;
}
