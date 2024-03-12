#pragma once

#include <stdio.h>

#include <stc/cstr.h>
#include "stc/csview.h"
#include <stc/types.h>

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
#define CLR_ERR "\x1b[31;1m"
#define CLR_GOOD "\x1b[32;1m"
#define CLR_PATH "\x1b[35m"
#define CLR_VALUE "\x1b[36m"
#define CLR_STR "\x1b[33m"
#define CLR_NUM "\x1b[34m"
#define CLR_TYPE "\x1b[32m"
#define CLR_NAME "\x1b[35m"
#define CLR_IMPLICIT "\x1b[37m"
