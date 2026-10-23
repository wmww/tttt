#include "program.h"
#include "tsconfig.h"
#include "lib_names.h"
#include "string_helpers.h"
#include "unused.h"
#include "ty/declaration_emit.h"
#include "import_attributes.h"
#include "parser.h"
#include "block.h"
#include "flow.h"
#include "function_impl.h"
#include <sched.h>
#include <sys/resource.h>
#include <cleat/filesystem.h>
#include "error.h"
#include "statement.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "interface_decl.h"
#include "several_types.h"
#include "port_statement.h"
#include "source_file.h"
#include "ty/ty.h"
#include "ty/graph.h"
#include "ty/atom_table.h"
#include "module_resolution.h"
#include "json.h"
#include "content_mapper.h"
#include "load_pool.h"
#include "project.h"
#include "symbol_chain.h"
#include "cached_fs.h"
#include <time.h>
#include <pthread.h>
#include <setjmp.h>
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

// A library the libs phase left to the pool worker parsing it, with what loading it had then (Program.deferred_libs)
typedef struct DeferredLib {
    cstr path; // Canonical
    SourceFile* parent; // Program.loading_parent: null for a library the options name, else the library referring to it
    Span site; // Program.include_site and include_kind (a `/// <reference lib>` between libraries: INCLUDE_REFERENCE)
    IncludeKind kind;
    bool set_included_at; // Loaded through program_load_reference_path(), which sets SourceFile.included_at
} DeferredLib;

