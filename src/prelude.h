#pragma once

#include <stdio.h>

#include <stc/cstr.h>
#include <stc/csview.h>
#include <stc/czview.h>
#include <stc/types.h>
#include "wrap_stc/forward.h"

#define my me->
#define __ (void)

__attribute__((noreturn))
void panic_internal(const char* error_type, const char* file, int line, const char* fmt, ...) __attribute__ ((format (printf, 4, 5)));
#define PANIC(...) panic_internal("panic", __FILE__, __LINE__, __VA_ARGS__)
#define ASSERT(cond) if (!(cond)) panic_internal("assertion failed", __FILE__, __LINE__, "%s", #cond)
#define UNREACHABLE PANIC("unreachable reached \\x_x/")

void nonfatal_internal(const char* error_type, const char* file, int line, const char* fmt, ...) __attribute__ ((format (printf, 4, 5)));
#define NONFATAL(...) nonfatal_internal("nonfatal", __FILE__, __LINE__, __VA_ARGS__)
void inhibit_nonfatals();
void uninhibit_nonfatals();

#define INDENT "  "
#define APPEND_INDENT(str, level) for (int indent_i = 0; indent_i < level; indent_i++) cstr_append(str, INDENT);

#define MUST_USE __attribute__((warn_unused_result))

#define CLR0 "\x1b[0m"
#define CLR_ERR "\x1b[1;31m"
#define CLR_ERR_CODE "\x1b[95m"
#define CLR_GOOD "\x1b[1;32m"
#define CLR_PATH "\x1b[94m"
#define CLR_STATUS "\x1b[0;94m"

#define CLR_VALUE "\x1b[0;36m"
#define CLR_STR "\x1b[0;33m"
#define CLR_NUM "\x1b[0;34m"
#define CLR_TYPE "\x1b[0;32m"
#define CLR_NAME "\x1b[0;35m"
#define CLR_IMPLICIT "\x1b[0;37m"

typedef enum ResolveState {
    RESOLVE_STATE_UNINITIALIZED = 0,
    RESOLVE_STATE_IN_FLIGHT,
    RESOLVE_STATE_DONE,
} ResolveState;
