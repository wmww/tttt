#include "ty/declaration_emit.h"
#include "ty/decl_comments.h"
#include "ty/decl_map.h"
#include "ty/ty_internal.h"
#include "statement.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "port_statement.h"
#include "source_file.h"
#include "compiler_options.h"
#include "error.h"
#include "block.h"
#include "program.h"
#include "callable_type.h"
#include "symbol_chain.h"
#include "several_types.h"
#include "expr.h"
#include "ty/pseudo.h"
#include "import_attributes.h"
#include "ast_walk.h"

#include <math.h>

// tsc's declaration transformer walks what a `.d.ts` would contain: the visible statements (everything in a script,
// the exported ones and what `export { x }` names in a module), and in them the members that are emitted. Wherever a
// type would be written that the source doesn't spell out, the node builder serializes the declaration's pseudo type
// (pseudo.h: what its syntax says) where that is equivalent to its type, else prints the type (tsgo's
// serializeTypeForDeclaration and serializeReturnTypeForSignature). With isolatedDeclarations every place the syntax
// doesn't say the type is reported (ReportInferenceFallback: TS9007-TS9039, with the "Add a type annotation" related
// information of createGetIsolatedDeclarationErrors). The transformer adds a few checks of its own (TS9019, TS9021,
// TS9023, TS9026, TS9038), and the symbol tracker reports what the printed types can't name (TS4xxx).
//
// This file walks the same way and does the serializing's equivalence and reporting, printing types for the tracker
// (into scratch strings). With emit on, a second walk writes the `.d.ts` (see "Declaration emit's writer" below).
//
// Not done: JavaScript (no JS declaration emit); what is approximated is in agents/hacks.md "isolatedDeclarations
// reporting" and "Declaration emit's writer"

// What tsc's findNearestDeclaration finds for a node: the declaration the "Add a type annotation" related information
// is about
typedef enum NearKind : uint8_t {
    NEAR_NONE,
    NEAR_VARIABLE,
    NEAR_PARAMETER,
    NEAR_PROPERTY,
    NEAR_EXPORT_ASSIGNMENT,
    // From a `return` statement: the function it is in
    NEAR_FUNCTION_EXPRESSION, // function expression or arrow function
    NEAR_FUNCTION_DECLARATION,
    NEAR_METHOD,
    NEAR_GET_ACCESSOR,
    NEAR_SET_ACCESSOR,
} NearKind;

typedef struct Near {
    NearKind kind;
    Span span; // Where tsc's createDiagnosticForNode puts it
    csview name; // getTextOfNode of its name, empty for an export assignment
} Near;

// Where a node is, as far as tsc's diagnostics for it care (tsgo looks at its parents, tttt finds it from the
// declaration being emitted, see locate())
typedef struct Place {
    Near near;
    // The node is the initializer of `near` (or the type of a type assertion that is), through parentheses and type
    // assertions (createExpressionError's `parentDeclaration === parent`)
    bool direct;
} Place;

// An accessor pair: the getter, the setter (either may be null)
typedef struct Accessors {
    FunctionImpl const* getter;
    FunctionImpl const* setter;
    Span getter_name, setter_name;
} Accessors;

// The declaration whose type is being serialized: the nodes reported while serializing it are found in it
typedef enum RootKind : uint8_t {
    ROOT_NONE,
    ROOT_VARIABLE, // `binding`
    ROOT_FIELD, // `field`
    ROOT_FUNCTION, // `function` (its parameters and return type)
    ROOT_EXPORT_DEFAULT, // `expr`, of the statement `at`
} RootKind;

typedef struct Root {
    RootKind kind;
    union {
        InitializedBinding const* binding;
        ClassItem const* field;
        FunctionImpl const* function;
        Expr const* expr;
    };
    Span at;
} Root;

// The declaration being emitted whose diagnostics a name it can't use gets (tsc's getSymbolAccessibilityDiagnostic,
// createGetSymbolAccessibilityDiagnosticForNode of the innermost declaration that canProduceDiagnostics)
typedef enum EmitKind : uint8_t {
    EMIT_NONE,
    EMIT_VARIABLE, // A variable declaration or binding element
    EMIT_PROPERTY, // A property declaration or signature, an expando assignment, a private constructor's property
    EMIT_GETTER,
    EMIT_SETTER,
    EMIT_RETURN, // A signature's return type
    EMIT_PARAMETER,
    EMIT_TYPE_PARAMETER,
    EMIT_METHOD_NAME, // A late bound name (createGetSymbolAccessibilityDiagnosticForNodeName)
    EMIT_ACCESSOR_NAME,
    EMIT_IMPLEMENTS,
    EMIT_EXTENDS, // Of a class
    EMIT_INTERFACE_EXTENDS,
    EMIT_TYPE_ALIAS,
    EMIT_DEFAULT_EXPORT,
    EMIT_IMPORT_EQUALS,
} EmitKind;

// What a parameter, return type or type parameter belongs to
typedef enum EmitSig : uint8_t {
    SIG_FUNCTION, // A function declaration or function type
    SIG_CONSTRUCTOR,
    SIG_CONSTRUCT, // A construct signature or constructor type
    SIG_CALL,
    SIG_INDEX,
    SIG_METHOD,
    SIG_ACCESSOR,
    SIG_CLASS,
    SIG_INTERFACE,
    SIG_TYPE_ALIAS,
    SIG_MAPPED,
    SIG_INFER,
} EmitSig;

typedef struct Emitting {
    EmitKind kind;
    EmitSig sig;
    bool is_static;
    bool in_class; // A member of a class, not of an interface or type literal
    Span name; // The declaration's name (the message's first argument for most kinds), empty if it has none
    Span at; // Where the diagnostic goes unless an entity name is to blame
    Span fallback; // Without a name: where the symbol tracker's own diagnostics go (tsc's errorFallbackNode), else `at`
    // `name` is only the message's name, not a node of the declaration (tsgo's errorDeclarationNameWithFallback of a
    // JavaScript `exports.x = e`, whose BinaryExpression has no Name()): the tracker's diagnostics go to `at`
    bool name_not_node;
    TyPrintedTypes* printed; // Its serializedTypes cache: the walk's printed_types or class_printed_types
} Emitting;

typedef struct Walk {
    SourceFile const* file;
    ErrorCtx* errs;
    Checker* checker;
    bool strict_null_checks;
    bool isolated; // isolatedDeclarations: the syntactic builder's fallbacks are errors (TS9xxx)
    Scope const* scope; // The statement list being walked (tsc's enclosingDeclaration, as far as names go)
    Emitting emitting;
    // Inside a type literal or mapped type that isn't a type alias's type: its members don't get diagnostics of their
    // own (tsc's suppressNewDiagnosticContexts)
    bool suppress_contexts;
    bool member_static, member_in_class; // The class or interface member being walked
    // The syntactic builder fell back to the checker's type somewhere in the declaration being emitted: its type is
    // printed (the node builder's serializeTypeOfDeclaration and friends)
    bool fell_back;
    // Walking a type node the syntactic builder reuses (an assertion's, a function expression's annotations): a name
    // that isn't accessible from here makes it fall back instead (trackExistingEntityName)
    bool reusing;
    Root root; // The declaration being serialized
    Accessors class_pair; // Serializing a class accessor: its pair
    // The declaration's type is printed because its pseudo type doesn't say it (tsgo's typeToTypeNode fallback): the
    // pseudo types of what it prints report (else they did already)
    bool print_reports;
    // Walking an object literal's members (tsgo's pseudoTypeToNode sets InObjectTypeLiteral): `this` printed there is
    // inaccessible (TS2527)
    bool in_object_literal;
    // The diagnostics reported since this index are of the declaration being emitted (tsc deduplicates them)
    isize_t emitting_errors;
    DeclTracker tracker;
    // The file's declarations an emitted one names that aren't visible themselves: declaration emit emits them too
    // (late painting: tsc's aliasesToMakeVisible and lateMarkedStatements). `painted_done` of them were walked
    Symbol const** painted;
    TyPrintedTypes printed_types; // What the declaration being emitted printed so far (TyDisplay.printed)
    // tsgo's serializedTypes cache is per enclosing declaration, which a class's property declarations share (the
    // class: a property isn't an enclosing declaration) and a function's parameters share (the function). The next
    // emit_enter's owner (printed_next: a function, or &class_printed_types for the class being walked), the owner
    // printed_types is about, the class being walked and the one class_printed_types is about
    void const* printed_next;
    void const* printed_owner;
    TyPrintedTypes class_printed_types;
    ClassDecl const* printed_class;
    ClassDecl const* class_printed_owner;
    u32 painted_count, painted_cap;
    bool* painted_done;
    bool late_pass; // Walking the statements again for painted ones only
    // The painted symbols this late pass took: each of their declarations is walked (a namespace and the interface it
    // merges with, both painted by one name)
    Symbol const** late_taken;
    u32 late_taken_count;
    u32 late_taken_cap;
    // A module's local declarations that `export { x }` or `export default x` name (tsc's collectLinkedAliases makes
    // them visible)
    Symbol const** exported_locals;
    u32 exported_local_count;
    // The specifiers asked for so far (the file's import types of each module): whether they only reach it through a
    // node_modules, and that specifier
    struct SpecifierMemo* specifiers;
    u32 specifier_count, specifier_cap;
    // Declaration emit (plan 64): the second walk writes the `.d.ts` into `out` (null in the first, which only reports).
    // `indent` levels of 4 spaces, `type_out`: where serialize_*() write the type they decide on (null: nowhere)
    cstr* out;
    u32 indent;
    cstr* type_out;
    cstr* pseudo_out; // The pseudo type serialize_*() print, kept until they know whether it falls back
    // The statements written so far in the statement list being written (tsc's transformAndReplaceLatePaintedStatements
    // bookkeeping): one is an export declaration or assignment (resultHasScopeMarker), one is a declaration without
    // `export` (needsScopeFixMarker), one is an import or export (resultHasExternalModuleIndicator, the file's level).
    // `export_offsets`: where in `out` the `export ` modifiers of a namespace body's statements are, which tsc strips
    // when every statement has one
    bool has_scope_marker, needs_scope_fix, has_module_indicator;
    u32 generated_names; // The printer's generated names given so far (`_a`, `_b`...)
    u32* export_offsets;
    u32 export_offset_count, export_offset_cap;
    DeclComments comments; // The comments copied from the source (decl_comments.h)
    // The writer: the type parameters declared where the declaration being written is (its own too), which a type
    // parameter the node builder prints may shadow (tsc's typeParameterShadowsOtherTypeParameterInScope)
    Ty const** scope_params;
    u32 scope_param_count, scope_param_cap;
    // The writer, writing a function's return type: its parameters are in scope there (tsc's enclosing declaration is the
    // function), shadowing what they name
    TyDisplayParams return_params;
    bool in_return;
    isize_t statement_start; // Where the statement being written starts in `out`, its leading comments first
    bool statement_synthesized; // The statement is written as declarations made anew, without its comments
    bool map; // declarationMap: the writer marks where source nodes start and end (decl_map.h)
    // A CommonJS JavaScript file's exports (collect_commonjs()): the last `module.exports = v` (the assignment) and
    // whether it is a statement of the file, the `exports.x = v` and `Object.defineProperty(exports, ...)` of each
    // name, first first. `cjs_names`: the names the writer generated for them
    struct Expr const* cjs_export_equals;
    bool cjs_export_equals_top_level;
    struct CjsMember* cjs_members;
    u32 cjs_member_count;
    cstr cjs_names;
    // Writing a class expression an export assigns: whether its own symbol was printed (tsgo's watchedClassSymbol)
    Symbol const* cjs_watched_class;
    bool cjs_class_tracked;
} Walk;

// A CommonJS export of a JavaScript file (tsgo's transformCommonJSExport input): `exports.x = v`, `module.exports.x =
// v`, `Object.defineProperty(exports, "x", descriptor)`
typedef struct CjsMember {
    Expr const* expr; // The assignment or the call
    Expr const* value; // What is assigned: the descriptor's `value` (null without one)
    csview name;
    bool identifier; // The name is an identifier (else written as a string literal)
    bool top_level; // The expression of a statement of the file
} CjsMember;

typedef struct SpecifierMemo {
    SourceFile const* target;
    bool has_plain, has_unsafe;
    cstr plain; // getSpecifierForModuleSymbol without a mode of its own
    cstr unsafe; // Of the import type naming the module, empty if portable
} SpecifierMemo;

// Symbol accessibility diagnostics (TS4xxx)

// The declaration emit accessibility messages (diagnosticMessages.json)
static char const* access_message(unsigned code) {
    switch (code) {
        case 4000: return "Import declaration '{0}' is using private name '{1}'.";
        case 4002: return "Type parameter '{0}' of exported class has or is using private name '{1}'.";
        case 4004: return "Type parameter '{0}' of exported interface has or is using private name '{1}'.";
        case 4006: return "Type parameter '{0}' of constructor signature from exported interface has or is using private name '{1}'.";
        case 4008: return "Type parameter '{0}' of call signature from exported interface has or is using private name '{1}'.";
        case 4010: return "Type parameter '{0}' of public static method from exported class has or is using private name '{1}'.";
        case 4012: return "Type parameter '{0}' of public method from exported class has or is using private name '{1}'.";
        case 4014: return "Type parameter '{0}' of method from exported interface has or is using private name '{1}'.";
        case 4016: return "Type parameter '{0}' of exported function has or is using private name '{1}'.";
        case 4019: return "Implements clause of exported class '{0}' has or is using private name '{1}'.";
        case 4020: return "'extends' clause of exported class '{0}' has or is using private name '{1}'.";
        case 4021: return "'extends' clause of exported class has or is using private name '{0}'.";
        case 4022: return "'extends' clause of exported interface '{0}' has or is using private name '{1}'.";
        case 4023: return "Exported variable '{0}' has or is using name '{1}' from external module {2} but cannot be named.";
        case 4024: return "Exported variable '{0}' has or is using name '{1}' from private module '{2}'.";
        case 4025: return "Exported variable '{0}' has or is using private name '{1}'.";
        case 4026: return "Public static property '{0}' of exported class has or is using name '{1}' from external module {2} but cannot be named.";
        case 4027: return "Public static property '{0}' of exported class has or is using name '{1}' from private module '{2}'.";
        case 4028: return "Public static property '{0}' of exported class has or is using private name '{1}'.";
        case 4029: return "Public property '{0}' of exported class has or is using name '{1}' from external module {2} but cannot be named.";
        case 4030: return "Public property '{0}' of exported class has or is using name '{1}' from private module '{2}'.";
        case 4031: return "Public property '{0}' of exported class has or is using private name '{1}'.";
        case 4032: return "Property '{0}' of exported interface has or is using name '{1}' from private module '{2}'.";
        case 4033: return "Property '{0}' of exported interface has or is using private name '{1}'.";
        case 4034: return "Parameter type of public static setter '{0}' from exported class has or is using name '{1}' from private module '{2}'.";
        case 4035: return "Parameter type of public static setter '{0}' from exported class has or is using private name '{1}'.";
        case 4036: return "Parameter type of public setter '{0}' from exported class has or is using name '{1}' from private module '{2}'.";
        case 4037: return "Parameter type of public setter '{0}' from exported class has or is using private name '{1}'.";
        case 4038: return "Return type of public static getter '{0}' from exported class has or is using name '{1}' from external module {2} but cannot be named.";
        case 4039: return "Return type of public static getter '{0}' from exported class has or is using name '{1}' from private module '{2}'.";
        case 4040: return "Return type of public static getter '{0}' from exported class has or is using private name '{1}'.";
        case 4041: return "Return type of public getter '{0}' from exported class has or is using name '{1}' from external module {2} but cannot be named.";
        case 4042: return "Return type of public getter '{0}' from exported class has or is using name '{1}' from private module '{2}'.";
        case 4043: return "Return type of public getter '{0}' from exported class has or is using private name '{1}'.";
        case 4044: return "Return type of constructor signature from exported interface has or is using name '{0}' from private module '{1}'.";
        case 4045: return "Return type of constructor signature from exported interface has or is using private name '{0}'.";
        case 4046: return "Return type of call signature from exported interface has or is using name '{0}' from private module '{1}'.";
        case 4047: return "Return type of call signature from exported interface has or is using private name '{0}'.";
        case 4048: return "Return type of index signature from exported interface has or is using name '{0}' from private module '{1}'.";
        case 4049: return "Return type of index signature from exported interface has or is using private name '{0}'.";
        case 4050: return "Return type of public static method from exported class has or is using name '{0}' from external module {1} but cannot be named.";
        case 4051: return "Return type of public static method from exported class has or is using name '{0}' from private module '{1}'.";
        case 4052: return "Return type of public static method from exported class has or is using private name '{0}'.";
        case 4053: return "Return type of public method from exported class has or is using name '{0}' from external module {1} but cannot be named.";
        case 4054: return "Return type of public method from exported class has or is using name '{0}' from private module '{1}'.";
        case 4055: return "Return type of public method from exported class has or is using private name '{0}'.";
        case 4056: return "Return type of method from exported interface has or is using name '{0}' from private module '{1}'.";
        case 4057: return "Return type of method from exported interface has or is using private name '{0}'.";
        case 4058: return "Return type of exported function has or is using name '{0}' from external module {1} but cannot be named.";
        case 4059: return "Return type of exported function has or is using name '{0}' from private module '{1}'.";
        case 4060: return "Return type of exported function has or is using private name '{0}'.";
        case 4061: return "Parameter '{0}' of constructor from exported class has or is using name '{1}' from external module {2} but cannot be named.";
        case 4062: return "Parameter '{0}' of constructor from exported class has or is using name '{1}' from private module '{2}'.";
        case 4063: return "Parameter '{0}' of constructor from exported class has or is using private name '{1}'.";
        case 4064: return "Parameter '{0}' of constructor signature from exported interface has or is using name '{1}' from private module '{2}'.";
        case 4065: return "Parameter '{0}' of constructor signature from exported interface has or is using private name '{1}'.";
        case 4066: return "Parameter '{0}' of call signature from exported interface has or is using name '{1}' from private module '{2}'.";
        case 4067: return "Parameter '{0}' of call signature from exported interface has or is using private name '{1}'.";
        case 4068: return "Parameter '{0}' of public static method from exported class has or is using name '{1}' from external module {2} but cannot be named.";
        case 4069: return "Parameter '{0}' of public static method from exported class has or is using name '{1}' from private module '{2}'.";
        case 4070: return "Parameter '{0}' of public static method from exported class has or is using private name '{1}'.";
        case 4071: return "Parameter '{0}' of public method from exported class has or is using name '{1}' from external module {2} but cannot be named.";
        case 4072: return "Parameter '{0}' of public method from exported class has or is using name '{1}' from private module '{2}'.";
        case 4073: return "Parameter '{0}' of public method from exported class has or is using private name '{1}'.";
        case 4074: return "Parameter '{0}' of method from exported interface has or is using name '{1}' from private module '{2}'.";
        case 4075: return "Parameter '{0}' of method from exported interface has or is using private name '{1}'.";
        case 4076: return "Parameter '{0}' of exported function has or is using name '{1}' from external module {2} but cannot be named.";
        case 4077: return "Parameter '{0}' of exported function has or is using name '{1}' from private module '{2}'.";
        case 4078: return "Parameter '{0}' of exported function has or is using private name '{1}'.";
        case 4081: return "Exported type alias '{0}' has or is using private name '{1}'.";
        case 4082: return "Default export of the module has or is using private name '{0}'.";
        case 4083: return "Type parameter '{0}' of exported type alias has or is using private name '{1}'.";
        case 4084: return "Exported type alias '{0}' has or is using private name '{1}' from module {2}.";
        case 4085: return "Extends clause for inferred type '{0}' has or is using private name '{1}'.";
        case 4091: return "Parameter '{0}' of index signature from exported interface has or is using name '{1}' from private module '{2}'.";
        case 4092: return "Parameter '{0}' of index signature from exported interface has or is using private name '{1}'.";
        case 4095: return "Public static method '{0}' of exported class has or is using name '{1}' from external module {2} but cannot be named.";
        case 4096: return "Public static method '{0}' of exported class has or is using name '{1}' from private module '{2}'.";
        case 4097: return "Public static method '{0}' of exported class has or is using private name '{1}'.";
        case 4098: return "Public method '{0}' of exported class has or is using name '{1}' from external module {2} but cannot be named.";
        case 4099: return "Public method '{0}' of exported class has or is using name '{1}' from private module '{2}'.";
        case 4100: return "Public method '{0}' of exported class has or is using private name '{1}'.";
        case 4101: return "Method '{0}' of exported interface has or is using name '{1}' from private module '{2}'.";
        case 4102: return "Method '{0}' of exported interface has or is using private name '{1}'.";
        case 4103: return "Type parameter '{0}' of exported mapped object type is using private name '{1}'.";
        case 4106: return "Parameter '{0}' of accessor has or is using private name '{1}'.";
        case 4107: return "Parameter '{0}' of accessor has or is using name '{1}' from private module '{2}'.";
        case 4108: return "Parameter '{0}' of accessor has or is using name '{1}' from external module '{2}' but cannot be named.";
        default: impossible;
    }
}

// The message of the accessibility diagnostic of what is being emitted (getSymbolAccessibilityDiagnostic): `module`, the
// result has an errorModuleName; `cannot_be_named`, the symbol is from an external module that can't be named. 0 if
// there is none (a constructor parameter property's own diagnostic is reported as a parameter)
static unsigned access_code(Emitting const* e, bool module, bool cannot_be_named) {
    // Codes for: a private name, a name from a private module, a name from an external module that can't be named (0:
    // there is no such message, the private module one is used)
    unsigned private_name = 0, private_module = 0, cannot = 0;
#define CODES(a, b, c) (private_name = (a), private_module = (b), cannot = (c))
    switch (e->kind) {
        case EMIT_NONE: return 0;
        case EMIT_VARIABLE: CODES(4025, 4024, 4023); break;
        case EMIT_PROPERTY:
        case EMIT_ACCESSOR_NAME:
            if (e->is_static) CODES(4028, 4027, 4026);
            else if (e->in_class) CODES(4031, 4030, 4029);
            else CODES(4033, 4032, 0);
            break;
        case EMIT_METHOD_NAME:
            if (e->is_static) CODES(4097, 4096, 4095);
            else if (e->in_class) CODES(4100, 4099, 4098);
            else CODES(4102, 4101, 0);
            break;
        case EMIT_SETTER:
            if (e->is_static) CODES(4035, 4034, 0);
            else CODES(4037, 4036, 0);
            break;
        case EMIT_GETTER:
            if (e->is_static) CODES(4040, 4039, 4038);
            else CODES(4043, 4042, 4041);
            break;
        case EMIT_RETURN:
            switch (e->sig) {
                case SIG_CONSTRUCT: CODES(4045, 4044, 0); break;
                case SIG_CALL: CODES(4047, 4046, 0); break;
                case SIG_INDEX: CODES(4049, 4048, 0); break;
                case SIG_METHOD:
                    if (e->is_static) CODES(4052, 4051, 4050);
                    else if (e->in_class) CODES(4055, 4054, 4053);
                    else CODES(4057, 4056, 0);
                    break;
                default: CODES(4060, 4059, 4058); break;
            }
            break;
        case EMIT_PARAMETER:
            switch (e->sig) {
                case SIG_CONSTRUCTOR: CODES(4063, 4062, 4061); break;
                case SIG_CONSTRUCT: CODES(4065, 4064, 0); break;
                case SIG_CALL: CODES(4067, 4066, 0); break;
                case SIG_INDEX: CODES(4092, 4091, 0); break;
                case SIG_METHOD:
                    if (e->is_static) CODES(4070, 4069, 4068);
                    else if (e->in_class) CODES(4073, 4072, 4071);
                    else CODES(4075, 4074, 0);
                    break;
                case SIG_ACCESSOR: CODES(4106, 4107, 4108); break;
                default: CODES(4078, 4077, 4076); break;
            }
            break;
        case EMIT_TYPE_PARAMETER: {
            unsigned code = 4016;
            switch (e->sig) {
                case SIG_CLASS: code = 4002; break;
                case SIG_INTERFACE: code = 4004; break;
                case SIG_MAPPED: code = 4103; break;
                case SIG_CONSTRUCT: code = 4006; break;
                case SIG_CALL: code = 4008; break;
                case SIG_METHOD: code = e->is_static ? 4010 : e->in_class ? 4012 : 4014; break;
                case SIG_INFER: code = 4085; break;
                case SIG_TYPE_ALIAS: code = 4083; break;
                default: break;
            }
            return code;
        }
        case EMIT_IMPLEMENTS: return 4019;
        case EMIT_EXTENDS: return span_valid(e->name) ? 4020 : 4021;
        case EMIT_INTERFACE_EXTENDS: return 4022;
        case EMIT_TYPE_ALIAS: return module ? 4084 : 4081;
        case EMIT_DEFAULT_EXPORT: return 4082;
        case EMIT_IMPORT_EQUALS: return 4000;
    }
#undef CODES
    if (!module) return private_name;
    return cannot_be_named && cannot ? cannot : private_module;
}

// Whether the kind's message starts with the declaration's name (tsc's typeName)
static bool access_has_type_name(Emitting const* e) {
    switch (e->kind) {
        case EMIT_RETURN:
        case EMIT_DEFAULT_EXPORT:
            return false;
        case EMIT_EXTENDS: return span_valid(e->name);
        default: return true;
    }
}

static void append_message(cstr* out, char const* template, csview const* args, u32 count) {
    for (char const* c = template; *c; c++) {
        if (c[0] == '{' && c[1] >= '0' && c[1] <= '9' && c[2] == '}') {
            u32 i = (u32)(c[1] - '0');
            if (i < count) cstr_append_sv(out, args[i]);
            c += 2;
            continue;
        }
        cstr_append_n(out, c, 1);
    }
}

// handleSymbolAccessibilityError of a result that isn't accessible: `at`, the result's errorNode (an entity name),
// else where the emitted declaration's diagnostics go
static void report_access(Walk* me, SymbolAccessibility kind, csview symbol_name, csview module_name, bool has_module,
    Span at
) {
    Emitting const* e = &my emitting;
    unsigned code = access_code(e, has_module, kind == SYMBOL_CANNOT_BE_NAMED);
    if (!code) return;
    csview args[3];
    u32 count = 0;
    if (access_has_type_name(e)) args[count++] = span_sv(e->name);
    args[count++] = symbol_name;
    args[count++] = module_name;
    cstr message = cstr_init();
    append_message(&message, access_message(code), args, count);
    if (!span_valid(at)) at = e->at;
    for (isize_t i = my emitting_errors; i < vec_error_size(&my errs->errors); i++) {
        Error const* other = vec_error_at(&my errs->errors, i);
        if (other->ts_code == code && span_eq(other->span, at) && cstr_equals_sv(&other->message, cstr_sv(&message))) {
            cstr_drop(&message);
            return;
        }
    }
    error_ctx_error_s(my errs, at, code, message);
}

// Enters the diagnostics of a declaration being emitted (createGetSymbolAccessibilityDiagnosticForNode), returning the
// ones to restore after it. `at`: where they go, if not at the name
static Emitting emit_enter(Walk* me, EmitKind kind, EmitSig sig, Span name, Span at) {
    Emitting saved = my emitting;
    // (tsgo's serializedTypes cache is per enclosing declaration)
    void const* owner = my printed_next;
    my printed_next = nullptr;
    TyPrintedTypes* printed = &my printed_types;
    if (owner == &my class_printed_types) {
        printed = &my class_printed_types;
        if (my class_printed_owner != my printed_class && printed->count) ty_printed_types_clear(printed);
        my class_printed_owner = my printed_class;
    } else {
        if ((!owner || owner != my printed_owner) && printed->count) ty_printed_types_clear(printed);
        my printed_owner = owner;
    }
    my emitting = (Emitting){
        .printed = printed,
        .kind = kind, .sig = sig, .is_static = my member_static, .in_class = my member_in_class, .name = name,
        .at = span_valid(at) ? at : name,
    };
    return saved;
}

// The same for a declaration inside a written type, which only gets diagnostics of its own outside type literals
static Emitting emit_enter_nested(Walk* me, EmitKind kind, EmitSig sig, Span name, Span at) {
    if (my suppress_contexts) return my emitting;
    return emit_enter(me, kind, sig, name, at);
}

static void emit_leave(Walk* me, Emitting saved) { my emitting = saved; }

// The node builder printing `type` for the declaration being emitted, for what its tracker reports (printed into a
// scratch string)
static void print_type(Walk* me, Ty const* type, bool reports);

static void print_inferred(Walk* me, Ty const* type) { print_type(me, type, my print_reports); }

// ... `reports`: what the printer prints from syntax reports (tsgo's typeToTypeNode without suppressReportInferenceFallback)
static TyDisplay emit_display(Walk* me, cstr* target);

static TyParamNames walk_param_names(Walk* me) {
    return (TyParamNames){ .in_scope = { .data = my scope_params, .len = my scope_param_count } };
}

// Enters a declaration's type parameters (see Walk.scope_params): returns what to leave with
static u32 enter_type_params(Walk* me, Generic const* generic) {
    u32 saved = my scope_param_count;
    if (!my out || !generic || generic_empty(generic)) return saved;
    TyList params = ty_type_params_of(my checker, generic);
    for (u32 i = 0; i < params.len; i++) {
        if (my scope_param_count == my scope_param_cap) {
            my scope_param_cap = my scope_param_cap ? my scope_param_cap * 2 : 8;
            my scope_params = realloc(my scope_params, my scope_param_cap * sizeof(Ty const*));
        }
        my scope_params[my scope_param_count++] = params.data[i];
    }
    return saved;
}

static void leave_type_params(Walk* me, u32 saved) { my scope_param_count = saved; }

static void print_type(Walk* me, Ty const* type, bool reports) {
    if (type && my type_out) {
        cstr* target = my type_out;
        cstr* outer_pseudo = my pseudo_out;
        my pseudo_out = my type_out = nullptr;
        cstr text = cstr_init();
        TyParamNames names = walk_param_names(me);
        TyDisplay d = emit_display(me, &text);
        d.param_names = &names;
        d.suppress_fallback = !reports;
        ty_display(my checker, d, type);
        ty_param_names_drop(&names);
        cstr_append_s(target, text);
        cstr_drop(&text);
        my type_out = target;
        my pseudo_out = outer_pseudo;
        return;
    }
    if (!type || my emitting.kind == EMIT_NONE) return;
    my tracker.truncated = false;
    // (With the types baseline's node reuse: tsc's declaration emit reuses annotations the same way, naming what a
    // reused name refers to anew where it means something else here)
    cstr text = cstr_init();
    TyParamNames names = walk_param_names(me);
    ty_display(my checker, (TyDisplay){
        .target = &text, .types_baseline = true, .enclosing = my scope, .tracker = &my tracker, .at = my emitting.name,
        .suppress_fallback = !reports, .in_object_literal = my in_object_literal, .param_names = &names,
    }, type);
    ty_param_names_drop(&names);
    if (getenv("TTTT_DEBUG_DECLARATION_TYPES")) {
        fprintf(stderr, "%.*s: %s\n", (int)my emitting.name.size, my emitting.name.buf, cstr_str(&text));
    }
    cstr_drop(&text);
}

// A declaration whose type the syntactic builder may fall back from: starts tracking that
static bool fallback_begin(Walk* me) {
    bool outer = my fell_back;
    my fell_back = false;
    my emitting_errors = vec_error_size(&my errs->errors);
    return outer;
}

// ... and prints its type if it did. Returns whether it did
static bool fallback_end(Walk* me, bool outer, Ty const* type) {
    bool fell_back = my fell_back;
    if (fell_back) print_inferred(me, type);
    my print_reports = false;
    my fell_back = outer || fell_back;
    return fell_back;
}

// A declaration of this file that an emitted declaration names: if it isn't visible, it is emitted too
static void paint(Walk* me, Symbol const* symbol) {
    if (!symbol || (symbol->flags & SYMBOL_TYPE_PARAMETER) || symbol->is_parameter) return;
    // An exported one is visible. A local type of its name in the file is the same symbol to tsc (`export const K`
    // and `type K`: the binder merges the type into the export's local symbol), whose declarations
    // isEntityNameVisible makes visible together
    if (symbol->exported) {
        Symbol const* local = my file->scope ? scope_resolve(my file->scope, symbol->name, SYMBOL_TYPE) : nullptr;
        if (local && local != symbol && !local->exported && (local->flags & (SYMBOL_TYPE_ALIAS | SYMBOL_INTERFACE))) {
            paint(me, local);
        }
        return;
    }
    bool here = false;
    for (u32 i = 0; i < symbol->decl_count && !here; i++) here = span_source(symbol->decls[i].name) == my file;
    if (!here) return;
    for (u32 i = 0; i < my painted_count; i++) {
        if (my painted[i] == symbol) return;
    }
    if (my painted_count == my painted_cap) {
        my painted_cap = my painted_cap ? my painted_cap * 2 : 8;
        my painted = realloc(my painted, my painted_cap * sizeof(*my painted));
        my painted_done = realloc(my painted_done, my painted_cap * sizeof(*my painted_done));
    }
    my painted_done[my painted_count] = false;
    my painted[my painted_count++] = symbol;
}

// Whether a painted declaration's statement is still to be walked (it is from now on)
static bool take_painted(Walk* me, Symbol const* symbol) {
    for (u32 i = 0; symbol && i < my painted_count; i++) {
        if (my painted[i] != symbol || my painted_done[i]) continue;
        my painted_done[i] = true;
        return true;
    }
    return false;
}

static bool is_painted_pending(Walk* me, Symbol const* symbol) {
    for (u32 i = 0; symbol && i < my painted_count; i++) {
        if (my painted[i] == symbol) return !my painted_done[i];
    }
    return false;
}

// errorNameNode or errorFallbackNode, and errorDeclarationNameWithFallback
static Span error_name_node(Walk* me, csview* name) {
    Emitting const* e = &my emitting;
    if (span_valid(e->name) && e->name_not_node) {
        *name = span_sv(e->name);
        return span_valid(e->fallback) ? e->fallback : e->at;
    }
    if (span_valid(e->name)) {
        *name = span_sv(e->name);
        return e->name;
    }
    *name = e->kind == EMIT_DEFAULT_EXPORT ? c_sv("default") : c_sv("(Missing)");
    return span_valid(e->fallback) ? e->fallback : e->at;
}

// An error of the declaration being emitted at errorNameNode (once)
static Error* report_once(Walk* me, Span at, unsigned code, cstr message) {
    for (isize_t i = my emitting_errors; i < vec_error_size(&my errs->errors); i++) {
        Error const* other = vec_error_at(&my errs->errors, i);
        if (other->ts_code == code && span_eq(other->span, at) && cstr_equals_sv(&other->message, cstr_sv(&message))) {
            cstr_drop(&message);
            return nullptr;
        }
    }
    return error_ctx_error_s(my errs, at, code, message);
}

// The symbol tracker's other reports
static void tracker_report(DeclTracker* tracker, DeclTrackReport kind, csview property) {
    Walk* me = tracker->ctx;
    if (my emitting.kind == EMIT_NONE) return;
    csview name;
    Span at = error_name_node(me, &name);
    switch (kind) {
        case DECL_REPORT_PRIVATE_IN_CLASS_LITERAL: {
            Error* error = report_once(me, at, 4094, cstr_d("Property '", property,
                "' of exported anonymous class type may not be private or protected."));
            if (error && my emitting.kind == EMIT_VARIABLE) {
                error_add_related_d(error, at, 9027, "Add a type annotation to the variable ", name, ".");
            }
            return;
        }
        case DECL_REPORT_UNIQUE_SYMBOL:
        case DECL_REPORT_THIS:
            report_once(me, at, 2527, cstr_d("The inferred type of '", name, "' references an inaccessible '",
                kind == DECL_REPORT_THIS ? "this" : "unique symbol", "' type. A type annotation is necessary."));
            return;
        case DECL_REPORT_TRUNCATION:
            report_once(me, at, 7056, cstr_from("The inferred type of this node exceeds the maximum length the compiler will "
                "serialize. An explicit type annotation is needed."));
            return;
        case DECL_REPORT_CYCLIC:
            report_once(me, at, 5088, cstr_d("The inferred type of '", name, "' references a type with a cyclic structure which "
                "cannot be trivially serialized. A type annotation is necessary."));
            return;
        case DECL_REPORT_NON_SERIALIZABLE:
            report_once(me, at, 4118, cstr_d("The type of this node cannot be serialized because its property '", property,
                "' cannot be serialized."));
            return;
    }
}

static void track_symbol(DeclTracker* tracker, Symbol const* symbol, u32 meaning);

static bool reaches_into_node_modules(cstr const* specifier) {
    return csview_find_sv(cstr_sv(specifier), c_sv("/node_modules/")) != c_NPOS;
}

