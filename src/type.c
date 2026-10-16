#include "type.h"
#include "lazy_type.h"
#include "interface.h"
#include "interface_member.h"
#include "several_types.h"
#include "callable_type.h"
#include "function_impl.h"
#include "generic.h"
#include "value.h"
#include "scope.h"
#include "display.h"
#include "error.h"
#include "math_helpers.h"
#include "ty/ty.h"
#include "unused.h"
#include "source_file.h"
#include "compiler_options.h"

const TypeNode type_any = { .tag = TYPE_ANY };
const TypeNode type_self_referential_error = { .tag = TYPE_ANY, .any.self_referential_error = true };
const TypeNode type_unknown = { .tag = TYPE_UNKNOWN };
const TypeNode type_never = { .tag = TYPE_NEVER };
const TypeNode type_void = { .tag = TYPE_VOID };
const TypeNode type_object = { .tag = TYPE_INTERFACE, .interface.members = {0} };
const TypeNode type_boolean = { .tag = TYPE_BOOLEAN };
const TypeNode type_number = { .tag = TYPE_NUMBER };
const TypeNode type_string = { .tag = TYPE_STRING };
const TypeNode type_symbol = { .tag = TYPE_SYMBOL };
const TypeNode type_bigint = { .tag = TYPE_BIGINT };
const TypeNode type_null = { .tag = TYPE_VALUE, .value.tag = VALUE_NULL };
const TypeNode type_undefined = { .tag = TYPE_VALUE, .value.tag = VALUE_UNDEFINED };

TypeNode type_composite_without_simplify(typeof((TypeNode){}.tag) tag, TypeNode a, TypeNode b) {
    // (A written operand in parentheses stays one, for reprinting)
    // (So does a JSDoc `?T`, a node of its own)
    if ((a.parenthesized || a.jsdoc_nullable) && a.tag == tag) a = (TypeNode){ .tag = tag, .parsed = true, .composite = c_init(vec_type, {a}) };
    if ((b.parenthesized || b.jsdoc_nullable) && b.tag == tag) b = (TypeNode){ .tag = tag, .parsed = true, .composite = c_init(vec_type, {b}) };
    if (a.tag == tag && b.tag == tag) {
        // Doesn't seem to be a more elegant way to append one vec to another
        vec_type_insert_n(&a.composite, vec_type_size(&a.composite), b.composite.data, vec_type_size(&b.composite));
        b.composite.size = 0; // fine, becuse the other vec now owns the data
        type_drop(&b);
        return a;
    } else if (a.tag == tag) {
        vec_type_push(&a.composite, b);
        return a;
    } else if (b.tag == tag) {
        vec_type_insert_n(&b.composite, 0, &a, 1);
        return b;
    } else {
        return (TypeNode){
            .tag = tag,
            .composite = c_init(vec_type, {a, b}),
        };
    }
}

void type_drop(TypeNode* me) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_UNIQUE_SYMBOL:
        case TYPE_BIGINT:
            break;

        case TYPE_VALUE:
            value_drop(&my value);
            break;

        case TYPE_IDENTIFIER:
            identifier_type_drop(my identifier);
            ast_delete(my identifier);
            break;

        case TYPE_INFERRED:
            inferred_type_drop(my inferred);
            ast_delete(my inferred);
            break;

        case TYPE_CALLABLE:
            callable_type_drop(my callable);
            ast_delete(my callable);
            break;

        case TYPE_INTERFACE:
            interface_drop(&my interface);
            break;

        case TYPE_ARRAY:
            type_drop(my array.element);
            ast_delete(my array.element);
            break;

        case TYPE_TUPLE:
            vec_tuple_element_drop(&my tuple.elements);
            break;

        case TYPE_INDEXED:
            indexed_type_drop(my indexed);
            ast_delete(my indexed);
            break;

        case TYPE_KEYOF:
            keyof_type_drop(my keyof);
            ast_delete(my keyof);
            break;

        case TYPE_TYPEOF:
            typeof_type_drop(my typeof_);
            ast_delete(my typeof_);
            break;

        case TYPE_TERNARY:
            ternary_type_drop(my ternary);
            ast_delete(my ternary);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            vec_type_drop(&my composite);
            break;

        case TYPE_TEMPLATE:
            template_type_drop(my template_);
            ast_delete(my template_);
            break;

        case TYPE_JSDOC:
            type_drop(&my jsdoc->type);
            ast_delete(my jsdoc);
            break;

        case TYPE_PREDICATE:
            if (my predicate->has_type) type_drop(&my predicate->type);
            ast_delete(my predicate);
            break;
    }
    my tag = TYPE_ANY; // Makes repeated calls to drop safe
}

