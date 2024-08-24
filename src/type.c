#include "type.h"
#include "interface.h"
#include "interface_member.h"
#include "several_types.h"
#include "callable_type.h"
#include "function_impl.h"
#include "generic.h"
#include "value.h"
#include "name_stack.h"
#include "display.h"
#include "error.h"

const Type type_any = { .tag = TYPE_ANY };
const Type type_unknown = { .tag = TYPE_UNKNOWN };
const Type type_self_referential_error = { .tag = TYPE_ANY, .any.self_referential_error = true, };
const Type type_excessively_deep_error = { .tag = TYPE_ANY, .any.excessively_deep_error = true, };
const Type type_never = { .tag = TYPE_NEVER };
const Type type_void = { .tag = TYPE_VOID };
const Type type_object = { .tag = TYPE_INTERFACE, .interface.members = {0} };
const Type type_boolean = { .tag = TYPE_BOOLEAN };
const Type type_number = { .tag = TYPE_NUMBER };
const Type type_string = { .tag = TYPE_STRING };
const Type type_null = { .tag = TYPE_VALUE, .value.tag = VALUE_NULL };
const Type type_undefined = { .tag = TYPE_VALUE, .value.tag = VALUE_UNDEFINED };

#define i_key_ssv
#define i_val bool
#define i_type hmap_str_bool
#include "wrap_stc/hmap.h"
typedef hmap_str_bool HmapStrBool;
typedef hmap_str_bool_result HmapStrBoolResult;
typedef hmap_str_bool_value HmapStrBoolValue;

typedef struct TypeCtx {
    Type const* currently_resolving;
    int recursion_level;
    int extends_attempts;
    HmapStrBool extends_results;
} TypeCtx;

static bool type_extends_internal(
    TypeCtx* ctx,
    Type const* sub,
    Type const* super,
    VecTypeRefinement* refinements
);
static Type const* type_resolve_internal(TypeCtx* ctx, Type const* me);
static bool index_type_of_internal(
    TypeCtx* ctx,
    Type const* me,
    Type const* index,
    Span index_span,
    bool error_if_not_found,
    ErrorCtx* errs,
    Type* ret
);

static void type_ctx_drop(TypeCtx* me) {
    hmap_str_bool_drop(&my extends_results);
}

Type type_composite_base(typeof((Type){}.tag) tag) {
    ASSERT(tag == TYPE_UNION || tag == TYPE_INTERSECTION);
    return tag == TYPE_UNION ? type_never : type_any;
}

Type type_composite_without_simplify(typeof((Type){}.tag) tag, Type a, Type b) {
    if (a.tag == tag && b.tag == tag) {
        // Doesn't seem to be a more elegant way to append one vec to another
        vec_type_insert_n(&a.composite, vec_type_size(&a.composite), b.composite.data, vec_type_size(&b.composite));
        b.composite._len = 0; // fine, becuse the other vec now owns the data
        type_drop(&b);
        return a;
    } else if (a.tag == tag) {
        vec_type_push(&a.composite, b);
        return a;
    } else if (b.tag == tag) {
        vec_type_insert_n(&b.composite, 0, &a, 1);
        return b;
    } else {
        return (Type){
            .tag = tag,
            .composite = c_init(vec_type, {a, b}),
        };
    }
}

static Type type_composite_internal(TypeCtx* ctx, typeof((Type){}.tag) tag, Type a, Type b) {
    ASSERT(tag == TYPE_UNION || tag == TYPE_INTERSECTION);
    if (a.tag == TYPE_REF) {
        return type_composite(tag, type_clone(*a.ref), b);
    } else if (b.tag == TYPE_REF) {
        return type_composite(tag, a, type_clone(*b.ref));
    } else if (a.tag == TYPE_NEVER || a.tag == TYPE_ANY || b.tag == TYPE_NEVER || b.tag == TYPE_ANY) {
        typeof((Type){}.tag) override_tag = tag == TYPE_UNION ? TYPE_ANY : TYPE_NEVER;
        typeof((Type){}.tag) ignore_tag = tag == TYPE_UNION ? TYPE_NEVER : TYPE_ANY;
        if (a.tag == override_tag || b.tag == override_tag) {
            type_drop(&a);
            type_drop(&b);
            return (Type){.tag = override_tag};
        } else if (a.tag == ignore_tag) {
            return b;
        } else {
            return a;
        }
    } else if (a.tag != TYPE_INTERFACE && b.tag != TYPE_INTERFACE && type_extends_internal(ctx, &a, &b, nullptr)) {
        if (tag == TYPE_UNION) {
            type_drop(&a);
            return b;
        } else {
            type_drop(&b);
            return a;
        }
    } else if (a.tag != TYPE_INTERFACE && b.tag != TYPE_INTERFACE && type_extends_internal(ctx, &b, &a, nullptr)) {
        if (tag == TYPE_UNION) {
            type_drop(&b);
            return a;
        } else {
            type_drop(&a);
            return b;
        }
    } else {
        return type_composite_without_simplify(tag, a, b);
    }
}

Type type_composite(typeof((Type){}.tag) tag, Type a, Type b) {
    TypeCtx ctx = {0};
    Type ret = type_composite_internal(&ctx, tag, a, b);
    type_ctx_drop(&ctx);
    return ret;
}

void composite_type_simplify(Type* me) {
    ASSERT(my tag == TYPE_UNION || my tag == TYPE_INTERSECTION);
    Type ret = type_composite_base(my tag);
    c_foreach(i, vec_type, my composite) {
        ret = type_composite(my tag, ret, *i.ref);
        *i.ref = type_any;
    }
    type_drop(me);
    *me = ret;
}

