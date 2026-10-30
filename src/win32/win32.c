// Windows: what src/win32/compat.h, sys/mman.h and sys/resource.h declare. Not part of the build elsewhere
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "compat.h"
#include "dirent.h"
#include "execinfo.h"
#include "sys/mman.h"
#include "sys/resource.h"

// ---- The C runtime ----

// Every file the runtime opens is binary (open() and fopen() without a mode letter would translate CRLF and stop at
// a Ctrl-Z), and so are the standard streams: bytes in, bytes out, LF where tsgo writes LF
__attribute__((constructor)) static void binary_files(void) {
    _set_fmode(_O_BINARY);
    _setmode(0, _O_BINARY);
    _setmode(1, _O_BINARY);
    _setmode(2, _O_BINARY);
}

// ---- Memory ----
//
// Linux's anonymous memory is reserved address space whose pages appear (zeroed) when touched; Windows' reserved
// address space faults until committed. A read-write mapping is a reservation registered here, and the vectored
// exception handler commits the part of a registered range an access faults on, LAZY_COMMIT at a time: a
// MAP_NORESERVE range costs commit charge only as it's used, MADV_DONTNEED decommits (the next touch reads zeroes,
// as on Linux) and MADV_POPULATE_WRITE commits up front, so no exception is taken where the code populates ahead
// (the AST slabs, checker arenas, read chunks). A range without MAP_NORESERVE is committed whole by mmap().

enum { LAZY_COMMIT = 1 << 20 };

typedef struct LazyRange {
    uintptr_t start, end;
} LazyRange;
static SRWLOCK lazy_lock = SRWLOCK_INIT;
static LazyRange* lazy_ranges; // Sorted by start, disjoint
static size_t lazy_count, lazy_cap;

// The first range whose end is above addr. Under lazy_lock
static size_t lazy_find(uintptr_t addr) {
    size_t low = 0, high = lazy_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (lazy_ranges[mid].end <= addr) low = mid + 1;
        else high = mid;
    }
    return low;
}

static void lazy_insert_locked(uintptr_t start, uintptr_t end) {
    if (lazy_count == lazy_cap) {
        size_t cap = lazy_cap ? lazy_cap * 2 : 256;
        // (HeapAlloc, not malloc: no allocator of ours runs under this lock)
        LazyRange* grown = lazy_ranges ? HeapReAlloc(GetProcessHeap(), 0, lazy_ranges, cap * sizeof(LazyRange))
                                       : HeapAlloc(GetProcessHeap(), 0, cap * sizeof(LazyRange));
        if (!grown) abort();
        lazy_ranges = grown;
        lazy_cap = cap;
    }
    size_t at = lazy_find(start);
    memmove(lazy_ranges + at + 1, lazy_ranges + at, (lazy_count - at) * sizeof(LazyRange));
    lazy_ranges[at] = (LazyRange){ start, end };
    lazy_count++;
}

static void lazy_add(uintptr_t start, uintptr_t end) {
    AcquireSRWLockExclusive(&lazy_lock);
    lazy_insert_locked(start, end);
    ReleaseSRWLockExclusive(&lazy_lock);
}

// Takes [start, end) out of the registered ranges (splitting one it's inside)
static void lazy_remove(uintptr_t start, uintptr_t end) {
    AcquireSRWLockExclusive(&lazy_lock);
    for (size_t i = lazy_find(start); i < lazy_count && lazy_ranges[i].start < end;) {
        LazyRange r = lazy_ranges[i];
        if (r.start < start && r.end > end) {
            lazy_ranges[i].end = start;
            lazy_insert_locked(end, r.end);
            break;
        }
        if (r.start < start) {
            lazy_ranges[i].end = start;
            i++;
        } else if (r.end > end) {
            lazy_ranges[i].start = end;
            break;
        } else {
            memmove(lazy_ranges + i, lazy_ranges + i + 1, (lazy_count - i - 1) * sizeof(LazyRange));
            lazy_count--;
        }
    }
    ReleaseSRWLockExclusive(&lazy_lock);
}