typedef struct Program {
    ProgramPhase phase;
    // The content mappers' processes (plan 56), made by the first mapped file program_make_source() reads (a load
    // pool worker's maybe: hence atomic)
    ContentMapperHost* _Atomic content_mapper_host;
    bool loading_stdlib; // If we're loading standard library files instead of normal files
    bool follow_lib_references; // program_follow_lib_references()
    cstr lib_dir; // cstr_null: default_lib_dir
    ErrorCtx* errs; // unowned
    Fs const* fs; // cached_fs's
    CachedFs* cached_fs;
    struct Preresolved** preresolved_garbage; // Taken from loaded files, freed when loading is over
    u32 preresolved_garbage_count, preresolved_garbage_cap;
    // program_resolve_names()'s releases (program_release_run()), off the critical path: joined by program_drop()
    pthread_t release_thread;
    bool release_started;
    double loader_parse_ms; // TTTT_LOAD_POOL_STATS without a pool: program_make_source()'s time
    cstr cwd; // program_cwd()
    Binder* binder;
    ModuleResolver* resolver; // Made on first use, the options are final by then
    // program_set_references(): the referenced projects, and a resolver with each one's options for its files
    ProjectRedirects const* redirects;
    u32 node_modules_depth; // How many node_modules lookups the file being loaded was reached through (tsc's currentNodeModulesDepth)
    Span include_site; // The module name or `/// <reference path>` the file being loaded is loaded for (SourceFile.included_at)
    IncludeKind include_kind; // Why the file being loaded is: its next SourceFile.include_reasons entry (with include_site)
    csview include_package_id; // And the package id of the resolution, for INCLUDE_IMPORT
    ModuleResolver** reference_resolvers;
    u32 reference_count;
    RealpathFn realpath; // program_set_realpath()
    void* realpath_ctx;
    SymlinkCache* symlinks; // What resolutions learned about symlinked directories, for program_module_specifier()
    // Whether symlinks has the links of every emitted file's package's dependencies (program_dependency_links())
    _Atomic bool dependency_links;
    pthread_mutex_t dependency_links_lock;
    u32 serial; // program_serial()
    HMapBoxSourceFile source_files_map;
    VecSourceFilePtr source_files_ordered;
    // The program_order of the first file: the files of a parent program (the test harness's shared libraries, see
    // program_copy_globals_from()) come before this one's, like tsc's default library files
    u32 order_base;
    // SourceFile.package_id -> the first file loaded with it, and whether another file got it too: only then has
    // program_collect_files() redirects to make
    HMapSourceFilePtr files_by_package_id;
    bool duplicate_packages;
    // The loader's walk, for program_collect_files(): the file whose references are being loaded (SourceFile.load_children),
    // the files loaded with none (roots, libraries, automatic types), in order, and whether the walk has been collected
    SourceFile* loading_parent;
    VecSourceFilePtr load_roots;
    bool collected;
    // program_set_defer_libs(): a library a pool worker is still on when the libs phase gets to it is remembered with
    // what loading it then had (DeferredLib) and loaded later, program_finish_deferred_libs()
    bool defer_libs;
    bool finishing_libs;
    bool lib_deferred; // program_add_source_idempotent() just remembered one (returning null)
    struct DeferredLib* deferred_libs;
    u32 deferred_lib_count, deferred_lib_cap;
    double deferred_libs_since; // (TTTT_LOAD_POOL_STATS)
    // tsc's getPackagesMap: the package name of every module resolution that found a file of a package, whether one of
    // them was a declaration file
    HMapPackageFlag packages;
    Checker* checker; // Created by program_make_current(), checks the first share of the files
    Checker** checkers; // malloc'd, checker_count of them once typechecked: checker and the others
    // --shared-graph: checkers retired after a shadowed file (graph_fresh_worker()), dropped with the others
    Checker** retired;
    u32 retired_count, retired_cap;
    u32 checker_count;
    // What program_typecheck() checks, in order: every checked file of source_files_ordered
    VecSourceFilePtr check_files;
    u32* file_shares; // malloc'd: the checker deal_files() deals each file of check_files to
    // malloc'd: the checker that checked each file of check_files (see share_of()), UINT32_MAX until one claims it:
    // a checker done with its own files takes what's left of a running one's (steal_file())
    _Atomic u32* file_checkers;
    // malloc'd, per checker while typechecking: its share's state (0 not started, 1 running, 2 done) and the source bytes
    // of its dealt files nobody claimed yet
    _Atomic u32* share_states;
    _Atomic u64* share_left;
    struct timespec check_start; // While typechecking: when the checkers started (TTTT_FILE_TIMES' timeline)
    u32* share_first; // malloc'd, per checker while typechecking: the file of check_files it starts with (deal_files())
    bool steal_files; // While typechecking: checkers take each other's files (TTTT_NO_STEAL=1: never, to measure)
    u32 requested_checkers; // program_set_checkers()
    bool shared_graph; // program_set_shared_graph()
    bool first_touch_stamps; // program_set_first_touch_stamps()
    struct TyGraph* graph; // Its checkers', while they share one (ty/graph.h)
    // --shared-graph on threads: the graph-wide recycle (graph_epoch_arrive()). What each share's checker holds
    // (checker_memory(), after each file), the sum's budget (UINT64_MAX: never), the barrier
    _Atomic u64* graph_held;
    u64 graph_budget;
    pthread_mutex_t graph_epoch_lock;
    pthread_cond_t graph_epoch_cond;
    u32 graph_active, graph_arrived, graph_epoch, graph_recycles;
    _Atomic bool graph_epoch_due;
    // The worker the others wait for at their next file boundary until it has checked a file (-1: none): after a
    // graph-wide recycle, the one that rebuilt the graph (graph_lead_wait())
    _Atomic int graph_leader;
    struct AtomTable* atoms; // Every checker's atoms (ty/atom_table.h)
    struct TypeDirectory* type_directory; // Every checker's types by id (lazy_type.h)
    u32 checker_cap; // program_set_checker_cap(), 0: the threads (past it only for a big program, auto_checker_count())
    u64 checker_memory; // program_set_checker_memory(), bytes: UINT64_MAX never recycles
    u64 checker_memory_cap; // program_set_checker_memory_cap(), bytes: UINT64_MAX none
    u64 checker_memory_shared; // program_set_checker_memory_shared(), bytes: UINT64_MAX none
    u64 memory_pool; // program_set_memory_pool(), bytes of the process's resident set: UINT64_MAX none
    _Atomic u64* pool_held; // malloc'd, per checker while typechecking under a memory pool: what it held after its last file
    u64 checker_budget; // While typechecking: what each checker may hold (checker_memory split between them)
    u64 checker_start_budget; // While typechecking: what each holds before its first recycle (check_share())
    // program_declaration_diagnostics_cli(): earlier phases reported something
    bool declaration_diagnostics_cli, declaration_diagnostics_earlier;
    bool emit_declarations; // program_set_emit_declarations()
    bool declarations_only; // program_set_declarations_only()
    bool defer_bodies; // program_set_defer_bodies()
    _Atomic bool defer_failed; // program_defer_failed()
    atomic_uint ahead_next; // Materialize-ahead (plan 78, program_typecheck()): the next file of check_files a helper takes
    // Output paths TS5055/TS5056 were reported for (relative to the working directory): tsc's hasEmitBlockingDiagnostics,
    // nothing is written there
    hmap_source_file_ptr emit_blocked;
    bool global_diagnostics_reported; // program_report_global_diagnostics()
    ErrorCtx* declaration_errs; // One per checker while typechecking: the declaration diagnostics of its files
    // While typechecking: a checker reported a diagnostic that is sure to be reported, so the gated declaration
    // diagnostics won't be (declaration_walk_moot())
    atomic_bool declaration_diagnostics_moot;
    u32 threads; // program_set_threads(), 0: 1
    LoadPool* pool; // While loading with threads: parses files ahead of the loader
    i64 (*file_size)(void* ctx, csview path); // program_set_file_size()
    void* file_size_ctx;
    CompilerOptions options;
    // program_set_config_file(): the config and its JSON (with offsets), null / a null value without one
    SourceFile* config_source;
    JsonValue config_json;
    // program_extended_config_error(): the configs the config extends that diagnostics are about
    SourceFile** extended_configs;
    u32 extended_config_count;
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
static SourceFile* program_add_made_source(Program* me, cstr canonical_path, SourceFile* source_file);

Program* program_new(ErrorCtx* errs, Fs const* fs) {
    check(errs);
    check(fs);
    CachedFs* cached = cached_fs_new(fs);
    Program* ret = c_new(Program, {
        .errs = errs,
        .fs = cached_fs_fs(cached),
        .cached_fs = cached,
        .binder = binder_new(errs),
        .options = compiler_options_init(),
        .symlinks = symlink_cache_new(),
        .checker_memory = UINT64_MAX,
        .atoms = atom_table_new(),
        .type_directory = type_directory_new(),
        // (TTTT_FIRST_TOUCH_STAMPS=1: --first-touch-stamps for every program, the suite's too)
        .first_touch_stamps = getenv("TTTT_FIRST_TOUCH_STAMPS") && strcmp(getenv("TTTT_FIRST_TOUCH_STAMPS"), "0"),
        .checker_memory_cap = UINT64_MAX,
        .checker_memory_shared = UINT64_MAX,
        .memory_pool = UINT64_MAX,
    });
    static atomic_uint serials;
    ret->serial = atomic_fetch_add(&serials, 1) + 1;
    pthread_mutex_init(&ret->dependency_links_lock, nullptr);
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
    pthread_t* workers = c_new_n(pthread_t, threads + 1);
    u32 started = 0;
    while (started + 1 < threads && !worker_thread_create(&workers[started], drop_source_files_thread, &drops)) started++;
    drop_source_files_thread(&drops);
    for (u32 t = 0; t < started; t++) worker_thread_join(workers[t]);
    free(workers);
    free(drops.files);
}

typedef struct Preresolved Preresolved;
static void preresolved_destroy(Preresolved* me);

static void program_free_preresolved(Program* me) {
    for (u32 i = 0; i < my preresolved_garbage_count; i++) preresolved_destroy(my preresolved_garbage[i]);
    free(my preresolved_garbage);
    my preresolved_garbage = nullptr;
    my preresolved_garbage_count = my preresolved_garbage_cap = 0;
}

void program_join_releases(Program* me) {
    if (!my release_started) return;
    worker_thread_join(my release_thread);
    my release_started = false;
}

static void program_drop(Program* me) {
    program_join_releases(me);
    load_pool_destroy(my pool);
    program_free_preresolved(me);
    content_mapper_host_drop(atomic_load(&my content_mapper_host));
    // First, its memos point into the AST and the scopes
    for (u32 i = 1; i < my checker_count; i++) checker_drop(my checkers[i]);
    for (u32 i = 0; i < my retired_count; i++) if (my retired[i] != my checker) checker_drop(my retired[i]);
    free(my retired);
    free(my file_shares);
    free(my file_checkers);
    vec_source_file_ptr_drop(&my check_files);
    free(my checkers);
    checker_drop(my checker);
    graph_drop(my graph);
    atom_table_drop(my atoms);
    type_directory_drop(my type_directory);
    drop_source_files(me);
    hmap_box_source_file_drop(&my source_files_map);
    hmap_source_file_ptr_drop(&my files_by_package_id);
    vec_source_file_ptr_drop(&my load_roots);
    for (u32 i = 0; i < my deferred_lib_count; i++) cstr_drop(&my deferred_libs[i].path);
    free(my deferred_libs);
    hmap_source_file_ptr_drop(&my emit_blocked);
    hmap_package_flag_drop(&my packages);
    vec_source_file_ptr_drop(&my source_files_ordered);
    module_resolver_destroy(my resolver);
    symlink_cache_destroy(my symlinks);
    pthread_mutex_destroy(&my dependency_links_lock);
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
    for (u32 i = 0; i < my extended_config_count; i++) {
        source_file_drop(my extended_configs[i]);
        free(my extended_configs[i]);
    }
    free(my extended_configs);
    binder_destroy(my binder); // After the AST: type nodes in it may own things symbols point to
    cached_fs_destroy(my cached_fs);
}

static csview program_cwd(Program const* me);

SourceFile const* program_set_config_file(Program* me, csview path) {
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
    // (Named like the other files, relative to the working directory: diagnostics sort by it)
    my config_source = c_new(SourceFile, source_file_make(relative_path_of(program_cwd(me), path), contents));
    source_file_register(my config_source);
    my config_source->is_json = true;
    my config_source->is_config = true;
    my config_source->unchecked = true;
    // tsc's parseJsonText: the config's syntax errors (TS1005 & co) are its parsing diagnostics, reported whatever
    // else is wrong (they stop nothing); what json_parse() recovers of a broken config is still what's read. tsgo's
    // test harness reports them too (tsconfigRootdirInclude)
    bool own_options = !compiler_options_private;
    if (own_options) set_compiler_options(&my options);
    parse_file(my config_source, my errs);
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

// Where tsc's createDiagnosticForOption puts a diagnostic about an option: on the key (or the value) of the first member
// named name or name2 of the config's compilerOptions (tsgo's ForEachPropertyAssignment; tsc 5 used every one), else on
// the `compilerOptions` key (createCompilerOptionsDiagnostic), else nowhere (a global diagnostic). One site
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
            // (The first property named either: tsgo's ForEachPropertyAssignment returns the callback's first result,
            // `"sourceMap": false, "mapRoot": "m"` has its TS5069 at sourceMap only)
            break;
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

static cstr shown_spec_list(StrList const* specs) {
    cstr ret = cstr_lit("[");
    for (isize i = 0; i < specs->count; i++) {
        cstr_append_d(&ret, DEFAULT_DISPLAY, i ? "," : "", "\"", specs->items[i], "\"");
    }
    cstr_append_lit(&ret, "]");
    return ret;
}

bool program_report_empty_files(Program* me) {
    if (!my config_source || my config_json.tag != JSON_OBJECT) return false;
    JsonValue const* files = json_get(&my config_json, "files");
    JsonValue const* references = json_get(&my config_json, "references");
    // (tsc's getConfigFileSpecs)
    bool no_references = !references || (references->tag == JSON_ARRAY && !references->count);
    if (!files || files->tag != JSON_ARRAY || files->count || !no_references || json_get(&my config_json, "extends")) return false;
    cstr config = absolute_path_of(program_cwd(me), cstr_sv(&my config_source->canonical_path));
    // (A config file parsing diagnostic: it gates nothing)
    program_config_error(me, files->start, files->end, 18002, cstr_d("The 'files' list in config file '", config, "' is empty."))
        ->phase = ERROR_PHASE_PARSE;
    cstr_drop(&config);
    return true;
}

bool program_report_no_inputs(Program* me) {
    if (!my config_source || my config_json.tag != JSON_OBJECT) return false;
    JsonValue const* files = json_get(&my config_json, "files");
    JsonValue const* references = json_get(&my config_json, "references");
    cstr config = absolute_path_of(program_cwd(me), cstr_sv(&my config_source->canonical_path));
    bool reported = false;
    // (tsc's canJsonReportNoInputFiles)
    if (files) {
        reported = program_report_empty_files(me);
    } else if (!references && !my options.config_files_given) {
        // (An extended config's specs as tsconfig_load() rebased them, else the config's own)
        cstr include = my options.config_include_shown.count ? shown_spec_list(&my options.config_include_shown) :
            config_string_list(json_get(&my config_json, "include"), "[\"**/*\"]");
        cstr exclude = my options.config_exclude_given ? shown_spec_list(&my options.config_exclude_shown) :
            config_string_list(json_get(&my config_json, "exclude"), "[]");
        if (!my options.config_exclude_given && !json_get(&my config_json, "exclude")) {
            // (The outputs are excluded by default: tsc's getConfigFileSpecs)
            cstr_assign(&exclude, "[");
            csview dirs[] = { cstr_sv(&my options.out_dir), cstr_sv(&my options.declaration_dir) };
            for (u32 i = 0; i < c_arraylen(dirs); i++) {
                if (!dirs[i].size) continue;
                cstr absolute = absolute_path_of(program_cwd(me), dirs[i]);
                cstr item = cstr_d(cstr_size(&exclude) > 1 ? "," : "", "\"", absolute, "\"");
                cstr_append_s(&exclude, item);
                c_drop(cstr, &item, &absolute);
            }
            cstr_append_lit(&exclude, "]");
        }
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

// A diagnostic about a config the config extends (tsc: at its own JSON source file, named relative to the working
// directory like the config); without a location when it can't be read
Error* program_extended_config_error(Program* me, csview path, u32 start, u32 end, unsigned code, cstr message) {
    SourceFile* source = nullptr;
    for (u32 i = 0; i < my extended_config_count && !source; i++) {
        if (csview_equals_sv(cstr_sv(&my extended_configs[i]->canonical_path), path)) source = my extended_configs[i];
    }
    if (!source) {
        cstr contents = cstr_null;
        try (fs_read_file(my fs, path, &contents)) if_err (e) {
            ignore e;
            current_error_destroy();
            return error_ctx_global_error_s(my errs, code, message);
        }
        text_decode(&contents);
        source = c_new(SourceFile, source_file_make(cstr_from_sv(path), contents));
        source_file_register(source);
        source->is_json = true;
        source->is_config = true;
        source->unchecked = true;
        my extended_configs = realloc(my extended_configs, sizeof(SourceFile*) * (my extended_config_count + 1));
        my extended_configs[my extended_config_count++] = source;
    }
    if (end > cstr_size(&source->contents) || start > end) return error_ctx_global_error_s(my errs, code, message);
    char const* text = cstr_str(&source->contents);
    return error_ctx_error_s(my errs, span_make(source, text + start, text + end), code, message);
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
    unused_free_retired(); // (A materialization after checking: its growths)
    if (me) {
        // (The thread that destroys a program is usually the one that asked it for names outside the checkers, the test
        // harness's symbols walk: its symbol chain caches. They're per thread and lazily remade, keyed by program)
        symbol_chain_release_thread();
        scope_release_thread();
        program_drop(me);
        free(me);
    }
}

static ModuleResolver* program_resolver(Program* me) {
    if (!my resolver) {
        my resolver = module_resolver_new(my fs, &my options);
        if (my realpath) module_resolver_set_realpath(my resolver, my realpath, my realpath_ctx);
    }
    return my resolver;
}

// What resolves the module names of a file: the program's resolver, or its referenced project's
static ModuleResolver* program_resolver_of(Program const* me, SourceFile const* file) {
    if (file && file->resolution_project) return my reference_resolvers[file->resolution_project - 1];
    check(my resolver);
    return my resolver;
}

// The options module resolution goes by in a file (tsc's getCompilerOptionsForFile)
static CompilerOptions const* program_options_of(Program const* me, SourceFile const* file) {
    if (file && file->resolution_project) return project_redirects_options(my redirects, file->resolution_project - 1);
    return &my options;
}

void program_set_references(Program* me, ProjectRedirects const* redirects) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    check(!my reference_count && !my pool);
    my redirects = redirects;
    // (One per referenced project: their output declaration files resolve with their project's options,
    // program_make_source())
    my reference_count = project_redirects_count(my redirects);
    my reference_resolvers = c_new_n(ModuleResolver*, my reference_count + 1);
    for (u32 i = 0; i < my reference_count; i++) {
        ModuleResolver* resolver = module_resolver_new(my fs, project_redirects_options(my redirects, i));
        if (my realpath) module_resolver_set_realpath(resolver, my realpath, my realpath_ctx);
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

// libReplacement: the `@typescript/lib-*` package that replaces a lib file (tsgo's fileLoader.pathForLibFile and
// getLibraryNameFromLibFileName: lib.dom.d.ts is @typescript/lib-dom, lib.dom.iterable.d.ts @typescript/lib-dom/iterable,
// lib.es2015.symbol.wellknown.d.ts @typescript/lib-es2015/symbol-wellknown), resolved as a CommonJS module from the
// config's directory, else the current one. Never lib.d.ts. Empty when there's no replacement. `name`: "dom.iterable"
static cstr program_lib_replacement(Program* me, csview name) {
    if (!my options.lib_replacement || csview_equals(name, "default")) return cstr_null;
    cstr library = cstr_lit("@typescript/lib-");
    u32 component = 0;
    for (csview rest = name; rest.size; component++) {
        isize dot = csview_find_sv(rest, c_sv("."));
        csview part = csview_slice(rest, 0, dot);
        if (component == 1) cstr_append_lit(&library, "/");
        else if (component > 1) cstr_append_lit(&library, "-");
        cstr_append_sv(&library, part);
        if (dot == c_NPOS) break;
        rest = csview_slice(rest, dot + 1, INTPTR_MAX);
    }
    csview base = cstr_size(&my options.config_dir) ? cstr_sv(&my options.config_dir) : program_cwd(me);
    cstr from = cstr_d(base, "/__lib_node_modules_lookup_lib.", name, ".d.ts__.ts");
    ResolvedModule resolved = module_resolver_resolve(program_resolver(me), cstr_sv(&library), cstr_sv(&from), RESOLUTION_MODE_COMMONJS);
    cstr ret = resolved.kind != RESOLVED_NONE ? cstr_clone(resolved.path) : cstr_null;
    resolved_module_drop(&resolved);
    c_drop(cstr, &library, &from);
    return ret;
}

// Loads a lib file's libReplacement (program_lib_replacement()): true if it has one. It isn't a lib file (the test
// baselines show it)
static bool program_load_lib_replacement(Program* me, csview name) {
    cstr replacement = program_lib_replacement(me, name);
    if (!cstr_size(&replacement)) return false;
    bool loading_stdlib = my loading_stdlib;
    my loading_stdlib = false;
    program_add_source_idempotent(me, cstr_sv(&replacement));
    my loading_stdlib = loading_stdlib;
    cstr_drop(&replacement);
    return true;
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

// relative_path_of(program_cwd(me), path), which every lookup of a file by path does: without normalizing (two
// allocations and a segment walk) a path that is already normalized and in the working directory, the common case. A
// view of path then, else of *storage (which the caller drops)
// The loader's hint (program_load_module_ref()): a load pool worker's canonical view of the path whose characters are at
// buf, a resolution's (the resolver's, not moved meanwhile). The worker scanned the path, the loader needn't
static thread_local struct { char const* buf; isize size; csview canonical; } canonical_hint;

// The last source_files_map lookup by a view whose characters are at buf (the loader asks for one path two or three
// times per import: whether it's new, then to add it). Forgotten at every insert
static thread_local struct { Program const* program; char const* buf; isize size; SourceFile* found; } lookup_memo;

typedef struct SourceFile SourceFile;
static SourceFile* program_lookup_canonical(Program const* me, csview canonical);

// relative_path_of(cwd, path) for a normalized absolute path and working directory other than `/`, in one allocation:
// what a file outside the working directory costs every lookup (a worktree's symlinked corpora, a package's real
// path), ~3000 instructions in relative_path_of()'s copies and appends
static cstr relative_normalized(csview cwd, csview path) {
    char const* c = cwd.buf + 1;
    char const* c_end = cwd.buf + cwd.size;
    char const* p = path.buf + 1;
    char const* p_end = path.buf + path.size;
    while (c < c_end && p < p_end) {
        char const* c_slash = memchr(c, '/', (size_t)(c_end - c));
        char const* p_slash = memchr(p, '/', (size_t)(p_end - p));
        isize c_len = (c_slash ? c_slash : c_end) - c, p_len = (p_slash ? p_slash : p_end) - p;
        if (c_len != p_len || memcmp(c, p, (size_t)c_len)) break;
        c = c_slash ? c_slash + 1 : c_end;
        p = p_slash ? p_slash + 1 : p_end;
    }
    isize ups = 0;
    for (char const* i = c; i < c_end; ups++) {
        char const* slash = memchr(i, '/', (size_t)(c_end - i));
        i = slash ? slash + 1 : c_end;
    }
    cstr ret = cstr_with_capacity(3 * ups + (p_end - p));
    for (isize i = 0; i < ups; i++) cstr_append_lit(&ret, "../");
    cstr_append_n(&ret, p, p_end - p);
    return ret;
}

static csview program_canonical_view(Program const* me, csview path, cstr* storage) {
    if (path.buf == canonical_hint.buf && path.size == canonical_hint.size && path.buf) return canonical_hint.canonical;
    csview cwd = program_cwd(me);
    if (path_is_normalized(path)) {
        if (path.buf[0] != '/') return path;
        if (cwd.size > 1 && path.size > cwd.size + 1 && path.buf[cwd.size] == '/' && !memcmp(path.buf, cwd.buf, cwd.size))
            return csview_slice(path, cwd.size + 1, path.size);
        if (cwd.size > 1 && cwd.buf[0] == '/' && path_is_normalized(cwd)) {
            *storage = relative_normalized(cwd, path);
            return cstr_sv(storage);
        }
    }
    *storage = relative_path_of(cwd, path);
    return cstr_sv(storage);
}

static cstr program_canonical_path(Program const* me, csview path) {
    cstr storage = cstr_null;
    csview view = program_canonical_view(me, path, &storage);
    return cstr_size(&storage) ? storage : cstr_from_sv(view);
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

static void program_load_resolved(Program* me, SourceFile* source, ModuleRef* ref, ResolvedModule* resolved_ptr);
static bool program_rewrite_crosses_projects(Program* me, SourceFile const* target);

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
    cstr absolute = path.size && path.buf[0] == '/' ? cstr_from_sv(path) : absolute_path_of(program_cwd(me), path);
    csview ret = project_redirects_source_of_output(my redirects, cstr_sv(&absolute));
    // (Through a link that wasn't followed: preserveSymlinks, tsgo's realpathDtsToSource)
    if (!ret.size && csview_contains(cstr_sv(&absolute), "/node_modules/") && my resolver) {
        cstr real = module_resolver_real_path(my resolver, cstr_sv(&absolute));
        if (cstr_size(&real)) ret = project_redirects_source_of_output(my redirects, cstr_sv(&real));
        cstr_drop(&real);
    }
    cstr_drop(&absolute);
    return ret;
}

// The output declaration file of a referenced project's source (absolute), empty if path isn't one
static csview program_output_of_source(Program const* me, csview path) {
    if (!my redirects || is_declaration_path(path)) return c_sv("");
    if (path.size && path.buf[0] == '/') return project_redirects_output_of_source(my redirects, path);
    cstr absolute = absolute_path_of(program_cwd(me), path);
    csview ret = project_redirects_output_of_source(my redirects, cstr_sv(&absolute));
    cstr_drop(&absolute);
    return ret;
}

// A resolution that found a referenced project's source finds its output declaration file (tsc's findSourceFileWorker,
// getRedirectFromSourceFile). Returns the output when it hasn't been built (resolved is then unchanged), empty otherwise
static csview program_output_of_resolved(Program const* me, ResolvedModule* resolved) {
    if ((resolved->kind != RESOLVED_TS && resolved->kind != RESOLVED_JS)) return c_sv("");
    csview output = program_output_of_source(me, cstr_sv(&resolved->path));
    if (!output.size) return output;
    if (!fs_is_reg_file(my fs, output)) return output;
    cstr_assign_sv(&resolved->path, output);
    resolved->kind = RESOLVED_DTS;
    resolved->is_external_library = false;
    cstr_take(&resolved->package_id, cstr_null);
    return c_sv("");
}

// A resolution that found a referenced project's source finds its output, or nothing when it hasn't been built
static void program_redirect_resolved(Program const* me, ResolvedModule* resolved) {
    if (program_output_of_resolved(me, resolved).size) resolved->kind = RESOLVED_NONE;
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
    if (ref->resolution_override == IMPORT_RESOLUTION_IMPORT) mode = RESOLUTION_MODE_ESM;
    if (ref->resolution_override == IMPORT_RESOLUTION_REQUIRE) mode = RESOLUTION_MODE_REQUIRE_SYNTAX;
    return mode;
}

// SourceFile.preresolved: a worker's resolutions of the file's imports, in module_refs order (the loader asks in that
// order too, so it looks from where it found the last). What the resolver's cache has for them, minus the lookup
typedef struct PreresolvedEntry {
    ModuleRef const* ref;
    ResolutionMode mode;
    ResolvedModule const* resolved; // The resolver's (module_resolver_resolve_shared())
    csview canonical; // Of resolved->path when it's a view of it (program_canonical_view() without storage), else empty
} PreresolvedEntry;

typedef struct Preresolved {
    PreresolvedEntry* entries;
    u32 count, next;
} Preresolved;

static void preresolved_destroy(Preresolved* me) {
    if (!me) return;
    free(my entries);
    free(me);
}

// The worker's resolution of ref in mode, if it made one (and its path's canonical view, see PreresolvedEntry)
static ResolvedModule const* preresolved_take(Preresolved* me, ModuleRef const* ref, ResolutionMode mode, csview* canonical) {
    if (!me) return nullptr;
    for (u32 n = 0; n < my count; n++) {
        u32 i = (my next + n) % my count;
        PreresolvedEntry* entry = &my entries[i];
        if (entry->ref != ref) continue;
        if (entry->mode != mode) return nullptr;
        entry->ref = nullptr;
        my next = i + 1;
        *canonical = entry->canonical;
        return entry->resolved;
    }
    return nullptr;
}

// Where a file's module names resolve from: a referenced project's output declaration file's resolve from its source
// (tsgo's getRedirectForResolution), with its project's options (program_resolver_of())
static csview program_resolution_containing(Program const* me, SourceFile const* file) {
    if (!file) return c_sv("");
    if (file->resolution_project && file->is_declaration) {
        csview source = program_source_of_output(me, cstr_sv(&file->canonical_path));
        if (source.size) return source;
    }
    return cstr_sv(&file->canonical_path);
}

// A resolved TypeScript file becomes part of the program, anything else is remembered on the ref as why there's no file
static void program_load_module_ref_inner(Program* me, SourceFile* source, ModuleRef* ref) {
    program_resolver(me);
    ModuleResolver* resolver = program_resolver_of(me, source);
    csview containing = program_resolution_containing(me, source);
    ResolutionMode mode = program_module_ref_mode(me, source, ref);
    csview canonical = {0};
    ResolvedModule const* shared = preresolved_take(source->preresolved, ref, mode, &canonical);
    if (!shared) shared = module_resolver_resolve_shared(resolver, cstr_sv(&ref->path), containing, mode);
    // (The resolver's, unless a referenced project's output may replace what it found: then a copy to change)
    bool owned = my redirects;
    ResolvedModule resolved = owned ? resolved_module_clone(shared) : *shared;
    if (!owned && canonical.size) {
        canonical_hint.buf = cstr_str(&resolved.path);
        canonical_hint.size = cstr_size(&resolved.path);
        canonical_hint.canonical = canonical;
    }
    program_learn_symlink(me, &resolved);
    csview unbuilt = program_output_of_resolved(me, &resolved);
    if (unbuilt.size && resolved.kind == RESOLVED_JS) {
        // (A referenced project's JavaScript source: tsc's resolveExternalModule says TS7016 for any resolution to
        // something other than TypeScript or JSON before it looks for a project reference redirect)
        ref->failure = MODULE_REF_IS_JS;
        cstr_take(&module_ref_rare_mut(ref)->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
        if (!module_name_is_relative(cstr_sv(&ref->path))) module_ref_rare_mut(ref)->package_name = cstr_clone(resolved.package_name);
        if (owned) resolved_module_drop(&resolved);
        return;
    }
    if (unbuilt.size) {
        ref->failure = MODULE_REF_NOT_BUILT;
        cstr_take(&module_ref_rare_mut(ref)->unbuilt_output, cstr_from_sv(unbuilt));
        cstr_take(&module_ref_rare_mut(ref)->resolved_path, cstr_clone(resolved.path));
        if (owned) resolved_module_drop(&resolved);
        return;
    }
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
        if (owned) resolved_module_drop(&resolved);
        return;
    }
    // An augmentation doesn't load what it augments (see ModuleRef.augmented_path), what can't be loaded anyway goes
    // the usual way (TS2665 for an untyped module)
    bool loadable = resolved.kind == RESOLVED_TS || resolved.kind == RESOLVED_DTS || (resolved.kind == RESOLVED_JS && my options.allow_js);
    if (ref->is_declaration && loadable) {
        ref->file = program_find_source(me, cstr_sv(&resolved.path));
        cstr_take(&module_ref_rare_mut(ref)->augmented_path, cstr_clone(resolved.path));
        if (owned) resolved_module_drop(&resolved);
        return;
    }
    bool is_new = resolved.kind != RESOLVED_NONE && !program_find_source(me, cstr_sv(&resolved.path));
    if (cstr_size(&resolved.alternate_result)) {
        cstr_take(&module_ref_rare_mut(ref)->alternate_result, program_path_for_message(me, cstr_sv(&resolved.alternate_result)));
    }
    ref->external_library = resolved.is_external_library;
    if (resolved.is_external_library) my node_modules_depth++;
    my include_site = ref->span;
    my include_kind = INCLUDE_IMPORT;
    my include_package_id = cstr_sv(&resolved.package_id);
    program_load_resolved(me, source, ref, &resolved);
    my include_site = span_null;
    my include_kind = INCLUDE_NONE;
    my include_package_id = c_sv("");
    if (resolved.is_external_library) my node_modules_depth--;
    if (is_new && ref->file && !ref->is_declaration) ref->file->loaded_by_import = true;
    if (ref->file && !span_valid(ref->file->included_at)) ref->file->included_at = ref->span;
    if (ref->file && ref->used_ts_extension && my options.rewrite_relative_import_extensions) {
        ref->rewrite_crosses_projects = program_rewrite_crosses_projects(me, ref->file);
    }
    if (owned) resolved_module_drop(&resolved);
}

static void program_load_module_ref(Program* me, SourceFile* source, ModuleRef* ref) {
    program_load_module_ref_inner(me, source, ref);
    // (The hint is good for this resolution only: its path may be freed, and the address reused, after loading)
    canonical_hint.buf = nullptr;
    canonical_hint.size = 0;
    lookup_memo.buf = nullptr;
}

// The same version of a package installed twice is one package (tsc's packageIdToSourceFile), unless
// `deduplicatePackages: false`: a file gets the id of the first resolution that reached it with one (an import, a type
// reference), program_collect_files() decides which copy is the program's once everything is loaded
static void program_note_package_id(Program* me, SourceFile* file, ResolvedModule const* resolved) {
    if (!file || !cstr_size(&resolved->package_id) || !my options.deduplicate_packages || cstr_size(&file->package_id)) return;
    cstr_assign_sv(&file->package_id, cstr_sv(&resolved->package_id));
    auto found = hmap_source_file_ptr_insert(&my files_by_package_id, cstr_clone(resolved->package_id), file);
    if (!found.inserted) my duplicate_packages = true;
}

// (resolved stays the caller's)
static void program_load_resolved(Program* me, SourceFile* source, ModuleRef* ref, ResolvedModule* resolved_ptr) {
    ResolvedModule const resolved = *resolved_ptr;
    switch (resolved.kind) {
        case RESOLVED_TS:
        case RESOLVED_DTS:
            if (my options.jsx == JSX_NONE && csview_ends_with(cstr_sv(&resolved.path), ".tsx")) {
                ref->failure = MODULE_REF_JSX_NOT_SET;
                cstr_take(&module_ref_rare_mut(ref)->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
            } else if (resolved.arbitrary_extension && !my options.allow_arbitrary_extensions && !source->is_declaration) {
                ref->failure = MODULE_REF_ARBITRARY_EXTENSION;
                cstr_take(&module_ref_rare_mut(ref)->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
            } else {
                ref->file = program_add_source_idempotent(me, cstr_sv(&resolved.path));
                // (Only an extension the name itself has counts, not one a package.json or an export map wrote:
                // tsc's candidateIsFromPackageJsonField, tryExtractTSExtension of the name)
                ref->used_ts_extension = resolved.used_ts_extension &&
                    (resolved.ts_extension_from_star || name_has_ts_extension(cstr_sv(&ref->path)));
                program_note_package_id(me, ref->file, &resolved);
            }
            break;
        case RESOLVED_JS:
            if (my options.jsx == JSX_NONE && csview_ends_with(cstr_sv(&resolved.path), ".jsx")) {
                ref->failure = MODULE_REF_JSX_NOT_SET;
                cstr_take(&module_ref_rare_mut(ref)->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
                break;
            }
            // JavaScript of a package is part of the program only down to maxNodeModuleJsDepth (0 by default)
            // (a package that is a link to somewhere outside of any node_modules is the project's own code)
            bool package_js = resolved.is_external_library &&
                csview_find_sv(cstr_sv(&resolved.path), c_sv("/node_modules/")) != c_NPOS;
            if (my options.allow_js && (!package_js || source->node_modules_js_depth < my options.max_node_module_js_depth)) {
                bool is_new = !program_find_source(me, cstr_sv(&resolved.path));
                ref->file = program_add_source_idempotent(me, cstr_sv(&resolved.path));
                program_note_package_id(me, ref->file, &resolved);
                // (tsc's maxNodeModuleJsDepth: a package's JavaScript, as deep as allowed, never checked)
                if (ref->file && package_js && is_new) {
                    ref->file->node_modules_js_depth = source->node_modules_js_depth + 1;
                    ref->file->unchecked = true;
                }
            } else if (my options.allow_js && program_find_source(me, cstr_sv(&resolved.path))) {
                // Not added for this import, but in the program anyway (a root file): tsc's resolveExternalModule finds
                // it with host.getSourceFile
                ref->file = program_find_source(me, cstr_sv(&resolved.path));
            } else {
                ref->failure = MODULE_REF_IS_JS;
                cstr_take(&module_ref_rare_mut(ref)->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
                if (!module_name_is_relative(cstr_sv(&ref->path))) module_ref_rare_mut(ref)->package_name = cstr_clone(resolved.package_name);
            }
            break;
        case RESOLVED_JSON:
            ref->file = program_add_source_idempotent(me, cstr_sv(&resolved.path));
            program_note_package_id(me, ref->file, &resolved);
            if (!ref->file) ref->failure = MODULE_REF_IS_JSON;
            break;
        case RESOLVED_NONE:
            if (resolved.json_without_option) {
                ref->failure = MODULE_REF_JSON_WITHOUT_OPTION;
            } else if (resolved.needs_extension) {
                ref->failure = MODULE_REF_NEEDS_EXTENSION;
                // (None for a path ending in a separator: tsgo's HasTrailingDirectorySeparator check, `./foo/` isn't
                // `./foo/.js`)
                if (cstr_size(&resolved.suggested_extension) && !cstr_ends_with(&ref->path, "/")) {
                    cstr_take(&module_ref_rare_mut(ref)->resolved_path, cstr_d(ref->path, resolved.suggested_extension));
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
// directive (null for a lib's `/// <reference lib>`) learns of an unbuilt referenced project's output (TS6305)
static SourceFile* program_load_reference_path(
    Program* me, Span path_span, csview path, SourceDirectiveStatement* directive
) {
    check(span_valid(path_span));
    csview dir = dirname_of(cstr_sv(&span_source(path_span)->canonical_path));
    // (A drive root `c:/` is rooted too: one of the current directory, see path_is_drive_rooted())
    bool rooted = (path.size && path.buf[0] == '/') || path_is_drive_rooted(path);
    cstr full = rooted ? cstr_from_sv(path) : cstr_d(dir, "/", path);
    SourceFile* ret = nullptr;
    static const char* const extensions[] = { "", ".ts", ".tsx", ".d.ts" };
    bool extension = path_has_extension(path);
    for (isize i = extension ? 0 : 1; i < (extension ? 1 : (isize)c_arraylen(extensions)) && !ret; i++) {
        cstr candidate = cstr_d(full, extensions[i]);
        csview source = fs_is_reg_file(my fs, cstr_sv(&candidate)) ? cstr_sv(&candidate) : c_sv("");
        // A referenced project's source is its output; when it hasn't been built that's TS6305 (tsc's
        // getSourceFileFromReferenceWorker through processSourceFile)
        csview output = source.size ? program_output_of_source(me, source) : c_sv("");
        if (output.size && !fs_is_reg_file(my fs, output)) {
            if (directive) {
                cstr_take(&directive->unbuilt_output, cstr_from_sv(output));
                cstr_take(&directive->unbuilt_source, absolute_path_of(program_cwd(me), source));
            }
            cstr_drop(&candidate);
            break;
        }
        if (output.size) source = output;
        if (source.size) {
            my include_site = path_span;
            my include_kind = INCLUDE_REFERENCE;
            ret = program_add_source_idempotent(me, source);
            my include_site = span_null;
            my include_kind = INCLUDE_NONE;
            if (ret && !span_valid(ret->included_at)) ret->included_at = path_span;
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
        program_resolver_of(me, source), name, program_resolution_containing(me, source),
        has_mode ? &mode : nullptr
    );
    program_learn_symlink(me, &resolved);
    program_redirect_resolved(me, &resolved);
    if (resolved.is_external_library) my node_modules_depth++;
    SourceFile* ret = resolved.kind == RESOLVED_NONE ? nullptr :
        program_add_source_idempotent(me, cstr_sv(&resolved.path));
    program_note_package_id(me, ret, &resolved);
    if (resolved.is_external_library) my node_modules_depth--;
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
                    if (path_is_network(cstr_sv(&statement->source_directive->path))) {
                        // (tsgo 7.1.0-beta's getSourceFileFromReference: TS5116, not looked up)
                        cstr_assign_sv(&statement->source_directive->not_found, cstr_sv(&statement->source_directive->path));
                        statement->source_directive->not_found_network = true;
                        break;
                    }
                    SourceFile const* referenced = program_load_reference_path(
                        me,
                        statement->source_directive->path_span,
                        cstr_sv(&statement->source_directive->path),
                        statement->source_directive
                    );
                    // (tsc's getSourceFileFromReferenceWorker)
                    if (referenced == source) statement->source_directive->self_reference = true;
                    statement->source_directive->referenced = referenced;
                    if (!referenced && !cstr_size(&statement->source_directive->unbuilt_output)) {
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
                    csview lib_name = csview_slice(cstr_sv(&name), 0, cstr_size(&name) - 5); // (Without `.d.ts`)
                    if ((my loading_stdlib || (my follow_lib_references && !my options.no_lib)) &&
                        program_load_lib_replacement(me, lib_name)
                    ) {
                        // (Replaced: libReplacement)
                    } else if (my loading_stdlib) {
                        program_load_reference_path(me, statement->source_directive->path_span, cstr_sv(&name), nullptr);
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

        default:
            break;
    }
}

// What of a file's top-level statements makes it a module (tsc's isAnExternalModuleIndicatorNode): set once, on the
// thread that parsed it (a load pool worker), so the loader doesn't walk a file's statements for it
static void source_scan_module_indicators(SourceFile* source) {
    if (source->module_indicators_scanned) return;
    source->module_indicators_scanned = true;
    for_each (i, vec_statement, &source->ast.statements) {
        Statement* statement = (Statement*)i;
        switch (statement->tag) {
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
            default:
                break;
        }
    }
}

// Loads the file at the given path, adds it to the program, and returns a SourceFile for it. Returns nullptr on error.
// Only loads each file once, even if paths aren't expressed identically. Calls itself recursively to load imports and
// referenced files.
// Where a file's leading comments start: past a BOM and a `#!` line (tsc's scanner takes both as trivia)
static const char* past_bom_and_shebang(csview contents) {
    const char* c = contents.buf;
    const char* end = contents.buf + contents.size;
    if (end - c >= 3 && (uint8_t)c[0] == 0xEF && (uint8_t)c[1] == 0xBB && (uint8_t)c[2] == 0xBF) c += 3;
    if (end - c >= 2 && c[0] == '#' && c[1] == '!') {
        while (c < end && *c != '\n') c++;
    }
    return c;
}

// `// @ts-check` (1) or `// @ts-nocheck` (-1) in the comments at the top of a file, the last one wins. 0 without
static int source_check_directive(csview contents) {
    int ret = 0;
    const char* c = past_bom_and_shebang(contents);
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
    const char* c = past_bom_and_shebang(contents);
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

// A content-mapped file's text through its mapper (plan 56): *contents becomes the transformed text, empty on a failure
static ContentMapperOutput program_content_map(Program const* me, ContentMapper const* mapper, csview canonical_path,
    cstr* contents) {
    ContentMapperHost* host = atomic_load(&my content_mapper_host);
    if (!host) {
        cstr config = my config_source ? absolute_path_of(program_cwd(me), cstr_sv(&my config_source->canonical_path)) :
            cstr_null;
        ContentMapperHost* made = content_mapper_host_new(cstr_sv(&config), &my options);
        cstr_drop(&config);
        if (atomic_compare_exchange_strong(&((Program*)me)->content_mapper_host, &host, made)) host = made;
        else content_mapper_host_drop(made);
    }
    ContentMapperOutput ret = {0};
    if (!mapper->resolved) {
        // TODO(plan 56 part 2): what tsgo does with a mapper whose package didn't resolve (TS18075-18079)
        ret.failure_code = 18069;
        ret.failure = cstr_d("The content mapper '", mapper->package, "' failed to transform this file.");
    } else {
        cstr absolute = absolute_path_of(program_cwd(me), canonical_path);
        ret = content_mapper_host_transform(host, mapper, cstr_sv(&absolute), cstr_sv(contents));
        // (tsgo's CheckSupplementalFileNameCollisions: a supplemental output can't be named like a real file)
        for (u32 i = 0; !ret.failure_code && i < ret.supplemental_count; i++) {
            cstr name = cstr_d(absolute, ".", i, ret.supplemental[i].extension);
            if (fs_exists(my fs, cstr_sv(&name))) {
                cstr shown = program_path_for_message(me, cstr_sv(&name));
                content_mapper_output_drop(&ret);
                ret.failure_code = 18069;
                ret.failure = cstr_d("The content mapper '", cstr_size(&mapper->name) ? cstr_sv(&mapper->name) :
                    cstr_sv(&mapper->package), "' failed to transform this file.");
                ret.detail_code = 18096;
                ret.detail = cstr_d("Content mapper supplemental output file '", shown, "' conflicts with an existing file.");
                cstr_drop(&shown);
                content_mapper_count_failure(host, mapper, &ret);
            }
            cstr_drop(&name);
        }
        cstr_drop(&absolute);
    }
    // The original text is kept by program_attach_content_mapped() (in ret.text's place: they swap)
    cstr transformed = ret.failure_code ? cstr_null : ret.text;
    ret.text = *contents;
    *contents = transformed;
    return ret;
}

// The mapper's output on the file made from its transformed text: the original text as a file of its own, the span
// map, the mapper's diagnostics (in the original text) or its failure (at the start of the empty placeholder)
static void program_attach_content_mapped(SourceFile* file, ContentMapper const* mapper, ContentMapperOutput* out,
    ErrorCtx* errs) {
    ContentMapped* mapped = c_new(ContentMapped, {
        .mapper = mapper,
        .identity = content_mapper_identity(mapper),
        .original = c_new(SourceFile, source_file_make(cstr_clone(file->canonical_path), out->text)),
        .segments = out->segments,
        .segment_count = out->segment_count,
        .directives = out->failure_code ? nullptr : out->directives,
        .directive_count = out->failure_code ? 0 : out->directive_count,
        .has_map = !out->failure_code,
        .diagnostic_source = out->diagnostic_source,
        .virtual_extension = out->failure_code || !cstr_size(&out->extension) ? cstr_from(".ts") : cstr_clone(out->extension),
    });
    out->text = cstr_null;
    out->segments = nullptr;
    out->diagnostic_source = cstr_null;
    if (!out->failure_code) {
        out->directives = nullptr;
        out->directive_count = 0;
    }
    source_file_register(mapped->original);
    mapped->original->content_original = true;
    file->content_mapped = mapped;
    if (out->global_code) {
        Error* error = error_ctx_global_error_s(errs, out->global_code, out->global);
        out->global = cstr_null;
        if (out->global_detail_code) {
            error_sub_error_s(error, span_null, out->global_detail_code, out->global_detail);
            out->global_detail = cstr_null;
        }
    }
    if (out->silent) return;
    if (out->failure_code) {
        char const* start = cstr_str(&file->contents);
        Error error = { .message = out->failure, .span = span_make(file, start, start), .ts_code = out->failure_code,
            .phase = ERROR_PHASE_PARSE };
        out->failure = cstr_null;
        if (out->detail_code) {
            error_sub_error_s(&error, span_null, out->detail_code, out->detail);
            out->detail = cstr_null;
            if (out->detail_sub_code) {
                error_sub_error_s(vec_error_back_mut(&error.sub_errors), span_null, out->detail_sub_code, out->detail_sub);
                out->detail_sub = cstr_null;
            }
        }
        error_ctx_append_error(errs, error);
        return;
    }
    char const* original = cstr_str(&mapped->original->contents);
    for (u32 i = 0; i < out->diagnostic_count; i++) {
        ContentMapperDiagnostic* d = &out->diagnostics[i];
        Error error = { .message = d->message, .ts_code = d->code, .phase = ERROR_PHASE_PARSE,
            .span = span_make(mapped->original, original + d->start, original + d->start + d->length),
            .source = cstr_str(&mapped->diagnostic_source) };
        d->message = cstr_null;
        error_ctx_append_error(errs, error);
    }
}

// A new SourceFile for the file at canonical_path, parsed (its errors into errs), null if it can't be read. On the
// loader's thread or on a load pool worker: it only reads the program
static SourceFile* program_make_source_text(Program const* me, csview canonical_path, cstr contents,
    ContentMapper const* mapper, ContentMapperOutput* mapped, bool is_stdlib, ErrorCtx* errs);

static SourceFile* program_make_source(Program const* me, csview canonical_path, bool is_stdlib, ErrorCtx* errs) {
    cstr contents;
    ContentMapper const* mapper = my options.content_mapper_count && !is_stdlib ?
        content_mapper_for_path(&my options, canonical_path) : nullptr;
    // (Not a test's virtual files, nor text that changes: a UTF-16 file or what a content mapper reads)
    u32 mapping = cached_fs_inner(my cached_fs) == fs && !mapper ? source_text_map(canonical_path, &contents) : 0;
    if (mapping) {
        unsigned char const* b = (unsigned char const*)cstr_str(&contents);
        if ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF)) {
            source_text_unmap(mapping);
        } else {
            SourceFile* made = program_make_source_text(me, canonical_path, contents, nullptr, nullptr, is_stdlib, errs);
            made->text_mapping = mapping;
            return made;
        }
    }
    try (fs_read_file(my fs, canonical_path, &contents)) if_err (e) {
        // Not worth reporting: the caller turns a null return into a TypeScript "file not found" error
        ignore e;
        current_error_destroy();
        return nullptr;
    }
    text_decode(&contents);

    ContentMapperOutput mapped = {0};
    if (mapper) {
        // (Parsed as its virtual name, tsgo's ParseResult; a failure is an empty TypeScript file)
        mapped = program_content_map(me, mapper, canonical_path, &contents);
    }
    return program_make_source_text(me, canonical_path, contents, mapper, mapper ? &mapped : nullptr, is_stdlib, errs);
}

// A content mapper's supplemental outputs as files of their own, `<file>.<i><extension>` (tsgo's ParseResult), made
// with the file they come from: the loader adds them after what it imports (ContentMapped.supplementals)
static void program_make_supplementals(Program const* me, SourceFile* file, ContentMapperOutput* out, ErrorCtx* errs) {
    ContentMapped* mapped = file->content_mapped;
    if (!out->supplemental_count || out->failure_code) return;
    mapped->supplementals = calloc(out->supplemental_count, sizeof(SourceFile*));
    for (u32 i = 0; i < out->supplemental_count; i++) {
        ContentMapperSupplemental* supplemental = &out->supplemental[i];
        cstr path = cstr_d(file->canonical_path, ".", i, supplemental->extension);
        SourceFile* made = program_make_source_text(me, cstr_sv(&path), supplemental->text, nullptr, nullptr, false, errs);
        supplemental->text = cstr_null;
        cstr_drop(&path);
        made->content_mapped = c_new(ContentMapped, {
            .mapper = mapped->mapper,
            .identity = cstr_clone(mapped->identity),
            .original = mapped->original,
            .canonical = file,
            .segments = supplemental->segments,
            .segment_count = supplemental->segment_count,
            .has_map = true,
            .directives = supplemental->directives,
            .directive_count = supplemental->directive_count,
            .diagnostic_source = cstr_clone(mapped->diagnostic_source),
            .virtual_extension = cstr_clone(supplemental->extension),
        });
        supplemental->segments = nullptr;
        supplemental->directives = nullptr;
        supplemental->directive_count = 0;
        mapped->supplementals[mapped->supplemental_count++] = made;
    }
}

// A new SourceFile of the given text (the file's, or what its content mapper made of it: mapped)
static SourceFile* program_make_source_text(Program const* me, csview canonical_path, cstr contents,
    ContentMapper const* mapper, ContentMapperOutput* mapped, bool is_stdlib, ErrorCtx* errs) {
    SourceFile* source_file = c_new(SourceFile, source_file_make(cstr_from_sv(canonical_path), contents));
    source_file_register(source_file);
    csview path = cstr_sv(&source_file->canonical_path);
    cstr virtual_path = cstr_null;
    if (mapper) {
        program_attach_content_mapped(source_file, mapper, mapped, errs);
        virtual_path = cstr_d(path, cstr_size(&mapped->extension) && !mapped->failure_code ? cstr_sv(&mapped->extension) :
            c_sv(".ts"));
        path = cstr_sv(&virtual_path);
        program_make_supplementals(me, source_file, mapped, errs);
        content_mapper_output_drop(mapped);
    }

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
    // A referenced project's output declaration file is the program's own, but its module names resolve with its
    // project's options
    if (my redirects && !is_stdlib && source_file->is_declaration) {
        cstr absolute = absolute_path_of(program_cwd(me), path);
        csview source = program_source_of_output(me, cstr_sv(&absolute));
        // (A referenced project's own declaration file, read as itself, resolves with its options too: tsgo's
        // getRedirectForResolution)
        source_file->resolution_project = project_redirects_project_of(my redirects, source.size ? source : cstr_sv(&absolute));
        cstr_drop(&absolute);
        if (source_file->resolution_project) {
            CompilerOptions const* options = project_redirects_options(my redirects, source_file->resolution_project - 1);
            source_file->reference_emits_esm = options->module >= MODULE_KIND_ES2015 && options->module <= MODULE_KIND_ESNEXT;
            // (A built output's const enums are ambient, unless its project preserves them)
            source_file->reference_preserves_const_enums = options->preserve_const_enums || options->isolated_modules ||
                options->verbatim_module_syntax;
        }
    }

    if (is_stdlib) {
        source_file->is_stdlib = true;
        // (tsc's skipTypeChecking: skipDefaultLibCheck leaves the default library's files unchecked)
        if (my options.skip_default_lib_check) source_file->unchecked = true;
        csview filename = basename_of(cstr_sv(&source_file->canonical_path));
        cstr_take(&source_file->decl_override, cstr_d("lib.", filename, ", --, --"));
        cstr_take(&source_file->bundled_path, csview_equals(filename, "default.d.ts") ?
            cstr_from("bundled:///libs/lib.d.ts") : cstr_d("bundled:///libs/lib.", filename));
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
        // (tsc's loadSourceFileMetaData keeps package.json's `type` of a .mts/.cts/.mjs/.cjs file only in node_modules:
        // its extension decides its format, and no "File is ECMAScript module because" explains it)
        bool extension_format = csview_ends_with(canonical, ".mts") || csview_ends_with(canonical, ".cts") ||
            csview_ends_with(canonical, ".mjs") || csview_ends_with(canonical, ".cjs");
        source_file->package_has_type = format.package_has_type && (in_node_modules || !extension_format);
        if (cstr_size(&format.package_json)) {
            source_file->package_json = program_path_for_message(me, cstr_sv(&format.package_json));
        }
        cstr_drop(&format.package_json);
    }

    // (Plan 78: not in a file that isn't checked, whose bodies' syntax errors no checker would find)
    source_file->defer_bodies = my defer_bodies && !source_file->is_js && !source_file->is_json &&
        !source_file->is_declaration && !is_stdlib && !source_file->unchecked && !source_file->content_mapped;
    parse_file(source_file, errs);
    source_collect_commonjs_candidates(source_file);
    source_scan_module_indicators(source_file);
    cstr_drop(&virtual_path);
    return source_file;
}

static void source_file_free(void* ctx, SourceFile* file) {
    ignore ctx;
    preresolved_destroy(file->preresolved);
    file->preresolved = nullptr;
    source_file_drop(file);
    free(file);
}

// What a file queues, queued under one lock once it's all known (the pool's lock is the loader's too)
typedef struct QueueBatch {
    csview* paths; // Canonical
    cstr* storage; // What a path is a view of when the batch owns it, else null
    bool* is_stdlib;
    isize count, cap;
} QueueBatch;

// canonical stays valid until program_queue_batch() unless storage (taken) owns it
static void queue_batch_push(QueueBatch* batch, csview canonical, cstr storage, bool is_stdlib) {
    if (batch->count == batch->cap) {
        batch->cap = batch->cap ? batch->cap * 2 : 16;
        batch->paths = realloc(batch->paths, sizeof(csview) * (size_t)batch->cap);
        batch->storage = realloc(batch->storage, sizeof(cstr) * (size_t)batch->cap);
        batch->is_stdlib = realloc(batch->is_stdlib, sizeof(bool) * (size_t)batch->cap);
        panic_if(!batch->paths || !batch->storage || !batch->is_stdlib, "out of memory");
    }
    batch->storage[batch->count] = storage;
    batch->paths[batch->count] = cstr_size(&storage) ? cstr_sv(&batch->storage[batch->count]) : canonical;
    batch->is_stdlib[batch->count++] = is_stdlib;
}

static void program_queue_path(Program* me, QueueBatch* batch, csview absolute, bool is_stdlib) {
    cstr storage = cstr_null;
    csview canonical = program_canonical_view(me, absolute, &storage);
    if (!cstr_size(&storage)) storage = cstr_from_sv(canonical);
    queue_batch_push(batch, canonical, storage, is_stdlib);
}

static void program_queue_batch(LoadPool* pool, QueueBatch* batch) {
    // (A short string's characters are in the cstr itself: the views are taken once the array stops moving)
    for (isize i = 0; i < batch->count; i++) {
        if (cstr_size(&batch->storage[i])) batch->paths[i] = cstr_sv(&batch->storage[i]);
    }
    load_pool_queue_each(pool, batch->paths, batch->is_stdlib, batch->count);
    for (isize i = 0; i < batch->count; i++) cstr_drop(&batch->storage[i]);
    free(batch->paths);
    free(batch->storage);
    free(batch->is_stdlib);
    *batch = (QueueBatch){0};
}

// Queues what the loader will probably load from a parsed file: a guess (the loader decides with what it learns after
// parsing, like the file's module-ness, the helpers and JSX runtime imports), so a wrong one only costs a parse.
// Resolutions go to the shared resolver's cache, and to the file (SourceFile.preresolved), where the loader finds them
static void program_load_ahead_from(Program* me, LoadPool* pool, SourceFile* file) {
    if (my options.no_resolve) return;
    QueueBatch batch = {0};
    ModuleResolver* resolver = program_resolver_of(me, file);
    csview containing = program_resolution_containing(me, file);
    // (In the loader's order, which the pool's ranks follow: the directives, then the imports)
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
                    program_queue_path(me, &batch, cstr_sv(&full), file->is_stdlib);
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
                if (resolved.kind != RESOLVED_NONE) program_queue_path(me, &batch, cstr_sv(&resolved.path), false);
                resolved_module_drop(&resolved);
            }   break;
            case SOURCE_DIRECTIVE_REFERENCE_LIB: {
                if (!file->is_stdlib && !(my follow_lib_references && !my options.no_lib)) break;
                cstr name = lib_reference_file_name(cstr_sv(&directive->path));
                cstr replacement = program_lib_replacement(me, csview_slice(cstr_sv(&name), 0, cstr_size(&name) - 5));
                cstr path = cstr_size(&replacement) ? cstr_clone(replacement) : file->is_stdlib ? cstr_d(dirname_of(containing), "/", name) :
                    cstr_d(cstr_size(&my lib_dir) ? cstr_sv(&my lib_dir) : default_lib_dir, "/", name);
                program_queue_path(me, &batch, cstr_sv(&path), !cstr_size(&replacement));
                c_drop(cstr, &path, &replacement);
                cstr_drop(&name);
            }   break;
        }
    }
    Preresolved* preresolved = file->module_ref_count ? c_new(Preresolved, {
        .entries = c_new_n(PreresolvedEntry, file->module_ref_count),
    }) : nullptr;
    for (u32 i = 0; i < file->module_ref_count; i++) {
        ModuleRef const* ref = file->module_refs[i];
        if (ref->is_declaration || ref->ambient_only) continue;
        ResolutionMode mode = program_module_ref_mode(me, file, ref);
        ResolvedModule const* shared = module_resolver_resolve_shared(resolver, cstr_sv(&ref->path), containing, mode);
        cstr storage = cstr_null;
        csview canonical = program_canonical_view(me, cstr_sv(&shared->path), &storage);
        if (cstr_size(&storage)) canonical = (csview){0};
        cstr_drop(&storage);
        preresolved->entries[preresolved->count++] = (PreresolvedEntry){
            .ref = ref, .mode = mode, .resolved = shared, .canonical = canonical,
        };
        bool owned = my redirects;
        ResolvedModule resolved = owned ? resolved_module_clone(shared) : *shared;
        program_redirect_resolved(me, &resolved);
        bool load = resolved.kind == RESOLVED_TS || resolved.kind == RESOLVED_DTS || resolved.kind == RESOLVED_JSON ||
            (resolved.kind == RESOLVED_JS && my options.allow_js && !(resolved.is_external_library &&
                csview_contains(cstr_sv(&resolved.path), "/node_modules/")));
        // (The canonical view of the resolver's path stays put: no copy)
        if (load && !owned && canonical.size) queue_batch_push(&batch, canonical, cstr_null, false);
        else if (load) program_queue_path(me, &batch, cstr_sv(&resolved.path), false);
        if (owned) resolved_module_drop(&resolved);
    }
    if (preresolved && !preresolved->count) {
        preresolved_destroy(preresolved);
        preresolved = nullptr;
    }
    preresolved_destroy(file->preresolved);
    file->preresolved = preresolved;
    program_queue_batch(pool, &batch);
}
// A load pool worker's job
static SourceFile* program_parse_ahead(void* ctx, LoadPool* pool, csview path, bool is_stdlib, ErrorCtx* errs) {
    Program* me = ctx;
    // (Content-mapped files are transformed by the loader, in program order: a mapper's failure budget counts
    // them in that order, tsgo's maxContentMapperFailures)
    if (my options.content_mapper_count && content_mapper_for_path(&my options, path)) return nullptr;
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
        my pool = load_pool_new(my threads, program_parse_ahead, source_file_free, me, my file_size, my file_size_ctx);
    }
    return my pool;
}

// A file first found searching node_modules is also one of the program's own: so is what it imports, other than
// through node_modules (tsc's findSourceFileWorker processes its imports again at depth 0)
static void program_found_outside_node_modules(SourceFile* file) {
    file->found_searching_node_modules = false;
    for (u32 i = 0; i < file->module_ref_count; i++) {
        ModuleRef const* ref = file->module_refs[i];
        if (ref->file && !ref->external_library && ref->file->found_searching_node_modules) {
            program_found_outside_node_modules(ref->file);
        }
    }
}

// The loader reached file from the file whose references it is loading (a root without one): tsgo's parse subtasks,
// in order, for program_collect_files()
static void program_note_loaded(Program* me, SourceFile* file) {
    SourceFile* parent = my loading_parent;
    if (!parent) {
        vec_source_file_ptr_push(&my load_roots, file);
        return;
    }
    if (parent->load_child_count == parent->load_child_cap) {
        parent->load_child_cap = parent->load_child_cap ? parent->load_child_cap * 2 : 4;
        parent->load_children = realloc(parent->load_children, sizeof(SourceFile*) * parent->load_child_cap);
        panic_if(!parent->load_children, "out of memory");
    }
    parent->load_children[parent->load_child_count++] = file;
}

// Records why the file being added is (Program.include_kind, once)
static void record_include_reason(Program* me, SourceFile* file) {
    if (!my include_kind) return;
    if (file->include_reason_count == file->include_reason_cap) {
        file->include_reason_cap = file->include_reason_cap ? file->include_reason_cap * 2 : 8;
        file->include_reasons = realloc(file->include_reasons, sizeof(IncludeReason) * file->include_reason_cap);
        panic_if(!file->include_reasons, "out of memory");
    }
    file->include_reasons[file->include_reason_count++] = (IncludeReason){
        .kind = my include_kind, .at = my include_site, .package_id = cstr_from_sv(my include_package_id),
    };
    my include_kind = INCLUDE_NONE;
}

static SourceFile* program_lookup_canonical(Program const* me, csview canonical) {
    if (lookup_memo.buf && lookup_memo.buf == canonical.buf && lookup_memo.size == canonical.size && lookup_memo.program == me) {
        return lookup_memo.found;
    }
    auto found = hmap_box_source_file_get(&my source_files_map, canonical);
    SourceFile* ret = found ? found->second.get : nullptr;
    // (Only during loading, and for a view the hint gave: a resolution's characters, which stay put until then)
    if (my phase == PROGRAM_PHASE_LOADING && canonical_hint.buf && canonical.buf == canonical_hint.canonical.buf) {
        lookup_memo.program = me;
        lookup_memo.buf = canonical.buf;
        lookup_memo.size = canonical.size;
        lookup_memo.found = ret;
    }
    return ret;
}


static isize stdlib_priority(SourceFile const* source);
static SourceFile* program_add_source_idempotent(Program* me, csview path);

static double deferred_libs_now_ms(void) {
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec * 1e3 + (double)now.tv_nsec / 1e6;
}

static void program_defer_lib(Program* me, csview canonical) {
    if (my deferred_lib_count == my deferred_lib_cap) {
        my deferred_lib_cap = my deferred_lib_cap ? my deferred_lib_cap * 2 : 4;
        my deferred_libs = realloc(my deferred_libs, sizeof(DeferredLib) * my deferred_lib_cap);
        panic_if(!my deferred_libs, "out of memory");
    }
    if (!my deferred_lib_count) my deferred_libs_since = deferred_libs_now_ms();
    my deferred_libs[my deferred_lib_count++] = (DeferredLib){
        .path = cstr_from_sv(canonical), .parent = my loading_parent, .site = my include_site, .kind = my include_kind,
        .set_included_at = my include_kind == INCLUDE_REFERENCE,
    };
}

// TTTT_DEFER_LIBS=all: every library the libs phase meets is deferred, pool or not (the oracle of deferral: the output
// must be what TTTT_NO_DEFER_LIBS=1 gives)
static bool defer_all_libs(void) {
    static int all = -1;
    if (all < 0) all = getenv("TTTT_DEFER_LIBS") && !strcmp(getenv("TTTT_DEFER_LIBS"), "all");
    return all;
}

void program_set_defer_libs(Program* me, bool defer) {
    my defer_libs = defer && (program_pool(me) || defer_all_libs());
}

static void program_finish_deferred_lib(Program* me, DeferredLib const* lib) {
    my loading_parent = lib->parent;
    my include_site = lib->site;
    my include_kind = lib->kind;
    my include_package_id = c_sv("");
    my loading_stdlib = true;
    my node_modules_depth = 0;
    SourceFile* file = program_add_source_idempotent(me, cstr_sv(&lib->path));
    my include_kind = INCLUDE_NONE;
    my include_site = span_null;
    if (file && lib->set_included_at && !span_valid(file->included_at)) file->included_at = lib->site;
#ifdef DEBUG_BUILD
    // (What the serial path gives it: a library file, sorted with the libraries, under the parent it was met in)
    check(file && file->is_stdlib && stdlib_priority(file) != INTPTR_MAX);
    bool found_under_parent = !lib->parent;
    if (lib->parent) {
        for (u32 j = 0; j < lib->parent->load_child_count && !found_under_parent; j++) found_under_parent = lib->parent->load_children[j] == file;
    } else {
        for_each (j, vec_source_file_ptr, &my load_roots) found_under_parent |= *j == file;
    }
    check(found_under_parent);
#endif
}

// The libraries the libs phase deferred, in the order it met them, each loaded as the libs phase would have: from its
// parent with its include reason, `loading_stdlib`, outside node_modules, without the compiler options of whatever is
// loading now (reset, then restored) and with its errors dropped like the libs phase's (main.c). What can differ from a
// load that waited: where in its parent's load_children it is and the order of a library's include reasons, neither of
// which is observable for a library (program_order_stdlib_first() sorts them by name; include reasons are only shown
// for emitted files)
void program_finish_deferred_libs(Program* me, char const* when, csview wanted) {
    if (!my deferred_lib_count || my finishing_libs) return;
    double start = deferred_libs_now_ms();
    SourceFile* parent = my loading_parent;
    Span site = my include_site;
    IncludeKind kind = my include_kind;
    csview package_id = my include_package_id;
    bool loading_stdlib = my loading_stdlib;
    bool defer = my defer_libs;
    u32 depth = my node_modules_depth;
    CompilerOptions const* options = compiler_options_private;
    VecError errors = my errs->errors;
    my errs->errors = (VecError){0};
    my finishing_libs = true;
    my defer_libs = false;
    compiler_options_private = nullptr;
    set_compiler_options(&my options);
    // Asked for one file (wanted), the ones already parsed first and only until it is there: their order isn't
    // observable either, and the one lib.dom.d.ts's worker is still on may not be needed for it (nest: @types/node's
    // `/// <reference lib>`s come early in the roots)
    u32 total = my deferred_lib_count, finished = 0;
    bool found = false;
    for (u32 pass = wanted.size ? 0 : 1; pass < 2 && !found; pass++) {
        u32 kept = 0;
        for (u32 i = 0; i < my deferred_lib_count; i++) {
            DeferredLib* lib = &my deferred_libs[i];
            bool now = !found && (pass || !my pool || !load_pool_in_flight(my pool, cstr_sv(&lib->path)));
            if (!now) {
                my deferred_libs[kept++] = *lib;
                continue;
            }
            program_finish_deferred_lib(me, lib);
            cstr_drop(&lib->path);
            finished++;
            found = wanted.size && program_lookup_canonical(me, wanted);
        }
        my deferred_lib_count = kept;
    }
    if (getenv("TTTT_LOAD_POOL_STATS")) {
        double end = deferred_libs_now_ms();
        eprintln("load: ", (i64)finished, " of ", (i64)total, " deferred libs finished ", when, wanted.size ? " " : "",
            wanted, " ", start - my deferred_libs_since, " ms after the first was deferred, in ", end - start, " ms");
    }
    clear_compiler_options();
    compiler_options_private = options;
    vec_error_drop(&my errs->errors);
    my errs->errors = errors;
    my finishing_libs = false;
    my defer_libs = defer;
    my loading_parent = parent;
    my include_site = site;
    my include_kind = kind;
    my include_package_id = package_id;
    my loading_stdlib = loading_stdlib;
    my node_modules_depth = depth;
}

static SourceFile* program_add_source_idempotent(Program* me, csview path) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);

    cstr storage = cstr_null;
    csview canonical_view = program_canonical_view(me, path, &storage);
    SourceFile* cached_source = program_lookup_canonical(me, canonical_view);
    // (After the libs phase, a library file the program doesn't have yet may be one the libs phase left to its worker,
    // or under one: the remembered ones are loaded first, as they would have been, program_finish_deferred_libs(). In
    // the libs phase a library met again is remembered again, so that each reference is replayed)
    if (!cached_source && my deferred_lib_count && !my finishing_libs && !my defer_libs &&
        csview_equals_sv(dirname_of(canonical_view), dirname_of(cstr_sv(&my deferred_libs[0].path)))
    ) {
        // (The view may be a memo's, which loading changes: a copy, canonical already)
        cstr wanted = cstr_from_sv(canonical_view);
        cstr_drop(&storage);
        program_finish_deferred_libs(me, "for", cstr_sv(&wanted));
        storage = wanted;
        canonical_view = cstr_sv(&storage);
        cached_source = program_lookup_canonical(me, canonical_view);
    }
    if (cached_source) {
        record_include_reason(me, cached_source);
        // A source file is already created for this path
        cstr_drop(&storage);
        SourceFile* found = cached_source;
        program_note_loaded(me, found);
        if (found->found_searching_node_modules && !my node_modules_depth) program_found_outside_node_modules(found);
        return found;
    }

    // The libs phase doesn't wait for a library a worker is still on (lib.dom.d.ts): it goes on to the roots, and the
    // library is loaded later in what loading it had now (program_finish_deferred_libs())
    if (my defer_libs && my loading_stdlib && (defer_all_libs() || (program_pool(me) && load_pool_in_flight(my pool, canonical_view)))) {
        program_defer_lib(me, canonical_view);
        cstr_drop(&storage);
        my lib_deferred = true;
        return nullptr;
    }
    cstr canonical_path = cstr_size(&storage) ? storage : cstr_from_sv(canonical_view);
    // No source file is created for this path: a load pool worker may have parsed it, else parse it here
    program_resolver(me);
    SourceFile* source_file = nullptr;
    if (program_pool(me)) source_file = load_pool_take(my pool, cstr_sv(&canonical_path), my loading_stdlib, my errs);
    if (!source_file) {
        struct timespec start, end;
        timespec_get(&start, TIME_UTC);
        // (A big declaration file's chunks go to the pool's threads too)
        if (my pool) parse_set_helpers(load_pool_helpers(my pool));
        source_file = program_make_source(me, cstr_sv(&canonical_path), my loading_stdlib, my errs);
        if (my pool) parse_set_helpers(nullptr);
        timespec_get(&end, TIME_UTC);
        double ms = (double)(end.tv_sec - start.tv_sec) * 1e3 + (double)(end.tv_nsec - start.tv_nsec) / 1e6;
        if (my pool) load_pool_note_inline(my pool, ms);
        else my loader_parse_ms += ms;
        if (!source_file) {
            cstr_drop(&canonical_path);
            return nullptr;
        }
        // (Its imports for the pool, which the loader is about to resolve anyway)
        if (my pool) program_load_ahead_from(me, my pool, source_file);
    }
    return program_add_made_source(me, canonical_path, source_file);
}

// The rest of program_add_source_idempotent() for a file just made (canonical_path is taken): it and what it refers to
// join the program, it after them
// An import or export in a module's augmentation (`declare global {}`, `declare module "m" {}`) isn't among the file's
// imports (tsc's collectModuleReferences doesn't walk an augmentation in a module): the checker finds a module for it
// only if one of the imports of the same name and mode resolved to one (getResolvedModule), else an ambient module or
// TS2307. unplugin-auto-import's `declare global { export type { Ref } from 'vue' }`, which `typeof import('vue')`
// types in the same file resolve
static void program_mark_uncollected_refs(Program const* me, SourceFile* source_file) {
    if (!source_file->is_module) {
        // A script's import of a name it declares an ambient module for (top level) isn't resolved: it can only be that
        // module (tsgo 7.1.0-beta's AmbientModuleNames, #64491: the package of the name isn't loaded)
        for (u32 i = 0; i < source_file->module_ref_count; i++) {
            ModuleRef* ref = source_file->module_refs[i];
            if (ref->is_declaration || ref->ambient_only) continue;
            for (u32 j = 0; j < source_file->module_ref_count && !ref->ambient_only; j++) {
                ModuleRef const* other = source_file->module_refs[j];
                ref->ambient_only = other->is_declaration && !other->nested_in_ambient_module && cstr_eq(&other->path, &ref->path);
            }
        }
        return;
    }
    for (u32 i = 0; i < source_file->module_ref_count; i++) {
        ModuleRef* ref = source_file->module_refs[i];
        if (!ref->in_augmentation_body || ref->is_declaration) continue;
        SpecifierMode mode = program_usage_mode(me, source_file, ref);
        bool twin = false;
        for (u32 j = 0; j < source_file->module_ref_count && !twin; j++) {
            ModuleRef const* other = source_file->module_refs[j];
            if (other->in_augmentation_body || other->is_declaration || other->ambient_only) continue;
            twin = cstr_eq(&other->path, &ref->path) && program_usage_mode(me, source_file, other) == mode;
        }
        ref->ambient_only = !twin;
    }
}

static SourceFile* program_add_made_source(Program* me, cstr canonical_path, SourceFile* source_file) {
    if (source_file->is_json) source_file->is_module = true;
    program_note_loaded(me, source_file);
    SourceFile* parent = my loading_parent;
    my loading_parent = source_file;

    hmap_box_source_file_insert(&my source_files_map, canonical_path, box_source_file_from_ptr(source_file));
    lookup_memo.buf = nullptr;
    source_file->found_searching_node_modules = my node_modules_depth > 0;
    // (Before what it refers to is loaded, which may refer back to it: tsc adds the file's include reason first)
    record_include_reason(me, source_file);
    if (span_valid(my include_site)) source_file->included_at = my include_site;
    my include_site = span_null;

    // `/// <reference path>`s first, then the rest (`types`, `lib`): tsc's processReferencedFiles comes before
    // processTypeReferenceDirectives, and the order files load in is the program's
    for_each (i, vec_statement, &source_file->ast.statements) {
        if (i->tag == STATEMENT_SOURCE_DIRECTIVE && i->source_directive->tag == SOURCE_DIRECTIVE_REFERENCE_PATH) {
            program_handle_statement(me, source_file, i);
        }
    }
    for_each (i, vec_statement, &source_file->ast.statements) {
        if (i->tag != STATEMENT_SOURCE_DIRECTIVE || i->source_directive->tag == SOURCE_DIRECTIVE_REFERENCE_PATH) continue;
        program_handle_statement(me, source_file, i);
    }
    // (Made where it was parsed, unless something else made it: a content mapper's supplemental output)
    source_scan_module_indicators(source_file);
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
        Span at = span_from_sv(source_file, csview_from_n(source_file_start(source_file), 0));
        source_file->helpers_ref = source_file_add_module_ref(source_file, cstr_lit("tslib"), at);
        // First, like tsc's: it decides the order files are in the program
        memmove(source_file->module_refs + 1, source_file->module_refs,
            sizeof(ModuleRef*) * (source_file->module_ref_count - 1));
        source_file->module_refs[0] = source_file->helpers_ref;
        // Resolved like the import the emitter writes: a `require()` in CommonJS output
        if (ty_emits_commonjs_like(source_file)) source_file->helpers_ref->syntax = MODULE_REF_REQUIRE;
    }
    // (Only a .tsx or JavaScript file's: tsgo's fileloader, not tsc 6's every module)
    cstr jsx_runtime = my loading_stdlib || !source_file->is_jsx ? cstr_null : source_file_jsx_runtime_import(source_file);
    if (cstr_size(&jsx_runtime)) {
        Span at = span_from_sv(source_file, csview_from_n(source_file_start(source_file), 0));
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

    program_mark_uncollected_refs(me, source_file);
    // Every module the file mentions, wherever (the parser lists them), the declarations' first (tsc's order of
    // file.imports, which decides the order of the program's files). By index: loading parses, never this file
    // (The refs are a worker's, not in this core's cache: the first pass fetches them ahead)
    for (u32 i = 0; i < source_file->module_ref_count; i++) __builtin_prefetch(source_file->module_refs[i]);
    for (u32 n = 0; n < 2 * source_file->module_ref_count; n++) {
        u32 i = n % source_file->module_ref_count;
        ModuleRef* ref = source_file->module_refs[i];
        if (module_ref_collected_late(ref) != (n >= source_file->module_ref_count)) continue;
        // `declare module "./a" {}` in a module augments a file. In a script it declares an ambient module, and so
        // does a name that finds no file
        if (ref->is_declaration && !source_file->is_module && !ref->nested_in_ambient_module) continue;
        if (ref->ambient_only) continue;
        if (my options.no_resolve) continue; // program_resolve_names(), once every root is loaded
        program_load_module_ref(me, source_file, ref);
    }
    // A content mapper's supplemental outputs, after what the file imports (tsgo's parse subtasks)
    ContentMapped const* mapped = source_file->content_mapped;
    for (u32 i = 0; mapped && i < mapped->supplemental_count; i++) {
        SourceFile* supplemental = mapped->supplementals[i];
        if (hmap_box_source_file_get(&my source_files_map, cstr_sv(&supplemental->canonical_path))) continue;
        program_add_made_source(me, cstr_clone(supplemental->canonical_path), supplemental);
    }

    // (A worker's blocks: freed once loading is over, not here, where it would wait on the worker's malloc arena)
    if (source_file->preresolved) {
        if (my preresolved_garbage_count == my preresolved_garbage_cap) {
            my preresolved_garbage_cap = my preresolved_garbage_cap ? my preresolved_garbage_cap * 2 : 256;
            my preresolved_garbage = realloc(my preresolved_garbage, sizeof(Preresolved*) * my preresolved_garbage_cap);
            panic_if(!my preresolved_garbage, "out of memory");
        }
        my preresolved_garbage[my preresolved_garbage_count++] = source_file->preresolved;
    }
    source_file->preresolved = nullptr;

    // Add here so imports come before importing file
    source_file->program_order = my order_base + (u32)vec_source_file_ptr_size(&my source_files_ordered);
    vec_source_file_ptr_push(&my source_files_ordered, source_file);
    my loading_parent = parent;

    return source_file;
}

// tsc's chain under a diagnostic about a root file: "The file is in the program because:" and how the config (or the
// command line) has it
// The config file as messages name it (tsc's toFileName(config.ConfigName()): absolute on the command line)
static cstr program_config_name_for_message(Program* me) {
    if (!my config_source) return cstr_null;
    cstr absolute = absolute_path_of(program_cwd(me), cstr_sv(&my config_source->canonical_path));
    cstr ret = program_path_for_message(me, cstr_sv(&absolute));
    cstr_drop(&absolute);
    return ret;
}

static void program_explain_root(Program* me, Error* error, csview path) {
    cstr absolute = absolute_path_of(program_cwd(me), path);
    cstr config_name = program_config_name_for_message(me);
    csview config = cstr_sv(&config_name);
    cstr reason = tsconfig_root_file_reason(&my options, cstr_sv(&absolute), config);
    cstr_drop(&config_name);
    error_sub_error_d(error, span_null, TS_CODE_NONE, "The file is in the program because:");
    error_sub_error_d(vec_error_back_mut(&error->sub_errors), span_null, TS_CODE_NONE, reason);
    c_drop(cstr, &reason, &absolute);
}

failable program_load(Program* me, csview path, returns SourceFile*) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    // A referenced project's source is its output (tsc's getSourceFileFromReferenceWorker)
    csview output = program_output_of_source(me, path);
    if (output.size && !fs_is_reg_file(my fs, output)) {
        // An unbuilt one isn't in the program (tsgo's root file redirected to an output that isn't there: nothing is
        // reported for it, an import of it is TS6305 at the import)
        if (!fs_is_reg_file(my fs, path)) return err_d("File '", path, "' not found.");
        return ok(nullptr);
    }
    if (output.size) path = output;

    // tsc's getSourceFileFromReferenceWorker for a root: an extension that isn't a script's (a `.map`, a `.txt`)
    static const char* const scripts[] = { ".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs", ".json" };
    bool script = false;
    for (isize i = 0; i < c_countof(scripts) && !script; i++) script = csview_ends_with(path, scripts[i]);
    script = script || (my options.content_mapper_count && content_mapper_for_path(&my options, path));
    if (!path_has_extension(path)) {
        // The first group of supported extensions completes it (getSourceFileFromReferenceWorker)
        static const char* const completions[] = { ".ts", ".tsx", ".d.ts" };
        for (isize i = 0; i < c_countof(completions); i++) {
            cstr candidate = cstr_d(path, completions[i]);
            if (fs_is_reg_file(my fs, cstr_sv(&candidate))) {
                set_compiler_options(&my options);
                if (!my loading_stdlib) my include_kind = INCLUDE_ROOT;
                SourceFile* found = program_add_source_idempotent(me, cstr_sv(&candidate));
                my include_kind = INCLUDE_NONE;
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
        program_explain_root(me, error, path);
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
        program_explain_root(me, error, path);
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
        program_explain_root(me, error, path);
        cstr_drop(&shown);
        return ok(nullptr);
    }

    set_compiler_options(&my options);

    // Add the source file to the program
    if (!my loading_stdlib) my include_kind = INCLUDE_ROOT;
    my lib_deferred = false;
    SourceFile* main_source = program_add_source_idempotent(me, path);
    my include_kind = INCLUDE_NONE;
    if (main_source && !my loading_stdlib) main_source->is_root = true;

    clear_compiler_options();
    // (A library left to its worker, program_finish_deferred_libs())
    if (!main_source && my lib_deferred) {
        my lib_deferred = false;
        return ok(nullptr);
    }

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
    cstr storage = cstr_null;
    SourceFile* found = program_lookup_canonical(me, program_canonical_view(me, path, &storage));
    cstr_drop(&storage);
    // (A redirect as itself: what refers to it says what it stands for)
    if (found && found->excluded && !found->package_redirect) return nullptr;
    return found;
}

failable program_load_stdlib(Program* me, csview name, returns SourceFile*) {
    cstr replacement = program_lib_replacement(me, name);
    if (cstr_size(&replacement)) {
        // (libReplacement: the package's file, not a lib file)
        try (SourceFile* source, program_load(me, cstr_sv(&replacement), &source)) cleanup {
            cstr_drop(&replacement);
        }
        cstr_drop(&replacement);
        return ok(source);
    }
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

void program_load_ahead_libs(Program* me, csview const* libs, isize lib_count) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    LoadPool* pool = program_pool(me);
    if (!pool) return;
    QueueBatch batch = {0};
    for (isize i = 0; i < lib_count; i++) {
        cstr replacement = program_lib_replacement(me, libs[i]);
        cstr path = cstr_size(&replacement) ? cstr_clone(replacement) :
            cstr_d(cstr_size(&my lib_dir) ? cstr_sv(&my lib_dir) : default_lib_dir, "/", libs[i], ".d.ts");
        program_queue_path(me, &batch, cstr_sv(&path), !cstr_size(&replacement));
        c_drop(cstr, &path, &replacement);
    }
    program_queue_batch(pool, &batch);
}

void program_load_ahead_roots(Program* me, csview const* roots, isize root_count) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    LoadPool* pool = program_pool(me);
    if (!pool || !root_count) return;
    // (Under one lock)
    csview* canonical = c_new_n(csview, root_count + 1);
    cstr* storage = c_new_n(cstr, root_count + 1);
    for (isize i = 0; i < root_count; i++) storage[i] = cstr_null;
    for (isize i = 0; i < root_count; i++) canonical[i] = program_canonical_view(me, roots[i], &storage[i]);
    load_pool_queue_many(pool, canonical, root_count, false);
    for (isize i = 0; i < root_count; i++) cstr_drop(&storage[i]);
    c_free_n(storage, root_count + 1);
    c_free_n(canonical, root_count + 1);
}

void program_load_ahead(Program* me, csview const* libs, isize lib_count, csview const* roots, isize root_count) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    LoadPool* pool = program_pool(me);
    if (!pool) return;
    program_load_ahead_libs(me, libs, lib_count);
    // (The workers are already taking the libs)
    program_load_ahead_roots(me, roots, root_count);
    QueueBatch batch = {0};
    // The automatic types (program_load_automatic_types() resolves them again)
    StrList names = module_resolver_automatic_types(my resolver);
    for (isize i = 0; i < names.count; i++) {
        ResolvedModule resolved = module_resolver_resolve_type_reference(my resolver, cstr_sv(&names.items[i]), c_sv(""), nullptr);
        if (resolved.kind != RESOLVED_NONE) program_queue_path(me, &batch, cstr_sv(&resolved.path), false);
        resolved_module_drop(&resolved);
    }
    str_list_drop(&names);
    program_queue_batch(pool, &batch);
}

// program_collect_files()'s walk: tsgo's collectFiles, depth first from the roots in the order the loader reached
// files. The first file with a package id stands for it, a later one with the same id is its redirect and the walk
// doesn't follow what the redirect refers to
static void collect_walk(Program* me, SourceFile* file, HMapSourceFilePtr* by_package_id, VecSourceFilePtr* out) {
    if (file->collected) return;
    file->collected = true;
    if (cstr_size(&file->package_id)) {
        auto found = hmap_source_file_ptr_insert(by_package_id, cstr_clone(file->package_id), file);
        if (!found.inserted) {
            file->package_redirect = true;
            file->package_redirect_target = found.ref->second;
            file->unchecked = true;
            return;
        }
    }
    for (u32 i = 0; i < file->load_child_count; i++) collect_walk(me, file->load_children[i], by_package_id, out);
    vec_source_file_ptr_push(out, file);
}

// Why a file is in the program, from a file that isn't (a redirect, what only one reaches) doesn't count: tsgo's
// collectFiles adds the include reasons of what it walks
static bool from_excluded(Span at) {
    return span_valid(at) && span_source(at)->excluded;
}

// The program's files, once everything reachable is loaded: when two files have the same package id (a package
// installed twice, a workspace package seen through a symlinked directory and at its real path), the first in tsgo's
// order stands for both, the other becomes a redirect (SourceFile.package_redirect) that isn't one of the program's
// files, and what only the redirect refers to isn't either (SourceFile.excluded). Imports of a redirect mean the first
static void program_collect_files(Program* me) {
    if (my collected) return;
    my collected = true;
    if (my duplicate_packages) {
        HMapSourceFilePtr by_package_id = {0};
        VecSourceFilePtr files = {0};
        for_each (i, vec_source_file_ptr, &my load_roots) collect_walk(me, *i, &by_package_id, &files);
        hmap_source_file_ptr_drop(&by_package_id);
        for_each (i, vec_source_file_ptr, &my source_files_ordered) (*i)->excluded = !(*i)->collected || (*i)->package_redirect;
        check(vec_source_file_ptr_size(&files) <= vec_source_file_ptr_size(&my source_files_ordered));
        vec_source_file_ptr_drop(&my source_files_ordered);
        my source_files_ordered = files;
        for (u32 k = 0; k < vec_source_file_ptr_size(&my source_files_ordered); k++) {
            SourceFile* file = *vec_source_file_ptr_at(&my source_files_ordered, k);
            file->program_order = my order_base + k;
            for (u32 j = 0; j < file->module_ref_count; j++) {
                ModuleRef* ref = file->module_refs[j];
                if (!ref->file || !ref->file->excluded) continue;
                if (ref->file->package_redirect) {
                    module_ref_rare_mut(ref)->redirected_from = ref->file;
                    ref->file = ref->file->package_redirect_target;
                } else {
                    ref->file = nullptr; // (An augmentation of what isn't in the program)
                }
            }
            u32 kept = 0;
            for (u32 j = 0; j < file->include_reason_count; j++) {
                if (from_excluded(file->include_reasons[j].at)) cstr_drop(&file->include_reasons[j].package_id);
                else file->include_reasons[kept++] = file->include_reasons[j];
            }
            file->include_reason_count = kept;
            if (from_excluded(file->included_at)) {
                file->included_at = span_null;
                for (u32 j = 0; j < kept && !span_valid(file->included_at); j++) file->included_at = file->include_reasons[j].at;
            }
        }
    }
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        free((*i)->load_children);
        (*i)->load_children = nullptr;
        (*i)->load_child_count = (*i)->load_child_cap = 0;
    }
    vec_source_file_ptr_drop(&my load_roots);
    my load_roots = (VecSourceFilePtr){0};
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
    program_collect_files(me);
    // (The parse errors of referenced projects' files and of what isn't in the program, and the parser's grammar errors
    // in unchecked files (tsc's checker's, a skipLibCheck declaration file's), before anyone reports them)
    error_ctx_remove_unchecked(my errs);
}

void program_copy_globals_from(Program* me, Program const* other) {
    binder_set_parent(my binder, other->binder);
    my order_base = other->order_base + (u32)vec_source_file_ptr_size(&other->source_files_ordered);
    check(!vec_source_file_ptr_size(&my source_files_ordered));
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
    JsonValue const* written = options && options->value.tag == JSON_OBJECT ? json_get(&options->value, "paths") : nullptr;
    if (!written || written->tag != JSON_OBJECT) {
        // Not in the config's own compilerOptions (an extended config's, or the command line's): on its
        // `compilerOptions` key, else without a location
        Span site = options ? config_span(me, options->key_start, options->key_end) : span_null;
        for (isize i = 0; i < my options.path_count; i++) {
            PathMapping const* mapping = &my options.paths[i];
            if (count_asterisks(cstr_sv(&mapping->pattern)) > 1) {
                option_error_d_one(me, site, 5061, "Pattern '", mapping->pattern, "' can have at most one '*' character.");
            }
            if (!mapping->substitutions.count) {
                option_error_d_one(me, site, 5066, "Substitutions for pattern '", mapping->pattern,
                    "' shouldn't be an empty array.");
            }
            for (isize j = 0; j < mapping->substitutions.count; j++) {
                cstr const* substitution = &mapping->substitutions.items[j];
                if (count_asterisks(cstr_sv(substitution)) > 1) {
                    option_error_d_one(me, site, 5062, "Substitution '", *substitution, "' in pattern '", mapping->pattern,
                        "' can have at most one '*' character.");
                }
                if (!path_is_relative_or_absolute(cstr_sv(substitution))) {
                    option_error_d_one(me, site, 5090, "Non-relative paths are not allowed. Did you forget a leading './'?");
                } else if (path_is_network(cstr_sv(substitution))) {
                    option_error_d_one(me, site, 5116, "Network path '", *substitution, "' is not supported.");
                }
            }
        }
        return;
    }
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
                    csview written = span_sv(at);
                    error_ctx_error_d(my errs, at, 5064, "Substitution '", written, "' for pattern '", pattern->key,
                        "' has incorrect type, expected 'string', got '", json_typeof(substitution), "'.");
                    continue;
                }
                if (count_asterisks(cstr_sv(&substitution->string)) > 1) {
                    error_ctx_error_d(my errs, at, 5062, "Substitution '", substitution->string, "' in pattern '",
                        pattern->key, "' can have at most one '*' character.");
                }
                if (!path_is_relative_or_absolute(cstr_sv(&substitution->string))) {
                    error_ctx_error_d(my errs, at, 5090, "Non-relative paths are not allowed. Did you forget a leading './'?");
                } else if (path_is_network(cstr_sv(&substitution->string))) {
                    error_ctx_error_d(my errs, at, 5116, "Network path '", substitution->string, "' is not supported.");
                }
            }
        }
    }
}

// tsc's verifyCompilerOptions(), the part that doesn't depend on emit. With a tsconfig the diagnostics are on the
// option there (option_sites())
static void program_validate_options(Program* me) {
    CompilerOptions const* o = &my options;
    // Removed in TypeScript 7: an option (TS5102, on its name) or a value (TS5108, on the value), which stop checking
    if (o->base_url_set) {
        Span sites[4];
        u32 site_count = option_sites(me, "baseUrl", nullptr, true, sites);
        // (With a tsconfig: what `paths` says the same with, relative to the config's file)
        cstr instead = cstr_null;
        if (my config_source) {
            csview cwd = program_cwd(me);
            cstr config_dir = absolute_path_of(cwd, cstr_sv(&o->config_dir));
            cstr base = module_resolver_option_path(program_resolver(me), cstr_sv(&o->base_url));
            cstr relative = relative_path_of(cstr_sv(&config_dir), cstr_sv(&base));
            if (cstr_equals(&relative, ".")) cstr_clear(&relative);
            while (cstr_ends_with(&relative, "/")) cstr_resize(&relative, cstr_size(&relative) - 1, '\0');
            if (!cstr_starts_with(&relative, "./") && !cstr_starts_with(&relative, "../")) cstr_insert(&relative, 0, "./");
            if (!cstr_ends_with(&relative, "/")) cstr_append(&relative, "/");
            instead = cstr_lit("\"paths\": {\"*\": [\"");
            for (isize i = 0; i < cstr_size(&relative); i++) {
                char c = cstr_str(&relative)[i];
                if (c == '"' || c == '\\') cstr_append_n(&instead, "\\", 1);
                cstr_append_n(&instead, &c, 1);
            }
            cstr_append(&instead, "*\"]}");
            c_drop(cstr, &config_dir, &base, &relative);
        }
        for (u32 j = 0; j < site_count; j++) {
            Error* error = option_error_d_one(me, sites[j], 5102,
                "Option 'baseUrl' has been removed. Please remove it from your configuration.");
            if (cstr_size(&instead)) error_sub_error_d(error, span_null, TS_CODE_NONE, "Use '", instead, "' instead.");
        }
        cstr_drop(&instead);
    }
    {
        // (In tsc's order)
        struct { char const* name; char const* value; bool given; } const removed[] = {
            { "outFile", nullptr, cstr_size(&o->out_file) > 0 },
            { "target", "ES5", o->target_set && o->target == SCRIPT_TARGET_ES5 },
            { "module", "AMD", o->module_set && o->module == MODULE_KIND_AMD },
            { "module", "System", o->module_set && o->module == MODULE_KIND_SYSTEM },
            { "module", "UMD", o->module_set && o->module == MODULE_KIND_UMD },
            { "moduleResolution", "Classic", o->module_resolution_removed == MODULE_RESOLUTION_CLASSIC },
            { "alwaysStrict", "false", o->is_set[COMPILER_OPTION_always_strict] && !o->always_strict },
            { "esModuleInterop", "false", o->is_set[COMPILER_OPTION_es_module_interop] && !o->es_module_interop },
            { "allowSyntheticDefaultImports", "false",
                o->is_set[COMPILER_OPTION_allow_synthetic_default_imports] && !o->allow_synthetic_default_imports },
            { "moduleResolution", "node10", o->module_resolution_removed == MODULE_RESOLUTION_NODE10 },
            { "downlevelIteration", nullptr, o->is_set[COMPILER_OPTION_downlevel_iteration] },
        };
        for (u32 i = 0; i < c_arraylen(removed); i++) {
            if (!removed[i].given) continue;
            if (removed[i].value) {
                option_error_d(me, removed[i].name, nullptr, false, 5108, "Option '", removed[i].name, "=", removed[i].value,
                    "' has been removed. Please remove it from your configuration.");
            } else {
                option_error_d(me, removed[i].name, nullptr, true, 5102, "Option '", removed[i].name,
                    "' has been removed. Please remove it from your configuration.");
            }
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
    if (!cstr_size(&o->ts_build_info_file) && o->incremental && !my config_source) {
        error_ctx_global_error_d(my errs, 5074, "Option '--incremental' is only valid with a known configuration file ",
            "(like 'tsconfig.json') or when '--tsBuildInfoFile' is explicitly provided.");
    }
    program_validate_paths(me);
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
    if (o->check_js && !o->allow_js) {
        option_error_d(me, "checkJs", "allowJs", true, 5052,
            "Option 'checkJs' cannot be specified without specifying option 'allowJs'.");
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
    if (source->is_declaration || source->is_stdlib) return false;
    if (source->found_searching_node_modules) return false;
    if (!source->is_json) return true;
    if (!cstr_size(&my options.out_dir)) return false;
    // (A referenced project's `.json` source is read as itself: still its project's, tsgo's GetProjectReferenceFromSource)
    if (project_redirects_count(my redirects)) {
        cstr absolute = absolute_path_of(program_cwd(me), cstr_sv(&source->canonical_path));
        bool referenced = project_redirects_project_of(my redirects, cstr_sv(&absolute));
        cstr_drop(&absolute);
        if (referenced) return false;
    }
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

// Reports one output path (tsc's verifyEmitFilePath): TS5055 if it is an input file, TS5056 the second time. Either
// blocks writing there
static void emit_verify_path(Program* me, cstr output, hmap_source_file_ptr* seen) {
    bool blocked = false;
    if (program_find_source(me, cstr_sv(&output))) {
        blocked = true;
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
        blocked = true;
    } else {
        hmap_source_file_ptr_insert(seen, cstr_clone(key), nullptr);
    }
    if (blocked && !hmap_source_file_ptr_contains(&my emit_blocked, cstr_sv(&key))) {
        hmap_source_file_ptr_insert(&my emit_blocked, key, nullptr);
    } else {
        cstr_drop(&key);
    }
    cstr_drop(&output);
}

bool program_emit_blocked(Program* me, csview output) {
    if (!hmap_source_file_ptr_size(&my emit_blocked)) return false;
    cstr key = relative_path_of(program_cwd(me), output);
    bool ret = hmap_source_file_ptr_contains(&my emit_blocked, cstr_sv(&key));
    cstr_drop(&key);
    return ret;
}

bool program_source_may_be_emitted(Program const* me, SourceFile const* source) {
    return emit_source_may_be_emitted(me, source);
}

// The end of tsc's verifyCompilerOptions: no output file may overwrite an input file or another output file
// The root of a file's name as tsc has it: `c:/` of a DOS drive path, else `/` (a relative name is in the current
// directory)
static csview path_root_of(csview path) {
    if (!path_is_drive_rooted(path)) return c_sv("/");
    return csview_slice(path, 0, path.size > 2 ? 3 : 2);
}

// tsc's computeCommonSourceDirectoryOfFilenames on the files that may be emitted (getComputedCommonSourceDirectory),
// base_dir without any
static cstr computed_common_dir(Program* me, csview cwd, csview base_dir) {
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
    // (Nothing emitted, all declaration files or no files at all: tsc's computeCommonSourceDirectoryOfFilenames
    // says the program's base directory, a config's directory or the current one)
    if (first) cstr_assign_sv(&common_dir, base_dir);
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
    cstr config_dir = absolute_path_of(cwd, cstr_sv(&o->config_dir));
    cstr dir59 = computed_common_dir(me, cwd, cstr_sv(&config_dir));
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
        common_dir = computed_common_dir(me, cstr_sv(&cwd), cstr_sv(&cwd));
    }
    cstr_drop(&cwd);
    return common_dir;
}

// tsgo's TS2878 test: target is a referenced project's source (or output) whose root and output directories aren't
// placed like this program's (a rewritten `.ts` import path would point elsewhere in the outputs)
static bool program_rewrite_crosses_projects(Program* me, SourceFile const* target) {
    // (tsgo's GetRedirectForResolution: a referenced project's output)
    u32 project = target->resolution_project;
    if (!project || !my redirects) return false;
    CompilerOptions const* other = project_redirects_options(my redirects, project - 1);
    csview cwd = program_cwd(me);
    cstr own_root = program_common_source_dir(me);
    cstr other_root = absolute_path_of(cwd, cstr_sv(cstr_size(&other->root_dir) ? &other->root_dir : &other->config_dir));
    cstr own_out = cstr_size(&my options.out_dir) ? absolute_path_of(cwd, cstr_sv(&my options.out_dir)) : cstr_clone(own_root);
    cstr other_out = cstr_size(&other->out_dir) ? absolute_path_of(cwd, cstr_sv(&other->out_dir)) : cstr_clone(other_root);
    // (tsgo's RelativePathFromDirectory: `..` included)
    cstr roots = relative_path_of(cstr_sv(&own_root), cstr_sv(&other_root));
    cstr outs = relative_path_of(cstr_sv(&own_out), cstr_sv(&other_out));
    bool ret = !cstr_eq(&roots, &outs);
    c_drop(cstr, &own_root, &other_root, &own_out, &other_out, &roots, &outs);
    return ret;
}

// tsc's stringutil.EncodeURI: what encodeURI escapes
static cstr encode_uri(csview text) {
    cstr out = cstr_init();
    for (isize i = 0; i < text.size; i++) {
        unsigned char c = (unsigned char)text.buf[i];
        if (isalnum(c) || strchr(";,/?:@&=+$-_.!~*'()#", c)) {
            cstr_append_n(&out, (char const*)&c, 1);
        } else {
            char escaped[4];
            snprintf(escaped, sizeof escaped, "%%%02X", c);
            cstr_append(&out, escaped);
        }
    }
    return out;
}

// A file's declarationMap paths (SourceFile.declaration_map_source, declaration_map_url): tsc's emitter
// getSourceMapDirectory (the source is named relative to the sourceRoot's common source directory, the mapRoot's
// directory for it, else the `.d.ts`'s) and getSourceMappingURL
static void set_declaration_map_paths(Program* me, SourceFile* source) {
    CompilerOptions const* o = &my options;
    csview cwd = program_cwd(me);
    csview dts = cstr_sv(&source->declaration_output_path);
    csview dts_dir = dirname_of(dts);
    cstr absolute = absolute_path_of(cwd, cstr_sv(&source->canonical_path));
    bool roots = cstr_size(&o->source_root) || cstr_size(&o->map_root);
    cstr common_dir = roots ? program_common_source_dir(me) : cstr_null;
    // (getMapRootDirectory: the mapRoot, relative to the common source directory, with the file's directory under it)
    cstr map_dir = cstr_null;
    if (cstr_size(&o->map_root)) {
        cstr root = absolute_path_of(cstr_str(&o->map_root)[0] == '/' ? cwd : cstr_sv(&common_dir), cstr_sv(&o->map_root));
        cstr relative = relative_path_of(cstr_sv(&common_dir), cstr_sv(&absolute));
        cstr in_root = cstr_starts_with(&relative, "..") ? cstr_clone(absolute) : absolute_path_of(cstr_sv(&root), cstr_sv(&relative));
        map_dir = cstr_from_sv(dirname_of(cstr_sv(&in_root)));
        c_drop(cstr, &root, &relative, &in_root);
    }
    csview sources_dir = cstr_size(&o->source_root) ? cstr_sv(&common_dir) : cstr_size(&o->map_root) ? cstr_sv(&map_dir) : dts_dir;
    cstr_take(&source->declaration_map_source, relative_path_of(sources_dir, cstr_sv(&absolute)));
    cstr map_name = cstr_from_sv(basename_of(dts));
    cstr_append(&map_name, ".map");
    if (cstr_size(&o->map_root)) {
        cstr map_path = absolute_path_of(cstr_sv(&map_dir), cstr_sv(&map_name));
        bool relative_root = cstr_str(&o->map_root)[0] != '/';
        cstr url = relative_root ? relative_path_of(dts_dir, cstr_sv(&map_path)) : cstr_clone(map_path);
        cstr_take(&source->declaration_map_url, encode_uri(cstr_sv(&url)));
        c_drop(cstr, &map_path, &url);
    } else {
        cstr_take(&source->declaration_map_url, encode_uri(cstr_sv(&map_name)));
    }
    c_drop(cstr, &absolute, &common_dir, &map_dir, &map_name);
}

// Each file's declaration output path (SourceFile.declaration_output_path, absolute): tsc's GetOutputPathsFor with the
// declaration path forced, so a file that isn't emitted has one too (a `/// <reference path>` to it is rewritten to it)
// and a declaration file is its own
static void program_set_declaration_paths(Program* me) {
    CompilerOptions const* o = &my options;
    csview cwd = program_cwd(me);
    csview dir = cstr_size(&o->declaration_dir) ? cstr_sv(&o->declaration_dir) : cstr_sv(&o->out_dir);
    cstr common_dir = dir.size ? program_common_source_dir(me) : cstr_null;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        if (source->is_stdlib || source->is_json || source->is_config) continue;
        csview path = source_file_display_path(source);
        // (A declaration file is its own: what a reference to it names)
        if (source->is_declaration) {
            cstr_take(&source->declaration_output_path, absolute_path_of(cwd, path));
            continue;
        }
        cstr output = emit_output_path(me, path, dir, cstr_sv(&common_dir), emit_declaration_extension(path));
        cstr_take(&source->declaration_output_path, absolute_path_of(cwd, cstr_sv(&output)));
        cstr_drop(&output);
        if (o->declaration_map) set_declaration_map_paths(me, source);
    }
    cstr_drop(&common_dir);
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

// One of tsc's file include reasons as its chain says it (FileIncludeReason.toDiagnostic, file names absolute)
static cstr include_reason_text(Program* me, SourceFile const* file, IncludeReason const* reason) {
    switch (reason->kind) {
        case INCLUDE_ROOT: {
            cstr absolute = absolute_path_of(program_cwd(me), cstr_sv(&file->canonical_path));
            cstr config = program_config_name_for_message(me);
            cstr ret = tsconfig_root_file_reason(&my options, cstr_sv(&absolute), cstr_sv(&config));
            c_drop(cstr, &absolute, &config);
            return ret;
        }
        case INCLUDE_IMPORT:
        case INCLUDE_REFERENCE: {
            SourceFile const* from = span_source(reason->at);
            cstr absolute = from ? absolute_path_of(program_cwd(me), cstr_sv(&from->canonical_path)) : cstr_null;
            cstr from_name = program_path_for_message(me, cstr_sv(&absolute));
            cstr ret = reason->kind == INCLUDE_REFERENCE ?
                cstr_d("Referenced via '", reason->at, "' from file '", from_name, "'") :
                cstr_size(&reason->package_id) ?
                cstr_d("Imported via ", reason->at, " from file '", from_name, "' with packageId '", reason->package_id, "'") :
                cstr_d("Imported via ", reason->at, " from file '", from_name, "'");
            c_drop(cstr, &absolute, &from_name);
            return ret;
        }
        case INCLUDE_NONE: break;
    }
    return cstr_null;
}

// The string element of the config's own (not an extended config's) `files` or `include` array (tsgo's
// GetTsConfigPropArrayElementValue): the index-th string of `files`, the first `include` spec equal to spec
static JsonValue const* config_spec_element(Program const* me, char const* key, isize index, csview spec) {
    if (!my config_source || my config_json.tag != JSON_OBJECT) return nullptr;
    JsonValue const* array = json_get_tagged(&my config_json, key, JSON_ARRAY);
    for (isize i = 0, strings = 0; array && i < array->count; i++) {
        JsonValue const* item = &array->items[i];
        if (item->tag != JSON_STRING) continue;
        if (spec.buf ? csview_equals_sv(cstr_sv(&item->string), spec) : strings == index) return item;
        strings++;
    }
    return nullptr;
}

// tsgo's FileIncludeReason.toRelatedInfo: where a reason that isn't the diagnostic's location is (an import or
// reference; for a root file the config's `files` element or non-default `include` pattern naming it)
static void add_include_related_info(Program* me, Error* error, SourceFile const* file, IncludeReason const* reason) {
    switch (reason->kind) {
        case INCLUDE_IMPORT:
            if (span_valid(reason->at)) error_add_related_d(error, reason->at, 1399, "File is included via import here.");
            return;
        case INCLUDE_REFERENCE:
            if (span_valid(reason->at)) error_add_related_d(error, reason->at, 1401, "File is included via reference here.");
            return;
        case INCLUDE_ROOT: {
            cstr absolute = absolute_path_of(program_cwd(me), cstr_sv(&file->canonical_path));
            isize index = 0;
            ConfigSpecMatch match = tsconfig_match_root_file(&my options, cstr_sv(&absolute), &index);
            cstr_drop(&absolute);
            JsonValue const* at = match == CONFIG_SPEC_FILES ? config_spec_element(me, "files", index, (csview){0}) :
                match == CONFIG_SPEC_INCLUDE ?
                config_spec_element(me, "include", 0, cstr_sv(&my options.config_include_specs.items[index])) : nullptr;
            if (!at || at->end > cstr_size(&my config_source->contents)) return;
            if (match == CONFIG_SPEC_FILES) {
                error_add_related_d(error, config_span(me, at->start, at->end), 1410, "File is matched by 'files' list specified here.");
            } else {
                error_add_related_d(error, config_span(me, at->start, at->end), 1408, "File is matched by include pattern specified here.");
            }
            return;
        }
        case INCLUDE_NONE: return;
    }
}

// tsc's explainRedirectAndImpliedFormat: why a module file has the format it has (GetImpliedNodeFormatForEmit)
static void explain_implied_format(Error* error, SourceFile const* file) {
    if (!file->is_module) return;
    ModuleKind format = source_file_implied_format_for_emit(file);
    csview package = cstr_sv(&file->package_json);
    if (format == MODULE_KIND_ESNEXT && file->package_has_type && package.size) {
        error_sub_error_d(error, span_null, TS_CODE_NONE, "File is ECMAScript module because '", package,
            "' has field \"type\" with value \"module\"");
    } else if (format == MODULE_KIND_COMMONJS) {
        if (file->package_has_type && package.size) {
            error_sub_error_d(error, span_null, TS_CODE_NONE, "File is CommonJS module because '", package,
                "' has field \"type\" whose value is not \"module\"");
        } else if (package.size) {
            error_sub_error_d(error, span_null, TS_CODE_NONE, "File is CommonJS module because '", package,
                "' does not have field \"type\"");
        } else {
            error_sub_error_d(error, span_null, TS_CODE_NONE, "File is CommonJS module because 'package.json' was not found");
        }
    }
}

// tsc's createDiagnosticExplainingFile: at the first import or reference that brought the file in (else a global
// diagnostic), with "The file is in the program because:" and every reason unless that import is the only one, then
// what decides its module format
static void program_error_explaining_file(Program* me, SourceFile const* file, unsigned code, cstr message) {
    IncludeReason const* preferred = nullptr;
    for (u32 i = 0; i < file->include_reason_count && !preferred; i++) {
        if (file->include_reasons[i].kind != INCLUDE_ROOT) preferred = &file->include_reasons[i];
    }
    Error* error;
    if (preferred) {
        // (Not in a file that isn't checked, a skipLibCheck declaration file: tsc's getProgramDiagnostics skips them)
        SourceFile const* at = span_source(preferred->at);
        if (at && at->unchecked && !at->plain_js) {
            cstr_drop(&message);
            return;
        }
        error = error_ctx_error_s(my errs, preferred->at, code, message);
        // (A semantic diagnostic of that file: a @ts-ignore/@ts-expect-error before it applies, tsgo's
        // GetIncludeProcessorDiagnostics through getDiagnosticsWithPrecedingDirectives. Reported in plain JavaScript
        // whatever its code)
        if (error) {
            error->phase = ERROR_PHASE_RESOLVE_NAMES;
            error->include_processor = true;
        }
    } else {
        error = error_ctx_global_error_s(my errs, code, message);
    }
    if (!error) return;
    if (file->include_reason_count && (!preferred || file->include_reason_count != 1)) {
        error_sub_error_d(error, span_null, TS_CODE_NONE, "The file is in the program because:");
        Error* chain = vec_error_back_mut(&error->sub_errors);
        for (u32 i = 0; i < file->include_reason_count; i++) {
            cstr text = include_reason_text(me, file, &file->include_reasons[i]);
            if (cstr_size(&text)) error_sub_error_s(chain, span_null, TS_CODE_NONE, text);
            else cstr_drop(&text);
        }
    }
    // (The format's line is part of the chain: before the related information, which shares the sub-error vector)
    CompilerOptions const* saved = compiler_options_private;
    set_compiler_options(program_options_of(me, file));
    explain_implied_format(error, file);
    compiler_options_private = saved;
    for (u32 i = 0; i < file->include_reason_count; i++) {
        if (&file->include_reasons[i] != preferred) add_include_related_info(me, error, file, &file->include_reasons[i]);
    }
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
        // (vscode: absolute_path_of() per file was most of this loop's 41 M instructions, serial before the declare)
        csview canonical = cstr_sv(&file->canonical_path);
        cstr absolute = path_is_drive_rooted(cwd) || path_is_drive_rooted(canonical) ? absolute_path_of(cwd, canonical) :
            absolute_path_fast(cwd, canonical);
        // (tsgo's ContainsFilePath: the directory itself counts, `rootDir` naming the file; a case-insensitive host
        // compares Unicode lower cases, ToFileNameLowerCase)
        bool fold = o->case_insensitive_file_names;
        cstr folded_path = fold ? cstr_tolower_sv(cstr_sv(&absolute)) : cstr_null;
        cstr folded_root = fold ? cstr_tolower_sv(cstr_sv(&root_dir)) : cstr_null;
        csview path = cstr_sv(fold ? &folded_path : &absolute);
        csview root = cstr_sv(fold ? &folded_root : &root_dir);
        bool under = path.size >= root.size && !memcmp(path.buf, root.buf, (size_t)root.size) &&
            (path.size == root.size || path.buf[root.size] == '/' || (root.size && root.buf[root.size - 1] == '/'));
        c_drop(cstr, &folded_path, &folded_root);
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
        SourceFile* source = (SourceFile*)span_source(error->span);
        bool suppressed = false;
        // (And the grammar errors the parser finds for tsc's checker, checkGrammar*: semantic diagnostics there)
        bool applies = error->phase == phase ||
            (phase == ERROR_PHASE_RESOLVE_NAMES && error->phase == ERROR_PHASE_PARSE && error->grammar);
        if (applies && source && source->comment_directive_count && !source->unchecked) {
            const char* text = source_file_start(source);
            const char* text_end = text + cstr_size(&source->contents);
            for (intptr_t line = (intptr_t)source_file_line_index(source, error->span.buf) - 1; line >= 0; line--) {
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

// What content mappers said about their `options` when their projects opened (tsgo's Project.Diagnostics()): at the
// node the path names inside the config's contentMappers[i].options (the options object if it isn't there)
static void program_report_content_mapper_options(Program* me) {
    u32 count;
    ContentMapperOptionDiagnostic* const* diagnostics =
        content_mapper_host_option_diagnostics(atomic_load(&my content_mapper_host), &count);
    for (u32 i = 0; i < count; i++) {
        ContentMapperOptionDiagnostic const* d = diagnostics[i];
        JsonValue const* mappers = json_get_tagged(&my config_json, "contentMappers", JSON_ARRAY);
        JsonValue const* at = mappers && d->mapper->index < mappers->count ?
            json_get(&mappers->items[d->mapper->index], "options") : nullptr;
        JsonValue path = {0};
        json_parse(cstr_sv(&d->path), &path);
        for (isize j = 0; at && path.tag == JSON_ARRAY && j < path.count; j++) {
            JsonValue const* segment = &path.items[j];
            JsonValue const* next = nullptr;
            if (segment->tag == JSON_STRING) next = json_get(at, cstr_str(&segment->string));
            else if (segment->tag == JSON_NUMBER && at->tag == JSON_ARRAY && segment->number >= 0 && segment->number < at->count) {
                next = &at->items[(isize)segment->number];
            }
            if (!next) break;
            at = next;
        }
        json_drop(&path);
        Error* error = at ? program_config_error(me, at->start, at->end, d->code, cstr_clone(d->message)) :
            error_ctx_global_error_s(my errs, d->code, cstr_clone(d->message));
        if (error) error->source = cstr_str(&d->source);
    }
}

// tsgo's applyContentMapperDiagnosticDirectives: a content mapper's directives suppress the semantic diagnostics that
// start in their virtual range (not the parser's, not the mapper's own); an `expect` one that suppressed nothing is the
// mapper's unused-directive diagnostic at its original range
static void program_apply_content_mapper_directives(Program* me) {
    bool any = false;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) any |= (*i)->content_mapped && (*i)->content_mapped->directive_count;
    if (!any) return;
    VecError* errors = &my errs->errors;
    for (isize i = 0; i < vec_error_size(errors);) {
        Error* error = &errors->data[i];
        SourceFile const* file = span_valid(error->span) ? span_source(error->span) : nullptr;
        ContentMapped* mapped = file ? file->content_mapped : nullptr;
        bool semantic = error->phase == ERROR_PHASE_RESOLVE_NAMES || error->phase == ERROR_PHASE_TYPECHECK ||
            (error->phase == ERROR_PHASE_PARSE && error->grammar);
        bool suppressed = false;
        if (mapped && semantic && !error->source) {
            u32 pos = (u32)(error->span.buf - cstr_str(&file->contents));
            for (u32 j = 0; j < mapped->directive_count && !suppressed; j++) {
                ContentMapDirective* directive = &mapped->directives[j];
                if (pos >= directive->virtual_start && pos < directive->virtual_end) {
                    directive->used = true;
                    suppressed = true;
                }
            }
        }
        if (suppressed) vec_error_erase_n(errors, i, 1);
        else i++;
    }
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        ContentMapped* mapped = (*i)->content_mapped;
        for (u32 j = 0; mapped && j < mapped->directive_count; j++) {
            ContentMapDirective const* directive = &mapped->directives[j];
            if (!directive->expect || directive->used) continue;
            char const* original = cstr_str(&mapped->original->contents);
            error_ctx_append_error(my errs, (Error){ .message = cstr_clone(directive->unused_message),
                .span = span_make(mapped->original, original + directive->original_start, original + directive->original_end),
                .ts_code = directive->unused_code, .phase = ERROR_PHASE_TYPECHECK,
                .source = cstr_str(&mapped->diagnostic_source) });
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
            SourceFile const* file = span_source(decl->name);
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
// lib_names (tsc's libMap) (lib.d.ts and lib.es6.d.ts, the targets' default libraries, before all), whatever
// order their references loaded them in. It is the order globals merge their declarations in (a merged interface's
// declarations, its overloads)
static isize stdlib_priority(SourceFile const* source) {
    if (!source->is_stdlib) return INTPTR_MAX;
    csview name = basename_of(cstr_sv(&source->canonical_path));
    if (csview_starts_with(name, "lib.")) name = csview_subview(name, 4, name.size - 4);
    if (!csview_ends_with(name, ".d.ts")) return (isize)lib_name_count + 2;
    name = csview_subview(name, 0, name.size - 5);
    if (csview_equals(name, "default") || csview_equals(name, "es6") || csview_equals(name, "")) return 0;
    for (isize i = 0; i < (isize)lib_name_count; i++) {
        if (csview_equals(name, lib_names[i])) return i + 1;
    }
    return (isize)lib_name_count + 2;
}

void program_order_stdlib_first(Program* me) {
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
    for (isize i = 0; i < count; i++) files[i]->program_order = my order_base + (u32)i;
}

// What resolve_file() does after the walk (a file walked in chunks: once its chunks are merged)
static void resolve_file_finish(SourceFile* source) {
    if (!source->is_stdlib && !source->unchecked && !source->is_declaration) unused_mark_jsdoc_links(source);
    // (Grown by doubling while the walk registered them: kept until checking ends, so without the slack)
    if (source->unused_cap > source->unused_count) {
        UnusedCandidate* trimmed = source->unused_count ? realloc(source->unused, sizeof(UnusedCandidate) * source->unused_count) : nullptr;
        if (trimmed || !source->unused_count) {
            if (!source->unused_count) free(source->unused);
            source->unused = trimmed;
            source->unused_cap = source->unused_count;
        }
    }
}

static void resolve_file(SourceFile* source, ErrorCtx* errs) {
    block_lookup_names_container(&source->ast, c_sv("file"), span_null, source->scope, errs);
    resolve_file_finish(source);
}

// Whether a file's resolve walk can run on a thread of its own (plan 45). A JavaScript file's does too: its assignment
// declarations (`this.x = v`, `F.prototype.x = v`, expandos), which declare into other files' symbols, wait for its
// replay (DEFERRED_JS_* in scope.c)
static bool resolves_in_parallel(SourceFile const* source) {
    return !source->resolve_serial;
}

// The chunked walk of big declaration files (lib.dom.d.ts: 2.4 MB, 2862 statements, most of a small project's walk on
// one thread). A file whose top-level statements leave the walk's state as they found it is walked in runs of
// statements (chunks), each on whichever thread takes it, into a shard of its own; the file's shards are merged in
// statement order where its one shard would be (resolve_shard_merge_chunks()). Every list a shard collects is ordered
// and of the file, so the merge sees the serial walk's lists, whatever the boundaries and the schedule.
// - The file's control flow builder: its scope's is &flow_builder_per_thread (scope_flow()), and each chunk walks with
//   a copy of the builder the serial walk would start with, its start node made. No statement the rule allows makes a
//   flow node of the file's container or leaves anything of the builder changed (checked after each), so every
//   statement sees the builder the serial walk would give it. References to the start node mark it atomically
// - A variable's Symbol.flow_start: written only by the chunk of its first variable declaration; another chunk reads
//   what the serial walk would have seen there (walk_flow_start_differs())
// - The one dependency between chunks: a later declaration of a generic class or interface shares the type parameter
//   symbols of the earlier ones, which their walks make (scope_new_generic_merged()): its chunk waits for theirs
// - The file's unused names: none (a declaration file's never are, unused_register())
typedef struct WalkChunk {
    u32 from, to; // Top-level statements
    isize start; // Where it starts in the text (the first chunk: 0), chunked_walk_chunk_at()
    u32* after; // malloc'd: the chunks that must be walked before this one starts
    u32 after_count;
    atomic_bool done;
} WalkChunk;

typedef struct ChunkedWalk {
    SourceFile* file;
    FlowBuilder flow; // The builder each chunk starts with
    FlowBuilder* outer; // What the scope's builder was
    WalkChunk* chunks;
    ResolveShard* shards; // A chunk's
    u32 chunk_count;
    u32 handed; // The chunks given an item of the lane so far
} ChunkedWalk;

// A top-level (or ambient namespace) statement of a declaration file that is a chunk's: a declaration that makes no
// flow node of its container and declares nothing outside the file's own tables
static bool statement_walks_alone(Statement const* statement) {
    if (statement_declares_name(statement->tag) &&
        (statement->decl.props.invalid_decorators || statement->decl.props.exported == DECL_EXPORTED_YES_DEFAULT)
    ) {
        return false;
    }
    switch (statement->tag) {
        case STATEMENT_NOOP:
        case STATEMENT_SOURCE_DIRECTIVE:
        case STATEMENT_DECL_INTERFACE:
        case STATEMENT_DECL_TYPE:
            return true;
        case STATEMENT_DECL_VARIABLE:
            for_each (i, vec_initialized_binding, &statement->decl.variable.initializers) {
                if (i->value) return false;
            }
            return true;
        case STATEMENT_DECL_FUNCTION:
            return !statement->decl.function.impl->has_body;
        case STATEMENT_DECL_NAMESPACE: {
            NamespaceDecl const* namespace = statement->decl.namespace;
            if (namespace->is_global || namespace->is_invalid_global || namespace->is_ambient_module ||
                namespace->is_shorthand || namespace->is_augmentation || namespace->labeled || namespace->misplaced ||
                namespace->jsdoc || namespace->attributes
            ) {
                return false;
            }
            for_each (i, vec_statement, &namespace->contents.statements) {
                if (!statement_walks_alone(i)) return false;
            }
            return true;
        }
        default:
            return false;
    }
}

// Where a span starts (or ends) in its file's text, -1 when it isn't in it (a missing node's, a directive's)
static isize statement_offset(SourceFile const* source, Span span, bool end) {
    char const* text = cstr_str(&source->contents);
    if (!span_valid(span) || span_source(span) != source) return -1;
    char const* at = end ? span_end_ptr(span) : span_start_ptr(span);
    return at >= text && at <= text + cstr_size(&source->contents) ? at - text : -1;
}

// The chunks of a file the chunked walk takes, null for any other file
static ChunkedWalk* chunked_walk_new(SourceFile* source) {
    if (!source->is_declaration || source->is_js || source->is_json || source->resolve_serial || flow_dump_enabled()) return nullptr;
    VecStatement const* statements = &source->ast.statements;
    u32 count = (u32)vec_statement_size(statements);
    isize size = cstr_size(&source->contents);
    u32 chunk_bytes = debug_env.walk_chunk_bytes ? debug_env.walk_chunk_bytes : 128 * 1024;
    if (debug_env.walk_chunk_bytes ? count < 2 : size < 256 * 1024 || count < 64) return nullptr;
    for_each (i, vec_statement, statements) {
        if (!statement_walks_alone(i)) return nullptr;
    }
    ChunkedWalk* ret = c_new(ChunkedWalk, { .file = source });
    ret->chunks = c_new_n(WalkChunk, count);
    // (Chunks of at least chunk_bytes from the start of their first statement to the end of their last, where the
    // statements' spans are in the text: not every one's is)
    u32 from = 0;
    isize start = -1;
    for (u32 i = 0; i < count; i++) {
        Statement const* statement = vec_statement_at(statements, i);
        if (start < 0) start = statement_offset(source, statement->span, false);
        isize end = statement_offset(source, statement->span, true);
        if (start >= 0 && end >= 0 && end - start >= chunk_bytes && i + 1 < count) {
            u32 at = ret->chunk_count++;
            ret->chunks[at] = (WalkChunk){ .from = from, .to = i + 1, .start = at ? start : 0 };
            from = i + 1;
            start = -1;
        }
    }
    // (The rest: a chunk of its own if any of its statements is in the text, else the last chunk's)
    if (start >= 0 || !ret->chunk_count) {
        u32 at = ret->chunk_count++;
        ret->chunks[at] = (WalkChunk){ .from = from, .to = count, .start = at ? start : 0 };
    } else {
        ret->chunks[ret->chunk_count - 1].to = count;
    }
    if (ret->chunk_count < 2) {
        free(ret->chunks);
        free(ret);
        return nullptr;
    }
    ret->shards = c_new_n(ResolveShard, ret->chunk_count);
    return ret;
}

static void chunked_walk_drop(ChunkedWalk* me) {
    for (u32 i = 0; i < my chunk_count; i++) free(my chunks[i].after);
    free(my chunks);
    free(my shards);
    free(me);
}

// The chunk with a position in the file's text
static u32 chunked_walk_chunk_at(ChunkedWalk const* me, char const* at) {
    isize offset = at - cstr_str(&my file->contents);
    u32 low = 0;
    u32 high = my chunk_count;
    while (high - low > 1) {
        u32 mid = low + (high - low) / 2;
        if (my chunks[mid].start <= offset) low = mid;
        else high = mid;
    }
    return low;
}

typedef struct ChunkedWalks {
    ChunkedWalk** walks;
    u32 count;
} ChunkedWalks;

// binder_generic_merge_pairs()'s: the later declaration's chunk waits for the earlier one's
static void chunked_walk_pair(void* ctx, SourceFile* file, Span later, Span earlier) {
    ChunkedWalks* walks = ctx;
    for (u32 w = 0; w < walks->count; w++) {
        ChunkedWalk* walk = walks->walks[w];
        if (!walk || walk->file != file) continue;
        u32 to = chunked_walk_chunk_at(walk, span_start_ptr(later));
        u32 from = chunked_walk_chunk_at(walk, span_start_ptr(earlier));
        if (from == to) return;
        if (from > to) {
            // (Declarations are in source order: never, but walked whole the file would be right anyway)
            log_error("A declaration's earlier declaration is after it");
            chunked_walk_drop(walk);
            walks->walks[w] = nullptr;
            return;
        }
        WalkChunk* chunk = &walk->chunks[to];
        for (u32 i = 0; i < chunk->after_count; i++) {
            if (chunk->after[i] == from) return;
        }
        chunk->after = realloc(chunk->after, sizeof(u32) * (chunk->after_count + 1));
        panic_if(!chunk->after, "out of memory");
        chunk->after[chunk->after_count++] = from;
        return;
    }
}

// Before the lane: the file's builder, as block_lookup_names_container() makes it, and its scope's made per thread
static void chunked_walk_start(ChunkedWalk* me) {
    Scope* scope = my file->scope;
    my outer = scope_flow(scope);
    flow_builder_init(&my flow, my outer, scope, nullptr, nullptr);
    my flow.current = flow_new_start(&my flow, nullptr, false);
    my flow.container = &my file->ast;
    my flow.statement_list = &my file->ast.statements;
    my flow.lookup_statements = &my file->ast.statements;
    scope->flow_ = &flow_builder_per_thread;
}

// Whether a chunk's builder is the one it started with, but for what is its own
static bool chunked_walk_flow_unchanged(ChunkedWalk const* me, FlowBuilder const* flow) {
    FlowBuilder probe = *flow;
    probe.arena = my flow.arena;
    probe.errs = my flow.errs;
    probe.next_id = my flow.next_id;
    probe.id_counter = my flow.id_counter;
    probe.statement_list = my flow.statement_list;
    probe.lookup_statements = my flow.lookup_statements;
    // (A cache of equal contexts: a chunk's first property access makes one the serial walk might have shared)
    probe.property_context = my flow.property_context;
    return memcmp(&probe, &my flow, sizeof probe) == 0;
}

// The chunk this thread walks, if any
static thread_local ChunkedWalk* walk_chunked;
static thread_local u32 walk_chunk;

// The chunk of a symbol's first variable declaration in the file walked in chunks (top_level: whether it is in the
// file's own scope rather than a namespace's), UINT32_MAX when it has none there
static u32 chunked_walk_binding_chunk(ChunkedWalk const* walk, Symbol const* symbol, bool* top_level) {
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (decl->kind != SYMBOL_DECL_VARIABLE) continue;
        if (span_source(decl->full) != walk->file) return UINT32_MAX;
        if (!decl->block) log_error("A variable declaration without its scope");
        *top_level = decl->block == walk->file->scope;
#ifdef DEBUG_BUILD
        // (The first variable declaration is the first binding the serial walk reaches: source order in the file)
        for (u32 j = i + 1; j < symbol->decl_count; j++) {
            SymbolDecl const* later = &symbol->decls[j];
            if (later->kind == SYMBOL_DECL_VARIABLE && span_source(later->full) == walk->file) {
                check(span_start_ptr(later->full) >= span_start_ptr(decl->full));
            }
        }
#endif
        return chunked_walk_chunk_at(walk, span_start_ptr(decl->full));
    }
    return UINT32_MAX;
}

bool chunked_walk_writes_flow_start(Symbol const* symbol) {
    ChunkedWalk const* walk = walk_chunked;
    if (!walk) return true;
    bool top_level = false;
    u32 chunk = chunked_walk_binding_chunk(walk, symbol, &top_level);
    return chunk == UINT32_MAX || chunk == walk_chunk;
}

bool chunked_walk_flow_start_differs(Symbol const* symbol, FlowNode const* container_start) {
    ChunkedWalk const* walk = walk_chunked;
    bool top_level = false;
    u32 chunk = walk ? chunked_walk_binding_chunk(walk, symbol, &top_level) : UINT32_MAX;
    if (chunk == UINT32_MAX || chunk == walk_chunk) return symbol->flow_start && container_start != symbol->flow_start;
    // (The serial walk hasn't walked the binding yet)
    if (chunk > walk_chunk) return false;
    // It has: the binding's container is the file, or a namespace walked in another chunk (never this one)
    return !top_level || container_start != walk->flow.container_start;
}

static void chunked_walk_run(ChunkedWalk* me, u32 c, ErrorCtx* errs) {
    WalkChunk* chunk = &my chunks[c];
    for (u32 i = 0; i < chunk->after_count; i++) {
        while (!atomic_load_explicit(&my chunks[chunk->after[i]].done, memory_order_acquire)) sched_yield();
    }
    SourceFile* source = my file;
    FlowBuilder flow = my flow;
    flow.next_id = &flow.id_counter;
    flow.arena = binder_arena(source->scope->binder);
    flow.errs = errs;
    flow_thread_builder = &flow;
    walk_chunked = me;
    walk_chunk = c;
    VecStatement* statements = &source->ast.statements;
    for (u32 i = chunk->from; i < chunk->to; i++) {
        flow.statement_list = statements;
        flow.lookup_statements = statements;
        statement_resolve_names(vec_statement_at_mut(statements, i), source->scope, errs);
        if (!chunked_walk_flow_unchanged(me, &flow)) {
            log_error("A statement of a file walked in chunks changed its flow builder: ", span_line_index(vec_statement_at(statements, i)->span) + 1);
            my flow = flow; // (Once)
        }
    }
    flow_thread_builder = nullptr;
    walk_chunked = nullptr;
    atomic_store_explicit(&chunk->done, true, memory_order_release);
}

// After the lane, in program order: the chunks' shards merged as the file's one would be
static void chunked_walk_finish(ChunkedWalk* me) {
    resolve_shard_merge_chunks(my shards, my chunk_count);
    flow_builder_finish(&my flow, c_sv("file"), span_null);
    my file->scope->flow_ = my outer;
    resolve_file_finish(my file);
}

// One job of a resolve lane: a file, or a chunk of one
typedef struct ResolveItem {
    SourceFile* file;
    ResolveShard* shard;
    ChunkedWalk* chunked; // Null: the whole file
    u32 chunk;
    isize size;
    double ms; // TTTT_RESOLVE_TIMES
} ResolveItem;

// The TypeScript files' walks, shared by the resolve threads
typedef struct ResolveLane {
    Program* program;
    void (*run)(SourceFile* source, ErrorCtx* errs); // A file's job: resolve_file(), declare_file()
    ResolveItem* items; // Biggest first (a file's chunks in their order)
    u32 count;
    atomic_uint next;
} ResolveLane;

static double now_ms(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void resolve_lane_run(ResolveLane* me, u32 thread) {
    Binder* binder = my program->binder;
    Arena* arena = thread ? binder_thread_arena(binder, thread) : &binder->arena;
    Arena* scope_arena = thread ? binder_thread_scope_arena(binder, thread) : &binder->scope_arena;
    for (u32 i; (i = atomic_fetch_add(&my next, 1)) < my count;) {
        ResolveItem* item = &my items[i];
        double start = debug_env.resolve_times ? now_ms() : 0;
        ResolveShard* shard = item->shard;
        *shard = (ResolveShard){
            .binder = binder, .arena = arena, .scope_arena = scope_arena,
            .errs = { .phase = my program->errs->phase, .parent = my program->errs },
            .discarded = error_ctx_scratch(my program->errs),
        };
        resolve_shard = shard;
        if (item->chunked) chunked_walk_run(item->chunked, item->chunk, &shard->errs);
        else my run(item->file, &shard->errs);
        resolve_shard = nullptr;
        if (debug_env.resolve_times) item->ms = now_ms() - start;
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
    scope_release_thread();
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

// Biggest first, so that one big file doesn't start last (the items are in program order). A file's chunks take the
// places its chunks got in their order: a chunk waits only for chunks handed out before it
static void resolve_lane_sort(ResolveLane* me) {
    ResolveOrder* order = c_new_n(ResolveOrder, my count ? my count : 1);
    for (u32 i = 0; i < my count; i++) order[i] = (ResolveOrder){ my items[i].size, i };
    qsort(order, my count, sizeof(ResolveOrder), resolve_order_cmp);
    ResolveItem* items = c_new_n(ResolveItem, my count ? my count : 1);
    for (u32 i = 0; i < my count; i++) {
        ResolveItem* item = &items[i];
        *item = my items[order[i].index];
        if (!item->chunked) continue;
        item->chunk = item->chunked->handed++;
        item->shard = &item->chunked->shards[item->chunk];
    }
    free(order);
    free(my items);
    my items = items;
}

// The lane's costliest jobs on stderr (TTTT_RESOLVE_TIMES)
static void resolve_lane_report(ResolveLane const* me, char const* name) {
    if (!debug_env.resolve_times || !my count) return;
    u32 top[8];
    u32 top_count = 0;
    for (u32 i = 0; i < my count; i++) {
        u32 at = top_count < 8 ? top_count++ : 7;
        if (at == 7 && top_count == 8 && my items[top[7]].ms >= my items[i].ms) continue;
        top[at] = i;
        while (at > 0 && my items[top[at - 1]].ms < my items[top[at]].ms) {
            u32 swap = top[at - 1];
            top[at - 1] = top[at];
            top[at] = swap;
            at--;
        }
    }
    for (u32 i = 0; i < top_count; i++) {
        ResolveItem const* item = &my items[top[i]];
        if (item->chunked) {
            eprintln("resolve: ", name, " ", item->ms, " ms ", cstr_str(&item->file->canonical_path), " chunk ", item->chunk, " of ", item->chunked->chunk_count);
        } else {
            eprintln("resolve: ", name, " ", item->ms, " ms ", cstr_str(&item->file->canonical_path));
        }
    }
}

// The resolve walk of every file (plan 45). TypeScript files are walked in any order, on --threads threads, each into a
// ResolveShard of its own; then, in program order, each TypeScript file's shard is replayed and merged and each
// JavaScript file walked. The order of everything that reaches the binder is the same for any thread count
// TTTT_RESOLVE_TIMES: the time since *last under the given name on stderr, and *last reset
void timeline_phase(char const* name) __attribute__((weak)); // bench/timeline's LD_PRELOADed sampler, absent otherwise
static void resolve_step_done(char const* name, double* last) {
    if (timeline_phase && *name) {
        char mark[64];
        snprintf(mark, sizeof mark, "resolve: %s", name);
        timeline_phase(mark);
    }
    if (!debug_env.resolve_times) return;
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    double now = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
    // (The process's minor faults meanwhile: page faulting, not the work, was most of small projects' link)
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    static long faults_last;
    if (*last) eprintln("resolve: ", name, " ", now - *last, " ms, ", usage.ru_minflt - faults_last, " faults");
    faults_last = usage.ru_minflt;
    *last = now;
}

// Runs the lane on `threads` threads, the calling one among them (with Binder.arena), the program's error context frozen
// meanwhile (every file reports into its shard's)
static void resolve_lane_threads(Program* me, ResolveLane* lane, u32 threads) {
    my errs->frozen = true;
    if (threads <= 1) {
        resolve_lane_run(lane, 0);
    } else {
        // Workers get the main thread's stack size: the walk recurses
        pthread_t* workers = c_new_n(pthread_t, threads - 1);
        ResolveThread* args = c_new_n(ResolveThread, threads - 1);
        for (u32 t = 1; t < threads; t++) binder_thread_arena(my binder, t);
        for (u32 t = 0; t < threads - 1; t++) {
            args[t] = (ResolveThread){ lane, t + 1 };
            panic_if(worker_thread_create(&workers[t], resolve_lane_thread, &args[t]), "couldn't start a resolve thread");
        }
        resolve_lane_run(lane, 0);
        for (u32 t = 0; t < threads - 1; t++) worker_thread_join(workers[t]);
        free(workers);
        free(args);
    }
    my errs->frozen = false;
}

// A file's declarations (program_resolve_names()'s declare step)
static void declare_file(SourceFile* source, ErrorCtx* errs) {
    ignore errs; // (binder_errs(): the shard's on a declare thread)
    block_add_names(&source->ast, source->scope);
    scope_add_commonjs_module(source->scope);
    scope_check_export_merges(source->scope, block_span(&source->ast));
    if (source->is_module) scope_check_duplicate_exports(source->scope);
}

// An assignment scope_add_expandos() takes for an expando's (`f.x = v`, a chain): its host is found by name, maybe in the
// globals, and gets the member
static bool is_expando_candidate(Expr const* e) {
    return e->tag == EXPR_BINARY_OP && e->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(e);
}

// Whether the block (a file's, a namespace's) declares something outside its file: `declare global`, `declare module
// "m"` (an ambient module or an augmentation), `export as namespace` (a UMD global), an expando assignment (whose host
// may be anyone's). A labeled statement says yes
static bool block_declares_outside(Block const* block) {
    for_each (i, vec_statement, &block->statements) {
        switch (i->tag) {
            case STATEMENT_EXPR:
                if (is_expando_candidate(&i->expr)) return true;
                break;
            case STATEMENT_DECL_VARIABLE:
                for_each (j, vec_initialized_binding, &i->decl.variable.initializers) {
                    Expr const* value = initialized_binding_value(j);
                    if (value && is_expando_candidate(value)) return true;
                }
                break;
            case STATEMENT_DECL_NAMESPACE: {
                NamespaceDecl const* namespace = i->decl.namespace;
                if (namespace->is_global || namespace->is_invalid_global || namespace->is_ambient_module ||
                    namespace->is_augmentation || block_declares_outside(&namespace->contents)
                ) {
                    return true;
                }
            }   break;
            case STATEMENT_IMPORT_EXPORT:
                if (i->import_export->main.tag == PORT_EXPORT_AS_NAMESPACE) return true;
                break;
            case STATEMENT_LABELED:
                return true;
            default:
                break;
        }
    }
    return false;
}

// Whether a file's declare step only writes its own tables (plan 86 stage 3): a TypeScript module that declares nothing
// outside itself. Its declarations go nowhere another file's do and it reads nothing another file declares, so it
// declares on a thread, into a ResolveShard of its own (the binder's guards say if one reaches a shared table)
static bool declares_in_isolation(SourceFile const* source) {
    return source->is_module && !source->is_js && !source->is_json && !source->is_stdlib &&
        !block_declares_outside(&source->ast);
}

// The declare step of every file: the files that declare in isolation on --threads threads (biggest first) into a
// shard each, then in program order each one's shard merged and every other file declared. The binder sees the same
// symbols, reports and pending merges in the same order at any thread count
static void program_declare_files(Program* me) {
    u32 file_count = (u32)vec_source_file_ptr_size(&my source_files_ordered);
    u32 threads = my threads ? my threads : 1;
    bool* isolated = c_new_n(bool, file_count ? file_count : 1);
    ResolveShard* shards = c_new_n(ResolveShard, file_count ? file_count : 1);
    ResolveLane lane = { .program = me, .run = declare_file, .items = c_new_n(ResolveItem, file_count ? file_count : 1) };
    for (u32 i = 0; i < file_count; i++) {
        SourceFile* source = *vec_source_file_ptr_at(&my source_files_ordered, i);
        isolated[i] = threads > 1 && declares_in_isolation(source);
        if (isolated[i]) lane.items[lane.count++] = (ResolveItem){ .file = source, .shard = &shards[i], .size = cstr_size(&source->contents) };
    }
    // (Few files aren't worth a thread each)
    if (threads > lane.count / 8 && !debug_env.parallel_resolve) threads = lane.count / 8 ? lane.count / 8 : 1;
    if (debug_env.resolve_times) eprintln("resolve: ", lane.count, " of ", file_count, " files declare in isolation");
    if (threads <= 1) {
        for (u32 i = 0; i < file_count; i++) declare_file(*vec_source_file_ptr_at(&my source_files_ordered, i), my errs);
        free(isolated);
        free(shards);
        free(lane.items);
        return;
    }
    resolve_lane_sort(&lane);
    atomic_init(&lane.next, 0);
    double last = 0;
    resolve_step_done("", &last);
    my binder->isolated_declares = true;
    resolve_lane_threads(me, &lane, threads);
    my binder->isolated_declares = false;
    resolve_step_done("declare in isolation", &last);
    for (u32 i = 0; i < file_count; i++) {
        SourceFile* source = *vec_source_file_ptr_at(&my source_files_ordered, i);
        if (isolated[i]) resolve_shard_merge(&shards[i]);
        else declare_file(source, my errs);
    }
    resolve_step_done("declare shard merge and the other files", &last);
    free(lane.items);
    free(shards);
    free(isolated);
}

static void program_resolve_walk(Program* me) {
    u32 file_count = (u32)vec_source_file_ptr_size(&my source_files_ordered);
    u32 threads = my threads ? my threads : 1;
    ResolveShard* shards = c_new_n(ResolveShard, file_count ? file_count : 1);
    double last = 0;
    resolve_step_done("", &last);
    binder_mark_serial_resolves(my binder);
    resolve_step_done("mark serial resolves", &last);
    // The files walked in chunks (by index in source_files_ordered, null for the others)
    ChunkedWalks chunked = { .walks = calloc(file_count ? file_count : 1, sizeof(ChunkedWalk*)), .count = file_count };
    panic_if(!chunked.walks, "out of memory");
    u32 item_count = 0;
    bool any_js = false;
    bool any_chunked = false;
    for (u32 i = 0; i < file_count; i++) {
        SourceFile* source = *vec_source_file_ptr_at(&my source_files_ordered, i);
        any_js |= source->is_js;
        if (!resolves_in_parallel(source)) continue;
        if (threads > 1 || debug_env.walk_chunk_bytes) chunked.walks[i] = chunked_walk_new(source);
        any_chunked |= chunked.walks[i] != nullptr;
        item_count += chunked.walks[i] ? chunked.walks[i]->chunk_count : 1;
    }
    if (any_chunked) binder_generic_merge_pairs(my binder, chunked_walk_pair, &chunked);
    if (any_chunked && debug_env.resolve_times) {
        u32 files = 0, chunks = 0, waits = 0;
        for (u32 i = 0; i < file_count; i++) {
            ChunkedWalk const* walk = chunked.walks[i];
            if (!walk) continue;
            files++;
            chunks += walk->chunk_count;
            for (u32 c = 0; c < walk->chunk_count; c++) waits += walk->chunks[c].after_count;
        }
        eprintln("resolve: ", files, " files walked in ", chunks, " chunks (", waits, " waits for an earlier chunk)");
    }
    ResolveLane lane = { .program = me, .run = resolve_file, .items = c_new_n(ResolveItem, item_count ? item_count : 1) };
    for (u32 i = 0; i < file_count; i++) {
        SourceFile* source = *vec_source_file_ptr_at(&my source_files_ordered, i);
        if (!resolves_in_parallel(source)) continue;
        ChunkedWalk* walk = chunked.walks[i];
        if (!walk) {
            lane.items[lane.count++] = (ResolveItem){ .file = source, .shard = &shards[i], .size = cstr_size(&source->contents) };
            continue;
        }
        chunked_walk_start(walk);
        for (u32 c = 0; c < walk->chunk_count; c++) {
            isize end = c + 1 < walk->chunk_count ? walk->chunks[c + 1].start : cstr_size(&source->contents);
            lane.items[lane.count++] = (ResolveItem){ .file = source, .chunked = walk, .chunk = c, .size = end - walk->chunks[c].start };
        }
    }
    // (The `require` of a JavaScript file that doesn't declare one: one symbol, made before the walks share it)
    if (any_js) binder_require_symbol(my binder);
    resolve_lane_sort(&lane);
    atomic_init(&lane.next, 0);
    // (Few files aren't worth a thread each)
    if (threads > lane.count / 8 && !debug_env.parallel_resolve) threads = lane.count / 8 ? lane.count / 8 : 1;
    // Nothing reports into the program's context meanwhile, every file into its own
    resolve_lane_threads(me, &lane, threads);
    resolve_step_done("parallel walk", &last);
    resolve_lane_report(&lane, "walked");
    for (u32 i = 0; i < file_count; i++) {
        SourceFile* source = *vec_source_file_ptr_at(&my source_files_ordered, i);
        if (chunked.walks[i]) {
            chunked_walk_finish(chunked.walks[i]);
            chunked_walk_drop(chunked.walks[i]);
        } else if (resolves_in_parallel(source)) {
            resolve_shard_merge(&shards[i]);
        } else {
            resolve_file(source, my errs);
        }
    }
    resolve_step_done("shard merge and serial files", &last);
    free(chunked.walks);
    free(lane.items);
    free(shards);
}

void program_verify_options(Program* me) {
    program_validate_options(me);
    program_verify_inferred_root_dir(me);
    program_verify_emit_paths(me);
    program_verify_project_files(me);
}

// tsc's initializeTypeChecker asks for the global types it keeps (getGlobalType: Array, Function...) before it merges
// the scripts' ambient modules into the globals. getDeclaredTypeOfClassOrInterface's isThislessInterface resolves the
// base names of each declaration, up to the first one that mentions `this` or whose base is missing or has a `this`
// type. An import such a base name goes through is resolved right then, when no ambient module can be it, and stays
// that way (`declare global { interface Function extends Ember.X {} }` with `import Ember from "ember"` and "ember" a
// script's `declare module`: TS2306 on the import, Ember unknown). Followed only as far as it can be told without
// resolving names: a base name that starts with a local name (marking it if it's an import) or isn't a global
// interface ends it, which only misses imports tsc would reach
static bool init_global_type_thisless(Program* me, Symbol const* symbol, u32 depth) {
    if (depth > 8) return false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (decl->kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* interface_decl = decl->node;
        if (interface_decl->contains_this) return false;
        for_each (extends, vec_spanned_type, &interface_decl->extends) {
            if (extends->type.tag != TYPE_IDENTIFIER) return false;
            IdentifierType const* identifier = extends->type.identifier;
            if (identifier_type_import_ref(identifier)) return false;
            bool qualified = identifier->rare && ast_spans_size(&identifier->rare->namespaces);
            csview name = span_sv(qualified ? *ast_spans_at(&identifier->rare->namespaces, 0) : identifier->name);
            for (Scope const* scope = decl->block; scope && scope->file; scope = scope->parent) {
                Symbol const* local = symbol_table_get(scope->locals, name);
                if (!local) continue;
                PortItem const* item = local->flags & SYMBOL_ALIAS && local->alias ? local->alias->item : nullptr;
                if (item && item->module_ref && (item->tag == PORT_IMPORT_DEFAULT || item->tag == PORT_IMPORT_GATHER || item->tag == PORT_IMPORT_SPECIFIC)) {
                    item->module_ref->resolved_before_ambient = true;
                }
                return false;
            }
            if (qualified) return false;
            Symbol const* base = symbol_table_get(&my binder->globals, name);
            if (!base || !(base->flags & SYMBOL_INTERFACE) || (base->flags & SYMBOL_CLASS)) return false;
            if (base == symbol || !init_global_type_thisless(me, base, depth + 1)) return false;
        }
    }
    return true;
}

static void program_mark_init_resolved_imports(Program* me) {
    if (!my binder->ambient_modules.count) return;
    static char const* const names[] = {
        "IArguments", "Array", "Object", "Function", "CallableFunction", "NewableFunction", "String", "Number", "Boolean",
        "RegExp", "ReadonlyArray", "ThisType",
    };
    for (u32 i = 0; i < c_arraylen(names); i++) {
        bool bind_call_apply = !strcmp(names[i], "CallableFunction") || !strcmp(names[i], "NewableFunction");
        if (bind_call_apply && !my options.strict_bind_call_apply) continue;
        Symbol const* symbol = symbol_table_get(&my binder->globals, csview_from(names[i]));
        if (symbol && symbol->flags & SYMBOL_INTERFACE && !(symbol->flags & SYMBOL_CLASS)) init_global_type_thisless(me, symbol, 0);
    }
}

// What every module name refers to (binder_module_of_ref()): a lookup per ref that writes only the ref, in the
// ambient modules, the file's module symbol or the patterns, all complete by now, so on --threads threads. Except
// when a pattern has augmentations, whose lookup sets the augmentation's parent table (serial then)
typedef struct ModuleRefsLane {
    Program* program;
    atomic_uint next;
    pthread_mutex_t deferred_lock;
    u32* deferred; // The files with a ref binder_module_of_ref_parallel() deferred, from it on
    u32 deferred_count, deferred_cap;
} ModuleRefsLane;

static void* module_refs_lane_run(void* arg) {
    ModuleRefsLane* lane = arg;
    Program* me = lane->program;
    u32 count = (u32)vec_source_file_ptr_size(&my source_files_ordered);
    // (In chunks: a file has a handful of refs)
    for (u32 start; (start = atomic_fetch_add(&lane->next, 64)) < count;) {
        for (u32 i = start; i < start + 64 && i < count; i++) {
            SourceFile* source = *vec_source_file_ptr_at(&my source_files_ordered, i);
            for (u32 j = 0; j < source->module_ref_count; j++) {
                if (binder_module_of_ref_parallel(my binder, source->module_refs[j])) continue;
                // (Deferred: the file, from this ref on)
                pthread_mutex_lock(&lane->deferred_lock);
                {
                    if (lane->deferred_count == lane->deferred_cap) {
                        lane->deferred_cap = lane->deferred_cap ? lane->deferred_cap * 2 : 64;
                        lane->deferred = realloc(lane->deferred, sizeof(u32) * lane->deferred_cap);
                        panic_if(!lane->deferred, "out of memory");
                    }
                    lane->deferred[lane->deferred_count++] = i;
                }
                pthread_mutex_unlock(&lane->deferred_lock);
                break;
            }
        }
    }
    return nullptr;
}

static int u32_compare(void const* a, void const* b) {
    u32 x = *(u32 const*)a, y = *(u32 const*)b;
    return x < y ? -1 : x > y;
}

static void program_link_module_refs(Program* me) {
    u32 count = (u32)vec_source_file_ptr_size(&my source_files_ordered);
    u32 threads = my threads ? my threads : 1;
    if (threads > count / 256 && !debug_env.parallel_resolve) threads = count / 256 ? count / 256 : 1;
    ModuleRefsLane lane = { .program = me };
    atomic_init(&lane.next, 0);
    pthread_mutex_init(&lane.deferred_lock, nullptr);
    pthread_t* workers = threads > 1 ? c_new_n(pthread_t, threads - 1) : nullptr;
    u32 started = 0;
    while (started + 1 < threads && !worker_thread_create(&workers[started], module_refs_lane_run, &lane)) started++;
    module_refs_lane_run(&lane);
    for (u32 t = 0; t < started; t++) worker_thread_join(workers[t]);
    free(workers);
    // What meets a pattern augmentation, in program order: the first to find one sets its exports' parent, as when
    // every ref went in order (refs before a deferred one in its file are done: they set nothing)
    qsort(lane.deferred, lane.deferred_count, sizeof(u32), u32_compare);
    if (debug_env.resolve_times) eprintln("resolve: module refs on ", started + 1, " threads, ", lane.deferred_count, " files' deferred");
    for (u32 d = 0; d < lane.deferred_count; d++) {
        SourceFile* source = *vec_source_file_ptr_at(&my source_files_ordered, lane.deferred[d]);
        for (u32 j = 0; j < source->module_ref_count; j++) binder_module_of_ref(my binder, source->module_refs[j]);
    }
    free(lane.deferred);
    pthread_mutex_destroy(&lane.deferred_lock);
}

// What loading kept and nothing needs once names are resolved: the module resolutions, the workers' preresolved
// imports, the source texts' pages (vscode: ~25 ms at the end of the front end, serial, run next to the check instead)
static void* program_release_run(void* arg) {
    Program* me = arg;
    program_free_preresolved(me);
    module_resolver_forget_resolutions(my resolver);
    for (u32 i = 0; i < my reference_count && my reference_resolvers; i++) module_resolver_forget_resolutions(my reference_resolvers[i]);
    // (The mapped texts' pages stay: dropping them was a madvise() per segment as the checkers started, a TLB shootdown
    // on every CPU running one, for file pages the page cache keeps anyway: vscode's peak RSS within noise)
    return nullptr;
}

void program_resolve_names(Program* me) {
    double last = 0;
    resolve_step_done("", &last);
    program_report_content_mapper_options(me);
    // (main.c finished them after the roots: another way of loading still must)
    program_finish_deferred_libs(me, "at resolve", c_sv(""));
    program_collect_files(me);
    CHANGE_PHASE(PROGRAM_PHASE_LOADING, PROGRAM_PHASE_RESOLVING_NAMES);
    // Loading is over: what the pool parsed for nothing goes
    if (!my pool && getenv("TTTT_LOAD_POOL_STATS")) eprintln("load: no pool, the loader read and parsed for ", my loader_parse_ms, " ms");
    load_pool_destroy(my pool);
    my pool = nullptr;
    program_verify_options(me);
    my errs->phase = ERROR_PHASE_RESOLVE_NAMES;
    set_compiler_options(&my options);
    program_order_stdlib_first(me);
    resolve_step_done("collect files, the load pool's end, options", &last);

    // Declare: symbols, the scopes of files and namespaces, exports
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        source->scope = scope_new_file(my binder, source);
        // What module augmentations augment, if something else brought it into the program
        for (u32 j = 0; j < source->module_ref_count; j++) {
            ModuleRef* ref = source->module_refs[j];
            if (ref->is_declaration && !ref->file && cstr_size(&module_ref_rare(ref)->augmented_path)) {
                ref->file = program_find_source(me, cstr_sv(&module_ref_rare(ref)->augmented_path));
            }
            // (A package's second copy is the first to an augmentation too: tsc's redirect source file has the
            // first's symbol)
            while (ref->is_declaration && ref->file && ref->file->package_redirect_target) ref->file = ref->file->package_redirect_target;
        }
    }
    resolve_step_done("file scopes", &last);
    program_declare_files(me);
    binder_declare_global_augmentations(my binder);
    program_check_builtin_global_names(me);
    binder_check_ambient_module_exports(my binder, my errs);
    resolve_step_done("declare", &last);

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
                csview shown = cstr_sv(&module_ref_rare(ref)->resolved_path);
                cstr absolute = shown.size && shown.buf[0] == '/' ? cstr_from_sv(shown) : cstr_d(program_cwd(me), "/", shown);
                ref->file = program_find_source(me, cstr_sv(&absolute));
                ref->used_ts_extension = ref->file && csview_ends_with(cstr_sv(&ref->path), ".tsx");
                cstr_drop(&absolute);
                continue;
            }
            if (ref->failure != MODULE_REF_IS_JS || !cstr_size(&module_ref_rare(ref)->package_name)) continue;
            csview name = cstr_sv(&module_ref_rare(ref)->package_name);
            cstr types = cstr_lit("@types/");
            if (name.size && name.buf[0] == '@') {
                isize slash = csview_find_sv(name, c_sv("/"));
                if (slash != c_NPOS) cstr_append_d(&types, DEFAULT_DISPLAY, csview_slice(name, 1, slash), "__", csview_slice(name, slash + 1, name.size));
                else cstr_append_sv(&types, name);
            } else {
                cstr_append_sv(&types, name);
            }
            auto bundles = hmap_package_flag_get(&my packages, name);
            module_ref_rare_mut(ref)->package_hint = hmap_package_flag_get(&my packages, cstr_sv(&types)) ? MODULE_REF_HINT_TYPES_EXIST :
                bundles && bundles->second ? MODULE_REF_HINT_BUNDLES_TYPES : MODULE_REF_HINT_INSTALL_TYPES;
            cstr_drop(&types);
        }
    }

    program_mark_init_resolved_imports(me);
    resolve_step_done("between declare and link", &last);
    // Link: what every module name refers to (ambient modules are all declared now), `export *` tables and every alias. Nothing writes to a symbol after this
    program_link_module_refs(me);
    resolve_step_done("link: module refs", &last);
    binder_link(my binder);
    resolve_step_done("link", &last);
    binder_check_export_star_conflicts(my binder, my errs);
    resolve_step_done("link: export star conflicts", &last);

    // Resolve: every identifier gets its symbol, function and block scopes are made on the way
    program_resolve_walk(me);
    resolve_step_done("walk", &last);

    binder_resolve_remaining_aliases(my binder);
    resolve_step_done("remaining aliases", &last);

    error_ctx_remove_unchecked(my errs);
    program_apply_comment_directives(me, ERROR_PHASE_RESOLVE_NAMES);
    resolve_step_done("comment directives", &last);

    // (Nothing resolves module names any more: what loading kept goes, on a thread of its own)
    my release_started = !worker_thread_create(&my release_thread, program_release_run, me);
    if (!my release_started) program_release_run(me);
    resolve_step_done("releases started", &last);
    clear_compiler_options();
    my errs->phase = ERROR_PHASE_UNKOWN;
    CHANGE_PHASE(PROGRAM_PHASE_RESOLVING_NAMES, PROGRAM_PHASE_NAMES_RESOLVED);
}

void program_make_current(Program* me) {
    panic_if(my phase < PROGRAM_PHASE_NAMES_RESOLVED, "program needs its names resolved before types can be evaluated");
    if (!my checker) my checker = checker_new(me);
    checker_make_current(my checker);
}

// (16 KB, for wall time before CPU and memory, 2026-10-22: at 64 KB small projects got 2-3 checkers and ran faster at a
// forced 8-12. Callgrind per thread at the defaults but --checkers K, the longest checker's instructions / every
// checker's / the whole run's, in G: nest (240 KB) K3 0.104 / 0.27 / 1.19, K4 0.094 / 0.28 / 1.20, K8 0.070 / 0.32 /
// 1.25, K12 0.054 / 0.34 / 1.27; bulletproof-react (177 KB) K2 0.369 / 0.68 / 1.48, K4 0.236 / 0.90 / 1.70, K8 0.178 /
// 1.19 / 1.99, K12 0.155 / 1.44 / 2.24. Walls on the 16-vCPU box: nest 0.082 -> 0.075 s at K8 (CPU +8%), react 0.112
// -> 0.088 (CPU +49%). Each extra checker builds the global and library types again (~9 M instructions on nest) and
// whatever its files share (~67 M on react: React's types re-derived per checker). The cap at the physical cores stays:
// past it TypeScript6's longest checker grows (1.06 -> 1.44 G at K12), the threads left to parse bodies ahead
// (materialize-ahead) being fewer. 32 KB is the alternative if CPU matters more (about half the extra CPU, most of the
// gain); the laptop confirms the wall. Before: 64 KB, below which a second checker cost 4-9% of the RSS, plan 62 stage 3)
enum { CHECKER_BYTES = 16 << 10 };
// Past program_set_checker_cap()'s physical cores, one more checker (a second hardware thread's) per SMT_CHECKER_BYTES
// of checked source, up to the threads: a checker on a core's second thread pays for itself only when each checker has
// enough to check to win back the global and library types it builds again. On 12 cores / 24 threads vscode (138 MB,
// 5.8 MB a checker at K24) is 17% faster at K24 than at K12; webpack (21 MB) ties and drizzle (2.4 MB) and xstate
// (1.5 MB) lose 9-23% (2026-10-21). No project of the source sweep or the corpora lies between 21 and 138 MB
enum { SMT_CHECKER_BYTES = 4 << 20 };

void program_set_first_touch_stamps(Program* me, bool stamps) {
    my first_touch_stamps = stamps;
}

bool program_first_touch_stamps(Program const* me) {
    return my first_touch_stamps;
}

void program_set_shared_graph(Program* me, bool shared) {
    my shared_graph = shared;
    // (A shared graph hands out type ids in scheduling order, and deep nesting decides by "newer" types: by id, one
    // worker's relation cache was poisoned by the schedule and measured every later variance as unreliable, plan 86 4e
    // item 1. Stamps make "newer" this worker's history; at one checker they equal the ids)
    // (TTTT_FIRST_TOUCH_STAMPS=0: not, to test what needs them)
    char const* stamps = getenv("TTTT_FIRST_TOUCH_STAMPS");
    if (shared && !(stamps && !strcmp(stamps, "0"))) my first_touch_stamps = true;
}

struct TypeDirectory* program_type_directory(Program const* me) {
    return my type_directory;
}

struct AtomTable* program_atoms(Program const* me) {
    return my atoms;
}

TyGraph* program_graph(Program const* me) {
    return my graph;
}


void program_set_checkers(Program* me, u32 count) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    my requested_checkers = count;
}

void program_set_checker_cap(Program* me, u32 count) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    my checker_cap = count;
}

void program_set_checker_memory(Program* me, u64 bytes) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    my checker_memory = bytes;
}

void program_set_checker_memory_cap(Program* me, u64 bytes) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    my checker_memory_cap = bytes;
}

void program_set_memory_pool(Program* me, u64 bytes) {
    my memory_pool = bytes;
}

void program_set_checker_memory_shared(Program* me, u64 bytes) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    my checker_memory_shared = bytes;
}

void program_set_emit_declarations(Program* me, bool emit) { my emit_declarations = emit; }

void program_set_declarations_only(Program* me, bool only) { my declarations_only = only; }

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

void program_set_defer_bodies(Program* me, bool defer) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    my defer_bodies = defer;
}

void program_offer_listing(void* program, csview dir, csview const* names, signed char const* is_directory, isize count) {
    Program* me = program;
    cached_fs_offer_listing(my cached_fs, dir, names, is_directory, count);
}

bool program_has_load_pool(Program const* me) {
    return my pool != nullptr;
}

bool program_has_deferred_bodies(Program const* me) {
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        if ((*i)->has_deferred_bodies) return true;
    }
    return false;
}

bool program_deferred_bodies_parse(Program* me) {
    CompilerOptions const* saved = compiler_options_private;
    bool ret = true;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* file = *i;
        DeferredBodies* list = file->deferred_bodies;
        // (Skipped bodies the resolve walk hasn't listed yet, before it ran: they can't be parsed from here, the caller
        // runs again without deferral)
        if (!list && file->has_deferred_bodies) {
            ret = false;
            break;
        }
        if (!list || atomic_load(&list->materialized)) continue;
        // (Parsed into the materialization's memory and dropped: the bodies are pending again, as the parser left them)
        ErrorCtx scratch = { .phase = ERROR_PHASE_PARSE };
        compiler_options_private = program_options_of(me, file);
        bool same = true;
        ast_materialize_begin(&list->memory, false);
        for (DeferredBody* body = list->first; body && same; body = body->next) {
            same = parse_deferred_body(file, body->function, &scratch);
        }
        ast_materialize_end();
        for (DeferredBody* body = list->first; body; body = body->next) body->block = (Block){0};
        ast_arena_drop(&list->memory);
        error_ctx_drop(&scratch);
        if (!same) {
            ret = false;
            break;
        }
    }
    compiler_options_private = saved;
    return ret;
}

