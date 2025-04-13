#include "type.h"
#include "lazy_type.h"
#include "interface.h"
#include "interface_member.h"
#include "several_types.h"
#include "callable_type.h"
#include "function_impl.h"
#include "generic.h"
#include "value.h"
#include "scope_stack.h"
#include "scope.h"
#include "display.h"
#include "error.h"
#include "math_helpers.h"
#include "type_pair_cache.h"

const Type type_any = { .tag = TYPE_ANY };
const Type type_self_referential_error = { .tag = TYPE_ANY, .any.self_referential_error = true };
const Type type_unknown = { .tag = TYPE_UNKNOWN };
const Type type_never = { .tag = TYPE_NEVER };
const Type type_void = { .tag = TYPE_VOID };
const Type type_object = { .tag = TYPE_INTERFACE, .interface.members = {0} };
const Type type_boolean = { .tag = TYPE_BOOLEAN };
const Type type_number = { .tag = TYPE_NUMBER };
const Type type_string = { .tag = TYPE_STRING };
const Type type_symbol = { .tag = TYPE_SYMBOL };
const Type type_bigint = { .tag = TYPE_BIGINT };
const Type type_null = { .tag = TYPE_VALUE, .value.tag = VALUE_NULL };
const Type type_undefined = { .tag = TYPE_VALUE, .value.tag = VALUE_UNDEFINED };

typedef struct TypeCtx {
    unsigned recursion_level;
    HsetDeclarationCPtr currently_resolving;
    TypePairCache extends_results;
} TypeCtx;

static bool type_extends_internal(
    TypeCtx* ctx,
    Type const* sub,
    Type const* super,
    VecTypeRefinement* refinements
);
static Type const* type_resolve_internal(TypeCtx* ctx, Type const* me, bool through_identifier);
static bool type_overlaps_internal(TypeCtx* ctx, Type const* a, Type const* b);
static bool index_type_evaluate_type_internal(TypeCtx* ctx, Type const* me, Type const* index, Type* ret);

static void type_ctx_drop(TypeCtx* me) {
    hset_declaration_cptr_drop(&my currently_resolving);
    type_pair_cache_drop(&my extends_results);
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
    } else if (a.tag != TYPE_INTERFACE && b.tag != TYPE_INTERFACE && a.tag != TYPE_ARRAY && b.tag != TYPE_ARRAY) {
        if (type_extends_internal(ctx, &a, &b, nullptr)) {
            if (tag == TYPE_UNION) {
                type_drop(&a);
                return b;
            } else {
                type_drop(&b);
                return a;
            }
        } else if (type_extends_internal(ctx, &b, &a, nullptr)) {
            if (tag == TYPE_UNION) {
                type_drop(&b);
                return a;
            } else {
                type_drop(&a);
                return b;
            }
        }
    }
    return type_composite_without_simplify(tag, a, b);
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
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_LAZY:
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

        case TYPE_TYPEOF:
            typeof_type_drop(my typeof_);
            free(my typeof_);
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
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_LAZY:
        case TYPE_REF:
            break;

        case TYPE_VALUE:
            me.value = value_clone(me.value);
            break;

        case TYPE_IDENTIFIER:
            me.identifier = c_new(IdentifierType, identifier_type_clone(*me.identifier));
            break;

        case TYPE_INFERRED:
            me.inferred = c_new(InferredType, inferred_type_clone(*me.inferred));
            break;

        case TYPE_CALLABLE:
            me.callable = c_new(CallableType, callable_type_clone(*me.callable));
            break;

        case TYPE_INTERFACE:
            me.interface = interface_clone(me.interface);
            break;

        case TYPE_ARRAY:
            me.array.element = c_new(Type, type_clone(*me.array.element));
            break;

        case TYPE_TUPLE:
            me.tuple.elements = vec_tuple_element_clone(me.tuple.elements);
            break;

        case TYPE_INDEXED:
            me.indexed = c_new(IndexedType, indexed_type_clone(*me.indexed));
            break;

        case TYPE_KEYOF:
            me.keyof = c_new(KeyofType, keyof_type_clone(*me.keyof));
            break;

        case TYPE_TYPEOF:
            me.typeof_ = c_new(TypeofType, typeof_type_clone(*me.typeof_));
            break;

        case TYPE_TERNARY:
            me.ternary = c_new(TernaryType, ternary_type_clone(*me.ternary));
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            me.composite = vec_type_clone(me.composite);
            break;
    }
    return me;
}

Type type_deref_clone(Type me) {
    switch (me.tag) {
        case TYPE_LAZY: return type_deref_clone(*lazy_type_get(me.lazy));
        case TYPE_REF:  return type_deref_clone(*me.ref);
        default:        return type_clone(me);
    }
    UNREACHABLE;
}

void cstr_append_type_error_msg(cstr* target, Type const* me) {
    display_type(error_display(target), me);
}

