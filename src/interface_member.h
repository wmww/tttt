#pragma once

#include "type.h"

typedef struct InterfaceMember {
    Span span;
    bool readonly;
    struct {
        Span span;
        enum {
            INTERFACE_KEY_NAME,
            INTERFACE_KEY_INDEX,
            INTERFACE_KEY_MAPPED,
        } tag;
        union {
            cstr name;
            Type index;
            ArcExpr mapped;
        };
    } key;
    Type type;
} InterfaceMember;

static inline InterfaceMember interface_member_make(
    Span span,
    bool readonly,
    cstr name,
    Span name_span,
    Type type
) {
    return (InterfaceMember) {
        .span = span,
        .readonly = readonly,
        .key = {
            .span = name_span,
            .tag = INTERFACE_KEY_NAME,
            .name = name,
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
