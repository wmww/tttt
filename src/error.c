#include "error.h"
#include "string_helpers.h"

void Error_drop(Error* self) {
    cstr_drop(&self->message);
}

Error Error_clone(Error self) {
    self.message = cstr_clone(self.message);
    return self;
}

void cstr_append_error_debug(cstr* target, Error const* self) {
    int line = span_line_index(&self->span) + 1;

    cstr_append_fmt(
        target,
        "%d - " CLR_ERR "error TS%d:" CLR0 CLR_STR " %s" CLR0,
        line, self->ts_code,
        cstr_str(&self->message)
    );

    if (self->span.file_start) {
        const char* line_start = span_line_start(&self->span);
        const char* line_end = span_line_end(&self->span);
        int col = span_col_index(&self->span);

        cstr_append_fmt(
            target,
            "\n\n%.*s\n",
            (int)(line_end - line_start), line_start
        );

        for (int i = 0; i < col; i++) {
            cstr_append_sv(target, c_sv(" "));
        }

        cstr_append_sv(target, c_sv(CLR_ERR));
        for (int i = 0; i < (self->span.length ? (int)self->span.length : 1); i++) {
            cstr_append_sv(target, c_sv("-"));
        }
        cstr_append_sv(target, c_sv(CLR0));
    }
}

void cstr_append_error_txt(cstr* target, Error const* self) {
    const char* line_start = span_line_start(&self->span);
    const char* line_end = span_line_end(&self->span);
    int line = span_line_index(&self->span);
    int col = span_col_index(&self->span);

    cstr_append_fmt(
        target,
        "%s(%d,%d): error TS%d: %s",
        "filename.ts", line, col,
        self->ts_code,
        cstr_str(&self->message)
    );

    if (self->span.file_start) {
        cstr_append_fmt(
            target,
            "\n%.*s\n",
            (int)(line_end - line_start), line_start
        );
        for (int i = 0; i < col; i++) {
            cstr_append_sv(target, c_sv(" "));
        }
        for (int i = 0; i < (self->span.length ? (int)self->span.length : 1); i++) {
            cstr_append_sv(target, c_sv("~"));
        }
    }
}

void ErrorCtx_drop(ErrorCtx* self) {
    vec_Error_drop(&self->errors);
}

void ErrorCtx_display(ErrorCtx* self) {
    cstr str = cstr_null;
    c_foreach(i, vec_Error, self->errors) {
        cstr_append_error_debug(&str, i.ref);
        printf("%s\n\n", cstr_str(&str));
        cstr_clear(&str);
    }
    cstr_drop(&str);
}

void ErrorCtx_append_errors_txt(ErrorCtx* self, cstr* target) {
    c_foreach(i, vec_Error, self->errors) {
        cstr_append_error_txt(target, i.ref);
    }
}

bool ErrorCtx_empty(ErrorCtx* self) {
    return vec_Error_empty(&self->errors);
}

void ErrorCtx_append_consume(ErrorCtx* self, ErrorCtx other) {
    c_foreach(i, vec_Error, other.errors) {
        vec_Error_push(&self->errors, *i.ref);
        *i.ref = (Error){0};
    }
    ErrorCtx_drop(&other);
}

void ErrorCtx_parse_error(ErrorCtx* self, const char* static_message) {
    if (!self) {
        return;
    }
    vec_Error_push(&self->errors, (Error){
        .message = cstr_from(static_message),
        .span = {0},
        .ts_code = TS_CODE_NONE,
    });
}

void ErrorCtx_type_error(ErrorCtx* self, const char* static_message) {
    if (!self) {
        return;
    }
    vec_Error_push(&self->errors, (Error){
        .message = cstr_from(static_message),
        .span = {0},
        .ts_code = TS_CODE_NONE,
    });
}

void ErrorCtx_error_s(ErrorCtx* self, Span span, unsigned ts_code, cstr message) {
    if (!self) {
        return;
    }
    vec_Error_push(&self->errors, (Error){
        .message = message,
        .span = span,
        .ts_code = ts_code,
    });
}

void ErrorCtx_error(ErrorCtx* self, Span span, unsigned ts_code, const char* fmt, ...) {
    if (!self) {
        return;
    }
    cstr message = cstr_null;
    va_list args;
    va_start(args, fmt);
    cstr_vfmt(&message, 0, fmt, args);
    va_end(args);
    ErrorCtx_error_s(self, span, ts_code, message);
}
