// realpath() and PATH_MAX, which strict -std=c2x hides
#define _XOPEN_SOURCE 700
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/resource.h>

#include "ty/ty.h"
#include "type.h"
#include "program.h"
#include "engine.h"
#include "error.h"
#include "display.h"
#include "source_file.h"
#include "string_helpers.h"
#include "tsconfig.h"
#include "project.h"
#include "load_pool.h"
#include "expect_types.h"
#include <cleat/filesystem.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_COMPILER_OPTION_ARGS = 64 };

typedef struct Options {
    bool quiet;   // Only print diagnostics and the final summary
    bool print_ast; // Dump the parsed AST of the (last) file before typechecking
    bool time;    // Print how long each phase took to stderr
    bool keep_going; // Typecheck and report everything even after syntactic or options diagnostics (tsc stops)
    bool no_lib;  // Don't load the standard library, for checking the library files themselves
    const char* project; // -p: a tsconfig.json or the directory of one
    bool build; // -b: paths are projects, each checked with everything it references, in build order
    bool stop_build_on_errors; // --stopBuildOnErrors
    u32 checkers; // --checkers
    u32 threads; // --threads
    const char* expect_types; // --expect-types FILE: print the types of the expressions it lists (expect_types.h)
    u32 build_jobs; // --build-jobs: how many projects -b checks at once (0: decided by threads)
    const char** paths;
    intptr_t path_count;
    // --strictNullChecks false, --target es2020 and so on, in order
    const char* option_names[MAX_COMPILER_OPTION_ARGS];
    const char* option_values[MAX_COMPILER_OPTION_ARGS];
    intptr_t option_count;
} Options;

// Where a program's messages go: straight to stdout and stderr, or (-b checking projects concurrently) into buffers
// that are printed in build order
typedef struct Output {
    bool buffered;
    cstr out, err;
} Output;

static void output_put(Output* me, FILE* file, DisplayCtx d) {
    if (my buffered) {
        cstr_append_sv(file == stdout ? &my out : &my err, cstr_sv(&d.target));
    } else {
        fputs(cstr_str(&d.target), file);
    }
    display_ctx_drop(&d);
}
#define out_println(out, ...) output_put(out, stdout, display_sequence(PRINT_DISPLAY, __VA_OPT__(__VA_ARGS__,) "\n"))
#define out_eprintln(out, ...) output_put(out, stderr, display_sequence(EPRINT_DISPLAY, __VA_OPT__(__VA_ARGS__,) "\n"))

static void output_flush(Output* me) {
    fputs(cstr_str(&my out), stdout);
    fputs(cstr_str(&my err), stderr);
    cstr_clear(&my out);
    cstr_clear(&my err);
}

