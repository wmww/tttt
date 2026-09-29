#include "interface_member.h"
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
    vec_span_drop(&my declarations);
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: cstr_drop(&my key.name_string); break;
        case INTERFACE_KEY_INDEX:
            type_drop(&my key.index.type);
            vec_span_drop(&my key.index.more_names);
            vec_span_drop(&my key.index.more_decls);
            vec_type_drop(&my key.index.more_types);
            break;
        case INTERFACE_KEY_EXPR:
            if (my key.expr.owned) {
                expr_drop(my key.expr.owned);
                free(my key.expr.owned);
            }
            break;
        case INTERFACE_KEY_MAPPED:
            type_drop(&my key.mapped.in_type);
            type_drop(&my key.mapped.as_type);
            break;
        case INTERFACE_KEY_CALLABLE: break;
    }
    type_drop(&my type);
    if (my has_write_type) type_drop(&my write_type);
}

bool interface_member_is_misplaced_mapped(InterfaceMember const* me) {
    if (my key.tag != INTERFACE_KEY_EXPR || my is_method || my computed_accessor) return false; // (A property only)
    Expr const* key = my key.expr.ref;
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
            type_lookup_names(&my key.index.type, names, errs);
            for_each (i, vec_type, &my key.index.more_types) type_lookup_names(i, names, errs);
            type_flow_restore(saved_flow);
        }   break;
        case INTERFACE_KEY_EXPR:
            // (A mapped type's member elsewhere, `[K in T]`: resolved silently, TS7061 said it all)
            expr_lookup_names(my key.expr.ref, names, interface_member_is_misplaced_mapped(me) ? nullptr : errs);
            break;

        case INTERFACE_KEY_MAPPED:
            type_lookup_names(&my key.mapped.in_type, names, errs);
            names = scope_new(names, SCOPE_BLOCK);
            scope_init_declaration(names, &my key.mapped.owned_symbol, (DeclProps){
                .name = my key.mapped.type_name,
                .full = my key.mapped.type_name,
                .kind = SYMBOL_DECL_MAPPED_KEY,
                .node = me,
            });
            my key.mapped.symbol_ref = my key.mapped.owned_symbol;
            // The `as` clause sees the key parameter
            type_lookup_names(&my key.mapped.as_type, names, errs);
            break;

        case INTERFACE_KEY_CALLABLE: break;
    }

    if (my key.tag == INTERFACE_KEY_MAPPED && !my key.mapped.has_as_type) {
        // (The key has an implied constraint in there: conditional_flow_type(), lower.c)
        TypeFlow const* saved_flow = type_flow_enter_mapped_template(me);
        type_lookup_names(&my type, names, errs);
        type_flow_restore(saved_flow);
    } else {
        type_lookup_names(&my type, names, errs);
    }
    if (my has_write_type) type_lookup_names(&my write_type, names, errs);
}

