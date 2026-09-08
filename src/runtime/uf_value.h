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
    UF_VAL_CHANNEL,
    UF_VAL_BUFFER,
    UF_VAL_BOUND_METHOD,
    UF_VAL_ENUM_DEF,
    UF_VAL_ENUM_VAL,
    UF_VAL_TRAIT_DEF,
    UF_VAL_REG_FN,
    UF_VAL_REG_CLOSURE,
    UF_VAL_PROMISE
} UfValueKind;

struct UfPromiseObject;
typedef struct UfPromiseObject UfPromiseObject;

struct UfBytecodeFunction;
typedef struct UfBytecodeFunction UfBytecodeFunction;
struct UfClosureObject;
typedef struct UfClosureObject UfClosureObject;
struct UfRegFunction;
typedef struct UfRegFunction UfRegFunction;
struct UfRegClosure;
typedef struct UfRegClosure UfRegClosure;
struct UfUpvalueCell;
typedef struct UfUpvalueCell UfUpvalueCell;
struct UfFiber;
typedef struct UfFiber UfFiber;
struct UfChannel;
typedef struct UfChannel UfChannel;
struct UfBoundMethodObject;
typedef struct UfBoundMethodObject UfBoundMethodObject;
struct UfEnumDefObject;
typedef struct UfEnumDefObject UfEnumDefObject;
struct UfEnumValObject;
typedef struct UfEnumValObject UfEnumValObject;
struct UfTraitDefObject;
typedef struct UfTraitDefObject UfTraitDefObject;

typedef struct {
    UfObj obj;
    uint8_t* data;
    size_t size;
} UfBufferObject;

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
    struct UfExpr** param_defaults;
    size_t min_param_count;
    bool has_rest;
    bool is_async;
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
    const char** method_names;
    UfValue* method_values;
    size_t method_count;
    const char** impl_traits;
    size_t impl_trait_count;
} UfStructDefObject;

typedef struct {
    UfObj obj;
    UfStructDefObject* def;
    UfValue* fields;
    size_t field_count;
} UfInstanceObject;

struct UfEnumDefObject {
    UfObj obj;
    const char* name;
    size_t variant_count;
    const char** variant_names;
    size_t* variant_field_counts;
    const char*** variant_field_names;
    const char*** variant_field_types;
};

struct UfEnumValObject {
    UfObj obj;
    UfEnumDefObject* def;
    int tag;
    const char* variant_name;
    size_t field_count;
    UfValue* fields;
};

struct UfTraitDefObject {
    UfObj obj;
    const char* name;
    size_t method_count;
    const char** method_names;
    size_t* method_param_counts;
    const char*** method_param_names;
    const char*** method_param_types;
    const char** method_return_types;
};

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
        UfBufferObject* buffer;
        UfBoundMethodObject* bound_method;
        UfEnumDefObject* enum_def;
        UfEnumValObject* enum_val;
        UfTraitDefObject* trait_def;
        UfRegFunction* reg_fn;
        UfRegClosure* reg_closure;
        UfPromiseObject* promise;
    } as;
};

typedef enum {
    UF_PROMISE_PENDING,
    UF_PROMISE_RESOLVED,
    UF_PROMISE_REJECTED
} UfPromiseState;

struct UfPromiseObject {
    UfObj obj;
    UfPromiseState state;
    UfValue result;
    UfValue error;
    struct UfFiber** waiters;
    size_t waiter_count;
    size_t waiter_capacity;
};

struct UfBoundMethodObject {
    UfObj obj;
    UfValue receiver;
    UfValue method;
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
    size_t tombstone_count;
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
UfValue uf_val_function(UfRuntime* rt, const char* name, const char** params, struct UfExpr** param_defaults, size_t param_count, size_t min_param_count, bool has_rest, struct UfStmt* body, UfEnv* closure_env);
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
UfValue uf_val_struct_def(UfRuntime* rt, const char* name, const char** field_names, const char** field_types, size_t field_count, const char** method_names, UfValue* method_values, size_t method_count);
UfValue uf_val_struct_def_with_traits(UfRuntime* rt, const char* name, const char** field_names, const char** field_types, size_t field_count, const char** method_names, UfValue* method_values, size_t method_count, const char** impl_traits, size_t impl_trait_count);
UfValue uf_val_trait_def(UfRuntime* rt, const char* name, size_t method_count, const char** method_names, size_t* method_param_counts, const char*** method_param_names, const char*** method_param_types, const char** method_return_types);
bool uf_struct_implements_trait(UfStructDefObject* sdef, const char* trait_name);
bool uf_instance_implements_trait(UfInstanceObject* inst, const char* trait_name);
UfValue uf_val_instance(UfRuntime* rt, UfStructDefObject* def, UfValue* fields, size_t count);
UfValue uf_val_enum_def(UfRuntime* rt, const char* name, size_t variant_count, const char** variant_names, size_t* variant_field_counts, const char*** variant_field_names, const char*** variant_field_types);
UfValue uf_val_enum_val(UfRuntime* rt, UfEnumDefObject* def, int tag, const char* variant_name, UfValue* fields, size_t count);
UfValue uf_val_bound_method(UfRuntime* rt, UfValue receiver, UfValue method);
UfValue uf_val_bytecode_fn(UfRuntime* rt, UfBytecodeFunction* fn);
UfValue uf_val_reg_fn(UfRuntime* rt, UfRegFunction* fn);
UfValue uf_val_reg_closure(UfRuntime* rt, UfRegClosure* closure);
UfValue uf_val_fiber(UfRuntime* rt, UfFiber* fiber);
UfValue uf_val_channel(UfRuntime* rt, UfChannel* channel);
UfValue uf_val_buffer(UfRuntime* rt, size_t size);
UfValue uf_val_buffer_from_bytes(UfRuntime* rt, const uint8_t* bytes, size_t size);
UfValue uf_val_promise(UfRuntime* rt, UfPromiseObject* promise);

/* Operations */
bool uf_val_is_truthy(UfValue val);
bool uf_val_equal(UfValue a, UfValue b);
char* uf_val_to_string(UfValue val);
char* uf_val_repr(UfValue val);
const char* uf_val_type_name(UfValue val);
void uf_val_print(UfValue val, FILE* out);

#endif /* UF_VALUE_H */
