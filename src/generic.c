#include "generic.h"
#include "err.h"
#include "name_stack.h"

void GenericItem_drop(GenericItem* self) {
    cstr_drop(&self->name);
    Type_drop(&self->extends);
    Type_drop(&self->fallback);
}

GenericItem GenericItem_clone(GenericItem self) {
    return (GenericItem){
        .name = cstr_clone(self.name),
        .extends = Type_clone(self.extends),
        .fallback_explicit = self.fallback_explicit,
        .fallback = Type_clone(self.fallback),
    };
}

void cstr_append_vec_generic_item_debug(cstr* target, vec_GenericItem* generics, int indent) {
    if (vec_GenericItem_empty(generics)) {
        return;
    }
    cstr_append_sv(target, c_sv("<"));
    c_foreach(i, vec_GenericItem, *generics) {
        cstr_append_sv(target, c_sv(CLR_TYPE));
        cstr_append_s(target, i.ref->name);
        cstr_append_sv(target, c_sv(CLR0));
        if (i.ref->extends.tag != TYPE_ANY) {
            cstr_append_sv(target, c_sv(" extends "));
            cstr_append_type_debug(target, &i.ref->extends, indent);
        }
        if (i.ref->fallback_explicit) {
            cstr_append_sv(target, c_sv(" = "));
            cstr_append_type_debug(target, &i.ref->fallback, indent);
        }
        if (i.ref < i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(">"));
}

void Generic_drop(Generic* self) {
    vec_GenericItem_drop(&self->items);
    Type_drop(&self->inner);
}

Generic Generic_clone(Generic self) {
    return (Generic) {
        .items = vec_GenericItem_clone(self.items),
        .inner = Type_clone(self.inner),
    };
}

void cstr_append_generic_debug(cstr* target, Generic* self, int indent) {
    if (self->inner.tag == TYPE_CALLABLE) {
        cstr_append_vec_generic_item_debug(target, &self->items, indent);
        cstr_append_type_debug(target, &self->inner, indent);
    } else {
        cstr_append_type_debug(target, &self->inner, indent);
        cstr_append_vec_generic_item_debug(target, &self->items, indent);
    }
}

void Generic_resolve_names(Generic* self, NameStack* names, ErrorCtx* errs) {
    NameStack_push_frame(names, true);
    long generic_count = vec_GenericItem_size(&self->items);
    for (long i = 0; i < generic_count; i++) {
        hmap_str_NamedType_result result = hmap_str_NamedType_insert(
            &vec_NameStackFrame_back(&names->stack)->types,
            cstr_clone(vec_GenericItem_at(&self->items, i)->name),
            (NamedType){
                .type = &vec_GenericItem_at(&self->items, i)->extends,
                .is_generic = true,
                .generic_index = i,
            }
        );
        if (!result.inserted) {
            // name is cstr_str(&result.ref->first)
            ErrorCtx_type_error(errs, "multiple function generics with same name");
        }
    }
    Type_resolve_names(&self->inner, names, errs);
    NameStack_pop_frame(names);
}

Type Generic_callable_infer(Generic const* self, vec_Expr const* args, ErrorCtx* errs) {
    (void)self;
    (void)args;
    (void)errs;
    return Type_any;
}
