#include "uf_vm.h"
#include "uf_disasm.h"
#include "../runtime/uf_env.h"
#include "../runtime/uf_module.h"
#include "../runtime/uf_fiber.h"
#include "../tooling/uf_profiler.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

static void vm_runtime_error(UfVM* vm, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (vm->rt) {
        uf_runtime_error(vm->rt, (SourceSpan){0}, "%s", buf);
    } else {
        fprintf(stderr, "Runtime Error: %s\n", buf);
    }
}

void uf_vm_init(UfVM* vm, UfRuntime* rt) {
    vm->frame_count = 0;
    vm->stack_top = vm->stack;
    vm->open_upvalues = NULL;
    vm->handler_count = 0;
    vm->rt = rt;
    vm->had_error = false;
    vm->trace_execution = false;
    vm->total_instructions = 0;
    vm->peak_stack_depth = 0;
    vm->peak_frame_depth = 0;
    if (rt) {
        rt->active_vm = vm;
        if (!rt->call_fn) {
            rt->call_fn = uf_call_value;
        }
    }
}

void uf_vm_free(UfVM* vm) {
    if (vm->rt && vm->rt->active_vm == vm) {
        vm->rt->active_vm = NULL;
    }
    vm->frame_count = 0;
    vm->stack_top = vm->stack;
    vm->open_upvalues = NULL;
    vm->handler_count = 0;
    vm->had_error = false;
    vm->trace_execution = false;
    vm->total_instructions = 0;
    vm->peak_stack_depth = 0;
    vm->peak_frame_depth = 0;
}


