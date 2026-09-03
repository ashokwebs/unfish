#include "uf_interpreter.h"
#include "uf_module.h"
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

    uf_runtime_push_temp_root(rt, callee);
    for (size_t i = 0; i < argc; ++i) {
        uf_runtime_push_temp_root(rt, args[i]);
    }

    UfValue result = uf_val_null();

    if (callee.kind == UF_VAL_FUNCTION) {
        UfFunctionObject* fn = callee.as.function;
        if (fn->param_count != argc) {
            uf_runtime_error(rt, span, "Function '%s' expects %zu arguments, but %zu provided",
                             fn->name ? fn->name : "anonymous", fn->param_count, argc);
        } else {
            UfEnv* call_env = uf_env_create(rt, fn->closure_env);
            if (uf_runtime_push_frame(rt, fn->name ? fn->name : "<anonymous>", span, call_env)) {
                UfEnv* prev_env = rt->current_env;
                rt->current_env = call_env;

                if (fn->name) {
                    uf_env_declare(call_env, fn->name, callee);
                }
                for (size_t i = 0; i < argc; ++i) {
                    uf_env_declare(call_env, fn->params[i], args[i]);
                }

                ExecResult body_res = execute_statement(rt, call_env, fn->body);
                uf_runtime_pop_frame(rt);
                rt->current_env = prev_env;

                if (body_res.status == EXEC_RETURN) {
                    result = body_res.value;
                } else {
                    result = uf_val_null();
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
    } else {
        uf_runtime_error(rt, span, "Cannot call non-function of type '%s'", uf_val_type_name(callee));
    }

    uf_runtime_pop_temp_roots(rt, 1 + argc);
    return result;
}

static UfValue evaluate_call(UfRuntime* rt, UfEnv* env, const UfExpr* expr) {
    UfValue callee = uf_evaluate_expression(rt, env, expr->as.call.callee);
    if (rt->had_runtime_error) {
        return uf_val_null();
    }
    uf_runtime_push_temp_root(rt, callee);

    size_t argc = expr->as.call.argc;
    UfValue args[64];
    for (size_t i = 0; i < argc; ++i) {
        args[i] = uf_evaluate_expression(rt, env, expr->as.call.args[i]);
        if (rt->had_runtime_error) {
            uf_runtime_pop_temp_roots(rt, 1 + i);
            return uf_val_null();
        }
        uf_runtime_push_temp_root(rt, args[i]);
    }

    UfValue result = uf_call_value(rt, callee, argc, args, expr->span);
    uf_runtime_pop_temp_roots(rt, 1 + argc);
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
                    if (left.kind != UF_VAL_NUMBER || right.kind != UF_VAL_NUMBER) {
                        uf_runtime_error(rt, expr->span, "Comparison operands must be numbers, got '%s' and '%s'",
                                         uf_val_type_name(left), uf_val_type_name(right));
                    } else {
                        double a = left.as.number;
                        double b = right.as.number;

                        if (expr->as.binary.op == UF_TOK_LT)   result = uf_val_bool(a < b);
                        if (expr->as.binary.op == UF_TOK_LTEQ) result = uf_val_bool(a <= b);
                        if (expr->as.binary.op == UF_TOK_GT)   result = uf_val_bool(a > b);
                        if (expr->as.binary.op == UF_TOK_GTEQ) result = uf_val_bool(a >= b);
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
                UfValue elem = uf_evaluate_expression(rt, env, expr->as.array_lit.elements[i]);
                if (rt->had_runtime_error) {
                    uf_runtime_pop_temp_root(rt);
                    return uf_val_null();
                }
                uf_array_push(rt, arr_val.as.array, elem);
            }

            uf_runtime_pop_temp_root(rt);
            return arr_val;
        }

        case UF_EXPR_MAP: {
            UfValue map_val = uf_val_map(rt, expr->as.map_lit.count);
            uf_runtime_push_temp_root(rt, map_val);

            for (size_t i = 0; i < expr->as.map_lit.count; ++i) {
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
                    if (fidx < 0) {
                        uf_runtime_error(rt, expr->span, "Struct '%s' has no field '%s'", inst->def->name, fname);
                    } else {
                        result = inst->fields[fidx];
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
                                         expr->as.fn_expr.param_count,
                                         expr->as.fn_expr.body,
                                         env);
            return fn;
        }
    }

    return uf_val_null();
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

static ExecResult execute_statement(UfRuntime* rt, UfEnv* env, const UfStmt* stmt) {
    if (!stmt || rt->had_runtime_error) {
        return exec_error();
    }

    /* Execution quota check */
    if (rt->max_steps > 0 && ++rt->step_count > rt->max_steps) {
        uf_runtime_error(rt, stmt->span, "ExecutionQuotaExceeded: Program exceeded maximum step quota (%llu steps)", (unsigned long long)rt->max_steps);
        return exec_error();
    }

    switch (stmt->kind) {
        case UF_STMT_LET: {
            UfValue init_val = uf_val_null();
            if (stmt->as.let_stmt.init) {
                init_val = uf_evaluate_expression(rt, env, stmt->as.let_stmt.init);
                if (rt->had_runtime_error) return exec_error();
                uf_runtime_push_temp_root(rt, init_val);
            }
            uf_env_declare(env, stmt->as.let_stmt.name, init_val);
            if (stmt->as.let_stmt.init) {
                uf_runtime_pop_temp_root(rt);
            }
            return exec_ok();
        }

        case UF_STMT_ASSIGN: {
            UfValue val = uf_evaluate_expression(rt, env, stmt->as.assign_stmt.value);
            if (rt->had_runtime_error) return exec_error();
            uf_runtime_push_temp_root(rt, val);
            if (!uf_env_assign(env, stmt->as.assign_stmt.name, val)) {
                uf_runtime_error(rt, stmt->span, "Cannot assign to undefined identifier '%s'", stmt->as.assign_stmt.name);
                uf_runtime_pop_temp_root(rt);
                return exec_error();
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
                    UfEnv* loop_env = uf_env_create(rt, env);
                    char ch[2] = { str->chars[i], '\0' };
                    UfValue char_val = uf_val_string(rt, ch, 1);
                    uf_env_declare(loop_env, stmt->as.for_stmt.var_name, char_val);

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
                                         stmt->as.function_stmt.param_count,
                                         stmt->as.function_stmt.body,
                                         env);
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

            if (setjmp(h->jmp) == 0) {
                ExecResult res = execute_statement(rt, env, stmt->as.try_catch.try_block);
                if (rt->try_handler_count > 0 && &rt->try_handlers[rt->try_handler_count - 1] == h) {
                    rt->try_handler_count--;
                }
                return res;
            } else {
                /* Exception was caught via longjmp */
                UfValue caught_err = rt->current_error;
                uf_runtime_push_temp_root(rt, caught_err);
                UfEnv* catch_env = uf_env_create(rt, env);
                if (stmt->as.try_catch.catch_var) {
                    uf_env_declare(catch_env, stmt->as.try_catch.catch_var, caught_err);
                }
                UfEnv* prev_env = rt->current_env;
                rt->current_env = catch_env;
                ExecResult res = execute_statement(rt, catch_env, stmt->as.try_catch.catch_block);
                rt->current_env = prev_env;
                uf_runtime_pop_temp_roots(rt, 1);
                rt->current_error = uf_val_null();
                rt->had_runtime_error = false;
                return res;
            }
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

        case UF_STMT_STRUCT: {
            UfValue sdef = uf_val_struct_def(rt,
                                             stmt->as.struct_stmt.name,
                                             stmt->as.struct_stmt.field_names,
                                             stmt->as.struct_stmt.field_types,
                                             stmt->as.struct_stmt.field_count);
            uf_env_declare(env, stmt->as.struct_stmt.name, sdef);
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

    /* Pass 1: Hoist top-level function declarations */
    for (size_t i = 0; i < program->count; ++i) {
        UfStmt* stmt = program->stmts[i];
        if (stmt->kind == UF_STMT_FUNCTION) {
            UfValue fn = uf_val_function(rt,
                                         stmt->as.function_stmt.name,
                                         stmt->as.function_stmt.params,
                                         stmt->as.function_stmt.param_count,
                                         stmt->as.function_stmt.body,
                                         exec_env);
            uf_env_declare(exec_env, stmt->as.function_stmt.name, fn);
        }
    }

    /* Pass 2: Execute statements in order (skipping hoisted functions) */
    for (size_t i = 0; i < program->count; ++i) {
        if (program->stmts[i]->kind == UF_STMT_FUNCTION) {
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
