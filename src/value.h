#pragma once

#include "prelude.h"

typedef struct Type Type;
typedef struct Block Block;
typedef struct FunctionArg FunctionArg;
typedef struct Value Value;
typedef struct Engine Engine;
typedef struct Result Result;
typedef struct ObjectValueItem ObjectValueItem;
typedef struct ObjectValue ObjectValue;
typedef struct ArrayValue ArrayValue;
typedef struct JsFuncValue JsFuncValue;
typedef struct NativeFuncValue NativeFuncValue;

forward_hmap(HmapStrObjectValueItem, cstr, ObjectValueItem);
forward_arc(ArcObject, ObjectValue);
forward_arc(ArcArray, ArrayValue);
forward_arc(ArcJsFunc, JsFuncValue);
forward_arc(ArcNativeFunc, NativeFuncValue);
forward_vec(VecFunctionArg, FunctionArg);

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
        ArcObject object;
        ArcArray array;
        ArcJsFunc js_func;
        ArcNativeFunc native_func;
    };
} Value;

void value_drop_internal(Value* self);
static inline void value_drop(Value* self) {
    switch (self->tag) {
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
            value_drop_internal(self);
            break;
    }
    self->tag = VALUE_NULL; // Makes repeated calls to drop safe
}
Value value_clone(Value self);
bool value_to_boolean(Value const* self);
double value_to_number(Value const* self);
void cstr_append_value_js(cstr* target, Value const* self);
void cstr_append_value_debug(cstr* target, Value const* self, int indent);
bool value_loose_eq(Value const* a, Value const* b);
bool value_strict_eq(Value const* a, Value const* b);

// Object

typedef struct ObjectValueItem {
    int position;
    Value value;
} ObjectValueItem;

typedef struct ObjectItemRef {
    cstr* name;
    ObjectValueItem* item;
} ObjectItemRef;

typedef struct ObjectItemInit {
    const char* name;
    Value value;
} ObjectItemInit;

#define i_key ObjectItemRef
#define i_more
// TODO: make purel positive numeric keys go first
#define i_cmp(a, b) c_default_cmp(&a->item->position, &b->item->position)
#include "stc/vec.h"
#include "stc/algo/quicksort.h"

void object_value_item_drop(ObjectValueItem* self);
ObjectValueItem object_value_item_clone(ObjectValueItem self);

typedef struct ObjectValue {
    HmapStrObjectValueItem items;
    int total_inserted;
} ObjectValue;

void object_value_drop(ObjectValue* self);
ObjectValue object_value_clone(ObjectValue self);
// NOTE: returned list is invalidated by changes to object and should be dropped before then
VecObjectItemRef object_value_sorted_items(ObjectValue* self);

void object_value_set(ObjectValue* self, cstr key, Value val);
// On err .value is set to undefined, so you can ignore .is_err if you want the normal javascript behavior
// Also means result doesn't need to be dropped as long as value is handled in the ok case
Result object_value_get(ObjectValue* self, csview key);
bool object_value_has(ObjectValue* self, csview key);

#define i_type HmapStrObjectValueItem
#define i_key_ssv
#define i_val_class ObjectValueItem
#define i_is_forward
#include "stc/hmap.h"

#define i_tag Object
#define i_key_class ObjectValue
#define i_opt c_no_atomic
#define i_is_forward
#define i_no_hash
#include "stc/arc.h"

#define i_key_class Value
#include "stc/vec.h"

// Array

typedef struct ArrayValue {
    ObjectValue fallback;
    VecValue vec;
} ArrayValue;

void array_value_drop(ArrayValue* self);
ArrayValue array_value_clone(ArrayValue self);

#define i_tag Array
#define i_key_class ArrayValue
#define i_opt c_no_atomic
#define i_is_forward
#define i_no_hash
#include "stc/arc.h"

// JsFunc

typedef struct JsFuncValue {
    VecFunctionArg const* args; // non-owning
    Block const* code; // non-owning
} JsFuncValue;

void js_func_value_drop(JsFuncValue* self);

#define i_tag JsFunc
#define i_key_class JsFuncValue
#define i_opt c_no_atomic
#define i_is_forward
#define i_no_hash
#include "stc/arc.h"

// NativeFunc

typedef struct NativeFuncValue {
    Result (*func)(Engine*, int, Value const*, void*);
    void (*drop)(void*);
    void* data;
} NativeFuncValue;

void native_func_value_drop(NativeFuncValue* self);

#define i_tag NativeFunc
#define i_key_class NativeFuncValue
#define i_opt c_no_atomic
#define i_is_forward
#define i_no_hash
#include "stc/arc.h"

// Builders

#define value_undefined ((Value){ .tag = VALUE_UNDEFINED })
#define value_null ((Value){ .tag = VALUE_NULL })
static inline Value value_boolean(bool value) { return (Value){ .tag = VALUE_BOOLEAN, .boolean = value }; }
static inline Value value_number(double value) { return (Value){ .tag = VALUE_NUMBER, .number = value }; }
static inline Value value_string(cstr value) { return (Value){ .tag = VALUE_STRING, .string = value }; }

static inline Value value_object_from_n(ObjectItemInit* items, int n) {
    ObjectValue data = {
        .items = hmap_str_object_value_item_init(),
        .total_inserted = n,
    };
    for (int i = 0; i < n; i++) {
        hmap_str_object_value_item_insert_or_assign(
            &data.items,
            cstr_from(items[i].name),
            (ObjectValueItem){
                .position = i,
                .value = items[i].value,
            });
    }
    return (Value){
        .tag = VALUE_OBJECT,
        .object = arc_object_make(data),
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
        .array = arc_array_make((ArrayValue){0}),
    };
}

static inline Value value_js_func(VecFunctionArg const* args, Block const* code) {
    return (Value){
        .tag = VALUE_JS_FUNC,
        .js_func = arc_js_func_make((JsFuncValue){
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
        .native_func = arc_native_func_make((NativeFuncValue){
            .func = func,
            .drop = drop,
            .data = data,
        }),
    };
}
