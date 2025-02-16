#pragma once

#include "span.h"

typedef struct Program Program;
typedef struct ErrorCtx ErrorCtx;
typedef struct SourceFile SourceFile;
typedef struct Fs Fs;
typedef struct Scope Scope;
typedef struct arc_js_object ArcObject;

Program* program_new(ErrorCtx* errs, Fs const* fs);
void program_destroy(Program* me);

// Add all declarations from other program to this one. Must be called after resolving names on other and before
// resolving names on me. me must not outlive other.
void program_add_globals_from_program(Program* me, Program const* other);

// Parses the given file, loads all imports, adds them to the program and returns the SourceFile for the given path.
// Can be called multiple times. Returns nullptr if path is not found
SourceFile* program_load(Program* me, csview path);
SourceFile* program_preload(Program* me, csview path);
SourceFile* program_preload_es5(Program* me);

// Should be called once after loading all files is done. Should always be called before typechecking
void program_resolve_names(Program* me);

// Typechecks the loaded file and all imports, does not typecheck preloaded files
void program_typecheck(Program* me);

// Returns the number of source files loaded or imported
intptr_t program_get_file_count(Program const* me);
