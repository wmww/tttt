#pragma once

typedef struct SourceFile SourceFile;
typedef struct ErrorCtx ErrorCtx;

void parse_file(SourceFile* source, ErrorCtx* errs);
