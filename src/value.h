#pragma once

#include "prelude.h"

typedef struct TypeNode TypeNode;
typedef struct Block Block;
typedef struct FunctionImpl FunctionImpl;
typedef struct Value Value;
typedef struct Engine Engine;
typedef struct Result Result;
typedef struct ObjectItem ObjectItem;
typedef struct JsObject JsObject;
typedef struct JsArray JsArray;
typedef struct JsFuncValue JsFuncValue;
typedef struct NativeFuncValue NativeFuncValue;
typedef struct Binding Binding;
typedef struct InitializedBinding InitializedBinding;

typedef struct SymbolValue {
    cstr str;
} SymbolValue;
static inline void symbol_value_drop(SymbolValue* me) {
    cstr_drop(&my str);
}
#define g_class_key SymbolValue, symbol_value
#define g_opt (c_no_atomic)
#include <cleat/generic/arc.h>

g_declare_hmap(SvStr, ObjectItem, object_item);
g_declare_arc(JsObject, js_object);
g_declare_arc(JsArray, js_array);
g_declare_arc(JsFuncValue, js_func_value);
g_declare_arc(NativeFuncValue, native_func_value);
g_declare_vec(Binding, binding);
g_declare_vec(InitializedBinding, initialized_binding);

// A bigint as written (`0x1_F`, without the `n`), which lives as long as the program's text (or a checker's atoms), and
// a sign: exact at any size, printed and compared in decimal (bigint_append_decimal())
typedef struct BigintValue {
    char const* text;
    u32 len;
    bool negative;
} BigintValue;

typedef struct Value {
    enum {
        VALUE_UNDEFINED,
        VALUE_NULL,
        VALUE_BOOLEAN,
        VALUE_NUMBER,
        VALUE_STRING,
        VALUE_SYMBOL,
        VALUE_BIGINT,
        VALUE_OBJECT,
        VALUE_ARRAY,
        VALUE_JS_FUNC,
        VALUE_NATIVE_FUNC,
    } tag;
    union {
        bool boolean;
        double number;
        cstr string;
        ArcSymbolValue symbol;
        BigintValue bigint;
        ArcJsObject object; // PERF: const non-ref variant of object for perf during type checking
        ArcJsArray array;
        ArcJsFuncValue js_func;
        ArcNativeFuncValue native_func;
    };
} Value;

void value_drop_internal(Value* me);
static inline void value_drop(Value* me) {
    switch (my tag) {
        case VALUE_UNDEFINED:
        case VALUE_NULL:
        case VALUE_BOOLEAN:
        case VALUE_NUMBER:
            break;

        case VALUE_STRING:
        case VALUE_SYMBOL:
        case VALUE_BIGINT:
        case VALUE_OBJECT:
        case VALUE_ARRAY:
        case VALUE_JS_FUNC:
        case VALUE_NATIVE_FUNC:
            value_drop_internal(me);
            break;
    }
    my tag = VALUE_UNDEFINED; // Makes repeated calls to drop safe
}
Value value_clone(Value me);
bool value_to_boolean(Value const* me);
double value_to_number(Value const* me);
// `(+name).toString() === name` (tsc's isNumericLiteralName): "1", "1.1", "1e+21", not "01" or "1.0"
bool value_is_numeric_literal_name(csview name);
void cstr_append_value_js(cstr* target, Value const* me);
bool value_loose_eq(Value const* a, Value const* b);
bool value_strict_eq(Value const* a, Value const* b);

// Object

typedef struct ObjectItem {
    int position;
    Value value;
} ObjectItem;

typedef struct ObjectItemRef {
    cstr* name;
    ObjectItem* item;
} ObjectItemRef;

typedef struct ObjectItemInit {
    const char* name;
    Value value;
} ObjectItemInit;

#define g_key ObjectItemRef, object_item_ref
// TODO: make positive numeric keys go first
#define g_cmp(a, b) c_default_cmp(&a->item->position, &b->item->position)
#include <cleat/generic/vec.h>

void object_item_drop(ObjectItem* me);
ObjectItem object_item_clone(ObjectItem me);

typedef struct JsObject {
    HMapObjectItem items;
    int total_inserted;
} JsObject;

void js_object_drop(JsObject* me);
JsObject js_object_clone(JsObject me);
// NOTE: returned list is invalidated by changes to object and should be dropped before then
VecObjectItemRef js_object_sorted_items(JsObject* me);

