#include "../class_decl.h"
#include "baseline_test_case.h"
#include "baseline_test_traversal.h"
#include <strings.h>
#include "mock_fs.h"
#include "../tsconfig.h"
#include "../content_mapper.h"
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
#include "statement.h"
#include "symbol_chain.h"

typedef struct LineNumberedStr {
    unsigned line;
    cstr str;
    // A types line of `any`: where its type starts. When the test has no errors tsc's harness prints the intrinsic name
    // there instead of a type node (TypeWriterWalker without hadErrorBaseline). 0: always printed as it is
    isize_t intrinsic_at;
    // ... and it's the error type: its intrinsic name is `error` (it prints `any` or an unresolved name otherwise)
    bool error_type;
} LineNumberedStr;

static void line_numbered_str_drop(LineNumberedStr* me) {
    cstr_drop(&my str);
}

// A types line of `any` as tsc's harness prints it without errors: the intrinsic name
static void append_intrinsic_any(cstr* target, LineNumberedStr const* line) {
    cstr_append_sv(target, csview_from_n(cstr_str(&line->str), line->intrinsic_at));
    cstr_append(target, line->error_type ? "error" : "any");
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
    // (Shared by the tests that don't check library files, baseline_test_case's others load their own)
    ignore compiler_options_set(program_options(program), c_sv("skipdefaultlibcheck"), c_sv("true"));
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
    cstr_drop(&my header);
    cstr_drop(&my raw_contents);
    vec_line_numbered_str_drop(&my symbols);
    vec_line_numbered_str_drop(&my types);
    vec_line_numbered_str_drop(&my errors);
}

