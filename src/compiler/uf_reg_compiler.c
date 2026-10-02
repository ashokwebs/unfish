#include "uf_reg_compiler.h"
#include "../vm2/uf_regvm.h"
#include "../runtime/uf_env.h"
#include "../runtime/uf_module.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* name;
    int depth;
    bool is_captured;
    uint8_t reg;
} UfRegLocal;

typedef struct {
    uint8_t index;
    bool is_local;
} UfRegUpvalue;

typedef struct UfRegLoop {
    int start_ip;
    int scope_depth;
    int try_depth;
    int* break_jumps;
    size_t break_count;
    size_t break_capacity;
    int* continue_jumps;
    size_t continue_count;
    size_t continue_capacity;
    struct UfRegLoop* enclosing;
} UfRegLoop;

typedef enum {
    REG_FN_SCRIPT,
    REG_FN_FUNCTION
} RegFunctionType;

/* One enclosing `try` statement, as seen from code compiled inside it.
 * `return`/`break`/`continue` leave these innermost-first: popping the
 * runtime handler if it is still installed, then running the finally block
 * inline, because the jump bypasses the normal fall-through into it. */
typedef struct {
    const UfStmt* finally_block; /* NULL when there is no finally */
    bool handler_active;         /* a ROP_PUSH_TRY is still in effect */
} UfRegTryContext;

#define UF_REG_MAX_TRY_CONTEXTS 64

typedef struct UfRegCompiler {
    struct UfRegCompiler* enclosing;
    RegFunctionType type;
    const char* fn_name;
    size_t arity;
    size_t min_arity;
    bool has_rest;

    UfRegChunk* chunk;
    UfRegFunction* function;

    UfRegLocal locals[256];
    int local_count;
    int scope_depth;
    int try_depth; /* number of entries in try_contexts */
    UfRegTryContext try_contexts[UF_REG_MAX_TRY_CONTEXTS];

    UfRegUpvalue upvalues[256];
    int upvalue_count;

    UfRegLoop* current_loop;

    uint8_t next_reg;
    uint8_t max_regs;

    UfRuntime* rt;
    UfDiagnosticReporter* reporter;
    bool had_error;
    /* Line of the statement/expression currently being compiled, so that
     * internal-limit errors raised deep in helpers still point somewhere useful. */
    int current_line;
} UfRegCompiler;

/* Report a compile-time failure (an internal limit, or a construct the
 * compiler cannot lower) as a real diagnostic instead of silently emitting
 * broken bytecode. Only the first error per compiler is reported; later ones
 * are almost always cascades of the first. */
