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

static bool compare_outputs(
    const char* test_name,
    const char* output_name,
    const char* path_of_expected,
    csview our_result,
    bool show_diff_on_error
) {
    cstr expected = cstr_null;
    if (path_is_reg_file(path_of_expected)) {
        if (!read_entire_file(path_of_expected, false, &expected)) {
            if (show_diff_on_error) {
                printf("%s: %s could not be read\n", test_name, path_of_expected);
            }
            return false;
        }
    }

    csview expected_sv = cstr_sv(&expected);
    if (csview_cmp(&our_result, &expected_sv) == 0) {
        cstr_drop(&expected);
        return true;
    } else {
        cstr result_display = cstr_from_fmt("tttt %s:\n", output_name);
        cstr_append_sv(&result_display, our_result);
        cstr_append_sv(&result_display, c_sv("\n"));

        cstr expected_display = cstr_from_fmt("%s:\n", path_of_expected);
        cstr_append_sv(&expected_display, expected_sv);
        cstr_append_sv(&expected_display, c_sv("\n"));

        if (show_diff_on_error) {
            printf("%s: %s did not match:\n", test_name, output_name);
            run_diff_subprocess(cstr_sv(&result_display), cstr_sv(&expected_display));
        }

        c_drop(cstr, &expected, &result_display, &expected_display);
        return false;
    }
}

static const char* relative_test_path(const char* test_path) {
    const char* split = "/tests/cases/";
    while (true) {
        const char* found = strstr(test_path, split);
        if (!found) {
            break;
        }
        test_path = found + 1;
    }
    return test_path;
}

