#pragma once

#include "prelude.h"

// Values are ordered, so `target >= SCRIPT_TARGET_ES2015` works
typedef enum ScriptTarget {
    SCRIPT_TARGET_ES5,
    SCRIPT_TARGET_ES2015,
    SCRIPT_TARGET_ES2016,
    SCRIPT_TARGET_ES2017,
    SCRIPT_TARGET_ES2018,
    SCRIPT_TARGET_ES2019,
    SCRIPT_TARGET_ES2020,
    SCRIPT_TARGET_ES2021,
    SCRIPT_TARGET_ES2022,
    SCRIPT_TARGET_ES2023,
    SCRIPT_TARGET_ES2024,
    SCRIPT_TARGET_ES2025,
    SCRIPT_TARGET_ES2026,
    SCRIPT_TARGET_ESNEXT,
} ScriptTarget;

typedef enum ModuleKind {
    MODULE_KIND_NONE,
    MODULE_KIND_COMMONJS,
    MODULE_KIND_AMD,
    MODULE_KIND_UMD,
    MODULE_KIND_SYSTEM,
    MODULE_KIND_ES2015,
    MODULE_KIND_ES2020,
    MODULE_KIND_ES2022,
    MODULE_KIND_ESNEXT,
    MODULE_KIND_NODE16,
    MODULE_KIND_NODE18,
    MODULE_KIND_NODE20,
    MODULE_KIND_NODENEXT,
    MODULE_KIND_PRESERVE,
} ModuleKind;

typedef enum ModuleResolutionKind {
    MODULE_RESOLUTION_NONE,
    MODULE_RESOLUTION_CLASSIC,
    MODULE_RESOLUTION_NODE10,
    MODULE_RESOLUTION_NODE16,
    MODULE_RESOLUTION_NODENEXT,
    MODULE_RESOLUTION_BUNDLER,
} ModuleResolutionKind;

typedef enum JsxKind {
    JSX_NONE,
    JSX_PRESERVE,
    JSX_REACT_NATIVE,
    JSX_REACT,
    JSX_REACT_JSX,
    JSX_REACT_JSXDEV,
} JsxKind;

typedef enum ModuleDetectionKind {
    MODULE_DETECTION_AUTO,
    MODULE_DETECTION_LEGACY,
    MODULE_DETECTION_FORCE,
} ModuleDetectionKind;

