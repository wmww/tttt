#include "source_file.h"
#include "content_mapper.h"
#include "import_attributes.h"
#include "module_resolution.h"
#include "scope.h"
#include "error.h"
#include "string_helpers.h"
#include "compiler_options.h"
#include <cleat/filesystem.h>

ModuleKind source_file_implied_format_for_emit(SourceFile const* me) {
    CompilerOptions const* o = compiler_options();
    if (my is_json) return MODULE_KIND_NONE; // (No implied format)
    if (o->module >= MODULE_KIND_NODE16 && o->module <= MODULE_KIND_NODENEXT) {
        return my is_esm ? MODULE_KIND_ESNEXT : MODULE_KIND_COMMONJS;
    }
    csview path = cstr_sv(&my canonical_path);
    if (csview_ends_with(path, ".cts") || csview_ends_with(path, ".cjs")) return MODULE_KIND_COMMONJS;
    if (csview_ends_with(path, ".mts") || csview_ends_with(path, ".mjs")) return MODULE_KIND_ESNEXT;
    // (is_esm and package_has_type are only known with node16/nodenext resolution or in node_modules, where tsc looks
    // at package.json)
    if (my package_has_type) return my is_esm ? MODULE_KIND_ESNEXT : MODULE_KIND_COMMONJS;
    return MODULE_KIND_NONE;
}

ModuleKind source_file_emit_module_kind(SourceFile const* me) {
    ModuleKind implied = source_file_implied_format_for_emit(me);
    return implied != MODULE_KIND_NONE ? implied : compiler_options()->module;
}

// tsc's getVerbatimModuleSyntaxErrorMessage: ESM syntax in a CommonJS file under verbatimModuleSyntax (TS1286 in a
// .cts/.cjs, else TS1295 with the hint about package.json's `type`)
void source_file_verbatim_cjs_error(SourceFile const* me, ErrorCtx* errs, Span at, bool grammar) {
    csview path = me ? cstr_sv(&my canonical_path) : c_sv("");
    if (csview_ends_with(path, ".cts") || csview_ends_with(path, ".cjs")) {
        char const* message = "ECMAScript imports and exports cannot be written in a CommonJS file under 'verbatimModuleSyntax'.";
        if (grammar) error_ctx_grammar_error_d(errs, at, 1286, message);
        else error_ctx_error_d(errs, at, 1286, message);
        return;
    }
    char const* message = "ECMAScript imports and exports cannot be written in a CommonJS file under 'verbatimModuleSyntax'. "
        "Adjust the 'type' field in the nearest 'package.json' to make this file an ECMAScript module, or adjust your "
        "'verbatimModuleSyntax', 'module', and 'moduleResolution' settings in TypeScript.";
    if (grammar) error_ctx_grammar_error_d(errs, at, 1295, message);
    else error_ctx_error_d(errs, at, 1295, message);
}

// Whether some line break of the text isn't \n or \r\n: a lone \r, U+2028 or U+2029
static bool has_other_line_breaks(const char* start, const char* end) {
    for (const char* pos = start; (pos = memchr(pos, '\r', (size_t)(end - pos))); pos++) {
        if (pos + 1 == end || pos[1] != '\n') return true;
    }
    for (const char* pos = start; (pos = memchr(pos, 0xE2, (size_t)(end - pos))); pos++) {
        if (end - pos >= 3 && (unsigned char)pos[1] == 0x80 && ((unsigned char)pos[2] & 0xFE) == 0xA8) return true;
    }
    return false;
}

static cstr message_cwd;

void source_file_set_message_cwd(csview cwd) {
    cstr_assign_sv(&message_cwd, cwd);
}

bool messages_use_absolute_paths(void) {
    return cstr_size(&message_cwd) > 0;
}

cstr path_for_message(csview path) {
    if (!cstr_size(&message_cwd) || (path.size && path.buf[0] == '/')) return cstr_from_sv(path);
    return absolute_path_of(cstr_sv(&message_cwd), path);
}

