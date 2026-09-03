#ifndef UF_OBJECT_H
#define UF_OBJECT_H

#include "../common/uf_common.h"

typedef enum {
    UF_OBJ_STRING,
    UF_OBJ_FUNCTION,
    UF_OBJ_ENV,
    UF_OBJ_ARRAY,
    UF_OBJ_MAP
} UfObjKind;

typedef struct UfObj {
    UfObjKind kind;
    bool marked;
    struct UfObj* next;
} UfObj;

#endif /* UF_OBJECT_H */
