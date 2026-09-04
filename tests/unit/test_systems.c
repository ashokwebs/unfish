#define _POSIX_C_SOURCE 200809L
#include "../../src/runtime/uf_runtime.h"
#include "../../src/runtime/uf_env.h"
#include "../../src/runtime/uf_stdlib.h"
#include "../../src/interpreter/uf_interpreter.h"
#include "../../src/parser/uf_parser.h"
#include "../../src/lexer/uf_lexer.h"
#include "../../src/semantic/uf_semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_buffer_creation_and_bounds(void) {
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfValue buf_val = uf_val_buffer(&rt, 16);
    assert(buf_val.kind == UF_VAL_BUFFER);
    assert(buf_val.as.buffer != NULL);
    assert(buf_val.as.buffer->size == 16);

    for (size_t i = 0; i < 16; ++i) {
        assert(buf_val.as.buffer->data[i] == 0);
    }

    /* Test truthiness */
    assert(uf_val_is_truthy(buf_val));

    UfValue empty_buf = uf_val_buffer(&rt, 0);
    assert(!uf_val_is_truthy(empty_buf));

    uf_runtime_free(&rt);
    printf("test_buffer_creation_and_bounds passed!\n");
}

static void test_buffer_string_roundtrip(void) {
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    const char* orig = "Hello Systems!";
    UfValue buf_val = uf_val_buffer_from_bytes(&rt, (const uint8_t*)orig, strlen(orig));
    assert(buf_val.as.buffer->size == strlen(orig));
    assert(memcmp(buf_val.as.buffer->data, orig, strlen(orig)) == 0);

    uf_runtime_free(&rt);
    printf("test_buffer_string_roundtrip passed!\n");
}

static void test_systems_script_execution(void) {
    const char* source =
        "let buf = buffer(8)\n"
        "buffer_set(buf, 0, 72)\n"
        "buffer_set(buf, 1, 105)\n"
        "buffer_set(buf, 2, 33)\n"
        "let b0 = buffer_get(buf, 0)\n"
        "let b1 = buffer_get(buf, 1)\n"
        "let b2 = buffer_get(buf, 2)\n"
        "let sub = buffer_slice(buf, 0, 3)\n"
        "let str = buffer_to_string(sub)\n"
        "let sz = len(buf)\n";

    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", source);
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfLexer lexer;
    uf_lexer_init(&lexer, "<test>", source, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog != NULL);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool ok = uf_analyze_program(&sema, prog);
    assert(ok);

    UfInterpretResult res = uf_interpret_program(&rt, prog);
    assert(res == UF_INTERPRET_OK);
    assert(!rt.had_runtime_error);

    UfValue b0_val, b1_val, b2_val, str_val, sz_val;
    assert(uf_env_lookup(rt.global_env, "b0", &b0_val) && b0_val.as.number == 72.0);
    assert(uf_env_lookup(rt.global_env, "b1", &b1_val) && b1_val.as.number == 105.0);
    assert(uf_env_lookup(rt.global_env, "b2", &b2_val) && b2_val.as.number == 33.0);
    assert(uf_env_lookup(rt.global_env, "str", &str_val) && strcmp(str_val.as.string->chars, "Hi!") == 0);
    assert(uf_env_lookup(rt.global_env, "sz", &sz_val) && sz_val.as.number == 8.0);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_systems_script_execution passed!\n");
}

