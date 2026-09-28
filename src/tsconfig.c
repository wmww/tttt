#include "tsconfig.h"
#include "json.h"

typedef struct ConfigLoad {
    Fs const* fs;
    CompilerOptions* options;
    cstr cwd;
    cstr config_dir; // Of the config that was asked for: what `${configDir}` means in every config it extends
    // `files` / `include` / `exclude` of the nearest config that has them, and the directory they are relative to
    JsonValue const* files;
    JsonValue const* include;
    JsonValue const* exclude;
    cstr files_dir, include_dir, exclude_dir;
    bool has_files_or_include;
    int depth;
    // Every config stays alive until the end: files/include/exclude point into them
    JsonValue* documents[32];
    isize document_count;
} ConfigLoad;

static cstr path_join(csview dir, csview rest) {
    if (rest.size && rest.buf[0] == '/') return absolute_path_of(c_sv("/"), rest);
    cstr combined = cstr_d(dir, "/", rest);
    cstr ret = absolute_path_of(c_sv("/"), cstr_sv(&combined));
    cstr_drop(&combined);
    return ret;
}

static csview parent_of(csview path) {
    isize i = path.size;
    while (i > 0 && path.buf[i - 1] != '/') i--;
    while (i > 1 && path.buf[i - 1] == '/') i--;
    return csview_slice(path, 0, i ? i : 1);
}

// A path written in a config: `${configDir}` is the directory of the outermost config, anything relative is relative
// to the config it is written in
static cstr config_path(ConfigLoad* me, csview dir, csview written) {
    static const char template[] = "${configDir}";
    if (csview_starts_with(written, template)) {
        csview rest = csview_slice(written, (isize)sizeof(template) - 1, written.size);
        while (rest.size && rest.buf[0] == '/') rest = csview_slice(rest, 1, rest.size);
        return path_join(cstr_sv(&my config_dir), rest);
    }
    return path_join(dir, written);
}

static void set_path_list(ConfigLoad* me, csview dir, const char* name, JsonValue const* value) {
    cstr joined = cstr_null;
    for (isize i = 0; i < value->count; i++) {
        if (value->items[i].tag != JSON_STRING) continue;
        cstr absolute = config_path(me, dir, cstr_sv(&value->items[i].string));
        if (cstr_size(&joined)) cstr_append_lit(&joined, ",");
        cstr_append_sv(&joined, cstr_sv(&absolute));
        cstr_drop(&absolute);
    }
    ignore compiler_options_set(my options, csview_from(name), cstr_sv(&joined));
    cstr_drop(&joined);
}

static void apply_compiler_options(ConfigLoad* me, csview dir, JsonValue const* compiler_options) {
    for (isize i = 0; i < compiler_options->count; i++) {
        const char* name = cstr_str(&compiler_options->members[i].key);
        JsonValue const* value = &compiler_options->members[i].value;
        if (!strcmp(name, "paths")) {
            if (value->tag != JSON_OBJECT) continue;
            compiler_options_clear_paths(my options);
            cstr_take(&my options->paths_base, cstr_from_sv(dir));
            for (isize j = 0; j < value->count; j++) {
                JsonValue const* substitutions = &value->members[j].value;
                if (substitutions->tag != JSON_ARRAY) continue;
                StrList list = {0};
                for (isize k = 0; k < substitutions->count; k++) {
                    if (substitutions->items[k].tag != JSON_STRING) continue;
                    // Relative to the baseUrl or the config, which only module resolution knows; `${configDir}` is ours
                    csview written = cstr_sv(&substitutions->items[k].string);
                    if (csview_starts_with(written, "${configDir}")) {
                        cstr absolute = config_path(me, dir, written);
                        str_list_push(&list, cstr_sv(&absolute));
                        cstr_drop(&absolute);
                    } else {
                        str_list_push(&list, written);
                    }
                }
                compiler_options_add_path(my options, cstr_sv(&value->members[j].key), list);
            }
            my options->paths_set = true;
        } else if (!strcmp(name, "baseUrl") || !strcmp(name, "outDir") || !strcmp(name, "outFile") ||
            !strcmp(name, "rootDir") || !strcmp(name, "declarationDir")) {
            // Relative to the config that has them, which may be one this one extends
            if (value->tag != JSON_STRING) continue;
            cstr absolute = config_path(me, dir, cstr_sv(&value->string));
            ignore compiler_options_set(my options, csview_from(name), cstr_sv(&absolute));
            cstr_drop(&absolute);
        } else if (!strcmp(name, "rootDirs") || !strcmp(name, "typeRoots")) {
            if (value->tag == JSON_ARRAY) set_path_list(me, dir, name, value);
        } else if (value->tag == JSON_BOOL) {
            ignore compiler_options_set(my options, csview_from(name), value->boolean ? c_sv("true") : c_sv("false"));
        } else if (value->tag == JSON_STRING) {
            ignore compiler_options_set(my options, csview_from(name), cstr_sv(&value->string));
        } else if (value->tag == JSON_ARRAY) {
            cstr joined = cstr_null;
            for (isize j = 0; j < value->count; j++) {
                if (value->items[j].tag != JSON_STRING) continue;
                if (cstr_size(&joined)) cstr_append_lit(&joined, ",");
                cstr_append_sv(&joined, cstr_sv(&value->items[j].string));
            }
            ignore compiler_options_set(my options, csview_from(name), cstr_sv(&joined));
            cstr_drop(&joined);
        }
    }
}

