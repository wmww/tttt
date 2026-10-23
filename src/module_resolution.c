// pthread_rwlock_t, which strict -std=c2x hides
#define _XOPEN_SOURCE 700
#include "module_resolution.h"
#include "json.h"
#include "content_mapper.h"
#include "string_helpers.h"
#include <pthread.h>
#include <stdatomic.h>


enum {
    EXT_TS = 1 << 0,
    EXT_JS = 1 << 1,
    EXT_DTS = 1 << 2,
    EXT_JSON = 1 << 3,
};

// tsc's SearchResult: a lookup either has nothing to say (the search goes on), found something, or decided that the
// name resolves to nothing (a `paths` pattern or an `exports` map matched and led nowhere)
typedef enum Search {
    SEARCH_CONTINUE,
    SEARCH_FOUND,
    SEARCH_STOP,
} Search;

typedef struct Found {
    cstr path;
    // The package whose `exports` or `imports` mapped to it (tsc's getPackageId of the scope): its package id, outside
    // node_modules too (a package importing itself by name)
    cstr package_dir;
    ResolvedKind kind;
    bool used_ts_extension;
    bool ts_extension_from_star; // used_ts_extension of a package.json target's `*` (load_package_json_field())
    bool arbitrary_extension;
} Found;

typedef struct PackageJson {
    bool exists;
    JsonValue json;
    // The package id's `+peer@version` part (append_peer_dependencies()), computed by the first package_id_of() of the
    // package (under its resolver shard's lock once computed: several threads may get there)
    bool peer_suffix_computed;
    cstr peer_suffix;
} PackageJson;

static void package_json_drop(PackageJson* me) {
    json_drop(&my json);
    cstr_drop(&my peer_suffix);
}

// Boxed: a PackageJson never moves once read, whatever other threads add to the map
typedef PackageJson* PackageJsonBox;
static void package_json_box_drop(PackageJsonBox* me) {
    package_json_drop(*me);
    free(*me);
}

#define g_pro_key SvStr, svstr
#define g_class_val PackageJsonBox, package_json_box
#define g_no_clone
#include <cleat/generic/hmap.h>

// (Results live in their shard's boxes: two keys may share one, and pointers to them stay valid until forgotten)
typedef ResolvedModule const* ResolvedModulePtr;
#define g_pro_key SvStr, svstr
#define g_val ResolvedModulePtr, resolved_module_ptr
#include <cleat/generic/hmap.h>

// Sharded by key hash: the load pool's workers all resolve at once. (At 64 shards a sixteenth of vscode's load-phase
// thread blocking was here: ~1% of 350k lock takes contended, each wait ~90 us on a loaded machine, the holder preempted
// or growing its map; 1024 take a seventh of the contended ones. ~130 KB a resolver)
enum { RESOLVER_SHARDS = 1024 };

typedef struct ResolverShard {
    // (A reader-writer lock: nearly every use is a lookup that finds, and a thread preempted holding it for one, on a
    // loaded machine, then blocks no other lookup)
    pthread_rwlock_t lock;
    HMapPackageJsonBox package_jsons; // By directory
    // module_resolver_resolve()'s results by containing directory, mode and specifier (resolution_key()): the files of
    // a directory import the same packages, each a walk up the node_modules directories
    HMapResolvedModulePtr resolutions;
    ResolvedModule** boxes; // What resolutions point to, made in this shard
    isize box_count, box_cap;
} ResolverShard;

typedef struct ModuleResolver {
    Fs const* fs;
    CompilerOptions const* options;
    cstr cwd; // Once cwd_known
    _Atomic bool cwd_known;
    bool cwd_normalized; // An absolute path_is_normalized() one: containing files are joined to it as they are
    RealpathFn realpath;
    void* realpath_ctx;
    pthread_mutex_t cwd_lock;
    // Resolutions may run on several threads at once (a program's load pool): the caches are shared, sharded by key
    // under a lock each (the filesystem probing between a miss and the insert isn't; two threads may both do a lookup,
    // and agree). One lock was the loader's main wait with a dozen workers resolving
    ResolverShard shards[RESOLVER_SHARDS];
} ModuleResolver;

// One resolution in progress
typedef struct State {
    ModuleResolver* resolver;
    ModuleResolutionKind kind;
    bool esm_mode; // node16/nodenext resolving an `import`: no extension or index probing
    bool use_exports, use_imports, use_self_name;
    bool in_type_reference;
    const char* conditions[24];
    isize condition_count;
    csview containing_dir;
    bool json_without_option;
    bool resolved_package_directory; // tsc's state.resolvedPackageDirectory: a package.json at a package's root
    // tsc's TS2209/TS2210: an exports/imports target was mapped back to an input file with the project root guessed
    cstr ambiguous_root_entry;
    cstr ambiguous_root_package_json;
    bool ambiguous_root_in_imports;
} State;

void resolved_module_drop(ResolvedModule* me) {
    cstr_drop(&my path);
    cstr_drop(&my suggested_extension);
    cstr_drop(&my package_id);
    cstr_drop(&my package_name);
    cstr_drop(&my original_path);
    cstr_drop(&my alternate_result);
    cstr_drop(&my ambiguous_root_entry);
    cstr_drop(&my ambiguous_root_package_json);
    *me = (ResolvedModule){0};
}

ResolvedModule resolved_module_clone(ResolvedModule const* me) {
    ResolvedModule ret = *me;
    ret.path = cstr_clone(my path);
    ret.suggested_extension = cstr_clone(my suggested_extension);
    ret.package_id = cstr_clone(my package_id);
    ret.package_name = cstr_clone(my package_name);
    ret.original_path = cstr_clone(my original_path);
    ret.alternate_result = cstr_clone(my alternate_result);
    ret.ambiguous_root_entry = cstr_clone(my ambiguous_root_entry);
    ret.ambiguous_root_package_json = cstr_clone(my ambiguous_root_package_json);
    return ret;
}

ModuleResolver* module_resolver_new(Fs const* fs, CompilerOptions const* options) {
    ModuleResolver* ret = c_new(ModuleResolver, { .fs = fs, .options = options });
    pthread_mutex_init(&ret->cwd_lock, nullptr);
    for (int i = 0; i < RESOLVER_SHARDS; i++) pthread_rwlock_init(&ret->shards[i].lock, nullptr);
    return ret;
}

static ResolverShard* resolver_shard(ModuleResolver* me, csview key) {
    uint32_t hash = 2166136261u;
    for (isize i = 0; i < key.size; i++) hash = (hash ^ (uint8_t)key.buf[i]) * 16777619u;
    return &my shards[hash % RESOLVER_SHARDS];
}

void module_resolver_set_realpath(ModuleResolver* me, RealpathFn realpath, void* ctx) {
    my realpath = realpath;
    my realpath_ctx = ctx;
}

cstr module_resolver_real_path(ModuleResolver* me, csview path) {
    return my realpath ? my realpath(my realpath_ctx, path) : cstr_null;
}

// Follows a resolved file's symlinks (tsc's getOriginalAndResolvedFileName)
static void follow_symlinks(ModuleResolver* me, ResolvedModule* resolved) {
    if (!my realpath || my options->preserve_symlinks) return;
    cstr real = module_resolver_real_path(me, cstr_sv(&resolved->path));
    if (cstr_size(&real)) {
        if (!cstr_eq(&real, &resolved->path)) cstr_take(&resolved->original_path, cstr_clone(resolved->path));
        cstr_take(&resolved->path, real);
    } else {
        cstr_drop(&real);
    }
}

// Modules are followed only out of node_modules
static void apply_realpath(ModuleResolver* me, ResolvedModule* resolved) {
    if (resolved->is_external_library) follow_symlinks(me, resolved);
}

void module_resolver_destroy(ModuleResolver* me) {
    if (!me) return;
    cstr_drop(&my cwd);
    module_resolver_forget_resolutions(me);
    for (int i = 0; i < RESOLVER_SHARDS; i++) {
        hmap_package_json_box_drop(&my shards[i].package_jsons);
        pthread_rwlock_destroy(&my shards[i].lock);
    }
    pthread_mutex_destroy(&my cwd_lock);
    free(me);
}

void module_resolver_forget_resolutions(ModuleResolver* me) {
    if (!me) return;
    for (int i = 0; i < RESOLVER_SHARDS; i++) {
        ResolverShard* shard = &my shards[i];
        // (Under the lock: the package.json cache next to them stays in use, program_resolve_names() forgets on a
        // thread of its own while the checkers start)
        pthread_rwlock_wrlock(&shard->lock);
        hmap_resolved_module_ptr_drop(&shard->resolutions);
        shard->resolutions = (HMapResolvedModulePtr){0};
        for (isize j = 0; j < shard->box_count; j++) {
            resolved_module_drop(shard->boxes[j]);
            free(shard->boxes[j]);
        }
        free(shard->boxes);
        shard->boxes = nullptr;
        shard->box_count = shard->box_cap = 0;
        pthread_rwlock_unlock(&shard->lock);
    }
}

// ---- Paths ----

// Appends `path`'s segments to the absolute path in `out` (length `*len`) the way absolute_path_of() does
static void push_segments(char* out, intptr_t* len, csview path) {
    const char* c = path.buf;
    const char* end = path.buf + path.size;
    while (c < end) {
        const char* segment = c;
        while (c < end && *c != '/') c++;
        intptr_t n = c - segment;
        if (c < end) c++;
        if (!n || (n == 1 && segment[0] == '.')) continue;
        if (n == 2 && segment[0] == '.' && segment[1] == '.') {
            intptr_t i = *len;
            while (i > 0 && out[--i] != '/') {}
            *len = i < 1 ? 1 : i;
            continue;
        }
        if (*len > 1) out[(*len)++] = '/';
        memcpy(out + *len, segment, n);
        *len += n;
    }
}

// absolute_path_of(cwd, path) in one buffer (it appends a segment at a time; the resolver makes paths by the thousand)
// dir and rest, both normalized (path_is_normalized()), dir absolute and rest not
static cstr concat_normalized(csview dir, csview rest) {
    return dir.size == 1 ? cstr_d("/", rest) : cstr_d(dir, "/", rest);
}

cstr absolute_path_fast(csview cwd, csview path) {
    bool rooted = path.size && path.buf[0] == '/';
    // (Most paths are normalized already: resolved files, the program's canonical paths)
    if (path_is_normalized(path)) {
        if (rooted) return cstr_from_sv(path);
        if (cwd.size && cwd.buf[0] == '/' && path_is_normalized(cwd)) return concat_normalized(cwd, path);
    }
    cstr ret = cstr_with_size(1 + (rooted ? 0 : cwd.size + 1) + path.size, 0);
    char* out = cstr_data(&ret);
    intptr_t len = 1;
    out[0] = '/';
    if (!rooted) push_segments(out, &len, cwd);
    push_segments(out, &len, path);
    cstr_resize(&ret, len, 0);
    return ret;
}

static cstr join(csview dir, csview rest) {
    if (rest.size && rest.buf[0] == '/') return absolute_path_fast(c_sv("/"), rest); // A rooted name: `import "/foo"`
    if (dir.size && dir.buf[0] == '/' && path_is_normalized(dir) && path_is_normalized(rest)) return concat_normalized(dir, rest);
    cstr ret = absolute_path_fast(c_sv("/"), dir);
    intptr_t len = cstr_size(&ret);
    cstr_resize(&ret, len + 1 + rest.size, 0);
    push_segments(cstr_data(&ret), &len, rest);
    cstr_resize(&ret, len, 0);
    return ret;
}

// Of an option: relative to the tsconfig it came from, else to the current directory
static cstr option_path(ModuleResolver const* me, csview path) {
    // (A drive root is one of the current directory, see path_is_drive_rooted())
    if (path_is_drive_rooted(path)) return absolute_path_of(cstr_sv(&my cwd), path);
    csview base = cstr_size(&my options->config_dir) ? cstr_sv(&my options->config_dir) : cstr_sv(&my cwd);
    cstr absolute_base = absolute_path_of(cstr_sv(&my cwd), base);
    cstr ret = absolute_path_of(cstr_sv(&absolute_base), path);
    cstr_drop(&absolute_base);
    return ret;
}

static bool ends_with(csview s, const char* suffix) {
    return csview_ends_with(s, suffix);
}

static bool sv_starts_with(csview s, csview prefix) {
    return s.size >= prefix.size && !memcmp(s.buf, prefix.buf, (size_t)prefix.size);
}

static bool sv_ends_with(csview s, csview suffix) {
    return s.size >= suffix.size && !memcmp(s.buf + s.size - suffix.size, suffix.buf, (size_t)suffix.size);
}

