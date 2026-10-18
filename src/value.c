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
        case VALUE_STRING: ast_cstr_drop(&my string); break; // (The AST's literals are: ast_cstr_seal())
        case VALUE_SYMBOL: arc_symbol_value_drop(&my symbol); break;
        case VALUE_BIGINT: break;
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
        case VALUE_SYMBOL: me.symbol = arc_symbol_value_clone(me.symbol); break;
        case VALUE_BIGINT: break;
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
    if (string_to_double(sv.buf, end, true, false, false, &ret) == end) {
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
        case VALUE_NUMBER: return !isnan(my number) && my number != 0;
        case VALUE_STRING: return !cstr_is_empty(&my string);
        case VALUE_SYMBOL: return true;
        case VALUE_BIGINT: return !bigint_is_zero(my bigint);
        case VALUE_OBJECT: return true;
        case VALUE_ARRAY: return true;
        case VALUE_JS_FUNC: return true;
        case VALUE_NATIVE_FUNC: return true;
    }
    impossible;
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
        case VALUE_SYMBOL: return 0;
        case VALUE_BIGINT: {
            cstr text = cstr_init();
            bigint_append_decimal(&text, my bigint);
            double ret = strtod(cstr_str(&text), nullptr);
            cstr_drop(&text);
            return ret;
        }

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
    impossible;
}

// ECMAScript's Number::toString: the shortest digits that read back as the number, positioned by its rules
static void append_js_number(cstr* target, double number) {
    if (number == 0) {
        cstr_append_lit(target, "0");
        return;
    }
    if (isnan(number)) {
        cstr_append_lit(target, "NaN");
        return;
    }
    if (number < 0) {
        cstr_append_lit(target, "-");
        number = -number;
    }
    if (isinf(number)) {
        cstr_append_lit(target, "Infinity");
        return;
    }
    char buf[48];
    for (int precision = 1; precision <= 17; precision++) {
        snprintf(buf, sizeof(buf), "%.*e", precision - 1, number);
        if (strtod(buf, nullptr) == number) break;
    }
    // buf is d.ddde[+-]x: the digits, and the position n of the decimal point (the number is 0.digits * 10^n)
    char digits[24];
    int k = 0;
    char const* c = buf;
    for (; *c && *c != 'e'; c++) {
        if (*c >= '0' && *c <= '9' && k < (int)sizeof(digits) - 1) digits[k++] = *c;
    }
    while (k > 1 && digits[k - 1] == '0') k--;
    digits[k] = 0;
    int n = (*c == 'e' ? atoi(c + 1) : 0) + 1;
    if (k <= n && n <= 21) {
        cstr_append_n(target, digits, k);
        for (int i = k; i < n; i++) cstr_append_lit(target, "0");
    } else if (0 < n && n <= 21) {
        cstr_append_n(target, digits, n);
        cstr_append_lit(target, ".");
        cstr_append_n(target, digits + n, k - n);
    } else if (-6 < n && n <= 0) {
        cstr_append_lit(target, "0.");
        for (int i = 0; i < -n; i++) cstr_append_lit(target, "0");
        cstr_append_n(target, digits, k);
    } else {
        cstr_append_n(target, digits, 1);
        if (k > 1) {
            cstr_append_lit(target, ".");
            cstr_append_n(target, digits + 1, k - 1);
        }
        cstr_append_fmt(target, "e%c%d", n - 1 < 0 ? '-' : '+', abs(n - 1));
    }
}

bool value_is_numeric_literal_name(csview name) {
    if (!name.size || name.size > 64) return false;
    char buf[65];
    memcpy(buf, name.buf, (size_t)name.size);
    buf[name.size] = 0;
    char* end = nullptr;
    double number = strtod(buf, &end);
    if (end != buf + name.size) return false;
    cstr printed = cstr_init();
    append_js_number(&printed, number);
    bool ret = csview_equals_sv(cstr_sv(&printed), name);
    cstr_drop(&printed);
    return ret;
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
            append_js_number(target, my number);
            break;

        case VALUE_SYMBOL:
            cstr_append_lit(target, "Symbol(");
            cstr_append_s(target, my symbol.get->str);
            cstr_append_lit(target, ")");
            break;

        case VALUE_BIGINT:
            bigint_append_decimal(target, my bigint);
            break;

        case VALUE_ARRAY:
            for_each (i, vec_value, &my array.get->vec) {
                if (i_index != 0) {
                    cstr_append_lit(target, ",");
                }
                cstr_append_value_js(target, i);
            }
            break;

        case VALUE_JS_FUNC:
            cstr_append_span(target, my js_func.get->impl->full_span);
            break;

        case VALUE_NATIVE_FUNC:
            cstr_append_lit(target, "function() {\n    [native code]\n}");
            break;
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
        case VALUE_SYMBOL: return a->symbol.get == b->symbol.get;
        case VALUE_BIGINT: {
            cstr x = cstr_init(), y = cstr_init();
            bigint_append_decimal(&x, a->bigint);
            bigint_append_decimal(&y, b->bigint);
            bool ret = cstr_eq(&x, &y);
            cstr_drop(&x);
            cstr_drop(&y);
            return ret;
        }
        case VALUE_OBJECT: return a->object.get == b->object.get;
        case VALUE_ARRAY: return a->array.get == b->array.get;
        case VALUE_JS_FUNC: return a->js_func.get == b->js_func.get;
        case VALUE_NATIVE_FUNC: return a->native_func.get == b->native_func.get;
    }
    impossible;
}

