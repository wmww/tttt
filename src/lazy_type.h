#pragma once

#include "type.h"

typedef struct LazyType {
    enum {
        // Makes it easy to show a warning if a LazyType is accidentally zero-initialized, otherwise shouldn't be used
        LAZY_TYPE_UNINITIALIZED = 0,

        // The final type is already cached
        LAZY_TYPE_EVALUATED,

        // The rest means this LazyType is part of one of the following structs
        LAZY_TYPE_EXPR,
        LAZY_TYPE_INITIALIZED_BINDING,
        LAZY_TYPE_FOR_EACH_LOOP_VAR,
        LAZY_TYPE_FUNCTION,
        LAZY_TYPE_CLASS_INSTANCE,
        LAZY_TYPE_CLASS_CLASS,
        LAZY_TYPE_CLASS_ITEM_FIELD,
        LAZY_TYPE_ENUM_INSTANCE,
        LAZY_TYPE_ENUM_INTERFACE,
        LAZY_TYPE_INTERFACE_DECL,
        LAZY_TYPE_IDENTIFIER_TYPE,
        LAZY_TYPE_MERGED,
        LAZY_TYPE_SCOPE_VALUE_INTERFACE,
    } tag;
    bool evaluation_started;
    bool is_self_referential;
    Type evaluated; // Should be {0} when tag is not LAZY_TYPE_EVALUATED
} LazyType;

// Should only be called via lazy_type_get()
void lazy_type_evaluate(LazyType* me);

static inline void lazy_type_drop(LazyType* me) {
    if (my tag == LAZY_TYPE_EVALUATED) {
        type_drop(&my evaluated);
    }
}

/*
 * This implementation is valid, but lazy types being clonable implies they are not part of the AST. this means pointers
 * to them could become invalid, which is unexpected if they are added to the name stack. Somehow make sure any non-AST
 * lazy types are not added to the name stack
 *
static inline LazyType lazy_type_clone(LazyType me) {
    switch (me.tag) {
        case LAZY_TYPE_EVALUATED:
            me.evaluated = type_clone(me.evaluated);
            break;

        default:
            log_error("LazyType with tag ", (i32)me.tag, " should not be cloned");
            break;
    }
    return me;
}
*/

static inline LazyType lazy_type_preeval(Type type) {
    return (LazyType){
        .tag = LAZY_TYPE_EVALUATED,
        .evaluated = type,
    };
}

static inline Type const* lazy_type_get(LazyType* me) {
    if (my tag != LAZY_TYPE_EVALUATED) {
        lazy_type_evaluate(me);
    }
    return &my evaluated;
}
