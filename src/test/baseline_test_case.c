#include "baseline_test_case.h"
#include "baseline_test_suite.h"
#include "baseline_test_traversal.h"
#include "subprocess.h"
#include "filesystem.h"
#include "string_helpers.h"
#include "error.h"
#include "block.h"
#include "identifier_type.h"
#include "parser.h"
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

// Line is treated as if it's empty if it has only whitespace, or whitespace plus a single open or close brace
bool line_effectively_empty(csview line) {
    line = csview_trim_whitespace(line);
    return csview_size(line) == 0 || (
        csview_size(line) == 1 && (
            *line.buf == '{' ||
            *line.buf == '}'
        )
    );
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
    bool newline_if_next_line_non_empty = false;

    if (!is_errors) {
        cstr_append_fmt(&ret, "//// [%s] ////\r\n", relative_path);
        cstr_append(&ret, "\r\n");
        cstr_append_fmt(&ret, "=== %s ===\r\n", basename_of(source_path));
        newline_if_next_line_non_empty = vec_size == 0;
    }

    csview source_without_ws = csview_trim_whitespace(source_contents);

    c_foreach(i, lines_sv, source_contents) {
        csview line_without_ws = csview_trim_whitespace(i.line);
        if (newline_if_next_line_non_empty && (
                is_errors ||
                !line_effectively_empty(line_without_ws)
            )
        ) {
            cstr_append(&ret, "\r\n");
        }
        newline_if_next_line_non_empty = false;
        cstr_append(&ret, line_indent);
        cstr_append_sv(&ret, i.line);
        if (!is_errors ||
            index < vec_size ||
            csview_end_ptr(line_without_ws) < csview_end_ptr(source_without_ws)
        ) {
            cstr_append(&ret, "\r\n");
        }
        bool lines_added_from_vec = false;
        while (index < vec_size && vec_line_numbered_str_at(lines, index)->line <= line) {
            if (lines_added_from_vec) {
                cstr_append(&ret, "\r\n");
            }
            cstr_append_s(&ret, vec_line_numbered_str_at(lines, index)->str);
            lines_added_from_vec = true;
            index++;
            newline_if_next_line_non_empty = true;
        }
        if (!is_errors && lines_added_from_vec) {
            cstr_append(&ret, "\r\n");
        }
        line++;
    }

    if (csview_ends_with(source_contents, "\n")) {
        cstr_append_fmt(&ret, "\r\n%s", line_indent);
    }

    if (!is_errors && newline_if_next_line_non_empty && !cstr_ends_with(&ret, "\r\n\r\n")) {
        cstr_append(&ret, "\r\n");
    }

    return ret;
}

static void render_errors(ErrorCtx* errs, const char* filename, cstr* header, VecLineNumberedStr* lines) {
    c_foreach(error, vec_error, errs->errors) {
        int line = span_line_index(&error.ref->span);
        int col = span_col_index(&error.ref->span);

        cstr_append_fmt(
            header,
            "%s(%d,%d): error TS%d: %s\r\n",
            filename, line + 1, col + 1,
            error.ref->ts_code,
            cstr_str(&error.ref->message)
        );

        LineNumberedStr item = {
            .line = line,
            .str = cstr_null,
        };

        if (span_valid(&error.ref->span)) {
            for (int j = 0; j < col + 4; j++) {
                cstr_append_sv(&item.str, c_sv(" "));
            }
            for (unsigned j = 0; j < error.ref->span.length; j++) {
                cstr_append_sv(&item.str, c_sv("~"));
            }
            cstr_append(&item.str, "\r\n");
        } else {
            cstr_append(&item.str, "<NO SPAN>\r\n");
        }

        cstr_append_fmt(
            &item.str,
            "!!! error TS%d: %s",
            error.ref->ts_code,
            cstr_str(&error.ref->message)
        );

        c_foreach(related, vec_error, error.ref->related_errors) {
            int related_line = span_line_index(&related.ref->span);
            int related_col = span_col_index(&related.ref->span);
            cstr_append_fmt(
                &item.str,
                "\r\n!!! related TS%d %s:%d:%d: %s",
                related.ref->ts_code,
                filename,
                related_line + 1,
                related_col + 1,
                cstr_str(&related.ref->message)
            );
        }

        vec_line_numbered_str_push(lines, item);
    }
}