void object_item_drop(ObjectItem* me) {
    value_drop(&my value);
}

ObjectItem object_item_clone(ObjectItem me) {
    me.value = value_clone(me.value);
    return me;
}

void js_object_drop(JsObject* me) {
    hmap_object_item_drop(&my items);
}

JsObject js_object_clone(JsObject me) {
    me.items = hmap_object_item_clone(me.items);
    return me;
}

VecObjectItemRef js_object_sorted_items(JsObject* me) {
    VecObjectItemRef ret = vec_object_item_ref_with_capacity(my total_inserted);
    for_each_kv (name, item, hmap_object_item, &my items) {
        vec_object_item_ref_push(&ret, (ObjectItemRef){
            .name = name,
            .item = item,
        });
    }
    vec_object_item_ref_sort(&ret);
    return ret;
}

void js_object_set(JsObject* me, cstr key, Value val) {
    hmap_object_item_insert_or_assign(&my items, key, (ObjectItem){
        .value = val,
        .position = my total_inserted,
    });
    my total_inserted++;
}

Result js_object_get(JsObject const* me, csview key) {
    auto value = hmap_object_item_get(&my items, key);
    if (value) {
        return result_ok(value_clone(value->second.value));
    } else {
        return result_err(value_undefined);
    }
}

bool js_object_has(JsObject* me, csview key) {
    return hmap_object_item_get(&my items, key) != nullptr;
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
    ignore me;
}

void native_func_value_drop(NativeFuncValue* me) {
    if (my drop) {
        my drop(my data);
    }
}

// Base 10^9 limbs, least significant first
static u32 bigint_limbs(BigintValue value, u32** out) {
    char const* p = value.text;
    char const* end = value.text + value.len;
    u32 base = 10;
    if (end - p >= 2 && p[0] == '0' && strchr("xXoObB", p[1])) {
        base = (p[1] == 'x' || p[1] == 'X') ? 16 : (p[1] == 'o' || p[1] == 'O') ? 8 : 2;
        p += 2;
    }
    u32 cap = (u32)(end - p) / 2 + 2;
    u32* limbs = calloc(cap, sizeof(u32));
    panic_if(!limbs, "out of memory");
    u32 count = 0;
    for (; p < end; p++) {
        char c = *p;
        u32 digit;
        if (c >= '0' && c <= '9') digit = (u32)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (u32)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = (u32)(c - 'A' + 10);
        else continue; // (`_` separators)
        u64 carry = digit;
        for (u32 i = 0; i < count; i++) {
            u64 v = (u64)limbs[i] * base + carry;
            limbs[i] = (u32)(v % 1000000000u);
            carry = v / 1000000000u;
        }
        while (carry) {
            limbs[count++] = (u32)(carry % 1000000000u);
            carry /= 1000000000u;
        }
    }
    *out = limbs;
    return count;
}

bool bigint_is_zero(BigintValue value) {
    for (u32 i = 0; i < value.len; i++) {
        char c = value.text[i];
        if (i == 1 && value.text[0] == '0' && strchr("xXoObB", c)) continue;
        if (c != '0' && c != '_') return false;
    }
    return true;
}

void bigint_append_decimal(cstr* out, BigintValue value) {
    u32* limbs;
    u32 count = bigint_limbs(value, &limbs);
    if (!count) {
        cstr_append_lit(out, "0");
    } else {
        if (value.negative) cstr_append_lit(out, "-");
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", limbs[count - 1]);
        cstr_append_n(out, buf, (isize)strlen(buf));
        for (u32 i = count - 1; i-- > 0;) {
            snprintf(buf, sizeof(buf), "%09u", limbs[i]);
            cstr_append_n(out, buf, (isize)strlen(buf));
        }
    }
    free(limbs);
}
