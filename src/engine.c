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

static Result MUST_USE Engine_eval_identifier(Engine* self, csview identifier) {
    Result get_result = Result_err(Value_undefined);
    c_foreach_rev(i, vec_StackFrame, self->stack) {
        get_result = ObjectValue_get(i.ref->get, identifier);
        if (!get_result.is_err) {
            break;
        }
    }
    if (get_result.is_err) {
        Result_drop(&get_result);
        return Result_err(Value_string(cstr_from_fmt(
            "ReferenceError: %s is not defined",
            identifier.buf
        )));
    } else {
        return get_result;
    }
}

static Result MUST_USE Engine_eval_binary_op(Engine* self, BinaryOpExpr const* op_expr) {
    (void)self;
    (void)op_expr;
    PANIC("binary op isn't implemented");
}

static Result MUST_USE Engine_eval_unary_op(Engine* self, UnaryOpExpr const* op_expr) {
    (void)self;
    (void)op_expr;
    PANIC("unary op isn't implemented");
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
            ret.is_err = false; // undefined is fine if key is not found
            break;

        case VALUE_ARRAY:
            if (strcmp(cstr_str(&prop_expr->prop), "length") == 0) {
                ret.value = Value_number(vec_Value_size(&value.data.array.get->vec));
            } else {
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
            cstr callable_str = Value_to_cstr(&callable, 0);
            ret = Result_err(Value_string(cstr_from_fmt(
                "%s is not a function",
                cstr_str(&callable_str)
            )));
            cstr_drop(&callable_str);
        }
    }

    vec_Value_drop(&given_args);
    Value_drop(&callable);
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

static Result MUST_USE Engine_run_statement(Engine* self, Statement const* statement) {
    switch (statement->tag) {
        case STATEMENT_INTERFACE: break;

        case STATEMENT_SCOPE: {
            TRY_THROW_DROP(Engine_run_scope_in_stack_frame(self, &statement->data.scope));
        }   break;

        case STATEMENT_EXPR: {
            TRY_THROW_DROP(Engine_eval_expr(self, &statement->data.expr));
        }   break;

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

static Result console_log(Engine* engine, int argc, Value const argv[], void* data) {
    (void)engine; (void)data;
    for (int i = 0; i < argc; i++) {
        cstr item = Value_to_cstr(&argv[i], 0);
        printf("%s%s", cstr_str(&item), i == argc - 1 ? "\n" : " ");
        cstr_drop(&item);
    }
    return Result_ok_undefined;
}

void eval_program(Scope const* scope) {
    Engine engine = {
        .stack = c_init(vec_StackFrame, {
            Value_object_init({
                {"console", Value_object_init({
                    {"log", Value_native_func(console_log, nullptr, nullptr)}
                })},
            }).data.object,
        }),
    };
    Result res = Engine_run_scope(&engine, scope);
    if (res.is_err) {
        cstr err = Value_to_cstr(&res.value, 0);
        printf("Error thrown: %s\n", cstr_str(&err));
        cstr_drop(&err);
    }
    Result_drop(&res);
    Engine_drop(&engine);
}
