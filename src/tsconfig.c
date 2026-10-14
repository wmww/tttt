// realpath() and PATH_MAX, which strict -std=c2x hides
#define _XOPEN_SOURCE 700
#include <limits.h>
#include <stdlib.h>
#include <strings.h>

#include "tsconfig.h"
#include "content_mapper.h"
#include "json.h"

typedef uint8_t PathKind; // PathSet: 1 a literal (`files`) root, 2 one include patterns found
#define g_pro_key SvStr, svstr
#define g_val PathKind, path_kind
#include <cleat/generic/hmap.h>

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
    TsconfigDiagnostics* diagnostics; // Null: not wanted
} ConfigLoad;

static void config_diagnostic(ConfigLoad* me, unsigned code, cstr message, csview config, u32 start, u32 end) {
    if (!my diagnostics) {
        cstr_drop(&message);
        return;
    }
    TsconfigDiagnostics* d = my diagnostics;
    d->items = realloc(d->items, sizeof(TsconfigDiagnostic) * (size_t)(d->count + 1));
    d->items[d->count++] = (TsconfigDiagnostic){
        .code = code, .message = message, .config = cstr_from_sv(config), .start = start, .end = end,
    };
}

void tsconfig_diagnostics_drop(TsconfigDiagnostics* me) {
    for (u32 i = 0; i < my count; i++) {
        cstr_drop(&my items[i].message);
        cstr_drop(&my items[i].config);
    }
    free(my items);
    *me = (TsconfigDiagnostics){0};
}

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

static void apply_compiler_options(ConfigLoad* me, csview config, csview dir, JsonValue const* compiler_options) {
    for (isize i = 0; i < compiler_options->count; i++) {
        const char* name = cstr_str(&compiler_options->members[i].key);
        JsonValue const* value = &compiler_options->members[i].value;
        // A name that isn't one of tsc's options (in exact case: tsc ignores `"Target"`): TS5023 at it, and nothing set
        cstr message = cstr_null;
        unsigned unknown = compiler_option_unknown_diagnostic(csview_from(name), false, csview_from(name), &message);
        if (unknown) {
            config_diagnostic(me, unknown, message, config, compiler_options->members[i].key_start,
                compiler_options->members[i].key_end);
            continue;
        }
        // (tsgo's convertJsonOption: not one a config may set, on its name)
        if (compiler_option_is_command_line_only(csview_from(name))) {
            config_diagnostic(me, 6266, cstr_d("Option '", name, "' can only be specified on command line."), config,
                compiler_options->members[i].key_start, compiler_options->members[i].key_end);
            continue;
        }
        // tsgo's convertPropertyValueToJson and convertJsonOption: a value of another type (or none: TS1328 too, from
        // the parser) is TS5024 at it, and the option stays as it was; so is a list's element, which is left out
        cstr type_message = cstr_null;
        if (compiler_option_check_json_type(csview_from(name), (int)value->tag, false, &type_message)) {
            config_diagnostic(me, 5024, type_message, config, value->start, value->end);
            continue;
        }
        for (isize j = 0; value->tag == JSON_ARRAY && j < value->count; j++) {
            JsonValue const* item = &value->items[j];
            if (compiler_option_check_json_type(csview_from(name), (int)item->tag, item->tag != JSON_INVALID, &type_message)) {
                config_diagnostic(me, 5024, type_message, config, item->start, item->end);
            }
        }
        if (value->tag == JSON_NULL) {
            // null takes back what an extended config gave (tsc: the option is undefined again), e.g. a config
            // extending one with the 6.0-deprecated baseUrl. Only the options that tell "unset" from a value here
            if (!strcmp(name, "baseUrl") || compiler_option_is_boolean(csview_from(name))) {
                ignore compiler_options_set(my options, csview_from(name), c_sv("null"));
            } else if (compiler_option_is_string(csview_from(name))) {
                ignore compiler_options_set(my options, csview_from(name), c_sv(""));
            }
            continue;
        }
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
                    // Relative to the config (paths_base), which module resolution applies; `${configDir}` is ours
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
            // A value an enum option doesn't take: TS6046 at it, and the option stays as it was
            cstr bad = compiler_option_bad_value_message(csview_from(name), cstr_sv(&value->string));
            if (cstr_size(&bad)) config_diagnostic(me, 6046, bad, config, value->start, value->end);
            else ignore compiler_options_set(my options, csview_from(name), cstr_sv(&value->string));
        } else if (value->tag == JSON_ARRAY) {
            cstr joined = cstr_null;
            for (isize j = 0; j < value->count; j++) {
                if (value->items[j].tag != JSON_STRING) continue;
                // A lib that doesn't exist: TS6046 at it, and the rest goes on without it
                if (!strcmp(name, "lib") && !compiler_options_lib_name_valid(cstr_sv(&value->items[j].string))) {
                    config_diagnostic(me, 6046, compiler_options_lib_names_message(), config, value->items[j].start,
                        value->items[j].end);
                    continue;
                }
                if (cstr_size(&joined)) cstr_append_lit(&joined, ",");
                cstr_append_sv(&joined, cstr_sv(&value->items[j].string));
            }
            ignore compiler_options_set(my options, csview_from(name), cstr_sv(&joined));
            cstr_drop(&joined);
        }
    }
}

