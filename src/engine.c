#include "engine.h"
#include "block.h"
#include "statement.h"
#include "expr.h"
#include "value.h"
#include "result.h"
#include "math.h"
#include "source_file.h"

typedef ArcObject StackFrame;
#define stack_frame_drop arc_js_object_drop
#define stack_frame_clone arc_js_object_clone

static inline StackFrame stack_frame_new() { return value_object_make_empty().object; }

#define g_class_key StackFrame, stack_frame
#include <cleat/generic/vec.h>

typedef struct Engine {
    VecStackFrame stack;
} Engine;

void engine_drop(Engine* me) {
    vec_stack_frame_drop(&my stack);
}

JsObject* engine_get_locals(Engine* me) {
    return vec_stack_frame_back(&my stack)->get;
}

static Result MUST_USE engine_eval_expr(Engine* me, Expr const* expr);
static Result MUST_USE engine_run_block(Engine* me, Block const* block);
static Result MUST_USE engine_run_block_in_stack_frame(Engine* me, Block const* block);
static Result MUST_USE engine_run_statement(Engine* me, Statement const* statement);

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

static Result MUST_USE engine_eval_identifier(Engine* me, csview identifier) {
    for_each_reverse (i, vec_stack_frame, &my stack) {
        Result get_result = js_object_get(i->get, identifier);
        if (get_result.tag == RESULT_OK) {
            return get_result;
        }
        // No need to drop the non-ok result since it's value is always undefined
    }
    return result_err(value_string(cstr_d("ReferenceError: ", identifier, " is not defined")));
}

static Result MUST_USE engine_assign_identifier(Engine* me, csview identifier, Value value) {
    for_each_reverse (i, vec_stack_frame, &my stack) {
        if (js_object_has(i->get, identifier)) {
            js_object_set(i->get, cstr_from_sv(identifier), value);
            return result_ok_undefined;
        }
    }
    // If we were in strict mode this would be an error
    js_object_set(vec_stack_frame_back(&my stack)->get, cstr_from_sv(identifier), value);
    return result_ok_undefined;
}

