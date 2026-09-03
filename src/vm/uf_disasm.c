#include "uf_disasm.h"
#include "uf_vm.h"
#include "../runtime/uf_value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void uf_disasm_chunk(const UfChunk* chunk, const char* name, size_t arity, size_t upvalue_count, FILE* out) {
    if (!chunk) return;
    if (!out) out = stdout;

    fprintf(out, "== %s (arity: %zu, upvalues: %zu, %zu bytes) ==\n",
            name ? name : "<script>", arity, upvalue_count, chunk->code_count);

    for (size_t offset = 0; offset < chunk->code_count;) {
        offset = uf_disassemble_instruction(chunk, offset, out);
    }
}

void uf_disasm_function_tree(const UfBytecodeFunction* fn, FILE* out) {
    if (!fn) return;
    if (!out) out = stdout;

    uf_disasm_chunk(&fn->chunk, fn->name ? fn->name : "<script>", fn->arity, fn->upvalue_count, out);

    /* Recursively disassemble child functions in constant pool */
    for (size_t i = 0; i < fn->chunk.const_count; ++i) {
        UfValue val = fn->chunk.constants[i];
        if (val.kind == UF_VAL_BYTECODE_FN && val.as.bytecode_fn) {
            fprintf(out, "\n");
            uf_disasm_function_tree(val.as.bytecode_fn, out);
        }
    }
}

void uf_disasm_trace_instruction(const struct UfVM* vm, const struct UfVMFrame* frame, FILE* out) {
    if (!vm || !frame) return;
    if (!out) out = stdout;

    fprintf(out, "          ");
    if (vm->stack == vm->stack_top) {
        fprintf(out, "[ <empty stack> ]");
    } else {
        for (UfValue* slot = (UfValue*)vm->stack; slot < vm->stack_top; ++slot) {
            char* s = uf_val_to_string(*slot);
            fprintf(out, "[ %s ]", s ? s : "null");
            free(s);
        }
    }
    fprintf(out, "\n");

    size_t offset = (size_t)(frame->ip - frame->closure->function->chunk.code);
    uf_disassemble_instruction(&frame->closure->function->chunk, offset, out);
}
