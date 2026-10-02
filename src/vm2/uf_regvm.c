#include "uf_regvm.h"
#include "../runtime/uf_env.h"
#include "../runtime/uf_stdlib.h"
#include "../runtime/uf_module.h"
#include "../runtime/uf_fiber.h"
#include "../tooling/uf_profiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#if defined(__GNUC__) || defined(__clang__)
#define UF_USE_COMPUTED_GOTO 1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#define UF_USE_COMPUTED_GOTO 0
#endif

/* --- Chunk Management --- */

void uf_reg_chunk_init(UfRegChunk* chunk) {
    chunk->code = NULL;
    chunk->code_count = 0;
    chunk->code_capacity = 0;
    chunk->constants = NULL;
    chunk->const_count = 0;
    chunk->const_capacity = 0;
    chunk->lines = NULL;
}

void uf_reg_chunk_free(UfRegChunk* chunk) {
    if (chunk->code) free(chunk->code);
    if (chunk->constants) free(chunk->constants);
    if (chunk->lines) free(chunk->lines);
    uf_reg_chunk_init(chunk);
}

void uf_reg_chunk_write(UfRegChunk* chunk, uint32_t instr, int line) {
    if (chunk->code_count >= chunk->code_capacity) {
        size_t old_cap = chunk->code_capacity;
        chunk->code_capacity = (old_cap < 8) ? 8 : old_cap * 2;
        chunk->code = (uint32_t*)realloc(chunk->code, chunk->code_capacity * sizeof(uint32_t));
        chunk->lines = (int*)realloc(chunk->lines, chunk->lines ? chunk->code_capacity * sizeof(int) : chunk->code_capacity * sizeof(int));
    }
    chunk->code[chunk->code_count] = instr;
    chunk->lines[chunk->code_count] = line;
    chunk->code_count++;
}

size_t uf_reg_chunk_add_constant(UfRegChunk* chunk, UfValue value) {
    if (chunk->const_count >= chunk->const_capacity) {
        size_t old_cap = chunk->const_capacity;
        chunk->const_capacity = (old_cap < 8) ? 8 : old_cap * 2;
        chunk->constants = (UfValue*)realloc(chunk->constants, chunk->const_capacity * sizeof(UfValue));
    }
    chunk->constants[chunk->const_count] = value;
    return chunk->const_count++;
}

/* --- Object Constructors --- */

UfRegFunction* uf_reg_fn_new(UfRuntime* rt, const char* name, size_t arity, size_t min_arity, bool has_rest) {
    UfRegFunction* fn = (UfRegFunction*)malloc(sizeof(UfRegFunction));
    if (!fn) return NULL;
    fn->obj.kind = UF_OBJ_REG_FN;
    fn->obj.marked = false;
    fn->obj.next = NULL;
    fn->name = name ? strdup(name) : NULL;
    fn->arity = arity;
    fn->min_arity = min_arity;
    fn->has_rest = has_rest;
    fn->is_async = false;
    fn->max_regs = 0;
    uf_reg_chunk_init(&fn->chunk);
    fn->upvalues = NULL;
    fn->upvalue_count = 0;

    if (rt) {
        uf_runtime_register_obj(rt, (UfObj*)fn, sizeof(UfRegFunction));
    }
    return fn;
}

UfRegClosure* uf_reg_closure_new(UfRuntime* rt, UfRegFunction* fn) {
    UfRegClosure* cl = (UfRegClosure*)malloc(sizeof(UfRegClosure));
    if (!cl) return NULL;
    cl->obj.kind = UF_OBJ_REG_CLOSURE;
    cl->obj.marked = false;
    cl->obj.next = NULL;
    cl->function = fn;
    cl->upvalue_count = fn ? fn->upvalue_count : 0;
    cl->upvalues = NULL;
    if (cl->upvalue_count > 0) {
        cl->upvalues = (UfUpvalueCell**)calloc(cl->upvalue_count, sizeof(UfUpvalueCell*));
    }

    if (rt) {
        uf_runtime_register_obj(rt, (UfObj*)cl, sizeof(UfRegClosure) + cl->upvalue_count * sizeof(UfUpvalueCell*));
    }
    return cl;
}

/* --- Upvalue Management --- */

static UfUpvalueCell* capture_upvalue(UfRegVM* vm, UfValue* local) {
    UfUpvalueCell* prev = NULL;
    UfUpvalueCell* curr = vm->open_upvalues;
    while (curr && curr->location > local) {
        prev = curr;
        curr = curr->next;
    }
    if (curr && curr->location == local) {
        return curr;
    }

    UfUpvalueCell* cell = (UfUpvalueCell*)malloc(sizeof(UfUpvalueCell));
    if (!cell) return NULL;
    cell->obj.kind = UF_OBJ_UPVALUE;
    cell->obj.marked = false;
    cell->obj.next = NULL;
    cell->location = local;
    cell->closed = uf_val_null();
    cell->next = curr;

    if (prev == NULL) {
        vm->open_upvalues = cell;
    } else {
        prev->next = cell;
    }

    if (vm->rt) {
        uf_runtime_register_obj(vm->rt, (UfObj*)cell, sizeof(UfUpvalueCell));
    }
    return cell;
}

static void close_upvalues(UfRegVM* vm, UfValue* last) {
    while (vm->open_upvalues && vm->open_upvalues->location >= last) {
        UfUpvalueCell* up = vm->open_upvalues;
        up->closed = *up->location;
        up->location = &up->closed;
        vm->open_upvalues = up->next;
    }
}

/* --- Error Handling --- */

static void regvm_runtime_error(UfRegVM* vm, const char* fmt, ...) {
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    SourceSpan span = {0};
    span.start.file = "<vm>";
    span.start.line = 1;
    span.start.col = 1;
    span.end = span.start;

    if (vm->frame_count > 0) {
        UfRegFrame* frame = &vm->frames[vm->frame_count - 1];
        size_t offset = (size_t)(frame->ip - frame->closure->function->chunk.code);
        if (offset > 0 && offset <= frame->closure->function->chunk.code_count) {
            int line = frame->closure->function->chunk.lines[offset - 1];
            span.start.line = line;
            span.end.line = line;
            if (frame->closure->function->name) {
                span.start.file = frame->closure->function->name;
                span.end.file = frame->closure->function->name;
            }
        }
    }

    const char* kind = "RuntimeError";
    if (strncmp(msg, "Division by zero", 16) == 0) kind = "DivisionByZero";
    else if (strncmp(msg, "IndexOutOfBounds", 16) == 0 || strstr(msg, "out of bounds") != NULL) kind = "IndexOutOfBounds";
    else if (strncmp(msg, "StackOverflowError", 18) == 0) kind = "StackOverflowError";
    else if (strncmp(msg, "ExecutionQuotaExceeded", 22) == 0) kind = "ExecutionQuotaExceeded";
    else if (strncmp(msg, "AssertionError", 14) == 0 || strncmp(msg, "Assertion failed", 16) == 0) kind = "AssertionError";
    else if (strstr(msg, "domain error") != NULL) kind = "DomainError";
    else if (strstr(msg, "expects") != NULL || strstr(msg, "Cannot") != NULL || strstr(msg, "must be") != NULL) kind = "TypeError";

    if (vm->rt) {
        vm->rt->current_error = uf_val_error(vm->rt, msg, kind, span);
        vm->rt->had_runtime_error = true;
    }
    vm->had_error = true;

    /* Check if an internal try handler can catch this */
    if (vm->handler_count > 0) {
        UfRegVMHandler* h = &vm->handlers[--vm->handler_count];
        /* This handler is being consumed here rather than by a longjmp, so the
         * runtime's parallel handler stack has to be popped too. Leaving it
         * behind desynchronises the two stacks, and a later uf_runtime_raise()
         * would longjmp into this spent handler with the VM's handler stack
         * empty -- indexing vm->handlers[-1] and jumping to a garbage ip. */
        if (vm->rt && vm->rt->try_handler_count > 0) vm->rt->try_handler_count--;
        while (vm->frame_count > h->frame_index + 1) {
            UfRegFrame* top = &vm->frames[vm->frame_count - 1];
            close_upvalues(vm, top->regs);
            vm->frame_count--;
        }
        UfRegFrame* catch_frame = &vm->frames[h->frame_index];
        catch_frame->regs[h->error_reg] = vm->rt->current_error;
        catch_frame->ip = h->catch_ip;
        vm->had_error = false;
        if (vm->rt) vm->rt->had_runtime_error = false;
        return;
    }

    /* Print backtrace if unhandled and reporter is available */
    if (vm->rt && vm->rt->reporter) {
        fprintf(vm->rt->err_stream, "Runtime Error: %s\n", msg);
        for (int i = vm->frame_count - 1; i >= 0; --i) {
            UfRegFrame* frame = &vm->frames[i];
            size_t offset = (size_t)(frame->ip - frame->closure->function->chunk.code);
            int line = (offset > 0 && offset <= frame->closure->function->chunk.code_count)
                           ? frame->closure->function->chunk.lines[offset - 1]
                           : 1;
            const char* fn_name = frame->closure->function->name ? frame->closure->function->name : "<script>";
            fprintf(vm->rt->err_stream, "  at %s (line %d)\n", fn_name, line);
        }
    }

    /* If enclosed by an outer try/catch (longjmp), unwind to it */
    if (vm->rt && vm->rt->try_handler_count > 0) {
        UfTryHandler* th = &vm->rt->try_handlers[--vm->rt->try_handler_count];
        vm->rt->frame_count = th->frame_count;
        vm->rt->temp_root_count = th->temp_root_count;
        vm->rt->current_env = th->scope_env;
        longjmp(th->jmp, 1);
    }
}

