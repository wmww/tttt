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

static bool is_named_method(InterfaceMember const* member) {
    return member->key.tag == INTERFACE_KEY_NAME && member->key.name_is_member_function && member->type.tag == TYPE_CALLABLE;
}

// tsc's checkQuestionTokenAgreementBetweenOverloads for method signatures: the overloads of a name are all optional or
// all required, the first one decides (TS2386). Per body, not across merged declarations
static void interface_check_overload_optionality(Interface const* me, ErrorCtx* errs) {
    bool any_optional = false;
    for_each (i, vec_interface_member, &my members) any_optional |= is_named_method(i) && i->optional;
    if (!any_optional) return;
    isize count = vec_interface_member_size(&my members);
    for (isize i = 0; i < count; i++) {
        InterfaceMember const* first = vec_interface_member_at(&my members, i);
        if (!is_named_method(first)) continue;
        bool seen_before = false;
        for (isize j = 0; j < i && !seen_before; j++) {
            InterfaceMember const* other = vec_interface_member_at(&my members, j);
            seen_before = is_named_method(other) && cstr_eq(&other->key.name_string, &first->key.name_string);
        }
        if (seen_before) continue;
        for (isize j = i + 1; j < count; j++) {
            InterfaceMember const* other = vec_interface_member_at(&my members, j);
            if (!is_named_method(other) || !cstr_eq(&other->key.name_string, &first->key.name_string)) continue;
            if (other->optional != first->optional) {
                error_ctx_error_d(errs, other->key.span, 2386, "Overload signatures must all be optional or required.");
            }
        }
    }
}

void interface_typecheck(Interface const* me, ErrorCtx* errs) {
    for_each (i, vec_interface_member, &my members) {
        interface_member_typecheck(i, false, errs);
    }
    interface_check_overload_optionality(me, errs);
}
