#include "type.h"
#include "program.h"
#include "engine.h"
#include "error.h"
#include "display.h"
#include "source_file.h"
#include "string_helpers.h"
#include <cleat/filesystem.h>
#include <time.h>

static const char* const stdlib_names[] = {
    "default",
    "es2015.collection",
    "es2015.iterable",
    "es2015.generator",
};

typedef struct Options {
    bool quiet;   // Only print diagnostics and the final summary (no AST dump)
    bool time;    // Print how long each phase took to stderr
    bool keep_going; // Typecheck even if there were parse or name errors
    bool no_lib;  // Don't load the standard library, for checking the library files themselves
    const char** paths;
    intptr_t path_count;
} Options;

static double now_ms() {
    struct timespec ts;
    // (C23's own clock rather than clock_gettime(), which strict -std=c2x hides)
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

// Reports the time since *last under the given name and resets *last. Reading and parsing can't be told apart, since
// program_load() does both (and chases imports) in one go
static void phase_done(Options const* opts, const char* name, double* last) {
    const double now = now_ms();
    if (opts->time) eprintln("time: ", name, " ", now - *last, " ms");
    *last = now;
}

static_failable load_stdlib(Program* program) {
    for (intptr_t i = 0; i < (intptr_t)c_arraylen(stdlib_names); i++) {
        try (SourceFile* lib, program_load_stdlib(program, csview_from(stdlib_names[i]), &lib)) bubble;
        ignore lib;
    }
    return ok;
}

static void destroy_program(Program* program, ErrorCtx* errs) {
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

// Checks the file at the given path, logically returning the exit code to leave with. Problems with the checked
// program are printed as TypeScript diagnostics and reflected in that exit code; only failures that stop us checking
// at all (an unreadable standard library or entry point) come back as errors.
static_failable check_files(Options const* opts, returns int) {
    ErrorCtx errs = error_ctx_init();
    error_config_enable_strict(&errs.config);
    Program* program = program_new(&errs, fs);
    double last = now_ms();

    if (!opts->no_lib) {
        if (!opts->quiet) println(color_good, "Loading libraries...", color_normal);
        try (load_stdlib(program)) cleanup {
            destroy_program(program, &errs);
        }

        if (!error_ctx_empty(&errs)) {
            const intptr_t lib_errors = vec_error_size(&errs.errors);
            println("Encountered ", color_error, lib_errors, " errors", color_normal, " loading libraries.");
            //print_error_ctx(&errs);
            error_ctx_clear(&errs);
        }
    }
    phase_done(opts, "load+parse libs", &last);

    SourceFile* source = nullptr;
    for (intptr_t i = 0; i < opts->path_count; i++) {
        csview path = csview_from(opts->paths[i]);
        if (!opts->quiet) println(color_good, "Loading ", path, "...", color_normal);
        try (program_load(program, path, &source)) cleanup {
            destroy_program(program, &errs);
        }
    }
    phase_done(opts, "load+parse files", &last);

    int ret = 0;
    const intptr_t parse_errors = report_errors(&errs);
    if (parse_errors) ret = 1;

    program_resolve_names(program);

    const intptr_t name_errors = report_errors(&errs);
    if (name_errors) ret = 1;
    phase_done(opts, "resolve names", &last);

    if (!opts->quiet) {
        println(color_good, "Parsed:", color_normal);
        println(debug_d(&source->ast), "\n");
    }

    intptr_t type_errors = 0;
    if (ret == 0 || opts->keep_going) {
        program_typecheck(program);

        if (error_ctx_empty(&errs)) {
            println(color_good, "Typecheck passed", color_normal);
        } else {
            println(color_error, "Typecheck failed", color_normal);
        }

        type_errors = report_errors(&errs);
        if (type_errors) ret = 1;
        phase_done(opts, "typecheck", &last);
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

    destroy_program(program, &errs);
    return ok(ret);
}

int main(int argc, const char** argv) {
    Options opts = { .paths = argv };
    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "--help") || streq(argv[i], "-h")) {
            println(
                "The Tess TypeScript Typechecker\n\nUSAGE: tttt [OPTIONS] TSPATH...\n\n"
                "  -q, --quiet   only print diagnostics and the summary\n"
                "  --time        print how long each phase took to stderr\n"
                "  --no-lib      don't load the standard library\n"
                "  --keep-going  typecheck even after parse or name errors"
            );
            return 0;
        } else if (streq(argv[i], "--quiet") || streq(argv[i], "-q")) {
            opts.quiet = true;
        } else if (streq(argv[i], "--time")) {
            opts.time = true;
        } else if (streq(argv[i], "--keep-going")) {
            opts.keep_going = true;
        } else if (streq(argv[i], "--no-lib")) {
            opts.no_lib = true;
        } else {
            // Paths are compacted to the front of argv, which is fine as they never outnumber what we've already read
            opts.paths[opts.path_count++] = argv[i];
        }
    }

    if (opts.path_count == 0) {
        println(color_error, "Too few arguments", color_normal);
        return 1;
    }

    try (int ret, check_files(&opts, &ret)) if_err (e) {
        error_base_eprintln(e);
        current_error_destroy();
        return 1;
    }

    return ret;
}