// The member of a mapped type node as written (not parenthesized), null for anything else
static InterfaceMember const* mapped_member_of(TypeNode const* node) {
    if (node->tag != TYPE_INTERFACE || node->parenthesized || vec_interface_member_size(&node->interface.members) != 1) return nullptr;
    InterfaceMember const* member = vec_interface_member_at(&node->interface.members, 0);
    return member->key.tag == INTERFACE_KEY_MAPPED ? member : nullptr;
}

// Inside an import type's type arguments (see type_add_inferred_names_at())
static _Thread_local u32 infer_unchecked_depth;

// Declares the `infer` type parameters of an extends type. `position` is where `me` is written (its immediate parent)
static void type_add_inferred_names_at(TypeNode const* me, Scope* scope, InferPosition position, IdentifierType const* reference, u32 arg_index) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_UNIQUE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
            break;

        case TYPE_IDENTIFIER: {
            // (tsc's checkImportType doesn't check an import type's type arguments: an `infer` in them is never
            // registered for the unused check)
            bool import_type = identifier_type_import_ref(my identifier);
            infer_unchecked_depth += import_type;
            for_each (i, vec_spanned_type, &my identifier->generic_params) {
                type_add_inferred_names_at(&i->type, scope, INFER_POSITION_TYPE_ARG, my identifier, (u32)i_index);
            }
            infer_unchecked_depth -= import_type;
            break;
        }

        case TYPE_INFERRED:
            scope_init_declaration(scope, &my inferred->owned_symbol, (DeclProps){
                .name = my inferred->name,
                .full = my inferred->name,
                .kind = SYMBOL_DECL_INFER,
                .node = my inferred,
            });
            if (!infer_unchecked_depth) unused_register(scope, UNUSED_INFER, my inferred);
            my inferred->position = position;
            my inferred->reference = reference;
            my inferred->arg_index = arg_index;
            break;

        case TYPE_CALLABLE:
            for_each (i, vec_function_arg, &my callable->function->args) {
                if (i->initializer.binding.has_explicit_type && i->initializer.binding.explicit_type) {
                    InferPosition arg_position = i->rest ? INFER_POSITION_REST : INFER_POSITION_OTHER;
                    type_add_inferred_names_at(binding_type(&i->initializer.binding), scope, arg_position, nullptr, 0);
                }
            }
            type_add_inferred_names_at(&my callable->function->ret, scope, INFER_POSITION_OTHER, nullptr, 0);
            if (my callable->function->predicate && my callable->function->predicate->has_type) {
                type_add_inferred_names_at(&my callable->function->predicate->type, scope, INFER_POSITION_OTHER, nullptr, 0);
            }
            break;

        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &my interface.members) {
                if (i->key.tag == INTERFACE_KEY_MAPPED) {
                    type_add_inferred_names_at(&i->key.mapped->in_type, scope, INFER_POSITION_MAPPED_KEY, nullptr, 0);
                    if (i->key.mapped->has_as_type) {
                        type_add_inferred_names_at(&i->key.mapped->as_type, scope, INFER_POSITION_OTHER, nullptr, 0);
                    }
                }
                type_add_inferred_names_at(&i->type, scope, INFER_POSITION_OTHER, nullptr, 0);
            }
            break;

        case TYPE_ARRAY:
            type_add_inferred_names_at(my array.element, scope, INFER_POSITION_OTHER, nullptr, 0);
            break;

        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &my tuple.elements) {
                InferPosition element_position = i->rest ? INFER_POSITION_REST : INFER_POSITION_OTHER;
                type_add_inferred_names_at(&i->type, scope, element_position, nullptr, 0);
            }
            break;

        case TYPE_INDEXED:
            type_add_inferred_names_at(&my indexed->interface, scope, INFER_POSITION_OTHER, nullptr, 0);
            type_add_inferred_names_at(&my indexed->index, scope, INFER_POSITION_OTHER, nullptr, 0);
            break;

        case TYPE_KEYOF:
            type_add_inferred_names_at(&my keyof->interface, scope, INFER_POSITION_OTHER, nullptr, 0);
            break;

        // (`typeof Type<infer V>`: the type arguments of an instantiation expression in a type query)
        case TYPE_TYPEOF:
            if (my typeof_->expr->tag == EXPR_GENERIC_PARAMS) {
                for_each (i, vec_spanned_type, &my typeof_->expr->generic_params->params) {
                    type_add_inferred_names_at(&i->type, scope, INFER_POSITION_OTHER, nullptr, 0);
                }
            }
            break;

        case TYPE_TERNARY:
            // A nested conditional's extends type declares its own `infer`s, the rest belong to the nearest
            // conditional whose extends type holds them (tsc's getInferTypeContainer)
            type_add_inferred_names_at(&my ternary->maybe_sub, scope, INFER_POSITION_OTHER, nullptr, 0);
            type_add_inferred_names_at(&my ternary->if_true, scope, INFER_POSITION_OTHER, nullptr, 0);
            type_add_inferred_names_at(&my ternary->if_false, scope, INFER_POSITION_OTHER, nullptr, 0);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &my composite) {
                type_add_inferred_names_at(i, scope, INFER_POSITION_OTHER, nullptr, 0);
            }
            break;

        case TYPE_TEMPLATE:
            for_each (i, vec_type, &my template_->types) {
                type_add_inferred_names_at(i, scope, INFER_POSITION_TEMPLATE, nullptr, 0);
            }
            break;

        case TYPE_JSDOC: type_add_inferred_names_at(&my jsdoc->type, scope, position, reference, arg_index); break;
        case TYPE_PREDICATE: break;

    }
}

