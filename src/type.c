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
#include "ty/ty.h"

// How an index shows up in a "Property 'x' does not exist" message: a string literal index shows its value, anything
// else shows its type
typedef struct IndexPropertyName {
    Type const* index;
} IndexPropertyName;

#define index_property_name(index) ((IndexPropertyName){ .index = (index) })

impl_display_no_drop(IndexPropertyName, index_property_name, static) {
    if (me.index->tag == TYPE_VALUE && me.index->value.tag == VALUE_STRING) {
        return debuggable_sv_display(cstr_sv(&me.index->value.string), d);
    } else {
        return type_display(me.index, d);
    }
}

#undef DISPLAYABLES
#define DISPLAYABLES \
    TTTT_DISPLAYABLES, \
    IndexPropertyName: index_property_name_display

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
    HSetDeclarationCPtr currently_resolving;
    // legacy_infer_extends(): the (declaration of a named sub type, super type) pairs being compared up the stack
    struct { void const* sub; void const* super; } in_progress[64];
    unsigned in_progress_count;
} TypeCtx;

static bool type_is_strict_subtype(Type const* sub, Type const* super);
static bool legacy_infer_extends(
    TypeCtx* ctx,
    Type const* sub,
    Type const* super,
    VecTypeRefinement* refinements
);
static Type const* type_resolve_internal(TypeCtx* ctx, Type const* me, bool through_identifier);
static bool index_type_evaluate_type_internal(TypeCtx* ctx, Type const* me, Type const* index, Type* ret);

static void type_ctx_drop(TypeCtx* me) {
    hset_declaration_cptr_drop(&my currently_resolving);
}

Type type_composite_without_simplify(typeof((Type){}.tag) tag, Type a, Type b) {
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
        return (Type){
            .tag = tag,
            .composite = c_init(vec_type, {a, b}),
        };
    }
}

