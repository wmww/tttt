#pragma once

#include "type.h"

typedef struct InterfaceMember {
    VecSpan declarations;
    Span origin_interface_name; // The original interface this member came from, used in tests
    bool readonly; // NOT used for mapped types
    bool optional; // NOT used for mapped types
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
            Type index;
            ArcExpr expr;
            struct {
                Span type_name;
                Type in_type;
                Type as_type;
                bool has_as_type;
                signed char readonly; // -1 for remove, +1 for add
                signed char optional; // -1 for remove, +1 for add
                ArcDeclaration owned_declaration;
                Declaration const* declaration_ref;
            } mapped;
        };
    } key;
    Type type;
} InterfaceMember;

static inline InterfaceMember interface_member_make(
    Span declaration,
    Span origin_interface_name,
    bool readonly,
    bool optional,
    cstr name_string,
    Span name_span,
    bool name_is_value,
    Type type
) {
    return (InterfaceMember) {
        .declarations = vec_span_single(declaration),
        .origin_interface_name = origin_interface_name,
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
void interface_member_lookup_names(InterfaceMember* me, ScopeStack* names, ErrorCtx* errs);
void interface_member_typecheck(InterfaceMember* me, ErrorCtx* errs);

bool interface_member_key_overlaps(InterfaceMember const* me, Type const* index);
bool interface_key_extends(InterfaceMember const* sub, InterfaceMember const* super);

#define i_key_class_names InterfaceMember, interface_member
#define i_is_forward
#include "wrap_stc/vec.h"
