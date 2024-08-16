#include "type.h"
#include "binding.h"
#include "interface.h"
#include "interface_member.h"
#include "identifier_type.h"
#include "ternary_type.h"
#include "indexed_type.h"
#include "callable_type.h"
#include "generic.h"
#include "value.h"
#include "name_stack.h"
#include "display.h"
#include "error.h"

const Type type_any = { .tag = TYPE_ANY };
const Type type_unknown = { .tag = TYPE_UNKNOWN };
const Type type_self_referential_error = { .tag = TYPE_ANY, .any.self_referential_error = true, };
const Type type_never = { .tag = TYPE_NEVER };
const Type type_void = { .tag = TYPE_VOID };
const Type type_object = { .tag = TYPE_INTERFACE, .interface.members = {0} };
const Type type_boolean = { .tag = TYPE_BOOLEAN };
const Type type_number = { .tag = TYPE_NUMBER };
const Type type_string = { .tag = TYPE_STRING };
const Type type_null = { .tag = TYPE_VALUE, .value.tag = VALUE_NULL };
const Type type_undefined = { .tag = TYPE_VALUE, .value.tag = VALUE_UNDEFINED };

static bool type_extends_internal(
    Type const* sub,
    Type const* super,
    VecTypeRefinement* refinements,
    int level
);
static Type const* type_resolve_internal(Type const* me, int level);

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

Type type_composite(typeof((Type){}.tag) tag, Type a, Type b) {
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
    } else if (a.tag != TYPE_INTERFACE && b.tag != TYPE_INTERFACE && type_extends(&a, &b)) {
        if (tag == TYPE_UNION) {
            type_drop(&a);
            return b;
        } else {
            type_drop(&b);
            return a;
        }
    } else if (a.tag != TYPE_INTERFACE && b.tag != TYPE_INTERFACE && type_extends(&b, &a)) {
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
            vec_type_drop(&my tuple.elements);
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
        case TYPE_CALLABLE: return type_callable(c_new(CallableType, callable_type_clone(*me.callable)));
        case TYPE_INTERFACE: return type_interface(interface_clone(me.interface));
        case TYPE_ARRAY: return type_array(c_new(Type, type_clone(*me.array.element)), me.array.readonly);
        case TYPE_TUPLE: return type_tuple(vec_type_clone(me.tuple.elements), me.tuple.readonly);
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
            if (my identifier->inferred) {
                if (my identifier->inferred_type.get) {
                    NONFATAL("Inferred identifier added multiple times");
                } else {
                    my identifier->inferred_type = arc_named_type_from_ptr(make_named_type(
                        my identifier->name,
                        lazy_type_ref(&my identifier->degenericized),
                        nullptr
                    ));
                    my identifier->type = my identifier->inferred_type.get;
                    name_stack_add_type(names, my identifier->type);
                }
            }
            break;

        case TYPE_CALLABLE:
            c_foreach(i, vec_callable_arg, my callable->args) {
                type_add_inferred_names(&i.ref->binding.explicit_type, names);
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
            c_foreach(i, vec_type, my tuple.elements) {
                type_add_inferred_names(i.ref, names);
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
            break;

        case TYPE_IDENTIFIER: identifier_type_resolve_names(my identifier, names, errs); break;
        case TYPE_CALLABLE: callable_type_resolve_names(my callable, nullptr, names, errs); break;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                interface_member_resolve_names(i.ref, names, errs);
            }
            break;

        case TYPE_ARRAY:
            type_resolve_names(my array.element, names, errs);
            break;

        case TYPE_TUPLE:
            c_foreach(i, vec_type, my tuple.elements) {
                type_resolve_names(i.ref, names, errs);
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
            break;

        case TYPE_IDENTIFIER:
            identifier_type_typecheck(my identifier, errs);
            break;

        case TYPE_CALLABLE:
            callable_type_typecheck(my callable, nullptr, errs);
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
            c_foreach(i, vec_type, my tuple.elements) {
                type_typecheck(i.ref, errs);
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

static Type type_all_keys(Type const* me, int level) {
    me = type_resolve_internal(me, level);

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
                        ret = type_composite(
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
                ret = type_composite(flipped_tag, ret, type_all_keys(i.ref, level));
            }
            return ret;
        }

        case TYPE_REF: return type_all_keys(my ref, level);

        default: return type_never;
    }
}

static Type const* type_resolve_internal(Type const* me, int level) {
    while (true) {
        if (level > 100) {
            NONFATAL("Max type recursion depth exceeded");
            return &type_unknown;
        }
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
                        me = &my identifier->degenericized;
                    } else {
                        me = lazy_type_resolve(&my identifier->type->type);
                    }
                } else {
                    me = &type_unknown;
                }
                break;

            case TYPE_INDEXED:
                NONFATAL("Resolving index types not yet supported");
                me = &type_any;
                break;

            case TYPE_KEYOF:
                if (!my keyof->has_cache) {
                    my keyof->cached = type_all_keys(&my keyof->interface, level);
                    my keyof->has_cache = true;
                }
                me = &my keyof->cached;
                break;

            case TYPE_TERNARY: {
                VecTypeRefinement refinements = {0};
                if (type_extends_internal(&my ternary->maybe_sub, &my ternary->maybe_super, &refinements, level)) {
                    replace_named_types_with(&my ternary->if_true, &refinements);
                    me = &my ternary->if_true;
                } else {
                    me = &my ternary->if_false;
                }
                vec_type_refinement_drop(&refinements);
            }   break;

            case TYPE_UNION:
            case TYPE_INTERSECTION: {
                bool found = false;
                c_foreach(i, vec_type, my composite) {
                    Type const* resolved = type_resolve_internal(i.ref, level + 1);
                    if (resolved->tag == (my tag == TYPE_UNION ? TYPE_ANY : TYPE_NEVER)) {
                        me = resolved;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return me;
                }
                break;
            }

            case TYPE_REF: me = my ref; break;

            default:
                return me;
        }
        level++;
    }
}

