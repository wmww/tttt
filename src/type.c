#include "type.h"
#include "interface.h"
#include "interface_member.h"
#include "ternary_type.h"
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
            Value_drop(&self->data.value);
            break;

        case TYPE_INTERFACE:
            Interface_drop(&self->data.interface);
            break;

        case TYPE_TERNARY:
            TernaryType_drop(self->data.ternary);
            free(self->data.ternary);
            break;
    }
    self->tag = TYPE_ANY; // Makes repeated calls to drop safe
}

cstr Type_to_cstr(Type* self, int indent) {
    switch (self->tag) {
        case TYPE_ANY: return cstr_lit(CLR_TYPE "any" CLR0);
        case TYPE_VOID: return cstr_lit(CLR_TYPE "void" CLR0);
        case TYPE_BOOLEAN: return cstr_lit(CLR_TYPE "boolean" CLR0);
        case TYPE_NUMBER: return cstr_lit(CLR_TYPE "number" CLR0);
        case TYPE_STRING: return cstr_lit(CLR_TYPE "string" CLR0);
        case TYPE_VALUE: return Value_to_cstr_debug(&self->data.value, indent);
        case TYPE_INTERFACE: return Interface_to_cstr(&self->data.interface, indent);
        case TYPE_TERNARY: return TernaryType_to_cstr(self->data.ternary, indent);
    }
    UNREACHABLE;
}

Type Type_clone(Type self) {
    switch (self.tag) {
        case TYPE_ANY:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING: return self;
        case TYPE_VALUE: return Type_value_new(Value_clone(self.data.value));
        case TYPE_INTERFACE: return Type_interface_new(Interface_clone(self.data.interface));
        case TYPE_TERNARY: return Type_ternary_new(c_new(TernaryType, TernaryType_clone(*self.data.ternary)));
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
        case TYPE_TERNARY: Type_extends(TernaryType_resolve(sub->data.ternary), super);
        default: break;
    }

    switch (super->tag) {
        case TYPE_ANY:
            return true;

        case TYPE_VOID:
            return sub->tag == TYPE_VOID;

        case TYPE_BOOLEAN:
            return sub->tag == TYPE_BOOLEAN || (sub->tag == TYPE_VALUE && sub->data.value.tag == VALUE_BOOLEAN);

        case TYPE_NUMBER:
            return sub->tag == TYPE_NUMBER || (sub->tag == TYPE_VALUE && sub->data.value.tag == VALUE_NUMBER);

        case TYPE_STRING:
            return sub->tag == TYPE_STRING || (sub->tag == TYPE_VALUE && sub->data.value.tag == VALUE_STRING);

        case TYPE_VALUE:
            if (sub->tag != TYPE_VALUE) {
                return false;
            }
            return Value_strict_eq(&sub->data.value, &super->data.value);
            break;

        case TYPE_INTERFACE:
            PANIC("extends(TYPE_INTERFACE, ...) not implemented");
            break;

        case TYPE_TERNARY: return Type_extends(sub, TernaryType_resolve(super->data.ternary));
    }
    UNREACHABLE;
}
