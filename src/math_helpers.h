#pragma once

#include <stdint.h>

static inline int imax(int a, int b) {
    return a > b ? a : b;
}

static inline const char* ccpmax(const char* a, const char* b) {
    return a > b ? a : b;
}

static inline const char* ccpmin(const char* a, const char* b) {
    return a < b ? a : b;
}