static bool parse_tag(csview original, csview* key, csview* value) {
    csview line = original;
    // (At the very start of the line: tsc's harness matches its optionRegex against each line, `^\/{2}`, so an indented
    // `// @todo: x` is code)
    line = csview_trim_whitespace(line);
    if (line.buf != original.buf) return false;
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
        // (tsgo's extractCompilerSettings drops one trailing `;`: `// @declaration: true;`)
        if (value->size && value->buf[value->size - 1] == ';') value->size--;
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
    if (tag_equals(key, "fullemitpaths")) {
        my full_emit_paths = tag_equals(value, "true");
        return;
    }
    if (tag_equals(key, "capturesuggestions")) {
        program_options(my program)->capture_suggestions = tag_equals(value, "true");
        return;
    }
    if (tag_equals(key, "usecasesensitivefilenames")) {
        program_options(my program)->case_insensitive_file_names = tag_equals(value, "false");
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
static_failable add_virtual_source_file(BaselineTestCase* me, csview written_path, cstr contents) {
    cstr normalized = cstr_from_sv(written_path);
    for (isize k = 0; k < cstr_size(&normalized); k++) {
        if (cstr_data(&normalized)[k] == '\\') cstr_data(&normalized)[k] = '/';
    }
    bool renamed = !csview_equals_sv(cstr_sv(&normalized), written_path);
    // (A repeated `/` is one: tsgo's harness normalizes the unit name, `development//x.d.ts`, shown normalized too)
    for (isize k = 1; k < cstr_size(&normalized); k++) {
        if (cstr_data(&normalized)[k] == '/' && cstr_data(&normalized)[k - 1] == '/') {
            cstr_erase(&normalized, k, 1);
            k--;
        }
    }
    csview virtual_path = cstr_sv(&normalized);
    if (fs_exists(mock_fs_get_fs(my fs), virtual_path)) {
        // A handful of tsc tests repeat a // @Filename:, in which case the last copy is the file (tsc's harness
        // writes them all to its virtual file system in order); an earlier one is still shown with its own text,
        // unless it was empty
        for (isize i = vec_baseline_test_file_size(&my files) - 1; i >= 0; i--) {
            BaselineTestFile* earlier = &my files.data[i];
            if (earlier->superseded || !csview_equals_sv(cstr_sv(&earlier->path), virtual_path)) continue;
            cstr shown = cstr_null;
            try (fs_read_file(mock_fs_get_fs(my fs), virtual_path, &shown)) or_log(info);
            if (!cstr_size(&shown)) {
                cstr_drop(&shown);
                baseline_test_file_drop(earlier);
                vec_baseline_test_file_erase_n(&my files, i, 1);
            } else {
                earlier->raw_contents = shown;
                earlier->superseded = true;
            }
            break;
        }
        try (mock_fs_overwrite_file(my fs, virtual_path, contents)) cleanup {
            cstr_drop(&normalized);
        }
    } else {
        try (mock_fs_create_file(my fs, virtual_path, contents)) cleanup {
            cstr_drop(&normalized);
        }
    }
    vec_baseline_test_file_push(&my files, (BaselineTestFile){
        .path = normalized,
        .header = renamed ? cstr_from_sv(written_path) : cstr_null,
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


// tsgo's SkipUnsupportedCompilerOptions (and the Fatalf of failOnUnsupportedCompilerOptions): configurations that give
// an option 7.0 removed, as written (before compiler_options_resolve()'s defaults)
static bool tsgo_skips_configuration(CompilerOptions const* options) {
    return (options->module_set && (options->module == MODULE_KIND_UMD || options->module == MODULE_KIND_SYSTEM ||
                                    options->module == MODULE_KIND_AMD)) ||
        (options->module_resolution_set && (options->module_resolution == MODULE_RESOLUTION_NODE10 ||
                                            options->module_resolution == MODULE_RESOLUTION_CLASSIC)) ||
        (options->is_set[COMPILER_OPTION_es_module_interop] && !options->es_module_interop) ||
        (options->is_set[COMPILER_OPTION_allow_synthetic_default_imports] && !options->allow_synthetic_default_imports) ||
        (options->is_set[COMPILER_OPTION_always_strict] && !options->always_strict) ||
        options->base_url_set || cstr_size(&options->out_file) ||
        (options->target_set && options->target == SCRIPT_TARGET_ES5);
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
    // tsc 6.0's Utils.splitContentByNewlines: split at /\r?\n/ (never at a lone `\r`)
    csview* chunks = nullptr;
    isize chunk_count = 0, chunk_cap = 0;
#define PUSH_CHUNK(sv) do { \
        if (chunk_count == chunk_cap) { chunk_cap = chunk_cap ? chunk_cap * 2 : 64; chunks = realloc(chunks, sizeof(csview) * (size_t)chunk_cap); } \
        chunks[chunk_count++] = (sv); \
    } while (0)
    {
        csview rest = contents_sv;
        for (;;) {
            isize at = csview_find_sv(rest, c_sv("\n"));
            if (at == c_NPOS) {
                if (rest.size) PUSH_CHUNK(rest); // (like lines_sv: no empty line after the last line break)
                break;
            }
            isize end = at > 0 && rest.buf[at - 1] == '\r' ? at - 1 : at;
            PUSH_CHUNK(csview_slice(rest, 0, end));
            rest = csview_slice(rest, at + 1, rest.size);
        }
    }
#undef PUSH_CHUNK
    // The units, written to the mock filesystem once they're all known (in tsgo's harness's order, see below)
    struct { csview name; cstr contents; }* units = nullptr;
    isize unit_count = 0;
#define PUSH_UNIT(n, c) do { \
        units = realloc(units, sizeof(*units) * (size_t)(unit_count + 1)); \
        panic_if(!units, "out of memory"); \
        units[unit_count].name = (n); \
        units[unit_count++].contents = (c); \
    } while (0)
    for (isize chunk_index = 0; chunk_index < chunk_count; chunk_index++) {
        csview whole = chunks[chunk_index];
        isize newline = csview_find_sv(whole, c_sv("\n"));
        csview i_value = newline == c_NPOS ? whole : csview_slice(whole, 0, newline);
        csview* i = &i_value;
        csview tag_key, tag_value;
        if (parse_tag(*i, &tag_key, &tag_value)) {
            for (csview rest = newline == c_NPOS ? csview_slice(whole, whole.size, whole.size) : csview_slice(whole, newline + 1, whole.size); rest.size;) {
                isize next = csview_find_sv(rest, c_sv("\n"));
                csview line = next == c_NPOS ? rest : csview_slice(rest, 0, next);
                csview k, v;
                if (parse_tag(line, &k, &v) && !tag_equals(k, "filename")) apply_setting(me, k, v);
                rest = next == c_NPOS ? csview_slice(rest, rest.size, rest.size) : csview_slice(rest, next + 1, rest.size);
            }
            if (tag_equals(tag_key, "filename")) {
                // (what precedes the first `@filename` is dropped: tsc's harness only allows comments there)
                if (seen_filename) {
                    PUSH_UNIT(current_filename, source_contents);
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
            // tsgo's makeUnitsFromTest: a unit's lines joined by `\n`, leading empty lines dropped (so there's no line
            // break before the next `@filename`)
            if (cstr_size(&source_contents)) cstr_append(&source_contents, "\n");
            cstr_append_sv(&source_contents, whole);
        }
    }
    free(chunks);
    // (the empty line after the test file's last line break is a line of the last unit too; a lone `\r` at the end,
    // which tsc's harness keeps, breaks a line when rendered)
    bool ends_with_break = csview_ends_with(contents_sv, "\n");
    if (cstr_size(&source_contents) && ends_with_break) {
        cstr_append(&source_contents, "\n");
    }

    PUSH_UNIT(current_filename, source_contents);
#undef PUSH_UNIT

    // tsc's harness: with `@noImplicitReferences`, or when the last unit mentions `require(` or `reference path`, that
    // unit is the only root (the others are part of the program if something refers to them). It writes the roots
    // first, then the other units (CompileFiles' inputFiles, then otherFiles), so of a repeated `@filename` the last
    // copy written wins: the root's copy loses to an earlier one. The baselines show them in that order too
    bool names_tsconfig = false;
    for (isize u = 0; u < unit_count; u++) {
        csview name = basename_of(units[u].name);
        names_tsconfig |= tag_equals(name, "tsconfig.json") || tag_equals(name, "jsconfig.json");
    }
    my last_is_only_root = !names_tsconfig && unit_count > 1 &&
        (my no_implicit_references || contents_mention_references(cstr_sv(&units[unit_count - 1].contents)));
    for (isize n = 0; n < unit_count; n++) {
        isize u = !my last_is_only_root ? n : n == 0 ? unit_count - 1 : n - 1;
        cstr contents = units[u].contents;
        units[u].contents = cstr_null;
        try (add_virtual_source_file(me, units[u].name, contents)) cleanup {
            for (isize k = 0; k < unit_count; k++) cstr_drop(&units[k].contents);
            free(units);
            cstr_drop(&unparsed_contents);
        }
    }
    free(units);
    // tsc's harness has its tests/lib folder at /.lib (vfs.ts testLibFolder), for `@libFiles` and for
    // `/// <reference path="/.lib/react16.d.ts" />`. Copied in only for the tests that mention it
    if (cstr_size(&my lib_files) || csview_find_sv(contents_sv, c_sv("/.lib/")) != c_NPOS) {
        isize cases = csview_find_sv(my source_path, c_sv("/tests/cases/"));
        if (cases != c_NPOS) {
            cstr lib_dir = cstr_d(csview_slice(my source_path, 0, cases), "/tests/lib");
            mount_test_lib_folder(me, cstr_sv(&lib_dir), c_sv("/.lib"));
            cstr_drop(&lib_dir);
        } else if (csview_size(baseline_test_lib_dir)) {
            mount_test_lib_folder(me, baseline_test_lib_dir, c_sv("/.lib"));
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
    // `/// <reference lib="dom" />` in a test's file adds that lib to the program (tsc's processLibReferenceDirectives).
    // The libs are a cached program of their own that a file can't add to (program_follow_lib_references() is off),
    // so the harness adds them to the list up front. Not with libReplacement, whose libs the test's program loads
    // itself (the lib comes from node_modules/@typescript/lib-*)
    cstr referenced_libs = cstr_null;
    bool lib_replacement = csview_find_sv(contents_sv, c_sv("@libReplacement: true")) != c_NPOS;
    for (csview rest = contents_sv; !lib_replacement;) {
        isize at = csview_find_sv(rest, c_sv("<reference lib=\""));
        if (at == c_NPOS) break;
        rest = csview_slice(rest, at + (isize)strlen("<reference lib=\""), INTPTR_MAX);
        isize end = csview_find_sv(rest, c_sv("\""));
        if (end == c_NPOS) break;
        cstr name = cstr_from_sv(csview_slice(rest, 0, end));
        cstr_lowercase(&name);
        if (cstr_size(&referenced_libs)) cstr_append_lit(&referenced_libs, ",");
        cstr_append_s(&referenced_libs, name);
        cstr_drop(&name);
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
        // (tsc's harness parses it without the directives, parseJsonSourceFileConfigFileContent with no existing
        // options: they don't decide its files, an `@allowJs` doesn't make a .js file a root. They apply after)
        CompilerOptions* options = program_options(my program);
        bool capture_suggestions = options->capture_suggestions;
        bool case_insensitive = options->case_insensitive_file_names;
        compiler_options_drop(options);
        *options = compiler_options_init();
        options->capture_suggestions = capture_suggestions;
        options->case_insensitive_file_names = case_insensitive;
        // (A command line only option the config's parsing depends on: content mappers without it are TS18068)
        for (isize j = 0; j + 1 < vec_cstr_size(&my directives); j += 2) {
            if (!strcasecmp(cstr_str(vec_cstr_at(&my directives, j)), "runexternalcode")) {
                ignore compiler_options_set(options, c_sv("runexternalcode"), cstr_sv(vec_cstr_at(&my directives, j + 1)));
            }
        }
        bool loaded = true;
        TsconfigDiagnostics diagnostics = {0};
        try (tsconfig_load_diagnosed(mock_fs_get_fs(my fs), cstr_sv(&i->path), program_options(my program),
            &my tsconfig_roots, &diagnostics)) if_err (e) {
            ignore e;
            current_error_destroy();
            loaded = false;
        }
        if (loaded) {
            my has_tsconfig = true;
            i->config_source = program_set_config_file(my program, cstr_sv(&i->path));
            // Its parsing diagnostics (those of a config it extends have no location here). The loader names the
            // config absolute, the test by its own spelling (`/tsconfig.json`, `tsconfig.json`)
            cstr harness_cwd = cstr_null;
            try (fs_get_cwd(mock_fs_get_fs(my fs), &harness_cwd)) or_panic;
            cstr own = absolute_path_of(cstr_sv(&harness_cwd), cstr_sv(&i->path));
            if (fs_is_directory(mock_fs_get_fs(my fs), cstr_sv(&own))) cstr_append_lit(&own, "/tsconfig.json");
            for (u32 j = 0; j < diagnostics.count; j++) {
                TsconfigDiagnostic* d = &diagnostics.items[j];
                Error* error = cstr_eq(&d->config, &own) ?
                    program_config_error(my program, d->start, d->end, d->code, d->message) :
                    error_ctx_global_error_s(program_errors(my program), d->code, d->message);
                error->phase = ERROR_PHASE_PARSE;
                d->message = cstr_null;
            }
            c_drop(cstr, &harness_cwd, &own);
        }
        tsconfig_diagnostics_drop(&diagnostics);
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
    // (tsgo's harness doesn't truncate types in messages unless the test says so: compileFiles' default)
    if (!program_options(my program)->is_set[COMPILER_OPTION_no_error_truncation]) {
        ignore compiler_options_set(program_options(my program), c_sv("noerrortruncation"), c_sv("true"));
    }
    // (tsc's harness skips checking the default lib unless a test says otherwise)
    if (!program_options(my program)->is_set[COMPILER_OPTION_skip_default_lib_check]) {
        ignore compiler_options_set(program_options(my program), c_sv("skipdefaultlibcheck"), c_sv("true"));
    }
    my skipped = tsgo_skips_configuration(program_options(my program));
    compiler_options_resolve(program_options(my program));
    cstr_take(&my lib_list, no_default_lib ? cstr_null : compiler_options_lib_files(program_options(my program)));
    for (csview rest = cstr_sv(&referenced_libs); rest.size && cstr_size(&my lib_list);) {
        isize end = csview_find_sv(rest, c_sv(","));
        csview single = csview_slice(rest, 0, end);
        rest = end == c_NPOS ? c_sv("") : csview_slice(rest, end + 1, INTPTR_MAX);
        // (Once: the list is the cache's key)
        cstr listed = cstr_d(",", my lib_list, ",");
        cstr needle = cstr_d(",", single, ",");
        if (cstr_find(&listed, cstr_str(&needle)) == c_NPOS) { cstr_append_lit(&my lib_list, ","); cstr_append_sv(&my lib_list, single); }
        cstr_drop(&listed);
        cstr_drop(&needle);
    }
    cstr_drop(&referenced_libs);

    my did_run = true;
    return ok;
}

static BaselineTestFile* find_file(BaselineTestCase* me, SourceFile const* source) {
    panic_if(!source, "Tests detected span with null source");
    for_each (i, vec_baseline_test_file, &my files) {
        if (i->source == source || (i->config_source && i->config_source == source)) {
            return i;
        }
    }
    // A file the test didn't write: reached through a symlink by a path we didn't expect, say
    log_error("failed to find test for source file ", source->canonical_path);
    return nullptr;
}

// The name the baselines' headers give the file: as the test writes it
static const char* baseline_test_file_header(BaselineTestFile const* file) {
    char const* ret = cstr_str(cstr_size(&file->header) ? &file->header : &file->path);
    // (tsgo's harness normalizes the unit name: `// @filename: ./b.ts` is `b.ts` there too)
    return strncmp(ret, "./", 2) == 0 ? ret + 2 : ret;
}

static const char* error_file_name(SourceFile const* source) {
    char const* ret = source->display_path_override ? source->display_path_override : cstr_str(&source->canonical_path);
    // (`// @filename: ./b.ts` is `b.ts` in the diagnostics, the file's normalized name)
    return strncmp(ret, "./", 2) == 0 ? ret + 2 : ret;
}

static void remove_test_path_prefixes(cstr* target, isize from);

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
        cstr_append_d(global_target, DEFAULT_DISPLAY, error_category_name(error), " TS", error->ts_code, ": ");
    } else if (indent_level == 0) {
        unsigned line = span_line_index(error->span);
        unsigned col = span_col_index(error->span);
        const char* filename = span_source(error->span) ? error_file_name(span_source(error->span)) : "(nil)";

        if (span_source(error->span) && span_source(error->span)->is_stdlib) {
            // (tsc's harness: a lib file's errors without a position)
            csview name = basename_of(cstr_sv(&span_source(error->span)->canonical_path));
            cstr_append_d(
                global_target, DEFAULT_DISPLAY,
                csview_starts_with(name, "lib.") ? "" : "lib.", name, "(--,--): ", error_category_name(error), " TS",
                error->ts_code, ": "
            );
        } else if (strlen(filename) >= 8 && !strncasecmp(filename, "lib", 3) &&
            !strcasecmp(filename + strlen(filename) - 5, ".d.ts")
        ) {
            // (tsc's harness masks the position of any summary line naming a `lib*.d.ts`, a test's own file too:
            // /^(lib.*\.d\.ts)\(\d+,\d+\)/gim)
            cstr_append_d(global_target, DEFAULT_DISPLAY, filename, "(--,--): ", error_category_name(error), " TS", error->ts_code, ": ");
        } else {
            cstr_append_d(
                global_target, DEFAULT_DISPLAY,
                filename, "(", line + 1, ",", col + 1, "): ", error_category_name(error), " TS", error->ts_code, ": "
            );
        }
    }

    if (indent_level > 0 && error->detached) {
        cstr_append_d(file_target, DEFAULT_DISPLAY, "\r\n!!! related TS", error->ts_code, ": ", error->message);
        return;
    }

    // Add toplevel errors and non-related errors (sub-errors that don't have a valid span) to the global string
    // (tsgo's harness removes the test path prefixes from messages too: `File '/.src/a.ts'` is `File 'a.ts'`)
    if (indent_level == 0 || !span_valid(error->span)) {
        isize from = cstr_size(global_target);
        cstr_append_d(global_target, DEFAULT_DISPLAY, repeated_d("  ", indent_level), error->message, "\r\n");
        remove_test_path_prefixes(global_target, from);
    }

    if (indent_level == 0 || !span_valid(error->span)) {
        // Handle toplevel errors and non-related errors (sub-errors that don't have a valid span)
        isize from = cstr_size(file_target);
        cstr_append_d(
            file_target, DEFAULT_DISPLAY,
            "\r\n!!! ", error_category_name(toplevel), " TS", toplevel->ts_code, ": ", repeated_d("  ", indent_level),
            error->message
        );
        remove_test_path_prefixes(file_target, from);
    } else {
        // Handle related errors (sub-errors that do have a valid span): in their own file, lib files without a position
        SourceFile const* related_source = span_source(error->span) ? span_source(error->span) : current_source;
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
            cstr_append_d(file_target, DEFAULT_DISPLAY, "\r\n!!! related TS", error->ts_code, " ");
            // (the harness removes the test path prefixes from the location: `/.lib/react16.d.ts` is `react16.d.ts`)
            isize location = cstr_size(file_target);
            cstr_append_d(file_target, DEFAULT_DISPLAY, error_file_name(related_source), ":", related_line + 1, ":", related_col + 1);
            remove_test_path_prefixes(file_target, location);
            cstr_append_d(file_target, DEFAULT_DISPLAY, ": ", error->message);
        }
    }

    // Traverse down the tree handling sub-errors and related errors
    for_each (related, vec_error, &error->sub_errors) {
        format_error_tree(global_target, file_target, toplevel, related, current_source, indent_level + 1);
    }
}

// The byte length of the JavaScript whitespace character (a regular expression's `\s`) at c, 0 if it isn't one
static unsigned js_whitespace_length(const char* c, const char* end) {
    if (*c == ' ' || *c == '\t' || *c == '\v' || *c == '\f') return 1;
    const unsigned char* u = (const unsigned char*)c;
    if (u[0] == 0xC2 && end - c >= 2 && u[1] == 0xA0) return 2;
    if (end - c < 3 || (u[0] & 0xF0) != 0xE0) return 0;
    u32 cp = ((u[0] & 0x0Fu) << 12) | ((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu);
    bool space = cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F ||
        cp == 0x205F || cp == 0x3000 || cp == 0xFEFF;
    return space ? 3 : 0;
}

// A diagnostic of a content-mapped file: in the transformed text, or the mapper's own in the original
static bool error_in_content_mapped_file(Program const* program, Error const* error) {
    SourceFile const* file = span_valid(error->span) ? span_source(error->span) : nullptr;
    if (!file) return false;
    // (A supplemental output's are the errors baseline's: tsgo leaves out the canonical files only)
    if (file->content_mapped) return !file->content_mapped->canonical;
    for (u32 i = 0; i < program_source_count(program); i++) {
        ContentMapped const* mapped = program_source_at(program, i)->content_mapped;
        if (mapped && !mapped->canonical && mapped->original == file) return true;
    }
    return false;
}

// Strips the ANSI colours error_format_pretty() writes (tsgo's baseline formats with colour and strips it too)
static void strip_ansi(cstr* text) {
    cstr out = cstr_null;
    char const* s = cstr_str(text);
    for (isize i = 0; i < cstr_size(text); i++) {
        if (s[i] == 0x1b && i + 1 < cstr_size(text) && s[i + 1] == '[') {
            i += 2;
            while (i < cstr_size(text) && ((s[i] >= '0' && s[i] <= '9') || s[i] == ';')) i++;
            continue;
        }
        cstr_append_n(&out, s + i, 1);
    }
    cstr_take(text, out);
}

// tsgo's DoContentMapperBaseline (tsbaseline/contentmapper_baseline.go): each content-mapped file's original and
// transformed text, then their diagnostics as FormatDiagnosticsWithColorAndContext prints them (mapped back to the
// original text, content_mapped_map_error()). Those diagnostics leave the errors baseline, and so do the files
static void render_content_mapper_baseline(BaselineTestCase* me) {
    Program const* program = my program;
    cstr* out = &my content_mapper_baseline;
    for (u32 i = 0; i < program_source_count(program); i++) {
        SourceFile const* file = program_source_at(program, i);
        ContentMapped* mapped = file->content_mapped;
        if (!mapped || mapped->canonical) continue;
        mapped->original->display_path_override = file->display_path_override;
        for_each (f, vec_baseline_test_file, &my files) {
            if (f->source == file) f->content_mapped = true;
        }
        // (The ScriptKind of the virtual extension; Go's %v of the mapper's extensions)
        csview ext = cstr_sv(&mapped->virtual_extension);
        char const* kind = csview_equals(ext, ".json") ? "ScriptKindJSON" : csview_equals(ext, ".tsx") ? "ScriptKindTSX" :
            csview_equals(ext, ".jsx") ? "ScriptKindJSX" : csview_ends_with(ext, "js") ? "ScriptKindJS" : "ScriptKindTS";
        isize from = cstr_size(out);
        cstr_append_d(out, DEFAULT_DISPLAY, "//// [", error_file_name(file), "] (ScriptKind: ", kind, ", ContentMapper: [");
        for (isize j = 0; j < mapped->mapper->extensions.count; j++) {
            if (j) cstr_append_lit(out, " ");
            cstr_append_s(out, mapped->mapper->extensions.items[j]);
        }
        cstr_append_lit(out, "])\n--- Original ---\n");
        remove_test_path_prefixes(out, from);
        cstr_append_s(out, mapped->original->contents);
        if (cstr_size(&mapped->original->contents) && !cstr_ends_with(&mapped->original->contents, "\n")) {
            cstr_append_lit(out, "\n");
        }
        cstr_append_lit(out, "--- Transformed ---\n");
        cstr_append_s(out, file->contents);
        if (cstr_size(&file->contents) && !cstr_ends_with(&file->contents, "\n")) cstr_append_lit(out, "\n");
        cstr_append_lit(out, "\n");
    }
    if (!cstr_size(out)) return;
    for (u32 i = 0; i < program_source_count(program); i++) {
        SourceFile* file = program_source_at(program, i);
        if (file->content_mapped && file->content_mapped->canonical) {
            file->display_path_override = file->content_mapped->canonical->display_path_override;
        }
    }
    cstr_append_lit(out, "=== Diagnostics ===\n\n");
    VecError* errors = &my error_ctx->errors;
    cstr rendered = cstr_null;
    for (isize i = 0; i < vec_error_size(errors);) {
        Error* error = &errors->data[i];
        if (!error_in_content_mapped_file(program, error)) {
            i++;
            continue;
        }
        if (content_mapped_map_error(error)) {
            if (cstr_size(&rendered)) cstr_append_lit(&rendered, "\n");
            error_format_pretty(&rendered, error, "\n", error_file_name);
        }
        vec_error_erase_n(errors, i, 1);
    }
    my content_mapper_errors = cstr_size(&rendered) > 0;
    if (!cstr_size(&rendered)) {
        cstr_append_lit(out, "<no content>\n");
    } else {
        strip_ansi(&rendered);
        isize from = cstr_size(out);
        cstr_append_s(out, rendered);
        remove_test_path_prefixes(out, from);
    }
    cstr_drop(&rendered);
    // The supplemental outputs' diagnostics stay in the errors baseline, mapped back the same way
    for (isize i = 0; i < vec_error_size(errors);) {
        if (content_mapped_map_error(&errors->data[i])) i++;
        else vec_error_erase_n(errors, i, 1);
    }
}

static void process_errors(BaselineTestCase* me) {
    if (error_ctx_empty(my error_ctx)) {
        return;
    }
    if (my pretty) {
        // (the minimal list is made below all the same, into a string that is thrown away)
        // (tsgo's FormatDiagnosticsWithColorAndContext: a line break between two)
        for_each (error, vec_error, &my error_ctx->errors) {
            if (cstr_size(&my formatted_pretty_errors)) cstr_append_lit(&my formatted_pretty_errors, "\r\n");
            error_format_pretty(&my formatted_pretty_errors, error, "\r\n", error_file_name);
        }
        remove_test_path_prefixes(&my formatted_pretty_errors, 0);
    }

    SourceFile const* current_source = nullptr;
    BaselineTestFile* current_test_file = nullptr;
    // A lib file's errors (a user declaration conflicting with one there) are only listed, after the others
    cstr lib_errors = cstr_null;

    for_each (error, vec_error, &my error_ctx->errors) {
        if (!span_valid(error->span)) {
            // A diagnostic without a file: listed before the first file
            format_error_tree(&my formatted_errors, &my formatted_global_errors, error, error, nullptr, 0);
            continue;
        }
        if (span_source(error->span) && span_source(error->span)->is_stdlib) {
            cstr discarded = cstr_null;
            format_error_tree(&lib_errors, &discarded, error, error, span_source(error->span), 0);
            cstr_drop(&discarded);
            continue;
        }

        if (span_source(error->span) != current_source) {
            current_source = span_source(error->span);
            current_test_file = find_file(me, current_source);
        }

        if (!current_test_file) {
            // (A content mapper's supplemental output, or its original text: listed, but no file of the test)
            if (current_source && (current_source->content_original || current_source->content_mapped)) {
                cstr discarded = cstr_null;
                format_error_tree(&my formatted_errors, &discarded, error, error, current_source, 0);
                cstr_drop(&discarded);
            }
            continue;
        }

        // Like tsc's harness (iterateErrorBaseline): a squiggle under every line the error covers, the message after
        // the last one. Lines of several errors interleave, so items are kept ordered by line
        unsigned line = span_line_index(error->span);
        const char* file_start = source_file_start(current_source);
        const char* start = span_start_ptr(error->span);
        const char* end = span_end_ptr(error->span);
        const char* line_start = start;
        while (line_start > file_start && line_start[-1] != '\n' && line_start[-1] != '\r') line_start--;
        // (A NUL byte is a character of the file, not its end)
        const char* file_end = file_start + cstr_size(&current_source->contents);
        while (true) {
            const char* line_end = line_start;
            while (line_end < file_end && *line_end != '\n' && *line_end != '\r') line_end++;
            const char* next_line_start = line_end;
            if (*next_line_start == '\r') next_line_start++;
            if (*next_line_start == '\n') next_line_start++;
            bool is_last_line = line_end == file_end;

            LineNumberedStr item = { .line = line, .str = cstr_null };
            const char* squiggle_start = start > line_start ? start : line_start;
            cstr_append_lit(&item.str, "    ");
            // (one per code point, like tsgo's harness; its `\S` replacement keeps whitespace as it is, which in Go's
            // RE2 is ASCII only: a tab stays, a no-break space is a space)
            // (A byte that isn't UTF-8 is a code point of its own, Go's RuneError)
            for (const char* c = line_start; c < squiggle_start && c < line_end; c += utf8_rune_size(c)) {
                if (*c == '\t' || *c == '\f' || *c == '\r' || *c == ' ') {
                    cstr_append_n(&item.str, c, 1);
                    continue;
                }
                cstr_append_lit(&item.str, " ");
            }
            const char* squiggle_end = end < line_end ? end : line_end;
            for (const char* c = squiggle_start; c < squiggle_end; c += utf8_rune_size(c)) cstr_append_lit(&item.str, "~");
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
    cstr_append_s(&my formatted_errors, lib_errors);
    cstr_drop(&lib_errors);
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
csview baseline_test_lib_dir = {0};
u32 baseline_test_threads = 0;
bool baseline_test_defer_bodies = false;
i64 baseline_test_checker_memory = -1;

static BaselineTestCase run_baseline_test_case_with(
    BaselineTestCache* cache,
    csview source_path,
    csview config,
    bool noisy,
    bool show_logs,
    bool defer_bodies
);

BaselineTestCase run_baseline_test_case(
    BaselineTestCache* cache,
    csview source_path,
    csview config,
    bool noisy,
    bool show_logs
) {
    BaselineTestCase ret = run_baseline_test_case_with(cache, source_path, config, noisy, show_logs, baseline_test_defer_bodies);
    // (A deferred body's materialization that tells it from the eager parse and walk: again without, like the CLI)
    if (baseline_test_defer_bodies && ret.program && program_defer_failed(ret.program)) {
        baseline_test_case_drop(&ret);
        defer_stat_add(DEFER_STAT_RESTARTS, 1);
        ret = run_baseline_test_case_with(cache, source_path, config, noisy, show_logs, false);
    }
    return ret;
}

static BaselineTestCase run_baseline_test_case_with(
    BaselineTestCache* cache,
    csview source_path,
    csview config,
    bool noisy,
    bool show_logs,
    bool defer_bodies
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
    program_set_threads(program, baseline_test_threads);
    program_set_defer_bodies(program, defer_bodies);
    if (baseline_test_checker_memory >= 0) program_set_checker_memory(program, (u64)baseline_test_checker_memory << 20);

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
    if (ret.skipped) {
        logger_config.threshold = prev_threshold;
        return ret;
    }

    // A test that checks the default library (`@skipDefaultLibCheck: false`) loads it itself, like the CLI: the cached
    // programs' library files are unchecked, and their declarations aren't merged with the test's (a merged interface's
    // first declaration, checked in the library, has to see the test's declarations)
    // (So does one with libReplacement: its libs come from the test's node_modules/@typescript/lib-*, and its own
    // `/// <reference lib>` adds to them)
    CompilerOptions const* options = program_options(program);
    if (options->lib_replacement) program_follow_lib_references(program);
    if ((!options->skip_default_lib_check && !options->skip_lib_check && !options->no_check) || options->lib_replacement) {
        // (In the test's file system: a directory of its own that no test path names)
        mount_test_lib_folder(&ret, c_sv("thirdparty/lib"), c_sv("/.tttt-default-lib"));
        program_set_lib_dir(program, c_sv("/.tttt-default-lib"));
        for (csview rest = cstr_sv(&ret.lib_list); rest.size;) {
            isize end = csview_find_sv(rest, c_sv(","));
            csview single = csview_trim_whitespace(csview_slice(rest, 0, end));
            rest = end == c_NPOS ? c_sv("") : csview_slice(rest, end + 1, INTPTR_MAX);
            if (!single.size) continue;
            cstr name = cstr_from_sv(single);
            cstr_lowercase(&name);
            try (SourceFile* lib, program_load_stdlib(program, cstr_sv(&name), &lib)) or_log(info);
            ignore lib;
            cstr_drop(&name);
        }
    } else {
        Program* lib_program = baseline_test_cache_get_program_with_libs(cache, cstr_sv(&ret.lib_list));
        program_copy_globals_from(program, lib_program);
    }

    // (The only root, if any, was written first: load_test_source())
    bool last_is_only_root = ret.last_is_only_root && !ret.has_tsconfig;

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
        // (By name: a later copy of the root's `@filename` is the file)
        bool is_root = !last_is_only_root || cstr_eq(&i->path, &ret.files.data[0].path);
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
        // (tsc's harness makes every file but a .json or .tsbuildinfo a root: a `.map` or `.txt` one is TS6054)
        if (!is_root || i->superseded || csview_ends_with(cstr_sv(&i->path), ".json") ||
            csview_ends_with(cstr_sv(&i->path), ".tsbuildinfo")
        ) {
            continue;
        }
        // These were all just written into our mock filesystem, so failing to load one is a bug on our end
        try (SourceFile* source, program_load(program, cstr_sv(&i->path), &source)) or_panic;
        // (A root without extension may stand for another file, `e` for e.ts: that one is shown as itself)
        if (source && !csview_equals_sv(basename_of(cstr_sv(&source->canonical_path)), basename_of(cstr_sv(&i->path)))) source = nullptr;
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
        if (i->superseded) continue;
        if (!i->source && (path_is_script(cstr_sv(&i->path)) || csview_ends_with(cstr_sv(&i->path), ".json"))) {
            i->source = program_find_source(program, cstr_sv(&i->path));
        }
        // (Only now: the paths are final)
        if (i->config_source) ((SourceFile*)i->config_source)->display_path_override = cstr_str(&i->path);
        if (i->source) {
            ((SourceFile*)i->source)->display_path_override = cstr_str(&i->path);
        } else {
            // package.json, tsconfig.json, .map files and so on, and scripts nothing refers to: they exist in the
            // filesystem but aren't part of the program, the errors baseline still shows them
            try (fs_read_file(mock_fs_get_fs(fs), cstr_sv(&i->path), &i->raw_contents)) or_panic;
        }
    }
    // The second copy of a package: walked as the first (BaselineTestFile.redirect_to)
    for (u32 k = 0, count = program_source_count(program); k < count; k++) {
        SourceFile const* file = program_source_at(program, k);
        for (u32 r = 0; r < file->module_ref_count; r++) {
            ModuleRef const* ref = file->module_refs[r];
            if (!module_ref_rare(ref)->redirected_from || !ref->file) continue;
            for_each (i, vec_baseline_test_file, &ret.files) {
                if (i->source && i->source == module_ref_rare(ref)->redirected_from) i->redirect_to = ref->file;
            }
        }
    }
    // A file of the program the test doesn't write, outside the current directory (the other path of a symlink): tsc
    // names it by its absolute path, not relative to /.src
    // (Pointers into the vector once it's complete: pushing moves the strings)
    for (int pass = 0, named = 0; pass < 2; pass++) {
        for (u32 i = 0, count = program_source_count(program); i < count; i++) {
            SourceFile* source = program_source_at(program, i);
            if (source->display_path_override || source->is_stdlib || !csview_starts_with(cstr_sv(&source->canonical_path), "../")) continue;
            if (pass == 1) {
                source->display_path_override = cstr_str(vec_cstr_at(&ret.display_paths, named++));
                continue;
            }
            cstr cwd = cstr_null;
            try (fs_get_cwd(mock_fs_get_fs(fs), &cwd)) or_panic;
            vec_cstr_push(&ret.display_paths, absolute_path_of(cstr_sv(&cwd), cstr_sv(&source->canonical_path)));
            cstr_drop(&cwd);
        }
    }

    // tsc reports type errors next to parse errors (getPreEmitDiagnostics() is all of them)
    program_resolve_names(program);
    // (tsgo's harness emits unless noEmit: the `.js` baseline's declaration files, the dts component)
    program_set_emit_declarations(program, !program_options(program)->no_emit);
    if (!program_options(program)->new_line) program_options(program)->new_line = 1; // (the harness's default)
    program_typecheck(program);
    if (program_defer_failed(program)) {
        logger_config.threshold = prev_threshold;
        return ret;
    }

    set_compiler_options(program_options(program));
    ret.pretty = program_options(program)->pretty;
    // What the checkers report while the walk below asks for types isn't a diagnostic of the program: tsc's harness
    // takes the errors baseline before its type walk, and the CLI never walks. Counting them would hide checks that
    // only happen when something prints a type (a generator's inferred return type, say)
    ErrorCtx walk_errs = error_ctx_scratch(errs);
    walk_errs.parent = errs;
    errs->frozen = true;
    for (u32 c = 0; c < program_checker_count(program); c++) program_checker_at(program, c)->errs = &walk_errs;
    // (After emit: what declaration emit painted is visible, see Checker.painted_kept)
    u32 kept_count = 0;
    for (u32 c = 0; c < program_checker_count(program); c++) kept_count += program_checker_at(program, c)->painted_kept_count;
    Symbol const** kept = kept_count ? malloc(kept_count * sizeof(Symbol const*)) : nullptr;
    for (u32 c = 0, at = 0; c < program_checker_count(program); c++) {
        Checker const* checker = program_checker_at(program, c);
        for (u32 j = 0; j < checker->painted_kept_count; j++) kept[at++] = checker->painted_kept[j];
    }
    symbol_chain_set_kept_painted(kept, kept_count);
    // (@noTypesAndSymbols: there are no .types and .symbols to compare with, and printing every type in full can take
    // long: tsgo's declarationEmitOversizedCachedType has one of 3^13 properties)
    // (Recycled checkers took the expression types with them: --checker-memory compares diagnostics only)
    for_each (i, vec_baseline_test_file, &ret.files) {
        if (!i->source || ret.no_types_and_symbols || baseline_test_checker_memory >= 0) continue;
        // PERF: perhaps mark the file as "current" so we don't have to look it up
        ret.enclosing = i->source->scope;
        // (Types as the checker that checked the file knows them, whatever the checker count)
        checker_make_current(program_checker_of(program, i->source));
        block_compile_test_info(&i->source->ast, &ret);
    }
    checker_make_current(program_get_checker(program));
    symbol_chain_set_kept_painted(nullptr, 0);
    free(kept);
    clear_compiler_options();
    for (u32 c = 0; c < program_checker_count(program); c++) program_checker_at(program, c)->errs = errs;
    errs->frozen = false;
    if (noisy && !error_ctx_empty(&walk_errs)) {
        log_warn("the type walk reported ", vec_error_size(&walk_errs.errors), " diagnostics the checking didn't (dropped)");
    }
    error_ctx_drop(&walk_errs);

    render_content_mapper_baseline(&ret);
    process_errors(&ret);

    logger_config.threshold = prev_threshold;

    return ret;
}

void baseline_test_case_drop(BaselineTestCase* me) {
    for (u32 i = 0; i < my merged_declaration_count; i++) vec_span_drop(&my merged_declarations[i].declarations);
    program_destroy(my program);
    mock_fs_destroy(my fs);
    error_ctx_drop(my error_ctx);
    free(my error_ctx);
    cstr_drop(&my lib_list);
    cstr_drop(&my lib_files);
    cstr_drop(&my formatted_errors);
    cstr_drop(&my formatted_pretty_errors);
    cstr_drop(&my content_mapper_baseline);
    cstr_drop(&my formatted_global_errors);
    vec_baseline_test_file_drop(&my files);
    vec_cstr_drop(&my links);
    vec_cstr_drop(&my display_paths);
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
// A types or symbols baseline's file header: tsgo's walker prints the program's file name, its slashes normalized
static void append_file_header(cstr* target, const char* source_path) {
    cstr_append_lit(target, "=== ");
    for (const char* c = source_path; *c; c++) cstr_append_n(target, *c == '\\' ? "/" : c, 1);
    cstr_append_lit(target, " ===\r\n");
}

static void render_types_or_symbols_file(
    cstr* target, const char* source_path, csview source_contents, VecLineNumberedStr const* lines, bool intrinsic_anys
) {
    append_file_header(target, source_path);
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
        if (intrinsic_anys && i->intrinsic_at) {
            append_intrinsic_any(target, i);
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
    bool intrinsic_anys
) {
    const char* line_indent = is_errors ? "    " : "";
    unsigned line = 0;
    unsigned index = 0;
    intptr_t vec_size = vec_line_numbered_str_size(lines);
    bool newline_if_next_line_non_empty = false;

    if (!is_errors) {
        append_file_header(target, source_path);
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

    // tsc's harness (iterateErrorBaseline) splits the source on \n, and only a file without any on \r: then a lone \r
    // is the line break
    cstr lone_cr_lines = cstr_null;
    if (is_errors && !memchr(source_contents.buf, '\n', (size_t)source_contents.size) &&
        memchr(source_contents.buf, '\r', (size_t)source_contents.size)
    ) {
        lone_cr_lines = cstr_from_sv(source_contents);
        char* c = cstr_data(&lone_cr_lines);
        for (isize i = 0; i < source_contents.size; i++) if (c[i] == '\r') c[i] = '\n';
        source_contents = cstr_sv(&lone_cr_lines);
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
            if (intrinsic_anys && added->intrinsic_at) {
                append_intrinsic_any(target, added);
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
    cstr_drop(&lone_cr_lines);
}

void baseline_test_case_render_errors(cstr* target, BaselineTestCase const* me) {
    if (error_ctx_empty(my error_ctx)) {
        return;
    }

    cstr_append_s(target, my pretty ? my formatted_pretty_errors : my formatted_errors);
    cstr_append_lit(target, "\r\n");
    if (cstr_size(&my formatted_global_errors)) {
        cstr_append_d(target, DEFAULT_DISPLAY, my formatted_global_errors);
    }

    for_each (i, vec_baseline_test_file, &my files) {
        if (i->content_mapped) continue;
        cstr_append_lit(target, "\r\n==== ");
        // (tsc's harness removes the test path prefixes from the name: `/.src/a.ts` is `a.ts`)
        isize header = cstr_size(target);
        cstr_append(target, baseline_test_file_header(i));
        remove_test_path_prefixes(target, header);
        cstr_append_d(target, DEFAULT_DISPLAY, " (", i->error_count, " errors) ====\r\n");

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
    if (my pretty) {
        isize from = cstr_size(target);
        error_format_pretty_summary(target, &my error_ctx->errors, "\r\n", error_file_name);
        remove_test_path_prefixes(target, from);
    }
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

// A unit whose name a later `@filename` repeats: tsc's harness shows its text with what the walk of the program's file
// of that name (the last copy) found at each line
static BaselineTestFile const* superseding_file(BaselineTestCase const* me, BaselineTestFile const* earlier) {
    BaselineTestFile const* ret = nullptr;
    if (earlier->redirect_to) {
        for_each (i, vec_baseline_test_file, &my files) {
            if (i->source == earlier->redirect_to) return i;
        }
        return nullptr;
    }
    for_each (i, vec_baseline_test_file, &my files) {
        if (i->source && cstr_eq(&i->path, &earlier->path)) ret = i;
    }
    return ret;
}

void baseline_test_case_render_symbols(cstr* target, BaselineTestCase const* me) {
    cstr_append_d(target, DEFAULT_DISPLAY, "//// [", relative_test_path(my source_path), "] ////\r\n\r\n");
    for_each (i, vec_baseline_test_file, &my files) {
        BaselineTestFile const* walked = i->superseded || i->redirect_to ? superseding_file(me, i) : i;
        if (!walked || !walked->source) continue;
        isize from = cstr_size(target);
        render_types_or_symbols_file(target, baseline_test_file_header(i),
            i->superseded ? cstr_sv(&i->raw_contents) : cstr_sv(&i->source->contents), &walked->symbols,
            false);
        remove_test_path_prefixes(target, from);
    }
}

bool baseline_test_case_has_errors(BaselineTestCase const* me) {
    return !error_ctx_empty(my error_ctx) || cstr_size(&my formatted_global_errors);
}

void baseline_test_case_render_types(cstr* target, BaselineTestCase const* me, bool had_errors) {
    cstr_append_d(target, DEFAULT_DISPLAY, "//// [", relative_test_path(my source_path), "] ////\r\n\r\n");
    for_each (i, vec_baseline_test_file, &my files) {
        BaselineTestFile const* walked = i->superseded || i->redirect_to ? superseding_file(me, i) : i;
        if (!walked || !walked->source) continue;
        isize from = cstr_size(target);
        render_types_or_symbols_file(target, baseline_test_file_header(i),
            i->superseded ? cstr_sv(&i->raw_contents) : cstr_sv(&i->source->contents), &walked->types,
            !had_errors);
        remove_test_path_prefixes(target, from);
    }
}

// The harness hides the positions in the libs and a test's own lib.d.ts: tsgo's isDefaultLibraryFile: `lib.*.d.ts` (6.0 matched `lib` anywhere in the name: `tslib.d.ts` too)
static bool is_lib_file_name(csview name) {
    return csview_starts_with(name, "lib.") && csview_ends_with(name, ".d.ts");
}

// A merge conflict marker at the start of a line (`<<<<<<<`, `|||||||`, `=======`, `>>>>>>>`): its character
static char conflict_marker_at(const char* line, const char* end) {
    if (end - line < 7) return 0;
    char c = line[0];
    if (c != '<' && c != '|' && c != '=' && c != '>') return 0;
    for (int i = 1; i < 7; i++) if (line[i] != c) return 0;
    if (end - line > 7 && line[7] == c) return 0;
    return c;
}

// From the start of a marker line back to the start of the closest earlier `marker` line, if the lines between have
// no other marker (the region it skips); else `from`
static const char* skip_back_to_marker(const char* file_start, const char* from, char marker) {
    const char* line = from;
    while (line > file_start) {
        const char* previous = line - 1;
        while (previous > file_start && previous[-1] != '\n') previous--;
        char found = conflict_marker_at(previous, from);
        if (found == marker) return previous;
        if (found) return from;
        line = previous;
    }
    return from;
}
// The byte length of the non-ASCII whitespace or line break (tsc's isWhiteSpaceLike) that ends at `end`, 0 if none
static u32 unicode_space_before(const char* file_start, const char* end) {
    for (u32 len = 2; len <= 3; len++) {
        if (end - len < file_start) return 0;
        const unsigned char* c = (const unsigned char*)end - len;
        u32 cp = len == 2 ? ((c[0] & 0x1Fu) << 6) | (c[1] & 0x3Fu) :
            ((c[0] & 0x0Fu) << 12) | ((c[1] & 0x3Fu) << 6) | (c[2] & 0x3Fu);
        bool lead = len == 2 ? (c[0] & 0xE0) == 0xC0 : (c[0] & 0xF0) == 0xE0;
        if (!lead) continue;
        if (cp == 0xA0 || cp == 0x85 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200B) || cp == 0x2028 || cp == 0x2029 ||
            cp == 0x202F || cp == 0x205F || cp == 0x3000 || cp == 0xFEFF
        ) {
            return len;
        }
    }
    return 0;
}


static void append_decl(cstr* target, Span span) {
    if (!span_valid(span) || !span_source(span)) {
        return;
    }
    if (cstr_size(&span_source(span)->decl_override)) {
        cstr_append_lit(target, ", Decl(");
        cstr_append_s(target, span_source(span)->decl_override);
        cstr_append_lit(target, ")");
        return;
    } else if (is_lib_file_name(basename_of(cstr_sv(&span_source(span)->canonical_path)))) {
        cstr_append_d(
            target, DEFAULT_DISPLAY,
            ", Decl(", basename_of(cstr_sv(&span_source(span)->canonical_path)), ", --, --)"
        );
        return;
    }
    const char* file_start = source_file_start(span_source(span));
    const char* start = span_start_ptr(span);
    bool keep_going = true;
    // A JSDoc tag (`@typedef`, `@property`...) starts at its `@`: tsc's JSDoc nodes have no leading trivia
    if (span_is_jsdoc_tag(span)) keep_going = false;
    // Inside a JSDoc comment (a member of a type in a tag) a line's leading `*` is trivia too (tsc's JSDoc scanner)
    bool in_jsdoc = false;
    if (keep_going && span_source(span)->is_js) {
        for (const char* c = start; c - 1 > file_start; c--) {
            if (c[-2] == '*' && c[-1] == '/') break;
            if (c[-2] == '/' && c[-1] == '*' && *c == '*') {
                in_jsdoc = true;
                break;
            }
        }
    }
    // The name after a JSDoc tag's type or a `@typedef` (`@typedef {T} A.B`: tsc's JSDocNamespaceDeclaration) has none either
    if (in_jsdoc) {
        const char* c = start;
        while (c > file_start && (c[-1] == ' ' || c[-1] == '\t')) c--;
        if (c > file_start && c[-1] == '}') keep_going = false;
        for (int t = 0; t < 2 && keep_going; t++) {
            const char* tag = t ? "@callback" : "@typedef";
            size_t len = strlen(tag);
            if (c - file_start >= (ptrdiff_t)len && !memcmp(c - len, tag, len)) keep_going = false;
        }
    }
    // The space before the symbol is included in the span. Why? I have no idea.
    // Here we parse code backwards, which is an extremely silly thing to do
    while (keep_going && start > file_start) {
        switch (start[-1]) {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
            case '\v':
            case '\f':
                start--;
                break;


            case '*':
                if (in_jsdoc) {
                    const char* line = start - 1;
                    while (line > file_start && (line[-1] == ' ' || line[-1] == '\t')) line--;
                    if (line == file_start || line[-1] == '\n') {
                        start--;
                        break;
                    }
                }
                goto other;

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

            default: other: {
                // (Non-ASCII whitespace is trivia too)
                u32 space = unicode_space_before(file_start, start);
                if (space) {
                    start -= space;
                    break;
                }
                keep_going = false;
                // Skip line comments, going backwards: the comment starts at the first `//` of the line (`///x`)
                const char* line_start = start - 1;
                while (line_start > file_start && line_start[-1] != '\n') line_start--;
                if (line_start == file_start && line_start[0] == '#' && line_start[1] == '!') {
                    start = file_start; // A shebang is trivia too
                    break;
                }
                // A merge conflict marker line is trivia, and so is what `=======` (and diff3's `|||||||`) skip up to
                // the next marker (tsc's scanConflictMarkerTrivia)
                char marker = conflict_marker_at(line_start, cstr_str(&span_source(span)->contents) + cstr_size(&span_source(span)->contents));
                if (marker) {
                    keep_going = true;
                    start = line_start;
                    if (marker == '>') start = skip_back_to_marker(file_start, start, '=');
                    if (marker == '>' || marker == '=') start = skip_back_to_marker(file_start, start, '|');
                    break;
                }
                // (Not a `//` in a string literal: `"https://x",`)
                char quote = 0;
                for (const char* c = line_start; c + 1 < start; c++) {
                    if (quote) {
                        if (c[0] == '\\') c++;
                        else if (c[0] == quote) quote = 0;
                        continue;
                    }
                    if (c[0] == '"' || c[0] == '\'' || c[0] == '`') {
                        quote = c[0];
                        continue;
                    }
                    if (c[0] == '/' && c[1] == '/') {
                        keep_going = true;
                        start = c;
                        break;
                    }
                }
            }   break;
        }
    }
    span_set_sv(&span, csview_from_n(start, 0));
    unsigned format_line = span_line_index(span);
    unsigned format_col = span_col_index(span);
    cstr_append_d(
        target, DEFAULT_DISPLAY,
        ", Decl(", basename_of(cstr_sv(&span_source(span)->canonical_path)), ", ", format_line, ", ", format_col, ")"
    );
}

// The line tsc's typeWriter files a node under: skipTrivia(node.pos). Our spans start after the trivia, except a
// missing node's (zero width, at the full start like tsc's)
static unsigned placement_line(Span span) {
    if (span.size || !span_source(span)) return span_line_index(span);
    const char* c = span.buf;
    const char* end = cstr_str(&span_source(span)->contents) + cstr_size(&span_source(span)->contents);
    while (c < end) {
        if (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n') c++;
        else if (c + 1 < end && c[0] == '/' && c[1] == '/') { while (c < end && *c != '\n') c++; }
        else if (c + 1 < end && c[0] == '/' && c[1] == '*') {
            c += 2;
            while (c + 1 < end && !(c[0] == '*' && c[1] == '/')) c++;
            c = c + 2 < end ? c + 2 : end;
        } else break;
    }
    span.buf = c;
    return span_line_index(span);
}

static void append_span_contents_without_newlines(cstr* target, Span span) {
    const char* start = span_start_ptr(span);
    const char* end = span_end_ptr(span);
    // tsc's `sourceText.replace(/\r?\n/g, "")`: a lone \r stays
    for (const char* c = start; c < end; c++) {
        if (*c == '\n' || (*c == '\r' && c + 1 < end && c[1] == '\n')) continue;
        cstr_append_n(target, c, 1);
    }
}

void baseline_test_case_add_symbol(BaselineTestCase* me, BaselineSym data) {
    if (my in_with) {
        cstr_drop(&data.text);
        vec_span_drop(&data.declarations);
        vec_span_drop(&data.namespaces);
        return;
    }
    unsigned line_for_placement = placement_line(data.span);
    for (u32 i = 0; i < my symbol_override_count; i++) {
        if (my symbol_overrides[i].span.buf != data.span.buf || my symbol_overrides[i].span.size != data.span.size) continue;
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

    for (u32 i = 0; i < my merged_declaration_count && vec_span_size(&data.declarations); i++) {
        Span first = *vec_span_at(&data.declarations, 0);
        bool member = false;
        for_each (j, vec_span, &my merged_declarations[i].declarations) {
            member |= j->buf == first.buf && j->size == first.size;
        }
        if (!member) continue;
        vec_span_drop(&data.declarations);
        data.declarations = vec_span_clone(my merged_declarations[i].declarations);
        cstr_drop(&data.text);
        data.text = cstr_from_sv(my merged_declarations[i].name);
        break;
    }

    cstr text = cstr_lit(">");
    append_span_contents_without_newlines(&text, data.span);
    cstr_append_lit(&text, " : Symbol(");

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
            if (i->size) cstr_append_span(&text, *i);
            else cstr_append_lit(&text, "unknown"); // (A missing name: tsc's unknownSymbol)
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
        Span name = span_valid(data.name) ? data.name : data.span;
        // (A name with an escape as written isn't identifier text to tsgo's printer: an element access, `C[a\u0062]`)
        bool escaped = has_namespace && memchr(name.buf, '\\', (size_t)name.size);
        if (has_namespace && !escaped) {
            cstr_append_lit(&text, ".");
        }
        if (escaped) {
            cstr_append_lit(&text, "[");
            cstr_append_span(&text, name);
            cstr_append_lit(&text, "]");
        } else if (name.size) {
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
    // (A name written over several lines, `[\r\n x\r\n]`, has `\n` line breaks: tsc's symbolToString of the
    // declaration name prints it again)
    if (memchr(cstr_str(&text), '\r', (size_t)cstr_size(&text))) {
        isize from = cstr_find(&text, " : Symbol(");
        if (from != c_NPOS) {
            cstr fixed = cstr_from_sv(csview_from_n(cstr_str(&text), from));
            for (const char* c = cstr_str(&text) + from; *c; c++) {
                if (c[0] == '\r' && c[1] == '\n') continue;
                cstr_append_n(&fixed, c, 1);
            }
            cstr_drop(&text);
            text = fixed;
        }
    }
    BaselineTestFile* test_file = find_file(me, span_source(data.span));
    vec_line_numbered_str_push(&test_file->symbols, (LineNumberedStr){
        .line = line_for_placement,
        .str = text,
    });
    vec_span_drop(&data.declarations);
    vec_span_drop(&data.namespaces);
    cstr_drop(&data.text);
}

// One `>text : type` line per parenthesization level of the expression
static void add_type_rendered(BaselineTestCase* me, Span outer_span, Span inner_span, csview rendered) {
    for (u32 i = 0; i < my hidden_type_span_count; i++) {
        if (my hidden_type_spans[i].buf == inner_span.buf && my hidden_type_spans[i].size == inner_span.size) {
            my any_keeps_rendering = false;
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
    // A `(` without its `)` is a parenthesized expression that ends with its operand (`( y = 1 ; 2 )`)
    bool unclosed = outer_start != inner_start && outer_last == inner_last;
    if (outer_start == inner_start) {
        outer_start = inner_start;
        outer_last = inner_last;
        base_case = true;
    }
    outer_span = span_make_in(inner_span, outer_start, outer_last + 1);

    intptr_t line = placement_line(outer_span);
    cstr target = cstr_lit(">");
    append_span_contents_without_newlines(&target, outer_span);
    cstr_append_lit(&target, " : ");
    bool intrinsic = !my any_keeps_rendering && (csview_equals(rendered, "any") || my unresolved_type);
    isize_t intrinsic_at = intrinsic ? cstr_size(&target) : 0;
    cstr_append_sv(&target, rendered);
    my any_keeps_rendering = false; // Only the node itself, not what's parenthesized in it
    BaselineTestFile* test_file = find_file(me, span_source(outer_span));
    vec_line_numbered_str_push(&test_file->types, (LineNumberedStr){
        .line = line,
        .str = target,
        .intrinsic_at = intrinsic_at,
        .error_type = my error_type && intrinsic,
    });
    if (!base_case) {
        outer_span.buf++;
        outer_span.size -= unclosed ? 1 : 2;
        add_type_rendered(me, outer_span, inner_span, rendered);
    }
}

void baseline_test_case_add_type_recursive(BaselineTestCase* me, Span outer_span, Span inner_span, Ty const* type) {
    if (my in_with) type = ty_intrinsic(current_checker, TY_ERROR);
    cstr rendered = cstr_null;
    TyParamNames param_names = { .in_scope = { .data = my scope_params, .len = my scope_params_len } };
    ty_display(current_checker, (TyDisplay){
        .target = &rendered, .enclosing = my enclosing, .types_baseline = true,
        .param_names = &param_names, .at = inner_span, .usage_ref = my usage_ref, .usage_attributes = my usage_attributes,
    }, type);
    ty_param_names_drop(&param_names);
    my error_type = (type->flags & TYF_ERROR) != 0;
    my unresolved_type = my error_type && type->kind == TY_INTRINSIC && type != ty_intrinsic(current_checker, TY_ERROR);
    add_type_rendered(me, outer_span, inner_span, cstr_sv(&rendered));
    my error_type = false;
    my unresolved_type = false;
    cstr_drop(&rendered);
}

void baseline_test_case_add_type_text(BaselineTestCase* me, Span span, csview text) {
    if (my in_with) {
        baseline_test_case_add_type(me, span, nullptr);
        return;
    }
    add_type_rendered(me, span, span, text);
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

cstr baseline_test_case_module_alias_text(BaselineTestCase const* me, Symbol const* declaration) {
    cstr text = cstr_init();
    // (A class expression a CommonJS module exports has no parent, its container is the module: containers_of())
    ClassDecl const* class_expression = declaration && (declaration->flags & SYMBOL_CLASS) ? symbol_find_node(declaration, SYMBOL_DECL_CLASS) : nullptr;
    bool commonjs_class = class_expression && class_expression->is_expression && class_expression->commonjs_module;
    if (declaration && ((declaration->parent && declaration->parent->is_module) || commonjs_class) && my enclosing) {
        SymbolChain chain = symbol_chain_at(my enclosing, declaration, 0, false);
        // (Only the module's own export, not an export specifier re-exporting it under another name: tsc doesn't
        // name `export { f as g } from "./self"` through `self.g` there. Why is unverified)
        // (Or an alias that is the declaration: `var wat = require("./self")` of `module.exports = class C {}`)
        // (The last is the export that is an alias of the class expression, `exports.D = class D {}`)
        Symbol const* last = chain.len ? chain.items[chain.len - 1] : nullptr;
        bool last_is_declaration = last == declaration ||
            (last && (last->flags & SYMBOL_ALIAS) && last->alias && last->alias->class_expr && last->alias->class_expr->symbol == declaration);
        if (chain.len && (chain.items[0]->flags & SYMBOL_ALIAS) &&
            (chain.len > 1 ? last_is_declaration : chain.items[0] != declaration)
        ) {
            symbol_chain_append(&text, my enclosing, &chain);
        }
    }
    return text;
}

// The declarations of a symbol as tsc's binder has them, where tttt's binder splits one of its symbols in two:
// - A local of a namespace block or module also has the exported declarations of its name in that block: tsc declares
//   an exported declaration in the container's locals too (declareModuleMember: the local symbol, with no meaning but
//   ExportValue, and its exportSymbol), so a local declaration of the same name merges into that local symbol. tttt
//   keeps the export out of the locals (lookups don't need it)
// - A function's type parameters are in the function's locals, with its parameters and the top level of its body
//   (`function f<A>(A: A)` is one symbol A); tttt declares them in a scope around the function's
// Only the declarations differ: the meanings and value declarations are tsc's already
VecSpan baseline_symbol_declaration_spans(Symbol const* me) {
    if (!me || !me->decl_count) return symbol_declaration_spans(me);
    // (An import is a local too, declared in the scope it resolves from)
    bool import = me->alias && me->is_local;
    Scope const* block = me->decls[0].block ? me->decls[0].block : import ? me->alias->scope : nullptr;
    if (!block) return symbol_declaration_spans(me);
    Symbol const* twin = nullptr;
    bool twin_same_block = false;
    if (me->decls[0].kind == SYMBOL_DECL_TYPE_PARAMETER && scope_rare(block)->type_params_function) {
        twin = symbol_table_get(scope_locals(scope_rare(block)->type_params_function), me->name);
    } else if (block->kind == SCOPE_FUNCTION && block->parent && scope_rare(block->parent)->type_params_function == block) {
        twin = symbol_table_get(scope_locals(block->parent), me->name);
        if (twin && !(twin->decl_count && twin->decls[0].kind == SYMBOL_DECL_TYPE_PARAMETER)) twin = nullptr;
    }
    // (A type of the body conflicts with the type parameter, TS2300: tsc declares it as a symbol of its own)
    if (twin && twin != me) {
        bool me_param = me->decls[0].kind == SYMBOL_DECL_TYPE_PARAMETER;
        bool twin_param = twin->decl_count && twin->decls[0].kind == SYMBOL_DECL_TYPE_PARAMETER;
        if (me_param != twin_param && ((me_param ? twin : me)->flags & SYMBOL_TYPE)) twin = nullptr;
    } else if (me->is_local || import) {
        Symbol const* container = block->container;
        twin = container && container->exports ? symbol_table_get(container->exports, me->name) : nullptr;
        twin_same_block = true;
    }
    if (!twin || twin == me) return symbol_declaration_spans(me);
    u32 count = 0;
    // (A type's local twin conflicts with a local it excludes, TS2300: tsgo's declareModuleMember doesn't add it)
    #define TWIN_DECL_TAKEN(decl) ((!twin_same_block || (decl)->block == block) && \
        !((decl)->kind == SYMBOL_DECL_TYPE_ALIAS && (me->flags & SYMBOL_TYPE_ALIAS_EXCLUDES & ~(u32)SYMBOL_TYPE_ALIAS)) && \
        !((decl)->kind == SYMBOL_DECL_INTERFACE && (me->flags & SYMBOL_INTERFACE_EXCLUDES & ~(u32)SYMBOL_INTERFACE)))
    for (u32 i = 0; i < twin->decl_count; i++) count += TWIN_DECL_TAKEN(&twin->decls[i]);
    if (!count) return symbol_declaration_spans(me);
    // (In the order the binder declares them: source order, see symbol_declaration_spans() for functions; a JavaScript
    // file's typedefs after everything else, tsgo's delayed binding of JSDoc declarations)
    SymbolDecl* decls = malloc(sizeof(SymbolDecl) * (me->decl_count + count));
    u32 len = 0;
    for (u32 i = 0; i < twin->decl_count; i++) if (TWIN_DECL_TAKEN(&twin->decls[i])) decls[len++] = twin->decls[i];
    #undef TWIN_DECL_TAKEN
    for (u32 i = 0; i < me->decl_count; i++) {
        // (A local function has the exported declarations of its block already, see scope_init_declaration())
        bool seen = false;
        for (u32 k = 0; k < len && !seen; k++) seen = decls[k].node && decls[k].node == me->decls[i].node;
        if (!seen) decls[len++] = me->decls[i];
    }
    #define LATE_DECL(decl) ((decl).kind == SYMBOL_DECL_TYPE_ALIAS && (decl).node && ((TypeDecl const*)(decl).node)->jsdoc)
    for (u32 i = 1; i < len; i++) {
        for (u32 j = i; j > 0 && (LATE_DECL(decls[j]) == LATE_DECL(decls[j - 1]) ?
            span_start_ptr(decls[j].full) < span_start_ptr(decls[j - 1].full) : LATE_DECL(decls[j - 1])); j--) {
            SymbolDecl t = decls[j];
            decls[j] = decls[j - 1];
            decls[j - 1] = t;
        }
    }
    #undef LATE_DECL
    Symbol merged = *me;
    merged.decls = decls;
    merged.decl_count = len;
    VecSpan ret = symbol_declaration_spans(&merged);
    free(decls);
    return ret;
}

void baseline_test_case_add_declaration(BaselineTestCase* me, Span span, Symbol const* declaration) {
    if (!declaration) {
        log_error("baseline_test_case_add_declaration() called with null declaration");
        return;
    }
    check(span_source(span));
    VecSpan declarations = baseline_symbol_declaration_spans(declaration);
    // (A script's declaration merged into a UMD global, a file's module: declared by that file first, like tsc's
    // clone of the module symbol)
    if (declaration->is_module && declaration->module && declaration->module->file) {
        SourceFile const* file = declaration->module->file;
        VecSpan with_file = vec_span_init();
        vec_span_push(&with_file, span_from_sv(file, csview_from_n(source_file_start(file), 0)));
        for_each (i, vec_span, &declarations) vec_span_push(&with_file, *i);
        vec_span_drop(&declarations);
        declarations = with_file;
    }
    baseline_test_case_add_symbol(me, (BaselineSym){
        .declarations = declarations,
        .span = span,
        .name = declaration->name_span,
        .text = baseline_test_case_module_alias_text(me, declaration),
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
    if (!value || (value->is_placeholder && !(value->is_unresolved && csview_equals(value->name, "undefined")))) {
        // (tsc's getTypeOfAlias: errorType, which shows as `error` where the harness prints its intrinsic name. What
        // a shorthand ambient module gives is any)
        bool shorthand = value && value->is_placeholder && !value->is_unresolved;
        baseline_test_case_add_type(me, span, ty_intrinsic(current_checker, shorthand ? TY_ANY : TY_ERROR));
        return;
    }
    // (the alias itself: `import * as ns` of an `export =` module can be its own type, see namespace_import_type())
    baseline_test_case_add_type(me, span, ty_type_of_symbol(current_checker, symbol));
}
