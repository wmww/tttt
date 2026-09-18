#include "display.h"
#include "log.h"

DisplayCtx fmt_display(DisplayCtx d, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    cstr_vfmt(&d.target, cstr_size(&d.target), fmt, args);
    va_end(args);
    return d;
}

DisplayCtx string_debug_display(csview sv, DisplayCtx d) {
    display_ctx_reserve(&d, sv.size + 2);
    d = lit_display("\"", d);
    for (i64 i = 0; i < sv.size; i++) {
        switch (sv.buf[i]) {
            case '"': d = lit_display("\\\"", d); break;
            case '\\': d = lit_display("\\\\", d); break;
            case '\n': d = lit_display("\\n", d); break;
            case '\t': d = lit_display("\\t", d); break;
            case '\r': d = lit_display("\\r", d); break;
            case '\x1B': d = lit_display("\\x1B", d); break;
            default: d = sv_display(c_sv(&sv.buf[i], 1), d); break;
        }
    }
    d = lit_display("\"", d);
    return d;
}

void default_log_fatal_abort(csview file, i64 line, csview label, DisplayCtx d) {
    logger_config.func(&logger_config, file, line, LOG_LEVEL_FATAL, label, d);
    abort();
}

void default_log_fatal_abort_sv(csview file, i64 line, csview label, csview message) {
    default_log_fatal_abort(file, line, label, sv_display(message, logger_config.display_ctx));
}
