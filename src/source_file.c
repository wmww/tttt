#include "source_file.h"
#include "error.h"
#include "string_helpers.h"
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
    cstr_drop(&my package_json);
    for (u32 i = 0; i < my module_ref_count; i++) {
        cstr_drop(&my module_refs[i]->path);
        cstr_drop(&my module_refs[i]->resolved_path);
        free(my module_refs[i]);
    }
    free(my module_refs);
    free(my comment_directives);
}

void source_file_add_comment_directive(SourceFile* me, const char* start, const char* end, bool expect_error) {
    // Almost always past every directive seen so far: an append
    u32 index = my comment_directive_count;
    if (index && my comment_directives[index - 1].start >= start) {
        u32 low = 0;
        while (low < index) {
            u32 mid = low + (index - low) / 2;
            if (my comment_directives[mid].start < start) low = mid + 1;
            else index = mid;
        }
        if (my comment_directives[index].start == start) return;
    }
    if (my comment_directive_count == my comment_directive_cap) {
        my comment_directive_cap = my comment_directive_cap ? my comment_directive_cap * 2 : 8;
        my comment_directives = realloc(my comment_directives, sizeof(CommentDirective) * my comment_directive_cap);
    }
    memmove(
        my comment_directives + index + 1, my comment_directives + index,
        sizeof(CommentDirective) * (my comment_directive_count - index)
    );
    my comment_directives[index] = (CommentDirective){
        .start = start,
        .end = end,
        .line = source_file_line_index(me, end),
        .expect_error = expect_error,
    };
    my comment_directive_count++;
}

CommentDirective* source_file_comment_directive_at_line(SourceFile* me, u32 line) {
    // The first directive past the line, the one before it is the last one on the line or before
    u32 low = 0;
    u32 high = my comment_directive_count;
    while (low < high) {
        u32 mid = low + (high - low) / 2;
        if (my comment_directives[mid].line <= line) low = mid + 1;
        else high = mid;
    }
    if (low && my comment_directives[low - 1].line == line) return &my comment_directives[low - 1];
    return nullptr;
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
        case MODULE_REF_JSX_NOT_SET:
            error_ctx_error_d(errs, my span, 6142,
                "Module '", my path, "' was resolved to '", my resolved_path, "', but '--jsx' is not set.");
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

void module_ref_report_resolved(ModuleRef const* me, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    bool allowed = options->allow_importing_ts_extensions || options->rewrite_relative_import_extensions ||
        (my span.source && my span.source->is_declaration);
    // node16 and node18 can't `require()` an ES module (nodenext can)
    SourceFile const* from = my span.source;
    bool sync_import = my syntax == MODULE_REF_REQUIRE || (my syntax == MODULE_REF_IMPORT && from && !from->is_esm);
    if ((options->module == MODULE_KIND_NODE16 || options->module == MODULE_KIND_NODE18) && sync_import &&
        my file && my file->is_esm && my file->is_module
    ) {
        if (my syntax == MODULE_REF_REQUIRE) {
            error_ctx_error_d(errs, my span, 1471,
                "Module '", my path, "' cannot be imported using this construct. The specifier only resolves to an "
                "ES module, which cannot be imported with 'require'. Use an ECMAScript import instead.");
        } else {
            Error* error = error_ctx_error_d(errs, my span, 1479,
                "The current file is a CommonJS module whose imports will produce 'require' calls; however, the "
                "referenced file is an ECMAScript module and cannot be imported with 'require'. Consider writing a "
                "dynamic 'import(\"", my path, "\")' call instead.");
            // What would make this file an ES module, when its extension leaves that open
            csview name = cstr_sv(&from->canonical_path);
            bool ts = csview_ends_with(name, ".ts"), js = csview_ends_with(name, ".js");
            if (error && (ts || js || csview_ends_with(name, ".tsx") || csview_ends_with(name, ".jsx"))) {
                const char* rename = ts ? ".mts" : js ? ".mjs" : nullptr;
                cstr hint = cstr_lit("To convert this file to an ECMAScript module, ");
                if (rename) cstr_append_d(&hint, DEFAULT_DISPLAY, "change its file extension to '", rename, "', or ");
                if (cstr_size(&from->package_json) && !from->package_has_type) {
                    cstr_append_d(&hint, DEFAULT_DISPLAY,
                        "add the field `\"type\": \"module\"` to '", from->package_json, "'.");
                } else {
                    cstr_append_lit(&hint, "create a local package.json file with `{ \"type\": \"module\" }`.");
                }
                error_sub_error_s(error, span_null, 1479, hint);
            }
        }
    }
    if (my used_ts_extension && !allowed && my file && !my file->is_declaration) {
        isize dot = cstr_size(&my path);
        while (dot > 0 && cstr_str(&my path)[dot - 1] != '.') dot--;
        error_ctx_error_d(errs, my span, 5097,
            "An import path can only end with a '.", csview_slice(cstr_sv(&my path), dot, cstr_size(&my path)),
            "' extension when 'allowImportingTsExtensions' is enabled.");
    }
}
