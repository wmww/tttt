#include "baseline_test_traversal.h"
#include "baseline_test_case.h"
#include "type.h"
#include "interface_member.h"
#include "generic.h"
#include "expr.h"
#include "several_types.h"
#include "function_impl.h"
#include "import_statement.h"
#include "statement.h"
#include "block.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "function_impl.h"
#include "callable_type.h"

static inline VecSpan declaration_get_spans(Declaration const* me) {
    return me ? vec_span_clone(my declarations) : vec_span_init();
}

void generic_compile_test_info(Generic const* me, BaselineTestCase* ctx) {
    c_foreach(i, vec_generic_item, my items) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = i.ref->name });
        type_compile_test_info(&i.ref->extends, ctx);
        if (i.ref->fallback_explicit) {
            type_compile_test_info(&i.ref->fallback, ctx);
        }
    }
}

void binding_content_compile_test_info(Binding const* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case BINDING_NOOP:
            baseline_test_case_add_type(ctx, my content_span, &type_undefined);
            break;

        case BINDING_IDENTIFIER:
            baseline_test_case_add_declaration(ctx, my content_span, my declaration.get);
            break;

        case BINDING_ARRAY:
            c_foreach(i, vec_binding, my array) {
                binding_content_compile_test_info(i.ref, ctx);
            }
            break;

        case BINDING_OBJECT:
            c_foreach(i, vec_object_binding_item, my object) {
                binding_content_compile_test_info(&i.ref->target, ctx);
            }
            break;
    }
}

void binding_compile_test_info(Binding const* me, BaselineTestCase* ctx) {
    binding_content_compile_test_info(me, ctx);
    if (my has_explicit_type) {
        type_compile_test_info(&my explicit_type, ctx);
    }
}

void initialized_binding_compile_test_info(
    InitializedBinding* me,
    BaselineTestCase* ctx
) {
    binding_compile_test_info(&my binding, ctx);
    if (my value.tag != EXPR_IMPLICIT_ANY) {
        expr_compile_test_info(&my value, ctx);
    }
}

void interface_member_compile_test_info(InterfaceMember const* me, Span scope, BaselineTestCase* ctx) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME:
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_clone(my declarations),
                .namespaces = vec_span_single(scope),
                .span = my key.span,
                .name_is_value = my key.name_is_value,
            });
            baseline_test_case_add_type(ctx, my key.span, &my type);
            break;

        case INTERFACE_KEY_INDEX:
            baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = my key.index.name });
            baseline_test_case_add_type(ctx, my key.index.name, &my key.index.type);
            type_compile_test_info(&my key.index.type, ctx);
            break;

        case INTERFACE_KEY_EXPR:
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_single(my key.span),
                .namespaces = vec_span_single(scope),
                .span = my key.span,
                .name = my key.expr.ref->outer_span,
                .name_is_value = true,
            });
            baseline_test_case_add_type(ctx, my key.span, &my type);
            expr_compile_test_info(my key.expr.ref, ctx);
            break;

        case INTERFACE_KEY_MAPPED:
            baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = my key.mapped.type_name });
            type_compile_test_info(&my key.mapped.in_type, ctx);
            type_compile_test_info(&my key.mapped.as_type, ctx);
            break;

        case INTERFACE_KEY_CALLABLE: break;
    }
    type_compile_test_info(&my type, ctx);
}

void interface_compile_test_info(Interface const* me, Span scope, BaselineTestCase* ctx) {
    c_foreach(i, vec_interface_member, my members) {
        interface_member_compile_test_info(i.ref, scope, ctx);
    }
}

