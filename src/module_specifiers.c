#include "module_specifiers.h"
#include "json.h"
#include <pthread.h>

// A port of tsc's moduleSpecifiers.ts (computeModuleSpecifiers and what it calls) for declaration emit's questions.
// Paths are case sensitive (tsc's getCanonicalFileName is the identity there). Not ported: auto-import preferences
// (exclude regexes, `relative`/`non-relative`), the language service's project reference redirects, ambient modules
// (the caller names those itself), the output files a package.json `imports` entry may point to

// ---- Symlink cache ----

typedef struct SymlinkDir {
    cstr real; // With a trailing separator
    cstr link; // Without
} SymlinkDir;

struct SymlinkCache {
    pthread_mutex_t lock;
    SymlinkDir* dirs;
    u32 count;
    u32 cap;
    // The package directories whose dependencies were resolved
    StrList dependency_scopes;
};

SymlinkCache* symlink_cache_new(void) {
    SymlinkCache* ret = c_new(SymlinkCache, {0});
    pthread_mutex_init(&ret->lock, nullptr);
    return ret;
}

void symlink_cache_destroy(SymlinkCache* me) {
    if (!me) return;
    for (u32 i = 0; i < my count; i++) c_drop(cstr, &my dirs[i].real, &my dirs[i].link);
    free(my dirs);
    str_list_drop(&my dependency_scopes);
    pthread_mutex_destroy(&my lock);
    free(me);
}

// tsc's containsIgnoredPath: pnpm's `.pnpm` store and the like are never named
static bool contains_ignored_path(csview path) {
    return csview_find_sv(path, c_sv("/node_modules/.")) != c_NPOS || csview_find_sv(path, c_sv("/.git")) != c_NPOS ||
        csview_find_sv(path, c_sv("/.#")) != c_NPOS;
}

bool module_path_is_ignored(csview path) {
    return contains_ignored_path(path);
}

static csview last_segment(csview path) {
    isize i = path.size;
    while (i > 0 && path.buf[i - 1] != '/') i--;
    return csview_slice(path, i, path.size);
}

// Of an absolute path, without a trailing separator ("/" for what is directly in the root)
static csview parent_dir(csview path) {
    isize i = path.size;
    while (i > 0 && path.buf[i - 1] != '/') i--;
    if (i > 1) i--;
    return csview_slice(path, 0, i ? i : 1);
}

static bool is_node_modules_or_scope(csview segment) {
    return csview_equals(segment, "node_modules") || (segment.size && segment.buf[0] == '@');
}

// (under the lock)
static void add_dir(SymlinkCache* me, csview link, csview real) {
    if (contains_ignored_path(link)) return;
    for (u32 i = 0; i < my count; i++) {
        if (cstr_equals_sv(&my dirs[i].link, link)) return;
    }
    if (my count == my cap) {
        my cap = my cap ? my cap * 2 : 16;
        my dirs = realloc(my dirs, my cap * sizeof(*my dirs));
    }
    cstr real_dir = real.size > 1 ? cstr_d(real, "/") : cstr_from("/");
    my dirs[my count++] = (SymlinkDir){ .real = real_dir, .link = cstr_from_sv(link) };
}

// tsc's processResolution and guessDirectorySymlink: the deepest directories of the two paths whose rest is the same
// (not across a node_modules or a scope directory, which are unlikely to be the link themselves)
static void add_resolution(SymlinkCache* me, csview original, csview real) {
    bool directory = false;
    while (true) {
        csview a_parent = parent_dir(real), b_parent = parent_dir(original);
        if (a_parent.size == real.size || b_parent.size == original.size) break; // At the root
        if (is_node_modules_or_scope(last_segment(a_parent)) || is_node_modules_or_scope(last_segment(b_parent))) break;
        if (!csview_equals_sv(last_segment(real), last_segment(original))) break;
        real = a_parent;
        original = b_parent;
        directory = true;
    }
    if (directory) add_dir(me, original, real);
}

void symlink_cache_add_resolution(SymlinkCache* me, csview original, csview real) {
    pthread_mutex_lock(&my lock);
    add_resolution(me, original, real);
    pthread_mutex_unlock(&my lock);
}

void symlink_cache_add_dependencies(SymlinkCache* me, ModuleResolver* resolver, csview package_dir) {
    pthread_mutex_lock(&my lock);
    for (isize i = 0; i < my dependency_scopes.count; i++) {
        if (cstr_equals_sv(&my dependency_scopes.items[i], package_dir)) {
            pthread_mutex_unlock(&my lock);
            return;
        }
    }
    str_list_push(&my dependency_scopes, package_dir);
    // (under the lock: a checker asking for a specifier meanwhile waits for the links)
    JsonValue const* package = module_resolver_package_json(resolver, package_dir);
    cstr package_json = cstr_d(package_dir, "/package.json");
    static char const* const fields[] = { "dependencies", "peerDependencies", "optionalDependencies" };
    for (u32 f = 0; f < c_arraylen(fields); f++) {
        JsonValue const* deps = json_get_tagged(package, fields[f], JSON_OBJECT);
        for (isize i = 0; deps && i < deps->count; i++) {
            ResolvedModule resolved = module_resolver_resolve_package_directory(
                resolver, cstr_sv(&deps->members[i].key), cstr_sv(&package_json)
            );
            if (cstr_size(&resolved.original_path) && cstr_size(&resolved.path)) {
                cstr original = cstr_d(resolved.original_path, "/package.json");
                cstr real = cstr_d(resolved.path, "/package.json");
                add_resolution(me, cstr_sv(&original), cstr_sv(&real));
                c_drop(cstr, &original, &real);
            }
            resolved_module_drop(&resolved);
        }
    }
    cstr_drop(&package_json);
    pthread_mutex_unlock(&my lock);
}

// The links to the directory real_dir (with a trailing separator)
static StrList links_of(SymlinkCache* me, csview real_dir) {
    StrList ret = {0};
    pthread_mutex_lock(&my lock);
    for (u32 i = 0; i < my count; i++) {
        if (cstr_equals_sv(&my dirs[i].real, real_dir)) str_list_push(&ret, cstr_sv(&my dirs[i].link));
    }
    pthread_mutex_unlock(&my lock);
    return ret;
}

// ---- Paths and extensions ----

static bool starts_with(csview s, csview prefix) {
    return s.size >= prefix.size && !memcmp(s.buf, prefix.buf, (size_t)prefix.size);
}

static bool ends_with(csview s, csview suffix) {
    return s.size >= suffix.size && !memcmp(s.buf + s.size - suffix.size, suffix.buf, (size_t)suffix.size);
}

// tsc's startsWithDirectory: path is inside dir
static bool starts_with_directory(csview path, csview dir) {
    if (!starts_with(path, dir)) return false;
    if (dir.size && dir.buf[dir.size - 1] == '/') return true;
    return path.size > dir.size && path.buf[dir.size] == '/';
}

// tsc's containsPath: child is parent or inside it
static bool contains_path(csview parent, csview child) {
    return csview_equals_sv(parent, child) || starts_with_directory(child, parent);
}

// tsc's pathIsRelative
static bool path_is_relative(csview path) {
    return starts_with(path, c_sv("./")) || starts_with(path, c_sv("../")) || csview_equals(path, ".") ||
        csview_equals(path, "..");
}

