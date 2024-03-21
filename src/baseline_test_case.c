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
#include "value.h"
#include "result.h"

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
    bool is_errors,
    csview source_contents,
    VecLineNumberedStr const* lines
) {
    cstr ret = cstr_null;
    const char* line_indent = is_errors ? "    " : "";
    int line = 0;
    int index = 0;
    int vec_size = vec_line_numbered_str_size(lines);
    const char* relative_path = relative_test_path(source_path);
    if (!is_errors) {
        cstr_append_fmt(&ret, "//// [%s] ////\r\n", relative_path);
        cstr_append(&ret, "\r\n");
        cstr_append_fmt(&ret, "=== %s ===\r\n", basename_of(source_path));
        if (!vec_size &&
            !csview_starts_with(source_contents, " ") &&
            !csview_starts_with(source_contents, "{")
        ) {
            cstr_append(&ret, "\r\n");
        }
    }
    bool last_line_from_vec = false;
    c_foreach(i, lines_sv, source_contents) {
        if (csview_size(i.line) && last_line_from_vec) {
            cstr_append(&ret, "\r\n");
        }
        last_line_from_vec = false;
        cstr_append(&ret, line_indent);
        cstr_append_sv(&ret, i.line);
        cstr_append(&ret, "\r\n");
        bool lines_added_from_vec = false;
        while (index < vec_size && vec_line_numbered_str_at(lines, index)->line <= line) {
            if (lines_added_from_vec) {
                cstr_append(&ret, "\r\n");
            }
            cstr_append_s(&ret, vec_line_numbered_str_at(lines, index)->str);
            lines_added_from_vec = true;
            index++;
            last_line_from_vec = true;
        }
        if (!is_errors && lines_added_from_vec) {
            cstr_append(&ret, "\r\n");
        }
        line++;
    }
    if (!is_errors && last_line_from_vec && !cstr_ends_with(&ret, "\r\n\r\n")) {
        cstr_append(&ret, "\r\n");
    }
    return ret;
}

static void render_errros(ErrorCtx* errs, const char* filename, cstr* header, VecLineNumberedStr* lines) {
    c_foreach(i, vec_error, errs->errors) {
        int line = span_line_index(&i.ref->span);
        int col = span_col_index(&i.ref->span);

        cstr_append_fmt(
            header,
            "%s(%d,%d): error TS%d: %s\r\n",
            filename, line + 1, col + 1,
            i.ref->ts_code,
            cstr_str(&i.ref->message)
        );

        LineNumberedStr item = {
            .line = line,
            .str = cstr_null,
        };

        if (i.ref->span.file_start) {
            for (int j = 0; j < col + 4; j++) {
                cstr_append_sv(&item.str, c_sv(" "));
            }
            for (unsigned j = 0; j < i.ref->span.length; j++) {
                cstr_append_sv(&item.str, c_sv("~"));
            }
            cstr_append(&item.str, "\r\n");
        } else {
            cstr_append(&item.str, "<NO SPAN>\r\n");
        }

        cstr_append_fmt(
            &item.str,
            "!!! error TS%d: %s",
            i.ref->ts_code,
            cstr_str(&i.ref->message)
        );

        vec_line_numbered_str_push(lines, item);
    }
}


