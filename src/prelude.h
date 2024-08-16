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

#define cstr_append_lit(str, lit) cstr_append_sv(str, c_sv(lit))
#define c_sv_const_init(str) {str, c_litstrlen(str)}

#define MUST_USE __attribute__((warn_unused_result))

typedef enum ResolveState {
    RESOLVE_STATE_UNINITIALIZED = 0,
    RESOLVE_STATE_IN_FLIGHT,
    RESOLVE_STATE_DONE,
} ResolveState;