Type const* type_resolve(Type const* me) {
    return type_resolve_internal(me, 0);
}

static bool type_extends_internal(Type const* sub, Type const* super, VecTypeRefinement* refinements, int level) {
    if (level > 12) {
        NONFATAL("Max type recursion depth exceeded");
        return true;
    }
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
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TERNARY:
        case TYPE_REF:
            return type_extends_internal(type_resolve_internal(sub, level + 1), super, refinements, level + 1);

        case TYPE_UNION:
        case TYPE_INTERSECTION: // It's actually correct to treat both the same here (I think)
            c_foreach(i, vec_type, sub->composite) {
                if (!type_extends_internal(i.ref, super, refinements, level + 1)) {
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
                            lazy_type_resolve(&i.ref->type),
                            super,
                            refinements,
                            level + 1)
                        ) {
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
                        binding_get_type(&vec_callable_arg_at_mut(&sub->callable->args, i)->binding),
                        binding_get_type(&vec_callable_arg_at_mut(&super->callable->args, i)->binding),
                        refinements,
                        level + 1
                    )) {
                        return false;
                    }
                }
                // Order is not flipped for return
                return type_extends_internal(&sub->callable->ret, &super->callable->ret, refinements, level + 1);
            } else {
                return false;
            }

        case TYPE_INTERFACE:
            if (sub->tag != TYPE_INTERFACE) {
                return false;
            }
            c_foreach(i, vec_interface_member, super->interface.members) {
                if (i.ref->key.tag != INTERFACE_KEY_NAME) {
                    continue; // TODO
                }
                bool found = false;
                c_foreach(j, vec_interface_member, sub->interface.members) {
                    if (j.ref->key.tag != INTERFACE_KEY_NAME) {
                        continue; // TODO
                    }
                    if (cstr_eq(&i.ref->key.name_string, &j.ref->key.name_string)) {
                        if (type_extends_internal(
                            lazy_type_resolve(&j.ref->type),
                            lazy_type_resolve(&i.ref->type),
                            refinements,
                            level + 1)
                        ) {
                            found = true;
                            break;
                        } else {
                            return false;
                        }
                    }
                }
                if (!found) {
                    return false;
                }
            }
            return true;

        case TYPE_ARRAY:
            if (sub->tag == TYPE_ARRAY) {
                if (sub->array.readonly && !super->array.readonly) {
                    return false;
                }
                return type_extends_internal(sub->array.element, super->array.element, refinements, level + 1);
            } else if (sub->tag == TYPE_TUPLE) {
                if (sub->tuple.readonly && !super->array.readonly) {
                    return false;
                }
                c_foreach(i, vec_type, sub->tuple.elements) {
                    if (!type_extends_internal(i.ref, super->array.element, refinements, level + 1)) {
                        return false;
                    }
                }
                return true;
            } else {
                return false;
            }

        case TYPE_TUPLE:
            if (sub->tag != TYPE_TUPLE ||
                vec_type_size(&super->tuple.elements) != vec_type_size(&sub->tuple.elements) ||
                (sub->tuple.readonly && !super->tuple.readonly)
            ) {
                return false;
            }
            for (intptr_t i = 0; i < vec_type_size(&super->tuple.elements); i++) {
                if (!type_extends_internal(
                    vec_type_at(&sub->tuple.elements, i),
                    vec_type_at(&super->tuple.elements, i),
                    refinements,
                    level + 1
                )) {
                    return false;
                }
            }
            return true;

        case TYPE_IDENTIFIER:
            if (refinements && super->identifier->inferred) {
                if (type_extends_internal(sub, &super->identifier->degenericized, nullptr, level + 1)) {
                    vec_type_refinement_push(refinements, (TypeRefinement){
                        .target = super->identifier->type,
                        .refined = sub,
                    });
                    return true;
                } else {
                    return false;
                }
            }
            // fallthrough

        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TERNARY:
        case TYPE_REF:
            return type_extends_internal(sub, type_resolve_internal(super, level + 1), refinements, level + 1);

        case TYPE_UNION:
            c_foreach(i, vec_type, super->composite) {
                if (type_extends_internal(sub, i.ref, refinements, level + 1)) {
                    return true;
                }
            }
            return false;

        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, super->composite) {
                if (!type_extends_internal(sub, i.ref, refinements, level + 1)) {
                    return false;
                }
            }
            return true;
    }
    UNREACHABLE;
}

