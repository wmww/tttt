#include "engine.h"
#include "scope.h"
#include "statement.h"
#include "expr.h"
#include "value.h"
#include "result.h"

typedef arc_Object StackFrame;
#define StackFrame_drop arc_Object_drop
#define StackFrame_clone arc_Object_clone

static inline StackFrame StackFrame_new() { return Value_object_empty().data.object; }

#define i_key_class StackFrame
#include "stc/vec.h"

typedef struct Engine {
    vec_StackFrame stack;
} Engine;

void Engine_drop(Engine* self) {
    vec_StackFrame_drop(&self->stack);
}

ObjectValue* Engine_get_locals(Engine* self) {
    return vec_StackFrame_back(&self->stack)->get;
}

static Result MUST_USE Engine_eval_expr(Engine* self, Expr const* expr);
static Result MUST_USE Engine_run_scope(Engine* self, Scope const* scope);
static Result MUST_USE Engine_run_scope_in_stack_frame(Engine* self, Scope const* scope);

static bool contains_only_digits(const char* str) {
    if (!*str) {
        return false;
    }
    str++;
    while (*str) {
        if (*str < '0' || *str > '9') {
            return false;
        }
        str++;
    }
    return true;
}

static Result MUST_USE Engine_eval_identifier(Engine* self, csview identifier) {
    c_foreach_rev(i, vec_StackFrame, self->stack) {
        Result get_result = ObjectValue_get(i.ref->get, identifier);
        if (get_result.tag == RESULT_OK) {
            return get_result;
        }
        // No need to drop the non-ok result since it's value is always undefined
    }
    return Result_err(Value_string(cstr_from_fmt(
        "ReferenceError: %s is not defined",
        identifier.buf
    )));
}

static Result MUST_USE Engine_assign_identifier(Engine* self, csview identifier, Value value) {
    c_foreach_rev(i, vec_StackFrame, self->stack) {
        if (ObjectValue_has(i.ref->get, identifier)) {
            ObjectValue_set(i.ref->get, cstr_from_sv(identifier), value);
            return Result_ok_undefined;
        }
    }
    // If we were in strict mode this would be an error
    ObjectValue_set(vec_StackFrame_back(&self->stack)->get, cstr_from_sv(identifier), value);
    return Result_ok_undefined;
}

// Returns Result_ok_undefined on success, not the value. Consumes value.
static Result MUST_USE Engine_assign(Engine* self, Expr const* target, Value value) {
    switch (target->tag) {
        case EXPR_IDENTIFIER: return Engine_assign_identifier(self, cstr_sv(&target->data.identifier), value);

        case EXPR_DOT_PROP: {
            TRY_THROW(target_value, Engine_eval_expr(self, &target->data.dot_prop->expr));
            switch (target_value.tag) {
                case VALUE_UNDEFINED:
                case VALUE_NULL:
                    Value_drop(&value);
                    Value_drop(&target_value);
                    return Result_err(Value_string(
                        target_value.tag == VALUE_UNDEFINED
                        ? cstr_lit("TypeError: foo is undefined")
                        : cstr_lit("TypeError: foo is null")
                    ));

                case VALUE_ARRAY:
                case VALUE_JS_FUNC:
                case VALUE_NATIVE_FUNC:
                    // TODO: these should support properties, but for now fall through

                case VALUE_BOOLEAN:
                case VALUE_NUMBER:
                case VALUE_STRING:
                    // Return the value, but do not assign it to anything (that's what browsers seem to do)
                    Value_drop(&target_value);
                    return Result_ok_undefined;

                case VALUE_OBJECT:
                    ObjectValue_set(target_value.data.object.get, cstr_clone(target->data.identifier), value);
                    return Result_ok_undefined;
            }
            UNREACHABLE;
        }

        default: return Result_err(Value_string(cstr_lit("SyntaxError: invalid assignment left-hand side")));
    }
    UNREACHABLE;
}