static Type type_composite_internal(TypeCtx* ctx, typeof((Type){}.tag) tag, Type a, Type b) {
    (void)ctx;
    check(tag == TYPE_UNION || tag == TYPE_INTERSECTION);
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
        if (type_is_strict_subtype(&a, &b)) {
            if (tag == TYPE_UNION) {
                type_drop(&a);
                return b;
            } else {
                type_drop(&b);
                return a;
            }
        } else if (type_is_strict_subtype(&b, &a)) {
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
    check(my tag == TYPE_UNION || my tag == TYPE_INTERSECTION);
    Type ret = type_composite_base(my tag);
    for_each (i, vec_type, &my composite) {
        ret = type_composite(my tag, ret, *i);
        *i = type_any;
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
        case TYPE_TYPEOF: return type_typeof(c_new(TypeofType, typeof_type_clone(*me.typeof_)));
        case TYPE_TERNARY: return type_ternary(c_new(TernaryType, ternary_type_clone(*me.ternary)));
        case TYPE_UNION: case TYPE_INTERSECTION: return (Type){.tag = me.tag, .composite = vec_type_clone(me.composite)};
    }
    impossible;
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
            for_each (i, vec_spanned_type, &my identifier->generic_params) {
                type_add_inferred_names(&i->type, scope);
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
            for_each (i, vec_callable_arg, &my callable->args) {
                type_add_inferred_names(&i->type, scope);
            }
            type_add_inferred_names(&my callable->ret, scope);
            break;

        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &my interface.members) {
                type_add_inferred_names(&i->type, scope);
            }
            break;

        case TYPE_ARRAY:
            type_add_inferred_names(my array.element, scope);
            break;

        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &my tuple.elements) {
                type_add_inferred_names(&i->type, scope);
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
            for_each (i, vec_type, &my composite) {
                type_add_inferred_names(i, scope);
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
            for_each (i, vec_interface_member, &my interface.members) {
                interface_member_lookup_names(i, names, errs);
            }
            break;

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
            for_each (i, vec_type, &my composite) {
                type_lookup_names(i, names, errs);
            }
            break;

        case TYPE_LAZY: // Resolving names is the responsibility of the LazyType owner
        case TYPE_REF: break;
    }
}

void type_typecheck(Type const* me, ErrorCtx* errs) {
    ty_shadow_record_node(me);
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
            for_each (i, vec_tuple_element, &my tuple.elements) {
                type_typecheck(&i->type, errs);
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
            for_each (i, vec_interface_member, &my interface.members) {
                switch (i->key.tag) {
                    case INTERFACE_KEY_NAME:
                        ret = type_composite_internal(
                            ctx,
                            TYPE_UNION,
                            ret,
                            type_value(value_string(cstr_clone(i->key.name_string)))
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
            for_each (i, vec_type, &my composite) {
                ret = type_composite_internal(ctx, flipped_tag, ret, type_all_keys(ctx, i));
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

static bool type_contains_inferred(Type const* me, unsigned depth) {
    if (depth > 32) return true;
    switch (my tag) {
        case TYPE_INFERRED: return true;
        case TYPE_REF: return type_contains_inferred(my ref, depth + 1);
        case TYPE_ARRAY: return type_contains_inferred(my array.element, depth + 1);
        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &my tuple.elements) {
                if (type_contains_inferred(&i->type, depth + 1)) return true;
            }
            return false;
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &my composite) {
                if (type_contains_inferred(i, depth + 1)) return true;
            }
            return false;
        case TYPE_IDENTIFIER:
            for_each (i, vec_spanned_type, &my identifier->generic_params) {
                if (type_contains_inferred(&i->type, depth + 1)) return true;
            }
            return false;
        case TYPE_CALLABLE:
            for_each (i, vec_callable_arg, &my callable->args) {
                if (type_contains_inferred(&i->type, depth + 1)) return true;
            }
            return type_contains_inferred(&my callable->ret, depth + 1);
        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &my interface.members) {
                if (type_contains_inferred(&i->type, depth + 1)) return true;
            }
            return false;
        case TYPE_INDEXED:
            return type_contains_inferred(&my indexed->interface, depth + 1) ||
                type_contains_inferred(&my indexed->index, depth + 1);
        case TYPE_KEYOF: return type_contains_inferred(&my keyof->interface, depth + 1);
        case TYPE_TERNARY:
            return type_contains_inferred(&my ternary->maybe_sub, depth + 1) ||
                type_contains_inferred(&my ternary->maybe_super, depth + 1);
        default: return false;
    }
}

static inline Type const* type_resolve_ternary_type(TypeCtx* ctx, TernaryType* me) {
    if (my has_cache) {
        return &my cached;
    } else if (my maybe_sub.tag == TYPE_ANY) {
        my cached = type_composite_internal(ctx, TYPE_UNION, type_clone(my if_true), type_clone(my if_false));
        my has_cache = true;
        return &my cached;
    } else if (!type_contains_inferred(&my maybe_super, 0)) {
        return type_extends(&my maybe_sub, &my maybe_super) ? &my if_true : &my if_false;
    } else {
        // HACK(stage 4): `infer` needs its results as legacy types, so this is the one thing the legacy structural
        // relation is still used for
        VecTypeRefinement refinements = {0};
        Type const* ret;
        if (legacy_infer_extends(ctx, &my maybe_sub, &my maybe_super, &refinements)) {
            if (!vec_type_refinement_is_empty(&refinements)) {
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
            for_each (i, vec_type, &my composite) {
                Type const* resolved = type_resolve_internal(ctx, i, true);
                if (resolved->tag == (my tag == TYPE_UNION ? TYPE_ANY : TYPE_NEVER)) {
                    return resolved;
                }
            }
            return nullptr;

        case TYPE_LAZY: return lazy_type_get(my lazy);

        case TYPE_REF: return my ref;
    }
    impossible;
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

Type const* type_resolve_until_identifier(Type const* me) {
    TypeCtx ctx = {0};
    Type const* ret = type_resolve_internal(&ctx, me, false);
    type_ctx_drop(&ctx);
    return ret;
}

static inline bool legacy_infer_extends_more(TypeCtx* ctx, Type const* sub, Type const* super, VecTypeRefinement* refinements) {
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
            return legacy_infer_extends(
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
                return legacy_infer_extends(ctx, sub_resolved, super, refinements);
            } else {
                return false;
            }
        }

        case TYPE_UNION:
            for_each (i, vec_type, &sub->composite) {
                if (!legacy_infer_extends(ctx, i, super, refinements)) return false;
            }
            return true;

        case TYPE_INTERSECTION:
            for_each (i, vec_type, &sub->composite) {
                if (legacy_infer_extends(ctx, i, super, refinements)) return true;
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
                for_each (i, vec_interface_member, &sub->interface.members) {
                    if (i->key.tag == INTERFACE_KEY_CALLABLE) {
                        if (legacy_infer_extends(
                            ctx,
                            &i->type,
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
                    if (!legacy_infer_extends(
                        ctx,
                        &vec_callable_arg_at_mut(&sub->callable->args, i)->type,
                        &vec_callable_arg_at_mut(&super->callable->args, i)->type,
                        refinements
                    )) {
                        return false;
                    }
                }
                // Order is not flipped for return
                return legacy_infer_extends(ctx, &sub->callable->ret, &super->callable->ret, refinements);
            } else {
                return false;
            }

        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &super->interface.members) {
                switch (i->key.tag) {
                    case INTERFACE_KEY_NAME: {
                        // Don't clone the string, and don't drop it the type. Hacky but safe.
                        Type key = type_value(value_string(i->key.name_string));
                        Type prop;
                        if (index_type_evaluate_type_internal(ctx, sub, &key, &prop)) {
                            // TODO: pass in refinements, but first make refinements own it's types
                            // (will fix nondistributiveConditionalTypeInfer)
                            Type member_type = type_refcopy(&i->type);
                            if (i->optional) {
                                member_type = type_composite(TYPE_UNION, member_type, type_undefined);
                            }
                            bool extends = legacy_infer_extends(ctx, &prop, &member_type, nullptr);
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
                return legacy_infer_extends(ctx, sub->array.element, super->array.element, refinements);
            } else if (sub->tag == TYPE_TUPLE) {
                if (sub->tuple.readonly && !super->array.readonly) {
                    return false;
                }
                for_each (i, vec_tuple_element, &sub->tuple.elements) {
                    if (i->rest) {
                        // Make sure every item in the rest tuple/array extends the array type
                        Type elem = index_type_evaluate_type(&i->type, &type_number);
                        bool extends = legacy_infer_extends(ctx, &elem, super->array.element, refinements);
                        type_drop(&elem);
                        if (!extends) return false;
                    } else {
                        if (!legacy_infer_extends(ctx, &i->type, super->array.element, refinements)) {
                            return false;
                        }
                    }
                    if (i->readonly && !super->array.readonly) {
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
                if (!legacy_infer_extends(
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
                if (legacy_infer_extends(ctx, sub, &super->inferred->extends, nullptr)) {
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
            for_each (i, vec_type, &super->composite) {
                if (legacy_infer_extends(ctx, sub, i, refinements)) return true;
            }
            return false;

        case TYPE_INTERSECTION:
            for_each (i, vec_type, &super->composite) {
                if (!legacy_infer_extends(ctx, sub, i, refinements)) return false;
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
            impossible;
    }
    impossible;
}

static bool legacy_infer_extends(TypeCtx* ctx, Type const* sub, Type const* super, VecTypeRefinement* refinements) {
    if (ctx->recursion_level > 100) {
        log_error("Max type recursion depth exceeded (legacy_infer_extends)");
        return true;
    }
    // HACK: recursive types are assumed to extend when they come around again. Coarse (type arguments are ignored),
    // but without it this is exponential, and it only exists to collect `infer` results anymore
    bool named = (sub->tag == TYPE_IDENTIFIER && sub->identifier->decl_ref) ||
        (super->tag == TYPE_IDENTIFIER && super->identifier->decl_ref);
    if (named) {
        // Named types are cloned every time they're resolved, so their declaration is all the identity there is
        void const* sub_key = sub->tag == TYPE_IDENTIFIER && sub->identifier->decl_ref ? (void const*)sub->identifier->decl_ref : sub;
        void const* super_key = super->tag == TYPE_IDENTIFIER && super->identifier->decl_ref ? (void const*)super->identifier->decl_ref : super;
        for (unsigned i = 0; i < ctx->in_progress_count; i++) {
            if (ctx->in_progress[i].sub == sub_key && ctx->in_progress[i].super == super_key) return true;
        }
        if (ctx->in_progress_count == c_arraylen(ctx->in_progress)) return true;
        ctx->in_progress[ctx->in_progress_count].sub = sub_key;
        ctx->in_progress[ctx->in_progress_count++].super = super_key;
    }
    ctx->recursion_level++;
    bool ret = legacy_infer_extends_more(ctx, sub, super, refinements);
    ctx->recursion_level--;
    if (named) ctx->in_progress_count--;
    return ret;
}

// The relations live in the new type core (ty/relate.c). HACK(stage 4): until expressions evaluate to Ty, both sides
// get lowered on every call. They may be temporaries, which is what LowerCtx.transient is about
static Ty const* lower_for_relation(Checker* checker, Type const* type) {
    return ty_from_legacy(checker, type, (LowerCtx){ .generic = true, .transient = true });
}

bool type_extends(Type const* sub, Type const* super) {
    Checker* checker = current_checker;
    panic_if(!checker, "type_extends() without a current checker");
    return ty_is_related(checker, lower_for_relation(checker, sub), lower_for_relation(checker, super), TY_RELATION_ASSIGNABLE);
}

static bool type_is_strict_subtype(Type const* sub, Type const* super) {
    Checker* checker = current_checker;
    panic_if(!checker, "union reduction without a current checker");
    return ty_is_related(
        checker, lower_for_relation(checker, sub), lower_for_relation(checker, super), TY_RELATION_STRICT_SUBTYPE
    );
}

bool type_extends_strict_nulls(Type const* sub, Type const* super) {
    Checker* checker = current_checker;
    panic_if(!checker, "type_extends_strict_nulls() without a current checker");
    return ty_is_related(
        checker, lower_for_relation(checker, sub), lower_for_relation(checker, super), TY_RELATION_ASSIGNABLE_STRICT_NULLS
    );
}

bool type_satisfies_constraint(Generic const* generic, VecSpannedType const* args, intptr_t index) {
    Checker* checker = current_checker;
    panic_if(!checker, "type_satisfies_constraint() without a current checker");
    // The constraint can mention the other type parameters, which stand for the arguments here
    Ty const* params[32];
    Ty const* lowered[32];
    u32 len = 0;
    for_each (i, vec_generic_item, &generic->items) {
        if (len == c_arraylen(params) || i_index >= vec_spanned_type_size(args) || !i->declaration_ref) break;
        params[len] = ty_type_param(checker, i->declaration_ref);
        lowered[len++] = lower_for_relation(checker, &vec_spanned_type_at(args, i_index)->type);
    }
    if ((u32)index >= len) return true;
    Ty const* constraint = ty_param_constraint(checker, params[index]);
    if (!constraint) return true;
    TyMapper mapper = { .len = len, .sources = params, .targets = lowered };
    return ty_is_related(checker, lowered[index], ty_instantiate(checker, constraint, &mapper), TY_RELATION_ASSIGNABLE);
}

bool type_overlaps(Type const* a, Type const* b) {
    Checker* checker = current_checker;
    panic_if(!checker, "type_overlaps() without a current checker");
    Ty const* lowered_a = lower_for_relation(checker, a);
    Ty const* lowered_b = lower_for_relation(checker, b);
    return ty_is_related(checker, lowered_a, lowered_b, TY_RELATION_COMPARABLE) ||
        ty_is_related(checker, lowered_b, lowered_a, TY_RELATION_COMPARABLE);
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
            if (!vec_tuple_element_is_empty(&my tuple.elements) && vec_tuple_element_back(&my tuple.elements)->rest) {
                ret--;
                ret += type_get_numeric_index_bound(&vec_tuple_element_back(&my tuple.elements)->type, get_max);
            }
            return ret;
        }

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            intptr_t result = get_max ? -1 : 0;
            for_each (i, vec_type, &my composite) {
                intptr_t bound = type_get_numeric_index_bound(i, get_max);
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
                    for_each_kv (name, item, hmap_object_item, &my value.object.get->items) {
                        InterfaceMember member = interface_member_make(
                            span_null,
                            false,
                            false,
                            cstr_clone(*name),
                            span_null,
                            false,
                            type_value(value_clone(item->value))
                        );
                        check(member.type.tag == INTERFACE_TYPE_INSTANTIATED);
                        type_abstract_literals(&member.type.instantiated);
                        vec_interface_member_push(&interface.members, member);
                    }
                    type_drop(me);
                    *me = type_interface(interface);
                }   break; */
                    log_error("type_abstract_literals() not implemented for objects");
                    break;

                case VALUE_ARRAY:
                    log_error("type_abstract_literals() not implemented for arrays");
                    break;

                case VALUE_JS_FUNC:
                case VALUE_NATIVE_FUNC:
                    type_drop(me);
                    *me = type_any;
                    break;
            }
            break;

        case TYPE_TUPLE: {
            for_each (i, vec_tuple_element, &my tuple.elements) {
                type_abstract_literals(&i->type);
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
            for_each (i, vec_type, &my composite) {
                type_remove_null_and_undefined(i);
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
    impossible;
}

// Returns true if index found (ret is set). Else returns false and ret is unchanged.
static bool index_type_evaluate_type_internal(TypeCtx* ctx, Type const* me, Type const* index, Type* ret) {
    if (ctx->recursion_level > 500) {
        log_error("Max type recursion depth exceeded (index_type_evaluate_type_internal)");
        return false;
    }

    index = type_resolve_internal(ctx, index, true);

    if (index->tag == TYPE_UNION || index->tag == TYPE_INTERSECTION) {
        Type comp = type_composite_base(index->tag);
        bool found_in_all = true;
        bool found_in_any = false;
        for_each (i, vec_type, &index->composite) {
            Type temp;
            if (index_type_evaluate_type_internal(ctx, me, i, &temp)) {
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
            for_each (i, vec_interface_member, &my interface.members) {
                if (interface_member_key_overlaps(i, index)) {
                    *ret = type_clone(i->type);
                    if (i->optional) {
                        *ret = type_composite_internal(ctx, TYPE_INTERSECTION, *ret, type_undefined);
                    }
                    return true;
                }
            }
            return false;
        }

        case TYPE_ARRAY:
            if (type_extends(index, &type_number)) {
                *ret = type_clone(*my array.element);
                return true;
            } else {
                return false;
            }

        case TYPE_TUPLE: {
            if (!type_extends(index, &type_number)) {
                return false;
            }

            Type comp = type_composite_base(TYPE_UNION);
            for_each (i, vec_tuple_element, &my tuple.elements) {
                intptr_t j = i_index;
                Type key = type_value(value_number(j));
                if (type_extends(&key, index)) {
                    comp = type_composite_internal(ctx, TYPE_UNION, comp, type_clone(i->type));
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
            for_each (i, vec_type, &my composite) {
                Type temp;
                if (index_type_evaluate_type_internal(ctx, i, index, &temp)) {
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
            panic("Type with tag ", (i32)my tag, " should have been resolved away");
    }
    impossible;
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
        log_error("Max type recursion depth exceeded (index_type_typecheck_internal)");
        return;
    }

    index = type_resolve_internal(ctx, index, true);

    if (index->tag == TYPE_UNION || index->tag == TYPE_INTERSECTION) {
        for_each (i, vec_type, &index->composite) {
            index_type_typecheck_internal(ctx, me, i, indexed_span, index_span, error_if_not_found, errs);
        }
    } else if (!type_is_simple_index_type(index)) {
        error_ctx_error_d(errs, index_span, 2538, "Type '", index, "' cannot be used as an index type.");
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
                        error_ctx_error_d(
                            errs, indexed_span, 18047,
                            "'", indexed_span, "' is possibly '",
                            my value.tag == VALUE_NULL ? "null" : "undefined", "'."
                        );
                    }
                    break;

                default:
                    if (get_error_config()->no_unchecked_indexed_access && error_if_not_found) {
                        error_ctx_error_d(errs, index_span, 2339,
                            "Property '", index_property_name(index),
                            "' does not exist on type '", me_original, "'.");
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
                error_ctx_error_d(errs, index_span, 2339,
                    "Property '", index_property_name(index),
                    "' does not exist on type '", me_original, "'.");
            }
            break;

        case TYPE_INTERFACE: {
            bool found = false;
            for_each (i, vec_interface_member, &my interface.members) {
                if (interface_member_key_overlaps(i, index)) {
                    found = true;
                }
            }
            if (!found) {
                if (get_error_config()->no_unchecked_indexed_access && error_if_not_found) {
                    error_ctx_error_d(errs, index_span, 2551,
                        "Property '", index_property_name(index),
                        "' does not exist on type '", me_original, "'.");
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
                error_ctx_error_d(errs, index_span, 2493,
                    "Tuple type '", me, "' of length '", vec_tuple_element_size(&my tuple.elements),
                    "' has no element at index '", &index->value, "'.");
            }
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &my composite) {
                index_type_typecheck_internal(ctx, i, index, indexed_span, index_span, error_if_not_found, errs);
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
            panic("Type with tag ", (i32)my tag, " should have been resolved away");
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
    Checker* checker = current_checker;
    panic_if(!checker, "assignability check without a current checker");
    Ty const* lowered_source = lower_for_relation(checker, source);
    // HACK: the legacy checker says `any` for whatever it can't type (no initializer, failed inference...), and its
    // relation used to let `any` through to `never` as well
    if (lowered_source->flags & TYF_ANY) return true;
    Ty const* lowered_target = lower_for_relation(checker, target);
    bool related = ty_is_related(checker, lowered_source, lowered_target, TY_RELATION_ASSIGNABLE);
    if (checker->instantiation_limit_hit) {
        checker->instantiation_limit_hit = false;
        error_ctx_error_d(errs, target_span, 2589, "Type instantiation is excessively deep and possibly infinite.");
        return false;
    }
    if (related) {
        return true;
    }

    // Why not, outermost line first
    VecTyReportLine lines = {0};
    ty_explain_unrelated(checker, lowered_source, lowered_target, TY_RELATION_ASSIGNABLE, &lines);
    checker->instantiation_limit_hit = false;

    Type source_abstracted = type_any;
    if (target->tag != TYPE_VALUE) {
        source_abstracted = type_clone(*source);
        type_abstract_literals(&source_abstracted);
        source = &source_abstracted;
    }

    // Rendered before the types are dropped below, which happens before the error is built
    // HACK(stage 4): the head line is still the legacy rendering of the legacy types (it knows how expression types
    // print, the lowered temporaries don't quite), the elaboration below it comes from the new core
    cstr source_str = cstr_d(source);
    cstr target_str = cstr_d(target);
    type_drop(&source_abstracted);

    isize_t first_nested = 0;
    Error* error;
    bool default_head = ts_code == 2322;
    TyReportLine const* natural_head = vec_ty_report_line_size(&lines) ? vec_ty_report_line_at(&lines, 0) : nullptr;
    if (default_head && natural_head && natural_head->ts_code != 2322) {
        // Something more specific than "not assignable" (a missing property, say) takes the place of the head
        error = error_ctx_error_s(errs, target_span, natural_head->ts_code, cstr_clone(natural_head->message));
        first_nested = 1;
    } else {
        error = error_ctx_error_d(
            errs, target_span, ts_code,
            source_label, " '", source_str, "' is not assignable to ", target_label, " '", target_str, "'."
        );
        if (natural_head && natural_head->ts_code == 2322) first_nested = 1;
    }
    for (isize_t i = first_nested; error && i < vec_ty_report_line_size(&lines); i++) {
        error_sub_error_s(error, (Span){0}, TS_CODE_NONE, cstr_clone(vec_ty_report_line_at(&lines, i)->message));
        error = vec_error_back_mut(&error->sub_errors);
    }
    vec_ty_report_line_drop(&lines);
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

            Type ret = type_clone(callable->ret);
            callable_type_drop(&inferred);
            return ret;
        }

        case TYPE_UNION:
        case TYPE_INTERSECTION: {
            Type ret = type_composite_base(my tag);
            for_each (i, vec_type, &my composite) {
                ret = type_composite(
                    my tag,
                    ret,
                    type_call_evaluate_type(i, call)
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
                for_each (i, vec_interface_member, &my interface.members) {
                    if (i->key.tag == INTERFACE_KEY_CALLABLE) {
                        ret = type_composite(TYPE_INTERSECTION, ret, type_clone(i->type.callable->ret));
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
                        error_ctx_error_d(errs, span, 7009,
                            "'new' expression, whose target lacks a construct signature, implicitly has an 'any' type.");
                    }
                } else if (callable->is_abstract_constructor) {
                    error_ctx_error_d(errs, span, 2511, "Cannot create an instance of an abstract class.");
                }
            } else {
                if (callable->is_constructor) {
                    error_ctx_error_d(errs, span, 2348,
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

            // TODO: a spread argument stands for an unknown number of arguments, so nothing is checked from it onwards
            intptr_t first_spread = INTPTR_MAX;
            for_each (i, vec_expr, &call->args) {
                if (i->tag == EXPR_SPREAD) {
                    first_spread = i_index;
                    break;
                }
            }

            if (first_spread != INTPTR_MAX) {
                // Can't know how many arguments there are
            } else if (call_arg_count < min_args || call_arg_count > max_args) {
                check_types_against_callable = false;

                Span span = call->callable.inner_span;
                if (call_arg_count > max_args) {
                    span = span_between(
                        vec_expr_at(&call->args, max_args)->outer_span,
                        vec_expr_at(&call->args, call_arg_count - 1)->outer_span
                    );
                }

                if (max_args == INTPTR_MAX) {
                    error_ctx_error_d(errs, span, 2555,
                        "Expected at least ", min_args, " arguments, but got ", call_arg_count, ".");
                } else if (min_args == max_args) {
                    error_ctx_error_d(errs, span, 2554,
                        "Expected ", min_args, " arguments, but got ", call_arg_count, ".");
                } else {
                    error_ctx_error_d(errs, span, 2554,
                        "Expected ", min_args, "-", max_args, " arguments, but got ", call_arg_count, ".");
                }
            }

            bool last_arg_rest =
                check_types_against_callable &&
                callable_arg_count &&
                vec_callable_arg_at(&callable->args, callable_arg_count - 1)->source->rest;
            Span rest_span = span_null;
            VecTupleElement rest_tuple = {0};

            for_each (i, vec_expr, &call->args) {
                intptr_t index = i_index;
                if (index >= first_spread) {
                    check_types_against_callable = false;
                    break;
                }
                // Don't report errors, errors inside argument expressions are reported elsewhere
                if (check_types_against_callable && index < callable_arg_count - (last_arg_rest ? 1 : 0)) {
                    CallableArg* arg = vec_callable_arg_at_mut(&callable->args, index);
                    if (!check_assignable_or_error_internal(
                        "Argument of type",
                        expr_get_type(i),
                        "parameter of type",
                        &arg->type,
                        2345,
                        i->outer_span,
                        errs
                    )) {
                        check_types_against_callable = false;
                    }
                } else if (last_arg_rest) {
                    rest_span = span_between(rest_span, i->outer_span);
                    vec_tuple_element_push(&rest_tuple, (TupleElement){
                        .type = type_refcopy(expr_get_type(i)),
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
            for_each (i, vec_type, &my composite) {
                type_typecheck_call(i, call, span, errs);
            };
            return;

        case TYPE_INTERFACE:
            bool is_callable;
            InterfaceMember const* overload = interface_select_callable_overload(&my interface, call, &is_callable);
            if (overload) {
                type_typecheck_call(&overload->type, call, span, errs);
                return;
            } else if (is_callable) {
                error_ctx_error_d(errs, call->callable.outer_span, 2769, "No overload matches this call.");
                return;
            } else {
                break;
            }

        default: break;
    }

    if (call->is_extends) {
        error_ctx_error_d(errs, call->callable.outer_span, 2507,
            "Type '", me, "' is not a constructor function type.");
    } else {
        Error* e = error_ctx_error_d(errs, call->callable.outer_span, 2349, "This expression is not callable.");
        if (my tag == TYPE_INTERFACE) {
            error_sub_error_d(e, span_null, TS_CODE_NONE, "Type '", me, "' has no call signatures.");
        }
    }
}
