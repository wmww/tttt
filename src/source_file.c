#include "source_file.h"

void source_directive_drop(SourceDirective* me) {
    cstr_drop(&my path);
}

SourceFile source_file_make(cstr canonical_path, cstr contents, ErrorCtx* errs) {
    ASSERT(cstr_size(&canonical_path));
    return (SourceFile){
        .canonical_path = canonical_path,
        .contents = contents,
        .scope = scope_init_toplevel(errs),
        .typecheck = true,
    };
}

SourceFile source_file_make_not_found_placeholder(ErrorCtx* errs) {
    return (SourceFile){
        .scope = scope_init_placeholder(errs),
    };
}

void source_file_drop(SourceFile* me) {
    cstr_drop(&my canonical_path);
    cstr_drop(&my contents);
    vec_source_directive_drop(&my directives);
    block_drop(&my ast);
    scope_drop(&my scope);
    cstr_drop(&my decl_override);
}
