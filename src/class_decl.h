#pragma once

#include "function_impl.h"
#include "interface_member.h"
#include "declaration.h"

typedef enum AccessModifier {
    ACCESS_MODIFIER_PUBLIC,
    ACCESS_MODIFIER_PROTECTED,
    ACCESS_MODIFIER_PRIVATE,
} AccessModifier;

typedef struct ClassItem {
    enum {
        CLASS_ITEM_CONSTRUCTOR,
        CLASS_ITEM_METHOD,
        CLASS_ITEM_FIELD,
        CLASS_ITEM_INTERFACE_MEMBER,
        CLASS_ITEM_STATIC_BLOCK,
    } tag;
    AccessModifier access;
    Span span;
    VecExpr decorators; // TODO: only checked as expressions, not as decorators of this member
    union {
        struct {
            Span name_span;
            FunctionImpl func;
        } constructor;
        struct {
            bool is_static;
            bool is_optional;
            bool is_abstract;
            cstr name_string;
            Span name_span;
            bool name_is_value;
            FunctionImpl func;
            // Accessors (`get x() {}`/`set x(v) {}`) are methods that show up as a property of accessor_type
            enum {
                CLASS_ACCESSOR_NONE = 0,
                CLASS_ACCESSOR_GET,
                CLASS_ACCESSOR_SET,
            } accessor;
            LazyType accessor_type;
            // The first accessor in the class with the same name and staticness (possibly this one) and, for setters,
            // the getter (if any). Set when looking up names.
            struct ClassItem* accessor_first;
            struct ClassItem* accessor_getter;
        } method;
        struct {
            bool is_static;
            bool is_readonly;
            bool is_optional;
            bool is_abstract;
            cstr name_string;
            Span name_span;
            bool name_is_value;
            bool has_explicit_type;
            Span explicit_type_span;
            Type explicit_type;
            Expr value;
            LazyType lazy_type;
        } field;
        InterfaceMember interface_memeber;
        Block static_block;
    };
} ClassItem;

void class_item_drop(ClassItem* me);
bool class_item_is_static(ClassItem* me);
Type class_item_field_evaluate_type(ClassItem* me);
Type class_item_accessor_evaluate_type(ClassItem* me);
Type class_item_get_type(ClassItem* me);

#define g_class_key ClassItem, class_item
#define g_no_clone
#include <cleat/generic/vec.h>

typedef struct ClassDecl {
    Span full_span;
    Span name;
    bool is_abstract;
    bool has_typechecked;
    VecExpr decorators; // TODO: only checked as expressions, not as decorators of this class
    Generic generic;
    Expr extends;
    VecType implements;
    VecClassItem items;
    LazyType instance_type;
    LazyType class_type;
    Declaration typeof_declaration;
    ArcDeclaration declaration;
    Type instance_identifier_type;
    VecType miscellaneous_owned_types;
} ClassDecl;

void class_decl_drop(ClassDecl* me);
void class_decl_add_names(ClassDecl* me, Scope* scope, bool exported);
void class_decl_lookup_names(ClassDecl* me, ScopeStack* names, ErrorCtx* errs);
Type class_decl_evaluate_type(ClassDecl* me, bool resolve_instance);
void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs);
