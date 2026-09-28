#include "program.h"
#include "string_helpers.h"
#include "unused.h"
#include "import_attributes.h"
#include "parser.h"
#include "block.h"
#include <cleat/filesystem.h>
#include "error.h"
#include "statement.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "interface_decl.h"
#include "port_statement.h"
#include "source_file.h"
#include "ty/ty.h"
#include "module_resolution.h"
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/resource.h>

#define g_class_key SourceFile, source_file
#define g_no_clone
#include <cleat/generic/box.h>

// Keep source files in a box so new ones can be added without invalidating old pointers
#define g_pro_key SvStr, svstr
#define g_class_val BoxSourceFile, box_source_file
#define g_no_clone
#include <cleat/generic/hmap.h>

typedef SourceFile* SourceFilePtr;
#define g_key SourceFilePtr, source_file_ptr
#include <cleat/generic/vec.h>

#define g_pro_key SvStr, svstr
#define g_val SourceFilePtr, source_file_ptr
#include <cleat/generic/hmap.h>

// Where the standard library is unless program_set_lib_dir() says otherwise (the repo root is the working directory)
static const csview default_lib_dir = c_sv_const_init("thirdparty/lib");

typedef enum {
    PROGRAM_PHASE_LOADING = 0, // Files may be loaded
    PROGRAM_PHASE_RESOLVING_NAMES, // Names currently being resolved
    PROGRAM_PHASE_NAMES_RESOLVED, // Resolving names is done
    PROGRAM_PHASE_TYPECHECKING, // Typechecking has been started
    PROGRAM_PHASE_TYPECHECKED, // Typechecking has completed
} ProgramPhase;

static const char* program_phase_get_str(ProgramPhase phase) {
    switch (phase) {
        case PROGRAM_PHASE_LOADING: return "loading";
        case PROGRAM_PHASE_RESOLVING_NAMES: return "resolving names";
        case PROGRAM_PHASE_NAMES_RESOLVED: return "names resolved";
        case PROGRAM_PHASE_TYPECHECKING: return "typechecking";
        case PROGRAM_PHASE_TYPECHECKED: return "typecheck done";
    }
    impossible;
}

#define ASSERT_PHASE(expected) \
    panic_if( \
        my phase != expected, \
        "expected program phase to be ", program_phase_get_str(expected), \
        ", was actually ", program_phase_get_str(my phase) \
    ) \

#define CHANGE_PHASE(from, to) ASSERT_PHASE(from); my phase = to;

typedef struct Program {
    ProgramPhase phase;
    bool loading_stdlib; // If we're loading standard library files instead of normal files
    bool follow_lib_references; // program_follow_lib_references()
    cstr lib_dir; // cstr_null: default_lib_dir
    ErrorCtx* errs; // unowned
    Fs const* fs;
    Binder* binder;
    ModuleResolver* resolver; // Made on first use, the options are final by then
    HMapBoxSourceFile source_files_map;
    VecSourceFilePtr source_files_ordered;
    HMapSourceFilePtr files_by_package_id; // ResolvedModule.package_id -> the first file found with it
    Checker* checker; // Created by program_make_current(), checks the first share of the files
    Checker** checkers; // malloc'd, checker_count of them once typechecked: checker and the others
    u32 checker_count;
    u32 requested_checkers; // program_set_checkers()
    u32 threads; // program_set_threads(), 0: 1
    CompilerOptions options;
} Program;

static void program_add_builtins(Program* me) {
#define GLOBAL_VALUE(global_name, global_value) {\
    Symbol* symbol = nullptr; \
    scope_init_declaration(my binder->global_scope, &symbol, (DeclProps) { \
        .name = sv_make_span(c_sv(global_name)), \
        .kind = SYMBOL_DECL_BUILTIN, \
        .node = &global_value, \
        .function_scoped = true, \
    } ); \
}

#define GLOBAL_TYPE(global_name, global_type) {\
    Symbol* symbol = nullptr; \
    scope_init_declaration(my binder->global_scope, &symbol, (DeclProps) { \
        .name = sv_make_span(c_sv(global_name)), \
        .kind = SYMBOL_DECL_BUILTIN, \
        .node = &global_type, \
    } ); \
}

    binder_declare_global_this(my binder);

    GLOBAL_TYPE("any", type_any);
    GLOBAL_TYPE("unknown", type_unknown);
    GLOBAL_TYPE("never", type_never);
    GLOBAL_TYPE("void", type_void);
    GLOBAL_TYPE("object", type_object);
    GLOBAL_TYPE("boolean", type_boolean);
    GLOBAL_TYPE("number", type_number);
    GLOBAL_TYPE("string", type_string);
    GLOBAL_TYPE("intrinsic", type_any);
}

static SourceFile* program_add_source_idempotent(Program* me, csview path);

Program* program_new(ErrorCtx* errs, Fs const* fs) {
    check(errs);
    check(fs);
    Program* ret = c_new(Program, {
        .errs = errs,
        .fs = fs,
        .binder = binder_new(errs),
        .options = compiler_options_init(),
    });
    program_add_builtins(ret);
    return ret;
}

static void program_drop(Program* me) {
    // First, its memos point into the AST and the scopes
    for (u32 i = 1; i < my checker_count; i++) checker_drop(my checkers[i]);
    free(my checkers);
    checker_drop(my checker);
    hmap_box_source_file_drop(&my source_files_map);
    hmap_source_file_ptr_drop(&my files_by_package_id);
    vec_source_file_ptr_drop(&my source_files_ordered);
    module_resolver_destroy(my resolver);
    cstr_drop(&my lib_dir);
    compiler_options_drop(&my options);
    binder_destroy(my binder); // After the AST: type nodes in it may own things symbols point to
}

CompilerOptions* program_options(Program* me) {
    return &my options;
}

void program_destroy(Program* me) {
    if (me) {
        program_drop(me);
        free(me);
    }
}

static ModuleResolver* program_resolver(Program* me) {
    if (!my resolver) my resolver = module_resolver_new(my fs, &my options);
    return my resolver;
}

void program_set_lib_dir(Program* me, csview dir) {
    cstr_assign_sv(&my lib_dir, dir);
}

void program_follow_lib_references(Program* me) {
    my follow_lib_references = true;
}

void program_set_realpath(Program* me, cstr (*realpath)(void* ctx, csview path), void* ctx) {
    module_resolver_set_realpath(program_resolver(me), realpath, ctx);
}

// tsc names files by absolute path, and its test harness strips the directory the test's files are in: relative
// below the working directory, absolute elsewhere
static cstr program_path_for_message(Program* me, csview absolute) {
    cstr cwd = cstr_null;
    try (fs_get_cwd(my fs, &cwd)) or_panic;
    if (cstr_size(&cwd) > 1) cstr_append_lit(&cwd, "/");
    cstr ret = cstr_size(&cwd) > 1 && absolute.size > cstr_size(&cwd) &&
            !memcmp(absolute.buf, cstr_str(&cwd), (size_t)cstr_size(&cwd)) ?
        cstr_from_sv(csview_slice(absolute, cstr_size(&cwd), absolute.size)) : cstr_from_sv(absolute);
    cstr_drop(&cwd);
    return ret;
}

// Whether a script declares `declare module "<name>"` (tsc's SourceFile.ambientModuleNames)
static bool source_declares_ambient_module(SourceFile const* source, csview name) {
    for (u32 i = 0; i < source->module_ref_count; i++) {
        ModuleRef const* ref = source->module_refs[i];
        if (ref->is_declaration && csview_equals_sv(cstr_sv(&ref->path), name)) return true;
    }
    return false;
}

