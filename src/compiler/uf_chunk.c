#include "uf_chunk.h"
#include "../runtime/uf_runtime.h"
#include <stdlib.h>
#include <string.h>

const char* uf_opcode_name(UfOpcode op) {
    switch (op) {
        case OP_CONSTANT: return "OP_CONSTANT";
        case OP_NULL: return "OP_NULL";
        case OP_TRUE: return "OP_TRUE";
        case OP_FALSE: return "OP_FALSE";
        case OP_POP: return "OP_POP";
        case OP_DUP: return "OP_DUP";
        case OP_LOAD_LOCAL: return "OP_LOAD_LOCAL";
        case OP_STORE_LOCAL: return "OP_STORE_LOCAL";
        case OP_LOAD_GLOBAL: return "OP_LOAD_GLOBAL";
        case OP_STORE_GLOBAL: return "OP_STORE_GLOBAL";
        case OP_DEFINE_GLOBAL: return "OP_DEFINE_GLOBAL";
        case OP_GET_UPVALUE: return "OP_GET_UPVALUE";
        case OP_SET_UPVALUE: return "OP_SET_UPVALUE";
        case OP_CLOSURE: return "OP_CLOSURE";
        case OP_CLOSE_UPVALUE: return "OP_CLOSE_UPVALUE";
        case OP_ADD: return "OP_ADD";
        case OP_SUB: return "OP_SUB";
        case OP_MUL: return "OP_MUL";
        case OP_DIV: return "OP_DIV";
        case OP_MOD: return "OP_MOD";
        case OP_NEG: return "OP_NEG";
        case OP_NOT: return "OP_NOT";
        case OP_EQ: return "OP_EQ";
        case OP_NEQ: return "OP_NEQ";
        case OP_LT: return "OP_LT";
        case OP_LTE: return "OP_LTE";
        case OP_GT: return "OP_GT";
        case OP_GTE: return "OP_GTE";
        case OP_JUMP: return "OP_JUMP";
        case OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE";
        case OP_JUMP_IF_ARG: return "OP_JUMP_IF_ARG";
        case OP_LOOP: return "OP_LOOP";
        case OP_CALL: return "OP_CALL";
        case OP_CALL_SPREAD: return "OP_CALL_SPREAD";
        case OP_RETURN: return "OP_RETURN";
        case OP_BUILD_ARRAY: return "OP_BUILD_ARRAY";
        case OP_ARRAY_PUSH: return "OP_ARRAY_PUSH";
        case OP_ARRAY_EXTEND: return "OP_ARRAY_EXTEND";
        case OP_ARRAY_SLICE: return "OP_ARRAY_SLICE";
        case OP_ASSERT_ARRAY: return "OP_ASSERT_ARRAY";
        case OP_ASSERT_MAP: return "OP_ASSERT_MAP";
        case OP_ARRAY_GET_SAFE: return "OP_ARRAY_GET_SAFE";
        case OP_MAP_GET_SAFE: return "OP_MAP_GET_SAFE";
        case OP_MAP_REST: return "OP_MAP_REST";
        case OP_BUILD_MAP: return "OP_BUILD_MAP";
        case OP_MAP_SET: return "OP_MAP_SET";
        case OP_MAP_EXTEND: return "OP_MAP_EXTEND";
        case OP_INDEX_GET: return "OP_INDEX_GET";
        case OP_INDEX_SET: return "OP_INDEX_SET";
        case OP_ITER_GET: return "OP_ITER_GET";
        case OP_SAY: return "OP_SAY";
        case OP_STRUCT_DEF: return "OP_STRUCT_DEF";
        case OP_INSTANCE: return "OP_INSTANCE";
        case OP_PUSH_TRY: return "OP_PUSH_TRY";
        case OP_POP_TRY: return "OP_POP_TRY";
        case OP_RETHROW: return "OP_RETHROW";
        case OP_AWAIT: return "OP_AWAIT";
        case OP_MATCH_SHAPE: return "OP_MATCH_SHAPE";
        case OP_MATCH_FIELD: return "OP_MATCH_FIELD";
        default: return "OP_UNKNOWN";
    }
}

void uf_chunk_init(UfChunk* chunk) {
    if (!chunk) return;
    chunk->code = NULL;
    chunk->code_count = 0;
    chunk->code_capacity = 0;
    chunk->constants = NULL;
    chunk->const_count = 0;
    chunk->const_capacity = 0;
    chunk->lines = NULL;
}