void type_drop(Type* me) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_REF:
            break;

        case TYPE_VALUE:
            value_drop(&my value);
            break;

        case TYPE_IDENTIFIER:
            identifier_type_drop(my identifier);
            free(my identifier);
            break;

        case TYPE_INFERRED:
            inferred_type_drop(my inferred);
            free(my inferred);
            break;

        case TYPE_CALLABLE:
            callable_type_drop(my callable);
            free(my callable);
            break;

        case TYPE_INTERFACE:
            interface_drop(&my interface);
            break;

        case TYPE_ARRAY:
            type_drop(my array.element);
            free(my array.element);
            break;

        case TYPE_TUPLE:
            vec_tuple_element_drop(&my tuple.elements);
            break;

        case TYPE_INDEXED:
            indexed_type_drop(my indexed);
            free(my indexed);
            break;

        case TYPE_KEYOF:
            keyof_type_drop(my keyof);
            free(my keyof);
            break;

        case TYPE_TERNARY:
            ternary_type_drop(my ternary);
            free(my ternary);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            vec_type_drop(&my composite);
            break;
    }
    my tag = TYPE_ANY; // Makes repeated calls to drop safe
}

Type type_clone(Type me) {
    switch (me.tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_REF: return me;
        case TYPE_VALUE: return type_value(value_clone(me.value));
        case TYPE_IDENTIFIER: return type_identifier(c_new(IdentifierType, identifier_type_clone(*me.identifier)));
        case TYPE_INFERRED: return type_inferred(c_new(InferredType, inferred_type_clone(*me.inferred)));
        case TYPE_CALLABLE: return type_callable(c_new(CallableType, callable_type_clone(*me.callable)));
        case TYPE_INTERFACE: return type_interface(interface_clone(me.interface));
        case TYPE_ARRAY: return type_array(c_new(Type, type_clone(*me.array.element)), me.array.readonly);
        case TYPE_TUPLE: return type_tuple(vec_tuple_element_clone(me.tuple.elements), me.tuple.readonly);
        case TYPE_INDEXED: return type_indexed(c_new(IndexedType, indexed_type_clone(*me.indexed)));
        case TYPE_KEYOF: return type_keyof(c_new(KeyofType, keyof_type_clone(*me.keyof)));
        case TYPE_TERNARY: return type_ternary(c_new(TernaryType, ternary_type_clone(*me.ternary)));
        case TYPE_UNION: case TYPE_INTERSECTION: return (Type){.tag = me.tag, .composite = vec_type_clone(me.composite)};
    }
    UNREACHABLE;
}

void cstr_append_type_error_msg(cstr* target, Type const* me) {
    display_type(error_display(target), me);
}

static void type_add_inferred_names(Type const* me, NameStack* names) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_VALUE:
            break;

        case TYPE_IDENTIFIER:
            c_foreach(i, vec_spanned_type, my identifier->generic_params) {
                type_add_inferred_names(&i.ref->type, names);
            }
            break;

        case TYPE_INFERRED:
            if (my inferred->inferred_type.get) {
                NONFATAL("Inferred type added multiple times");
            } else {
                my inferred->inferred_type = arc_named_type_from_ptr(make_named_type(
                    my inferred->name,
                    my inferred->name,
                    lazy_type_value(type_clone(my inferred->extends)),
                    nullptr
                ));
                my inferred->type = my inferred->inferred_type.get;
                name_stack_add_type(names, my inferred->type);
            }
            break;

        case TYPE_CALLABLE:
            c_foreach(i, vec_callable_arg, my callable->args) {
                type_add_inferred_names(lazy_type_resolve(&i.ref->type), names);
            }
            type_add_inferred_names(&my callable->ret, names);
            break;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                Type const* member = lazy_type_try_get(&i.ref->type);
                if (member) {
                    type_add_inferred_names(member, names);
                }
            }
            break;

        case TYPE_ARRAY:
            type_add_inferred_names(my array.element, names);
            break;

        case TYPE_TUPLE:
            c_foreach(i, vec_tuple_element, my tuple.elements) {
                type_add_inferred_names(&i.ref->type, names);
            }
            break;

        case TYPE_INDEXED:
            type_add_inferred_names(&my indexed->interface, names);
            type_add_inferred_names(&my indexed->index, names);
            break;

        case TYPE_KEYOF:
            type_add_inferred_names(&my keyof->interface, names);
            break;

        case TYPE_TERNARY:
            type_add_inferred_names(&my ternary->maybe_sub, names);
            type_add_inferred_names(&my ternary->maybe_super, names);
            type_add_inferred_names(&my ternary->if_true, names);
            type_add_inferred_names(&my ternary->if_false, names);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                type_add_inferred_names(i.ref, names);
            }
            break;

        case TYPE_REF: break;
    }
}

void type_resolve_names(Type* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_VALUE:
        case TYPE_INFERRED:
            break;

        case TYPE_IDENTIFIER: identifier_type_resolve_names(my identifier, names, errs); break;
        case TYPE_CALLABLE: callable_type_resolve_names(my callable, names, errs); break;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                interface_member_resolve_names(i.ref, names, errs);
            }
            break;

        case TYPE_ARRAY:
            type_resolve_names(my array.element, names, errs);
            break;

        case TYPE_TUPLE:
            c_foreach(i, vec_tuple_element, my tuple.elements) {
                type_resolve_names(&i.ref->type, names, errs);
            }
            break;

        case TYPE_INDEXED: indexed_type_resolve_names(my indexed, names, errs); break;
        case TYPE_KEYOF: keyof_type_resolve_names(my keyof, names, errs); break;

        case TYPE_TERNARY:
            type_resolve_names(&my ternary->maybe_sub, names, errs);
            type_resolve_names(&my ternary->maybe_super, names, errs);

            name_stack_push_block_frame(names);
            type_add_inferred_names(&my ternary->maybe_super, names);
            type_resolve_names(&my ternary->if_true, names, errs);
            name_stack_pop_frame(names);

            type_resolve_names(&my ternary->if_false, names, errs);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                type_resolve_names(i.ref, names, errs);
            }
            break;

        case TYPE_REF: break;
    }
}