/* --- Call Invocation --- */

static bool regvm_call_value(UfRegVM* vm, UfValue callee, size_t argc, uint8_t base, uint8_t dest_reg) {
    UfRegFrame* caller_frame = &vm->frames[vm->frame_count - 1];
    UfValue* args_base = caller_frame->regs + base + 1;

    if (callee.kind == UF_VAL_BOUND_METHOD) {
        UfBoundMethodObject* bm = callee.as.bound_method;
        /* Shift arguments right by 1 to insert receiver at slot 0 */
        for (int i = (int)argc; i > 0; --i) {
            args_base[i] = args_base[i - 1];
        }
        args_base[0] = bm->receiver;
        return regvm_call_value(vm, bm->method, argc + 1, base, dest_reg);
    }

    if (callee.kind == UF_VAL_REG_CLOSURE) {
        UfRegClosure* cl = callee.as.reg_closure;
        if (cl->function->has_rest) {
            if (argc < cl->function->min_arity) {
                regvm_runtime_error(vm, "Function '%s' expects at least %zu arguments, but %zu provided",
                                    cl->function->name ? cl->function->name : "anonymous",
                                    cl->function->min_arity, argc);
                return false;
            }
        } else {
            if (argc < cl->function->min_arity || argc > cl->function->arity) {
                if (cl->function->min_arity == cl->function->arity) {
                    regvm_runtime_error(vm, "Function '%s' expects %zu arguments, but %zu provided",
                                        cl->function->name ? cl->function->name : "anonymous",
                                        cl->function->arity, argc);
                } else {
                    regvm_runtime_error(vm, "Function '%s' expects %zu to %zu arguments, but %zu provided",
                                        cl->function->name ? cl->function->name : "anonymous",
                                        cl->function->min_arity, cl->function->arity, argc);
                }
                return false;
            }
        }

        if (vm->frame_count >= UF_REGVM_FRAMES_MAX) {
            regvm_runtime_error(vm, "StackOverflowError: Maximum call stack depth exceeded (%d frames)",
                                UF_REGVM_FRAMES_MAX_CALLS);
            return false;
        }
        /* The callee's register window starts at the caller's `base` register.
         * The register file is not sized for every frame using its maximum
         * window, so check it rather than writing past the end. */
        if (caller_frame->regs + base + cl->function->max_regs + 16 > vm->stack + UF_REGVM_STACK_MAX) {
            regvm_runtime_error(vm, "StackOverflowError: Register stack overflow (%d slots)", UF_REGVM_STACK_MAX);
            return false;
        }

        /* Pad optional/default arguments with null */
        if (!cl->function->has_rest) {
            for (size_t i = argc; i < cl->function->arity; ++i) {
                args_base[i] = uf_val_null();
            }
        } else {
            size_t fixed_count = cl->function->arity - 1;
            if (argc < fixed_count) {
                for (size_t i = argc; i < fixed_count; ++i) {
                    args_base[i] = uf_val_null();
                }
                args_base[fixed_count] = uf_val_array(vm->rt, 0);
            } else {
                size_t rest_count = argc - fixed_count;
                UfValue rest_arr = uf_val_array(vm->rt, rest_count);
                for (size_t i = 0; i < rest_count; ++i) {
                    uf_array_push(vm->rt, rest_arr.as.array, args_base[fixed_count + i]);
                }
                args_base[fixed_count] = rest_arr;
            }
        }

        UfRegFrame* new_frame = &vm->frames[vm->frame_count++];
        new_frame->closure = cl;
        new_frame->ip = cl->function->chunk.code;
        new_frame->regs = caller_frame->regs + base;
        new_frame->argc = argc;
        new_frame->dest_reg = dest_reg;

        if (vm->rt && vm->rt->profiler) {
            const char* name = cl->function->name ? cl->function->name : "<anonymous>";
            uf_profiler_enter(vm->rt->profiler, name);
        }

        /* Keep stack_top ahead of active registers */
        size_t needed_regs = cl->function->max_regs + 16;
        if (new_frame->regs + needed_regs > vm->stack_top) {
            vm->stack_top = new_frame->regs + needed_regs;
        }
        return true;
    }

    if (callee.kind == UF_VAL_NATIVE_FN) {
        UfNativeFn fn = callee.as.native_fn.fn;
        int arity = callee.as.native_fn.arity;
        if (arity != -1 && arity != (int)argc) {
            regvm_runtime_error(vm, "Native function '%s' expects %d arguments, but %zu provided",
                                callee.as.native_fn.name, arity, argc);
            return false;
        }
        if (vm->rt && vm->rt->profiler) {
            uf_profiler_enter(vm->rt->profiler, callee.as.native_fn.name);
        }
        UfValue res = fn(vm->rt, argc, args_base);
        if (vm->rt && vm->rt->profiler) {
            uf_profiler_exit(vm->rt->profiler, callee.as.native_fn.name);
        }
        /* A caught error never returns here (it longjmps to its handler), so a
         * raised flag means the error was uncaught and already reported. It must
         * stop the VM; carrying on with the null result ran the rest of the
         * program after the diagnostic was printed. */
        if (vm->rt && vm->rt->had_runtime_error) {
            vm->had_error = true;
            return false;
        }
        caller_frame->regs[dest_reg] = res;
        return true;
    }

    if (callee.kind == UF_VAL_STRUCT_DEF) {
        UfStructDefObject* sdef = callee.as.struct_def;
        if (argc != sdef->field_count) {
            regvm_runtime_error(vm, "Struct '%s' expects %zu fields, but %zu provided",
                                sdef->name, sdef->field_count, argc);
            return false;
        }
        UfValue inst = uf_val_instance(vm->rt, sdef, args_base, argc);
        caller_frame->regs[dest_reg] = inst;
        return true;
    }

    if (callee.kind == UF_VAL_ENUM_VAL) {
        UfEnumValObject* ev = callee.as.enum_val;
        if (ev->def && (size_t)ev->tag < ev->def->variant_count) {
            size_t expected = ev->def->variant_field_counts[ev->tag];
            if (argc != expected) {
                regvm_runtime_error(vm, "TypeError: Enum variant '%s' expects %zu argument%s, but %zu provided",
                                    ev->variant_name, expected, expected == 1 ? "" : "s", argc);
                return false;
            }
            UfValue eval = uf_val_enum_val(vm->rt, ev->def, ev->tag, ev->variant_name, args_base, argc);
            caller_frame->regs[dest_reg] = eval;
            return true;
        }
    }

    if (callee.kind == UF_VAL_FUNCTION) {
        SourceSpan span = (SourceSpan){0};
        UfValue res = uf_runtime_call(vm->rt, callee, argc, args_base, span);
        if (vm->rt && vm->rt->had_runtime_error) {
            vm->had_error = true;
            return false;
        }
        caller_frame->regs[dest_reg] = res;
        return true;
    }

    regvm_runtime_error(vm, "Can only call functions and structs, got '%s'", uf_val_type_name(callee));
    return false;
}