static Result MUST_USE Engine_eval_binary_op(Engine* self, BinaryOpExpr const* op_expr) {
    if (op_expr->op == BINARY_OP_SINGLE_EQUAL) {
        TRY_THROW(right, Engine_eval_expr(self, &op_expr->right));
        TRY_THROW_DROP(Engine_assign(self, &op_expr->left, right));
        return Result_ok(Value_clone(right));
    }
    TRY_THROW(left, Engine_eval_expr(self, &op_expr->left));
    TRY_CATCH(right, Engine_eval_expr(self, &op_expr->right), res_err, {
        Value_drop(&left);
        return res_err;
    });
    Value ret = Value_undefined;
    switch (op_expr->op) {
        case BINARY_OP_PLUS:
            if (left.tag == VALUE_NUMBER && right.tag == VALUE_NUMBER) {
                ret = Value_number(left.data.number + right.data.number);
            } else {
                cstr str = cstr_null;
                cstr_append_value_js(&str, &left);
                cstr_append_value_js(&str, &right);
                ret = Value_string(str);
            }
            break;
        case BINARY_OP_MINUS: ret = Value_number(Value_to_number(&left) - Value_to_number(&right)); break;
        case BINARY_OP_STAR: ret = Value_number(Value_to_number(&left) * Value_to_number(&right)); break;
        case BINARY_OP_SLASH: ret = Value_number(Value_to_number(&left) / Value_to_number(&right)); break;
        case BINARY_OP_SINGLE_EQUAL: UNREACHABLE;
        case BINARY_OP_DOUBLE_EQUAL: ret = Value_boolean(Value_loose_eq(&left, &right)); break;
        case BINARY_OP_TRIPLE_EQUAL: ret = Value_boolean(Value_strict_eq(&left, &right)); break;
        case BINARY_OP_LESS: ret = Value_boolean(Value_to_number(&left) < Value_to_number(&right)); break;
        case BINARY_OP_GREATER: ret = Value_boolean(Value_to_number(&left) > Value_to_number(&right)); break;
        case BINARY_OP_LESS_EQUAL: ret = Value_boolean(Value_to_number(&left) <= Value_to_number(&right)); break;
        case BINARY_OP_GREATER_EQUAL: ret = Value_boolean(Value_to_number(&left) >= Value_to_number(&right)); break;
        case BINARY_OP_NOT_EQUAL: ret = Value_boolean(!Value_loose_eq(&left, &right)); break;
        case BINARY_OP_DOUBLE_NOT_EQUAL: ret = Value_boolean(!Value_strict_eq(&left, &right)); break;
    }
    Value_drop(&left);
    Value_drop(&right);
    return Result_ok(ret);
}

static Result MUST_USE Engine_eval_inc_dec(
    Engine* self,
    Expr const* target,
    Value const* previous,
    int before_ret,
    int after_ret
) {
    double num = Value_to_number(previous) + before_ret;
    TRY_THROW_DROP(Engine_assign(self, target, Value_number(num + after_ret)));
    return Result_ok(Value_number(num));
}

static Result MUST_USE Engine_eval_unary_op(Engine* self, UnaryOpExpr const* op_expr) {
    TRY_THROW(value, Engine_eval_expr(self, &op_expr->expr));
    Result res = Result_ok_undefined;
    switch (op_expr->op) {
        case UNARY_OP_BANG: res.value = Value_boolean(!Value_to_boolean(&value)); break;
        case UNARY_OP_PLUS: res.value = Value_number(Value_to_number(&value)); break;
        case UNARY_OP_MINUS: res.value = Value_number(-Value_to_number(&value)); break;
        case UNARY_OP_LEFT_PLUS_PLUS: res = Engine_eval_inc_dec(self, &op_expr->expr, &value, 1, 0); break;
        case UNARY_OP_LEFT_MINUS_MINUS: res = Engine_eval_inc_dec(self, &op_expr->expr, &value, -1, 0); break;
        case UNARY_OP_RIGHT_PLUS_PLUS: res = Engine_eval_inc_dec(self, &op_expr->expr, &value, 0, 1); break;
        case UNARY_OP_RIGHT_MINUS_MINUS: res = Engine_eval_inc_dec(self, &op_expr->expr, &value, 0, -1); break;
    }
    Value_drop(&value);
    return res;
}