void type_typecheck(Type const* me, ErrorCtx* errs) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_VALUE:
        case TYPE_INFERRED:
            break;

        case TYPE_IDENTIFIER:
            identifier_type_typecheck(my identifier, errs);
            break;

        case TYPE_CALLABLE:
            callable_type_typecheck(my callable, errs);
            break;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                interface_member_typecheck(i.ref, errs);
            }
            break;

        case TYPE_ARRAY:
            type_typecheck(my array.element, errs);
            break;

        case TYPE_TUPLE:
            c_foreach(i, vec_tuple_element, my tuple.elements) {
                type_typecheck(&i.ref->type, errs);
            }
            break;

        case TYPE_INDEXED:
            indexed_type_typecheck(my indexed, errs);
            break;

        case TYPE_KEYOF:
            keyof_type_typecheck(my keyof, errs);
            break;

        case TYPE_TERNARY:
            type_typecheck(&my ternary->maybe_sub, errs);
            type_typecheck(&my ternary->maybe_super, errs);
            type_typecheck(&my ternary->if_true, errs);
            type_typecheck(&my ternary->if_false, errs);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                type_typecheck(i.ref, errs);
            }
            break;

        case TYPE_REF:
            break;
    }
}

static Type type_all_keys(TypeCtx* ctx, Type const* me) {
    me = type_resolve_internal(ctx, me);

    switch (my tag) {
        case TYPE_ANY:
            return type_any;

        case TYPE_NEVER:
            return type_never;

        case TYPE_INTERFACE: {
            Type ret = type_never;
            c_foreach(i, vec_interface_member, my interface.members) {
                switch (i.ref->key.tag) {
                    case INTERFACE_KEY_NAME:
                        ret = type_composite_internal(
                            ctx,
                            TYPE_UNION,
                            ret,
                            type_value(value_string(cstr_clone(i.ref->key.name_string)))
                        );

                    default: break;
                }
            }
            return ret;
        }

        case TYPE_ARRAY:
        case TYPE_TUPLE:
        case TYPE_STRING:
            return type_number; // TODO: needs to be more complicated

        case TYPE_UNION:
        case TYPE_INTERSECTION: {
            typeof(my tag) flipped_tag = my tag == TYPE_UNION ? TYPE_INTERSECTION : TYPE_UNION;
            Type ret = type_composite_base(flipped_tag);
            c_foreach(i, vec_type, my composite) {
                ret = type_composite_internal(ctx, flipped_tag, ret, type_all_keys(ctx, i.ref));
            }
            return ret;
        }

        case TYPE_REF: return type_all_keys(ctx, my ref);

        default: return type_never;
    }
}

static Type const* type_resolve_one_layer(TypeCtx* ctx, Type const* me) {
    switch (my tag) {
        case TYPE_IDENTIFIER:
            if (my identifier->type) {
                if (my identifier->type->generic) {
                    switch (my identifier->degenericized_state) {
                        case RESOLVE_STATE_UNINITIALIZED:
                            my identifier->degenericized_state = RESOLVE_STATE_IN_FLIGHT;
                            Type degenericized = generic_degenericize_type(
                                my identifier->type->generic,
                                type_clone(*lazy_type_resolve(&my identifier->type->type)),
                                &my identifier->generic_params
                            );
                            if (my identifier->degenericized_state == RESOLVE_STATE_IN_FLIGHT) {
                                my identifier->degenericized = degenericized;
                                my identifier->degenericized_state = RESOLVE_STATE_DONE;
                            } else {
                                type_drop(&degenericized);
                            }
                            break;

                        case RESOLVE_STATE_IN_FLIGHT:
                            my identifier->degenericized = type_self_referential_error;
                            my identifier->degenericized_state = RESOLVE_STATE_DONE;

                        case RESOLVE_STATE_DONE: break;
                    }
                    return &my identifier->degenericized;
                } else {
                    return lazy_type_resolve(&my identifier->type->type);
                }
            } else {
                return &type_unknown;
            }
            UNREACHABLE;

        case TYPE_INFERRED: return &my inferred->extends;

        case TYPE_INDEXED:
            if (!my indexed->has_cache) {
                if (!index_type_of_internal(
                    ctx,
                    &my indexed->interface,
                    &my indexed->index,
                    span_null,
                    false,
                    nullptr,
                    &my indexed->cached
                )) {
                    my indexed->cached = type_any;
                }
                my indexed->has_cache = true;
            }
            return &my indexed->cached;

        case TYPE_KEYOF:
            if (!my keyof->has_cache) {
                my keyof->cached = type_all_keys(ctx, &my keyof->interface);
                my keyof->has_cache = true;
            }
            return &my keyof->cached;

        case TYPE_TERNARY:
            if (my ternary->has_cache) {
                return &my ternary->cached;
            } else {
                VecTypeRefinement refinements = {0};
                Type const* ret;
                if (type_extends_internal(ctx, &my ternary->maybe_sub, &my ternary->maybe_super, &refinements)) {
                    if (!vec_type_refinement_empty(&refinements)) {
                        my ternary->cached = type_clone(my ternary->if_true);
                        my ternary->has_cache = true;
                        replace_named_types_with(&my ternary->cached, &refinements);
                        ret = &my ternary->cached;
                    } else {
                        ret = &my ternary->if_true;
                    }
                } else {
                    ret = &my ternary->if_false;
                }
                vec_type_refinement_drop(&refinements);
                return ret;
            }

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                Type const* resolved = type_resolve_internal(ctx, i.ref);
                if (resolved->tag == (my tag == TYPE_UNION ? TYPE_ANY : TYPE_NEVER)) {
                    return resolved;
                }
            }
            return nullptr;

        case TYPE_REF: return my ref;

        default: return nullptr;
    }
    UNREACHABLE;
}

static Type const* type_resolve_internal(TypeCtx* ctx, Type const* me) {
    if (ctx->recursion_level > 50 || ctx->currently_resolving == me) {
        return &type_excessively_deep_error;
    }

    bool currently_resolving_null = ctx->currently_resolving == nullptr;
    if (currently_resolving_null) ctx->currently_resolving = me;

    Type const* tmp = type_resolve_one_layer(ctx, me);
    if (tmp) {
        ctx->recursion_level++;
        me = type_resolve_internal(ctx, tmp);
        ctx->recursion_level--;
    }

    if (currently_resolving_null) ctx->currently_resolving = nullptr;
    return me;
}

