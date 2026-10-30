// realpath() and PATH_MAX, which strict -std=c2x hides, and sched_getaffinity()
#define _GNU_SOURCE
#include <limits.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/mman.h>

#include "ty/ty.h"
#include "ty/atom_table.h"
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
#include "emit_store.h"
#include <cleat/filesystem.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <sched.h>
#include <poll.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

enum { MAX_COMPILER_OPTION_ARGS = 64 };
// --checker-memory's default: a quarter of the memory, at most DEFAULT_CHECKER_MEMORY_MAX_MB and at least
// DEFAULT_CHECKER_MEMORY_MIN_MB, split between the checkers (each at least 96 MB while that fits, else 64: the cap).
// Recycling rebuilds each checker's warm state: on 16 vCPUs (2026-10-21) the old 96 MB each cost vscode 6% of its
// CPU (26.3 against 24.8 s off, peak 3.2 against 5.0 GB) and dokploy 3x (32 against 10.6 s, 2.4 against 2.8 GB). The
// human takes speed over memory up to ~3x bun's peak (vscode: 2.2 GB); 8 GB never recycles any measured project
#define DEFAULT_CHECKER_MEMORY_MAX_MB 8192
#define DEFAULT_CHECKER_MEMORY_MIN_MB 256

static u64 memory_pool_resident0, memory_pool_available0; // What memory_pool_default() was made of

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
    int rerun; // --rerun 1, --no-rerun 0, -1 when not given (on unless TTTT_NO_RERUN=1): program_set_history_rerun()
    bool no_lib;  // Don't load the standard library, for checking the library files themselves
    const char* project; // -p: a tsconfig.json or the directory of one
    bool build; // -b: paths are projects, each checked with everything it references, in build order
    bool stop_build_on_errors; // --stopBuildOnErrors
    u32 checkers; // --checkers
    u32 checker_cap; // Unless --checkers is given: default_checker_cap()
    u64 checker_memory; // --checker-memory, bytes (UINT64_MAX: off)
    bool checker_memory_given; // Else the default: the memory pool (memory_pool), else that total caps it (each at least 64 MB)
    u64 memory_pool; // The default's resident set bound (memory_pool_default()), 0 when unknown
    u32 threads; // --threads
    const char* expect_types; // --expect-types FILE: print the types of the expressions it lists (expect_types.h)
    u32 build_jobs; // --builders: how many projects -b checks at once (0: decided by threads)
    const char* dts_dump; // --dts-dump DIR: write each emitted declaration file to DIR + its source's absolute path
    bool list_emitted_files; // --listEmittedFiles: `TSFILE: path` for each file written, after the diagnostics
    bool no_write; // --no-write: emit as tsc would, but write nothing (tools on shared checkouts: `-b` emits)
    // Not -b and no noEmit on the command line: tttt is `tsc --noEmit` unless the options make it
    // `tsc --emitDeclarationOnly` (apply_compiler_options())
    bool implicit_no_emit;
    // -b reading its references' emitted declarations (plan 87): the store the program adds its declaration files to
    // (as emit_owner, its build position) and the view of the earlier ones' it reads through
    EmitStore* emit_store;
    u32 emit_owner;
    EmitView* emit_view;
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

// The resident set so far, in MB (0 when unknown)
static long resident_mb() {
    return (long)(process_resident_bytes() >> 20);
}

// Reports the time since *last under the given name and resets *last. Reading and parsing can't be told apart, since
// program_load() does both (and chases imports) in one go
// scripts/memtrack.sh's LD_PRELOADed live-heap tracker (bench/memtrack/memtrack.c), absent otherwise
void memtrack_dump(char const* name) __attribute__((weak));
void memtrack_scan(char const* name) __attribute__((weak));
void timeline_phase(char const* name) __attribute__((weak)); // bench/timeline's LD_PRELOADed sampler, absent otherwise
static void phase_done(Options const* opts, Output* out, const char* name, double* last) {
    if (memtrack_dump && getenv("MEMTRACK")) {
        char phase[64];
        size_t i = 0;
        for (; name[i] && i < sizeof(phase) - 1; i++) phase[i] = name[i] == ' ' || name[i] == '+' ? '_' : name[i];
        phase[i] = 0;
        memtrack_dump(phase);
        memtrack_scan(phase);
    }
    if (timeline_phase) timeline_phase(name);
    const double now = wall_ms();
    // (How long the main thread was on a CPU meanwhile: a loaded machine's preemption is the rest, for the serial
    // phases)
    // (Only under --time: CLOCK_THREAD_CPUTIME_ID isn't served by the vDSO, each read is a syscall)
    static double main_cpu_last;
    if (opts->time) {
        struct timespec cpu;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu);
        double main_cpu = (double)cpu.tv_sec * 1e3 + (double)cpu.tv_nsec / 1e6;
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        static long faults_last;
        static double sys_last, user_last;
        double sys = (double)usage.ru_stime.tv_sec * 1e3 + (double)usage.ru_stime.tv_usec / 1e3;
        double user = (double)usage.ru_utime.tv_sec * 1e3 + (double)usage.ru_utime.tv_usec / 1e3;
        // (The process' CPU over the phase, every thread: over the wall, how many cores the phase kept busy)
        out_eprintln(out, "time: ", name, " ", now - *last, " ms (resident ", resident_mb(), " MB, main thread on a CPU ",
            main_cpu - main_cpu_last, " ms, ", usage.ru_minflt - faults_last, " faults, sys ", sys - sys_last,
            " ms, process CPU ", user + sys - user_last - sys_last, " ms)");
        main_cpu_last = main_cpu;
        faults_last = usage.ru_minflt;
        sys_last = sys;
        user_last = user;
    }
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

// The realpath() of directories real_path_of() met, by absolute path (null: it failed). glibc's realpath() readlinks
// every component of the path (nest: 748 node_modules files, 8.9k failed readlinks under 418 distinct paths), so a
// directory's is its parent's (worked out the same way, once) and its name unless it is a symlink: one lstat() each
typedef char* RealDir;
#define g_pro_key SvStr, svstr
#define g_val RealDir, real_dir
#include <cleat/generic/hmap.h>
static HMapRealDir real_dirs;
static pthread_mutex_t real_dirs_lock = PTHREAD_MUTEX_INITIALIZER;

