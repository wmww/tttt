#pragma once

#include "ast_alloc.h"
#include "type.h"
#include "expr.h"
#include "value.h"

typedef struct ObjectBindingItem ObjectBindingItem;

g_declare_vec(ObjectBindingItem, object_binding_item);

typedef struct Binding {
    union {
        Symbol* symbol;
        VecBinding array;
        VecObjectBindingItem object;
    };
    Span content_span; // Just the name/array/object, not the type/value
    // An object pattern: where its members are read (owned, may be null: see AccessSite). Set by binding_lookup_names()
    struct AccessSite* access_site;
    Expr* default_value; // Owned, `x = 1` as an element of a pattern. Null if there's none
    // Owned, the type explicitly specified, null if none was (most declarations): read through binding_type(), write
    // through binding_set_type()
    TypeNode* explicit_type;
    // Where the type comes from when there's no explicit type, see ty_binding_type(). Set by binding_add_names()
    struct Binding const* parent; // The pattern this is an element of, null for the root of a pattern
    union {
        ObjectBindingItem const* parent_item; // If parent is an object pattern
        u32 parent_index; // If parent is an array pattern
    };
    // Root only: the type of what is assigned (the initializer, the elements of a for..of). Null: any (a catch variable)
    LazyType* source;
    // binding_explicit_type_span() and binding_element_span(): offsets in content_span's file
    u32 explicit_type_start, explicit_type_length;
    u32 element_start, element_length;
    enum {
        BINDING_NOOP = 0,
        BINDING_IDENTIFIER,
        BINDING_ARRAY,
        BINDING_OBJECT,
    } tag : 8;
    bool has_explicit_type_span : 1;
    bool has_element_span : 1;
    bool rest : 1; // `...x` as an element of an array or object pattern
    bool rest_property_name : 1; // `{ ...a: x }`: a rest element with a property name (TS2566)
    bool definitely_assigned : 1; // `let x!: T`
    bool ambient : 1; // Declared in an ambient context, set by binding_add_names()
    bool is_parameter : 1; // Of a function's parameter (the parameter itself when it has no parent), see binding_add_names()
    bool optional_parameter : 1; // `x?: T` of a function: read as `T | undefined`
    bool has_explicit_type : 1; // If the type was explicitly specified and explicit_type is meaningful
} Binding;

extern const TypeNode type_node_none;
// The annotation (only meaningful if has_explicit_type), a zeroed node (any) if there is none
static inline TypeNode* binding_type(Binding const* me) {
    return me->explicit_type ? me->explicit_type : (TypeNode*)&type_node_none;
}
// Takes the type (dropping the one it had)
void binding_set_type(Binding* me, TypeNode type);
// Gives the type up (a zeroed one if there was none)
TypeNode binding_take_type(Binding* me);

// Where the type annotation is (only valid if has_explicit_type, may be invalid then too)
Span binding_explicit_type_span(Binding const* me);
// With the `...` and the default, only valid for elements of a pattern
Span binding_element_span(Binding const* me);
// Both in the file of content_span, which must be set first
void binding_set_explicit_type_span(Binding* me, Span span);
void binding_set_element_span(Binding* me, Span span);

typedef struct ObjectBindingItem {
    Value source;
    Span source_span;
    Expr* computed_key; // Owned, `{ [k]: x }`. Null otherwise, and source is meaningless if set
    Binding target;
    // `{ #a: x }` (TS18064): the key is never (tsgo's getLiteralTypeFromPropertyName), source is undefined
    bool private_name;
} ObjectBindingItem;

