#pragma once

#include "value.h"

typedef struct Result {
    Value value;
    bool is_err;
} Result;

static inline void Result_drop(Result* self) {
    if (self->value.tag != VALUE_UNDEFINED) {
        Value_drop(&self->value);
    }
}

static inline Result Result_ok(Value value) {
    return (Result) {
        .is_err = false,
        .value = value,
    };
}

static inline Result Result_err(Value value) {
    return (Result) {
        .is_err = true,
        .value = value,
    };
}

#define Result_ok_undefined ((Result){ .is_err = false, .value = Value_undefined })

#define TRY_CATCH(ok_name, expr, result_name, catch_block) \
    Value ok_name = Value_undefined; \
    { \
        Result result_name = expr; \
        if (result_name.is_err) { catch_block } \
        else { ok_name = result_name.value; } \
    }
#define TRY_THROW(ok_name, expr) TRY_CATCH(ok_name, expr, result_tmp, return result_tmp;)
#define TRY_THROW_DROP(expr) { TRY_THROW(value_tmp, expr); Value_drop(&value_tmp); }
#define TRY_CATCH_DROP(expr, result_name, catch_block) \
    { TRY_CATCH(value_tmp, expr, result_name, catch_block); Value_drop(&value_tmp); }

