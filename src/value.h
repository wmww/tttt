#pragma once

#include "prelude.h"

typedef struct Type Type;
typedef struct Value Value;
typedef struct ObjectValueItem ObjectValueItem;
typedef struct ObjectValue ObjectValue;

forward_hmap(hmap_str_ObjectValueItem, cstr, ObjectValueItem);
forward_arc(arc_Object, ObjectValue);

typedef struct Value {
    enum {
        VALUE_NULL,
        VALUE_UNDEFINED,
        VALUE_BOOLEAN,
        VALUE_NUMBER,
        VALUE_STRING,
        VALUE_OBJECT,
    } tag;
    union {
        bool boolean;
        double number;
        cstr string;
        // TODO: const non-ref variant of object for perf during type checking
        arc_Object object;
    } data;
} Value;

void Value_drop(Value* self);
Value Value_clone(Value self);
cstr Value_to_cstr(Value const* self, int indent);
bool Value_strict_eq(Value const* a, Value const* b);

typedef struct ObjectValueItem {
    int position;
    Value value;
} ObjectValueItem;

typedef struct ObjectItemRef {
    cstr* name;
    ObjectValueItem* item;
} ObjectItemRef;

#define i_key ObjectItemRef
#define i_more
#define i_cmp(a, b) c_default_cmp(&a->item->position, &b->item->position)
#include "stc/vec.h"
#include "stc/algo/quicksort.h"

void ObjectValueItem_drop(ObjectValueItem* self);
ObjectValueItem ObjectValueItem_clone(ObjectValueItem self);

typedef struct ObjectValue {
    hmap_str_ObjectValueItem items;
    int total_inserted;
} ObjectValue;

void ObjectValue_drop(ObjectValue* self);
ObjectValue ObjectValue_clone(ObjectValue self);
// NOTE: returned list is invalidated by changes to object and should be dropped before then
vec_ObjectItemRef ObjectValue_sorted_items(ObjectValue* self);

#define i_type hmap_str_ObjectValueItem
#define i_key_str
#define i_val_class ObjectValueItem
#define i_is_forward
#include "stc/hmap.h"

#define i_tag Object
#define i_key_class ObjectValue
#define i_opt c_no_atomic
#define i_is_forward
#define i_no_hash
#include "stc/arc.h"

static inline Value Value_null_new() { return (Value){ .tag = VALUE_NULL }; }
static inline Value Value_undefined_new() { return (Value){ .tag = VALUE_UNDEFINED }; }
static inline Value Value_boolean_new(bool value) { return (Value){ .tag = VALUE_BOOLEAN, .data.boolean = value }; }
static inline Value Value_number_new(double value) { return (Value){ .tag = VALUE_NUMBER, .data.number = value }; }
static inline Value Value_string_new(cstr value) { return (Value){ .tag = VALUE_NUMBER, .data.string = value }; }
static inline Value Value_object_new() {
    return (Value){
        .tag = VALUE_OBJECT,
        .data.object = arc_Object_make((ObjectValue){
            .items = hmap_str_ObjectValueItem_init(),
            .total_inserted = 0,
        }),
    };
}
