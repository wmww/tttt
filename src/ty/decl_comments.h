#pragma once

#include "../prelude.h"

typedef struct SourceFile SourceFile;

// Declaration emit's comments (plan 64 stage 3). tsc's printer copies the comments around each node it reprints from
// the source (the declaration transformer keeps the original nodes' positions), and in a declaration file only
// JSDoc-like (`/** */`) and pinned (`/*! */`) ones (OnlyPrintJSDocStyle). A node's leading comments are those after
// the first line break past its full start (tsc's getLeadingCommentRanges), its trailing ones those after its end on the
// same line. Full starts come from SourceFile.comments (every comment the parser hopped, recorded where declarations
// are emitted): back from the node's first token over whitespace and comments
//
// Output goes to a cstr whose last character says whether it is at the start of a line (or that is empty).
// `indent` is the writer's indentation level (4 spaces each), what a comment's first line gets at the start of a line
// and what its other lines are re-indented against (tsc's writeCommentRangeWorker)
typedef struct DeclComments {
    SourceFile const* file;
    bool off; // removeComments: only a pinned comment at the top of the file is kept
    // The file's detached comments (a copyright header, see decl_comments_detached()) end here: the first statement's
    // leading comments start after them
    char const* detached_end;
    bool map; // Declaration maps: each comment written is mapped (decl_map.h)
} DeclComments;

// The comments at the top of the file that a blank line separates from its first statement (tsc's emitDetachedComments)
void decl_comments_detached(DeclComments* me, cstr* out);
// The leading comments of the node whose first token starts at `token` (the node's position is its full start)
void decl_comments_leading(DeclComments const* me, cstr* out, u32 indent, char const* token);
// The trailing comments after `end`, a node's end
void decl_comments_trailing(DeclComments const* me, cstr* out, char const* end);
// The comments on the line before a list element (a parameter, a variable declaration after the first: tsc's
// emitListItems' intervening comments, which aren't filtered), written before it
void decl_comments_intervening(DeclComments const* me, cstr* out, u32 indent, char const* token);
// A list's comma-delimited element on a line of its own (an enum member): the comments right after the previous one's
// comma, which are filtered when `prefix_space` (forceNoNewline: a line comment ends the line)
void decl_comments_after_delimiter(DeclComments const* me, cstr* out, u32 indent, char const* token, bool prefix_space);
// The comments after the file's last statement (tsc's emitDetachedCommentsAfterStatementList), `end` its end
void decl_comments_file_end(DeclComments const* me, cstr* out, char const* end);
// The same in a file without statements: every comment past the detached ones (the statement list ends at its start)
void decl_comments_empty_file_end(DeclComments const* me, cstr* out);
// A node's end from the end of its span, which may take in the whitespace and comments after it: back over those. With
// `separators`, past the one of them that follows (a member's `;`, which is part of it)
char const* decl_comments_end(DeclComments const* me, char const* end, char const* separators);
// Whether the last comment between the previous token and `token` contains `needle` (tsc's isInternalDeclaration of a
// parameter: `@internal`)
bool decl_comments_last_contains(DeclComments const* me, char const* token, char const* needle);
// Whether a node from its full start (back from `token` over whitespace and comments) to `end` spans lines (tsc's
// declaration transformer makes a tuple written on one line SingleLine that way)
bool decl_comments_spans_lines(DeclComments const* me, char const* token, char const* end);
// Whether a leading comment of the node at `token` contains `needle` (tsc's isInternalDeclaration: `@internal`)
bool decl_comments_leading_contains(DeclComments const* me, char const* token, char const* needle);
// Whether a node starts at `token` in the file the comments are copied from
bool decl_comments_in_file(DeclComments const* me, char const* token);
