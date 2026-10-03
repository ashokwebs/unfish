#ifndef UF_OPCODE_H
#define UF_OPCODE_H

#include <stdint.h>

typedef enum {
    /* Literals & Constants */
    OP_CONSTANT,        /* [u16 const_idx] */
    OP_NULL,
    OP_TRUE,
    OP_FALSE,

    /* Stack Management */
    OP_POP,
    OP_DUP,

    /* Local Variables (Stack Slots) */
    OP_LOAD_LOCAL,      /* [u16 slot] */
    OP_STORE_LOCAL,     /* [u16 slot] */

    /* Global Variables (Constant Name) */
    OP_LOAD_GLOBAL,     /* [u16 const_name_idx] */
    OP_STORE_GLOBAL,    /* [u16 const_name_idx] */
    OP_DEFINE_GLOBAL,   /* [u16 const_name_idx] */

    /* Closures & Upvalues */
    OP_GET_UPVALUE,     /* [u8 upvalue_idx] */
    OP_SET_UPVALUE,     /* [u8 upvalue_idx] */
    OP_CLOSURE,         /* [u16 fn_const_idx] */
    OP_CLOSE_UPVALUE,

    /* Arithmetic Operators */
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,

    /* Unary Operators */
    OP_NEG,
    OP_NOT,

    /* Comparison Operators */
    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_LTE,
    OP_GT,
    OP_GTE,

    /* Control Flow */
    OP_JUMP,            /* [u16 forward_offset] */
    OP_JUMP_IF_FALSE,   /* [u16 forward_offset] */
    OP_JUMP_IF_ARG,     /* [u8 arg_idx, u16 forward_offset] */
    OP_LOOP,            /* [u16 backward_offset] */

    /* Functions */
    OP_CALL,            /* [u8 argc] */
    OP_CALL_SPREAD,
    OP_RETURN,

    /* Collections */
    OP_BUILD_ARRAY,     /* [u16 count] */
    OP_ARRAY_PUSH,
    OP_ARRAY_EXTEND,
    OP_ARRAY_SLICE,     /* [u16 start_idx] */
    OP_ASSERT_ARRAY,
    OP_ASSERT_MAP,
    OP_ARRAY_GET_SAFE,  /* [u16 index] */
    OP_MAP_GET_SAFE,
    OP_MAP_REST,        /* [u16 exclude_count, ...u16 const_indices] */
    OP_BUILD_MAP,       /* [u16 pair_count] */
    OP_MAP_SET,
    OP_MAP_EXTEND,
    OP_INDEX_GET,
    OP_INDEX_SET,
    OP_ITER_GET,

    /* Statements */
    OP_SAY,

    /* Structs */
    OP_STRUCT_DEF,      /* [u16 const_sdef_idx] */
    OP_INSTANCE,        /* [u16 const_sdef_idx] */

    /* Exception Handling */
    OP_PUSH_TRY,        /* [u16 catch_offset] */
    OP_POP_TRY,
    OP_RETHROW,

    /* Async / Concurrency */
    OP_AWAIT,

    /* Pattern matching (appended to keep earlier opcode numbers stable) */
    OP_MATCH_SHAPE,     /* [u8 shape, u16 count] pops value, pushes bool */
    OP_MATCH_FIELD      /* [u16 index] pops instance/enum, pushes field or null */
} UfOpcode;

/* Shapes tested by OP_MATCH_SHAPE. */
#define UF_MATCH_ARRAY_EXACT    0 /* array with exactly `count` elements */
#define UF_MATCH_ARRAY_AT_LEAST 1 /* array with at least `count` elements */
#define UF_MATCH_MAP            2 /* map or struct instance */
#define UF_MATCH_FIELD_COUNT    3 /* instance or enum value with `count` fields */

const char* uf_opcode_name(UfOpcode op);

#endif /* UF_OPCODE_H */
