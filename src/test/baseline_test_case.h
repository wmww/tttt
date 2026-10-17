#pragma once

#include "../compiler_options.h"

#include "error.h"
#include "source_file.h"
#include "program.h"

typedef struct Span Span;
typedef struct TypeNode TypeNode;
typedef struct Program Program;
typedef struct MockFs MockFs;
typedef struct JsObject JsObject;
typedef struct BaselineTestSuite BaselineTestSuite;
typedef struct LineNumberedStr LineNumberedStr;

g_declare_vec(LineNumberedStr, line_numbered_str);

typedef Program* ProgramPtr;
#define g_pro_key SvStr, svstr
#define g_val ProgramPtr, program_ptr
#define g_no_clone
#define g_valdrop(program) program_destroy(*program)
#include <cleat/generic/hmap.h>

typedef struct BaselineTestCache {
    ErrorCtx* errors;
    HMapProgramPtr libs;
} BaselineTestCache;

BaselineTestCache baseline_test_cache_init();
void baseline_test_cache_drop(BaselineTestCache* me);
Program* baseline_test_cache_get_program_with_libs(BaselineTestCache* me, csview comma_sep_libs);

// Get the name of a test given it's path (just strips the dir and extension). output is only valid as long as input is.
csview test_name_from_path(csview path);

// A virtual file within a test case
typedef struct BaselineTestFile {
    cstr path; // With `/` for `\` (`// @Filename: C:\a\b.ts` is the program's C:/a/b.ts, as in tsc's harness)
    cstr header; // The name as the test writes it, for the baselines' headers, when it isn't path
    SourceFile const* source; // Null for files that aren't scripts (package.json and so on)
    cstr raw_contents; // Only for files that aren't scripts, tsc still shows them in the errors baseline
    // A `// @Filename:` the test repeats later: the later copy is the file, this one is only shown (raw_contents)
    bool superseded;
    // A second copy of a package (same name and version) that isn't in the program: the first copy, which stands in for
    // it (ModuleRef.redirected_from), is what tsc's walk of it shows
    SourceFile const* redirect_to;
    SourceFile const* config_source; // The tsconfig the options come from (program_set_config_file()): its diagnostics
    VecLineNumberedStr symbols;
    VecLineNumberedStr types;
    VecLineNumberedStr errors;
    intptr_t error_count;
    bool content_mapped; // A content mapper's file: in the .contentmapper baseline, not the errors one
} BaselineTestFile;

void baseline_test_file_drop(BaselineTestFile* me);

#define g_class_key BaselineTestFile, baseline_test_file
#define g_no_clone
#include <cleat/generic/vec.h>

