#pragma once

#include "type.h"

typedef struct InterfaceMember {
    Span span;
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
                cstr name;
                bool name_is_value;
            };
            Type index;
            ArcExpr expr;
            struct {
                cstr type_name;
                Span type_name_span;
                Type in_type;
                Type as_type;
                bool has_as_type;
                signed char readonly; // -1 for remove, +1 for add
                signed char optional; // -1 for remove, +1 for add
            } mapped;
        };
    } key;
    Type type;
} InterfaceMember;

static inline InterfaceMember interface_member_make(
    Span span,
    bool readonly,
    bool optional,
    cstr name,
    Span name_span,
    bool name_is_value,
    Type type
) {
    return (InterfaceMember) {
        .span = span,
        .readonly = readonly,
        .optional = optional,
        .key = {
            .span = name_span,
            .tag = INTERFACE_KEY_NAME,
            .name = name,
            .name_is_value = name_is_value,
        },
        .type = type,
    };
}

void interface_member_drop(InterfaceMember* me);
InterfaceMember interface_member_clone(InterfaceMember me);
void cstr_append_interface_member_debug(cstr* target, InterfaceMember const* me, int indent);
void cstr_append_interface_member_error_msg(cstr* target, InterfaceMember const* me);
void interface_member_resolve_names(InterfaceMember* me, NameStack* names, ErrorCtx* errs);

#define i_key_class_names InterfaceMember, interface_member
#define i_is_forward
#include "wrap_stc/vec.h"