bool type_extends(Type const* sub, Type const* super) {
    return type_extends_internal(sub, super, nullptr, 0);
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
        Type ret = vec_type_empty(&my tuple.elements) ? type_undefined : type_never;
        c_foreach(i, vec_type, my tuple.elements) {
            if (i.ref->tag != TYPE_VALUE || i.ref->value.tag != VALUE_UNDEFINED) {
                ret = type_composite(TYPE_UNION, ret, *i.ref);
                *i.ref = type_any;
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
            c_foreach(i, vec_type, my tuple.elements) {
                type_abstract_literals(i.ref);
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

static Type index_type_of_internal(
    Type const* me,
    Type const* index,
    Span index_span,
    bool allow_not_found,
    ErrorCtx* errs,
    int level
) {
    if (level > 100) {
        NONFATAL("Max type recursion depth exceeded");
        return type_any;
    }

    index = type_resolve_internal(index, level);
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
            Type ret = type_composite_base(index->tag);
            c_foreach(i, vec_type, index->composite) {
                ret = type_composite(
                    index->tag,
                    ret,
                    index_type_of_internal(me, i.ref, index_span, allow_not_found, errs, level + 1)
                );
            }
            return ret;
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
        return type_any;
    }

    Type const* me_original = me;
    me = type_resolve_internal(me, level);
    if (type_is_self_referential(me)) {
        return type_clone(*me);
    }

    switch (my tag) {
        case TYPE_NEVER:
        case TYPE_VOID:
            return type_never;

        case TYPE_UNKNOWN:
        case TYPE_ANY:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_CALLABLE:
            return type_any;

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
                    return type_any;

                case VALUE_STRING:
                    break;
            }
            // Falls through if string
            // Fallthrough

        case TYPE_STRING:
            if (type_extends(index, &type_number)) {
                return type_string;
            } else {
                return type_any;
            }

        case TYPE_INTERFACE: {
            Type ret = type_composite_base(TYPE_UNION);
            bool found = false;
            c_foreach(i, vec_interface_member, my interface.members) {
                switch (i.ref->key.tag) {
                    case INTERFACE_KEY_NAME: {
                        // Don't clone the string, and don't drop it the type. Hacky but safe.
                        Type key = type_value(value_string(i.ref->key.name_string));
                        if (type_extends(&key, index)) {
                            ret = type_composite(
                                TYPE_UNION,
                                ret,
                                type_clone(*lazy_type_resolve(&i.ref->type))
                            );
                            if (i.ref->optional) {
                                ret = type_composite(TYPE_UNION, ret, type_undefined);
                            }
                            found = true;
                        }
                    }   break;

                    case INTERFACE_KEY_INDEX:
                        if (type_overlaps(&i.ref->key.index, index)) {
                            ret = type_composite(
                                TYPE_UNION,
                                ret,
                                type_clone(*lazy_type_resolve(&i.ref->type))
                            );
                            if (i.ref->optional) {
                                ret = type_composite(TYPE_UNION, ret, type_undefined);
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
            if (!found) {
                type_drop(&ret);
                ret = type_any;
                if (errs && !allow_not_found) {
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
            }
            return ret;
        }

        case TYPE_ARRAY:
            if (type_extends(index, &type_number)) {
                return type_clone(*my array.element);
            } else {
                return type_any;
            }

        case TYPE_TUPLE: {
            if (index->tag == TYPE_VALUE &&
                index->value.tag == VALUE_NUMBER &&
                index->value.number >= vec_type_size(&my tuple.elements) &&
                errs
            ) {
                cstr msg = cstr_lit("Tuple type '");
                cstr_append_type_error_msg(&msg, me);
                cstr_append_fmt(
                    &msg,
                    "' of length '%ld' has no element at index '",
                    vec_type_size(&my tuple.elements)
                );
                cstr_append_value_error_msg(&msg, &index->value);
                cstr_append_sv(&msg, c_sv("'."));
                error_ctx_error_s(errs, index_span, 2493, msg);
            }

            if (!type_extends(index, &type_number)) {
                return type_any;
            }

            Type ret = type_composite_base(TYPE_UNION);
            c_foreach(i, vec_type, my tuple.elements) {
                intptr_t j = i.ref - my tuple.elements.data;
                Type key = type_value(value_number(j));
                if (type_extends(&key, index)) {
                    ret = type_composite(TYPE_UNION, ret, type_clone(*i.ref));
                }
            }
            return ret;
        }

        case TYPE_UNION:
        case TYPE_INTERSECTION: {
            Type ret = type_composite_base(my tag);
            c_foreach(i, vec_type, my composite) {
                ret = type_composite(
                    my tag,
                    ret,
                    index_type_of_internal(i.ref, index, index_span, allow_not_found, errs, level + 1)
                );
            }
            return ret;
        }

        case TYPE_IDENTIFIER:
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TERNARY:
        case TYPE_REF:
            PANIC("Type with tag %d should have been resolved away", my tag);
    }
    UNREACHABLE;
}

Type index_type_of(Type const* me, Type const* index, Span index_span, bool allow_not_found, ErrorCtx* errs) {
    return index_type_of_internal(me, index, index_span, allow_not_found, errs, 0);
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
    if (type_extends(source, target)) {
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
    if (args_size && vec_callable_arg_at(&my args, args_size - 1)->rest) {
        Type const* rest_type = binding_get_type(&vec_callable_arg_at_mut(&my args, args_size - 1)->binding);
        if (rest_type->tag == TYPE_TUPLE && vec_type_size(&rest_type->tuple.elements) > 0) {
            return args_size - 1 + vec_type_size(&rest_type->tuple.elements);
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
    if (args_size && vec_callable_arg_at(&my args, args_size - 1)->rest) {
        Type const* rest_type = binding_get_type(&vec_callable_arg_at_mut(&my args, args_size - 1)->binding);
        if (rest_type->tag == TYPE_TUPLE) {
            return args_size - 1 + vec_type_size(&rest_type->tuple.elements);
        } else {
            return INTPTR_MAX;
        }
    } else {
        return args_size;
    }
}

Type type_typecheck_call(Type const* me, CallExpr const* call, Span span, ErrorCtx* errs, int level) {
    if (level > 100) {
        NONFATAL("Max type recursion depth exceeeded");
        return type_never;
    }

    me = type_resolve_internal(me, level);

    switch (my tag) {
        case TYPE_ANY:
            return type_any;

        case TYPE_CALLABLE: {
            CallableType inferred = {0};
            CallableType* callable = my callable;

            if (call->is_extends) {
                if (!callable->is_constructor) {
                    break;
                }
            } else if (call->uses_new) {
                if (!callable->is_constructor) {
                    // TODO: add this check only in strict mode
                    /*
                    error_ctx_error(
                        errs,
                        span,
                        7009,
                        "'new' expression, whose target lacks a construct signature, implicitly has an 'any' type."
                    );
                    */
                } else if (callable->is_abstract_constructor) {
                    error_ctx_error(errs, span, 2511, "Cannot create an instance of an abstract class.");
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
                vec_callable_arg_at(&callable->args, callable_arg_count - 1)->rest;
            Span rest_span = span_null;
            VecType rest_tuple = {0};

            c_foreach(i, vec_expr, call->args) {
                intptr_t index = i.ref - call->args.data;
                // Don't report errors, errors inside argument expressions are reported elsewhere
                Type arg_type = expr_get_type_and_typecheck(i.ref, nullptr);
                if (check_types_against_callable && index < callable_arg_count - (last_arg_rest ? 1 : 0)) {
                    if (!check_assignable_or_error_internal(
                        "Argument of type",
                        &arg_type,
                        "parameter of type",
                        binding_get_type(&vec_callable_arg_at_mut(&callable->args, index)->binding),
                        2345,
                        i.ref->outer_span,
                        errs
                    )) {
                        check_types_against_callable = false;
                    }
                    type_drop(&arg_type);
                } else if (last_arg_rest) {
                    rest_span = span_between(&rest_span, &i.ref->outer_span);
                    vec_type_push(&rest_tuple, arg_type);
                } else {
                    type_drop(&arg_type);
                }
            }

            if (last_arg_rest) {
                Type rest_type = type_tuple(rest_tuple, false);
                if (check_types_against_callable) {
                    check_assignable_or_error_internal(
                        "Argument of type",
                        &rest_type,
                        "parameter of type",
                        binding_get_type(&vec_callable_arg_at_mut(&callable->args, callable_arg_count - 1)->binding),
                        2345,
                        span_valid(&rest_span) ? rest_span : span,
                        errs
                    );
                }
                type_drop(&rest_type);
            }

            Type ret = (call->uses_new && callable->is_abstract_constructor) ? type_any : type_clone(callable->ret);
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
                ret = type_composite(my tag, ret, type_typecheck_call(i.ref, call, span, errs, level));
            }
            return ret;
        }

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                // TODO: add to overload list instead of selecting first option
                if (i.ref->key.tag == INTERFACE_KEY_CALLABLE) {
                    return type_typecheck_call(lazy_type_resolve(&i.ref->type), call, span, errs, level);
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