// symbolToTypeNode's import type: TS2883 when its specifier can only reach the module through a node_modules (in
// node16/nodenext, unless the other resolution mode's specifier can, with a `resolution-mode` attribute)
static cstr unsafe_specifier(Walk* me, SourceFile const* target) {
    // (A module named by the augmentation of it: tryGetModuleNameFromAmbientModule)
    if (target->scope && target->scope->container && module_ambient_name(target->scope->container).size) return cstr_null;
    CompilerOptions const* o = compiler_options();
    bool node_next = o->module_resolution == MODULE_RESOLUTION_NODE16 || o->module_resolution == MODULE_RESOLUTION_NODENEXT;
    Program* program = my checker->program;
    bool file_esm = my file->has_format && my file->is_esm;
    SpecifierMode override = SPECIFIER_MODE_NONE;
    // (An import type of an ES module from another format says so)
    if (node_next && target->has_format && target->is_esm && !file_esm) override = SPECIFIER_MODE_ESM;
    // (With that attribute it's never reported, whatever the specifier: tsgo's symbolToTypeNode checks importModeOverride,
    // which the attribute already set)
    if (override != SPECIFIER_MODE_NONE) return cstr_null;
    cstr specifier = program_module_specifier(program, my file, target, override);
    if (!reaches_into_node_modules(&specifier)) {
        cstr_drop(&specifier);
        return cstr_null;
    }
    if (node_next) {
        cstr swapped = program_module_specifier(program, my file, target, file_esm ? SPECIFIER_MODE_COMMONJS : SPECIFIER_MODE_ESM);
        bool portable = !reaches_into_node_modules(&swapped);
        cstr_drop(&swapped);
        if (portable) {
            cstr_drop(&specifier);
            return cstr_null;
        }
    }
    return specifier;
}

static SpecifierMemo* specifier_memo(Walk* me, SourceFile const* target) {
    for (u32 i = 0; i < my specifier_count; i++) {
        if (my specifiers[i].target == target) return &my specifiers[i];
    }
    if (my specifier_count == my specifier_cap) {
        my specifier_cap = my specifier_cap ? my specifier_cap * 2 : 4;
        my specifiers = realloc(my specifiers, my specifier_cap * sizeof(*my specifiers));
    }
    my specifiers[my specifier_count] = (SpecifierMemo){ .target = target };
    return &my specifiers[my specifier_count++];
}

// symbol_chain_set_module_specifier()'s hook: the containers a symbol can be named through are tried by their names
static cstr walk_module_specifier(void* ctx, SourceFile const* target) {
    Walk* me = ctx;
    SpecifierMemo* memo = specifier_memo(me, target);
    if (!memo->has_plain) {
        memo->plain = program_module_specifier(my checker->program, my file, target, SPECIFIER_MODE_NONE);
        memo->has_plain = true;
    }
    return cstr_clone(memo->plain);
}

static void track_module_file(DeclTracker* tracker, SourceFile const* target, Symbol const* symbol) {
    Walk* me = tracker->ctx;
    if (my emitting.kind == EMIT_NONE || target == my file) return;
    SpecifierMemo* memo = specifier_memo(me, target);
    if (!memo->has_unsafe) {
        memo->unsafe = unsafe_specifier(me, target);
        memo->has_unsafe = true;
    }
    if (!cstr_size(&memo->unsafe)) return;
    csview name;
    Span at = error_name_node(me, &name);
    report_once(me, at, 2883, cstr_d("The inferred type of '", name, "' cannot be named without a reference to '",
        symbol->name, "' from '", memo->unsafe, "'. This is likely not portable. A type annotation is necessary."));
}

// trackComputedName: the first identifier of a late bound name as resolved from here, else as resolved where the name
// is declared (which is a module's local there, never accessible from another file)
static void track_computed_name(DeclTracker* tracker, csview name, SourceFile const* file) {
    Walk* me = tracker->ctx;
    char const* c = name.buf + (name.size && name.buf[0] == '[');
    char const* end = name.buf + name.size;
    while (c < end && (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r')) c++;
    char const* start = c;
    while (c < end && *c != '.' && *c != ']' && *c != ' ' && *c != '[' && *c != '(') c++;
    csview first = csview_from_n(start, c - start);
    if (!first.size) return;
    Symbol const* found = scope_resolve(my scope, first, SYMBOL_VALUE);
    if (found) {
        track_symbol(tracker, found, SYMBOL_VALUE);
        return;
    }
    // (A function's parameters are in scope in its return type, tsc's enclosingDeclaration being the function, and as
    // visible as the function: `{ [field]: T }`)
    if (my root.kind == ROOT_FUNCTION && my root.function) {
        for_each (i, vec_function_arg, &my root.function->args) {
            Binding const* binding = &i->initializer.binding;
            if (binding->tag == BINDING_IDENTIFIER && csview_equals_sv(span_sv(binding->content_span), first)) return;
        }
    }
    Scope const* module = file && file->scope ? scope_external_module(file->scope) : nullptr;
    if (module && module != scope_external_module(my scope)) {
        cstr module_name = module_name_for_error(module->container, nullptr, false);
        report_access(me, SYMBOL_CANNOT_BE_NAMED, first, cstr_sv(&module_name), true, span_null);
        cstr_drop(&module_name);
    } else {
        report_access(me, SYMBOL_NOT_ACCESSIBLE, first, c_sv(""), false, span_null);
    }
}

// trackSymbol: isSymbolAccessible from where the declaration is emitted, and its diagnostic
static void track_symbol(DeclTracker* tracker, Symbol const* symbol, u32 meaning) {
    Walk* me = tracker->ctx;
    if (symbol->flags & SYMBOL_TYPE_PARAMETER) return;
    if (my cjs_watched_class && symbol == my cjs_watched_class) my cjs_class_tracked = true;
    SymbolAccess access = symbol_accessibility_at(my scope, symbol, meaning);
    if (access.kind == SYMBOL_ACCESSIBLE) {
        paint(me, access.head);
        return;
    }
    // (symbolToString from the enclosing declaration)
    SymbolChain chain = symbol_chain_at(my scope, symbol, meaning, false);
    cstr name = cstr_init();
    symbol_chain_append(&name, my scope, &chain);
    cstr module = cstr_init();
    if (access.module && access.kind == SYMBOL_CANNOT_BE_NAMED) {
        cstr_drop(&module);
        module = module_name_for_error(access.module, nullptr, false);
    } else if (access.module) {
        SymbolChain module_chain = symbol_chain_at(my scope, access.module, SYMBOL_NAMESPACE, false);
        symbol_chain_append(&module, my scope, &module_chain);
    }
    report_access(me, access.kind, cstr_sv(&name), cstr_sv(&module), access.module != nullptr, span_null);
    c_drop(cstr, &name, &module);
}

// A module's export that merges `export { x }` with declarations of its own (a JavaScript module's dotted `@typedef`
// namespaces `x.T` next to `const x`): what the specifier names, when every other declaration is visible. tsgo's
// binder merges those namespaces into x's local symbol, which the name resolves to; here the module's export has them
static Symbol const* export_specifier_local(Symbol const* symbol) {
    if (!(symbol->flags & SYMBOL_ALIAS) || !symbol->exported) return nullptr;
    bool specifier = false, others = false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (decl->kind == SYMBOL_DECL_ALIAS && decl->node && ((PortItem const*)decl->node)->tag == PORT_EXPORT_SPECIFIC) {
            specifier = true;
        } else if (decl->kind == SYMBOL_DECL_NAMESPACE) {
            others = true;
        } else {
            return nullptr;
        }
    }
    if (!specifier || !others) return nullptr;
    Symbol const* target = symbol_resolve_alias((Symbol*)symbol);
    return target && target != symbol ? target : nullptr;
}

// tsc's isEntityNameVisible of a name in a written type, and handleSymbolAccessibilityError: `symbol` is what its first
// identifier resolved to (null: unresolved, which the checker reports)
static void check_entity_name(Walk* me, Symbol const* symbol, Span first, u32 meaning) {
    if (!symbol || my emitting.kind == EMIT_NONE) return;
    if (symbol->flags & SYMBOL_TYPE_PARAMETER) return;
    if (my reusing) {
        if (symbol->is_parameter) return; // (resolvable here, so visible)
        SymbolAccess access = symbol_accessibility_at(my scope, symbol, meaning);
        if (access.kind != SYMBOL_ACCESSIBLE) my fell_back = true;
        else paint(me, access.head);
        return;
    }
    if (symbol_has_visible_declarations(symbol)) {
        paint(me, symbol);
        return;
    }
    Symbol const* local = export_specifier_local(symbol);
    if (local && symbol_has_visible_declarations(local)) {
        paint(me, local);
        return;
    }
    report_access(me, SYMBOL_NOT_ACCESSIBLE, span_sv(first), c_sv(""), false, first);
}

static Expr const* entity_leftmost(Expr const* expr) {
    while (true) {
        if (expr->tag == EXPR_PROPERTY) expr = &expr->property->expr;
        else if (expr->tag == EXPR_GENERIC_PARAMS) expr = &expr->generic_params->expr;
        else return expr;
    }
}

// The first identifier of an entity name expression (`a` of `a.b.c`), checked
static void check_entity_expression(Walk* me, Expr const* expr) {
    Expr const* first = entity_leftmost(expr);
    if (first->tag != EXPR_IDENTIFIER || first->identifier.this_kind) return;
    check_entity_name(me, first->identifier.symbol, first->inner_span, SYMBOL_VALUE);
}

// Diagnostics

static char const* related_message(NearKind kind, unsigned* code) {
    switch (kind) {
        case NEAR_VARIABLE: *code = 9027; return "Add a type annotation to the variable ";
        case NEAR_PARAMETER: *code = 9028; return "Add a type annotation to the parameter ";
        case NEAR_PROPERTY: *code = 9029; return "Add a type annotation to the property ";
        case NEAR_FUNCTION_EXPRESSION: *code = 9030; return "Add a return type to the function expression.";
        case NEAR_FUNCTION_DECLARATION: *code = 9031; return "Add a return type to the function declaration.";
        case NEAR_GET_ACCESSOR: *code = 9032; return "Add a return type to the get accessor declaration.";
        case NEAR_SET_ACCESSOR: *code = 9033; return "Add a type to parameter of the set accessor declaration.";
        case NEAR_METHOD: *code = 9034; return "Add a return type to the method";
        case NEAR_EXPORT_ASSIGNMENT:
            *code = 9036;
            return "Move the expression in default export to a variable and add a type annotation to it.";
        case NEAR_NONE: break;
    }
    impossible;
}

// relatedSuggestionByDeclarationKind
static void add_related(Error* error, Near near) {
    if (!error || near.kind == NEAR_NONE) return;
    unsigned code = 0;
    char const* message = related_message(near.kind, &code);
    bool named = near.kind == NEAR_VARIABLE || near.kind == NEAR_PARAMETER || near.kind == NEAR_PROPERTY;
    if (named) {
        error_add_related_d(error, near.span, code, message, near.name, ".");
    } else {
        error_add_related_d(error, near.span, code, message);
    }
}

static Error* report_s(Walk* me, Span at, unsigned code, cstr message) {
    my fell_back = true;
    if (!my isolated) {
        cstr_drop(&message);
        return nullptr;
    }
    return error_ctx_error_s(my errs, at, code, message);
}

static Error* report(Walk* me, Span at, unsigned code, char const* message) { return report_s(me, at, code, cstr_from(message)); }

#define report_d(me, at, code, ...) report_s(me, at, code, cstr_d(__VA_ARGS__))

static char const* const msg_expression = "Expression type can't be inferred with --isolatedDeclarations.";

// errorByDeclarationKind of what `near` is
static void kind_error(NearKind kind, unsigned* code, char const** message) {
    switch (kind) {
        case NEAR_VARIABLE:
            *code = 9010, *message = "Variable must have an explicit type annotation with --isolatedDeclarations.";
            return;
        case NEAR_PARAMETER:
            *code = 9011, *message = "Parameter must have an explicit type annotation with --isolatedDeclarations.";
            return;
        case NEAR_PROPERTY:
            *code = 9012, *message = "Property must have an explicit type annotation with --isolatedDeclarations.";
            return;
        case NEAR_EXPORT_ASSIGNMENT:
            *code = 9037, *message = "Default exports can't be inferred with --isolatedDeclarations.";
            return;
        case NEAR_FUNCTION_EXPRESSION:
        case NEAR_FUNCTION_DECLARATION:
            *code = 9007;
            *message = "Function must have an explicit return type annotation with --isolatedDeclarations.";
            return;
        case NEAR_METHOD:
            *code = 9008, *message = "Method must have an explicit return type annotation with --isolatedDeclarations.";
            return;
        case NEAR_GET_ACCESSOR:
        case NEAR_SET_ACCESSOR:
            *code = 9009;
            *message = "At least one accessor must have an explicit type annotation with --isolatedDeclarations.";
            return;
        case NEAR_NONE: break;
    }
    impossible;
}

static void report_heritage(Walk* me, Span at) {
    report(me, at, 9021, "Extends clause can't contain an expression with --isolatedDeclarations.");
}

// createExpressionError; code 0: the message by what the expression is the initializer of
static void report_expression(Walk* me, Span at, Place place, unsigned code, char const* message) {
    if (place.near.kind == NEAR_NONE) {
        report(me, at, code ? code : 9013, code ? message : msg_expression);
        return;
    }
    if (place.direct) {
        if (!code) kind_error(place.near.kind, &code, &message);
        add_related(report(me, at, code, message), place.near);
        return;
    }
    Error* error = report(me, at, code ? code : 9013, code ? message : msg_expression);
    if (!error) return;
    add_related(error, place.near);
    error_add_related_d(error, at, 9035,
        "Add satisfies and a type assertion to this expression (satisfies T as T) to make the type explicit.");
}

// createObjectLiteralError, createArrayLiteralError: the error and the nearest declaration
static void report_with_near(Walk* me, Span at, Place place, unsigned code, char const* message) {
    add_related(report(me, at, code, message), place.near);
}

// createVariableOrPropertyError
static void report_declaration(Walk* me, Near near) {
    unsigned code = 0;
    char const* message = nullptr;
    kind_error(near.kind, &code, &message);
    add_related(report(me, near.span, code, message), near);
}

// Syntax helpers

static bool is_numeric_literal(Expr const* expr) {
    return expr->tag == EXPR_LITERAL && expr->literal.tag == VALUE_NUMBER;
}

static bool is_bigint_literal(Expr const* expr) {
    return expr->tag == EXPR_LITERAL && expr->literal.tag == VALUE_BIGINT;
}

// tsc's isPrimitiveLiteralValue (parentheses are seen through: tttt has no node for them)
static bool is_primitive_literal_value(Expr const* expr, bool include_bigint) {
    switch (expr->tag) {
        case EXPR_LITERAL:
            switch (expr->literal.tag) {
                case VALUE_BOOLEAN:
                case VALUE_NUMBER:
                case VALUE_STRING: return true;
                case VALUE_BIGINT: return include_bigint;
                default: return false;
            }
        case EXPR_UNARY_OP: {
            Expr const* operand = &expr->unary_op->expr;
            if (expr->unary_op->op == UNARY_OP_MINUS) {
                return is_numeric_literal(operand) || (include_bigint && is_bigint_literal(operand));
            }
            return expr->unary_op->op == UNARY_OP_PLUS && is_numeric_literal(operand);
        }
        default: return false;
    }
}

// tsc's isDefinitelyReferenceToGlobalSymbolObject: exactly `Symbol.x` (or `globalThis.Symbol.x`) with the global Symbol
static bool is_global_symbol_reference(Walk* me, Expr const* expr) {
    if (expr->tag != EXPR_PROPERTY || expr->property->form != PROPERTY_EXPR_DOT) return false;
    Expr const* object = &expr->property->expr;
    if (object->tag == EXPR_PROPERTY) {
        if (object->property->form != PROPERTY_EXPR_DOT || object->property->expr.tag != EXPR_IDENTIFIER) return false;
        bool symbol = csview_equals(name_cooked(span_sv(property_name(object->property))), "Symbol");
        IdentifierExpr const* global = &object->property->expr.identifier;
        return symbol && csview_equals(span_sv(identifier_expr_name(global)), "globalThis") && global->target && global->target->is_global_this;
    }
    if (object->tag != EXPR_IDENTIFIER || !csview_equals(span_sv(object->inner_span), "Symbol")) return false;
    Symbol const* global = program_get_global(my checker->program, c_sv("Symbol"), SYMBOL_VALUE);
    return !object->identifier.target || object->identifier.target == global;
}

// tsc's isDynamicName of a computed property name's expression
static bool is_dynamic_name(Expr const* key) {
    if (!key) return false;
    if (key->tag == EXPR_LITERAL && (key->literal.tag == VALUE_STRING || key->literal.tag == VALUE_NUMBER)) return false;
    if (key->tag == EXPR_UNARY_OP && key->unary_op->op == UNARY_OP_MINUS && is_numeric_literal(&key->unary_op->expr)) {
        return false;
    }
    return true;
}

// The syntactic builder (tsgo's pseudochecker, pseudo.c) and what the node builder reports from it

static void check_written_type(Walk* me, TypeNode const* type);
static void check_written_signature(Walk* me, FunctionImpl const* fn, EmitSig sig, bool declaration);
static void check_generic(Walk* me, Generic const* generic, EmitSig sig);

// The kind of `near` a function is
static NearKind function_kind(FunctionImpl const* fn) {
    if (fn->accessor == 1) return NEAR_GET_ACCESSOR;
    if (fn->accessor == 2) return NEAR_SET_ACCESSOR;
    if (fn->is_method) return NEAR_METHOD;
    if (fn->is_expression) return NEAR_FUNCTION_EXPRESSION;
    return NEAR_FUNCTION_DECLARATION;
}

static Near parameter_near(FunctionArg const* arg) {
    return (Near){ .kind = NEAR_PARAMETER, .span = arg->span, .name = span_sv(arg->initializer.binding.content_span) };
}

static Near variable_near(Binding const* binding) {
    return (Near){ .kind = NEAR_VARIABLE, .span = binding->content_span, .name = span_sv(binding->content_span) };
}

static Near field_near(ClassItem const* field) {
    return (Near){ .kind = NEAR_PROPERTY, .span = field->field.name_span, .name = span_sv(field->field.name_span) };
}

// Finding a reported node in the declaration being serialized: the descent of the pseudochecker's builder (pseudo.c),
// keeping what tsc's findNearestDeclaration and createExpressionError's parent would see
typedef struct Search {
    PseudoError const* target;
    Place found;
    ObjectExpr const* object; // The object literal the target is a member (or a member's function) of
    NearKind function_kind; // The target is a function: what kind (an object literal's method isn't is_method)
    bool done;
} Search;

static bool search_found(Search* me, Near near, bool direct) {
    my found = (Place){ .near = near, .direct = direct };
    my done = true;
    return true;
}

static bool target_is_expr(PseudoError const* target, Expr const* expr) {
    switch (target->kind) {
        case PSEUDO_ERROR_ARRAY:
        case PSEUDO_ERROR_SPREAD_ELEMENT:
        case PSEUDO_ERROR_CLASS:
        case PSEUDO_ERROR_EXPRESSION: return target->expr == expr;
        default: return false;
    }
}

static bool target_is_function(PseudoError const* target, FunctionImpl const* fn) {
    return (target->kind == PSEUDO_ERROR_FUNCTION || target->kind == PSEUDO_ERROR_ACCESSOR) && target->function == fn;
}

static bool search_function(Search* me, FunctionImpl const* fn, Near near, NearKind kind);

// `direct`: the expression is `near`'s initializer (through type assertions)
static bool search_expr(Search* me, Expr const* expr, Near near, bool direct) {
    if (target_is_expr(my target, expr)) return search_found(me, near, direct);
    switch (expr->tag) {
        case EXPR_AS_TYPE: return search_expr(me, &expr->as_type->expr, near, direct);
        case EXPR_OBJECT:
            for_each (i, vec_object_expr_item, &expr->object) {
                bool member = my target->kind == PSEUDO_ERROR_OBJECT_MEMBER || my target->kind == PSEUDO_ERROR_COMPUTED_NAME;
                if (member && my target->item == i) {
                    my object = &expr->object;
                    return search_found(me, near, false);
                }
                if (i->tag == OBJECT_ITEM_PROPERTY && search_expr(me, &i->value, near, false)) return true;
                bool function = i->tag == OBJECT_ITEM_METHOD || i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER;
                if (function && i->value.tag == EXPR_FUNCTION) {
                    if (target_is_function(my target, i->value.function)) my object = &expr->object;
                    NearKind kind = i->tag == OBJECT_ITEM_METHOD ? NEAR_METHOD : function_kind(i->value.function);
                    if (search_function(me, i->value.function, near, kind)) return true;
                }
            }
            return false;
        case EXPR_ARRAY:
            for_each (i, vec_expr, &expr->array) {
                if (search_expr(me, i, near, false)) return true;
            }
            return false;
        case EXPR_FUNCTION: return search_function(me, expr->function, near, function_kind(expr->function));
        default: return false;
    }
}

// `kind`: what the function is (as a nearest declaration)
static bool search_function(Search* me, FunctionImpl const* fn, Near near, NearKind kind) {
    if (target_is_function(my target, fn)) {
        my function_kind = kind;
        return search_found(me, near, false);
    }
    for_each (i, vec_function_arg, &fn->args) {
        if (my target->kind == PSEUDO_ERROR_PARAMETER && my target->arg == i) return search_found(me, near, false);
        Expr const* init = initialized_binding_value(&i->initializer);
        if (init->tag != EXPR_IMPLICIT_ANY && search_expr(me, init, parameter_near(i), true)) return true;
    }
    if (!fn->has_body || fn->body_missing) return false;
    if (fn->expression_body) {
        Statement const* ret = vec_statement_at(&function_impl_body(fn)->statements, 0);
        return ret && ret->tag == STATEMENT_RETURN_EXPR && search_expr(me, &ret->expr, near, false);
    }
    // (A `return` directly in the body: the function is the nearest declaration of what it returns)
    for_each (i, vec_statement, &function_impl_body(fn)->statements) {
        if (i->tag != STATEMENT_RETURN_EXPR || statement_is_bare_return(i)) continue;
        if (search_expr(me, &i->expr, (Near){ .kind = kind, .span = function_error_span(fn) }, false)) return true;
    }
    return false;
}

// Where a reported node is in the declaration being serialized (nowhere: a node of some other declaration)
static Search locate(Walk* me, PseudoError const* target) {
    Search search = { .target = target };
    Root const* root = &my root;
    switch (root->kind) {
        case ROOT_VARIABLE: {
            Expr const* init = initialized_binding_value(root->binding);
            if (init->tag != EXPR_IMPLICIT_ANY) search_expr(&search, init, variable_near(&root->binding->binding), true);
            break;
        }
        case ROOT_FIELD:
            if (root->field->field.value.tag != EXPR_IMPLICIT_ANY) {
                search_expr(&search, &root->field->field.value, field_near(root->field), true);
            }
            break;
        case ROOT_FUNCTION: search_function(&search, root->function, (Near){0}, function_kind(root->function)); break;
        case ROOT_EXPORT_DEFAULT:
            search_expr(&search, root->expr, (Near){ .kind = NEAR_EXPORT_ASSIGNMENT, .span = root->at }, true);
            break;
        case ROOT_NONE: break;
    }
    return search;
}

static bool is_optional_parameter(FunctionImpl const* fn, FunctionArg const* arg);
static bool requires_adding_undefined(Walk* me, FunctionImpl const* fn, FunctionArg const* arg, bool in_function);

// createAccessorTypeError for `at` (one of the pair)
static void report_accessor(Walk* me, Accessors pair, bool on_setter) {
    Span at = on_setter ? pair.setter_name : pair.getter_name;
    if (on_setter && vec_function_arg_size(&pair.setter->args)) at = vec_function_arg_at(&pair.setter->args, 0)->span;
    Error* error = report(me, at, 9009,
        "At least one accessor must have an explicit type annotation with --isolatedDeclarations.");
    if (pair.setter) add_related(error, (Near){ .kind = NEAR_SET_ACCESSOR, .span = pair.setter_name });
    if (pair.getter) add_related(error, (Near){ .kind = NEAR_GET_ACCESSOR, .span = pair.getter_name });
}

// The accessor pair an object literal accessor belongs to (tsc's getAllAccessorDeclarations)
static Accessors object_accessors(ObjectExpr const* object, FunctionImpl const* accessor) {
    ObjectExprItem const* item = nullptr;
    for_each (i, vec_object_expr_item, object) {
        if (i->value.tag == EXPR_FUNCTION && i->value.function == accessor) item = i;
    }
    Accessors pair = {0};
    if (!item) return pair;
    for_each (i, vec_object_expr_item, object) {
        if (i->tag != OBJECT_ITEM_GETTER && i->tag != OBJECT_ITEM_SETTER) continue;
        if (i != item) {
            if (!i->computed_key != !item->computed_key) continue;
            if (i->computed_key ? !csview_equals_sv(span_sv(expr_outer_span(i->computed_key)), span_sv(expr_outer_span(item->computed_key))) :
                !cstr_eq(&i->name_string, &item->name_string)
            ) {
                continue;
            }
        }
        if (i->tag == OBJECT_ITEM_GETTER && !pair.getter) {
            pair.getter = i->value.function;
            pair.getter_name = i->name_span;
        }
        if (i->tag == OBJECT_ITEM_SETTER && !pair.setter) {
            pair.setter = i->value.function;
            pair.setter_name = i->name_span;
        }
    }
    return pair;
}

// createAccessorTypeError of an accessor: its pair is the class accessor being serialized, or found in its object literal
static void report_accessor_of(Walk* me, FunctionImpl const* accessor, Search const* found) {
    Accessors pair = {0};
    if (my class_pair.getter == accessor || my class_pair.setter == accessor) {
        pair = my class_pair;
    } else if (found->object) {
        pair = object_accessors(found->object, accessor);
    }
    if (pair.getter != accessor && pair.setter != accessor) {
        pair = (Accessors){0};
        if (accessor->accessor == 1) pair.getter = accessor, pair.getter_name = function_error_span(accessor);
        else pair.setter = accessor, pair.setter_name = function_error_span(accessor);
    }
    report_accessor(me, pair, pair.setter == accessor);
}

// createReturnTypeError: the function (of `kind`, NEAR_NONE: by itself), with its nearest declaration and itself as
// related information
static void report_return(Walk* me, FunctionImpl const* fn, Near near, Span at, NearKind kind) {
    unsigned code = 0;
    char const* message = nullptr;
    if (kind == NEAR_NONE) kind = function_kind(fn);
    kind_error(kind, &code, &message);
    if (!span_valid(at)) at = function_error_span(fn);
    Error* error = report(me, at, code, message);
    add_related(error, near);
    add_related(error, (Near){ .kind = kind, .span = at });
}

// createParameterError
static void report_parameter(Walk* me, FunctionImpl const* fn, FunctionArg const* arg) {
    if (fn->accessor == 2) {
        Search none = {0};
        report_accessor_of(me, fn, &none);
        return;
    }
    Expr const* init = initialized_binding_value(&arg->initializer);
    bool add_undefined = requires_adding_undefined(me, fn, arg, true);
    if (!add_undefined && init->tag != EXPR_IMPLICIT_ANY) {
        report_expression(me, expr_error_span(init), (Place){ .near = parameter_near(arg), .direct = true }, 0, nullptr);
        return;
    }
    if (add_undefined) {
        add_related(report(me, arg->span, 9025, "Declaration emit for this parameter requires implicitly adding "
            "undefined to its type. This is not supported with --isolatedDeclarations."), parameter_near(arg));
        return;
    }
    report_declaration(me, parameter_near(arg));
}

// Somewhere in the reported node
static Span reported_span(PseudoError const* node) {
    switch (node->kind) {
        case PSEUDO_ERROR_OBJECT_MEMBER:
        case PSEUDO_ERROR_COMPUTED_NAME: return node->item->span;
        case PSEUDO_ERROR_ARRAY:
        case PSEUDO_ERROR_SPREAD_ELEMENT:
        case PSEUDO_ERROR_CLASS:
        case PSEUDO_ERROR_EXPRESSION: return node->expr->inner_span;
        case PSEUDO_ERROR_ACCESSOR:
        case PSEUDO_ERROR_FUNCTION: return function_error_span(node->function);
        case PSEUDO_ERROR_PARAMETER: return node->arg->span;
        case PSEUDO_ERROR_VARIABLE: return node->binding->binding.content_span;
        case PSEUDO_ERROR_FIELD: return node->field->field.name_span;
    }
    impossible;
}

// tsgo's SymbolTracker.ReportInferenceFallback: the declaration emitter's isolatedDeclarations diagnostic for the node
// (createGetIsolatedDeclarationErrors), by its kind
static void report_fallback(Walk* me, PseudoError const* node) {
    if (!my isolated) return;
    // (A node of another file is that file's declaration emit's to report)
    if (span_source(reported_span(node)) != my file) return;
    Search found = locate(me, node);
    Place place = found.found;
    switch (node->kind) {
        case PSEUDO_ERROR_OBJECT_MEMBER: {
            ObjectExprItem const* item = node->item;
            switch (item->tag) {
                case OBJECT_ITEM_SHORTHAND:
                    report_with_near(me, item->name_span, place, 9016,
                        "Objects that contain shorthand properties can't be inferred with --isolatedDeclarations.");
                    return;
                case OBJECT_ITEM_SPREAD:
                    report_with_near(me, item->span, place, 9015,
                        "Objects that contain spread assignments can't be inferred with --isolatedDeclarations.");
                    return;
                case OBJECT_ITEM_PROPERTY:
                    // (createExpressionError of its initializer, which isn't its nearest declaration's)
                    report_expression(me, expr_error_span(&item->value), (Place){ .near = place.near }, 0, nullptr);
                    return;
                case OBJECT_ITEM_METHOD:
                    if (item->value.tag == EXPR_FUNCTION) report_return(me, item->value.function, place.near, (Span){0}, NEAR_METHOD);
                    return;
                case OBJECT_ITEM_GETTER:
                case OBJECT_ITEM_SETTER:
                    if (item->value.tag == EXPR_FUNCTION) report_accessor_of(me, item->value.function, &found);
                    return;
                default: return;
            }
        }
        case PSEUDO_ERROR_COMPUTED_NAME:
            report_with_near(me, node->item->name_span, place, 9038,
                "Computed property names on class or object literals cannot be inferred with --isolatedDeclarations.");
            return;
        case PSEUDO_ERROR_ARRAY:
            report_with_near(me, expr_error_span_skip_parens(node->expr), place, 9017, "Only const arrays can be inferred with --isolatedDeclarations.");
            return;
        case PSEUDO_ERROR_SPREAD_ELEMENT:
            report_with_near(me, expr_error_span_skip_parens(node->expr), place, 9018,
                "Arrays with spread elements can't inferred with --isolatedDeclarations.");
            return;
        case PSEUDO_ERROR_CLASS:
            report_expression(me, expr_error_span_skip_parens(node->expr), place, 9022,
                "Inference from class expressions is not supported with --isolatedDeclarations.");
            return;
        case PSEUDO_ERROR_EXPRESSION:
            // (By the expression's kind, like any reported node)
            switch (node->expr->tag) {
                case EXPR_ARRAY: report_fallback(me, &(PseudoError){ .kind = PSEUDO_ERROR_ARRAY, .expr = node->expr }); return;
                case EXPR_SPREAD:
                    report_fallback(me, &(PseudoError){ .kind = PSEUDO_ERROR_SPREAD_ELEMENT, .expr = node->expr });
                    return;
                case EXPR_CLASS_DECL: report_fallback(me, &(PseudoError){ .kind = PSEUDO_ERROR_CLASS, .expr = node->expr }); return;
                case EXPR_FUNCTION:
                    report_fallback(me, &(PseudoError){ .kind = PSEUDO_ERROR_FUNCTION, .function = node->expr->function });
                    return;
                default: break;
            }
            if (pseudo_is_entity_name(node->expr)) {
                // createEntityInTypeNodeError
                Span at = expr_error_span_skip_parens(node->expr);
                add_related(report_d(me, at, 9039, "Type containing private name '", span_sv(at),
                    "' can't be used with --isolatedDeclarations."), place.near);
                return;
            }
            report_expression(me, expr_error_span_skip_parens(node->expr), place, 0, nullptr);
            return;
        case PSEUDO_ERROR_ACCESSOR:
        case PSEUDO_ERROR_FUNCTION:
            if (node->function->accessor) {
                report_accessor_of(me, node->function, &found);
                return;
            }
            report_return(me, node->function, place.near,
                my root.kind == ROOT_FUNCTION && my root.function == node->function ? my root.at : (Span){0}, found.function_kind);
            return;
        case PSEUDO_ERROR_PARAMETER: report_parameter(me, node->function, node->arg); return;
        case PSEUDO_ERROR_VARIABLE: report_declaration(me, variable_near(&node->binding->binding)); return;
        case PSEUDO_ERROR_FIELD: report_declaration(me, field_near(node->field)); return;
    }
}

static void report_fallback_callback(void* ctx, PseudoError const* node) { report_fallback(ctx, node); }

static void tracker_report_fallback(DeclTracker* tracker, PseudoError const* node) { report_fallback(tracker->ctx, node); }

static PseudoReporter walk_reporter(Walk* me) { return (PseudoReporter){ .report = report_fallback_callback, .ctx = me }; }

// A type node the pseudo type reuses: a name in it that isn't accessible from here makes the declaration print its type
static void reuse_type(Walk* me, TypeNode const* type) {
    bool outer_reusing = my reusing;
    my reusing = true;
    check_written_type(me, type);
    my reusing = outer_reusing;
}

static void reuse_signature(Walk* me, FunctionImpl const* fn) {
    bool outer_reusing = my reusing;
    my reusing = true;
    check_generic(me, &fn->generic, SIG_FUNCTION);
    my reusing = outer_reusing;
}

static void pseudo_to_node(Walk* me, Pseudo const* pseudo);

// What the syntax doesn't say is printed as the checker's type: pseudoTypeToNode's serializeTypeForDeclaration of the
// expression's declaration, whose own pseudo types report, or of a return type (serializeReturnTypeForSignature without
// reuse: serializeInferredReturnTypeForSignature, which doesn't). `in_return`: the expression is a returned one
static void print_part(Walk* me, Pseudo const* pseudo, bool in_return) {
    Ty const* type = ty_pseudo_type(my checker, pseudo);
    if (type) print_type(me, type, !in_return && !pseudo->signature_return);
}

// pseudo_to_node() of a pseudo type whose parent is a declaration (see pseudo_report_inferred_node()), or that is a
// function's return type (`in_return`)
static void pseudo_to_node_in(Walk* me, Pseudo const* pseudo, ObjectExprItem const* property, FunctionImpl const* function,
    bool in_return
) {
    if (pseudo->kind != PSEUDO_INFERRED) {
        pseudo_to_node(me, pseudo);
        return;
    }
    PseudoReporter reporter = walk_reporter(me);
    pseudo_report_inferred_node(&reporter, pseudo, property, function);
    print_part(me, pseudo, in_return);
}

static void check_pattern_keys(Walk* me, Binding const* pattern);

static void pseudo_params_to_node(Walk* me, PseudoParam const* params, u32 count) {
    for (u32 i = 0; i < count; i++) {
        if (params[i].arg) check_pattern_keys(me, &params[i].arg->initializer.binding);
        pseudo_to_node(me, params[i].type);
    }
}

// pseudoTypeToNode, for what it reports (nothing is printed: the reused nodes' names are checked, see reuse_type())
static void pseudo_to_node(Walk* me, Pseudo const* pseudo) {
    switch (pseudo->kind) {
        case PSEUDO_DIRECT:
            if (!pseudo->predicate) reuse_type(me, pseudo->node);
            else if (pseudo->function->predicate->has_type) reuse_type(me, &pseudo->function->predicate->type);
            return;
        case PSEUDO_INFERRED: {
            PseudoReporter reporter = walk_reporter(me);
            pseudo_report_inferred_node(&reporter, pseudo, nullptr, nullptr);
            print_part(me, pseudo, false);
            return;
        }
        case PSEUDO_NO_RESULT: {
            PseudoError node = pseudo_no_result_node(pseudo);
            report_fallback(me, &node);
            return;
        }
        case PSEUDO_MAYBE_CONST: pseudo_to_node(me, pseudo->in_const ? pseudo->const_type : pseudo->regular_type); return;
        case PSEUDO_UNION:
        case PSEUDO_TUPLE:
            for (u32 i = 0; i < pseudo->count; i++) pseudo_to_node(me, pseudo->types[i]);
            return;
        case PSEUDO_SIGNATURE:
            reuse_signature(me, pseudo->function);
            pseudo_params_to_node(me, pseudo->params, pseudo->count);
            // (An arrow function's expression body has the function as its parent)
            pseudo_to_node_in(me, pseudo->ret, nullptr, pseudo->function->expression_body ? pseudo->function : nullptr, true);
            return;
        case PSEUDO_OBJECT: {
            bool outer_in_object = my in_object_literal;
            my in_object_literal = true;
            for (u32 i = 0; i < pseudo->count; i++) {
                PseudoMember const* member = &pseudo->members[i];
                // (A computed name is written as it is: its entity named, checkEntityNameVisibility)
                if (member->item->computed_key && pseudo_is_entity_name(member->item->computed_key)) {
                    check_entity_expression(me, member->item->computed_key);
                }
                if (member->function) reuse_signature(me, member->function);
                pseudo_params_to_node(me, member->params, member->param_count);
                bool property = member->kind == PSEUDO_MEMBER_PROPERTY && member->item->tag == OBJECT_ITEM_PROPERTY;
                bool in_return = member->kind == PSEUDO_MEMBER_METHOD || member->kind == PSEUDO_MEMBER_GETTER;
                if (member->type) pseudo_to_node_in(me, member->type, property ? member->item : nullptr, nullptr, in_return);
            }
            my in_object_literal = outer_in_object;
            return;
        }
        default: return;
    }
}

// pseudoTypeToNodeWithCheckerFallback: an Inferred pseudo type reports, and prints the checker's type (`type`)
static void pseudo_to_node_with_fallback(Walk* me, Pseudo const* pseudo, Ty const* type) {
    if (pseudo->kind != PSEUDO_INFERRED) {
        // (Its reports print the types of its parts for the tracker only)
        cstr* outer_type = my type_out;
        cstr* outer_pseudo = my pseudo_out;
        my type_out = my pseudo_out = nullptr;
        pseudo_to_node(me, pseudo);
        my type_out = outer_type;
        my pseudo_out = outer_pseudo;
        if (my pseudo_out) {
            cstr* target = my pseudo_out;
            cstr* outer_type = my type_out;
            // (What the tracker's callbacks serialize meanwhile isn't written)
            my pseudo_out = my type_out = nullptr;
            cstr text = cstr_init();
            TyParamNames names = walk_param_names(me);
            TyDisplay d = emit_display(me, &text);
            d.param_names = &names;
            ty_display_pseudo(my checker, d, pseudo, type);
            ty_param_names_drop(&names);
            cstr_take(target, text);
            my pseudo_out = target;
            my type_out = outer_type;
        }
        return;
    }
    PseudoReporter reporter = walk_reporter(me);
    pseudo_report_inferred(&reporter, pseudo);
    my fell_back = true; // (The declaration's type is printed, not reporting)
}

static bool pseudo_equivalent_reporting(Walk* me, Pseudo const* pseudo, Ty const* type, bool optional) {
    PseudoReporter reporter = { .report = report_fallback_callback, .ctx = me };
    return ty_pseudo_equivalent(my checker, pseudo, type, optional, &reporter);
}

// tsgo's serializeTypeForDeclaration of a declaration whose type isn't written (or a parameter whose annotation lacks
// the undefined declaration emit adds): its pseudo type where that says `type`, else the checker's type (printed).
// `optional`: the declaration has a `?` (its type's undefined isn't the pseudo type's), `add_undefined`: a parameter
// declaration emit writes with undefined (requiresAddingImplicitUndefined)
static void serialize_declaration(Walk* me, Pseudo const* pseudo, Ty const* type, bool optional, bool add_undefined) {
    if (!type) return;
    if (add_undefined && ty_without_undefined(my checker, type) == type) {
        type = ty_union(my checker, (Ty const*[]){ type, ty_intrinsic(my checker, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
    }
    bool outer = fallback_begin(me);
    cstr pseudo_text = cstr_init();
    cstr* outer_pseudo = my pseudo_out;
    my pseudo_out = my type_out ? &pseudo_text : nullptr;
    if (pseudo_equivalent_reporting(me, pseudo, type, optional && !add_undefined)) {
        pseudo_to_node_with_fallback(me, pseudo, type);
    } else {
        my fell_back = true;
        // (Its error nodes reported: the printed type doesn't report again)
        my print_reports = !(pseudo->kind == PSEUDO_INFERRED && pseudo->count);
        // (With undefined added to a pseudo type that can't have it, it may say the type)
        bool should_add = false;
        if (add_undefined) {
            Ty const* as_type = ty_pseudo_type(my checker, pseudo);
            should_add = as_type ? ty_without_undefined(my checker, as_type) == as_type : !pseudo_could_be_undefined(pseudo);
        }
        Pseudo const* with_undefined = should_add ? pseudo_add_undefined(my checker, pseudo) : nullptr;
        if (with_undefined && pseudo_equivalent_reporting(me, with_undefined, type, false)) {
            my fell_back = my print_reports = false;
            pseudo_to_node_with_fallback(me, with_undefined, type);
        }
    }
    my pseudo_out = outer_pseudo;
    if (!fallback_end(me, outer, type) && my type_out) cstr_append_s(my type_out, pseudo_text);
    cstr_drop(&pseudo_text);
}

// tsgo's serializeReturnTypeForSignature of a function without a return type: its pseudo type where that says the
// return type (and its predicate), else the checker's
static void serialize_return(Walk* me, FunctionImpl const* fn, Pseudo const* pseudo) {
    Signature const* sig = ty_function_signature(my checker, fn);
    if (!sig) return;
    Ty const* type = signature_return_type(my checker, sig);
    bool outer = fallback_begin(me);
    cstr pseudo_text = cstr_init();
    cstr* outer_pseudo = my pseudo_out;
    my pseudo_out = my type_out ? &pseudo_text : nullptr;
    if (pseudo_equivalent_reporting(me, pseudo, type, false)) {
        // (The pseudochecker doesn't know inferred type predicates: one is reported at the function)
        TyPredicate const* predicate = signature_predicate(my checker, sig);
        if (predicate && !(pseudo->kind == PSEUDO_DIRECT && pseudo->predicate)) {
            report_fallback(me, &(PseudoError){ .kind = PSEUDO_ERROR_FUNCTION, .function = fn });
            my fell_back = true;
            // (The predicate's type is what w_return() writes: its names are tracked, painting the imports they go
            // through, vscode's `code is CharCode`)
            if (!my type_out && predicate->type) print_type(me, predicate->type, my print_reports);
        } else {
            pseudo_to_node_with_fallback(me, pseudo, type);
        }
    } else {
        my fell_back = true;
    }
    my pseudo_out = outer_pseudo;
    if (!fallback_end(me, outer, type) && my type_out) cstr_append_s(my type_out, pseudo_text);
    cstr_drop(&pseudo_text);
}

static TypeNode const* setter_type(FunctionImpl const* setter) {
    if (!setter || !vec_function_arg_size(&setter->args)) return nullptr;
    FunctionArg const* arg = vec_function_arg_at(&setter->args, 0);
    return arg->initializer.binding.has_explicit_type ? binding_type(&arg->initializer.binding) : nullptr;
}

// tsc's isOptionalParameter: `?`, or an initializer with no required parameter after it
static bool is_optional_parameter(FunctionImpl const* fn, FunctionArg const* arg) {
    if (arg->explicit_optional) return true;
    if (initialized_binding_value(&arg->initializer)->tag == EXPR_IMPLICIT_ANY) return false;
    bool after = false;
    for_each (i, vec_function_arg, &fn->args) {
        if (i == arg) {
            after = true;
            continue;
        }
        if (!after) continue;
        bool initialized = initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY;
        if (!i->explicit_optional && !initialized && !i->rest) return false;
    }
    return true;
}

static bool is_parameter_property(FunctionArg const* arg) { return arg->is_parameter_property || arg->is_readonly_property; }

// tsc's requiresAddingImplicitUndefined of a parameter: declaration emit writes `T | undefined` for it. `in_function`:
// the declaration being emitted is the function (not the class a parameter property is a property of)
static bool requires_adding_undefined(Walk* me, FunctionImpl const* fn, FunctionArg const* arg, bool in_function) {
    if (!my strict_null_checks) return false;
    bool optional = is_optional_parameter(fn, arg);
    bool initialized = initialized_binding_value(&arg->initializer)->tag != EXPR_IMPLICIT_ANY;
    bool required_initialized = !optional && initialized && (!is_parameter_property(arg) || in_function);
    bool optional_property = optional && !initialized && is_parameter_property(arg);
    if (!required_initialized && !optional_property) return false;
    Binding const* binding = &arg->initializer.binding;
    if (!binding->has_explicit_type) return true;
    // declaredParameterTypeContainsUndefined (an error type counts)
    Ty const* type = ty_from_node(my checker, binding_type(binding), (LowerCtx){0});
    if (type->flags & (TYF_ERROR | TYF_UNDEFINED)) return false;
    if (type->kind == TY_UNION) {
        for (u32 i = 0; i < type->composite.types.len; i++) {
            if (type->composite.types.data[i]->flags & TYF_UNDEFINED) return false;
        }
    }
    return true;
}

// The type of a parameter where the node builder writes it: its symbol's, with `undefined` where declaration emit
// adds it (the printer's names are all that matter here)
static Ty const* parameter_type(Walk* me, FunctionImpl const* fn, FunctionArg const* arg) {
    if (arg->initializer.binding.tag == BINDING_IDENTIFIER) {
        Symbol const* symbol = arg->initializer.binding.symbol;
        return symbol ? ty_type_of_symbol(my checker, symbol) : nullptr;
    }
    // (a binding pattern's: its signature parameter's)
    // (Resolved: a function with a written return type may not have needed its parameters yet)
    Signature const* sig = ty_function_signature(my checker, fn);
    if (sig) signature_resolve(my checker, sig);
    for (u32 i = 0; sig && i < sig->param_count; i++) {
        if (sig->params[i].decl == arg) return sig->params[i].type;
    }
    return nullptr;
}

// ensureType of a parameter (ensureParameter): its annotation is written as it is unless declaration emit adds undefined
// to it, else serialized. `in_function`: see requires_adding_undefined()
static void serialize_parameter(Walk* me, FunctionImpl const* fn, FunctionArg const* arg, bool in_function) {
    Binding const* binding = &arg->initializer.binding;
    if (binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this")) return; // (getThisParameter)
    bool add_undefined = requires_adding_undefined(me, fn, arg, in_function);
    if (binding->has_explicit_type && !add_undefined) return; // (check_written_signature() walks it)
    Ty const* type = parameter_type(me, fn, arg);
    if (type) type = ty_widened_literal(my checker, type);
    Pseudo const* pseudo = pseudo_of_parameter(my checker, fn, arg);
    serialize_declaration(me, pseudo, type, arg->explicit_optional, add_undefined);
}

// The parameters of a function the declaration has (tsc's updateParamsList / ensureParameter)
static void serialize_parameters(Walk* me, FunctionImpl const* fn) {
    for_each (i, vec_function_arg, &fn->args) serialize_parameter(me, fn, i, true);
}

// The parameters of a declaration the transformer visits, each with diagnostics of its own
// The entity names of a binding pattern's computed keys, which are written with it (visitDeclarationSubtree's
// checkEntityNameVisibility)
static void check_pattern_keys(Walk* me, Binding const* pattern) {
    if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) check_pattern_keys(me, i);
    } else if (pattern->tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &pattern->object) {
            if (i->computed_key && pseudo_is_entity_name(i->computed_key)) check_entity_expression(me, i->computed_key);
            check_pattern_keys(me, &i->target);
        }
    }
}

static void check_declaration_parameters(Walk* me, FunctionImpl const* fn, EmitSig sig) {
    Root outer_root = my root;
    my root = (Root){ .kind = ROOT_FUNCTION, .function = fn };
    for_each (i, vec_function_arg, &fn->args) {
        Span name = i->initializer.binding.content_span;
        my printed_next = fn;
        Emitting saved = emit_enter(me, EMIT_PARAMETER, sig, name, name);
        check_pattern_keys(me, &i->initializer.binding);
        serialize_parameter(me, fn, i, true);
        emit_leave(me, saved);
    }
    my root = outer_root;
}

// The return type of a declaration's signature (a getter's: of the accessor pair), with the signature's diagnostics.
// `at`: where the function's own diagnostics go, if not function_error_span()
static void check_declaration_return(Walk* me, FunctionImpl const* fn, EmitSig sig, Span name, Span at) {
    if (fn->ret_explicit || fn->predicate) return;
    if (!span_valid(name)) name = fn->name;
    Emitting saved = emit_enter(me, sig == SIG_ACCESSOR ? EMIT_GETTER : EMIT_RETURN, sig, name,
        span_valid(at) ? at : span_valid(name) ? name : function_error_span(fn));
    Root outer_root = my root;
    my root = (Root){ .kind = ROOT_FUNCTION, .function = fn, .at = at };
    Pseudo const* pseudo = fn->accessor == 1 ? pseudo_of_accessors(my checker, fn, my class_pair.setter, false) :
        pseudo_of_return(my checker, fn);
    serialize_return(me, fn, pseudo);
    my root = outer_root;
    emit_leave(me, saved);
}

// Visibility

static bool is_exported_local(Walk* me, Symbol const* symbol) {
    if (!symbol) return false;
    for (u32 i = 0; i < my exported_local_count; i++) {
        if (my exported_locals[i] == symbol) return true;
    }
    return false;
}

// Where a statement list is: what makes its declarations visible (tsc's isDeclarationVisible)
typedef struct Container {
    bool global; // A script's top level: everything is
    bool ambient; // An ambient namespace: everything is (implicitly exported)
    bool module_top_level; // `export { x }` can name the declarations
} Container;

// A JavaScript module's `@typedef`/`@callback` is exported (tsgo's IsImplicitlyExportedJSDocDeclaration)
static bool implicitly_exported_jsdoc(Statement const* statement, Container container) {
    return container.module_top_level && ((statement->tag == STATEMENT_DECL_TYPE && statement->decl.type->jsdoc) ||
        (statement->tag == STATEMENT_DECL_NAMESPACE && statement->decl.namespace->jsdoc));
}

static bool statement_visible_at_first(Walk* me, Statement const* statement, Container container, Symbol const* symbol) {
    if (container.global || container.ambient) return true;
    if (statement_declares_name(statement->tag) && statement->decl.props.exported != DECL_EXPORTED_NO) return true;
    if (implicitly_exported_jsdoc(statement, container)) return true;
    return container.module_top_level && is_exported_local(me, symbol);
}

// Whether to walk a declaration: it's visible, or (in a late pass) painted and not walked yet
static bool statement_visible(Walk* me, Statement const* statement, Container container, Symbol const* symbol) {
    if (!my late_pass) return statement_visible_at_first(me, statement, container, symbol);
    return !statement_visible_at_first(me, statement, container, symbol) && take_painted(me, symbol);
}

// Declarations

static bool symbol_declared_once(Symbol const* symbol) {
    if (!symbol) return true;
    if (symbol->decl_count == 1) return true;
    u32 variables = 0;
    for (u32 i = 0; i < symbol->decl_count; i++) variables += symbol->decls[i].kind == SYMBOL_DECL_VARIABLE;
    return variables == 1;
}

// tsc's isExpandoFunctionDeclaration (of a function declaration, or of a variable initialized with a function): `f.x =
// v` declares members of it. Reports TS9023 at each such assignment when `report`
static bool check_expandos(Walk* me, Symbol const* symbol, bool report_them) {
    if (!symbol || !symbol->exports) return false;
    bool any = false;
    for (u32 i = 0; i < symbol->exports->count; i++) {
        Symbol const* member = symbol->exports->entries[i].symbol;
        if (!member || !member->decl_count || member->decls[0].kind != SYMBOL_DECL_ASSIGNMENT) continue;
        // (A late-bound one (`f[k] = v`) whose key isn't a property name type isn't a member at all: tsc's
        // lateBindMember skips it in getExportsOfSymbol)
        if (symbol_rare(member)->late_bound_key) {
            bool is_number = false;
            if (!ty_property_name_of_key(my checker, ty_expr_type(my checker, symbol_rare(member)->late_bound_key), &is_number)) continue;
        }
        any = true;
        if (!report_them) continue;
        report(me, member->decls[0].name, 9023, "Assigning properties to functions without declaring them is not "
            "supported with --isolatedDeclarations. Add an explicit declaration for the properties assigned to this "
            "function.");
        // The members are written as a namespace's variables, of the checker's type (typeFromExpandoProperty)
        bool outer_static = my member_static, outer_in_class = my member_in_class;
        my member_static = my member_in_class = false;
        // (the access `f.x` is the declaration, `x` its name)
        Span at = member->decls[0].name;
        Span name = at;
        if (name.size > member->name.size && csview_equals_sv(csview_slice(span_sv(name), name.size - member->name.size, name.size), member->name)) {
            name = span_make_in(at, span_end_ptr(at) - member->name.size, span_end_ptr(at));
        }
        // (Its accessibility errors are at the assignment, the declaration tsgo's binder records: `f.x = v`)
        Expr const* value = member->decls[0].node;
        Span full = value && span_valid(expr_outer_span(value)) && span_source(expr_outer_span(value)) == span_source(at) ?
            span_between(at, expr_outer_span(value)) : at;
        Emitting saved = emit_enter(me, EMIT_PROPERTY, SIG_INTERFACE, name, full);
        // (A name as the value is written `export { v as x }`: the local it names is kept)
        if (value && value->tag == EXPR_IDENTIFIER) check_entity_expression(me, value);
        bool outer = fallback_begin(me);
        my fell_back = true;
        symbol_chain_set_expando_host(symbol);
        fallback_end(me, outer, ty_type_of_symbol(my checker, member));
        symbol_chain_set_expando_host(nullptr);
        emit_leave(me, saved);
        my member_static = outer_static, my member_in_class = outer_in_class;
    }
    return any;
}

static bool expando_member_written(Walk* me, Symbol const* member, csview* name);

static bool expando_any_written(Walk* me, Symbol const* symbol) {
    for (u32 i = 0; symbol && symbol->exports && i < symbol->exports->count; i++) {
        csview name;
        if (expando_member_written(me, symbol->exports->entries[i].symbol, &name)) return true;
    }
    return false;
}

// (tsc's transformExpandoAssignment: a variable initialized with a function or arrow function, without a type, of which
// an expando member is written)
static bool is_expando_variable(Walk* me, Binding const* binding, Expr const* init) {
    return init->tag == EXPR_FUNCTION && check_expandos(me, binding->symbol, false) && expando_any_written(me, binding->symbol);
}

// The return type and parameters of a function expression declaration emit writes as a function declaration (an expando
// function's)
static void serialize_signature(Walk* me, FunctionImpl const* fn) {
    if (!fn->ret_explicit && !fn->predicate) serialize_return(me, fn, pseudo_of_return(my checker, fn));
    serialize_parameters(me, fn);
}

// reportInferenceFallback of a variable declaration
static void report_variable(Walk* me, InitializedBinding const* decl) {
    Binding const* binding = &decl->binding;
    Expr const* init = initialized_binding_value(decl);
    if (is_expando_variable(me, binding, init)) {
        check_expandos(me, binding->symbol, true);
        // The checker's type is written instead, and its signature's return type and parameters go through the
        // builder again (the node builder's serializeReturnTypeForSignature)
        if (init->tag == EXPR_FUNCTION) serialize_signature(me, init->function);
        return;
    }
    report_fallback(me, &(PseudoError){ .kind = PSEUDO_ERROR_VARIABLE, .binding = decl });
}

// tsc's isLiteralConstDeclaration: a const (or readonly) declaration of a fresh literal type, which declaration emit
// writes with its initializer
static bool is_literal_const(Walk* me, Symbol const* symbol) {
    return symbol && ty_is_fresh(ty_type_of_symbol(my checker, symbol));
}

static bool is_literal_const_field(ClassItem const* item) {
    return ty_is_fresh(lazy_type_get((LazyType*)&item->field.lazy_type));
}

// hasAnyBindingInitializers
static bool binding_has_initializers(Binding const* pattern) {
    if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) {
            if (i->default_value || binding_has_initializers(i)) return true;
        }
    } else if (pattern->tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &pattern->object) {
            if (i->target.default_value || binding_has_initializers(&i->target)) return true;
        }
    }
    return false;
}

