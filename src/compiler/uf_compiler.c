#include "uf_compiler.h"
#include "uf_optimize.h"
#include "../runtime/uf_module.h"
#include "../runtime/uf_env.h"
#include "../vm/uf_vm.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Report a compile-time failure (an internal limit, or a construct the
 * compiler cannot lower) as a real diagnostic instead of silently emitting
 * broken bytecode. Only the first error per compiler is reported; later ones
 * are almost always cascades of the first. */
static void compile_error(UfCompiler* c, int line, const char* fmt, ...) {
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

static void compiler_init(UfCompiler* compiler, UfCompiler* enclosing, FunctionType type,
                          const char* fn_name, size_t arity, size_t min_arity, bool has_rest, UfRuntime* rt, UfDiagnosticReporter* reporter) {
    compiler->enclosing = enclosing;
    compiler->type = type;
    compiler->fn_name = fn_name;
    compiler->arity = arity;
    compiler->min_arity = min_arity;
    compiler->has_rest = has_rest;
    compiler->rt = rt;
    compiler->reporter = reporter;
    compiler->had_error = false;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->try_depth = 0;
    compiler->upvalue_count = 0;
    compiler->current_loop = NULL;
    compiler->current_line = enclosing ? enclosing->current_line : 0;

    compiler->function = uf_bytecode_fn_new(rt, fn_name, arity, min_arity, has_rest);
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
        compile_error(c, c->current_line,
                      "Jump distance too large (limit is 65535 bytes of bytecode); "
                      "split this block into smaller functions");
        return;
    }
    c->chunk->code[offset] = (uint8_t)((jump >> 8) & 0xFF);
    c->chunk->code[offset + 1] = (uint8_t)(jump & 0xFF);
}

static void emit_loop(UfCompiler* c, int loop_start, int line) {
    emit_byte(c, (uint8_t)OP_LOOP, line);
    int offset = (int)c->chunk->code_count - loop_start + 2;
    if (offset > 65535) {
        compile_error(c, line,
                      "Loop body too large (limit is 65535 bytes of bytecode); "
                      "split it into smaller functions");
        return;
    }
    emit_u16(c, (uint16_t)offset, line);
}

/* Patches every `continue` inside this loop to land here: the point where
 * an iteration's per-iteration "advance and re-check" step begins (for
 * `while`, that's the condition re-check itself; for `for`/`repeat`, it's
 * the hidden index/counter increment that runs after the body, which
 * `continue` must not skip — skipping it left `for`+`continue` an infinite
 * loop and `repeat`+`continue` off-by-one). Call this at that point in
 * each loop kind's own compilation, then free the accumulated array. */
