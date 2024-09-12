#include "baseline_test_case.h"
#include "baseline_test_traversal.h"
#include "mock_fs.h"
#include "program.h"
#include "source_file.h"
#include "string_helpers.h"
#include "error.h"
#include "several_types.h"
#include "value.h"

typedef struct LineNumberedStr {
    unsigned line;
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

void baseline_test_file_drop(BaselineTestFile* me) {
    cstr_drop(&my path);
    vec_line_numbered_str_drop(&my symbols);
    vec_line_numbered_str_drop(&my types);
    vec_line_numbered_str_drop(&my errors);
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

static void add_virtual_source_file(BaselineTestCase* me, const char* virtual_path, cstr contents) {
    mock_fs_create_file(my fs, virtual_path, contents);
    vec_baseline_test_file_push(&my files, (BaselineTestFile){
        .path = cstr_from(virtual_path),
    });
}

// Returns true on success, on fail ret is set to cstr_null
static bool load_test_source(BaselineTestCase* me) {
    cstr unparsed_contents = cstr_null;
    cstr source_contents = cstr_null;

    if (!fs_read_entire_file(
        my noisy ? &real_fs_noisy : &real_fs_quiet,
        my source_path,
        &unparsed_contents
    )) {
        return false;
    }

    csview contents_sv = csview_from_range(
        file_start_excluding_bom(cstr_str(&unparsed_contents)),
        csview_end_ptr(cstr_sv(&unparsed_contents))
    );

    bool has_line = false;
    cstr current_filename = cstr_from(my source_path);
    c_foreach(i, lines_sv, contents_sv) {
        csview tag_key, tag_value;
        if (parse_tag(i.line, &tag_key, &tag_value)) {
            if (tag_equals(tag_key, "filename")) {
                if (cstr_size(&source_contents)) {
                    add_virtual_source_file(me, cstr_str(&current_filename), source_contents);
                    source_contents = cstr_null;
                }
                has_line = false;
                cstr_take(&current_filename, cstr_from_sv(tag_value));
            } else if (tag_equals(tag_key, "strict") && tag_equals(tag_value, "true")) {
                error_config_enable_strict(&my error_ctx->conf);
            } else if (tag_equals(tag_key, "noimplicitany") && tag_equals(tag_value, "true")) {
                my error_ctx->conf.no_implicit_any = true;
            }
        } else {
            cstr_append_sv(&source_contents, i.line);
            if ((has_line || csview_size(i.line)) &&
                csview_end_ptr(i.line) < csview_end_ptr(contents_sv)
            ) {
                cstr_append(&source_contents, "\r\n");
                has_line = true;
            }
        }
    }

    add_virtual_source_file(me, cstr_str(&current_filename), source_contents);
    cstr_drop(&current_filename);
    cstr_drop(&unparsed_contents);

    return true;
}

static BaselineTestFile* find_file(BaselineTestCase* me, SourceFile const* source) {
    PANIC_IF(!source, "Tests detected null span");
    c_foreach(i, vec_baseline_test_file, my files) {
        if (i.ref->source == source) {
            return i.ref;
        }
    }
    PANIC("failed to find test for source file %s", cstr_str(&source->absolute_path));
}

static void process_errors(BaselineTestCase* me) {
    if (error_ctx_empty(my error_ctx)) {
        return;
    }

    SourceFile const* current_source = nullptr;
    BaselineTestFile* current_test_file = nullptr;

    c_foreach(error, vec_error, my error_ctx->errors) {
        if (!span_valid(error.ref->span)) {
            continue;
        }

        if (error.ref->span.source != current_source) {
            current_source = error.ref->span.source;
            current_test_file = find_file(me, current_source);
        }

        ASSERT(current_test_file);

        unsigned line = span_line_index(error.ref->span);
        unsigned col = span_col_index(error.ref->span);

        LineNumberedStr item = {
            .line = line,
            .str = cstr_null,
        };

        if (span_valid(error.ref->span)) {
            for (unsigned j = 0; j < col + 4; j++) {
                cstr_append_sv(&item.str, c_sv(" "));
            }
            for (unsigned j = 0; j < error.ref->span.sv.size; j++) {
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
            unsigned related_line = span_line_index(related.ref->span);
            unsigned related_col = span_col_index(related.ref->span);
            cstr_append_fmt(
                &item.str,
                "\r\n!!! related TS%d %s:%u:%u: %s",
                related.ref->ts_code,
                basename_of(cstr_str(&current_source->absolute_path)),
                related_line + 1,
                related_col + 1,
                cstr_str(&related.ref->message)
            );
        }

        vec_line_numbered_str_push(&current_test_file->errors, item);
        current_test_file->error_count++;
    }
}

BaselineTestCase run_baseline_test_case(const char* source_path, bool noisy) {
    if (!noisy) {
        inhibit_nonfatals();
    }

    ErrorCtx* errs = c_new(ErrorCtx, error_ctx_init());
    MockFs* fs = mock_fs_new();
    Program* program = program_new(errs, mock_fs_get_fs(fs));

    BaselineTestCase ret = {
        .source_path = source_path,
        .error_ctx = errs,
        .fs = fs,
        .program = program,
        .noisy = noisy,
    };

    ret.did_run = load_test_source(&ret);

    c_foreach(i, vec_baseline_test_file, ret.files) {
        i.ref->source = program_load(program, cstr_str(&i.ref->path));
    }

    // Yes, the correct logic seems to be to only show type errors if there were no parse errors,
    // but name errors are shown reguardless
    program_typecheck(program, !errs->has_parse_errors);

    c_foreach(i, vec_baseline_test_file, ret.files) {
        // PERF: perhaps mark the file as "current" so we don't have to look it up
        block_compile_test_info(&i.ref->source->ast, &ret);
    }

    process_errors(&ret);

    if (!noisy) {
        uninhibit_nonfatals();
    }

    return ret;
}

void baseline_test_case_drop(BaselineTestCase* me) {
    program_destroy(my program);
    mock_fs_destroy(my fs);
    error_ctx_drop(my error_ctx);
    free(my error_ctx);
    vec_baseline_test_file_drop(&my files);
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
    VecLineNumberedStr const* lines,
    bool is_last_file
) {
    const char* line_indent = is_errors ? "    " : "";
    unsigned line = 0;
    unsigned index = 0;
    intptr_t vec_size = vec_line_numbered_str_size(lines);
    bool newline_if_next_line_non_empty = false;

    if (!is_errors) {
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

    intptr_t newlines_at_end_of_source = 0;
    for (const char* p = csview_end_ptr(source_without_ws); p < csview_end_ptr(source_contents); p++) {
        if (*p == '\n') newlines_at_end_of_source++;
        else if (*p != '\r') newlines_at_end_of_source = 0;
    }

    if (newlines_at_end_of_source == 1 ||
        (is_last_file && newlines_at_end_of_source > 0) ||
        (is_last_file && !is_errors && newline_if_next_line_non_empty)
    ) {
        cstr_append_fmt(target, "\r\n%s", line_indent);
    }
}

void baseline_test_case_render_errors(cstr* target, BaselineTestCase const* me) {
    if (error_ctx_empty(my error_ctx)) {
        return;
    }

    c_foreach(error, vec_error, my error_ctx->errors) {
        unsigned line = span_line_index(error.ref->span);
        unsigned col = span_col_index(error.ref->span);
        const char* basename = span_valid(error.ref->span) ?
            basename_of(cstr_str(&error.ref->span.source->absolute_path)) :
            "(nil)";

        cstr_append_fmt(
            target,
            "%s(%u,%u): error TS%d: %s\r\n",
            basename, line + 1, col + 1,
            error.ref->ts_code,
            cstr_str(&error.ref->message)
        );
    }

    c_foreach(i, vec_baseline_test_file, my files) {
        cstr_append_fmt(
            target,
            "\r\n\r\n==== %s (%ld errors) ====\r\n",
            basename_of(cstr_str(&i.ref->path)),
            i.ref->error_count
        );

        render_lines_woven_with_file(
            target,
            cstr_str(&i.ref->path),
            true,
            cstr_sv(&i.ref->source->contents),
            &i.ref->errors,
            i.ref == vec_baseline_test_file_back(&my files)
        );
    }
}

void baseline_test_case_render_symbols(cstr* target, BaselineTestCase const* me) {
    cstr_append_fmt(target, "//// [%s] ////\r\n\r\n", relative_test_path(my source_path));
    c_foreach(i, vec_baseline_test_file, my files) {
        render_lines_woven_with_file(
            target,
            cstr_str(&i.ref->path),
            false,
            cstr_sv(&i.ref->source->contents),
            &i.ref->symbols,
            i.ref == vec_baseline_test_file_back(&my files)
        );
    }
}

void baseline_test_case_render_types(cstr* target, BaselineTestCase const* me) {
    cstr_append_fmt(target, "//// [%s] ////\r\n\r\n", relative_test_path(my source_path));
    c_foreach(i, vec_baseline_test_file, my files) {
        render_lines_woven_with_file(
            target,
            cstr_str(&i.ref->path),
            false,
            cstr_sv(&i.ref->source->contents),
            &i.ref->types,
            i.ref == vec_baseline_test_file_back(&my files)
        );
    }
}

static void append_decl(cstr* target, Span span) {
    if (!span_valid(span)) {
        return;
    }
    const char* file_start = source_file_start(span.source);
    const char* start = span_start_ptr(span);
    bool keep_going = true;
    // The space before the symbol is included in the span. Why? I have no idea.
    // Here we parse code backwards, which is an extremely silly thing to do
    while (keep_going && start > file_start) {
        switch (start[-1]) {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
                start--;
                break;

            case '/':
                if (start > file_start + 1 && start[-2] == '*') {
                    keep_going = false;
                    // Skip block comments
                    const char* c = start - 1;
                    while (c > file_start) {
                        if (*(c - 1) == '/' && *c == '*') {
                            keep_going = true;
                            start = c - 1;
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
                const char* c = start - 1;
                while (c > file_start && *c != '\n') {
                    if (*(c - 1) == '/' && *c == '/') {
                        keep_going = true;
                        start = c - 1;
                        break;
                    }
                    c--;
                }
            }   break;
        }
    }
    span.sv = csview_from_n(start, 0);
    unsigned format_line = span_line_index(span);
    unsigned format_col = span_col_index(span);
    cstr_append_fmt(
        target,
        ", Decl(%s, %u, %u)",
        basename_of(cstr_str(&span.source->absolute_path)),
        format_line,
        format_col
    );
}

void baseline_test_case_add_symbol(BaselineTestCase* me, BaselineSym data) {
    unsigned line_for_placement = span_line_index(data.span);

    cstr text = cstr_from_fmt(">%.*s : Symbol(", c_SPAN(data.span));

    bool has_namespace = false;
    c_foreach(i, vec_span, data.namespaces) {
        if (span_valid(*i.ref)) {
            if (has_namespace) {
                cstr_append_lit(&text, ".");
            }
            cstr_append_span(&text, *i.ref);
            has_namespace = true;
        }
    }

    if (data.name_is_value) {
        if (has_namespace) cstr_append_lit(&text, "[");
        cstr_append_span(&text, data.span);
        if (has_namespace) cstr_append_lit(&text, "]");
    } else {
        if (has_namespace) {
            cstr_append_lit(&text, ".");
        }
        cstr_append_span(&text, span_valid(data.name) ? data.name : data.span);
    }

    bool found_decl = false;
    c_foreach(i, vec_span, data.declarations) {
        if (span_valid(*i.ref)) {
            append_decl(&text, *i.ref);
            found_decl = true;
        }
    }

    if (!found_decl && !data.allow_declaration_empty) {
        append_decl(&text, data.span);
    }

    cstr_append_lit(&text, ")");
    BaselineTestFile* test_file = find_file(me, data.span.source);
    vec_line_numbered_str_push(&test_file->symbols, (LineNumberedStr){
        .line = line_for_placement,
        .str = text,
    });
    vec_span_drop(&data.declarations);
    vec_span_drop(&data.namespaces);
}

static void append_span_contents_without_newlines(cstr* target, Span span) {
    const char* start = span_start_ptr(span);
    const char* end = span_end_ptr(span);
    for (const char* c = start; c < end; c++) {
        switch (*c) {
            case '\r': case '\n': break;
            default: cstr_push(target, c);
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
    const char* outer_start = span_start_ptr(outer_span);
    const char* outer_last = span_end_ptr(outer_span) - 1;
    const char* inner_start = span_start_ptr(inner_span);
    const char* inner_last = span_end_ptr(inner_span) - 1;

    while (outer_start < inner_start && *outer_start != '(') outer_start++;
    while (outer_last > inner_last && *outer_last != ')') outer_last--;

    bool base_case = false;
    if (outer_start == inner_start || outer_last == inner_last) {
        outer_start = inner_start;
        outer_last = inner_last;
        base_case = true;
    }
    outer_span = span_make(inner_span.source, outer_start, outer_last + 1);

    intptr_t line = span_line_index(outer_span);
    cstr str = cstr_lit(">");
    append_span_contents_without_newlines(&str, outer_span);
    cstr_append(&str, " : ");
    if (type->tag == TYPE_INFERRED) {
        // Inferred identifiers in ternaries include the infer keyword, which the tests don't like
        cstr_append_span(&str, type->inferred.get->name);
    } else {
        cstr_append_type_error_msg(&str, type);
    }
    BaselineTestFile* test_file = find_file(me, outer_span.source);
    vec_line_numbered_str_push(&test_file->types, (LineNumberedStr){
        .line = line,
        .str = str,
    });
    if (!base_case) {
        outer_span.sv.buf++;
        outer_span.sv.size -= 2;
        baseline_test_case_add_type_recursive(me, outer_span, inner_span, type);
    }
}
