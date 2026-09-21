#pragma once

#include "prelude.h"
#include "compiler_options.h"

#include <cleat/filesystem.h>

// tsconfig.json: `extends` (relative, or a package in node_modules), `compilerOptions`, and the root files from
// `files` / `include` / `exclude`. Not: project references, watch options, `compileOnSave`

// path is the file, or a directory with a tsconfig.json in it. Options the config (or what it extends) sets are set on
// options, which still wants compiler_options_resolve(); path options become absolute and `config_dir` is set. The
// root files are appended to root_files as absolute paths, in tsc's order (`files`, then what `include` matches
// directory by directory, files before subdirectories, both sorted)
failable tsconfig_load(Fs const* fs, csview path, CompilerOptions* options, StrList* root_files);

// The nearest tsconfig.json in directory or above it, null if there is none
cstr tsconfig_find(Fs const* fs, csview directory);
