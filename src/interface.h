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
    bool type_literal : 1; // A type literal's members, not an interface declaration's body
    Span span; // From `{` to `}`
} Interface;

void interface_drop(Interface* me);
// `merged`: the interface symbol whose declarations its own merges with (null for a type literal)
void interface_typecheck(Interface const* me, struct Symbol const* merged, ErrorCtx* errs);
// A method signature with a written name (`m(): void`, not `m: () => void`)
bool interface_member_is_named_method(struct InterfaceMember const* member);