static LONG CALLBACK lazy_commit_handler(EXCEPTION_POINTERS* info) {
    EXCEPTION_RECORD const* record = info->ExceptionRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t addr = (uintptr_t)record->ExceptionInformation[1];
    AcquireSRWLockShared(&lazy_lock);
    size_t i = lazy_find(addr);
    bool ours = i < lazy_count && lazy_ranges[i].start <= addr;
    uintptr_t from = 0, to = 0;
    if (ours) {
        from = addr & ~(uintptr_t)(LAZY_COMMIT - 1);
        to = from + LAZY_COMMIT;
        if (from < lazy_ranges[i].start) from = lazy_ranges[i].start;
        if (to > lazy_ranges[i].end) to = lazy_ranges[i].end;
    }
    ReleaseSRWLockShared(&lazy_lock);
    if (!ours) return EXCEPTION_CONTINUE_SEARCH;
    if (!VirtualAlloc((void*)from, to - from, MEM_COMMIT, PAGE_READWRITE)) {
        // (The commit limit: RAM plus the page file)
        static char const message[] = "tttt: out of memory (the system's commit limit)\n";
        DWORD written;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, sizeof message - 1, &written, nullptr);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

static INIT_ONCE handler_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK handler_install(INIT_ONCE* once, void* param, void** context) {
    (void)once, (void)param, (void)context;
    // (First: before any handler the runtime added)
    return AddVectoredExceptionHandler(1, lazy_commit_handler) != nullptr;
}

void* mmap(void* addr, size_t len, int prot, int flags, int fd, long long offset) {
    (void)offset;
    if (fd != -1 || !(flags & MAP_ANONYMOUS) || !len) {
        errno = ENOTSUP;
        return MAP_FAILED;
    }
    InitOnceExecuteOnce(&handler_once, handler_install, nullptr, nullptr);
    if (flags & MAP_FIXED) {
        // (Only over a range of ours: back to reserved, read-write ranges read as zeroes again)
        uintptr_t start = (uintptr_t)addr, end = start + len;
        VirtualFree(addr, len, MEM_DECOMMIT);
        lazy_remove(start, end);
        if (prot & (PROT_READ | PROT_WRITE)) lazy_add(start, end);
        return addr;
    }
    bool access = prot & (PROT_READ | PROT_WRITE);
    void* ret = VirtualAlloc(addr, len, MEM_RESERVE, access ? PAGE_READWRITE : PAGE_NOACCESS);
    if (!ret) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    if (access) {
        if (!(flags & MAP_NORESERVE) && !VirtualAlloc(ret, len, MEM_COMMIT, PAGE_READWRITE)) {
            VirtualFree(ret, 0, MEM_RELEASE);
            errno = ENOMEM;
            return MAP_FAILED;
        }
        lazy_add((uintptr_t)ret, (uintptr_t)ret + len);
    }
    return ret;
}

int munmap(void* addr, size_t len) {
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(addr, &info, sizeof info)) return errno = EINVAL, -1;
    lazy_remove((uintptr_t)addr, (uintptr_t)addr + len);
    // (The whole reservation: released. Part of one, which Windows can't release: decommitted, its addresses stay
    // reserved and fault from now on, as an unmapped range does)
    if (info.AllocationBase == addr) {
        MEMORY_BASIC_INFORMATION last;
        uintptr_t end = (uintptr_t)addr + len;
        // (Is anything of the reservation left past len?)
        if (VirtualQuery((void*)end, &last, sizeof last) && last.AllocationBase == addr) {
            VirtualFree(addr, len, MEM_DECOMMIT);
            return 0;
        }
        return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : (errno = EINVAL, -1);
    }
    VirtualFree(addr, len, MEM_DECOMMIT);
    return 0;
}

int madvise(void* addr, size_t len, int advice) {
    if (!len) return 0;
    if (advice == MADV_DONTNEED) return VirtualFree(addr, len, MEM_DECOMMIT) ? 0 : (errno = EINVAL, -1);
    if (advice == MADV_POPULATE_WRITE) return VirtualAlloc(addr, len, MEM_COMMIT, PAGE_READWRITE) ? 0 : (errno = ENOMEM, -1);
    return 0;
}

int mprotect(void* addr, size_t len, int prot) {
    uintptr_t start = (uintptr_t)addr, end = start + len;
    if (!(prot & (PROT_READ | PROT_WRITE))) {
        // (Faults from now on: decommitted and not committed again by the handler)
        lazy_remove(start, end);
        VirtualFree(addr, len, MEM_DECOMMIT);
        return 0;
    }
    lazy_remove(start, end);
    lazy_add(start, end);
    return 0;
}

long sysconf(int name) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    if (name == _SC_PAGESIZE) return (long)info.dwPageSize;
    if (name == _SC_NPROCESSORS_ONLN) return (long)info.dwNumberOfProcessors;
    if (name == _SC_PHYS_PAGES) {
        MEMORYSTATUSEX status = { .dwLength = sizeof status };
        // (long is 32 bits here: pages, not bytes, fit)
        return GlobalMemoryStatusEx(&status) ? (long)(status.ullTotalPhys / info.dwPageSize) : -1;
    }
    errno = EINVAL;
    return -1;
}