static void patch_loop_continues(UfCompiler* c, UfLoop* loop) {
    for (size_t i = 0; i < loop->continue_count; ++i) {
        patch_jump(c, loop->continue_jumps[i]);
    }
    free(loop->continue_jumps);
    loop->continue_jumps = NULL;
    loop->continue_count = 0;
    loop->continue_capacity = 0;
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
        compile_error(c, line,
                      "Too many local variables in function '%s' (limit is 255); "
                      "split it into smaller functions",
                      c->fn_name ? c->fn_name : "<script>");
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
        compile_error(c, c->current_line,
                      "Too many captured variables in function '%s' (limit is 256)",
                      c->fn_name ? c->fn_name : "<script>");
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
    c->current_line = line;

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
            bool has_spread = false;
            for (size_t i = 0; i < expr->as.call.argc; ++i) {
                if (expr->as.call.args[i]->kind == UF_EXPR_SPREAD) {
                    has_spread = true;
                    break;
                }
            }

            compile_expr(c, expr->as.call.callee);

            if (!has_spread) {
                for (size_t i = 0; i < expr->as.call.argc; ++i) {
                    compile_expr(c, expr->as.call.args[i]);
                }
                emit_byte(c, (uint8_t)OP_CALL, line);
                emit_byte(c, (uint8_t)expr->as.call.argc, line);
            } else {
                emit_byte(c, (uint8_t)OP_BUILD_ARRAY, line);
                emit_u16(c, 0, line);
                for (size_t i = 0; i < expr->as.call.argc; ++i) {
                    if (expr->as.call.args[i]->kind == UF_EXPR_SPREAD) {
                        compile_expr(c, expr->as.call.args[i]->as.spread.operand);
                        emit_byte(c, (uint8_t)OP_ARRAY_EXTEND, line);
                    } else {
                        compile_expr(c, expr->as.call.args[i]);
                        emit_byte(c, (uint8_t)OP_ARRAY_PUSH, line);
                    }
                }
                emit_byte(c, (uint8_t)OP_CALL_SPREAD, line);
            }
            break;
        }
        case UF_EXPR_ARRAY: {
            bool has_spread = false;
            for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                if (expr->as.array_lit.elements[i]->kind == UF_EXPR_SPREAD) {
                    has_spread = true;
                    break;
                }
            }
            if (!has_spread) {
                for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                    compile_expr(c, expr->as.array_lit.elements[i]);
                }
                emit_byte(c, (uint8_t)OP_BUILD_ARRAY, line);
                emit_u16(c, (uint16_t)expr->as.array_lit.count, line);
            } else {
                emit_byte(c, (uint8_t)OP_BUILD_ARRAY, line);
                emit_u16(c, 0, line);
                for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                    if (expr->as.array_lit.elements[i]->kind == UF_EXPR_SPREAD) {
                        compile_expr(c, expr->as.array_lit.elements[i]->as.spread.operand);
                        emit_byte(c, (uint8_t)OP_ARRAY_EXTEND, line);
                    } else {
                        compile_expr(c, expr->as.array_lit.elements[i]);
                        emit_byte(c, (uint8_t)OP_ARRAY_PUSH, line);
                    }
                }
            }
            break;
        }
        case UF_EXPR_INDEX: {
            compile_expr(c, expr->as.index_expr.target);
            compile_expr(c, expr->as.index_expr.index);
            emit_byte(c, (uint8_t)OP_INDEX_GET, line);
            break;
        }
        case UF_EXPR_MAP: {
            bool has_spread = false;
            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                if (expr->as.map_lit.values[i] == NULL || expr->as.map_lit.keys[i]->kind == UF_EXPR_SPREAD) {
                    has_spread = true;
                    break;
                }
            }
            if (!has_spread) {
                for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                    compile_expr(c, expr->as.map_lit.keys[i]);
                    compile_expr(c, expr->as.map_lit.values[i]);
                }
                emit_byte(c, (uint8_t)OP_BUILD_MAP, line);
                emit_u16(c, (uint16_t)expr->as.map_lit.count, line);
            } else {
                emit_byte(c, (uint8_t)OP_BUILD_MAP, line);
                emit_u16(c, 0, line);
                for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                    if (expr->as.map_lit.values[i] == NULL) {
                        compile_expr(c, expr->as.map_lit.keys[i]->as.spread.operand);
                        emit_byte(c, (uint8_t)OP_MAP_EXTEND, line);
                    } else {
                        compile_expr(c, expr->as.map_lit.keys[i]);
                        compile_expr(c, expr->as.map_lit.values[i]);
                        emit_byte(c, (uint8_t)OP_MAP_SET, line);
                    }
                }
            }
            break;
        }
        case UF_EXPR_SPREAD: {
            compile_expr(c, expr->as.spread.operand);
            break;
        }
        case UF_EXPR_AWAIT: {
            compile_expr(c, expr->as.await_expr.value);
            emit_byte(c, (uint8_t)OP_AWAIT, line);
            break;
        }
        case UF_EXPR_FUNCTION: {
            UfCompiler fn_compiler;
            compiler_init(&fn_compiler, c, TYPE_FUNCTION, expr->as.fn_expr.name,
                          expr->as.fn_expr.param_count, expr->as.fn_expr.min_param_count, expr->as.fn_expr.has_rest, c->rt, c->reporter);
            fn_compiler.function->is_async = expr->as.fn_expr.is_async;
            begin_scope(&fn_compiler);

            for (size_t i = 0; i < expr->as.fn_expr.param_count; ++i) {
                add_local(&fn_compiler, expr->as.fn_expr.params[i], line);
                mark_initialized(&fn_compiler);
            }

            for (size_t i = expr->as.fn_expr.min_param_count; i < expr->as.fn_expr.param_count; ++i) {
                if (expr->as.fn_expr.param_defaults && expr->as.fn_expr.param_defaults[i]) {
                    emit_byte(&fn_compiler, (uint8_t)OP_JUMP_IF_ARG, line);
                    emit_byte(&fn_compiler, (uint8_t)i, line);
                    emit_byte(&fn_compiler, 0xFF, line);
                    emit_byte(&fn_compiler, 0xFF, line);
                    int jump = (int)fn_compiler.chunk->code_count - 2;

                    compile_expr(&fn_compiler, expr->as.fn_expr.param_defaults[i]);
                    emit_byte(&fn_compiler, (uint8_t)OP_STORE_LOCAL, line);
                    emit_u16(&fn_compiler, (uint16_t)(i + 1), line);
                    emit_byte(&fn_compiler, (uint8_t)OP_POP, line);

                    patch_jump(&fn_compiler, jump);
                }
            }

            compile_stmt(&fn_compiler, expr->as.fn_expr.body);

            /* Implicit return null if function does not end with return */
            emit_byte(&fn_compiler, (uint8_t)OP_NULL, line);
            emit_byte(&fn_compiler, (uint8_t)OP_RETURN, line);

            fn_compiler.function->upvalue_count = fn_compiler.upvalue_count;

            /* A failure inside the nested function invalidates the whole
             * compilation: without this the enclosing compiler would happily
             * emit a closure over half-built bytecode and the VM would run it. */
            if (fn_compiler.had_error) {
                c->had_error = true;
                return;
            }

            size_t fn_idx = make_constant(c, uf_val_bytecode_fn(c->rt, fn_compiler.function));
            emit_byte(c, (uint8_t)OP_CLOSURE, line);
            emit_u16(c, (uint16_t)fn_idx, line);

            for (int i = 0; i < fn_compiler.upvalue_count; ++i) {
                emit_byte(c, (uint8_t)(fn_compiler.upvalues[i].is_local ? 1 : 0), line);
                emit_byte(c, fn_compiler.upvalues[i].index, line);
            }
            break;
        }
        case UF_EXPR_STRING_INTERP: {
            emit_constant(c, uf_val_string(c->rt, "", 0), line);
            for (size_t i = 0; i < expr->as.string_interp.count; ++i) {
                compile_expr(c, expr->as.string_interp.parts[i]);
                emit_byte(c, (uint8_t)OP_ADD, line);
            }
            break;
        }
    }
}

static UfBytecodeFunction* compile_method_helper(UfCompiler* c, UfStmt* method, int line) {
    UfCompiler fn_compiler;
    compiler_init(&fn_compiler, c, TYPE_FUNCTION, method->as.function_stmt.name,
                  method->as.function_stmt.param_count, method->as.function_stmt.min_param_count,
                  method->as.function_stmt.has_rest, c->rt, c->reporter);
    fn_compiler.function->is_async = method->as.function_stmt.is_async;
    begin_scope(&fn_compiler);

    for (size_t i = 0; i < method->as.function_stmt.param_count; ++i) {
        add_local(&fn_compiler, method->as.function_stmt.params[i], line);
        mark_initialized(&fn_compiler);
    }

    for (size_t i = method->as.function_stmt.min_param_count; i < method->as.function_stmt.param_count; ++i) {
        if (method->as.function_stmt.param_defaults && method->as.function_stmt.param_defaults[i]) {
            emit_byte(&fn_compiler, (uint8_t)OP_JUMP_IF_ARG, line);
            emit_byte(&fn_compiler, (uint8_t)i, line);
            emit_byte(&fn_compiler, 0xFF, line);
            emit_byte(&fn_compiler, 0xFF, line);
            int jump = (int)fn_compiler.chunk->code_count - 2;

            compile_expr(&fn_compiler, method->as.function_stmt.param_defaults[i]);
            emit_byte(&fn_compiler, (uint8_t)OP_STORE_LOCAL, line);
            emit_u16(&fn_compiler, (uint16_t)(i + 1), line);
            emit_byte(&fn_compiler, (uint8_t)OP_POP, line);

            patch_jump(&fn_compiler, jump);
        }
    }

    compile_stmt(&fn_compiler, method->as.function_stmt.body);

    emit_byte(&fn_compiler, (uint8_t)OP_NULL, line);
    emit_byte(&fn_compiler, (uint8_t)OP_RETURN, line);

    fn_compiler.function->upvalue_count = fn_compiler.upvalue_count;
    /* Surface a failure in the method body to the enclosing compiler so the
     * whole compile aborts. The (incomplete) function is still returned:
     * callers wire it into a struct definition unconditionally, and nothing
     * ever runs it because uf_compile() now bails out on had_error. */
    if (fn_compiler.had_error) {
        c->had_error = true;
    }
    return fn_compiler.function;
}