bool program_defer_failed(Program const* me) {
    return atomic_load(&my defer_failed);
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

// Which checker checked the index-th checked file (the one it was dealt to, when nobody did)
static u32 share_of(Program const* me, u32 index) {
    u32 c = atomic_load_explicit(&my file_checkers[index], memory_order_relaxed);
    return c == UINT32_MAX ? my file_shares[index] : c;
}

// The checker that checked a file in program_typecheck() (the first one for a file nobody checked). Expression types
// are only complete in the checker that walked the file: one that didn't would type an expression without the
// contextual type its parent gives it (see "Expressions" in documentation.md)
Checker* program_checker_of(Program const* me, SourceFile const* file) {
    for (u32 c = 0; c < my checker_count; c++) {
        panic_if(my checkers[c]->recycles, "program_checker_of() after recycled checkers (program_set_checker_memory())");
    }
    if (my checker_count <= 1) return my checker;
    u32 index = 0;
    for_each (i, vec_source_file_ptr, &my check_files) {
        if (*i == file) return my checkers[share_of(me, index)];
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
            if (!span_source(decl->name) || span_source(decl->name)->is_stdlib) break;
            error_ctx_error_d(my errs, decl->name, 2317, "Global type '", names[i], "' must have ", arity,
                " type parameter(s).");
            break;
        }
    }
}

