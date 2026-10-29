// mmap(), madvise(), memfd_create(), which strict -std=c2x hides
#define _GNU_SOURCE
#include "source_file.h"
#include "content_mapper.h"
#include "import_attributes.h"
#include "module_resolution.h"
#include "scope.h"
#include "error.h"
#include "string_helpers.h"
#include "compiler_options.h"
#include <cleat/filesystem.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

ModuleKind source_file_implied_format_for_emit(SourceFile const* me) {
    CompilerOptions const* o = compiler_options();
    if (my is_json) return MODULE_KIND_NONE; // (No implied format)
    if (o->module >= MODULE_KIND_NODE16 && o->module <= MODULE_KIND_NODENEXT) {
        return my is_esm ? MODULE_KIND_ESNEXT : MODULE_KIND_COMMONJS;
    }
    csview path = cstr_sv(&my canonical_path);
    if (csview_ends_with(path, ".cts") || csview_ends_with(path, ".cjs")) return MODULE_KIND_COMMONJS;
    if (csview_ends_with(path, ".mts") || csview_ends_with(path, ".mjs")) return MODULE_KIND_ESNEXT;
    // (is_esm and package_type_decides are only known with node16/nodenext resolution or in node_modules, where tsc looks
    // at package.json)
    if (my package_type_decides) return my is_esm ? MODULE_KIND_ESNEXT : MODULE_KIND_COMMONJS;
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
    return (SourceFile){
        .canonical_path = canonical_path,
        .contents = contents,
    };
}

// The count, then the line starts
static u32* compute_line_starts(const char* start, const char* end) {
    u32 cap = 1024;
    u32* lines = malloc(sizeof(u32) * cap);
    u32 count = 1;
    lines[count++] = 0;
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
        if (count == cap) {
            cap *= 2;
            lines = realloc(lines, sizeof(u32) * cap);
        }
        lines[count++] = (u32)(pos - start);
    }
    if (count < cap) lines = realloc(lines, sizeof(u32) * count);
    lines[0] = count - 1;
    return lines;
}

// (Made by whichever thread asks first; two at once both make them and one keeps its own)
LineStarts source_file_lines(SourceFile const* me) {
    SourceFile* file = (SourceFile*)me;
    u32* lines = atomic_load_explicit(&file->lines, memory_order_acquire);
    if (!lines) {
        const char* start = cstr_str(&my contents);
        u32* made = compute_line_starts(start, start + cstr_size(&my contents));
        if (atomic_compare_exchange_strong_explicit(&file->lines, &lines, made, memory_order_acq_rel, memory_order_acquire)) {
            lines = made;
        } else {
            free(made);
        }
    }
    return (LineStarts){ lines + 1, lines[0] };
}

