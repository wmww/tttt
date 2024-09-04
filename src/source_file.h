#pragma once

#include "block.h"

typedef struct SourceFile {
    cstr canonical_path;
    cstr contents;
    Block ast;
    NameStack* name_stack;
} SourceFile;

SourceFile source_file_make(cstr canonical_path, cstr contents, ErrorCtx* errs);
void source_file_drop(SourceFile* me);