static bool path_contains_node_modules(csview path) {
    return csview_find_sv(path, c_sv("/node_modules/")) != c_NPOS;
}

// A path relative to another, either both absolute or both relative (tsc's getRelativePathFromDirectory)
static cstr relative_from(csview dir, csview to) {
    if (dir.size && dir.buf[0] == '/') return relative_path_of(dir, to);
    cstr a = cstr_d("/", dir), b = cstr_d("/", to);
    cstr ret = relative_path_of(cstr_sv(&a), cstr_sv(&b));
    c_drop(cstr, &a, &b);
    return ret;
}

// tsc's ensurePathIsNonModuleName
static cstr non_module_name(cstr path) {
    csview p = cstr_sv(&path);
    if ((p.size && p.buf[0] == '/') || path_is_relative(p)) return path;
    cstr ret = cstr_d("./", p);
    cstr_drop(&path);
    return ret;
}

// getNormalizedAbsolutePath without a current directory: `.` and `..` resolved, a relative path stays relative
static cstr normalize(csview path) {
    if (path.size && path.buf[0] == '/') return absolute_path_of(c_sv("/"), path);
    cstr absolute = absolute_path_of(c_sv("/"), path);
    cstr ret = cstr_from_sv(csview_slice(cstr_sv(&absolute), 1, cstr_size(&absolute)));
    cstr_drop(&absolute);
    return ret;
}

static cstr combine(csview a, csview b) {
    if (b.size && b.buf[0] == '/') return cstr_from_sv(b);
    if (!a.size) return cstr_from_sv(b);
    return a.buf[a.size - 1] == '/' ? cstr_d(a, b) : cstr_d(a, "/", b);
}

static char const* const removable_extensions[] = {
    ".d.ts", ".d.mts", ".d.cts", ".mjs", ".mts", ".cjs", ".cts", ".ts", ".js", ".tsx", ".jsx", ".json",
};

// tsc's tryGetExtensionFromPath over extensionsToRemove, null if none
static char const* extension_of(csview path) {
    for (u32 i = 0; i < c_arraylen(removable_extensions); i++) {
        if (ends_with(path, csview_from(removable_extensions[i]))) return removable_extensions[i];
    }
    return nullptr;
}

static csview remove_extension(csview path) {
    char const* extension = extension_of(path);
    return extension ? csview_slice(path, 0, path.size - (isize)strlen(extension)) : path;
}

static bool extension_is(csview path, char const* extension) {
    return ends_with(path, csview_from(extension));
}

// extensionsNotSupportingExtensionlessResolution
static bool extension_needs_writing(csview path) {
    return extension_is(path, ".mts") || extension_is(path, ".d.mts") || extension_is(path, ".mjs") ||
        extension_is(path, ".cts") || extension_is(path, ".d.cts") || extension_is(path, ".cjs");
}

static bool has_ts_extension(csview path) {
    return extension_is(path, ".ts") || extension_is(path, ".tsx") || extension_is(path, ".mts") || extension_is(path, ".cts");
}

static bool has_js_extension(csview path) {
    return extension_is(path, ".js") || extension_is(path, ".jsx") || extension_is(path, ".mjs") || extension_is(path, ".cjs");
}

// tsc's isDeclarationFileName
static bool is_declaration_name(csview path) {
    if (extension_is(path, ".d.ts") || extension_is(path, ".d.mts") || extension_is(path, ".d.cts")) return true;
    csview base = last_segment(path);
    return ends_with(base, c_sv(".ts")) && csview_find_sv(base, c_sv(".d.")) != c_NPOS;
}

static bool has_implementation_ts_extension(csview path) {
    return has_ts_extension(path) && !is_declaration_name(path);
}

// tsc's tryGetJSExtensionForFile, null if none
static char const* js_extension_for(csview path, CompilerOptions const* options) {
    char const* extension = extension_of(path);
    if (!extension) return nullptr;
    if (!strcmp(extension, ".ts") || !strcmp(extension, ".d.ts")) return ".js";
    if (!strcmp(extension, ".tsx")) return options->jsx == JSX_PRESERVE ? ".jsx" : ".js";
    if (!strcmp(extension, ".js") || !strcmp(extension, ".jsx") || !strcmp(extension, ".json")) return extension;
    if (!strcmp(extension, ".d.mts") || !strcmp(extension, ".mts") || !strcmp(extension, ".mjs")) return ".mjs";
    return ".cjs";
}

// tsc's tryGetRealFileNameForNonJsDeclarationFileName: `foo.d.json.ts` is foo.json
static cstr real_name_of_non_js_declaration(csview path) {
    csview no_extension = csview_slice(path, 0, path.size - 3);
    csview base = last_segment(no_extension);
    isize dot = base.size;
    while (dot > 0 && base.buf[dot - 1] != '.') dot--;
    csview extension = dot ? csview_slice(base, dot - 1, base.size) : c_sv("");
    isize d = csview_find_sv(no_extension, c_sv(".d."));
    return cstr_d(csview_slice(no_extension, 0, d), extension);
}

typedef enum Ending : uint8_t {
    ENDING_MINIMAL,
    ENDING_INDEX,
    ENDING_JS,
    ENDING_TS,
} Ending;

typedef struct Endings {
    Ending items[4];
    u32 len;
} Endings;

static i32 ending_priority(Endings const* endings, Ending ending) {
    for (u32 i = 0; i < endings->len; i++) {
        if (endings->items[i] == ending) return (i32)i;
    }
    return -1;
}

// tsc's tryGetAnyFileFromPath: a file that would be found before a directory of that name
static bool any_file_at(ModuleResolver* resolver, csview path) {
    static char const* const extensions[] = {
        ".ts", ".tsx", ".d.ts", ".cts", ".d.cts", ".mts", ".d.mts", ".js", ".jsx", ".cjs", ".mjs", ".node", ".json",
    };
    cstr absolute = absolute_path_of(module_resolver_cwd(resolver), path);
    bool found = false;
    for (u32 i = 0; i < c_arraylen(extensions) && !found; i++) {
        cstr candidate = cstr_d(absolute, extensions[i]);
        found = module_resolver_file_exists(resolver, cstr_sv(&candidate));
        cstr_drop(&candidate);
    }
    cstr_drop(&absolute);
    return found;
}