void js_object_set(JsObject* me, cstr key, Value val);
// On err .value is set to undefined, so you can ignore .is_err if you want the normal javascript behavior
// Also means result doesn't need to be dropped as long as value is handled in the ok case
Result js_object_get(JsObject const* me, csview key);
bool js_object_has(JsObject* me, csview key);

#define g_pro_key SvStr, svstr
#define g_class_val ObjectItem, object_item
#define g_declared
#include <cleat/generic/hmap.h>

#define g_class_key JsObject, js_object
#define g_opt (c_no_atomic)
#define g_declared
#define g_no_hash
#include <cleat/generic/arc.h>

#define g_class_key Value, value
#include <cleat/generic/vec.h>

// Array

typedef struct JsArray {
    JsObject fallback;
    VecValue vec;
} JsArray;

void js_array_drop(JsArray* me);
JsArray js_array_clone(JsArray me);

#define g_class_key JsArray, js_array
#define g_opt (c_no_atomic)
#define g_declared
#define g_no_hash
#include <cleat/generic/arc.h>

// JsFunc

typedef struct JsFuncValue {
    FunctionImpl const* impl; // non-owning
} JsFuncValue;

void js_func_value_drop(JsFuncValue* me);

#define g_class_key JsFuncValue, js_func_value
#define g_opt (c_no_atomic)
#define g_declared
#define g_no_hash
#include <cleat/generic/arc.h>

// NativeFunc

typedef struct NativeFuncValue {
    Result (*func)(Engine*, int, Value const*, void*);
    void (*drop)(void*);
    void* data;
} NativeFuncValue;

void native_func_value_drop(NativeFuncValue* me);

#define g_class_key NativeFuncValue, native_func_value
#define g_opt (c_no_atomic)
#define g_declared
#define g_no_hash
#include <cleat/generic/arc.h>

// Builders

#define value_undefined ((Value){ .tag = VALUE_UNDEFINED })
#define value_null ((Value){ .tag = VALUE_NULL })
static inline Value value_boolean(bool value) { return (Value){ .tag = VALUE_BOOLEAN, .boolean = value }; }
static inline Value value_number(double value) { return (Value){ .tag = VALUE_NUMBER, .number = value }; }
// PERF: switch to value_sv() and value_cstr() and use value_sv() wherever possible
static inline Value value_string(cstr value) { return (Value){ .tag = VALUE_STRING, .string = value }; }

static inline Value value_object_from_n(ObjectItemInit* items, int n) {
    JsObject data = {
        .items = hmap_object_item_init(),
        .total_inserted = n,
    };
    for (int i = 0; i < n; i++) {
        hmap_object_item_insert_or_assign(
            &data.items,
            cstr_from(items[i].name),
            (ObjectItem){
                .position = i,
                .value = items[i].value,
            });
    }
    return (Value){
        .tag = VALUE_OBJECT,
        .object = arc_js_object_make(data),
    };
}

static inline Value value_object_make_empty() {
    return value_object_from_n(nullptr, 0);
}

#define value_object_init(...) \
    value_object_from_n((ObjectItemInit[])__VA_ARGS__, c_arraylen(((ObjectItemInit[])__VA_ARGS__)))

static inline Value value_array_make_empty() {
    return (Value){
        .tag = VALUE_ARRAY,
        .array = arc_js_array_make((JsArray){0}),
    };
}

static inline Value value_js_func(FunctionImpl const* impl) {
    return (Value){
        .tag = VALUE_JS_FUNC,
        .js_func = arc_js_func_value_make((JsFuncValue){ .impl = impl }),
    };
}

static inline Value value_native_func(
    Result (*func)(Engine*, int, Value const*, void*),
    void (*drop)(void*),
    void* data
) {
    return (Value){
        .tag = VALUE_NATIVE_FUNC,
        .native_func = arc_native_func_value_make((NativeFuncValue){
            .func = func,
            .drop = drop,
            .data = data,
        }),
    };
}

// The decimal digits of a bigint, `-` first when negative and not zero
void bigint_append_decimal(cstr* out, BigintValue value);
// The literal as tsgo's scanner keeps it, its property name (`0XFF` is "0xffn", `0b11` "3n")
void bigint_append_token_value(cstr* out, BigintValue value);
bool bigint_is_zero(BigintValue value);
static inline Value value_bigint(char const* text, u32 len, bool negative) {
    return (Value){ .tag = VALUE_BIGINT, .bigint = { .text = text, .len = len, .negative = negative } };
}
static inline Value value_bigint_negated(Value const* me) {
    Value ret = *me;
    ret.bigint.negative = !ret.bigint.negative;
    return ret;
}