void type_compile_test_info(Type const* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
            break;

        case TYPE_IDENTIFIER:
            if (span_equals(my identifier->name, "object")) {
                break;
            }
            if (my identifier->decl_ref && !span_valid(my identifier->decl_ref->name)) {
                break;
            }
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = declaration_get_spans(my identifier->decl_ref),
                .allow_declaration_empty = true,
                .span = my identifier->name,
            });
            c_foreach(i, vec_spanned_type, my identifier->generic_params) {
                type_compile_test_info(&i.ref->type, ctx);
            }
            break;

        case TYPE_INFERRED:
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = declaration_get_spans(my inferred->declaration_ref),
                .allow_declaration_empty = true,
                .span = my inferred->name,
            });
            break;

        case TYPE_CALLABLE:
            generic_compile_test_info(&my callable->generic, ctx);
            c_foreach(i, vec_callable_arg, my callable->args) {
                binding_content_compile_test_info(&i.ref->source->initializer.binding, ctx);
                if (i.ref->source->initializer.binding.has_explicit_type) {
                    type_compile_test_info(&i.ref->type, ctx);
                }
            }
            type_compile_test_info(&my callable->ret, ctx);
            break;

        case TYPE_INTERFACE: interface_compile_test_info(&my interface, span_null, ctx); break;

        case TYPE_ARRAY:
            type_compile_test_info(my array.element, ctx);
            break;

        case TYPE_TUPLE:
            c_foreach(i, vec_tuple_element, my tuple.elements) {
                type_compile_test_info(&i.ref->type, ctx);
            }
            break;

        case TYPE_INDEXED:
            type_compile_test_info(&my indexed->interface, ctx);
            type_compile_test_info(&my indexed->index, ctx);
            break;

        case TYPE_KEYOF:
            type_compile_test_info(&my keyof->interface, ctx);
            break;

        case TYPE_TYPEOF:
            expr_compile_test_info(my typeof_->expr, ctx);
            break;

        case TYPE_TERNARY:
            type_compile_test_info(&my ternary->maybe_sub, ctx);
            type_compile_test_info(&my ternary->maybe_super, ctx);
            type_compile_test_info(&my ternary->if_true, ctx);
            type_compile_test_info(&my ternary->if_false, ctx);
            break;

        case TYPE_ABSTRACTED:
            type_compile_test_info(&my abstracted->inner, ctx);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                type_compile_test_info(i.ref, ctx);
            }
            break;

        case TYPE_LAZY:
            type_compile_test_info(lazy_type_get(my lazy), ctx);
            break;

        case TYPE_REF:
            type_compile_test_info(my ref, ctx);
            break;
    }
}

void function_impl_compile_test_info(FunctionImpl* me, Span scope, BaselineTestCase* ctx) {
    if (span_valid(my name)) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = vec_span_single(my full_span),
            .namespaces = vec_span_single(scope),
            .span = my name,
        });
        baseline_test_case_add_type(ctx, my name, lazy_type_get(&my lazy_type));
    }
    generic_compile_test_info(&my generic, ctx);
    c_foreach(i, vec_function_arg, my args) {
        initialized_binding_compile_test_info(&i.ref->initializer, ctx);
    }
    if (my ret_explicit && !my is_constructor) {
        type_compile_test_info(&my ret, ctx);
    }
    block_compile_test_info(&my body, ctx);
}

void import_statement_compile_test_info(ImportStatement const* me, BaselineTestCase* ctx) {
    if (my main.source_tag != IMPORT_SOURCE_NONE && !my main.is_export_wildcard) {
        baseline_test_case_add_declaration(ctx, my main.decl_name, my main.declaration.get);
    }
    c_foreach(i, vec_import_item, my items) {
        if (i.ref->declaration.get) {
            if (i.ref->decl_name.sv.buf != i.ref->source_name.sv.buf) {
                baseline_test_case_add_type(ctx, i.ref->source_name, &i.ref->declaration.get->value_type);
            }
            baseline_test_case_add_declaration(ctx, i.ref->decl_name, i.ref->declaration.get);
        }
    }
}

void variable_statement_compile_test_info(VariableStatement const* me, BaselineTestCase* ctx) {
    c_foreach(i, vec_initialized_binding, my initializers) {
        initialized_binding_compile_test_info(i.ref, ctx);
    }
}

void enum_decl_compile_test_info(EnumDecl const* me, BaselineTestCase* ctx) {
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        .declarations = vec_span_single(my full_span),
        .span = my name,
    });
    Type t = type_identifier(c_new(IdentifierType, {
        .name = my name,
    }));
    baseline_test_case_add_type(ctx, my name, &t);
    type_drop(&t);
    c_foreach(i, vec_enum_case, my cases) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = vec_span_single(i.ref->name),
            .namespaces = vec_span_single(my name),
            .span = i.ref->name,
        });
        baseline_test_case_add_type(ctx, i.ref->name, &i.ref->identifier_type);
        if (i.ref->value.tag != EXPR_IMPLICIT_ANY) {
            expr_compile_test_info(&i.ref->value, ctx);
        }
    }
}