// The boolean options. X(field, "lowercasename", in_strict_family). Options in the strict family default to the value
// of `strict`, the rest default to false unless compiler_options_resolve() says otherwise. TypeScript 7 removed
// alwaysStrict, esModuleInterop and allowSyntheticDefaultImports `false` and downlevelIteration (TS5108/TS5102, which
// stop checking): nothing but program_validate_options() reads those, the checker behaves as if they were true. Same
// for target ES5 and module AMD/UMD/System, which parse to be reported
#define COMPILER_BOOL_OPTIONS(X) \
    X(strict, "strict", false) \
    X(always_strict, "alwaysstrict", false) \
    X(no_implicit_any, "noimplicitany", true) \
    X(no_implicit_this, "noimplicitthis", true) \
    X(strict_bind_call_apply, "strictbindcallapply", true) \
    X(strict_builtin_iterator_return, "strictbuiltiniteratorreturn", true) \
    X(strict_function_types, "strictfunctiontypes", true) \
    X(strict_null_checks, "strictnullchecks", true) \
    X(strict_property_initialization, "strictpropertyinitialization", true) \
    X(use_unknown_in_catch_variables, "useunknownincatchvariables", true) \
    X(allow_js, "allowjs", false) \
    X(check_js, "checkjs", false) \
    X(allow_unreachable_code, "allowunreachablecode", false) \
    X(allow_unused_labels, "allowunusedlabels", false) \
    X(allow_synthetic_default_imports, "allowsyntheticdefaultimports", false) \
    X(allow_umd_global_access, "allowumdglobalaccess", false) \
    X(allow_importing_ts_extensions, "allowimportingtsextensions", false) \
    X(allow_arbitrary_extensions, "allowarbitraryextensions", false) \
    X(composite, "composite", false) \
    X(declaration, "declaration", false) \
    X(declaration_map, "declarationmap", false) \
    X(deduplicate_packages, "deduplicatepackages", false) \
    X(downlevel_iteration, "downleveliteration", false) \
    X(emit_declaration_only, "emitdeclarationonly", false) \
    X(emit_decorator_metadata, "emitdecoratormetadata", false) \
    X(erasable_syntax_only, "erasablesyntaxonly", false) \
    X(es_module_interop, "esmoduleinterop", false) \
    X(exact_optional_property_types, "exactoptionalpropertytypes", false) \
    X(experimental_decorators, "experimentaldecorators", false) \
    X(import_helpers, "importhelpers", false) \
    X(incremental, "incremental", false) \
    X(inline_source_map, "inlinesourcemap", false) \
    X(inline_sources, "inlinesources", false) \
    X(isolated_declarations, "isolateddeclarations", false) \
    X(isolated_modules, "isolatedmodules", false) \
    X(keyof_strings_only, "keyofstringsonly", false) \
    X(lib_replacement, "libreplacement", false) \
    X(no_check, "nocheck", false) \
    X(no_emit, "noemit", false) \
    X(no_emit_on_error, "noemitonerror", false) \
    X(no_error_truncation, "noerrortruncation", false) \
    X(no_fallthrough_cases_in_switch, "nofallthroughcasesinswitch", false) \
    X(no_implicit_override, "noimplicitoverride", false) \
    X(no_implicit_returns, "noimplicitreturns", false) \
    X(no_lib, "nolib", false) \
    X(no_property_access_from_index_signature, "nopropertyaccessfromindexsignature", false) \
    X(no_resolve, "noresolve", false) \
    X(no_strict_generic_checks, "nostrictgenericchecks", false) \
    X(no_unchecked_indexed_access, "nouncheckedindexedaccess", false) \
    X(no_unchecked_side_effect_imports, "nouncheckedsideeffectimports", false) \
    X(no_unused_locals, "nounusedlocals", false) \
    X(no_unused_parameters, "nounusedparameters", false) \
    X(preserve_const_enums, "preserveconstenums", false) \
    X(pretty, "pretty", false) \
    X(preserve_symlinks, "preservesymlinks", false) \
    X(resolve_json_module, "resolvejsonmodule", false) \
    X(resolve_package_json_exports, "resolvepackagejsonexports", false) \
    X(resolve_package_json_imports, "resolvepackagejsonimports", false) \
    X(rewrite_relative_import_extensions, "rewriterelativeimportextensions", false) \
    /* (Command line only: content mappers may run. tttt runs no external code, so this only decides TS18068) */ \
    X(emit_bom, "emitbom", false) \
    X(remove_comments, "removecomments", false) \
    X(run_external_code, "runexternalcode", false) \
    X(skip_default_lib_check, "skipdefaultlibcheck", false) \
    X(source_map, "sourcemap", false) \
    X(stable_type_ordering, "stabletypeordering", false) \
    X(skip_lib_check, "skiplibcheck", false) \
    X(strip_internal, "stripinternal", false) \
    X(suppress_excess_property_errors, "suppressexcesspropertyerrors", false) \
    X(suppress_implicit_any_index_errors, "suppressimplicitanyindexerrors", false) \
    X(suppress_output_path_check, "suppressoutputpathcheck", false) \
    X(use_define_for_class_fields, "usedefineforclassfields", false) \
    X(verbatim_module_syntax, "verbatimmodulesyntax", false)

// A list of strings, as the list options are written ("a,b" on the command line, an array in a tsconfig)
typedef struct StrList {
    cstr* items;
    isize count;
    isize cap;
} StrList;

void str_list_push(StrList* me, csview item);
void str_list_clear(StrList* me);
void str_list_drop(StrList* me);

// One entry of `paths`: "@app/*": ["src/*", "generated/*"]
typedef struct PathMapping {
    cstr pattern;
    StrList substitutions;
} PathMapping;

typedef enum CompilerOptionId {
#define X(field, name, strict_family) COMPILER_OPTION_##field,
    COMPILER_BOOL_OPTIONS(X)
#undef X
    COMPILER_BOOL_OPTION_COUNT,
} CompilerOptionId;

