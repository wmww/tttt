#include "interface_member.h"
#include "interface_decl.h"
#include "type.h"
#include "expr.h"
#include "scope.h"
#include "error.h"
#include "callable_type.h"
#include "function_impl.h"
#include "ty/ty.h"
#include "flow.h"
#include "source_file.h"
#include "compiler_options.h"

void interface_member_drop(InterfaceMember* me) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: ast_cstr_drop(&my key.name_string); break;
        case INTERFACE_KEY_INDEX:
            if (!my key.index) break;
            type_drop(&my key.index->type);
            vec_span_drop(&my key.index->more_names);
            vec_span_drop(&my key.index->more_decls);
            for (isize i = 0; i < vec_span_size(&my key.index->initialized); i++) {
                expr_drop(my key.index->initializer_values[i]);
                ast_delete(my key.index->initializer_values[i]);
            }
            ast_delete(my key.index->initializer_values);
            vec_span_drop(&my key.index->initialized);
            vec_type_drop(&my key.index->more_types);
            ast_delete(my key.index);
            break;
        case INTERFACE_KEY_EXPR:
            if (my key.expr) {
                expr_drop(my key.expr);
                ast_delete(my key.expr);
            }
            break;
        case INTERFACE_KEY_MAPPED:
            if (!my key.mapped) break;
            type_drop(&my key.mapped->in_type);
            type_drop(&my key.mapped->as_type);
            ast_delete(my key.mapped);
            break;
        case INTERFACE_KEY_CALLABLE: break;
    }
    if (!my accessor || !my accessor->type_borrowed) type_drop(&my type);
    if (my accessor) {
        if (my accessor->setter_function) {
            expr_drop(my accessor->setter_function);
            ast_delete(my accessor->setter_function);
        }
        if (my accessor->initializer) {
            expr_drop(my accessor->initializer);
            ast_delete(my accessor->initializer);
        }
        if (my accessor->getter_body) {
            expr_drop(my accessor->getter_body);
            ast_delete(my accessor->getter_body);
        }
        ast_delete(my accessor);
    }
}

void interface_members_bind(VecInterfaceMember* members) {
    // Each name with an accessor (the others merge as the members builder has them, see builder_add_interface_member()):
    // its symbol is its first declaration, and the kinds that joined it; 1 property, 2 method, 4 get, 8 set
    isize count = vec_interface_member_size(members);
    for (isize j = 0; j < count; j++) {
        InterfaceMember* member = &members->data[j];
        if (!member->accessor_kind || member->key.tag != INTERFACE_KEY_NAME || interface_member_is_private_name(member)) continue;
        bool first = true;
        for (isize k = 0; k < j && first; k++) {
            InterfaceMember const* other = &members->data[k];
            first = !other->accessor_kind || other->key.tag != INTERFACE_KEY_NAME ||
                !cstr_eq(&other->key.name_string, &member->key.name_string);
        }
        if (!first) continue;
        u32 flags = 0;
        isize getter = -1, setter = -1;
        for (isize k = 0; k < count; k++) {
            InterfaceMember* other = &members->data[k];
            if (other->key.tag != INTERFACE_KEY_NAME || !cstr_eq(&other->key.name_string, &member->key.name_string)) continue;
            u32 kind = other->accessor_kind == 1 ? 4 : other->accessor_kind == 2 ? 8 :
                (other->is_method || other->key.name_is_member_function) && other->type.tag == TYPE_CALLABLE ? 2 : 1;
            // (PropertyExcludes, MethodExcludes, GetAccessorExcludes, SetAccessorExcludes)
            u32 excludes = kind == 1 ? 2u : kind == 2 ? 1u | 4u | 8u : kind == 4 ? 2u | 4u : 2u | 8u;
            if (flags & excludes) {
                other->conflicting = true;
                // (An accessor conflicting with another kind makes the symbol a full accessor: later ones conflict too)
                if ((flags & 12u) && (flags & 12u) != (kind & 12u)) flags |= 12u;
                continue;
            }
            flags |= kind;
            if (kind == 4 && getter < 0) getter = k;
            if (kind == 8 && setter < 0) setter = k;
        }
        if (getter >= 0 && setter >= 0) {
            members->data[getter].accessor->partner = (int32_t)(setter - getter);
            members->data[setter].accessor->partner = (int32_t)(getter - setter);
        }
    }
}

