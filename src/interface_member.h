#pragma once

#include "ast_alloc.h"
#include "type.h"

// tsc parses an index signature's brackets as a parameter list and checks it in checkGrammarIndexSignatureParameters.
// name/type are the first parameter (the key), `any` (`any[]` for a rest) where it has no annotation
typedef struct InterfaceIndexKey {
    Span name; // Null for `[]`
    TypeNode type;
    Span node; // The whole signature, its `;`/`,` included (TS1021)
    Span parameter_property; // A first parameter with `public`/`readonly`... (TS2369)
    Span decl; // The first parameter from its modifiers or `...` (its symbol's declaration)
    VecSpan more_names; // The parameters after the first, for the baselines
    VecSpan more_decls;
    VecTypeNode more_types;
    // The parameters with an initializer (TS2371), each up to its initializer's end
    VecSpan initialized;
    // ... and the initializers (owned, as many), which tsc's types baseline shows
    struct Expr** initializer_values;
    bool valid; // Exactly one parameter, with a type annotation: the signature makes an index info
    bool grammar_reported; // The parser reported a parameter grammar error: tsc's check stops there
    bool has_value_type;
    // Which parameters (bit k: the k-th) are written `x?` (TS1019), whose types tsc's baseline shows with undefined
    u32 optional_params;
} InterfaceIndexKey;

typedef struct InterfaceMappedKey {
    Span type_name;
    TypeNode in_type;
    Span in_type_span;
    TypeNode as_type;
    Span as_type_span;
    Symbol* owned_symbol;
    TypeFlow template_flow[2]; // What the template's nodes have as TypeNode.flow, as TernaryType.true_flow
    bool has_as_type;
    signed char readonly; // -1 for remove, +1 for add
    signed char optional; // -1 for remove, +1 for add
} InterfaceMappedKey;

// What only a `get`/`set` pair in a type literal or an interface has
typedef struct InterfaceAccessor {
    // A setter's parameter name (for the types and symbols baselines), or its binding pattern as written (declaration
    // emit reprints it)
    Span setter_param;
    Span setter_pattern;
    // The other accessor of its symbol (tsc's getAllAccessorDeclarations), as an index delta in the member list: a
    // getter's setter, a setter's getter (0 none). See interface_members_bind()
    int32_t partner;
    // Not an accessor's: `a: T = v` (TS1246/TS1247), an initializer tsc's parser keeps and its types baseline shows.
    // Owned, null if none (the rare parts of a member live here)
    struct Expr* initializer;
    // A getter with a body (TS1183): the body as a function expression (EXPR_FUNCTION, owned, null if none), which tsc
    // binds and walks like any, and whose return type is the getter's type when it has no annotation
    struct Expr* getter_body;
    bool getter_body_infers; // The getter has no return annotation: its type is getter_body's return type
    // A setter whose parameter is a binding pattern: the setter as a function expression (EXPR_FUNCTION, owned, null if
    // none), whose parameter scope declares the pattern's names (tsc binds the accessor like any signature: `set
    // x({ name: alias }: Named & { o: typeof alias })` resolves `alias`, TS2842 for an unused renaming). The
    // parameter's annotation stays the function's: the member's `type` is that same node, borrowed (not resolved nor
    // dropped through the member)
    struct Expr* setter_function;
    bool type_borrowed; // InterfaceMember.type is setter_function's parameter annotation
    bool annotated; // A getter's return annotation or body, a setter's parameter annotation (noImplicitAny)
} InterfaceAccessor;

