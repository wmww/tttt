#pragma once

#include "prelude.h"
#include "function_impl.h"
#include "interface_member.h"

typedef enum AccessModifier {
    ACCESS_MODIFIER_PUBLIC,
    ACCESS_MODIFIER_PROTECTED,
    ACCESS_MODIFIER_PRIVATE,
} AccessModifier;

typedef struct ClassItem {
    enum {
        CLASS_ITEM_CONSTRUCTOR,
        CLASS_ITEM_MEMBER,
        CLASS_ITEM_FIELD,
        CLASS_ITEM_INTERFACE_MEMBER,
        CLASS_ITEM_STATIC_BLOCK,
    } tag;
    AccessModifier access;
    Span span;
    union {
        struct {
            Span name_span;
            FunctionImpl func;
        } constructor;
        struct {
            bool is_static;
            bool is_optional;
            cstr name_string;
            Span name_span;
            bool name_is_value;
            FunctionImpl func;
        } member;
        struct {
            bool is_static;
            bool is_readonly;
            bool is_optional;
            cstr name_string;
            Span name_span;
            bool name_is_value;
            Type type;
            Span type_span;
            bool type_explicit;
            Expr value;
        } field;
        InterfaceMember interface_memeber;
        Block static_block;
    };
} ClassItem;

void class_item_drop(ClassItem* me);

#define i_key_class_names ClassItem, class_item
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct ClassDecl {
    Span full_span;
    Span name;
    bool is_abstract;
    Generic generic;
    Type interface;
    Expr extends;
    VecType implements;
    CallableType constructor_type;
    VecClassItem items;
    ArcNamedType type_ref;
} ClassDecl;

void class_decl_drop(ClassDecl* me);
void cstr_append_class_decl_debug(cstr* target, ClassDecl const* me, int indent);
void class_decl_resolve_names(ClassDecl* me, NameStack* names, ErrorCtx* errs);
void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs);
