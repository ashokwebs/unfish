#ifndef UF_VALUE_H
#define UF_VALUE_H

#include "../common/uf_common.h"
#include "../common/uf_source.h"
#include "uf_object.h"

typedef struct UfValue UfValue;
typedef struct UfEnv UfEnv;
typedef struct UfRuntime UfRuntime;
struct UfStmt;

typedef enum {
    UF_VAL_NULL,
    UF_VAL_BOOL,
    UF_VAL_NUMBER,
    UF_VAL_STRING,
    UF_VAL_FUNCTION,
    UF_VAL_NATIVE_FN
} UfValueKind;

typedef UfValue (*UfNativeFn)(UfRuntime* rt, int argc, UfValue* args);

typedef struct {
    UfObj obj;
    size_t length;
    char chars[];
} UfStringObject;

typedef struct {
    UfObj obj;
    const char* name;
    const char** params;
    size_t param_count;
    struct UfStmt* body;
    UfEnv* closure_env;
} UfFunctionObject;

typedef struct {
    const char* name;
    UfNativeFn fn;
    int arity;
} UfNativeObject;

struct UfValue {
    UfValueKind kind;
    union {
        bool boolean;
        double number;
        UfStringObject* string;
        UfFunctionObject* function;
        UfNativeObject native_fn;
    } as;
};

/* Value constructors */
UfValue uf_val_null(void);
UfValue uf_val_bool(bool b);
UfValue uf_val_number(double n);
UfValue uf_val_string(UfRuntime* rt, const char* chars, size_t len);
UfValue uf_val_string_take(UfRuntime* rt, char* chars, size_t len);
UfValue uf_val_string_cstr(UfRuntime* rt, const char* cstr);
UfValue uf_val_function(UfRuntime* rt, const char* name, const char** params, size_t param_count, struct UfStmt* body, UfEnv* closure_env);
UfValue uf_val_native(const char* name, UfNativeFn fn, int arity);

/* Operations */
bool uf_val_is_truthy(UfValue val);
bool uf_val_equal(UfValue a, UfValue b);
char* uf_val_to_string(UfValue val);
const char* uf_val_type_name(UfValue val);
void uf_val_print(UfValue val, FILE* out);

#endif /* UF_VALUE_H */
