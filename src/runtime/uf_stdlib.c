#include "uf_stdlib.h"
#include "uf_value.h"
#include "uf_env.h"
#include "uf_runtime.h"
#include "uf_fiber.h"
#include "../vm/uf_vm.h"
#include "../semantic/uf_semantic.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ========================================================================= */
/* STRING STANDARD LIBRARY FUNCTIONS                                         */
/* ========================================================================= */

static const void* uf_memmem(const void* haystack, size_t haystack_len, const void* needle, size_t needle_len) {
    if (needle_len == 0) return haystack;
    if (haystack_len < needle_len) return NULL;
    const unsigned char* h = (const unsigned char*)haystack;
    const unsigned char* n = (const unsigned char*)needle;
    unsigned char first = n[0];
    size_t max_offset = haystack_len - needle_len;
    for (size_t i = 0; i <= max_offset; ++i) {
        if (h[i] == first && memcmp(h + i, n, needle_len) == 0) {
            return (const void*)(h + i);
        }
    }
    return NULL;
}

static inline int64_t safe_num_to_i64(double d) {
    if (isnan(d) || isinf(d)) return 0;
    if (d > 9223372036854775807.0) return 9223372036854775807LL;
    if (d < -9223372036854775808.0) return (-9223372036854775807LL - 1);
    return (int64_t)d;
}

static UfValue std_split(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'split()' expects two string arguments (string, delimiter)");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    UfStringObject* delim = args[1].as.string;

    UfValue res = uf_val_array(rt, 8);
    uf_runtime_push_temp_root(rt, res);

    if (delim->length == 0) {
        /* Split into individual characters */
        for (size_t i = 0; i < str->length; ++i) {
            char ch[2] = { str->chars[i], '\0' };
            UfValue s = uf_val_string(rt, ch, 1);
            uf_array_push(rt, res.as.array, s);
        }
        uf_runtime_pop_temp_roots(rt, 1);
        return res;
    }

    const char* p = str->chars;
    const char* end = str->chars + str->length;
    while (p < end) {
        size_t rem = (size_t)(end - p);
        const char* next = (const char*)uf_memmem(p, rem, delim->chars, delim->length);
        if (!next) {
            UfValue s = uf_val_string(rt, p, rem);
            uf_array_push(rt, res.as.array, s);
            break;
        }
        UfValue s = uf_val_string(rt, p, (size_t)(next - p));
        uf_array_push(rt, res.as.array, s);
        p = next + delim->length;
        if (p == end) {
            /* Trailing delimiter produces an empty string at the end */
            UfValue empty_str = uf_val_string(rt, "", 0);
            uf_array_push(rt, res.as.array, empty_str);
        }
    }

    uf_runtime_pop_temp_roots(rt, 1);
    return res;
}

static UfValue std_join(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_ARRAY || args[1].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'join()' expects an array and a string delimiter");
        return uf_val_null();
    }
    UfArrayObject* arr = args[0].as.array;
    UfStringObject* sep = args[1].as.string;

    if (arr->count == 0) {
        return uf_val_string(rt, "", 0);
    }

    /* First pass: convert items to strings and calculate total length */
    char** str_items = (char**)malloc(arr->count * sizeof(char*));
    size_t total_len = 0;
    for (size_t i = 0; i < arr->count; ++i) {
        str_items[i] = uf_val_to_string(arr->elements[i]);
        total_len += strlen(str_items[i]);
    }
    total_len += (arr->count - 1) * sep->length;

    char* buffer = (char*)malloc(total_len + 1);
    char* cur = buffer;
    for (size_t i = 0; i < arr->count; ++i) {
        if (i > 0 && sep->length > 0) {
            memcpy(cur, sep->chars, sep->length);
            cur += sep->length;
        }
        size_t len = strlen(str_items[i]);
        memcpy(cur, str_items[i], len);
        cur += len;
        free(str_items[i]);
    }
    free(str_items);
    *cur = '\0';

    UfValue result = uf_val_string(rt, buffer, total_len);
    free(buffer);
    return result;
}

static UfValue std_trim(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'trim()' expects a string");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    const char* start = str->chars;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (*start == '\0') {
        return uf_val_string(rt, "", 0);
    }
    const char* end = str->chars + str->length - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }
    size_t len = (end - start) + 1;
    return uf_val_string(rt, start, len);
}

static UfValue std_replace(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 3 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_STRING || args[2].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'replace()' expects three string arguments (target, old, new)");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    UfStringObject* old_sub = args[1].as.string;
    UfStringObject* new_sub = args[2].as.string;

    if (old_sub->length == 0 || str->length == 0) {
        return args[0];
    }

    /* Count occurrences */
    size_t count = 0;
    const char* p = str->chars;
    const char* end = str->chars + str->length;
    while (p < end) {
        size_t rem = (size_t)(end - p);
        const char* match = (const char*)uf_memmem(p, rem, old_sub->chars, old_sub->length);
        if (!match) break;
        count++;
        p = match + old_sub->length;
    }

    if (count == 0) {
        return args[0];
    }

    size_t new_total_len = str->length + count * (new_sub->length - old_sub->length);
    char* buffer = (char*)malloc(new_total_len + 1);
    char* cur = buffer;
    p = str->chars;

    while (p < end) {
        size_t rem = (size_t)(end - p);
        const char* match = (const char*)uf_memmem(p, rem, old_sub->chars, old_sub->length);
        if (!match) {
            memcpy(cur, p, rem);
            cur += rem;
            break;
        }
        size_t seg = (size_t)(match - p);
        memcpy(cur, p, seg);
        cur += seg;
        if (new_sub->length > 0) {
            memcpy(cur, new_sub->chars, new_sub->length);
            cur += new_sub->length;
        }
        p = match + old_sub->length;
    }
    *cur = '\0';

    UfValue result = uf_val_string(rt, buffer, new_total_len);
    free(buffer);
    return result;
}

static UfValue std_to_upper(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'to_upper()' expects a string");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    char* buf = (char*)malloc(str->length + 1);
    for (size_t i = 0; i < str->length; ++i) {
        buf[i] = (char)toupper((unsigned char)str->chars[i]);
    }
    buf[str->length] = '\0';
    UfValue res = uf_val_string(rt, buf, str->length);
    free(buf);
    return res;
}

static UfValue std_to_lower(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'to_lower()' expects a string");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    char* buf = (char*)malloc(str->length + 1);
    for (size_t i = 0; i < str->length; ++i) {
        buf[i] = (char)tolower((unsigned char)str->chars[i]);
    }
    buf[str->length] = '\0';
    UfValue res = uf_val_string(rt, buf, str->length);
    free(buf);
    return res;
}

static UfValue std_contains(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'contains()' expects two strings (haystack, needle)");
        return uf_val_null();
    }
    return uf_val_bool(uf_memmem(args[0].as.string->chars, args[0].as.string->length,
                                 args[1].as.string->chars, args[1].as.string->length) != NULL);
}

static UfValue std_starts_with(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'starts_with()' expects two strings (target, prefix)");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    UfStringObject* pfx = args[1].as.string;
    if (str->length < pfx->length) return uf_val_bool(false);
    return uf_val_bool(strncmp(str->chars, pfx->chars, pfx->length) == 0);
}

