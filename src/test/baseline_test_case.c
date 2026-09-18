#include "baseline_test_case.h"
#include "baseline_test_traversal.h"
#include "mock_fs.h"
#include "program.h"

#include "source_file.h"
#include "string_helpers.h"
#include "error.h"
#include "value.h"

#include <cleat/filesystem.h>

typedef struct LineNumberedStr {
    unsigned line;
    cstr str;
} LineNumberedStr;

static void line_numbered_str_drop(LineNumberedStr* me) {
    cstr_drop(&my str);
}

#define g_class_key LineNumberedStr, line_numbered_str
#define g_no_clone
#define g_declared
#include <cleat/generic/vec.h>

BaselineTestCache baseline_test_cache_init() {
    ErrorCtx* errors = c_new(ErrorCtx, error_ctx_init());
    return (BaselineTestCache){
        .errors = errors,
    };
}

void baseline_test_cache_drop(BaselineTestCache* me) {
    error_ctx_drop(my errors);
    free(my errors);
    hmap_program_ptr_drop(&my libs);
}

Program* baseline_test_cache_get_program_with_libs(BaselineTestCache* me, csview comma_sep_libs) {
    auto value = hmap_program_ptr_get(&my libs, comma_sep_libs);
    if (value) {
        return value->second;
    }
    cstr key = cstr_null;
    while (comma_sep_libs.size) {
        intptr_t end = csview_find_sv(comma_sep_libs, c_sv(","));
        csview single_lib = csview_trim_whitespace(csview_slice(comma_sep_libs, 0, end));
        if (csview_size(single_lib)) {
            if (cstr_size(&key)) {
                cstr_append_lit(&key, ",");
            }
            cstr_append_sv(&key, single_lib);
        }
        if (end == c_NPOS) break;
        comma_sep_libs = csview_slice(comma_sep_libs, end + 1, INTPTR_MAX);
    }
    cstr_lowercase(&key);
    value = hmap_program_ptr_get(&my libs, cstr_sv(&key));
    if (value) {
        cstr_drop(&key);
        return value->second;
    }
    Program* program = program_new(my errors, fs);
    comma_sep_libs = cstr_sv(&key);
    while (comma_sep_libs.size) {
        intptr_t end = csview_find_sv(comma_sep_libs, c_sv(","));
        // A standard library file we can't read means every test using it is meaningless, so don't limp on
        try (SourceFile* lib, program_load_stdlib(program, csview_slice(comma_sep_libs, 0, end), &lib)) or_panic;
        ignore lib;
        if (end == c_NPOS) break;
        comma_sep_libs = csview_slice(comma_sep_libs, end + 1, INTPTR_MAX);
    }
    program_resolve_names(program);
    hmap_program_ptr_insert(&my libs, key, program);
    return program;
}

