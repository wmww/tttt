#pragma once

#include "prelude.h"

typedef struct InterfaceMember InterfaceMember;
typedef struct Type Type;
typedef struct CallExpr CallExpr;
typedef struct Declaration Declaration;
typedef struct ErrorCtx ErrorCtx;
wrap_forward_vec(InterfaceMember, interface_member);
wrap_forward_vec(Type, type);

typedef struct Interface {
    VecInterfaceMember members;
} Interface;

void interface_drop(Interface* me);
Interface interface_clone(Interface me);
bool interface_try_from_type(Type const* me, Type* ret);
Type interface_from_merged_interfaces(VecType interfaces);
void interface_typecheck(Interface const* me, ErrorCtx* errs);
void interface_typecheck_members_against(Interface* me, Declaration const* declaration, ErrorCtx* errs);
InterfaceMember const* interface_select_callable_overload(
    Interface const* me,
    CallExpr const* call,
    bool* is_callable_ret // may be null
);
