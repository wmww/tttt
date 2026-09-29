#pragma once

#include "prelude.h"
#include "span.h"

typedef struct InterfaceMember InterfaceMember;
typedef struct TypeNode TypeNode;
typedef struct CallExpr CallExpr;
typedef struct Symbol Symbol;
typedef struct ErrorCtx ErrorCtx;
g_declare_vec(InterfaceMember, interface_member);
g_declare_vec(TypeNode, type);

typedef struct Interface {
    VecInterfaceMember members;
    bool parsed : 1;
    // A JSDoc type literal made of `@param`/`@property` tags: tsc's printer reprints it on several lines (which the
    // types baseline flattens), see display_node_interface()
    bool jsdoc_literal : 1;
    Span span; // From `{` to `}` (invalid for interface declarations' bodies)
} Interface;

void interface_drop(Interface* me);
void interface_typecheck(Interface const* me, ErrorCtx* errs);