// tsc's processEnding. resolver: whether the filesystem may be asked (removing `/index` when a file has its name)
static cstr process_ending(csview path, Endings const* endings, CompilerOptions const* options, ModuleResolver* resolver) {
    if (extension_is(path, ".json") || extension_is(path, ".mjs") || extension_is(path, ".cjs")) return cstr_from_sv(path);
    csview no_extension = remove_extension(path);
    if (no_extension.size == path.size) return cstr_from_sv(path);
    i32 js_priority = ending_priority(endings, ENDING_JS);
    i32 ts_priority = ending_priority(endings, ENDING_TS);
    if ((extension_is(path, ".mts") || extension_is(path, ".cts")) && ts_priority != -1 && ts_priority < js_priority) {
        return cstr_from_sv(path);
    }
    if (extension_is(path, ".d.mts") || extension_is(path, ".mts") || extension_is(path, ".d.cts") || extension_is(path, ".cts")) {
        return cstr_d(no_extension, js_extension_for(path, options));
    }
    if (!extension_is(path, ".d.ts") && extension_is(path, ".ts") && csview_find_sv(path, c_sv(".d.")) != c_NPOS) {
        return real_name_of_non_js_declaration(path);
    }
    switch (endings->items[0]) {
        case ENDING_MINIMAL: {
            csview without_index = ends_with(no_extension, c_sv("/index")) ?
                csview_slice(no_extension, 0, no_extension.size - 6) : no_extension;
            if (resolver && without_index.size != no_extension.size && any_file_at(resolver, without_index)) {
                return cstr_from_sv(no_extension);
            }
            return cstr_from_sv(without_index);
        }
        case ENDING_INDEX:
            return cstr_from_sv(no_extension);
        case ENDING_JS:
            return cstr_d(no_extension, js_extension_for(path, options));
        case ENDING_TS:
            if (is_declaration_name(path)) {
                i32 extensionless = -1;
                for (u32 i = 0; i < endings->len; i++) {
                    if (endings->items[i] == ENDING_MINIMAL || endings->items[i] == ENDING_INDEX) {
                        extensionless = (i32)i;
                        break;
                    }
                }
                return extensionless != -1 && extensionless < js_priority ? cstr_from_sv(no_extension) :
                    cstr_d(no_extension, js_extension_for(path, options));
            }
            return cstr_from_sv(path);
    }
    impossible;
}

static u32 separators(csview path) {
    u32 n = 0;
    for (isize i = 0; i < path.size; i++) n += path.buf[i] == '/';
    return n;
}

// ---- Options and preferences ----

typedef struct Generator {
    SpecifierRequest const* request;
    CompilerOptions const* options;
    ModuleResolver* resolver;
    csview source_dir;
    bool node_next; // moduleResolution node16..nodenext
    Ending file_preferred; // getPreferredEnding() of the importing file
    bool prefer_js; // importModuleSpecifierEnding "js" (declaration emit asks for it in ESM)
} Generator;

static bool allow_ts_extensions(CompilerOptions const* options) {
    return options->allow_importing_ts_extensions || options->rewrite_relative_import_extensions;
}

// tsc's getModuleSpecifierEndingPreference with the importing file
static Ending ending_preference(Generator* me, SpecifierMode mode) {
    SpecifierRequest const* r = my request;
    bool allow_ts = allow_ts_extensions(my options);
    // inferPreference()
    bool uses_js = false;
    bool uses_ts = false;
    for (u32 i = 0; i < r->import_count; i++) {
        csview text = r->imports[i].text;
        if (!path_is_relative(text)) continue;
        // (tsgo skips every relative import for a CommonJS specifier under node16/nodenext: tsc's check that the
        // import is an ES one is a TODO there, so such a file prefers no extension whatever it imports)
        if (my node_next && mode == SPECIFIER_MODE_COMMONJS) continue;
        if (extension_needs_writing(text)) continue;
        if (has_ts_extension(text)) {
            uses_ts = true;
            break;
        }
        if (has_js_extension(text)) uses_js = true;
    }
    Ending inferred = uses_ts ? ENDING_TS : uses_js ? ENDING_JS : ENDING_MINIMAL;
    if (my prefer_js || (mode == SPECIFIER_MODE_ESM && my node_next)) {
        if (!allow_ts) return ENDING_JS;
        return inferred != ENDING_JS ? ENDING_TS : ENDING_JS;
    }
    if (!allow_ts) {
        // usesExtensionsOnImports: the first relative import that may leave its extension out decides
        for (u32 i = 0; i < r->import_count; i++) {
            csview text = r->imports[i].text;
            if (!path_is_relative(text) || extension_needs_writing(text)) continue;
            return has_js_extension(text) || has_ts_extension(text) ? ENDING_JS : ENDING_MINIMAL;
        }
        return ENDING_MINIMAL;
    }
    return inferred;
}

static void endings_push(Endings* me, Ending ending) {
    my items[my len++] = ending;
}

// getAllowedEndingsInPreferredOrder. NONE: the file's own format
static Endings allowed_endings(Generator* me, SpecifierMode syntax_mode) {
    Endings ret = {0};
    SpecifierMode implied = my request->importing_default_mode;
    Ending preferred = syntax_mode != implied ? ending_preference(me, syntax_mode ? syntax_mode : implied) : my file_preferred;
    // (the importing file is never a declaration file: allowImportingTsExtensions alone decides)
    bool allow_ts = allow_ts_extensions(my options);
    if ((syntax_mode ? syntax_mode : implied) == SPECIFIER_MODE_ESM && my node_next) {
        if (allow_ts) endings_push(&ret, ENDING_TS);
        endings_push(&ret, ENDING_JS);
        return ret;
    }
    switch (preferred) {
        case ENDING_JS:
            endings_push(&ret, ENDING_JS);
            if (allow_ts) endings_push(&ret, ENDING_TS);
            endings_push(&ret, ENDING_MINIMAL);
            endings_push(&ret, ENDING_INDEX);
            break;
        case ENDING_TS:
            endings_push(&ret, ENDING_TS);
            endings_push(&ret, ENDING_MINIMAL);
            endings_push(&ret, ENDING_JS);
            endings_push(&ret, ENDING_INDEX);
            break;
        case ENDING_INDEX:
            endings_push(&ret, ENDING_INDEX);
            endings_push(&ret, ENDING_MINIMAL);
            if (allow_ts) endings_push(&ret, ENDING_TS);
            endings_push(&ret, ENDING_JS);
            break;
        case ENDING_MINIMAL:
            endings_push(&ret, ENDING_MINIMAL);
            endings_push(&ret, ENDING_INDEX);
            if (allow_ts) endings_push(&ret, ENDING_TS);
            endings_push(&ret, ENDING_JS);
            break;
    }
    return ret;
}

// tsc's getConditions
typedef struct Conditions {
    char const* items[24];
    u32 len;
    bool types;
} Conditions;

static Conditions conditions_for(CompilerOptions const* options, SpecifierMode mode) {
    Conditions ret = {0};
    if (mode == SPECIFIER_MODE_NONE) {
        if (options->module_resolution == MODULE_RESOLUTION_BUNDLER) mode = SPECIFIER_MODE_ESM;
    }
    ret.items[ret.len++] = mode == SPECIFIER_MODE_ESM ? "import" : "require";
    ret.items[ret.len++] = "types";
    ret.types = true;
    if (options->module_resolution != MODULE_RESOLUTION_BUNDLER) ret.items[ret.len++] = "node";
    for (isize i = 0; i < options->custom_conditions.count && ret.len < c_arraylen(ret.items); i++) {
        ret.items[ret.len++] = cstr_str(&options->custom_conditions.items[i]);
    }
    return ret;
}

static bool condition_applies(Conditions const* conditions, csview key) {
    if (csview_equals(key, "default")) return true;
    for (u32 i = 0; i < conditions->len; i++) {
        if (csview_equals(key, conditions->items[i])) return true;
    }
    // isApplicableVersionedTypesKey
    return conditions->types && starts_with(key, c_sv("types@")) &&
        module_resolver_version_in_range(csview_slice(key, 6, key.size));
}

static bool resolves_package_json_exports(CompilerOptions const* options) {
    ModuleResolutionKind kind = options->module_resolution;
    bool supported = kind == MODULE_RESOLUTION_NODE16 || kind == MODULE_RESOLUTION_NODENEXT || kind == MODULE_RESOLUTION_BUNDLER;
    return supported && options->resolve_package_json_exports;
}

