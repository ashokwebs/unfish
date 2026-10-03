#ifndef UF_REGVM_OPCODES_H
#define UF_REGVM_OPCODES_H

typedef enum {
    /* Constants & Literals */
    ROP_LOAD_K,          /* A = K[Bx]              Load constant Bx into register A */
    ROP_LOAD_NULL,       /* A = null               Set register A to null */
    ROP_LOAD_TRUE,       /* A = true               Set register A to true */
    ROP_LOAD_FALSE,      /* A = false              Set register A to false */

    /* Register Moves */
    ROP_MOVE,            /* A = B                  Copy register B into register A */

    /* Globals */
    ROP_GET_GLOBAL,      /* A = globals[K[Bx]]     Load global named K[Bx] into register A */
    ROP_SET_GLOBAL,      /* globals[K[Bx]] = A     Store register A into global named K[Bx] */
    ROP_DEF_GLOBAL,      /* define globals[K[Bx]]=A Define global named K[Bx] with value A */

    /* Closures & Upvalues */
    ROP_GET_UPVAL,       /* A = upvalues[B]        Load upvalue B into register A */
    ROP_SET_UPVAL,       /* upvalues[B] = A        Store register A into upvalue B */
    ROP_CLOSE_UPVAL,     /* Close upvalues >= &regs[A] */
    ROP_CLOSURE,         /* A = closure(K[Bx])     Create closure for function K[Bx] */

    /* Arithmetic (3-address: A = B op C) */
    ROP_ADD,             /* A = B + C              Add / string concatenate */
    ROP_SUB,             /* A = B - C              Subtract */
    ROP_MUL,             /* A = B * C              Multiply */
    ROP_DIV,             /* A = B / C              Divide */
    ROP_MOD,             /* A = B % C              Modulo */
    ROP_NEG,             /* A = -B                 Unary negate */
    ROP_NOT,             /* A = !B                 Logical not */

    /* Comparison (3-address: A = B op C) */
    ROP_EQ,              /* A = (B == C)           Equality */
    ROP_NEQ,             /* A = (B != C)           Inequality */
    ROP_LT,              /* A = (B < C)            Less than */
    ROP_LTE,             /* A = (B <= C)           Less than or equal */
    ROP_GT,              /* A = (B > C)            Greater than */
    ROP_GTE,             /* A = (B >= C)           Greater than or equal */

    /* Control Flow */
    ROP_JMP,             /* PC += sAx              Unconditional jump */
    ROP_JMP_FALSE,       /* if !A: PC += sBx       Jump if falsey */
    ROP_JMP_TRUE,        /* if A:  PC += sBx       Jump if truthy */
    ROP_JMP_ARG,         /* if argc > A: PC += sBx Jump if argument A was provided */
    ROP_LOOP,            /* PC -= Bx               Backward jump for loops */

    /* Function Calls */
    ROP_CALL,            /* A = A(A+1...A+B), dest in C */
    ROP_CALL_SPREAD,     /* Call with spread argument list */
    ROP_RETURN,          /* return A               Return register A */

    /* Collections */
    ROP_NEW_ARRAY,       /* A = array(B items starting at reg C) */
    ROP_NEW_MAP,         /* A = map(B pairs starting at reg C) */
    ROP_ARRAY_PUSH,      /* Push register B into array register A */
    ROP_ARRAY_EXTEND,    /* Extend array register A with array register B */
    ROP_ARRAY_SLICE,     /* A = slice(B, start=C) */
    ROP_MAP_SET,         /* A[B] = C               Set map key B to value C */
    ROP_MAP_EXTEND,      /* Extend map register A with map register B */
    ROP_INDEX_GET,       /* A = B[C]               Index or dot-property get */
    ROP_INDEX_SET,       /* A[B] = C               Index or dot-property set */
    ROP_ITER_GET,        /* A = iter_get(B, C)     Get element at index C of iterable B */

    /* Pattern Matching & Destructuring */
    ROP_ASSERT_ARRAY,    /* Assert register A is an array */
    ROP_ASSERT_MAP,      /* Assert register A is a map */
    ROP_ARRAY_GET_SAFE,  /* A = B[C] (safe get, null if out of bounds) */
    ROP_MAP_GET_SAFE,    /* A = B[K[C]] (safe map get, null if key missing) */
    ROP_MAP_REST,        /* A = map_rest(B, exclude_count, ...keys) */

    /* Statements */
    ROP_SAY,             /* Output register A with newline */

    /* Structs */
    ROP_STRUCT_DEF,      /* A = struct_def(K[Bx])  Register struct definition */
    ROP_INSTANCE,        /* A = new instance of struct B with C fields starting at A+1 */

    /* Exception Handling */
    ROP_PUSH_TRY,        /* push_try(catch_offset=Bx, error_reg=A) */
    ROP_POP_TRY,         /* pop_try() */
    ROP_RETHROW,         /* rethrow(A) */

    /* Async / Await */
    ROP_AWAIT,           /* A = await B            Await promise B into register A */

    /* Pattern matching */
    ROP_MATCH_SHAPE,     /* A = B has shape C; the next word's Bx is the count, or
                          * for REG_MATCH_NAMED the struct/variant name constant */
    ROP_MATCH_FIELD,     /* A = positional field C of instance/enum B, or null */

    ROP_COUNT            /* Total number of register opcodes */
} UfRegOpcode;

/* Shapes tested by ROP_MATCH_SHAPE (the first four as in the stack VM). */
#define REG_MATCH_ARRAY_EXACT    0 /* array with exactly `count` elements */
#define REG_MATCH_ARRAY_AT_LEAST 1 /* array with at least `count` elements */
#define REG_MATCH_MAP            2 /* map or struct instance */
#define REG_MATCH_FIELD_COUNT    3 /* instance or enum value with `count` fields */
#define REG_MATCH_NAMED          4 /* instance of struct / value of variant named K[Bx] */

#endif /* UF_REGVM_OPCODES_H */
