#include "uf_value.h"
#include "uf_runtime.h"
#include "uf_env.h"
#include <math.h>

UfValue uf_val_null(void) {
    UfValue v;
    v.kind = UF_VAL_NULL;
    v.as.number = 0;
    return v;
}

UfValue uf_val_bool(bool b) {
    UfValue v;
    v.kind = UF_VAL_BOOL;
    v.as.boolean = b;
    return v;
}

UfValue uf_val_number(double n) {
    UfValue v;
    v.kind = UF_VAL_NUMBER;
    v.as.number = n;
    return v;
}

UfValue uf_val_string(UfRuntime* rt, const char* chars, size_t len) {
    size_t size = sizeof(UfStringObject) + len + 1;
    UfStringObject* obj = (UfStringObject*)malloc(size);
    if (!obj) {
        fprintf(stderr, "Fatal error: Out of memory allocating string\n");
        abort();
    }
    obj->obj.kind = UF_OBJ_STRING;
    obj->obj.marked = false;
    obj->obj.next = NULL;
    obj->length = len;
    memcpy(obj->chars, chars, len);
    obj->chars[len] = '\0';

    if (rt) {
        uf_runtime_register_obj(rt, (UfObj*)obj, size);
    }

    UfValue v;
    v.kind = UF_VAL_STRING;
    v.as.string = obj;
    return v;
}

UfValue uf_val_string_take(UfRuntime* rt, char* chars, size_t len) {
    UfValue v = uf_val_string(rt, chars, len);
    free(chars);
    return v;
}

UfValue uf_val_string_cstr(UfRuntime* rt, const char* cstr) {
    return uf_val_string(rt, cstr, strlen(cstr));
}

UfValue uf_val_function(UfRuntime* rt, const char* name, const char** params, size_t param_count, struct UfStmt* body, UfEnv* closure_env) {
    size_t size = sizeof(UfFunctionObject);
    UfFunctionObject* fn = (UfFunctionObject*)malloc(size);
    if (!fn) {
        fprintf(stderr, "Fatal error: Out of memory allocating function\n");
        abort();
    }
    fn->obj.kind = UF_OBJ_FUNCTION;
    fn->obj.marked = false;
    fn->obj.next = NULL;
    fn->name = name;
    fn->params = params;
    fn->param_count = param_count;
    fn->body = body;
    fn->closure_env = closure_env;

    if (rt) {
        uf_runtime_register_obj(rt, (UfObj*)fn, size);
    }

    UfValue v;
    v.kind = UF_VAL_FUNCTION;
    v.as.function = fn;
    return v;
}

UfValue uf_val_native(const char* name, UfNativeFn fn, int arity) {
    UfValue v;
    v.kind = UF_VAL_NATIVE_FN;
    v.as.native_fn.name = name;
    v.as.native_fn.fn = fn;
    v.as.native_fn.arity = arity;
    return v;
}

UfValue uf_val_array(UfRuntime* rt, size_t initial_cap) {
    size_t cap = initial_cap < 4 ? 4 : initial_cap;
    UfArrayObject* arr = (UfArrayObject*)malloc(sizeof(UfArrayObject));
    if (!arr) {
        fprintf(stderr, "Fatal error: Out of memory allocating array\n");
        abort();
    }
    arr->obj.kind = UF_OBJ_ARRAY;
    arr->obj.marked = false;
    arr->obj.next = NULL;
    arr->count = 0;
    arr->capacity = cap;
    arr->elements = (UfValue*)calloc(cap, sizeof(UfValue));
    if (!arr->elements) {
        fprintf(stderr, "Fatal error: Out of memory allocating array elements\n");
        abort();
    }

    if (rt) {
        size_t size = sizeof(UfArrayObject) + cap * sizeof(UfValue);
        uf_runtime_register_obj(rt, (UfObj*)arr, size);
    }

    UfValue v;
    v.kind = UF_VAL_ARRAY;
    v.as.array = arr;
    return v;
}

void uf_array_push(UfRuntime* rt, UfArrayObject* arr, UfValue val) {
    if (arr->count + 1 > arr->capacity) {
        size_t new_cap = arr->capacity * 2;
        UfValue* new_elems = (UfValue*)realloc(arr->elements, new_cap * sizeof(UfValue));
        if (!new_elems) {
            fprintf(stderr, "Fatal error: Out of memory growing array\n");
            abort();
        }
        if (rt) {
            rt->bytes_allocated += (new_cap - arr->capacity) * sizeof(UfValue);
        }
        arr->elements = new_elems;
        arr->capacity = new_cap;
    }
    arr->elements[arr->count++] = val;
}

UfValue uf_array_pop(UfArrayObject* arr) {
    if (arr->count == 0) return uf_val_null();
    return arr->elements[--arr->count];
}