// A binding pattern with initializers is emitted as variables of its own, each with no syntactic type: TS9019
static void report_binding_elements(Walk* me, Binding const* pattern) {
    if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) {
            if (i->tag == BINDING_IDENTIFIER) {
                report(me, i->content_span, 9019,
                    "Binding elements with initializers can't be exported directly with --isolatedDeclarations.");
            } else {
                report_binding_elements(me, i);
            }
        }
    } else if (pattern->tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &pattern->object) {
            Binding const* target = &i->target;
            if (target->tag == BINDING_IDENTIFIER) {
                report(me, target->content_span, 9019,
                    "Binding elements with initializers can't be exported directly with --isolatedDeclarations.");
            } else {
                report_binding_elements(me, target);
            }
        }
    }
}

// The elements of a binding pattern are written as variables of their types (inferTypeOfDeclaration)
static void print_binding_elements(Walk* me, Binding const* pattern) {
    if (pattern->tag == BINDING_IDENTIFIER) {
        Emitting saved = emit_enter(me, EMIT_VARIABLE, SIG_FUNCTION, pattern->content_span, pattern->content_span);
        bool outer = fallback_begin(me);
        my fell_back = true;
        fallback_end(me, outer, pattern->symbol ? ty_type_of_symbol(my checker, pattern->symbol) : nullptr);
        emit_leave(me, saved);
    } else if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) print_binding_elements(me, i);
    } else if (pattern->tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &pattern->object) print_binding_elements(me, &i->target);
    }
}

// ensureType of a variable declaration
static void serialize_variable(Walk* me, InitializedBinding const* decl, bool is_const) {
    Binding const* binding = &decl->binding;
    Expr const* init = initialized_binding_value(decl);
    bool initialized = init->tag != EXPR_IMPLICIT_ANY;
    if (is_const && initialized && is_literal_const(me, binding->symbol)) {
        // A literal const is written with its initializer, which must be a literal (ensureNoInitializer)
        if (!is_primitive_literal_value(init, true)) report_variable(me, decl);
        // (An enum member's names the enum: createLiteralConstValue's symbolToExpression, tracked)
        Ty const* type = ty_type_of_symbol(my checker, binding->symbol);
        if (type && (type->flags & TYF_ENUM_LITERAL)) print_type(me, type, false);
        return;
    }
    if (binding->has_explicit_type) {
        check_written_type(me, binding_type(binding));
        return;
    }
    Ty const* type = binding->symbol ? ty_type_of_symbol(my checker, binding->symbol) : nullptr;
    if (initialized && is_expando_variable(me, binding, init)) {
        bool outer = fallback_begin(me);
        report_variable(me, decl);
        my fell_back = outer || my fell_back;
        if (my fell_back && binding->symbol && init->tag == EXPR_FUNCTION) {
            // An expando function is emitted as a function declaration per call signature and a namespace of its
            // members (tsc's declaration transform of isExpandoFunctionDeclaration): its signatures are printed
            Members const* members = ty_members(my checker, type);
            for (u32 i = 0; i < members->call_count; i++) print_inferred(me, ty_signature_type(my checker, members->call[i]));
        } else if (my fell_back) {
            print_inferred(me, type);
        }
        return;
    }
    // (A variable declared more than once has no syntactic type: typeFromVariable)
    if (!symbol_declared_once(binding->symbol)) {
        bool outer = fallback_begin(me);
        report_variable(me, decl);
        my fell_back = my print_reports = true;
        fallback_end(me, outer, type);
        return;
    }
    Pseudo const* pseudo = pseudo_of_variable(my checker, decl, is_const, (PseudoPlace){0});
    serialize_declaration(me, pseudo, type ? ty_widened_literal(my checker, type) : nullptr, false, false);
}

static void check_variable(Walk* me, InitializedBinding const* decl, bool is_const) {
    Binding const* binding = &decl->binding;
    if (binding->tag != BINDING_IDENTIFIER) {
        if (binding_has_initializers(binding)) {
            report_binding_elements(me, binding);
        } else {
            // The declaration is written with its type, which has no syntactic type (no symbol of its own: tsgo's
            // typeFromVariable), unless it still widens (an object literal's: serializeTypeForDeclaration doesn't try)
            Expr const* init = initialized_binding_value(decl);
            Ty const* type = init->tag != EXPR_IMPLICIT_ANY ? lazy_type_get((LazyType*)&init->lazy_type) : nullptr;
            bool widens = type && ((type->flags & TYF_FRESH_OBJECT) || (type->xflags & TYX_CONTAINS_WIDENING));
            if (type && !widens && !binding->has_explicit_type) {
                Root outer_root = my root;
                my root = (Root){ .kind = ROOT_VARIABLE, .binding = decl };
                report_fallback(me, &(PseudoError){ .kind = PSEUDO_ERROR_VARIABLE, .binding = decl });
                my root = outer_root;
            }
            // (emit_variable_statement() writes the pattern with that whole type: its names are tracked, hexo's
            // `declare const { Readable }: typeof Stream`)
            Emitting saved = emit_enter(me, EMIT_VARIABLE, SIG_FUNCTION, binding->content_span, binding->content_span);
            if (binding->has_explicit_type) check_written_type(me, binding_type(binding));
            else if (type) print_type(me, ty_widened_literal(my checker, ty_widened_objects(my checker, ty_regular(my checker, type))), false);
            emit_leave(me, saved);
        }
        print_binding_elements(me, binding);
        return;
    }
    Emitting saved = emit_enter(me, EMIT_VARIABLE, SIG_FUNCTION, binding->content_span, binding->content_span);
    Root outer_root = my root;
    my root = (Root){ .kind = ROOT_VARIABLE, .binding = decl };
    serialize_variable(me, decl, is_const);
    my root = outer_root;
    emit_leave(me, saved);
}

// tsc's isImplementationOfOverload for a function declaration: the symbol has another signature (an overload, or
// another implementation: a duplicate, TS2393, makes both implementations of overloads)
static bool is_overload_implementation(FunctionImpl const* fn) {
    if (!fn->has_body || !fn->symbol) return false;
    for (u32 i = 0; i < fn->symbol->decl_count; i++) {
        SymbolDecl const* decl = &fn->symbol->decls[i];
        if (decl->kind == SYMBOL_DECL_FUNCTION && decl->node != fn) return true;
    }
    return false;
}

// shouldEmitFunctionProperties: the implementation, or the last overload of an ambient function
static bool emits_function_properties(FunctionImpl const* fn) {
    if (fn->has_body || !fn->symbol) return true;
    // (From the end: once per overload, each of them a declaration)
    for (u32 i = fn->symbol->decl_count; i-- > 0;) {
        SymbolDecl const* decl = &fn->symbol->decls[i];
        if (decl->kind == SYMBOL_DECL_FUNCTION && !((FunctionImpl const*)decl->node)->has_body) return decl->node == fn;
    }
    return true;
}

static void check_function_declaration(Walk* me, FunctionImpl const* fn, Statement const* statement) {
    if (is_overload_implementation(fn)) return;
    check_written_signature(me, fn, SIG_FUNCTION, true);
    check_declaration_parameters(me, fn, SIG_FUNCTION);
    // (Without a name, `export default function () {}` is spanned from its first token, the `export`)
    Span at = span_valid(fn->name) ? fn->name : block_first_token(statement->span);
    check_declaration_return(me, fn, SIG_FUNCTION, (Span){0}, at);
    if (emits_function_properties(fn)) check_expandos(me, fn->symbol, true);
}

static bool item_has_private_name(ClassItem const* item) { return span_valid(class_item_private_name(item)); }

// The accessor pair of a class accessor
static Accessors class_accessors(ClassItem const* item) {
    Accessors pair = {0};
    ClassItem const* first = ty_class_accessor_first(current_checker, item);
    if (!first) first = item;
    ClassItem const* getter = item->method.accessor == CLASS_ACCESSOR_GET ? item : ty_class_accessor_getter(current_checker, item);
    if (!getter && first->method.accessor == CLASS_ACCESSOR_GET) getter = first;
    if (getter) {
        pair.getter = &getter->method.func;
        pair.getter_name = getter->method.name_span;
    }
    if (item->method.accessor == CLASS_ACCESSOR_SET) {
        pair.setter = &item->method.func;
        pair.setter_name = item->method.name_span;
    }
    return pair;
}

// A class's setter for a getter: the class's items are searched (accessor_getter only links setters to getters)
static void find_setter(ClassDecl const* cls, ClassItem const* getter, Accessors* pair) {
    for_each (i, vec_class_item, &cls->items) {
        if (i->tag != CLASS_ITEM_METHOD || i->method.accessor != CLASS_ACCESSOR_SET) continue;
        if (ty_class_accessor_getter(current_checker, i) == getter) {
            pair->setter = &i->method.func;
            pair->setter_name = i->method.name_span;
            return;
        }
    }
}

