#include "uf_interpreter.h"
#include "uf_module.h"
#include "../runtime/uf_fiber.h"
#include "../vm/uf_vm.h"
#include "../vm2/uf_regvm.h"
#include <math.h>

typedef enum {
    EXEC_OK,
    EXEC_RETURN,
    EXEC_BREAK,
    EXEC_CONTINUE,
    EXEC_ERROR
} ExecStatus;

typedef struct {
    ExecStatus status;
    UfValue value;
} ExecResult;

static ExecResult exec_ok(void) {
    ExecResult r;
    r.status = EXEC_OK;
    r.value = uf_val_null();
    return r;
}

static ExecResult exec_return(UfValue val) {
    ExecResult r;
    r.status = EXEC_RETURN;
    r.value = val;
    return r;
}

static ExecResult exec_break(void) {
    ExecResult r;
    r.status = EXEC_BREAK;
    r.value = uf_val_null();
    return r;
}

static ExecResult exec_continue(void) {
    ExecResult r;
    r.status = EXEC_CONTINUE;
    r.value = uf_val_null();
    return r;
}

static ExecResult exec_error(void) {
    ExecResult r;
    r.status = EXEC_ERROR;
    r.value = uf_val_null();
    return r;
}

static ExecResult execute_statement(UfRuntime* rt, UfEnv* env, const UfStmt* stmt);
static ExecResult execute_block(UfRuntime* rt, UfEnv* env, const UfStmt* block_stmt);

UfValue uf_call_value(UfRuntime* rt, UfValue callee, size_t argc, UfValue* args, SourceSpan span) {
    if (rt->had_runtime_error) return uf_val_null();

    UfValue result = uf_val_null();

    if (callee.kind == UF_VAL_FUNCTION) {
        UfFunctionObject* fn = callee.as.function;
        bool arity_ok = false;
        if (fn->has_rest) {
            arity_ok = (argc >= fn->min_param_count);
            if (!arity_ok) {
                uf_runtime_error(rt, span, "Function '%s' expects at least %zu arguments, but %zu provided",
                                 fn->name ? fn->name : "anonymous", fn->min_param_count, argc);
            }
        } else {
            arity_ok = (argc >= fn->min_param_count && argc <= fn->param_count);
            if (!arity_ok) {
                if (fn->min_param_count == fn->param_count) {
                    uf_runtime_error(rt, span, "Function '%s' expects %zu arguments, but %zu provided",
                                     fn->name ? fn->name : "anonymous", fn->param_count, argc);
                } else {
                    uf_runtime_error(rt, span, "Function '%s' expects %zu to %zu arguments, but %zu provided",
                                     fn->name ? fn->name : "anonymous", fn->min_param_count, fn->param_count, argc);
                }
            }
        }

        if (arity_ok) {
            UfEnv* call_env = uf_env_create(rt, fn->closure_env);
            if (uf_runtime_push_frame(rt, fn->name ? fn->name : "<anonymous>", span, call_env)) {
                UfEnv* prev_env = rt->current_env;
                rt->current_env = call_env;

                if (fn->name) {
                    uf_env_declare(call_env, fn->name, callee);
                }
                if (fn->has_rest) {
                    size_t fixed_count = fn->param_count - 1;
                    for (size_t i = 0; i < fixed_count && i < argc; ++i) {
                        uf_env_declare(call_env, fn->params[i], args[i]);
                    }
                    for (size_t i = argc; i < fixed_count; ++i) {
                        UfValue def_val = uf_val_null();
                        if (fn->param_defaults && fn->param_defaults[i]) {
                            def_val = uf_evaluate_expression(rt, call_env, fn->param_defaults[i]);
                            if (rt->had_runtime_error) break;
                        }
                        uf_env_declare(call_env, fn->params[i], def_val);
                    }
                    size_t rest_count = (argc > fixed_count) ? (argc - fixed_count) : 0;
                    UfValue rest_arr = uf_val_array(rt, rest_count);
                    for (size_t i = 0; i < rest_count; ++i) {
                        uf_array_push(rt, rest_arr.as.array, args[fixed_count + i]);
                    }
                    uf_env_declare(call_env, fn->params[fixed_count], rest_arr);
                } else {
                    for (size_t i = 0; i < argc; ++i) {
                        uf_env_declare(call_env, fn->params[i], args[i]);
                    }
                    for (size_t i = argc; i < fn->param_count; ++i) {
                        UfValue def_val = uf_val_null();
                        if (fn->param_defaults && fn->param_defaults[i]) {
                            def_val = uf_evaluate_expression(rt, call_env, fn->param_defaults[i]);
                            if (rt->had_runtime_error) break;
                        }
                        uf_env_declare(call_env, fn->params[i], def_val);
                    }
                }

                if (rt->debug_hook) {
                    UfDebugEvent ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = UF_DEBUG_EVENT_CALL_ENTER;
                    ev.span = span;
                    ev.fn_name = fn->name ? fn->name : "<anonymous>";
                    uf_runtime_emit_debug(rt, &ev);
                }

                ExecResult body_res = execute_statement(rt, call_env, fn->body);

                if (rt->debug_hook) {
                    UfDebugEvent ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = UF_DEBUG_EVENT_CALL_EXIT;
                    ev.span = span;
                    ev.fn_name = fn->name ? fn->name : "<anonymous>";
                    ev.val = (body_res.status == EXEC_RETURN) ? body_res.value : uf_val_null();
                    uf_runtime_emit_debug(rt, &ev);
                }

                uf_runtime_pop_frame(rt);
                rt->current_env = prev_env;

                if (body_res.status == EXEC_RETURN) {
                    result = body_res.value;
                } else {
                    result = uf_val_null();
                }
                if (fn->is_async) {
                    uf_runtime_push_temp_root(rt, result);
                    UfPromiseObject* p = uf_promise_create(rt);
                    uf_promise_resolve(rt, p, result);
                    uf_runtime_pop_temp_root(rt);
                    result = uf_val_promise(rt, p);
                }
            }
        }
    } else if (callee.kind == UF_VAL_NATIVE_FN) {
        UfNativeObject* nat = &callee.as.native_fn;
        if (nat->arity >= 0 && (int)argc != nat->arity) {
            uf_runtime_error(rt, span, "Native function '%s' expects %d arguments, but %zu provided",
                             nat->name, nat->arity, argc);
        } else if (uf_runtime_push_frame(rt, nat->name, span, rt->current_env)) {
            result = nat->fn(rt, (int)argc, args);
            uf_runtime_pop_frame(rt);
        }
    } else if (callee.kind == UF_VAL_STRUCT_DEF) {
        UfStructDefObject* sdef = callee.as.struct_def;
        if (sdef->field_count != argc) {
            uf_runtime_error(rt, span, "Struct '%s' constructor expects %zu argument%s, but %zu provided",
                             sdef->name, sdef->field_count, sdef->field_count == 1 ? "" : "s", argc);
        } else {
            result = uf_val_instance(rt, sdef, args, argc);
        }
    } else if (callee.kind == UF_VAL_ENUM_VAL) {
        UfEnumValObject* ev = callee.as.enum_val;
        if (ev->def && (size_t)ev->tag < ev->def->variant_count) {
            size_t expected = ev->def->variant_field_counts[ev->tag];
            if (expected != argc) {
                uf_runtime_error(rt, span, "TypeError: Enum variant '%s' expects %zu argument%s, but %zu provided",
                                 ev->variant_name, expected, expected == 1 ? "" : "s", argc);
            } else {
                result = uf_val_enum_val(rt, ev->def, ev->tag, ev->variant_name, args, argc);
            }
        }
    } else if (callee.kind == UF_VAL_CLOSURE) {
        UfClosureObject* cl = callee.as.closure;
        bool arity_ok = false;
        if (cl->function->has_rest) {
            arity_ok = (argc >= cl->function->min_arity);
            if (!arity_ok) {
                uf_runtime_error(rt, span, "Function '%s' expects at least %zu arguments, but %zu provided",
                                 cl->function->name ? cl->function->name : "anonymous", cl->function->min_arity, argc);
            }
        } else {
            arity_ok = (argc >= cl->function->min_arity && argc <= cl->function->arity);
            if (!arity_ok) {
                if (cl->function->min_arity == cl->function->arity) {
                    uf_runtime_error(rt, span, "Function '%s' expects %zu arguments, but %zu provided",
                                     cl->function->name ? cl->function->name : "anonymous", cl->function->arity, argc);
                } else {
                    uf_runtime_error(rt, span, "Function '%s' expects %zu to %zu arguments, but %zu provided",
                                     cl->function->name ? cl->function->name : "anonymous", cl->function->min_arity, cl->function->arity, argc);
                }
            }
        }
        if (arity_ok) {
            if (rt->active_vm) {
                result = uf_vm_run_closure((UfVM*)rt->active_vm, cl, argc, args);
            } else {
                uf_runtime_error(rt, span, "Cannot call bytecode closure without active VM");
            }
        }
    } else if (callee.kind == UF_VAL_REG_CLOSURE) {
        UfRegClosure* cl = callee.as.reg_closure;
        bool arity_ok = false;
        if (cl->function->has_rest) {
            arity_ok = (argc >= cl->function->min_arity);
            if (!arity_ok) {
                uf_runtime_error(rt, span, "Function '%s' expects at least %zu arguments, but %zu provided",
                                 cl->function->name ? cl->function->name : "anonymous", cl->function->min_arity, argc);
            }
        } else {
            arity_ok = (argc >= cl->function->min_arity && argc <= cl->function->arity);
            if (!arity_ok) {
                if (cl->function->min_arity == cl->function->arity) {
                    uf_runtime_error(rt, span, "Function '%s' expects %zu arguments, but %zu provided",
                                     cl->function->name ? cl->function->name : "anonymous", cl->function->arity, argc);
                } else {
                    uf_runtime_error(rt, span, "Function '%s' expects %zu to %zu arguments, but %zu provided",
                                     cl->function->name ? cl->function->name : "anonymous", cl->function->min_arity, cl->function->arity, argc);
                }
            }
        }
        if (arity_ok) {
            if (rt->active_regvm) {
                result = uf_regvm_run_closure((UfRegVM*)rt->active_regvm, cl, argc, args);
            } else {
                uf_runtime_error(rt, span, "Cannot call register closure without active RegVM");
            }
        }
    } else if (callee.kind == UF_VAL_BOUND_METHOD) {
        UfBoundMethodObject* bm = callee.as.bound_method;
        UfValue* new_args = (UfValue*)malloc((argc + 1) * sizeof(UfValue));
        new_args[0] = bm->receiver;
        for (size_t i = 0; i < argc; ++i) {
            new_args[i + 1] = args[i];
        }
        result = uf_call_value(rt, bm->method, argc + 1, new_args, span);
        free(new_args);
    } else {
        uf_runtime_error(rt, span, "Cannot call non-function of type '%s'", uf_val_type_name(callee));
    }

    return result;
}