// What `"extends": "name"` names: a path, or a package (its `tsconfig` field, a file in it, or its tsconfig.json)
static bool resolve_extends(ConfigLoad* me, csview dir, csview name, cstr* out) {
    bool is_path = name.size && (name.buf[0] == '/' || name.buf[0] == '.');
    if (is_path) {
        cstr base = path_join(dir, name);
        if (!csview_ends_with(name, ".json")) {
            cstr with_extension = cstr_d(base, ".json");
            if (fs_is_reg_file(my fs, cstr_sv(&with_extension))) {
                cstr_drop(&base);
                *out = with_extension;
                return true;
            }
            cstr_drop(&with_extension);
        }
        if (fs_is_reg_file(my fs, cstr_sv(&base))) {
            *out = base;
            return true;
        }
        cstr_drop(&base);
        return false;
    }
    cstr at = cstr_from_sv(dir);
    while (true) {
        cstr package = cstr_d(at, "/node_modules/", name);
        const char* const suffixes[] = { "", ".json", "/tsconfig.json" };
        for (isize i = 0; i < (isize)c_arraylen(suffixes); i++) {
            cstr candidate = cstr_d(package, suffixes[i]);
            if (fs_is_reg_file(my fs, cstr_sv(&candidate))) {
                cstr_drop(&package);
                cstr_drop(&at);
                *out = candidate;
                return true;
            }
            cstr_drop(&candidate);
        }
        cstr package_json_path = cstr_d(package, "/package.json");
        cstr contents = cstr_null;
        bool found = false;
        if (fs_is_reg_file(my fs, cstr_sv(&package_json_path))) {
            bool readable = true;
            try (fs_read_file(my fs, cstr_sv(&package_json_path), &contents)) if_err (e) {
                ignore e;
                current_error_destroy();
                readable = false;
            }
            JsonValue package_json = {0};
            if (readable && json_parse(cstr_sv(&contents), &package_json)) {
                csview field = json_get_string(&package_json, "tsconfig");
                if (field.size) {
                    *out = path_join(cstr_sv(&package), field);
                    found = fs_is_reg_file(my fs, cstr_sv(out));
                    if (!found) cstr_drop(out);
                }
            }
            json_drop(&package_json);
            if (readable) cstr_drop(&contents);
        }
        cstr_drop(&package_json_path);
        cstr_drop(&package);
        if (found) {
            cstr_drop(&at);
            return true;
        }
        if (cstr_size(&at) <= 1) break;
        cstr parent = cstr_from_sv(parent_of(cstr_sv(&at)));
        cstr_take(&at, parent);
    }
    cstr_drop(&at);
    return false;
}

