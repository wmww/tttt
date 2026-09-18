#pragma once

#include "prelude.h"

typedef struct InterfaceMember InterfaceMember;
typedef struct TypeNode TypeNode;
typedef struct CallExpr CallExpr;
typedef struct Declaration Declaration;
typedef struct ErrorCtx ErrorCtx;
g_declare_vec(InterfaceMember, interface_member);
g_declare_vec(TypeNode, type);

typedef struct Interface {
    VecInterfaceMember members;
    bool parsed : 1;
} Interface;

void interface_drop(Interface* me);
Interface interface_clone(Interface me);
void interface_typecheck(Interface const* me, ErrorCtx* errs);