static void compile_error(UfRegCompiler* c, int line, const char* fmt, ...) {
    if (c->had_error) return;
    c->had_error = true;
    if (!c->reporter) return;

    char message[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (line <= 0) line = c->current_line;
    const char* file = c->reporter->file_name;
    SourceLoc loc = source_loc_make(file, (uint32_t)(line > 0 ? line : 1), 1, 0);
    uf_report_error(c->reporter, UF_DIAG_SYNTAX_ERROR, source_span_make(loc, loc), "%s", message);
}

/* --- Register Allocation Helpers --- */

static uint8_t alloc_reg(UfRegCompiler* c) {
    if (c->next_reg >= 250) {
        compile_error(c, c->current_line,
                      "Expression too complex in function '%s': out of registers "
                      "(limit is 250); split it into smaller expressions",
                      c->fn_name ? c->fn_name : "<script>");
        return 0;
    }
    uint8_t r = c->next_reg++;
    if (c->next_reg > c->max_regs) {
        c->max_regs = c->next_reg;
    }
    return r;
}

static void free_reg(UfRegCompiler* c, uint8_t reg) {
    if (reg == c->next_reg - 1 && reg >= (uint8_t)c->local_count) {
        c->next_reg--;
    }
}

static uint8_t alloc_reg_block(UfRegCompiler* c, uint8_t count) {
    if (c->next_reg + count >= 250) {
        compile_error(c, c->current_line,
                      "Expression too complex in function '%s': out of registers "
                      "(limit is 250); split it into smaller expressions",
                      c->fn_name ? c->fn_name : "<script>");
        return 0;
    }
    uint8_t base = c->next_reg;
    c->next_reg += count;
    if (c->next_reg > c->max_regs) {
        c->max_regs = c->next_reg;
    }
    return base;
}

static void free_reg_block(UfRegCompiler* c, uint8_t base, uint8_t count) {
    if (base + count == c->next_reg && base >= (uint8_t)c->local_count) {
        c->next_reg = base;
    }
}

/* --- Instruction Emission Helpers --- */

static void emit_instr(UfRegCompiler* c, uint32_t instr, int line) {
    uf_reg_chunk_write(c->chunk, instr, line);
}

static void emit_abc(UfRegCompiler* c, UfRegOpcode op, uint8_t a, uint8_t b, uint8_t c_reg, int line) {
    emit_instr(c, REG_ENCODE_ABC(op, a, b, c_reg), line);
}

static void emit_abx(UfRegCompiler* c, UfRegOpcode op, uint8_t a, uint16_t bx, int line) {
    emit_instr(c, REG_ENCODE_ABx(op, a, bx), line);
}

static void emit_asbx(UfRegCompiler* c, UfRegOpcode op, uint8_t a, int16_t sbx, int line) {
    emit_instr(c, REG_ENCODE_ABx(op, a, (uint16_t)sbx), line);
}

static void emit_sax(UfRegCompiler* c, UfRegOpcode op, int32_t sax, int line) {
    emit_instr(c, REG_ENCODE_sAx(op, sax), line);
}

static size_t make_constant(UfRegCompiler* c, UfValue val) {
    return uf_reg_chunk_add_constant(c->chunk, val);
}

static uint16_t identifier_constant(UfRegCompiler* c, const char* name) {
    UfValue val = uf_val_string_cstr(c->rt, name);
    return (uint16_t)make_constant(c, val);
}

static int emit_jump(UfRegCompiler* c, UfRegOpcode op, uint8_t a, int line) {
    emit_asbx(c, op, a, 0, line);
    return (int)c->chunk->code_count - 1;
}

static void patch_jump_to_current(UfRegCompiler* c, int jump_idx) {
    int offset = (int)c->chunk->code_count - jump_idx - 1;
    uint32_t old = c->chunk->code[jump_idx];
    uint8_t op = REG_GET_OP(old);
    uint8_t a = REG_GET_A(old);
    c->chunk->code[jump_idx] = REG_ENCODE_ABx(op, a, (uint16_t)(int16_t)offset);
}

static void patch_jump_ax_to_current(UfRegCompiler* c, int jump_idx) {
    int offset = (int)c->chunk->code_count - jump_idx - 1;
    uint32_t old = c->chunk->code[jump_idx];
    uint8_t op = REG_GET_OP(old);
    c->chunk->code[jump_idx] = REG_ENCODE_sAx(op, offset);
}

/* --- Scope & Local Management --- */

static void begin_scope(UfRegCompiler* c) {
    c->scope_depth++;
}

static void end_scope(UfRegCompiler* c, int line) {
    c->scope_depth--;
    while (c->local_count > 0 && c->locals[c->local_count - 1].depth > c->scope_depth) {
        if (c->locals[c->local_count - 1].is_captured) {
            emit_abc(c, ROP_CLOSE_UPVAL, c->locals[c->local_count - 1].reg, 0, 0, line);
        }
        c->local_count--;
    }
    c->next_reg = (uint8_t)c->local_count;
}

static int add_local(UfRegCompiler* c, const char* name, int line) {
    if (c->local_count >= 250) {
        compile_error(c, line,
                      "Too many local variables in function '%s' (limit is 250); "
                      "split it into smaller functions",
                      c->fn_name ? c->fn_name : "<script>");
        return -1;
    }
    c->next_reg = (uint8_t)c->local_count;
    uint8_t reg = alloc_reg(c);
    UfRegLocal* local = &c->locals[c->local_count++];
    local->name = name;
    local->depth = -1; /* uninitialized */
    local->is_captured = false;
    local->reg = reg;
    return c->local_count - 1;
}

static void mark_initialized(UfRegCompiler* c) {
    if (c->scope_depth == 0) return;
    c->locals[c->local_count - 1].depth = c->scope_depth;
}

static void compile_stmt(UfRegCompiler* c, const UfStmt* stmt);

static void push_try_context(UfRegCompiler* c, const UfStmt* finally_block, int line) {
    if (c->try_depth >= UF_REG_MAX_TRY_CONTEXTS) {
        compile_error(c, line, "Too many nested try statements in function '%s' (limit is %d)",
                      c->fn_name ? c->fn_name : "<script>", UF_REG_MAX_TRY_CONTEXTS);
        return;
    }
    c->try_contexts[c->try_depth].finally_block = finally_block;
    c->try_contexts[c->try_depth].handler_active = true;
    c->try_depth++;
}

static void pop_try_context(UfRegCompiler* c) {
    if (c->try_depth > 0) c->try_depth--;
}

/* Emit the exit path through every try statement above `floor`, innermost
 * first: pop the handler if it is still installed, then inline the finally
 * block. Each finally is compiled with only the try statements outside it in
 * scope, so a `return` inside a finally does not re-run that same finally. */
static void emit_try_exits(UfRegCompiler* c, int floor, int line) {
    int saved = c->try_depth;
    for (int i = saved - 1; i >= floor; --i) {
        UfRegTryContext ctx = c->try_contexts[i];
        c->try_depth = i;
        if (ctx.handler_active) {
            emit_abc(c, ROP_POP_TRY, 0, 0, 0, line);
        }
        if (ctx.finally_block) {
            compile_stmt(c, ctx.finally_block);
        }
    }
    c->try_depth = saved;
}

static bool has_pending_finally(UfRegCompiler* c, int floor) {
    for (int i = floor; i < c->try_depth; ++i) {
        if (c->try_contexts[i].finally_block) return true;
    }
    return false;
}

static int resolve_local(UfRegCompiler* c, const char* name) {
    for (int i = c->local_count - 1; i >= 0; --i) {
        if (c->locals[i].name && strcmp(c->locals[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int add_upvalue(UfRegCompiler* c, uint8_t index, bool is_local) {
    for (int i = 0; i < c->upvalue_count; ++i) {
        if (c->upvalues[i].index == index && c->upvalues[i].is_local == is_local) {
            return i;
        }
    }
    if (c->upvalue_count >= 256) {
        compile_error(c, c->current_line,
                      "Too many captured variables in function '%s' (limit is 256)",
                      c->fn_name ? c->fn_name : "<script>");
        return 0;
    }
    c->upvalues[c->upvalue_count].index = index;
    c->upvalues[c->upvalue_count].is_local = is_local;
    return c->upvalue_count++;
}

static int resolve_upvalue(UfRegCompiler* c, const char* name) {
    if (c->enclosing == NULL) return -1;

    int local = resolve_local(c->enclosing, name);
    if (local != -1) {
        c->enclosing->locals[local].is_captured = true;
        return add_upvalue(c, c->enclosing->locals[local].reg, true);
    }

    int upval = resolve_upvalue(c->enclosing, name);
    if (upval != -1) {
        return add_upvalue(c, (uint8_t)upval, false);
    }
    return -1;
}

static void patch_loop_continues(UfRegCompiler* c, UfRegLoop* loop) {
    for (size_t i = 0; i < loop->continue_count; ++i) {
        patch_jump_to_current(c, loop->continue_jumps[i]);
    }
    if (loop->continue_jumps) free(loop->continue_jumps);
    loop->continue_jumps = NULL;
    loop->continue_count = 0;
    loop->continue_capacity = 0;
}

static void compiler_init(UfRegCompiler* c, UfRegCompiler* enclosing, RegFunctionType type,
                          const char* fn_name, size_t arity, size_t min_arity, bool has_rest,
                          UfRuntime* rt, UfDiagnosticReporter* reporter) {
    c->enclosing = enclosing;
    c->type = type;
    c->fn_name = fn_name;
    c->arity = arity;
    c->min_arity = min_arity;
    c->has_rest = has_rest;
    c->rt = rt;
    c->reporter = reporter;
    c->had_error = false;
    c->local_count = 0;
    c->scope_depth = 0;
    c->try_depth = 0;
    c->upvalue_count = 0;
    c->current_loop = NULL;
    c->next_reg = 0;
    c->max_regs = 0;
    c->current_line = enclosing ? enclosing->current_line : 0;

    c->function = uf_reg_fn_new(rt, fn_name, arity, min_arity, has_rest);
    c->chunk = &c->function->chunk;

    /* Reserve slot 0 for function itself / base */
    uint8_t r0 = alloc_reg(c);
    UfRegLocal* local = &c->locals[c->local_count++];
    local->depth = 0;
    local->is_captured = false;
    local->reg = r0;
    local->name = (type == REG_FN_FUNCTION) ? (fn_name ? fn_name : "") : "";
}

/* Forward declarations */
static uint8_t compile_expr(UfRegCompiler* c, const UfExpr* expr, int target_reg);
static void compile_stmt(UfRegCompiler* c, const UfStmt* stmt);
static void compile_destructure_pattern(UfRegCompiler* c, const UfPattern* pat, uint8_t target_reg, int line, bool is_decl);

/* --- Expression Compilation --- */

static uint8_t compile_expr(UfRegCompiler* c, const UfExpr* expr, int target_reg) {
    if (!expr || c->had_error) return 0;
    c->current_line = (int)expr->span.start.line;
    int line = (int)expr->span.start.line;
    uint8_t dst = (target_reg >= 0) ? (uint8_t)target_reg : alloc_reg(c);

    switch (expr->kind) {
        case UF_EXPR_LITERAL_NULL:
            emit_abc(c, ROP_LOAD_NULL, dst, 0, 0, line);
            break;

        case UF_EXPR_LITERAL_BOOL:
            if (expr->as.bool_val) {
                emit_abc(c, ROP_LOAD_TRUE, dst, 0, 0, line);
            } else {
                emit_abc(c, ROP_LOAD_FALSE, dst, 0, 0, line);
            }
            break;

        case UF_EXPR_LITERAL_NUMBER: {
            uint16_t k = (uint16_t)make_constant(c, uf_val_number(expr->as.number_val));
            emit_abx(c, ROP_LOAD_K, dst, k, line);
            break;
        }

        case UF_EXPR_LITERAL_STRING: {
            UfValue sval = uf_val_string(c->rt, expr->as.string_val, strlen(expr->as.string_val));
            uint16_t k = (uint16_t)make_constant(c, sval);
            emit_abx(c, ROP_LOAD_K, dst, k, line);
            break;
        }

        case UF_EXPR_IDENTIFIER: {
            const char* name = expr->as.identifier_name;
            int local = resolve_local(c, name);
            if (local != -1) {
                uint8_t src_reg = c->locals[local].reg;
                if (dst != src_reg) {
                    emit_abc(c, ROP_MOVE, dst, src_reg, 0, line);
                }
            } else {
                int upval = resolve_upvalue(c, name);
                if (upval != -1) {
                    emit_abc(c, ROP_GET_UPVAL, dst, (uint8_t)upval, 0, line);
                } else {
                    uint16_t k = identifier_constant(c, name);
                    emit_abx(c, ROP_GET_GLOBAL, dst, k, line);
                }
            }
            break;
        }

        case UF_EXPR_UNARY: {
            uint8_t r_op = compile_expr(c, expr->as.unary.operand, -1);
            if (expr->as.unary.op == UF_TOK_MINUS) {
                emit_abc(c, ROP_NEG, dst, r_op, 0, line);
            } else if (expr->as.unary.op == UF_TOK_NOT) {
                emit_abc(c, ROP_NOT, dst, r_op, 0, line);
            }
            free_reg(c, r_op);
            break;
        }

        case UF_EXPR_BINARY: {
            if (expr->as.binary.op == UF_TOK_AND) {
                compile_expr(c, expr->as.binary.left, dst);
                int jump = emit_jump(c, ROP_JMP_FALSE, dst, line);
                compile_expr(c, expr->as.binary.right, dst);
                patch_jump_to_current(c, jump);
                break;
            }
            if (expr->as.binary.op == UF_TOK_OR) {
                compile_expr(c, expr->as.binary.left, dst);
                int jump = emit_jump(c, ROP_JMP_TRUE, dst, line);
                compile_expr(c, expr->as.binary.right, dst);
                patch_jump_to_current(c, jump);
                break;
            }

            uint8_t r_left = compile_expr(c, expr->as.binary.left, -1);
            uint8_t r_right = compile_expr(c, expr->as.binary.right, -1);

            switch (expr->as.binary.op) {
                case UF_TOK_PLUS:    emit_abc(c, ROP_ADD, dst, r_left, r_right, line); break;
                case UF_TOK_MINUS:   emit_abc(c, ROP_SUB, dst, r_left, r_right, line); break;
                case UF_TOK_STAR:    emit_abc(c, ROP_MUL, dst, r_left, r_right, line); break;
                case UF_TOK_SLASH:   emit_abc(c, ROP_DIV, dst, r_left, r_right, line); break;
                case UF_TOK_PERCENT: emit_abc(c, ROP_MOD, dst, r_left, r_right, line); break;
                case UF_TOK_EQEQ:    emit_abc(c, ROP_EQ,  dst, r_left, r_right, line); break;
                case UF_TOK_BANGEQ:  emit_abc(c, ROP_NEQ, dst, r_left, r_right, line); break;
                case UF_TOK_LT:      emit_abc(c, ROP_LT,  dst, r_left, r_right, line); break;
                case UF_TOK_LTEQ:    emit_abc(c, ROP_LTE, dst, r_left, r_right, line); break;
                case UF_TOK_GT:      emit_abc(c, ROP_GT,  dst, r_left, r_right, line); break;
                case UF_TOK_GTEQ:    emit_abc(c, ROP_GTE, dst, r_left, r_right, line); break;
                default: break;
            }
            free_reg(c, r_right);
            free_reg(c, r_left);
            break;
        }

        case UF_EXPR_GROUPING:
            compile_expr(c, expr->as.grouping.inner, dst);
            break;

        case UF_EXPR_CALL: {
            bool has_spread = false;
            size_t argc = expr->as.call.argc;
            for (size_t i = 0; i < argc; ++i) {
                if (expr->as.call.args[i]->kind == UF_EXPR_SPREAD) {
                    has_spread = true;
                    break;
                }
            }

            if (!has_spread) {
                uint8_t base = alloc_reg_block(c, (uint8_t)(1 + argc));
                compile_expr(c, expr->as.call.callee, base);
                for (size_t i = 0; i < argc; ++i) {
                    compile_expr(c, expr->as.call.args[i], base + 1 + (uint8_t)i);
                }
                emit_abc(c, ROP_CALL, base, (uint8_t)argc, dst, line);
                free_reg_block(c, base, (uint8_t)(1 + argc));
            } else {
                uint8_t base = alloc_reg_block(c, 2);
                compile_expr(c, expr->as.call.callee, base);
                uint8_t r_args = base + 1;
                emit_abc(c, ROP_NEW_ARRAY, r_args, 0, 0, line);
                for (size_t i = 0; i < argc; ++i) {
                    if (expr->as.call.args[i]->kind == UF_EXPR_SPREAD) {
                        uint8_t r_spr = compile_expr(c, expr->as.call.args[i]->as.spread.operand, -1);
                        emit_abc(c, ROP_ARRAY_EXTEND, r_args, r_spr, 0, line);
                        free_reg(c, r_spr);
                    } else {
                        uint8_t r_elem = compile_expr(c, expr->as.call.args[i], -1);
                        emit_abc(c, ROP_ARRAY_PUSH, r_args, r_elem, 0, line);
                        free_reg(c, r_elem);
                    }
                }
                emit_abc(c, ROP_CALL_SPREAD, base, 0, dst, line);
                free_reg_block(c, base, 2);
            }
            break;
        }

        case UF_EXPR_ARRAY: {
            bool has_spread = false;
            size_t count = expr->as.array_lit.count;
            for (size_t i = 0; i < count; ++i) {
                if (expr->as.array_lit.elements[i]->kind == UF_EXPR_SPREAD) {
                    has_spread = true;
                    break;
                }
            }

            if (!has_spread) {
                uint8_t start = alloc_reg_block(c, (uint8_t)count);
                for (size_t i = 0; i < count; ++i) {
                    compile_expr(c, expr->as.array_lit.elements[i], start + (uint8_t)i);
                }
                emit_abc(c, ROP_NEW_ARRAY, dst, (uint8_t)count, start, line);
                free_reg_block(c, start, (uint8_t)count);
            } else {
                emit_abc(c, ROP_NEW_ARRAY, dst, 0, 0, line);
                for (size_t i = 0; i < count; ++i) {
                    if (expr->as.array_lit.elements[i]->kind == UF_EXPR_SPREAD) {
                        uint8_t r_spr = compile_expr(c, expr->as.array_lit.elements[i]->as.spread.operand, -1);
                        emit_abc(c, ROP_ARRAY_EXTEND, dst, r_spr, 0, line);
                        free_reg(c, r_spr);
                    } else {
                        uint8_t r_elem = compile_expr(c, expr->as.array_lit.elements[i], -1);
                        emit_abc(c, ROP_ARRAY_PUSH, dst, r_elem, 0, line);
                        free_reg(c, r_elem);
                    }
                }
            }
            break;
        }

        case UF_EXPR_INDEX: {
            uint8_t r_tgt = compile_expr(c, expr->as.index_expr.target, -1);
            uint8_t r_idx = compile_expr(c, expr->as.index_expr.index, -1);
            emit_abc(c, ROP_INDEX_GET, dst, r_tgt, r_idx, line);
            free_reg(c, r_idx);
            free_reg(c, r_tgt);
            break;
        }

        case UF_EXPR_MAP: {
            bool has_spread = false;
            size_t count = expr->as.map_lit.count;
            for (size_t i = 0; i < count; ++i) {
                if (expr->as.map_lit.values[i] == NULL || expr->as.map_lit.keys[i]->kind == UF_EXPR_SPREAD) {
                    has_spread = true;
                    break;
                }
            }

            if (!has_spread) {
                uint8_t start = alloc_reg_block(c, (uint8_t)(2 * count));
                for (size_t i = 0; i < count; ++i) {
                    compile_expr(c, expr->as.map_lit.keys[i], start + (uint8_t)(2 * i));
                    compile_expr(c, expr->as.map_lit.values[i], start + (uint8_t)(2 * i + 1));
                }
                emit_abc(c, ROP_NEW_MAP, dst, (uint8_t)count, start, line);
                free_reg_block(c, start, (uint8_t)(2 * count));
            } else {
                emit_abc(c, ROP_NEW_MAP, dst, 0, 0, line);
                for (size_t i = 0; i < count; ++i) {
                    if (expr->as.map_lit.values[i] == NULL) {
                        uint8_t r_spr = compile_expr(c, expr->as.map_lit.keys[i]->as.spread.operand, -1);
                        emit_abc(c, ROP_MAP_EXTEND, dst, r_spr, 0, line);
                        free_reg(c, r_spr);
                    } else {
                        uint8_t r_k = compile_expr(c, expr->as.map_lit.keys[i], -1);
                        uint8_t r_v = compile_expr(c, expr->as.map_lit.values[i], -1);
                        emit_abc(c, ROP_MAP_SET, dst, r_k, r_v, line);
                        free_reg(c, r_v);
                        free_reg(c, r_k);
                    }
                }
            }
            break;
        }

        case UF_EXPR_SPREAD:
            compile_expr(c, expr->as.spread.operand, dst);
            break;

        case UF_EXPR_AWAIT: {
            uint8_t sub_reg = compile_expr(c, expr->as.await_expr.value, -1);
            emit_abc(c, ROP_AWAIT, dst, sub_reg, 0, line);
            free_reg(c, sub_reg);
            break;
        }

        case UF_EXPR_FUNCTION: {
            UfRegCompiler fn_c;
            compiler_init(&fn_c, c, REG_FN_FUNCTION, expr->as.fn_expr.name,
                          expr->as.fn_expr.param_count, expr->as.fn_expr.min_param_count,
                          expr->as.fn_expr.has_rest, c->rt, c->reporter);
            fn_c.function->is_async = expr->as.fn_expr.is_async;
            begin_scope(&fn_c);

            for (size_t i = 0; i < expr->as.fn_expr.param_count; ++i) {
                add_local(&fn_c, expr->as.fn_expr.params[i], line);
                mark_initialized(&fn_c);
            }

            for (size_t i = expr->as.fn_expr.min_param_count; i < expr->as.fn_expr.param_count; ++i) {
                if (expr->as.fn_expr.param_defaults && expr->as.fn_expr.param_defaults[i]) {
                    int jump = emit_jump(&fn_c, ROP_JMP_ARG, (uint8_t)i, line);
                    uint8_t p_reg = fn_c.locals[1 + i].reg;
                    compile_expr(&fn_c, expr->as.fn_expr.param_defaults[i], p_reg);
                    patch_jump_to_current(&fn_c, jump);
                }
            }

            compile_stmt(&fn_c, expr->as.fn_expr.body);

            /* Implicit return null */
            uint8_t r_null = alloc_reg(&fn_c);
            emit_abc(&fn_c, ROP_LOAD_NULL, r_null, 0, 0, line);
            emit_abc(&fn_c, ROP_RETURN, r_null, 0, 0, line);

            fn_c.function->max_regs = fn_c.max_regs;
            fn_c.function->upvalue_count = fn_c.upvalue_count;
            if (fn_c.upvalue_count > 0) {
                fn_c.function->upvalues = (UfRegUpvalueDesc*)malloc(fn_c.upvalue_count * sizeof(UfRegUpvalueDesc));
                for (int i = 0; i < fn_c.upvalue_count; ++i) {
                    fn_c.function->upvalues[i].index = fn_c.upvalues[i].index;
                    fn_c.function->upvalues[i].is_local = fn_c.upvalues[i].is_local ? 1 : 0;
                }
            }

            /* A failure inside the nested function invalidates the whole
             * compilation: without this the enclosing compiler would happily
             * emit a closure over half-built bytecode and the VM would run it. */
            if (fn_c.had_error) {
                c->had_error = true;
                return dst;
            }

            uint16_t fn_idx = (uint16_t)make_constant(c, uf_val_reg_fn(c->rt, fn_c.function));
            emit_abx(c, ROP_CLOSURE, dst, fn_idx, line);
            break;
        }

        case UF_EXPR_STRING_INTERP: {
            uint16_t empty_k = (uint16_t)make_constant(c, uf_val_string(c->rt, "", 0));
            emit_abx(c, ROP_LOAD_K, dst, empty_k, line);
            for (size_t i = 0; i < expr->as.string_interp.count; ++i) {
                uint8_t r_part = compile_expr(c, expr->as.string_interp.parts[i], -1);
                emit_abc(c, ROP_ADD, dst, dst, r_part, line);
                free_reg(c, r_part);
            }
            break;
        }
    }

    return dst;
}

/* --- Destructuring Compilation --- */

static void compile_destructure_pattern(UfRegCompiler* c, const UfPattern* pat, uint8_t target_reg, int line, bool is_decl) {
    if (!pat) return;

    switch (pat->kind) {
        case UF_PAT_WILDCARD:
            break;

        case UF_PAT_LITERAL:
            break;

        case UF_PAT_VARIABLE: {
            if (is_decl) {
                if (c->scope_depth > 0) {
                    int slot = add_local(c, pat->as.var_name, line);
                    mark_initialized(c);
                    emit_abc(c, ROP_MOVE, c->locals[slot].reg, target_reg, 0, line);
                } else {
                    uint16_t g_idx = identifier_constant(c, pat->as.var_name);
                    emit_abx(c, ROP_DEF_GLOBAL, target_reg, g_idx, line);
                }
            } else {
                int local = resolve_local(c, pat->as.var_name);
                if (local != -1) {
                    emit_abc(c, ROP_MOVE, c->locals[local].reg, target_reg, 0, line);
                } else {
                    int upval = resolve_upvalue(c, pat->as.var_name);
                    if (upval != -1) {
                        emit_abc(c, ROP_SET_UPVAL, target_reg, (uint8_t)upval, 0, line);
                    } else {
                        uint16_t g_idx = identifier_constant(c, pat->as.var_name);
                        emit_abx(c, ROP_SET_GLOBAL, target_reg, g_idx, line);
                    }
                }
            }
            break;
        }

        case UF_PAT_REST:
            compile_destructure_pattern(c, pat->as.rest_pat.subpattern, target_reg, line, is_decl);
            break;

        case UF_PAT_ARRAY: {
            emit_abc(c, ROP_ASSERT_ARRAY, target_reg, 0, 0, line);
            size_t normal_count = pat->as.array_pat.has_rest
                                      ? (pat->as.array_pat.count > 0 ? pat->as.array_pat.count - 1 : 0)
                                      : pat->as.array_pat.count;
            for (size_t i = 0; i < normal_count; ++i) {
                UfPattern* ep = pat->as.array_pat.elements[i];
                if (ep->kind == UF_PAT_WILDCARD) continue;

                if (ep->kind == UF_PAT_VARIABLE && is_decl && c->scope_depth > 0) {
                    int slot = add_local(c, ep->as.var_name, line);
                    mark_initialized(c);
                    uint8_t r_idx = alloc_reg(c);
                    uint16_t k = (uint16_t)make_constant(c, uf_val_number((double)i));
                    emit_abx(c, ROP_LOAD_K, r_idx, k, line);
                    emit_abc(c, ROP_ARRAY_GET_SAFE, c->locals[slot].reg, target_reg, r_idx, line);
                    free_reg(c, r_idx);
                } else {
                    uint8_t r_elem = alloc_reg(c);
                    uint8_t r_idx = alloc_reg(c);
                    uint16_t k = (uint16_t)make_constant(c, uf_val_number((double)i));
                    emit_abx(c, ROP_LOAD_K, r_idx, k, line);
                    emit_abc(c, ROP_ARRAY_GET_SAFE, r_elem, target_reg, r_idx, line);
                    free_reg(c, r_idx);

                    compile_destructure_pattern(c, ep, r_elem, line, is_decl);
                    free_reg(c, r_elem);
                }
            }

            if (pat->as.array_pat.has_rest && pat->as.array_pat.count > 0) {
                UfPattern* rest_pat = pat->as.array_pat.elements[normal_count];
                if (rest_pat->kind == UF_PAT_REST) rest_pat = rest_pat->as.rest_pat.subpattern;
                if (rest_pat && rest_pat->kind == UF_PAT_VARIABLE && is_decl && c->scope_depth > 0) {
                    int rslot = add_local(c, rest_pat->as.var_name, line);
                    mark_initialized(c);
                    uint8_t r_start = alloc_reg(c);
                    uint16_t k = (uint16_t)make_constant(c, uf_val_number((double)normal_count));
                    emit_abx(c, ROP_LOAD_K, r_start, k, line);
                    emit_abc(c, ROP_ARRAY_SLICE, c->locals[rslot].reg, target_reg, r_start, line);
                    free_reg(c, r_start);
                } else if (rest_pat && rest_pat->kind != UF_PAT_WILDCARD) {
                    uint8_t r_rest = alloc_reg(c);
                    uint8_t r_start = alloc_reg(c);
                    uint16_t k = (uint16_t)make_constant(c, uf_val_number((double)normal_count));
                    emit_abx(c, ROP_LOAD_K, r_start, k, line);
                    emit_abc(c, ROP_ARRAY_SLICE, r_rest, target_reg, r_start, line);
                    free_reg(c, r_start);

                    compile_destructure_pattern(c, rest_pat, r_rest, line, is_decl);
                    free_reg(c, r_rest);
                }
            }
            break;
        }

        case UF_PAT_MAP: {
            emit_abc(c, ROP_ASSERT_MAP, target_reg, 0, 0, line);
            size_t count = pat->as.map_pat.count;
            for (size_t i = 0; i < count; ++i) {
                UfPattern* vp = pat->as.map_pat.values[i];
                if (vp->kind == UF_PAT_WILDCARD) continue;

                if (vp->kind == UF_PAT_VARIABLE && is_decl && c->scope_depth > 0) {
                    int slot = add_local(c, vp->as.var_name, line);
                    mark_initialized(c);
                    uint8_t r_key = alloc_reg(c);
                    uint16_t k = (uint16_t)make_constant(c, uf_val_string_cstr(c->rt, pat->as.map_pat.keys[i]));
                    emit_abx(c, ROP_LOAD_K, r_key, k, line);
                    emit_abc(c, ROP_MAP_GET_SAFE, c->locals[slot].reg, target_reg, r_key, line);
                    free_reg(c, r_key);
                } else {
                    uint8_t r_val = alloc_reg(c);
                    uint8_t r_key = alloc_reg(c);
                    uint16_t k = (uint16_t)make_constant(c, uf_val_string_cstr(c->rt, pat->as.map_pat.keys[i]));
                    emit_abx(c, ROP_LOAD_K, r_key, k, line);
                    emit_abc(c, ROP_MAP_GET_SAFE, r_val, target_reg, r_key, line);
                    free_reg(c, r_key);

                    compile_destructure_pattern(c, vp, r_val, line, is_decl);
                    free_reg(c, r_val);
                }
            }

            if (pat->as.map_pat.has_rest && pat->as.map_pat.rest_pattern) {
                UfPattern* rest_pat = pat->as.map_pat.rest_pattern;
                if (rest_pat->kind == UF_PAT_VARIABLE && is_decl && c->scope_depth > 0) {
                    int rslot = add_local(c, rest_pat->as.var_name, line);
                    mark_initialized(c);
                    emit_abc(c, ROP_MAP_REST, c->locals[rslot].reg, target_reg, (uint8_t)count, line);
                    for (size_t i = 0; i < count; ++i) {
                        uint16_t k_idx = (uint16_t)make_constant(c, uf_val_string_cstr(c->rt, pat->as.map_pat.keys[i]));
                        emit_abx(c, ROP_LOAD_K, 0, k_idx, line);
                    }
                } else if (rest_pat->kind != UF_PAT_WILDCARD) {
                    uint8_t r_rest = alloc_reg(c);
                    emit_abc(c, ROP_MAP_REST, r_rest, target_reg, (uint8_t)count, line);
                    for (size_t i = 0; i < count; ++i) {
                        uint16_t k_idx = (uint16_t)make_constant(c, uf_val_string_cstr(c->rt, pat->as.map_pat.keys[i]));
                        emit_abx(c, ROP_LOAD_K, 0, k_idx, line);
                    }
                    compile_destructure_pattern(c, rest_pat, r_rest, line, is_decl);
                    free_reg(c, r_rest);
                }
            }
            break;
        }

        case UF_PAT_STRUCT: {
            for (size_t i = 0; i < pat->as.struct_pat.field_count; ++i) {
                UfPattern* fp = pat->as.struct_pat.field_patterns[i];
                if (fp->kind == UF_PAT_WILDCARD) continue;

                uint8_t r_val = alloc_reg(c);
                uint8_t r_idx = alloc_reg(c);
                uint16_t k = (uint16_t)make_constant(c, uf_val_number((double)i));
                emit_abx(c, ROP_LOAD_K, r_idx, k, line);
                emit_abc(c, ROP_INDEX_GET, r_val, target_reg, r_idx, line);
                free_reg(c, r_idx);

                compile_destructure_pattern(c, fp, r_val, line, is_decl);
                free_reg(c, r_val);
            }
            break;
        }
    }
}

/* --- Method Compilation Helper --- */

static UfRegFunction* compile_reg_method_helper(UfRegCompiler* c, UfStmt* method, int line) {
    UfRegCompiler fn_c;
    compiler_init(&fn_c, c, REG_FN_FUNCTION, method->as.function_stmt.name,
                  method->as.function_stmt.param_count, method->as.function_stmt.min_param_count,
                  method->as.function_stmt.has_rest, c->rt, c->reporter);
    fn_c.function->is_async = method->as.function_stmt.is_async;
    begin_scope(&fn_c);

    for (size_t i = 0; i < method->as.function_stmt.param_count; ++i) {
        add_local(&fn_c, method->as.function_stmt.params[i], line);
        mark_initialized(&fn_c);
    }

    for (size_t i = method->as.function_stmt.min_param_count; i < method->as.function_stmt.param_count; ++i) {
        if (method->as.function_stmt.param_defaults && method->as.function_stmt.param_defaults[i]) {
            int jump = emit_jump(&fn_c, ROP_JMP_ARG, (uint8_t)i, line);
            uint8_t p_reg = fn_c.locals[1 + i].reg;
            compile_expr(&fn_c, method->as.function_stmt.param_defaults[i], p_reg);
            patch_jump_to_current(&fn_c, jump);
        }
    }

    compile_stmt(&fn_c, method->as.function_stmt.body);

    uint8_t r_null = alloc_reg(&fn_c);
    emit_abc(&fn_c, ROP_LOAD_NULL, r_null, 0, 0, line);
    emit_abc(&fn_c, ROP_RETURN, r_null, 0, 0, line);

    fn_c.function->max_regs = fn_c.max_regs;
    fn_c.function->upvalue_count = fn_c.upvalue_count;
    if (fn_c.upvalue_count > 0) {
        fn_c.function->upvalues = (UfRegUpvalueDesc*)malloc(fn_c.upvalue_count * sizeof(UfRegUpvalueDesc));
        for (int i = 0; i < fn_c.upvalue_count; ++i) {
            fn_c.function->upvalues[i].index = fn_c.upvalues[i].index;
            fn_c.function->upvalues[i].is_local = fn_c.upvalues[i].is_local ? 1 : 0;
        }
    }
    return fn_c.function;
}

/* --- Statement Compilation --- */

static void compile_stmt(UfRegCompiler* c, const UfStmt* stmt) {
    if (!stmt || c->had_error) return;
    int line = (int)stmt->span.start.line;
    c->current_line = line;

    switch (stmt->kind) {
        case UF_STMT_LET: {
            if (stmt->as.let_stmt.pattern) {
                if (c->scope_depth > 0) {
                    int init_slot = add_local(c, "_destruct_init", line);
                    uint8_t r_init = c->locals[init_slot].reg;
                    if (stmt->as.let_stmt.init) {
                        compile_expr(c, stmt->as.let_stmt.init, r_init);
                    } else {
                        emit_abc(c, ROP_LOAD_NULL, r_init, 0, 0, line);
                    }
                    mark_initialized(c);
                    compile_destructure_pattern(c, stmt->as.let_stmt.pattern, r_init, line, true);
                } else {
                    uint8_t r_init = alloc_reg(c);
                    if (stmt->as.let_stmt.init) {
                        compile_expr(c, stmt->as.let_stmt.init, r_init);
                    } else {
                        emit_abc(c, ROP_LOAD_NULL, r_init, 0, 0, line);
                    }
                    compile_destructure_pattern(c, stmt->as.let_stmt.pattern, r_init, line, true);
                    free_reg(c, r_init);
                }
                break;
            }

            if (c->scope_depth > 0) {
                int slot = add_local(c, stmt->as.let_stmt.name, line);
                uint8_t local_reg = c->locals[slot].reg;
                if (stmt->as.let_stmt.init) {
                    compile_expr(c, stmt->as.let_stmt.init, local_reg);
                } else {
                    emit_abc(c, ROP_LOAD_NULL, local_reg, 0, 0, line);
                }
                mark_initialized(c);
            } else {
                uint8_t r_val = alloc_reg(c);
                if (stmt->as.let_stmt.init) {
                    compile_expr(c, stmt->as.let_stmt.init, r_val);
                } else {
                    emit_abc(c, ROP_LOAD_NULL, r_val, 0, 0, line);
                }
                uint16_t g_idx = identifier_constant(c, stmt->as.let_stmt.name);
                emit_abx(c, ROP_DEF_GLOBAL, r_val, g_idx, line);
                free_reg(c, r_val);
            }
            break;
        }

        case UF_STMT_ASSIGN: {
            if (stmt->as.assign_stmt.pattern) {
                uint8_t r_val = alloc_reg(c);
                compile_expr(c, stmt->as.assign_stmt.value, r_val);
                compile_destructure_pattern(c, stmt->as.assign_stmt.pattern, r_val, line, false);
                free_reg(c, r_val);
                break;
            }

            const char* name = stmt->as.assign_stmt.name;
            int local = resolve_local(c, name);
            if (local != -1) {
                compile_expr(c, stmt->as.assign_stmt.value, c->locals[local].reg);
            } else {
                uint8_t r_val = compile_expr(c, stmt->as.assign_stmt.value, -1);
                int upval = resolve_upvalue(c, name);
                if (upval != -1) {
                    emit_abc(c, ROP_SET_UPVAL, r_val, (uint8_t)upval, 0, line);
                } else {
                    uint16_t g_idx = identifier_constant(c, name);
                    emit_abx(c, ROP_SET_GLOBAL, r_val, g_idx, line);
                }
                free_reg(c, r_val);
            }
            break;
        }

        case UF_STMT_INDEX_ASSIGN: {
            uint8_t r_tgt = compile_expr(c, stmt->as.index_assign.target, -1);
            uint8_t r_idx = compile_expr(c, stmt->as.index_assign.index, -1);
            uint8_t r_val = compile_expr(c, stmt->as.index_assign.value, -1);
            emit_abc(c, ROP_INDEX_SET, r_tgt, r_idx, r_val, line);
            free_reg(c, r_val);
            free_reg(c, r_idx);
            free_reg(c, r_tgt);
            break;
        }

        case UF_STMT_SAY: {
            uint8_t r_val = compile_expr(c, stmt->as.say_stmt.expr, -1);
            emit_abc(c, ROP_SAY, r_val, 0, 0, line);
            free_reg(c, r_val);
            break;
        }

        case UF_STMT_EXPR: {
            uint8_t r = compile_expr(c, stmt->as.expr_stmt.expr, -1);
            free_reg(c, r);
            break;
        }

        case UF_STMT_IF: {
            uint8_t r_cond = compile_expr(c, stmt->as.if_stmt.condition, -1);
            int jump_false = emit_jump(c, ROP_JMP_FALSE, r_cond, line);
            free_reg(c, r_cond);

            compile_stmt(c, stmt->as.if_stmt.then_branch);

            if (stmt->as.if_stmt.else_branch) {
                int jump_end = (int)c->chunk->code_count;
                emit_sax(c, ROP_JMP, 0, line);
                patch_jump_to_current(c, jump_false);
                compile_stmt(c, stmt->as.if_stmt.else_branch);
                patch_jump_ax_to_current(c, jump_end);
            } else {
                patch_jump_to_current(c, jump_false);
            }
            break;
        }

        case UF_STMT_WHILE: {
            UfRegLoop loop;
            loop.start_ip = (int)c->chunk->code_count;
            loop.scope_depth = c->scope_depth;
            loop.try_depth = c->try_depth;
            loop.break_jumps = NULL;
            loop.break_count = 0;
            loop.break_capacity = 0;
            loop.continue_jumps = NULL;
            loop.continue_count = 0;
            loop.continue_capacity = 0;
            loop.enclosing = c->current_loop;
            c->current_loop = &loop;

            uint8_t r_cond = compile_expr(c, stmt->as.while_stmt.condition, -1);
            int exit_jump = emit_jump(c, ROP_JMP_FALSE, r_cond, line);
            free_reg(c, r_cond);

            compile_stmt(c, stmt->as.while_stmt.body);

            patch_loop_continues(c, &loop);
            int loop_offset = (int)c->chunk->code_count - loop.start_ip + 1;
            emit_abx(c, ROP_LOOP, 0, (uint16_t)loop_offset, line);
            patch_jump_to_current(c, exit_jump);

            for (size_t i = 0; i < loop.break_count; ++i) {
                patch_jump_ax_to_current(c, loop.break_jumps[i]);
            }
            if (loop.break_jumps) free(loop.break_jumps);
            c->current_loop = loop.enclosing;
            break;
        }

        case UF_STMT_REPEAT: {
            begin_scope(c);
            int count_local = add_local(c, "_repeat_count", line);
            uint8_t count_reg = c->locals[count_local].reg;
            compile_expr(c, stmt->as.repeat_stmt.count_expr, count_reg);
            mark_initialized(c);

            UfRegLoop loop;
            loop.start_ip = (int)c->chunk->code_count;
            loop.scope_depth = c->scope_depth;
            loop.try_depth = c->try_depth;
            loop.break_jumps = NULL;
            loop.break_count = 0;
            loop.break_capacity = 0;
            loop.continue_jumps = NULL;
            loop.continue_count = 0;
            loop.continue_capacity = 0;
            loop.enclosing = c->current_loop;
            c->current_loop = &loop;

            uint8_t r_zero = alloc_reg(c);
            uint16_t k_zero = (uint16_t)make_constant(c, uf_val_number(0));
            emit_abx(c, ROP_LOAD_K, r_zero, k_zero, line);
            uint8_t r_cmp = alloc_reg(c);
            emit_abc(c, ROP_GT, r_cmp, count_reg, r_zero, line);
            free_reg(c, r_zero);
            int exit_jump = emit_jump(c, ROP_JMP_FALSE, r_cmp, line);
            free_reg(c, r_cmp);

            compile_stmt(c, stmt->as.repeat_stmt.body);

            patch_loop_continues(c, &loop);
            uint8_t r_one = alloc_reg(c);
            uint16_t k_one = (uint16_t)make_constant(c, uf_val_number(1));
            emit_abx(c, ROP_LOAD_K, r_one, k_one, line);
            emit_abc(c, ROP_SUB, count_reg, count_reg, r_one, line);
            free_reg(c, r_one);

            int loop_offset = (int)c->chunk->code_count - loop.start_ip + 1;
            emit_abx(c, ROP_LOOP, 0, (uint16_t)loop_offset, line);
            patch_jump_to_current(c, exit_jump);

            for (size_t i = 0; i < loop.break_count; ++i) {
                patch_jump_ax_to_current(c, loop.break_jumps[i]);
            }
            if (loop.break_jumps) free(loop.break_jumps);
            c->current_loop = loop.enclosing;
            end_scope(c, line);
            break;
        }

        case UF_STMT_FOR: {
            begin_scope(c);
            int iter_local = add_local(c, "_for_iterable", line);
            uint8_t iter_reg = c->locals[iter_local].reg;
            compile_expr(c, stmt->as.for_stmt.iterable, iter_reg);
            mark_initialized(c);

            int idx_local = add_local(c, "_for_index", line);
            uint8_t idx_reg = c->locals[idx_local].reg;
            uint16_t k_zero = (uint16_t)make_constant(c, uf_val_number(0));
            emit_abx(c, ROP_LOAD_K, idx_reg, k_zero, line);
            mark_initialized(c);

            UfRegLoop loop;
            loop.start_ip = (int)c->chunk->code_count;
            loop.scope_depth = c->scope_depth;
            loop.try_depth = c->try_depth;
            loop.break_jumps = NULL;
            loop.break_count = 0;
            loop.break_capacity = 0;
            loop.continue_jumps = NULL;
            loop.continue_count = 0;
            loop.continue_capacity = 0;
            loop.enclosing = c->current_loop;
            c->current_loop = &loop;

            /* Check index bounds */
            uint8_t r_len = alloc_reg(c);
            uint8_t r_len_fn = alloc_reg(c);
            uint16_t len_k = identifier_constant(c, "len");
            emit_abx(c, ROP_GET_GLOBAL, r_len_fn, len_k, line);
            uint8_t call_base = alloc_reg_block(c, 2);
            emit_abc(c, ROP_MOVE, call_base, r_len_fn, 0, line);
            emit_abc(c, ROP_MOVE, call_base + 1, iter_reg, 0, line);
            emit_abc(c, ROP_CALL, call_base, 1, r_len, line);
            free_reg_block(c, call_base, 2);
            free_reg(c, r_len_fn);

            uint8_t r_cmp = alloc_reg(c);
            emit_abc(c, ROP_LT, r_cmp, idx_reg, r_len, line);
            int exit_jump = emit_jump(c, ROP_JMP_FALSE, r_cmp, line);
            free_reg(c, r_cmp);
            free_reg(c, r_len);

            /* Fetch current element in inner scope */
            begin_scope(c);
            int var_local = add_local(c, stmt->as.for_stmt.var_name, line);
            uint8_t var_reg = c->locals[var_local].reg;
            emit_abc(c, ROP_ITER_GET, var_reg, iter_reg, idx_reg, line);
            mark_initialized(c);

            compile_stmt(c, stmt->as.for_stmt.body);
            end_scope(c, line);

            patch_loop_continues(c, &loop);
            uint8_t r_one = alloc_reg(c);
            uint16_t k_one = (uint16_t)make_constant(c, uf_val_number(1));
            emit_abx(c, ROP_LOAD_K, r_one, k_one, line);
            emit_abc(c, ROP_ADD, idx_reg, idx_reg, r_one, line);
            free_reg(c, r_one);

            int loop_offset = (int)c->chunk->code_count - loop.start_ip + 1;
            emit_abx(c, ROP_LOOP, 0, (uint16_t)loop_offset, line);
            patch_jump_to_current(c, exit_jump);

            for (size_t i = 0; i < loop.break_count; ++i) {
                patch_jump_ax_to_current(c, loop.break_jumps[i]);
            }
            if (loop.break_jumps) free(loop.break_jumps);
            c->current_loop = loop.enclosing;
            end_scope(c, line);
            break;
        }

        case UF_STMT_BREAK: {
            if (!c->current_loop) {
                compile_error(c, line, "'break' outside of a loop");
                return;
            }
            emit_try_exits(c, c->current_loop->try_depth, line);
            for (int i = c->local_count - 1; i >= 0; --i) {
                if (c->locals[i].depth <= c->current_loop->scope_depth) break;
                if (c->locals[i].is_captured) {
                    emit_abc(c, ROP_CLOSE_UPVAL, c->locals[i].reg, 0, 0, line);
                }
            }
            if (c->current_loop->break_count >= c->current_loop->break_capacity) {
                size_t old_cap = c->current_loop->break_capacity;
                c->current_loop->break_capacity = (old_cap < 8) ? 8 : old_cap * 2;
                c->current_loop->break_jumps = (int*)realloc(c->current_loop->break_jumps,
                                                            c->current_loop->break_capacity * sizeof(int));
            }
            int jump = (int)c->chunk->code_count;
            emit_sax(c, ROP_JMP, 0, line);
            c->current_loop->break_jumps[c->current_loop->break_count++] = jump;
            break;
        }

        case UF_STMT_CONTINUE: {
            if (!c->current_loop) {
                compile_error(c, line, "'continue' outside of a loop");
                return;
            }
            emit_try_exits(c, c->current_loop->try_depth, line);
            for (int i = c->local_count - 1; i >= 0; --i) {
                if (c->locals[i].depth <= c->current_loop->scope_depth) break;
                if (c->locals[i].is_captured) {
                    emit_abc(c, ROP_CLOSE_UPVAL, c->locals[i].reg, 0, 0, line);
                }
            }
            if (c->current_loop->continue_count >= c->current_loop->continue_capacity) {
                size_t old_cap = c->current_loop->continue_capacity;
                c->current_loop->continue_capacity = (old_cap < 8) ? 8 : old_cap * 2;
                c->current_loop->continue_jumps = (int*)realloc(c->current_loop->continue_jumps,
                                                               c->current_loop->continue_capacity * sizeof(int));
            }
            int jump = emit_jump(c, ROP_JMP_FALSE, 0, line); /* will patch to continue target */
            c->current_loop->continue_jumps[c->current_loop->continue_count++] = jump;
            break;
        }

        case UF_STMT_FUNCTION: {
            UfRegCompiler fn_c;
            compiler_init(&fn_c, c, REG_FN_FUNCTION, stmt->as.function_stmt.name,
                          stmt->as.function_stmt.param_count, stmt->as.function_stmt.min_param_count,
                          stmt->as.function_stmt.has_rest, c->rt, c->reporter);
            fn_c.function->is_async = stmt->as.function_stmt.is_async;
            begin_scope(&fn_c);

            for (size_t i = 0; i < stmt->as.function_stmt.param_count; ++i) {
                add_local(&fn_c, stmt->as.function_stmt.params[i], line);
                mark_initialized(&fn_c);
            }

            for (size_t i = stmt->as.function_stmt.min_param_count; i < stmt->as.function_stmt.param_count; ++i) {
                if (stmt->as.function_stmt.param_defaults && stmt->as.function_stmt.param_defaults[i]) {
                    int jump = emit_jump(&fn_c, ROP_JMP_ARG, (uint8_t)i, line);
                    uint8_t p_reg = fn_c.locals[1 + i].reg;
                    compile_expr(&fn_c, stmt->as.function_stmt.param_defaults[i], p_reg);
                    patch_jump_to_current(&fn_c, jump);
                }
            }

            compile_stmt(&fn_c, stmt->as.function_stmt.body);

            uint8_t r_null = alloc_reg(&fn_c);
            emit_abc(&fn_c, ROP_LOAD_NULL, r_null, 0, 0, line);
            emit_abc(&fn_c, ROP_RETURN, r_null, 0, 0, line);

            fn_c.function->max_regs = fn_c.max_regs;
            fn_c.function->upvalue_count = fn_c.upvalue_count;
            if (fn_c.upvalue_count > 0) {
                fn_c.function->upvalues = (UfRegUpvalueDesc*)malloc(fn_c.upvalue_count * sizeof(UfRegUpvalueDesc));
                for (int i = 0; i < fn_c.upvalue_count; ++i) {
                    fn_c.function->upvalues[i].index = fn_c.upvalues[i].index;
                    fn_c.function->upvalues[i].is_local = fn_c.upvalues[i].is_local ? 1 : 0;
                }
            }

            /* A failure inside the nested function invalidates the whole
             * compilation: without this the enclosing compiler would happily
             * emit a closure over half-built bytecode and the VM would run it. */
            if (fn_c.had_error) {
                c->had_error = true;
                return;
            }

            uint16_t fn_idx = (uint16_t)make_constant(c, uf_val_reg_fn(c->rt, fn_c.function));

            if (c->scope_depth > 0) {
                int slot = add_local(c, stmt->as.function_stmt.name, line);
                emit_abx(c, ROP_CLOSURE, c->locals[slot].reg, fn_idx, line);
                mark_initialized(c);
            } else {
                uint8_t r_cl = alloc_reg(c);
                emit_abx(c, ROP_CLOSURE, r_cl, fn_idx, line);
                uint16_t g_idx = identifier_constant(c, stmt->as.function_stmt.name);
                emit_abx(c, ROP_DEF_GLOBAL, r_cl, g_idx, line);
                free_reg(c, r_cl);
            }
            break;
        }

        case UF_STMT_RETURN: {
            /* Evaluate the value while the enclosing handlers are still
             * installed: an error raised by the return expression itself must
             * be caught by the try it is written in. */
            uint8_t r_val;
            if (stmt->as.return_stmt.value) {
                r_val = compile_expr(c, stmt->as.return_stmt.value, -1);
            } else {
                r_val = alloc_reg(c);
                emit_abc(c, ROP_LOAD_NULL, r_val, 0, 0, line);
            }
            if (has_pending_finally(c, 0)) {
                /* Park the value in a hidden local so the finally blocks
                 * neither clobber its register nor change what is returned
                 * by reassigning the variable it came from. */
                int slot = add_local(c, "", line);
                if (slot < 0) break;
                mark_initialized(c);
                uint8_t r_ret = c->locals[slot].reg;
                if (r_ret != r_val) emit_abc(c, ROP_MOVE, r_ret, r_val, 0, line);
                emit_try_exits(c, 0, line);
                emit_abc(c, ROP_RETURN, r_ret, 0, 0, line);
                c->local_count--;
                c->next_reg = (uint8_t)c->local_count;
            } else {
                emit_try_exits(c, 0, line);
                emit_abc(c, ROP_RETURN, r_val, 0, 0, line);
                free_reg(c, r_val);
            }
            break;
        }

        case UF_STMT_BLOCK: {
            begin_scope(c);
            for (size_t i = 0; i < stmt->as.block.count; ++i) {
                compile_stmt(c, stmt->as.block.stmts[i]);
            }
            end_scope(c, line);
            break;
        }

        case UF_STMT_TRY_CATCH: {
            const UfStmt* finally_block = stmt->as.try_catch.finally_block;
            uint8_t r_err = alloc_reg(c);
            int try_jump = (int)c->chunk->code_count;
            emit_abx(c, ROP_PUSH_TRY, r_err, 0, line);
            push_try_context(c, finally_block, line);

            compile_stmt(c, stmt->as.try_catch.try_block);
            emit_abc(c, ROP_POP_TRY, 0, 0, 0, line);
            pop_try_context(c);

            int skip_catch = (int)c->chunk->code_count;
            emit_sax(c, ROP_JMP, 0, line);

            /* Catch offset relative to instruction following ROP_PUSH_TRY */
            int catch_offset = (int)c->chunk->code_count - try_jump - 1;
            c->chunk->code[try_jump] = REG_ENCODE_ABx(ROP_PUSH_TRY, r_err, (uint16_t)catch_offset);

            if (stmt->as.try_catch.catch_block) {
                begin_scope(c);
                if (stmt->as.try_catch.catch_var) {
                    int err_slot = add_local(c, stmt->as.try_catch.catch_var, line);
                    emit_abc(c, ROP_MOVE, c->locals[err_slot].reg, r_err, 0, line);
                    mark_initialized(c);
                }
                if (finally_block) {
                    /* Guard the catch clause: an error raised inside it must
                     * still run the finally block before propagating. */
                    uint8_t r_guard_err = alloc_reg(c);
                    int guard_jump = (int)c->chunk->code_count;
                    emit_abx(c, ROP_PUSH_TRY, r_guard_err, 0, line);
                    push_try_context(c, finally_block, line);
                    compile_stmt(c, stmt->as.try_catch.catch_block);
                    emit_abc(c, ROP_POP_TRY, 0, 0, 0, line);
                    pop_try_context(c);
                    end_scope(c, line);
                    int catch_done = (int)c->chunk->code_count;
                    emit_sax(c, ROP_JMP, 0, line);

                    int guard_offset = (int)c->chunk->code_count - guard_jump - 1;
                    c->chunk->code[guard_jump] = REG_ENCODE_ABx(ROP_PUSH_TRY, r_guard_err, (uint16_t)guard_offset);
                    begin_scope(c);
                    int pending_slot = add_local(c, "_rethrow_err", line);
                    emit_abc(c, ROP_MOVE, c->locals[pending_slot].reg, r_guard_err, 0, line);
                    mark_initialized(c);
                    compile_stmt(c, finally_block);
                    emit_abc(c, ROP_RETHROW, c->locals[pending_slot].reg, 0, 0, line);
                    end_scope(c, line);

                    patch_jump_ax_to_current(c, catch_done);
                } else {
                    compile_stmt(c, stmt->as.try_catch.catch_block);
                    end_scope(c, line);
                }

                patch_jump_ax_to_current(c, skip_catch);
                free_reg(c, r_err);

                if (stmt->as.try_catch.finally_block) {
                    compile_stmt(c, stmt->as.try_catch.finally_block);
                }
            } else {
                begin_scope(c);
                int err_slot = add_local(c, "_rethrow_err", line);
                emit_abc(c, ROP_MOVE, c->locals[err_slot].reg, r_err, 0, line);
                mark_initialized(c);

                if (stmt->as.try_catch.finally_block) {
                    compile_stmt(c, stmt->as.try_catch.finally_block);
                }

                emit_abc(c, ROP_RETHROW, c->locals[err_slot].reg, 0, 0, line);
                end_scope(c, line);

                patch_jump_ax_to_current(c, skip_catch);
                free_reg(c, r_err);

                if (stmt->as.try_catch.finally_block) {
                    compile_stmt(c, stmt->as.try_catch.finally_block);
                }
            }
            break;
        }

        case UF_STMT_TRAIT: {
            UfValue tdef = uf_val_trait_def(c->rt,
                                             stmt->as.trait_stmt.name,
                                             stmt->as.trait_stmt.method_count,
                                             stmt->as.trait_stmt.method_names,
                                             stmt->as.trait_stmt.method_param_counts,
                                             stmt->as.trait_stmt.method_param_names,
                                             stmt->as.trait_stmt.method_param_types,
                                             stmt->as.trait_stmt.method_return_types);
            uf_env_declare(c->rt->global_env, stmt->as.trait_stmt.name, tdef);
            uint16_t t_const = (uint16_t)make_constant(c, tdef);
            if (c->scope_depth > 0) {
                int slot = add_local(c, stmt->as.trait_stmt.name, line);
                emit_abx(c, ROP_LOAD_K, c->locals[slot].reg, t_const, line);
                mark_initialized(c);
            } else {
                uint8_t r_t = alloc_reg(c);
                emit_abx(c, ROP_LOAD_K, r_t, t_const, line);
                uint16_t g_idx = identifier_constant(c, stmt->as.trait_stmt.name);
                emit_abx(c, ROP_DEF_GLOBAL, r_t, g_idx, line);
                free_reg(c, r_t);
            }
            break;
        }

        case UF_STMT_STRUCT: {
            size_t direct_mcount = stmt->as.struct_stmt.method_count;
            size_t total_mcount = direct_mcount;
            for (size_t b = 0; b < stmt->as.struct_stmt.impl_block_count; ++b) {
                total_mcount += stmt->as.struct_stmt.impl_blocks[b]->as.impl_stmt.method_count;
            }
            const char** mnames = NULL;
            UfValue* mvals = NULL;
            if (total_mcount > 0) {
                mnames = (const char**)malloc(total_mcount * sizeof(const char*));
                mvals = (UfValue*)malloc(total_mcount * sizeof(UfValue));
                size_t idx = 0;
                for (size_t m = 0; m < direct_mcount; ++m) {
                    UfStmt* mstmt = stmt->as.struct_stmt.methods[m];
                    mnames[idx] = mstmt->as.function_stmt.name;
                    UfRegFunction* mfn = compile_reg_method_helper(c, mstmt, line);
                    mvals[idx] = uf_val_reg_closure(c->rt, uf_reg_closure_new(c->rt, mfn));
                    idx++;
                }
                for (size_t b = 0; b < stmt->as.struct_stmt.impl_block_count; ++b) {
                    UfStmt* ib = stmt->as.struct_stmt.impl_blocks[b];
                    for (size_t m = 0; m < ib->as.impl_stmt.method_count; ++m) {
                        UfStmt* mstmt = ib->as.impl_stmt.methods[m];
                        mnames[idx] = mstmt->as.function_stmt.name;
                        UfRegFunction* mfn = compile_reg_method_helper(c, mstmt, line);
                        mvals[idx] = uf_val_reg_closure(c->rt, uf_reg_closure_new(c->rt, mfn));
                        idx++;
                    }
                }
            }

            size_t trait_count = stmt->as.struct_stmt.impl_block_count;
            const char** traits = NULL;
            if (trait_count > 0) {
                traits = (const char**)malloc(trait_count * sizeof(const char*));
                for (size_t b = 0; b < trait_count; ++b) {
                    traits[b] = stmt->as.struct_stmt.impl_blocks[b]->as.impl_stmt.trait_name;
                }
            }

            UfValue sdef = uf_val_struct_def_with_traits(c->rt, stmt->as.struct_stmt.name,
                                                         stmt->as.struct_stmt.field_names,
                                                         stmt->as.struct_stmt.field_types,
                                                         stmt->as.struct_stmt.field_count,
                                                         mnames,
                                                         mvals,
                                                         total_mcount,
                                                         traits,
                                                         trait_count);
            uf_env_declare(c->rt->global_env, stmt->as.struct_stmt.name, sdef);
            uint16_t s_const = (uint16_t)make_constant(c, sdef);
            if (c->scope_depth > 0) {
                int slot = add_local(c, stmt->as.struct_stmt.name, line);
                emit_abx(c, ROP_STRUCT_DEF, c->locals[slot].reg, s_const, line);
                mark_initialized(c);
            } else {
                uint8_t r_s = alloc_reg(c);
                emit_abx(c, ROP_STRUCT_DEF, r_s, s_const, line);
                uint16_t g_idx = identifier_constant(c, stmt->as.struct_stmt.name);
                emit_abx(c, ROP_DEF_GLOBAL, r_s, g_idx, line);
                free_reg(c, r_s);
            }
            break;
        }

        case UF_STMT_IMPL: {
            const char* sname = stmt->as.impl_stmt.struct_name;
            if (sname) {
                UfValue sval;
                if (uf_env_lookup(c->rt->global_env, sname, &sval) && sval.kind == UF_VAL_STRUCT_DEF) {
                    UfStructDefObject* sdef = sval.as.struct_def;
                    size_t add_mcount = stmt->as.impl_stmt.method_count;
                    if (add_mcount > 0) {
                        size_t new_mcount = sdef->method_count + add_mcount;
                        sdef->method_names = (const char**)realloc((void*)sdef->method_names, new_mcount * sizeof(const char*));
                        sdef->method_values = (UfValue*)realloc((void*)sdef->method_values, new_mcount * sizeof(UfValue));
                        for (size_t i = 0; i < add_mcount; ++i) {
                            UfStmt* mstmt = stmt->as.impl_stmt.methods[i];
                            sdef->method_names[sdef->method_count + i] = mstmt->as.function_stmt.name;
                            UfRegFunction* mfn = compile_reg_method_helper(c, mstmt, line);
                            sdef->method_values[sdef->method_count + i] = uf_val_reg_closure(c->rt, uf_reg_closure_new(c->rt, mfn));
                        }
                        sdef->method_count = new_mcount;
                    }
                    if (stmt->as.impl_stmt.trait_name) {
                        sdef->impl_traits = (const char**)realloc((void*)sdef->impl_traits, (sdef->impl_trait_count + 1) * sizeof(const char*));
                        sdef->impl_traits[sdef->impl_trait_count++] = stmt->as.impl_stmt.trait_name;
                    }
                }
            }
            break;
        }

        case UF_STMT_ENUM: {
            size_t vcount = stmt->as.enum_stmt.variant_count;
            const char** vnames = (const char**)malloc(vcount * sizeof(const char*));
            size_t* vfcounts = (size_t*)malloc(vcount * sizeof(size_t));
            const char*** vfnames = (const char***)malloc(vcount * sizeof(const char**));
            const char*** vftypes = (const char***)malloc(vcount * sizeof(const char**));
            for (size_t i = 0; i < vcount; ++i) {
                vnames[i] = stmt->as.enum_stmt.variants[i].name;
                vfcounts[i] = stmt->as.enum_stmt.variants[i].field_count;
                vfnames[i] = stmt->as.enum_stmt.variants[i].field_names;
                vftypes[i] = stmt->as.enum_stmt.variants[i].field_types;
            }
            UfValue edef = uf_val_enum_def(c->rt, stmt->as.enum_stmt.name, vcount, vnames, vfcounts, vfnames, vftypes);
            uf_env_declare(c->rt->global_env, stmt->as.enum_stmt.name, edef);
            uint16_t e_const = (uint16_t)make_constant(c, edef);
            if (c->scope_depth > 0) {
                int slot = add_local(c, stmt->as.enum_stmt.name, line);
                emit_abx(c, ROP_LOAD_K, c->locals[slot].reg, e_const, line);
                mark_initialized(c);
            } else {
                uint8_t r_e = alloc_reg(c);
                emit_abx(c, ROP_LOAD_K, r_e, e_const, line);
                uint16_t g_idx = identifier_constant(c, stmt->as.enum_stmt.name);
                emit_abx(c, ROP_DEF_GLOBAL, r_e, g_idx, line);
                free_reg(c, r_e);
            }

            for (size_t v = 0; v < vcount; ++v) {
                const char* vname = vnames[v];
                UfValue eval = uf_val_enum_val(c->rt, edef.as.enum_def, (int)v, vname, NULL, 0);
                uf_env_declare(c->rt->global_env, vname, eval);
                uint16_t ev_const = (uint16_t)make_constant(c, eval);
                if (c->scope_depth > 0) {
                    int vslot = add_local(c, vname, line);
                    emit_abx(c, ROP_LOAD_K, c->locals[vslot].reg, ev_const, line);
                    mark_initialized(c);
                } else {
                    uint8_t r_v = alloc_reg(c);
                    emit_abx(c, ROP_LOAD_K, r_v, ev_const, line);
                    uint16_t vg_idx = identifier_constant(c, vname);
                    emit_abx(c, ROP_DEF_GLOBAL, r_v, vg_idx, line);
                    free_reg(c, r_v);
                }
            }
            break;
        }

        case UF_STMT_MATCH: {
            begin_scope(c);
            int target_local = add_local(c, "_match_target", line);
            uint8_t target_reg = c->locals[target_local].reg;
            compile_expr(c, stmt->as.match_stmt.expr, target_reg);
            mark_initialized(c);

            int* end_jumps = (int*)malloc(sizeof(int) * (stmt->as.match_stmt.arm_count + 1));
            size_t end_jump_count = 0;

            for (size_t i = 0; i < stmt->as.match_stmt.arm_count; ++i) {
                UfMatchArm* arm = &stmt->as.match_stmt.arms[i];
                int fail_jumps[256];
                size_t fail_jump_count = 0;

                begin_scope(c);

                if (arm->pattern->kind == UF_PAT_LITERAL) {
                    uint8_t r_pat = compile_expr(c, arm->pattern->as.literal, -1);
                    uint8_t r_eq = alloc_reg(c);
                    emit_abc(c, ROP_EQ, r_eq, target_reg, r_pat, line);
                    free_reg(c, r_pat);
                    fail_jumps[fail_jump_count++] = emit_jump(c, ROP_JMP_FALSE, r_eq, line);
                    free_reg(c, r_eq);
                } else if (arm->pattern->kind == UF_PAT_VARIABLE) {
                    UfValue existing = uf_val_null();
                    if (uf_env_lookup(c->rt->global_env, arm->pattern->as.var_name, &existing) &&
                        existing.kind == UF_VAL_ENUM_VAL && existing.as.enum_val && existing.as.enum_val->field_count == 0) {
                        uint16_t c_idx = (uint16_t)make_constant(c, existing);
                        uint8_t r_enum = alloc_reg(c);
                        emit_abx(c, ROP_LOAD_K, r_enum, c_idx, line);
                        uint8_t r_eq = alloc_reg(c);
                        emit_abc(c, ROP_EQ, r_eq, target_reg, r_enum, line);
                        free_reg(c, r_enum);
                        fail_jumps[fail_jump_count++] = emit_jump(c, ROP_JMP_FALSE, r_eq, line);
                        free_reg(c, r_eq);
                    } else {
                        int vslot = add_local(c, arm->pattern->as.var_name, line);
                        emit_abc(c, ROP_MOVE, c->locals[vslot].reg, target_reg, 0, line);
                        mark_initialized(c);
                    }
                } else if (arm->pattern->kind == UF_PAT_WILDCARD) {
                    /* Matches unconditionally */
                } else if (arm->pattern->kind == UF_PAT_STRUCT) {
                    uint16_t s_idx = identifier_constant(c, arm->pattern->as.struct_pat.struct_name);
                    uint8_t r_match = alloc_reg(c);
                    emit_abc(c, ROP_INSTANCE, r_match, target_reg, (uint8_t)s_idx, line);
                    fail_jumps[fail_jump_count++] = emit_jump(c, ROP_JMP_FALSE, r_match, line);
                    free_reg(c, r_match);

                    const char** field_names_lookup = NULL;
                    size_t field_names_count = 0;
                    UfValue val_lookup;
                    if (uf_env_lookup(c->rt->global_env, arm->pattern->as.struct_pat.struct_name, &val_lookup)) {
                        if (val_lookup.kind == UF_VAL_STRUCT_DEF) {
                            field_names_lookup = val_lookup.as.struct_def->field_names;
                            field_names_count = val_lookup.as.struct_def->field_count;
                        } else if (val_lookup.kind == UF_VAL_ENUM_VAL && val_lookup.as.enum_val && val_lookup.as.enum_val->def) {
                            UfEnumValObject* ev = val_lookup.as.enum_val;
                            if (ev->def->variant_field_names && (size_t)ev->tag < ev->def->variant_count) {
                                field_names_lookup = ev->def->variant_field_names[ev->tag];
                                field_names_count = ev->def->variant_field_counts[ev->tag];
                            }
                        }
                    }

                    for (size_t f = 0; f < arm->pattern->as.struct_pat.field_count; ++f) {
                        UfPattern* fp = arm->pattern->as.struct_pat.field_patterns[f];
                        const char* fname = (field_names_lookup && f < field_names_count) ? field_names_lookup[f] : "";

                        uint8_t r_fval = alloc_reg(c);
                        uint8_t r_fidx = alloc_reg(c);
                        if (fname && fname[0] != '\0') {
                            uint16_t k = (uint16_t)make_constant(c, uf_val_string_cstr(c->rt, fname));
                            emit_abx(c, ROP_LOAD_K, r_fidx, k, line);
                        } else {
                            uint16_t k = (uint16_t)make_constant(c, uf_val_number((double)f));
                            emit_abx(c, ROP_LOAD_K, r_fidx, k, line);
                        }
                        emit_abc(c, ROP_INDEX_GET, r_fval, target_reg, r_fidx, line);
                        free_reg(c, r_fidx);

                        if (fp->kind == UF_PAT_VARIABLE) {
                            int fslot = add_local(c, fp->as.var_name, line);
                            emit_abc(c, ROP_MOVE, c->locals[fslot].reg, r_fval, 0, line);
                            mark_initialized(c);
                            free_reg(c, r_fval);
                        } else if (fp->kind == UF_PAT_LITERAL) {
                            uint8_t r_lit = compile_expr(c, fp->as.literal, -1);
                            uint8_t r_feq = alloc_reg(c);
                            emit_abc(c, ROP_EQ, r_feq, r_fval, r_lit, line);
                            free_reg(c, r_lit);
                            free_reg(c, r_fval);
                            fail_jumps[fail_jump_count++] = emit_jump(c, ROP_JMP_FALSE, r_feq, line);
                            free_reg(c, r_feq);
                        } else {
                            free_reg(c, r_fval);
                        }
                    }
                } else if (arm->pattern->kind == UF_PAT_ARRAY) {
                    size_t normal_count = arm->pattern->as.array_pat.has_rest
                                              ? (arm->pattern->as.array_pat.count > 0 ? arm->pattern->as.array_pat.count - 1 : 0)
                                              : arm->pattern->as.array_pat.count;
                    for (size_t f = 0; f < normal_count; ++f) {
                        UfPattern* ep = arm->pattern->as.array_pat.elements[f];
                        if (ep->kind == UF_PAT_VARIABLE) {
                            int fslot = add_local(c, ep->as.var_name, line);
                            mark_initialized(c);
                            uint8_t r_fidx = alloc_reg(c);
                            uint16_t k = (uint16_t)make_constant(c, uf_val_number((double)f));
                            emit_abx(c, ROP_LOAD_K, r_fidx, k, line);
                            emit_abc(c, ROP_INDEX_GET, c->locals[fslot].reg, target_reg, r_fidx, line);
                            free_reg(c, r_fidx);
                        } else if (ep->kind == UF_PAT_LITERAL) {
                            uint8_t r_fval = alloc_reg(c);
                            uint8_t r_fidx = alloc_reg(c);
                            uint16_t k = (uint16_t)make_constant(c, uf_val_number((double)f));
                            emit_abx(c, ROP_LOAD_K, r_fidx, k, line);
                            emit_abc(c, ROP_INDEX_GET, r_fval, target_reg, r_fidx, line);
                            free_reg(c, r_fidx);

                            uint8_t r_lit = compile_expr(c, ep->as.literal, -1);
                            uint8_t r_feq = alloc_reg(c);
                            emit_abc(c, ROP_EQ, r_feq, r_fval, r_lit, line);
                            free_reg(c, r_lit);
                            free_reg(c, r_fval);
                            fail_jumps[fail_jump_count++] = emit_jump(c, ROP_JMP_FALSE, r_feq, line);
                            free_reg(c, r_feq);
                        }
                    }
                    if (arm->pattern->as.array_pat.has_rest) {
                        UfPattern* rp = arm->pattern->as.array_pat.elements[normal_count];
                        if (rp->kind == UF_PAT_REST) rp = rp->as.rest_pat.subpattern;
                        if (rp && rp->kind == UF_PAT_VARIABLE) {
                            int rslot = add_local(c, rp->as.var_name, line);
                            mark_initialized(c);
                            uint8_t r_start = alloc_reg(c);
                            uint16_t k = (uint16_t)make_constant(c, uf_val_number((double)normal_count));
                            emit_abx(c, ROP_LOAD_K, r_start, k, line);
                            emit_abc(c, ROP_ARRAY_SLICE, c->locals[rslot].reg, target_reg, r_start, line);
                            free_reg(c, r_start);
                        }
                    }
                } else if (arm->pattern->kind == UF_PAT_MAP) {
                    for (size_t f = 0; f < arm->pattern->as.map_pat.count; ++f) {
                        UfPattern* vp = arm->pattern->as.map_pat.values[f];
                        if (vp->kind == UF_PAT_VARIABLE) {
                            int vslot = add_local(c, vp->as.var_name, line);
                            mark_initialized(c);
                            uint8_t r_fidx = alloc_reg(c);
                            uint16_t k = (uint16_t)make_constant(c, uf_val_string_cstr(c->rt, arm->pattern->as.map_pat.keys[f]));
                            emit_abx(c, ROP_LOAD_K, r_fidx, k, line);
                            emit_abc(c, ROP_INDEX_GET, c->locals[vslot].reg, target_reg, r_fidx, line);
                            free_reg(c, r_fidx);
                        } else if (vp->kind == UF_PAT_LITERAL) {
                            uint8_t r_fval = alloc_reg(c);
                            uint8_t r_fidx = alloc_reg(c);
                            uint16_t k = (uint16_t)make_constant(c, uf_val_string_cstr(c->rt, arm->pattern->as.map_pat.keys[f]));
                            emit_abx(c, ROP_LOAD_K, r_fidx, k, line);
                            emit_abc(c, ROP_INDEX_GET, r_fval, target_reg, r_fidx, line);
                            free_reg(c, r_fidx);

                            uint8_t r_lit = compile_expr(c, vp->as.literal, -1);
                            uint8_t r_feq = alloc_reg(c);
                            emit_abc(c, ROP_EQ, r_feq, r_fval, r_lit, line);
                            free_reg(c, r_lit);
                            free_reg(c, r_fval);
                            fail_jumps[fail_jump_count++] = emit_jump(c, ROP_JMP_FALSE, r_feq, line);
                            free_reg(c, r_feq);
                        }
                    }
                }

                if (arm->guard) {
                    uint8_t r_guard = compile_expr(c, arm->guard, -1);
                    fail_jumps[fail_jump_count++] = emit_jump(c, ROP_JMP_FALSE, r_guard, line);
                    free_reg(c, r_guard);
                }

                compile_stmt(c, arm->body);
                end_scope(c, line);

                end_jumps[end_jump_count++] = (int)c->chunk->code_count;
                emit_sax(c, ROP_JMP, 0, line);

                for (size_t fj = 0; fj < fail_jump_count; ++fj) {
                    patch_jump_to_current(c, fail_jumps[fj]);
                }
            }

            if (stmt->as.match_stmt.else_branch) {
                compile_stmt(c, stmt->as.match_stmt.else_branch);
            }

            for (size_t ej = 0; ej < end_jump_count; ++ej) {
                patch_jump_ax_to_current(c, end_jumps[ej]);
            }
            free(end_jumps);
            end_scope(c, line);
            break;
        }

        case UF_STMT_IMPORT: {
            UfModuleObject* mod = uf_module_load(c->rt, stmt->as.import_stmt.module_name, stmt->span);
            if (!mod || c->rt->had_runtime_error) {
                c->had_error = true;
                return;
            }
            const char* bound = stmt->as.import_stmt.alias ? stmt->as.import_stmt.alias : stmt->as.import_stmt.module_name;
            uf_env_declare(c->rt->global_env, bound, uf_val_module(c->rt, mod));
            break;
        }

        case UF_STMT_FROM_IMPORT: {
            UfModuleObject* mod = uf_module_load(c->rt, stmt->as.from_import_stmt.module_name, stmt->span);
            if (!mod || c->rt->had_runtime_error) {
                c->had_error = true;
                return;
            }
            for (size_t i = 0; i < stmt->as.from_import_stmt.count; ++i) {
                const char* sym = stmt->as.from_import_stmt.symbols[i];
                UfValue sym_key = uf_val_string(c->rt, sym, strlen(sym));
                if (!uf_map_has(mod->exports.as.map, sym_key)) {
                    uf_runtime_raise(c->rt, "ImportError", stmt->span, "Cannot import name '%s' from module '%s'", sym, stmt->as.from_import_stmt.module_name);
                    c->had_error = true;
                    return;
                }
                UfValue val = uf_map_get(mod->exports.as.map, sym_key);
                const char* bound = (stmt->as.from_import_stmt.aliases && stmt->as.from_import_stmt.aliases[i])
                                     ? stmt->as.from_import_stmt.aliases[i]
                                     : sym;
                uf_env_declare(c->rt->global_env, bound, val);
            }
            break;
        }
    }
}

/* --- Public API --- */

UfRegFunction* uf_reg_compile(const UfProgram* program, UfRuntime* rt, UfDiagnosticReporter* reporter) {
    if (!program) return NULL;

    UfRegCompiler compiler;
    compiler_init(&compiler, NULL, REG_FN_SCRIPT, "<script>", 0, 0, false, rt, reporter);

    /* Pass 1: Hoisted definitions (functions, traits, structs, impls, enums, imports) */
    for (size_t i = 0; i < program->count; ++i) {
        UfStmtKind k = program->stmts[i]->kind;
        if (k == UF_STMT_FUNCTION || k == UF_STMT_TRAIT || k == UF_STMT_STRUCT || k == UF_STMT_IMPL || k == UF_STMT_ENUM || k == UF_STMT_IMPORT || k == UF_STMT_FROM_IMPORT) {
            compile_stmt(&compiler, program->stmts[i]);
            if (compiler.had_error) return NULL;
        }
    }

    /* Pass 2: Executable statements */
    for (size_t i = 0; i < program->count; ++i) {
        UfStmtKind k = program->stmts[i]->kind;
        if (k != UF_STMT_FUNCTION && k != UF_STMT_TRAIT && k != UF_STMT_STRUCT && k != UF_STMT_IMPL && k != UF_STMT_ENUM && k != UF_STMT_IMPORT && k != UF_STMT_FROM_IMPORT) {
            compile_stmt(&compiler, program->stmts[i]);
            if (compiler.had_error) return NULL;
        }
    }

    /* Top-level implicit return null */
    uint8_t r_null = alloc_reg(&compiler);
    emit_abc(&compiler, ROP_LOAD_NULL, r_null, 0, 0, 1);
    emit_abc(&compiler, ROP_RETURN, r_null, 0, 0, 1);

    compiler.function->max_regs = compiler.max_regs;
    compiler.function->upvalue_count = compiler.upvalue_count;
    if (compiler.upvalue_count > 0) {
        compiler.function->upvalues = (UfRegUpvalueDesc*)malloc(compiler.upvalue_count * sizeof(UfRegUpvalueDesc));
        for (int i = 0; i < compiler.upvalue_count; ++i) {
            compiler.function->upvalues[i].index = compiler.upvalues[i].index;
            compiler.function->upvalues[i].is_local = compiler.upvalues[i].is_local ? 1 : 0;
        }
    }

    return compiler.function;
}