// realpath() into `resolved`, copied with our malloc (not strdup(): under the debug allocator it isn't one, and a
// thread that lost the race frees its copy)
static char* real_path_plain(char const* path, char* resolved) {
    char const* found = realpath(path, resolved);
    if (!found) return nullptr;
    size_t len = strlen(found);
    char* ret = malloc(len + 1);
    panic_if(!ret, "out of memory");
    memcpy(ret, found, len + 1);
    return ret;
}

// dir: absolute, not "/"; the result is owned by real_dirs
static char const* real_dir_of(csview dir) {
    pthread_mutex_lock(&real_dirs_lock);
    auto cached = hmap_real_dir_get(&real_dirs, dir);
    bool known = cached != nullptr;
    char* ret = known ? cached->second : nullptr;
    pthread_mutex_unlock(&real_dirs_lock);
    if (known) return ret;
    char buf[PATH_MAX];
    memcpy(buf, dir.buf, (size_t)dir.size);
    buf[dir.size] = 0;
    char resolved[PATH_MAX];
    isize slash = dir.size;
    while (slash > 0 && dir.buf[slash - 1] != '/') slash--;
    csview base = csview_slice(dir, slash, dir.size);
    struct stat st;
    if (!base.size || csview_equals(base, ".") || csview_equals(base, "..")) {
        char* found = real_path_plain(buf, resolved);
        ret = found;
    } else if (lstat(buf, &st)) {
        ret = nullptr;
    } else if (S_ISLNK(st.st_mode)) {
        char* found = real_path_plain(buf, resolved);
        ret = found;
    } else {
        // (The parent's, which a successful lstat() says exists)
        char const* parent = slash <= 1 ? "/" : real_dir_of(csview_slice(dir, 0, slash - 1));
        if (parent) {
            size_t parent_len = strlen(parent);
            ret = malloc(parent_len + 2 + (size_t)base.size);
            panic_if(!ret, "out of memory");
            memcpy(ret, parent, parent_len);
            if (parent_len != 1) ret[parent_len++] = '/'; // (realpath("/x") is "/", not "")
            memcpy(ret + parent_len, base.buf, (size_t)base.size);
            ret[parent_len + (size_t)base.size] = 0;
            if (parent_len + (size_t)base.size >= PATH_MAX) {
                free(ret);
                char* found = real_path_plain(buf, resolved);
                ret = found;
            }
        }
    }
    pthread_mutex_lock(&real_dirs_lock);
    auto entry = hmap_real_dir_insert(&real_dirs, cstr_from_sv(dir), ret);
    // (Another thread got there first: its answer, the same one)
    if (!entry.inserted) {
        free(ret);
        ret = entry.ref->second;
    }
    pthread_mutex_unlock(&real_dirs_lock);
    return ret;
}

// Follows symlinks like the real filesystem does, for the program's module resolution: `dir/base` is realpath(dir)
// followed by base when base isn't a symlink itself, so a directory's realpath is worked out once
static cstr real_path_of(void* ctx, csview path) {
    ignore ctx;
    if (path.size >= PATH_MAX) return cstr_null;
    char buf[PATH_MAX];
    memcpy(buf, path.buf, (size_t)path.size);
    buf[path.size] = 0;
    char resolved[PATH_MAX];
    isize slash = path.size;
    while (slash > 0 && path.buf[slash - 1] != '/') slash--;
    csview base = csview_slice(path, slash, path.size);
    // (Anything but an absolute path to a named entry: as it is)
    if (!path.size || path.buf[0] != '/' || slash <= 1 || !base.size || csview_equals(base, ".") || csview_equals(base, "..")) {
        return realpath(buf, resolved) ? cstr_from(resolved) : cstr_null;
    }
    char const* dir_real = real_dir_of(csview_slice(path, 0, slash - 1));
    if (!dir_real) return cstr_null;
    size_t dir_len = strlen(dir_real);
    if (dir_len + 1 + (size_t)base.size >= PATH_MAX) return realpath(buf, resolved) ? cstr_from(resolved) : cstr_null;
    char joined[PATH_MAX];
    memcpy(joined, dir_real, dir_len);
    if (dir_len != 1) joined[dir_len++] = '/'; // (realpath("/x") is "/", not "")
    memcpy(joined + dir_len, base.buf, (size_t)base.size);
    joined[dir_len + (size_t)base.size] = 0;
    struct stat st;
    if (lstat(joined, &st)) return cstr_null;
    if (S_ISLNK(st.st_mode)) return realpath(joined, resolved) ? cstr_from(resolved) : cstr_null;
    return cstr_from(joined);
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
            // (Null when it repeats one: `"extends": ["./base.json", "./base.json"]`, which tsc reports twice)
            if (error) error->phase = ERROR_PHASE_PARSE;
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
        if (!options->no_emit) options->no_emit_implied = true;
        ignore compiler_options_set(options, c_sv("noEmit"), c_sv("true"));
    }
    compiler_options_resolve(options);
    return ok;
}

