#include "program.h"
#include "string_helpers.h"
#include "unused.h"
#include "ty/declaration_diagnostics.h"
#include "import_attributes.h"
#include "parser.h"
#include "block.h"
#include <cleat/filesystem.h>
#include "error.h"
#include "statement.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "interface_decl.h"
#include "port_statement.h"
#include "source_file.h"
#include "ty/ty.h"
#include "module_resolution.h"
#include "json.h"
#include "load_pool.h"
#include "project.h"
#include "symbol_chain.h"
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

#define g_class_key SourceFile, source_file
#define g_no_clone
#include <cleat/generic/box.h>

// Keep source files in a box so new ones can be added without invalidating old pointers
#define g_pro_key SvStr, svstr
#define g_class_val BoxSourceFile, box_source_file
#define g_no_clone
#include <cleat/generic/hmap.h>

typedef SourceFile* SourceFilePtr;
#define g_key SourceFilePtr, source_file_ptr
#include <cleat/generic/vec.h>

#define g_pro_key SvStr, svstr
#define g_val SourceFilePtr, source_file_ptr
#include <cleat/generic/hmap.h>

typedef uint8_t PackageFlag; // Program.packages: whether a declaration file of the package was found
#define g_pro_key SvStr, svstr
#define g_val PackageFlag, package_flag
#include <cleat/generic/hmap.h>

// Where the standard library is unless program_set_lib_dir() says otherwise (the repo root is the working directory)
static const csview default_lib_dir = c_sv_const_init("thirdparty/lib");

typedef enum {
    PROGRAM_PHASE_LOADING = 0, // Files may be loaded
    PROGRAM_PHASE_RESOLVING_NAMES, // Names currently being resolved
    PROGRAM_PHASE_NAMES_RESOLVED, // Resolving names is done
    PROGRAM_PHASE_TYPECHECKING, // Typechecking has been started
    PROGRAM_PHASE_TYPECHECKED, // Typechecking has completed
} ProgramPhase;

static const char* program_phase_get_str(ProgramPhase phase) {
    switch (phase) {
        case PROGRAM_PHASE_LOADING: return "loading";
        case PROGRAM_PHASE_RESOLVING_NAMES: return "resolving names";
        case PROGRAM_PHASE_NAMES_RESOLVED: return "names resolved";
        case PROGRAM_PHASE_TYPECHECKING: return "typechecking";
        case PROGRAM_PHASE_TYPECHECKED: return "typecheck done";
    }
    impossible;
}

#define ASSERT_PHASE(expected) \
    panic_if( \
        my phase != expected, \
        "expected program phase to be ", program_phase_get_str(expected), \
        ", was actually ", program_phase_get_str(my phase) \
    ) \

#define CHANGE_PHASE(from, to) ASSERT_PHASE(from); my phase = to;

typedef struct Program {
    ProgramPhase phase;
    bool loading_stdlib; // If we're loading standard library files instead of normal files
    bool follow_lib_references; // program_follow_lib_references()
    cstr lib_dir; // cstr_null: default_lib_dir
    ErrorCtx* errs; // unowned
    Fs const* fs;
    cstr cwd; // program_cwd()
    Binder* binder;
    ModuleResolver* resolver; // Made on first use, the options are final by then
    // program_set_references(): the referenced projects, and a resolver with each one's options for its files
    ProjectRedirects const* redirects;
    ModuleResolver** reference_resolvers;
    u32 reference_count;
    RealpathFn realpath; // program_set_realpath()
    void* realpath_ctx;
    SymlinkCache* symlinks; // What resolutions learned about symlinked directories, for program_module_specifier()
    u32 serial; // program_serial()
    HMapBoxSourceFile source_files_map;
    VecSourceFilePtr source_files_ordered;
    HMapSourceFilePtr files_by_package_id; // ResolvedModule.package_id -> the first file found with it
    // tsc's getPackagesMap: the package name of every module resolution that found a file of a package, whether one of
    // them was a declaration file
    HMapPackageFlag packages;
    Checker* checker; // Created by program_make_current(), checks the first share of the files
    Checker** checkers; // malloc'd, checker_count of them once typechecked: checker and the others
    u32 checker_count;
    u32* file_shares; // malloc'd: the checker of each checked file of source_files_ordered (see share_of())
    u32 requested_checkers; // program_set_checkers()
    // program_declaration_diagnostics_cli(): earlier phases reported something
    bool declaration_diagnostics_cli, declaration_diagnostics_earlier;
    ErrorCtx* declaration_errs; // One per checker while typechecking: the declaration diagnostics of its files
    u32 threads; // program_set_threads(), 0: 1
    LoadPool* pool; // While loading with threads: parses files ahead of the loader
    i64 (*file_size)(void* ctx, csview path); // program_set_file_size()
    void* file_size_ctx;
    CompilerOptions options;
    // program_set_config_file(): the config and its JSON (with offsets), null / a null value without one
    SourceFile* config_source;
    JsonValue config_json;
} Program;

static void program_add_builtins(Program* me) {
#define GLOBAL_VALUE(global_name, global_value) {\
    Symbol* symbol = nullptr; \
    scope_init_declaration(my binder->global_scope, &symbol, (DeclProps) { \
        .name = sv_make_span(c_sv(global_name)), \
        .kind = SYMBOL_DECL_BUILTIN, \
        .node = &global_value, \
        .function_scoped = true, \
    } ); \
}

#define GLOBAL_TYPE(global_name, global_type) {\
    Symbol* symbol = nullptr; \
    scope_init_declaration(my binder->global_scope, &symbol, (DeclProps) { \
        .name = sv_make_span(c_sv(global_name)), \
        .kind = SYMBOL_DECL_BUILTIN, \
        .node = &global_type, \
    } ); \
}

    binder_declare_global_this(my binder);

    GLOBAL_TYPE("any", type_any);
    GLOBAL_TYPE("unknown", type_unknown);
    GLOBAL_TYPE("never", type_never);
    GLOBAL_TYPE("void", type_void);
    GLOBAL_TYPE("object", type_object);
    GLOBAL_TYPE("boolean", type_boolean);
    GLOBAL_TYPE("number", type_number);
    GLOBAL_TYPE("string", type_string);
    // (Named so that no lookup finds it: a type alias body `intrinsic` is it, see IdentifierType.intrinsic_keyword)
    GLOBAL_TYPE("#intrinsic", type_any);
}

static SourceFile* program_add_source_idempotent(Program* me, csview path);

Program* program_new(ErrorCtx* errs, Fs const* fs) {
    check(errs);
    check(fs);
    Program* ret = c_new(Program, {
        .errs = errs,
        .fs = fs,
        .binder = binder_new(errs),
        .options = compiler_options_init(),
        .symlinks = symlink_cache_new(),
    });
    static atomic_uint serials;
    ret->serial = atomic_fetch_add(&serials, 1) + 1;
    program_add_builtins(ret);
    return ret;
}

// Source files to free, shared by the threads of drop_source_files()
typedef struct FileDrops {
    SourceFile** files;
    u32 count;
    atomic_uint next;
} FileDrops;

static void* drop_source_files_thread(void* arg) {
    FileDrops* drops = arg;
    for (u32 i; (i = atomic_fetch_add(&drops->next, 1)) < drops->count;) {
        source_file_drop(drops->files[i]);
        *drops->files[i] = (SourceFile){0};
    }
    return nullptr;
}

// Frees what the source files hold (their ASTs, node by node) on the program's threads: a fifth of a big program's
// time on one (-b frees a program per project). Each file's drop only touches its own nodes. The emptied files are
// freed with the map
static void drop_source_files(Program* me) {
    u32 threads = my threads;
    FileDrops drops = { .files = c_new_n(SourceFile*, hmap_box_source_file_size(&my source_files_map) + 1) };
    atomic_init(&drops.next, 0);
    for_each (i, hmap_box_source_file, &my source_files_map) drops.files[drops.count++] = i->second.get;
    if (threads > drops.count / 16) threads = drops.count / 16;
    // (Dropping recurses per nesting level like the checker does: its stack size)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, worker_stack_size());
    pthread_t* workers = c_new_n(pthread_t, threads + 1);
    u32 started = 0;
    while (started + 1 < threads && !pthread_create(&workers[started], &attr, drop_source_files_thread, &drops)) started++;
    pthread_attr_destroy(&attr);
    drop_source_files_thread(&drops);
    for (u32 t = 0; t < started; t++) pthread_join(workers[t], nullptr);
    free(workers);
    free(drops.files);
}

static void program_drop(Program* me) {
    load_pool_destroy(my pool);
    // First, its memos point into the AST and the scopes
    for (u32 i = 1; i < my checker_count; i++) checker_drop(my checkers[i]);
    free(my file_shares);
    free(my checkers);
    checker_drop(my checker);
    drop_source_files(me);
    hmap_box_source_file_drop(&my source_files_map);
    hmap_source_file_ptr_drop(&my files_by_package_id);
    hmap_package_flag_drop(&my packages);
    vec_source_file_ptr_drop(&my source_files_ordered);
    module_resolver_destroy(my resolver);
    symlink_cache_destroy(my symlinks);
    for (u32 i = 0; i < my reference_count; i++) module_resolver_destroy(my reference_resolvers[i]);
    free(my reference_resolvers);
    cstr_drop(&my lib_dir);
    cstr_drop(&my cwd);
    compiler_options_drop(&my options);
    if (my config_source) {
        source_file_drop(my config_source);
        free(my config_source);
    }
    json_drop(&my config_json);
    binder_destroy(my binder); // After the AST: type nodes in it may own things symbols point to
}

SourceFile const* program_set_config_file(Program* me, csview path, bool report_syntax) {
    cstr contents = cstr_null;
    try (fs_read_file(my fs, path, &contents)) if_err (e) {
        ignore e;
        current_error_destroy();
        return nullptr;
    }
    text_decode(&contents);
    json_drop(&my config_json);
    if (my config_source) {
        source_file_drop(my config_source);
        free(my config_source);
    }
    my config_source = c_new(SourceFile, source_file_make(cstr_from_sv(path), contents));
    my config_source->is_json = true;
    my config_source->is_config = true;
    my config_source->unchecked = true;
    // tsc's parseJsonText: the config's syntax errors (TS1005 & co) are its parsing diagnostics, reported whatever
    // else is wrong (they stop nothing); what json_parse() recovers of a broken config is still what's read.
    // Without report_syntax they're dropped, like tsc's test harness does (it ignores the ParsedCommandLine's errors,
    // and the program only reports the config's program diagnostics)
    bool own_options = !compiler_options_private;
    if (own_options) set_compiler_options(&my options);
    ErrorCtx dropped = error_ctx_scratch(my errs);
    parse_file(my config_source, report_syntax ? my errs : &dropped);
    error_ctx_drop(&dropped);
    if (own_options) clear_compiler_options();
    if (!json_parse(cstr_sv(&my config_source->contents), &my config_json)) my config_json = (JsonValue){0};
    return my config_source;
}

static Span config_span(Program const* me, u32 start, u32 end) {
    char const* text = cstr_str(&my config_source->contents);
    return span_make(my config_source, text + start, text + end);
}

// The `compilerOptions` member of the config (tsc's getCompilerOptionsPropertySyntax), null if there's none
static JsonMember const* config_compiler_options(Program const* me) {
    if (!my config_source || my config_json.tag != JSON_OBJECT) return nullptr;
    for (isize i = 0; i < my config_json.count; i++) {
        if (cstr_equals(&my config_json.members[i].key, "compilerOptions")) return &my config_json.members[i];
    }
    return nullptr;
}

// Where tsc's createDiagnosticForOption puts a diagnostic about an option: on the key (or the value) of every member
// named name or name2 of the config's compilerOptions (forEachPropertyAssignment), else on the `compilerOptions` key
// (createCompilerOptionsDiagnostic), else nowhere (a global diagnostic). At least one site, at most 4
static u32 option_sites(Program const* me, char const* name, char const* name2, bool on_key, Span sites[4]) {
    JsonMember const* options = config_compiler_options(me);
    if (!options) {
        sites[0] = span_null;
        return 1;
    }
    u32 count = 0;
    if (options->value.tag == JSON_OBJECT) {
        for (isize i = 0; i < options->value.count && count < 4; i++) {
            JsonMember const* member = &options->value.members[i];
            if (!cstr_equals(&member->key, name) && !(name2 && cstr_equals(&member->key, name2))) continue;
            sites[count++] = on_key ? config_span(me, member->key_start, member->key_end) :
                config_span(me, member->value.start, member->value.end);
        }
    }
    if (!count) sites[count++] = config_span(me, options->key_start, options->key_end);
    return count;
}

// A diagnostic at a site of option_sites(), without a file if it has none
static Error* option_error_s(Program* me, Span site, unsigned code, cstr message) {
    if (span_valid(site)) return error_ctx_error_s(my errs, site, code, message);
    return error_ctx_global_error_s(my errs, code, message);
}
static csview program_cwd(Program const* me);

// JSON.stringify of an array of strings as written in the config, `fallback` without one
static cstr config_string_list(JsonValue const* list, char const* fallback) {
    if (!list || list->tag != JSON_ARRAY) return cstr_from(fallback);
    cstr ret = cstr_lit("[");
    for (isize i = 0; i < list->count; i++) {
        if (list->items[i].tag != JSON_STRING) continue;
        if (cstr_size(&ret) > 1) cstr_append_lit(&ret, ",");
        cstr_append_lit(&ret, "\"");
        cstr_append_sv(&ret, cstr_sv(&list->items[i].string));
        cstr_append_lit(&ret, "\"");
    }
    cstr_append_lit(&ret, "]");
    return ret;
}

bool program_report_no_inputs(Program* me) {
    if (!my config_source || my config_json.tag != JSON_OBJECT) return false;
    JsonValue const* files = json_get(&my config_json, "files");
    JsonValue const* references = json_get(&my config_json, "references");
    cstr config = absolute_path_of(program_cwd(me), cstr_sv(&my config_source->canonical_path));
    bool reported = false;
    // (tsc's getConfigFileSpecs and canJsonReportNoInputFiles)
    if (files) {
        bool no_references = !references || (references->tag == JSON_ARRAY && !references->count);
        if (files->tag == JSON_ARRAY && !files->count && no_references && !json_get(&my config_json, "extends")) {
            program_config_error(me, files->start, files->end, 18002,
                cstr_d("The 'files' list in config file '", config, "' is empty."));
            reported = true;
        }
    } else if (!references) {
        cstr include = config_string_list(json_get(&my config_json, "include"), "[\"**/*\"]");
        cstr exclude = config_string_list(json_get(&my config_json, "exclude"), "[]");
        error_ctx_global_error_d(my errs, 18003, "No inputs were found in config file '", config,
            "'. Specified 'include' paths were '", include, "' and 'exclude' paths were '", exclude, "'.");
        c_drop(cstr, &include, &exclude);
        reported = true;
    }
    cstr_drop(&config);
    return reported;
}

Error* program_config_error(Program* me, u32 start, u32 end, unsigned code, cstr message) {
    if (!my config_source || end > cstr_size(&my config_source->contents) || start > end) {
        return error_ctx_global_error_s(my errs, code, message);
    }
    return error_ctx_error_s(my errs, config_span(me, start, end), code, message);
}

// tsc's createDiagnosticForOptionName (on_key) and createOptionValueDiagnostic
#define option_error_d_one(me, site, code, ...) option_error_s(me, site, code, cstr_d(__VA_ARGS__))
#define option_error_d(me, name, name2, on_key, code, ...) do { \
    Span option_sites_[4]; \
    u32 option_site_count_ = option_sites(me, name, name2, on_key, option_sites_); \
    for (u32 option_site_ = 0; option_site_ < option_site_count_; option_site_++) { \
        option_error_s(me, option_sites_[option_site_], code, cstr_d(__VA_ARGS__)); \
    } \
} while (0)

CompilerOptions* program_options(Program* me) {
    return &my options;
}

void program_destroy(Program* me) {
    if (me) {
        // (The thread that destroys a program is usually the one that asked it for names outside the checkers, the test
        // harness's symbols walk: its symbol chain caches. They're per thread and lazily remade, keyed by program)
        symbol_chain_release_thread();
        program_drop(me);
        free(me);
    }
}

static ModuleResolver* program_resolver(Program* me) {
    if (!my resolver) {
        my resolver = module_resolver_new(my fs, &my options);
        if (my realpath) module_resolver_set_realpath(my resolver, my realpath, my realpath_ctx);
        module_resolver_set_redirects(my resolver, my redirects);
    }
    return my resolver;
}

// What resolves the module names of a file: the program's resolver, or its referenced project's
static ModuleResolver* program_resolver_of(Program const* me, SourceFile const* file) {
    if (file && file->from_reference) return my reference_resolvers[file->from_reference - 1];
    check(my resolver);
    return my resolver;
}

// The options module resolution goes by in a file (tsc's getCompilerOptionsForFile)
static CompilerOptions const* program_options_of(Program const* me, SourceFile const* file) {
    if (file && file->from_reference) return project_redirects_options(my redirects, file->from_reference - 1);
    return &my options;
}

void program_set_references(Program* me, ProjectRedirects const* redirects) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    check(!my reference_count && !my pool);
    my redirects = redirects;
    if (my resolver) module_resolver_set_redirects(my resolver, redirects);
    my reference_count = project_redirects_count(redirects);
    my reference_resolvers = c_new_n(ModuleResolver*, my reference_count + 1);
    for (u32 i = 0; i < my reference_count; i++) {
        ModuleResolver* resolver = module_resolver_new(my fs, project_redirects_options(redirects, i));
        if (my realpath) module_resolver_set_realpath(resolver, my realpath, my realpath_ctx);
        module_resolver_set_redirects(resolver, redirects);
        my reference_resolvers[i] = resolver;
    }
}

void program_set_lib_dir(Program* me, csview dir) {
    cstr_assign_sv(&my lib_dir, dir);
}

// The file a `/// <reference lib="NAME" />` loads (tsc's libMap: "esnext.bigint" is lib.es2020.bigint.d.ts)
static cstr lib_reference_file_name(csview path) {
    cstr name = cstr_from_sv(path);
    cstr_lowercase(&name);
    cstr ret = cstr_d(compiler_options_lib_file_name(cstr_sv(&name)), ".d.ts");
    cstr_drop(&name);
    return ret;
}

void program_follow_lib_references(Program* me) {
    my follow_lib_references = true;
}

void program_set_realpath(Program* me, cstr (*realpath)(void* ctx, csview path), void* ctx) {
    my realpath = realpath;
    my realpath_ctx = ctx;
    module_resolver_set_realpath(program_resolver(me), realpath, ctx);
    for (u32 i = 0; i < my reference_count; i++) module_resolver_set_realpath(my reference_resolvers[i], realpath, ctx);
}

// The fs' working directory as of its first use (the harness sets it up after making the program), which must not
// change once files are loaded. Read on the main thread before the load pool starts, only read after
static csview program_cwd(Program const* me) {
    if (!cstr_size(&my cwd)) {
        // Not being able to determine our own working directory isn't something we can carry on from
        try (fs_get_cwd(my fs, &((Program*)me)->cwd)) or_panic;
    }
    return cstr_sv(&my cwd);
}