// typeFromProperty of a class field
// ensureType of a class's property declaration (typeFromProperty)
static void check_field(Walk* me, ClassItem const* item) {
    if (item->access == ACCESS_MODIFIER_PRIVATE) return;
    my printed_next = &my class_printed_types;
    Emitting saved = emit_enter(me, EMIT_PROPERTY, SIG_CLASS, item->field.name_span, item->field.name_span);
    Root outer_root = my root;
    my root = (Root){ .kind = ROOT_FIELD, .field = item };
    Expr const* init = &item->field.value;
    Ty const* type = lazy_type_get((LazyType*)&item->field.lazy_type);
    if (item->field.is_readonly && init->tag != EXPR_IMPLICIT_ANY && is_literal_const_field(item)) {
        // A readonly literal is written with its initializer, which must be a literal (ensureNoInitializer)
        if (!is_primitive_literal_value(init, true)) {
            bool outer = fallback_begin(me);
            report_fallback(me, &(PseudoError){ .kind = PSEUDO_ERROR_FIELD, .field = item });
            fallback_end(me, outer, type);
        }
        // (An enum member's names the enum: createLiteralConstValue's symbolToExpression, tracked, see
        // serialize_variable())
        if (type && (type->flags & TYF_ENUM_LITERAL)) print_type(me, type, false);
    } else if (item->field.has_explicit_type) {
        check_written_type(me, &item->field.explicit_type);
    } else {
        // (An optional property's type has undefined: tsc's addOptionality)
        Ty const* shown = type ? ty_widened_literal(my checker, type) : nullptr;
        if (shown && item->field.is_optional && my strict_null_checks && ty_without_undefined(my checker, shown) == shown) {
            shown = ty_union(my checker, (Ty const*[]){ shown, ty_intrinsic(my checker, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
        }
        serialize_declaration(me, pseudo_of_field(my checker, item), shown, item->field.is_optional, false);
    }
    my root = outer_root;
    emit_leave(me, saved);
}

// A method an interface merged with the class declares too: the class's method implements its overloads (tsc's
// getSignaturesOfSymbol has both), declaration emit leaves it out. Only one with a body (isImplementationOfOverload):
// an ambient class's method stays (vscode's `declare class FileSystemObserver` merged with an interface)
static bool interface_declares_method(ClassDecl const* cls, ClassItem const* item) {
    Symbol const* symbol = cls->symbol;
    if (!symbol || item->method.computed_key || item->method.is_static || !item->method.func.has_body) return false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* decl = symbol->decls[i].node;
        if (decl->type.tag != TYPE_INTERFACE) continue;
        for_each (j, vec_interface_member, &decl->type.interface.members) {
            if (j->key.tag == INTERFACE_KEY_NAME && (j->is_method || j->key.name_is_member_function) && cstr_eq(&j->key.name_string, &item->method.name_string)) {
                return true;
            }
        }
    }
    return false;
}

// (Its overloads are among its namesakes: link_overload_implementations() finds it among theirs)
static bool is_class_overload_implementation(ClassDecl const* cls, ClassItem const* item, FunctionImpl const* fn) {
    if (!fn->has_body) return false;
    for (ClassNamesakes it = class_namesakes(cls, item, false); class_namesakes_next(&it);) {
        ClassItem const* i = it.item;
        FunctionImpl const* other = i->tag == CLASS_ITEM_METHOD ? &i->method.func :
            i->tag == CLASS_ITEM_CONSTRUCTOR ? &i->constructor.func : nullptr;
        if (other && other != fn && function_impl_rare(other)->overload_implementation == fn) return true;
    }
    // A computed name's overloads are the methods of its symbol (tsc's isImplementationOfOverload: more than one
    // signature declaration), which the checker's member symbols know: `[Symbol.toPrimitive](h: 'string'): R;` and its
    // implementation
    if (item->tag == CLASS_ITEM_METHOD && item->method.computed_key && !item->method.accessor) {
        ClassMemberSymbols const* symbols = ty_class_member_symbols(current_checker, cls);
        u32 at = (u32)(item - cls->items.data);
        i32 entry = symbols && at < symbols->item_count ? symbols->item_entry[at] : CLASS_MEMBER_NONE;
        for (u32 k = 0; entry >= 0 && k < symbols->item_count; k++) {
            ClassItem const* other = &cls->items.data[k];
            if (k != at && symbols->item_entry[k] == entry && other->tag == CLASS_ITEM_METHOD && !other->method.accessor) return true;
        }
    }
    return false;
}

// `statement`: the class declaration's (an anonymous one's diagnostics go to its first token)
static bool contains_internal(char const* from, char const* to) {
    for (char const* c = from; c + 9 <= to; c++) {
        if (*c == '@' && memcmp(c, "@internal", 9) == 0) return true;
    }
    return false;
}

// tsgo's shouldStripInternal: with stripInternal, a declaration whose leading comments say `@internal` isn't emitted, so
// it has no declaration diagnostics. `start`: the declaration's first token (its modifiers included). Block comments and
// whole-line `//` comments right before it (tsc's getLeadingCommentRanges from the previous token's end)
static bool is_internal_declaration(Span start) {
    if (!compiler_options()->strip_internal || !span_valid(start)) return false;
    SourceFile const* file = span_source(start);
    if (!file) return false;
    char const* text = cstr_str(&file->contents);
    char const* at = start.buf;
    if (at < text || at > text + cstr_size(&file->contents)) return false;
    for (;;) {
        while (at > text && (at[-1] == ' ' || at[-1] == '\t' || at[-1] == '\n' || at[-1] == '\r')) at--;
        if (at - text >= 2 && at[-1] == '/' && at[-2] == '*') {
            char const* open = at - 2;
            while (open - text >= 2 && !(open[-2] == '/' && open[-1] == '*')) open--;
            if (open - text < 2) return false;
            open -= 2;
            if (contains_internal(open, at)) return true;
            at = open;
            continue;
        }
        char const* line = at;
        while (line > text && line[-1] != '\n') line--;
        char const* first = line;
        while (first < at && (*first == ' ' || *first == '\t')) first++;
        if (at - first >= 2 && first[0] == '/' && first[1] == '/') {
            if (contains_internal(first, at)) return true;
            at = line;
            continue;
        }
        return false;
    }
}


// A member whose computed name is of type string, number or symbol (not a literal or unique symbol): a component of
// the index signature such names make (tsc's getIndexInfosOfIndexSymbol), which declaration emit writes as the members
// themselves where every name is a visible entity name (createLateBoundIndexSignatures)
typedef struct LateIndexMember {
    Expr const* name;
    ClassItem const* item; // A class member's, else an interface's merged into the class
    InterfaceMember const* member;
    uint8_t kind; // 1 string, 2 number, 3 symbol (the order the infos come in)
    bool is_static, readonly, optional;
} LateIndexMember;

static uint8_t late_index_kind(Expr const* key) {
    if (!key || !is_dynamic_name(key)) return 0;
    Ty const* type = lazy_type_get((LazyType*)&key->lazy_type);
    if (!type || (type->flags & (TYF_LITERAL | TYF_UNIQUE_ES_SYMBOL))) return 0;
    if (type->flags & TYF_ANY) return pseudo_is_entity_name(key) ? 2 : 0; // (assignable to number first)
    if (type->flags & TYF_NUMBER) return 2;
    if (type->flags & TYF_ES_SYMBOL) return 3;
    if (type->flags & (TYF_STRING | TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) return 1;
    // (A union of them: of the kind all its members are, isTypeAssignableTo)
    if (type->kind == TY_UNION && type->composite.types.len) {
        u32 kinds[4] = {0};
        for (u32 i = 0; i < type->composite.types.len; i++) {
            u32 flags = type->composite.types.data[i]->flags;
            kinds[flags & (TYF_NUMBER | TYF_NUMBER_LITERAL) ? 2 : flags & TYF_ES_SYMBOL_LIKE ? 3 : flags & TYF_STRING_LIKE ? 1 : 0]++;
        }
        for (uint8_t k = 1; k <= 3; k++) {
            if (kinds[k] == type->composite.types.len) return k;
        }
    }
    return 0;
}

// The class's late bound index members (null when none), `count` of them
static LateIndexMember* late_index_members(ClassDecl const* cls, u32* count) {
    LateIndexMember* members = nullptr;
    u32 n = 0, cap = 0;
#define LATE_PUSH(...) do { \
        if (n == cap) { cap = cap ? cap * 2 : 4; members = realloc(members, cap * sizeof(LateIndexMember)); } \
        members[n++] = (LateIndexMember){ __VA_ARGS__ }; \
    } while (0)
    for_each (i, vec_class_item, &cls->items) {
        if (is_internal_declaration(i->span)) continue;
        // (Accessors too, each its own symbol of its own type: a getter and a setter of one name are two members)
        Expr const* key = i->tag == CLASS_ITEM_METHOD ? i->method.computed_key :
            i->tag == CLASS_ITEM_FIELD ? i->field.computed_key : nullptr;
        uint8_t kind = late_index_kind(key);
        if (!kind) continue;
        bool field = i->tag == CLASS_ITEM_FIELD;
        LATE_PUSH(.name = key, .item = i, .kind = kind, .is_static = class_item_is_static((ClassItem*)i),
            .readonly = field && i->field.is_readonly, .optional = field ? i->field.is_optional : i->method.is_optional);
    }
    Symbol const* symbol = cls->symbol;
    for (u32 d = 0; symbol && d < symbol->decl_count; d++) {
        if (symbol->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* decl = symbol->decls[d].node;
        if (decl->type.tag != TYPE_INTERFACE) continue;
        for_each (j, vec_interface_member, &decl->type.interface.members) {
            uint8_t kind = j->key.tag == INTERFACE_KEY_EXPR ? late_index_kind(j->key.expr) : 0;
            if (!kind) continue;
            LATE_PUSH(.name = j->key.expr, .member = j, .kind = kind, .readonly = j->readonly, .optional = j->optional);
        }
    }
#undef LATE_PUSH
    *count = n;
    return members;
}

// Whether the members of one index signature can all be written by name (their entity names visible from here)
static bool late_index_names_visible(Walk* me, LateIndexMember const* members, u32 count, bool is_static, uint8_t kind) {
    for (u32 i = 0; i < count; i++) {
        if (members[i].is_static != is_static || members[i].kind != kind) continue;
        Expr const* name = members[i].name;
        if (!pseudo_is_entity_name(name)) return false;
        Expr const* leftmost = entity_leftmost(name);
        Symbol const* symbol = leftmost->tag == EXPR_IDENTIFIER ? leftmost->identifier.symbol : nullptr;
        if (!symbol) return false;
        SymbolAccess access = symbol_accessibility_at(my scope, symbol, SYMBOL_VALUE);
        if (access.kind != SYMBOL_ACCESSIBLE && !symbol_has_visible_declarations(symbol)) return false;
    }
    return true;
}

// The first walk: what the names written refer to is painted
static void check_late_indexes(Walk* me, ClassDecl const* cls) {
    u32 count = 0;
    LateIndexMember* members = late_index_members(cls, &count);
    for (u32 side = 0; side < 2; side++) {
        for (uint8_t kind = 1; kind <= 3; kind++) {
            bool any = false;
            for (u32 i = 0; i < count; i++) any |= members[i].is_static == (side == 0) && members[i].kind == kind;
            if (!any || !late_index_names_visible(me, members, count, side == 0, kind)) continue;
            for (u32 i = 0; i < count; i++) {
                if (members[i].is_static != (side == 0) || members[i].kind != kind) continue;
                Expr const* leftmost = entity_leftmost(members[i].name);
                Symbol const* symbol = leftmost->identifier.symbol;
                SymbolAccess access = symbol_accessibility_at(my scope, symbol, SYMBOL_VALUE);
                paint(me, access.kind == SYMBOL_ACCESSIBLE ? access.head : symbol);
            }
        }
    }
    free(members);
}

// A JavaScript class's `this.x = v` members become properties (tsgo's collectThisPropertyAssignments), whose type the
// pseudochecker never infers (typeFromExpandoProperty: NoResult without a JSDoc type): under isolatedDeclarations each
// is TS9013 at its first assignment. (A base class's members that one overrides: not handled, ReportInferenceFallback
// on the class)
static void check_js_this_properties(Walk* me, ClassDecl const* cls) {
    Symbol const* symbol = cls->symbol;
    SymbolTable const* members = symbol ? symbol_rare(symbol)->members : nullptr;
    for (u32 i = 0; members && i < members->count; i++) {
        Symbol const* member = members->entries[i].symbol;
        if (!member->decl_count) continue;
        SymbolDecl const* decl = &member->decls[0];
        if (decl->kind != SYMBOL_DECL_ASSIGNMENT ||
            (decl->assignment != ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR && decl->assignment != ASSIGNMENT_DECL_THIS_IN_METHOD)
        ) {
            continue;
        }
        csview name = symbol_entry_name(&members->entries[i]);
        if (csview_equals(name, "constructor") || class_decl_find_member(cls, name, false) || symbol_decl_jsdoc_type(decl)) continue;
        Expr const* value = decl->node;
        Span whole = decl->full;
        if (value && span_source(expr_outer_span(value)) == span_source(whole) && span_end_ptr(expr_outer_span(value)) > span_start_ptr(whole)) {
            whole = span_make_in(whole, span_start_ptr(whole), span_end_ptr(expr_outer_span(value)));
        }
        report(me, whole, 9013, msg_expression);
    }
}

// A JavaScript class's `this.x = v` members (tsgo's collectThisPropertyAssignments): a property for each name (and
// side) the class doesn't declare, in the order of their first assignment, with the type of the member (an
// assignment's JSDoc `@type` as written). Not one whose base class member already says it: a method or accessor, or
// a property of the identical type and modifiers (IsThisPropertyAssignmentDeclarationRedundant)
typedef struct ThisProperty {
    Symbol const* member;
    SymbolDecl const* decl; // Its first `this.x =` in source order
    bool is_static;
} ThisProperty;

static u32 js_this_properties(Walk* me, ClassDecl const* cls, ThisProperty** out) {
    *out = nullptr;
    Symbol const* symbol = cls->symbol;
    if (!symbol) return 0;
    u32 count = 0;
    for (u32 side = 0; side < 2; side++) {
        SymbolTable const* table = side ? symbol->exports : symbol_rare(symbol)->members;
        for (u32 i = 0; table && i < table->count; i++) {
            Symbol const* member = table->entries[i].symbol;
            SymbolDecl const* first = nullptr;
            for (u32 j = 0; j < member->decl_count; j++) {
                SymbolDecl const* decl = &member->decls[j];
                if (decl->kind != SYMBOL_DECL_ASSIGNMENT ||
                    (decl->assignment != ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR && decl->assignment != ASSIGNMENT_DECL_THIS_IN_METHOD)
                ) {
                    continue;
                }
                if (!first || decl->full.buf < first->full.buf) first = decl;
            }
            if (!first) continue;
            csview name = symbol_entry_name(&table->entries[i]);
            if (csview_equals(name, "constructor") || class_decl_find_member(cls, name, side)) continue;
            *out = realloc(*out, (count + 1) * sizeof(ThisProperty));
            (*out)[count++] = (ThisProperty){ member, first, side };
        }
    }
    for (u32 i = 1; i < count; i++) {
        for (u32 k = i; k > 0 && (*out)[k - 1].decl->full.buf > (*out)[k].decl->full.buf; k--) {
            ThisProperty t = (*out)[k];
            (*out)[k] = (*out)[k - 1];
            (*out)[k - 1] = t;
        }
    }
    // (What a base already has: tsgo's IsThisPropertyAssignmentDeclarationRedundant looks in the base instance types,
    // a static member's too)
    Expr const* base = &cls->extends;
    bool extends_null = base->tag == EXPR_LITERAL && base->literal.tag == VALUE_NULL;
    if (base->tag != EXPR_IMPLICIT_ANY && !extends_null) {
        Ty const* instance = ty_class_base_instance(my checker, cls);
        u32 kept = 0;
        for (u32 i = 0; i < count; i++) {
            ThisProperty const* p = &(*out)[i];
            Ty const* base_side = instance;
            Prop const* base_prop = base_side ? ty_get_prop(my checker, base_side, checker_atom(my checker, p->member->name)) : nullptr;
            bool redundant = false;
            if (base_prop) {
                Ty const* own_side = p->is_static ? ty_type_of_symbol(my checker, symbol) : ty_class_instance_type(my checker, symbol);
                Prop const* own = own_side ? ty_get_prop(my checker, own_side, checker_atom(my checker, p->member->name)) : nullptr;
                redundant = (base_prop->flags & (PROP_METHOD | PROP_ACCESSOR | PROP_GETTER | PROP_SETTER)) ||
                    (own && ty_props_identical(my checker, own, base_prop));
            }
            if (!redundant) (*out)[kept++] = *p;
        }
        count = kept;
    }
    return count;
}

// The first walk of them: what their types name is painted
static void check_js_this_property_types(Walk* me, ClassDecl const* cls) {
    ThisProperty* props;
    u32 count = js_this_properties(me, cls, &props);
    for (u32 i = 0; i < count; i++) {
        TypeNode const* annotation = symbol_decl_jsdoc_type(props[i].decl);
        Span at = props[i].decl->full;
        Emitting saved = emit_enter(me, EMIT_PROPERTY, SIG_INTERFACE, at, at);
        my emitting.name_not_node = true;
        static Pseudo const no_result = { .kind = PSEUDO_NO_RESULT };
        if (annotation) check_written_type(me, annotation);
        else serialize_declaration(me, &no_result, ty_type_of_symbol(my checker, props[i].member), false, false);
        emit_leave(me, saved);
    }
    free(props);
}

static void check_class(Walk* me, ClassDecl const* cls, Span statement) {
    ClassDecl const* outer_printed_class = my printed_class;
    my printed_class = cls;
    bool outer_static = my member_static, outer_in_class = my member_in_class;
    my member_in_class = true;
    check_late_indexes(me, cls);
    if (my file->is_js) check_js_this_properties(me, cls);
    if (my file->is_js && !my isolated) check_js_this_property_types(me, cls);
    for_each (i, vec_class_item, &cls->items) {
        if (is_internal_declaration(i->span)) continue;
        my member_static = class_item_is_static((ClassItem*)i);
        Expr const* key = i->tag == CLASS_ITEM_METHOD ? i->method.computed_key :
            i->tag == CLASS_ITEM_FIELD ? i->field.computed_key : nullptr;
        // A late bound name's entity (checkName): what it names is emitted
        bool late_bound = false;
        if (is_dynamic_name(key) && pseudo_is_entity_name(key) && !item_has_private_name(i)) {
            Ty const* key_type = lazy_type_get((LazyType*)&key->lazy_type);
            late_bound = key_type && (key_type->flags & (TYF_LITERAL | TYF_UNIQUE_ES_SYMBOL));
            if (late_bound) {
                Span name = i->tag == CLASS_ITEM_METHOD ? i->method.name_span : i->field.name_span;
                Emitting saved = emit_enter_nested(me, i->tag == CLASS_ITEM_METHOD ? EMIT_METHOD_NAME : EMIT_PROPERTY, SIG_METHOD, name, name);
                check_entity_expression(me, key);
                emit_leave(me, saved);
            }
        }
        if (is_dynamic_name(key)) {
            if (!is_global_symbol_reference(me, key)) {
                Span name = i->tag == CLASS_ITEM_METHOD ? i->method.name_span : i->field.name_span;
                report(me, name, 9038,
                    "Computed property names on class or object literals cannot be inferred with --isolatedDeclarations.");
                // A method whose name is of type string, number or symbol makes an index signature of the class, whose
                // type the node builder writes: the method's signature goes through the builder again
                // (createLateBoundIndexSignatures)
                Ty const* name_type = lazy_type_get((LazyType*)&key->lazy_type);
                bool index = !(name_type->flags & (TYF_ANY | TYF_LITERAL | TYF_UNIQUE_ES_SYMBOL)) &&
                    (name_type->flags & (TYF_STRING | TYF_NUMBER | TYF_ES_SYMBOL));
                if (index && i->tag == CLASS_ITEM_METHOD && i->method.accessor == CLASS_ACCESSOR_NONE) {
                    Root outer_root = my root;
                    my root = (Root){ .kind = ROOT_FUNCTION, .function = &i->method.func };
                    serialize_signature(me, &i->method.func);
                    my root = outer_root;
                }
                // (A late bound member is emitted, and its type checked, except with isolatedDeclarations: tsgo's
                // visitDeclarationSubtree)
                if (my isolated || !late_bound) continue;
            }
        }
        switch (i->tag) {
            case CLASS_ITEM_CONSTRUCTOR: {
                FunctionImpl const* ctor = &i->constructor.func;
                if (is_class_overload_implementation(cls, i, ctor)) break;
                // The parameter properties are properties of the class (only their own optionality is added), and the
                // constructor's parameters
                if (ctor->has_body) {
                    for_each (j, vec_function_arg, &ctor->args) {
                        if (!is_parameter_property(j)) continue;
                        if (j->initializer.binding.tag == BINDING_IDENTIFIER && j->access != ACCESS_MODIFIER_PRIVATE) {
                            // (a parameter's diagnostics, a property's if the constructor is private)
                            Span name = j->initializer.binding.content_span;
                            bool private_ctor = i->access == ACCESS_MODIFIER_PRIVATE;
                            Emitting saved = emit_enter(me, private_ctor ? EMIT_PROPERTY : EMIT_PARAMETER, SIG_CONSTRUCTOR, name, name);
                            Root outer_root = my root;
                            my root = (Root){ .kind = ROOT_FUNCTION, .function = ctor };
                            serialize_parameter(me, ctor, j, false);
                            // (A private constructor's signature isn't written, its parameter properties are: their
                            // annotations are checked here, check_written_signature() skips them)
                            Binding const* binding = &j->initializer.binding;
                            if (private_ctor && binding->has_explicit_type) check_written_type(me, binding_type(binding));
                            my root = outer_root;
                            emit_leave(me, saved);
                        }
                    }
                }
                if (i->access != ACCESS_MODIFIER_PRIVATE) {
                    check_written_signature(me, ctor, SIG_CONSTRUCTOR, true);
                    check_declaration_parameters(me, ctor, SIG_CONSTRUCTOR);
                }
                break;
            }
            case CLASS_ITEM_METHOD: {
                if (item_has_private_name(i)) break;
                FunctionImpl const* fn = &i->method.func;
                if (i->method.accessor == CLASS_ACCESSOR_NONE) {
                    if (i->access == ACCESS_MODIFIER_PRIVATE || is_class_overload_implementation(cls, i, fn) ||
                        interface_declares_method(cls, i)
                    ) {
                        break;
                    }
                    check_written_signature(me, fn, SIG_METHOD, true);
                    check_declaration_return(me, fn, SIG_METHOD, i->method.name_span, (Span){0});
                    check_declaration_parameters(me, fn, SIG_METHOD);
                    break;
                }
                if (i->access == ACCESS_MODIFIER_PRIVATE) break;
                check_written_signature(me, fn, SIG_ACCESSOR, true);
                Accessors pair = class_accessors(i);
                bool getter = i->method.accessor == CLASS_ACCESSOR_GET;
                if (getter) find_setter(cls, i, &pair);
                Accessors outer_pair = my class_pair;
                my class_pair = pair;
                if (getter) {
                    check_declaration_return(me, fn, SIG_ACCESSOR, i->method.name_span, (Span){0});
                } else {
                    // (Its value parameter: the accessor pair's type)
                    Emitting saved = emit_enter(me, EMIT_SETTER, SIG_ACCESSOR, i->method.name_span, i->method.name_span);
                    Root outer_root = my root;
                    my root = (Root){ .kind = ROOT_FUNCTION, .function = fn };
                    for_each (j, vec_function_arg, &fn->args) {
                        Binding const* binding = &j->initializer.binding;
                        if (binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this")) continue;
                        serialize_parameter(me, fn, j, true);
                        break;
                    }
                    my root = outer_root;
                    emit_leave(me, saved);
                }
                my class_pair = outer_pair;
                break;
            }
            case CLASS_ITEM_FIELD:
                if (item_has_private_name(i)) break;
                check_field(me, i);
                break;
            default: break;
        }
    }
    my member_static = outer_static, my member_in_class = outer_in_class;
    my printed_class = outer_printed_class;
    check_generic(me, &cls->generic, SIG_CLASS);
    for_each (i, vec_spanned_type, &cls->implements) {
        Emitting saved = emit_enter(me, EMIT_IMPLEMENTS, SIG_CLASS, cls->name, i->span);
        check_written_type(me, &i->type);
        emit_leave(me, saved);
    }
    // A base that isn't a name is written as a variable of its type
    Expr const* base = &cls->extends;
    VecSpannedType const* base_args = nullptr;
    if (base->tag == EXPR_GENERIC_PARAMS) {
        base_args = &base->generic_params->params;
        base = &base->generic_params->expr;
    }
    bool is_null = base->tag == EXPR_LITERAL && base->literal.tag == VALUE_NULL;
    if (base->tag == EXPR_IMPLICIT_ANY || is_null) return;
    Emitting saved = emit_enter(me, EMIT_EXTENDS, SIG_CLASS, cls->name,
        span_valid(cls->extends_clause) ? cls->extends_clause : expr_outer_span(&cls->extends));
    my emitting.fallback = block_first_token(statement);
    // (Its type arguments are the clause's: what they name is painted, or reported, as the clause's)
    if (base_args) {
        for_each (i, vec_spanned_type, base_args) check_written_type(me, &i->type);
    }
    if (pseudo_is_entity_name(base)) {
        check_entity_expression(me, base);
    } else {
        // (written as a variable `_base` of the expression's type)
        bool outer = fallback_begin(me);
        report_heritage(me, expr_error_span(base));
        my fell_back = true;
        fallback_end(me, outer, lazy_type_get((LazyType*)&base->lazy_type));
    }
    emit_leave(me, saved);
}

static void check_generic(Walk* me, Generic const* generic, EmitSig sig) {
    for_each (i, vec_generic_item, &generic->items) {
        Emitting saved = emit_enter_nested(me, EMIT_TYPE_PARAMETER, sig, i->name, i->name);
        if (i->extends_explicit) check_written_type(me, &i->extends);
        if (i->fallback_explicit) check_written_type(me, i->fallback);
        emit_leave(me, saved);
    }
}

// The types written in a signature (parameters, return type, type parameters)
// `declaration`: a declaration the transformer visits (its return type gets the signature's diagnostics), else a
// function type or a function expression the syntactic builder reuses
static void check_written_signature(Walk* me, FunctionImpl const* fn, EmitSig sig, bool declaration) {
    check_generic(me, &fn->generic, sig);
    for_each (i, vec_function_arg, &fn->args) {
        if (!i->initializer.binding.has_explicit_type) continue;
        Span name = i->initializer.binding.content_span;
        Emitting saved = declaration ? emit_enter(me, EMIT_PARAMETER, sig, name, name) :
            emit_enter_nested(me, EMIT_PARAMETER, sig, name, name);
        check_written_type(me, binding_type(&i->initializer.binding));
        emit_leave(me, saved);
    }
    Emitting saved = my emitting;
    if (declaration) {
        EmitKind kind = sig == SIG_ACCESSOR ? EMIT_GETTER : EMIT_RETURN;
        saved = emit_enter(me, kind, sig, fn->name, span_valid(fn->name) ? fn->name : function_error_span(fn));
    }
    if (fn->ret_explicit) check_written_type(me, &fn->ret);
    if (fn->predicate && fn->predicate->has_type) check_written_type(me, &fn->predicate->type);
    emit_leave(me, saved);
}

// The members of an interface or type literal (`decl`: an interface's, whose merged members come from elsewhere too).
// `own_contexts`: each member gets diagnostics of its own (an interface, a type alias's type literal)
static void check_type_members(Walk* me, VecInterfaceMember const* members, Symbol const* decl, bool own_contexts) {
    bool outer_suppress = my suppress_contexts;
    bool outer_static = my member_static, outer_in_class = my member_in_class;
    my suppress_contexts = !own_contexts;
    my member_static = my member_in_class = false;
    for_each (i, vec_interface_member, members) {
        if (decl && i->origin_interface && i->origin_interface != decl) continue;
        if (span_valid(i->declaration) && is_internal_declaration(i->declaration)) continue;
        Span at = interface_member_error_span(i);
        if (i->key.tag == INTERFACE_KEY_CALLABLE && span_valid(i->declaration)) {
            // (A signature's node ends after its `;` or `,`)
            at = i->declaration;
            if (span_valid(at) && (span_end_ptr(at)[0] == ';' || span_end_ptr(at)[0] == ',')) at.size++;
        }
        if (i->key.tag == INTERFACE_KEY_MAPPED) {
            Emitting saved = emit_enter_nested(me, EMIT_TYPE_PARAMETER, SIG_MAPPED, i->key.span, i->key.span);
            check_written_type(me, &i->key.mapped->in_type);
            if (i->key.mapped->has_as_type) check_written_type(me, &i->key.mapped->as_type);
            emit_leave(me, saved);
            check_written_type(me, &i->type);
            continue;
        }
        if (i->key.tag == INTERFACE_KEY_INDEX) {
            Emitting saved = emit_enter_nested(me, EMIT_PARAMETER, SIG_INDEX, i->key.index->name, i->key.index->name);
            check_written_type(me, &i->key.index->type);
            emit_leave(me, saved);
            saved = emit_enter_nested(me, EMIT_RETURN, SIG_INDEX, span_null, at);
            check_written_type(me, &i->type);
            emit_leave(me, saved);
            continue;
        }
        bool callable = i->type.tag == TYPE_CALLABLE && i->type.callable && i->type.callable->function;
        // (A computed name that isn't late bound, of a type that isn't a literal or unique symbol: the transformer
        // elides the member before it checks anything, visitDeclarationSubtree)
        if (i->key.tag == INTERFACE_KEY_EXPR && i->key.expr && !compiler_options()->isolated_declarations &&
            is_dynamic_name(i->key.expr) &&
            (ty_expr_type(my checker, i->key.expr)->kind != TY_LITERAL || !pseudo_is_entity_name(i->key.expr))
        ) {
            continue;
        }
        if (i->key.tag == INTERFACE_KEY_EXPR && i->key.expr) {
            Expr const* key = i->key.expr;
            if (is_dynamic_name(key) && !is_global_symbol_reference(me, key) && !pseudo_is_entity_name(key)) {
                report(me, at, 9014, "Computed properties must be number or string literals, variables or dotted "
                    "expressions with --isolatedDeclarations.");
                continue;
            }
            // A late bound name's entity (checkName)
            if (pseudo_is_entity_name(key)) {
                Emitting saved = emit_enter_nested(me, i->is_method ? EMIT_METHOD_NAME : EMIT_PROPERTY, SIG_METHOD, i->key.span, i->key.span);
                check_entity_expression(me, key);
                emit_leave(me, saved);
            }
        }
        if (i->implicit_any) {
            // A property signature without a type: getDiagnostic has no case for it
            report(me, i->key.span, 9013, msg_expression);
            continue;
        }
        if (!callable || (!i->is_method && i->key.tag != INTERFACE_KEY_CALLABLE)) {
            Emitting saved = emit_enter_nested(me, EMIT_PROPERTY, SIG_INTERFACE, i->key.span, i->key.span);
            check_written_type(me, &i->type);
            emit_leave(me, saved);
            continue;
        }
        FunctionImpl const* fn = i->type.callable->function;
        EmitSig sig = i->key.tag != INTERFACE_KEY_CALLABLE ? SIG_METHOD : fn->is_constructor ? SIG_CONSTRUCT : SIG_CALL;
        if (my suppress_contexts) {
            check_written_signature(me, fn, sig, false);
        } else {
            check_written_signature(me, fn, sig, true);
        }
        if (fn->ret_explicit || fn->predicate) continue;
        if (fn->is_constructor) {
            Error* error = report(me, at, 9008,
                "Method must have an explicit return type annotation with --isolatedDeclarations.");
            add_related(error, (Near){ .kind = NEAR_FUNCTION_DECLARATION, .span = at });
        } else {
            report(me, at, 9013, msg_expression); // (method and call signatures: no case either)
        }
    }
    my suppress_contexts = outer_suppress;
    my member_static = outer_static, my member_in_class = outer_in_class;
}

// The first identifier of a written type reference, checked
static void check_type_reference(Walk* me, IdentifierType const* type) {
    if (identifier_type_import_ref(type) || type->keyword || type->this_kind) return;
    if (identifier_type_namespace_count(type)) {
        if (!ast_symbols_size(identifier_type_namespace_symbols(type))) return;
        check_entity_name(me, *ast_symbols_at(identifier_type_namespace_symbols(type), 0), *ast_spans_at(identifier_type_namespaces(type), 0),
            SYMBOL_NAMESPACE);
        return;
    }
    // (An import of a value-only property of what `export =` names isn't a type: the name means a global there, tsc's
    // resolveName goes on past the import; node's `import { Console } from 'node:console'`)
    if (!type->jsdoc && ty_export_equals_property_means_global(my checker, type)) return;
    if (type->jsdoc && type->symbol && !ty_jsdoc_name_is_type(type->symbol, type)) return;
    check_entity_name(me, type->symbol, type->name, SYMBOL_TYPE);
}

// A written type of a declaration: the type literals in it are emitted, members and all (visitDeclarationSubtree)
static void check_written_type(Walk* me, TypeNode const* type) {
    switch (type->tag) {
        case TYPE_INTERFACE: check_type_members(me, &type->interface.members, nullptr, false); return;
        case TYPE_CALLABLE:
            if (type->callable && type->callable->function) {
                FunctionImpl const* fn = type->callable->function;
                check_written_signature(me, fn, fn->is_constructor ? SIG_CONSTRUCT : SIG_FUNCTION, false);
            }
            return;
        case TYPE_ARRAY: check_written_type(me, type->array.element); return;
        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &type->tuple.elements) check_written_type(me, &i->type);
            return;
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &type->composite) check_written_type(me, i);
            return;
        case TYPE_IDENTIFIER:
            check_type_reference(me, type->identifier);
            for_each (i, vec_spanned_type, &type->identifier->generic_params) check_written_type(me, &i->type);
            return;
        case TYPE_TYPEOF:
            if (type->typeof_->expr) {
                // `typeof this.#a`: the emitted declaration can't name a private member (tsgo's declaration transform of
                // a QualifiedName whose right is a PrivateIdentifier, TS7080 at the qualified name)
                for (Expr const* e = type->typeof_->expr; e->tag == EXPR_PROPERTY; e = &e->property->expr) {
                    csview name = span_sv(property_key_inner_span(e->property));
                    if (!name.size || name.buf[0] != '#') continue;
                    error_ctx_error_d(my errs, expr_outer_span(e), 7080, "Declaration emit elides private members, but '",
                        name, "' refers to a private member. Write an explicit type here.");
                }
                check_entity_expression(me, type->typeof_->expr);
                // (And its type arguments, `typeof f<T>`: type nodes of the query)
                for (Expr const* e = type->typeof_->expr; e->tag == EXPR_PROPERTY || e->tag == EXPR_GENERIC_PARAMS;) {
                    if (e->tag == EXPR_GENERIC_PARAMS) {
                        for_each (i, vec_spanned_type, &e->generic_params->params) check_written_type(me, &i->type);
                        e = &e->generic_params->expr;
                    } else {
                        e = &e->property->expr;
                    }
                }
            }
            return;
        case TYPE_INDEXED:
            check_written_type(me, &type->indexed->interface);
            check_written_type(me, &type->indexed->index);
            return;
        case TYPE_KEYOF: check_written_type(me, &type->keyof->interface); return;
        case TYPE_TERNARY:
            check_written_type(me, &type->ternary->maybe_sub);
            check_written_type(me, &type->ternary->maybe_super);
            check_written_type(me, &type->ternary->if_true);
            check_written_type(me, &type->ternary->if_false);
            return;
        case TYPE_TEMPLATE:
            for_each (i, vec_type, &type->template_->types) check_written_type(me, i);
            return;
        case TYPE_INFERRED: check_written_type(me, &type->inferred->extends); return;
        case TYPE_JSDOC: check_written_type(me, &type->jsdoc->type); return;
        case TYPE_PREDICATE:
            if (type->predicate->has_type) check_written_type(me, &type->predicate->type);
            return;
        default: return;
    }
}

static void check_interface(Walk* me, InterfaceDecl const* decl) {
    check_generic(me, &decl->generic, SIG_INTERFACE);
    for_each (i, vec_spanned_type, &decl->extends) {
        Emitting saved = emit_enter(me, EMIT_INTERFACE_EXTENDS, SIG_INTERFACE, decl->name, i->span);
        check_written_type(me, &i->type);
        emit_leave(me, saved);
    }
    if (decl->type.tag == TYPE_INTERFACE) check_type_members(me, &decl->type.interface.members, decl->symbol, true);
}

// tsc's evaluator's hasExternalReferences for an enum member's initializer: it names a member of another enum
// declaration or a constant variable (also through the members it names)
static bool enum_external_references(Walk* me, EnumDecl const* enum_decl, Expr const* expr, u32 depth);

static bool enum_member_external_references(Walk* me, EnumDecl const* enum_decl, Symbol const* symbol, u32 depth) {
    if (!symbol || !symbol->decl_count) return false;
    SymbolDecl const* decl = &symbol->decls[0];
    if (decl->kind == SYMBOL_DECL_ENUM_MEMBER) {
        EnumCase const* member = decl->node;
        if (member->parent != enum_decl) return true;
        return depth < 32 && enum_external_references(me, member->parent, &member->value, depth + 1);
    }
    if (decl->kind != SYMBOL_DECL_VARIABLE || !symbol->is_const) return false;
    Binding const* binding = decl->node;
    return !binding->parent && !binding->has_explicit_type && binding->source &&
        binding->source->tag == LAZY_TYPE_INITIALIZED_BINDING;
}

static Symbol const* enum_entity_symbol(Expr const* expr) {
    if (expr->tag == EXPR_IDENTIFIER) return expr->identifier.target;
    if (expr->tag != EXPR_PROPERTY) return nullptr;
    csview name;
    if (!property_string_key(expr->property, &name)) return nullptr;
    Symbol const* object = enum_entity_symbol(&expr->property->expr);
    if (!object || !(object->flags & (SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE | SYMBOL_ENUM))) return nullptr;
    return symbol_get_export(object, name);
}

static bool enum_external_references(Walk* me, EnumDecl const* enum_decl, Expr const* expr, u32 depth) {
    switch (expr->tag) {
        case EXPR_UNARY_OP: return enum_external_references(me, enum_decl, &expr->unary_op->expr, depth);
        case EXPR_BINARY_OP:
            return enum_external_references(me, enum_decl, &expr->binary_op->left, depth) ||
                enum_external_references(me, enum_decl, &expr->binary_op->right, depth);
        case EXPR_TEMPLATE:
            if (expr->template_->tag) return false;
            for_each (i, vec_expr, &expr->template_->substitutions) {
                if (enum_external_references(me, enum_decl, i, depth)) return true;
            }
            return false;
        case EXPR_IDENTIFIER:
        case EXPR_PROPERTY:
            return enum_member_external_references(me, enum_decl, enum_entity_symbol(expr), depth);
        default: return false;
    }
}

static void check_enum(Walk* me, EnumDecl const* decl) {
    for_each (i, vec_enum_case, &decl->cases) {
        if (i->value.tag == EXPR_IMPLICIT_ANY || i->computed_name) continue;
        if (enum_external_references(me, decl, &i->value, 0)) {
            report(me, i->full_name, 9020, "Enum member initializers must be computable without references to external "
                "symbols with --isolatedDeclarations.");
        }
    }
}

static void check_statements(Walk* me, VecStatement const* statements, Container container);

static void check_statement_now(Walk* me, Statement const* statement, Container container);

static Symbol const* statement_declared_symbol(Statement const* statement) {
    switch (statement->tag) {
        case STATEMENT_DECL_FUNCTION: return statement->decl.function.impl->symbol;
        case STATEMENT_DECL_CLASS: return statement->decl.class->symbol;
        case STATEMENT_DECL_INTERFACE: return statement->decl.interface->symbol;
        case STATEMENT_DECL_TYPE: return statement->decl.type->symbol;
        case STATEMENT_DECL_ENUM: return statement->decl.enum_->symbol;
        case STATEMENT_DECL_NAMESPACE: return statement->decl.namespace->symbol;
        default: return nullptr;
    }
}

// A destructuring declaration some painted name of is walked whole (getBindingNameVisible): takes its painted names
static bool take_painted_pattern(Walk* me, Binding const* pattern) {
    if (pattern->tag == BINDING_IDENTIFIER) return !is_exported_local(me, pattern->symbol) && take_painted(me, pattern->symbol);
    bool any = false;
    if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) any |= take_painted_pattern(me, i);
    } else if (pattern->tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &pattern->object) any |= take_painted_pattern(me, &i->target);
    }
    return any;
}

static void late_taken_push(Walk* me, Symbol const* symbol) {
    if (my late_taken_count == my late_taken_cap) {
        my late_taken_cap = my late_taken_cap ? my late_taken_cap * 2 : 8;
        my late_taken = realloc(my late_taken, my late_taken_cap * sizeof(*my late_taken));
    }
    my late_taken[my late_taken_count++] = symbol;
}

static void check_statement(Walk* me, Statement const* statement, Container container) {
    if (!my late_pass) {
        check_statement_now(me, statement, container);
        return;
    }
    // A late pass: the painted declarations (a fresh walk of each), and visible namespaces may have some
    if (statement->tag == STATEMENT_DECL_VARIABLE) {
        bool visible = container.global || container.ambient || statement->decl.props.exported != DECL_EXPORTED_NO;
        if (visible) return;
        my late_pass = false;
        for_each (i, vec_initialized_binding, &statement->decl.variable.initializers) {
            bool painted = i->binding.tag == BINDING_IDENTIFIER ? !is_exported_local(me, i->binding.symbol) &&
                take_painted(me, i->binding.symbol) : take_painted_pattern(me, &i->binding);
            if (!painted) continue;
            if (i->binding.tag == BINDING_IDENTIFIER) late_taken_push(me, i->binding.symbol);
            check_variable(me, i, variable_statement_is_const(&statement->decl.variable));
        }
        my late_pass = true;
        return;
    }
    if (statement->tag == STATEMENT_DECL_NAMESPACE) {
        NamespaceDecl const* decl = statement->decl.namespace;
        bool augmentation = decl->is_augmentation || decl->is_global;
        if (augmentation || statement_visible_at_first(me, statement, container, decl->symbol)) {
            check_statement_now(me, statement, container); // (still a late pass inside)
            return;
        }
    }
    // A painted `import x = A.B` paints A (tsc's isDeclarationVisible of the alias marks what it references)
    if (statement->tag == STATEMENT_IMPORT_EXPORT && statement->import_export->main.tag == PORT_IMPORT_ALIAS) {
        PortItem const* item = &statement->import_export->main;
        if (take_painted(me, item->symbol) && vec_symbol_cptr_size(&port_item_alias(item)->path_symbols)) {
            paint(me, *vec_symbol_cptr_at(&port_item_alias(item)->path_symbols, 0));
        }
        return;
    }
    if (!statement_declares_name(statement->tag)) return;
    Symbol const* symbol = statement_declared_symbol(statement);
    bool again = false;
    for (u32 i = 0; symbol && i < my late_taken_count && !again; i++) again = my late_taken[i] == symbol;
    if (!again) {
        if (!is_painted_pending(me, symbol)) return;
        take_painted(me, symbol);
        late_taken_push(me, symbol);
    }
    my late_pass = false;
    check_statement_now(me, statement, (Container){ .global = true });
    my late_pass = true;
}

static bool was_painted(Walk* me, Symbol const* symbol) {
    for (u32 i = 0; symbol && i < my painted_count; i++) {
        if (my painted[i] == symbol) return true;
    }
    return false;
}

// tsc's isImportRequiredByAugmentation: the imported file declares something this module exports (an augmentation of
// it), so declaration emit keeps `import "m"` even when nothing it imports is used. Only this file's own symbol merged
// with the target's augmentation counts (tsc's getMergedSymbol(s) != s): not one `export *` merely re-exports, merged
// elsewhere or not (vue's index re-exports runtime-core's augmented ComponentCustomProperties, and doesn't keep an
// unused import of runtime-core's component.ts)
static bool import_required_by_augmentation(Walk* me, SourceFile const* target) {
    Symbol const* module = my file->scope ? my file->scope->container : nullptr;
    SymbolTable const* exports = module ? module->exports : nullptr;
    for (u32 i = 0; exports && i < exports->count; i++) {
        Symbol const* symbol = exports->entries[i].symbol;
        bool in_target = false, here = false;
        for (u32 j = 0; symbol && j < symbol->decl_count; j++) {
            SourceFile const* file = span_source(symbol->decls[j].name);
            if (file == target) in_target = true;
            if (file == my file) here = true;
        }
        if (in_target && here) return true;
    }
    return false;
}

// transformImportDeclaration: named imports none of which an emitted declaration names (so far) are elided, unless an
// augmentation needs the import (TS9026)
static void check_import(Walk* me, Statement const* statement) {
    PortStatement const* port = statement->import_export;
    if (!my isolated || !my file->is_module || port->side_effect_only || port->is_import_require || port->jsdoc) return;
    if (!span_equals(port->keyword_span, "import") || !port->module_ref || !port->module_ref->file) return;
    if (port->module_ref->file == my file) return;
    if (port->main.tag != PORT_NOOP && port->main.tag != PORT_IMPORT_DEFAULT) return; // (a namespace import, `import x =`)
    if (port->main.tag == PORT_IMPORT_DEFAULT && (!vec_port_item_size(&port->items) || was_painted(me, port->main.symbol))) {
        return;
    }
    for_each (i, vec_port_item, &port->items) {
        if (i->tag != PORT_IMPORT_SPECIFIC || was_painted(me, i->symbol)) return;
    }
    if (!import_required_by_augmentation(me, port->module_ref->file)) return;
    report(me, statement->span, 9026, "Declaration emit for this file requires preserving this import for augmentations. "
        "This is not supported with --isolatedDeclarations.");
}

