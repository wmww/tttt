#pragma once

#include "prelude.h"
#include "function_impl.h"
#include "statement.h" // for NameInitData

typedef struct ClassItem {
    enum {
        CLASS_ITEM_CONSTRUCTOR,
        CLASS_ITEM_MEMBER,
        CLASS_ITEM_FIELD,
    } tag;
    struct {
        FunctionImpl constructor;
        struct {
            cstr name;
            Span name_span;
            FunctionImpl func;
        } member;
        NameInitData field;
    };
} ClassItem;

void class_item_drop(ClassItem* me);

#define i_key_class_names ClassItem, class_item
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct ClassDecl {
    cstr name;
    Span name_span;
    VecClassItem items;
} ClassDecl;

void class_decl_drop(ClassDecl* me);
void cstr_append_class_decl_debug(cstr* target, ClassDecl const* me, int indent);