// last: nothing is checked after it (-b checks a program per project)
static void destroy_program(Program* program, ErrorCtx* errs, [[maybe_unused]] bool last) {
#ifndef DEBUG_BUILD
    // The process is about to exit: freeing the AST node by node was a tenth of a run on a real project. Set
    // TTTT_TEARDOWN to look for leaks with a release build. (Its threads are joined all the same)
    program_join_releases(program);
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
        for (u32 i = 0; i < program_source_count(program); i++) {
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
        // (A reference's `prepend`, removed in 5.5, is ignored: tsgo has no verifyDeprecatedProjectReference)
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

// -b reading emitted declarations (plan 87): every declaration file the program emits goes to the store, written or not
static void store_declarations(Options const* opts, Program* program) {
    for (u32 i = 0; i < program_source_count(program); i++) {
        SourceFile const* source = program_source_at(program, i);
        csview path = cstr_sv(&source->declaration_output_path);
        if (!source->declaration_output || !path.size || program_emit_blocked(program, path)) continue;
        if (getenv("TTTT_DEBUG_EMIT_STORE")) eprintln("stored ", path, " for ", opts->emit_owner);
        emit_store_add(opts->emit_store, opts->emit_owner, path, cstr_sv(source->declaration_output));
    }
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
// tsgo -b's builders (its default --builders)
#define TSGO_BUILDERS 4

// compiler_options_lib_files()'s comma separated names, views of it (c_free_n(ret, libs.size + 1))
static csview* lib_names_of(csview libs, isize* count) {
    csview* ret = c_new_n(csview, libs.size + 1);
    *count = 0;
    for (csview rest = libs; rest.size;) {
        intptr_t end = csview_find_sv(rest, c_sv(","));
        ret[(*count)++] = csview_slice(rest, 0, end);
        if (end == c_NPOS) break;
        rest = csview_slice(rest, end + 1, INTPTR_MAX);
    }
    return ret;
}

// The include walk's root files, on a thread of its own when there's a load pool: the pool gets them a batch at a time
// (program_load_ahead_roots()), and the loader each batch once the pool has it, in root order, so that it loads root N
// while the walk looks for N + 1 (vscode: a 30-55 ms walk, then the loader's own ~85 ms, were one after the other)
typedef struct RootStream {
    Program* program;
    ProjectSet* projects;
    u32 project;
    cstr batch[16]; // For the pool: the files the walk matched
    isize batch_count;
    StrList pending; // Roots in batch, not given to the loader yet
    pthread_mutex_t lock; // roots and done
    pthread_cond_t more;
    StrList roots; // In order
    bool done;
    bool threaded;
    pthread_t thread;
} RootStream;

static void root_stream_flush(RootStream* me) {
    csview views[c_arraylen(my batch)];
    for (isize i = 0; i < my batch_count; i++) views[i] = cstr_sv(&my batch[i]);
    program_load_ahead_roots(my program, views, my batch_count);
    for (isize i = 0; i < my batch_count; i++) cstr_drop(&my batch[i]);
    my batch_count = 0;
    if (!my pending.count) return;
    if (my threaded) pthread_mutex_lock(&my lock);
    for (isize i = 0; i < my pending.count; i++) str_list_push(&my roots, cstr_sv(&my pending.items[i]));
    if (my threaded) {
        pthread_cond_signal(&my more);
        pthread_mutex_unlock(&my lock);
    }
    str_list_clear(&my pending);
}

// (The walk hints a file before it says it's a root: the pool has every root before the loader does)
static void root_stream_found(void* ctx, csview path, bool final) {
    RootStream* me = ctx;
    if (final) {
        str_list_push(&my pending, path);
        if (my pending.count >= 16) root_stream_flush(me);
        return;
    }
    my batch[my batch_count++] = cstr_from_sv(path);
    if (my batch_count == (isize)c_arraylen(my batch)) {
        // (Not the loader's yet: pending stays)
        StrList pending = my pending;
        my pending = (StrList){0};
        root_stream_flush(me);
        my pending = pending;
    }
}

static void root_stream_walk(RootStream* me) {
    // (The directories it lists answer module resolution's probes: vscode listed 1.4k of them twice)
    tsconfig_set_listed_hook(program_offer_listing, my program);
    project_set_root_files_streamed(my projects, my project, root_stream_found, me);
    tsconfig_set_listed_hook(nullptr, nullptr);
    root_stream_flush(me);
    if (my threaded) pthread_mutex_lock(&my lock);
    my done = true;
    if (my threaded) {
        pthread_cond_signal(&my more);
        pthread_mutex_unlock(&my lock);
    }
}

static void* root_stream_run(void* arg) {
    root_stream_walk(arg);
    return nullptr;
}

// Starts the walk: on a thread when the program has a load pool, else here and now
static void root_stream_start(RootStream* me, bool threaded) {
    pthread_mutex_init(&my lock, nullptr);
    pthread_cond_init(&my more, nullptr);
    // (Set before the thread starts: it reads it)
    my threaded = threaded;
    if (my threaded && worker_thread_create(&my thread, root_stream_run, me)) my threaded = false;
    if (!my threaded) root_stream_walk(me);
}

// The i-th root file (a copy: a short cstr's characters move with the list, which the walk grows), waiting for the
// walk to find it; false past the last one
static bool root_stream_at(RootStream* me, isize i, cstr* path) {
    if (my threaded) pthread_mutex_lock(&my lock);
    while (my threaded && i >= my roots.count && !my done) pthread_cond_wait(&my more, &my lock);
    bool ret = i < my roots.count;
    if (ret) cstr_assign_sv(path, cstr_sv(&my roots.items[i]));
    if (my threaded) pthread_mutex_unlock(&my lock);
    return ret;
}

// Waits for the walk's end (every early return of check_files() after root_stream_start() calls this)
static void root_stream_finish(RootStream* me) {
    if (my threaded) worker_thread_join(my thread);
    my threaded = false;
}

static void root_stream_drop(RootStream* me) {
    root_stream_finish(me);
    str_list_drop(&my roots);
    str_list_drop(&my pending);
    pthread_mutex_destroy(&my lock);
    pthread_cond_destroy(&my more);
}

static_failable check_files(Options const* opts, Output* out, csview config, ProjectSet* projects, u32 project,
    ProjectRedirects const* redirects, bool last, CheckCounts* counts, bool defer, returns int
) {
    double last_time = wall_ms();
    if (timeline_phase) timeline_phase("startup");
    ErrorCtx* errs = c_new(ErrorCtx, error_ctx_init());
    Program* program = program_new(errs, opts->emit_view ? emit_view_fs(opts->emit_view) : fs);
    // (--listFilesOnly reports every syntax error and checks nothing: the bodies would be parsed for it before any walk
    // could list them, program_deferred_bodies_parse())
    if (opts->list_files_only) defer = false;
    program_set_defer_bodies(program, defer);
    program_set_history_rerun(program, opts->rerun >= 0 ? opts->rerun : !(getenv("TTTT_NO_RERUN") && streq(getenv("TTTT_NO_RERUN"), "1")));
    if (opts->emit_view) {
        program_set_realpath(program, emit_view_real_path, opts->emit_view);
        program_set_file_size(program, emit_view_file_size, opts->emit_view);
    } else {
        program_set_realpath(program, real_path_of, nullptr);
        program_set_file_size(program, file_size_of, nullptr);
    }
    program_set_checkers(program, opts->checkers);
    program_set_build_mode(program, opts->build);
    // (Or TTTT_FIRST_TOUCH_STAMPS=1, which program_new() reads)
    program_set_checker_cap(program, opts->checker_cap);
    // (--expect-types reads each file's types in the checker that checked it)
    if (!opts->expect_types && opts->checker_memory_given) program_set_checker_memory(program, opts->checker_memory);
    // (No growing start, program_set_checker_memory_shared(): plan 81 stage 1's 16 MB start held vscode's peak at
    // 1.6 GB against 3.0 without it, for 6-20% more CPU; the human takes speed over memory, 2026-10-21)
    if (!opts->expect_types && !opts->checker_memory_given) {
        // The default: the memory pool (memory_pool_default()), else (unknown) the old quarter of the memory
        if (opts->memory_pool) {
            program_set_memory_pool(program, opts->memory_pool);
            program_set_memory_pool_basis(program, memory_pool_resident0, memory_pool_available0);
        }
        else {
            program_set_checker_memory(program, opts->checker_memory);
            program_set_checker_memory_cap(program, opts->checker_memory);
        }
    }
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
    // (A referenced project's outputs: written ones under -p, under -b the ones the build emitted, opts->emit_view)
    if (project_redirects_count(redirects)) program_set_references(program, redirects);
    program_set_threads(program, opts->threads);
    // The config's root files, unless there are files on the command line. The options are final: the load pool
    // starts on the libs and on each file as the include walk finds it (vscode: 29 ms of walk, lib.dom's parse 13).
    // Not for a config with references, which may be a solution (no files: nothing would be loaded)
    // (Streamed: the roots come from the walk's thread, root_stream_at(), and root_files stays empty)
    RootStream stream = { .program = program, .projects = projects, .project = project };
    bool streamed = false;
    if (config.size && !opts->path_count) {
        bool early = !project_set_at(projects, project)->has_references && !project_set_at(projects, project)->root_files_listed;
        if (early) {
            cstr libs = opts->no_lib ? cstr_null : compiler_options_lib_files(program_options(program));
            isize lib_count = 0;
            csview* lib_names = lib_names_of(cstr_sv(&libs), &lib_count);
            program_load_ahead_libs(program, lib_names, lib_count);
            c_free_n(lib_names, cstr_size(&libs) + 1);
            cstr_drop(&libs);
            root_stream_start(&stream, program_has_load_pool(program));
            streamed = true;
        } else {
            StrList const* listed = project_set_root_files(projects, project);
            for (isize i = 0; i < listed->count; i++) str_list_push(&root_files, cstr_sv(&listed->items[i]));
        }
    }
    // (Libraries' errors are dropped once they're loaded: these come after)
    cstr first_root = cstr_null;
    bool has_inputs = opts->path_count + root_files.count > 0 || (streamed && root_stream_at(&stream, 0, &first_root));
    cstr_drop(&first_root);
    if (!has_inputs) {
        root_stream_drop(&stream);
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
        bool skipped = !compiler_options_no_emit_given(options) && options->no_emit_on_error;
        str_list_drop(&root_files);
        destroy_program(program, errs, last);
        // (tsgo: diagnostics and no file to emit is ExitStatus.DiagnosticsPresent_OutputsGenerated, 2)
        if (build_solution) return ok(config_errors ? 1 : 0);
        // (A config without inputs that isn't a mistake, `files` inherited through `extends`, is an empty program)
        if (solution || no_inputs || built) return ok(config_errors ? (skipped ? 1 : 2) : 0);
        return err_d("No input files: none on the command line and none from a tsconfig.json");
    }
    // (The config's parsing and its files' glob)
    phase_done(opts, out, "config", &last_time);

    {
        // Parsing on the side: what the loader will want, as far as that's known now
        cstr libs = opts->no_lib ? cstr_null : compiler_options_lib_files(program_options(program));
        isize lib_count = 0, root_count = opts->path_count + root_files.count; // (Streamed: queued already)
        csview* lib_names = lib_names_of(cstr_sv(&libs), &lib_count);
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
        // (Not waiting for a library a load worker is still on: the roots meanwhile, program_set_defer_libs())
        program_set_defer_libs(program, !getenv("TTTT_NO_DEFER_LIBS"));
        FailableResult libs_loaded = load_stdlib(program);
        program_set_defer_libs(program, false);
        try (libs_loaded) cleanup {
            root_stream_drop(&stream);
            vec_error_drop(&earlier);
            vec_error_drop(&shown);
            destroy_program(program, errs, last);
        }
        vec_error_drop(&errs->errors);
        errs->errors = earlier;
    }
    phase_done(opts, out, "load+parse libs", &last_time);
    if (config.size) {
        verify_references(program, projects, project, true);
        program_report_empty_files(program);
    }

    SourceFile* source = nullptr;
    cstr streamed_root = cstr_null;
    for (intptr_t i = 0;; i++) {
        csview path;
        if (i < opts->path_count) path = csview_from(opts->paths[i]);
        else if (streamed) {
            if (!root_stream_at(&stream, i - opts->path_count, &streamed_root)) break;
            path = cstr_sv(&streamed_root);
        }
        else if (i < opts->path_count + root_files.count) path = cstr_sv(&root_files.items[i - opts->path_count]);
        else break;
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
            cstr_drop(&streamed_root);
            root_stream_drop(&stream);
            str_list_drop(&root_files);
            vec_error_drop(&shown);
            destroy_program(program, errs, last);
        }
    }
    cstr_drop(&streamed_root);
    root_stream_drop(&stream);
    str_list_drop(&root_files);
    program_finish_deferred_libs(program, "after the roots", c_sv(""));
    program_load_automatic_types(program);
    program_take_lib_replacement_errors(program);
    phase_done(opts, out, "load+parse files", &last_time);
    // (The syntax errors of deferred bodies are only found when they're parsed: an output that has to have them all
    // without checking parses the bodies for them, and runs again without deferral if one has some)
    bool bodies_parsed = false;
    if (defer && program_has_deferred_bodies(program) && (opts->list_files_only || count_gating_diagnostics(errs, opts)) &&
        !(bodies_parsed = program_deferred_bodies_parse(program))
    ) {
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
        // (TTTT_EXIT_AFTER_RESOLVE=1: the front end's phases alone, timed with --time; nothing checked or reported)
        if (getenv("TTTT_EXIT_AFTER_RESOLVE")) {
            program_join_releases(program);
            exit(0);
        }

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
        if (opts->dts_dump || ((options->emit_declaration_only || opts->emit_store) && emit_declarations)) {
            program_set_emit_declarations(program, true);
        }
        // (Gated, only the declarations are walked: tsc's emit resolver types only what they print. Same output, a
        // fraction of the work: jest's packages, each gated by TS5108)
        bool declarations_only = (gated || getenv("TTTT_DECLARATIONS_ONLY")) && typecheck && !opts->expect_types;
        if (declarations_only) program_set_declarations_only(program, true);
#ifdef __GLIBC__
        mallopt(M_TOP_PAD, 128 << 10); // (glibc's default, see main())
        // What the front end freed goes back before the checkers grow: the load workers' heaps, their 16 MB pads, the
        // resolver's caches (nest at the defaults: peak RSS -4 MB, react -8; 0.3 ms on nest, 1 ms on vscode).
        // TTTT_NO_TRIM=1: kept
        if (!getenv("TTTT_NO_TRIM")) malloc_trim(0);
#endif
        if (typecheck) program_typecheck(program);
        // (Checked, every body was materialized: program_typecheck() says if one parsed otherwise)
        if (defer && (program_defer_failed(program) ||
            (gated && (!typecheck || declarations_only) && !bodies_parsed && program_has_deferred_bodies(program) && !program_deferred_bodies_parse(program)))
        ) {
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
                " KB of checked source, ", opts->threads, " threads)");
            ProgramRerun const* rerun = program_rerun(program);
            if (rerun) {
                out_eprintln(out, "time: rerun in one checker after a parallel pass of ", rerun->parallel_ms, " ms at ",
                    rerun->checkers, " checkers", rerun->stopped ? ", stopped early" : "", " (", rerun->rule, ", TS", rerun->code, rerun->file ? " in " : "",
                    rerun->file ? cstr_sv(&rerun->file->canonical_path) : c_sv(""), ")");
            }
            for (u32 i = 0; i < program_checker_count(program); i++) {
                Checker const* checker = program_checker_at(program, i);
                out_eprintln(out, "time: checker ", i, ": ", checker->check_ms, " ms, arena ", (i64)(checker->arena.total >> 20),
                    " MB, ", checker_type_count(checker), " types, ", checker->files_taken, " files taken from others");
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
                    checker->types.cap, ", links ", checker->links.len, " / ", checker->links.cap, ", atoms ", atom_table_len(checker->atom_table),
                    ", intersection props ", checker->intersection_props.len, " / ",
                    checker->intersection_props.cap);
                // (Blocks out of reach of their table's base, agents/notes/intern_tables.md: expected none)
                if (checker->types.far_len + checker->links.far_len + checker->intersection_props.far_len) {
                    out_eprintln(out, "time: checker ", i, " intern blocks far from their table's base: types ", checker->types.far_len,
                        ", links ", checker->links.far_len, ", intersection props ", checker->intersection_props.far_len);
                }
                out_eprintln(out, "time: checker ", i, " node links ", checker->node_links.len, " / ", checker->node_links.cap);
            }
#ifdef TTTT_ARENA_SITES
            arena_sites_print();
#endif
#ifdef TTTT_TAINT
            taint_print();
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
    // (What tsc -b writes, the programs after this one read: tsc 5.6+ writes past errors, but not with noEmitOnError)
    if (opts->emit_store && emit_declarations && typecheck && !(options->no_emit_on_error && vec_error_size(&shown))) {
        store_declarations(opts, program);
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
    if (ret) ret = !compiler_options_no_emit_given(options) && (options->no_emit_on_error || declaration_blocked || program_emit_any_blocked(program)) ? 1 : 2;
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
    bool started; // Under Build.lock
    // The jobs (by build position) whose emitted declarations the program sees, which it waits for, and its view of them
    bool* visible;
    EmitView* view;
    // Under Build.lock: the jobs still to finish that read this one's emitted declarations, the job itself included
    // (emit_store_release() once none is left)
    u32 readers;
} BuildJob;

typedef struct Build {
    Options const* opts;
    Options project_opts; // The command line's options for each project, without its paths
    ProjectSet const* projects;
    u32 const* order;
    u32 count;
    BuildJob* jobs;
    EmitStore* store;
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
    Options opts = my project_opts;
    opts.emit_store = my store;
    opts.emit_owner = i;
    opts.emit_view = job->view = emit_view_new(my store, fs, job->visible);
    job->result = check_files(&opts, &job->out, cstr_sv(&project->config), (ProjectSet*)my projects,
        my order[i], job->redirects, i + 1 == my count, &job->counts, !my project_opts.no_defer_bodies, &job->ret);
}

// Job i is done (under Build.lock with build threads): what nothing will read any more is freed
static void build_job_done(Build* me, u32 i) {
    my jobs[i].done = true;
    if (my jobs[i].visible) {
        for (u32 j = 0; j < i; j++) {
            if (my jobs[i].visible[j] && !--my jobs[j].readers) emit_store_release(my store, j);
        }
    }
    if (!--my jobs[i].readers) emit_store_release(my store, i);
}

static double build_epoch;

static void* build_thread(void* arg) {
    Build* me = arg;
    for (;;) {
        // The first job (in build order) whose program reads only what's done: tsgo's builders wait on their
        // references. (Some job is running whenever none is ready: the first one not started waits only on started ones)
        u32 i = UINT32_MAX;
        pthread_mutex_lock(&my lock);
        for (;;) {
            bool pending = false;
            u32 end = atomic_load(&my end);
            for (u32 k = 0; k < end && i == UINT32_MAX; k++) {
                if (my jobs[k].started) continue;
                pending = true;
                bool ready = true;
                for (u32 j = 0; j < k && ready && my jobs[k].visible; j++) ready = !my jobs[k].visible[j] || my jobs[j].done;
                if (ready) i = k;
            }
            if (i != UINT32_MAX || !pending) break;
            pthread_cond_wait(&my done, &my lock);
        }
        if (i != UINT32_MAX) my jobs[i].started = true;
        pthread_mutex_unlock(&my lock);
        if (i == UINT32_MAX) break;
        double started = wall_ms();
        build_run(me, i);
        if (getenv("TTTT_BUILD_TIMES")) {
            fprintf(stderr, "job %u %s: %.0f..%.0f ms\n", i, cstr_str(&project_set_at(my projects, my order[i])->config),
                started - build_epoch, wall_ms() - build_epoch);
        }
        pthread_mutex_lock(&my lock);
        build_job_done(me, i);
        pthread_cond_broadcast(&my done);
        pthread_mutex_unlock(&my lock);
    }
    return nullptr;
}

// -b: every project the paths name (tsconfig.json files or their directories, `.` without any), each with what it
// references before it (tsc's build order). Each is its own program, with its own options and the command line's, and
// reads the declaration files its references emitted, in memory (plan 87: emit_store.h). Like tsc 5.6+ goes on past
// a project with errors (not with --stopBuildOnErrors). With --threads 4, two projects are checked at once on two
// threads each (opts->build_jobs), each once what it reads is done, their output printed in build order
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

    // tsgo 7.1's builders take the projects in build order stably sorted by dependency depth (the orchestrator's
    // computeScheduleOrder: projects without references in the build first, then their dependents...) and report them
    // in build order. Jobs are numbered in that schedule order; printed[k] is the job of build position k
    u32* printed = c_new_n(u32, count + 1);
    {
        u32 project_count = 0;
        for (u32 k = 0; k < count; k++) if (order[k] + 1 > project_count) project_count = order[k] + 1;
        u32* at = c_new_n(u32, project_count + 1);
        for (u32 p = 0; p < project_count; p++) at[p] = UINT32_MAX;
        for (u32 k = 0; k < count; k++) at[order[k]] = k;
        u32* depth = c_new_n(u32, count + 1);
        u32 max_depth = 0;
        for (u32 k = 0; k < count; k++) {
            Project const* p = project_set_at(projects, order[k]);
            depth[k] = 0;
            for (u32 r = 0; p->readable && r < p->reference_count; r++) {
                u32 ref = p->referenced[r] < project_count ? at[p->referenced[r]] : UINT32_MAX;
                if (ref < k && depth[ref] + 1 > depth[k]) depth[k] = depth[ref] + 1;
            }
            if (depth[k] > max_depth) max_depth = depth[k];
        }
        u32* scheduled = c_new_n(u32, count + 1);
        u32 s = 0;
        for (u32 d = 0; d <= max_depth && s < count; d++) {
            for (u32 k = 0; k < count; k++) {
                if (depth[k] != d) continue;
                printed[k] = s;
                scheduled[s++] = order[k];
            }
        }
        memcpy(order, scheduled, sizeof(u32) * count);
        c_free_n(scheduled, count + 1);
        c_free_n(depth, count + 1);
        c_free_n(at, project_count + 1);
    }

    u32 jobs = opts->build_jobs ? opts->build_jobs : opts->threads >= 4 ? 2 : 1;
    if (jobs > count) jobs = count ? count : 1;
    Build b = { .opts = opts, .project_opts = *opts, .projects = projects, .order = order, .count = count };
    b.project_opts.path_count = 0;
    b.project_opts.threads = opts->threads / jobs ? opts->threads / jobs : 1;
    // (The physical cores split between the projects checking at once like the threads)
    if (opts->checker_cap) b.project_opts.checker_cap = opts->checker_cap / jobs ? opts->checker_cap / jobs : 1;
    b.jobs = calloc((size_t)count + 1, sizeof(BuildJob));
    atomic_init(&b.end, count);
    // Everything the programs read of the project set, made before any of them runs. A program reads the outputs of
    // the projects tsc -b built before it, references or not (an import into another package's `dist`). tsgo's
    // builders (4, else --builders) take projects in schedule order and only wait for references: an unreferenced
    // project is built by the time one 4 places later starts, else it races with it, and counts as not built
    // (hacks.md). (The one way --builders changes the output, like tsgo's: `--builders 1` is tsc 5's sequential -b)
    u32 window = opts->build_jobs ? opts->build_jobs : TSGO_BUILDERS;
    // A program sees what its references (transitively) and the projects the window counts as built emitted, through
    // its view of the store
    b.store = emit_store_new(count);
    u32 position_count = 0;
    for (u32 i = 0; i < count; i++) if (order[i] + 1 > position_count) position_count = order[i] + 1;
    u32* position = c_new_n(u32, position_count ? position_count : 1);
    for (u32 i = 0; i < position_count; i++) position[i] = UINT32_MAX;
    for (u32 i = 0; i < count; i++) position[order[i]] = i;
    u32* stack = c_new_n(u32, count + 1);
    for (u32 i = 0; i < count; i++) {
        b.jobs[i].out.buffered = true; // (Printed in build order, which isn't the schedule's)
        b.jobs[i].readers++;
        if (!project_set_at(projects, order[i])->readable) continue;
        project_set_root_files(projects, order[i]);
        u32 built = i >= window ? i - window + 1 : 0;
        b.jobs[i].redirects = project_redirects_new(projects, order[i]);
        bool* visible = b.jobs[i].visible = calloc((size_t)count + 1, sizeof(bool));
        for (u32 j = 0; j < built; j++) {
            visible[j] = true;
            b.jobs[j].readers++;
        }
        u32 depth = 0;
        stack[depth++] = order[i];
        while (depth) {
            Project const* p = project_set_at(projects, stack[--depth]);
            if (!p->readable) continue;
            for (u32 r = 0; r < p->reference_count; r++) {
                u32 at = p->referenced[r] < position_count ? position[p->referenced[r]] : UINT32_MAX;
                if (at >= i || visible[at]) continue;
                visible[at] = true;
                b.jobs[at].readers++;
                if (depth < count) stack[depth++] = p->referenced[r];
            }
        }
    }
    c_free_n(stack, count + 1);
    c_free_n(position, position_count ? position_count : 1);
    pthread_t* threads = nullptr;
    build_epoch = wall_ms();
    if (jobs > 1) {
        pthread_mutex_init(&b.lock, nullptr);
        pthread_cond_init(&b.done, nullptr);
        threads = c_new_n(pthread_t, jobs);
        for (u32 t = 0; t < jobs; t++) {
            panic_if(worker_thread_create(&threads[t], build_thread, &b), "couldn't start a build thread");
        }
    }

    CheckCounts counts = {0};
    FailableResult failure = {0};
    u32 next_run = 0; // (Without build threads: the jobs are run here, in schedule order, as far as each print needs)
    for (u32 k = 0; k < count; k++) {
        u32 i = printed[k];
        BuildJob* job = &b.jobs[i];
        if (jobs > 1) {
            pthread_mutex_lock(&b.lock);
            while (!job->done) pthread_cond_wait(&b.done, &b.lock);
            pthread_mutex_unlock(&b.lock);
        } else {
            for (; !job->done && next_run < count; next_run++) {
                build_run(&b, next_run);
                build_job_done(&b, next_run);
            }
        }
        output_flush(&job->out);
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
        for (u32 t = 0; t < jobs; t++) worker_thread_join(threads[t]);
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
        emit_view_destroy(b.jobs[i].view);
        free(b.jobs[i].visible);
    }
    emit_store_destroy(b.store);
    free(b.jobs);
    free(order);
    c_free_n(printed, count + 1);
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
#define TTTT_TYPESCRIPT_VERSION "7.1.0-beta"

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
        "  --checkers K  check with K checkers (default: one per thread, but at most one per 64 KB of checked\n"
        "                source, and past 4 one per physical core or per 4 MB, whichever is more). Each gets\n"
        "                contiguous runs of the files; the output doesn't depend on K\n"
        "  --builders J  with -b, check J projects at once, each on threads/J threads (default: 2 with\n"
        "                4 threads, else 1). The output doesn't depend on it\n"
        "  --singleThreaded  one checker on one thread, one project at a time\n"
        "  --checker-memory MB  what the checkers' types may take together (split between them, each at least\n"
        "                96) before each drops them and goes on fresh (0: a fresh checker per file, off: never).\n"
        "                Default: the whole process stays below what it held at start plus 80% of the memory\n"
        "                available then (MemAvailable, cgroup limits; TTTT_MEM_AVAILABLE_MB overrides it): past\n"
        "                that the checkers holding the most start fresh\n"
        "                The output doesn't depend on it\n"
        "  --no-rerun    when several checkers (or a recycled one) report an instantiation limit or a\n"
        "                circularity, keep their output instead of checking everything again in one checker\n"
        "                (faster; where such a diagnostic lands may then depend on the checkers). --rerun: the\n"
        "                default, tsc --singleThreaded's output at any K; TTTT_NO_RERUN=1 is --no-rerun\n"
        "  --threads N   parse, bind and check on N threads (default: the number of cores; checking uses at\n"
        "                most K of them)\n"
        "  --OPTION [VALUE]  a tsc compiler option: --target es2020, --lib es2015,dom, --strict false,\n"
        "                --noImplicitAny... (strict is on by default, a boolean without a value means true)\n\n",
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

// The physical cores among the CPUs this process may run on (distinct package and core ids in sysfs), 0 when unknown.
// macOS: hw.physicalcpu (no affinity masks; performance and efficiency cores alike)
static u32 physical_cores(void) {
#ifdef __APPLE__
    int cores = 0;
    size_t size = sizeof cores;
    return sysctlbyname("hw.physicalcpu", &cores, &size, nullptr, 0) || cores <= 0 ? 0 : (u32)cores;
#else
    cpu_set_t allowed;
    if (sched_getaffinity(0, sizeof allowed, &allowed)) return 0;
    enum { MAX_CORES = 1024 };
    static u64 seen[MAX_CORES];
    u32 count = 0;
    for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
        if (!CPU_ISSET(cpu, &allowed)) continue;
        long ids[2];
        char const* names[2] = { "physical_package_id", "core_id" };
        for (int i = 0; i < 2; i++) {
            char path[96];
            snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/topology/%s", cpu, names[i]);
            FILE* file = fopen(path, "r");
            if (!file) return 0;
            int read = fscanf(file, "%ld", &ids[i]);
            fclose(file);
            if (read != 1) return 0;
        }
        u64 id = (u64)ids[0] << 32 | (u32)ids[1];
        u32 j = 0;
        while (j < count && seen[j] != id) j++;
        if (j < count) continue;
        if (count == MAX_CORES) return 0;
        seen[count++] = id;
    }
    return count;
#endif
}

// The default checker count's ceiling: one checker per physical core, not per hardware thread, but every thread up
// to 4. A second hardware thread checking on a core doubles the checkers' CPU and memory (each builds the declared
// world again) for little: on 12 cores / 24 threads K12 -> K24 bought 0-11% wall, while 24 threads oversubscribed on
// 4 vCPUs spent 6-26% more CPU at K24 than at K12 on the corpora (dokploy 42%, monkeytype 31%) and 0.2-2 GB more
// peak. Below 4 hardware threads the second one is worth it (4 vCPUs on 2 cores: K2 is 10-26% slower than K4). A big
// program goes past it (program.c's auto_checker_count(): vscode is 17% faster at K24 there)
static u32 default_checker_cap(u32 threads) {
    u32 cores = physical_cores();
    if (!cores) return 0;
    u32 few = threads < 4 ? threads : 4;
    return cores > few ? cores : few;
}

// The default memory pool (program_set_memory_pool()): what the process may reach while checking, bytes: its
// resident set now plus 80% of what the system could still give it (memory_available_bytes(): MemAvailable, cgroup
// limits), so that it never pushes the machine into an OOM kill of itself or anyone else but otherwise uses what is
// free. 0 when the available memory is unknown (the quarter-of-RAM split then)
#define MEMORY_POOL_PERCENT 80
static u64 memory_pool_default(void) {
    u64 available = memory_available_bytes();
    if (!available) return 0;
    memory_pool_resident0 = process_resident_bytes();
    memory_pool_available0 = available;
    return memory_pool_resident0 + available / 100 * MEMORY_POOL_PERCENT;
}

// --checker-memory's old default, bytes (when memory_pool_default() is unknown)
static u64 default_checker_memory(void) {
    long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
    u64 quarter = pages > 0 && page > 0 ? (u64)pages * (u64)page / 4 : (u64)DEFAULT_CHECKER_MEMORY_MIN_MB << 20;
    u64 most = (u64)DEFAULT_CHECKER_MEMORY_MAX_MB << 20, least = (u64)DEFAULT_CHECKER_MEMORY_MIN_MB << 20;
    return quarter > most ? most : quarter < least ? least : quarter;
}

static int run(int argc, const char** argv) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    // Every core parses and binds; checking uses at most `checkers` of them (0: one per core, fewer for a small program,
    // auto_checker_count(): the output doesn't depend on the count, plan 62)
    Options opts = {
        .paths = argv, .pretty = -1, .rerun = -1, .checkers = 0, .threads = cores < 1 ? 1 : (u32)cores,
        .checker_memory = default_checker_memory(),
        .memory_pool = memory_pool_default(),
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
        } else if (streq(argv[i], "--no-rerun")) {
            opts.rerun = 0;
        } else if (streq(argv[i], "--rerun")) {
            opts.rerun = 1;
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
    if (!opts.checkers) opts.checker_cap = default_checker_cap(opts.threads);
    // (TTTT_GRAPH_SEED, ty/graph_sched.c: one worker runs at a time, so nothing may block outside its token)
    if (getenv("TTTT_GRAPH_SEED") && (opts.checker_memory != UINT64_MAX || !opts.checkers || opts.threads != opts.checkers)) {
        eprintln("tttt: TTTT_GRAPH_SEED needs --checker-memory off (the graph-wide recycle's barrier blocks) and --threads "
            "equal to --checkers (no materialize-ahead helpers)");
        return 1;
    }
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

// The kernel's teardown of a big address space (every page and page table freed: ~25 ms on drizzle, 5% of its wall at
// the defaults on a 4 KB-page machine) runs in exit_group() before the parent's wait4() returns and before the
// output pipes see their end. A child sharing the address space (CLONE_VM, its own process) takes it over: it drops
// its copy of the descriptors, waits for this process to be gone and exits holding the last reference, so the
// teardown runs after the caller has the status and the output. (Its CPU time is then nobody's: TTTT_SYNC_EXIT=1
// tears down in the process, for measuring sys. Not under valgrind, sanitizers or a debug build, nor with leak checks.
// Linux only: macOS has neither clone() nor pidfds)
#ifdef __linux__
static int exit_reaper(void* arg) {
    int pidfd = (int)(intptr_t)arg;
    syscall(SYS_close_range, 0, (unsigned)pidfd - 1, 0);
    syscall(SYS_close_range, (unsigned)pidfd + 1, ~0u, 0);
    struct pollfd wait = { .fd = pidfd, .events = POLLIN };
    // (ppoll with no timeout and no mask: aarch64 has no poll syscall)
    while (syscall(SYS_ppoll, &wait, 1, nullptr, nullptr, 0) < 0) {}
    syscall(SYS_exit, 0);
    return 0;
}

static void exit_detached(void) {
#if defined DEBUG_BUILD || defined __SANITIZE_THREAD__ || defined __SANITIZE_ADDRESS__
    return;
#endif
#if defined __has_feature
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
    return;
#endif
#endif
    char const* preload = getenv("LD_PRELOAD");
    if ((preload && strstr(preload, "vgpreload")) || getenv("TTTT_SYNC_EXIT") || getenv("TTTT_TEARDOWN")) return;
    int pidfd = (int)syscall(SYS_pidfd_open, getpid(), 0);
    if (pidfd < 0) return;
    size_t const stack_size = 64 << 10;
    char* stack = mmap(nullptr, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED || clone(exit_reaper, stack + stack_size, CLONE_VM, (void*)(intptr_t)pidfd) < 0) close(pidfd);
}
#else
static void exit_detached(void) {}
#endif

int main(int argc, const char** argv) {
    // (Tools that compare with tsgo's own harness run its test content mappers through this, plan 56)
    content_mapper_set_launcher(getenv("TTTT_CONTENT_MAPPER_LAUNCHER"));
    raise_stack_limit();
    // (Loading and declaring grow glibc's per-thread heaps by an mprotect() per 128 KB, each one taking the mmap lock
    // for writing and stalling every other thread's page faults: vscode's front end made 21k of them. A 16 MB pad
    // until the checkers start; kept during checking it held up to 4% more peak RSS, trimmed less)
#ifdef __GLIBC__
    mallopt(M_TOP_PAD, 16 << 20);
    // (glibc gives a block of over M_MMAP_THRESHOLD a mapping of its own, unmapped at its free(), and gives a heap's
    // free top back past M_TRIM_THRESHOLD with a madvise(): each one a TLB shootdown on every CPU running another of
    // our threads, ~100 a run in the checkers on drizzle, zod or vue (the intern tables' and relation caches' growth).
    // A one-shot CLI keeps what it freed: the blocks it frees are reused, malloc_trim() before checking gives the front
    // end's back (which ignores the threshold). 32 MB is glibc's most; past it a block is still a mapping)
    mallopt(M_MMAP_THRESHOLD, 32 << 20);
    mallopt(M_TRIM_THRESHOLD, 1 << 30);
#endif
    int ret = run(argc, argv);
    ast_stats_print();
    exit_detached();
    return ret;
}