/* --- VM Lifecycle --- */

void uf_regvm_init(UfRegVM* vm, UfRuntime* rt) {
    vm->frame_count = 0;
    vm->stack_top = vm->stack;
    for (size_t i = 0; i < UF_REGVM_STACK_MAX; ++i) {
        vm->stack[i] = uf_val_null();
    }
    vm->handler_count = 0;
    vm->open_upvalues = NULL;
    vm->rt = rt;
    vm->had_error = false;
    vm->trace_execution = false;
    vm->total_instructions = 0;
    vm->peak_stack_depth = 0;
    vm->peak_frame_depth = 0;

    if (rt) {
        rt->active_regvm = vm;
        if (!rt->call_fn) {
            rt->call_fn = uf_call_value;
        }
    }
}

void uf_regvm_free(UfRegVM* vm) {
    close_upvalues(vm, vm->stack);
    if (vm->rt && vm->rt->active_regvm == vm) {
        vm->rt->active_regvm = NULL;
    }
}

/* --- Main VM Execution Loop --- */

static UfValue run_regvm_frames(UfRegVM* vm, int target_frame_count) {
    if (vm->frame_count <= target_frame_count) return uf_val_null();

    UfRegFrame* volatile frame = &vm->frames[vm->frame_count - 1];
    uint32_t* volatile ip = frame->ip;
    UfValue* volatile regs = frame->regs;
    UfRegChunk* volatile chunk = &frame->closure->function->chunk;

#if UF_USE_COMPUTED_GOTO
    static void* dispatch_table[ROP_COUNT] = {
        [ROP_LOAD_K]         = &&do_ROP_LOAD_K,
        [ROP_LOAD_NULL]      = &&do_ROP_LOAD_NULL,
        [ROP_LOAD_TRUE]      = &&do_ROP_LOAD_TRUE,
        [ROP_LOAD_FALSE]     = &&do_ROP_LOAD_FALSE,
        [ROP_MOVE]           = &&do_ROP_MOVE,
        [ROP_GET_GLOBAL]     = &&do_ROP_GET_GLOBAL,
        [ROP_SET_GLOBAL]     = &&do_ROP_SET_GLOBAL,
        [ROP_DEF_GLOBAL]     = &&do_ROP_DEF_GLOBAL,
        [ROP_GET_UPVAL]      = &&do_ROP_GET_UPVAL,
        [ROP_SET_UPVAL]      = &&do_ROP_SET_UPVAL,
        [ROP_CLOSE_UPVAL]    = &&do_ROP_CLOSE_UPVAL,
        [ROP_CLOSURE]        = &&do_ROP_CLOSURE,
        [ROP_ADD]            = &&do_ROP_ADD,
        [ROP_SUB]            = &&do_ROP_SUB,
        [ROP_MUL]            = &&do_ROP_MUL,
        [ROP_DIV]            = &&do_ROP_DIV,
        [ROP_MOD]            = &&do_ROP_MOD,
        [ROP_NEG]            = &&do_ROP_NEG,
        [ROP_NOT]            = &&do_ROP_NOT,
        [ROP_EQ]             = &&do_ROP_EQ,
        [ROP_NEQ]            = &&do_ROP_NEQ,
        [ROP_LT]             = &&do_ROP_LT,
        [ROP_LTE]            = &&do_ROP_LTE,
        [ROP_GT]             = &&do_ROP_GT,
        [ROP_GTE]            = &&do_ROP_GTE,
        [ROP_JMP]            = &&do_ROP_JMP,
        [ROP_JMP_FALSE]      = &&do_ROP_JMP_FALSE,
        [ROP_JMP_TRUE]       = &&do_ROP_JMP_TRUE,
        [ROP_JMP_ARG]        = &&do_ROP_JMP_ARG,
        [ROP_LOOP]           = &&do_ROP_LOOP,
        [ROP_CALL]           = &&do_ROP_CALL,
        [ROP_CALL_SPREAD]    = &&do_ROP_CALL_SPREAD,
        [ROP_RETURN]         = &&do_ROP_RETURN,
        [ROP_NEW_ARRAY]      = &&do_ROP_NEW_ARRAY,
        [ROP_NEW_MAP]        = &&do_ROP_NEW_MAP,
        [ROP_ARRAY_PUSH]     = &&do_ROP_ARRAY_PUSH,
        [ROP_ARRAY_EXTEND]   = &&do_ROP_ARRAY_EXTEND,
        [ROP_ARRAY_SLICE]    = &&do_ROP_ARRAY_SLICE,
        [ROP_MAP_SET]        = &&do_ROP_MAP_SET,
        [ROP_MAP_EXTEND]     = &&do_ROP_MAP_EXTEND,
        [ROP_INDEX_GET]      = &&do_ROP_INDEX_GET,
        [ROP_INDEX_SET]      = &&do_ROP_INDEX_SET,
        [ROP_ITER_GET]       = &&do_ROP_ITER_GET,
        [ROP_ASSERT_ARRAY]   = &&do_ROP_ASSERT_ARRAY,
        [ROP_ASSERT_MAP]     = &&do_ROP_ASSERT_MAP,
        [ROP_ARRAY_GET_SAFE] = &&do_ROP_ARRAY_GET_SAFE,
        [ROP_MAP_GET_SAFE]   = &&do_ROP_MAP_GET_SAFE,
        [ROP_MAP_REST]       = &&do_ROP_MAP_REST,
        [ROP_SAY]            = &&do_ROP_SAY,
        [ROP_STRUCT_DEF]     = &&do_ROP_STRUCT_DEF,
        [ROP_INSTANCE]       = &&do_ROP_INSTANCE,
        [ROP_PUSH_TRY]       = &&do_ROP_PUSH_TRY,
        [ROP_POP_TRY]        = &&do_ROP_POP_TRY,
        [ROP_RETHROW]        = &&do_ROP_RETHROW,
        [ROP_AWAIT]          = &&do_ROP_AWAIT
    };
#define DISPATCH() do { \
    instr = *ip++; \
    vm->total_instructions++; \
    goto *dispatch_table[REG_GET_OP(instr)]; \
} while(0)
#else
#define DISPATCH() break
#endif

#define SYNC_FRAME() do { \
    frame->ip = ip; \
} while(0)

#define RELOAD_FRAME() do { \
    frame = &vm->frames[vm->frame_count - 1]; \
    ip = frame->ip; \
    regs = frame->regs; \
    chunk = &frame->closure->function->chunk; \
} while(0)

#define REGVM_ERROR(...) do { \
    SYNC_FRAME(); \
    regvm_runtime_error(vm, __VA_ARGS__); \
    if (vm->had_error) return uf_val_null(); \
    RELOAD_FRAME(); \
    DISPATCH(); \
} while(0)

    uint32_t instr;

#if UF_USE_COMPUTED_GOTO
    DISPATCH();
