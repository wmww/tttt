#include "value.h"
#include "math.h"
#include "type.h"

void Value_drop(Value* self) {
    switch (self->tag) {
        case VALUE_NULL:
        case VALUE_UNDEFINED:
        case VALUE_BOOLEAN:
        case VALUE_NUMBER:
            break;

        case VALUE_STRING:
            cstr_drop(&self->data.string);
            break;
    }
}

Value Value_clone(Value self) {
    switch (self.tag) {
        case VALUE_NULL:
        case VALUE_UNDEFINED:
        case VALUE_BOOLEAN:
        case VALUE_NUMBER:
            break;

        case VALUE_STRING:
            self.data.string = cstr_clone(self.data.string);
            break;
    }
    return self;
}

cstr Value_to_cstr(Value const* self, int indent) {
    (void)indent;
    switch (self->tag) {
        case VALUE_NULL: return cstr_lit("null");
        case VALUE_UNDEFINED: return cstr_lit("undefined");
        case VALUE_BOOLEAN: return self->data.boolean ? cstr_lit("true") : cstr_lit("false");
        case VALUE_NUMBER: return round(self->data.number) == self->data.number
            ? cstr_from_fmt("%ld", (long)self->data.number)
            : cstr_from_fmt("%f", self->data.number);
        case VALUE_STRING:
            // TODO: escape string contents
            return cstr_from_fmt("'%s'", cstr_str(&self->data.string));
    }
    UNREACHABLE;
}

bool Value_strict_eq(Value const* a, Value const* b) {
    if (a->tag != b->tag) {
        return false;
    }
    switch (a->tag) {
        case VALUE_NULL: return true;
        case VALUE_UNDEFINED: return true;
        case VALUE_BOOLEAN: return a->data.boolean == b->data.boolean;
        case VALUE_NUMBER: return a->data.number == b->data.boolean;
        case VALUE_STRING: return cstr_eq(&a->data.string, &b->data.string);
    }
    UNREACHABLE;
}
