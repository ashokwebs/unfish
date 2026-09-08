#include "../../src/codegen/uf_emit_c.h"
#include "../../src/ast/uf_ast.h"
#include "../../src/parser/uf_parser.h"
#include "../../src/lexer/uf_lexer.h"
#include "../../src/semantic/uf_semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

static UfProgram* parse_string(const char* source, UfArena* arena, UfInterner* interner, UfDiagnosticReporter* reporter) {
    UfLexer lexer;
    uf_lexer_init(&lexer, "<test>", source, arena, interner, reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, arena, reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog != NULL);
    assert(reporter->error_count == 0);

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, arena, reporter);
    bool ok = uf_analyze_program(&sema, prog);
    assert(ok);
    return prog;
}

static void test_wasm_build_magic(void) {
    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");

    const char* code =
        "let x = 40\n"
        "let y = 2\n"
        "say x + y\n";

    UfProgram* prog = parse_string(code, &arena, &interner, &reporter);
    const char* wasm_path = "/tmp/test_uf_magic.wasm";
    bool built = uf_build_wasm(prog, wasm_path);
    assert(built);

    FILE* f = fopen(wasm_path, "rb");
    assert(f != NULL);
    uint8_t header[8];
    size_t n = fread(header, 1, 8, f);
    fclose(f);
    remove(wasm_path);

    assert(n == 8);
    /* WASM binary magic: 0x00 0x61 0x73 0x6d (\0asm) */
    assert(header[0] == 0x00);
    assert(header[1] == 0x61);
    assert(header[2] == 0x73);
    assert(header[3] == 0x6d);
    /* Version 1: 0x01 0x00 0x00 0x00 */
    assert(header[4] == 0x01);
    assert(header[5] == 0x00);
    assert(header[6] == 0x00);
    assert(header[7] == 0x00);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_wasm_build_magic passed!\n");
}

static void test_wasm_execution(void) {
    UfArena arena;
    uf_arena_init(&arena, 32768);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");

    const char* code =
        "function sum_to(n):\n"
        "    let acc = 0\n"
        "    for i in range(1, 11):\n"
        "        acc = acc + i\n"
        "    return acc\n"
        "say sum_to(10)\n";


    UfProgram* prog = parse_string(code, &arena, &interner, &reporter);
    const char* wasm_path = "/tmp/test_uf_sum.wasm";
    bool built = uf_build_wasm(prog, wasm_path);
    assert(built);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "node --no-warnings --experimental-wasi-unstable-preview1 tools/wasm/run_wasm.js %s", wasm_path);
    FILE* pipe = popen(cmd, "r");
    assert(pipe != NULL);
    char out_buf[128];
    char* s = fgets(out_buf, sizeof(out_buf), pipe);
    assert(s != NULL);
    pclose(pipe);
    remove(wasm_path);

    /* sum(1..10) = 55 */
    assert(atoi(out_buf) == 55);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_wasm_execution passed!\n");
}

static void test_wasm_enums_and_matching(void) {
    UfArena arena;
    uf_arena_init(&arena, 32768);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<test>", "");

    const char* code =
        "enum Result:\n"
        "    Ok(val)\n"
        "    Err(msg)\n"
        "let res = Result.Ok(100)\n"
        "match res:\n"
        "    when Result.Ok(v):\n"
        "        say v * 2\n"
        "    when Result.Err(m):\n"
        "        say 0\n";


    UfProgram* prog = parse_string(code, &arena, &interner, &reporter);
    const char* wasm_path = "/tmp/test_uf_enum.wasm";
    bool built = uf_build_wasm(prog, wasm_path);
    assert(built);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "node --no-warnings --experimental-wasi-unstable-preview1 tools/wasm/run_wasm.js %s", wasm_path);
    FILE* pipe = popen(cmd, "r");
    assert(pipe != NULL);
    char out_buf[128];
    char* s = fgets(out_buf, sizeof(out_buf), pipe);
    assert(s != NULL);
    pclose(pipe);
    remove(wasm_path);

    assert(atoi(out_buf) == 200);

    uf_interner_free(&interner);
    uf_arena_free(&arena);
    printf("test_wasm_enums_and_matching passed!\n");
}

int main(void) {
    printf("Running WebAssembly (WASM/WASI) compilation unit tests...\n");
    test_wasm_build_magic();
    test_wasm_execution();
    test_wasm_enums_and_matching();
    printf("All WebAssembly unit tests passed successfully!\n");
    return 0;
}