// What tsc calls CompilerOptions. Every field holds the *effective* value (defaults that depend on other options are
// already applied), as long as compiler_options_resolve() ran after the last compiler_options_set(). One per Program,
// read during parsing and checking through compiler_options()
typedef struct CompilerOptions {
#define X(field, name, strict_family) bool field;
    COMPILER_BOOL_OPTIONS(X)
#undef X
    ScriptTarget target;
    ModuleKind module;
    ModuleResolutionKind module_resolution; // Never classic or node10 (removed in TypeScript 7)
    ModuleResolutionKind module_resolution_removed; // `moduleResolution: classic|node10|node` as given (TS5108)
    bool capture_suggestions; // The test harness's `@captureSuggestions`: suggestion diagnostics are reported too
    // The harness's `@useCaseSensitiveFileNames: false` (tsc's host setting): only compares file names' roots (TS5009)
    bool case_insensitive_file_names;
    JsxKind jsx;
    ModuleDetectionKind module_detection;
    uint8_t new_line; // `newLine`: 1 crlf, 2 lf, 0 not set (lf; tsc's harness: crlf). What emitted files end lines with
    cstr lib; // Comma separated, lowercase, as written ("es2015,dom"). Empty: the default library of the target
    bool lib_set; // `lib` was given: empty means none at all (`"lib": []`, tsc's options.lib of no names)
    // As written, empty if not given
    cstr jsx_factory; // `h`, `React.createElement`
    cstr jsx_fragment_factory;
    cstr jsx_import_source;
    cstr react_namespace;
    cstr map_root; // Emit options only validated (verifyCompilerOptions), as written
    cstr source_root;
    cstr ts_build_info_file;
    // Emit paths, only for tsc's output path checks (TS5055/TS5056): tttt doesn't emit. Relative to config_dir like
    // the module resolution ones below
    cstr out_dir;
    cstr out_file; // Removed in TypeScript 7 (TS5102): only reported, and by tsc's TS5011
    cstr root_dir;
    cstr declaration_dir;

    // Module resolution (src/module_resolution.h reads these). Paths are as written: relative ones are relative to
    // config_dir (the directory of the tsconfig they came from), or to the current directory when that is empty
    cstr config_dir;
    cstr base_url; // Removed in TypeScript 7 (TS5102): only reported
    cstr paths_base; // What `paths` substitutions are relative to: the tsconfig that has the `paths`
    PathMapping* paths;
    isize path_count;
    StrList root_dirs;
    StrList type_roots;
    StrList types;
    StrList module_suffixes;
    StrList custom_conditions;
    // tsc's configFileSpecs, for why a root file is in the program (tsconfig_root_file_reason()): the config's `files`
    // (absolute), its `include` specs as written and as absolute patterns, and config_default_include: neither given
    StrList config_files, config_include_specs, config_include_patterns;
    bool config_default_include;
    // The `include` and `exclude` specs as TS18003 shows them: an extended config's rebased onto the config's directory
    // (tsc's `../e2e` for an `e2e` one directory up); config_exclude_given: there is an `exclude`, config_files_given:
    // a `files` (an extended config's too: tsc's raw has it, so no TS18003)
    StrList config_include_shown, config_exclude_shown;
    bool config_exclude_given, config_files_given;
    bool base_url_set, paths_set, type_roots_set, types_set;
    // The config's `contentMappers` (content_mapper.h), only with runExternalCode and when valid. Owned
    struct ContentMapper* content_mappers;
    u32 content_mapper_count;
    // How deep into packages the program follows JavaScript imports (tsc's maxNodeModuleJsDepth, default 0)
    u32 max_node_module_js_depth;

    // Which options were given explicitly, the rest get their defaults from compiler_options_resolve()
    bool is_set[COMPILER_BOOL_OPTION_COUNT];
    bool target_set, module_set, module_resolution_set, module_detection_set;
} CompilerOptions;

typedef enum CompilerOptionSetResult {
    COMPILER_OPTION_OK,
    COMPILER_OPTION_UNKNOWN,   // Not an option we do anything with (which includes most of tsc's emit options)
    COMPILER_OPTION_BAD_VALUE,
} CompilerOptionSetResult;