void program_report_global_diagnostics(Program* me) {
    panic_if(my phase != PROGRAM_PHASE_NAMES_RESOLVED, "global diagnostics are reported between resolving and checking");
    if (my global_diagnostics_reported) return;
    my global_diagnostics_reported = true;
    set_compiler_options(&my options);
    ErrorPhase phase = my errs->phase;
    my errs->phase = ERROR_PHASE_TYPECHECK;
    program_report_missing_global_types(me);
    my errs->phase = phase;
    clear_compiler_options();
}

// The checkers of program_typecheck(), taken by the threads in index order. Which files a checker is dealt depends only
// on the file list and the checker count; which ones it checks also on timing (check_share() takes others' files)
typedef struct CheckShares {
    Program* program;
    u32 count;
    atomic_uint next;
} CheckShares;

// One of Checker.deferred (tsc's checkDeferredNode)
// (It is the current node, like tsc's checkDeferredNode sets currentNode: what goes wrong without a node of its own
// inside, TS2589, TS2590, is reported there)
static void check_deferred(DeferredCheck deferred, ErrorCtx* errs) {
    Checker* checker = current_checker;
    switch (deferred.kind) {
        case DEFERRED_FUNCTION_BODY: {
            CurrentNode saved = ty_enter_node(checker, ((FunctionImpl*)deferred.node)->full_span);
            function_impl_typecheck_body((FunctionImpl*)deferred.node, errs);
            ty_leave_node(checker, saved);
        }   break;
        case DEFERRED_CLASS_MEMBERS: class_decl_typecheck_members((ClassDecl*)deferred.node, errs); break;
        case DEFERRED_JSX_ELEMENT: {
            CurrentNode saved = ty_enter_node(checker, ((Expr const*)deferred.node)->inner_span);
            jsx_element_typecheck(deferred.node, errs);
            ty_leave_node(checker, saved);
        }   break;
        case DEFERRED_EXPR: {
            CurrentNode saved = ty_enter_node(checker, ((Expr const*)deferred.node)->inner_span);
            expr_typecheck(deferred.node, errs);
            ty_leave_node(checker, saved);
        }   break;
    }
}

