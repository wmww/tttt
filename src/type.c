#include "type.h"
#include "interface.h"
#include "interface_member.h"
#include "ternary_type.h"
#include "callable_type.h"
#include "generic.h"
#include "extends_conditional.h"
#include "value.h"
#include "name_stack.h"

const Type type_any = { .tag = TYPE_ANY };
const Type type_unkown = { .tag = TYPE_UNKOWN };
const Type type_never = { .tag = TYPE_NEVER };
const Type type_void = { .tag = TYPE_VOID };
const Type type_boolean = { .tag = TYPE_BOOLEAN };
const Type type_number = { .tag = TYPE_NUMBER };
const Type type_string = { .tag = TYPE_STRING };

void type_drop(Type* self) {
    switch (self->tag) {
        case TYPE_ANY:
        case TYPE_UNKOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
            break;

        case TYPE_VALUE:
            value_drop(&self->value);
            break;

        case TYPE_IDENTIFIER:
            cstr_drop(&self->identifier.name);
            break;

        case TYPE_GENERIC:
            generic_drop(self->generic);
            free(self->generic);
            break;

        case TYPE_CALLABLE:
            callable_type_drop(self->callable);
            free(self->callable);
            break;

        case TYPE_INTERFACE:
            interface_drop(&self->interface);
            break;

        case TYPE_ARRAY:
            type_drop(self->array);
            free(self->array);
            break;

        case TYPE_TERNARY:
            ternary_type_drop(self->ternary);
            free(self->ternary);
            break;
    }
    self->tag = TYPE_ANY; // Makes repeated calls to drop safe
}

void cstr_append_type_debug(cstr* target, Type const* self, int indent) {
    switch (self->tag) {
        case TYPE_ANY: cstr_append_sv(target, c_sv(CLR_TYPE "any" CLR0)); break;
        case TYPE_UNKOWN: cstr_append_sv(target, c_sv(CLR_TYPE "unkown" CLR0)); break;
        case TYPE_NEVER: cstr_append_sv(target, c_sv(CLR_TYPE "never" CLR0)); break;
        case TYPE_VOID: cstr_append_sv(target, c_sv(CLR_TYPE "void" CLR0)); break;
        case TYPE_BOOLEAN: cstr_append_sv(target, c_sv(CLR_TYPE "boolean" CLR0)); break;
        case TYPE_NUMBER: cstr_append_sv(target, c_sv(CLR_TYPE "number" CLR0)); break;
        case TYPE_STRING: cstr_append_sv(target, c_sv(CLR_TYPE "string" CLR0)); break;
        case TYPE_VALUE: cstr_append_value_debug(target, &self->value, indent); break;
        case TYPE_IDENTIFIER:
            cstr_append_sv(target, c_sv(CLR_TYPE));
            cstr_append_s(target, self->identifier.name);
            cstr_append_sv(target, c_sv(CLR0));
            break;
        case TYPE_GENERIC: cstr_append_generic_debug(target, self->generic, indent); break;
        case TYPE_CALLABLE: cstr_append_callable_type_debug(target, self->callable, indent); break;
        case TYPE_INTERFACE: cstr_append_interface_debug(target, &self->interface, indent); break;
        case TYPE_ARRAY:
            cstr_append_type_debug(target, self->array, indent);
            cstr_append_sv(target, c_sv(CLR_TYPE "[]" CLR0));
            break;
        case TYPE_TERNARY: cstr_append_ternary_type_debug(target, self->ternary, indent); break;
    }
}

void cstr_append_type_error_msg(cstr* target, Type const* self) {
    switch (self->tag) {
        case TYPE_ANY: cstr_append_sv(target, c_sv("any")); break;
        case TYPE_UNKOWN: cstr_append_sv(target, c_sv("unkown")); break;
        case TYPE_NEVER: cstr_append_sv(target, c_sv("never")); break;
        case TYPE_VOID: cstr_append_sv(target, c_sv("void")); break;
        case TYPE_BOOLEAN: cstr_append_sv(target, c_sv("boolean")); break;
        case TYPE_NUMBER: cstr_append_sv(target, c_sv("number")); break;
        case TYPE_STRING: cstr_append_sv(target, c_sv("string")); break;
        case TYPE_VALUE: cstr_append_value_js(target, &self->value); break;
        case TYPE_IDENTIFIER: cstr_append_s(target, self->identifier.name); break;
        case TYPE_GENERIC: cstr_append_generic_error_msg(target, self->generic); break;
        case TYPE_CALLABLE: cstr_append_callable_type_error_msg(target, self->callable); break;
        case TYPE_INTERFACE: cstr_append_interface_error_msg(target, &self->interface); break;
        case TYPE_ARRAY:
            cstr_append_type_error_msg(target, self->array);
            cstr_append_sv(target, c_sv("[]"));
            break;
        case TYPE_TERNARY: cstr_append_ternary_type_error_msg(target, self->ternary); break;
    }
}

Type type_clone(Type self) {
    switch (self.tag) {
        case TYPE_ANY:
        case TYPE_UNKOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING: return self;
        case TYPE_VALUE: return type_value(value_clone(self.value));
        case TYPE_IDENTIFIER: return type_identifier(
            cstr_clone(self.identifier.name),
            self.identifier.type,
            self.identifier.generic_depth,
            self.identifier.generic_index,
            self.identifier.span
        );
        case TYPE_GENERIC: return type_generic(c_new(Generic, generic_clone(*self.generic)));
        case TYPE_CALLABLE: return type_callable(c_new(CallableType, callable_type_clone(*self.callable)));
        case TYPE_INTERFACE: return type_interface(interface_clone(self.interface));
        case TYPE_ARRAY: return type_array(c_new(Type, type_clone(*self.array)));
        case TYPE_TERNARY: return type_ternary(c_new(TernaryType, ternary_type_clone(*self.ternary)));
    }
    UNREACHABLE;
}