// Of a normalised absolute path, without a trailing separator ("/" for what is directly in the root)
static csview parent_of(csview path) {
    isize i = path.size;
    while (i > 0 && path.buf[i - 1] == '/') i--;
    while (i > 0 && path.buf[i - 1] != '/') i--;
    while (i > 1 && path.buf[i - 1] == '/') i--;
    return csview_slice(path, 0, i ? i : 1);
}

// dir = its parent, false at the root
static bool go_up(cstr* dir) {
    if (cstr_size(dir) <= 1) return false;
    cstr parent = cstr_from_sv(parent_of(cstr_sv(dir)));
    cstr_take(dir, parent);
    return true;
}

static bool path_contains_node_modules(csview path) {
    return csview_find_sv(path, c_sv("/node_modules/")) != c_NPOS;
}

// ---- Files ----

static bool file_exists(State* me, csview path) {
    return fs_is_reg_file(my resolver->fs, path);
}

static bool directory_exists(State* me, csview path) {
    return fs_is_directory(my resolver->fs, path);
}

// tsc's tryFile(): with `moduleSuffixes`, "a.ts" is looked for as "a<suffix>.ts" for each suffix in order
static bool try_file(State* me, csview path, csview extension, cstr* out) {
    StrList const* suffixes = &my resolver->options->module_suffixes;
    if (!suffixes->count) {
        if (!file_exists(me, path)) return false;
        *out = cstr_from_sv(path);
        return true;
    }
    csview stem = csview_slice(path, 0, path.size - extension.size);
    for (isize i = 0; i < suffixes->count; i++) {
        cstr candidate = cstr_d(stem, suffixes->items[i], extension);
        if (file_exists(me, cstr_sv(&candidate))) {
            *out = candidate;
            return true;
        }
        cstr_drop(&candidate);
    }
    return false;
}

static bool try_extension(State* me, csview stem, const char* extension, ResolvedKind kind, Found* out) {
    cstr path = cstr_d(stem, extension);
    bool found = try_file(me, cstr_sv(&path), csview_from(extension), &out->path);
    cstr_drop(&path);
    if (found) out->kind = kind;
    return found;
}

// tsc's tryAddingExtensions(): the files `stem` + extension could mean, given the extension that was written
static bool try_adding_extensions(State* me, csview stem, int extensions, csview original, Found* out) {
    if (csview_equals(original, ".mjs") || csview_equals(original, ".mts") || csview_equals(original, ".d.mts")) {
        return (extensions & EXT_TS && try_extension(me, stem, ".mts", RESOLVED_TS, out)) ||
            (extensions & EXT_DTS && try_extension(me, stem, ".d.mts", RESOLVED_DTS, out)) ||
            (extensions & EXT_JS && try_extension(me, stem, ".mjs", RESOLVED_JS, out));
    }
    if (csview_equals(original, ".cjs") || csview_equals(original, ".cts") || csview_equals(original, ".d.cts")) {
        return (extensions & EXT_TS && try_extension(me, stem, ".cts", RESOLVED_TS, out)) ||
            (extensions & EXT_DTS && try_extension(me, stem, ".d.cts", RESOLVED_DTS, out)) ||
            (extensions & EXT_JS && try_extension(me, stem, ".cjs", RESOLVED_JS, out));
    }
    if (csview_equals(original, ".json")) {
        return (extensions & EXT_DTS && try_extension(me, stem, ".d.json.ts", RESOLVED_DTS, out)) ||
            (extensions & EXT_JSON && try_extension(me, stem, ".json", RESOLVED_JSON, out));
    }
    if (csview_equals(original, ".tsx") || csview_equals(original, ".jsx")) {
        // "./a.jsx" prefers a.tsx to a.ts
        return (extensions & EXT_TS && (
                try_extension(me, stem, ".tsx", RESOLVED_TS, out) || try_extension(me, stem, ".ts", RESOLVED_TS, out))) ||
            (extensions & EXT_DTS && try_extension(me, stem, ".d.ts", RESOLVED_DTS, out)) ||
            (extensions & EXT_JS && (
                try_extension(me, stem, ".jsx", RESOLVED_JS, out) || try_extension(me, stem, ".js", RESOLVED_JS, out)));
    }
    if (!original.size || csview_equals(original, ".ts") || csview_equals(original, ".d.ts") ||
        csview_equals(original, ".js")
    ) {
        return (extensions & EXT_TS && (
                try_extension(me, stem, ".ts", RESOLVED_TS, out) || try_extension(me, stem, ".tsx", RESOLVED_TS, out))) ||
            (extensions & EXT_DTS && try_extension(me, stem, ".d.ts", RESOLVED_DTS, out)) ||
            (extensions & EXT_JS && (
                try_extension(me, stem, ".js", RESOLVED_JS, out) || try_extension(me, stem, ".jsx", RESOLVED_JS, out)));
    }
    // A content mapper's extension (tsgo's extraExtensions) names the file itself
    if (original.size && content_mapper_has_extension(my resolver->options, original)) {
        cstr extension = cstr_from_sv(original);
        bool found = try_extension(me, stem, cstr_str(&extension), RESOLVED_TS, out);
        cstr_drop(&extension);
        if (found) return true;
    }
    // `import "./style.css"` finds style.d.css.ts
    if (extensions & EXT_DTS) {
        cstr extension = cstr_d(".d", original, ".ts");
        bool found = try_extension(me, stem, cstr_str(&extension), RESOLVED_DTS, out);
        cstr_drop(&extension);
        if (found) out->arbitrary_extension = true;
        return found;
    }
    return false;
}

// The extension of a path as far as resolution cares: ".d.ts" style ones whole, "" for anything that isn't a script
static csview script_extension_of(csview path) {
    static const char* const known[] = {
        ".d.ts", ".d.mts", ".d.cts", ".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs", ".json",
    };
    for (isize i = 0; i < (isize)c_arraylen(known); i++) {
        if (ends_with(path, known[i])) return csview_slice(path, path.size - (isize)strlen(known[i]), path.size);
    }
    return c_sv("");
}

static bool declaration_extension_of(csview extension) {
    return csview_equals(extension, ".d.ts") || csview_equals(extension, ".d.mts") || csview_equals(extension, ".d.cts");
}

static bool extension_is_ts_source(csview extension) {
    return csview_equals(extension, ".ts") || csview_equals(extension, ".tsx") || csview_equals(extension, ".mts") ||
        csview_equals(extension, ".cts");
}

// "./a.js" means a.ts, a.tsx or a.d.ts (and a.js itself); "./a.css" means a.d.css.ts
static bool load_file_no_implicit_extensions(State* me, csview candidate, int extensions, Found* out) {
    csview name = basename_of(candidate);
    isize dot = c_NPOS;
    // (A leading dot counts: "./empty/.js" means empty/.ts, tsgo's loadModuleFromFileNoImplicitExtensions)
    for (isize i = name.size - 1; i >= 0; i--) {
        if (name.buf[i] == '.') {
            dot = i;
            break;
        }
    }
    if (dot == c_NPOS) return false;
    csview extension = script_extension_of(candidate);
    if (!extension.size) extension = content_mapper_extension_of(my resolver->options, name);
    if (!extension.size) extension = csview_slice(name, dot, name.size);
    if (csview_equals(extension, ".json") && !my resolver->options->resolve_json_module) {
        // Found only to tell the caller why not
        cstr path = cstr_null;
        if (extensions & EXT_TS && try_file(me, candidate, extension, &path)) {
            my json_without_option = true;
            cstr_drop(&path);
        }
        extensions &= ~EXT_JSON;
    }
    csview stem = csview_slice(candidate, 0, candidate.size - extension.size);
    if (!try_adding_extensions(me, stem, extensions, extension, out)) return false;
    // (tsc's tryAddingExtensions: resolvedUsingTsExtension, a TypeScript or declaration extension written and a
    // TypeScript file or declaration found: "./a.d.ts" may be a.ts)
    out->used_ts_extension = (extension_is_ts_source(extension) || declaration_extension_of(extension)) &&
        (out->kind == RESOLVED_TS || out->kind == RESOLVED_DTS);
    return true;
}

// tsc's loadModuleFromFile()
static bool load_file(State* me, csview candidate, int extensions, Found* out) {
    if (load_file_no_implicit_extensions(me, candidate, extensions, out)) return true;
    if (my esm_mode) return false;
    return try_adding_extensions(me, candidate, extensions, c_sv(""), out);
}

// ---- package.json ----

static PackageJson const* package_json_of(State* me, csview directory) {
    ModuleResolver* resolver = my resolver;
    ResolverShard* shard = resolver_shard(resolver, directory);
    pthread_rwlock_rdlock(&shard->lock);
    auto cached = hmap_package_json_box_get(&shard->package_jsons, directory);
    PackageJson const* known = cached ? cached->second : nullptr;
    pthread_rwlock_unlock(&shard->lock);
    if (known) return known->exists ? known : nullptr;
    PackageJson package = {0};
    cstr path = cstr_d(directory, "/package.json");
    if (fs_is_reg_file(resolver->fs, cstr_sv(&path))) {
        cstr contents = cstr_null;
        bool readable = true;
        try (fs_read_file(resolver->fs, cstr_sv(&path), &contents)) if_err (e) {
            ignore e;
            current_error_destroy();
            readable = false;
        }
        if (readable) {
            // A package.json that doesn't parse cleanly still marks a package, with nothing in it (tsc's readJson)
            package.exists = true;
            ignore json_parse_strict(cstr_sv(&contents), &package.json);
            cstr_drop(&contents);
        }
    }
    cstr_drop(&path);
    PackageJson* box = c_new(PackageJson, package);
    pthread_rwlock_wrlock(&shard->lock);
    // (If another thread's read of the same file got there first, the same contents: insert drops ours)
    auto inserted = hmap_package_json_box_insert(&shard->package_jsons, cstr_from_sv(directory), box);
    known = inserted.ref->second;
    pthread_rwlock_unlock(&shard->lock);
    return known->exists ? known : nullptr;
}

static int compare_csview(void const* a, void const* b) {
    csview const* x = a;
    csview const* y = b;
    int c = memcmp(x->buf, y->buf, (size_t)(x->size < y->size ? x->size : y->size));
    return c ? c : (x->size > y->size) - (x->size < y->size);
}

// tsgo's readPackageJsonPeerDependencies: `+name@version` for each peer dependency (sorted by name) installed next to
// the package's real directory, so that two installs of one version with other peers (pnpm's `vite@8.3.0_esbuild@...`
// directories) are two packages to deduplication
static void append_peer_dependencies(State* me, csview package_dir, PackageJson const* package, cstr* out) {
    JsonValue const* peers = json_get_tagged(&package->json, "peerDependencies", JSON_OBJECT);
    if (!peers || !peers->count) return;
    cstr real = module_resolver_real_path(my resolver, package_dir);
    csview dir = cstr_size(&real) ? cstr_sv(&real) : package_dir;
    // (The directory up to its last node_modules component)
    isize at = -1;
    for (isize i = 0; i + (isize)strlen("/node_modules") <= dir.size; i++) {
        if (!memcmp(dir.buf + i, "/node_modules", strlen("/node_modules")) &&
            (i + (isize)strlen("/node_modules") == dir.size || dir.buf[i + strlen("/node_modules")] == '/')
        ) {
            at = i + (isize)strlen("/node_modules");
        }
    }
    if (at >= 0) {
        csview node_modules = csview_from_n(dir.buf, at);
        csview* names = malloc(sizeof(csview) * (size_t)peers->count);
        for (isize i = 0; i < peers->count; i++) names[i] = cstr_sv(&peers->members[i].key);
        qsort(names, (size_t)peers->count, sizeof(csview), compare_csview);
        for (isize i = 0; i < peers->count; i++) {
            if (i && csview_equals_sv(names[i], names[i - 1])) continue; // (a repeated key is one map entry)
            cstr peer_dir = cstr_d(node_modules, "/", names[i]);
            PackageJson const* peer = package_json_of(me, cstr_sv(&peer_dir));
            cstr_drop(&peer_dir);
            if (!peer) continue;
            cstr_append_sv(out, c_sv("+"));
            cstr_append_sv(out, names[i]);
            cstr_append_sv(out, c_sv("@"));
            cstr_append_sv(out, json_get_string(&peer->json, "version"));
        }
        free(names);
    }
    cstr_drop(&real);
}

