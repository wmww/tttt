#include "error.h"
#include "string_helpers.h"
#include "source_file.h"

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
    if (span_valid(my span) && my span.source) {
        display_color(d, color_path);
        display_str(d, cstr_str(&my span.source->canonical_path));
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
        display_repeated(d, c_sv("-"), my span.sv.size ? my span.sv.size : 1);
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
    if (!span_valid(span) || !span.source) {
        cstr_drop(&message);
        return;
    }
    error_sub_error_s(me, span, ts_code, message);
}

void error_ctx_drop(ErrorCtx* me) {
    vec_error_drop(&my errors);
}

intptr_t print_error_ctx_max_display = 5000;

void print_error_ctx(ErrorCtx const* me) {
    const intptr_t max_display = print_error_ctx_max_display;
    intptr_t displayed = 0;
    for_each (i, vec_error, &my errors) {
        println(debug_d(i), "\n");
        displayed++;
        if (displayed >= max_display) {
            break;
        }
    }
    if (vec_error_size(&my errors) > displayed) {
        println(
            color_error, "  ", vec_error_size(&my errors) - displayed,
            " additional errors not shown", color_normal, "\n"
        );
    }
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
    if (ret) {
        return ret;
    } else if (a->phase != b->phase) {
        return a->phase > b->phase ? 1 : -1;
    } else {
        return 0;
    }
}

static intptr_t insertion_index_for_span(ErrorCtx* me, Error const* error) {
    for (intptr_t i = vec_error_size(&my errors); i > 0; i--) {
        int cmp = error_cmp(vec_error_at(&my errors, i - 1), error);
        if (cmp < 0) {
            return i;
        } else if (cmp == 0) {
            if (error->phase == ERROR_PHASE_TYPECHECK) {
                // Allow multiple typechecking errors for the same span
                return i - 1;
            } else {
                // Do not allow multiple errors for the same span of other types
                return -1;
            }
        }
    }
    return 0;
}

Error* error_ctx_append_error(ErrorCtx* me, Error error) {
    intptr_t insertion_index = insertion_index_for_span(me, &error);
    if (insertion_index >= 0) {
        return vec_error_insert_n(&my errors, insertion_index, &error, 1).ref;
    } else {
        error_drop(&error);
        return nullptr;
    }
}

Error* error_ctx_global_error_s(ErrorCtx* me, unsigned ts_code, cstr message) {
    if (!me) {
        cstr_drop(&message);
        return nullptr;
    }
    // tsc sorts diagnostics without a file by code, then by message
    intptr_t index = 0;
    while (index < vec_error_size(&my errors)) {
        Error const* other = vec_error_at(&my errors, index);
        if (span_valid(other->span) || other->ts_code > ts_code ||
            (other->ts_code == ts_code && cstr_cmp(&other->message, &message) > 0)) break;
        index++;
    }
    Error error = { .message = message, .ts_code = ts_code, .phase = my phase };
    return vec_error_insert_n(&my errors, index, &error, 1).ref;
}

Error* error_ctx_find(ErrorCtx* me, Span span, unsigned ts_code) {
    for (isize_t i = vec_error_size(&my errors); me && i > 0; i--) {
        Error* error = vec_error_at_mut(&my errors, i - 1);
        if (error->ts_code == ts_code && span_eq(error->span, span)) return error;
    }
    return nullptr;
}

void error_ctx_remove_unchecked(ErrorCtx* me) {
    intptr_t kept = 0;
    for (intptr_t i = 0; i < vec_error_size(&my errors); i++) {
        Error* error = vec_error_at_mut(&my errors, i);
        SourceFile const* source = error->span.source;
        if (source && source->unchecked && error->phase >= ERROR_PHASE_RESOLVE_NAMES) {
            error_drop(error);
        } else {
            if (kept != i) my errors.data[kept] = *error;
            kept++;
        }
    }
    my errors.size = kept;
}

Error* error_ctx_grammar_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message) {
    if (span.source && span.source->has_parse_errors) {
        cstr_drop(&message);
        return nullptr;
    }
    return error_ctx_error_s(me, span, ts_code, message);
}

Error* error_ctx_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message) {
    if (!span_valid(span) || !span.source || span.source->is_stdlib) {
        log_error("Error added with invalid span: TS", ts_code, ": ", message);
        cstr_drop(&message);
        return nullptr;
    }
    check(span_valid(span) && span.source);
    return error_ctx_append_error(me, (Error){
        .message = message,
        .span = span,
        .ts_code = ts_code,
        .phase = my phase,
    });
}

