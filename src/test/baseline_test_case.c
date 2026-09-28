#include "baseline_test_case.h"
#include "baseline_test_traversal.h"
#include <strings.h>
#include "mock_fs.h"
#include "../tsconfig.h"
#include "program.h"

#include "source_file.h"
#include "string_helpers.h"
#include "error.h"
#include "value.h"
#include "ty/ty.h"
#include "several_types.h"

#include <cleat/filesystem.h>
#include "binding.h"
#include "scope.h"

typedef struct LineNumberedStr {
    unsigned line;
    cstr str;
    // A types line of `any`: where its underline starts. tsc's harness leaves it out when the test has no errors (a
    // TypeWriterWalker without hadErrorBaseline prints the intrinsic name instead of a type node). 0: always kept
    isize_t any_underline_at;
    // ... and it's the error type: then it prints its intrinsic name, `error`
    bool error_type;
} LineNumberedStr;

static void line_numbered_str_drop(LineNumberedStr* me) {
    cstr_drop(&my str);
}

// A types line of `any` as tsc's harness prints it without errors: the intrinsic name, no underline
static void append_without_any_underline(cstr* target, LineNumberedStr const* line) {
    if (line->error_type) {
        cstr_append_sv(target, csview_from_n(cstr_str(&line->str), line->any_underline_at - 3));
        cstr_append_lit(target, "error");
    } else {
        cstr_append_sv(target, csview_from_n(cstr_str(&line->str), line->any_underline_at));
    }
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
        // A test can ask for a library that doesn't exist (tsc reports TS6046 and carries on without it). The
        // default libraries do exist, so a test can't end up without its globals because of this
        try (SourceFile* lib, program_load_stdlib(program, csview_slice(comma_sep_libs, 0, end), &lib)) or_log(info);
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
    isize extension = csview_ends_with(name, ".tsx") ? 4 : 3;
    check(csview_ends_with(name, ".ts") || extension == 4);
    name = csview_subview(name, 0, csview_size(name) - extension);
    return name;
}

void baseline_test_file_drop(BaselineTestFile* me) {
    cstr_drop(&my path);
    cstr_drop(&my raw_contents);
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
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (*p != '@') {
        return false;
    }
    // tsc's harness: /^\/{2}\s*@(\w+)\s*:\s*([^\r\n]*)/, so `// @ts-ignore: why` is no option
    const char* key_start = p + 1;
    p++;
    while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p < end && *p == ':') {
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
    return *str == 0;
}

typedef struct Variation {
    csview key;
    VecCStr values;
} Variation;

static void variation_drop(Variation* me) {
    vec_cstr_drop(&my values);
}

#define g_class_key Variation, variation
#define g_no_clone
#include <cleat/generic/vec.h>

static bool sv_equals_ignore_case(csview a, csview b) {
    if (a.size != b.size) return false;
    for (isize i = 0; i < a.size; i++) {
        if (tolower((unsigned char)a.buf[i]) != tolower((unsigned char)b.buf[i])) return false;
    }
    return true;
}

// The id of a value, so that es6 and es2015 count as the same thing. Unknown values are only equal to themselves
static int value_id(csview value, const char* const* known, int const* known_ids, isize known_count, int fallback) {
    for (isize k = 0; k < known_count; k++) {
        if (csview_equals(value, known[k])) return known_ids[k];
    }
    return fallback;
}

// tsc's splitVaryBySettingValue(): the values an option varies over, empty if it's a plain setting
static VecCStr split_vary_by_value(csview key, csview text) {
    enum { MAX_VALUES = 32 };
    const char* known[MAX_VALUES];
    int known_ids[MAX_VALUES];
    isize known_count = compiler_option_values(key, known, known_ids, MAX_VALUES);
    VecCStr includes = {0};
    VecCStr excludes = {0};
    VecCStr ret = {0};
    bool star = false;
    while (text.size) {
        isize end = csview_find_sv(text, c_sv(","));
        cstr value = cstr_from_sv(csview_trim_whitespace(csview_slice(text, 0, end)));
        cstr_lowercase(&value);
        if (!cstr_size(&value)) {
            cstr_drop(&value);
        } else if (cstr_equals(&value, "*")) {
            star = true;
            cstr_drop(&value);
        } else if (*cstr_str(&value) == '-' || *cstr_str(&value) == '!') {
            cstr_erase(&value, 0, 1);
            vec_cstr_push(&excludes, value);
        } else {
            vec_cstr_push(&includes, value);
        }
        if (end == c_NPOS) break;
        text = csview_slice(text, end + 1, INTPTR_MAX);
    }

    if (vec_cstr_size(&includes) > 1 || star || vec_cstr_size(&excludes)) {
        enum { MAX_VARIATIONS = 64 };
        int ids[MAX_VARIATIONS];
        int unknown_id = -1;
        for_each (i, vec_cstr, &includes) {
            int id = value_id(cstr_sv(i), known, known_ids, known_count, unknown_id--);
            bool seen = false;
            for_each (j, vec_cstr, &ret) {
                if (cstr_eq(i, j) || ids[j_index] == id) seen = true;
            }
            if (!seen && vec_cstr_size(&ret) < MAX_VARIATIONS) {
                ids[vec_cstr_size(&ret)] = id;
                vec_cstr_push(&ret, cstr_clone(*i));
            }
        }
        if (star) {
            for (isize k = 0; k < known_count; k++) {
                bool seen = false;
                for_each (j, vec_cstr, &ret) {
                    if (cstr_equals(j, known[k]) || ids[j_index] == known_ids[k]) seen = true;
                }
                if (!seen && vec_cstr_size(&ret) < MAX_VARIATIONS) {
                    ids[vec_cstr_size(&ret)] = known_ids[k];
                    vec_cstr_push(&ret, cstr_from(known[k]));
                }
            }
        }
        for_each (i, vec_cstr, &excludes) {
            int id = value_id(cstr_sv(i), known, known_ids, known_count, -1000000);
            for (isize j = vec_cstr_size(&ret) - 1; j >= 0; j--) {
                if (cstr_eq(i, vec_cstr_at(&ret, j)) || ids[j] == id) {
                    vec_cstr_erase_n(&ret, j, 1);
                    memmove(&ids[j], &ids[j + 1], sizeof(int) * (size_t)(vec_cstr_size(&ret) - j));
                }
            }
        }
    }
    vec_cstr_drop(&includes);
    vec_cstr_drop(&excludes);
    return ret;
}