static bool resolves_package_json_imports(CompilerOptions const* options) {
    ModuleResolutionKind kind = options->module_resolution;
    bool supported = kind == MODULE_RESOLUTION_NODE16 || kind == MODULE_RESOLUTION_NODENEXT || kind == MODULE_RESOLUTION_BUNDLER;
    return supported && options->resolve_package_json_imports;
}

// tsc's getNearestAncestorDirectoryWithPackageJson, empty if none
static csview nearest_package_json_dir(Generator* me, csview dir) {
    while (true) {
        if (module_resolver_package_json(my resolver, dir)) return dir;
        csview parent = parent_dir(dir);
        if (parent.size == dir.size) return c_sv("");
        dir = parent;
    }
}

// ---- package.json `exports` and `imports` ----

typedef enum MatchingMode {
    MATCH_EXACT,
    MATCH_DIRECTORY,
    MATCH_PATTERN,
} MatchingMode;

static MatchingMode matching_mode_of(csview key) {
    if (ends_with(key, c_sv("/"))) return MATCH_DIRECTORY;
    return csview_find_sv(key, c_sv("*")) != c_NPOS ? MATCH_PATTERN : MATCH_EXACT;
}

static cstr replace_first_star(csview text, csview star) {
    isize at = csview_find_sv(text, c_sv("*"));
    if (at == c_NPOS) return cstr_from_sv(text);
    return cstr_d(csview_slice(text, 0, at), star, csview_slice(text, at + 1, text.size));
}

// tsc's tryGetModuleNameFromExportsOrImports, empty if the entry doesn't lead to target. (The output files an
// `imports` entry may name instead of the source aren't looked at)
static cstr from_exports_or_imports(
    Generator* me, csview target, csview package_dir, csview package_name, JsonValue const* exports,
    Conditions const* conditions, MatchingMode mode, bool prefer_ts
) {
    if (exports->tag == JSON_STRING) {
        cstr joined = combine(package_dir, cstr_sv(&exports->string));
        cstr pattern = normalize(cstr_sv(&joined));
        cstr_drop(&joined);
        csview p = cstr_sv(&pattern);
        cstr swapped = cstr_null;
        if (has_ts_extension(target) && js_extension_for(target, my options)) {
            swapped = cstr_d(remove_extension(target), js_extension_for(target, my options));
        }
        csview s = cstr_sv(&swapped);
        bool can_try_ts = prefer_ts && has_implementation_ts_extension(target);
        cstr ret = cstr_null;
        switch (mode) {
            case MATCH_EXACT:
                if ((s.size && csview_equals_sv(s, p)) || csview_equals_sv(target, p)) ret = cstr_from_sv(package_name);
                break;
            case MATCH_DIRECTORY: {
                csview from = c_sv("");
                if (can_try_ts && contains_path(target, p)) from = target;
                else if (s.size && contains_path(p, s)) from = s;
                else if (!can_try_ts && contains_path(p, target)) from = target;
                if (from.size) {
                    cstr fragment = relative_from(p, from);
                    cstr base = combine(package_name, cstr_sv(&exports->string));
                    cstr full = combine(cstr_sv(&base), cstr_sv(&fragment));
                    ret = normalize(cstr_sv(&full));
                    c_drop(cstr, &fragment, &base, &full);
                }
                break;
            }
            case MATCH_PATTERN: {
                isize star = csview_find_sv(p, c_sv("*"));
                if (star == c_NPOS) break;
                csview leading = csview_slice(p, 0, star);
                csview trailing = csview_slice(p, star + 1, p.size);
                csview candidates[3] = { can_try_ts ? target : c_sv(""), s, can_try_ts ? c_sv("") : target };
                for (u32 i = 0; i < 3; i++) {
                    csview c = candidates[i];
                    if (!c.size || c.size < leading.size + trailing.size) continue;
                    if (!starts_with(c, leading) || !ends_with(c, trailing)) continue;
                    ret = replace_first_star(package_name, csview_slice(c, leading.size, c.size - trailing.size));
                    break;
                }
                break;
            }
        }
        c_drop(cstr, &pattern, &swapped);
        return ret;
    }
    if (exports->tag == JSON_ARRAY) {
        for (isize i = 0; i < exports->count; i++) {
            cstr found = from_exports_or_imports(me, target, package_dir, package_name, &exports->items[i], conditions, mode, prefer_ts);
            if (cstr_size(&found)) return found;
            cstr_drop(&found);
        }
        return cstr_null;
    }
    if (exports->tag == JSON_OBJECT) {
        for (isize i = 0; i < exports->count; i++) {
            if (!condition_applies(conditions, cstr_sv(&exports->members[i].key))) continue;
            cstr found = from_exports_or_imports(
                me, target, package_dir, package_name, &exports->members[i].value, conditions, mode, prefer_ts
            );
            if (cstr_size(&found)) return found;
            cstr_drop(&found);
        }
    }
    return cstr_null;
}

// tsc's tryGetModuleNameFromExports
static cstr from_exports(
    Generator* me, csview target, csview package_dir, csview package_name, JsonValue const* exports,
    Conditions const* conditions
) {
    bool sub_paths = exports->tag == JSON_OBJECT;
    for (isize i = 0; sub_paths && i < exports->count; i++) {
        sub_paths = starts_with(cstr_sv(&exports->members[i].key), c_sv("."));
    }
    if (sub_paths) {
        for (isize i = 0; i < exports->count; i++) {
            csview key = cstr_sv(&exports->members[i].key);
            cstr joined = combine(package_name, key);
            cstr sub_name = normalize(cstr_sv(&joined));
            cstr found = from_exports_or_imports(
                me, target, package_dir, cstr_sv(&sub_name), &exports->members[i].value, conditions, matching_mode_of(key), false
            );
            c_drop(cstr, &joined, &sub_name);
            if (cstr_size(&found)) return found;
            cstr_drop(&found);
        }
        return cstr_null;
    }
    return from_exports_or_imports(me, target, package_dir, package_name, exports, conditions, MATCH_EXACT, false);
}

// tsc's tryGetModuleNameFromPackageJsonImports: a `#name` of the importing file's package
static cstr from_package_json_imports(Generator* me, csview target, SpecifierMode mode, bool prefer_ts) {
    if (!resolves_package_json_imports(my options)) return cstr_null;
    csview dir = nearest_package_json_dir(me, my source_dir);
    if (!dir.size) return cstr_null;
    JsonValue const* package = module_resolver_package_json(my resolver, dir);
    JsonValue const* imports = package ? json_get_tagged(package, "imports", JSON_OBJECT) : nullptr;
    if (!imports) return cstr_null;
    Conditions conditions = conditions_for(my options, mode);
    for (isize i = 0; i < imports->count; i++) {
        csview key = cstr_sv(&imports->members[i].key);
        if (!starts_with(key, c_sv("#")) || csview_equals(key, "#") || starts_with(key, c_sv("#/"))) continue;
        cstr found = from_exports_or_imports(
            me, target, dir, key, &imports->members[i].value, &conditions, matching_mode_of(key), prefer_ts
        );
        if (cstr_size(&found)) return found;
        cstr_drop(&found);
    }
    return cstr_null;
}

// ---- `paths`, `typesVersions`, `rootDirs` ----

