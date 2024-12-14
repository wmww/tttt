#include "source_file.h"

void source_directive_drop(SourceDirective* me) {
    cstr_drop(&my path);
}

SourceFile source_file_make(cstr canonical_path, cstr contents, ErrorCtx* errs) {
    return (SourceFile){
        .canonical_path = canonical_path,
        .contents = contents,
        .scope = cstr_size(&canonical_path) ? scope_init_toplevel(errs) : scope_init_placeholder(errs),
        .typecheck = true,
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