static void type_add_inferred_names(Type const* me, Scope* scope) {
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
            c_foreach(i, vec_spanned_type, my identifier->generic_params) {
                type_add_inferred_names(&i.ref->type, scope);
            }
            break;

        case TYPE_INFERRED:
            scope_init_declaration(scope, &my inferred->owned_declaration, (DeclProps){
                .name = my inferred->name,
                .full = my inferred->name,
                .tag = DECLARATION_TYPE,
                .declared_type = type_refcopy(&my inferred->extends),
            });
            my inferred->declaration_ref = my inferred->owned_declaration.get;
            break;

        case TYPE_CALLABLE:
            c_foreach(i, vec_callable_arg, my callable->args) {
                type_add_inferred_names(&i.ref->type, scope);
            }
            type_add_inferred_names(&my callable->ret, scope);
            break;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                type_add_inferred_names(&i.ref->type, scope);
            }
            break;

        case TYPE_ARRAY:
            type_add_inferred_names(my array.element, scope);
            break;

        case TYPE_TUPLE:
            c_foreach(i, vec_tuple_element, my tuple.elements) {
                type_add_inferred_names(&i.ref->type, scope);
            }
            break;

        case TYPE_INDEXED:
            type_add_inferred_names(&my indexed->interface, scope);
            type_add_inferred_names(&my indexed->index, scope);
            break;

        case TYPE_KEYOF:
            type_add_inferred_names(&my keyof->interface, scope);
            break;

        case TYPE_TYPEOF: break;

        case TYPE_TERNARY:
            type_add_inferred_names(&my ternary->maybe_sub, scope);
            type_add_inferred_names(&my ternary->maybe_super, scope);
            type_add_inferred_names(&my ternary->if_true, scope);
            type_add_inferred_names(&my ternary->if_false, scope);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                type_add_inferred_names(i.ref, scope);
            }
            break;

        case TYPE_LAZY: break;

        case TYPE_REF:
            type_add_inferred_names(my ref, scope);
            break;
    }
}

void type_lookup_names(Type* me, ScopeStack* names, ErrorCtx* errs) {
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
        case TYPE_INFERRED:
            break;

        case TYPE_IDENTIFIER: identifier_type_lookup_names(my identifier, names, errs); break;
        case TYPE_CALLABLE: callable_type_resolve_names(my callable, names, errs); break;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                interface_member_lookup_names(i.ref, names, errs);
            }
            break;

        case TYPE_ARRAY:
            type_lookup_names(my array.element, names, errs);
            break;

        case TYPE_TUPLE:
            c_foreach(i, vec_tuple_element, my tuple.elements) {
                type_lookup_names(&i.ref->type, names, errs);
            }
            break;

        case TYPE_INDEXED: indexed_type_lookup_names(my indexed, names, errs); break;
        case TYPE_KEYOF: keyof_type_lookup_names(my keyof, names, errs); break;
        case TYPE_TYPEOF: typeof_type_lookup_names(my typeof_, names, errs); break;

        case TYPE_TERNARY: {
            type_lookup_names(&my ternary->maybe_sub, names, errs);
            type_lookup_names(&my ternary->maybe_super, names, errs);

            Scope inferred_types_scope = scope_init(errs);
            type_add_inferred_names(&my ternary->maybe_super, &inferred_types_scope);
            scope_push_to_stack(&inferred_types_scope, names);
            type_lookup_names(&my ternary->if_true, names, errs);
            scope_drop(&inferred_types_scope);

            type_lookup_names(&my ternary->if_false, names, errs);
        }   break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                type_lookup_names(i.ref, names, errs);
            }
            break;

        case TYPE_LAZY: // Resolving names is the responsibility of the LazyType owner
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
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
        case TYPE_INFERRED:
            break;

        case TYPE_IDENTIFIER: identifier_type_typecheck(my identifier, errs); break;
        case TYPE_CALLABLE: callable_type_typecheck(my callable, errs); break;
        case TYPE_INTERFACE: interface_typecheck(&my interface, errs); break;
        case TYPE_ARRAY: type_typecheck(my array.element, errs); break;

        case TYPE_TUPLE:
            c_foreach(i, vec_tuple_element, my tuple.elements) {
                type_typecheck(&i.ref->type, errs);
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
            c_foreach(i, vec_type, my composite) {
                type_typecheck(i.ref, errs);
            }
            break;

        case TYPE_LAZY: // Typechecking is the responsibility of the LazyType owner
        case TYPE_REF:
            break;
    }
}

