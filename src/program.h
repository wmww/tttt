#pragma once

#include "span.h"

typedef struct Program Program;
typedef struct ErrorCtx ErrorCtx;
typedef struct SourceFile SourceFile;
typedef struct Fs Fs;
typedef struct arc_js_object ArcObject;

Program* program_new(ErrorCtx* errs, Fs const* fs);
void program_destroy(Program* me);

// Parses and resolves names into the global scope, used for adding standard library type declarations. Preloaded files
// are not added to the import database
void program_preload(Program* me, const char* path);

// Parses and resolves names of the given file and all imports. Can be called multiple times, but can not be called
// indirectly by itself (this is not how imports are handled)
SourceFile const* program_load(Program* me, const char* path);

// Typechecks the loaded file and all imports, does not typecheck preloaded files
// TODO: Typechecking shouldn't be required at all if show_errors is false, so remove this arg
void program_typecheck(Program* me, bool show_errors);

// Returns the number of source files loaded or imported
intptr_t program_get_file_count(Program const* me);
