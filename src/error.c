#include "error.h"
#include "string_helpers.h"

void error_drop(Error* me) {
    cstr_drop(&my message);
}

Error error_clone(Error me) {
    me.message = cstr_clone(me.message);
    return me;
}

void cstr_append_error_debug(cstr* target, Error const* me) {
    int line = span_line_index(&my span) + 1;

    cstr_append_fmt(
        target,
        "%d - " CLR_ERR "error TS%d:" CLR0 CLR_STR " %s" CLR0,
        line, my ts_code,
        cstr_str(&my message)
    );

    if (span_valid(&my span)) {
        const char* line_start = span_line_start(&my span);
        const char* line_end = span_line_end(&my span);
        int col = span_col_index(&my span);

        cstr_append_fmt(
            target,
            "\n\n%.*s\n",
            (int)(line_end - line_start), line_start
        );

        for (int i = 0; i < col; i++) {
            cstr_append_sv(target, c_sv(" "));
        }

        cstr_append_sv(target, c_sv(CLR_ERR));
        for (int i = 0; i < (my span.length ? (int)my span.length : 1); i++) {
            cstr_append_sv(target, c_sv("-"));
        }
        cstr_append_sv(target, c_sv(CLR0));
    }
}

void error_ctx_drop(ErrorCtx* me) {
    vec_error_drop(&my errors);
}

void error_ctx_display(ErrorCtx* me) {
    cstr str = cstr_null;
    const int max_display = 10;
    c_foreach_n(i, vec_error, my errors, max_display) {
        cstr_append_error_debug(&str, i.ref);
        printf("%s\n\n", cstr_str(&str));
        cstr_clear(&str);
    }
    if (vec_error_size(&my errors) > max_display) {
        printf(
            CLR_ERR "  %ld additional errors not shown\n\n" CLR0,
            vec_error_size(&my errors) - max_display
        );
    }
    cstr_drop(&str);
}

bool error_ctx_empty(ErrorCtx* me) {
    return vec_error_empty(&my errors);
}

void error_ctx_append_error(ErrorCtx* me, Error error) {
    intptr_t i = vec_error_size(&my errors);
    vec_error_insert_uninit(&my errors, i, 1);
    while (i > 0 && span_cmp(&vec_error_at(&my errors, i - 1)->span, &error.span) > 0) {
        *vec_error_at_mut(&my errors, i) = *vec_error_at(&my errors, i - 1);
        i--;
    }
    *vec_error_at_mut(&my errors, i) = error;
}

void error_ctx_append_consume(ErrorCtx* me, ErrorCtx other) {
    c_foreach(i, vec_error, other.errors) {
        error_ctx_append_error(me, *i.ref);
        *i.ref = (Error){0};
    }
    error_ctx_drop(&other);
}

void error_ctx_parse_error(ErrorCtx* me, const char* static_message) {
    if (!me) {
        return;
    }
    error_ctx_append_error(me, (Error){
        .message = cstr_from(static_message),
        .span = {0},
        .ts_code = TS_CODE_NONE,
    });
}

void error_ctx_type_error(ErrorCtx* me, const char* static_message) {
    if (!me) {
        return;
    }
    error_ctx_append_error(me, (Error){
        .message = cstr_from(static_message),
        .span = {0},
        .ts_code = TS_CODE_NONE,
    });
}

void error_ctx_error_s(ErrorCtx* me, Span span, unsigned ts_code, cstr message) {
    if (!me) {
        cstr_drop(&message);
        return;
    }
    error_ctx_append_error(me, (Error){
        .message = message,
        .span = span,
        .ts_code = ts_code,
    });
}

void error_ctx_error(ErrorCtx* me, Span span, unsigned ts_code, const char* fmt, ...) {
    if (!me) {
        return;
    }
    cstr message = cstr_null;
    va_list args;
    va_start(args, fmt);
    cstr_vfmt(&message, 0, fmt, args);
    va_end(args);
    error_ctx_error_s(me, span, ts_code, message);
}