static void compile_destructure_pattern(UfCompiler* c, const UfPattern* pat, int target_slot, int line, bool is_declaration) {
    if (!pat) return;
    switch (pat->kind) {
        case UF_PAT_WILDCARD:
        case UF_PAT_LITERAL:
            break;
        case UF_PAT_VARIABLE: {
            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
            emit_u16(c, (uint16_t)target_slot, line);
            if (is_declaration) {
                if (c->scope_depth > 0) {
                    add_local(c, pat->as.var_name, line);
                    mark_initialized(c);
                } else {
                    uint16_t g_idx = identifier_constant(c, pat->as.var_name);
                    emit_byte(c, (uint8_t)OP_DEFINE_GLOBAL, line);
                    emit_u16(c, g_idx, line);
                }
            } else {
                int arg = resolve_local(c, pat->as.var_name);
                if (arg != -1) {
                    emit_byte(c, (uint8_t)OP_STORE_LOCAL, line);
                    emit_u16(c, (uint16_t)arg, line);
                } else if ((arg = resolve_upvalue(c, pat->as.var_name)) != -1) {
                    emit_byte(c, (uint8_t)OP_SET_UPVALUE, line);
                    emit_byte(c, (uint8_t)arg, line);
                } else {
                    uint16_t g_idx = identifier_constant(c, pat->as.var_name);
                    emit_byte(c, (uint8_t)OP_STORE_GLOBAL, line);
                    emit_u16(c, g_idx, line);
                }
                emit_byte(c, (uint8_t)OP_POP, line);
            }
            break;
        }
        case UF_PAT_REST:
            if (pat->as.rest_pat.subpattern) {
                compile_destructure_pattern(c, pat->as.rest_pat.subpattern, target_slot, line, is_declaration);
            }
            break;
        case UF_PAT_ARRAY: {
            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
            emit_u16(c, (uint16_t)target_slot, line);
            emit_byte(c, (uint8_t)OP_ASSERT_ARRAY, line);

            size_t normal_count = pat->as.array_pat.has_rest ? (pat->as.array_pat.count > 0 ? pat->as.array_pat.count - 1 : 0) : pat->as.array_pat.count;
            for (size_t i = 0; i < normal_count; ++i) {
                UfPattern* elem = pat->as.array_pat.elements[i];
                if (elem->kind == UF_PAT_WILDCARD) continue;

                emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                emit_u16(c, (uint16_t)target_slot, line);
                emit_byte(c, (uint8_t)OP_ARRAY_GET_SAFE, line);
                emit_u16(c, (uint16_t)i, line);

                if (elem->kind == UF_PAT_VARIABLE) {
                    if (is_declaration) {
                        if (c->scope_depth > 0) {
                            add_local(c, elem->as.var_name, line);
                            mark_initialized(c);
                        } else {
                            uint16_t g_idx = identifier_constant(c, elem->as.var_name);
                            emit_byte(c, (uint8_t)OP_DEFINE_GLOBAL, line);
                            emit_u16(c, g_idx, line);
                        }
                    } else {
                        int arg = resolve_local(c, elem->as.var_name);
                        if (arg != -1) {
                            emit_byte(c, (uint8_t)OP_STORE_LOCAL, line);
                            emit_u16(c, (uint16_t)arg, line);
                        } else if ((arg = resolve_upvalue(c, elem->as.var_name)) != -1) {
                            emit_byte(c, (uint8_t)OP_SET_UPVALUE, line);
                            emit_byte(c, (uint8_t)arg, line);
                        } else {
                            uint16_t g_idx = identifier_constant(c, elem->as.var_name);
                            emit_byte(c, (uint8_t)OP_STORE_GLOBAL, line);
                            emit_u16(c, g_idx, line);
                        }
                        emit_byte(c, (uint8_t)OP_POP, line);
                    }
                } else {
                    int sub_slot = add_local(c, "", line);
                    mark_initialized(c);
                    compile_destructure_pattern(c, elem, sub_slot, line, is_declaration);
                }
            }
            if (pat->as.array_pat.has_rest) {
                UfPattern* rest_elem = pat->as.array_pat.elements[normal_count];
                if (rest_elem->kind == UF_PAT_REST) rest_elem = rest_elem->as.rest_pat.subpattern;
                if (rest_elem && rest_elem->kind != UF_PAT_WILDCARD) {
                    emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                    emit_u16(c, (uint16_t)target_slot, line);
                    emit_byte(c, (uint8_t)OP_ARRAY_SLICE, line);
                    emit_u16(c, (uint16_t)normal_count, line);

                    if (rest_elem->kind == UF_PAT_VARIABLE) {
                        if (is_declaration) {
                            if (c->scope_depth > 0) {
                                add_local(c, rest_elem->as.var_name, line);
                                mark_initialized(c);
                            } else {
                                uint16_t g_idx = identifier_constant(c, rest_elem->as.var_name);
                                emit_byte(c, (uint8_t)OP_DEFINE_GLOBAL, line);
                                emit_u16(c, g_idx, line);
                            }
                        } else {
                            int arg = resolve_local(c, rest_elem->as.var_name);
                            if (arg != -1) {
                                emit_byte(c, (uint8_t)OP_STORE_LOCAL, line);
                                emit_u16(c, (uint16_t)arg, line);
                            } else if ((arg = resolve_upvalue(c, rest_elem->as.var_name)) != -1) {
                                emit_byte(c, (uint8_t)OP_SET_UPVALUE, line);
                                emit_byte(c, (uint8_t)arg, line);
                            } else {
                                uint16_t g_idx = identifier_constant(c, rest_elem->as.var_name);
                                emit_byte(c, (uint8_t)OP_STORE_GLOBAL, line);
                                emit_u16(c, g_idx, line);
                            }
                            emit_byte(c, (uint8_t)OP_POP, line);
                        }
                    } else {
                        int sub_slot = add_local(c, "", line);
                        mark_initialized(c);
                        compile_destructure_pattern(c, rest_elem, sub_slot, line, is_declaration);
                    }
                }
            }
            break;
        }
        case UF_PAT_MAP: {
            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
            emit_u16(c, (uint16_t)target_slot, line);
            emit_byte(c, (uint8_t)OP_ASSERT_MAP, line);

            for (size_t i = 0; i < pat->as.map_pat.count; ++i) {
                UfPattern* val_pat = pat->as.map_pat.values[i];
                if (val_pat->kind == UF_PAT_WILDCARD) continue;

                emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                emit_u16(c, (uint16_t)target_slot, line);
                emit_constant(c, uf_val_string_cstr(c->rt, pat->as.map_pat.keys[i]), line);
                emit_byte(c, (uint8_t)OP_MAP_GET_SAFE, line);

                if (val_pat->kind == UF_PAT_VARIABLE) {
                    if (is_declaration) {
                        if (c->scope_depth > 0) {
                            add_local(c, val_pat->as.var_name, line);
                            mark_initialized(c);
                        } else {
                            uint16_t g_idx = identifier_constant(c, val_pat->as.var_name);
                            emit_byte(c, (uint8_t)OP_DEFINE_GLOBAL, line);
                            emit_u16(c, g_idx, line);
                        }
                    } else {
                        int arg = resolve_local(c, val_pat->as.var_name);
                        if (arg != -1) {
                            emit_byte(c, (uint8_t)OP_STORE_LOCAL, line);
                            emit_u16(c, (uint16_t)arg, line);
                        } else if ((arg = resolve_upvalue(c, val_pat->as.var_name)) != -1) {
                            emit_byte(c, (uint8_t)OP_SET_UPVALUE, line);
                            emit_byte(c, (uint8_t)arg, line);
                        } else {
                            uint16_t g_idx = identifier_constant(c, val_pat->as.var_name);
                            emit_byte(c, (uint8_t)OP_STORE_GLOBAL, line);
                            emit_u16(c, g_idx, line);
                        }
                        emit_byte(c, (uint8_t)OP_POP, line);
                    }
                } else {
                    int sub_slot = add_local(c, "", line);
                    mark_initialized(c);
                    compile_destructure_pattern(c, val_pat, sub_slot, line, is_declaration);
                }
            }
            if (pat->as.map_pat.has_rest && pat->as.map_pat.rest_pattern) {
                UfPattern* rest_pat = pat->as.map_pat.rest_pattern;
                if (rest_pat->kind != UF_PAT_WILDCARD) {
                    emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                    emit_u16(c, (uint16_t)target_slot, line);
                    emit_byte(c, (uint8_t)OP_MAP_REST, line);
                    emit_u16(c, (uint16_t)pat->as.map_pat.count, line);
                    for (size_t i = 0; i < pat->as.map_pat.count; ++i) {
                        uint16_t k_idx = (uint16_t)uf_chunk_add_constant(c->chunk, uf_val_string_cstr(c->rt, pat->as.map_pat.keys[i]));
                        emit_u16(c, k_idx, line);
                    }

                    if (rest_pat->kind == UF_PAT_VARIABLE) {
                        if (is_declaration) {
                            if (c->scope_depth > 0) {
                                add_local(c, rest_pat->as.var_name, line);
                                mark_initialized(c);
                            } else {
                                uint16_t g_idx = identifier_constant(c, rest_pat->as.var_name);
                                emit_byte(c, (uint8_t)OP_DEFINE_GLOBAL, line);
                                emit_u16(c, g_idx, line);
                            }
                        } else {
                            int arg = resolve_local(c, rest_pat->as.var_name);
                            if (arg != -1) {
                                emit_byte(c, (uint8_t)OP_STORE_LOCAL, line);
                                emit_u16(c, (uint16_t)arg, line);
                            } else if ((arg = resolve_upvalue(c, rest_pat->as.var_name)) != -1) {
                                emit_byte(c, (uint8_t)OP_SET_UPVALUE, line);
                                emit_byte(c, (uint8_t)arg, line);
                            } else {
                                uint16_t g_idx = identifier_constant(c, rest_pat->as.var_name);
                                emit_byte(c, (uint8_t)OP_STORE_GLOBAL, line);
                                emit_u16(c, g_idx, line);
                            }
                            emit_byte(c, (uint8_t)OP_POP, line);
                        }
                    } else {
                        int sub_slot = add_local(c, "", line);
                        mark_initialized(c);
                        compile_destructure_pattern(c, rest_pat, sub_slot, line, is_declaration);
                    }
                }
            }
            break;
        }
        case UF_PAT_STRUCT: {
            for (size_t i = 0; i < pat->as.struct_pat.field_count; ++i) {
                UfPattern* fp = pat->as.struct_pat.field_patterns[i];
                if (fp->kind == UF_PAT_WILDCARD) continue;

                emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                emit_u16(c, (uint16_t)target_slot, line);
                emit_constant(c, uf_val_number((double)i), line);
                emit_byte(c, (uint8_t)OP_INDEX_GET, line);

                if (fp->kind == UF_PAT_VARIABLE) {
                    if (is_declaration) {
                        if (c->scope_depth > 0) {
                            add_local(c, fp->as.var_name, line);
                            mark_initialized(c);
                        } else {
                            uint16_t g_idx = identifier_constant(c, fp->as.var_name);
                            emit_byte(c, (uint8_t)OP_DEFINE_GLOBAL, line);
                            emit_u16(c, g_idx, line);
                        }
                    } else {
                        int arg = resolve_local(c, fp->as.var_name);
                        if (arg != -1) {
                            emit_byte(c, (uint8_t)OP_STORE_LOCAL, line);
                            emit_u16(c, (uint16_t)arg, line);
                        } else if ((arg = resolve_upvalue(c, fp->as.var_name)) != -1) {
                            emit_byte(c, (uint8_t)OP_SET_UPVALUE, line);
                            emit_byte(c, (uint8_t)arg, line);
                        } else {
                            uint16_t g_idx = identifier_constant(c, fp->as.var_name);
                            emit_byte(c, (uint8_t)OP_STORE_GLOBAL, line);
                            emit_u16(c, g_idx, line);
                        }
                        emit_byte(c, (uint8_t)OP_POP, line);
                    }
                } else {
                    int sub_slot = add_local(c, "", line);
                    mark_initialized(c);
                    compile_destructure_pattern(c, fp, sub_slot, line, is_declaration);
                }
            }
            break;
        }
    }
}

