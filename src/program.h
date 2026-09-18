#pragma once

#include "span.h"

typedef struct Program Program;
typedef struct ErrorCtx ErrorCtx;
typedef struct SourceFile SourceFile;
typedef struct Fs Fs;
typedef struct Scope Scope;
typedef union arc_js_object ArcObject;

Program* program_new(ErrorCtx* errs, Fs const* fs);
void program_destroy(Program* me);

// Parses the given file, loads all imports, adds them to the program and returns the SourceFile for the given path.
// Can be called multiple times. Fails if path can't be read; problems inside the file are reported through the
// program's ErrorCtx instead
failable program_load(Program* me, csview path, returns SourceFile*);

// Loads the standard library file based on the name (such as es5 or es2015.collection)
failable program_load_stdlib(Program* me, csview name, returns SourceFile*);

// Add all declarations from other program to this one. Must be called after resolving names on other and before
// resolving names on me. me must not be used after other is destroyed
void program_copy_globals_from(Program* me, Program const* other);

// Should be called once after loading all files is done. Should always be called before typechecking
void program_resolve_names(Program* me);

// Typechecks the loaded file and all imports, does not typecheck libraries loaded with program_load_lib()
void program_typecheck(Program* me);

Scope const* program_get_global_scope(Program const* me);

// Returns the number of source files loaded or imported
intptr_t program_get_file_count(Program const* me);