void class_item_compile_test_info(ClassItem* me, Span class_name, BaselineTestCase* ctx) {
    switch(my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_compile_test_info(&my constructor.func, class_name, ctx);
            break;

        case CLASS_ITEM_METHOD:
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_single(my span),
                .namespaces = vec_span_single(class_name),
                .span = my method.name_span,
                .name_is_value = my method.name_is_value,
            });
            Type t = class_item_get_type(me);
            baseline_test_case_add_type(ctx, my method.name_span, &t);
            type_drop(&t);
            function_impl_compile_test_info(&my method.func, class_name, ctx);
            break;

        case CLASS_ITEM_FIELD: {
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_single(my span),
                .namespaces = vec_span_single(class_name),
                .span = my field.name_span,
                .name_is_value = my field.name_is_value,
            });
            Type t = class_item_get_type(me);
            baseline_test_case_add_type(ctx, my field.name_span, &t);
            type_drop(&t);

            if (my field.has_explicit_type) {
                type_compile_test_info(&my field.explicit_type, ctx);
            }
            if (my field.value.tag != EXPR_IMPLICIT_ANY) {
                expr_compile_test_info(&my field.value, ctx);
            }
        }   break;

        case CLASS_ITEM_INTERFACE_MEMBER:
            interface_member_compile_test_info(&my interface_memeber, class_name, ctx);
            break;

        case CLASS_ITEM_STATIC_BLOCK:
            block_compile_test_info(&my static_block, ctx);
            break;
    }
}

void class_decl_compile_test_info(ClassDecl* me, bool is_expr, BaselineTestCase* ctx) {
    if (span_valid(my name)) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(my declaration.get),
            .span = my name,
        });
        if (is_expr) {
            baseline_test_case_add_type(ctx, my name, &my declaration.get->value_type);
        } else {
            baseline_test_case_add_type(ctx, my name, &my instance_identifier_type);
        }
    }
    generic_compile_test_info(&my generic, ctx);
    if (my extends.tag != EXPR_IMPLICIT_ANY) {
        Type const* unresolved = expr_get_type(&my extends);
        Type const* resolved = type_resolve_until_identifier(unresolved);
        if (resolved->tag == TYPE_IDENTIFIER &&
            resolved->identifier->decl_ref &&
            resolved->identifier->decl_ref->is_typeof
        ) {
            if (my extends.tag != EXPR_GENERIC_PARAMS) {
                // Extremely hacky but whatever. No idea why tsc does this.
                *(bool*)(&resolved->identifier->decl_ref->is_typeof) = false;
                baseline_test_case_add_type_recursive(ctx, my extends.outer_span, my extends.inner_span, resolved);
                *(bool*)(&resolved->identifier->decl_ref->is_typeof) = true;
            }
            expr_compile_test_info_inner(&my extends, ctx);
        } else {
            expr_compile_test_info(&my extends, ctx);
        }
    }
    c_foreach(i, vec_type, my implements) {
        type_compile_test_info(i.ref, ctx);
    }
    c_foreach(i, vec_class_item, my items) {
        class_item_compile_test_info(i.ref, my name, ctx);
    }
}