static UfValue std_ends_with(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'ends_with()' expects two strings (target, suffix)");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    UfStringObject* sfx = args[1].as.string;
    if (str->length < sfx->length) return uf_val_bool(false);
    return uf_val_bool(memcmp(str->chars + (str->length - sfx->length), sfx->chars, sfx->length) == 0);
}

static UfValue std_char_at(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'char_at()' expects a string and a numeric index");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    int64_t idx = safe_num_to_i64(args[1].as.number);
    if (idx < 0) idx += str->length;
    if (idx < 0 || (size_t)idx >= str->length) {
        return uf_val_string(rt, "", 0);
    }
    char ch[2] = { str->chars[idx], '\0' };
    return uf_val_string(rt, ch, 1);
}

static UfValue std_to_number(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'to_number()' expects a string");
        return uf_val_null();
    }
    const char* str = args[0].as.string->chars;
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return uf_val_null();

    char* endptr = NULL;
    double val = strtod(str, &endptr);
    if (endptr == str) return uf_val_null();
    while (isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') return uf_val_null();

    return uf_val_number(val);
}

static UfValue std_to_string(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1) {
        return uf_val_string(rt, "null", 4);
    }
    char* str = uf_val_to_string(args[0]);
    UfValue res = uf_val_string(rt, str, strlen(str));
    free(str);
    return res;
}

static UfValue std_repeat_string(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'repeat_string()' expects a string and a number count");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    int64_t count = safe_num_to_i64(args[1].as.number);
    if (count <= 0 || str->length == 0) {
        return uf_val_string(rt, "", 0);
    }
    if (count > 100000) count = 100000; /* Safeguard */

    size_t total_len = str->length * count;
    char* buf = (char*)malloc(total_len + 1);
    char* cur = buf;
    for (int64_t i = 0; i < count; ++i) {
        memcpy(cur, str->chars, str->length);
        cur += str->length;
    }
    *cur = '\0';
    UfValue res = uf_val_string(rt, buf, total_len);
    free(buf);
    return res;
}

static UfValue std_substring(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'substring()' expects a string, a start index, and an optional end index");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    int64_t start = safe_num_to_i64(args[1].as.number);
    int64_t end = (argc >= 3 && args[2].kind == UF_VAL_NUMBER) ? safe_num_to_i64(args[2].as.number) : (int64_t)str->length;

    if (start < 0) start += str->length;
    if (end < 0) end += str->length;

    if (start < 0) start = 0;
    if ((size_t)start > str->length) start = str->length;
    if (end < start) end = start;
    if ((size_t)end > str->length) end = str->length;

    size_t len = (size_t)(end - start);
    return uf_val_string(rt, str->chars + start, len);
}

static UfValue std_index_of(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'index_of()' expects two strings (haystack, needle)");
        return uf_val_null();
    }
    const char* match = (const char*)uf_memmem(args[0].as.string->chars, args[0].as.string->length,
                                               args[1].as.string->chars, args[1].as.string->length);
    if (!match) return uf_val_number(-1);
    return uf_val_number((double)(match - args[0].as.string->chars));
}

static UfValue std_pad_start(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'pad_start()' expects a string and target length");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    int64_t target = safe_num_to_i64(args[1].as.number);
    if (target <= 0 || (size_t)target <= str->length) {
        return args[0];
    }
    const char* pad_chars = " ";
    size_t pad_len = 1;
    if (argc >= 3 && args[2].kind == UF_VAL_STRING && args[2].as.string->length > 0) {
        pad_chars = args[2].as.string->chars;
        pad_len = args[2].as.string->length;
    }
    size_t total = (size_t)target;
    size_t pad_needed = total - str->length;
    char* buf = (char*)malloc(total + 1);
    for (size_t i = 0; i < pad_needed; ++i) {
        buf[i] = pad_chars[i % pad_len];
    }
    memcpy(buf + pad_needed, str->chars, str->length);
    buf[total] = '\0';
    UfValue res = uf_val_string(rt, buf, total);
    free(buf);
    return res;
}

static UfValue std_pad_end(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'pad_end()' expects a string and target length");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    int64_t target = safe_num_to_i64(args[1].as.number);
    if (target <= 0 || (size_t)target <= str->length) {
        return args[0];
    }
    const char* pad_chars = " ";
    size_t pad_len = 1;
    if (argc >= 3 && args[2].kind == UF_VAL_STRING && args[2].as.string->length > 0) {
        pad_chars = args[2].as.string->chars;
        pad_len = args[2].as.string->length;
    }
    size_t total = (size_t)target;
    size_t pad_needed = total - str->length;
    char* buf = (char*)malloc(total + 1);
    memcpy(buf, str->chars, str->length);
    for (size_t i = 0; i < pad_needed; ++i) {
        buf[str->length + i] = pad_chars[i % pad_len];
    }
    buf[total] = '\0';
    UfValue res = uf_val_string(rt, buf, total);
    free(buf);
    return res;
}

static UfValue std_trim_start(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'trim_start()' expects a string");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    const char* start = str->chars;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    size_t len = (str->chars + str->length) - start;
    return uf_val_string(rt, start, len);
}

static UfValue std_trim_end(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'trim_end()' expects a string");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    if (str->length == 0) {
        return args[0];
    }
    const char* start = str->chars;
    const char* end = str->chars + str->length - 1;
    while (end >= start && isspace((unsigned char)*end)) {
        end--;
    }
    if (end < start) {
        return uf_val_string(rt, "", 0);
    }
    size_t len = (size_t)(end - start + 1);
    return uf_val_string(rt, start, len);
}

static UfValue std_chars(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'chars()' expects a string");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    UfValue res = uf_val_array(rt, str->length > 0 ? str->length : 1);
    uf_runtime_push_temp_root(rt, res);
    for (size_t i = 0; i < str->length; ++i) {
        char ch[2] = { str->chars[i], '\0' };
        UfValue s = uf_val_string(rt, ch, 1);
        uf_array_push(rt, res.as.array, s);
    }
    uf_runtime_pop_temp_roots(rt, 1);
    return res;
}

static UfValue std_count(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_STRING || args[1].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'count()' expects two strings (haystack, needle)");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    UfStringObject* sub = args[1].as.string;
    if (sub->length == 0 || str->length < sub->length) {
        return uf_val_number(0);
    }
    size_t cnt = 0;
    const char* p = str->chars;
    const char* end = str->chars + str->length;
    while (p < end) {
        size_t rem = (size_t)(end - p);
        const char* match = (const char*)uf_memmem(p, rem, sub->chars, sub->length);
        if (!match) break;
        cnt++;
        p = match + sub->length;
    }
    return uf_val_number((double)cnt);
}

/* ========================================================================= */
/* ARRAY STANDARD LIBRARY FUNCTIONS                                          */
/* ========================================================================= */

static UfValue std_concat(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_ARRAY || args[1].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'concat()' expects two arrays");
        return uf_val_null();
    }
    UfArrayObject* a1 = args[0].as.array;
    UfArrayObject* a2 = args[1].as.array;
    size_t total = a1->count + a2->count;
    UfValue res = uf_val_array(rt, total > 0 ? total : 1);
    uf_runtime_push_temp_root(rt, res);
    for (size_t i = 0; i < a1->count; ++i) {
        uf_array_push(rt, res.as.array, a1->elements[i]);
    }
    for (size_t i = 0; i < a2->count; ++i) {
        uf_array_push(rt, res.as.array, a2->elements[i]);
    }
    uf_runtime_pop_temp_roots(rt, 1);
    return res;
}

