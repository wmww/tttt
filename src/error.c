#include "error.h"
#include "string_helpers.h"

void error_drop(Error* me) {
    cstr_drop(&my message);
    vec_error_drop(&my related_errors);
}

Error error_clone(Error me) {
    me.message = cstr_clone(me.message);
    return me;
}

void display_error(Display d, Error const* me) {
    int line = span_line_index(&my span) + 1;
    cstr prefix = cstr_null;

    if (my phase == ERROR_PHASE_RELATED) {
        Display prefix_d = d;
        prefix_d.target = &prefix;
        display_colored(prefix_d, color_implicit, " |  ");
    }

    display_s(d, prefix);
    display_color(d, color_path);
    display_lit(d, "filename.ts");
    display_color(d, color_line_number);
    display_fmt(d, ":%d", line);
    display_color(d, color_implicit);
    display_fmt(d, " [TS%d] ", my ts_code);
    display_color(d, color_error);
    display_s(d, my message);
    display_color(d, color_normal);

    if (span_valid(&my span)) {
        const char* line_start = span_line_start(&my span);
        const char* line_end = span_line_end(&my span);
        int col = span_col_index(&my span);

        display_fmt(
            d,
            "\n%s\n%s%.*s%s\n%s",
            cstr_str(&prefix),
            cstr_str(&prefix),
            (int)(line_end - line_start),
            line_start,
            cstr_str(&prefix),
            cstr_str(&prefix)
        );

        for (int i = 0; i < col; i++) {
            display_lit(d, " ");
        }

        display_color(d, color_error);
        for (int i = 0; i < (my span.length ? (int)my span.length : 1); i++) {
            display_lit(d, "-");
        }
        display_color(d, color_normal);
    }

    c_foreach(i, vec_error, my related_errors) {
        display_lit(d, "\n");
        display_error(d, i.ref);
    }

    cstr_drop(&prefix);
}

void error_related_error_s(Error* me, Span span, unsigned ts_code, cstr message) {
    if (!me) {
        cstr_drop(&message);
        return;
    }
    vec_error_push(&my related_errors, (Error){
        .message = message,
        .span = span,
        .ts_code = ts_code,
        .phase = ERROR_PHASE_RELATED,
    });
}

void error_related_error(Error* me, Span span, unsigned ts_code, const char* fmt, ...) {
    if (!me) {
        return;
    }
    cstr message = cstr_null;
    va_list args;
    va_start(args, fmt);
    cstr_vfmt(&message, 0, fmt, args);
    va_end(args);
    error_related_error_s(me, span, ts_code, message);
}

void error_config_enable_strict(ErrorConfig* me) {
    my strict_bind_call_apply = true;
    my strict_function_types = true;
    my strict_null_checks = true;
    my strict_property_initialization = true;
    my use_unknown_in_catch_variables = true;
}

void error_ctx_drop(ErrorCtx* me) {
    vec_error_drop(&my errors);
}

void print_error_ctx(ErrorCtx const* me) {
    cstr str = cstr_null;
    const int max_display = 10;
    c_foreach_n(i, vec_error, my errors, max_display) {
        display_error(debug_display(&str), i.ref);
        printf("%s\n\n", cstr_str(&str));
        cstr_clear(&str);
    }
    if (vec_error_size(&my errors) > max_display) {
        printf(
            "%s  %ld additional errors not shown%s\n\n",
            color_error,
            vec_error_size(&my errors) - max_display,
            color_normal
        );
    }
    cstr_drop(&str);
}

bool error_ctx_empty(ErrorCtx const* me) {
    return vec_error_empty(&my errors);
}

static int error_cmp(Error const* a, Error const* b) {
    int ret = span_cmp(&a->span, &b->span);
    if (ret) {
        return ret;
    } else if (a->phase != b->phase) {
        return a->phase > b->phase ? 1 : -1;
    } else {
        return 0;
    }
}

static intptr_t insertion_index_for_span(ErrorCtx* me, Error const* error) {
    intptr_t i = vec_error_size(&my errors);
    while (i > 0) {
        int cmp = error_cmp(vec_error_at(&my errors, i - 1), error);
        if (cmp < 0) {
            return i;
        } else if (cmp == 0) {
            // If two errors are in the same place and the the same phase, discard all but the first
            return -1;
        }
        i--;
    }
    return 0;
}

Error* error_ctx_append_error(ErrorCtx* me, Error error) {
    if (error.phase == ERROR_PHASE_UNKOWN) {
        NONFATAL("Error added with unkown phase: %s", cstr_str(&error.message));
    }
    intptr_t insertion_index = insertion_index_for_span(me, &error);
    if (insertion_index >= 0) {
        return vec_error_insert_n(&my errors, insertion_index, &error, 1).ref;
    } else {
        error_drop(&error);
        return nullptr;
    }
}

Error* error_ctx_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message) {
    if (!me) {
        // Inefficient to create owned string if it's just going to be dropped, so warn
        NONFATAL("error_ctx_error_s() called with null ErrorCtx: %s", cstr_str(&message));
        cstr_drop(&message);
        return nullptr;
    }
    return error_ctx_append_error(me, (Error){
        .message = message,
        .span = span,
        .ts_code = ts_code,
        .phase = my phase,
    });
}

Error* error_ctx_error(ErrorCtx* me, Span span, unsigned ts_code, const char* fmt, ...) {
    if (!me) {
        return nullptr;
    }
    cstr message = cstr_null;
    va_list args;
    va_start(args, fmt);
    cstr_vfmt(&message, 0, fmt, args);
    va_end(args);
    return error_ctx_error_s(me, span, ts_code, message);
}