// tsc's parseNodeModuleFromPath + withPackageId: the package directory after the last node_modules (`@scope/name`
// for a scoped one) and what its package.json says
static cstr package_id_of(State* me, csview path, csview package_dir, cstr* package_name) {
    isize end = -1;
    if (package_dir.size && path.size > package_dir.size && sv_starts_with(path, package_dir) && path.buf[package_dir.size] == '/') {
        end = package_dir.size;
    } else {
        isize at = -1;
        for (isize i = csview_find_sv(path, c_sv("/node_modules/")); i != c_NPOS;) {
            at = i;
            isize next = csview_find_sv(csview_from_n(path.buf + i + 1, path.size - i - 1), c_sv("/node_modules/"));
            i = next == c_NPOS ? c_NPOS : i + 1 + next;
        }
        if (at < 0) return cstr_null;
        isize start = at + (isize)strlen("/node_modules/");
        end = start;
        for (int segments = path.buf[start] == '@' ? 2 : 1; segments--;) {
            while (end < path.size && path.buf[end] != '/') end++;
            if (segments) end++;
        }
        if (end >= path.size) return cstr_null;
    }
    PackageJson const* package = package_json_of(me, csview_from_n(path.buf, end));
    if (!package) return cstr_null;
    csview name = json_get_string(&package->json, "name");
    csview version = json_get_string(&package->json, "version");
    if (!name.size || !version.size) return cstr_null;
    *package_name = cstr_from_sv(name);
    cstr ret = cstr_d(name, csview_from_n(path.buf + end, path.size - end), "@", version);
    // (Under the lock of the shard the package.json is in)
    ResolverShard* shard = resolver_shard(my resolver, csview_from_n(path.buf, end));
    pthread_rwlock_rdlock(&shard->lock);
    bool computed = package->peer_suffix_computed;
    pthread_rwlock_unlock(&shard->lock);
    if (!computed) {
        cstr suffix = cstr_null;
        append_peer_dependencies(me, csview_from_n(path.buf, end), package, &suffix);
        pthread_rwlock_wrlock(&shard->lock);
        PackageJson* mut = (PackageJson*)package;
        if (!mut->peer_suffix_computed) {
            mut->peer_suffix = suffix;
            suffix = cstr_null;
            mut->peer_suffix_computed = true;
        }
        pthread_rwlock_unlock(&shard->lock);
        cstr_drop(&suffix);
    }
    // (Written once under the lock, read-only after)
    cstr_append_sv(&ret, cstr_sv(&package->peer_suffix));
    return ret;
}

// The nearest package.json at or above directory, and where it is
static PackageJson const* package_scope_of(State* me, csview directory, cstr* scope_dir) {
    cstr dir = cstr_from_sv(directory);
    while (true) {
        if (!ends_with(cstr_sv(&dir), "/node_modules")) {
            PackageJson const* found = package_json_of(me, cstr_sv(&dir));
            if (found) {
                *scope_dir = dir;
                return found;
            }
        }
        if (!go_up(&dir)) break;
    }
    cstr_drop(&dir);
    return nullptr;
}

// tsc's version as typesVersions and `types@` conditions test it (tsgo's core.Version(), "7.1.0-beta": a prerelease, so
// `>=7.1` holds and `>=7.1.0` doesn't), with tsgo's semver package's rules (internal/semver/version_range.go)
static const long TS_VERSION[3] = { 7, 1, 0 };
#define TS_VERSION_PRERELEASE "beta"

// A comparator's operand: major.minor.patch and a prerelease ("" none, else its dot-separated identifiers)
typedef struct SemVer {
    long part[3];
    csview prerelease;
} SemVer;

static bool semver_numeric(csview id) {
    if (!id.size) return false;
    for (isize i = 0; i < id.size; i++) if (id.buf[i] < '0' || id.buf[i] > '9') return false;
    return id.size == 1 || id.buf[0] != '0';
}

// tsc's version against `operand` (Version.Compare): numerically, then by prerelease, where none is the greater
static int semver_compare_mine(SemVer const* operand) {
    for (int i = 0; i < 3; i++) {
        if (TS_VERSION[i] != operand->part[i]) return TS_VERSION[i] < operand->part[i] ? -1 : 1;
    }
    csview mine = c_sv(TS_VERSION_PRERELEASE), theirs = operand->prerelease;
    if (!mine.size || !theirs.size) return mine.size == theirs.size ? 0 : mine.size ? -1 : 1;
    while (true) {
        if (!mine.size || !theirs.size) return mine.size == theirs.size ? 0 : mine.size ? 1 : -1;
        isize a = csview_find_sv(mine, c_sv(".")), b = csview_find_sv(theirs, c_sv("."));
        csview x = csview_slice(mine, 0, a == c_NPOS ? mine.size : a), y = csview_slice(theirs, 0, b == c_NPOS ? theirs.size : b);
        mine = a == c_NPOS ? c_sv("") : csview_slice(mine, a + 1, mine.size);
        theirs = b == c_NPOS ? c_sv("") : csview_slice(theirs, b + 1, theirs.size);
        if (csview_equals_sv(x, y)) continue;
        bool xn = semver_numeric(x), yn = semver_numeric(y);
        if (xn != yn) return xn ? -1 : 1; // (Numeric identifiers are the lower)
        if (xn && x.size != y.size) return x.size < y.size ? -1 : 1;
        for (isize i = 0; i < x.size && i < y.size; i++) {
            if (x.buf[i] != y.buf[i]) return (unsigned char)x.buf[i] < (unsigned char)y.buf[i] ? -1 : 1;
        }
        return x.size < y.size ? -1 : 1;
    }
}

// tsgo's parsePartial: `1`, `1.2`, `1.2.3-pre+build`, with `x`/`X`/`*` wildcards. wild[i]: part i is a wildcard
static bool semver_parse_partial(csview text, SemVer* out, bool wild[3]) {
    *out = (SemVer){0};
    isize at = 0;
    for (int i = 0; i < 3; i++) {
        wild[i] = true;
        if (i && (at >= text.size || text.buf[at] != '.')) continue;
        if (i) at++;
        if (at < text.size && (text.buf[at] == 'x' || text.buf[at] == 'X' || text.buf[at] == '*')) {
            at++;
            continue;
        }
        isize from = at;
        long n = 0;
        while (at < text.size && text.buf[at] >= '0' && text.buf[at] <= '9') n = n * 10 + (text.buf[at++] - '0');
        if (at == from || (at - from > 1 && text.buf[from] == '0')) return false;
        wild[i] = false;
        out->part[i] = n;
    }
    for (int i = 0; i < 3; i++) if (wild[i]) for (int j = i; j < 3; j++) { wild[j] = true; out->part[j] = 0; }
    if (at < text.size && text.buf[at] == '-' && !wild[2]) {
        isize from = ++at;
        while (at < text.size && text.buf[at] != '+') at++;
        out->prerelease = csview_slice(text, from, at);
        if (!out->prerelease.size) return false;
    }
    if (at < text.size && text.buf[at] == '+' && !wild[2]) at = text.size;
    return at == text.size;
}

static SemVer semver_bump(SemVer v, int part) {
    SemVer ret = {0};
    for (int i = 0; i < part; i++) ret.part[i] = v.part[i];
    ret.part[part] = v.part[part] + 1;
    return ret;
}

static bool semver_test(char const* op, SemVer operand) {
    int c = semver_compare_mine(&operand);
    if (!strcmp(op, "<")) return c < 0;
    if (!strcmp(op, "<=")) return c <= 0;
    if (!strcmp(op, ">")) return c > 0;
    if (!strcmp(op, ">=")) return c >= 0;
    return c == 0;
}

// One comparator of a range (tsgo's parseComparator then testComparator), into *fits; false if it doesn't parse
static bool semver_comparator(csview op, csview text, bool* fits) {
    SemVer v;
    bool wild[3];
    if (!semver_parse_partial(text, &v, wild)) return false;
    *fits = true;
    SemVer zero_pre = v;
    zero_pre.prerelease = c_sv("0");
    if (wild[0]) {
        if (csview_equals(op, "<") || csview_equals(op, ">")) *fits = semver_test("<", (SemVer){ .prerelease = c_sv("0") });
        return true;
    }
    if (csview_equals(op, "~")) {
        *fits = semver_test(">=", v) && semver_test("<", semver_bump(v, wild[1] ? 0 : 1));
    } else if (csview_equals(op, "^")) {
        int bump = v.part[0] > 0 || wild[1] ? 0 : v.part[1] > 0 || wild[2] ? 1 : 2;
        *fits = semver_test(">=", v) && semver_test("<", semver_bump(v, bump));
    } else if (csview_equals(op, "<") || csview_equals(op, ">=")) {
        *fits = semver_test(csview_equals(op, "<") ? "<" : ">=", wild[1] || wild[2] ? zero_pre : v);
    } else if (csview_equals(op, "<=") || csview_equals(op, ">")) {
        bool le = csview_equals(op, "<=");
        if (wild[1] || wild[2]) {
            SemVer bumped = semver_bump(v, wild[1] ? 0 : 1);
            bumped.prerelease = c_sv("0");
            *fits = semver_test(le ? "<" : ">=", bumped);
        } else {
            *fits = semver_test(le ? "<=" : ">", v);
        }
    } else if (!op.size || csview_equals(op, "=")) {
        if (wild[1] || wild[2]) {
            SemVer high = semver_bump(v, wild[1] ? 0 : 1);
            high.prerelease = c_sv("0");
            *fits = semver_test(">=", zero_pre) && semver_test("<", high);
        } else {
            *fits = semver_test("=", v);
        }
    } else {
        return false;
    }
    return true;
}

// One comparator set of a semver range against tsc's version: ">=3.1 <4.0", "*", "3.x", "^5.0", "~5.8", "1.0 - 2.x"
static bool version_in_comparators(csview set) {
    set = csview_trim_whitespace(set);
    isize hyphen = csview_find_sv(set, c_sv(" - "));
    if (hyphen != c_NPOS) {
        SemVer low, high;
        bool lw[3], hw[3];
        if (!semver_parse_partial(csview_trim_whitespace(csview_slice(set, 0, hyphen)), &low, lw) ||
            !semver_parse_partial(csview_trim_whitespace(csview_slice(set, hyphen + 3, set.size)), &high, hw)
        ) {
            return false;
        }
        if (!lw[0] && !semver_test(">=", low)) return false;
        if (hw[0]) return true;
        if (hw[1]) return semver_test("<", semver_bump(high, 0));
        if (hw[2]) return semver_test("<", semver_bump(high, 1));
        return semver_test("<=", high);
    }
    while (true) {
        set = csview_trim_whitespace(set);
        if (!set.size) return true;
        isize end = 0;
        while (end < set.size && set.buf[end] != ' ' && set.buf[end] != '\t') end++;
        csview comparator = csview_slice(set, 0, end);
        set = csview_slice(set, end, set.size);
        isize op_len = 0;
        while (op_len < comparator.size && op_len < 2 && strchr("<>=^~", comparator.buf[op_len])) op_len++;
        csview op = csview_slice(comparator, 0, op_len);
        if (op_len == 2 && !csview_equals(op, "<=") && !csview_equals(op, ">=")) return false;
        bool fits;
        if (!semver_comparator(op, csview_slice(comparator, op_len, comparator.size), &fits) || !fits) return false;
    }
}

static bool version_in_range(csview range) {
    while (true) {
        isize bar = csview_find_sv(range, c_sv("||"));
        if (version_in_comparators(csview_slice(range, 0, bar))) return true;
        if (bar == c_NPOS) return false;
        range = csview_slice(range, bar + 2, range.size);
    }
}

// The `paths`-like object of the first `typesVersions` entry that matches tsc's version
static JsonValue const* version_paths_of(PackageJson const* package) {
    JsonValue const* versions = json_get_tagged(&package->json, "typesVersions", JSON_OBJECT);
    if (!versions) return nullptr;
    for (isize i = 0; i < versions->count; i++) {
        if (version_in_range(cstr_sv(&versions->members[i].key))) {
            JsonValue const* paths = &versions->members[i].value;
            return paths->tag == JSON_OBJECT ? paths : nullptr;
        }
    }
    return nullptr;
}

// ---- `paths` and friends ----

