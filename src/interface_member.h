#pragma once

#include "type.h"

typedef struct InterfaceMember {
    Span span;
    bool readonly;
    cstr name;
    Span name_span;
    Type type;
} InterfaceMember;

static inline InterfaceMember interface_member_make(Span span, bool readonly, cstr name, Span name_span, Type type) {
    return (InterfaceMember) {
        .span = span,
        .readonly = readonly,
        .name = name,
        .name_span = name_span,
        .type = type,
    };
}

void interface_member_drop(InterfaceMember* me);
InterfaceMember interface_member_clone(InterfaceMember me);

#define i_key_class_names InterfaceMember, interface_member
#define i_is_forward
#include "wrap_stc/vec.h"
