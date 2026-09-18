#include "compiler_options.h"
#include "string_helpers.h"

typedef struct EnumValue {
    const char* name;
    int value;
} EnumValue;

// In tsc's order, which is the order "@target: *" expands in
static const EnumValue target_values[] = {
    {"es3", SCRIPT_TARGET_ES3}, {"es5", SCRIPT_TARGET_ES5}, {"es6", SCRIPT_TARGET_ES2015},
    {"es2015", SCRIPT_TARGET_ES2015}, {"es2016", SCRIPT_TARGET_ES2016}, {"es2017", SCRIPT_TARGET_ES2017},
    {"es2018", SCRIPT_TARGET_ES2018}, {"es2019", SCRIPT_TARGET_ES2019}, {"es2020", SCRIPT_TARGET_ES2020},
    {"es2021", SCRIPT_TARGET_ES2021}, {"es2022", SCRIPT_TARGET_ES2022}, {"es2023", SCRIPT_TARGET_ES2023},
    {"es2024", SCRIPT_TARGET_ES2024}, {"esnext", SCRIPT_TARGET_ESNEXT}, {},
};

static const EnumValue module_values[] = {
    {"none", MODULE_KIND_NONE}, {"commonjs", MODULE_KIND_COMMONJS}, {"amd", MODULE_KIND_AMD},
    {"system", MODULE_KIND_SYSTEM}, {"umd", MODULE_KIND_UMD}, {"es6", MODULE_KIND_ES2015},
    {"es2015", MODULE_KIND_ES2015}, {"es2020", MODULE_KIND_ES2020}, {"es2022", MODULE_KIND_ES2022},
    {"esnext", MODULE_KIND_ESNEXT}, {"node16", MODULE_KIND_NODE16}, {"node18", MODULE_KIND_NODE18},
    {"nodenext", MODULE_KIND_NODENEXT}, {"preserve", MODULE_KIND_PRESERVE}, {},
};

static const EnumValue module_resolution_values[] = {
    {"node10", MODULE_RESOLUTION_NODE10}, {"node", MODULE_RESOLUTION_NODE10}, {"classic", MODULE_RESOLUTION_CLASSIC},
    {"node16", MODULE_RESOLUTION_NODE16}, {"nodenext", MODULE_RESOLUTION_NODENEXT},
    {"bundler", MODULE_RESOLUTION_BUNDLER}, {},
};

static const EnumValue jsx_values[] = {
    {"preserve", JSX_PRESERVE}, {"react-native", JSX_REACT_NATIVE}, {"react", JSX_REACT},
    {"react-jsx", JSX_REACT_JSX}, {"react-jsxdev", JSX_REACT_JSXDEV}, {},
};

static const EnumValue module_detection_values[] = {
    {"auto", MODULE_DETECTION_AUTO}, {"legacy", MODULE_DETECTION_LEGACY}, {"force", MODULE_DETECTION_FORCE}, {},
};

static const EnumValue bool_values[] = { {"true", 1}, {"false", 0}, {} };

// Enum options we don't otherwise read, the harness still has to know what "*" means for them
static const EnumValue new_line_values[] = { {"crlf", 0}, {"lf", 1}, {} };
static const EnumValue imports_not_used_values[] = { {"remove", 0}, {"preserve", 1}, {"error", 2}, {} };

static const struct { const char* name; EnumValue const* values; } enum_options[] = {
    {"target", target_values},
    {"module", module_values},
    {"moduleresolution", module_resolution_values},
    {"jsx", jsx_values},
    {"moduledetection", module_detection_values},
    {"newline", new_line_values},
    {"importsnotusedasvalues", imports_not_used_values},
};

// tsc's string, number and list options plus the harness's own: a comma in these is part of the value
static const char* const non_varying_options[] = {
    "baseurl", "charset", "customconditions", "declarationdir", "generatecpuprofile", "generatetrace",
    "ignoredeprecations", "jsxfactory", "jsxfragmentfactory", "jsximportsource", "lib", "locale", "maproot",
    "maxnodemodulejsdepth", "modulesuffixes", "out", "outdir", "outfile", "paths", "plugins", "project",
    "reactnamespace", "rootdir", "rootdirs", "sourceroot", "tsbuildinfofile", "typeroots", "types",
    "filename", "libfiles", "currentdirectory", "symlink", "link", "baselinefile", "includebuiltfile",
    "typescriptversion",
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
}