bool type_literal_has_index_constraints(TypeNode const* me) {
    if (my tag != TYPE_INTERFACE) return false;
    // Only a literal with an index signature and something else to check against it (the others have nothing to
    // check: not lowering them saves the work)
    u32 index_count = 0, other_count = 0;
    for_each (i, vec_interface_member, &my interface.members) {
        if (i->key.tag == INTERFACE_KEY_MAPPED) return false;
        if (i->key.tag == INTERFACE_KEY_INDEX) index_count++;
        else if (i->key.tag != INTERFACE_KEY_CALLABLE) other_count++;
    }
    return index_count && (other_count || index_count > 1);
}

// The true branches around the node being walked (TypeNode.flow)
static thread_local TypeFlow const* current_flow;

static TypeFlow const* flow_flipped(TypeFlow const* flow) {
    if (!flow) return nullptr;
    return flow->ternary ? &flow->ternary->true_flow[flow->covariant] : &flow->mapped->key.mapped->template_flow[flow->covariant];
}

TypeFlow const* type_flow_enter_mapped_template(InterfaceMember* mapped) {
    TypeFlow const* saved = current_flow;
    mapped->key.mapped->template_flow[0] = (TypeFlow){ .mapped = mapped, .covariant = true, .outer = current_flow };
    mapped->key.mapped->template_flow[1] = (TypeFlow){ .mapped = mapped, .covariant = false, .outer = flow_flipped(current_flow) };
    current_flow = &mapped->key.mapped->template_flow[0];
    return saved;
}

TypeFlow const* type_flow_enter_parameter(void) {
    TypeFlow const* saved = current_flow;
    current_flow = flow_flipped(current_flow);
    return saved;
}

TypeFlow const* type_flow_enter_statements(void) {
    TypeFlow const* saved = current_flow;
    current_flow = nullptr;
    return saved;
}

void type_flow_restore(TypeFlow const* saved) {
    current_flow = saved;
}

// The type parameters that conditional types around the node being looked up distribute over (see TYPE_TERNARY)
static thread_local struct {
    Symbol const* symbols[64];
    u32 len;
} distributing;

