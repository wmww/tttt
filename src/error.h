#pragma once

#include "span.h"
#include "display.h"
#include "compiler_options.h"

typedef struct Error Error;
typedef struct Program Program;

g_declare_vec(Error, error);

typedef enum ErrorPhase {
    ERROR_PHASE_UNKOWN = 0,
    ERROR_PHASE_PARSE,
    ERROR_PHASE_RESOLVE_NAMES,
    ERROR_PHASE_TYPECHECK
} ErrorPhase;

typedef struct Error {
    cstr message;
    Span span;
    unsigned ts_code;
    ErrorPhase phase;
    VecError sub_errors;
} Error;

void error_drop(Error* me);
Error error_clone(Error me);
/// noop if me is null
void error_sub_error_s(Error* me, Span span, unsigned ts_code, cstr message);
/// Builds the message from a display sequence. noop if me is null
#define error_sub_error_d(me, span, ts_code, ...) \
    error_sub_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))

#define g_class_key Error, error
#define g_declared
#include <cleat/generic/vec.h>

typedef struct ErrorCtx {
    ErrorPhase phase;
    VecError errors;
} ErrorCtx;

static inline ErrorCtx error_ctx_init() { return (ErrorCtx){0}; }
void error_ctx_drop(ErrorCtx* me);
extern intptr_t print_error_ctx_max_display; // tttt --all-errors lifts it
void print_error_ctx(ErrorCtx const* me);
bool error_ctx_empty(ErrorCtx const* me);
void error_ctx_clear(ErrorCtx* me);
Error* error_ctx_append_error(ErrorCtx* me, Error error);
/// Returned Error can be used to add related errors, but is owned by the ErrorCtx. Additional calls into this ErrorCtx
/// invalidate returned Error.
Error* error_ctx_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message);
/// Builds the message from a display sequence. noop and returns null if me is null. Returned Error can be used to add
/// related errors, but is owned by the ErrorCtx. Additional calls into this ErrorCtx invalidate returned Error.
#define error_ctx_error_d(me, span, ts_code, ...) \
    error_ctx_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))


#define TS_CODE_NONE 0
