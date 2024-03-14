#pragma once

#include "error.h"

typedef struct Span Span;
typedef struct Type Type;
typedef struct ObjectValue ObjectValue;
typedef struct BaselineTestSuite BaselineTestSuite;

typedef struct LineNumberedStr {
    int line;
    cstr str;
} LineNumberedStr;

void LineNumberedStr_drop(LineNumberedStr* self);

#define i_key_class LineNumberedStr
#define i_no_clone
#include "stc/vec.h"

typedef struct BaselineTestCase {
    BaselineTestSuite* suite;
    cstr name;
    cstr source_path;
    bool has_run;
    bool has_prev;
    bool errors_prev;
    bool symbols_prev;
    bool types_prev;
    bool errors_passed;
    bool symbols_passed;
    bool types_passed;
    struct {
        cstr source;
        ErrorCtx errs;
        vec_LineNumberedStr symbols;
        vec_LineNumberedStr types;
    } detail;
} BaselineTestCase;

BaselineTestCase make_baseline_test_case(BaselineTestSuite* suite, cstr name, cstr source_path, ObjectValue* previous);
void BaselineTestCase_drop(BaselineTestCase* self);
void BaselineTestCase_clear_detail(BaselineTestCase* self);
void BaselineTestCase_add_symbol(BaselineTestCase* self, const char* name, Span const* span);
void BaselineTestCase_add_type(BaselineTestCase* self, Span const* span, Type const* type);
void BaselineTestCase_run(BaselineTestCase* self, bool noisy); // returns if test ran successfully, NOT if it passed
bool BaselineTestCase_is_pass(BaselineTestCase const* self);
// Both *_has_new_pass() and *_has_new_fail() can be true at once, _has_new_pass() does not imply *_is_pass()
bool BaselineTestCase_has_new_pass(BaselineTestCase const* self);
bool BaselineTestCase_has_new_fail(BaselineTestCase const* self);