// Returns result_ok_undefined on success, not the value. Consumes value.
static Result MUST_USE engine_assign(Engine* me, Expr const* target, Value value) {
    switch (target->tag) {
        case EXPR_IDENTIFIER: return engine_assign_identifier(me, span_sv(target->inner_span), value);

        case EXPR_PROPERTY: {
            TRY_CATCH(target_value, engine_eval_expr(me, &target->property->expr), res_err, {
                value_drop(&value);
                return res_err;
            });
            switch (target_value.tag) {
                case VALUE_UNDEFINED:
                case VALUE_NULL:
                    value_drop(&value);
                    value_drop(&target_value);
                    return result_err(value_string(
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
                case VALUE_SYMBOL:
                case VALUE_BIGINT:
                    // Return the value, but do not assign it to anything (that's what browsers seem to do)
                    value_drop(&target_value);
                    return result_ok(value);

                case VALUE_OBJECT:
                    js_object_set(target_value.object.get, cstr_from_span(target->inner_span), value);
                    return result_ok_undefined;
            }
            impossible;
        }

        default:
            value_drop(&value);
            TRY_CATCH_DROP(engine_eval_expr(me, target), res_err, {
                return res_err;
            });
            return result_err(value_string(cstr_lit("SyntaxError: invalid assignment left-hand side")));
    }
    impossible;
}

static Result MUST_USE engine_eval_binary_op(Engine* me, BinaryOpExpr const* op_expr) {
    if (op_expr->op == BINARY_OP_EQUAL) {
        TRY_THROW(right, engine_eval_expr(me, &op_expr->right));
        TRY_THROW_DROP(engine_assign(me, &op_expr->left, right));
        return result_ok(value_clone(right));
    }
    TRY_THROW(left, engine_eval_expr(me, &op_expr->left));
    TRY_CATCH(right, engine_eval_expr(me, &op_expr->right), res_err, {
        value_drop(&left);
        return res_err;
    });
    Value ret = value_undefined;
    switch (op_expr->op) {
        case BINARY_OP_PLUS:
            if (left.tag == VALUE_NUMBER && right.tag == VALUE_NUMBER) {
                ret = value_number(left.number + right.number);
            } else {
                cstr str = cstr_null;
                cstr_append_value_js(&str, &left);
                cstr_append_value_js(&str, &right);
                ret = value_string(str);
            }
            break;
        case BINARY_OP_MINUS: ret = value_number(value_to_number(&left) - value_to_number(&right)); break;
        case BINARY_OP_STAR: ret = value_number(value_to_number(&left) * value_to_number(&right)); break;
        case BINARY_OP_SLASH: ret = value_number(value_to_number(&left) / value_to_number(&right)); break;
        case BINARY_OP_EQUAL: impossible; // handled above
        case BINARY_OP_DOUBLE_EQUAL: ret = value_boolean(value_loose_eq(&left, &right)); break;
        case BINARY_OP_TRIPLE_EQUAL: ret = value_boolean(value_strict_eq(&left, &right)); break;
        case BINARY_OP_LESS: ret = value_boolean(value_to_number(&left) < value_to_number(&right)); break;
        case BINARY_OP_GREATER: ret = value_boolean(value_to_number(&left) > value_to_number(&right)); break;
        case BINARY_OP_LESS_EQUAL: ret = value_boolean(value_to_number(&left) <= value_to_number(&right)); break;
        case BINARY_OP_GREATER_EQUAL: ret = value_boolean(value_to_number(&left) >= value_to_number(&right)); break;
        case BINARY_OP_NOT_EQUAL: ret = value_boolean(!value_loose_eq(&left, &right)); break;
        case BINARY_OP_TRIPLE_NOT_EQUAL: ret = value_boolean(!value_strict_eq(&left, &right)); break;
        case BINARY_OP_COMMA: ret = right; right = value_undefined; break;
        default:
            log_error("operator ", binary_op_get_zv(op_expr->op).str, " not implemented");
            ret = value_undefined;
    }
    value_drop(&left);
    value_drop(&right);
    return result_ok(ret);
}

static Result MUST_USE engine_eval_inc_dec(
    Engine* me,
    Expr const* target,
    Value const* previous,
    int before_ret,
    int after_ret
) {
    double num = value_to_number(previous) + before_ret;
    TRY_THROW_DROP(engine_assign(me, target, value_number(num + after_ret)));
    return result_ok(value_number(num));
}

static Result MUST_USE engine_eval_typeof(Engine* me, Value const* value) {
    ignore me; ignore value;
    log_error("typeof not yet implemented");
    return result_ok(value_string(cstr_lit("object")));
}

static Result MUST_USE engine_eval_unary_op(Engine* me, UnaryOpExpr const* op_expr) {
    TRY_THROW(value, engine_eval_expr(me, &op_expr->expr));
    Result res = result_ok_undefined;
    switch (op_expr->op) {
        case UNARY_OP_LEFT_BANG: res.value = value_boolean(!value_to_boolean(&value)); break;
        case UNARY_OP_TYPEOF: res = engine_eval_typeof(me, &value); break;
        case UNARY_OP_VOID: break;
        case UNARY_OP_DELETE: case UNARY_OP_AWAIT: case UNARY_OP_YIELD: case UNARY_OP_YIELD_STAR: break; // TODO
        case UNARY_OP_TILDE: res.value = value_number(~(long)value_to_number(&value)); break;
        case UNARY_OP_PLUS: res.value = value_number(value_to_number(&value)); break;
        case UNARY_OP_MINUS: res.value = value_number(-value_to_number(&value)); break;
        case UNARY_OP_LEFT_PLUS_PLUS: res = engine_eval_inc_dec(me, &op_expr->expr, &value, 1, 0); break;
        case UNARY_OP_LEFT_MINUS_MINUS: res = engine_eval_inc_dec(me, &op_expr->expr, &value, -1, 0); break;
        case UNARY_OP_RIGHT_PLUS_PLUS: res = engine_eval_inc_dec(me, &op_expr->expr, &value, 0, 1); break;
        case UNARY_OP_RIGHT_MINUS_MINUS: res = engine_eval_inc_dec(me, &op_expr->expr, &value, 0, -1); break;
        case UNARY_OP_RIGHT_BANG: res.value = value; value = value_undefined; break;
    }
    value_drop(&value);
    return res;
}

static Result MUST_USE engine_eval_property(Engine* me, PropertyExpr const* prop_expr) {
    TRY_THROW(value, engine_eval_expr(me, &prop_expr->expr));
    Value property;
    if (prop_expr->form == PROPERTY_EXPR_DOT) {
        property = value_string(cstr_from_sv(name_cooked(span_sv(property_name(prop_expr)))));
    } else {
        TRY_THROW(index, engine_eval_expr(me, &prop_expr->index));
        property = index;
    }
    Result ret = {0};
    switch (value.tag) {
        case VALUE_UNDEFINED: ret = result_err(value_string(cstr_lit("TypeError: undefined has no properties"))); break;
        case VALUE_NULL: ret = result_err(value_string(cstr_lit("TypeError: null has no properties"))); break;

        case VALUE_BOOLEAN:
        case VALUE_NUMBER:
        case VALUE_STRING:
        case VALUE_SYMBOL:
        case VALUE_BIGINT:
        case VALUE_JS_FUNC:
        case VALUE_NATIVE_FUNC: ret.value = value_undefined; break;

        case VALUE_OBJECT: {
            cstr prop_str = cstr_null;
            cstr_append_value_js(&prop_str, &property);
            ret = js_object_get(value.object.get, cstr_sv(&prop_str));
            ret.tag = RESULT_OK; // undefined is fine if key is not found
            cstr_drop(&prop_str);
        }   break;

        case VALUE_ARRAY: {
            if (property.tag == VALUE_NUMBER) {
                long index = property.number;
                if (index == property.number && index >= 0 && index < vec_value_size(&value.array.get->vec)) {
                    ret = result_ok(value_clone(*vec_value_at(&value.array.get->vec, index)));
                    break;
                }
            }
            cstr prop_str = cstr_null;
            cstr_append_value_js(&prop_str, &property);
            if (strcmp(cstr_str(&prop_str), "length") == 0) {
                ret.value = value_number(vec_value_size(&value.array.get->vec));
            } else {
                ret = js_object_get(&value.array.get->fallback, cstr_sv(&prop_str));
                ret.tag = RESULT_OK; // undefined is fine if key is not found
            }
            cstr_drop(&prop_str);
        }   break;
    }
    value_drop(&value);
    value_drop(&property);
    return ret;
}

static Result MUST_USE engine_eval_ternary(Engine* me, TernaryExpr* ternary) {
    TRY_THROW(condition_val, engine_eval_expr(me, &ternary->condition));
    bool condition = value_to_boolean(&condition_val);
    value_drop(&condition_val);
    return engine_eval_expr(me, condition ? &ternary->yes : &ternary->no);
}

static Result MUST_USE engine_eval_native_func(Engine* me, NativeFuncValue const* func, VecValue const* args) {
    return func->func(me, vec_value_size(args), args->data, func->data);
}

static Result MUST_USE engine_eval_js_func(Engine* me, JsFuncValue const* func, VecValue const* args) {
    vec_stack_frame_push(&my stack, stack_frame_new());
    int defined_arg_num = vec_function_arg_size(&func->impl->args);
    int given_arg_num = vec_value_size(args);
    JsObject* locals = engine_get_locals(me);
    for (int i = 0; i < defined_arg_num; i++) {
        FunctionArg const* arg = vec_function_arg_at(&func->impl->args, i);
        if (arg->rest) {
            log_error("Rest arguments not supported in engine");
        }
        if (arg->initializer.binding.tag == BINDING_IDENTIFIER) {
            Value value = value_undefined;
            if (i < given_arg_num) {
                // TODO: don't clone all args
                value = value_clone(*vec_value_at(args, i));
            }
            if (value.tag == VALUE_UNDEFINED) {
                TRY_CATCH(
                    default_value,
                    engine_eval_expr(me, initialized_binding_value(&arg->initializer)),
                    res_err,
                {
                    vec_stack_frame_pop(&my stack);
                    return res_err;
                });
                value = default_value;
            }
            js_object_set(locals, cstr_from_span(arg->initializer.binding.content_span), value);
        } else {
            log_error("Structured binding function arguments not yet supported in engine");
        }
    }
    Result res = engine_run_block(me, (Block*)function_impl_body(func->impl));
    vec_stack_frame_pop(&my stack);
    return res;
}

static Result MUST_USE engine_eval_call(Engine* me, CallExpr const* call_expr) {
    TRY_THROW(callable, engine_eval_expr(me, &call_expr->callable));

    int given_arg_num = vec_expr_size(&call_expr->args);
    VecValue given_args = vec_value_with_capacity(given_arg_num);
    for_each (i, vec_expr, &call_expr->args) {
        TRY_CATCH(value, engine_eval_expr(me, i), res_err, {
            vec_value_drop(&given_args);
            value_drop(&callable);
            return res_err;
        });
        vec_value_push(&given_args, value);
    }

    Result ret = result_ok_undefined;
    switch (callable.tag) {
        case VALUE_JS_FUNC:
            ret = engine_eval_js_func(me, callable.js_func.get, &given_args);
            break;

        case VALUE_NATIVE_FUNC:
            ret = engine_eval_native_func(me, callable.native_func.get, &given_args);
            break;

        default: {
            cstr message = cstr_null;
            cstr_append_value_js(&message, &callable);
            cstr_append_lit(&message, " is not a function");
            ret = result_err(value_string(message));
        }
    }

    vec_value_drop(&given_args);
    value_drop(&callable);
    if (ret.tag == RESULT_RETURN) {
        ret.tag = RESULT_OK;
    }
    return ret;
}

static Result MUST_USE engine_eval_object(Engine* me, ObjectExpr const* obj_expr) {
    Value ret = value_object_make_empty();
    for_each (i, vec_object_expr_item, obj_expr) {
        if (i->tag == OBJECT_ITEM_SPREAD || i->computed_key) {
            continue; // Not supported by the engine
        }
        TRY_CATCH(value, engine_eval_expr(me, &i->value), res_err, {
            value_drop(&ret);
            return res_err;
        });
        js_object_set(ret.object.get, cstr_clone(i->name_string), value);
    }
    return result_ok(ret);
}

static Result MUST_USE engine_eval_array(Engine* me, VecExpr const* array_expr) {
    Value ret = value_array_make_empty();
    ret.array.get->vec = vec_value_with_capacity(vec_expr_size(array_expr));
    for_each (i, vec_expr, array_expr) {
        TRY_CATCH(value, engine_eval_expr(me, i), res_err, {
            value_drop(&ret);
            return res_err;
        });
        vec_value_push(&ret.array.get->vec, value);
    }
    return result_ok(ret);
}

static Result MUST_USE engine_eval_expr(Engine* me, Expr const* expr) {
    switch (expr->tag) {
        case EXPR_IMPLICIT_ANY: return result_ok(value_undefined);
        case EXPR_LITERAL: return result_ok(value_clone(expr->literal));
        case EXPR_IDENTIFIER: return engine_eval_identifier(me, span_sv(expr->inner_span));
        case EXPR_BINARY_OP: return engine_eval_binary_op(me, expr->binary_op);
        case EXPR_UNARY_OP: return engine_eval_unary_op(me, expr->unary_op);
        case EXPR_PROPERTY: return engine_eval_property(me, expr->property);
        case EXPR_GENERIC_PARAMS: return engine_eval_expr(me, &expr->generic_params->expr);
        case EXPR_AS_TYPE: return engine_eval_expr(me, &expr->as_type->expr);
        case EXPR_TERNARY: return engine_eval_ternary(me, expr->ternary);
        case EXPR_CALL: return engine_eval_call(me, expr->call);
        case EXPR_FUNCTION: return result_ok(value_js_func(expr->function));
        case EXPR_OBJECT: return engine_eval_object(me, &expr->object);
        case EXPR_ARRAY: return engine_eval_array(me, &expr->array);
        case EXPR_CLASS_DECL: return result_ok_undefined; // TODO
        case EXPR_SPREAD: return result_ok_undefined; // TODO
        case EXPR_REGEX: return result_ok_undefined; // TODO
        case EXPR_PRIVATE_NAME: return result_ok_undefined;
        case EXPR_TEMPLATE: return result_ok_undefined; // TODO
        case EXPR_JSX_ELEMENT: case EXPR_JSX_FRAGMENT: case EXPR_JSX_ATTRIBUTES: return result_ok_undefined;
    }
    impossible;
}

static Result MUST_USE engine_run_if_else(Engine* me, IfStatement const* if_else) {
    int statement_size = vec_statement_size(&if_else->statements);
    int cond_size = vec_expr_size(&if_else->conditions);
    for (int i = 0; i < statement_size; i++) {
        bool cond_true = true;
        if (i < cond_size) {
            TRY_THROW(condition, engine_eval_expr(me, vec_expr_at(&if_else->conditions, i)));
            cond_true = value_to_boolean(&condition);
            value_drop(&condition);
        }
        if (cond_true) {
            TRY_THROW_DROP(engine_run_statement(
                me,
                vec_statement_at(&if_else->statements, i)
            ));
            break;
        }
    }
    return result_ok_undefined;
}

static Result MUST_USE engine_run_for_loop(Engine* me, ForLoopStatement const* loop) {
    TRY_THROW_DROP(engine_run_statement(me, &loop->initialization));
    while (true) {
        TRY_THROW(keep_going, engine_eval_expr(me, &loop->condition));
        if (!value_to_boolean(&keep_going)) {
            break;
        }
        TRY_CATCH_DROP(engine_run_statement(me, &loop->body), res_err, {
            if (res_err.tag == RESULT_BREAK) {
                return result_ok_undefined;
            } else {
                return res_err;
            }
        });
        TRY_THROW_DROP(engine_eval_expr(me, &loop->afterthought));
    }
    return result_ok_undefined;
}

static Result MUST_USE engine_run_binding(Engine* me, Binding const* binding, Value value) {
    switch (binding->tag) {
        case BINDING_NOOP:
            value_drop(&value);
            break;

        case BINDING_IDENTIFIER: {
            cstr name = cstr_from_span(binding->content_span);
            JsObject* locals = engine_get_locals(me);
            js_object_set(locals, name, value);
        }   break;

        case BINDING_ARRAY:
            log_error("Array structured bindings not supported");
            value_drop(&value);
            break;

        case BINDING_OBJECT:
            log_error("Object structured bindings not supported");
            value_drop(&value);
            break;
    }
    return result_ok_undefined;
}

// Only return statements return ok with a value, caller has to check statement tag to notice undefined returns
static Result MUST_USE engine_run_statement(Engine* me, Statement const* statement) {
    switch (statement->tag) {
        case STATEMENT_NOOP:
        case STATEMENT_SOURCE_DIRECTIVE:
            return result_ok_undefined;

        case STATEMENT_BLOCK:
            TRY_THROW_DROP(engine_run_block_in_stack_frame(me, &statement->block));
            return result_ok_undefined;

        case STATEMENT_IF_ELSE: return engine_run_if_else(me, &statement->if_else);

        case STATEMENT_FOR_LOOP: {
            vec_stack_frame_push(&my stack, stack_frame_new());
            Result res = engine_run_for_loop(me, statement->for_loop);
            vec_stack_frame_pop(&my stack);
            return res;
        }

        case STATEMENT_FOR_EACH_LOOP:
            log_error("Running for-each loops not yet implemented");
            return result_ok_undefined;

        case STATEMENT_SWITCH:
            log_error("Running switch statements not yet implemented");
            return result_ok_undefined;

        case STATEMENT_TRY_CATCH:
            log_error("Running try-catch blocks not yet implemented");
            return result_ok_undefined;

        case STATEMENT_LABELED: return engine_run_statement(me, statement->labeled.body);

        case STATEMENT_WITH:
            log_error("Running with statements not yet implemented");
            return result_ok_undefined;

        case STATEMENT_BREAK: return result_break();
        case STATEMENT_CONTINUE: return result_err(value_string(cstr_lit("Continue statements not yet implemented")));

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
            TRY_THROW_DROP(engine_eval_expr(me, &statement->expr));
            return result_ok_undefined;

        case STATEMENT_RETURN_EXPR: {
            TRY_THROW(value, engine_eval_expr(me, &statement->expr));
            return result_return(value);
        }

        case STATEMENT_THROW_EXPR: {
            TRY_THROW(value, engine_eval_expr(me, &statement->expr));
            return result_err(value);
        }

        case STATEMENT_IMPORT_EXPORT:
            log_error("Importing not yet implemented");
            return result_ok_undefined;

        case STATEMENT_DECL_VARIABLE: {
            // TODO: respect keyword (let, const, var)
            for_each (i, vec_initialized_binding, &statement->decl.variable.initializers) {
                TRY_THROW(value, engine_eval_expr(me, initialized_binding_value(i)));
                TRY_THROW_DROP(engine_run_binding(me, &i->binding, value));
            }
            return result_ok_undefined;
        }

        case STATEMENT_DECL_FUNCTION: {
            if (!span_valid(statement->decl.function.impl->name)) {
                return result_ok_undefined;
            }
            cstr name = cstr_from_span(statement->decl.function.impl->name);
            Value value = value_js_func(statement->decl.function.impl);
            JsObject* locals = engine_get_locals(me);
            js_object_set(locals, name, value);
            return result_ok_undefined;
        }

        case STATEMENT_DECL_CLASS:
            log_error("Creating classes not yet implemented");
            return result_ok_undefined;

        case STATEMENT_DECL_ENUM: return result_ok_undefined;
        case STATEMENT_DECL_TYPE: return result_ok_undefined;
        case STATEMENT_DECL_INTERFACE: return result_ok_undefined;

        case STATEMENT_DECL_NAMESPACE:
            log_error("Creating namespaces not yet implementated");
            return result_ok_undefined;
    }
    impossible;
}

static Result MUST_USE engine_run_block(Engine* me, Block const* block) {
    for_each (i, vec_statement, &block->statements) {
        TRY_THROW_DROP(engine_run_statement(me, i));
    }
    return result_ok_undefined;
}

static Result MUST_USE engine_run_block_in_stack_frame(Engine* me, Block const* block) {
    vec_stack_frame_push(&my stack, stack_frame_new());
    TRY_CATCH_DROP(engine_run_block(me, block), res_err, {
        vec_stack_frame_pop(&my stack);
        return res_err;
    });
    vec_stack_frame_pop(&my stack);
    return result_ok_undefined;
}

static Result console_log_impl(Engine* engine, int argc, Value const argv[], void* data) {
    ignore engine; ignore data;
    cstr str = cstr_null;
    for (int i = 0; i < argc; i++) {
        cstr_append_value_js(&str, &argv[i]);
        if (i < argc - 1) {
            cstr_append_lit(&str, " ");
        }
    }
    println(consume_d(str));
    return result_ok_undefined;
}

static Result math_sqrt_impl(Engine* engine, int argc, Value const argv[], void* data) {
    ignore engine; ignore data;
    if (argc > 0) {
        return result_ok(value_number(sqrt(value_to_number(&argv[0]))));
    } else {
        return result_ok(value_number(0));
    }
}

static Result string_impl(Engine* engine, int argc, Value const argv[], void* data) {
    ignore engine; ignore data;
    if (argc > 0) {
        cstr ret = cstr_null;
        cstr_append_value_js(&ret, argv);
        return result_ok(value_string(ret));
    } else {
        return result_ok(value_string(cstr_null));
    }
}

static Result number_impl(Engine* engine, int argc, Value const argv[], void* data) {
    ignore engine; ignore data;
    return result_ok(value_number(argc > 0 ? value_to_number(argv) : 0));
}

static Result boolean_impl(Engine* engine, int argc, Value const argv[], void* data) {
    ignore engine; ignore data;
    return result_ok(value_boolean(argc > 0 ? value_to_boolean(argv) : false));
}

void run_in_engine(SourceFile const* source, ArcObject root) {
    if (!root.get) {
        root = arc_js_object_make((JsObject){0});
    }
    js_object_set(root.get, cstr_lit("console"), value_object_init({
        {"log", value_native_func(console_log_impl, nullptr, nullptr)}
    }));
    js_object_set(root.get, cstr_lit("Math"), value_object_init({
        {"sqrt", value_native_func(math_sqrt_impl, nullptr, nullptr)}
    }));
    js_object_set(root.get, cstr_lit("String"), value_native_func(string_impl, nullptr, nullptr));
    js_object_set(root.get, cstr_lit("Number"), value_native_func(number_impl, nullptr, nullptr));
    js_object_set(root.get, cstr_lit("Boolean"), value_native_func(boolean_impl, nullptr, nullptr));
    js_object_set(root.get, cstr_lit("NaN"), value_number(NAN));
    Engine engine = {
        .stack = c_init(vec_stack_frame, {root}),
    };
    Result res = engine_run_block(&engine, &source->ast);
    if (res.tag == RESULT_ERR) {
        cstr err = cstr_null;
        cstr_append_value_js(&err, &res.value);
        println("Error thrown: ", consume_d(err));
    }
    result_drop(&res);
    if (vec_stack_frame_size(&engine.stack) != 1) {
        panic(
            "Stack mismanaged. Ended up with ", vec_stack_frame_size(&engine.stack),
            " stack frames, should have been 1"
        );
    }
    engine_drop(&engine);
}
