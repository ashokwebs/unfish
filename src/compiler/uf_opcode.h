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
    OP_LOOP,            /* [u16 backward_offset] */

    /* Functions */
    OP_CALL,            /* [u8 argc] */
    OP_RETURN,

    /* Collections */
    OP_BUILD_ARRAY,     /* [u16 count] */
    OP_BUILD_MAP,       /* [u16 pair_count] */
    OP_INDEX_GET,
    OP_INDEX_SET,

    /* Statements */
    OP_SAY,

    /* Structs */
    OP_STRUCT_DEF,      /* [u16 const_sdef_idx] */
    OP_INSTANCE         /* [u16 const_sdef_idx, u8 argc] */
} UfOpcode;

const char* uf_opcode_name(UfOpcode op);

#endif /* UF_OPCODE_H */
