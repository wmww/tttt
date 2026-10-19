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
// How many files owner emitted
u32 emit_store_count(EmitStore const* me, u32 owner);

// One program's file system: the inner one with the files of the owners visible[owner] says it sees on top (files and
// the directories they're in; a directory listing shows only the inner one's). Reads through a workspace link
// (node_modules) find them by the link's target
typedef struct EmitView EmitView;

EmitView* emit_view_new(EmitStore const* store, Fs const* inner, bool const* visible);
void emit_view_destroy(EmitView* me);
Fs const* emit_view_fs(EmitView const* me);
// realpath() that knows the view's files (program_set_realpath()'s callback, ctx the view)
cstr emit_view_real_path(void* ctx, csview path);
// The size of a file of the view or the disk's (program_set_file_size()'s callback, ctx the view)
i64 emit_view_file_size(void* ctx, csview path);
