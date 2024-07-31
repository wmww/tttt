#pragma once

#include "span.h"

typedef enum ErrorPhase {
    ERROR_PHASE_UNKOWN = 0,
    ERROR_PHASE_PARSE,
    ERROR_PHASE_RESOLVE_NAMES,
    ERROR_PHASE_TYPECHECK,
    ERROR_PHASE_TYPECHECK_2,
} ErrorPhase;

typedef struct Error {
    cstr message;
    Span span;
    unsigned ts_code;
    ErrorPhase phase;
} Error;

void error_drop(Error* me);
Error error_clone(Error me);
void cstr_append_error_debug(cstr* target, Error const* me);
void cstr_append_error_txt(cstr* target, Error const* me);

#define i_key_class_names Error, error
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
void error_ctx_append_error(ErrorCtx* me, Error error);
void error_ctx_append_consume(ErrorCtx* me, ErrorCtx other);
void error_ctx_parse_error(ErrorCtx* me, const char* static_message);
void error_ctx_type_error(ErrorCtx* me, const char* static_message);
void error_ctx_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message);
void error_ctx_error(ErrorCtx* me, Span span, unsigned ts_code, const char* fmt, ...) __attribute__ ((format (printf, 4, 5)));

#define TS_CODE_NONE 0