static Result MUST_USE Engine_eval_dot_prop(Engine* self, DotPropExpr const* prop_expr) {
    TRY_THROW(value, Engine_eval_expr(self, &prop_expr->expr));
    Result ret = {0};
    switch (value.tag) {
        case VALUE_NULL: ret = Result_err(Value_string(cstr_lit("TypeError: null has no properties"))); break;
        case VALUE_UNDEFINED: ret = Result_err(Value_string(cstr_lit("TypeError: undefined has no properties"))); break;

        case VALUE_BOOLEAN:
        case VALUE_NUMBER:
        case VALUE_STRING:
        case VALUE_JS_FUNC:
        case VALUE_NATIVE_FUNC: ret.value = Value_undefined; break;

        case VALUE_OBJECT:
            ret = ObjectValue_get(value.data.object.get, cstr_sv(&prop_expr->prop));
            ret.tag = RESULT_OK; // undefined is fine if key is not found
            break;

        case VALUE_ARRAY:
            if (strcmp(cstr_str(&prop_expr->prop), "length") == 0) {
                ret.value = Value_number(vec_Value_size(&value.data.array.get->vec));
            } else {
                // TODO: support arbitrary properties on arrays
                ret.value = Value_undefined;
            }
            break;
    }
    Value_drop(&value);
    return ret;
}

static Result MUST_USE Engine_eval_native_func(Engine* self, NativeFuncValue const* func, vec_Value const* args) {
    return func->func(self, vec_Value_size(args), args->data, func->data);
}

static Result MUST_USE Engine_eval_js_func(Engine* self, JsFuncValue const* func, vec_Value const* args) {
    vec_StackFrame_push(&self->stack, StackFrame_new());
    int defined_arg_num = vec_FunctionStatementArg_size(func->args);
    int given_arg_num = vec_Value_size(args);
    ObjectValue* locals = Engine_get_locals(self);
    for (int i = 0; i < defined_arg_num; i++) {
        FunctionStatementArg const* defined = vec_FunctionStatementArg_at(func->args, i);
        cstr name = cstr_clone(defined->data.name);
        Value value = Value_undefined;
        if (i < given_arg_num) {
            // TODO: don't clone all args
            value = Value_clone(*vec_Value_at(args, i));
        } else if (defined->data.has_value) {
            TRY_CATCH(default_value, Engine_eval_expr(self, &defined->data.value), res_err, {
                cstr_drop(&name);
                vec_StackFrame_pop(&self->stack);
                return res_err;
            });
            value = default_value;
        }
        ObjectValue_set(locals, name, value);
    }
    Result res = Engine_run_scope(self, func->code);
    vec_StackFrame_pop(&self->stack);
    return res;
}

static Result MUST_USE Engine_eval_call(Engine* self, CallExpr const* call_expr) {
    TRY_THROW(callable, Engine_eval_expr(self, &call_expr->callable));

    int given_arg_num = vec_Expr_size(&call_expr->args);
    vec_Value given_args = vec_Value_with_capacity(given_arg_num);
    c_foreach(i, vec_Expr, call_expr->args) {
        TRY_CATCH(value, Engine_eval_expr(self, i.ref), res_err, {
            vec_Value_drop(&given_args);
            Value_drop(&callable);
            return res_err;
        });
        vec_Value_push(&given_args, value);
    }

    Result ret = Result_ok_undefined;
    switch (callable.tag) {
        case VALUE_JS_FUNC:
            ret = Engine_eval_js_func(self, callable.data.js_func.get, &given_args);
            break;

        case VALUE_NATIVE_FUNC:
            ret = Engine_eval_native_func(self, callable.data.native_func.get, &given_args);
            break;

        default: {
            cstr message = cstr_null;
            cstr_append_value_js(&message, &callable);
            cstr_append_sv(&message, c_sv(" is not a function"));
            ret = Result_err(Value_string(message));
        }
    }

    vec_Value_drop(&given_args);
    Value_drop(&callable);
    if (ret.tag == RESULT_RETURN) {
        ret.tag = RESULT_OK;
    }
    return ret;
}

