#include "source_file.h"
#include "error.h"
#include "compiler_options.h"

SourceFile source_file_make(cstr canonical_path, cstr contents) {
    check(cstr_size(&canonical_path));
    const char* start = cstr_str(&contents);
    const char* end = start + cstr_size(&contents);
    u32 cap = 1024;
    u32* line_starts = malloc(sizeof(u32) * cap);
    u32 line_count = 0;
    line_starts[line_count++] = 0;
    for (const char* pos = start; (pos = memchr(pos, '\n', (size_t)(end - pos))); ) {
        pos++;
        if (line_count == cap) {
            cap *= 2;
            line_starts = realloc(line_starts, sizeof(u32) * cap);
        }
        line_starts[line_count++] = (u32)(pos - start);
    }
    return (SourceFile){
        .canonical_path = canonical_path,
        .contents = contents,
        .line_starts = line_starts,
        .line_count = line_count,
    };
}

unsigned source_file_line_index(SourceFile const* me, const char* pos) {
    u32 offset = (u32)(pos - source_file_start(me));
    // The last line that starts at or before offset
    u32 low = 0;
    u32 high = my line_count;
    while (high - low > 1) {
        u32 mid = low + (high - low) / 2;
        if (my line_starts[mid] <= offset) low = mid;
        else high = mid;
    }
    return low;
}

void source_file_drop(SourceFile* me) {
    cstr_drop(&my canonical_path);
    cstr_drop(&my contents);
    free(my line_starts);
    block_drop(&my ast);
    cstr_drop(&my decl_override);
    for (u32 i = 0; i < my module_ref_count; i++) {
        cstr_drop(&my module_refs[i]->path);
        cstr_drop(&my module_refs[i]->resolved_path);
        free(my module_refs[i]);
    }
    free(my module_refs);
}

ModuleRef* source_file_add_module_ref(SourceFile* me, cstr path, Span span) {
    if (my module_ref_count == my module_ref_cap) {
        my module_ref_cap = my module_ref_cap ? my module_ref_cap * 2 : 8;
        my module_refs = realloc(my module_refs, sizeof(ModuleRef*) * my module_ref_cap);
    }
    ModuleRef* ret = c_new(ModuleRef, { .path = path, .span = span });
    my module_refs[my module_ref_count++] = ret;
    return ret;
}


void module_ref_report_unresolved(ModuleRef const* me, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    switch (my failure) {
        case MODULE_REF_IS_JSON:
            return;
        case MODULE_REF_IS_JS:
            // The import is `any`, which is only worth a word when that has to be explicit
            if (options->no_implicit_any) {
                error_ctx_error_d(errs, my span, 7016,
                    "Could not find a declaration file for module '", my path, "'. '", my resolved_path,
                    "' implicitly has an 'any' type.");
            }
            return;
        case MODULE_REF_ARBITRARY_EXTENSION:
            error_ctx_error_d(errs, my span, 6263,
                "Module '", my path, "' was resolved to '", my resolved_path,
                "', but '--allowArbitraryExtensions' is not set.");
            return;
        case MODULE_REF_JSON_WITHOUT_OPTION:
            error_ctx_error_d(errs, my span, 2732,
                "Cannot find module '", my path,
                "'. Consider using '--resolveJsonModule' to import module with '.json' extension.");
            return;
        case MODULE_REF_NEEDS_EXTENSION:
            if (cstr_size(&my resolved_path)) {
                error_ctx_error_d(errs, my span, 2835,
                    "Relative import paths need explicit file extensions in ECMAScript imports when "
                    "'--moduleResolution' is 'node16' or 'nodenext'. Did you mean '", my resolved_path, "'?");
            } else {
                error_ctx_error_d(errs, my span, 2834,
                    "Relative import paths need explicit file extensions in ECMAScript imports when "
                    "'--moduleResolution' is 'node16' or 'nodenext'. Consider adding an extension to the import path.");
            }
            return;
        case MODULE_REF_NOT_FOUND:
            break;
    }
    if (options->module_resolution == MODULE_RESOLUTION_CLASSIC) {
        error_ctx_error_d(errs, my span, 2792,
            "Cannot find module '", my path, "'. Did you mean to set the 'moduleResolution' option to 'nodenext', "
            "or to add aliases to the 'paths' option?");
    } else {
        error_ctx_error_d(errs, my span, 2307,
            "Cannot find module '", my path, "' or its corresponding type declarations.");
    }
}
