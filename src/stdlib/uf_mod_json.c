#include "uf_mod_json.h"
#include "../runtime/uf_runtime.h"
#include <ctype.h>
#include <math.h>
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

static inline int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static UfValue parse_string(UfRuntime* rt, JsonParser* p) {
    skip_whitespace(p);
    if (p->pos >= p->len || p->src[p->pos++] != '"') {
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
                case 'u': {
                    if (p->pos + 4 <= p->len) {
                        int d0 = hex_digit(p->src[p->pos]);
                        int d1 = hex_digit(p->src[p->pos + 1]);
                        int d2 = hex_digit(p->src[p->pos + 2]);
                        int d3 = hex_digit(p->src[p->pos + 3]);
                        if (d0 >= 0 && d1 >= 0 && d2 >= 0 && d3 >= 0) {
                            p->pos += 4;
                            uint32_t cp = (uint32_t)((d0 << 12) | (d1 << 8) | (d2 << 4) | d3);
                            if (cp >= 0xD800 && cp <= 0xDBFF && p->pos + 6 <= p->len &&
                                p->src[p->pos] == '\\' && p->src[p->pos + 1] == 'u') {
                                int s0 = hex_digit(p->src[p->pos + 2]);
                                int s1 = hex_digit(p->src[p->pos + 3]);
                                int s2 = hex_digit(p->src[p->pos + 4]);
                                int s3 = hex_digit(p->src[p->pos + 5]);
                                if (s0 >= 0 && s1 >= 0 && s2 >= 0 && s3 >= 0) {
                                    uint32_t low = (uint32_t)((s0 << 12) | (s1 << 8) | (s2 << 4) | s3);
                                    if (low >= 0xDC00 && low <= 0xDFFF) {
                                        p->pos += 6;
                                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                                    }
                                }
                            }
                            if (len + 4 >= cap) {
                                cap = (cap * 2) + 8;
                                buf = (char*)realloc(buf, cap);
                            }
                            if (cp <= 0x7F) {
                                buf[len++] = (char)cp;
                            } else if (cp <= 0x7FF) {
                                buf[len++] = (char)(0xC0 | ((cp >> 6) & 0x1F));
                                buf[len++] = (char)(0x80 | (cp & 0x3F));
                            } else if (cp <= 0xFFFF) {
                                buf[len++] = (char)(0xE0 | ((cp >> 12) & 0x0F));
                                buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                buf[len++] = (char)(0x80 | (cp & 0x3F));
                            } else {
                                buf[len++] = (char)(0xF0 | ((cp >> 18) & 0x07));
                                buf[len++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                                buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                buf[len++] = (char)(0x80 | (cp & 0x3F));
                            }
                            continue;
                        }
                    }
                    c = esc;
                    break;
                }
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

        /* The key is unreachable until it is stored, and parsing the value
         * allocates. */
        uf_runtime_push_temp_root(rt, key);
        UfValue val = parse_value(rt, p);
        if (!p->has_error) {
            uf_map_set(rt, map.as.map, key, val);
        }
        uf_runtime_pop_temp_root(rt);
        if (p->has_error) break;

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

/* Tracks the array/map object pointers currently being stringified on the
 * current recursive call chain. Unlike say/print's repr (which prints
 * "[...]" for a repeated container), JSON has no syntax for a cycle, so a
 * self- or mutually-referential structure is a runtime error here — the
 * same behavior as JS's JSON.stringify ("Converting circular structure to
 * JSON") — rather than either crashing with a stack overflow or silently
 * emitting a truncated placeholder that wouldn't parse back as valid JSON. */
typedef struct {
    const void** ptrs;
    size_t count;
    size_t cap;
} UfJsonVisited;

static bool json_visit_enter(UfJsonVisited* vis, const void* ptr) {
    for (size_t i = 0; i < vis->count; ++i) {
        if (vis->ptrs[i] == ptr) return false;
    }
    if (vis->count == vis->cap) {
        size_t new_cap = vis->cap == 0 ? 8 : vis->cap * 2;
        const void** new_ptrs = (const void**)realloc((void*)vis->ptrs, new_cap * sizeof(const void*));
        if (!new_ptrs) return false;
        vis->ptrs = new_ptrs;
        vis->cap = new_cap;
    }
    vis->ptrs[vis->count++] = ptr;
    return true;
}

static void json_visit_leave(UfJsonVisited* vis) {
    if (vis->count > 0) vis->count--;
}

static void stringify_json_string(StringBuilder* sb, const char* s, size_t slen) {
    sb_append(sb, "\"", 1);
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
}

/* Returns false (without raising anything itself) on a detected cycle, so
 * the top-level caller can free its StringBuilder/visited-set before
 * raising the catchable error — uf_runtime_error may longjmp out past this
 * function entirely when a try/catch is active, which would otherwise skip
 * that cleanup and leak them. */
static bool stringify_value(StringBuilder* sb, UfValue val, UfJsonVisited* vis) {
    switch (val.kind) {
        case UF_VAL_NULL:
            sb_append(sb, "null", 4);
            return true;
        case UF_VAL_BOOL:
            if (val.as.boolean) sb_append(sb, "true", 4);
            else sb_append(sb, "false", 5);
            return true;
        case UF_VAL_NUMBER: {
            char num_buf[64];
            /* fabs(...) < 1e15 must be checked before the int64_t cast below:
             * converting a double outside int64_t's range (or NaN/infinity)
             * is undefined behavior in C, so it has to be known-safe first. */
            if (!isnan(val.as.number) && !isinf(val.as.number) &&
                fabs(val.as.number) < 1e15 && val.as.number == floor(val.as.number)) {
                snprintf(num_buf, sizeof(num_buf), "%lld", (long long)val.as.number); /* long is 32-bit on wasm32/ARM */
            } else {
                snprintf(num_buf, sizeof(num_buf), "%.14g", val.as.number);
            }
            sb_append(sb, num_buf, strlen(num_buf));
            return true;
        }
        case UF_VAL_STRING: {
            stringify_json_string(sb, val.as.string->chars, val.as.string->length);
            return true;
        }
        case UF_VAL_ARRAY: {
            UfArrayObject* arr = val.as.array;
            if (!json_visit_enter(vis, arr)) return false;
            sb_append(sb, "[", 1);
            for (size_t i = 0; i < arr->count; ++i) {
                if (i > 0) sb_append(sb, ", ", 2);
                if (!stringify_value(sb, arr->elements[i], vis)) {
                    json_visit_leave(vis);
                    return false;
                }
            }
            sb_append(sb, "]", 1);
            json_visit_leave(vis);
            return true;
        }
        case UF_VAL_MAP: {
            UfMapObject* map = val.as.map;
            if (!json_visit_enter(vis, map)) return false;
            sb_append(sb, "{", 1);
            for (size_t i = 0; i < map->order_count; ++i) {
                if (i > 0) sb_append(sb, ", ", 2);
                UfValue k = map->order_keys[i];
                char* k_str = uf_val_to_string(k);
                stringify_json_string(sb, k_str, strlen(k_str));
                sb_append(sb, ": ", 2);
                free(k_str);
                UfValue v = uf_map_get(map, k);
                if (!stringify_value(sb, v, vis)) {
                    json_visit_leave(vis);
                    return false;
                }
            }
            sb_append(sb, "}", 1);
            json_visit_leave(vis);
            return true;
        }
        default:
            sb_append(sb, "null", 4);
            return true;
    }
}

static UfValue json_stringify(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1) {
        return uf_val_string_cstr(rt, "null");
    }
    StringBuilder sb;
    sb_init(&sb);
    UfJsonVisited vis = {0};
    bool ok = stringify_value(&sb, args[0], &vis);
    free((void*)vis.ptrs);
    if (!ok) {
        free(sb.data);
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_raise(rt, "TypeError", source_span_make(loc, loc), "Converting circular structure to JSON");
        return uf_val_null();
    }
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
