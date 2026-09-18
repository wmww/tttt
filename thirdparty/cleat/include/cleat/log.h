#pragma once

#include "display.h"

typedef enum LogLevel {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL,
    LOG_LEVEL_NEVER, // Used only as a min log level to disable logging, do not log with this level
} LogLevel;

typedef struct LoggerConfig LoggerConfig;
typedef void (*LoggerFunc)(
    LoggerConfig const* config,
    csview file,
    i64 line,
    LogLevel level,
    // can be used to override how level is displayed, usually csview_null
    csview label,
    DisplayCtx d
);

struct LoggerConfig {
    LogLevel threshold;
    // If INCLUDE_VALGRIND is set *and* we're running under Valgrind, logs >= this level will print a Valgrind stack
    // trace to stdout
    LogLevel valgrind_threshold;
    // .target should always be cstr_null
    DisplayCtx display_ctx;
    bool relative_timestamps : 1;
    bool absolute_timestamps : 1;
    bool file_line : 1;
    bool source_code_links : 1;
    FILE* output_file;
    LoggerFunc func;
};

// Threadsafety: this struct must not be modified while logging from another thread. The intended pattern is to mutate
// this struct (if needed) at the start of the program, and leave it alone thereafter. If you need to change logging
// config on the fly in a multithreaded program, dispatch through a custom logger that handles locking.
extern LoggerConfig logger_config;

#define log_display_ctx(config, level, display_ctx) do { \
    if (level >= (config)->threshold) \
        (config)->func( \
            (config), \
            c_sv(__FILE__), \
            __LINE__, \
            level, \
            csview_null, \
            display_ctx \
        ); \
} while (0)
#define log_generic(config, level, ...) \
    log_display_ctx(config, level, display_sequence((config)->display_ctx __VA_OPT__(, __VA_ARGS__)))
#define log_debug(...) log_generic(&logger_config, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define log_info(...)  log_generic(&logger_config, LOG_LEVEL_INFO,  __VA_ARGS__)
#define log_warn(...)  log_generic(&logger_config, LOG_LEVEL_WARN,  __VA_ARGS__)
#define log_error(...) log_generic(&logger_config, LOG_LEVEL_ERROR, __VA_ARGS__)

csview debug_clean_up_path(csview file);
void default_logger(
    LoggerConfig const* config,
    csview file,
    i64 line,
    LogLevel level,
    csview label,
    DisplayCtx message_d
);
