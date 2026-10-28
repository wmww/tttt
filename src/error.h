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
    // Of a JSDoc comment (tsc's jsdocDiagnostics): reported for checked JavaScript only, whatever the code
    bool jsdoc;
    // One of tsc's getJSSyntacticDiagnosticsForFile (a JavaScript file's TypeScript-only syntax): reported whatever the
    // file's parse errors and in unchecked JavaScript, even with a grammar error's code (TS1206)
    bool js_syntax;
    bool unchecked_js; // Only reported where the checker doesn't check the JavaScript file (a checked one's is its own)
    // A program diagnostic at an import (tsgo's include processor: TS6059, TS6307...): plain JavaScript reports it whatever
    // its code, it isn't the checker's (GetIncludeProcessorDiagnostics only skips files canIncludeBindAndCheckDiagnostics
    // rejects)
    bool include_processor;
    // A scanner error about a token (parser.c's scanner_error()): the parser decided whether a parse error at its position
    // is kept, tsc's order of the two differs from tttt's
    bool scanner;
    bool suggestion; // tsc's DiagnosticCategory.Suggestion (only with CompilerOptions.capture_suggestions)
    // A declaration diagnostic (tsc's getDeclarationDiagnostics, TS4xxx/TS9xxx): emit reports them whatever gated the
    // semantic ones
    bool declaration;
    // A history-dependent diagnostic (error_is_history_dependent()) reported in another file than the one its checker was
    // checking (error_checking_file), or a circularity while a cycle through another file's declarations was found
    // (error_cycle_foreign): what it is about was reached from elsewhere, where one checker in program order may reach it
    // first from a third file (program.c's history_dependent_matters())
    bool history_foreign;
    // A circularity reported while a cycle of more than one function's return type was found in this file
    // (error_cycle_entry_dependent): where the cycle is entered may decide what is reported
    bool history_entry_dependent;
    // A diagnostic tsc reports once per program, from whichever file asks first (a checker-wide memo): non-zero, what
    // it is about. Each checker reports it once; error_ctx_keep_first_once() keeps the one with the lowest once_order
    // (the index of the file being checked), which is the one a single checker reports
    u64 once_key;
    u32 once_order;
    // A private member nothing read (TS6133/TS6138, unused.c): reported unless a file checked no later than this one
    // (unread_order, its index among the checked files) read it in any checker (program_member_referenced()), which is
    // what one checker in program order sees when it reports the file's unused members (tsc's checkSourceFile)
    char const* unread_member;
    u32 unread_order;
    // A content mapper's own diagnostic (tsgo's external diagnostic, plan 56): its diagnostic source, printed in
    // place of `TS` (`box1000`); owned by the file's ContentMapped. Null for tttt's own
    char const* source;
    VecError sub_errors;
} Error;

