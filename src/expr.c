#include "expr.h"

void Expr_drop(Expr* self) {
    (void)self;
}

cstr Expr_to_cstr(Expr* self) {
    (void)self;
    return cstr_lit("undefined");
}
