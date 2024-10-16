#include "statement.h"
#include "import_statement.h"
#include "error.h"
#include "name_stack.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "function_impl.h"

// Variable initialization

void variable_statement_drop(VariableStatement* me) {
    vec_initialized_binding_drop(&my initializers);
}

void variable_statement_add_names(VariableStatement* me, bool declared, Scope* scope) {
    c_foreach(i, vec_initialized_binding, my initializers) {
        if (my keyword == VAR_INIT_CONST &&
            i.ref->value.tag == EXPR_IMPLICIT_ANY &&
            !declared
        ) {
            error_ctx_error(
                scope->errs,
                i.ref->binding.content_span,
                1155,
                "'const' declarations must be initialized."
            );
        }
        initialized_binding_add_names(i.ref, my keyword != VAR_INIT_CONST, scope);
    }
}

void variable_statement_resolve_names(VariableStatement* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_initialized_binding, my initializers) {
        initialized_binding_lookup_names(i.ref, names, errs);
    }
}

void variable_statement_typecheck(VariableStatement* me, ErrorCtx* errs) {
    c_foreach(i, vec_initialized_binding, my initializers) {
        initialized_binding_typecheck(i.ref, errs);
    }
}

// Type declaration

void type_decl_drop(TypeDecl* me) {
    generic_drop(&my generic);
    type_drop(&my type);
    arc_declaration_drop(&my declaration);
}

void type_decl_add_names(TypeDecl* me, Scope* scope) {
    scope_init_declaration(scope, &my declaration, (DeclProps){
        .name = my name,
        .full = my full_span,
        .tag = DECLARATION_TYPE,
        .generic = &my generic,
        .declared_type = type_refcopy(&my type),
    });
}

void type_decl_typecheck(TypeDecl* me, ErrorCtx* errs) {
    generic_typecheck(&my generic, errs);
    type_typecheck(&my type, errs);
}

// Namespace declaration

void namespace_decl_drop(NamespaceDecl* me) {
    block_drop(&my contents);
    scope_drop(&my inner_scope);
    type_drop(&my value_type);
    arc_declaration_drop(&my declaration);
}

void namespace_decl_add_names(NamespaceDecl* me, Scope* scope) {
    my inner_scope = scope_init(scope->errs);
    c_foreach(i, vec_statement, my contents.statements) {
        statement_add_names(i.ref, &my inner_scope);
    }

    scope_add_namespace(scope, my name, &my inner_scope);

    ASSERT(my value_type.tag == 0);
    my value_type = scope_make_interface_from(&my inner_scope);

    scope_init_declaration(scope, &my declaration, (DeclProps){
        .name = my name,
        .full = my full_span,
        .tag = DECLARATION_NAMESPACE,
        .value_type = type_ref_always(&my value_type),
    });
}

void namespace_decl_resolve_names(NamespaceDecl* me, NameStack* names, ErrorCtx* errs) {
    scope_push_to_stack(&my inner_scope, names);
    c_foreach(i, vec_statement, my contents.statements) {
        statement_resolve_names(i.ref, names, errs);
    }
    scope_pop_from_stack(&my inner_scope);
}

void namespace_decl_typecheck(NamespaceDecl* me, ErrorCtx* errs) {
    block_typecheck(&my contents, (BlockTypecheckCtx){0}, errs);
}

// If statements

czview for_each_loop_statement_keyword_str(ForEachLoopStatement const* me) {
    switch (my keyword) {
        case FOR_EACH_LOOP_OF: return c_zv("of");
        case FOR_EACH_LOOP_IN: return c_zv("in");
    }
    UNREACHABLE;
}