static UfValue std_flatten(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'flatten()' expects an array");
        return uf_val_null();
    }
    UfArrayObject* a = args[0].as.array;
    UfValue res = uf_val_array(rt, a->count > 0 ? a->count : 1);
    uf_runtime_push_temp_root(rt, res);
    for (size_t i = 0; i < a->count; ++i) {
        if (a->elements[i].kind == UF_VAL_ARRAY) {
            UfArrayObject* inner = a->elements[i].as.array;
            for (size_t j = 0; j < inner->count; ++j) {
                uf_array_push(rt, res.as.array, inner->elements[j]);
            }
        } else {
            uf_array_push(rt, res.as.array, a->elements[i]);
        }
    }
    uf_runtime_pop_temp_roots(rt, 1);
    return res;
}

static UfValue std_fill(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'fill()' expects an array and a value");
        return uf_val_null();
    }
    UfArrayObject* a = args[0].as.array;
    for (size_t i = 0; i < a->count; ++i) {
        a->elements[i] = args[1];
    }
    return args[0];
}

static UfValue std_zip(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_ARRAY || args[1].kind != UF_VAL_ARRAY) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'zip()' expects two arrays");
        return uf_val_null();
    }
    UfArrayObject* a1 = args[0].as.array;
    UfArrayObject* a2 = args[1].as.array;
    size_t count = a1->count < a2->count ? a1->count : a2->count;
    UfValue res = uf_val_array(rt, count > 0 ? count : 1);
    uf_runtime_push_temp_root(rt, res);
    for (size_t i = 0; i < count; ++i) {
        UfValue pair = uf_val_array(rt, 2);
        uf_runtime_push_temp_root(rt, pair);
        uf_array_push(rt, pair.as.array, a1->elements[i]);
        uf_array_push(rt, pair.as.array, a2->elements[i]);
        uf_array_push(rt, res.as.array, pair);
        uf_runtime_pop_temp_roots(rt, 1);
    }
    uf_runtime_pop_temp_roots(rt, 1);
    return res;
}

/* ========================================================================= */
/* MATH STANDARD LIBRARY FUNCTIONS                                           */
/* ========================================================================= */

static UfValue std_abs(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'abs()' expects a number");
        return uf_val_null();
    }
    return uf_val_number(fabs(args[0].as.number));
}

static UfValue std_floor(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'floor()' expects a number");
        return uf_val_null();
    }
    return uf_val_number(floor(args[0].as.number));
}

static UfValue std_ceil(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'ceil()' expects a number");
        return uf_val_null();
    }
    return uf_val_number(ceil(args[0].as.number));
}

static UfValue std_round(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'round()' expects a number");
        return uf_val_null();
    }
    return uf_val_number(round(args[0].as.number));
}

static UfValue std_sqrt(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'sqrt()' expects a number");
        return uf_val_null();
    }
    if (args[0].as.number < 0) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'sqrt()' domain error: cannot compute square root of negative number");
        return uf_val_null();
    }
    return uf_val_number(sqrt(args[0].as.number));
}

static UfValue std_pow(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_NUMBER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'pow()' expects two numbers (base, exponent)");
        return uf_val_null();
    }
    return uf_val_number(pow(args[0].as.number, args[1].as.number));
}

static UfValue std_min(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_NUMBER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'min()' expects two numbers");
        return uf_val_null();
    }
    return uf_val_number((args[0].as.number < args[1].as.number) ? args[0].as.number : args[1].as.number);
}

static UfValue std_max(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_NUMBER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'max()' expects two numbers");
        return uf_val_null();
    }
    return uf_val_number((args[0].as.number > args[1].as.number) ? args[0].as.number : args[1].as.number);
}

static UfValue std_log(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'log()' expects a number");
        return uf_val_null();
    }
    if (args[0].as.number <= 0) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'log()' domain error: argument must be positive");
        return uf_val_null();
    }
    return uf_val_number(log(args[0].as.number));
}

static UfValue std_sin(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'sin()' expects a number");
        return uf_val_null();
    }
    return uf_val_number(sin(args[0].as.number));
}

static UfValue std_cos(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'cos()' expects a number");
        return uf_val_null();
    }
    return uf_val_number(cos(args[0].as.number));
}

static UfValue std_tan(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'tan()' expects a number");
        return uf_val_null();
    }
    return uf_val_number(tan(args[0].as.number));
}

static UfValue std_random(UfRuntime* rt, int argc, UfValue* args) {
    UF_UNUSED(rt);
    UF_UNUSED(argc);
    UF_UNUSED(args);
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }
    double r = (double)rand() / ((double)RAND_MAX + 1.0);
    return uf_val_number(r);
}

static UfValue std_random_int(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_NUMBER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'random_int()' expects two numbers (min, max)");
        return uf_val_null();
    }
    int64_t min_v = safe_num_to_i64(args[0].as.number);
    int64_t max_v = safe_num_to_i64(args[1].as.number);
    if (max_v < min_v) {
        int64_t tmp = min_v;
        min_v = max_v;
        max_v = tmp;
    }
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }
    int64_t span = max_v - min_v + 1;
    if (span <= 0) {
        return uf_val_number((double)min_v);
    }
    int64_t res = min_v + (rand() % span);
    return uf_val_number((double)res);
}

static UfValue std_error(UfRuntime* rt, int argc, UfValue* args) {
    const char* msg = "Error";
    if (argc >= 1) {
        if (args[0].kind == UF_VAL_STRING) {
            msg = args[0].as.string->chars;
        } else {
            char* s = uf_val_to_string(args[0]);
            UfValue sv = uf_val_string(rt, s, strlen(s));
            free(s);
            msg = sv.as.string->chars;
        }
    }
    const char* kind = (argc >= 2 && args[1].kind == UF_VAL_STRING) ? args[1].as.string->chars : "UserError";

    SourceLoc loc = source_loc_make("<user>", 0, 0, 0);
    SourceSpan span = source_span_make(loc, loc);
    if (rt->frame_count > 0) {
        span = rt->frames[rt->frame_count - 1].call_span;
    }

    uf_runtime_raise(rt, kind, span, "%s", msg);
    return uf_val_null();
}

/* ========================================================================= */
/* CONCURRENCY / FIBER / CHANNEL FUNCTIONS                                   */
/* ========================================================================= */

static UfValue std_spawn(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'spawn()' expects at least 1 callable argument");
        return uf_val_null();
    }
    UfValue callable = args[0];
    size_t fiber_argc = (size_t)(argc > 1 ? argc - 1 : 0);
    UfValue* fiber_args = (argc > 1) ? &args[1] : NULL;
    UfFiber* fiber = uf_fiber_create(rt, callable, fiber_argc, fiber_args);
    if (!fiber) return uf_val_null();
    uf_scheduler_spawn(rt, fiber);
    return uf_val_fiber(rt, fiber);
}

