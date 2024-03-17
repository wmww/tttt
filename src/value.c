#include "value.h"
#include "math.h"
#include "type.h"
#include "result.h"

void value_drop_internal(Value* me) {
    switch (me->tag) {
        case VALUE_UNDEFINED:
        case VALUE_NULL:
        case VALUE_BOOLEAN:
        case VALUE_NUMBER: break;
        case VALUE_STRING: cstr_drop(&me->string); break;
        case VALUE_OBJECT: arc_js_object_drop(&me->object); break;
        case VALUE_ARRAY: arc_js_array_drop(&me->array); break;
        case VALUE_JS_FUNC: arc_js_func_value_drop(&me->js_func); break;
        case VALUE_NATIVE_FUNC: arc_native_func_value_drop(&me->native_func); break;
    }
}

Value value_clone(Value me) {
    switch (me.tag) {
        case VALUE_UNDEFINED:
        case VALUE_NULL:
        case VALUE_BOOLEAN:
        case VALUE_NUMBER: break;
        case VALUE_STRING: me.string = cstr_clone(me.string); break;
        case VALUE_OBJECT: me.object = arc_js_object_clone(me.object); break;
        case VALUE_ARRAY: me.array = arc_js_array_clone(me.array); break;
        case VALUE_JS_FUNC: me.js_func = arc_js_func_value_clone(me.js_func); break;
        case VALUE_NATIVE_FUNC: me.native_func = arc_native_func_value_clone(me.native_func); break;
    }
    return me;
}

