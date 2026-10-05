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
    // A grammar error the parser reports (tsc's checker's): not one parse error per position, see parse_file()
    bool grammar;
    bool suggestion; // tsc's DiagnosticCategory.Suggestion (only with CompilerOptions.capture_suggestions)
    // A diagnostic tsc reports once per program, from whichever file asks first (a checker-wide memo): non-zero, what
    // it is about. Each checker reports it once; error_ctx_keep_first_once() keeps the one with the lowest once_order
    // (the index of the file being checked), which is the one a single checker reports
    u64 once_key;
    u32 once_order;
    VecError sub_errors;
} Error;

// tsc's DiagnosticCategory of a diagnostic as the baselines print it: "error", "message" (TS1450) or "suggestion"
char const* error_category_name(Error const* error);

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
    // A checker's own diagnostics go into a context of their own, merged into the program's after checking: the
    // program's (frozen meanwhile) is its parent, which error_ctx_find() looks in too
    struct ErrorCtx* parent;
    // Nothing may be reported into it: every checker reports into its own (a report is log_error()ed)
    bool frozen;
} ErrorCtx;

static inline ErrorCtx error_ctx_init() { return (ErrorCtx){0}; }
// An empty context to collect errors in for a while (to merge into `like` or drop): the phase is `like`'s, or
// @ts-expect-error/@ts-ignore would miss the errors merged back
static inline ErrorCtx error_ctx_scratch(ErrorCtx const* like) { return (ErrorCtx){ .phase = like->phase }; }
void error_ctx_drop(ErrorCtx* me);
// tsc's formatDiagnosticsWithColorAndContext (`--pretty`) of one diagnostic, lines ending in nl, file_name naming the
// files of the diagnostic and its related information
typedef char const* (*ErrorFileNamer)(SourceFile const* source);
void error_format_pretty(cstr* out, Error const* me, char const* newline, ErrorFileNamer file_name);
// tsc's getErrorSummaryText: "Found 3 errors in 2 files." and a table of them (errors in the order they're printed)
void error_format_pretty_summary(cstr* out, VecError const* errors, char const* newline, ErrorFileNamer file_name);

extern intptr_t print_error_ctx_max_display; // tttt --all-errors lifts it
void print_error_ctx(ErrorCtx const* me);
// What print_error_ctx() prints, appended to *out
void display_error_ctx(ErrorCtx const* me, cstr* out);
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
/// Moves the diagnostics of from into me (from's global diagnostics that me has already are dropped)
void error_ctx_merge(ErrorCtx* me, ErrorCtx* from);
/// Of the diagnostics of the count contexts with the same Error.once_key, drops all but the one with the lowest
/// once_order
void error_ctx_keep_first_once(ErrorCtx* ctxs, isize count);
/// What tsc's sortAndDeduplicateDiagnostics does to the diagnostics its harness prints: equal locations, codes and head
/// messages are ordered by the message chain and the related information, then only the first of those with the same
/// head message stays. Afterwards the order doesn't depend on the order the diagnostics were reported in
void error_ctx_sort_and_dedupe(ErrorCtx* me);
/// The error with this span and code, if there is one (also in the parent)
Error* error_ctx_find(ErrorCtx* me, Span span, unsigned ts_code);
/// Semantic diagnostics (name resolution and typecheck) are not reported inside files that are SourceFile.unchecked
void error_ctx_remove_unchecked(ErrorCtx* me);
#define error_ctx_error_d(me, span, ts_code, ...) \
    error_ctx_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))
/// An error that may be in a lib file (error_ctx_error_s drops those): the lib side of a conflict with a user file's
/// declaration, which tsc reports too
Error* error_ctx_lib_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message);
#define error_ctx_lib_error_d(me, span, ts_code, ...) \
    error_ctx_lib_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))
/// A grammar error the checker finds (tsc's grammarErrorOnNode): not reported in a file with parse errors
Error* error_ctx_grammar_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message);
#define error_ctx_grammar_error_d(me, span, ts_code, ...) \
    error_ctx_grammar_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))


#define TS_CODE_NONE 0