// A name the binding declares is one `export { x }` names
static bool binding_exports_local(Walk* me, Binding const* binding) {
    switch (binding->tag) {
        case BINDING_IDENTIFIER: return is_exported_local(me, binding->symbol);
        case BINDING_ARRAY:
            for_each (i, vec_binding, &binding->array) {
                if (binding_exports_local(me, i)) return true;
            }
            return false;
        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &binding->object) {
                if (binding_exports_local(me, &i->target)) return true;
            }
            return false;
        default: return false;
    }
}

static void check_statement_now(Walk* me, Statement const* statement, Container container) {
    if (is_internal_declaration(statement->span)) return;
    switch (statement->tag) {
        case STATEMENT_IMPORT_EXPORT: {
            // `export import x = A.B` paints A (checkEntityNameVisibility)
            PortItem const* item = &statement->import_export->main;
            if (item->tag == PORT_IMPORT_ALIAS && item->export_import && vec_symbol_cptr_size(&port_item_alias(item)->path_symbols)) {
                paint(me, *vec_symbol_cptr_at(&port_item_alias(item)->path_symbols, 0));
            }
            check_import(me, statement);
            return;
        }
        case STATEMENT_DECL_VARIABLE: {
            if (!container.global && !container.ambient && statement->decl.props.exported == DECL_EXPORTED_NO) {
                // (`export { x }` makes that one declaration visible)
                bool any = false;
                for_each (i, vec_initialized_binding, &statement->decl.variable.initializers) {
                    any |= container.module_top_level && binding_exports_local(me, &i->binding);
                }
                if (!any) return;
                for_each (i, vec_initialized_binding, &statement->decl.variable.initializers) {
                    if (binding_exports_local(me, &i->binding)) check_variable(me, i, variable_statement_is_const(&statement->decl.variable));
                }
                return;
            }
            for_each (i, vec_initialized_binding, &statement->decl.variable.initializers) {
                check_variable(me, i, variable_statement_is_const(&statement->decl.variable));
            }
            return;
        }
        case STATEMENT_DECL_FUNCTION: {
            FunctionImpl const* fn = statement->decl.function.impl;
            if (statement_visible(me, statement, container, fn->symbol)) check_function_declaration(me, fn, statement);
            return;
        }
        case STATEMENT_DECL_CLASS: {
            ClassDecl const* cls = statement->decl.class;
            if (statement_visible(me, statement, container, cls->symbol)) check_class(me, cls, statement->span);
            return;
        }
        case STATEMENT_DECL_INTERFACE: {
            InterfaceDecl const* decl = statement->decl.interface;
            if (statement_visible(me, statement, container, decl->symbol)) check_interface(me, decl);
            return;
        }
        case STATEMENT_DECL_TYPE: {
            TypeDecl const* decl = statement->decl.type;
            if (decl->jsdoc_check_only) return;
            if (statement_visible(me, statement, container, decl->symbol)) {
                check_generic(me, &decl->generic, SIG_TYPE_ALIAS);
                // (A type literal that is the alias's type gives its members diagnostics of their own)
                Emitting saved = emit_enter(me, EMIT_TYPE_ALIAS, SIG_TYPE_ALIAS, decl->name, decl->name);
                if (decl->type.tag == TYPE_INTERFACE) {
                    check_type_members(me, &decl->type.interface.members, nullptr, true);
                } else {
                    check_written_type(me, &decl->type);
                }
                emit_leave(me, saved);
            }
            return;
        }
        case STATEMENT_DECL_ENUM: {
            EnumDecl const* decl = statement->decl.enum_;
            if (statement_visible(me, statement, container, decl->symbol)) check_enum(me, decl);
            return;
        }
        case STATEMENT_DECL_NAMESPACE: {
            NamespaceDecl const* decl = statement->decl.namespace;
            bool augmentation = decl->is_augmentation || decl->is_global;
            // (A late pass only gets here for a namespace visible from the start, to walk what was painted inside)
            if (!augmentation && !my late_pass && !statement_visible(me, statement, container, decl->symbol)) return;
            bool ambient = container.ambient || statement->decl.props.declared || decl->is_ambient_module || decl->is_global;
            Scope const* outer_scope = my scope;
            if (decl->inner_scope) my scope = decl->inner_scope;
            check_statements(me, &decl->contents.statements, (Container){ .ambient = ambient });
            my scope = outer_scope;
            return;
        }
        case STATEMENT_EXPORT_DEFAULT_EXPR: {
            Expr const* expr = &statement->expr;
            // (An alias it names is emitted: collectLinkedAliases)
            if (expr->tag == EXPR_IDENTIFIER) {
                if (!my late_pass) paint(me, expr->identifier.symbol);
                return;
            }
            Emitting saved = emit_enter(me, EMIT_DEFAULT_EXPORT, SIG_FUNCTION, span_null, statement->span);
            // (A JavaScript one's JSDoc `@type` is its type as written)
            if (statement->expr_jsdoc_type) {
                check_written_type(me, statement->expr_jsdoc_type);
                emit_leave(me, saved);
                return;
            }
            Root outer_root = my root;
            my root = (Root){ .kind = ROOT_EXPORT_DEFAULT, .expr = expr, .at = statement->span };
            // (The `default` symbol's type: the expression's, widened)
            Ty const* type = lazy_type_get((LazyType*)&expr->lazy_type);
            // (The default export's type is the expression's widened like a variable's, fresh literals included: tsc's
    // widenTypeForVariableLikeDeclaration in getTypeOfVariableOrParameterOrProperty, `'ok' satisfies string` is string)
    type = ty_regular(my checker, ty_widened_literal(my checker, ty_widened_nullable(my checker, ty_widened_objects(my checker, type))));
            serialize_declaration(me, pseudo_of_expression(my checker, expr, (PseudoPlace){0}), type, false, false);
            my root = outer_root;
            emit_leave(me, saved);
            return;
        }
        default: return;
    }
}

static void check_statements(Walk* me, VecStatement const* statements, Container container) {
    for_each (i, vec_statement, statements) check_statement(me, i, container);
}

static void push_exported_local(Walk* me, Symbol const* symbol, u32* cap) {
    if (my exported_local_count == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        my exported_locals = realloc(my exported_locals, *cap * sizeof(Symbol const*));
    }
    my exported_locals[my exported_local_count++] = symbol;
}

// What `export { x }` and `export default x` name in a module, a namespace's `export { x }` too (tsc's
// aliasMarkingVisitor walks the whole file)
static void collect_exported_locals(Walk* me, VecStatement const* statements, u32* cap) {
    for_each (i, vec_statement, statements) {
        if (i->tag == STATEMENT_DECL_NAMESPACE) {
            collect_exported_locals(me, &i->decl.namespace->contents.statements, cap);
            continue;
        }
        if (i->tag == STATEMENT_IMPORT_EXPORT) {
            PortStatement const* port = i->import_export;
            if (port->module_ref) continue;
            for_each (j, vec_port_item, &port->items) {
                if (j->tag == PORT_EXPORT_SPECIFIC && j->source_symbol) push_exported_local(me, j->source_symbol, cap);
            }
        } else if (i->tag == STATEMENT_EXPORT_DEFAULT_EXPR && i->expr.tag == EXPR_IDENTIFIER && i->expr.identifier.symbol) {
            push_exported_local(me, i->expr.identifier.symbol, cap);
        }
    }
}

// CommonJS exports (tsgo's visitCJSExportAssignments and visitNestedExpression: anywhere in the file, in source order)
typedef struct CjsCollect {
    Walk* walk;
    Statement const* statement;
} CjsCollect;

static bool name_is_identifier(csview name) {
    bool identifier = name.size && !isdigit((unsigned char)name.buf[0]);
    for (isize_t c = 0; c < name.size; c++) {
        char ch = name.buf[c];
        identifier &= isalnum((unsigned char)ch) || ch == '_' || ch == '$' || (unsigned char)ch >= 0x80;
    }
    return identifier;
}

static void cjs_add_member(Walk* me, Expr const* expr, Expr const* value, csview name, bool top_level) {
    // (Once per name: the first, tsgo's witnessedCjsExports)
    for (u32 i = 0; i < my cjs_member_count; i++) {
        if (csview_equals_sv(my cjs_members[i].name, name)) return;
    }
    my cjs_members = realloc(my cjs_members, (my cjs_member_count + 1) * sizeof(CjsMember));
    my cjs_members[my cjs_member_count++] = (CjsMember){
        .expr = expr, .value = value, .name = name, .identifier = name_is_identifier(name), .top_level = top_level,
    };
}

static AstWalk cjs_collect_visit(Expr const* expr, void* arg) {
    CjsCollect* collect = arg;
    Walk* me = collect->walk;
    bool top_level = collect->statement->tag == STATEMENT_EXPR && &collect->statement->expr == expr && !expr_is_perenthisized(expr);
    if (expr->tag == EXPR_BINARY_OP && expr->binary_op->op == BINARY_OP_EQUAL) {
        Span name = span_null;
        CommonJsTarget target = binary_commonjs_declaration(expr->binary_op, &name);
        if (target == COMMONJS_TARGET_MODULE_EXPORTS && !expr_is_plain_identifier(&expr->binary_op->right, "exports")) {
            my cjs_export_equals = expr;
            my cjs_export_equals_top_level = top_level;
        } else if (target == COMMONJS_TARGET_EXPORTS_PROPERTY && span_valid(name)) {
            cjs_add_member(me, expr, &expr->binary_op->right, span_sv(name), top_level);
        }
    } else if (expr->tag == EXPR_CALL) {
        Span name;
        if (expr_is_define_property_call(expr, &name) && expr_is_exports_object(vec_expr_at(&expr->call->args, 0))) {
            Expr const* descriptor = vec_expr_at(&expr->call->args, 2);
            Expr const* value = nullptr;
            if (descriptor->tag == EXPR_OBJECT) {
                for_each (j, vec_object_expr_item, &descriptor->object) {
                    if (j->tag == OBJECT_ITEM_PROPERTY && !j->computed_key && cstr_equals(&j->name_string, "value")) value = &j->value;
                }
            }
            cjs_add_member(me, expr, value, span_sv(name), top_level);
        }
    }
    return AST_WALK_CONTINUE;
}

static void collect_commonjs(Walk* me) {
    for_each (i, vec_statement, &my file->ast.statements) {
        CjsCollect collect = { me, i };
        ast_walk_statement(i, cjs_collect_visit, &collect);
    }
}

// What `module.exports = v` exports: the value assigned last (`module.exports = exports = v`)
static Expr const* cjs_assigned_value(Expr const* assignment) {
    Expr const* value = &assignment->binary_op->right;
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    return value;
}

// The export symbol a member declares
static Symbol const* cjs_member_symbol(Walk* me, CjsMember const* member) {
    Symbol const* module = my file->scope ? my file->scope->container : nullptr;
    return module && module->exports ? symbol_table_get(module->exports, member->name) : nullptr;
}

// `exports.x = a` at the top level, of a name declared once: `export { a as x }` (tsgo's isCommonJSAliasExport)
static bool cjs_member_is_alias(Walk* me, CjsMember const* member) {
    // (An assignment's: `Object.defineProperty(exports, "x", { value: a })` is a variable)
    if (!member->top_level || member->expr->tag != EXPR_BINARY_OP || !member->value || member->value->tag != EXPR_IDENTIFIER || expr_is_perenthisized(member->value)) {
        return false;
    }
    Symbol const* symbol = cjs_member_symbol(me, member);
    return symbol && symbol->decl_count == 1;
}

static Expr const* cjs_unparenthesized(Expr const* expr) {
    return expr;
}

// The type a member's declaration gets: its export's (the assignments' union), else what is assigned
static Ty const* cjs_member_type(Walk* me, CjsMember const* member) {
    Symbol const* symbol = cjs_member_symbol(me, member);
    if (symbol) return ty_type_of_symbol(my checker, symbol);
    Ty const* type = member->value ? lazy_type_get((LazyType*)&member->value->lazy_type) : nullptr;
    return type ? ty_widened_literal(my checker, type) : nullptr;
}

// A member's JSDoc `@type` (`/** @type {T} */ exports.x = v`)
static TypeNode const* cjs_member_annotation(CjsMember const* member) {
    return member->expr->tag == EXPR_BINARY_OP ? member->expr->binary_op->jsdoc_type : nullptr;
}

static void cjs_serialize_member(Walk* me, CjsMember const* member) {
    Span whole = expr_outer_span(member->expr);
    Emitting saved = emit_enter(me, EMIT_VARIABLE, SIG_FUNCTION, whole, whole);
    my emitting.name_not_node = true;
    static Pseudo const no_result = { .kind = PSEUDO_NO_RESULT };
    Pseudo const* pseudo = member->value ? pseudo_of_expression(my checker, member->value, (PseudoPlace){0}) : &no_result;
    serialize_declaration(me, pseudo, cjs_member_type(me, member), false, false);
    emit_leave(me, saved);
}

// The first walk of the declarations the exports make (painting what they name; their diagnostics aren't reported:
// check_js_commonjs_exports() reports those tsc's do)
static void check_commonjs_exports(Walk* me) {
    if (my cjs_export_equals) {
        Expr const* value = cjs_assigned_value(my cjs_export_equals);
        Span at = expr_outer_span(my cjs_export_equals);
        TypeNode const* annotation = my cjs_export_equals->binary_op->jsdoc_type;
        if (value->tag == EXPR_IDENTIFIER && my cjs_export_equals_top_level) {
            paint(me, value->identifier.symbol);
        } else if (annotation) {
            Emitting saved = emit_enter(me, EMIT_DEFAULT_EXPORT, SIG_FUNCTION, span_null, at);
            check_written_type(me, annotation);
            emit_leave(me, saved);
        } else if (value->tag == EXPR_CLASS_DECL) {
            check_class(me, value->class_decl, at);
        } else if (value->tag == EXPR_FUNCTION) {
            FunctionImpl const* fn = value->function;
            check_written_signature(me, fn, SIG_FUNCTION, true);
            check_declaration_parameters(me, fn, SIG_FUNCTION);
            check_declaration_return(me, fn, SIG_FUNCTION, (Span){0}, at);
        } else {
            Emitting saved = emit_enter(me, EMIT_DEFAULT_EXPORT, SIG_FUNCTION, span_null, at);
            Ty const* type = lazy_type_get((LazyType*)&value->lazy_type);
            type = ty_regular(my checker, ty_widened_literal(my checker, ty_widened_nullable(my checker, ty_widened_objects(my checker, type))));
            serialize_declaration(me, pseudo_of_expression(my checker, value, (PseudoPlace){0}), type, false, false);
            emit_leave(me, saved);
        }
    }
    for (u32 i = 0; i < my cjs_member_count; i++) {
        CjsMember const* member = &my cjs_members[i];
        if (cjs_member_is_alias(me, member)) {
            paint(me, member->value->identifier.symbol);
            continue;
        }
        Expr const* value = member->value ? cjs_unparenthesized(member->value) : nullptr;
        if (value && value->tag == EXPR_CLASS_DECL) {
            check_class(me, value->class_decl, expr_outer_span(member->expr));
            continue;
        }
        if (cjs_member_annotation(member)) {
            Span whole = expr_outer_span(member->expr);
            Emitting saved = emit_enter(me, EMIT_VARIABLE, SIG_FUNCTION, whole, whole);
            check_written_type(me, cjs_member_annotation(member));
            emit_leave(me, saved);
        } else {
            cjs_serialize_member(me, member);
        }
    }
}

// Declaration emit's writer (plan 64)
//
// A second walk over what the first one found to be emitted: the first walk reports, painting what emitted
// declarations name (late painting), the second writes the `.d.ts` text, a painted declaration where it is (tsc's
// transformAndReplaceLatePaintedStatements), its diagnostics into a scratch ErrorCtx. The types it writes are those the
// serialize_*() functions decide on (`type_out`): a declaration's pseudo type where it says its type, else its type.
// The layout is tsc's printer's for declaration files: 4 spaces of indentation, one declaration or member per line.
//
// Not done yet (plan 64 stage 2 and on): comments, triple-slash references, import attributes, `stripInternal` beyond
// what the walk skips, expandos, class expressions as bases (`_base` variables), JavaScript

static TyDisplay emit_display(Walk* me, cstr* target) {
    // (Somewhere in the file, which names modules from there: the start when no declaration is being emitted)
    Span at = my emitting.name;
    if (!span_valid(at) || span_source(at) != my file) {
        char const* start = cstr_str(&my file->contents);
        at = span_make((SourceFile*)my file, start, start);
    }
    return (TyDisplay){
        .target = target, .target_start = cstr_size(target), .types_baseline = true, .enclosing = my scope, .tracker = &my tracker, .at = at,
        .multiline = true, .indent = my indent, .comments = &my comments,
        .printed = my emitting.printed ? my emitting.printed : &my printed_types,
        .printed_params = my in_return ? &my return_params : nullptr,
    };
}

static void w(Walk* me, csview text) { cstr_append_sv(my out, text); }
static void emit_expando_namespace(Walk* me, Symbol const* host, Span name, bool mapped, csview modifiers);
#define w_lit(me, s) w(me, c_sv(s))

static bool w_at_line_start(Walk* me) {
    char last = decl_map_last_char(cstr_sv(my out));
    return !last || last == '\n';
}

// Declaration maps: `out` is where the source node at `at` starts or ends (tsc's emitSourceMapsBeforeNode/AfterNode)
static void m(Walk* me, char const* at) {
    if (!my map || !at) return;
    char const* text = cstr_str(&my file->contents);
    if (at < text || at > text + cstr_size(&my file->contents)) return;
    decl_map_mark(my out, (u32)(at - text));
}

// (A span's end may take in the whitespace after it: a node ends at its last token)
static void m_span(Walk* me, Span span, bool end) {
    if (span_valid(span)) m(me, end ? decl_map_back_trivia(span_end_ptr(span), span.buf) : span.buf);
}

// A name as written, mapped
static void w_name(Walk* me, Span name) {
    m_span(me, name, false);
    w(me, span_sv(name));
    m_span(me, name, true);
}

// An entity name expression as written, its names mapped (a property access from its left's start)
static void w_entity_expr(Walk* me, Expr const* expr) {
    if (expr->tag == EXPR_PROPERTY && expr->property->form == PROPERTY_EXPR_DOT) {
        m_span(me, expr->inner_span, false);
        w_entity_expr(me, &expr->property->expr);
        w_lit(me, ".");
        w_name(me, property_name(expr->property));
        m_span(me, expr->inner_span, true);
    } else if (expr->tag == EXPR_IDENTIFIER) {
        w_name(me, expr->inner_span);
    } else {
        w_name(me, expr_outer_span(expr));
    }
}

// A member's name as written: a computed one is mapped from its `[` to its `]` with what is inside
static void w_member_name(Walk* me, Span name, Expr const* computed_key) {
    if (!my map || !computed_key || !span_valid(name) || name.buf[0] != '[' ||
        !(computed_key->tag == EXPR_IDENTIFIER || (computed_key->tag == EXPR_PROPERTY && computed_key->property->form == PROPERTY_EXPR_DOT) ||
          (computed_key->tag == EXPR_LITERAL))
    ) {
        w_name(me, name);
        return;
    }
    m_span(me, name, false);
    w_lit(me, "[");
    w_entity_expr(me, computed_key);
    w_lit(me, "]");
    m_span(me, name, true);
}

// Where a statement ends (tsc's node end): its last token, its `;` included where it has one
static char const* statement_end_in(Walk* me, Statement const* statement) {
    if (!my map || !span_valid(statement->span)) return nullptr;
    bool braces = statement->tag == STATEMENT_DECL_CLASS || statement->tag == STATEMENT_DECL_INTERFACE ||
        statement->tag == STATEMENT_DECL_ENUM || statement->tag == STATEMENT_DECL_NAMESPACE ||
        (statement->tag == STATEMENT_DECL_FUNCTION && statement->decl.function.impl->has_body);
    return decl_comments_end(&my comments, span_end_ptr(statement->span), braces ? nullptr : ";");
}
#define statement_end(statement) statement_end_in(me, statement)

// Declaration maps: where the token `c` is after `from`, past trivia (null if it isn't)
static char const* m_token_after(Walk* me, char const* from, char c) {
    if (!my map || !from) return nullptr;
    char const* end = cstr_str(&my file->contents) + cstr_size(&my file->contents);
    char const* at = decl_map_skip_trivia(from, end);
    return at < end && *at == c ? at : nullptr;
}

// Declaration maps: where the keyword `word` is right before `at` (whitespace between), null if it isn't
static char const* m_keyword_before(Walk* me, char const* at, char const* word) {
    if (!my map || !at) return nullptr;
    char const* text = cstr_str(&my file->contents);
    if (at < text || at > text + cstr_size(&my file->contents)) return nullptr;
    char const* c = at;
    while (c > text && (c[-1] == ' ' || c[-1] == '\t' || c[-1] == '\n' || c[-1] == '\r')) c--;
    size_t n = strlen(word);
    if (c - text < (isize_t)n || memcmp(c - n, word, n)) return nullptr;
    return c - n;
}

// (Nothing after a leading comment that ended on the line the declaration starts: `/** a */ a: T;`)
static void w_indent(Walk* me) {
    if (!w_at_line_start(me)) return;
    for (u32 i = 0; i < my indent; i++) w_lit(me, "    ");
}

// A line break unless at the start of a line (tsc's writeLine)
static void w_line(Walk* me) {
    if (!w_at_line_start(me)) w_lit(me, "\n");
}

// The leading comments of a node about to be written, from the first token at `token` (decl_comments.h). Returns where
// they started: w_unless_written() takes them back when nothing was written after them
static isize_t w_leading(Walk* me, char const* token) {
    isize_t start = cstr_size(my out);
    decl_comments_leading(&my comments, my out, my indent, token);
    return start;
}

// After a node: false (and its leading comments taken back) if nothing was written since they were, else its trailing
// comments after `end`, before the line break that ends it
static bool w_unless_written(Walk* me, isize_t start, isize_t written_from, char const* end) {
    if (cstr_size(my out) == written_from) {
        cstr_erase(my out, start, cstr_size(my out) - start);
        return false;
    }
    if (!end) return true;
    isize_t size = decl_map_text_end(cstr_sv(my out));
    bool line_break = size && cstr_str(my out)[size - 1] == '\n';
    if (line_break) cstr_erase(my out, size - 1, 1);
    decl_comments_trailing(&my comments, my out, my indent, end);
    if (line_break) w_lit(me, "\n");
    return true;
}

// (While a w_*() helper prints, what the tracker's callbacks serialize isn't written)
#define SINKS_OFF cstr* sinks_type = my type_out; cstr* sinks_pseudo = my pseudo_out; my type_out = my pseudo_out = nullptr
#define SINKS_ON my type_out = sinks_type; my pseudo_out = sinks_pseudo

// `visited`: a node tsgo's transformer visits in JavaScript too (a type alias's type, heritage type arguments, an
// interface's members), not an annotation, which goes through the node builder there (TryJSTypeNodeToTypeNode)
static void w_node_described(Walk* me, TypeNode const* node, bool ends_declaration, bool visited, Interface const* described) {
    SINKS_OFF;
    TyParamNames names = walk_param_names(me);
    // (JavaScript's annotations go through the node builder, tsgo's TryJSTypeNodeToTypeNode)
    names.as_written = !my file->is_js;
    TyDisplay d = emit_display(me, my out);
    d.param_names = &names;
    d.builder_part = my file->is_js && !visited;
    d.jsdoc_described = described;
    // (The declaration ends with it: its trailing comments are the declaration's, which the caller writes)
    if (ends_declaration) d.comments_container_end = decl_map_type_end(node, my file);
    ty_display_node(my checker, d, node);
    ty_param_names_drop(&names);
    SINKS_ON;
}

static void w_node_in(Walk* me, TypeNode const* node, bool ends_declaration, bool visited) {
    w_node_described(me, node, ends_declaration, visited, nullptr);
}

static void w_node(Walk* me, TypeNode const* node) { w_node_in(me, node, false, false); }
static void w_node_visited(Walk* me, TypeNode const* node) { w_node_in(me, node, false, true); }
// A parameter's or variable's annotation
static void w_node_last(Walk* me, TypeNode const* node) { w_node_in(me, node, true, false); }

static void w_type(Walk* me, Ty const* type) {
    SINKS_OFF;
    TyParamNames names = walk_param_names(me);
    TyDisplay d = emit_display(me, my out);
    d.param_names = &names;
    ty_display(my checker, d, type);
    ty_param_names_drop(&names);
    SINKS_ON;
}

static void w_type_params(Walk* me, Generic const* generic) {
    SINKS_OFF;
    TyParamNames names = walk_param_names(me);
    TyDisplay d = emit_display(me, my out);
    d.param_names = &names;
    ty_display_type_params(my checker, d, generic);
    ty_param_names_drop(&names);
    SINKS_ON;
}

// What a serialize_*() call decided to write, written (`any` when nothing: tsc's ensureType without a type node)
static void w_serialized(Walk* me, cstr* text) {
    w(me, cstr_size(text) ? cstr_sv(text) : c_sv("any"));
    cstr_drop(text);
}

static bool emit_visible(Walk* me, Statement const* statement, Container container, Symbol const* symbol) {
    return statement_visible_at_first(me, statement, container, symbol) || was_painted(me, symbol);
}

// Whether the statement list is a file's (else a namespace body's)
static bool container_is_file(Container container) { return container.global || container.module_top_level; }

// Modifiers to write, in tsc's order (createModifiersFromModifierFlags)
typedef struct Modifiers {
    char const* words[8];
    u32 count;
} Modifiers;

static void mods_add(Modifiers* mods, char const* word) {
    if (mods->count < c_arraylen(mods->words)) mods->words[mods->count++] = word;
}

static bool is_word_char(char c) { return isalnum((unsigned char)c) || c == '_' || c == '$' || (unsigned char)c >= 0x80; }

// The modifiers written before a declaration that starts at `from` in the source (decorators skipped): each one's
// position and length. A word is one where another word, a name or a bracket follows it (`readonly: T` is a name)
static u32 source_modifiers(Walk* me, char const* from, char const** at, u32* size, u32 cap) {
    static char const* const words[] = { "export", "default", "declare", "abstract", "static", "readonly", "private",
        "protected", "public", "async", "override", "accessor", "const" };
    char const* end = cstr_str(&my file->contents) + cstr_size(&my file->contents);
    if (!from || from < cstr_str(&my file->contents) || from > end) return 0;
    u32 count = 0;
    for (char const* c = decl_map_skip_trivia(from, end); c < end && count < cap; c = decl_map_skip_trivia(c, end)) {
        if (*c == '@') {
            // `@a.b(...)`
            c++;
            while (c < end && (is_word_char(*c) || *c == '.')) c++;
            c = decl_map_skip_trivia(c, end);
            if (c < end && *c == '(') {
                u32 depth = 0;
                for (; c < end; c++) {
                    if (*c == '(') depth++;
                    else if (*c == ')' && !--depth) {
                        c++;
                        break;
                    }
                }
            }
            continue;
        }
        char const* word_end = c;
        while (word_end < end && is_word_char(*word_end)) word_end++;
        u32 n = (u32)(word_end - c);
        bool modifier = false;
        for (u32 i = 0; i < c_arraylen(words) && !modifier; i++) modifier = strlen(words[i]) == n && !memcmp(words[i], c, n);
        if (!modifier) break;
        char const* next = decl_map_skip_trivia(word_end, end);
        if (next >= end || !(is_word_char(*next) || *next == '[' || *next == '#' || *next == '*' || *next == '"' || *next == '\'')) break;
        // (`const` is one in `const enum` only)
        if (n == 5 && !memcmp(c, "const", 5) && !(end - next > 4 && !memcmp(next, "enum", 4) && !is_word_char(next[4]))) break;
        at[count] = c;
        size[count++] = n;
        c = word_end;
    }
    return count;
}

// Writes the modifiers (each followed by a space). Where the declaration's modifiers in the source are the same set
// (tsc's ensureModifiers reuses their nodes when the flags didn't change) they're written as there, in their order, each
// mapped; else they're new. Returns where `export` was written in `out`, -1 if it wasn't
static isize_t w_modifiers(Walk* me, char const* from, Modifiers mods) {
    char const* at[8];
    u32 size[8];
    u32 count = source_modifiers(me, from, at, size, 8);
    bool same = count == mods.count;
    for (u32 i = 0; i < mods.count && same; i++) {
        bool found = false;
        for (u32 j = 0; j < count && !found; j++) found = strlen(mods.words[i]) == size[j] && !memcmp(mods.words[i], at[j], size[j]);
        same = found;
    }
    isize_t export_at = -1;
    if (same) {
        for (u32 i = 0; i < count; i++) {
            if (size[i] == 6 && !memcmp(at[i], "export", 6)) export_at = cstr_size(my out);
            m(me, at[i]);
            w(me, (csview){ at[i], size[i] });
            m(me, at[i] + size[i]);
            w_lit(me, " ");
        }
        return export_at;
    }
    for (u32 i = 0; i < mods.count; i++) {
        if (!strcmp(mods.words[i], "export")) export_at = cstr_size(my out);
        w(me, csview_from(mods.words[i]));
        w_lit(me, " ");
    }
    return export_at;
}

// Erases `chars` characters of `out` from `at`, and the marks among them (decl_map.h)
static void erase_text(cstr* out, isize_t at, isize_t chars) {
    char const* text = cstr_str(out);
    isize_t size = cstr_size(out), end = at;
    while (end < size && chars) {
        if (text[end] == DECL_MAP_MARK && end + DECL_MAP_MARK_SIZE <= size) {
            end += DECL_MAP_MARK_SIZE;
            continue;
        }
        end++;
        chars--;
    }
    cstr_erase(out, at, end - at);
}

// erase_text() at each of `count` ascending offsets, in one pass (one by one a big namespace body is quadratic)
static void erase_texts(cstr* out, u32 const* offsets, u32 count, isize_t chars) {
    if (!count) return;
    char* text = cstr_data(out);
    isize_t size = cstr_size(out), to = offsets[0], from = offsets[0];
    for (u32 i = 0; i < count; i++) {
        check(offsets[i] >= from);
        memmove(text + to, text + from, (size_t)(offsets[i] - from));
        to += offsets[i] - from;
        from = offsets[i];
        for (isize_t left = chars; from < size && left;) {
            if (text[from] == DECL_MAP_MARK && from + DECL_MAP_MARK_SIZE <= size) {
                from += DECL_MAP_MARK_SIZE;
                continue;
            }
            from++;
            left--;
        }
    }
    memmove(text + to, text + from, (size_t)(size - from));
    cstr_resize(out, to + (size - from), '\0');
}

// A top-level declaration's modifiers (tsc's ensureModifiers): its `export` and `default`, and `declare` at a file's top
// level unless it is a type or a default export (needsDeclare). In a namespace body, where each `export ` is written
// is remembered (see Walk.export_offsets)
static void w_statement_modifiers_and(Walk* me, Statement const* statement, Container container, bool always_type, char const* more) {
    DeclExported exported = statement_declares_name(statement->tag) || statement->tag == STATEMENT_DECL_VARIABLE ?
        statement->decl.props.exported : DECL_EXPORTED_NO;
    if (implicitly_exported_jsdoc(statement, container)) exported = DECL_EXPORTED_YES;
    bool is_file = container_is_file(container);
    Modifiers mods = {0};
    if (exported != DECL_EXPORTED_NO) {
        mods_add(&mods, "export");
        if (is_file) my has_module_indicator = true;
    } else {
        my needs_scope_fix = true;
    }
    if (exported == DECL_EXPORTED_YES_DEFAULT) {
        mods_add(&mods, "default");
    } else if (is_file && (!always_type || span_valid(statement->decl.props.declare_keyword))) {
        // (A type's `declare` is kept where it is written)
        mods_add(&mods, "declare");
    }
    if (more) mods_add(&mods, more);
    isize_t export_at = w_modifiers(me, statement->span.buf, mods);
    if (export_at >= 0 && !is_file) {
        if (my export_offset_count == my export_offset_cap) {
            my export_offset_cap = my export_offset_cap ? my export_offset_cap * 2 : 8;
            my export_offsets = realloc(my export_offsets, my export_offset_cap * sizeof(u32));
        }
        my export_offsets[my export_offset_count++] = (u32)export_at;
    }
}

static void w_statement_modifiers(Walk* me, Statement const* statement, Container container, bool always_type) {
    w_statement_modifiers_and(me, statement, container, always_type, nullptr);
}

// A parameter (ensureParameter): its name without initializers, `?` where it is optional, its type
static void w_parameter_inner(Walk* me, FunctionImpl const* fn, FunctionArg const* arg, bool private_member);

static void w_parameter(Walk* me, FunctionImpl const* fn, FunctionArg const* arg, bool private_member) {
    m_span(me, arg->span, false);
    w_parameter_inner(me, fn, arg, private_member);
    m_span(me, arg->span, true);
}

static void w_parameter_inner(Walk* me, FunctionImpl const* fn, FunctionArg const* arg, bool private_member) {
    Binding const* binding = &arg->initializer.binding;
    if (arg->rest) {
        Span rest = function_arg_rest_span(arg);
        if (span_valid(rest)) m(me, span_end_ptr(rest) - 3);
        w_lit(me, "...");
        if (span_valid(rest)) m(me, span_end_ptr(rest));
    }
    if (binding->tag == BINDING_ARRAY || binding->tag == BINDING_OBJECT) {
        cstr pattern = cstr_init();
        ty_append_binding_pattern_commented(&pattern, binding, my map ? my file : nullptr, &my comments, my indent);
        w(me, cstr_sv(&pattern));
        cstr_drop(&pattern);
    } else {
        w_name(me, binding->content_span);
    }
    bool is_this = binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this");
    if (!is_this && is_optional_parameter(fn, arg)) {
        // (The `?` written, or one added for an initializer: new)
        Span question = arg->explicit_optional ? function_arg_question_span(arg) : span_null;
        if (!span_valid(question) && arg->explicit_optional && arg->jsdoc_question && span_valid(*arg->jsdoc_question)) {
            // (A JSDoc `[name]`'s question token is its tag, ending exactly where the tag does: tsgo's reparser)
            m(me, arg->jsdoc_question->buf);
            w_lit(me, "?");
            m(me, span_end_ptr(*arg->jsdoc_question));
        } else {
            m_span(me, question, false);
            w_lit(me, "?");
            m_span(me, question, true);
        }
    }
    if (private_member) return;
    w_lit(me, ": ");
    if (binding->has_explicit_type && (is_this || !requires_adding_undefined(me, fn, arg, true))) {
        w_node_last(me, binding_type(binding));
        return;
    }
    cstr text = cstr_init();
    cstr* outer = my type_out;
    my type_out = &text;
    my printed_next = fn; // (A function's parameters share its cache)
    Emitting saved = emit_enter(me, EMIT_PARAMETER, SIG_FUNCTION, binding->content_span, binding->content_span);
    serialize_parameter(me, fn, arg, true);
    emit_leave(me, saved);
    my type_out = outer;
    w_serialized(me, &text);
}

// A parameter in a list, with the comments around it (tsc's emitListItems)
static void w_parameter_with_comments(Walk* me, FunctionImpl const* fn, FunctionArg const* arg) {
    decl_comments_intervening(&my comments, my out, my indent, arg->span.buf);
    w_leading(me, arg->span.buf);
    w_indent(me);
    w_parameter(me, fn, arg, false);
    decl_comments_trailing(&my comments, my out, my indent, span_end_ptr(arg->span));
}

static void w_parameters(Walk* me, FunctionImpl const* fn, bool private_member) {
    w_lit(me, "(");
    if (!private_member) {
        for_each (i, vec_function_arg, &fn->args) {
            if (i_index) w_lit(me, ", ");
            w_parameter_with_comments(me, fn, i);
        }
    }
    w_lit(me, ")");
}

// A signature's return type (ensureType of a function-like): as written, else serialized
static void w_return(Walk* me, FunctionImpl const* fn, bool getter_pair) {
    w_lit(me, ": ");
    if (fn->predicate) {
        TypePredicateNode const* predicate = fn->predicate;
        m_span(me, predicate->span, false);
        if (predicate->asserts) {
            // (The `asserts` modifier is a token node)
            m_span(me, predicate->span, false);
            w_lit(me, "asserts");
            if (span_valid(predicate->span)) m(me, predicate->span.buf + 7);
            w_lit(me, " ");
        }
        w_name(me, predicate->name);
        if (predicate->has_type) {
            w_lit(me, " is ");
            w_node(me, &predicate->type);
        }
        m_span(me, predicate->span, true);
        return;
    }
    if (fn->ret_explicit) {
        w_node(me, &fn->ret);
        return;
    }
    // (An inferred type predicate is written as one)
    Signature const* sig = getter_pair ? nullptr : ty_function_signature(my checker, fn);
    TyPredicate const* predicate = sig ? signature_predicate(my checker, sig) : nullptr;
    if (predicate) {
        SINKS_OFF;
        TyParamNames names = walk_param_names(me);
        TyDisplay d = emit_display(me, my out);
        d.param_names = &names;
        ty_display_predicate(my checker, d, predicate);
        ty_param_names_drop(&names);
        SINKS_ON;
        return;
    }
    cstr text = cstr_init();
    cstr* outer = my type_out;
    my type_out = &text;
    Emitting saved = emit_enter(me, EMIT_RETURN, SIG_FUNCTION, fn->name, fn->name);
    Pseudo const* pseudo = getter_pair ? pseudo_of_accessors(my checker, fn, my class_pair.setter, false) :
        pseudo_of_return(my checker, fn);
    Signature const* own = getter_pair ? nullptr : ty_function_signature(my checker, fn);
    TyDisplayParams outer_params = my return_params;
    bool outer_in_return = my in_return;
    if (own) {
        signature_resolve(my checker, own);
        my return_params = (TyDisplayParams){ .params = own->params, .count = own->param_count, .function = fn };
        my in_return = true;
    }
    serialize_return(me, fn, pseudo);
    my return_params = outer_params;
    my in_return = outer_in_return;
    emit_leave(me, saved);
    my type_out = outer;
    w_serialized(me, &text);
}