// PERF: the rare parts (index signatures, mapped types, accessors) are boxed: there are hundreds of thousands of these
typedef struct InterfaceMember {
    // The declaration, null for none
    Span declaration;
    // The original declared interface this member came from, used by the tests. Can be null.
    Symbol const* origin_interface;
    struct {
        Span span;
        union {
            cstr name_string; // INTERFACE_KEY_NAME
            InterfaceIndexKey* index; // Owned
            Expr* expr; // Owned
            InterfaceMappedKey* mapped; // Owned
        };
        enum {
            INTERFACE_KEY_NAME,
            INTERFACE_KEY_INDEX,
            INTERFACE_KEY_EXPR,
            INTERFACE_KEY_MAPPED,
            INTERFACE_KEY_CALLABLE, // No key data, and type must be callable
        } tag;
        bool name_is_value : 1; // INTERFACE_KEY_NAME
        bool name_is_member_function : 1; // INTERFACE_KEY_NAME
    } key;
    // An accessor's: a getter's return type, a setter's parameter type
    TypeNode type;
    // Owned, null unless the member is an accessor or has one of the rare parts (interface_member_accessor())
    InterfaceAccessor* accessor;
    bool readonly : 1; // NOT used for mapped types
    bool optional : 1; // NOT used for mapped types
    bool is_method : 1;
    bool implicit_any : 1; // A property signature without a type annotation
    // A declaration its symbol's earlier ones exclude (tsc's declareSymbol: TS2300, a symbol of its own that isn't in
    // the members table), see interface_members_bind()
    bool conflicting : 1;
    // `get x(): T` / `set x(v: T)` (a name or a computed key): each accessor is a member of its own, `type` the getter's
    // return or the setter's parameter type; a getter and a setter of one symbol make one property (members.c). 0 none,
    // 1 get, 2 set
    unsigned char accessor_kind : 2;
} InterfaceMember;

// The accessor side of a member, made on demand (parser)
static inline InterfaceAccessor* interface_member_accessor(InterfaceMember* me) {
    if (!my accessor) {
        my accessor = ast_new(InterfaceAccessor, {0});
    }
    return my accessor;
}
static inline Span interface_member_setter_param(InterfaceMember const* me) {
    return my accessor ? my accessor->setter_param : span_null;
}

// `#x: T` in an interface or a type literal: TS18016, and no member of the type (tsc's binder gives it no name outside
// a class)
static inline bool interface_member_is_private_name(InterfaceMember const* me) {
    return my key.tag == INTERFACE_KEY_NAME && my key.span.size && my key.span.buf[0] == '#';
}

static inline InterfaceMember interface_member_make(
    Span declaration,
    Symbol const* origin_interface,
    bool readonly,
    bool optional,
    cstr name_string,
    Span name_span,
    bool name_is_value,
    TypeNode type
) {
    return (InterfaceMember) {
        .declaration = declaration,
        .origin_interface = origin_interface,
        .readonly = readonly,
        .optional = optional,
        .key = {
            .span = name_span,
            .tag = INTERFACE_KEY_NAME,
            .name_string = name_string,
            .name_is_value = name_is_value,
        },
        .type = type,
    };
}

void interface_member_drop(InterfaceMember* me);
// `[K in T]` as a type member where tsc parses no mapped type (a property named by an `in` expression): TS7061
bool interface_member_is_misplaced_mapped(InterfaceMember const* me);
void interface_member_lookup_names(InterfaceMember* me, Scope* names, ErrorCtx* errs);
// Where tsc's diagnostics about the member go: its name, a method signature's whole signature with its `;` or `,`
// (getErrorSpanForNode has no case for it)
Span interface_member_error_span(InterfaceMember const* me);
void interface_member_typecheck(InterfaceMember* me, bool is_in_class, bool in_type_literal, ErrorCtx* errs);

#define g_class_key InterfaceMember, interface_member
#define g_no_clone
#define g_declared
#define g_allocator ast
#include <cleat/generic/vec.h>

// tsc's binder over a type's members (declareSymbol with each kind's excludes): marks the declarations that conflict
// with their name's symbol (InterfaceMember.conflicting) and links each accessor to the other one of its symbol
// (InterfaceAccessor.partner). Once the list is complete (parser)
void interface_members_bind(VecInterfaceMember* members);