static UfValue std_yield(UfRuntime* rt, int argc, UfValue* args) {
    UfValue val = (argc > 0) ? args[0] : uf_val_null();
    return uf_scheduler_yield(rt, val);
}

static UfValue std_channel(UfRuntime* rt, int argc, UfValue* args) {
    size_t cap = 0;
    if (argc > 0 && args[0].kind == UF_VAL_NUMBER) {
        if (args[0].as.number > 0) {
            cap = (size_t)args[0].as.number;
        }
    }
    UfChannel* ch = uf_channel_create(rt, cap);
    if (!ch) return uf_val_null();
    return uf_val_channel(rt, ch);
}

static UfValue std_send(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_CHANNEL) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'send()' expects a channel and a value");
        return uf_val_bool(false);
    }
    bool ok = uf_channel_send(rt, args[0].as.channel, args[1]);
    return uf_val_bool(ok);
}

static UfValue std_recv(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_CHANNEL) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'recv()' expects a channel");
        return uf_val_null();
    }
    UfValue out = uf_val_null();
    uf_channel_recv(rt, args[0].as.channel, &out);
    return out;
}

static UfValue std_close_channel(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_CHANNEL) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'close_channel()' expects a channel");
        return uf_val_null();
    }
    uf_channel_close(rt, args[0].as.channel);
    return uf_val_null();
}

static UfValue std_run_scheduler(UfRuntime* rt, int argc, UfValue* args) {
    (void)argc; (void)args;
    int count = uf_scheduler_run(rt);
    return uf_val_number((double)count);
}

static UfValue std_run_async(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'run_async()' expects at least 1 argument");
        return uf_val_null();
    }
    UfValue target = args[0];
    UfValue result = uf_val_null();
    SourceSpan span = source_span_make(source_loc_make("<run_async>", 0, 0, 0), source_loc_make("<run_async>", 0, 0, 0));

    if (target.kind == UF_VAL_PROMISE && target.as.promise) {
        result = uf_promise_await(rt, target.as.promise);
    } else {
        size_t call_argc = (size_t)(argc > 1 ? argc - 1 : 0);
        UfValue* call_args = (argc > 1) ? &args[1] : NULL;
        UfValue val = uf_runtime_call(rt, target, call_argc, call_args, span);
        if (val.kind == UF_VAL_PROMISE && val.as.promise) {
            result = uf_promise_await(rt, val.as.promise);
        } else {
            result = val;
        }
    }
    uf_scheduler_run(rt);
    return result;
}

/* ========================================================================= */
/* SYSTEMS PROGRAMMING / LOW-LEVEL BUFFER / MEMORY FUNCTIONS                 */
/* ========================================================================= */

static UfValue std_buffer(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer()' expects a size number");
        return uf_val_null();
    }
    double sz = args[0].as.number;
    if (sz < 0) sz = 0;
    return uf_val_buffer(rt, (size_t)sz);
}

static UfValue std_buffer_from_string(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_from_string()' expects a string");
        return uf_val_null();
    }
    UfStringObject* str = args[0].as.string;
    return uf_val_buffer_from_bytes(rt, (const uint8_t*)str->chars, str->length);
}

static UfValue std_buffer_to_string(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_BUFFER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_to_string()' expects a buffer");
        return uf_val_null();
    }
    UfBufferObject* buf = args[0].as.buffer;
    return uf_val_string(rt, (const char*)buf->data, buf->size);
}

static UfValue std_buffer_size(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_BUFFER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_size()' expects a buffer");
        return uf_val_null();
    }
    return uf_val_number((double)args[0].as.buffer->size);
}

static UfValue std_buffer_get(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_BUFFER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_get()' expects a buffer and an offset number");
        return uf_val_null();
    }
    UfBufferObject* buf = args[0].as.buffer;
    int64_t offset = safe_num_to_i64(args[1].as.number);
    if (offset < 0 || (size_t)offset >= buf->size) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "IndexOutOfBounds: Buffer offset %ld out of bounds (size %zu)", (long)offset, buf->size);
        return uf_val_null();
    }
    return uf_val_number((double)buf->data[offset]);
}

static UfValue std_buffer_set(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 3 || args[0].kind != UF_VAL_BUFFER || args[1].kind != UF_VAL_NUMBER || args[2].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_set()' expects buffer, offset number, and byte value");
        return uf_val_null();
    }
    UfBufferObject* buf = args[0].as.buffer;
    int64_t offset = safe_num_to_i64(args[1].as.number);
    if (offset < 0 || (size_t)offset >= buf->size) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "IndexOutOfBounds: Buffer offset %ld out of bounds (size %zu)", (long)offset, buf->size);
        return uf_val_null();
    }
    uint8_t byte_val = (uint8_t)((int)args[2].as.number & 0xFF);
    buf->data[offset] = byte_val;
    return args[2];
}

static UfValue std_buffer_fill(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_BUFFER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_fill()' expects a buffer and a byte value");
        return uf_val_null();
    }
    UfBufferObject* buf = args[0].as.buffer;
    uint8_t byte_val = (uint8_t)((int)args[1].as.number & 0xFF);
    if (buf->size > 0 && buf->data) {
        memset(buf->data, byte_val, buf->size);
    }
    return args[0];
}

static UfValue std_buffer_slice(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_BUFFER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_slice()' expects buffer and start offset");
        return uf_val_null();
    }
    UfBufferObject* buf = args[0].as.buffer;
    int64_t start = safe_num_to_i64(args[1].as.number);
    if (start < 0) start = 0;
    if ((size_t)start > buf->size) start = (int64_t)buf->size;

    size_t length = buf->size - (size_t)start;
    if (argc >= 3 && args[2].kind == UF_VAL_NUMBER) {
        int64_t user_len = safe_num_to_i64(args[2].as.number);
        if (user_len < 0) user_len = 0;
        if ((size_t)user_len < length) length = (size_t)user_len;
    }

    const uint8_t* src = (length > 0 && buf->data) ? buf->data + start : NULL;
    return uf_val_buffer_from_bytes(rt, src, length);
}

static UfValue std_buffer_read_u16_le(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_BUFFER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_read_u16_le()' expects buffer and offset");
        return uf_val_null();
    }
    UfBufferObject* buf = args[0].as.buffer;
    int64_t offset = safe_num_to_i64(args[1].as.number);
    if (offset < 0 || (size_t)(offset + 2) > buf->size) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "IndexOutOfBounds: Read past buffer end (offset %ld + 2 > size %zu)", (long)offset, buf->size);
        return uf_val_null();
    }
    uint16_t v = (uint16_t)(buf->data[offset] | (buf->data[offset + 1] << 8));
    return uf_val_number((double)v);
}

static UfValue std_buffer_write_u16_le(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 3 || args[0].kind != UF_VAL_BUFFER || args[1].kind != UF_VAL_NUMBER || args[2].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_write_u16_le()' expects buffer, offset, and value");
        return uf_val_null();
    }
    UfBufferObject* buf = args[0].as.buffer;
    int64_t offset = safe_num_to_i64(args[1].as.number);
    if (offset < 0 || (size_t)(offset + 2) > buf->size) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "IndexOutOfBounds: Write past buffer end (offset %ld + 2 > size %zu)", (long)offset, buf->size);
        return uf_val_null();
    }
    uint16_t v = (uint16_t)(uint32_t)args[2].as.number;
    buf->data[offset] = (uint8_t)(v & 0xFF);
    buf->data[offset + 1] = (uint8_t)((v >> 8) & 0xFF);
    return args[2];
}

