#include "compiler_options.h"
#include "content_mapper.h"
#include "string_helpers.h"
#include "lib_names.h"

// (scope.h's, which this can't include: it has another name_equals)
isize spelling_suggestion(csview name, csview const* names, isize count);
isize spelling_suggestion_ex(csview name, csview const* names, isize count, int (*compare)(void* ctx, isize a, isize b), void* ctx);
int spelling_compare_strings(void* ctx, isize a, isize b);

typedef struct EnumValue {
    const char* name;
    int value;
} EnumValue;

// In tsc's order, which is the order "@target: *" expands in
static const EnumValue target_values[] = {
    {"es5", SCRIPT_TARGET_ES5}, {"es6", SCRIPT_TARGET_ES2015},
    {"es2015", SCRIPT_TARGET_ES2015}, {"es2016", SCRIPT_TARGET_ES2016}, {"es2017", SCRIPT_TARGET_ES2017},
    {"es2018", SCRIPT_TARGET_ES2018}, {"es2019", SCRIPT_TARGET_ES2019}, {"es2020", SCRIPT_TARGET_ES2020},
    {"es2021", SCRIPT_TARGET_ES2021}, {"es2022", SCRIPT_TARGET_ES2022}, {"es2023", SCRIPT_TARGET_ES2023},
    {"es2024", SCRIPT_TARGET_ES2024}, {"es2025", SCRIPT_TARGET_ES2025}, {"es2026", SCRIPT_TARGET_ES2026}, {"esnext", SCRIPT_TARGET_ESNEXT}, {},
};

static const EnumValue module_values[] = {
    {"commonjs", MODULE_KIND_COMMONJS}, {"amd", MODULE_KIND_AMD},
    {"system", MODULE_KIND_SYSTEM}, {"umd", MODULE_KIND_UMD}, {"es6", MODULE_KIND_ES2015},
    {"es2015", MODULE_KIND_ES2015}, {"es2020", MODULE_KIND_ES2020}, {"es2022", MODULE_KIND_ES2022},
    {"esnext", MODULE_KIND_ESNEXT}, {"node16", MODULE_KIND_NODE16}, {"node18", MODULE_KIND_NODE18}, {"node20", MODULE_KIND_NODE20},
    {"nodenext", MODULE_KIND_NODENEXT}, {"preserve", MODULE_KIND_PRESERVE}, {},
};

static const EnumValue module_resolution_values[] = {
    {"node16", MODULE_RESOLUTION_NODE16}, {"nodenext", MODULE_RESOLUTION_NODENEXT}, {"bundler", MODULE_RESOLUTION_BUNDLER},
    {"classic", MODULE_RESOLUTION_CLASSIC}, {"node", MODULE_RESOLUTION_NODE10}, {"node10", MODULE_RESOLUTION_NODE10}, {},
};

static const EnumValue jsx_values[] = {
    {"preserve", JSX_PRESERVE}, {"react-native", JSX_REACT_NATIVE}, {"react-jsx", JSX_REACT_JSX},
    {"react-jsxdev", JSX_REACT_JSXDEV}, {"react", JSX_REACT}, {},
};

static const EnumValue module_detection_values[] = {
    {"auto", MODULE_DETECTION_AUTO}, {"legacy", MODULE_DETECTION_LEGACY}, {"force", MODULE_DETECTION_FORCE}, {},
};

static const EnumValue bool_values[] = { {"true", 1}, {"false", 0}, {} };

// (0: not set, which is lf on the command line and crlf in tsc's harness)
static const EnumValue new_line_values[] = { {"crlf", 1}, {"lf", 2}, {} };

static const struct { const char* name; EnumValue const* values; } enum_options[] = {
    {"target", target_values},
    {"module", module_values},
    {"moduleresolution", module_resolution_values},
    {"jsx", jsx_values},
    {"moduledetection", module_detection_values},
    {"newline", new_line_values},
};