Type const* type_resolve(Type const* me) {
    TypeCtx ctx = {0};
    Type const* ret = type_resolve_internal(&ctx, me);
    type_ctx_drop(&ctx);
    return ret;
}

static bool type_extends_more_internal(TypeCtx* ctx, Type const* sub, Type const* super, VecTypeRefinement* refinements) {
    if (sub == super) {
        return true;
    }
    switch (sub->tag) {
        case TYPE_ANY:
            switch (super->tag) {
                case TYPE_NEVER:
                case TYPE_UNION:
                case TYPE_INTERSECTION: break;
                default: return true;
            }
            break;

        case TYPE_NEVER:
            return true;

        case TYPE_IDENTIFIER:
        case TYPE_INFERRED:
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TERNARY:
        case TYPE_REF: {
            Type const* sub_resolved = type_resolve_one_layer(ctx, sub);
            if (sub_resolved) {
                return type_extends_internal(ctx, sub_resolved, super, refinements);
            } else {
                return false;
            }
        }

        case TYPE_UNION:
        case TYPE_INTERSECTION: // It's actually correct to treat both the same here (I think)
            c_foreach(i, vec_type, sub->composite) {
                if (!type_extends_internal(ctx, i.ref, super, refinements)) {
                    return false;
                }
            }
            return true;

        default: break;
    }

    switch (super->tag) {
        case TYPE_UNKNOWN:
        case TYPE_ANY:
            return true;

        case TYPE_NEVER:
            return false;

        case TYPE_VOID:
            return sub->tag == TYPE_VOID || (sub->tag == TYPE_VALUE && sub->value.tag == VALUE_UNDEFINED);

        case TYPE_BOOLEAN:
            return sub->tag == TYPE_BOOLEAN || (sub->tag == TYPE_VALUE && sub->value.tag == VALUE_BOOLEAN);

        case TYPE_NUMBER:
            return sub->tag == TYPE_NUMBER || (sub->tag == TYPE_VALUE && sub->value.tag == VALUE_NUMBER);

        case TYPE_STRING:
            return sub->tag == TYPE_STRING || (sub->tag == TYPE_VALUE && sub->value.tag == VALUE_STRING);

        case TYPE_VALUE:
            if (sub->tag != TYPE_VALUE) {
                return false;
            }
            return value_strict_eq(&sub->value, &super->value);

        case TYPE_CALLABLE:
            if (sub->tag == TYPE_INTERFACE) {
                c_foreach(i, vec_interface_member, sub->interface.members) {
                    if (i.ref->key.tag == INTERFACE_KEY_CALLABLE) {
                        if (type_extends_internal(
                            ctx,
                            lazy_type_resolve(&i.ref->type),
                            super,
                            refinements
                        )) {
                            return true;
                        }
                    }
                }
                return false;
            } else if (sub->tag == TYPE_CALLABLE) {
                long arg_count = vec_callable_arg_size(&super->callable->args);
                if (vec_callable_arg_size(&sub->callable->args) != arg_count) {
                    return false;
                }
                for (intptr_t i = 0; i < arg_count; i++) {
                    // Order is flipped for arguments
                    if (!type_extends_internal(
                        ctx,
                        lazy_type_resolve(&vec_callable_arg_at_mut(&sub->callable->args, i)->type),
                        lazy_type_resolve(&vec_callable_arg_at_mut(&super->callable->args, i)->type),
                        refinements
                    )) {
                        return false;
                    }
                }
                // Order is not flipped for return
                return type_extends_internal(ctx, &sub->callable->ret, &super->callable->ret, refinements);
            } else {
                return false;
            }

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, super->interface.members) {
                switch (i.ref->key.tag) {
                    case INTERFACE_KEY_NAME: {
                        // Don't clone the string, and don't drop it the type. Hacky but safe.
                        Type key = type_value(value_string(i.ref->key.name_string));
                        Type prop;
                        if (index_type_of_internal(ctx, sub, &key, span_null, false, nullptr, &prop)) {
                            // TODO: pass in refinements, but first make refinements own it's types
                            // (will fix nondistributiveConditionalTypeInfer)
                            if (!type_extends_internal(ctx, &prop, lazy_type_resolve(&i.ref->type), nullptr)) {
                                type_drop(&prop);
                                return false;
                            }
                            type_drop(&prop);
                        } else {
                            return false;
                        }
                    }   break;
                    case INTERFACE_KEY_INDEX:
                    case INTERFACE_KEY_EXPR:
                    case INTERFACE_KEY_MAPPED:
                    case INTERFACE_KEY_CALLABLE:
                        continue; // TODO
                }
            }
            return true;

        case TYPE_ARRAY:
            if (sub->tag == TYPE_ARRAY) {
                if (sub->array.readonly && !super->array.readonly) {
                    return false;
                }
                return type_extends_internal(ctx, sub->array.element, super->array.element, refinements);
            } else if (sub->tag == TYPE_TUPLE) {
                if (sub->tuple.readonly && !super->array.readonly) {
                    return false;
                }
                c_foreach(i, vec_tuple_element, sub->tuple.elements) {
                    if (i.ref->rest) {
                        // Make sure every item in the rest tuple/array extends the array type
                        Type elem = type_any;
                        index_type_of(&i.ref->type, &type_number, span_null, false, nullptr, &elem);
                        bool extends = type_extends_internal(ctx, &elem, super->array.element, refinements);
                        type_drop(&elem);
                        if (!extends) return false;
                    } else {
                        if (!type_extends_internal(ctx, &i.ref->type, super->array.element, refinements)) {
                            return false;
                        }
                    }
                    if (i.ref->readonly && !super->array.readonly) {
                        return false;
                    }
                }
                return true;
            } else {
                return false;
            }

        case TYPE_TUPLE:
            if (sub->tag != TYPE_TUPLE ||
                vec_tuple_element_size(&super->tuple.elements) != vec_tuple_element_size(&sub->tuple.elements) ||
                (sub->tuple.readonly && !super->tuple.readonly)
            ) {
                // TODO: correctly account for rest elements
                return false;
            }
            for (intptr_t i = 0; i < vec_tuple_element_size(&super->tuple.elements); i++) {
                if (!type_extends_internal(
                    ctx,
                    &vec_tuple_element_at(&sub->tuple.elements, i)->type,
                    &vec_tuple_element_at(&super->tuple.elements, i)->type,
                    refinements
                )) {
                    return false;
                }
            }
            return true;

        case TYPE_INFERRED:
            if (refinements) {
                if (type_extends_internal(ctx, sub, &super->inferred->extends, nullptr)) {
                    vec_type_refinement_push(refinements, (TypeRefinement){
                        .target = super->inferred->type,
                        .refined = sub,
                    });
                    return true;
                } else {
                    return false;
                }
            }
            return true;

        case TYPE_IDENTIFIER:
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TERNARY:
        case TYPE_REF: {
            Type const* super_resolved = type_resolve_one_layer(ctx, super);
            return type_extends_internal(
                ctx,
                sub,
                super_resolved,
                super->tag == TYPE_IDENTIFIER ? nullptr : refinements
            );
        }

        case TYPE_UNION:
            c_foreach(i, vec_type, super->composite) {
                if (type_extends_internal(ctx, sub, i.ref, refinements)) {
                    return true;
                }
            }
            return false;

        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, super->composite) {
                if (!type_extends_internal(ctx, sub, i.ref, refinements)) {
                    return false;
                }
            }
            return true;
    }
    UNREACHABLE;
}

