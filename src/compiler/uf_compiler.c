#include "uf_compiler.h"
#include "../runtime/uf_module.h"
#include "../runtime/uf_env.h"
#include <stdlib.h>
#include <string.h>

static void compiler_init(UfCompiler* compiler, UfCompiler* enclosing, FunctionType type,
                          const char* fn_name, size_t arity, UfRuntime* rt, UfDiagnosticReporter* reporter) {
    compiler->enclosing = enclosing;
    compiler->type = type;
    compiler->fn_name = fn_name;
    compiler->arity = arity;
    compiler->rt = rt;
    compiler->reporter = reporter;
    compiler->had_error = false;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->upvalue_count = 0;
    compiler->current_loop = NULL;

    compiler->function = uf_bytecode_fn_new(rt, fn_name, arity);
    compiler->chunk = &compiler->function->chunk;

    /* Reserve slot 0 for the function itself / call frame base */
    UfLocal* local = &compiler->locals[compiler->local_count++];
    local->depth = 0;
    local->is_captured = false;
    local->name = (type == TYPE_FUNCTION) ? (fn_name ? fn_name : "") : "";
}

static void emit_byte(UfCompiler* c, uint8_t byte, int line) {
    uf_chunk_write(c->chunk, byte, line);
}

static void emit_u16(UfCompiler* c, uint16_t val, int line) {
    emit_byte(c, (uint8_t)((val >> 8) & 0xFF), line);
    emit_byte(c, (uint8_t)(val & 0xFF), line);
}

static size_t make_constant(UfCompiler* c, UfValue val) {
    return uf_chunk_add_constant(c->chunk, val);
}

static void emit_constant(UfCompiler* c, UfValue val, int line) {
    size_t const_idx = make_constant(c, val);
    emit_byte(c, (uint8_t)OP_CONSTANT, line);
    emit_u16(c, (uint16_t)const_idx, line);
}

static int emit_jump(UfCompiler* c, uint8_t instruction, int line) {
    emit_byte(c, instruction, line);
    emit_byte(c, 0xFF, line);
    emit_byte(c, 0xFF, line);
    return (int)c->chunk->code_count - 2;
}

static void patch_jump(UfCompiler* c, int offset) {
    int jump = (int)c->chunk->code_count - offset - 2;
    if (jump < 0 || jump > 65535) {
        c->had_error = true;
        return;
    }
    c->chunk->code[offset] = (uint8_t)((jump >> 8) & 0xFF);
    c->chunk->code[offset + 1] = (uint8_t)(jump & 0xFF);
}

static void emit_loop(UfCompiler* c, int loop_start, int line) {
    emit_byte(c, (uint8_t)OP_LOOP, line);
    int offset = (int)c->chunk->code_count - loop_start + 2;
    if (offset > 65535) {
        c->had_error = true;
        return;
    }
    emit_u16(c, (uint16_t)offset, line);
}

static void begin_scope(UfCompiler* c) {
    c->scope_depth++;
}

static void end_scope(UfCompiler* c, int line) {
    c->scope_depth--;
    while (c->local_count > 0 && c->locals[c->local_count - 1].depth > c->scope_depth) {
        if (c->locals[c->local_count - 1].is_captured) {
            emit_byte(c, (uint8_t)OP_CLOSE_UPVALUE, line);
        } else {
            emit_byte(c, (uint8_t)OP_POP, line);
        }
        c->local_count--;
    }
}

static int add_local(UfCompiler* c, const char* name, int line) {
    if (c->local_count >= 256) {
        c->had_error = true;
        return -1;
    }
    UfLocal* local = &c->locals[c->local_count++];
    local->name = name;
    local->depth = -1; /* uninitialized */
    local->is_captured = false;
    (void)line;
    return c->local_count - 1;
}

static void mark_initialized(UfCompiler* c) {
    if (c->scope_depth == 0) return;
    c->locals[c->local_count - 1].depth = c->scope_depth;
}

