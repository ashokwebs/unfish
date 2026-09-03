#include "uf_mod_json.h"
#include "../runtime/uf_runtime.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- JSON Parser (Recursive Descent) --- */

typedef struct {
    const char* src;
    size_t pos;
    size_t len;
    bool has_error;
} JsonParser;

static void skip_whitespace(JsonParser* p) {
    while (p->pos < p->len && isspace((unsigned char)p->src[p->pos])) {
        p->pos++;
    }
}

static char peek(JsonParser* p) {
    skip_whitespace(p);
    if (p->pos >= p->len) return '\0';
    return p->src[p->pos];
}

static char advance(JsonParser* p) {
    skip_whitespace(p);
    if (p->pos >= p->len) return '\0';
    return p->src[p->pos++];
}

static UfValue parse_value(UfRuntime* rt, JsonParser* p);

static UfValue parse_string(UfRuntime* rt, JsonParser* p) {
    if (advance(p) != '"') {
        p->has_error = true;
        return uf_val_null();
    }
    size_t cap = 64;
    size_t len = 0;
    char* buf = (char*)malloc(cap);

    while (p->pos < p->len) {
        char c = p->src[p->pos++];
        if (c == '"') {
            buf[len] = '\0';
            UfValue v = uf_val_string(rt, buf, len);
            free(buf);
            return v;
        }
        if (c == '\\') {
            if (p->pos >= p->len) break;
            char esc = p->src[p->pos++];
            switch (esc) {
                case '"':  c = '"'; break;
                case '\\': c = '\\'; break;
                case '/':  c = '/'; break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                default:   c = esc; break;
            }
        }
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char*)realloc(buf, cap);
        }
        buf[len++] = c;
    }
    free(buf);
    p->has_error = true;
    return uf_val_null();
}

static UfValue parse_number(UfRuntime* rt, JsonParser* p) {
    UF_UNUSED(rt);
    skip_whitespace(p);
    char* endptr = NULL;
    double n = strtod(p->src + p->pos, &endptr);
    if (endptr == p->src + p->pos) {
        p->has_error = true;
        return uf_val_null();
    }
    p->pos = (size_t)(endptr - p->src);
    return uf_val_number(n);
}

static UfValue parse_array(UfRuntime* rt, JsonParser* p) {
    if (advance(p) != '[') {
        p->has_error = true;
        return uf_val_null();
    }
    UfValue arr = uf_val_array(rt, 8);
    uf_runtime_push_temp_root(rt, arr);

    skip_whitespace(p);
    if (peek(p) == ']') {
        advance(p);
        uf_runtime_pop_temp_root(rt);
        return arr;
    }

    while (!p->has_error && p->pos < p->len) {
        UfValue elem = parse_value(rt, p);
        if (p->has_error) break;
        uf_array_push(rt, arr.as.array, elem);

        skip_whitespace(p);
        if (peek(p) == ',') {
            advance(p);
        } else if (peek(p) == ']') {
            advance(p);
            break;
        } else {
            p->has_error = true;
            break;
        }
    }

    uf_runtime_pop_temp_root(rt);
    return arr;
}

static UfValue parse_object(UfRuntime* rt, JsonParser* p) {
    if (advance(p) != '{') {
        p->has_error = true;
        return uf_val_null();
    }
    UfValue map = uf_val_map(rt, 8);
    uf_runtime_push_temp_root(rt, map);

    skip_whitespace(p);
    if (peek(p) == '}') {
        advance(p);
        uf_runtime_pop_temp_root(rt);
        return map;
    }

    while (!p->has_error && p->pos < p->len) {
        skip_whitespace(p);
        if (peek(p) != '"') {
            p->has_error = true;
            break;
        }
        UfValue key = parse_string(rt, p);
        if (p->has_error) break;

        skip_whitespace(p);
        if (advance(p) != ':') {
            p->has_error = true;
            break;
        }

        UfValue val = parse_value(rt, p);
        if (p->has_error) break;

        uf_map_set(rt, map.as.map, key, val);

        skip_whitespace(p);
        if (peek(p) == ',') {
            advance(p);
        } else if (peek(p) == '}') {
            advance(p);
            break;
        } else {
            p->has_error = true;
            break;
        }
    }

    uf_runtime_pop_temp_root(rt);
    return map;
}

static UfValue parse_value(UfRuntime* rt, JsonParser* p) {
    skip_whitespace(p);
    char c = peek(p);
    if (c == '"') {
        return parse_string(rt, p);
    }
    if (c == '{') {
        return parse_object(rt, p);
    }
    if (c == '[') {
        return parse_array(rt, p);
    }
    if (c == '-' || isdigit((unsigned char)c)) {
        return parse_number(rt, p);
    }
    if (strncmp(p->src + p->pos, "true", 4) == 0) {
        p->pos += 4;
        return uf_val_bool(true);
    }
    if (strncmp(p->src + p->pos, "false", 5) == 0) {
        p->pos += 5;
        return uf_val_bool(false);
    }
    if (strncmp(p->src + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return uf_val_null();
    }
    p->has_error = true;
    return uf_val_null();
}

static UfValue json_parse(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        return uf_val_null();
    }
    JsonParser p;
    p.src = args[0].as.string->chars;
    p.pos = 0;
    p.len = args[0].as.string->length;
    p.has_error = false;

    UfValue val = parse_value(rt, &p);
    skip_whitespace(&p);
    if (p.has_error || p.pos < p.len) {
        return uf_val_null();
    }
    return val;
}

