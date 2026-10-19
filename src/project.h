#pragma once

#include "prelude.h"
#include "compiler_options.h"
#include "tsconfig.h"

#include <cleat/filesystem.h>

// Project references (tsc's `references`, `tsc -b`): the only code that knows about more than one tsconfig. A program
// checks one project. Under -p the projects it references (transitively) are read through their built outputs like
// tsc's command line (program_set_references()); under -b as sources, never as built `.d.ts` outputs
// (tsc's useSourceOfProjectReferenceRedirect, what its language service does): an import that resolves to a referenced
// project's output declaration file gets the project's source file instead, and the files of a referenced project
// are part of the program but neither checked nor reported (documentation.md, "Project references")

typedef struct Project {
    cstr config; // The tsconfig file, absolute
    bool readable; // False: the config couldn't be read (TS6053 where it's referenced)
    // The config's own options (no command line), resolved: what module resolution in the project's files and its
    // output paths go by
    CompilerOptions options;
    StrList root_files; // Absolute, once project_set_root_files() has listed them
    bool root_files_listed;
    TsconfigReference* references;
    u32 reference_count;
    bool has_references; // A `references` array, maybe empty: without files the config is a solution (tsgo's -b)
    u32* referenced; // The project of each of references
} Project;

typedef struct ProjectSet ProjectSet;

ProjectSet* project_set_new(Fs const* fs);
// --runExternalCode: the configs' content mappers count when listing root files (their extensions are roots)
void project_set_run_external_code(ProjectSet* me, bool on);
void project_set_destroy(ProjectSet* me);

// The project of the tsconfig at path (a file, or a directory with a tsconfig.json), loaded with everything it
// references (transitively) the first time
u32 project_set_load(ProjectSet* me, csview path);
Project const* project_set_at(ProjectSet const* me, u32 index);
// The project's root files (tsc's fileNames), listed on first use: walking `include` is a measurable part of a small
// project's time, so each project does it once
StrList const* project_set_root_files(ProjectSet* me, u32 index);

// tsc's build order of `-b` for the given projects: depth-first, references before what references them, in written
// order. A cycle is reported in *cycle_errors (one message each, tsc's TS6202) unless a reference on the way is
// `circular`. Returns how many projects *out (malloc'd) holds
u32 project_set_build_order(ProjectSet* me, u32 const* roots, u32 root_count, u32** out, StrList* cycle_errors);

// What one program sees of the projects its project references, transitively (not the project itself), and under -b
// of the projects built before it (built: indices into set, in build order), which it may reach through their outputs
typedef struct ProjectRedirects ProjectRedirects;

ProjectRedirects* project_redirects_new(ProjectSet* set, u32 project, u32 const* built, u32 built_count);
void project_redirects_destroy(ProjectRedirects* me);
// How many referenced projects (0: the program has no redirects)
u32 project_redirects_count(ProjectRedirects const* me);
CompilerOptions const* project_redirects_options(ProjectRedirects const* me, u32 index);
// Whether that project is one built before the program rather than a reference of it
bool project_redirects_built(ProjectRedirects const* me, u32 index);
// 1 + the index of the referenced project that has the file at absolute (a root file of it), 0 if none
u32 project_redirects_project_of(ProjectRedirects const* me, csview absolute);
// The source file a referenced project's output declaration file stands for (absolute in and out), empty if absolute
// isn't one. Whether or not the output exists
csview project_redirects_source_of_output(ProjectRedirects const* me, csview absolute);
// The other direction: the output declaration file of a referenced project's source (absolute in and out), empty if
// absolute isn't one (or has no declaration output: `.d.ts`, `.json`)
csview project_redirects_output_of_source(ProjectRedirects const* me, csview absolute);
// Whether a directory exists as far as module resolution is concerned, though it may not on disk: a referenced
// project's declaration directory (`declarationDir`, else `outDir`), something in it or a parent of it
bool project_redirects_output_directory(ProjectRedirects const* me, csview absolute);
