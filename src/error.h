#pragma once

#include "prelude.h"
#include "span.h"

typedef struct Error {
    cstr message;
    Span span;
    unsigned ts_code;
} Error;

void Error_drop(Error* self);
Error Error_clone(Error self);
void cstr_append_error_debug(cstr* target, Error const* self);

#define i_key_class Error
#include "stc/vec.h"

typedef struct ErrorCtx {
    vec_Error errors;
} ErrorCtx;

void ErrorCtx_drop(ErrorCtx* self);
void ErrorCtx_display(ErrorCtx* self);
bool ErrorCtx_empty(ErrorCtx* self);
void ErrorCtx_append_consume(ErrorCtx* self, ErrorCtx other);
void ErrorCtx_parse_error(ErrorCtx* self, const char* static_message);
void ErrorCtx_type_error(ErrorCtx* self, const char* static_message);
void ErrorCtx_add_error_s(ErrorCtx* self, Span span, unsigned ts_code, cstr message);
void ErrorCtx_error(ErrorCtx* self, Span span, unsigned ts_code, const char* fmt, ...) __attribute__ ((format (printf, 4, 5)));