void interface_member_typecheck(InterfaceMember* me, bool is_in_class, ErrorCtx* errs) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME:
            // tsc's reportImplicitAny of a property signature, and of a method signature's return type
            if (my implicit_any && compiler_options()->no_implicit_any && my key.span.source && !my key.span.source->is_js) {
                error_ctx_error_d(errs, my key.span, 7008, "Member '", my key.span, "' implicitly has an 'any' type.");
            }
            // (A method signature's errors are at the whole signature, its `;` or `,` included: tsc's
            // getErrorSpanForNode has no case for it)
            Span at = my key.span;
            bool method = my key.name_is_member_function && my type.tag == TYPE_CALLABLE;
            if (method) {
                FunctionImpl const* function = my type.callable->function;
                if (span_valid(function->full_span) && function->full_span.source == at.source) {
                    const char* end = span_end_ptr(function->full_span);
                    const char* after = end;
                    while (*after == ' ' || *after == '\t') after++;
                    if (*after == ';' || *after == ',') end = after + 1;
                    if (end > span_start_ptr(at)) at = span_make(at.source, span_start_ptr(at), end);
                }
            }
            // tsc's checkPropertySignature, checkMethodDeclaration
            if (!is_in_class && interface_member_is_private_name(me)) {
                error_ctx_error_d(errs, at, 18016, "Private identifiers are not allowed outside class bodies.");
            }
            if (method) function_impl_check_implicit_any_return(my type.callable->function, my key.span, at, errs);
            break;

        case INTERFACE_KEY_INDEX: {
            type_typecheck(&my key.index.type, errs);
            for_each (i, vec_type, &my key.index.more_types) type_typecheck(i, errs);
            if (span_valid(my key.index.parameter_property)) {
                error_ctx_error_d(errs, my key.index.parameter_property, 2369,
                    "A parameter property is only allowed in a constructor implementation.");
            }
            // tsc's checkGrammarIndexSignatureParameters after what the parser reported (the parameter list's shape)
            if (my key.index.grammar_reported) break;
            Checker* checker = current_checker;
            Ty const* key = ty_of_node(&my key.index.type);
            TyList parts = key->kind == TY_UNION ? key->composite.types : (TyList){ .data = &key, .len = 1 };
            bool literal = false, valid = true;
            for (u32 i = 0; i < parts.len; i++) {
                literal |= (parts.data[i]->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL)) != 0;
                valid &= ty_is_valid_index_key(parts.data[i]);
            }
            if (literal || ty_is_generic_object(checker, key) || ty_is_generic_index(key)) {
                error_ctx_grammar_error_d(errs, my key.index.name, 1337, "An index signature parameter type cannot be a "
                    "literal type or generic type. Consider using a mapped object type instead.");
            } else if (!valid) {
                error_ctx_grammar_error_d(errs, my key.index.name, 1268,
                    "An index signature parameter type must be 'string', 'number', 'symbol', or a template literal type.");
            } else if (!my key.index.has_value_type) {
                error_ctx_grammar_error_d(errs, my key.index.node, 1021, "An index signature must have a type annotation.");
            }
        }   break;

        case INTERFACE_KEY_EXPR:
            if (interface_member_is_misplaced_mapped(me)) break;
            // (An accessor's `[k in T]` is no mapped type's member but a name, and no TS1169: tsc's checkAccessorDeclaration)
            ty_check_computed_name(current_checker, my key.expr.ref, my key.span, !my computed_accessor, errs);
            if (!my computed_accessor && expr_is_non_bindable_dynamic_name(my key.expr.ref)) {
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
                } else {
                    error_ctx_grammar_error_d(errs, my key.span, 1169,
                        "A computed property name in an interface must refer to an expression whose type is a literal type or a 'unique symbol' type.");
                }
            }
            expr_typecheck(my key.expr.ref, errs);
            break;

        case INTERFACE_KEY_MAPPED:
            type_typecheck(&my key.mapped.in_type, errs);
            if (span_valid(my key.mapped.in_type_span) && my key.mapped.symbol_ref &&
                ty_param_constraint_is_circular(current_checker, ty_type_param(current_checker, my key.mapped.symbol_ref))
            ) {
                error_ctx_error_d(errs, my key.mapped.in_type_span, 2313, "Type parameter '", my key.mapped.type_name, "' has a circular constraint.");
            }
            type_typecheck(&my key.mapped.as_type, errs);
            break;

        case INTERFACE_KEY_CALLABLE:
            // tsc's checkSignatureDeclaration under noImplicitAny
            if (my type.tag == TYPE_CALLABLE && !my type.callable->function->ret_explicit && compiler_options()->no_implicit_any &&
                vec_span_size(&my declarations) && vec_span_at(&my declarations, 0)->source &&
                !vec_span_at(&my declarations, 0)->source->is_js
            ) {
                Span at = *vec_span_at(&my declarations, 0);
                if (my type.callable->function->is_constructor) {
                    error_ctx_error_d(errs, at, 7013, "Construct signature, which lacks return-type annotation, implicitly has an 'any' return type.");
                } else {
                    error_ctx_error_d(errs, at, 7020, "Call signature, which lacks return-type annotation, implicitly has an 'any' return type.");
                }
            }
            break;
    }
    type_typecheck(&my type, errs);
    if (my has_write_type) type_typecheck(&my write_type, errs);
}