#endif

    for (;;) {
#if !UF_USE_COMPUTED_GOTO
        instr = *ip++;
        vm->total_instructions++;
        switch (REG_GET_OP(instr)) {
#endif

        /* --- Constants & Literals --- */
#if UF_USE_COMPUTED_GOTO
        do_ROP_LOAD_K:
#else
        case ROP_LOAD_K:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint16_t bx = REG_GET_Bx(instr);
            regs[a] = chunk->constants[bx];
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_LOAD_NULL:
#else
        case ROP_LOAD_NULL:
#endif
        {
            regs[REG_GET_A(instr)] = uf_val_null();
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_LOAD_TRUE:
#else
        case ROP_LOAD_TRUE:
#endif
        {
            regs[REG_GET_A(instr)] = uf_val_bool(true);
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_LOAD_FALSE:
#else
        case ROP_LOAD_FALSE:
#endif
        {
            regs[REG_GET_A(instr)] = uf_val_bool(false);
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_MOVE:
#else
        case ROP_MOVE:
#endif
        {
            regs[REG_GET_A(instr)] = regs[REG_GET_B(instr)];
            DISPATCH();
        }

        /* --- Globals --- */
#if UF_USE_COMPUTED_GOTO
        do_ROP_GET_GLOBAL:
#else
        case ROP_GET_GLOBAL:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint16_t bx = REG_GET_Bx(instr);
            const char* name = chunk->constants[bx].as.string->chars;
            UfValue val;
            if (!uf_env_lookup(vm->rt->global_env, name, &val)) {
                REGVM_ERROR("Undefined variable '%s'", name);
            }
            regs[a] = val;
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_SET_GLOBAL:
#else
        case ROP_SET_GLOBAL:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint16_t bx = REG_GET_Bx(instr);
            const char* name = chunk->constants[bx].as.string->chars;
            if (!uf_env_assign(vm->rt->global_env, name, regs[a])) {
                REGVM_ERROR("Undefined variable '%s'", name);
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_DEF_GLOBAL:
#else
        case ROP_DEF_GLOBAL:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint16_t bx = REG_GET_Bx(instr);
            const char* name = chunk->constants[bx].as.string->chars;
            uf_env_declare(vm->rt->global_env, name, regs[a]);
            DISPATCH();
        }

        /* --- Upvalues & Closures --- */
#if UF_USE_COMPUTED_GOTO
        do_ROP_GET_UPVAL:
#else
        case ROP_GET_UPVAL:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            regs[a] = *frame->closure->upvalues[b]->location;
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_SET_UPVAL:
#else
        case ROP_SET_UPVAL:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            *frame->closure->upvalues[b]->location = regs[a];
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_CLOSE_UPVAL:
#else
        case ROP_CLOSE_UPVAL:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            close_upvalues(vm, &regs[a]);
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_CLOSURE:
#else
        case ROP_CLOSURE:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint16_t bx = REG_GET_Bx(instr);
            UfRegFunction* fn = chunk->constants[bx].as.reg_fn;
            SYNC_FRAME();
            UfRegClosure* cl = uf_reg_closure_new(vm->rt, fn);
            for (size_t i = 0; i < cl->upvalue_count; ++i) {
                if (fn->upvalues[i].is_local) {
                    cl->upvalues[i] = capture_upvalue(vm, &regs[fn->upvalues[i].index]);
                } else {
                    cl->upvalues[i] = frame->closure->upvalues[fn->upvalues[i].index];
                }
            }
            regs[a] = uf_val_reg_closure(vm->rt, cl);
            DISPATCH();
        }

        /* --- Arithmetic (3-address: A = B op C) --- */
#if UF_USE_COMPUTED_GOTO
        do_ROP_ADD:
#else
        case ROP_ADD:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            uint8_t c = REG_GET_C(instr);
            UfValue vb = regs[b];
            UfValue vc = regs[c];

            if (vb.kind == UF_VAL_NUMBER && vc.kind == UF_VAL_NUMBER) {
                regs[a] = uf_val_number(vb.as.number + vc.as.number);
            } else if (vb.kind == UF_VAL_STRING || vc.kind == UF_VAL_STRING) {
                char* sb = uf_val_to_string(vb);
                char* sc = uf_val_to_string(vc);
                size_t lb = strlen(sb);
                size_t lc = strlen(sc);
                char* buf = (char*)malloc(lb + lc + 1);
                memcpy(buf, sb, lb);
                memcpy(buf + lb, sc, lc);
                buf[lb + lc] = '\0';
                free(sb);
                free(sc);
                SYNC_FRAME();
                regs[a] = uf_val_string_take(vm->rt, buf, lb + lc);
            } else if (vb.kind == UF_VAL_ARRAY && vc.kind == UF_VAL_ARRAY) {
                SYNC_FRAME();
                size_t nb = vb.as.array->count;
                size_t nc = vc.as.array->count;
                UfValue arr = uf_val_array(vm->rt, nb + nc);
                for (size_t i = 0; i < nb; ++i) {
                    uf_array_push(vm->rt, arr.as.array, vb.as.array->elements[i]);
                }
                for (size_t i = 0; i < nc; ++i) {
                    uf_array_push(vm->rt, arr.as.array, vc.as.array->elements[i]);
                }
                regs[a] = arr;
            } else {
                REGVM_ERROR("Invalid operands to '+' (%s and %s)",
                            uf_val_type_name(vb), uf_val_type_name(vc));
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_SUB:
#else
        case ROP_SUB:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            uint8_t c = REG_GET_C(instr);
            if (regs[b].kind != UF_VAL_NUMBER || regs[c].kind != UF_VAL_NUMBER) {
                REGVM_ERROR("Operands to '-' must be numbers");
            }
            regs[a] = uf_val_number(regs[b].as.number - regs[c].as.number);
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_MUL:
#else
        case ROP_MUL:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            uint8_t c = REG_GET_C(instr);
            if (regs[b].kind != UF_VAL_NUMBER || regs[c].kind != UF_VAL_NUMBER) {
                REGVM_ERROR("Operands to '*' must be numbers");
            }
            regs[a] = uf_val_number(regs[b].as.number * regs[c].as.number);
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_DIV:
#else
        case ROP_DIV:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            uint8_t c = REG_GET_C(instr);
            if (regs[b].kind != UF_VAL_NUMBER || regs[c].kind != UF_VAL_NUMBER) {
                REGVM_ERROR("Operands to '/' must be numbers");
            }
            if (regs[c].as.number == 0.0) {
                REGVM_ERROR("Division by zero");
            }
            regs[a] = uf_val_number(regs[b].as.number / regs[c].as.number);
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_MOD:
#else
        case ROP_MOD:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            uint8_t c = REG_GET_C(instr);
            if (regs[b].kind != UF_VAL_NUMBER || regs[c].kind != UF_VAL_NUMBER) {
                REGVM_ERROR("Operands to '%%' must be numbers");
            }
            if (regs[c].as.number == 0.0) {
                REGVM_ERROR("Division by zero in modulo");
            }
            regs[a] = uf_val_number(fmod(regs[b].as.number, regs[c].as.number));
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_NEG:
#else
        case ROP_NEG:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            if (regs[b].kind != UF_VAL_NUMBER) {
                REGVM_ERROR("Operand to '-' must be a number");
            }
            regs[a] = uf_val_number(-regs[b].as.number);
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_NOT:
#else
        case ROP_NOT:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            regs[a] = uf_val_bool(!uf_val_is_truthy(regs[b]));
            DISPATCH();
        }

        /* --- Comparison (3-address: A = B op C) --- */
#if UF_USE_COMPUTED_GOTO
        do_ROP_EQ:
#else
        case ROP_EQ:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            uint8_t c = REG_GET_C(instr);
            regs[a] = uf_val_bool(uf_val_equal(regs[b], regs[c]));
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_NEQ:
#else
        case ROP_NEQ:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            uint8_t c = REG_GET_C(instr);
            regs[a] = uf_val_bool(!uf_val_equal(regs[b], regs[c]));
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_LT:
#else
        case ROP_LT:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            uint8_t c = REG_GET_C(instr);
            UfValue vb = regs[b];
            UfValue vc = regs[c];
            if (vb.kind == UF_VAL_NUMBER && vc.kind == UF_VAL_NUMBER) {
                regs[a] = uf_val_bool(vb.as.number < vc.as.number);
            } else if (vb.kind == UF_VAL_STRING && vc.kind == UF_VAL_STRING) {
                regs[a] = uf_val_bool(strcmp(vb.as.string->chars, vc.as.string->chars) < 0);
            } else {
                REGVM_ERROR("Operands to '<' must be comparable");
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_LTE:
#else
        case ROP_LTE:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            uint8_t c = REG_GET_C(instr);
            UfValue vb = regs[b];
            UfValue vc = regs[c];
            if (vb.kind == UF_VAL_NUMBER && vc.kind == UF_VAL_NUMBER) {
                regs[a] = uf_val_bool(vb.as.number <= vc.as.number);
            } else if (vb.kind == UF_VAL_STRING && vc.kind == UF_VAL_STRING) {
                regs[a] = uf_val_bool(strcmp(vb.as.string->chars, vc.as.string->chars) <= 0);
            } else {
                REGVM_ERROR("Operands to '<=' must be comparable");
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_GT:
#else
        case ROP_GT:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            uint8_t c = REG_GET_C(instr);
            UfValue vb = regs[b];
            UfValue vc = regs[c];
            if (vb.kind == UF_VAL_NUMBER && vc.kind == UF_VAL_NUMBER) {
                regs[a] = uf_val_bool(vb.as.number > vc.as.number);
            } else if (vb.kind == UF_VAL_STRING && vc.kind == UF_VAL_STRING) {
                regs[a] = uf_val_bool(strcmp(vb.as.string->chars, vc.as.string->chars) > 0);
            } else {
                REGVM_ERROR("Operands to '>' must be comparable");
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_GTE:
#else
        case ROP_GTE:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            uint8_t c = REG_GET_C(instr);
            UfValue vb = regs[b];
            UfValue vc = regs[c];
            if (vb.kind == UF_VAL_NUMBER && vc.kind == UF_VAL_NUMBER) {
                regs[a] = uf_val_bool(vb.as.number >= vc.as.number);
            } else if (vb.kind == UF_VAL_STRING && vc.kind == UF_VAL_STRING) {
                regs[a] = uf_val_bool(strcmp(vb.as.string->chars, vc.as.string->chars) >= 0);
            } else {
                REGVM_ERROR("Operands to '>=' must be comparable");
            }
            DISPATCH();
        }

        /* --- Control Flow --- */
#if UF_USE_COMPUTED_GOTO
        do_ROP_JMP:
#else
        case ROP_JMP:
#endif
        {
            int32_t offset = REG_GET_sAx(instr);
            ip += offset;
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_JMP_FALSE:
#else
        case ROP_JMP_FALSE:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            int16_t offset = REG_GET_sBx(instr);
            if (!uf_val_is_truthy(regs[a])) {
                ip += offset;
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_JMP_TRUE:
#else
        case ROP_JMP_TRUE:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            int16_t offset = REG_GET_sBx(instr);
            if (uf_val_is_truthy(regs[a])) {
                ip += offset;
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_JMP_ARG:
#else
        case ROP_JMP_ARG:
#endif
        {
            uint8_t arg_idx = REG_GET_A(instr);
            int16_t offset = REG_GET_sBx(instr);
            if (frame->argc > arg_idx) {
                ip += offset;
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_LOOP:
#else
        case ROP_LOOP:
#endif
        {
            uint16_t offset = REG_GET_Bx(instr);
            ip -= offset;
            DISPATCH();
        }

        /* --- Function Calls & Returns --- */
#if UF_USE_COMPUTED_GOTO
        do_ROP_CALL:
#else
        case ROP_CALL:
#endif
        {
            uint8_t base = REG_GET_A(instr);
            uint8_t argc = REG_GET_B(instr);
            uint8_t dest = REG_GET_C(instr);
            UfValue callee = regs[base];

            SYNC_FRAME();
            if (!regvm_call_value(vm, callee, argc, base, dest)) {
                if (vm->had_error) return uf_val_null();
            }
            RELOAD_FRAME();
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_CALL_SPREAD:
#else
        case ROP_CALL_SPREAD:
#endif
        {
            uint8_t base = REG_GET_A(instr);
            uint8_t leading_argc = REG_GET_B(instr);
            uint8_t dest = REG_GET_C(instr);
            UfValue callee = regs[base];
            UfValue spread_val = regs[base + 1 + leading_argc];

            if (spread_val.kind != UF_VAL_ARRAY) {
                REGVM_ERROR("Spread argument must be an array");
            }
            size_t spread_len = spread_val.as.array->count;
            size_t total_argc = leading_argc + spread_len;

            /* Expand spread elements into consecutive registers */
            for (size_t i = 0; i < spread_len; ++i) {
                regs[base + 1 + leading_argc + i] = spread_val.as.array->elements[i];
            }

            SYNC_FRAME();
            if (!regvm_call_value(vm, callee, total_argc, base, dest)) {
                if (vm->had_error) return uf_val_null();
            }
            RELOAD_FRAME();
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_RETURN:
#else
        case ROP_RETURN:
#endif
        {
            while (vm->handler_count > 0 && vm->handlers[vm->handler_count - 1].frame_index >= vm->frame_count - 1) {
                vm->handler_count--;
                if (vm->rt && vm->rt->try_handler_count > 0) vm->rt->try_handler_count--;
            }
            uint8_t a = REG_GET_A(instr);
            UfValue ret_val = regs[a];
            if (frame->closure && frame->closure->function && frame->closure->function->is_async) {
                UfPromiseObject* p = uf_promise_create(vm->rt);
                uf_promise_resolve(vm->rt, p, ret_val);
                ret_val = uf_val_promise(vm->rt, p);
            }
            close_upvalues(vm, regs);

            if (vm->rt && vm->rt->profiler) {
                const char* name = (frame->closure && frame->closure->function && frame->closure->function->name)
                    ? frame->closure->function->name : "<anonymous>";
                uf_profiler_exit(vm->rt->profiler, name);
            }

            uint8_t dest = frame->dest_reg;
            vm->frame_count--;
            if (vm->frame_count == target_frame_count) {
                return ret_val;
            }

            RELOAD_FRAME();
            regs[dest] = ret_val;
            DISPATCH();
        }

        /* --- Collections --- */
#if UF_USE_COMPUTED_GOTO
        do_ROP_NEW_ARRAY:
#else
        case ROP_NEW_ARRAY:
#endif
        {
            uint8_t dest = REG_GET_A(instr);
            uint8_t count = REG_GET_B(instr);
            uint8_t start_reg = REG_GET_C(instr);
            SYNC_FRAME();
            UfValue arr = uf_val_array(vm->rt, count);
            for (size_t i = 0; i < count; ++i) {
                uf_array_push(vm->rt, arr.as.array, regs[start_reg + i]);
            }
            regs[dest] = arr;
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_NEW_MAP:
#else
        case ROP_NEW_MAP:
#endif
        {
            uint8_t dest = REG_GET_A(instr);
            uint8_t pair_count = REG_GET_B(instr);
            uint8_t start_reg = REG_GET_C(instr);
            SYNC_FRAME();
            UfValue map = uf_val_map(vm->rt, pair_count);
            for (size_t i = 0; i < pair_count; ++i) {
                UfValue k = regs[start_reg + 2 * i];
                UfValue v = regs[start_reg + 2 * i + 1];
                if (k.kind != UF_VAL_STRING && k.kind != UF_VAL_NUMBER &&
                    k.kind != UF_VAL_BOOL && k.kind != UF_VAL_NULL) {
                    REGVM_ERROR("Map key must be a string or number, got '%s'", uf_val_type_name(k));
                }
                uf_map_set(vm->rt, map.as.map, k, v);
            }
            regs[dest] = map;
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_ARRAY_PUSH:
#else
        case ROP_ARRAY_PUSH:
#endif
        {
            uint8_t arr_reg = REG_GET_A(instr);
            uint8_t val_reg = REG_GET_B(instr);
            if (regs[arr_reg].kind != UF_VAL_ARRAY) {
                REGVM_ERROR("Expected array for array push");
            }
            SYNC_FRAME();
            uf_array_push(vm->rt, regs[arr_reg].as.array, regs[val_reg]);
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_ARRAY_EXTEND:
#else
        case ROP_ARRAY_EXTEND:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            if (regs[a].kind != UF_VAL_ARRAY || regs[b].kind != UF_VAL_ARRAY) {
                REGVM_ERROR("Expected array for spread extend");
            }
            SYNC_FRAME();
            size_t count = regs[b].as.array->count;
            for (size_t i = 0; i < count; ++i) {
                uf_array_push(vm->rt, regs[a].as.array, regs[b].as.array->elements[i]);
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_ARRAY_SLICE:
#else
        case ROP_ARRAY_SLICE:
#endif
        {
            uint8_t dest = REG_GET_A(instr);
            uint8_t src = REG_GET_B(instr);
            uint8_t start_reg = REG_GET_C(instr);
            if (regs[src].kind != UF_VAL_ARRAY) {
                REGVM_ERROR("Expected array for rest slice");
            }
            size_t start_idx = 0;
            if (regs[start_reg].kind == UF_VAL_NUMBER) {
                long s = (long)regs[start_reg].as.number;
                if (s > 0) start_idx = (size_t)s;
            }
            SYNC_FRAME();
            size_t total = regs[src].as.array->count;
            size_t slice_len = (start_idx < total) ? (total - start_idx) : 0;
            UfValue sliced = uf_val_array(vm->rt, slice_len);
            for (size_t i = start_idx; i < total; ++i) {
                uf_array_push(vm->rt, sliced.as.array, regs[src].as.array->elements[i]);
            }
            regs[dest] = sliced;
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_MAP_SET:
#else
        case ROP_MAP_SET:
#endif
        {
            uint8_t m = REG_GET_A(instr);
            uint8_t k = REG_GET_B(instr);
            uint8_t v = REG_GET_C(instr);
            if (regs[m].kind != UF_VAL_MAP) {
                REGVM_ERROR("Expected map");
            }
            if (regs[k].kind != UF_VAL_STRING && regs[k].kind != UF_VAL_NUMBER &&
                regs[k].kind != UF_VAL_BOOL && regs[k].kind != UF_VAL_NULL) {
                REGVM_ERROR("Map key must be a string or number, got '%s'", uf_val_type_name(regs[k]));
            }
            SYNC_FRAME();
            uf_map_set(vm->rt, regs[m].as.map, regs[k], regs[v]);
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_MAP_EXTEND:
#else
        case ROP_MAP_EXTEND:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            if (regs[a].kind != UF_VAL_MAP || regs[b].kind != UF_VAL_MAP) {
                REGVM_ERROR("Expected map for spread extend");
            }
            SYNC_FRAME();
            UfMapObject* src_map = regs[b].as.map;
            for (size_t i = 0; i < src_map->order_count; ++i) {
                UfValue k = src_map->order_keys[i];
                UfValue v = uf_map_get(src_map, k);
                uf_map_set(vm->rt, regs[a].as.map, k, v);
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_INDEX_GET:
#else
        case ROP_INDEX_GET:
#endif
        {
            uint8_t dest = REG_GET_A(instr);
            uint8_t target_reg = REG_GET_B(instr);
            uint8_t idx_reg = REG_GET_C(instr);
            UfValue target = regs[target_reg];
            UfValue index = regs[idx_reg];

            if (target.kind == UF_VAL_ARRAY) {
                if (index.kind != UF_VAL_NUMBER) {
                    REGVM_ERROR("Array index must be a number");
                }
                long idx = (long)index.as.number;
                if (idx < 0) idx += target.as.array->count;
                if (idx < 0 || (size_t)idx >= target.as.array->count) {
                    REGVM_ERROR("IndexOutOfBounds: Index %ld out of bounds for array of length %zu",
                                idx, target.as.array->count);
                }
                regs[dest] = uf_array_get(target.as.array, (size_t)idx);
            } else if (target.kind == UF_VAL_MAP) {
                regs[dest] = uf_map_get(target.as.map, index);
            } else if (target.kind == UF_VAL_STRING) {
                if (index.kind != UF_VAL_NUMBER) {
                    REGVM_ERROR("String index must be a number");
                }
                long idx = (long)index.as.number;
                if (idx < 0) idx += target.as.string->length;
                if (idx < 0 || (size_t)idx >= target.as.string->length) {
                    REGVM_ERROR("String index out of bounds");
                }
                char ch_buf[2] = { target.as.string->chars[idx], '\0' };
                SYNC_FRAME();
                regs[dest] = uf_val_string_cstr(vm->rt, ch_buf);
            } else if (target.kind == UF_VAL_INSTANCE) {
                if (index.kind != UF_VAL_STRING) {
                    REGVM_ERROR("Instance field must be a string");
                }
                const char* fname = index.as.string->chars;
                UfInstanceObject* inst = target.as.instance;
                bool found = false;
                for (size_t i = 0; i < inst->field_count; ++i) {
                    if (strcmp(inst->def->field_names[i], fname) == 0) {
                        regs[dest] = inst->fields[i];
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    for (size_t i = 0; i < inst->def->method_count; ++i) {
                        if (strcmp(inst->def->method_names[i], fname) == 0) {
                            SYNC_FRAME();
                            regs[dest] = uf_val_bound_method(vm->rt, target, inst->def->method_values[i]);
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    REGVM_ERROR("Field '%s' not found on struct '%s'", fname, inst->def->name);
                }
            } else if (target.kind == UF_VAL_MODULE) {
                if (index.kind != UF_VAL_STRING) {
                    REGVM_ERROR("Module member access expects a string name");
                }
                UfValue res = uf_map_get(target.as.module->exports.as.map, index);
                if (res.kind == UF_VAL_NULL && !uf_map_has(target.as.module->exports.as.map, index)) {
                    REGVM_ERROR("Module '%s' has no exported member '%s'",
                                (target.as.module && target.as.module->name) ? target.as.module->name : "anonymous",
                                index.as.string->chars);
                }
                regs[dest] = res;
            } else if (target.kind == UF_VAL_ERROR) {
                if (index.kind != UF_VAL_STRING) {
                    REGVM_ERROR("Error property access expects string");
                }
                const char* prop = index.as.string->chars;
                UfErrorObject* err = target.as.error;
                SYNC_FRAME();
                if (strcmp(prop, "kind") == 0) {
                    regs[dest] = uf_val_string(vm->rt, err->kind ? err->kind->chars : "Error", err->kind ? err->kind->length : 5);
                } else if (strcmp(prop, "message") == 0) {
                    regs[dest] = uf_val_string(vm->rt, err->message ? err->message->chars : "", err->message ? err->message->length : 0);
                } else if (strcmp(prop, "line") == 0) {
                    regs[dest] = uf_val_number((double)err->line);
                } else if (strcmp(prop, "file") == 0) {
                    regs[dest] = uf_val_string_cstr(vm->rt, err->file ? err->file : "<unknown>");
                } else {
                    regs[dest] = uf_val_null();
                }
            } else if (target.kind == UF_VAL_ENUM_DEF) {
                if (index.kind != UF_VAL_STRING) {
                    REGVM_ERROR("Enum member access expects a string variant name");
                }
                const char* vname = index.as.string->chars;
                UfEnumDefObject* def = target.as.enum_def;
                bool found = false;
                for (size_t i = 0; i < def->variant_count; ++i) {
                    if (strcmp(def->variant_names[i], vname) == 0) {
                        found = true;
                        SYNC_FRAME();
                        regs[dest] = uf_val_enum_val(vm->rt, def, (int)i, def->variant_names[i], NULL, 0);
                        break;
                    }
                }
                if (!found) {
                    REGVM_ERROR("Enum '%s' has no variant '%s'", def->name ? def->name : "", vname);
                }
            } else if (target.kind == UF_VAL_ENUM_VAL) {
                UfEnumValObject* ev = target.as.enum_val;
                if (index.kind == UF_VAL_NUMBER) {
                    long idx = (long)index.as.number;
                    if (idx >= 0 && (size_t)idx < ev->field_count) {
                        regs[dest] = ev->fields[idx];
                    } else {
                        regs[dest] = uf_val_null();
                    }
                } else if (index.kind == UF_VAL_STRING) {
                    const char* prop = index.as.string->chars;
                    if (strcmp(prop, "tag") == 0) {
                        regs[dest] = uf_val_number((double)ev->tag);
                    } else if (strcmp(prop, "name") == 0) {
                        SYNC_FRAME();
                        regs[dest] = uf_val_string_cstr(vm->rt, ev->variant_name ? ev->variant_name : "");
                    } else {
                        bool found = false;
                        if (ev->def && (size_t)ev->tag < ev->def->variant_count && ev->def->variant_field_names) {
                            const char** fnames = ev->def->variant_field_names[ev->tag];
                            if (fnames) {
                                for (size_t f = 0; f < ev->field_count; ++f) {
                                    if (strcmp(fnames[f], prop) == 0) {
                                        regs[dest] = ev->fields[f];
                                        found = true;
                                        break;
                                    }
                                }
                            }
                        }
                        if (!found) {
                            REGVM_ERROR("Enum variant '%s' has no field '%s'",
                                        ev->variant_name ? ev->variant_name : "", prop);
                        }
                    }
                } else {
                    REGVM_ERROR("Enum property access expects string or index");
                }
            } else {
                REGVM_ERROR("Cannot index type '%s'", uf_val_type_name(target));
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_INDEX_SET:
#else
        case ROP_INDEX_SET:
#endif
        {
            uint8_t target_reg = REG_GET_A(instr);
            uint8_t idx_reg = REG_GET_B(instr);
            uint8_t val_reg = REG_GET_C(instr);
            UfValue target = regs[target_reg];
            UfValue index = regs[idx_reg];
            UfValue val = regs[val_reg];

            if (target.kind == UF_VAL_ARRAY) {
                if (index.kind != UF_VAL_NUMBER) {
                    REGVM_ERROR("Array index must be a number");
                }
                long idx = (long)index.as.number;
                if (idx < 0) idx += target.as.array->count;
                if (idx < 0 || (size_t)idx >= target.as.array->count) {
                    REGVM_ERROR("Array index out of bounds");
                }
                uf_array_set(target.as.array, (size_t)idx, val);
            } else if (target.kind == UF_VAL_MAP) {
                if (index.kind != UF_VAL_STRING && index.kind != UF_VAL_NUMBER &&
                    index.kind != UF_VAL_BOOL && index.kind != UF_VAL_NULL) {
                    REGVM_ERROR("Map key must be a string or number, got '%s'", uf_val_type_name(index));
                }
                SYNC_FRAME();
                uf_map_set(vm->rt, target.as.map, index, val);
            } else if (target.kind == UF_VAL_INSTANCE) {
                if (index.kind != UF_VAL_STRING) {
                    REGVM_ERROR("Instance field must be a string");
                }
                const char* fname = index.as.string->chars;
                UfInstanceObject* inst = target.as.instance;
                bool found = false;
                for (size_t i = 0; i < inst->field_count; ++i) {
                    if (strcmp(inst->def->field_names[i], fname) == 0) {
                        inst->fields[i] = val;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    REGVM_ERROR("Field '%s' not found on struct '%s'", fname, inst->def->name);
                }
            } else {
                REGVM_ERROR("Cannot index-assign type '%s'", uf_val_type_name(target));
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_ITER_GET:
#else
        case ROP_ITER_GET:
#endif
        {
            uint8_t dest = REG_GET_A(instr);
            uint8_t target_reg = REG_GET_B(instr);
            uint8_t idx_reg = REG_GET_C(instr);
            UfValue target = regs[target_reg];
            UfValue index = regs[idx_reg];

            if (index.kind != UF_VAL_NUMBER) {
                REGVM_ERROR("Iterator index must be a number");
            }
            long idx = (long)index.as.number;
            if (target.kind == UF_VAL_ARRAY) {
                if (idx < 0 || (size_t)idx >= target.as.array->count) {
                    REGVM_ERROR("Array index out of bounds");
                }
                regs[dest] = target.as.array->elements[idx];
            } else if (target.kind == UF_VAL_MAP) {
                if (idx < 0 || (size_t)idx >= target.as.map->order_count) {
                    REGVM_ERROR("Map index out of bounds");
                }
                regs[dest] = target.as.map->order_keys[idx];
            } else if (target.kind == UF_VAL_STRING) {
                if (idx < 0 || (size_t)idx >= target.as.string->length) {
                    REGVM_ERROR("String index out of bounds");
                }
                char ch_buf[2] = { target.as.string->chars[idx], '\0' };
                SYNC_FRAME();
                regs[dest] = uf_val_string_cstr(vm->rt, ch_buf);
            } else {
                REGVM_ERROR("Cannot iterate type '%s'", uf_val_type_name(target));
            }
            DISPATCH();
        }

        /* --- Pattern Matching Assertions & Destructuring --- */
#if UF_USE_COMPUTED_GOTO
        do_ROP_ASSERT_ARRAY:
#else
        case ROP_ASSERT_ARRAY:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            if (regs[a].kind != UF_VAL_ARRAY) {
                REGVM_ERROR("TypeError: Cannot destructure non-array value");
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_ASSERT_MAP:
#else
        case ROP_ASSERT_MAP:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            if (regs[a].kind != UF_VAL_MAP) {
                REGVM_ERROR("TypeError: Cannot destructure non-map value");
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_ARRAY_GET_SAFE:
#else
        case ROP_ARRAY_GET_SAFE:
#endif
        {
            uint8_t dest = REG_GET_A(instr);
            uint8_t arr_reg = REG_GET_B(instr);
            uint8_t idx_reg = REG_GET_C(instr);
            UfValue arr = regs[arr_reg];
            UfValue idx_val = regs[idx_reg];
            if (arr.kind == UF_VAL_ARRAY && idx_val.kind == UF_VAL_NUMBER) {
                long idx = (long)idx_val.as.number;
                if (idx >= 0 && (size_t)idx < arr.as.array->count) {
                    regs[dest] = arr.as.array->elements[idx];
                } else {
                    regs[dest] = uf_val_null();
                }
            } else {
                regs[dest] = uf_val_null();
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_MAP_GET_SAFE:
#else
        case ROP_MAP_GET_SAFE:
#endif
        {
            uint8_t dest = REG_GET_A(instr);
            uint8_t map_reg = REG_GET_B(instr);
            uint8_t key_reg = REG_GET_C(instr);
            UfValue m = regs[map_reg];
            UfValue k = regs[key_reg];
            if (m.kind == UF_VAL_MAP) {
                regs[dest] = uf_map_get(m.as.map, k);
            } else {
                regs[dest] = uf_val_null();
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_MAP_REST:
#else
        case ROP_MAP_REST:
#endif
        {
            uint8_t dest = REG_GET_A(instr);
            uint8_t src_reg = REG_GET_B(instr);
            uint8_t exclude_count = REG_GET_C(instr);
            uint32_t* excl_ptr = ip;
            ip += exclude_count;
            UfValue src = regs[src_reg];
            if (src.kind != UF_VAL_MAP) {
                regs[dest] = uf_val_map(vm->rt, 0);
            } else {
                SYNC_FRAME();
                UfValue rest_map = uf_val_map(vm->rt, src.as.map->count);
                for (size_t i = 0; i < src.as.map->order_count; ++i) {
                    UfValue k = src.as.map->order_keys[i];
                    bool excluded = false;
                    for (size_t e = 0; e < exclude_count; ++e) {
                        uint32_t excl_instr = excl_ptr[e];
                        uint16_t const_idx = REG_GET_Bx(excl_instr);
                        if (uf_val_equal(k, chunk->constants[const_idx])) {
                            excluded = true;
                            break;
                        }
                    }
                    if (!excluded) {
                        UfValue v = uf_map_get(src.as.map, k);
                        uf_map_set(vm->rt, rest_map.as.map, k, v);
                    }
                }
                regs[dest] = rest_map;
            }
            DISPATCH();
        }

        /* --- Statements --- */
#if UF_USE_COMPUTED_GOTO
        do_ROP_SAY:
#else
        case ROP_SAY:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            char* str = uf_val_to_string(regs[a]);
            fprintf(vm->rt->out_stream, "%s\n", str ? str : "null");
            free(str);
            fflush(vm->rt->out_stream);
            DISPATCH();
        }

        /* --- Structs --- */
#if UF_USE_COMPUTED_GOTO
        do_ROP_STRUCT_DEF:
#else
        case ROP_STRUCT_DEF:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint16_t bx = REG_GET_Bx(instr);
            regs[a] = chunk->constants[bx];
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_INSTANCE:
#else
        case ROP_INSTANCE:
#endif
        {
            uint8_t dest = REG_GET_A(instr);
            uint8_t target_reg = REG_GET_B(instr);
            uint8_t name_const_idx = REG_GET_C(instr);
            const char* sname = chunk->constants[name_const_idx].as.string->chars;
            UfValue target = regs[target_reg];
            bool matches = false;
            if (target.kind == UF_VAL_INSTANCE &&
                target.as.instance &&
                target.as.instance->def &&
                strcmp(target.as.instance->def->name, sname) == 0) {
                matches = true;
            } else if (target.kind == UF_VAL_ENUM_VAL &&
                       target.as.enum_val &&
                       target.as.enum_val->variant_name &&
                       strcmp(target.as.enum_val->variant_name, sname) == 0) {
                matches = true;
            }
            regs[dest] = uf_val_bool(matches);
            DISPATCH();
        }

        /* --- Exception Handling --- */
#if UF_USE_COMPUTED_GOTO
        do_ROP_PUSH_TRY:
#else
        case ROP_PUSH_TRY:
#endif
        {
            uint8_t err_reg = REG_GET_A(instr);
            uint16_t catch_offset = REG_GET_Bx(instr);
            SYNC_FRAME();
            if (vm->handler_count >= UF_REGVM_HANDLERS_MAX || (vm->rt && vm->rt->try_handler_count >= UF_MAX_TRY_HANDLERS)) {
                REGVM_ERROR("Maximum nested try-catch handlers exceeded");
            }
            int h_idx = vm->handler_count++;
            UfRegVMHandler* h = &vm->handlers[h_idx];
            h->frame_index = vm->frame_count - 1;
            h->catch_ip = ip + catch_offset;
            h->error_reg = err_reg;

            if (vm->rt) {
                UfTryHandler* th = &vm->rt->try_handlers[vm->rt->try_handler_count++];
                th->frame_count = vm->rt->frame_count;
                th->temp_root_count = vm->rt->temp_root_count;
                th->scope_env = vm->rt->current_env;

                if (setjmp(th->jmp) != 0) {
                    /* Defensive: a longjmp must always be paired with a live
                     * VM handler. Bail out rather than index handlers[-1] if
                     * the two stacks ever drift apart again. */
                    if (vm->handler_count <= 0) {
                        vm->had_error = true;
                        if (vm->rt) vm->rt->had_runtime_error = true;
                        return uf_val_null();
                    }
                    UfRegVMHandler* cur_h = &vm->handlers[vm->handler_count - 1];
                    UfValue err = vm->rt->current_error;
                    while (vm->frame_count - 1 > cur_h->frame_index) {
                        UfRegFrame* top = &vm->frames[vm->frame_count - 1];
                        close_upvalues(vm, top->regs);
                        vm->frame_count--;
                    }
                    RELOAD_FRAME();
                    close_upvalues(vm, frame->regs);
                    frame->regs[cur_h->error_reg] = err;
                    ip = cur_h->catch_ip;
                    vm->handler_count--;
                    vm->had_error = false;
                    vm->rt->had_runtime_error = false;
                    vm->rt->current_error = uf_val_null();
                    DISPATCH();
                }
            }
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_POP_TRY:
#else
        case ROP_POP_TRY:
#endif
        {
            if (vm->handler_count > 0) vm->handler_count--;
            if (vm->rt && vm->rt->try_handler_count > 0) vm->rt->try_handler_count--;
            DISPATCH();
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_RETHROW:
#else
        case ROP_RETHROW:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            SYNC_FRAME();
            UfValue err = regs[a];
            if (vm->rt) vm->rt->current_error = err;
            if (vm->handler_count > 0) {
                if (vm->rt && vm->rt->try_handler_count > 0) {
                    vm->rt->try_handler_count--;
                }
                UfRegVMHandler* h = &vm->handlers[--vm->handler_count];
                while (vm->frame_count - 1 > h->frame_index) {
                    UfRegFrame* top = &vm->frames[vm->frame_count - 1];
                    close_upvalues(vm, top->regs);
                    vm->frame_count--;
                }
                RELOAD_FRAME();
                close_upvalues(vm, frame->regs);
                frame->regs[h->error_reg] = err;
                ip = h->catch_ip;
                DISPATCH();
            } else if (vm->rt && vm->rt->try_handler_count > 0) {
                UfTryHandler* th = &vm->rt->try_handlers[--vm->rt->try_handler_count];
                vm->rt->frame_count = th->frame_count;
                vm->rt->temp_root_count = th->temp_root_count;
                vm->rt->current_env = th->scope_env;
                longjmp(th->jmp, 1);
            } else {
                vm->rt->had_runtime_error = true;
                vm->rt->current_error = err;
                if (regs[a].kind == UF_VAL_ERROR && regs[a].as.error) {
                    const char* msg = regs[a].as.error->message ? regs[a].as.error->message->chars : "Error";
                    SourceSpan sp = {
                        .start = { .line = (uint32_t)regs[a].as.error->line, .col = 1, .file = regs[a].as.error->file },
                        .end = { .line = (uint32_t)regs[a].as.error->line, .col = 1, .file = regs[a].as.error->file }
                    };
                    if (vm->rt->reporter) {
                        uf_report_diag(vm->rt->reporter, UF_DIAG_RUNTIME_ERROR, sp, msg, NULL);
                    } else {
                        fprintf(vm->rt->err_stream, "Runtime Error: %s\n", msg);
                    }
                } else {
                    char* err_str = uf_val_to_string(regs[a]);
                    fprintf(vm->rt->err_stream, "Runtime Error: %s\n", err_str ? err_str : "Error");
                    free(err_str);
                }
                vm->had_error = true;
                return uf_val_null();
            }
        }

#if UF_USE_COMPUTED_GOTO
        do_ROP_AWAIT:
#else
        case ROP_AWAIT:
#endif
        {
            uint8_t a = REG_GET_A(instr);
            uint8_t b = REG_GET_B(instr);
            UfValue val = regs[b];
            SYNC_FRAME();
            if (val.kind == UF_VAL_PROMISE && val.as.promise) {
                val = uf_promise_await(vm->rt, val.as.promise);
            }
            regs[a] = val;
            DISPATCH();
        }

#if !UF_USE_COMPUTED_GOTO
        default:
            REGVM_ERROR("Unknown opcode %d", (int)REG_GET_OP(instr));
            break;
        } /* end switch */
#endif
    }
    return uf_val_null();
}

UfInterpretResult uf_regvm_run(UfRegVM* vm, UfRegFunction* function) {
    if (!function || !vm) return UF_INTERPRET_RUNTIME_ERROR;

    vm->frame_count = 0;
    vm->stack_top = vm->stack;
    UfRegClosure* closure = uf_reg_closure_new(vm->rt, function);
    UfRegFrame* frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->regs = vm->stack;
    frame->argc = 0;
    frame->dest_reg = 0;

    size_t initial_regs = closure->function->max_regs + 16;
    vm->stack_top = vm->stack + (initial_regs < UF_REGVM_STACK_MAX ? initial_regs : UF_REGVM_STACK_MAX);

    run_regvm_frames(vm, 0);

    if (vm->had_error || (vm->rt && vm->rt->had_runtime_error)) {
        return UF_INTERPRET_RUNTIME_ERROR;
    }
    return UF_INTERPRET_OK;
}

UfValue uf_regvm_run_closure(UfRegVM* vm, UfRegClosure* closure, size_t argc, UfValue* args) {
    if (!closure || !closure->function || !vm) return uf_val_null();

    if (vm->frame_count >= UF_REGVM_FRAMES_MAX) {
        regvm_runtime_error(vm, "StackOverflowError: Maximum call stack depth exceeded (%d frames)",
                            UF_REGVM_FRAMES_MAX_CALLS);
        return uf_val_null();
    }

    int target_frame_count = vm->frame_count;
    UfValue* frame_regs = vm->stack_top;
    UfRegFunction* fn = closure->function;

    /* Slot 0 is reserved for closure */
    frame_regs[0] = uf_val_reg_closure(vm->rt, closure);

    if (fn->has_rest) {
        size_t fixed_count = fn->arity - 1;
        for (size_t i = 0; i < fixed_count && i < argc; ++i) {
            frame_regs[1 + i] = args[i];
        }
        for (size_t i = argc; i < fixed_count; ++i) {
            frame_regs[1 + i] = uf_val_null();
        }
        size_t rest_count = (argc > fixed_count) ? (argc - fixed_count) : 0;
        UfValue rest_arr = uf_val_array(vm->rt, rest_count);
        for (size_t i = 0; i < rest_count; ++i) {
            uf_array_push(vm->rt, rest_arr.as.array, args[fixed_count + i]);
        }
        frame_regs[1 + fixed_count] = rest_arr;
    } else {
        for (size_t i = 0; i < argc; ++i) {
            frame_regs[1 + i] = args[i];
        }
        for (size_t i = argc; i < fn->arity; ++i) {
            frame_regs[1 + i] = uf_val_null();
        }
    }

    size_t needed = 1 + fn->max_regs + 16;
    if (frame_regs + needed > vm->stack + UF_REGVM_STACK_MAX) {
        regvm_runtime_error(vm, "StackOverflowError: Register stack overflow (%d slots)", UF_REGVM_STACK_MAX);
        return uf_val_null();
    }
    UfValue* prev_stack_top = vm->stack_top;
    vm->stack_top = frame_regs + needed;

    UfRegFrame* frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = fn->chunk.code;
    frame->regs = frame_regs;
    frame->argc = argc;
    frame->dest_reg = 0;

    UfValue ret = run_regvm_frames(vm, target_frame_count);
    vm->stack_top = prev_stack_top;
    return ret;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
