#define _POSIX_C_SOURCE 199309L

#include "time_helpers.h"
#include "display_functions.h"
#include <time.h>
#include <errno.h>

static struct timespec time_base;

__attribute__((constructor))
static void timer_init() {
    clock_gettime(CLOCK_MONOTONIC, &time_base);
}

double time_since_start() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    i64 sec  = now.tv_sec  - time_base.tv_sec;
    i64 nsec = now.tv_nsec - time_base.tv_nsec;
    return sec + nsec / 1000000000.0;
}

i64 milliseconds_since_start() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    i64 sec  = now.tv_sec  - time_base.tv_sec;
    i64 nsec = now.tv_nsec - time_base.tv_nsec;
    return (int64_t)sec * 1000 + nsec / 1000000;
}

DisplayCtx time_tm_display(const char* format, struct tm const* time, DisplayCtx d) {
    isize start_size = cstr_size(&d.target);
    cstr_reserve(&d.target, start_size + 64);
    int iters = 0;
    write_to_string:
    size_t written_chars = strftime(
        cstr_data(&d.target) + start_size,
        cstr_capacity(&d.target) - start_size, // Maybe could be +1? play it safe
        format,
        time
    );
    if (written_chars) {
        // strftime excludes the null terminator in returned value
        _cstr_set_size(&d.target, start_size + written_chars);
        return d;
    } else if (iters++ >= 10) {
        return lit_display("[time format error]", d);
    } else {
        cstr_reserve(&d.target, cstr_capacity(&d.target) + 1024);
        goto write_to_string;
    }
}

struct tm get_current_time_tm_utc() {
    time_t now = time(nullptr);
    struct tm utc;
    if (!gmtime_r(&now, &utc)) utc = (struct tm){0};
    return utc;
}

void sleep_for(double seconds) {
    if (seconds <= 0.0) return;
    struct timespec rem, req = {
        .tv_sec  = (time_t)seconds,
        .tv_nsec = (long)((seconds - (time_t)seconds) * 1e9)
    };
    while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
}
