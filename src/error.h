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
    bool detached; // Related information without a location (tsc's createDetachedDiagnostic)
    VecError sub_errors;
} Error;

void error_drop(Error* me);
Error error_clone(Error me);
/// noop if me is null
void error_sub_error_s(Error* me, Span span, unsigned ts_code, cstr message);
/// Builds the message from a display sequence. noop if me is null
#define error_sub_error_d(me, span, ts_code, ...) \
    error_sub_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))

/// Related information (tsc's relatedInformation): printed after the whole message chain. noop if me is null or the
/// span is invalid (a sub error without a span is a line of the chain)
void error_add_related_s(Error* me, Span span, unsigned ts_code, cstr message);
#define error_add_related_d(me, span, ts_code, ...) \
    error_add_related_s(me, span, ts_code, cstr_d(__VA_ARGS__))
/// Related information without a location (tsc's detached diagnostics, like a regular expression's "Did you mean"
/// suggestions). noop if me is null
void error_add_detached_related_s(Error* me, unsigned ts_code, cstr message);

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
/// A diagnostic without a file (tsc's global and option diagnostics). They sort before every other error
Error* error_ctx_global_error_s(ErrorCtx* me, unsigned ts_code, cstr message);
#define error_ctx_global_error_d(me, ts_code, ...) \
    error_ctx_global_error_s(me, ts_code, cstr_d(__VA_ARGS__))
/// The error with this span and code, if there is one
Error* error_ctx_find(ErrorCtx* me, Span span, unsigned ts_code);
/// Semantic diagnostics (name resolution and typecheck) are not reported inside files that are SourceFile.unchecked
void error_ctx_remove_unchecked(ErrorCtx* me);
#define error_ctx_error_d(me, span, ts_code, ...) \
    error_ctx_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))
/// A grammar error the checker finds (tsc's grammarErrorOnNode): not reported in a file with parse errors
Error* error_ctx_grammar_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message);
#define error_ctx_grammar_error_d(me, span, ts_code, ...) \
    error_ctx_grammar_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))


#define TS_CODE_NONE 0