// A resolved TypeScript file becomes part of the program, anything else is remembered on the ref as why there's no file
static void program_load_module_ref(Program* me, SourceFile* source, ModuleRef* ref) {
    ModuleResolver* resolver = program_resolver(me);
    csview containing = cstr_sv(&source->canonical_path);
    // The conditions `exports` are read with: by syntax, else the format of the importing file
    ResolutionMode mode = ref->syntax == MODULE_REF_REQUIRE ? RESOLUTION_MODE_REQUIRE_SYNTAX :
        ref->syntax == MODULE_REF_DYNAMIC || source->is_esm ? RESOLUTION_MODE_ESM : RESOLUTION_MODE_COMMONJS;
    if (ref->resolution_override == IMPORT_RESOLUTION_IMPORT) mode = RESOLUTION_MODE_ESM | RESOLUTION_MODE_EXPLICIT;
    if (ref->resolution_override == IMPORT_RESOLUTION_REQUIRE) mode = RESOLUTION_MODE_REQUIRE_SYNTAX | RESOLUTION_MODE_EXPLICIT;
    ResolvedModule resolved = module_resolver_resolve(resolver, cstr_sv(&ref->path), containing, mode);
    if (cstr_size(&resolved.ambiguous_root_package_json)) {
        cstr shown = program_path_for_message(me, cstr_sv(&resolved.ambiguous_root_package_json));
        cstr message = cstr_d(
            "The project root is ambiguous, but is required to resolve ",
            resolved.ambiguous_root_in_imports ? "import" : "export", " map entry '", resolved.ambiguous_root_entry,
            "' in file '", shown, "'. Supply the `rootDir` compiler option to disambiguate."
        );
        unsigned code = resolved.ambiguous_root_in_imports ? 2210 : 2209;
        bool seen = false;
        if (my errs) for_each (i, vec_error, &my errs->errors) {
            if (i->ts_code == code && cstr_eq(&i->message, &message)) seen = true;
        }
        if (seen) cstr_drop(&message);
        else error_ctx_global_error_s(my errs, code, message);
        cstr_drop(&shown);
    }
    if (my options.no_resolve) {
        // Nothing is added to the program, but a name may still mean a file that is part of it
        if (resolved.kind != RESOLVED_NONE) ref->file = program_find_source(me, cstr_sv(&resolved.path));
        resolved_module_drop(&resolved);
        return;
    }
    switch (resolved.kind) {
        case RESOLVED_TS:
        case RESOLVED_DTS:
            if (my options.jsx == JSX_NONE && csview_ends_with(cstr_sv(&resolved.path), ".tsx")) {
                ref->failure = MODULE_REF_JSX_NOT_SET;
                cstr_take(&ref->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
            } else if (resolved.arbitrary_extension && !my options.allow_arbitrary_extensions && !source->is_declaration) {
                ref->failure = MODULE_REF_ARBITRARY_EXTENSION;
                cstr_take(&ref->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
            } else {
                ref->file = program_add_source_idempotent(me, cstr_sv(&resolved.path));
                ref->used_ts_extension = resolved.used_ts_extension;
                // The same version of a package installed twice is one package (tsc's packageIdToSourceFile): the
                // second copy stays in the program, but imports mean the first
                if (cstr_size(&resolved.package_id) && ref->file) {
                    auto first = hmap_source_file_ptr_get(&my files_by_package_id, cstr_sv(&resolved.package_id));
                    if (first) {
                        ref->file = first->second;
                    } else {
                        hmap_source_file_ptr_insert(&my files_by_package_id, cstr_clone(resolved.package_id), ref->file);
                    }
                }
            }
            break;
        case RESOLVED_JS:
            if (my options.jsx == JSX_NONE && csview_ends_with(cstr_sv(&resolved.path), ".jsx")) {
                ref->failure = MODULE_REF_JSX_NOT_SET;
                cstr_take(&ref->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
                break;
            }
            // JavaScript of a package isn't part of the program even with allowJs (tsc's maxNodeModuleJsDepth is 0)
            // (a package that is a link to somewhere outside of any node_modules is the project's own code)
            if (my options.allow_js && !(resolved.is_external_library &&
                csview_find_sv(cstr_sv(&resolved.path), c_sv("/node_modules/")) != c_NPOS)
            ) {
                ref->file = program_add_source_idempotent(me, cstr_sv(&resolved.path));
            } else {
                ref->failure = MODULE_REF_IS_JS;
                cstr_take(&ref->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
            }
            break;
        case RESOLVED_JSON:
            ref->file = program_add_source_idempotent(me, cstr_sv(&resolved.path));
            if (!ref->file) ref->failure = MODULE_REF_IS_JSON;
            break;
        case RESOLVED_NONE:
            if (resolved.json_without_option) {
                ref->failure = MODULE_REF_JSON_WITHOUT_OPTION;
            } else if (resolved.needs_extension) {
                ref->failure = MODULE_REF_NEEDS_EXTENSION;
                if (cstr_size(&resolved.suggested_extension)) {
                    cstr_take(&ref->resolved_path, cstr_d(ref->path, resolved.suggested_extension));
                }
            }
            break;
    }
    resolved_module_drop(&resolved);
}

// `/// <reference path="a.ts" />`: the file itself, relative to the one with the directive
static SourceFile* program_load_reference_path(Program* me, Span path_span, csview path) {
    check(span_valid(path_span));
    csview dir = dirname_of(cstr_sv(&path_span.source->canonical_path));
    cstr full = path.size && path.buf[0] == '/' ? cstr_from_sv(path) : cstr_d(dir, "/", path);
    SourceFile* ret = nullptr;
    static const char* const extensions[] = { "", ".ts", ".tsx", ".d.ts" };
    for (isize i = 0; i < (isize)c_arraylen(extensions) && !ret; i++) {
        cstr candidate = cstr_d(full, extensions[i]);
        if (fs_is_reg_file(my fs, cstr_sv(&candidate))) ret = program_add_source_idempotent(me, cstr_sv(&candidate));
        cstr_drop(&candidate);
    }
    cstr_drop(&full);
    return ret;
}

// A `types` reference from source (the program's `types` when null) with the reference's resolution mode, else the file's
static SourceFile* program_load_type_reference(
    Program* me, csview name, SourceFile const* source, ImportResolutionOverride override
) {
    ResolutionMode mode = RESOLUTION_MODE_COMMONJS;
    bool has_mode = true;
    if (override == IMPORT_RESOLUTION_IMPORT) mode = RESOLUTION_MODE_ESM;
    else if (override == IMPORT_RESOLUTION_REQUIRE) mode = RESOLUTION_MODE_COMMONJS;
    else if (source && source->has_format) mode = source->is_esm ? RESOLUTION_MODE_ESM : RESOLUTION_MODE_COMMONJS;
    else has_mode = false;
    ResolvedModule resolved = module_resolver_resolve_type_reference(
        program_resolver(me), name, source ? cstr_sv(&source->canonical_path) : c_sv(""), has_mode ? &mode : nullptr
    );
    SourceFile* ret = resolved.kind == RESOLVED_NONE ? nullptr :
        program_add_source_idempotent(me, cstr_sv(&resolved.path));
    resolved_module_drop(&resolved);
    return ret;
}

// Called for each toplevel statement of each loaded file
static inline void program_handle_statement(Program* me, SourceFile* source, Statement* statement) {
    switch (statement->tag) {
        case STATEMENT_SOURCE_DIRECTIVE:
            switch (statement->source_directive->tag) {
                case SOURCE_DIRECTIVE_REFERENCE_PATH:
                    if (my options.no_resolve) break;
                    if (!program_load_reference_path(
                        me,
                        statement->source_directive->path_span,
                        cstr_sv(&statement->source_directive->path)
                    )) {
                        // Reported when names are resolved: it is no reason not to typecheck
                        cstr cwd = cstr_null;
                        try (fs_get_cwd(my fs, &cwd)) or_panic;
                        cstr dir = absolute_path_of(cstr_sv(&cwd), dirname_of(cstr_sv(&source->canonical_path)));
                        cstr absolute = absolute_path_of(cstr_sv(&dir), cstr_sv(&statement->source_directive->path));
                        cstr_take(&statement->source_directive->not_found, program_path_for_message(me, cstr_sv(&absolute)));
                        cstr_drop(&absolute);
                        cstr_drop(&dir);
                        cstr_drop(&cwd);
                    }
                    break;
                case SOURCE_DIRECTIVE_REFERENCE_TYPES:
                    if (!program_load_type_reference(
                        me, cstr_sv(&statement->source_directive->path), source,
                        statement->source_directive->resolution_override
                    )) {
                        cstr_assign_sv(&statement->source_directive->not_found, cstr_sv(&statement->source_directive->path));
                    }
                    break;
                case SOURCE_DIRECTIVE_REFERENCE_LIB: {
                    // Between lib files: they are all in one directory. From another file (tsc's
                    // processLibReferenceDirectives, `@types/node` has some): a lib like the ones the options name. An
                    // unknown name is ignored (TODO: TS2726). The harness keeps libs in a shared program, which a
                    // test's file can't add to (TODO)
                    cstr name = cstr_d(statement->source_directive->path, ".d.ts");
                    cstr_lowercase(&name);
                    if (my loading_stdlib) {
                        program_load_reference_path(me, statement->source_directive->path_span, cstr_sv(&name));
                    } else if (my follow_lib_references && !my options.no_lib) {
                        cstr path = cstr_d(cstr_size(&my lib_dir) ? cstr_sv(&my lib_dir) : default_lib_dir, "/", name);
                        if (fs_is_reg_file(my fs, cstr_sv(&path))) {
                            my loading_stdlib = true;
                            program_add_source_idempotent(me, cstr_sv(&path));
                            my loading_stdlib = false;
                        }
                        cstr_drop(&path);
                    }
                    cstr_drop(&name);
                }   break;
            }
            break;

        case STATEMENT_IMPORT_EXPORT: {
            PortStatement* port = statement->import_export;
            if ((port->main.tag != PORT_IMPORT_ALIAS || port->main.export_import) && !port->jsdoc) {
                source->is_module = true;
                if (!span_valid(source->module_indicator)) source->module_indicator = statement->span;
            }
        }   break;

        case STATEMENT_EXPORT_DEFAULT_EXPR:
            source->is_module = true;
            if (!span_valid(source->module_indicator)) source->module_indicator = statement->span;
            break;

        case STATEMENT_DECL_NAMESPACE:
        case STATEMENT_DECL_VARIABLE:
        case STATEMENT_DECL_FUNCTION:
        case STATEMENT_DECL_ENUM:
        case STATEMENT_DECL_CLASS:
        case STATEMENT_DECL_TYPE:
        case STATEMENT_DECL_INTERFACE:
            if (statement->decl.props.exported) {
                source->is_module = true;
                if (!span_valid(source->module_indicator)) {
                    Span name = statement->tag == STATEMENT_DECL_CLASS ? statement->decl.class->name :
                        statement->tag == STATEMENT_DECL_FUNCTION ? statement->decl.function.impl->name :
                        statement->tag == STATEMENT_DECL_ENUM ? statement->decl.enum_->name :
                        statement->tag == STATEMENT_DECL_INTERFACE ? statement->decl.interface->name :
                        statement->tag == STATEMENT_DECL_TYPE ? statement->decl.type.name :
                        statement->tag == STATEMENT_DECL_NAMESPACE ? statement->decl.namespace.name : span_null;
                    source->module_indicator = span_valid(name) ? name : statement->span;
                }
            }
            break;

        case STATEMENT_NOOP:
        case STATEMENT_BLOCK:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_SWITCH:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_LABELED:
        case STATEMENT_WITH:
        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE:
        case STATEMENT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            break;
    }
}

// Loads the file at the given path, adds it to the program, and returns a SourceFile for it. Returns nullptr on error.
// Only loads each file once, even if paths aren't expressed identically. Calls itself recursively to load imports and
// referenced files.
static bool path_is_js(csview path) {
    return csview_ends_with(path, ".js") || csview_ends_with(path, ".jsx") || csview_ends_with(path, ".mjs") ||
        csview_ends_with(path, ".cjs");
}

// `// @ts-check` (1) or `// @ts-nocheck` (-1) in the comments at the top of a file, the last one wins. 0 without
static int source_check_directive(csview contents) {
    int ret = 0;
    const char* c = contents.buf;
    const char* end = contents.buf + contents.size;
    while (c < end) {
        while (c < end && (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n')) c++;
        if (c + 1 >= end || c[0] != '/' || (c[1] != '/' && c[1] != '*')) break;
        const char* comment_end;
        if (c[1] == '/') {
            comment_end = c;
            while (comment_end < end && *comment_end != '\n') comment_end++;
        } else {
            csview rest = csview_from_range(c + 2, end);
            isize close = csview_find_sv(rest, c_sv("*/"));
            comment_end = close == c_NPOS ? end : rest.buf + close + 2;
        }
        // tsc: /^\/\/\/?\s*@ts-(no)?check/, single line comments only
        if (c[1] == '/') {
            const char* p = c + 2;
            if (p < comment_end && *p == '/') p++;
            while (p < comment_end && (*p == ' ' || *p == '\t')) p++;
            csview text = csview_from_range(p, comment_end);
            if (csview_starts_with(text, "@ts-nocheck")) ret = -1;
            else if (csview_starts_with(text, "@ts-check")) ret = 1;
        }
        c = comment_end;
    }
    return ret;
}

static bool equals_ignoring_case(csview sv, const char* lowercase) {
    isize len = (isize)strlen(lowercase);
    if (sv.size != len) return false;
    for (isize i = 0; i < len; i++) {
        if (tolower((unsigned char)sv.buf[i]) != lowercase[i]) return false;
    }
    return true;
}

// tsc's processCommentPragmas as far as the JSX pragmas go: in each `/* */` comment at the top of the file, every
// line's `@name args` (/@(\S+)(\s+(?:\S.*)?)?$/gm), the first word of args being the argument
static void source_jsx_pragmas(SourceFile* file) {
    csview contents = cstr_sv(&file->contents);
    const char* c = contents.buf;
    const char* end = contents.buf + contents.size;
    while (c < end) {
        while (c < end && (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n')) c++;
        if (c + 1 >= end || c[0] != '/' || (c[1] != '/' && c[1] != '*')) break;
        const char* comment_end;
        if (c[1] == '/') {
            comment_end = c;
            while (comment_end < end && *comment_end != '\n') comment_end++;
            c = comment_end;
            continue;
        }
        csview rest = csview_from_range(c + 2, end);
        isize close = csview_find_sv(rest, c_sv("*/"));
        comment_end = close == c_NPOS ? end : rest.buf + close + 2;
        for (const char* line = c; line < comment_end;) {
            const char* line_end = line;
            while (line_end < comment_end && *line_end != '\n' && *line_end != '\r') line_end++;
            const char* at = memchr(line, '@', (size_t)(line_end - line));
            if (at) {
                const char* name_end = at + 1;
                while (name_end < line_end && !isspace((unsigned char)*name_end)) name_end++;
                const char* arg = name_end;
                while (arg < line_end && isspace((unsigned char)*arg)) arg++;
                const char* arg_end = arg;
                while (arg_end < line_end && !isspace((unsigned char)*arg_end)) arg_end++;
                csview name = csview_from_range(at + 1, name_end);
                csview value = csview_from_range(arg, arg_end);
                if (value.size) {
                    if (equals_ignoring_case(name, "jsx") && !file->jsx_pragmas.factory.size) file->jsx_pragmas.factory = value;
                    if (equals_ignoring_case(name, "jsxfrag") && !file->jsx_pragmas.fragment_factory.size) file->jsx_pragmas.fragment_factory = value;
                    if (equals_ignoring_case(name, "jsxruntime")) file->jsx_pragmas.runtime = value;
                    if (equals_ignoring_case(name, "jsximportsource")) file->jsx_pragmas.import_source = value;
                }
            }
            line = line_end;
            while (line < comment_end && (*line == '\n' || *line == '\r')) line++;
        }
        c = comment_end;
    }
}

static SourceFile* program_add_source_idempotent(Program* me, csview path) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);

    cstr cwd = cstr_null;
    // Not being able to determine our own working directory isn't something we can carry on from
    try (fs_get_cwd(my fs, &cwd)) or_panic;
    cstr canonical_path = relative_path_of(cstr_sv(&cwd), path);
    cstr_drop(&cwd);

    auto cached_source = hmap_box_source_file_get(&my source_files_map, cstr_sv(&canonical_path));
    if (cached_source) {
        // A source file is already created for this path
        cstr_drop(&canonical_path);
        return cached_source->second.get;
    }

    // No source file is created for this path, load it
    cstr contents;
    try (fs_read_file(my fs, cstr_sv(&canonical_path), &contents)) if_err (e) {
        // Not worth reporting: the caller turns a null return into a TypeScript "file not found" error
        ignore e;
        current_error_destroy();
        cstr_drop(&canonical_path);
        return nullptr;
    }
    text_decode(&contents);

    SourceFile* source_file = c_new(
        SourceFile,
        source_file_make(cstr_clone(canonical_path), contents)
    );

    if (csview_ends_with(path, ".d.ts") || csview_ends_with(path, ".d.mts") || csview_ends_with(path, ".d.cts")) {
        source_file->is_declaration = true;
    } else if (csview_ends_with(path, ".ts")) {
        // tsc's getDeclarationFileExtension: `x.d.css.ts` (allowArbitraryExtensions) declares too
        isize base = path.size;
        while (base > 0 && path.buf[base - 1] != '/') base--;
        for (isize i = base; i + 3 <= path.size; i++) {
            if (!memcmp(path.buf + i, ".d.", 3)) source_file->is_declaration = true;
        }
    }

    source_file->is_js = path_is_js(path);
    source_file->is_json = csview_ends_with(path, ".json");
    source_file->is_jsx = source_file->is_js || csview_ends_with(path, ".tsx");
    source_jsx_pragmas(source_file);
    int check_directive = source_check_directive(cstr_sv(&source_file->contents));
    source_file->unchecked = check_directive ? check_directive < 0 : source_file->is_js && !my options.check_js;
    // tsc's skipTypeChecking: declaration files aren't checked with skipLibCheck
    if (source_file->is_declaration && my options.skip_lib_check) source_file->unchecked = true;
    if (my options.no_check) source_file->unchecked = true; // (tsc's skipTypeChecking: no semantic diagnostics at all)

    if (my loading_stdlib) {
        source_file->is_stdlib = true;
        csview filename = basename_of(cstr_sv(&source_file->canonical_path));
        cstr_take(&source_file->decl_override, cstr_d("lib.", filename, ", --, --"));
    }

    bool node_formats = (my options.module >= MODULE_KIND_NODE16 && my options.module <= MODULE_KIND_NODENEXT) ||
        my options.module_resolution == MODULE_RESOLUTION_NODE16 ||
        my options.module_resolution == MODULE_RESOLUTION_NODENEXT;
    // (A JSON file has no module format of its own: tsc's getImpliedNodeFormatForFile, which also looks at the
    // package.json of anything in node_modules)
    bool in_node_modules = csview_contains(cstr_sv(&source_file->canonical_path), "/node_modules/");
    if ((node_formats || in_node_modules) && !my loading_stdlib && !source_file->is_json) {
        FileFormat format = module_resolver_file_format(program_resolver(me), cstr_sv(&source_file->canonical_path));
        source_file->is_esm = format.mode == RESOLUTION_MODE_ESM;
        source_file->has_format = true;
        source_file->package_has_type = format.package_has_type;
        if (cstr_size(&format.package_json)) {
            source_file->package_json = program_path_for_message(me, cstr_sv(&format.package_json));
        }
        cstr_drop(&format.package_json);
    }

    parse_file(source_file, my errs);
    if (source_file->is_json) source_file->is_module = true;

    hmap_box_source_file_insert(&my source_files_map, canonical_path, box_source_file_from_ptr(source_file));

    for_each (i, vec_statement, &source_file->ast.statements) {
        program_handle_statement(me, source_file, i);
    }
    // tsc's setCommonJsModuleIndicator: ES imports and exports win
    if (source_file->is_js && source_file->has_commonjs && !source_file->is_module) {
        source_file->is_module = true;
        source_file->is_commonjs = true;
    }
    // tsc's setExternalModuleIndicator by moduleDetection: `force` makes every file that isn't a declaration file a
    // module, `auto` (the default) too for `import.meta`, a module format (.mts/.cts/.mjs/.cjs, an ESM package), and
    // a JSX tag under react-jsx
    if (!source_file->is_module && !source_file->is_declaration && !my loading_stdlib) {
        csview name = cstr_sv(&source_file->canonical_path);
        switch (my options.module_detection) {
            case MODULE_DETECTION_FORCE:
                source_file->is_module = true;
                break;
            case MODULE_DETECTION_AUTO:
                source_file->is_module = source_file->has_import_meta || source_file->is_esm ||
                    csview_ends_with(name, ".mts") || csview_ends_with(name, ".cts") ||
                    csview_ends_with(name, ".mjs") || csview_ends_with(name, ".cjs") ||
                    (source_file->has_jsx && (my options.jsx == JSX_REACT_JSX || my options.jsx == JSX_REACT_JSXDEV));
                break;
            case MODULE_DETECTION_LEGACY:
                source_file->is_module = source_file->has_import_meta;
                break;
        }
    }
    if (my options.import_helpers && source_file->is_module && !source_file->is_declaration && !my loading_stdlib) {
        Span at = { .source = source_file, .sv = csview_from_n(source_file_start(source_file), 0) };
        source_file->helpers_ref = source_file_add_module_ref(source_file, cstr_lit("tslib"), at);
        // First, like tsc's: it decides the order files are in the program
        memmove(source_file->module_refs + 1, source_file->module_refs,
            sizeof(ModuleRef*) * (source_file->module_ref_count - 1));
        source_file->module_refs[0] = source_file->helpers_ref;
        // Resolved like the import the emitter writes: a `require()` in CommonJS output
        if (ty_emits_commonjs_like(source_file)) source_file->helpers_ref->syntax = MODULE_REF_REQUIRE;
    }
    cstr jsx_runtime = my loading_stdlib ? cstr_null : source_file_jsx_runtime_import(source_file);
    if (cstr_size(&jsx_runtime)) {
        Span at = { .source = source_file, .sv = csview_from_n(source_file_start(source_file), 0) };
        ModuleRef* ref = source_file_add_module_ref(source_file, jsx_runtime, at);
        // Right after the helpers, before what the file imports itself (tsc's order of file.imports)
        u32 first = source_file->helpers_ref ? 1 : 0;
        memmove(source_file->module_refs + first + 1, source_file->module_refs + first,
            sizeof(ModuleRef*) * (source_file->module_ref_count - first - 1));
        source_file->module_refs[first] = ref;
        ref->ambient_only = !source_file->is_js && (source_file->is_declaration ||
            !(source_file->is_module || my options.isolated_modules || my options.verbatim_module_syntax));
        source_file->jsx_runtime_ref = ref;
    } else {
        cstr_drop(&jsx_runtime);
    }

    // Every module the file mentions, wherever (the parser lists them). By index: loading parses, never this file
    for (u32 i = 0; i < source_file->module_ref_count; i++) {
        ModuleRef* ref = source_file->module_refs[i];
        // `declare module "./a" {}` in a module augments a file. In a script it declares an ambient module, and so
        // does a name that finds no file
        if (ref->is_declaration && !source_file->is_module) continue;
        if (ref->ambient_only) continue;
        if (my options.no_resolve) continue; // program_resolve_names(), once every root is loaded
        // A name the file declares an ambient module for is that module, not a file (tsc's resolveToOwnAmbientModule):
        // `import * as React from "react"` inside react16.d.ts's `declare module "react/jsx-runtime"`
        if (!source_file->is_module && source_declares_ambient_module(source_file, cstr_sv(&ref->path))) continue;
        program_load_module_ref(me, source_file, ref);
    }

    // Add here so imports come before importing file
    vec_source_file_ptr_push(&my source_files_ordered, source_file);

    return source_file;
}

failable program_load(Program* me, csview path, returns SourceFile*) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);

    if (path_is_js(path) && !my options.allow_js) {
        // Not part of the program. tsc still wants the file to exist, so does the caller
        if (!fs_is_reg_file(my fs, path)) return err_d("File '", path, "' not found.");
        cstr shown = program_path_for_message(me, path);
        Error* error = error_ctx_global_error_d(
            my errs, 6504, "File '", shown, "' is a JavaScript file. Did you mean to enable the 'allowJs' option?"
        );
        error_sub_error_d(error, span_null, TS_CODE_NONE, "The file is in the program because:");
        error_sub_error_d(
            vec_error_back_mut(&error->sub_errors), span_null, TS_CODE_NONE, "Root file specified for compilation"
        );
        cstr_drop(&shown);
        return ok(nullptr);
    }

    set_compiler_options(&my options);

    // Add the source file to the program
    SourceFile* main_source = program_add_source_idempotent(me, path);

    clear_compiler_options();

    // An unreadable entry point isn't a TypeScript-level diagnostic: it has no span to attach to, so
    // error_ctx_error_d() would just drop it. Report it to our caller instead.
    if (!main_source) {
        return err_d("File '", path, "' not found.");
    }

    return ok(main_source);
}

SourceFile* program_find_source(Program const* me, csview path) {
    cstr cwd = cstr_null;
    try (fs_get_cwd(my fs, &cwd)) or_panic;
    cstr canonical_path = relative_path_of(cstr_sv(&cwd), path);
    cstr_drop(&cwd);
    auto found = hmap_box_source_file_get(&my source_files_map, cstr_sv(&canonical_path));
    cstr_drop(&canonical_path);
    return found ? found->second.get : nullptr;
}

failable program_load_stdlib(Program* me, csview name, returns SourceFile*) {
    my loading_stdlib = true;
    cstr path = cstr_d(cstr_size(&my lib_dir) ? cstr_sv(&my lib_dir) : default_lib_dir, "/", name, ".d.ts");
    try (SourceFile* source, program_load(me, cstr_sv(&path), &source)) cleanup {
        my loading_stdlib = false;
        cstr_drop(&path);
    }
    my loading_stdlib = false;
    cstr_drop(&path);
    return ok(source);
}

void program_load_automatic_types(Program* me) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    set_compiler_options(&my options);
    StrList names = module_resolver_automatic_types(program_resolver(me));
    for (isize i = 0; i < names.count; i++) {
        if (program_load_type_reference(me, cstr_sv(&names.items[i]), nullptr, IMPORT_RESOLUTION_DEFAULT)) continue;
        cstr const* name = &names.items[i];
        Error* error = error_ctx_global_error_d(my errs, 2688, "Cannot find type definition file for '", *name, "'.");
        error_sub_error_d(error, span_null, TS_CODE_NONE, "The file is in the program because:");
        error_sub_error_d(
            vec_error_back_mut(&error->sub_errors), span_null, TS_CODE_NONE,
            my options.types_set ? "Entry point of type library '" : "Entry point for implicit type library '",
            *name, my options.types_set ? "' specified in compilerOptions" : "'"
        );
    }
    str_list_drop(&names);
    clear_compiler_options();
}

void program_copy_globals_from(Program* me, Program const* other) {
    binder_set_parent(my binder, other->binder);
}

// tsc's verifyCompilerOptions(), the part that doesn't depend on emit. Diagnostics without a file: with a tsconfig tsc
// puts them on the option there, which we don't
static void program_validate_options(Program* me) {
    CompilerOptions const* o = &my options;
    if (cstr_size(&o->config_dir)) return;
    for (isize i = 0; i < o->removed_options.count; i++) {
        cstr const* name = &o->removed_options.items[i];
        Error* error = error_ctx_global_error_d(
            my errs, 5102, "Option '", *name, "' has been removed. Please remove it from your configuration."
        );
        const char* instead = cstr_equals(name, "out") ? "outFile" :
            cstr_equals(name, "importsNotUsedAsValues") || cstr_equals(name, "preserveValueImports") ?
                "verbatimModuleSyntax" : nullptr;
        if (instead) error_sub_error_d(error, span_null, TS_CODE_NONE, "Use '", instead, "' instead.");
    }
    if (o->target == SCRIPT_TARGET_ES3) {
        error_ctx_global_error_d(
            my errs, 5108, "Option 'target=ES3' has been removed. Please remove it from your configuration."
        );
    }
    if (o->resolve_json_module) {
        if (o->module_resolution == MODULE_RESOLUTION_CLASSIC) {
            error_ctx_global_error_d(
                my errs, 5070,
                "Option '--resolveJsonModule' cannot be specified when 'moduleResolution' is set to 'classic'."
            );
        } else if (o->module == MODULE_KIND_NONE || o->module == MODULE_KIND_SYSTEM || o->module == MODULE_KIND_UMD) {
            error_ctx_global_error_d(
                my errs, 5071,
                "Option '--resolveJsonModule' cannot be specified when 'module' is set to 'none', 'system', or 'umd'."
            );
        }
    }
    bool modern_resolution = o->module_resolution >= MODULE_RESOLUTION_NODE16;
    static const char* const needs_modern[] = { "resolvePackageJsonExports", "resolvePackageJsonImports", "customConditions" };
    bool const given[] = {
        o->is_set[COMPILER_OPTION_resolve_package_json_exports] && o->resolve_package_json_exports,
        o->is_set[COMPILER_OPTION_resolve_package_json_imports] && o->resolve_package_json_imports,
        o->custom_conditions.count > 0,
    };
    for (isize i = 0; i < 3 && !modern_resolution; i++) {
        if (given[i]) error_ctx_global_error_d(
            my errs, 5098, "Option '", needs_modern[i],
            "' can only be used when 'moduleResolution' is set to 'node16', 'nodenext', or 'bundler'."
        );
    }
    bool node_module = o->module >= MODULE_KIND_NODE16 && o->module <= MODULE_KIND_NODENEXT;
    bool node_resolution = o->module_resolution == MODULE_RESOLUTION_NODE16 ||
        o->module_resolution == MODULE_RESOLUTION_NODENEXT;
    bool es_module = o->module >= MODULE_KIND_ES2015 && o->module <= MODULE_KIND_ESNEXT;
    if (o->module_resolution == MODULE_RESOLUTION_BUNDLER && !es_module && o->module != MODULE_KIND_PRESERVE) {
        error_ctx_global_error_d(
            my errs, 5095,
            "Option 'bundler' can only be used when 'module' is set to 'preserve' or to 'es2015' or later."
        );
    }
    if (node_module && !node_resolution) {
        const char* module_name = o->module == MODULE_KIND_NODE16 ? "Node16" :
            o->module == MODULE_KIND_NODE18 ? "Node18" : "NodeNext";
        error_ctx_global_error_d(
            my errs, 5109, "Option 'moduleResolution' must be set to '",
            o->module == MODULE_KIND_NODENEXT ? "NodeNext" : "Node16",
            "' (or left unspecified) when option 'module' is set to '", module_name, "'."
        );
    } else if (node_resolution && !node_module) {
        const char* name = o->module_resolution == MODULE_RESOLUTION_NODE16 ? "Node16" : "NodeNext";
        error_ctx_global_error_d(
            my errs, 5110, "Option 'module' must be set to '", name, "' when option 'moduleResolution' is set to '",
            name, "'."
        );
    }
    // Declaration emit options without declaration emit
    bool declarations = o->declaration || o->composite;
    const char* needs_declarations[3] = {
        o->isolated_declarations ? "isolatedDeclarations" : nullptr,
        cstr_size(&o->declaration_dir) ? "declarationDir" : nullptr,
        o->declaration_map ? "declarationMap" : nullptr,
    };
    for (isize i = 0; i < 3 && !declarations; i++) {
        if (needs_declarations[i]) error_ctx_global_error_d(my errs, 5069, "Option '", needs_declarations[i],
            "' cannot be specified without specifying option 'declaration' or option 'composite'.");
    }
    if (cstr_size(&o->out_file) && !o->emit_declaration_only && o->module_set && o->module != MODULE_KIND_NONE &&
        o->module != MODULE_KIND_AMD && o->module != MODULE_KIND_SYSTEM
    ) {
        error_ctx_global_error_d(my errs, 6082, "Only 'amd' and 'system' modules are supported alongside --outFile.");
    }
    if (o->emit_declaration_only && !declarations) {
        error_ctx_global_error_d(my errs, 5069,
            "Option 'emitDeclarationOnly' cannot be specified without specifying option 'declaration' or option 'composite'.");
    }
    if (o->emit_decorator_metadata && !o->experimental_decorators) {
        error_ctx_global_error_d(
            my errs, 5052,
            "Option 'emitDecoratorMetadata' cannot be specified without specifying option 'experimentalDecorators'."
        );
    }
    // The JSX factories (tsc's verifyCompilerOptions)
    bool automatic = o->jsx == JSX_REACT_JSX || o->jsx == JSX_REACT_JSXDEV;
    const char* jsx_name = o->jsx == JSX_REACT_JSX ? "react-jsx" : o->jsx == JSX_REACT_JSXDEV ? "react-jsxdev" : "react";
    if (cstr_size(&o->jsx_factory)) {
        if (cstr_size(&o->react_namespace)) {
            error_ctx_global_error_d(my errs, 5053, "Option 'reactNamespace' cannot be specified with option 'jsxFactory'.");
        }
        if (automatic) {
            error_ctx_global_error_d(my errs, 5089, "Option 'jsxFactory' cannot be specified when option 'jsx' is '",
                jsx_name, "'.");
        }
        if (!compiler_option_is_entity_name(cstr_sv(&o->jsx_factory))) {
            error_ctx_global_error_d(my errs, 5067, "Invalid value for 'jsxFactory'. '", o->jsx_factory,
                "' is not a valid identifier or qualified-name.");
        }
    } else if (cstr_size(&o->react_namespace) && !compiler_option_is_identifier(cstr_sv(&o->react_namespace))) {
        error_ctx_global_error_d(my errs, 5059, "Invalid value for '--reactNamespace'. '", o->react_namespace,
            "' is not a valid identifier.");
    }
    if (cstr_size(&o->jsx_fragment_factory)) {
        if (!cstr_size(&o->jsx_factory)) {
            error_ctx_global_error_d(my errs, 5052,
                "Option 'jsxFragmentFactory' cannot be specified without specifying option 'jsxFactory'.");
        }
        if (automatic) {
            error_ctx_global_error_d(my errs, 5089, "Option 'jsxFragmentFactory' cannot be specified when option 'jsx' is '",
                jsx_name, "'.");
        }
        if (!compiler_option_is_entity_name(cstr_sv(&o->jsx_fragment_factory))) {
            error_ctx_global_error_d(my errs, 18035, "Invalid value for 'jsxFragmentFactory'. '", o->jsx_fragment_factory,
                "' is not a valid identifier or qualified-name.");
        }
    }
    if (cstr_size(&o->react_namespace) && automatic) {
        error_ctx_global_error_d(my errs, 5089, "Option 'reactNamespace' cannot be specified when option 'jsx' is '",
            jsx_name, "'.");
    }
    if (cstr_size(&o->jsx_import_source) && o->jsx == JSX_REACT) {
        error_ctx_global_error_d(my errs, 5089, "Option 'jsxImportSource' cannot be specified when option 'jsx' is '",
            jsx_name, "'.");
    }
}

// tsc's getOutputExtension and getDeclarationEmitExtensionForPath
static const char* emit_js_extension(Program const* me, csview path) {
    if (csview_ends_with(path, ".json")) return ".json";
    if (my options.jsx == JSX_PRESERVE && (csview_ends_with(path, ".jsx") || csview_ends_with(path, ".tsx"))) return ".jsx";
    if (csview_ends_with(path, ".mts") || csview_ends_with(path, ".mjs")) return ".mjs";
    if (csview_ends_with(path, ".cts") || csview_ends_with(path, ".cjs")) return ".cjs";
    return ".js";
}

static const char* emit_declaration_extension(csview path) {
    if (csview_ends_with(path, ".mts") || csview_ends_with(path, ".mjs")) return ".d.mts";
    if (csview_ends_with(path, ".cts") || csview_ends_with(path, ".cjs")) return ".d.cts";
    if (csview_ends_with(path, ".json")) return ".d.json.ts";
    return ".d.ts";
}

// tsc's changeExtension: the source extensions are replaced, anything else is appended to
static cstr emit_change_extension(csview path, const char* extension) {
    static const char* const known[] = { ".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs", ".json" };
    for (isize i = 0; i < c_countof(known); i++) {
        if (csview_ends_with(path, known[i])) {
            path = csview_slice(path, 0, path.size - (isize)strlen(known[i]));
            break;
        }
    }
    return cstr_d(path, extension);
}

// Where tsc writes `path`'s output (getOwnEmitOutputFilePath): next to it, or at the same place relative to
// `common_dir` (an absolute directory) under `out_dir`
static cstr emit_output_path(Program const* me, csview path, csview out_dir, csview common_dir, const char* extension) {
    if (!out_dir.size) return emit_change_extension(path, extension);
    cstr cwd = cstr_null;
    try (fs_get_cwd(my fs, &cwd)) or_panic;
    cstr absolute = absolute_path_of(cstr_sv(&cwd), path);
    csview rest = cstr_sv(&absolute);
    if (rest.size >= common_dir.size && !memcmp(rest.buf, common_dir.buf, (size_t)common_dir.size)) rest = csview_slice(rest, common_dir.size, rest.size);
    while (rest.size && rest.buf[0] == '/') rest = csview_slice(rest, 1, rest.size);
    cstr joined = cstr_d(out_dir, out_dir.buf[out_dir.size - 1] == '/' ? "" : "/", rest);
    cstr ret = emit_change_extension(cstr_sv(&joined), extension);
    c_drop(cstr, &cwd, &absolute, &joined);
    return ret;
}

// tsc's sourceFileMayBeEmitted
static bool emit_source_may_be_emitted(Program const* me, SourceFile const* source) {
    if (source->is_declaration || source->is_stdlib) return false;
    csview path = cstr_sv(&source->canonical_path);
    if (csview_starts_with(path, "node_modules/") || csview_contains(path, "/node_modules/")) return false;
    if (!source->is_json) return true;
    return cstr_size(&my options.out_file) || cstr_size(&my options.out_dir);
}

// Reports one output path (tsc's verifyEmitFilePath): TS5055 if it is an input file, TS5056 the second time
static void emit_verify_path(Program* me, cstr output, hmap_source_file_ptr* seen) {
    if (program_find_source(me, cstr_sv(&output))) {
        Error* error = error_ctx_global_error_d(
            my errs, 5055, "Cannot write file '", output, "' because it would overwrite input file."
        );
        if (!cstr_size(&my options.config_dir)) {
            error_sub_error_d(error, span_null, TS_CODE_NONE,
                "Adding a tsconfig.json file will help organize projects that contain both TypeScript and JavaScript "
                "files. Learn more at https://aka.ms/tsconfig.");
        }
    }
    cstr cwd = cstr_null;
    try (fs_get_cwd(my fs, &cwd)) or_panic;
    cstr key = relative_path_of(cstr_sv(&cwd), cstr_sv(&output));
    cstr_drop(&cwd);
    if (hmap_source_file_ptr_get(seen, cstr_sv(&key))) {
        error_ctx_global_error_d(
            my errs, 5056, "Cannot write file '", output, "' because it would be overwritten by multiple input files."
        );
        cstr_drop(&key);
    } else {
        hmap_source_file_ptr_insert(seen, key, nullptr);
    }
    cstr_drop(&output);
}

// The part of tsc's verifyCompilerOptions that blames the first module (tsc's firstNonAmbientExternalModuleSourceFile):
// modules need a module kind (TS1148), and one that can be bundled into an outFile (TS6131)
static void program_verify_module_kind(Program* me) {
    CompilerOptions const* o = &my options;
    SourceFile const* first = nullptr;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile const* source = *i;
        if (source->is_module && !source->is_commonjs && !source->is_json && !source->is_declaration && !source->is_stdlib) {
            first = source;
            break;
        }
    }
    // (tsc's indicator of a module by moduleDetection has a span of its own: not reported)
    if (!first || !span_valid(first->module_indicator)) return;
    bool isolated = o->isolated_modules || o->verbatim_module_syntax;
    if (!isolated && o->target < SCRIPT_TARGET_ES2015 && o->module_set && o->module == MODULE_KIND_NONE) {
        error_ctx_error_d(my errs, first->module_indicator, 1148,
            "Cannot use imports, exports, or module augmentations when '--module' is 'none'.");
    }
    if (cstr_size(&o->out_file) && !o->emit_declaration_only && !o->module_set) {
        error_ctx_error_d(my errs, first->module_indicator, 6131,
            "Cannot compile modules using option 'outFile' unless the '--module' flag is 'amd' or 'system'.");
    }
}

// The end of tsc's verifyCompilerOptions: no output file may overwrite an input file or another output file
static void program_verify_emit_paths(Program* me) {
    CompilerOptions const* o = &my options;
    if (o->no_emit || o->suppress_output_path_check) return;
    bool declarations = o->declaration || o->composite;
    hmap_source_file_ptr seen = {0};
    if (cstr_size(&o->out_file)) {
        // One bundle: of the scripts, and of modules too with AMD/System (tsc's getSourceFilesToEmit)
        bool modules = o->emit_declaration_only || o->module == MODULE_KIND_AMD || o->module == MODULE_KIND_SYSTEM;
        bool any = false;
        for_each (i, vec_source_file_ptr, &my source_files_ordered) {
            any |= emit_source_may_be_emitted(me, *i) && (modules || !(*i)->is_module);
        }
        if (any) {
            if (!o->emit_declaration_only) emit_verify_path(me, cstr_clone(o->out_file), &seen);
            if (declarations) {
                emit_verify_path(me, emit_change_extension(cstr_sv(&o->out_file), ".d.ts"), &seen);
            }
        }
        hmap_source_file_ptr_drop(&seen);
        return;
    }
    // tsc's getCommonSourceDirectory, only needed with an outDir or declarationDir
    cstr common_dir = cstr_null;
    if (cstr_size(&o->out_dir) || cstr_size(&o->declaration_dir)) {
        cstr cwd = cstr_null;
        try (fs_get_cwd(my fs, &cwd)) or_panic;
        if (cstr_size(&o->root_dir)) {
            common_dir = absolute_path_of(cstr_sv(&cwd), cstr_sv(&o->root_dir));
        } else if (o->composite && cstr_size(&o->config_dir)) {
            common_dir = cstr_clone(o->config_dir);
        } else {
            bool first = true;
            for_each (i, vec_source_file_ptr, &my source_files_ordered) {
                if (!emit_source_may_be_emitted(me, *i)) continue;
                cstr absolute = absolute_path_of(cstr_sv(&cwd), cstr_sv(&(*i)->canonical_path));
                csview dir = dirname_of(cstr_sv(&absolute));
                if (first) {
                    cstr_assign_sv(&common_dir, dir);
                    first = false;
                } else {
                    // The longest common prefix of whole components
                    csview common = cstr_sv(&common_dir);
                    isize n = 0, last_sep = 0;
                    while (n < common.size && n < dir.size && common.buf[n] == dir.buf[n]) {
                        if (common.buf[n] == '/') last_sep = n;
                        n++;
                    }
                    bool whole = (n == common.size || common.buf[n] == '/') && (n == dir.size || dir.buf[n] == '/');
                    cstr_resize(&common_dir, whole ? n : last_sep ? last_sep : 1, '\0');
                }
                cstr_drop(&absolute);
            }
        }
        cstr_drop(&cwd);
    }
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile const* source = *i;
        if (!emit_source_may_be_emitted(me, source)) continue;
        csview path = source_file_display_path(source);
        if (!o->emit_declaration_only) {
            cstr js = emit_output_path(me, path, cstr_sv(&o->out_dir), cstr_sv(&common_dir), emit_js_extension(me, path));
            // A JSON file that would be written over itself isn't written
            if (source->is_json && program_find_source(me, cstr_sv(&js)) == source) {
                cstr_drop(&js);
            } else {
                emit_verify_path(me, js, &seen);
            }
        }
        if (declarations && !source->is_json) {
            csview dir = cstr_size(&o->declaration_dir) ? cstr_sv(&o->declaration_dir) : cstr_sv(&o->out_dir);
            cstr dts = emit_output_path(me, path, dir, cstr_sv(&common_dir), emit_declaration_extension(path));
            emit_verify_path(me, dts, &seen);
        }
    }
    cstr_drop(&common_dir);
    hmap_source_file_ptr_drop(&seen);
}

// tsc's getDiagnosticsWithPrecedingDirectives: a semantic diagnostic (of `phase`, resolving names or typechecking) that
// a @ts-expect-error/@ts-ignore precedes isn't reported. Starting from the line before the diagnostic's, lines that
// are blank or only a `//` comment are passed over on the way up to the directive. Parse errors stay
static void program_apply_comment_directives(Program* me, ErrorPhase phase) {
    bool any = false;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        if ((*i)->comment_directive_count) {
            any = true;
            break;
        }
    }
    if (!any) return;

    VecError* errors = &my errs->errors;
    intptr_t kept = 0;
    for (intptr_t i = 0; i < vec_error_size(errors); i++) {
        Error* error = vec_error_at_mut(errors, i);
        SourceFile* source = (SourceFile*)error->span.source;
        bool suppressed = false;
        if (error->phase == phase && source && source->comment_directive_count && !source->unchecked) {
            const char* text = source_file_start(source);
            const char* text_end = text + cstr_size(&source->contents);
            for (intptr_t line = (intptr_t)source_file_line_index(source, error->span.sv.buf) - 1; line >= 0; line--) {
                CommentDirective* directive = source_file_comment_directive_at_line(source, (u32)line);
                if (directive) {
                    directive->used = true;
                    suppressed = true;
                    break;
                }
                const char* c = text + source->line_starts[line];
                const char* end = line + 1 < source->line_count ? text + source->line_starts[line + 1] : text_end;
                while (c < end && (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n' || *c == '\v' || *c == '\f')) c++;
                if (c < end && !(c + 1 < end && c[0] == '/' && c[1] == '/')) break;
            }
        }
        if (suppressed) {
            error_drop(error);
        } else {
            if (kept != i) errors->data[kept] = *error;
            kept++;
        }
    }
    errors->size = kept;
}

// TS2578 for every @ts-expect-error nothing was reported under, once all semantic diagnostics are in
// tsc's getErrorSpanForNode of a source file: its first token after trivia
static Span file_first_token(SourceFile const* file) {
    char const* text = source_file_start(file);
    char const* end = text + cstr_size(&file->contents);
    char const* c = text;
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

// A duplicate between two files: the other file (of its "was also declared here"), else null
static SourceFile const* cross_file_duplicate_other(Error const* e) {
    if ((e->ts_code != 2300 && e->ts_code != 2451) || !span_valid(e->span) || !e->span.source) return nullptr;
    for_each (i, vec_error, &e->sub_errors) {
        if (i->ts_code == 6203 && span_valid(i->span) && i->span.source && i->span.source != e->span.source) {
            return i->span.source;
        }
    }
    return nullptr;
}

// tsc's amalgamatedDuplicates: when 8 or more names conflict between two files, their duplicate errors become one
// TS6200 per file listing the names, related to the other file (TS6201)
static void program_amalgamate_duplicates(Program* me) {
    VecError* errors = &my errs->errors;
    for (isize i = 0; i < vec_error_size(errors); i++) {
        Error const* seed = vec_error_at(errors, i);
        SourceFile const* other = cross_file_duplicate_other(seed);
        if (!other) continue;
        SourceFile const* a = seed->span.source;
        // The pair's names, the first file's (by path) in order
        SourceFile const* first = cstr_cmp(&a->canonical_path, &other->canonical_path) < 0 ? a : other;
        SourceFile const* second = first == a ? other : a;
        cstr names = cstr_null;
        isize name_count = 0;
        for (int side = 0; side < 2; side++) {
            for_each (e, vec_error, errors) {
                SourceFile const* o = cross_file_duplicate_other(e);
                if (!o || e->span.source != (side ? second : first) || o != (side ? first : second)) continue;
                csview name = e->span.sv;
                bool seen = false;
                for (isize at = 0; at < cstr_size(&names) && !seen;) {
                    isize next = cstr_find_at(&names, at, ", ");
                    if (next == c_NPOS) next = cstr_size(&names);
                    seen = csview_equals_sv(csview_slice(cstr_sv(&names), at, next), name);
                    at = next + 2;
                }
                if (seen) continue;
                if (name_count++) cstr_append(&names, ", ");
                cstr_append_sv(&names, name);
            }
        }
        if (name_count < 8) {
            cstr_drop(&names);
            continue;
        }
        for (isize j = vec_error_size(errors) - 1; j >= 0; j--) {
            Error const* e = vec_error_at(errors, j);
            SourceFile const* o = cross_file_duplicate_other(e);
            if (o && ((e->span.source == first && o == second) || (e->span.source == second && o == first))) {
                vec_error_erase_n(errors, j, 1); // (drops it)
            }
        }
        for (int side = 0; side < 2; side++) {
            SourceFile const* here = side ? second : first;
            SourceFile const* there = side ? first : second;
            Error error = {
                .message = cstr_d("Definitions of the following identifiers conflict with those in another file: ", names),
                .span = file_first_token(here), .ts_code = 6200, .phase = ERROR_PHASE_TYPECHECK,
            };
            error_add_related_d(&error, file_first_token(there), 6201, "Conflicts are in this file.");
            vec_error_push(errors, error);
        }
        cstr_drop(&names);
        i = -1; // (the vector changed: start over, the pair's errors are gone)
    }
}

static void program_report_unused_expect_errors(Program* me) {
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        if (source->unchecked || source->is_stdlib) continue;
        for (u32 j = 0; j < source->comment_directive_count; j++) {
            CommentDirective const* directive = &source->comment_directives[j];
            // Of two directives on one line only the last one counts
            bool shadowed = j + 1 < source->comment_directive_count &&
                source->comment_directives[j + 1].line == directive->line;
            if (directive->expect_error && !directive->used && !shadowed) {
                error_ctx_error_d(
                    my errs, span_make(source, directive->start, directive->end), 2578,
                    "Unused '@ts-expect-error' directive."
                );
            }
        }
    }
}

// tsc's addUndefinedToGlobalsOrErrorOnRedeclaration and the `globalThis` check of initializeTypeChecker: a global value
// can't be named `undefined`, a script can't declare `globalThis` (TS2397)
static void program_check_builtin_global_names(Program* me) {
    static char const* const names[] = { "undefined", "globalThis" };
    for (u32 n = 0; n < c_arraylen(names); n++) {
        Symbol const* symbol = symbol_table_get(&my binder->globals, csview_from(names[n]));
        for (u32 i = 0; symbol && i < symbol->decl_count; i++) {
            SymbolDecl const* decl = &symbol->decls[i];
            SourceFile const* file = decl->name.source;
            if (!file || file->is_stdlib || !span_valid(decl->name)) continue;
            // (`declare global { namespace globalThis {} }` in a module augments it)
            if (n == 1 && file->is_module) continue;
            // (Type declarations: checkTypeNameIsReserved's business)
            if (decl->kind != SYMBOL_DECL_VARIABLE && decl->kind != SYMBOL_DECL_FUNCTION &&
                decl->kind != SYMBOL_DECL_NAMESPACE) {
                continue;
            }
            error_ctx_error_d(my errs, decl->name, 2397, "Declaration name conflicts with built-in global identifier '",
                names[n], "'.");
        }
    }
}

void program_resolve_names(Program* me) {
    CHANGE_PHASE(PROGRAM_PHASE_LOADING, PROGRAM_PHASE_RESOLVING_NAMES);
    program_validate_options(me);
    program_verify_module_kind(me);
    program_verify_emit_paths(me);
    my errs->phase = ERROR_PHASE_RESOLVE_NAMES;
    set_compiler_options(&my options);

    // Declare: symbols, the scopes of files and namespaces, exports
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        source->scope = scope_new_file(my binder, source);
    }
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        block_add_names(&source->ast, source->scope);
        scope_add_commonjs_module(source->scope);
        scope_check_export_merges(source->scope, block_span(&source->ast));
        if (source->is_module && !source->is_js) scope_check_duplicate_exports(source->scope);
    }
    binder_link_global_expandos(my binder);
    program_check_builtin_global_names(me);
    binder_check_ambient_module_exports(my binder, my errs);

    if (my options.no_resolve) {
        for_each (i, vec_source_file_ptr, &my source_files_ordered) {
            SourceFile* source = *i;
            for (u32 j = 0; j < source->module_ref_count; j++) {
                ModuleRef* ref = source->module_refs[j];
                if ((!ref->is_declaration || source->is_module) && !ref->ambient_only) program_load_module_ref(me, source, ref);
            }
        }
    }

    // Link: what every module name refers to (ambient modules are all declared now), `export *` tables and every alias. Nothing writes to a symbol after this
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        for (u32 j = 0; j < source->module_ref_count; j++) {
            binder_module_of_ref(my binder, source->module_refs[j]);
        }
    }
    binder_link(my binder);
    binder_check_export_star_conflicts(my binder, my errs);

    // Resolve: every identifier gets its symbol, function and block scopes are made on the way
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        block_lookup_names_container(&source->ast, false, c_sv("file"), span_null, source->scope, my errs);
        if (!source->is_stdlib && !source->unchecked && !source->is_declaration) unused_mark_jsdoc_links(source);
    }

    binder_resolve_remaining_aliases(my binder);

    error_ctx_remove_unchecked(my errs);
    program_apply_comment_directives(me, ERROR_PHASE_RESOLVE_NAMES);

    clear_compiler_options();
    my errs->phase = ERROR_PHASE_UNKOWN;
    CHANGE_PHASE(PROGRAM_PHASE_RESOLVING_NAMES, PROGRAM_PHASE_NAMES_RESOLVED);
}