void program_set_file_size(Program* me, i64 (*file_size)(void* ctx, csview path), void* ctx) {
    my file_size = file_size;
    my file_size_ctx = ctx;
}

// tsc names files by absolute path, and its test harness strips the directory the test's files are in: relative
// below the working directory, absolute elsewhere
static cstr program_path_for_message(Program const* me, csview absolute) {
    // (The CLI's messages: absolute, like tsc's)
    if (messages_use_absolute_paths()) return cstr_from_sv(absolute);
    csview cwd = program_cwd(me);
    bool below = cwd.size > 1 && absolute.size > cwd.size + 1 && !memcmp(absolute.buf, cwd.buf, (size_t)cwd.size) &&
        absolute.buf[cwd.size] == '/';
    return below ? cstr_from_sv(csview_slice(absolute, cwd.size + 1, absolute.size)) : cstr_from_sv(absolute);
}

// Whether a script declares `declare module "<name>"` (tsc's SourceFile.ambientModuleNames)
static bool source_declares_ambient_module(SourceFile const* source, csview name) {
    for (u32 i = 0; i < source->module_ref_count; i++) {
        ModuleRef const* ref = source->module_refs[i];
        // (Not an augmentation inside an ambient module: that names a file)
        if (ref->is_declaration && !ref->nested_in_ambient_module && csview_equals_sv(cstr_sv(&ref->path), name)) return true;
    }
    return false;
}

static void program_load_resolved(Program* me, SourceFile* source, ModuleRef* ref, ResolvedModule* resolved_ptr);

static bool path_is_js(csview path) {
    return csview_ends_with(path, ".js") || csview_ends_with(path, ".jsx") || csview_ends_with(path, ".mjs") ||
        csview_ends_with(path, ".cjs");
}

static bool is_declaration_path(csview path) {
    return csview_ends_with(path, ".d.ts") || csview_ends_with(path, ".d.mts") || csview_ends_with(path, ".d.cts");
}

// The source file a referenced project's output declaration file stands for (absolute), empty if path isn't one
static csview program_source_of_output(Program const* me, csview path) {
    if (!my redirects || !is_declaration_path(path)) return c_sv("");
    if (path.size && path.buf[0] == '/') return project_redirects_source_of_output(my redirects, path);
    cstr absolute = absolute_path_of(program_cwd(me), path);
    csview ret = project_redirects_source_of_output(my redirects, cstr_sv(&absolute));
    cstr_drop(&absolute);
    return ret;
}

// A resolution that found a referenced project's output finds its source (tsc's getSourceOfProjectReferenceRedirect)
static void program_redirect_resolved(Program const* me, ResolvedModule* resolved) {
    if (resolved->kind != RESOLVED_DTS || !my redirects) return;
    csview source = program_source_of_output(me, cstr_sv(&resolved->path));
    if (!source.size && csview_contains(cstr_sv(&resolved->path), "/node_modules/")) {
        // Through a link that wasn't followed (preserveSymlinks)
        cstr real = module_resolver_real_path_lenient(my resolver, cstr_sv(&resolved->path));
        if (cstr_size(&real)) source = program_source_of_output(me, cstr_sv(&real));
        cstr_drop(&real);
    }
    if (!source.size) return;
    cstr_assign_sv(&resolved->path, source);
    resolved->kind = path_is_js(source) ? RESOLVED_JS : RESOLVED_TS;
    resolved->is_external_library = false;
    cstr_take(&resolved->package_id, cstr_null);
}

// A resolution that followed a symlink (tsc's symlink cache, setSymlinksFromResolutions)
static void program_learn_symlink(Program* me, ResolvedModule const* resolved) {
    if (cstr_size(&resolved->original_path) && cstr_size(&resolved->path)) {
        symlink_cache_add_resolution(my symlinks, cstr_sv(&resolved->original_path), cstr_sv(&resolved->path));
    }
}

// tsc's tryExtractTSExtension: the name ends in .d.ts, .d.mts, .d.cts, .ts, .tsx, .mts or .cts
static bool name_has_ts_extension(csview name) {
    static const char* const extensions[] = { ".ts", ".tsx", ".mts", ".cts" };
    for (u32 i = 0; i < c_arraylen(extensions); i++) {
        if (csview_ends_with(name, extensions[i])) return true;
    }
    return false;
}

// The conditions `exports` are read with: by syntax, else the format of the importing file. Under bundler an import in
// a file that is emitted as CommonJS (.cts, .cjs) uses `require` too (tsc's getModeForUsageLocation:
// importSyntaxAffectsModuleResolution, getEmitSyntaxForUsageLocation)
static ResolutionMode program_module_ref_mode(Program const* me, SourceFile const* source, ModuleRef const* ref) {
    ResolutionMode mode = ref->syntax == MODULE_REF_REQUIRE ? RESOLUTION_MODE_REQUIRE_SYNTAX :
        ref->syntax == MODULE_REF_DYNAMIC || source->is_esm ? RESOLUTION_MODE_ESM : RESOLUTION_MODE_COMMONJS;
    csview name = cstr_sv(&source->canonical_path);
    CompilerOptions const* options = program_options_of(me, source);
    bool node_resolution = options->module_resolution == MODULE_RESOLUTION_NODE16 ||
        options->module_resolution == MODULE_RESOLUTION_NODENEXT;
    bool node_module = options->module >= MODULE_KIND_NODE16 && options->module <= MODULE_KIND_NODENEXT;
    if (node_resolution && !node_module && ref->syntax == MODULE_REF_IMPORT) {
        // Node resolution with a module kind that isn't node's: an import goes by the file's emit format, which is the
        // module kind unless an extension or package.json `type` decides (tsc's getEmitSyntaxForUsageLocation)
        bool esm = (options->module >= MODULE_KIND_ES2015 && options->module <= MODULE_KIND_ESNEXT) ||
            options->module == MODULE_KIND_PRESERVE;
        if (csview_ends_with(name, ".cts") || csview_ends_with(name, ".cjs")) esm = false;
        else if (csview_ends_with(name, ".mts") || csview_ends_with(name, ".mjs")) esm = true;
        else if (source->has_format && source->package_has_type) esm = source->is_esm;
        mode = esm ? RESOLUTION_MODE_ESM : RESOLUTION_MODE_COMMONJS;
    }
    if (options->module_resolution == MODULE_RESOLUTION_BUNDLER &&
        (options->resolve_package_json_exports || options->resolve_package_json_imports) &&
        mode == RESOLUTION_MODE_COMMONJS && ref->syntax == MODULE_REF_IMPORT
    ) {
        // The file's emit format (tsc's getEmitModuleFormatOfFileWorker outside node16..nodenext): its extension or
        // package.json `type` where its implied format comes from one, else the module kind
        bool cjs = options->module == MODULE_KIND_COMMONJS;
        if (csview_ends_with(name, ".cts") || csview_ends_with(name, ".cjs")) cjs = true;
        else if (csview_ends_with(name, ".mts") || csview_ends_with(name, ".mjs")) cjs = false;
        else if (source->has_format && source->package_has_type) cjs = !source->is_esm;
        if (cjs) mode = RESOLUTION_MODE_REQUIRE_SYNTAX;
    }
    if (ref->resolution_override == IMPORT_RESOLUTION_IMPORT) mode = RESOLUTION_MODE_ESM | RESOLUTION_MODE_EXPLICIT;
    if (ref->resolution_override == IMPORT_RESOLUTION_REQUIRE) mode = RESOLUTION_MODE_REQUIRE_SYNTAX | RESOLUTION_MODE_EXPLICIT;
    return mode;
}

// A resolved TypeScript file becomes part of the program, anything else is remembered on the ref as why there's no file
static void program_load_module_ref(Program* me, SourceFile* source, ModuleRef* ref) {
    program_resolver(me);
    ModuleResolver* resolver = program_resolver_of(me, source);
    csview containing = cstr_sv(&source->canonical_path);
    ResolutionMode mode = program_module_ref_mode(me, source, ref);
    ResolvedModule resolved = module_resolver_resolve(resolver, cstr_sv(&ref->path), containing, mode);
    program_learn_symlink(me, &resolved);
    program_redirect_resolved(me, &resolved);
    if (cstr_size(&resolved.ambiguous_root_package_json)) {
        cstr shown = program_path_for_message(me, cstr_sv(&resolved.ambiguous_root_package_json));
        cstr message = cstr_d(
            "The project root is ambiguous, but is required to resolve ",
            resolved.ambiguous_root_in_imports ? "import" : "export", " map entry '", resolved.ambiguous_root_entry,
            "' in file '", shown, "'. Supply the `rootDir` compiler option to disambiguate."
        );
        unsigned code = resolved.ambiguous_root_in_imports ? 2210 : 2209;
        bool seen = false;
        if (my errs) for_each (i, vec_error, &my errs->errors) {
            if (i->ts_code == code && cstr_eq(&i->message, &message)) seen = true;
        }
        if (seen) cstr_drop(&message);
        else error_ctx_global_error_s(my errs, code, message);
        cstr_drop(&shown);
    }
    if (cstr_size(&resolved.package_name)) {
        auto entry = hmap_package_flag_insert(&my packages, cstr_clone(resolved.package_name), 0);
        if (resolved.kind == RESOLVED_DTS) entry.ref->second = 1;
    }
    if (my options.no_resolve) {
        // Nothing is added to the program, but a name may still mean a file that is part of it
        if (resolved.kind != RESOLVED_NONE) ref->file = program_find_source(me, cstr_sv(&resolved.path));
        resolved_module_drop(&resolved);
        return;
    }
    // An augmentation doesn't load what it augments (see ModuleRef.augmented_path), what can't be loaded anyway goes
    // the usual way (TS2665 for an untyped module)
    bool loadable = resolved.kind == RESOLVED_TS || resolved.kind == RESOLVED_DTS || (resolved.kind == RESOLVED_JS && my options.allow_js);
    if (ref->is_declaration && loadable) {
        ref->file = program_find_source(me, cstr_sv(&resolved.path));
        cstr_take(&ref->augmented_path, cstr_clone(resolved.path));
        resolved_module_drop(&resolved);
        return;
    }
    bool is_new = resolved.kind != RESOLVED_NONE && !program_find_source(me, cstr_sv(&resolved.path));
    if (cstr_size(&resolved.alternate_result)) {
        cstr_take(&ref->alternate_result, program_path_for_message(me, cstr_sv(&resolved.alternate_result)));
    }
    program_load_resolved(me, source, ref, &resolved);
    if (is_new && ref->file && !ref->is_declaration) ref->file->loaded_by_import = true;
    if (is_new && ref->file && !span_valid(ref->file->included_at)) ref->file->included_at = ref->span;
    resolved_module_drop(&resolved);
}

