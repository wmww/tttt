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

// All all the declarations in the scope to the global namespace of the program. `scope` can come from another program,
// but this program must not be used after that program is destroyed.
void program_add_globals_from_scope(Program* me, Scope const* scope);
void program_add_globals_from_program(Program* me, Program const* other);

// Parses and resolves names of the given file and all imports. Can be called multiple times, but can not be called
// indirectly by itself. Returns nullptr if path is not found
SourceFile* program_load(Program* me, csview path);
SourceFile* program_preload(Program* me, csview path);
SourceFile* program_preload_es5(Program* me);

// Typechecks the loaded file and all imports, does not typecheck preloaded files
void program_typecheck(Program* me);

// Returns the number of source files loaded or imported
intptr_t program_get_file_count(Program const* me);
