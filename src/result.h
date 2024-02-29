#pragma once

#include "value.h"

typedef struct Result {
    enum {
        RESULT_OK = 0,
        RESULT_ERR,
        RESULT_RETURN,
        RESULT_BREAK,
    } tag;
    Value value;
} Result;

static inline void Result_drop(Result* self) {
    if (self->value.tag != VALUE_UNDEFINED) {
        Value_drop(&self->value);
    }
}

static inline Result Result_ok(Value value) {
    return (Result) {
        .tag = RESULT_OK,
        .value = value,
    };
}

static inline Result Result_err(Value value) {
    return (Result) {
        .tag = RESULT_ERR,
        .value = value,
    };
}

static inline Result Result_return(Value value) {
    return (Result) {
        .tag = RESULT_RETURN,
        .value = value,
    };
}

static inline Result Result_break() {
    return (Result) {
        .tag = RESULT_BREAK,
        .value = Value_undefined,
    };
}

#define Result_ok_undefined ((Result){ .tag = RESULT_OK, .value = Value_undefined })

#define TRY_CATCH(ok_name, expr, result_name, catch_block) \
    Value ok_name = Value_undefined; \
    { \
        Result result_name = expr; \
        if (result_name.tag != RESULT_OK) { catch_block } \
        else { ok_name = result_name.value; } \
    }
#define TRY_THROW(ok_name, expr) TRY_CATCH(ok_name, expr, result_tmp, return result_tmp;)
#define TRY_THROW_DROP(expr) { TRY_THROW(value_tmp, expr); Value_drop(&value_tmp); }
#define TRY_CATCH_DROP(expr, result_name, catch_block) \
    { TRY_CATCH(value_tmp, expr, result_name, catch_block); Value_drop(&value_tmp); }

