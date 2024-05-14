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
#include "error.h"

const Type type_any = { .tag = TYPE_ANY };
const Type type_unknown = { .tag = TYPE_UNKNOWN };
const Type type_never = { .tag = TYPE_NEVER };
const Type type_void = { .tag = TYPE_VOID };
const Type type_object = { .tag = TYPE_INTERFACE, .interface.members = {0} };
const Type type_boolean = { .tag = TYPE_BOOLEAN };
const Type type_number = { .tag = TYPE_NUMBER };
const Type type_string = { .tag = TYPE_STRING };
const Type type_null = { .tag = TYPE_VALUE, .value.tag = VALUE_NULL };
const Type type_undefined = { .tag = TYPE_VALUE, .value.tag = VALUE_UNDEFINED };

static bool type_extends_internal(Type const* sub, Type const* super, VecTypeRefinement* refinements, int level);
static Type const* type_resolve_internal(Type const* me, int level);

Type type_composite_without_simplify(Type a, Type b, typeof((Type){}.tag) tag) {
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

Type type_composite(Type a, Type b, typeof((Type){}.tag) tag) {
    ASSERT(tag == TYPE_UNION || tag == TYPE_INTERSECTION);
    if (a.tag == TYPE_NEVER || a.tag == TYPE_ANY || b.tag == TYPE_NEVER || b.tag == TYPE_ANY) {
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
        return type_composite_without_simplify(a, b, tag);
    }
}

void composite_type_simplify(Type* me) {
    ASSERT(my tag == TYPE_UNION || my tag == TYPE_INTERSECTION);
    Type ret = my tag == TYPE_UNION ? type_never : type_any;
    c_foreach(i, vec_type, my composite) {
        ret = type_composite(ret, *i.ref, my tag);
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
            type_drop(my array);
            free(my array);
            break;

        case TYPE_TUPLE:
            vec_type_drop(&my tuple);
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
        case TYPE_STRING: return me;
        case TYPE_VALUE: return type_value(value_clone(me.value));
        case TYPE_IDENTIFIER: return type_identifier(c_new(IdentifierType, identifier_type_clone(*me.identifier)));
        case TYPE_CALLABLE: return type_callable(c_new(CallableType, callable_type_clone(*me.callable)));
        case TYPE_INTERFACE: return type_interface(interface_clone(me.interface));
        case TYPE_ARRAY: return type_array(c_new(Type, type_clone(*me.array)));
        case TYPE_TUPLE: return type_tuple(vec_type_clone(me.tuple));
        case TYPE_INDEXED: return type_indexed(c_new(IndexedType, indexed_type_clone(*me.indexed)));
        case TYPE_KEYOF: return type_keyof(c_new(KeyofType, keyof_type_clone(*me.keyof)));
        case TYPE_TERNARY: return type_ternary(c_new(TernaryType, ternary_type_clone(*me.ternary)));
        case TYPE_UNION: case TYPE_INTERSECTION: return (Type){.tag = me.tag, .composite = vec_type_clone(me.composite)};
    }
    UNREACHABLE;
}

void cstr_append_type_debug(cstr* target, Type const* me, int indent) {
    switch (my tag) {
        case TYPE_ANY: cstr_append_sv(target, c_sv(CLR_TYPE "any" CLR0)); break;
        case TYPE_UNKNOWN: cstr_append_sv(target, c_sv(CLR_TYPE "unknown" CLR0)); break;
        case TYPE_NEVER: cstr_append_sv(target, c_sv(CLR_TYPE "never" CLR0)); break;
        case TYPE_VOID: cstr_append_sv(target, c_sv(CLR_TYPE "void" CLR0)); break;
        case TYPE_BOOLEAN: cstr_append_sv(target, c_sv(CLR_TYPE "boolean" CLR0)); break;
        case TYPE_NUMBER: cstr_append_sv(target, c_sv(CLR_TYPE "number" CLR0)); break;
        case TYPE_STRING: cstr_append_sv(target, c_sv(CLR_TYPE "string" CLR0)); break;
        case TYPE_VALUE: cstr_append_value_debug(target, &my value, indent); break;
        case TYPE_IDENTIFIER: cstr_append_identifier_type_debug(target, my identifier, indent); break;
        case TYPE_CALLABLE: cstr_append_callable_type_debug(target, my callable, indent); break;
        case TYPE_INTERFACE: cstr_append_interface_debug(target, &my interface, indent); break;

        case TYPE_ARRAY:
            cstr_append_type_debug(target, my array, indent);
            cstr_append_sv(target, c_sv(CLR_TYPE "[]" CLR0));
            break;

        case TYPE_TUPLE:
            cstr_append_sv(target, c_sv("["));
            c_foreach(i, vec_type, my tuple) {
                cstr_append_type_debug(target, i.ref, indent);
                if (i.ref != i.end - 1) {
                    cstr_append_sv(target, c_sv(", "));
                }
            }
            cstr_append_sv(target, c_sv("]"));
            break;

        case TYPE_INDEXED: cstr_append_indexed_type_debug(target, my indexed, indent); break;
        case TYPE_KEYOF: cstr_append_keyof_type_debug(target, my keyof, indent); break;
        case TYPE_TERNARY: cstr_append_ternary_type_debug(target, my ternary, indent); break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            cstr_append_sv(target, c_sv(CLR_IMPLICIT "(" CLR0));
            c_foreach(i, vec_type, my composite) {
                cstr_append_type_debug(target, i.ref, indent);
                if (i.ref != i.end - 1) {
                    cstr_append_sv(target, my tag == TYPE_UNION ? c_sv(" | ") : c_sv(" & "));
                }
            }
            cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
            break;
    }
}

void cstr_append_type_error_msg(cstr* target, Type const* me) {
    switch (my tag) {
        case TYPE_ANY: cstr_append_sv(target, c_sv("any")); break;
        case TYPE_UNKNOWN: cstr_append_sv(target, c_sv("unknown")); break;
        case TYPE_NEVER: cstr_append_sv(target, c_sv("never")); break;
        case TYPE_VOID: cstr_append_sv(target, c_sv("void")); break;
        case TYPE_BOOLEAN: cstr_append_sv(target, c_sv("boolean")); break;
        case TYPE_NUMBER: cstr_append_sv(target, c_sv("number")); break;
        case TYPE_STRING: cstr_append_sv(target, c_sv("string")); break;
        case TYPE_VALUE: cstr_append_value_error_msg(target, &my value); break;
        case TYPE_IDENTIFIER: cstr_append_identifier_type_error_msg(target, my identifier); break;
        case TYPE_CALLABLE: cstr_append_callable_type_error_msg(target, my callable); break;
        case TYPE_INTERFACE: cstr_append_interface_error_msg(target, &my interface); break;

        case TYPE_ARRAY: {
            bool needs_peren = my array->tag == TYPE_UNION || my array->tag == TYPE_INTERSECTION;
            if (needs_peren) {
                cstr_append_sv(target, c_sv("("));
            }
            cstr_append_type_error_msg(target, my array);
            if (needs_peren) {
                cstr_append_sv(target, c_sv(")"));
            }
            cstr_append_sv(target, c_sv("[]"));
        }   break;

        case TYPE_TUPLE:
            cstr_append_sv(target, c_sv("["));
            c_foreach(i, vec_type, my tuple) {
                cstr_append_type_error_msg(target, i.ref);
                if (i.ref != i.end - 1) {
                    cstr_append_sv(target, c_sv(", "));
                }
            }
            cstr_append_sv(target, c_sv("]"));
            break;

        case TYPE_INDEXED: cstr_append_indexed_type_error_msg(target, my indexed); break;
        case TYPE_KEYOF: cstr_append_keyof_type_error_msg(target, my keyof); break;
        case TYPE_TERNARY: cstr_append_ternary_type_error_msg(target, my ternary); break;

        case TYPE_UNION:
        case TYPE_INTERSECTION: {
            bool first = true;
            c_foreach(i, vec_type, my composite) {
                if (i.ref->tag != (my tag == TYPE_UNION ? TYPE_NEVER : TYPE_ANY)) {
                    if (!first) {
                        cstr_append_sv(target, my tag == TYPE_UNION ? c_sv(" | ") : c_sv(" & "));
                    }
                    first = false;
                    cstr_append_type_error_msg(target, i.ref);
                }
            }
        }   break;
    }
}

Generic* type_get_generic(Type const* me) {
    switch (my tag) {
        case TYPE_CALLABLE:
            if (!generic_empty(&my callable->generic)) {
                return &my callable->generic;
            }
            break;

        default: break;
    }
    return nullptr;
}

static void type_add_inferred_names(Type* me, NameStack* names) {
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
            c_foreach(i, vec_type, my identifier->generic_params) {
                type_add_inferred_names(i.ref, names);
            }
            if (my identifier->inferred) {
                name_stack_add_type(
                    names,
                    cstr_clone(my identifier->name),
                    my identifier->span,
                    &my identifier->degenericized,
                    nullptr,
                    false
                );
                ArcNamedType const* result = name_stack_get_type(names, cstr_sv(&my identifier->name));
                ASSERT(result);
                if (my identifier->type.get) {
                    NONFATAL("Inferred identifier added multiple times");
                    arc_named_type_drop(&my identifier->type);
                }
                my identifier->type = arc_named_type_clone(*result);
            }
            break;

        case TYPE_CALLABLE:
            c_foreach(i, vec_callable_arg, my callable->args) {
                type_add_inferred_names(&i.ref->binding.type, names);
            }
            type_add_inferred_names(&my callable->ret, names);
            break;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                type_add_inferred_names(&i.ref->type, names);
            }
            break;

        case TYPE_ARRAY:
            type_add_inferred_names(my array, names);
            break;

        case TYPE_TUPLE:
            c_foreach(i, vec_type, my tuple) {
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
    }
}