void type_decl_compile_test_info(TypeDecl const* me, BaselineTestCase* ctx) {
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        .declarations = declaration_get_spans(my declaration.get),
        .span = my name,
    });
    Type to_drop = {0};
    Type const* my_type = &my type;
    Type const* resolved = my_type;
    if (generic_empty(&my generic)) { // If the type is not generic
        if (my_type->tag == TYPE_IDENTIFIER &&
            my_type->identifier->decl_ref &&
            my_type->identifier->decl_ref->type_tag == DECLARATION_CLASS
        ) {
        } else {
            resolved = type_resolve(my_type);
            switch (resolved->tag) {
                case TYPE_ANY:
                case TYPE_UNKNOWN:
                case TYPE_NEVER:
                case TYPE_VOID:
                case TYPE_BOOLEAN:
                case TYPE_NUMBER:
                case TYPE_STRING:
                case TYPE_VALUE:
                case TYPE_IDENTIFIER: break;
                default: resolved = my_type;
            }
        }
    } else { // If the type is generic

        /* This logic is still fucky, use this to test:
type Foo<T> = T | { x: Foo<T>}
type Bar<T> = 1 extends T ? [T] : T
type Baz<T> = 1 extends number ? [T] : T
type Biz<T> = {notbiz: T} extends {biz: T} ? [T] : T
type Buz<T> = {buz: 7} extends {buz: T} ? [T] : T
type Buzz<T extends number> = {buzz: T} extends {buzz: number} ? [T] : T
type Boop<T> = 7
type Beep<T> = {beep: 7}
        */

        to_drop = type_identifier(c_new(IdentifierType, {
            .name = my name,
        }));
        c_foreach(i, vec_generic_item, my generic.items) {
            vec_spanned_type_push(&to_drop.identifier->generic_params, spanned_type(
                i.ref->name,
                type_identifier(c_new(IdentifierType, { .name = i.ref->name }))
            ));
        }
        resolved = &to_drop;
    }
    baseline_test_case_add_type(ctx, my name, resolved);
    type_drop(&to_drop);
    generic_compile_test_info(&my generic, ctx);
    type_compile_test_info(my_type, ctx);
}

void interface_decl_compile_test_info(InterfaceDecl const* me, BaselineTestCase* ctx) {
    if (span_valid(my name)) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(my declaration.get),
            .span = my name,
        });
    }
    generic_compile_test_info(&my generic, ctx);
    c_foreach(i, vec_spanned_type, my extends) {
        type_compile_test_info(&i.ref->type, ctx);
    }
    interface_compile_test_info(&my type.interface, my name, ctx);
}

void namespace_decl_compile_test_info(NamespaceDecl const* me, BaselineTestCase* ctx) {
    if (span_valid(my name)) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(my declaration.get),
            .span = my name,
        });
        if (my declaration.get->value_tag) {
            Declaration d = {0};
            declaration_init(&d, (DeclProps){
                .tag = DECLARATION_TYPE,
                .is_typeof = true,
            });
            Type t = type_identifier(c_new(IdentifierType, {
                .name = my name,
                .decl_ref = &d,
            }));
            baseline_test_case_add_type(ctx, my name, &t);
            type_drop(&t);
            declaration_drop(&d);
        }
    }
    block_compile_test_info(&my contents, ctx);
}

void identifier_expr_compile_test_info(IdentifierExpr* me, BaselineTestCase* ctx) {
    if (my decl_ref && span_valid(my decl_ref->name)) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(my decl_ref),
            .span = my name,
            .name = my decl_ref->name,
        });
    }
    c_foreach(i, vec_spanned_type, my generic_params) {
        type_compile_test_info(&i.ref->type, ctx);
    }
}