typedef struct PathEntry {
    csview key;
    csview patterns[16];
    u32 pattern_count;
} PathEntry;

typedef struct PathEntries {
    PathEntry* items;
    u32 len;
} PathEntries;

static PathEntries paths_of_options(CompilerOptions const* options) {
    PathEntries ret = { .items = calloc((size_t)options->path_count + 1, sizeof(PathEntry)) };
    for (isize i = 0; i < options->path_count; i++) {
        PathEntry* e = &ret.items[ret.len++];
        e->key = cstr_sv(&options->paths[i].pattern);
        for (isize j = 0; j < options->paths[i].substitutions.count && e->pattern_count < c_arraylen(e->patterns); j++) {
            e->patterns[e->pattern_count++] = cstr_sv(&options->paths[i].substitutions.items[j]);
        }
    }
    return ret;
}

static PathEntries paths_of_json(JsonValue const* paths) {
    PathEntries ret = { .items = calloc((size_t)paths->count + 1, sizeof(PathEntry)) };
    for (isize i = 0; i < paths->count; i++) {
        PathEntry* e = &ret.items[ret.len++];
        e->key = cstr_sv(&paths->members[i].key);
        JsonValue const* value = &paths->members[i].value;
        if (value->tag != JSON_ARRAY) continue;
        for (isize j = 0; j < value->count && e->pattern_count < c_arraylen(e->patterns); j++) {
            if (value->items[j].tag == JSON_STRING) e->patterns[e->pattern_count++] = cstr_sv(&value->items[j].string);
        }
    }
    return ret;
}

// tsc's tryGetModuleNameFromPaths: relative_to_base (the target relative to base_dir) through a mapping
static cstr from_paths(Generator* me, csview relative_to_base, PathEntries const* paths, Endings const* endings, csview base_dir) {
    // (tsc's candidates: the target with each allowed ending, and as is when a pattern has an extension; validateEnding
    // asks the filesystem whether the minimal ending is really one)
    cstr values[5];
    Ending kinds[5];
    u32 count = 0;
    for (u32 i = 0; i < endings->len; i++) {
        Endings one = { .items = { endings->items[i] }, .len = 1 };
        kinds[count] = endings->items[i];
        values[count++] = process_ending(relative_to_base, &one, my options, nullptr);
    }
    Endings minimal = { .items = { ENDING_MINIMAL }, .len = 1 };
    cstr validated = cstr_null;
    bool validated_done = false;
    cstr ret = cstr_null;
    for (u32 k = 0; k < paths->len && !cstr_size(&ret); k++) {
        PathEntry const* e = &paths->items[k];
        for (u32 j = 0; j < e->pattern_count && !cstr_size(&ret); j++) {
            cstr normalized = normalize(e->patterns[j]);
            cstr absolute = absolute_path_of(base_dir, cstr_sv(&normalized));
            cstr pattern = relative_path_of(base_dir, cstr_sv(&absolute));
            csview p = cstr_sv(&pattern);
            u32 candidate_count = count;
            bool with_as_is = extension_of(p) != nullptr;
            isize star = csview_find_sv(p, c_sv("*"));
            for (u32 c = 0; c < candidate_count + with_as_is && !cstr_size(&ret); c++) {
                csview value = c < candidate_count ? cstr_sv(&values[c]) : relative_to_base;
                bool is_minimal = c < candidate_count && kinds[c] == ENDING_MINIMAL;
                bool matches;
                csview matched = c_sv("");
                if (star != c_NPOS) {
                    csview prefix = csview_slice(p, 0, star), suffix = csview_slice(p, star + 1, p.size);
                    matches = value.size >= prefix.size + suffix.size && starts_with(value, prefix) && ends_with(value, suffix);
                    if (matches) matched = csview_slice(value, prefix.size, value.size - suffix.size);
                } else {
                    matches = csview_equals_sv(p, value);
                }
                if (!matches) continue;
                if (is_minimal) {
                    if (!validated_done) {
                        validated = process_ending(relative_to_base, &minimal, my options, my resolver);
                        validated_done = true;
                    }
                    if (!csview_equals_sv(value, cstr_sv(&validated))) continue;
                }
                if (star != c_NPOS) {
                    if (!path_is_relative(matched)) ret = replace_first_star(e->key, matched);
                } else {
                    ret = cstr_from_sv(e->key);
                }
            }
            c_drop(cstr, &normalized, &absolute, &pattern);
        }
    }
    for (u32 i = 0; i < count; i++) cstr_drop(&values[i]);
    cstr_drop(&validated);
    return ret;
}

// tsc's getPathsRelativeToRootDirs
static StrList relative_to_root_dirs(Generator* me, csview path) {
    StrList ret = {0};
    StrList const* root_dirs = &my options->root_dirs;
    for (isize i = 0; i < root_dirs->count; i++) {
        cstr root = module_resolver_option_path(my resolver, cstr_sv(&root_dirs->items[i]));
        cstr relative = relative_path_of(cstr_sv(&root), path);
        if (!starts_with(cstr_sv(&relative), c_sv(".."))) str_list_push(&ret, cstr_sv(&relative));
        c_drop(cstr, &root, &relative);
    }
    return ret;
}

// tsc's tryGetModuleNameFromRootDirs
static cstr from_root_dirs(Generator* me, csview target, Endings const* endings) {
    StrList targets = relative_to_root_dirs(me, target);
    StrList sources = relative_to_root_dirs(me, my source_dir);
    cstr shortest = cstr_null;
    for (isize i = 0; i < sources.count; i++) {
        for (isize j = 0; j < targets.count; j++) {
            cstr relative = non_module_name(relative_from(cstr_sv(&sources.items[i]), cstr_sv(&targets.items[j])));
            if (!cstr_size(&shortest) || separators(cstr_sv(&relative)) < separators(cstr_sv(&shortest))) {
                cstr_take(&shortest, relative);
            } else {
                cstr_drop(&relative);
            }
        }
    }
    str_list_drop(&targets);
    str_list_drop(&sources);
    if (!cstr_size(&shortest)) return shortest;
    cstr ret = process_ending(cstr_sv(&shortest), endings, my options, nullptr);
    cstr_drop(&shortest);
    return ret;
}

