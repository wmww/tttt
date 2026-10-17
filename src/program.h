#pragma once

#include "span.h"
#include "compiler_options.h"
#include "module_specifiers.h"

typedef struct Program Program;
typedef struct ErrorCtx ErrorCtx;
typedef struct SourceFile SourceFile;
typedef struct Fs Fs;
typedef struct Symbol Symbol;
typedef struct ModuleRef ModuleRef;
typedef union arc_js_object ArcObject;

Program* program_new(ErrorCtx* errs, Fs const* fs);
void program_destroy(Program* me);

// The directory the standard library files (es5.d.ts & co.) are loaded from
void program_set_lib_dir(Program* me, csview dir);
// How the program's module resolution follows symlinks (see module_resolution.h), before loading anything
void program_set_realpath(Program* me, cstr (*realpath)(void* ctx, csview path), void* ctx);
// How big the file at path is, a guess (0 if unknown): with threads, the biggest files are parsed first
void program_set_file_size(Program* me, i64 (*file_size)(void* ctx, csview path), void* ctx);
// A `/// <reference lib="x">` in a file that isn't a lib loads that lib into this program, like tsc does. Not for a
// program whose libs are copied from another (program_copy_globals_from())
void program_follow_lib_references(Program* me);

// The projects the program's project references (project.h), before loading anything (and before
// program_set_threads() has a load pool start). With outputs (tsc's command line), a referenced project's source is
// read as its output declaration file, which is a TS6305 where it hasn't been built. Without (tsc's language service,
// what tttt's -b does), their outputs are read as their sources, their files are part of the program but unchecked and
// unreported (program_load_automatic_types() drops their parse errors). Either way module names in them resolve with
// their project's options. redirects must outlive the program
typedef struct ProjectRedirects ProjectRedirects;
void program_set_references(Program* me, ProjectRedirects const* redirects, bool outputs);

// Change them before loading anything, and call compiler_options_resolve() when done. Parsing, name resolution and
// checking of this program read them through compiler_options()
CompilerOptions* program_options(Program* me);

// The tsconfig.json the options came from (tsc's options.configFile): diagnostics about the options go on it. Returns
// its source file, which isn't part of the program (nothing parses or checks it), null if it can't be read
SourceFile const* program_set_config_file(Program* me, csview path);

// A diagnostic at those byte offsets of the config file (a global one without a config)
typedef struct Error Error;
Error* program_config_error(Program* me, u32 start, u32 end, unsigned code, cstr message);
Error* program_extended_config_error(Program* me, csview path, u32 start, u32 end, unsigned code, cstr message);
// For a config that gave no root files: tsc's TS18002 (an empty `files` without references) or TS18003 (no inputs, no
// `files` nor `references`). Whether it reported one
bool program_report_no_inputs(Program* me);
// tsc's verifyCompilerOptions (and the emit path checks) on their own: for a program without any files (no inputs, a
// solution config: tsc still makes the program and reports its options diagnostics), or whose names aren't resolved
// (syntactic errors). program_resolve_names() does it otherwise
void program_verify_options(Program* me);

// Where the program's diagnostics go (the checker reports tsc's `error(currentNode, ...)` diagnostics straight into it)
ErrorCtx* program_errors(Program const* me);

// Parses the given file, loads all imports, adds them to the program and returns the SourceFile for the given path.
// Can be called multiple times. Fails if path can't be read; problems inside the file are reported through the
// program's ErrorCtx instead. A JavaScript file without `allowJs` is such a problem (TS6504) and gives null
failable program_load(Program* me, csview path, returns SourceFile*);

// With threads (program_set_threads()), starts parsing these ahead of program_load(), program_load_stdlib() and
// program_load_automatic_types(), which still decide everything: the libs by name (`es5`), the root files by path,
// and the automatic types. Only makes loading faster
void program_load_ahead(Program* me, csview const* libs, isize lib_count, csview const* roots, isize root_count);

// The packages that are part of every program: the `types` option, else everything in the type roots (`typeRoots`,
// else node_modules/@types from the config's or the current directory upwards). Call it after the root files
void program_load_automatic_types(Program* me);

// The file at path if it is part of the program (loaded or imported), null otherwise
SourceFile* program_find_source(Program const* me, csview path);
// The program's files, in the order they were added
u32 program_source_count(Program const* me);
SourceFile* program_source_at(Program const* me, u32 index);

// Loads the standard library file based on the name (such as es5 or es2015.collection)
failable program_load_stdlib(Program* me, csview name, returns SourceFile*);

// Add all declarations from other program to this one. Must be called after resolving names on other and before
// resolving names on me. me must not be used after other is destroyed
void program_copy_globals_from(Program* me, Program const* other);

// The files in tsc's order (the default libraries first, tsc's lib order): for listing them without resolving names.
// program_resolve_names() does it otherwise
void program_order_stdlib_first(Program* me);

// Should be called once after loading all files is done. Should always be called before typechecking
void program_resolve_names(Program* me);

// Typechecks the loaded file and all imports, does not typecheck libraries loaded with program_load_lib()
void program_typecheck(Program* me);

// The global diagnostics tsc's checker reports when it's made (initializeTypeChecker: TS2318 for the global types it
// needs), without checking anything: tsc's CLI doesn't check files when there are some. program_typecheck() reports
// them otherwise. Call it after program_resolve_names()
void program_report_global_diagnostics(Program* me);