static void push_configurations(VecVariation const* variations, isize offset, cstr const* prefix, VecCStr* out) {
    if (offset >= vec_variation_size(variations)) {
        vec_cstr_push(out, cstr_clone(*prefix));
        return;
    }
    Variation const* variation = vec_variation_at(variations, offset);
    for_each (i, vec_cstr, &variation->values) {
        cstr next = cstr_d(*prefix, cstr_size(prefix) ? "," : "", variation->key, "=", *i);
        // Keys are compared case insensitively, but tsc names the baseline after the lowercased one
        cstr_lowercase(&next);
        push_configurations(variations, offset + 1, &next, out);
        cstr_drop(&next);
    }
}

void baseline_test_configurations(csview test_source, VecCStr* out) {
    test_source = csview_from_range(file_start_excluding_bom(test_source.buf), csview_end_ptr(test_source));
    VecVariation variations = {0};
    for_each (line, lines_sv, test_source) {
        csview key, value;
        if (!parse_tag(line, &key, &value) || !compiler_option_varies(key)) continue;
        // A repeated option replaces the earlier one
        for (isize i = vec_variation_size(&variations) - 1; i >= 0; i--) {
            if (sv_equals_ignore_case(vec_variation_at(&variations, i)->key, key)) {
                variation_drop(vec_variation_at_mut(&variations, i));
                vec_variation_erase_n(&variations, i, 1);
            }
        }
        VecCStr values = split_vary_by_value(key, value);
        if (vec_cstr_size(&values)) {
            // Sorted by option name, which is how tsc names the configuration
            isize at = 0;
            while (at < vec_variation_size(&variations)) {
                csview other = vec_variation_at(&variations, at)->key;
                cstr a = cstr_from_sv(key), b = cstr_from_sv(other);
                cstr_lowercase(&a);
                cstr_lowercase(&b);
                bool before = cstr_cmp(&a, &b) < 0;
                c_drop(cstr, &a, &b);
                if (before) break;
                at++;
            }
            vec_variation_insert_n(&variations, at, &(Variation){ .key = key, .values = values }, 1);
        } else {
            vec_cstr_drop(&values);
        }
    }
    cstr prefix = cstr_null;
    push_configurations(&variations, 0, &prefix, out);
    vec_variation_drop(&variations);
}

// `/require\(/` or `/reference\spath/`, like tsc's harness
static bool contents_mention_references(csview contents) {
    if (csview_find_sv(contents, c_sv("require(")) != c_NPOS) return true;
    static const char* const forms[] = { "reference path", "reference\tpath" };
    for (size_t i = 0; i < sizeof(forms) / sizeof(*forms); i++) {
        if (csview_find_sv(contents, csview_from(forms[i])) != c_NPOS) return true;
    }
    return false;
}

