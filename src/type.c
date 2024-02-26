#include "type.h"
#include "interface.h"
#include "interface_member.h"
#include "ternary_type.h"
#include "extends_conditional.h"

void Type_drop(Type* self) {
    switch (self->tag) {
        case TYPE_NULL:
        case TYPE_ANY:
        case TYPE_UNDEFINED:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
            break;

        case TYPE_INTERFACE:
            Interface_drop(&self->data.interface);
            break;

        case TYPE_TERNARY:
            TernaryType_drop(self->data.ternary);
            free(self->data.ternary);
            break;
    }
}

cstr Type_to_cstr(Type* self, int indent) {
    switch (self->tag) {
        case TYPE_NULL: return cstr_lit("null");
        case TYPE_ANY: return cstr_lit("any");
        case TYPE_UNDEFINED: return cstr_lit("undefined");
        case TYPE_BOOLEAN: return cstr_lit("boolean");
        case TYPE_NUMBER: return cstr_lit("number");
        case TYPE_STRING: return cstr_lit("string");
        case TYPE_INTERFACE: return Interface_to_cstr(&self->data.interface, indent);
        case TYPE_TERNARY: return TernaryType_to_cstr(self->data.ternary, indent);
    }
    UNREACHABLE;
}

Type Type_move(Type* self) {
    Type ret = *self;
    self->tag = TYPE_UNDEFINED;
    return ret;
}

bool type_extends(Type *sub, Type *super) {
    switch (sub->tag) {
        case TYPE_NULL:
        case TYPE_ANY:
        case TYPE_UNDEFINED:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
            return sub->tag == super->tag;

        case TYPE_INTERFACE:
            PANIC("extends(TYPE_INTERFACE, ...) not implemented");
            break;

        case TYPE_TERNARY:
            PANIC("extends(TYPE_TERNARY, ...) not implemented");
            break;
    }
    UNREACHABLE;
}

Type type_simplify(Type t) {
    switch (t.tag) {
        case TYPE_NULL:
        case TYPE_ANY:
        case TYPE_UNDEFINED:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
            return t;

        case TYPE_INTERFACE:
            c_foreach(i, vec_InterfaceMember, t.data.interface.members) {
                i.ref->type = type_simplify(i.ref->type);
            }
            return t;
            break;

        case TYPE_TERNARY: {
            Type ret;
            if (extends_conditional_eval(&t.data.ternary->conditional)) {
                Type_drop(&t.data.ternary->falseType);
                ret = Type_move(&t.data.ternary->trueType);
            } else {
                Type_drop(&t.data.ternary->trueType);
                ret = Type_move(&t.data.ternary->falseType);
            }
            free(t.data.ternary);
            return type_simplify(ret);
        }
    }
    UNREACHABLE;
}
