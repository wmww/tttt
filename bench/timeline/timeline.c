// A whole-process timeline for machines without perf: LD_PRELOAD it and every TIMELINE_US microseconds (default 500)
// of wall time a monitor thread reads every thread's /proc/self/task/T/schedstat (time on a CPU, time waiting for one)
// and sends each thread that ran since the last tick a signal whose handler records its backtrace. Phase marks come
// from timeline_phase(), which tttt's --time calls (main.c phase_done()) when this library is loaded. Written at exit to
// (TIMELINE_ALL=1: every thread, also blocked ones, each sample with its state)
// TIMELINE_OUT (default timeline.out) with /proc/self/maps; bench/timeline/timeline.py turns it into a per-phase table.
// Build: gcc -O2 -shared -fPIC -pthread -o /tmp/timeline.so bench/timeline/timeline.c
#define _GNU_SOURCE
#include <dirent.h>
#include <stdbool.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

enum { DEPTH = 96, MAX_SAMPLES = 400000, MAX_STATS = 2000000, MAX_PHASES = 64 };

typedef struct Sample { long long ns; int tid, depth; char state; void* at[DEPTH]; } Sample;
typedef struct Stat { long long ns, run, wait; int tid; } Stat;

static Sample* samples;
static Stat* stats;
static int sample_count, stat_count;
static struct { long long ns; char name[48]; } phases[MAX_PHASES];
static int phase_count;
static long long start_ns;
static volatile int stopping;
static pthread_t monitor;
static int monitor_tid;

static long long now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

void timeline_phase(char const* name) {
    if (phase_count >= MAX_PHASES) return;
    phases[phase_count].ns = now_ns();
    snprintf(phases[phase_count].name, sizeof phases[phase_count].name, "%s", name);
    phase_count++;
}

static void on_sample(int sig, siginfo_t* info, void* context) {
    (void)sig;
    (void)context;
    int i = __atomic_fetch_add(&sample_count, 1, __ATOMIC_RELAXED);
    if (i >= MAX_SAMPLES) return;
    samples[i].ns = now_ns();
    samples[i].tid = (int)syscall(SYS_gettid);
    samples[i].state = info->si_code == SI_QUEUE ? (char)info->si_value.sival_int : '?';
    samples[i].depth = backtrace(samples[i].at, DEPTH);
}

typedef struct Last { int tid; long long run; } Last;

static void* monitor_run(void* arg) {
    (void)arg;
    monitor_tid = (int)syscall(SYS_gettid);
    bool all = getenv("TIMELINE_ALL") && *getenv("TIMELINE_ALL") == '1';
    long us = getenv("TIMELINE_US") ? atol(getenv("TIMELINE_US")) : 500;
    int pid = getpid();
    static Last last[4096];
    int last_count = 0;
    char path[128], buf[256];
    while (!stopping) {
        struct timespec sleep = { 0, us * 1000 };
        nanosleep(&sleep, NULL);
        long long ns = now_ns();
        DIR* dir = opendir("/proc/self/task");
        if (!dir) continue;
        struct dirent* e;
        while ((e = readdir(dir))) {
            if (e->d_name[0] == '.') continue;
            int tid = atoi(e->d_name);
            if (tid == monitor_tid) continue;
            snprintf(path, sizeof path, "/proc/self/task/%d/schedstat", tid);
            int fd = open(path, O_RDONLY);
            if (fd < 0) continue;
            ssize_t n = read(fd, buf, sizeof buf - 1);
            close(fd);
            if (n <= 0) continue;
            buf[n] = 0;
            long long run = 0, wait = 0;
            sscanf(buf, "%lld %lld", &run, &wait);
            int k = 0;
            while (k < last_count && last[k].tid != tid) k++;
            if (k == last_count && last_count < 4096) last[last_count++] = (Last){ tid, -1 };
            bool ran = k < 4096 && run != last[k].run;
            if (k < 4096) last[k].run = run;
            int s = stat_count;
            if (s < MAX_STATS) { stats[s] = (Stat){ ns, run, wait, tid }; stat_count = s + 1; }
            // (Only a thread on a CPU or runnable now: one that ran and then blocked would be sampled in its wait)
            if (ran) {
                snprintf(path, sizeof path, "/proc/self/task/%d/stat", tid);
                fd = open(path, O_RDONLY);
                n = fd < 0 ? 0 : read(fd, buf, sizeof buf - 1);
                if (fd >= 0) close(fd);
                buf[n > 0 ? n : 0] = 0;
                char const* paren = strrchr(buf, ')');
                char state = paren && paren[1] == ' ' ? paren[2] : '?';
                if (state == 'R' || all) {
                    siginfo_t info = { .si_signo = SIGPROF, .si_code = SI_QUEUE, .si_pid = pid, .si_uid = getuid() };
                    info.si_value.sival_int = state;
                    syscall(SYS_rt_tgsigqueueinfo, pid, tid, SIGPROF, &info);
                }
            } else if (all) {
                // (A thread that didn't run: blocked or idle, TIMELINE_ALL=1 samples where)
                siginfo_t info = { .si_signo = SIGPROF, .si_code = SI_QUEUE, .si_pid = pid, .si_uid = getuid() };
                info.si_value.sival_int = 'S';
                syscall(SYS_rt_tgsigqueueinfo, pid, tid, SIGPROF, &info);
            }
        }
        closedir(dir);
    }
    return NULL;
}

__attribute__((constructor)) static void timeline_start(void) {
    void* warm[2];
    backtrace(warm, 2);
    start_ns = now_ns();
    samples = mmap(NULL, sizeof(Sample) * MAX_SAMPLES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    stats = mmap(NULL, sizeof(Stat) * MAX_STATS, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    struct sigaction action = { .sa_sigaction = on_sample, .sa_flags = SA_RESTART | SA_SIGINFO };
    sigaction(SIGPROF, &action, NULL);
    timeline_phase("start");
    pthread_create(&monitor, NULL, monitor_run, NULL);
}

__attribute__((destructor)) static void timeline_stop(void) {
    timeline_phase("exit");
    stopping = 1;
    pthread_join(monitor, NULL);
    char const* path = getenv("TIMELINE_OUT") ? getenv("TIMELINE_OUT") : "timeline.out";
    FILE* out = fopen(path, "w");
    if (!out) return;
    FILE* maps = fopen("/proc/self/maps", "r");
    char line[4096];
    while (maps && fgets(line, sizeof line, maps)) fprintf(out, "M %s", line);
    if (maps) fclose(maps);
    fprintf(out, "B %lld %d\n", start_ns, getpid());
    for (int i = 0; i < phase_count; i++) fprintf(out, "P %lld %s\n", phases[i].ns, phases[i].name);
    for (int i = 0; i < stat_count; i++) fprintf(out, "R %lld %d %lld %lld\n", stats[i].ns, stats[i].tid, stats[i].run, stats[i].wait);
    int n = sample_count < MAX_SAMPLES ? sample_count : MAX_SAMPLES;
    for (int i = 0; i < n; i++) {
        fprintf(out, "S %lld %d %c", samples[i].ns, samples[i].tid, samples[i].state);
        for (int d = 2; d < samples[i].depth; d++) fprintf(out, " %lx", (unsigned long)samples[i].at[d]);
        fprintf(out, "\n");
    }
    fclose(out);
}
