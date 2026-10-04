#pragma once

#include "type.h"
#include "expr.h"
#include "value.h"

typedef struct ObjectBindingItem ObjectBindingItem;

g_declare_vec(ObjectBindingItem, object_binding_item);

typedef struct Binding {
    enum {
        BINDING_NOOP = 0,
        BINDING_IDENTIFIER,
        BINDING_ARRAY,
        BINDING_OBJECT,
    } tag;
    union {
        Symbol* symbol;
        VecBinding array;
        VecObjectBindingItem object;
    };
    Span content_span; // Just the name/array/object, not the type/value
    // An object pattern: where its members are read (owned, may be null: see AccessSite). Set by binding_lookup_names()
    struct AccessSite* access_site;
    Span element_span; // With the `...` and the default, only valid for elements of a pattern
    Expr* default_value; // Owned, `x = 1` as an element of a pattern. Null if there's none
    bool rest; // `...x` as an element of an array or object pattern
    bool rest_property_name; // `{ ...a: x }`: a rest element with a property name (TS2566)
    bool definitely_assigned; // `let x!: T`
    bool ambient; // Declared in an ambient context, set by binding_add_names()
    bool is_parameter; // Of a function's parameter (the parameter itself when it has no parent), see binding_add_names()
    bool optional_parameter; // `x?: T` of a function: read as `T | undefined`
    bool has_explicit_type; // If the type was explicitly specified and explicit_type is meaningful
    Span explicit_type_span; // Only valid if has_explicit_type
    TypeNode explicit_type; // The type explicitly specified
    // Where the type comes from when there's no explicit type, see ty_binding_type(). Set by binding_add_names()
    struct Binding const* parent; // The pattern this is an element of, null for the root of a pattern
    ObjectBindingItem const* parent_item; // If parent is an object pattern
    u32 parent_index; // If parent is an array pattern
    // Root only: the type of what is assigned (the initializer, the elements of a for..of). Null: any (a catch variable)
    LazyType* source;
} Binding;

typedef struct ObjectBindingItem {
    Value source;
    Span source_span;
    Expr* computed_key; // Owned, `{ [k]: x }`. Null otherwise, and source is meaningless if set
    Binding target;
} ObjectBindingItem;

typedef struct BindingAddNamesProps {
    bool abstract_literals;
    bool exported;
    bool is_function_arg;
    bool function_scoped; // `var`
    bool is_const;
    bool is_ambient; // `declare`
    Span full;
} BindingAddNamesProps;

void binding_drop(Binding* me);
void binding_add_names(Binding* me, LazyType* source, Scope* scope, BindingAddNamesProps props);
void binding_lookup_names(Binding* me, Scope* names, ErrorCtx* errs);
// Control flow: an assignment node for every variable in the pattern
void binding_flow_assign(Binding const* me, Scope* names);
// An expression that is only evaluated sometimes (a default)
void expr_lookup_names_initializer(Expr* me, Scope* names, ErrorCtx* errs);
// `parameter`: the whole parameter when `me` is one's (TS7006 at it), null for a binding element (TS7031)
void binding_add_implicit_any_type_errors(Binding const* me, Span parameter, ErrorCtx* errs);
// A parameter's array pattern with a tuple initializer: the elements past its end that have no default are implicitly
// any (tsc's padTupleType)
void binding_add_padded_implicit_any_errors(Binding const* me, Ty const* initializer, ErrorCtx* errs);
bool binding_is_parameter(Binding const* pattern);
// tsc's reportWideningErrorsInType: TS7018 where an object literal property in `ty` widens to any. Whether it said anything
bool binding_report_widening_errors_in_type(struct Ty const* ty, ErrorCtx* errs);
// The names of the variables it declares, in order
void binding_collect_names(Binding const* me, VecSpan* out);
/// `initializer`: what the binding is initialized with, if anything (a failed check is elaborated into it)
void binding_typecheck(Binding* me, Expr const* initializer, ErrorCtx* errs);

void object_binding_item_drop(ObjectBindingItem* me);

#define g_class_key Binding, binding
#define g_declared
#define g_no_clone
#include <cleat/generic/vec.h>

#define g_class_key ObjectBindingItem, object_binding_item
#define g_declared
#define g_no_clone
#include <cleat/generic/vec.h>

typedef struct InitializedBinding {
    Binding binding;
    Expr value;
    bool abstract_literals;
    bool is_rest_parameter; // `...x`: without an annotation it's any[]
    LazyType lazy_type;
    // The unannotated parameter of a setter: the getter of the same property, whose return type it has
    struct FunctionImpl const* getter;
    // Their computed name (`set [k](v)`), if any: they are one property only if its type is a literal or unique symbol
    // (tsc's isLateBindableName), see initialized_binding_getter()
    struct Expr const* getter_key;
    // The parameter of a JavaScript function whose JSDoc `@type` types it (FunctionImpl.jsdoc_signature_type)
    struct FunctionImpl const* jsdoc_typed_by;
    // An unannotated `this` parameter: its function, whose contextual `this` type it has (tsc's
    // getTypeForVariableLikeDeclaration: getContextualThisParameterType)
    struct FunctionImpl const* this_of;
    // JavaScript: `const x = require("m")`, `const { a, b: c } = require("m")`, `const a = require("m").a` declare
    // aliases like imports (tsc's isVariableDeclarationInitializedToBareOrAccessedRequire), one per name (owned)
    struct PortItem* require_ports;
    u32 require_port_count;
} InitializedBinding;

void initialized_binding_drop(InitializedBinding* me);
void unique_symbol_decl_mark(UniqueSymbolDecl decl, TypeNode* annotation, Expr* initializer);
void initialized_binding_add_names(InitializedBinding* me, Scope* scope, BindingAddNamesProps props);
void initialized_binding_lookup_names(InitializedBinding* me, Scope* names, ErrorCtx* errs);
void initialized_binding_lookup_names_parameter(InitializedBinding* me, Scope* names, ErrorCtx* errs);
Ty const* initialized_binding_evaluate_assigned_type(InitializedBinding* me);
void initialized_binding_typecheck(InitializedBinding* me, ErrorCtx* errs);
// A parameter (`span`: all of it)
void initialized_binding_typecheck_parameter(InitializedBinding* me, Span span, ErrorCtx* errs);

#define g_class_key InitializedBinding, initialized_binding
#define g_no_clone
#define g_declared
#include <cleat/generic/vec.h>

// The getter whose return type an unannotated setter parameter has (InitializedBinding.getter), if they pair
struct FunctionImpl const* initialized_binding_getter(InitializedBinding const* me);
