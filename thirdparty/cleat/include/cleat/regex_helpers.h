#pragma once

#define cregex_iter cregex_iter_stcs_version
#include <stc/cregex.h>
#undef cregex_iter

typedef struct {
    const cregex* regex;
    csview input;
    csview match[CREG_MAX_CAPTURES];
    bool ref;
} cregex_iter;

static inline void cregex_next(cregex_iter* me) {
    me->ref = cregex_match_sv_opt(
        me->regex,
        me->input,
        (struct cregex_match_opt){
            .match = me->match,
            .flags = CREG_NEXT,
        }
    ) == CREG_OK;
}

static inline cregex_iter cregex_begin(cregex* re, csview sv) {
    cregex_iter ret = {
        .regex = re,
        .input = sv,
        .match = {0},
        .ref = true,
    };
    cregex_next(&ret);
    return ret;
}

static inline csview* cregex_iter_deref(cregex_iter* iter) { return iter->match; }
