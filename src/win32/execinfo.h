#pragma once
// Windows: glibc's backtrace() for the debug instrumentation that prints stacks (src/win32/win32.c): return addresses
// only, printed in hex (no symbols: addr2line on the executable names them)
int backtrace(void** frames, int size);
void backtrace_symbols_fd(void* const* frames, int size, int fd);