// ---- Strings ----

void* memrchr(void const* s, int c, size_t n) {
    unsigned char const* p = s;
    while (n--) {
        if (p[n] == (unsigned char)c) return (void*)(p + n);
    }
    return nullptr;
}

void* memmem(void const* haystack, size_t haystack_len, void const* needle, size_t needle_len) {
    if (!needle_len) return (void*)haystack;
    char const* h = haystack;
    char const* n = needle;
    while (haystack_len >= needle_len) {
        char const* first = memchr(h, n[0], haystack_len - needle_len + 1);
        if (!first) return nullptr;
        if (!memcmp(first, n, needle_len)) return (void*)first;
        haystack_len -= (size_t)(first - h) + 1;
        h = first + 1;
    }
    return nullptr;
}

// ---- Paths ----

// UTF-8 -> a malloc()ed UTF-16 string, null on failure
static wchar_t* wide_of(char const* s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, nullptr, 0);
    if (n <= 0) return nullptr;
    wchar_t* ret = malloc((size_t)n * sizeof(wchar_t));
    if (ret && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, ret, n) != n) {
        free(ret);
        return nullptr;
    }
    return ret;
}

char* realpath(char const* path, char* resolved) {
    wchar_t* wide = wide_of(path);
    if (!wide) return errno = ENOENT, nullptr;
    HANDLE h = CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    free(wide);
    if (h == INVALID_HANDLE_VALUE) return errno = ENOENT, nullptr;
    wchar_t buf[32768];
    DWORD n = GetFinalPathNameByHandleW(h, buf, sizeof buf / sizeof buf[0], FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(h);
    if (!n || n >= sizeof buf / sizeof buf[0]) return errno = ENOENT, nullptr;
    wchar_t const* from = buf;
    bool unc = false;
    // (\\?\C:\dir, \\?\UNC\server\share\dir)
    if (!wcsncmp(from, L"\\\\?\\UNC\\", 8)) from += 7, unc = true;
    else if (!wcsncmp(from, L"\\\\?\\", 4)) from += 4;
    char out[PATH_MAX];
    size_t at = 0;
    if (unc) out[at++] = '/';
    int bytes = WideCharToMultiByte(CP_UTF8, 0, from, -1, out + at, (int)(sizeof out - at), nullptr, nullptr);
    if (bytes <= 0) return errno = ENAMETOOLONG, nullptr;
    for (char* c = out; *c; c++) {
        if (*c == '\\') *c = '/';
    }
    size_t len = strlen(out);
    // ("C:/" stays, as tsgo's root; "C:/dir/" doesn't happen)
    if (!resolved) resolved = malloc(len + 1);
    if (!resolved) return errno = ENOMEM, nullptr;
    memcpy(resolved, out, len + 1);
    return resolved;
}

int lstat(char const* path, struct stat* st) {
    if (stat(path, st)) return -1;
    wchar_t* wide = wide_of(path);
    if (!wide) return 0;
    DWORD attributes = GetFileAttributesW(wide);
    free(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        st->st_mode = (unsigned short)((st->st_mode & ~0xF000) | S_IFLNK);
    }
    return 0;
}

// ---- Directories ----

struct DIR {
    HANDLE find;
    bool first; // found holds the first entry, not yet returned
    WIN32_FIND_DATAW found;
    struct dirent entry;
};

DIR* opendir(char const* path) {
    size_t len = strlen(path);
    char* pattern = malloc(len + 3);
    if (!pattern) return errno = ENOMEM, nullptr;
    memcpy(pattern, path, len);
    if (len && pattern[len - 1] != '/' && pattern[len - 1] != '\\') pattern[len++] = '/';
    pattern[len++] = '*';
    pattern[len] = 0;
    wchar_t* wide = wide_of(pattern);
    free(pattern);
    if (!wide) return errno = ENOENT, nullptr;
    DIR* dir = calloc(1, sizeof(DIR));
    if (!dir) {
        free(wide);
        return errno = ENOMEM, nullptr;
    }
    dir->find = FindFirstFileExW(wide, FindExInfoBasic, &dir->found, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    free(wide);
    if (dir->find == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        free(dir);
        errno = error == ERROR_DIRECTORY ? ENOTDIR : ENOENT;
        return nullptr;
    }
    dir->first = true;
    return dir;
}

struct dirent* readdir(DIR* dir) {
    for (;;) {
        if (!dir->first && !FindNextFileW(dir->find, &dir->found)) return nullptr;
        dir->first = false;
        if (WideCharToMultiByte(CP_UTF8, 0, dir->found.cFileName, -1, dir->entry.d_name, sizeof dir->entry.d_name, nullptr, nullptr) <= 0) continue;
        DWORD attributes = dir->found.dwFileAttributes;
        dir->entry.d_type = attributes & FILE_ATTRIBUTE_REPARSE_POINT ? DT_UNKNOWN : attributes & FILE_ATTRIBUTE_DIRECTORY ? DT_DIR : DT_REG;
        return &dir->entry;
    }
}

int closedir(DIR* dir) {
    FindClose(dir->find);
    free(dir);
    return 0;
}

// ---- The process ----

int backtrace(void** frames, int size) {
    return size > 0 ? CaptureStackBackTrace(1, (DWORD)size, frames, nullptr) : 0;
}

void backtrace_symbols_fd(void* const* frames, int size, int fd) {
    for (int i = 0; i < size; i++) {
        char line[32];
        int n = snprintf(line, sizeof line, "%p\n", frames[i]);
        if (n > 0) _write(fd, line, (unsigned)n);
    }
}

int getrlimit(int resource, struct rlimit* limit) {
    if (resource == RLIMIT_STACK) {
        // (The stack each thread reserves: the executable's SizeOfStackReserve)
        HMODULE exe = GetModuleHandleW(nullptr);
        IMAGE_DOS_HEADER const* dos = (IMAGE_DOS_HEADER const*)exe;
        IMAGE_NT_HEADERS const* nt = (IMAGE_NT_HEADERS const*)((char const*)exe + dos->e_lfanew);
        limit->rlim_cur = limit->rlim_max = nt->OptionalHeader.SizeOfStackReserve;
        return 0;
    }
    limit->rlim_cur = limit->rlim_max = RLIM_INFINITY;
    return 0;
}

int setrlimit(int resource, struct rlimit const* limit) {
    (void)resource, (void)limit;
    errno = EPERM;
    return -1;
}

static void timeval_of(FILETIME t, struct timeval* out) {
    uint64_t ticks = ((uint64_t)t.dwHighDateTime << 32 | t.dwLowDateTime) / 10; // (100 ns -> us)
    out->tv_sec = (long)(ticks / 1000000);
    out->tv_usec = (long)(ticks % 1000000);
}

int getrusage(int who, struct rusage* usage) {
    memset(usage, 0, sizeof *usage);
    FILETIME created, exited, kernel, user;
    bool got = who == RUSAGE_THREAD ? GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)
                                    : GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    if (!got) return errno = EINVAL, -1;
    timeval_of(user, &usage->ru_utime);
    timeval_of(kernel, &usage->ru_stime);
    PROCESS_MEMORY_COUNTERS counters;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters)) {
        usage->ru_maxrss = (long long)(counters.PeakWorkingSetSize >> 10);
        usage->ru_minflt = counters.PageFaultCount;
    }
    return 0;
}

