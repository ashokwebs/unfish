#define _POSIX_C_SOURCE 200809L
#include "uf_chunk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_chunk_lifecycle(void) {
    UfChunk chunk;
    uf_chunk_init(&chunk);
    assert(chunk.code_count == 0);
    assert(chunk.const_count == 0);

    uf_chunk_write_constant(&chunk, uf_val_number(42.0), 1);
    uf_chunk_write(&chunk, OP_SAY, 1);
    uf_chunk_write(&chunk, OP_RETURN, 2);

    assert(chunk.code_count == 5); /* OP_CONSTANT(1) + idx(2) + OP_SAY(1) + OP_RETURN(1) */
    assert(chunk.const_count == 1);
    assert(chunk.constants[0].kind == UF_VAL_NUMBER);
    assert(chunk.constants[0].as.number == 42.0);

    uf_chunk_free(&chunk);
    assert(chunk.code == NULL);
    printf("test_chunk_lifecycle passed!\n");
}

static void test_chunk_disassembler(void) {
    UfChunk chunk;
    uf_chunk_init(&chunk);

    uf_chunk_write_constant(&chunk, uf_val_number(100.5), 10);
    uf_chunk_write(&chunk, OP_STORE_LOCAL, 10);
    uf_chunk_write(&chunk, 0, 10);
    uf_chunk_write(&chunk, 1, 10);

    uf_chunk_write(&chunk, OP_LOAD_LOCAL, 11);
    uf_chunk_write(&chunk, 0, 11);
    uf_chunk_write(&chunk, 1, 11);

    uf_chunk_write(&chunk, OP_ADD, 11);
    uf_chunk_write(&chunk, OP_RETURN, 12);

    char* dis_buf = NULL;
    size_t dis_sz = 0;
    FILE* mem = open_memstream(&dis_buf, &dis_sz);
    assert(mem != NULL);

    uf_chunk_disassemble(&chunk, "test_routine", mem);
    fclose(mem);

    assert(dis_buf != NULL);
    assert(strstr(dis_buf, "== test_routine ==") != NULL);
    assert(strstr(dis_buf, "OP_CONSTANT") != NULL);
    assert(strstr(dis_buf, "100.5") != NULL);
    assert(strstr(dis_buf, "OP_STORE_LOCAL") != NULL);
    assert(strstr(dis_buf, "OP_LOAD_LOCAL") != NULL);
    assert(strstr(dis_buf, "OP_ADD") != NULL);
    assert(strstr(dis_buf, "OP_RETURN") != NULL);

    free(dis_buf);
    uf_chunk_free(&chunk);
    printf("test_chunk_disassembler passed!\n");
}

int main(void) {
    printf("Running bytecode chunk unit tests...\n");
    test_chunk_lifecycle();
    test_chunk_disassembler();
    printf("All bytecode chunk unit tests passed successfully!\n");
    return 0;
}