void program_make_current(Program* me) {
    panic_if(my phase < PROGRAM_PHASE_NAMES_RESOLVED, "program needs its names resolved before types can be evaluated");
    if (!my checker) my checker = checker_new(me);
    checker_make_current(my checker);
}

void program_set_checkers(Program* me, u32 count) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    my requested_checkers = count;
}

void program_set_threads(Program* me, u32 count) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
    my threads = count;
}

Binder const* program_get_binder(Program const* me) {
    return my binder;
}

ErrorCtx* program_errors(Program const* me) {
    return my errs;
}

Checker* program_get_checker(Program const* me) {
    return my checker;
}

u32 program_checker_count(Program const* me) {
    return my checker_count;
}

Checker* program_checker_at(Program const* me, u32 index) {
    return my checkers[index];
}

Symbol const* program_get_global(Program const* me, csview name, u32 meaning) {
    return symbol_for_meaning(symbol_table_get(&my binder->globals, name), meaning);
}

// The global types tsc's initializeTypeChecker looks up with reportErrors (missing with noLib or a partial lib)
static void program_report_missing_global_types(Program* me) {
    static char const* const names[] = {
        "IArguments", "Array", "Object", "Function", "CallableFunction", "NewableFunction", "String", "Number", "Boolean",
        "RegExp",
    };
    for (u32 i = 0; i < c_arraylen(names); i++) {
        bool bind_call_apply = !strcmp(names[i], "CallableFunction") || !strcmp(names[i], "NewableFunction");
        if (bind_call_apply && !my options.strict_bind_call_apply) continue;
        if (program_get_global(me, csview_from(names[i]), SYMBOL_TYPE)) continue;
        error_ctx_global_error_d(my errs, 2318, "Cannot find global type '", names[i], "'.");
    }
}