// tsc's getLocalModuleSpecifier with the `project-relative` preference. paths_only: only a `paths` mapping will do
// (empty otherwise)
static cstr local_specifier(Generator* me, csview target, SpecifierMode mode, bool paths_only) {
    CompilerOptions const* o = my options;
    bool has_paths = o->path_count > 0;
    if (paths_only && !has_paths) return cstr_null;
    Endings endings = allowed_endings(me, mode);
    cstr relative = o->root_dirs.count ? from_root_dirs(me, target, &endings) : cstr_null;
    if (!cstr_size(&relative)) {
        cstr_drop(&relative);
        cstr path = non_module_name(relative_path_of(my source_dir, target));
        relative = process_ending(cstr_sv(&path), &endings, o, nullptr);
        cstr_drop(&path);
    }
    if (!has_paths && !resolves_package_json_imports(o)) {
        if (paths_only) cstr_drop(&relative);
        return relative;
    }
    cstr base_dir = has_paths && cstr_size(&o->paths_base) ? module_resolver_option_path(my resolver, cstr_sv(&o->paths_base)) :
        cstr_from_sv(module_resolver_cwd(my resolver));
    cstr relative_to_base = relative_path_of(cstr_sv(&base_dir), target);
    cstr from_imports = paths_only ? cstr_null : from_package_json_imports(me, target, mode, ending_priority(&endings, ENDING_TS) > -1 &&
        ending_priority(&endings, ENDING_TS) < ending_priority(&endings, ENDING_JS));
    cstr from_mapping = cstr_null;
    if ((paths_only || !cstr_size(&from_imports)) && has_paths) {
        PathEntries entries = paths_of_options(o);
        from_mapping = from_paths(me, cstr_sv(&relative_to_base), &entries, &endings, cstr_sv(&base_dir));
        free(entries.items);
    }
    cstr ret = cstr_null;
    if (paths_only) {
        ret = from_mapping;
        from_mapping = cstr_null;
        cstr_drop(&relative);
        goto done;
    }
    cstr non_relative = cstr_size(&from_imports) ? cstr_clone(from_imports) : cstr_clone(from_mapping);
    if (!cstr_size(&non_relative) || path_is_relative(cstr_sv(&non_relative))) {
        // (`project-relative` only prefers a non-relative name that isn't relative)
        cstr_drop(&non_relative);
        ret = relative;
        goto done;
    }
    // ExternalNonRelative: the non-relative name when the import crosses the project's directory or a package.json
    csview project_dir = my request->project_dir.size ? my request->project_dir : module_resolver_cwd(my resolver);
    bool source_internal = starts_with(my source_dir, project_dir);
    bool target_internal = starts_with(target, project_dir);
    bool crosses = source_internal != target_internal;
    if (!crosses) {
        csview target_package = nearest_package_json_dir(me, parent_dir(target));
        csview source_package = nearest_package_json_dir(me, my source_dir);
        crosses = !csview_equals_sv(target_package, source_package);
    }
    if (crosses) {
        ret = non_relative;
        cstr_drop(&relative);
    } else {
        ret = relative;
        cstr_drop(&non_relative);
    }
done:
    c_drop(cstr, &base_dir, &relative_to_base, &from_imports, &from_mapping);
    return ret;
}

// ---- node_modules ----

// tsc's getNodeModulePathParts: indices of the first `/node_modules/`, the package name after it, the root of the
// package the file is in (after the last node_modules) and the file name
typedef struct NodeModulePathParts {
    isize top_level_node_modules;
    isize top_level_package_name;
    isize package_root;
    isize file_name;
} NodeModulePathParts;

static bool node_module_path_parts(csview path, NodeModulePathParts* out) {
    enum { BEFORE_NODE_MODULES, NODE_MODULES, SCOPE, PACKAGE_CONTENT } state = BEFORE_NODE_MODULES;
    NodeModulePathParts parts = {0};
    isize part_start = 0, part_end = 0;
    csview marker = c_sv("/node_modules/");
    while (part_end >= 0) {
        part_start = part_end;
        isize next = part_start + 1 <= path.size ? csview_find_sv(csview_slice(path, part_start + 1, path.size), c_sv("/")) : c_NPOS;
        part_end = next == c_NPOS ? -1 : part_start + 1 + next;
        bool at_marker = starts_with(csview_slice(path, part_start, path.size), marker);
        switch (state) {
            case BEFORE_NODE_MODULES:
                if (at_marker) {
                    parts.top_level_node_modules = part_start;
                    parts.top_level_package_name = part_end;
                    state = NODE_MODULES;
                }
                break;
            case NODE_MODULES:
            case SCOPE:
                if (state == NODE_MODULES && part_start + 1 < path.size && path.buf[part_start + 1] == '@') {
                    state = SCOPE;
                } else {
                    parts.package_root = part_end;
                    state = PACKAGE_CONTENT;
                }
                break;
            case PACKAGE_CONTENT:
                state = at_marker ? NODE_MODULES : PACKAGE_CONTENT;
                break;
        }
    }
    parts.file_name = part_start;
    *out = parts;
    return state > NODE_MODULES;
}

// tsc's getPackageNameFromTypesPackageName
static cstr package_name_from_types(csview name) {
    if (!starts_with(name, c_sv("@types/"))) return cstr_from_sv(name);
    csview rest = csview_slice(name, 7, name.size);
    isize sep = csview_find_sv(rest, c_sv("__"));
    if (sep == c_NPOS) return cstr_from_sv(rest);
    return cstr_d("@", csview_slice(rest, 0, sep), "/", csview_slice(rest, sep + 2, rest.size));
}

typedef struct DirectoryResult {
    cstr module_file; // moduleFileToTry
    bool package_root; // packageRootPath: the directory's name names the file
    bool blocked_by_exports;
    bool verbatim_from_exports;
} DirectoryResult;

// tsc's matchPatternOrExact over the keys of a `typesVersions` mapping
static bool matches_any_key(JsonValue const* paths, csview candidate) {
    for (isize i = 0; i < paths->count; i++) {
        csview key = cstr_sv(&paths->members[i].key);
        isize star = csview_find_sv(key, c_sv("*"));
        if (star == c_NPOS) {
            if (csview_equals_sv(key, candidate)) return true;
            continue;
        }
        if (csview_find_sv(csview_slice(key, star + 1, key.size), c_sv("*")) != c_NPOS) continue; // (not a pattern)
        csview prefix = csview_slice(key, 0, star), suffix = csview_slice(key, star + 1, key.size);
        if (candidate.size >= prefix.size + suffix.size && starts_with(candidate, prefix) && ends_with(candidate, suffix)) return true;
    }
    return false;
}

// JavaScript's String.prototype.substring: indices clamped to the string, swapped when reversed (tsc's paths of a
// file directly in a node_modules have a package root index of -1, which substring reads as 0)
static csview js_substring(csview s, isize start, isize end) {
    start = start < 0 ? 0 : start > s.size ? s.size : start;
    end = end < 0 ? 0 : end > s.size ? s.size : end;
    return start <= end ? csview_slice(s, start, end) : csview_slice(s, end, start);
}

