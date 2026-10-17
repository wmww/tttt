#pragma once

#include "prelude.h"

// The AST's allocator (plan 63 stage 2): what the parser allocates for a file (node boxes, the AST's lists, rare
// structs) is bumped out of that file's AstArena, and given back all at once when the file is dropped.
//
// While parse_file() runs, its thread's allocations through ast_malloc() and friends go to the file's arena
// (ast_arena_begin()/ast_arena_end()); anywhere else they are plain malloc. Arena memory lies in address segments
// reserved for it (ast_owns(): one byte read), so ast_free() and ast_realloc() can be handed either kind at any time:
// arena memory is never passed to free(), and growing it after parsing copies it out to malloc. The AST's drop
// functions keep freeing what they own: the malloc'd parts (cstrs, what later phases attached) go back to malloc, the
// arena parts are no-ops. During parsing a freed block is kept for reuse by the next allocation of its size (a failed
// speculation's nodes, a list's outgrown buffer), so sizes given to ast_free() must be the allocated ones.
//
// Every AST list type is declared with `#define g_allocator ast`; boxes are made with ast_new() and freed with
// ast_delete(). A box from c_new()/malloc() passed to ast_delete() is fine, an arena box passed to free() aborts.
typedef struct AstRange AstRange;
typedef struct AstArena {
    AstRange* ranges; // The address ranges the file's allocations lie in, usually one
    u32 range_count;
    u32 range_cap;
    size_t total; // Bytes bumped
    bool materialization; // ast_materialize_begin()'s: ranges start on a page of their own and end on one
    bool lasting; // A lasting materialization's
} AstArena;

// The current thread allocates into `arena` until ast_arena_end(); not nested
void ast_arena_begin(AstArena* arena);
void ast_arena_end(void);
// Gives the arena's pages back to the system. Its address space isn't reused: nothing may point into it any more
void ast_arena_drop(AstArena* arena);

// Plan 78 stage 2: the memory of a materialization of a file's deferred function bodies (program.c) is an arena of its
// own, dropped when no checker holds it (no walk: ast_arena_drop() alone). Between ast_materialize_begin() and
// ast_materialize_end() the thread's AST allocations go to it like a parsed file's (the bodies' parse, then what their
// resolve walk attaches to them) and so do the binder's (an Arena.ast_backed: ast_materialize_alloc()). Whatever the
// walk attaches to a node outside the bodies is undone before the drop (program.c's deferred_bodies_undo()), and a
// list outside them it grows is copied out to malloc. A thread takes materializations out of slabs of their own, each
// starting on a page of its own; addresses are never handed out twice, so nothing keyed by a dropped node's address can
// meet another node. Their 2 MB chunks may be huge pages, so a chunk is given back whole, once every materialization in
// it is dropped (most go together, at a checker's recycle). A `lasting` one (made for another file than the checker's,
// likely kept across recycles: program.c's checker_recycled()) would keep its chunk alive: those come from a slab of
// small pages, given back page by page
void ast_materialize_begin(AstArena* arena, bool lasting);
void ast_materialize_end(void);
// Zeroed, 8 (or 16) byte aligned, in the thread's current materialization (an Arena.ast_backed's allocation or chunk)
void* ast_materialize_alloc(size_t size, bool align16);

// Arenas take their addresses out of segments of 1 << AST_SEGMENT_SHIFT bytes (256 MB)
#define AST_SEGMENT_SHIFT 28
extern uint8_t ast_segment_map[(size_t)1 << (47 - AST_SEGMENT_SHIFT)];

// Whether p was handed out by some AstArena
static inline bool ast_owns(void const* p) {
    uintptr_t index = (uintptr_t)p >> AST_SEGMENT_SHIFT;
    return index < ((uintptr_t)1 << (47 - AST_SEGMENT_SHIFT)) && ast_segment_map[index];
}

void* ast_malloc(isize size);
void* ast_calloc(isize count, isize size);
void* ast_realloc(void* p, isize old_size, isize size);
void ast_free(void* p, isize size);

#define ast_new(T, ...) ((T*)c_safe_memcpy(ast_malloc(c_sizeof(T)), ((T[]){__VA_ARGS__}), c_sizeof(T)))
#define ast_delete(p) ast_free((p), c_sizeof(*(p)))

// TTTT_AST_STATS: the arenas' churn on stderr
void ast_stats_print(void);