// The checkers of program_typecheck(), taken by the threads in index order. Which files a checker checks depends only
// on the file list and the checker count, never on which thread runs it
typedef struct CheckShares {
    Program* program;
    u32 count;
    atomic_uint next;
} CheckShares;

static void check_share(Program* me, u32 c, u32 count) {
    struct timespec start, end;
    timespec_get(&start, TIME_UTC);
    Checker* checker = my checkers[c];
    checker_make_current(checker);
    ty_init_global_types(checker);
    u32 index = 0;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        if (source->is_stdlib || source->unchecked || index++ % count != c) continue;
        checker->file_order = index - 1;
        if (source->is_declaration) block_check_required_declare(&source->ast, checker->errs);
        block_typecheck(&source->ast, (BlockTypecheckCtx){ .ambient = source->is_declaration }, checker->errs);
    }
    // After its files are checked: the checker marks members it reads
    index = 0;
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        if (source->is_stdlib || source->unchecked || index++ % count != c) continue;
        if (!source->is_declaration) unused_report(source, checker->errs);
    }
    checker_make_current(nullptr);
    timespec_get(&end, TIME_UTC);
    checker->check_ms = (double)(end.tv_sec - start.tv_sec) * 1e3 + (double)(end.tv_nsec - start.tv_nsec) / 1e6;
}