bool interface_member_is_misplaced_mapped(InterfaceMember const* me) {
    if (my key.tag != INTERFACE_KEY_EXPR || my is_method || my accessor_kind) return false; // (A property only)
    Expr const* key = my key.expr;
    return key->tag == EXPR_BINARY_OP && key->binary_op->op == BINARY_OP_IN && !expr_is_perenthisized(key);
}

void interface_member_lookup_names(InterfaceMember* me, Scope* names, ErrorCtx* errs) {
    // `readonly k: unique symbol`. The owner is set by the interface declaration, if there is one
    if (my type.tag == TYPE_UNIQUE_SYMBOL && my readonly && my key.tag == INTERFACE_KEY_NAME) {
        my type.unique_symbol.key = me;
        my type.unique_symbol.member = my key.span;
    }
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: break;
        case INTERFACE_KEY_INDEX: {
            // (The key is a parameter)
            TypeFlow const* saved_flow = type_flow_enter_parameter();
            type_lookup_names(&my key.index->type, names, errs);
            for_each (i, vec_type, &my key.index->more_types) type_lookup_names(i, names, errs);
            type_flow_restore(saved_flow);
            // (Resolved silently: TS2371 said it all)
            for (isize i = 0; i < vec_span_size(&my key.index->initialized); i++) {
                expr_lookup_names(my key.index->initializer_values[i], names, nullptr);
            }
        }   break;
        case INTERFACE_KEY_EXPR: {
            // (An interface's type parameters are out of reach of its members' names)
            Scope* key_names = names;
            if (scope_rare(names)->this_ref.kind == THIS_INTERFACE && scope_rare(names)->this_ref.interface) {
                key_names = scope_new(names, SCOPE_BLOCK);
                scope_rare_mut(key_names)->computed_name_of = &scope_rare(names)->this_ref.interface->generic;
            }
            // (A mapped type's member elsewhere, `[K in T]`: resolved silently, TS7061 said it all)
            scope_flow(names)->unmarked_assignments++; // (In a type node or an interface: tsc's markNodeAssignments skips both)
            expr_lookup_names(my key.expr, key_names, interface_member_is_misplaced_mapped(me) ? nullptr : errs);
            scope_flow(names)->unmarked_assignments--;
        }   break;

        case INTERFACE_KEY_MAPPED:
            // (The constraint is in the key parameter's scope too: `{ [P in P]: X }` is circular, TS2313)
            names = scope_new(names, SCOPE_BLOCK);
            scope_init_declaration(names, &my key.mapped->owned_symbol, (DeclProps){
                .name = my key.mapped->type_name,
                .full = my key.mapped->type_name,
                .kind = SYMBOL_DECL_MAPPED_KEY,
                .node = me,
            });
            type_lookup_names(&my key.mapped->in_type, names, errs);
            // The `as` clause sees the key parameter
            type_lookup_names(&my key.mapped->as_type, names, errs);
            break;

        case INTERFACE_KEY_CALLABLE: break;
    }

    if (my key.tag == INTERFACE_KEY_MAPPED && !my key.mapped->has_as_type) {
        // (The key has an implied constraint in there: conditional_flow_type(), lower.c)
        TypeFlow const* saved_flow = type_flow_enter_mapped_template(me);
        type_lookup_names(&my type, names, errs);
        type_flow_restore(saved_flow);
    } else if (!my accessor || !my accessor->type_borrowed) {
        type_lookup_names(&my type, names, errs);
    }
    // (A setter's pattern parameter: its annotation is resolved in the setter's parameter scope, see setter_function)
    if (my accessor && my accessor->setter_function) expr_lookup_names(my accessor->setter_function, names, errs);
    // (Resolved silently: the grammar error said it all)
    if (my accessor && my accessor->initializer) expr_lookup_names(my accessor->initializer, names, nullptr);
    if (my accessor && my accessor->getter_body) expr_lookup_names(my accessor->getter_body, names, nullptr);
}

// A method signature (written as one: not a property of a function type)
static bool interface_member_is_method_signature(InterfaceMember const* me) {
    bool method = my key.tag == INTERFACE_KEY_NAME ? my key.name_is_member_function : my is_method;
    return method && my type.tag == TYPE_CALLABLE;
}

Span interface_member_error_span(InterfaceMember const* me) {
    Span at = my key.span;
    if (!interface_member_is_method_signature(me)) return at;
    FunctionImpl const* function = my type.callable->function;
    if (span_valid(function->full_span) && span_source(function->full_span) == span_source(at)) {
        const char* end = span_end_ptr(function->full_span);
        const char* after = end;
        while (*after == ' ' || *after == '\t') after++;
        if (*after == ';' || *after == ',') end = after + 1;
        if (end > span_start_ptr(at)) at = span_make_in(at, span_start_ptr(at), end);
    }
    return at;
}