// A harness directive that is a compiler option (or one of the harness's own that we honour)
static void apply_setting(BaselineTestCase* me, csview key, csview value) {
    if (tag_equals(key, "notypesandsymbols")) {
        my no_types_and_symbols = tag_equals(value, "true");
        return;
    }
    if (tag_equals(key, "noimplicitreferences")) {
        my no_implicit_references = tag_equals(value, "true");
        return;
    }
    if (tag_equals(key, "libfiles")) {
        cstr_take(&my lib_files, cstr_from_sv(value));
        return;
    }
    if (tag_equals(key, "currentdirectory")) {
        Fs const* mock = mock_fs_get_fs(my fs);
        try (fs_mkdir(mock, value)) or_log(info);
        try (fs_change_cwd(mock, value)) or_log(info);
        return;
    }
    if (tag_equals(key, "symlink")) {
        // `// @symlink: /a/x.ts,/b/x.ts` inside a file: other paths the file is at
        while (value.size) {
            isize end = csview_find_sv(value, c_sv(","));
            csview link = csview_trim_whitespace(csview_slice(value, 0, end));
            if (link.size) {
                vec_cstr_push(&my links, cstr_from_sv(my current_unit));
                vec_cstr_push(&my links, cstr_from_sv(link));
            }
            if (end == c_NPOS) break;
            value = csview_slice(value, end + 1, INTPTR_MAX);
        }
        return;
    }
    if (tag_equals(key, "link")) {
        // `// @link: /packages/a -> /node_modules/a`
        isize arrow = csview_find_sv(value, c_sv("->"));
        if (arrow != c_NPOS) {
            csview target = csview_trim_whitespace(csview_slice(value, 0, arrow));
            csview link = csview_trim_whitespace(csview_slice(value, arrow + 2, INTPTR_MAX));
            vec_cstr_push(&my links, cstr_from_sv(target));
            vec_cstr_push(&my links, cstr_from_sv(link));
        }
        return;
    }
    vec_cstr_push(&my directives, cstr_from_sv(key));
    vec_cstr_push(&my directives, cstr_from_sv(value));
    // Unknown options are overwhelmingly emit options. A bad value is something tsc's harness refuses to run, which
    // doesn't happen in tests that have baselines
    ignore compiler_options_set(program_options(my program), key, value);
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

// Copies the real directory `from` into the mock filesystem at `to`
static void mount_test_lib_folder(BaselineTestCase* me, csview from, csview to) {
    try (fs_mkdir(mock_fs_get_fs(my fs), to)) or_log(info);
    for_each (item, fs_listdir_sorted, fs, from) {
        cstr target = cstr_d(to, "/", item.name);
        if (fs_is_directory(fs, item.path)) {
            mount_test_lib_folder(me, item.path, cstr_sv(&target));
        } else {
            cstr contents = cstr_null;
            try (fs_read_file(fs, item.path, &contents)) or_log(info);
            try (mock_fs_create_file(my fs, cstr_sv(&target), contents)) or_log(info);
        }
        cstr_drop(&target);
    }
}

static_failable load_test_source(BaselineTestCase* me, csview config) {
    cstr unparsed_contents = cstr_null;
    cstr source_contents = cstr_null;

    try (fs_read_file(fs, my source_path, &unparsed_contents)) bubble;
    text_decode(&unparsed_contents);

    // Like tsc's harness: relative file names are in /.src, which is the current directory unless the test says
    // otherwise. So `/node_modules` is found from every file but isn't below the sources
    try (fs_mkdir(mock_fs_get_fs(my fs), c_sv("/.src"))) bubble;
    try (fs_change_cwd(mock_fs_get_fs(my fs), c_sv("/.src"))) bubble;

    csview contents_sv = csview_from_range(
        file_start_excluding_bom(cstr_str(&unparsed_contents)),
        csview_end_ptr(cstr_sv(&unparsed_contents))
    );

    bool seen_filename = false;
    csview current_filename = basename_of(my source_path);
    my current_unit = current_filename;
    for_each (i, lines_sv, contents_sv) {
        csview tag_key, tag_value;
        if (parse_tag(i, &tag_key, &tag_value)) {
            if (tag_equals(tag_key, "filename")) {
                // (what precedes the first `@filename` is dropped: tsc's harness only allows comments there)
                if (seen_filename) {
                    try (add_virtual_source_file(me, current_filename, source_contents)) cleanup {
                        cstr_drop(&unparsed_contents);
                    }
                } else {
                    cstr_drop(&source_contents);
                }
                source_contents = cstr_null;
                seen_filename = true;
                current_filename = tag_value;
                my current_unit = current_filename;
            } else {
                apply_setting(me, tag_key, tag_value);
            }
        } else {
            // tsc's makeUnitsFromTest: a unit's lines joined by line breaks, leading empty lines dropped (so there's
            // no line break before the next `@filename`)
            if (cstr_size(&source_contents)) cstr_append(&source_contents, "\r\n");
            cstr_append_sv(&source_contents, i);
        }
    }
    // (the empty line after the test file's last line break is a line of the last unit too; a lone `\r` at the end,
    // which tsc's harness keeps, breaks a line when rendered)
    if (cstr_size(&source_contents) && (csview_ends_with(contents_sv, "\n") || csview_ends_with(contents_sv, "\r"))) {
        cstr_append(&source_contents, "\r\n");
    }

    cstr_take(&my last_unit, cstr_from_sv(current_filename));
    try (add_virtual_source_file(me, current_filename, source_contents)) cleanup {
        cstr_drop(&unparsed_contents);
    }
    // tsc's harness has its tests/lib folder at /.lib (vfs.ts testLibFolder), for `@libFiles` and for
    // `/// <reference path="/.lib/react16.d.ts" />`. Copied in only for the tests that mention it
    if (cstr_size(&my lib_files) || csview_find_sv(contents_sv, c_sv("/.lib/")) != c_NPOS) {
        isize cases = csview_find_sv(my source_path, c_sv("/tests/cases/"));
        if (cases != c_NPOS) {
            cstr lib_dir = cstr_d(csview_slice(my source_path, 0, cases), "/tests/lib");
            mount_test_lib_folder(me, cstr_sv(&lib_dir), c_sv("/.lib"));
            cstr_drop(&lib_dir);
        }
    }

    // A file that says `/// <reference no-default-lib="true"/>` is the default library (tsc's hasNoDefaultLib)
    bool no_default_lib = csview_find_sv(contents_sv, c_sv("no-default-lib=\"true\"")) != c_NPOS;
    for (csview list = cstr_sv(&my lib_files); list.size && !no_default_lib;) {
        isize end = csview_find_sv(list, c_sv(","));
        cstr path = cstr_d("/.lib/", csview_trim_whitespace(csview_slice(list, 0, end)));
        list = end == c_NPOS ? c_sv("") : csview_slice(list, end + 1, INTPTR_MAX);
        cstr lib_contents = cstr_null;
        try (fs_read_file(mock_fs_get_fs(my fs), cstr_sv(&path), &lib_contents)) if_err (e) {
            ignore e;
            current_error_destroy();
        }
        no_default_lib = cstr_find(&lib_contents, "no-default-lib=\"true\"") != c_NPOS;
        cstr_drop(&lib_contents);
        cstr_drop(&path);
    }
    cstr_drop(&unparsed_contents);

    for (isize i = 0; i + 1 < vec_cstr_size(&my links); i += 2) {
        try (mock_fs_create_symlink(my fs, cstr_sv(vec_cstr_at(&my links, i + 1)), cstr_sv(vec_cstr_at(&my links, i)))) or_log(info);
    }

    // Like tsc's harness, a tsconfig.json (or jsconfig.json) among the files gives the options the directives are
    // applied over, and decides which of the files are roots
    for_each (i, vec_baseline_test_file, &my files) {
        csview name = basename_of(cstr_sv(&i->path));
        if (!tag_equals(name, "tsconfig.json") && !tag_equals(name, "jsconfig.json")) continue;
        try (tsconfig_load(mock_fs_get_fs(my fs), cstr_sv(&i->path), program_options(my program), &my tsconfig_roots)) if_err (e) {
            ignore e;
            current_error_destroy();
            break;
        }
        my has_tsconfig = true;
        VecCStr directives = my directives;
        my directives = (VecCStr){0};
        for (isize j = 0; j + 1 < vec_cstr_size(&directives); j += 2) {
            apply_setting(me, cstr_sv(vec_cstr_at(&directives, j)), cstr_sv(vec_cstr_at(&directives, j + 1)));
        }
        vec_cstr_drop(&directives);
        break;
    }

    // The configuration's values replace the "es5,es2015" the directives above left behind
    while (config.size) {
        isize end = csview_find_sv(config, c_sv(","));
        csview pair = csview_slice(config, 0, end);
        isize eq = csview_find_sv(pair, c_sv("="));
        check(eq != c_NPOS);
        apply_setting(me, csview_slice(pair, 0, eq), csview_slice(pair, eq + 1, INTPTR_MAX));
        if (end == c_NPOS) break;
        config = csview_slice(config, end + 1, INTPTR_MAX);
    }
    compiler_options_resolve(program_options(my program));
    cstr_take(&my lib_list, no_default_lib ? cstr_null : compiler_options_lib_files(program_options(my program)));

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
    // A file the test didn't write: reached through a symlink by a path we didn't expect, say
    log_error("failed to find test for source file ", source->canonical_path);
    return nullptr;
}

static const char* error_file_name(SourceFile const* source) {
    char const* ret = source->display_path_override ? source->display_path_override : cstr_str(&source->canonical_path);
    // (`// @filename: ./b.ts` is `b.ts` in the diagnostics, the file's normalized name, and `./b.ts` in its header)
    return strncmp(ret, "./", 2) == 0 ? ret + 2 : ret;
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
    if (indent_level == 0 && !span_valid(error->span)) {
        cstr_append_d(global_target, DEFAULT_DISPLAY, "error TS", error->ts_code, ": ");
    } else if (indent_level == 0) {
        unsigned line = span_line_index(error->span);
        unsigned col = span_col_index(error->span);
        const char* filename = error->span.source ? error_file_name(error->span.source) : "(nil)";

        cstr_append_d(
            global_target, DEFAULT_DISPLAY,
            filename, "(", line + 1, ",", col + 1, "): error TS", error->ts_code, ": "
        );
    }

    if (indent_level > 0 && error->detached) {
        cstr_append_d(file_target, DEFAULT_DISPLAY, "\r\n!!! related TS", error->ts_code, ": ", error->message);
        return;
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
        // Handle related errors (sub-errors that do have a valid span): in their own file, lib files without a position
        SourceFile const* related_source = error->span.source ? error->span.source : current_source;
        if (related_source->is_stdlib) {
            // (the default lib's files are named after their lib, a `@libFiles` one already is `lib.*.d.ts`)
            csview name = basename_of(cstr_sv(&related_source->canonical_path));
            cstr_append_d(
                file_target, DEFAULT_DISPLAY,
                "\r\n!!! related TS", error->ts_code, " ", csview_starts_with(name, "lib.") ? "" : "lib.", name,
                ":--:--: ", error->message
            );
        } else {
            unsigned related_line = span_line_index(error->span);
            unsigned related_col = span_col_index(error->span);
            cstr_append_d(
                file_target, DEFAULT_DISPLAY,
                "\r\n!!! related TS", error->ts_code, " ", error_file_name(related_source), ":",
                related_line + 1, ":", related_col + 1, ": ", error->message
            );
        }
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
            // A diagnostic without a file: listed before the first file
            format_error_tree(&my formatted_errors, &my formatted_global_errors, error, error, nullptr, 0);
            continue;
        }

        if (error->span.source != current_source) {
            current_source = error->span.source;
            current_test_file = find_file(me, current_source);
        }

        if (!current_test_file) continue;

        // Like tsc's harness (iterateErrorBaseline): a squiggle under every line the error covers, the message after
        // the last one. Lines of several errors interleave, so items are kept ordered by line
        unsigned line = span_line_index(error->span);
        const char* file_start = source_file_start(current_source);
        const char* start = span_start_ptr(error->span);
        const char* end = span_end_ptr(error->span);
        const char* line_start = start;
        while (line_start > file_start && line_start[-1] != '\n' && line_start[-1] != '\r') line_start--;
        while (true) {
            const char* line_end = line_start;
            while (*line_end && *line_end != '\n' && *line_end != '\r') line_end++;
            const char* next_line_start = line_end;
            if (*next_line_start == '\r') next_line_start++;
            if (*next_line_start == '\n') next_line_start++;
            bool is_last_line = !*line_end;

            LineNumberedStr item = { .line = line, .str = cstr_null };
            const char* squiggle_start = start > line_start ? start : line_start;
            cstr_append_lit(&item.str, "    ");
            // (in UTF-16 code units, like tsc's harness)
            for (const char* c = line_start; c < squiggle_start && c < line_end; c++) {
                for (unsigned n = utf8_byte_utf16_units(c); n; n--) cstr_append_n(&item.str, *c == '\t' ? "\t" : " ", 1);
            }
            const char* squiggle_end = end < line_end ? end : line_end;
            for (const char* c = squiggle_start; c < squiggle_end; c++) {
                for (unsigned n = utf8_byte_utf16_units(c); n; n--) cstr_append_lit(&item.str, "~");
            }
            bool finishes = is_last_line || next_line_start > end;
            if (finishes) format_error_tree(&my formatted_errors, &item.str, error, error, current_source, 0);

            vec_line_numbered_str_push(&current_test_file->errors, item);
            LineNumberedStr* items = current_test_file->errors.data;
            for (isize_t j = vec_line_numbered_str_size(&current_test_file->errors) - 1; j > 0 && items[j - 1].line > items[j].line; j--) {
                LineNumberedStr swapped = items[j];
                items[j] = items[j - 1];
                items[j - 1] = swapped;
            }
            if (finishes) break;
            line++;
            line_start = next_line_start;
        }
        current_test_file->error_count++;
    }
}