static UfValue std_buffer_read_u32_le(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_BUFFER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_read_u32_le()' expects buffer and offset");
        return uf_val_null();
    }
    UfBufferObject* buf = args[0].as.buffer;
    int64_t offset = safe_num_to_i64(args[1].as.number);
    if (offset < 0 || (size_t)(offset + 4) > buf->size) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "IndexOutOfBounds: Read past buffer end (offset %ld + 4 > size %zu)", (long)offset, buf->size);
        return uf_val_null();
    }
    uint32_t v = (uint32_t)(buf->data[offset]) |
                 ((uint32_t)(buf->data[offset + 1]) << 8) |
                 ((uint32_t)(buf->data[offset + 2]) << 16) |
                 ((uint32_t)(buf->data[offset + 3]) << 24);
    return uf_val_number((double)v);
}

static UfValue std_buffer_write_u32_le(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 3 || args[0].kind != UF_VAL_BUFFER || args[1].kind != UF_VAL_NUMBER || args[2].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_write_u32_le()' expects buffer, offset, and value");
        return uf_val_null();
    }
    UfBufferObject* buf = args[0].as.buffer;
    int64_t offset = safe_num_to_i64(args[1].as.number);
    if (offset < 0 || (size_t)(offset + 4) > buf->size) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "IndexOutOfBounds: Write past buffer end (offset %ld + 4 > size %zu)", (long)offset, buf->size);
        return uf_val_null();
    }
    uint32_t v = (uint32_t)(uint64_t)args[2].as.number;
    buf->data[offset] = (uint8_t)(v & 0xFF);
    buf->data[offset + 1] = (uint8_t)((v >> 8) & 0xFF);
    buf->data[offset + 2] = (uint8_t)((v >> 16) & 0xFF);
    buf->data[offset + 3] = (uint8_t)((v >> 24) & 0xFF);
    return args[2];
}

static UfValue std_buffer_read_i32_le(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_BUFFER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_read_i32_le()' expects buffer and offset");
        return uf_val_null();
    }
    UfBufferObject* buf = args[0].as.buffer;
    int64_t offset = safe_num_to_i64(args[1].as.number);
    if (offset < 0 || (size_t)(offset + 4) > buf->size) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "IndexOutOfBounds: Read past buffer end (offset %ld + 4 > size %zu)", (long)offset, buf->size);
        return uf_val_null();
    }
    int32_t v = (int32_t)((uint32_t)(buf->data[offset]) |
                          ((uint32_t)(buf->data[offset + 1]) << 8) |
                          ((uint32_t)(buf->data[offset + 2]) << 16) |
                          ((uint32_t)(buf->data[offset + 3]) << 24));
    return uf_val_number((double)v);
}


static UfValue std_buffer_write_i32_le(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 3 || args[0].kind != UF_VAL_BUFFER || args[1].kind != UF_VAL_NUMBER || args[2].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_write_i32_le()' expects buffer, offset, and value");
        return uf_val_null();
    }
    UfBufferObject* buf = args[0].as.buffer;
    int64_t offset = safe_num_to_i64(args[1].as.number);
    if (offset < 0 || (size_t)(offset + 4) > buf->size) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "IndexOutOfBounds: Write past buffer end (offset %ld + 4 > size %zu)", (long)offset, buf->size);
        return uf_val_null();
    }
    int32_t v = (int32_t)safe_num_to_i64(args[2].as.number);
    buf->data[offset] = (uint8_t)(v & 0xFF);
    buf->data[offset + 1] = (uint8_t)((v >> 8) & 0xFF);
    buf->data[offset + 2] = (uint8_t)((v >> 16) & 0xFF);
    buf->data[offset + 3] = (uint8_t)((v >> 24) & 0xFF);
    return args[2];
}

static UfValue std_u8(UfRuntime* rt, int argc, UfValue* args) {
    (void)rt;
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) return uf_val_number(0);
    return uf_val_number((double)((uint8_t)safe_num_to_i64(args[0].as.number)));
}

static UfValue std_i8(UfRuntime* rt, int argc, UfValue* args) {
    (void)rt;
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) return uf_val_number(0);
    return uf_val_number((double)((int8_t)safe_num_to_i64(args[0].as.number)));
}

static UfValue std_u16(UfRuntime* rt, int argc, UfValue* args) {
    (void)rt;
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) return uf_val_number(0);
    return uf_val_number((double)((uint16_t)safe_num_to_i64(args[0].as.number)));
}

static UfValue std_i16(UfRuntime* rt, int argc, UfValue* args) {
    (void)rt;
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) return uf_val_number(0);
    return uf_val_number((double)((int16_t)safe_num_to_i64(args[0].as.number)));
}

static UfValue std_u32(UfRuntime* rt, int argc, UfValue* args) {
    (void)rt;
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) return uf_val_number(0);
    return uf_val_number((double)((uint32_t)safe_num_to_i64(args[0].as.number)));
}

static UfValue std_i32(UfRuntime* rt, int argc, UfValue* args) {
    (void)rt;
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) return uf_val_number(0);
    return uf_val_number((double)((int32_t)safe_num_to_i64(args[0].as.number)));
}

static UfValue std_band(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_NUMBER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'band()' expects two numbers");
        return uf_val_null();
    }
    uint32_t a = (uint32_t)safe_num_to_i64(args[0].as.number);
    uint32_t b = (uint32_t)safe_num_to_i64(args[1].as.number);
    return uf_val_number((double)(a & b));
}

static UfValue std_bor(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_NUMBER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'bor()' expects two numbers");
        return uf_val_null();
    }
    uint32_t a = (uint32_t)safe_num_to_i64(args[0].as.number);
    uint32_t b = (uint32_t)safe_num_to_i64(args[1].as.number);
    return uf_val_number((double)(a | b));
}

static UfValue std_bxor(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_NUMBER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'bxor()' expects two numbers");
        return uf_val_null();
    }
    uint32_t a = (uint32_t)safe_num_to_i64(args[0].as.number);
    uint32_t b = (uint32_t)safe_num_to_i64(args[1].as.number);
    return uf_val_number((double)(a ^ b));
}

static UfValue std_bnot(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'bnot()' expects a number");
        return uf_val_null();
    }
    uint32_t a = (uint32_t)safe_num_to_i64(args[0].as.number);
    return uf_val_number((double)(~a));
}

static UfValue std_shl(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_NUMBER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'shl()' expects two numbers");
        return uf_val_null();
    }
    uint32_t a = (uint32_t)safe_num_to_i64(args[0].as.number);
    uint32_t b = (uint32_t)safe_num_to_i64(args[1].as.number) & 31;
    return uf_val_number((double)(a << b));
}

static UfValue std_shr(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_NUMBER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'shr()' expects two numbers");
        return uf_val_null();
    }
    uint32_t a = (uint32_t)safe_num_to_i64(args[0].as.number);
    uint32_t b = (uint32_t)safe_num_to_i64(args[1].as.number) & 31;
    return uf_val_number((double)(a >> b));
}