static Type type_all_keys(TypeCtx* ctx, Type const* me) {
    me = type_resolve_internal(ctx, me, true);

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

static inline Type const* type_resolve_indexed_type(TypeCtx* ctx, IndexedType* me) {
    if (!my has_cache) {
        if (my array_only && !type_extends_array(&my interface)) {
            my cached = type_any;
        } else if (!index_type_evaluate_type_internal(ctx, &my interface, &my index, &my cached)) {
            my cached = type_any;
        }
        my has_cache = true;
    }
    return &my cached;
}

static inline Type const* type_resolve_ternary_type(TypeCtx* ctx, TernaryType* me) {
    if (my has_cache) {
        return &my cached;
    } else if (my maybe_sub.tag == TYPE_ANY) {
        my cached = type_composite_internal(ctx, TYPE_UNION, type_clone(my if_true), type_clone(my if_false));
        my has_cache = true;
        return &my cached;
    } else {
        VecTypeRefinement refinements = {0};
        Type const* ret;
        if (type_extends_internal(ctx, &my maybe_sub, &my maybe_super, &refinements)) {
            if (!vec_type_refinement_empty(&refinements)) {
                my cached = type_clone(my if_true);
                my has_cache = true;
                replace_named_types_with(&my cached, &refinements);
                ret = &my cached;
            } else {
                ret = &my if_true;
            }
        } else {
            ret = &my if_false;
        }
        vec_type_refinement_drop(&refinements);
        return ret;
    }
}

static Type const* type_resolve_one_layer_internal(TypeCtx* ctx, Type const* me) {
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
        case TYPE_CALLABLE:
        case TYPE_INTERFACE:
        case TYPE_ARRAY:
        case TYPE_TUPLE:
            return nullptr;

        case TYPE_IDENTIFIER: return lazy_type_get(&my identifier->type);
        case TYPE_INFERRED: return &my inferred->extends;
        case TYPE_INDEXED: return type_resolve_indexed_type(ctx, my indexed);

        case TYPE_KEYOF:
            if (!my keyof->has_cache) {
                my keyof->cached = type_all_keys(ctx, &my keyof->interface);
                my keyof->has_cache = true;
            }
            return &my keyof->cached;

        case TYPE_TYPEOF: return &type_any;
        case TYPE_TERNARY: return type_resolve_ternary_type(ctx, my ternary);

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                Type const* resolved = type_resolve_internal(ctx, i.ref, true);
                if (resolved->tag == (my tag == TYPE_UNION ? TYPE_ANY : TYPE_NEVER)) {
                    return resolved;
                }
            }
            return nullptr;

        case TYPE_LAZY: return lazy_type_get(my lazy);

        case TYPE_REF: return my ref;
    }
    UNREACHABLE;
}

Type const* type_resolve_one_layer(Type const* me) {
    TypeCtx ctx = {0};
    Type const* ret = type_resolve_one_layer_internal(&ctx, me);
    type_ctx_drop(&ctx);
    return ret;
}

static Type const* type_resolve_internal(TypeCtx* ctx, Type const* me, bool through_identifier) {
    Declaration const* to_remove = nullptr;
    if (my tag == TYPE_IDENTIFIER) {
        if (through_identifier) {
            if (my identifier->decl_ref) {
                if (hset_declaration_cptr_insert(&ctx->currently_resolving, my identifier->decl_ref).inserted) {
                    to_remove = my identifier->decl_ref;
                } else {
                    return &type_any;
                }
            }
        } else {
            return me;
        }
    }

    Type const* tmp = type_resolve_one_layer_internal(ctx, me);
    if (tmp) {
        ctx->recursion_level++;
        me = type_resolve_internal(ctx, tmp, through_identifier);
        ctx->recursion_level--;
    }

    if (to_remove) {
        hset_declaration_cptr_erase(&ctx->currently_resolving, to_remove);
    }
    return me;
}

Type const* type_resolve(Type const* me) {
    TypeCtx ctx = {0};
    Type const* ret = type_resolve_internal(&ctx, me, true);
    type_ctx_drop(&ctx);
    return ret;
}

Type const* type_resolve_refs(Type const* type) {
    while (true) {
        switch (type->tag) {
            case TYPE_LAZY:
                type = lazy_type_get(type->lazy);
                break;

            case TYPE_REF:
                type = type->ref;
                break;

            default: return type;
        }
    }
}

Type const* type_resolve_until_identifier(Type const* me) {
    TypeCtx ctx = {0};
    Type const* ret = type_resolve_internal(&ctx, me, false);
    type_ctx_drop(&ctx);
    return ret;
}

static inline bool type_extends_more_internal(TypeCtx* ctx, Type const* sub, Type const* super, VecTypeRefinement* refinements) {
    if (sub == super) {
        return true;
    }

    switch (super->tag) {
        case TYPE_UNKNOWN:
        case TYPE_ANY:
            return true;

        case TYPE_IDENTIFIER:
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TYPEOF:
        case TYPE_TERNARY:
        case TYPE_LAZY:
        case TYPE_REF: {
            Type const* super_resolved = type_resolve_one_layer_internal(ctx, super);
            return type_extends_internal(
                ctx,
                sub,
                super_resolved,
                super->tag == TYPE_IDENTIFIER ? nullptr : refinements
            );
        }

        default: break;
    }

    switch (sub->tag) {
        case TYPE_ANY:
            switch (super->tag) {
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
        case TYPE_LAZY:
        case TYPE_REF: {
            Type const* sub_resolved = type_resolve_one_layer_internal(ctx, sub);
            if (sub_resolved) {
                return type_extends_internal(ctx, sub_resolved, super, refinements);
            } else {
                return false;
            }
        }

        case TYPE_UNION:
            c_foreach(i, vec_type, sub->composite) {
                if (!type_extends_internal(ctx, i.ref, super, refinements)) return false;
            }
            return true;

        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, sub->composite) {
                if (type_extends_internal(ctx, i.ref, super, refinements)) return true;
            }
            return false;

        default: break;
    }

    switch (super->tag) {
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

        case TYPE_SYMBOL:
            return sub->tag == TYPE_SYMBOL || (sub->tag == TYPE_VALUE && sub->value.tag == VALUE_SYMBOL);

        case TYPE_BIGINT:
            return sub->tag == TYPE_BIGINT || (sub->tag == TYPE_VALUE && sub->value.tag == VALUE_BIGINT);

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
                            &i.ref->type,
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
                        &vec_callable_arg_at_mut(&sub->callable->args, i)->type,
                        &vec_callable_arg_at_mut(&super->callable->args, i)->type,
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
                        if (index_type_evaluate_type_internal(ctx, sub, &key, &prop)) {
                            // TODO: pass in refinements, but first make refinements own it's types
                            // (will fix nondistributiveConditionalTypeInfer)
                            Type member_type = type_refcopy(&i.ref->type);
                            if (i.ref->optional) {
                                member_type = type_composite(TYPE_UNION, member_type, type_undefined);
                            }
                            bool extends = type_extends_internal(ctx, &prop, &member_type, nullptr);
                            type_drop(&member_type);
                            type_drop(&prop);
                            if (!extends) return false;
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
                        Type elem = index_type_evaluate_type(&i.ref->type, &type_number);
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
                        .target = super->inferred->declaration_ref,
                        .refined = sub,
                    });
                    return true;
                } else {
                    return false;
                }
            }
            return true;

        case TYPE_UNION:
            c_foreach(i, vec_type, super->composite) {
                if (type_extends_internal(ctx, sub, i.ref, refinements)) return true;
            }
            return false;

        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, super->composite) {
                if (!type_extends_internal(ctx, sub, i.ref, refinements)) return false;
            }
            return true;

        case TYPE_UNKNOWN:
        case TYPE_ANY:
        case TYPE_IDENTIFIER:
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TYPEOF:
        case TYPE_TERNARY:
        case TYPE_LAZY:
        case TYPE_REF:
            // Should have been handled above
            UNREACHABLE;
    }
    UNREACHABLE;
}