static UfUpvalueCell* capture_upvalue(UfVM* vm, UfValue* local) {
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

static void close_upvalues(UfVM* vm, UfValue* last) {
    while (vm->open_upvalues && vm->open_upvalues->location >= last) {
        UfUpvalueCell* up = vm->open_upvalues;
        up->closed = *up->location;
        up->location = &up->closed;
        vm->open_upvalues = up->next;
    }
}

static bool call_value(UfVM* vm, UfValue callee, size_t argc) {
    if (callee.kind == UF_VAL_BOUND_METHOD) {
        UfBoundMethodObject* bm = callee.as.bound_method;
        if (vm->stack_top >= vm->stack + UF_VM_STACK_MAX) {
            vm_runtime_error(vm, "Stack overflow calling bound method");
            vm->had_error = true;
            return false;
        }
        for (int i = 0; i < (int)argc; ++i) {
            vm->stack_top[-i] = vm->stack_top[-i - 1];
        }
        vm->stack_top++;
        vm->stack_top[-argc - 1] = bm->receiver;
        vm->stack_top[-argc - 2] = bm->method;
        return call_value(vm, bm->method, argc + 1);
    }

    if (callee.kind == UF_VAL_CLOSURE) {
        UfClosureObject* cl = callee.as.closure;
        if (cl->function->has_rest) {
            if (argc < cl->function->min_arity) {
                vm_runtime_error(vm, "Function '%s' expects at least %zu arguments, but %zu provided",
                        cl->function->name ? cl->function->name : "anonymous", cl->function->min_arity, argc);
                vm->had_error = true;
                return false;
            }
        } else {
            if (argc < cl->function->min_arity || argc > cl->function->arity) {
                if (cl->function->min_arity == cl->function->arity) {
                    vm_runtime_error(vm, "Function '%s' expects %zu arguments, but %zu provided",
                            cl->function->name ? cl->function->name : "anonymous", cl->function->arity, argc);
                } else {
                    vm_runtime_error(vm, "Function '%s' expects %zu to %zu arguments, but %zu provided",
                            cl->function->name ? cl->function->name : "anonymous", cl->function->min_arity, cl->function->arity, argc);
                }
                vm->had_error = true;
                return false;
            }
        }

        if (vm->frame_count >= UF_VM_FRAMES_MAX) {
            vm_runtime_error(vm, "StackOverflowError: Maximum call stack depth exceeded (%d frames)",
                    UF_VM_FRAMES_MAX_CALLS);
            vm->had_error = true;
            return false;
        }

        if (cl->function->has_rest) {
            size_t fixed_count = cl->function->arity - 1;
            if (argc < fixed_count) {
                for (size_t i = argc; i < fixed_count; ++i) {
                    if (vm->stack_top >= vm->stack + UF_VM_STACK_MAX) {
                        vm_runtime_error(vm, "Stack overflow while initializing default parameters");
                        vm->had_error = true;
                        return false;
                    }
                    *vm->stack_top++ = uf_val_null();
                }
                if (vm->stack_top >= vm->stack + UF_VM_STACK_MAX) {
                    vm_runtime_error(vm, "Stack overflow while initializing rest parameter");
                    vm->had_error = true;
                    return false;
                }
                *vm->stack_top++ = uf_val_array(vm->rt, 0);
            } else {
                size_t rest_count = argc - fixed_count;
                UfValue rest_arr = uf_val_array(vm->rt, rest_count);
                for (size_t i = 0; i < rest_count; ++i) {
                    uf_array_push(vm->rt, rest_arr.as.array, (vm->stack_top - argc + fixed_count)[i]);
                }
                (vm->stack_top - argc + fixed_count)[0] = rest_arr;
                vm->stack_top = (vm->stack_top - argc) + cl->function->arity;
            }
        } else {
            for (size_t i = argc; i < cl->function->arity; ++i) {
                if (vm->stack_top >= vm->stack + UF_VM_STACK_MAX) {
                    vm_runtime_error(vm, "Stack overflow while initializing default parameters");
                    vm->had_error = true;
                    return false;
                }
                *vm->stack_top++ = uf_val_null();
            }
        }

        UfVMFrame* frame = &vm->frames[vm->frame_count++];
        frame->closure = cl;
        frame->ip = cl->function->chunk.code;
        frame->slots = vm->stack_top - cl->function->arity - 1;
        frame->argc = argc;
        if (vm->rt && vm->rt->profiler) {
            const char* name = cl->function->name ? cl->function->name : "<anonymous>";
            uf_profiler_enter(vm->rt->profiler, name);
        }
        return true;
    }

    if (callee.kind == UF_VAL_NATIVE_FN) {
        UfNativeFn fn = callee.as.native_fn.fn;
        int arity = callee.as.native_fn.arity;
        if (arity != -1 && arity != (int)argc) {
            vm_runtime_error(vm, "Native function '%s' expects %d arguments, but %u provided",
                    callee.as.native_fn.name, arity, argc);
            vm->had_error = true;
            return false;
        }

        if (vm->rt && vm->rt->profiler) {
            uf_profiler_enter(vm->rt->profiler, callee.as.native_fn.name);
        }
        UfValue res = fn(vm->rt, argc, vm->stack_top - argc);
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
        vm->stack_top -= argc + 1;
        uf_vm_push(vm, res);
        return true;
    }

    if (callee.kind == UF_VAL_STRUCT_DEF) {
        UfStructDefObject* sdef = callee.as.struct_def;
        if (argc != sdef->field_count) {
            vm_runtime_error(vm, "Struct '%s' expects %zu fields, but %u provided",
                    sdef->name, sdef->field_count, argc);
            vm->had_error = true;
            return false;
        }

        UfValue inst = uf_val_instance(vm->rt, sdef, vm->stack_top - argc, argc);
        vm->stack_top -= argc + 1;
        uf_vm_push(vm, inst);
        return true;
    }

    if (callee.kind == UF_VAL_ENUM_VAL) {
        UfEnumValObject* ev = callee.as.enum_val;
        if (ev->def && (size_t)ev->tag < ev->def->variant_count) {
            size_t expected = ev->def->variant_field_counts[ev->tag];
            if (argc != expected) {
                vm_runtime_error(vm, "TypeError: Enum variant '%s' expects %zu argument%s, but %u provided",
                                 ev->variant_name, expected, expected == 1 ? "" : "s", argc);
                vm->had_error = true;
                return false;
            }
            UfValue eval = uf_val_enum_val(vm->rt, ev->def, ev->tag, ev->variant_name, vm->stack_top - argc, argc);
            vm->stack_top -= argc + 1;
            uf_vm_push(vm, eval);
            return true;
        }
    }

    if (callee.kind == UF_VAL_FUNCTION) {
        /* Tree-walk function called from VM */
        SourceSpan span = (SourceSpan){0};
        UfValue res = uf_runtime_call(vm->rt, callee, argc, vm->stack_top - argc, span);
        if (vm->rt && vm->rt->had_runtime_error) {
            vm->had_error = true;
            return false;
        }
        vm->stack_top -= argc + 1;
        uf_vm_push(vm, res);
        return true;
    }

    char* c_str = uf_val_to_string(callee);
    vm_runtime_error(vm, "Attempted to call non-callable value of type '%s' (%s)",
            uf_val_type_name(callee), c_str ? c_str : "");
    free(c_str);
    vm->had_error = true;
    return false;
}

static UfValue run_vm_frames(UfVM* vm, int target_frame_count) {
    UfVMFrame* volatile frame = &vm->frames[vm->frame_count - 1];
    uint8_t* volatile ip = frame->ip;
    UfValue* volatile stack_top = vm->stack_top;

#define READ_BYTE() (*ip++)
#define READ_U16() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_CONSTANT(idx) (frame->closure->function->chunk.constants[idx])
#define ERROR_RETURN() do { SYNC_VM(); vm->had_error = true; return uf_val_null(); } while (0)
#define VM_RUNTIME_ERROR(...) do { \
    SYNC_VM(); \
    vm_runtime_error(vm, __VA_ARGS__); \
    ERROR_RETURN(); \
} while (0)
#define PUSH(val) do { \
    if (stack_top >= vm->stack + UF_VM_STACK_MAX) { \
        VM_RUNTIME_ERROR("StackOverflowError: VM stack overflow (%d slots)", UF_VM_STACK_MAX); \
    } \
    *stack_top++ = (val); \
} while (0)
#define POP() (*--stack_top)
#define PEEK(dist) (stack_top[-1 - (dist)])
#define SYNC_VM() do { vm->stack_top = stack_top; frame->ip = ip; } while (0)
#define RESTORE_VM() do { stack_top = vm->stack_top; ip = frame->ip; } while (0)

    for (;;) {
        vm->total_instructions++;
        size_t cur_stack = (size_t)(stack_top - vm->stack);
        if (cur_stack > vm->peak_stack_depth) vm->peak_stack_depth = cur_stack;
        if ((size_t)vm->frame_count > vm->peak_frame_depth) vm->peak_frame_depth = (size_t)vm->frame_count;

        if (vm->trace_execution) {
            SYNC_VM();
            uf_disasm_trace_instruction(vm, (const struct UfVMFrame*)frame, stdout);
        }

        uint8_t instruction = READ_BYTE();
        switch (instruction) {
            case OP_CONSTANT: {
                uint16_t const_idx = READ_U16();
                PUSH(READ_CONSTANT(const_idx));
                break;
            }
            case OP_NULL:
                PUSH(uf_val_null());
                break;
            case OP_TRUE:
                PUSH(uf_val_bool(true));
                break;
            case OP_FALSE:
                PUSH(uf_val_bool(false));
                break;
            case OP_POP:
                stack_top--;
                break;
            case OP_DUP:
                *stack_top = stack_top[-1];
                stack_top++;
                break;
            case OP_LOAD_LOCAL: {
                uint16_t slot = READ_U16();
                PUSH(frame->slots[slot]);
                break;
            }
            case OP_STORE_LOCAL: {
                uint16_t slot = READ_U16();
                frame->slots[slot] = PEEK(0);
                break;
            }
            case OP_LOAD_GLOBAL: {
                uint16_t c_idx = READ_U16();
                const char* name = READ_CONSTANT(c_idx).as.string->chars;
                UfValue val;
                if (uf_env_lookup(vm->rt->global_env, name, &val)) {
                    PUSH(val);
                } else {
                    VM_RUNTIME_ERROR("Undefined variable '%s'", name);
                }
                break;
            }
            case OP_STORE_GLOBAL: {
                uint16_t c_idx = READ_U16();
                const char* name = READ_CONSTANT(c_idx).as.string->chars;
                if (!uf_env_assign(vm->rt->global_env, name, PEEK(0))) {
                    VM_RUNTIME_ERROR("Undefined variable '%s'", name);
                }
                break;
            }
            case OP_DEFINE_GLOBAL: {
                uint16_t c_idx = READ_U16();
                const char* name = READ_CONSTANT(c_idx).as.string->chars;
                uf_env_declare(vm->rt->global_env, name, PEEK(0));
                stack_top--;
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                PUSH(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = PEEK(0);
                break;
            }
            case OP_CLOSURE: {
                uint16_t c_idx = READ_U16();
                UfBytecodeFunction* fn = READ_CONSTANT(c_idx).as.bytecode_fn;
                SYNC_VM();
                UfClosureObject* cl = uf_closure_new(vm->rt, fn);
                PUSH(uf_val_closure(vm->rt, cl));

                for (size_t i = 0; i < fn->upvalue_count; ++i) {
                    uint8_t is_local = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (is_local) {
                        SYNC_VM();
                        cl->upvalues[i] = capture_upvalue(vm, frame->slots + index);
                    } else {
                        cl->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OP_CLOSE_UPVALUE: {
                SYNC_VM();
                close_upvalues(vm, stack_top - 1);
                stack_top--;
                break;
            }
            case OP_ADD: {
                UfValue* b_ptr = stack_top - 1;
                UfValue* a_ptr = stack_top - 2;
                if (a_ptr->kind == UF_VAL_NUMBER && b_ptr->kind == UF_VAL_NUMBER) {
                    a_ptr->as.number += b_ptr->as.number;
                    stack_top--;
                } else {
                    UfValue a = *a_ptr;
                    UfValue b = *b_ptr;
                    if (a.kind == UF_VAL_STRING || b.kind == UF_VAL_STRING) {
                        char* sa = uf_val_to_string(a);
                        char* sb = uf_val_to_string(b);
                        size_t la = strlen(sa);
                        size_t lb = strlen(sb);
                        char* buf = (char*)malloc(la + lb + 1);
                        memcpy(buf, sa, la);
                        memcpy(buf + la, sb, lb);
                        buf[la + lb] = '\0';
                        free(sa);
                        free(sb);
                        SYNC_VM();
                        UfValue res = uf_val_string_take(vm->rt, buf, la + lb);
                        stack_top -= 2;
                        *stack_top++ = res;
                    } else if (a.kind == UF_VAL_ARRAY && b.kind == UF_VAL_ARRAY) {
                        SYNC_VM();
                        size_t na = a.as.array->count;
                        size_t nb = b.as.array->count;
                        UfValue arr = uf_val_array(vm->rt, na + nb);
                        for (size_t i = 0; i < na; ++i) {
                            uf_array_push(vm->rt, arr.as.array, a.as.array->elements[i]);
                        }
                        for (size_t i = 0; i < nb; ++i) {
                            uf_array_push(vm->rt, arr.as.array, b.as.array->elements[i]);
                        }
                        stack_top -= 2;
                        *stack_top++ = arr;
                    } else {
                        VM_RUNTIME_ERROR("Invalid operands to '+' (%s and %s)",
                                uf_val_type_name(a), uf_val_type_name(b));
                    }
                }
                break;
            }
            case OP_SUB: {
                UfValue* b_ptr = stack_top - 1;
                UfValue* a_ptr = stack_top - 2;
                if (a_ptr->kind == UF_VAL_NUMBER && b_ptr->kind == UF_VAL_NUMBER) {
                    a_ptr->as.number -= b_ptr->as.number;
                    stack_top--;
                } else {
                    VM_RUNTIME_ERROR("Operands to '-' must be numbers");
                }
                break;
            }
            case OP_MUL: {
                UfValue* b_ptr = stack_top - 1;
                UfValue* a_ptr = stack_top - 2;
                if (a_ptr->kind == UF_VAL_NUMBER && b_ptr->kind == UF_VAL_NUMBER) {
                    a_ptr->as.number *= b_ptr->as.number;
                    stack_top--;
                } else {
                    VM_RUNTIME_ERROR("Operands to '*' must be numbers");
                }
                break;
            }
            case OP_DIV: {
                UfValue* b_ptr = stack_top - 1;
                UfValue* a_ptr = stack_top - 2;
                if (a_ptr->kind != UF_VAL_NUMBER || b_ptr->kind != UF_VAL_NUMBER) {
                    VM_RUNTIME_ERROR("Operands to '/' must be numbers");
                }
                if (b_ptr->as.number == 0.0) {
                    VM_RUNTIME_ERROR("Division by zero");
                }
                a_ptr->as.number /= b_ptr->as.number;
                stack_top--;
                break;
            }
            case OP_MOD: {
                UfValue* b_ptr = stack_top - 1;
                UfValue* a_ptr = stack_top - 2;
                if (a_ptr->kind != UF_VAL_NUMBER || b_ptr->kind != UF_VAL_NUMBER) {
                    VM_RUNTIME_ERROR("Operands to '%%' must be numbers");
                }
                if (b_ptr->as.number == 0.0) {
                    VM_RUNTIME_ERROR("Division by zero in modulo");
                }
                a_ptr->as.number = fmod(a_ptr->as.number, b_ptr->as.number);
                stack_top--;
                break;
            }
            case OP_NEG: {
                UfValue* v = stack_top - 1;
                if (v->kind != UF_VAL_NUMBER) {
                    VM_RUNTIME_ERROR("Operand to '-' must be a number");
                }
                v->as.number = -v->as.number;
                break;
            }
            case OP_NOT: {
                UfValue* v = stack_top - 1;
                bool truth = uf_val_is_truthy(*v);
                v->kind = UF_VAL_BOOL;
                v->as.boolean = !truth;
                break;
            }
            case OP_EQ: {
                UfValue b = POP();
                UfValue a = POP();
                PUSH(uf_val_bool(uf_val_equal(a, b)));
                break;
            }
            case OP_NEQ: {
                UfValue b = POP();
                UfValue a = POP();
                PUSH(uf_val_bool(!uf_val_equal(a, b)));
                break;
            }
            case OP_LT: {
                UfValue* b_ptr = stack_top - 1;
                UfValue* a_ptr = stack_top - 2;
                if (a_ptr->kind == UF_VAL_NUMBER && b_ptr->kind == UF_VAL_NUMBER) {
                    bool res = a_ptr->as.number < b_ptr->as.number;
                    stack_top--;
                    a_ptr->kind = UF_VAL_BOOL;
                    a_ptr->as.boolean = res;
                } else if (a_ptr->kind == UF_VAL_STRING && b_ptr->kind == UF_VAL_STRING) {
                    bool res = strcmp(a_ptr->as.string->chars, b_ptr->as.string->chars) < 0;
                    stack_top--;
                    a_ptr->kind = UF_VAL_BOOL;
                    a_ptr->as.boolean = res;
                } else {
                    VM_RUNTIME_ERROR("Operands to '<' must be comparable");
                }
                break;
            }
            case OP_LTE: {
                UfValue* b_ptr = stack_top - 1;
                UfValue* a_ptr = stack_top - 2;
                if (a_ptr->kind == UF_VAL_NUMBER && b_ptr->kind == UF_VAL_NUMBER) {
                    bool res = a_ptr->as.number <= b_ptr->as.number;
                    stack_top--;
                    a_ptr->kind = UF_VAL_BOOL;
                    a_ptr->as.boolean = res;
                } else if (a_ptr->kind == UF_VAL_STRING && b_ptr->kind == UF_VAL_STRING) {
                    bool res = strcmp(a_ptr->as.string->chars, b_ptr->as.string->chars) <= 0;
                    stack_top--;
                    a_ptr->kind = UF_VAL_BOOL;
                    a_ptr->as.boolean = res;
                } else {
                    VM_RUNTIME_ERROR("Operands to '<=' must be comparable");
                }
                break;
            }
            case OP_GT: {
                UfValue* b_ptr = stack_top - 1;
                UfValue* a_ptr = stack_top - 2;
                if (a_ptr->kind == UF_VAL_NUMBER && b_ptr->kind == UF_VAL_NUMBER) {
                    bool res = a_ptr->as.number > b_ptr->as.number;
                    stack_top--;
                    a_ptr->kind = UF_VAL_BOOL;
                    a_ptr->as.boolean = res;
                } else if (a_ptr->kind == UF_VAL_STRING && b_ptr->kind == UF_VAL_STRING) {
                    bool res = strcmp(a_ptr->as.string->chars, b_ptr->as.string->chars) > 0;
                    stack_top--;
                    a_ptr->kind = UF_VAL_BOOL;
                    a_ptr->as.boolean = res;
                } else {
                    VM_RUNTIME_ERROR("Operands to '>' must be comparable");
                }
                break;
            }
            case OP_GTE: {
                UfValue* b_ptr = stack_top - 1;
                UfValue* a_ptr = stack_top - 2;
                if (a_ptr->kind == UF_VAL_NUMBER && b_ptr->kind == UF_VAL_NUMBER) {
                    bool res = a_ptr->as.number >= b_ptr->as.number;
                    stack_top--;
                    a_ptr->kind = UF_VAL_BOOL;
                    a_ptr->as.boolean = res;
                } else if (a_ptr->kind == UF_VAL_STRING && b_ptr->kind == UF_VAL_STRING) {
                    bool res = strcmp(a_ptr->as.string->chars, b_ptr->as.string->chars) >= 0;
                    stack_top--;
                    a_ptr->kind = UF_VAL_BOOL;
                    a_ptr->as.boolean = res;
                } else {
                    VM_RUNTIME_ERROR("Operands to '>=' must be comparable");
                }
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_U16();
                ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_U16();
                if (!uf_val_is_truthy(PEEK(0))) {
                    ip += offset;
                }
                break;
            }
            case OP_JUMP_IF_ARG: {
                uint8_t arg_idx = READ_BYTE();
                uint16_t offset = READ_U16();
                if (frame->argc > arg_idx) {
                    ip += offset;
                }
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_U16();
                ip -= offset;
                break;
            }
            case OP_CALL: {
                uint8_t argc = READ_BYTE();
                SYNC_VM();
                if (!call_value(vm, PEEK(argc), argc)) {
                    ERROR_RETURN();
                }
                frame = &vm->frames[vm->frame_count - 1];
                RESTORE_VM();
                break;
            }
            case OP_CALL_SPREAD: {
                UfValue args_val = POP();
                if (args_val.kind != UF_VAL_ARRAY) {
                    SYNC_VM();
                    VM_RUNTIME_ERROR("Spread operand in function call must be an array, got '%s'", uf_val_type_name(args_val));
                }
                UfArrayObject* a = args_val.as.array;
                size_t argc = a->count;
                if (stack_top + argc >= vm->stack + UF_VM_STACK_MAX) {
                    SYNC_VM();
                    VM_RUNTIME_ERROR("Stack overflow calling function with spread arguments");
                }
                for (size_t i = 0; i < argc; ++i) {
                    *stack_top++ = a->elements[i];
                }
                UfValue callee = *(stack_top - argc - 1);
                SYNC_VM();
                if (!call_value(vm, callee, argc)) {
                    ERROR_RETURN();
                }
                frame = &vm->frames[vm->frame_count - 1];
                RESTORE_VM();
                break;
            }
            case OP_RETURN: {
                while (vm->handler_count > 0 && vm->handlers[vm->handler_count - 1].frame_index >= vm->frame_count - 1) {
                    vm->handler_count--;
                    if (vm->rt && vm->rt->try_handler_count > 0) vm->rt->try_handler_count--;
                }
                UfValue result = POP();
                if (frame->closure && frame->closure->function && frame->closure->function->is_async) {
                    UfPromiseObject* p = uf_promise_create(vm->rt);
                    uf_promise_resolve(vm->rt, p, result);
                    result = uf_val_promise(vm->rt, p);
                }
                SYNC_VM();
                close_upvalues(vm, frame->slots);
                if (vm->rt && vm->rt->profiler) {
                    const char* name = (frame->closure && frame->closure->function && frame->closure->function->name)
                        ? frame->closure->function->name : "<anonymous>";
                    uf_profiler_exit(vm->rt->profiler, name);
                }
                vm->frame_count--;
                stack_top = frame->slots;
                if (vm->frame_count == target_frame_count) {
                    vm->stack_top = stack_top;
                    frame->ip = ip;
                    return result;
                }

                *stack_top++ = result;
                frame = &vm->frames[vm->frame_count - 1];
                ip = frame->ip;
                break;
            }
            case OP_BUILD_ARRAY: {
                uint16_t count = READ_U16();
                SYNC_VM();
                UfValue arr = uf_val_array(vm->rt, count);
                for (size_t i = 0; i < count; ++i) {
                    UfValue elem = stack_top[-count + i];
                    uf_array_push(vm->rt, arr.as.array, elem);
                }
                stack_top -= count;
                PUSH(arr);
                break;
            }
            case OP_ARRAY_PUSH: {
                UfValue item = POP();
                UfValue arr = PEEK(0);
                SYNC_VM();
                uf_array_push(vm->rt, arr.as.array, item);
                break;
            }
            case OP_ARRAY_EXTEND: {
                UfValue other = POP();
                UfValue arr = PEEK(0);
                if (other.kind != UF_VAL_ARRAY) {
                    SYNC_VM();
                    VM_RUNTIME_ERROR("Spread operand in array literal must be an array, got '%s'", uf_val_type_name(other));
                }
                SYNC_VM();
                for (size_t i = 0; i < other.as.array->count; ++i) {
                    uf_array_push(vm->rt, arr.as.array, other.as.array->elements[i]);
                }
                break;
            }
            case OP_ARRAY_SLICE: {
                uint16_t start_idx = READ_U16();
                UfValue arr_val = POP();
                if (arr_val.kind != UF_VAL_ARRAY) {
                    SYNC_VM();
                    VM_RUNTIME_ERROR("TypeError: Cannot destructure non-array value of type '%s'", uf_val_type_name(arr_val));
                }
                UfArrayObject* arr = arr_val.as.array;
                size_t rest_len = arr->count >= start_idx ? arr->count - start_idx : 0;
                SYNC_VM();
                UfValue res = uf_val_array(vm->rt, rest_len);
                for (size_t i = 0; i < rest_len; ++i) {
                    uf_array_push(vm->rt, res.as.array, arr->elements[start_idx + i]);
                }
                PUSH(res);
                break;
            }
            case OP_ASSERT_ARRAY: {
                UfValue val = POP();
                if (val.kind != UF_VAL_ARRAY) {
                    SYNC_VM();
                    VM_RUNTIME_ERROR("TypeError: Cannot destructure non-array value of type '%s'", uf_val_type_name(val));
                }
                break;
            }
            case OP_ASSERT_MAP: {
                UfValue val = POP();
                if (val.kind != UF_VAL_MAP && val.kind != UF_VAL_INSTANCE) {
                    SYNC_VM();
                    VM_RUNTIME_ERROR("TypeError: Cannot destructure non-map value of type '%s'", uf_val_type_name(val));
                }
                break;
            }
            case OP_ARRAY_GET_SAFE: {
                uint16_t idx = READ_U16();
                UfValue val = POP();
                if (val.kind != UF_VAL_ARRAY) {
                    SYNC_VM();
                    VM_RUNTIME_ERROR("TypeError: Cannot destructure non-array value of type '%s'", uf_val_type_name(val));
                }
                UfArrayObject* arr = val.as.array;
                if (idx < arr->count) {
                    PUSH(arr->elements[idx]);
                } else {
                    PUSH(uf_val_null());
                }
                break;
            }
            case OP_MAP_GET_SAFE: {
                UfValue key = POP();
                UfValue target = POP();
                if (target.kind == UF_VAL_MAP) {
                    PUSH(uf_map_get(target.as.map, key));
                } else if (target.kind == UF_VAL_INSTANCE) {
                    const char* fname = (key.kind == UF_VAL_STRING) ? key.as.string->chars : "";
                    UfInstanceObject* inst = target.as.instance;
                    bool found = false;
                    for (size_t f = 0; f < inst->field_count; ++f) {
                        if (inst->def && strcmp(inst->def->field_names[f], fname) == 0) {
                            PUSH(inst->fields[f]);
                            found = true;
                            break;
                        }
                    }
                    if (!found) PUSH(uf_val_null());
                } else {
                    PUSH(uf_val_null());
                }
                break;
            }
            case OP_MAP_REST: {
                uint16_t exclude_count = READ_U16();
                uint16_t* k_indices = NULL;
                if (exclude_count > 0) {
                    k_indices = (uint16_t*)malloc(sizeof(uint16_t) * exclude_count);
                    for (size_t k = 0; k < exclude_count; ++k) {
                        k_indices[k] = READ_U16();
                    }
                }
                UfValue target = POP();
                SYNC_VM();
                UfValue rest_map = uf_val_map(vm->rt, 0);
                if (target.kind == UF_VAL_MAP) {
                    UfMapObject* m = target.as.map;
                    for (size_t i = 0; i < m->order_count; ++i) {
                        UfValue k = m->order_keys[i];
                        if (k.kind == UF_VAL_STRING) {
                            bool excluded = false;
                            for (size_t j = 0; j < exclude_count; ++j) {
                                UfValue ex_k = READ_CONSTANT(k_indices[j]);
                                if (ex_k.kind == UF_VAL_STRING && strcmp(k.as.string->chars, ex_k.as.string->chars) == 0) {
                                    excluded = true;
                                    break;
                                }
                            }
                            if (!excluded) {
                                uf_map_set(vm->rt, rest_map.as.map, k, uf_map_get(m, k));
                            }
                        }
                    }
                }
                if (k_indices) free(k_indices);
                PUSH(rest_map);
                break;
            }
            case OP_BUILD_MAP: {
                uint16_t count = READ_U16();
                SYNC_VM();
                UfValue map = uf_val_map(vm->rt, count);
                for (size_t i = 0; i < count; ++i) {
                    UfValue key = stack_top[-count * 2 + i * 2];
                    UfValue val = stack_top[-count * 2 + i * 2 + 1];
                    if (key.kind != UF_VAL_STRING && key.kind != UF_VAL_NUMBER && key.kind != UF_VAL_BOOL && key.kind != UF_VAL_NULL) {
                        VM_RUNTIME_ERROR("Map key must be a string or number, got '%s'", uf_val_type_name(key));
                    }
                    uf_map_set(vm->rt, map.as.map, key, val);
                }
                stack_top -= count * 2;
                PUSH(map);
                break;
            }
            case OP_MAP_SET: {
                UfValue val = POP();
                UfValue key = POP();
                UfValue map = PEEK(0);
                if (key.kind != UF_VAL_STRING && key.kind != UF_VAL_NUMBER && key.kind != UF_VAL_BOOL && key.kind != UF_VAL_NULL) {
                    SYNC_VM();
                    VM_RUNTIME_ERROR("Map key must be a string or number, got '%s'", uf_val_type_name(key));
                }
                SYNC_VM();
                uf_map_set(vm->rt, map.as.map, key, val);
                break;
            }
            case OP_MAP_EXTEND: {
                UfValue other = POP();
                UfValue map = PEEK(0);
                if (other.kind != UF_VAL_MAP) {
                    SYNC_VM();
                    VM_RUNTIME_ERROR("Spread operand in map literal must be a map, got '%s'", uf_val_type_name(other));
                }
                SYNC_VM();
                for (size_t i = 0; i < other.as.map->order_count; ++i) {
                    UfValue k = other.as.map->order_keys[i];
                    UfValue v = uf_map_get(other.as.map, k);
                    uf_map_set(vm->rt, map.as.map, k, v);
                }
                break;
            }
            case OP_INDEX_GET: {
                UfValue index = POP();
                UfValue target = POP();

                if (target.kind == UF_VAL_ARRAY) {
                    if (index.kind != UF_VAL_NUMBER) {
                        VM_RUNTIME_ERROR("Array index must be a number");
                    }
                    long idx = (long)index.as.number;
                    if (idx < 0) idx += target.as.array->count;
                    if (idx < 0 || (size_t)idx >= target.as.array->count) {
                        VM_RUNTIME_ERROR("IndexOutOfBounds: Index %ld out of bounds for array of length %zu", idx, target.as.array->count);
                    }
                    PUSH(uf_array_get(target.as.array, (size_t)idx));
                } else if (target.kind == UF_VAL_MAP) {
                    PUSH(uf_map_get(target.as.map, index));
                } else if (target.kind == UF_VAL_STRING) {
                    if (index.kind != UF_VAL_NUMBER) {
                        VM_RUNTIME_ERROR("String index must be a number");
                    }
                    long idx = (long)index.as.number;
                    if (idx < 0) idx += target.as.string->length;
                    if (idx < 0 || (size_t)idx >= target.as.string->length) {
                        VM_RUNTIME_ERROR("String index out of bounds");
                    }
                    char ch_buf[2] = { target.as.string->chars[idx], '\0' };
                    SYNC_VM();
                    PUSH(uf_val_string_cstr(vm->rt, ch_buf));
                } else if (target.kind == UF_VAL_INSTANCE) {
                    if (index.kind != UF_VAL_STRING) {
                        VM_RUNTIME_ERROR("Instance field must be a string");
                    }
                    const char* fname = index.as.string->chars;
                    UfInstanceObject* inst = target.as.instance;
                    bool found = false;
                    for (size_t i = 0; i < inst->field_count; ++i) {
                        if (strcmp(inst->def->field_names[i], fname) == 0) {
                            PUSH(inst->fields[i]);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        for (size_t i = 0; i < inst->def->method_count; ++i) {
                            if (strcmp(inst->def->method_names[i], fname) == 0) {
                                PUSH(uf_val_bound_method(vm->rt, target, inst->def->method_values[i]));
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found) {
                        VM_RUNTIME_ERROR("Field '%s' not found on struct '%s'",
                                fname, inst->def->name);
                    }
                } else if (target.kind == UF_VAL_MODULE) {
                    if (index.kind != UF_VAL_STRING) {
                        VM_RUNTIME_ERROR("Module member access expects a string name");
                    }
                    UfValue res = uf_map_get(target.as.module->exports.as.map, index);
                    if (res.kind == UF_VAL_NULL && !uf_map_has(target.as.module->exports.as.map, index)) {
                        VM_RUNTIME_ERROR("Module '%s' has no exported member '%s'",
                                (target.as.module && target.as.module->name) ? target.as.module->name : "anonymous",
                                index.as.string->chars);
                    }
                    PUSH(res);
                } else if (target.kind == UF_VAL_ERROR) {
                    if (index.kind != UF_VAL_STRING) {
                        VM_RUNTIME_ERROR("Error property access expects string");
                    }
                    const char* prop = index.as.string->chars;
                    UfErrorObject* err = target.as.error;
                    if (strcmp(prop, "kind") == 0) {
                        PUSH(uf_val_string(vm->rt, err->kind ? err->kind->chars : "Error", err->kind ? err->kind->length : 5));
                    } else if (strcmp(prop, "message") == 0) {
                        PUSH(uf_val_string(vm->rt, err->message ? err->message->chars : "", err->message ? err->message->length : 0));
                    } else if (strcmp(prop, "line") == 0) {
                        PUSH(uf_val_number((double)err->line));
                    } else if (strcmp(prop, "file") == 0) {
                        PUSH(uf_val_string_cstr(vm->rt, err->file ? err->file : "<unknown>"));
                    } else {
                        PUSH(uf_val_null());
                    }
                } else if (target.kind == UF_VAL_ENUM_DEF) {
                    if (index.kind != UF_VAL_STRING) {
                        VM_RUNTIME_ERROR("Enum member access expects a string variant name");
                    }
                    const char* vname = index.as.string->chars;
                    UfEnumDefObject* def = target.as.enum_def;
                    bool found = false;
                    for (size_t i = 0; i < def->variant_count; ++i) {
                        if (strcmp(def->variant_names[i], vname) == 0) {
                            found = true;
                            SYNC_VM();
                            PUSH(uf_val_enum_val(vm->rt, def, (int)i, def->variant_names[i], NULL, 0));
                            break;
                        }
                    }
                    if (!found) {
                        VM_RUNTIME_ERROR("Enum '%s' has no variant '%s'", def->name ? def->name : "", vname);
                    }
                } else if (target.kind == UF_VAL_ENUM_VAL) {
                    UfEnumValObject* ev = target.as.enum_val;
                    if (index.kind == UF_VAL_NUMBER) {
                        long idx = (long)index.as.number;
                        if (idx >= 0 && (size_t)idx < ev->field_count) {
                            PUSH(ev->fields[idx]);
                        } else {
                            PUSH(uf_val_null());
                        }
                    } else if (index.kind == UF_VAL_STRING) {
                        const char* prop = index.as.string->chars;
                        if (strcmp(prop, "tag") == 0) {
                            PUSH(uf_val_number((double)ev->tag));
                        } else if (strcmp(prop, "name") == 0) {
                            SYNC_VM();
                            PUSH(uf_val_string_cstr(vm->rt, ev->variant_name ? ev->variant_name : ""));
                        } else {
                            bool found = false;
                            if (ev->def && (size_t)ev->tag < ev->def->variant_count && ev->def->variant_field_names) {
                                const char** fnames = ev->def->variant_field_names[ev->tag];
                                if (fnames) {
                                    for (size_t f = 0; f < ev->field_count; ++f) {
                                        if (strcmp(fnames[f], prop) == 0) {
                                            PUSH(ev->fields[f]);
                                            found = true;
                                            break;
                                        }
                                    }
                                }
                            }
                            if (!found) {
                                VM_RUNTIME_ERROR("Enum variant '%s' has no field '%s'", ev->variant_name ? ev->variant_name : "", prop);
                            }
                        }
                    } else {
                        VM_RUNTIME_ERROR("Enum property access expects string or index");
                    }
                } else {
                    VM_RUNTIME_ERROR("Cannot index type '%s'", uf_val_type_name(target));
                }
                break;
            }
            case OP_INDEX_SET: {
                UfValue val = POP();
                UfValue index = POP();
                UfValue target = POP();

                if (target.kind == UF_VAL_ARRAY) {
                    if (index.kind != UF_VAL_NUMBER) {
                        VM_RUNTIME_ERROR("Array index must be a number");
                    }
                    long idx = (long)index.as.number;
                    if (idx < 0) idx += target.as.array->count;
                    if (idx < 0 || (size_t)idx >= target.as.array->count) {
                        VM_RUNTIME_ERROR("Array index out of bounds");
                    }
                    uf_array_set(target.as.array, (size_t)idx, val);
                } else if (target.kind == UF_VAL_MAP) {
                    if (index.kind != UF_VAL_STRING && index.kind != UF_VAL_NUMBER && index.kind != UF_VAL_BOOL && index.kind != UF_VAL_NULL) {
                        VM_RUNTIME_ERROR("Map key must be a string or number, got '%s'", uf_val_type_name(index));
                    }
                    uf_map_set(vm->rt, target.as.map, index, val);
                } else if (target.kind == UF_VAL_INSTANCE) {
                    if (index.kind != UF_VAL_STRING) {
                        VM_RUNTIME_ERROR("Instance field must be a string");
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
                        VM_RUNTIME_ERROR("Field '%s' not found on struct '%s'",
                                fname, inst->def->name);
                    }
                } else {
                    VM_RUNTIME_ERROR("Cannot index-assign type '%s'", uf_val_type_name(target));
                }
                break;
            }
            case OP_SAY: {
                UfValue val = POP();
                char* str = uf_val_to_string(val);
                fprintf(vm->rt->out_stream, "%s\n", str ? str : "null");
                free(str);
                fflush(vm->rt->out_stream);
                break;
            }
            case OP_STRUCT_DEF: {
                uint16_t const_idx = READ_U16();
                PUSH(READ_CONSTANT(const_idx));
                break;
            }
            case OP_MATCH_SHAPE: {
                uint8_t shape = READ_BYTE();
                uint16_t count = READ_U16();
                UfValue target = POP();
                bool ok = false;
                switch (shape) {
                    case UF_MATCH_ARRAY_EXACT:
                        ok = target.kind == UF_VAL_ARRAY && target.as.array->count == count;
                        break;
                    case UF_MATCH_ARRAY_AT_LEAST:
                        ok = target.kind == UF_VAL_ARRAY && target.as.array->count >= count;
                        break;
                    case UF_MATCH_MAP:
                        ok = target.kind == UF_VAL_MAP || target.kind == UF_VAL_INSTANCE;
                        break;
                    case UF_MATCH_FIELD_COUNT:
                        if (target.kind == UF_VAL_INSTANCE && target.as.instance) {
                            ok = target.as.instance->field_count == count;
                        } else if (target.kind == UF_VAL_ENUM_VAL && target.as.enum_val) {
                            ok = target.as.enum_val->field_count == count;
                        }
                        break;
                    default:
                        break;
                }
                PUSH(uf_val_bool(ok));
                break;
            }
            case OP_MATCH_FIELD: {
                uint16_t idx = READ_U16();
                UfValue target = POP();
                UfValue field = uf_val_null();
                if (target.kind == UF_VAL_INSTANCE && target.as.instance && idx < target.as.instance->field_count) {
                    field = target.as.instance->fields[idx];
                } else if (target.kind == UF_VAL_ENUM_VAL && target.as.enum_val && idx < target.as.enum_val->field_count) {
                    field = target.as.enum_val->fields[idx];
                }
                PUSH(field);
                break;
            }
            case OP_INSTANCE: {
                uint16_t c_idx = READ_U16();
                const char* sname = READ_CONSTANT(c_idx).as.string->chars;
                UfValue target = POP();
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
                PUSH(uf_val_bool(matches));
                break;
            }
            case OP_ITER_GET: {
                UfValue index = POP();
                UfValue target = POP();
                if (index.kind != UF_VAL_NUMBER) {
                    VM_RUNTIME_ERROR("Iterator index must be a number");
                }
                long idx = (long)index.as.number;
                if (target.kind == UF_VAL_ARRAY) {
                    if (idx < 0 || (size_t)idx >= target.as.array->count) {
                        VM_RUNTIME_ERROR("Array index out of bounds");
                    }
                    PUSH(target.as.array->elements[idx]);
                } else if (target.kind == UF_VAL_MAP) {
                    if (idx < 0 || (size_t)idx >= target.as.map->order_count) {
                        VM_RUNTIME_ERROR("Map index out of bounds");
                    }
                    PUSH(target.as.map->order_keys[idx]);
                } else if (target.kind == UF_VAL_STRING) {
                    if (idx < 0 || (size_t)idx >= target.as.string->length) {
                        VM_RUNTIME_ERROR("String index out of bounds");
                    }
                    char ch_buf[2] = { target.as.string->chars[idx], '\0' };
                    PUSH(uf_val_string_cstr(vm->rt, ch_buf));
                } else {
                    VM_RUNTIME_ERROR("Cannot iterate type '%s'", uf_val_type_name(target));
                }
                break;
            }
            case OP_PUSH_TRY: {
                uint16_t catch_offset = READ_U16();
                SYNC_VM();
                if (vm->handler_count >= UF_VM_HANDLERS_MAX || vm->rt->try_handler_count >= UF_MAX_TRY_HANDLERS) {
                    VM_RUNTIME_ERROR("Maximum nested try-catch handlers exceeded");
                }
                int h_idx = vm->handler_count++;
                vm->handlers[h_idx].frame_index = vm->frame_count - 1;
                vm->handlers[h_idx].catch_ip = ip + catch_offset;
                vm->handlers[h_idx].stack_top = stack_top;

                UfTryHandler* th = &vm->rt->try_handlers[vm->rt->try_handler_count++];
                th->frame_count = vm->rt->frame_count;
                th->temp_root_count = vm->rt->temp_root_count;
                th->scope_env = vm->rt->current_env;

                if (setjmp(th->jmp) != 0) {
                    /* Unwind from runtime error to active handler */
                    UfVMHandler* cur_h = &vm->handlers[vm->handler_count - 1];
                    UfValue err = vm->rt->current_error;
                    while (vm->frame_count - 1 > cur_h->frame_index) {
                        close_upvalues(vm, vm->frames[vm->frame_count - 1].slots);
                        vm->frame_count--;
                    }
                    frame = &vm->frames[cur_h->frame_index];
                    close_upvalues(vm, cur_h->stack_top);
                    stack_top = cur_h->stack_top;
                    *stack_top++ = err;
                    ip = cur_h->catch_ip;
                    vm->handler_count--;
                    vm->had_error = false;
                    vm->rt->had_runtime_error = false;
                    vm->rt->current_error = uf_val_null();
                }
                break;
            }
            case OP_POP_TRY: {
                if (vm->handler_count > 0) vm->handler_count--;
                if (vm->rt && vm->rt->try_handler_count > 0) vm->rt->try_handler_count--;
                break;
            }
            case OP_RETHROW: {
                UfValue err = POP();
                SYNC_VM();
                if (vm->handler_count > 0) {
                    UfVMHandler* cur_h = &vm->handlers[vm->handler_count - 1];
                    while (vm->frame_count - 1 > cur_h->frame_index) {
                        close_upvalues(vm, vm->frames[vm->frame_count - 1].slots);
                        vm->frame_count--;
                    }
                    frame = &vm->frames[cur_h->frame_index];
                    close_upvalues(vm, cur_h->stack_top);
                    stack_top = cur_h->stack_top;
                    *stack_top++ = err;
                    ip = cur_h->catch_ip;
                    vm->handler_count--;
                    vm->had_error = false;
                    vm->rt->had_runtime_error = false;
                    vm->rt->current_error = uf_val_null();
                } else {
                    vm->rt->had_runtime_error = true;
                    vm->rt->current_error = err;
                    if (err.kind == UF_VAL_ERROR && err.as.error) {
                        const char* msg = err.as.error->message ? err.as.error->message->chars : "Error";
                        SourceSpan sp = {
                            .start = { .line = (uint32_t)err.as.error->line, .col = 1, .file = err.as.error->file },
                            .end = { .line = (uint32_t)err.as.error->line, .col = 1, .file = err.as.error->file }
                        };
                        if (vm->rt->reporter) {
                            uf_report_diag(vm->rt->reporter, UF_DIAG_RUNTIME_ERROR, sp, msg, NULL);
                        } else {
                            fprintf(vm->rt->err_stream, "Runtime Error: %s\n", msg);
                        }
                    } else {
                        char* err_str = uf_val_to_string(err);
                        fprintf(vm->rt->err_stream, "Runtime Error: %s\n", err_str ? err_str : "Error");
                        free(err_str);
                    }
                    ERROR_RETURN();
                }
                break;
            }
            case OP_AWAIT: {
                UfValue val = POP();
                SYNC_VM();
                if (val.kind == UF_VAL_PROMISE && val.as.promise) {
                    val = uf_promise_await(vm->rt, val.as.promise);
                }
                RESTORE_VM();
                PUSH(val);
                break;
            }
            default:
                VM_RUNTIME_ERROR("Unknown opcode %d", instruction);
        }
    }

#undef READ_BYTE
#undef READ_U16
#undef READ_CONSTANT
#undef PUSH
#undef POP
#undef PEEK
#undef SYNC_VM
#undef RESTORE_VM
#undef ERROR_RETURN
}

UfInterpretResult uf_vm_run(UfVM* vm, UfBytecodeFunction* function) {
    if (!function || !vm) return UF_INTERPRET_RUNTIME_ERROR;

    UfClosureObject* root_closure = uf_closure_new(vm->rt, function);
    uf_vm_push(vm, uf_val_closure(vm->rt, root_closure));

    UfVMFrame* frame = &vm->frames[vm->frame_count++];
    frame->closure = root_closure;
    frame->ip = function->chunk.code;
    frame->slots = vm->stack;

    run_vm_frames(vm, 0);
    if (vm->had_error || (vm->rt && vm->rt->had_runtime_error)) {
        return UF_INTERPRET_RUNTIME_ERROR;
    }
    return UF_INTERPRET_OK;
}

UfValue uf_vm_run_closure(UfVM* vm, UfClosureObject* closure, size_t argc, UfValue* args) {
    if (!closure || !vm) return uf_val_null();

    int target = vm->frame_count;
    UfValue* frame_slots = vm->stack_top;

    uf_vm_push(vm, uf_val_closure(vm->rt, closure));
    for (size_t i = 0; i < argc; ++i) {
        uf_vm_push(vm, args[i]);
    }

    UfVMFrame* frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = frame_slots;

    return run_vm_frames(vm, target);
}