static bool type_extends_internal(TypeCtx* ctx, Type const* sub, Type const* super, VecTypeRefinement* refinements) {
    if (ctx->recursion_level > 40) {
        NONFATAL("Max type recursion depth exceeded (type_extends_internal)");
        return true;
    }

    static const bool default_result = true;
    csview key_sv;
    bool add_to_extends_results = ctx->extends_attempts > 10 &&
        (sub->tag == TYPE_IDENTIFIER || super->tag == TYPE_IDENTIFIER);

    if (add_to_extends_results) {
        cstr key = cstr_null;
        display_type(hash_key_display(&key), sub);
        cstr_append_lit(&key, " # ");
        display_type(hash_key_display(&key), super);
        key_sv = cstr_sv(&key); // Hacky but should work as long as we don't use it after key is dropped
        // Owndership of the key is passed in here (which means key_sv is then invalid if result.inserted is false)
        HmapStrBoolResult result = hmap_str_bool_insert(&ctx->extends_results, key, default_result);
        if (!result.inserted) {
            return result.ref->second;
        }
    } else {
        ctx->extends_attempts++;
    }

    ctx->recursion_level++;
    bool ret = type_extends_more_internal(ctx, sub, super, refinements);
    ctx->recursion_level--;

    if (add_to_extends_results) {
        if (ret != default_result) {
            HmapStrBoolValue* result = hmap_str_bool_get_mut(&ctx->extends_results, key_sv);
            ASSERT(result);
            result->second = ret;
        }
    }

    return ret;
}

bool type_extends(Type const* sub, Type const* super) {
    TypeCtx ctx = {0};
    bool ret = type_extends_internal(&ctx, sub, super, nullptr);
    type_ctx_drop(&ctx);
    return ret;
}

bool type_overlaps(Type const* a, Type const* b) {
    return type_extends(a, b) || type_extends(b, a); // TODO: do this correctly
}

bool type_extends_array(Type const* sub) {
    static Type array_any = {
        .tag = TYPE_ARRAY,
        .array = {
            .element = (Type*)&type_any,
            .readonly = true
        }
    };
    return type_extends(sub, &array_any);
}

void type_abstract_tuple_to_array(Type* me) {
    if (my tag == TYPE_TUPLE) {
        type_abstract_literals(me);
        Type ret = type_never;
        c_foreach(i, vec_tuple_element, my tuple.elements) {
            if (i.ref->rest) {
                Type elem = type_any;
                index_type_of(&i.ref->type, &type_number, span_null, false, nullptr, &elem);
                ret = type_composite(TYPE_UNION, ret, elem);
            } else if (i.ref->type.tag != TYPE_VALUE || i.ref->type.value.tag != VALUE_UNDEFINED) {
                ret = type_composite(TYPE_UNION, ret, i.ref->type);
                i.ref->type = type_any;
            }
        }
        bool readonly = my tuple.readonly;
        type_drop(me);
        if (ret.tag == TYPE_NEVER) {
            ret = type_undefined;
        }
        *me = type_array(c_new(Type, ret), readonly);
    }
}

void type_abstract_literals(Type* me) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_IDENTIFIER:
        case TYPE_INFERRED:
        case TYPE_CALLABLE:
        case TYPE_INTERFACE:
        case TYPE_ARRAY:
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TERNARY:
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            break;

        case TYPE_VALUE:
            switch (my value.tag) {
                case VALUE_UNDEFINED:
                case VALUE_NULL:
                    break;

                case VALUE_BOOLEAN:
                    *me = type_boolean;
                    break;

                case VALUE_NUMBER:
                    *me = type_number;
                    break;

                case VALUE_STRING:
                    type_drop(me);
                    *me = type_string;
                    break;

                case VALUE_OBJECT: /*{
                    Interface interface = {0};
                    c_foreach(i, hmap_str_object_item, my value.object.get->items) {
                        InterfaceMember member = interface_member_make(
                            span_null,
                            false,
                            false,
                            cstr_clone(i.ref->first),
                            span_null,
                            false,
                            type_value(value_clone(i.ref->second.value))
                        );
                        ASSERT(member.type.tag == INTERFACE_TYPE_INSTANTIATED);
                        type_abstract_literals(&member.type.instantiated);
                        vec_interface_member_push(&interface.members, member);
                    }
                    type_drop(me);
                    *me = type_interface(interface);
                }   break; */
                    NONFATAL("type_abstract_literals() not implemented for objects");
                    break;

                case VALUE_ARRAY:
                    NONFATAL("type_abstract_literals() not implemented for arrays");
                    break;

                case VALUE_JS_FUNC:
                case VALUE_NATIVE_FUNC:
                    type_drop(me);
                    *me = type_any;
                    break;
            }
            break;

        case TYPE_TUPLE: {
            c_foreach(i, vec_tuple_element, my tuple.elements) {
                type_abstract_literals(&i.ref->type);
            }
        }   break;

        case TYPE_REF:
            *me = type_clone(*my ref);
            type_abstract_literals(me);
            break;
    }
}