// `<T>(a: A): R` of a function declaration or method
static void w_signature(Walk* me, FunctionImpl const* fn, bool private_member) {
    if (private_member) {
        w_lit(me, "()");
        return;
    }
    // (A JavaScript function typed by a generic JSDoc `@type {F}`: its signature's type parameters, tsgo's
    // CreateTypeParametersOfSignatureDeclaration of the FullSignature, so the whole signature is printed)
    if (my file->is_js && generic_empty(&fn->generic) && fn->rare && function_impl_rare(fn)->jsdoc_signature_type) {
        Signature const* sig = ty_function_signature(my checker, fn);
        if (sig && sig->type_params.len) {
            // (Its type parameter list, cut from the printed signature: the parameters are the function's own)
            SINKS_OFF;
            cstr printed = cstr_init();
            TyParamNames names = walk_param_names(me);
            TyDisplay d = emit_display(me, &printed);
            d.param_names = &names;
            d.tracker = nullptr;
            ty_display_signature(my checker, d, sig);
            ty_param_names_drop(&names);
            SINKS_ON;
            csview text = cstr_sv(&printed);
            isize_t end = 0;
            for (isize_t i = 0, depth = 0; i < text.size; i++) {
                if (text.buf[i] == '<') depth++;
                else if (text.buf[i] == '>' && (i == 0 || text.buf[i - 1] != '=') && !--depth) {
                    end = i + 1;
                    break;
                }
            }
            if (csview_starts_with(text, "<") && end) w(me, csview_slice(text, 0, end));
            cstr_drop(&printed);
            w_parameters(me, fn, false);
            if (!fn->is_constructor) w_return(me, fn, false);
            return;
        }
    }
    w_type_params(me, &fn->generic);
    w_parameters(me, fn, false);
    if (!fn->is_constructor) w_return(me, fn, false);
}

// The type a variable is written with (ensureType, or its literal initializer: shouldPrintWithInitializer)
// The type of a declaration that is the unique symbol it declares: `unique symbol` (the node builder names the symbol
// everywhere else)
static bool w_own_unique_symbol(Walk* me, Ty const* type, Symbol const* symbol) {
    if (!type || !symbol || !(type->flags & TYF_UNIQUE_ES_SYMBOL) || type->literal.unique->owner != symbol) return false;
    w_lit(me, "unique symbol");
    return true;
}

// A literal type as the initializer that says it (tsc's createLiteralConstValue): an expression, so an enum member that
// isn't an identifier is `E["a b"]`, not the type's `(typeof E)["a b"]`
static void w_literal_value(Walk* me, Ty const* type) {
    // (A string is a string literal node it makes, which the printer escapes to ASCII)
    if (type->kind == TY_LITERAL && type->literal.string && !(type->flags & TYF_ENUM_LITERAL)) {
        TyDisplay d = emit_display(me, my out);
        d.tracker = nullptr; // (No length limit: it isn't a type the node builder serializes)
        ty_display_synthesized_string(d, atom_sv(type->literal.string));
        return;
    }
    isize_t start = cstr_size(my out);
    w_type(me, type);
    csview written = csview_slice(cstr_sv(my out), start, cstr_size(my out));
    if (!csview_starts_with(written, "(typeof ")) return;
    isize_t close = csview_find_sv(written, c_sv(")["));
    if (close == c_NPOS) return;
    cstr_erase(my out, start + close, 1);
    cstr_erase(my out, start, 8);
}

static void w_variable_type(Walk* me, InitializedBinding const* decl, bool is_const) {
    Binding const* binding = &decl->binding;
    Expr const* init = initialized_binding_value(decl);
    if (is_const && init->tag != EXPR_IMPLICIT_ANY && is_literal_const(me, binding->symbol)) {
        w_lit(me, " = ");
        w_literal_value(me, ty_type_of_symbol(my checker, binding->symbol));
        return;
    }
    w_lit(me, ": ");
    if (binding->has_explicit_type) {
        w_node_last(me, binding_type(binding));
        return;
    }
    if (binding->symbol && w_own_unique_symbol(me, ty_type_of_symbol(my checker, binding->symbol), binding->symbol)) return;
    cstr text = cstr_init();
    cstr* outer = my type_out;
    my type_out = &text;
    Emitting saved = emit_enter(me, EMIT_VARIABLE, SIG_FUNCTION, binding->content_span, binding->content_span);
    serialize_variable(me, decl, is_const);
    emit_leave(me, saved);
    my type_out = outer;
    w_serialized(me, &text);
}

// tsgo's getBindingNameVisible of a pattern: some name in it is visible (painted by an alias, `export { a }`)
static bool binding_name_visible(Walk* me, Binding const* pattern, bool module_top_level) {
    if (pattern->tag == BINDING_IDENTIFIER) {
        return was_painted(me, pattern->symbol) || (module_top_level && is_exported_local(me, pattern->symbol));
    }
    if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) if (binding_name_visible(me, i, module_top_level)) return true;
    } else if (pattern->tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &pattern->object) if (binding_name_visible(me, &i->target, module_top_level)) return true;
    }
    return false;
}

static bool binding_declares_names(Binding const* pattern) {
    if (pattern->tag == BINDING_IDENTIFIER) return true;
    if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) if (binding_declares_names(i)) return true;
    } else if (pattern->tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &pattern->object) if (binding_declares_names(&i->target)) return true;
    }
    return false;
}

// The names a binding pattern declares, each a declaration of its own type (walkBindingPattern)
static void w_binding_elements(Walk* me, Binding const* pattern, Container container, bool visible, u32* written) {
    if (pattern->tag == BINDING_IDENTIFIER) {
        if (!visible && !was_painted(me, pattern->symbol) && !is_exported_local(me, pattern->symbol)) return;
        if ((*written)++) w_lit(me, ", ");
        w(me, span_sv(pattern->content_span));
        w_lit(me, ": ");
        w_type(me, pattern->symbol ? ty_type_of_symbol(my checker, pattern->symbol) : ty_intrinsic(my checker, TY_ANY));
    } else if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) w_binding_elements(me, i, container, visible, written);
    } else if (pattern->tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &pattern->object) w_binding_elements(me, &i->target, container, visible, written);
    }
}

static void emit_variable_statement(Walk* me, Statement const* statement, Container container) {
    VariableStatement const* variable = &statement->decl.variable;
    bool visible = container.global || container.ambient || statement->decl.props.exported != DECL_EXPORTED_NO;
    bool is_const = variable_statement_is_const(variable);
    // A function expression with expandos is a function declaration and a namespace (transformExpandoHost)
    if (vec_initialized_binding_size(&variable->initializers) == 1) {
        InitializedBinding const* only = vec_initialized_binding_at(&variable->initializers, 0);
        Expr const* init = initialized_binding_value(only);
        if (only->binding.tag == BINDING_IDENTIFIER && !only->binding.has_explicit_type && init->tag == EXPR_FUNCTION &&
            (visible || was_painted(me, only->binding.symbol) ||
                (container.module_top_level && is_exported_local(me, only->binding.symbol))) &&
            is_expando_variable(me, &only->binding, init)
        ) {
            FunctionImpl const* fn = init->function;
            // (A function declaration it makes anew: the statement's comments aren't copied)
            cstr_erase(my out, my statement_start, cstr_size(my out) - my statement_start);
            my statement_synthesized = true;
            w_indent(me);
            isize_t modifiers_start = (isize_t)cstr_size(my out);
            w_statement_modifiers(me, statement, container, false);
            cstr modifiers = cstr_from_sv(csview_slice(cstr_sv(my out), (isize_t)modifiers_start, cstr_size(my out)));
            decl_map_strip(&modifiers);
            w_lit(me, "function ");
            w(me, span_sv(only->binding.content_span));
            w_signature(me, fn, false);
            w_lit(me, ";\n");
            emit_expando_namespace(me, only->binding.symbol, only->binding.content_span, false, cstr_sv(&modifiers));
            cstr_drop(&modifiers);
            return;
        }
    }
    cstr declarations = cstr_init();
    cstr* outer = my out;
    my out = &declarations;
    u32 written = 0;
    for_each (i, vec_initialized_binding, &variable->initializers) {
        Binding const* binding = &i->binding;
        // A CommonJS file's `const x = require("m")` is `import x = require("m")`, `const { a, b: c } = require("m")`
        // `import { a, b as c } from "m"` (tsgo's transformCjsRequireVariableDeclaration), before the rest
        if (my file->is_js && my file->is_commonjs && expr_is_require_call(initialized_binding_value(i)) &&
            vec_expr_at(&initialized_binding_value(i)->call->args, 0)->tag == EXPR_LITERAL &&
            vec_expr_at(&initialized_binding_value(i)->call->args, 0)->literal.tag == VALUE_STRING
        ) {
            if (binding->tag == BINDING_ARRAY) continue;
            bool shown = visible || binding_name_visible(me, binding, container.module_top_level);
            for (u32 k = 0; !shown && i->rare && k < initialized_binding_rare(i)->require_port_count; k++) {
                shown = was_painted(me, initialized_binding_rare(i)->require_ports[k].symbol);
            }
            if (!shown) continue;
            csview specifier = span_sv(vec_expr_at(&initialized_binding_value(i)->call->args, 0)->inner_span);
            my out = outer;
            w_indent(me);
            if (binding->tag == BINDING_IDENTIFIER) {
                w_lit(me, "import ");
                w(me, span_sv(binding->content_span));
                w_lit(me, " = require(");
                w(me, specifier);
                w_lit(me, ");\n");
            } else {
                w_lit(me, "import { ");
                u32 names = 0;
                for_each (j, vec_object_binding_item, &binding->object) {
                    if (j->target.tag != BINDING_IDENTIFIER) continue;
                    if (names++) w_lit(me, ", ");
                    csview name = span_sv(j->target.content_span);
                    if (!j->computed_key && span_valid(j->source_span) && !csview_equals_sv(span_sv(j->source_span), name)) {
                        w(me, span_sv(j->source_span));
                        w_lit(me, " as ");
                    }
                    w(me, name);
                }
                w(me, names ? c_sv(" } from ") : c_sv("} from "));
                w(me, specifier);
                w_lit(me, ";\n");
            }
            my has_module_indicator = true;
            my out = &declarations;
            continue;
        }
        if (binding->tag != BINDING_IDENTIFIER && binding_has_initializers(binding)) {
            w_binding_elements(me, binding, container, visible, &written);
            continue;
        }
        if (binding->tag != BINDING_IDENTIFIER) {
            // (Without initializers in it, the pattern is written with its type, whole when a name in it is visible;
            // an empty one isn't)
            if (!visible && !binding_name_visible(me, binding, container.module_top_level)) continue;
            if (!binding_declares_names(binding)) continue;
            if (written++) w_lit(me, ", ");
            m_span(me, binding->content_span, false);
            cstr pattern = cstr_init();
            ty_append_binding_pattern_mapped(&pattern, binding, my map ? my file : nullptr);
            w(me, cstr_sv(&pattern));
            cstr_drop(&pattern);
            w_lit(me, ": ");
            Expr const* init = initialized_binding_value(i);
            if (binding->has_explicit_type) {
                w_node_last(me, binding_type(binding));
            } else {
                Ty const* type = init->tag != EXPR_IMPLICIT_ANY ? lazy_type_get((LazyType*)&init->lazy_type) : nullptr;
                w_type(me, type ? ty_widened_literal(my checker, ty_widened_objects(my checker, ty_regular(my checker, type))) :
                    ty_intrinsic(my checker, TY_ANY));
            }
            m_span(me, init->tag != EXPR_IMPLICIT_ANY ? expr_outer_span(init) :
                binding->has_explicit_type ? binding_explicit_type_span(binding) : binding->content_span, true);
            continue;
        }
        if (!visible && !was_painted(me, binding->symbol) && !(container.module_top_level && is_exported_local(me, binding->symbol))) {
            continue;
        }
        if (written++) w_lit(me, ", ");
        decl_comments_intervening(&my comments, my out, my indent, binding->content_span.buf);
        w_leading(me, binding->content_span.buf);
        if (cstr_size(my out)) w_indent(me); // (The list is written apart: empty, it is in the middle of a line)
        m_span(me, binding->content_span, false);
        w_name(me, binding->content_span);
        w_variable_type(me, i, is_const);
        // (The declaration ends after its initializer, else its type)
        Expr const* init = initialized_binding_value(i);
        Span end = init->tag != EXPR_IMPLICIT_ANY ? expr_outer_span(init) :
            binding->has_explicit_type ? binding_explicit_type_span(binding) : binding->content_span;
        m_span(me, end, true);
        if (span_valid(end)) decl_comments_trailing(&my comments, my out, my indent, span_end_ptr(end));
    }
    my out = outer;
    if (written) {
        w_indent(me);
        m_span(me, statement->span, false);
        w_statement_modifiers(me, statement, container, false);
        // (The declaration list, from its keyword)
        m_span(me, variable->span, false);
        bool const_like = variable->keyword == VAR_INIT_USING || variable->keyword == VAR_INIT_AWAIT_USING;
        w(me, const_like ? c_sv("const") : csview_from(variable_statement_keyword_str(variable)));
        w_lit(me, " ");
        w(me, cstr_sv(&declarations));
        w_lit(me, ";");
        m(me, statement_end(statement));
        w_lit(me, "\n");
    }
    cstr_drop(&declarations);
}

// Whether the file's text has `word` as an identifier (approximately tsc's sourceFile.identifiers: a whole word)
static bool file_has_word(SourceFile const* file, csview word) {
    csview text = cstr_sv(&file->contents);
    for (isize_t at = 0; at + word.size <= text.size;) {
        isize_t found = csview_find_sv(csview_slice(text, at, text.size), word);
        if (found == c_NPOS) return false;
        found += at;
        char before = found ? text.buf[found - 1] : ' ';
        char after = found + word.size < text.size ? text.buf[found + word.size] : ' ';
        bool word_char_before = isalnum((unsigned char)before) || before == '_' || before == '$';
        bool word_char_after = isalnum((unsigned char)after) || after == '_' || after == '$';
        if (!word_char_before && !word_char_after) return true;
        at = found + 1;
    }
    return false;
}

// The printer's next generated name (tsc's makeTempVariableName): `_a` to `_z` but `_i` and `_n` (kept for loops), then
// `_0`, `_1`..., none the file has as an identifier (isUniqueName)
static void generated_name(Walk* me, char* out, size_t size) {
    while (true) {
        u32 n;
        do {
            n = my generated_names++;
        } while (n == 8 || n == 13);
        if (n < 26) snprintf(out, size, "_%c", 'a' + n);
        else snprintf(out, size, "_%u", n - 26);
        if (!file_has_word(my file, csview_from(out))) return;
    }
}

// A reserved word (tsc's isNonContextualKeyword: what can't name a variable)
static bool is_reserved_word(csview name) {
    static char const* const words[] = {
        "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else", "enum",
        "export", "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof", "new", "null",
        "return", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while", "with",
        "implements", "interface", "let", "package", "private", "protected", "public", "static", "yield",
    };
    for (u32 i = 0; i < c_arraylen(words); i++) {
        if (csview_equals(name, words[i])) return true;
    }
    return false;
}

// An expando member declaration emit writes (tsc's transformExpandoAssignment): one whose name is an identifier (a late
// bound one's, `f[k] = v`, what k's type names), `name`
static bool expando_member_written(Walk* me, Symbol const* member, csview* name) {
    if (!member || !member->decl_count || member->decls[0].kind != SYMBOL_DECL_ASSIGNMENT) return false;
    // (A JavaScript class's static `this.x = v` is a member of the class)
    AssignmentDeclKind kind = member->decls[0].assignment;
    if (kind == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR || kind == ASSIGNMENT_DECL_THIS_IN_METHOD) return false;
    *name = member->name;
    Expr const* key = symbol_rare(member)->late_bound_key;
    if (key) {
        bool is_number = false;
        Atom const* atom = ty_property_name_of_key(my checker, ty_expr_type(my checker, key), &is_number);
        if (!atom || is_number) return false;
        *name = atom_sv(atom);
    }
    return name->size && name->buf[0] != '#' && ty_is_identifier_name(*name);
}

// The expando members of a function (`f.x = v`, tsc's transformExpandoAssignment), as a namespace of the function's
// name after it (createFullExpandoBlock), one statement per assignment in order: a variable of the assignment's type,
// named as the property unless that name resolves here or is a keyword (a generated `_a` exported as the property then),
// or `export { v as x }` when the value is a name. Once there is an export declaration, the variables declared before
// the first renamed one and every later one are exported. `modifiers`: the function's, as written
typedef struct ExpandoStatement {
    cstr text; // Past the modifiers, with its line break
    bool exported;
    bool declaration; // An export declaration
} ExpandoStatement;

static void emit_expando_namespace(Walk* me, Symbol const* host, Span name, bool mapped, csview modifiers) {
    if (!host || !host->exports) return;
    ExpandoStatement* statements = nullptr;
    u32 count = 0, cap = 0;
    bool has_export = false;
#define EXPANDO_PUSH(t, e, d) do { \
        if (count == cap) { cap = cap ? cap * 2 : 8; statements = realloc(statements, cap * sizeof(ExpandoStatement)); } \
        statements[count++] = (ExpandoStatement){ .text = (t), .exported = (e), .declaration = (d) }; \
    } while (0)
    cstr* outer = my out;
    // (In source order: the binder's is evaluation order, a for loop's body before its incrementor)
    typedef struct { Symbol const* member; u32 decl; char const* at; } Assignment;
    Assignment* assignments = nullptr;
    u32 assignment_count = 0;
    for (u32 i = 0; i < host->exports->count; i++) {
        Symbol const* member = host->exports->entries[i].symbol;
        csview member_name;
        if (!expando_member_written(me, member, &member_name)) continue;
        for (u32 j = 0; j < member->decl_count; j++) {
            if (member->decls[j].kind != SYMBOL_DECL_ASSIGNMENT) continue;
            assignments = realloc(assignments, (assignment_count + 1) * sizeof(Assignment));
            assignments[assignment_count++] = (Assignment){ member, j, member->decls[j].full.buf };
        }
    }
    for (u32 i = 1; i < assignment_count; i++) {
        for (u32 k = i; k > 0 && assignments[k - 1].at > assignments[k].at; k--) {
            Assignment t = assignments[k];
            assignments[k] = assignments[k - 1];
            assignments[k - 1] = t;
        }
    }
    for (u32 a = 0; a < assignment_count; a++) {
        Symbol const* member = assignments[a].member;
        csview member_name;
        expando_member_written(me, member, &member_name);
        {
            u32 j = assignments[a].decl;
            Expr const* value = member->decls[j].node;
            if (value && value->tag == EXPR_IDENTIFIER) {
                if (!has_export) {
                    for (u32 k = 0; k < count; k++) statements[k].exported = true;
                }
                has_export = true;
                cstr text = cstr_from("export { ");
                // (The specifier's name is the assigned identifier, mapped when the specifier renames it: tsgo's
                // `export { joinSql as join }` maps joinSql, `export { attribute }` nothing)
                my out = &text;
                if (csview_equals_sv(span_sv(expr_outer_span(value)), member_name)) w(me, span_sv(expr_outer_span(value)));
                else w_name(me, expr_outer_span(value));
                my out = outer;
                if (!csview_equals_sv(span_sv(expr_outer_span(value)), member_name)) {
                    cstr_append_sv(&text, c_sv(" as "));
                    cstr_append_sv(&text, member_name);
                }
                cstr_append_sv(&text, c_sv(" };\n"));
                EXPANDO_PUSH(text, false, true);
                continue;
            }
            // (A name that resolves here or a keyword can't be the variable's: a generated one, exported as the property)
            Symbol const* resolved = my scope ? scope_resolve(my scope, member_name, SYMBOL_VALUE | SYMBOL_TYPE | SYMBOL_NAMESPACE) : nullptr;
            // (Not a keyword type: `any` has no symbol in tsc)
            if (resolved && resolved->decl_count && resolved->decls[0].kind == SYMBOL_DECL_BUILTIN) resolved = nullptr;
            // (`undefined` is a global symbol in tsc)
            bool renamed = is_reserved_word(member_name) || resolved || csview_equals(member_name, "undefined");
            char generated[16] = "";
            if (renamed) {
                generated_name(me, generated, sizeof generated);
            }
            cstr text = cstr_init();
            my out = &text;
            my indent++;
            w_lit(me, "var ");
            w(me, renamed ? csview_from(generated) : member_name);
            w_lit(me, ": ");
            symbol_chain_set_expando_host(host);
            w_type(me, ty_widened_literal(my checker, ty_type_of_symbol(my checker, member)));
            symbol_chain_set_expando_host(nullptr);
            w_lit(me, ";\n");
            my indent--;
            my out = outer;
            EXPANDO_PUSH(text, has_export, false);
            if (renamed) {
                cstr declaration = cstr_from("export { ");
                cstr_append_sv(&declaration, csview_from(generated));
                cstr_append_sv(&declaration, c_sv(" as "));
                cstr_append_sv(&declaration, member_name);
                cstr_append_sv(&declaration, c_sv(" };\n"));
                // (Before it is added: the variable stays private)
                if (!has_export) {
                    for (u32 k = 0; k + 1 < count; k++) statements[k].exported = true;
                }
                has_export = true;
                EXPANDO_PUSH(declaration, false, true);
            }
        }
    }
#undef EXPANDO_PUSH
    free(assignments);
    if (count) {
        w_indent(me);
        // (In a namespace body whose exports are stripped, this one's is too: see Walk.export_offsets)
        if (csview_starts_with(modifiers, "export ") && my export_offsets && my indent) {
            if (my export_offset_count == my export_offset_cap) {
                my export_offset_cap = my export_offset_cap ? my export_offset_cap * 2 : 8;
                my export_offsets = realloc(my export_offsets, my export_offset_cap * sizeof(u32));
            }
            my export_offsets[my export_offset_count++] = (u32)cstr_size(my out);
        }
        w(me, modifiers);
        w_lit(me, "namespace ");
        // (A function declaration's name node; a variable's function is named anew)
        if (mapped) w_name(me, name);
        else w(me, span_sv(name));
        w_lit(me, " {\n");
        my indent++;
        for (u32 i = 0; i < count; i++) {
            w_indent(me);
            if (statements[i].exported) w_lit(me, "export ");
            w(me, cstr_sv(&statements[i].text));
        }
        my indent--;
        w_indent(me);
        w_lit(me, "}\n");
    }
    for (u32 i = 0; i < count; i++) cstr_drop(&statements[i].text);
    free(statements);
}

// The JSDoc `@overload`s of a JavaScript function or method that are signatures
static u32 js_overload_count(FunctionImpl const* fn) {
    if (!fn->rare) return 0;
    FunctionImplRare const* rare = function_impl_rare(fn);
    for (u32 i = 0; i < rare->jsdoc_overload_count; i++) {
        if (rare->jsdoc_overloads[i].tag != TYPE_CALLABLE) return 0;
    }
    return rare->jsdoc_overload_count;
}

static void emit_function_declaration(Walk* me, Statement const* statement, Container container) {
    FunctionImpl const* fn = statement->decl.function.impl;
    if (is_overload_implementation(fn)) return;
    Symbol const* symbol = fn->symbol;
    // (After the first declaration of the function: its symbol's value declaration)
    bool first = symbol && symbol->decl_count && symbol->decls[0].node == fn;
    // A default export with expandos is a declaration, the export, then the namespace (tsc's transformExpandoHost)
    if (first && statement->decl.props.exported == DECL_EXPORTED_YES_DEFAULT && container_is_file(container) &&
        span_valid(fn->name) && expando_any_written(me, symbol)
    ) {
        my has_module_indicator = my has_scope_marker = true;
        w_indent(me);
        m_span(me, statement->span, false);
        w_lit(me, "declare function ");
        w_name(me, fn->name);
        w_signature(me, fn, false);
        w_lit(me, ";");
        m(me, statement_end(statement));
        w_lit(me, "\n");
        w_indent(me);
        w_lit(me, "export default ");
        w(me, span_sv(fn->name));
        w_lit(me, ";\n");
        emit_expando_namespace(me, symbol, fn->name, true, c_sv("declare "));
        return;
    }
    // A JavaScript function's `@overload`s are its declarations, without comments (tsgo's reparsed signatures before it,
    // the implementation elided)
    u32 overloads = js_overload_count(fn);
    if (overloads) {
        // (A default export's keep the statement's comments)
        bool default_export = statement->decl.props.exported == DECL_EXPORTED_YES_DEFAULT;
        my statement_synthesized = !default_export;
        if (!default_export && my statement_start <= (isize_t)cstr_size(my out)) cstr_erase(my out, my statement_start, cstr_size(my out) - my statement_start);
        for (u32 i = 0; i < overloads; i++) {
            FunctionImpl const* overload = function_impl_rare(fn)->jsdoc_overloads[i].callable->function;
            w_indent(me);
            w_statement_modifiers(me, statement, container, false);
            w_lit(me, "function ");
            w(me, span_sv(fn->name));
            u32 saved = enter_type_params(me, &overload->generic);
            w_signature(me, overload, false);
            leave_type_params(me, saved);
            w_lit(me, ";\n");
        }
        return;
    }
    w_indent(me);
    m_span(me, statement->span, false);
    isize_t modifiers_start = (isize_t)cstr_size(my out);
    w_statement_modifiers(me, statement, container, false);
    cstr modifiers = cstr_from_sv(csview_slice(cstr_sv(my out), (isize_t)modifiers_start, cstr_size(my out)));
    decl_map_strip(&modifiers);
    w_lit(me, "function ");
    w_name(me, fn->name);
    u32 scope_saved = enter_type_params(me, &fn->generic);
    w_signature(me, fn, false);
    leave_type_params(me, scope_saved);
    w_lit(me, ";");
    m(me, statement_end(statement));
    w_lit(me, "\n");
    if (first && statement->decl.props.exported != DECL_EXPORTED_YES_DEFAULT) {
        emit_expando_namespace(me, symbol, fn->name, true, cstr_sv(&modifiers));
    }
    cstr_drop(&modifiers);
}

// A class member's modifiers in tsc's order (createModifiersFromModifierFlags; `public`, `async` and `override` aren't
// written, nor `declare`)
static void w_member_modifiers(Walk* me, char const* from, AccessModifier access, bool is_abstract, bool is_static, bool readonly, bool accessor) {
    Modifiers mods = {0};
    if (access == ACCESS_MODIFIER_PRIVATE) mods_add(&mods, "private");
    if (access == ACCESS_MODIFIER_PROTECTED) mods_add(&mods, "protected");
    if (is_abstract) mods_add(&mods, "abstract");
    if (is_static) mods_add(&mods, "static");
    if (readonly) mods_add(&mods, "readonly");
    if (accessor) mods_add(&mods, "accessor");
    w_modifiers(me, from, mods);
}

static csview item_name(ClassItem const* item) {
    return span_sv(item->tag == CLASS_ITEM_METHOD ? item->method.name_span : item->field.name_span);
}

static void emit_field(Walk* me, ClassItem const* item, char const* member_end) {
    w_indent(me);
    m_span(me, item->span, false);
    w_member_modifiers(me, item->span.buf, item->access, item->field.is_abstract, item->field.is_static, item->field.is_readonly,
        item->field.is_auto_accessor);
    w_member_name(me, item->field.name_span, item->field.computed_key);
    if (item->field.is_optional) {
        char const* question = m_token_after(me, span_end_ptr(item->field.name_span), '?');
        m(me, question);
        w_lit(me, "?");
        if (question) m(me, question + 1);
    }
    if (item->access == ACCESS_MODIFIER_PRIVATE) {
        w_lit(me, ";");
        m(me, member_end);
        w_lit(me, "\n");
        return;
    }
    Expr const* init = &item->field.value;
    if (item->field.is_readonly && init->tag != EXPR_IMPLICIT_ANY && is_literal_const_field(item) && !item->field.has_explicit_type) {
        w_lit(me, " = ");
        w_literal_value(me, lazy_type_get((LazyType*)&item->field.lazy_type));
        w_lit(me, ";");
        m(me, member_end);
        w_lit(me, "\n");
        return;
    }
    w_lit(me, ": ");
    Ty const* field_type = lazy_type_get((LazyType*)&item->field.lazy_type);
    // (A static readonly member's unique symbol is the class's, known by the member's name)
    UniqueSymbolDecl const* unique = field_type && (field_type->flags & TYF_UNIQUE_ES_SYMBOL) ? field_type->literal.unique : nullptr;
    Symbol const* field_symbol = unique ? unique->owner : nullptr;
    bool own_unique = unique && (span_valid(unique->member) ? span_eq(unique->member, item->field.name_span) :
        field_symbol && span_eq(field_symbol->decl_count ? field_symbol->decls[0].name : span_null, item->field.name_span));
    if (item->field.has_explicit_type) {
        w_node(me, &item->field.explicit_type);
    } else if (own_unique) {
        w_lit(me, "unique symbol");
    } else {
        cstr text = cstr_init();
        cstr* outer = my type_out;
        my type_out = &text;
        check_field(me, item);
        my type_out = outer;
        w_serialized(me, &text);
    }
    w_lit(me, ";");
    m(me, member_end);
    w_lit(me, "\n");
}

static void emit_parameter_property(Walk* me, FunctionImpl const* ctor, FunctionArg const* arg) {
    Binding const* binding = &arg->initializer.binding;
    if (binding->tag != BINDING_IDENTIFIER) {
        // (An error, but each name is written as a property of its type: walkBindingPattern)
        Binding const* stack[64];
        u32 depth = 0;
        stack[depth++] = binding;
        while (depth) {
            Binding const* b = stack[--depth];
            if (b->tag == BINDING_IDENTIFIER) {
                w_indent(me);
                w(me, span_sv(b->content_span));
                w_lit(me, ": ");
                w_type(me, b->symbol ? ty_type_of_symbol(my checker, b->symbol) : ty_intrinsic(my checker, TY_ANY));
                w_lit(me, ";\n");
            } else if (b->tag == BINDING_ARRAY) {
                for (isize_t k = vec_binding_size(&b->array); k-- > 0 && depth < 64;) stack[depth++] = vec_binding_at(&b->array, k);
            } else if (b->tag == BINDING_OBJECT) {
                for (isize_t k = vec_object_binding_item_size(&b->object); k-- > 0 && depth < 64;) {
                    stack[depth++] = &vec_object_binding_item_at(&b->object, k)->target;
                }
            }
        }
        return;
    }
    w_indent(me);
    w_member_modifiers(me, arg->span.buf, arg->access, false, false, arg->is_readonly_property, false);
    w_name(me, binding->content_span);
    if (arg->explicit_optional) {
        Span question = function_arg_question_span(arg);
        m_span(me, question, false);
        w_lit(me, "?");
        m_span(me, question, true);
    }
    if (arg->access == ACCESS_MODIFIER_PRIVATE) {
        w_lit(me, ";\n");
        return;
    }
    w_lit(me, ": ");
    if (binding->has_explicit_type && !requires_adding_undefined(me, ctor, arg, false)) {
        w_node_last(me, binding_type(binding));
    } else {
        cstr text = cstr_init();
        cstr* outer = my type_out;
        my type_out = &text;
        my printed_next = &my class_printed_types;
        Emitting saved = emit_enter(me, EMIT_PROPERTY, SIG_CONSTRUCTOR, binding->content_span, binding->content_span);
        serialize_parameter(me, ctor, arg, false);
        emit_leave(me, saved);
        my type_out = outer;
        w_serialized(me, &text);
    }
    w_lit(me, ";\n");
}


