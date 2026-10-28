#include "interface.h"
#include "interface_member.h"
#include "display.h"
#include "error.h"
#include "lazy_type.h"
#include "callable_type.h"
#include "expr.h"
#include "scope.h"
#include "interface_decl.h"
#include "class_decl.h"
#include "source_file.h"

void interface_drop(Interface* me) {
    vec_interface_member_drop(&my members);
}

bool interface_member_is_named_method(InterfaceMember const* member) {
    return member->key.tag == INTERFACE_KEY_NAME && member->key.name_is_member_function && member->type.tag == TYPE_CALLABLE;
}

// The first method signature named `name` among the declarations of `merged` (tsc's getCanonicalOverload: the first
// overload of the merged symbol, wherever it is), null if none
static InterfaceMember const* canonical_method(Symbol const* merged, cstr const* name) {
    for (u32 d = 0; merged && d < merged->decl_count; d++) {
        if (merged->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* decl = merged->decls[d].node;
        for_each (i, vec_interface_member, &decl->type.interface.members) {
            if (interface_member_is_named_method(i) && cstr_eq(&i->key.name_string, name)) return i;
        }
    }
    return nullptr;
}

// tsc's checkQuestionTokenAgreementBetweenOverloads for method signatures: the overloads of a name are all optional or
// all required, the first one decides (TS2386). Across the merged declarations of an interface (the first one of
// them, a lib's `ImportMeta.resolve` against @types/node's `resolve?`), else per body
static void interface_check_overload_optionality(Interface const* me, Symbol const* merged, ErrorCtx* errs) {
    bool any_optional = false, any_required = false;
    for_each (i, vec_interface_member, &my members) {
        if (!interface_member_is_named_method(i)) continue;
        any_optional |= i->optional;
        any_required |= !i->optional;
    }
    if (!any_optional && !(any_required && merged && merged->decl_count > 1)) return;
    isize count = vec_interface_member_size(&my members);
    for (isize i = 0; i < count; i++) {
        InterfaceMember const* first = vec_interface_member_at(&my members, i);
        if (!interface_member_is_named_method(first)) continue;
        bool seen_before = false;
        for (isize j = 0; j < i && !seen_before; j++) {
            InterfaceMember const* other = vec_interface_member_at(&my members, j);
            seen_before = interface_member_is_named_method(other) && cstr_eq(&other->key.name_string, &first->key.name_string);
        }
        if (seen_before) continue;
        // (A method of a class it merges with in this file: one symbol, the class checks it)
        bool class_merged = false;
        for (u32 d = 0; merged && d < merged->decl_count && !class_merged; d++) {
            class_merged = merged->decls[d].kind == SYMBOL_DECL_CLASS &&
                class_decl_merges_method(merged->decls[d].node, &first->key.name_string, span_source(first->key.span));
        }
        if (class_merged) continue;
        InterfaceMember const* canonical = merged && merged->decl_count > 1 ? canonical_method(merged, &first->key.name_string) : nullptr;
        if (!canonical) canonical = first;
        for (isize j = i; j < count; j++) {
            InterfaceMember const* other = vec_interface_member_at(&my members, j);
            if (other == canonical || !interface_member_is_named_method(other) || !cstr_eq(&other->key.name_string, &first->key.name_string)) continue;
            if (other->optional != canonical->optional) {
                error_ctx_error_d(errs, other->key.span, 2386, "Overload signatures must all be optional or required.");
            }
        }
    }
}

void interface_typecheck(Interface const* me, Symbol const* merged, ErrorCtx* errs) {
    for_each (i, vec_interface_member, &my members) {
        interface_member_typecheck(i, false, my type_literal, errs);
        // (A mapped type's other members, after TS7061, are only recovery: tsc's checkMappedType never visits them)
        if (my type_literal && i->key.tag == INTERFACE_KEY_MAPPED && i == my members.data) break;
    }
    interface_check_overload_optionality(me, merged, errs);
}
