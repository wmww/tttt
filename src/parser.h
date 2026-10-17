#pragma once

typedef struct SourceFile SourceFile;
typedef struct ErrorCtx ErrorCtx;

void parse_file(SourceFile* source, ErrorCtx* errs);
typedef struct FunctionImpl FunctionImpl;
// Plan 78: parses a deferred function body into its DeferredBody (the file's AST arena begun: ast_arena_begin()). False
// if it can be told from what the eager parse would have made (a syntax error in it, another end): nothing is reported
bool parse_deferred_body(SourceFile* source, FunctionImpl* func, ErrorCtx* errs);