double win32_thread_cpu_ms(void) {
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return 0;
    uint64_t k = (uint64_t)kernel.dwHighDateTime << 32 | kernel.dwLowDateTime;
    uint64_t u = (uint64_t)user.dwHighDateTime << 32 | user.dwLowDateTime;
    return (double)(k + u) / 1e4;
}

uint64_t win32_resident_bytes(void) {
    PROCESS_MEMORY_COUNTERS counters;
    return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters) ? counters.WorkingSetSize : 0;
}

uint64_t win32_available_bytes(void) {
    MEMORYSTATUSEX status = { .dwLength = sizeof status };
    return GlobalMemoryStatusEx(&status) ? status.ullAvailPhys : 0;
}

unsigned win32_physical_cores(void) {
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (!bytes) return 0;
    char* buf = malloc(bytes);
    if (!buf) return 0;
    unsigned cores = 0;
    DWORD_PTR process_mask = 0, system_mask = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask);
    WORD group = 0;
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)buf, &bytes)) {
        for (DWORD at = 0; at < bytes;) {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX const* e = (void const*)(buf + at);
            // (A core counts when one of its logical processors is in the process's mask: one group, as the mask is)
            for (WORD g = 0; g < e->Processor.GroupCount; g++) {
                if (e->Processor.GroupMask[g].Group == group && (e->Processor.GroupMask[g].Mask & process_mask)) {
                    cores++;
                    break;
                }
            }
            at += e->Size;
        }
    }
    free(buf);
    return cores;
}