static_failable load_config(ConfigLoad* me, csview path) {
    if (my depth > 16 || my document_count == (isize)c_arraylen(my documents)) {
        return err_d(path, ": too many levels of 'extends' (a cycle?)");
    }
    cstr contents = cstr_null;
    try (fs_read_file(my fs, path, &contents)) bubble;
    JsonValue* document = c_new(JsonValue, {0});
    my documents[my document_count++] = document;
    bool parsed = json_parse(cstr_sv(&contents), document);
    cstr_drop(&contents);
    if (!parsed) return err_d(path, ": not valid JSON");

    cstr dir = cstr_from_sv(parent_of(path));
    // What is extended first, so that this config overrides it
    JsonValue const* extends = json_get(document, "extends");
    for (isize i = 0; extends && i < (extends->tag == JSON_ARRAY ? extends->count : 1); i++) {
        JsonValue const* name = extends->tag == JSON_ARRAY ? &extends->items[i] : extends;
        if (name->tag != JSON_STRING) continue;
        cstr base = cstr_null;
        if (!resolve_extends(me, cstr_sv(&dir), cstr_sv(&name->string), &base)) {
            cstr_drop(&dir);
            return err_d(path, ": file '", name->string, "' not found");
        }
        my depth++;
        try (load_config(me, cstr_sv(&base))) cleanup {
            cstr_drop(&base);
            cstr_drop(&dir);
        }
        my depth--;
        cstr_drop(&base);
    }

    JsonValue const* compiler_options = json_get_tagged(document, "compilerOptions", JSON_OBJECT);
    if (compiler_options) apply_compiler_options(me, cstr_sv(&dir), compiler_options);

    // `files` and `include` are inherited as a pair, `exclude` on its own
    JsonValue const* files = json_get_tagged(document, "files", JSON_ARRAY);
    JsonValue const* include = json_get_tagged(document, "include", JSON_ARRAY);
    JsonValue const* exclude = json_get_tagged(document, "exclude", JSON_ARRAY);
    if (files) {
        my files = files;
        cstr_assign_sv(&my files_dir, cstr_sv(&dir));
    }
    if (include) {
        my include = include;
        cstr_assign_sv(&my include_dir, cstr_sv(&dir));
    }
    if (exclude) {
        my exclude = exclude;
        cstr_assign_sv(&my exclude_dir, cstr_sv(&dir));
    }
    cstr_drop(&dir);
    return ok;
}

// ---- Globs ----

static bool segment_matches(csview pattern, csview name) {
    // `*` is any run of characters, `?` one. Iterative with one backtrack point
    isize p = 0, n = 0, star = -1, star_n = 0;
    while (n < name.size) {
        if (p < pattern.size && (pattern.buf[p] == '?' || pattern.buf[p] == name.buf[n])) {
            p++;
            n++;
        } else if (p < pattern.size && pattern.buf[p] == '*') {
            star = p++;
            star_n = n;
        } else if (star >= 0) {
            p = star + 1;
            n = ++star_n;
        } else {
            return false;
        }
    }
    while (p < pattern.size && pattern.buf[p] == '*') p++;
    return p == pattern.size;
}

static csview next_segment(csview* path) {
    isize end = csview_find_sv(*path, c_sv("/"));
    csview segment = csview_slice(*path, 0, end);
    *path = end == c_NPOS ? c_sv("") : csview_slice(*path, end + 1, path->size);
    return segment;
}

static bool is_implicitly_excluded(csview segment) {
    return csview_equals(segment, "node_modules") || csview_equals(segment, "bower_components") ||
        csview_equals(segment, "jspm_packages");
}

// Whether the absolute path matches the absolute pattern. With prefix, matching a leading part of the path is enough
// (an excluded directory excludes what is in it)
static bool glob_matches(csview pattern, csview path, bool prefix) {
    if (!pattern.size) return prefix || !path.size;
    if (!path.size) {
        // Only `**` can match nothing
        csview rest = pattern;
        while (rest.size) {
            if (!csview_equals(next_segment(&rest), "**")) return false;
        }
        return true;
    }
    csview pattern_rest = pattern;
    csview segment = next_segment(&pattern_rest);
    if (csview_equals(segment, "**")) {
        if (glob_matches(pattern_rest, path, prefix)) return true;
        csview path_rest = path;
        csview name = next_segment(&path_rest);
        // `**` doesn't go into package directories or hidden ones
        if (is_implicitly_excluded(name) || (name.size && name.buf[0] == '.')) return false;
        return glob_matches(pattern, path_rest, prefix);
    }
    csview path_rest = path;
    csview name = next_segment(&path_rest);
    bool has_wildcard = csview_find_sv(segment, c_sv("*")) != c_NPOS || csview_find_sv(segment, c_sv("?")) != c_NPOS;
    if (has_wildcard && name.size && name.buf[0] == '.' && segment.buf[0] != '.') return false;
    if (!segment_matches(segment, name)) return false;
    return glob_matches(pattern_rest, path_rest, prefix);
}