void uf_chunk_free(UfChunk* chunk) {
    if (!chunk) return;
    if (chunk->code) free(chunk->code);
    if (chunk->constants) free(chunk->constants);
    if (chunk->lines) free(chunk->lines);
    uf_chunk_init(chunk);
}

void uf_chunk_write(UfChunk* chunk, uint8_t byte, int line) {
    if (chunk->code_count >= chunk->code_capacity) {
        size_t old_cap = chunk->code_capacity;
        chunk->code_capacity = old_cap < 8 ? 8 : old_cap * 2;
        chunk->code = (uint8_t*)realloc(chunk->code, chunk->code_capacity);
        chunk->lines = (int*)realloc(chunk->lines, sizeof(int) * chunk->code_capacity);
    }
    chunk->code[chunk->code_count] = byte;
    chunk->lines[chunk->code_count] = line;
    chunk->code_count++;
}

size_t uf_chunk_add_constant(UfChunk* chunk, UfValue value) {
    if (chunk->const_count >= chunk->const_capacity) {
        size_t old_cap = chunk->const_capacity;
        chunk->const_capacity = old_cap < 8 ? 8 : old_cap * 2;
        chunk->constants = (UfValue*)realloc(chunk->constants, sizeof(UfValue) * chunk->const_capacity);
    }
    chunk->constants[chunk->const_count] = value;
    return chunk->const_count++;
}

void uf_chunk_write_constant(UfChunk* chunk, UfValue value, int line) {
    size_t const_idx = uf_chunk_add_constant(chunk, value);
    uf_chunk_write(chunk, (uint8_t)OP_CONSTANT, line);
    uf_chunk_write(chunk, (uint8_t)((const_idx >> 8) & 0xFF), line);
    uf_chunk_write(chunk, (uint8_t)(const_idx & 0xFF), line);
}

static size_t simple_instruction(const char* name, size_t offset, FILE* out) {
    fprintf(out, "%s\n", name);
    return offset + 1;
}

static size_t byte_instruction(const char* name, const UfChunk* chunk, size_t offset, FILE* out) {
    uint8_t slot = chunk->code[offset + 1];
    fprintf(out, "%-18s %4d\n", name, slot);
    return offset + 2;
}