// tsgo's resolveContentMapperManifest: the package found like bundler resolution's ResolvePackageDirectory from the
// config's directory (node_modules up the tree), its package.json's name, version and `typescript.contentMapper`
// TODO(plan 56 part 2): TS18075-18079 when that fails (the mapper stays unresolved and maps nothing)
static void resolve_content_mapper(ConfigLoad* me, csview config, ContentMapper* mapper) {
    for (csview dir = parent_of(config);; dir = parent_of(dir)) {
        cstr package_dir = path_join(dir, c_sv("node_modules"));
        cstr_take(&package_dir, path_join(cstr_sv(&package_dir), cstr_sv(&mapper->package)));
        cstr package_json = path_join(cstr_sv(&package_dir), c_sv("package.json"));
        cstr contents = cstr_null;
        bool found = fs_is_reg_file(my fs, cstr_sv(&package_json));
        if (found) {
            try (fs_read_file(my fs, cstr_sv(&package_json), &contents)) if_err (e) {
                ignore e;
                current_error_destroy();
            }
            JsonValue document = {0};
            json_parse_strict(cstr_sv(&contents), &document);
            JsonValue const* typescript = json_get_tagged(&document, "typescript", JSON_OBJECT);
            JsonValue const* manifest = typescript ? json_get_tagged(typescript, "contentMapper", JSON_OBJECT) : nullptr;
            JsonValue const* exec = manifest ? json_get_tagged(manifest, "exec", JSON_ARRAY) : nullptr;
            bool strings = exec && exec->count;
            for (isize i = 0; strings && i < exec->count; i++) strings = exec->items[i].tag == JSON_STRING;
            csview name = json_get_string(&document, "name");
            if (name.size && strings) {
                mapper->resolved = true;
                mapper->name = cstr_from_sv(name);
                mapper->version = cstr_from_sv(json_get_string(&document, "version"));
                for (isize i = 0; i < exec->count; i++) str_list_push(&mapper->exec, cstr_sv(&exec->items[i].string));
                JsonValue const* options = json_get_tagged(manifest, "compilerOptions", JSON_ARRAY);
                for (isize i = 0; options && i < options->count; i++) {
                    if (options->items[i].tag == JSON_STRING) {
                        str_list_push(&mapper->compiler_options, cstr_sv(&options->items[i].string));
                    }
                }
                JsonValue const* dynamic = json_get_tagged(manifest, "dynamicConfig", JSON_BOOL);
                mapper->dynamic_config = dynamic && dynamic->boolean;
                cstr_take(&mapper->package_dir, cstr_clone(package_dir));
            }
            json_drop(&document);
        }
        c_drop(cstr, &package_dir, &package_json, &contents);
        if (found || !dir.size || csview_equals(dir, "/") || csview_equals_sv(dir, parent_of(dir))) return;
    }
}

// tsgo's content mappers (`contentMappers`: packages that turn other files, `.mdx`, into TypeScript), of the config
// asked for: each one's shape (parseContentMapper, TS5024 at it) and extensions (TS18065-18067 at them), then TS18068 at
// the key without --runExternalCode (tsgo then drops them all: their files stay unknown). tttt runs no mappers either way
static void check_content_mappers(ConfigLoad* me, csview path, JsonValue const* document) {
    JsonMember const* key = nullptr;
    for (isize i = 0; document->tag == JSON_OBJECT && i < document->count; i++) {
        if (cstr_equals(&document->members[i].key, "contentMappers")) key = &document->members[i];
    }
    if (!key || key->value.tag != JSON_ARRAY) return;
    static const char* const native[] = { ".ts", ".tsx", ".d.ts", ".cts", ".d.cts", ".mts", ".d.mts", ".js", ".jsx", ".cjs",
        ".mjs", ".json" };
    StrList seen = {0};
    bool any = false;
    for (isize i = 0; i < key->value.count; i++) {
        JsonValue const* mapper = &key->value.items[i];
        if (mapper->tag != JSON_OBJECT) {
            // (getPropFromRaw's element check)
            if (mapper->tag != JSON_NULL) {
                config_diagnostic(me, 5024, cstr_lit("Compiler option 'contentMappers' requires a value of type object."),
                    path, mapper->start, mapper->end);
            }
            continue;
        }
        JsonValue const* package = json_get(mapper, "package");
        JsonValue const* extensions = json_get(mapper, "extensions");
        JsonValue const* options = json_get(mapper, "options");
        bool valid = true;
        if (!package || package->tag != JSON_STRING || !cstr_size(&package->string)) {
            config_diagnostic(me, 5024, cstr_lit("Compiler option 'contentMapper.package' requires a value of type string."),
                path, mapper->start, mapper->end);
            valid = false;
        }
        bool strings = extensions && extensions->tag == JSON_ARRAY;
        for (isize j = 0; strings && j < extensions->count; j++) strings = extensions->items[j].tag == JSON_STRING;
        if (!strings) {
            config_diagnostic(me, 5024, cstr_lit("Compiler option 'contentMapper.extensions' requires a value of type string[]."),
                path, mapper->start, mapper->end);
            valid = false;
        }
        if (options && options->tag != JSON_OBJECT) {
            config_diagnostic(me, 5024, cstr_lit("Compiler option 'contentMapper.options' requires a value of type object."),
                path, mapper->start, mapper->end);
            valid = false;
        }
        if (!valid) continue;
        any = true;
        ContentMapper entry = { .package = cstr_clone(package->string) };
        if (options) json_write(&entry.options_json, options);
        for (isize j = 0; j < extensions->count; j++) {
            csview ext = cstr_sv(&extensions->items[j].string);
            // (At the first element with that text: getContentMapperExtensionSyntax)
            JsonValue const* extension = &extensions->items[j];
            for (isize k = 0; k < j; k++) {
                if (csview_equals_sv(cstr_sv(&extensions->items[k].string), ext)) {
                    extension = &extensions->items[k];
                    break;
                }
            }
            bool builtin = false;
            for (u32 k = 0; k < c_arraylen(native) && !builtin; k++) {
                builtin = ext.size == (isize)strlen(native[k]) && !strncasecmp(ext.buf, native[k], (size_t)ext.size);
            }
            bool duplicate = false;
            for (isize k = 0; k < seen.count && !duplicate; k++) duplicate = csview_equals_sv(cstr_sv(&seen.items[k]), ext);
            if (!csview_starts_with(ext, ".")) {
                config_diagnostic(me, 18065, cstr_d("Content mapper file extension '", ext, "' must begin with a '.'."), path,
                    extension->start, extension->end);
            } else if (builtin) {
                config_diagnostic(me, 18066, cstr_d("Content mapper file extension '", ext,
                    "' is a built-in extension and cannot be registered by a content mapper."), path, extension->start, extension->end);
            } else if (duplicate) {
                config_diagnostic(me, 18067, cstr_d("Content mapper file extension '", ext,
                    "' is registered by more than one content mapper."), path, extension->start, extension->end);
            } else {
                str_list_push(&seen, ext);
                str_list_push(&entry.extensions, ext);
            }
        }
        if (my options->run_external_code) {
            resolve_content_mapper(me, path, &entry);
            my options->content_mappers = realloc(my options->content_mappers,
                sizeof(ContentMapper) * (my options->content_mapper_count + 1));
            my options->content_mappers[my options->content_mapper_count++] = entry;
        } else {
            content_mapper_drop(&entry);
        }
    }
    str_list_drop(&seen);
    if (any && !my options->run_external_code) {
        config_diagnostic(me, 18068, cstr_lit("Content mappers require the '--runExternalCode' command line flag to be enabled."),
            path, key->key_start, key->key_end);
    }
}