// (resolved stays the caller's)
static void program_load_resolved(Program* me, SourceFile* source, ModuleRef* ref, ResolvedModule* resolved_ptr) {
    ResolvedModule const resolved = *resolved_ptr;
    switch (resolved.kind) {
        case RESOLVED_TS:
        case RESOLVED_DTS:
            if (my options.jsx == JSX_NONE && csview_ends_with(cstr_sv(&resolved.path), ".tsx")) {
                ref->failure = MODULE_REF_JSX_NOT_SET;
                cstr_take(&ref->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
            } else if (resolved.arbitrary_extension && !my options.allow_arbitrary_extensions && !source->is_declaration) {
                ref->failure = MODULE_REF_ARBITRARY_EXTENSION;
                cstr_take(&ref->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
            } else {
                ref->file = program_add_source_idempotent(me, cstr_sv(&resolved.path));
                // (Only an extension the name itself has counts, not one a package.json or an export map wrote:
                // tsc's candidateIsFromPackageJsonField, tryExtractTSExtension of the name)
                ref->used_ts_extension = resolved.used_ts_extension && name_has_ts_extension(cstr_sv(&ref->path));
                // The same version of a package installed twice is one package (tsc's packageIdToSourceFile): the
                // second copy stays in the program, but imports mean the first
                if (cstr_size(&resolved.package_id) && ref->file) {
                    auto first = hmap_source_file_ptr_get(&my files_by_package_id, cstr_sv(&resolved.package_id));
                    if (first) {
                        if (first->second != ref->file) {
                            ref->redirected_from = ref->file;
                            ref->file->package_redirect = true;
                            ref->file->unchecked = true;
                        }
                        ref->file = first->second;
                    } else {
                        hmap_source_file_ptr_insert(&my files_by_package_id, cstr_clone(resolved.package_id), ref->file);
                    }
                }
            }
            break;
        case RESOLVED_JS:
            if (my options.jsx == JSX_NONE && csview_ends_with(cstr_sv(&resolved.path), ".jsx")) {
                ref->failure = MODULE_REF_JSX_NOT_SET;
                cstr_take(&ref->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
                break;
            }
            // JavaScript of a package is part of the program only down to maxNodeModuleJsDepth (0 by default)
            // (a package that is a link to somewhere outside of any node_modules is the project's own code)
            bool package_js = resolved.is_external_library &&
                csview_find_sv(cstr_sv(&resolved.path), c_sv("/node_modules/")) != c_NPOS;
            if (my options.allow_js && (!package_js || source->node_modules_js_depth < my options.max_node_module_js_depth)) {
                bool is_new = !program_find_source(me, cstr_sv(&resolved.path));
                ref->file = program_add_source_idempotent(me, cstr_sv(&resolved.path));
                // (tsc's maxNodeModuleJsDepth: a package's JavaScript, as deep as allowed, never checked)
                if (ref->file && package_js && is_new) {
                    ref->file->node_modules_js_depth = source->node_modules_js_depth + 1;
                    ref->file->unchecked = true;
                }
            } else {
                ref->failure = MODULE_REF_IS_JS;
                cstr_take(&ref->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
                if (!module_name_is_relative(cstr_sv(&ref->path))) ref->package_name = cstr_clone(resolved.package_name);
            }
            break;
        case RESOLVED_JSON:
            ref->file = program_add_source_idempotent(me, cstr_sv(&resolved.path));
            if (!ref->file) ref->failure = MODULE_REF_IS_JSON;
            break;
        case RESOLVED_NONE:
            if (resolved.json_without_option) {
                ref->failure = MODULE_REF_JSON_WITHOUT_OPTION;
            } else if (resolved.needs_extension) {
                ref->failure = MODULE_REF_NEEDS_EXTENSION;
                if (cstr_size(&resolved.suggested_extension)) {
                    cstr_take(&ref->resolved_path, cstr_d(ref->path, resolved.suggested_extension));
                }
            }
            break;
    }
}

// `/// <reference path="a.ts" />`: the file itself, relative to the one with the directive
// tsc's hasExtension: the file name (not the directories) has a dot
static bool path_has_extension(csview path) {
    return memchr(basename_of(path).buf, '.', (size_t)basename_of(path).size) != nullptr;
}

// (tsc's getSourceFileFromReferenceWorker: a name with an extension is that file, one without gets the first group of
// supported extensions)
static SourceFile* program_load_reference_path(Program* me, Span path_span, csview path) {
    check(span_valid(path_span));
    csview dir = dirname_of(cstr_sv(&path_span.source->canonical_path));
    // (A drive root `c:/` is rooted too: one of the current directory, see path_is_drive_rooted())
    bool rooted = (path.size && path.buf[0] == '/') || path_is_drive_rooted(path);
    cstr full = rooted ? cstr_from_sv(path) : cstr_d(dir, "/", path);
    SourceFile* ret = nullptr;
    static const char* const extensions[] = { "", ".ts", ".tsx", ".d.ts" };
    bool extension = path_has_extension(path);
    for (isize i = extension ? 0 : 1; i < (extension ? 1 : (isize)c_arraylen(extensions)) && !ret; i++) {
        cstr candidate = cstr_d(full, extensions[i]);
        csview source = program_source_of_output(me, cstr_sv(&candidate));
        if (!source.size && fs_is_reg_file(my fs, cstr_sv(&candidate))) source = cstr_sv(&candidate);
        if (source.size) {
            bool is_new = !program_find_source(me, source);
            ret = program_add_source_idempotent(me, source);
            if (is_new && ret && !span_valid(ret->included_at)) ret->included_at = path_span;
        }
        cstr_drop(&candidate);
    }
    cstr_drop(&full);
    return ret;
}

// A `types` reference from source (the program's `types` when null) with the reference's resolution mode, else the file's
static SourceFile* program_load_type_reference(
    Program* me, csview name, SourceFile const* source, ImportResolutionOverride override
) {
    ResolutionMode mode = RESOLUTION_MODE_COMMONJS;
    bool has_mode = true;
    if (override == IMPORT_RESOLUTION_IMPORT) mode = RESOLUTION_MODE_ESM;
    else if (override == IMPORT_RESOLUTION_REQUIRE) mode = RESOLUTION_MODE_COMMONJS;
    else if (source && source->has_format) mode = source->is_esm ? RESOLUTION_MODE_ESM : RESOLUTION_MODE_COMMONJS;
    else has_mode = false;
    program_resolver(me);
    ResolvedModule resolved = module_resolver_resolve_type_reference(
        program_resolver_of(me, source), name, source ? cstr_sv(&source->canonical_path) : c_sv(""),
        has_mode ? &mode : nullptr
    );
    program_learn_symlink(me, &resolved);
    program_redirect_resolved(me, &resolved);
    SourceFile* ret = resolved.kind == RESOLVED_NONE ? nullptr :
        program_add_source_idempotent(me, cstr_sv(&resolved.path));
    resolved_module_drop(&resolved);
    return ret;
}

// Called for each toplevel statement of each loaded file
static inline void program_handle_statement(Program* me, SourceFile* source, Statement* statement) {
    switch (statement->tag) {
        case STATEMENT_SOURCE_DIRECTIVE:
            switch (statement->source_directive->tag) {
                case SOURCE_DIRECTIVE_REFERENCE_PATH:
                    if (my options.no_resolve) break;
                    SourceFile const* referenced = program_load_reference_path(
                        me,
                        statement->source_directive->path_span,
                        cstr_sv(&statement->source_directive->path)
                    );
                    // (tsc's getSourceFileFromReferenceWorker)
                    if (referenced == source) statement->source_directive->self_reference = true;
                    if (!referenced) {
                        // Reported when names are resolved (it is no reason not to typecheck), named after the file's
                        // name as messages have it by then
                        cstr_assign_sv(&statement->source_directive->not_found, cstr_sv(&statement->source_directive->path));
                        statement->source_directive->not_found_extensionless =
                            !path_has_extension(cstr_sv(&statement->source_directive->path));
                    }
                    break;
                case SOURCE_DIRECTIVE_REFERENCE_TYPES:
                    if (!program_load_type_reference(
                        me, cstr_sv(&statement->source_directive->path), source,
                        statement->source_directive->resolution_override
                    )) {
                        cstr_assign_sv(&statement->source_directive->not_found, cstr_sv(&statement->source_directive->path));
                    }
                    break;
                case SOURCE_DIRECTIVE_REFERENCE_LIB: {
                    // Between lib files: they are all in one directory. From another file (tsc's
                    // processLibReferenceDirectives, `@types/node` has some): a lib like the ones the options name. An
                    // unknown name is ignored (TODO: TS2726). The harness keeps libs in a shared program, which a
                    // test's file can't add to (TODO)
                    cstr name = lib_reference_file_name(cstr_sv(&statement->source_directive->path));
                    if (my loading_stdlib) {
                        program_load_reference_path(me, statement->source_directive->path_span, cstr_sv(&name));
                    } else if (my follow_lib_references && !my options.no_lib) {
                        cstr path = cstr_d(cstr_size(&my lib_dir) ? cstr_sv(&my lib_dir) : default_lib_dir, "/", name);
                        if (fs_is_reg_file(my fs, cstr_sv(&path))) {
                            my loading_stdlib = true;
                            program_add_source_idempotent(me, cstr_sv(&path));
                            my loading_stdlib = false;
                        }
                        cstr_drop(&path);
                    }
                    cstr_drop(&name);
                }   break;
            }
            break;

        case STATEMENT_IMPORT_EXPORT: {
            PortStatement* port = statement->import_export;
            // (`export as namespace` is no indicator: tsc's isAnExternalModuleIndicatorNode)
            if ((port->main.tag != PORT_IMPORT_ALIAS || port->main.export_import) && port->main.tag != PORT_EXPORT_AS_NAMESPACE &&
                !port->jsdoc
            ) {
                source->is_module = true;
                if (!span_valid(source->module_indicator)) source->module_indicator = statement->span;
            }
        }   break;

        case STATEMENT_EXPORT_DEFAULT_EXPR:
            source->is_module = true;
            if (!span_valid(source->module_indicator)) source->module_indicator = statement->span;
            break;

        case STATEMENT_DECL_NAMESPACE:
        case STATEMENT_DECL_VARIABLE:
        case STATEMENT_DECL_FUNCTION:
        case STATEMENT_DECL_ENUM:
        case STATEMENT_DECL_CLASS:
        case STATEMENT_DECL_TYPE:
        case STATEMENT_DECL_INTERFACE:
            // (A JSDoc `@enum` of `exports.C = {}` is exported, but no module syntax)
            if (statement->decl.props.exported && !(statement->tag == STATEMENT_DECL_TYPE && statement->decl.type->jsdoc)) {
                source->is_module = true;
                if (!span_valid(source->module_indicator)) {
                    Span name = statement->tag == STATEMENT_DECL_CLASS ? statement->decl.class->name :
                        statement->tag == STATEMENT_DECL_FUNCTION ? statement->decl.function.impl->name :
                        statement->tag == STATEMENT_DECL_ENUM ? statement->decl.enum_->name :
                        statement->tag == STATEMENT_DECL_INTERFACE ? statement->decl.interface->name :
                        statement->tag == STATEMENT_DECL_TYPE ? statement->decl.type->name :
                        statement->tag == STATEMENT_DECL_NAMESPACE ? statement->decl.namespace->name : span_null;
                    source->module_indicator = span_valid(name) ? name : statement->span;
                }
            }
            break;

        case STATEMENT_NOOP:
        case STATEMENT_BLOCK:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_SWITCH:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_LABELED:
        case STATEMENT_WITH:
        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE:
        case STATEMENT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            break;
    }
}

// Loads the file at the given path, adds it to the program, and returns a SourceFile for it. Returns nullptr on error.
// Only loads each file once, even if paths aren't expressed identically. Calls itself recursively to load imports and
// referenced files.
// `// @ts-check` (1) or `// @ts-nocheck` (-1) in the comments at the top of a file, the last one wins. 0 without
static int source_check_directive(csview contents) {
    int ret = 0;
    const char* c = contents.buf;
    const char* end = contents.buf + contents.size;
    while (c < end) {
        while (c < end && (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n')) c++;
        if (c + 1 >= end || c[0] != '/' || (c[1] != '/' && c[1] != '*')) break;
        const char* comment_end;
        if (c[1] == '/') {
            comment_end = c;
            while (comment_end < end && *comment_end != '\n') comment_end++;
        } else {
            csview rest = csview_from_range(c + 2, end);
            isize close = csview_find_sv(rest, c_sv("*/"));
            comment_end = close == c_NPOS ? end : rest.buf + close + 2;
        }
        // tsc: /^\/\/\/?\s*@ts-(no)?check/, single line comments only
        if (c[1] == '/') {
            const char* p = c + 2;
            if (p < comment_end && *p == '/') p++;
            while (p < comment_end && (*p == ' ' || *p == '\t')) p++;
            csview text = csview_from_range(p, comment_end);
            if (csview_starts_with(text, "@ts-nocheck")) ret = -1;
            else if (csview_starts_with(text, "@ts-check")) ret = 1;
        }
        c = comment_end;
    }
    return ret;
}

// `/// <reference no-default-lib="true"/>` in the comments at the top of a file (tsc's hasNoDefaultLib): the file
// says it is the default lib, which skipDefaultLibCheck doesn't check
static bool source_has_no_default_lib(csview contents) {
    const char* c = contents.buf;
    const char* end = contents.buf + contents.size;
    while (c < end) {
        while (c < end && (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n')) c++;
        if (c + 1 >= end || c[0] != '/' || (c[1] != '/' && c[1] != '*')) break;
        const char* comment_end;
        if (c[1] == '/') {
            comment_end = c;
            while (comment_end < end && *comment_end != '\n') comment_end++;
            csview text = csview_from_range(c, comment_end);
            if (csview_starts_with(text, "///") && csview_contains(text, "<reference") &&
                (csview_contains(text, "no-default-lib=\"true\"") || csview_contains(text, "no-default-lib='true'"))
            ) {
                return true;
            }
        } else {
            csview rest = csview_from_range(c + 2, end);
            isize close = csview_find_sv(rest, c_sv("*/"));
            comment_end = close == c_NPOS ? end : rest.buf + close + 2;
        }
        c = comment_end;
    }
    return false;
}

static bool equals_ignoring_case(csview sv, const char* lowercase) {
    isize len = (isize)strlen(lowercase);
    if (sv.size != len) return false;
    for (isize i = 0; i < len; i++) {
        if (tolower((unsigned char)sv.buf[i]) != lowercase[i]) return false;
    }
    return true;
}

// tsc's processCommentPragmas as far as the JSX pragmas go: in each `/* */` comment at the top of the file, every
// line's `@name args` (/@(\S+)(\s+(?:\S.*)?)?$/gm), the first word of args being the argument
static void source_jsx_pragmas(SourceFile* file) {
    csview contents = cstr_sv(&file->contents);
    const char* c = contents.buf;
    const char* end = contents.buf + contents.size;
    while (c < end) {
        while (c < end && (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n')) c++;
        if (c + 1 >= end || c[0] != '/' || (c[1] != '/' && c[1] != '*')) break;
        const char* comment_end;
        if (c[1] == '/') {
            comment_end = c;
            while (comment_end < end && *comment_end != '\n') comment_end++;
            c = comment_end;
            continue;
        }
        csview rest = csview_from_range(c + 2, end);
        isize close = csview_find_sv(rest, c_sv("*/"));
        comment_end = close == c_NPOS ? end : rest.buf + close + 2;
        for (const char* line = c; line < comment_end;) {
            const char* line_end = line;
            while (line_end < comment_end && *line_end != '\n' && *line_end != '\r') line_end++;
            const char* at = memchr(line, '@', (size_t)(line_end - line));
            if (at) {
                const char* name_end = at + 1;
                while (name_end < line_end && !isspace((unsigned char)*name_end)) name_end++;
                const char* arg = name_end;
                while (arg < line_end && isspace((unsigned char)*arg)) arg++;
                const char* arg_end = arg;
                while (arg_end < line_end && !isspace((unsigned char)*arg_end)) arg_end++;
                csview name = csview_from_range(at + 1, name_end);
                csview value = csview_from_range(arg, arg_end);
                if (value.size) {
                    if (equals_ignoring_case(name, "jsx") && !file->jsx_pragmas.factory.size) file->jsx_pragmas.factory = value;
                    if (equals_ignoring_case(name, "jsxfrag") && !file->jsx_pragmas.fragment_factory.size) file->jsx_pragmas.fragment_factory = value;
                    if (equals_ignoring_case(name, "jsxruntime")) file->jsx_pragmas.runtime = value;
                    if (equals_ignoring_case(name, "jsximportsource")) file->jsx_pragmas.import_source = value;
                }
            }
            line = line_end;
            while (line < comment_end && (*line == '\n' || *line == '\r')) line++;
        }
        c = comment_end;
    }
}

// A new SourceFile for the file at canonical_path, parsed (its errors into errs), null if it can't be read. On the
// loader's thread or on a load pool worker: it only reads the program
static SourceFile* program_make_source(Program const* me, csview canonical_path, bool is_stdlib, ErrorCtx* errs) {
    cstr contents;
    try (fs_read_file(my fs, canonical_path, &contents)) if_err (e) {
        // Not worth reporting: the caller turns a null return into a TypeScript "file not found" error
        ignore e;
        current_error_destroy();
        return nullptr;
    }
    text_decode(&contents);

    SourceFile* source_file = c_new(SourceFile, source_file_make(cstr_from_sv(canonical_path), contents));
    csview path = cstr_sv(&source_file->canonical_path);

    if (csview_ends_with(path, ".d.ts") || csview_ends_with(path, ".d.mts") || csview_ends_with(path, ".d.cts")) {
        source_file->is_declaration = true;
    } else if (csview_ends_with(path, ".ts")) {
        // tsc's getDeclarationFileExtension: `x.d.css.ts` (allowArbitraryExtensions) declares too
        isize base = path.size;
        while (base > 0 && path.buf[base - 1] != '/') base--;
        for (isize i = base; i + 3 <= path.size; i++) {
            if (!memcmp(path.buf + i, ".d.", 3)) source_file->is_declaration = true;
        }
    }

    source_file->is_js = path_is_js(path);
    source_file->is_json = csview_ends_with(path, ".json");
    source_file->is_jsx = source_file->is_js || csview_ends_with(path, ".tsx");
    source_jsx_pragmas(source_file);
    int check_directive = source_check_directive(cstr_sv(&source_file->contents));
    source_file->unchecked = check_directive ? check_directive < 0 : source_file->is_js && !my options.check_js;
    // tsc's skipTypeChecking: declaration files aren't checked with skipLibCheck
    if (source_file->is_declaration && my options.skip_lib_check) source_file->unchecked = true;
    if (my options.no_check) source_file->unchecked = true; // (tsc's skipTypeChecking: no semantic diagnostics at all)
    if (my options.skip_default_lib_check && !is_stdlib && source_has_no_default_lib(cstr_sv(&source_file->contents))) {
        source_file->unchecked = true;
    }
    // (tsc's canIncludeBindAndCheckDiagnostics: a JSON file has none, only convertToJson's syntax errors)
    if (source_file->is_json) source_file->unchecked = true;
    source_file->plain_js = source_file->is_js && !check_directive && !my options.is_set[COMPILER_OPTION_check_js] &&
        !my options.no_check;
    // A referenced project's file is that project's to check and report
    if (my redirects && !is_stdlib) {
        cstr absolute = absolute_path_of(program_cwd(me), path);
        source_file->from_reference = project_redirects_project_of(my redirects, cstr_sv(&absolute));
        cstr_drop(&absolute);
        if (source_file->from_reference) {
            source_file->unchecked = true;
            source_file->plain_js = false;
            source_file->skip_typed_bodies = !source_file->is_js && !source_file->is_jsx && !source_file->is_json;
            CompilerOptions const* options = project_redirects_options(my redirects, source_file->from_reference - 1);
            source_file->reference_const_enums_ambient = !options->preserve_const_enums && !options->isolated_modules &&
                !options->verbatim_module_syntax;
        }
    }

    if (is_stdlib) {
        source_file->is_stdlib = true;
        csview filename = basename_of(cstr_sv(&source_file->canonical_path));
        cstr_take(&source_file->decl_override, cstr_d("lib.", filename, ", --, --"));
    }

    CompilerOptions const* options = program_options_of(me, source_file);
    bool node_formats = (options->module >= MODULE_KIND_NODE16 && options->module <= MODULE_KIND_NODENEXT) ||
        options->module_resolution == MODULE_RESOLUTION_NODE16 ||
        options->module_resolution == MODULE_RESOLUTION_NODENEXT;
    // (A JSON file has no module format of its own: tsc's getImpliedNodeFormatForFile, which also looks at the
    // package.json of anything in node_modules)
    // (canonical paths are relative to the working directory: `node_modules/x` is in one too)
    csview canonical = cstr_sv(&source_file->canonical_path);
    bool in_node_modules = csview_contains(canonical, "/node_modules/") || csview_starts_with(canonical, "node_modules/");
    if ((node_formats || in_node_modules) && !is_stdlib && !source_file->is_json) {
        FileFormat format = module_resolver_file_format(
            program_resolver_of(me, source_file), cstr_sv(&source_file->canonical_path)
        );
        source_file->is_esm = format.mode == RESOLUTION_MODE_ESM;
        source_file->has_format = true;
        source_file->package_has_type = format.package_has_type;
        if (cstr_size(&format.package_json)) {
            source_file->package_json = program_path_for_message(me, cstr_sv(&format.package_json));
        }
        cstr_drop(&format.package_json);
    }

    parse_file(source_file, errs);
    return source_file;
}

static void source_file_free(void* ctx, SourceFile* file) {
    ignore ctx;
    source_file_drop(file);
    free(file);
}

static void program_queue_path(Program* me, LoadPool* pool, csview absolute, bool is_stdlib) {
    cstr canonical = relative_path_of(program_cwd(me), absolute);
    i64 size = my file_size ? my file_size(my file_size_ctx, cstr_sv(&canonical)) : 0;
    load_pool_queue(pool, cstr_sv(&canonical), is_stdlib, size);
    cstr_drop(&canonical);
}

// Queues what the loader will probably load from a parsed file: a guess (the loader decides with what it learns after
// parsing, like the file's module-ness, the helpers and JSX runtime imports), so a wrong one only costs a parse.
// Resolutions go to the shared resolver's cache, where the loader finds them
static void program_load_ahead_from(Program* me, LoadPool* pool, SourceFile const* file) {
    if (my options.no_resolve) return;
    ModuleResolver* resolver = program_resolver_of(me, file);
    csview containing = cstr_sv(&file->canonical_path);
    for (u32 i = 0; i < file->module_ref_count; i++) {
        ModuleRef const* ref = file->module_refs[i];
        if (ref->is_declaration || ref->ambient_only) continue;
        ResolutionMode mode = program_module_ref_mode(me, file, ref);
        ResolvedModule resolved = module_resolver_resolve(resolver, cstr_sv(&ref->path), containing, mode);
        program_redirect_resolved(me, &resolved);
        bool load = resolved.kind == RESOLVED_TS || resolved.kind == RESOLVED_DTS || resolved.kind == RESOLVED_JSON ||
            (resolved.kind == RESOLVED_JS && my options.allow_js && !(resolved.is_external_library &&
                csview_contains(cstr_sv(&resolved.path), "/node_modules/")));
        if (load) program_queue_path(me, pool, cstr_sv(&resolved.path), false);
        resolved_module_drop(&resolved);
    }
    for_each (s, vec_statement, &file->ast.statements) {
        if (s->tag != STATEMENT_SOURCE_DIRECTIVE) continue;
        SourceDirectiveStatement const* directive = s->source_directive;
        switch (directive->tag) {
            case SOURCE_DIRECTIVE_REFERENCE_PATH:
                // (program_load_reference_path() probes extensions: the common case is a whole name)
                if (span_valid(directive->path_span)) {
                    csview path = cstr_sv(&directive->path);
                    cstr full = path.size && path.buf[0] == '/' ? cstr_from_sv(path) :
                        cstr_d(dirname_of(containing), "/", path);
                    program_queue_path(me, pool, cstr_sv(&full), file->is_stdlib);
                    cstr_drop(&full);
                }
                break;
            case SOURCE_DIRECTIVE_REFERENCE_TYPES: {
                ResolutionMode mode = RESOLUTION_MODE_COMMONJS;
                bool has_mode = true;
                if (directive->resolution_override == IMPORT_RESOLUTION_IMPORT) mode = RESOLUTION_MODE_ESM;
                else if (directive->resolution_override == IMPORT_RESOLUTION_REQUIRE) mode = RESOLUTION_MODE_COMMONJS;
                else if (file->has_format) mode = file->is_esm ? RESOLUTION_MODE_ESM : RESOLUTION_MODE_COMMONJS;
                else has_mode = false;
                ResolvedModule resolved = module_resolver_resolve_type_reference(
                    resolver, cstr_sv(&directive->path), containing, has_mode ? &mode : nullptr
                );
                program_redirect_resolved(me, &resolved);
                if (resolved.kind != RESOLVED_NONE) program_queue_path(me, pool, cstr_sv(&resolved.path), false);
                resolved_module_drop(&resolved);
            }   break;
            case SOURCE_DIRECTIVE_REFERENCE_LIB: {
                if (!file->is_stdlib && !(my follow_lib_references && !my options.no_lib)) break;
                cstr name = lib_reference_file_name(cstr_sv(&directive->path));
                cstr path = file->is_stdlib ? cstr_d(dirname_of(containing), "/", name) :
                    cstr_d(cstr_size(&my lib_dir) ? cstr_sv(&my lib_dir) : default_lib_dir, "/", name);
                program_queue_path(me, pool, cstr_sv(&path), true);
                cstr_drop(&path);
                cstr_drop(&name);
            }   break;
        }
    }
}

// A load pool worker's job
static SourceFile* program_parse_ahead(void* ctx, LoadPool* pool, csview path, bool is_stdlib, ErrorCtx* errs) {
    Program* me = ctx;
    // (The loader runs jobs too, with the options set)
    bool own_options = !compiler_options_private;
    if (own_options) set_compiler_options(&my options);
    SourceFile* file = program_make_source(me, path, is_stdlib, errs);
    if (file) program_load_ahead_from(me, pool, file);
    if (own_options) clear_compiler_options();
    return file;
}

// The load pool, started on first use when there are threads
static LoadPool* program_pool(Program* me) {
    if (!my pool && my threads > 1 && my phase == PROGRAM_PHASE_LOADING) {
        program_resolver(me);
        program_cwd(me);
        // (As many workers as threads: the loader mostly waits for them, and parses queued files meanwhile)
        my pool = load_pool_new(my threads, program_parse_ahead, source_file_free, me);
    }
    return my pool;
}

static SourceFile* program_add_source_idempotent(Program* me, csview path) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);

    cstr canonical_path = relative_path_of(program_cwd(me), path);

    auto cached_source = hmap_box_source_file_get(&my source_files_map, cstr_sv(&canonical_path));
    if (cached_source) {
        // A source file is already created for this path
        cstr_drop(&canonical_path);
        return cached_source->second.get;
    }

    // No source file is created for this path: a load pool worker may have parsed it, else parse it here
    program_resolver(me);
    SourceFile* source_file = nullptr;
    if (program_pool(me)) source_file = load_pool_take(my pool, cstr_sv(&canonical_path), my loading_stdlib, my errs);
    if (!source_file) {
        source_file = program_make_source(me, cstr_sv(&canonical_path), my loading_stdlib, my errs);
        if (!source_file) {
            cstr_drop(&canonical_path);
            return nullptr;
        }
        // (Its imports for the pool, which the loader is about to resolve anyway)
        if (my pool) program_load_ahead_from(me, my pool, source_file);
    }
    if (source_file->is_json) source_file->is_module = true;

    hmap_box_source_file_insert(&my source_files_map, canonical_path, box_source_file_from_ptr(source_file));

    // `/// <reference path>`s first, then the rest (`types`, `lib`): tsc's processReferencedFiles comes before
    // processTypeReferenceDirectives, and the order files load in is the program's
    for_each (i, vec_statement, &source_file->ast.statements) {
        if (i->tag == STATEMENT_SOURCE_DIRECTIVE && i->source_directive->tag == SOURCE_DIRECTIVE_REFERENCE_PATH) {
            program_handle_statement(me, source_file, i);
        }
    }
    for_each (i, vec_statement, &source_file->ast.statements) {
        if (i->tag == STATEMENT_SOURCE_DIRECTIVE && i->source_directive->tag == SOURCE_DIRECTIVE_REFERENCE_PATH) continue;
        program_handle_statement(me, source_file, i);
    }
    // tsc's setCommonJsModuleIndicator: ES imports and exports win
    if (source_file->is_js && source_file->has_commonjs && !source_file->is_module) {
        source_file->is_module = true;
        source_file->is_commonjs = true;
    }
    // tsc's setExternalModuleIndicator by moduleDetection: `force` makes every file that isn't a declaration file a
    // module, `auto` (the default) too for `import.meta`, a module format (.mts/.cts/.mjs/.cjs, an ESM package), and
    // a JSX tag under react-jsx
    if (!source_file->is_module && !source_file->is_declaration && !my loading_stdlib) {
        csview name = cstr_sv(&source_file->canonical_path);
        switch (my options.module_detection) {
            case MODULE_DETECTION_FORCE:
                source_file->is_module = true;
                break;
            case MODULE_DETECTION_AUTO:
                source_file->is_module = source_file->has_import_meta || source_file->is_esm ||
                    csview_ends_with(name, ".mts") || csview_ends_with(name, ".cts") ||
                    csview_ends_with(name, ".mjs") || csview_ends_with(name, ".cjs") ||
                    (source_file->has_jsx && (my options.jsx == JSX_REACT_JSX || my options.jsx == JSX_REACT_JSXDEV));
                break;
            case MODULE_DETECTION_LEGACY:
                source_file->is_module = source_file->has_import_meta;
                break;
        }
    }
    if (my options.import_helpers && source_file->is_module && !source_file->is_declaration && !my loading_stdlib) {
        Span at = { .source = source_file, .sv = csview_from_n(source_file_start(source_file), 0) };
        source_file->helpers_ref = source_file_add_module_ref(source_file, cstr_lit("tslib"), at);
        // First, like tsc's: it decides the order files are in the program
        memmove(source_file->module_refs + 1, source_file->module_refs,
            sizeof(ModuleRef*) * (source_file->module_ref_count - 1));
        source_file->module_refs[0] = source_file->helpers_ref;
        // Resolved like the import the emitter writes: a `require()` in CommonJS output
        if (ty_emits_commonjs_like(source_file)) source_file->helpers_ref->syntax = MODULE_REF_REQUIRE;
    }
    cstr jsx_runtime = my loading_stdlib ? cstr_null : source_file_jsx_runtime_import(source_file);
    if (cstr_size(&jsx_runtime)) {
        Span at = { .source = source_file, .sv = csview_from_n(source_file_start(source_file), 0) };
        ModuleRef* ref = source_file_add_module_ref(source_file, jsx_runtime, at);
        // Right after the helpers, before what the file imports itself (tsc's order of file.imports)
        u32 first = source_file->helpers_ref ? 1 : 0;
        memmove(source_file->module_refs + first + 1, source_file->module_refs + first,
            sizeof(ModuleRef*) * (source_file->module_ref_count - first - 1));
        source_file->module_refs[first] = ref;
        ref->ambient_only = !source_file->is_js && (source_file->is_declaration ||
            !(source_file->is_module || my options.isolated_modules || my options.verbatim_module_syntax));
        source_file->jsx_runtime_ref = ref;
    } else {
        cstr_drop(&jsx_runtime);
    }

    // Every module the file mentions, wherever (the parser lists them). By index: loading parses, never this file
    for (u32 i = 0; i < source_file->module_ref_count; i++) {
        ModuleRef* ref = source_file->module_refs[i];
        // `declare module "./a" {}` in a module augments a file. In a script it declares an ambient module, and so
        // does a name that finds no file
        if (ref->is_declaration && !source_file->is_module && !ref->nested_in_ambient_module) continue;
        if (ref->ambient_only) continue;
        if (my options.no_resolve) continue; // program_resolve_names(), once every root is loaded
        // A name the file declares an ambient module for is that module, not a file (tsc's resolveToOwnAmbientModule):
        // `import * as React from "react"` inside react16.d.ts's `declare module "react/jsx-runtime"`
        if (!source_file->is_module && !ref->nested_in_ambient_module && source_declares_ambient_module(source_file, cstr_sv(&ref->path))) continue;
        program_load_module_ref(me, source_file, ref);
    }

    // Add here so imports come before importing file
    source_file->program_order = (u32)vec_source_file_ptr_size(&my source_files_ordered);
    vec_source_file_ptr_push(&my source_files_ordered, source_file);

    return source_file;
}

failable program_load(Program* me, csview path, returns SourceFile*) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    // A referenced project's output is its source
    csview source_of_output = program_source_of_output(me, path);
    if (source_of_output.size) path = source_of_output;

    // tsc's getSourceFileFromReferenceWorker for a root: an extension that isn't a script's (a `.map`, a `.txt`)
    static const char* const scripts[] = { ".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs", ".json" };
    bool script = false;
    for (isize i = 0; i < c_countof(scripts) && !script; i++) script = csview_ends_with(path, scripts[i]);
    if (!path_has_extension(path)) {
        // The first group of supported extensions completes it (getSourceFileFromReferenceWorker)
        static const char* const completions[] = { ".ts", ".tsx", ".d.ts" };
        for (isize i = 0; i < c_countof(completions); i++) {
            cstr candidate = cstr_d(path, completions[i]);
            if (fs_is_reg_file(my fs, cstr_sv(&candidate))) {
                set_compiler_options(&my options);
                SourceFile* found = program_add_source_idempotent(me, cstr_sv(&candidate));
                clear_compiler_options();
                cstr_drop(&candidate);
                return ok(found);
            }
            cstr_drop(&candidate);
        }
        cstr shown = program_path_for_message(me, path);
        Error* error = error_ctx_global_error_d(my errs, 6231, "Could not resolve the path '", shown,
            "' with the extensions: ", my options.allow_js ?
                "'.ts', '.tsx', '.d.ts', '.js', '.jsx', '.cts', '.d.cts', '.cjs', '.mts', '.d.mts', '.mjs'." :
                "'.ts', '.tsx', '.d.ts', '.cts', '.d.cts', '.mts', '.d.mts'.");
        error_sub_error_d(error, span_null, TS_CODE_NONE, "The file is in the program because:");
        error_sub_error_d(
            vec_error_back_mut(&error->sub_errors), span_null, TS_CODE_NONE, "Root file specified for compilation"
        );
        cstr_drop(&shown);
        return ok(nullptr);
    }
    if (!script) {
        if (!fs_is_reg_file(my fs, path)) return err_d("File '", path, "' not found.");
        cstr shown = program_path_for_message(me, path);
        Error* error = error_ctx_global_error_d(my errs, 6054, "File '", shown, "' has an unsupported extension. The ",
            "only supported extensions are ", my options.allow_js ?
                "'.ts', '.tsx', '.d.ts', '.js', '.jsx', '.cts', '.d.cts', '.cjs', '.mts', '.d.mts', '.mjs'." :
                "'.ts', '.tsx', '.d.ts', '.cts', '.d.cts', '.mts', '.d.mts'.");
        error_sub_error_d(error, span_null, TS_CODE_NONE, "The file is in the program because:");
        error_sub_error_d(
            vec_error_back_mut(&error->sub_errors), span_null, TS_CODE_NONE, "Root file specified for compilation"
        );
        cstr_drop(&shown);
        return ok(nullptr);
    }
    if (path_is_js(path) && !my options.allow_js) {
        // Not part of the program. tsc still wants the file to exist, so does the caller
        if (!fs_is_reg_file(my fs, path)) return err_d("File '", path, "' not found.");
        cstr shown = program_path_for_message(me, path);
        Error* error = error_ctx_global_error_d(
            my errs, 6504, "File '", shown, "' is a JavaScript file. Did you mean to enable the 'allowJs' option?"
        );
        error_sub_error_d(error, span_null, TS_CODE_NONE, "The file is in the program because:");
        error_sub_error_d(
            vec_error_back_mut(&error->sub_errors), span_null, TS_CODE_NONE, "Root file specified for compilation"
        );
        cstr_drop(&shown);
        return ok(nullptr);
    }

    set_compiler_options(&my options);

    // Add the source file to the program
    SourceFile* main_source = program_add_source_idempotent(me, path);
    if (main_source && !my loading_stdlib) main_source->is_root = true;

    clear_compiler_options();

    // An unreadable entry point isn't a TypeScript-level diagnostic: it has no span to attach to, so
    // error_ctx_error_d() would just drop it. Report it to our caller instead.
    if (!main_source) {
        return err_d("File '", path, "' not found.");
    }

    return ok(main_source);
}

u32 program_source_count(Program const* me) {
    return (u32)vec_source_file_ptr_size(&my source_files_ordered);
}

SourceFile* program_source_at(Program const* me, u32 index) {
    return *vec_source_file_ptr_at(&my source_files_ordered, index);
}

SourceFile* program_find_source(Program const* me, csview path) {
    cstr canonical_path = relative_path_of(program_cwd(me), path);
    auto found = hmap_box_source_file_get(&my source_files_map, cstr_sv(&canonical_path));
    cstr_drop(&canonical_path);
    return found ? found->second.get : nullptr;
}

failable program_load_stdlib(Program* me, csview name, returns SourceFile*) {
    my loading_stdlib = true;
    cstr path = cstr_d(cstr_size(&my lib_dir) ? cstr_sv(&my lib_dir) : default_lib_dir, "/", name, ".d.ts");
    try (SourceFile* source, program_load(me, cstr_sv(&path), &source)) cleanup {
        my loading_stdlib = false;
        cstr_drop(&path);
    }
    my loading_stdlib = false;
    cstr_drop(&path);
    return ok(source);
}

void program_load_ahead(Program* me, csview const* libs, isize lib_count, csview const* roots, isize root_count) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    LoadPool* pool = program_pool(me);
    if (!pool) return;
    for (isize i = 0; i < lib_count; i++) {
        cstr path = cstr_d(cstr_size(&my lib_dir) ? cstr_sv(&my lib_dir) : default_lib_dir, "/", libs[i], ".d.ts");
        program_queue_path(me, pool, cstr_sv(&path), true);
        cstr_drop(&path);
    }
    for (isize i = 0; i < root_count; i++) program_queue_path(me, pool, roots[i], false);
    // The automatic types (program_load_automatic_types() resolves them again)
    StrList names = module_resolver_automatic_types(my resolver);
    for (isize i = 0; i < names.count; i++) {
        ResolvedModule resolved = module_resolver_resolve_type_reference(my resolver, cstr_sv(&names.items[i]), c_sv(""), nullptr);
        if (resolved.kind != RESOLVED_NONE) program_queue_path(me, pool, cstr_sv(&resolved.path), false);
        resolved_module_drop(&resolved);
    }
    str_list_drop(&names);
}

void program_load_automatic_types(Program* me) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    set_compiler_options(&my options);
    StrList names = module_resolver_automatic_types(program_resolver(me));
    for (isize i = 0; i < names.count; i++) {
        if (program_load_type_reference(me, cstr_sv(&names.items[i]), nullptr, IMPORT_RESOLUTION_DEFAULT)) continue;
        cstr const* name = &names.items[i];
        Error* error = error_ctx_global_error_d(my errs, 2688, "Cannot find type definition file for '", *name, "'.");
        error_sub_error_d(error, span_null, TS_CODE_NONE, "The file is in the program because:");
        error_sub_error_d(
            vec_error_back_mut(&error->sub_errors), span_null, TS_CODE_NONE,
            my options.types_set ? "Entry point of type library '" : "Entry point for implicit type library '",
            *name, my options.types_set ? "' specified in compilerOptions" : "'"
        );
    }
    str_list_drop(&names);
    clear_compiler_options();
    // (The parse errors of referenced projects' files, before anyone reports them)
    if (my reference_count) error_ctx_remove_unchecked(my errs);
}

void program_copy_globals_from(Program* me, Program const* other) {
    binder_set_parent(my binder, other->binder);
}

static u32 count_asterisks(csview text) {
    u32 count = 0;
    for (isize i = 0; i < text.size; i++) count += text.buf[i] == '*';
    return count;
}

// tsc's pathIsRelative || pathIsAbsolute (a root: `/`, `c:`, a URL)
static bool path_is_relative_or_absolute(csview path) {
    if (csview_starts_with(path, "./") || csview_starts_with(path, "../") || csview_equals(path, ".") ||
        csview_equals(path, "..") || csview_starts_with(path, ".\\") || csview_starts_with(path, "..\\")
    ) {
        return true;
    }
    if (path.size && (path.buf[0] == '/' || path.buf[0] == '\\')) return true;
    if (path.size >= 2 && path.buf[1] == ':' && ((path.buf[0] | 0x20) >= 'a' && (path.buf[0] | 0x20) <= 'z')) return true;
    return csview_contains(path, "://");
}

static char const* json_typeof(JsonValue const* value) {
    switch (value->tag) {
        case JSON_STRING: return "string";
        case JSON_NUMBER: return "number";
        case JSON_BOOL: return "boolean";
        default: return "object";
    }
}

// tsc's verifyCompilerOptions on `paths`, as the config writes them (createDiagnosticForOptionPaths and
// createDiagnosticForOptionPathKeyValue: on the config's `paths`, else on `compilerOptions`)
static void program_validate_paths(Program* me) {
    JsonMember const* options = config_compiler_options(me);
    if (!options || options->value.tag != JSON_OBJECT) return;
    for (isize p = 0; p < options->value.count; p++) {
        JsonMember const* paths = &options->value.members[p];
        if (!cstr_equals(&paths->key, "paths") || paths->value.tag != JSON_OBJECT) continue;
        if (json_get(&options->value, "paths") != &paths->value) continue; // (The last one is the option)
        for (isize i = 0; i < paths->value.count; i++) {
            JsonMember const* pattern = &paths->value.members[i];
            JsonValue const* substitutions = &pattern->value;
            // (tsc checks the converted options: a pattern written again later is that one)
            bool overridden = false;
            for (isize j = i + 1; j < paths->value.count && !overridden; j++) {
                overridden = cstr_eq(&paths->value.members[j].key, &pattern->key);
            }
            if (overridden) continue;
            if (count_asterisks(cstr_sv(&pattern->key)) > 1) {
                error_ctx_error_d(my errs, config_span(me, pattern->key_start, pattern->key_end), 5061, "Pattern '",
                    pattern->key, "' can have at most one '*' character.");
            }
            Span value_span = config_span(me, substitutions->start, substitutions->end);
            if (substitutions->tag != JSON_ARRAY) {
                error_ctx_error_d(my errs, value_span, 5063, "Substitutions for pattern '", pattern->key,
                    "' should be an array.");
                continue;
            }
            if (!substitutions->count) {
                error_ctx_error_d(my errs, value_span, 5066, "Substitutions for pattern '", pattern->key,
                    "' shouldn't be an empty array.");
            }
            for (isize j = 0; j < substitutions->count; j++) {
                JsonValue const* substitution = &substitutions->items[j];
                Span at = config_span(me, substitution->start, substitution->end);
                if (substitution->tag != JSON_STRING) {
                    // (`${subst}`: the number or boolean as written)
                    csview written = at.sv;
                    error_ctx_error_d(my errs, at, 5064, "Substitution '", written, "' for pattern '", pattern->key,
                        "' has incorrect type, expected 'string', got '", json_typeof(substitution), "'.");
                    continue;
                }
                if (count_asterisks(cstr_sv(&substitution->string)) > 1) {
                    error_ctx_error_d(my errs, at, 5062, "Substitution '", substitution->string, "' in pattern '",
                        pattern->key, "' can have at most one '*' character.");
                }
                if (!my options.base_url_set && !path_is_relative_or_absolute(cstr_sv(&substitution->string))) {
                    error_ctx_error_d(my errs, at, 5090,
                        "Non-relative paths are not allowed when 'baseUrl' is not set. Did you forget a leading './'?");
                }
            }
        }
    }
}

// "5.4" as 5 * 1000 + 4, 0 if it isn't a version
static unsigned parse_major_minor(csview version) {
    unsigned major = 0, minor = 0;
    isize i = 0;
    while (i < version.size && version.buf[i] >= '0' && version.buf[i] <= '9') major = major * 10 + (unsigned)(version.buf[i++] - '0');
    if (i < version.size && version.buf[i] == '.') i++;
    while (i < version.size && version.buf[i] >= '0' && version.buf[i] <= '9') minor = minor * 10 + (unsigned)(version.buf[i++] - '0');
    return major * 1000 + minor;
}

// tsc's verifyCompilerOptions(), the part that doesn't depend on emit. With a tsconfig the diagnostics are on the
// option there (option_sites())
static void program_validate_options(Program* me) {
    CompilerOptions const* o = &my options;
    // tsc's verifyDeprecatedCompilerOptions: deprecated in 5.0, removed in 5.5. Before that (the harness's
    // `@typeScriptVersion`) they're only deprecated, which `"ignoreDeprecations": "5.0"` silences
    unsigned version = parse_major_minor(cstr_size(&o->typescript_version) ? cstr_sv(&o->typescript_version) : c_sv("6.0"));
    unsigned ignore_version = 0;
    if (cstr_size(&o->ignore_deprecations)) {
        if (cstr_equals(&o->ignore_deprecations, "5.0") || cstr_equals(&o->ignore_deprecations, "6.0")) {
            ignore_version = parse_major_minor(cstr_sv(&o->ignore_deprecations));
        } else {
            option_error_d(me, "ignoreDeprecations", nullptr, false, 5103, "Invalid value for '--ignoreDeprecations'.");
        }
    }
    // The top of tsc's verifyCompilerOptions
    // (As written: the strict family's defaults don't count)
    if (o->is_set[COMPILER_OPTION_strict_property_initialization] && o->strict_property_initialization &&
        !o->strict_null_checks
    ) {
        option_error_d(me, "strictPropertyInitialization", "strictNullChecks", true, 5052,
            "Option 'strictPropertyInitialization' cannot be specified without specifying option 'strictNullChecks'.");
    }
    if (o->exact_optional_property_types && !o->strict_null_checks) {
        option_error_d(me, "exactOptionalPropertyTypes", "strictNullChecks", true, 5052,
            "Option 'exactOptionalPropertyTypes' cannot be specified without specifying option 'strictNullChecks'.");
    }
    if ((o->isolated_modules || o->verbatim_module_syntax) && cstr_size(&o->out_file)) {
        const char* with = o->verbatim_module_syntax ? "verbatimModuleSyntax" : "isolatedModules";
        option_error_d(me, "outFile", with, true, 5053, "Option 'outFile' cannot be specified with option '", with, "'.");
    }
    if (o->isolated_declarations && o->allow_js) {
        option_error_d(me, "allowJs", "isolatedDeclarations", true, 5053,
            "Option 'allowJs' cannot be specified with option 'isolatedDeclarations'.");
    }
    if (o->inline_source_map) {
        if (o->source_map) {
            option_error_d(me, "sourceMap", "inlineSourceMap", true, 5053,
                "Option 'sourceMap' cannot be specified with option 'inlineSourceMap'.");
        }
        if (cstr_size(&o->map_root)) {
            option_error_d(me, "mapRoot", "inlineSourceMap", true, 5053,
                "Option 'mapRoot' cannot be specified with option 'inlineSourceMap'.");
        }
    }
    if (o->composite) {
        if (o->is_set[COMPILER_OPTION_declaration] && !o->declaration) {
            option_error_d(me, "declaration", nullptr, true, 6304, "Composite projects may not disable declaration emit.");
        }
        if (o->is_set[COMPILER_OPTION_incremental] && !o->incremental) {
            option_error_d(me, "declaration", nullptr, true, 6379, "Composite projects may not disable incremental compilation.");
        }
    }
    if (!cstr_size(&o->ts_build_info_file) && o->incremental && !cstr_size(&o->out_file) && !my config_source) {
        error_ctx_global_error_d(my errs, 5074, "Option '--incremental' can only be specified using tsconfig, emitting to ",
            "single file or when option '--tsBuildInfoFile' is specified.");
    }
    program_validate_paths(me);
    bool removed = version >= parse_major_minor(c_sv("5.5"));
    bool deprecated = !removed && ignore_version < parse_major_minor(c_sv("5.0"));
    if (o->target_es3 && removed) {
        option_error_d(me, "target", nullptr, false, 5108,
            "Option 'target=ES3' has been removed. Please remove it from your configuration.");
    } else if (o->target_es3 && deprecated) {
        option_error_d(me, "target", nullptr, false, 5107, "Option 'target=ES3' is deprecated and will stop functioning ",
            "in TypeScript 5.5. Specify compilerOption '\"ignoreDeprecations\": \"5.0\"' to silence this error.");
    }
    for (isize i = 0; i < o->removed_options.count && (removed || deprecated); i++) {
        cstr const* name = &o->removed_options.items[i];
        // (6.0 no longer says to use outFile instead of out: outFile is deprecated too)
        const char* instead = cstr_equals(name, "importsNotUsedAsValues") || cstr_equals(name, "preserveValueImports") ?
            "verbatimModuleSyntax" : nullptr;
        Span sites[4];
        u32 site_count = option_sites(me, cstr_str(name), nullptr, true, sites);
        for (u32 j = 0; j < site_count; j++) {
            Error* error = removed ?
                option_error_d_one(me, sites[j], 5102, "Option '", *name,
                    "' has been removed. Please remove it from your configuration.") :
                option_error_d_one(me, sites[j], 5101, "Option '", *name, "' is deprecated and will stop functioning in ",
                    "TypeScript 5.5. Specify compilerOption '\"ignoreDeprecations\": \"5.0\"' to silence this error.");
            if (instead) error_sub_error_d(error, span_null, TS_CODE_NONE, "Use '", instead, "' instead.");
        }
    }
    // The 6.0 deprecations (removed in 7.0), "ignoreDeprecations": "6.0" silences them
    bool removed7 = version >= parse_major_minor(c_sv("7.0"));
    bool deprecated6 = !removed7 && ignore_version < parse_major_minor(c_sv("6.0"));
    if (removed7 || deprecated6) {
        struct { char const* name; char const* value; bool given; bool visit; } const deps[] = {
            { "alwaysStrict", "false", o->is_set[COMPILER_OPTION_always_strict] && !o->always_strict, false },
            { "target", "ES5", o->target_set && o->target == SCRIPT_TARGET_ES5, false },
            { "moduleResolution", "node10", o->module_resolution_set && o->module_resolution == MODULE_RESOLUTION_NODE10, true },
            { "moduleResolution", "classic", o->module_resolution_set && o->module_resolution == MODULE_RESOLUTION_CLASSIC, false },
            { "baseUrl", nullptr, o->base_url_set, true },
            { "esModuleInterop", "false", o->is_set[COMPILER_OPTION_es_module_interop] && !o->es_module_interop, false },
            { "allowSyntheticDefaultImports", "false",
                o->is_set[COMPILER_OPTION_allow_synthetic_default_imports] && !o->allow_synthetic_default_imports, false },
            { "outFile", nullptr, cstr_size(&o->out_file) > 0, false },
            { "module", o->module == MODULE_KIND_NONE ? "None" : o->module == MODULE_KIND_AMD ? "AMD" :
                o->module == MODULE_KIND_UMD ? "UMD" : "System",
                o->module_set && (o->module == MODULE_KIND_NONE || o->module == MODULE_KIND_AMD ||
                    o->module == MODULE_KIND_UMD || o->module == MODULE_KIND_SYSTEM), false },
            { "downlevelIteration", nullptr, o->is_set[COMPILER_OPTION_downlevel_iteration], false },
        };
        for (u32 i = 0; i < c_arraylen(deps); i++) {
            if (!deps[i].given) continue;
            Span sites[4];
            u32 site_count = option_sites(me, deps[i].name, nullptr, !deps[i].value, sites);
            for (u32 j = 0; j < site_count; j++) {
                Error* error;
                if (deps[i].value) {
                    error = removed7 ?
                        option_error_d_one(me, sites[j], 5108, "Option '", deps[i].name, "=", deps[i].value,
                            "' has been removed. Please remove it from your configuration.") :
                        option_error_d_one(me, sites[j], 5107, "Option '", deps[i].name, "=", deps[i].value,
                            "' is deprecated and will stop functioning in TypeScript 7.0. Specify compilerOption ",
                            "'\"ignoreDeprecations\": \"6.0\"' to silence this error.");
                } else {
                    error = removed7 ?
                        option_error_d_one(me, sites[j], 5102, "Option '", deps[i].name,
                            "' has been removed. Please remove it from your configuration.") :
                        option_error_d_one(me, sites[j], 5101, "Option '", deps[i].name,
                            "' is deprecated and will stop functioning in TypeScript 7.0. Specify compilerOption ",
                            "'\"ignoreDeprecations\": \"6.0\"' to silence this error.");
                }
                if (deps[i].visit) {
                    error_sub_error_d(error, span_null, TS_CODE_NONE, "Visit https://aka.ms/ts6 for migration information.");
                }
            }
        }
    }
    if (!o->source_map && !o->inline_source_map) {
        if (o->inline_sources) {
            option_error_d(me, "inlineSources", nullptr, true, 5051, "Option 'inlineSources can only be used when either ",
                "option '--inlineSourceMap' or option '--sourceMap' is provided.");
        }
        if (cstr_size(&o->source_root)) {
            option_error_d(me, "sourceRoot", nullptr, true, 5051, "Option 'sourceRoot can only be used when either ",
                "option '--inlineSourceMap' or option '--sourceMap' is provided.");
        }
    }
    if (cstr_size(&o->map_root) && !o->source_map && !o->declaration_map) {
        option_error_d(me, "mapRoot", "sourceMap", true, 5069, "Option 'mapRoot' cannot be specified without specifying ",
            "option 'sourceMap' or option 'declarationMap'.");
    }
    if (o->is_set[COMPILER_OPTION_no_lib] && o->no_lib && cstr_size(&o->lib)) {
        option_error_d(me, "lib", "noLib", true, 5053, "Option 'lib' cannot be specified with option 'noLib'.");
    }
    if ((o->isolated_modules || o->verbatim_module_syntax) && o->is_set[COMPILER_OPTION_preserve_const_enums] &&
        !o->preserve_const_enums
    ) {
        const char* with = o->verbatim_module_syntax ? "verbatimModuleSyntax" : "isolatedModules";
        option_error_d(me, with, "preserveConstEnums", true, 5091, "Option 'preserveConstEnums' cannot be disabled when '",
            with, "' is enabled.");
    }
    if (o->verbatim_module_syntax &&
        (o->module == MODULE_KIND_AMD || o->module == MODULE_KIND_UMD || o->module == MODULE_KIND_SYSTEM)
    ) {
        option_error_d(me, "verbatimModuleSyntax", nullptr, true, 5105,
            "Option 'verbatimModuleSyntax' cannot be used when 'module' is set to 'UMD', 'AMD', or 'System'.");
    }
    if (o->resolve_json_module) {
        if (o->module_resolution == MODULE_RESOLUTION_CLASSIC) {
            option_error_d(
                me, "resolveJsonModule", nullptr, true, 5070,
                "Option '--resolveJsonModule' cannot be specified when 'moduleResolution' is set to 'classic'."
            );
        } else if (o->module == MODULE_KIND_NONE || o->module == MODULE_KIND_SYSTEM || o->module == MODULE_KIND_UMD) {
            option_error_d(
                me, "resolveJsonModule", "module", true, 5071,
                "Option '--resolveJsonModule' cannot be specified when 'module' is set to 'none', 'system', or 'umd'."
            );
        }
    }
    if (o->allow_importing_ts_extensions && !(o->no_emit || o->emit_declaration_only || o->rewrite_relative_import_extensions)) {
        option_error_d(me, "allowImportingTsExtensions", nullptr, false, 5096,
            "Option 'allowImportingTsExtensions' can only be used when one of 'noEmit', 'emitDeclarationOnly', or 'rewriteRelativeImportExtensions' is set.");
    }
    bool modern_resolution = o->module_resolution >= MODULE_RESOLUTION_NODE16;
    static const char* const needs_modern[] = { "resolvePackageJsonExports", "resolvePackageJsonImports", "customConditions" };
    bool const given[] = {
        o->is_set[COMPILER_OPTION_resolve_package_json_exports] && o->resolve_package_json_exports,
        o->is_set[COMPILER_OPTION_resolve_package_json_imports] && o->resolve_package_json_imports,
        o->custom_conditions.count > 0,
    };
    for (isize i = 0; i < 3 && !modern_resolution; i++) {
        if (given[i]) option_error_d(
            me, needs_modern[i], nullptr, true, 5098, "Option '", needs_modern[i],
            "' can only be used when 'moduleResolution' is set to 'node16', 'nodenext', or 'bundler'."
        );
    }
    bool node_module = o->module >= MODULE_KIND_NODE16 && o->module <= MODULE_KIND_NODENEXT;
    bool node_resolution = o->module_resolution == MODULE_RESOLUTION_NODE16 ||
        o->module_resolution == MODULE_RESOLUTION_NODENEXT;
    bool es_module = o->module >= MODULE_KIND_ES2015 && o->module <= MODULE_KIND_ESNEXT;
    if (o->module_resolution == MODULE_RESOLUTION_BUNDLER && !es_module && o->module != MODULE_KIND_PRESERVE &&
        o->module != MODULE_KIND_COMMONJS
    ) {
        option_error_d(
            me, "moduleResolution", nullptr, false, 5095,
            "Option 'bundler' can only be used when 'module' is set to 'preserve', 'commonjs', or 'es2015' or later."
        );
    }
    if (node_module && !node_resolution) {
        const char* module_name = o->module == MODULE_KIND_NODE16 ? "Node16" :
            o->module == MODULE_KIND_NODE18 ? "Node18" : o->module == MODULE_KIND_NODE20 ? "Node20" : "NodeNext";
        option_error_d(
            me, "moduleResolution", nullptr, false, 5109, "Option 'moduleResolution' must be set to '",
            o->module == MODULE_KIND_NODENEXT ? "NodeNext" : "Node16",
            "' (or left unspecified) when option 'module' is set to '", module_name, "'."
        );
    } else if (node_resolution && !node_module) {
        const char* name = o->module_resolution == MODULE_RESOLUTION_NODE16 ? "Node16" : "NodeNext";
        option_error_d(
            me, "module", nullptr, false, 5110, "Option 'module' must be set to '", name, "' when option 'moduleResolution' is set to '",
            name, "'."
        );
    }
    // Declaration emit options without declaration emit
    bool declarations = o->declaration || o->composite;
    const char* needs_declarations[3] = {
        o->isolated_declarations ? "isolatedDeclarations" : nullptr,
        cstr_size(&o->declaration_dir) ? "declarationDir" : nullptr,
        o->declaration_map ? "declarationMap" : nullptr,
    };
    for (isize i = 0; i < 3 && !declarations; i++) {
        if (needs_declarations[i]) option_error_d(me, needs_declarations[i], "declaration", true, 5069, "Option '",
            needs_declarations[i], "' cannot be specified without specifying option 'declaration' or option 'composite'.");
    }
    if (cstr_size(&o->out_file) && !o->emit_declaration_only && o->module_set && o->module != MODULE_KIND_NONE &&
        o->module != MODULE_KIND_AMD && o->module != MODULE_KIND_SYSTEM
    ) {
        option_error_d(me, "outFile", "module", true, 6082, "Only 'amd' and 'system' modules are supported alongside --outFile.");
    }
    if (o->emit_declaration_only && !declarations) {
        option_error_d(me, "emitDeclarationOnly", "declaration", true, 5069,
            "Option 'emitDeclarationOnly' cannot be specified without specifying option 'declaration' or option 'composite'.");
    }
    if (o->emit_decorator_metadata && !o->experimental_decorators) {
        option_error_d(
            me, "emitDecoratorMetadata", "experimentalDecorators", true, 5052,
            "Option 'emitDecoratorMetadata' cannot be specified without specifying option 'experimentalDecorators'."
        );
    }
    // The JSX factories (tsc's verifyCompilerOptions)
    bool automatic = o->jsx == JSX_REACT_JSX || o->jsx == JSX_REACT_JSXDEV;
    const char* jsx_name = o->jsx == JSX_REACT_JSX ? "react-jsx" : o->jsx == JSX_REACT_JSXDEV ? "react-jsxdev" : "react";
    if (cstr_size(&o->jsx_factory)) {
        if (cstr_size(&o->react_namespace)) {
            option_error_d(me, "reactNamespace", "jsxFactory", true, 5053, "Option 'reactNamespace' cannot be specified with option 'jsxFactory'.");
        }
        if (automatic) {
            option_error_d(me, "jsxFactory", jsx_name, true, 5089, "Option 'jsxFactory' cannot be specified when option 'jsx' is '",
                jsx_name, "'.");
        }
        if (!compiler_option_is_entity_name(cstr_sv(&o->jsx_factory))) {
            option_error_d(me, "jsxFactory", nullptr, false, 5067, "Invalid value for 'jsxFactory'. '", o->jsx_factory,
                "' is not a valid identifier or qualified-name.");
        }
    } else if (cstr_size(&o->react_namespace) && !compiler_option_is_identifier(cstr_sv(&o->react_namespace))) {
        option_error_d(me, "reactNamespace", nullptr, false, 5059, "Invalid value for '--reactNamespace'. '", o->react_namespace,
            "' is not a valid identifier.");
    }
    if (cstr_size(&o->jsx_fragment_factory)) {
        if (!cstr_size(&o->jsx_factory)) {
            option_error_d(me, "jsxFragmentFactory", "jsxFactory", true, 5052,
                "Option 'jsxFragmentFactory' cannot be specified without specifying option 'jsxFactory'.");
        }
        if (automatic) {
            option_error_d(me, "jsxFragmentFactory", jsx_name, true, 5089, "Option 'jsxFragmentFactory' cannot be specified when option 'jsx' is '",
                jsx_name, "'.");
        }
        if (!compiler_option_is_entity_name(cstr_sv(&o->jsx_fragment_factory))) {
            option_error_d(me, "jsxFragmentFactory", nullptr, false, 18035, "Invalid value for 'jsxFragmentFactory'. '", o->jsx_fragment_factory,
                "' is not a valid identifier or qualified-name.");
        }
    }
    if (cstr_size(&o->react_namespace) && automatic) {
        option_error_d(me, "reactNamespace", jsx_name, true, 5089, "Option 'reactNamespace' cannot be specified when option 'jsx' is '",
            jsx_name, "'.");
    }
    if (cstr_size(&o->jsx_import_source) && o->jsx == JSX_REACT) {
        option_error_d(me, "jsxImportSource", jsx_name, true, 5089, "Option 'jsxImportSource' cannot be specified when option 'jsx' is '",
            jsx_name, "'.");
    }
}

// tsc's getOutputExtension and getDeclarationEmitExtensionForPath
static const char* emit_js_extension(Program const* me, csview path) {
    if (csview_ends_with(path, ".json")) return ".json";
    if (my options.jsx == JSX_PRESERVE && (csview_ends_with(path, ".jsx") || csview_ends_with(path, ".tsx"))) return ".jsx";
    if (csview_ends_with(path, ".mts") || csview_ends_with(path, ".mjs")) return ".mjs";
    if (csview_ends_with(path, ".cts") || csview_ends_with(path, ".cjs")) return ".cjs";
    return ".js";
}

static const char* emit_declaration_extension(csview path) {
    if (csview_ends_with(path, ".mts") || csview_ends_with(path, ".mjs")) return ".d.mts";
    if (csview_ends_with(path, ".cts") || csview_ends_with(path, ".cjs")) return ".d.cts";
    if (csview_ends_with(path, ".json")) return ".d.json.ts";
    return ".d.ts";
}

// tsc's changeExtension: the source extensions are replaced, anything else is appended to
static cstr emit_change_extension(csview path, const char* extension) {
    static const char* const known[] = { ".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs", ".json" };
    for (isize i = 0; i < c_countof(known); i++) {
        if (csview_ends_with(path, known[i])) {
            path = csview_slice(path, 0, path.size - (isize)strlen(known[i]));
            break;
        }
    }
    return cstr_d(path, extension);
}

// Where tsc writes `path`'s output (getOwnEmitOutputFilePath): next to it, or at the same place relative to
// `common_dir` (an absolute directory) under `out_dir`
static cstr emit_output_path(Program const* me, csview path, csview out_dir, csview common_dir, const char* extension) {
    if (!out_dir.size) return emit_change_extension(path, extension);
    cstr cwd = cstr_null;
    try (fs_get_cwd(my fs, &cwd)) or_panic;
    cstr absolute = absolute_path_of(cstr_sv(&cwd), path);
    csview rest = cstr_sv(&absolute);
    bool inside = rest.size >= common_dir.size && !memcmp(rest.buf, common_dir.buf, (size_t)common_dir.size) &&
        (rest.size == common_dir.size || !common_dir.size || common_dir.buf[common_dir.size - 1] == '/' || rest.buf[common_dir.size] == '/');
    if (inside) rest = csview_slice(rest, common_dir.size, rest.size);
    while (inside && rest.size && rest.buf[0] == '/') rest = csview_slice(rest, 1, rest.size);
    // (tsc's getSourceFilePathInNewDirWorker: a file outside the common directory keeps its absolute path, which
    // combinePaths keeps as it is rooted)
    cstr joined = inside ? cstr_d(out_dir, out_dir.buf[out_dir.size - 1] == '/' ? "" : "/", rest) : cstr_from_sv(rest);
    cstr ret = emit_change_extension(cstr_sv(&joined), extension);
    c_drop(cstr, &cwd, &absolute, &joined);
    return ret;
}

// tsc's sourceFileMayBeEmitted
static bool emit_source_may_be_emitted(Program const* me, SourceFile const* source) {
    if (source->is_declaration || source->is_stdlib || source->from_reference) return false;
    csview path = cstr_sv(&source->canonical_path);
    if (csview_starts_with(path, "node_modules/") || csview_contains(path, "/node_modules/")) return false;
    if (!source->is_json) return true;
    if (cstr_size(&my options.out_file)) return true;
    if (!cstr_size(&my options.out_dir)) return false;
    // Not when it would be written over itself, if the common source directory is known up front
    CompilerOptions const* o = &my options;
    if (!cstr_size(&o->root_dir) && !cstr_size(&o->config_dir)) return true;
    csview cwd = program_cwd(me);
    cstr common = cstr_size(&o->root_dir) ? absolute_path_of(cwd, cstr_sv(&o->root_dir)) : cstr_clone(o->config_dir);
    cstr absolute = absolute_path_of(cwd, cstr_sv(&source->canonical_path));
    // (tsc's getSourceFilePathInNewDirWorker: a file outside the common directory keeps its absolute path, so its
    // output is itself)
    bool same = true;
    if (cstr_starts_with_sv(&absolute, cstr_sv(&common))) {
        cstr joined = cstr_d(o->out_dir, "/", csview_slice(cstr_sv(&absolute), cstr_size(&common), cstr_size(&absolute)));
        cstr output = absolute_path_of(cwd, cstr_sv(&joined));
        same = cstr_eq(&output, &absolute);
        c_drop(cstr, &joined, &output);
    }
    c_drop(cstr, &common, &absolute);
    return !same;
}

// Reports one output path (tsc's verifyEmitFilePath): TS5055 if it is an input file, TS5056 the second time
static void emit_verify_path(Program* me, cstr output, hmap_source_file_ptr* seen) {
    if (program_find_source(me, cstr_sv(&output))) {
        Error* error = error_ctx_global_error_d(
            my errs, 5055, "Cannot write file '", output, "' because it would overwrite input file."
        );
        if (!cstr_size(&my options.config_dir)) {
            error_sub_error_d(error, span_null, TS_CODE_NONE,
                "Adding a tsconfig.json file will help organize projects that contain both TypeScript and JavaScript "
                "files. Learn more at https://aka.ms/tsconfig.");
        }
    }
    cstr cwd = cstr_null;
    try (fs_get_cwd(my fs, &cwd)) or_panic;
    cstr key = relative_path_of(cstr_sv(&cwd), cstr_sv(&output));
    cstr_drop(&cwd);
    if (hmap_source_file_ptr_get(seen, cstr_sv(&key))) {
        error_ctx_global_error_d(
            my errs, 5056, "Cannot write file '", output, "' because it would be overwritten by multiple input files."
        );
        cstr_drop(&key);
    } else {
        hmap_source_file_ptr_insert(seen, key, nullptr);
    }
    cstr_drop(&output);
}

// The part of tsc's verifyCompilerOptions that blames the first module (tsc's firstNonAmbientExternalModuleSourceFile):
// modules need a module kind (TS1148), and one that can be bundled into an outFile (TS6131)
static void program_verify_module_kind(Program* me) {
    CompilerOptions const* o = &my options;
    SourceFile const* first = nullptr;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile const* source = *i;
        if (source->is_module && !source->is_commonjs && !source->is_json && !source->is_declaration && !source->is_stdlib) {
            first = source;
            break;
        }
    }
    // (tsc's indicator of a module by moduleDetection has a span of its own: not reported)
    if (!first || !span_valid(first->module_indicator)) return;
    bool isolated = o->isolated_modules || o->verbatim_module_syntax;
    if (!isolated && o->target < SCRIPT_TARGET_ES2015 && o->module_set && o->module == MODULE_KIND_NONE) {
        error_ctx_error_d(my errs, first->module_indicator, 1148,
            "Cannot use imports, exports, or module augmentations when '--module' is 'none'.");
    }
    if (cstr_size(&o->out_file) && !o->emit_declaration_only && !o->module_set) {
        error_ctx_error_d(my errs, first->module_indicator, 6131,
            "Cannot compile modules using option 'outFile' unless the '--module' flag is 'amd' or 'system'.");
    }
}

// The end of tsc's verifyCompilerOptions: no output file may overwrite an input file or another output file
// The root of a file's name as tsc has it: `c:/` of a DOS drive path, else `/` (a relative name is in the current
// directory)
static csview path_root_of(csview path) {
    if (!path_is_drive_rooted(path)) return c_sv("/");
    return csview_slice(path, 0, path.size > 2 ? 3 : 2);
}

// tsc's computeCommonSourceDirectoryOfFilenames on the files that may be emitted (getComputedCommonSourceDirectory),
// empty without any
static cstr computed_common_dir(Program* me, csview cwd) {
    cstr common_dir = cstr_null;
    bool first = true;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        if (!emit_source_may_be_emitted(me, *i)) continue;
        cstr absolute = absolute_path_of(cwd, cstr_sv(&(*i)->canonical_path));
        csview dir = dirname_of(cstr_sv(&absolute));
        if (first) {
            cstr_assign_sv(&common_dir, dir);
            first = false;
        } else {
            // The longest common prefix of whole components
            csview common = cstr_sv(&common_dir);
            isize n = 0, last_sep = 0;
            while (n < common.size && n < dir.size && common.buf[n] == dir.buf[n]) {
                if (common.buf[n] == '/') last_sep = n;
                n++;
            }
            bool whole = (n == common.size || common.buf[n] == '/') && (n == dir.size || dir.buf[n] == '/');
            cstr_resize(&common_dir, whole ? n : last_sep ? last_sep : 1, '\0');
        }
        cstr_drop(&absolute);
    }
    return common_dir;
}

// tsc's verifyCompilerOptions, 6.0: a project's output layout follows its config's directory where 5.x inferred the
// common directory of its files, so where those differ the rootDir has to be said (TS5011)
static void program_verify_inferred_root_dir(Program* me) {
    CompilerOptions const* o = &my options;
    if (o->no_emit || o->composite || cstr_size(&o->root_dir) || !my config_source || !cstr_size(&o->config_dir)) return;
    bool declaration_dir = (o->declaration || o->composite) && cstr_size(&o->declaration_dir);
    if (!cstr_size(&o->out_dir) && !declaration_dir && !cstr_size(&o->out_file)) return;
    csview cwd = program_cwd(me);
    cstr dir59 = computed_common_dir(me, cwd);
    cstr config_dir = absolute_path_of(cwd, cstr_sv(&o->config_dir));
    cstr relative = cstr_size(&dir59) ? relative_path_of(cstr_sv(&config_dir), cstr_sv(&dir59)) : cstr_null;
    bool same = !cstr_size(&relative) || cstr_equals(&relative, ".") ||
        (o->case_insensitive_file_names && csview_iequals_sv(cstr_sv(&dir59), cstr_sv(&config_dir)));
    if (!same) {
        cstr config = absolute_path_of(cwd, cstr_sv(&my config_source->canonical_path));
        csview config_name = cstr_sv(&config);
        for (isize i = config_name.size; i > 0; i--) {
            if (config_name.buf[i - 1] == '/') {
                config_name = csview_slice(config_name, i, config_name.size);
                break;
            }
        }
        // (getRelativePathFromFile: ensurePathIsNonModuleName)
        while (cstr_size(&relative) > 1 && cstr_ends_with(&relative, "/")) cstr_resize(&relative, cstr_size(&relative) - 1, '\0');
        if (!cstr_starts_with(&relative, ".") && !cstr_starts_with(&relative, "/")) cstr_insert(&relative, 0, "./");
        char const* name = cstr_size(&o->out_file) ? "outFile" : cstr_size(&o->out_dir) ? "outDir" : "declarationDir";
        char const* name2 = !cstr_size(&o->out_file) && cstr_size(&o->out_dir) ? "declarationDir" : nullptr;
        Span sites[4];
        u32 count = option_sites(me, name, name2, true, sites);
        for (u32 i = 0; i < count; i++) {
            Error* error = option_error_d_one(me, sites[i], 5011, "The common source directory of '", config_name, "' is '",
                relative, "'. The 'rootDir' setting must be explicitly set to this or another path to adjust your output's "
                "file layout.");
            error_sub_error_d(error, span_null, TS_CODE_NONE, "Visit https://aka.ms/ts6 for migration information.");
        }
        cstr_drop(&config);
    }
    c_drop(cstr, &dir59, &config_dir, &relative);
}

// tsc's getCommonSourceDirectory (absolute): the rootDir, a project's directory (6.0), else the longest directory every
// emitted file is in
static cstr program_common_source_dir(Program* me) {
    CompilerOptions const* o = &my options;
    cstr cwd = cstr_null;
    try (fs_get_cwd(my fs, &cwd)) or_panic;
    cstr common_dir = cstr_null;
    if (cstr_size(&o->root_dir)) {
        common_dir = absolute_path_of(cstr_sv(&cwd), cstr_sv(&o->root_dir));
    } else if (cstr_size(&o->config_dir)) {
        // (A project never infers its root from its files, 6.0: TS5011 says where 5.x put it)
        common_dir = cstr_clone(o->config_dir);
    } else {
        common_dir = computed_common_dir(me, cstr_sv(&cwd));
    }
    cstr_drop(&cwd);
    return common_dir;
}

static void program_verify_emit_paths(Program* me) {
    CompilerOptions const* o = &my options;
    // tsc's verifyCompilerOptions: with an outDir and no rootDir, files on different drives have no common source
    // directory (getCommonSourceDirectory is "", which only another root than `/` can make)
    if (cstr_size(&o->out_dir) && !cstr_size(&o->root_dir) && !cstr_size(&o->config_dir)) {
        csview root = {0};
        bool differ = false, drive = false;
        for_each (i, vec_source_file_ptr, &my source_files_ordered) {
            if (!emit_source_may_be_emitted(me, *i)) continue;
            csview file_root = path_root_of(source_file_display_path(*i));
            drive |= file_root.size > 1;
            if (!root.buf) root = file_root;
            else differ |= o->case_insensitive_file_names ? !csview_iequals_sv(root, file_root) : !csview_equals_sv(root, file_root);
        }
        if (differ && drive) {
            option_error_d(me, "outDir", nullptr, true, 5009, "Cannot find the common subdirectory path for the input files.");
        }
    }
    if (o->no_emit || o->suppress_output_path_check) return;
    bool declarations = o->declaration || o->composite;
    hmap_source_file_ptr seen = {0};
    if (cstr_size(&o->out_file)) {
        // One bundle: of the scripts, and of modules too with AMD/System (tsc's getSourceFilesToEmit)
        bool modules = o->emit_declaration_only || o->module == MODULE_KIND_AMD || o->module == MODULE_KIND_SYSTEM;
        bool any = false;
        for_each (i, vec_source_file_ptr, &my source_files_ordered) {
            any |= emit_source_may_be_emitted(me, *i) && (modules || !(*i)->is_module);
        }
        if (any) {
            if (!o->emit_declaration_only) emit_verify_path(me, cstr_clone(o->out_file), &seen);
            if (declarations) {
                emit_verify_path(me, emit_change_extension(cstr_sv(&o->out_file), ".d.ts"), &seen);
            }
        }
        hmap_source_file_ptr_drop(&seen);
        return;
    }
    // tsc's getCommonSourceDirectory, only needed with an outDir or declarationDir
    cstr common_dir = cstr_size(&o->out_dir) || cstr_size(&o->declaration_dir) ? program_common_source_dir(me) : cstr_null;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile const* source = *i;
        if (!emit_source_may_be_emitted(me, source)) continue;
        csview path = source_file_display_path(source);
        if (!o->emit_declaration_only) {
            cstr js = emit_output_path(me, path, cstr_sv(&o->out_dir), cstr_sv(&common_dir), emit_js_extension(me, path));
            // A JSON file that would be written over itself isn't written
            if (source->is_json && program_find_source(me, cstr_sv(&js)) == source) {
                cstr_drop(&js);
            } else {
                emit_verify_path(me, js, &seen);
            }
        }
        if (declarations && !source->is_json) {
            csview dir = cstr_size(&o->declaration_dir) ? cstr_sv(&o->declaration_dir) : cstr_sv(&o->out_dir);
            cstr dts = emit_output_path(me, path, dir, cstr_sv(&common_dir), emit_declaration_extension(path));
            emit_verify_path(me, dts, &seen);
        }
    }
    cstr_drop(&common_dir);
    hmap_source_file_ptr_drop(&seen);
}

// tsc's addLazyProgramDiagnosticExplainingFile: at what first brought the file in (an import), else (a root file) a
// global diagnostic saying why it is in the program
static void program_error_explaining_file(Program* me, SourceFile const* file, unsigned code, cstr message) {
    if (span_valid(file->included_at)) {
        error_ctx_error_s(my errs, file->included_at, code, message);
        return;
    }
    // (TODO: tsc says why it is: "The file is in the program because: Matched by default include pattern '**/*'")
    error_ctx_global_error_s(my errs, code, message);
}

// tsc's verifyCompilerOptions on the files of the program: a composite project lists every file it emits (TS6307),
// and with a rootDir (else the config's directory, 6.0) every emitted file is in it (TS6059, from
// getCommonSourceDirectory's checkSourceFilesBelongToPath)
static void program_verify_project_files(Program* me) {
    CompilerOptions const* o = &my options;
    bool has_config = my config_source && cstr_size(&o->config_dir);
    bool common_dir = cstr_size(&o->out_dir) || cstr_size(&o->root_dir) || cstr_size(&o->source_root) ||
        cstr_size(&o->map_root) || ((o->declaration || o->composite) && cstr_size(&o->declaration_dir));
    bool root_dir_check = common_dir && (cstr_size(&o->root_dir) || has_config);
    bool list_check = o->composite && has_config;
    if (!root_dir_check && !list_check) return;
    csview cwd = program_cwd(me);
    cstr root_dir = cstr_size(&o->root_dir) ? absolute_path_of(cwd, cstr_sv(&o->root_dir)) : cstr_clone(o->config_dir);
    cstr config = my config_source ? absolute_path_of(cwd, cstr_sv(&my config_source->canonical_path)) : cstr_null;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile const* file = *i;
        if (!emit_source_may_be_emitted(me, file)) continue;
        cstr absolute = absolute_path_of(cwd, cstr_sv(&file->canonical_path));
        csview path = cstr_sv(&absolute);
        csview root = cstr_sv(&root_dir);
        bool under = path.size > root.size && !memcmp(path.buf, root.buf, (size_t)root.size) &&
            (path.buf[root.size] == '/' || (root.size == 1 && root.buf[0] == '/'));
        if (root_dir_check && !under) {
            program_error_explaining_file(me, file, 6059, cstr_d("File '", absolute, "' is not under 'rootDir' '",
                root_dir, "'. 'rootDir' is expected to contain all source files."));
        }
        if (list_check && !file->is_root) {
            program_error_explaining_file(me, file, 6307, cstr_d("File '", absolute, "' is not listed within the file "
                "list of project '", config, "'. Projects must list all files or use an 'include' pattern."));
        }
        cstr_drop(&absolute);
    }
    cstr_drop(&config);
    cstr_drop(&root_dir);
}

// tsc's getDiagnosticsWithPrecedingDirectives: a semantic diagnostic (of `phase`, resolving names or typechecking) that
// a @ts-expect-error/@ts-ignore precedes isn't reported. Starting from the line before the diagnostic's, lines that
// are blank or only a `//` comment are passed over on the way up to the directive. Parse errors stay
static void program_apply_comment_directives(Program* me, ErrorPhase phase) {
    bool any = false;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        if ((*i)->comment_directive_count) {
            any = true;
            break;
        }
    }
    if (!any) return;

    VecError* errors = &my errs->errors;
    intptr_t kept = 0;
    for (intptr_t i = 0; i < vec_error_size(errors); i++) {
        Error* error = vec_error_at_mut(errors, i);
        SourceFile* source = (SourceFile*)error->span.source;
        bool suppressed = false;
        if (error->phase == phase && source && source->comment_directive_count && !source->unchecked) {
            const char* text = source_file_start(source);
            const char* text_end = text + cstr_size(&source->contents);
            for (intptr_t line = (intptr_t)source_file_line_index(source, error->span.sv.buf) - 1; line >= 0; line--) {
                CommentDirective* directive = source_file_comment_directive_at_line(source, (u32)line);
                if (directive) {
                    directive->used = true;
                    suppressed = true;
                    break;
                }
                const char* c = text + source->line_starts[line];
                const char* end = line + 1 < source->line_count ? text + source->line_starts[line + 1] : text_end;
                while (c < end && (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n' || *c == '\v' || *c == '\f')) c++;
                if (c < end && !(c + 1 < end && c[0] == '/' && c[1] == '/')) break;
            }
        }
        if (suppressed) {
            error_drop(error);
        } else {
            if (kept != i) errors->data[kept] = *error;
            kept++;
        }
    }
    errors->size = kept;
}

// TS2578 for every @ts-expect-error nothing was reported under, once all semantic diagnostics are in
// A duplicate between two files: the other file (of its "was also declared here"), else null
static SourceFile const* cross_file_duplicate_other(Error const* e) {
    if ((e->ts_code != 2300 && e->ts_code != 2451) || !span_valid(e->span) || !e->span.source) return nullptr;
    for_each (i, vec_error, &e->sub_errors) {
        if (i->ts_code == 6203 && span_valid(i->span) && i->span.source && i->span.source != e->span.source) {
            return i->span.source;
        }
    }
    return nullptr;
}

// tsc's amalgamatedDuplicates: when 8 or more names conflict between two files, their duplicate errors become one
// TS6200 per file listing the names, related to the other file (TS6201)
static void program_amalgamate_duplicates(Program* me) {
    VecError* errors = &my errs->errors;
    for (isize i = 0; i < vec_error_size(errors); i++) {
        Error const* seed = vec_error_at(errors, i);
        SourceFile const* other = cross_file_duplicate_other(seed);
        if (!other) continue;
        SourceFile const* a = seed->span.source;
        // The pair's errors, the first file's (by path) first
        SourceFile const* first = cstr_cmp(&a->canonical_path, &other->canonical_path) < 0 ? a : other;
        SourceFile const* second = first == a ? other : a;
        // The names in the order of the file merged into the other's symbols, the later one in the program (tsc's
        // mergeSymbolTable walks the source's table, its declaration order)
        SourceFile const* merged = a->program_order > other->program_order ? a : other;
        SourceFile const* into = merged == a ? other : a;
        cstr names = cstr_null;
        isize name_count = 0;
        for (int side = 0; side < 2; side++) {
            for_each (e, vec_error, errors) {
                SourceFile const* o = cross_file_duplicate_other(e);
                if (!o || e->span.source != (side ? into : merged) || o != (side ? merged : into)) continue;
                csview name = e->span.sv;
                bool seen = false;
                for (isize at = 0; at < cstr_size(&names) && !seen;) {
                    isize next = cstr_find_at(&names, at, ", ");
                    if (next == c_NPOS) next = cstr_size(&names);
                    seen = csview_equals_sv(csview_slice(cstr_sv(&names), at, next), name);
                    at = next + 2;
                }
                if (seen) continue;
                if (name_count++) cstr_append(&names, ", ");
                cstr_append_sv(&names, name);
            }
        }
        if (name_count < 8) {
            cstr_drop(&names);
            continue;
        }
        for (isize j = vec_error_size(errors) - 1; j >= 0; j--) {
            Error const* e = vec_error_at(errors, j);
            SourceFile const* o = cross_file_duplicate_other(e);
            if (o && ((e->span.source == first && o == second) || (e->span.source == second && o == first))) {
                vec_error_erase_n(errors, j, 1); // (drops it)
            }
        }
        for (int side = 0; side < 2; side++) {
            SourceFile const* here = side ? second : first;
            SourceFile const* there = side ? first : second;
            Error error = {
                .message = cstr_d("Definitions of the following identifiers conflict with those in another file: ", names),
                .span = source_file_first_token(here), .ts_code = 6200, .phase = ERROR_PHASE_TYPECHECK,
            };
            error_add_related_d(&error, source_file_first_token(there), 6201, "Conflicts are in this file.");
            vec_error_push(errors, error);
        }
        cstr_drop(&names);
        i = -1; // (the vector changed: start over, the pair's errors are gone)
    }
}

static void program_report_unused_expect_errors(Program* me) {
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        if (source->unchecked || source->is_stdlib) continue;
        for (u32 j = 0; j < source->comment_directive_count; j++) {
            CommentDirective const* directive = &source->comment_directives[j];
            // Of two directives on one line only the last one counts
            bool shadowed = j + 1 < source->comment_directive_count &&
                source->comment_directives[j + 1].line == directive->line;
            if (directive->expect_error && !directive->used && !shadowed) {
                error_ctx_error_d(
                    my errs, span_make(source, directive->start, directive->end), 2578,
                    "Unused '@ts-expect-error' directive."
                );
            }
        }
    }
}

// tsc's addUndefinedToGlobalsOrErrorOnRedeclaration and the `globalThis` check of initializeTypeChecker: a global value
// can't be named `undefined`, a script can't declare `globalThis` (TS2397)
static void program_check_builtin_global_names(Program* me) {
    static char const* const names[] = { "undefined", "globalThis" };
    for (u32 n = 0; n < c_arraylen(names); n++) {
        Symbol const* symbol = symbol_table_get(&my binder->globals, csview_from(names[n]));
        for (u32 i = 0; symbol && i < symbol->decl_count; i++) {
            SymbolDecl const* decl = &symbol->decls[i];
            SourceFile const* file = decl->name.source;
            if (!file || file->is_stdlib || !span_valid(decl->name)) continue;
            // (`declare global { namespace globalThis {} }` in a module augments it)
            if (n == 1 && file->is_module) continue;
            // (Type declarations: checkTypeNameIsReserved's business)
            if (decl->kind != SYMBOL_DECL_VARIABLE && decl->kind != SYMBOL_DECL_FUNCTION &&
                decl->kind != SYMBOL_DECL_NAMESPACE) {
                continue;
            }
            error_ctx_error_d(my errs, decl->name, 2397, "Declaration name conflicts with built-in global identifier '",
                names[n], "'.");
        }
    }
}

// tsc's getDefaultLibFilePriority: the library files come first in the program, by their name's position in
// commandLineParser's libEntries (lib.d.ts and lib.es6.d.ts, the targets' default libraries, before all), whatever
// order their references loaded them in. It is the order globals merge their declarations in (a merged interface's
// declarations, its overloads)
static isize stdlib_priority(SourceFile const* source) {
    static char const* const libs[] = {
    "es5",
    "es6",
    "es2015",
    "es7",
    "es2016",
    "es2017",
    "es2018",
    "es2019",
    "es2020",
    "es2021",
    "es2022",
    "es2023",
    "es2024",
    "es2025",
    "esnext",
    "dom",
    "dom.iterable",
    "dom.asynciterable",
    "webworker",
    "webworker.importscripts",
    "webworker.iterable",
    "webworker.asynciterable",
    "scripthost",
    "es2015.core",
    "es2015.collection",
    "es2015.generator",
    "es2015.iterable",
    "es2015.promise",
    "es2015.proxy",
    "es2015.reflect",
    "es2015.symbol",
    "es2015.symbol.wellknown",
    "es2016.array.include",
    "es2016.intl",
    "es2017.arraybuffer",
    "es2017.date",
    "es2017.object",
    "es2017.sharedmemory",
    "es2017.string",
    "es2017.intl",
    "es2017.typedarrays",
    "es2018.asyncgenerator",
    "es2018.asynciterable",
    "es2018.intl",
    "es2018.promise",
    "es2018.regexp",
    "es2019.array",
    "es2019.object",
    "es2019.string",
    "es2019.symbol",
    "es2019.intl",
    "es2020.bigint",
    "es2020.date",
    "es2020.promise",
    "es2020.sharedmemory",
    "es2020.string",
    "es2020.symbol.wellknown",
    "es2020.intl",
    "es2020.number",
    "es2021.promise",
    "es2021.string",
    "es2021.weakref",
    "es2021.intl",
    "es2022.array",
    "es2022.error",
    "es2022.intl",
    "es2022.object",
    "es2022.string",
    "es2022.regexp",
    "es2023.array",
    "es2023.collection",
    "es2023.intl",
    "es2024.arraybuffer",
    "es2024.collection",
    "es2024.object",
    "es2024.promise",
    "es2024.regexp",
    "es2024.sharedmemory",
    "es2024.string",
    "es2025.collection",
    "es2025.float16",
    "es2025.intl",
    "es2025.iterator",
    "es2025.promise",
    "es2025.regexp",
    "esnext.asynciterable",
    "esnext.symbol",
    "esnext.bigint",
    "esnext.weakref",
    "esnext.object",
    "esnext.regexp",
    "esnext.string",
    "esnext.float16",
    "esnext.iterator",
    "esnext.promise",
    "esnext.array",
    "esnext.collection",
    "esnext.date",
    "esnext.decorators",
    "esnext.disposable",
    "esnext.error",
    "esnext.intl",
    "esnext.sharedmemory",
    "esnext.temporal",
    "esnext.typedarrays",
    "decorators",
    "decorators.legacy",
    };
    if (!source->is_stdlib) return INTPTR_MAX;
    csview name = basename_of(cstr_sv(&source->canonical_path));
    if (csview_starts_with(name, "lib.")) name = csview_subview(name, 4, name.size - 4);
    if (!csview_ends_with(name, ".d.ts")) return (isize)c_arraylen(libs) + 2;
    name = csview_subview(name, 0, name.size - 5);
    if (csview_equals(name, "default") || csview_equals(name, "es6") || csview_equals(name, "")) return 0;
    for (isize i = 0; i < (isize)c_arraylen(libs); i++) {
        if (csview_equals(name, libs[i])) return i + 1;
    }
    return (isize)c_arraylen(libs) + 2;
}

static void program_order_stdlib_first(Program* me) {
    isize count = vec_source_file_ptr_size(&my source_files_ordered);
    if (!count) return;
    SourceFile** files = (SourceFile**)vec_source_file_ptr_at_mut(&my source_files_ordered, 0);
    // (A stable insertion sort: few libraries, and most programs have them in order already)
    for (isize i = 1; i < count; i++) {
        SourceFile* file = files[i];
        isize priority = stdlib_priority(file);
        isize j = i;
        while (j > 0 && stdlib_priority(files[j - 1]) > priority) {
            files[j] = files[j - 1];
            j--;
        }
        files[j] = file;
    }
    for (isize i = 0; i < count; i++) files[i]->program_order = (u32)i;
}

static void resolve_file(SourceFile* source, ErrorCtx* errs) {
    block_lookup_names_container(&source->ast, false, c_sv("file"), span_null, source->scope, errs);
    if (!source->is_stdlib && !source->unchecked && !source->is_declaration) unused_mark_jsdoc_links(source);
}

// Whether a file's resolve walk can run on a thread of its own (plan 45). A JavaScript file's does too: its assignment
// declarations (`this.x = v`, `F.prototype.x = v`, expandos), which declare into other files' symbols, wait for its
// replay (DEFERRED_JS_* in scope.c)
static bool resolves_in_parallel(SourceFile const* source) {
    return !source->resolve_serial;
}

// The TypeScript files' walks, shared by the resolve threads
typedef struct ResolveLane {
    Program* program;
    SourceFile** files; // Biggest first
    u32* shard_of; // Index into shards, by files' index
    ResolveShard* shards;
    u32 count;
    atomic_uint next;
} ResolveLane;

static void resolve_lane_run(ResolveLane* me, u32 thread) {
    Binder* binder = my program->binder;
    Arena* arena = thread ? binder_thread_arena(binder, thread) : &binder->arena;
    for (u32 i; (i = atomic_fetch_add(&my next, 1)) < my count;) {
        ResolveShard* shard = &my shards[my shard_of[i]];
        *shard = (ResolveShard){
            .binder = binder, .arena = arena,
            .errs = { .phase = my program->errs->phase, .parent = my program->errs },
            .discarded = error_ctx_scratch(my program->errs),
        };
        resolve_shard = shard;
        resolve_file(my files[i], &shard->errs);
        resolve_shard = nullptr;
    }
}

typedef struct ResolveThread {
    ResolveLane* lane;
    u32 index;
} ResolveThread;

static void* resolve_lane_thread(void* arg) {
    ResolveThread* thread = arg;
    set_compiler_options(&thread->lane->program->options);
    resolve_lane_run(thread->lane, thread->index);
    clear_compiler_options();
    return nullptr;
}

typedef struct ResolveOrder {
    isize size;
    u32 index;
} ResolveOrder;

static int resolve_order_cmp(void const* a, void const* b) {
    ResolveOrder const* x = a;
    ResolveOrder const* y = b;
    if (x->size != y->size) return x->size > y->size ? -1 : 1;
    return x->index < y->index ? -1 : x->index > y->index;
}

// The resolve walk of every file (plan 45). TypeScript files are walked in any order, on --threads threads, each into a
// ResolveShard of its own; then, in program order, each TypeScript file's shard is replayed and merged and each
// JavaScript file walked. The order of everything that reaches the binder is the same for any thread count
static void program_resolve_walk(Program* me) {
    isize file_count = vec_source_file_ptr_size(&my source_files_ordered);
    ResolveShard* shards = c_new_n(ResolveShard, file_count ? file_count : 1);
    ResolveLane lane = { .program = me, .shards = shards };
    binder_mark_serial_resolves(my binder);
    lane.files = c_new_n(SourceFile*, file_count ? file_count : 1);
    lane.shard_of = c_new_n(u32, file_count ? file_count : 1);
    // Biggest first, so that one big file doesn't start last
    ResolveOrder* order = c_new_n(ResolveOrder, file_count ? file_count : 1);
    bool any_js = false;
    for (isize i = 0; i < file_count; i++) {
        SourceFile* source = *vec_source_file_ptr_at(&my source_files_ordered, i);
        if (resolves_in_parallel(source)) order[lane.count++] = (ResolveOrder){ cstr_size(&source->contents), (u32)i };
        any_js |= source->is_js;
    }
    // (The `require` of a JavaScript file that doesn't declare one: one symbol, made before the walks share it)
    if (any_js) binder_require_symbol(my binder);
    qsort(order, lane.count, sizeof(ResolveOrder), resolve_order_cmp);
    for (u32 i = 0; i < lane.count; i++) {
        lane.files[i] = *vec_source_file_ptr_at(&my source_files_ordered, order[i].index);
        lane.shard_of[i] = order[i].index;
    }
    free(order);
    atomic_init(&lane.next, 0);
    u32 threads = my threads ? my threads : 1;
    // (Few files aren't worth a thread each)
    if (threads > lane.count / 8 && !debug_env.parallel_resolve) threads = lane.count / 8 ? lane.count / 8 : 1;
    // Nothing reports into the program's context meanwhile, every file into its own
    my errs->frozen = true;
    if (threads <= 1) {
        resolve_lane_run(&lane, 0);
    } else {
        // The main thread works too, with Binder.arena. Workers get the main thread's stack size: the walk recurses
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, worker_stack_size());
        pthread_t* workers = c_new_n(pthread_t, threads - 1);
        ResolveThread* args = c_new_n(ResolveThread, threads - 1);
        for (u32 t = 1; t < threads; t++) binder_thread_arena(my binder, t);
        for (u32 t = 0; t < threads - 1; t++) {
            args[t] = (ResolveThread){ &lane, t + 1 };
            panic_if(pthread_create(&workers[t], &attr, resolve_lane_thread, &args[t]), "couldn't start a resolve thread");
        }
        resolve_lane_run(&lane, 0);
        for (u32 t = 0; t < threads - 1; t++) pthread_join(workers[t], nullptr);
        free(workers);
        free(args);
        pthread_attr_destroy(&attr);
    }
    my errs->frozen = false;
    for (isize i = 0; i < file_count; i++) {
        SourceFile* source = *vec_source_file_ptr_at(&my source_files_ordered, i);
        if (resolves_in_parallel(source)) resolve_shard_merge(&shards[i]);
        else resolve_file(source, my errs);
    }
    free(lane.files);
    free(lane.shard_of);
    free(shards);
}

