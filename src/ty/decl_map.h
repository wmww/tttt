#pragma once

#include "../prelude.h"

typedef struct SourceFile SourceFile;

// Declaration maps (declarationMap, plan 64 stage 5). tsc's printer maps the start (past trivia) and the end of every
// node it prints that has a position in the source, and of each comment it copies (emitSourceMapsBeforeNode/AfterNode,
// emitPos): the declaration transformer keeps the original nodes' positions, what it makes up (inferred types, added
// modifiers) has none. tttt's writer prints text, so a mapping is a mark in that text: DECL_MAP_MARK and the source
// offset, where the writer is when it reaches the node's start or end. The marks travel with the text through whatever
// the writer does to it (buffers spliced together, `export ` taken back), and decl_map_take() takes them out at the end,
// encoding them as tsc's generator does (sourcemap/generator.go)
#define DECL_MAP_MARK '\x01'
#define DECL_MAP_MARK_SIZE 9 // The mark byte and 8 bytes 0x10 + a nibble of the offset (control characters, no text)

// A mapping of where `out` ends to `source_offset` in the file being emitted
void decl_map_mark(cstr* out, u32 source_offset);
// The size of `text` without the marks at its end
isize_t decl_map_text_end(csview text);
// The last character of `text` before the marks at its end, 0 if none
char decl_map_last_char(csview text);
// Takes the marks out of `text` (the `.d.ts` with `\n` line breaks, before a BOM) and returns the VLQ mappings of
// them into `source`, columns in UTF-16 units
cstr decl_map_take(cstr* text, SourceFile const* source);
// The `.d.ts.map` file: `file` the `.d.ts`'s name, `source_root` as tsc writes it (empty: none), `source` the source's
// path as the map names it
cstr decl_map_json(csview file, csview source_root, csview source, csview mappings);
// Removes the marks from `text` (output that isn't mapped after all)
void decl_map_strip(cstr* text);

struct TypeNode;
// Past the whitespace and comments at `at` (tsc's skipTrivia)
char const* decl_map_skip_trivia(char const* at, char const* end);
// Back over the whitespace and block comments before `at` (not before `start`)
char const* decl_map_back_trivia(char const* at, char const* start);
// Where a parsed type node starts and ends in `file` (TypeNode.pos, the ends from its parts), null if it wasn't parsed
// there. A parenthesized node's are its parentheses', decl_map_type_inner() what is in them
char const* decl_map_type_start(struct TypeNode const* node, SourceFile const* file);
char const* decl_map_type_end(struct TypeNode const* node, SourceFile const* file);
// Inside the parentheses of a parenthesized node (`inner_start` past the `(`, `inner_end` before the `)`); the node's own
// start and end without them
char const* decl_map_type_inner_start(struct TypeNode const* node, SourceFile const* file);
char const* decl_map_type_inner_end(struct TypeNode const* node, SourceFile const* file);