// tryGetModuleNameAsNodeModule's tryDirectoryWithPackageJson
static DirectoryResult directory_with_package_json(
    Generator* me, csview path, NodeModulePathParts const* parts, isize root_index, Endings const* endings, SpecifierMode mode
) {
    csview root = js_substring(path, 0, root_index);
    DirectoryResult ret = { .module_file = cstr_from_sv(path) };
    JsonValue const* package = module_resolver_package_json(my resolver, root);
    if (!package) {
        // No package.json: an index file is still found by the package's name
        csview file = js_substring(path, parts->package_root + 1, path.size);
        if (csview_equals(file, "index.d.ts") || csview_equals(file, "index.js") || csview_equals(file, "index.ts") ||
            csview_equals(file, "index.tsx")) {
            ret.package_root = true;
        }
        return ret;
    }
    SpecifierMode import_mode = mode ? mode : my request->importing_default_mode;
    JsonValue const* exports = json_get(package, "exports");
    if (resolves_package_json_exports(my options)) {
        // (tsgo's exports conditions follow the target's own format by its extension, where tsc took the importing
        // file's mode)
        if (ends_with(path, c_sv(".cjs")) || ends_with(path, c_sv(".cts"))) import_mode = SPECIFIER_MODE_COMMONJS;
        else if (ends_with(path, c_sv(".mjs")) || ends_with(path, c_sv(".mts"))) import_mode = SPECIFIER_MODE_ESM;
        cstr name = package_name_from_types(js_substring(root, parts->top_level_package_name + 1, root.size));
        Conditions conditions = conditions_for(my options, import_mode);
        cstr found = exports && exports->tag != JSON_NULL ? from_exports(me, path, root, cstr_sv(&name), exports, &conditions) :
            cstr_null;
        cstr_drop(&name);
        if (cstr_size(&found)) {
            cstr_take(&ret.module_file, found);
            ret.verbatim_from_exports = true;
            return ret;
        }
        cstr_drop(&found);
        if (exports && exports->tag != JSON_NULL) {
            ret.blocked_by_exports = true;
            return ret;
        }
    }
    JsonValue const* version_paths = json_get(package, "typesVersions") ? module_resolver_types_versions(package) : nullptr;
    bool maybe_blocked = false;
    if (version_paths) {
        csview sub_module = csview_slice(path, root.size + 1, path.size);
        PathEntries entries = paths_of_json(version_paths);
        cstr found = from_paths(me, sub_module, &entries, endings, root);
        free(entries.items);
        if (!cstr_size(&found)) {
            maybe_blocked = true;
        } else {
            cstr_take(&ret.module_file, combine(root, cstr_sv(&found)));
        }
        cstr_drop(&found);
    }
    // The main file is named by the package's name
    csview main = c_sv("");
    bool main_is_string = true;
    static char const* const fields[] = { "typings", "types", "main" };
    for (u32 i = 0; i < c_arraylen(fields); i++) {
        JsonValue const* field = json_get(package, fields[i]);
        if (!field || field->tag == JSON_NULL || (field->tag == JSON_STRING && !cstr_size(&field->string)) ||
            (field->tag == JSON_BOOL && !field->boolean) || (field->tag == JSON_NUMBER && field->number == 0)) {
            continue;
        }
        main_is_string = field->tag == JSON_STRING;
        main = main_is_string ? cstr_sv(&field->string) : c_sv("");
        break;
    }
    if (main_is_string && !main.size) main = c_sv("index.js");
    if (main_is_string && !(maybe_blocked && matches_any_key(version_paths, main))) {
        cstr main_file = absolute_path_of(root, main);
        csview module_file = cstr_sv(&ret.module_file);
        csview main_stem = remove_extension(cstr_sv(&main_file));
        if (csview_equals_sv(main_stem, remove_extension(module_file))) {
            ret.package_root = true;
        } else if (!csview_equals(json_get_string(package, "type"), "module") && !extension_needs_writing(module_file) &&
            starts_with(module_file, cstr_sv(&main_file)) && csview_equals_sv(parent_dir(module_file), cstr_sv(&main_file)) &&
            csview_equals(remove_extension(last_segment(module_file)), "index")
        ) {
            ret.package_root = true;
        }
        cstr_drop(&main_file);
    }
    return ret;
}

// tsc's tryGetModuleNameAsNodeModule: the name of a file in a node_modules as a package name (and subpath), empty if
// the file can't be reached that way from the importing file
static cstr as_node_module(Generator* me, csview path, SpecifierMode mode, bool is_redirect) {
    NodeModulePathParts parts;
    if (!node_module_path_parts(path, &parts)) return cstr_null;
    // Only through a node_modules the importing file sees, checked before the package.json `exports` can give a bare
    // specifier (tsgo 7.1.0-beta, #64599)
    if (!starts_with(my source_dir, js_substring(path, 0, parts.top_level_node_modules))) return cstr_null;
    Endings endings = allowed_endings(me, SPECIFIER_MODE_NONE);
    cstr specifier = cstr_from_sv(path);
    isize root_index = parts.package_root;
    cstr module_file = cstr_null;
    bool package_root = false;
    while (true) {
        DirectoryResult r = directory_with_package_json(me, path, &parts, root_index, &endings, mode);
        if (r.blocked_by_exports) {
            c_drop(cstr, &r.module_file, &specifier, &module_file);
            return cstr_null;
        }
        if (r.verbatim_from_exports) {
            c_drop(cstr, &specifier, &module_file);
            return r.module_file;
        }
        if (r.package_root) {
            cstr_take(&specifier, cstr_from_sv(js_substring(path, 0, root_index)));
            cstr_drop(&r.module_file);
            package_root = true;
            break;
        }
        if (!cstr_size(&module_file)) cstr_take(&module_file, r.module_file);
        else cstr_drop(&r.module_file);
        isize next = root_index + 1 <= path.size ? csview_find_sv(csview_slice(path, root_index + 1, path.size), c_sv("/")) : c_NPOS;
        if (next == c_NPOS) {
            cstr_take(&specifier, process_ending(cstr_sv(&module_file), &endings, my options, my resolver));
            break;
        }
        root_index = root_index + 1 + next;
    }
    cstr_drop(&module_file);
    // (A referenced project's output declaration file is named by its package's name alone: a subpath would be into
    // its outDir, which its own source names better, better-auth's `node_modules/.cache/ts/out`)
    if (is_redirect && !package_root) {
        cstr_drop(&specifier);
        return cstr_null;
    }
    csview s = cstr_sv(&specifier);
    // (Under that node_modules)
    csview top = js_substring(s, 0, parts.top_level_node_modules);
    if (!starts_with(my source_dir, top)) {
        cstr_drop(&specifier);
        return cstr_null;
    }
    csview directory_name = js_substring(s, parts.top_level_package_name + 1, s.size);
    cstr ret = package_name_from_types(directory_name);
    cstr_drop(&specifier);
    return ret;
}

// ---- The paths of the module ----

typedef struct ModulePath {
    cstr path;
    bool in_node_modules;
    bool is_redirect; // A referenced project's output declaration file (or a link to it): sorted first
} ModulePath;

typedef struct ModulePaths {
    ModulePath* items;
    u32 len;
    u32 cap;
} ModulePaths;

static void module_paths_add(ModulePaths* me, csview path, bool is_redirect) {
    for (u32 i = 0; i < my len; i++) {
        if (cstr_equals_sv(&my items[i].path, path)) return;
    }
    if (my len == my cap) {
        my cap = my cap ? my cap * 2 : 4;
        my items = realloc(my items, my cap * sizeof(*my items));
    }
    my items[my len++] = (ModulePath){
        .path = cstr_from_sv(path), .in_node_modules = path_contains_node_modules(path), .is_redirect = is_redirect,
    };
}

static void module_paths_drop(ModulePaths* me) {
    for (u32 i = 0; i < my len; i++) cstr_drop(&my items[i].path);
    free(my items);
}

// tsc's comparePathsByRedirect: redirects first, then fewer directories, then by path
static bool path_sorts_after(ModulePath const* a, ModulePath const* b, u32 b_depth) {
    if (a->is_redirect != b->is_redirect) return b->is_redirect;
    u32 a_depth = separators(cstr_sv(&a->path));
    if (a_depth != b_depth) return a_depth > b_depth;
    // (tsgo's comparePathsByRedirect then compares the paths, case sensitively here, where tsc kept insertion order)
    return strcmp(cstr_str(&a->path), cstr_str(&b->path)) > 0;
}