// Whether name matches "prefix*suffix" (or the pattern exactly); the text the star stands for
static bool pattern_matches(csview pattern, csview name, csview* star, isize* prefix_len) {
    isize at = csview_find_sv(pattern, c_sv("*"));
    if (at == c_NPOS) {
        *star = c_sv("");
        *prefix_len = INTPTR_MAX; // An exact match beats any pattern
        return csview_equals_sv(pattern, name);
    }
    csview prefix = csview_slice(pattern, 0, at);
    csview suffix = csview_slice(pattern, at + 1, pattern.size);
    if (name.size < prefix.size + suffix.size) return false;
    if (memcmp(name.buf, prefix.buf, (size_t)prefix.size)) return false;
    if (memcmp(name.buf + name.size - suffix.size, suffix.buf, (size_t)suffix.size)) return false;
    *star = csview_slice(name, prefix.size, name.size - suffix.size);
    *prefix_len = prefix.size;
    return true;
}

static cstr replace_first_star(csview text, csview star) {
    isize at = csview_find_sv(text, c_sv("*"));
    if (at == c_NPOS) return cstr_from_sv(text);
    return cstr_d(csview_slice(text, 0, at), star, csview_slice(text, at + 1, text.size));
}

typedef bool (*Loader)(State* me, csview candidate, int extensions, Found* out);

static bool load_as_file_or_directory(State* me, csview candidate, int extensions, Found* out);

static ResolvedKind kind_of_extension(csview extension) {
    if (csview_starts_with(extension, ".d.")) return RESOLVED_DTS;
    if (extension_is_ts_source(extension)) return RESOLVED_TS;
    if (csview_equals(extension, ".json")) return RESOLVED_JSON;
    return RESOLVED_JS;
}

// One substitution of a matched `paths` / `typesVersions` pattern
// join() of a name that may be rooted to tsc as a DOS drive path (`import "c:/x"`, a `paths` substitution
// `c:/shared/*`): tsc's combinePaths keeps it, and a drive root is one of the current directory (path_is_drive_rooted())
static cstr join_rooted(State const* me, csview dir, csview rest) {
    if (path_is_drive_rooted(rest)) return absolute_path_of(cstr_sv(&my resolver->cwd), rest);
    return join(dir, rest);
}

static bool try_substitution(
    State* me, csview base, csview substitution, csview star, int extensions, Loader loader, Found* out
) {
    cstr path = replace_first_star(substitution, star);
    cstr candidate = join_rooted(me, base, cstr_sv(&path));
    cstr_drop(&path);
    bool found = false;
    // A mapping may name a file with its extension, which an import shouldn't
    csview extension = script_extension_of(substitution);
    if (extension.size && try_file(me, cstr_sv(&candidate), extension, &out->path)) {
        out->kind = kind_of_extension(extension);
        found = true;
    }
    if (!found) found = loader(me, cstr_sv(&candidate), extensions, out);
    cstr_drop(&candidate);
    return found;
}

// tsc's tryLoadModuleUsingPaths() over the option
static Search load_using_paths_option(State* me, csview name, int extensions, Loader loader, Found* out) {
    CompilerOptions const* options = my resolver->options;
    PathMapping const* best = nullptr;
    csview best_star = c_sv("");
    isize best_prefix = -1;
    for (isize i = 0; i < options->path_count; i++) {
        csview star;
        isize prefix_len;
        if (pattern_matches(cstr_sv(&options->paths[i].pattern), name, &star, &prefix_len) && prefix_len > best_prefix) {
            best = &options->paths[i];
            best_star = star;
            best_prefix = prefix_len;
        }
    }
    if (!best) return SEARCH_CONTINUE;
    cstr base = cstr_size(&options->paths_base) ? option_path(my resolver, cstr_sv(&options->paths_base)) :
        option_path(my resolver, c_sv("."));
    Search ret = SEARCH_STOP;
    for (isize i = 0; i < best->substitutions.count; i++) {
        if (try_substitution(me, cstr_sv(&base), cstr_sv(&best->substitutions.items[i]), best_star, extensions, loader, out)) {
            ret = SEARCH_FOUND;
            break;
        }
    }
    cstr_drop(&base);
    return ret;
}

// The same over a `typesVersions` entry of a package
static Search load_using_version_paths(
    State* me, JsonValue const* paths, csview base, csview name, int extensions, Loader loader, Found* out
) {
    JsonMember const* best = nullptr;
    csview best_star = c_sv("");
    isize best_prefix = -1;
    for (isize i = 0; i < paths->count; i++) {
        csview star;
        isize prefix_len;
        if (pattern_matches(cstr_sv(&paths->members[i].key), name, &star, &prefix_len) && prefix_len > best_prefix) {
            best = &paths->members[i];
            best_star = star;
            best_prefix = prefix_len;
        }
    }
    // (A matched pattern whose substitutions find nothing falls back to the plain lookup, like tsc's
    // tryLoadModuleUsingPaths: moment's `"*": ["ts3.1-typings/*"]` has no `locale/es`, its locale/es.js is found)
    if (!best || best->value.tag != JSON_ARRAY) return SEARCH_CONTINUE;
    for (isize i = 0; i < best->value.count; i++) {
        JsonValue const* substitution = &best->value.items[i];
        if (substitution->tag != JSON_STRING) continue;
        if (try_substitution(me, base, cstr_sv(&substitution->string), best_star, extensions, loader, out)) {
            return SEARCH_FOUND;
        }
    }
    return SEARCH_CONTINUE;
}

// tsc's tryLoadModuleUsingRootDirs(): a relative name is looked for below every root dir, as if they were one directory
static bool load_using_root_dirs(State* me, csview name, int extensions, Loader loader, Found* out) {
    StrList const* root_dirs = &my resolver->options->root_dirs;
    if (!root_dirs->count) return false;
    cstr candidate = join(my containing_dir, name);
    isize matched = -1;
    isize matched_len = 0;
    for (isize i = 0; i < root_dirs->count; i++) {
        cstr root = option_path(my resolver, cstr_sv(&root_dirs->items[i]));
        if (cstr_size(&root) > 1) cstr_append_lit(&root, "/");
        if (cstr_starts_with_sv(&candidate, cstr_sv(&root)) && cstr_size(&root) > matched_len) {
            matched = i;
            matched_len = cstr_size(&root);
        }
        cstr_drop(&root);
    }
    bool found = false;
    if (matched >= 0) {
        found = loader(me, cstr_sv(&candidate), extensions, out);
        csview suffix = csview_slice(cstr_sv(&candidate), matched_len, cstr_size(&candidate));
        for (isize i = 0; i < root_dirs->count && !found; i++) {
            if (i == matched) continue;
            cstr root = option_path(my resolver, cstr_sv(&root_dirs->items[i]));
            cstr other = join(cstr_sv(&root), suffix);
            found = loader(me, cstr_sv(&other), extensions, out);
            cstr_drop(&other);
            cstr_drop(&root);
        }
    }
    cstr_drop(&candidate);
    return found;
}

// tsc's tryLoadModuleUsingOptionalResolutionSettings()
static Search load_using_options(State* me, csview name, int extensions, Loader loader, Found* out) {
    CompilerOptions const* options = my resolver->options;
    bool relative = module_name_is_relative(name);
    if (options->path_count && !(name.buf[0] == '.' && relative)) {
        // A pattern that matched and led nowhere ends what the options have to say, the ordinary lookup still runs
        Search result = load_using_paths_option(me, name, extensions, loader, out);
        if (result != SEARCH_CONTINUE) return result == SEARCH_FOUND ? SEARCH_FOUND : SEARCH_CONTINUE;
    }
    if (relative) {
        return load_using_root_dirs(me, name, extensions, loader, out) ? SEARCH_FOUND : SEARCH_CONTINUE;
    }
    return SEARCH_CONTINUE;
}

// ---- Directories and packages ----

// tsc's loadFileNameFromPackageJsonField(): a file a package.json names, which may be the output of the file we want.
// `value`: the exports/imports target as written, if it was one: a TS extension it doesn't end with came from the
// import (resolvedUsingTsExtension)
static bool load_package_json_field(State* me, csview candidate, csview value, int extensions, Found* out) {
    csview extension = script_extension_of(candidate);
    if ((extensions & EXT_TS && extension_is_ts_source(extension)) ||
        (extensions & EXT_DTS && csview_starts_with(extension, ".d."))
    ) {
        if (try_file(me, candidate, extension, &out->path)) {
            out->kind = kind_of_extension(extension);
            out->used_ts_extension = value.size && !(value.size >= extension.size && memcmp(value.buf + value.size - extension.size, extension.buf, (size_t)extension.size) == 0);
            // (The specifier's part a `*` matched had it: `#/foo.ts.omg` of "#/*.omg": "./src/*", tsgo's
            // loadFileNameFromPackageJSONField)
            out->ts_extension_from_star = out->used_ts_extension && csview_ends_with(value, "*");
            return true;
        }
    }
    if (!load_file_no_implicit_extensions(me, candidate, extensions, out)) return false;
    out->used_ts_extension = false; // The package wrote the extension, not the import
    return true;
}

static bool load_file_loader(State* me, csview candidate, int extensions, Found* out) {
    return load_file(me, candidate, extensions, out);
}

// What a package.json (may be null) makes of its directory: `typings` / `types` / `main`, then `index`
static bool load_directory(State* me, csview directory, PackageJson const* package, int extensions, Found* out) {
    csview entry = c_sv("");
    JsonValue const* version_paths = nullptr;
    if (package) {
        if (extensions & (EXT_TS | EXT_DTS)) {
            entry = json_get_string(&package->json, "typings");
            if (!entry.size) entry = json_get_string(&package->json, "types");
        }
        if (!entry.size) entry = json_get_string(&package->json, "main");
        version_paths = version_paths_of(package);
    }
    cstr entry_path = entry.size ? join(directory, entry) : cstr_null;
    cstr index_path = join(directory, c_sv("index"));
    bool found = false;

    if (version_paths) {
        // The name `typesVersions` sees is the entry relative to the package
        csview base = entry.size ? cstr_sv(&entry_path) : cstr_sv(&index_path);
        csview relative = csview_slice(base, directory.size + 1, base.size);
        Search result = load_using_version_paths(
            me, version_paths, directory, relative, extensions, load_as_file_or_directory, out);
        if (result != SEARCH_CONTINUE) {
            cstr_drop(&entry_path);
            cstr_drop(&index_path);
            return result == SEARCH_FOUND;
        }
    }

    if (entry.size) {
        found = load_package_json_field(me, cstr_sv(&entry_path), (csview){0}, extensions, out);
        if (!found) {
            // `"main": "lib"` or `"types": "dist/index"`. Not through package.json again, and with extension probing
            // even in ESM mode
            bool esm_mode = my esm_mode;
            my esm_mode = false;
            // Even when only declarations are wanted, "types" may lead to a .ts file
            int expanded = extensions == EXT_DTS ? EXT_TS | EXT_DTS : extensions;
            found = load_file(me, cstr_sv(&entry_path), expanded, out);
            if (!found && directory_exists(me, cstr_sv(&entry_path))) {
                cstr nested_index = join(cstr_sv(&entry_path), c_sv("index"));
                found = load_file(me, cstr_sv(&nested_index), expanded, out);
                cstr_drop(&nested_index);
            }
            my esm_mode = esm_mode;
        }
    }
    if (!found && !my esm_mode) found = load_file(me, cstr_sv(&index_path), extensions, out);
    cstr_drop(&entry_path);
    cstr_drop(&index_path);
    return found;
}

// tsc's nodeLoadModuleByRelativeName(): a path without (or with a replaceable) extension, as a file, then a directory
static bool load_as_file_or_directory(State* me, csview candidate, int extensions, Found* out) {
    bool trailing_slash = ends_with(candidate, "/");
    if (trailing_slash) candidate.size--;
    if (!trailing_slash) {
        if (!directory_exists(me, parent_of(candidate))) return false;
        if (load_file(me, candidate, extensions, out)) return true;
    }
    if (my esm_mode && !my in_type_reference) return false;
    if (!directory_exists(me, candidate)) return false;
    return load_directory(me, candidate, package_json_of(me, candidate), extensions, out);
}

// ---- `exports` and `imports` ----

static bool condition_applies(State* me, csview condition) {
    if (csview_equals(condition, "default")) return true;
    for (isize i = 0; i < my condition_count; i++) {
        if (csview_equals(condition, my conditions[i])) return true;
    }
    // "types@>=5.2"
    if (csview_starts_with(condition, "types@")) {
        for (isize i = 0; i < my condition_count; i++) {
            if (!strcmp(my conditions[i], "types")) return version_in_range(csview_slice(condition, 6, condition.size));
        }
    }
    return false;
}

static bool has_bad_segment(csview path, bool skip_first) {
    while (path.size) {
        isize end = csview_find_sv(path, c_sv("/"));
        csview segment = csview_slice(path, 0, end);
        if (!skip_first && (csview_equals(segment, "..") || csview_equals(segment, ".") ||
            csview_equals(segment, "node_modules"))
        ) {
            return true;
        }
        skip_first = false;
        if (end == c_NPOS) break;
        path = csview_slice(path, end + 1, path.size);
    }
    return false;
}