static void compile_stmt(UfCompiler* c, const UfStmt* stmt) {
    if (!stmt || c->had_error) return;
    int line = (int)stmt->span.start.line;
    c->current_line = line;

    switch (stmt->kind) {
        case UF_STMT_LET: {
            if (stmt->as.let_stmt.pattern) {
                if (stmt->as.let_stmt.init) {
                    compile_expr(c, stmt->as.let_stmt.init);
                } else {
                    emit_byte(c, (uint8_t)OP_NULL, line);
                }
                int target_slot = add_local(c, "", line);
                mark_initialized(c);
                compile_destructure_pattern(c, stmt->as.let_stmt.pattern, target_slot, line, true);
                if (c->scope_depth == 0) {
                    emit_byte(c, (uint8_t)OP_POP, line);
                    c->local_count--;
                }
                break;
            }

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
            if (stmt->as.assign_stmt.pattern) {
                compile_expr(c, stmt->as.assign_stmt.value);
                int target_slot = add_local(c, "", line);
                mark_initialized(c);
                compile_destructure_pattern(c, stmt->as.assign_stmt.pattern, target_slot, line, false);
                emit_byte(c, (uint8_t)OP_POP, line);
                c->local_count--;
                break;
            }

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
            loop.try_depth = c->try_depth;
            loop.break_jumps = NULL;
            loop.break_count = 0;
            loop.break_capacity = 0;
            loop.continue_jumps = NULL;
            loop.continue_count = 0;
            loop.continue_capacity = 0;
            loop.enclosing = c->current_loop;
            c->current_loop = &loop;

            compile_expr(c, stmt->as.while_stmt.condition);
            int exit_jump = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
            emit_byte(c, (uint8_t)OP_POP, line);

            compile_stmt(c, stmt->as.while_stmt.body);

            /* while has no hidden per-iteration advance step, so continue's
             * target is exactly the condition re-check, same as start_ip. */
            patch_loop_continues(c, &loop);
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
            loop.try_depth = c->try_depth;
            loop.break_jumps = NULL;
            loop.break_count = 0;
            loop.break_capacity = 0;
            loop.continue_jumps = NULL;
            loop.continue_count = 0;
            loop.continue_capacity = 0;
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

            /* `continue` must land here, before the hidden counter advances
             * below — jumping straight to start_ip (as a naive backward
             * jump would) skips this increment and makes the loop run one
             * extra iteration to compensate, silently miscounting. */
            patch_loop_continues(c, &loop);

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
            loop.try_depth = c->try_depth;
            loop.break_jumps = NULL;
            loop.break_count = 0;
            loop.break_capacity = 0;
            loop.continue_jumps = NULL;
            loop.continue_count = 0;
            loop.continue_capacity = 0;
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

            /* `continue` must land here, before the hidden index advances
             * below — jumping straight to start_ip (the condition
             * re-check, as a naive backward jump would) skips this
             * increment entirely, so the condition re-checks against the
             * exact same index forever: an infinite loop. */
            patch_loop_continues(c, &loop);

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
                compile_error(c, line, "'break' outside of a loop");
                return;
            }
            /* Pop any locals inside current loop */
            for (int i = c->local_count - 1; i >= 0; --i) {
                if (c->locals[i].depth > c->current_loop->scope_depth) {
                    if (c->locals[i].is_captured) {
                        emit_byte(c, (uint8_t)OP_CLOSE_UPVALUE, line);
                    } else {
                        emit_byte(c, (uint8_t)OP_POP, line);
                    }
                }
            }
            for (int t = c->current_loop->try_depth; t < c->try_depth; ++t) {
                emit_byte(c, (uint8_t)OP_POP_TRY, line);
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
                compile_error(c, line, "'continue' outside of a loop");
                return;
            }
            for (int i = c->local_count - 1; i >= 0; --i) {
                if (c->locals[i].depth > c->current_loop->scope_depth) {
                    if (c->locals[i].is_captured) {
                        emit_byte(c, (uint8_t)OP_CLOSE_UPVALUE, line);
                    } else {
                        emit_byte(c, (uint8_t)OP_POP, line);
                    }
                }
            }
            for (int t = c->current_loop->try_depth; t < c->try_depth; ++t) {
                emit_byte(c, (uint8_t)OP_POP_TRY, line);
            }
            int jump = emit_jump(c, (uint8_t)OP_JUMP, line);
            if (c->current_loop->continue_count >= c->current_loop->continue_capacity) {
                size_t ncap = c->current_loop->continue_capacity < 4 ? 4 : c->current_loop->continue_capacity * 2;
                c->current_loop->continue_jumps = (int*)realloc(c->current_loop->continue_jumps, sizeof(int) * ncap);
                c->current_loop->continue_capacity = ncap;
            }
            c->current_loop->continue_jumps[c->current_loop->continue_count++] = jump;
            break;
        }
        case UF_STMT_FUNCTION: {
            UfCompiler fn_compiler;
            compiler_init(&fn_compiler, c, TYPE_FUNCTION, stmt->as.function_stmt.name,
                          stmt->as.function_stmt.param_count, stmt->as.function_stmt.min_param_count,
                          stmt->as.function_stmt.has_rest, c->rt, c->reporter);
            fn_compiler.function->is_async = stmt->as.function_stmt.is_async;
            begin_scope(&fn_compiler);

            for (size_t i = 0; i < stmt->as.function_stmt.param_count; ++i) {
                add_local(&fn_compiler, stmt->as.function_stmt.params[i], line);
                mark_initialized(&fn_compiler);
            }

            for (size_t i = stmt->as.function_stmt.min_param_count; i < stmt->as.function_stmt.param_count; ++i) {
                if (stmt->as.function_stmt.param_defaults && stmt->as.function_stmt.param_defaults[i]) {
                    emit_byte(&fn_compiler, (uint8_t)OP_JUMP_IF_ARG, line);
                    emit_byte(&fn_compiler, (uint8_t)i, line);
                    emit_byte(&fn_compiler, 0xFF, line);
                    emit_byte(&fn_compiler, 0xFF, line);
                    int jump = (int)fn_compiler.chunk->code_count - 2;

                    compile_expr(&fn_compiler, stmt->as.function_stmt.param_defaults[i]);
                    emit_byte(&fn_compiler, (uint8_t)OP_STORE_LOCAL, line);
                    emit_u16(&fn_compiler, (uint16_t)(i + 1), line);
                    emit_byte(&fn_compiler, (uint8_t)OP_POP, line);

                    patch_jump(&fn_compiler, jump);
                }
            }

            compile_stmt(&fn_compiler, stmt->as.function_stmt.body);

            emit_byte(&fn_compiler, (uint8_t)OP_NULL, line);
            emit_byte(&fn_compiler, (uint8_t)OP_RETURN, line);

            fn_compiler.function->upvalue_count = fn_compiler.upvalue_count;

            /* A failure inside the nested function invalidates the whole
             * compilation: without this the enclosing compiler would happily
             * emit a closure over half-built bytecode and the VM would run it. */
            if (fn_compiler.had_error) {
                c->had_error = true;
                return;
            }

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
            for (int t = 0; t < c->try_depth; ++t) {
                emit_byte(c, (uint8_t)OP_POP_TRY, line);
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
            c->try_depth++;

            compile_stmt(c, stmt->as.try_catch.try_block);

            emit_byte(c, (uint8_t)OP_POP_TRY, line);
            c->try_depth--;
            int try_success_jump = emit_jump(c, (uint8_t)OP_JUMP, line);

            patch_jump(c, catch_jump);

            if (stmt->as.try_catch.catch_block) {
                begin_scope(c);
                if (stmt->as.try_catch.catch_var) {
                    add_local(c, stmt->as.try_catch.catch_var, line);
                    mark_initialized(c);
                } else {
                    emit_byte(c, (uint8_t)OP_POP, line);
                }

                compile_stmt(c, stmt->as.try_catch.catch_block);
                end_scope(c, line);

                patch_jump(c, try_success_jump);

                if (stmt->as.try_catch.finally_block) {
                    compile_stmt(c, stmt->as.try_catch.finally_block);
                }
            } else {
                begin_scope(c);
                add_local(c, "", line);
                mark_initialized(c);
                uint16_t err_slot = (uint16_t)(c->local_count - 1);

                if (stmt->as.try_catch.finally_block) {
                    compile_stmt(c, stmt->as.try_catch.finally_block);
                }

                emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                emit_u16(c, err_slot, line);
                emit_byte(c, (uint8_t)OP_RETHROW, line);
                end_scope(c, line);

                patch_jump(c, try_success_jump);

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
            size_t c_idx = make_constant(c, tdef);
            emit_byte(c, (uint8_t)OP_CONSTANT, line);
            emit_u16(c, (uint16_t)c_idx, line);
            if (c->scope_depth == 0) {
                uint16_t g_idx = identifier_constant(c, stmt->as.trait_stmt.name);
                emit_byte(c, (uint8_t)OP_DEFINE_GLOBAL, line);
                emit_u16(c, g_idx, line);
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
                for (size_t i = 0; i < direct_mcount; ++i) {
                    UfStmt* m = stmt->as.struct_stmt.methods[i];
                    mnames[idx] = m->as.function_stmt.name;
                    UfBytecodeFunction* bfn = compile_method_helper(c, m, line);
                    UfClosureObject* cl = uf_closure_new(c->rt, bfn);
                    mvals[idx] = uf_val_closure(c->rt, cl);
                    idx++;
                }
                for (size_t b = 0; b < stmt->as.struct_stmt.impl_block_count; ++b) {
                    UfStmt* ib = stmt->as.struct_stmt.impl_blocks[b];
                    for (size_t i = 0; i < ib->as.impl_stmt.method_count; ++i) {
                        UfStmt* m = ib->as.impl_stmt.methods[i];
                        mnames[idx] = m->as.function_stmt.name;
                        UfBytecodeFunction* bfn = compile_method_helper(c, m, line);
                        UfClosureObject* cl = uf_closure_new(c->rt, bfn);
                        mvals[idx] = uf_val_closure(c->rt, cl);
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
            UfValue sval = uf_val_struct_def_with_traits(c->rt, stmt->as.struct_stmt.name,
                                                         stmt->as.struct_stmt.field_names,
                                                         stmt->as.struct_stmt.field_types,
                                                         stmt->as.struct_stmt.field_count,
                                                         mnames,
                                                         mvals,
                                                         total_mcount,
                                                         traits,
                                                         trait_count);
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
                            UfStmt* m = stmt->as.impl_stmt.methods[i];
                            sdef->method_names[sdef->method_count + i] = m->as.function_stmt.name;
                            UfBytecodeFunction* bfn = compile_method_helper(c, m, line);
                            UfClosureObject* cl = uf_closure_new(c->rt, bfn);
                            sdef->method_values[sdef->method_count + i] = uf_val_closure(c->rt, cl);
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
            size_t c_idx = make_constant(c, edef);
            emit_byte(c, (uint8_t)OP_CONSTANT, line);
            emit_u16(c, (uint16_t)c_idx, line);
            if (c->scope_depth == 0) {
                uint16_t g_idx = identifier_constant(c, stmt->as.enum_stmt.name);
                emit_byte(c, (uint8_t)OP_DEFINE_GLOBAL, line);
                emit_u16(c, g_idx, line);
            }
            for (size_t i = 0; i < vcount; ++i) {
                UfValue val_tmpl = uf_val_enum_val(c->rt, edef.as.enum_def, (int)i, vnames[i], NULL, 0);
                uf_env_declare(c->rt->global_env, vnames[i], val_tmpl);
                size_t v_idx = make_constant(c, val_tmpl);
                emit_byte(c, (uint8_t)OP_CONSTANT, line);
                emit_u16(c, (uint16_t)v_idx, line);
                if (c->scope_depth == 0) {
                    uint16_t g_idx = identifier_constant(c, vnames[i]);
                    emit_byte(c, (uint8_t)OP_DEFINE_GLOBAL, line);
                    emit_u16(c, g_idx, line);
                }
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
                int fail_jumps[256];
                size_t fail_jump_count = 0;

                begin_scope(c);

                int arm_start_locals = c->local_count;

                if (arm->pattern->kind == UF_PAT_LITERAL) {
                    emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                    emit_u16(c, (uint16_t)match_val_slot, line);
                    compile_expr(c, arm->pattern->as.literal);
                    emit_byte(c, (uint8_t)OP_EQ, line);
                    if (fail_jump_count < 256) {
                        fail_jumps[fail_jump_count++] = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                    } else {
                        compile_error(c, line,
                                      "Match arm has too many pattern tests (limit is 256); "
                                      "simplify the pattern or split the match");
                    }
                    emit_byte(c, (uint8_t)OP_POP, line);
                } else if (arm->pattern->kind == UF_PAT_VARIABLE) {
                    UfValue existing = uf_val_null();
                    if (uf_env_lookup(c->rt->global_env, arm->pattern->as.var_name, &existing) &&
                        existing.kind == UF_VAL_ENUM_VAL && existing.as.enum_val && existing.as.enum_val->field_count == 0) {
                        emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                        emit_u16(c, (uint16_t)match_val_slot, line);
                        size_t c_idx = make_constant(c, existing);
                        emit_byte(c, (uint8_t)OP_CONSTANT, line);
                        emit_u16(c, (uint16_t)c_idx, line);
                        emit_byte(c, (uint8_t)OP_EQ, line);
                        if (fail_jump_count < 256) {
                            fail_jumps[fail_jump_count++] = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                        } else {
                            compile_error(c, line,
                                          "Match arm has too many pattern tests (limit is 256); "
                                          "simplify the pattern or split the match");
                        }
                        emit_byte(c, (uint8_t)OP_POP, line);
                    } else {
                        emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                        emit_u16(c, (uint16_t)match_val_slot, line);
                        add_local(c, arm->pattern->as.var_name, line);
                        mark_initialized(c);
                    }
                } else if (arm->pattern->kind == UF_PAT_WILDCARD) {
                    /* Matches unconditionally */
                } else if (arm->pattern->kind == UF_PAT_STRUCT) {
                    uint16_t s_idx = identifier_constant(c, arm->pattern->as.struct_pat.struct_name);
                    emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                    emit_u16(c, (uint16_t)match_val_slot, line);
                    emit_byte(c, (uint8_t)OP_INSTANCE, line);
                    emit_u16(c, s_idx, line);
                    if (fail_jump_count < 256) {
                        fail_jumps[fail_jump_count++] = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                    } else {
                        compile_error(c, line,
                                      "Match arm has too many pattern tests (limit is 256); "
                                      "simplify the pattern or split the match");
                    }
                    emit_byte(c, (uint8_t)OP_POP, line);

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

                    /* Pass 1: test literal fields before binding any variables */
                    for (size_t f = 0; f < arm->pattern->as.struct_pat.field_count; ++f) {
                        UfPattern* fp = arm->pattern->as.struct_pat.field_patterns[f];
                        if (fp->kind == UF_PAT_LITERAL) {
                            const char* fname = (field_names_lookup && f < field_names_count) ? field_names_lookup[f] : "";
                            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                            emit_u16(c, (uint16_t)match_val_slot, line);
                            if (fname && fname[0] != '\0') {
                                emit_constant(c, uf_val_string_cstr(c->rt, fname), line);
                            } else {
                                emit_constant(c, uf_val_number((double)f), line);
                            }
                            emit_byte(c, (uint8_t)OP_INDEX_GET, line);
                            compile_expr(c, fp->as.literal);
                            emit_byte(c, (uint8_t)OP_EQ, line);
                            if (fail_jump_count < 256) {
                                fail_jumps[fail_jump_count++] = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                            } else {
                                compile_error(c, line,
                                              "Match arm has too many pattern tests (limit is 256); "
                                              "simplify the pattern or split the match");
                            }
                            emit_byte(c, (uint8_t)OP_POP, line);
                        }
                    }

                    /* Pass 2: bind variable fields now that literals matched */
                    for (size_t f = 0; f < arm->pattern->as.struct_pat.field_count; ++f) {
                        UfPattern* fp = arm->pattern->as.struct_pat.field_patterns[f];
                        if (fp->kind == UF_PAT_VARIABLE) {
                            const char* fname = (field_names_lookup && f < field_names_count) ? field_names_lookup[f] : "";
                            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                            emit_u16(c, (uint16_t)match_val_slot, line);
                            if (fname && fname[0] != '\0') {
                                emit_constant(c, uf_val_string_cstr(c->rt, fname), line);
                            } else {
                                emit_constant(c, uf_val_number((double)f), line);
                            }
                            emit_byte(c, (uint8_t)OP_INDEX_GET, line);
                            add_local(c, fp->as.var_name, line);
                            mark_initialized(c);
                        }
                    }
                } else if (arm->pattern->kind == UF_PAT_ARRAY) {
                    size_t normal_count = arm->pattern->as.array_pat.has_rest ? (arm->pattern->as.array_pat.count > 0 ? arm->pattern->as.array_pat.count - 1 : 0) : arm->pattern->as.array_pat.count;
                    /* Pass 1: test literal elements */
                    for (size_t f = 0; f < normal_count; ++f) {
                        UfPattern* ep = arm->pattern->as.array_pat.elements[f];
                        if (ep->kind == UF_PAT_LITERAL) {
                            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                            emit_u16(c, (uint16_t)match_val_slot, line);
                            emit_constant(c, uf_val_number((double)f), line);
                            emit_byte(c, (uint8_t)OP_INDEX_GET, line);
                            compile_expr(c, ep->as.literal);
                            emit_byte(c, (uint8_t)OP_EQ, line);
                            if (fail_jump_count < 256) {
                                fail_jumps[fail_jump_count++] = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                            } else {
                                compile_error(c, line,
                                              "Match arm has too many pattern tests (limit is 256); "
                                              "simplify the pattern or split the match");
                            }
                            emit_byte(c, (uint8_t)OP_POP, line);
                        }
                    }
                    /* Pass 2: bind variable elements */
                    for (size_t f = 0; f < normal_count; ++f) {
                        UfPattern* ep = arm->pattern->as.array_pat.elements[f];
                        if (ep->kind == UF_PAT_VARIABLE) {
                            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                            emit_u16(c, (uint16_t)match_val_slot, line);
                            emit_constant(c, uf_val_number((double)f), line);
                            emit_byte(c, (uint8_t)OP_INDEX_GET, line);
                            add_local(c, ep->as.var_name, line);
                            mark_initialized(c);
                        }
                    }
                    if (arm->pattern->as.array_pat.has_rest) {
                        UfPattern* rp = arm->pattern->as.array_pat.elements[normal_count];
                        if (rp->kind == UF_PAT_REST) rp = rp->as.rest_pat.subpattern;
                        if (rp && rp->kind == UF_PAT_VARIABLE) {
                            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                            emit_u16(c, (uint16_t)match_val_slot, line);
                            emit_byte(c, (uint8_t)OP_ARRAY_SLICE, line);
                            emit_u16(c, (uint16_t)normal_count, line);
                            add_local(c, rp->as.var_name, line);
                            mark_initialized(c);
                        }
                    }
                } else if (arm->pattern->kind == UF_PAT_MAP) {
                    /* Pass 1: test literal values */
                    for (size_t f = 0; f < arm->pattern->as.map_pat.count; ++f) {
                        UfPattern* vp = arm->pattern->as.map_pat.values[f];
                        if (vp->kind == UF_PAT_LITERAL) {
                            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                            emit_u16(c, (uint16_t)match_val_slot, line);
                            emit_constant(c, uf_val_string_cstr(c->rt, arm->pattern->as.map_pat.keys[f]), line);
                            emit_byte(c, (uint8_t)OP_INDEX_GET, line);
                            compile_expr(c, vp->as.literal);
                            emit_byte(c, (uint8_t)OP_EQ, line);
                            if (fail_jump_count < 256) {
                                fail_jumps[fail_jump_count++] = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                            } else {
                                compile_error(c, line,
                                              "Match arm has too many pattern tests (limit is 256); "
                                              "simplify the pattern or split the match");
                            }
                            emit_byte(c, (uint8_t)OP_POP, line);
                        }
                    }
                    /* Pass 2: bind variable values */
                    for (size_t f = 0; f < arm->pattern->as.map_pat.count; ++f) {
                        UfPattern* vp = arm->pattern->as.map_pat.values[f];
                        if (vp->kind == UF_PAT_VARIABLE) {
                            emit_byte(c, (uint8_t)OP_LOAD_LOCAL, line);
                            emit_u16(c, (uint16_t)match_val_slot, line);
                            emit_constant(c, uf_val_string_cstr(c->rt, arm->pattern->as.map_pat.keys[f]), line);
                            emit_byte(c, (uint8_t)OP_INDEX_GET, line);
                            add_local(c, vp->as.var_name, line);
                            mark_initialized(c);
                        }
                    }
                }

                int guard_fail_jump = -1;
                if (arm->guard) {
                    compile_expr(c, arm->guard);
                    int arm_locals = c->local_count - arm_start_locals;
                    if (arm_locals > 0) {
                        int guard_fail = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                        emit_byte(c, (uint8_t)OP_POP, line); /* pop true when guard passes */
                        int guard_pass_jump = emit_jump(c, (uint8_t)OP_JUMP, line);

                        patch_jump(c, guard_fail);
                        emit_byte(c, (uint8_t)OP_POP, line); /* pop false */
                        for (int p = 0; p < arm_locals; ++p) {
                            emit_byte(c, (uint8_t)OP_POP, line);
                        }
                        guard_fail_jump = emit_jump(c, (uint8_t)OP_JUMP, line);

                        patch_jump(c, guard_pass_jump);
                    } else {
                        if (fail_jump_count < 256) {
                            fail_jumps[fail_jump_count++] = emit_jump(c, (uint8_t)OP_JUMP_IF_FALSE, line);
                        } else {
                            compile_error(c, line,
                                          "Match arm has too many pattern tests (limit is 256); "
                                          "simplify the pattern or split the match");
                        }
                        emit_byte(c, (uint8_t)OP_POP, line);
                    }
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
                if (guard_fail_jump >= 0) {
                    patch_jump(c, guard_fail_jump);
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

    /* Optimization Pass 1: Constant folding & Dead code elimination on AST */
    uf_optimize_ast((UfProgram*)program, rt);

    UfCompiler compiler;
    compiler_init(&compiler, NULL, TYPE_SCRIPT, "<script>", 0, 0, false, rt, reporter);

    /* Pass 1: Hoisted definitions (functions, traits, structs, impls, enums, imports) */
    for (size_t i = 0; i < program->count; ++i) {
        UfStmtKind k = program->stmts[i]->kind;
        if (k == UF_STMT_FUNCTION || k == UF_STMT_TRAIT || k == UF_STMT_STRUCT || k == UF_STMT_IMPL || k == UF_STMT_ENUM || k == UF_STMT_IMPORT || k == UF_STMT_FROM_IMPORT) {
            compile_stmt(&compiler, program->stmts[i]);
            if (compiler.had_error) break;
        }
    }

    /* Pass 2: Executable statements */
    for (size_t i = 0; i < program->count; ++i) {
        UfStmtKind k = program->stmts[i]->kind;
        if (k != UF_STMT_FUNCTION && k != UF_STMT_TRAIT && k != UF_STMT_STRUCT && k != UF_STMT_IMPL && k != UF_STMT_ENUM && k != UF_STMT_IMPORT && k != UF_STMT_FROM_IMPORT) {
            compile_stmt(&compiler, program->stmts[i]);
            if (compiler.had_error) break;
        }
    }

    emit_byte(&compiler, (uint8_t)OP_NULL, 0);
    emit_byte(&compiler, (uint8_t)OP_RETURN, 0);

    if (compiler.had_error) {
        return NULL;
    }

    /* Optimization Pass 2: Bytecode Peephole & Function Tree Optimization */
    uf_optimize_function_tree(compiler.function, rt);

    return compiler.function;
}