static UfValue evaluate_call(UfRuntime* rt, UfEnv* env, const UfExpr* expr) {
    UfValue callee = uf_evaluate_expression(rt, env, expr->as.call.callee);
    if (rt->had_runtime_error) {
        return uf_val_null();
    }
    uf_runtime_push_temp_root(rt, callee);

    size_t cap = expr->as.call.argc < 16 ? 16 : expr->as.call.argc;
    UfValue inline_args[16];
    UfValue* args = inline_args;
    if (cap > 16) {
        args = (UfValue*)malloc(cap * sizeof(UfValue));
        if (!args) {
            uf_runtime_error(rt, expr->span, "Out of memory");
            uf_runtime_pop_temp_roots(rt, 1);
            return uf_val_null();
        }
    }

    size_t total_args = 0;
    for (size_t i = 0; i < expr->as.call.argc; ++i) {
        const UfExpr* arg_expr = expr->as.call.args[i];
        if (arg_expr->kind == UF_EXPR_SPREAD) {
            UfValue op = uf_evaluate_expression(rt, env, arg_expr->as.spread.operand);
            if (rt->had_runtime_error) {
                if (args != inline_args) free(args);
                uf_runtime_pop_temp_roots(rt, 1 + total_args);
                return uf_val_null();
            }
            if (op.kind != UF_VAL_ARRAY) {
                uf_runtime_error(rt, arg_expr->span, "Spread operand in function call must be an array, got '%s'",
                                 uf_val_type_name(op));
                if (args != inline_args) free(args);
                uf_runtime_pop_temp_roots(rt, 1 + total_args);
                return uf_val_null();
            }
            UfArrayObject* arr = op.as.array;
            size_t new_cap = total_args + arr->count;
            if (new_cap > cap) {
                cap = new_cap < cap * 2 ? cap * 2 : new_cap;
                if (args == inline_args) {
                    args = (UfValue*)malloc(cap * sizeof(UfValue));
                    if (!args) {
                        uf_runtime_error(rt, expr->span, "Out of memory");
                        uf_runtime_pop_temp_roots(rt, 1 + total_args);
                        return uf_val_null();
                    }
                    memcpy(args, inline_args, total_args * sizeof(UfValue));
                } else {
                    UfValue* new_args = (UfValue*)realloc(args, cap * sizeof(UfValue));
                    if (!new_args) {
                        free(args);
                        uf_runtime_error(rt, expr->span, "Out of memory");
                        uf_runtime_pop_temp_roots(rt, 1 + total_args);
                        return uf_val_null();
                    }
                    args = new_args;
                }
            }
            for (size_t j = 0; j < arr->count; ++j) {
                args[total_args++] = arr->elements[j];
                uf_runtime_push_temp_root(rt, arr->elements[j]);
            }
        } else {
            UfValue val = uf_evaluate_expression(rt, env, arg_expr);
            if (rt->had_runtime_error) {
                if (args != inline_args) free(args);
                uf_runtime_pop_temp_roots(rt, 1 + total_args);
                return uf_val_null();
            }
            if (total_args >= cap) {
                cap = cap * 2;
                if (args == inline_args) {
                    args = (UfValue*)malloc(cap * sizeof(UfValue));
                    if (!args) {
                        uf_runtime_error(rt, expr->span, "Out of memory");
                        uf_runtime_pop_temp_roots(rt, 1 + total_args);
                        return uf_val_null();
                    }
                    memcpy(args, inline_args, total_args * sizeof(UfValue));
                } else {
                    UfValue* new_args = (UfValue*)realloc(args, cap * sizeof(UfValue));
                    if (!new_args) {
                        free(args);
                        uf_runtime_error(rt, expr->span, "Out of memory");
                        uf_runtime_pop_temp_roots(rt, 1 + total_args);
                        return uf_val_null();
                    }
                    args = new_args;
                }
            }
            args[total_args++] = val;
            uf_runtime_push_temp_root(rt, val);
        }
    }

    UfValue result = uf_call_value(rt, callee, total_args, args, expr->span);
    uf_runtime_pop_temp_roots(rt, 1 + total_args);
    if (args != inline_args) {
        free(args);
    }
    return result;
}