Type for_each_loop_statement_evaluate_var_type(ForEachLoopStatement* me) {
    Type iterable_owned = {0};
    Type const* iterable_type = type_resolve(lazy_type_get(&my iterable.lazy_type));

    Type var_type = type_any;
    switch (my keyword) {
        case FOR_EACH_LOOP_IN:
            switch (iterable_type->tag) {
                case TYPE_ANY:
                    var_type = iterable_type->any.self_referential_error ? type_self_referential_error : type_string;
                    break;

                case TYPE_INTERFACE:
                case TYPE_CALLABLE:
                    // TODO: there def needs to be more nuance in this check
                    // TODO: maybe do keyof and then index of the returned union?
                    var_type = type_string;
                    break;

                case TYPE_ARRAY:
                case TYPE_TUPLE:
                    var_type = type_string;
                    break;

                default:
                    var_type = type_string;
                    break;
            }
            break;

        case FOR_EACH_LOOP_OF:
            if (type_is_self_referential(iterable_type)) {
                var_type = type_self_referential_error;
            } else if (type_extends_array(iterable_type) || type_extends(iterable_type, &type_string)) {
                var_type = index_type_evaluate_type(iterable_type, &type_number);
            }
            break;
    }

    type_drop(&iterable_owned);
    return var_type;
}

// Statements

void statement_drop(Statement* me) {
    switch (my tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_BLOCK:
            block_drop(&my block);
            break;

        case STATEMENT_IF_ELSE:
            vec_expr_drop(&my if_else.conditions);
            vec_statement_drop(&my if_else.statements);
            break;

        case STATEMENT_FOR_LOOP:
            statement_drop(&my for_loop->initialization);
            expr_drop(&my for_loop->condition);
            expr_drop(&my for_loop->afterthought);
            statement_drop(&my for_loop->body);
            free(my for_loop);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT: variable_statement_drop(&my for_each_loop->variable.statement); break;
                case FOR_EACH_LOOP_EXPR: expr_drop(&my for_each_loop->variable.expr); break;
            }
            lazy_type_drop(&my for_each_loop->var_type);
            expr_drop(&my for_each_loop->iterable);
            statement_drop(&my for_each_loop->body);
            free(my for_each_loop);
            break;

        case STATEMENT_SWITCH:
            expr_drop(&my switch_->needle);
            vec_switch_case_drop(&my switch_->cases);
            free(my switch_);
            break;

        case STATEMENT_TRY_CATCH:
            block_drop(&my try_catch->try_block);
            binding_drop(&my try_catch->catch_var);
            block_drop(&my try_catch->catch_block);
            block_drop(&my try_catch->finally_block);
            free(my try_catch);
            break;

        case STATEMENT_BREAK: break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            expr_drop(&my expr);
            break;

        case STATEMENT_IMPORT:
            import_statement_drop(my import);
            free(my import);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_drop(&my decl.variable);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_drop(my decl.function.impl);
            free(my decl.function.impl);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_drop(my decl.enum_);
            free(my decl.enum_);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_drop(my decl.class);
            free(my decl.class);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_drop(&my decl.type);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_drop(my decl.interface);
            free(my decl.interface);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_drop(&my decl.namespace);
            break;
    }
}

void statement_add_names(Statement* me, Scope* scope) {
    switch (my tag) {
        case STATEMENT_IMPORT:
            import_statement_add_names(my import, scope);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_add_names(&my decl.variable, my decl.declared, scope);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_add_names(my decl.function.impl, scope);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_add_names(my decl.enum_, scope);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_add_names(my decl.class, scope);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_add_names(&my decl.type, scope);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_add_names(my decl.interface, scope);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_add_names(&my decl.namespace, scope);
            break;

        case STATEMENT_NOOP:
        case STATEMENT_BLOCK:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_SWITCH:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_BREAK:
        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            break;
    }
}

