#include "source_file.h"

void source_directive_drop(SourceDirective* me) {
    cstr_drop(&my path);
}

SourceFile source_file_make(cstr canonical_path, cstr contents) {
    ASSERT(cstr_size(&canonical_path));
    return (SourceFile){
        .canonical_path = canonical_path,
        .contents = contents,
    };
}

void source_file_drop(SourceFile* me) {
    cstr_drop(&my canonical_path);
    cstr_drop(&my contents);
    vec_source_directive_drop(&my directives);
    block_drop(&my ast);
    scope_drop(&my module_scope);
    vec_scope_cptr_drop(&my global_scopes);
    cstr_drop(&my decl_override);
}

void source_file_set_module_true(SourceFile* me, ErrorCtx* errs) {
    if (!my is_module) {
        my is_module = true;
        my module_scope = scope_init_toplevel(errs);
    } else {
        ASSERT(my module_scope.errs == errs);
    }
}