unsigned source_file_line_index(SourceFile const* me, const char* pos) {
    u32 offset = (u32)(pos - source_file_start(me));
    LineStarts lines = source_file_lines(me);
    // The last line that starts at or before offset
    u32 low = 0;
    u32 high = lines.count;
    while (high - low > 1) {
        u32 mid = low + (high - low) / 2;
        if (lines.starts[mid] <= offset) low = mid;
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

static _Atomic i64 defer_stats[DEFER_STAT_COUNT];

static void defer_stats_print(void) {
    static char const* const names[DEFER_STAT_COUNT] = {
        "skipped", "undeferred", "skipped bytes", "files materialized", "bodies materialized", "cross-file materializations",
        "parse failed", "signature collided", "restarts", "rematerialized", "bodies singly", "singly completed", "dropped", "pinned", "ahead waits", "materialize us", "rematerialize us", "drop us",
    };
    for (u32 i = 0; i < DEFER_STAT_COUNT; i++) fprintf(stderr, "defer: %s %lld\n", names[i], (long long)defer_stats[i]);
}

void defer_stat_add(DeferStat stat, i64 n) {
    static _Atomic bool registered;
    if (!debug_env.defer_stats) return;
    if (!atomic_exchange(&registered, true)) atexit(defer_stats_print);
    defer_stats[stat] += n;
}

void source_file_drop(SourceFile* me) {
    names_unregister_source(me);
    source_file_unregister(me);
    free(my include_reasons);
    for (u32 i = 0; i < my include_package_id_count; i++) cstr_drop(&my include_package_ids[i]);
    free(my include_package_ids);
    cstr_drop(&my package_id);
    free(my load_children);
    cstr_drop(&my canonical_path);
    cstr_drop(&my absolute_path);
    if (my text_mapping) source_text_unmap(my text_mapping);
    else cstr_drop(&my contents);
    free(atomic_load(&my lines));
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
    statement_names_drop(my statement_names);
    free(my deprecated_jsdoc_ends);
    free(my comments);
    free(my expando_containers);
    free(my unused);
    free(my error_nodes);
    free(my commonjs_candidates);
    if (my content_mapped) content_mapped_free(my content_mapped);
    if (my deferred_bodies) {
        // (After block_drop(): the bodies' AST is in it)
        ast_arena_drop(&my deferred_bodies->memory);
        free(my deferred_bodies->snapshots);
        free(my deferred_bodies->undo);
        ResolveShard* unfinished = my deferred_bodies->unfinished;
        if (unfinished) {
            resolve_shard_discard(unfinished);
            free(unfinished);
        }
        error_ctx_drop(&my deferred_bodies->errs);
        pthread_mutex_destroy(&my deferred_bodies->lock);
        free(my deferred_bodies);
    }
}

// tsc's containsParseError: whether a parse error starts in the node's text or right after it (a missing `,` or `}` is
// reported at the next token, which its node ends before)
bool source_file_error_node_within(SourceFile const* file, char const* start, char const* end) {
    if (!file || !start) return false;
    for (u32 i = 0; i < file->error_node_count; i++) {
        if (file->error_nodes[2 * i] >= start && file->error_nodes[2 * i + 1] <= end) return true;
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

void source_file_push_module_ref(SourceFile* me, ModuleRef* ref) {
    if (my module_ref_count == my module_ref_cap) {
        my module_ref_cap = my module_ref_cap ? my module_ref_cap * 2 : 8;
        my module_refs = realloc(my module_refs, sizeof(ModuleRef*) * my module_ref_cap);
    }
    my module_refs[my module_ref_count++] = ref;
}

ModuleRef* source_file_add_module_ref(SourceFile* me, cstr path, Span span) {
    ModuleRef* ret = c_new(ModuleRef, { .path = path, .span = span });
    source_file_push_module_ref(me, ret);
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

void module_ref_report_not_built_at(ModuleRef const* me, Span at, ErrorCtx* errs) {
    error_ctx_error_d(errs, at, 6305, "Output file '", module_ref_rare(me)->unbuilt_output,
        "' has not been built from source file '", module_ref_rare(me)->resolved_path, "'.");
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
            module_ref_report_not_built_at(me, my span, errs);
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

// tsc's createModeMismatchDetails: what would make the importing file an ES module, when its extension leaves that open
static void add_mode_mismatch_details(Error* error, SourceFile const* from) {
    csview name = cstr_sv(&from->canonical_path);
    // (tsc's tryGetExtensionFromPath(): a .d.ts is none of these)
    bool ts = csview_ends_with(name, ".ts") && !csview_ends_with(name, ".d.ts");
    bool js = csview_ends_with(name, ".js");
    if (!error || !(ts || js || csview_ends_with(name, ".tsx") || csview_ends_with(name, ".jsx"))) return;
    const char* rename = ts ? ".mts" : js ? ".mjs" : nullptr;
    cstr hint = cstr_lit("To convert this file to an ECMAScript module, ");
    bool add_field = cstr_size(&from->package_json) && !from->package_has_type;
    // (Only the package.json message has a comma before its "or")
    if (rename) cstr_append_d(&hint, DEFAULT_DISPLAY, "change its file extension to '", rename, add_field ? "', or " : "' or ");
    if (add_field) {
        cstr_append_d(&hint, DEFAULT_DISPLAY, "add the field `\"type\": \"module\"` to '", from->package_json, "'.");
    } else {
        cstr_append_lit(&hint, "create a local package.json file with `{ \"type\": \"module\" }`.");
    }
    error_sub_error_s(error, span_null, 1479, hint);
}

// node16 and node18 can't `require()` an ES module (nodenext can): TS1471/TS1479 at `at` (the specifier, or the
// first JSX element of a file whose runtime module it is)
void module_ref_report_require_of_esm(ModuleRef const* me, Span at, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    // (An ambient module of the name is what the reference is, before any file: tsc's resolveExternalModule returns
    // it before looking at the resolved file's format. dexie-react-hooks' `declare module 'yjs'` next to an ES yjs)
    if (my module_state == ALIAS_DONE && my module && my module->is_module && my module->module && !my module->module->file) return;
    SourceFile const* from = span_source(my span);
    // (A referenced project's output goes by its project's options, tsgo's GetDefaultResolutionModeForFile with
    // getCompilerOptionsForFile: without node formats there it has none, unless its extension says CommonJS)
    bool from_cjs = from && !from->is_esm;
    if (from_cjs && from->resolution_project && !from->has_format) {
        csview path = cstr_sv(&from->canonical_path);
        from_cjs = csview_ends_with(path, ".cts") || csview_ends_with(path, ".cjs");
    }
    bool sync_import = my syntax == MODULE_REF_REQUIRE || (my syntax == MODULE_REF_IMPORT && from_cjs);
    // (A `resolution-mode` attribute of a type-only import or an import type lets it import across formats: tsc's
    // hasResolutionModeOverride)
    if ((options->module == MODULE_KIND_NODE16 || options->module == MODULE_KIND_NODE18) && sync_import &&
        my file && my file->is_esm && my file->is_module && my resolution_override == IMPORT_RESOLUTION_DEFAULT
    ) {
        if (my syntax == MODULE_REF_REQUIRE) {
            error_ctx_error_d(errs, at, 1471,
                "Module '", my path, "' cannot be imported using this construct. The specifier only resolves to an "
                "ES module, which cannot be imported with 'require'. Use an ECMAScript import instead.");
        } else {
            Error* error = my type_only_import || my import_type ?
                error_ctx_error_d(errs, at, my type_only_import ? 1541 : 1542, my type_only_import ? "Type-only import" :
                    "Type import", " of an ECMAScript module from a CommonJS module must have a 'resolution-mode' attribute.") :
                error_ctx_error_d(errs, at, 1479,
                    "The current file is a CommonJS module whose imports will produce 'require' calls; however, the "
                    "referenced file is an ECMAScript module and cannot be imported with 'require'. Consider writing a "
                    "dynamic 'import(\"", my path, "\")' call instead.");
            add_mode_mismatch_details(error, from);
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
        // (sourceFileMayBeEmitted: not a file found searching node_modules, a workspace package's through its link
        // too, whose real path has no node_modules in it: tsc's IsSourceFileFromExternalLibrary)
        !my file->found_searching_node_modules
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

// A resolved name in an import type, `import("./a.tsx")`: what tsc's resolveExternalModule says of it there
void module_ref_report_resolved_import_type(ModuleRef const* me, ErrorCtx* errs) {
    if (my failure == MODULE_REF_JSX_NOT_SET) {
        error_ctx_error_d(errs, my span, 6142,
            "Module '", my path, "' was resolved to '", module_ref_rare(me)->resolved_path, "', but '--jsx' is not set.");
    }
}

// not_emittable: an import without a clause, `import "./a.ts"`, or a JavaScript `require("./a.ts")`: neither TS5097 nor
// TS2846 (tsgo's resolveExternalModule reports both only inside an IsEmittableImport, which an import declaration is
// only with a clause, and a call only when it's `import()`)
void module_ref_report_resolved(ModuleRef const* me, bool type_only, bool not_emittable, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    report_types_package(me, errs);
    if (my failure == MODULE_REF_JSX_NOT_SET) {
        error_ctx_error_d(errs, my span, 6142,
            "Module '", my path, "' was resolved to '", module_ref_rare(me)->resolved_path, "', but '--jsx' is not set.");
    }
    // A declaration file named with its extension (tsc's resolveExternalModule: resolvedUsingTsExtension and
    // isDeclarationFileName), outside a type-only import; the suggestion is getSuggestedImportSource's
    csview dts = declaration_extension(cstr_sv(&my path));
    if (dts.size && !type_only && !not_emittable && my file && my used_ts_extension) {
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
    if (my used_ts_extension && !allowed && !type_only && !not_emittable && my file && !dts.size) {
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

// Mapped source texts lie end to end in reserved segments, so that dropping all their pages is a madvise() per segment
// rather than per file (each one a TLB shootdown on every thread). A slot is never reused: unmapping a text maps the
// reservation back over it
#define TEXT_SEGMENT ((size_t)256 << 20)
// Each file is a mapping of its own (a VMA): leave most of vm.max_map_count (65530 by default) to everything else
#define TEXT_MAX_MAPPED 16384
typedef struct TextSegment {
    char* base;
    size_t used, size;
} TextSegment;
typedef struct TextMapping {
    dev_t dev;
    ino_t ino;
    char* slot; // Null once unmapped
    size_t size; // The file's
} TextMapping;
// (A fixed array: text_sigbus() reads it without the lock)
#define TEXT_MAX_SEGMENTS 256
static struct {
    pthread_mutex_t lock;
    TextSegment segments[TEXT_MAX_SEGMENTS];
    _Atomic u32 count;
    TextMapping* mappings; // By SourceFile.text_mapping - 1
    u32 mapping_count, mapping_cap;
    u32 live;
} texts = { .lock = PTHREAD_MUTEX_INITIALIZER };

// Small files are read() into packed read-write chunks instead, each thread its own: a mapping each would be a syscall
// taking the mm lock for writing (while other threads fault pages) and a page of memory for ~1.4 KB of text (nest: 2230
// of its 2309 files are under 16 KB, 44% of the bytes; vscode: 8.7k of 12.8k mmaps). Their pages stay (a copy can't be
// dropped and read back, source_texts_release()), which the packing pays for. TTTT_TEXT_READ_BELOW=N: the size limit
// (default 16 KB, 0: every file mapped)
#define TEXT_READ_CHUNK ((size_t)256 << 10)
static size_t text_read_below(void) {
    static _Atomic i64 below = -1;
    i64 ret = atomic_load_explicit(&below, memory_order_relaxed);
    if (ret < 0) {
        char const* env = getenv("TTTT_TEXT_READ_BELOW");
        ret = env ? (i64)strtoull(env, nullptr, 0) : 16 * 1024;
        atomic_store_explicit(&below, ret, memory_order_relaxed);
    }
    return (size_t)ret;
}

static size_t text_page_round(size_t n) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    return (n + page - 1) / page * page;
}

// A slot of size bytes (page-rounded) in the segments, null past TEXT_MAX_MAPPED. Under the lock
static char* text_slot_locked(size_t size) {
    if (texts.live >= TEXT_MAX_MAPPED) return nullptr;
    TextSegment* last = texts.count ? &texts.segments[texts.count - 1] : nullptr;
    if (!last || last->size - last->used < size) {
        size_t reserve = size > TEXT_SEGMENT ? size : TEXT_SEGMENT;
        if (texts.count == TEXT_MAX_SEGMENTS) return nullptr;
        void* base = mmap(nullptr, reserve, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (base == MAP_FAILED) return nullptr;
        texts.segments[texts.count] = (TextSegment){ .base = base, .size = reserve };
        texts.count++;
        last = &texts.segments[texts.count - 1];
    }
    char* slot = last->base + last->used;
    last->used += size;
    return slot;
}

// A file truncated by another process while it's mapped faults with SIGBUS past its new end: the faulting page becomes
// zeros (the text reads as ending there) and the run goes on, with a warning, rather than crashing. tsc read the file
// whole before checking it; tttt's output for that file is then unreliable, as a file changed mid-run is anyway
static void text_sigbus(int sig, siginfo_t* info, void* context) {
    (void)context;
    char* at = info->si_addr;
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    u32 count = texts.count;
    for (u32 i = 0; i < count; i++) {
        TextSegment const* segment = &texts.segments[i];
        if (at < segment->base || at >= segment->base + segment->size) continue;
        char* start = (char*)((uintptr_t)at / page * page);
        if (mmap(start, page, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) break;
        static _Atomic bool warned;
        if (!atomic_exchange(&warned, true)) {
            static char const message[] = "tttt: warning: a source file was truncated while it was being checked; "
                                          "diagnostics for it may be wrong\n";
            ssize_t written = write(STDERR_FILENO, message, sizeof message - 1);
            (void)written;
        }
        return;
    }
    // Not a mapped text's: the default action when the access is retried
    signal(sig, SIG_DFL);
}

static void text_sigbus_install(void) {
    struct sigaction action = { .sa_sigaction = text_sigbus, .sa_flags = SA_SIGINFO };
    sigemptyset(&action.sa_mask);
    sigaction(SIGBUS, &action, nullptr);
}

// The calling thread's read chunk (each thread packs its own: threads writing into the same pages would fault on them
// together)
static thread_local char* text_read_at;
static thread_local size_t text_read_left;

// size + 1 bytes (the last one zero, a terminator) of the thread's read chunk
static char* text_read_slot(size_t size) {
    size_t need = (size + 1 + 7) & ~(size_t)7;
    if (text_read_left < need) {
        // (Populated: a read() faulting on its pages waits for the mm lock behind the other threads' mmap()s of the
        // big files, which made the load phase 20-40% slower on vscode)
        void* chunk = mmap(nullptr, TEXT_READ_CHUNK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
        if (chunk == MAP_FAILED) return nullptr;
        vma_name(chunk, TEXT_READ_CHUNK, "text");
        text_read_at = chunk;
        text_read_left = TEXT_READ_CHUNK;
    }
    char* ret = text_read_at;
    text_read_at += need;
    text_read_left -= need;
    return ret;
}

// A small file read into a read chunk, wrapped like a mapping: the text's id (0 if it can't be read)
static u32 source_text_read(int fd, size_t size, cstr* out) {
    char* slot = text_read_slot(size);
    size_t at = 0;
    while (slot && at < size) {
        ssize_t n = read(fd, slot + at, size - at);
        if (n <= 0) break;
        at += (size_t)n;
    }
    close(fd);
    // (Truncated meanwhile: what there was. A failed read leaves its bytes in the chunk)
    if (!slot || !at) return 0;
    slot[at] = 0;
    pthread_mutex_lock(&texts.lock);
    if (texts.mapping_count == texts.mapping_cap) {
        texts.mapping_cap = texts.mapping_cap ? texts.mapping_cap * 2 : 1024;
        texts.mappings = realloc(texts.mappings, texts.mapping_cap * sizeof(TextMapping));
    }
    // (Not a mapping: the slot is null, so nothing unmaps or detaches it)
    texts.mappings[texts.mapping_count++] = (TextMapping){ .size = at };
    u32 id = texts.mapping_count;
    pthread_mutex_unlock(&texts.lock);
    cstr s = {0};
    s.lon.data = slot;
    s.lon.size = (isize)at;
    cstr_l_set_cap(&s, (isize)at);
    check(cstr_is_long(&s) && (size_t)cstr_size(&s) == at && cstr_str(&s) == slot);
    *out = s;
    return id;
}

u32 source_text_map(csview path, cstr* out) {
    static pthread_once_t installed = PTHREAD_ONCE_INIT;
    pthread_once(&installed, text_sigbus_install);
    cstr z = cstr_from_sv(path);
    int fd = open(cstr_str(&z), O_RDONLY | O_CLOEXEC);
    cstr_drop(&z);
    if (fd < 0) return 0;
    struct stat st;
    // (The mapping's last page is zero past the end of the file: the terminator the parser reads. A size that fills
    // its last page has none)
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size == 0 || st.st_size > INT32_MAX) {
        close(fd);
        return 0;
    }
    if ((size_t)st.st_size < text_read_below()) return source_text_read(fd, (size_t)st.st_size, out);
    if ((size_t)st.st_size == text_page_round((size_t)st.st_size)) {
        close(fd);
        return 0;
    }
    size_t size = (size_t)st.st_size;
    pthread_mutex_lock(&texts.lock);
    char* slot = text_slot_locked(text_page_round(size));
    if (slot) texts.live++;
    pthread_mutex_unlock(&texts.lock);
    // (A failed mapping leaves its slot reserved)
    void* text = slot ? mmap(slot, size, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0) : MAP_FAILED;
    close(fd);
    pthread_mutex_lock(&texts.lock);
    u32 id = 0;
    if (text == MAP_FAILED) {
        if (slot) texts.live--;
    } else {
        if (texts.mapping_count == texts.mapping_cap) {
            texts.mapping_cap = texts.mapping_cap ? texts.mapping_cap * 2 : 1024;
            texts.mappings = realloc(texts.mappings, texts.mapping_cap * sizeof(TextMapping));
        }
        texts.mappings[texts.mapping_count++] = (TextMapping){ .dev = st.st_dev, .ino = st.st_ino, .slot = slot, .size = size };
        id = texts.mapping_count;
    }
    pthread_mutex_unlock(&texts.lock);
    if (!id) return 0;
    // A long cstr pointing at the mapping: read as any other, never dropped (source_file_drop() unmaps it)
    cstr s = {0};
    s.lon.data = text;
    s.lon.size = size;
    cstr_l_set_cap(&s, (isize)size);
    check(cstr_is_long(&s) && (size_t)cstr_size(&s) == size && cstr_str(&s) == text);
    *out = s;
    return id;
}

void source_text_unmap(u32 id) {
    pthread_mutex_lock(&texts.lock);
    TextMapping* mapping = &texts.mappings[id - 1];
    // (A read text stays in its chunk)
    if (!mapping->slot) {
        pthread_mutex_unlock(&texts.lock);
        return;
    }
    mmap(mapping->slot, text_page_round(mapping->size), PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
    mapping->slot = nullptr;
    texts.live--;
    pthread_mutex_unlock(&texts.lock);
}

void source_texts_release(void) {
    pthread_mutex_lock(&texts.lock);
    for (u32 i = 0; i < texts.count; i++) madvise(texts.segments[i].base, texts.segments[i].used, MADV_DONTNEED);
    pthread_mutex_unlock(&texts.lock);
}

void source_texts_drop(void) {
    pthread_mutex_lock(&texts.lock);
    // (The segments stay reserved: a later run's texts go after the used part)
    free(texts.mappings);
    texts.mappings = nullptr;
    texts.mapping_count = texts.mapping_cap = 0;
    pthread_mutex_unlock(&texts.lock);
}

void source_text_detach(csview path) {
    cstr z = cstr_from_sv(path);
    struct stat st;
    bool exists = !stat(cstr_str(&z), &st);
    cstr_drop(&z);
    if (!exists) return;
    pthread_mutex_lock(&texts.lock);
    for (u32 i = 0; i < texts.mapping_count; i++) {
        TextMapping* mapping = &texts.mappings[i];
        if (!mapping->slot || mapping->dev != st.st_dev || mapping->ino != st.st_ino) continue;
        // The text moves to a memory file mapped at the same address (anonymous memory would be zeroed by
        // source_texts_release())
        int fd = memfd_create("tttt-text", MFD_CLOEXEC);
        bool copied = fd >= 0;
        for (size_t at = 0; copied && at < mapping->size; ) {
            ssize_t n = write(fd, mapping->slot + at, mapping->size - at);
            if (n <= 0) copied = false;
            else at += (size_t)n;
        }
        panic_if(!copied || mmap(mapping->slot, mapping->size, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0) == MAP_FAILED,
            "can't keep the text of ", path, " before overwriting it");
        close(fd);
        mapping->dev = 0;
        mapping->ino = 0;
    }
    pthread_mutex_unlock(&texts.lock);
}

static bool contains_internal(char const* from, char const* to) {
    for (char const* c = from; c + 9 <= to; c++) {
        if (*c == '@' && memcmp(c, "@internal", 9) == 0) return true;
    }
    return false;
}


bool source_text_internal_before(SourceFile const* me, char const* at) {
    char const* text = cstr_str(&my contents);
    if (at < text || at > text + cstr_size(&my contents)) return false;
    // Back over the comments and whitespace to the previous token, remembering the last block comment that says it
    char const* internal_block = nullptr; // Its start
    bool internal_line = false;
    for (;;) {
        while (at > text && (at[-1] == ' ' || at[-1] == '\t' || at[-1] == '\n' || at[-1] == '\r')) at--;
        if (at - text >= 2 && at[-1] == '/' && at[-2] == '*') {
            char const* open = at - 2;
            while (open - text >= 2 && !(open[-2] == '/' && open[-1] == '*')) open--;
            if (open - text < 2) return false;
            open -= 2;
            if (!internal_block && contains_internal(open, at)) internal_block = open;
            at = open;
            continue;
        }
        char const* line = at;
        while (line > text && line[-1] != '\n') line--;
        char const* first = line;
        while (first < at && (*first == ' ' || *first == '\t')) first++;
        if (at - first >= 2 && first[0] == '/' && first[1] == '/') {
            // (A whole-line one: after a line break, so leading)
            internal_line |= contains_internal(first, at);
            at = line;
            continue;
        }
        break;
    }
    if (internal_line) return true;
    if (!internal_block) return false;
    // A block comment on the previous token's line is its trailing comment, not the declaration's (tsc's
    // getLeadingCommentRanges collects from the first line break on, or from the start of the file)
    if (at == text) return true;
    for (char const* c = at; c < internal_block; c++) {
        if (*c == '\n') return true;
    }
    return false;
}