void statement_resolve_names(Statement* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case STATEMENT_BLOCK:
            block_resolve_names(&my block, names, errs);
            break;

        case STATEMENT_IF_ELSE:
            c_foreach(i, vec_expr, my if_else.conditions) {
                expr_lookup_names(i.ref, names, errs);
            }
            c_foreach(i, vec_statement, my if_else.statements) {
                Scope branch_scope = scope_init(errs);
                statement_add_names(i.ref, &branch_scope);
                scope_push_to_stack(&branch_scope, names);
                statement_resolve_names(i.ref, names, errs);
                scope_drop(&branch_scope);
            }
            break;

        case STATEMENT_FOR_LOOP: {
            Scope loop_scope = scope_init(errs);
            statement_add_names(&my for_loop->initialization, &loop_scope);

            scope_push_to_stack(&loop_scope, names);
            statement_resolve_names(&my for_loop->initialization, names, errs);
            expr_lookup_names(&my for_loop->condition, names, errs);
            expr_lookup_names(&my for_loop->afterthought, names, errs);
            scope_pop_from_stack(&loop_scope);

            statement_add_names(&my for_loop->body, &loop_scope);

            scope_push_to_stack(&loop_scope, names);
            statement_resolve_names(&my for_loop->body, names, errs);
            scope_drop(&loop_scope);
        }   break;

        case STATEMENT_FOR_EACH_LOOP: {
            my for_each_loop->var_type.tag = LAZY_TYPE_FOR_EACH_LOOP_VAR;
            Scope loop_scope = scope_init(errs);
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    VecInitializedBinding* initializers = &my for_each_loop->variable.statement.initializers;
                    intptr_t var_size = vec_initialized_binding_size(initializers);
                    if (var_size > 1) {
                        error_ctx_error(
                            errs,
                            vec_initialized_binding_at(initializers, 1)->binding.content_span,
                            my for_each_loop->keyword == FOR_EACH_LOOP_OF ? 1188 : 1091,
                            "Only a single variable declaration is allowed in a 'for...%s' statement.",
                            for_each_loop_statement_keyword_str(my for_each_loop).str
                        );
                    } else if (var_size == 0) {
                        // Error added by parser
                    } else {
                        InitializedBinding* initializer = initializers->data;
                        if (initializer->value.tag != EXPR_IMPLICIT_ANY) {
                            error_ctx_error(
                                errs,
                                initializer->binding.content_span,
                                1189,
                                "The variable declaration of a 'for...%s' statement cannot have an initializer.",
                                for_each_loop_statement_keyword_str(my for_each_loop).str
                            );
                        } else if (initializer->binding.has_explicit_type) {
                            error_ctx_error(
                                errs,
                                initializer->binding.content_span,
                                2404,
                                "The left-hand side of a 'for...%s' statement cannot use a type annotation.",
                                for_each_loop_statement_keyword_str(my for_each_loop).str
                            );
                        }
                    }
                    c_foreach(i, vec_initialized_binding, *initializers) {
                        // If we set has_explicit_type = false here but don't clear the type, the type will still show
                        // up in test AST traversial (as required), but will not be typechecked (as required). This
                        // is dumb but it works.
                        i.ref->binding.has_explicit_type = false;
                        binding_add_names(&i.ref->binding, type_lazy(&my for_each_loop->var_type), &loop_scope);
                    }
                    scope_push_to_stack(&loop_scope, names);
                    c_foreach(i, vec_initialized_binding, *initializers) {
                        binding_lookup_names(&i.ref->binding, names, errs);
                        expr_lookup_names(&i.ref->value, names, errs);
                    }
                    scope_pop_from_stack(&loop_scope);
                    break;

                case FOR_EACH_LOOP_EXPR:
                    expr_lookup_names(&my for_each_loop->variable.expr, names, errs);
                    break;
            }

            scope_push_to_stack(&loop_scope, names);
            expr_lookup_names(&my for_each_loop->iterable, names, errs);
            scope_pop_from_stack(&loop_scope);

            statement_add_names(&my for_each_loop->body, &loop_scope);

            scope_push_to_stack(&loop_scope, names);
            statement_resolve_names(&my for_each_loop->body, names, errs);
            scope_drop(&loop_scope);
        }   break;

        case STATEMENT_SWITCH: {
            expr_lookup_names(&my switch_->needle, names, errs);
            Scope switch_scope = scope_init(errs);
            c_foreach(i, vec_switch_case, my switch_->cases) {
                c_foreach(j, vec_statement, i.ref->statements) {
                    statement_add_names(j.ref, &switch_scope);
                }
            }
            scope_push_to_stack(&switch_scope, names);
            c_foreach(i, vec_switch_case, my switch_->cases) {
                if (i.ref->is_pattern) {
                    expr_lookup_names(&i.ref->pattern, names, errs);
                }
                c_foreach(j, vec_statement, i.ref->statements) {
                    statement_resolve_names(j.ref, names, errs);
                }
            }
            scope_drop(&switch_scope);
        }   break;

        case STATEMENT_TRY_CATCH: {
            block_resolve_names(&my try_catch->try_block, names, errs);

            Scope catch_scope = scope_init(errs);
            binding_add_names(&my try_catch->catch_var, type_any, &catch_scope);
            scope_push_to_stack(&catch_scope, names);
            binding_lookup_names(&my try_catch->catch_var, names, errs);
            block_resolve_names(&my try_catch->catch_block, names, errs);
            scope_drop(&catch_scope);

            block_resolve_names(&my try_catch->finally_block, names, errs);
        }   break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_THROW_EXPR:
            expr_lookup_names(&my expr, names, errs);
            break;

        case STATEMENT_RETURN_EXPR:
            if (!name_stack_has_function(names)) {
                error_ctx_error(
                    errs,
                    my expr.outer_span,
                    1108,
                    "A 'return' statement can only be used within a function body."
                );
            }
            expr_lookup_names(&my expr, names, errs);
            break;

        case STATEMENT_IMPORT: break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_resolve_names(&my decl.variable, names, errs);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_lookup_names(my decl.function.impl, true, names, errs);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_lookup_names(my decl.enum_, names, errs);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_lookup_names(my decl.class, names, errs);
            break;

        case STATEMENT_DECL_TYPE: {
            Scope generic_scope = scope_init(errs);
            scope_push_generic_to_stack(&generic_scope, &my decl.type.generic, names);
            type_lookup_names(&my decl.type.type, names, errs);
            scope_drop(&generic_scope);
        }   break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_lookup_names(my decl.interface, names, errs);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_resolve_names(&my decl.namespace, names, errs);
            break;

        case STATEMENT_NOOP:
        case STATEMENT_BREAK:
            break;
    }
}