static cstr replace_all_stars(csview text, csview star) {
    cstr ret = cstr_null;
    while (true) {
        isize at = csview_find_sv(text, c_sv("*"));
        cstr_append_sv(&ret, csview_slice(text, 0, at));
        if (at == c_NPOS) break;
        cstr_append_sv(&ret, star);
        text = csview_slice(text, at + 1, text.size);
    }
    return ret;
}

static Search resolve_non_relative(State* me, csview name, csview from_dir, int extensions, Found* out);

// tsc's containsPath() of two normalised absolute paths
static bool path_contains(csview dir, csview path) {
    if (dir.size == 1) return true;
    return sv_starts_with(path, dir) && (path.size == dir.size || path.buf[dir.size] == '/');
}

// tsc's extensionIsOk()
static bool extension_is_ok(int extensions, const char* extension) {
    csview e = c_sv(extension, strlen(extension));
    if (extensions & EXT_JS && (csview_equals(e, ".js") || csview_equals(e, ".jsx") || csview_equals(e, ".mjs") ||
        csview_equals(e, ".cjs"))) return true;
    if (extensions & EXT_TS && (csview_equals(e, ".ts") || csview_equals(e, ".tsx") || csview_equals(e, ".mts") ||
        csview_equals(e, ".cts"))) return true;
    if (extensions & EXT_DTS && (csview_equals(e, ".d.ts") || csview_equals(e, ".d.mts") || csview_equals(e, ".d.cts")))
        return true;
    return extensions & EXT_JSON && csview_equals(e, ".json");
}

// tsc's tryLoadInputFileForPath(): a package's exports/imports that point into the outDir/declarationDir of the
// project itself (package self-names) mean the input files the outputs are made from, relative to the rootDir or the
// config's directory
static Search try_load_input_file_for_path(
    State* me, csview final_path, csview entry, csview package_dir, bool is_imports, int extensions, Found* out
) {
    CompilerOptions const* options = my resolver->options;
    if (!cstr_size(&options->out_dir) && !cstr_size(&options->declaration_dir)) return SEARCH_CONTINUE;
    if (path_contains_node_modules(final_path)) return SEARCH_CONTINUE;
    bool has_config = cstr_size(&options->config_dir) > 0;
    if (has_config) {
        cstr config_dir = option_path(my resolver, c_sv("."));
        bool inside = path_contains(package_dir, cstr_sv(&config_dir));
        cstr_drop(&config_dir);
        if (!inside) return SEARCH_CONTINUE;
    }
    StrList guesses = {0};
    // (A project's common source directory is its config's directory, 6.0: getCommonSourceDirectory)
    if (cstr_size(&options->root_dir) || has_config) {
        cstr dir = cstr_size(&options->root_dir) ? option_path(my resolver, cstr_sv(&options->root_dir)) :
            option_path(my resolver, c_sv("."));
        str_list_push(&guesses, cstr_sv(&dir));
        cstr_drop(&dir);
    } else {
        // (tsgo doesn't guess the root like 6.0 did: without a rootDir or a config it is ambiguous, TS2209/TS2210,
        // and the target is unresolved)
        if (!cstr_size(&my ambiguous_root_package_json)) {
            cstr_assign_sv(&my ambiguous_root_entry, entry.size ? entry : c_sv("."));
            my ambiguous_root_package_json = join(package_dir, c_sv("package.json"));
            my ambiguous_root_in_imports = is_imports;
        }
        return SEARCH_STOP;
    }
    static const char* const output_extensions[] = { ".mjs", ".cjs", ".js", ".json", ".d.mts", ".d.cts", ".d.ts" };
    bool found = false;
    for (isize g = guesses.count - 1; g >= 0 && !found; g--) {
        csview guess = cstr_sv(&guesses.items[g]);
        cstr candidates[2] = {0};
        isize candidate_count = 0;
        // Relative to the current directory with a tsconfig (its paths are absolute by then), else to the guess
        csview output_dirs[2] = { cstr_sv(&options->declaration_dir), cstr_sv(&options->out_dir) };
        for (isize i = 0; i < 2; i++) {
            if (!output_dirs[i].size) continue;
            if (i == 1 && csview_equals_sv(output_dirs[1], output_dirs[0])) continue;
            candidates[candidate_count++] = has_config ? option_path(my resolver, output_dirs[i]) :
                join(guess, output_dirs[i]);
        }
        for (isize c = 0; c < candidate_count && !found; c++) {
            csview candidate = cstr_sv(&candidates[c]);
            if (!path_contains(candidate, final_path) || final_path.size == candidate.size) continue;
            csview fragment = csview_slice(final_path, candidate.size == 1 ? 1 : candidate.size + 1, final_path.size);
            cstr base = join(guess, fragment);
            csview base_sv = cstr_sv(&base);
            for (isize e = 0; e < (isize)c_arraylen(output_extensions) && !found; e++) {
                if (!ends_with(base_sv, output_extensions[e])) continue;
                // tsc's getPossibleOriginalInputExtensionForExtension()
                static const char* const mts[] = { ".mts", ".mjs", nullptr };
                static const char* const cts[] = { ".cts", ".cjs", nullptr };
                static const char* const json[] = { ".json", nullptr };
                static const char* const ts[] = { ".tsx", ".ts", ".jsx", ".js", nullptr };
                const char* const* inputs =
                    ends_with(base_sv, ".d.mts") || ends_with(base_sv, ".mjs") || ends_with(base_sv, ".mts") ? mts :
                    ends_with(base_sv, ".d.cts") || ends_with(base_sv, ".cjs") || ends_with(base_sv, ".cts") ? cts :
                    ends_with(base_sv, ".d.json.ts") ? json : ts;
                csview stem = csview_slice(base_sv, 0, base_sv.size - (isize)strlen(output_extensions[e]));
                // (tsc resolves an exports target in one pass of every extension, where the input a target is the
                // output of comes first: the TypeScript-only pass of the HACK in resolve_uncached() takes a JavaScript
                // input too, as the JavaScript pass would never get here once the target's declaration file is found)
                int input_extensions = extensions & EXT_TS && options->allow_js ? extensions | EXT_JS : extensions;
                for (isize k = 0; inputs[k] && !found; k++) {
                    if (!extension_is_ok(input_extensions, inputs[k])) continue;
                    cstr input = cstr_d(stem, inputs[k]);
                    if (file_exists(me, cstr_sv(&input))) {
                        found = true;
                        Found loaded = {0};
                        if (load_package_json_field(me, cstr_sv(&input), (csview){0}, input_extensions, &loaded)) *out = loaded;
                        else found = false;
                    }
                    cstr_drop(&input);
                }
            }
            cstr_drop(&base);
        }
        for (isize c = 0; c < candidate_count; c++) cstr_drop(&candidates[c]);
    }
    str_list_drop(&guesses);
    return found ? SEARCH_FOUND : SEARCH_CONTINUE;
}

// tsc's loadModuleFromTargetExportOrImport(): what one target of an exports/imports map leads to. Anything that
// doesn't resolve lets the next condition or array entry try (the callers make an overall miss final)
static Search load_target(
    State* me, JsonValue const* target, csview subpath, bool pattern, bool is_imports, csview package_dir,
    int extensions, Found* out
) {
    switch (target->tag) {
        case JSON_STRING: {
            csview text = cstr_sv(&target->string);
            if (!pattern && subpath.size && !ends_with(text, "/")) return SEARCH_CONTINUE;
            if (!csview_starts_with(text, "./")) {
                if (is_imports && !csview_starts_with(text, "../") && !csview_starts_with(text, "/")) {
                    // `"#dep": "some-package"`
                    cstr name = pattern ? replace_all_stars(text, subpath) : cstr_d(text, subpath);
                    Search result = resolve_non_relative(me, cstr_sv(&name), package_dir, extensions, out);
                    cstr_drop(&name);
                    return result == SEARCH_FOUND ? SEARCH_FOUND : SEARCH_CONTINUE;
                }
                return SEARCH_CONTINUE;
            }
            if (has_bad_segment(text, true) || has_bad_segment(subpath, false)) return SEARCH_CONTINUE;
            cstr relative = pattern ? replace_all_stars(text, subpath) : cstr_d(text, subpath);
            cstr path = join(package_dir, cstr_sv(&relative));
            // (An ambiguous project root stops there: tsgo's tryLoadInputFileForPath is unresolved())
            Search input = try_load_input_file_for_path(me, cstr_sv(&path), subpath, package_dir, is_imports, extensions, out);
            bool found = input == SEARCH_FOUND || (input == SEARCH_CONTINUE && load_package_json_field(me, cstr_sv(&path), text, extensions, out));
            cstr_drop(&relative);
            cstr_drop(&path);
            return found ? SEARCH_FOUND : input == SEARCH_STOP ? SEARCH_STOP : SEARCH_CONTINUE;
        }
        case JSON_OBJECT:
            for (isize i = 0; i < target->count; i++) {
                if (!condition_applies(me, cstr_sv(&target->members[i].key))) continue;
                Search result = load_target(
                    me, &target->members[i].value, subpath, pattern, is_imports, package_dir, extensions, out);
                if (result != SEARCH_CONTINUE) return result;
            }
            return SEARCH_CONTINUE;
        case JSON_ARRAY:
            if (!target->count) return SEARCH_CONTINUE;
            for (isize i = 0; i < target->count; i++) {
                Search result = load_target(
                    me, &target->items[i], subpath, pattern, is_imports, package_dir, extensions, out);
                if (result != SEARCH_CONTINUE) return result;
            }
            return SEARCH_CONTINUE;
        case JSON_NULL:
            // Explicitly mapped to nothing: no other condition or entry gets to try (tsc 6.0)
            return SEARCH_STOP;
        default:
            return SEARCH_CONTINUE;
    }
}

// tsc's comparePatternKeys(): the longer the part before the star the better, then the longer key
static bool pattern_key_is_better(csview a, csview b) {
    isize a_star = csview_find_sv(a, c_sv("*"));
    isize b_star = csview_find_sv(b, c_sv("*"));
    isize a_base = a_star == c_NPOS ? a.size : a_star + 1;
    isize b_base = b_star == c_NPOS ? b.size : b_star + 1;
    if (a_base != b_base) return a_base > b_base;
    if (a_star == c_NPOS) return false;
    if (b_star == c_NPOS) return true;
    return a.size > b.size;
}

// tsc's loadModuleFromExportsOrImports(): a lookup of "./sub/path" or "#name" in a map of subpaths and patterns
static Search load_from_map(
    State* me, JsonValue const* map, csview key, bool is_imports, csview package_dir, int extensions, Found* out
) {
    bool key_is_plain = !ends_with(key, "/") && csview_find_sv(key, c_sv("*")) == c_NPOS;
    if (key_is_plain) {
        for (isize i = map->count - 1; i >= 0; i--) {
            if (cstr_equals_sv(&map->members[i].key, key)) {
                Search result = load_target(
                    me, &map->members[i].value, c_sv(""), false, is_imports, package_dir, extensions, out);
                return result == SEARCH_CONTINUE ? SEARCH_STOP : result;
            }
        }
    }
    JsonMember const* best = nullptr;
    for (isize i = 0; i < map->count; i++) {
        csview candidate = cstr_sv(&map->members[i].key);
        isize star = csview_find_sv(candidate, c_sv("*"));
        bool matches;
        if (star != c_NPOS) {
            if (csview_find_sv(csview_slice(candidate, star + 1, candidate.size), c_sv("*")) != c_NPOS) continue;
            csview prefix = csview_slice(candidate, 0, star);
            csview suffix = csview_slice(candidate, star + 1, candidate.size);
            matches = key.size >= prefix.size + suffix.size && sv_starts_with(key, prefix) &&
                sv_ends_with(key, suffix) && !csview_equals_sv(key, candidate);
        } else if (ends_with(candidate, "/")) {
            matches = sv_starts_with(key, candidate);
        } else {
            continue;
        }
        if (matches && (!best || pattern_key_is_better(candidate, cstr_sv(&best->key)))) best = &map->members[i];
    }
    if (!best) return SEARCH_CONTINUE;
    csview matched = cstr_sv(&best->key);
    isize star = csview_find_sv(matched, c_sv("*"));
    csview subpath = star != c_NPOS ?
        csview_slice(key, star, key.size - (matched.size - star - 1)) :
        csview_slice(key, matched.size, key.size);
    Search result = load_target(me, &best->value, subpath, star != c_NPOS, is_imports, package_dir, extensions, out);
    return result == SEARCH_CONTINUE ? SEARCH_STOP : result;
}

