#include "error.h"
#include "string_helpers.h"
#include "source_file.h"
#include "display.h"
#include <math.h>

void error_drop(Error* me) {
    cstr_drop(&my message);
    vec_error_drop(&my sub_errors);
}

Error error_clone(Error me) {
    me.message = cstr_clone(me.message);
    me.sub_errors = vec_error_clone(me.sub_errors);
    return me;
}

void display_error(Display d, Error const* me) {
    unsigned line = span_line_index(my span) + 1;
    cstr prefix = cstr_null;

    {
        Display prefix_d = d;
        prefix_d.target = &prefix;
        for (int i = 0; i < d.indent; i++) {
            display_colored(prefix_d, color_implicit, " |  ");
        }
    }

    display_s(d, prefix);
    if (span_valid(my span) && span_source(my span)) {
        display_color(d, color_path);
        display_str(d, cstr_str(&span_source(my span)->canonical_path));
        display_color(d, color_line_number);
        display_lit(d, ":");
        display_i64(d, line);
    }
    display_color(d, color_implicit);
    // Related information without a code has none to show (a diagnostic without a file is still an error)
    if (my ts_code || !d.indent) {
        if (span_valid(my span)) display_lit(d, " ");
        display_lit(d, "[TS");
        display_i64(d, my ts_code);
        display_lit(d, "] ");
    }
    display_color(d, color_error);
    display_s(d, my message);
    display_color(d, color_normal);

    if (span_valid(my span)) {
        const char* line_start = span_line_start(my span);
        const char* line_end = span_line_end(my span);
        unsigned col = span_col_index(my span);

        display_lit(d, "\n");
        display_s(d, prefix);
        display_lit(d, "\n");
        display_s(d, prefix);
        display_sv(d, csview_from_range(line_start, line_end));
        display_s(d, prefix);
        display_lit(d, "\n");
        display_s(d, prefix);

        display_repeated(d, c_sv(" "), col);

        display_color(d, color_error);
        display_repeated(d, c_sv("-"), my span.size ? my span.size : 1);
        display_color(d, color_normal);
    }

    d.indent++;
    for_each (i, vec_error, &my sub_errors) {
        display_lit(d, "\n");
        display_error(d, i);
    }

    cstr_drop(&prefix);
}

impl_ast_display(Error, error)

void error_sub_error_s(Error* me, Span span, unsigned ts_code, cstr message) {
    if (!me) {
        cstr_drop(&message);
        return;
    }
    vec_error_push(&my sub_errors, (Error){
        .message = message,
        .span = span,
        .ts_code = ts_code
    });
}

void error_add_related_s(Error* me, Span span, unsigned ts_code, cstr message) {
    if (!span_valid(span) || !span_source(span)) {
        cstr_drop(&message);
        return;
    }
    error_sub_error_s(me, span, ts_code, message);
}

void error_add_detached_related_s(Error* me, unsigned ts_code, cstr message) {
    if (!me) {
        cstr_drop(&message);
        return;
    }
    vec_error_push(&my sub_errors, (Error){ .message = message, .ts_code = ts_code, .detached = true });
}

void error_ctx_drop(ErrorCtx* me) {
    vec_error_drop(&my errors);
}

// Pretty diagnostics (tsc's program.ts and watch.ts), with tsc's escape sequences

#define PRETTY_RESET "\x1b[0m"
#define PRETTY_GUTTER "\x1b[7m"
#define PRETTY_RED "\x1b[91m"
#define PRETTY_YELLOW "\x1b[93m"
#define PRETTY_CYAN "\x1b[96m"
#define PRETTY_GREY "\x1b[90m"

// tsc's formatLocation: file:line:column, 1-based
static void pretty_location(cstr* out, Span span, ErrorFileNamer file_name) {
    cstr_append_d(out, DEFAULT_DISPLAY,
        PRETTY_CYAN, file_name(span_source(span)), PRETTY_RESET ":" PRETTY_YELLOW, span_line_index(span) + 1, PRETTY_RESET ":"
        PRETTY_YELLOW, span_col_index(span) + 1, PRETTY_RESET);
}

