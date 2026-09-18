#include "log.h"
#include "string_helpers.h"
#include "time_helpers.h"
#include "uri.h"
#include "failable.h"

#include <time.h>

#ifdef INCLUDE_VALGRIND
#include <valgrind/valgrind.h>
#endif

LoggerConfig logger_config = {
    .threshold = LOG_LEVEL_INFO,
    .valgrind_threshold = LOG_LEVEL_WARN,
    .display_ctx = {
        .color = true,
        .multiline = true,
    },
    .relative_timestamps = false,
    .absolute_timestamps = false,
    .file_line = false,
    .source_code_links = true,
    .output_file = nullptr,
    .func = default_logger,
};

const csview log_level_colors[] = {
    [LOG_LEVEL_DEBUG] = c_sv_const_init("\x1b[0;90m"),
    [LOG_LEVEL_INFO]  = c_sv_const_init("\x1b[0;32m"),
    [LOG_LEVEL_WARN]  = c_sv_const_init("\x1b[1;33m"),
    [LOG_LEVEL_ERROR] = c_sv_const_init("\x1b[1;31m"),
    [LOG_LEVEL_FATAL] = c_sv_const_init("\x1b[1;31m"),
};

const csview log_level_strings[] = {
    [LOG_LEVEL_DEBUG] = c_sv_const_init("debug"),
    [LOG_LEVEL_INFO]  = c_sv_const_init("info"),
    [LOG_LEVEL_WARN]  = c_sv_const_init("warning"),
    [LOG_LEVEL_ERROR] = c_sv_const_init("error"),
    [LOG_LEVEL_FATAL] = c_sv_const_init("panic"),
};

static const csview time_color   = c_sv_const_init("\x1b[0;90m");
static const csview path_color   = c_sv_const_init("\x1b[0;34m");
static const csview normal_color = c_sv_const_init("\x1b[0m");

csview debug_clean_up_path(csview file) {
    file = csview_without_prefix(file, "../");
    file = csview_without_prefix(file, "src/");
    return file;
}

static inline csview color_for_level(LogLevel level) {
    if (level >= 0 && level < c_arraylen(log_level_colors)) {
        return log_level_colors[level];
    } else {
        return normal_color;
    }
}

static inline csview string_for_level(LogLevel level, csview label) {
    if (label.size) {
        return label;
    } else if (level >= 0 && level < c_arraylen(log_level_strings)) {
        return log_level_strings[level];
    } else {
        return c_sv("[invalid log level]");
    }
}

void default_logger(
    LoggerConfig const* config,
    csview file,
    i64 line,
    LogLevel level,
    csview label,
    DisplayCtx message_d
) {
    DisplayCtx prefix_d = config->display_ctx;
    bool source_code_link = config->source_code_links;
    if (source_code_link) {
        cstr link_uri = cstr_null;
        try (existing_file_uri_encode_append(file, line, &link_uri)) if_ok {
            prefix_d = sv_display(terminal_link_begin_uri, prefix_d);
            prefix_d = sv_display(cstr_sv(&link_uri), prefix_d);
            prefix_d = sv_display(terminal_link_uri_to_text, prefix_d);
        } if_err (er) {
            source_code_link = false;
        }
        cstr_drop(&link_uri);
    }
    if (config->absolute_timestamps) {
        struct tm utc = get_current_time_tm_utc();
        prefix_d = color_display(time_color, prefix_d);
        prefix_d = time_tm_display(ISO8601_UTC_FORMAT " ", &utc, prefix_d);
    }
    if (config->relative_timestamps) {
        prefix_d = color_display(time_color, prefix_d);
        cstr_append_fmt(&prefix_d.target, "%.3f ", time_since_start());
    }
    if (config->file_line) {
        prefix_d = color_display(path_color, prefix_d);
        prefix_d = sv_display(debug_clean_up_path(file), prefix_d);
        prefix_d = lit_display(":", prefix_d);
        prefix_d = i64_display(line, prefix_d);
        prefix_d = lit_display(": ", prefix_d);
    }
    prefix_d = color_display(color_for_level(level), prefix_d);
    prefix_d = sv_display(string_for_level(level, label), prefix_d);
    if (cstr_size(&prefix_d.target)) prefix_d = lit_display(": ", prefix_d);
    prefix_d = color_display(normal_color, prefix_d);
    if (source_code_link) prefix_d = sv_display(terminal_link_end_text, prefix_d);
    FILE* target = config->output_file ? config->output_file : stderr;
    fprintf(target, "%s%s\n", cstr_str(&prefix_d.target), cstr_str(&message_d.target));
    display_ctx_drop(&prefix_d);
    display_ctx_drop(&message_d);
#ifdef INCLUDE_VALGRIND
    if (level >= config->valgrind_threshold && level < LOG_LEVEL_NEVER) {
        VALGRIND_PRINTF_BACKTRACE("");
    }
#endif
}