// Whether a path inside the directory `dir` can match the pattern (tsc's directory patterns: don't walk what no
// include can match)
static bool glob_may_match_inside(csview pattern, csview dir) {
    if (!dir.size) return pattern.size > 0;
    if (!pattern.size) return false;
    csview pattern_rest = pattern;
    csview segment = next_segment(&pattern_rest);
    csview dir_rest = dir;
    if (csview_equals(segment, "**")) {
        if (glob_may_match_inside(pattern_rest, dir)) return true;
        csview name = next_segment(&dir_rest);
        if (is_implicitly_excluded(name) || (name.size && name.buf[0] == '.')) return false;
        return glob_may_match_inside(pattern, dir_rest);
    }
    csview name = next_segment(&dir_rest);
    bool has_wildcard = csview_find_sv(segment, c_sv("*")) != c_NPOS || csview_find_sv(segment, c_sv("?")) != c_NPOS;
    if (has_wildcard && name.size && name.buf[0] == '.' && segment.buf[0] != '.') return false;
    if (!segment_matches(segment, name)) return false;
    return glob_may_match_inside(pattern_rest, dir_rest);
}

// "src" means "src/**/*": a last segment without wildcard or extension is a directory
static cstr include_pattern(ConfigLoad* me, csview dir, csview written) {
    cstr absolute = config_path(me, dir, written);
    csview last = basename_of(cstr_sv(&absolute));
    bool is_file_pattern = csview_find_sv(last, c_sv("*")) != c_NPOS || csview_find_sv(last, c_sv("?")) != c_NPOS ||
        csview_find_sv(last, c_sv(".")) != c_NPOS;
    if (!is_file_pattern) cstr_append_lit(&absolute, "/**/*");
    return absolute;
}

static bool has_source_extension(ConfigLoad* me, csview name) {
    static const char* const ts[] = { ".ts", ".tsx", ".mts", ".cts" };
    static const char* const js[] = { ".js", ".jsx", ".mjs", ".cjs" };
    for (isize i = 0; i < (isize)c_arraylen(ts); i++) {
        if (csview_ends_with(name, ts[i])) return true;
    }
    if (!my options->allow_js) return false;
    if (csview_ends_with(name, ".min.js")) return false;
    for (isize i = 0; i < (isize)c_arraylen(js); i++) {
        if (csview_ends_with(name, js[i])) return true;
    }
    return false;
}

typedef struct Patterns {
    StrList include;
    StrList exclude;
} Patterns;

static bool is_excluded(Patterns const* patterns, csview path) {
    for (isize i = 0; i < patterns->exclude.count; i++) {
        if (glob_matches(cstr_sv(&patterns->exclude.items[i]), path, true)) return true;
    }
    return false;
}

static void collect_files(ConfigLoad* me, Patterns const* patterns, csview dir, StrList* out, int depth) {
    if (depth > 64) return;
    StrList subdirs = {0};
    for_each (item, fs_listdir_sorted, my fs, dir) {
        cstr path = path_join(dir, item.name);
        if (fs_is_directory(my fs, cstr_sv(&path))) {
            bool wanted = false;
            for (isize i = 0; i < patterns->include.count && !wanted; i++) {
                wanted = glob_may_match_inside(cstr_sv(&patterns->include.items[i]), cstr_sv(&path));
            }
            if (wanted && !is_implicitly_excluded(item.name) && item.name.buf[0] != '.' &&
                !is_excluded(patterns, cstr_sv(&path))
            ) {
                str_list_push(&subdirs, cstr_sv(&path));
            }
        } else if (has_source_extension(me, item.name) && !is_excluded(patterns, cstr_sv(&path))) {
            for (isize i = 0; i < patterns->include.count; i++) {
                if (glob_matches(cstr_sv(&patterns->include.items[i]), cstr_sv(&path), false)) {
                    str_list_push(out, cstr_sv(&path));
                    break;
                }
            }
        }
        cstr_drop(&path);
    }
    for (isize i = 0; i < subdirs.count; i++) collect_files(me, patterns, cstr_sv(&subdirs.items[i]), out, depth + 1);
    str_list_drop(&subdirs);
}

// The directory a pattern can first match something in: its leading segments without wildcards
static cstr pattern_base(csview pattern) {
    cstr ret = cstr_null;
    csview rest = pattern;
    while (rest.size) {
        csview segment = next_segment(&rest);
        if (csview_find_sv(segment, c_sv("*")) != c_NPOS || csview_find_sv(segment, c_sv("?")) != c_NPOS) break;
        if (!rest.size) break; // The last segment is a file
        if (segment.size) {
            cstr_append_lit(&ret, "/");
            cstr_append_sv(&ret, segment);
        }
    }
    if (!cstr_size(&ret)) cstr_assign(&ret, "/");
    return ret;
}

