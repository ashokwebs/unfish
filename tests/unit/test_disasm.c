#include "../../src/compiler/uf_chunk.h"
#include "../../src/compiler/uf_opcode.h"
#include "../../src/vm/uf_vm.h"
#include "../../src/vm/uf_disasm.h"
#include "../../src/runtime/uf_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_disasm_simple_chunk(void) {
    UfRuntime rt;
    uf_runtime_init(&rt, NULL);

    UfChunk chunk;
    uf_chunk_init(&chunk);

    uf_chunk_write_constant(&chunk, uf_val_number(42.0), 1);
    uf_chunk_write(&chunk, (uint8_t)OP_LOAD_LOCAL, 1);
    uf_chunk_write(&chunk, 0, 1);
    uf_chunk_write(&chunk, 0, 1);
    uf_chunk_write(&chunk, (uint8_t)OP_ADD, 2);
    uf_chunk_write(&chunk, (uint8_t)OP_SAY, 2);
    uf_chunk_write(&chunk, (uint8_t)OP_RETURN, 3);

    FILE* stream = tmpfile();
    assert(stream != NULL);

    uf_disasm_chunk(&chunk, "test_simple", 0, 0, stream);
    rewind(stream);

    char buf[4096];
    size_t len = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[len] = '\0';
    fclose(stream);

    assert(strstr(buf, "== test_simple") != NULL);
    assert(strstr(buf, "OP_CONSTANT") != NULL);
    assert(strstr(buf, "'42'") != NULL);
    assert(strstr(buf, "OP_LOAD_LOCAL") != NULL);
    assert(strstr(buf, "OP_ADD") != NULL);
    assert(strstr(buf, "OP_SAY") != NULL);
    assert(strstr(buf, "OP_RETURN") != NULL);

    uf_chunk_free(&chunk);
    uf_runtime_free(&rt);
    printf("test_disasm_simple_chunk passed!\n");
}

static void test_disasm_jumps(void) {
    UfRuntime rt;
    uf_runtime_init(&rt, NULL);

    UfChunk chunk;
    uf_chunk_init(&chunk);

    uf_chunk_write(&chunk, (uint8_t)OP_JUMP_IF_FALSE, 10);
    uf_chunk_write(&chunk, 0, 10);
    uf_chunk_write(&chunk, 5, 10);
    uf_chunk_write(&chunk, (uint8_t)OP_LOOP, 11);
    uf_chunk_write(&chunk, 0, 11);
    uf_chunk_write(&chunk, 6, 11);

    FILE* stream = tmpfile();
    assert(stream != NULL);

    uf_disasm_chunk(&chunk, "test_jumps", 0, 0, stream);
    rewind(stream);

    char buf[4096];
    size_t len = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[len] = '\0';
    fclose(stream);

    assert(strstr(buf, "OP_JUMP_IF_FALSE") != NULL);
    assert(strstr(buf, "OP_LOOP") != NULL);
    assert(strstr(buf, "->") != NULL);

    uf_chunk_free(&chunk);
    uf_runtime_free(&rt);
    printf("test_disasm_jumps passed!\n");
}

static void test_disasm_closures(void) {
    UfRuntime rt;
    uf_runtime_init(&rt, NULL);

    UfBytecodeFunction* inner = uf_bytecode_fn_new(&rt, "inner_fn", 1);
    inner->upvalue_count = 2;
    uf_chunk_write(&inner->chunk, (uint8_t)OP_RETURN, 1);

    UfBytecodeFunction* outer = uf_bytecode_fn_new(&rt, "outer_fn", 0);
    size_t const_idx = uf_chunk_add_constant(&outer->chunk, uf_val_bytecode_fn(&rt, inner));

    uf_chunk_write(&outer->chunk, (uint8_t)OP_CLOSURE, 5);
    uf_chunk_write(&outer->chunk, (uint8_t)(const_idx >> 8), 5);
    uf_chunk_write(&outer->chunk, (uint8_t)(const_idx & 0xFF), 5);
    /* upvalue 0: local 3 */
    uf_chunk_write(&outer->chunk, 1, 5);
    uf_chunk_write(&outer->chunk, 3, 5);
    /* upvalue 1: upvalue 0 */
    uf_chunk_write(&outer->chunk, 0, 5);
    uf_chunk_write(&outer->chunk, 0, 5);
    uf_chunk_write(&outer->chunk, (uint8_t)OP_RETURN, 6);

    FILE* stream = tmpfile();
    assert(stream != NULL);

    uf_disasm_function_tree(outer, stream);
    rewind(stream);

    char buf[4096];
    size_t len = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[len] = '\0';
    fclose(stream);

    assert(strstr(buf, "== outer_fn") != NULL);
    assert(strstr(buf, "OP_CLOSURE") != NULL);
    assert(strstr(buf, "local 3") != NULL);
    assert(strstr(buf, "upvalue 0") != NULL);
    assert(strstr(buf, "== inner_fn") != NULL);

    uf_runtime_free(&rt);
    printf("test_disasm_closures passed!\n");
}

static void test_disasm_trace(void) {
    UfRuntime rt;
    uf_runtime_init(&rt, NULL);

    UfBytecodeFunction* fn = uf_bytecode_fn_new(&rt, "trace_target", 0);
    uf_chunk_write_constant(&fn->chunk, uf_val_number(123.0), 1);
    uf_chunk_write(&fn->chunk, (uint8_t)OP_RETURN, 1);

    UfClosureObject* cl = uf_closure_new(&rt, fn);

    UfVM vm;
    uf_vm_init(&vm, &rt);
    uf_vm_push(&vm, uf_val_closure(&rt, cl));
    uf_vm_push(&vm, uf_val_number(999.0));

    UfVMFrame frame;
    frame.closure = cl;
    frame.ip = fn->chunk.code;
    frame.slots = vm.stack;

    FILE* stream = tmpfile();
    assert(stream != NULL);

    uf_disasm_trace_instruction(&vm, &frame, stream);
    rewind(stream);

    char buf[4096];
    size_t len = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[len] = '\0';
    fclose(stream);

    assert(strstr(buf, "[ 999 ]") != NULL);
    assert(strstr(buf, "OP_CONSTANT") != NULL);

    uf_vm_free(&vm);
    uf_runtime_free(&rt);
    printf("test_disasm_trace passed!\n");
}

int main(void) {
    printf("Running disassembler unit tests...\n");
    test_disasm_simple_chunk();
    test_disasm_jumps();
    test_disasm_closures();
    test_disasm_trace();
    printf("All disassembler unit tests passed successfully!\n");
    return 0;
}
