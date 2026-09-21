#include "program.h"
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

static const csview lib_path = c_sv_const_init("thirdparty/lib");

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
    ErrorCtx* errs; // unowned
    Fs const* fs;
    Binder* binder;
    ModuleResolver* resolver; // Made on first use, the options are final by then
    HMapBoxSourceFile source_files_map;
    VecSourceFilePtr source_files_ordered;
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
    vec_source_file_ptr_drop(&my source_files_ordered);
    module_resolver_destroy(my resolver);
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

// A resolved TypeScript file becomes part of the program, anything else is remembered on the ref as why there's no file
static void program_load_module_ref(Program* me, SourceFile* source, ModuleRef* ref) {
    ModuleResolver* resolver = program_resolver(me);
    csview containing = cstr_sv(&source->canonical_path);
    ResolutionMode mode = RESOLUTION_MODE_COMMONJS;
    if (my options.module_resolution == MODULE_RESOLUTION_NODE16 ||
        my options.module_resolution == MODULE_RESOLUTION_NODENEXT
    ) {
        if (!source->resolution_mode_known) {
            source->is_esm = module_resolver_file_mode(resolver, containing) == RESOLUTION_MODE_ESM;
            source->resolution_mode_known = true;
        }
        mode = ref->syntax == MODULE_REF_REQUIRE ? RESOLUTION_MODE_COMMONJS :
            ref->syntax == MODULE_REF_DYNAMIC || source->is_esm ? RESOLUTION_MODE_ESM : RESOLUTION_MODE_COMMONJS;
    }
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
            if (resolved.arbitrary_extension && !my options.allow_arbitrary_extensions) {
                ref->failure = MODULE_REF_ARBITRARY_EXTENSION;
                cstr_take(&ref->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
            } else {
                ref->file = program_add_source_idempotent(me, cstr_sv(&resolved.path));
            }
            break;
        case RESOLVED_JS:
            // JavaScript of a package isn't part of the program even with allowJs (tsc's maxNodeModuleJsDepth is 0)
            if (my options.allow_js && !resolved.is_external_library) {
                ref->file = program_add_source_idempotent(me, cstr_sv(&resolved.path));
            } else {
                ref->failure = MODULE_REF_IS_JS;
                cstr_take(&ref->resolved_path, program_path_for_message(me, cstr_sv(&resolved.path)));
            }
            break;
        case RESOLVED_JSON:
            ref->failure = MODULE_REF_IS_JSON;
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
                    program_load_reference_path(
                        me,
                        statement->source_directive->path_span,
                        cstr_sv(&statement->source_directive->path)
                    );
                    break;
                case SOURCE_DIRECTIVE_REFERENCE_TYPES:
                    program_load_type_reference(
                        me, cstr_sv(&statement->source_directive->path), cstr_sv(&source->canonical_path));
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
    }

    if (my loading_stdlib) {
        source_file->is_stdlib = true;
        csview filename = basename_of(cstr_sv(&source_file->canonical_path));
        cstr_take(&source_file->decl_override, cstr_d("lib.", filename, ", --, --"));
    }

    parse_file(source_file, my errs);

    hmap_box_source_file_insert(&my source_files_map, canonical_path, box_source_file_from_ptr(source_file));

    for_each (i, vec_statement, &source_file->ast.statements) {
        program_handle_statement(me, source_file, i);
    }

    // Every module the file mentions, wherever (the parser lists them). By index: loading parses, never this file
    for (u32 i = 0; i < source_file->module_ref_count; i++) {
        ModuleRef* ref = source_file->module_refs[i];
        // `declare module "./a" {}` in a module augments a file. In a script it declares an ambient module, and so
        // does a name that finds no file
        if (ref->is_declaration && !source_file->is_module) continue;
        if (my options.no_resolve) continue; // program_resolve_names(), once every root is loaded
        program_load_module_ref(me, source_file, ref);
    }

    // Add here so imports come before importing file
    vec_source_file_ptr_push(&my source_files_ordered, source_file);

    return source_file;
}

failable program_load(Program* me, csview path, returns SourceFile*) {
    ASSERT_PHASE(PROGRAM_PHASE_LOADING);
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
    cstr path = cstr_d(lib_path, "/", name, ".d.ts");
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
        // TODO: TS2688 for an entry of `types` that isn't found (a diagnostic without a file)
        ignore program_load_type_reference(me, cstr_sv(&names.items[i]), c_sv(""));
    }
    str_list_drop(&names);
    clear_compiler_options();
}

void program_copy_globals_from(Program* me, Program const* other) {
    binder_set_parent(my binder, other->binder);
}

void program_resolve_names(Program* me) {
    CHANGE_PHASE(PROGRAM_PHASE_LOADING, PROGRAM_PHASE_RESOLVING_NAMES);
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
    }

    if (my options.no_resolve) {
        for_each (i, vec_source_file_ptr, &my source_files_ordered) {
            SourceFile* source = *i;
            for (u32 j = 0; j < source->module_ref_count; j++) {
                ModuleRef* ref = source->module_refs[j];
                if (!ref->is_declaration || source->is_module) program_load_module_ref(me, source, ref);
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

    for_each (i, vec_source_file_ptr, &my source_files_ordered) {
        SourceFile* source = *i;
        if (!source->is_stdlib) {
            block_typecheck(&source->ast, (BlockTypecheckCtx){0}, my errs);
        }
    }

    clear_compiler_options();
    my errs->phase = ERROR_PHASE_UNKOWN;
    CHANGE_PHASE(PROGRAM_PHASE_TYPECHECKING, PROGRAM_PHASE_TYPECHECKED);
}

intptr_t program_get_file_count(Program const* me) {
    return hmap_box_source_file_size(&my source_files_map);
}