static double now_ms() {
    struct timespec ts;
    // (C23's own clock rather than clock_gettime(), which strict -std=c2x hides)
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

// The resident set so far, in MB (0 where /proc isn't there)
static long resident_mb() {
    FILE* statm = fopen("/proc/self/statm", "r");
    if (!statm) return 0;
    long size = 0, resident = 0;
    if (fscanf(statm, "%ld %ld", &size, &resident) != 2) resident = 0;
    fclose(statm);
    return resident * 4096 / (1024 * 1024);
}

// Reports the time since *last under the given name and resets *last. Reading and parsing can't be told apart, since
// program_load() does both (and chases imports) in one go
// ./memtrack.sh's LD_PRELOADed live-heap tracker (bench/memtrack/memtrack.c), absent otherwise
void memtrack_dump(char const* name) __attribute__((weak));
void memtrack_scan(char const* name) __attribute__((weak));
static void phase_done(Options const* opts, Output* out, const char* name, double* last) {
    if (memtrack_dump && getenv("MEMTRACK")) {
        char phase[64];
        size_t i = 0;
        for (; name[i] && i < sizeof(phase) - 1; i++) phase[i] = name[i] == ' ' || name[i] == '+' ? '_' : name[i];
        phase[i] = 0;
        memtrack_dump(phase);
        memtrack_scan(phase);
    }
    const double now = now_ms();
    if (opts->time) out_eprintln(out, "time: ", name, " ", now - *last, " ms (resident ", resident_mb(), " MB)");
    *last = now;
}

// The standard library is the repo's thirdparty/lib: relative to the working directory when that's the repo root, else
// found from the executable (release/tttt: its directory's parent is the repo root)
static void set_lib_dir(Program* program) {
    if (access("thirdparty/lib/es5.d.ts", R_OK) == 0) return;
    char exe[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len <= 0) return;
    exe[len] = 0;
    cstr dir = cstr_d(dirname(exe), "/../thirdparty/lib");
    program_set_lib_dir(program, cstr_sv(&dir));
    cstr_drop(&dir);
}

static_failable load_stdlib(Program* program) {
    cstr libs = compiler_options_lib_files(program_options(program));
    csview rest = cstr_sv(&libs);
    while (rest.size) {
        intptr_t end = csview_find_sv(rest, c_sv(","));
        // (A name that isn't a lib, `--lib foo`: tsc reports TS6046 and goes on without it)
        try (SourceFile* lib, program_load_stdlib(program, csview_slice(rest, 0, end), &lib)) or_log(warn);
        ignore lib;
        if (end == c_NPOS) break;
        rest = csview_slice(rest, end + 1, INTPTR_MAX);
    }
    cstr_drop(&libs);
    return ok;
}

// Follows symlinks like the real filesystem does, for the program's module resolution
static cstr real_path_of(void* ctx, csview path) {
    ignore ctx;
    if (path.size >= PATH_MAX) return cstr_null;
    char buf[PATH_MAX];
    memcpy(buf, path.buf, (size_t)path.size);
    buf[path.size] = 0;
    char resolved[PATH_MAX];
    return realpath(buf, resolved) ? cstr_from(resolved) : cstr_null;
}

// For the program's scheduling of parses
static i64 file_size_of(void* ctx, csview path) {
    ignore ctx;
    if (path.size >= PATH_MAX) return 0;
    char buf[PATH_MAX];
    memcpy(buf, path.buf, (size_t)path.size);
    buf[path.size] = 0;
    struct stat info;
    return stat(buf, &info) ? 0 : (i64)info.st_size;
}

// The tsconfig if there is one, then whatever was on the command line
static_failable apply_compiler_options(Options const* opts, Output* out, csview config, Program* program) {
    CompilerOptions* options = program_options(program);
    if (config.size) {
        if (!opts->quiet) out_println(out, color_good, "Project ", config, color_normal);
        // Files on the command line replace the config's
        TsconfigDiagnostics diagnostics = {0};
        try (tsconfig_load_diagnosed(fs, config, options, nullptr, &diagnostics)) cleanup {
            tsconfig_diagnostics_drop(&diagnostics);
        }
        // (Where the diagnostics about the options go, named like the files: relative to the working directory)
        cstr cwd = cstr_null;
        try (fs_get_cwd(fs, &cwd)) cleanup {
            tsconfig_diagnostics_drop(&diagnostics);
        }
        cstr relative = relative_path_of(cstr_sv(&cwd), config);
        program_set_config_file(program, cstr_sv(&relative), true);
        // The config parsing diagnostics of its `extends` (at the name in this config, else without a location)
        cstr absolute = absolute_path_of(cstr_sv(&cwd), config);
        for (u32 i = 0; i < diagnostics.count; i++) {
            TsconfigDiagnostic* d = &diagnostics.items[i];
            // (One in a config it extends has no location: the program doesn't have a file for that one)
            Error* error = cstr_eq(&d->config, &absolute) ?
                program_config_error(program, d->start, d->end, d->code, d->message) :
                error_ctx_global_error_s(program_errors(program), d->code, d->message);
            error->phase = ERROR_PHASE_PARSE;
            d->message = cstr_null;
        }
        tsconfig_diagnostics_drop(&diagnostics);
        c_drop(cstr, &absolute, &relative, &cwd);
    }
    for (intptr_t i = 0; i < opts->option_count; i++) {
        csview name = csview_from(opts->option_names[i]);
        switch (compiler_options_set(options, name, csview_from(opts->option_values[i]))) {
            case COMPILER_OPTION_OK: break;
            case COMPILER_OPTION_UNKNOWN: return err_d("Unknown option --", name);
            case COMPILER_OPTION_BAD_VALUE:
                return err_d("Bad value '", opts->option_values[i], "' for option --", name);
        }
    }
    compiler_options_resolve(options);
    return ok;
}

// last: nothing is checked after it (-b checks a program per project)
static void destroy_program(Program* program, ErrorCtx* errs, [[maybe_unused]] bool last) {
#ifndef DEBUG_BUILD
    // The process is about to exit: freeing the AST node by node was a tenth of a run on a real project. Set
    // TTTT_TEARDOWN to look for leaks with a release build
    if (last && !getenv("TTTT_TEARDOWN")) return;
#endif
    program_destroy(program);
    error_ctx_drop(errs);
    free(errs);
}

// Prints and clears whatever errors have accumulated so far, returning how many there were
static intptr_t report_errors(Output* out, ErrorCtx* errs) {
    if (error_ctx_empty(errs)) {
        return 0;
    }
    const intptr_t count = vec_error_size(&errs->errors);
    if (out->buffered) {
        display_error_ctx(errs, &out->out);
    } else {
        print_error_ctx(errs);
    }
    error_ctx_clear(errs);
    return count;
}

// Which of tsc's diagnostic kinds an error is, for emitFilesAndReportErrors' gating (tsc -p and -b alike): the config
// file's parsing diagnostics are always reported; syntactic ones (parse errors) hide everything else; options and global
// diagnostics (without a file, or in the config file) hide the semantic ones (everything located in a source file that
// isn't a parse error, file inclusion diagnostics at an import included)
typedef enum DiagnosticKind {
    DIAGNOSTIC_CONFIG_PARSING,
    DIAGNOSTIC_SYNTACTIC,
    DIAGNOSTIC_OPTIONS,
    DIAGNOSTIC_SEMANTIC,
    DIAGNOSTIC_DECLARATION, // Not gated when emitting: the emit reports them
    DIAGNOSTIC_KIND_COUNT
} DiagnosticKind;

static DiagnosticKind diagnostic_kind(Error const* error) {
    bool in_config = !span_valid(error->span) || (error->span.source && error->span.source->is_config);
    if (in_config) return error->phase == ERROR_PHASE_PARSE ? DIAGNOSTIC_CONFIG_PARSING : DIAGNOSTIC_OPTIONS;
    if (error->phase == ERROR_PHASE_PARSE && !error->grammar) return DIAGNOSTIC_SYNTACTIC;
    return error->declaration ? DIAGNOSTIC_DECLARATION : DIAGNOSTIC_SEMANTIC;
}

static void count_diagnostic_kinds(ErrorCtx const* errs, intptr_t counts[DIAGNOSTIC_KIND_COUNT]) {
    for (DiagnosticKind k = 0; k < DIAGNOSTIC_KIND_COUNT; k++) counts[k] = 0;
    for_each (i, vec_error, &errs->errors) counts[diagnostic_kind(i)]++;
}

// Drops the errors of the kind (what tsc doesn't ask the program for)
static void drop_diagnostic_kind(ErrorCtx* errs, DiagnosticKind kind) {
    VecError* errors = &errs->errors;
    intptr_t kept = 0;
    for (intptr_t i = 0; i < vec_error_size(errors); i++) {
        Error* error = vec_error_at_mut(errors, i);
        if (diagnostic_kind(error) == kind) {
            error_drop(error);
            continue;
        }
        if (kept != i) errors->data[kept] = *error;
        kept++;
    }
    errors->size = kept;
}

// What one program found, for -b's summary
typedef struct CheckCounts {
    intptr_t errors;
} CheckCounts;

// tsc's verifyProjectReferences, for the references of the program's own config: on the reference
static void verify_references(Program* program, ProjectSet const* projects, u32 project, bool has_inputs) {
    Project const* p = project_set_at(projects, project);
    for (u32 i = 0; i < p->reference_count; i++) {
        TsconfigReference const* reference = &p->references[i];
        Project const* referenced = project_set_at(projects, p->referenced[i]);
        // (tsc's verifyDeprecatedProjectReference)
        if (reference->prepend) {
            program_config_error(program, reference->start, reference->end, 5102,
                cstr_from("Option 'prepend' has been removed. Please remove it from your configuration."));
        }
        if (!referenced->readable) {
            program_config_error(program, reference->start, reference->end, 6053,
                cstr_d("File '", reference->written, "' not found."));
            continue;
        }
        // (A program without files of its own, a container, may reference anything)
        if (!has_inputs) continue;
        if (!referenced->options.composite) {
            program_config_error(program, reference->start, reference->end, 6306,
                cstr_d("Referenced project '", reference->written, "' must have setting \"composite\": true."));
        }
        if (referenced->options.no_emit) {
            program_config_error(program, reference->start, reference->end, 6310,
                cstr_d("Referenced project '", reference->written, "' may not disable emit."));
        }
    }
}

// Checks one program, logically returning the exit code to leave with: the files on the command line (with the
// config's options if there's a config), else the config's files. With a config the program sees the projects it
// references through their sources (projects is where they're loaded, project the config's). Problems with the checked
// program are printed as TypeScript diagnostics and reflected in that exit code; only failures that stop us checking
// at all (an unreadable standard library or entry point) come back as errors. redirects: what the config references
// (null without a config), the caller's to free after the program. last: nothing is checked after it
static_failable check_files(Options const* opts, Output* out, csview config, ProjectSet* projects, u32 project,
    ProjectRedirects const* redirects, bool last, CheckCounts* counts, returns int
) {
    ErrorCtx* errs = c_new(ErrorCtx, error_ctx_init());
    Program* program = program_new(errs, fs);
    program_set_realpath(program, real_path_of, nullptr);
    program_set_file_size(program, file_size_of, nullptr);
    program_set_checkers(program, opts->checkers);
    set_lib_dir(program);
    program_follow_lib_references(program);
    StrList root_files = {0};
    try (apply_compiler_options(opts, out, config, program)) cleanup {
        destroy_program(program, errs, last);
    }
    // The config's root files, unless there are files on the command line
    if (config.size && !opts->path_count) {
        StrList const* listed = project_set_root_files(projects, project);
        for (isize i = 0; i < listed->count; i++) str_list_push(&root_files, cstr_sv(&listed->items[i]));
    }
    // (-p reads a referenced project's built outputs like tsc's command line, -b its sources: they stand for the
    // outputs its build would have written)
    if (project_redirects_count(redirects)) program_set_references(program, redirects, !opts->build);
    program_set_threads(program, opts->threads);
    // (Libraries' errors are dropped once they're loaded: these come after)
    bool has_inputs = opts->path_count + root_files.count > 0;
    if (!has_inputs) {
        if (config.size) verify_references(program, projects, project, false);
        bool no_inputs = config.size && program_report_no_inputs(program);
        if (config.size) program_verify_options(program);
        // A solution config: only references (tsc says nothing, and builds nothing without -b)
        bool solution = config.size && project_set_at(projects, project)->reference_count;
        intptr_t config_errors = report_errors(out, errs);
        if (counts) counts->errors += config_errors;
        str_list_drop(&root_files);
        destroy_program(program, errs, last);
        if (solution || no_inputs) {
            if (solution && !opts->quiet && !opts->build) out_println(out, "Only project references to check: use -b to check them");
            return ok(config_errors ? 1 : 0);
        }
        return err_d("No input files: none on the command line and none from a tsconfig.json");
    }
    double last_time = now_ms();

    {
        // Parsing on the side: what the loader will want, as far as that's known now
        cstr libs = opts->no_lib ? cstr_null : compiler_options_lib_files(program_options(program));
        isize lib_count = 0, root_count = opts->path_count + root_files.count;
        csview* lib_names = c_new_n(csview, cstr_size(&libs) + 1);
        for (csview rest = cstr_sv(&libs); rest.size;) {
            intptr_t end = csview_find_sv(rest, c_sv(","));
            lib_names[lib_count++] = csview_slice(rest, 0, end);
            if (end == c_NPOS) break;
            rest = csview_slice(rest, end + 1, INTPTR_MAX);
        }
        csview* roots = c_new_n(csview, root_count + 1);
        for (isize i = 0; i < root_count; i++) {
            roots[i] = i < opts->path_count ? csview_from(opts->paths[i]) : cstr_sv(&root_files.items[i - opts->path_count]);
        }
        program_load_ahead(program, lib_names, lib_count, roots, root_count);
        c_free_n(roots, root_count + 1);
        c_free_n(lib_names, cstr_size(&libs) + 1);
        cstr_drop(&libs);
    }

    if (!opts->no_lib) {
        if (!opts->quiet) out_println(out, color_good, "Loading libraries...", color_normal);
        // (What's been reported so far, the config's syntax errors, is kept aside from the libraries' errors)
        VecError earlier = errs->errors;
        errs->errors = (VecError){0};
        try (load_stdlib(program)) cleanup {
            vec_error_drop(&earlier);
            destroy_program(program, errs, last);
        }

        if (!error_ctx_empty(errs)) {
            const intptr_t lib_errors = vec_error_size(&errs->errors);
            out_println(out, "Encountered ", color_error, lib_errors, " errors", color_normal, " loading libraries.");
            //print_error_ctx(errs);
            error_ctx_clear(errs);
        }
        vec_error_drop(&errs->errors);
        errs->errors = earlier;
    }
    phase_done(opts, out, "load+parse libs", &last_time);
    if (config.size) verify_references(program, projects, project, true);

    SourceFile* source = nullptr;
    for (intptr_t i = 0; i < opts->path_count + root_files.count; i++) {
        csview path = i < opts->path_count ? csview_from(opts->paths[i]) : cstr_sv(&root_files.items[i - opts->path_count]);
        if (!opts->quiet) out_println(out, color_good, "Loading ", path, "...", color_normal);
        bool from_config = i >= opts->path_count;
        FailableResult loaded = program_load(program, path, &source);
        if (loaded.error && from_config) {
            // A `files` entry that doesn't exist: tsc's TS6053, and the rest is checked anyway
            failable_result_drop(&loaded);
            Error* error = error_ctx_global_error_d(errs, 6053, "File '", path, "' not found.");
            error_sub_error_d(error, span_null, TS_CODE_NONE, "The file is in the program because:");
            error_sub_error_d(
                vec_error_back_mut(&error->sub_errors), span_null, TS_CODE_NONE, "Part of 'files' list in tsconfig.json"
            );
            continue;
        }
        try (loaded) cleanup {
            str_list_drop(&root_files);
            destroy_program(program, errs, last);
        }
    }
    str_list_drop(&root_files);
    program_load_automatic_types(program);
    phase_done(opts, out, "load+parse files", &last_time);

    int ret = 0;
    // tsc's emitFilesAndReportErrors: semantic diagnostics only without syntactic, options and global ones. With
    // noEmitOnError, emitting (handleNoEmitOptions) reports all of them anyway, ungated
    CompilerOptions const* options = program_options(program);
    bool ungated = opts->keep_going || (options->no_emit_on_error && !options->no_emit);
    intptr_t kinds[DIAGNOSTIC_KIND_COUNT];
    count_diagnostic_kinds(errs, kinds);
    bool syntactic = kinds[DIAGNOSTIC_SYNTACTIC];
    bool gated = !ungated && (syntactic || kinds[DIAGNOSTIC_OPTIONS]);
    if (!ungated && kinds[DIAGNOSTIC_SYNTACTIC]) drop_diagnostic_kind(errs, DIAGNOSTIC_OPTIONS);
    if (gated) drop_diagnostic_kind(errs, DIAGNOSTIC_SEMANTIC);
    const intptr_t parse_errors = report_errors(out, errs);
    if (parse_errors) ret = 1;

    // Gated, the emit still reports the declaration diagnostics: checked only for them. Gated by options diagnostics
    // alone, the rest of them (program_resolve_names() validates the options) are still reported
    bool emit_declarations = !options->no_emit && (options->declaration || options->composite);
    bool typecheck = !gated || emit_declarations;
    intptr_t name_errors = 0, type_errors = 0;
    if (typecheck || !syntactic) {
        program_resolve_names(program);
        // (Not reported yet: global diagnostics found from here on hide them)
        ErrorCtx names = error_ctx_scratch(errs);
        names.errors = errs->errors;
        errs->errors = (VecError){0};
        phase_done(opts, out, "resolve names", &last_time);

        if (opts->print_ast && source) {
            out_println(out, color_good, "Parsed:", color_normal);
            out_println(out, debug_d(&source->ast), "\n");
        }

        program_declaration_diagnostics_cli(program, parse_errors || !error_ctx_empty(&names));
        if (typecheck) program_typecheck(program);
        // Global diagnostics (the checker's, without a file) hide the semantic ones
        count_diagnostic_kinds(&names, kinds);
        intptr_t type_kinds[DIAGNOSTIC_KIND_COUNT];
        count_diagnostic_kinds(errs, type_kinds);
        if (gated || (!ungated && (kinds[DIAGNOSTIC_OPTIONS] || type_kinds[DIAGNOSTIC_OPTIONS]))) {
            drop_diagnostic_kind(&names, DIAGNOSTIC_SEMANTIC);
            drop_diagnostic_kind(errs, DIAGNOSTIC_SEMANTIC);
        }
        if (gated && syntactic) {
            drop_diagnostic_kind(&names, DIAGNOSTIC_OPTIONS);
            drop_diagnostic_kind(errs, DIAGNOSTIC_OPTIONS);
        }
        name_errors = report_errors(out, &names);
        error_ctx_drop(&names);
        if (name_errors) ret = 1;

        if (error_ctx_empty(errs)) {
            out_println(out, color_good, "Typecheck passed", color_normal);
        } else {
            out_println(out, color_error, "Typecheck failed", color_normal);
        }

        type_errors = report_errors(out, errs);
        if (type_errors) ret = 1;
        phase_done(opts, out, "typecheck", &last_time);
        if (opts->expect_types && typecheck) {
            output_flush(out);
            program_print_expect_types(program, opts->expect_types);
        }
        if (opts->time) {
            for (u32 i = 0; i < program_checker_count(program); i++) {
                Checker const* checker = program_checker_at(program, i);
                out_eprintln(out, "time: checker ", i, ": ", checker->check_ms, " ms, arena ", (i64)(checker->arena.total >> 20),
                    " MB, ", checker->next_type_id, " types");
                // (Comparable with tsc --extendedDiagnostics's cache sizes, which count failures and nested pairs too)
                out_eprintln(out, "time: checker ", i, " relation cache: assignable ", checker->relations[TY_RELATION_ASSIGNABLE].len,
                    ", comparable ", checker->relations[TY_RELATION_COMPARABLE].len, ", subtype ",
                    checker->relations[TY_RELATION_SUBTYPE].len, ", strict subtype ", checker->relations[TY_RELATION_STRICT_SUBTYPE].len,
                    ", identity ", checker->relations[TY_RELATION_IDENTITY].len);
            }
#ifdef TTTT_ARENA_SITES
            arena_sites_print();
#endif
        }
    }

    if (parse_errors || name_errors || type_errors) {
        out_println(out,
            "Checked ", program_get_file_count(program), " files, found ",
            parse_errors ? color_error : color_none, parse_errors, " parse errors", color_normal, ", ",
            name_errors  ? color_error : color_none, name_errors,  " name errors",  color_normal, " and ",
            type_errors  ? color_error : color_none, type_errors,  " type errors",  color_normal, "."
        );
    } else {
        out_println(out, color_good, "Checked ", program_get_file_count(program), " files, no errors found!", color_normal);
    }

    //println(color_good, "Running...", color_normal);
    //run_in_engine(source, arc_js_object_init());
    //println(color_good, "Run succeeded", color_normal);

    if (counts) counts->errors += parse_errors + name_errors + type_errors;
    destroy_program(program, errs, last);
    if (!last) phase_done(opts, out, "free", &last_time);
    return ok(ret);
}

// Without -b: the files on the command line, else the project (-p, else the nearest tsconfig.json)
static_failable check_command_line(Options const* opts, returns int) {
    cstr config = opts->project ? cstr_from(opts->project) :
        opts->path_count ? cstr_null : tsconfig_find(fs, c_sv("."));
    if (cstr_size(&config) && fs_is_directory(fs, cstr_sv(&config))) cstr_append_lit(&config, "/tsconfig.json");
    ProjectSet* projects = project_set_new(fs);
    u32 project = cstr_size(&config) ? project_set_load(projects, cstr_sv(&config)) : 0;
    ProjectRedirects* redirects = cstr_size(&config) ? project_redirects_new(projects, project) : nullptr;
    Output out = {0};
    FailableResult result =
        check_files(opts, &out, cstr_sv(&config), projects, project, redirects, true, nullptr, _OUTPUT_PARAM);
    // (The program may not have been freed: a release build leaves that to the exit)
#ifdef DEBUG_BUILD
    project_redirects_destroy(redirects);
    project_set_destroy(projects);
#endif
    cstr_drop(&config);
    return result;
}

// One project of a -b build: checked on a build thread, its output kept for the main thread to print in build order
typedef struct BuildJob {
    Output out;
    CheckCounts counts;
    ProjectRedirects* redirects;
    FailableResult result;
    int ret;
    bool done; // Under Build.lock
} BuildJob;

typedef struct Build {
    Options const* opts;
    Options project_opts; // The command line's options for each project, without its paths
    ProjectSet const* projects;
    u32 const* order;
    u32 count;
    BuildJob* jobs;
    atomic_uint next; // The next job a build thread takes
    atomic_uint end; // Nothing from here on is started (--stopBuildOnErrors)
    pthread_mutex_t lock;
    pthread_cond_t done;
} Build;

static void build_run(Build* me, u32 i) {
    BuildJob* job = &my jobs[i];
    Project const* project = project_set_at(my projects, my order[i]);
    if (!project->readable) {
        ErrorCtx errs = error_ctx_init();
        error_ctx_global_error_d(&errs, 5083, "Cannot read file '", project->config, "'.");
        job->counts.errors += report_errors(&job->out, &errs);
        error_ctx_drop(&errs);
        job->ret = 1;
        return;
    }
    // (The project set is complete and only read from here on: build() listed every root file up front)
    job->result = check_files(&my project_opts, &job->out, cstr_sv(&project->config), (ProjectSet*)my projects,
        my order[i], job->redirects, i + 1 == my count, &job->counts, &job->ret);
}

static void* build_thread(void* arg) {
    Build* me = arg;
    for (u32 i; (i = atomic_fetch_add(&my next, 1)) < atomic_load(&my end);) {
        build_run(me, i);
        pthread_mutex_lock(&my lock);
        my jobs[i].done = true;
        pthread_cond_broadcast(&my done);
        pthread_mutex_unlock(&my lock);
    }
    return nullptr;
}

// -b: every project the paths name (tsconfig.json files or their directories, `.` without any), each with what it
// references before it (tsc's build order). Each is its own program, with its own options and the command line's, and
// sees its references as sources. Like tsc 5.6+ goes on past a project with errors (not with --stopBuildOnErrors).
// With sources rather than built outputs no program needs another's result: with --threads 4, two projects are checked
// at once on two threads each (opts->build_jobs), their output printed in build order, so it doesn't depend on that
static_failable build(Options const* opts, returns int) {
    ProjectSet* projects = project_set_new(fs);
    isize root_count = opts->path_count ? opts->path_count : 1;
    u32* roots = c_new_n(u32, root_count);
    for (isize i = 0; i < root_count; i++) {
        csview path = opts->path_count ? csview_from(opts->paths[i]) : c_sv(".");
        roots[i] = project_set_load(projects, path);
    }
    u32* order = nullptr;
    StrList cycles = {0};
    u32 count = project_set_build_order(projects, roots, (u32)root_count, &order, &cycles);
    c_free_n(roots, root_count);
    int ret = 0;
    Output direct = {0};
    if (cycles.count) {
        ErrorCtx errs = error_ctx_init();
        for (isize i = 0; i < cycles.count; i++) error_ctx_global_error_s(&errs, 6202, cstr_clone(cycles.items[i]));
        report_errors(&direct, &errs);
        error_ctx_drop(&errs);
        ret = 1;
        count = 0; // (tsc builds nothing then)
    }
    str_list_drop(&cycles);

    u32 jobs = opts->build_jobs ? opts->build_jobs : opts->threads >= 4 ? 2 : 1;
    if (jobs > count) jobs = count ? count : 1;
    Build b = { .opts = opts, .project_opts = *opts, .projects = projects, .order = order, .count = count };
    b.project_opts.path_count = 0;
    b.project_opts.threads = opts->threads / jobs ? opts->threads / jobs : 1;
    b.jobs = calloc((size_t)count + 1, sizeof(BuildJob));
    atomic_init(&b.next, 0);
    atomic_init(&b.end, count);
    // Everything the programs read of the project set, made before any of them runs
    for (u32 i = 0; i < count; i++) {
        b.jobs[i].out.buffered = jobs > 1;
        if (!project_set_at(projects, order[i])->readable) continue;
        project_set_root_files(projects, order[i]);
        b.jobs[i].redirects = project_redirects_new(projects, order[i]);
    }
    pthread_t* threads = nullptr;
    if (jobs > 1) {
        pthread_mutex_init(&b.lock, nullptr);
        pthread_cond_init(&b.done, nullptr);
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, worker_stack_size());
        threads = c_new_n(pthread_t, jobs);
        for (u32 t = 0; t < jobs; t++) {
            panic_if(pthread_create(&threads[t], &attr, build_thread, &b), "couldn't start a build thread");
        }
        pthread_attr_destroy(&attr);
    }

    CheckCounts counts = {0};
    u32 failed = 0;
    FailableResult failure = {0};
    for (u32 i = 0; i < count; i++) {
        BuildJob* job = &b.jobs[i];
        if (jobs > 1) {
            pthread_mutex_lock(&b.lock);
            while (!job->done) pthread_cond_wait(&b.done, &b.lock);
            pthread_mutex_unlock(&b.lock);
            output_flush(&job->out);
        } else {
            build_run(&b, i);
        }
        if (job->result.error) {
            // (What stops us checking at all: a later project's error isn't any less fatal, but this one comes first)
            failure = job->result;
            job->result = (FailableResult){0};
            atomic_store(&b.end, 0);
            break;
        }
        counts.errors += job->counts.errors;
        if (job->ret || job->counts.errors) {
            failed++;
            ret = 1;
            if (opts->stop_build_on_errors) {
                atomic_store(&b.end, 0);
                break;
            }
        }
    }
    if (threads) {
        for (u32 t = 0; t < jobs; t++) pthread_join(threads[t], nullptr);
        free(threads);
        pthread_cond_destroy(&b.done);
        pthread_mutex_destroy(&b.lock);
    }
    for (u32 i = 0; i < count; i++) {
        // (Whatever was checked past where the build stopped isn't printed)
        cstr_drop(&b.jobs[i].out.out);
        cstr_drop(&b.jobs[i].out.err);
        failable_result_drop(&b.jobs[i].result);
        project_redirects_destroy(b.jobs[i].redirects);
    }
    free(b.jobs);
    free(order);
    if (failure.error) {
#ifdef DEBUG_BUILD
        project_set_destroy(projects);
#endif
        return failure;
    }
    if (!opts->quiet || failed) {
        println(failed ? color_error : color_good, "Checked ", count, count == 1 ? " project" : " projects", ", ",
            failed, " with errors (", counts.errors, " errors)", color_normal);
    }
#ifdef DEBUG_BUILD
    project_set_destroy(projects);
#endif
    return ok(ret);
}

