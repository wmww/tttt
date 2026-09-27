#include "program.h"
#include "unused.h"
#include "import_attributes.h"
#include "parser.h"
#include "block.h"
#include <cleat/filesystem.h>
#include "error.h"
#include "statement.h"
#include "port_statement.h"
#include "source_file.h"
#include "ty/ty.h"
#include "module_resolution.h"

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
    cstr lib_dir; // cstr_null: default_lib_dir
    ErrorCtx* errs; // unowned
    Fs const* fs;
    Binder* binder;
    ModuleResolver* resolver; // Made on first use, the options are final by then
    HMapBoxSourceFile source_files_map;
    VecSourceFilePtr source_files_ordered;
    HMapSourceFilePtr files_by_package_id; // ResolvedModule.package_id -> the first file found with it
    Checker* checker; // Created by program_make_current()
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
    if (ref->resolution_override == IMPORT_RESOLUTION_IMPORT) mode = RESOLUTION_MODE_ESM;
    if (ref->resolution_override == IMPORT_RESOLUTION_REQUIRE) mode = RESOLUTION_MODE_REQUIRE_SYNTAX;
    ResolvedModule resolved = module_resolver_resolve(resolver, cstr_sv(&ref->path), containing, mode);
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

static SourceFile* program_load_type_reference(Program* me, csview name, csview containing_file) {
    ResolvedModule resolved = module_resolver_resolve_type_reference(program_resolver(me), name, containing_file);
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
                        me, cstr_sv(&statement->source_directive->path), cstr_sv(&source->canonical_path))
                    ) {
                        cstr_assign_sv(&statement->source_directive->not_found, cstr_sv(&statement->source_directive->path));
                    }
                    break;
                case SOURCE_DIRECTIVE_REFERENCE_LIB:
                    // Between lib files: they are all in one directory. TODO: a lib only a directive in the program
                    // names (the harness keeps libs in a shared program, which a test's file can't add to)
                    if (my loading_stdlib) {
                        cstr name = cstr_d(statement->source_directive->path, ".d.ts");
                        cstr_lowercase(&name);
                        program_load_reference_path(me, statement->source_directive->path_span, cstr_sv(&name));
                        cstr_drop(&name);
                    }
                    break;
            }
            break;

        case STATEMENT_IMPORT_EXPORT: {
            PortStatement* port = statement->import_export;
            if (port->main.tag != PORT_IMPORT_ALIAS || port->main.export_import) {
                source->is_module = true;
            }
        }   break;

        case STATEMENT_EXPORT_DEFAULT_EXPR:
            source->is_module = true;
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
    // (A JSON file has no module format of its own: tsc's getImpliedNodeFormatForFile)
    if (node_formats && !my loading_stdlib && !source_file->is_json) {
        FileFormat format = module_resolver_file_format(program_resolver(me), cstr_sv(&source_file->canonical_path));
        source_file->is_esm = format.mode == RESOLUTION_MODE_ESM;
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
        if (program_load_type_reference(me, cstr_sv(&names.items[i]), c_sv(""))) continue;
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

void program_resolve_names(Program* me) {
    CHANGE_PHASE(PROGRAM_PHASE_LOADING, PROGRAM_PHASE_RESOLVING_NAMES);
    program_validate_options(me);
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

    // Resolve: every identifier gets its symbol, function and block scopes are made on the way
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        block_lookup_names_container(&source->ast, false, c_sv("file"), span_null, source->scope, my errs);
    }

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

Binder const* program_get_binder(Program const* me) {
    return my binder;
}

Checker* program_get_checker(Program const* me) {
    return my checker;
}

Symbol const* program_get_global(Program const* me, csview name, u32 meaning) {
    return symbol_for_meaning(symbol_table_get(&my binder->globals, name), meaning);
}

void program_typecheck(Program* me) {
    CHANGE_PHASE(PROGRAM_PHASE_NAMES_RESOLVED, PROGRAM_PHASE_TYPECHECKING);
    my errs->phase = ERROR_PHASE_TYPECHECK;
    set_compiler_options(&my options);
    program_make_current(me);

    scope_report_missing_prefixes(my binder);
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        if (!source->is_stdlib && !source->unchecked) {
            block_typecheck(&source->ast, (BlockTypecheckCtx){ .ambient = source->is_declaration }, my errs);
        }
    }
    // After everything is checked: the checker marks members it reads
    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        if (!source->is_stdlib && !source->unchecked && !source->is_declaration) unused_report(source, my errs);
    }
    error_ctx_remove_unchecked(my errs);
    program_apply_comment_directives(me, ERROR_PHASE_TYPECHECK);
    program_report_unused_expect_errors(me);

    clear_compiler_options();
    my errs->phase = ERROR_PHASE_UNKOWN;
    CHANGE_PHASE(PROGRAM_PHASE_TYPECHECKING, PROGRAM_PHASE_TYPECHECKED);
}

intptr_t program_get_file_count(Program const* me) {
    return hmap_box_source_file_size(&my source_files_map);
}
