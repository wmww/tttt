#pragma once

#include "prelude.h"
#include "span.h"

typedef struct Error {
    cstr message;
    Span span;
    unsigned ts_code;
} Error;

void error_drop(Error* self);
Error error_clone(Error self);
void cstr_append_error_debug(cstr* target, Error const* self);
void cstr_append_error_txt(cstr* target, Error const* self);

#define i_key_class_names Error, error
#include "wrap_stc/vec.h"

typedef struct ErrorCtx {
    VecError errors;
} ErrorCtx;

static inline ErrorCtx error_ctx_init() { return (ErrorCtx){0}; }
void error_ctx_drop(ErrorCtx* self);
void error_ctx_display(ErrorCtx* self);
bool error_ctx_empty(ErrorCtx* self);
void error_ctx_append_consume(ErrorCtx* self, ErrorCtx other);
void error_ctx_parse_error(ErrorCtx* self, const char* static_message);
void error_ctx_type_error(ErrorCtx* self, const char* static_message);
void error_ctx_error_s(ErrorCtx* self, Span span, unsigned ts_code, cstr message);
void error_ctx_error(ErrorCtx* self, Span span, unsigned ts_code, const char* fmt, ...) __attribute__ ((format (printf, 4, 5)));

#define TS_CODE_NONE 0