static void collect_root_files(ConfigLoad* me, StrList* out) {
    for (isize i = 0; my files && i < my files->count; i++) {
        if (my files->items[i].tag != JSON_STRING) continue;
        cstr path = config_path(me, cstr_sv(&my files_dir), cstr_sv(&my files->items[i].string));
        str_list_push(out, cstr_sv(&path));
        cstr_drop(&path);
    }
    if (my files && !my include) return;

    Patterns patterns = {0};
    if (my include) {
        for (isize i = 0; i < my include->count; i++) {
            if (my include->items[i].tag != JSON_STRING) continue;
            cstr pattern = include_pattern(me, cstr_sv(&my include_dir), cstr_sv(&my include->items[i].string));
            str_list_push(&patterns.include, cstr_sv(&pattern));
            cstr_drop(&pattern);
        }
    } else {
        cstr pattern = include_pattern(me, cstr_sv(&my config_dir), c_sv("**/*"));
        str_list_push(&patterns.include, cstr_sv(&pattern));
        cstr_drop(&pattern);
    }
    for (isize i = 0; my exclude && i < my exclude->count; i++) {
        if (my exclude->items[i].tag != JSON_STRING) continue;
        cstr pattern = config_path(me, cstr_sv(&my exclude_dir), cstr_sv(&my exclude->items[i].string));
        str_list_push(&patterns.exclude, cstr_sv(&pattern));
        cstr_drop(&pattern);
    }

    // One walk per distinct base directory, skipping bases inside one already walked
    StrList found = {0};
    StrList bases = {0};
    for (isize i = 0; i < patterns.include.count; i++) {
        cstr base = pattern_base(cstr_sv(&patterns.include.items[i]));
        bool covered = false;
        for (isize j = 0; j < bases.count && !covered; j++) {
            csview other = cstr_sv(&bases.items[j]);
            covered = cstr_equals_sv(&base, other) || (cstr_starts_with_sv(&base, other) &&
                (other.size == 1 || cstr_str(&base)[other.size] == '/'));
        }
        if (!covered) str_list_push(&bases, cstr_sv(&base));
        cstr_drop(&base);
    }
    for (isize i = 0; i < bases.count; i++) {
        if (fs_is_directory(my fs, cstr_sv(&bases.items[i]))) {
            collect_files(me, &patterns, cstr_sv(&bases.items[i]), &found, 0);
        }
    }
    for (isize i = 0; i < found.count; i++) {
        bool seen = false;
        for (isize j = 0; j < out->count && !seen; j++) seen = cstr_eq(&out->items[j], &found.items[i]);
        if (!seen) str_list_push(out, cstr_sv(&found.items[i]));
    }
    str_list_drop(&found);
    str_list_drop(&bases);
    str_list_drop(&patterns.include);
    str_list_drop(&patterns.exclude);
}

failable tsconfig_load(Fs const* fs, csview path, CompilerOptions* options, StrList* root_files) {
    ConfigLoad load = { .fs = fs, .options = options };
    try (fs_get_cwd(fs, &load.cwd)) bubble;
    cstr absolute = absolute_path_of(cstr_sv(&load.cwd), path);
    if (fs_is_directory(fs, cstr_sv(&absolute))) cstr_append_lit(&absolute, "/tsconfig.json");
    load.config_dir = cstr_from_sv(parent_of(cstr_sv(&absolute)));

    FailableResult result = load_config(&load, cstr_sv(&absolute));
    if (!result.error) {
        cstr_assign_sv(&options->config_dir, cstr_sv(&load.config_dir));
        if (root_files) collect_root_files(&load, root_files);
    }
    for (isize i = 0; i < load.document_count; i++) {
        json_drop(load.documents[i]);
        free(load.documents[i]);
    }
    cstr_drop(&absolute);
    cstr_drop(&load.cwd);
    cstr_drop(&load.config_dir);
    cstr_drop(&load.files_dir);
    cstr_drop(&load.include_dir);
    cstr_drop(&load.exclude_dir);
    return result;
}

cstr tsconfig_find(Fs const* fs, csview directory) {
    cstr cwd = cstr_null;
    try (fs_get_cwd(fs, &cwd)) or_panic;
    cstr dir = absolute_path_of(cstr_sv(&cwd), directory);
    cstr_drop(&cwd);
    while (true) {
        cstr candidate = path_join(cstr_sv(&dir), c_sv("tsconfig.json"));
        if (fs_is_reg_file(fs, cstr_sv(&candidate))) {
            cstr_drop(&dir);
            return candidate;
        }
        cstr_drop(&candidate);
        if (cstr_size(&dir) <= 1) break;
        cstr parent = cstr_from_sv(parent_of(cstr_sv(&dir)));
        cstr_take(&dir, parent);
    }
    cstr_drop(&dir);
    return cstr_null;
}