SourceFile source_file_make(cstr canonical_path, cstr contents) {
    check(cstr_size(&canonical_path));
    const char* start = cstr_str(&contents);
    const char* end = start + cstr_size(&contents);
    u32 cap = 1024;
    u32* line_starts = malloc(sizeof(u32) * cap);
    u32 line_count = 0;
    line_starts[line_count++] = 0;
    // tsc's computeLineStarts: \r\n, \r, \n, U+2028 and U+2029 end a line. Most files have only \n and \r\n: memchr
    // (SIMD) finds those, and checking for the others with memchr is much cheaper than looking at every byte
    bool only_lf = !has_other_line_breaks(start, end);
    for (const char* pos = start; pos < end; ) {
        if (only_lf) {
            pos = memchr(pos, '\n', (size_t)(end - pos));
            if (!pos) break;
            pos++;
        } else {
            unsigned char c = (unsigned char)*pos;
            if (c == '\n') pos++;
            else if (c == '\r') pos += pos + 1 < end && pos[1] == '\n' ? 2 : 1;
            else if (c == 0xE2 && pos + 2 < end && (unsigned char)pos[1] == 0x80 && ((unsigned char)pos[2] & 0xFE) == 0xA8) pos += 3;
            else { pos++; continue; }
        }
        if (line_count == cap) {
            cap *= 2;
            line_starts = realloc(line_starts, sizeof(u32) * cap);
        }
        line_starts[line_count++] = (u32)(pos - start);
    }
    // (Most files are much shorter than the first guess: 9 MB of the nest corpus was unused room)
    if (line_count < cap) line_starts = realloc(line_starts, sizeof(u32) * line_count);
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

static_assert(offsetof(SourceFile, id) == 0, "source_file_id() reads the first word");

const ModuleRefRare module_ref_no_rare = {0};

ModuleRefRare* module_ref_rare_mut(ModuleRef* me) {
    if (!me->rare) me->rare = c_new(ModuleRefRare, {0});
    return me->rare;
}

void source_file_drop(SourceFile* me) {
    names_unregister_source(me);
    source_file_unregister(me);
    for (u32 i = 0; i < my include_reason_count; i++) cstr_drop(&my include_reasons[i].package_id);
    free(my include_reasons);
    cstr_drop(&my canonical_path);
    cstr_drop(&my contents);
    free(my line_starts);
    block_drop(&my ast);
    ast_arena_drop(&my arena);
    cstr_drop(&my decl_override);
    if (my declaration_output) cstr_drop(my declaration_output);
    free(my declaration_output);
    cstr_drop(&my declaration_output_path);
    if (my declaration_map) cstr_drop(my declaration_map);
    free(my declaration_map);
    c_drop(cstr, &my declaration_map_source, &my declaration_map_url);
    cstr_drop(&my bundled_path);
    cstr_drop(&my package_json);
    for (u32 i = 0; i < my module_ref_count; i++) {
        cstr_drop(&my module_refs[i]->path);
        ModuleRefRare* rare = my module_refs[i]->rare;
        if (rare) c_drop(cstr, &rare->resolved_path, &rare->package_name, &rare->alternate_result, &rare->unbuilt_output, &rare->augmented_path);
        free(rare);
        module_ref_attrs_free(my module_refs[i]->attrs);
        free(my module_refs[i]);
    }
    free(my module_refs);
    free(my comment_directives);
    free(my deprecated_jsdoc_ends);
    free(my comments);
    free(my expando_containers);
    free(my unused);
    free(my parse_error_starts);
    if (my content_mapped) content_mapped_free(my content_mapped);
}

// tsc's containsParseError: whether a parse error starts in the node's text or right after it (a missing `,` or `}` is
// reported at the next token, which its node ends before)
bool source_file_parse_error_within(Span node) {
    SourceFile const* file = span_source(node);
    if (!file || !file->has_parse_errors || !span_valid(node)) return false;
    for (u32 i = 0; i < file->parse_error_count; i++) {
        char const* at = file->parse_error_starts[i];
        if (at >= span_start_ptr(node) && at <= span_end_ptr(node)) return true;
    }
    return false;
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

cstr source_file_jsx_runtime_import(SourceFile const* me) {
    CompilerOptions const* options = compiler_options();
    csview runtime = my jsx_pragmas.runtime;
    if (csview_equals(runtime, "classic")) return cstr_null;
    bool automatic = options->jsx == JSX_REACT_JSX || options->jsx == JSX_REACT_JSXDEV ||
        cstr_size(&options->jsx_import_source) || my jsx_pragmas.import_source.size || csview_equals(runtime, "automatic");
    if (!automatic) return cstr_null;
    csview base = my jsx_pragmas.import_source.size ? my jsx_pragmas.import_source :
        cstr_size(&options->jsx_import_source) ? cstr_sv(&options->jsx_import_source) : c_sv("react");
    return cstr_d(base, options->jsx == JSX_REACT_JSXDEV ? "/jsx-dev-runtime" : "/jsx-runtime");
}

// tsc's mangleScopedPackageName: `@scope/name` is `scope__name` in DefinitelyTyped
static cstr mangle_scoped_package_name(csview name) {
    isize slash = name.size && name.buf[0] == '@' ? csview_find_sv(name, c_sv("/")) : c_NPOS;
    if (slash == c_NPOS) return cstr_from_sv(name);
    return cstr_d(csview_slice(name, 1, slash), "__", csview_slice(name, slash + 1, name.size));
}

// tsc's createModuleNotFoundChain: why no declarations were found, one line under the error. The declarations another
// resolution would find (alternateResult), else what the program has of the package (ModuleRef.package_hint)
static void module_ref_add_not_found_chain(ModuleRef const* me, Error* error, csview package_name) {
    cstr mangled = mangle_scoped_package_name(package_name);
    if (cstr_size(&module_ref_rare(me)->alternate_result)) {
        bool types = csview_find_sv(cstr_sv(&module_ref_rare(me)->alternate_result), c_sv("node_modules/@types/")) != c_NPOS;
        cstr library = types ? cstr_d("@types/", mangled) : cstr_from_sv(package_name);
        error_sub_error_d(error, span_null, 6277, "There are types at '", module_ref_rare(me)->alternate_result, "', but this "
            "result could not be resolved when respecting package.json \"exports\". The '", library,
            "' library may need to update its package.json or typings.");
        cstr_drop(&library);
        cstr_drop(&mangled);
        return;
    }
    switch (module_ref_rare(me)->package_hint) {
        case MODULE_REF_HINT_NONE: break;
        case MODULE_REF_HINT_TYPES_EXIST:
            error_sub_error_d(error, span_null, 7040, "If the '", package_name, "' package actually exposes this module, "
                "consider sending a pull request to amend 'https://github.com/DefinitelyTyped/DefinitelyTyped/tree/master/types/",
                mangled, "'");
            break;
        case MODULE_REF_HINT_BUNDLES_TYPES:
            error_sub_error_d(error, span_null, 7058, "If the '", package_name, "' package actually exposes this module, "
                "try adding a new declaration (.d.ts) file containing `declare module '", my path, "';`");
            break;
        case MODULE_REF_HINT_INSTALL_TYPES:
            error_sub_error_d(error, span_null, 7035, "Try `npm i --save-dev @types/", mangled, "` if it exists or add a "
                "new declaration (.d.ts) file containing `declare module '", my path, "';`");
            break;
    }
    cstr_drop(&mangled);
}

// tsc's resolveExternalModule: `@types/x` names a declaration package, whether or not it resolves
static void report_types_package(ModuleRef const* me, ErrorCtx* errs) {
    csview path = cstr_sv(&my path);
    if (!csview_starts_with(path, "@types/")) return;
    error_ctx_error_d(errs, my span, 6137, "Cannot import type declaration files. Consider importing '",
        csview_slice(path, 7, path.size), "' instead of '", path, "'.");
}

void module_ref_report_implicit_any_at(ModuleRef const* me, Span at, ErrorCtx* errs) {
    // The import is `any`, which is only worth a word when that has to be explicit
    if (!compiler_options()->no_implicit_any) return;
    Error* error = error_ctx_error_d(errs, at, 7016,
        "Could not find a declaration file for module '", my path, "'. '", module_ref_rare(me)->resolved_path,
        "' implicitly has an 'any' type.");
    // (tsc's errorOnImplicitAnyModule: for a bare name into a package)
    if (error && cstr_size(&module_ref_rare(me)->package_name)) module_ref_add_not_found_chain(me, error, cstr_sv(&module_ref_rare(me)->package_name));
}

static void report_unresolved(ModuleRef const* me, ErrorCtx* errs, bool side_effect);
void module_ref_report_unresolved(ModuleRef const* me, ErrorCtx* errs) { report_unresolved(me, errs, false); }
void module_ref_report_unresolved_side_effect(ModuleRef const* me, ErrorCtx* errs) { report_unresolved(me, errs, true); }

// tsc's nodeCoreModules: what Node.js has built in, with or without `node:` (some only with it)
static bool is_node_core_module(csview path) {
    static char const* const modules[] = {
        "assert", "assert/strict", "async_hooks", "buffer", "child_process", "cluster", "console", "constants", "crypto",
        "dgram", "diagnostics_channel", "dns", "dns/promises", "domain", "events", "fs", "fs/promises", "http", "http2",
        "https", "inspector", "inspector/promises", "module", "net", "os", "path", "path/posix", "path/win32",
        "perf_hooks", "process", "punycode", "querystring", "readline", "readline/promises", "repl", "stream",
        "stream/consumers", "stream/promises", "stream/web", "string_decoder", "sys", "timers", "timers/promises", "tls",
        "trace_events", "tty", "url", "util", "util/types", "v8", "vm", "wasi", "worker_threads", "zlib",
    };
    static char const* const prefixed_only[] = { "quic", "sea", "sqlite", "test", "test/reporters" };
    bool prefixed = csview_starts_with(path, "node:");
    if (prefixed) path = csview_subview(path, 5, path.size - 5);
    for (u32 i = 0; i < c_arraylen(modules); i++) {
        if (csview_equals(path, modules[i])) return true;
    }
    for (u32 i = 0; prefixed && i < c_arraylen(prefixed_only); i++) {
        if (csview_equals(path, prefixed_only[i])) return true;
    }
    return false;
}

static void report_unresolved(ModuleRef const* me, ErrorCtx* errs, bool side_effect) {
    CompilerOptions const* options = compiler_options();
    report_types_package(me, errs);
    switch (my failure) {
        case MODULE_REF_IS_JSON:
            return;
        case MODULE_REF_IS_JS:
            module_ref_report_implicit_any_at(me, my span, errs);
            return;
        case MODULE_REF_ARBITRARY_EXTENSION:
            error_ctx_error_d(errs, my span, 6263,
                "Module '", my path, "' was resolved to '", module_ref_rare(me)->resolved_path,
                "', but '--allowArbitraryExtensions' is not set.");
            return;
        case MODULE_REF_JSX_NOT_SET:
            error_ctx_error_d(errs, my span, 6142,
                "Module '", my path, "' was resolved to '", module_ref_rare(me)->resolved_path, "', but '--jsx' is not set.");
            return;
        case MODULE_REF_JSON_WITHOUT_OPTION:
            error_ctx_error_d(errs, my span, 2732,
                "Cannot find module '", my path,
                "'. Consider using '--resolveJsonModule' to import module with '.json' extension.");
            return;
        case MODULE_REF_NEEDS_EXTENSION:
            if (cstr_size(&module_ref_rare(me)->resolved_path)) {
                error_ctx_error_d(errs, my span, 2835,
                    "Relative import paths need explicit file extensions in ECMAScript imports when "
                    "'--moduleResolution' is 'node16' or 'nodenext'. Did you mean '", module_ref_rare(me)->resolved_path, "'?");
            } else {
                error_ctx_error_d(errs, my span, 2834,
                    "Relative import paths need explicit file extensions in ECMAScript imports when "
                    "'--moduleResolution' is 'node16' or 'nodenext'. Consider adding an extension to the import path.");
            }
            return;
        case MODULE_REF_NOT_BUILT:
            error_ctx_error_d(errs, my span, 6305, "Output file '", module_ref_rare(me)->unbuilt_output,
                "' has not been built from source file '", module_ref_rare(me)->resolved_path, "'.");
            return;
        case MODULE_REF_NOT_FOUND:
            break;
    }
    if (!side_effect && is_node_core_module(cstr_sv(&my path))) {
        // (tsc 6.0's getCannotResolveModuleNameErrorForSpecificModule: the "install @types/node" message)
        bool wildcard = false;
        for (isize i = 0; i < options->types.count; i++) wildcard |= cstr_equals(&options->types.items[i], "*");
        error_ctx_error_d(errs, my span, wildcard ? 2580 : 2591, "Cannot find name '", my path, "'. Do you need to install type "
            "definitions for node? Try `npm i --save-dev @types/node`",
            wildcard ? "." : " and then add 'node' to the types field in your tsconfig.");
    } else if (side_effect) {
        // (A side-effect import's message is the caller's: tsc's resolveExternalModuleName)
        Error* error = error_ctx_error_d(errs, my span, 2882,
            "Cannot find module or type declarations for side-effect import of '", my path, "'.");
        if (error && cstr_size(&module_ref_rare(me)->alternate_result)) module_ref_add_not_found_chain(me, error, cstr_sv(&my path));
    } else {
        Error* error = error_ctx_error_d(errs, my span, 2307,
            "Cannot find module '", my path, "' or its corresponding type declarations.");
        if (error && cstr_size(&module_ref_rare(me)->alternate_result)) module_ref_add_not_found_chain(me, error, cstr_sv(&my path));
    }
}

// tsc's tryExtractTSExtension of a declaration file name: ".d.ts", ".d.mts", ".d.cts", else empty
static csview declaration_extension(csview path) {
    static char const* const extensions[] = { ".d.ts", ".d.mts", ".d.cts" };
    for (u32 i = 0; i < c_arraylen(extensions); i++) {
        if (csview_ends_with(path, extensions[i])) return csview_from(extensions[i]);
    }
    return (csview){0};
}

// node16 and node18 can't `require()` an ES module (nodenext can): TS1471/TS1479 at `at` (the specifier, or the
// first JSX element of a file whose runtime module it is)
void module_ref_report_require_of_esm(ModuleRef const* me, Span at, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    // (An ambient module of the name is what the reference is, before any file: tsc's resolveExternalModule returns
    // it before looking at the resolved file's format. dexie-react-hooks' `declare module 'yjs'` next to an ES yjs)
    if (my module_state == ALIAS_DONE && my module && my module->is_module && my module->module && !my module->module->file) return;
    SourceFile const* from = span_source(my span);
    bool sync_import = my syntax == MODULE_REF_REQUIRE || (my syntax == MODULE_REF_IMPORT && from && !from->is_esm);
    // (A `resolution-mode` attribute of a type-only import or an import type lets it import across formats: tsc's
    // hasResolutionModeOverride)
    if ((options->module == MODULE_KIND_NODE16 || options->module == MODULE_KIND_NODE18) && sync_import &&
        my file && my file->is_esm && my file->is_module && my resolution_override == IMPORT_RESOLUTION_DEFAULT
    ) {
        if (my syntax == MODULE_REF_REQUIRE) {
            error_ctx_error_d(errs, at, 1471,
                "Module '", my path, "' cannot be imported using this construct. The specifier only resolves to an "
                "ES module, which cannot be imported with 'require'. Use an ECMAScript import instead.");
        } else if (my type_only_import || my import_type) {
            error_ctx_error_d(errs, at, my type_only_import ? 1541 : 1542, my type_only_import ? "Type-only import" : "Type import",
                " of an ECMAScript module from a CommonJS module must have a 'resolution-mode' attribute.");
        } else {
            Error* error = error_ctx_error_d(errs, at, 1479,
                "The current file is a CommonJS module whose imports will produce 'require' calls; however, the "
                "referenced file is an ECMAScript module and cannot be imported with 'require'. Consider writing a "
                "dynamic 'import(\"", my path, "\")' call instead.");
            // What would make this file an ES module, when its extension leaves that open
            csview name = cstr_sv(&from->canonical_path);
            // (tsc's tryGetExtensionFromPath(): a .d.ts is none of these)
            bool ts = csview_ends_with(name, ".ts") && !csview_ends_with(name, ".d.ts");
            bool js = csview_ends_with(name, ".js");
            if (error && (ts || js || csview_ends_with(name, ".tsx") || csview_ends_with(name, ".jsx"))) {
                const char* rename = ts ? ".mts" : js ? ".mjs" : nullptr;
                cstr hint = cstr_lit("To convert this file to an ECMAScript module, ");
                bool add_field = cstr_size(&from->package_json) && !from->package_has_type;
                // (Only the package.json message has a comma before its "or")
                if (rename) cstr_append_d(&hint, DEFAULT_DISPLAY, "change its file extension to '", rename, add_field ? "', or " : "' or ");
                if (add_field) {
                    cstr_append_d(&hint, DEFAULT_DISPLAY,
                        "add the field `\"type\": \"module\"` to '", from->package_json, "'.");
                } else {
                    cstr_append_lit(&hint, "create a local package.json file with `{ \"type\": \"module\" }`.");
                }
                error_sub_error_s(error, span_null, 1479, hint);
            }
        }
    }
}

// tsc's getRelativePathFromFile: `to` from the directory of `from` (both relative to the same directory), "./" first
// unless it goes up
static cstr relative_path_from_file(csview from, csview to) {
    isize dir_end = from.size;
    while (dir_end > 0 && from.buf[dir_end - 1] != '/') dir_end--;
    csview dir = csview_slice(from, 0, dir_end); // With its trailing '/', or empty
    // The longest common prefix that ends at a '/'
    isize common = 0;
    for (isize i = 0; i < dir.size && i < to.size && dir.buf[i] == to.buf[i]; i++) {
        if (dir.buf[i] == '/') common = i + 1;
    }
    cstr ret = cstr_init();
    isize ups = 0;
    for (isize i = common; i < dir.size; i++) ups += dir.buf[i] == '/';
    if (!ups) cstr_append(&ret, "./");
    for (isize i = 0; i < ups; i++) cstr_append(&ret, "../");
    cstr_append_sv(&ret, csview_slice(to, common, to.size));
    return ret;
}

static bool has_ts_extension(csview path) {
    return (csview_ends_with(path, ".ts") || csview_ends_with(path, ".tsx") || csview_ends_with(path, ".mts") ||
        csview_ends_with(path, ".cts")) && !declaration_extension(path).size;
}

// tsc's resolveExternalModule under rewriteRelativeImportExtensions: what emit's rewriting of `.ts` specifiers to
// `.js` would get wrong (not in ambient code, declaration file names, type-only imports)
static void report_unsafe_rewrite(ModuleRef const* me, ErrorCtx* errs) {
    SourceFile const* from = span_source(my span);
    if (!from || from->is_declaration || declaration_extension(cstr_sv(&my path)).size) return;
    csview path = cstr_sv(&my path);
    bool relative = csview_starts_with(path, "./") || csview_starts_with(path, "../") || csview_equals(path, ".") ||
        csview_equals(path, "..");
    bool should_rewrite = relative && has_ts_extension(path);
    if (!my used_ts_extension && should_rewrite) {
        cstr relative_path = relative_path_from_file(cstr_sv(&from->canonical_path), cstr_sv(&my file->canonical_path));
        error_ctx_error_d(errs, my span, 2876, "This relative import path is unsafe to rewrite because it looks like a file "
            "name, but actually resolves to \"", relative_path, "\".");
        cstr_drop(&relative_path);
    } else if (my used_ts_extension && !should_rewrite && !my file->is_declaration &&
        // (sourceFileMayBeEmitted: not a package's file)
        !csview_contains(cstr_sv(&my file->canonical_path), "/node_modules/") &&
        !csview_starts_with(cstr_sv(&my file->canonical_path), "node_modules/")
    ) {
        isize dot = path.size;
        while (dot > 0 && path.buf[dot - 1] != '.' && path.buf[dot - 1] != '/') dot--;
        error_ctx_error_d(errs, my span, 2877, "This import uses a '.", csview_slice(path, dot, path.size), "' extension to "
            "resolve to an input TypeScript file, but will not be rewritten during emit because it is not a relative path.");
    } else if (my used_ts_extension && should_rewrite && my rewrite_crosses_projects) {
        error_ctx_error_d(errs, my span, 2878, "This import path is unsafe to rewrite because it resolves to another project, "
            "and the relative path between the projects' output files is not the same as the relative path between its "
            "input files.");
    }
}

// side_effect: an import without a clause, `import "./a.ts"` (TS5097, but no TS2846: tsc's importOrExport is missing)
void module_ref_report_resolved(ModuleRef const* me, bool type_only, bool side_effect, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    report_types_package(me, errs);
    if (my failure == MODULE_REF_JSX_NOT_SET) {
        error_ctx_error_d(errs, my span, 6142,
            "Module '", my path, "' was resolved to '", module_ref_rare(me)->resolved_path, "', but '--jsx' is not set.");
    }
    // A declaration file named with its extension (tsc's resolveExternalModule: resolvedUsingTsExtension and
    // isDeclarationFileName), outside a type-only import; the suggestion is getSuggestedImportSource's
    csview dts = declaration_extension(cstr_sv(&my path));
    if (dts.size && !type_only && !side_effect && my file && my used_ts_extension) {
        csview stem = csview_slice(cstr_sv(&my path), 0, cstr_size(&my path) - dts.size);
        bool esm = (options->module >= MODULE_KIND_ES2015 && options->module <= MODULE_KIND_ESNEXT) ||
            (span_source(my span) && span_source(my span)->is_esm);
        char const* ext = "";
        if (esm) {
            bool prefer_ts = options->allow_importing_ts_extensions;
            // (.d.cts gives .js: tsc compares with Dmts twice)
            ext = csview_equals(dts, ".d.mts") ? (prefer_ts ? ".mts" : ".mjs") : prefer_ts ? ".ts" : ".js";
        }
        error_ctx_error_d(errs, my span, 2846, "A declaration file cannot be imported without 'import type'. Did you "
            "mean to import an implementation file '", stem, ext, "' instead?");
    }
    bool allowed = options->allow_importing_ts_extensions || options->rewrite_relative_import_extensions ||
        (span_source(my span) && span_source(my span)->is_declaration);
    module_ref_report_require_of_esm(me, my span, errs);
    // (Not for a type-only import or export, or an import type: tsc's resolveExternalModule)
    if (my used_ts_extension && !allowed && !type_only && my file && !dts.size) {
        isize dot = cstr_size(&my path);
        while (dot > 0 && cstr_str(&my path)[dot - 1] != '.') dot--;
        csview extension = csview_slice(cstr_sv(&my path), dot > 0 ? dot - 1 : 0, cstr_size(&my path));
        if (!csview_equals(extension, ".ts") && !csview_equals(extension, ".tsx") && !csview_equals(extension, ".mts") &&
            !csview_equals(extension, ".cts")
        ) {
            // (A `*` of a package.json target matched it inside the name, `#/foo.ts.omg`: the first one it contains,
            // tsgo's fallback over SupportedTSExtensionsFlat)
            static char const* const flat[] = { ".ts", ".tsx", ".d.ts", ".cts", ".d.cts", ".mts", ".d.mts" };
            for (u32 i = 0; i < c_arraylen(flat); i++) {
                if (csview_contains(cstr_sv(&my path), flat[i])) {
                    extension = csview_from(flat[i]);
                    break;
                }
            }
        }
        error_ctx_error_d(errs, my span, 5097, "An import path can only end with a '", extension,
            "' extension when 'allowImportingTsExtensions' is enabled.");
    }
    if (options->rewrite_relative_import_extensions && !type_only && my file && !dts.size &&
        !(my used_ts_extension && !allowed)
    ) {
        report_unsafe_rewrite(me, errs);
    }
}

// tsc's getErrorSpanForNode of a source file: its first token after trivia
Span source_file_first_token(SourceFile const* file) {
    char const* text = source_file_start(file);
    char const* end = text + cstr_size(&file->contents);
    char const* c = text;
    if (end - c >= 2 && c[0] == '#' && c[1] == '!') {
        while (c < end && *c != '\n') c++;
    }
    while (c < end) {
        if (isspace((unsigned char)*c)) c++;
        else if (c + 1 < end && c[0] == '/' && c[1] == '/') { while (c < end && *c != '\n') c++; }
        else if (c + 1 < end && c[0] == '/' && c[1] == '*') {
            c += 2;
            while (c + 1 < end && !(c[0] == '*' && c[1] == '/')) c++;
            c = c + 2 < end ? c + 2 : end;
        } else break;
    }
    char const* token_end = c;
    while (token_end < end && (isalnum((unsigned char)*token_end) || *token_end == '_' || *token_end == '$')) token_end++;
    if (token_end == c && c < end) token_end++;
    return span_make(file, c, token_end);
}

void source_file_add_deprecated_jsdoc(SourceFile* me, const char* end) {
    // (Almost always past every one seen so far: an append)
    u32 index = my deprecated_jsdoc_count;
    while (index && my deprecated_jsdoc_ends[index - 1] >= end) {
        if (my deprecated_jsdoc_ends[index - 1] == end) return;
        index--;
    }
    if (my deprecated_jsdoc_count == my deprecated_jsdoc_cap) {
        my deprecated_jsdoc_cap = my deprecated_jsdoc_cap ? my deprecated_jsdoc_cap * 2 : 8;
        my deprecated_jsdoc_ends = realloc(my deprecated_jsdoc_ends, sizeof(const char*) * my deprecated_jsdoc_cap);
        panic_if(!my deprecated_jsdoc_ends, "out of memory");
    }
    memmove(my deprecated_jsdoc_ends + index + 1, my deprecated_jsdoc_ends + index,
        sizeof(const char*) * (my deprecated_jsdoc_count - index));
    my deprecated_jsdoc_ends[index] = end;
    my deprecated_jsdoc_count++;
}

void source_file_add_comment(SourceFile* me, const char* start, const char* end) {
    char const* text = cstr_str(&my contents);
    u32 from = (u32)(start - text), to = (u32)(end - text);
    // (Almost always past every one seen so far: an append)
    u32 index = my comment_count;
    while (index && my comments[2 * index - 2] >= from) {
        if (my comments[2 * index - 2] == from) return;
        index--;
    }
    if (my comment_count == my comment_cap) {
        my comment_cap = my comment_cap ? my comment_cap * 2 : 64;
        my comments = realloc(my comments, sizeof(u32) * 2 * my comment_cap);
        panic_if(!my comments, "out of memory");
    }
    memmove(my comments + 2 * index + 2, my comments + 2 * index, sizeof(u32) * 2 * (my comment_count - index));
    my comments[2 * index] = from;
    my comments[2 * index + 1] = to;
    my comment_count++;
}

char const* source_file_comment_ending_at(SourceFile const* me, const char* end) {
    char const* text = cstr_str(&my contents);
    u32 at = (u32)(end - text);
    u32 low = 0, high = my comment_count;
    while (low < high) {
        u32 mid = low + (high - low) / 2;
        if (my comments[2 * mid + 1] < at) low = mid + 1;
        else high = mid;
    }
    return low < my comment_count && my comments[2 * low + 1] == at ? text + my comments[2 * low] : nullptr;
}

bool source_file_deprecated_jsdoc_ends_at(SourceFile const* me, const char* end) {
    u32 low = 0, high = my deprecated_jsdoc_count;
    while (low < high) {
        u32 mid = low + (high - low) / 2;
        if (my deprecated_jsdoc_ends[mid] < end) low = mid + 1;
        else high = mid;
    }
    return low < my deprecated_jsdoc_count && my deprecated_jsdoc_ends[low] == end;
}
