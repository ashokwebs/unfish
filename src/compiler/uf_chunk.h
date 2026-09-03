#ifndef UF_CHUNK_H
#define UF_CHUNK_H

#include "uf_opcode.h"
#include "../runtime/uf_value.h"
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t* code;
    size_t code_count;
    size_t code_capacity;

    UfValue* constants;
    size_t const_count;
    size_t const_capacity;

    int* lines;
} UfChunk;

void uf_chunk_init(UfChunk* chunk);
void uf_chunk_free(UfChunk* chunk);

void uf_chunk_write(UfChunk* chunk, uint8_t byte, int line);
size_t uf_chunk_add_constant(UfChunk* chunk, UfValue value);
void uf_chunk_write_constant(UfChunk* chunk, UfValue value, int line);

void uf_chunk_disassemble(const UfChunk* chunk, const char* name, FILE* out);
size_t uf_disassemble_instruction(const UfChunk* chunk, size_t offset, FILE* out);

#endif /* UF_CHUNK_H */