static bool path_is_script(csview path) {
    static const char* const extensions[] = { ".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs" };
    for (isize i = 0; i < (isize)c_arraylen(extensions); i++) {
        if (csview_ends_with(path, extensions[i])) {
            return true;
        }
    }
    return false;
}

// Where a file goes in the baselines of a test with a tsconfig.json: the config, the roots, the rest
static int tsconfig_file_rank(BaselineTestCase const* test, MockFs* fs, BaselineTestFile const* file) {
    csview name = basename_of(cstr_sv(&file->path));
    if (tag_equals(name, "tsconfig.json") || tag_equals(name, "jsconfig.json")) return 0;
    cstr cwd = cstr_null;
    try (fs_get_cwd(mock_fs_get_fs(fs), &cwd)) or_panic;
    cstr absolute = absolute_path_of(cstr_sv(&cwd), cstr_sv(&file->path));
    int ret = 2;
    for (isize j = 0; j < test->tsconfig_roots.count; j++) {
        if (cstr_eq(&test->tsconfig_roots.items[j], &absolute)) ret = 1;
    }
    cstr_drop(&absolute);
    cstr_drop(&cwd);
    return ret;
}

u32 baseline_test_checkers = 0;

BaselineTestCase run_baseline_test_case(
    BaselineTestCache* cache,
    csview source_path,
    csview config,
    bool noisy,
    bool show_logs
) {
    // A quiet test case shouldn't print the log_error()s the checker emits for constructs it can't handle yet
    LogLevel prev_threshold = logger_config.threshold;
    if (!noisy && !show_logs) {
        logger_config.threshold = LOG_LEVEL_NEVER;
    }

    ErrorCtx* errs = c_new(ErrorCtx, error_ctx_init());
    MockFs* fs = mock_fs_new();
    Program* program = program_new(errs, mock_fs_get_fs(fs));
    program_set_realpath(program, mock_fs_realpath, fs);
    program_set_checkers(program, baseline_test_checkers);

    BaselineTestCase ret = {
        .source_path = source_path,
        .error_ctx = errs,
        .fs = fs,
        .program = program,
        .noisy = noisy,
    };

    // A test we can't read or unpack just doesn't run; did_run stays false and the result is reported as such
    try (load_test_source(&ret, config)) if_err (e) {
        if (noisy) error_base_eprintln(e);
    }

    Program* lib_program = baseline_test_cache_get_program_with_libs(cache, cstr_sv(&ret.lib_list));
    program_copy_globals_from(program, lib_program);

    // tsc's harness: with `@noImplicitReferences`, or when the last file mentions `require(` or `reference path`, that
    // file is the only root (the others are part of the program if something refers to them) and comes first in the
    // baselines
    bool last_is_only_root = false;
    if (!ret.has_tsconfig && vec_baseline_test_file_size(&ret.files) > 1) {
        BaselineTestFile* last = vec_baseline_test_file_back_mut(&ret.files);
        cstr contents = cstr_null;
        try (fs_read_file(mock_fs_get_fs(fs), cstr_sv(&last->path), &contents)) if_err (e) {
            ignore e;
            current_error_destroy(); // A symlink (`@link`) to nowhere and the like
        }
        last_is_only_root = ret.no_implicit_references || contents_mention_references(cstr_sv(&contents));
        cstr_drop(&contents);
        if (last_is_only_root) {
            // Rotate it to the front (in place: popping would drop it)
            BaselineTestFile moved = *last;
            isize count = vec_baseline_test_file_size(&ret.files);
            memmove(ret.files.data + 1, ret.files.data, sizeof(BaselineTestFile) * (size_t)(count - 1));
            ret.files.data[0] = moved;
        }
    }

    // With a tsconfig.json the baselines show it, then the roots, then the other files (the harness's tsConfigFiles,
    // toBeCompiled and otherFiles), each in the order the test has them
    if (ret.has_tsconfig) {
        // (a stable insertion sort by rank)
        isize count = vec_baseline_test_file_size(&ret.files);
        for (isize i = 1; i < count; i++) {
            BaselineTestFile moving = ret.files.data[i];
            int rank = tsconfig_file_rank(&ret, fs, &moving);
            isize j = i;
            while (j > 0 && tsconfig_file_rank(&ret, fs, &ret.files.data[j - 1]) > rank) {
                ret.files.data[j] = ret.files.data[j - 1];
                j--;
            }
            ret.files.data[j] = moving;
        }
    }

    for_each (i, vec_baseline_test_file, &ret.files) {
        bool is_root = !last_is_only_root || i_index == 0;
        if (ret.has_tsconfig) {
            cstr cwd = cstr_null;
            try (fs_get_cwd(mock_fs_get_fs(fs), &cwd)) or_panic;
            cstr absolute = absolute_path_of(cstr_sv(&cwd), cstr_sv(&i->path));
            is_root = false;
            for (isize j = 0; j < ret.tsconfig_roots.count && !is_root; j++) {
                is_root = cstr_eq(&ret.tsconfig_roots.items[j], &absolute);
            }
            cstr_drop(&absolute);
            cstr_drop(&cwd);
        }
        if (!path_is_script(cstr_sv(&i->path)) || !is_root) continue;
        // These were all just written into our mock filesystem, so failing to load one is a bug on our end
        try (SourceFile* source, program_load(program, cstr_sv(&i->path), &source)) or_panic;
        i->source = source;
    }
    // `@libFiles` are roots after the test's own files (harnessIO.ts compileFiles). Like the default lib, a `lib.*.d.ts`
    // among them is shown without positions
    for (csview list = cstr_sv(&ret.lib_files); list.size;) {
        isize end = csview_find_sv(list, c_sv(","));
        csview name = csview_trim_whitespace(csview_slice(list, 0, end));
        list = end == c_NPOS ? c_sv("") : csview_slice(list, end + 1, INTPTR_MAX);
        if (!name.size) continue;
        cstr path = cstr_d("/.lib/", name);
        SourceFile* source = nullptr;
        try (program_load(program, cstr_sv(&path), &source)) if_err (e) {
            ignore e;
            current_error_destroy();
        }
        if (source && csview_starts_with(name, "lib.") && csview_ends_with(name, ".d.ts")) {
            source->is_stdlib = true;
            cstr_take(&source->decl_override, cstr_d(name, ", --, --"));
        }
        cstr_drop(&path);
    }
    program_load_automatic_types(program);
    for_each (i, vec_baseline_test_file, &ret.files) {
        if (!i->source && (path_is_script(cstr_sv(&i->path)) || csview_ends_with(cstr_sv(&i->path), ".json"))) {
            i->source = program_find_source(program, cstr_sv(&i->path));
        }
        if (i->source) {
            ((SourceFile*)i->source)->display_path_override = cstr_str(&i->path);
        } else {
            // package.json, tsconfig.json, .map files and so on, and scripts nothing refers to: they exist in the
            // filesystem but aren't part of the program, the errors baseline still shows them
            try (fs_read_file(mock_fs_get_fs(fs), cstr_sv(&i->path), &i->raw_contents)) or_panic;
        }
    }

    // tsc reports type errors next to parse errors (getPreEmitDiagnostics() is all of them)
    program_resolve_names(program);
    program_typecheck(program);

    set_compiler_options(program_options(program));
    for_each (i, vec_baseline_test_file, &ret.files) {
        if (!i->source) continue;
        // PERF: perhaps mark the file as "current" so we don't have to look it up
        ret.enclosing = i->source->scope;
        block_compile_test_info(&i->source->ast, &ret);
    }
    clear_compiler_options();

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
    cstr_drop(&my lib_files);
    cstr_drop(&my last_unit);
    cstr_drop(&my formatted_errors);
    cstr_drop(&my formatted_global_errors);
    vec_baseline_test_file_drop(&my files);
    vec_cstr_drop(&my links);
    vec_cstr_drop(&my directives);
    str_list_drop(&my tsconfig_roots);
    free(my scope_params);
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

// A line tsc's harness doesn't separate from the lines before it by an empty line: blank, or only `{`, `|` or `}`
static bool is_quiet_code_line(csview line) {
    csview t = csview_trim_whitespace(line);
    return t.size == 0 || (t.size == 1 && (t.buf[0] == '{' || t.buf[0] == '|' || t.buf[0] == '}'));
}

typedef struct CodeLines {
    csview* data;
    u32 len;
    u32 cap;
} CodeLines;

// tsc's harness: `content.split(/\r?\n/)`, each part split again on \r, U+2028 and U+2029
static CodeLines split_code_lines(csview contents) {
    CodeLines ret = {0};
    char const* start = contents.buf;
    char const* end = csview_end_ptr(contents);
    char const* c = start;
    for (;;) {
        char const* line_end = c;
        size_t skip = 0;
        while (line_end < end) {
            if (*line_end == '\n') { skip = 1; break; }
            if (*line_end == '\r') { skip = line_end + 1 < end && line_end[1] == '\n' ? 2 : 1; break; }
            if ((unsigned char)line_end[0] == 0xE2 && line_end + 2 < end && (unsigned char)line_end[1] == 0x80 &&
                ((unsigned char)line_end[2] == 0xA8 || (unsigned char)line_end[2] == 0xA9)
            ) {
                skip = 3;
                break;
            }
            line_end++;
        }
        if (ret.len == ret.cap) {
            ret.cap = ret.cap ? ret.cap * 2 : 64;
            ret.data = realloc(ret.data, sizeof(csview) * ret.cap);
            panic_if(!ret.data, "out of memory");
        }
        ret.data[ret.len++] = csview_from_n(c, line_end - c);
        if (!skip) break;
        c = line_end + skip;
    }
    return ret;
}

// tsc's harness iterateBaseLine() for one file of the types or symbols baseline: code lines up to each result's line,
// an empty line before a run of code lines that don't follow the last result's line directly (unless the first of them
// is blank or a lone brace), the file's rest, a final line break
static void render_types_or_symbols_file(
    cstr* target, const char* source_path, csview source_contents, VecLineNumberedStr const* lines, bool is_last_file,
    bool drop_any_underlines
) {
    cstr_append_d(target, DEFAULT_DISPLAY, "=== ", source_path, " ===\r\n");
    CodeLines code = split_code_lines(source_contents);
    i64 last = -2; // -2: nothing written yet
    for_each (i, vec_line_numbered_str, lines) {
        i64 line = i->line < code.len ? i->line : code.len - 1;
        if (last == -2) {
            for (i64 l = 0; l <= line; l++) {
                cstr_append_sv(target, code.data[l]);
                cstr_append_lit(target, "\r\n");
            }
        } else if (line != last) {
            if (!(last + 1 < code.len && is_quiet_code_line(code.data[last + 1]))) cstr_append_lit(target, "\r\n");
            for (i64 l = last + 1; l <= line; l++) {
                cstr_append_sv(target, code.data[l]);
                cstr_append_lit(target, "\r\n");
            }
        }
        last = line;
        if (drop_any_underlines && i->any_underline_at) {
            append_without_any_underline(target, i);
        } else {
            cstr_append_s(target, i->str);
        }
        cstr_append_lit(target, "\r\n");
    }
    if (last == -2) last = -1;
    if (last + 1 < code.len) {
        if (!is_quiet_code_line(code.data[last + 1])) cstr_append_lit(target, "\r\n");
        for (i64 l = last + 1; l < code.len; l++) {
            if (l > last + 1) cstr_append_lit(target, "\r\n");
            cstr_append_sv(target, code.data[l]);
        }
    }
    cstr_append_lit(target, "\r\n");
    free(code.data);
}

static void render_lines_woven_with_file(
    cstr* target,
    const char* source_path,
    bool is_errors,
    csview source_contents,
    VecLineNumberedStr const* lines,
    bool is_last_file,
    bool drop_any_underlines
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

    // An empty file is one empty line (tsc splits "" into [""])
    if (is_errors && !source_contents.size) {
        cstr_append(target, line_indent);
        for (; index < vec_size; index++) {
            cstr_append(target, "\r\n");
            cstr_append_s(target, vec_line_numbered_str_at(lines, index)->str);
        }
        return;
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
            (index < vec_size && vec_line_numbered_str_at(lines, index)->line <= line) ||
            csview_end_ptr(i) < end_of_content_lines
        ) {
            cstr_append(target, "\r\n");
        }
        bool lines_added_from_vec = false;
        while (index < vec_size && vec_line_numbered_str_at(lines, index)->line <= line) {
            if (lines_added_from_vec) {
                cstr_append(target, "\r\n");
            }
            LineNumberedStr const* added = vec_line_numbered_str_at(lines, index);
            if (drop_any_underlines && added->any_underline_at) {
                append_without_any_underline(target, added);
            } else {
                cstr_append_s(target, added->str);
            }
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


    for (intptr_t i = 0; i < newlines_at_end_of_source; i++) {
        cstr_append_d(target, DEFAULT_DISPLAY, "\r\n", line_indent);
    }
    // Errors at the very end of a file that ends in a newline sit on the empty line after it
    for (; index < vec_size; index++) {
        cstr_append(target, "\r\n");
        cstr_append_s(target, vec_line_numbered_str_at(lines, index)->str);
    }
}

void baseline_test_case_render_errors(cstr* target, BaselineTestCase const* me) {
    if (error_ctx_empty(my error_ctx)) {
        return;
    }

    cstr_append_s(target, my formatted_errors);
    cstr_append_lit(target, "\r\n");
    if (cstr_size(&my formatted_global_errors)) {
        cstr_append_d(target, DEFAULT_DISPLAY, my formatted_global_errors);
    }

    for_each (i, vec_baseline_test_file, &my files) {
        cstr_append_d(
            target, DEFAULT_DISPLAY,
            "\r\n==== ", i->path, " (", i->error_count, " errors) ====\r\n"
        );

        render_lines_woven_with_file(
            target,
            cstr_str(&i->path),
            true,
            i->source ? cstr_sv(&i->source->contents) : cstr_sv(&i->raw_contents),
            &i->errors,
            i == vec_baseline_test_file_back(&my files),
            false
        );
    }
}

static bool is_last_unit(BaselineTestCase const* me, BaselineTestFile const* file) {
    csview a = cstr_sv(&file->path), b = cstr_sv(&my last_unit);
    if (csview_starts_with(a, "/")) a = csview_slice(a, 1, a.size);
    if (csview_starts_with(b, "/")) b = csview_slice(b, 1, b.size);
    return csview_equals_sv(a, b);
}

// tsc's Utils.removeTestPathPrefixes on what was appended to target from `from` on: `/.src/`, `/.lib/` and `/.ts/`
// disappear (`file:///.src/` keeps its `file:///`)
static void remove_test_path_prefixes(cstr* target, isize from) {
    static const char* const prefixes[] = { "/.src/", "/.lib/", "/.ts/" };
    for (isize i = 0; i < (isize)c_arraylen(prefixes); i++) {
        isize at = from;
        while ((at = cstr_find_at(target, at, prefixes[i])) != c_NPOS) {
            cstr_erase(target, at, (isize)strlen(prefixes[i]) - 1);
            // (the `/` of the prefix stays only after `file://`)
            const char* str = cstr_str(target);
            if (at >= 7 && !memcmp(str + at - 7, "file://", 7)) {
                at++;
            } else {
                cstr_erase(target, at, 1);
            }
        }
    }
}

void baseline_test_case_render_symbols(cstr* target, BaselineTestCase const* me) {
    cstr_append_d(target, DEFAULT_DISPLAY, "//// [", relative_test_path(my source_path), "] ////\r\n\r\n");
    for_each (i, vec_baseline_test_file, &my files) {
        if (!i->source) continue;
        isize from = cstr_size(target);
        render_types_or_symbols_file(target, cstr_str(&i->path), cstr_sv(&i->source->contents), &i->symbols,
            is_last_unit(me, i), false);
        remove_test_path_prefixes(target, from);
    }
}

bool baseline_test_case_has_errors(BaselineTestCase const* me) {
    return !error_ctx_empty(my error_ctx) || cstr_size(&my formatted_global_errors);
}

void baseline_test_case_render_types(cstr* target, BaselineTestCase const* me, bool had_errors) {
    cstr_append_d(target, DEFAULT_DISPLAY, "//// [", relative_test_path(my source_path), "] ////\r\n\r\n");
    for_each (i, vec_baseline_test_file, &my files) {
        if (!i->source) continue;
        isize from = cstr_size(target);
        render_types_or_symbols_file(target, cstr_str(&i->path), cstr_sv(&i->source->contents), &i->types,
            is_last_unit(me, i), !had_errors);
        remove_test_path_prefixes(target, from);
    }
}

// tsc's harness hides the positions in files whose name matches /lib.*\.d\.ts/i (the libs, tslib.d.ts, a test's
// own lib.d.ts)
static bool is_lib_file_name(csview name) {
    for (isize i = 0; i + 3 <= name.size; i++) {
        if (tolower((unsigned char)name.buf[i]) != 'l' || tolower((unsigned char)name.buf[i + 1]) != 'i' ||
            tolower((unsigned char)name.buf[i + 2]) != 'b'
        ) {
            continue;
        }
        for (isize j = i + 3; j + 5 <= name.size; j++) {
            if (strncasecmp(name.buf + j, ".d.ts", 5) == 0) return true;
        }
        return false;
    }
    return false;
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
    } else if (is_lib_file_name(basename_of(cstr_sv(&span.source->canonical_path)))) {
        cstr_append_d(
            target, DEFAULT_DISPLAY,
            ", Decl(", basename_of(cstr_sv(&span.source->canonical_path)), ", --, --)"
        );
        return;
    }
    const char* file_start = source_file_start(span.source);
    const char* start = span_start_ptr(span);
    bool keep_going = true;
    // A JSDoc tag (`@typedef`, `@property`...) starts at its `@`: tsc's JSDoc nodes have no leading trivia
    if (*start == '@') {
        const char* c = start;
        while (c > file_start && (c[-1] == ' ' || c[-1] == '\t')) c--;
        if (c > file_start && c[-1] == '*') keep_going = false;
    }
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
                // Skip line comments, going backwards: the comment starts at the first `//` of the line (`///x`)
                const char* line_start = start - 1;
                while (line_start > file_start && line_start[-1] != '\n') line_start--;
                if (line_start == file_start && line_start[0] == '#' && line_start[1] == '!') {
                    start = file_start; // A shebang is trivia too
                    break;
                }
                for (const char* c = line_start; c + 1 < start; c++) {
                    if (c[0] == '/' && c[1] == '/') {
                        keep_going = true;
                        start = c;
                        break;
                    }
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
    if (my in_with) {
        cstr_drop(&data.text);
        vec_span_drop(&data.declarations);
        vec_span_drop(&data.namespaces);
        return;
    }
    unsigned line_for_placement = span_line_index(data.span);
    for (u32 i = 0; i < my symbol_override_count; i++) {
        if (my symbol_overrides[i].span.sv.buf != data.span.sv.buf || my symbol_overrides[i].span.sv.size != data.span.sv.size) continue;
        cstr_drop(&data.text);
        vec_span_drop(&data.declarations);
        if (my symbol_overrides[i].suppress) {
            vec_span_drop(&data.namespaces);
            return;
        }
        data.text = cstr_clone(my symbol_overrides[i].text);
        data.declarations = vec_span_clone(my symbol_overrides[i].declarations);
        data.allow_declaration_empty = true;
        break; // (The first one: an assignment's target is what it declares first)
    }

    cstr text = cstr_d(">", data.span, " : Symbol(");

    bool has_namespace = false;
    if (cstr_size(&data.text)) {
        cstr_append_sv(&text, cstr_sv(&data.text));
        vec_span_clear(&data.namespaces);
        data.name_is_value = false;
    }
    for_each (i, vec_span, &data.namespaces) {
        if (span_valid(*i)) {
            if (has_namespace) {
                cstr_append_lit(&text, ".");
            }
            cstr_append_span(&text, *i);
            has_namespace = true;
        }
    }

    if (cstr_size(&data.text)) {
        // (the name is complete)
    } else if (data.name_is_value) {
        if (has_namespace) cstr_append_lit(&text, "[");
        cstr_append_span(&text, span_valid(data.name) ? data.name : data.span);
        if (has_namespace) cstr_append_lit(&text, "]");
    } else {
        if (has_namespace) {
            cstr_append_lit(&text, ".");
        }
        Span name = span_valid(data.name) ? data.name : data.span;
        if (name.sv.size) {
            cstr_append_span(&text, name);
        } else {
            cstr_append_lit(&text, "(Missing)"); // A name the parser recovered as missing (tsc's "__missing")
        }
    }

    bool found_decl = false;
    u32 valid = 0;
    for_each (i, vec_span, &data.declarations) valid += span_valid(*i);
    u32 shown = 0;
    for_each (i, vec_span, &data.declarations) {
        if (span_valid(*i)) {
            // (tsc's writer shows five)
            if (shown == 5) {
                cstr_append_d(&text, DEFAULT_DISPLAY, " ... and ", valid - shown, " more");
                break;
            }
            append_decl(&text, *i);
            found_decl = true;
            shown++;
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
    cstr_drop(&data.text);
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

// One `>text : type` line plus its underline per parenthesization level of the expression
// `underline` null: all of it synthetic (`^`)
static void add_type_rendered(BaselineTestCase* me, Span outer_span, Span inner_span, csview rendered, cstr const* underline_of_type) {
    for (u32 i = 0; i < my hidden_type_span_count; i++) {
        if (my hidden_type_spans[i].sv.buf == inner_span.sv.buf && my hidden_type_spans[i].sv.size == inner_span.sv.size) {
            my any_keeps_underline = false;
            return;
        }
    }
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
    cstr target = cstr_lit(">");
    append_span_contents_without_newlines(&target, outer_span);
    // As wide as the text in UTF-16 code units, which is what the harness counts
    isize_t width = 0;
    for (isize_t i = 0; i < cstr_size(&target); i++) {
        unsigned char c = (unsigned char)cstr_str(&target)[i];
        if ((c & 0xC0) != 0x80) width += c >= 0xF0 ? 2 : 1;
    }
    cstr underline = cstr_null;
    cstr_resize(&underline, width, ' ');
    *cstr_data(&underline) = '>';
    cstr_append_lit(&target, " : ");
    cstr_append_lit(&underline, " : ");
    cstr_append_sv(&target, rendered);
    if (underline_of_type) {
        cstr_append_s(&underline, *underline_of_type);
    } else {
        cstr_resize(&underline, cstr_size(&underline) + rendered.size, '^');
    }
    isize_t any_underline_at = my any_keeps_underline || !csview_equals(rendered, "any") ? 0 : cstr_size(&target);
    bool error_type = my error_type && any_underline_at;
    my any_keeps_underline = false; // Only the node itself, not what's parenthesized in it
    cstr_append_lit(&target, "\r\n");
    cstr_append_s(&target, underline);
    cstr_drop(&underline);
    BaselineTestFile* test_file = find_file(me, outer_span.source);
    vec_line_numbered_str_push(&test_file->types, (LineNumberedStr){
        .line = line,
        .str = target,
        .any_underline_at = any_underline_at,
        .error_type = error_type,
    });
    if (!base_case) {
        outer_span.sv.buf++;
        outer_span.sv.size -= 2;
        add_type_rendered(me, outer_span, inner_span, rendered, underline_of_type);
    }
}

void baseline_test_case_add_type_recursive(BaselineTestCase* me, Span outer_span, Span inner_span, Ty const* type) {
    if (my in_with) type = ty_intrinsic(current_checker, TY_ERROR);
    cstr rendered = cstr_null;
    cstr underline = cstr_null;
    TyParamNames param_names = { .in_scope = { .data = my scope_params, .len = my scope_params_len } };
    ty_display(current_checker, (TyDisplay){
        .target = &rendered, .enclosing = my enclosing, .underline = &underline, .unique_symbols = true,
        .param_names = &param_names,
    }, type);
    free(param_names.given);
    my error_type = (type->flags & TYF_ERROR) != 0;
    add_type_rendered(me, outer_span, inner_span, cstr_sv(&rendered), &underline);
    my error_type = false;
    c_drop(cstr, &rendered, &underline);
}

void baseline_test_case_add_type_text(BaselineTestCase* me, Span span, csview text) {
    if (my in_with) {
        baseline_test_case_add_type(me, span, nullptr);
        return;
    }
    cstr underline = cstr_null;
    for (isize i = 0; i < text.size; i++) cstr_push(&underline, "^");
    add_type_rendered(me, span, span, text, &underline);
    cstr_drop(&underline);
}

u32 baseline_test_case_enter_generic(BaselineTestCase* me, Generic const* generic) {
    return baseline_test_case_enter_type_params(me, ty_type_params_of(current_checker, generic));
}

u32 baseline_test_case_enter_type_params(BaselineTestCase* me, TyList params) {
    u32 saved = my scope_params_len;
    for (u32 i = 0; i < params.len; i++) {
        if (my scope_params_len == my scope_params_cap) {
            my scope_params_cap = my scope_params_cap ? my scope_params_cap * 2 : 16;
            my scope_params = realloc(my scope_params, sizeof(Ty const*) * my scope_params_cap);
        }
        my scope_params[my scope_params_len++] = params.data[i];
    }
    return saved;
}

void baseline_test_case_leave_generic(BaselineTestCase* me, u32 saved) {
    my scope_params_len = saved;
}

void baseline_test_case_add_type(BaselineTestCase* me, Span span, Ty const* type) {
    baseline_test_case_add_type_recursive(me, span, span, type);
}

void baseline_test_case_add_type_node(BaselineTestCase* me, Span span, TypeNode const* node) {
    baseline_test_case_add_type(me, span, ty_of_node(node));
}

void baseline_test_case_add_declaration(BaselineTestCase* me, Span span, Symbol const* declaration) {
    if (!declaration) {
        log_error("baseline_test_case_add_declaration() called with null declaration");
        return;
    }
    check(span.source);
    baseline_test_case_add_symbol(me, (BaselineSym){
        .declarations = symbol_declaration_spans(declaration),
        .span = span,
        .name = declaration->name_span,
    });
    // (An alias here is a variable declared by `require()` in JavaScript)
    if (declaration->flags & (SYMBOL_VALUE | SYMBOL_ALIAS)) {
        baseline_test_case_add_symbol_value(me, span, declaration);
    } else if (declaration->flags & SYMBOL_TYPE) {
        baseline_test_case_add_type(me, span, ty_declared_type_of(current_checker, declaration));
    }
}

void baseline_test_case_add_symbol_value(BaselineTestCase* me, Span span, Symbol const* symbol) {
    Symbol const* value = symbol_for_meaning(symbol, SYMBOL_VALUE);
    if (!value || value->is_placeholder) {
        baseline_test_case_add_type_node(me, span, &type_any);
        return;
    }
    baseline_test_case_add_type(me, span, ty_type_of_symbol(current_checker, value));
}