void expr_compile_test_info_inner(Expr* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
        case EXPR_LITERAL:
            if (my literal.tag == VALUE_UNDEFINED && csview_equals(my inner_span.sv, "undefined")) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .allow_declaration_empty = true,
                    .span = my inner_span,
                });
            }
            break;

        case EXPR_IDENTIFIER:
            identifier_expr_compile_test_info(&my identifier, ctx);
            break;

        case EXPR_BINARY_OP:
            expr_compile_test_info(&my binary_op->left, ctx);
            expr_compile_test_info(&my binary_op->right, ctx);
            break;

        case EXPR_UNARY_OP:
            expr_compile_test_info(&my unary_op->expr, ctx);
            break;

        case EXPR_PROPERTY: {
            bool has_compiled_expr_info = false;
            Type const* expr_t_resolved = type_resolve(expr_get_type(&my property->expr));
            Type const* prop_t = expr_get_type(&my property->prop);

            if (expr_t_resolved->tag == TYPE_INTERFACE &&
                prop_t->tag == TYPE_VALUE &&
                prop_t->value.tag == VALUE_STRING
            ) {
                c_foreach(i, vec_interface_member, expr_t_resolved->interface.members) {
                    if (i.ref->key.tag == INTERFACE_KEY_NAME &&
                        cstr_eq(&i.ref->key.name_string, &prop_t->value.string)
                    ) {
                        if (my property->form == PROPERTY_EXPR_DOT) {
                            baseline_test_case_add_symbol(ctx, (BaselineSym){
                                .declarations = vec_span_clone(i.ref->declarations),
                                .allow_declaration_empty = true,
                                .namespaces = vec_span_single(i.ref->origin_interface_name),
                                .span = my outer_span,
                                .name = span_valid(i.ref->key.span) ? i.ref->key.span : my property->prop.outer_span,
                                .name_is_value = i.ref->key.name_is_value,
                            });
                        }
                        expr_compile_test_info(&my property->expr, ctx);
                        has_compiled_expr_info = true;
                        if (my property->prop.tag == EXPR_LITERAL) {
                            baseline_test_case_add_symbol(ctx, (BaselineSym){
                                .declarations = vec_span_clone(i.ref->declarations),
                                .allow_declaration_empty = true,
                                .namespaces = vec_span_single(i.ref->origin_interface_name),
                                .span = my property->prop.outer_span,
                                .name = i.ref->key.span,
                                .name_is_value = i.ref->key.name_is_value,
                            });
                        }
                        break;
                    }
                }
            }

            if (!has_compiled_expr_info) {
                expr_compile_test_info(&my property->expr, ctx);
            }

            if (my property->form == PROPERTY_EXPR_BRACKET) {
                expr_compile_test_info(&my property->prop, ctx);
            } else {
                baseline_test_case_add_type(ctx, my property->prop.outer_span, expr_get_type(me));
            }
        }   break;

        case EXPR_GENERIC_PARAMS:
            expr_compile_test_info(&my generic_params->expr, ctx);
            c_foreach(i, vec_spanned_type, my generic_params->params) {
                type_compile_test_info(&i.ref->type, ctx);
            }
            break;

        case EXPR_AS_TYPE:
            if (my as_type->angle_bracket_syntax) {
                type_compile_test_info(&my as_type->type, ctx);
            }
            expr_compile_test_info(&my as_type->expr, ctx);
            if (!my as_type->angle_bracket_syntax) {
                type_compile_test_info(&my as_type->type, ctx);
            }
            break;

        case EXPR_TERNARY:
            expr_compile_test_info(&my ternary->condition, ctx);
            expr_compile_test_info(&my ternary->yes, ctx);
            expr_compile_test_info(&my ternary->no, ctx);
            break;

        case EXPR_CALL:
            expr_compile_test_info(&my call->callable, ctx);
            c_foreach(i, vec_expr, my call->args) {
                expr_compile_test_info(i.ref, ctx);
            }
            break;

        case EXPR_FUNCTION:
            function_impl_compile_test_info(my function, span_null, ctx);
            break;

        case EXPR_OBJECT:
            c_foreach(i, vec_object_expr_item, my object) {
                if (!i.ref->is_spread) {
                    baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = i.ref->name_span });
                    Type t = type_clone(*expr_get_type(&i.ref->value));
                    type_abstract_literals(&t);
                    baseline_test_case_add_type(ctx, i.ref->name_span, &t);
                    type_drop(&t);
                }

                expr_compile_test_info(&i.ref->value, ctx);
            }
            break;

        case EXPR_ARRAY:
            c_foreach(i, vec_expr, my array) {
                expr_compile_test_info(i.ref, ctx);
            }
            break;

        case EXPR_CLASS_DECL:
            class_decl_compile_test_info(my class_decl, true, ctx);
            break;
    }
}

static bool vec_expr_contains_elements_other_than_undefined(VecExpr const* me) {
    c_foreach(i, vec_expr, *me) {
        if (i.ref->tag != EXPR_LITERAL || i.ref->literal.tag != VALUE_UNDEFINED) {
            return true;
        }
    }
    return false;
}