thread_local bool type_lookup_lazy_silent;

void type_lookup_names(TypeNode* me, Scope* names, ErrorCtx* errs) {
    my flow = current_flow;
    // What tsc only resolves when a type's members are (a type literal's, a signature's), under an import type's type
    // arguments, which tsc's checkImportType doesn't check: nothing to say (see type_lookup_lazy_silent)
    if (type_lookup_lazy_silent && errs && (my tag == TYPE_INTERFACE || my tag == TYPE_CALLABLE)) {
        ErrorCtx silent = error_ctx_scratch(errs);
        type_lookup_lazy_silent = false;
        type_lookup_names(me, names, &silent);
        type_lookup_lazy_silent = true;
        error_ctx_drop(&silent);
        return;
    }
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_UNIQUE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
            break;

        // (`infer X extends C`: C is resolved where the extends type is)
        case TYPE_INFERRED:
            if (!my inferred->owned_symbol) {
                error_ctx_error_d(errs, my inferred->full_span, 1338, "'infer' declarations are only permitted in the 'extends' clause of a conditional type.");
            }
            type_lookup_names(&my inferred->extends, names, errs);
            break;
        case TYPE_IDENTIFIER:
            identifier_type_lookup_names(my identifier, names, errs);
            if (distributing.len && my identifier->target && (my identifier->target->flags & SYMBOL_TYPE_PARAMETER)) {
                for (u32 i = 0; i < distributing.len; i++) {
                    if (distributing.symbols[i] == my identifier->target) my identifier->distributed = true;
                }
            }
            break;
        case TYPE_CALLABLE: callable_type_resolve_names(my callable, names, errs); break;

        case TYPE_INTERFACE: {
            // The members of a type literal are what a `this` type in them would be about (tsc's getThisContainer),
            // and they have none
            ScopePendingMark pending_mark = scope_pending_mark(names->binder);
            Scope* literal_scope = scope_new(names, SCOPE_BLOCK);
            // Computed names in there are in a type: not uses of values (tsc's isInAmbientOrTypeNode)
            bool saved_type_query = names->flow->in_type_query;
            names->flow->in_type_query = true;
            // (Not a mapped type's: it has no member declarations for getThisContainer to stop at, `{ [K in keyof
            // this["a"]]: ... }` in an interface member is about the interface)
            bool mapped = vec_interface_member_size(&my interface.members) == 1 &&
                my interface.members.data[0].key.tag == INTERFACE_KEY_MAPPED;
            if (!mapped) scope_rare_mut(literal_scope)->this_ref = (ThisRef){ .kind = THIS_TYPE_LITERAL };
            if (vec_interface_member_size(&my interface.members) == 1) {
                InterfaceMember const* only = my interface.members.data;
                if (only->key.tag == INTERFACE_KEY_EXPR && !only->is_method && !only->accessor_kind) {
                    scope_rare_mut(literal_scope)->lone_computed_key = only->key.expr;
                }
            }
            for_each (i, vec_interface_member, &my interface.members) {
                interface_member_lookup_names(i, literal_scope, errs);
            }
            names->flow->in_type_query = saved_type_query;
            scope_release(literal_scope, pending_mark);
        }   break;

        case TYPE_ARRAY:
            type_lookup_names(my array.element, names, errs);
            break;

        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &my tuple.elements) {
                type_lookup_names(&i->type, names, errs);
            }
            break;

        case TYPE_INDEXED: indexed_type_lookup_names(my indexed, names, errs); break;
        case TYPE_KEYOF: keyof_type_lookup_names(my keyof, names, errs); break;
        case TYPE_TYPEOF: typeof_type_lookup_names(my typeof_, names, errs); break;

        case TYPE_TERNARY: {
            type_lookup_names(&my ternary->maybe_sub, names, errs);
            // (Declared before the extends type is looked up, which tells an `infer` without a conditional of its own)
            Scope* inferred_types_scope = scope_new(names, SCOPE_BLOCK);
            type_add_inferred_names_at(&my ternary->maybe_super, inferred_types_scope, INFER_POSITION_OTHER, nullptr, 0);
            // An `infer X` that is the template of a mapped extends type, the check type being a mapped type too (tsc's
            // getInferredTypeParameterConstraint)
            InterfaceMember const* super_mapped = mapped_member_of(&my ternary->maybe_super);
            InterfaceMember const* sub_mapped = mapped_member_of(&my ternary->maybe_sub);
            if (super_mapped && sub_mapped && super_mapped->type.tag == TYPE_INFERRED && sub_mapped->type.parsed) {
                super_mapped->type.inferred->position = INFER_POSITION_MAPPED_TEMPLATE;
                super_mapped->type.inferred->check_mapped = sub_mapped;
            }
            // A bare type parameter check type distributes: references to it anywhere in the conditional (the check type
            // too) are its distributed twin (tsgo 7.1's getDistributedTypeParameter)
            u32 saved_distributing = distributing.len;
            {
                TypeNode const* check = &my ternary->maybe_sub;
                IdentifierType* id = check->tag == TYPE_IDENTIFIER && !check->parenthesized ? check->identifier : nullptr;
                if (id && !id->keyword && !identifier_type_namespace_count(id) && !(id->rare && id->rare->import_ref) &&
                    !vec_spanned_type_size(&id->generic_params) && id->target && (id->target->flags & SYMBOL_TYPE_PARAMETER) &&
                    distributing.len < c_arraylen(distributing.symbols)
                ) {
                    distributing.symbols[distributing.len++] = id->target;
                }
            }
            type_lookup_names(&my ternary->maybe_super, names, errs);

            TypeFlow const* saved_flow = current_flow;
            my ternary->true_flow[0] = (TypeFlow){ .ternary = my ternary, .covariant = true, .outer = current_flow };
            my ternary->true_flow[1] = (TypeFlow){ .ternary = my ternary, .covariant = false, .outer = flow_flipped(current_flow) };
            current_flow = &my ternary->true_flow[0];
            type_lookup_names(&my ternary->if_true, inferred_types_scope, errs);
            current_flow = saved_flow;

            type_lookup_names(&my ternary->if_false, names, errs);
            distributing.len = saved_distributing;
        }   break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &my composite) {
                type_lookup_names(i, names, errs);
            }
            break;

        case TYPE_TEMPLATE:
            for_each (i, vec_type, &my template_->types) {
                // (An `infer X` outside a conditional, TS1338, still has its implied constraint: see lower.c)
                if (i->tag == TYPE_INFERRED && !i->inferred->owned_symbol) i->inferred->position = INFER_POSITION_TEMPLATE;
                type_lookup_names(i, names, errs);
            }
            break;

        case TYPE_JSDOC: type_lookup_names(&my jsdoc->type, names, errs); break;
        // (The names of the type, for the symbols baseline: tsc checks nothing else in a misplaced predicate)
        case TYPE_PREDICATE: if (my predicate->has_type) type_lookup_names(&my predicate->type, names, errs); break;
    }
}

