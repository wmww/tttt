#include "callable_type.h"
#include "function_impl.h"
#include "error.h"

void callable_type_drop(CallableType* me) {
    function_impl_drop(my function);
    free(my function);
}

void callable_type_resolve_names(CallableType* me, Scope* names, ErrorCtx* errs) {
    function_impl_lookup_names(my function, false, names, errs);
}

void callable_type_typecheck(CallableType const* me, ErrorCtx* errs) {
    function_impl_typecheck_signature(my function, true, errs);
    type_typecheck(&my function->ret, errs);
}
