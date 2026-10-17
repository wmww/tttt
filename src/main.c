// realpath() and PATH_MAX, which strict -std=c2x hides
#define _XOPEN_SOURCE 700
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/resource.h>

#include "ty/ty.h"
#include "content_mapper.h"
#include "type.h"
#include "program.h"
#include "ast_alloc.h"
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
#include <strings.h>
#include <ctype.h>
#include <errno.h>

enum { MAX_COMPILER_OPTION_ARGS = 64 };
// --checker-memory's default (plan 67 stage 1)
#define DEFAULT_CHECKER_MEMORY_MB 256
// Unless --checker-memory is given: what the checkers hold together at most, each at least 64 MB (plan 75: 128 MB
// each up to 12 checkers, 64 MB each at 24)
#define CHECKER_MEMORY_CAP_MB 1536
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)

typedef struct Options {
    bool quiet;   // -q: don't print diagnostics (tsgo's), only the summary
    int pretty; // --pretty: 1 or 0, -1 when not given (the config's, else whether stdout is a terminal)
    bool list_files; // --listFiles: the program's files after its diagnostics
    bool list_files_only; // --listFilesOnly: the program's files, nothing checked
    bool ignore_config; // --ignoreConfig: files on the command line next to a tsconfig.json
    bool print_ast; // Dump the parsed AST of the (last) file before typechecking
    bool time;    // Print how long each phase took to stderr
    bool keep_going; // Typecheck and report everything even after syntactic or options diagnostics (tsc stops)
    bool no_defer_bodies; // --no-defer-bodies: parse and resolve every function body up front (plan 78)
    bool no_lib;  // Don't load the standard library, for checking the library files themselves
    const char* project; // -p: a tsconfig.json or the directory of one
    bool build; // -b: paths are projects, each checked with everything it references, in build order
    bool stop_build_on_errors; // --stopBuildOnErrors
    u32 checkers; // --checkers
    u64 checker_memory; // --checker-memory, bytes (UINT64_MAX: off)
    bool checker_memory_given; // Else CHECKER_MEMORY_CAP_MB caps it
    u32 threads; // --threads
    const char* expect_types; // --expect-types FILE: print the types of the expressions it lists (expect_types.h)
    u32 build_jobs; // --builders: how many projects -b checks at once (0: decided by threads)
    const char* dts_dump; // --dts-dump DIR: write each emitted declaration file to DIR + its source's absolute path
    bool list_emitted_files; // --listEmittedFiles: `TSFILE: path` for each file written, after the diagnostics
    bool no_write; // --no-write: emit as tsc would, but write nothing (tools on shared checkouts: `-b` emits)
    // Not -b and no noEmit on the command line: tttt is `tsc --noEmit` unless the options make it
    // `tsc --emitDeclarationOnly` (apply_compiler_options())
    bool implicit_no_emit;
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
    ErrorSummary summary; // Of the diagnostics printed
    cstr emitted; // --listEmittedFiles: the `TSFILE: ` lines of the program, printed with its diagnostics
    bool pretty; // How the last program printed its diagnostics
} Output;