// tsc's DiagnosticCategory of a diagnostic as the baselines print it: "error", "message" (TS1450) or "suggestion"
char const* error_category_name(Error const* error);
// What the code is printed after: `TS`, or a content mapper's diagnostic source
static inline char const* error_code_prefix(Error const* error) { return error->source ? error->source : "TS"; }

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
// While a checker checks its share (program.c's check_share()): its program's flag, set when a diagnostic whose site depends on what
// the checker saw before is reported into any context (error_is_history_dependent(): program_typecheck()'s rerun in one
// checker)
extern thread_local _Atomic bool* error_history_flag;
// While a checker checks its share in a parallel pass that a history-dependent diagnostic may make rerun in one checker
// (program.c's check_share()), else null: `stop` is set once the rerun is certain, and every checker then drops what
// it is checking at its next statement (typecheck_stopping()); `history` is the program's history_reported, and once
// it is set `poll` looks for a diagnostic of this checker's that makes the rerun certain (setting `stop`)
typedef struct TypecheckStop TypecheckStop;
struct TypecheckStop {
    _Atomic bool* stop;
    _Atomic bool* history;
    bool (*poll)(TypecheckStop* me);
};
extern thread_local TypecheckStop* typecheck_stop;
// Whether the checker should drop the rest of what it is checking (statements_typecheck(), check_file()): its results
// and diagnostics go unread
static inline bool typecheck_stopping(void) {
    TypecheckStop* s = typecheck_stop;
    if (__builtin_expect(s == nullptr, 1)) return false;
    if (atomic_load_explicit(s->stop, memory_order_relaxed)) return true;
    return __builtin_expect(atomic_load_explicit(s->history, memory_order_relaxed), 0) && s->poll(s);
}
// The file the thread's checker is checking (program.c's check_file()), null outside one: see Error.history_foreign
extern thread_local struct SourceFile const* error_checking_file;
// A cycle found while checking error_checking_file went through another file's declarations (ty/checker.c's
// resolving_mark_cycle()): a circularity reported then is Error.history_foreign. Reset per checked file
extern thread_local bool error_cycle_foreign;
// A cycle found while checking error_checking_file was more than one function's return type (resolving_mark_cycle()): a
// circularity reported then is Error.history_entry_dependent. Reset per checked file
extern thread_local bool error_cycle_entry_dependent;
// The diagnostics a checker reports where it first runs into something it then caches: the instantiation and relation
// limits (TS2589, TS5114, TS5115, TS2590, TS2321, TS2859) and the circularities (TS2615, TS7022, TS7023, TS7024, TS2456,
// TS2502). Which file reaches it first decides where, and what the cached error type silences afterwards
static inline bool error_is_history_dependent(unsigned ts_code) {
    switch (ts_code) {
        case 2589: case 5114: case 5115: case 2590: case 2321: case 2859:
        case 2615: case 7022: case 7023: case 7024: case 2456: case 2502:
            return true;
        default:
            return false;
    }
}
// The circularities among them (the rest are the limits)
static inline bool error_is_circularity(unsigned ts_code) {
    return ts_code == 2615 || ts_code == 7022 || ts_code == 7023 || ts_code == 7024 || ts_code == 2456 || ts_code == 2502;
}
// tsc's formatDiagnosticsWithColorAndContext (`--pretty`) of one diagnostic, lines ending in nl, file_name naming the
// files of the diagnostic and its related information
typedef char const* (*ErrorFileNamer)(SourceFile const* source);
void error_format_pretty(cstr* out, Error const* me, char const* newline, ErrorFileNamer file_name);
// tsc's formatDiagnostic (`--pretty false`) of one diagnostic: `file(line,col): error TSxxxx: ` and the message chain
void error_format_plain(cstr* out, Error const* me, char const* newline, ErrorFileNamer file_name);
// What tsc's error summary says about the diagnostics printed so far (kept past their program): how many errors, and
// the files with errors in the order they were printed, each with its first error's line and its error count
typedef struct ErrorSummaryFile {
    cstr name;
    cstr path; // What the table is sorted by: the file's absolute path (tsgo's FileName()), source_file_message_path()
    unsigned line;
    isize errors;
} ErrorSummaryFile;
typedef struct ErrorSummary {
    isize errors;
    bool first_has_file; // The first error has a file
    isize file_count;
    ErrorSummaryFile* files;
    bool repeated_names; // Some rows have the same name (content-mapped files': error_summary_add())
} ErrorSummary;
void error_summary_add(ErrorSummary* me, VecError const* errors, ErrorFileNamer file_name);
// Adds from's to me, leaving from empty
void error_summary_merge(ErrorSummary* me, ErrorSummary* from);
void error_summary_drop(ErrorSummary* me);
// tsc's getErrorSummaryText: "Found 3 errors in 2 files." and a table of them (without_files: just "Found 3 errors.",
// what tsgo's -b says)
void error_summary_format(cstr* out, ErrorSummary const* me, char const* newline, bool with_files);
// The summary of the errors alone
void error_format_pretty_summary(cstr* out, VecError const* errors, char const* newline, ErrorFileNamer file_name);
// tsc's sortAndDeduplicateDiagnostics, what its CLI does to all the diagnostics of a program before printing them: by
// the path of the file (path() of it, the diagnostics without one first), then compareDiagnostics; of those with the
// same file, location, code and head message only the first stays
void error_vec_sort_and_dedupe(VecError* errors, cstr (*path)(SourceFile const* source));
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
/// A grammar error the checker finds (tsc's grammarErrorOnNode): not reported in a file with parse errors
Error* error_ctx_grammar_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message);
#define error_ctx_grammar_error_d(me, span, ts_code, ...) \
    error_ctx_grammar_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))


#define TS_CODE_NONE 0
