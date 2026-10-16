#pragma once

#include "function_impl.h"
#include "interface_member.h"
#include "lazy_type.h"

typedef Expr const* ExprCPtr;
g_declare_vec(ExprCPtr, expr_cptr);
#define g_key ExprCPtr, expr_cptr
#define g_declared
#include <cleat/generic/vec.h>

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
    // CLASS_ITEM_CONSTRUCTOR: a JavaScript JSDoc `@readonly` (TS1024; tsgo's reparser makes it a modifier, which
    // declaration emit keeps)
    bool jsdoc_readonly;
    // A method, accessor or field with a written name: the first declaration of the member symbol tsc's binder puts it
    // in, itself if it stands alone because it conflicts with what came first (`x: T; get x()`) or is that first one
    // (see bind_member_symbols()). Set when looking up names, null for other items
    struct ClassItem* symbol_first;
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
            bool is_declare; // `declare m()` (TS1031): ambient, no body expected (tsc's parser gives it NodeFlags.Ambient)
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
            bool is_definite; // `x!: T`
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
            // A property an accessor of its name after it joined (tsgo 7.1's binder: one symbol, see
            // bind_member_symbols()): that accessor pair's first item, whose type the symbol has (getTypeOfSymbol
            // types an accessor symbol by its accessors, the property's declaration is never typed). Set when looking up
            // names
            struct ClassItem* joined_accessor;
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
// Whether two methods are written with the same name (a computed literal by its value, `["kk"]` is `kk`; a private name
// is its class's own, `#a` isn't `["#a"]`)
static inline bool class_methods_same_name(ClassItem const* a, ClassItem const* b) {
    return cstr_eq(&a->method.name_string, &b->method.name_string) &&
        span_valid(class_item_private_name(a)) == span_valid(class_item_private_name(b));
}
Ty const* class_item_field_evaluate_type(ClassItem* me);
Ty const* class_item_accessor_evaluate_type(ClassItem* me);
Ty const* class_item_get_type(ClassItem* me);

#define g_class_key ClassItem, class_item
#define g_no_clone
#include <cleat/generic/vec.h>

// The declarations of the member symbol `me` is part of, in order (see ClassItem.symbol_first). Its own count 1 if it
// stands alone

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
    // JavaScript: the type of an `@augments`/`@extends` tag naming the class `extends` names, whose type arguments the
    // clause takes when it has none (tsgo's reparser): where the tag's own reference is checked
    Span jsdoc_extends;
    // JavaScript: an `@augments`/`@extends` tag naming another class than the clause (or nothing): its type (owned,
    // null for a missing name), checked against the base type (TS8023, tsgo's checkJSDocAugmentsTagMatchesExtends)
    TypeNode* jsdoc_augments;
    Span jsdoc_augments_name; // Valid with such a tag
    Span jsdoc_augments_tag; // Its tag name
    Span jsdoc_extends_name; // The clause's rightmost name
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
    // A JavaScript class expression that `exports.C = class {}` aliases: the scope with its name, made (with the
    // class's symbol) by the declare step, which the resolve walk takes instead of making one (scope_add_commonjs_export())
    struct Scope* name_scope;
    // The module whose export it is then (tsc's getContainersOfSymbol for a class expression assigned to
    // `module.exports` or `exports.C`): names of the class go through the module
    Symbol const* commonjs_module;
    // `export = class {}`: an alias of the class names it from where it's imported (`typeof Chunk`)
    bool export_assigned;
    // `this[k] = v` directly in the constructor with a key that isn't a literal (`this[Flags.X]`, `this[key]`): which
    // field it assigns needs the key's type, so TS2564 (field_initialized_in_constructor()) tries these as the flow
    // reference of fields no `this.x = v` names. Set by the resolve walk
    VecExprCPtr computed_this_assignments;
    // In a class of more than CLASS_NAMESAKES_FROM items (set when looking up names; null otherwise): per item, the
    // first and the next item (-1 at the end) with its name_string (methods and fields, computed keys by their text;
    // -1 for the others), and the items with a computed key in order. The loops over the declarations of a member
    // (overloads, accessor pairs, member symbols) walk these (class_namesakes()), not every item: linear on large classes
    i32* namesake_first;
    i32* namesake_next;
    i32* computed_items;
    u32 computed_item_count;
} ClassDecl;

enum { CLASS_NAMESAKES_FROM = 16 };

void class_decl_drop(ClassDecl* me);

// The items that may declare the same member as `item`, in order: those with its name_string, and `with_computed`
// those with a computed key too (same_member() compares a computed key's name, not its text; for an item with a
// computed key itself, every item). Every item in a small class, or for an item without a name_string. Callers keep
// their own name tests: this only skips items that can't pass them
//     for (ClassNamesakes it = class_namesakes(decl, item, false); class_namesakes_next(&it);) { it.item, it.index }
typedef struct ClassNamesakes {
    ClassDecl const* decl;
    bool all;
    i32 chain; // The next item of the name chain, -1 at its end
    u32 computed; // The next position in decl->computed_items
    isize index; // The current item's
    struct ClassItem* item;
} ClassNamesakes;

ClassNamesakes class_namesakes(ClassDecl const* decl, struct ClassItem const* item, bool with_computed);

static inline bool class_namesakes_next(ClassNamesakes* it) {
    ClassDecl const* decl = it->decl;
    if (it->all) {
        if (++it->index >= (isize)decl->items.size) return false;
    } else {
        isize computed = it->computed < decl->computed_item_count ? decl->computed_items[it->computed] : PTRDIFF_MAX;
        isize chain = it->chain >= 0 ? it->chain : PTRDIFF_MAX;
        if (computed == PTRDIFF_MAX && chain == PTRDIFF_MAX) return false;
        if (chain <= computed) {
            it->index = chain;
            it->chain = decl->namesake_next[chain];
            if (computed == chain) it->computed++;
        } else {
            it->index = computed;
            it->computed++;
        }
    }
    it->item = &decl->items.data[it->index];
    return true;
}
/// What the class is called in messages and baselines: its name, else its assigned name, else `(Anonymous class)`
Span class_decl_display_name(ClassDecl const* me);
/// The first member of the class named `#name` (tsc's lookupSymbolForPrivateIdentifierDeclaration for one class)
ClassItem const* class_decl_find_private_name(ClassDecl const* me, csview name);
/// The field, method or accessor the class body declares by this name (not computed), null if none
ClassItem* class_decl_find_member(ClassDecl const* me, csview name, bool is_static);
void class_decl_add_names(ClassDecl* me, Scope* scope, DeclStatementProps props);
void class_decl_lookup_names(ClassDecl* me, Scope* names, ErrorCtx* errs);
void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs);
// The part of class_decl_typecheck() about the members, deferred for a class expression (Checker.defer_checks)
void class_decl_typecheck_members(ClassDecl* me, ErrorCtx* errs);
// Whether a base class named by the `extends` clause gets as many type arguments as it takes (TS2314/TS2707 if not,
// and the base is errorType). Always in JavaScript
bool class_decl_base_arity_fits(ClassDecl const* me);
// The base of the first interface merged into the class that extends something (tsc's getBaseTypes after the class's
// own base), null if none
struct Ty const* class_decl_merged_interface_base(ClassDecl const* me);
