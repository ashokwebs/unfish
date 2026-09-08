#ifndef UF_OBJECT_H
#define UF_OBJECT_H

#include "../common/uf_common.h"

typedef enum {
    UF_OBJ_STRING,
    UF_OBJ_FUNCTION,
    UF_OBJ_ENV,
    UF_OBJ_ARRAY,
    UF_OBJ_MAP,
    UF_OBJ_ERROR,
    UF_OBJ_MODULE,
    UF_OBJ_STRUCT_DEF,
    UF_OBJ_INSTANCE,
    UF_OBJ_BYTECODE_FN,
    UF_OBJ_CLOSURE,
    UF_OBJ_UPVALUE,
    UF_OBJ_FIBER,
    UF_OBJ_CHANNEL,
    UF_OBJ_BUFFER,
    UF_OBJ_BOUND_METHOD,
    UF_OBJ_ENUM_DEF,
    UF_OBJ_ENUM_VAL,
    UF_OBJ_TRAIT_DEF,
    UF_OBJ_REG_FN,
    UF_OBJ_REG_CLOSURE,
    UF_OBJ_PROMISE
} UfObjKind;

typedef struct UfObj {
    UfObjKind kind;
    bool marked;
    size_t size;
    struct UfObj* next;
} UfObj;

#endif /* UF_OBJECT_H */
