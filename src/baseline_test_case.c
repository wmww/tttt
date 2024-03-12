#include "baseline_test_case.h"
#include "baseline_test_suite.h"
#include "baseline_test_traversal.h"
#include "subprocess.h"
#include "filesystem.h"
#include "string_helpers.h"
#include "error.h"
#include "block.h"
#include "parser.h"
#include "name_stack.h"

void LineNumberedStr_drop(LineNumberedStr* self) {
    cstr_drop(&self->str);
}

static bool compare_outputs(const char* test_name, const char* output_name, const char* expected_path, csview ours) {
    cstr expected = cstr_null;
    if (path_is_reg_file(expected_path)) {
        if (!read_entire_file(expected_path, true, &expected)) {
            return false;
        }
    }

    csview expected_sv = cstr_sv(&expected);
    if (csview_cmp(&ours, &expected_sv) == 0) {
        cstr_drop(&expected);
        return true;
    } else {
        printf("%s: %s did not match:\n", test_name, output_name);

        cstr result_display = cstr_from_fmt("tttt %s:\n", output_name);
        cstr_append_sv(&result_display, ours);
        cstr_append_sv(&result_display, c_sv("\n"));

        cstr expected_display = cstr_from_fmt("%s:\n", expected_path);
        cstr_append_sv(&expected_display, expected_sv);
        cstr_append_sv(&expected_display, c_sv("\n"));

        show_diff(cstr_sv(&result_display), cstr_sv(&expected_display));

        c_drop(cstr, &expected, &result_display, &expected_display);
        return false;
    }
}

static cstr render_lines_woven_with_file(const char* rel_path, csview file, vec_LineNumberedStr const* lines) {
    cstr ret = cstr_null;
    int line = 0;
    int index = 0;
    int vec_size = vec_LineNumberedStr_size(lines);
    cstr_append_fmt(&ret, "//// [tests/cases/%s] ////\r\n", rel_path);
    cstr_append(&ret, "\r\n");
    cstr_append_fmt(&ret, "=== %s ===\r\n", basename_of(rel_path));
    c_foreach(i, LinesSv, file) {
        cstr_append_sv(&ret, i.line);
        if (csview_size(i.line)) {
            cstr_append(&ret, "\r\n");
        }
        bool lines_added = false;
        while (index < vec_size && vec_LineNumberedStr_at(lines, index)->line <= line) {
            cstr_append_s(&ret, vec_LineNumberedStr_at(lines, index)->str);
            cstr_append(&ret, "\r\n");
            lines_added = true;
            index++;
        }
        if (lines_added) {
            cstr_append(&ret, "\r\n");
        }
        line++;
    }
    return ret;
}


bool load_test_case(const char* test_path, const char* full_path, BaselineTestCase* ret) {
    cstr raw_contents_str = cstr_null;
    cstr contents = cstr_null;

    if (!read_entire_file(full_path, true, &raw_contents_str)) {
        return false;
    }

    csview raw_contents = cstr_sv(&raw_contents_str);
    if (csview_starts_with(raw_contents, "\xEF\xBB\xBF")) {
        // Byte order mark, check only needed
        raw_contents = csview_last(raw_contents, csview_size(raw_contents) - 3);
    }
    bool has_line = false;
    c_foreach(i, LinesSv, raw_contents) {
        if (!csview_starts_with(i.line, "// @")) {
            cstr_append_sv(&contents, i.line);
            if (has_line || csview_size(i.line)) {
                cstr_append(&contents, "\r\n");
                has_line = true;
            }
        }
    }

    cstr_drop(&raw_contents_str);

    *ret = (BaselineTestCase){
        .test_path = test_path,
        .contents = contents,
        .symbols = vec_LineNumberedStr_init(),
        .types = vec_LineNumberedStr_init(),
    };
    return true;
}

void BaselineTestCase_drop(BaselineTestCase* self) {
    cstr_drop(&self->contents);
    vec_LineNumberedStr_drop(&self->symbols);
    vec_LineNumberedStr_drop(&self->types);
}