static UfValue std_sar(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 2 || args[0].kind != UF_VAL_NUMBER || args[1].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'sar()' expects two numbers");
        return uf_val_null();
    }
    int32_t a = (int32_t)safe_num_to_i64(args[0].as.number);
    uint32_t b = (uint32_t)safe_num_to_i64(args[1].as.number) & 31;
    return uf_val_number((double)(a >> b));
}

static UfValue std_to_hex(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_NUMBER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'to_hex()' expects a number");
        return uf_val_null();
    }
    uint64_t v = (uint64_t)safe_num_to_i64(args[0].as.number);
    char buf[32];
    snprintf(buf, sizeof(buf), "%lx", (unsigned long)v);
    return uf_val_string(rt, buf, strlen(buf));
}

static UfValue std_from_hex(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'from_hex()' expects a string");
        return uf_val_null();
    }
    const char* s = args[0].as.string->chars;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    char* endptr = NULL;
    unsigned long long val = strtoull(s, &endptr, 16);
    if (endptr == s) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "ValueError: Invalid hex string '%s'", args[0].as.string->chars);
        return uf_val_null();
    }
    return uf_val_number((double)val);
}

static UfValue std_buffer_to_hex(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_BUFFER) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_to_hex()' expects a buffer");
        return uf_val_null();
    }
    UfBufferObject* b = args[0].as.buffer;
    if (b->size == 0) return uf_val_string(rt, "", 0);
    size_t hex_len = b->size * 2;
    char* hex = (char*)malloc(hex_len + 1);
    if (!hex) return uf_val_null();
    static const char hex_digits[] = "0123456789abcdef";
    for (size_t i = 0; i < b->size; ++i) {
        hex[i * 2] = hex_digits[(b->data[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hex_digits[b->data[i] & 0x0F];
    }
    hex[hex_len] = '\0';
    UfValue res = uf_val_string(rt, hex, hex_len);
    free(hex);
    return res;
}

static int hex_char_to_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static UfValue std_buffer_from_hex(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1 || args[0].kind != UF_VAL_STRING) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "'buffer_from_hex()' expects a hex string");
        return uf_val_null();
    }
    const char* s = args[0].as.string->chars;
    size_t len = args[0].as.string->length;
    if (len % 2 != 0) {
        SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
        uf_runtime_error(rt, source_span_make(loc, loc), "ValueError: Hex string must have an even length");
        return uf_val_null();
    }
    size_t buf_len = len / 2;
    UfValue bval = uf_val_buffer(rt, buf_len);
    UfBufferObject* buf = bval.as.buffer;
    for (size_t i = 0; i < buf_len; ++i) {
        int hi = hex_char_to_val(s[i * 2]);
        int lo = hex_char_to_val(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            SourceLoc loc = source_loc_make("<native>", 0, 0, 0);
            uf_runtime_error(rt, source_span_make(loc, loc), "ValueError: Invalid hex character in '%s'", s);
            return uf_val_null();
        }
        buf->data[i] = (uint8_t)((hi << 4) | lo);
    }
    return bval;
}

static UfValue std_inspect(UfRuntime* rt, int argc, UfValue* args) {
    if (argc < 1) return uf_val_null();
    UfValue v = args[0];

    UfValue map_val = uf_val_map(rt, 8);
    uf_runtime_push_temp_root(rt, map_val);
    UfMapObject* map = map_val.as.map;

    /* Root the type name: allocating the key may collect. */
    UfValue type_name = uf_val_string_cstr(rt, uf_val_type_name(v));
    uf_runtime_push_temp_root(rt, type_name);
    uf_map_set(rt, map, uf_val_string_cstr(rt, "type"), type_name);
    uf_runtime_pop_temp_root(rt);

    size_t sz = sizeof(UfValue);
    bool marked = false;

    switch (v.kind) {
        case UF_VAL_STRING:
            if (v.as.string) {
                sz = sizeof(UfStringObject) + v.as.string->length + 1;
                marked = v.as.string->obj.marked;
                uf_map_set(rt, map, uf_val_string_cstr(rt, "length"), uf_val_number((double)v.as.string->length));
            }
            break;
        case UF_VAL_ARRAY:
            if (v.as.array) {
                sz = sizeof(UfArrayObject) + v.as.array->capacity * sizeof(UfValue);
                marked = v.as.array->obj.marked;
                uf_map_set(rt, map, uf_val_string_cstr(rt, "count"), uf_val_number((double)v.as.array->count));
                uf_map_set(rt, map, uf_val_string_cstr(rt, "capacity"), uf_val_number((double)v.as.array->capacity));
            }
            break;
        case UF_VAL_MAP:
            if (v.as.map) {
                sz = sizeof(UfMapObject) + v.as.map->capacity * sizeof(UfMapEntry);
                marked = v.as.map->obj.marked;
                uf_map_set(rt, map, uf_val_string_cstr(rt, "count"), uf_val_number((double)v.as.map->count));
                uf_map_set(rt, map, uf_val_string_cstr(rt, "capacity"), uf_val_number((double)v.as.map->capacity));
            }
            break;
        case UF_VAL_BUFFER:
            if (v.as.buffer) {
                sz = sizeof(UfBufferObject) + v.as.buffer->size;
                marked = v.as.buffer->obj.marked;
                uf_map_set(rt, map, uf_val_string_cstr(rt, "buffer_size"), uf_val_number((double)v.as.buffer->size));
            }
            break;
        case UF_VAL_INSTANCE:
            if (v.as.instance) {
                sz = sizeof(UfInstanceObject) + v.as.instance->field_count * sizeof(UfValue);
                marked = v.as.instance->obj.marked;
                uf_map_set(rt, map, uf_val_string_cstr(rt, "field_count"), uf_val_number((double)v.as.instance->field_count));
            }
            break;
        case UF_VAL_CLOSURE:
            if (v.as.closure) {
                sz = sizeof(UfClosureObject) + v.as.closure->upvalue_count * sizeof(UfUpvalueCell*);
                marked = v.as.closure->obj.marked;
                uf_map_set(rt, map, uf_val_string_cstr(rt, "upvalue_count"), uf_val_number((double)v.as.closure->upvalue_count));
            }
            break;
        case UF_VAL_FIBER:
            if (v.as.fiber) {
                sz = sizeof(UfFiber) + v.as.fiber->argc * sizeof(UfValue);
                marked = v.as.fiber->obj.marked;
                uf_map_set(rt, map, uf_val_string_cstr(rt, "fiber_id"), uf_val_number((double)v.as.fiber->id));
            }
            break;
        case UF_VAL_CHANNEL:
            if (v.as.channel) {
                sz = sizeof(UfChannel) + v.as.channel->capacity * sizeof(UfValue);
                marked = v.as.channel->obj.marked;
                uf_map_set(rt, map, uf_val_string_cstr(rt, "count"), uf_val_number((double)v.as.channel->count));
                uf_map_set(rt, map, uf_val_string_cstr(rt, "capacity"), uf_val_number((double)v.as.channel->capacity));
            }
            break;
        default:
            break;
    }

    uf_map_set(rt, map, uf_val_string_cstr(rt, "size_bytes"), uf_val_number((double)sz));
    uf_map_set(rt, map, uf_val_string_cstr(rt, "marked"), uf_val_bool(marked));

    uf_runtime_pop_temp_roots(rt, 1);
    return map_val;
}

