#pragma once

#include <stdio.h>

#include <stc/cstr.h>
#include <stc/csview.h>
#include <stc/czview.h>
#include <stc/types.h>
#include "wrap_stc/forward.h"

#define my me->
#define ignore (void)

__attribute__((noreturn))
void panic_internal(const char* error_type, const char* file, int line, const char* fmt, ...) __attribute__((format (printf, 4, 5)));
#define PANIC(...) panic_internal("panic", __FILE__, __LINE__, __VA_ARGS__)
#define PANIC_IF(cond, ...) if (cond) panic_internal("panic", __FILE__, __LINE__, __VA_ARGS__)
#define ASSERT(cond) if (!(cond)) panic_internal("assertion failed", __FILE__, __LINE__, "%s", #cond)
#define UNREACHABLE PANIC("unreachable reached \\x_x/")

extern int nonfatal_inhibit_count;
cstr* acquire_debug_cstr(); // Should only be used by FMT_DBG
const char* get_latest_debug_str(); // Should only be used by FMT_DBG
void nonfatal_internal(const char* error_type, const char* file, int line, const char* fmt, ...) __attribute__((format (printf, 4, 5)));
void print_debug_internal(const char* file, int line, const char* fmt, ...) __attribute__((format (printf, 3, 4)));
#define NONFATAL(...) do { if (nonfatal_inhibit_count == 0) nonfatal_internal("nonfatal", __FILE__, __LINE__, __VA_ARGS__); } while (false)
#define PRINT_DBG(...) { if (nonfatal_inhibit_count == 0) print_debug_internal(__FILE__, __LINE__, __VA_ARGS__); } while (false)


#define cstr_append_lit(str, lit) cstr_append_sv(str, c_sv(lit))
#define c_sv_const_init(str) {str, c_litstrlen(str)}

#define MUST_USE __attribute__((warn_unused_result))

// PERF: make this optional (it's always on in debug mode, but it hardly slows us down so it's nice to have in release
// mode sometimes. At time of writing it's only useful for identifying which test of many has a leak)
#define DEBUG_ALLOCATOR

#if defined DEBUG_BUILD || defined DEBUG_ALLOCATOR
#include "debug_allocator.h"
#endif

// These are used in debugging, DBG should not appear in committed code. Other debugging logic can be tagged with // DBG
#define PANIC_DBG PANIC("AAaaa");
#define PANIC_AFTER_DBG(count, ...) do { \
        static int panic_count = 0; \
        panic_count++; \
        if (panic_count > count) PANIC(__VA_ARGS__); \
        else NONFATAL(__VA_ARGS__); \
    } while (false)
#define NONFATAL_DBG NONFATAL
#define INDENT_DBG(size) (display_repeated(debug_brief_display(acquire_debug_cstr()), c_sv(" "), size), get_latest_debug_str())
#define FMT_DBG(type, value) (display_##type(debug_brief_display(acquire_debug_cstr()), &(value)), get_latest_debug_str())
#define TYPE_DBG(value) FMT_DBG(type, value)