UfValue uf_evaluate_expression(UfRuntime* rt, UfEnv* env, const UfExpr* expr) {
    if (!expr || rt->had_runtime_error) {
        return uf_val_null();
    }
    if (!rt->call_fn) {
        rt->call_fn = uf_call_value;
    }

    switch (expr->kind) {
        case UF_EXPR_LITERAL_NULL:
            return uf_val_null();

        case UF_EXPR_LITERAL_BOOL:
            return uf_val_bool(expr->as.bool_val);

        case UF_EXPR_LITERAL_NUMBER:
            return uf_val_number(expr->as.number_val);

        case UF_EXPR_LITERAL_STRING:
            return uf_val_string_cstr(rt, expr->as.string_val);

        case UF_EXPR_IDENTIFIER: {
            UfValue val;
            if (uf_env_lookup(env, expr->as.identifier_name, &val)) {
                return val;
            }
            uf_runtime_error(rt, expr->span, "Undefined identifier '%s'", expr->as.identifier_name);
            return uf_val_null();
        }

        case UF_EXPR_GROUPING:
            return uf_evaluate_expression(rt, env, expr->as.grouping.inner);

        case UF_EXPR_UNARY: {
            UfValue operand = uf_evaluate_expression(rt, env, expr->as.unary.operand);
            if (rt->had_runtime_error) return operand;

            if (expr->as.unary.op == UF_TOK_MINUS) {
                if (operand.kind != UF_VAL_NUMBER) {
                    uf_runtime_error(rt, expr->span, "Operand to unary '-' must be a number, got '%s'", uf_val_type_name(operand));
                    return uf_val_null();
                }
                return uf_val_number(-operand.as.number);
            } else if (expr->as.unary.op == UF_TOK_NOT) {
                return uf_val_bool(!uf_val_is_truthy(operand));
            }
            return uf_val_null();
        }

        case UF_EXPR_BINARY: {
            /* Short-circuit logical operators */
            if (expr->as.binary.op == UF_TOK_AND) {
                UfValue left = uf_evaluate_expression(rt, env, expr->as.binary.left);
                if (rt->had_runtime_error) return left;
                if (!uf_val_is_truthy(left)) {
                    return left;
                }
                return uf_evaluate_expression(rt, env, expr->as.binary.right);
            }

            if (expr->as.binary.op == UF_TOK_OR) {
                UfValue left = uf_evaluate_expression(rt, env, expr->as.binary.left);
                if (rt->had_runtime_error) return left;
                if (uf_val_is_truthy(left)) {
                    return left;
                }
                return uf_evaluate_expression(rt, env, expr->as.binary.right);
            }

            UfValue left = uf_evaluate_expression(rt, env, expr->as.binary.left);
            if (rt->had_runtime_error) return left;
            uf_runtime_push_temp_root(rt, left);

            UfValue right = uf_evaluate_expression(rt, env, expr->as.binary.right);
            if (rt->had_runtime_error) {
                uf_runtime_pop_temp_root(rt);
                return right;
            }
            uf_runtime_push_temp_root(rt, right);

            UfValue result = uf_val_null();

            switch (expr->as.binary.op) {
                case UF_TOK_PLUS: {
                    if (left.kind == UF_VAL_STRING || right.kind == UF_VAL_STRING) {
                        char* s1 = uf_val_to_string(left);
                        char* s2 = uf_val_to_string(right);
                        size_t l1 = strlen(s1);
                        size_t l2 = strlen(s2);
                        char* combined = (char*)malloc(l1 + l2 + 1);
                        memcpy(combined, s1, l1);
                        memcpy(combined + l1, s2, l2);
                        combined[l1 + l2] = '\0';
                        free(s1);
                        free(s2);
                        result = uf_val_string_take(rt, combined, l1 + l2);
                    } else if (left.kind == UF_VAL_NUMBER && right.kind == UF_VAL_NUMBER) {
                        result = uf_val_number(left.as.number + right.as.number);
                    } else {
                        uf_runtime_error(rt, expr->span, "Operands of '+' must be two numbers or at least one string, got '%s' and '%s'",
                                         uf_val_type_name(left), uf_val_type_name(right));
                    }
                    break;
                }

                case UF_TOK_MINUS:
                case UF_TOK_STAR:
                case UF_TOK_SLASH:
                case UF_TOK_PERCENT: {
                    if (left.kind != UF_VAL_NUMBER || right.kind != UF_VAL_NUMBER) {
                        uf_runtime_error(rt, expr->span, "Arithmetic operands must be numbers, got '%s' and '%s'",
                                         uf_val_type_name(left), uf_val_type_name(right));
                    } else {
                        double n1 = left.as.number;
                        double n2 = right.as.number;

                        if (expr->as.binary.op == UF_TOK_SLASH) {
                            if (n2 == 0.0) {
                                uf_runtime_error(rt, expr->span, "Division by zero");
                            } else {
                                result = uf_val_number(n1 / n2);
                            }
                        } else if (expr->as.binary.op == UF_TOK_PERCENT) {
                            if (n2 == 0.0) {
                                uf_runtime_error(rt, expr->span, "Modulo by zero");
                            } else {
                                result = uf_val_number(fmod(n1, n2));
                            }
                        } else if (expr->as.binary.op == UF_TOK_MINUS) {
                            result = uf_val_number(n1 - n2);
                        } else {
                            result = uf_val_number(n1 * n2);
                        }
                    }
                    break;
                }

                case UF_TOK_EQEQ:
                    result = uf_val_bool(uf_val_equal(left, right));
                    break;

                case UF_TOK_BANGEQ:
                    result = uf_val_bool(!uf_val_equal(left, right));
                    break;

                case UF_TOK_LT:
                case UF_TOK_LTEQ:
                case UF_TOK_GT:
                case UF_TOK_GTEQ: {
                    /* Strings order lexicographically, exactly as the bytecode
                     * VMs, the native backend, and sort()'s default comparison
                     * already do. The interpreter used to reject them outright,
                     * so `"a" < "b"` was the one thing sort() could do that a
                     * hand-written comparison could not. */
                    int cmp = 0;
                    bool comparable = true;
                    if (left.kind == UF_VAL_NUMBER && right.kind == UF_VAL_NUMBER) {
                        double a = left.as.number;
                        double b = right.as.number;
                        cmp = (a < b) ? -1 : ((a > b) ? 1 : 0);
                    } else if (left.kind == UF_VAL_STRING && right.kind == UF_VAL_STRING) {
                        cmp = strcmp(left.as.string->chars, right.as.string->chars);
                    } else {
                        comparable = false;
                    }

                    if (!comparable) {
                        uf_runtime_error(rt, expr->span,
                                         "Comparison operands must both be numbers or both be strings, got '%s' and '%s'",
                                         uf_val_type_name(left), uf_val_type_name(right));
                    } else {
                        if (expr->as.binary.op == UF_TOK_LT)   result = uf_val_bool(cmp < 0);
                        if (expr->as.binary.op == UF_TOK_LTEQ) result = uf_val_bool(cmp <= 0);
                        if (expr->as.binary.op == UF_TOK_GT)   result = uf_val_bool(cmp > 0);
                        if (expr->as.binary.op == UF_TOK_GTEQ) result = uf_val_bool(cmp >= 0);
                    }
                    break;
                }

                default:
                    break;
            }

            uf_runtime_pop_temp_roots(rt, 2);
            return result;
        }

        case UF_EXPR_CALL:
            return evaluate_call(rt, env, expr);

        case UF_EXPR_ARRAY: {
            UfValue arr_val = uf_val_array(rt, expr->as.array_lit.count);
            uf_runtime_push_temp_root(rt, arr_val);

            for (size_t i = 0; i < expr->as.array_lit.count; ++i) {
                const UfExpr* elem_expr = expr->as.array_lit.elements[i];
                if (elem_expr->kind == UF_EXPR_SPREAD) {
                    UfValue other = uf_evaluate_expression(rt, env, elem_expr->as.spread.operand);
                    if (rt->had_runtime_error) {
                        uf_runtime_pop_temp_root(rt);
                        return uf_val_null();
                    }
                    if (other.kind != UF_VAL_ARRAY) {
                        uf_runtime_error(rt, elem_expr->span, "Spread operand in array literal must be an array, got '%s'",
                                         uf_val_type_name(other));
                        uf_runtime_pop_temp_root(rt);
                        return uf_val_null();
                    }
                    for (size_t j = 0; j < other.as.array->count; ++j) {
                        uf_array_push(rt, arr_val.as.array, other.as.array->elements[j]);
                    }
                } else {
                    UfValue elem = uf_evaluate_expression(rt, env, elem_expr);
                    if (rt->had_runtime_error) {
                        uf_runtime_pop_temp_root(rt);
                        return uf_val_null();
                    }
                    uf_array_push(rt, arr_val.as.array, elem);
                }
            }

            uf_runtime_pop_temp_root(rt);
            return arr_val;
        }

        case UF_EXPR_MAP: {
            UfValue map_val = uf_val_map(rt, expr->as.map_lit.count);
            uf_runtime_push_temp_root(rt, map_val);

            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
                if (expr->as.map_lit.values[i] == NULL) {
                    const UfExpr* spread_expr = expr->as.map_lit.keys[i];
                    const UfExpr* operand = spread_expr->kind == UF_EXPR_SPREAD ? spread_expr->as.spread.operand : spread_expr;
                    UfValue other = uf_evaluate_expression(rt, env, operand);
                    if (rt->had_runtime_error) {
                        uf_runtime_pop_temp_root(rt);
                        return uf_val_null();
                    }
                    if (other.kind != UF_VAL_MAP) {
                        uf_runtime_error(rt, spread_expr->span, "Spread operand in map literal must be a map, got '%s'",
                                         uf_val_type_name(other));
                        uf_runtime_pop_temp_root(rt);
                        return uf_val_null();
                    }
                    for (size_t j = 0; j < other.as.map->order_count; ++j) {
                        UfValue k = other.as.map->order_keys[j];
                        UfValue v = uf_map_get(other.as.map, k);
                        uf_map_set(rt, map_val.as.map, k, v);
                    }
                    continue;
                }

                UfValue k = uf_evaluate_expression(rt, env, expr->as.map_lit.keys[i]);
                if (rt->had_runtime_error) {
                    uf_runtime_pop_temp_root(rt);
                    return uf_val_null();
                }
                uf_runtime_push_temp_root(rt, k);

                UfValue v = uf_evaluate_expression(rt, env, expr->as.map_lit.values[i]);
                if (rt->had_runtime_error) {
                    uf_runtime_pop_temp_roots(rt, 2);
                    return uf_val_null();
                }

                if (k.kind != UF_VAL_STRING && k.kind != UF_VAL_NUMBER && k.kind != UF_VAL_BOOL && k.kind != UF_VAL_NULL) {
                    uf_runtime_error(rt, expr->as.map_lit.keys[i]->span, "Map key must be a string or number, got '%s'", uf_val_type_name(k));
                    uf_runtime_pop_temp_roots(rt, 2);
                    return uf_val_null();
                }

                uf_map_set(rt, map_val.as.map, k, v);
                uf_runtime_pop_temp_root(rt);
            }

            uf_runtime_pop_temp_root(rt);
            return map_val;
        }

        case UF_EXPR_INDEX: {
            UfValue target = uf_evaluate_expression(rt, env, expr->as.index_expr.target);
            if (rt->had_runtime_error) return uf_val_null();
            uf_runtime_push_temp_root(rt, target);

            UfValue idx_val = uf_evaluate_expression(rt, env, expr->as.index_expr.index);
            if (rt->had_runtime_error) {
                uf_runtime_pop_temp_root(rt);
                return uf_val_null();
            }
            uf_runtime_push_temp_root(rt, idx_val);

            UfValue result = uf_val_null();

            if (target.kind == UF_VAL_MAP) {
                result = uf_map_get(target.as.map, idx_val);
            } else if (target.kind == UF_VAL_ARRAY) {
                if (idx_val.kind != UF_VAL_NUMBER) {
                    uf_runtime_error(rt, expr->span, "Array index must be a number, got '%s'", uf_val_type_name(idx_val));
                } else {
                    int64_t idx = (int64_t)idx_val.as.number;
                    UfArrayObject* arr = target.as.array;
                    if (idx < 0) idx += arr->count;
                    if (idx < 0 || (size_t)idx >= arr->count) {
                        uf_runtime_error(rt, expr->span, "IndexOutOfBounds: Index %ld out of bounds for array of length %zu", (long)idx, arr->count);
                    } else {
                        result = arr->elements[idx];
                    }
                }
            } else if (target.kind == UF_VAL_STRING) {
                if (idx_val.kind != UF_VAL_NUMBER) {
                    uf_runtime_error(rt, expr->span, "String index must be a number, got '%s'", uf_val_type_name(idx_val));
                } else {
                    int64_t idx = (int64_t)idx_val.as.number;
                    UfStringObject* str = target.as.string;
                    if (idx < 0) idx += str->length;
                    if (idx < 0 || (size_t)idx >= str->length) {
                        uf_runtime_error(rt, expr->span, "IndexOutOfBounds: Index %ld out of bounds for string of length %zu", (long)idx, str->length);
                    } else {
                        char ch[2] = { str->chars[idx], '\0' };
                        result = uf_val_string(rt, ch, 1);
                    }
                }
            } else if (target.kind == UF_VAL_ERROR) {
                if (idx_val.kind != UF_VAL_STRING) {
                    uf_runtime_error(rt, expr->span, "Error property must be a string");
                } else {
                    const char* prop = idx_val.as.string->chars;
                    UfErrorObject* err = target.as.error;
                    if (strcmp(prop, "message") == 0) {
                        result = uf_val_string(rt, err->message ? err->message->chars : "", err->message ? err->message->length : 0);
                    } else if (strcmp(prop, "kind") == 0) {
                        result = uf_val_string(rt, err->kind ? err->kind->chars : "Error", err->kind ? err->kind->length : 5);
                    } else if (strcmp(prop, "line") == 0) {
                        result = uf_val_number((double)err->line);
                    } else if (strcmp(prop, "file") == 0) {
                        result = uf_val_string_cstr(rt, err->file ? err->file : "<unknown>");
                    } else {
                        result = uf_val_null();
                    }
                }
            } else if (target.kind == UF_VAL_MODULE) {
                if (idx_val.kind != UF_VAL_STRING) {
                    uf_runtime_error(rt, expr->span, "Module member access expects a string name, got '%s'", uf_val_type_name(idx_val));
                } else {
                    result = uf_map_get(target.as.module->exports.as.map, idx_val);
                    if (result.kind == UF_VAL_NULL && !uf_map_has(target.as.module->exports.as.map, idx_val)) {
                        uf_runtime_error(rt, expr->span, "Module '%s' has no exported member '%s'",
                                         (target.as.module && target.as.module->name) ? target.as.module->name : "anonymous",
                                         idx_val.as.string->chars);
                    }
                }
            } else if (target.kind == UF_VAL_INSTANCE) {
                if (idx_val.kind != UF_VAL_STRING) {
                    uf_runtime_error(rt, expr->span, "Struct field access expects a string name, got '%s'", uf_val_type_name(idx_val));
                } else {
                    const char* fname = idx_val.as.string->chars;
                    UfInstanceObject* inst = target.as.instance;
                    int fidx = -1;
                    for (size_t i = 0; i < inst->field_count; ++i) {
                        if (strcmp(inst->def->field_names[i], fname) == 0) {
                            fidx = (int)i;
                            break;
                        }
                    }
                    if (fidx >= 0) {
                        result = inst->fields[fidx];
                    } else {
                        bool found_method = false;
                        for (size_t i = 0; i < inst->def->method_count; ++i) {
                            if (strcmp(inst->def->method_names[i], fname) == 0) {
                                result = uf_val_bound_method(rt, target, inst->def->method_values[i]);
                                found_method = true;
                                break;
                            }
                        }
                        if (!found_method) {
                            uf_runtime_error(rt, expr->span, "Struct '%s' has no field or method '%s'", inst->def->name, fname);
                        }
                    }
                }
            } else if (target.kind == UF_VAL_ENUM_DEF) {
                if (idx_val.kind != UF_VAL_STRING) {
                    uf_runtime_error(rt, expr->span, "Enum member access expects a string variant name, got '%s'", uf_val_type_name(idx_val));
                } else {
                    const char* vname = idx_val.as.string->chars;
                    UfEnumDefObject* def = target.as.enum_def;
                    bool found = false;
                    for (size_t i = 0; i < def->variant_count; ++i) {
                        if (strcmp(def->variant_names[i], vname) == 0) {
                            found = true;
                            result = uf_val_enum_val(rt, def, (int)i, def->variant_names[i], NULL, 0);
                            break;
                        }
                    }
                    if (!found) {
                        uf_runtime_error(rt, expr->span, "Enum '%s' has no variant '%s'", def->name ? def->name : "", vname);
                    }
                }
            } else if (target.kind == UF_VAL_ENUM_VAL) {
                UfEnumValObject* ev = target.as.enum_val;
                if (idx_val.kind == UF_VAL_NUMBER) {
                    long idx = (long)idx_val.as.number;
                    if (idx >= 0 && (size_t)idx < ev->field_count) {
                        result = ev->fields[idx];
                    } else {
                        result = uf_val_null();
                    }
                } else if (idx_val.kind != UF_VAL_STRING) {
                    uf_runtime_error(rt, expr->span, "Enum property access expects a string name, got '%s'", uf_val_type_name(idx_val));
                } else {
                    const char* prop = idx_val.as.string->chars;
                    if (strcmp(prop, "tag") == 0) {
                        result = uf_val_number((double)ev->tag);
                    } else if (strcmp(prop, "name") == 0) {
                        result = uf_val_string_cstr(rt, ev->variant_name ? ev->variant_name : "");
                    } else {
                        bool found = false;
                        if (ev->def && (size_t)ev->tag < ev->def->variant_count && ev->def->variant_field_names) {
                            const char** fnames = ev->def->variant_field_names[ev->tag];
                            if (fnames) {
                                for (size_t f = 0; f < ev->field_count; ++f) {
                                    if (strcmp(fnames[f], prop) == 0) {
                                        result = ev->fields[f];
                                        found = true;
                                        break;
                                    }
                                }
                            }
                        }
                        if (!found) {
                            uf_runtime_error(rt, expr->span, "Enum variant '%s' has no field '%s'", ev->variant_name ? ev->variant_name : "", prop);
                        }
                    }
                }
            } else {
                uf_runtime_error(rt, expr->span, "Cannot index value of type '%s'", uf_val_type_name(target));
            }

            uf_runtime_pop_temp_roots(rt, 2);
            return result;
        }

        case UF_EXPR_FUNCTION: {
            UfValue fn = uf_val_function(rt,
                                         expr->as.fn_expr.name,
                                         expr->as.fn_expr.params,
                                         expr->as.fn_expr.param_defaults,
                                         expr->as.fn_expr.param_count,
                                         expr->as.fn_expr.min_param_count,
                                         expr->as.fn_expr.has_rest,
                                         expr->as.fn_expr.body,
                                         env);
            fn.as.function->is_async = expr->as.fn_expr.is_async;
            return fn;
        }

        case UF_EXPR_SPREAD:
            return uf_evaluate_expression(rt, env, expr->as.spread.operand);

        case UF_EXPR_AWAIT: {
            UfValue val = uf_evaluate_expression(rt, env, expr->as.await_expr.value);
            if (rt->had_runtime_error) return uf_val_null();
            if (val.kind == UF_VAL_PROMISE && val.as.promise) {
                return uf_promise_await(rt, val.as.promise);
            }
            return val;
        }

        case UF_EXPR_STRING_INTERP: {
            UfStrBuf buf;
            uf_strbuf_init(&buf);
            for (size_t i = 0; i < expr->as.string_interp.count; ++i) {
                UfValue val = uf_evaluate_expression(rt, env, expr->as.string_interp.parts[i]);
                if (rt->had_runtime_error) {
                    uf_strbuf_free(&buf);
                    return uf_val_null();
                }
                char* s = uf_val_to_string(val);
                uf_strbuf_append(&buf, s ? s : "");
                free(s);
            }
            UfValue result = uf_val_string(rt, buf.data ? buf.data : "", buf.length);
            uf_strbuf_free(&buf);
            return result;
        }
    }

    return uf_val_null();
}