/* ========================================================================= */
/* REGISTRATION                                                              */
/* ========================================================================= */

void uf_stdlib_register_runtime(UfRuntime* rt) {
    /* String functions */
    uf_env_declare(rt->global_env, "split",         uf_val_native("split",         std_split,         2));
    uf_env_declare(rt->global_env, "join",          uf_val_native("join",          std_join,          2));
    uf_env_declare(rt->global_env, "trim",          uf_val_native("trim",          std_trim,          1));
    uf_env_declare(rt->global_env, "replace",       uf_val_native("replace",       std_replace,       3));
    uf_env_declare(rt->global_env, "to_upper",      uf_val_native("to_upper",      std_to_upper,      1));
    uf_env_declare(rt->global_env, "to_lower",      uf_val_native("to_lower",      std_to_lower,      1));
    uf_env_declare(rt->global_env, "contains",      uf_val_native("contains",      std_contains,      2));
    uf_env_declare(rt->global_env, "starts_with",   uf_val_native("starts_with",   std_starts_with,   2));
    uf_env_declare(rt->global_env, "ends_with",     uf_val_native("ends_with",     std_ends_with,     2));
    uf_env_declare(rt->global_env, "char_at",       uf_val_native("char_at",       std_char_at,       2));
    uf_env_declare(rt->global_env, "to_number",     uf_val_native("to_number",     std_to_number,     1));
    uf_env_declare(rt->global_env, "to_string",     uf_val_native("to_string",     std_to_string,     1));
    uf_env_declare(rt->global_env, "repeat_string", uf_val_native("repeat_string", std_repeat_string, 2));
    uf_env_declare(rt->global_env, "substring",     uf_val_native("substring",     std_substring,     -1));
    uf_env_declare(rt->global_env, "index_of",      uf_val_native("index_of",      std_index_of,      2));
    uf_env_declare(rt->global_env, "pad_start",     uf_val_native("pad_start",     std_pad_start,     -1));
    uf_env_declare(rt->global_env, "pad_end",       uf_val_native("pad_end",       std_pad_end,       -1));
    uf_env_declare(rt->global_env, "trim_start",    uf_val_native("trim_start",    std_trim_start,    1));
    uf_env_declare(rt->global_env, "trim_end",      uf_val_native("trim_end",      std_trim_end,      1));
    uf_env_declare(rt->global_env, "chars",         uf_val_native("chars",         std_chars,         1));
    uf_env_declare(rt->global_env, "count",         uf_val_native("count",         std_count,         2));

    /* Array functions */
    uf_env_declare(rt->global_env, "concat",        uf_val_native("concat",        std_concat,        2));
    uf_env_declare(rt->global_env, "flatten",       uf_val_native("flatten",       std_flatten,       1));
    uf_env_declare(rt->global_env, "fill",          uf_val_native("fill",          std_fill,          2));
    uf_env_declare(rt->global_env, "zip",           uf_val_native("zip",           std_zip,           2));

    /* Math functions */
    uf_env_declare(rt->global_env, "abs",        uf_val_native("abs",        std_abs,        1));
    uf_env_declare(rt->global_env, "floor",      uf_val_native("floor",      std_floor,      1));
    uf_env_declare(rt->global_env, "ceil",       uf_val_native("ceil",       std_ceil,       1));
    uf_env_declare(rt->global_env, "round",      uf_val_native("round",      std_round,      1));
    uf_env_declare(rt->global_env, "sqrt",       uf_val_native("sqrt",       std_sqrt,       1));
    uf_env_declare(rt->global_env, "pow",        uf_val_native("pow",        std_pow,        2));
    uf_env_declare(rt->global_env, "min",        uf_val_native("min",        std_min,        2));
    uf_env_declare(rt->global_env, "max",        uf_val_native("max",        std_max,        2));
    uf_env_declare(rt->global_env, "log",        uf_val_native("log",        std_log,        1));
    uf_env_declare(rt->global_env, "sin",        uf_val_native("sin",        std_sin,        1));
    uf_env_declare(rt->global_env, "cos",        uf_val_native("cos",        std_cos,        1));
    uf_env_declare(rt->global_env, "tan",        uf_val_native("tan",        std_tan,        1));
    uf_env_declare(rt->global_env, "random",     uf_val_native("random",     std_random,     0));
    uf_env_declare(rt->global_env, "random_int", uf_val_native("random_int", std_random_int, 2));

    /* Math constants */
    uf_env_declare(rt->global_env, "PI",       uf_val_number(3.14159265358979323846));
    uf_env_declare(rt->global_env, "E",        uf_val_number(2.71828182845904523536));
    uf_env_declare(rt->global_env, "INFINITY", uf_val_number(HUGE_VAL));

    /* Concurrency functions */
    uf_env_declare(rt->global_env, "spawn",         uf_val_native("spawn",         std_spawn,         -1));
    uf_env_declare(rt->global_env, "yield",         uf_val_native("yield",         std_yield,         -1));
    uf_env_declare(rt->global_env, "channel",       uf_val_native("channel",       std_channel,       -1));
    uf_env_declare(rt->global_env, "send",          uf_val_native("send",          std_send,          2));
    uf_env_declare(rt->global_env, "recv",          uf_val_native("recv",          std_recv,          1));
    uf_env_declare(rt->global_env, "close_channel", uf_val_native("close_channel", std_close_channel, 1));
    uf_env_declare(rt->global_env, "run_scheduler", uf_val_native("run_scheduler", std_run_scheduler, 0));
    uf_env_declare(rt->global_env, "run_async",     uf_val_native("run_async",     std_run_async,     -1));

    /* Systems / Buffer / Low-Level functions */
    uf_env_declare(rt->global_env, "buffer",              uf_val_native("buffer",              std_buffer,              1));
    uf_env_declare(rt->global_env, "buffer_from_string",  uf_val_native("buffer_from_string",  std_buffer_from_string,  1));
    uf_env_declare(rt->global_env, "buffer_to_string",    uf_val_native("buffer_to_string",    std_buffer_to_string,    1));
    uf_env_declare(rt->global_env, "buffer_size",         uf_val_native("buffer_size",         std_buffer_size,         1));
    uf_env_declare(rt->global_env, "buffer_get",          uf_val_native("buffer_get",          std_buffer_get,          2));
    uf_env_declare(rt->global_env, "buffer_set",          uf_val_native("buffer_set",          std_buffer_set,          3));
    uf_env_declare(rt->global_env, "buffer_fill",         uf_val_native("buffer_fill",         std_buffer_fill,         2));
    uf_env_declare(rt->global_env, "buffer_slice",        uf_val_native("buffer_slice",        std_buffer_slice,        -1));
    uf_env_declare(rt->global_env, "buffer_read_u16_le",  uf_val_native("buffer_read_u16_le",  std_buffer_read_u16_le,  2));
    uf_env_declare(rt->global_env, "buffer_write_u16_le", uf_val_native("buffer_write_u16_le", std_buffer_write_u16_le, 3));
    uf_env_declare(rt->global_env, "buffer_read_u32_le",  uf_val_native("buffer_read_u32_le",  std_buffer_read_u32_le,  2));
    uf_env_declare(rt->global_env, "buffer_write_u32_le", uf_val_native("buffer_write_u32_le", std_buffer_write_u32_le, 3));
    uf_env_declare(rt->global_env, "buffer_read_i32_le",  uf_val_native("buffer_read_i32_le",  std_buffer_read_i32_le,  2));
    uf_env_declare(rt->global_env, "buffer_write_i32_le", uf_val_native("buffer_write_i32_le", std_buffer_write_i32_le, 3));
    uf_env_declare(rt->global_env, "u8",                  uf_val_native("u8",                  std_u8,                  1));
    uf_env_declare(rt->global_env, "i8",                  uf_val_native("i8",                  std_i8,                  1));
    uf_env_declare(rt->global_env, "u16",                 uf_val_native("u16",                 std_u16,                 1));
    uf_env_declare(rt->global_env, "i16",                 uf_val_native("i16",                 std_i16,                 1));
    uf_env_declare(rt->global_env, "u32",                 uf_val_native("u32",                 std_u32,                 1));
    uf_env_declare(rt->global_env, "i32",                 uf_val_native("i32",                 std_i32,                 1));
    uf_env_declare(rt->global_env, "band",                uf_val_native("band",                std_band,                2));
    uf_env_declare(rt->global_env, "bor",                 uf_val_native("bor",                 std_bor,                 2));
    uf_env_declare(rt->global_env, "bxor",                uf_val_native("bxor",                std_bxor,                2));
    uf_env_declare(rt->global_env, "bnot",                uf_val_native("bnot",                std_bnot,                1));
    uf_env_declare(rt->global_env, "shl",                 uf_val_native("shl",                 std_shl,                 2));
    uf_env_declare(rt->global_env, "shr",                 uf_val_native("shr",                 std_shr,                 2));
    uf_env_declare(rt->global_env, "sar",                 uf_val_native("sar",                 std_sar,                 2));
    uf_env_declare(rt->global_env, "to_hex",              uf_val_native("to_hex",              std_to_hex,              1));
    uf_env_declare(rt->global_env, "from_hex",            uf_val_native("from_hex",            std_from_hex,            1));
    uf_env_declare(rt->global_env, "buffer_to_hex",       uf_val_native("buffer_to_hex",       std_buffer_to_hex,       1));
    uf_env_declare(rt->global_env, "buffer_from_hex",     uf_val_native("buffer_from_hex",     std_buffer_from_hex,     1));
    uf_env_declare(rt->global_env, "inspect",             uf_val_native("inspect",             std_inspect,             1));

    /* Error handling */
    uf_env_declare(rt->global_env, "error", uf_val_native("error", std_error, -1));
}