static bool any_key_starts_with_dot(JsonValue const* object) {
    for (isize i = 0; i < object->count; i++) {
        if (cstr_starts_with(&object->members[i].key, ".")) return true;
    }
    return false;
}

// tsc's loadModuleFromExports(). subpath is "." or "./sub/path"
static Search load_from_exports(
    State* me, PackageJson const* package, csview package_dir, csview subpath, int extensions, Found* out
) {
    JsonValue const* exports = json_get(&package->json, "exports");
    if (!exports || exports->tag == JSON_NULL) return SEARCH_CONTINUE;
    bool is_subpath_map = exports->tag == JSON_OBJECT && any_key_starts_with_dot(exports);
    if (csview_equals(subpath, ".")) {
        JsonValue const* main_export = is_subpath_map ? json_get(exports, ".") : exports;
        if (main_export) {
            Search result = load_target(me, main_export, c_sv(""), false, false, package_dir, extensions, out);
            if (result == SEARCH_FOUND) cstr_assign_sv(&out->package_dir, package_dir);
            return result == SEARCH_FOUND ? SEARCH_FOUND : SEARCH_STOP;
        }
    } else if (is_subpath_map) {
        Search result = load_from_map(me, exports, subpath, false, package_dir, extensions, out);
        if (result == SEARCH_FOUND) cstr_assign_sv(&out->package_dir, package_dir);
        if (result != SEARCH_CONTINUE) return result;
    }
    return SEARCH_STOP;
}

// `import "#internal/x"`: the `imports` of the package the importing file is in
static Search load_from_imports(State* me, csview name, csview from_dir, int extensions, Found* out) {
    // (`#/x` only with tsc 6.0's NodeResolutionFeatures.ImportsPatternRoot: nodenext and bundler, not node16)
    if (csview_equals(name, "#") || (csview_starts_with(name, "#/") && my kind == MODULE_RESOLUTION_NODE16)) return SEARCH_STOP;
    cstr scope_dir = cstr_null;
    PackageJson const* scope = package_scope_of(me, from_dir, &scope_dir);
    Search result = SEARCH_STOP;
    if (scope) {
        JsonValue const* imports = json_get_tagged(&scope->json, "imports", JSON_OBJECT);
        if (imports) {
            result = load_from_map(me, imports, name, true, cstr_sv(&scope_dir), extensions, out);
            if (result == SEARCH_FOUND) cstr_assign_sv(&out->package_dir, cstr_sv(&scope_dir));
            if (result == SEARCH_CONTINUE) result = SEARCH_STOP;
        }
    }
    cstr_drop(&scope_dir);
    return result;
}

// A package importing itself by name, through its own `exports`
static Search load_from_self_name(State* me, csview name, csview from_dir, int extensions, Found* out) {
    cstr scope_dir = cstr_null;
    PackageJson const* scope = package_scope_of(me, from_dir, &scope_dir);
    Search result = SEARCH_CONTINUE;
    if (scope && json_get(&scope->json, "exports")) {
        csview package_name = json_get_string(&scope->json, "name");
        if (package_name.size && sv_starts_with(name, package_name) &&
            (name.size == package_name.size || name.buf[package_name.size] == '/')
        ) {
            cstr subpath = cstr_d(".", csview_slice(name, package_name.size, name.size));
            // tsc: types first, then whatever else the caller accepts
            result = load_from_exports(me, scope, cstr_sv(&scope_dir), cstr_sv(&subpath), extensions, out);
            cstr_drop(&subpath);
        }
    }
    cstr_drop(&scope_dir);
    return result;
}

// ---- node_modules ----

// "@scope/pkg/sub/path" -> "@scope/pkg" + "sub/path"
static csview package_name_of(csview name, csview* rest) {
    isize slash = csview_find_sv(name, c_sv("/"));
    if (slash != c_NPOS && name.buf[0] == '@') {
        isize second = csview_find_sv(csview_slice(name, slash + 1, name.size), c_sv("/"));
        slash = second == c_NPOS ? c_NPOS : slash + 1 + second;
    }
    if (slash == c_NPOS) {
        *rest = c_sv("");
        return name;
    }
    *rest = csview_slice(name, slash + 1, name.size);
    return csview_slice(name, 0, slash);
}

// "@scope/pkg" lives in @types as "scope__pkg"
static cstr mangle_scoped_name(csview name) {
    if (!name.size || name.buf[0] != '@') return cstr_from_sv(name);
    isize slash = csview_find_sv(name, c_sv("/"));
    if (slash == c_NPOS) return cstr_from_sv(name);
    return cstr_d(csview_slice(name, 1, slash), "__", csview_slice(name, slash + 1, name.size));
}

typedef struct PackageLoad {
    PackageJson const* package;
    csview directory;
    bool has_rest;
} PackageLoad;

// tsc's loadModuleFromSpecificNodeModulesDirectory()
static Search load_from_node_modules_dir(State* me, csview node_modules, csview name, int extensions, Found* out) {
    // (tsgo strips a trailing separator here, `pkg/dir/` is `pkg/dir`: `prismjs/components/` finds components.d.ts
    // before components/index.d.ts. tsc 6 kept it, which found no file)
    while (name.size > 1 && name.buf[name.size - 1] == '/') name.size--;
    cstr candidate = join(node_modules, name);
    csview rest;
    csview package_name = package_name_of(name, &rest);
    cstr package_dir = join(node_modules, package_name);
    Search result = SEARCH_CONTINUE;
    bool directory_only = false;

    PackageJson const* package = directory_exists(me, cstr_sv(&candidate)) ? package_json_of(me, cstr_sv(&candidate)) : nullptr;
    PackageJson const* root_package = rest.size ? package_json_of(me, cstr_sv(&package_dir)) : package;
    bool exports_rule = my use_exports && root_package && json_get(&root_package->json, "exports") &&
        json_get(&root_package->json, "exports")->tag != JSON_NULL;
    if (root_package && !(rest.size && package && !exports_rule)) my resolved_package_directory = true;

    if (rest.size && package && !exports_rule) {
        // A nested package.json: `node_modules/foo/bar/package.json`
        bool found = (!directory_only && load_file(me, cstr_sv(&candidate), extensions, out)) ||
            load_directory(me, cstr_sv(&candidate), package, extensions, out);
        result = found ? SEARCH_FOUND : SEARCH_CONTINUE;
    } else if (exports_rule) {
        // `exports` blocks every other way into the package
        cstr subpath = rest.size ? cstr_d("./", rest) : cstr_lit(".");
        result = load_from_exports(me, root_package, cstr_sv(&package_dir), cstr_sv(&subpath), extensions, out);
        cstr_drop(&subpath);
        if (result == SEARCH_STOP) result = SEARCH_CONTINUE; // The walk up the directories goes on, like tsc's `?.value`
    } else {
        JsonValue const* version_paths = rest.size && root_package ? version_paths_of(root_package) : nullptr;
        bool versioned = false;
        if (version_paths) {
            result = load_using_version_paths(
                me, version_paths, cstr_sv(&package_dir), rest, extensions, load_as_file_or_directory, out);
            versioned = result == SEARCH_FOUND;
        }
        if (!versioned && directory_exists(me, parent_of(cstr_sv(&candidate)))) {
            bool found = (rest.size || !my esm_mode) && !directory_only && load_file(me, cstr_sv(&candidate), extensions, out);
            if (!found && directory_exists(me, cstr_sv(&candidate))) {
                found = load_directory(me, cstr_sv(&candidate), package, extensions, out);
                if (!found && !rest.size && package && my esm_mode) {
                    // No index probing in ESM mode, except that a package without `main` or `exports` is its index.js
                    cstr index = join(cstr_sv(&candidate), c_sv("index.js"));
                    found = load_file(me, cstr_sv(&index), extensions, out);
                    cstr_drop(&index);
                }
            }
            if (found) result = SEARCH_FOUND;
        }
    }
    cstr_drop(&candidate);
    cstr_drop(&package_dir);
    return result;
}

// One `node_modules`: the package, then its @types twin
static Search load_from_immediate_node_modules(
    State* me, csview directory, csview name, int extensions, bool types_only, Found* out
) {
    cstr node_modules = join(directory, c_sv("node_modules"));
    Search result = SEARCH_CONTINUE;
    if (directory_exists(me, cstr_sv(&node_modules))) {
        if (!types_only) result = load_from_node_modules_dir(me, cstr_sv(&node_modules), name, extensions, out);
        if (result == SEARCH_CONTINUE && extensions & EXT_DTS) {
            cstr types_dir = join(cstr_sv(&node_modules), c_sv("@types"));
            if (directory_exists(me, cstr_sv(&types_dir))) {
                cstr mangled = mangle_scoped_name(name);
                result = load_from_node_modules_dir(me, cstr_sv(&types_dir), cstr_sv(&mangled), EXT_DTS, out);
                cstr_drop(&mangled);
            }
            cstr_drop(&types_dir);
        }
    }
    cstr_drop(&node_modules);
    return result;
}

static Search walk_up_node_modules(
    State* me, csview from_dir, csview name, int extensions, bool types_only, Found* out
) {
    cstr dir = cstr_from_sv(from_dir);
    Search result = SEARCH_CONTINUE;
    while (true) {
        if (!csview_equals(basename_of(cstr_sv(&dir)), "node_modules")) {
            result = load_from_immediate_node_modules(me, cstr_sv(&dir), name, extensions, types_only, out);
            if (result != SEARCH_CONTINUE) break;
        }
        if (!go_up(&dir)) break;
    }
    cstr_drop(&dir);
    return result;
}

// tsc's loadModuleFromNearestNodeModulesDirectory(): TypeScript and declarations all the way up first, then the rest
static Search load_from_nearest_node_modules(State* me, csview from_dir, csview name, int extensions, Found* out) {
    int priority = extensions & (EXT_TS | EXT_DTS);
    int secondary = extensions & ~(EXT_TS | EXT_DTS);
    if (priority) {
        Search result = walk_up_node_modules(me, from_dir, name, priority, false, out);
        if (result != SEARCH_CONTINUE) return result;
    }
    if (secondary) return walk_up_node_modules(me, from_dir, name, secondary, false, out);
    return SEARCH_CONTINUE;
}

// ---- Type roots ----

static StrList effective_type_roots(ModuleResolver* me) {
    StrList ret = {0};
    CompilerOptions const* options = my options;
    if (options->type_roots_set) {
        for (isize i = 0; i < options->type_roots.count; i++) {
            cstr root = option_path(me, cstr_sv(&options->type_roots.items[i]));
            str_list_push(&ret, cstr_sv(&root));
            cstr_drop(&root);
        }
        return ret;
    }
    // Every node_modules/@types from the config's (else the current) directory upwards
    cstr dir = option_path(me, c_sv("."));
    while (true) {
        cstr candidate = join(cstr_sv(&dir), c_sv("node_modules/@types"));
        if (fs_is_directory(my fs, cstr_sv(&candidate))) str_list_push(&ret, cstr_sv(&candidate));
        cstr_drop(&candidate);
        if (!go_up(&dir)) break;
    }
    cstr_drop(&dir);
    return ret;
}

// tsc's getCandidateFromTypeRoot(): a scoped name is mangled in a `node_modules/@types` root
static cstr candidate_from_type_root(csview root, csview name) {
    bool at_types = csview_ends_with(root, "/node_modules/@types") || csview_ends_with(root, "/node_modules/@types/");
    if (!at_types) return join(root, name);
    cstr mangled = mangle_scoped_name(name);
    cstr ret = join(root, cstr_sv(&mangled));
    cstr_drop(&mangled);
    return ret;
}

// tsc's resolveFromTypeRoot(): the last resort of a node-style lookup of a module name
static bool load_from_type_roots(State* me, csview name, Found* out) {
    if (!my resolver->options->type_roots_set) return false;
    StrList roots = effective_type_roots(my resolver);
    bool found = false;
    for (isize i = 0; i < roots.count && !found; i++) {
        cstr candidate = candidate_from_type_root(cstr_sv(&roots.items[i]), name);
        if (directory_exists(me, cstr_sv(&roots.items[i]))) {
            found = load_file(me, cstr_sv(&candidate), EXT_DTS, out);
            if (!found && directory_exists(me, cstr_sv(&candidate))) {
                found = load_directory(me, cstr_sv(&candidate), package_json_of(me, cstr_sv(&candidate)), EXT_DTS, out);
            }
        }
        cstr_drop(&candidate);
    }
    str_list_drop(&roots);
    return found;
}

