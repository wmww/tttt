#pragma once

#include "expr.h"
#include "type.h"

typedef struct AsTypeExpr {
    Expr expr;
    Type type;
} AsTypeExpr;
