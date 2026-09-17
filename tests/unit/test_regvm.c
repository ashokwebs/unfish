#include "uf_common.h"
#include "uf_arena.h"
#include "uf_string.h"
#include "uf_diagnostic.h"
#include "uf_token.h"
#include "uf_lexer.h"
#include "uf_ast.h"
#include "uf_parser.h"
#include "uf_semantic.h"
#include "uf_runtime.h"
#include "uf_stdlib.h"
#include "uf_interpreter.h"
#include "uf_regvm.h"
#include "uf_reg_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static void test_instruction_encoding(void) {
    /* Test ABC format */
    uint32_t instr_abc = REG_ENCODE_ABC(ROP_ADD, 12, 34, 56);
    assert(REG_GET_OP(instr_abc) == ROP_ADD);
    assert(REG_GET_A(instr_abc) == 12);
    assert(REG_GET_B(instr_abc) == 34);
    assert(REG_GET_C(instr_abc) == 56);

    /* Test ABx format */
    uint32_t instr_abx = REG_ENCODE_ABx(ROP_LOAD_K, 5, 12345);
    assert(REG_GET_OP(instr_abx) == ROP_LOAD_K);
    assert(REG_GET_A(instr_abx) == 5);
    assert(REG_GET_Bx(instr_abx) == 12345);

    /* Test sAx format */
    uint32_t instr_sax_pos = REG_ENCODE_sAx(ROP_JMP, 42);
    assert(REG_GET_OP(instr_sax_pos) == ROP_JMP);
    assert(REG_GET_sAx(instr_sax_pos) == 42);

    uint32_t instr_sax_neg = REG_ENCODE_sAx(ROP_JMP, -100);
    assert(REG_GET_OP(instr_sax_neg) == ROP_JMP);
    assert(REG_GET_sAx(instr_sax_neg) == -100);

    printf("test_instruction_encoding passed!\n");
}

static void test_raw_regvm_execution(void) {
    UfRuntime rt;
    uf_runtime_init(&rt, NULL);
    UfRegVM vm;
    uf_regvm_init(&vm, &rt);

    /* Construct a function that computes 10 + 25 = 35 */
    UfRegFunction* fn = uf_reg_fn_new(&rt, "test_add", 0, 0, false);
    fn->max_regs = 4;

    uint16_t k10 = (uint16_t)uf_reg_chunk_add_constant(&fn->chunk, uf_val_number(10));
    uint16_t k25 = (uint16_t)uf_reg_chunk_add_constant(&fn->chunk, uf_val_number(25));

    /* R1 = K[k10] */
    uf_reg_chunk_write(&fn->chunk, REG_ENCODE_ABx(ROP_LOAD_K, 1, k10), 1);
    /* R2 = K[k25] */
    uf_reg_chunk_write(&fn->chunk, REG_ENCODE_ABx(ROP_LOAD_K, 2, k25), 1);
    /* R3 = R1 + R2 */
    uf_reg_chunk_write(&fn->chunk, REG_ENCODE_ABC(ROP_ADD, 3, 1, 2), 1);
    /* return R3 */
    uf_reg_chunk_write(&fn->chunk, REG_ENCODE_ABC(ROP_RETURN, 3, 0, 0), 1);

    UfInterpretResult res = uf_regvm_run(&vm, fn);
    assert(res == UF_INTERPRET_OK);
    /* Verify result */
    assert(vm.frames[0].regs[3].kind == UF_VAL_NUMBER);
    assert(vm.frames[0].regs[3].as.number == 35.0);

    uf_regvm_free(&vm);
    uf_runtime_free(&rt);
    printf("test_raw_regvm_execution passed!\n");
}

static void test_compiled_script(const char* source, const char* expected_output) {
    UfArena arena;
    uf_arena_init(&arena, 16384);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "test.unfish", source);

    UfLexer lexer;
    uf_lexer_init(&lexer, "test.unfish", source, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);

    UfProgram* program = uf_parse_program(&parser);
    assert(program != NULL && !parser.had_error);

    UfSemanticAnalyzer analyzer;
    uf_semantic_init(&analyzer, &arena, &reporter);
    bool sem_ok = uf_analyze_program(&analyzer, program);
    assert(sem_ok && reporter.error_count == 0);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);

    char out_buf[4096] = {0};
    FILE* mem_stream = fmemopen(out_buf, sizeof(out_buf), "w");
    rt.out_stream = mem_stream;

    UfRegFunction* reg_fn = uf_reg_compile(program, &rt, &reporter);
    assert(reg_fn != NULL);

    UfRegVM vm;
    uf_regvm_init(&vm, &rt);
    UfInterpretResult res = uf_regvm_run(&vm, reg_fn);
    assert(res == UF_INTERPRET_OK);

    fflush(mem_stream);
    fclose(mem_stream);

    if (expected_output) {
        assert(strstr(out_buf, expected_output) != NULL);
    }

    uf_regvm_free(&vm);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
}