// ---- The resolvers ----

static Search resolve_non_relative(State* me, csview name, csview from_dir, int extensions, Found* out) {
    if (my use_imports && csview_starts_with(name, "#")) {
        return load_from_imports(me, name, from_dir, extensions, out);
    }
    if (my use_self_name) {
        Search result = load_from_self_name(me, name, from_dir, extensions, out);
        if (result != SEARCH_CONTINUE) return result;
    }
    if (csview_find_sv(name, c_sv(":")) != c_NPOS) return SEARCH_STOP; // "node:fs" is an ambient module or nothing
    Search result = load_from_nearest_node_modules(me, from_dir, name, extensions, out);
    if (result == SEARCH_CONTINUE && extensions & EXT_DTS && load_from_type_roots(me, name, out)) return SEARCH_FOUND;
    return result;
}

static Search node_try_resolve(State* me, csview name, int extensions, Found* out, bool* external) {
    Search result = load_using_options(me, name, extensions, load_as_file_or_directory, out);
    if (result != SEARCH_CONTINUE) {
        if (result == SEARCH_FOUND) *external = path_contains_node_modules(cstr_sv(&out->path));
        return result;
    }
    if (!module_name_is_relative(name)) {
        *external = !csview_starts_with(name, "#");
        result = resolve_non_relative(me, name, my containing_dir, extensions, out);
        if (result == SEARCH_FOUND && !path_contains_node_modules(cstr_sv(&out->path))) *external = false;
        return result;
    }
    cstr candidate = join_rooted(me, my containing_dir, name);
    // "." and "./" mean the directory, never a file next to it
    bool is_directory = ends_with(name, "/") || csview_equals(name, ".") || csview_equals(name, "..") ||
        ends_with(name, "/.") || ends_with(name, "/..");
    if (is_directory) cstr_append_lit(&candidate, "/");
    bool found = load_as_file_or_directory(me, cstr_sv(&candidate), extensions, out);
    cstr_drop(&candidate);
    // (tsgo's resolveNodeLikeWorker: whether what was found is in a node_modules, for a relative name too: a package's
    // own `./types` is an external library import)
    *external = found && path_contains_node_modules(cstr_sv(&out->path));
    return found ? SEARCH_FOUND : SEARCH_CONTINUE;
}

static void add_condition(State* me, const char* condition) {
    if (my condition_count < (isize)c_arraylen(my conditions)) my conditions[my condition_count++] = condition;
}

// The working directory as of the first resolution: whoever made the resolver may still have been setting up
static void resolver_init_cwd(ModuleResolver* resolver) {
    if (atomic_load_explicit(&resolver->cwd_known, memory_order_acquire)) return;
    pthread_mutex_lock(&resolver->cwd_lock);
    if (!cstr_size(&resolver->cwd)) {
        try (fs_get_cwd(resolver->fs, &resolver->cwd)) or_panic;
        resolver->cwd_normalized = cstr_size(&resolver->cwd) && cstr_str(&resolver->cwd)[0] == '/' &&
            path_is_normalized(cstr_sv(&resolver->cwd));
    }
    atomic_store_explicit(&resolver->cwd_known, true, memory_order_release);
    pthread_mutex_unlock(&resolver->cwd_lock);
}

static State state_make(ModuleResolver* resolver, ResolutionMode mode) {
    resolver_init_cwd(resolver);
    CompilerOptions const* options = resolver->options;
    State state = { .resolver = resolver, .kind = options->module_resolution };
    bool node_next = state.kind == MODULE_RESOLUTION_NODE16 || state.kind == MODULE_RESOLUTION_NODENEXT;
    bool bundler = state.kind == MODULE_RESOLUTION_BUNDLER;
    state.esm_mode = node_next && mode == RESOLUTION_MODE_ESM;
    state.use_exports = options->resolve_package_json_exports && (node_next || bundler);
    state.use_imports = options->resolve_package_json_imports && (node_next || bundler);
    state.use_self_name = node_next || bundler;
    bool import = mode == RESOLUTION_MODE_ESM || (bundler && mode != RESOLUTION_MODE_REQUIRE_SYNTAX);
    add_condition(&state, import ? "import" : "require");
    add_condition(&state, "types");
    if (!bundler) add_condition(&state, "node");
    for (isize i = 0; i < options->custom_conditions.count; i++) {
        add_condition(&state, cstr_str(&options->custom_conditions.items[i]));
    }
    return state;
}

static cstr absolute_containing_dir(ModuleResolver* me, csview containing_file) {
    cstr absolute = absolute_path_fast(cstr_sv(&my cwd), containing_file);
    cstr ret = cstr_from_sv(parent_of(cstr_sv(&absolute)));
    cstr_drop(&absolute);
    return ret;
}

// module_resolver_resolve()'s cache key: the containing directory (absolute), a line break (no path has one), the mode
// and the specifier. Built in place for the usual normalized containing file
static cstr resolution_key(ModuleResolver* me, csview containing_file, ResolutionMode mode, csview specifier) {
    if (my cwd_normalized && path_is_normalized(containing_file)) {
        isize slash = containing_file.size;
        while (slash > 0 && containing_file.buf[slash - 1] != '/') slash--;
        bool rooted = containing_file.buf[0] == '/';
        csview cwd = cstr_sv(&my cwd);
        // (The directory: the file's up to its last slash, joined to the working directory unless rooted)
        csview own = csview_slice(containing_file, 0, slash ? slash - 1 : 0);
        if (rooted && !own.size) own = c_sv("/");
        csview base = rooted ? c_sv("") : cwd;
        bool separator = !rooted && own.size && cwd.size > 1;
        cstr ret = cstr_with_capacity(base.size + separator + own.size + 2 + specifier.size);
        cstr_append_sv(&ret, base);
        if (separator) cstr_append_n(&ret, "/", 1);
        cstr_append_sv(&ret, own);
        char middle[2] = { '\n', (char)('0' + mode) };
        cstr_append_n(&ret, middle, 2);
        cstr_append_sv(&ret, specifier);
        return ret;
    }
    cstr dir = absolute_containing_dir(me, containing_file);
    cstr ret = cstr_d(dir, "\n", (char)('0' + mode), specifier);
    cstr_drop(&dir);
    return ret;
}

static ResolvedModule resolve_uncached(ModuleResolver* me, csview specifier, csview containing_dir, ResolutionMode mode) {
    ResolvedModule ret = {0};
    State state = state_make(me, mode);
    state.containing_dir = containing_dir;

    Found found = {0};
    bool external = false;
    Search result;
    int secondary = EXT_JS | (my options->resolve_json_module ? EXT_JSON : 0);
    if (state.use_exports && !module_name_is_relative(specifier)) {
        // All extensions at once (`./dir` is dir.js before dir/index.ts), except that
        // HACK: a package name with export maps in play is resolved in two passes (the TypeScript extensions first: a
        // `types` condition anywhere beats any JavaScript), which is how tsc ends up in its selfNameModuleAugmentation
        // behaviour (microsoft/TypeScript#50762) by other means
        result = node_try_resolve(&state, specifier, EXT_TS | EXT_DTS, &found, &external);
        if (result != SEARCH_FOUND) {
            Search second = node_try_resolve(&state, specifier, secondary, &found, &external);
            if (second == SEARCH_FOUND) result = second;
        }
    } else {
        result = node_try_resolve(&state, specifier, EXT_TS | EXT_DTS | secondary, &found, &external);
    }

    // tsc's alternateResult: a bare name that reached a package's directory but found no declarations
    bool wanted_types_got_js = result == SEARCH_FOUND && (found.kind == RESOLVED_JS || found.kind == RESOLVED_JSON);
    if (state.resolved_package_directory && !module_name_is_relative(specifier)) {
        bool import_condition = false;
        for (isize i = 0; i < state.condition_count; i++) import_condition |= !strcmp(state.conditions[i], "import");
        State retry = {0};
        bool again = false;
        if (result == SEARCH_FOUND && external && wanted_types_got_js && state.use_exports && import_condition) {
            // Without `exports` (NodeResolutionFeatures.Exports off)
            retry = state_make(me, mode);
            retry.use_exports = false;
            again = true;
        }
        if (again) {
            retry.containing_dir = containing_dir;
            Found alternate = {0};
            bool alternate_external = false;
            if (node_try_resolve(&retry, specifier, EXT_TS | EXT_DTS, &alternate, &alternate_external) == SEARCH_FOUND &&
                alternate_external
            ) {
                ret.alternate_result = alternate.path;
                alternate.path = cstr_null;
            }
            cstr_drop(&alternate.path);
            cstr_drop(&retry.ambiguous_root_entry);
            cstr_drop(&retry.ambiguous_root_package_json);
        }
    }

    if (result == SEARCH_FOUND) {
        ret.kind = found.kind;
        ret.path = found.path;
        ret.is_external_library = external;
        ret.used_ts_extension = found.used_ts_extension;
        ret.ts_extension_from_star = found.ts_extension_from_star;
        ret.arbitrary_extension = found.arbitrary_extension;
        ret.package_id = package_id_of(&state, cstr_sv(&ret.path), cstr_sv(&found.package_dir), &ret.package_name);
        // (Not for a relative name, external or not: tsgo's relative branch is createResolvedModule, without
        // createResolvedModuleHandlingSymlink's realpath)
        if (!module_name_is_relative(specifier)) apply_realpath(me, &ret);
    } else {
        ret.json_without_option = state.json_without_option;
        bool dot_relative = specifier.buf[0] == '.' && module_name_is_relative(specifier);
        // (tsc's hasExtension(): "./" and ".." have one)
        bool has_extension = csview_find_sv(basename_of(specifier), c_sv(".")) != c_NPOS;
        if (state.esm_mode && dot_relative && !has_extension) {
            // tsc's checker: the extension to write is the output's of whichever file is next to the name
            ret.needs_extension = true;
            static const char* const suggestions[][2] = {
                {".mts", ".mjs"}, {".ts", ".js"}, {".cts", ".cjs"}, {".mjs", ".mjs"}, {".js", ".js"}, {".cjs", ".cjs"},
                {".tsx", nullptr}, {".jsx", ".jsx"}, {".json", ".json"},
            };
            cstr stem = join(state.containing_dir, specifier);
            for (isize i = 0; i < (isize)c_arraylen(suggestions); i++) {
                cstr candidate = cstr_d(stem, suggestions[i][0]);
                bool exists = file_exists(&state, cstr_sv(&candidate));
                cstr_drop(&candidate);
                if (!exists) continue;
                const char* suggestion = suggestions[i][1] ? suggestions[i][1] :
                    my options->jsx == JSX_PRESERVE ? ".jsx" : ".js";
                ret.suggested_extension = cstr_from(suggestion);
                break;
            }
            cstr_drop(&stem);
        }
    }
    ret.ambiguous_root_entry = state.ambiguous_root_entry;
    ret.ambiguous_root_package_json = state.ambiguous_root_package_json;
    ret.ambiguous_root_in_imports = state.ambiguous_root_in_imports;
    cstr_drop(&found.package_dir);
    return ret;
}

// A `./` or `../` name that means a file (not `./dir/`, `..`): what it resolves to only depends on the path it makes
// joined to the directory, and the mode (paths aren't consulted, rootDirs and the extension suggestion go by that
// path). module_resolver_resolve() caches those by that path too: a file imported from many directories is looked
// for once (vscode's lifecycle.ts, from a thousand)
static bool resolves_by_joined_path(csview specifier) {
    if (!csview_starts_with(specifier, "./") && !csview_starts_with(specifier, "../")) return false;
    return !ends_with(specifier, "/") && !ends_with(specifier, "/.") && !ends_with(specifier, "/..");
}

// Under the shard's lock: key means value from now on unless it already meant something, which it keeps (two threads
// both resolved it, and agree). key is taken
static ResolvedModule const* shard_insert(ResolverShard* shard, cstr key, ResolvedModule const* value) {
    auto inserted = hmap_resolved_module_ptr_insert(&shard->resolutions, key, value);
    return inserted.ref->second;
}

// Under the shard's lock: box (made before taking it) is the shard's to free from now on, for shard_insert()
static ResolvedModule const* shard_box(ResolverShard* shard, ResolvedModule* box) {
    if (shard->box_count == shard->box_cap) {
        shard->box_cap = shard->box_cap ? shard->box_cap * 2 : 16;
        shard->boxes = realloc(shard->boxes, sizeof(ResolvedModule*) * (size_t)shard->box_cap);
        panic_if(!shard->boxes, "out of memory");
    }
    shard->boxes[shard->box_count++] = box;
    return box;
}

