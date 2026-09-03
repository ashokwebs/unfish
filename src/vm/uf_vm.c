#include "uf_vm.h"
#include "../runtime/uf_env.h"
#include "../runtime/uf_module.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void uf_vm_init(UfVM* vm, UfRuntime* rt) {
    vm->frame_count = 0;
    vm->stack_top = vm->stack;
    vm->open_upvalues = NULL;
    vm->handler_count = 0;
    vm->rt = rt;
    vm->had_error = false;
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
}

void uf_vm_push(UfVM* vm, UfValue value) {
    *vm->stack_top++ = value;
}

UfValue uf_vm_pop(UfVM* vm) {
    return *--vm->stack_top;
}

UfValue uf_vm_peek(UfVM* vm, int distance) {
    return vm->stack_top[-1 - distance];
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

static bool call_value(UfVM* vm, UfValue callee, uint8_t argc) {
    if (callee.kind == UF_VAL_CLOSURE) {
        UfClosureObject* cl = callee.as.closure;
        if (argc != cl->function->arity) {
            fprintf(stderr, "Runtime Error: Function '%s' expects %zu arguments, but %u provided\n",
                    cl->function->name ? cl->function->name : "anonymous", cl->function->arity, argc);
            vm->had_error = true;
            return false;
        }

        if (vm->frame_count >= UF_VM_FRAMES_MAX) {
            fprintf(stderr, "Runtime Error: StackOverflowError: Maximum call stack depth exceeded (%d frames)\n",
                    UF_VM_FRAMES_MAX);
            vm->had_error = true;
            return false;
        }

        UfVMFrame* frame = &vm->frames[vm->frame_count++];
        frame->closure = cl;
        frame->ip = cl->function->chunk.code;
        frame->slots = vm->stack_top - argc - 1;
        return true;
    }

    if (callee.kind == UF_VAL_NATIVE_FN) {
        UfNativeFn fn = callee.as.native_fn.fn;
        int arity = callee.as.native_fn.arity;
        if (arity != -1 && arity != (int)argc) {
            fprintf(stderr, "Runtime Error: Native function '%s' expects %d arguments, but %u provided\n",
                    callee.as.native_fn.name, arity, argc);
            vm->had_error = true;
            return false;
        }

        UfValue res = fn(vm->rt, argc, vm->stack_top - argc);
        vm->stack_top -= argc + 1;
        uf_vm_push(vm, res);
        return true;
    }

    if (callee.kind == UF_VAL_STRUCT_DEF) {
        UfStructDefObject* sdef = callee.as.struct_def;
        if (argc != sdef->field_count) {
            fprintf(stderr, "Runtime Error: Struct '%s' expects %zu fields, but %u provided\n",
                    sdef->name, sdef->field_count, argc);
            vm->had_error = true;
            return false;
        }

        UfValue inst = uf_val_instance(vm->rt, sdef, vm->stack_top - argc, argc);
        vm->stack_top -= argc + 1;
        uf_vm_push(vm, inst);
        return true;
    }

    if (callee.kind == UF_VAL_FUNCTION) {
        /* Tree-walk function called from VM */
        SourceSpan span = (SourceSpan){0};
        UfValue res = uf_runtime_call(vm->rt, callee, argc, vm->stack_top - argc, span);
        vm->stack_top -= argc + 1;
        uf_vm_push(vm, res);
        return true;
    }

    char* c_str = uf_val_to_string(callee);
    fprintf(stderr, "Runtime Error: Attempted to call non-callable value of type '%s' (%s)\n",
            uf_val_type_name(callee), c_str ? c_str : "");
    free(c_str);
    vm->had_error = true;
    return false;
}

static UfValue run_vm_frames(UfVM* vm, int target_frame_count) {
    UfVMFrame* volatile frame = &vm->frames[vm->frame_count - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_U16() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT(idx) (frame->closure->function->chunk.constants[idx])
#define ERROR_RETURN() do { vm->had_error = true; return uf_val_null(); } while (0)

    for (;;) {
        uint8_t instruction = READ_BYTE();
        switch (instruction) {
            case OP_CONSTANT: {
                uint16_t const_idx = READ_U16();
                uf_vm_push(vm, READ_CONSTANT(const_idx));
                break;
            }
            case OP_NULL:
                uf_vm_push(vm, uf_val_null());
                break;
            case OP_TRUE:
                uf_vm_push(vm, uf_val_bool(true));
                break;
            case OP_FALSE:
                uf_vm_push(vm, uf_val_bool(false));
                break;
            case OP_POP:
                uf_vm_pop(vm);
                break;
            case OP_DUP:
                uf_vm_push(vm, uf_vm_peek(vm, 0));
                break;
            case OP_LOAD_LOCAL: {
                uint16_t slot = READ_U16();
                uf_vm_push(vm, frame->slots[slot]);
                break;
            }
            case OP_STORE_LOCAL: {
                uint16_t slot = READ_U16();
                frame->slots[slot] = uf_vm_peek(vm, 0);
                break;
            }
            case OP_LOAD_GLOBAL: {
                uint16_t c_idx = READ_U16();
                const char* name = READ_CONSTANT(c_idx).as.string->chars;
                UfValue val;
                if (uf_env_lookup(vm->rt->global_env, name, &val)) {
                    uf_vm_push(vm, val);
                } else {
                    fprintf(stderr, "Runtime Error: Undefined variable '%s'\n", name);
                    ERROR_RETURN();
                }
                break;
            }
            case OP_STORE_GLOBAL: {
                uint16_t c_idx = READ_U16();
                const char* name = READ_CONSTANT(c_idx).as.string->chars;
                if (!uf_env_assign(vm->rt->global_env, name, uf_vm_peek(vm, 0))) {
                    fprintf(stderr, "Runtime Error: Undefined variable '%s'\n", name);
                    ERROR_RETURN();
                }
                break;
            }
            case OP_DEFINE_GLOBAL: {
                uint16_t c_idx = READ_U16();
                const char* name = READ_CONSTANT(c_idx).as.string->chars;
                uf_env_declare(vm->rt->global_env, name, uf_vm_peek(vm, 0));
                uf_vm_pop(vm);
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                uf_vm_push(vm, *frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = uf_vm_peek(vm, 0);
                break;
            }
            case OP_CLOSURE: {
                uint16_t c_idx = READ_U16();
                UfBytecodeFunction* fn = READ_CONSTANT(c_idx).as.bytecode_fn;
                UfClosureObject* cl = uf_closure_new(vm->rt, fn);
                uf_vm_push(vm, uf_val_closure(vm->rt, cl));

                for (size_t i = 0; i < fn->upvalue_count; ++i) {
                    uint8_t is_local = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (is_local) {
                        cl->upvalues[i] = capture_upvalue(vm, frame->slots + index);
                    } else {
                        cl->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OP_CLOSE_UPVALUE: {
                close_upvalues(vm, vm->stack_top - 1);
                uf_vm_pop(vm);
                break;
            }
            case OP_ADD: {
                UfValue b = uf_vm_pop(vm);
                UfValue a = uf_vm_pop(vm);
                if (a.kind == UF_VAL_NUMBER && b.kind == UF_VAL_NUMBER) {
                    uf_vm_push(vm, uf_val_number(a.as.number + b.as.number));
                } else if (a.kind == UF_VAL_STRING || b.kind == UF_VAL_STRING) {
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
                    uf_vm_push(vm, uf_val_string_take(vm->rt, buf, la + lb));
                } else if (a.kind == UF_VAL_ARRAY && b.kind == UF_VAL_ARRAY) {
                    size_t na = a.as.array->count;
                    size_t nb = b.as.array->count;
                    UfValue arr = uf_val_array(vm->rt, na + nb);
                    for (size_t i = 0; i < na; ++i) {
                        uf_array_push(vm->rt, arr.as.array, a.as.array->elements[i]);
                    }
                    for (size_t i = 0; i < nb; ++i) {
                        uf_array_push(vm->rt, arr.as.array, b.as.array->elements[i]);
                    }
                    uf_vm_push(vm, arr);
                } else {
                    fprintf(stderr, "Runtime Error: Invalid operands to '+' (%s and %s)\n",
                            uf_val_type_name(a), uf_val_type_name(b));
                    ERROR_RETURN();
                }
                break;
            }
            case OP_SUB: {
                UfValue b = uf_vm_pop(vm);
                UfValue a = uf_vm_pop(vm);
                if (a.kind != UF_VAL_NUMBER || b.kind != UF_VAL_NUMBER) {
                    fprintf(stderr, "Runtime Error: Operands to '-' must be numbers\n");
                    ERROR_RETURN();
                }
                uf_vm_push(vm, uf_val_number(a.as.number - b.as.number));
                break;
            }
            case OP_MUL: {
                UfValue b = uf_vm_pop(vm);
                UfValue a = uf_vm_pop(vm);
                if (a.kind != UF_VAL_NUMBER || b.kind != UF_VAL_NUMBER) {
                    fprintf(stderr, "Runtime Error: Operands to '*' must be numbers\n");
                    ERROR_RETURN();
                }
                uf_vm_push(vm, uf_val_number(a.as.number * b.as.number));
                break;
            }
            case OP_DIV: {
                UfValue b = uf_vm_pop(vm);
                UfValue a = uf_vm_pop(vm);
                if (a.kind != UF_VAL_NUMBER || b.kind != UF_VAL_NUMBER) {
                    if (vm->rt) {
                        uf_runtime_error(vm->rt, (SourceSpan){0}, "Operands to '/' must be numbers");
                    } else {
                        fprintf(stderr, "Runtime Error: Operands to '/' must be numbers\n");
                    }
                    ERROR_RETURN();
                }
                if (b.as.number == 0.0) {
                    if (vm->rt) {
                        uf_runtime_error(vm->rt, (SourceSpan){0}, "Division by zero");
                    } else {
                        fprintf(stderr, "Runtime Error: Division by zero\n");
                    }
                    ERROR_RETURN();
                }
                uf_vm_push(vm, uf_val_number(a.as.number / b.as.number));
                break;
            }
            case OP_MOD: {
                UfValue b = uf_vm_pop(vm);
                UfValue a = uf_vm_pop(vm);
                if (a.kind != UF_VAL_NUMBER || b.kind != UF_VAL_NUMBER) {
                    fprintf(stderr, "Runtime Error: Operands to '%%' must be numbers\n");
                    ERROR_RETURN();
                }
                if (b.as.number == 0.0) {
                    fprintf(stderr, "Runtime Error: Division by zero in modulo\n");
                    ERROR_RETURN();
                }
                uf_vm_push(vm, uf_val_number(fmod(a.as.number, b.as.number)));
                break;
            }
            case OP_NEG: {
                UfValue v = uf_vm_pop(vm);
                if (v.kind != UF_VAL_NUMBER) {
                    fprintf(stderr, "Runtime Error: Operand to '-' must be a number\n");
                    ERROR_RETURN();
                }
                uf_vm_push(vm, uf_val_number(-v.as.number));
                break;
            }
            case OP_NOT: {
                UfValue v = uf_vm_pop(vm);
                uf_vm_push(vm, uf_val_bool(!uf_val_is_truthy(v)));
                break;
            }
            case OP_EQ: {
                UfValue b = uf_vm_pop(vm);
                UfValue a = uf_vm_pop(vm);
                uf_vm_push(vm, uf_val_bool(uf_val_equal(a, b)));
                break;
            }
            case OP_NEQ: {
                UfValue b = uf_vm_pop(vm);
                UfValue a = uf_vm_pop(vm);
                uf_vm_push(vm, uf_val_bool(!uf_val_equal(a, b)));
                break;
            }
            case OP_LT: {
                UfValue b = uf_vm_pop(vm);
                UfValue a = uf_vm_pop(vm);
                if (a.kind == UF_VAL_NUMBER && b.kind == UF_VAL_NUMBER) {
                    uf_vm_push(vm, uf_val_bool(a.as.number < b.as.number));
                } else if (a.kind == UF_VAL_STRING && b.kind == UF_VAL_STRING) {
                    uf_vm_push(vm, uf_val_bool(strcmp(a.as.string->chars, b.as.string->chars) < 0));
                } else {
                    fprintf(stderr, "Runtime Error: Operands to '<' must be comparable\n");
                    ERROR_RETURN();
                }
                break;
            }
            case OP_LTE: {
                UfValue b = uf_vm_pop(vm);
                UfValue a = uf_vm_pop(vm);
                if (a.kind == UF_VAL_NUMBER && b.kind == UF_VAL_NUMBER) {
                    uf_vm_push(vm, uf_val_bool(a.as.number <= b.as.number));
                } else if (a.kind == UF_VAL_STRING && b.kind == UF_VAL_STRING) {
                    uf_vm_push(vm, uf_val_bool(strcmp(a.as.string->chars, b.as.string->chars) <= 0));
                } else {
                    fprintf(stderr, "Runtime Error: Operands to '<=' must be comparable\n");
                    ERROR_RETURN();
                }
                break;
            }
            case OP_GT: {
                UfValue b = uf_vm_pop(vm);
                UfValue a = uf_vm_pop(vm);
                if (a.kind == UF_VAL_NUMBER && b.kind == UF_VAL_NUMBER) {
                    uf_vm_push(vm, uf_val_bool(a.as.number > b.as.number));
                } else if (a.kind == UF_VAL_STRING && b.kind == UF_VAL_STRING) {
                    uf_vm_push(vm, uf_val_bool(strcmp(a.as.string->chars, b.as.string->chars) > 0));
                } else {
                    fprintf(stderr, "Runtime Error: Operands to '>' must be comparable\n");
                    ERROR_RETURN();
                }
                break;
            }
            case OP_GTE: {
                UfValue b = uf_vm_pop(vm);
                UfValue a = uf_vm_pop(vm);
                if (a.kind == UF_VAL_NUMBER && b.kind == UF_VAL_NUMBER) {
                    uf_vm_push(vm, uf_val_bool(a.as.number >= b.as.number));
                } else if (a.kind == UF_VAL_STRING && b.kind == UF_VAL_STRING) {
                    uf_vm_push(vm, uf_val_bool(strcmp(a.as.string->chars, b.as.string->chars) >= 0));
                } else {
                    fprintf(stderr, "Runtime Error: Operands to '>=' must be comparable\n");
                    ERROR_RETURN();
                }
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_U16();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_U16();
                if (!uf_val_is_truthy(uf_vm_peek(vm, 0))) {
                    frame->ip += offset;
                }
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_U16();
                frame->ip -= offset;
                break;
            }
            case OP_CALL: {
                uint8_t argc = READ_BYTE();
                if (!call_value(vm, uf_vm_peek(vm, argc), argc)) {
                    ERROR_RETURN();
                }
                frame = &vm->frames[vm->frame_count - 1];
                break;
            }
            case OP_RETURN: {
                UfValue result = uf_vm_pop(vm);
                close_upvalues(vm, frame->slots);
                vm->frame_count--;
                vm->stack_top = frame->slots;
                if (vm->frame_count == target_frame_count) {
                    return result;
                }

                uf_vm_push(vm, result);
                frame = &vm->frames[vm->frame_count - 1];
                break;
            }
            case OP_BUILD_ARRAY: {
                uint16_t count = READ_U16();
                UfValue arr = uf_val_array(vm->rt, count);
                for (size_t i = 0; i < count; ++i) {
                    UfValue elem = vm->stack_top[-count + i];
                    uf_array_push(vm->rt, arr.as.array, elem);
                }
                vm->stack_top -= count;
                uf_vm_push(vm, arr);
                break;
            }
            case OP_BUILD_MAP: {
                uint16_t count = READ_U16();
                UfValue map = uf_val_map(vm->rt, count);
                for (size_t i = 0; i < count; ++i) {
                    UfValue key = vm->stack_top[-count * 2 + i * 2];
                    UfValue val = vm->stack_top[-count * 2 + i * 2 + 1];
                    if (key.kind != UF_VAL_STRING && key.kind != UF_VAL_NUMBER && key.kind != UF_VAL_BOOL && key.kind != UF_VAL_NULL) {
                        fprintf(stderr, "Runtime Error: Map key must be a string or number, got '%s'\n", uf_val_type_name(key));
                        ERROR_RETURN();
                    }
                    uf_map_set(vm->rt, map.as.map, key, val);
                }
                vm->stack_top -= count * 2;
                uf_vm_push(vm, map);
                break;
            }
            case OP_INDEX_GET: {
                UfValue index = uf_vm_pop(vm);
                UfValue target = uf_vm_pop(vm);

                if (target.kind == UF_VAL_ARRAY) {
                    if (index.kind != UF_VAL_NUMBER) {
                        fprintf(stderr, "Runtime Error: Array index must be a number\n");
                        ERROR_RETURN();
                    }
                    long idx = (long)index.as.number;
                    if (idx < 0) idx += target.as.array->count;
                    if (idx < 0 || (size_t)idx >= target.as.array->count) {
                        if (vm->rt) {
                            uf_runtime_error(vm->rt, (SourceSpan){0}, "IndexOutOfBounds: Index %ld out of bounds for array of length %zu", idx, target.as.array->count);
                        } else {
                            fprintf(stderr, "Runtime Error: IndexOutOfBounds: Index %ld out of bounds for array of length %zu\n", idx, target.as.array->count);
                        }
                        ERROR_RETURN();
                    }
                    uf_vm_push(vm, uf_array_get(target.as.array, (size_t)idx));
                } else if (target.kind == UF_VAL_MAP) {
                    uf_vm_push(vm, uf_map_get(target.as.map, index));
                } else if (target.kind == UF_VAL_STRING) {
                    if (index.kind != UF_VAL_NUMBER) {
                        fprintf(stderr, "Runtime Error: String index must be a number\n");
                        ERROR_RETURN();
                    }
                    long idx = (long)index.as.number;
                    if (idx < 0) idx += target.as.string->length;
                    if (idx < 0 || (size_t)idx >= target.as.string->length) {
                        fprintf(stderr, "Runtime Error: String index out of bounds\n");
                        ERROR_RETURN();
                    }
                    char ch_buf[2] = { target.as.string->chars[idx], '\0' };
                    uf_vm_push(vm, uf_val_string_cstr(vm->rt, ch_buf));
                } else if (target.kind == UF_VAL_INSTANCE) {
                    if (index.kind != UF_VAL_STRING) {
                        fprintf(stderr, "Runtime Error: Instance field must be a string\n");
                        ERROR_RETURN();
                    }
                    const char* fname = index.as.string->chars;
                    UfInstanceObject* inst = target.as.instance;
                    bool found = false;
                    for (size_t i = 0; i < inst->field_count; ++i) {
                        if (strcmp(inst->def->field_names[i], fname) == 0) {
                            uf_vm_push(vm, inst->fields[i]);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        fprintf(stderr, "Runtime Error: Field '%s' not found on struct '%s'\n",
                                fname, inst->def->name);
                        ERROR_RETURN();
                    }
                } else if (target.kind == UF_VAL_MODULE) {
                    if (index.kind != UF_VAL_STRING) {
                        fprintf(stderr, "Runtime Error: Module member access expects a string name\n");
                        ERROR_RETURN();
                    }
                    UfValue res = uf_map_get(target.as.module->exports.as.map, index);
                    if (res.kind == UF_VAL_NULL && !uf_map_has(target.as.module->exports.as.map, index)) {
                        fprintf(stderr, "Runtime Error: Module '%s' has no exported member '%s'\n",
                                (target.as.module && target.as.module->name) ? target.as.module->name : "anonymous",
                                index.as.string->chars);
                        ERROR_RETURN();
                    }
                    uf_vm_push(vm, res);
                } else if (target.kind == UF_VAL_ERROR) {
                    if (index.kind != UF_VAL_STRING) {
                        fprintf(stderr, "Runtime Error: Error property access expects string\n");
                        ERROR_RETURN();
                    }
                    const char* prop = index.as.string->chars;
                    UfErrorObject* err = target.as.error;
                    if (strcmp(prop, "kind") == 0) {
                        uf_vm_push(vm, uf_val_string(vm->rt, err->kind ? err->kind->chars : "Error", err->kind ? err->kind->length : 5));
                    } else if (strcmp(prop, "message") == 0) {
                        uf_vm_push(vm, uf_val_string(vm->rt, err->message ? err->message->chars : "", err->message ? err->message->length : 0));
                    } else if (strcmp(prop, "line") == 0) {
                        uf_vm_push(vm, uf_val_number((double)err->line));
                    } else if (strcmp(prop, "file") == 0) {
                        uf_vm_push(vm, uf_val_string_cstr(vm->rt, err->file ? err->file : "<unknown>"));
                    } else {
                        uf_vm_push(vm, uf_val_null());
                    }
                } else {
                    fprintf(stderr, "Runtime Error: Cannot index type '%s'\n", uf_val_type_name(target));
                    ERROR_RETURN();
                }
                break;
            }
            case OP_INDEX_SET: {
                UfValue val = uf_vm_pop(vm);
                UfValue index = uf_vm_pop(vm);
                UfValue target = uf_vm_pop(vm);

                if (target.kind == UF_VAL_ARRAY) {
                    if (index.kind != UF_VAL_NUMBER) {
                        fprintf(stderr, "Runtime Error: Array index must be a number\n");
                        ERROR_RETURN();
                    }
                    long idx = (long)index.as.number;
                    if (idx < 0) idx += target.as.array->count;
                    if (idx < 0 || (size_t)idx >= target.as.array->count) {
                        fprintf(stderr, "Runtime Error: Array index out of bounds\n");
                        ERROR_RETURN();
                    }
                    uf_array_set(target.as.array, (size_t)idx, val);
                } else if (target.kind == UF_VAL_MAP) {
                    if (index.kind != UF_VAL_STRING && index.kind != UF_VAL_NUMBER && index.kind != UF_VAL_BOOL && index.kind != UF_VAL_NULL) {
                        fprintf(stderr, "Runtime Error: Map key must be a string or number, got '%s'\n", uf_val_type_name(index));
                        ERROR_RETURN();
                    }
                    uf_map_set(vm->rt, target.as.map, index, val);
                } else if (target.kind == UF_VAL_INSTANCE) {
                    if (index.kind != UF_VAL_STRING) {
                        fprintf(stderr, "Runtime Error: Instance field must be a string\n");
                        ERROR_RETURN();
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
                        fprintf(stderr, "Runtime Error: Field '%s' not found on struct '%s'\n",
                                fname, inst->def->name);
                        ERROR_RETURN();
                    }
                } else {
                    fprintf(stderr, "Runtime Error: Cannot index-assign type '%s'\n", uf_val_type_name(target));
                    ERROR_RETURN();
                }
                break;
            }
            case OP_SAY: {
                UfValue val = uf_vm_pop(vm);
                char* str = uf_val_to_string(val);
                fprintf(vm->rt->out_stream, "%s\n", str ? str : "null");
                free(str);
                fflush(vm->rt->out_stream);
                break;
            }
            case OP_STRUCT_DEF: {
                uint16_t const_idx = READ_U16();
                uf_vm_push(vm, READ_CONSTANT(const_idx));
                break;
            }
            case OP_INSTANCE: {
                uint16_t c_idx = READ_U16();
                const char* sname = READ_CONSTANT(c_idx).as.string->chars;
                UfValue target = uf_vm_pop(vm);
                bool matches = (target.kind == UF_VAL_INSTANCE &&
                                target.as.instance &&
                                target.as.instance->def &&
                                strcmp(target.as.instance->def->name, sname) == 0);
                uf_vm_push(vm, uf_val_bool(matches));
                break;
            }
            case OP_ITER_GET: {
                UfValue index = uf_vm_pop(vm);
                UfValue target = uf_vm_pop(vm);
                if (index.kind != UF_VAL_NUMBER) {
                    fprintf(stderr, "Runtime Error: Iterator index must be a number\n");
                    ERROR_RETURN();
                }
                long idx = (long)index.as.number;
                if (target.kind == UF_VAL_ARRAY) {
                    if (idx < 0 || (size_t)idx >= target.as.array->count) {
                        fprintf(stderr, "Runtime Error: Array index out of bounds\n");
                        ERROR_RETURN();
                    }
                    uf_vm_push(vm, target.as.array->elements[idx]);
                } else if (target.kind == UF_VAL_MAP) {
                    if (idx < 0 || (size_t)idx >= target.as.map->order_count) {
                        fprintf(stderr, "Runtime Error: Map index out of bounds\n");
                        ERROR_RETURN();
                    }
                    uf_vm_push(vm, target.as.map->order_keys[idx]);
                } else if (target.kind == UF_VAL_STRING) {
                    if (idx < 0 || (size_t)idx >= target.as.string->length) {
                        fprintf(stderr, "Runtime Error: String index out of bounds\n");
                        ERROR_RETURN();
                    }
                    char ch_buf[2] = { target.as.string->chars[idx], '\0' };
                    uf_vm_push(vm, uf_val_string_cstr(vm->rt, ch_buf));
                } else {
                    fprintf(stderr, "Runtime Error: Cannot iterate type '%s'\n", uf_val_type_name(target));
                    ERROR_RETURN();
                }
                break;
            }
            case OP_PUSH_TRY: {
                uint16_t catch_offset = READ_U16();
                if (vm->handler_count >= UF_VM_HANDLERS_MAX || vm->rt->try_handler_count >= UF_MAX_TRY_HANDLERS) {
                    fprintf(stderr, "Runtime Error: Maximum nested try-catch handlers exceeded\n");
                    ERROR_RETURN();
                }
                int h_idx = vm->handler_count++;
                vm->handlers[h_idx].frame_index = vm->frame_count - 1;
                vm->handlers[h_idx].catch_ip = frame->ip + catch_offset;
                vm->handlers[h_idx].stack_top = vm->stack_top;

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
                    vm->stack_top = cur_h->stack_top;
                    uf_vm_push(vm, err);
                    frame->ip = cur_h->catch_ip;
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
            default:
                fprintf(stderr, "Runtime Error: Unknown opcode %d\n", instruction);
                ERROR_RETURN();
        }
    }

#undef READ_BYTE
#undef READ_U16
#undef READ_CONSTANT
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
