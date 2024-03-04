#include "value.h"
#include "math.h"
#include "type.h"
#include "result.h"

void Value_drop_internal(Value* self) {
    switch (self->tag) {
        case VALUE_UNDEFINED:
        case VALUE_NULL:
        case VALUE_BOOLEAN:
        case VALUE_NUMBER: break;
        case VALUE_STRING: cstr_drop(&self->data.string); break;
        case VALUE_OBJECT: arc_Object_drop(&self->data.object); break;
        case VALUE_ARRAY: arc_Array_drop(&self->data.array); break;
        case VALUE_JS_FUNC: arc_JsFunc_drop(&self->data.js_func); break;
        case VALUE_NATIVE_FUNC: arc_NativeFunc_drop(&self->data.native_func); break;
    }
}

Value Value_clone(Value self) {
    switch (self.tag) {
        case VALUE_UNDEFINED:
        case VALUE_NULL:
        case VALUE_BOOLEAN:
        case VALUE_NUMBER: break;
        case VALUE_STRING: self.data.string = cstr_clone(self.data.string); break;
        case VALUE_OBJECT: self.data.object = arc_Object_clone(self.data.object); break;
        case VALUE_ARRAY: self.data.array = arc_Array_clone(self.data.array); break;
        case VALUE_JS_FUNC: self.data.js_func = arc_JsFunc_clone(self.data.js_func); break;
        case VALUE_NATIVE_FUNC: self.data.native_func = arc_NativeFunc_clone(self.data.native_func); break;
    }
    return self;
}

static double string_to_num(const char* start) {
    if (*start) {
        return 0;
    }
    char* end = (char*)start;
    double num = strtod(start, &end);
    if (!*end) {
        return num;
    } else {
        return NAN;
    }
}

bool Value_to_boolean(Value const* self) {
    switch (self->tag) {
        case VALUE_UNDEFINED: return false;
        case VALUE_NULL: return false;
        case VALUE_BOOLEAN: return self->data.boolean;
        case VALUE_NUMBER: return self->data.number != 0;
        case VALUE_STRING: return !cstr_empty(&self->data.string);
        case VALUE_OBJECT: return true;
        case VALUE_ARRAY: return true;
        case VALUE_JS_FUNC: return true;
        case VALUE_NATIVE_FUNC: return true;
    }
    UNREACHABLE;
}

double Value_to_number(Value const* self) {
    switch (self->tag) {
        case VALUE_UNDEFINED:
        case VALUE_OBJECT:
        case VALUE_JS_FUNC:
        case VALUE_NATIVE_FUNC:
            return NAN;

        case VALUE_NULL: return 0;
        case VALUE_BOOLEAN: return self->data.boolean ? 1 : 0;
        case VALUE_NUMBER: return self->data.number;
        case VALUE_STRING: return string_to_num(cstr_str(&self->data.string));

        case VALUE_ARRAY: {
            int len = vec_Value_size(&self->data.array.get->vec);
            if (len == 0) {
                return 0;
            } else if (len == 1) {
                cstr str = cstr_null;
                cstr_append_value_js(&str, vec_Value_at(&self->data.array.get->vec, 0));
                double ret = string_to_num(cstr_str(&str));
                cstr_drop(&str);
                return ret;
            } else {
                return NAN;
            }
        }
    }
    UNREACHABLE;
}

void cstr_append_value_js(cstr* target, Value const* self) {
    switch (self->tag) {
        case VALUE_UNDEFINED: cstr_append_sv(target, c_sv("undefined")); break;
        case VALUE_NULL: cstr_append_sv(target, c_sv("null")); break;
        case VALUE_STRING: cstr_append_s(target, self->data.string); break;
        case VALUE_OBJECT: cstr_append_sv(target, c_sv("[object Object]")); break;
        case VALUE_JS_FUNC: cstr_append_sv(target, c_sv("[javascript function]")); break;
        case VALUE_NATIVE_FUNC: cstr_append_sv(target, c_sv("[native function]")); break;

        case VALUE_BOOLEAN:
            if (self->data.boolean) {
                cstr_append_sv(target, c_sv("true"));
            } else {
                cstr_append_sv(target, c_sv("false"));
            }
            break;

        case VALUE_NUMBER:
            if (round(self->data.number) == self->data.number) {
                cstr_append_fmt(target, "%ld", (long)self->data.number);
            } else if (isnan(self->data.number)) {
                cstr_append_sv(target, c_sv("NaN"));
            } else {
                cstr_append_fmt(target, "%f", self->data.number);
            }
            break;

        case VALUE_ARRAY:
            c_foreach(i, vec_Value, self->data.array.get->vec) {
                cstr_append_value_js(target, i.ref);
                if (i.ref < i.end - 1) {
                    cstr_append_sv(target, c_sv(","));
                }
            }
            break;
    }
}

