#pragma once

#include "prelude.h"
#include "error.h"

typedef struct Span Span;
typedef struct Type Type;
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
    struct {
        cstr source;
        ErrorCtx errs;
        vec_LineNumberedStr symbols;
        vec_LineNumberedStr types;
    } detail;
} BaselineTestCase;

BaselineTestCase make_baseline_test_case(BaselineTestSuite* suite, cstr source_path);
void BaselineTestCase_drop(BaselineTestCase* self);
void BaselineTestCase_clear_detail(BaselineTestCase* self);
void BaselineTestCase_add_symbol(BaselineTestCase* self, const char* name, Span const* span);
void BaselineTestCase_add_type(BaselineTestCase* self, Span const* span, Type const* type);
bool BaselineTestCase_run(BaselineTestCase* self, bool noisy);