void type_resolve_names(Type* me, NameStack* names, ErrorCtx* errs) {
    Generic* generic = type_get_generic(me);
    if (generic) {
        name_stack_push_generic_frame(names, generic, errs);
    }

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
        case TYPE_CALLABLE: callable_type_resolve_names(my callable, names, errs); break;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                interface_member_resolve_names(i.ref, names, errs);
            }
            break;

        case TYPE_ARRAY:
            type_resolve_names(my array, names, errs);
            break;

        case TYPE_TUPLE:
            c_foreach(i, vec_type, my tuple) {
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
    }

    if (generic) {
        name_stack_pop_frame(names);
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
                            ret,
                            type_value(value_string(cstr_clone(i.ref->key.name))),
                            TYPE_UNION
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
            Type ret = flipped_tag == TYPE_UNION ? type_never : type_any;
            c_foreach(i, vec_type, my composite) {
                ret = type_composite(ret, type_all_keys(i.ref, level), flipped_tag);
            }
            return ret;
        }

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
                if (my identifier->type.get) {
                    if (vec_type_empty(&my identifier->generic_params)) {
                        me = my identifier->type.get->type;
                    } else {
                        if (!my identifier->degenericized.tag && my identifier->type.get->generic) {
                            my identifier->degenericized = generic_degenericize_type(
                                my identifier->type.get->generic,
                                type_clone(*my identifier->type.get->type),
                                &my identifier->generic_params
                            );
                        }
                        me = &my identifier->degenericized;
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
                    if (i.ref->tag == (my tag == TYPE_UNION ? TYPE_ANY : TYPE_NEVER)) {
                        me = i.ref;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return me;
                }
                break;
            }

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
            if (sub->tag != TYPE_CALLABLE) {
                return false;
            }
            long arg_count = vec_callable_arg_size(&super->callable->args);
            if (vec_callable_arg_size(&sub->callable->args) != arg_count) {
                return false;
            }
            for (intptr_t i = 0; i < arg_count; i++) {
                // Order is flipped for arguments
                if (!type_extends_internal(
                    &vec_callable_arg_at(&sub->callable->args, i)->binding.type,
                    &vec_callable_arg_at(&super->callable->args, i)->binding.type,
                    refinements,
                    level + 1
                )) {
                    return false;
                }
            }
            // Order is not flipped for return
            return type_extends_internal(&sub->callable->ret, &super->callable->ret, refinements, level + 1);

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
                    if (cstr_eq(&i.ref->key.name, &j.ref->key.name)) {
                        if (type_extends_internal(&j.ref->type, &i.ref->type, refinements, level + 1)) {
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
                return type_extends_internal(sub->array, super->array, refinements, level + 1);
            } else if (sub->tag == TYPE_TUPLE) {
                c_foreach(i, vec_type, sub->tuple) {
                    if (!type_extends_internal(i.ref, super->array, refinements, level + 1)) {
                        return false;
                    }
                }
                return true;
            } else {
                return false;
            }

        case TYPE_TUPLE:
            if (sub->tag != TYPE_TUPLE || vec_type_size(&super->tuple) != vec_type_size(&sub->tuple)) {
                return false;
            }
            for (intptr_t i = 0; i < vec_type_size(&super->tuple); i++) {
                if (!type_extends_internal(
                    vec_type_at(&sub->tuple, i),
                    vec_type_at(&super->tuple, i),
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
                        .target = super->identifier->type.get,
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

bool type_extends_array(Type const* sub) {
    static Type array_any = {
        .tag = TYPE_ARRAY,
        .array = (Type*)&type_any,
    };
    return type_extends(sub, &array_any);
}

void type_abstract_tuple_to_array(Type* me) {
    if (my tag == TYPE_TUPLE) {
        type_abstract_literals(me);
        Type ret = vec_type_empty(&my tuple) ? type_any : type_never;
        c_foreach(i, vec_type, my tuple) {
            if (i.ref->tag != TYPE_VALUE || i.ref->value.tag != VALUE_UNDEFINED) {
                ret = type_composite(ret, *i.ref, TYPE_UNION);
                *i.ref = type_any;
            }
        }
        type_drop(me);
        if (ret.tag == TYPE_NEVER) {
            ret = type_undefined;
        }
        *me = type_array(c_new(Type, ret));
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

                case VALUE_OBJECT: {
                    Interface interface;
                    c_foreach(i, hmap_str_object_item, my value.object.get->items) {
                        InterfaceMember member = interface_member_make(
                            span_null,
                            false,
                            cstr_clone(i.ref->first),
                            span_null,
                            type_value(value_clone(i.ref->second.value))
                        );
                        type_abstract_literals(&member.type);
                        vec_interface_member_push(&interface.members, member);
                    }
                    type_drop(me);
                    *me = type_interface(interface);
                }   break;

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
            c_foreach(i, vec_type, my tuple) {
                type_abstract_literals(i.ref);
            }
        }   break;
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

Type array_type_of(Type value) {
    Type ret = type_any;
    switch (value.tag) {
        case TYPE_VALUE:
            if (value.value.tag == VALUE_STRING) {
                ret = type_string;
            }
            break;

        case TYPE_STRING:
            ret = type_string;
            break;

        case TYPE_ARRAY:
            ret = *value.array;
            *value.array = type_any;
            break;

        case TYPE_TUPLE:
            ret = type_never;
            c_foreach(i, vec_type, value.tuple) {
                ret = type_composite(ret, *i.ref, TYPE_UNION);
                *i.ref = type_any;
            }
            break;

        default: break;
    }
    type_drop(&value);
    return ret;
}

Type property_type_of(Type value, Type prop, Span prop_span, ErrorCtx* errs) {
    if (type_extends(&prop, &type_number)) {
        Type ret = type_any;
        if (value.tag == TYPE_TUPLE && prop.tag == TYPE_VALUE) {
            if (value_is_int(&prop.value) &&
                prop.value.number >= 0 &&
                (long)prop.value.number < vec_type_size(&value.tuple)
            ) {
                ret = *vec_type_at(&value.tuple, (long)prop.value.number);
                *vec_type_at_mut(&value.tuple, (long)prop.value.number) = type_any;
            } else {
                cstr msg = cstr_lit("Tuple type '");
                cstr_append_type_error_msg(&msg, &value);
                cstr_append_fmt(
                    &msg,
                    "' of length '%ld' has no element at index '",
                    vec_type_size(&value.tuple)
                );
                cstr_append_value_error_msg(&msg, &prop.value);
                cstr_append_sv(&msg, c_sv("'."));
                error_ctx_error_s(errs, prop_span, 2493, msg);
            }
        } else {
            ret = array_type_of(value);
            value = type_any;
        }
        type_drop(&value);
        type_drop(&prop);
        return ret;
    } else if (type_extends(&prop, &type_string)) {
        // TODO
        type_drop(&value);
        type_drop(&prop);
        return type_any;
    } else {
        // TODO: support number | string as index type
        // TODO: refine type in error message, only report on possible subtypes that can't be used as index types
        cstr msg = cstr_lit("Type '");
        cstr_append_type_error_msg(&msg, &prop);
        cstr_append_sv(&msg, c_sv("' cannot be used as an index type."));
        error_ctx_error_s(errs, prop_span, 2538, msg);
        type_drop(&value);
        type_drop(&prop);
        return type_any;
    }
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

static intptr_t min_args_of_callable(CallableType const* me) {
    intptr_t args_size = vec_callable_arg_size(&my args);
    intptr_t ret = args_size;
    if (args_size && vec_callable_arg_at(&my args, args_size - 1)->rest) {
        Type const* rest_type = &vec_callable_arg_at(&my args, args_size - 1)->binding.type;
        if (rest_type->tag == TYPE_TUPLE && vec_type_size(&rest_type->tuple) > 0) {
            return args_size - 1 + vec_type_size(&rest_type->tuple);
        } else {
            ret--;
        }
    }
    while (ret && vec_callable_arg_at(&my args, ret - 1)->optional) {
        ret--;
    }
    return ret;
}

static intptr_t max_args_of_callable(CallableType const* me) {
    intptr_t args_size = vec_callable_arg_size(&my args);
    if (args_size && vec_callable_arg_at(&my args, args_size - 1)->rest) {
        Type const* rest_type = &vec_callable_arg_at(&my args, args_size - 1)->binding.type;
        if (rest_type->tag == TYPE_TUPLE) {
            return args_size - 1 + vec_type_size(&rest_type->tuple);
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
            CallableType const* callable = my callable;

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

                Span span = call->callable.span;
                if (call_arg_count > max_args) {
                    span = span_between(
                        &vec_expr_at(&call->args, max_args)->span,
                        &vec_expr_at(&call->args, call_arg_count - 1)->span
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
                Type arg_type = expr_get_type_and_typecheck(i.ref, errs);
                if (check_types_against_callable && index < callable_arg_count - (last_arg_rest ? 1 : 0)) {
                    if (!check_assignable_or_error_internal(
                        "Argument of type",
                        &arg_type,
                        "parameter of type",
                        &vec_callable_arg_at(&callable->args, index)->binding.type,
                        2345,
                        i.ref->span,
                        errs
                    )) {
                        check_types_against_callable = false;
                    }
                    type_drop(&arg_type);
                } else if (last_arg_rest) {
                    rest_span = span_between(&rest_span, &i.ref->span);
                    vec_type_push(&rest_tuple, arg_type);
                } else {
                    type_drop(&arg_type);
                }
            }

            if (last_arg_rest) {
                Type rest_type = type_tuple(rest_tuple);
                if (check_types_against_callable) {
                    check_assignable_or_error_internal(
                        "Argument of type",
                        &rest_type,
                        "parameter of type",
                        &vec_callable_arg_at(&callable->args, callable_arg_count - 1)->binding.type,
                        2345,
                        span_valid(&rest_span) ? rest_span : span,
                        errs
                    );
                }
                type_drop(&rest_type);
            }

            Type ret = type_clone(callable->ret);
            callable_type_drop(&inferred);
            return ret;
        }

        case TYPE_IDENTIFIER:
        case TYPE_INDEXED:
        case TYPE_TERNARY:
            UNREACHABLE; // Should be handled by resolve

        case TYPE_UNION:
        case TYPE_INTERSECTION: {
            Type ret = my tag == TYPE_UNION ? type_never : type_any;
            c_foreach(i, vec_type, my composite) {
                ret = type_composite(ret, type_typecheck_call(i.ref, call, span, errs, level), my tag);
            }
            return ret;
        }

        default:
            error_ctx_error(errs, call->callable.span, 2349, "This expression is not callable.");
            return type_any;
    }
}
