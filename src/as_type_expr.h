#pragma once

#include "expr.h"
#include "type.h"

typedef struct AsTypeExpr {
    Expr expr;
    Type type;
    bool angle_bracket_syntax; // If the <type>value syntax was used
} AsTypeExpr;