void expr_compile_test_info(Expr* me, BaselineTestCase* ctx) {
    if (my tag == EXPR_IDENTIFIER && my identifier.decl_ref && !my identifier.keyword) {
        // Do NOT degenericize
        baseline_test_case_add_type_recursive(
            ctx,
            my outer_span,
            my inner_span,
            &my identifier.decl_ref->value_type
        );
    } else if (my tag == EXPR_ARRAY && !vec_expr_contains_elements_other_than_undefined(&my array)) {
        Type t = type_array((Type*)&type_undefined, false);
        baseline_test_case_add_type_recursive(ctx, my outer_span, my inner_span, &t);
        // Don't drop, since we didn't allocate
    } else if (my tag == EXPR_LITERAL && my literal.tag == VALUE_NULL) {
    } else if (my tag == EXPR_GENERIC_PARAMS) {
    } else {
        baseline_test_case_add_type_recursive(ctx, my outer_span, my inner_span, expr_get_type(me));
    }

    expr_compile_test_info_inner(me, ctx);
}

void statement_compile_test_info(Statement* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_BLOCK:
            block_compile_test_info(&my block, ctx);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            if (my expr.tag != EXPR_LITERAL || my expr.literal.tag != VALUE_UNDEFINED) {
                expr_compile_test_info(&my expr, ctx);
            }
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&my if_else.statements);
            int cond_size = vec_expr_size(&my if_else.conditions);
            for (int i = 0; i < statement_size; i++) {
                if (i < cond_size) {
                    expr_compile_test_info(vec_expr_at_mut(&my if_else.conditions, i), ctx);
                }
                statement_compile_test_info(vec_statement_at_mut(&my if_else.statements, i), ctx);
            }
        }   break;

        case STATEMENT_FOR_LOOP:
            statement_compile_test_info(&my for_loop->initialization, ctx);
            expr_compile_test_info(&my for_loop->condition, ctx);
            if (my for_loop->afterthought.tag != EXPR_LITERAL ||
                my for_loop->afterthought.literal.tag != VALUE_UNDEFINED
            ) {
                expr_compile_test_info(&my for_loop->afterthought, ctx);
            }
            statement_compile_test_info(&my for_loop->body, ctx);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    variable_statement_compile_test_info(&my for_each_loop->variable.statement, ctx);
                    break;

                case FOR_EACH_LOOP_EXPR:
                    expr_compile_test_info(&my for_each_loop->variable.expr, ctx);
                    break;
            }
            expr_compile_test_info(&my for_each_loop->iterable, ctx);
            statement_compile_test_info(&my for_each_loop->body, ctx);
            break;

        case STATEMENT_SWITCH:
            expr_compile_test_info(&my switch_->needle, ctx);
            c_foreach(i, vec_switch_case, my switch_->cases) {
                if (i.ref->is_pattern) {
                    expr_compile_test_info(&i.ref->pattern, ctx);
                }
                c_foreach(j, vec_statement, i.ref->statements) {
                    statement_compile_test_info(j.ref, ctx);
                }
            }
            break;

        case STATEMENT_TRY_CATCH:
            block_compile_test_info(&my try_catch->try_block, ctx);
            if (my try_catch->catch_var.tag != BINDING_NOOP) {
                binding_compile_test_info(&my try_catch->catch_var, ctx);
                block_compile_test_info(&my try_catch->catch_block, ctx);
            }
            block_compile_test_info(&my try_catch->finally_block, ctx);
            break;

        case STATEMENT_BREAK: break;
        case STATEMENT_CONTINUE: break;

        case STATEMENT_IMPORT:
            import_statement_compile_test_info(my import, ctx);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_compile_test_info(&my decl.variable, ctx);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_compile_test_info(my decl.function.impl, span_null, ctx);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_compile_test_info(my decl.enum_, ctx);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_compile_test_info(my decl.class, false, ctx);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_compile_test_info(&my decl.type, ctx);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_compile_test_info(my decl.interface, ctx);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_compile_test_info(&my decl.namespace, ctx);
            break;
    }
}

void block_compile_test_info(Block const* me, BaselineTestCase* ctx) {
    c_foreach(i, vec_statement, my statements) {
        statement_compile_test_info(i.ref, ctx);
    }
}