// tsc's reportImplicitAny of a property signature, and of a method signature's return type
static void interface_member_check_implicit_any(InterfaceMember const* me, ErrorCtx* errs) {
    if (my implicit_any && compiler_options()->no_implicit_any && span_source(my key.span) && !span_source(my key.span)->is_js) {
        error_ctx_error_d(errs, my key.span, 7008, "Member '", my key.span, "' implicitly has an 'any' type.");
    }
    // An accessor pair neither of whose sides says the type (tsc's getTypeOfAccessors): at the setter if there is one,
    // else at the getter; a setter without a getter has an implicitly any parameter too
    InterfaceAccessor const* sides = my accessor;
    bool checked = sides && my accessor_kind && my key.tag == INTERFACE_KEY_NAME && compiler_options()->no_implicit_any &&
        span_source(my key.span) && !span_source(my key.span)->is_js;
    InterfaceMember const* partner = checked && sides->partner ? me + sides->partner : nullptr;
    if (checked && !sides->annotated && !(partner && partner->accessor->annotated)) {
        if (my accessor_kind == 2) {
            error_ctx_error_d(errs, my key.span, 7032, "Property '", my key.span,
                "' implicitly has type 'any', because its set accessor lacks a parameter type annotation.");
        } else if (!partner) {
            error_ctx_error_d(errs, my key.span, 7033, "Property '", my key.span,
                "' implicitly has type 'any', because its get accessor lacks a return type annotation.");
        }
    }
    if (checked && my accessor_kind == 2 && !sides->annotated && span_valid(sides->setter_param) && !partner) {
        error_ctx_error_d(errs, sides->setter_param, 7006, "Parameter '", sides->setter_param, "' implicitly has an 'any' type.");
    }
    if (interface_member_is_method_signature(me)) {
        function_impl_check_implicit_any_return(my type.callable->function, my key.span, interface_member_error_span(me), errs);
    }
}

