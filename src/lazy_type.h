#pragma once

#include "type.h"

typedef struct Expr Expr;
typedef struct FunctionImpl FunctionImpl;
typedef struct ClassDecl ClassDecl;
typedef struct TypeDecl TypeDecl;
typedef struct LazyType LazyType;

wrap_forward_arc(LazyType, lazy_type);

typedef struct LazyType {
    enum {
        LAZY_TYPE_VALUE = 0,
        LAZY_TYPE_REF,
        LAZY_TYPE_IN_FLIGHT,
        LAZY_TYPE_EXPR,
        LAZY_TYPE_FUNCTION,
        LAZY_TYPE_CLASS_INSTANCE,
        LAZY_TYPE_CLASS_CLASS,
        LAZY_TYPE_INTERFACE,
        LAZY_TYPE_INDEXED,
    } tag;
    union {
        Type value;
        Type const* ref;
        struct {
            // PERF: figure out how to not always calculate types for expressions twice
            Expr const* ptr;
            bool abstract_literals;
        } expr;
        FunctionImpl* function;
        ClassDecl* class;
        TypeDecl* interface;
        struct {
            ArcLazyType source;
            Type* index; // Owned
        } indexed;
    };
} LazyType;

static inline LazyType lazy_type_value(Type value) {
    return (LazyType){
        .tag = LAZY_TYPE_VALUE,
        .value = value,
    };
}

static inline LazyType lazy_type_ref(Type const* ref) {
    ASSERT(ref);
    return (LazyType){
        .tag = LAZY_TYPE_REF,
        .ref = ref,
    };
}

static inline LazyType lazy_type_expr(Expr const* expr, bool abstract_literals) {
    return (LazyType){
        .tag = LAZY_TYPE_EXPR,
        .expr = {
            .ptr = expr,
            .abstract_literals = abstract_literals,
        },
    };
}

static inline LazyType lazy_type_function(FunctionImpl* function) {
    return (LazyType){
        .tag = LAZY_TYPE_FUNCTION,
        .function = function,
    };
}

static inline LazyType lazy_type_class_instance(ClassDecl* class) {
    return (LazyType){
        .tag = LAZY_TYPE_CLASS_INSTANCE,
        .class = class,
    };
}

static inline LazyType lazy_type_class_class(ClassDecl* class) {
    return (LazyType){
        .tag = LAZY_TYPE_CLASS_CLASS,
        .class = class,
    };
}

LazyType lazy_type_interface(TypeDecl* interface);
LazyType lazy_type_indexed(ArcLazyType source, Value index);

void lazy_type_drop(LazyType* me);
// PERF: remove lazy_type_clone(), cloning implies lazy types may be resolved multiple times. code should be
// re-structured so this isn't required. if this isn't possible, a variant of LazyType that holds an arc to another
// LazyType could be implemented, and any LazyType that is cloned could be converted to that.
LazyType lazy_type_clone(LazyType me);
void cstr_append_lazy_type_debug(cstr* target, LazyType const* me, int indent);
void cstr_append_lazy_type_error_msg(cstr* target, LazyType const* me);
Type const* lazy_type_resolve(LazyType* me);

// May return NULL
static inline Type const* lazy_type_try_get(LazyType const* me) {
    switch (my tag) {
        case LAZY_TYPE_VALUE: return &my value;
        case LAZY_TYPE_REF: return my ref;
        default: return nullptr;
    }
}

static inline Type const* lazy_type_assert_get(LazyType const* me) {
    switch (my tag) {
        case LAZY_TYPE_VALUE: return &my value;
        case LAZY_TYPE_REF: return my ref;
        default:
            NONFATAL("Tried to get unresolved lazy type with tag %d", my tag);
            return &type_unknown;
    }
}

#define i_key_class_names LazyType, lazy_type
#define i_opt c_no_atomic
#define i_is_forward
#define i_no_hash
#include "wrap_stc/arc.h"
