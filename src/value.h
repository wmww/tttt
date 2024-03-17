#pragma once

#include "prelude.h"

typedef struct Type Type;
typedef struct Block Block;
typedef struct FunctionArg FunctionArg;
typedef struct Value Value;
typedef struct Engine Engine;
typedef struct Result Result;
typedef struct ObjectItem ObjectItem;
typedef struct JsObject JsObject;
typedef struct JsArray JsArray;
typedef struct JsFuncValue JsFuncValue;
typedef struct NativeFuncValue NativeFuncValue;

wrap_forward_hmap_str(ObjectItem, object_item);
wrap_forward_arc(JsObject, js_object);
wrap_forward_arc(JsArray, js_array);
wrap_forward_arc(JsFuncValue, js_func_value);
wrap_forward_arc(NativeFuncValue, native_func_value);
wrap_forward_vec(FunctionArg, function_arg);

typedef struct Value {
    enum {
        VALUE_UNDEFINED,
        VALUE_NULL,
        VALUE_BOOLEAN,
        VALUE_NUMBER,
        VALUE_STRING,
        VALUE_OBJECT,
        VALUE_ARRAY,
        VALUE_JS_FUNC,
        VALUE_NATIVE_FUNC,
    } tag;
    union {
        bool boolean;
        double number;
        cstr string;
        // TODO: const non-ref variant of object for perf during type checking
        ArcJsObject object;
        ArcJsArray array;
        ArcJsFuncValue js_func;
        ArcNativeFuncValue native_func;
    };
} Value;

void value_drop_internal(Value* me);
static inline void value_drop(Value* me) {
    switch (me->tag) {
        case VALUE_NULL:
        case VALUE_UNDEFINED:
        case VALUE_BOOLEAN:
        case VALUE_NUMBER:
            break;

        case VALUE_STRING:
        case VALUE_OBJECT:
        case VALUE_ARRAY:
        case VALUE_JS_FUNC:
        case VALUE_NATIVE_FUNC:
            value_drop_internal(me);
            break;
    }
    me->tag = VALUE_NULL; // Makes repeated calls to drop safe
}
Value value_clone(Value me);
bool value_to_boolean(Value const* me);
double value_to_number(Value const* me);
void cstr_append_value_js(cstr* target, Value const* me);
void cstr_append_value_debug(cstr* target, Value const* me, int indent);
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

#define i_key_names ObjectItemRef, object_item_ref
#define i_more
// TODO: make positive numeric keys go first
#define i_cmp(a, b) c_default_cmp(&a->item->position, &b->item->position)
#include "wrap_stc/vec.h"
#include "wrap_stc/quicksort.h"

void object_item_drop(ObjectItem* me);
ObjectItem object_item_clone(ObjectItem me);

typedef struct JsObject {
    HmapStrObjectItem items;
    int total_inserted;
} JsObject;

void js_object_drop(JsObject* me);
JsObject js_object_clone(JsObject me);
// NOTE: returned list is invalidated by changes to object and should be dropped before then
VecObjectItemRef js_object_sorted_items(JsObject* me);

void js_object_set(JsObject* me, cstr key, Value val);
// On err .value is set to undefined, so you can ignore .is_err if you want the normal javascript behavior
// Also means result doesn't need to be dropped as long as value is handled in the ok case
Result js_object_get(JsObject* me, csview key);
bool js_object_has(JsObject* me, csview key);

#define i_key_ssv
#define i_val_class_names ObjectItem, object_item
#define i_is_forward
#include "wrap_stc/hmap.h"

#define i_key_class_names JsObject, js_object
#define i_opt c_no_atomic
#define i_is_forward
#define i_no_hash
#include "wrap_stc/arc.h"

#define i_key_class_names Value, value
#include "wrap_stc/vec.h"

// Array

typedef struct JsArray {
    JsObject fallback;
    VecValue vec;
} JsArray;

void js_array_drop(JsArray* me);
JsArray js_array_clone(JsArray me);

#define i_key_class_names JsArray, js_array
#define i_opt c_no_atomic
#define i_is_forward
#define i_no_hash
#include "wrap_stc/arc.h"

// JsFunc

typedef struct JsFuncValue {
    VecFunctionArg const* args; // non-owning
    Block const* code; // non-owning
} JsFuncValue;

void js_func_value_drop(JsFuncValue* me);

#define i_key_class_names JsFuncValue, js_func_value
#define i_opt c_no_atomic
#define i_is_forward
#define i_no_hash
#include "wrap_stc/arc.h"

// NativeFunc

typedef struct NativeFuncValue {
    Result (*func)(Engine*, int, Value const*, void*);
    void (*drop)(void*);
    void* data;
} NativeFuncValue;

void native_func_value_drop(NativeFuncValue* me);

#define i_key_class_names NativeFuncValue, native_func_value
#define i_opt c_no_atomic
#define i_is_forward
#define i_no_hash
#include "wrap_stc/arc.h"

// Builders

#define value_undefined ((Value){ .tag = VALUE_UNDEFINED })
#define value_null ((Value){ .tag = VALUE_NULL })
static inline Value value_boolean(bool value) { return (Value){ .tag = VALUE_BOOLEAN, .boolean = value }; }
static inline Value value_number(double value) { return (Value){ .tag = VALUE_NUMBER, .number = value }; }
static inline Value value_string(cstr value) { return (Value){ .tag = VALUE_STRING, .string = value }; }

static inline Value value_object_from_n(ObjectItemInit* items, int n) {
    JsObject data = {
        .items = hmap_str_object_item_init(),
        .total_inserted = n,
    };
    for (int i = 0; i < n; i++) {
        hmap_str_object_item_insert_or_assign(
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

static inline Value value_object_empty() {
    return value_object_from_n(nullptr, 0);
}

#define value_object_init(...) \
    value_object_from_n((ObjectItemInit[])__VA_ARGS__, c_arraylen(((ObjectItemInit[])__VA_ARGS__)))

static inline Value value_array_empty() {
    return (Value){
        .tag = VALUE_ARRAY,
        .array = arc_js_array_make((JsArray){0}),
    };
}

static inline Value value_js_func(VecFunctionArg const* args, Block const* code) {
    return (Value){
        .tag = VALUE_JS_FUNC,
        .js_func = arc_js_func_value_make((JsFuncValue){
            .args = args,
            .code = code,
        }),
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