// tsc's checkJSDocTypeIsInJsFile for JSDocNullableType/JSDocNonNullableType: what the form should have been written as
static void jsdoc_type_typecheck(TypeNode const* me, ErrorCtx* errs) {
    JsdocType const* jsdoc = my jsdoc;
    if (!span_source(jsdoc->span) || span_source(jsdoc->span)->is_js) return;
    Checker* checker = current_checker;
    TypeNode const* inner = &jsdoc->type;
    if (!jsdoc->non_null) {
        check(inner->tag == TYPE_UNION && vec_type_size(&inner->composite) == 2);
        inner = vec_type_at(&inner->composite, 0);
    }
    Ty const* type = ty_of_node(inner);
    if (!jsdoc->non_null && type != ty_intrinsic(checker, TY_NEVER) && type != ty_intrinsic(checker, TY_VOID)) {
        Ty const* members[3] = { type, ty_intrinsic(checker, TY_UNDEFINED), ty_intrinsic(checker, TY_NULL) };
        type = ty_union(checker, members, jsdoc->postfix ? 2 : 3, nullptr, (TyList){0});
    }
    cstr text = ty_to_cstr(checker, type);
    error_ctx_grammar_error_d(errs, jsdoc->span, jsdoc->postfix ? 17019 : 17020,
        "'", jsdoc->non_null ? "!" : "?", "' at the ", jsdoc->postfix ? "end" : "start",
        " of a type is not valid TypeScript syntax. Did you mean to write '", text, "'?");
    cstr_drop(&text);
}