// A package's `exports` target for a condition-less or node/require/types/default condition: a string, else the first
// matching condition's
static JsonValue const* exports_target(JsonValue const* target) {
    for (u32 depth = 0; target && target->tag == JSON_OBJECT && depth < 8; depth++) {
        JsonValue const* next = nullptr;
        for (isize i = 0; i < target->count && !next; i++) {
            csview key = cstr_sv(&target->members[i].key);
            if (csview_equals(key, "node") || csview_equals(key, "require") || csview_equals(key, "types") ||
                csview_equals(key, "default")
            ) {
                next = &target->members[i].value;
            }
        }
        target = next;
    }
    return target && target->tag == JSON_STRING ? target : nullptr;
}

// `"extends": "pkg/sub"` through the package's `exports` (tsc resolves it like a module import: an exact subpath or
// a `./x/*` pattern). False if the package has no exports or they don't have it
static bool resolve_extends_exports(ConfigLoad* me, JsonValue const* package_json, csview package_dir, csview subpath, cstr* out) {
    JsonValue const* exports = json_get_tagged(package_json, "exports", JSON_OBJECT);
    if (!exports) return false;
    for (isize i = 0; i < exports->count; i++) {
        csview key = cstr_sv(&exports->members[i].key);
        isize star = csview_find_sv(key, c_sv("*"));
        csview middle = {0};
        if (star == c_NPOS) {
            if (!csview_equals_sv(key, subpath)) continue;
        } else {
            csview prefix = csview_slice(key, 0, star);
            csview suffix = csview_slice(key, star + 1, key.size);
            if (subpath.size < prefix.size + suffix.size ||
                !csview_equals_sv(csview_slice(subpath, 0, prefix.size), prefix) ||
                !csview_equals_sv(csview_slice(subpath, subpath.size - suffix.size, subpath.size), suffix)
            ) {
                continue;
            }
            middle = csview_slice(subpath, prefix.size, subpath.size - suffix.size);
        }
        JsonValue const* target = exports_target(&exports->members[i].value);
        if (!target) return false;
        csview pattern = cstr_sv(&target->string);
        isize target_star = csview_find_sv(pattern, c_sv("*"));
        cstr relative = star != c_NPOS && target_star != c_NPOS ?
            cstr_d(csview_slice(pattern, 0, target_star), middle, csview_slice(pattern, target_star + 1, pattern.size)) :
            cstr_from_sv(pattern);
        *out = path_join(package_dir, cstr_sv(&relative));
        cstr_drop(&relative);
        if (fs_is_reg_file(my fs, cstr_sv(out))) return true;
        cstr_drop(out);
        return false;
    }
    return false;
}