CompilerOptions compiler_options_init();
void compiler_options_drop(CompilerOptions* me);
// name is case insensitive ("strictNullChecks"), value is what would be written in a tsconfig or on the command line
CompilerOptionSetResult compiler_options_set(CompilerOptions* me, csview name, csview value);
// A name `lib` takes (case insensitive), and TS6046's message listing them
bool compiler_options_lib_name_valid(csview name);
cstr compiler_options_lib_names_message(void);
// The file a lib name means (lib.NAME.d.ts), lowercase or not: `esnext.bigint` is es2020.bigint
csview compiler_options_lib_file_name(csview name);
// One entry of `paths` (there is no way to write them as a single string). Replaces an entry with the same pattern
void compiler_options_add_path(CompilerOptions* me, csview pattern, StrList substitutions);
void compiler_options_clear_paths(CompilerOptions* me);
// Applies the defaults that depend on other options. Idempotent
void compiler_options_resolve(CompilerOptions* me);
// Shorthand for tests and tools: `strict: true` and resolve
void compiler_options_enable_strict(CompilerOptions* me);

// The standard library files (names as in thirdparty/lib, without .d.ts) to load for these options, comma separated:
// the `lib` option if there is one (mapped through tsc's lib names, "es6" is lib.es2015.d.ts), else the default
// library of the target ("default", "es6", "es2016.full"...). Empty with noLib. Names aren't validated, that's up to whoever loads them
cstr compiler_options_lib_files(CompilerOptions const* me);

bool compiler_option_is_string(csview name);
bool compiler_option_is_boolean(csview name);
// tsgo's convertJsonOption type check of a config value (isCompilerOptionsValue): 0 if the option (exact case, as
// written in a config) takes a JSON value of this tag (null always), else the 5024 message "Compiler option 'NAME'
// requires a value of type T." in *message. element: the check of one element of a list option instead
// An option only a command line may set (tsgo's IsCommandLineOnly: TS6266 in a config), exact case
bool compiler_option_is_command_line_only(csview name);
unsigned compiler_option_check_json_type(csview name, int json_tag, bool element, cstr* message);

// tsc's createUnknownOptionError with compilerOptionsDidYouMeanDiagnostics: 0 if name is one of tsc's compiler options
// (its optionDeclarations; exact case in a config's compilerOptions, ignore_case for the command line), else the code
// (TS5023, TS5025 "Did you mean", TS5093 a --build only option, TS6369 `build`) with its message in *message. `shown`
// is the name as the message quotes it (the command line's `--x`)
unsigned compiler_option_unknown_diagnostic(csview name, bool ignore_case, csview shown, cstr* message);
// tsc's commandOptionsWithoutBuild: an option a config may only set inside compilerOptions (TS6258 at its root)
bool compiler_option_is_command_option_without_build(csview name);

// tsc's isIdentifierText / parseIsolatedEntityName, for jsxFactory, jsxFragmentFactory, reactNamespace and the JSX
// pragmas: `h`, `React.createElement` (any non-ASCII character counts as an identifier character)
bool compiler_option_is_identifier(csview text);
bool compiler_option_is_entity_name(csview text);

// For the harness: if name is an option that tsc's test runner varies ("@target: es5,es2015" is two test
// configurations, "@lib: es5,dom" is one). True for the boolean and enum options, including ones we ignore otherwise
bool compiler_option_varies(csview name);
// For "@target: *" and exclusions ("-es3"): the values of an enum option (or true/false), and which spellings mean the
// same thing. Writes up to max names into out, returns how many there are. ids[i] is equal for equal values
isize compiler_option_values(csview name, const char** out, int* ids, isize max);
// TS6046's message if `name` is an enum option that doesn't take `value` (case insensitive), else empty
cstr compiler_option_bad_value_message(csview name, csview value);

// The options of the program being parsed or checked, per thread (a program's load pool and checker threads set
// them for themselves)
extern thread_local CompilerOptions const* compiler_options_private;
void set_compiler_options(CompilerOptions const* options);
void clear_compiler_options();
static inline CompilerOptions const* compiler_options() {
    panic_if(!compiler_options_private, "global CompilerOptions unset");
    return compiler_options_private;
}

// Whether the default lib's files are checked (tsc reports what it says about them then: the lib side of a conflict
// with a user declaration). tsc's harness sets skipDefaultLibCheck unless a test says otherwise
static inline bool lib_files_checked() {
    return !compiler_options()->skip_default_lib_check && !compiler_options()->skip_lib_check;
}
