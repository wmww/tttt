#include "error.h"

void ErrorCtx_drop(ErrorCtx* self) {
    vec_Error_drop(&self->errors);
}

void ErrorCtx_display(ErrorCtx* self) {
    c_foreach(i, vec_Error, self->errors) {
        printf(CLR_ERR "Error:" CLR0 " %s\n", i.ref->static_message);
    }
}

bool ErrorCtx_empty(ErrorCtx* self) {
    return vec_Error_empty(&self->errors);
}

void ErrorCtx_append(ErrorCtx* self, ErrorCtx other) {
    c_foreach(i, vec_Error, other.errors) {
        vec_Error_push(&self->errors, *i.ref);
        *i.ref = (Error){0};
    }
    ErrorCtx_drop(&other);
}

void ErrorCtx_parse_error(ErrorCtx* self, const char* location, const char* static_message) {
    if (!self) {
        return;
    }
    vec_Error_push(&self->errors, (Error){
        .location = location,
        .static_message = static_message,
    });
}

void ErrorCtx_type_error(ErrorCtx* self, const char* static_message) {
    if (!self) {
        return;
    }
    vec_Error_push(&self->errors, (Error){
        .location = nullptr,
        .static_message = static_message,
    });
}