static void test_fixed_width_and_endian_access(void) {
    const char* source =
        "let b = buffer(16)\n"
        "buffer_write_u16_le(b, 0, 4660)\n"
        "let r16 = buffer_read_u16_le(b, 0)\n"
        "buffer_write_u32_le(b, 4, 305419896)\n"
        "let r32 = buffer_read_u32_le(b, 4)\n"
        "buffer_write_i32_le(b, 8, -42)\n"
        "let ri32 = buffer_read_i32_le(b, 8)\n"
        "let c_u8 = u8(258)\n"
        "let c_i8 = i8(255)\n"
        "let c_u16 = u16(65538)\n"
        "let c_i16 = i16(65535)\n";

    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", source);
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfLexer lexer;
    uf_lexer_init(&lexer, "<test>", source, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog != NULL);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool ok = uf_analyze_program(&sema, prog);
    assert(ok);

    UfInterpretResult res = uf_interpret_program(&rt, prog);
    assert(res == UF_INTERPRET_OK);
    assert(!rt.had_runtime_error);

    UfValue r16, r32, ri32, c_u8, c_i8, c_u16, c_i16;
    assert(uf_env_lookup(rt.global_env, "r16", &r16) && r16.as.number == 4660.0);
    assert(uf_env_lookup(rt.global_env, "r32", &r32) && r32.as.number == 305419896.0);
    assert(uf_env_lookup(rt.global_env, "ri32", &ri32) && ri32.as.number == -42.0);
    assert(uf_env_lookup(rt.global_env, "c_u8", &c_u8) && c_u8.as.number == 2.0);
    assert(uf_env_lookup(rt.global_env, "c_i8", &c_i8) && c_i8.as.number == -1.0);
    assert(uf_env_lookup(rt.global_env, "c_u16", &c_u16) && c_u16.as.number == 2.0);
    assert(uf_env_lookup(rt.global_env, "c_i16", &c_i16) && c_i16.as.number == -1.0);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_fixed_width_and_endian_access passed!\n");
}

static void test_inspect_memory_layout(void) {
    const char* source =
        "let b = buffer(32)\n"
        "let info = inspect(b)\n"
        "let b_type = info[\"type\"]\n"
        "let b_sz = info[\"buffer_size\"]\n"
        "let arr = [10, 20, 30]\n"
        "let arr_info = inspect(arr)\n"
        "let arr_type = arr_info[\"type\"]\n"
        "let arr_count = arr_info[\"count\"]\n";

    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", source);
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    UfLexer lexer;
    uf_lexer_init(&lexer, "<test>", source, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog != NULL);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    bool ok = uf_analyze_program(&sema, prog);
    assert(ok);

    UfInterpretResult res = uf_interpret_program(&rt, prog);
    assert(res == UF_INTERPRET_OK);
    assert(!rt.had_runtime_error);

    UfValue b_type, b_sz, arr_type, arr_count;
    assert(uf_env_lookup(rt.global_env, "b_type", &b_type) && strcmp(b_type.as.string->chars, "buffer") == 0);
    assert(uf_env_lookup(rt.global_env, "b_sz", &b_sz) && b_sz.as.number == 32.0);
    assert(uf_env_lookup(rt.global_env, "arr_type", &arr_type) && strcmp(arr_type.as.string->chars, "array") == 0);
    assert(uf_env_lookup(rt.global_env, "arr_count", &arr_count) && arr_count.as.number == 3.0);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_inspect_memory_layout passed!\n");
}

static void test_buffer_gc_stress(void) {
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");
    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    for (int i = 0; i < 50; ++i) {
        UfValue b = uf_val_buffer(&rt, 1024);
        (void)b;
    }
    uf_gc_collect(&rt);

    UfValue live_buf = uf_val_buffer(&rt, 512);
    uf_runtime_push_temp_root(&rt, live_buf);
    live_buf.as.buffer->data[0] = 99;

    uf_gc_collect(&rt);
    assert(live_buf.as.buffer->data[0] == 99);

    uf_runtime_pop_temp_roots(&rt, 1);
    uf_runtime_free(&rt);
    printf("test_buffer_gc_stress passed!\n");
}

int main(void) {
    printf("=== Running Systems Programming & Buffer Unit Tests ===\n");
    test_buffer_creation_and_bounds();
    test_buffer_string_roundtrip();
    test_systems_script_execution();
    test_fixed_width_and_endian_access();
    test_inspect_memory_layout();
    test_buffer_gc_stress();
    printf("All systems programming unit tests passed!\n");
    return 0;
}