// glibc's malloc can back its heap with transparent huge pages, which takes a tenth off the wall time of a big project
// (the page faults of a few hundred MB of AST and types were ~18% of it), but only takes the setting from the
// environment at startup: start over once with it set. Not when GLIBC_TUNABLES says something already (the user's,
// or ours after the re-exec), under valgrind (the new process would run outside the tool), or with TTTT_NO_REEXEC
static void reexec_with_huge_pages(char const* const* argv) {
#ifdef __GLIBC__
    if (getenv("GLIBC_TUNABLES") || getenv("TTTT_NO_REEXEC")) return;
    char const* preload = getenv("LD_PRELOAD");
    if (preload && strstr(preload, "vgpreload")) return;
    if (setenv("GLIBC_TUNABLES", "glibc.malloc.hugetlb=1", 1) != 0) return;
    execv("/proc/self/exe", (char* const*)argv);
    unsetenv("GLIBC_TUNABLES"); // (Carry on as we are if that didn't work)
#else
    (void)argv;
#endif
}

static int run(int argc, const char** argv) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    Options opts = { .paths = argv, .checkers = 4, .threads = cores < 1 ? 1 : cores > 4 ? 4 : (u32)cores };
    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "--help") || streq(argv[i], "-h")) {
            println(
                "The Tess TypeScript Typechecker\n\nUSAGE: tttt [OPTIONS] [TSPATH...]\n\n"
                "  -q, --quiet   only print diagnostics and the summary\n"
                "  --time        print how long each phase took to stderr\n"
                "  --print-ast   print the parsed AST of the last file (debugging)\n"
                "  -p, --project PATH  a tsconfig.json or its directory: options and, without TSPATHs, the files.\n"
                "                Without TSPATHs and -p, the nearest tsconfig.json from here upwards. Referenced\n"
                "                projects are read from their sources, not built outputs, and aren't checked\n"
                "  -b, --build [PROJECT...]  check each project (`.` by default) and everything it references, each\n"
                "                with its own options, references first (-v, -f are accepted and ignored)\n"
                "  --stopBuildOnErrors  with -b, stop after the first project with errors\n"
                "  --build-jobs J  with -b, check J projects at once, each on threads/J threads (default: 2 with\n"
                "                4 threads, else 1). The output doesn't depend on it\n"
                "  --no-lib      don't load the standard library\n"
                "  --expect-types FILE  after checking, print the types of the expressions FILE lists by span\n"
                "  --keep-going  typecheck even after parse or options errors\n"
                "  --all-errors  print every diagnostic, not just the first 5000 of each phase\n"
                "  --checkers K  check with K checkers (default 4): each gets contiguous runs of the files. Only the\n"
                "                order of union members in messages depends on K\n"
                "  --threads N   run the checkers on N threads (default: the number of cores, at most 4)\n"
                "  --OPTION [VALUE]  a tsc compiler option: --target es2020, --lib es2015,dom, --strict false,\n"
                "                --noImplicitAny... (strict is on by default, a boolean without a value means true)"
            );
            return 0;
        } else if (streq(argv[i], "--quiet") || streq(argv[i], "-q")) {
            opts.quiet = true;
        } else if (streq(argv[i], "--print-ast")) {
            opts.print_ast = true;
        } else if (streq(argv[i], "--time")) {
            opts.time = true;
        } else if (streq(argv[i], "--keep-going")) {
            opts.keep_going = true;
        } else if (streq(argv[i], "--all-errors")) {
            print_error_ctx_max_display = INTPTR_MAX;
        } else if ((streq(argv[i], "-p") || streq(argv[i], "--project")) && i + 1 < argc) {
            opts.project = argv[++i];
        } else if (streq(argv[i], "-b") || streq(argv[i], "--build")) {
            opts.build = true;
        } else if (opts.build && (streq(argv[i], "-v") || streq(argv[i], "--v") || streq(argv[i], "--verbose") ||
            streq(argv[i], "-f") || streq(argv[i], "--f") || streq(argv[i], "--force"))
        ) {
            // (Nothing is built, so there's nothing to say about it, nor anything up to date)
        } else if (streq(argv[i], "--stopBuildOnErrors")) {
            opts.stop_build_on_errors = true;
        } else if (opts.build && (streq(argv[i], "--dry") || streq(argv[i], "-d") || streq(argv[i], "--clean") ||
            streq(argv[i], "-w") || streq(argv[i], "--watch"))
        ) {
            println(color_error, "-b ", argv[i], " isn't supported: tttt only checks", color_normal);
            return 1;
        } else if (streq(argv[i], "--checkers") && i + 1 < argc && atoi(argv[i + 1]) > 0) {
            opts.checkers = (u32)atoi(argv[++i]);
        } else if (streq(argv[i], "--threads") && i + 1 < argc && atoi(argv[i + 1]) > 0) {
            opts.threads = (u32)atoi(argv[++i]);
        } else if (streq(argv[i], "--build-jobs") && i + 1 < argc && atoi(argv[i + 1]) > 0) {
            opts.build_jobs = (u32)atoi(argv[++i]);
        } else if (streq(argv[i], "--expect-types") && i + 1 < argc) {
            opts.expect_types = argv[++i];
        } else if (streq(argv[i], "--no-lib")) {
            opts.no_lib = true;
        } else if (argv[i][0] == '-' && argv[i][1] == '-') {
            if (opts.option_count == MAX_COMPILER_OPTION_ARGS) {
                println(color_error, "Too many options", color_normal);
                return 1;
            }
            // Whether this is an option we know is checked once there's a CompilerOptions to try it on. A boolean
            // option may come without a value, anything else has one
            bool has_value = i + 1 < argc && argv[i + 1][0] != '-' && (
                !compiler_option_is_boolean(csview_from(argv[i] + 2)) ||
                streq(argv[i + 1], "true") || streq(argv[i + 1], "false") || streq(argv[i + 1], "null")
            );
            CompilerOptions scratch = compiler_options_init();
            bool unknown = compiler_options_set(&scratch, csview_from(argv[i] + 2), c_sv("")) == COMPILER_OPTION_UNKNOWN;
            compiler_options_drop(&scratch);
            if (unknown) {
                println(color_error, "Unknown option ", argv[i], color_normal);
                return 1;
            }
            opts.option_names[opts.option_count] = argv[i] + 2;
            opts.option_values[opts.option_count++] = has_value ? argv[++i] : "true";
        } else {
            // Paths are compacted to the front of argv, which is fine as they never outnumber what we've already read
            opts.paths[opts.path_count++] = argv[i];
        }
    }

    {
        // (tsc's CLI names files in message text by absolute path)
        cstr cwd = cstr_null;
        if (!fs_get_cwd(fs, &cwd).error) source_file_set_message_cwd(cstr_sv(&cwd));
        cstr_drop(&cwd);
    }
    try (int ret, opts.build ? build(&opts, &ret) : check_command_line(&opts, &ret)) if_err (e) {
        error_base_eprintln(e);
        current_error_destroy();
        return 1;
    }

    return ret;
}

// The checker recurses per nesting level, and tsc's tail-recursive conditional types (1000 steps of `A<...> extends X ?
// ... : A<...>`, type-fest's IntRange) recurse here too: 8 MB of main thread stack isn't enough. The main thread's
// stack grows up to the limit as it is when it grows (and the re-exec starts with it), so raise it. (Not a thread of
// our own with a big stack: glibc's malloc gives other threads a slower arena, +0.2-0.9% instructions)
static void raise_stack_limit(void) {
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit) || limit.rlim_cur == RLIM_INFINITY || limit.rlim_cur >= worker_stack_size()) return;
    limit.rlim_cur = limit.rlim_max != RLIM_INFINITY && limit.rlim_max < worker_stack_size() ? limit.rlim_max : worker_stack_size();
    setrlimit(RLIMIT_STACK, &limit);
}

int main(int argc, const char** argv) {
    raise_stack_limit();
    reexec_with_huge_pages(argv);
    return run(argc, argv);
}