void type_remove_null_and_undefined(Type* me) {
    Type const* resolved = type_resolve(me);
    if (resolved != me) {
        Type tmp = type_clone(*resolved);
        type_drop(me);
        *me = tmp;
    }
    switch (my tag) {
        case TYPE_VALUE:
            switch (my value.tag) {
                case VALUE_UNDEFINED:
                case VALUE_NULL:
                    *me = type_never;
                    break;

                default: break;
            }
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                type_remove_null_and_undefined(i.ref);
            }
            composite_type_simplify(me);
            break;

        default: break;
    }
}

static bool index_type_of_internal(
    TypeCtx* ctx,
    Type const* me,
    Type const* index,
    Span index_span,
    bool error_if_not_found,
    ErrorCtx* errs,
    Type* ret
) {
    if (ctx->recursion_level > 50) {
        NONFATAL("Max type recursion depth exceeded (index_type_of_internal)");
        return false;
    }

    index = type_resolve_internal(ctx, index);
    bool index_invalid = false;

    switch (index->tag) {
        case TYPE_ANY:
        case TYPE_NUMBER:
        case TYPE_STRING:
            break;

        case TYPE_VALUE:
            switch (index->value.tag) {
                case VALUE_NUMBER:
                case VALUE_STRING:
                    break;

                default: index_invalid = true; break;
            }
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION: {
            Type comp = type_composite_base(index->tag);
            bool found_in_all = true;
            bool found_in_any = false;
            c_foreach(i, vec_type, index->composite) {
                Type temp;
                if (index_type_of_internal(ctx, me, i.ref, index_span, error_if_not_found, errs, &temp)) {
                    comp = type_composite_internal(ctx, index->tag, comp, temp);
                    found_in_any = true;
                } else {
                    found_in_all = false;
                }
            }
            if (index->tag == TYPE_UNION ? found_in_all : found_in_any) {
                *ret = comp;
                return true;
            } else {
                type_drop(&comp);
                return false;
            }
        }

        default: index_invalid = true; break;
    }

    if (index_invalid) {
        if (errs) {
            cstr msg = cstr_lit("Type '");
            cstr_append_type_error_msg(&msg, index);
            cstr_append_sv(&msg, c_sv("' cannot be used as an index type."));
            error_ctx_error_s(errs, index_span, 2538, msg);
        }
        return false;
    }

    Type const* me_original = me;
    me = type_resolve_internal(ctx, me);
    if (type_is_self_referential(me)) {
        return false;
    }

    switch (my tag) {
        case TYPE_NEVER:
            *ret = type_never;
            return true;

        case TYPE_VOID:
        case TYPE_UNKNOWN:
        case TYPE_ANY:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_CALLABLE:
            return false;

        case TYPE_VALUE:
            switch (my value.tag) {
                case VALUE_UNDEFINED:
                case VALUE_NULL:
                case VALUE_BOOLEAN:
                case VALUE_NUMBER:
                case VALUE_OBJECT:
                case VALUE_ARRAY:
                case VALUE_JS_FUNC:
                case VALUE_NATIVE_FUNC:
                    return false;

                case VALUE_STRING:
                    break;
            }
            // Falls through if string
            // Fallthrough

        case TYPE_STRING:
            if (type_extends(index, &type_number)) {
                *ret = type_string;
                return true;
            } else {
                return false;
            }

        case TYPE_INTERFACE: {
            Type comp = type_composite_base(TYPE_UNION);
            bool found = false;
            c_foreach(i, vec_interface_member, my interface.members) {
                switch (i.ref->key.tag) {
                    case INTERFACE_KEY_NAME: {
                        // Don't clone the string, and don't drop it the type. Hacky but safe.
                        Type key = type_value(value_string(i.ref->key.name_string));
                        if (type_extends(&key, index)) {
                            comp = type_composite_internal(
                                ctx,
                                TYPE_UNION,
                                comp,
                                type_clone(*lazy_type_resolve(&i.ref->type))
                            );
                            if (i.ref->optional) {
                                comp = type_composite_internal(ctx, TYPE_UNION, comp, type_undefined);
                            }
                            found = true;
                        }
                    }   break;

                    case INTERFACE_KEY_INDEX:
                        if (type_overlaps(&i.ref->key.index, index)) {
                            comp = type_composite_internal(
                                ctx,
                                TYPE_UNION,
                                comp,
                                type_clone(*lazy_type_resolve(&i.ref->type))
                            );
                            if (i.ref->optional) {
                                comp = type_composite_internal(ctx, TYPE_UNION, comp, type_undefined);
                            }
                            found = true;
                        }
                        break;

                    case INTERFACE_KEY_EXPR:
                    case INTERFACE_KEY_MAPPED:
                        // TODO
                        break;

                    case INTERFACE_KEY_CALLABLE:
                        break;
                }
            }
            if (found) {
                *ret = comp;
                return true;
            } else {
                type_drop(&comp);
                if (errs && errs->conf.no_unchecked_indexed_access && error_if_not_found) {
                    cstr msg = cstr_lit("Property '");
                    if (index->tag == TYPE_VALUE && index->value.tag == VALUE_STRING) {
                        cstr_append_s(&msg, index->value.string);
                    } else {
                        cstr_append_type_error_msg(&msg, index);
                    }
                    cstr_append_sv(&msg, c_sv("' does not exist on type '"));
                    cstr_append_type_error_msg(&msg, me_original);
                    cstr_append_sv(&msg, c_sv("'."));
                    error_ctx_error_s(errs, index_span, 2551, msg);
                }
                return false;
            }
        }

        case TYPE_ARRAY:
            if (type_extends(index, &type_number)) {
                *ret = type_clone(*my array.element);
                return true;
            } else {
                return false;
            }

        case TYPE_TUPLE: {
            if (index->tag == TYPE_VALUE &&
                index->value.tag == VALUE_NUMBER &&
                index->value.number >= vec_tuple_element_size(&my tuple.elements) &&
                errs
            ) {
                // TODO: this logic doesn't account for rest elements
                cstr msg = cstr_lit("Tuple type '");
                cstr_append_type_error_msg(&msg, me);
                cstr_append_fmt(
                    &msg,
                    "' of length '%ld' has no element at index '",
                    vec_tuple_element_size(&my tuple.elements)
                );
                cstr_append_value_error_msg(&msg, &index->value);
                cstr_append_sv(&msg, c_sv("'."));
                error_ctx_error_s(errs, index_span, 2493, msg);
            }

            if (!type_extends(index, &type_number)) {
                return false;
            }

            Type comp = type_composite_base(TYPE_UNION);
            c_foreach(i, vec_tuple_element, my tuple.elements) {
                intptr_t j = i.ref - my tuple.elements.data;
                Type key = type_value(value_number(j));
                if (type_extends(&key, index)) {
                    comp = type_composite_internal(ctx, TYPE_UNION, comp, type_clone(i.ref->type));
                }
            }
            *ret = comp;
            return true;
        }

        case TYPE_UNION:
        case TYPE_INTERSECTION: {
            Type comp = type_composite_base(my tag);
            bool found_in_all = true;
            bool found_in_any = false;
            c_foreach(i, vec_type, my composite) {
                Type temp;
                if (index_type_of_internal(ctx, i.ref, index, index_span, error_if_not_found, errs, &temp)) {
                    comp = type_composite_internal(ctx, my tag, comp, temp);
                    found_in_any = true;
                } else {
                    found_in_all = false;
                }
            }
            if (my tag == TYPE_UNION ? found_in_all : found_in_any) {
                *ret = comp;
                return true;
            } else {
                type_drop(&comp);
                return false;
            }
            return ret;
        }

        case TYPE_IDENTIFIER:
        case TYPE_INFERRED:
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TERNARY:
        case TYPE_REF:
            PANIC("Type with tag %d should have been resolved away", my tag);
    }
    UNREACHABLE;
}