static void test_reg_compiler_programs(void) {
    /* 1. Arithmetic & variables */
    test_compiled_script(
        "let x = 10\n"
        "let y = 20\n"
        "say x + y\n",
        "30\n"
    );

    /* 2. Control flow: if / else */
    test_compiled_script(
        "let a = 15\n"
        "if a > 10:\n"
        "    say \"greater\"\n"
        "else:\n"
        "    say \"lesser\"\n",
        "greater\n"
    );

    /* 3. While loop */
    test_compiled_script(
        "let i = 0\n"
        "let sum = 0\n"
        "while i < 5:\n"
        "    sum = sum + i\n"
        "    i = i + 1\n"
        "say sum\n",
        "10\n"
    );

    /* 4. Functions & Recursion */
    test_compiled_script(
        "function fib(n):\n"
        "    if n <= 1:\n"
        "        return n\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "say fib(7)\n",
        "13\n"
    );

    /* 5. Closures & Upvalues */
    test_compiled_script(
        "function make_adder(x):\n"
        "    function add(y):\n"
        "        return x + y\n"
        "    return add\n"
        "let add5 = make_adder(5)\n"
        "say add5(10)\n",
        "15\n"
    );

    /* 6. Collections: Arrays */
    test_compiled_script(
        "let arr = [1, 2, 3]\n"
        "say arr[1]\n",
        "2\n"
    );

    /* 7. Collections: Maps */
    test_compiled_script(
        "let m = {\"greeting\": \"hello\"}\n"
        "say m[\"greeting\"]\n",
        "hello\n"
    );

    /* 8. Destructuring */
    test_compiled_script(
        "let [a, b] = [100, 200]\n"
        "say a + b\n",
        "300\n"
    );

    /* 9. Structs */
    test_compiled_script(
        "struct Point:\n"
        "    x: Number\n"
        "    y: Number\n"
        "let p = Point(3, 4)\n"
        "say p.x + p.y\n",
        "7\n"
    );

    printf("test_reg_compiler_programs passed!\n");
}


/* Regression: a deeply right-nested expression exhausts the 250 virtual
 * registers. The allocator used to return register 0 -- which aliases the
 * live closure slot -- so the program ran on corrupt bytecode and printed
 * nothing (or a wrong value) with no diagnostic. Compilation must now fail
 * with a reported error. */
static void test_reg_compiler_reports_internal_limits(void) {
    UfStrBuf sb;
    uf_strbuf_init(&sb);
    uf_strbuf_append(&sb, "let r = ");
    const int depth = 300;
    for (int i = 1; i <= depth; ++i) {
        char part[32];
        snprintf(part, sizeof(part), "(%d + ", i);
        uf_strbuf_append(&sb, part);
    }
    uf_strbuf_append(&sb, "0");
    for (int i = 0; i < depth; ++i) {
        uf_strbuf_append(&sb, ")");
    }
    uf_strbuf_append(&sb, "\nsay(r)\n");

    const char* src = sb.data;

    UfArena arena;
    uf_arena_init(&arena, 8192);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "test.unfish", src);

    UfLexer lexer;
    uf_lexer_init(&lexer, "test.unfish", src, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);
    UfProgram* prog = uf_parse_program(&parser);
    assert(prog && !parser.had_error);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);
    uf_stdlib_register_runtime(&rt);

    UfRegFunction* fn = uf_reg_compile(prog, &rt, &reporter);

    /* Must refuse to produce bytecode, and must say why. */
    assert(fn == NULL);
    assert(reporter.error_count > 0);

    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    uf_strbuf_free(&sb);
    printf("test_reg_compiler_reports_internal_limits passed!\n");
}

int main(void) {
    printf("Running Register VM unit tests...\n");
    test_instruction_encoding();
    test_raw_regvm_execution();
    test_reg_compiler_programs();
    test_reg_compiler_reports_internal_limits();
    printf("All Register VM unit tests passed successfully!\n");
    return 0;
}
