#include "baseline_test_case.h"
#include "baseline_test_traversal.h"
#include "filesystem.h"
#include "string_helpers.h"
#include "error.h"
#include "block.h"
#include "several_types.h"
#include "parser.h"
#include "value.h"

typedef struct LineNumberedStr {
    int line;
    cstr str;
} LineNumberedStr;

static void line_numbered_str_drop(LineNumberedStr* me) {
    cstr_drop(&my str);
}

#define i_key_class_names LineNumberedStr, line_numbered_str
#define i_no_clone
#define i_is_forward
#include "wrap_stc/vec.h"

csview test_name_from_path(const char* path) {
    csview name = csview_from(basename_of(path));
    ASSERT(csview_ends_with(name, ".ts"));
    name = csview_substr(name, 0, csview_size(name) - 3);
    return name;
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

// Returns true on success, on fail ret is set to cstr_null
static bool load_test_source(BaselineTestCase* test, bool noisy, cstr* ret) {
    cstr unparsed_contents = cstr_null;
    *ret = cstr_null;

    if (!read_entire_file(test->source_path, noisy, &unparsed_contents)) {
        return false;
    }

    csview contents_sv = cstr_sv(&unparsed_contents);
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
                    cstr_drop(&unparsed_contents);
                    cstr_take(ret, cstr_null);
                    return false;
                } else {
                    has_filename = true;
                }
            } else if (tag_equals(tag_key, "strict") && tag_equals(tag_value, "true")) {
                error_config_enable_strict(&test->error_ctx.conf);
            } else if (tag_equals(tag_key, "noimplicitany") && tag_equals(tag_value, "true")) {
                test->error_ctx.conf.no_implicit_any = true;
            }
        } else {
            cstr_append_sv(ret, i.line);
            if ((has_line || csview_size(i.line)) &&
                i.line.buf + i.line.size < contents_sv.buf + contents_sv.size
            ) {
                cstr_append(ret, "\r\n");
                has_line = true;
            }
        }
    }

    cstr_drop(&unparsed_contents);
    return true;
}

BaselineTestCase run_baseline_test_case(const char* source_path, bool noisy) {
    if (!noisy) {
        inhibit_nonfatals();
    }

    BaselineTestCase ret = {
        .source_path = source_path,
    };

    cstr source_cstr;
    if (!load_test_source(&ret, noisy, &source_cstr)) {
        return ret;
    }
    intptr_t source_size = cstr_size(&source_cstr);
    ret.owned_source = cstr_into_owned_ptr(source_cstr);
    ret.source_view = (czview){.str = ret.owned_source, .size = source_size};

    Block ast = parse_file(ret.owned_source, &ret.error_ctx);
    // Yes, the correct logic seems to be to only show type errors if there were no parse errors,
    // but name errors are shown reguardless
    bool show_type_errors = error_ctx_empty(&ret.error_ctx);
    block_resolve_names_toplevel(&ast, &ret.error_ctx);
    block_typecheck_toplevel(&ast, show_type_errors ? &ret.error_ctx : nullptr);
    block_compile_test_info(&ast, &ret);
    block_drop(&ast);

    ret.did_run = true;

    if (!noisy) {
        uninhibit_nonfatals();
    }

    return ret;
}

void baseline_test_case_drop(BaselineTestCase* me) {
    free(my owned_source);
    error_ctx_drop(&my error_ctx);
    vec_line_numbered_str_drop(&my symbols);
    vec_line_numbered_str_drop(&my types);
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
static bool line_effectively_empty(csview line) {
    line = csview_trim_whitespace(line);
    return csview_size(line) == 0 || (
        csview_size(line) == 1 && (
            *line.buf == '{' ||
            *line.buf == '}'
        )
    );
}

static void render_lines_woven_with_file(
    cstr* target,
    const char* source_path,
    bool is_errors,
    csview source_contents,
    VecLineNumberedStr const* lines
) {
    const char* line_indent = is_errors ? "    " : "";
    int line = 0;
    int index = 0;
    int vec_size = vec_line_numbered_str_size(lines);
    const char* relative_path = relative_test_path(source_path);
    bool newline_if_next_line_non_empty = false;

    if (!is_errors) {
        cstr_append_fmt(target, "//// [%s] ////\r\n", relative_path);
        cstr_append(target, "\r\n");
        cstr_append_fmt(target, "=== %s ===\r\n", basename_of(source_path));
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
            cstr_append(target, "\r\n");
        }
        newline_if_next_line_non_empty = false;
        cstr_append(target, line_indent);
        cstr_append_sv(target, i.line);
        if (!is_errors ||
            index < vec_size ||
            csview_end_ptr(line_without_ws) < csview_end_ptr(source_without_ws)
        ) {
            cstr_append(target, "\r\n");
        }
        bool lines_added_from_vec = false;
        while (index < vec_size && vec_line_numbered_str_at(lines, index)->line <= line) {
            if (lines_added_from_vec) {
                cstr_append(target, "\r\n");
            }
            cstr_append_s(target, vec_line_numbered_str_at(lines, index)->str);
            lines_added_from_vec = true;
            index++;
            newline_if_next_line_non_empty = true;
        }
        if (!is_errors && lines_added_from_vec) {
            cstr_append(target, "\r\n");
        }
        line++;
    }

    if (csview_ends_with(source_contents, "\n")) {
        cstr_append_fmt(target, "\r\n%s", line_indent);
    }

    if (!is_errors && newline_if_next_line_non_empty && !cstr_ends_with(target, "\r\n\r\n")) {
        cstr_append(target, "\r\n");
    }
}

void baseline_test_case_render_errors(cstr* target, BaselineTestCase const* me) {
    if (error_ctx_empty(&my error_ctx)) {
        return;
    }

    const char* source_basename = basename_of(my source_path);
    VecLineNumberedStr error_lines = vec_line_numbered_str_init();

    c_foreach(error, vec_error, my error_ctx.errors) {
        int line = span_line_index(&error.ref->span);
        int col = span_col_index(&error.ref->span);

        cstr_append_fmt(
            target,
            "%s(%d,%d): error TS%d: %s\r\n",
            source_basename, line + 1, col + 1,
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
                source_basename,
                related_line + 1,
                related_col + 1,
                cstr_str(&related.ref->message)
            );
        }

        vec_line_numbered_str_push(&error_lines, item);
    }

    cstr_append_fmt(
        target,
        "\r\n\r\n==== %s (%ld errors) ====\r\n",
        source_basename,
        vec_error_size(&my error_ctx.errors)
    );
    render_lines_woven_with_file(
        target,
        my source_path,
        true,
        czview_sv(my source_view),
        &error_lines
    );
    vec_line_numbered_str_drop(&error_lines);
}

void baseline_test_case_render_symbols(cstr* target, BaselineTestCase const* me) {
    render_lines_woven_with_file(target, my source_path, false, czview_sv(my source_view), &my symbols);
}

void baseline_test_case_render_types(cstr* target, BaselineTestCase const* me) {
    render_lines_woven_with_file(target, my source_path, false, czview_sv(my source_view), &my types);
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
        cstr_append_fmt(&text, ", Decl(%s, %d, %d)", basename_of(my source_path), format_line, format_col);
    }
    cstr_append(&text, ")");
    vec_line_numbered_str_push(&my symbols, (LineNumberedStr){
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
    vec_line_numbered_str_push(&my types, (LineNumberedStr){
        .line = line,
        .str = str,
    });
    if (!base_case) {
        outer_span.offset++;
        outer_span.length -= 2;
        baseline_test_case_add_type_recursive(me, outer_span, inner_span, type);
    }
}