static inline bool cache_type_in_extends_check(Type const* me) {
    switch (my tag) {
        case TYPE_IDENTIFIER:
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            return true;

        default:
            return false;
    }
}

static bool type_extends_internal(TypeCtx* ctx, Type const* sub, Type const* super, VecTypeRefinement* refinements) {
    if (ctx->recursion_level > 400) {
        NONFATAL("Max type recursion depth exceeded (type_extends_internal)");
        return true;
    }

    bool cache_result = cache_type_in_extends_check(sub) || cache_type_in_extends_check(super);
    TypePairCacheEntry cache_entry;

    if (cache_result) {
        cache_entry = type_pair_cache_entry_make(&ctx->extends_results, sub, super, true);
        if (cache_entry.serialized_types_eq) {
            type_pair_cache_entry_drop(&cache_entry);
            return true;
        }
        if (!type_pair_cache_entry_insert(&cache_entry)) {
            type_pair_cache_entry_drop(&cache_entry);
            return cache_entry.result;
        }
    }

    ctx->recursion_level++;
    bool ret = type_extends_more_internal(ctx, sub, super, refinements);
    ctx->recursion_level--;

    if (cache_result) {
        type_pair_cache_entry_update(&cache_entry, ret);
        type_pair_cache_entry_drop(&cache_entry);
    }

    return ret;
}

bool type_extends(Type const* sub, Type const* super) {
    TypeCtx ctx = {0};
    bool ret = type_extends_internal(&ctx, sub, super, nullptr);
    type_ctx_drop(&ctx);
    return ret;
}

static bool composite_type_overlaps(TypeCtx* ctx, Type const* composite, Type const* other) {
    ASSERT(composite->tag == TYPE_UNION || composite->tag == TYPE_INTERSECTION);
    bool overlaps_in_all = true;
    bool overlaps_in_any = false;
    c_foreach(i, vec_type, composite->composite) {
        if (type_overlaps_internal(ctx, other, i.ref)) {
            overlaps_in_any = true;
        } else {
            overlaps_in_all = false;
        }
    }
    return composite->tag == TYPE_UNION ? overlaps_in_any : overlaps_in_all;
}

static bool array_type_overlaps(TypeCtx* ctx, Type const* a, Type const* b) {
    Type a_item = index_type_evaluate_type(a, &type_number);
    Type b_item = index_type_evaluate_type(b, &type_number);
    bool overlaps = type_overlaps_internal(ctx, &a_item, &b_item);
    type_drop(&a_item);
    type_drop(&b_item);
    return overlaps;
}