Type type_move(Type* self) {
    Type ret = *self;
    self->tag = TYPE_ANY;
    return ret;
}

void type_resolve_names(Type* self, NameStack* names, ErrorCtx* errs) {
    switch (self->tag) {
        case TYPE_ANY:
        case TYPE_UNKOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_VALUE:
            break;

        case TYPE_IDENTIFIER: {
            int generic_depth;
            NamedType const* result = name_stack_get_type(names, cstr_sv(&self->identifier.name), &generic_depth);
            if (result) {
                self->identifier.type = result->type;
                self->identifier.generic_depth = result->is_generic ? generic_depth : 0;
                self->identifier.generic_index = result->generic_index;
            } else {
                error_ctx_error(
                    errs,
                    self->identifier.span,
                    2304,
                    "Cannot find name '%s'.",
                    cstr_str(&self->identifier.name)
                );
            }
        }   break;

        case TYPE_GENERIC:
            generic_resolve_names(self->generic, names, errs);
            break;

        case TYPE_CALLABLE:
            c_foreach(i, vec_callable_type_arg, self->callable->args) {
                type_resolve_names(&i.ref->type, names, errs);
            }
            type_resolve_names(&self->callable->ret, names, errs);
            break;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, self->interface.members) {
                type_resolve_names(&i.ref->type, names, errs);
            }
            break;

        case TYPE_ARRAY:
            type_resolve_names(self->array, names, errs);
            break;

        case TYPE_TERNARY:
            type_resolve_names(&self->ternary->conditional.MaybeSub, names, errs);
            type_resolve_names(&self->ternary->conditional.MaybeSuper, names, errs);
            type_resolve_names(&self->ternary->TrueType, names, errs);
            type_resolve_names(&self->ternary->FalseType, names, errs);
            break;

    }
}

bool type_extends(Type const* sub, Type const* super) {
    switch (sub->tag) {
        case TYPE_ANY: return true;

        case TYPE_IDENTIFIER:
            if (sub->identifier.type) {
                return type_extends(sub->identifier.type, super);
            }
            return true;

        case TYPE_TERNARY: return type_extends(ternary_type_resolve(sub->ternary), super);

        default: break;
    }

    switch (super->tag) {
        case TYPE_ANY:
            return true;

        case TYPE_UNKOWN:
        case TYPE_NEVER:
            return false;

        case TYPE_VOID:
            return sub->tag == TYPE_VOID;

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
            if (super->identifier.type) {
                return type_extends(sub, super->identifier.type);
            }
            return true;

        case TYPE_GENERIC:
            NONFATAL("generic extends not yet implemented");
            return false;

        case TYPE_CALLABLE:
            if (sub->tag == TYPE_GENERIC) {
                NONFATAL("generic extends not yet implemented");
                return false;
            } else if (sub->tag == TYPE_CALLABLE) {
                long arg_count = vec_callable_type_arg_size(&super->callable->args);
                if (vec_callable_type_arg_size(&sub->callable->args) != arg_count) {
                    return false;
                }
                for (int i = 0; i < arg_count; i++) {
                    // Order is flipped for arguments
                    if (!type_extends(
                        &vec_callable_type_arg_at(&super->callable->args, i)->type,
                        &vec_callable_type_arg_at(&sub->callable->args, i)->type
                    )) {
                        return false;
                    }
                }
                // Order is not flipped for return
                return type_extends(&sub->callable->ret, &super->callable->ret);
            } else {
                return false;
            }

        case TYPE_ARRAY:
            return sub->tag == TYPE_ARRAY && type_extends(sub->array, super->array);

        case TYPE_INTERFACE:
            NONFATAL("extends(TYPE_INTERFACE, ...) not implemented");
            return false;

        case TYPE_TERNARY: return type_extends(sub, ternary_type_resolve(super->ternary));
    }
    UNREACHABLE;
}

void type_abstract_literals(Type* self) {
    switch (self->tag) {
        case TYPE_ANY:
        case TYPE_UNKOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_IDENTIFIER:
        case TYPE_GENERIC:
        case TYPE_CALLABLE:
        case TYPE_INTERFACE:
        case TYPE_ARRAY:
        case TYPE_TERNARY:
            break;

        case TYPE_VALUE:
            switch (self->value.tag) {
                case VALUE_UNDEFINED:
                case VALUE_NULL:
                    break;

                case VALUE_BOOLEAN:
                    *self = type_boolean;
                    break;

                case VALUE_NUMBER:
                    *self = type_number;
                    break;

                case VALUE_STRING:
                    type_drop(self);
                    *self = type_string;
                    break;

                case VALUE_OBJECT: {
                    Interface interface;
                    c_foreach(i, hmap_str_object_item, self->value.object.get->items) {
                        InterfaceMember member = interface_member_make(
                            cstr_clone(i.ref->first),
                            type_value(value_clone(i.ref->second.value))
                        );
                        type_abstract_literals(&member.type);
                        vec_interface_member_push(&interface.members, member);
                    }
                    type_drop(self);
                    *self = type_interface(interface);
                }   break;

                case VALUE_ARRAY:
                    NONFATAL("type_abstract_literals() not implemented for arrays");
                    break;

                case VALUE_JS_FUNC:
                case VALUE_NATIVE_FUNC:
                    type_drop(self);
                    *self = type_any;
                    break;
            }
            break;
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
