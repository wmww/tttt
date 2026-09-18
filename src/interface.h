#pragma once

#include "prelude.h"

typedef struct InterfaceMember InterfaceMember;
typedef struct Type Type;
typedef struct CallExpr CallExpr;
typedef struct Declaration Declaration;
typedef struct ErrorCtx ErrorCtx;
g_declare_vec(InterfaceMember, interface_member);
g_declare_vec(Type, type);

typedef struct Interface {
    VecInterfaceMember members;
    bool parsed : 1;
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
