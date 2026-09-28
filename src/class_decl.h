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
    bool is_override; // `override`, or in JavaScript a JSDoc `@override` (fields, methods, accessors)
    bool interface_member_static; // CLASS_ITEM_INTERFACE_MEMBER: `static [k: string]: T`
    Span span;
    VecExpr decorators; // Checked as calls by class_decl_typecheck() (ty_check_decorator())
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
            // JavaScript, without a type or initializer: the member the constructor's `this.x = v` declares, whose type
            // it has (tsc's isAutoTypedProperty). Set by the resolve walk
            struct Symbol const* js_assigned;
            // TypeScript, without a type or initializer: the constructor (a static block, for a static field) assigns
            // `this.x` directly, which types the field under noImplicitAny (tsc's getFlowTypeInConstructor /
            // getFlowTypeInStaticBlocks) instead of it being implicitly any. The first such `this.x`, which the flow
            // query at the end of the constructor / static blocks goes by, and the class. Set by the resolve walk
            Expr const* ts_assigned;
            struct ClassDecl const* ts_assigned_class;
        } field;
        InterfaceMember interface_memeber;
        struct {
            Block static_block;
            FlowNode const* static_block_return_flow; // Every way out, like FunctionImpl.return_flow
        };
    };
} ClassItem;

void class_item_drop(ClassItem* me);
bool class_item_is_static(ClassItem* me);
/// `#x` (not `"#x"` or `[k]`): the item's name_span, else an invalid span
Span class_item_private_name(ClassItem const* me);
Ty const* class_item_field_evaluate_type(ClassItem* me);
Ty const* class_item_accessor_evaluate_type(ClassItem* me);
Ty const* class_item_get_type(ClassItem* me);
bool class_item_field_is_definite(ClassItem const* me); // `x!: T`

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
    VecExpr decorators; // Checked as calls by class_decl_typecheck() (ty_check_decorator())
    Generic generic;
    OuterGenerics const* outer_generics; // The type parameters it captures (set by the resolve walk)
    Expr extends;
    Span extends_clause; // `extends A<T>`
    VecSpannedType implements; // The first `implements` clause's entity names (`A.B<T>`)
    // Heritage tsc parses but doesn't check (the parser reports checkGrammarClassDeclarationHeritageClauses' TS1172-
    // TS1175 about it): the elements after the base (`extends A, B`, a second `extends` clause) and those of later
    // `implements` clauses. Only resolved, silently, for the baselines
    VecExpr unchecked_extends;
    VecTypeNode unchecked_implements;
    // `implements` elements that aren't entity names (`A?.B`, `f()`): the first invalid_implements_checked (those of the
    // first clause) are TS2500, and all are only resolved, silently
    VecExpr invalid_implements;
    u32 invalid_implements_checked;
    VecClassItem items;
    Symbol* symbol;
    struct ClassDecl const* outer_class; // The class this one is in (scope_enclosing_class()), set when resolving
    // A JavaScript class expression that initializes a variable with expandos (`var C = class {}; C.x = 1`,
    // scope_add_expandos()): the variable, whose exports are static members of this class
    Symbol const* expando_host;
} ClassDecl;

void class_decl_drop(ClassDecl* me);
/// What the class is called in messages and baselines: its name, else its assigned name, else `(Anonymous class)`
Span class_decl_display_name(ClassDecl const* me);
/// The first member of the class named `#name` (tsc's lookupSymbolForPrivateIdentifierDeclaration for one class)
ClassItem const* class_decl_find_private_name(ClassDecl const* me, csview name);
/// The field, method or accessor the class body declares by this name (not computed), null if none
ClassItem* class_decl_find_member(ClassDecl const* me, csview name, bool is_static);
void class_decl_add_names(ClassDecl* me, Scope* scope, DeclExported exported, bool declared);
void class_decl_lookup_names(ClassDecl* me, Scope* names, ErrorCtx* errs);
void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs);
