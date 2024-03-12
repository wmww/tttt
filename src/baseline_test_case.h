#pragma once

#include "prelude.h"

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
    const char* test_path;
    cstr contents;
    vec_LineNumberedStr symbols;
    vec_LineNumberedStr types;
} BaselineTestCase;

void BaselineTestCase_drop(BaselineTestCase* self);
void BaselineTestCase_add_symbol(BaselineTestCase* test_case, const char* name, Span const* span);
void BaselineTestCase_add_type(BaselineTestCase* ctx, Span const* span, Type const* type);
bool run_baseline_test_case(BaselineTestSuite* suite, const char* test_name, const char* test_path);
