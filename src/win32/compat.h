#pragma once
// Windows: the POSIX and GNU functions tttt uses that the C runtime (UCRT, MinGW-w64) lacks, implemented in
// src/win32/win32.c. prelude.h includes this on _WIN32 only; the directory is on the include path only there, where its
// sys/mman.h and sys/resource.h stand in for the system's. Paths are UTF-8 with forward slashes (the process's code
// page is UTF-8: the manifest in src/win32/tttt.manifest), absolute ones `C:/dir` (or `/dir`)
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <malloc.h> // (_aligned_malloc())

#include <limits.h> // (src/win32's: PATH_MAX)

// The resolved path, symlinks and junctions followed (GetFinalPathNameByHandle, as tsgo's nativepath.Realpath: the
// names' case as on disk), forward slashes; null and errno set if path doesn't exist. resolved: PATH_MAX bytes, or null
// for a malloc()ed result
char* realpath(char const* path, char* resolved);
void* memrchr(void const* s, int c, size_t n);
void* memmem(void const* haystack, size_t haystack_len, void const* needle, size_t needle_len);

#define _SC_PAGESIZE 30
#define _SC_NPROCESSORS_ONLN 84
#define _SC_PHYS_PAGES 85
long sysconf(int name);

// lstat(): Windows' stat() follows no links of its own; a symlink or junction is S_IFLNK here (FILE_ATTRIBUTE_REPARSE_POINT)
#ifndef S_IFLNK
#define S_IFLNK 0xA000
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & 0xF000) == S_IFLNK)
#endif
struct stat;
int lstat(char const* path, struct stat* st);

#ifndef O_CLOEXEC
#define O_CLOEXEC 0 // (Windows' handles aren't inherited unless asked)
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

// The physical cores of the processors this process may run on (GetLogicalProcessorInformationEx), 0 when unknown
unsigned win32_physical_cores(void);
// The process's working set and commit charge, bytes
uint64_t win32_resident_bytes(void);
// The physical memory available now (GlobalMemoryStatusEx), bytes
uint64_t win32_available_bytes(void);
// The calling thread's CPU time, milliseconds
double win32_thread_cpu_ms(void);
