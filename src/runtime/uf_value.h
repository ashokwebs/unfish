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
    UF_VAL_NATIVE_FN,
    UF_VAL_ARRAY,
    UF_VAL_MAP,
    UF_VAL_ERROR,
    UF_VAL_MODULE,
    UF_VAL_STRUCT_DEF,
    UF_VAL_INSTANCE,
    UF_VAL_BYTECODE_FN,
    UF_VAL_CLOSURE,
    UF_VAL_FIBER,
    UF_VAL_CHANNEL
} UfValueKind;

struct UfBytecodeFunction;
typedef struct UfBytecodeFunction UfBytecodeFunction;
struct UfClosureObject;
typedef struct UfClosureObject UfClosureObject;
struct UfUpvalueCell;
typedef struct UfUpvalueCell UfUpvalueCell;
struct UfFiber;
typedef struct UfFiber UfFiber;
struct UfChannel;
typedef struct UfChannel UfChannel;

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

typedef struct {
    UfObj obj;
    UfValue* elements;
    size_t count;
    size_t capacity;
} UfArrayObject;

typedef struct UfMapObject UfMapObject;
typedef struct UfModuleObject UfModuleObject;

typedef struct {
    UfObj obj;
    UfStringObject* message;
    UfStringObject* kind;
    int line;
    const char* file;
} UfErrorObject;

typedef struct {
    UfObj obj;
    const char* name;
    const char** field_names;
    const char** field_types;
    size_t field_count;
} UfStructDefObject;

typedef struct {
    UfObj obj;
    UfStructDefObject* def;
    UfValue* fields;
    size_t field_count;
} UfInstanceObject;

struct UfValue {
    UfValueKind kind;
    union {
        bool boolean;
        double number;
        UfStringObject* string;
        UfFunctionObject* function;
        UfNativeObject native_fn;
        UfArrayObject* array;
        UfMapObject* map;
        UfErrorObject* error;
        UfModuleObject* module;
        UfStructDefObject* struct_def;
        UfInstanceObject* instance;
        UfBytecodeFunction* bytecode_fn;
        UfClosureObject* closure;
        UfFiber* fiber;
        UfChannel* channel;
    } as;
};

typedef struct {
    UfValue key;
    UfValue value;
    bool occupied;
    bool tombstone;
} UfMapEntry;

struct UfMapObject {
    UfObj obj;
    UfMapEntry* entries;
    size_t count;
    size_t capacity;
    UfValue* order_keys;
    size_t order_count;
    size_t order_capacity;
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
UfValue uf_val_array(UfRuntime* rt, size_t initial_cap);
void uf_array_push(UfRuntime* rt, UfArrayObject* arr, UfValue val);
UfValue uf_array_pop(UfArrayObject* arr);
UfValue uf_array_get(UfArrayObject* arr, size_t index);
void uf_array_set(UfArrayObject* arr, size_t index, UfValue val);

UfValue uf_val_map(UfRuntime* rt, size_t initial_cap);
bool uf_map_set(UfRuntime* rt, UfMapObject* map, UfValue key, UfValue val);
UfValue uf_map_get(UfMapObject* map, UfValue key);
bool uf_map_has(UfMapObject* map, UfValue key);
bool uf_map_delete(UfMapObject* map, UfValue key);
UfValue uf_val_error(UfRuntime* rt, const char* message, const char* kind, SourceSpan span);
UfValue uf_val_module(UfRuntime* rt, UfModuleObject* mod);
UfValue uf_val_struct_def(UfRuntime* rt, const char* name, const char** field_names, const char** field_types, size_t field_count);
UfValue uf_val_instance(UfRuntime* rt, UfStructDefObject* def, UfValue* fields, size_t count);
UfValue uf_val_bytecode_fn(UfRuntime* rt, UfBytecodeFunction* fn);
UfValue uf_val_fiber(UfRuntime* rt, UfFiber* fiber);
UfValue uf_val_channel(UfRuntime* rt, UfChannel* channel);

/* Operations */
bool uf_val_is_truthy(UfValue val);
bool uf_val_equal(UfValue a, UfValue b);
char* uf_val_to_string(UfValue val);
const char* uf_val_type_name(UfValue val);
void uf_val_print(UfValue val, FILE* out);

#endif /* UF_VALUE_H */
