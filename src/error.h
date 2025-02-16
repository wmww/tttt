#pragma once

#include "span.h"
#include "display.h"

typedef struct Error Error;
typedef struct Program Program;

wrap_forward_vec(Error, error);

typedef enum ErrorPhase {
    ERROR_PHASE_UNKOWN = 0,
    ERROR_PHASE_LOAD,
    ERROR_PHASE_PARSE,
    ERROR_PHASE_RESOLVE_NAMES,
    ERROR_PHASE_TYPECHECK,
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
void display_error(Display d, Error const* me);
/// noop if me is null
void error_related_error_s(Error* me, Span span, unsigned ts_code, cstr message);
/// noop if me is null
void error_related_error(Error* me, Span span, unsigned ts_code, const char* fmt, ...) __attribute__ ((format (printf, 4, 5)));

#define i_key_class_names Error, error
#define i_is_forward
#include "wrap_stc/vec.h"

typedef struct ErrorConfig {
    bool allow_unreachable_code : 1;
    bool allow_unused_labels : 1;
    bool always_strict : 1;
    bool exact_optional_property_types : 1;
    bool no_fallthrough_cases_in_switch : 1;
    bool no_implicit_any : 1;
    bool no_implicit_override : 1;
    bool no_implicit_returns : 1;
    bool no_implicit_this : 1;
    bool no_property_access_from_index_signature : 1;
    bool no_unchecked_indexed_access : 1;
    bool no_unused_locals : 1;
    bool no_unused_parameters : 1;
    bool strict_bind_call_apply : 1;
    bool strict_function_types : 1;
    bool strict_null_checks : 1;
    bool strict_property_initialization : 1;
    bool use_unknown_in_catch_variables : 1;
} ErrorConfig;

void error_config_enable_strict(ErrorConfig* me);

typedef struct ErrorCtx {
    ErrorPhase phase;
    intptr_t parse_error_count;
    VecError errors;
    ErrorConfig config;
} ErrorCtx;

static inline ErrorCtx error_ctx_init() { return (ErrorCtx){0}; }
void error_ctx_drop(ErrorCtx* me);
void error_ctx_sort(ErrorCtx* me);
void print_error_ctx(ErrorCtx const* me);
bool error_ctx_empty(ErrorCtx const* me);
void error_ctx_clear(ErrorCtx* me);
Error* error_ctx_append_error(ErrorCtx* me, Error error);
/// Returned Error can be used to add related errors, but is owned by the ErrorCtx. Additional calls into this ErrorCtx
/// invalidate returned Error.
Error* error_ctx_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message);
/// noop and returns null if me is null. Returned Error can be used to add related errors, but is owned by the ErrorCtx.
/// Additional calls into this ErrorCtx invalidate returned Error.
Error* error_ctx_error(ErrorCtx* me, Span span, unsigned ts_code, const char* fmt, ...) __attribute__ ((format (printf, 4, 5)));

extern ErrorConfig const* global_error_config_private;
void set_error_config(ErrorConfig const* conf);
void clear_error_config();
static inline ErrorConfig const* get_error_config() {
    PANIC_IF(!global_error_config_private, "global ErrorConfig unset");
    return global_error_config_private;
}

#define TS_CODE_NONE 0