void program_resolve_names(Program* me) {
    CHANGE_PHASE(PROGRAM_PHASE_LOADING, PROGRAM_PHASE_RESOLVING_NAMES);
    // Loading is over: what the pool parsed for nothing goes
    load_pool_destroy(my pool);
    my pool = nullptr;
    program_validate_options(me);
    program_verify_module_kind(me);
    program_verify_inferred_root_dir(me);
    program_verify_emit_paths(me);
    program_verify_project_files(me);
    my errs->phase = ERROR_PHASE_RESOLVE_NAMES;
    set_compiler_options(&my options);
    program_order_stdlib_first(me);

    // Declare: symbols, the scopes of files and namespaces, exports
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        source->scope = scope_new_file(my binder, source);
        // What module augmentations augment, if something else brought it into the program
        for (u32 j = 0; j < source->module_ref_count; j++) {
            ModuleRef* ref = source->module_refs[j];
            if (ref->is_declaration && !ref->file && cstr_size(&ref->augmented_path)) {
                ref->file = program_find_source(me, cstr_sv(&ref->augmented_path));
            }
        }
    }
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        block_add_names(&source->ast, source->scope);
        scope_add_commonjs_module(source->scope);
        scope_check_export_merges(source->scope, block_span(&source->ast));
        if (source->is_module && !source->is_js) scope_check_duplicate_exports(source->scope);
    }
    binder_declare_global_augmentations(my binder);
    binder_link_global_expandos(my binder);
    program_check_builtin_global_names(me);
    binder_check_ambient_module_exports(my binder, my errs);

    if (my options.no_resolve) {
        for_each (i, vec_source_file_ptr, &my source_files_ordered) {
            SourceFile* source = *i;
            for (u32 j = 0; j < source->module_ref_count; j++) {
                ModuleRef* ref = source->module_refs[j];
                if ((!ref->is_declaration || source->is_module || ref->nested_in_ambient_module) && !ref->ambient_only) program_load_module_ref(me, source, ref);
            }
        }
    }

    // What TS7016 suggests for a JavaScript module of a package, by what the whole program found (tsc's
    // createModuleNotFoundChain)
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        for (u32 j = 0; j < source->module_ref_count; j++) {
            ModuleRef* ref = source->module_refs[j];
            if (ref->failure == MODULE_REF_JSX_NOT_SET && !ref->file) {
                // A .tsx file that is part of the program anyway (a root) is still what the name means (tsc's
                // resolveExternalModule: TS6142, and the file)
                csview shown = cstr_sv(&ref->resolved_path);
                cstr absolute = shown.size && shown.buf[0] == '/' ? cstr_from_sv(shown) : cstr_d(program_cwd(me), "/", shown);
                ref->file = program_find_source(me, cstr_sv(&absolute));
                ref->used_ts_extension = ref->file && csview_ends_with(cstr_sv(&ref->path), ".tsx");
                cstr_drop(&absolute);
                continue;
            }
            if (ref->failure != MODULE_REF_IS_JS || !cstr_size(&ref->package_name)) continue;
            csview name = cstr_sv(&ref->package_name);
            cstr types = cstr_lit("@types/");
            if (name.size && name.buf[0] == '@') {
                isize slash = csview_find_sv(name, c_sv("/"));
                if (slash != c_NPOS) cstr_append_d(&types, DEFAULT_DISPLAY, csview_slice(name, 1, slash), "__", csview_slice(name, slash + 1, name.size));
                else cstr_append_sv(&types, name);
            } else {
                cstr_append_sv(&types, name);
            }
            auto bundles = hmap_package_flag_get(&my packages, name);
            ref->package_hint = hmap_package_flag_get(&my packages, cstr_sv(&types)) ? MODULE_REF_HINT_TYPES_EXIST :
                bundles && bundles->second ? MODULE_REF_HINT_BUNDLES_TYPES : MODULE_REF_HINT_INSTALL_TYPES;
            cstr_drop(&types);
        }
    }

    // Link: what every module name refers to (ambient modules are all declared now), `export *` tables and every alias. Nothing writes to a symbol after this
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        for (u32 j = 0; j < source->module_ref_count; j++) {
            binder_module_of_ref(my binder, source->module_refs[j]);
        }
    }
    binder_link(my binder);
    binder_check_export_star_conflicts(my binder, my errs);

    // Resolve: every identifier gets its symbol, function and block scopes are made on the way
    program_resolve_walk(me);

    binder_resolve_remaining_aliases(my binder);

    error_ctx_remove_unchecked(my errs);
    program_apply_comment_directives(me, ERROR_PHASE_RESOLVE_NAMES);

    clear_compiler_options();
    my errs->phase = ERROR_PHASE_UNKOWN;
    CHANGE_PHASE(PROGRAM_PHASE_RESOLVING_NAMES, PROGRAM_PHASE_NAMES_RESOLVED);
}