// Go's strings.TrimRightFunc(unicode.IsSpace) of a line's text (some of it), for what tsc's source files contain
static char const* pretty_trim_end(char const* start, char const* end) {
    while (end > start) {
        unsigned char c = (unsigned char)end[-1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') end--;
        else if (end - start >= 2 && (unsigned char)end[-2] == 0xC2 && c == 0xA0) end -= 2; // U+00A0
        else if (end - start >= 3 && (unsigned char)end[-3] == 0xE2 && (unsigned char)end[-2] == 0x80 && (c & 0xFE) == 0xA8) end -= 3;
        else break;
    }
    return end;
}

// tsc's formatCodeSpan: the lines of the span with a gutter of line numbers, squiggled (UTF-16 code units, like tsc)
static void pretty_code_span(cstr* out, Span span, char const* indent, char const* squiggle, char const* newline) {
    SourceFile const* source = span_source(span);
    char const* text = source_file_start(source);
    char const* text_end = text + cstr_size(&source->contents);
    unsigned first_line = span_line_index(span);
    unsigned last_line = source_file_line_index(source, span_end_ptr(span));
    unsigned last_line_in_file = source_file_line_index(source, text_end);
    unsigned first_char = span_col_index(span);
    unsigned last_char = 0;
    for (char const* c = text + source->line_starts[last_line]; c < span_end_ptr(span); c++) last_char += utf8_byte_utf16_units(c, text + source->line_starts[last_line]);
    if (span_start_ptr(span) == span_end_ptr(span)) last_char++; // (the character after an empty span)

    bool more_than_five = last_line - first_line >= 4;
    char digits[16];
    int gutter = snprintf(digits, sizeof(digits), "%u", last_line + 1);
    if (more_than_five && gutter < 3) gutter = 3;
    for (unsigned i = first_line; i <= last_line; i++) {
        cstr_append(out, newline);
        if (more_than_five && first_line + 1 < i && i < last_line - 1) {
            cstr_append_d(out, DEFAULT_DISPLAY, indent, PRETTY_GUTTER, repeated_d(" ", gutter - 3), "..." PRETTY_RESET " ", newline);
            i = last_line - 1;
        }
        char const* line_start = text + source->line_starts[i];
        char const* line_end = i < last_line_in_file ? text + source->line_starts[i + 1] : text_end;
        line_end = pretty_trim_end(line_start, line_end);
        int width = snprintf(digits, sizeof(digits), "%u", i + 1);
        cstr_append_d(out, DEFAULT_DISPLAY, indent, PRETTY_GUTTER, repeated_d(" ", gutter - width), digits, PRETTY_RESET " ");
        for (char const* c = line_start; c < line_end; c++) cstr_append_n(out, *c == '\t' ? " " : c, 1);
        cstr_append(out, newline);
        cstr_append_d(out, DEFAULT_DISPLAY, indent, PRETTY_GUTTER, repeated_d(" ", gutter), PRETTY_RESET " ", squiggle);
        // Columns in UTF-16 units: spaces up to the first character, then tildes (tsgo's writeCodeSnippet)
        unsigned line_units = 0;
        for (char const* c = line_start; c < line_end; c++) line_units += utf8_byte_utf16_units(c, line_start);
        if (i == first_line) {
            unsigned to = i == last_line ? last_char : line_units;
            cstr_append_d(out, DEFAULT_DISPLAY, repeated_d(" ", first_char), repeated_d("~", to > first_char ? to - first_char : 0));
        } else {
            cstr_append_d(out, DEFAULT_DISPLAY, repeated_d("~", i == last_line ? last_char : line_units));
        }
        cstr_append_lit(out, PRETTY_RESET);
    }
}

// tsc's flattenDiagnosticMessageText: the chain (sub errors without a location) indented by depth
static void pretty_message(cstr* out, Error const* me, char const* newline, int depth) {
    if (depth) cstr_append_d(out, DEFAULT_DISPLAY, newline, repeated_d("  ", depth));
    cstr_append_s(out, my message);
    for_each (i, vec_error, &my sub_errors) {
        if (!i->detached && !span_valid(i->span)) pretty_message(out, i, newline, depth + 1);
    }
}

// tsgo's FormatDiagnosticWithColorAndContext: each related information at a location as `  file:line:col - message`
// over its code frame, then a line break (only that for one without a location)
static void pretty_related(cstr* out, Error const* me, char const* newline, ErrorFileNamer file_name) {
    for_each (i, vec_error, &my sub_errors) {
        if (i->detached) {
            cstr_append(out, newline);
        } else if (span_valid(i->span) && span_source(i->span)) {
            cstr_append_d(out, DEFAULT_DISPLAY, newline, "  ");
            pretty_location(out, i->span, file_name);
            cstr_append_lit(out, " - ");
            pretty_message(out, i, newline, 0);
            pretty_code_span(out, i->span, "    ", PRETTY_CYAN, newline);
            cstr_append(out, newline);
        } else {
            pretty_related(out, i, newline, file_name);
        }
    }
}

static bool has_related(Error const* me) {
    for_each (i, vec_error, &my sub_errors) {
        if (i->detached || (span_valid(i->span) && span_source(i->span)) || has_related(i)) return true;
    }
    return false;
}

void error_format_pretty(cstr* out, Error const* me, char const* newline, ErrorFileNamer file_name) {
    bool located = span_valid(my span) && span_source(my span);
    if (located) {
        pretty_location(out, my span, file_name);
        cstr_append_lit(out, " - ");
    }
    cstr_append_d(out, DEFAULT_DISPLAY, PRETTY_RED "error" PRETTY_RESET PRETTY_GREY " ", error_code_prefix(me), my ts_code, ": " PRETTY_RESET);
    pretty_message(out, me, newline, 0);
    if (located) {
        cstr_append(out, newline);
        pretty_code_span(out, my span, "", PRETTY_RED, newline);
        cstr_append(out, newline);
    }
    bool related = has_related(me);
    pretty_related(out, me, newline, file_name);
    if (!located && !related) cstr_append(out, newline);
}

// tsc's formatDiagnostic (`--pretty false`): file(line,column): error TSxxxx: the flattened message chain
void error_format_plain(cstr* out, Error const* me, char const* newline, ErrorFileNamer file_name) {
    if (span_valid(my span) && span_source(my span)) {
        cstr_append_d(out, DEFAULT_DISPLAY, file_name(span_source(my span)), "(", span_line_index(my span) + 1, ",",
            span_col_index(my span) + 1, "): ");
    }
    cstr_append_d(out, DEFAULT_DISPLAY, error_category_name(me), " ", error_code_prefix(me), my ts_code, ": ");
    pretty_message(out, me, newline, 0);
    cstr_append(out, newline);
}

void error_summary_add(ErrorSummary* me, VecError const* errors, ErrorFileNamer file_name) {
    for_each (i, vec_error, errors) {
        if (strcmp(error_category_name(i), "error")) continue;
        SourceFile const* source = span_valid(i->span) ? span_source(i->span) : nullptr;
        if (!my errors) my first_has_file = source != nullptr;
        my errors++;
        if (!source) continue;
        char const* name = file_name(source);
        isize j = 0;
        while (j < my file_count && strcmp(cstr_str(&my files[j].name), name)) j++;
        // (tsgo's diagnosticwriter shows each diagnostic of a content-mapped file against an original text object of
        // its own, and its summary groups by that object: one row each)
        if (source->content_original) j = my file_count;
        if (j == my file_count) {
            my files = realloc(my files, sizeof(ErrorSummaryFile) * (size_t)(my file_count + 1));
            my files[my file_count++] = (ErrorSummaryFile){ cstr_from(name), source_file_message_path(source),
                span_line_index(i->span) + 1, 0 };
        }
        my files[j].errors++;
    }
}

void error_summary_merge(ErrorSummary* me, ErrorSummary* from) {
    if (!my errors) my first_has_file = from->first_has_file;
    my errors += from->errors;
    for (isize i = 0; i < from->file_count; i++) {
        isize j = 0;
        while (j < my file_count && !cstr_eq(&my files[j].name, &from->files[i].name)) j++;
        if (j < my file_count) {
            my files[j].errors += from->files[i].errors;
            cstr_drop(&from->files[i].name);
            cstr_drop(&from->files[i].path);
            continue;
        }
        my files = realloc(my files, sizeof(ErrorSummaryFile) * (size_t)(my file_count + 1));
        my files[my file_count++] = from->files[i];
    }
    free(from->files);
    *from = (ErrorSummary){0};
}

void error_summary_drop(ErrorSummary* me) {
    for (isize i = 0; i < my file_count; i++) c_drop(cstr, &my files[i].name, &my files[i].path);
    free(my files);
    *me = (ErrorSummary){0};
}

void error_summary_format(cstr* out, ErrorSummary const* me, char const* newline, bool with_files) {
    if (!my errors) return;
    isize file_count = with_files ? my file_count : 0;
    cstr first = cstr_null;
    if (file_count) first = cstr_d(my files[0].name, PRETTY_GREY ":", my files[0].line, PRETTY_RESET);
    cstr_append(out, newline);
    if (my errors == 1) {
        if (file_count && my first_has_file) cstr_append_d(out, DEFAULT_DISPLAY, "Found 1 error in ", first);
        else cstr_append_lit(out, "Found 1 error.");
    } else if (!file_count) {
        cstr_append_d(out, DEFAULT_DISPLAY, "Found ", my errors, " errors.");
    } else if (file_count == 1) {
        cstr_append_d(out, DEFAULT_DISPLAY, "Found ", my errors, " errors in the same file, starting at: ", first);
    } else {
        cstr_append_d(out, DEFAULT_DISPLAY, "Found ", my errors, " errors in ", file_count, " files.");
    }
    cstr_append(out, newline);
    cstr_append(out, newline);
    if (file_count > 1) {
        // tsgo's writeTabularErrorsDisplay: the files by absolute path, the counts right-aligned to the longest of them or
        // `Errors`, a newline after the table
        isize max = 0;
        for (isize j = 0; j < file_count; j++) if (my files[j].errors > max) max = my files[j].errors;
        int max_digits = 1;
        for (isize n = max; n >= 10; n /= 10) max_digits++;
        int goal = max_digits > 6 ? max_digits : 6;
        int header_padding = max_digits - 6 > 0 ? max_digits - 6 : 0;
        cstr_append_d(out, DEFAULT_DISPLAY, repeated_d(" ", header_padding), "Errors  Files", newline);
        isize* order = malloc(sizeof(isize) * (size_t)file_count);
        panic_if(!order, "out of memory");
        for (isize j = 0; j < file_count; j++) {
            isize k = j;
            while (k > 0 && strcmp(cstr_str(&my files[order[k - 1]].path), cstr_str(&my files[j].path)) > 0) {
                order[k] = order[k - 1];
                k--;
            }
            order[k] = j;
        }
        for (isize o = 0; o < file_count; o++) {
            isize j = order[o];
            int digits = 1;
            for (isize n = my files[j].errors; n >= 10; n /= 10) digits++;
            int padding = digits < goal ? goal - digits : 0;
            cstr_append_d(out, DEFAULT_DISPLAY, repeated_d(" ", padding), my files[j].errors, "  ",
                my files[j].name, PRETTY_GREY ":", my files[j].line, PRETTY_RESET, newline);
        }
        free(order);
        cstr_append(out, newline);
    }
    cstr_drop(&first);
}

void error_format_pretty_summary(cstr* out, VecError const* errors, char const* newline, ErrorFileNamer file_name) {
    ErrorSummary summary = {0};
    error_summary_add(&summary, errors, file_name);
    error_summary_format(out, &summary, newline, true);
    error_summary_drop(&summary);
}

bool error_ctx_empty(ErrorCtx const* me) {
    return vec_error_is_empty(&my errors);
}

void error_ctx_clear(ErrorCtx* me) {
    my phase = ERROR_PHASE_UNKOWN;
    vec_error_clear(&my errors);
}

static int error_cmp(Error const* a, Error const* b) {
    // Errors without a file come first (error_ctx_global_error_s() orders them)
    if (!span_valid(a->span)) return -1;
    // TODO: sort based on sorce file
    int ret = span_cmp(a->span, b->span);
    if (ret) return ret;
    // (The parser's own order: the first of several at one position is the one that stays, see the dedupe)
    if (a->phase == ERROR_PHASE_PARSE && b->phase == ERROR_PHASE_PARSE) return 0;
    // tsc's compareDiagnostics (what the harness sorts by): code, then message
    if (a->ts_code != b->ts_code) return a->ts_code > b->ts_code ? 1 : -1;
    ret = cstr_cmp(&a->message, &b->message);
    return ret > 0 ? 1 : ret < 0 ? -1 : 0;
}

// The same message chain (tsc's compareDiagnosticsSkipRelatedInformation: related information doesn't count)
static bool same_message_chain(Error const* a, Error const* b) {
    if (a->ts_code != b->ts_code || !cstr_eq(&a->message, &b->message)) return false;
    Error const* a_next = nullptr;
    Error const* b_next = nullptr;
    for_each (i, vec_error, &a->sub_errors) if (!span_valid(i->span)) a_next = i;
    for_each (i, vec_error, &b->sub_errors) if (!span_valid(i->span)) b_next = i;
    if (!a_next || !b_next) return !a_next && !b_next;
    return same_message_chain(a_next, b_next);
}

static bool error_is_js_syntax(Error const* error) {
    return (error->ts_code >= 8002 && error->ts_code <= 8017) || error->ts_code == 8037;
}

static bool is_related_info(Error const* sub) {
    return sub->detached || span_valid(sub->span);
}

// tsgo's CompareDiagnostics of two related informations: by file, start, end, code, then message
static int related_info_cmp(void const* pa, void const* pb) {
    Error const* a = pa;
    Error const* b = pb;
    char const* a_path = span_valid(a->span) && span_source(a->span) ? cstr_str(&span_source(a->span)->canonical_path) : "";
    char const* b_path = span_valid(b->span) && span_source(b->span) ? cstr_str(&span_source(b->span)->canonical_path) : "";
    int ret = strcmp(a_path, b_path);
    if (ret) return ret;
    if (span_valid(a->span) && span_valid(b->span)) {
        if (span_start_ptr(a->span) != span_start_ptr(b->span)) return span_start_ptr(a->span) < span_start_ptr(b->span) ? -1 : 1;
        if (span_end_ptr(a->span) != span_end_ptr(b->span)) return span_end_ptr(a->span) < span_end_ptr(b->span) ? -1 : 1;
    }
    if (a->ts_code != b->ts_code) return a->ts_code < b->ts_code ? -1 : 1;
    return cstr_cmp(&a->message, &b->message);
}

// Whether two diagnostics have the same related information, in the same order
static bool same_related_info(Error const* a, Error const* b) {
    Error const* as = vec_error_begin(&a->sub_errors).ref;
    Error const* bs = vec_error_begin(&b->sub_errors).ref;
    isize_t ai = 0, bi = 0, an = vec_error_size(&a->sub_errors), bn = vec_error_size(&b->sub_errors);
    for (;;) {
        while (ai < an && !is_related_info(&as[ai])) ai++;
        while (bi < bn && !is_related_info(&bs[bi])) bi++;
        if (ai == an || bi == bn) return ai == an && bi == bn;
        if (related_info_cmp(&as[ai], &bs[bi]) || cstr_cmp(&as[ai].message, &bs[bi].message)) return false;
        ai++, bi++;
    }
}

// tsgo's compactAndMergeRelatedInfos: a diagnostic that differs from `into` only by its related information adds it,
// the related information then sorted and without duplicates. Takes `from`'s related information
static void merge_related_info(Error* into, Error* from) {
    isize_t into_count = 0, from_count = 0;
    for_each (i, vec_error, &into->sub_errors) into_count += is_related_info(i);
    for_each (i, vec_error, &from->sub_errors) from_count += is_related_info(i);
    if (!from_count) return;
    if (from_count == into_count && same_related_info(into, from)) return;
    VecError related = {0};
    VecError kept = {0};
    for_each (i, vec_error, &into->sub_errors) vec_error_push(is_related_info(i) ? &related : &kept, *i);
    VecError from_kept = {0};
    for_each (i, vec_error, &from->sub_errors) vec_error_push(is_related_info(i) ? &related : &from_kept, *i);
    from->sub_errors.size = 0; // (moved)
    vec_error_drop(&from->sub_errors);
    from->sub_errors = from_kept;
    into->sub_errors.size = 0;
    vec_error_drop(&into->sub_errors);
    qsort(related.data, (size_t)vec_error_size(&related), sizeof(Error), related_info_cmp);
    for (isize_t i = 0; i < vec_error_size(&related); i++) {
        Error* r = vec_error_at_mut(&related, i);
        if (vec_error_size(&kept) && is_related_info(vec_error_back(&kept)) && !related_info_cmp(vec_error_back(&kept), r)) {
            error_drop(r);
        } else {
            vec_error_push(&kept, *r);
        }
    }
    related.size = 0;
    vec_error_drop(&related);
    into->sub_errors = kept;
}

static intptr_t insertion_index_in(VecError const* errors, Error const* error);
static intptr_t insertion_index_for_span(ErrorCtx* me, Error const* error) {
    return insertion_index_in(&my errors, error);
}

static intptr_t insertion_index_in(VecError const* errors, Error const* error) {
    for (intptr_t i = vec_error_size(errors); i > 0; i--) {
        int cmp = error_cmp(vec_error_at(errors, i - 1), error);
        if (cmp < 0) {
            return i;
        } else if (cmp == 0) {
            if (error->phase == ERROR_PHASE_TYPECHECK || error->phase == ERROR_PHASE_RESOLVE_NAMES) {
                // tsc's diagnostic collection drops an identical diagnostic (the binder's too: a third default export
                // reports the first one again, related to itself, merged by sortAndDeduplicateDiagnostics)
                for (intptr_t j = i; j > 0 && !error_cmp(vec_error_at(errors, j - 1), error); j--) {
                    Error const* other = vec_error_at(errors, j - 1);
                    // (One with other related information is another diagnostic until they're merged:
                    // error_vec_merge_related())
                    if (span_eq(other->span, error->span) && same_message_chain(other, error) && same_related_info(other, error)) {
                        return -1;
                    }
                }
                // Allow multiple typechecking errors for the same span
                return i - 1;
            } else if (error->phase == ERROR_PHASE_PARSE && vec_error_at(errors, i - 1)->ts_code != error->ts_code &&
                (error_is_js_syntax(error) != error_is_js_syntax(vec_error_at(errors, i - 1)) ||
                    error->grammar || vec_error_at(errors, i - 1)->grammar ||
                    error->scanner || vec_error_at(errors, i - 1)->scanner)
            ) {
                // A JavaScript file's TypeScript-only syntax error (tsc's getJSSyntacticDiagnosticsForFile), a
                // grammar error (the checker's) or a scanner error the parser's dedupe already saw (see
                // last_error_starts_at() in parser.c) is a diagnostic of its own next to another there: by code
                Error const* other = vec_error_at(errors, i - 1);
                return other->ts_code < error->ts_code ? i : i - 1;
            } else if (error->phase == ERROR_PHASE_PARSE && cstr_cmp(&vec_error_at(errors, i - 1)->message, &error->message) != 0) {
                // Two parse errors the parser kept at one span with other messages (`':' expected.` and `'</'
                // expected.` at the end of the file): tsc's sortAndDeduplicateDiagnostics only drops identical ones,
                // by code, then message
                Error const* other = vec_error_at(errors, i - 1);
                bool after = other->ts_code != error->ts_code ? other->ts_code < error->ts_code :
                    cstr_cmp(&other->message, &error->message) < 0;
                return after ? i : i - 1;
            } else {
                // Do not allow multiple errors for the same span of other types
                return -1;
            }
        }
    }
    return 0;
}

// The lines of the message chain directly under e (tsc's `next` array) are its sub errors without a location, the rest
// is related information
static bool error_is_chain(Error const* e) {
    return !span_valid(e->span) && !e->detached;
}

static isize chain_count(Error const* e) {
    isize n = 0;
    for_each (i, vec_error, &e->sub_errors) n += error_is_chain(i);
    return n;
}

static Error const* chain_at(Error const* e, isize index) {
    for_each (i, vec_error, &e->sub_errors) {
        if (error_is_chain(i) && index-- == 0) return i;
    }
    return nullptr;
}

// tsc's compareMessageChainSize, of the chains under a and b: more elaboration is less
static int chain_size_cmp(Error const* a, Error const* b) {
    isize na = chain_count(a), nb = chain_count(b);
    if (!na && !nb) return 0;
    if (!na) return 1;
    if (!nb) return -1;
    if (na != nb) return na > nb ? -1 : 1;
    for (isize i = 0; i < na; i++) {
        int ret = chain_size_cmp(chain_at(a, i), chain_at(b, i));
        if (ret) return ret;
    }
    return 0;
}

// tsc's compareMessageChainContent (the shapes are the same)
static int chain_content_cmp(Error const* a, Error const* b) {
    isize n = chain_count(a);
    for (isize i = 0; i < n; i++) {
        Error const* ai = chain_at(a, i);
        Error const* bi = chain_at(b, i);
        int ret = cstr_cmp(&ai->message, &bi->message);
        if (ret) return ret > 0 ? 1 : -1;
        ret = chain_content_cmp(ai, bi);
        if (ret) return ret;
    }
    return 0;
}

// Where a diagnostic without a file sorts among the others (tsgo's CompareDiagnostics by Loc, then code): the program's
// (option and file processing diagnostics, NewCompilerDiagnostic: Loc -1) before the checker's (c.error(nil, ...),
// a missing global type: Loc 0)
static int global_rank(Error const* error) {
    return error->phase == ERROR_PHASE_TYPECHECK;
}

static int error_full_cmp(Error const* a, Error const* b);
static Error* global_insert(ErrorCtx* me, Error error);

// tsc's compareRelatedInformation: more of it is less
static int related_cmp(Error const* a, Error const* b) {
    isize na = vec_error_size(&a->sub_errors) - chain_count(a);
    isize nb = vec_error_size(&b->sub_errors) - chain_count(b);
    if (!na && !nb) return 0;
    if (!na || !nb) return na ? -1 : 1;
    if (na != nb) return na > nb ? -1 : 1;
    Error const* ai = vec_error_begin(&a->sub_errors).ref;
    Error const* bi = vec_error_begin(&b->sub_errors).ref;
    for (isize i = 0; i < na; i++, ai++, bi++) {
        while (error_is_chain(ai)) ai++;
        while (error_is_chain(bi)) bi++;
        int ret = error_full_cmp(ai, bi);
        if (ret) return ret;
    }
    return 0;
}

// tsc's compareDiagnostics (the order its harness prints diagnostics in): error_cmp(), then the message chain, then the
// related information
static int error_full_cmp(Error const* a, Error const* b) {
    if (span_valid(a->span) != span_valid(b->span)) return span_valid(a->span) ? 1 : -1;
    if (!span_valid(a->span) && global_rank(a) != global_rank(b)) return global_rank(a) - global_rank(b);
    int ret = span_cmp(a->span, b->span);
    if (ret) return ret;
    if (a->ts_code != b->ts_code) return a->ts_code > b->ts_code ? 1 : -1;
    ret = cstr_cmp(&a->message, &b->message);
    if (ret) return ret > 0 ? 1 : -1;
    ret = chain_size_cmp(a, b);
    if (ret) return ret;
    ret = chain_content_cmp(a, b);
    if (ret) return ret;
    return related_cmp(a, b);
}

// Same location, code and head message
static bool error_same_head(Error const* a, Error const* b) {
    return span_valid(a->span) == span_valid(b->span) && !span_cmp(a->span, b->span) && a->ts_code == b->ts_code &&
        cstr_eq(&a->message, &b->message);
}

void error_ctx_merge(ErrorCtx* me, ErrorCtx* from) {
    bool located = false;
    for_each (i, vec_error, &from->errors) {
        if (span_valid(i->span)) {
            located = true;
            continue;
        }
        // (Several checkers may each report the same global diagnostic)
        bool seen = false;
        for_each (j, vec_error, &my errors) {
            if (span_valid(j->span)) break;
            if (error_same_head(i, j)) seen = true;
        }
        if (seen) {
            error_drop(i);
        } else {
            global_insert(me, (Error){ .message = i->message, .ts_code = i->ts_code, .phase = i->phase,
                .sub_errors = i->sub_errors });
        }
    }
    if (located && my frozen) log_error("Merged into a frozen ErrorCtx");
    if (located) {
        // One pass rather than an insertion each (quadratic: inbox-zero's 21k errors from 4 checkers took 380 ms).
        // Before each error goes what of ours is at or before its span: the rest is after it in error_cmp() too, so
        // the insertion's scan from the end (insertion_index_in()) sees exactly what it would have in all of ours.
        // What of ours is at or before the first one's span stays where it is (a file's errors join at the end)
        Span first = span_null;
        for_each (i, vec_error, &from->errors) {
            if (span_valid(i->span)) {
                first = i->span;
                break;
            }
        }
        isize kept = vec_error_size(&my errors);
        while (kept > 0 && span_valid(my errors.data[kept - 1].span) && span_cmp(my errors.data[kept - 1].span, first) > 0) kept--;
        isize ours = vec_error_size(&my errors) - kept;
        // (error_ctx_append_error() keeps them in span order)
        for (isize i = kept + 1; i < kept + ours; i++) check(span_cmp(my errors.data[i - 1].span, my errors.data[i].span) <= 0);
        Error* tail = ours ? c_new_n(Error, ours) : nullptr;
        if (ours) memcpy(tail, my errors.data + kept, sizeof(Error) * (size_t)ours);
        my errors.size = kept;
        vec_error_reserve(&my errors, kept + ours + vec_error_size(&from->errors));
        isize next = 0;
        for_each (i, vec_error, &from->errors) {
            if (!span_valid(i->span)) continue;
            for (; next < ours && span_cmp(tail[next].span, i->span) <= 0; next++) vec_error_push(&my errors, tail[next]);
            intptr_t at = insertion_index_in(&my errors, i);
            if (at >= 0) vec_error_insert_n(&my errors, at, i, 1);
            else error_drop(i);
        }
        for (; next < ours; next++) vec_error_push(&my errors, tail[next]);
        free(tail);
    }
    from->errors.size = 0;
}

void error_ctx_keep_first_once(ErrorCtx* ctxs, isize count) {
    for (isize c = 0; c < count; c++) {
        for_each (i, vec_error, &ctxs[c].errors) {
            if (!i->once_key) continue;
            for (isize d = 0; d < count; d++) {
                for_each (j, vec_error, &ctxs[d].errors) {
                    if (j->once_key == i->once_key && j != i && j->once_order < i->once_order) i->once_key = UINT64_MAX;
                }
            }
        }
    }
    // (Marked first, dropped after: the comparison above needs every candidate)
    for (isize c = 0; c < count; c++) {
        VecError* errors = &ctxs[c].errors;
        isize kept = 0;
        for (isize i = 0; i < vec_error_size(errors); i++) {
            if (errors->data[i].once_key == UINT64_MAX) error_drop(&errors->data[i]);
            else errors->data[kept++] = errors->data[i];
        }
        errors->size = kept;
    }
}

void error_ctx_sort_and_dedupe(ErrorCtx* me) {
    isize n = vec_error_size(&my errors);
    Error* data = my errors.data;
    // The errors are sorted by error_cmp() already: order each run of equal ones by the rest of compareDiagnostics
    for (isize start = 0; start < n;) {
        isize end = start + 1;
        while (end < n && span_valid(data[start].span) && !error_cmp(&data[start], &data[end]) &&
            !(data[start].phase == ERROR_PHASE_PARSE && data[end].phase == ERROR_PHASE_PARSE)
        ) {
            end++;
        }
        for (isize i = start + 1; i < end; i++) {
            Error moved = data[i];
            isize j = i;
            for (; j > start && error_full_cmp(&data[j - 1], &moved) > 0; j--) data[j] = data[j - 1];
            data[j] = moved;
        }
        start = end;
    }
    // tsgo's compactAndMergeRelatedInfos: of the diagnostics with the same location, code and whole message chain only
    // the first stays (with every one's related information). Other chains stay: TS2320 once per property that differs
    isize kept = 0;
    for (isize i = 0; i < n; i++) {
        if (kept && data[i].phase != ERROR_PHASE_PARSE && error_same_head(&data[kept - 1], &data[i]) &&
            same_message_chain(&data[kept - 1], &data[i])) {
            // (tsgo's compactAndMergeRelatedInfos: the same message chain with other related information is one
            // diagnostic with all of it)
            if (same_message_chain(&data[kept - 1], &data[i])) merge_related_info(&data[kept - 1], &data[i]);
            error_drop(&data[i]);
        } else {
            data[kept++] = data[i];
        }
    }
    my errors.size = kept;
}

// One diagnostic of error_vec_sort_and_dedupe(): its file's path to sort by, its index for a stable order
typedef struct SortedError {
    Error error;
    cstr path;
    isize index;
} SortedError;

static int sorted_error_cmp(void const* a_, void const* b_) {
    SortedError const* a = a_;
    SortedError const* b = b_;
    int ret = cstr_cmp(&a->path, &b->path);
    if (!ret) ret = error_full_cmp(&a->error, &b->error);
    if (!ret) ret = a->index < b->index ? -1 : a->index > b->index;
    return ret;
}

void error_vec_sort_and_dedupe(VecError* errors, cstr (*path)(SourceFile const* source)) {
    isize n = vec_error_size(errors);
    if (n < 2) return;
    SortedError* sorted = c_new_n(SortedError, n);
    for (isize i = 0; i < n; i++) {
        Error* e = &errors->data[i];
        sorted[i] = (SortedError){ *e, span_valid(e->span) && span_source(e->span) ? path(span_source(e->span)) : cstr_null, i };
    }
    qsort(sorted, (size_t)n, sizeof(SortedError), sorted_error_cmp);
    isize kept = 0;
    for (isize i = 0; i < n; i++) {
        if (kept && cstr_eq(&sorted[i].path, &sorted[i - 1].path) && error_same_head(&errors->data[kept - 1], &sorted[i].error) &&
            same_message_chain(&errors->data[kept - 1], &sorted[i].error)) {
            merge_related_info(&errors->data[kept - 1], &sorted[i].error);
            error_drop(&sorted[i].error);
        } else {
            errors->data[kept++] = sorted[i].error;
        }
    }
    for (isize i = 0; i < n; i++) cstr_drop(&sorted[i].path);
    c_free_n(sorted, n);
    errors->size = kept;
}

Error* error_ctx_append_error(ErrorCtx* me, Error error) {
    if (my frozen) log_error("Reported into a frozen ErrorCtx (a checker's diagnostic that bypasses its own): TS", error.ts_code, ": ", error.message);
    intptr_t insertion_index = insertion_index_for_span(me, &error);
    if (insertion_index >= 0) {
        return vec_error_insert_n(&my errors, insertion_index, &error, 1).ref;
    } else {
        error_drop(&error);
        return nullptr;
    }
}

static Error* global_insert(ErrorCtx* me, Error error) {
    // (by rank, then code, then message)
    intptr_t index = 0;
    int rank = global_rank(&error);
    while (index < vec_error_size(&my errors)) {
        Error const* other = vec_error_at(&my errors, index);
        int other_rank = global_rank(other);
        if (span_valid(other->span) || other_rank > rank || (other_rank == rank && (other->ts_code > error.ts_code ||
            (other->ts_code == error.ts_code && cstr_cmp(&other->message, &error.message) > 0)))) break;
        index++;
    }
    return vec_error_insert_n(&my errors, index, &error, 1).ref;
}

thread_local _Atomic bool* error_history_flag;
thread_local struct SourceFile const* error_checking_file;
thread_local bool error_cycle_foreign;
thread_local bool error_cycle_entry_dependent;

static inline void note_history_dependent(unsigned ts_code) {
    if (__builtin_expect(error_history_flag != nullptr, 0) && error_is_history_dependent(ts_code) &&
        !atomic_load_explicit(error_history_flag, memory_order_relaxed)
    ) {
        atomic_store(error_history_flag, true);
    }
}

Error* error_ctx_global_error_s(ErrorCtx* me, unsigned ts_code, cstr message) {
    if (!me) {
        cstr_drop(&message);
        return nullptr;
    }
    note_history_dependent(ts_code);
    if (my frozen) log_error("Reported into a frozen ErrorCtx (a checker's diagnostic that bypasses its own): TS", ts_code, ": ", message);
    return global_insert(me, (Error){ .message = message, .ts_code = ts_code, .phase = my phase });
}

Error* error_ctx_find(ErrorCtx* me, Span span, unsigned ts_code) {
    for (isize_t i = vec_error_size(&my errors); me && i > 0; i--) {
        Error* error = vec_error_at_mut(&my errors, i - 1);
        if (error->ts_code == ts_code && span_eq(error->span, span)) return error;
    }
    return me && my parent ? error_ctx_find(my parent, span, ts_code) : nullptr;
}

// tsc's plainJSErrors: the binder and grammar errors a plain JavaScript file reports (SourceFile.plain_js), sorted
static bool plain_js_reports(unsigned code) {
    static const unsigned codes[] = {
        1005, 1009, 1013, 1014, 1029, 1030, 1031, 1042, 1044, 1048, 1049, 1053, 1054, 1089, 1090, 1091, 1097, 1100,
        1101, 1102, 1104, 1105, 1106, 1107, 1111, 1113, 1114, 1115, 1116, 1123, 1155, 1156, 1162, 1171, 1172, 1174,
        1182, 1184, 1186, 1188, 1189, 1190, 1191, 1193, 1197, 1200, 1210, 1211, 1214, 1215, 1248, 1255, 1258, 1262,
        1308, 1312, 1325, 1341, 1344, 1358, 1359, 1368, 1450, 1451, 1473, 1474, 2451, 2462, 2480, 2492, 2501, 2528,
        2566, 2633, 2752, 2753, 2803, 2839, 2852, 5076, 17000, 17001, 17012, 18006, 18007, 18012, 18013, 18016, 18036,
        18038, 18041, 18061, 18111, 18112, 18113, 18114, 18115, 18117,
    };
    u32 lo = 0, hi = c_arraylen(codes);
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;
        if (codes[mid] == code) return true;
        if (codes[mid] < code) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

void error_ctx_remove_unchecked(ErrorCtx* me) {
    intptr_t kept = 0;
    for (intptr_t i = 0; i < vec_error_size(&my errors); i++) {
        Error* error = vec_error_at_mut(&my errors, i);
        SourceFile const* source = span_source(error->span);
        // (The parser's grammar errors are tsc's checker's: an unchecked file has none, like skipLibCheck's .d.ts)
        if (source && (source->excluded ||
            (error->unchecked_js && !source->unchecked) ||
            (source->unchecked &&
            (error->phase >= ERROR_PHASE_RESOLVE_NAMES || error->grammar) &&
            (error->jsdoc || !(source->plain_js && (error->include_processor || plain_js_reports(error->ts_code))))))
        ) {
            error_drop(error);
        } else {
            if (kept != i) my errors.data[kept] = *error;
            kept++;
        }
    }
    my errors.size = kept;
}

Error* error_ctx_grammar_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message) {
    if (span_source(span) && span_source(span)->has_parse_errors) {
        cstr_drop(&message);
        return nullptr;
    }
    return error_ctx_error_s(me, span, ts_code, message);
}

Error* error_ctx_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message) {
    // A silent resolution (no context, and so no span either) reports nothing
    if (!me) {
        cstr_drop(&message);
        return nullptr;
    }
    note_history_dependent(ts_code);
    // An unchecked lib file (skipDefaultLibCheck, skipLibCheck: the suite's harness sets the former) reports nothing,
    // though names in it still resolve lazily (with a partial `@lib` some of them don't exist). A checked one reports
    // like any file, also what checking another file finds in it (the lib side of a conflict with a user declaration)
    if (span_valid(span) && span_source(span) && span_source(span)->is_stdlib && span_source(span)->unchecked) {
        log_debug("Error in a lib file dropped: TS", ts_code, ": ", message);
        cstr_drop(&message);
        return nullptr;
    }
    if (!span_valid(span) || !span_source(span)) {
        log_error("Error added with invalid span: TS", ts_code, ": ", message);
        cstr_drop(&message);
        return nullptr;
    }
    check(span_valid(span) && span_source(span));
    return error_ctx_append_error(me, (Error){
        .message = message,
        .span = span,
        .ts_code = ts_code,
        .phase = my phase,
        .history_foreign = error_checking_file && error_is_history_dependent(ts_code) &&
            (span_source(span) != error_checking_file || (error_cycle_foreign && error_is_circularity(ts_code))),
        .history_entry_dependent = error_cycle_entry_dependent && error_is_circularity(ts_code),
    });
}

char const* error_category_name(Error const* error) {
    if (error->suggestion) return "suggestion";
    return error->ts_code == 1450 ? "message" : "error";
}