typedef struct BaselineTestCase {
    csview source_path; // Not owned, path to real file on disk that may describe several virtual files
    bool did_run;
    // tsgo's `.contentmapper` baseline (plan 56): the content-mapped files and their diagnostics, empty without any
    cstr content_mapper_baseline;
    bool content_mapper_errors; // It has diagnostics (they count as the test's for its types baseline)
    // A configuration tsgo's harness doesn't run (SkipUnsupportedCompilerOptions: an option 7.0 removed), so it has no
    // baselines: not checked, and not a test at all
    bool skipped;
    ErrorCtx* error_ctx;
    bool noisy;
    MockFs* fs;
    Program* program;
    bool no_implicit_references; // `@noImplicitReferences`
    // The last unit is the only root (tsc's harness: `@noImplicitReferences`, or it mentions `require(`/`reference
    // path`), written and shown first
    bool last_is_only_root;
    // Where the traversal is: types are printed the way they're named from there
    struct Scope const* enclosing;
    struct ModuleRef const* usage_ref; // See TyDisplay.usage_ref: set while an import or export name is added
    struct ImportAttributes const* usage_attributes; // See TyDisplay.usage_attributes
    // The class whose constructor's parameter properties are being walked: their names are its members (`C.x`)
    Span parameter_property_class;
    struct Symbol const* class_symbol; // The class whose members are being walked
    struct Symbol const* parameter_property_symbol; // With parameter_property_class: the class, and the parameter
    Span parameter_property_arg;
    struct Ty const* member_owner; // The interface or type literal whose members are being walked
    struct Symbol const* member_container; // The class, interface or enum whose members are being walked (null: literal)
    // The object literal a variable is initialized with, while walking it: its symbol is named by the variable (tsc's
    // getNameOfSymbolAsWritten), not `__object`
    struct Expr const* variable_literal;
    Span variable_literal_name;
    // The type literal a variable is annotated with, while walking its initializer: its `__type` symbol is named by the
    // variable too (getNameOfSymbolAsWritten of a declaration whose parent is a VariableDeclaration)
    struct TypeNode const* variable_type_literal;
    Span variable_type_literal_name;
    // The type parameters of the declarations around the traversal (TyParamNames.in_scope), innermost last
    struct Ty const** scope_params;
    u32 scope_params_len;
    u32 scope_params_cap;
    // The class whose members are being walked: its type parameters, and how many were in scope outside it (they
    // aren't in scope in computed member names)
    struct Ty const* const* class_params;
    u32 class_params_len;
    u32 class_params_outside;
    // Set by the traversal right before adding a node whose `any` prints as the node builder has it even without errors
    // (tsc's harness exempts children of property accesses and qualified names, binding elements, labels and import/export
    // names). The next types line consumes it
    bool any_keeps_rendering;
    // The name of a value exported by `export = x` / `export default x`: not an expression to tsc's type writer, it
    // shows the symbol's type, without narrowing (getTypeOfNode, isInRightSideOfImportOrExportAssignment)
    struct Expr const* export_name;
    bool in_type_query; // Walking the expression of a `typeof x` type
    bool private_name_in_left; // The next expression is the `#x` of `#x in o`, the only private name that is an expression
    // Set right before walking a heritage clause's type (`implements A`, an interface's `extends A`): tsc has no
    // symbols line for names there that don't resolve (not so in a type reference). The type consumes it
    bool heritage_type;
    // Symbols lines at these spans show another symbol (tsc's getSpecialPropertyAssignmentSymbol: the parts of a
    // CommonJS assignment's target show what it declares). Set around the traversal of such a target
    struct {
        Span span;
        cstr text;
        VecSpan declarations;
        bool suppress; // No symbols line at all
    } symbol_overrides[8];
    u32 symbol_override_count;
    // Declarations that are one symbol in tsc but not in tttt: a class's or interface's type parameter and a member of
    // its name (tsc's binder declares both in the members table, TypeParameterExcludes allows a value). A symbols line
    // whose first declaration is one of `declarations` shows them all, by `name`
    struct {
        csview name;
        VecSpan declarations;
    } merged_declarations[8];
    u32 merged_declaration_count;
    // No types line at these spans (the nodes tsc's isPartOfTypeNode takes for a type: the property accesses of a
    // heritage clause's expression where a type is meant). Set around the traversal of such an expression
    Span hidden_type_spans[8];
    u32 hidden_type_span_count;
    bool error_type; // The types line being added is the error type (see LineNumberedStr.error_type)
    // ... and an unresolved name, which prints as written (tsc reuses the reference) unless the intrinsic name is
    // printed (no errors): `error`
    bool unresolved_type;
    // In the body of a `with`: tsc's getTypeOfNode and getSymbolAtLocation answer nothing there, every types line is
    // the error type and there are no symbols lines
    bool in_with;
    // The first local import in the file being walked of a shorthand ambient module (`declare module "m";`): the name
    // tsc's symbolToString finds for the module symbol, which every specifier's property name of it resolves to
    struct Symbol const* shorthand_module;
    Span shorthand_alias;
    cstr lib_list;
    cstr lib_files; // `@libFiles: react.d.ts,lib.d.ts`: files of tsc's tests/lib (at /.lib) that are roots too
    bool no_types_and_symbols; // @noTypesAndSymbols: tsc writes no .types and .symbols baselines for this test
    bool full_emit_paths; // @fullEmitPaths: the `.js` baseline names outputs by their paths, not their base names
    cstr formatted_errors;
    cstr formatted_pretty_errors;
    bool pretty; // `@pretty: true`: the diagnostics at the top are tsc's pretty ones, and a summary ends the baseline
    cstr formatted_global_errors; // The `!!!` lines of diagnostics without a file
    VecBaselineTestFile files;
    // A unit named tsconfig.json: its options (under the directives) and its files are the roots
    bool has_tsconfig;
    StrList tsconfig_roots; // Absolute
    VecCStr directives; // (key, value) pairs, to be applied again over the tsconfig's options
    csview current_unit; // While the test source is read: the `@filename` directives apply to
    VecCStr links; // `@link: target -> link` directives as (target, link) pairs, made once the files exist
    VecCStr display_paths; // Owns the display_path_override of program files the test doesn't write
} BaselineTestCase;

