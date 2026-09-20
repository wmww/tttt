#pragma once

#include "type.h"

typedef struct InterfaceMember {
    VecSpan declarations;
    // The original declared interface this member came from, used by the tests. Can be null.
    Declaration const* origin_interface;
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
            struct {
                Span name;
                TypeNode type;
            } index;
            struct {
                Expr* ref;
                // Only the initial interface parsed into the AST will own an expr
                Expr* owned;
            } expr;
            struct {
                Span type_name;
                TypeNode in_type;
                TypeNode as_type;
                bool has_as_type;
                signed char readonly; // -1 for remove, +1 for add
                signed char optional; // -1 for remove, +1 for add
                DeclHandle owned_declaration;
                Declaration const* declaration_ref;
            } mapped;
        };
    } key;
    TypeNode type;
} InterfaceMember;

static inline InterfaceMember interface_member_make(
    Span declaration,
    Declaration const* origin_interface,
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
InterfaceMember interface_member_clone(InterfaceMember me);
void interface_member_lookup_names(InterfaceMember* me, Scope* names, ErrorCtx* errs);
void interface_member_typecheck(InterfaceMember* me, bool is_in_class, ErrorCtx* errs);


#define g_class_key InterfaceMember, interface_member
#define g_declared
#include <cleat/generic/vec.h>