// tsc's string, number and list options plus the harness's own: a comma in these is part of the value
static const char* const non_varying_options[] = {
    "baseurl", "customconditions", "declarationdir", "generatecpuprofile", "generatetrace",
    "ignoredeprecations", "jsxfactory", "jsxfragmentfactory", "jsximportsource", "lib", "locale", "maproot",
    "maxnodemodulejsdepth", "modulesuffixes", "outdir", "outfile", "paths", "plugins", "project",
    "reactnamespace", "rootdir", "rootdirs", "sourceroot", "tsbuildinfofile", "typeroots", "types",
    "filename", "libfiles", "currentdirectory", "symlink", "link", "baselinefile", "includebuiltfile",
};

static bool name_equals(csview name, const char* lowercase) {
    isize len = (isize)strlen(lowercase);
    if (name.size != len) return false;
    for (isize i = 0; i < len; i++) {
        char c = name.buf[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != lowercase[i]) return false;
    }
    return true;
}

static EnumValue const* find_enum_value(EnumValue const* values, csview value) {
    for (; values->name; values++) {
        if (name_equals(value, values->name)) return values;
    }
    return nullptr;
}

CompilerOptions compiler_options_init() {
    CompilerOptions ret = {0};
    compiler_options_resolve(&ret);
    return ret;
}

void compiler_options_drop(CompilerOptions* me) {
    cstr_drop(&my lib);
    cstr_drop(&my config_dir);
    cstr_drop(&my base_url);
    c_drop(cstr, &my jsx_factory, &my jsx_fragment_factory, &my jsx_import_source, &my react_namespace);
    c_drop(cstr, &my map_root, &my source_root, &my ts_build_info_file);
    c_drop(cstr, &my out_dir, &my out_file, &my root_dir, &my declaration_dir);
    cstr_drop(&my paths_base);
    compiler_options_clear_paths(me);
    str_list_drop(&my root_dirs);
    str_list_drop(&my type_roots);
    str_list_drop(&my types);
    str_list_drop(&my module_suffixes);
    str_list_drop(&my custom_conditions);
    str_list_drop(&my config_files);
    str_list_drop(&my config_include_specs);
    str_list_drop(&my config_include_patterns);
    str_list_drop(&my config_include_shown);
    str_list_drop(&my config_exclude_shown);
    for (u32 i = 0; i < my content_mapper_count; i++) content_mapper_drop(&my content_mappers[i]);
    free(my content_mappers);
}

void str_list_push(StrList* me, csview item) {
    if (my count == my cap) {
        my cap = my cap ? my cap * 2 : 4;
        my items = realloc(my items, sizeof(cstr) * (size_t)my cap);
    }
    my items[my count++] = cstr_from_sv(item);
}

void str_list_clear(StrList* me) {
    for (isize i = 0; i < my count; i++) cstr_drop(&my items[i]);
    my count = 0;
}

void str_list_drop(StrList* me) {
    str_list_clear(me);
    free(my items);
    *me = (StrList){0};
}

// keep_empty: an empty entry means something (the "" of `moduleSuffixes`), so "a," is two entries. Like tsc's
// parseListTypeOption only the whole value is trimmed, not the entries (`@customConditions: webpack, browser` is
// "webpack" and " browser")
static void str_list_set_from_commas(StrList* me, csview value, bool keep_empty) {
    str_list_clear(me);
    value = csview_trim_whitespace(value);
    if (!value.size) return;
    while (true) {
        isize end = csview_find_sv(value, c_sv(","));
        csview single = csview_slice(value, 0, end);
        if (single.size || keep_empty) str_list_push(me, single);
        if (end == c_NPOS) break;
        value = csview_slice(value, end + 1, INTPTR_MAX);
    }
}

void compiler_options_clear_paths(CompilerOptions* me) {
    for (isize i = 0; i < my path_count; i++) {
        cstr_drop(&my paths[i].pattern);
        str_list_drop(&my paths[i].substitutions);
    }
    free(my paths);
    my paths = nullptr;
    my path_count = 0;
}