UfValue uf_array_get(UfArrayObject* arr, size_t index) {
    if (index >= arr->count) return uf_val_null();
    return arr->elements[index];
}

void uf_array_set(UfArrayObject* arr, size_t index, UfValue val) {
    if (index < arr->count) {
        arr->elements[index] = val;
    }
}

bool uf_val_is_truthy(UfValue val) {
    switch (val.kind) {
        case UF_VAL_NULL:
            return false;
        case UF_VAL_BOOL:
            return val.as.boolean;
        case UF_VAL_NUMBER:
            return val.as.number != 0.0 && !isnan(val.as.number);
        case UF_VAL_STRING:
            return val.as.string->length > 0;
        case UF_VAL_FUNCTION:
        case UF_VAL_NATIVE_FN:
            return true;
        case UF_VAL_ARRAY:
            return val.as.array->count > 0;
    }
    return false;
}

bool uf_val_equal(UfValue a, UfValue b) {
    if (a.kind != b.kind) return false;

    switch (a.kind) {
        case UF_VAL_NULL:
            return true;
        case UF_VAL_BOOL:
            return a.as.boolean == b.as.boolean;
        case UF_VAL_NUMBER:
            return a.as.number == b.as.number;
        case UF_VAL_STRING:
            if (a.as.string->length != b.as.string->length) return false;
            return memcmp(a.as.string->chars, b.as.string->chars, a.as.string->length) == 0;
        case UF_VAL_FUNCTION:
            return a.as.function == b.as.function;
        case UF_VAL_NATIVE_FN:
            return a.as.native_fn.fn == b.as.native_fn.fn;
        case UF_VAL_ARRAY: {
            if (a.as.array == b.as.array) return true;
            if (a.as.array->count != b.as.array->count) return false;
            for (size_t i = 0; i < a.as.array->count; ++i) {
                if (!uf_val_equal(a.as.array->elements[i], b.as.array->elements[i])) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

char* uf_val_to_string(UfValue val) {
    char buf[128];
    switch (val.kind) {
        case UF_VAL_NULL:
            return strdup("null");
        case UF_VAL_BOOL:
            return strdup(val.as.boolean ? "true" : "false");
        case UF_VAL_NUMBER: {
            double n = val.as.number;
            if (isnan(n)) {
                return strdup("nan");
            }
            if (isinf(n)) {
                return strdup(n > 0 ? "inf" : "-inf");
            }
            if (floor(n) == n && fabs(n) < 1e15) {
                snprintf(buf, sizeof(buf), "%.0f", n);
            } else {
                snprintf(buf, sizeof(buf), "%.14g", n);
            }
            return strdup(buf);
        }
        case UF_VAL_STRING:
            return strdup(val.as.string->chars);
        case UF_VAL_FUNCTION:
            snprintf(buf, sizeof(buf), "<function %s>", val.as.function->name ? val.as.function->name : "anonymous");
            return strdup(buf);
        case UF_VAL_NATIVE_FN:
            snprintf(buf, sizeof(buf), "<native function %s>", val.as.native_fn.name);
            return strdup(buf);
        case UF_VAL_ARRAY: {
            UfArrayObject* arr = val.as.array;
            size_t cap = 64;
            char* out = (char*)malloc(cap);
            out[0] = '[';
            out[1] = '\0';
            size_t len = 1;

            for (size_t i = 0; i < arr->count; ++i) {
                if (i > 0) {
                    if (len + 3 >= cap) {
                        cap *= 2;
                        out = (char*)realloc(out, cap);
                    }
                    strcat(out, ", ");
                    len += 2;
                }
                char* elem_s = uf_val_to_string(arr->elements[i]);
                size_t elen = strlen(elem_s);
                if (len + elen + 2 >= cap) {
                    cap = (len + elen + 2) * 2;
                    out = (char*)realloc(out, cap);
                }
                strcat(out, elem_s);
                len += elen;
                free(elem_s);
            }
            if (len + 2 >= cap) {
                cap += 2;
                out = (char*)realloc(out, cap);
            }
            strcat(out, "]");
            return out;
        }
    }
    return strdup("<unknown>");
}

const char* uf_val_type_name(UfValue val) {
    switch (val.kind) {
        case UF_VAL_NULL:      return "null";
        case UF_VAL_BOOL:      return "boolean";
        case UF_VAL_NUMBER:    return "number";
        case UF_VAL_STRING:    return "string";
        case UF_VAL_FUNCTION:  return "function";
        case UF_VAL_NATIVE_FN: return "function";
        case UF_VAL_ARRAY:     return "array";
    }
    return "<unknown>";
}

void uf_val_print(UfValue val, FILE* out) {
    char* str = uf_val_to_string(val);
    fprintf(out, "%s", str);
    free(str);
}