static inline bool type_overlaps_more_internal(TypeCtx* ctx, Type const* a, Type const* b) {
    a = type_resolve_internal(ctx, a, true);
    b = type_resolve_internal(ctx, b, true);

    if (a == b) {
        return a->tag != TYPE_NEVER;
    }

    if (b->tag == TYPE_UNION || b->tag == TYPE_INTERSECTION) {
        return composite_type_overlaps(ctx, b, a);
    }

    switch (a->tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
            return type_extends(b, a);

        case TYPE_VALUE:
            return type_extends(a, b);

        case TYPE_CALLABLE:
            switch (b->tag) {
                case TYPE_CALLABLE: {
                    intptr_t a_min_args = callable_type_min_arg_count(a->callable);
                    intptr_t a_max_args = callable_type_max_arg_count(a->callable);
                    intptr_t b_min_args = callable_type_min_arg_count(b->callable);
                    intptr_t b_max_args = callable_type_max_arg_count(b->callable);
                    if (a_min_args > b_max_args || a_max_args < b_min_args) {
                        return false;
                    }
                    intptr_t shared_min_args = iptrmax(a_min_args, b_min_args);
                    for (intptr_t i = 0; i < shared_min_args; i++) {
                        Type arg_a = callable_type_get_arg_type(a->callable, i);
                        Type arg_b = callable_type_get_arg_type(a->callable, i);
                        bool overlaps = type_overlaps_internal(ctx, &arg_a, &arg_b);
                        type_drop(&arg_a);
                        type_drop(&arg_b);
                        if (!overlaps) {
                            return false;
                        }
                    }
                    return type_overlaps_internal(ctx, &a->callable->ret, &b->callable->ret);
                }

                case TYPE_INTERFACE:
                    c_foreach(i, vec_interface_member, b->interface.members) {
                        if (i.ref->key.tag == INTERFACE_KEY_CALLABLE) {
                            if (type_overlaps_internal(ctx, a, &i.ref->type)) {
                                return true;
                            }
                        }
                    }
                    return false;

                default: return false;
            }

        case TYPE_INTERFACE:
            // TODO?
            return type_extends(a, b) || type_extends(b, a);

        case TYPE_ARRAY:
        case TYPE_TUPLE:
            switch (b->tag) {
                case TYPE_ARRAY:
                case TYPE_TUPLE:
                    if (a->tag == TYPE_TUPLE && b->tag == TYPE_TUPLE) {
                        intptr_t a_min_elems = type_min_numeric_index(a);
                        intptr_t a_max_elems = type_max_numeric_index(a);
                        intptr_t b_min_elems = type_min_numeric_index(b);
                        intptr_t b_max_elems = type_max_numeric_index(b);
                        if (a_min_elems > b_max_elems || a_max_elems < b_min_elems) {
                            return false;
                        }
                        intptr_t shared_min_elems = iptrmax(a_min_elems, b_min_elems);
                        for (intptr_t i = 0; i < shared_min_elems; i++) {
                            Type index = type_value(value_number(i));
                            // No need to drop index
                            Type elem_a = index_type_evaluate_type(a, &index);
                            Type elem_b = index_type_evaluate_type(b, &index);
                            bool overlaps = type_overlaps_internal(ctx, &elem_a, &elem_b);
                            type_drop(&elem_a);
                            type_drop(&elem_b);
                            if (!overlaps) {
                                return false;
                            }
                        }
                        return true;
                    } else {
                        return array_type_overlaps(ctx, a, b);
                    }

                case TYPE_INTERFACE:
                    return type_extends(a, b) && type_extends(b, a);

                default: return false;
            }

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            return composite_type_overlaps(ctx, a, b);

        case TYPE_IDENTIFIER:
        case TYPE_INFERRED:
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TYPEOF:
        case TYPE_TERNARY:
        case TYPE_LAZY:
        case TYPE_REF:
            UNREACHABLE; // Should have been resolved
    }
    UNREACHABLE;
}

static bool type_overlaps_internal(TypeCtx* ctx, Type const* a, Type const* b) {
    if (ctx->recursion_level > 400) {
        NONFATAL("Max type recursion depth exceeded (type_extends_internal)");
        return true;
    }

    bool cache_result = cache_type_in_extends_check(a) || cache_type_in_extends_check(b);
    TypePairCacheEntry cache_entry;

    if (cache_result) {
        cache_entry = type_pair_cache_entry_make(&ctx->extends_results, a, b, false);
        if (cache_entry.serialized_types_eq) {
            type_pair_cache_entry_drop(&cache_entry);
            return true;
        }
        if (!type_pair_cache_entry_insert(&cache_entry)) {
            type_pair_cache_entry_drop(&cache_entry);
            return cache_entry.result;
        }
    }

    ctx->recursion_level++;
    bool ret = type_overlaps_more_internal(ctx, a, b);
    ctx->recursion_level--;

    if (cache_result) {
        type_pair_cache_entry_update(&cache_entry, ret);
        type_pair_cache_entry_drop(&cache_entry);
    }

    return ret;
}

bool type_overlaps(Type const* a, Type const* b) {
    TypeCtx ctx = {0};
    bool ret = type_overlaps_internal(&ctx, a, b);
    type_ctx_drop(&ctx);
    return ret;
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

static inline intptr_t type_get_numeric_index_bound(Type const* me, bool get_max) {
    me = type_resolve(me);
    switch (my tag) {
        // case TYPE_INTERFACE: // TODO?

        case TYPE_ARRAY:
            return get_max ? INTPTR_MAX : 0;

        case TYPE_TUPLE: {
            intptr_t ret = vec_tuple_element_size(&my tuple.elements);
            if (!vec_tuple_element_empty(&my tuple.elements) && vec_tuple_element_back(&my tuple.elements)->rest) {
                ret--;
                ret += type_get_numeric_index_bound(&vec_tuple_element_back(&my tuple.elements)->type, get_max);
            }
            return ret;
        }

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            intptr_t result = get_max ? -1 : 0;
            c_foreach(i, vec_type, my composite) {
                intptr_t bound = type_get_numeric_index_bound(i.ref, get_max);
                if (get_max) {
                    result = iptrmin(result, bound);
                } else {
                    result = iptrmax(result, bound);
                }
            }
            return result;

        default: return get_max ? -1 : 0;
    }
}

intptr_t type_min_numeric_index(Type const* me) {
    return type_get_numeric_index_bound(me, false);
}

intptr_t type_max_numeric_index(Type const* me) {
    return type_get_numeric_index_bound(me, true);
}

signed char type_is_trivially_truthy(Type const* me) {
    me = type_resolve(me);
    switch (my tag) {
        case TYPE_VALUE: return value_to_boolean(&my value);
        default: break;
    }
    return -1;
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
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_IDENTIFIER:
        case TYPE_INFERRED:
        case TYPE_CALLABLE:
        case TYPE_INTERFACE:
        case TYPE_ARRAY:
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TYPEOF:
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

                case VALUE_SYMBOL:
                    type_drop(me);
                    *me = type_symbol;
                    break;

                case VALUE_BIGINT:
                    type_drop(me);
                    *me = type_bigint;
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

        case TYPE_LAZY:
            *me = type_clone(*lazy_type_get(my lazy));
            type_abstract_literals(me);
            break;

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

// Returns true if this is a valid index type. Does not handle types that need to be resolved or composites.
static inline bool type_is_simple_index_type(Type const* me) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_NUMBER:
        case TYPE_STRING:
            return true;

        case TYPE_VALUE:
            switch (my value.tag) {
                case VALUE_NUMBER:
                case VALUE_STRING:
                    return true;

                default: return false;
            }

        default: return false;
    }
    UNREACHABLE;
}

