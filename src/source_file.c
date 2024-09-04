#include "source_file.h"
#include "name_stack.h"

SourceFile source_file_make(cstr canonical_path, cstr contents, ErrorCtx* errs) {
    return (SourceFile){
        .canonical_path = canonical_path,
        .contents = contents,
        .name_stack = name_stack_new(errs),
    };
}

void source_file_drop(SourceFile* me) {
    cstr_drop(&my canonical_path);
    cstr_drop(&my contents);
    block_drop(&my ast);
    name_stack_destroy(my name_stack);
}