/* --- JSON Stringifier --- */

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StringBuilder;

static void sb_init(StringBuilder* sb) {
    sb->cap = 128;
    sb->len = 0;
    sb->data = (char*)malloc(sb->cap);
    sb->data[0] = '\0';
}

static void sb_append(StringBuilder* sb, const char* str, size_t len) {
    while (sb->len + len + 1 >= sb->cap) {
        sb->cap *= 2;
        sb->data = (char*)realloc(sb->data, sb->cap);
    }
    memcpy(sb->data + sb->len, str, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
}

static void stringify_value(StringBuilder* sb, UfValue val) {
    switch (val.kind) {
        case UF_VAL_NULL:
            sb_append(sb, "null", 4);
            break;
        case UF_VAL_BOOL:
            if (val.as.boolean) sb_append(sb, "true", 4);
            else sb_append(sb, "false", 5);
            break;
        case UF_VAL_NUMBER: {
            char num_buf[64];
            if (val.as.number == (double)(int64_t)val.as.number) {
                snprintf(num_buf, sizeof(num_buf), "%ld", (long)(int64_t)val.as.number);
            } else {
                snprintf(num_buf, sizeof(num_buf), "%.14g", val.as.number);
            }
            sb_append(sb, num_buf, strlen(num_buf));
            break;
        }
        case UF_VAL_STRING: {
            sb_append(sb, "\"", 1);
            const char* s = val.as.string->chars;
            size_t slen = val.as.string->length;
            for (size_t i = 0; i < slen; ++i) {
                char c = s[i];
                switch (c) {
                    case '"':  sb_append(sb, "\\\"", 2); break;
                    case '\\': sb_append(sb, "\\\\", 2); break;
                    case '\b': sb_append(sb, "\\b", 2); break;
                    case '\f': sb_append(sb, "\\f", 2); break;
                    case '\n': sb_append(sb, "\\n", 2); break;
                    case '\r': sb_append(sb, "\\r", 2); break;
                    case '\t': sb_append(sb, "\\t", 2); break;
                    default:   sb_append(sb, &c, 1); break;
                }
            }
            sb_append(sb, "\"", 1);
            break;
        }
        case UF_VAL_ARRAY: {
            sb_append(sb, "[", 1);
            UfArrayObject* arr = val.as.array;
            for (size_t i = 0; i < arr->count; ++i) {
                if (i > 0) sb_append(sb, ", ", 2);
                stringify_value(sb, arr->elements[i]);
            }
            sb_append(sb, "]", 1);
            break;
        }
        case UF_VAL_MAP: {
            sb_append(sb, "{", 1);
            UfMapObject* map = val.as.map;
            for (size_t i = 0; i < map->order_count; ++i) {
                if (i > 0) sb_append(sb, ", ", 2);
                UfValue k = map->order_keys[i];
                char* k_str = uf_val_to_string(k);
                sb_append(sb, "\"", 1);
                sb_append(sb, k_str, strlen(k_str));
                sb_append(sb, "\": ", 3);
                free(k_str);
                UfValue v = uf_map_get(map, k);
                stringify_value(sb, v);
            }
            sb_append(sb, "}", 1);
            break;
        }
        default:
            sb_append(sb, "null", 4);
            break;
    }
}

static UfValue json_stringify(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1) {
        return uf_val_string_cstr(rt, "null");
    }
    StringBuilder sb;
    sb_init(&sb);
    stringify_value(&sb, args[0]);
    UfValue res = uf_val_string(rt, sb.data, sb.len);
    free(sb.data);
    return res;
}

UfModuleObject* uf_mod_json_create(UfRuntime* rt) {
    UfModuleObject* mod = uf_module_create(rt, "json", "<builtin:json>");
    uf_runtime_push_temp_root(rt, uf_val_module(rt, mod));

    struct {
        const char* name;
        UfNativeFn fn;
        int arity;
    } fns[] = {
        { "parse", json_parse, 1 },
        { "stringify", json_stringify, 1 }
    };

    for (size_t i = 0; i < sizeof(fns) / sizeof(fns[0]); ++i) {
        UfValue val = uf_val_native(fns[i].name, fns[i].fn, fns[i].arity);
        UfValue k = uf_val_string_cstr(rt, fns[i].name);
        uf_map_set(rt, mod->exports.as.map, k, val);
        uf_env_declare(mod->env, fns[i].name, val);
    }

    uf_runtime_pop_temp_root(rt);
    mod->state = UF_MOD_LOADED;
    return mod;
}