bool index_type_of(
    Type const* me,
    Type const* index,
    Span index_span,
    bool error_if_not_found,
    ErrorCtx* errs,
    Type* ret
) {
    TypeCtx ctx = {0};
    bool found = index_type_of_internal(&ctx, me, index, index_span, error_if_not_found, errs, ret);
    type_ctx_drop(&ctx);
    return found;
}

static bool check_assignable_or_error_internal(
    const char* source_label,
    Type const* source,
    const char* target_label,
    Type const* target,
    unsigned ts_code,
    Span span,
    ErrorCtx* errs
) {
    Type local_type = {0};

    if (errs && !errs->conf.strict_null_checks) {
        local_type = type_clone(*source);
        type_remove_null_and_undefined(&local_type);
        source = &local_type;
    }

    if (type_extends(source, target)) {
        type_drop(&local_type);
        return true;
    }

    cstr source_str = cstr_null, target_str = cstr_null;
    Type source_abstracted = type_any;
    if (target->tag != TYPE_VALUE) {
        source_abstracted = type_clone(*source);
        type_abstract_literals(&source_abstracted);
        source = &source_abstracted;
    }

    cstr_append_type_error_msg(&source_str, source);
    cstr_append_type_error_msg(&target_str, target);
    type_drop(&source_abstracted);
    type_drop(&local_type);
    error_ctx_error(
        errs,
        span,
        ts_code,
        "%s '%s' is not assignable to %s '%s'.",
        source_label,
        cstr_str(&source_str),
        target_label,
        cstr_str(&target_str)
    );
    c_drop(cstr, &source_str, &target_str);
    return false;
}

void check_assignable_or_error(Type const* source, Type const* target, Span span, ErrorCtx* errs) {
    check_assignable_or_error_internal("Type", source, "type", target, 2322, span, errs);
}

static intptr_t min_args_of_callable(CallableType* me) {
    intptr_t args_size = vec_callable_arg_size(&my args);
    intptr_t ret = args_size;
    if (args_size && vec_callable_arg_at(&my args, args_size - 1)->source->rest) {
        Type const* arg_type = lazy_type_resolve(&vec_callable_arg_at_mut(&my args, args_size - 1)->type);
        if (arg_type->tag == TYPE_TUPLE && vec_tuple_element_size(&arg_type->tuple.elements) > 0) {
            // TODO: account for rest arguments within tuple
            return args_size - 1 + vec_tuple_element_size(&arg_type->tuple.elements);
        } else {
            ret--;
        }
    }
    while (ret && vec_callable_arg_at(&my args, ret - 1)->optional) {
        ret--;
    }
    return ret;
}

static intptr_t max_args_of_callable(CallableType* me) {
    intptr_t args_size = vec_callable_arg_size(&my args);
    CallableArg* last_arg = args_size ? vec_callable_arg_at_mut(&my args, args_size - 1) : nullptr;
    if (last_arg && last_arg->source->rest) {
        Type const* last_arg_type = lazy_type_resolve(&last_arg->type);
        if (last_arg_type->tag == TYPE_TUPLE) {
            // TODO: account for rest arguments within tuple
            return args_size - 1 + vec_tuple_element_size(&last_arg_type->tuple.elements);
        } else {
            return INTPTR_MAX;
        }
    } else {
        return args_size;
    }
}