// Plan 78: deferred function bodies

// The file whose deferred bodies the thread is parsing and walking: their readers there take them as they are
static thread_local SourceFile const* materializing_file;

// A deferred function's scope and the outer walk's flow nodes the body's walk refers to (their reference counts, the
// return label's list), as the walk of its signature left them (DeferredBodies.snapshots)
typedef struct DeferredScopeSnapshot {
    SymbolTable* locals;
    SymbolTable copy;
    u32 flow_flags[4]; // DeferredBody.start, container_start, current, return_target
    // ...as all the bodies' walks left them: what a body materialized alone puts back (its walk alone marks a node
    // another body's walk marks too, referenced rather than shared)
    u32 walked_flow_flags[4];
    FlowList* return_antecedents;
} DeferredScopeSnapshot;

static FlowNode** snapshot_flow_node(DeferredBody* body, u32 i) {
    switch (i) {
        case 0: return &body->start;
        case 1: return &body->container_start;
        case 2: return &body->current;
        default: return &body->return_target;
    }
}

// One of DeferredBodies.undo: the `size` (at most 8) bytes at `at` were `old` before the walk wrote them
typedef struct DeferredUndo {
    void* at;
    u64 old;
    u32 size;
    void (*drop)(void*); // A pointer to what the walk made: dropped when the old one is put back
} DeferredUndo;