static cstr render_lines_woven_with_file(
    const char* source_path,
    csview source_contents,
    vec_LineNumberedStr const* lines
) {
    cstr ret = cstr_null;
    int line = 0;
    int index = 0;
    int vec_size = vec_LineNumberedStr_size(lines);
    const char* relative_path = relative_test_path(source_path);
    cstr_append_fmt(&ret, "//// [%s] ////\r\n", relative_path);
    cstr_append(&ret, "\r\n");
    cstr_append_fmt(&ret, "=== %s ===\r\n", basename_of(source_path));
    c_foreach(i, LinesSv, source_contents) {
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

static cstr load_test_source(const char* path) {
    cstr contents_s = cstr_null;
    cstr ret = cstr_null;

    if (!read_entire_file(path, true, &contents_s)) {
        return cstr_null;
    }

    csview contents_sv = cstr_sv(&contents_s);
    // Get rid of byte order mark
    if (csview_starts_with(contents_sv, "\xEF\xBB\xBF")) {
        contents_sv = csview_last(contents_sv, csview_size(contents_sv) - 3);
    }

    bool has_line = false;
    c_foreach(i, LinesSv, contents_sv) {
        if (!csview_starts_with(i.line, "// @")) {
            cstr_append_sv(&ret, i.line);
            if (has_line || csview_size(i.line)) {
                cstr_append(&ret, "\r\n");
                has_line = true;
            }
        }
    }

    cstr_drop(&contents_s);
    return ret;
}

void LineNumberedStr_drop(LineNumberedStr* self) {
    cstr_drop(&self->str);
}

BaselineTestCase make_baseline_test_case(BaselineTestSuite* suite, cstr name, cstr source_path) {
    return (BaselineTestCase) {
        .suite = suite,
        .name = name,
        .source_path = source_path,
        .detail = {0}
    };
}

void BaselineTestCase_drop(BaselineTestCase* self) {
    BaselineTestCase_clear_detail(self);
    cstr_drop(&self->name);
    cstr_drop(&self->source_path);
}

void BaselineTestCase_clear_detail(BaselineTestCase* self) {
    cstr_drop(&self->detail.source);
    ErrorCtx_drop(&self->detail.errs);
    vec_LineNumberedStr_drop(&self->detail.symbols);
    vec_LineNumberedStr_drop(&self->detail.types);
    self->detail = (typeof(self->detail)){ 0 };
}

void BaselineTestCase_add_symbol(BaselineTestCase* self, const char* name, Span const* span) {
    const char* line_start = span_line_start(span);
    unsigned line = span_line_index(span);
    const char* span_start = span_start_ptr(span);
    // The space before the symbol is included in the span. Why? I have no idea.
    while (span_start > line_start && *(span_start - 1) == ' ') span_start--;
    unsigned col = span_start - line_start + 1;
    vec_LineNumberedStr_push(&self->detail.symbols, (LineNumberedStr){
        .line = line,
        .str = cstr_from_fmt(
            ">%s : Symbol(%s, Decl(%s, %d, %d))",
            name, name,
            basename_of(cstr_str(&self->source_path)),
            line, col - 1
        ),
    });
}

void BaselineTestCase_add_type(BaselineTestCase* self, Span const* span, Type const* type) {
    unsigned line = span_line_index(span);
    cstr str = cstr_from_fmt(">%.*s : ", c_SV(span_sv(span)));
    cstr_append_type_error_msg(&str, type);
    vec_LineNumberedStr_push(&self->detail.types, (LineNumberedStr){
        .line = line,
        .str = str,
    });
}

bool BaselineTestCase_run(BaselineTestCase* self, bool noisy) {
    bool ret = true;
    const char* name = cstr_str(&self->name);
    const char* dir = self->suite->tests_dir;
    cstr errors_path = cstr_from_fmt("%s/baselines/reference/%s.errors.txt", dir, name);
    cstr symbols_path = cstr_from_fmt("%s/baselines/reference/%s.symbols", dir, name);
    cstr types_path = cstr_from_fmt("%s/baselines/reference/%s.types", dir, name);
    cstr js_path = cstr_from_fmt("%s/baselines/reference/%s.js", dir, name);
    cstr errors_result = cstr_null;
    cstr symbols_results = cstr_null;
    cstr types_results = cstr_null;

    // It's ok if errors file doesn't exist

    if (!path_is_reg_file(cstr_str(&symbols_path))) {
        if (noisy) {
            fprintf(stderr, CLR_ERR "%s:" CLR0 " %s does not exist\n", name, cstr_str(&symbols_path));
        }
        ret = false;
        goto cleanup;
    }

    if (!path_is_reg_file(cstr_str(&types_path))) {
        if (noisy) {
            fprintf(stderr, CLR_ERR "%s:" CLR0 " %s does not exist\n", name, cstr_str(&types_path));
        }
        ret = false;
        goto cleanup;
    }

    if (cstr_empty(&self->detail.source)) {
        if (noisy) {
            printf("Testing %s\n", cstr_str(&self->source_path));
        }
        self->detail.source = load_test_source(cstr_str(&self->source_path));
        if (cstr_empty(&self->detail.source)) {
            if (noisy) {
                fprintf(stderr, CLR_ERR "%s:" CLR0 " Failed to load test case\n", name);
            }
            ret = false;
            goto cleanup;
        }

        if (!noisy) {
            inhibit_nonfatals();
        }

        Block ast = parse_file(cstr_str(&self->detail.source), &self->detail.errs);

        NameStack names = NameStack_init();
        Block_resolve_names(&ast, &names, &self->detail.errs);
        NameStack_drop(&names);

        Block_typecheck_toplevel(&ast, &self->detail.errs);
        Block_compile_test_info(&ast, self);

        if (!noisy) {
            uninhibit_nonfatals();
        }

        Block_drop(&ast);
    }

    ErrorCtx_append_errors_txt(&self->detail.errs, &errors_result);
    symbols_results = render_lines_woven_with_file(
        cstr_str(&self->source_path),
        cstr_sv(&self->detail.source),
        &self->detail.symbols
    );
    types_results = render_lines_woven_with_file(
        cstr_str(&self->source_path),
        cstr_sv(&self->detail.source),
        &self->detail.types
    );

    ret &= compare_outputs(name, "errors", cstr_str(&errors_path), cstr_sv(&errors_result), noisy);
    ret &= compare_outputs(name, "symbols", cstr_str(&symbols_path), cstr_sv(&symbols_results), noisy);
    ret &= compare_outputs(name, "types", cstr_str(&types_path), cstr_sv(&types_results), noisy);

cleanup:
    c_drop(cstr,
        &errors_path,
        &symbols_path,
        &types_path,
        &js_path,
        &errors_result,
        &symbols_results,
        &types_results
    );
    return ret;
}
