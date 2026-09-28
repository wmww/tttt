#pragma once

#include "span.h"
#include "compiler_options.h"

typedef struct Program Program;
typedef struct ErrorCtx ErrorCtx;
typedef struct SourceFile SourceFile;
typedef struct Fs Fs;
typedef union arc_js_object ArcObject;

Program* program_new(ErrorCtx* errs, Fs const* fs);
void program_destroy(Program* me);

// The directory the standard library files (es5.d.ts & co.) are loaded from
void program_set_lib_dir(Program* me, csview dir);
// How the program's module resolution follows symlinks (see module_resolution.h), before loading anything
void program_set_realpath(Program* me, cstr (*realpath)(void* ctx, csview path), void* ctx);
// A `/// <reference lib="x">` in a file that isn't a lib loads that lib into this program, like tsc does. Not for a
// program whose libs are copied from another (program_copy_globals_from())
void program_follow_lib_references(Program* me);

// Change them before loading anything, and call compiler_options_resolve() when done. Parsing, name resolution and
// checking of this program read them through compiler_options()
CompilerOptions* program_options(Program* me);

// Where the program's diagnostics go (the checker reports tsc's `error(currentNode, ...)` diagnostics straight into it)
ErrorCtx* program_errors(Program const* me);

// Parses the given file, loads all imports, adds them to the program and returns the SourceFile for the given path.
// Can be called multiple times. Fails if path can't be read; problems inside the file are reported through the
// program's ErrorCtx instead. A JavaScript file without `allowJs` is such a problem (TS6504) and gives null
failable program_load(Program* me, csview path, returns SourceFile*);

// The packages that are part of every program: the `types` option, else everything in the type roots (`typeRoots`,
// else node_modules/@types from the config's or the current directory upwards). Call it after the root files
void program_load_automatic_types(Program* me);

// The file at path if it is part of the program (loaded or imported), null otherwise
SourceFile* program_find_source(Program const* me, csview path);

// Loads the standard library file based on the name (such as es5 or es2015.collection)
failable program_load_stdlib(Program* me, csview name, returns SourceFile*);

// Add all declarations from other program to this one. Must be called after resolving names on other and before
// resolving names on me. me must not be used after other is destroyed
void program_copy_globals_from(Program* me, Program const* other);

// Should be called once after loading all files is done. Should always be called before typechecking
void program_resolve_names(Program* me);

// Typechecks the loaded file and all imports, does not typecheck libraries loaded with program_load_lib()
void program_typecheck(Program* me);

// How many checkers program_typecheck() deals the files to (tsgo's --checkers; 0 is the default, 1). The diagnostics
// depend on it (only in the order of union members inside messages: type ids are per checker), not on anything else.
// Call it while loading
void program_set_checkers(Program* me, u32 count);

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

// A global by name and meaning (SymbolFlags), null if there is none
struct Symbol const* program_get_global(Program const* me, csview name, u32 meaning);

// Returns the number of source files loaded or imported
intptr_t program_get_file_count(Program const* me);
