#include "type.h"
#include "interface.h"
#include "interface_member.h"
#include "ternary_type.h"
#include "callable_type.h"
#include "generic.h"
#include "extends_conditional.h"
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
    } else if (type_extends(&a, &b)) {
        if (tag == TYPE_UNION) {
            type_drop(&a);
            return b;
        } else {
            type_drop(&b);
            return a;
        }
    } else if (type_extends(&b, &a)) {
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
            cstr_drop(&my identifier.name);
            arc_named_type_drop(&my identifier.type);
            vec_type_drop(&my identifier.generic_params);
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

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            vec_type_drop(&my composite);
            break;

        case TYPE_TERNARY:
            ternary_type_drop(my ternary);
            free(my ternary);
            break;
    }
    my tag = TYPE_ANY; // Makes repeated calls to drop safe
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

        case TYPE_IDENTIFIER:
            cstr_append_sv(target, c_sv(CLR_TYPE));
            cstr_append_s(target, my identifier.name);
            cstr_append_sv(target, c_sv(CLR0));
            if (!vec_type_empty(&my identifier.generic_params)) {
                cstr_append_sv(target, c_sv("<"));
                c_foreach(i, vec_type, my identifier.generic_params) {
                    cstr_append_type_debug(target, i.ref, indent);
                    if (i.ref != i.end - 1) {
                        cstr_append_sv(target, c_sv(", "));
                    }
                }
                cstr_append_sv(target, c_sv(">"));
            }
            break;

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

        case TYPE_TERNARY: cstr_append_ternary_type_debug(target, my ternary, indent); break;
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

        case TYPE_IDENTIFIER:
            cstr_append_s(target, my identifier.name);
            if (!vec_type_empty(&my identifier.generic_params)) {
                cstr_append_sv(target, c_sv("<"));
                c_foreach(i, vec_type, my identifier.generic_params) {
                    cstr_append_type_error_msg(target, i.ref);
                    if (i.ref != i.end - 1) {
                        cstr_append_sv(target, c_sv(", "));
                    }
                }
                cstr_append_sv(target, c_sv(">"));
            }
            break;

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

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                cstr_append_type_error_msg(target, i.ref);
                if (i.ref != i.end - 1) {
                    cstr_append_sv(target, my tag == TYPE_UNION ? c_sv(" | ") : c_sv(" & "));
                }
            }
            break;

        case TYPE_TERNARY: cstr_append_ternary_type_error_msg(target, my ternary); break;
    }
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
        case TYPE_IDENTIFIER: return type_identifier_full(
            cstr_clone(me.identifier.name),
            me.identifier.span,
            arc_named_type_clone(me.identifier.type),
            me.identifier.generic_depth
        );
        case TYPE_CALLABLE: return type_callable(c_new(CallableType, callable_type_clone(*me.callable)));
        case TYPE_INTERFACE: return type_interface(interface_clone(me.interface));
        case TYPE_ARRAY: return type_array(c_new(Type, type_clone(*me.array)));
        case TYPE_TUPLE: return type_tuple(vec_type_clone(me.tuple));
        case TYPE_UNION: case TYPE_INTERSECTION: return (Type){.tag = me.tag, .composite = vec_type_clone(me.composite)};
        case TYPE_TERNARY: return type_ternary(c_new(TernaryType, ternary_type_clone(*me.ternary)));
    }
    UNREACHABLE;
}

Type type_move(Type* me) {
    Type ret = *me;
    my tag = TYPE_ANY;
    return ret;
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

        case TYPE_IDENTIFIER: {
            int generic_depth;
            ArcNamedType const* result = name_stack_get_type(names, cstr_sv(&my identifier.name), &generic_depth);
            if (!result) {
                error_ctx_error(
                    errs,
                    my identifier.span,
                    2304,
                    "Cannot find name '%s'.",
                    cstr_str(&my identifier.name)
                );
                result = name_stack_get_type(names, c_sv("any"), &generic_depth);
            }
            if (result) {
                my identifier.type = arc_named_type_clone(*result);
                my identifier.generic_depth = result->get->is_generic ? generic_depth : 0;
            }
            c_foreach(i, vec_type, my identifier.generic_params) {
                type_resolve_names(i.ref, names, errs);
            }
        }   break;

        case TYPE_CALLABLE:
            callable_type_resolve_names(my callable, names, errs);
            break;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                type_resolve_names(&i.ref->type, names, errs);
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

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                type_resolve_names(i.ref, names, errs);
            }
            break;

        case TYPE_TERNARY:
            type_resolve_names(&my ternary->conditional.MaybeSub, names, errs);
            type_resolve_names(&my ternary->conditional.MaybeSuper, names, errs);
            type_resolve_names(&my ternary->TrueType, names, errs);
            type_resolve_names(&my ternary->FalseType, names, errs);
            break;
    }

    if (generic) {
        name_stack_pop_frame(names);
    }
}

