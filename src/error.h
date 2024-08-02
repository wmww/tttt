#pragma once

#include "span.h"

typedef struct Error Error;

wrap_forward_vec(Error, error);

typedef enum ErrorPhase {
    ERROR_PHASE_UNKOWN = 0,
    ERROR_PHASE_PARSE,
    ERROR_PHASE_RESOLVE_NAMES,
    ERROR_PHASE_TYPECHECK,
    ERROR_PHASE_TYPECHECK_2, // Required for unclear reasons
    ERROR_PHASE_RELATED, // Only used for sub-errors in another Error's related_errors vec
} ErrorPhase;

typedef struct Error {
    cstr message;
    Span span;
    unsigned ts_code;
    ErrorPhase phase;
    VecError related_errors;
} Error;

void error_drop(Error* me);
Error error_clone(Error me);
void cstr_append_error_debug(cstr* target, Error const* me);
/// noop if me is null
void error_related_error_s(Error* me, Span span, unsigned ts_code, cstr message);
/// noop if me is null
void error_related_error(Error* me, Span span, unsigned ts_code, const char* fmt, ...) __attribute__ ((format (printf, 4, 5)));

#define i_key_class_names Error, error
#define i_is_forward
#include "wrap_stc/vec.h"

typedef struct ErrorCtx {
    ErrorPhase phase;
    VecError errors;
} ErrorCtx;

static inline ErrorCtx error_ctx_init() { return (ErrorCtx){0}; }
void error_ctx_drop(ErrorCtx* me);
void error_ctx_sort(ErrorCtx* me);
void error_ctx_display(ErrorCtx* me);
bool error_ctx_empty(ErrorCtx* me);
void error_ctx_append_consume(ErrorCtx* me, ErrorCtx other);
void error_ctx_type_error(ErrorCtx* me, const char* static_message);
/// Returned Error can be used to add related errors, but is owned by the ErrorCtx.
Error* error_ctx_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message);
/// noop and returns null if me is null. Returned Error can be used to add related errors, but is owned by the ErrorCtx.
Error* error_ctx_error(ErrorCtx* me, Span span, unsigned ts_code, const char* fmt, ...) __attribute__ ((format (printf, 4, 5)));

#define TS_CODE_NONE 0