ResolvedModule const* module_resolver_resolve_shared(
    ModuleResolver* me,
    csview specifier,
    csview containing_file,
    ResolutionMode mode
) {
    static ResolvedModule const nothing = {0};
    if (!specifier.size) return &nothing;
    resolver_init_cwd(me);
    cstr key = resolution_key(me, containing_file, mode, specifier);
    ResolverShard* shard = resolver_shard(me, cstr_sv(&key));
    pthread_rwlock_rdlock(&shard->lock);
    auto cached = hmap_resolved_module_ptr_get(&shard->resolutions, cstr_sv(&key));
    ResolvedModule const* ret = cached ? cached->second : nullptr;
    pthread_rwlock_unlock(&shard->lock);
    if (ret) {
        cstr_drop(&key);
        return ret;
    }
    // (Paths are C strings further down: the directory is its own string)
    cstr dir = cstr_from_sv(csview_slice(cstr_sv(&key), 0, csview_find(cstr_sv(&key), "\n")));
    // The joined path's key: the path, a carriage return (no directory key has one before its line break), the mode
    cstr joined_key = cstr_null;
    ResolverShard* joined_shard = nullptr;
    if (resolves_by_joined_path(specifier)) {
        cstr joined = join(cstr_sv(&dir), specifier);
        joined_key = cstr_d(joined, "\r", (char)('0' + mode));
        cstr_drop(&joined);
        joined_shard = resolver_shard(me, cstr_sv(&joined_key));
        pthread_rwlock_rdlock(&joined_shard->lock);
        auto by_path = hmap_resolved_module_ptr_get(&joined_shard->resolutions, cstr_sv(&joined_key));
        ret = by_path ? by_path->second : nullptr;
        pthread_rwlock_unlock(&joined_shard->lock);
        if (ret) {
            pthread_rwlock_wrlock(&shard->lock);
            ret = shard_insert(shard, key, ret);
            pthread_rwlock_unlock(&shard->lock);
            c_drop(cstr, &dir, &joined_key);
            return ret;
        }
    }
    ResolvedModule* box = c_new(ResolvedModule, resolve_uncached(me, specifier, cstr_sv(&dir), mode));
    pthread_rwlock_wrlock(&shard->lock);
    ret = shard_insert(shard, key, shard_box(shard, box));
    pthread_rwlock_unlock(&shard->lock);
    if (joined_shard) {
        pthread_rwlock_wrlock(&joined_shard->lock);
        shard_insert(joined_shard, joined_key, ret);
        pthread_rwlock_unlock(&joined_shard->lock);
    }
    cstr_drop(&dir);
    return ret;
}

ResolvedModule module_resolver_resolve(
    ModuleResolver* me,
    csview specifier,
    csview containing_file,
    ResolutionMode mode
) {
    return resolved_module_clone(module_resolver_resolve_shared(me, specifier, containing_file, mode));
}

ResolvedModule module_resolver_resolve_package_directory(ModuleResolver* me, csview name, csview containing_file) {
    ResolvedModule ret = {0};
    if (!name.size || module_name_is_relative(name)) return ret;
    State state = state_make(me, RESOLUTION_MODE_COMMONJS);
    cstr dir = absolute_containing_dir(me, containing_file);
    cstr mangled = mangle_scoped_name(name);
    do {
        csview here = cstr_sv(&dir);
        isize base = here.size;
        while (base > 0 && here.buf[base - 1] != '/') base--;
        if (csview_equals(csview_slice(here, base, here.size), "node_modules")) continue;
        cstr node_modules = join(here, c_sv("node_modules"));
        if (directory_exists(&state, cstr_sv(&node_modules))) {
            cstr candidate = join(cstr_sv(&node_modules), name);
            if (!directory_exists(&state, cstr_sv(&candidate))) {
                cstr_drop(&candidate);
                cstr types = join(cstr_sv(&node_modules), c_sv("@types"));
                candidate = join(cstr_sv(&types), cstr_sv(&mangled));
                cstr_drop(&types);
            }
            if (directory_exists(&state, cstr_sv(&candidate))) {
                cstr_take(&ret.path, candidate);
                ret.kind = RESOLVED_DTS;
                ret.is_external_library = true;
                follow_symlinks(me, &ret);
            } else {
                cstr_drop(&candidate);
            }
        }
        cstr_drop(&node_modules);
    } while (!cstr_size(&ret.path) && go_up(&dir));
    c_drop(cstr, &dir, &mangled);
    return ret;
}

ResolvedModule module_resolver_resolve_type_reference(
    ModuleResolver* me, csview name, csview containing_file, ResolutionMode const* mode
) {
    ResolvedModule ret = {0};
    if (!name.size) return ret;
    // Without a mode, bundler reads `exports` with `import`, the others with `require` (tsc's getConditions)
    State state = state_make(me, !mode ? RESOLUTION_MODE_COMMONJS :
        *mode == RESOLUTION_MODE_ESM ? RESOLUTION_MODE_ESM : RESOLUTION_MODE_REQUIRE_SYNTAX);
    state.in_type_reference = true;
    // Type reference lookup reads exports whatever the options say when there is a mode (tsc's
    // resolveTypeReferenceDirective: NodeResolutionFeatures.AllFeatures)
    if (mode) state.use_exports = state.use_imports = state.use_self_name = true;
    cstr containing_dir = containing_file.size ? absolute_containing_dir(me, containing_file) : option_path(me, c_sv("."));
    state.containing_dir = cstr_sv(&containing_dir);

    Found found = {0};
    bool resolved = false;
    // Primary: the type roots
    StrList roots = effective_type_roots(me);
    for (isize i = 0; i < roots.count && !resolved; i++) {
        cstr candidate = candidate_from_type_root(cstr_sv(&roots.items[i]), name);
        if (directory_exists(&state, cstr_sv(&roots.items[i]))) {
            // Custom type roots resolve as file or directory, like modules
            // (Not a name ending in '/': a directory only, tsgo's resolutionCandidate.directoryOnly)
            bool directory_only = name.size && name.buf[name.size - 1] == '/';
            if (my options->type_roots_set && !directory_only) resolved = load_file(&state, cstr_sv(&candidate), EXT_DTS, &found);
            if (!resolved && directory_exists(&state, cstr_sv(&candidate))) {
                resolved = load_directory(&state, cstr_sv(&candidate), package_json_of(&state, cstr_sv(&candidate)), EXT_DTS, &found);
            }
        }
        cstr_drop(&candidate);
    }
    str_list_drop(&roots);
    // Secondary: node_modules from the containing file upwards. Not for the automatic ones when there are custom type
    // roots, which are then the only place those come from
    if (!resolved && !(my options->type_roots_set && !containing_file.size)) {
        if (module_name_is_relative(name)) {
            cstr candidate = join(state.containing_dir, name);
            resolved = load_as_file_or_directory(&state, cstr_sv(&candidate), EXT_DTS, &found);
            cstr_drop(&candidate);
        } else {
            resolved = load_from_nearest_node_modules(&state, state.containing_dir, name, EXT_DTS, &found) == SEARCH_FOUND;
        }
    }
    if (resolved) {
        ret.kind = found.kind;
        ret.path = found.path;
        // A type root's directive is followed through symlinks too (tsc realpaths every resolved directive: a
        // node_modules/@types that links to a workspace's types/ loads each file once)
        ret.is_external_library = path_contains_node_modules(cstr_sv(&ret.path));
        follow_symlinks(me, &ret);
    }
    cstr_drop(&found.package_dir);
    cstr_drop(&containing_dir);
    return ret;
}

static StrList module_resolver_wildcard_types(ModuleResolver* me);

StrList module_resolver_automatic_types(ModuleResolver* me) {
    StrList ret = {0};
    // TypeScript 6.0: `types` defaults to [], "*" stands for every package of the type roots (in its position)
    bool wildcard = false;
    for (isize i = 0; i < my options->types.count; i++) wildcard |= cstr_equals(&my options->types.items[i], "*");
    if (!wildcard) {
        for (isize i = 0; i < my options->types.count; i++) str_list_push(&ret, cstr_sv(&my options->types.items[i]));
        return ret;
    }
    StrList wild = {0};
    for (isize i = 0; i < my options->types.count; i++) {
        if (!cstr_equals(&my options->types.items[i], "*")) {
            bool seen = false;
            for (isize j = 0; j < ret.count && !seen; j++) seen = cstr_equals(&ret.items[j], cstr_str(&my options->types.items[i]));
            if (!seen) str_list_push(&ret, cstr_sv(&my options->types.items[i]));
            continue;
        }
        wild = module_resolver_wildcard_types(me);
        for (isize k = 0; k < wild.count; k++) {
            bool seen = false;
            for (isize j = 0; j < ret.count && !seen; j++) seen = cstr_equals(&ret.items[j], cstr_str(&wild.items[k]));
            if (!seen) str_list_push(&ret, cstr_sv(&wild.items[k]));
        }
        str_list_drop(&wild);
    }
    return ret;
}

static StrList module_resolver_wildcard_types(ModuleResolver* me) {
    StrList ret = {0};
    State state = state_make(me, RESOLUTION_MODE_COMMONJS);
    StrList roots = effective_type_roots(me);
    for (isize i = 0; i < roots.count; i++) {
        if (!fs_is_directory(my fs, cstr_sv(&roots.items[i]))) continue;
        for_each (item, fs_listdir_sorted, my fs, cstr_sv(&roots.items[i])) {
            if (!item.name.size || item.name.buf[0] == '.') continue;
            if (!fs_is_directory(my fs, item.path)) continue;
            cstr absolute = absolute_path_of(cstr_sv(&my cwd), item.path);
            PackageJson const* package = package_json_of(&state, cstr_sv(&absolute));
            cstr_drop(&absolute);
            // A package without a package.json counts, one that says `"typings": null` opts out
            if (package) {
                JsonValue const* typings = json_get(&package->json, "typings");
                if (typings && typings->tag == JSON_NULL) continue;
            }
            bool seen = false;
            for (isize j = 0; j < ret.count && !seen; j++) seen = cstr_equals_sv(&ret.items[j], item.name);
            if (!seen) str_list_push(&ret, item.name);
        }
    }
    str_list_drop(&roots);
    return ret;
}

FileFormat module_resolver_file_format(ModuleResolver* me, csview file) {
    FileFormat ret = { .mode = RESOLUTION_MODE_COMMONJS };
    State state = state_make(me, RESOLUTION_MODE_COMMONJS);
    cstr dir = absolute_containing_dir(me, file);
    cstr scope_dir = cstr_null;
    PackageJson const* scope = package_scope_of(&state, cstr_sv(&dir), &scope_dir);
    if (scope) {
        ret.package_json = cstr_d(scope_dir, cstr_size(&scope_dir) > 1 ? "/" : "", "package.json");
        csview type = json_get_string(&scope->json, "type");
        ret.package_has_type = type.size > 0;
        ret.package_type_decides = csview_equals(type, "module") || csview_equals(type, "commonjs");
        if (csview_equals(type, "module")) ret.mode = RESOLUTION_MODE_ESM;
    }
    if (ends_with(file, ".mts") || ends_with(file, ".mjs")) ret.mode = RESOLUTION_MODE_ESM;
    if (ends_with(file, ".cts") || ends_with(file, ".cjs")) ret.mode = RESOLUTION_MODE_COMMONJS;
    cstr_drop(&scope_dir);
    cstr_drop(&dir);
    return ret;
}

// ---- What module_specifiers.c asks ----

JsonValue const* module_resolver_package_json(ModuleResolver* me, csview directory) {
    State state = { .resolver = me };
    PackageJson const* package = package_json_of(&state, directory);
    return package ? &package->json : nullptr;
}

bool module_resolver_file_exists(ModuleResolver* me, csview path) {
    State state = { .resolver = me };
    return file_exists(&state, path);
}

cstr module_resolver_option_path(ModuleResolver* me, csview path) {
    resolver_init_cwd(me);
    return option_path(me, path);
}

csview module_resolver_cwd(ModuleResolver* me) {
    resolver_init_cwd(me);
    return cstr_sv(&my cwd);
}

JsonValue const* module_resolver_types_versions(JsonValue const* package_json) {
    PackageJson package = { .exists = true, .json = *package_json };
    return version_paths_of(&package);
}

bool module_resolver_version_in_range(csview range) {
    return version_in_range(range);
}
