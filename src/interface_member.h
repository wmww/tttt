#pragma once

#include "type.h"

typedef struct InterfaceMember {
    VecSpan declarations;
    // The original declared interface this member came from, used by the tests. Can be null.
    Symbol const* origin_interface;
    bool readonly; // NOT used for mapped types
    bool optional; // NOT used for mapped types
    bool is_method;
    struct {
        Span span;
        enum {
            INTERFACE_KEY_NAME,
            INTERFACE_KEY_INDEX,
            INTERFACE_KEY_EXPR,
            INTERFACE_KEY_MAPPED,
            INTERFACE_KEY_CALLABLE, // No key data, and type must be callable
        } tag;
        union {
            struct {
                cstr name_string;
                bool name_is_value;
                bool name_is_member_function;
            };
            // tsc parses the brackets as a parameter list and checks it in checkGrammarIndexSignatureParameters.
            // name/type are the first parameter (the key), `any` (`any[]` for a rest) where it has no annotation
            struct {
                Span name; // Null for `[]`
                TypeNode type;
                Span node; // The whole signature, its `;`/`,` included (TS1021)
                Span parameter_property; // A first parameter with `public`/`readonly`... (TS2369)
                Span decl; // The first parameter from its modifiers or `...` (its symbol's declaration)
                VecSpan more_names; // The parameters after the first, for the baselines
                VecSpan more_decls;
                VecTypeNode more_types;
                bool valid; // Exactly one parameter, with a type annotation: the signature makes an index info
                bool grammar_reported; // The parser reported a parameter grammar error: tsc's check stops there
                bool has_value_type;
            } index;
            struct {
                Expr* ref;
                // Only the initial interface parsed into the AST will own an expr
                Expr* owned;
            } expr;
            struct {
                Span type_name;
                TypeNode in_type;
                Span in_type_span;
                TypeNode as_type;
                bool has_as_type;
                signed char readonly; // -1 for remove, +1 for add
                signed char optional; // -1 for remove, +1 for add
                Symbol* owned_symbol;
                Symbol const* symbol_ref;
                TypeFlow template_flow[2]; // What the template's nodes have as TypeNode.flow, as TernaryType.true_flow
            } mapped;
        };
    } key;
    TypeNode type;
    bool implicit_any; // A property signature without a type annotation
    // `get x(): T` with `set x(v: U)`: writes are checked against U (tsc's write type of a divergent accessor)
    TypeNode write_type;
    bool has_write_type;
    bool setter_first; // Parser only: `type` is a typed setter's parameter type until a getter shows up
    // `get [k](): T` / `set [k](v: T)` (a computed key): `type` is the getter's return or the setter's parameter type,
    // a getter and a setter of the same key make one property (members.c). 0 none, 1 get, 2 set
    unsigned char computed_accessor;
    // An accessor pair's second name, and a setter's parameter name (for the types and symbols baselines)
    Span second_name;
    Span setter_param;
} InterfaceMember;

// `#x: T` in an interface or a type literal: TS18016, and no member of the type (tsc's binder gives it no name outside
// a class)
static inline bool interface_member_is_private_name(InterfaceMember const* me) {
    return my key.tag == INTERFACE_KEY_NAME && my key.span.sv.size && my key.span.sv.buf[0] == '#';
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
        .declarations = vec_span_single(declaration),
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
void interface_member_lookup_names(InterfaceMember* me, Scope* names, ErrorCtx* errs);
void interface_member_typecheck(InterfaceMember* me, bool is_in_class, ErrorCtx* errs);

#define g_class_key InterfaceMember, interface_member
#define g_no_clone
#define g_declared
#include <cleat/generic/vec.h>
