#include "source_file.h"

SourceFile source_file_make(cstr absolute_path, cstr contents, ErrorCtx* errs) {
    return (SourceFile){
        .absolute_path = absolute_path,
        .contents = contents,
        .scope = scope_init(errs),
        .typecheck = true,
    };
}

void source_file_drop(SourceFile* me) {
    cstr_drop(&my absolute_path);
    cstr_drop(&my contents);
    block_drop(&my ast);
    scope_drop(&my scope);
    cstr_drop(&my decl_override);
}