csview test_name_from_path(csview path) {
    csview name = basename_of(path);
    check(csview_ends_with(name, ".ts"));
    name = csview_subview(name, 0, csview_size(name) - 3);
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

// Takes ownership of contents. Only records the file once there's actually something behind it in the mock
// filesystem, so a failure here can't leave us with a BaselineTestFile pointing at nothing
static_failable add_virtual_source_file(BaselineTestCase* me, csview virtual_path, cstr contents) {
    if (fs_exists(mock_fs_get_fs(my fs), virtual_path)) {
        // A handful of tsc tests repeat a // @Filename:, in which case the first copy of the file wins
        cstr_drop(&contents);
    } else {
        try (mock_fs_create_file(my fs, virtual_path, contents)) bubble;
    }
    vec_baseline_test_file_push(&my files, (BaselineTestFile){
        .path = cstr_from_sv(virtual_path),
    });
    return ok;
}

static_failable load_test_source(BaselineTestCase* me) {
    cstr unparsed_contents = cstr_null;
    cstr source_contents = cstr_null;

    try (fs_read_file(fs, my source_path, &unparsed_contents)) bubble;

    csview contents_sv = csview_from_range(
        file_start_excluding_bom(cstr_str(&unparsed_contents)),
        csview_end_ptr(cstr_sv(&unparsed_contents))
    );

    bool has_line = false;
    csview current_filename = basename_of(my source_path);
    for_each (i, lines_sv, contents_sv) {
        csview tag_key, tag_value;
        if (parse_tag(i, &tag_key, &tag_value)) {
            if (tag_equals(tag_key, "filename")) {
                if (cstr_size(&source_contents)) {
                    try (add_virtual_source_file(me, current_filename, source_contents)) cleanup {
                        cstr_drop(&unparsed_contents);
                    }
                    source_contents = cstr_null;
                }
                has_line = false;
                current_filename = tag_value;
            } else if (tag_equals(tag_key, "strict") && tag_equals(tag_value, "true")) {
                error_config_enable_strict(&my error_ctx->config);
            } else if (tag_equals(tag_key, "noimplicitany") && tag_equals(tag_value, "true")) {
                my error_ctx->config.no_implicit_any = true;
            } else if (tag_equals(tag_key, "lib")) {
                my lib_list = cstr_from_sv(tag_value);
            }
        } else {
            cstr_append_sv(&source_contents, i);
            if ((has_line || csview_size(i)) &&
                csview_end_ptr(i) < csview_end_ptr(contents_sv)
            ) {
                cstr_append(&source_contents, "\r\n");
                has_line = true;
            }
        }
    }

    try (add_virtual_source_file(me, current_filename, source_contents)) cleanup {
        cstr_drop(&unparsed_contents);
    }
    cstr_drop(&unparsed_contents);

    my did_run = true;
    return ok;
}

static BaselineTestFile* find_file(BaselineTestCase* me, SourceFile const* source) {
    panic_if(!source, "Tests detected span with null source");
    for_each (i, vec_baseline_test_file, &my files) {
        if (i->source == source) {
            return i;
        }
    }
    panic("failed to find test for source file ", source->canonical_path);
}

static void format_error_tree(
    cstr* global_target,
    cstr* file_target,
    Error* toplevel,
    Error* error,
    SourceFile const* current_source,
    int indent_level
) {
    // Add the prefix for toplevel errors, this "global" string will appear at the top of the output file
    if (indent_level == 0) {
        unsigned line = span_line_index(error->span);
        unsigned col = span_col_index(error->span);
        const char* filename = error->span.source ? (
            error->span.source->display_path_override ?
                error->span.source->display_path_override :
                cstr_str(&error->span.source->canonical_path)
        ) : "(nil)";

        cstr_append_d(
            global_target, DEFAULT_DISPLAY,
            filename, "(", line + 1, ",", col + 1, "): error TS", error->ts_code, ": "
        );
    }

    // Add toplevel errors and non-related errors (sub-errors that don't have a valid span) to the global string
    if (indent_level == 0 || !span_valid(error->span)) {
        cstr_append_d(global_target, DEFAULT_DISPLAY, repeated_d("  ", indent_level), error->message, "\r\n");
    }

    if (indent_level == 0 || !span_valid(error->span)) {
        // Handle toplevel errors and non-related errors (sub-errors that don't have a valid span)
        cstr_append_d(
            file_target, DEFAULT_DISPLAY,
            "\r\n!!! error TS", toplevel->ts_code, ": ", repeated_d("  ", indent_level), error->message
        );
    } else {
        // Handle related errors (sub-errors that do have a valid span)
        unsigned related_line = span_line_index(error->span);
        unsigned related_col = span_col_index(error->span);
        cstr_append_d(
            file_target, DEFAULT_DISPLAY,
            "\r\n!!! related TS", error->ts_code, " ",
            basename_of(cstr_sv(&current_source->canonical_path)), ":",
            related_line + 1, ":", related_col + 1, ": ", error->message
        );
    }

    // Traverse down the tree handling sub-errors and related errors
    for_each (related, vec_error, &error->sub_errors) {
        format_error_tree(global_target, file_target, toplevel, related, current_source, indent_level + 1);
    }
}

static void process_errors(BaselineTestCase* me) {
    if (error_ctx_empty(my error_ctx)) {
        return;
    }

    SourceFile const* current_source = nullptr;
    BaselineTestFile* current_test_file = nullptr;

    for_each (error, vec_error, &my error_ctx->errors) {
        if (!span_valid(error->span)) {
            continue;
        }

        if (error->span.source != current_source) {
            current_source = error->span.source;
            current_test_file = find_file(me, current_source);
        }

        check(current_test_file);

        unsigned line = span_line_index(error->span);
        unsigned col = span_col_index(error->span);

        LineNumberedStr item = {
            .line = line,
            .str = cstr_null,
        };

        if (span_valid(error->span)) {
            for (unsigned j = 0; j < col + 4; j++) {
                cstr_append_lit(&item.str, " ");
            }
            for (unsigned j = 0; j < error->span.sv.size; j++) {
                cstr_append_lit(&item.str, "~");
            }
        } else {
            cstr_append(&item.str, "<NO SPAN>");
        }

        format_error_tree(&my formatted_errors, &item.str, error, error, current_source, 0);

        vec_line_numbered_str_push(&current_test_file->errors, item);
        current_test_file->error_count++;
    }
}

BaselineTestCase run_baseline_test_case(BaselineTestCache* cache, csview source_path, bool noisy) {
    // A quiet test case shouldn't print the log_error()s the checker emits for constructs it can't handle yet
    LogLevel prev_threshold = logger_config.threshold;
    if (!noisy) {
        logger_config.threshold = LOG_LEVEL_NEVER;
    }

    ErrorCtx* errs = c_new(ErrorCtx, error_ctx_init());
    MockFs* fs = mock_fs_new();
    Program* program = program_new(errs, mock_fs_get_fs(fs));

    BaselineTestCase ret = {
        .source_path = source_path,
        .error_ctx = errs,
        .fs = fs,
        .lib_list = cstr_lit("default"),
        .program = program,
        .noisy = noisy,
    };

    // A test we can't read or unpack just doesn't run; did_run stays false and the result is reported as such
    try (load_test_source(&ret)) if_err (e) {
        if (noisy) error_base_eprintln(e);
    }

    Program* lib_program = baseline_test_cache_get_program_with_libs(cache, cstr_sv(&ret.lib_list));
    program_copy_globals_from(program, lib_program);

    for_each (i, vec_baseline_test_file, &ret.files) {
        // These were all just written into our mock filesystem, so failing to load one is a bug on our end
        try (SourceFile* source, program_load(program, cstr_sv(&i->path), &source)) or_panic;
        source->display_path_override = cstr_str(&i->path);
        i->source = source;
    }

    // Yes, the correct logic seems to be to only show type errors if there were no parse errors, but name errors are
    // shown reguardless
    bool typecheck = error_ctx_empty(errs);

    program_resolve_names(program);

    if (typecheck) {
        program_typecheck(program);
    }

    set_error_config(&errs->config);
    for_each (i, vec_baseline_test_file, &ret.files) {
        // PERF: perhaps mark the file as "current" so we don't have to look it up
        block_compile_test_info(&i->source->ast, &ret);
    }
    clear_error_config();

    process_errors(&ret);

    logger_config.threshold = prev_threshold;

    return ret;
}

void baseline_test_case_drop(BaselineTestCase* me) {
    program_destroy(my program);
    mock_fs_destroy(my fs);
    error_ctx_drop(my error_ctx);
    free(my error_ctx);
    cstr_drop(&my lib_list);
    cstr_drop(&my formatted_errors);
    vec_baseline_test_file_drop(&my files);
}

static csview relative_test_path(csview test_path) {
    const csview split = c_sv("/tests/cases/");
    while (true) {
        intptr_t found = csview_find_sv(test_path, split);
        if (found == c_NPOS) {
            break;
        }
        test_path = csview_slice(test_path, found + 1, INTPTR_MAX);
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
        cstr_append_d(target, DEFAULT_DISPLAY, "=== ", source_path, " ===\r\n");
        newline_if_next_line_non_empty = vec_size == 0;
    }

    intptr_t newlines_at_end_of_source = 0;
    const char* end_of_content_lines = csview_end_ptr(source_contents);
    while (end_of_content_lines > source_contents.buf) {
        switch (*(end_of_content_lines - 1)) {
            case '\n': newlines_at_end_of_source++; // fallthrough
            case '\r': end_of_content_lines--; break;
            default: goto found_content_end;
        }
    }
    found_content_end:

    for_each (i, lines_sv, source_contents) {
        if (i.buf < end_of_content_lines) {
            if (newline_if_next_line_non_empty && (
                    is_errors ||
                    !line_effectively_empty(i)
                )
            ) {
                cstr_append(target, "\r\n");
            }
            cstr_append(target, line_indent);
        }
        newline_if_next_line_non_empty = false;
        cstr_append_sv(target, i);
        if (!is_errors ||
            index < vec_size ||
            csview_end_ptr(i) < end_of_content_lines
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

    if (!is_errors) {
        if (newlines_at_end_of_source || (newline_if_next_line_non_empty && is_last_file)) {
            newlines_at_end_of_source = 1;
        }
    }

    if (!newline_if_next_line_non_empty && !is_last_file) {
        newlines_at_end_of_source--;
    }

    for (intptr_t i = 0; i < newlines_at_end_of_source; i++) {
        cstr_append_d(target, DEFAULT_DISPLAY, "\r\n", line_indent);
    }
}

void baseline_test_case_render_errors(cstr* target, BaselineTestCase const* me) {
    if (error_ctx_empty(my error_ctx)) {
        return;
    }

    cstr_append_s(target, my formatted_errors);
    cstr_append_lit(target, "\r\n");

    for_each (i, vec_baseline_test_file, &my files) {
        cstr_append_d(
            target, DEFAULT_DISPLAY,
            "\r\n==== ", i->path, " (", i->error_count, " errors) ====\r\n"
        );

        render_lines_woven_with_file(
            target,
            cstr_str(&i->path),
            true,
            cstr_sv(&i->source->contents),
            &i->errors,
            i == vec_baseline_test_file_back(&my files)
        );
    }
}

void baseline_test_case_render_symbols(cstr* target, BaselineTestCase const* me) {
    cstr_append_d(target, DEFAULT_DISPLAY, "//// [", relative_test_path(my source_path), "] ////\r\n\r\n");
    for_each (i, vec_baseline_test_file, &my files) {
        render_lines_woven_with_file(
            target,
            cstr_str(&i->path),
            false,
            cstr_sv(&i->source->contents),
            &i->symbols,
            i == vec_baseline_test_file_back(&my files)
        );
    }
}

void baseline_test_case_render_types(cstr* target, BaselineTestCase const* me) {
    cstr_append_d(target, DEFAULT_DISPLAY, "//// [", relative_test_path(my source_path), "] ////\r\n\r\n");
    for_each (i, vec_baseline_test_file, &my files) {
        render_lines_woven_with_file(
            target,
            cstr_str(&i->path),
            false,
            cstr_sv(&i->source->contents),
            &i->types,
            i == vec_baseline_test_file_back(&my files)
        );
    }
}

static void append_decl(cstr* target, Span span) {
    if (!span_valid(span) || !span.source) {
        return;
    }
    if (cstr_size(&span.source->decl_override)) {
        cstr_append_lit(target, ", Decl(");
        cstr_append_s(target, span.source->decl_override);
        cstr_append_lit(target, ")");
        return;
    } else if (cstr_ends_with_sv(&span.source->canonical_path, c_sv("tslib.d.ts"))) {
        cstr_append_d(
            target, DEFAULT_DISPLAY,
            ", Decl(", basename_of(cstr_sv(&span.source->canonical_path)), ", --, --)"
        );
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
    cstr_append_d(
        target, DEFAULT_DISPLAY,
        ", Decl(", basename_of(cstr_sv(&span.source->canonical_path)), ", ", format_line, ", ", format_col, ")"
    );
}

void baseline_test_case_add_symbol(BaselineTestCase* me, BaselineSym data) {
    unsigned line_for_placement = span_line_index(data.span);

    cstr text = cstr_d(">", data.span, " : Symbol(");

    bool has_namespace = false;
    for_each (i, vec_span, &data.namespaces) {
        if (span_valid(*i)) {
            if (has_namespace) {
                cstr_append_lit(&text, ".");
            }
            cstr_append_span(&text, *i);
            has_namespace = true;
        }
    }

    if (data.name_is_value) {
        if (has_namespace) cstr_append_lit(&text, "[");
        cstr_append_span(&text, span_valid(data.name) ? data.name : data.span);
        if (has_namespace) cstr_append_lit(&text, "]");
    } else {
        if (has_namespace) {
            cstr_append_lit(&text, ".");
        }
        cstr_append_span(&text, span_valid(data.name) ? data.name : data.span);
    }

    bool found_decl = false;
    for_each (i, vec_span, &data.declarations) {
        if (span_valid(*i)) {
            append_decl(&text, *i);
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
    DisplayUnderlineTarget target = {
        .target = cstr_lit(">"),
        .underline = cstr_lit(">"),
        .chars = {' ', '^'},
    };
    append_span_contents_without_newlines(&target.target, outer_span);
    cstr_resize(&target.underline, cstr_size(&target.target), ' ');
    cstr_append_lit(&target.target, " : ");
    cstr_append_lit(&target.underline, " : ");
    display_type(underline_display(&target), type);
    cstr_append_lit(&target.target, "\r\n");
    cstr_append_s(&target.target, target.underline);
    cstr_drop(&target.underline);
    BaselineTestFile* test_file = find_file(me, outer_span.source);
    vec_line_numbered_str_push(&test_file->types, (LineNumberedStr){
        .line = line,
        .str = target.target,
    });
    if (!base_case) {
        outer_span.sv.buf++;
        outer_span.sv.size -= 2;
        baseline_test_case_add_type_recursive(me, outer_span, inner_span, type);
    }
}

void baseline_test_case_add_declaration(BaselineTestCase* me, Span span, Declaration const* declaration) {
    if (!declaration) {
        log_error("baseline_test_case_add_declaration() called with null declaration");
        return;
    }
    check(span.source);
    baseline_test_case_add_symbol(me, (BaselineSym){
        .declarations = vec_span_clone(declaration->declarations),
        .span = span,
        .name = declaration->name,
    });
    if (declaration->value_tag) {
        baseline_test_case_add_type(me, span, &declaration->value_type);
    } else if (declaration->type_tag) {
        baseline_test_case_add_type(me, span, &declaration->declared_type);
    }
}
