#include "source_file.h"
#include "name_stack.h"

SourceFile source_file_make(cstr absolute_path, cstr contents, ErrorCtx* errs) {
    return (SourceFile){
        .absolute_path = absolute_path,
        .contents = contents,
        .scope = scope_init(),
        .name_stack = name_stack_new(errs),
    };
}

void source_file_drop(SourceFile* me) {
    cstr_drop(&my absolute_path);
    cstr_drop(&my contents);
    block_drop(&my ast);
    scope_drop(&my scope);
    name_stack_destroy(my name_stack);
}