void BaselineTestCase_add_symbol(BaselineTestCase* test_case, const char* name, Span const* span) {
    const char* line_start = span_line_start(span);
    unsigned line = span_line_index(span);
    const char* span_start = span_start_ptr(span);
    // The space before the symbol is included in the span. Why? I have no idea.
    while (span_start > line_start && *(span_start - 1) == ' ') span_start--;
    unsigned col = span_start - line_start + 1;
    vec_LineNumberedStr_push(&test_case->symbols, (LineNumberedStr){
        .line = line,
        .str = cstr_from_fmt(
            ">%s : Symbol(%s, Decl(%s, %d, %d))",
            name, name,
            basename_of(test_case->test_path),
            line, col - 1
        ),
    });
}

void BaselineTestCase_add_type(BaselineTestCase* ctx, Span const* span, Type const* type) {
    unsigned line = span_line_index(span);
    cstr str = cstr_from_fmt(">%.*s : ", c_SV(span_sv(span)));
    cstr_append_type_error_msg(&str, type);
    vec_LineNumberedStr_push(&ctx->types, (LineNumberedStr){
        .line = line,
        .str = str,
    });
}

bool run_baseline_test_case(BaselineTestSuite* ctx, const char* test_name, const char* test_path) {
    bool ret = true;
    ErrorCtx errs = {0};
    Block ast = {0};
    cstr full_test_path = cstr_from_fmt("%s/cases/%s", ctx->typescript_tests_dir, test_path);
    cstr errors_path = cstr_from_fmt("%s/baselines/reference/%s.errors.txt", ctx->typescript_tests_dir, test_name);
    cstr symbols_path = cstr_from_fmt("%s/baselines/reference/%s.symbols", ctx->typescript_tests_dir, test_name);
    cstr types_path = cstr_from_fmt("%s/baselines/reference/%s.types", ctx->typescript_tests_dir, test_name);
    cstr js_path = cstr_from_fmt("%s/baselines/reference/%s.js", ctx->typescript_tests_dir, test_name);
    cstr errors_result = cstr_null;
    cstr symbols_results = cstr_null;
    cstr types_results = cstr_null;
    BaselineTestCase test_case = { 0 };



    printf("Testing %s\n", cstr_str(&full_test_path));

    if (!load_test_case(test_path, cstr_str(&full_test_path), &test_case)) {
        fprintf(stderr, "%s: Failed to load test case\n", test_name);
        goto cleanup;
    }

    ast = parse_file(cstr_str(&test_case.contents), &errs);

    NameStack names = NameStack_init();
    Block_resolve_names(&ast, &names, &errs);
    NameStack_drop(&names);

    Block_typecheck_toplevel(&ast, &errs);

    Block_compile_test_info(&ast, &test_case);

    symbols_results = render_lines_woven_with_file(test_path, cstr_sv(&test_case.contents), &test_case.symbols);
    types_results = render_lines_woven_with_file(test_path, cstr_sv(&test_case.contents), &test_case.types);

    if (!ErrorCtx_empty(&errs)) {
        ErrorCtx_append_errors_txt(&errs, &errors_result);
    }

    ret &= compare_outputs(test_name, "errors", cstr_str(&errors_path), cstr_sv(&errors_result));
    ret &= compare_outputs(test_name, "symbols", cstr_str(&symbols_path), cstr_sv(&symbols_results));
    ret &= compare_outputs(test_name, "types", cstr_str(&types_path), cstr_sv(&types_results));

cleanup:
    c_drop(cstr,
        &full_test_path,
        &errors_path,
        &symbols_path,
        &types_path,
        &js_path,
        &errors_result,
        &symbols_results,
        &types_results
    );
    BaselineTestCase_drop(&test_case);
    ErrorCtx_drop(&errs);
    Block_drop(&ast);
    return ret;
}
