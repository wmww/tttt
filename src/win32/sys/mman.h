#pragma once
// Windows: the part of <sys/mman.h> tttt uses, anonymous memory only (src/win32/win32.c). A read-write mapping is
// reserved and committed lazily: a vectored exception handler commits the pages an access faults on (a megabyte at a
// time), so MAP_NORESERVE reservations, MADV_DONTNEED (decommitted, read back as zeroes) and MADV_POPULATE_WRITE
// (committed with one call) mean what they mean on Linux. File mappings aren't supported (source texts are read)
#include <stddef.h>
#include <sys/types.h>

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000
#ifndef MAP_STACK
#define MAP_STACK 0x20000
#endif
#define MAP_FAILED ((void*)-1)
#define MADV_DONTNEED 4
#define MADV_POPULATE_WRITE 23

void* mmap(void* addr, size_t len, int prot, int flags, int fd, long long offset);
int munmap(void* addr, size_t len);
int madvise(void* addr, size_t len, int advice);
int mprotect(void* addr, size_t len, int prot);
