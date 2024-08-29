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

// These are used in debugging, DBG should not appear in committed code. Other debugging logic can be tagged with // DBG
#define PANIC_AFTER_DBG(count, ...) do { \
        static int panic_count = 0; \
        panic_count++; \
        if (panic_count > count) PANIC(__VA_ARGS__); \
        else NONFATAL(__VA_ARGS__); \
    } while (false)
#define NONFATAL_DBG NONFATAL
#define NONFATAL_TYPE_DBG(msg, type) do { \
        cstr tmp = cstr_lit(msg); \
        display_type(debug_display(&tmp), type); \
        NONFATAL("%s", cstr_str(&tmp)); \
        cstr_drop(&tmp); \
    } while (false)