void program_make_current(Program* me) {
    panic_if(my phase < PROGRAM_PHASE_NAMES_RESOLVED, "program needs its names resolved before types can be evaluated");
    if (!my checker) my checker = checker_new(me);
    checker_make_current(my checker);
}

void program_set_checkers(Program* me, u32 count) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    my requested_checkers = count;
}

void program_declaration_diagnostics_cli(Program* me, bool earlier_errors) {
    my declaration_diagnostics_cli = true;
    my declaration_diagnostics_earlier = earlier_errors;
}

// Whether the declaration diagnostics may still be reported: tsc's CLI doesn't emit with noEmit or noEmitOnError when
// there are other diagnostics, nor computes them
static bool program_declaration_diagnostics_gated(Program const* me) {
    // (TTTT_DECLARATION_DIAGNOSTICS=1: report them anyway, to audit a corpus that has other errors)
    static int forced = -1;
    if (forced < 0) forced = getenv("TTTT_DECLARATION_DIAGNOSTICS") != nullptr;
    return my declaration_diagnostics_cli && (my options.no_emit || my options.no_emit_on_error) && !forced;
}

void program_set_threads(Program* me, u32 count) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    my threads = count;
}

Binder const* program_get_binder(Program const* me) {
    return my binder;
}

ErrorCtx* program_errors(Program const* me) {
    return my errs;
}