void interface_member_typecheck(InterfaceMember* me, bool is_in_class, bool in_type_literal, ErrorCtx* errs) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME:
            // tsc's checkPropertySignature, checkMethodDeclaration
            if (!is_in_class && interface_member_is_private_name(me)) {
                error_ctx_error_d(errs, interface_member_error_span(me), 18016,
                    "Private identifiers are not allowed outside class bodies.");
            }
            interface_member_check_implicit_any(me, errs);
            // (A getter's body, TS1183, is checked all the same: tsc's checkAccessorDeclaration)
            if (my accessor && my accessor->getter_body) function_impl_typecheck(my accessor->getter_body->function, errs);
            // (A setter's pattern parameter: TS2842 for an unused renaming, tsc's checkSignatureDeclaration)
            if (my accessor && my accessor->setter_function) function_impl_check_unused_renamings(my accessor->setter_function->function, errs);
            // (tsc's checkVariableLikeDeclaration of a property signature)
            if (!my is_method && !my key.name_is_member_function && span_is_bigint_name(my key.span, my key.name_is_value)) {
                error_ctx_error_d(errs, my key.span, 1539, "A 'bigint' literal cannot be used as a property name.");
            }
            // A property signature's initializer (TS1246/TS1247 already) is still checked against its type: tsc's
            // checkVariableLikeDeclaration, at the name
            if (my accessor && my accessor->initializer && !my is_method && !my key.name_is_member_function) {
                Checker* checker = current_checker;
                Ty const* type = ty_of_node(&my type);
                if (my optional && compiler_options()->strict_null_checks && !(type->flags & TYF_ANY)) {
                    Ty const* parts[2] = { type, ty_intrinsic(checker, TY_UNDEFINED) };
                    type = ty_union(checker, parts, 2, nullptr, (TyList){0});
                }
                ty_check_assignable_expr(checker, ty_expr_type(checker, my accessor->initializer), type, my key.span,
                    my accessor->initializer, errs);
            }
            break;

        case INTERFACE_KEY_INDEX: {
            type_typecheck(&my key.index->type, errs);
            for_each (i, vec_type, &my key.index->more_types) type_typecheck(i, errs);
            if (span_valid(my key.index->parameter_property)) {
                error_ctx_error_d(errs, my key.index->parameter_property, 2369,
                    "A parameter property is only allowed in a constructor implementation.");
            }
            // (tsc's checkVariableLikeDeclaration of a parameter of a signature without a body)
            for_each (i, vec_span, &my key.index->initialized) {
                error_ctx_error_d(errs, *i, 2371, "A parameter initializer is only allowed in a function or constructor implementation.");
            }
            // tsc's checkGrammarIndexSignatureParameters after what the parser reported (the parameter list's shape)
            if (my key.index->grammar_reported) break;
            Checker* checker = current_checker;
            Ty const* key = ty_of_node(&my key.index->type);
            TyList parts = key->kind == TY_UNION ? key->composite.types : (TyList){ .data = &key, .len = 1 };
            bool literal = false, valid = true;
            for (u32 i = 0; i < parts.len; i++) {
                literal |= (parts.data[i]->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL)) != 0;
                valid &= ty_is_valid_index_key(parts.data[i]);
            }
            if (literal || ty_is_generic_object(checker, key) || ty_is_generic_index(key)) {
                error_ctx_grammar_error_d(errs, my key.index->name, 1337, "An index signature parameter type cannot be a "
                    "literal type or generic type. Consider using a mapped object type instead.");
            } else if (!valid) {
                error_ctx_grammar_error_d(errs, my key.index->name, 1268,
                    "An index signature parameter type must be 'string', 'number', 'symbol', or a template literal type.");
            } else if (!my key.index->has_value_type) {
                error_ctx_grammar_error_d(errs, my key.index->node, 1021, "An index signature must have a type annotation.");
            }
        }   break;

        case INTERFACE_KEY_EXPR:
            if (interface_member_is_misplaced_mapped(me)) break;
            // (An accessor's `[k in T]` is no mapped type's member but a name, and no TS1169: tsc's checkAccessorDeclaration)
            ty_check_computed_name(current_checker, my key.expr, my key.span, !my accessor_kind, errs);
            if (!my accessor_kind && expr_is_non_bindable_dynamic_name(my key.expr)) {
                if (is_in_class) {
                    if (my is_method &&
                        my type.tag == TYPE_CALLABLE &&
                        my type.callable->function->has_body
                    ) {
                        // Only overload signatures are restricted, a method with a body can have any key
                    } else if (my is_method) {
                        error_ctx_grammar_error_d(errs, my key.span, 1168,
                            "A computed property name in a method overload must refer to an expression whose type is a literal type or a 'unique symbol' type.");
                    } else {
                        error_ctx_grammar_error_d(errs, my key.span, 1166,
                            "A computed property name in a class property declaration must have a simple literal type or a 'unique symbol' type.");
                    }
                } else if (in_type_literal) {
                    error_ctx_grammar_error_d(errs, my key.span, 1170,
                        "A computed property name in a type literal must refer to an expression whose type is a literal type or a 'unique symbol' type.");
                } else {
                    error_ctx_grammar_error_d(errs, my key.span, 1169,
                        "A computed property name in an interface must refer to an expression whose type is a literal type or a 'unique symbol' type.");
                }
            }
            expr_typecheck_type_only_site(my key.expr, errs);
            if (!is_in_class) interface_member_check_implicit_any(me, errs);
            break;

        case INTERFACE_KEY_MAPPED:
            type_typecheck(&my key.mapped->in_type, errs);
            if (span_valid(my key.mapped->in_type_span) && my key.mapped->owned_symbol &&
                ty_param_constraint_is_circular(current_checker, ty_type_param(current_checker, my key.mapped->owned_symbol))
            ) {
                error_ctx_error_d(errs, my key.mapped->in_type_span, 2313, "Type parameter '", my key.mapped->type_name, "' has a circular constraint.");
            }
            type_typecheck(&my key.mapped->as_type, errs);
            break;

        case INTERFACE_KEY_CALLABLE:
            // tsc's checkSignatureDeclaration under noImplicitAny
            if (my type.tag == TYPE_CALLABLE && !my type.callable->function->ret_explicit && compiler_options()->no_implicit_any &&
                span_valid(my declaration) && span_source(my declaration) &&
                !span_source(my declaration)->is_js
            ) {
                Span at = span_with_member_separator(my declaration);
                if (my type.callable->function->is_constructor) {
                    error_ctx_error_d(errs, at, 7013, "Construct signature, which lacks return-type annotation, implicitly has an 'any' return type.");
                } else {
                    error_ctx_error_d(errs, at, 7020, "Call signature, which lacks return-type annotation, implicitly has an 'any' return type.");
                }
            }
            break;
    }
    type_typecheck(&my type, errs);
}
