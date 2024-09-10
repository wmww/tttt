#pragma once

#include "block.h"
#include "scope.h"

typedef struct SourceFile {
    cstr absolute_path;
    cstr contents;
    Block ast;
    Scope scope;
    NameStack* name_stack;
} SourceFile;

SourceFile source_file_make(cstr absolute_path, cstr contents, ErrorCtx* errs);
void source_file_drop(SourceFile* me);

static inline const char* source_file_start(SourceFile const* me) {
    return cstr_str(&my contents);
}
