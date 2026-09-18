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
    display_color(d, color_path);
    display_str(d, my span.source ? cstr_str(&my span.source->canonical_path) : "[unkown file]");
    display_color(d, color_line_number);
    display_lit(d, ":");
    display_i64(d, line);
    display_color(d, color_implicit);
    display_lit(d, " [TS");
    display_i64(d, my ts_code);
    display_lit(d, "] ");
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

void error_ctx_drop(ErrorCtx* me) {
    vec_error_drop(&my errors);
}

void print_error_ctx(ErrorCtx const* me) {
    const intptr_t max_display = 5000;
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