static void deferred_bodies_log(void* at, u64 old, u32 size, void (*drop)(void*)) {
    ResolveShard* shard = resolve_shard;
    DeferredBodies* list = shard ? shard->materialized : nullptr;
    if (!list) return;
    if (list->undo_count == list->undo_cap) {
        list->undo_cap = list->undo_cap ? list->undo_cap * 2 : 4;
        list->undo = realloc(list->undo, sizeof(DeferredUndo) * list->undo_cap);
        panic_if(!list->undo, "out of memory");
    }
    list->undo[list->undo_count++] = (DeferredUndo){ .at = at, .old = old, .size = size, .drop = drop };
}

void deferred_bodies_undo(void* at, u32 size) {
    for (u32 i = 0; i < size; i += 8) {
        u64 old = 0;
        u32 n = size - i < 8 ? size - i : 8;
        memcpy(&old, (char*)at + i, n);
        deferred_bodies_log((char*)at + i, old, n, nullptr);
    }
}

void deferred_bodies_undo_made(void** at, void* old, void (*drop)(void*)) {
    deferred_bodies_log(at, (u64)(uintptr_t)old, sizeof(void*), drop);
}

// The sum of the declaration counts of a scope's own symbols: a body's declaration merged into one of them (a `var`
// named like a parameter) changes it
static u64 locals_decl_count(SymbolTable const* table) {
    u64 ret = 0;
    for (u32 i = 0; table && i < table->count; i++) ret += table->entries[i].symbol->decl_count;
    return ret;
}

// Parses and walks every deferred body of the file (its lock held, its state pending), into a memory of its own. The
// first materialization's diagnostics go to the file's DeferredBodies.errs, its walk's pending reports (they ask the
// checker for types: not under the lock, a type may need another file's bodies) are returned for the caller to make
// (resolve_shard_finish_materialized()); a later one's are the same, and dropped
// (TTTT_DEFER_STATS's times)
static u64 defer_stats_now(void) {
    if (!debug_env.defer_stats) return 0;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u64)now.tv_sec * 1000000 + (u64)now.tv_nsec / 1000;
}

// Whether a materialization makes body i: `only` if it is one (a body alone, DeferredBodies.singly), else every pending
static bool materializes(DeferredBody const* i, DeferredBody const* only) {
    return only ? i == only : !atomic_load_explicit(&i->ready, memory_order_relaxed);
}

// The bodies' unused registrations in walk order (SourceFile.unused), once every body of a materialization that made
// some alone is made: the eager walk's order, which unused_report() reports in
static void deferred_bodies_unused_in_order(SourceFile* file) {
    DeferredBodies* list = file->deferred_bodies;
    u32 count = file->unused_count - list->unused_before;
    if (!count) return;
    UnusedCandidate* ordered = c_new_n(UnusedCandidate, count);
    u32 n = 0;
    for (DeferredBody* i = list->first; i; i = i->next) {
        memcpy(ordered + n, file->unused + i->unused_from, sizeof(UnusedCandidate) * (i->unused_to - i->unused_from));
        n += i->unused_to - i->unused_from;
    }
    check(n == count);
    memcpy(file->unused + list->unused_before, ordered, sizeof(UnusedCandidate) * count);
    free(ordered);
}

// `only`: that body alone (the file's bodies are singly and were materialized before), else every pending one
static ResolveShard* program_materialize_locked(Program* me, SourceFile* file, bool cross, DeferredBody* only) {
    u64 start = defer_stats_now();
    DeferredBodies* list = file->deferred_bodies;
    bool first = !atomic_load_explicit(&list->materialized, memory_order_relaxed);
    DeferredBodiesState state = atomic_load_explicit(&list->state, memory_order_relaxed);
    check(!only || (!first && list->singly));
    SourceFile const* outer = materializing_file;
    materializing_file = file;
    ErrorCtx scratch = { .phase = ERROR_PHASE_RESOLVE_NAMES };
    ErrorCtx* errs = first ? &list->errs : &scratch;
    list->errs.parent = my errs;
    bool same = true;
    // (Going on with a materialization some bodies of are made: in the same kind of memory)
    ast_materialize_begin(&list->memory, state == DEFERRED_BODIES_PARTIAL ? list->memory.lasting : cross);
    lazy_slots_new_block();
    u32 walking = 0;
    for (DeferredBody* i = list->first; i; i = i->next) {
        if (!materializes(i, only)) continue;
        bool parsed = parse_deferred_body(file, i->function, errs);
        if (!parsed) defer_stat_add(DEFER_STAT_PARSE_FAILED, 1);
        same &= parsed;
        walking++;
    }
    u64 parsed_at = defer_stats_now();
    if (state == DEFERRED_BODIES_PENDING) defer_stat_add(first ? DEFER_STAT_FILES : DEFER_STAT_REMATERIALIZED, 1);
    if (only) defer_stat_add(DEFER_STAT_SINGLY, 1);
    else if (state == DEFERRED_BODIES_PARTIAL) defer_stat_add(DEFER_STAT_SINGLY_COMPLETED, 1);
    defer_stat_add(DEFER_STAT_BODIES, walking);
    u64* decls_before = nullptr;
    if (first) {
        list->unused_before = file->unused_count;
        list->snapshots = c_new_n(DeferredScopeSnapshot, list->count);
        decls_before = c_new_n(u64, list->count);
        u32 index = 0;
        for (DeferredBody* i = list->first; i; i = i->next, index++) {
            SymbolTable* locals = i->function_scope->locals;
            DeferredScopeSnapshot* snapshot = &list->snapshots[index];
            *snapshot = (DeferredScopeSnapshot){
                .locals = locals, .copy = locals ? *locals : (SymbolTable){0},
                .return_antecedents = i->return_target && !flow_is_unreachable(i->return_target) ? i->return_target->antecedents : nullptr,
            };
            for (u32 n = 0; n < 4; n++) {
                FlowNode* node = *snapshot_flow_node(i, n);
                snapshot->flow_flags[n] = node ? node->flags : 0;
            }
            FunctionImpl const* f = i->function;
            if (f->end_flow_ || f->has_explicit_return_ || f->has_bare_return_ || function_impl_rare(f)->yields_ ||
                function_impl_rare(f)->return_flow_ || function_impl_rare(f)->first_super_call_ ||
                span_valid(function_impl_rare(f)->first_this_or_super_)
            ) {
                log_error("A deferred function has what its body's walk sets before it");
            }
            decls_before[index] = locals_decl_count(locals);
        }
    }
    // (On the heap: the walk's deferred events point at its contexts, and a helper leaves it to a checker)
    ResolveShard* shard = c_new(ResolveShard, {
        .binder = my binder,
        .errs = { .phase = ERROR_PHASE_RESOLVE_NAMES, .parent = my errs },
        .discarded = { .phase = ERROR_PHASE_RESOLVE_NAMES },
        .materialization = true,
        .materialized = list,
        .own_arenas = { { .ast_backed = ARENA_AST_DIRECT }, { .ast_backed = ARENA_AST_CHUNKED } },
    });
    shard->arena = &shard->own_arenas[0];
    shard->scope_arena = &shard->own_arenas[1];
    ResolveShard* outer_shard = resolve_shard;
    u32 suppressed = unused_marking_suppressed;
    unused_marking_suppressed = 0;
    resolve_shard = shard;
    if (only) list->out_of_order = true;
    u32 index = 0;
    for (DeferredBody* i = list->first; i; i = i->next, index++) {
        if (!materializes(i, only)) continue;
        i->unused_from = file->unused_count;
        bool walked = function_impl_lookup_deferred_body(i, &shard->errs);
        if (!walked) defer_stat_add(DEFER_STAT_COLLIDED, 1);
        same &= walked;
        i->unused_to = file->unused_count;
        if (!first) {
            DeferredScopeSnapshot const* snapshot = &list->snapshots[index];
            for (u32 n = 0; n < 4; n++) {
                FlowNode* node = *snapshot_flow_node(i, n);
                if (node && !flow_is_unreachable(node)) node->flags = snapshot->walked_flow_flags[n];
            }
        }
    }
    resolve_shard = outer_shard;
    unused_marking_suppressed = suppressed;
    ast_materialize_end();
    materializing_file = outer;
    for (DeferredBody* i = list->first; i; i = i->next) {
        if (materializes(i, only)) atomic_store_explicit(&i->ready, true, memory_order_release);
    }
    bool all = true;
    for (DeferredBody* i = list->first; i && all; i = i->next) all = atomic_load_explicit(&i->ready, memory_order_relaxed);
    if (all && list->out_of_order) {
        deferred_bodies_unused_in_order(file);
        list->out_of_order = false;
    }
    if (first) {
        u32 at = 0;
        for (DeferredBody* i = list->first; i; i = i->next, at++) {
            for (u32 n = 0; n < 4; n++) {
                FlowNode* node = *snapshot_flow_node(i, n);
                list->snapshots[at].walked_flow_flags[n] = node ? node->flags : 0;
            }
        }
        // (A body alone: when no walk wrote anything outside the bodies but what a drop puts back per body)
        list->singly = true;
        for (u32 i = 0; i < list->undo_count; i++) list->singly &= list->undo[i].drop != nullptr;
    }
    if (first) {
        u32 index = 0;
        for (DeferredBody* i = list->first; i && !list->pinned; i = i->next, index++) {
            DeferredScopeSnapshot const* snapshot = &list->snapshots[index];
            // (The entries the signature's walk made are still where the copy says, whatever the walk added)
            list->pinned = locals_decl_count(snapshot->locals ? &snapshot->copy : nullptr) != decls_before[index];
        }
        free(decls_before);
        if (list->pinned) defer_stat_add(DEFER_STAT_PINNED, 1);
        if (!same) atomic_store(&my defer_failed, true);
    }
    error_ctx_drop(&scratch);
    // (TTTT_DEFER_STATS: the slow ones, which a run's critical path may start with: TypeScript6's checker.ts, ~35 ms
    // of parse and ~35 of walk, nearly all of it createTypeChecker's one body)
    if (start && defer_stats_now() - start > 10000) {
        eprintln("defer: ", file->canonical_path, " materialized in ", (i64)(defer_stats_now() - start), " us (parsed in ",
            (i64)(parsed_at - start), ", ", walking, " bodies)");
    }
    if (start) defer_stat_add(first ? DEFER_STAT_MATERIALIZE_US : DEFER_STAT_REMATERIALIZE_US, (i64)(defer_stats_now() - start));
    atomic_store_explicit(&list->state, !same ? DEFERRED_BODIES_FAILED : all ? DEFERRED_BODIES_READY : DEFERRED_BODIES_PARTIAL,
        memory_order_release);
    atomic_store_explicit(&list->materialized, true, memory_order_release);
    if (first) return shard;
    resolve_shard_discard(shard);
    free(shard);
    return nullptr;
}

// A materialization no checker holds any more (its lock held): what its walk wrote outside the bodies is put back,
// its memory given back, and the bodies are pending again. What pointed into it was the holders' (checker
// incarnations: their types, links and memos), and they're gone
static void deferred_bodies_drop_locked(SourceFile* file) {
    u64 start = defer_stats_now();
    DeferredBodies* list = file->deferred_bodies;
    for (u32 i = list->undo_count; i-- > 0;) {
        DeferredUndo const* undo = &list->undo[i];
        if (undo->drop && *(void**)undo->at != (void*)(uintptr_t)undo->old) undo->drop(*(void**)undo->at);
        memcpy(undo->at, &undo->old, undo->size);
    }
    list->undo_count = 0;
    file->unused_count = list->unused_before;
    u32 index = 0;
    for (DeferredBody* i = list->first; i; i = i->next, index++) {
        DeferredScopeSnapshot const* snapshot = &list->snapshots[index];
        i->function_scope->locals = snapshot->locals;
        if (snapshot->locals) symbol_table_restore(snapshot->locals, &snapshot->copy);
        for (u32 n = 0; n < 4; n++) {
            FlowNode* node = *snapshot_flow_node(i, n);
            if (node && !flow_is_unreachable(node)) node->flags = snapshot->flow_flags[n];
        }
        if (i->return_target && !flow_is_unreachable(i->return_target)) i->return_target->antecedents = snapshot->return_antecedents;
        // (Not block_drop(): what the bodies' AST owns is all in the arena, their strings too, ast_cstr_seal(). The
        // harness's leak check holds a drop to that)
        i->block = (Block){0};
        atomic_store_explicit(&i->ready, false, memory_order_relaxed);
    }
    list->out_of_order = false;
    ast_arena_drop(&list->memory);
    defer_stat_add(DEFER_STAT_DROPPED, 1);
    if (start) defer_stat_add(DEFER_STAT_DROP_US, (i64)(defer_stats_now() - start));
    atomic_store_explicit(&list->state, DEFERRED_BODIES_PENDING, memory_order_release);
}

static void deferred_bodies_release_one(DeferredBodies* list, SourceFile* file) {
    pthread_mutex_lock(&list->lock);
    check(list->refs);
    DeferredBodiesState state = atomic_load_explicit(&list->state, memory_order_relaxed);
    if (--list->refs == 0 && !list->pinned && (state == DEFERRED_BODIES_READY || state == DEFERRED_BODIES_PARTIAL)) {
        deferred_bodies_drop_locked(file);
    }
    pthread_mutex_unlock(&list->lock);
}

// (The file of a DeferredBodies: its first body's function's)
static SourceFile* deferred_bodies_file(DeferredBodies const* list) {
    return (SourceFile*)span_source(list->first->function->full_span);
}

void deferred_bodies_release(DeferredBodies** held, u32 count) {
    for (u32 i = 0; i < count; i++) deferred_bodies_release_one(held[i], deferred_bodies_file(held[i]));
}

static bool checker_holds(Checker const* checker, SourceFile const* file) {
    u32 word = file->id / 64;
    return word < checker->held_files_words && checker->held_files[word] >> (file->id % 64) & 1;
}

static void checker_hold(Checker* checker, SourceFile const* file, DeferredBodies* list) {
    u32 word = file->id / 64;
    if (word >= checker->held_files_words) {
        u32 words = word * 2 + 8;
        checker->held_files = realloc(checker->held_files, sizeof(u64) * words);
        checker->cross_files = realloc(checker->cross_files, sizeof(u64) * words);
        panic_if(!checker->held_files || !checker->cross_files, "out of memory");
        memset(checker->held_files + checker->held_files_words, 0, sizeof(u64) * (words - checker->held_files_words));
        memset(checker->cross_files + checker->held_files_words, 0, sizeof(u64) * (words - checker->held_files_words));
        checker->held_files_words = words;
    }
    checker->held_files[word] |= (u64)1 << (file->id % 64);
    if (checker->held_count == checker->held_cap) {
        checker->held_cap = checker->held_cap ? checker->held_cap * 2 : 64;
        checker->held = realloc(checker->held, sizeof(DeferredBodies*) * checker->held_cap);
        panic_if(!checker->held, "out of memory");
    }
    checker->held[checker->held_count++] = list;
}

// The pending reports of a file's first materialization, in the current checker
static void program_finish_materialized(DeferredBodies* list, ResolveShard* shard) {
    resolve_shard_finish_materialized(shard, &list->errs);
    free(shard);
}

// Materialize-ahead (program_typecheck()): body source bytes helpers materialized that no checker took yet, and the
// most they may get ahead by (TTTT_AHEAD_KB)
static _Atomic u64 ahead_bytes;
static u64 ahead_limit;
static u64 ahead_resume; // (Where they start again: an eighth of it, vscode K1 T4 12.13 s at a half, 12.05)
static pthread_mutex_t ahead_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ahead_taken = PTHREAD_COND_INITIALIZER;
#define AHEAD_LIMIT_KB 4096

// The file's deferred bodies for the current checker incarnation (materialized unless some holder has them), held
// until it is dropped. A helper (no checker) holds them for the first checker to come. `only`: a checker needs that
// body (null: all of them); materialized before and singly, a file's pending bodies are materialized one at a time
static void deferred_bodies_acquire(Program* me, SourceFile* file, DeferredBody* only) {
    DeferredBodies* list = file->deferred_bodies;
    if (!list) return;
    Checker* checker = current_checker;
    bool held = checker && checker_holds(checker, file);
    bool cross_file = checker && checker->checking_file && checker->checking_file != file;
    if (held && (only ? atomic_load_explicit(&only->ready, memory_order_acquire) :
        atomic_load_explicit(&list->state, memory_order_acquire) != DEFERRED_BODIES_PARTIAL)
    ) {
        if (cross_file) checker->cross_files[file->id / 64] |= (u64)1 << (file->id % 64);
        return;
    }
    pthread_mutex_lock(&list->lock);
    ResolveShard* unfinished = nullptr;
    DeferredBodiesState state = atomic_load_explicit(&list->state, memory_order_relaxed);
    if (!checker && (list->refs || state != DEFERRED_BODIES_PENDING)) {
        // (Someone has it already: nothing for the helper to do)
        pthread_mutex_unlock(&list->lock);
        return;
    }
    if (only && !(list->singly && atomic_load_explicit(&list->materialized, memory_order_relaxed))) only = nullptr;
    if ((state == DEFERRED_BODIES_PENDING || state == DEFERRED_BODIES_PARTIAL) &&
        !(only && atomic_load_explicit(&only->ready, memory_order_relaxed))
    ) {
        if (cross_file && state == DEFERRED_BODIES_PENDING) defer_stat_add(DEFER_STAT_CROSS, 1);
        unfinished = program_materialize_locked(me, file, cross_file, only);
    }
    if (!checker) {
        list->ahead = true;
        list->refs++;
        atomic_fetch_add(&ahead_bytes, list->source_bytes);
        // (A helper has no checker: the first checker to take the file makes the reports)
        atomic_store_explicit(&list->unfinished, unfinished, memory_order_release);
        pthread_mutex_unlock(&list->lock);
        return;
    }
    if (held) {
        // (A first materialization is never held before: no reports to make)
        pthread_mutex_unlock(&list->lock);
        if (cross_file) checker->cross_files[file->id / 64] |= (u64)1 << (file->id % 64);
        return;
    }
    if (list->ahead) {
        list->ahead = false;
        u64 before = atomic_fetch_sub(&ahead_bytes, list->source_bytes);
        if (before > ahead_resume && before - list->source_bytes <= ahead_resume) {
            pthread_mutex_lock(&ahead_lock);
            pthread_cond_broadcast(&ahead_taken);
            pthread_mutex_unlock(&ahead_lock);
        }
    } else {
        list->refs++;
    }
    if (!unfinished) unfinished = atomic_exchange(&list->unfinished, nullptr);
    pthread_mutex_unlock(&list->lock);
    checker_hold(checker, file, list);
    if (cross_file) checker->cross_files[file->id / 64] |= (u64)1 << (file->id % 64);
    if (unfinished) program_finish_materialized(list, unfinished);
}

void deferred_bodies_materialize(SourceFile const* file) {
    if (file == materializing_file) return;
    Checker* checker = current_checker;
    if (checker) deferred_bodies_acquire(checker->program, (SourceFile*)file, nullptr);
    else log_error("Deferred function bodies needed outside a checker");
}

Block* function_impl_deferred_body(FunctionImpl const* me) {
    DeferredBody* body = me->body_.deferred;
    SourceFile* file = (SourceFile*)span_source(me->full_span);
    if (file == materializing_file) return &body->block;
    Checker* checker = current_checker;
    if (checker) deferred_bodies_acquire(checker->program, file, body);
    else log_error("A deferred function body needed outside a checker");
    if (!body->function) log_error("A deferred function body the resolve walk never reached");
    return &body->block;
}

// The end of program_typecheck(): the deferred bodies no checker needed (files not checked: TTTT_CHECK_ONLY) are
// materialized, for their diagnostics, and every file's materialization diagnostics join the program's, the comment
// directives applying to them as to the resolve walk's
static void program_finish_deferred_bodies(Program* me) {
    bool any = false;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* file = *i;
        if (!file->deferred_bodies) continue;
        any = true;
        if (!program_defer_failed(me) && !atomic_load(&file->deferred_bodies->materialized)) deferred_bodies_acquire(me, file, nullptr);
        ResolveShard* unfinished = atomic_exchange(&file->deferred_bodies->unfinished, nullptr);
        if (unfinished) program_finish_materialized(file->deferred_bodies, unfinished);
        error_ctx_merge(my errs, &file->deferred_bodies->errs);
        error_ctx_drop(&file->deferred_bodies->errs);
        file->deferred_bodies->errs = (ErrorCtx){ .phase = ERROR_PHASE_RESOLVE_NAMES };
    }
    if (!any) return;
    error_ctx_remove_unchecked(my errs);
    program_apply_comment_directives(me, ERROR_PHASE_RESOLVE_NAMES);
}

// TTTT_FILE_TIMES
// (One fprintf per line: the checker threads' lines interleave, scripts/perfshape.sh reads them)
static void print_file_time(char const* what, struct timespec start, SourceFile const* source, u32 c, char const* more) {
    struct timespec end;
    timespec_get(&end, TIME_UTC);
    double ms = (double)(end.tv_sec - start.tv_sec) * 1e3 + (double)(end.tv_nsec - start.tv_nsec) / 1e6;
    fprintf(stderr, "file time: %10.1f ms %s (checker %u%s) %s\n", ms, what, c, more, cstr_str(&source->canonical_path));
}

#ifdef TTTT_SURVIVORS
// The file census (TTTT_FILE_CENSUS=1): see ty/survivors.c
void survivors_file_census(Checker const* me, char const* when);
u32 survivors_file_start(Checker const* me, u32 at);
u32 survivors_owner_of(Checker const* me, u32 at);
#endif

// Whether a diagnostic a checker reported is sure to be reported: nothing program_typecheck() filters afterwards can
// drop it (no comment directive or content mapper in its file, a checked file, not once per program)
static bool error_surely_reported(Error const* error) {
    SourceFile const* source = span_valid(error->span) ? span_source(error->span) : nullptr;
    return source && !error->suggestion && !error->unchecked_js && !error->jsdoc && !error->once_key &&
        !source->unchecked && !source->package_redirect &&
        !source->comment_directive_count && !source->content_mapped;
}

// Whether the declaration walk is for nothing: its diagnostics are gated (noEmit, noEmitOnError) and some checker
// reported a diagnostic that will be reported. Which checker saw one first depends on the threads, what is reported
// doesn't: program_typecheck() drops the declaration diagnostics then anyway
static bool declaration_walk_moot(Program* me) {
    return !my emit_declarations && program_declaration_diagnostics_gated(me) &&
        atomic_load_explicit(&my declaration_diagnostics_moot, memory_order_relaxed);
}

// After a segment's files (indices into check_files, in the order it checked them) are checked, in the checker that
// checked them: unused_report() reads the marks the checker made on members while checking, the declaration walk the
// types it inferred
static void check_segment_after(Program* me, Checker* checker, u32 c, u32 const* segment, u32 count) {
    for (u32 i = 0; i < count; i++) {
        SourceFile* source = vec_source_file_ptr_at(&my check_files, segment[i])[0];
        struct timespec file_start;
        if (debug_env.file_times) timespec_get(&file_start, TIME_UTC);
#ifdef TTTT_SURVIVORS
        survivors_owner = survivors_owner_of(checker, segment[i]);
#endif
        if (!source->is_declaration && !source->unchecked) unused_report(source, checker->errs);
        if (my declaration_errs && !source->is_stdlib && !declaration_walk_moot(me)) {
            declaration_diagnostics_check(source, &my declaration_errs[c], my emit_declarations);
        }
#ifdef TTTT_SURVIVORS
        survivors_owner = 0;
#endif
        if (debug_env.file_times) print_file_time("unused and declaration", file_start, source, c, "");
    }
}

