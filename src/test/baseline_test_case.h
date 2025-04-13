#pragma once

#include "error.h"
#include "source_file.h"
#include "program.h"

typedef struct Span Span;
typedef struct Type Type;
typedef struct Program Program;
typedef struct MockFs MockFs;
typedef struct JsObject JsObject;
typedef struct BaselineTestSuite BaselineTestSuite;
typedef struct LineNumberedStr LineNumberedStr;

wrap_forward_vec(LineNumberedStr, line_numbered_str);

typedef Program* ProgramPtr;
#define i_key_ssv
#define i_val_names ProgramPtr, program_ptr
#define i_no_clone
#define i_valdrop(program) program_destroy(*program)
#include "wrap_stc/hmap.h"

typedef struct BaselineTestCache {
    ErrorCtx* errors;
    HmapStrProgramPtr libs;
} BaselineTestCache;

BaselineTestCache baseline_test_cache_init();
void baseline_test_cache_drop(BaselineTestCache* me);
Program* baseline_test_cache_get_program_with_libs(BaselineTestCache* me, csview comma_sep_libs);

// Get the name of a test given it's path (just strips the dir and extension). output is only valid as long as input is.
csview test_name_from_path(csview path);

// A virtual file within a test case
typedef struct BaselineTestFile {
    cstr path;
    SourceFile const* source;
    VecLineNumberedStr symbols;
    VecLineNumberedStr types;
    VecLineNumberedStr errors;
    intptr_t error_count;
} BaselineTestFile;

void baseline_test_file_drop(BaselineTestFile* me);

#define i_key_class_names BaselineTestFile, baseline_test_file
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct BaselineTestCase {
    csview source_path; // Not owned, path to real file on disk that may describe several virtual files
    bool did_run;
    ErrorCtx* error_ctx;
    bool underline_any;
    bool noisy;
    MockFs* fs;
    Program* program;
    cstr lib_list;
    cstr formatted_errors;
    VecBaselineTestFile files;
} BaselineTestCase;

// Never prints to stdout, only prints to stderr if noisy is true, or if it panics
BaselineTestCase run_baseline_test_case(BaselineTestCache* cache, csview source_path, bool noisy);
void baseline_test_case_drop(BaselineTestCase* me);

// These are used to get the data into string form, so it can be compared with known correct results or serialized
void baseline_test_case_render_errors(cstr* target, BaselineTestCase const* me);
void baseline_test_case_render_symbols(cstr* target, BaselineTestCase const* me);
void baseline_test_case_render_types(cstr* target, BaselineTestCase const* me);

typedef struct Annotation {
    VecSpan declarations;   // The places this symbol was declared. Empty spans are ignored. If empty or all nulls falls
                            // back to `span` unless allow_declaration_empty is set
    bool allow_declaration_empty; // if this is false and `declarations` is empty or all null the span is used as a
                            // declaration
    VecSpan namespaces;     // Includes the interface/class name if applicable. Empty spans are ignored, `span`/
                            // `name` is added to the end
    Span span;              // Used for placing this symbol in the output
    Span inner_span;        // Used when multiple layers of AST nodes need to be simulated
    Span name;              // The specific identifier or value, defaults to the contents of `span` if not set
    bool name_is_value;     // If to use namespace['property'] notation
    Type const* type;       // You can also add a type with the same span
} Annotation;

// These are used by traversial to assemble the test case data while the test is being run. After the test has completed
// (and run_baseline_test_case() has returned) these should no longer be called.
void baseline_test_case_add_symbol(BaselineTestCase* me, Annotation data);
void baseline_test_case_add_type(BaselineTestCase* me, Annotation data);
void baseline_test_case_add_both(BaselineTestCase* me, Annotation data);
void baseline_test_case_add_declaration(BaselineTestCase* me, Span span, Declaration const* declaration);