static bool match_pattern_and_bind(UfRuntime* rt, UfEnv* env, const UfPattern* pat, UfValue val) {
    if (!pat) return true;
    switch (pat->kind) {
        case UF_PAT_WILDCARD:
            return true;

        case UF_PAT_VARIABLE: {
            UfValue existing = uf_val_null();
            if (uf_env_lookup(env, pat->as.var_name, &existing)) {
                if (existing.kind == UF_VAL_ENUM_VAL && existing.as.enum_val && existing.as.enum_val->field_count == 0) {
                    return uf_val_equal(val, existing);
                }
            }
            uf_env_declare(env, pat->as.var_name, val);
            return true;
        }

        case UF_PAT_LITERAL: {
            UfValue lit_val = uf_evaluate_expression(rt, env, pat->as.literal);
            if (rt->had_runtime_error) return false;
            return uf_val_equal(lit_val, val);
        }

        case UF_PAT_STRUCT: {
            if (val.kind == UF_VAL_INSTANCE) {
                if (!val.as.instance || !val.as.instance->def) return false;
                if (strcmp(val.as.instance->def->name, pat->as.struct_pat.struct_name) != 0) return false;
                if (val.as.instance->field_count != pat->as.struct_pat.field_count) return false;
                for (size_t i = 0; i < pat->as.struct_pat.field_count; ++i) {
                    if (!match_pattern_and_bind(rt, env, pat->as.struct_pat.field_patterns[i], val.as.instance->fields[i])) {
                        return false;
                    }
                }
                return true;
            } else if (val.kind == UF_VAL_ENUM_VAL) {
                if (!val.as.enum_val || !val.as.enum_val->variant_name) return false;
                if (strcmp(val.as.enum_val->variant_name, pat->as.struct_pat.struct_name) != 0) return false;
                if (val.as.enum_val->field_count != pat->as.struct_pat.field_count) return false;
                for (size_t i = 0; i < pat->as.struct_pat.field_count; ++i) {
                    if (!match_pattern_and_bind(rt, env, pat->as.struct_pat.field_patterns[i], val.as.enum_val->fields[i])) {
                        return false;
                    }
                }
                return true;
            }
            return false;
        }

        case UF_PAT_REST:
            if (pat->as.rest_pat.subpattern) {
                return match_pattern_and_bind(rt, env, pat->as.rest_pat.subpattern, val);
            }
            return true;

        case UF_PAT_ARRAY: {
            if (val.kind != UF_VAL_ARRAY) return false;
            UfArrayObject* arr = val.as.array;
            size_t min_count = pat->as.array_pat.has_rest ? (pat->as.array_pat.count > 0 ? pat->as.array_pat.count - 1 : 0) : pat->as.array_pat.count;
            if (!pat->as.array_pat.has_rest) {
                if (arr->count != pat->as.array_pat.count) return false;
            } else {
                if (arr->count < min_count) return false;
            }
            for (size_t i = 0; i < min_count; ++i) {
                if (!match_pattern_and_bind(rt, env, pat->as.array_pat.elements[i], arr->elements[i])) {
                    return false;
                }
            }
            if (pat->as.array_pat.has_rest) {
                size_t rest_len = arr->count >= min_count ? arr->count - min_count : 0;
                UfValue rest_val = uf_val_array(rt, rest_len);
                for (size_t i = 0; i < rest_len; ++i) {
                    uf_array_push(rt, rest_val.as.array, arr->elements[min_count + i]);
                }
                UfPattern* rest_pat = pat->as.array_pat.elements[min_count];
                if (!match_pattern_and_bind(rt, env, rest_pat, rest_val)) {
                    return false;
                }
            }
            return true;
        }

        case UF_PAT_MAP: {
            if (val.kind != UF_VAL_MAP && val.kind != UF_VAL_INSTANCE) return false;
            for (size_t i = 0; i < pat->as.map_pat.count; ++i) {
                const char* k_str = pat->as.map_pat.keys[i];
                UfValue item_val = uf_val_null();
                if (val.kind == UF_VAL_MAP) {
                    UfValue k_val = uf_val_string(rt, k_str, strlen(k_str));
                    item_val = uf_map_get(val.as.map, k_val);
                } else if (val.kind == UF_VAL_INSTANCE && val.as.instance->def) {
                    for (size_t f = 0; f < val.as.instance->field_count; ++f) {
                        if (strcmp(val.as.instance->def->field_names[f], k_str) == 0) {
                            item_val = val.as.instance->fields[f];
                            break;
                        }
                    }
                }
                if (!match_pattern_and_bind(rt, env, pat->as.map_pat.values[i], item_val)) {
                    return false;
                }
            }
            if (pat->as.map_pat.has_rest && pat->as.map_pat.rest_pattern) {
                UfValue rest_map = uf_val_map(rt, 0);
                if (val.kind == UF_VAL_MAP) {
                    UfMapObject* m = val.as.map;
                    for (size_t i = 0; i < m->order_count; ++i) {
                        UfValue k = m->order_keys[i];
                        if (k.kind == UF_VAL_STRING) {
                            bool excluded = false;
                            for (size_t j = 0; j < pat->as.map_pat.count; ++j) {
                                if (strcmp(k.as.string->chars, pat->as.map_pat.keys[j]) == 0) {
                                    excluded = true;
                                    break;
                                }
                            }
                            if (!excluded) {
                                uf_map_set(rt, rest_map.as.map, k, uf_map_get(m, k));
                            }
                        }
                    }
                }
                if (!match_pattern_and_bind(rt, env, pat->as.map_pat.rest_pattern, rest_map)) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

static void destructure_and_bind(UfRuntime* rt, UfEnv* env, const UfPattern* pat, UfValue val, SourceSpan span, bool is_declaration) {
    if (!pat || rt->had_runtime_error) return;
    switch (pat->kind) {
        case UF_PAT_WILDCARD:
            break;
        case UF_PAT_VARIABLE:
            if (is_declaration) {
                uf_env_declare(env, pat->as.var_name, val);
            } else {
                if (!uf_env_assign(env, pat->as.var_name, val)) {
                    uf_runtime_error(rt, span, "Cannot assign to undefined identifier '%s'", pat->as.var_name);
                }
            }
            break;
        case UF_PAT_REST:
            if (pat->as.rest_pat.subpattern) {
                destructure_and_bind(rt, env, pat->as.rest_pat.subpattern, val, span, is_declaration);
            }
            break;
        case UF_PAT_ARRAY: {
            if (val.kind != UF_VAL_ARRAY) {
                uf_runtime_error(rt, span, "TypeError: Cannot destructure non-array value of type '%s'", uf_val_type_name(val));
                return;
            }
            UfArrayObject* arr = val.as.array;
            size_t normal_count = pat->as.array_pat.has_rest ? (pat->as.array_pat.count > 0 ? pat->as.array_pat.count - 1 : 0) : pat->as.array_pat.count;
            for (size_t i = 0; i < normal_count; ++i) {
                UfValue elem = (i < arr->count) ? arr->elements[i] : uf_val_null();
                destructure_and_bind(rt, env, pat->as.array_pat.elements[i], elem, span, is_declaration);
                if (rt->had_runtime_error) return;
            }
            if (pat->as.array_pat.has_rest) {
                size_t rest_len = arr->count >= normal_count ? arr->count - normal_count : 0;
                UfValue rest_val = uf_val_array(rt, rest_len);
                for (size_t i = 0; i < rest_len; ++i) {
                    uf_array_push(rt, rest_val.as.array, arr->elements[normal_count + i]);
                }
                UfPattern* rest_pat = pat->as.array_pat.elements[normal_count];
                destructure_and_bind(rt, env, rest_pat, rest_val, span, is_declaration);
            }
            break;
        }
        case UF_PAT_MAP: {
            if (val.kind != UF_VAL_MAP && val.kind != UF_VAL_INSTANCE) {
                uf_runtime_error(rt, span, "TypeError: Cannot destructure non-map value of type '%s'", uf_val_type_name(val));
                return;
            }
            for (size_t i = 0; i < pat->as.map_pat.count; ++i) {
                const char* k_str = pat->as.map_pat.keys[i];
                UfValue item_val = uf_val_null();
                if (val.kind == UF_VAL_MAP) {
                    UfValue k_val = uf_val_string(rt, k_str, strlen(k_str));
                    item_val = uf_map_get(val.as.map, k_val);
                } else if (val.kind == UF_VAL_INSTANCE && val.as.instance->def) {
                    for (size_t f = 0; f < val.as.instance->field_count; ++f) {
                        if (strcmp(val.as.instance->def->field_names[f], k_str) == 0) {
                            item_val = val.as.instance->fields[f];
                            break;
                        }
                    }
                }
                destructure_and_bind(rt, env, pat->as.map_pat.values[i], item_val, span, is_declaration);
                if (rt->had_runtime_error) return;
            }
            if (pat->as.map_pat.has_rest && pat->as.map_pat.rest_pattern) {
                UfValue rest_map = uf_val_map(rt, 0);
                if (val.kind == UF_VAL_MAP) {
                    UfMapObject* m = val.as.map;
                    for (size_t i = 0; i < m->order_count; ++i) {
                        UfValue k = m->order_keys[i];
                        if (k.kind == UF_VAL_STRING) {
                            bool excluded = false;
                            for (size_t j = 0; j < pat->as.map_pat.count; ++j) {
                                if (strcmp(k.as.string->chars, pat->as.map_pat.keys[j]) == 0) {
                                    excluded = true;
                                    break;
                                }
                            }
                            if (!excluded) {
                                uf_map_set(rt, rest_map.as.map, k, uf_map_get(m, k));
                            }
                        }
                    }
                }
                destructure_and_bind(rt, env, pat->as.map_pat.rest_pattern, rest_map, span, is_declaration);
            }
            break;
        }
        case UF_PAT_STRUCT: {
            if (val.kind != UF_VAL_INSTANCE) {
                uf_runtime_error(rt, span, "TypeError: Cannot destructure struct from non-instance value");
                return;
            }
            for (size_t i = 0; i < pat->as.struct_pat.field_count; ++i) {
                UfValue f_val = (i < val.as.instance->field_count) ? val.as.instance->fields[i] : uf_val_null();
                destructure_and_bind(rt, env, pat->as.struct_pat.field_patterns[i], f_val, span, is_declaration);
                if (rt->had_runtime_error) return;
            }
            break;
        }
        case UF_PAT_LITERAL:
            break;
    }
}

static ExecResult execute_block(UfRuntime* rt, UfEnv* env, const UfStmt* block_stmt) {
    for (size_t i = 0; i < block_stmt->as.block.count; ++i) {
        ExecResult res = execute_statement(rt, env, block_stmt->as.block.stmts[i]);
        if (res.status != EXEC_OK) {
            return res;
        }
    }
    return exec_ok();
}

static void execute_trait_def(UfRuntime* rt, UfEnv* env, const UfStmt* stmt) {
    UfValue tdef = uf_val_trait_def(rt,
                                     stmt->as.trait_stmt.name,
                                     stmt->as.trait_stmt.method_count,
                                     stmt->as.trait_stmt.method_names,
                                     stmt->as.trait_stmt.method_param_counts,
                                     stmt->as.trait_stmt.method_param_names,
                                     stmt->as.trait_stmt.method_param_types,
                                     stmt->as.trait_stmt.method_return_types);
    uf_env_declare(env, stmt->as.trait_stmt.name, tdef);
}

static void execute_struct_def(UfRuntime* rt, UfEnv* env, const UfStmt* stmt) {
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
            mvals[idx] = uf_val_function(rt,
                                       m->as.function_stmt.name,
                                       m->as.function_stmt.params,
                                       m->as.function_stmt.param_defaults,
                                       m->as.function_stmt.param_count,
                                       m->as.function_stmt.min_param_count,
                                       m->as.function_stmt.has_rest,
                                       m->as.function_stmt.body,
                                       env);
            mvals[idx].as.function->is_async = m->as.function_stmt.is_async;
            uf_runtime_push_temp_root(rt, mvals[idx]);
            idx++;
        }
        for (size_t b = 0; b < stmt->as.struct_stmt.impl_block_count; ++b) {
            UfStmt* iblock = stmt->as.struct_stmt.impl_blocks[b];
            for (size_t i = 0; i < iblock->as.impl_stmt.method_count; ++i) {
                UfStmt* m = iblock->as.impl_stmt.methods[i];
                mnames[idx] = m->as.function_stmt.name;
                mvals[idx] = uf_val_function(rt,
                                           m->as.function_stmt.name,
                                           m->as.function_stmt.params,
                                           m->as.function_stmt.param_defaults,
                                           m->as.function_stmt.param_count,
                                           m->as.function_stmt.min_param_count,
                                           m->as.function_stmt.has_rest,
                                           m->as.function_stmt.body,
                                           env);
                mvals[idx].as.function->is_async = m->as.function_stmt.is_async;
                uf_runtime_push_temp_root(rt, mvals[idx]);
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
    UfValue sdef = uf_val_struct_def_with_traits(rt,
                                                 stmt->as.struct_stmt.name,
                                                 stmt->as.struct_stmt.field_names,
                                                 stmt->as.struct_stmt.field_types,
                                                 stmt->as.struct_stmt.field_count,
                                                 mnames,
                                                 mvals,
                                                 total_mcount,
                                                 traits,
                                                 trait_count);
    uf_env_declare(env, stmt->as.struct_stmt.name, sdef);
    /* The methods sat in a plain C array until the definition took them, so
     * they were rooted one by one as they were created. */
    uf_runtime_pop_temp_roots(rt, total_mcount);
}

static void execute_impl_def(UfRuntime* rt, UfEnv* env, const UfStmt* stmt) {
    const char* sname = stmt->as.impl_stmt.struct_name;
    if (sname) {
        UfValue sval;
        if (uf_env_lookup(env, sname, &sval) && sval.kind == UF_VAL_STRUCT_DEF) {
            UfStructDefObject* sdef = sval.as.struct_def;
            size_t add_mcount = stmt->as.impl_stmt.method_count;
            if (add_mcount > 0) {
                size_t new_mcount = sdef->method_count + add_mcount;
                sdef->method_names = (const char**)realloc((void*)sdef->method_names, new_mcount * sizeof(const char*));
                sdef->method_values = (UfValue*)realloc((void*)sdef->method_values, new_mcount * sizeof(UfValue));
                /* Count each method in as soon as it is stored: the next
                 * one's allocation may collect, and only the first
                 * method_count slots are marked. */
                for (size_t i = 0; i < add_mcount; ++i) {
                    UfStmt* m = stmt->as.impl_stmt.methods[i];
                    sdef->method_names[sdef->method_count] = m->as.function_stmt.name;
                    sdef->method_values[sdef->method_count] = uf_val_function(rt,
                                                                                  m->as.function_stmt.name,
                                                                                  m->as.function_stmt.params,
                                                                                  m->as.function_stmt.param_defaults,
                                                                                  m->as.function_stmt.param_count,
                                                                                  m->as.function_stmt.min_param_count,
                                                                                  m->as.function_stmt.has_rest,
                                                                                  m->as.function_stmt.body,
                                                                                  env);
                    sdef->method_values[sdef->method_count].as.function->is_async = m->as.function_stmt.is_async;
                    sdef->method_count++;
                }
            }
            if (stmt->as.impl_stmt.trait_name) {
                sdef->impl_traits = (const char**)realloc((void*)sdef->impl_traits, (sdef->impl_trait_count + 1) * sizeof(const char*));
                sdef->impl_traits[sdef->impl_trait_count++] = stmt->as.impl_stmt.trait_name;
            }
        }
    }
}

static ExecResult execute_statement(UfRuntime* rt, UfEnv* env, const UfStmt* stmt) {
    if (!stmt || rt->had_runtime_error) {
        return exec_error();
    }

    /* Execution quota check */
    if (rt->max_steps > 0 && ++rt->step_count > rt->max_steps) {
        uf_runtime_error(rt, stmt->span, "ExecutionQuotaExceeded: Program exceeded maximum step quota (%llu steps)", (unsigned long long)rt->max_steps);
        return exec_error();
    }

    if (rt->debug_hook && stmt->kind != UF_STMT_BLOCK && stmt->span.start.line > 0) {
        UfDebugEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = UF_DEBUG_EVENT_STEP;
        ev.span = stmt->span;
        uf_runtime_emit_debug(rt, &ev);
    }

    switch (stmt->kind) {
        case UF_STMT_LET: {
            UfValue init_val = uf_val_null();
            if (stmt->as.let_stmt.init) {
                init_val = uf_evaluate_expression(rt, env, stmt->as.let_stmt.init);
                if (rt->had_runtime_error) return exec_error();
                uf_runtime_push_temp_root(rt, init_val);
            }
            if (stmt->as.let_stmt.pattern) {
                destructure_and_bind(rt, env, stmt->as.let_stmt.pattern, init_val, stmt->span, true);
                if (stmt->as.let_stmt.init) {
                    uf_runtime_pop_temp_root(rt);
                }
                if (rt->had_runtime_error) return exec_error();
                return exec_ok();
            }
            uf_env_declare(env, stmt->as.let_stmt.name, init_val);
            if (rt->debug_hook) {
                UfDebugEvent ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = UF_DEBUG_EVENT_VAR_BIND;
                ev.span = stmt->span;
                ev.var_name = stmt->as.let_stmt.name;
                ev.val = init_val;
                uf_runtime_emit_debug(rt, &ev);
            }
            if (stmt->as.let_stmt.init) {
                uf_runtime_pop_temp_root(rt);
            }
            return exec_ok();
        }

        case UF_STMT_ASSIGN: {
            UfValue val = uf_evaluate_expression(rt, env, stmt->as.assign_stmt.value);
            if (rt->had_runtime_error) return exec_error();
            uf_runtime_push_temp_root(rt, val);
            if (stmt->as.assign_stmt.pattern) {
                destructure_and_bind(rt, env, stmt->as.assign_stmt.pattern, val, stmt->span, false);
                uf_runtime_pop_temp_root(rt);
                if (rt->had_runtime_error) return exec_error();
                return exec_ok();
            }
            if (!uf_env_assign(env, stmt->as.assign_stmt.name, val)) {
                uf_runtime_error(rt, stmt->span, "Cannot assign to undefined identifier '%s'", stmt->as.assign_stmt.name);
                uf_runtime_pop_temp_root(rt);
                return exec_error();
            }
            if (rt->debug_hook) {
                UfDebugEvent ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = UF_DEBUG_EVENT_VAR_ASSIGN;
                ev.span = stmt->span;
                ev.var_name = stmt->as.assign_stmt.name;
                ev.val = val;
                uf_runtime_emit_debug(rt, &ev);
            }
            uf_runtime_pop_temp_root(rt);
            return exec_ok();
        }

        case UF_STMT_INDEX_ASSIGN: {
            UfValue target = uf_evaluate_expression(rt, env, stmt->as.index_assign.target);
            if (rt->had_runtime_error) return exec_error();
            uf_runtime_push_temp_root(rt, target);

            UfValue idx_val = uf_evaluate_expression(rt, env, stmt->as.index_assign.index);
            if (rt->had_runtime_error) {
                uf_runtime_pop_temp_root(rt);
                return exec_error();
            }
            uf_runtime_push_temp_root(rt, idx_val);

            UfValue val = uf_evaluate_expression(rt, env, stmt->as.index_assign.value);
            if (rt->had_runtime_error) {
                uf_runtime_pop_temp_roots(rt, 2);
                return exec_error();
            }
            uf_runtime_push_temp_root(rt, val);

            if (target.kind == UF_VAL_MAP) {
                if (idx_val.kind != UF_VAL_STRING && idx_val.kind != UF_VAL_NUMBER && idx_val.kind != UF_VAL_BOOL && idx_val.kind != UF_VAL_NULL) {
                    uf_runtime_error(rt, stmt->span, "Map key must be a string or number, got '%s'", uf_val_type_name(idx_val));
                } else {
                    uf_map_set(rt, target.as.map, idx_val, val);
                }
            } else if (target.kind == UF_VAL_ARRAY) {
                if (idx_val.kind != UF_VAL_NUMBER) {
                    uf_runtime_error(rt, stmt->span, "Array index must be a number, got '%s'", uf_val_type_name(idx_val));
                } else {
                    int64_t idx = (int64_t)idx_val.as.number;
                    UfArrayObject* arr = target.as.array;
                    if (idx < 0) idx += arr->count;
                    if (idx < 0 || (size_t)idx >= arr->count) {
                        uf_runtime_error(rt, stmt->span, "IndexOutOfBounds: Index %ld out of bounds for array of length %zu", (long)idx, arr->count);
                    } else {
                        arr->elements[idx] = val;
                    }
                }
            } else if (target.kind == UF_VAL_INSTANCE) {
                if (idx_val.kind != UF_VAL_STRING) {
                    uf_runtime_error(rt, stmt->span, "Struct field name must be a string, got '%s'", uf_val_type_name(idx_val));
                } else {
                    const char* fname = idx_val.as.string->chars;
                    UfInstanceObject* inst = target.as.instance;
                    int fidx = -1;
                    for (size_t i = 0; i < inst->field_count; ++i) {
                        if (strcmp(inst->def->field_names[i], fname) == 0) {
                            fidx = (int)i;
                            break;
                        }
                    }
                    if (fidx < 0) {
                        uf_runtime_error(rt, stmt->span, "Struct '%s' has no field '%s'", inst->def->name, fname);
                    } else {
                        inst->fields[fidx] = val;
                    }
                }
            } else {
                uf_runtime_error(rt, stmt->span, "Cannot assign to index of type '%s'", uf_val_type_name(target));
            }

            uf_runtime_pop_temp_roots(rt, 3);
            return rt->had_runtime_error ? exec_error() : exec_ok();
        }

        case UF_STMT_SAY: {
            UfValue val = uf_evaluate_expression(rt, env, stmt->as.say_stmt.expr);
            if (rt->had_runtime_error) return exec_error();
            char* s = uf_val_to_string(val);
            fprintf(rt->out_stream, "%s\n", s);
            fflush(rt->out_stream);
            free(s);
            return exec_ok();
        }

        case UF_STMT_EXPR: {
            uf_evaluate_expression(rt, env, stmt->as.expr_stmt.expr);
            if (rt->had_runtime_error) return exec_error();
            return exec_ok();
        }

        case UF_STMT_IF: {
            UfValue cond = uf_evaluate_expression(rt, env, stmt->as.if_stmt.condition);
            if (rt->had_runtime_error) return exec_error();
            bool truthy = uf_val_is_truthy(cond);

            if (truthy) {
                UfEnv* block_env = uf_env_create(rt, env);
                UfEnv* prev_env = rt->current_env;
                rt->current_env = block_env;
                ExecResult res = execute_statement(rt, block_env, stmt->as.if_stmt.then_branch);
                rt->current_env = prev_env;
                return res;
            } else if (stmt->as.if_stmt.else_branch) {
                if (stmt->as.if_stmt.else_branch->kind == UF_STMT_IF) {
                    return execute_statement(rt, env, stmt->as.if_stmt.else_branch);
                } else {
                    UfEnv* block_env = uf_env_create(rt, env);
                    UfEnv* prev_env = rt->current_env;
                    rt->current_env = block_env;
                    ExecResult res = execute_statement(rt, block_env, stmt->as.if_stmt.else_branch);
                    rt->current_env = prev_env;
                    return res;
                }
            }
            return exec_ok();
        }

        case UF_STMT_WHILE: {
            for (;;) {
                UfValue cond = uf_evaluate_expression(rt, env, stmt->as.while_stmt.condition);
                if (rt->had_runtime_error) return exec_error();

                if (!uf_val_is_truthy(cond)) {
                    break;
                }

                UfEnv* block_env = uf_env_create(rt, env);
                UfEnv* prev_env = rt->current_env;
                rt->current_env = block_env;
                ExecResult res = execute_statement(rt, block_env, stmt->as.while_stmt.body);
                rt->current_env = prev_env;

                if (res.status == EXEC_RETURN || res.status == EXEC_ERROR) {
                    return res;
                }
                if (res.status == EXEC_BREAK) {
                    break;
                }
                /* EXEC_CONTINUE proceeds to next iteration */
            }
            return exec_ok();
        }

        case UF_STMT_REPEAT: {
            UfValue count_val = uf_evaluate_expression(rt, env, stmt->as.repeat_stmt.count_expr);
            if (rt->had_runtime_error) return exec_error();
            if (count_val.kind != UF_VAL_NUMBER) {
                uf_runtime_error(rt, stmt->span, "Repeat count must be a number, got '%s'", uf_val_type_name(count_val));
                return exec_error();
            }
            int64_t count = (int64_t)count_val.as.number;

            for (int64_t i = 0; i < count; ++i) {
                UfEnv* block_env = uf_env_create(rt, env);
                UfEnv* prev_env = rt->current_env;
                rt->current_env = block_env;
                ExecResult res = execute_statement(rt, block_env, stmt->as.repeat_stmt.body);
                rt->current_env = prev_env;

                if (res.status == EXEC_RETURN || res.status == EXEC_ERROR) {
                    return res;
                }
                if (res.status == EXEC_BREAK) {
                    break;
                }
                /* EXEC_CONTINUE proceeds to next iteration */
            }
            return exec_ok();
        }

        case UF_STMT_FOR: {
            UfValue iter_val = uf_evaluate_expression(rt, env, stmt->as.for_stmt.iterable);
            if (rt->had_runtime_error) return exec_error();
            uf_runtime_push_temp_root(rt, iter_val);

            if (iter_val.kind == UF_VAL_ARRAY) {
                UfArrayObject* arr = iter_val.as.array;
                for (size_t i = 0; i < arr->count; ++i) {
                    UfEnv* loop_env = uf_env_create(rt, env);
                    uf_env_declare(loop_env, stmt->as.for_stmt.var_name, arr->elements[i]);

                    UfEnv* prev_env = rt->current_env;
                    rt->current_env = loop_env;
                    ExecResult res = execute_statement(rt, loop_env, stmt->as.for_stmt.body);
                    rt->current_env = prev_env;

                    if (res.status == EXEC_RETURN || res.status == EXEC_ERROR) {
                        uf_runtime_pop_temp_root(rt);
                        return res;
                    }
                    if (res.status == EXEC_BREAK) break;
                }
            } else if (iter_val.kind == UF_VAL_MAP) {
                UfMapObject* map = iter_val.as.map;
                for (size_t i = 0; i < map->order_count; ++i) {
                    UfEnv* loop_env = uf_env_create(rt, env);
                    uf_env_declare(loop_env, stmt->as.for_stmt.var_name, map->order_keys[i]);

                    UfEnv* prev_env = rt->current_env;
                    rt->current_env = loop_env;
                    ExecResult res = execute_statement(rt, loop_env, stmt->as.for_stmt.body);
                    rt->current_env = prev_env;

                    if (res.status == EXEC_RETURN || res.status == EXEC_ERROR) {
                        uf_runtime_pop_temp_root(rt);
                        return res;
                    }
                    if (res.status == EXEC_BREAK) break;
                }
            } else if (iter_val.kind == UF_VAL_STRING) {
                UfStringObject* str = iter_val.as.string;
                for (size_t i = 0; i < str->length; ++i) {
                    /* Make the character first and root it: creating the
                     * loop scope may collect, and so may anything allocated
                     * while that scope is not yet current. */
                    char ch[2] = { str->chars[i], '\0' };
                    UfValue char_val = uf_val_string(rt, ch, 1);
                    uf_runtime_push_temp_root(rt, char_val);
                    UfEnv* loop_env = uf_env_create(rt, env);
                    uf_env_declare(loop_env, stmt->as.for_stmt.var_name, char_val);
                    uf_runtime_pop_temp_root(rt);

                    UfEnv* prev_env = rt->current_env;
                    rt->current_env = loop_env;
                    ExecResult res = execute_statement(rt, loop_env, stmt->as.for_stmt.body);
                    rt->current_env = prev_env;

                    if (res.status == EXEC_RETURN || res.status == EXEC_ERROR) {
                        uf_runtime_pop_temp_root(rt);
                        return res;
                    }
                    if (res.status == EXEC_BREAK) break;
                }
            } else {
                uf_runtime_error(rt, stmt->span, "'for' loop expects iterable (array, map, or string), got '%s'", uf_val_type_name(iter_val));
                uf_runtime_pop_temp_root(rt);
                return exec_error();
            }

            uf_runtime_pop_temp_root(rt);
            return exec_ok();
        }

        case UF_STMT_BREAK:
            return exec_break();

        case UF_STMT_CONTINUE:
            return exec_continue();

        case UF_STMT_FUNCTION: {
            UfValue fn = uf_val_function(rt,
                                         stmt->as.function_stmt.name,
                                         stmt->as.function_stmt.params,
                                         stmt->as.function_stmt.param_defaults,
                                         stmt->as.function_stmt.param_count,
                                         stmt->as.function_stmt.min_param_count,
                                         stmt->as.function_stmt.has_rest,
                                         stmt->as.function_stmt.body,
                                         env);
            fn.as.function->is_async = stmt->as.function_stmt.is_async;
            uf_env_declare(env, stmt->as.function_stmt.name, fn);
            return exec_ok();
        }

        case UF_STMT_RETURN: {
            UfValue val = uf_val_null();
            if (stmt->as.return_stmt.value) {
                val = uf_evaluate_expression(rt, env, stmt->as.return_stmt.value);
                if (rt->had_runtime_error) return exec_error();
            }
            return exec_return(val);
        }

        case UF_STMT_BLOCK:
            return execute_block(rt, env, stmt);

        case UF_STMT_TRY_CATCH: {
            if (rt->try_handler_count >= UF_MAX_TRY_HANDLERS) {
                uf_runtime_error(rt, stmt->span, "Maximum nested try-catch handlers exceeded");
                return exec_error();
            }

            UfTryHandler* h = &rt->try_handlers[rt->try_handler_count++];
            h->scope_env = env;
            h->frame_count = rt->frame_count;
            h->temp_root_count = rt->temp_root_count;

            ExecResult res = exec_ok();
            bool had_exception = false;
            UfValue caught_err = uf_val_null();

            if (setjmp(h->jmp) == 0) {
                res = execute_statement(rt, env, stmt->as.try_catch.try_block);
                if (rt->try_handler_count > 0 && &rt->try_handlers[rt->try_handler_count - 1] == h) {
                    rt->try_handler_count--;
                }
            } else {
                had_exception = true;
                rt->had_runtime_error = false;
                caught_err = rt->current_error;
                rt->current_error = uf_val_null();
            }

            /* An error still in flight once the catch clause is done with it:
             * either there was no catch clause, or the catch clause raised. The
             * finally block runs first and the error is rethrown afterwards. */
            volatile bool rethrow_pending = had_exception && !stmt->as.try_catch.catch_block;

            if (had_exception) {
                if (stmt->as.try_catch.catch_block) {
                    uf_runtime_push_temp_root(rt, caught_err);
                    UfEnv* catch_env = uf_env_create(rt, env);
                    if (stmt->as.try_catch.catch_var) {
                        uf_env_declare(catch_env, stmt->as.try_catch.catch_var, caught_err);
                    }
                    UfEnv* prev_env = rt->current_env;
                    rt->current_env = catch_env;
                    if (stmt->as.try_catch.finally_block && rt->try_handler_count < UF_MAX_TRY_HANDLERS) {
                        /* Guard the catch clause so an error raised inside it
                         * still runs the finally block before propagating,
                         * rather than longjmp-ing straight past it. */
                        UfTryHandler* guard = &rt->try_handlers[rt->try_handler_count++];
                        guard->scope_env = catch_env;
                        guard->frame_count = rt->frame_count;
                        guard->temp_root_count = rt->temp_root_count;
                        if (setjmp(guard->jmp) == 0) {
                            res = execute_statement(rt, catch_env, stmt->as.try_catch.catch_block);
                            if (rt->try_handler_count > 0 && &rt->try_handlers[rt->try_handler_count - 1] == guard) {
                                rt->try_handler_count--;
                            }
                        } else {
                            rethrow_pending = true;
                            res = exec_ok();
                            rt->had_runtime_error = false;
                            caught_err = rt->current_error;
                            rt->current_error = uf_val_null();
                        }
                    } else {
                        res = execute_statement(rt, catch_env, stmt->as.try_catch.catch_block);
                    }
                    rt->current_env = prev_env;
                    uf_runtime_pop_temp_roots(rt, 1);
                    if (!rethrow_pending && (res.status == EXEC_OK || res.status == EXEC_RETURN)) {
                        rt->had_runtime_error = false;
                    }
                }
            }

            if (stmt->as.try_catch.finally_block) {
                /* While the finally block runs, a pending return value and an
                 * error still to be rethrown are held only here. */
                uf_runtime_push_temp_root(rt, res.value);
                uf_runtime_push_temp_root(rt, caught_err);
                ExecResult fin_res = execute_statement(rt, env, stmt->as.try_catch.finally_block);
                uf_runtime_pop_temp_roots(rt, 2);
                if (fin_res.status == EXEC_RETURN || fin_res.status == EXEC_ERROR ||
                    fin_res.status == EXEC_BREAK || fin_res.status == EXEC_CONTINUE) {
                    res = fin_res;
                } else if (rethrow_pending) {
                    if (!rt->had_runtime_error) {
                        rt->had_runtime_error = true;
                        rt->current_error = caught_err;
                        if (rt->try_handler_count > 0) {
                            UfTryHandler* outer = &rt->try_handlers[--rt->try_handler_count];
                            rt->frame_count = outer->frame_count;
                            rt->temp_root_count = outer->temp_root_count;
                            rt->current_env = outer->scope_env;
                            longjmp(outer->jmp, 1);
                        } else {
                            rt->had_runtime_error = true;
                            if (caught_err.kind == UF_VAL_ERROR && caught_err.as.error) {
                                const char* msg = caught_err.as.error->message ? caught_err.as.error->message->chars : "Error";
                                SourceSpan sp = {
                                    .start = { .line = (uint32_t)caught_err.as.error->line, .col = 1, .file = caught_err.as.error->file },
                                    .end = { .line = (uint32_t)caught_err.as.error->line, .col = 1, .file = caught_err.as.error->file }
                                };
                                if (rt->reporter) {
                                    uf_report_diag(rt->reporter, UF_DIAG_RUNTIME_ERROR, sp, msg, NULL);
                                } else {
                                    fprintf(rt->err_stream, "Runtime Error: %s\n", msg);
                                }
                            } else {
                                char* err_str = uf_val_to_string(caught_err);
                                fprintf(rt->err_stream, "Runtime Error: %s\n", err_str ? err_str : "Error");
                                free(err_str);
                            }
                            return exec_error();
                        }
                    }
                }
            }

            return res;
        }

        case UF_STMT_IMPORT: {
            UfModuleObject* mod = uf_module_load(rt, stmt->as.import_stmt.module_name, stmt->span);
            if (!mod || rt->had_runtime_error) return exec_error();
            const char* bound = stmt->as.import_stmt.alias ? stmt->as.import_stmt.alias : stmt->as.import_stmt.module_name;
            uf_env_declare(env, bound, uf_val_module(rt, mod));
            return exec_ok();
        }

        case UF_STMT_FROM_IMPORT: {
            UfModuleObject* mod = uf_module_load(rt, stmt->as.from_import_stmt.module_name, stmt->span);
            if (!mod || rt->had_runtime_error) return exec_error();
            for (size_t i = 0; i < stmt->as.from_import_stmt.count; ++i) {
                const char* sym = stmt->as.from_import_stmt.symbols[i];
                UfValue sym_key = uf_val_string(rt, sym, strlen(sym));
                if (!uf_map_has(mod->exports.as.map, sym_key)) {
                    uf_runtime_raise(rt, "ImportError", stmt->span, "Cannot import name '%s' from module '%s'", sym, stmt->as.from_import_stmt.module_name);
                    return exec_error();
                }
                UfValue val = uf_map_get(mod->exports.as.map, sym_key);
                const char* bound = (stmt->as.from_import_stmt.aliases && stmt->as.from_import_stmt.aliases[i])
                                     ? stmt->as.from_import_stmt.aliases[i]
                                     : sym;
                uf_env_declare(env, bound, val);
            }
            return exec_ok();
        }

        case UF_STMT_TRAIT: {
            execute_trait_def(rt, env, stmt);
            return exec_ok();
        }

        case UF_STMT_STRUCT: {
            execute_struct_def(rt, env, stmt);
            return exec_ok();
        }

        case UF_STMT_IMPL: {
            execute_impl_def(rt, env, stmt);
            return exec_ok();
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
            UfValue edef = uf_val_enum_def(rt, stmt->as.enum_stmt.name, vcount, vnames, vfcounts, vfnames, vftypes);
            uf_env_declare(env, stmt->as.enum_stmt.name, edef);
            for (size_t i = 0; i < vcount; ++i) {
                UfValue val_tmpl = uf_val_enum_val(rt, edef.as.enum_def, (int)i, vnames[i], NULL, 0);
                uf_env_declare(env, vnames[i], val_tmpl);
            }
            return exec_ok();
        }

        case UF_STMT_MATCH: {
            UfValue val = uf_evaluate_expression(rt, env, stmt->as.match_stmt.expr);
            if (rt->had_runtime_error) return exec_error();
            uf_runtime_push_temp_root(rt, val);

            bool matched = false;
            ExecResult arm_res = exec_ok();

            for (size_t i = 0; i < stmt->as.match_stmt.arm_count; ++i) {
                /* The arm's scope is reachable only through current_env while
                 * its pattern binds, its guard runs and its body executes. */
                UfEnv* arm_env = uf_env_create(rt, env);
                UfEnv* prev_env = rt->current_env;
                rt->current_env = arm_env;
                if (match_pattern_and_bind(rt, arm_env, stmt->as.match_stmt.arms[i].pattern, val)) {
                    bool guard_ok = true;
                    if (stmt->as.match_stmt.arms[i].guard) {
                        UfValue gval = uf_evaluate_expression(rt, arm_env, stmt->as.match_stmt.arms[i].guard);
                        if (rt->had_runtime_error) {
                            rt->current_env = prev_env;
                            uf_runtime_pop_temp_root(rt);
                            return exec_error();
                        }
                        guard_ok = uf_val_is_truthy(gval);
                    }
                    if (guard_ok) {
                        matched = true;
                        arm_res = execute_statement(rt, arm_env, stmt->as.match_stmt.arms[i].body);
                        rt->current_env = prev_env;
                        break;
                    }
                }
                rt->current_env = prev_env;
            }

            uf_runtime_pop_temp_root(rt);

            if (matched) {
                return arm_res;
            }

            if (stmt->as.match_stmt.else_branch) {
                return execute_statement(rt, env, stmt->as.match_stmt.else_branch);
            }

            return exec_ok();
        }
    }

    return exec_ok();
}

UfInterpretResult uf_interpret_program(UfRuntime* rt, const UfProgram* program) {
    rt->call_fn = uf_call_value;
    UfEnv* prev_env = rt->current_env;
    UfEnv* exec_env = (rt->current_env != NULL) ? rt->current_env : rt->global_env;
    rt->current_env = exec_env;

    /* Pass 1: Hoist top-level function and struct declarations */
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_FUNCTION) {
            UfValue fn = uf_val_function(rt,
                                         stmt->as.function_stmt.name,
                                         stmt->as.function_stmt.params,
                                         stmt->as.function_stmt.param_defaults,
                                         stmt->as.function_stmt.param_count,
                                         stmt->as.function_stmt.min_param_count,
                                         stmt->as.function_stmt.has_rest,
                                         stmt->as.function_stmt.body,
                                         exec_env);
            fn.as.function->is_async = stmt->as.function_stmt.is_async;
            uf_env_declare(exec_env, stmt->as.function_stmt.name, fn);
        } else if (stmt->kind == UF_STMT_TRAIT) {
            execute_trait_def(rt, exec_env, stmt);
        } else if (stmt->kind == UF_STMT_STRUCT) {
            execute_struct_def(rt, exec_env, stmt);
        } else if (stmt->kind == UF_STMT_IMPL) {
            execute_impl_def(rt, exec_env, stmt);
        } else if (stmt->kind == UF_STMT_ENUM) {
            size_t vcount = stmt->as.enum_stmt.variant_count;
            const char** vnames = (const char**)malloc(vcount * sizeof(const char*));
            size_t* vfcounts = (size_t*)malloc(vcount * sizeof(size_t));
            const char*** vfnames = (const char***)malloc(vcount * sizeof(const char**));
            const char*** vftypes = (const char***)malloc(vcount * sizeof(const char**));
            for (size_t v = 0; v < vcount; ++v) {
                vnames[v] = stmt->as.enum_stmt.variants[v].name;
                vfcounts[v] = stmt->as.enum_stmt.variants[v].field_count;
                vfnames[v] = stmt->as.enum_stmt.variants[v].field_names;
                vftypes[v] = stmt->as.enum_stmt.variants[v].field_types;
            }
            UfValue edef = uf_val_enum_def(rt, stmt->as.enum_stmt.name, vcount, vnames, vfcounts, vfnames, vftypes);
            uf_env_declare(exec_env, stmt->as.enum_stmt.name, edef);
            for (size_t v = 0; v < vcount; ++v) {
                UfValue val_tmpl = uf_val_enum_val(rt, edef.as.enum_def, (int)v, vnames[v], NULL, 0);
                uf_env_declare(exec_env, vnames[v], val_tmpl);
            }
        }
    }

    /* Pass 2: Execute statements in order (skipping hoisted functions, traits, structs, impls, and enums) */
    for (size_t i = 0; i < program->count; ++i) {
        if (program->stmts[i]->kind == UF_STMT_FUNCTION ||
            program->stmts[i]->kind == UF_STMT_TRAIT ||
            program->stmts[i]->kind == UF_STMT_STRUCT ||
            program->stmts[i]->kind == UF_STMT_IMPL ||
            program->stmts[i]->kind == UF_STMT_ENUM) {
            continue;
        }
        ExecResult res = execute_statement(rt, exec_env, program->stmts[i]);
        if (res.status == EXEC_ERROR || rt->had_runtime_error) {
            rt->current_env = prev_env;
            return UF_INTERPRET_RUNTIME_ERROR;
        }
        if (res.status == EXEC_RETURN) {
            rt->current_env = prev_env;
            return UF_INTERPRET_OK;
        }
    }

    rt->current_env = prev_env;
    return UF_INTERPRET_OK;
}