typedef struct BindingAddNamesProps {
    bool abstract_literals;
    bool exported;
    bool is_function_arg;
    bool function_scoped; // `var`
    bool is_const;
    bool is_ambient; // `declare`
    bool declare_keyword; // An explicit `declare`
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
void binding_add_implicit_any_type_errors(Binding const* me, Span parameter, bool bodiless, ErrorCtx* errs);
// A parameter's array pattern with a tuple initializer: the elements past its end that have no default are implicitly
// any (tsc's padTupleType)
void binding_add_padded_implicit_any_errors(Binding const* me, Ty const* initializer, ErrorCtx* errs);
bool binding_is_parameter(Binding const* pattern);
// The type a pattern's elements are taken from as far as reporting what they lack goes (tsc's
// getBindingElementTypeFromParentType), null if nothing is. `initializer`: the root's initializer or the pattern's default
Ty const* binding_element_parent_type(Binding const* me, Expr const* initializer);
// tsc's reportWideningErrorsInType: TS7018 where an object literal property in `ty` widens to any. Whether it said anything
bool binding_report_widening_errors_in_type(struct Ty const* ty, ErrorCtx* errs);
void binding_report_iterated_widening(Binding const* me, struct Ty const* element, ErrorCtx* errs);
// The names of the variables it declares, in order
void binding_collect_names(Binding const* me, VecSpan* out);
/// `initializer`: what the binding is initialized with, if anything (a failed check is elaborated into it)
void binding_typecheck(Binding* me, Expr const* initializer, ErrorCtx* errs);
// A parameter of a signature without a body (a type's, an overload's): an element's default is TS2371 (reported by
// the signature) and nothing else of that element is checked
void binding_typecheck_bodiless(Binding* me, ErrorCtx* errs);

void object_binding_item_drop(ObjectBindingItem* me);

#define g_class_key Binding, binding
#define g_declared
#define g_no_clone
#define g_allocator ast
#include <cleat/generic/vec.h>

#define g_class_key ObjectBindingItem, object_binding_item
#define g_declared
#define g_no_clone
#define g_allocator ast
#include <cleat/generic/vec.h>

typedef struct InitializedBinding {
    Binding binding;
    // Owned, null when there is no initializer (a missing one, `x = ;`, is an EXPR_IMPLICIT_ANY with `missing`): read it
    // through initialized_binding_value()
    Expr* value;
    bool abstract_literals;
    bool is_rest_parameter; // `...x`: without an annotation it's any[]
    LazyType lazy_type;
    struct InitializedBindingRare* rare; // Owned, null if none of its fields is set: read through initialized_binding_rare()
} InitializedBinding;

// What few InitializedBindings have
typedef struct InitializedBindingRare {
    // The unannotated parameter of a setter: the getter of the same property, whose return type it has
    struct FunctionImpl const* getter;
    // Their computed name (`set [k](v)`), if any: they are one property only if its type is a literal or unique symbol
    // (tsc's isLateBindableName), see initialized_binding_getter()
    struct Expr const* getter_key;
    // The unannotated parameter of a class's setter whose computed name isn't a literal: its setter, whose getter the
    // checker finds (function_impl_late_getter())
    struct FunctionImpl const* late_setter;
    // The parameter of a JavaScript function whose JSDoc `@type` types it (FunctionImpl.jsdoc_signature_type)
    struct FunctionImpl const* jsdoc_typed_by;
    // An unannotated `this` parameter: its function, whose contextual `this` type it has (tsc's
    // getTypeForVariableLikeDeclaration: getContextualThisParameterType)
    struct FunctionImpl const* this_of;
    // An unannotated parameter of a function expression that has a context root (FunctionImpl.context_root): that
    // function, whose context is settled when something asks for the parameter's type before anything checked the
    // function (tsc's getContextuallyTypedParameterType resolves the call then: a JavaScript `this.y` read in an
    // earlier method, `xs.forEach(x => { this.y = x })`)
    struct FunctionImpl const* context_function;
    // An unannotated parameter of a function expression that is an argument of a call an expression statement makes:
    // that statement's expression, checked first when the parameter's type is asked for before it is
    struct Expr const* settled_by;
    // JavaScript: `const x = require("m")`, `const { a, b: c } = require("m")`, `const a = require("m").a` declare
    // aliases like imports (tsc's isVariableDeclarationInitializedToBareOrAccessedRequire), one per name (owned)
    struct PortItem* require_ports;
    u32 require_port_count;
} InitializedBindingRare;

void initialized_binding_drop(InitializedBinding* me);
// What an absent initializer reads as: an EXPR_IMPLICIT_ANY without a span whose type is any (read only)
extern const Expr initialized_binding_no_value;
extern const InitializedBindingRare initialized_binding_no_rare;
static inline InitializedBindingRare const* initialized_binding_rare(InitializedBinding const* me) {
    return me->rare ? me->rare : &initialized_binding_no_rare;
}
// Allocates them if needed
InitializedBindingRare* initialized_binding_rare_mut(InitializedBinding* me);
// The initializer, else initialized_binding_no_value
static inline Expr* initialized_binding_value(InitializedBinding const* me) {
    return me->value ? me->value : (Expr*)&initialized_binding_no_value;
}
void unique_symbol_decl_mark(UniqueSymbolDecl decl, TypeNode* annotation, Expr* initializer);
void initialized_binding_add_names(InitializedBinding* me, Scope* scope, BindingAddNamesProps props);
void initialized_binding_lookup_names(InitializedBinding* me, Scope* names, ErrorCtx* errs);
// `value_unchecked`: the initializer of a function without a body (TS2371 only): its names are resolved silently
void initialized_binding_lookup_names_parameter(InitializedBinding* me, Scope* names, bool value_unchecked, ErrorCtx* errs);
Ty const* initialized_binding_evaluate_assigned_type(InitializedBinding* me);
void initialized_binding_settle_value(InitializedBinding* me); // Its initializer checked, see ty_settle_function_context()
bool initialized_binding_js_empty_array(InitializedBinding const* me, Ty const* init);
void initialized_binding_typecheck(InitializedBinding* me, ErrorCtx* errs);
// A parameter (`span`: all of it)
void initialized_binding_typecheck_parameter(InitializedBinding* me, Span span, bool bodiless, ErrorCtx* errs);

#define g_class_key InitializedBinding, initialized_binding
#define g_no_clone
#define g_declared
#define g_allocator ast
#include <cleat/generic/vec.h>

// The getter whose return type an unannotated setter parameter has (InitializedBinding.getter), if they pair
struct FunctionImpl const* initialized_binding_getter(InitializedBinding const* me);