// Never prints to stdout, only prints to stderr if noisy is true, or if it panics. show_logs leaves the checker's
// log output on without the rest of what noisy does. config is one of baseline_test_configurations(), or empty
// The traversal enters a generic declaration: its type parameters are in scope until
// baseline_test_case_leave_generic(the returned value)
u32 baseline_test_case_enter_generic(BaselineTestCase* me, struct Generic const* generic);
void baseline_test_case_leave_generic(BaselineTestCase* me, u32 saved);
struct TyList;
u32 baseline_test_case_enter_type_params(BaselineTestCase* me, struct TyList params);

// How many checkers every test program is checked with (baseline_tests --checkers, program_set_checkers()). tsc's
// baselines come from one checker
extern u32 baseline_test_checkers;
// The suite's tests/lib (tsc's /.lib) for tests that aren't below a tests/cases directory (tttt's own tests)
extern csview baseline_test_lib_dir;
extern u32 baseline_test_threads; // baseline_tests --threads, program_set_threads()
// baseline_tests --defer-bodies: function bodies parsed and walked when a checker needs them (plan 78,
// program_set_defer_bodies())
extern bool baseline_test_defer_bodies;
// --checker-memory MB: a checker recycle budget (plan 67), -1 (the default) none; 0 recycles after every file, which
// drops deferred bodies' materializations as early as can be (plan 78 stage 2)
extern i64 baseline_test_checker_memory;

BaselineTestCase run_baseline_test_case(
    BaselineTestCache* cache,
    csview source_path,
    csview config,
    bool noisy,
    bool show_logs
);

// A test whose `// @option:` directives give several values for a compiler option ("@target: es5,es2015",
// "@strict: *", "@module: *,-amd") is one test per combination of those, each with its own baselines named
// name(option=value,other=value). Appends the configurations of the test with the given source text to out in that
// form ("option=value,other=value", options sorted by name, all lowercase), or a single empty string for the usual
// test that has only one
void baseline_test_configurations(csview test_source, VecCStr* out);
void baseline_test_case_drop(BaselineTestCase* me);

// These are used to get the data into string form, so it can be compared with known correct results or serialized
void baseline_test_case_render_errors(cstr* target, BaselineTestCase const* me);
void baseline_test_case_render_symbols(cstr* target, BaselineTestCase const* me);
// had_errors: whether tsc reported anything for the test, which decides whether an `any` prints its intrinsic name (see
// LineNumberedStr.intrinsic_at). The expected baseline's say rather than ours, so that a missing error doesn't
// fail the types component too
void baseline_test_case_render_types(cstr* target, BaselineTestCase const* me, bool had_errors);
bool baseline_test_case_has_errors(BaselineTestCase const* me);

typedef struct BaselineSym {
    VecSpan declarations;   // The places this symbol was declared. Empty spans are ignored. If empty or all nulls falls
                            // back to `span` unless allow_declaration_empty is set
    bool allow_declaration_empty; // if this is true and `declarations` is empty or all null no declarations are added
    VecSpan namespaces;     // Includes the interface/class name if applicable. Empty spans are ignored, `span`/
                            // `name` is added to the end
    Span span;              // Used for placing this symbol in the output
    Span name;              // The specific identifier or value, defaults to the contents of `span` if not set
    bool name_is_value;     // If to use namespace['property'] notation
    cstr text;              // The whole name when set (owned): what symbol_chain.h printed. `namespaces`/`name` are
                            // ignored then
} BaselineSym;

// These are used by traversial to assemble the test case data while the test is being run. After the test has completed
// (and run_baseline_test_case() has returned) these should no longer be called.
void baseline_test_case_add_symbol(BaselineTestCase* me, BaselineSym data);
// The symbols baseline's name of a module's export at its declaration when an alias of the module reaches it (`import
// * as self from "./self"` makes it `self.x`: tsc's symbolToString goes by the accessible chain), else empty
cstr baseline_test_case_module_alias_text(BaselineTestCase const* me, Symbol const* declaration);
void baseline_test_case_add_type(BaselineTestCase* me, Span span, Ty const* type);
// A type line that is already text (nothing in it is reused)
void baseline_test_case_add_type_text(BaselineTestCase* me, Span span, csview text);
void baseline_test_case_add_type_node(BaselineTestCase* me, Span span, TypeNode const* node); // A written type
void baseline_test_case_add_type_recursive(BaselineTestCase* me, Span outer_span, Span inner_span, Ty const* type);
// symbol_declaration_spans() as tsc's symbols baseline lists them (see the definition)
VecSpan baseline_symbol_declaration_spans(Symbol const* me);
void baseline_test_case_add_declaration(BaselineTestCase* me, Span span, Symbol const* declaration);
// The type of a symbol as a value (any if it has none) at span
void baseline_test_case_add_symbol_value(BaselineTestCase* me, Span span, Symbol const* symbol);
