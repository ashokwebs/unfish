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
    UF_OBJ_CHANNEL
} UfObjKind;

typedef struct UfObj {
    UfObjKind kind;
    bool marked;
    struct UfObj* next;
} UfObj;

#endif /* UF_OBJECT_H */
