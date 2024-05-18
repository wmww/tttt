#pragma once

#include "error.h"

typedef struct Span Span;
typedef struct Type Type;
typedef struct JsObject JsObject;
typedef struct BaselineTestSuite BaselineTestSuite;

typedef struct LineNumberedStr {
    int line;
    cstr str;
} LineNumberedStr;

void line_numbered_str_drop(LineNumberedStr* me);

#define i_key_class_names LineNumberedStr, line_numbered_str
#define i_no_clone
#include "wrap_stc/vec.h"

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
        VecLineNumberedStr symbols;
        VecLineNumberedStr types;
    } detail;
} BaselineTestCase;

BaselineTestCase make_baseline_test_case(BaselineTestSuite* suite, cstr name, cstr source_path, JsObject* previous);
void baseline_test_case_drop(BaselineTestCase* me);
void baseline_test_case_clear_detail(BaselineTestCase* me);
void baseline_test_case_add_symbol(BaselineTestCase* me, csview name, Span span, Span decl_span);
void baseline_test_case_add_symbol_with_scope(
    BaselineTestCase* me,
    csview scope,
    csview name,
    Span span,
    bool name_is_value,
    Span decl_span
);
void baseline_test_case_add_type(BaselineTestCase* me, Span span, Type const* type);
void baseline_test_case_add_type_with_peren(BaselineTestCase* me, Span span, Type const* type, int paren_depth);
void baseline_test_case_run(BaselineTestCase* me, bool noisy); // returns if test ran successfully, NOT if it passed
bool baseline_test_case_is_pass(BaselineTestCase const* me);
bool baseline_test_case_was_pass(BaselineTestCase const* me);
// Both *_has_new_pass() and *_has_new_fail() can be true at once, _has_new_pass() does not imply *_is_pass()
bool baseline_test_case_has_new_pass(BaselineTestCase const* me);
bool baseline_test_case_has_new_fail(BaselineTestCase const* me);
