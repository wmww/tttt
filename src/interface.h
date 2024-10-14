#pragma once

#include "prelude.h"

typedef struct InterfaceMember InterfaceMember;
typedef struct Type Type;
typedef struct Declaration Declaration;
typedef struct ErrorCtx ErrorCtx;
wrap_forward_vec(InterfaceMember, interface_member);
wrap_forward_vec(Type, type);

typedef struct Interface {
    VecInterfaceMember members;
} Interface;

void interface_drop(Interface* me);
Interface interface_clone(Interface me);
Type interface_from_merged(VecType const* types);
void interface_typecheck(Interface const* me, ErrorCtx* errs);
void interface_typecheck_members_against(Interface* me, Declaration const* declaration, ErrorCtx* errs);
