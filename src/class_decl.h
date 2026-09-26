#pragma once

#include "function_impl.h"
#include "interface_member.h"
#include "lazy_type.h"

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
    bool interface_member_static; // CLASS_ITEM_INTERFACE_MEMBER: `static [k: string]: T`
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
            cstr name_string; // For a computed key, its source text: what accessor pairs and overloads are matched by
            Span name_span;
            bool name_is_value;
            Expr* computed_key; // Owned, `[k]() {}`. Null otherwise
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
            bool is_declare; // `declare x: T`: nothing is emitted, so it can't be decorated
            bool is_auto_accessor; // `accessor x = 1`
            cstr name_string; // See method.name_string
            Span name_span;
            bool name_is_value;
            Expr* computed_key; // Owned, `[k]: T`. Null otherwise
            bool has_explicit_type;
            Span explicit_type_span;
            TypeNode explicit_type;
            Expr value;
            LazyType lazy_type;
        } field;
        InterfaceMember interface_memeber;
        Block static_block;
    };
} ClassItem;

void class_item_drop(ClassItem* me);
bool class_item_is_static(ClassItem* me);
/// `#x` (not `"#x"` or `[k]`): the item's name_span, else an invalid span
Span class_item_private_name(ClassItem const* me);
Ty const* class_item_field_evaluate_type(ClassItem* me);
Ty const* class_item_accessor_evaluate_type(ClassItem* me);
Ty const* class_item_get_type(ClassItem* me);

#define g_class_key ClassItem, class_item
#define g_no_clone
#include <cleat/generic/vec.h>

// An anonymous class expression that the emitter names at runtime (tsc's isNamedEvaluationSource), set by the parser
typedef enum ClassNamedEvaluation {
    CLASS_NOT_NAMED_BY_CONTEXT,
    CLASS_NAMED_BY_CONTEXT, // `x = class {}`, `{ a: class {} }`, `export default class {}`...
    CLASS_NAMED_BY_COMPUTED_KEY, // `{ [k]: class {} }`, `[k] = class {}` in a class: named with __propKey
} ClassNamedEvaluation;

typedef struct ClassDecl {
    Span full_span;
    Span name;
    // An anonymous class expression: the name of what it's assigned to (tsc's getAssignedName: `const C = class {}`,
    // `{ C: class {} }`, `x.C = class {}`), which messages and baselines call it. Invalid: `(Anonymous class)`
    Span assigned_name;
    bool is_abstract;
    bool is_expression; // A class expression, which legacy decorators can't decorate the members of
    bool is_ambient; // `declare class`, or in an ambient context
    uint8_t named_evaluation; // ClassNamedEvaluation
    bool has_typechecked;
    VecExpr decorators; // TODO: only checked as expressions, not as decorators of this class
    Generic generic;
    Expr extends;
    Span extends_clause; // `extends A<T>`
    VecTypeNode implements;
    VecClassItem items;
    Symbol* symbol;
    struct ClassDecl const* outer_class; // The class this one is in (scope_enclosing_class()), set when resolving
} ClassDecl;

void class_decl_drop(ClassDecl* me);
/// What the class is called in messages and baselines: its name, else its assigned name, else `(Anonymous class)`
Span class_decl_display_name(ClassDecl const* me);
/// The first member of the class named `#name` (tsc's lookupSymbolForPrivateIdentifierDeclaration for one class)
ClassItem const* class_decl_find_private_name(ClassDecl const* me, csview name);
void class_decl_add_names(ClassDecl* me, Scope* scope, DeclExported exported, bool declared);
void class_decl_lookup_names(ClassDecl* me, Scope* names, ErrorCtx* errs);
void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs);
