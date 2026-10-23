#pragma once

#include "prelude.h"
#include <cleat/filesystem.h>

// The declarations -b's programs emit, kept in memory for the programs built after them (plan 87): tsc -b's downstream
// programs read their references' freshly written `.d.ts` files at their output paths. Each project (by build position)
// adds its files once, before the programs that see it start (build()'s scheduling orders that), so an owner's files are
// read without a lock
typedef struct EmitStore EmitStore;

EmitStore* emit_store_new(u32 owner_count);
void emit_store_destroy(EmitStore* me);
// A file the project at build position owner emitted (absolute path; the text is copied). Only before any view that
// sees owner reads
void emit_store_add(EmitStore* me, u32 owner, csview path, csview text);
// Frees what owner emitted, once no view that sees it reads any more (the store's peak is the build's live frontier)
void emit_store_release(EmitStore* me, u32 owner);

// One program's file system: the inner one with the files of the owners visible[owner] says it sees on top (files and
// the directories they're in; a directory listing shows only the inner one's). Reads through a workspace link
// (node_modules) find them by the link's target
typedef struct EmitView EmitView;

// tsgo -b's file system caches (cachedvfs) last the whole build and never learn about a write: a declaration file one
// program probed before its project wrote it stays missing for every program after, references included. The store
// keeps the simulated times (build()) of each owner's finish and of each declaration path's first probe that missed:
// an owner's file is seen only where nobody probed it before it was written
void emit_store_set_finish(EmitStore* me, u32 owner, double finish);
// referenced[owner]: the program references owner's project (transitively), whose outputs at their paths aren't
// subject to the caches. start: the program's simulated start, what its missed probes are recorded at
EmitView* emit_view_new(EmitStore* store, Fs const* inner, bool const* visible, bool const* referenced, double start);
void emit_view_destroy(EmitView* me);
Fs const* emit_view_fs(EmitView const* me);
// realpath() that knows the view's files (program_set_realpath()'s callback, ctx the view)
cstr emit_view_real_path(void* ctx, csview path);
// The size of a file of the view or the disk's (program_set_file_size()'s callback, ctx the view)
i64 emit_view_file_size(void* ctx, csview path);