// What `"extends": "name"` names: a path, or a package (its `tsconfig` field, a file in it, or its tsconfig.json)
static bool resolve_extends(ConfigLoad* me, csview dir, csview name, cstr* out) {
    // (tsgo's getExtendsConfigPath: rooted, `./` or `../`; anything else is resolved like a module)
    bool is_path = name.size && (name.buf[0] == '/' || csview_starts_with(name, "./") || csview_starts_with(name, "../"));
    if (csview_equals(name, ".") || csview_equals(name, "..")) {
        // A relative module name (tsgo's ResolveConfig): the file with `.json`, else the directory's package.json
        // `tsconfig`, else its tsconfig.json
        cstr base = path_join(dir, name);
        cstr candidates[3] = { cstr_d(base, ".json"), cstr_null, cstr_d(base, "/tsconfig.json") };
        cstr package_json_path = cstr_d(base, "/package.json");
        if (fs_is_reg_file(my fs, cstr_sv(&package_json_path))) {
            cstr text = cstr_null;
            bool readable = true;
            try (fs_read_file(my fs, cstr_sv(&package_json_path), &text)) if_err (e) {
                ignore e;
                current_error_destroy();
                readable = false;
            }
            JsonValue json = {0};
            if (readable && json_parse(cstr_sv(&text), &json)) {
                JsonValue const* field = json_get_tagged(&json, "tsconfig", JSON_STRING);
                if (field) candidates[1] = path_join(cstr_sv(&base), cstr_sv(&field->string));
            }
            json_drop(&json);
            if (readable) cstr_drop(&text);
        }
        bool found = false;
        for (u32 i = 0; i < c_arraylen(candidates); i++) {
            if (!found && cstr_size(&candidates[i]) && fs_is_reg_file(my fs, cstr_sv(&candidates[i]))) {
                *out = candidates[i];
                candidates[i] = cstr_null;
                found = true;
            }
            cstr_drop(&candidates[i]);
        }
        c_drop(cstr, &package_json_path, &base);
        return found;
    }
    if (is_path) {
        cstr base = path_join(dir, name);
        // (A `.json` is what's read whether it's there or not: TS5083 then, like tsc's getExtendsConfigPath)
        if (csview_ends_with(name, ".json")) {
            *out = base;
            return true;
        }
        {
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
    // The package's name and the subpath in it (`@scope/pkg/sub`)
    isize name_end = csview_find_sv(name, c_sv("/"));
    if (name_end != c_NPOS && name.buf[0] == '@') {
        isize second = csview_find_sv(csview_slice(name, name_end + 1, name.size), c_sv("/"));
        name_end = second == c_NPOS ? c_NPOS : name_end + 1 + second;
    }
    csview package_name = name_end == c_NPOS ? name : csview_slice(name, 0, name_end);
    cstr subpath = name_end == c_NPOS ? cstr_lit(".") : cstr_d(".", csview_slice(name, name_end, name.size));
    // The package the config is in, by its own name (tsc's loadModuleFromSelfNameReference, before node_modules): the
    // nearest package.json's exports, if its name is the one asked for
    for (cstr scope = cstr_from_sv(dir);;) {
        cstr scope_json_path = cstr_d(scope, "/package.json");
        bool found = fs_is_reg_file(my fs, cstr_sv(&scope_json_path));
        bool exported = false;
        if (found) {
            cstr text = cstr_null;
            bool readable = true;
            try (fs_read_file(my fs, cstr_sv(&scope_json_path), &text)) if_err (e) {
                ignore e;
                current_error_destroy();
                readable = false;
            }
            JsonValue json = {0};
            if (readable && json_parse(cstr_sv(&text), &json)) {
                JsonValue const* own_name = json_get_tagged(&json, "name", JSON_STRING);
                if (own_name && csview_equals_sv(cstr_sv(&own_name->string), package_name)) {
                    exported = resolve_extends_exports(me, &json, cstr_sv(&scope), cstr_sv(&subpath), out);
                }
            }
            json_drop(&json);
            if (readable) cstr_drop(&text);
        }
        cstr_drop(&scope_json_path);
        csview parent = parent_of(cstr_sv(&scope));
        bool top = found || !parent.size || parent.size >= cstr_size(&scope);
        if (!top) {
            cstr next = cstr_from_sv(parent);
            cstr_take(&scope, next);
        }
        if (exported) {
            cstr_drop(&scope);
            cstr_drop(&subpath);
            return true;
        }
        if (top) {
            cstr_drop(&scope);
            break;
        }
    }
    cstr at = cstr_from_sv(dir);
    while (true) {
        // Through the package's exports first
        cstr package_dir = cstr_d(at, "/node_modules/", package_name);
        cstr exports_json_path = cstr_d(package_dir, "/package.json");
        bool exported = false;
        if (fs_is_reg_file(my fs, cstr_sv(&exports_json_path))) {
            cstr text = cstr_null;
            bool readable = true;
            try (fs_read_file(my fs, cstr_sv(&exports_json_path), &text)) if_err (e) {
                ignore e;
                current_error_destroy();
                readable = false;
            }
            JsonValue json = {0};
            if (readable && json_parse(cstr_sv(&text), &json)) {
                exported = resolve_extends_exports(me, &json, cstr_sv(&package_dir), cstr_sv(&subpath), out);
            }
            json_drop(&json);
            if (readable) cstr_drop(&text);
        }
        cstr_drop(&exports_json_path);
        cstr_drop(&package_dir);
        if (exported) {
            cstr_drop(&at);
            cstr_drop(&subpath);
            return true;
        }
        cstr package = cstr_d(at, "/node_modules/", name);
        const char* const suffixes[] = { "", ".json", "/tsconfig.json" };
        for (isize i = 0; i < (isize)c_arraylen(suffixes); i++) {
            cstr candidate = cstr_d(package, suffixes[i]);
            if (fs_is_reg_file(my fs, cstr_sv(&candidate))) {
                cstr_drop(&package);
                cstr_drop(&at);
                cstr_drop(&subpath);
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
            cstr_drop(&subpath);
            return true;
        }
        if (cstr_size(&at) <= 1) break;
        cstr parent = cstr_from_sv(parent_of(cstr_sv(&at)));
        cstr_take(&at, parent);
    }
    cstr_drop(&at);
    cstr_drop(&subpath);
    return false;
}

// A config found in a node_modules is where its links lead: what it extends is found from there (tsc resolves
// `extends` like a module, with realpath). Only on the real filesystem
static void follow_links(ConfigLoad* me, cstr* path) {
    if (my fs != fs || !csview_contains(cstr_sv(path), "/node_modules/") || cstr_size(path) >= PATH_MAX) return;
    char resolved[PATH_MAX];
    if (realpath(cstr_str(path), resolved)) cstr_assign(path, resolved);
}

static_failable load_config(ConfigLoad* me, csview path) {
    if (my depth > 16 || my document_count == (isize)c_arraylen(my documents)) {
        return err_d(path, ": too many levels of 'extends' (a cycle?)");
    }
    cstr contents = cstr_null;
    FailableResult read = fs_read_file(my fs, path, &contents);
    if (read.error) {
        // A config it extends that can't be read: TS5083, and the rest goes on without it
        if (!my depth) return read;
        failable_result_drop(&read);
        config_diagnostic(me, 5083, cstr_d("Cannot read file '", path, "'."), c_sv(""), 0, 0);
        return ok;
    }
    JsonValue* document = c_new(JsonValue, {0});
    my documents[my document_count++] = document;
    // (Syntax errors are recovered from: the program reports them, see program_set_config_file())
    ignore json_parse(cstr_sv(&contents), document);
    cstr_drop(&contents);
    if (document->tag != JSON_OBJECT && document->end > document->start) {
        // tsc's convertConfigFileToObject: the root must be an object (TS5092), but of an array the first object
        // element is the config, silently (the JSON parser's recovery from stray characters makes such arrays)
        JsonValue* first = nullptr;
        for (isize i = 0; document->tag == JSON_ARRAY && i < document->count && !first; i++) {
            if (document->items[i].tag == JSON_OBJECT) first = &document->items[i];
        }
        if (first) {
            JsonValue taken = *first;
            *first = (JsonValue){0};
            json_drop(document);
            *document = taken;
        } else {
            csview base = csview_ends_with(path, "jsconfig.json") ? c_sv("jsconfig.json") : c_sv("tsconfig.json");
            config_diagnostic(me, 5092, cstr_d("The root value of a '", base, "' file must be an object."), path,
                document->start, document->end);
        }
    }

    cstr dir = cstr_from_sv(parent_of(path));
    // What is extended first, so that this config overrides it
    JsonValue const* extends = json_get(document, "extends");
    for (isize i = 0; extends && i < (extends->tag == JSON_ARRAY ? extends->count : 1); i++) {
        JsonValue const* name = extends->tag == JSON_ARRAY ? &extends->items[i] : extends;
        if (name->tag != JSON_STRING) continue;
        cstr base = cstr_null;
        if (!resolve_extends(me, cstr_sv(&dir), cstr_sv(&name->string), &base)) {
            // tsc's TS6053 at the name (TS18051 for an empty one), and the rest goes on without it
            if (!cstr_size(&name->string)) {
                config_diagnostic(me, 18051, cstr_lit("Compiler option 'extends' cannot be given an empty string."),
                    path, name->start, name->end);
            } else {
                config_diagnostic(me, 6053, cstr_d("File '", name->string, "' not found."), path, name->start, name->end);
            }
            continue;
        }
        follow_links(me, &base);
        my depth++;
        try (load_config(me, cstr_sv(&base))) cleanup {
            cstr_drop(&base);
            cstr_drop(&dir);
        }
        my depth--;
        cstr_drop(&base);
    }

    // tsc's parseOwnConfigOfJsonSourceFile on the root's own keys: `excludes` (TS6114), and the first compiler option
    // written outside compilerOptions when there is none (TS6258)
    bool has_compiler_options = json_get(document, "compilerOptions");
    bool root_option_reported = false;
    for (isize i = 0; document->tag == JSON_OBJECT && i < document->count; i++) {
        JsonMember const* member = &document->members[i];
        // (tsc's convertJsonOption of the root's list options)
        bool list = cstr_equals(&member->key, "files") || cstr_equals(&member->key, "include") ||
            cstr_equals(&member->key, "exclude") || cstr_equals(&member->key, "references");
        if (list && member->value.tag != JSON_ARRAY && member->value.tag != JSON_NULL) {
            config_diagnostic(me, 5024, cstr_d("Compiler option '", member->key, "' requires a value of type Array."), path,
                member->value.start, member->value.end);
        }
        // (Their elements: strings, an invalid one is about the list; and the object options)
        for (isize j = 0; list && member->value.tag == JSON_ARRAY && j < member->value.count; j++) {
            JsonValue const* item = &member->value.items[j];
            if (item->tag == JSON_INVALID) {
                config_diagnostic(me, 5024, cstr_d("Compiler option '", member->key, "' requires a value of type Array."), path,
                    item->start, item->end);
            } else if (item->tag != JSON_STRING && item->tag != JSON_NULL && !cstr_equals(&member->key, "references")) {
                config_diagnostic(me, 5024, cstr_d("Compiler option '", member->key, "' requires a value of type string."), path,
                    item->start, item->end);
            }
        }
        if ((cstr_equals(&member->key, "compilerOptions") || cstr_equals(&member->key, "watchOptions") ||
            cstr_equals(&member->key, "typeAcquisition")) && member->value.tag != JSON_OBJECT && member->value.tag != JSON_NULL
        ) {
            config_diagnostic(me, 5024, cstr_d("Compiler option '", member->key, "' requires a value of type object."), path,
                member->value.start, member->value.end);
        }
        // (tsc's convertJsonOption of each reference against its declarations: `path` a non-empty string, `circular` a
        // boolean; an entry without a path is TS5024 on the whole entry)
        if (cstr_equals(&member->key, "references") && member->value.tag == JSON_ARRAY) {
            for (isize r = 0; r < member->value.count; r++) {
                JsonValue const* entry = &member->value.items[r];
                if (entry->tag != JSON_OBJECT) continue;
                JsonValue const* reference_path = json_get(entry, "path");
                if (!reference_path || reference_path->tag != JSON_STRING) {
                    JsonValue const* at = reference_path ? reference_path : entry;
                    config_diagnostic(me, 5024, cstr_lit("Compiler option 'reference.path' requires a value of type string."),
                        path, at->start, at->end);
                } else if (!cstr_size(&reference_path->string)) {
                    config_diagnostic(me, 18051, cstr_lit("Compiler option 'reference.path' cannot be given an empty string."),
                        path, reference_path->start, reference_path->end);
                }
                JsonValue const* circular = json_get(entry, "circular");
                if (circular && circular->tag != JSON_BOOL) {
                    config_diagnostic(me, 5024, cstr_lit("Compiler option 'reference.circular' requires a value of type boolean."),
                        path, circular->start, circular->end);
                }
            }
        }
        if (cstr_equals(&member->key, "excludes")) {
            config_diagnostic(me, 6114, cstr_lit("Unknown option 'excludes'. Did you mean 'exclude'?"), path,
                member->key_start, member->key_end);
        }
        if (!has_compiler_options && !root_option_reported &&
            compiler_option_is_command_option_without_build(cstr_sv(&member->key))
        ) {
            root_option_reported = true;
            config_diagnostic(me, 6258, cstr_d("'", member->key, "' should be set inside the 'compilerOptions' object of the config json file"),
                path, member->key_start, member->key_end);
        }
    }
    JsonValue const* compiler_options = json_get_tagged(document, "compilerOptions", JSON_OBJECT);
    if (compiler_options) apply_compiler_options(me, path, cstr_sv(&dir), compiler_options);
    if (!my depth) check_content_mappers(me, path, document);

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

// allowJs, before compiler_options_resolve() (checkJs implies it)
static bool allows_js(CompilerOptions const* options) {
    return options->is_set[COMPILER_OPTION_allow_js] ? options->allow_js : options->check_js;
}

static bool has_source_extension(ConfigLoad* me, csview name) {
    static const char* const ts[] = { ".ts", ".tsx", ".mts", ".cts" };
    static const char* const js[] = { ".js", ".jsx", ".mjs", ".cjs" };
    for (isize i = 0; i < (isize)c_arraylen(ts); i++) {
        if (csview_ends_with(name, ts[i])) return true;
    }
    if (content_mapper_for_path(my options, name)) return true;
    if (!allows_js(my options)) return false;
    if (csview_ends_with(name, ".min.js")) return false;
    for (isize i = 0; i < (isize)c_arraylen(js); i++) {
        if (csview_ends_with(name, js[i])) return true;
    }
    return false;
}

typedef struct Patterns {
    StrList include;
    StrList exclude;
    StrList json; // Found JSON files: only include patterns ending in `.json` take them, with resolveJsonModule
} Patterns;

static bool is_excluded(Patterns const* patterns, csview path) {
    for (isize i = 0; i < patterns->exclude.count; i++) {
        if (glob_matches(cstr_sv(&patterns->exclude.items[i]), path, true)) return true;
    }
    return false;
}

// resolveJsonModule, before compiler_options_resolve() (the default of bundler resolution, which module preserve has)
static bool resolves_json(CompilerOptions const* options) {
    if (options->is_set[COMPILER_OPTION_resolve_json_module]) return options->resolve_json_module;
    return options->module_resolution_set ? options->module_resolution == MODULE_RESOLUTION_BUNDLER :
        options->module_set && options->module == MODULE_KIND_PRESERVE;
}

// The files of dir (and below) the include patterns match, each into the bucket of the first pattern that matches it
// (tsgo's matchFileNames: one result list per include pattern, flattened in include order)
static void collect_files(ConfigLoad* me, Patterns* patterns, csview dir, StrList* buckets, int depth) {
    if (depth > 64) return;
    StrList subdirs = {0};
    for_each (item, fs_listdir_sorted, my fs, dir) {
        cstr path = path_join(dir, item.name);
        if (fs_is_directory(my fs, cstr_sv(&path))) {
            bool wanted = false;
            for (isize i = 0; i < patterns->include.count && !wanted; i++) {
                wanted = glob_may_match_inside(cstr_sv(&patterns->include.items[i]), cstr_sv(&path));
            }
            // (Wildcards don't go into package directories or hidden ones, glob_may_match_inside() says, but a pattern
            // may name one: `./.vitepress/**/*.ts`)
            if (wanted && !is_excluded(patterns, cstr_sv(&path))) {
                str_list_push(&subdirs, cstr_sv(&path));
            }
        } else if (has_source_extension(me, item.name) && !is_excluded(patterns, cstr_sv(&path))) {
            for (isize i = 0; i < patterns->include.count; i++) {
                if (glob_matches(cstr_sv(&patterns->include.items[i]), cstr_sv(&path), false)) {
                    str_list_push(&buckets[i], cstr_sv(&path));
                    break;
                }
            }
        } else if (resolves_json(my options) && csview_ends_with(item.name, ".json") &&
            !is_excluded(patterns, cstr_sv(&path))
        ) {
            for (isize i = 0; i < patterns->include.count; i++) {
                csview pattern = cstr_sv(&patterns->include.items[i]);
                if (csview_ends_with(pattern, ".json") && glob_matches(pattern, cstr_sv(&path), false)) {
                    str_list_push(&patterns->json, cstr_sv(&path));
                    break;
                }
            }
        }
        cstr_drop(&path);
    }
    for (isize i = 0; i < subdirs.count; i++) collect_files(me, patterns, cstr_sv(&subdirs.items[i]), buckets, depth + 1);
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

// tsc's extension groups (getSupportedExtensions), highest priority first
static char const* const extension_groups[3][5] = {
    { ".ts", ".tsx", ".d.ts", ".js", ".jsx" },
    { ".cts", ".d.cts", ".cjs" },
    { ".mts", ".d.mts", ".mjs" },
};


// tsc's hasFileWithHigherPriorityExtension: a file include patterns found is left out when the same name with an
// extension before its own in its group is there too (literal or found): `a.d.ts` (an output) next to `a.ts`, `a.js`
// next to `a.ts` with allowJs. Not a `.js` for a `.d.ts` (tsc's legacy behaviour)
static bool has_higher_priority_sibling(ConfigLoad* me, HMapPathKind const* files, csview path) {
    for (isize g = 0; g < (isize)c_arraylen(extension_groups); g++) {
        // The file's own extension in the group: the longest that matches (`.d.ts` before `.ts`)
        isize own = -1;
        for (isize e = 0; e < 5 && extension_groups[g][e]; e++) {
            char const* extension = extension_groups[g][e];
            if (!csview_ends_with(path, extension)) continue;
            if (own < 0 || strlen(extension) > strlen(extension_groups[g][own])) own = e;
        }
        if (own < 0) continue;
        bool is_js = csview_ends_with(path, ".js") || csview_ends_with(path, ".jsx") || csview_ends_with(path, ".cjs") ||
            csview_ends_with(path, ".mjs");
        if (is_js && !allows_js(my options)) return false;
        csview stem = csview_slice(path, 0, path.size - (isize)strlen(extension_groups[g][own]));
        for (isize e = 0; e < own; e++) {
            char const* extension = extension_groups[g][e];
            if (is_js && !strncmp(extension, ".d.", 3)) continue;
            if (!allows_js(my options) && (!strcmp(extension, ".js") || !strcmp(extension, ".jsx"))) continue;
            cstr sibling = cstr_d(stem, extension);
            bool there = hmap_path_kind_get(files, cstr_sv(&sibling)) != nullptr;
            cstr_drop(&sibling);
            if (there) return true;
        }
        return false;
    }
    return false;
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
    // Without an `exclude`, the outputs are (tsc's getConfigFileSpecs: outDir and declarationDir)
    if (!my exclude) {
        if (cstr_size(&my options->out_dir)) str_list_push(&patterns.exclude, cstr_sv(&my options->out_dir));
        if (cstr_size(&my options->declaration_dir)) str_list_push(&patterns.exclude, cstr_sv(&my options->declaration_dir));
    }

    // One walk per distinct base directory, skipping bases inside another (tsgo's getBasePaths). tsgo sorts them first,
    // which with its include-ordered buckets doesn't change the files' order
    StrList found = {0};
    StrList bases = {0};
    StrList all = {0};
    for (isize i = 0; i < patterns.include.count; i++) {
        cstr base = pattern_base(cstr_sv(&patterns.include.items[i]));
        str_list_push(&all, cstr_sv(&base));
        cstr_drop(&base);
    }
    for (isize i = 0; i < all.count; i++) {
        csview base = cstr_sv(&all.items[i]);
        bool covered = false;
        for (isize j = 0; j < all.count && !covered; j++) {
            csview other = cstr_sv(&all.items[j]);
            if (j == i) continue;
            if (csview_equals_sv(base, other)) covered = j < i;
            else covered = base.size > other.size && !memcmp(base.buf, other.buf, (size_t)other.size) &&
                (other.size == 1 || base.buf[other.size] == '/');
        }
        if (!covered) str_list_push(&bases, base);
    }
    str_list_drop(&all);
    StrList* buckets = calloc((size_t)patterns.include.count + 1, sizeof(StrList));
    for (isize i = 0; i < bases.count; i++) {
        if (fs_is_directory(my fs, cstr_sv(&bases.items[i]))) {
            collect_files(me, &patterns, cstr_sv(&bases.items[i]), buckets, 0);
        }
    }
    for (isize i = 0; i < patterns.include.count; i++) {
        for (isize j = 0; j < buckets[i].count; j++) str_list_push(&found, cstr_sv(&buckets[i].items[j]));
        str_list_drop(&buckets[i]);
    }
    free(buckets);
    HMapPathKind files = {0};
    for (isize i = 0; i < out->count; i++) hmap_path_kind_insert(&files, cstr_clone(out->items[i]), 1);
    for (isize i = 0; i < found.count; i++) hmap_path_kind_insert(&files, cstr_clone(found.items[i]), 2);
    for (isize i = 0; i < found.count; i++) {
        csview path = cstr_sv(&found.items[i]);
        if (hmap_path_kind_get(&files, path)->second == 1 || has_higher_priority_sibling(me, &files, path)) continue;
        str_list_push(out, path);
    }
    // (tsc's wildCardJsonFileMap comes last)
    for (isize i = 0; i < patterns.json.count; i++) {
        csview path = cstr_sv(&patterns.json.items[i]);
        if (hmap_path_kind_get(&files, path)) continue;
        hmap_path_kind_insert(&files, cstr_from_sv(path), 2);
        str_list_push(out, path);
    }
    hmap_path_kind_drop(&files);
    str_list_drop(&patterns.json);
    str_list_drop(&found);
    str_list_drop(&bases);
    str_list_drop(&patterns.include);
    str_list_drop(&patterns.exclude);
}

// A spec as TS18003 shows it: one inherited from an extended config is rebased onto the config's directory (tsc's
// parseConfig's updatePath: combinePaths of the relative directory and the spec, not normalized)
static void push_shown_spec(ConfigLoad* me, StrList* list, csview dir, csview spec) {
    if (cstr_equals_sv(&my config_dir, dir) || (spec.size && spec.buf[0] == '/') || csview_starts_with(spec, "${configDir}")) {
        str_list_push(list, spec);
        return;
    }
    cstr relative = relative_path_of(cstr_sv(&my config_dir), dir);
    cstr shown = !cstr_size(&relative) ? cstr_from_sv(spec) : cstr_ends_with(&relative, "/") ? cstr_d(relative, spec) :
        cstr_d(relative, "/", spec);
    str_list_push(list, cstr_sv(&shown));
    c_drop(cstr, &shown, &relative);
}

// tsc's configFileSpecs into the options (tsconfig_root_file_reason() reads them)
static void record_config_specs(ConfigLoad* me) {
    CompilerOptions* o = my options;
    str_list_clear(&o->config_files);
    str_list_clear(&o->config_include_specs);
    str_list_clear(&o->config_include_patterns);
    for (isize i = 0; my files && i < my files->count; i++) {
        if (my files->items[i].tag != JSON_STRING) continue;
        cstr path = config_path(me, cstr_sv(&my files_dir), cstr_sv(&my files->items[i].string));
        str_list_push(&o->config_files, cstr_sv(&path));
        cstr_drop(&path);
    }
    for (isize i = 0; my include && i < my include->count; i++) {
        if (my include->items[i].tag != JSON_STRING) continue;
        cstr pattern = include_pattern(me, cstr_sv(&my include_dir), cstr_sv(&my include->items[i].string));
        str_list_push(&o->config_include_specs, cstr_sv(&my include->items[i].string));
        str_list_push(&o->config_include_patterns, cstr_sv(&pattern));
        cstr_drop(&pattern);
    }
    o->config_default_include = !my files && !my include;
    str_list_clear(&o->config_include_shown);
    str_list_clear(&o->config_exclude_shown);
    for (isize i = 0; my include && i < my include->count; i++) {
        if (my include->items[i].tag != JSON_STRING) continue;
        push_shown_spec(me, &o->config_include_shown, cstr_sv(&my include_dir), cstr_sv(&my include->items[i].string));
    }
    for (isize i = 0; my exclude && i < my exclude->count; i++) {
        if (my exclude->items[i].tag != JSON_STRING) continue;
        push_shown_spec(me, &o->config_exclude_shown, cstr_sv(&my exclude_dir), cstr_sv(&my exclude->items[i].string));
    }
    o->config_exclude_given = my exclude != nullptr;
}

ConfigSpecMatch tsconfig_match_root_file(CompilerOptions const* options, csview path, isize* index) {
    // getMatchedFileSpec, then getMatchedIncludeSpec (a JSON file only by a spec ending in .json)
    for (isize i = 0; i < options->config_files.count; i++) {
        if (csview_equals_sv(cstr_sv(&options->config_files.items[i]), path)) {
            *index = i;
            return CONFIG_SPEC_FILES;
        }
    }
    if (options->config_default_include) return CONFIG_SPEC_DEFAULT_INCLUDE;
    bool json = csview_ends_with(path, ".json");
    for (isize i = 0; i < options->config_include_patterns.count; i++) {
        csview pattern = cstr_sv(&options->config_include_patterns.items[i]);
        if (json && !csview_ends_with(pattern, ".json")) continue;
        if (glob_matches(pattern, path, false)) {
            *index = i;
            return CONFIG_SPEC_INCLUDE;
        }
    }
    return CONFIG_SPEC_NONE;
}

cstr tsconfig_root_file_reason(CompilerOptions const* options, csview path, csview config_name) {
    if (!options->config_files.count && !options->config_include_specs.count && !options->config_default_include) {
        return cstr_lit("Root file specified for compilation");
    }
    isize index = 0;
    switch (tsconfig_match_root_file(options, path, &index)) {
        case CONFIG_SPEC_FILES: return cstr_lit("Part of 'files' list in tsconfig.json");
        case CONFIG_SPEC_DEFAULT_INCLUDE: return cstr_lit("Matched by default include pattern '**/*'");
        case CONFIG_SPEC_INCLUDE:
            return cstr_d("Matched by include pattern '", options->config_include_specs.items[index], "' in '", config_name, "'");
        case CONFIG_SPEC_NONE: break;
    }
    return cstr_lit("Root file specified for compilation");
}

failable tsconfig_load(Fs const* fs, csview path, CompilerOptions* options, StrList* root_files) {
    return tsconfig_load_diagnosed(fs, path, options, root_files, nullptr);
}

failable tsconfig_load_diagnosed(Fs const* fs, csview path, CompilerOptions* options, StrList* root_files,
    TsconfigDiagnostics* diagnostics
) {
    ConfigLoad load = { .fs = fs, .options = options, .diagnostics = diagnostics };
    try (fs_get_cwd(fs, &load.cwd)) bubble;
    cstr absolute = absolute_path_of(cstr_sv(&load.cwd), path);
    if (fs_is_directory(fs, cstr_sv(&absolute))) cstr_append_lit(&absolute, "/tsconfig.json");
    load.config_dir = cstr_from_sv(parent_of(cstr_sv(&absolute)));

    FailableResult result = load_config(&load, cstr_sv(&absolute));
    if (!result.error) {
        cstr_assign_sv(&options->config_dir, cstr_sv(&load.config_dir));
        record_config_specs(&load);
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

failable tsconfig_load_references(Fs const* fs, csview path, TsconfigReference** out, u32* count, bool* has_references) {
    cstr cwd = cstr_null;
    try (fs_get_cwd(fs, &cwd)) bubble;
    cstr absolute = absolute_path_of(cstr_sv(&cwd), path);
    cstr_drop(&cwd);
    if (fs_is_directory(fs, cstr_sv(&absolute))) cstr_append_lit(&absolute, "/tsconfig.json");
    cstr contents = cstr_null;
    try (fs_read_file(fs, cstr_sv(&absolute), &contents)) cleanup {
        cstr_drop(&absolute);
    }
    JsonValue document = {0};
    bool parsed = json_parse(cstr_sv(&contents), &document);
    cstr_drop(&contents);
    JsonValue const* references = parsed ? json_get_tagged(&document, "references", JSON_ARRAY) : nullptr;
    *has_references = references != nullptr;
    csview dir = parent_of(cstr_sv(&absolute));
    for (isize i = 0; references && i < references->count; i++) {
        JsonValue const* entry = &references->items[i];
        csview written = json_get_string(entry, "path");
        if (entry->tag != JSON_OBJECT || !written.size) continue;
        JsonValue const* circular = json_get_tagged(entry, "circular", JSON_BOOL);
        JsonValue const* prepend = json_get_tagged(entry, "prepend", JSON_BOOL);
        TsconfigReference reference = {
            .written = path_join(dir, written),
            .circular = circular && circular->boolean,
            .prepend = prepend && prepend->boolean,
            .start = entry->start,
            .end = entry->end,
        };
        reference.path = csview_ends_with(written, ".json") ? cstr_clone(reference.written) :
            path_join(cstr_sv(&reference.written), c_sv("tsconfig.json"));
        *out = realloc(*out, sizeof(TsconfigReference) * (*count + 1));
        (*out)[(*count)++] = reference;
    }
    json_drop(&document);
    cstr_drop(&absolute);
    return ok;
}

void tsconfig_references_drop(TsconfigReference* references, u32 count) {
    for (u32 i = 0; i < count; i++) {
        cstr_drop(&references[i].path);
        cstr_drop(&references[i].written);
    }
    free(references);
}