Checker* program_get_checker(Program const* me) {
    return my checker;
}

u32 program_checker_count(Program const* me) {
    return my checker_count;
}

Checker* program_checker_at(Program const* me, u32 index) {
    return my checkers[index];
}

// Which checker checks the index-th checked file
static u32 share_of(Program const* me, u32 index) {
    return my file_shares[index];
}

// The checker that checked a file in program_typecheck() (the first one for a file nobody checked). Expression types
// are only complete in the checker that walked the file: one that didn't would type an expression without the
// contextual type its parent gives it (see "Expressions" in documentation.md)
Checker* program_checker_of(Program const* me, SourceFile const* file) {
    if (my checker_count <= 1 || file->is_stdlib || !source_file_is_checked(file)) return my checker;
    u32 index = 0;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile const* source = *i;
        if (source->is_stdlib || !source_file_is_checked(source)) continue;
        if (source == file) return my checkers[share_of(me, index)];
        index++;
    }
    return my checker;
}

Symbol const* program_get_global(Program const* me, csview name, u32 meaning) {
    return symbol_for_meaning(symbol_table_get(&my binder->globals, name), meaning);
}

// The global types tsc's initializeTypeChecker looks up with reportErrors (missing with noLib or a partial lib)
static void program_report_missing_global_types(Program* me) {
    static char const* const names[] = {
        "IArguments", "Array", "Object", "Function", "CallableFunction", "NewableFunction", "String", "Number", "Boolean",
        "RegExp",
    };
    for (u32 i = 0; i < c_arraylen(names); i++) {
        bool bind_call_apply = !strcmp(names[i], "CallableFunction") || !strcmp(names[i], "NewableFunction");
        if (bind_call_apply && !my options.strict_bind_call_apply) continue;
        Symbol const* symbol = program_get_global(me, csview_from(names[i]), SYMBOL_TYPE);
        if (!symbol) {
            error_ctx_global_error_d(my errs, 2318, "Cannot find global type '", names[i], "'.");
            continue;
        }
        // (tsc's getTypeOfGlobalSymbol: as many type parameters as the checker uses, at the first declaration)
        u32 arity = !strcmp(names[i], "Array") ? 1 : 0;
        Generic const* generic = symbol_type_generic(symbol);
        u32 count = generic ? (u32)vec_generic_item_size(&generic->items) : 0;
        if (count == arity) continue;
        for (u32 d = 0; d < symbol->decl_count; d++) {
            SymbolDecl const* decl = &symbol->decls[d];
            if (decl->kind != SYMBOL_DECL_INTERFACE && decl->kind != SYMBOL_DECL_CLASS) continue;
            if (!decl->name.source || decl->name.source->is_stdlib) break;
            error_ctx_error_d(my errs, decl->name, 2317, "Global type '", names[i], "' must have ", arity,
                " type parameter(s).");
            break;
        }
    }
}