void uf_stdlib_register_semantic(struct UfSemanticAnalyzer* analyzer) {
    SourceLoc loc = source_loc_make("<stdlib>", 0, 0, 0);
    SourceSpan span = source_span_make(loc, loc);

    /* String functions */
    uf_semantic_add_symbol(analyzer, "split",         UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "join",          UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "trim",          UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "replace",       UF_SYM_BUILTIN, span, 3);
    uf_semantic_add_symbol(analyzer, "to_upper",      UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "to_lower",      UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "contains",      UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "starts_with",   UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "ends_with",     UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "char_at",       UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "to_number",     UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "to_string",     UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "repeat_string", UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "substring",     UF_SYM_BUILTIN, span, -1);
    uf_semantic_add_symbol(analyzer, "index_of",      UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "pad_start",     UF_SYM_BUILTIN, span, -1);
    uf_semantic_add_symbol(analyzer, "pad_end",       UF_SYM_BUILTIN, span, -1);
    uf_semantic_add_symbol(analyzer, "trim_start",    UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "trim_end",      UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "chars",         UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "count",         UF_SYM_BUILTIN, span, 2);

    /* Array functions */
    uf_semantic_add_symbol(analyzer, "concat",        UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "flatten",       UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "fill",          UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "zip",           UF_SYM_BUILTIN, span, 2);

    /* Math functions */
    uf_semantic_add_symbol(analyzer, "abs",        UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "floor",      UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "ceil",       UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "round",      UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "sqrt",       UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "pow",        UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "min",        UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "max",        UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "log",        UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "sin",        UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "cos",        UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "tan",        UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "random",     UF_SYM_BUILTIN, span, 0);
    uf_semantic_add_symbol(analyzer, "random_int", UF_SYM_BUILTIN, span, 2);

    /* Math constants */
    uf_semantic_add_symbol(analyzer, "PI",       UF_SYM_VAR, span, -1);
    uf_semantic_add_symbol(analyzer, "E",        UF_SYM_VAR, span, -1);
    uf_semantic_add_symbol(analyzer, "INFINITY", UF_SYM_VAR, span, -1);

    /* Concurrency functions */
    uf_semantic_add_symbol(analyzer, "spawn",         UF_SYM_BUILTIN, span, -1);
    uf_semantic_add_symbol(analyzer, "yield",         UF_SYM_BUILTIN, span, -1);
    uf_semantic_add_symbol(analyzer, "channel",       UF_SYM_BUILTIN, span, -1);
    uf_semantic_add_symbol(analyzer, "send",          UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "recv",          UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "close_channel", UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "run_scheduler", UF_SYM_BUILTIN, span, 0);

    /* Systems / Buffer / Low-Level functions */
    uf_semantic_add_symbol(analyzer, "buffer",              UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "buffer_from_string",  UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "buffer_to_string",    UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "buffer_size",         UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "buffer_get",          UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "buffer_set",          UF_SYM_BUILTIN, span, 3);
    uf_semantic_add_symbol(analyzer, "buffer_fill",         UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "buffer_slice",        UF_SYM_BUILTIN, span, -1);
    uf_semantic_add_symbol(analyzer, "buffer_read_u16_le",  UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "buffer_write_u16_le", UF_SYM_BUILTIN, span, 3);
    uf_semantic_add_symbol(analyzer, "buffer_read_u32_le",  UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "buffer_write_u32_le", UF_SYM_BUILTIN, span, 3);
    uf_semantic_add_symbol(analyzer, "buffer_read_i32_le",  UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "buffer_write_i32_le", UF_SYM_BUILTIN, span, 3);
    uf_semantic_add_symbol(analyzer, "u8",                  UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "i8",                  UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "u16",                 UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "i16",                 UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "u32",                 UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "i32",                 UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "band",                UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "bor",                 UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "bxor",                UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "bnot",                UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "shl",                 UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "shr",                 UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "sar",                 UF_SYM_BUILTIN, span, 2);
    uf_semantic_add_symbol(analyzer, "to_hex",              UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "from_hex",            UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "buffer_to_hex",       UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "buffer_from_hex",     UF_SYM_BUILTIN, span, 1);
    uf_semantic_add_symbol(analyzer, "inspect",             UF_SYM_BUILTIN, span, 1);

    /* Error handling */
    uf_semantic_add_symbol(analyzer, "error", UF_SYM_BUILTIN, span, -1);
}
