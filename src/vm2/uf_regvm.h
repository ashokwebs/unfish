#ifndef UF_REGVM_H
#define UF_REGVM_H

#include "uf_regvm_opcodes.h"
#include "../runtime/uf_runtime.h"
#include "../runtime/uf_value.h"
#include "../interpreter/uf_interpreter.h"
#include "../vm/uf_vm.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Instruction encoding and decoding */
#define REG_ENCODE_ABC(op, a, b, c) \
    ((uint32_t)(uint8_t)(op) | ((uint32_t)(uint8_t)(a) << 8) | ((uint32_t)(uint8_t)(b) << 16) | ((uint32_t)(uint8_t)(c) << 24))

#define REG_ENCODE_ABx(op, a, bx) \
    ((uint32_t)(uint8_t)(op) | ((uint32_t)(uint8_t)(a) << 8) | ((uint32_t)(uint16_t)(bx) << 16))

#define REG_ENCODE_sAx(op, sax) \
    ((uint32_t)(uint8_t)(op) | (((uint32_t)(sax) & 0xFFFFFF) << 8))

#define REG_GET_OP(i)   ((uint8_t)((i) & 0xFF))
#define REG_GET_A(i)    ((uint8_t)(((i) >> 8) & 0xFF))
#define REG_GET_B(i)    ((uint8_t)(((i) >> 16) & 0xFF))
#define REG_GET_C(i)    ((uint8_t)(((i) >> 24) & 0xFF))
#define REG_GET_Bx(i)   ((uint16_t)(((i) >> 16) & 0xFFFF))
#define REG_GET_sBx(i)  ((int16_t)((uint16_t)(((i) >> 16) & 0xFFFF)))
#define REG_GET_sAx(i)  ((int32_t)((int32_t)(i) >> 8))

/* Upvalue descriptor inside a compiled function */
typedef struct {
    uint8_t is_local;
    uint8_t index;
} UfRegUpvalueDesc;

/* Register bytecode chunk */
typedef struct {
    uint32_t* code;
    size_t code_count;
    size_t code_capacity;

    UfValue* constants;
    size_t const_count;
    size_t const_capacity;

    int* lines;
} UfRegChunk;

/* Register bytecode function */
struct UfRegFunction {
    UfObj obj;
    const char* name;
    size_t arity;
    size_t min_arity;
    bool has_rest;
    bool is_async;
    uint8_t max_regs;
    UfRegChunk chunk;
    UfRegUpvalueDesc* upvalues;
    size_t upvalue_count;
};

/* Register closure */
struct UfRegClosure {
    UfObj obj;
    UfRegFunction* function;
    UfUpvalueCell** upvalues;
    size_t upvalue_count;
};

/* Register VM Call Frame */
typedef struct {
    UfRegClosure* closure;
    uint32_t* ip;
    UfValue* regs;
    size_t argc;
    uint8_t dest_reg;
} UfRegFrame;

/* Same call depth as the AST interpreter's UF_MAX_CALL_FRAMES, so a program's
 * recursion limit does not depend on which engine runs it. The register file
 * is shared by all frames (each window starts at its caller's argument base)
 * and is bounds-checked on every call. */
/* Function frames allowed, matching UF_MAX_CALL_FRAMES; the frames array
 * holds one more for the top-level script, which the interpreter does not
 * count as a call. */
#define UF_REGVM_FRAMES_MAX_CALLS 512
#define UF_REGVM_FRAMES_MAX (UF_REGVM_FRAMES_MAX_CALLS + 1)
#define UF_REGVM_HANDLERS_MAX  64
#define UF_REGVM_STACK_MAX     65536

typedef struct {
    int frame_index;
    uint32_t* catch_ip;
    uint8_t error_reg;
} UfRegVMHandler;

/* The Register Virtual Machine */
typedef struct UfRegVM {
    UfRegFrame frames[UF_REGVM_FRAMES_MAX];
    int frame_count;

    UfValue stack[UF_REGVM_STACK_MAX];
    UfValue* stack_top;

    UfRegVMHandler handlers[UF_REGVM_HANDLERS_MAX];
    int handler_count;

    UfUpvalueCell* open_upvalues;
    UfRuntime* rt;
    bool had_error;

    bool trace_execution;
    uint64_t total_instructions;
    size_t peak_stack_depth;
    size_t peak_frame_depth;
} UfRegVM;

/* Chunk management */
void uf_reg_chunk_init(UfRegChunk* chunk);
void uf_reg_chunk_free(UfRegChunk* chunk);
void uf_reg_chunk_write(UfRegChunk* chunk, uint32_t instr, int line);
size_t uf_reg_chunk_add_constant(UfRegChunk* chunk, UfValue value);

/* Object constructors */
UfRegFunction* uf_reg_fn_new(UfRuntime* rt, const char* name, size_t arity, size_t min_arity, bool has_rest);
UfRegClosure* uf_reg_closure_new(UfRuntime* rt, UfRegFunction* fn);

/* VM management */
void uf_regvm_init(UfRegVM* vm, UfRuntime* rt);
void uf_regvm_free(UfRegVM* vm);

UfInterpretResult uf_regvm_run(UfRegVM* vm, UfRegFunction* function);
UfValue uf_regvm_run_closure(UfRegVM* vm, UfRegClosure* closure, size_t argc, UfValue* args);

#endif /* UF_REGVM_H */