void cstr_append_value_debug(cstr* target, Value const* self, int indent) {
    (void)indent;
    switch (self->tag) {
        case VALUE_UNDEFINED: cstr_append_sv(target, c_sv(CLR_VALUE "undefined" CLR0)); break;
        case VALUE_NULL: cstr_append_sv(target, c_sv(CLR_VALUE "null" CLR0)); break;

        case VALUE_BOOLEAN:
            if (self->data.boolean) {
                cstr_append_sv(target, c_sv(CLR_VALUE "true" CLR0));
            } else {
                cstr_append_sv(target, c_sv(CLR_VALUE "false" CLR0));
            }
            break;

        case VALUE_NUMBER:
            if (round(self->data.number) == self->data.number) {
                cstr_append_fmt(target, CLR_NUM "%ld" CLR0, (long)self->data.number);
            } else {
                cstr_append_fmt(target, CLR_NUM "%f" CLR0, self->data.number);
            }
            break;

        case VALUE_STRING:
            // TODO: escape string contents
            cstr_append_sv(target, c_sv(CLR_STR "'"));
            cstr_append_s(target, self->data.string);
            cstr_append_sv(target, c_sv("'" CLR0));
            break;

        case VALUE_OBJECT: {
            cstr_append_sv(target, c_sv("{"));
            vec_ObjectItemRef items = ObjectValue_sorted_items(self->data.object.get);
            int size = vec_ObjectItemRef_size(&items);
            if (size > 1) {
                cstr_append_sv(target, c_sv("\n"));
                c_foreach(i, vec_ObjectItemRef, items) {
                    APPEND_INDENT(target, indent + 1);
                    cstr_append_s(target, *i.ref->name);
                    cstr_append_sv(target, c_sv(": "));
                    cstr_append_value_debug(target, &i.ref->item->value, indent);
                    cstr_append_sv(target, c_sv(",\n"));
                }
                APPEND_INDENT(target, indent);
            } else if (size == 1) {
                cstr_append_sv(target, c_sv(" "));
                cstr_append_s(target, *vec_ObjectItemRef_at(&items, 0)->name);
                cstr_append_sv(target, c_sv(": "));
                cstr_append_value_debug(target, &vec_ObjectItemRef_at(&items, 0)->item->value, indent);
                cstr_append_sv(target, c_sv(" "));
            }
            cstr_append_sv(target, c_sv("}"));
            vec_ObjectItemRef_drop(&items);
        }   break;

        case VALUE_ARRAY: {
            cstr_append_sv(target, c_sv("["));
            ArrayValue* value = self->data.array.get;
            int size = vec_Value_size(&value->vec);
            if (size > 1) {
                cstr_append_sv(target, c_sv("\n"));
                c_foreach(i, vec_Value, value->vec) {
                    APPEND_INDENT(target, indent + 1);
                    cstr_append_value_debug(target, i.ref, indent);
                    cstr_append_sv(target, c_sv(",\n"));
                }
                APPEND_INDENT(target, indent);
            } else if (size == 1) {
                cstr_append_sv(target, c_sv(" "));
                cstr_append_value_debug(target, vec_Value_at(&value->vec, 0), indent);
                cstr_append_sv(target, c_sv(" "));
            }
            cstr_append_sv(target, c_sv("]"));
        }   break;

        case VALUE_JS_FUNC: cstr_append_sv(target, c_sv("[javascript function]")); break;
        case VALUE_NATIVE_FUNC: cstr_append_sv(target, c_sv("[native function]")); break;
    }
}