void type_typecheck(TypeNode const* me, ErrorCtx* errs) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_UNIQUE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
            break;

        case TYPE_INFERRED:
            type_typecheck(&my inferred->extends, errs);
            ty_check_infer_constraints(current_checker, my inferred, errs);
            break;
        case TYPE_IDENTIFIER:
            identifier_type_typecheck(my identifier, errs);
            // tsc's checkTypeReferenceNode resolves the reference (getTypeFromTypeReference): a generic alias's
            // instantiation runs into its limits here, at the reference (TS2589)
            if (my identifier->target && (my identifier->target->flags & SYMBOL_TYPE_ALIAS) &&
                vec_spanned_type_size(&my identifier->generic_params)
            ) {
                CurrentNode saved = ty_enter_node(current_checker, identifier_type_span(my identifier));
                ty_of_node(me);
                ty_leave_node(current_checker, saved);
            }
            break;
        case TYPE_CALLABLE: callable_type_typecheck(my callable, errs); break;
        case TYPE_INTERFACE:
            // A type literal (an interface's body is checked by interface_decl_typecheck())
            interface_typecheck(&my interface, nullptr, errs);
            // tsc's checkMappedType: a mapped type without a type (reportImplicitAny)
            if (vec_interface_member_size(&my interface.members) &&
                vec_interface_member_at(&my interface.members, 0)->key.tag == INTERFACE_KEY_MAPPED &&
                !vec_interface_member_at(&my interface.members, 0)->type.parsed && compiler_options()->no_implicit_any
            ) {
                error_ctx_error_d(errs, my interface.span, 7039, "Mapped object type implicitly has an 'any' template type.");
            }
            // (Its keys, the `as` type if any, must be property keys: tsc's checkMappedType)
            if (vec_interface_member_size(&my interface.members) &&
                vec_interface_member_at(&my interface.members, 0)->key.tag == INTERFACE_KEY_MAPPED
            ) {
                InterfaceMember const* mapped = vec_interface_member_at(&my interface.members, 0);
                bool as = mapped->key.mapped->has_as_type;
                Ty const* keys = ty_of_node(as ? &mapped->key.mapped->as_type : &mapped->key.mapped->in_type);
                Checker* checker = current_checker;
                Ty const* all[3] = { ty_intrinsic(checker, TY_STRING), ty_intrinsic(checker, TY_NUMBER), ty_intrinsic(checker, TY_ES_SYMBOL) };
                // (A circular key constraint is the error type: tsc's getConstraintTypeFromMappedType)
                if (as || !mapped->key.mapped->owned_symbol ||
                    !ty_param_constraint_is_circular(checker, ty_type_param(checker, mapped->key.mapped->owned_symbol))) {
                    ty_check_assignable(checker, keys, ty_union(checker, all, 3, nullptr, (TyList){0}),
                        as ? mapped->key.mapped->as_type_span : mapped->key.mapped->in_type_span, errs);
                }
            }
            {
                VecInterfaceMember const* list = &my interface.members;
                ty_check_object_type_duplicate_members(current_checker, &list, 1, errs);
                ty_check_property_modifiers(current_checker, nullptr, nullptr, list, errs);
                InterfaceMember const* indexes[16];
                u32 index_count = 0;
                for_each (i, vec_interface_member, &my interface.members) {
                    if (i->key.tag == INTERFACE_KEY_INDEX && index_count < c_arraylen(indexes)) indexes[index_count++] = i;
                }
                if (index_count > 1) ty_check_duplicate_index_signatures(current_checker, indexes, index_count, errs);
            }
            if (type_literal_has_index_constraints(me)) {
                Ty const* type = ty_of_node(me);
                ty_check_index_constraints(current_checker, type, nullptr, false, nullptr, 0, (Span){0}, errs);
            }
            break;
        case TYPE_ARRAY: type_typecheck(my array.element, errs); break;

        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &my tuple.elements) {
                type_typecheck(&i->type, errs);
            }
            // tsc's checkTupleType: a variadic element (`...T`, T no array type syntactically) must be array-like; a
            // rest element (`...T[]`, `...[...T[]]`) can't follow another nor be followed by an optional one, and a
            // required element can't follow an optional one (grammar errors). The first error ends the walk
            bool seen_optional = false, seen_rest = false;
            for_each (i, vec_tuple_element, &my tuple.elements) {
                if (!span_valid(i->span)) continue;
                bool is_rest = false;
                if (i->rest && i->type.tag == TYPE_ARRAY) {
                    is_rest = true;
                } else if (i->rest) {
                    Ty const* type = ty_of_node(&i->type);
                    // (A generic one too, through its constraint: a mapped type over a type parameter that isn't
                    // constrained to an array isn't array-like, tsc's isArrayLikeType)
                    bool skip = i->type.tag == TYPE_TUPLE;
                    if (!skip && !ty_extends_array(current_checker, type)) {
                        error_ctx_error_d(errs, i->span, 2574, "A rest element type must be an array type.");
                        break;
                    }
                    TupleShape const* shape = ty_tuple_shape(type);
                    bool tuple_rest = false;
                    // (A spread of a type variable is variadic, not rest: tsc's combinedFlags & ElementFlags.Rest,
                    // `...[...I]` with I extends readonly X[] can be followed by an optional element)
                    TyList shape_args = shape && type->kind == TY_REFERENCE ? ty_reference_args(current_checker, type) : (TyList){0};
                    for (u32 k = 0; shape && k < shape->arity && !tuple_rest; k++) {
                        Ty const* element = k < shape_args.len ? shape_args.data[k] : nullptr;
                        tuple_rest = (shape->flags[k] & TUPLE_ELEMENT_REST) &&
                            !(element && (element->kind == TY_TYPE_PARAM || (element->flags & TYF_DEFERRED)));
                    }
                    is_rest = ty_is_array(current_checker, type) || tuple_rest;
                }
                if (is_rest) {
                    if (seen_rest) {
                        error_ctx_grammar_error_d(errs, i->span, 1265, "A rest element cannot follow another rest element.");
                        break;
                    }
                    seen_rest = true;
                } else if (i->optional) {
                    if (seen_rest) {
                        error_ctx_grammar_error_d(errs, i->span, 1266, "An optional element cannot follow a rest element.");
                        break;
                    }
                    seen_optional = true;
                } else if (!i->rest && seen_optional) {
                    error_ctx_grammar_error_d(errs, i->span, 1257, "A required element cannot follow an optional element.");
                    break;
                }
            }
            break;

        case TYPE_INDEXED: indexed_type_typecheck(my indexed, errs); break;
        case TYPE_KEYOF: keyof_type_typecheck(my keyof, errs); break;
        case TYPE_TYPEOF: typeof_type_typecheck(my typeof_, errs); break;

        case TYPE_TERNARY:
            type_typecheck(&my ternary->maybe_sub, errs);
            type_typecheck(&my ternary->maybe_super, errs);
            type_typecheck(&my ternary->if_true, errs);
            type_typecheck(&my ternary->if_false, errs);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &my composite) {
                type_typecheck(i, errs);
            }
            break;

        case TYPE_TEMPLATE: {
            // At the template (tsc's checkSourceElement): what checkTemplateLiteralType relates its placeholders to
            // templateConstraintType, and resolving the template itself (getTemplateLiteralType's
            // checkCrossProductUnion: TS2590 there)
            CurrentNode saved = ty_enter_node(current_checker, my template_->span);
            template_type_typecheck(my template_, errs);
            ty_of_node(me);
            ty_leave_node(current_checker, saved);
        }   break;

        case TYPE_JSDOC:
            jsdoc_type_typecheck(me, errs);
            type_typecheck(&my jsdoc->type, errs);
            break;

        case TYPE_PREDICATE:
            // (tsc's checkTypePredicate without getTypePredicateParent: nothing else is checked)
            error_ctx_error_d(errs, my predicate->span, 1228,
                "A type predicate is only allowed in return type position for functions and methods.");
            break;
    }
}
