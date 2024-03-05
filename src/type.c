#include "type.h"
#include "interface.h"
#include "interface_member.h"
#include "ternary_type.h"
#include "callable_type.h"
#include "extends_conditional.h"
#include "value.h"

const Type Type_any = { .tag = TYPE_ANY };
const Type Type_void = { .tag = TYPE_VOID };
const Type Type_boolean = { .tag = TYPE_BOOLEAN };
const Type Type_number = { .tag = TYPE_NUMBER };
const Type Type_string = { .tag = TYPE_STRING };

void Type_drop(Type* self) {
    switch (self->tag) {
        case TYPE_ANY:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
            break;

        case TYPE_VALUE:
            Value_drop(&self->value);
            break;

        case TYPE_IDENTIFIER:
            cstr_drop(&self->identifier);
            break;

        case TYPE_CALLABLE:
            CallableType_drop(self->callable);
            free(self->callable);
            break;

        case TYPE_INTERFACE:
            Interface_drop(&self->interface);
            break;

        case TYPE_TERNARY:
            TernaryType_drop(self->ternary);
            free(self->ternary);
            break;
    }
    self->tag = TYPE_ANY; // Makes repeated calls to drop safe
}

void cstr_append_type_debug(cstr* target, Type* self, int indent) {
    switch (self->tag) {
        case TYPE_ANY: cstr_append_sv(target, c_sv(CLR_TYPE "any" CLR0)); break;
        case TYPE_VOID: cstr_append_sv(target, c_sv(CLR_TYPE "void" CLR0)); break;
        case TYPE_BOOLEAN: cstr_append_sv(target, c_sv(CLR_TYPE "boolean" CLR0)); break;
        case TYPE_NUMBER: cstr_append_sv(target, c_sv(CLR_TYPE "number" CLR0)); break;
        case TYPE_STRING: cstr_append_sv(target, c_sv(CLR_TYPE "string" CLR0)); break;
        case TYPE_VALUE: cstr_append_value_debug(target, &self->value, indent); break;
        case TYPE_IDENTIFIER:
            cstr_append_sv(target, c_sv(CLR_TYPE));
            cstr_append_s(target, self->identifier);
            cstr_append_sv(target, c_sv(CLR0));
            break;
        case TYPE_CALLABLE: cstr_append_callable_type_debug(target, self->callable, indent); break;
        case TYPE_INTERFACE: cstr_append_interface_debug(target, &self->interface, indent); break;
        case TYPE_TERNARY: cstr_append_ternary_type_debug(target, self->ternary, indent); break;
    }
}

Type Type_clone(Type self) {
    switch (self.tag) {
        case TYPE_ANY:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING: return self;
        case TYPE_VALUE: return Type_value(Value_clone(self.value));
        case TYPE_IDENTIFIER: return Type_identifier(cstr_clone(self.identifier));
        case TYPE_CALLABLE: return Type_callable(c_new(CallableType, CallableType_clone(*self.callable)));
        case TYPE_INTERFACE: return Type_interface(Interface_clone(self.interface));
        case TYPE_TERNARY: return Type_ternary(c_new(TernaryType, TernaryType_clone(*self.ternary)));
    }
    UNREACHABLE;
}

Type Type_move(Type* self) {
    Type ret = *self;
    self->tag = TYPE_ANY;
    return ret;
}

bool Type_extends(Type const* sub, Type const* super) {
    switch (sub->tag) {
        case TYPE_ANY: return true;
        case TYPE_TERNARY: Type_extends(TernaryType_resolve(sub->ternary), super);
        default: break;
    }

    switch (super->tag) {
        case TYPE_ANY:
            return true;

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
            return Value_strict_eq(&sub->value, &super->value);
            break;

        case TYPE_IDENTIFIER:
            PANIC("identifier extends not yet implemented");
            break;

        case TYPE_CALLABLE:
            PANIC("callable extends not yet implemented");
            break;

        case TYPE_INTERFACE:
            PANIC("extends(TYPE_INTERFACE, ...) not implemented");
            break;

        case TYPE_TERNARY: return Type_extends(sub, TernaryType_resolve(super->ternary));
    }
    UNREACHABLE;
}