Type statement_evaluate_return_type(Statement* me) {
    switch (my tag) {
        case STATEMENT_BLOCK: return block_evaluate_return_type(&my block);

        case STATEMENT_IF_ELSE: {
            Type ret = type_composite_base(TYPE_UNION);
            c_foreach(i, vec_statement, my if_else.statements) {
                ret = type_composite(TYPE_UNION, ret, statement_evaluate_return_type(i.ref));
            }
            return ret;
        }

        case STATEMENT_FOR_LOOP:
            return type_composite(
                TYPE_UNION,
                statement_evaluate_return_type(&my for_loop->initialization),
                statement_evaluate_return_type(&my for_loop->body)
            );

        case STATEMENT_FOR_EACH_LOOP:
            return statement_evaluate_return_type(&my for_each_loop->body);

        case STATEMENT_SWITCH: {
            Type ret = type_composite_base(TYPE_UNION);
            c_foreach(i, vec_switch_case, my switch_->cases) {
                c_foreach(j, vec_statement, i.ref->statements) {
                    ret = type_composite(TYPE_UNION, ret, statement_evaluate_return_type(j.ref));
                }
            }
            return ret;
        }

        case STATEMENT_TRY_CATCH:
            return type_composite(
                TYPE_UNION,
                type_composite(
                    TYPE_UNION,
                    block_evaluate_return_type(&my try_catch->try_block),
                    block_evaluate_return_type(&my try_catch->catch_block)
                ),
                block_evaluate_return_type(&my try_catch->finally_block)
            );

        case STATEMENT_RETURN_EXPR: return type_clone(*expr_get_type(&my expr));

        case STATEMENT_NOOP:
        case STATEMENT_BREAK:
        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_THROW_EXPR:
        case STATEMENT_IMPORT:
        case STATEMENT_DECL_VARIABLE:
        case STATEMENT_DECL_FUNCTION:
        case STATEMENT_DECL_ENUM:
        case STATEMENT_DECL_CLASS:
        case STATEMENT_DECL_TYPE:
        case STATEMENT_DECL_INTERFACE:
        case STATEMENT_DECL_NAMESPACE: return type_never;
    }
    UNREACHABLE;
}

