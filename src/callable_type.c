#include "callable_type.h"
#include "function_impl.h"
#include "error.h"
#include "scope.h"
#include "binding.h"

void callable_type_drop(CallableType* me) {
    function_impl_drop(my function);
    free(my function);
}

static bool is_type_keyword(csview name) {
    static char const* const keywords[] = {
        "any", "unknown", "number", "bigint", "object", "boolean", "string", "symbol", "void", "undefined", "never",
    };
    for (u32 i = 0; i < c_arraylen(keywords); i++) {
        if (csview_equals(name, keywords[i])) return true;
    }
    return false;
}

void callable_type_resolve_names(CallableType* me, Scope* names, ErrorCtx* errs) {
    function_impl_lookup_names(my function, false, names, errs);
    // (tsc's reportImplicitAny: the name resolves as a type where the signature is, or is a type keyword)
    for_each (i, vec_function_arg, &my function->args) {
        Binding const* binding = &i->initializer.binding;
        if (binding->tag != BINDING_IDENTIFIER || binding->has_explicit_type) continue;
        csview name = binding->content_span.sv;
        i->name_is_type = is_type_keyword(name) || scope_get_type(names, name, nullptr);
    }
}

void callable_type_typecheck(CallableType const* me, ErrorCtx* errs) {
    // A JSDoc `function(...)` without `: R` (tsc's checkJSDocFunctionType, whatever noImplicitAny says)
    if (my function->jsdoc_type && !my function->jsdoc_callback && !my function->jsdoc_unchecked && !my function->ret_explicit &&
        !my function->is_constructor
    ) {
        error_ctx_error_d(errs, my function->full_span, 7014,
            "Function type, which lacks return-type annotation, implicitly has an 'any' return type.");
    }
    function_impl_typecheck_signature(my function, true, errs);
    function_impl_check_implicit_any_parameters(my function, errs);
    for_each (i, vec_function_arg, &my function->args) {
        if (i->initializer.binding.has_explicit_type) type_typecheck(&i->initializer.binding.explicit_type, errs);
    }
    type_typecheck(&my function->ret, errs);
    if (my function->predicate && my function->predicate->has_type) type_typecheck(&my function->predicate->type, errs);
}