// The members of the index signatures computed names make, written as properties (createLateBoundIndexSignatures):
// static ones first, string, number, then symbol names; each `[name]?: T`, readonly when all of the signature's are
static void emit_late_indexes(Walk* me, ClassDecl const* cls) {
    u32 count = 0;
    LateIndexMember* members = late_index_members(cls, &count);
    for (u32 side = 0; side < 2; side++) {
        for (uint8_t kind = 1; kind <= 3; kind++) {
            bool any = false, readonly = true;
            for (u32 i = 0; i < count; i++) {
                if (members[i].is_static != (side == 0) || members[i].kind != kind) continue;
                any = true;
                readonly &= members[i].readonly;
            }
            if (!any || !late_index_names_visible(me, members, count, side == 0, kind)) continue;
            for (u32 i = 0; i < count; i++) {
                LateIndexMember const* m = &members[i];
                if (m->is_static != (side == 0) || m->kind != kind) continue;
                w_indent(me);
                if (side == 0) w_lit(me, "static ");
                if (readonly) w_lit(me, "readonly ");
                w_lit(me, "[");
                w(me, span_sv(expr_outer_span(m->name)));
                w_lit(me, "]");
                if (m->optional) w_lit(me, "?");
                w_lit(me, ": ");
                if (m->item && m->item->tag == CLASS_ITEM_METHOD && m->item->method.accessor != CLASS_ACCESSOR_NONE) {
                    w_type(me, ty_object_literal_accessor_type(my checker, &m->item->method.func,
                        m->item->method.accessor == CLASS_ACCESSOR_GET));
                } else if (m->item && m->item->tag == CLASS_ITEM_METHOD) {
                    Ty const* type = ty_function_type(my checker, &m->item->method.func);
                    if (m->optional && my strict_null_checks) {
                        type = ty_union(my checker, (Ty const*[]){ type, ty_intrinsic(my checker, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
                    }
                    w_type(me, type);
                } else if (m->item) {
                    Ty const* type = lazy_type_get((LazyType*)&m->item->field.lazy_type);
                    if (type && m->optional && my strict_null_checks) {
                        type = ty_union(my checker, (Ty const*[]){ type, ty_intrinsic(my checker, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
                    }
                    w_type(me, type ? type : ty_intrinsic(my checker, TY_ANY));
                } else if (m->member->type.tag == TYPE_CALLABLE && m->member->is_method) {
                    SINKS_OFF;
                    TyParamNames names = walk_param_names(me);
                    TyDisplay d = emit_display(me, my out);
                    d.param_names = &names;
                    ty_display_node_signature(my checker, d, m->member->type.callable->function, " => ");
                    ty_param_names_drop(&names);
                    SINKS_ON;
                } else {
                    w_node(me, &m->member->type);
                }
                w_lit(me, ";\n");
            }
        }
    }
    free(members);
}

static void w_heritage(Walk* me, VecSpannedType const* types, char const* keyword);
static void w_heritage_comments(Walk* me, char const* keyword_at);

// `statement` null: a class expression written as a declaration named `name` (an export default's)
// `this["x"] = v`'s name, the string literal as written (empty for any other)
static csview this_property_literal_name(Walk* me, SymbolDecl const* decl) {
    Span full = decl->full;
    if (!span_valid(full) || span_source(full) != my file || !csview_starts_with(span_sv(full), "this")) return (csview){0};
    char const* end = cstr_str(&my file->contents) + cstr_size(&my file->contents);
    char const* c = decl_map_skip_trivia(full.buf + 4, end);
    if (c >= end || *c != '[') return (csview){0};
    c = decl_map_skip_trivia(c + 1, end);
    if (c >= end || (*c != '"' && *c != '\'')) return (csview){0};
    char const* close = c + 1;
    while (close < end && *close != *c && *close != '\n') close += *close == '\\' ? 2 : 1;
    return close < end && *close == *c ? csview_from_n(c, close + 1 - c) : (csview){0};
}

static void emit_js_this_properties(Walk* me, ClassDecl const* cls) {
    ThisProperty* props;
    u32 count = js_this_properties(me, cls, &props);
    for (u32 i = 0; i < count; i++) {
        ThisProperty const* p = &props[i];
        csview name = p->member->name;
        // (With the comments of its expression statement: preserveJsDoc)
        w_leading(me, p->decl->full.buf);
        w_indent(me);
        if (p->is_static) w_lit(me, "static ");
        // (`this["a-b"] = v`: a string literal name, as written)
        csview literal = this_property_literal_name(me, p->decl);
        bool identifier = name.size && !isdigit((unsigned char)name.buf[0]);
        for (isize_t c = 0; c < name.size; c++) identifier &= is_word_char(name.buf[c]);
        if (literal.size) {
            w(me, literal);
        } else if (identifier) {
            w(me, name);
        } else {
            w_lit(me, "\"");
            w(me, name);
            w_lit(me, "\"");
        }
        // (`/** @private */ this.x = v` has no type, nor a modifier: ensureType's GetEffectiveDeclarationFlags sees the
        // tag, the modifiers aren't the JSDoc's)
        if (!(p->decl->jsdoc_modifiers & JSDOC_MODIFIER_PRIVATE)) {
            w_lit(me, ": ");
            TypeNode const* annotation = symbol_decl_jsdoc_type(p->decl);
            if (annotation) w_node(me, annotation);
            else w_type(me, ty_type_of_symbol(my checker, p->member));
        }
        w_lit(me, ";\n");
    }
    free(props);
}

// (Without a statement: `modifiers` are written, `declare ` where empty in a file)
static void emit_class(Walk* me, ClassDecl const* cls, Statement const* statement, Container container, csview name, csview modifiers) {
    ClassDecl const* outer_printed_class = my printed_class;
    my printed_class = cls;
    // A base that isn't a name is a variable of its type, `C_base` (transformClassDeclaration)
    Expr const* base = &cls->extends;
    Expr const* base_name = base->tag == EXPR_GENERIC_PARAMS ? &base->generic_params->expr : base;
    bool base_variable = base->tag != EXPR_IMPLICIT_ANY && !pseudo_is_entity_name(base_name) &&
        !(base_name->tag == EXPR_LITERAL && base_name->literal.tag == VALUE_NULL);
    cstr base_variable_name = cstr_init();
    if (base_variable) {
        cstr_take(&base_variable_name, cstr_d(span_valid(cls->name) ? span_sv(cls->name) : c_sv("default"), "_base"));
        // (Before the class's leading comments, which are the class's: the variable is a new statement)
        isize_t at = statement ? my statement_start : (isize_t)cstr_size(my out);
        cstr* outer = my out;
        cstr line = cstr_init();
        my out = &line;
        w_indent(me);
        if (container_is_file(container)) w_lit(me, "declare ");
        my needs_scope_fix = true;
        w_lit(me, "const ");
        w(me, cstr_sv(&base_variable_name));
        w_lit(me, ": ");
        w_type(me, lazy_type_get((LazyType*)&base_name->lazy_type));
        w_lit(me, ";\n");
        my out = outer;
        if (at > (isize_t)cstr_size(my out)) at = cstr_size(my out);
        cstr_insert_sv(my out, at, cstr_sv(&line));
        for (u32 i = 0; i < my export_offset_count; i++) {
            if ((isize_t)my export_offsets[i] >= at) my export_offsets[i] += (u32)cstr_size(&line);
        }
        cstr_drop(&line);
    }
    w_indent(me);
    cstr class_modifiers = cstr_init();
    if (statement) {
        m_span(me, statement->span, false);
        isize_t modifiers_start = (isize_t)cstr_size(my out);
        w_statement_modifiers_and(me, statement, container, false, cls->is_abstract ? "abstract" : nullptr);
        cstr_take(&class_modifiers, cstr_from_sv(csview_slice(cstr_sv(my out), modifiers_start, cstr_size(my out))));
        decl_map_strip(&class_modifiers);
        // (The namespace of its expandos has the others)
        isize_t abstract_at = cstr_find(&class_modifiers, "abstract ");
        if (abstract_at != c_NPOS) cstr_erase(&class_modifiers, abstract_at, 9);
    } else {
        if (modifiers.size) w(me, modifiers);
        else if (container_is_file(container)) w_lit(me, "declare ");
        if (cls->is_abstract) w_lit(me, "abstract ");
    }
    w_lit(me, "class");
    if (name.size) {
        w_lit(me, " ");
        w(me, name);
    } else if (span_valid(cls->name)) {
        w_lit(me, " ");
        w_name(me, cls->name);
    }
    w_type_params(me, &cls->generic);
    if (base->tag != EXPR_IMPLICIT_ANY) {
        // (The clause from its keyword, the expression with its type arguments)
        m_span(me, cls->extends_clause, false);
        w_heritage_comments(me, span_valid(cls->extends_clause) && span_source(cls->extends_clause) == my file ?
            span_start_ptr(cls->extends_clause) : nullptr);
        w_lit(me, " extends ");
        Span base_span = expr_outer_span(base_name);
        m_span(me, base_span, false); // (The expression replaced by `C_base` keeps the range)
        if (base_variable) w(me, cstr_sv(&base_variable_name));
        else if (base_name->tag == EXPR_IDENTIFIER || (base_name->tag == EXPR_PROPERTY && base_name->property->form == PROPERTY_EXPR_DOT)) w_entity_expr(me, base_name);
        else w_name(me, base_span);
        if (base->tag == EXPR_GENERIC_PARAMS) {
            w_lit(me, "<");
            for_each (i, vec_spanned_type, &base->generic_params->params) {
                if (i_index) w_lit(me, ", ");
                w_node_visited(me, &i->type);
            }
            w_lit(me, ">");
        }
        if (!base_variable) m_span(me, cls->extends_clause, true);
        m_span(me, cls->extends_clause, true);
    }
    w_heritage(me, &cls->implements, "implements");
    w_lit(me, " {\n");
    Scope const* outer_scope = my scope;
    u32 class_scope_saved = enter_type_params(me, &cls->generic);
    my indent++;
    bool private_names = false;
    for_each (i, vec_class_item, &cls->items) {
        if ((i->tag == CLASS_ITEM_METHOD || i->tag == CLASS_ITEM_FIELD) && item_has_private_name(i)) private_names = true;
    }
    if (private_names) {
        w_indent(me);
        w_lit(me, "#private;\n");
    }
    emit_late_indexes(me, cls);
    // The parameter properties, as properties
    for_each (i, vec_class_item, &cls->items) {
        if (i->tag != CLASS_ITEM_CONSTRUCTOR || !i->constructor.func.has_body) continue;
        for_each (j, vec_function_arg, &i->constructor.func.args) {
            // (`@internal` in the comment before it: tsc's isInternalDeclaration of a parameter)
            if (compiler_options()->strip_internal && decl_comments_last_contains(&my comments, j->span.buf, "@internal")) continue;
            if (!is_parameter_property(j)) continue;
            // (With the parameter's comments: preserveJsDoc)
            isize_t start = w_leading(me, j->span.buf);
            isize_t written_from = cstr_size(my out);
            emit_parameter_property(me, &i->constructor.func, j);
            w_unless_written(me, start, written_from, span_end_ptr(j->span));
        }
    }
    if (my file->is_js) emit_js_this_properties(me, cls);
    bool outer_static = my member_static, outer_in_class = my member_in_class;
    my member_in_class = true;
    for_each (i, vec_class_item, &cls->items) {
        if (is_internal_declaration(i->span)) continue;
        my member_static = class_item_is_static((ClassItem*)i);
        // (A computed name that isn't late bound, of a type that isn't a literal or unique symbol, isn't written:
        // visitDeclarationSubtree's hasDynamicName check)
        Expr const* key = i->tag == CLASS_ITEM_METHOD ? i->method.computed_key : i->tag == CLASS_ITEM_FIELD ? i->field.computed_key : nullptr;
        if (key && is_dynamic_name(key)) {
            Ty const* key_type = lazy_type_get((LazyType*)&key->lazy_type);
            if (!pseudo_is_entity_name(key) || !key_type || !(key_type->flags & (TYF_LITERAL | TYF_UNIQUE_ES_SYMBOL))) continue;
        }
        isize_t member_start = w_leading(me, i->span.buf);
        isize_t member_written_from = cstr_size(my out);
        // (Where the member ends, its `;` included: mapped after it)
        char const* member_end = decl_comments_end(&my comments, span_end_ptr(i->span), ";");
        switch (i->tag) {
            case CLASS_ITEM_CONSTRUCTOR: {
                FunctionImpl const* ctor = &i->constructor.func;
                if (is_class_overload_implementation(cls, i, ctor)) break;
                if (js_overload_count(ctor)) {
                    // (A JavaScript constructor's `@overload`s, without its comments: tsgo's reparsed constructors)
                    cstr_erase(my out, member_start, cstr_size(my out) - member_start);
                    u32 overloads = js_overload_count(ctor);
                    for (u32 o = 0; o < overloads; o++) {
                        FunctionImpl const* overload = function_impl_rare(ctor)->jsdoc_overloads[o].callable->function;
                        w_indent(me);
                        w_member_modifiers(me, i->span.buf, i->access, false, false, false, false);
                        w_lit(me, "constructor");
                        w_parameters(me, overload, i->access == ACCESS_MODIFIER_PRIVATE);
                        w_lit(me, ";\n");
                    }
                    continue;
                }
                w_indent(me);
                m_span(me, i->span, false);
                w_member_modifiers(me, i->span.buf, i->access, false, false, false, false);
                if (i->jsdoc_readonly) w_lit(me, "readonly ");
                w_lit(me, "constructor");
                w_parameters(me, ctor, i->access == ACCESS_MODIFIER_PRIVATE);
                w_lit(me, ";");
                m(me, member_end);
                w_lit(me, "\n");
                break;
            }
            case CLASS_ITEM_METHOD: {
                if (item_has_private_name(i)) break;
                FunctionImpl const* fn = &i->method.func;
                bool private_member = i->access == ACCESS_MODIFIER_PRIVATE;
                if (i->method.accessor == CLASS_ACCESSOR_NONE && js_overload_count(fn)) {
                    // (A JavaScript method's `@overload`s, each with the method's comments)
                    u32 overloads = js_overload_count(fn);
                    for (u32 o = 0; o < overloads; o++) {
                        FunctionImpl const* overload = function_impl_rare(fn)->jsdoc_overloads[o].callable->function;
                        if (o) w_leading(me, i->span.buf);
                        w_indent(me);
                        w_member_modifiers(me, i->span.buf, i->access, i->method.is_abstract, i->method.is_static, false, false);
                        w_member_name(me, i->method.name_span, i->method.computed_key);
                        u32 saved = enter_type_params(me, &overload->generic);
                        w_signature(me, overload, false);
                        leave_type_params(me, saved);
                        w_lit(me, ";\n");
                    }
                    break;
                }
                if (i->method.accessor == CLASS_ACCESSOR_NONE) {
                    if (is_class_overload_implementation(cls, i, fn) || interface_declares_method(cls, i)) break;
                    // (A private method is a property without a type: omitPrivateMethodType, once per name)
                    if (private_member && i->symbol_first && i->symbol_first != i) break;
                    w_indent(me);
                    // (A private method is a new property: its parts are mapped, it isn't, omitPrivateMethodType)
                    if (!private_member) m_span(me, i->span, false);
                    w_member_modifiers(me, i->span.buf, i->access, i->method.is_abstract, i->method.is_static, false, false);
                    w_member_name(me, i->method.name_span, i->method.computed_key);
                    if (i->method.is_optional) {
                        char const* question = m_token_after(me, span_end_ptr(i->method.name_span), '?');
                        m(me, question);
                        w_lit(me, "?");
                        if (question) m(me, question + 1);
                    }
                    u32 method_scope_saved = enter_type_params(me, &fn->generic);
                    if (!private_member) w_signature(me, fn, false);
                    leave_type_params(me, method_scope_saved);
                    w_lit(me, ";");
                    if (!private_member) m(me, member_end);
                    w_lit(me, "\n");
                    break;
                }
                bool getter = i->method.accessor == CLASS_ACCESSOR_GET;
                w_indent(me);
                m_span(me, i->span, false);
                w_member_modifiers(me, i->span.buf, i->access, i->method.is_abstract, i->method.is_static, false, false);
                w(me, getter ? c_sv("get ") : c_sv("set "));
                w_member_name(me, i->method.name_span, i->method.computed_key);
                if (getter) {
                    w_lit(me, "()");
                    if (!private_member) {
                        Accessors pair = class_accessors(i);
                        find_setter(cls, i, &pair);
                        Accessors outer_pair = my class_pair;
                        my class_pair = pair;
                        w_return(me, fn, true);
                        my class_pair = outer_pair;
                    }
                } else {
                    w_lit(me, "(");
                    if (private_member) {
                        w_lit(me, "value"); // (updateAccessorParamList)
                    } else {
                        bool written = false;
                        for_each (j, vec_function_arg, &fn->args) {
                            Binding const* binding = &j->initializer.binding;
                            if (binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this")) continue;
                            w_parameter_with_comments(me, fn, j);
                            written = true;
                            break;
                        }
                        if (!written) w_lit(me, "value: any");
                    }
                    w_lit(me, ")");
                }
                w_lit(me, ";");
                m(me, member_end);
                w_lit(me, "\n");
                break;
            }
            case CLASS_ITEM_FIELD:
                if (item_has_private_name(i)) break;
                emit_field(me, i, member_end);
                break;
            case CLASS_ITEM_INTERFACE_MEMBER: {
                // An index signature, as written
                InterfaceMember const* member = &i->interface_memeber;
                if (member->key.tag != INTERFACE_KEY_INDEX) break;
                w_indent(me);
                m_span(me, i->span, false);
                w_member_modifiers(me, i->span.buf, ACCESS_MODIFIER_PUBLIC, false, i->interface_member_static, member->readonly, false);
                w_lit(me, "[");
                m_span(me, member->key.index->name, false);
                w_name(me, member->key.index->name);
                w_lit(me, ": ");
                w_node_visited(me, &member->key.index->type);
                m(me, decl_map_type_end(&member->key.index->type, my file));
                w_lit(me, "]: ");
                w_node_visited(me, &member->type);
                w_lit(me, ";");
                m(me, member_end);
                w_lit(me, "\n");
                break;
            }
            default: break;
        }
        w_unless_written(me, member_start, member_written_from, decl_comments_end(&my comments, span_end_ptr(i->span), ";"));
    }
    my member_static = outer_static, my member_in_class = outer_in_class;
    my indent--;
    my scope = outer_scope;
    leave_type_params(me, class_scope_saved);
    w_indent(me);
    w_lit(me, "}");
    if (statement) m(me, statement_end(statement));
    w_lit(me, "\n");
    // (A JavaScript class's expandos, `C.x = v`: a namespace after it, tsgo's transformExpandoHost)
    if (statement && my file->is_js && span_valid(cls->name) && expando_any_written(me, cls->symbol)) {
        emit_expando_namespace(me, cls->symbol, cls->name, true, cstr_sv(&class_modifiers));
    }
    cstr_drop(&class_modifiers);
    cstr_drop(&base_variable_name);
    my printed_class = outer_printed_class;
}

// A type literal's members, one per line, braces and all (an interface's body: `{` and `}` on lines of their own even
// when empty)
static void w_members(Walk* me, TypeNode const* type) {
    bool empty = type->tag != TYPE_INTERFACE || !vec_interface_member_size(&type->interface.members);
    if (!empty) {
        isize_t start = cstr_size(my out);
        // (An interface's body isn't a node: not mapped itself)
        TypeNode body = *type;
        body.pos = 0;
        w_node_visited(me, &body);
        // (Every member left out)
        if (cstr_size(my out) - start != 2 || cstr_str(my out)[start] != '{') return;
        cstr_erase(my out, start, 2);
    }
    w_lit(me, "{\n");
    w_indent(me);
    w_lit(me, "}");
}

// A heritage clause's types after its keyword (`extends A, B`): the clause is mapped from the keyword (its leading space
// in it) to its last type's end
// Where `word` ends right before `at` (whitespace apart) in the file, null if it doesn't
static char const* keyword_before(SourceFile const* file, char const* at, char const* word) {
    char const* text = cstr_str(&file->contents);
    if (!at || at < text || at > text + cstr_size(&file->contents)) return nullptr;
    char const* c = at;
    while (c > text && (c[-1] == ' ' || c[-1] == '\t' || c[-1] == '\n' || c[-1] == '\r')) c--;
    size_t n = strlen(word);
    if (c - text < (isize_t)n || memcmp(c - n, word, n)) return nullptr;
    return c - n;
}

// A heritage clause's leading comments on the line of the token before it, each and a space (tsc emits the clause
// node's leading comments: `class E/* <T> */  implements S`); none without a clause
static void w_heritage_comments(Walk* me, char const* keyword_at) {
    if (keyword_at) decl_comments_intervening(&my comments, my out, my indent, keyword_at);
}

static void w_heritage(Walk* me, VecSpannedType const* types, char const* keyword) {
    if (!vec_spanned_type_size(types)) return;
    Span first = vec_spanned_type_at(types, 0)->span;
    m(me, span_valid(first) ? m_keyword_before(me, first.buf, keyword) : nullptr);
    w_heritage_comments(me, span_valid(first) && span_source(first) == my file ? keyword_before(my file, first.buf, keyword) : nullptr);
    w_lit(me, " ");
    w(me, csview_from(keyword));
    w_lit(me, " ");
    for_each (i, vec_spanned_type, types) {
        if (i_index) w_lit(me, ", ");
        w_node_visited(me, &i->type);
    }
    Span last = vec_spanned_type_back(types)->span;
    m_span(me, last, true);
}

static void emit_interface(Walk* me, InterfaceDecl const* decl, Statement const* statement, Container container) {
    w_indent(me);
    m_span(me, statement->span, false);
    w_statement_modifiers(me, statement, container, true);
    w_lit(me, "interface ");
    w_name(me, decl->name);
    w_type_params(me, &decl->generic);
    w_heritage(me, &decl->extends, "extends");
    w_lit(me, " ");
    w_members(me, &decl->type);
    m(me, statement_end(statement));
    w_lit(me, "\n");
}

static void emit_type_alias(Walk* me, TypeDecl const* decl, Statement const* statement, Container container) {
    w_indent(me);
    m_span(me, statement->span, false);
    w_statement_modifiers(me, statement, container, true);
    w_lit(me, "type ");
    w_name(me, decl->name);
    w_type_params(me, &decl->generic);
    w_lit(me, " = ");
    // (A `@typedef`'s `@property` literal, or an array of one: the transformer visits it, its members keep their tags'
    // descriptions)
    TypeNode const* literal = decl->type.tag == TYPE_ARRAY && decl->type.array.element ? decl->type.array.element : &decl->type;
    bool described = decl->jsdoc && literal->tag == TYPE_INTERFACE && literal->interface.jsdoc_literal;
    w_node_described(me, &decl->type, false, true, described ? &literal->interface : nullptr);
    w_lit(me, ";");
    m(me, decl->jsdoc && decl->jsdoc_end ? decl->jsdoc_end : statement_end(statement));
    w_lit(me, "\n");
}

static void emit_enum(Walk* me, EnumDecl const* decl, Statement const* statement, Container container) {
    w_indent(me);
    m_span(me, statement->span, false);
    w_statement_modifiers_and(me, statement, container, false, decl->is_const ? "const" : nullptr);
    w_lit(me, "enum ");
    w_name(me, decl->name);
    w_lit(me, " {\n");
    my indent++;
    u32 written = 0;
    for_each (i, vec_enum_case, &decl->cases) {
        if (is_internal_declaration(i->full_name)) continue;
        if (written++) {
            w_lit(me, ",");
            decl_comments_after_delimiter(&my comments, my out, my indent, i->full_name.buf, false);
            w_line(me);
        }
        w_leading(me, i->full_name.buf);
        w_indent(me);
        m_span(me, i->full_name, false);
        w_name(me, i->full_name);
        // (Its value where it has one: getEnumMemberValue)
        Ty const* type = i->symbol ? ty_type_of_symbol(my checker, i->symbol) : nullptr;
        bool literal = type && type->kind == TY_LITERAL && type->literal.enum_decl;
        double number = 0;
        int constant = i->value.tag == EXPR_IMPLICIT_ANY ? 0 : ty_enum_member_constant(my checker, i, &number);
        Ty const* value = nullptr;
        if (constant == 2 && literal && type->literal.string) {
            value = ty_string_literal(my checker, atom_sv(type->literal.string));
        } else if (constant == 1) {
            Value v = value_number(number);
            value = ty_literal(my checker, &v);
        } else if (i->value.tag == EXPR_IMPLICIT_ANY && literal && !type->literal.string && type->literal.value.tag == VALUE_NUMBER &&
            !isnan(type->literal.value.number)
        ) {
            value = ty_literal(my checker, &type->literal.value);
        }
        if (value) {
            w_lit(me, " = ");
            w_literal_value(me, value); // (A string escaped to ASCII: a string literal node it makes)
        }
        // (The member ends after its initializer)
        m_span(me, i->value.tag == EXPR_IMPLICIT_ANY ? i->full_name : expr_outer_span(&i->value), true);
        decl_comments_trailing(&my comments, my out, my indent, span_end_ptr(i->value.tag == EXPR_IMPLICIT_ANY ? i->full_name : expr_outer_span(&i->value)));
    }
    if (written) w_line(me);
    my indent--;
    w_indent(me);
    w_lit(me, "}");
    m(me, statement_end(statement));
    w_lit(me, "\n");
}

static void emit_statements(Walk* me, VecStatement const* statements, Container container);

static void emit_namespace(Walk* me, NamespaceDecl const* decl, Statement const* statement, Container container) {
    w_indent(me);
    m_span(me, statement->span, false);
    bool augmentation = decl->is_augmentation || decl->is_global;
    if (augmentation) {
        Modifiers mods = {0};
        if (container_is_file(container)) mods_add(&mods, "declare");
        w_modifiers(me, statement->span.buf, mods);
    } else {
        w_statement_modifiers(me, statement, container, false);
    }
    if (decl->is_global) {
        m_span(me, decl->name, false);
        w_lit(me, "global");
        m_span(me, decl->name, true);
    } else {
        w(me, decl->is_ambient_module ? c_sv("module ") : c_sv("namespace "));
        w_name(me, decl->name);
    }
    if (decl->is_shorthand) {
        w_lit(me, ";");
        m(me, statement_end(statement));
        w_lit(me, "\n");
        return;
    }
    // (`namespace A.B {}` is a namespace B exported from A: written as it was)
    NamespaceDecl const* body = decl;
    while (!decl->is_ambient_module && !decl->is_global && vec_statement_size(&body->contents.statements) == 1) {
        Statement const* only = vec_statement_at(&body->contents.statements, 0);
        if (only->tag != STATEMENT_DECL_NAMESPACE) break;
        NamespaceDecl const* inner = only->decl.namespace;
        char const* after = span_end_ptr(body->name);
        if (inner->name.buf != after + 1 || *after != '.') break;
        w_lit(me, ".");
        // (The inner declaration starts at its name)
        m_span(me, inner->name, false);
        w_name(me, inner->name);
        body = inner;
    }
    // (The module block, from its `{`)
    w_lit(me, " ");
    isize_t brace_at = cstr_size(my out);
    m(me, m_token_after(me, span_end_ptr(body->name), '{'));
    w_lit(me, "{\n");
    bool ambient = container.ambient || statement->decl.props.declared || decl->is_ambient_module || decl->is_global;
    Scope const* outer_scope = my scope;
    if (body->inner_scope) my scope = body->inner_scope;
    bool outer_marker = my has_scope_marker, outer_fix = my needs_scope_fix, outer_indicator = my has_module_indicator;
    u32 outer_offsets = my export_offset_count;
    my has_scope_marker = my needs_scope_fix = false;
    my indent++;
    u32 body_start = (u32)cstr_size(my out);
    emit_statements(me, &body->contents.statements, (Container){ .ambient = ambient });
    if (ambient) my needs_scope_fix = false;
    if (!decl->is_global && !my has_scope_marker) {
        if (my needs_scope_fix) {
            w_indent(me);
            w_lit(me, "export {};\n");
        } else if (!decl->jsdoc) {
            // (Every statement is exported: the modifiers are left out. Not a dotted `@typedef`'s namespace's, whose
            // alias tsgo's stripExportModifiers leaves alone)
            u32 from = outer_offsets;
            while (from < my export_offset_count && my export_offsets[from] < body_start) from++;
            erase_texts(my out, my export_offsets + from, my export_offset_count - from, 7);
        }
    }
    my export_offset_count = outer_offsets;
    my indent--;
    my scope = outer_scope;
    my has_scope_marker = outer_marker, my needs_scope_fix = outer_fix, my has_module_indicator = outer_indicator;
    // (An empty block is `{ }` where it was written on one line)
    char const* block_from = span_end_ptr(body->name);
    bool one_line = !memchr(block_from, '\n', (size_t)(span_end_ptr(decl->full_span) - block_from));
    if (cstr_size(my out) == body_start && one_line) {
        cstr_resize(my out, brace_at, '\0');
        m(me, m_token_after(me, span_end_ptr(body->name), '{'));
        w_lit(me, "{ }");
        m(me, statement_end(statement));
        w_lit(me, "\n");
        return;
    }
    w_indent(me);
    w_lit(me, "}");
    m(me, statement_end(statement));
    w_lit(me, "\n");
}

// ` from "m" with { ... }`: the specifier and attributes as written
// (The printer's ImportAttributes list, PreserveLines: the first attribute on the brace's line, the others and the
// closing brace on a line of their own where they were, an attribute as `name: value`)
static void w_specifier(Walk* me, PortStatement const* port) {
    w_name(me, port->target_path_span);
    ImportAttributes const* attributes = port->attributes;
    if (!attributes) return;
    w_lit(me, " ");
    if (!vec_import_attribute_size(&attributes->elements)) {
        w(me, span_sv(attributes->span));
        return;
    }
    m_span(me, attributes->keyword, false);
    w(me, span_sv(attributes->keyword));
    w_lit(me, " { ");
    char const* previous_end = nullptr;
    for_each (i, vec_import_attribute, &attributes->elements) {
        char const* start = i->name.buf;
        if (previous_end) {
            w_lit(me, ",");
            if (memchr(previous_end, '\n', (size_t)(start - previous_end))) {
                w_lit(me, "\n");
                my indent++;
                w_indent(me);
                my indent--;
            } else {
                w_lit(me, " ");
            }
        }
        m_span(me, i->name, false);
        w_name(me, i->name);
        w_lit(me, ": ");
        w_name(me, expr_outer_span(&i->value));
        m_span(me, expr_outer_span(&i->value), true);
        previous_end = span_end_ptr(expr_outer_span(&i->value));
    }
    char const* close = span_end_ptr(attributes->span) - 1;
    if (memchr(previous_end, '\n', (size_t)(close - previous_end))) {
        w_lit(me, "\n");
        w_indent(me);
        w_lit(me, "}");
    } else {
        w_lit(me, " }");
    }
    m_span(me, attributes->span, true);
}

// `{ a, type b as c }`: an import or export declaration's specifiers (`kept` the ones written, null for all), with their
// comments, an export's trailing comma as written (the printer's NamedImportsOrExportsElements)
// Declaration maps: where a named import or export list's braces are, from its items (the `{` before the first, the `}`
// after the last and a comma)
static char const* m_list_open(Walk* me, PortStatement const* port) {
    if (!my map || !vec_port_item_size(&port->items) || !span_valid(port->keyword_span)) return nullptr;
    Span first = vec_port_item_at(&port->items, 0)->full_span;
    char const* text = cstr_str(&my file->contents);
    char const* end = text + cstr_size(&my file->contents);
    if (!span_valid(first) || first.buf < text || first.buf > end) return nullptr;
    // (Forward from the keyword over trivia, `type`, a default import and its comma)
    for (char const* c = decl_map_skip_trivia(span_end_ptr(port->keyword_span), end); c < first.buf; c = decl_map_skip_trivia(c, end)) {
        if (*c == '{') return c;
        if (*c == ',') {
            c++;
            continue;
        }
        if (!is_word_char(*c)) return nullptr;
        while (c < end && is_word_char(*c)) c++;
    }
    return nullptr;
}

static char const* m_list_end(Walk* me, PortStatement const* port) {
    if (!my map || !vec_port_item_size(&port->items)) return nullptr;
    Span last = vec_port_item_back(&port->items)->full_span;
    if (!span_valid(last)) return nullptr;
    char const* end = span_end_ptr(last);
    char const* comma = m_token_after(me, end, ',');
    char const* close = m_token_after(me, comma ? comma + 1 : end, '}');
    return close ? close + 1 : nullptr;
}

static void w_port_items(Walk* me, PortStatement const* port, bool const* kept, bool trailing_comma) {
    u32 written = 0;
    PortItem const* last = nullptr;
    for_each (i, vec_port_item, &port->items) {
        if (kept && !kept[i_index]) continue;
        if (!written) m(me, m_list_open(me, port));
        w(me, written++ ? c_sv(", ") : c_sv("{ "));
        char const* start = span_valid(i->full_span) ? i->full_span.buf : nullptr;
        if (start) {
            decl_comments_intervening(&my comments, my out, my indent, start);
            w_leading(me, start);
            w_indent(me);
        }
        m(me, start);
        if (i->type_modifier) w_lit(me, "type ");
        w_name(me, i->source_name);
        if (!span_eq(i->source_name, i->decl_name) && span_valid(i->decl_name)) {
            w_lit(me, " as ");
            w_name(me, i->decl_name);
        }
        m_span(me, i->full_span, true);
        if (start) decl_comments_trailing(&my comments, my out, my indent, span_end_ptr(i->full_span));
        last = i;
    }
    if (!written) {
        w_lit(me, "{}");
        return;
    }
    if (trailing_comma && last && span_valid(last->full_span)) {
        char const* after = decl_comments_end(&my comments, span_end_ptr(last->full_span), ",");
        if (after != span_end_ptr(last->full_span) && after[-1] == ',') w_lit(me, ",");
    }
    w_lit(me, " }");
    m(me, m_list_end(me, port));
}

// An import or export declaration (transformImportDeclaration: an import keeps the names emitted declarations use)
// (An import's name is kept where an emitted declaration names it, or `export { x }` does)
#define KEPT(symbol) (was_painted(me, symbol) || (container.module_top_level && is_exported_local(me, symbol)))

static void emit_port(Walk* me, Statement const* statement, Container container) {
    PortStatement const* port = statement->import_export;
    bool is_file = container_is_file(container);
    csview specifier = span_sv(port->target_path_span);
    if (span_equals(port->keyword_span, "export") && !port->main.export_import) {
        if (!vec_port_item_size(&port->items) && port->main.tag == PORT_NOOP && !port->module_ref) {
            // `export {}`
            w_indent(me);
            m_span(me, statement->span, false);
            w_lit(me, "export ");
            char const* open = span_valid(port->keyword_span) ? m_token_after(me, span_end_ptr(port->keyword_span), '{') : nullptr;
            char const* close = open ? m_token_after(me, open + 1, '}') : nullptr;
            m(me, open);
            w_lit(me, "{}");
            if (close) m(me, close + 1);
            w_lit(me, ";");
            m(me, statement_end(statement));
            w_lit(me, "\n");
            my has_scope_marker = true;
            if (is_file) my has_module_indicator = true;
            return;
        }
        w_indent(me);
        if (port->main.tag == PORT_EXPORT_AS_NAMESPACE) {
            w_lit(me, "export as namespace ");
            w(me, span_sv(port->main.decl_name));
            w_lit(me, ";\n");
            return;
        }
        my has_scope_marker = true;
        if (is_file) my has_module_indicator = true;
        m_span(me, statement->span, false);
        w_lit(me, "export ");
        if (port->main.type_only) w_lit(me, "type ");
        if (port->main.tag == PORT_REEXPORT_SPLAT) {
            w_lit(me, "*");
        } else if (port->main.tag == PORT_REEXPORT_GATHER) {
            // (`* as ns`: a node from its `*`)
            char const* star = m_keyword_before(me, m_keyword_before(me, port->main.decl_name.buf, "as"), "*");
            m(me, star);
            w_lit(me, "* as ");
            w_name(me, port->main.decl_name);
        } else {
            w_port_items(me, port, nullptr, true);
        }
        if (specifier.size) {
            w_lit(me, " from ");
            w_specifier(me, port);
        }
        w_lit(me, ";");
        m(me, statement_end(statement));
        w_lit(me, "\n");
        return;
    }
    // Imports
    if (port->side_effect_only) {
        w_indent(me);
        m_span(me, statement->span, false);
        w_lit(me, "import ");
        w_specifier(me, port);
        w_lit(me, ";");
        m(me, statement_end(statement));
        w_lit(me, "\n");
        if (is_file) my has_module_indicator = true;
        return;
    }
    if (port->main.tag == PORT_IMPORT_ALIAS || port->is_import_require) {
        PortItem const* item = &port->main;
        // (A script's own import alias is global, visible like its other declarations: tsc's
        // determineIfDeclarationIsVisible, isGlobalSourceFile of its parent; not in an ambient namespace)
        if (!item->export_import && !container.global && !KEPT(item->symbol)) return;
        w_indent(me);
        m_span(me, statement->span, false);
        if (item->export_import) w_lit(me, "export ");
        w_lit(me, "import ");
        if (item->type_only) w_lit(me, "type ");
        w_name(me, item->decl_name);
        w_lit(me, " = ");
        if (port->is_import_require) {
            // (The external module reference, from its `require`)
            m(me, m_keyword_before(me, m_keyword_before(me, port->target_path_span.buf, "("), "require"));
            w_lit(me, "require(");
            w_specifier(me, port);
            w_lit(me, ")");
            char const* close = span_valid(port->target_path_span) ? m_token_after(me, span_end_ptr(port->target_path_span), ')') : nullptr;
            if (close) m(me, close + 1);
            if (is_file) my has_module_indicator = true;
        } else {
            VecSpan const* path = &port_item_alias(item)->path;
            // (A qualified name: each from the first name's start)
            for (isize_t k = vec_span_size(path); k-- > 1;) m_span(me, *vec_span_at(path, 0), false);
            for_each (i, vec_span, path) {
                if (i_index) w_lit(me, ".");
                w_name(me, *i);
                if (i_index) m_span(me, *i, true);
            }
            if (vec_span_size(path) == 1 && !vec_span_at(path, 0)->size) {
                // (No reference, `import from 'foo'`: tsc's missing identifier ends where it starts, before the trivia
                // its start skips)
                m(me, decl_map_back_trivia(vec_span_at(path, 0)->buf, statement->span.buf));
            }
        }
        w_lit(me, ";");
        m(me, statement_end(statement));
        w_lit(me, "\n");
        return;
    }
    bool default_kept = port->main.tag == PORT_IMPORT_DEFAULT && KEPT(port->main.symbol);
    // (`import d, * as ns`: the namespace import is one of the items)
    PortItem const* gather = port->main.tag == PORT_IMPORT_GATHER ? &port->main : nullptr;
    for_each (i, vec_port_item, &port->items) if (i->tag == PORT_IMPORT_GATHER) gather = i;
    bool gather_kept = gather && KEPT(gather->symbol);
    u32 named = 0;
    for_each (i, vec_port_item, &port->items) named += i->tag != PORT_IMPORT_GATHER && KEPT(i->symbol);
    if (!default_kept && !gather_kept && !named) {
        // (Only named imports, `import {} from`, are kept for an augmentation: transformImportDeclaration elides a
        // default or namespace import whose name isn't visible)
        bool named_imports = !gather && (port->main.tag == PORT_NOOP || vec_port_item_size(&port->items));
        if (named_imports && port->module_ref && port->module_ref->file && port->module_ref->file != my file &&
            import_required_by_augmentation(me, port->module_ref->file)
        ) {
            w_indent(me);
            m_span(me, statement->span, false);
            w_lit(me, "import ");
            w_specifier(me, port);
            w_lit(me, ";");
            m(me, statement_end(statement));
            w_lit(me, "\n");
            if (is_file) my has_module_indicator = true;
        }
        return;
    }
    if (is_file) my has_module_indicator = true;
    w_indent(me);
    m_span(me, statement->span, false);
    w_lit(me, "import ");
    // (The import clause, from its first token)
    if (span_valid(port->keyword_span)) {
        char const* end = cstr_str(&my file->contents) + cstr_size(&my file->contents);
        if (my map) m(me, decl_map_skip_trivia(span_end_ptr(port->keyword_span), end));
    }
    if (port->main.type_only || port->jsdoc) w_lit(me, "type ");
    if (port->source_phase) w_lit(me, "source ");
    if (default_kept) {
        w_name(me, port->main.decl_name);
        if (gather_kept || named) w_lit(me, ", ");
    }
    if (gather_kept) {
        char const* star = m_keyword_before(me, m_keyword_before(me, gather->decl_name.buf, "as"), "*");
        m(me, star);
        w_lit(me, "* as ");
        w_name(me, gather->decl_name);
    } else if (named) {
        bool* kept = malloc(sizeof(bool) * (size_t)vec_port_item_size(&port->items));
        for_each (i, vec_port_item, &port->items) kept[i_index] = KEPT(i->symbol);
        w_port_items(me, port, kept, false);
        free(kept);
    }
    // (The clause keeps its range, whatever of it is left: it ends before `from`)
    if (my map && span_valid(port->target_path_span)) {
        char const* from = m_keyword_before(me, port->target_path_span.buf, "from");
        if (from) m(me, decl_map_back_trivia(from, cstr_str(&my file->contents)));
    }
    w_lit(me, " from ");
    w_specifier(me, port);
    w_lit(me, ";");
    m(me, statement_end(statement));
    w_lit(me, "\n");
}

// The name an export default's declaration gets: the function or class expression's own, else `_default` (tsc's
// getNameOfExportedAssignedExpression), `_1`... where that is taken in the scope
static bool cjs_name_taken(Walk* me, csview name) {
    for (csview rest = cstr_sv(&my cjs_names); rest.size;) {
        isize_t eol = csview_find_sv(rest, c_sv("\n"));
        if (csview_equals_sv(csview_slice(rest, 0, eol), name)) return true;
        rest = csview_slice(rest, eol + 1, rest.size);
    }
    return false;
}

// A generated name (the printer's optimistic unique names): `base`, else `base_1`... avoiding what resolves where the
// file's declarations are, the file's words, and what the writer generated already
static cstr generated_unique_name(Walk* me, csview base, bool generated) {
    cstr name = cstr_from_sv(base);
    for (u32 n = 1; (my scope && scope_resolve(my scope, cstr_sv(&name), SYMBOL_VALUE | SYMBOL_TYPE | SYMBOL_NAMESPACE)) ||
        (generated && (file_has_word(my file, cstr_sv(&name)) || cjs_name_taken(me, cstr_sv(&name)))); n++
    ) {
        cstr_take(&name, cstr_d(base, "_", n));
    }
    if (generated) {
        cstr_append_sv(&my cjs_names, cstr_sv(&name));
        cstr_append(&my cjs_names, "\n");
    }
    return name;
}

static cstr export_default_name_or(Walk* me, Span own, csview fallback) {
    bool generated = !span_valid(own) || span_equals(own, "default");
    // (The printer's unique names avoid every identifier of the file too: isUniqueName)
    return generated_unique_name(me, generated ? fallback : span_sv(own), generated);
}

static cstr export_default_name(Walk* me, Span own) { return export_default_name_or(me, own, c_sv("_default")); }

// `export default <expression>` (tsc's transformExportAssignment): a name as written, a function or class expression as a
// declaration after the export (the statement's comments on it), anything else as `declare const _default: T;` before
// it (the comments on that)
static void emit_export_assignment_of(
    Walk* me, Expr const* expr, TypeNode const* annotation, char const* end_mark, Span span, bool export_equals, bool name_kept,
    Container container, csview fallback, cstr* name_out
);

static void emit_export_default(Walk* me, Statement const* statement, Container container) {
    emit_export_assignment_of(me, &statement->expr, statement->expr_jsdoc_type, statement_end(statement), statement->span,
        statement->export_equals, true, container, c_sv("_default"), nullptr);
}

// (`span`: the statement's, or a JavaScript file's `module.exports = v` (its comments are those before it), `end_mark`
// where it ends for the declaration map. Without `name_kept` a name is a value like any other. `name_out`: the name a
// declaration got, if one did. `annotation`: a JavaScript one's JSDoc `@type`, the type written)
static void emit_export_assignment_of(
    Walk* me, Expr const* expr, TypeNode const* annotation, char const* end_mark, Span span, bool export_equals, bool name_kept,
    Container container, csview fallback, cstr* name_out
) {
    bool is_file = container_is_file(container);
    if (is_file) my has_module_indicator = true;
    my has_scope_marker = true;
    char const* comments_from = span.buf;
    char const* comments_end = span_end_ptr(span);
    csview keyword = export_equals ? c_sv("export = ") : c_sv("export default ");
    // (The export assignment keeps the statement's range, tsc's AssignSourceMapRange)
    if (expr->tag == EXPR_IDENTIFIER && name_kept) {
        w_leading(me, comments_from);
        w_indent(me);
        m_span(me, span, false);
        w(me, keyword);
        w_name(me, expr->inner_span);
        w_lit(me, ";");
        m(me, end_mark);
        decl_comments_trailing(&my comments, my out, my indent, comments_end);
        w_lit(me, "\n");
        return;
    }
    if (!annotation && ((expr->tag == EXPR_FUNCTION && !expr->function->is_constructor) || expr->tag == EXPR_CLASS_DECL)) {
        Span own = expr->tag == EXPR_FUNCTION ? expr->function->name : expr->class_decl->name;
        cstr name = export_default_name_or(me, own, fallback);
        w_indent(me);
        m_span(me, span, false);
        w(me, keyword);
        w(me, cstr_sv(&name));
        w_lit(me, ";");
        m(me, end_mark);
        w_lit(me, "\n");
        w_leading(me, comments_from);
        if (expr->tag == EXPR_FUNCTION) {
            w_indent(me);
            if (is_file) w_lit(me, "declare ");
            w_lit(me, "function ");
            w(me, cstr_sv(&name));
            w_signature(me, expr->function, false);
            w_lit(me, ";");
            decl_comments_trailing(&my comments, my out, my indent, comments_end);
            w_lit(me, "\n");
        } else {
            emit_class(me, expr->class_decl, nullptr, container, cstr_sv(&name), c_sv(""));
            isize_t size = cstr_size(my out);
            cstr_erase(my out, size - 1, 1);
            decl_comments_trailing(&my comments, my out, my indent, comments_end);
            w_lit(me, "\n");
        }
        if (name_out) cstr_take(name_out, name);
        else cstr_drop(&name);
        return;
    }
    // `declare const _default: T; export default _default;` (a literal's: `= "x"`)
    cstr text = cstr_init();
    Ty const* expr_type = lazy_type_get((LazyType*)&expr->lazy_type);
    bool literal_value = !annotation && expr_type && ty_is_fresh(expr_type) && is_primitive_literal_value(expr, true);
    cstr* outer = my type_out;
    my type_out = annotation ? nullptr : &text;
    Emitting saved = emit_enter(me, EMIT_DEFAULT_EXPORT, SIG_FUNCTION, span_null, span);
    Root outer_root = my root;
    my root = (Root){ .kind = ROOT_EXPORT_DEFAULT, .expr = expr, .at = span };
    Ty const* type = lazy_type_get((LazyType*)&expr->lazy_type);
    // (The default export's type is the expression's widened like a variable's, fresh literals included: tsc's
    // widenTypeForVariableLikeDeclaration in getTypeOfVariableOrParameterOrProperty, `'ok' satisfies string` is string)
    type = ty_regular(my checker, ty_widened_literal(my checker, ty_widened_nullable(my checker, ty_widened_objects(my checker, type))));
    serialize_declaration(me, pseudo_of_expression(my checker, expr, (PseudoPlace){0}), type, false, false);
    my root = outer_root;
    emit_leave(me, saved);
    my type_out = outer;
    cstr name = expr->tag == EXPR_IDENTIFIER ? export_default_name_or(me, expr->inner_span, fallback) :
        export_default_name_or(me, span_null, fallback);
    // (`module.exports = (x) => {}` with a JSDoc `@type`: tsgo's transformFunctionLikeToDeclaration of the full signature,
    // the export assignment first like a function's)
    bool export_first = annotation && export_equals && expr->tag == EXPR_FUNCTION && !expr->function->is_constructor;
    if (export_first) {
        w_indent(me);
        m_span(me, span, false);
        w(me, keyword);
        w(me, cstr_sv(&name));
        w_lit(me, ";");
        m(me, end_mark);
        w_lit(me, "\n");
    }
    w_leading(me, comments_from);
    w_indent(me);
    if (is_file) w_lit(me, "declare ");
    w_lit(me, "const ");
    w(me, cstr_sv(&name));
    if (literal_value) {
        cstr_drop(&text);
        w_lit(me, " = ");
        w_literal_value(me, expr_type);
    } else if (annotation) {
        cstr_drop(&text);
        w_lit(me, ": ");
        // (`export default`'s is printed as its type, `module.exports =`' reused: tsgo's reparser makes the former a
        // cast of the expression)
        if (export_equals) w_node(me, annotation);
        else w_type(me, ty_of_node(annotation));
    } else {
        w_lit(me, ": ");
        w_serialized(me, &text);
    }
    w_lit(me, ";");
    decl_comments_trailing(&my comments, my out, my indent, comments_end);
    w_lit(me, "\n");
    if (!export_first) {
        w_indent(me);
        m_span(me, span, false);
        w(me, keyword);
        w(me, cstr_sv(&name));
        w_lit(me, ";");
        m(me, end_mark);
        w_lit(me, "\n");
    }
    if (name_out) cstr_take(name_out, name);
    else cstr_drop(&name);
}

static void emit_statement_inner(Walk* me, Statement const* statement, Container container);
static void emit_export_default(Walk* me, Statement const* statement, Container container);

// A JavaScript statement list has its statements' JSDoc declarations (`@typedef`, `@callback`, `@import`) after each
// statement (jsdoc_bind_comments()); tsgo's parser puts its reparsed ones before it: they are written there
static bool is_jsdoc_declaration(Statement const* statement) {
    return (statement->tag == STATEMENT_DECL_TYPE && statement->decl.type->jsdoc) ||
        (statement->tag == STATEMENT_DECL_NAMESPACE && statement->decl.namespace->jsdoc) ||
        (statement->tag == STATEMENT_IMPORT_EXPORT && statement->import_export->jsdoc);
}

static void emit_statement(Walk* me, Statement const* statement, Container container) {
    if (is_internal_declaration(statement->span)) return;
    if (is_jsdoc_declaration(statement)) {
        // (Made from a tag: the comment stays where it is, the declaration has none)
        emit_statement_inner(me, statement, container);
        return;
    }
    if (statement->tag == STATEMENT_EXPORT_DEFAULT_EXPR) {
        emit_export_default(me, statement, container); // (Its comments go to the declaration it makes)
        return;
    }
    isize_t outer_start = my statement_start;
    bool outer_synthesized = my statement_synthesized;
    my statement_start = cstr_size(my out);
    my statement_synthesized = false;
    isize_t start = w_leading(me, statement->span.buf);
    isize_t written_from = cstr_size(my out);
    emit_statement_inner(me, statement, container);
    my statement_start = outer_start;
    bool synthesized = my statement_synthesized;
    my statement_synthesized = outer_synthesized;
    // (A declaration made anew has no comments of the statement's: the leading ones were taken back)
    if (!synthesized) w_unless_written(me, start, written_from, span_end_ptr(statement->span));
}

static void emit_statement_inner(Walk* me, Statement const* statement, Container container) {
    switch (statement->tag) {
        case STATEMENT_IMPORT_EXPORT: emit_port(me, statement, container); return;
        case STATEMENT_DECL_VARIABLE: emit_variable_statement(me, statement, container); return;
        case STATEMENT_DECL_FUNCTION:
            if (emit_visible(me, statement, container, statement->decl.function.impl->symbol)) {
                emit_function_declaration(me, statement, container);
            }
            return;
        case STATEMENT_DECL_CLASS:
            if (emit_visible(me, statement, container, statement->decl.class->symbol)) {
                emit_class(me, statement->decl.class, statement, container, c_sv(""), c_sv(""));
            }
            return;
        case STATEMENT_DECL_INTERFACE:
            if (emit_visible(me, statement, container, statement->decl.interface->symbol)) {
                emit_interface(me, statement->decl.interface, statement, container);
            }
            return;
        case STATEMENT_DECL_TYPE: {
            TypeDecl const* decl = statement->decl.type;
            if (decl->jsdoc_check_only) return;
            if (emit_visible(me, statement, container, decl->symbol)) emit_type_alias(me, decl, statement, container);
            return;
        }
        case STATEMENT_DECL_ENUM:
            if (emit_visible(me, statement, container, statement->decl.enum_->symbol)) {
                emit_enum(me, statement->decl.enum_, statement, container);
            }
            return;
        case STATEMENT_DECL_NAMESPACE: {
            NamespaceDecl const* decl = statement->decl.namespace;
            bool augmentation = decl->is_augmentation || decl->is_global;
            if (augmentation || emit_visible(me, statement, container, decl->symbol)) emit_namespace(me, decl, statement, container);
            return;
        }
        case STATEMENT_EXPORT_DEFAULT_EXPR: emit_export_default(me, statement, container); return;
        default: return;
    }
}


static void emit_statements(Walk* me, VecStatement const* statements, Container container) {
    if (!my file->is_js) {
        for_each (i, vec_statement, statements) emit_statement(me, i, container);
        return;
    }
    isize count = vec_statement_size(statements);
    for (isize i = 0; i < count;) {
        Statement const* statement = vec_statement_at(statements, i);
        isize next = i + 1;
        if (!is_jsdoc_declaration(statement)) {
            char const* end = span_end_ptr(statement->span);
            while (next < count && is_jsdoc_declaration(vec_statement_at(statements, next)) &&
                span_start_ptr(vec_statement_at(statements, next)->span) < end
            ) {
                next++;
            }
            for (isize j = i + 1; j < next; j++) emit_statement(me, vec_statement_at(statements, j), container);
        }
        emit_statement(me, statement, container);
        i = next;
    }
}

// The `/// <reference ... preserve="true" />` directives (tsc's getReferencedFiles, getTypeReferences, getLibReferences:
// only preserved ones are kept), paths, types, then libs. A path names the referenced file's declaration file
static void emit_directives(Walk* me) {
    static char const* const kinds[] = { "path", "types", "lib" };
    static int const tags[] = { SOURCE_DIRECTIVE_REFERENCE_PATH, SOURCE_DIRECTIVE_REFERENCE_TYPES, SOURCE_DIRECTIVE_REFERENCE_LIB };
    for (u32 k = 0; k < 3; k++) {
        for_each (i, vec_statement, &my file->ast.statements) {
            if (i->tag != STATEMENT_SOURCE_DIRECTIVE) continue;
            SourceDirectiveStatement const* directive = i->source_directive;
            if ((int)directive->tag != tags[k] || !directive->preserve) continue;
            csview path = cstr_sv(&directive->path);
            while (csview_starts_with(path, "./")) path = csview_slice(path, 2, path.size);
            // (A path names the referenced file's declaration output relative to this one's, tsc's getReferencedFiles;
            // one the program didn't load is dropped)
            cstr relative = cstr_null;
            if (tags[k] == SOURCE_DIRECTIVE_REFERENCE_PATH && cstr_size(&my file->declaration_output_path)) {
                SourceFile const* referenced = directive->referenced;
                if (!referenced || !cstr_size(&referenced->declaration_output_path)) continue;
                relative = relative_path_of(dirname_of(cstr_sv(&my file->declaration_output_path)),
                    cstr_sv(&referenced->declaration_output_path));
                path = cstr_sv(&relative);
            }
            w_lit(me, "/// <reference ");
            w(me, csview_from(kinds[k]));
            w_lit(me, "=\"");
            bool declaration = csview_ends_with(path, ".d.ts") || csview_ends_with(path, ".d.mts") || csview_ends_with(path, ".d.cts");
            if (tags[k] == SOURCE_DIRECTIVE_REFERENCE_PATH && !declaration && (csview_ends_with(path, ".ts") || csview_ends_with(path, ".tsx"))) {
                w(me, csview_slice(path, 0, path.size - (csview_ends_with(path, ".ts") ? 3 : 4)));
                w_lit(me, ".d.ts");
            } else if (tags[k] == SOURCE_DIRECTIVE_REFERENCE_PATH && !declaration && (csview_ends_with(path, ".mts") || csview_ends_with(path, ".cts"))) {
                w(me, csview_slice(path, 0, path.size - 4));
                w(me, csview_ends_with(path, ".mts") ? c_sv(".d.mts") : c_sv(".d.cts"));
            } else {
                w(me, path);
            }
            w_lit(me, "\" ");
            if (directive->resolution_override == IMPORT_RESOLUTION_IMPORT) w_lit(me, "resolution-mode=\"import\" ");
            if (directive->resolution_override == IMPORT_RESOLUTION_REQUIRE) w_lit(me, "resolution-mode=\"require\" ");
            w_lit(me, "preserve=\"true\" />\n");
            cstr_drop(&relative);
        }
    }
}

// The export's name finds something else where the file's declarations are (tsgo's GetReferencedValueDeclaration of
// the name: a lexical lookup), which a variable of that name would shadow
static bool cjs_name_resolves_elsewhere(Walk* me, CjsMember const* member) {
    Symbol const* found = my file->scope ? scope_resolve(my file->scope, member->name, SYMBOL_VALUE) : nullptr;
    return found && found != cjs_member_symbol(me, member);
}

// A CommonJS export's name as an export specifier's: an identifier, else a string literal
static void w_export_name(Walk* me, CjsMember const* member) {
    if (member->identifier) {
        w(me, member->name);
        return;
    }
    w_lit(me, "\"");
    w(me, member->name);
    w_lit(me, "\"");
}

// `<modifiers>const NAME: T;` of a member's type
static void w_cjs_member_variable(Walk* me, CjsMember const* member, char const* keyword, csview name) {
    TypeNode const* annotation = cjs_member_annotation(member);
    cstr text = cstr_init();
    if (!annotation) {
        cstr* outer = my type_out;
        my type_out = &text;
        cjs_serialize_member(me, member);
        my type_out = outer;
    }
    w_indent(me);
    w(me, csview_from(keyword));
    w(me, name);
    w_lit(me, ": ");
    if (annotation) {
        w_node(me, annotation);
        cstr_drop(&text);
    } else {
        w_serialized(me, &text);
    }
    w_lit(me, ";\n");
}

// Whether a class expression's members print its own symbol (written into a scratch string to find out)
static bool cjs_class_self_referenced(Walk* me, ClassDecl const* cls) {
    if (!cls->symbol) return false;
    cstr* outer = my out;
    cstr scratch = cstr_init();
    my out = &scratch;
    bool map = my map;
    my map = false;
    my cjs_watched_class = cls->symbol;
    my cjs_class_tracked = false;
    emit_class(me, cls, nullptr, (Container){0}, span_sv(cls->name), c_sv("export "));
    bool tracked = my cjs_class_tracked;
    my cjs_watched_class = nullptr;
    my map = map;
    my out = outer;
    cstr_drop(&scratch);
    return tracked;
}

// A CommonJS export (tsgo's transformCommonJSExportWorker), in a namespace of the `export=`'s name where there is one
// (wrapInCJSExportNamespace: one per export, without `declare`s)
static void emit_commonjs_member(Walk* me, CjsMember const* member, csview ns) {
    bool wrapped = ns.size;
    cstr* outer = my out;
    cstr content = cstr_init();
    my out = &content;
    if (wrapped) my indent++;
    char const* declare = wrapped ? "" : "declare ";
    my has_module_indicator = my has_scope_marker = true;
    Expr const* value = member->value;
    char const* comments_from = expr_outer_span(member->expr).buf;
    if (cjs_member_is_alias(me, member)) {
        // `export { a as x };`
        w_indent(me);
        w_lit(me, "export { ");
        csview source = span_sv(value->inner_span);
        if (!member->identifier || !csview_equals_sv(source, member->name)) {
            w(me, source);
            w_lit(me, " as ");
        }
        w_export_name(me, member);
        w_lit(me, " };\n");
    } else if (value && value->tag == EXPR_CLASS_DECL) {
        ClassDecl const* cls = value->class_decl;
        bool named = span_valid(cls->name) && cls->name.size;
        cstr modifiers = cstr_d("export ", declare);
        // (Its members see its own name)
        Scope const* outer_scope = my scope;
        if (cls->name_scope) my scope = cls->name_scope;
        bool isolated = named && (!member->identifier || !csview_equals_sv(span_sv(cls->name), member->name) ||
            cjs_class_self_referenced(me, cls));
        if (named && !isolated) {
            w_leading(me, comments_from);
            emit_class(me, cls, nullptr, (Container){ .module_top_level = !wrapped }, span_sv(cls->name), cstr_sv(&modifiers));
        } else if (named) {
            // (Another name than the export's: the class in a namespace of its own, `_ns`, exported through an alias)
            cstr ns_name = generated_unique_name(me, c_sv("_ns"), true);
            cstr alias_base = member->identifier && name_is_identifier(member->name) ? cstr_d("_", member->name) : cstr_from("_exported");
            cstr alias = generated_unique_name(me, cstr_sv(&alias_base), true);
            w_indent(me);
            w(me, csview_from(declare));
            w_lit(me, "namespace ");
            w(me, cstr_sv(&ns_name));
            w_lit(me, " {\n");
            my indent++;
            w_leading(me, comments_from);
            emit_class(me, cls, nullptr, (Container){0}, span_sv(cls->name), c_sv("export "));
            my indent--;
            w_indent(me);
            w_lit(me, "}\n");
            w_indent(me);
            w_lit(me, "import ");
            w(me, cstr_sv(&alias));
            w_lit(me, " = ");
            w(me, cstr_sv(&ns_name));
            w_lit(me, ".");
            w(me, span_sv(cls->name));
            w_lit(me, ";\n");
            w_indent(me);
            w_lit(me, "export { ");
            w(me, cstr_sv(&alias));
            w_lit(me, " as ");
            w_export_name(me, member);
            w_lit(me, " };\n");
            c_drop(cstr, &ns_name, &alias_base, &alias);
        } else if (member->identifier) {
            w_leading(me, comments_from);
            emit_class(me, cls, nullptr, (Container){ .module_top_level = !wrapped }, member->name, cstr_sv(&modifiers));
        } else {
            cstr name = generated_unique_name(me, c_sv("_class"), true);
            w_leading(me, comments_from);
            emit_class(me, cls, nullptr, (Container){ .module_top_level = !wrapped }, cstr_sv(&name), cstr_sv(&modifiers));
            w_indent(me);
            w_lit(me, "export { ");
            w(me, cstr_sv(&name));
            w_lit(me, " as ");
            w_export_name(me, member);
            w_lit(me, " };\n");
            cstr_drop(&name);
        }
        my scope = outer_scope;
        cstr_drop(&modifiers);
    } else if (member->identifier && csview_equals(member->name, "default")) {
        // `declare const _default: T; export default _default;`
        cstr name = generated_unique_name(me, c_sv("_default"), true);
        w_leading(me, comments_from);
        w_cjs_member_variable(me, member, wrapped ? "const " : "declare const ", cstr_sv(&name));
        w_indent(me);
        w_lit(me, "export default ");
        w(me, cstr_sv(&name));
        w_lit(me, ";\n");
        cstr_drop(&name);
    } else if (member->identifier && !cjs_name_resolves_elsewhere(me, member)) {
        w_cjs_member_variable(me, member, wrapped ? "export var " : "export declare var ", member->name);
    } else {
        // `declare const _exported: T; export { _exported as "x" };`
        cstr name = generated_unique_name(me, c_sv("_exported"), true);
        w_leading(me, comments_from);
        w_cjs_member_variable(me, member, wrapped ? "const " : "declare const ", cstr_sv(&name));
        w_indent(me);
        w_lit(me, "export { ");
        w(me, cstr_sv(&name));
        w_lit(me, " as ");
        w_export_name(me, member);
        w_lit(me, " };\n");
        cstr_drop(&name);
    }
    if (wrapped) my indent--;
    my out = outer;
    if (wrapped) {
        w_indent(me);
        w_lit(me, "declare namespace ");
        w(me, ns);
        w_lit(me, " {\n");
    }
    w(me, cstr_sv(&content));
    if (wrapped) {
        w_indent(me);
        w_lit(me, "}\n");
    }
    cstr_drop(&content);
}

// A CommonJS file's exports, before its statements (tsgo's appendCjsExports): the `export=`, then each export
static void emit_commonjs(Walk* me, Container top) {
    cstr ns = cstr_init();
    if (my cjs_export_equals) {
        Expr const* assignment = my cjs_export_equals;
        Expr const* value = cjs_assigned_value(assignment);
        Statement const* statement = nullptr;
        if (my cjs_export_equals_top_level) {
            for_each (i, vec_statement, &my file->ast.statements) {
                if (i->tag == STATEMENT_EXPR && &i->expr == assignment) statement = i;
            }
        }
        Span span = statement ? statement->span : expr_outer_span(assignment);
        char const* end_mark = statement ? statement_end(statement) :
            my map ? decl_comments_end(&my comments, span_end_ptr(span), ";") : nullptr;
        emit_export_assignment_of(me, value, assignment->binary_op->jsdoc_type, end_mark, span, true,
            my cjs_export_equals_top_level, top, c_sv("_exports"), &ns);
        my has_module_indicator = my has_scope_marker = true;
    }
    for (u32 i = 0; i < my cjs_member_count; i++) emit_commonjs_member(me, &my cjs_members[i], cstr_sv(&ns));
    cstr_drop(&ns);
}

// The second walk: the file's `.d.ts`
static void emit_file(Walk* me, Container top) {
    my comments = (DeclComments){ .file = my file, .off = compiler_options()->remove_comments, .map = my map };
    // (The shebang is kept: the printer's emitShebangIfNeeded)
    csview text = cstr_sv(&my file->contents);
    if (csview_starts_with(text, "\xEF\xBB\xBF")) text = csview_slice(text, 3, text.size);
    if (csview_starts_with(text, "#!")) {
        isize_t line = 0;
        while (line < text.size && text.buf[line] != '\n' && text.buf[line] != '\r') line++;
        w(me, csview_slice(text, 0, line));
        w_lit(me, "\n");
    }
    decl_comments_detached(&my comments, my out);
    emit_directives(me);
    if (my file->is_js && my file->is_commonjs) emit_commonjs(me, top);
    emit_statements(me, &my file->ast.statements, top);
    // (A module whose declarations wouldn't say so, or one with a declaration that isn't exported and nothing that says
    // which are: `export {};`)
    if (my file->is_module && (!my has_module_indicator || (my needs_scope_fix && !my has_scope_marker))) {
        w_lit(me, "export {};\n");
    }
    Statement const* last = nullptr;
    for_each (i, vec_statement, &my file->ast.statements) {
        if (span_valid(i->span) && i->tag != STATEMENT_SOURCE_DIRECTIVE && !is_jsdoc_declaration(i)) last = i;
    }
    if (last) decl_comments_file_end(&my comments, my out, span_end_ptr(last->span));
    else decl_comments_empty_file_end(&my comments, my out);
}

// Whether a file's declaration emit is walked for its diagnostics (written where emitting)
bool declaration_diagnostics_wanted(SourceFile const* file) {
    CompilerOptions const* options = compiler_options();
    if (!options->declaration && !options->composite) return false;
    if (file->is_declaration || file->is_json || file->is_stdlib || file->is_config) return false;
    // (tsc's sourceFileMayBeEmitted: not a package's file that an import found through a node_modules, even where a
    // `paths` mapping into a symlinked node_modules leads back into the workspace (isSourceFileFromExternalLibrary), nor
    // a referenced project's)
    if (file->from_reference) return false;
    // (A root file is at depth 0 wherever it is: tsgo's lowestDepth)
    if (file->is_root) return true;
    if (file->found_searching_node_modules) return false;
    return !(file->loaded_by_import && strstr(cstr_str(&file->canonical_path), "node_modules/"));
}

// What JavaScript declaration emit reports (the rest of the walk is TypeScript's only so far): a CommonJS module
// assigning `module.exports` more than once (tsgo's transformSourceFile)
static void js_declaration_diagnostics_check(SourceFile const* file, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    if ((!options->declaration && !options->composite) || !file->is_commonjs || file->is_stdlib || file->is_config) return;
    if (file->loaded_by_import && strstr(cstr_str(&file->canonical_path), "node_modules/")) return;
    Symbol const* module = file->scope ? file->scope->container : nullptr;
    Symbol const* export_equals = module && module->exports ? symbol_table_get(module->exports, c_sv("export=")) : nullptr;
    for (u32 i = 0; export_equals && export_equals->decl_count > 1 && i < export_equals->decl_count; i++) {
        error_ctx_error_d(errs, export_equals->decls[i].full, 6424, "Multiple 'module.exports' assignments cannot be serialized for declaration emit.");
    }
}

// The CommonJS exports of a JavaScript file that tsgo's declaration transformer writes with an inferred type: `module.exports
// = x` of a variable of the file (`export = x` with x's declaration), `Object.defineProperty(exports, "n", { value: e })`
// (a variable "n" of e's type). What those types name and nothing can (TS4023...). The rest of JavaScript declaration
// emit (functions, classes, `exports.x = e`, JSDoc types) isn't walked: plan 64 stage 6
static void check_js_commonjs_exports(Walk* me) {
    for_each (i, vec_statement, &my file->ast.statements) {
        if (i->tag != STATEMENT_EXPR) continue;
        Expr const* expr = &i->expr;
        if (expr->tag == EXPR_BINARY_OP && expr->binary_op->op == BINARY_OP_EQUAL && expr_is_module_exports(&expr->binary_op->left)) {
            Expr const* value = &expr->binary_op->right;
            if (value->tag != EXPR_IDENTIFIER || expr_is_perenthisized(value)) continue;
            Symbol const* symbol = value->identifier.target;
            if (!symbol || symbol->decl_count != 1 || symbol->decls[0].kind != SYMBOL_DECL_VARIABLE) continue;
            Binding const* binding = symbol->decls[0].node;
            if (binding->parent || binding->tag != BINDING_IDENTIFIER || span_source(binding->content_span) != my file ||
                symbol_decl_jsdoc_type(&symbol->decls[0])
            ) {
                continue;
            }
            check_variable(me, (InitializedBinding const*)binding, symbol->is_const);
            continue;
        }
        // `exports.x = e` (not a function or class, which are declarations of their own kinds): a variable x of e's
        // type, reported at the assignment
        if (expr->tag == EXPR_BINARY_OP && expr->binary_op->op == BINARY_OP_EQUAL && expr->binary_op->left.tag == EXPR_PROPERTY &&
            expr->binary_op->left.property->form == PROPERTY_EXPR_DOT && expr_is_exports_object(&expr->binary_op->left.property->expr)
        ) {
            Expr const* value = &expr->binary_op->right;
            if (value->tag == EXPR_FUNCTION || value->tag == EXPR_CLASS_DECL || value->tag == EXPR_BINARY_OP) continue;
            Span name = expr_static_property_name(&expr->binary_op->left);
            if (!span_valid(name)) continue;
            Span whole = span_make_in(expr_outer_span(expr), span_start_ptr(expr_outer_span(&expr->binary_op->left)), span_end_ptr(expr_outer_span(value)));
            Emitting saved = emit_enter(me, EMIT_VARIABLE, SIG_FUNCTION, name, whole);
            my emitting.name_not_node = true;
            Ty const* type = lazy_type_get((LazyType*)&value->lazy_type);
            serialize_declaration(me, pseudo_of_expression(my checker, value, (PseudoPlace){0}), type ? ty_widened_literal(my checker, type) : nullptr, false, false);
            emit_leave(me, saved);
            continue;
        }
        Span name;
        if (!expr_is_define_property_call(expr, &name) || !expr_is_exports_object(vec_expr_at(&expr->call->args, 0))) continue;
        Expr const* descriptor = vec_expr_at(&expr->call->args, 2);
        if (descriptor->tag != EXPR_OBJECT) continue;
        Expr const* value = nullptr;
        for_each (j, vec_object_expr_item, &descriptor->object) {
            if (j->tag == OBJECT_ITEM_PROPERTY && !j->computed_key && cstr_equals(&j->name_string, "value")) value = &j->value;
        }
        if (!value) continue;
        Span key = vec_expr_at(&expr->call->args, 1)->inner_span;
        Emitting saved = emit_enter(me, EMIT_VARIABLE, SIG_FUNCTION, key, key);
        Ty const* type = lazy_type_get((LazyType*)&value->lazy_type);
        serialize_declaration(me, pseudo_of_expression(my checker, value, (PseudoPlace){0}), type ? ty_widened_literal(my checker, type) : nullptr, false, false);
        emit_leave(me, saved);
    }
}

void declaration_diagnostics_check(SourceFile const* file, ErrorCtx* errs, bool emit) {
    // (TS6424 blocks the file's emit too)
    isize_t errors_before = vec_error_size(&errs->errors);
    if (file->is_js) js_declaration_diagnostics_check(file, errs);
    if (!declaration_diagnostics_wanted(file)) return;
    // (A JavaScript file's are its declaration emit's like a TypeScript file's: the walk is the same, plan 64 stage 6.
    // A CommonJS file's exports are checked first, outside isolatedDeclarations)
    bool js_exports = file->is_js && file->is_commonjs && !compiler_options()->isolated_declarations;
    ErrorCtx js_scratch = { .phase = errs->phase, .parent = errs->parent };
    Walk walk = {
        .file = file,
        .errs = errs,
        .checker = current_checker,
        .strict_null_checks = compiler_options()->strict_null_checks,
        .isolated = compiler_options()->isolated_declarations,
        .scope = file->scope,
    };
    walk.tracker = (DeclTracker){
        .track_symbol = track_symbol, .track_computed_name = track_computed_name, .report = tracker_report,
        .track_module_file = track_module_file, .report_fallback = tracker_report_fallback, .ctx = &walk,
    };
    symbol_chain_set_painted((Symbol const* const* const*)&walk.painted, &walk.painted_count);
    symbol_chain_set_module_specifier(walk_module_specifier, &walk);
    ErrorCtx* reported = errs;
    if (js_exports) {
        collect_commonjs(&walk);
        check_js_commonjs_exports(&walk);
        // (check_js_commonjs_exports() reported what the exports' declarations do: their serialization again only
        // paints)
        walk.errs = &js_scratch;
        check_commonjs_exports(&walk);
        walk.errs = errs;
    }
    if (file->is_module) {
        u32 cap = 0;
        collect_exported_locals(&walk, &file->ast.statements, &cap);
    }
    Container top = { .global = !file->is_module, .module_top_level = file->is_module };
    check_statements(&walk, &file->ast.statements, top);
    // What the walk painted, and what that paints (transformAndReplaceLatePaintedStatements)
    for (u32 walked = 0; walked < walk.painted_count;) {
        u32 count = walk.painted_count;
        walk.late_pass = true;
        walk.late_taken_count = 0;
        check_statements(&walk, &file->ast.statements, top);
        walk.late_pass = false;
        // (one no statement declares has nothing to walk)
        for (u32 i = 0; i < count; i++) walk.painted_done[i] = true;
        walked = count;
    }
    // The second walk writes the file, unless the first reported something about it (tsc doesn't write a declaration
    // file whose transform has diagnostics)
    if (emit && vec_error_size(&reported->errors) == errors_before) {
        ErrorCtx scratch = { .phase = errs->phase, .parent = errs->parent };
        cstr* output = c_new(cstr, cstr_init());
        walk.errs = &scratch;
        walk.out = output;
        walk.map = compiler_options()->declaration_map && cstr_size(&file->declaration_output_path);
        emit_file(&walk, top);
        SourceFile* writable = (SourceFile*)file;
        if (writable->declaration_map) {
            cstr_drop(writable->declaration_map);
            free(writable->declaration_map);
            writable->declaration_map = nullptr;
        }
        if (walk.map) {
            cstr mappings = decl_map_take(output, file);
            cstr source_root = cstr_clone(compiler_options()->source_root);
            if (cstr_size(&source_root) && !cstr_ends_with(&source_root, "/")) cstr_append(&source_root, "/");
            writable->declaration_map = c_new(cstr, decl_map_json(basename_of(cstr_sv(&file->declaration_output_path)),
                cstr_sv(&source_root), cstr_sv(&file->declaration_map_source), cstr_sv(&mappings)));
            // (The trailer, on a line of its own: tsc's printSourceFile)
            if (decl_map_last_char(cstr_sv(output)) && decl_map_last_char(cstr_sv(output)) != '\n') cstr_append(output, "\n");
            cstr_append(output, "//# sourceMappingURL=");
            cstr_append_sv(output, cstr_sv(&file->declaration_map_url));
            c_drop(cstr, &mappings, &source_root);
        }
        if (compiler_options()->emit_bom) cstr_insert(output, 0, "\xEF\xBB\xBF");
        walk.out = nullptr;
        if (compiler_options()->new_line == 1) {
            cstr crlf = cstr_with_capacity(cstr_size(output) + cstr_size(output) / 16);
            for (csview rest = cstr_sv(output); rest.size;) {
                isize_t line = csview_find_sv(rest, c_sv("\n"));
                if (line == c_NPOS) {
                    cstr_append_sv(&crlf, rest);
                    break;
                }
                cstr_append_sv(&crlf, csview_slice(rest, 0, line));
                cstr_append_sv(&crlf, c_sv("\r\n"));
                rest = csview_slice(rest, line + 1, rest.size);
            }
            cstr_take(output, crlf);
        }
        walk.errs = reported;
        error_ctx_drop(&scratch);
        if (writable->declaration_output) {
            cstr_drop(writable->declaration_output);
            free(writable->declaration_output);
        }
        writable->declaration_output = output;
    }
    free(walk.export_offsets);
    free(walk.scope_params);
    symbol_chain_set_painted(nullptr, nullptr);
    symbol_chain_set_module_specifier(nullptr, nullptr);
    Checker* checker = walk.checker;
    for (u32 i = 0; i < walk.painted_count; i++) {
        if (checker->painted_kept_count == checker->painted_kept_cap) {
            checker->painted_kept_cap = checker->painted_kept_cap ? checker->painted_kept_cap * 2 : 8;
            checker->painted_kept = realloc(checker->painted_kept, checker->painted_kept_cap * sizeof(Symbol const*));
        }
        checker->painted_kept[checker->painted_kept_count++] = walk.painted[i];
    }
    free(walk.exported_locals);
    free(walk.painted);
    ty_printed_types_clear(&walk.printed_types);
    ty_printed_types_clear(&walk.class_printed_types);
    free(walk.painted_done);
    free(walk.late_taken);
    for (u32 i = 0; i < walk.specifier_count; i++) c_drop(cstr, &walk.specifiers[i].plain, &walk.specifiers[i].unsafe);
    free(walk.specifiers);
    free(walk.cjs_members);
    cstr_drop(&walk.cjs_names);
    error_ctx_drop(&js_scratch);
}