// The checkers of program_typecheck(), taken by the threads in index order. Which files a checker checks depends only
// on the file list and the checker count, never on which thread runs it
typedef struct CheckShares {
    Program* program;
    u32 count;
    atomic_uint next;
} CheckShares;

// One of Checker.deferred (tsc's checkDeferredNode)
static void check_deferred(DeferredCheck deferred, ErrorCtx* errs) {
    switch (deferred.kind) {
        case DEFERRED_FUNCTION_BODY: function_impl_typecheck_body((FunctionImpl*)deferred.node, errs); break;
        case DEFERRED_CLASS_MEMBERS: class_decl_typecheck_members((ClassDecl*)deferred.node, errs); break;
        case DEFERRED_JSX_ELEMENT: jsx_element_typecheck(deferred.node, errs); break;
        case DEFERRED_EXPR: expr_typecheck(deferred.node, errs); break;
    }
}

static void check_share(Program* me, u32 c, u32 count) {
    struct timespec start, end;
    timespec_get(&start, TIME_UTC);
    Checker* checker = my checkers[c];
    checker_make_current(checker);
    ty_init_global_types(checker);
    u32 index = 0;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        if (source->is_stdlib || !source_file_is_checked(source) || share_of(me, index++) != c) continue;
        checker->file_order = index - 1;
        if (source->is_declaration) block_check_required_declare(&source->ast, checker->errs);
        source_file_check_verbatim_exports(source, checker->errs);
        checker->defer_checks = true;
        block_typecheck(&source->ast, (BlockTypecheckCtx){ .ambient = source->is_declaration }, checker->errs);
        // (What a deferred check defers joins the end of the queue)
        for (u32 d = 0; d < checker->deferred_count; d++) {
            check_deferred(checker->deferred[d], checker->errs);
        }
        checker->deferred_count = 0;
        checker->defer_checks = false;
    }
    // After its files are checked: the checker marks members it reads
    index = 0;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        if (source->is_stdlib || !source_file_is_checked(source) || share_of(me, index++) != c) continue;
        if (!source->is_declaration && !source->unchecked) unused_report(source, checker->errs);
        if (my declaration_errs) declaration_diagnostics_check(source, &my declaration_errs[c]);
    }
    // Declaration emit's diagnostics don't depend on skipTypeChecking: also for files noCheck or `@ts-nocheck` leave
    // unchecked (not a referenced project's, which that project reports)
    index = 0;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        if (source->is_stdlib || source_file_is_checked(source) || source->from_reference || source->is_js) continue;
        if (index++ % count != c) continue;
        if (my declaration_errs) declaration_diagnostics_check(source, &my declaration_errs[c]);
    }
    checker_make_current(nullptr);
    timespec_get(&end, TIME_UTC);
    checker->check_ms = (double)(end.tv_sec - start.tv_sec) * 1e3 + (double)(end.tv_nsec - start.tv_nsec) / 1e6;
}