static cstr load_test_source(const char* path, bool noisy) {
    cstr contents_s = cstr_null;
    cstr ret = cstr_null;

    if (!read_entire_file(path, noisy, &contents_s)) {
        return cstr_null;
    }

    csview contents_sv = cstr_sv(&contents_s);
    // Get rid of byte order mark
    if (csview_starts_with(contents_sv, "\xEF\xBB\xBF")) {
        contents_sv = csview_last(contents_sv, csview_size(contents_sv) - 3);
    }

    bool has_line = false;
    c_foreach(i, lines_sv, contents_sv) {
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

void line_numbered_str_drop(LineNumberedStr* me) {
    cstr_drop(&my str);
}

BaselineTestCase make_baseline_test_case(
    BaselineTestSuite* suite,
    cstr name,
    cstr source_path,
    JsObject* previous
) {
    Result errors_prev  = previous ? js_object_get(previous, c_sv("errors"))  : result_ok(value_boolean(true));
    Result symbols_prev = previous ? js_object_get(previous, c_sv("symbols")) : result_ok(value_boolean(true));
    Result types_prev   = previous ? js_object_get(previous, c_sv("types"))   : result_ok(value_boolean(true));

    BaselineTestCase ret = {
        .suite = suite,
        .name = name,
        .source_path = source_path,
        .has_run = false,
        .has_prev = previous != nullptr,
        .errors_prev = value_to_boolean(&errors_prev.value),
        .symbols_prev = value_to_boolean(&symbols_prev.value),
        .types_prev = value_to_boolean(&types_prev.value),
        .errors_passed = false,
        .symbols_passed = false,
        .types_passed = false,
        .detail = {0}
    };

    result_drop(&errors_prev);
    result_drop(&symbols_prev);
    result_drop(&types_prev);
    return ret;
}

void baseline_test_case_drop(BaselineTestCase* me) {
    baseline_test_case_clear_detail(me);
    cstr_drop(&my name);
    cstr_drop(&my source_path);
}

void baseline_test_case_clear_detail(BaselineTestCase* me) {
    cstr_drop(&my detail.source);
    error_ctx_drop(&my detail.errs);
    vec_line_numbered_str_drop(&my detail.symbols);
    vec_line_numbered_str_drop(&my detail.types);
    my detail = (typeof(my detail)){ 0 };
}

void baseline_test_case_add_symbol(BaselineTestCase* me, const char* name, Span const* span, bool decl) {
    unsigned line_for_placement = span_line_index(span);
    cstr text = cstr_from_fmt(">%s : Symbol(%s", name, name);
    if (decl) {
        Span format_span = *span;
        bool keep_going = true;
        // The space before the symbol is included in the span. Why? I have no idea.
        while (keep_going && format_span.offset > 0) {
            switch (format_span.file_start[format_span.offset - 1]) {
                case ' ':
                case '\t':
                case '\n':
                case '\r':
                    format_span.offset--;
                    break;

                default:
                    keep_going = false;
                    break;
            }
        }
        int format_line = span_line_index(&format_span);
        int format_col = span_col_index(&format_span);
        cstr_append_fmt(&text, ", Decl(%s, %d, %d)", basename_of(cstr_str(&my source_path)), format_line, format_col);
    }
    cstr_append(&text, ")");
    vec_line_numbered_str_push(&my detail.symbols, (LineNumberedStr){
        .line = line_for_placement,
        .str = text,
    });
}

static void append_span_contents_without_newlines(cstr* target, Span const* span) {
    csview sv = span_sv(span);
    for (intptr_t i = 0; i < sv.size; i++) {
        if (sv.buf[i] != '\r' && sv.buf[i] != '\n') {
            cstr_push(target, &sv.buf[i]);
        }
    }
}

void baseline_test_case_add_type(BaselineTestCase* me, Span const* span, int paren_depth, Type const* type) {
    unsigned line = span_line_index(span);
    cstr str = cstr_lit(">");
    append_span_contents_without_newlines(&str, span);
    cstr_append(&str, " : ");
    cstr_append_type_error_msg(&str, type);
    vec_line_numbered_str_push(&my detail.types, (LineNumberedStr){
        .line = line,
        .str = str,
    });
    if (paren_depth) {
        const char* start = span_start_ptr(span);
        const char* end = span_end_ptr(span);
        if (start < end && *start == '(' && *(end - 1) == ')') {
            start++;
            end--;
            // If this gets too annoying (ex if we need comments) we can just make the parser emit nested AST nodes
            while (start < end && (*start == ' ' || *start == '\n' || *start == '\r')) start--;
            while (start < end && (*end == ' ' || *end == '\n' || *end == '\r')) end--;
            Span subspan = span_make(span->file_start, start - span->file_start, end - start);
            baseline_test_case_add_type(me, &subspan, paren_depth - 1, type);
        }
    }
}

void baseline_test_case_run(BaselineTestCase* me, bool noisy) {
    const char* name = cstr_str(&my name);
    const char* dir = my suite->tests_dir;
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
        goto cleanup;
    }

    if (!path_is_reg_file(cstr_str(&types_path))) {
        if (noisy) {
            fprintf(stderr, CLR_ERR "%s:" CLR0 " %s does not exist\n", name, cstr_str(&types_path));
        }
        goto cleanup;
    }

    if (cstr_empty(&my detail.source)) {
        if (noisy) {
            printf("Testing %s\n", cstr_str(&my source_path));
        }
        my detail.source = load_test_source(cstr_str(&my source_path), noisy);
        if (cstr_empty(&my detail.source)) {
            if (noisy) {
                fprintf(stderr, CLR_ERR "%s:" CLR0 " Failed to load test case\n", name);
            }
            goto cleanup;
        }

        if (!noisy) {
            inhibit_nonfatals();
        }

        Block ast = parse_file(cstr_str(&my detail.source), &my detail.errs);

        NameStack names = name_stack_init();
        block_resolve_names(&ast, &names, &my detail.errs);
        name_stack_drop(&names);

        block_typecheck_toplevel(&ast, &my detail.errs);
        block_compile_test_info(&ast, me);

        if (!noisy) {
            uninhibit_nonfatals();
        }

        block_drop(&ast);
    }

    errors_result = cstr_null;
    if (!error_ctx_empty(&my detail.errs)) {
        const char* source_basename = basename_of(cstr_str(&my source_path));
        VecLineNumberedStr error_lines = vec_line_numbered_str_init();
        render_errros(&my detail.errs, source_basename, &errors_result, &error_lines);
        cstr_append_fmt(
            &errors_result,
            "\r\n\r\n==== %s (%ld errors) ====\r\n",
            source_basename,
            vec_error_size(&my detail.errs.errors)
        );
        cstr errors_body = render_lines_woven_with_file(
            cstr_str(&my source_path),
            true,
            cstr_sv(&my detail.source),
            &error_lines
        );
        cstr_append_s(&errors_result, errors_body);
        cstr_drop(&errors_body);
        vec_line_numbered_str_drop(&error_lines);
    }

    symbols_results = render_lines_woven_with_file(
        cstr_str(&my source_path),
        false,
        cstr_sv(&my detail.source),
        &my detail.symbols
    );

    types_results = render_lines_woven_with_file(
        cstr_str(&my source_path),
        false,
        cstr_sv(&my detail.source),
        &my detail.types
    );

    my errors_passed = compare_outputs(
        name,
        "errors",
        cstr_str(&errors_path),
        cstr_sv(&errors_result),
        noisy
    );

    my symbols_passed = compare_outputs(
        name,
        "symbols",
        cstr_str(&symbols_path),
        cstr_sv(&symbols_results),
        noisy
    );

    my types_passed = compare_outputs(
        name,
        "types",
        cstr_str(&types_path),
        cstr_sv(&types_results),
        noisy
    );

    my has_run = true;

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
}

bool baseline_test_case_is_pass(BaselineTestCase const* me) {
    return my errors_passed && my symbols_passed && my types_passed;
}

bool baseline_test_case_was_pass(BaselineTestCase const* me) {
    return my errors_prev && my symbols_prev && my types_prev;
}

bool baseline_test_case_has_new_pass(BaselineTestCase const* me) {
    return (my errors_passed  && (!my has_prev || !my errors_prev )) ||
           (my symbols_passed && (!my has_prev || !my symbols_prev)) ||
           (my types_passed   && (!my has_prev || !my types_prev  ));
}

bool baseline_test_case_has_new_fail(BaselineTestCase const* me) {
    return (!my errors_passed  && my errors_prev ) ||
           (!my symbols_passed && my symbols_prev) ||
           (!my types_passed   && my types_prev  );
}
