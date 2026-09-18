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
} TypeCtx;

static bool type_is_strict_subtype(Type const* sub, Type const* super);
static Ty const* lower_for_relation(Checker* checker, Type const* type);
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
        case TYPE_NEW:
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
        case TYPE_NEW: return me;
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
        case TYPE_NEW: break;

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
        case TYPE_NEW:
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
        case TYPE_NEW:
        case TYPE_REF:
            break;
    }
}

// The type a destructuring binding takes out of its initializer: a property or element access
static inline Type const* type_resolve_binding_indexed_type(IndexedType const* me) {
    if (my array_only && !type_extends_array(&my interface)) return &type_any;
    Checker* checker = current_checker;
    Ty const* ret = ty_access(checker, lower_for_relation(checker, &my interface), lower_for_relation(checker, &my index));
    return ret ? ty_legacy_wrapper(checker, ret) : &type_any;
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
        case TYPE_INDEXED:
            if (my indexed->is_from_binding) return type_resolve_binding_indexed_type(my indexed);
            [[fallthrough]];
        case TYPE_KEYOF:
        case TYPE_TERNARY:
            // Type operators are evaluated by the new core
            return ty_legacy_wrapper(
                current_checker,
                ty_from_legacy(current_checker, me, (LowerCtx){ .generic = true, .transient = !my parsed })
            );

        case TYPE_TYPEOF: return &type_any;

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

        case TYPE_NEW: return ty_legacy_view(current_checker, type_new_ty(current_checker, me));
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

    // Named things of the new core are their own name, except type parameters, which legacy code knows by identifier
    if (my tag == TYPE_NEW && !through_identifier && type_new_ty(current_checker, me)->kind != TY_TYPE_PARAM) return me;

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

Type const* type_resolve_to_new(Type const* me) {
    TypeCtx ctx = {0};
    for (unsigned depth = 0; me && my tag != TYPE_NEW && depth < 64; depth++) {
        me = type_resolve_one_layer_internal(&ctx, me);
    }
    type_ctx_drop(&ctx);
    return me && my tag == TYPE_NEW ? me : nullptr;
}

Type const* type_resolve_until_identifier(Type const* me) {
    TypeCtx ctx = {0};
    Type const* ret = type_resolve_internal(&ctx, me, false);
    type_ctx_drop(&ctx);
    return ret;
}

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
    // tsc doesn't look at the value of an enum member here
    Type const* named = type_resolve_until_identifier(me);
    if (named->tag == TYPE_NEW && (type_new_ty(current_checker, named)->flags & TYF_ENUM_LITERAL)) return -1;
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

        case TYPE_NEW:
            *me = type_new(ty_widened_literal(current_checker, type_new_ty(current_checker, me)));
            break;
    }
}

void type_remove_null_and_undefined(Type* me) {
    // A type of the new core stays one (resolving it would flatten names and deferred types into their view)
    Type const* named = type_resolve_until_identifier(me);
    if (named->tag == TYPE_NEW) {
        Ty const* ty = ty_non_nullable(current_checker, type_new_ty(current_checker, named));
        type_drop(me);
        *me = type_new(ty);
        return;
    }
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
        case TYPE_SYMBOL:
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
                    if (i->optional && compiler_options()->strict_null_checks) {
                        *ret = type_composite_internal(ctx, TYPE_UNION, *ret, type_undefined);
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
        case TYPE_NEW:
            panic("Type with tag ", (i32)my tag, " should have been resolved away");
    }
    impossible;
}

// Property and element access is resolved by the new core (ty/access.c). HACK(stage 4b): both sides are still typed by
// the legacy checker and lowered on every access
Type index_type_evaluate_type(Type const* me, Type const* index) {
    Checker* checker = current_checker;
    panic_if(!checker, "property access without a current checker");
    Ty const* ret = ty_access(checker, lower_for_relation(checker, me), lower_for_relation(checker, index));
    return ret ? type_new(ret) : type_any;
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
                    if (compiler_options()->no_unchecked_indexed_access && error_if_not_found) {
                        error_ctx_error_d(
                            errs, indexed_span, 18047,
                            "'", indexed_span, "' is possibly '",
                            my value.tag == VALUE_NULL ? "null" : "undefined", "'."
                        );
                    }
                    break;

                default:
                    if (compiler_options()->no_unchecked_indexed_access && error_if_not_found) {
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
            if (compiler_options()->no_unchecked_indexed_access && error_if_not_found) {
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
                if (compiler_options()->no_unchecked_indexed_access && error_if_not_found) {
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
        case TYPE_NEW:
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

void type_report_unfit_argument(
    Type const* legacy_source, Ty const* source, Ty const* target, Span span, ErrorCtx* errs
) {
    Type new_source = type_new(source);
    Type new_target = type_new(target);
    check_assignable_or_error_internal(
        "Argument of type", legacy_source ? legacy_source : &new_source, "parameter of type", &new_target, 2345, span, errs
    );
}

// Calls are resolved by the new core (ty/call.c). HACK(stage 4b): the callee and the arguments are still typed by the
// legacy checker and lowered on every call
Type type_call_evaluate_type(Type const* me, CallExpr const* call) {
    Checker* checker = current_checker;
    panic_if(!checker, "call without a current checker");
    return type_new(ty_call_return_type(checker, lower_for_relation(checker, me), call));
}

void type_typecheck_call(Type const* me, CallExpr const* call, Span span, ErrorCtx* errs) {
    Checker* checker = current_checker;
    panic_if(!checker, "call without a current checker");
    ty_call_typecheck(checker, lower_for_relation(checker, me), call, span, errs);
}