// Returns true if index found (ret is set). Else returns false and ret is unchanged.
static bool index_type_evaluate_type_internal(TypeCtx* ctx, Type const* me, Type const* index, Type* ret) {
    if (ctx->recursion_level > 500) {
        NONFATAL("Max type recursion depth exceeded (index_type_evaluate_type_internal)");
        return false;
    }

    index = type_resolve_internal(ctx, index, true);

    if (index->tag == TYPE_UNION || index->tag == TYPE_INTERSECTION) {
        Type comp = type_composite_base(index->tag);
        bool found_in_all = true;
        bool found_in_any = false;
        c_foreach(i, vec_type, index->composite) {
            Type temp;
            if (index_type_evaluate_type_internal(ctx, me, i.ref, &temp)) {
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
    } else if (!type_is_simple_index_type(index)) {
        return false;
    }

    me = type_resolve_internal(ctx, me, true);
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
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_CALLABLE:
            return false;

        case TYPE_VALUE:
            switch (my value.tag) {
                case VALUE_UNDEFINED:
                case VALUE_NULL:
                case VALUE_BOOLEAN:
                case VALUE_NUMBER:
                case VALUE_SYMBOL:
                case VALUE_BIGINT:
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
            c_foreach(i, vec_interface_member, my interface.members) {
                if (interface_member_key_overlaps(i.ref, index)) {
                    *ret = type_deref_clone(i.ref->type);
                    ret->parsed = false;
                    if (i.ref->optional) {
                        *ret = type_composite_internal(ctx, TYPE_INTERSECTION, *ret, type_undefined);
                    }
                    return true;
                }
            }
            return false;
        }

        case TYPE_ARRAY:
            if (type_extends(index, &type_number)) {
                *ret = type_deref_clone(*my array.element);
                ret->parsed = false;
                return true;
            } else {
                return false;
            }

        case TYPE_TUPLE: {
            if (!type_extends(index, &type_number)) {
                return false;
            }

            Type comp = type_composite_base(TYPE_UNION);
            c_foreach(i, vec_tuple_element, my tuple.elements) {
                intptr_t j = i.ref - my tuple.elements.data;
                Type key = type_value(value_number(j));
                if (type_extends(&key, index)) {
                    Type item_type = type_deref_clone(i.ref->type);
                    item_type.parsed = false;
                    comp = type_composite_internal(ctx, TYPE_UNION, comp, item_type);
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
                if (index_type_evaluate_type_internal(ctx, i.ref, index, &temp)) {
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
        }

        case TYPE_IDENTIFIER:
        case TYPE_INFERRED:
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TYPEOF:
        case TYPE_TERNARY:
        case TYPE_LAZY:
        case TYPE_REF:
            PANIC("Type with tag %d should have been resolved away", my tag);
    }
    UNREACHABLE;
}

Type index_type_evaluate_type(Type const* me, Type const* index) {
    TypeCtx ctx = {0};
    Type ret = type_any;
    index_type_evaluate_type_internal(&ctx, me, index, &ret);
    type_ctx_drop(&ctx);
    return ret;
}

static void index_type_typecheck_internal(
    TypeCtx* ctx,
    Type const* me,
    Type const* index,
    Span indexed_span,
    Span index_span,
    bool error_if_not_found,
    ErrorCtx* errs
) {
    if (ctx->recursion_level > 500) {
        NONFATAL("Max type recursion depth exceeded (index_type_typecheck_internal)");
        return;
    }

    index = type_resolve_internal(ctx, index, true);

    if (index->tag == TYPE_UNION || index->tag == TYPE_INTERSECTION) {
        c_foreach(i, vec_type, index->composite) {
            index_type_typecheck_internal(ctx, me, i.ref, indexed_span, index_span, error_if_not_found, errs);
        }
    } else if (!type_is_simple_index_type(index)) {
        cstr msg = cstr_lit("Type '");
        cstr_append_type_error_msg(&msg, index);
        cstr_append_lit(&msg, "' cannot be used as an index type.");
        error_ctx_error_s(errs, index_span, 2538, msg);
        return;
    }

    Type const* me_original = me;
    me = type_resolve_internal(ctx, me, true);

    switch (my tag) {
        case TYPE_ANY: break;
        case TYPE_VALUE:
            switch (my value.tag) {
                case VALUE_NULL:
                case VALUE_UNDEFINED:
                    if (get_error_config()->no_unchecked_indexed_access && error_if_not_found) {
                        error_ctx_error(
                            errs,
                            indexed_span,
                            18047,
                            "'%.*s' is possibly '%s'.",
                            c_SPAN(indexed_span),
                            my value.tag == VALUE_NULL ? "null" : "undefined"
                        );
                    }
                    break;

                default:
                    if (get_error_config()->no_unchecked_indexed_access && error_if_not_found) {
                        cstr msg = cstr_lit("Property '");
                        if (index->tag == TYPE_VALUE && index->value.tag == VALUE_STRING) {
                            cstr_append_s(&msg, index->value.string);
                        } else {
                            cstr_append_type_error_msg(&msg, index);
                        }
                        cstr_append_lit(&msg, "' does not exist on type '");
                        cstr_append_type_error_msg(&msg, me_original);
                        cstr_append_lit(&msg, "'.");
                        error_ctx_error_s(errs, index_span, 2339, msg);
                    }
                break;
            }
            break;

        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_UNKNOWN:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_CALLABLE:
            if (get_error_config()->no_unchecked_indexed_access && error_if_not_found) {
                cstr msg = cstr_lit("Property '");
                if (index->tag == TYPE_VALUE && index->value.tag == VALUE_STRING) {
                    cstr_append_s(&msg, index->value.string);
                } else {
                    cstr_append_type_error_msg(&msg, index);
                }
                cstr_append_lit(&msg, "' does not exist on type '");
                cstr_append_type_error_msg(&msg, me_original);
                cstr_append_lit(&msg, "'.");
                error_ctx_error_s(errs, index_span, 2339, msg);
            }
            break;

        case TYPE_INTERFACE: {
            bool found = false;
            c_foreach(i, vec_interface_member, my interface.members) {
                if (interface_member_key_overlaps(i.ref, index)) {
                    found = true;
                }
            }
            if (!found) {
                if (get_error_config()->no_unchecked_indexed_access && error_if_not_found) {
                    cstr msg = cstr_lit("Property '");
                    if (index->tag == TYPE_VALUE && index->value.tag == VALUE_STRING) {
                        cstr_append_s(&msg, index->value.string);
                    } else {
                        cstr_append_type_error_msg(&msg, index);
                    }
                    cstr_append_lit(&msg, "' does not exist on type '");
                    cstr_append_type_error_msg(&msg, me_original);
                    cstr_append_lit(&msg, "'.");
                    error_ctx_error_s(errs, index_span, 2551, msg);
                }
            }
        }   break;

        case TYPE_ARRAY: break;

        case TYPE_TUPLE:
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
                cstr_append_lit(&msg, "'.");
                error_ctx_error_s(errs, index_span, 2493, msg);
            }
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                index_type_typecheck_internal(ctx, i.ref, index, indexed_span, index_span, error_if_not_found, errs);
            }
            break;

        case TYPE_IDENTIFIER:
        case TYPE_INFERRED:
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TYPEOF:
        case TYPE_TERNARY:
        case TYPE_LAZY:
        case TYPE_REF:
            PANIC("Type with tag %d should have been resolved away", my tag);
    }
}

void index_type_typecheck(
    Type const* me,
    Type const* index,
    Span indexed_span,
    Span index_span,
    bool error_if_not_found,
    ErrorCtx* errs
) {
    TypeCtx ctx = {0};
    index_type_typecheck_internal(&ctx, me, index, indexed_span, index_span, error_if_not_found, errs);
    type_ctx_drop(&ctx);
}

static bool check_assignable_or_error_internal(
    const char* source_label,
    Type const* source,
    const char* target_label,
    Type const* target,
    unsigned ts_code,
    Span target_span,
    ErrorCtx* errs
) {
    Type local_type = {0};

    if (!get_error_config()->strict_null_checks) {
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
        target_span,
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

void type_error_if_not_assignable(Type const* source, Type const* target, Span target_span, ErrorCtx* errs) {
    check_assignable_or_error_internal("Type", source, "type", target, 2322, target_span, errs);
}

Type type_call_evaluate_type(Type const* me, CallExpr const* call) {
    me = type_resolve(me);

    switch (my tag) {
        case TYPE_ANY:
            return type_any;

        case TYPE_CALLABLE: {
            CallableType inferred = {0};
            CallableType* callable = my callable;

            if (call->is_extends) {
                if (!callable->is_constructor) {
                    return type_any;
                }
            } else if (call->uses_new) {
                if (!callable->is_constructor || callable->is_abstract_constructor) {
                    return type_any;
                }
            }

            if (!generic_empty(&callable->generic)) {
                inferred = generic_infer_callable_types(&callable->generic, callable, &call->args);
                callable = &inferred;
            }

            Type ret = type_deref_clone(callable->ret);
            ret.parsed = false;
            callable_type_drop(&inferred);
            return ret;
        }

        case TYPE_UNION:
        case TYPE_INTERSECTION: {
            Type ret = type_composite_base(my tag);
            c_foreach(i, vec_type, my composite) {
                ret = type_composite(
                    my tag,
                    ret,
                    type_call_evaluate_type(i.ref, call)
                );
            }
            return ret;
        }

        case TYPE_INTERFACE:
            bool is_callable;
            InterfaceMember const* overload = interface_select_callable_overload(&my interface, call, &is_callable);
            if (overload) {
                return type_call_evaluate_type(&overload->type, call);
            } else if (is_callable) {
                Type ret = type_composite_base(TYPE_INTERSECTION);
                c_foreach(i, vec_interface_member, my interface.members) {
                    if (i.ref->key.tag == INTERFACE_KEY_CALLABLE) {
                        Type callable_ret = type_deref_clone(i.ref->type.callable->ret);
                        callable_ret.parsed = false;
                        ret = type_composite(TYPE_INTERSECTION, ret, callable_ret);
                    }
                }
                return ret;
            } else {
                return type_any;
            }

        default: break;
    }

    return type_any;
}

void type_typecheck_call(Type const* me, CallExpr const* call, Span span, ErrorCtx* errs) {
    me = type_resolve(me);

    switch (my tag) {
        case TYPE_ANY: return;

        case TYPE_CALLABLE: {
            CallableType inferred = {0};
            CallableType* callable = my callable;

            if (call->is_extends) {
                if (!callable->is_constructor) {
                    break;
                }
            } else if (call->uses_new) {
                if (!callable->is_constructor) {
                    if (get_error_config()->no_implicit_any) {
                        error_ctx_error(errs, span, 7009,
                            "'new' expression, whose target lacks a construct signature, implicitly has an 'any' type.");
                    }
                } else if (callable->is_abstract_constructor) {
                    error_ctx_error(errs, span, 2511, "Cannot create an instance of an abstract class.");
                }
            } else {
                if (callable->is_constructor) {
                    error_ctx_error(errs, span, 2348,
                        "Value of type 'typeof Foo' is not callable. Did you mean to include 'new'?");
                }
            }

            if (!generic_empty(&callable->generic)) {
                inferred = generic_infer_callable_types(&callable->generic, callable, &call->args);
                callable = &inferred;
            }

            intptr_t call_arg_count = vec_expr_size(&call->args);
            intptr_t callable_arg_count = vec_callable_arg_size(&callable->args);
            intptr_t min_args = callable_type_min_arg_count(callable);
            intptr_t max_args = callable_type_max_arg_count(callable);
            bool check_types_against_callable = true;

            if (call_arg_count < min_args || call_arg_count > max_args) {
                check_types_against_callable = false;

                Span span = call->callable.inner_span;
                if (call_arg_count > max_args) {
                    span = span_between(
                        vec_expr_at(&call->args, max_args)->outer_span,
                        vec_expr_at(&call->args, call_arg_count - 1)->outer_span
                    );
                }

                if (max_args == INTPTR_MAX) {
                    error_ctx_error(errs, span, 2555,
                        "Expected at least %ld arguments, but got %ld.", min_args, call_arg_count);
                } else if (min_args == max_args) {
                    error_ctx_error(errs, span, 2554,
                        "Expected %ld arguments, but got %ld.", min_args, call_arg_count);
                } else {
                    error_ctx_error(errs, span, 2554,
                        "Expected %ld-%ld arguments, but got %ld.", min_args, max_args, call_arg_count);
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
                if (check_types_against_callable && index < callable_arg_count - (last_arg_rest ? 1 : 0)) {
                    CallableArg* arg = vec_callable_arg_at_mut(&callable->args, index);
                    if (!check_assignable_or_error_internal(
                        "Argument of type",
                        expr_get_type(i.ref),
                        "parameter of type",
                        &arg->type,
                        2345,
                        i.ref->outer_span,
                        errs
                    )) {
                        check_types_against_callable = false;
                    }
                } else if (last_arg_rest) {
                    rest_span = span_between(rest_span, i.ref->outer_span);
                    vec_tuple_element_push(&rest_tuple, (TupleElement){
                        .type = type_refcopy(expr_get_type(i.ref)),
                    });
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
                        &arg->type,
                        2345,
                        span_valid(rest_span) ? rest_span : span,
                        errs
                    );
                }
                type_drop(&rest_type);
            }

            callable_type_drop(&inferred);
        }   return;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                type_typecheck_call(i.ref, call, span, errs);
            };
            return;

        case TYPE_INTERFACE:
            bool is_callable;
            InterfaceMember const* overload = interface_select_callable_overload(&my interface, call, &is_callable);
            if (overload) {
                type_typecheck_call(&overload->type, call, span, errs);
                return;
            } else if (is_callable) {
                error_ctx_error(errs, call->callable.outer_span, 2769, "No overload matches this call.");
                return;
            } else {
                break;
            }

        default: break;
    }

    if (call->is_extends) {
        cstr msg = cstr_lit("Type '");
        cstr_append_type_error_msg(&msg, me);
        cstr_append_lit(&msg, "' is not a constructor function type.");
        error_ctx_error_s(errs, call->callable.outer_span, 2507, msg);
    } else {
        Error* e = error_ctx_error(errs, call->callable.outer_span, 2349, "This expression is not callable.");
        if (my tag == TYPE_INTERFACE) {
            cstr msg = cstr_lit("Type '");
            cstr_append_type_error_msg(&msg, me);
            cstr_append_lit(&msg, "' has no call signatures.");
            error_sub_error_s(e, span_null, TS_CODE_NONE, msg);
        }
    }
}