// tsc's getAllModulePathsWorker: every name of the target (through the links to a directory it's in, then itself),
// ordered by how close they are to the importing file
static ModulePaths all_module_paths(Generator* me) {
    SpecifierRequest const* r = my request;
    ModulePaths found = {0};
    csview target = r->target;
    // (A referenced project's source is named by its output declaration file first, then itself: tsgo's
    // GetEachFileNameOfModule with the referenceRedirect)
    csview targets[2] = { r->redirect, target };
    u32 first_target = r->redirect.size ? 0 : 1;
    bool filter_ignored = false;
    for (u32 t = first_target; t < 2; t++) filter_ignored |= !contains_ignored_path(targets[t]);
    // forEachFileNameOfModule with preferSymlinks
    if (r->symlinks) {
        for (csview dir = parent_dir(target);;) {
            cstr key = dir.size > 1 ? cstr_d(dir, "/") : cstr_from("/");
            StrList links = links_of(r->symlinks, cstr_sv(&key));
            cstr_drop(&key);
            if (links.count) {
                // (A package doesn't import itself through its own link)
                if (starts_with_directory(r->importing, dir)) {
                    str_list_drop(&links);
                    break;
                }
                for (u32 t = first_target; t < 2; t++) {
                    if (!starts_with_directory(targets[t], dir)) continue;
                    cstr relative = relative_path_of(dir, targets[t]);
                    for (isize i = 0; i < links.count; i++) {
                        cstr option = absolute_path_of(cstr_sv(&links.items[i]), cstr_sv(&relative));
                        module_paths_add(&found, cstr_sv(&option), t == 0);
                        cstr_drop(&option);
                        filter_ignored = true;
                    }
                    cstr_drop(&relative);
                }
            }
            str_list_drop(&links);
            csview parent = parent_dir(dir);
            if (parent.size == dir.size) break;
            dir = parent;
        }
    }
    for (u32 t = first_target; t < 2; t++) {
        if (!(filter_ignored && contains_ignored_path(targets[t]))) module_paths_add(&found, targets[t], t == 0);
    }

    // Sorted by the closest directory both are in, then by how deep they are (stable)
    ModulePaths sorted = { .items = malloc(sizeof(ModulePath) * (found.len + 1)), .cap = found.len + 1 };
    bool* taken = calloc(found.len + 1, 1);
    csview dir = my source_dir;
    u32 left = found.len;
    while (left) {
        cstr start = dir.size > 1 ? cstr_d(dir, "/") : cstr_from("/");
        u32 first = sorted.len;
        for (u32 i = 0; i < found.len; i++) {
            if (taken[i] || !starts_with(cstr_sv(&found.items[i].path), cstr_sv(&start))) continue;
            taken[i] = true;
            left--;
            // (insertion keeps it stable)
            u32 at = sorted.len;
            u32 depth = separators(cstr_sv(&found.items[i].path));
            while (at > first && path_sorts_after(&sorted.items[at - 1], &found.items[i], depth)) {
                sorted.items[at] = sorted.items[at - 1];
                at--;
            }
            sorted.items[at] = found.items[i];
            sorted.len++;
        }
        cstr_drop(&start);
        csview parent = parent_dir(dir);
        if (parent.size == dir.size) break;
        dir = parent;
    }
    u32 first = sorted.len;
    for (u32 i = 0; i < found.len; i++) {
        if (taken[i]) continue;
        u32 at = sorted.len;
        u32 depth = separators(cstr_sv(&found.items[i].path));
        while (at > first && path_sorts_after(&sorted.items[at - 1], &found.items[i], depth)) {
            sorted.items[at] = sorted.items[at - 1];
            at--;
        }
        sorted.items[at] = found.items[i];
        sorted.len++;
    }
    free(taken);
    free(found.items);
    return sorted;
}

// ---- computeModuleSpecifiers ----

cstr module_specifier_generate(SpecifierRequest const* request) {
    CompilerOptions const* o = request->options;
    Generator gen = {
        .request = request,
        .options = o,
        .resolver = request->resolver,
        .source_dir = parent_dir(request->importing),
        .node_next = o->module_resolution == MODULE_RESOLUTION_NODE16 || o->module_resolution == MODULE_RESOLUTION_NODENEXT,
    };
    SpecifierMode emit_mode = request->ending_mode ? request->ending_mode :
        request->override_mode ? request->override_mode : request->importing_default_mode;
    gen.prefer_js = emit_mode == SPECIFIER_MODE_ESM;
    gen.file_preferred = ending_preference(&gen, request->importing_default_mode);
    ModulePaths paths = all_module_paths(&gen);
    // (The import resolved to the realpath, which only matches that path)
    if (request->existing.size) {
        for (u32 i = 0; i < paths.len; i++) {
            if (!csview_equals_sv(cstr_sv(&paths.items[i].path), request->target)) continue;
            module_paths_drop(&paths);
            return cstr_from_sv(request->existing);
        }
    }
    bool target_in_node_modules = false;
    for (u32 i = 0; i < paths.len; i++) target_in_node_modules |= paths.items[i].in_node_modules;
    // A `paths` mapping, else one of a referenced project's output declaration file (a redirect), else a package name
    // through node_modules, else relative
    cstr node_modules = cstr_null, mapped = cstr_null, redirect_mapped = cstr_null, relative = cstr_null;
    SpecifierMode local_mode = request->override_mode ? request->override_mode : request->importing_format;
    for (u32 i = 0; i < paths.len; i++) {
        csview path = cstr_sv(&paths.items[i].path);
        bool is_redirect = paths.items[i].is_redirect;
        cstr specifier = paths.items[i].in_node_modules ? as_node_module(&gen, path, request->override_mode, is_redirect) : cstr_null;
        if (cstr_size(&specifier) && !cstr_size(&node_modules)) cstr_take(&node_modules, cstr_clone(specifier));
        // (A redirect's is its package's bare name: none will be better, not even an earlier `paths` mapping)
        if (cstr_size(&specifier) && is_redirect) {
            cstr_drop(&specifier);
            module_paths_drop(&paths);
            c_drop(cstr, &mapped, &redirect_mapped, &relative);
            return node_modules;
        }
        cstr local = local_specifier(&gen, path, local_mode, is_redirect || cstr_size(&specifier) > 0);
        cstr_drop(&specifier);
        if (!cstr_size(&local)) {
            cstr_drop(&local);
            continue;
        }
        csview l = cstr_sv(&local);
        bool bare = !(l.size && l.buf[0] == '/') && !path_is_relative(l);
        if (is_redirect) {
            if (!cstr_size(&redirect_mapped)) cstr_take(&redirect_mapped, cstr_clone(local));
        } else if (bare && !path_contains_node_modules(l)) {
            if (!cstr_size(&mapped)) cstr_take(&mapped, cstr_clone(local));
        } else if (bare || !target_in_node_modules || paths.items[i].in_node_modules) {
            // (A relative path that doesn't go through a node_modules when another one does is likely into a sibling
            // package of a monorepo: not portable either, the one through node_modules is what the error names)
            if (!cstr_size(&relative)) cstr_take(&relative, cstr_clone(local));
        }
        cstr_drop(&local);
    }
    module_paths_drop(&paths);
    cstr ret = cstr_size(&mapped) ? cstr_clone(mapped) : cstr_size(&redirect_mapped) ? cstr_clone(redirect_mapped) :
        cstr_size(&node_modules) ? cstr_clone(node_modules) : cstr_clone(relative);
    c_drop(cstr, &node_modules, &mapped, &redirect_mapped, &relative);
    return ret;
}