// How many checkers program_typecheck() deals the files to (tsgo's --checkers). 0, the default: one per thread, fewer
// for a small program (auto_checker_count()). The diagnostics don't depend on it (plan 62). Call it while loading
void program_set_checkers(Program* me, u32 count);
// How much memory (bytes, split between the checkers) program_typecheck()'s checkers may hold before it drops one and
// goes on in a fresh one (see check_share()); UINT64_MAX, the default, never; 0 a fresh checker per file. The
// diagnostics don't depend on it. A recycled checker is gone: program_checker_of() needs the default. Call it while
// loading
void program_set_checker_memory(Program* me, u64 bytes);
// However many checkers there are, together they hold at most `bytes` (each at least 64 MB): the CLI's default, so a
// machine with many cores doesn't hold K x the per-checker budget (plan 75). Call it while loading
void program_set_checker_memory_cap(Program* me, u64 bytes);
// The bytes of the files the checkers check, the default library's aside (what the default checker count follows)
u64 program_checked_source_bytes(Program const* me);

// The program is checked like tsc's command line does (not like its test harness): with noEmit or noEmitOnError, the
// declaration diagnostics (declaration_emit.h) are only reported if there is no other diagnostic, `earlier_errors`
// saying whether earlier phases reported some. Call it before program_typecheck()
void program_declaration_diagnostics_cli(Program* me, bool earlier_errors);
// Declaration emit (plan 64): program_typecheck() also writes each emitted file's `.d.ts` text into its
// SourceFile.declaration_output, on the checker that checked it. Call it before program_typecheck()
void program_set_emit_declarations(Program* me, bool emit);
// Whether tsc would emit the file at all (sourceFileMayBeEmitted: not a declaration file, a library's, a referenced
// project's)
bool program_source_may_be_emitted(Program const* me, SourceFile const* source);
// Whether an output path (absolute) is one TS5055/TS5056 said can't be written (tsc's IsEmitBlocked)
bool program_emit_blocked(Program* me, csview output);

// How a declaration file of `importing` (a file of this program) would name the module `target` (tsc's
// getSpecifierForModuleSymbol): an import of the file that resolves to it, else module_specifier_generate(). override:
// the resolution mode an import type asks for. Thread safe
cstr program_module_specifier(Program* me, SourceFile const* importing, SourceFile const* target, SpecifierMode override);
// The same, with the resolution mode that picks the ending (getSpecifierForModuleSymbol's resolutionMode: "js" in ESM)
// when that isn't the importing file's default
cstr program_module_specifier_ending(
    Program* me, SourceFile const* importing, SourceFile const* target, SpecifierMode override, SpecifierMode ending_mode
);
// tsc's getModeForUsageLocation of an import of the file
SpecifierMode program_usage_mode(Program const* me, SourceFile const* file, ModuleRef const* ref);
// How tsc 6 names a module symbol from a place in `from` (symbolToString and symbolToTypeNode with an enclosing
// declaration: getSpecifierForModuleSymbol, which always has a resolver host now), in double quotes: a file by the
// module specifier an import in `from` would write (`"./b"`). An ambient module, a JSON file, no place, or an outFile
// bundle (tsc: non-relative from the common source directory, not ported) as module_name_for_error()
cstr program_module_display_name(Program* me, Symbol const* module, SourceFile const* from, SpecifierMode override, ModuleRef const* usage);
// tsgo's tryGetModuleNameFromAmbientModule (its first loop): a module file that a `declare module "m" {}` augments by a
// non-relative name is named "m" by every import type (unquoted, empty if none)
csview module_ambient_name(Symbol const* module);
// Different for every program this process makes (what memos that outlive a program key on, rather than its address)
u32 program_serial(Program const* me);
// tsc's getDefaultResolutionModeForFile
SpecifierMode program_default_resolution_mode(Program const* me, SourceFile const* file);

// How many threads run those checkers (0 is the default, 1: the calling thread alone). The output doesn't depend on
// it. Call it while loading
void program_set_threads(Program* me, u32 count);
// Plan 78: the parser skips the outermost function bodies of TypeScript files where that's safe, and the checker that
// first needs one parses and walks all of its file's (program_typecheck(), function_impl_body()). Off by default.
// Call it before loading anything
void program_set_defer_bodies(Program* me, bool defer);
// Whether some file had bodies deferred: an output that needs them all before checking (syntax errors that gate the
// checking) has to run again without deferral
bool program_has_deferred_bodies(Program const* me);
// After program_typecheck(): a deferred body's materialization could be told from the eager parse and walk (a syntax
// error in a body, a signature that would have seen the body's names): the output is wrong, run again without deferral
bool program_defer_failed(Program const* me);

// Makes this program's checker (created on first use) the one that types get evaluated in, until another program is
// made current or this one is destroyed. program_typecheck() does this, call it yourself to look at types of a program
// that isn't typechecked
void program_make_current(Program* me);

struct Binder const* program_get_binder(Program const* me);

// Null before program_make_current()
struct Checker* program_get_checker(Program const* me);

// The checkers program_typecheck() used (the first is program_get_checker()'s), 0 before
u32 program_checker_count(Program const* me);
struct Checker* program_checker_at(Program const* me, u32 index);
struct Checker* program_checker_of(Program const* me, SourceFile const* file);

// A global by name and meaning (SymbolFlags), null if there is none
struct Symbol const* program_get_global(Program const* me, csview name, u32 meaning);

// Returns the number of source files loaded or imported
intptr_t program_get_file_count(Program const* me);
