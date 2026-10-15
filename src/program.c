#include "program.h"
#include "tsconfig.h"
#include "lib_names.h"
#include "string_helpers.h"
#include "unused.h"
#include "ty/declaration_emit.h"
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
#include "content_mapper.h"
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
    // The content mappers' processes (plan 56), made by the first mapped file program_make_source() reads (a load
    // pool worker's maybe: hence atomic)
    ContentMapperHost* _Atomic content_mapper_host;
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
    bool reference_outputs; // A referenced project's sources are read as their outputs (tsc's command line)
    u32 node_modules_depth; // How many node_modules lookups the file being loaded was reached through (tsc's currentNodeModulesDepth)
    Span include_site; // The module name or `/// <reference path>` the file being loaded is loaded for (SourceFile.included_at)
    IncludeKind include_kind; // Why the file being loaded is: its next SourceFile.include_reasons entry (with include_site)
    csview include_package_id; // And the package id of the resolution, for INCLUDE_IMPORT
    ModuleResolver** reference_resolvers;
    u32 reference_count;
    RealpathFn realpath; // program_set_realpath()
    void* realpath_ctx;
    SymlinkCache* symlinks; // What resolutions learned about symlinked directories, for program_module_specifier()
    u32 serial; // program_serial()
    HMapBoxSourceFile source_files_map;
    VecSourceFilePtr source_files_ordered;
    // The program_order of the first file: the files of a parent program (the test harness's shared libraries, see
    // program_copy_globals_from()) come before this one's, like tsc's default library files
    u32 order_base;
    HMapSourceFilePtr files_by_package_id; // ResolvedModule.package_id -> the first file found with it
    // The file being added is a package's second copy of this first one (program_load_resolved())
    SourceFile* adding_package_redirect;
    // tsc's getPackagesMap: the package name of every module resolution that found a file of a package, whether one of
    // them was a declaration file
    HMapPackageFlag packages;
    Checker* checker; // Created by program_make_current(), checks the first share of the files
    Checker** checkers; // malloc'd, checker_count of them once typechecked: checker and the others
    u32 checker_count;
    // What program_typecheck() checks, in order: every checked file of source_files_ordered
    VecSourceFilePtr check_files;
    u32* file_shares; // malloc'd: the checker of each file of check_files (see share_of())
    u32 requested_checkers; // program_set_checkers()
    u64 checker_memory; // program_set_checker_memory(), bytes: UINT64_MAX never recycles
    u64 checker_budget; // While typechecking: what each checker may hold (checker_memory split between them)
    // program_declaration_diagnostics_cli(): earlier phases reported something
    bool declaration_diagnostics_cli, declaration_diagnostics_earlier;
    bool emit_declarations; // program_set_emit_declarations()
    // Output paths TS5055/TS5056 were reported for (relative to the working directory): tsc's hasEmitBlockingDiagnostics,
    // nothing is written there
    hmap_source_file_ptr emit_blocked;
    bool global_diagnostics_reported; // program_report_global_diagnostics()
    ErrorCtx* declaration_errs; // One per checker while typechecking: the declaration diagnostics of its files
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
    Program* ret = c_new(Program, {
        .errs = errs,
        .fs = fs,
        .binder = binder_new(errs),
        .options = compiler_options_init(),
        .symlinks = symlink_cache_new(),
        .checker_memory = UINT64_MAX,
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
    content_mapper_host_drop(atomic_load(&my content_mapper_host));
    // First, its memos point into the AST and the scopes
    for (u32 i = 1; i < my checker_count; i++) checker_drop(my checkers[i]);
    free(my file_shares);
    vec_source_file_ptr_drop(&my check_files);
    free(my checkers);
    checker_drop(my checker);
    drop_source_files(me);
    hmap_box_source_file_drop(&my source_files_map);
    hmap_source_file_ptr_drop(&my files_by_package_id);
    hmap_source_file_ptr_drop(&my emit_blocked);
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
    for (u32 i = 0; i < my extended_config_count; i++) {
        source_file_drop(my extended_configs[i]);
        free(my extended_configs[i]);
    }
    free(my extended_configs);
    binder_destroy(my binder); // After the AST: type nodes in it may own things symbols point to
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
        if (!my reference_outputs) module_resolver_set_redirects(my resolver, my redirects);
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

void program_set_references(Program* me, ProjectRedirects const* redirects, bool outputs) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    check(!my reference_count && !my pool);
    my redirects = redirects;
    my reference_outputs = outputs;
    // (Reading outputs, only built ones exist: tsc's command line host is the file system's)
    if (outputs) redirects = nullptr;
    if (my resolver) module_resolver_set_redirects(my resolver, redirects);
    // (One per referenced project also reading outputs: their output declaration files resolve with their project's
    // options, program_make_source())
    my reference_count = project_redirects_count(my redirects);
    my reference_resolvers = c_new_n(ModuleResolver*, my reference_count + 1);
    for (u32 i = 0; i < my reference_count; i++) {
        ModuleResolver* resolver = module_resolver_new(my fs, project_redirects_options(my redirects, i));
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
    if (!my redirects || my reference_outputs || !is_declaration_path(path)) return c_sv("");
    if (path.size && path.buf[0] == '/') return project_redirects_source_of_output(my redirects, path);
    cstr absolute = absolute_path_of(program_cwd(me), path);
    csview ret = project_redirects_source_of_output(my redirects, cstr_sv(&absolute));
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

// Reading outputs, a resolution that found a referenced project's source finds its output declaration file (tsc's
// findSourceFileWorker, getRedirectFromSourceFile). Returns the output when it hasn't been built (resolved is then
// unchanged), empty otherwise
static csview program_output_of_resolved(Program const* me, ResolvedModule* resolved) {
    if (!my reference_outputs || (resolved->kind != RESOLVED_TS && resolved->kind != RESOLVED_JS)) return c_sv("");
    csview output = program_output_of_source(me, cstr_sv(&resolved->path));
    if (!output.size) return output;
    if (!fs_is_reg_file(my fs, output)) return output;
    cstr_assign_sv(&resolved->path, output);
    resolved->kind = RESOLVED_DTS;
    resolved->is_external_library = false;
    cstr_take(&resolved->package_id, cstr_null);
    return c_sv("");
}

// A resolution that found a referenced project's output finds its source (tsc's getSourceOfProjectReferenceRedirect),
// or reading outputs, one that found its source finds its built output (else nothing)
static void program_redirect_resolved(Program const* me, ResolvedModule* resolved) {
    if (my reference_outputs) {
        if (program_output_of_resolved(me, resolved).size) resolved->kind = RESOLVED_NONE;
        return;
    }
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
    if (ref->resolution_override == IMPORT_RESOLUTION_IMPORT) mode = RESOLUTION_MODE_ESM;
    if (ref->resolution_override == IMPORT_RESOLUTION_REQUIRE) mode = RESOLUTION_MODE_REQUIRE_SYNTAX;
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
    csview unbuilt = program_output_of_resolved(me, &resolved);
    if (unbuilt.size) {
        ref->failure = MODULE_REF_NOT_BUILT;
        cstr_take(&module_ref_rare_mut(ref)->unbuilt_output, cstr_from_sv(unbuilt));
        cstr_take(&module_ref_rare_mut(ref)->resolved_path, cstr_clone(resolved.path));
        resolved_module_drop(&resolved);
        return;
    }
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
        cstr_take(&module_ref_rare_mut(ref)->augmented_path, cstr_clone(resolved.path));
        resolved_module_drop(&resolved);
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
                cstr_take(&module_ref_rare_mut(ref)->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
            } else if (resolved.arbitrary_extension && !my options.allow_arbitrary_extensions && !source->is_declaration) {
                ref->failure = MODULE_REF_ARBITRARY_EXTENSION;
                cstr_take(&module_ref_rare_mut(ref)->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
            } else {
                // The same version of a package installed twice is one package (tsc's packageIdToSourceFile): the
                // second copy stays in the program, but imports mean the first, and its own imports are the first's
                // (tsc's redirect source file shares the first's AST: see program_share_redirect_ref()). Unless
                // `deduplicatePackages: false` (tsgo's filesparser)
                bool dedup = cstr_size(&resolved.package_id) && my options.deduplicate_packages;
                auto found = dedup ? hmap_source_file_ptr_get(&my files_by_package_id, cstr_sv(&resolved.package_id)) : nullptr;
                SourceFile* first = found ? found->second : nullptr;
                my adding_package_redirect = first;
                ref->file = program_add_source_idempotent(me, cstr_sv(&resolved.path));
                my adding_package_redirect = nullptr;
                // (Only an extension the name itself has counts, not one a package.json or an export map wrote:
                // tsc's candidateIsFromPackageJsonField, tryExtractTSExtension of the name)
                ref->used_ts_extension = resolved.used_ts_extension &&
                    (resolved.ts_extension_from_star || name_has_ts_extension(cstr_sv(&ref->path)));
                if (dedup && ref->file) {
                    if (first) {
                        if (first != ref->file) {
                            module_ref_rare_mut(ref)->redirected_from = ref->file;
                            ref->file->package_redirect = true;
                            ref->file->package_redirect_target = first;
                            ref->file->unchecked = true;
                        }
                        ref->file = first;
                    } else {
                        hmap_source_file_ptr_insert(&my files_by_package_id, cstr_clone(resolved.package_id), ref->file);
                    }
                }
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
        csview source = program_source_of_output(me, cstr_sv(&candidate));
        if (!source.size && fs_is_reg_file(my fs, cstr_sv(&candidate))) source = cstr_sv(&candidate);
        // Reading outputs, a referenced project's source is its output; when it hasn't been built that's TS6305
        // (tsc's getSourceFileFromReferenceWorker through processSourceFile)
        csview output = source.size && my reference_outputs ? program_output_of_source(me, source) : c_sv("");
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
        program_resolver_of(me, source), name, source ? cstr_sv(&source->canonical_path) : c_sv(""),
        has_mode ? &mode : nullptr
    );
    program_learn_symlink(me, &resolved);
    program_redirect_resolved(me, &resolved);
    if (resolved.is_external_library) my node_modules_depth++;
    SourceFile* ret = resolved.kind == RESOLVED_NONE ? nullptr :
        program_add_source_idempotent(me, cstr_sv(&resolved.path));
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
    try (fs_read_file(my fs, canonical_path, &contents)) if_err (e) {
        // Not worth reporting: the caller turns a null return into a TypeScript "file not found" error
        ignore e;
        current_error_destroy();
        return nullptr;
    }
    text_decode(&contents);

    ContentMapper const* mapper = my options.content_mapper_count && !is_stdlib ?
        content_mapper_for_path(&my options, canonical_path) : nullptr;
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
    // A referenced project's file is that project's to check and report. Reading outputs, a built output declaration
    // file is the program's own, but its module names resolve with its project's options
    if (my redirects && !is_stdlib) {
        cstr absolute = absolute_path_of(program_cwd(me), path);
        if (my reference_outputs) {
            csview source = source_file->is_declaration ?
                project_redirects_source_of_output(my redirects, cstr_sv(&absolute)) : c_sv("");
            if (source.size) source_file->resolution_project = project_redirects_project_of(my redirects, source);
        } else {
            source_file->from_reference = project_redirects_project_of(my redirects, cstr_sv(&absolute));
            source_file->resolution_project = source_file->from_reference;
        }
        cstr_drop(&absolute);
        if (source_file->from_reference) {
            source_file->unchecked = true;
            source_file->plain_js = false;
            source_file->skip_typed_bodies = !source_file->is_js && !source_file->is_jsx && !source_file->is_json;
            CompilerOptions const* options = project_redirects_options(my redirects, source_file->from_reference - 1);
            source_file->reference_const_enums_ambient = !options->preserve_const_enums && !options->isolated_modules &&
                !options->verbatim_module_syntax;
        }
        if (source_file->resolution_project) {
            CompilerOptions const* options = project_redirects_options(my redirects, source_file->resolution_project - 1);
            source_file->reference_emits_esm = options->module >= MODULE_KIND_ES2015 && options->module <= MODULE_KIND_ESNEXT;
            // (A built output's const enums are ambient, unless its project preserves them)
            source_file->reference_preserves_const_enums = !source_file->from_reference && (options->preserve_const_enums ||
                options->isolated_modules || options->verbatim_module_syntax);
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
        source_file->package_has_type = format.package_has_type;
        if (cstr_size(&format.package_json)) {
            source_file->package_json = program_path_for_message(me, cstr_sv(&format.package_json));
        }
        cstr_drop(&format.package_json);
    }

    parse_file(source_file, errs);
    cstr_drop(&virtual_path);
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
                cstr replacement = program_lib_replacement(me, csview_slice(cstr_sv(&name), 0, cstr_size(&name) - 5));
                cstr path = cstr_size(&replacement) ? cstr_clone(replacement) : file->is_stdlib ? cstr_d(dirname_of(containing), "/", name) :
                    cstr_d(cstr_size(&my lib_dir) ? cstr_sv(&my lib_dir) : default_lib_dir, "/", name);
                program_queue_path(me, pool, cstr_sv(&path), !cstr_size(&replacement));
                c_drop(cstr, &path, &replacement);
                cstr_drop(&name);
            }   break;
        }
    }
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
        my pool = load_pool_new(my threads, program_parse_ahead, source_file_free, me);
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

// An import of a package's second copy (SourceFile.package_redirect) is what the same import of the first copy found,
// which is loaded already: tsc's redirect source file has the first's AST, imports included, so nothing the second
// copy's directory has is loaded (`import "globals"` of each copy). False if the first has no such import (the copies
// differ), resolved as its own then
static bool program_share_redirect_ref(SourceFile const* target, ModuleRef* ref) {
    if (ref->is_declaration) return false;
    for (u32 i = 0; i < target->module_ref_count; i++) {
        ModuleRef const* own = target->module_refs[i];
        if (own->is_declaration || own->syntax != ref->syntax || !cstr_eq(&own->path, &ref->path)) continue;
        ref->file = own->file;
        if (own->rare || ref->rare) module_ref_rare_mut(ref)->redirected_from = module_ref_rare(own)->redirected_from;
        ref->failure = own->failure;
        ref->used_ts_extension = own->used_ts_extension;
        ref->external_library = own->external_library;
        if (cstr_size(&module_ref_rare(own)->resolved_path) || ref->rare) {
            cstr_take(&module_ref_rare_mut(ref)->resolved_path, cstr_clone(module_ref_rare(own)->resolved_path));
        }
        return true;
    }
    return false;
}

// Records why the file being added is (Program.include_kind, once)
static void record_include_reason(Program* me, SourceFile* file) {
    if (!my include_kind) return;
    if (file->include_reason_count == file->include_reason_cap) {
        file->include_reason_cap = file->include_reason_cap ? file->include_reason_cap * 2 : 2;
        file->include_reasons = realloc(file->include_reasons, sizeof(IncludeReason) * file->include_reason_cap);
        panic_if(!file->include_reasons, "out of memory");
    }
    file->include_reasons[file->include_reason_count++] = (IncludeReason){
        .kind = my include_kind, .at = my include_site, .package_id = cstr_from_sv(my include_package_id),
    };
    my include_kind = INCLUDE_NONE;
}

static SourceFile* program_add_source_idempotent(Program* me, csview path) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);

    cstr canonical_path = relative_path_of(program_cwd(me), path);

    auto cached_source = hmap_box_source_file_get(&my source_files_map, cstr_sv(&canonical_path));
    if (cached_source) {
        record_include_reason(me, cached_source->second.get);
        // A source file is already created for this path
        cstr_drop(&canonical_path);
        SourceFile* found = cached_source->second.get;
        if (found->found_searching_node_modules && !my node_modules_depth) program_found_outside_node_modules(found);
        return found;
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
    return program_add_made_source(me, canonical_path, source_file);
}

// The rest of program_add_source_idempotent() for a file just made (canonical_path is taken): it and what it refers to
// join the program, it after them
static SourceFile* program_add_made_source(Program* me, cstr canonical_path, SourceFile* source_file) {
    if (source_file->is_json) source_file->is_module = true;

    hmap_box_source_file_insert(&my source_files_map, canonical_path, box_source_file_from_ptr(source_file));
    source_file->found_searching_node_modules = my node_modules_depth > 0;
    // (Before what it refers to is loaded, which may refer back to it: tsc adds the file's include reason first)
    record_include_reason(me, source_file);
    if (span_valid(my include_site)) source_file->included_at = my include_site;
    my include_site = span_null;

    // (A package's second copy, see program_load_resolved())
    SourceFile* redirect_target = my adding_package_redirect;
    my adding_package_redirect = nullptr;
    // (Only once the first copy is loaded, its imports resolved: not while it is, an import cycle back to the package)
    bool target_loaded = false;
    for (isize i = vec_source_file_ptr_size(&my source_files_ordered); redirect_target && !target_loaded && i-- > 0;) {
        target_loaded = *vec_source_file_ptr_at(&my source_files_ordered, i) == redirect_target;
    }
    if (!target_loaded) redirect_target = nullptr;
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

    // Every module the file mentions, wherever (the parser lists them). By index: loading parses, never this file
    for (u32 i = 0; i < source_file->module_ref_count; i++) {
        ModuleRef* ref = source_file->module_refs[i];
        // `declare module "./a" {}` in a module augments a file. In a script it declares an ambient module, and so
        // does a name that finds no file
        if (ref->is_declaration && !source_file->is_module && !ref->nested_in_ambient_module) continue;
        if (ref->ambient_only) continue;
        if (my options.no_resolve) continue; // program_resolve_names(), once every root is loaded
        // (tsgo resolves a name the file declares an ambient module for too, tsc 6's resolveToOwnAmbientModule is gone: the
        // file it finds is part of the program, and checked)
        if (redirect_target && program_share_redirect_ref(redirect_target, ref)) continue;
        program_load_module_ref(me, source_file, ref);
    }
    // A content mapper's supplemental outputs, after what the file imports (tsgo's parse subtasks)
    ContentMapped const* mapped = source_file->content_mapped;
    for (u32 i = 0; mapped && i < mapped->supplemental_count; i++) {
        SourceFile* supplemental = mapped->supplementals[i];
        if (hmap_box_source_file_get(&my source_files_map, cstr_sv(&supplemental->canonical_path))) continue;
        program_add_made_source(me, cstr_clone(supplemental->canonical_path), supplemental);
    }

    // Add here so imports come before importing file
    source_file->program_order = my order_base + (u32)vec_source_file_ptr_size(&my source_files_ordered);
    vec_source_file_ptr_push(&my source_files_ordered, source_file);

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
    // A referenced project's output is its source
    csview source_of_output = program_source_of_output(me, path);
    if (source_of_output.size) path = source_of_output;
    // Reading outputs, a referenced project's source is its output (tsc's getSourceFileFromReferenceWorker)
    csview output = my reference_outputs ? program_output_of_source(me, path) : c_sv("");
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
    SourceFile* main_source = program_add_source_idempotent(me, path);
    my include_kind = INCLUDE_NONE;
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

void program_load_ahead(Program* me, csview const* libs, isize lib_count, csview const* roots, isize root_count) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    LoadPool* pool = program_pool(me);
    if (!pool) return;
    for (isize i = 0; i < lib_count; i++) {
        cstr replacement = program_lib_replacement(me, libs[i]);
        cstr path = cstr_size(&replacement) ? cstr_clone(replacement) :
            cstr_d(cstr_size(&my lib_dir) ? cstr_sv(&my lib_dir) : default_lib_dir, "/", libs[i], ".d.ts");
        program_queue_path(me, pool, cstr_sv(&path), !cstr_size(&replacement));
        c_drop(cstr, &path, &replacement);
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
    // (The parse errors of referenced projects' files, and the parser's grammar errors in unchecked files (tsc's
    // checker's, a skipLibCheck declaration file's), before anyone reports them)
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
    if (source->is_declaration || source->is_stdlib || source->from_reference) return false;
    if (source->found_searching_node_modules) return false;
    if (!source->is_json) return true;
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
    // (tsgo's GetRedirectForResolution: a referenced project's source, or its output reading outputs)
    u32 project = target->from_reference ? target->from_reference : target->resolution_project;
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
        // GetIncludeProcessorDiagnostics through getDiagnosticsWithPrecedingDirectives)
        if (error) error->phase = ERROR_PHASE_RESOLVE_NAMES;
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
    for (u32 i = 0; i < file->include_reason_count; i++) {
        if (&file->include_reasons[i] != preferred) add_include_related_info(me, error, file, &file->include_reasons[i]);
    }
    CompilerOptions const* saved = compiler_options_private;
    set_compiler_options(program_options_of(me, file));
    explain_implied_format(error, file);
    compiler_options_private = saved;
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
    for (isize i = 0; i < count; i++) files[i]->program_order = my order_base + (u32)i;
}

static void resolve_file(SourceFile* source, ErrorCtx* errs) {
    block_lookup_names_container(&source->ast, c_sv("file"), span_null, source->scope, errs);
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
    Arena* scope_arena = thread ? binder_thread_scope_arena(binder, thread) : &binder->scope_arena;
    for (u32 i; (i = atomic_fetch_add(&my next, 1)) < my count;) {
        ResolveShard* shard = &my shards[my shard_of[i]];
        *shard = (ResolveShard){
            .binder = binder, .arena = arena, .scope_arena = scope_arena,
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

void program_verify_options(Program* me) {
    program_validate_options(me);
    program_verify_inferred_root_dir(me);
    program_verify_emit_paths(me);
    program_verify_project_files(me);
}

void program_resolve_names(Program* me) {
    program_report_content_mapper_options(me);
    CHANGE_PHASE(PROGRAM_PHASE_LOADING, PROGRAM_PHASE_RESOLVING_NAMES);
    // Loading is over: what the pool parsed for nothing goes
    load_pool_destroy(my pool);
    my pool = nullptr;
    program_verify_options(me);
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
            if (ref->is_declaration && !ref->file && cstr_size(&module_ref_rare(ref)->augmented_path)) {
                ref->file = program_find_source(me, cstr_sv(&module_ref_rare(ref)->augmented_path));
            }
            // (A package's second copy is the first to an augmentation too: tsc's redirect source file has the
            // first's symbol)
            while (ref->is_declaration && ref->file && ref->file->package_redirect_target) ref->file = ref->file->package_redirect_target;
        }
    }
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        block_add_names(&source->ast, source->scope);
        scope_add_commonjs_module(source->scope);
        scope_check_export_merges(source->scope, block_span(&source->ast));
        if (source->is_module && (!source->is_js || source->is_commonjs)) scope_check_duplicate_exports(source->scope);
    }
    binder_declare_global_augmentations(my binder);
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

// (64 KB: below it a second checker saves a few milliseconds and costs its own copy of the library types, 4-9% of the
// RSS; measured on a twelfth of the source sweep 2026-10-14, plan 62 stage 3. Bytes are a rough proxy for check time:
// nestjs-boilerplate checks 240 KB in 110 ms where most projects of that size take 25)
enum { CHECKER_BYTES = 64 << 10 };

void program_set_checkers(Program* me, u32 count) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    my requested_checkers = count;
}

void program_set_checker_memory(Program* me, u64 bytes) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    my checker_memory = bytes;
}

void program_set_emit_declarations(Program* me, bool emit) { my emit_declarations = emit; }

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

// The checkers of program_typecheck(), taken by the threads in index order. Which files a checker checks depends only
// on the file list and the checker count, never on which thread runs it
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

// TTTT_FILE_TIMES
static void print_file_time(char const* what, struct timespec start, SourceFile const* source) {
    struct timespec end;
    timespec_get(&end, TIME_UTC);
    double ms = (double)(end.tv_sec - start.tv_sec) * 1e3 + (double)(end.tv_nsec - start.tv_nsec) / 1e6;
    fprintf(stderr, "file time: %10.1f ms %s %s\n", ms, what, cstr_str(&source->canonical_path));
}

// After a segment's files (from..to of check_files) are checked, in the checker that checked them: unused_report() reads
// the marks the checker made on members while checking, the declaration walk the types it inferred
static void check_segment_after(Program* me, Checker* checker, u32 c, u32 from, u32 to) {
    for (u32 at = from; at < to; at++) {
        if (share_of(me, at) != c) continue;
        SourceFile* source = vec_source_file_ptr_at(&my check_files, at)[0];
        struct timespec file_start;
        if (debug_env.file_times) timespec_get(&file_start, TIME_UTC);
        if (!source->is_declaration && !source->unchecked) unused_report(source, checker->errs);
        if (my declaration_errs && !source->is_stdlib) {
            declaration_diagnostics_check(source, &my declaration_errs[c], my emit_declarations);
        }
        if (debug_env.file_times) print_file_time("unused and declaration", file_start, source);
    }
}

// A fresh checker for share c, reporting where the last one did
static Checker* checker_recycled(Program* me, Checker* old, u32 c) {
    Checker* checker = checker_new(me);
    checker->errs = old->errs;
    checker->recycles = old->recycles + 1;
    checker->peak_memory = old->peak_memory;
    checker_drop(old);
    my checkers[c] = checker;
    checker_make_current(checker);
    ty_init_global_types(checker);
    return checker;
}

// Checks share c of the files. With a checker memory budget the share is checked in segments: once the checker holds
// more than the budget after a file, the segment's files get their after-check pass and the next file starts in a fresh
// checker (tsgo keeps its checkers, but nothing tttt reports depends on a checker's history, plan 62: what bun does,
// plan 67). A fresh checker builds the global and library types its files use again: a few milliseconds on vscode,
// ~100 ms where the files share one big type graph (type-fest, drizzle). Nothing cheap predicts that cost (what the
// segment's first file held doesn't: those are the default library's declarations), so the budget alone decides.
// Where the cuts fall depends only on the checker's own work on its own files, not on the threads
#define CHECKER_MEMORY_FLOOR ((u64)128 << 20)

static void check_share(Program* me, u32 c, u32 count) {
    struct timespec start, end;
    timespec_get(&start, TIME_UTC);
    Checker* checker = my checkers[c];
    checker_make_current(checker);
    ty_init_global_types(checker);
    u32 total = (u32)vec_source_file_ptr_size(&my check_files);
    u32 last = 0; // The last file of the share
    for (u32 at = 0; at < total; at++) {
        if (share_of(me, at) == c) last = at;
    }
    u32 segment_start = 0;
    for (u32 at = 0; at < total; at++) {
        if (share_of(me, at) != c) continue;
        SourceFile* source = vec_source_file_ptr_at(&my check_files, at)[0];
        checker->file_order = at;
        checker->flow_analysis_disabled = false;
        struct timespec file_start;
        if (debug_env.file_times) timespec_get(&file_start, TIME_UTC);
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
        if (debug_env.file_times) print_file_time("check", file_start, source);
        if (my checker_budget == UINT64_MAX || at == last) continue;
        u64 held = checker_memory(checker);
        if (held > checker->peak_memory) checker->peak_memory = held;
        if (held <= my checker_budget) continue;
        check_segment_after(me, checker, c, segment_start, at + 1);
        checker = checker_recycled(me, checker, c);
        segment_start = at + 1;
    }
    check_segment_after(me, checker, c, segment_start, total);
    u64 held = checker_memory(checker);
    if (held > checker->peak_memory) checker->peak_memory = held;
    // Declaration emit's diagnostics don't depend on skipTypeChecking: also for files noCheck or `@ts-nocheck` leave
    // unchecked (not a referenced project's, which that project reports)
    u32 index = 0;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        if (source->is_stdlib || source_file_is_checked(source) || source->from_reference || source->is_js) continue;
        if (index++ % count != c) continue;
        if (my declaration_errs) declaration_diagnostics_check(source, &my declaration_errs[c], my emit_declarations);
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
// two keep most of it (1337 MB) at round robin's wall times or better. Bytes are the cost proxy, except for the default
// library's files: megabytes of declarations (lib.dom.d.ts) that check in a few milliseconds would take most runs and
// leave a small project's own files to one checker. They are dealt round robin instead
static void deal_files(Program* me) {
    enum { RUNS_PER_CHECKER = 2 };
    vec_source_file_ptr_clear(&my check_files);
    // (Debugging order dependences: TTTT_CHECK_ONLY=substring checks only the files whose path has it, in a fresh
    // checker, the rest of the program still there)
    char const* only = getenv("TTTT_CHECK_ONLY");
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        if (!source_file_is_checked(*i)) continue;
        if (only && !(*i)->is_stdlib && !strstr(cstr_str(&(*i)->canonical_path), only)) continue;
        vec_source_file_ptr_push(&my check_files, *i);
    }
    u32 total = (u32)vec_source_file_ptr_size(&my check_files);
    u64 bytes = 0;
    for_each (i, vec_source_file_ptr, &my check_files) {
        if (!(*i)->is_stdlib) bytes += cstr_size(&(*i)->contents);
    }
    free(my file_shares);
    my file_shares = c_new_n(u32, total ? total : 1);
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
// project's few files don't win back
static u32 auto_checker_count(Program const* me) {
    u64 count = program_checked_source_bytes(me) / CHECKER_BYTES;
    u32 threads = my threads ? my threads : 1;
    return count < 1 ? 1 : count > threads ? threads : (u32)count;
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
    if (!my global_diagnostics_reported) program_report_missing_global_types(me);
    // Each checker reports into its own ErrorCtx; nothing may write to the program's or make symbols meanwhile (they
    // run on threads of their own)
    u32 count = my requested_checkers ? my requested_checkers : auto_checker_count(me);
    my checkers = c_new_n(Checker*, count);
    my checker_count = count;
    // (Split between the checkers, but at least CHECKER_MEMORY_FLOOR each when there's that much: a fresh checker's
    // warm-ups cost more than the memory saves below it, vscode at 4 checkers of 64 MB +6% wall, of 128 +3%)
    u64 floor = my checker_memory < CHECKER_MEMORY_FLOOR ? my checker_memory : CHECKER_MEMORY_FLOOR;
    my checker_budget = my checker_memory == UINT64_MAX ? UINT64_MAX : (my checker_memory / count > floor ? my checker_memory / count : floor);
    ErrorCtx* checker_errs = c_new_n(ErrorCtx, count);
    my binder->frozen = true;
    my errs->frozen = true;
    // (Declaration emit needs the walk even where its diagnostics won't be reported: --dts-dump under noEmit)
    bool declaration_diagnostics =
        my emit_declarations || !(program_declaration_diagnostics_gated(me) && my declaration_diagnostics_earlier);
    my declaration_errs = declaration_diagnostics ? c_new_n(ErrorCtx, count) : nullptr;
    if (my emit_declarations) program_set_declaration_paths(me);
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
    // (Recycled: the first share's last checker)
    my checker = my checkers[0];
    checker_make_current(my checker);

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
    for (u32 i = 0; i < importing->module_ref_count; i++) {
        ModuleRef const* ref = importing->module_refs[i];
        if (ref->is_declaration || ref->ambient_only || ref == importing->helpers_ref || ref == importing->jsx_runtime_ref) continue;
        SpecifierMode mode = program_usage_mode(me, importing, ref);
        imports[import_count++] = (SpecifierImport){ .text = cstr_sv(&ref->path), .mode = mode };
        if (ref->file != target || seen_target) continue;
        seen_target = true;
        if (mode != wanted && mode != SPECIFIER_MODE_NONE && wanted != SPECIFIER_MODE_NONE) continue;
        free(imports);
        return cstr_clone(ref->path);
    }
    csview cwd = program_cwd(me);
    cstr importing_path = absolute_path_of(cwd, cstr_sv(&importing->canonical_path));
    cstr target_path = absolute_path_of(cwd, cstr_sv(&target->canonical_path));
    // (tsc's command line names a referenced project's output declaration file, which it reads in its place)
    // (tsc's command line names a referenced project's source by its output declaration file first, then by itself:
    // GetSourceOfProjectReferenceIfOutputIncluded and GetEachFileNameOfModule's referenceRedirect)
    csview output = target->from_reference ? project_redirects_output_of_source(my redirects, cstr_sv(&target_path)) : c_sv("");
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
    };
    cstr ret = module_specifier_generate(&request);
    c_drop(cstr, &importing_path, &target_path, &project_dir);
    free(imports);
    return ret;
}

static cstr program_common_source_dir(Program* me);
cstr program_module_display_name(Program* me, Symbol const* module, SourceFile const* from, SpecifierMode override, ModuleRef const* usage) {
    SourceFile const* target = module->module ? module->module->file : nullptr;
    // (A JSON file's module symbol has no SourceFile declaration in tsc: named like an ambient module, by its quoted
    // escapedName)
    if (!target || target->is_json || !from) {
        return module_name_for_error(module, nullptr, false);
    }
    SpecifierMode ending = override ? override : usage ? program_usage_mode(me, from, usage) : SPECIFIER_MODE_NONE;
    cstr specifier = program_module_specifier_ending(me, from, target, override, ending);
    cstr ret = cstr_d("\"", specifier, "\"");
    cstr_drop(&specifier);
    return ret;
}

u32 program_serial(Program const* me) {
    return my serial;
}