// A fresh checker for share c, reporting where the last one did
// (ty/dupcensus.c, TTTT_DUP_CENSUS=1)
void dup_census_add(Checker* me, u32 share);
void dup_census_report(void);
void dup_census_file(u32 share, u32 at, u64 bytes);
#ifdef TTTT_SURVIVORS
void survivors_report(Checker const* me);
#endif

static Checker* checker_recycled(Program* me, Checker* old, u32 c) {
#ifdef TTTT_SURVIVORS
    if (getenv("TTTT_FILE_CENSUS")) survivors_file_census(old, "recycle");
    else survivors_report(old);
#endif
    if (debug_env.dup_census) dup_census_add(old, c);
    Checker* checker = checker_new(me);
    checker->errs = old->errs;
    checker->recycles = old->recycles + 1;
    checker->peak_memory = old->peak_memory;
    // The deferred bodies the old one read for other files are likely read again (a module many import): the new one
    // holds them, as long as it reads them too (plan 78 stage 2: vscode at one checker materializes ~1100 files again
    // without). Only those materialized for another file (lasting, ast_materialize_begin()): one a file's own check made
    // would keep the huge page it shares with that segment's other files (vscode K1 +150 MB)
    for (u32 i = 0; i < old->held_count; i++) {
        DeferredBodies* list = old->held[i];
        SourceFile* file = deferred_bodies_file(list);
        if (!(old->cross_files[file->id / 64] >> (file->id % 64) & 1) || !list->memory.lasting) continue;
        pthread_mutex_lock(&list->lock);
        list->refs++;
        pthread_mutex_unlock(&list->lock);
        checker_hold(checker, file, list);
    }
    checker_drop(old);
    my checkers[c] = checker;
    checker_make_current(checker);
    ty_init_global_types(checker);
    return checker;
}

static pthread_mutex_t retired_lock = PTHREAD_MUTEX_INITIALIZER;
// --shared-graph: the checker retired (kept until the program ends: the graph holds what it made) and a fresh one, a
// new worker on the same graph, made current in its place. At a file boundary nothing of the old one is in flight
static Checker* graph_fresh_worker(Program* me, Checker* checker) {
    pthread_mutex_lock(&retired_lock);
    if (my retired_count == my retired_cap) {
        my retired_cap = my retired_cap ? my retired_cap * 2 : 8;
        my retired = realloc(my retired, sizeof(Checker*) * my retired_cap);
        panic_if(!my retired, "out of memory");
    }
    my retired[my retired_count++] = checker;
    pthread_mutex_unlock(&retired_lock);
    checker_make_current(nullptr);
    Checker* fresh = checker_new(me);
    fresh->errs = checker->errs;
    fresh->peak_memory = checker->peak_memory;
    checker_make_current(fresh);
    ty_init_global_types(fresh);
    return fresh;
}

// Below this a checker doesn't recycle for the memory pool: it would free little and rebuild its warm state
#define CHECKER_POOL_MIN_HELD ((u64)64 << 20)

// program_set_memory_pool(): whether the checker recycles after its file, the process being past the pool. The
// checkers holding the most go first (at least the average of what each held after its last file), so the pool's
// pressure is relieved where most can go; below the pool nobody recycles (the output doesn't depend on when)
static bool pool_recycle_due(Program* me, Checker* checker, u32 c, u32 count) {
    u64 held = checker_memory(checker);
    atomic_store_explicit(&my pool_held[c], held, memory_order_relaxed);
    if (held < CHECKER_POOL_MIN_HELD) return false;
    u64 resident = process_resident_bytes();
    if (!resident || resident <= my memory_pool) return false;
    u64 sum = 0;
    for (u32 i = 0; i < count; i++) sum += atomic_load_explicit(&my pool_held[i], memory_order_relaxed);
    if (held * count < sum) return false;
    if (debug_env.file_times) {
        fprintf(stderr, "file time: pool recycle checker %u, held %llu MB, resident %llu MB of a %llu MB pool, checkers %llu MB\n",
            c, (unsigned long long)(held >> 20), (unsigned long long)(resident >> 20),
            (unsigned long long)(my memory_pool >> 20), (unsigned long long)(sum >> 20));
    }
    atomic_store_explicit(&my pool_held[c], 0, memory_order_relaxed);
    return true;
}

// A file's check by `checker`
static void check_file(Program* me, Checker* checker, SourceFile* source, u32 at) {
    __atomic_store_n(&checker->file_order, at, __ATOMIC_RELAXED);
#ifdef TTTT_SURVIVORS
    survivors_owner = survivors_file_start(checker, at);
#endif
    checker->flow_analysis_disabled = false;
    deferred_bodies_acquire(me, source, nullptr);
    if (checker->graph) graph_memo_file_start(checker);
    if (source->is_declaration) block_check_required_declare(&source->ast, checker->errs);
    source_file_check_verbatim_exports(source, checker->errs);
    checker->defer_checks = true;
    checker->checking_file = source;
    block_typecheck(&source->ast, (BlockTypecheckCtx){ .ambient = source->is_declaration }, checker->errs);
    // (What a deferred check defers joins the end of the queue)
    for (u32 d = 0; d < checker->deferred_count; d++) {
        check_deferred(checker->deferred[d], checker->errs);
    }
    checker->deferred_count = 0;
    checker->defer_checks = false;
    checker->checking_file = nullptr;
#ifdef TTTT_SURVIVORS
    survivors_owner = 0;
#endif
    if (checker->graph_memo_on) graph_memo_flush(checker);
}

// The floor each checker keeps (today's default at 4 checkers): a table of every corpus and the source sweep's 169
// biggest projects at 4 checkers of 128 / 96 / 80 / 64 MB (measurements.md "The checkers' budget") put the corpora at
// +1.4 / +2.3 / +3.3% wall for -6.5 / -11 / -17% peak, the sweep's at +3 / +5 / +9% for -3 / -6 / -7%
#define CHECKER_MEMORY_FLOOR ((u64)96 << 20)
// A segment shorter than this many files doubles its checker's next budget (program_set_checker_memory_shared(): the
// CLI starts each of several checkers at 16 MB). Plan 81 stage 1 (measurements.md "Plan 81 stage 1") measured the
// defaults at 4 checkers against bun's on the 15 corpora and the source sweep's 174 biggest: 96 MB each (before)
// 4 / 14 bench corpora below bun's peak, 16 MB each without this 10 / 14 but the sweep's +28% wall (each recycle
// rebuilds a shared graph the segment's first file doesn't show, react-select's tests ~100 ms each), with it at 16
// files 9 / 14 and +13.5% (-22% peak), at 32 files 8 / 14 and +11%. Every corpus stays faster than bun, and 95% of
// the sweep's projects
#define CHECKER_MEMORY_BACKOFF_FILES 16
// What program_set_checker_memory_cap() still leaves each checker: below it the warm-ups dominate (vscode at one
// checker of 64 MB +4% wall; at 24 checkers of 64 MB +2.4% against 128, 1.75 GB less, measurements.md)
#define CHECKER_MEMORY_CAP_FLOOR ((u64)64 << 20)

// The source bytes a file counts for in its share's share_left (the default library's check in a few milliseconds)
static u64 share_cost(SourceFile const* source) {
    return source->is_stdlib ? 0 : cstr_size(&source->contents);
}

// Claims the index-th checked file for checker c: false if another checker has
static bool claim_file(Program* me, u32 at, u32 c) {
    u32 unclaimed = UINT32_MAX;
    if (!atomic_compare_exchange_strong(&my file_checkers[at], &unclaimed, c)) return false;
    SourceFile const* source = vec_source_file_ptr_at(&my check_files, at)[0];
    atomic_fetch_sub_explicit(&my share_left[my file_shares[at]], share_cost(source), memory_order_relaxed);
    return true;
}

// For checker c, done with its own files: the last unclaimed file of the running share with the most source left, or
// UINT32_MAX. Its tail is the part its checker would reach last, and taking files in a row keeps neighbours (which share
// types) together. Only a running share's: one no thread started yet (more checkers than threads) is all still to come
static u32 steal_file(Program* me, u32 c, u32 count) {
    for (;;) {
        u32 victim = UINT32_MAX;
        u64 most = 0;
        for (u32 v = 0; v < count; v++) {
            if (v == c || atomic_load_explicit(&my share_states[v], memory_order_acquire) != 1) continue;
            u64 left = atomic_load_explicit(&my share_left[v], memory_order_relaxed);
            if (left > most) most = left, victim = v;
        }
        if (victim == UINT32_MAX) return UINT32_MAX;
        bool any = false;
        for (u32 at = (u32)vec_source_file_ptr_size(&my check_files); at-- > 0;) {
            if (my file_shares[at] != victim) continue;
            u32 owner = atomic_load_explicit(&my file_checkers[at], memory_order_relaxed);
            // (Its own checker takes its files in order after its first: everything before one it took is taken)
            if (owner == victim && at != my share_first[victim]) break;
            if (owner != UINT32_MAX) continue;
            any = true;
            if (claim_file(me, at, c)) return at;
        }
        // (Its left bytes are the default library's, or another thief was quicker: look again)
        if (!any) atomic_store_explicit(&my share_left[victim], 0, memory_order_relaxed);
    }
}

// TTTT_STEAL_BLOCK=1 (the dup experiment): a thief takes a block of its victim's files in program order rather than
// the last file each time: from the middle of the victim's unclaimed files forward, until it meets a claimed one,
// then it picks a victim again. Neighbours (which share types) stay together, the victim keeps the first half
static u32 steal_block(Program* me, u32 c, u32 count, u32* cursor) {
    u32 total = (u32)vec_source_file_ptr_size(&my check_files);
    if (*cursor != UINT32_MAX) {
        u32 victim = my file_shares[*cursor];
        for (u32 at = *cursor + 1; at < total; at++) {
            if (my file_shares[at] != victim) continue;
            if (atomic_load_explicit(&my file_checkers[at], memory_order_relaxed) != UINT32_MAX) break;
            if (claim_file(me, at, c)) {
                *cursor = at;
                return at;
            }
            break;
        }
        *cursor = UINT32_MAX;
    }
    for (;;) {
        u32 victim = UINT32_MAX;
        u64 most = 0;
        for (u32 v = 0; v < count; v++) {
            if (v == c || atomic_load_explicit(&my share_states[v], memory_order_acquire) != 1) continue;
            u64 left = atomic_load_explicit(&my share_left[v], memory_order_relaxed);
            if (left > most) most = left, victim = v;
        }
        if (victim == UINT32_MAX) return UINT32_MAX;
        // (The victim's unclaimed files: the block starts at the one in the middle of their bytes)
        u64 unclaimed = 0;
        for (u32 at = 0; at < total; at++) {
            if (my file_shares[at] != victim || atomic_load_explicit(&my file_checkers[at], memory_order_relaxed) != UINT32_MAX) continue;
            unclaimed += share_cost(vec_source_file_ptr_at(&my check_files, at)[0]) + 1;
        }
        if (!unclaimed) {
            atomic_store_explicit(&my share_left[victim], 0, memory_order_relaxed);
            continue;
        }
        u64 seen = 0;
        for (u32 at = 0; at < total; at++) {
            if (my file_shares[at] != victim || atomic_load_explicit(&my file_checkers[at], memory_order_relaxed) != UINT32_MAX) continue;
            seen += share_cost(vec_source_file_ptr_at(&my check_files, at)[0]) + 1;
            if (seen * 2 < unclaimed) continue;
            if (claim_file(me, at, c)) {
                *cursor = at;
                return at;
            }
        }
    }
}

// Checks share c of the files, then helps the others: once its own files are all taken, it takes the last ones of the
// running share with the most left (steal_file()), so that the checkers finish together. Which checker checks a file
// then depends on timing, but what a file reports doesn't depend on the checker (plan 62; nor on its history, plan 67)
// With a checker memory budget the share is checked in segments: once the checker holds more than the budget after a
// file, the segment's files get their after-check pass and the next file starts in a fresh checker (tsgo keeps its
// checkers, but nothing tttt reports depends on a checker's history, plan 62: what bun does, plan 67). A fresh checker
// builds the global and library types its files use again: a few milliseconds on vscode, ~100 ms where the files share
// one big type graph (type-fest, drizzle). Nothing cheap predicts that cost (what the segment's first file held
// doesn't: those are the default library's declarations), so the budget alone decides
// --shared-graph on threads: the graph-wide recycle (plan 86 4e item 3). When what the checkers hold together passes
// the budget, every worker stops at its next file boundary, where nothing of its is in flight; the last to arrive drops
// every checker (their deferred bodies go with their holds), the retired ones and the graph, makes a fresh graph and
// checkers and lets the others go on. A worker that runs out of files leaves the barrier's
// count instead (graph_epoch_leave()), so finished shares never hold it up. Today's per-checker recycle, for the
// whole graph at once
enum { GRAPH_BUDGET_MB = 800 };

static void graph_epoch_rebuild(Program* me, u32 count) {
    ErrorCtx* errs[64];
    u32 recycles[64];
    u64 peaks[64];
    check(count <= 64);
    for (u32 c = 0; c < count; c++) {
        errs[c] = my checkers[c]->errs;
        recycles[c] = my checkers[c]->recycles;
        peaks[c] = my checkers[c]->peak_memory;
    }
    for (u32 c = 0; c < count; c++) {
        checker_drop(my checkers[c]);
        my checkers[c] = nullptr;
    }
    for (u32 i = 0; i < my retired_count; i++) checker_drop(my retired[i]);
    my retired_count = 0;
    graph_drop(my graph);
    my graph = nullptr;
    atomic_store(&my type_directory->reader_count, 0);
    Checker* first = checker_new(me);
    my graph = graph_new(first);
    graph_set_concurrent(my graph, (my threads ? my threads : 1) > 1);
    my checkers[0] = first;
    for (u32 c = 1; c < count; c++) my checkers[c] = checker_new(me);
    for (u32 c = 0; c < count; c++) {
        my checkers[c]->errs = errs[c];
        my checkers[c]->recycles = recycles[c] + 1;
        my checkers[c]->peak_memory = peaks[c];
        atomic_store(&my graph_held[c], 0);
    }
    my checker = first;
    my graph_recycles++;
}

// --shared-graph: a worker at a file boundary waits while another leads (graph_leader). After a graph-wide recycle
// every worker starts its next file at the same instant on an empty graph, and nothing in flight is shared (plan 86
// stage 4f), so all of them compute the same cold subgraph side by side: dokploy's tRPC router types, ~420 ms in one
// fresh checker, took ~850 ms in each of 12 workers at once (the 3x slow runs). The leader warms the graph alone and
// the others find its words published. It holds nothing in flight while it waits, nor does the leader wait for
// anyone, so no wait cycle can form; and what a file reports doesn't depend on the schedule
static void graph_lead_wait(Program* me, u32 c) {
    for (int leader; (leader = atomic_load_explicit(&my graph_leader, memory_order_acquire)) >= 0 && (u32)leader != c;) {
        struct timespec ts = { 0, 100000 };
        nanosleep(&ts, nullptr);
    }
}

static void graph_lead_done(Program* me, u32 c) {
    int leader = (int)c;
    atomic_compare_exchange_strong(&my graph_leader, &leader, -1);
}

// After a file: whether the graph is due, and if so the barrier. Returns the share's checker to go on with
static bool graph_epoch_due(Program* me, Checker* checker, u32 c, u32 count) {
    if (my graph_budget == UINT64_MAX) return false;
    atomic_store_explicit(&my graph_held[c], checker_memory(checker), memory_order_relaxed);
    if (atomic_load_explicit(&my graph_epoch_due, memory_order_relaxed)) return true;
    u64 sum = 0;
    for (u32 i = 0; i < count; i++) sum += atomic_load_explicit(&my graph_held[i], memory_order_relaxed);
    if (sum <= my graph_budget) return false;
    bool expected = false;
    if (atomic_compare_exchange_strong(&my graph_epoch_due, &expected, true) && (debug_env.file_times || getenv("TTTT_GRAPH_STATS"))) {
        eprintln("graph: recycle due, the checkers hold ", sum >> 20, " MB (budget ", my graph_budget >> 20, ")");
    }
    return true;
}

static Checker* graph_epoch_arrive(Program* me, Checker* checker, u32 c, u32 count) {
    graph_memo_flush(checker);
    checker_make_current(nullptr);
    pthread_mutex_lock(&my graph_epoch_lock);
    u32 epoch = my graph_epoch;
    if (++my graph_arrived == my graph_active) {
        graph_epoch_rebuild(me, count);
        if (!getenv("TTTT_GRAPH_NO_LEAD")) atomic_store(&my graph_leader, (int)c);
        my graph_arrived = 0;
        atomic_store(&my graph_epoch_due, false);
        my graph_epoch++;
        pthread_cond_broadcast(&my graph_epoch_cond);
    } else {
        while (my graph_epoch == epoch) pthread_cond_wait(&my graph_epoch_cond, &my graph_epoch_lock);
    }
    pthread_mutex_unlock(&my graph_epoch_lock);
    checker = my checkers[c];
    checker_make_current(checker);
    ty_init_global_types(checker);
    return checker;
}

// A worker done with its share: out of the barrier's count (the last of the others to arrive may be waiting for it)
static void graph_epoch_leave(Program* me, u32 count) {
    if (my graph_budget == UINT64_MAX) return;
    pthread_mutex_lock(&my graph_epoch_lock);
    my graph_active--;
    if (my graph_active && my graph_arrived == my graph_active && atomic_load(&my graph_epoch_due)) {
        graph_epoch_rebuild(me, count);
        my graph_arrived = 0;
        atomic_store(&my graph_epoch_due, false);
        my graph_epoch++;
        pthread_cond_broadcast(&my graph_epoch_cond);
    }
    pthread_mutex_unlock(&my graph_epoch_lock);
}

static void graph_epoch_join(Program* me) {
    if (my graph_budget == UINT64_MAX) return;
    pthread_mutex_lock(&my graph_epoch_lock);
    my graph_active++;
    pthread_mutex_unlock(&my graph_epoch_lock);
}

static void check_share(Program* me, u32 c, u32 count) {
    struct timespec start, end;
    timespec_get(&start, TIME_UTC);
    atomic_store_explicit(&my share_states[c], 1, memory_order_release);
    graph_epoch_join(me);
    Checker* checker = my checkers[c];
    checker_make_current(checker);
    ty_init_global_types(checker);
    u32 total = (u32)vec_source_file_ptr_size(&my check_files);
    // The files of the current segment, in the order it checked them
    u32* segment = c_new_n(u32, total ? total : 1);
    u32 segment_count = 0;
    bool recycle_due = false;
    // What the checker held after its segment's first file: its warm-up (the libraries' and the shared graph's types).
    // A recycle only pays when the checker has grown well past it (dokploy's trpc graph is ~150 MB, over a 128 MB
    // budget after any one file: recycled after nearly every file it was 20x slower)
    u64 segment_base = 0;
    double segment_ms = 0, segment_max_ms = 0; // The segment's check time and its slowest file's (TTTT_FILE_TIMES)
    u64 budget = my checker_start_budget;
    u32 stolen = 0;
    isize scanned = 0; // Of checker->errs->errors, for declaration_walk_moot()
    bool moot_watch = my declaration_errs && !my emit_declarations && program_declaration_diagnostics_gated(me);
    bool steal = my steal_files && count > 1;
    bool steal_blocks = getenv("TTTT_STEAL_BLOCK") != nullptr;
    u32 steal_cursor = UINT32_MAX;
    for (u32 next = 0, first = my share_first[c];;) {
        u32 at = UINT32_MAX;
        if (first != UINT32_MAX && claim_file(me, first, c)) at = first;
        first = UINT32_MAX;
        for (; at == UINT32_MAX && next < total; next++) {
            if (my file_shares[next] == c && claim_file(me, next, c)) {
                at = next++;
                break;
            }
        }
        if (at == UINT32_MAX && steal) {
            at = steal_blocks ? steal_block(me, c, count, &steal_cursor) : steal_file(me, c, count);
            // (With a memory budget the first one starts in a fresh checker: its own files' types are little use in
            // another's region, and kept they'd stack a second warm state on the first. The source sweep's 285 biggest
            // at the defaults: wall -7.1%, peak +3.8% kept; -5.2%, +0.4% dropped)
            if (at != UINT32_MAX && !stolen++ && my checker_budget != UINT64_MAX && segment_count &&
                checker_memory(checker) > budget / 2
            ) {
                recycle_due = true;
            }
        }
        if (at == UINT32_MAX) break;
        if (my graph) graph_lead_wait(me, c);
        graph_sched_point(false);
        SourceFile* source = vec_source_file_ptr_at(&my check_files, at)[0];
        // (A failed materialization means a run without deferral: what's left needn't be checked, plan 78)
        if (atomic_load_explicit(&my defer_failed, memory_order_relaxed)) continue;
        // (Not after the share's last file: due only once there is another)
        if (recycle_due) {
            check_segment_after(me, checker, c, segment, segment_count);
            checker = checker_recycled(me, checker, c);
            source_texts_release();
            segment_count = 0;
            segment_base = 0;
            segment_ms = segment_max_ms = 0;
            recycle_due = false;
        }
        segment[segment_count++] = at;
        struct timespec file_start;
        struct timespec file_cpu_start;
        if (debug_env.file_times) {
            timespec_get(&file_start, TIME_UTC);
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &file_cpu_start);
        }
        if (debug_env.dup_census) dup_census_file(c, at, share_cost(source));
        struct timespec rb_cpu;
        u32 rb_limits = checker->instantiation_limit_hits, rb_types = checker_type_count(checker);
        u64 rb_cuts = checker->deep_cuts;
        if (__builtin_expect(debug_env.rollback_stats, 0)) {
            rollback_file = cstr_str(&source->canonical_path);
            rollback_cycle_count = 0;
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &rb_cpu);
        }
        check_file(me, checker, source, at);
        if (__builtin_expect(debug_env.rollback_stats, 0)) {
            // (Its line: RB order checker cpu_ns types limit_hits deep_cuts cycles path keys...)
            struct timespec rb_end;
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &rb_end);
            char keys[64 * 20];
            int kn = 0;
            keys[0] = 0;
            for (u32 i = 0; i < rollback_cycle_count && i < 64; i++) kn += snprintf(keys + kn, sizeof keys - (size_t)kn, " %p", rollback_cycles[i]);
            fprintf(stderr, "RB %u %u %lld %u %u %llu %u %s%s\n", source->program_order, c,
                (long long)(rb_end.tv_sec - rb_cpu.tv_sec) * 1000000000ll + (rb_end.tv_nsec - rb_cpu.tv_nsec),
                checker_type_count(checker) - rb_types, checker->instantiation_limit_hits - rb_limits,
                (unsigned long long)(checker->deep_cuts - rb_cuts), rollback_cycle_count, rollback_file, keys);
            rollback_file = nullptr;
        }
        if (my graph) graph_lead_done(me, c);
        // (Plan 86 stage 4f: a worker that kept a tainted value of a word another worker published meanwhile goes by its
        // Overlay first, on every fast path. Its file is done: it hands over to a fresh worker, whose history starts
        // here, like a recycle's)
        if (checker->graph_shadowed && graph_can_retire(checker->graph)) {
            check_segment_after(me, checker, c, segment, segment_count);
            segment_count = 0;
            if (getenv("TTTT_GRAPH_STATS")) eprintln("graph: shadowed worker ", (u32)checker->graph_worker, " retired after ", source->canonical_path);
            checker = graph_fresh_worker(me, checker);
            my checkers[c] = checker;
        }
        if (debug_env.file_times) {
            struct timespec file_end;
            timespec_get(&file_end, TIME_UTC);
            double file_ms = (double)(file_end.tv_sec - file_start.tv_sec) * 1e3 + (double)(file_end.tv_nsec - file_start.tv_nsec) / 1e6;
            segment_ms += file_ms;
            if (file_ms > segment_max_ms) segment_max_ms = file_ms;
            double end_ms = (double)(file_end.tv_sec - my check_start.tv_sec) * 1e3 + (double)(file_end.tv_nsec - my check_start.tv_nsec) / 1e6;
            // (Its thread's CPU time too: on a loaded machine the wall includes waiting for a core)
            struct timespec file_cpu_end;
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &file_cpu_end);
            double cpu_ms = (double)(file_cpu_end.tv_sec - file_cpu_start.tv_sec) * 1e3 + (double)(file_cpu_end.tv_nsec - file_cpu_start.tv_nsec) / 1e6;
            char more[160];
            snprintf(more, sizeof more, ", cpu %.1f ms, holds %.1f MB, done at %.1f ms, %u types, %llu bytes", cpu_ms,
                (double)checker_memory(checker) / (1 << 20), end_ms, checker_type_count(checker) - rb_types,
                (unsigned long long)share_cost(source));
            print_file_time(my file_shares[at] == c ? "check" : "check taken", file_start, source, c, more);
        }
        for (; moot_watch && scanned < vec_error_size(&checker->errs->errors); scanned++) {
            if (!error_surely_reported(&checker->errs->errors.data[scanned])) continue;
            atomic_store_explicit(&my declaration_diagnostics_moot, true, memory_order_relaxed);
            moot_watch = false;
        }
        // (--shared-graph on threads: the graph-wide recycle, at this file boundary. The segment's after-check pass
        // first, with the types it needs)
        if (graph_epoch_due(me, checker, c, count)) {
            check_segment_after(me, checker, c, segment, segment_count);
            segment_count = 0;
            checker = graph_epoch_arrive(me, checker, c, count);
            source_texts_release();
        }
        if (my pool_held && pool_recycle_due(me, checker, c, count)) {
            recycle_due = true;
            continue;
        }
        if (my checker_budget == UINT64_MAX) continue;
        u64 held = checker_memory(checker);
        if (held > checker->peak_memory) checker->peak_memory = held;
        // (Not the costliest file the share starts with: what it holds is mostly its own types, and twice that kept
        // excalidraw's first checker at 47 MB)
        if (!segment_base && at != my share_first[c]) segment_base = held;
        // (A budget of 0 recycles after every file, warm-up or not: kdiff's fresh checker per file)
        if (budget && (held <= budget || held <= 2 * segment_base)) continue;
        // (A segment this short rebuilt a shared warm state the first file didn't show: the next may hold twice as
        // much, up to the full budget)
        if (segment_count < CHECKER_MEMORY_BACKOFF_FILES) budget = 2 * budget < my checker_budget ? 2 * budget : my checker_budget;
        if (debug_env.file_times) {
            fprintf(stderr, "file time: recycle checker %u after %u files, held %llu MB, warm-up %llu MB, budget %llu of %llu MB, "
                "%.0f ms, slowest file %.0f\n", c, segment_count, (unsigned long long)(held >> 20),
                (unsigned long long)(segment_base >> 20), (unsigned long long)(budget >> 20),
                (unsigned long long)(my checker_budget >> 20), segment_ms, segment_max_ms);
        }
        recycle_due = true;
    }
    if (my graph) graph_lead_done(me, c);
    atomic_store_explicit(&my share_states[c], 2, memory_order_release);
    check_segment_after(me, checker, c, segment, segment_count);
    free(segment);
    if (debug_env.file_times) {
        // (Its idle tail: the last checker's finish minus this one's, scripts/perfshape.sh)
        struct timespec end;
        timespec_get(&end, TIME_UTC);
        fprintf(stderr, "file time: checker %u finished at %.1f ms\n", c,
            (double)(end.tv_sec - my check_start.tv_sec) * 1e3 + (double)(end.tv_nsec - my check_start.tv_nsec) / 1e6);
    }
    u64 held = checker_memory(checker);
    if (held > checker->peak_memory) checker->peak_memory = held;
    // Declaration emit's diagnostics don't depend on skipTypeChecking: also for files noCheck or `@ts-nocheck` leave
    // unchecked
    u32 index = 0;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        // (A JavaScript file's only when it is emitted: its declaration diagnostics aren't reported, plan 64 stage 6.
        // Declarations only, every file's walk is here, a checked JavaScript file's as after its check)
        bool checked = source_file_is_checked(source);
        if (source->is_stdlib || (checked && !my declarations_only) ||
            (source->is_js && !checked && !my emit_declarations)) continue;
        if (index++ % count != c) continue;
        if (my declaration_errs && !declaration_walk_moot(me)) {
#ifdef TTTT_SURVIVORS
            survivors_owner = survivors_file_start(checker, UINT32_MAX);
#endif
            declaration_diagnostics_check(source, &my declaration_errs[c], my emit_declarations);
#ifdef TTTT_SURVIVORS
            survivors_owner = 0;
#endif
        }
    }
#ifdef TTTT_SURVIVORS
    if (getenv("TTTT_FILE_CENSUS")) survivors_file_census(checker, "end");
#endif
    // (--shared-graph: what it computed goes to the checkers after it, if there are any)
    if (checker->graph && getenv("TTTT_DEBUG_UNSHARED")) graph_check_unshared(checker->graph, checker);
    if (checker->graph) graph_memo_flush(checker);
    checker_make_current(nullptr);
    graph_epoch_leave(me, count);
    timespec_get(&end, TIME_UTC);
    checker->check_ms = (double)(end.tv_sec - start.tv_sec) * 1e3 + (double)(end.tv_nsec - start.tv_nsec) / 1e6;
    checker->files_taken = stolen;
}

// Deals the checked files to the checkers: which checker a file is dealt to depends only on the file list (a checker
// done with its share takes the rest of a running one's, check_share(): the output doesn't depend on the checker). Neighbours in program order (a file, then what it imports) share most of their
// types, and each checker instantiates what its files use again: the files are cut into 2 runs of about equal size
// per checker, dealt round robin. One run each makes the least duplication (drizzle's peak 1688 -> 1277 MB at 4
// checkers) but balances worse (valibot's test files sit together, type-fest's biggest file takes its neighbours);
// two keep most of it (1337 MB) at round robin's wall times or better. Bytes are the cost proxy, except for the default
// library's files: megabytes of declarations (lib.dom.d.ts) that check in a few milliseconds would take most runs and
// leave a small project's own files to one checker. They are dealt round robin instead
static void deal_files(Program* me) {
    enum { RUNS_PER_CHECKER = 2 };
    vec_source_file_ptr_clear(&my check_files);
    // (Debugging order dependences: TTTT_CHECK_ONLY=substring[,substring...] checks only the files whose path has one,
    // in a fresh checker, in program order, the rest of the program still there)
    char const* only = getenv("TTTT_CHECK_ONLY");
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        if (!source_file_is_checked(*i) || my declarations_only) continue;
        if (only && !(*i)->is_stdlib) {
            bool found = false;
            for (char const* part = only; *part && !found; ) {
                char const* comma = strchr(part, ',');
                size_t len = comma ? (size_t)(comma - part) : strlen(part);
                found = len && csview_find_sv(cstr_sv(&(*i)->canonical_path), c_sv_2(part, (isize_t)len)) != c_NPOS;
                part += len + (comma ? 1 : 0);
            }
            if (!found) continue;
        }
        vec_source_file_ptr_push(&my check_files, *i);
    }
    u32 total = (u32)vec_source_file_ptr_size(&my check_files);
    u64 bytes = 0;
    for_each (i, vec_source_file_ptr, &my check_files) {
        if (!(*i)->is_stdlib) bytes += cstr_size(&(*i)->contents);
    }
    free(my file_shares);
    my file_shares = c_new_n(u32, total ? total : 1);
    free(my file_checkers);
    my file_checkers = c_new_n(_Atomic u32, total ? total : 1);
    for (u32 at = 0; at < total; at++) atomic_init(&my file_checkers[at], UINT32_MAX);
    u64 runs = (u64)my checker_count * RUNS_PER_CHECKER;
    u32 index = 0;
    u32 libs = 0;
    u64 before = 0;
    for_each (i, vec_source_file_ptr, &my check_files) {
        if ((*i)->is_stdlib) {
            my file_shares[index++] = libs++ % my checker_count;
            continue;
        }
        u64 size = cstr_size(&(*i)->contents);
        // (A file belongs to the run its middle byte falls in)
        my file_shares[index++] = (u32)((before + size / 2) * runs / (bytes ? bytes : 1) % my checker_count);
        before += size;
    }
    // (TTTT_DUP_DEAL=PATH: the experiment's deal, a checker per checked file, one per line (ty/dupcensus.c's model))
    char const* deal_path = getenv("TTTT_DUP_DEAL");
    FILE* deal = deal_path ? fopen(deal_path, "r") : nullptr;
    if (deal) {
        for (u32 at = 0; at < total; at++) {
            unsigned share;
            panic_if(fscanf(deal, "%u", &share) != 1 || share >= my checker_count, "TTTT_DUP_DEAL: a checker below ", my checker_count, " per file");
            my file_shares[at] = share;
        }
        fclose(deal);
    }
    for (u32 c = 0; c < my checker_count; c++) {
        atomic_init(&my share_states[c], 0);
        atomic_init(&my share_left[c], 0);
    }
    // Each checker starts with its share's costliest file (by size), then the rest in order: the biggest file is a
    // run's critical path when it would come late (TypeScript6's checker.ts, 195 of a 290 ms check at 4 checkers:
    // -6% wall). Not at one checker, where the order only moves the warm-up. TTTT_NO_COSTLIEST_FIRST=1 turns it off
    bool costliest_first = my checker_count > 1 && !getenv("TTTT_NO_COSTLIEST_FIRST");
    u64* first_cost = c_new_n(u64, my checker_count);
    for (u32 c = 0; c < my checker_count; c++) my share_first[c] = UINT32_MAX, first_cost[c] = 0;
    for (u32 at = 0; at < total; at++) {
        SourceFile const* source = vec_source_file_ptr_at(&my check_files, at)[0];
        u32 c = my file_shares[at];
        u64 cost = share_cost(source);
        atomic_fetch_add_explicit(&my share_left[c], cost, memory_order_relaxed);
        if (costliest_first && cost > first_cost[c]) first_cost[c] = cost, my share_first[c] = at;
    }
    free(first_cost);
    // (TTTT_DUP_GRAPH=PATH, ty/dupcensus.c's offline model: per checked file its index, source bytes, whether it is the
    // default library's, its path and the checked files it imports)
    char const* graph_path = getenv("TTTT_DUP_GRAPH");
    FILE* graph = graph_path ? fopen(graph_path, "w") : nullptr;
    if (graph) {
        u32* check_index = c_new_n(u32, vec_source_file_ptr_size(&my source_files_ordered) + 1);
        for (isize i = 0; i < vec_source_file_ptr_size(&my source_files_ordered); i++) check_index[i] = UINT32_MAX;
        for (u32 at = 0; at < total; at++) check_index[vec_source_file_ptr_at(&my check_files, at)[0]->program_order] = at;
        for (u32 at = 0; at < total; at++) {
            SourceFile const* source = vec_source_file_ptr_at(&my check_files, at)[0];
            fprintf(graph, "%u %llu %d %s", at, (unsigned long long)share_cost(source), (int)source->is_stdlib, cstr_str(&source->canonical_path));
            for (u32 r = 0; r < source->module_ref_count; r++) {
                SourceFile const* file = source->module_refs[r]->file;
                if (file && file->program_order < vec_source_file_ptr_size(&my source_files_ordered) && check_index[file->program_order] != UINT32_MAX) {
                    fprintf(graph, " %u", check_index[file->program_order]);
                }
            }
            fprintf(graph, "\n");
        }
        free(check_index);
        fclose(graph);
    }
}

