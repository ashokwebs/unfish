#ifndef UF_DISASM_H
#define UF_DISASM_H

#include "../compiler/uf_chunk.h"
#include <stdio.h>

struct UfVM;
struct UfVMFrame;

/* Disassemble a single chunk with a formatted header */
void uf_disasm_chunk(const UfChunk* chunk, const char* name, size_t arity, size_t upvalue_count, FILE* out);

/* Disassemble a bytecode function and recursively all enclosed child function prototypes */
void uf_disasm_function_tree(const UfBytecodeFunction* fn, FILE* out);

/* VM instruction tracing hook: dumps current stack and disassembles current instruction */
void uf_disasm_trace_instruction(const struct UfVM* vm, const struct UfVMFrame* frame, FILE* out);

#endif /* UF_DISASM_H */
