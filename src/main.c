// realpath() and PATH_MAX, which strict -std=c2x hides
#define _XOPEN_SOURCE 700
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>

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
#include <cleat/filesystem.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

// What gets loaded when neither --lib nor --target say otherwise (tsc would load just lib.d.ts, which makes for..of
// over anything but an array an error; bench.sh gives tsc the same list)
static const char* const default_libs =
    "es5,dom,webworker.importscripts,scripthost,es2015.collection,es2015.iterable,es2015.generator";

enum { MAX_COMPILER_OPTION_ARGS = 64 };

typedef struct Options {
    bool quiet;   // Only print diagnostics and the final summary
    bool print_ast; // Dump the parsed AST of the (last) file before typechecking
    bool time;    // Print how long each phase took to stderr
    bool keep_going; // Typecheck even if there were parse or name errors
    bool no_lib;  // Don't load the standard library, for checking the library files themselves
    const char* project; // -p: a tsconfig.json or the directory of one
    bool build; // -b: paths are projects, each checked with everything it references, in build order
    bool stop_build_on_errors; // --stopBuildOnErrors
    u32 checkers; // --checkers
    u32 threads; // --threads
    const char** paths;
    intptr_t path_count;
    // --strictNullChecks false, --target es2020 and so on, in order
    const char* option_names[MAX_COMPILER_OPTION_ARGS];
    const char* option_values[MAX_COMPILER_OPTION_ARGS];
    intptr_t option_count;
} Options;

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
static void phase_done(Options const* opts, const char* name, double* last) {
    const double now = now_ms();
    if (opts->time) eprintln("time: ", name, " ", now - *last, " ms (resident ", resident_mb(), " MB)");
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
        try (SourceFile* lib, program_load_stdlib(program, csview_slice(rest, 0, end), &lib)) cleanup {
            cstr_drop(&libs);
        }
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

// The tsconfig if there is one (else strict, unless told otherwise), then whatever was on the command line. The
// config's root files are appended to root_files, unless there are files on the command line
static_failable apply_compiler_options(Options const* opts, csview config, Program* program, StrList* root_files) {
    CompilerOptions* options = program_options(program);
    bool libs_decided = false;
    if (config.size) {
        if (!opts->quiet) println(color_good, "Project ", config, color_normal);
        // Files on the command line replace the config's
        try (tsconfig_load(fs, config, options, opts->path_count && !opts->build ? nullptr : root_files)) bubble;
        libs_decided = options->target_set || cstr_size(&options->lib);
        // (Where the diagnostics about the options go, named like the files: relative to the working directory)
        cstr cwd = cstr_null;
        try (fs_get_cwd(fs, &cwd)) bubble;
        cstr relative = relative_path_of(cstr_sv(&cwd), config);
        program_set_config_file(program, cstr_sv(&relative));
        cstr_drop(&relative);
        cstr_drop(&cwd);
    } else {
        ignore compiler_options_set(options, c_sv("strict"), c_sv("true"));
    }
    for (intptr_t i = 0; i < opts->option_count; i++) {
        csview name = csview_from(opts->option_names[i]);
        switch (compiler_options_set(options, name, csview_from(opts->option_values[i]))) {
            case COMPILER_OPTION_OK: break;
            case COMPILER_OPTION_UNKNOWN: return err_d("Unknown option --", name);
            case COMPILER_OPTION_BAD_VALUE:
                return err_d("Bad value '", opts->option_values[i], "' for option --", name);
        }
        if (csview_equals(name, "lib") || csview_equals(name, "target")) libs_decided = true;
    }
    if (!libs_decided) ignore compiler_options_set(options, c_sv("lib"), csview_from(default_libs));
    compiler_options_resolve(options);
    return ok;
}

// last: nothing is checked after it (-b checks a program per project)
static void destroy_program(Program* program, ErrorCtx* errs, bool last) {
#ifndef DEBUG_BUILD
    // The process is about to exit: freeing the AST node by node was a tenth of a run on a real project. Set
    // TTTT_TEARDOWN to look for leaks with a release build
    if (last && !getenv("TTTT_TEARDOWN")) return;
#endif
    program_destroy(program);
    error_ctx_drop(errs);
}

// Prints and clears whatever errors have accumulated so far, returning how many there were
static intptr_t report_errors(ErrorCtx* errs) {
    if (error_ctx_empty(errs)) {
        return 0;
    }
    const intptr_t count = vec_error_size(&errs->errors);
    print_error_ctx(errs);
    error_ctx_clear(errs);
    return count;
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
        if (!referenced->readable) {
            program_config_error(program, reference->start, reference->end, 6053,
                cstr_d("File '", reference->path, "' not found."));
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
// at all (an unreadable standard library or entry point) come back as errors. last: nothing is checked after it
static_failable check_files(
    Options const* opts, csview config, ProjectSet* projects, u32 project, bool last, CheckCounts* counts, returns int
) {
    ErrorCtx errs = error_ctx_init();
    Program* program = program_new(&errs, fs);
    program_set_realpath(program, real_path_of, nullptr);
    program_set_file_size(program, file_size_of, nullptr);
    program_set_checkers(program, opts->checkers);
    set_lib_dir(program);
    program_follow_lib_references(program);
    StrList root_files = {0};
    try (apply_compiler_options(opts, config, program, &root_files)) cleanup {
        str_list_drop(&root_files);
        destroy_program(program, &errs, last);
    }
    ProjectRedirects* redirects = nullptr;
    if (config.size) {
        redirects = project_redirects_new(projects, project);
        if (project_redirects_count(redirects)) {
            program_set_references(program, redirects);
        } else {
            project_redirects_destroy(redirects);
            redirects = nullptr;
        }
    }
    program_set_threads(program, opts->threads);
    // (Libraries' errors are dropped once they're loaded: these come after)
    bool has_inputs = opts->path_count + root_files.count > 0;
    if (!has_inputs) {
        if (config.size) verify_references(program, projects, project, false);
        bool no_inputs = config.size && program_report_no_inputs(program);
        // A solution config: only references (tsc says nothing, and builds nothing without -b)
        bool solution = config.size && project_set_at(projects, project)->reference_count;
        intptr_t config_errors = report_errors(&errs);
        if (counts) counts->errors += config_errors;
        str_list_drop(&root_files);
        destroy_program(program, &errs, last);
        project_redirects_destroy(redirects);
        if (solution || no_inputs) {
            if (solution && !opts->quiet && !opts->build) println("Only project references to check: use -b to check them");
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
        if (!opts->quiet) println(color_good, "Loading libraries...", color_normal);
        try (load_stdlib(program)) cleanup {
            destroy_program(program, &errs, last);
            project_redirects_destroy(redirects);
        }

        if (!error_ctx_empty(&errs)) {
            const intptr_t lib_errors = vec_error_size(&errs.errors);
            println("Encountered ", color_error, lib_errors, " errors", color_normal, " loading libraries.");
            //print_error_ctx(&errs);
            error_ctx_clear(&errs);
        }
    }
    phase_done(opts, "load+parse libs", &last_time);
    if (config.size) verify_references(program, projects, project, true);

    SourceFile* source = nullptr;
    for (intptr_t i = 0; i < opts->path_count + root_files.count; i++) {
        csview path = i < opts->path_count ? csview_from(opts->paths[i]) : cstr_sv(&root_files.items[i - opts->path_count]);
        if (!opts->quiet) println(color_good, "Loading ", path, "...", color_normal);
        bool from_config = i >= opts->path_count;
        FailableResult loaded = program_load(program, path, &source);
        if (loaded.error && from_config) {
            // A `files` entry that doesn't exist: tsc's TS6053, and the rest is checked anyway
            failable_result_drop(&loaded);
            Error* error = error_ctx_global_error_d(&errs, 6053, "File '", path, "' not found.");
            error_sub_error_d(error, span_null, TS_CODE_NONE, "The file is in the program because:");
            error_sub_error_d(
                vec_error_back_mut(&error->sub_errors), span_null, TS_CODE_NONE, "Part of 'files' list in tsconfig.json"
            );
            continue;
        }
        try (loaded) cleanup {
            str_list_drop(&root_files);
            destroy_program(program, &errs, last);
            project_redirects_destroy(redirects);
        }
    }
    str_list_drop(&root_files);
    program_load_automatic_types(program);
    phase_done(opts, "load+parse files", &last_time);

    int ret = 0;
    const intptr_t parse_errors = report_errors(&errs);
    if (parse_errors) ret = 1;

    program_resolve_names(program);

    const intptr_t name_errors = report_errors(&errs);
    if (name_errors) ret = 1;
    phase_done(opts, "resolve names", &last_time);

    if (opts->print_ast && source) {
        println(color_good, "Parsed:", color_normal);
        println(debug_d(&source->ast), "\n");
    }

    intptr_t type_errors = 0;
    if (ret == 0 || opts->keep_going) {
        program_declaration_diagnostics_cli(program, parse_errors || name_errors);
        program_typecheck(program);

        if (error_ctx_empty(&errs)) {
            println(color_good, "Typecheck passed", color_normal);
        } else {
            println(color_error, "Typecheck failed", color_normal);
        }

        type_errors = report_errors(&errs);
        if (type_errors) ret = 1;
        phase_done(opts, "typecheck", &last_time);
        if (opts->time) {
            for (u32 i = 0; i < program_checker_count(program); i++) {
                Checker const* checker = program_checker_at(program, i);
                eprintln("time: checker ", i, ": ", checker->check_ms, " ms, arena ", (i64)(checker->arena.total >> 20),
                    " MB, ", checker->next_type_id, " types");
            }
#ifdef TTTT_ARENA_SITES
            arena_sites_print();
#endif
        }
    }

    if (parse_errors || name_errors || type_errors) {
        println(
            "Checked ", program_get_file_count(program), " files, found ",
            parse_errors ? color_error : color_none, parse_errors, " parse errors", color_normal, ", ",
            name_errors  ? color_error : color_none, name_errors,  " name errors",  color_normal, " and ",
            type_errors  ? color_error : color_none, type_errors,  " type errors",  color_normal, "."
        );
    } else {
        println(color_good, "Checked ", program_get_file_count(program), " files, no errors found!", color_normal);
    }

    //println(color_good, "Running...", color_normal);
    //run_in_engine(source, arc_js_object_init());
    //println(color_good, "Run succeeded", color_normal);

    if (counts) counts->errors += parse_errors + name_errors + type_errors;
    destroy_program(program, &errs, last);
    project_redirects_destroy(redirects);
    return ok(ret);
}

// Without -b: the files on the command line, else the project (-p, else the nearest tsconfig.json)
static_failable check_command_line(Options const* opts, returns int) {
    cstr config = opts->project ? cstr_from(opts->project) :
        opts->path_count ? cstr_null : tsconfig_find(fs, c_sv("."));
    if (cstr_size(&config) && fs_is_directory(fs, cstr_sv(&config))) cstr_append_lit(&config, "/tsconfig.json");
    ProjectSet* projects = project_set_new(fs);
    u32 project = cstr_size(&config) ? project_set_load(projects, cstr_sv(&config)) : 0;
    FailableResult result = check_files(opts, cstr_sv(&config), projects, project, true, nullptr, _OUTPUT_PARAM);
    // (The program may not have been freed: a release build leaves that to the exit)
#ifdef DEBUG_BUILD
    project_set_destroy(projects);
#endif
    cstr_drop(&config);
    return result;
}

// -b: every project the paths name (tsconfig.json files or their directories, `.` without any), each with what it
// references before it (tsc's build order). Each is its own program, with its own options and the command line's, and
// sees its references as sources. Like tsc 5.6+ goes on past a project with errors (not with --stopBuildOnErrors)
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
    if (cycles.count) {
        ErrorCtx errs = error_ctx_init();
        for (isize i = 0; i < cycles.count; i++) error_ctx_global_error_s(&errs, 6202, cstr_clone(cycles.items[i]));
        report_errors(&errs);
        error_ctx_drop(&errs);
        ret = 1;
        count = 0; // (tsc builds nothing then)
    }
    str_list_drop(&cycles);
    Options project_opts = *opts;
    project_opts.path_count = 0;
    CheckCounts counts = {0};
    u32 failed = 0;
    for (u32 i = 0; i < count; i++) {
        Project const* project = project_set_at(projects, order[i]);
        if (!project->readable) {
            ErrorCtx errs = error_ctx_init();
            error_ctx_global_error_d(&errs, 5083, "Cannot read file '", project->config, "'.");
            counts.errors += report_errors(&errs);
            error_ctx_drop(&errs);
            failed++;
            ret = 1;
            if (opts->stop_build_on_errors) break;
            continue;
        }
        intptr_t before = counts.errors;
        try (int project_ret, check_files(&project_opts, cstr_sv(&project->config), projects, order[i], i + 1 == count,
            &counts, &project_ret)) cleanup {
            free(order);
        }
        if (project_ret || counts.errors != before) {
            failed++;
            ret = 1;
            if (opts->stop_build_on_errors) break;
        }
    }
    free(order);
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

int main(int argc, const char** argv) {
    reexec_with_huge_pages(argv);
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
                "  --no-lib      don't load the standard library\n"
                "  --keep-going  typecheck even after parse or name errors\n"
                "  --all-errors  print every diagnostic, not just the first 5000 of each phase\n"
                "  --checkers K  check with K checkers (default 4): the files are dealt to them round robin. Only the\n"
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
                streq(argv[i + 1], "true") || streq(argv[i + 1], "false")
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

    try (int ret, opts.build ? build(&opts, &ret) : check_command_line(&opts, &ret)) if_err (e) {
        error_base_eprintln(e);
        current_error_destroy();
        return 1;
    }

    return ret;
}
