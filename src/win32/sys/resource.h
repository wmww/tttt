#pragma once
// Windows: the part of <sys/resource.h> tttt uses (src/win32/win32.c). RLIMIT_STACK reads the stack the executable
// reserves for each thread (the linker's --stack), which setrlimit() can't change
#include <sys/time.h>

typedef unsigned long long rlim_t;
#define RLIM_INFINITY (~(rlim_t)0)
#define RLIMIT_STACK 3
#define RLIMIT_AS 9
struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};
int getrlimit(int resource, struct rlimit* limit);
int setrlimit(int resource, struct rlimit const* limit);

#define RUSAGE_SELF 0
#define RUSAGE_THREAD 1
struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long long ru_maxrss; // KB, like Linux
    long long ru_minflt; // Page faults (Windows doesn't tell soft from hard)
    long long ru_majflt;
};
int getrusage(int who, struct rusage* usage);
