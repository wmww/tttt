#include "interface.h"
#include "interface_member.h"
#include "display.h"
#include "error.h"
#include "lazy_type.h"
#include "callable_type.h"
#include "expr.h"

void interface_drop(Interface* me) {
    vec_interface_member_drop(&my members);
}

void interface_typecheck(Interface const* me, ErrorCtx* errs) {
    for_each (i, vec_interface_member, &my members) {
        interface_member_typecheck(i, false, errs);
    }
}