static size_t u16_instruction(const char* name, const UfChunk* chunk, size_t offset, FILE* out) {
    uint16_t val = (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
    fprintf(out, "%-18s %4u\n", name, val);
    return offset + 3;
}

static size_t jump_instruction(const char* name, int sign, const UfChunk* chunk, size_t offset, FILE* out) {
    uint16_t jump = (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
    size_t target = offset + 3 + sign * jump;
    fprintf(out, "%-18s %4u -> %04zu\n", name, jump, target);
    return offset + 3;
}

static size_t constant_instruction(const char* name, const UfChunk* chunk, size_t offset, FILE* out) {
    uint16_t const_idx = (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
    fprintf(out, "%-18s %4u '", name, const_idx);
    if (const_idx < chunk->const_count) {
        char* str = uf_val_to_string(chunk->constants[const_idx]);
        fprintf(out, "%s", str ? str : "null");
        free(str);
    } else {
        fprintf(out, "<invalid const %u>", const_idx);
    }
    fprintf(out, "'\n");
    return offset + 3;
}

size_t uf_disassemble_instruction(const UfChunk* chunk, size_t offset, FILE* out) {
    if (!out) out = stdout;
    fprintf(out, "%04zu ", offset);

    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        fprintf(out, "   | ");
    } else {
        fprintf(out, "%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:
            return constant_instruction("OP_CONSTANT", chunk, offset, out);
        case OP_NULL:
            return simple_instruction("OP_NULL", offset, out);
        case OP_TRUE:
            return simple_instruction("OP_TRUE", offset, out);
        case OP_FALSE:
            return simple_instruction("OP_FALSE", offset, out);
        case OP_POP:
            return simple_instruction("OP_POP", offset, out);
        case OP_DUP:
            return simple_instruction("OP_DUP", offset, out);
        case OP_LOAD_LOCAL:
            return u16_instruction("OP_LOAD_LOCAL", chunk, offset, out);
        case OP_STORE_LOCAL:
            return u16_instruction("OP_STORE_LOCAL", chunk, offset, out);
        case OP_LOAD_GLOBAL:
            return constant_instruction("OP_LOAD_GLOBAL", chunk, offset, out);
        case OP_STORE_GLOBAL:
            return constant_instruction("OP_STORE_GLOBAL", chunk, offset, out);
        case OP_DEFINE_GLOBAL:
            return constant_instruction("OP_DEFINE_GLOBAL", chunk, offset, out);
        case OP_GET_UPVALUE:
            return byte_instruction("OP_GET_UPVALUE", chunk, offset, out);
        case OP_SET_UPVALUE:
            return byte_instruction("OP_SET_UPVALUE", chunk, offset, out);
        case OP_CLOSURE: {
            uint16_t const_idx = (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
            offset += 3;
            fprintf(out, "%-18s %4u ", "OP_CLOSURE", const_idx);
            if (const_idx < chunk->const_count) {
                UfValue val = chunk->constants[const_idx];
                char* s = uf_val_to_string(val);
                fprintf(out, "'%s'\n", s ? s : "function");
                free(s);
                if (val.kind == UF_VAL_BYTECODE_FN) {
                    UfBytecodeFunction* fn = val.as.bytecode_fn;
                    for (size_t j = 0; j < fn->upvalue_count && offset + 1 < chunk->code_count; ++j) {
                        uint8_t is_local = chunk->code[offset++];
                        uint8_t index = chunk->code[offset++];
                        fprintf(out, "%04zu      |                     %s %d\n",
                                offset - 2, is_local ? "local" : "upvalue", index);
                    }
                }
            } else {
                fprintf(out, "<invalid const %u>\n", const_idx);
            }
            return offset;
        }
        case OP_CLOSE_UPVALUE:
            return simple_instruction("OP_CLOSE_UPVALUE", offset, out);
        case OP_ADD:
            return simple_instruction("OP_ADD", offset, out);
        case OP_SUB:
            return simple_instruction("OP_SUB", offset, out);
        case OP_MUL:
            return simple_instruction("OP_MUL", offset, out);
        case OP_DIV:
            return simple_instruction("OP_DIV", offset, out);
        case OP_MOD:
            return simple_instruction("OP_MOD", offset, out);
        case OP_NEG:
            return simple_instruction("OP_NEG", offset, out);
        case OP_NOT:
            return simple_instruction("OP_NOT", offset, out);
        case OP_EQ:
            return simple_instruction("OP_EQ", offset, out);
        case OP_NEQ:
            return simple_instruction("OP_NEQ", offset, out);
        case OP_LT:
            return simple_instruction("OP_LT", offset, out);
        case OP_LTE:
            return simple_instruction("OP_LTE", offset, out);
        case OP_GT:
            return simple_instruction("OP_GT", offset, out);
        case OP_GTE:
            return simple_instruction("OP_GTE", offset, out);
        case OP_JUMP:
            return jump_instruction("OP_JUMP", 1, chunk, offset, out);
        case OP_JUMP_IF_FALSE:
            return jump_instruction("OP_JUMP_IF_FALSE", 1, chunk, offset, out);
        case OP_JUMP_IF_ARG: {
            uint8_t arg = chunk->code[offset + 1];
            uint16_t jump = (uint16_t)((chunk->code[offset + 2] << 8) | chunk->code[offset + 3]);
            fprintf(out, "%-16s %4d -> %zu\n", "OP_JUMP_IF_ARG", arg, offset + 4 + jump);
            return offset + 4;
        }
        case OP_LOOP:
            return jump_instruction("OP_LOOP", -1, chunk, offset, out);
        case OP_CALL:
            return byte_instruction("OP_CALL", chunk, offset, out);
        case OP_CALL_SPREAD:
            return simple_instruction("OP_CALL_SPREAD", offset, out);
        case OP_RETURN:
            return simple_instruction("OP_RETURN", offset, out);
        case OP_BUILD_ARRAY:
            return u16_instruction("OP_BUILD_ARRAY", chunk, offset, out);
        case OP_ARRAY_PUSH:
            return simple_instruction("OP_ARRAY_PUSH", offset, out);
        case OP_ARRAY_EXTEND:
            return simple_instruction("OP_ARRAY_EXTEND", offset, out);
        case OP_ARRAY_SLICE:
            return u16_instruction("OP_ARRAY_SLICE", chunk, offset, out);
        case OP_ASSERT_ARRAY:
            return simple_instruction("OP_ASSERT_ARRAY", offset, out);
        case OP_ASSERT_MAP:
            return simple_instruction("OP_ASSERT_MAP", offset, out);
        case OP_ARRAY_GET_SAFE:
            return u16_instruction("OP_ARRAY_GET_SAFE", chunk, offset, out);
        case OP_MAP_GET_SAFE:
            return simple_instruction("OP_MAP_GET_SAFE", offset, out);
        case OP_MAP_REST: {
            uint16_t count = (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
            fprintf(out, "%-16s %4d\n", "OP_MAP_REST", count);
            return offset + 3 + count * 2;
        }
        case OP_BUILD_MAP:
            return u16_instruction("OP_BUILD_MAP", chunk, offset, out);
        case OP_MAP_SET:
            return simple_instruction("OP_MAP_SET", offset, out);
        case OP_MAP_EXTEND:
            return simple_instruction("OP_MAP_EXTEND", offset, out);
        case OP_INDEX_GET:
            return simple_instruction("OP_INDEX_GET", offset, out);
        case OP_INDEX_SET:
            return simple_instruction("OP_INDEX_SET", offset, out);
        case OP_ITER_GET:
            return simple_instruction("OP_ITER_GET", offset, out);
        case OP_SAY:
            return simple_instruction("OP_SAY", offset, out);
        case OP_STRUCT_DEF:
            return u16_instruction("OP_STRUCT_DEF", chunk, offset, out);
        case OP_INSTANCE:
            return u16_instruction("OP_INSTANCE", chunk, offset, out);
        case OP_PUSH_TRY:
            return jump_instruction("OP_PUSH_TRY", 1, chunk, offset, out);
        case OP_POP_TRY:
            return simple_instruction("OP_POP_TRY", offset, out);
        case OP_RETHROW:
            return simple_instruction("OP_RETHROW", offset, out);
        case OP_AWAIT:
            return simple_instruction("OP_AWAIT", offset, out);
        case OP_MATCH_SHAPE: {
            static const char* shapes[] = { "array==", "array>=", "map", "fields==" };
            uint8_t shape = chunk->code[offset + 1];
            uint16_t count = (uint16_t)((chunk->code[offset + 2] << 8) | chunk->code[offset + 3]);
            fprintf(out, "%-16s %s %u\n", "OP_MATCH_SHAPE", shape < 4 ? shapes[shape] : "?", count);
            return offset + 4;
        }
        case OP_MATCH_FIELD:
            return u16_instruction("OP_MATCH_FIELD", chunk, offset, out);
        default:
            fprintf(out, "Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}

void uf_chunk_disassemble(const UfChunk* chunk, const char* name, FILE* out) {
    if (!out) out = stdout;
    fprintf(out, "== %s ==\n", name ? name : "<chunk>");
    for (size_t offset = 0; offset < chunk->code_count;) {
        offset = uf_disassemble_instruction(chunk, offset, out);
    }
    for (size_t i = 0; i < chunk->const_count; ++i) {
        if (chunk->constants[i].kind == UF_VAL_BYTECODE_FN && chunk->constants[i].as.bytecode_fn) {
            UfBytecodeFunction* sub = chunk->constants[i].as.bytecode_fn;
            char sub_name[128];
            snprintf(sub_name, sizeof(sub_name), "%s.<fn %s>", name ? name : "<chunk>", sub->name ? sub->name : "anon");
            fprintf(out, "\n");
            uf_chunk_disassemble(&sub->chunk, sub_name, out);
        }
    }
}

UfBytecodeFunction* uf_bytecode_fn_new(UfRuntime* rt, const char* name, size_t arity, size_t min_arity, bool has_rest) {
    UfBytecodeFunction* bfn = (UfBytecodeFunction*)malloc(sizeof(UfBytecodeFunction));
    if (!bfn) return NULL;
    bfn->obj.kind = UF_OBJ_BYTECODE_FN;
    bfn->obj.marked = false;
    bfn->obj.next = NULL;
    bfn->name = name;
    bfn->arity = arity;
    bfn->min_arity = min_arity;
    bfn->has_rest = has_rest;
    bfn->is_async = false;
    bfn->upvalue_count = 0;
    uf_chunk_init(&bfn->chunk);
    if (rt) {
        uf_runtime_register_obj(rt, (UfObj*)bfn, sizeof(UfBytecodeFunction));
    }
    return bfn;
}