static bool parse_tag(csview line, csview* key, csview* value) {
    line = csview_trim_whitespace(line);
    if (!csview_starts_with(line, "//")) {
        return false;
    }
    const char* p = line.buf + 2;
    const char* end = csview_end_ptr(line);
    while (p < end && *p == ' ') p++;
    if (*p != '@') {
        return false;
    }
    const char* key_start = p + 1;
    while (p < end && *p != ':') p++;
    if (p < end) {
        *key = csview_trim_whitespace(csview_from_n(key_start, p - key_start));
        p++;
        *value = csview_trim_whitespace(csview_from_n(p, end - p));
        return true;
    } else {
        return false;
    }
}

static bool tag_equals(csview tag, const char* str) {
    for (const char* p = tag.buf; p < csview_end_ptr(tag); p++) {
        if (*p != *str && *p != *str - 'a' + 'A') {
            return false;
        }
        str++;
    }
    return true;
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
    bool has_filename = false;
    c_foreach(i, lines_sv, contents_sv) {
        csview tag_key, tag_value;
        if (parse_tag(i.line, &tag_key, &tag_value)) {
            if (tag_equals(tag_key, "filename")) {
                if (has_filename) {
                    // We do not yet support multi-file tests
                    cstr_drop(&contents_s);
                    cstr_drop(&ret);
                    return cstr_null;
                } else {
                    has_filename = true;
                }
            }
        } else {
            cstr_append_sv(&ret, i.line);
            if ((has_line || csview_size(i.line)) &&
                i.line.buf + i.line.size < contents_sv.buf + contents_sv.size
            ) {
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

void baseline_test_case_add_symbol(BaselineTestCase* me, csview name, Span span, Span decl_span) {
    baseline_test_case_add_symbol_with_scope(me, c_sv(""), name, span, false, decl_span);
}

void baseline_test_case_add_symbol_with_scope(
    BaselineTestCase* me,
    csview scope,
    csview name,
    Span span,
    bool name_is_value,
    Span decl_span
) {
    unsigned line_for_placement = span_line_index(&span);
    cstr text = cstr_from_fmt(">%.*s : Symbol(", c_SV(span_sv(&span)));
    if (csview_empty(scope)) {
        cstr_append_sv(&text, name);
    } else if (name_is_value) {
        cstr_append_sv(&text, scope);
        cstr_append_sv(&text, c_sv("["));
        cstr_append_sv(&text, span_sv(&span));
        cstr_append_sv(&text, c_sv("]"));
    } else {
        cstr_append_sv(&text, scope);
        cstr_append_sv(&text, c_sv("."));
        cstr_append_sv(&text, name);
    }

    if (span_valid(&decl_span)) {
        bool keep_going = true;
        // The space before the symbol is included in the span. Why? I have no idea.
        while (keep_going && decl_span.offset > 0) {
            switch (decl_span.file_start[decl_span.offset - 1]) {
                case ' ':
                case '\t':
                case '\n':
                case '\r':
                    decl_span.offset--;
                    break;

                case '/':
                    if (decl_span.offset > 1 && decl_span.file_start[decl_span.offset - 2] == '*') {
                        keep_going = false;
                        // Skip block comments, going backwards
                        const char* c = decl_span.file_start + decl_span.offset - 1;
                        while (c > decl_span.file_start) {
                            if (*(c - 1) == '/' && *c == '*') {
                                keep_going = true;
                                decl_span.offset = c - 1 - decl_span.file_start;
                                break;
                            }
                            c--;
                        }
                        if (keep_going) {
                            break;
                        }
                    }
                    // fallthrough

                default: {
                    keep_going = false;
                    // Skip line comments, going backwards
                    const char* c = decl_span.file_start + decl_span.offset - 1;
                    while (c > decl_span.file_start && *c != '\n') {
                        if (*(c - 1) == '/' && *c == '/') {
                            keep_going = true;
                            decl_span.offset = c - 1 - decl_span.file_start;
                            break;
                        }
                        c--;
                    }
                }   break;
            }
        }
        int format_line = span_line_index(&decl_span);
        int format_col = span_col_index(&decl_span);
        cstr_append_fmt(&text, ", Decl(%s, %d, %d)", basename_of(cstr_str(&my source_path)), format_line, format_col);
    }
    cstr_append(&text, ")");
    vec_line_numbered_str_push(&my detail.symbols, (LineNumberedStr){
        .line = line_for_placement,
        .str = text,
    });
}

static void append_span_contents_without_newlines(cstr* target, Span span) {
    csview sv = span_sv(&span);
    for (intptr_t i = 0; i < sv.size; i++) {
        if (sv.buf[i] != '\r' && sv.buf[i] != '\n') {
            cstr_push(target, &sv.buf[i]);
        }
    }
}

void baseline_test_case_add_type(BaselineTestCase* me, Span span, Type const* type) {
    baseline_test_case_add_type_recursive(me, span, span, type);
}

void baseline_test_case_add_type_recursive(
    BaselineTestCase* me,
    Span outer_span,
    Span inner_span,
    Type const* type
) {
    const char* outer_start = span_start_ptr(&outer_span);
    const char* outer_last = span_end_ptr(&outer_span) - 1;
    const char* inner_start = span_start_ptr(&inner_span);
    const char* inner_last = span_end_ptr(&inner_span) - 1;

    while (outer_start < inner_start && *outer_start != '(') outer_start++;
    while (outer_last > inner_last && *outer_last != ')') outer_last--;

    bool base_case = false;
    if (outer_start == inner_start || outer_last == inner_last) {
        outer_start = inner_start;
        outer_last = inner_last;
        base_case = true;
    }
    outer_span = span_make(inner_span.file_start, outer_start - inner_span.file_start, outer_last + 1 - outer_start );

    unsigned line = span_line_index(&outer_span);
    cstr str = cstr_lit(">");
    append_span_contents_without_newlines(&str, outer_span);
    cstr_append(&str, " : ");
    if (type->tag == TYPE_IDENTIFIER && type->identifier->inferred) {
        // Do identifier types upport cstr_append_type_error_msg()? Yes, but inferred identifiers in ternaries include
        // the inferred keyword, which the tests don't like.
        cstr_append_span(&str, &type->identifier->name);
    } else {
        cstr_append_type_error_msg(&str, type);
    }
    vec_line_numbered_str_push(&my detail.types, (LineNumberedStr){
        .line = line,
        .str = str,
    });
    if (!base_case) {
        outer_span.offset++;
        outer_span.length -= 2;
        baseline_test_case_add_type_recursive(me, outer_span, inner_span, type);
    }
}

void baseline_test_case_run(BaselineTestCase* me, bool show_errs, bool show_syms, bool show_typs) {
    bool noisy = show_errs || show_syms || show_typs;
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

        ErrorCtx null_error_ctx = error_ctx_init();

        Block ast = parse_file(cstr_str(&my detail.source), &my detail.errs);
        // Yes, the correct logic seems to be to only show type errors if there were no parse errors,
        // but name errors are shown reguardless
        ErrorCtx* errs = error_ctx_empty(&my detail.errs) ? &my detail.errs : &null_error_ctx;
        block_resolve_names_toplevel(&ast, &my detail.errs);
        block_typecheck_toplevel(&ast, errs);
        block_compile_test_info(&ast, me);

        error_ctx_drop(&null_error_ctx);

        if (!noisy) {
            uninhibit_nonfatals();
        }

        block_drop(&ast);
    }

    errors_result = cstr_null;
    if (!error_ctx_empty(&my detail.errs)) {
        const char* source_basename = basename_of(cstr_str(&my source_path));
        VecLineNumberedStr error_lines = vec_line_numbered_str_init();
        render_errors(&my detail.errs, source_basename, &errors_result, &error_lines);
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
        show_errs
    );

    my symbols_passed = compare_outputs(
        name,
        "symbols",
        cstr_str(&symbols_path),
        cstr_sv(&symbols_results),
        show_syms
    );

    my types_passed = compare_outputs(
        name,
        "types",
        cstr_str(&types_path),
        cstr_sv(&types_results),
        show_typs
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