Type type_typecheck_call(Type const* me, CallExpr const* call, Span span, ErrorCtx* errs) {
    me = type_resolve(me);

    switch (my tag) {
        case TYPE_ANY:
            return type_any;

        case TYPE_CALLABLE: {
            CallableType inferred = {0};
            CallableType* callable = my callable;
            bool override_ret_with_any = false;

            if (call->is_extends) {
                if (!callable->is_constructor) {
                    break;
                }
            } else if (call->uses_new) {
                if (!callable->is_constructor) {
                    if (errs && errs->conf.no_implicit_any) {
                        error_ctx_error(
                            errs,
                            span,
                            7009,
                            "'new' expression, whose target lacks a construct signature, implicitly has an 'any' type."
                        );
                    }
                    override_ret_with_any = true;
                } else if (callable->is_abstract_constructor) {
                    error_ctx_error(errs, span, 2511, "Cannot create an instance of an abstract class.");
                    override_ret_with_any = true;
                }
            } else {
                if (callable->is_constructor) {
                    error_ctx_error(
                        errs,
                        span,
                        2348,
                        "Value of type 'typeof Foo' is not callable. Did you mean to include 'new'?"
                    );
                }
            }

            if (!generic_empty(&callable->generic)) {
                inferred = generic_infer_callable_types(
                    &callable->generic,
                    callable,
                    &call->args,
                    errs
                );
                callable = &inferred;
            }

            intptr_t call_arg_count = vec_expr_size(&call->args);
            intptr_t callable_arg_count = vec_callable_arg_size(&callable->args);
            intptr_t min_args = min_args_of_callable(callable);
            intptr_t max_args = max_args_of_callable(callable);
            bool check_types_against_callable = true;

            if (call_arg_count < min_args || call_arg_count > max_args) {
                check_types_against_callable = false;

                Span span = call->callable.inner_span;
                if (call_arg_count > max_args) {
                    span = span_between(
                        &vec_expr_at(&call->args, max_args)->outer_span,
                        &vec_expr_at(&call->args, call_arg_count - 1)->outer_span
                    );
                }

                if (max_args == INTPTR_MAX) {
                    error_ctx_error(
                        errs,
                        span,
                        2555,
                        "Expected at least %ld arguments, but got %ld.",
                        min_args,
                        call_arg_count
                    );
                } else if (min_args == max_args) {
                    error_ctx_error(
                        errs,
                        span,
                        2554,
                        "Expected %ld arguments, but got %ld.",
                        min_args,
                        call_arg_count
                    );
                } else {
                    error_ctx_error(
                        errs,
                        span,
                        2554,
                        "Expected %ld-%ld arguments, but got %ld.",
                        min_args,
                        max_args,
                        call_arg_count
                    );
                }
            }

            bool last_arg_rest =
                check_types_against_callable &&
                callable_arg_count &&
                vec_callable_arg_at(&callable->args, callable_arg_count - 1)->source->rest;
            Span rest_span = span_null;
            VecTupleElement rest_tuple = {0};

            c_foreach(i, vec_expr, call->args) {
                intptr_t index = i.ref - call->args.data;
                // Don't report errors, errors inside argument expressions are reported elsewhere
                Type arg_type = expr_get_type_and_typecheck(i.ref, nullptr);
                if (check_types_against_callable && index < callable_arg_count - (last_arg_rest ? 1 : 0)) {
                    CallableArg* arg = vec_callable_arg_at_mut(&callable->args, index);
                    if (!check_assignable_or_error_internal(
                        "Argument of type",
                        &arg_type,
                        "parameter of type",
                        lazy_type_resolve(&arg->type),
                        2345,
                        i.ref->outer_span,
                        errs
                    )) {
                        check_types_against_callable = false;
                    }
                    type_drop(&arg_type);
                } else if (last_arg_rest) {
                    rest_span = span_between(&rest_span, &i.ref->outer_span);
                    vec_tuple_element_push(&rest_tuple, (TupleElement){.type = arg_type});
                } else {
                    type_drop(&arg_type);
                }
            }

            if (last_arg_rest) {
                Type rest_type = type_tuple(rest_tuple, false);
                if (check_types_against_callable) {
                    CallableArg* arg = vec_callable_arg_at_mut(&callable->args, callable_arg_count - 1);
                    check_assignable_or_error_internal(
                        "Argument of type",
                        &rest_type,
                        "parameter of type",
                        lazy_type_resolve(&arg->type),
                        2345,
                        span_valid(&rest_span) ? rest_span : span,
                        errs
                    );
                }
                type_drop(&rest_type);
            }

            Type ret = override_ret_with_any ? type_any : type_clone(callable->ret);
            callable_type_drop(&inferred);
            return ret;
        }

        case TYPE_IDENTIFIER:
        case TYPE_INDEXED:
        case TYPE_TERNARY:
            UNREACHABLE; // Should be handled by resolve

        case TYPE_UNION:
        case TYPE_INTERSECTION: {
            Type ret = type_composite_base(my tag);
            c_foreach(i, vec_type, my composite) {
                ret = type_composite(
                    my tag,
                    ret,
                    type_typecheck_call(i.ref, call, span, errs)
                );
            }
            return ret;
        }

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                // TODO: add to overload list instead of selecting first option
                if (i.ref->key.tag == INTERFACE_KEY_CALLABLE) {
                    return type_typecheck_call(lazy_type_resolve(&i.ref->type), call, span, errs);
                }
            }
            break;

        default: break;
    }

    if (errs) {
        if (call->is_extends) {
            cstr msg = cstr_lit("Type '");
            cstr_append_type_error_msg(&msg, me);
            cstr_append_sv(&msg, c_sv("' is not a constructor function type."));
            error_ctx_error_s(errs, call->callable.outer_span, 2507, msg);
        } else {
            error_ctx_error(errs, call->callable.outer_span, 2349, "This expression is not callable.");
        }
    }
    return type_any;
}