u64 program_checked_source_bytes(Program const* me) {
    u64 bytes = 0;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        if (source_file_is_checked(*i) && !(*i)->is_stdlib) bytes += cstr_size(&(*i)->contents);
    }
    return bytes;
}

// program_set_checkers(0): one checker per thread, but not more than one per CHECKER_BYTES of checked source (the
// default library's files aside): each checker builds the global and library types it uses again, which a small
// project's few files don't win back. Past the cap (the physical cores), one per SMT_CHECKER_BYTES. Never more than
// checked bytes / the largest checked implementation file's bytes, rounded up (below). A declaration file doesn't count
// as the largest: checking one walks its declarations, whose members resolve lazily, so its bytes say nothing of its
// time (the bun breadth sweep, 2026-10-23: lucide-react's 2.2 MB .d.ts checks in 2 ms, @octokit/openapi-types' 4.8 MB
// in 0-67 ms, against termdom's 1.2 MB dom.ts in 615 ms), and capping by one left cores idle: formatjs/docs at K4,
// cheerio and octokit at K2)
static u64 largest_checked_source_bytes(Program const* me) {
    u64 largest = 0;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile const* source = *i;
        if (source_file_is_checked(source) && !source->is_stdlib && !source->is_declaration &&
            (u64)cstr_size(&source->contents) > largest
        ) {
            largest = (u64)cstr_size(&source->contents);
        }
    }
    return largest;
}

// The largest-file cap below applies only to a file this big: below it the bytes mislead (type-fest's cost is in tiny
// type-level test files, a 1.4 KB one takes 180 ms: its largest checked file is 0.59 MB, so ceil(1.92/0.59) = K4 was
// 0.28 s against 0.18 s at K12 on the laptop; tanstack-router's is 0.24 MB, react's 7 KB, nest's 16 KB, xstate's 0.11 MB;
// TypeScript6's checker.ts is 3.15 MB)
#define BIG_FILE_BYTES (1u << 20)

static u32 auto_checker_count(Program const* me) {
    u64 bytes = program_checked_source_bytes(me);
    u64 count = bytes / CHECKER_BYTES;
    u32 threads = my threads ? my threads : 1;
    if (my checker_cap && my checker_cap < threads) {
        u64 smt = bytes / SMT_CHECKER_BYTES;
        threads = smt > threads ? threads : smt > my checker_cap ? (u32)smt : my checker_cap;
    }
    u32 result = count < 1 ? 1 : count > threads ? threads : (u32)count;
    // and not more than the largest checked file's share of the source (rounded up): a program bounded by
    // one file (checker.ts is 2.9 MB of TypeScript6's 9.7 checked) has its critical path in that file whatever the
    // count, and every checker beyond the share only adds CPU and memory. The 12-core laptop confirms: TypeScript6 at
    // --checkers 4 199.7 ms best against 208.9 at the default 8-12, with 32% less CPU (here K4 against K8 at one
    // thread: 5% fewer instructions, longest checker +3%). Only a largest file of at least BIG_FILE_BYTES caps (type-fest, tanstack-router and react were capped by it before: K4 / K8 / K11, the laptop's type-fest 0.28 s against 0.18 s at K12). Longest checker's per-file sum at --threads 1 (this box): type-fest K4 337 ms, K8 433, K12 310 (noisy); tanstack K8 139, K12 110; react K11 59, K12 64; TypeScript6 K4 205, K8 192 (CPU total 508 against 534). Of the other corpora only TypeScript6 is capped
    u64 largest = largest_checked_source_bytes(me);
    if (largest >= BIG_FILE_BYTES) {
        u64 share = (bytes + largest - 1) / largest;
        if (share < 1) share = 1;
        if (result > share) result = (u32)share;
    }
    return result;
}

static void check_shares_run(CheckShares* me) {
    graph_sched_enter();
    for (u32 c; (c = atomic_fetch_add(&my next, 1)) < my count;) check_share(my program, c, my count);
    graph_sched_leave();
}

static void* check_shares_thread(void* shares) {
    set_compiler_options(&((CheckShares*)shares)->program->options);
    check_shares_run(shares);
    clear_compiler_options();
    symbol_chain_release_thread();
    return nullptr;
}

// Plan 78's materialize-ahead: the threads the checkers leave idle (fewer checkers than threads: `--checkers 1`, a
// small program) parse and walk the deferred bodies of the files in check order, as fast as they can. Without them a
// lone checker does all of it itself (vscode at one checker and 4 threads: 12.6 s against 11.6 eager, with them 11.4).
// Staying a window of files ahead of the checkers instead (32 files) took 14.2 s: the helpers' walks then run next to
// the checker's the whole time, evicting its caches. What they hold for the checkers is bounded in body source bytes
// (ahead_limit, ~14 bytes of memory each): past it they wait until the checkers took most of it, so that they work
// in bursts (vscode K1 T4: 12.0 s at 4 MB, 11.9 unbounded but holding every body; 12.4 polling every 200 us rather
// than woken by the checker)
static void* materialize_ahead_thread(void* arg) {
    Program* me = arg;
    set_compiler_options(&my options);
    u32 total = (u32)vec_source_file_ptr_size(&my check_files);
    for (u32 i; (i = atomic_fetch_add(&my ahead_next, 1)) < total;) {
        SourceFile* file = vec_source_file_ptr_at(&my check_files, i)[0];
        if (!file->deferred_bodies) continue;
        if (atomic_load_explicit(&my defer_failed, memory_order_relaxed)) break;
        if (atomic_load_explicit(&ahead_bytes, memory_order_relaxed) > ahead_limit) {
            defer_stat_add(DEFER_STAT_AHEAD_WAITS, 1);
            pthread_mutex_lock(&ahead_lock);
            while (atomic_load_explicit(&ahead_bytes, memory_order_relaxed) > ahead_resume &&
                atomic_load_explicit(&my ahead_next, memory_order_relaxed) < total
            ) {
                pthread_cond_wait(&ahead_taken, &ahead_lock);
            }
            pthread_mutex_unlock(&ahead_lock);
            if (atomic_load_explicit(&my ahead_next, memory_order_relaxed) >= total) break;
        }
        deferred_bodies_acquire(me, file, nullptr);
    }
    clear_compiler_options();
    scope_release_thread();
    return nullptr;
}

// TTTT_GRAPH_RECHECK=1 (or =graph, =private; --shared-graph only, plan 86 4f's gates): after the workers join, every
// checked file is checked again, in program order, by one fresh checker, and each file's diagnostics are compared with
// what the workers reported for it (both sorted and deduplicated like the CLI's, printed `--pretty false`). `graph`
// (the default) adopts the fresh checker into the finished graph, so it reads what the workers published: a file whose
// errors differ was checked in a history the published graph doesn't give (a poisoned relation
// cache, plan 86 4e item 1). `private` gives it no graph: a K1 check in the same process, which every file must
// match. On stderr: `recheck: N of M files differ`, then the first differing file in program order with both lists
// (TTTT_GRAPH_RECHECK_ALL=1: every differing file). The output itself is the workers'. Debug only: it doubles the work
#ifdef TTTT_GRAPH_FINGERPRINT
// The fingerprint oracle's record (graph_debug.c): every file checked by one private checker first, in program order,
// its results' fingerprints recorded; its errors are dropped and the checker is kept to the end (never dropped: what it
// materialized stays where it is, so the AST addresses the fingerprints hash are the workers' too)
static void graph_fingerprint_record(Program* me) {
    Checker* saved = current_checker;
    graph_fp_start(true);
    Checker* k1 = checker_new(me);
    ErrorCtx errs = { .phase = ERROR_PHASE_TYPECHECK, .parent = my errs };
    k1->errs = &errs;
    checker_make_current(k1);
    ty_init_global_types(k1);
    // (deal_files()'s list, made later)
    u32 at = 0;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        if (source_file_is_checked(*i) && !my declarations_only) check_file(me, k1, *i, at++);
    }
    graph_fp_record_tables(k1);
    error_ctx_drop(&errs);
    k1->errs = my errs;
    checker_make_current(saved);
    graph_fp_start(false);
}
#endif

static char const* recheck_file_name(SourceFile const* source) {
    return cstr_str(&source->canonical_path);
}

static void recheck_texts(VecError* errors, cstr* texts, u32 slots) {
    error_vec_sort_and_dedupe(errors, source_file_message_path);
    for_each (e, vec_error, errors) {
        SourceFile const* source = span_valid(e->span) ? span_source(e->span) : nullptr;
        u32 at = source && source->id < slots - 1 ? source->id : slots - 1;
        cstr line = cstr_init();
        error_format_plain(&line, e, "\n", recheck_file_name);
        cstr_append_s(&texts[at], line);
        cstr_drop(&line);
    }
}

static void graph_recheck(Program* me, ErrorCtx const* checker_errs, u32 count) {
    char const* mode = getenv("TTTT_GRAPH_RECHECK");
    bool private_ = !strcmp(mode, "private");
    TyGraph* graph = my graph;
    if (private_) my graph = nullptr;
    Checker* fresh = checker_new(me);
    my graph = graph;
    ErrorCtx errs = { .phase = ERROR_PHASE_TYPECHECK, .parent = my errs };
    fresh->errs = &errs;
    checker_make_current(fresh);
    ty_init_global_types(fresh);
    u32 files = (u32)vec_source_file_ptr_size(&my check_files);
    for (u32 at = 0; at < files; at++) {
        SourceFile* source = vec_source_file_ptr_at(&my check_files, at)[0];
        check_file(me, fresh, source, at);
    }
    for (u32 at = 0; at < files; at++) {
        SourceFile* source = vec_source_file_ptr_at(&my check_files, at)[0];
        if (!source->is_declaration && !source->unchecked) unused_report(source, fresh->errs);
    }
    checker_make_current(nullptr);
    VecError first = vec_error_init(), again = vec_error_init();
    u32 slots = 1;
    for (u32 c = 0; c < count; c++) {
        for_each (e, vec_error, &checker_errs[c].errors) {
            vec_error_push(&first, error_clone(*e));
            if (span_valid(e->span) && span_source(e->span)->id + 2 > slots) slots = span_source(e->span)->id + 2;
        }
    }
    for_each (e, vec_error, &errs.errors) {
        vec_error_push(&again, error_clone(*e));
        if (span_valid(e->span) && span_source(e->span)->id + 2 > slots) slots = span_source(e->span)->id + 2;
    }
    cstr* first_texts = c_new_n(cstr, slots);
    cstr* again_texts = c_new_n(cstr, slots);
    for (u32 i = 0; i < slots; i++) first_texts[i] = cstr_init(), again_texts[i] = cstr_init();
    recheck_texts(&first, first_texts, slots);
    recheck_texts(&again, again_texts, slots);
    bool all = getenv("TTTT_GRAPH_RECHECK_ALL") != nullptr;
    u32 differ = 0, shown = 0;
    // (Program order, then the diagnostics of files not checked and those without a file)
    for (u32 at = 0; at <= files; at++) {
        u32 slot = slots - 1;
        if (at < files) {
            u32 id = vec_source_file_ptr_at(&my check_files, at)[0]->id;
            if (id >= slots - 1) continue;
            slot = id;
        }
        if (cstr_equals_sv(&first_texts[slot], cstr_sv(&again_texts[slot]))) continue;
        differ++;
        if (shown++ && !all) continue;
        eprintln("recheck: differs: ", at < files ? cstr_str(&vec_source_file_ptr_at(&my check_files, at)[0]->canonical_path) : "(other files)");
        eprint("recheck: --- the workers' (", count, " checkers):\n", first_texts[slot]);
        eprint("recheck: +++ the recheck's (", private_ ? "private" : "in the graph", "):\n", again_texts[slot]);
    }
    eprintln("recheck: ", differ, " of ", files, " files differ (", private_ ? "private" : "in the graph", ")");
    for (u32 i = 0; i < slots; i++) cstr_drop(&first_texts[i]), cstr_drop(&again_texts[i]);
    free(first_texts);
    free(again_texts);
    vec_error_drop(&first);
    vec_error_drop(&again);
    error_ctx_drop(&errs);
    fresh->errs = my errs;
    // (In the graph it may hold what others point to: kept to the end like a shadowed worker's retired checker)
    if (private_) {
        checker_drop(fresh);
    } else {
        if (my retired_count == my retired_cap) {
            my retired_cap = my retired_cap ? my retired_cap * 2 : 8;
            my retired = realloc(my retired, sizeof(Checker*) * my retired_cap);
            panic_if(!my retired, "out of memory");
        }
        my retired[my retired_count++] = fresh;
    }
}

void program_typecheck(Program* me) {
    CHANGE_PHASE(PROGRAM_PHASE_NAMES_RESOLVED, PROGRAM_PHASE_TYPECHECKING);
    struct timespec typecheck_start;
    timespec_get(&typecheck_start, TIME_UTC);
    my errs->phase = ERROR_PHASE_TYPECHECK;
    set_compiler_options(&my options);
    program_make_current(me);

    scope_report_missing_prefixes(my binder);
    if (!my global_diagnostics_reported) program_report_missing_global_types(me);
    // Each checker reports into its own ErrorCtx; nothing may write to the program's or make symbols meanwhile (they
    // run on threads of their own)
    u32 count = my requested_checkers ? my requested_checkers : auto_checker_count(me);
    my checkers = c_new_n(Checker*, count);
    my checker_count = count;
    // (Split between the checkers, but at least CHECKER_MEMORY_FLOOR each when there's that much: a fresh checker's
    // warm-ups cost more than the memory saves below it, vscode at 4 checkers of 64 MB +3.6% wall, of 96 +1.7%)
    u64 floor = my checker_memory < CHECKER_MEMORY_FLOOR ? my checker_memory : CHECKER_MEMORY_FLOOR;
    my checker_budget = my checker_memory == UINT64_MAX ? UINT64_MAX : (my checker_memory / count > floor ? my checker_memory / count : floor);
    if (my checker_budget != UINT64_MAX && my checker_memory_cap != UINT64_MAX) {
        u64 capped = my checker_memory_cap / count > CHECKER_MEMORY_CAP_FLOOR ? my checker_memory_cap / count : CHECKER_MEMORY_CAP_FLOOR;
        if (capped < my checker_budget) my checker_budget = capped;
    }
    my checker_start_budget = count > 1 && my checker_memory_shared < my checker_budget ? my checker_memory_shared : my checker_budget;
    ErrorCtx* checker_errs = c_new_n(ErrorCtx, count);
    // --shared-graph (plan 86 stage 4, ty/graph.h): one checker after another, never recycled, sharing one graph
#ifdef TTTT_GRAPH_FINGERPRINT
    if (my shared_graph && !my graph && getenv("TTTT_GRAPH_FINGERPRINT")) graph_fingerprint_record(me);
#endif
    if (my shared_graph && !my graph) {
        my graph = graph_new(my checker);
        my checker_budget = my checker_start_budget = UINT64_MAX;
        // (On threads: the checkers run at once, each its own lib singletons, plan 86 4d)
        graph_set_concurrent(my graph, (my threads ? my threads : 1) > 1);
    }
    my binder->frozen = true;
    my errs->frozen = true;
    // (Declaration emit needs the walk even where its diagnostics won't be reported: --dts-dump under noEmit)
    bool declaration_diagnostics =
        my emit_declarations || !(program_declaration_diagnostics_gated(me) && my declaration_diagnostics_earlier);
    my declaration_errs = declaration_diagnostics ? c_new_n(ErrorCtx, count) : nullptr;
    atomic_store(&my declaration_diagnostics_moot, false);
    if (my emit_declarations) program_set_declaration_paths(me);
    for (u32 c = 0; c < count; c++) {
        if (my declaration_errs) my declaration_errs[c] = (ErrorCtx){ .phase = ERROR_PHASE_TYPECHECK, .parent = my errs };
        checker_errs[c] = (ErrorCtx){ .phase = ERROR_PHASE_TYPECHECK, .parent = my errs };
        my checkers[c] = c ? checker_new(me) : my checker;
        my checkers[c]->errs = &checker_errs[c];
    }
    // (--shared-graph: the graph-wide recycle's budget, what the checkers hold together (their arenas and
    // tables; the deferred bodies they hold come on top, about as much again on vscode). 800 MB recycles vscode twice
    // and none of the other corpora, dokploy or monkeytype (plan 86 4e item 3). TTTT_GRAPH_BUDGET_MB overrides; off
    // with --checker-memory off)
    // `--checker-memory 0` recycles the graph after every file, the recycle oracle's fresh start per file
    my graph_budget = UINT64_MAX;
    if (my graph && my checker_memory != UINT64_MAX) {
        char const* budget = getenv("TTTT_GRAPH_BUDGET_MB");
        my graph_budget = budget ? strtoull(budget, nullptr, 10) << 20 : my checker_memory ? (u64)GRAPH_BUDGET_MB << 20 : 0;
        my graph_held = c_new_n(_Atomic u64, count);
        for (u32 c = 0; c < count; c++) atomic_init(&my graph_held[c], 0);
        pthread_mutex_init(&my graph_epoch_lock, nullptr);
        pthread_cond_init(&my graph_epoch_cond, nullptr);
        my graph_active = my graph_arrived = my graph_epoch = 0;
        atomic_store(&my graph_epoch_due, false);
    }
    my steal_files = !getenv("TTTT_NO_STEAL");
    atomic_store(&my graph_leader, my graph && getenv("TTTT_GRAPH_LEAD") ? 0 : -1);
    my share_states = c_new_n(_Atomic u32, count);
    my share_left = c_new_n(_Atomic u64, count);
    if (my memory_pool != UINT64_MAX && !my graph) {
        my pool_held = c_new_n(_Atomic u64, count);
        for (u32 c = 0; c < count; c++) atomic_init(&my pool_held[c], 0);
    }
    my share_first = c_new_n(u32, count);
    deal_files(me);
    timespec_get(&my check_start, TIME_UTC);
    if (debug_env.file_times) {
        fprintf(stderr, "file time: checkers started %.1f ms into program_typecheck\n",
            (double)(my check_start.tv_sec - typecheck_start.tv_sec) * 1e3 + (double)(my check_start.tv_nsec - typecheck_start.tv_nsec) / 1e6);
    }
    CheckShares shares = { .program = me, .count = count };
    u32 threads = my threads ? my threads : 1;
    u32 helpers = threads > count && program_has_deferred_bodies(me) ? threads - count : 0;
    pthread_t* helper_threads = nullptr;
    if (helpers) {
        atomic_init(&my ahead_next, 0);
        char const* limit = getenv("TTTT_AHEAD_KB");
        ahead_limit = limit ? strtoull(limit, nullptr, 10) << 10 : (u64)AHEAD_LIMIT_KB << 10;
        ahead_resume = ahead_limit / 8;
        helper_threads = c_new_n(pthread_t, helpers);
        for (u32 t = 0; t < helpers; t++) {
            panic_if(worker_thread_create(&helper_threads[t], materialize_ahead_thread, me), "couldn't start a materialize thread");
        }
    }
    if (threads > count) threads = count;
    if (my graph && getenv("TTTT_GRAPH_SEED")) {
        // (main.c refuses the command line otherwise; a program made another way still mustn't block outside the token)
        panic_if(my graph_budget != UINT64_MAX || helpers, "TTTT_GRAPH_SEED needs --checker-memory off and --threads equal to --checkers");
        graph_sched_init(threads);
    }
    if (threads <= 1) {
        check_shares_run(&shares);
    } else {
        // The main thread works too. Workers get the main thread's stack size: the checker recurses per nesting level
        pthread_t* workers = c_new_n(pthread_t, threads - 1);
        for (u32 t = 0; t < threads - 1; t++) {
            panic_if(worker_thread_create(&workers[t], check_shares_thread, &shares), "couldn't start a checker thread");
        }
        check_shares_run(&shares);
        for (u32 t = 0; t < threads - 1; t++) worker_thread_join(workers[t]);
        free(workers);
    }
    if (helpers) {
        // (The checkers are done: what's left of the helpers' files no checker needs)
        atomic_store(&my ahead_next, (u32)vec_source_file_ptr_size(&my check_files));
        pthread_mutex_lock(&ahead_lock);
        pthread_cond_broadcast(&ahead_taken);
        pthread_mutex_unlock(&ahead_lock);
        for (u32 t = 0; t < helpers; t++) worker_thread_join(helper_threads[t]);
        free(helper_threads);
        // (What they hold that no checker took, when checking stopped early, stays to the end: its reports are still to
        // be made, program_finish_deferred_bodies())
        for_each (i, vec_source_file_ptr, &my check_files) {
            DeferredBodies* list = (*i)->deferred_bodies;
            if (!list || !list->ahead) continue;
            list->ahead = false;
            atomic_fetch_sub(&ahead_bytes, list->source_bytes);
        }
    }
    unused_free_retired();
    if (debug_env.dup_census) {
        for (u32 c = 0; c < count; c++) dup_census_add(my checkers[c], c);
        dup_census_report();
    }
    my binder->frozen = false;
    my errs->frozen = false;
    if (my graph && getenv("TTTT_GRAPH_STATS")) graph_print_stats(my graph);
    if (graph_sched_on) {
        if (getenv("TTTT_GRAPH_STATS")) graph_sched_print();
        graph_sched_on = false;
    }
#ifdef TTTT_GRAPH_FINGERPRINT
    graph_fp_print();
#endif
    if (getenv("TTTT_DEBUG_VARIANCE")) { extern void vlog_dump(void); vlog_dump(); }
    if (my graph && getenv("TTTT_DEBUG_DISAGREE")) {
        extern void graph_print_disagreements(Checker* checker);
        checker_make_current(my checkers[0]);
        graph_print_disagreements(my checkers[0]);
        checker_make_current(nullptr);
    }
    if (my first_touch_stamps && getenv("TTTT_GRAPH_STATS")) {
        u64 decisions = 0, disagreements = 0;
        for (u32 c = 0; c < count; c++) decisions += my checkers[c]->stamp_decisions, disagreements += my checkers[c]->stamp_disagreements;
        eprintln("stamps: ", decisions, " deep nesting decisions, ", disagreements, " the type ids would have made otherwise");
    }
    error_ctx_keep_first_once(checker_errs, count);
    if (my graph && getenv("TTTT_GRAPH_RECHECK")) graph_recheck(me, checker_errs, count);
    for (u32 c = 0; c < count; c++) {
        error_ctx_merge(my errs, &checker_errs[c]);
        error_ctx_drop(&checker_errs[c]);
        my checkers[c]->errs = my errs;
    }
    free(checker_errs);
    if (my graph_held) {
        if (getenv("TTTT_GRAPH_STATS")) eprintln("graph: ", my graph_recycles, " graph-wide recycles");
        free(my graph_held);
        my graph_held = nullptr;
        pthread_mutex_destroy(&my graph_epoch_lock);
        pthread_cond_destroy(&my graph_epoch_cond);
    }
    free(my share_states);
    free(my share_left);
    free(my pool_held);
    my pool_held = nullptr;
    free(my share_first);
    my share_states = nullptr;
    my share_left = nullptr;
    // (Recycled: the first share's last checker)
    my checker = my checkers[0];
    checker_make_current(my checker);
    program_finish_deferred_bodies(me);

    error_ctx_remove_unchecked(my errs);
    program_apply_comment_directives(me, ERROR_PHASE_TYPECHECK);
    program_report_unused_expect_errors(me);
    program_apply_content_mapper_directives(me);
    // (Comment directives don't apply to them: tsc's getDeclarationDiagnostics is apart from the checker's)
    if (my declaration_errs) {
        bool keep = !program_declaration_diagnostics_gated(me) || error_ctx_empty(my errs);
        for (u32 c = 0; c < count; c++) {
            for_each (e, vec_error, &my declaration_errs[c].errors) e->declaration = true;
            if (keep) error_ctx_merge(my errs, &my declaration_errs[c]);
            error_ctx_drop(&my declaration_errs[c]);
        }
        free(my declaration_errs);
        my declaration_errs = nullptr;
    }
    error_ctx_sort_and_dedupe(my errs);

    clear_compiler_options();
    my errs->phase = ERROR_PHASE_UNKOWN;
    CHANGE_PHASE(PROGRAM_PHASE_TYPECHECKING, PROGRAM_PHASE_TYPECHECKED);
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
SpecifierMode program_usage_mode(Program const* me, SourceFile const* file, ModuleRef const* ref) {
    if (ref->resolution_override == IMPORT_RESOLUTION_IMPORT) return SPECIFIER_MODE_ESM;
    if (ref->resolution_override == IMPORT_RESOLUTION_REQUIRE) return SPECIFIER_MODE_COMMONJS;
    if (!import_syntax_affects_resolution(program_options_of(me, file))) return SPECIFIER_MODE_NONE;
    if (ref->syntax == MODULE_REF_REQUIRE) return SPECIFIER_MODE_COMMONJS;
    if (ref->syntax == MODULE_REF_DYNAMIC) return SPECIFIER_MODE_ESM;
    return specifier_mode_of_kind(source_file_emit_module_kind(file));
}

cstr program_module_specifier(Program* me, SourceFile const* importing, SourceFile const* target, SpecifierMode override) {
    return program_module_specifier_ending(me, importing, target, override, SPECIFIER_MODE_NONE);
}

// tsgo's Program.GetSymlinkCache, once: besides the resolutions' links, those of the runtime dependencies of the
// package.json nearest to every file that may be emitted
static void program_dependency_links(Program* me) {
    if (atomic_load(&my dependency_links)) return;
    pthread_mutex_lock(&my dependency_links_lock);
    if (!atomic_load(&my dependency_links)) {
        csview cwd = program_cwd(me);
        for_each (i, vec_source_file_ptr, &my source_files_ordered) {
            SourceFile const* file = *i;
            if (!program_source_may_be_emitted(me, file)) continue;
            ModuleResolver* resolver = program_resolver_of(me, file);
            cstr path = absolute_path_of(cwd, cstr_sv(&file->canonical_path));
            FileFormat format = module_resolver_file_format(resolver, cstr_sv(&path));
            csview package_json = cstr_sv(&format.package_json);
            if (package_json.size > 13) {
                symlink_cache_add_dependencies(my symlinks, resolver, csview_slice(package_json, 0, package_json.size - 13));
            }
            cstr_drop(&format.package_json);
            cstr_drop(&path);
        }
        atomic_store(&my dependency_links, true);
    }
    pthread_mutex_unlock(&my dependency_links_lock);
}

cstr program_module_specifier_ending(
    Program* me, SourceFile const* importing, SourceFile const* target, SpecifierMode override, SpecifierMode ending_mode
) {
    SpecifierMode default_mode = program_default_resolution_mode(me, importing);
    SpecifierMode wanted = override ? override : default_mode;
    // An import that already names it (computeModuleSpecifiers' existingSpecifier, by the file's include reasons)
    SpecifierImport* imports = malloc(sizeof(SpecifierImport) * (importing->module_ref_count + 1));
    u32 import_count = 0;
    // (Only the first import of it counts: one of another mode means none, tsgo's computeModuleSpecifiers)
    bool seen_target = false;
    csview existing = {0};
    for (u32 i = 0; i < importing->module_ref_count; i++) {
        ModuleRef const* ref = importing->module_refs[i];
        if (ref->is_declaration || ref->ambient_only || ref == importing->helpers_ref || ref == importing->jsx_runtime_ref) continue;
        SpecifierMode mode = program_usage_mode(me, importing, ref);
        imports[import_count++] = (SpecifierImport){ .text = cstr_sv(&ref->path), .mode = mode };
        if (ref->file != target || seen_target) continue;
        seen_target = true;
        if (mode != wanted && mode != SPECIFIER_MODE_NONE && wanted != SPECIFIER_MODE_NONE) continue;
        existing = cstr_sv(&ref->path);
    }
    // (Only a file in pnpm's store or the like may not be among its own paths: the rest needs no paths computed)
    if (existing.size && !module_path_is_ignored(cstr_sv(&target->canonical_path))) {
        free(imports);
        return cstr_from_sv(existing);
    }
    program_dependency_links(me);
    csview cwd = program_cwd(me);
    cstr importing_path = absolute_path_of(cwd, cstr_sv(&importing->canonical_path));
    cstr target_path = absolute_path_of(cwd, cstr_sv(&target->canonical_path));
    // (A referenced project's output declaration file is named as its source would be, the output first: tsgo's
    // GetSourceOfProjectReferenceIfOutputIncluded and GetEachFileNameOfModule's referenceRedirect)
    csview output = c_sv("");
    if (target->resolution_project && target->is_declaration) {
        csview source = program_source_of_output(me, cstr_sv(&target_path));
        if (source.size) {
            output = project_redirects_output_of_source(my redirects, source);
            cstr_assign_sv(&target_path, source);
        }
    }
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
        .ending_mode = ending_mode,
        .target = cstr_sv(&target_path),
        .redirect = output,
        .project_dir = cstr_sv(&project_dir),
        .existing = existing,
    };
    cstr ret = module_specifier_generate(&request);
    c_drop(cstr, &importing_path, &target_path, &project_dir);
    free(imports);
    return ret;
}

csview module_ambient_name(Symbol const* module) {
    for (u32 i = 0; i < module->decl_count; i++) {
        if (module->decls[i].kind != SYMBOL_DECL_NAMESPACE) continue;
        NamespaceDecl const* ns = module->decls[i].node;
        if (!ns->is_ambient_module || ns->name.size < 2) continue;
        csview name = csview_slice(span_sv(ns->name), 1, ns->name.size - 1);
        if (!ns->is_augmentation || !module_name_is_relative(name)) return name;
    }
    return c_sv("");
}

static cstr program_common_source_dir(Program* me);
cstr program_module_display_name(Program* me, Symbol const* module, SourceFile const* from, SpecifierMode override, ModuleRef const* usage) {
    SourceFile const* target = module->module ? module->module->file : nullptr;
    // (A JSON file's module symbol has no SourceFile declaration in tsc: named like an ambient module, by its quoted
    // escapedName)
    if (!target || target->is_json || !from) {
        return module_name_for_error(module, nullptr, false);
    }
    csview ambient = module_ambient_name(module);
    if (ambient.size) return cstr_d("\"", ambient, "\"");
    SpecifierMode ending = override ? override : usage ? program_usage_mode(me, from, usage) : SPECIFIER_MODE_NONE;
    cstr specifier = program_module_specifier_ending(me, from, target, override, ending);
    cstr ret = cstr_d("\"", specifier, "\"");
    cstr_drop(&specifier);
    return ret;
}

u32 program_serial(Program const* me) {
    return my serial;
}
