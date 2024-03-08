#include "error.h"

void Error_drop(Error* self) {
    cstr_drop(&self->message);
}

Error Error_clone(Error self) {
    self.message = cstr_clone(self.message);
    return self;
}

void cstr_append_error_debug(cstr* target, Error const* self) {
    const char* file_start = self->span.file_start;
    const char* line_start = file_start;
    const char* line_end = nullptr;
    unsigned line = 1;
    unsigned col = 1;

    if (file_start) {
        for (unsigned i = 0; i < self->span.offset; i++) {
            if (file_start[i] == '\n') {
                line++;
                line_start = file_start + i + 1;
            }
        }
        line_end = file_start + self->span.offset + self->span.length;
        while (*line_end && *line_end != '\n') {
            line_end++;
        }
        col = file_start + self->span.offset - line_start + 1;
    }

    cstr_append_fmt(
        target,
        "%d:%d - " CLR_ERR "error TS%d:" CLR0 " %s",
        line, col, self->ts_code,
        cstr_str(&self->message)
    );

    if (file_start) {
        cstr_append_fmt(
            target,
            "\n\n" CLR_STR "%.*s" CLR0 "\n",
            (int)(line_end - line_start), line_start
        );

        for (unsigned i = 1; i < col; i++) {
            cstr_append_sv(target, c_sv(" "));
        }

        cstr_append_sv(target, c_sv(CLR_ERR));
        for (unsigned i = 0; i < (self->span.length ? self->span.length : 1); i++) {
            cstr_append_sv(target, c_sv("~"));
        }
        cstr_append_sv(target, c_sv(CLR0));
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
        .ts_code = 0,
    });
}

void ErrorCtx_type_error(ErrorCtx* self, const char* static_message) {
    if (!self) {
        return;
    }
    vec_Error_push(&self->errors, (Error){
        .message = cstr_from(static_message),
        .span = {0},
        .ts_code = 0,
    });
}

void ErrorCtx_add_error_s(ErrorCtx* self, Span span, unsigned ts_code, cstr message) {
    vec_Error_push(&self->errors, (Error){
        .message = message,
        .span = span,
        .ts_code = ts_code,
    });
}

void ErrorCtx_error(ErrorCtx* self, Span span, unsigned ts_code, const char* fmt, ...) {
    cstr message = cstr_null;
    va_list args;
    va_start(args, fmt);
    cstr_vfmt(&message, 0, fmt, args);
    va_end(args);
    ErrorCtx_add_error_s(self, span, ts_code, message);
}
