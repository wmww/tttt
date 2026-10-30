#pragma once
#pragma GCC system_header // (#include_next)
// Windows: the C runtime's <limits.h> with PATH_MAX for paths as long as Windows takes (MinGW-w64's is MAX_PATH, 260;
// the manifest's longPathAware lifts that limit, and node_modules paths pass it)
#include_next <limits.h>
#undef PATH_MAX
#define PATH_MAX 4096
