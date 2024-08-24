#include "value.h"
#include "math.h"
#include "function_impl.h"
#include "result.h"
#include "display.h"
#include "string_helpers.h"

void value_drop_internal(Value* me) {
    switch (my tag) {
        case VALUE_UNDEFINED:
        case VALUE_NULL:
        case VALUE_BOOLEAN:
        case VALUE_NUMBER: break;
        case VALUE_STRING: cstr_drop(&my string); break;
        case VALUE_OBJECT: arc_js_object_drop(&my object); break;
        case VALUE_ARRAY: arc_js_array_drop(&my array); break;
        case VALUE_JS_FUNC: arc_js_func_value_drop(&my js_func); break;
        case VALUE_NATIVE_FUNC: arc_native_func_value_drop(&my native_func); break;
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

static double string_to_num(csview sv) {
    sv = csview_trim_whitespace(sv);
    double ret;
    const char* end = csview_end_ptr(sv);
    if (string_to_double(sv.buf, end, true, false, &ret) == end) {
        return ret;
    } else {
        return NAN;
    }
}

bool value_to_boolean(Value const* me) {
    switch (my tag) {
        case VALUE_UNDEFINED: return false;
        case VALUE_NULL: return false;
        case VALUE_BOOLEAN: return my boolean;
        case VALUE_NUMBER: return my number != 0;
        case VALUE_STRING: return !cstr_empty(&my string);
        case VALUE_OBJECT: return true;
        case VALUE_ARRAY: return true;
        case VALUE_JS_FUNC: return true;
        case VALUE_NATIVE_FUNC: return true;
    }
    UNREACHABLE;
}

double value_to_number(Value const* me) {
    switch (my tag) {
        case VALUE_UNDEFINED:
        case VALUE_OBJECT:
        case VALUE_JS_FUNC:
        case VALUE_NATIVE_FUNC:
            return NAN;

        case VALUE_NULL: return 0;
        case VALUE_BOOLEAN: return my boolean ? 1 : 0;
        case VALUE_NUMBER: return my number;
        case VALUE_STRING: return string_to_num(cstr_sv(&my string));

        case VALUE_ARRAY: {
            int len = vec_value_size(&my array.get->vec);
            if (len == 0) {
                return 0;
            } else if (len == 1) {
                cstr str = cstr_null;
                cstr_append_value_js(&str, vec_value_at(&my array.get->vec, 0));
                double ret = string_to_num(cstr_sv(&str));
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
    switch (my tag) {
        case VALUE_UNDEFINED: cstr_append_lit(target, "undefined"); break;
        case VALUE_NULL: cstr_append_lit(target, "null"); break;
        case VALUE_STRING: cstr_append_s(target, my string); break;
        case VALUE_OBJECT: cstr_append_lit(target, "[object Object]"); break;

        case VALUE_BOOLEAN:
            if (my boolean) {
                cstr_append_lit(target, "true");
            } else {
                cstr_append_lit(target, "false");
            }
            break;

        case VALUE_NUMBER:
            if (my number == 0) {
                cstr_append_lit(target, "0");
            } else if (isnan(my number)) {
                cstr_append_lit(target, "NaN");
            } else {
                cstr_append_fmt(target, "%.21g", my number);
            }
            break;

        case VALUE_ARRAY:
            c_foreach(i, vec_value, my array.get->vec) {
                cstr_append_value_js(target, i.ref);
                if (i.ref < i.end - 1) {
                    cstr_append_lit(target, ",");
                }
            }
            break;

        case VALUE_JS_FUNC:
            cstr_append_sv(target, span_sv(&my js_func.get->impl->full_span));
            break;

        case VALUE_NATIVE_FUNC:
            cstr_append_lit(target, "function() {\n    [native code]\n}");
            break;
    }
}

void cstr_append_value_error_msg(cstr* target, Value const* me) {
    display_value(error_display(target), me);
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
    value_drop(&my value);
}

ObjectItem object_item_clone(ObjectItem me) {
    me.value = value_clone(me.value);
    return me;
}

void js_object_drop(JsObject* me) {
    hmap_str_object_item_drop(&my items);
}

JsObject js_object_clone(JsObject me) {
    me.items = hmap_str_object_item_clone(me.items);
    return me;
}

VecObjectItemRef js_object_sorted_items(JsObject* me) {
    VecObjectItemRef ret = vec_object_item_ref_with_capacity(my total_inserted);
    c_foreach(i, hmap_str_object_item, my items) {
        vec_object_item_ref_push(&ret, (ObjectItemRef){
            .name = &i.ref->first,
            .item = &i.ref->second,
        });
    }
    vec_object_item_ref_quicksort(&ret);
    return ret;
}

void js_object_set(JsObject* me, cstr key, Value val) {
    hmap_str_object_item_insert_or_assign(&my items, key, (ObjectItem){
        .value = val,
        .position = my total_inserted,
    });
    my total_inserted++;
}

Result js_object_get(JsObject const* me, csview key) {
    hmap_str_object_item_value const* value = hmap_str_object_item_get(&my items, key);
    if (value) {
        return result_ok(value_clone(value->second.value));
    } else {
        return result_err(value_undefined);
    }
}

bool js_object_has(JsObject* me, csview key) {
    return hmap_str_object_item_get(&my items, key) != nullptr;
}

void js_array_drop(JsArray* me) {
    js_object_drop(&my fallback);
    vec_value_drop(&my vec);
}

JsArray js_array_clone(JsArray me) {
    return (JsArray){
        .fallback = js_object_clone(me.fallback),
        .vec = vec_value_clone(me.vec),
    };
}

void js_func_value_drop(JsFuncValue* me) {
    __ me;
}

void native_func_value_drop(NativeFuncValue* me) {
    if (my drop) {
        my drop(my data);
    }
}
