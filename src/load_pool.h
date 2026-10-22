#pragma once

#include "prelude.h"

typedef struct SourceFile SourceFile;
typedef struct ErrorCtx ErrorCtx;
typedef struct LoadPool LoadPool;

// A cache warmer for a program's sequential loader: worker threads read and parse the files the loader will probably
// ask for, ahead of it. The loader decides everything (which files, their order, their errors' order) exactly as it
// would without a pool; it takes a preparsed file when it gets to its path (waiting if it's in flight), and parses a
// file itself if nobody has started it. A wrong guess only costs a wasted parse. Paths are canonical (the program's)

// Runs on a worker: the file at path (null if it can't be read), its parse errors into errs. It queues what the loader
// will probably load from it with load_pool_queue()
typedef SourceFile* (*LoadPoolParse)(void* ctx, LoadPool* pool, csview path, bool is_stdlib, ErrorCtx* errs);
// Drops a parsed file nobody took
typedef void (*LoadPoolDrop)(void* ctx, SourceFile* file);

// size (null: none) guesses a queued file's size, called by the workers (workers take the biggest queued file first;
// without sizes in queue order)
LoadPool* load_pool_new(
    u32 threads, LoadPoolParse parse, LoadPoolDrop drop, void* ctx, i64 (*size)(void* ctx, csview path), void* size_ctx
);
// Stops the workers and drops what nobody took
void load_pool_destroy(LoadPool* me);

// Queues a path unless it's already known (queued, parsed or taken). Any thread
void load_pool_queue(LoadPool* me, csview path, bool is_stdlib);
// load_pool_queue() of each path, under one lock
void load_pool_queue_many(LoadPool* me, csview const* paths, isize count, bool is_stdlib);
// The same with each path's own is_stdlib
void load_pool_queue_each(LoadPool* me, csview const* paths, bool const* is_stdlib, isize count);

// The loader: the file for path if a worker parsed it (or is parsing it: waits, parsing queued files meanwhile) as
// is_stdlib, with its parse errors moved to errs; null if the loader has to parse it itself, and then no worker will.
// The loader runs LoadPoolParse too then, with the compiler options set
SourceFile* load_pool_take(LoadPool* me, csview path, bool is_stdlib, ErrorCtx* errs);
// For TTTT_LOAD_POOL_STATS: how long the loader spent parsing a file load_pool_take() left to it
void load_pool_note_inline(LoadPool* me, double ms);

// What the loader's own parses offer big declaration files' chunks to (parse_set_helpers()), null with one thread
typedef struct ParseHelpers ParseHelpers;
ParseHelpers const* load_pool_helpers(LoadPool* me);

// The stack size for worker threads: the main thread's (at least 8 MB, at most 256): parsing and checking recurse per
// nesting level
size_t worker_stack_size(void);