static double string_to_num(const char* start) {
    if (!*start) {
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

bool value_to_boolean(Value const* me) {
    switch (me->tag) {
        case VALUE_UNDEFINED: return false;
        case VALUE_NULL: return false;
        case VALUE_BOOLEAN: return me->boolean;
        case VALUE_NUMBER: return me->number != 0;
        case VALUE_STRING: return !cstr_empty(&me->string);
        case VALUE_OBJECT: return true;
        case VALUE_ARRAY: return true;
        case VALUE_JS_FUNC: return true;
        case VALUE_NATIVE_FUNC: return true;
    }
    UNREACHABLE;
}

double value_to_number(Value const* me) {
    switch (me->tag) {
        case VALUE_UNDEFINED:
        case VALUE_OBJECT:
        case VALUE_JS_FUNC:
        case VALUE_NATIVE_FUNC:
            return NAN;

        case VALUE_NULL: return 0;
        case VALUE_BOOLEAN: return me->boolean ? 1 : 0;
        case VALUE_NUMBER: return me->number;
        case VALUE_STRING: return string_to_num(cstr_str(&me->string));

        case VALUE_ARRAY: {
            int len = vec_value_size(&me->array.get->vec);
            if (len == 0) {
                return 0;
            } else if (len == 1) {
                cstr str = cstr_null;
                cstr_append_value_js(&str, vec_value_at(&me->array.get->vec, 0));
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

void cstr_append_value_js(cstr* target, Value const* me) {
    switch (me->tag) {
        case VALUE_UNDEFINED: cstr_append_sv(target, c_sv("undefined")); break;
        case VALUE_NULL: cstr_append_sv(target, c_sv("null")); break;
        case VALUE_STRING: cstr_append_s(target, me->string); break;
        case VALUE_OBJECT: cstr_append_sv(target, c_sv("[object Object]")); break;
        case VALUE_JS_FUNC: cstr_append_sv(target, c_sv("[javascript function]")); break;
        case VALUE_NATIVE_FUNC: cstr_append_sv(target, c_sv("[native function]")); break;

        case VALUE_BOOLEAN:
            if (me->boolean) {
                cstr_append_sv(target, c_sv("true"));
            } else {
                cstr_append_sv(target, c_sv("false"));
            }
            break;

        case VALUE_NUMBER:
            if (round(me->number) == me->number) {
                cstr_append_fmt(target, "%ld", (long)me->number);
            } else if (isnan(me->number)) {
                cstr_append_sv(target, c_sv("NaN"));
            } else {
                cstr_append_fmt(target, "%f", me->number);
            }
            break;

        case VALUE_ARRAY:
            c_foreach(i, vec_value, me->array.get->vec) {
                cstr_append_value_js(target, i.ref);
                if (i.ref < i.end - 1) {
                    cstr_append_sv(target, c_sv(","));
                }
            }
            break;
    }
}

void cstr_append_value_debug(cstr* target, Value const* me, int indent) {
    (void)indent;
    switch (me->tag) {
        case VALUE_UNDEFINED: cstr_append_sv(target, c_sv(CLR_VALUE "undefined" CLR0)); break;
        case VALUE_NULL: cstr_append_sv(target, c_sv(CLR_VALUE "null" CLR0)); break;

        case VALUE_BOOLEAN:
            if (me->boolean) {
                cstr_append_sv(target, c_sv(CLR_VALUE "true" CLR0));
            } else {
                cstr_append_sv(target, c_sv(CLR_VALUE "false" CLR0));
            }
            break;

        case VALUE_NUMBER:
            if (round(me->number) == me->number) {
                cstr_append_fmt(target, CLR_NUM "%ld" CLR0, (long)me->number);
            } else {
                cstr_append_fmt(target, CLR_NUM "%f" CLR0, me->number);
            }
            break;

        case VALUE_STRING:
            // TODO: escape string contents
            cstr_append_sv(target, c_sv(CLR_STR "'"));
            cstr_append_s(target, me->string);
            cstr_append_sv(target, c_sv("'" CLR0));
            break;

        case VALUE_OBJECT: {
            cstr_append_sv(target, c_sv("{"));
            VecObjectItemRef items = js_object_sorted_items(me->object.get);
            int size = vec_object_item_ref_size(&items);
            if (size > 1) {
                cstr_append_sv(target, c_sv("\n"));
                c_foreach(i, vec_object_item_ref, items) {
                    APPEND_INDENT(target, indent + 1);
                    cstr_append_s(target, *i.ref->name);
                    cstr_append_sv(target, c_sv(": "));
                    cstr_append_value_debug(target, &i.ref->item->value, indent);
                    cstr_append_sv(target, c_sv(",\n"));
                }
                APPEND_INDENT(target, indent);
            } else if (size == 1) {
                cstr_append_sv(target, c_sv(" "));
                cstr_append_s(target, *vec_object_item_ref_at(&items, 0)->name);
                cstr_append_sv(target, c_sv(": "));
                cstr_append_value_debug(target, &vec_object_item_ref_at(&items, 0)->item->value, indent);
                cstr_append_sv(target, c_sv(" "));
            }
            cstr_append_sv(target, c_sv("}"));
            vec_object_item_ref_drop(&items);
        }   break;

        case VALUE_ARRAY: {
            cstr_append_sv(target, c_sv("["));
            JsArray* value = me->array.get;
            int size = vec_value_size(&value->vec);
            if (size > 1) {
                cstr_append_sv(target, c_sv("\n"));
                c_foreach(i, vec_value, value->vec) {
                    APPEND_INDENT(target, indent + 1);
                    cstr_append_value_debug(target, i.ref, indent);
                    cstr_append_sv(target, c_sv(",\n"));
                }
                APPEND_INDENT(target, indent);
            } else if (size == 1) {
                cstr_append_sv(target, c_sv(" "));
                cstr_append_value_debug(target, vec_value_at(&value->vec, 0), indent);
                cstr_append_sv(target, c_sv(" "));
            }
            cstr_append_sv(target, c_sv("]"));
        }   break;

        case VALUE_JS_FUNC: cstr_append_sv(target, c_sv("[javascript function]")); break;
        case VALUE_NATIVE_FUNC: cstr_append_sv(target, c_sv("[native function]")); break;
    }
}

bool value_loose_eq(Value const* a, Value const* b) {
    if (a->tag == b->tag) {
        return value_strict_eq(a, b);
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

bool value_strict_eq(Value const* a, Value const* b) {
    if (a->tag != b->tag) {
        return false;
    }
    switch (a->tag) {
        case VALUE_UNDEFINED: return true;
        case VALUE_NULL: return true;
        case VALUE_BOOLEAN: return a->boolean == b->boolean;
        case VALUE_NUMBER: return a->number == b->number;
        case VALUE_STRING: return cstr_eq(&a->string, &b->string);
        case VALUE_OBJECT: return a->object.get == b->object.get;
        case VALUE_ARRAY: return a->array.get == b->array.get;
        case VALUE_JS_FUNC: return a->js_func.get == b->js_func.get;
        case VALUE_NATIVE_FUNC: return a->native_func.get == b->native_func.get;
    }
    UNREACHABLE;
}

void object_item_drop(ObjectItem* me) {
    value_drop(&me->value);
}

ObjectItem object_item_clone(ObjectItem me) {
    me.value = value_clone(me.value);
    return me;
}

void js_object_drop(JsObject* me) {
    hmap_str_object_item_drop(&me->items);
}

JsObject js_object_clone(JsObject me) {
    me.items = hmap_str_object_item_clone(me.items);
    return me;
}

VecObjectItemRef js_object_sorted_items(JsObject* me) {
    VecObjectItemRef ret = vec_object_item_ref_with_capacity(me->total_inserted);
    c_foreach(i, hmap_str_object_item, me->items) {
        vec_object_item_ref_push(&ret, (ObjectItemRef){
            .name = &i.ref->first,
            .item = &i.ref->second,
        });
    }
    vec_object_item_ref_quicksort(&ret);
    return ret;
}

void js_object_set(JsObject* me, cstr key, Value val) {
    hmap_str_object_item_insert_or_assign(&me->items, key, (ObjectItem){
        .value = val,
        .position = me->total_inserted,
    });
    me->total_inserted++;
}

Result js_object_get(JsObject* me, csview key) {
    hmap_str_object_item_value const* value = hmap_str_object_item_get(&me->items, key);
    if (value) {
        return result_ok(value_clone(value->second.value));
    } else {
        return result_err(value_undefined);
    }
}

bool js_object_has(JsObject* me, csview key) {
    return hmap_str_object_item_get(&me->items, key) != nullptr;
}

void array_value_drop(JsArray* me) {
    js_object_drop(&me->fallback);
    vec_value_drop(&me->vec);
}

JsArray array_value_clone(JsArray me) {
    return (JsArray){
        .fallback = js_object_clone(me.fallback),
        .vec = vec_value_clone(me.vec),
    };
}

void js_func_value_drop(JsFuncValue* me) {
    (void)me;
}

void native_func_value_drop(NativeFuncValue* me) {
    if (me->drop) {
        me->drop(me->data);
    }
}
