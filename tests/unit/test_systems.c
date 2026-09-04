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

static void test_bitwise_and_hex_operations(void) {
    const char* source =
        "let a = band(240, 51)\n"          // 0xF0 & 0x33 = 0x30 = 48
        "let o = bor(16, 1)\n"             // 0x10 | 0x01 = 0x11 = 17
        "let x = bxor(255, 15)\n"          // 0xFF ^ 0x0F = 0xF0 = 240
        "let n = band(bnot(0), 255)\n"     // (~0) & 0xFF = 0xFF = 255
        "let sl = shl(1, 4)\n"             // 1 << 4 = 16
        "let sr = shr(32, 2)\n"            // 32 >> 2 = 8
        "let sa = sar(-16, 2)\n"           // -16 >> 2 = -4
        "let h1 = to_hex(255)\n"           // "ff"
        "let h2 = to_hex(4096)\n"          // "1000"
        "let n1 = from_hex(\"ff\")\n"      // 255
        "let n2 = from_hex(\"0x1000\")\n"  // 4096
        "let buf = buffer(3)\n"
        "buffer_set(buf, 0, 222)\n"        // 0xde
        "buffer_set(buf, 1, 173)\n"        // 0xad
        "buffer_set(buf, 2, 190)\n"        // 0xbe
        "let bhex = buffer_to_hex(buf)\n"  // "deadbe"
        "let buf2 = buffer_from_hex(\"deadbe\")\n"
        "let b2_0 = buffer_get(buf2, 0)\n"
        "let b2_1 = buffer_get(buf2, 1)\n"
        "let b2_2 = buffer_get(buf2, 2)\n";

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

    UfValue a_val, o_val, x_val, n_val, sl_val, sr_val, sa_val;
    UfValue h1_val, h2_val, n1_val, n2_val, bhex_val, b2_0, b2_1, b2_2;

    assert(uf_env_lookup(rt.global_env, "a", &a_val) && a_val.as.number == 48.0);
    assert(uf_env_lookup(rt.global_env, "o", &o_val) && o_val.as.number == 17.0);
    assert(uf_env_lookup(rt.global_env, "x", &x_val) && x_val.as.number == 240.0);
    assert(uf_env_lookup(rt.global_env, "n", &n_val) && n_val.as.number == 255.0);
    assert(uf_env_lookup(rt.global_env, "sl", &sl_val) && sl_val.as.number == 16.0);
    assert(uf_env_lookup(rt.global_env, "sr", &sr_val) && sr_val.as.number == 8.0);
    assert(uf_env_lookup(rt.global_env, "sa", &sa_val) && sa_val.as.number == -4.0);

    assert(uf_env_lookup(rt.global_env, "h1", &h1_val) && strcmp(h1_val.as.string->chars, "ff") == 0);
    assert(uf_env_lookup(rt.global_env, "h2", &h2_val) && strcmp(h2_val.as.string->chars, "1000") == 0);
    assert(uf_env_lookup(rt.global_env, "n1", &n1_val) && n1_val.as.number == 255.0);
    assert(uf_env_lookup(rt.global_env, "n2", &n2_val) && n2_val.as.number == 4096.0);
    assert(uf_env_lookup(rt.global_env, "bhex", &bhex_val) && strcmp(bhex_val.as.string->chars, "deadbe") == 0);

    assert(uf_env_lookup(rt.global_env, "b2_0", &b2_0) && b2_0.as.number == 222.0);
    assert(uf_env_lookup(rt.global_env, "b2_1", &b2_1) && b2_1.as.number == 173.0);
    assert(uf_env_lookup(rt.global_env, "b2_2", &b2_2) && b2_2.as.number == 190.0);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_bitwise_and_hex_operations passed!\n");
}

int main(void) {
    printf("=== Running Systems Programming & Buffer Unit Tests ===\n");
    test_buffer_creation_and_bounds();
    test_buffer_string_roundtrip();
    test_systems_script_execution();
    test_fixed_width_and_endian_access();
    test_inspect_memory_layout();
    test_buffer_gc_stress();
    test_bitwise_and_hex_operations();
    printf("All systems programming unit tests passed!\n");
    return 0;
}