bool Value_loose_eq(Value const* a, Value const* b) {
    if (a->tag == b->tag) {
        return Value_strict_eq(a, b);
    } else if (
        a->tag == VALUE_UNDEFINED || a->tag == VALUE_NULL ||
        b->tag == VALUE_UNDEFINED || b->tag == VALUE_NULL
    ) {
        return (
            (a->tag == VALUE_UNDEFINED || a->tag == VALUE_NULL) &&
            (b->tag == VALUE_UNDEFINED || b->tag == VALUE_NULL)
        );
    } else {
        // TODO: there's complicated rules for this, see https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Equality
        cstr as = cstr_null;
        cstr bs = cstr_null;
        cstr_append_value_js(&as, a);
        cstr_append_value_js(&bs, b);
        bool ret = cstr_eq(&as, &bs);
        cstr_drop(&as);
        cstr_drop(&bs);
        return ret;
    }
}

bool Value_strict_eq(Value const* a, Value const* b) {
    if (a->tag != b->tag) {
        return false;
    }
    switch (a->tag) {
        case VALUE_UNDEFINED: return true;
        case VALUE_NULL: return true;
        case VALUE_BOOLEAN: return a->data.boolean == b->data.boolean;
        case VALUE_NUMBER: return a->data.number == b->data.number;
        case VALUE_STRING: return cstr_eq(&a->data.string, &b->data.string);
        case VALUE_OBJECT: return a->data.object.get == b->data.object.get;
        case VALUE_ARRAY: return a->data.array.get == b->data.array.get;
        case VALUE_JS_FUNC: return a->data.js_func.get == b->data.js_func.get;
        case VALUE_NATIVE_FUNC: return a->data.native_func.get == b->data.native_func.get;
    }
    UNREACHABLE;
}

void ObjectValueItem_drop(ObjectValueItem* self) {
    Value_drop(&self->value);
}

ObjectValueItem ObjectValueItem_clone(ObjectValueItem self) {
    self.value = Value_clone(self.value);
    return self;
}

void ObjectValue_drop(ObjectValue* self) {
    hmap_str_ObjectValueItem_drop(&self->items);
}

ObjectValue ObjectValue_clone(ObjectValue self) {
    self.items = hmap_str_ObjectValueItem_clone(self.items);
    return self;
}

vec_ObjectItemRef ObjectValue_sorted_items(ObjectValue* self) {
    vec_ObjectItemRef ret = vec_ObjectItemRef_with_capacity(self->total_inserted);
    c_foreach(i, hmap_str_ObjectValueItem, self->items) {
        vec_ObjectItemRef_push(&ret, (ObjectItemRef){
            .name = &i.ref->first,
            .item = &i.ref->second,
        });
    }
    vec_ObjectItemRef_quicksort(&ret);
    return ret;
}

void ObjectValue_set(ObjectValue* self, cstr key, Value val) {
    hmap_str_ObjectValueItem_insert_or_assign(&self->items, key, (ObjectValueItem){
        .value = val,
        .position = self->total_inserted,
    });
    self->total_inserted++;
}

Result ObjectValue_get(ObjectValue* self, csview key) {
    hmap_str_ObjectValueItem_value const* value = hmap_str_ObjectValueItem_get(&self->items, key);
    if (value) {
        return Result_ok(Value_clone(value->second.value));
    } else {
        return Result_err(Value_undefined);
    }
}

bool ObjectValue_has(ObjectValue* self, csview key) {
    return hmap_str_ObjectValueItem_get(&self->items, key) != nullptr;
}

void ArrayValue_drop(ArrayValue* self) {
    ObjectValue_drop(&self->fallback);
    vec_Value_drop(&self->vec);
}

ArrayValue ArrayValue_clone(ArrayValue self) {
    return (ArrayValue){
        .fallback = ObjectValue_clone(self.fallback),
        .vec = vec_Value_clone(self.vec),
    };
}

void JsFuncValue_drop(JsFuncValue* self) {
    (void)self;
}

void NativeFuncValue_drop(NativeFuncValue* self) {
    if (self->drop) {
        self->drop(self->data);
    }
}