static Result MUST_USE Engine_eval_object(Engine* self, ObjectExpr const* obj_expr) {
    Value ret = Value_object_empty();
    c_foreach(i, vec_ObjectExprItem, *obj_expr) {
        TRY_CATCH(value, Engine_eval_expr(self, &i.ref->value), res_err, {
            Value_drop(&ret);
            return res_err;
        });
        ObjectValue_set(ret.data.object.get, cstr_clone(i.ref->name), value);
    }
    return Result_ok(ret);
}

static Result MUST_USE Engine_eval_expr(Engine* self, Expr const* expr) {
    switch (expr->tag) {
        case EXPR_LITERAL: return Result_ok(Value_clone(expr->data.literal));
        case EXPR_IDENTIFIER: return Engine_eval_identifier(self, cstr_sv(&expr->data.identifier));
        case EXPR_BINARY_OP: return Engine_eval_binary_op(self, expr->data.binary_op);
        case EXPR_UNARY_OP: return Engine_eval_unary_op(self, expr->data.unary_op);
        case EXPR_DOT_PROP: return Engine_eval_dot_prop(self, expr->data.dot_prop);
        case EXPR_CALL: return Engine_eval_call(self, expr->data.call);
        case EXPR_OBJECT: return Engine_eval_object(self, &expr->data.object);
    }
    UNREACHABLE;
}

// Only return statements return ok with a value, caller has to check statement tag to notice undefined returns
static Result MUST_USE Engine_run_statement(Engine* self, Statement const* statement) {
    switch (statement->tag) {
        case STATEMENT_NOOP: break;
        case STATEMENT_INTERFACE: break;

        case STATEMENT_SCOPE: {
            TRY_THROW_DROP(Engine_run_scope_in_stack_frame(self, &statement->data.scope));
        }   break;

        case STATEMENT_EXPR: {
            TRY_THROW_DROP(Engine_eval_expr(self, &statement->data.expr));
        }   break;

        case STATEMENT_RETURN: {
            TRY_THROW(value, Engine_eval_expr(self, &statement->data.return_expr));
            return Result_return(value);
        }

        case STATEMENT_VAR_INIT: {
            // TODO: respect keyword (let, const, var)
            TRY_THROW(value, Engine_eval_expr(self, &statement->data.var_init.data.value));
            cstr name = cstr_clone(statement->data.var_init.data.name);
            ObjectValue* locals = Engine_get_locals(self);
            ObjectValue_set(locals, name, value);
        }   break;

        case STATEMENT_FUNCTION: {
            cstr name = cstr_clone(statement->data.function.name);
            Value value = Value_js_func(
                &statement->data.function.args,
                &statement->data.function.body
            );
            ObjectValue* locals = Engine_get_locals(self);
            ObjectValue_set(locals, name, value);
        }   break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_Statement_size(&statement->data.if_else.statements);
            int cond_size = vec_Expr_size(&statement->data.if_else.conditions);
            if (statement_size != cond_size && statement_size != cond_size + 1) {
                PANIC("invalid if statement has %d conditions and %d statements", cond_size, statement_size);
            }
            for (int i = 0; i < statement_size; i++) {
                bool cond_true = true;
                if (i < cond_size) {
                    TRY_THROW(condition, Engine_eval_expr(self, vec_Expr_at(&statement->data.if_else.conditions, i)));
                    cond_true = Value_to_boolean(&condition);
                    Value_drop(&condition);
                }
                if (cond_true) {
                    TRY_THROW_DROP(Engine_run_statement(
                        self,
                        vec_Statement_at(&statement->data.if_else.statements, i)
                    ));
                    break;
                }
            }
        }   break;

        case STATEMENT_LOOP: {
            vec_StackFrame_push(&self->stack, StackFrame_new());
            TRY_CATCH_DROP(Engine_run_statement(self, &statement->data.loop->initialization), res_err, {
                vec_StackFrame_pop(&self->stack);
                return res_err;
            });
            while (true) {
                TRY_CATCH(keep_going, Engine_eval_expr(self, &statement->data.loop->condition), res_err, {
                    vec_StackFrame_pop(&self->stack);
                    return res_err;
                });
                if (!Value_to_boolean(&keep_going)) {
                    break;
                }
                TRY_CATCH_DROP(Engine_run_statement(self, &statement->data.loop->body), res_err, {
                    vec_StackFrame_pop(&self->stack);
                    if (res_err.tag == RESULT_BREAK) {
                        return Result_ok_undefined;
                    } else {
                        return res_err;
                    }
                });
                TRY_CATCH_DROP(Engine_run_statement(self, &statement->data.loop->afterthought), res_err, {
                    vec_StackFrame_pop(&self->stack);
                    return res_err;
                });
            }
        }   break;

        case STATEMENT_BREAK: return Result_break();
    }
    return Result_ok_undefined;
}