static void output_write(Output* me, FILE* file, csview text) {
    if (my buffered) {
        cstr_append_sv(file == stdout ? &my out : &my err, text);
    } else {
        fwrite(text.buf, 1, (size_t)text.size, file);
    }
}

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
// scripts/memtrack.sh's LD_PRELOADed live-heap tracker (bench/memtrack/memtrack.c), absent otherwise
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
// found from the executable (release/tttt: its directory's parent is the repo root), else the source tree it was built
// from (a build directory outside the repo, `meson setup /tmp/x`)
static void set_lib_dir(Program* program) {
    if (access("thirdparty/lib/es5.d.ts", R_OK) == 0) return;
    char exe[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
        exe[len] = 0;
        cstr dir = cstr_d(dirname(exe), "/../thirdparty/lib");
        cstr probe = cstr_d(cstr_sv(&dir), "/es5.d.ts");
        bool found = access(cstr_str(&probe), R_OK) == 0;
        cstr_drop(&probe);
        if (found) {
            program_set_lib_dir(program, cstr_sv(&dir));
            cstr_drop(&dir);
            return;
        }
        cstr_drop(&dir);
    }
#ifdef TTTT_SOURCE_LIB_DIR
    program_set_lib_dir(program, c_sv(TTTT_SOURCE_LIB_DIR));
#endif
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

// --runExternalCode given (and not false)
static bool runs_external_code(Options const* opts) {
    bool on = false;
    for (intptr_t i = 0; i < opts->option_count; i++) {
        if (!strcasecmp(opts->option_names[i], "runExternalCode")) {
            on = !opts->option_values[i] || strcasecmp(opts->option_values[i], "false");
        }
    }
    return on;
}

// The tsconfig if there is one, then whatever was on the command line
static_failable apply_compiler_options(Options const* opts, csview config, Program* program) {
    CompilerOptions* options = program_options(program);
    if (config.size) {
        // (A command line only option the config's parsing depends on: tsgo parses it with the command line's)
        for (intptr_t i = 0; i < opts->option_count; i++) {
            if (!strcasecmp(opts->option_names[i], "runExternalCode")) {
                ignore compiler_options_set(options, c_sv("runexternalcode"), csview_from(opts->option_values[i]));
            }
        }
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
        program_set_config_file(program, cstr_sv(&relative));
        // The config parsing diagnostics of its `extends` (at the name in this config, else without a location)
        cstr absolute = absolute_path_of(cstr_sv(&cwd), config);
        for (u32 i = 0; i < diagnostics.count; i++) {
            TsconfigDiagnostic* d = &diagnostics.items[i];
            // (One in a config it extends is at that config, named relative to the working directory like this one)
            Error* error = nullptr;
            if (cstr_eq(&d->config, &absolute)) {
                error = program_config_error(program, d->start, d->end, d->code, d->message);
            } else {
                cstr extended = relative_path_of(cstr_sv(&cwd), cstr_sv(&d->config));
                error = program_extended_config_error(program, cstr_sv(&extended), d->start, d->end, d->code, d->message);
                cstr_drop(&extended);
            }
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
            case COMPILER_OPTION_UNKNOWN: break; // (One of tsc's we ignore: main() rejected any other)
            case COMPILER_OPTION_BAD_VALUE:
                return err_d("Bad value '", opts->option_values[i], "' for option --", name);
        }
    }
    // tttt writes declarations (plan 64), not JavaScript: it emits like tsc only where tsc would only emit declarations,
    // else it is `tsc --noEmit`
    if (opts->implicit_no_emit && !(options->emit_declaration_only && (options->declaration || options->composite))) {
        ignore compiler_options_set(options, c_sv("noEmit"), c_sv("true"));
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

// Moves whatever errors have accumulated so far to the ones to print (shown), returning how many there were
static intptr_t report_errors(VecError* shown, ErrorCtx* errs) {
    const intptr_t count = vec_error_size(&errs->errors);
    for_each (i, vec_error, &errs->errors) vec_error_push(shown, *i);
    errs->errors.size = 0;
    error_ctx_clear(errs);
    return count;
}

// How the CLI names a diagnostic's file: relative to the working directory (tsc's convertToRelativePath)
static char const* cli_file_name(SourceFile const* source) {
    // (A default lib by tsgo's URL for it, source_file_message_path())
    if (source->is_stdlib && cstr_size(&source->bundled_path)) return cstr_str(&source->bundled_path);
    // (A content mapper's supplemental output is named by the file it comes from)
    if (source->content_mapped && source->content_mapped->canonical) source = source->content_mapped->canonical;
    char const* name = cstr_str(&source->canonical_path);
    return strncmp(name, "./", 2) == 0 ? name + 2 : name;
}

// tsc's emitFilesAndReportErrors: the diagnostics of a program sorted and deduplicated by their files' paths,
// printed in the format asked for, then the files with --listFiles. Returns how many were printed
static intptr_t print_diagnostics(Options const* opts, Output* out, Program* program, VecError* shown) {
    error_vec_sort_and_dedupe(shown, source_file_message_path);
    // (Content-mapped files' diagnostics are shown in their original text, plan 56)
    for (isize i = 0; i < vec_error_size(shown);) {
        if (content_mapped_map_error(&shown->data[i])) i++;
        else vec_error_erase_n(shown, i, 1);
    }
    cstr text = cstr_null;
    if (!opts->quiet) {
        for_each (i, vec_error, shown) {
            if (out->pretty) {
                error_format_pretty(&text, i, "\n", cli_file_name);
                cstr_append_lit(&text, "\n");
            } else {
                error_format_plain(&text, i, "\n", cli_file_name);
            }
        }
    }
    cstr_append_sv(&text, cstr_sv(&out->emitted));
    cstr_clear(&out->emitted);
    if (program && (opts->list_files || opts->list_files_only)) {
        for (intptr_t i = 0; i < program_get_file_count(program); i++) {
            cstr path = source_file_message_path(program_source_at(program, (u32)i));
            cstr_append_d(&text, DEFAULT_DISPLAY, path, "\n");
            cstr_drop(&path);
        }
    }
    output_write(out, stdout, cstr_sv(&text));
    cstr_drop(&text);
    error_summary_add(&out->summary, shown, cli_file_name);
    intptr_t count = vec_error_size(shown);
    vec_error_drop(shown);
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
    bool in_config = !span_valid(error->span) || (span_source(error->span) && span_source(error->span)->is_config);
    if (in_config) return error->phase == ERROR_PHASE_PARSE ? DIAGNOSTIC_CONFIG_PARSING : DIAGNOSTIC_OPTIONS;
    if (error->phase == ERROR_PHASE_PARSE && !error->grammar) return DIAGNOSTIC_SYNTACTIC;
    return error->declaration ? DIAGNOSTIC_DECLARATION : DIAGNOSTIC_SEMANTIC;
}

static void count_diagnostic_kinds(ErrorCtx const* errs, intptr_t counts[DIAGNOSTIC_KIND_COUNT]) {
    for (DiagnosticKind k = 0; k < DIAGNOSTIC_KIND_COUNT; k++) counts[k] = 0;
    for_each (i, vec_error, &errs->errors) counts[diagnostic_kind(i)]++;
}

// report_errors(), but the errors of one kind stay
static intptr_t report_errors_except(VecError* shown, ErrorCtx* errs, DiagnosticKind kept_kind) {
    VecError* errors = &errs->errors;
    intptr_t kept = 0, count = 0;
    for (intptr_t i = 0; i < vec_error_size(errors); i++) {
        Error* error = vec_error_at_mut(errors, i);
        if (diagnostic_kind(error) == kept_kind) {
            if (kept != i) errors->data[kept] = *error;
            kept++;
        } else {
            vec_error_push(shown, *error);
            count++;
        }
    }
    errors->size = kept;
    return count;
}

// Drops the errors of the kind (what tsc doesn't ask the program for)
static void drop_diagnostic_kind(ErrorCtx* errs, DiagnosticKind kind) {
    VecError* errors = &errs->errors;
    intptr_t kept = 0;
    for (intptr_t i = 0; i < vec_error_size(errors); i++) {
        Error* error = vec_error_at_mut(errors, i);
        // (A content mapper's program diagnostics, TS18099/TS18070, are options ones, but tsgo's
        // GetDiagnosticsOfAnyProgram reports them next to the syntactic ones those come with)
        bool content_mapper = !span_valid(error->span) && (error->ts_code == 18099 || error->ts_code == 18070);
        if (diagnostic_kind(error) == kind && !(kind == DIAGNOSTIC_OPTIONS && content_mapper)) {
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

// A diagnostic on a reference written in a config of the program's reference graph: the program's own config, or one
// it references (by its own JSON file, like tsc's CreateDiagnosticAtReferenceSyntax)
static Error* reference_error(Program* program, ProjectSet const* projects, u32 root, u32 parent,
    TsconfigReference const* reference, unsigned code, cstr message
) {
    if (parent == root) return program_config_error(program, reference->start, reference->end, code, message);
    cstr cwd = cstr_null;
    if (fs_get_cwd(fs, &cwd).error) cstr_clear(&cwd);
    cstr relative = relative_path_of(cstr_sv(&cwd), cstr_sv(&project_set_at(projects, parent)->config));
    Error* error = program_extended_config_error(program, cstr_sv(&relative), reference->start, reference->end, code, message);
    c_drop(cstr, &relative, &cwd);
    return error;
}

// tsc's verifyProjectReferences: every reference of the graph the program's config references (rangeResolvedProject
// Reference: depth-first in written order, each project once, the root not again), on the reference that first
// reaches it, about what it references when the config the reference is in has files of its own (has_inputs for
// the program's own: a container may reference anything)
static void verify_references_from(Program* program, ProjectSet* projects, u32 root, u32 parent, bool has_inputs,
    u32** seen, u32* seen_count
) {
    Project const* p = project_set_at(projects, parent);
    for (u32 i = 0; i < p->reference_count; i++) {
        TsconfigReference const* reference = &p->references[i];
        u32 index = p->referenced[i];
        bool already = false;
        for (u32 j = 0; j < *seen_count && !already; j++) already = (*seen)[j] == index;
        if (already) continue;
        *seen = realloc(*seen, sizeof(u32) * (*seen_count + 1));
        (*seen)[(*seen_count)++] = index;
        Project const* referenced = project_set_at(projects, index);
        // (tsc's verifyDeprecatedProjectReference, of the program's own config)
        if (reference->prepend && parent == root) {
            reference_error(program, projects, root, parent, reference, 5102,
                cstr_from("Option 'prepend' has been removed. Please remove it from your configuration."));
        }
        if (!referenced->readable) {
            reference_error(program, projects, root, parent, reference, 6053, cstr_d("File '", reference->written, "' not found."));
            continue;
        }
        if (has_inputs && !referenced->options.composite) {
            reference_error(program, projects, root, parent, reference, 6306,
                cstr_d("Referenced project '", reference->written, "' must have setting \"composite\": true."));
        }
        if (has_inputs && referenced->options.no_emit) {
            reference_error(program, projects, root, parent, reference, 6310,
                cstr_d("Referenced project '", reference->written, "' may not disable emit."));
        }
        bool referenced_inputs = referenced->reference_count && project_set_root_files(projects, index)->count > 0;
        verify_references_from(program, projects, root, index, referenced_inputs, seen, seen_count);
    }
}

static void verify_references(Program* program, ProjectSet* projects, u32 project, bool has_inputs) {
    u32* seen = malloc(sizeof(u32));
    seen[0] = project;
    u32 seen_count = 1;
    verify_references_from(program, projects, project, project, has_inputs, &seen, &seen_count);
    free(seen);
}

// --dts-dump DIR (scripts/dtsdiff.py, plan 64): each file's declaration emit output (SourceFile.declaration_output) to
// DIR + the source's absolute path with the declaration extension (a.ts -> DIR/abs/a.d.ts), whatever the options'
// outDir/declarationDir say: the script maps them to tsc's outputs. Nothing is written where nothing was emitted (no
// `declaration`, or checking was gated without it)
static void dump_declarations(char const* dir, Program const* program) {
    cstr cwd = cstr_null;
    try (fs_get_cwd(fs, &cwd)) or_log(warn);
    for (u32 i = 0; i < program_source_count(program); i++) {
        SourceFile const* source = program_source_at(program, i);
        if (!source->declaration_output) continue;
        cstr absolute = absolute_path_of(cstr_sv(&cwd), cstr_sv(&source->canonical_path));
        csview path = cstr_sv(&absolute);
        csview ext = file_extension_of(path);
        char const* dts = csview_equals(ext, "mts") || csview_equals(ext, "mjs") ? ".d.mts" :
            csview_equals(ext, "cts") || csview_equals(ext, "cjs") ? ".d.cts" : ".d.ts";
        if (ext.size) path = csview_slice(path, 0, path.size - ext.size - 1);
        cstr out_path = cstr_from(dir);
        cstr_append_sv(&out_path, path);
        cstr_append(&out_path, dts);
        try (fs_mkdir(fs, dirname_of(cstr_sv(&out_path)))) or_log(warn);
        source_text_detach(cstr_sv(&out_path));
        try (fs_write_file(fs, cstr_sv(&out_path), cstr_sv(source->declaration_output))) or_log(warn);
        c_drop(cstr, &absolute, &out_path);
    }
    cstr_drop(&cwd);
}

// tsc --emitDeclarationOnly (plan 64): writes each file's declarations (SourceFile.declaration_output) where tsc would
// (SourceFile.declaration_output_path), but not over a path TS5055/TS5056 blocked. A failed write is tsc's TS5033 in
// errs. Returns whether anything failed
static bool write_declarations(Options const* opts, Output* out, Program* program, ErrorCtx* errs) {
    bool failed = false;
    for (u32 i = 0; i < program_source_count(program); i++) {
        SourceFile const* source = program_source_at(program, i);
        csview path = cstr_sv(&source->declaration_output_path);
        if (!source->declaration_output || !path.size || program_emit_blocked(program, path) || opts->no_write) continue;
        FailableResult made = fs_mkdir(fs, dirname_of(path));
        // (tsc writes the declaration map first: printSourceFile)
        if (source->declaration_map && !made.error) {
            cstr map_path = cstr_from_sv(path);
            cstr_append(&map_path, ".map");
            bool map_failed = false;
            source_text_detach(cstr_sv(&map_path));
            try (fs_write_file(fs, cstr_sv(&map_path), cstr_sv(source->declaration_map))) if_err (e) {
                ignore e;
                int code = errno;
                current_error_destroy();
                error_ctx_global_error_d(errs, 5033, "Could not write file '", path, "': ", code ? strerror(code) : "error", ".");
                failed = map_failed = true;
            }
            if (!map_failed && opts->list_emitted_files) cstr_append_d(&out->emitted, DEFAULT_DISPLAY, "TSFILE: ", map_path, "\n");
            cstr_drop(&map_path);
        }
        if (!made.error) source_text_detach(path);
        FailableResult written = made.error ? made : fs_write_file(fs, path, cstr_sv(source->declaration_output));
        if (made.error) failable_result_drop(&written);
        try (written) if_err (e) {
            ignore e;
            int code = errno;
            current_error_destroy();
            error_ctx_global_error_d(errs, 5033, "Could not write file '", path, "': ", code ? strerror(code) : "error", ".");
            failed = true;
            continue;
        }
        if (opts->list_emitted_files) cstr_append_d(&out->emitted, DEFAULT_DISPLAY, "TSFILE: ", path, "\n");
    }
    return failed;
}

// Checks one program, logically returning the exit code to leave with: the files on the command line (with the
// config's options if there's a config), else the config's files. With a config the program sees the projects it
// references through their sources (projects is where they're loaded, project the config's). Problems with the checked
// program are printed as TypeScript diagnostics and reflected in that exit code; only failures that stop us checking
// at all (an unreadable standard library or entry point) come back as errors. redirects: what the config references
// (null without a config), the caller's to free after the program. last: nothing is checked after it
// Whether the diagnostics so far gate the checking (syntactic or options ones, tsc's emitFilesAndReportErrors)
static bool count_gating_diagnostics(ErrorCtx* errs, Options const* opts) {
    if (opts->keep_going) return false;
    intptr_t kinds[DIAGNOSTIC_KIND_COUNT];
    count_diagnostic_kinds(errs, kinds);
    return kinds[DIAGNOSTIC_SYNTACTIC] || kinds[DIAGNOSTIC_OPTIONS];
}

// `defer`: deferred function bodies (plan 78); an output that tells them from the eager parse and walk runs again
// without (program_defer_failed())
static_failable check_files(Options const* opts, Output* out, csview config, ProjectSet* projects, u32 project,
    ProjectRedirects const* redirects, bool last, CheckCounts* counts, bool defer, returns int
) {
    ErrorCtx* errs = c_new(ErrorCtx, error_ctx_init());
    Program* program = program_new(errs, fs);
    program_set_defer_bodies(program, defer);
    program_set_realpath(program, real_path_of, nullptr);
    program_set_file_size(program, file_size_of, nullptr);
    program_set_checkers(program, opts->checkers);
    // (--expect-types reads each file's types in the checker that checked it)
    if (!opts->expect_types) program_set_checker_memory(program, opts->checker_memory);
    if (!opts->expect_types && !opts->checker_memory_given) program_set_checker_memory_cap(program, (u64)CHECKER_MEMORY_CAP_MB << 20);
    set_lib_dir(program);
    program_follow_lib_references(program);
    StrList root_files = {0};
    try (apply_compiler_options(opts, config, program)) cleanup {
        destroy_program(program, errs, last);
    }
    // tsc's shouldBePretty: --pretty, else the config's, else whether the output is a terminal (and NO_COLOR isn't
    // set). -b only asks the command line
    {
        CompilerOptions const* options = program_options(program);
        int pretty = opts->pretty >= 0 || opts->build || !options->is_set[COMPILER_OPTION_pretty] ? opts->pretty :
            options->pretty;
        out->pretty = pretty >= 0 ? pretty : isatty(STDOUT_FILENO) && !getenv("NO_COLOR");
    }
    VecError shown = {0}; // What's printed at the end
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
        // A solution config: only references (tsc says nothing, and builds nothing without -b). -b doesn't build it,
        // so not even its options are checked
        // (tsgo: a `references` array, even an empty one, makes a config without files no mistake: no TS18003)
        bool solution = config.size && (project_set_at(projects, project)->reference_count ||
            project_set_at(projects, project)->has_references);
        // (tsgo's -b: a config without files and with a `references` array, even an empty one, is a solution: nothing
        // is built, only its config's parsing diagnostics are reported, and those are ExitStatus 1)
        bool build_solution = opts->build && config.size && project_set_at(projects, project)->has_references;
        bool built = config.size && !(opts->build && solution) && !build_solution;
        if (built) verify_references(program, projects, project, false);
        bool no_inputs = (built || build_solution) && program_report_no_inputs(program);
        if (built) program_verify_options(program);
        report_errors(&shown, errs);
        intptr_t config_errors = print_diagnostics(opts, out, program, &shown);
        if (counts) counts->errors += config_errors;
        // (An emitting program with noEmitOnError skipped its emit: 1)
        CompilerOptions const* options = program_options(program);
        bool skipped = !options->no_emit && options->no_emit_on_error;
        str_list_drop(&root_files);
        destroy_program(program, errs, last);
        // (tsgo: diagnostics and no file to emit is ExitStatus.DiagnosticsPresent_OutputsGenerated, 2)
        if (build_solution) return ok(config_errors ? 1 : 0);
        if (solution || no_inputs) return ok(config_errors ? (skipped ? 1 : 2) : 0);
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
        // (What's been reported so far, the config's syntax errors, is kept aside from the libraries' errors)
        VecError earlier = errs->errors;
        errs->errors = (VecError){0};
        try (load_stdlib(program)) cleanup {
            vec_error_drop(&earlier);
            vec_error_drop(&shown);
            destroy_program(program, errs, last);
        }
        vec_error_drop(&errs->errors);
        errs->errors = earlier;
    }
    phase_done(opts, out, "load+parse libs", &last_time);
    if (config.size) verify_references(program, projects, project, true);

    SourceFile* source = nullptr;
    for (intptr_t i = 0; i < opts->path_count + root_files.count; i++) {
        csview path = i < opts->path_count ? csview_from(opts->paths[i]) : cstr_sv(&root_files.items[i - opts->path_count]);
        bool from_config = i >= opts->path_count;
        FailableResult loaded = program_load(program, path, &source);
        if (loaded.error) {
            // A file on the command line or a `files` entry that doesn't exist: tsc's TS6053, and the rest is checked
            failable_result_drop(&loaded);
            Error* error = error_ctx_global_error_d(errs, 6053, "File '", path, "' not found.");
            error_sub_error_d(error, span_null, TS_CODE_NONE, "The file is in the program because:");
            error_sub_error_d(vec_error_back_mut(&error->sub_errors), span_null, TS_CODE_NONE,
                from_config ? "Part of 'files' list in tsconfig.json" : "Root file specified for compilation");
            continue;
        }
        try (loaded) cleanup {
            str_list_drop(&root_files);
            vec_error_drop(&shown);
            destroy_program(program, errs, last);
        }
    }
    str_list_drop(&root_files);
    program_load_automatic_types(program);
    phase_done(opts, out, "load+parse files", &last_time);
    // (The syntax errors of deferred bodies are only found when they're parsed: an output that has to have them all
    // without checking runs again without deferral)
    if (defer && program_has_deferred_bodies(program) && (opts->list_files_only || count_gating_diagnostics(errs, opts))) {
        destroy_program(program, errs, false);
        defer_stat_add(DEFER_STAT_RESTARTS, 1);
        return check_files(opts, out, config, projects, project, redirects, last, counts, false, _OUTPUT_PARAM);
    }
    if (opts->list_files_only) {
        // (tsc's listFilesOnly: the options and syntactic diagnostics, nothing checked)
        drop_diagnostic_kind(errs, DIAGNOSTIC_SEMANTIC);
        program_order_stdlib_first(program);
        report_errors(&shown, errs);
        intptr_t count = print_diagnostics(opts, out, program, &shown);
        if (counts) counts->errors += count;
        destroy_program(program, errs, last);
        return ok(0); // (tsgo's)
    }

    int ret = 0;
    // tsc's emitFilesAndReportErrors: semantic diagnostics only without syntactic, options and global ones (noEmitOnError's
    // handleNoEmitOptions gates them alike in tsgo 7)
    CompilerOptions const* options = program_options(program);
    bool ungated = opts->keep_going;
    intptr_t kinds[DIAGNOSTIC_KIND_COUNT];
    count_diagnostic_kinds(errs, kinds);
    bool syntactic = kinds[DIAGNOSTIC_SYNTACTIC];
    bool gated = !ungated && (syntactic || kinds[DIAGNOSTIC_OPTIONS]);
    if (!ungated && syntactic) drop_diagnostic_kind(errs, DIAGNOSTIC_OPTIONS);
    if (gated) drop_diagnostic_kind(errs, DIAGNOSTIC_SEMANTIC);
    // (The grammar errors tsc's checker reports, semantic ones, wait for the comment directives: reported with the rest)
    const intptr_t parse_errors = report_errors_except(&shown, errs, DIAGNOSTIC_SEMANTIC);
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

        // Options diagnostics from validating the options, and the checker's global diagnostics (without a file), hide
        // the semantic ones: then nothing is checked, as tsc doesn't (unless it emits declarations)
        if (!syntactic) program_report_global_diagnostics(program);
        count_diagnostic_kinds(&names, kinds);
        intptr_t type_kinds[DIAGNOSTIC_KIND_COUNT];
        count_diagnostic_kinds(errs, type_kinds);
        if (!ungated && (kinds[DIAGNOSTIC_OPTIONS] || type_kinds[DIAGNOSTIC_OPTIONS])) {
            gated = true;
            typecheck = emit_declarations;
        }
        program_declaration_diagnostics_cli(program, parse_errors || !error_ctx_empty(&names) || !error_ctx_empty(errs));
        if (opts->dts_dump || (options->emit_declaration_only && emit_declarations)) program_set_emit_declarations(program, true);
        if (typecheck) program_typecheck(program);
        if (defer && (program_defer_failed(program) || (gated && program_has_deferred_bodies(program)))) {
            error_ctx_drop(&names);
            vec_error_drop(&shown);
            destroy_program(program, errs, false);
            defer_stat_add(DEFER_STAT_RESTARTS, 1);
            return check_files(opts, out, config, projects, project, redirects, last, counts, false, _OUTPUT_PARAM);
        }
        if (opts->dts_dump && typecheck) dump_declarations(opts->dts_dump, program);
        // (Global diagnostics the checking itself finds, `using`'s TS2318 for a missing Disposable, hide nothing:
        // tsgo's GetDiagnosticsOfAnyProgram gates on those before checking and sweeps these up after)
        if (gated) {
            drop_diagnostic_kind(&names, DIAGNOSTIC_SEMANTIC);
            drop_diagnostic_kind(errs, DIAGNOSTIC_SEMANTIC);
        }
        if (gated && syntactic && !ungated) {
            drop_diagnostic_kind(&names, DIAGNOSTIC_OPTIONS);
            drop_diagnostic_kind(errs, DIAGNOSTIC_OPTIONS);
        }
        name_errors = report_errors(&shown, &names);
        error_ctx_drop(&names);
        if (name_errors) ret = 1;
        type_errors = report_errors(&shown, errs);
        if (type_errors) ret = 1;
        phase_done(opts, out, "typecheck", &last_time);
        if (opts->time) {
            out_eprintln(out, "time: checkers: ", program_checker_count(program), " (", (i64)(program_checked_source_bytes(program) >> 10),
                " KB of checked source)");
            for (u32 i = 0; i < program_checker_count(program); i++) {
                Checker const* checker = program_checker_at(program, i);
                out_eprintln(out, "time: checker ", i, ": ", checker->check_ms, " ms, arena ", (i64)(checker->arena.total >> 20),
                    " MB, ", checker->next_type_id, " types");
                if (checker->recycles) {
                    out_eprintln(out, "time: checker ", i, " recycled ", checker->recycles, " times, held at most ",
                        (i64)(checker->peak_memory >> 20), " MB");
                }
                // (Comparable with tsc --extendedDiagnostics's cache sizes, which count failures and nested pairs too)
                out_eprintln(out, "time: checker ", i, " relation cache: assignable ", checker->relations[TY_RELATION_ASSIGNABLE].len,
                    ", comparable ", checker->relations[TY_RELATION_COMPARABLE].len, ", subtype ",
                    checker->relations[TY_RELATION_SUBTYPE].len, ", strict subtype ", checker->relations[TY_RELATION_STRICT_SUBTYPE].len,
                    ", identity ", checker->relations[TY_RELATION_IDENTITY].len);
                out_eprintln(out, "time: checker ", i, " intern tables (entries / capacity): types ", checker->types.len, " / ",
                    checker->types.cap, ", links ", checker->links.len, " / ", checker->links.cap, ", atoms ", checker->atoms.len,
                    " / ", checker->atoms.cap, ", intersection props ", checker->intersection_props.len, " / ",
                    checker->intersection_props.cap);
                out_eprintln(out, "time: checker ", i, " node links ", checker->node_links.len, " / ", checker->node_links.cap);
            }
#ifdef TTTT_ARENA_SITES
            arena_sites_print();
#endif
        }
    }

    if (!(typecheck || !syntactic) && ungated) {
        // (Names aren't resolved after syntactic errors, but the options diagnostics are still asked for)
        program_verify_options(program);
        name_errors = report_errors(&shown, errs);
        if (name_errors) ret = 1;
    }
    // (A declaration diagnostic blocks its file's .d.ts: tsgo's emitDeclarationFile says EmitSkipped)
    bool declaration_blocked = false;
    for_each (e, vec_error, &shown) declaration_blocked |= e->declaration;
    // tsc --emitDeclarationOnly: nothing is written after a diagnostic with noEmitOnError (handleNoEmitOptions)
    if (options->emit_declaration_only && emit_declarations && typecheck &&
        !(options->no_emit_on_error && vec_error_size(&shown)) && write_declarations(opts, out, program, errs)) {
        report_errors(&shown, errs);
        if (!ret) ret = 1;
    }
    intptr_t printed = print_diagnostics(opts, out, program, &shown);
    if (opts->expect_types && typecheck) {
        output_flush(out);
        program_print_expect_types(program, opts->expect_types);
    }

    if (counts) counts->errors += printed;
    // tsgo 7.1's EmitAndReportStatistics: diagnostics are ExitStatus.DiagnosticsPresent_OutputsGenerated (2), --noEmit
    // included (handleNoEmitOptions doesn't count as a skipped emit), unless the emit was skipped (1): noEmitOnError, or
    // (-b, which emits) a declaration diagnostic
    if (ret) ret = !options->no_emit && (options->no_emit_on_error || declaration_blocked) ? 1 : 2;
    destroy_program(program, errs, last);
    if (!last) phase_done(opts, out, "free", &last_time);
    return ok(ret);
}

// tsc's reportErrorSummary, pretty only: "Found 2 errors in 2 files." (with_files: and the table of them)
static void print_summary(Output* out, bool pretty, bool with_files) {
    if (pretty) {
        cstr text = cstr_null;
        error_summary_format(&text, &out->summary, "\n", with_files);
        output_write(out, stdout, cstr_sv(&text));
        cstr_drop(&text);
    }
    error_summary_drop(&out->summary);
    cstr_drop(&out->emitted);
}

static int command_line_error(int pretty, unsigned code, cstr message);

// Without -b: the files on the command line, else the project (-p, else the nearest tsconfig.json)
static_failable check_command_line(Options const* opts, returns int) {
    if (opts->path_count && !opts->project && !opts->ignore_config) {
        // tsc's executeCommandLine: files on the command line and a tsconfig.json from here upwards are a mistake
        cstr found = tsconfig_find(fs, c_sv("."));
        bool present = cstr_size(&found) > 0;
        cstr_drop(&found);
        if (present) {
            return ok(command_line_error(opts->pretty, 5112, cstr_from("tsconfig.json is present but will not be loaded "
                "if files are specified on commandline. Use '--ignoreConfig' to skip this error.")));
        }
    }
    cstr config = opts->project ? cstr_from(opts->project) :
        opts->path_count ? cstr_null : tsconfig_find(fs, c_sv("."));
    if (cstr_size(&config) && fs_is_directory(fs, cstr_sv(&config))) cstr_append_lit(&config, "/tsconfig.json");
    ProjectSet* projects = project_set_new(fs);
    project_set_run_external_code(projects, runs_external_code(opts));
    u32 project = cstr_size(&config) ? project_set_load(projects, cstr_sv(&config)) : 0;
    ProjectRedirects* redirects = cstr_size(&config) ? project_redirects_new(projects, project) : nullptr;
    Output out = {0};
    FailableResult result =
        check_files(opts, &out, cstr_sv(&config), projects, project, redirects, true, nullptr, !opts->no_defer_bodies, _OUTPUT_PARAM);
    print_summary(&out, out.pretty, true);
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
        // (tsgo's build task: a config that isn't there is TS6053)
        if (access(cstr_str(&project->config), F_OK) != 0) {
            error_ctx_global_error_d(&errs, 6053, "File '", project->config, "' not found.");
        } else {
            error_ctx_global_error_d(&errs, 5083, "Cannot read file '", project->config, "'.");
        }
        VecError shown = {0};
        report_errors(&shown, &errs);
        job->out.pretty = my opts->pretty >= 0 ? my opts->pretty : isatty(STDOUT_FILENO) && !getenv("NO_COLOR");
        job->counts.errors += print_diagnostics(my opts, &job->out, nullptr, &shown);
        error_ctx_drop(&errs);
        job->ret = 1; // (tsgo's ExitStatusDiagnosticsPresent_OutputsSkipped)
        return;
    }
    // (The project set is complete and only read from here on: build() listed every root file up front)
    job->result = check_files(&my project_opts, &job->out, cstr_sv(&project->config), (ProjectSet*)my projects,
        my order[i], job->redirects, i + 1 == my count, &job->counts, !my project_opts.no_defer_bodies, &job->ret);
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
    project_set_run_external_code(projects, runs_external_code(opts));
    isize root_count = opts->path_count ? opts->path_count : 1;
    u32* roots = c_new_n(u32, root_count);
    for (isize i = 0; i < root_count; i++) {
        csview path = opts->path_count ? csview_from(opts->paths[i]) : c_sv(".");
        // (tsgo's ResolveConfigFileNameOfProjectReference: a .json path is the config, anything else its directory's
        // tsconfig.json, whether or not it exists)
        cstr config = csview_ends_with(path, ".json") ? cstr_from_sv(path) : cstr_d(path, "/tsconfig.json");
        roots[i] = project_set_load(projects, cstr_sv(&config));
        cstr_drop(&config);
    }
    u32* order = nullptr;
    StrList cycles = {0};
    u32 count = project_set_build_order(projects, roots, (u32)root_count, &order, &cycles);
    c_free_n(roots, root_count);
    int ret = 0;
    // (Where the summary of every project's diagnostics is gathered, in build order)
    Output direct = { .pretty = opts->pretty >= 0 ? opts->pretty : isatty(STDOUT_FILENO) && !getenv("NO_COLOR") };
    if (cycles.count) {
        ErrorCtx errs = error_ctx_init();
        for (isize i = 0; i < cycles.count; i++) error_ctx_global_error_s(&errs, 6202, cstr_clone(cycles.items[i]));
        VecError shown = {0};
        report_errors(&shown, &errs);
        print_diagnostics(opts, &direct, nullptr, &shown);
        error_ctx_drop(&errs);
        ret = 4; // (tsc's ExitStatus.ProjectReferenceCycle_OutputsSkipped)
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
        error_summary_merge(&direct.summary, &job->out.summary);
        if (job->result.error) {
            // (What stops us checking at all: a later project's error isn't any less fatal, but this one comes first)
            failure = job->result;
            job->result = (FailableResult){0};
            atomic_store(&b.end, 0);
            break;
        }
        counts.errors += job->counts.errors;
        // (tsgo's build result is the highest of its projects' exit statuses: 2 for diagnostics with the outputs
        // written, 1 when they're skipped, as `tsc -p` says for each)
        if (job->ret > ret) ret = job->ret;
        if (job->ret || job->counts.errors) {
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
        error_summary_drop(&b.jobs[i].out.summary);
        cstr_drop(&b.jobs[i].out.emitted);
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
    // (tsgo's -b summary is the count alone)
    print_summary(&direct, direct.pretty, false);
#ifdef DEBUG_BUILD
    project_set_destroy(projects);
#endif
    return ok(ret);
}

// The TypeScript version tttt checks like (what --version says)
#define TTTT_TYPESCRIPT_VERSION "7.1.0-dev"

// A command line error (tsc's TS5023 and friends) in the format diagnostics are printed in
static int command_line_error(int pretty, unsigned code, cstr message) {
    ErrorCtx errs = error_ctx_init();
    error_ctx_global_error_s(&errs, code, message);
    cstr text = cstr_null;
    bool colors = pretty >= 0 ? pretty : isatty(STDOUT_FILENO) && !getenv("NO_COLOR");
    if (colors) error_format_pretty(&text, vec_error_front(&errs.errors), "\n", cli_file_name);
    else error_format_plain(&text, vec_error_front(&errs.errors), "\n", cli_file_name);
    fputs(cstr_str(&text), stdout);
    cstr_drop(&text);
    error_ctx_drop(&errs);
    return 1;
}

// A count option's value (--checkers 4): positive
static bool count_value(int argc, const char** argv, int i) {
    return i + 1 < argc && atoi(argv[i + 1]) > 0;
}

// tsc's --locale validation (Go's language.Parse, approximately): a 2-3 letter language, then subtags of 1-8 letters
// or digits separated by `-` or `_` ("en", "ja-jp", "zh_TW", "pt-BR")
static bool locale_is_valid(char const* locale) {
    size_t n = 0;
    while (isalpha((unsigned char)locale[n])) n++;
    if (n < 2 || n > 3) return false;
    char const* p = locale + n;
    while (*p) {
        if (*p != '-' && *p != '_') return false;
        p++;
        size_t k = 0;
        while (isalnum((unsigned char)p[k])) k++;
        if (k < 1 || k > 8) return false;
        p += k;
    }
    return true;
}

static void print_help(void) {
    println(
        "The Tess TypeScript Typechecker: checks like `tsc --noEmit`, and emits like `tsc` where the options\n"
        "only emit declarations (emitDeclarationOnly; JavaScript is never emitted)\n\n"
        "USAGE: tttt [OPTIONS] [TSPATH...]\n\n"
        "  -p, --project PATH  a tsconfig.json or its directory: options and, without TSPATHs, the files.\n"
        "                Without TSPATHs and -p, the nearest tsconfig.json from here upwards. Referenced\n"
        "                projects are read from their sources, not built outputs, and aren't checked\n"
        "  -b, --build [PROJECT...]  check each project (`.` by default) and everything it references, each\n"
        "                with its own options, references first (-v, -f are accepted and ignored)\n"
        "  --stopBuildOnErrors  with -b, stop after the first project with errors\n"
        "  --pretty [false]  tsc's formats: with colours and code frames and a summary (the default when the\n"
        "                output is a terminal), else `file(line,col): error TSxxxx: message`\n"
        "  -q, --quiet   don't print diagnostics (only the summary, when pretty)\n"
        "  --listFiles   print the program's files after its diagnostics\n"
        "  --listFilesOnly  print the program's files and stop\n"
        "  --listEmittedFiles  print the written declaration files after the diagnostics\n"
        "  -v, --version print the version of TypeScript tttt checks like\n"
        "  --checkers K  check with K checkers (default: one per thread, at most one per 64 KB of checked\n"
        "                source). Each gets contiguous runs of the files; the output doesn't depend on K\n"
        "  --builders J  with -b, check J projects at once, each on threads/J threads (default: 2 with\n"
        "                4 threads, else 1). The output doesn't depend on it\n"
        "  --singleThreaded  one checker on one thread, one project at a time\n"
        "  --checker-memory MB  what the checkers may hold together before each drops its types and goes on\n"
        "                fresh (default: " STRINGIFY(DEFAULT_CHECKER_MEMORY_MB) ", each at least 128, together at most\n"
        "                " STRINGIFY(CHECKER_MEMORY_CAP_MB) " but each at least 64; 0: a fresh checker per file, off: never).\n"
        "                The output doesn't depend on it\n"
        "  --threads N   parse, bind and check on N threads (default: the number of cores; checking uses at\n"
        "                most K of them)\n"
        "  --OPTION [VALUE]  a tsc compiler option: --target es2020, --lib es2015,dom, --strict false,\n"
        "                --noImplicitAny... (strict is on by default, a boolean without a value means true)\n\n"
        "tttt's own (debugging and oracles):\n"
        "  --time        print how long each phase took to stderr\n"
        "  --print-ast   print the parsed AST of the last file\n"
        "  --no-lib      don't load the standard library\n"
        "  --expect-types FILE  after checking, print the types of the expressions FILE lists by span\n"
        "  --dts-dump DIR  write each file's declaration emit to DIR + the file's absolute path (.d.ts)\n"
        "  --no-write    emit like tsc but write nothing (tools on shared checkouts: -b emits)\n"
        "  --keep-going  typecheck and report everything even after parse or options errors (tsc stops)\n"
        "  --no-defer-bodies  parse and resolve every function body up front, not when a checker needs it"
    );
}

static int run(int argc, const char** argv) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    // Every core parses and binds; checking uses at most `checkers` of them (0: one per core, fewer for a small program,
    // auto_checker_count(): the output doesn't depend on the count, plan 62)
    Options opts = {
        .paths = argv, .pretty = -1, .checkers = 0, .threads = cores < 1 ? 1 : (u32)cores,
        .checker_memory = (u64)DEFAULT_CHECKER_MEMORY_MB << 20,
    };
    bool single_threaded = false;
    // tsc's executeCommandLine: `--build` only as the first argument (TS6369 elsewhere), and its options only with it
    // (TS5093): every such error is reported, then nothing is done
    ErrorCtx arg_errors = error_ctx_init();
    bool help = false;
    bool build_first = argc > 1 && (streq(argv[1], "-b") || streq(argv[1], "--b") || streq(argv[1], "--build"));
    for (int i = 1; i < argc; i++) {
        if (!build_first && argv[i][0] == '-') {
            char const* name = argv[i] + (argv[i][1] == '-' ? 2 : 1);
            if (!strcasecmp(name, "build") || (argv[i][1] != '-' && streq(name, "b")) || !strcasecmp(name, "b")) {
                error_ctx_global_error_d(&arg_errors, 6369, "Option '--build' must be the first command line argument.");
                continue;
            }
            static char const* const build_only[] = { "verbose", "dry", "force", "clean", "stopBuildOnErrors", "builders" };
            bool only = false;
            for (u32 k = 0; k < c_arraylen(build_only) && !only; k++) only = !strcasecmp(name, build_only[k]) && argv[i][1] == '-';
            if (only) {
                error_ctx_global_error_d(&arg_errors, 5093, "Compiler option '--", name, "' may only be used with '--build'.");
                if (!strcasecmp(name, "builders") && i + 1 < argc && argv[i + 1][0] != '-') i++;
                continue;
            }
        }
        if ((streq(argv[i], "--locale") || streq(argv[i], "--Locale")) && i + 1 < argc && !locale_is_valid(argv[i + 1])) {
            error_ctx_global_error_d(&arg_errors, 6048, "Locale must be an IETF BCP 47 language tag. Examples: 'en', 'ja-jp'.");
            i++;
            continue;
        }
        if (streq(argv[i], "--help") || streq(argv[i], "-h")) {
            help = true;
        } else if (!opts.build && !help && (streq(argv[i], "--version") || streq(argv[i], "-v"))) {
            println("Version " TTTT_TYPESCRIPT_VERSION);
            return 0;
        } else if (streq(argv[i], "--quiet") || streq(argv[i], "-q")) {
            opts.quiet = true;
        } else if (streq(argv[i], "--print-ast")) {
            opts.print_ast = true;
        } else if (streq(argv[i], "--time")) {
            opts.time = true;
        } else if (streq(argv[i], "--keep-going")) {
            opts.keep_going = true;
        } else if (streq(argv[i], "--no-defer-bodies")) {
            opts.no_defer_bodies = true;
        } else if (streq(argv[i], "--ignoreConfig")) {
            opts.ignore_config = true;
        } else if (streq(argv[i], "--listFiles")) {
            opts.list_files = true;
        } else if (streq(argv[i], "--no-write")) {
            opts.no_write = true;
        } else if (streq(argv[i], "--listEmittedFiles")) {
            opts.list_emitted_files = true;
        } else if (streq(argv[i], "--listFilesOnly")) {
            opts.list_files_only = true;
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
        } else if ((opts.build && (streq(argv[i], "--dry") || streq(argv[i], "-d") || streq(argv[i], "--clean"))) ||
            streq(argv[i], "-w") || streq(argv[i], "--watch") || streq(argv[i], "--init") ||
            streq(argv[i], "--showConfig") || streq(argv[i], "--explainFiles")
        ) {
            eprintln("tttt: ", argv[i], " isn't supported: tttt only checks");
            return 1;
        } else if (streq(argv[i], "--checkers") && count_value(argc, argv, i)) {
            opts.checkers = (u32)atoi(argv[++i]);
        } else if (streq(argv[i], "--checker-memory") && i + 1 < argc &&
            (streq(argv[i + 1], "off") || (isdigit((unsigned char)argv[i + 1][0]) && atoll(argv[i + 1]) >= 0))
        ) {
            i++;
            opts.checker_memory = streq(argv[i], "off") ? UINT64_MAX : (u64)atoll(argv[i]) << 20;
            opts.checker_memory_given = true;
        } else if (streq(argv[i], "--threads") && count_value(argc, argv, i)) {
            opts.threads = (u32)atoi(argv[++i]);
        } else if (streq(argv[i], "--builders") && count_value(argc, argv, i)) {
            opts.build_jobs = (u32)atoi(argv[++i]);
        } else if (streq(argv[i], "--singleThreaded")) {
            single_threaded = true;
        } else if (streq(argv[i], "--expect-types") && i + 1 < argc) {
            opts.expect_types = argv[++i];
        } else if (streq(argv[i], "--dts-dump") && i + 1 < argc) {
            opts.dts_dump = argv[++i];
        } else if (streq(argv[i], "--no-lib")) {
            opts.no_lib = true;
        } else if (argv[i][0] == '-' && argv[i][1] == '-') {
            if (opts.option_count == MAX_COMPILER_OPTION_ARGS) {
                eprintln("tttt: too many options");
                return 1;
            }
            // Whether this is an option we know is checked once there's a CompilerOptions to try it on. A boolean
            // option may come without a value, anything else has one
            bool has_value = i + 1 < argc && argv[i + 1][0] != '-' && (
                !compiler_option_is_boolean(csview_from(argv[i] + 2)) ||
                streq(argv[i + 1], "true") || streq(argv[i + 1], "false") || streq(argv[i + 1], "null")
            );
            // tsc's parseCommandLineWorker: a name that isn't one of its options (case insensitive here) is TS5023 and
            // nothing is checked. One of its options that we have no use for (most emit options) is ignored
            cstr message = cstr_null;
            unsigned unknown = compiler_option_unknown_diagnostic(csview_from(argv[i] + 2), true, csview_from(argv[i]), &message);
            if (unknown) return command_line_error(opts.pretty, unknown, message);
            if (has_value) {
                message = compiler_option_bad_value_message(csview_from(argv[i] + 2), csview_from(argv[i + 1]));
                if (cstr_size(&message)) return command_line_error(opts.pretty, 6046, message);
            }
            opts.option_names[opts.option_count] = argv[i] + 2;
            opts.option_values[opts.option_count++] = has_value ? argv[++i] : "true";
            if (!strcasecmp(argv[i - has_value] + 2, "pretty")) opts.pretty = !has_value || !streq(argv[i], "false");
        } else {
            // Paths are compacted to the front of argv, which is fine as they never outnumber what we've already read
            opts.paths[opts.path_count++] = argv[i];
        }
    }
    if (vec_error_size(&arg_errors.errors)) {
        cstr text = cstr_null;
        bool colors = opts.pretty >= 0 ? opts.pretty : isatty(STDOUT_FILENO) && !getenv("NO_COLOR");
        for_each (e, vec_error, &arg_errors.errors) {
            if (colors) error_format_pretty(&text, e, "\n", cli_file_name);
            else error_format_plain(&text, e, "\n", cli_file_name);
        }
        fputs(cstr_str(&text), stdout);
        cstr_drop(&text);
        error_ctx_drop(&arg_errors);
        return 1;
    }
    error_ctx_drop(&arg_errors);
    if (help) {
        print_help();
        return 0;
    }
    if (single_threaded) opts.checkers = opts.threads = opts.build_jobs = 1;
    // tttt only checks: it is `tsc --noEmit` (no emit-only option errors), unless the command line says otherwise or the
    // options only emit declarations. -b is tsc -b's, which emits
    bool no_emit_given = false;
    for (intptr_t i = 0; i < opts.option_count; i++) no_emit_given |= !strcasecmp(opts.option_names[i], "noEmit");
    opts.implicit_no_emit = !opts.build && !no_emit_given;

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
    // (Tools that compare with tsgo's own harness run its test content mappers through this, plan 56)
    content_mapper_set_launcher(getenv("TTTT_CONTENT_MAPPER_LAUNCHER"));
    raise_stack_limit();
    int ret = run(argc, argv);
    ast_stats_print();
    return ret;
}