void compiler_options_add_path(CompilerOptions* me, csview pattern, StrList substitutions) {
    my paths_set = true;
    for (isize i = 0; i < my path_count; i++) {
        if (cstr_equals_sv(&my paths[i].pattern, pattern)) {
            str_list_drop(&my paths[i].substitutions);
            my paths[i].substitutions = substitutions;
            return;
        }
    }
    my paths = realloc(my paths, sizeof(PathMapping) * (size_t)(my path_count + 1));
    my paths[my path_count++] = (PathMapping){ .pattern = cstr_from_sv(pattern), .substitutions = substitutions };
}

CompilerOptionSetResult compiler_options_set(CompilerOptions* me, csview name, csview value) {
    value = csview_trim_whitespace(value);

#define X(field, lowercase, strict_family) \
    if (name_equals(name, lowercase)) { \
        /* Like tsc's harness, anything but "true" is false; null takes back a value given before (an extended */ \
        /* config's, the command line's `--composite null`) */ \
        my field = name_equals(value, "true"); \
        my is_set[COMPILER_OPTION_##field] = !name_equals(value, "null"); \
        return COMPILER_OPTION_OK; \
    }
    COMPILER_BOOL_OPTIONS(X)
#undef X

#define ENUM_OPTION(lowercase, values, field, set_field) \
    if (name_equals(name, lowercase)) { \
        EnumValue const* found = find_enum_value(values, value); \
        if (!found) return COMPILER_OPTION_BAD_VALUE; \
        my field = found->value; \
        set_field; \
        return COMPILER_OPTION_OK; \
    }
    ENUM_OPTION("target", target_values, target, my target_set = true)
    ENUM_OPTION("module", module_values, module, my module_set = true)
    ENUM_OPTION("moduleresolution", module_resolution_values, module_resolution,
        my module_resolution_set = true; my module_resolution_removed = MODULE_RESOLUTION_NONE)
    ENUM_OPTION("jsx", jsx_values, jsx, )
    ENUM_OPTION("moduledetection", module_detection_values, module_detection, my module_detection_set = true)
    ENUM_OPTION("newline", new_line_values, new_line, )
#undef ENUM_OPTION

    if (name_equals(name, "lib")) {
        cstr_clear(&my lib);
        my lib_set = !name_equals(value, "null");
        while (value.size) {
            isize end = csview_find_sv(value, c_sv(","));
            csview single = csview_trim_whitespace(csview_slice(value, 0, end));
            if (single.size) {
                if (cstr_size(&my lib)) cstr_append_lit(&my lib, ",");
                cstr_append_sv(&my lib, single);
            }
            if (end == c_NPOS) break;
            value = csview_slice(value, end + 1, INTPTR_MAX);
        }
        cstr_lowercase(&my lib);
        return COMPILER_OPTION_OK;
    }

#define STRING_OPTION(lowercase, field) \
    if (name_equals(name, lowercase)) { \
        cstr_take(&my field, cstr_from_sv(value)); \
        return COMPILER_OPTION_OK; \
    }
    STRING_OPTION("jsxfactory", jsx_factory)
    STRING_OPTION("jsxfragmentfactory", jsx_fragment_factory)
    STRING_OPTION("jsximportsource", jsx_import_source)
    STRING_OPTION("reactnamespace", react_namespace)
    STRING_OPTION("outdir", out_dir)
    STRING_OPTION("outfile", out_file)
    STRING_OPTION("rootdir", root_dir)
    STRING_OPTION("declarationdir", declaration_dir)
    STRING_OPTION("maproot", map_root)
    STRING_OPTION("sourceroot", source_root)
    STRING_OPTION("tsbuildinfofile", ts_build_info_file)
#undef STRING_OPTION
    if (name_equals(name, "maxnodemodulejsdepth")) {
        u32 depth = 0;
        for (isize i = 0; i < value.size; i++) {
            if (value.buf[i] < '0' || value.buf[i] > '9') return COMPILER_OPTION_BAD_VALUE;
            depth = depth * 10 + (u32)(value.buf[i] - '0');
        }
        my max_node_module_js_depth = depth;
        return COMPILER_OPTION_OK;
    }
    if (name_equals(name, "baseurl")) {
        // null (a config's JSON null, the command line's `--baseUrl null`) unsets it
        bool unset = name_equals(value, "null");
        cstr_take(&my base_url, unset ? cstr_null : cstr_from_sv(value));
        my base_url_set = !unset;
        return COMPILER_OPTION_OK;
    }
#define LIST_OPTION(lowercase, field, set_field) \
    if (name_equals(name, lowercase)) { \
        str_list_set_from_commas(&my field, value, name_equals(name, "modulesuffixes")); \
        set_field; \
        return COMPILER_OPTION_OK; \
    }
    LIST_OPTION("rootdirs", root_dirs, )
    LIST_OPTION("typeroots", type_roots, my type_roots_set = true)
    LIST_OPTION("types", types, my types_set = true)
    LIST_OPTION("modulesuffixes", module_suffixes, )
    LIST_OPTION("customconditions", custom_conditions, )
#undef LIST_OPTION

    return COMPILER_OPTION_UNKNOWN;
}

void compiler_options_resolve(CompilerOptions* me) {
    // TypeScript 6.0: `strict` defaults to true (tsc's getStrictOptionValue: `strict !== false`)
    if (!my is_set[COMPILER_OPTION_strict]) my strict = true;
#define X(field, lowercase, strict_family) \
    if (!my is_set[COMPILER_OPTION_##field] && &my field != &my strict) my field = strict_family && my strict;
    COMPILER_BOOL_OPTIONS(X)
#undef X
#define DEFAULT(field, value) if (!my is_set[COMPILER_OPTION_##field]) my field = (value)
    DEFAULT(always_strict, true);
    DEFAULT(deduplicate_packages, true);

    // (tsc's computed target: the latest standard)
    if (!my target_set) my target = SCRIPT_TARGET_ES2026;
    if (!my module_set) {
        my module = my target == SCRIPT_TARGET_ESNEXT ? MODULE_KIND_ESNEXT :
            my target >= SCRIPT_TARGET_ES2022 ? MODULE_KIND_ES2022 :
            my target >= SCRIPT_TARGET_ES2020 ? MODULE_KIND_ES2020 :
            my target >= SCRIPT_TARGET_ES2015 ? MODULE_KIND_ES2015 : MODULE_KIND_COMMONJS;
    }
    bool node_module = my module >= MODULE_KIND_NODE16 && my module <= MODULE_KIND_NODENEXT;
    // TypeScript 7 removed classic and node10 (TS5108): tsgo's GetModuleResolutionKind computes the resolution as if
    // they weren't given
    if (my module_resolution_set &&
        (my module_resolution == MODULE_RESOLUTION_CLASSIC || my module_resolution == MODULE_RESOLUTION_NODE10)
    ) {
        my module_resolution_removed = my module_resolution;
        my module_resolution_set = false;
    }
    if (!my module_resolution_set) {
        switch (my module) {
            case MODULE_KIND_NODE16:
            case MODULE_KIND_NODE18:
            case MODULE_KIND_NODE20: my module_resolution = MODULE_RESOLUTION_NODE16; break;
            case MODULE_KIND_NODENEXT: my module_resolution = MODULE_RESOLUTION_NODENEXT; break;
            default: my module_resolution = MODULE_RESOLUTION_BUNDLER; break;
        }
    }
    if (!my module_detection_set) {
        my module_detection = node_module ? MODULE_DETECTION_FORCE : MODULE_DETECTION_AUTO;
    }
    bool modern_resolution = my module_resolution >= MODULE_RESOLUTION_NODE16;

    DEFAULT(allow_js, my check_js);
    DEFAULT(isolated_modules, my verbatim_module_syntax);
    DEFAULT(no_unchecked_side_effect_imports, true);
    // (Parsed only: tsgo's checker never reads it, types are ordered one way whatever it says)
    DEFAULT(stable_type_ordering, true);
    DEFAULT(es_module_interop, true);
    DEFAULT(allow_synthetic_default_imports, true);
    DEFAULT(use_define_for_class_fields, my target >= SCRIPT_TARGET_ES2022);
    DEFAULT(resolve_json_module, my module == MODULE_KIND_NODE20 || my module == MODULE_KIND_NODENEXT ||
        my module_resolution == MODULE_RESOLUTION_BUNDLER);
    DEFAULT(resolve_package_json_exports, modern_resolution);
    DEFAULT(resolve_package_json_imports, modern_resolution);
    DEFAULT(preserve_const_enums, my isolated_modules);
#undef DEFAULT
}

void compiler_options_enable_strict(CompilerOptions* me) {
    my strict = true;
    my is_set[COMPILER_OPTION_strict] = true;
    compiler_options_resolve(me);
}

bool compiler_options_lib_name_valid(csview name) {
    for (u32 i = 0; i < lib_name_count; i++) {
        if (name_equals(name, lib_names[i])) return true;
    }
    return false;
}

cstr compiler_options_lib_names_message(void) {
    cstr ret = cstr_lit("Argument for '--lib' option must be: ");
    for (u32 i = 0; i < lib_name_count; i++) {
        cstr_append(&ret, i ? ", '" : "'");
        cstr_append(&ret, lib_names[i]);
        cstr_append(&ret, "'");
    }
    cstr_append_lit(&ret, ".");
    return ret;
}

// The names that aren't the file's name (lib_aliases: es6 and es7, and the esnext features that made it into a year).
// For `lib` and `/// <reference lib>` alike
csview compiler_options_lib_file_name(csview name) {
    for (u32 i = 0; i < lib_alias_count; i++) {
        if (csview_iequals(name, lib_aliases[i][0])) return csview_from(lib_aliases[i][1]);
    }
    return name;
}

cstr compiler_options_lib_files(CompilerOptions const* me) {
    if (my no_lib) return cstr_null;
    if (!cstr_size(&my lib) && my lib_set) return cstr_null;
    if (!cstr_size(&my lib)) {
        switch (my target) {
            case SCRIPT_TARGET_ES5: return cstr_lit("default");
            case SCRIPT_TARGET_ES2015: return cstr_lit("es6"); // tsc doesn't use lib.es2015.full.d.ts either
            default: break;
        }
        for (EnumValue const* i = target_values; i->name; i++) {
            if (i->value == (int)my target) return cstr_d(i->name, ".full");
        }
        impossible;
    }
    cstr ret = cstr_null;
    csview rest = cstr_sv(&my lib);
    while (rest.size) {
        isize end = csview_find_sv(rest, c_sv(","));
        csview single = csview_slice(rest, 0, end);
        single = compiler_options_lib_file_name(single);
        if (csview_starts_with(single, "lib.") && csview_ends_with(single, ".d.ts")) {
            single = csview_subview(single, 4, single.size - 9);
        }
        if (cstr_size(&ret)) cstr_append_lit(&ret, ",");
        cstr_append_sv(&ret, single);
        if (end == c_NPOS) break;
        rest = csview_slice(rest, end + 1, INTPTR_MAX);
    }
    return ret;
}

// The options compiler_options_set() keeps as a string (a path or a name): empty is unset
bool compiler_option_is_string(csview name) {
    static char const* const names[] = {
        "jsxfactory", "jsxfragmentfactory", "jsximportsource", "reactnamespace", "outdir", "outfile", "rootdir",
        "declarationdir", "maproot", "sourceroot", "tsbuildinfofile", nullptr,
    };
    for (char const* const* i = names; *i; i++) {
        if (name_equals(name, *i)) return true;
    }
    return false;
}

bool compiler_option_is_boolean(csview name) {
#define X(field, lowercase, strict_family) if (name_equals(name, lowercase)) return true;
    COMPILER_BOOL_OPTIONS(X)
#undef X
    return false;
}

#include "json.h"

// The kind letter of an option (lib_names.h's tsc_option_kinds), 0 for none; element: of a list option's elements
static char option_kind(csview name, bool element) {
    if (element) {
        for (u32 i = 0; i < tsc_option_elements_count; i++) {
            if (csview_equals(name, tsc_option_elements[i][0])) return tsc_option_elements[i][1][0];
        }
        return 0;
    }
    for (u32 i = 0; i < tsc_option_names_count; i++) {
        if (csview_equals(name, tsc_option_names[i])) return tsc_option_kinds[i];
    }
    return 0;
}

static bool kind_takes(char kind, csview name, int tag) {
    switch (kind) {
        case 'b': return tag == JSON_BOOL;
        case 's': case 'e': return tag == JSON_STRING;
        case 'n': return tag == JSON_NUMBER;
        case 'o': return tag == JSON_OBJECT;
        case 'l': return tag == JSON_ARRAY;
        case 'L': return tag == JSON_ARRAY || kind_takes(option_kind(name, true), name, tag);
        default: return true;
    }
}

// tsgo's getCompilerOptionValueTypeString
static const char* kind_name(char kind) {
    switch (kind) {
        case 'b': return "boolean";
        case 's': return "string";
        case 'n': return "number";
        case 'e': return "enum";
        case 'o': return "object";
        default: return "Array";
    }
}

bool compiler_option_is_command_line_only(csview name) {
    for (u32 i = 0; i < tsc_command_line_only_options_count; i++) {
        if (csview_equals(name, tsc_command_line_only_options[i])) return true;
    }
    return false;
}

unsigned compiler_option_check_json_type(csview name, int json_tag, bool element, cstr* message) {
    char kind = option_kind(name, element);
    if (!kind || json_tag == JSON_NULL || kind_takes(kind, name, json_tag)) return 0;
    if (kind == 'L') {
        *message = cstr_d("Compiler option '", name, "' requires a value of type ", kind_name(option_kind(name, true)), " or Array.");
    } else {
        *message = cstr_d("Compiler option '", name, "' requires a value of type ", kind_name(kind), ".");
    }
    return 5024;
}

static bool option_name_equals(csview a, const char* b, bool ignore_case) {
    isize len = (isize)strlen(b);
    if (a.size != len) return false;
    if (!ignore_case) return !memcmp(a.buf, b, (size_t)len);
    for (isize i = 0; i < len; i++) {
        if (tolower((unsigned char)a.buf[i]) != tolower((unsigned char)b[i])) return false;
    }
    return true;
}

unsigned compiler_option_unknown_diagnostic(csview name, bool ignore_case, csview shown, cstr* message) {
    for (isize i = 0; i < (isize)tsc_option_names_count; i++) {
        if (option_name_equals(name, tsc_option_names[i], ignore_case)) return 0;
    }
    // (createUnknownOptionError's alternate mode, looked up in lowercase: the --build options)
    for (isize i = 0; i < (isize)tsc_build_only_option_names_count; i++) {
        if (!option_name_equals(name, tsc_build_only_option_names[i], true)) continue;
        if (!i) {
            *message = cstr_lit("Option '--build' must be the first command line argument.");
            return 6369;
        }
        *message = cstr_d("Compiler option '--", name, "' may only be used with '--build'.");
        return 5093;
    }
    csview candidates[256];
    check(tsc_option_names_count <= c_arraylen(candidates));
    for (isize i = 0; i < (isize)tsc_option_names_count; i++) candidates[i] = csview_from(tsc_option_names[i]);
    isize suggestion = spelling_suggestion_ex(name, candidates, (isize)tsc_option_names_count, spelling_compare_strings, candidates);
    if (suggestion >= 0) {
        *message = cstr_d("Unknown compiler option '", shown, "'. Did you mean '", tsc_option_names[suggestion], "'?");
        return 5025;
    }
    *message = cstr_d("Unknown compiler option '", shown, "'.");
    return 5023;
}

bool compiler_option_is_command_option_without_build(csview name) {
    for (isize i = tsc_common_options_with_build; i < (isize)tsc_option_names_count; i++) {
        if (option_name_equals(name, tsc_option_names[i], false)) return true;
    }
    return false;
}

bool compiler_option_varies(csview name) {
    for (isize i = 0; i < (isize)c_arraylen(non_varying_options); i++) {
        if (name_equals(name, non_varying_options[i])) return false;
    }
    return true;
}

cstr compiler_option_bad_value_message(csview name, csview value) {
    // (tsgo's formatEnumTypeKeys: not the values TypeScript 7 removed, which still parse to be reported)
    static const char* const removed[] = { "es5", "amd", "system", "umd", "classic", "node", "node10" };
    for (isize i = 0; i < (isize)c_arraylen(enum_options); i++) {
        if (!name_equals(name, enum_options[i].name)) continue;
        if (find_enum_value(enum_options[i].values, csview_trim_whitespace(value))) return cstr_null;
        csview canonical = name;
        for (u32 j = 0; j < tsc_option_names_count; j++) {
            if (name_equals(csview_from(tsc_option_names[j]), enum_options[i].name)) canonical = csview_from(tsc_option_names[j]);
        }
        cstr ret = cstr_d("Argument for '--", canonical, "' option must be: ");
        bool first = true;
        for (EnumValue const* v = enum_options[i].values; v->name; v++) {
            bool skip = false;
            for (isize j = 0; j < (isize)c_arraylen(removed) && !skip; j++) skip = !strcmp(v->name, removed[j]);
            if (skip) continue;
            cstr_append(&ret, first ? "'" : ", '");
            cstr_append(&ret, v->name);
            cstr_append(&ret, "'");
            first = false;
        }
        cstr_append(&ret, ".");
        return ret;
    }
    return cstr_null;
}

isize compiler_option_values(csview name, const char** out, int* ids, isize max) {
    EnumValue const* values = bool_values;
    for (isize i = 0; i < (isize)c_arraylen(enum_options); i++) {
        if (name_equals(name, enum_options[i].name)) values = enum_options[i].values;
    }
    isize count = 0;
    for (; values->name && count < max; values++, count++) {
        out[count] = values->name;
        ids[count] = values->value;
    }
    return count;
}

thread_local CompilerOptions const* compiler_options_private = nullptr;

void set_compiler_options(CompilerOptions const* options) {
    check(options);
    panic_if(compiler_options_private, "Global compiler options set multiple times without being cleared");
    compiler_options_private = options;
}

void clear_compiler_options() {
    panic_if(!compiler_options_private, "Global compiler options cleared when unset");
    compiler_options_private = nullptr;
}

bool compiler_option_is_identifier(csview text) {
    if (!text.size) return false;
    for (isize i = 0; i < text.size; i++) {
        unsigned char c = (unsigned char)text.buf[i];
        bool valid = c >= 0x80 || c == '_' || c == '$' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (i > 0 && c >= '0' && c <= '9');
        if (!valid) return false;
    }
    return true;
}

bool compiler_option_is_entity_name(csview text) {
    while (true) {
        isize dot = csview_find(text, ".");
        if (dot == c_NPOS) return compiler_option_is_identifier(text);
        if (!compiler_option_is_identifier(csview_slice(text, 0, dot))) return false;
        text = csview_slice(text, dot + 1, text.size);
    }
}