static void check_shares_run(CheckShares* me) {
    for (u32 c; (c = atomic_fetch_add(&my next, 1)) < my count;) check_share(my program, c, my count);
}

static void* check_shares_thread(void* shares) {
    check_shares_run(shares);
    return nullptr;
}

void program_typecheck(Program* me) {
    CHANGE_PHASE(PROGRAM_PHASE_NAMES_RESOLVED, PROGRAM_PHASE_TYPECHECKING);
    my errs->phase = ERROR_PHASE_TYPECHECK;
    set_compiler_options(&my options);
    program_make_current(me);

    scope_report_missing_prefixes(my binder);
    program_report_missing_global_types(me);
    // The files are dealt to the checkers round robin: which checker checks a file depends only on the file list, so
    // the output depends only on the checker count. Each reports into its own ErrorCtx; nothing may write to the
    // program's or make symbols meanwhile (they run on threads of their own)
    u32 count = my requested_checkers ? my requested_checkers : 1;
    my checkers = c_new_n(Checker*, count);
    my checker_count = count;
    ErrorCtx* checker_errs = c_new_n(ErrorCtx, count);
    my binder->frozen = true;
    my errs->frozen = true;
    for (u32 c = 0; c < count; c++) {
        checker_errs[c] = (ErrorCtx){ .phase = ERROR_PHASE_TYPECHECK, .parent = my errs };
        my checkers[c] = c ? checker_new(me) : my checker;
        my checkers[c]->errs = &checker_errs[c];
    }
    CheckShares shares = { .program = me, .count = count };
    u32 threads = my threads ? my threads : 1;
    if (threads > count) threads = count;
    if (threads <= 1) {
        check_shares_run(&shares);
    } else {
        // The main thread works too. Workers get the main thread's stack size: the checker recurses per nesting level
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        struct rlimit limit;
        size_t stack = 8u << 20;
        if (!getrlimit(RLIMIT_STACK, &limit) && limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur > stack) stack = limit.rlim_cur;
        if (stack > (256u << 20)) stack = 256u << 20;
        pthread_attr_setstacksize(&attr, stack);
        pthread_t* workers = c_new_n(pthread_t, threads - 1);
        for (u32 t = 0; t < threads - 1; t++) {
            panic_if(pthread_create(&workers[t], &attr, check_shares_thread, &shares), "couldn't start a checker thread");
        }
        check_shares_run(&shares);
        for (u32 t = 0; t < threads - 1; t++) pthread_join(workers[t], nullptr);
        free(workers);
        pthread_attr_destroy(&attr);
    }
    my binder->frozen = false;
    my errs->frozen = false;
    error_ctx_keep_first_once(checker_errs, count);
    for (u32 c = 0; c < count; c++) {
        error_ctx_merge(my errs, &checker_errs[c]);
        error_ctx_drop(&checker_errs[c]);
        my checkers[c]->errs = my errs;
    }
    free(checker_errs);
    checker_make_current(my checker);

    error_ctx_remove_unchecked(my errs);
    program_apply_comment_directives(me, ERROR_PHASE_TYPECHECK);
    program_report_unused_expect_errors(me);
    program_amalgamate_duplicates(me);
    error_ctx_sort_and_dedupe(my errs);

    clear_compiler_options();
    my errs->phase = ERROR_PHASE_UNKOWN;
    CHANGE_PHASE(PROGRAM_PHASE_TYPECHECKING, PROGRAM_PHASE_TYPECHECKED);
}

intptr_t program_get_file_count(Program const* me) {
    return hmap_box_source_file_size(&my source_files_map);
}