CompilerOptionSetResult compiler_options_set(CompilerOptions* me, csview name, csview value) {
    value = csview_trim_whitespace(value);

#define X(field, lowercase, strict_family) \
    if (name_equals(name, lowercase)) { \
        /* Like tsc's harness, anything but "true" is false */ \
        my field = name_equals(value, "true"); \
        my is_set[COMPILER_OPTION_##field] = true; \
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
    ENUM_OPTION("moduleresolution", module_resolution_values, module_resolution, my module_resolution_set = true)
    ENUM_OPTION("jsx", jsx_values, jsx, )
    ENUM_OPTION("moduledetection", module_detection_values, module_detection, my module_detection_set = true)
#undef ENUM_OPTION

    if (name_equals(name, "lib")) {
        cstr_clear(&my lib);
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

    return COMPILER_OPTION_UNKNOWN;
}

void compiler_options_resolve(CompilerOptions* me) {
#define X(field, lowercase, strict_family) \
    if (!my is_set[COMPILER_OPTION_##field]) my field = strict_family && my strict;
    COMPILER_BOOL_OPTIONS(X)
#undef X
#define DEFAULT(field, value) if (!my is_set[COMPILER_OPTION_##field]) my field = (value)

    if (!my target_set) my target = SCRIPT_TARGET_ES5;
    if (!my module_set) my module = my target >= SCRIPT_TARGET_ES2015 ? MODULE_KIND_ES2015 : MODULE_KIND_COMMONJS;
    bool node_module = my module >= MODULE_KIND_NODE16 && my module <= MODULE_KIND_NODENEXT;
    if (!my module_resolution_set) {
        switch (my module) {
            case MODULE_KIND_COMMONJS: my module_resolution = MODULE_RESOLUTION_NODE10; break;
            case MODULE_KIND_NODE16:
            case MODULE_KIND_NODE18: my module_resolution = MODULE_RESOLUTION_NODE16; break;
            case MODULE_KIND_NODENEXT: my module_resolution = MODULE_RESOLUTION_NODENEXT; break;
            case MODULE_KIND_PRESERVE: my module_resolution = MODULE_RESOLUTION_BUNDLER; break;
            default: my module_resolution = MODULE_RESOLUTION_CLASSIC; break;
        }
    }
    if (!my module_detection_set) {
        my module_detection = node_module ? MODULE_DETECTION_FORCE : MODULE_DETECTION_AUTO;
    }
    bool modern_resolution = my module_resolution >= MODULE_RESOLUTION_NODE16;

    DEFAULT(allow_js, my check_js);
    DEFAULT(isolated_modules, my verbatim_module_syntax);
    DEFAULT(es_module_interop, node_module || my module == MODULE_KIND_PRESERVE);
    DEFAULT(allow_synthetic_default_imports,
        my es_module_interop || my module == MODULE_KIND_SYSTEM || my module_resolution == MODULE_RESOLUTION_BUNDLER);
    DEFAULT(use_define_for_class_fields, my target >= SCRIPT_TARGET_ES2022);
    DEFAULT(resolve_json_module, my module_resolution == MODULE_RESOLUTION_BUNDLER);
    DEFAULT(resolve_package_json_exports, modern_resolution);
    DEFAULT(resolve_package_json_imports, modern_resolution);
    DEFAULT(preserve_const_enums, my isolated_modules);
    DEFAULT(declaration, my isolated_declarations);
#undef DEFAULT
}

void compiler_options_enable_strict(CompilerOptions* me) {
    my strict = true;
    my is_set[COMPILER_OPTION_strict] = true;
    compiler_options_resolve(me);
}

cstr compiler_options_lib_files(CompilerOptions const* me) {
    if (my no_lib) return cstr_null;
    if (!cstr_size(&my lib)) {
        switch (my target) {
            case SCRIPT_TARGET_ES3:
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
        // The two names that aren't the file's name: lib.es6.d.ts and lib.es7.d.ts are default libraries of a target
        if (csview_equals(single, "es6")) single = c_sv("es2015");
        if (csview_equals(single, "es7")) single = c_sv("es2016");
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

bool compiler_option_is_boolean(csview name) {
#define X(field, lowercase, strict_family) if (name_equals(name, lowercase)) return true;
    COMPILER_BOOL_OPTIONS(X)
#undef X
    return false;
}

bool compiler_option_varies(csview name) {
    for (isize i = 0; i < (isize)c_arraylen(non_varying_options); i++) {
        if (name_equals(name, non_varying_options[i])) return false;
    }
    return true;
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

CompilerOptions const* compiler_options_private = nullptr;

void set_compiler_options(CompilerOptions const* options) {
    check(options);
    panic_if(compiler_options_private, "Global compiler options set multiple times without being cleared");
    compiler_options_private = options;
}

void clear_compiler_options() {
    panic_if(!compiler_options_private, "Global compiler options cleared when unset");
    compiler_options_private = nullptr;
}