static bool type_extends_internal(Type const* sub, Type const* super, int level) {
    if (level > 100) {
        NONFATAL("max type recursion depth exceeded");
        return false;
    }
    switch (sub->tag) {
        case TYPE_ANY: return true;

        case TYPE_IDENTIFIER:
            if (sub->identifier.type.get) {
                while (super->tag == TYPE_IDENTIFIER && super->identifier.type.get) {
                    if (sub->identifier.type.get == super->identifier.type.get) {
                        return true;
                    }
                    super = super->identifier.type.get->type;
                }
                return type_extends_internal(sub->identifier.type.get->type, super, level + 1);
            }
            return false;

        case TYPE_TERNARY: return type_extends_internal(ternary_type_resolve(sub->ternary), super, level);

        case TYPE_UNION:
        case TYPE_INTERSECTION: // It's actually correct to treat both the same here (I think)
            c_foreach(i, vec_type, sub->composite) {
                if (!type_extends_internal(i.ref, super, level)) {
                    return false;
                }
            }
            return true;

        default: break;
    }

    switch (super->tag) {
        case TYPE_ANY:
            return true;

        case TYPE_UNKNOWN:
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

        case TYPE_IDENTIFIER:
            if (super->identifier.type.get) {
                return type_extends_internal(sub, super->identifier.type.get->type, level + 1);
            }
            return false;

        case TYPE_CALLABLE:
            if (sub->tag != TYPE_CALLABLE) {
                return false;
            }
            long arg_count = vec_callable_type_arg_size(&super->callable->args);
            if (vec_callable_type_arg_size(&sub->callable->args) != arg_count) {
                return false;
            }
            for (int i = 0; i < arg_count; i++) {
                // Order is flipped for arguments
                if (!type_extends_internal(
                    &vec_callable_type_arg_at(&super->callable->args, i)->type,
                    &vec_callable_type_arg_at(&sub->callable->args, i)->type,
                    level
                )) {
                    return false;
                }
            }
            // Order is not flipped for return
            return type_extends_internal(&sub->callable->ret, &super->callable->ret, level);

        case TYPE_ARRAY:
            return sub->tag == TYPE_ARRAY && type_extends_internal(sub->array, super->array, level);

        case TYPE_TUPLE:
            if (sub->tag != TYPE_TUPLE || vec_type_size(&super->tuple) != vec_type_size(&sub->tuple)) {
                return false;
            }
            for (intptr_t i = 0; i < vec_type_size(&super->tuple); i++) {
                if (!type_extends_internal(vec_type_at(&sub->tuple, i), vec_type_at(&super->tuple, i), level)) {
                    return false;
                }
            }
            return true;

        case TYPE_UNION:
            c_foreach(i, vec_type, super->composite) {
                if (type_extends_internal(sub, i.ref, level)) {
                    return true;
                }
            }
            return false;

        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, super->composite) {
                if (!type_extends_internal(sub, i.ref, level)) {
                    return false;
                }
            }
            return true;

        case TYPE_INTERFACE:
            NONFATAL("extends(TYPE_INTERFACE, ...) not implemented");
            return false;

        case TYPE_TERNARY: return type_extends_internal(sub, ternary_type_resolve(super->ternary), level);
    }
    UNREACHABLE;
}

bool type_extends(Type const* sub, Type const* super) {
    return type_extends_internal(sub, super, 0);
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
        case TYPE_UNION:
        case TYPE_INTERSECTION:
        case TYPE_TERNARY:
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

void type_simplify_internal(Type* me, int level) {
    if (level > 2) {
        NONFATAL("max type recursion depth exceeded");
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
        case TYPE_VALUE:
            break;

        case TYPE_IDENTIFIER:
            if (my identifier.type.get && !my identifier.generic_depth) {
                Type tmp = type_clone(*my identifier.type.get->type);
                type_drop(me);
                *me = tmp;
                type_simplify_internal(me, level + 1);
            }
            break;

        case TYPE_CALLABLE:
            c_foreach(i, vec_generic_item, my callable->generic.items) {
                type_simplify_internal(&i.ref->extends, level);
                type_simplify_internal(&i.ref->fallback, level);
            }
            c_foreach(i, vec_callable_type_arg, my callable->args) {
                type_simplify_internal(&i.ref->type, level);
            }
            type_simplify_internal(&my callable->ret, level);
            break;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                type_simplify_internal(&i.ref->type, level + 1);
            }
            break;

        case TYPE_ARRAY:
            type_simplify_internal(my array, level);
            break;

        case TYPE_TUPLE:
            c_foreach(i, vec_type, my tuple) {
                type_simplify_internal(i.ref, level);
            }
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                type_simplify_internal(i.ref, level);
            }
            break;

        case TYPE_TERNARY: {
            Type tmp = type_clone(*ternary_type_resolve(my ternary));
            type_drop(me);
            *me = tmp;
            type_simplify_internal(me, level);
        }   break;
    }
}

void type_simplify(Type* me) {
    type_simplify_internal(me, 0);
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

void check_assignable_or_error(Type const* source, Type const* target, Span span, ErrorCtx* errs) {
    if (type_extends(source, target)) {
        return;
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
        2322,
        "Type '%s' is not assignable to type '%s'.",
        cstr_str(&source_str),
        cstr_str(&target_str)
    );
    c_drop(cstr, &source_str, &target_str);
}