void statement_typecheck(Statement* me, BlockTypecheckCtx ctx, ErrorCtx* errs) {
    switch (my tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_BLOCK:
            block_typecheck(&my block, ctx, errs);
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&my if_else.statements);
            int cond_size = vec_expr_size(&my if_else.conditions);
            if (statement_size != cond_size && statement_size != cond_size + 1) {
                PANIC("invalid if statement has %d conditions and %d statements", cond_size, statement_size);
            }
            c_foreach(i, vec_expr, my if_else.conditions) {
                // Conditions do not have to be booleans
                expr_typecheck(i.ref, errs);
            }
            c_foreach(i, vec_statement, my if_else.statements) {
                statement_typecheck(i.ref, ctx, errs);
            }
            break;
        }

        case STATEMENT_FOR_LOOP: {
            statement_typecheck(&my for_loop->initialization, ctx, errs);
            expr_typecheck(&my for_loop->condition, errs);
            expr_typecheck(&my for_loop->afterthought, errs);
            ctx.break_allowed = true;
            statement_typecheck(&my for_loop->body, ctx, errs);
            break;
        }

        case STATEMENT_FOR_EACH_LOOP: {
            Type const* iterable_type = type_resolve(lazy_type_get(&my for_each_loop->iterable.lazy_type));

            switch (my for_each_loop->keyword) {
                case FOR_EACH_LOOP_IN:
                    switch (my for_each_loop->variable.tag) {
                        case FOR_EACH_LOOP_STATEMENT:
                            if (!vec_initialized_binding_empty(&my for_each_loop->variable.statement.initializers) &&
                                my for_each_loop->variable.statement.initializers.data->binding.tag != BINDING_IDENTIFIER
                            ) {
                                error_ctx_error(
                                    errs,
                                    my for_each_loop->variable.statement.initializers.data->binding.content_span,
                                    2491,
                                    "The left-hand side of a 'for...in' statement cannot be a destructuring pattern."
                                );
                            }
                            break;

                        case FOR_EACH_LOOP_EXPR:
                            if (my for_each_loop->variable.expr.tag == EXPR_ARRAY ||
                                my for_each_loop->variable.expr.tag == EXPR_OBJECT
                            ) {
                                error_ctx_error(
                                    errs,
                                    my for_each_loop->variable.expr.outer_span,
                                    2491,
                                    "The left-hand side of a 'for...in' statement cannot be a destructuring pattern."
                                );
                            }
                            break;
                    }

                    switch (iterable_type->tag) {
                        case TYPE_ANY:
                        case TYPE_INTERFACE:
                        case TYPE_CALLABLE:
                        case TYPE_ARRAY:
                        case TYPE_TUPLE:
                            break;

                        default: {
                            cstr msg = cstr_lit("The right-hand side of a 'for...in' statement must be of type 'any', an object type or a type parameter, but here has type '");
                            cstr_append_type_error_msg(&msg, iterable_type);
                            cstr_append_lit(&msg, "'.");
                            error_ctx_error_s(errs, my for_each_loop->iterable.outer_span, 2407, msg);
                        }   break;
                    }
                    break;

                case FOR_EACH_LOOP_OF:
                    if (!type_extends_array(iterable_type) && !type_extends(iterable_type, &type_string)) {
                        cstr msg = cstr_lit("Type '");
                        cstr_append_type_error_msg(&msg, iterable_type);
                        cstr_append_lit(&msg, "' is not an array type or a string type.");
                        error_ctx_error_s(errs, my for_each_loop->iterable.outer_span, 2495, msg);
                    }
                    break;
            }

            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    c_foreach(i, vec_initialized_binding, my for_each_loop->variable.statement.initializers) {
                        binding_typecheck(&i.ref->binding, errs);
                    }
                    break;

                case FOR_EACH_LOOP_EXPR:
                    expr_typecheck(&my for_each_loop->variable.expr, errs);
                    type_error_if_not_assignable(
                        lazy_type_get(&my for_each_loop->var_type),
                        expr_get_type(&my for_each_loop->variable.expr),
                        my for_each_loop->variable.expr.outer_span,
                        errs
                    );
                    break;
            }

            ctx.break_allowed = true;
            statement_typecheck(&my for_each_loop->body, ctx, errs);
        }   break;

        case STATEMENT_SWITCH: {
            expr_typecheck(&my switch_->needle, errs);
            Type const* needle_type = expr_get_type(&my switch_->needle);
            intptr_t default_count = 0;
            ctx.break_allowed = true;
            c_foreach(i, vec_switch_case, my switch_->cases) {
                if (i.ref->is_pattern) {
                    expr_typecheck(&i.ref->pattern, errs);
                    Type const* pattern_type = expr_get_type(&i.ref->pattern);
                    if (!type_overlaps(needle_type, pattern_type)) {
                        cstr message = cstr_lit("Type '");
                        cstr_append_type_error_msg(&message, pattern_type);
                        cstr_append_lit(&message, "' is not comparable to type '");
                        cstr_append_type_error_msg(&message, needle_type);
                        cstr_append_lit(&message, "'.");
                        error_ctx_error_s(errs, i.ref->pattern.outer_span, 2678, message);
                    }
                } else {
                    default_count++;
                    if (default_count == 2) {
                        error_ctx_error(
                            errs,
                            i.ref->default_span,
                            1113,
                            "A 'default' clause cannot appear more than once in a 'switch' statement."
                        );
                    }
                }
                c_foreach(j, vec_statement, i.ref->statements) {
                    statement_typecheck(j.ref, ctx, errs);
                }
            }
        }   break;

        case STATEMENT_TRY_CATCH:
            block_typecheck(&my try_catch->try_block, ctx, errs);
            binding_typecheck(&my try_catch->catch_var, errs);
            // No need to typecheck catch variable
            block_typecheck(&my try_catch->catch_block, ctx, errs);
            block_typecheck(&my try_catch->finally_block, ctx, errs);
            break;

        case STATEMENT_BREAK:
            if (!ctx.break_allowed) {
                error_ctx_error(
                    errs,
                    my break_span,
                    1105,
                    "A 'break' statement can only be used within an enclosing iteration or switch statement."
                );
            }
            break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_THROW_EXPR:
            expr_typecheck(&my expr, errs);
            break;

        case STATEMENT_RETURN_EXPR:
            expr_typecheck(&my expr, errs);
            if (ctx.specified_return) {
                type_error_if_not_assignable(
                    expr_get_type(&my expr),
                    ctx.specified_return,
                    my expr.outer_span,
                    errs
                );
            }
            break;

        case STATEMENT_IMPORT: break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_typecheck(&my decl.variable, errs);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_typecheck(my decl.function.impl, errs);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_typecheck(my decl.enum_, errs);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_typecheck(my decl.class, errs);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_typecheck(&my decl.type, errs);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_typecheck(my decl.interface, errs);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_typecheck(&my decl.namespace, errs);
            break;
    }
}

void switch_case_drop(SwitchCase* me) {
    if (my is_pattern) {
        expr_drop(&my pattern);
    }
    vec_statement_drop(&my statements);
}
