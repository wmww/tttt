#pragma once

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
            bool has_explicit_type;
            Span explicit_type_span;
            Type explicit_type;
            Expr value;
        } field;
        InterfaceMember interface_memeber;
        Block static_block;
    };
} ClassItem;

void class_item_drop(ClassItem* me);
bool class_item_is_static(ClassItem* me);
Type class_item_get_type(ClassItem* me);

#define i_key_class_names ClassItem, class_item
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct ClassDecl {
    Span full_span;
    Span name;
    bool is_abstract;
    bool has_typechecked;
    Generic generic;
    Expr extends;
    VecType implements;
    VecClassItem items;
    LazyType instance_type;
    LazyType class_type;
    // Perf: do these all need to be separate allocations?
    NamedType* instance_named_type;
    NamedType* class_named_type;
    NamedVariable* instance_named_var;
    NamedVariable* class_named_var;
    LazyType instance_identifier_type;
    LazyType class_identifier_type;
} ClassDecl;

void class_decl_drop(ClassDecl* me);
void class_decl_add_names(ClassDecl* me, NameStack* names, ErrorCtx* errs);
void class_decl_resolve_names(ClassDecl* me, NameStack* names, ErrorCtx* errs);
Type class_decl_evaluate_type(ClassDecl* me, bool resolve_instance);
void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs);
