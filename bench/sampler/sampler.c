// A wall-clock sampler of the main thread for machines without perf (perf_event_paranoid): LD_PRELOAD it, every
// SAMPLER_US microseconds (default 100) of wall time the main thread's backtrace is recorded, written at exit to
// SAMPLER_OUT (default sampler.out) with /proc/self/maps. bench/sampler/report.py symbolizes and ranks it.
// Build: gcc -O2 -shared -fPIC -o /tmp/sampler.so bench/sampler/sampler.c
#define _GNU_SOURCE
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define DEPTH 48
#define MAX_SAMPLES 200000

static void* samples[MAX_SAMPLES][DEPTH];
static int depths[MAX_SAMPLES];
static volatile int count;
static timer_t timer;

static void on_sample(int sig) {
    (void)sig;
    int i = count;
    if (i >= MAX_SAMPLES) return;
    depths[i] = backtrace(samples[i], DEPTH);
    count = i + 1;
}

__attribute__((constructor)) static void sampler_start(void) {
    // (backtrace() loads libgcc on first use: not in a signal handler)
    void* warm[2];
    backtrace(warm, 2);
    struct sigaction action = { .sa_handler = on_sample, .sa_flags = SA_RESTART };
    sigaction(SIGPROF, &action, NULL);
    struct sigevent event = { .sigev_notify = SIGEV_THREAD_ID, .sigev_signo = SIGPROF };
    event._sigev_un._tid = (pid_t)syscall(SYS_gettid);
    if (timer_create(CLOCK_MONOTONIC, &event, &timer)) return;
    long us = getenv("SAMPLER_US") ? atol(getenv("SAMPLER_US")) : 100;
    struct itimerspec spec = { .it_interval = { 0, us * 1000 }, .it_value = { 0, us * 1000 } };
    timer_settime(timer, 0, &spec, NULL);
}

__attribute__((destructor)) static void sampler_stop(void) {
    struct itimerspec off = {0};
    timer_settime(timer, 0, &off, NULL);
    char const* path = getenv("SAMPLER_OUT") ? getenv("SAMPLER_OUT") : "sampler.out";
    FILE* out = fopen(path, "w");
    if (!out) return;
    FILE* maps = fopen("/proc/self/maps", "r");
    char line[4096];
    while (maps && fgets(line, sizeof line, maps)) fprintf(out, "M %s", line);
    if (maps) fclose(maps);
    for (int i = 0; i < count; i++) {
        fprintf(out, "S");
        // (Frame 0 and 1 are the handler and the signal trampoline)
        for (int d = 2; d < depths[i]; d++) fprintf(out, " %lx", (unsigned long)samples[i][d]);
        fprintf(out, "\n");
    }
    fclose(out);
}