static int resolve_local(UfCompiler* c, const char* name) {
    for (int i = c->local_count - 1; i >= 0; --i) {
        if (c->locals[i].name && strcmp(c->locals[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int add_upvalue(UfCompiler* c, uint8_t index, bool is_local) {
    for (int i = 0; i < c->upvalue_count; ++i) {
        if (c->upvalues[i].index == index && c->upvalues[i].is_local == is_local) {
            return i;
        }
    }
    if (c->upvalue_count >= 256) {
        c->had_error = true;
        return 0;
    }
    c->upvalues[c->upvalue_count].index = index;
    c->upvalues[c->upvalue_count].is_local = is_local;
    return c->upvalue_count++;
}

static int resolve_upvalue(UfCompiler* c, const char* name) {
    if (c->enclosing == NULL) return -1;

    int local = resolve_local(c->enclosing, name);
    if (local != -1) {
        c->enclosing->locals[local].is_captured = true;
        return add_upvalue(c, (uint8_t)local, true);
    }

    int upvalue = resolve_upvalue(c->enclosing, name);
    if (upvalue != -1) {
        return add_upvalue(c, (uint8_t)upvalue, false);
    }

    return -1;
}

static uint16_t identifier_constant(UfCompiler* c, const char* name) {
    UfValue val = uf_val_string_cstr(c->rt, name);
    return (uint16_t)make_constant(c, val);
}

/* Forward declarations */
static void compile_expr(UfCompiler* c, const UfExpr* expr);
static void compile_stmt(UfCompiler* c, const UfStmt* stmt);

static void compile_expr(UfCompiler* c, const UfExpr* expr) {
    if (!expr || c->had_error) return;
    int line = (int)expr->span.start.line;

    switch (expr->kind) {
        case UF_EXPR_LITERAL_NULL:
            emit_byte(c, (uint8_t)OP_NULL, line);
            break;
        case UF_EXPR_LITERAL_BOOL:
            emit_byte(c, (uint8_t)(expr->as.bool_val ? OP_TRUE : OP_FALSE), line);
            break;
        case UF_EXPR_LITERAL_NUMBER:
            emit_constant(c, uf_val_number(expr->as.number_val), line);
            break;
        case UF_EXPR_LITERAL_STRING:
            emit_constant(c, uf_val_string_cstr(c->rt, expr->as.string_val), line);
            break;
        case UF_EXPR_IDENTIFIER: {
            const char* name = expr->as.identifier_name;
            int arg = resolve_local(c, name);
            if (arg != -1) {
                emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                emit_u16(c, (uint16_t)arg, line);
            } else if ((arg = resolve_upvalue(c, name)) != -1) {
                emit_byte(c, (uint8_t)OP_GET_UPVALUE, line);
                emit_byte(c, (uint8_t)arg, line);
            } else {
                uint16_t g_idx = identifier_constant(c, name);
                emit_byte(c, (uint8_t)OP_LOAD_GLOBAL, line);
                emit_u16(c, g_idx, line);
            }
            break;
        }
        case UF_EXPR_UNARY: {
            compile_expr(c, expr->as.unary.operand);
            if (expr->as.unary.op == UF_TOK_MINUS) {
                emit_byte(c, (uint8_t)OP_NEG, line);
            } else if (expr->as.unary.op == UF_TOK_NOT) {
                emit_byte(c, (uint8_t)OP_NOT, line);
            }
            break;
        }
        case UF_EXPR_BINARY: {
            if (expr->as.binary.op == UF_TOK_AND) {
                compile_expr(c, expr->as.binary.left);
                int end_jump = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                emit_byte(c, (uint8_t)OP_POP, line);
                compile_expr(c, expr->as.binary.right);
                patch_jump(c, end_jump);
                break;
            }
            if (expr->as.binary.op == UF_TOK_OR) {
                compile_expr(c, expr->as.binary.left);
                int else_jump = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                int end_jump = emit_jump(c, (uint8_t)OP_JUMP, line);
                patch_jump(c, else_jump);
                emit_byte(c, (uint8_t)OP_POP, line);
                compile_expr(c, expr->as.binary.right);
                patch_jump(c, end_jump);
                break;
            }
            compile_expr(c, expr->as.binary.left);
            compile_expr(c, expr->as.binary.right);
            switch (expr->as.binary.op) {
                case UF_TOK_PLUS:    emit_byte(c, (uint8_t)OP_ADD, line); break;
                case UF_TOK_MINUS:   emit_byte(c, (uint8_t)OP_SUB, line); break;
                case UF_TOK_STAR:    emit_byte(c, (uint8_t)OP_MUL, line); break;
                case UF_TOK_SLASH:   emit_byte(c, (uint8_t)OP_DIV, line); break;
                case UF_TOK_PERCENT: emit_byte(c, (uint8_t)OP_MOD, line); break;
                case UF_TOK_EQEQ:    emit_byte(c, (uint8_t)OP_EQ, line); break;
                case UF_TOK_BANGEQ:  emit_byte(c, (uint8_t)OP_NEQ, line); break;
                case UF_TOK_LT:      emit_byte(c, (uint8_t)OP_LT, line); break;
                case UF_TOK_LTEQ:    emit_byte(c, (uint8_t)OP_LTE, line); break;
                case UF_TOK_GT:      emit_byte(c, (uint8_t)OP_GT, line); break;
                case UF_TOK_GTEQ:    emit_byte(c, (uint8_t)OP_GTE, line); break;
                default: break;
            }
            break;
        }
        case UF_EXPR_GROUPING:
            compile_expr(c, expr->as.grouping.inner);
            break;
        case UF_EXPR_CALL: {
            compile_expr(c, expr->as.call.callee);
            for (size_t i = 0; i < expr->as.call.argc; ++i) {
                compile_expr(c, expr->as.call.args[i]);
            }
            emit_byte(c, (uint8_t)OP_CALL, line);
            emit_byte(c, (uint8_t)expr->as.call.argc, line);
            break;
        }
        case UF_EXPR_ARRAY: {
            for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                compile_expr(c, expr->as.array_lit.elements[i]);
            }
            emit_byte(c, (uint8_t)OP_BUILD_ARRAY, line);
            emit_u16(c, (uint16_t)expr->as.array_lit.count, line);
            break;
        }
        case UF_EXPR_INDEX: {
            compile_expr(c, expr->as.index_expr.target);
            compile_expr(c, expr->as.index_expr.index);
            emit_byte(c, (uint8_t)OP_INDEX_GET, line);
            break;
        }
        case UF_EXPR_MAP: {
            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                compile_expr(c, expr->as.map_lit.keys[i]);
                compile_expr(c, expr->as.map_lit.values[i]);
            }
            emit_byte(c, (uint8_t)OP_BUILD_MAP, line);
            emit_u16(c, (uint16_t)expr->as.map_lit.count, line);
            break;
        }
        case UF_EXPR_FUNCTION: {
            UfCompiler fn_compiler;
            compiler_init(&fn_compiler, c, TYPE_FUNCTION, expr->as.fn_expr.name,
                          expr->as.fn_expr.param_count, c->rt, c->reporter);
            begin_scope(&fn_compiler);

            for (size_t i = 0; i < expr->as.fn_expr.param_count; ++i) {
                add_local(&fn_compiler, expr->as.fn_expr.params[i], line);
                mark_initialized(&fn_compiler);
            }

            compile_stmt(&fn_compiler, expr->as.fn_expr.body);

            /* Implicit return null if function does not end with return */
            emit_byte(&fn_compiler, (uint8_t)OP_NULL, line);
            emit_byte(&fn_compiler, (uint8_t)OP_RETURN, line);

            fn_compiler.function->upvalue_count = fn_compiler.upvalue_count;

            size_t fn_idx = make_constant(c, uf_val_bytecode_fn(c->rt, fn_compiler.function));
            emit_byte(c, (uint8_t)OP_CLOSURE, line);
            emit_u16(c, (uint16_t)fn_idx, line);

            for (int i = 0; i < fn_compiler.upvalue_count; ++i) {
                emit_byte(c, (uint8_t)(fn_compiler.upvalues[i].is_local ? 1 : 0), line);
                emit_byte(c, fn_compiler.upvalues[i].index, line);
            }
            break;
        }
    }
}

static void compile_stmt(UfCompiler* c, const UfStmt* stmt) {
    if (!stmt || c->had_error) return;
    int line = (int)stmt->span.start.line;

    switch (stmt->kind) {
        case UF_STMT_LET: {
            if (stmt->as.let_stmt.init) {
                compile_expr(c, stmt->as.let_stmt.init);
            } else {
                emit_byte(c, (uint8_t)OP_NULL, line);
            }

            if (c->scope_depth > 0) {
                add_local(c, stmt->as.let_stmt.name, line);
                mark_initialized(c);
            } else {
                uint16_t g_idx = identifier_constant(c, stmt->as.let_stmt.name);
                emit_byte(c, (uint8_t)OP_DEFINE_GLOBAL, line);
                emit_u16(c, g_idx, line);
            }
            break;
        }
        case UF_STMT_ASSIGN: {
            compile_expr(c, stmt->as.assign_stmt.value);
            const char* name = stmt->as.assign_stmt.name;
            int arg = resolve_local(c, name);
            if (arg != -1) {
                emit_byte(c, (uint8_t)OP_STORE_LOCAL, line);
                emit_u16(c, (uint16_t)arg, line);
            } else if ((arg = resolve_upvalue(c, name)) != -1) {
                emit_byte(c, (uint8_t)OP_SET_UPVALUE, line);
                emit_byte(c, (uint8_t)arg, line);
            } else {
                uint16_t g_idx = identifier_constant(c, name);
                emit_byte(c, (uint8_t)OP_STORE_GLOBAL, line);
                emit_u16(c, g_idx, line);
            }
            emit_byte(c, (uint8_t)OP_POP, line);
            break;
        }
        case UF_STMT_INDEX_ASSIGN: {
            compile_expr(c, stmt->as.index_assign.target);
            compile_expr(c, stmt->as.index_assign.index);
            compile_expr(c, stmt->as.index_assign.value);
            emit_byte(c, (uint8_t)OP_INDEX_SET, line);
            break;
        }
        case UF_STMT_SAY: {
            if (stmt->as.say_stmt.expr) {
                compile_expr(c, stmt->as.say_stmt.expr);
            } else {
                emit_byte(c, (uint8_t)OP_NULL, line);
            }
            emit_byte(c, (uint8_t)OP_SAY, line);
            break;
        }
        case UF_STMT_EXPR: {
            compile_expr(c, stmt->as.expr_stmt.expr);
            emit_byte(c, (uint8_t)OP_POP, line);
            break;
        }
        case UF_STMT_IF: {
            compile_expr(c, stmt->as.if_stmt.condition);
            int then_jump = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
            emit_byte(c, (uint8_t)OP_POP, line);

            compile_stmt(c, stmt->as.if_stmt.then_branch);

            int else_jump = emit_jump(c, (uint8_t)OP_JUMP, line);
            patch_jump(c, then_jump);
            emit_byte(c, (uint8_t)OP_POP, line);

            if (stmt->as.if_stmt.else_branch) {
                compile_stmt(c, stmt->as.if_stmt.else_branch);
            }
            patch_jump(c, else_jump);
            break;
        }
        case UF_STMT_WHILE: {
            UfLoop loop;
            loop.start_ip = (int)c->chunk->code_count;
            loop.scope_depth = c->scope_depth;
            loop.break_jumps = NULL;
            loop.break_count = 0;
            loop.break_capacity = 0;
            loop.enclosing = c->current_loop;
            c->current_loop = &loop;

            compile_expr(c, stmt->as.while_stmt.condition);
            int exit_jump = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
            emit_byte(c, (uint8_t)OP_POP, line);

            compile_stmt(c, stmt->as.while_stmt.body);

            emit_loop(c, loop.start_ip, line);
            patch_jump(c, exit_jump);
            emit_byte(c, (uint8_t)OP_POP, line);

            for (size_t i = 0; i < loop.break_count; ++i) {
                patch_jump(c, loop.break_jumps[i]);
            }
            if (loop.break_jumps) free(loop.break_jumps);
            c->current_loop = loop.enclosing;
            break;
        }
        case UF_STMT_REPEAT: {
            begin_scope(c);
            /* Hidden loop counter = 0 */
            emit_constant(c, uf_val_number(0.0), line);
            int counter_slot = add_local(c, "_rep_counter", line);
            mark_initialized(c);

            /* Hidden count limit */
            compile_expr(c, stmt->as.repeat_stmt.count_expr);
            int limit_slot = add_local(c, "_rep_limit", line);
            mark_initialized(c);

            UfLoop loop;
            loop.start_ip = (int)c->chunk->code_count;
            loop.scope_depth = c->scope_depth;
            loop.break_jumps = NULL;
            loop.break_count = 0;
            loop.break_capacity = 0;
            loop.enclosing = c->current_loop;
            c->current_loop = &loop;

            /* Check counter < limit */
            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
            emit_u16(c, (uint16_t)counter_slot, line);
            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
            emit_u16(c, (uint16_t)limit_slot, line);
            emit_byte(c, (uint8_t)OP_LT, line);

            int exit_jump = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
            emit_byte(c, (uint8_t)OP_POP, line);

            compile_stmt(c, stmt->as.repeat_stmt.body);

            /* Increment counter */
            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
            emit_u16(c, (uint16_t)counter_slot, line);
            emit_constant(c, uf_val_number(1.0), line);
            emit_byte(c, (uint8_t)OP_ADD, line);
            emit_byte(c, (uint8_t)OP_STORE_LOCAL, line);
            emit_u16(c, (uint16_t)counter_slot, line);
            emit_byte(c, (uint8_t)OP_POP, line);

            emit_loop(c, loop.start_ip, line);
            patch_jump(c, exit_jump);
            emit_byte(c, (uint8_t)OP_POP, line);

            for (size_t i = 0; i < loop.break_count; ++i) {
                patch_jump(c, loop.break_jumps[i]);
            }
            if (loop.break_jumps) free(loop.break_jumps);
            c->current_loop = loop.enclosing;

            end_scope(c, line);
            break;
        }
        case UF_STMT_FOR: {
            begin_scope(c);
            /* Hidden iterable */
            compile_expr(c, stmt->as.for_stmt.iterable);
            int iter_slot = add_local(c, "_for_iter", line);
            mark_initialized(c);

            /* Hidden index = 0 */
            emit_constant(c, uf_val_number(0.0), line);
            int idx_slot = add_local(c, "_for_idx", line);
            mark_initialized(c);

            UfLoop loop;
            loop.start_ip = (int)c->chunk->code_count;
            loop.scope_depth = c->scope_depth;
            loop.break_jumps = NULL;
            loop.break_count = 0;
            loop.break_capacity = 0;
            loop.enclosing = c->current_loop;
            c->current_loop = &loop;

            /* Check idx < len(iter) */
            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
            emit_u16(c, (uint16_t)idx_slot, line);

            uint16_t len_sym = identifier_constant(c, "len");
            emit_byte(c, (uint8_t)OP_LOAD_GLOBAL, line);
            emit_u16(c, len_sym, line);
            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
            emit_u16(c, (uint16_t)iter_slot, line);
            emit_byte(c, (uint8_t)OP_CALL, line);
            emit_byte(c, 1, line);

            emit_byte(c, (uint8_t)OP_LT, line);

            int exit_jump = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
            emit_byte(c, (uint8_t)OP_POP, line);

            /* User loop variable = iter[idx] */
            begin_scope(c);
            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
            emit_u16(c, (uint16_t)iter_slot, line);
            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
            emit_u16(c, (uint16_t)idx_slot, line);
            emit_byte(c, (uint8_t)OP_ITER_GET, line);
            add_local(c, stmt->as.for_stmt.var_name, line);
            mark_initialized(c);

            compile_stmt(c, stmt->as.for_stmt.body);
            end_scope(c, line);

            /* Increment idx */
            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
            emit_u16(c, (uint16_t)idx_slot, line);
            emit_constant(c, uf_val_number(1.0), line);
            emit_byte(c, (uint8_t)OP_ADD, line);
            emit_byte(c, (uint8_t)OP_STORE_LOCAL, line);
            emit_u16(c, (uint16_t)idx_slot, line);
            emit_byte(c, (uint8_t)OP_POP, line);

            emit_loop(c, loop.start_ip, line);
            patch_jump(c, exit_jump);
            emit_byte(c, (uint8_t)OP_POP, line);

            for (size_t i = 0; i < loop.break_count; ++i) {
                patch_jump(c, loop.break_jumps[i]);
            }
            if (loop.break_jumps) free(loop.break_jumps);
            c->current_loop = loop.enclosing;

            end_scope(c, line);
            break;
        }
        case UF_STMT_BREAK: {
            if (!c->current_loop) {
                c->had_error = true;
                return;
            }
            /* Pop any locals inside current loop */
            for (int i = c->local_count - 1; i >= 0; --i) {
                if (c->locals[i].depth > c->current_loop->scope_depth) {
                    emit_byte(c, (uint8_t)OP_POP, line);
                }
            }
            int jump = emit_jump(c, (uint8_t)OP_JUMP, line);
            if (c->current_loop->break_count >= c->current_loop->break_capacity) {
                size_t ncap = c->current_loop->break_capacity < 4 ? 4 : c->current_loop->break_capacity * 2;
                c->current_loop->break_jumps = (int*)realloc(c->current_loop->break_jumps, sizeof(int) * ncap);
                c->current_loop->break_capacity = ncap;
            }
            c->current_loop->break_jumps[c->current_loop->break_count++] = jump;
            break;
        }
        case UF_STMT_CONTINUE: {
            if (!c->current_loop) {
                c->had_error = true;
                return;
            }
            for (int i = c->local_count - 1; i >= 0; --i) {
                if (c->locals[i].depth > c->current_loop->scope_depth) {
                    emit_byte(c, (uint8_t)OP_POP, line);
                }
            }
            emit_loop(c, c->current_loop->start_ip, line);
            break;
        }
        case UF_STMT_FUNCTION: {
            UfCompiler fn_compiler;
            compiler_init(&fn_compiler, c, TYPE_FUNCTION, stmt->as.function_stmt.name,
                          stmt->as.function_stmt.param_count, c->rt, c->reporter);
            begin_scope(&fn_compiler);

            for (size_t i = 0; i < stmt->as.function_stmt.param_count; ++i) {
                add_local(&fn_compiler, stmt->as.function_stmt.params[i], line);
                mark_initialized(&fn_compiler);
            }

            compile_stmt(&fn_compiler, stmt->as.function_stmt.body);

            emit_byte(&fn_compiler, (uint8_t)OP_NULL, line);
            emit_byte(&fn_compiler, (uint8_t)OP_RETURN, line);

            fn_compiler.function->upvalue_count = fn_compiler.upvalue_count;

            size_t fn_idx = make_constant(c, uf_val_bytecode_fn(c->rt, fn_compiler.function));
            emit_byte(c, (uint8_t)OP_CLOSURE, line);
            emit_u16(c, (uint16_t)fn_idx, line);

            for (int i = 0; i < fn_compiler.upvalue_count; ++i) {
                emit_byte(c, (uint8_t)(fn_compiler.upvalues[i].is_local ? 1 : 0), line);
                emit_byte(c, fn_compiler.upvalues[i].index, line);
            }

            if (c->scope_depth > 0) {
                add_local(c, stmt->as.function_stmt.name, line);
                mark_initialized(c);
            } else {
                uint16_t g_idx = identifier_constant(c, stmt->as.function_stmt.name);
                emit_byte(c, (uint8_t)OP_DEFINE_GLOBAL, line);
                emit_u16(c, g_idx, line);
            }
            break;
        }
        case UF_STMT_RETURN: {
            if (stmt->as.return_stmt.value) {
                compile_expr(c, stmt->as.return_stmt.value);
            } else {
                emit_byte(c, (uint8_t)OP_NULL, line);
            }
            emit_byte(c, (uint8_t)OP_RETURN, line);
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
            int catch_jump = emit_jump(c, (uint8_t)OP_PUSH_TRY, line);

            compile_stmt(c, stmt->as.try_catch.try_block);

            emit_byte(c, (uint8_t)OP_POP_TRY, line);
            int end_jump = emit_jump(c, (uint8_t)OP_JUMP, line);

            patch_jump(c, catch_jump);

            begin_scope(c);
            if (stmt->as.try_catch.catch_var) {
                add_local(c, stmt->as.try_catch.catch_var, line);
                mark_initialized(c);
            } else {
                emit_byte(c, (uint8_t)OP_POP, line);
            }

            compile_stmt(c, stmt->as.try_catch.catch_block);
            end_scope(c, line);

            patch_jump(c, end_jump);
            break;
        }
        case UF_STMT_STRUCT: {
            UfValue sval = uf_val_struct_def(c->rt, stmt->as.struct_stmt.name,
                                             stmt->as.struct_stmt.field_names,
                                             stmt->as.struct_stmt.field_types,
                                             stmt->as.struct_stmt.field_count);
            uf_env_declare(c->rt->global_env, stmt->as.struct_stmt.name, sval);
            size_t c_idx = make_constant(c, sval);
            emit_byte(c, (uint8_t)OP_STRUCT_DEF, line);
            emit_u16(c, (uint16_t)c_idx, line);
            if (c->scope_depth == 0) {
                uint16_t g_idx = identifier_constant(c, stmt->as.struct_stmt.name);
                emit_byte(c, (uint8_t)OP_DEFINE_GLOBAL, line);
                emit_u16(c, g_idx, line);
            }
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
        case UF_STMT_MATCH: {
            begin_scope(c);
            compile_expr(c, stmt->as.match_stmt.expr);
            int match_val_slot = add_local(c, "_match_target", line);
            mark_initialized(c);

            int* end_jumps = (int*)malloc(sizeof(int) * (stmt->as.match_stmt.arm_count + 1));
            size_t end_jump_count = 0;

            for (size_t i = 0; i < stmt->as.match_stmt.arm_count; ++i) {
                UfMatchArm* arm = &stmt->as.match_stmt.arms[i];
                int fail_jumps[16];
                size_t fail_jump_count = 0;

                begin_scope(c);

                if (arm->pattern->kind == UF_PAT_LITERAL) {
                    emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                    emit_u16(c, (uint16_t)match_val_slot, line);
                    compile_expr(c, arm->pattern->as.literal);
                    emit_byte(c, (uint8_t)OP_EQ, line);
                    fail_jumps[fail_jump_count++] = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                    emit_byte(c, (uint8_t)OP_POP, line);
                } else if (arm->pattern->kind == UF_PAT_VARIABLE) {
                    emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                    emit_u16(c, (uint16_t)match_val_slot, line);
                    add_local(c, arm->pattern->as.var_name, line);
                    mark_initialized(c);
                } else if (arm->pattern->kind == UF_PAT_WILDCARD) {
                    /* Matches unconditionally */
                } else if (arm->pattern->kind == UF_PAT_STRUCT) {
                    uint16_t s_idx = identifier_constant(c, arm->pattern->as.struct_pat.struct_name);
                    emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                    emit_u16(c, (uint16_t)match_val_slot, line);
                    emit_byte(c, (uint8_t)OP_INSTANCE, line);
                    emit_u16(c, s_idx, line);
                    fail_jumps[fail_jump_count++] = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                    emit_byte(c, (uint8_t)OP_POP, line);

                    UfStructDefObject* sdef = NULL;
                    UfValue sdef_val;
                    if (uf_env_lookup(c->rt->global_env, arm->pattern->as.struct_pat.struct_name, &sdef_val) &&
                        sdef_val.kind == UF_VAL_STRUCT_DEF) {
                        sdef = sdef_val.as.struct_def;
                    }

                    for (size_t f = 0; f < arm->pattern->as.struct_pat.field_count; ++f) {
                        UfPattern* fp = arm->pattern->as.struct_pat.field_patterns[f];
                        const char* fname = (sdef && f < sdef->field_count) ? sdef->field_names[f] : "";
                        emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                        emit_u16(c, (uint16_t)match_val_slot, line);
                        emit_constant(c, uf_val_string_cstr(c->rt, fname), line);
                        emit_byte(c, (uint8_t)OP_INDEX_GET, line);

                        if (fp->kind == UF_PAT_VARIABLE) {
                            add_local(c, fp->as.var_name, line);
                            mark_initialized(c);
                        } else if (fp->kind == UF_PAT_LITERAL) {
                            compile_expr(c, fp->as.literal);
                            emit_byte(c, (uint8_t)OP_EQ, line);
                            fail_jumps[fail_jump_count++] = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                            emit_byte(c, (uint8_t)OP_POP, line);
                        }
                    }
                }

                if (arm->guard) {
                    compile_expr(c, arm->guard);
                    fail_jumps[fail_jump_count++] = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                    emit_byte(c, (uint8_t)OP_POP, line);
                }

                compile_stmt(c, arm->body);
                end_scope(c, line);

                end_jumps[end_jump_count++] = emit_jump(c, (uint8_t)OP_JUMP, line);

                for (size_t fj = 0; fj < fail_jump_count; ++fj) {
                    patch_jump(c, fail_jumps[fj]);
                }
                if (fail_jump_count > 0) {
                    emit_byte(c, (uint8_t)OP_POP, line);
                }
            }

            if (stmt->as.match_stmt.else_branch) {
                compile_stmt(c, stmt->as.match_stmt.else_branch);
            }

            for (size_t j = 0; j < end_jump_count; ++j) {
                patch_jump(c, end_jumps[j]);
            }
            free(end_jumps);

            end_scope(c, line);
            break;
        }
        default:
            break;
    }
}

UfBytecodeFunction* uf_compile(const UfProgram* program, UfRuntime* rt, UfDiagnosticReporter* reporter) {
    if (!program) return NULL;

    UfCompiler compiler;
    compiler_init(&compiler, NULL, TYPE_SCRIPT, "<script>", 0, rt, reporter);

    /* Pass 1: Hoisted definitions (functions, structs, imports) */
    for (size_t i = 0; i < program->count; ++i) {
        UfStmtKind k = program->stmts[i]->kind;
        if (k == UF_STMT_FUNCTION || k == UF_STMT_STRUCT || k == UF_STMT_IMPORT || k == UF_STMT_FROM_IMPORT) {
            compile_stmt(&compiler, program->stmts[i]);
            if (compiler.had_error) break;
        }
    }

    /* Pass 2: Executable statements */
    for (size_t i = 0; i < program->count; ++i) {
        UfStmtKind k = program->stmts[i]->kind;
        if (k != UF_STMT_FUNCTION && k != UF_STMT_STRUCT && k != UF_STMT_IMPORT && k != UF_STMT_FROM_IMPORT) {
            compile_stmt(&compiler, program->stmts[i]);
            if (compiler.had_error) break;
        }
    }

    emit_byte(&compiler, (uint8_t)OP_NULL, 0);
    emit_byte(&compiler, (uint8_t)OP_RETURN, 0);

    if (compiler.had_error) {
        return NULL;
    }

    return compiler.function;
}
