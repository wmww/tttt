#pragma once

typedef struct SourceFile SourceFile;
typedef struct ErrorCtx ErrorCtx;

void parse_file(SourceFile* source, ErrorCtx* errs);
typedef struct FunctionImpl FunctionImpl;
// Plan 78: parses a deferred function body into its DeferredBody (the file's AST arena begun: ast_arena_begin()). False
// if it can be told from what the eager parse would have made (a syntax error in it, another end): nothing is reported
bool parse_deferred_body(SourceFile* source, FunctionImpl* func, ErrorCtx* errs);

// A big declaration file's parse in chunks on other threads (parse_file_chunked()): a thread whose parse_file()s may
// be helped sets helpers (null: none). offer: other threads may now call parse_chunk_job_join() and, if it said yes,
// parse_chunk_job_help(); retract: none may join from now on (parse_file() then waits for those in it)
typedef struct ParseChunkJob ParseChunkJob;
typedef struct ParseHelpers {
    void* ctx;
    void (*offer)(void* ctx, ParseChunkJob* job);
    void (*retract)(void* ctx, ParseChunkJob* job);
} ParseHelpers;
void parse_set_helpers(ParseHelpers const* helpers);
// Under whatever excludes retract(): true if the job has chunks left, and then the caller must parse_chunk_job_help()
bool parse_chunk_job_join(ParseChunkJob* job);
// Parses the job's chunks until none is left
void parse_chunk_job_help(ParseChunkJob* job);
