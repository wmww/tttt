#pragma once

#include <stdio.h>

#include <stc/cstr.h>
#include <stc/types.h>

__attribute__((noreturn))
void panic(const char* file, int line, const char* fmt, ...) __attribute__ ((format (printf, 3, 4)));
#define PANIC(...) panic(__FILE__, __LINE__, __VA_ARGS__)
#define UNREACHABLE PANIC("unreachable was reached \\x_x/")

#define INDENT "  "
#define APPEND_INDENT(str, level) for (int indent_i = 0; indent_i < level; indent_i++) cstr_append(str, INDENT);