// Deals the checked files to the checkers: which checker checks a file depends only on the file list, so the output
// depends only on the checker count. Neighbours in program order (a file, then what it imports) share most of their
// types, and each checker instantiates what its files use again: the files are cut into 2 runs of about equal size
// per checker, dealt round robin. One run each makes the least duplication (drizzle's peak 1688 -> 1277 MB at 4
// checkers) but balances worse (valibot's test files sit together, type-fest's biggest file takes its neighbours);
// two keep most of it (1337 MB) at round robin's wall times or better. Bytes are the cost proxy
static void deal_files(Program* me) {
    enum { RUNS_PER_CHECKER = 2 };
    u32 total = 0;
    u64 bytes = 0;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        if ((*i)->is_stdlib || !source_file_is_checked(*i)) continue;
        total++;
        bytes += cstr_size(&(*i)->contents);
    }
    free(my file_shares);
    my file_shares = c_new_n(u32, total ? total : 1);
    u64 runs = (u64)my checker_count * RUNS_PER_CHECKER;
    u32 index = 0;
    u64 before = 0;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        if ((*i)->is_stdlib || !source_file_is_checked(*i)) continue;
        u64 size = cstr_size(&(*i)->contents);
        // (A file belongs to the run its middle byte falls in)
        my file_shares[index++] = (u32)((before + size / 2) * runs / (bytes ? bytes : 1) % my checker_count);
        before += size;
    }
}

static void check_shares_run(CheckShares* me) {
    for (u32 c; (c = atomic_fetch_add(&my next, 1)) < my count;) check_share(my program, c, my count);
}

static void* check_shares_thread(void* shares) {
    set_compiler_options(&((CheckShares*)shares)->program->options);
    check_shares_run(shares);
    clear_compiler_options();
    symbol_chain_release_thread();
    return nullptr;
}

void program_typecheck(Program* me) {
    CHANGE_PHASE(PROGRAM_PHASE_NAMES_RESOLVED, PROGRAM_PHASE_TYPECHECKING);
    my errs->phase = ERROR_PHASE_TYPECHECK;
    set_compiler_options(&my options);
    program_make_current(me);

    scope_report_missing_prefixes(my binder);
    program_report_missing_global_types(me);
    // Each checker reports into its own ErrorCtx; nothing may write to the program's or make symbols meanwhile (they
    // run on threads of their own)
    u32 count = my requested_checkers ? my requested_checkers : 1;
    my checkers = c_new_n(Checker*, count);
    my checker_count = count;
    ErrorCtx* checker_errs = c_new_n(ErrorCtx, count);
    my binder->frozen = true;
    my errs->frozen = true;
    bool declaration_diagnostics = !(program_declaration_diagnostics_gated(me) && my declaration_diagnostics_earlier);
    my declaration_errs = declaration_diagnostics ? c_new_n(ErrorCtx, count) : nullptr;
    for (u32 c = 0; c < count; c++) {
        if (my declaration_errs) my declaration_errs[c] = (ErrorCtx){ .phase = ERROR_PHASE_TYPECHECK, .parent = my errs };
        checker_errs[c] = (ErrorCtx){ .phase = ERROR_PHASE_TYPECHECK, .parent = my errs };
        my checkers[c] = c ? checker_new(me) : my checker;
        my checkers[c]->errs = &checker_errs[c];
    }
    deal_files(me);
    CheckShares shares = { .program = me, .count = count };
    u32 threads = my threads ? my threads : 1;
    if (threads > count) threads = count;
    if (threads <= 1) {
        check_shares_run(&shares);
    } else {
        // The main thread works too. Workers get the main thread's stack size: the checker recurses per nesting level
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, worker_stack_size());
        pthread_t* workers = c_new_n(pthread_t, threads - 1);
        for (u32 t = 0; t < threads - 1; t++) {
            panic_if(pthread_create(&workers[t], &attr, check_shares_thread, &shares), "couldn't start a checker thread");
        }
        check_shares_run(&shares);
        for (u32 t = 0; t < threads - 1; t++) pthread_join(workers[t], nullptr);
        free(workers);
        pthread_attr_destroy(&attr);
    }
    my binder->frozen = false;
    my errs->frozen = false;
    error_ctx_keep_first_once(checker_errs, count);
    for (u32 c = 0; c < count; c++) {
        error_ctx_merge(my errs, &checker_errs[c]);
        error_ctx_drop(&checker_errs[c]);
        my checkers[c]->errs = my errs;
    }
    free(checker_errs);
    checker_make_current(my checker);

    error_ctx_remove_unchecked(my errs);
    program_apply_comment_directives(me, ERROR_PHASE_TYPECHECK);
    program_report_unused_expect_errors(me);
    // (Comment directives don't apply to them: tsc's getDeclarationDiagnostics is apart from the checker's)
    if (my declaration_errs) {
        bool keep = !program_declaration_diagnostics_gated(me) || error_ctx_empty(my errs);
        for (u32 c = 0; c < count; c++) {
            if (keep) error_ctx_merge(my errs, &my declaration_errs[c]);
            error_ctx_drop(&my declaration_errs[c]);
        }
        free(my declaration_errs);
        my declaration_errs = nullptr;
    }
    program_amalgamate_duplicates(me);
    error_ctx_sort_and_dedupe(my errs);

    clear_compiler_options();
    my errs->phase = ERROR_PHASE_UNKOWN;
    CHANGE_PHASE(PROGRAM_PHASE_TYPECHECKING, PROGRAM_PHASE_TYPECHECKED);
}

intptr_t program_get_file_count(Program const* me) {
    return hmap_box_source_file_size(&my source_files_map);
}

// tsc's importSyntaxAffectsModuleResolution
static bool import_syntax_affects_resolution(CompilerOptions const* o) {
    ModuleResolutionKind kind = o->module_resolution;
    bool node_next = kind == MODULE_RESOLUTION_NODE16 || kind == MODULE_RESOLUTION_NODENEXT;
    return node_next || ((node_next || kind == MODULE_RESOLUTION_BUNDLER) &&
        (o->resolve_package_json_exports || o->resolve_package_json_imports));
}

static SpecifierMode specifier_mode_of_kind(ModuleKind kind) {
    if (kind == MODULE_KIND_COMMONJS) return SPECIFIER_MODE_COMMONJS;
    if (kind >= MODULE_KIND_ES2015 && kind <= MODULE_KIND_ESNEXT) return SPECIFIER_MODE_ESM;
    return kind == MODULE_KIND_PRESERVE ? SPECIFIER_MODE_ESM : SPECIFIER_MODE_NONE;
}

SpecifierMode program_default_resolution_mode(Program const* me, SourceFile const* file) {
    if (!import_syntax_affects_resolution(program_options_of(me, file))) return SPECIFIER_MODE_NONE;
    return specifier_mode_of_kind(source_file_implied_format_for_emit(file));
}

// tsc's getModeForUsageLocation of an import
static SpecifierMode program_usage_mode(Program const* me, SourceFile const* file, ModuleRef const* ref) {
    if (ref->resolution_override == IMPORT_RESOLUTION_IMPORT) return SPECIFIER_MODE_ESM;
    if (ref->resolution_override == IMPORT_RESOLUTION_REQUIRE) return SPECIFIER_MODE_COMMONJS;
    if (!import_syntax_affects_resolution(program_options_of(me, file))) return SPECIFIER_MODE_NONE;
    if (ref->syntax == MODULE_REF_REQUIRE) return SPECIFIER_MODE_COMMONJS;
    if (ref->syntax == MODULE_REF_DYNAMIC) return SPECIFIER_MODE_ESM;
    return specifier_mode_of_kind(source_file_emit_module_kind(file));
}

cstr program_module_specifier(Program* me, SourceFile const* importing, SourceFile const* target, SpecifierMode override) {
    SpecifierMode default_mode = program_default_resolution_mode(me, importing);
    SpecifierMode wanted = override ? override : default_mode;
    // An import that already names it (computeModuleSpecifiers' existingSpecifier, by the file's include reasons)
    SpecifierImport* imports = malloc(sizeof(SpecifierImport) * (importing->module_ref_count + 1));
    u32 import_count = 0;
    for (u32 i = 0; i < importing->module_ref_count; i++) {
        ModuleRef const* ref = importing->module_refs[i];
        if (ref->is_declaration || ref->ambient_only || ref == importing->helpers_ref || ref == importing->jsx_runtime_ref) continue;
        SpecifierMode mode = program_usage_mode(me, importing, ref);
        imports[import_count++] = (SpecifierImport){ .text = cstr_sv(&ref->path), .mode = mode };
        if (ref->file != target) continue;
        if (mode != wanted && mode != SPECIFIER_MODE_NONE && wanted != SPECIFIER_MODE_NONE) continue;
        free(imports);
        return cstr_clone(ref->path);
    }
    csview cwd = program_cwd(me);
    cstr importing_path = absolute_path_of(cwd, cstr_sv(&importing->canonical_path));
    cstr target_path = absolute_path_of(cwd, cstr_sv(&target->canonical_path));
    // (tsc's command line names a referenced project's output declaration file, which it reads in its place)
    csview output = target->from_reference ? project_redirects_output_of_source(my redirects, cstr_sv(&target_path)) : c_sv("");
    if (output.size) cstr_assign_sv(&target_path, output);
    CompilerOptions const* options = program_options_of(me, importing);
    cstr project_dir = cstr_size(&options->config_dir) ? absolute_path_of(cwd, cstr_sv(&options->config_dir)) : cstr_null;
    SpecifierRequest request = {
        .resolver = program_resolver_of(me, importing),
        .options = options,
        .symlinks = my symlinks,
        .importing = cstr_sv(&importing_path),
        .importing_format = importing->has_format ? (importing->is_esm ? SPECIFIER_MODE_ESM : SPECIFIER_MODE_COMMONJS) :
            SPECIFIER_MODE_NONE,
        .importing_default_mode = default_mode,
        .imports = imports,
        .import_count = import_count,
        .override_mode = override,
        .target = cstr_sv(&target_path),
        .project_dir = cstr_sv(&project_dir),
    };
    cstr ret = module_specifier_generate(&request);
    c_drop(cstr, &importing_path, &target_path, &project_dir);
    free(imports);
    return ret;
}

static cstr program_common_source_dir(Program* me);
cstr program_module_display_name(Program* me, Symbol const* module, SourceFile const* from, SpecifierMode override) {
    SourceFile const* target = module->module ? module->module->file : nullptr;
    // (A JSON file's module symbol has no SourceFile declaration in tsc: named like an ambient module, by its quoted
    // escapedName)
    if (!target || target->is_json || !from) {
        return module_name_for_error(module, nullptr, false);
    }
    if (cstr_size(&compiler_options()->out_file)) {
        // A bundle names a file non-relatively from the common source directory, without its extension or `/index`
        // (getSpecifierForModuleSymbol: baseUrl the common source directory, "non-relative", ending "minimal")
        cstr common = program_common_source_dir(me);
        cstr cwd = cstr_null;
        try (fs_get_cwd(my fs, &cwd)) or_panic;
        cstr absolute = absolute_path_of(cstr_sv(&cwd), cstr_sv(&target->canonical_path));
        csview path = cstr_sv(&absolute);
        csview dir = cstr_sv(&common);
        while (dir.size > 1 && dir.buf[dir.size - 1] == '/') dir.size--;
        cstr ret = cstr_null;
        bool under = dir.size && path.size > dir.size && !memcmp(path.buf, dir.buf, (size_t)dir.size) &&
            (path.buf[dir.size] == '/' || dir.size == 1);
        if (under) {
            csview rest = csview_slice(path, dir.size == 1 ? 1 : dir.size + 1, path.size);
            static const char* const extensions[] = { ".d.ts", ".d.mts", ".d.cts", ".ts", ".tsx", ".js", ".jsx", ".mts", ".cts", ".mjs", ".cjs" };
            for (size_t i = 0; i < sizeof(extensions) / sizeof(*extensions); i++) {
                if (csview_ends_with(rest, extensions[i])) {
                    rest.size -= (isize)strlen(extensions[i]);
                    break;
                }
            }
            if (csview_equals(rest, "index")) rest = c_sv(".");
            else if (csview_ends_with(rest, "/index")) rest.size -= 6;
            ret = cstr_d("\"", rest, "\"");
        }
        c_drop(cstr, &common, &cwd, &absolute);
        if (under) return ret;
        return module_name_for_error(module, nullptr, false);
    }
    cstr specifier = program_module_specifier(me, from, target, override);
    cstr ret = cstr_d("\"", specifier, "\"");
    cstr_drop(&specifier);
    return ret;
}

u32 program_serial(Program const* me) {
    return my serial;
}
