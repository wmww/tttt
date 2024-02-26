#pragma once

#include "prelude.h"

typedef struct Error {
    const char* static_message; // not owned
    const char* location;
} Error;

#define i_key_class Error
#define i_no_clone
#define i_keydrop(err) (void)err;
#include "stc/vec.h"

typedef struct ErrorCtx {
    vec_Error errors;
} ErrorCtx;

void ErrorCtx_drop(ErrorCtx* self);
void ErrorCtx_display(ErrorCtx* self);
bool ErrorCtx_empty(ErrorCtx* self);
void ErrorCtx_append(ErrorCtx* self, ErrorCtx other);
void ErrorCtx_error(ErrorCtx* self, const char* location, const char* static_message);
