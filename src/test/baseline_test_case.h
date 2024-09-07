#pragma once

#include "error.h"

typedef struct Span Span;
typedef struct Type Type;
typedef struct Program Program;
typedef struct MockFs MockFs;
typedef struct SourceFile SourceFile;
typedef struct JsObject JsObject;
typedef struct BaselineTestSuite BaselineTestSuite;
typedef struct LineNumberedStr LineNumberedStr;

wrap_forward_vec(LineNumberedStr, line_numbered_str);

// Get the name of a test given it's path (just strips the dir and extension). output is only valid as long as input is.
csview test_name_from_path(const char* path);

typedef struct BaselineTestCase {
    const char* source_path; // Not owned
    bool did_run;
    ErrorCtx* error_ctx;
    MockFs* fs;
    Program* program;
    SourceFile const* main_source;
    VecLineNumberedStr symbols;
    VecLineNumberedStr types;
} BaselineTestCase;

// Never prints to stdout, only prints to stderr if noisy is true, or if it panics
BaselineTestCase run_baseline_test_case(const char* source_path, bool noisy);
void baseline_test_case_drop(BaselineTestCase* me);

// These are used to get the data into string form, so it can be compared with known correct results or serialized
void baseline_test_case_render_errors(cstr* target, BaselineTestCase const* me);
void baseline_test_case_render_symbols(cstr* target, BaselineTestCase const* me);
void baseline_test_case_render_types(cstr* target, BaselineTestCase const* me);

typedef struct BaselineSym {
    VecSpan declarations;   // The places this symbol was declared. Empty spans are ignored. If empty or all nulls falls
                            // back to `span` unless allow_declaration_empty is set
    bool allow_declaration_empty; // if this is true and `declarations` is empty or all null no declarations are added
    VecSpan namespaces;     // Includes the interface/class name if applicable. Empty spans are ignored, `span`/
                            // `name` is added to the end
    Span span;              // Used for placing this symbol in the output
    Span name;              // The specific identifier or value, defaults to the contents of `span` if not set
    bool name_is_value;     // If to use namespace['property'] notation
} BaselineSym;

// These are used by traversial to assemble the test case data while the test is being run. After the test has completed
// (and run_baseline_test_case() has returned) these should no longer be called.
void baseline_test_case_add_symbol(BaselineTestCase* me, BaselineSym data);
void baseline_test_case_add_type(BaselineTestCase* me, Span span, Type const* type);
void baseline_test_case_add_type_recursive(BaselineTestCase* me, Span outer_span, Span inner_span, Type const* type);