static Result MUST_USE Engine_run_scope(Engine* self, Scope const* scope) {
    c_foreach(i, vec_Statement, scope->statements) {
        TRY_THROW_DROP(Engine_run_statement(self, i.ref));
    }
    return Result_ok_undefined;
}

static Result MUST_USE Engine_run_scope_in_stack_frame(Engine* self, Scope const* scope) {
    vec_StackFrame_push(&self->stack, StackFrame_new());
    TRY_CATCH_DROP(Engine_run_scope(self, scope), res_err, {
        vec_StackFrame_pop(&self->stack);
        return res_err;
    });
    vec_StackFrame_pop(&self->stack);
    return Result_ok_undefined;
}

static Result console_log_impl(Engine* engine, int argc, Value const argv[], void* data) {
    (void)engine; (void)data;
    cstr str = cstr_null;
    for (int i = 0; i < argc; i++) {
        cstr_append_value_js(&str, &argv[i]);
        if (i < argc - 1) {
            cstr_append_sv(&str, c_sv(" "));
        }
    }
    printf("%s\n", cstr_str(&str));
    cstr_drop(&str);
    return Result_ok_undefined;
}

static Result string_impl(Engine* engine, int argc, Value const argv[], void* data) {
    (void)engine; (void)data;
    if (argc > 0) {
        cstr ret = cstr_null;
        cstr_append_value_js(&ret, argv);
        return Result_ok(Value_string(ret));
    } else {
        return Result_ok(Value_string(cstr_null));
    }
}

static Result number_impl(Engine* engine, int argc, Value const argv[], void* data) {
    (void)engine; (void)data;
    return Result_ok(Value_number(argc > 0 ? Value_to_number(argv) : 0));
}

static Result boolean_impl(Engine* engine, int argc, Value const argv[], void* data) {
    (void)engine; (void)data;
    return Result_ok(Value_boolean(argc > 0 ? Value_to_boolean(argv) : false));
}

void eval_program(Scope const* scope) {
    Engine engine = {
        .stack = c_init(vec_StackFrame, {
            Value_object_init({
                {"console", Value_object_init({
                    {"log", Value_native_func(console_log_impl, nullptr, nullptr)}
                })},
                {"String", Value_native_func(string_impl, nullptr, nullptr)},
                {"Number", Value_native_func(number_impl, nullptr, nullptr)},
                {"Boolean", Value_native_func(boolean_impl, nullptr, nullptr)},
            }).data.object,
        }),
    };
    Result res = Engine_run_scope(&engine, scope);
    if (res.tag == RESULT_ERR) {
        cstr err = cstr_null;
        cstr_append_value_js(&err, &res.value);
        printf("Error thrown: %s\n", cstr_str(&err));
        cstr_drop(&err);
    }
    Result_drop(&res);
    Engine_drop(&engine);
}
