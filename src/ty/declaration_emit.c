#include "ty/declaration_emit.h"
#include "ty/decl_comments.h"
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
    u32 painted_count, painted_cap;
    bool* painted_done;
    bool late_pass; // Walking the statements again for painted ones only
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
} Walk;

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
    my emitting = (Emitting){
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
    ty_display(my checker, (TyDisplay){
        .target = &text, .types_baseline = true, .enclosing = my scope, .tracker = &my tracker, .at = my emitting.name,
        .suppress_fallback = !reports, .in_object_literal = my in_object_literal,
    }, type);
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
    if (!symbol || symbol->exported || (symbol->flags & SYMBOL_TYPE_PARAMETER) || symbol->is_parameter) return;
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
    CompilerOptions const* o = compiler_options();
    bool node_next = o->module_resolution == MODULE_RESOLUTION_NODE16 || o->module_resolution == MODULE_RESOLUTION_NODENEXT;
    Program* program = my checker->program;
    bool file_esm = my file->has_format && my file->is_esm;
    SpecifierMode override = SPECIFIER_MODE_NONE;
    // (An import type of an ES module from another format says so)
    if (node_next && target->has_format && target->is_esm && !file_esm) override = SPECIFIER_MODE_ESM;
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
        Statement const* ret = vec_statement_at(&fn->body.statements, 0);
        return ret && ret->tag == STATEMENT_RETURN_EXPR && search_expr(me, &ret->expr, near, false);
    }
    // (A `return` directly in the body: the function is the nearest declaration of what it returns)
    for_each (i, vec_statement, &fn->body.statements) {
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
    Signature const* sig = ty_function_signature(my checker, fn);
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

static bool statement_visible_at_first(Walk* me, Statement const* statement, Container container, Symbol const* symbol) {
    if (container.global || container.ambient) return true;
    if (statement_declares_name(statement->tag) && statement->decl.props.exported != DECL_EXPORTED_NO) return true;
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
        bool outer = fallback_begin(me);
        my fell_back = true;
        fallback_end(me, outer, ty_type_of_symbol(my checker, member));
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

// tsc's isImplementationOfOverload for a function declaration
static bool is_overload_implementation(FunctionImpl const* fn) {
    if (!fn->has_body || !fn->symbol) return false;
    for (u32 i = 0; i < fn->symbol->decl_count; i++) {
        SymbolDecl const* decl = &fn->symbol->decls[i];
        if (decl->kind == SYMBOL_DECL_FUNCTION && decl->node != fn && !((FunctionImpl const*)decl->node)->has_body) return true;
    }
    return false;
}

// shouldEmitFunctionProperties: the implementation, or the last overload of an ambient function
static bool emits_function_properties(FunctionImpl const* fn) {
    if (fn->has_body || !fn->symbol) return true;
    FunctionImpl const* last = nullptr;
    for (u32 i = 0; i < fn->symbol->decl_count; i++) {
        SymbolDecl const* decl = &fn->symbol->decls[i];
        if (decl->kind == SYMBOL_DECL_FUNCTION && !((FunctionImpl const*)decl->node)->has_body) last = decl->node;
    }
    return !last || last == fn;
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
    ClassItem const* first = item->method.accessor_first ? item->method.accessor_first : item;
    ClassItem const* getter = item->method.accessor == CLASS_ACCESSOR_GET ? item : item->method.accessor_getter;
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
        if (i->method.accessor_getter == getter) {
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
// getSignaturesOfSymbol has both), declaration emit leaves it out
static bool interface_declares_method(ClassDecl const* cls, ClassItem const* item) {
    Symbol const* symbol = cls->symbol;
    if (!symbol || item->method.computed_key || item->method.is_static) return false;
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

static bool is_class_overload_implementation(ClassDecl const* cls, FunctionImpl const* fn) {
    if (!fn->has_body) return false;
    for_each (i, vec_class_item, &cls->items) {
        FunctionImpl const* other = i->tag == CLASS_ITEM_METHOD ? &i->method.func :
            i->tag == CLASS_ITEM_CONSTRUCTOR ? &i->constructor.func : nullptr;
        if (other && other != fn && function_impl_rare(other)->overload_implementation == fn) return true;
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
        Expr const* key = i->tag == CLASS_ITEM_METHOD && i->method.accessor == CLASS_ACCESSOR_NONE ? i->method.computed_key :
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

static void check_class(Walk* me, ClassDecl const* cls, Span statement) {
    bool outer_static = my member_static, outer_in_class = my member_in_class;
    my member_in_class = true;
    check_late_indexes(me, cls);
    for_each (i, vec_class_item, &cls->items) {
        if (is_internal_declaration(i->span)) continue;
        my member_static = class_item_is_static((ClassItem*)i);
        Expr const* key = i->tag == CLASS_ITEM_METHOD ? i->method.computed_key :
            i->tag == CLASS_ITEM_FIELD ? i->field.computed_key : nullptr;
        // A late bound name's entity (checkName): what it names is emitted
        if (is_dynamic_name(key) && pseudo_is_entity_name(key) && !item_has_private_name(i)) {
            Ty const* key_type = lazy_type_get((LazyType*)&key->lazy_type);
            if (key_type && (key_type->flags & (TYF_LITERAL | TYF_UNIQUE_ES_SYMBOL))) {
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
                continue;
            }
        }
        switch (i->tag) {
            case CLASS_ITEM_CONSTRUCTOR: {
                FunctionImpl const* ctor = &i->constructor.func;
                if (is_class_overload_implementation(cls, ctor)) break;
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
                    if (i->access == ACCESS_MODIFIER_PRIVATE || is_class_overload_implementation(cls, fn) ||
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
        if (!vec_symbol_cptr_size(identifier_type_namespace_symbols(type))) return;
        check_entity_name(me, *vec_symbol_cptr_at(identifier_type_namespace_symbols(type), 0), *vec_span_at(identifier_type_namespaces(type), 0),
            SYMBOL_NAMESPACE);
        return;
    }
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
            bool painted = i->binding.tag == BINDING_IDENTIFIER && !is_exported_local(me, i->binding.symbol) &&
                take_painted(me, i->binding.symbol);
            if (painted) check_variable(me, i, variable_statement_is_const(&statement->decl.variable));
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
        if (take_painted(me, item->symbol) && vec_symbol_cptr_size(&item->alias_path_symbols)) {
            paint(me, *vec_symbol_cptr_at(&item->alias_path_symbols, 0));
        }
        return;
    }
    if (!statement_declares_name(statement->tag)) return;
    Symbol const* symbol = statement_declared_symbol(statement);
    if (!is_painted_pending(me, symbol)) return;
    my late_pass = false;
    take_painted(me, symbol);
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
// it), so declaration emit keeps `import "m"` even when nothing it imports is used
static bool import_required_by_augmentation(Walk* me, SourceFile const* target) {
    Symbol const* module = my file->scope ? my file->scope->container : nullptr;
    SymbolTable const* exports = module ? module->exports : nullptr;
    for (u32 i = 0; exports && i < exports->count; i++) {
        Symbol const* symbol = exports->entries[i].symbol;
        for (u32 j = 0; symbol && j < symbol->decl_count; j++) {
            if (span_source(symbol->decls[j].name) == target) return true;
        }
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
            if (item->tag == PORT_IMPORT_ALIAS && item->export_import && vec_symbol_cptr_size(&item->alias_path_symbols)) {
                paint(me, *vec_symbol_cptr_at(&item->alias_path_symbols, 0));
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
            if (decl->jsdoc || decl->jsdoc_check_only) return;
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
            Root outer_root = my root;
            my root = (Root){ .kind = ROOT_EXPORT_DEFAULT, .expr = expr, .at = statement->span };
            // (The `default` symbol's type: the expression's, widened)
            Ty const* type = lazy_type_get((LazyType*)&expr->lazy_type);
            type = ty_widened_literal(my checker, ty_widened_nullable(my checker, ty_widened_objects(my checker, ty_regular(my checker, type))));
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

// What `export { x }` and `export default x` name in a module
static void collect_exported_locals(Walk* me, VecStatement const* statements) {
    u32 cap = 0;
    for_each (i, vec_statement, statements) {
        Symbol const* found[64];
        u32 count = 0;
        if (i->tag == STATEMENT_IMPORT_EXPORT) {
            PortStatement const* port = i->import_export;
            if (port->module_ref) continue;
            for_each (j, vec_port_item, &port->items) {
                if (j->tag == PORT_EXPORT_SPECIFIC && j->source_symbol && count < c_arraylen(found)) found[count++] = j->source_symbol;
            }
        } else if (i->tag == STATEMENT_EXPORT_DEFAULT_EXPR && i->expr.tag == EXPR_IDENTIFIER && i->expr.identifier.symbol) {
            found[count++] = i->expr.identifier.symbol;
        }
        for (u32 j = 0; j < count; j++) {
            if (my exported_local_count == cap) {
                cap = cap ? cap * 2 : 8;
                my exported_locals = realloc(my exported_locals, cap * sizeof(Symbol const*));
            }
            my exported_locals[my exported_local_count++] = found[j];
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
        .target = target, .types_baseline = true, .enclosing = my scope, .tracker = &my tracker, .at = at,
        .multiline = true, .indent = my indent, .comments = &my comments,
        .printed_params = my in_return ? &my return_params : nullptr,
    };
}

static void w(Walk* me, csview text) { cstr_append_sv(my out, text); }
static void emit_expando_namespace(Walk* me, Symbol const* host, csview name, csview modifiers);
#define w_lit(me, s) w(me, c_sv(s))

static bool w_at_line_start(Walk* me) { return !cstr_size(my out) || cstr_str(my out)[cstr_size(my out) - 1] == '\n'; }

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
    isize_t size = cstr_size(my out);
    bool line_break = size && cstr_str(my out)[size - 1] == '\n';
    if (line_break) cstr_erase(my out, size - 1, 1);
    decl_comments_trailing(&my comments, my out, end);
    if (line_break) w_lit(me, "\n");
    return true;
}

// (While a w_*() helper prints, what the tracker's callbacks serialize isn't written)
#define SINKS_OFF cstr* sinks_type = my type_out; cstr* sinks_pseudo = my pseudo_out; my type_out = my pseudo_out = nullptr
#define SINKS_ON my type_out = sinks_type; my pseudo_out = sinks_pseudo

static void w_node(Walk* me, TypeNode const* node) {
    SINKS_OFF;
    TyParamNames names = walk_param_names(me);
    TyDisplay d = emit_display(me, my out);
    d.param_names = &names;
    ty_display_node(my checker, d, node);
    ty_param_names_drop(&names);
    SINKS_ON;
}

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

// A top-level declaration's modifiers (tsc's ensureModifiers): its `export` and `default`, and `declare` at a file's top
// level unless it is a type or a default export (needsDeclare). In a namespace body, where each `export ` is written
// is remembered (see Walk.export_offsets)
static void w_statement_modifiers(Walk* me, Statement const* statement, Container container, bool always_type) {
    DeclExported exported = statement_declares_name(statement->tag) || statement->tag == STATEMENT_DECL_VARIABLE ?
        statement->decl.props.exported : DECL_EXPORTED_NO;
    bool is_file = container_is_file(container);
    if (exported != DECL_EXPORTED_NO) {
        if (!is_file) {
            if (my export_offset_count == my export_offset_cap) {
                my export_offset_cap = my export_offset_cap ? my export_offset_cap * 2 : 8;
                my export_offsets = realloc(my export_offsets, my export_offset_cap * sizeof(u32));
            }
            my export_offsets[my export_offset_count++] = (u32)cstr_size(my out);
        }
        w_lit(me, "export ");
        if (is_file) my has_module_indicator = true;
    } else {
        my needs_scope_fix = true;
    }
    if (exported == DECL_EXPORTED_YES_DEFAULT) {
        w_lit(me, "default ");
        return;
    }
    // (A type's `declare` is kept where it is written)
    if (is_file && (!always_type || span_valid(statement->decl.props.declare_keyword))) w_lit(me, "declare ");
}

// A parameter (ensureParameter): its name without initializers, `?` where it is optional, its type
static void w_parameter(Walk* me, FunctionImpl const* fn, FunctionArg const* arg, bool private_member) {
    Binding const* binding = &arg->initializer.binding;
    if (arg->rest) w_lit(me, "...");
    if (binding->tag == BINDING_ARRAY || binding->tag == BINDING_OBJECT) {
        cstr pattern = cstr_init();
        ty_append_binding_pattern(&pattern, binding);
        w(me, cstr_sv(&pattern));
        cstr_drop(&pattern);
    } else {
        w(me, span_sv(binding->content_span));
    }
    bool is_this = binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this");
    if (!is_this && is_optional_parameter(fn, arg)) w_lit(me, "?");
    if (private_member) return;
    w_lit(me, ": ");
    if (binding->has_explicit_type && (is_this || !requires_adding_undefined(me, fn, arg, true))) {
        w_node(me, binding_type(binding));
        return;
    }
    cstr text = cstr_init();
    cstr* outer = my type_out;
    my type_out = &text;
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
    decl_comments_trailing(&my comments, my out, span_end_ptr(arg->span));
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
        if (predicate->asserts) w_lit(me, "asserts ");
        w(me, span_sv(predicate->name));
        if (predicate->has_type) {
            w_lit(me, " is ");
            w_node(me, &predicate->type);
        }
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

static void w_variable_type(Walk* me, InitializedBinding const* decl, bool is_const) {
    Binding const* binding = &decl->binding;
    Expr const* init = initialized_binding_value(decl);
    if (is_const && init->tag != EXPR_IMPLICIT_ANY && is_literal_const(me, binding->symbol)) {
        w_lit(me, " = ");
        w_type(me, ty_type_of_symbol(my checker, binding->symbol));
        return;
    }
    w_lit(me, ": ");
    if (binding->has_explicit_type) {
        w_node(me, binding_type(binding));
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
            (visible || was_painted(me, only->binding.symbol)) && is_expando_variable(me, &only->binding, init)
        ) {
            FunctionImpl const* fn = init->function;
            w_indent(me);
            isize_t modifiers_start = (isize_t)cstr_size(my out);
            w_statement_modifiers(me, statement, container, false);
            cstr modifiers = cstr_from_sv(csview_slice(cstr_sv(my out), (isize_t)modifiers_start, cstr_size(my out)));
            w_lit(me, "function ");
            w(me, span_sv(only->binding.content_span));
            w_signature(me, fn, false);
            w_lit(me, ";\n");
            emit_expando_namespace(me, only->binding.symbol, span_sv(only->binding.content_span), cstr_sv(&modifiers));
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
        if (binding->tag != BINDING_IDENTIFIER && binding_has_initializers(binding)) {
            w_binding_elements(me, binding, container, visible, &written);
            continue;
        }
        if (binding->tag != BINDING_IDENTIFIER) {
            // (Without initializers in it, the pattern is written with its type; an empty one isn't)
            if (!visible) continue;
            if (!binding_declares_names(binding)) continue;
            if (written++) w_lit(me, ", ");
            cstr pattern = cstr_init();
            ty_append_binding_pattern(&pattern, binding);
            w(me, cstr_sv(&pattern));
            cstr_drop(&pattern);
            w_lit(me, ": ");
            if (binding->has_explicit_type) {
                w_node(me, binding_type(binding));
            } else {
                Expr const* init = initialized_binding_value(i);
                Ty const* type = init->tag != EXPR_IMPLICIT_ANY ? lazy_type_get((LazyType*)&init->lazy_type) : nullptr;
                w_type(me, type ? ty_widened_literal(my checker, ty_widened_objects(my checker, ty_regular(my checker, type))) :
                    ty_intrinsic(my checker, TY_ANY));
            }
            continue;
        }
        if (!visible && !was_painted(me, binding->symbol) && !(container.module_top_level && is_exported_local(me, binding->symbol))) {
            continue;
        }
        if (written++) w_lit(me, ", ");
        decl_comments_intervening(&my comments, my out, my indent, binding->content_span.buf);
        w_leading(me, binding->content_span.buf);
        if (cstr_size(my out)) w_indent(me); // (The list is written apart: empty, it is in the middle of a line)
        w(me, span_sv(binding->content_span));
        w_variable_type(me, i, is_const);
        // (The declaration ends after its initializer, else its type)
        Expr const* init = initialized_binding_value(i);
        Span end = init->tag != EXPR_IMPLICIT_ANY ? expr_outer_span(init) :
            binding->has_explicit_type ? binding_explicit_type_span(binding) : binding->content_span;
        if (span_valid(end)) decl_comments_trailing(&my comments, my out, span_end_ptr(end));
    }
    my out = outer;
    if (written) {
        w_indent(me);
        w_statement_modifiers(me, statement, container, false);
        bool const_like = variable->keyword == VAR_INIT_USING || variable->keyword == VAR_INIT_AWAIT_USING;
        w(me, const_like ? c_sv("const") : csview_from(variable_statement_keyword_str(variable)));
        w_lit(me, " ");
        w(me, cstr_sv(&declarations));
        w_lit(me, ";\n");
    }
    cstr_drop(&declarations);
}

// The printer's next generated name (tsc's makeTempVariableName): `_a` to `_z` but `_i` and `_n` (kept for loops), then
// `_0`, `_1`...
static void generated_name(Walk* me, char* out, size_t size) {
    u32 n;
    do {
        n = my generated_names++;
    } while (n == 8 || n == 13);
    if (n < 26) snprintf(out, size, "_%c", 'a' + n);
    else snprintf(out, size, "_%u", n - 26);
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

static void emit_expando_namespace(Walk* me, Symbol const* host, csview name, csview modifiers) {
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
                cstr_append_sv(&text, span_sv(expr_outer_span(value)));
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
            w_type(me, ty_widened_literal(my checker, ty_type_of_symbol(my checker, member)));
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
        w(me, name);
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
        w_lit(me, "declare function ");
        w(me, span_sv(fn->name));
        w_signature(me, fn, false);
        w_lit(me, ";\n");
        w_indent(me);
        w_lit(me, "export default ");
        w(me, span_sv(fn->name));
        w_lit(me, ";\n");
        emit_expando_namespace(me, symbol, span_sv(fn->name), c_sv("declare "));
        return;
    }
    w_indent(me);
    isize_t modifiers_start = (isize_t)cstr_size(my out);
    w_statement_modifiers(me, statement, container, false);
    cstr modifiers = cstr_from_sv(csview_slice(cstr_sv(my out), (isize_t)modifiers_start, cstr_size(my out)));
    w_lit(me, "function ");
    w(me, span_sv(fn->name));
    u32 scope_saved = enter_type_params(me, &fn->generic);
    w_signature(me, fn, false);
    leave_type_params(me, scope_saved);
    w_lit(me, ";\n");
    if (first && statement->decl.props.exported != DECL_EXPORTED_YES_DEFAULT) {
        emit_expando_namespace(me, symbol, span_sv(fn->name), cstr_sv(&modifiers));
    }
    cstr_drop(&modifiers);
}

// A class member's modifiers in tsc's order (createModifiersFromModifierFlags; `public`, `async` and `override` aren't
// written, nor `declare`)
static void w_member_modifiers(Walk* me, AccessModifier access, bool is_abstract, bool is_static, bool readonly, bool accessor) {
    if (access == ACCESS_MODIFIER_PRIVATE) w_lit(me, "private ");
    if (access == ACCESS_MODIFIER_PROTECTED) w_lit(me, "protected ");
    if (is_abstract) w_lit(me, "abstract ");
    if (is_static) w_lit(me, "static ");
    if (readonly) w_lit(me, "readonly ");
    if (accessor) w_lit(me, "accessor ");
}

static csview item_name(ClassItem const* item) {
    return span_sv(item->tag == CLASS_ITEM_METHOD ? item->method.name_span : item->field.name_span);
}

static void emit_field(Walk* me, ClassItem const* item) {
    w_indent(me);
    w_member_modifiers(me, item->access, item->field.is_abstract, item->field.is_static, item->field.is_readonly,
        item->field.is_auto_accessor);
    w(me, item_name(item));
    if (item->field.is_optional) w_lit(me, "?");
    if (item->access == ACCESS_MODIFIER_PRIVATE) {
        w_lit(me, ";\n");
        return;
    }
    Expr const* init = &item->field.value;
    if (item->field.is_readonly && init->tag != EXPR_IMPLICIT_ANY && is_literal_const_field(item) && !item->field.has_explicit_type) {
        w_lit(me, " = ");
        w_type(me, lazy_type_get((LazyType*)&item->field.lazy_type));
        w_lit(me, ";\n");
        return;
    }
    w_lit(me, ": ");
    Ty const* field_type = lazy_type_get((LazyType*)&item->field.lazy_type);
    Symbol const* field_symbol = field_type && (field_type->flags & TYF_UNIQUE_ES_SYMBOL) ? field_type->literal.unique->owner : nullptr;
    bool own_unique = field_symbol && span_eq(field_symbol->decl_count ? field_symbol->decls[0].name : span_null, item->field.name_span);
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
    w_lit(me, ";\n");
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
    w_member_modifiers(me, arg->access, false, false, arg->is_readonly_property, false);
    w(me, span_sv(binding->content_span));
    if (arg->explicit_optional) w_lit(me, "?");
    if (arg->access == ACCESS_MODIFIER_PRIVATE) {
        w_lit(me, ";\n");
        return;
    }
    w_lit(me, ": ");
    if (binding->has_explicit_type && !requires_adding_undefined(me, ctor, arg, false)) {
        w_node(me, binding_type(binding));
    } else {
        cstr text = cstr_init();
        cstr* outer = my type_out;
        my type_out = &text;
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
                if (m->item && m->item->tag == CLASS_ITEM_METHOD) {
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

// `statement` null: a class expression written as a declaration named `name` (an export default's)
static void emit_class(Walk* me, ClassDecl const* cls, Statement const* statement, Container container, csview name) {
    // A base that isn't a name is a variable of its type, `C_base` (transformClassDeclaration)
    Expr const* base = &cls->extends;
    Expr const* base_name = base->tag == EXPR_GENERIC_PARAMS ? &base->generic_params->expr : base;
    bool base_variable = base->tag != EXPR_IMPLICIT_ANY && !pseudo_is_entity_name(base_name) &&
        !(base_name->tag == EXPR_LITERAL && base_name->literal.tag == VALUE_NULL);
    cstr base_variable_name = cstr_init();
    if (base_variable) {
        cstr_take(&base_variable_name, cstr_d(span_valid(cls->name) ? span_sv(cls->name) : c_sv("default"), "_base"));
        w_indent(me);
        if (container_is_file(container)) w_lit(me, "declare ");
        my needs_scope_fix = true;
        w_lit(me, "const ");
        w(me, cstr_sv(&base_variable_name));
        w_lit(me, ": ");
        w_type(me, lazy_type_get((LazyType*)&base_name->lazy_type));
        w_lit(me, ";\n");
    }
    w_indent(me);
    if (statement) w_statement_modifiers(me, statement, container, false);
    else if (container_is_file(container)) w_lit(me, "declare ");
    if (cls->is_abstract) w_lit(me, "abstract ");
    w_lit(me, "class");
    if (name.size) {
        w_lit(me, " ");
        w(me, name);
    } else if (span_valid(cls->name)) {
        w_lit(me, " ");
        w(me, span_sv(cls->name));
    }
    w_type_params(me, &cls->generic);
    if (base->tag != EXPR_IMPLICIT_ANY) {
        w_lit(me, " extends ");
        w(me, base_variable ? cstr_sv(&base_variable_name) : span_sv(expr_outer_span(base_name)));
        if (base->tag == EXPR_GENERIC_PARAMS) {
            w_lit(me, "<");
            for_each (i, vec_spanned_type, &base->generic_params->params) {
                if (i_index) w_lit(me, ", ");
                w_node(me, &i->type);
            }
            w_lit(me, ">");
        }
    }
    for_each (i, vec_spanned_type, &cls->implements) {
        w(me, i_index ? c_sv(", ") : c_sv(" implements "));
        w_node(me, &i->type);
    }
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
            if (is_parameter_property(j)) emit_parameter_property(me, &i->constructor.func, j);
        }
    }
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
        switch (i->tag) {
            case CLASS_ITEM_CONSTRUCTOR: {
                FunctionImpl const* ctor = &i->constructor.func;
                if (is_class_overload_implementation(cls, ctor)) break;
                w_indent(me);
                w_member_modifiers(me, i->access, false, false, false, false);
                w_lit(me, "constructor");
                w_parameters(me, ctor, i->access == ACCESS_MODIFIER_PRIVATE);
                w_lit(me, ";\n");
                break;
            }
            case CLASS_ITEM_METHOD: {
                if (item_has_private_name(i)) break;
                FunctionImpl const* fn = &i->method.func;
                bool private_member = i->access == ACCESS_MODIFIER_PRIVATE;
                if (i->method.accessor == CLASS_ACCESSOR_NONE) {
                    if (is_class_overload_implementation(cls, fn) || interface_declares_method(cls, i)) break;
                    // (A private method is a property without a type: omitPrivateMethodType, once per name)
                    if (private_member && i->symbol_first && i->symbol_first != i) break;
                    w_indent(me);
                    w_member_modifiers(me, i->access, i->method.is_abstract, i->method.is_static, false, false);
                    w(me, item_name(i));
                    if (i->method.is_optional) w_lit(me, "?");
                    u32 method_scope_saved = enter_type_params(me, &fn->generic);
                    if (!private_member) w_signature(me, fn, false);
                    leave_type_params(me, method_scope_saved);
                    w_lit(me, ";\n");
                    break;
                }
                bool getter = i->method.accessor == CLASS_ACCESSOR_GET;
                w_indent(me);
                w_member_modifiers(me, i->access, i->method.is_abstract, i->method.is_static, false, false);
                w(me, getter ? c_sv("get ") : c_sv("set "));
                w(me, item_name(i));
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
                w_lit(me, ";\n");
                break;
            }
            case CLASS_ITEM_FIELD:
                if (item_has_private_name(i)) break;
                emit_field(me, i);
                break;
            case CLASS_ITEM_INTERFACE_MEMBER: {
                // An index signature, as written
                InterfaceMember const* member = &i->interface_memeber;
                if (member->key.tag != INTERFACE_KEY_INDEX) break;
                w_indent(me);
                if (i->interface_member_static) w_lit(me, "static ");
                if (member->readonly) w_lit(me, "readonly ");
                w_lit(me, "[");
                w(me, span_sv(member->key.index->name));
                w_lit(me, ": ");
                w_node(me, &member->key.index->type);
                w_lit(me, "]: ");
                w_node(me, &member->type);
                w_lit(me, ";\n");
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
    w_lit(me, "}\n");
    cstr_drop(&base_variable_name);
}

// A type literal's members, one per line, braces and all (an interface's body: `{` and `}` on lines of their own even
// when empty)
static void w_members(Walk* me, TypeNode const* type) {
    bool empty = type->tag != TYPE_INTERFACE || !vec_interface_member_size(&type->interface.members);
    if (!empty) {
        isize_t start = cstr_size(my out);
        w_node(me, type);
        // (Every member left out)
        if (cstr_size(my out) - start != 2 || cstr_str(my out)[start] != '{') return;
        cstr_erase(my out, start, 2);
    }
    w_lit(me, "{\n");
    w_indent(me);
    w_lit(me, "}");
}

static void emit_interface(Walk* me, InterfaceDecl const* decl, Statement const* statement, Container container) {
    w_indent(me);
    w_statement_modifiers(me, statement, container, true);
    w_lit(me, "interface ");
    w(me, span_sv(decl->name));
    w_type_params(me, &decl->generic);
    for_each (i, vec_spanned_type, &decl->extends) {
        w(me, i_index ? c_sv(", ") : c_sv(" extends "));
        w_node(me, &i->type);
    }
    w_lit(me, " ");
    w_members(me, &decl->type);
    w_lit(me, "\n");
}

static void emit_type_alias(Walk* me, TypeDecl const* decl, Statement const* statement, Container container) {
    w_indent(me);
    w_statement_modifiers(me, statement, container, true);
    w_lit(me, "type ");
    w(me, span_sv(decl->name));
    w_type_params(me, &decl->generic);
    w_lit(me, " = ");
    w_node(me, &decl->type);
    w_lit(me, ";\n");
}

static void emit_enum(Walk* me, EnumDecl const* decl, Statement const* statement, Container container) {
    w_indent(me);
    w_statement_modifiers(me, statement, container, false);
    if (decl->is_const) w_lit(me, "const ");
    w_lit(me, "enum ");
    w(me, span_sv(decl->name));
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
        w(me, span_sv(i->full_name));
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
            w_type(me, value);
        }
        decl_comments_trailing(&my comments, my out, span_end_ptr(i->value.tag == EXPR_IMPLICIT_ANY ? i->full_name : expr_outer_span(&i->value)));
    }
    if (written) w_line(me);
    my indent--;
    w_indent(me);
    w_lit(me, "}\n");
}

static void emit_statements(Walk* me, VecStatement const* statements, Container container);

static void emit_namespace(Walk* me, NamespaceDecl const* decl, Statement const* statement, Container container) {
    w_indent(me);
    bool augmentation = decl->is_augmentation || decl->is_global;
    if (augmentation) {
        if (container_is_file(container)) w_lit(me, "declare ");
    } else {
        w_statement_modifiers(me, statement, container, false);
    }
    if (decl->is_global) {
        w_lit(me, "global");
    } else {
        w(me, decl->is_ambient_module ? c_sv("module ") : c_sv("namespace "));
        w(me, span_sv(decl->name));
    }
    if (decl->is_shorthand) {
        w_lit(me, ";\n");
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
        w(me, span_sv(inner->name));
        body = inner;
    }
    w_lit(me, " {\n");
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
        } else {
            // (Every statement is exported: the modifiers are left out, from the last one back)
            for (u32 i = my export_offset_count; i-- > outer_offsets;) {
                if (my export_offsets[i] < body_start) continue;
                cstr_erase(my out, my export_offsets[i], 7);
            }
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
        cstr_erase(my out, body_start - 3, 3);
        w_lit(me, " { }\n");
        return;
    }
    w_indent(me);
    w_lit(me, "}\n");
}

// ` from "m" with { ... }`: the specifier and attributes as written
// (The printer's ImportAttributes list, PreserveLines: the first attribute on the brace's line, the others and the
// closing brace on a line of their own where they were, an attribute as `name: value`)
static void w_specifier(Walk* me, PortStatement const* port) {
    w(me, span_sv(port->target_path_span));
    ImportAttributes const* attributes = port->attributes;
    if (!attributes) return;
    w_lit(me, " ");
    if (!vec_import_attribute_size(&attributes->elements)) {
        w(me, span_sv(attributes->span));
        return;
    }
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
        w(me, span_sv(i->name));
        w_lit(me, ": ");
        w(me, span_sv(expr_outer_span(&i->value)));
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
}

// `{ a, type b as c }`: an import or export declaration's specifiers (`kept` the ones written, null for all), with their
// comments, an export's trailing comma as written (the printer's NamedImportsOrExportsElements)
static void w_port_items(Walk* me, PortStatement const* port, bool const* kept, bool trailing_comma) {
    u32 written = 0;
    PortItem const* last = nullptr;
    for_each (i, vec_port_item, &port->items) {
        if (kept && !kept[i_index]) continue;
        w(me, written++ ? c_sv(", ") : c_sv("{ "));
        char const* start = span_valid(i->full_span) ? i->full_span.buf : nullptr;
        if (start) {
            decl_comments_intervening(&my comments, my out, my indent, start);
            w_leading(me, start);
            w_indent(me);
        }
        if (i->type_modifier) w_lit(me, "type ");
        w(me, span_sv(i->source_name));
        if (!span_eq(i->source_name, i->decl_name) && span_valid(i->decl_name)) {
            w_lit(me, " as ");
            w(me, span_sv(i->decl_name));
        }
        if (start) decl_comments_trailing(&my comments, my out, span_end_ptr(i->full_span));
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
}

// An import or export declaration (transformImportDeclaration: an import keeps the names emitted declarations use)
// (An import's name is kept where an emitted declaration names it, or `export { x }` does)
#define KEPT(symbol) (was_painted(me, symbol) || (container.module_top_level && is_exported_local(me, symbol)))

static void emit_port(Walk* me, Statement const* statement, Container container) {
    PortStatement const* port = statement->import_export;
    if (port->jsdoc) return;
    bool is_file = container_is_file(container);
    csview specifier = span_sv(port->target_path_span);
    if (span_equals(port->keyword_span, "export") && !port->main.export_import) {
        if (!vec_port_item_size(&port->items) && port->main.tag == PORT_NOOP && !port->module_ref) {
            // `export {}`
            w_indent(me);
            w_lit(me, "export {};\n");
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
        w_lit(me, "export ");
        if (port->main.type_only) w_lit(me, "type ");
        if (port->main.tag == PORT_REEXPORT_SPLAT) {
            w_lit(me, "*");
        } else if (port->main.tag == PORT_REEXPORT_GATHER) {
            w_lit(me, "* as ");
            w(me, span_sv(port->main.decl_name));
        } else {
            w_port_items(me, port, nullptr, true);
        }
        if (specifier.size) {
            w_lit(me, " from ");
            w_specifier(me, port);
        }
        w_lit(me, ";\n");
        return;
    }
    // Imports
    if (port->side_effect_only) {
        w_indent(me);
        w_lit(me, "import ");
        w_specifier(me, port);
        w_lit(me, ";\n");
        if (is_file) my has_module_indicator = true;
        return;
    }
    if (port->main.tag == PORT_IMPORT_ALIAS || port->is_import_require) {
        PortItem const* item = &port->main;
        if (!item->export_import && !KEPT(item->symbol)) return;
        w_indent(me);
        if (item->export_import) w_lit(me, "export ");
        w_lit(me, "import ");
        if (item->type_only) w_lit(me, "type ");
        w(me, span_sv(item->decl_name));
        w_lit(me, " = ");
        if (port->is_import_require) {
            w_lit(me, "require(");
            w_specifier(me, port);
            w_lit(me, ")");
            if (is_file) my has_module_indicator = true;
        } else {
            for_each (i, vec_span, &item->alias_path) {
                if (i_index) w_lit(me, ".");
                w(me, span_sv(*i));
            }
        }
        w_lit(me, ";\n");
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
        if (port->module_ref && port->module_ref->file && port->module_ref->file != my file &&
            import_required_by_augmentation(me, port->module_ref->file)
        ) {
            w_indent(me);
            w_lit(me, "import ");
            w_specifier(me, port);
            w_lit(me, ";\n");
            if (is_file) my has_module_indicator = true;
        }
        return;
    }
    if (is_file) my has_module_indicator = true;
    w_indent(me);
    w_lit(me, "import ");
    if (port->main.type_only) w_lit(me, "type ");
    if (port->source_phase) w_lit(me, "source ");
    if (default_kept) {
        w(me, span_sv(port->main.decl_name));
        if (gather_kept || named) w_lit(me, ", ");
    }
    if (gather_kept) {
        w_lit(me, "* as ");
        w(me, span_sv(gather->decl_name));
    } else if (named) {
        bool* kept = malloc(sizeof(bool) * (size_t)vec_port_item_size(&port->items));
        for_each (i, vec_port_item, &port->items) kept[i_index] = KEPT(i->symbol);
        w_port_items(me, port, kept, false);
        free(kept);
    }
    w_lit(me, " from ");
    w_specifier(me, port);
    w_lit(me, ";\n");
}

// The name an export default's declaration gets: the function or class expression's own, else `_default` (tsc's
// getNameOfExportedAssignedExpression), `_1`... where that is taken in the scope
static cstr export_default_name(Walk* me, Span own) {
    csview base = span_valid(own) && !span_equals(own, "default") ? span_sv(own) : c_sv("_default");
    cstr name = cstr_from_sv(base);
    for (u32 n = 1; my scope && scope_resolve(my scope, cstr_sv(&name), SYMBOL_VALUE | SYMBOL_TYPE | SYMBOL_NAMESPACE); n++) {
        cstr_take(&name, cstr_d(base, "_", n));
    }
    return name;
}

// `export default <expression>` (tsc's transformExportAssignment): a name as written, a function or class expression as a
// declaration after the export (the statement's comments on it), anything else as `declare const _default: T;` before
// it (the comments on that)
static void emit_export_default(Walk* me, Statement const* statement, Container container) {
    Expr const* expr = &statement->expr;
    bool is_file = container_is_file(container);
    if (is_file) my has_module_indicator = true;
    my has_scope_marker = true;
    char const* comments_from = statement->span.buf;
    char const* comments_end = span_end_ptr(statement->span);
    csview keyword = statement->export_equals ? c_sv("export = ") : c_sv("export default ");
    if (expr->tag == EXPR_IDENTIFIER) {
        w_leading(me, comments_from);
        w_indent(me);
        w(me, keyword);
        w(me, span_sv(expr_outer_span(expr)));
        w_lit(me, ";");
        decl_comments_trailing(&my comments, my out, comments_end);
        w_lit(me, "\n");
        return;
    }
    if ((expr->tag == EXPR_FUNCTION && !expr->function->is_constructor) || expr->tag == EXPR_CLASS_DECL) {
        Span own = expr->tag == EXPR_FUNCTION ? expr->function->name : expr->class_decl->name;
        cstr name = export_default_name(me, own);
        w_indent(me);
        w(me, keyword);
        w(me, cstr_sv(&name));
        w_lit(me, ";\n");
        w_leading(me, comments_from);
        if (expr->tag == EXPR_FUNCTION) {
            w_indent(me);
            if (is_file) w_lit(me, "declare ");
            w_lit(me, "function ");
            w(me, cstr_sv(&name));
            w_signature(me, expr->function, false);
            w_lit(me, ";");
            decl_comments_trailing(&my comments, my out, comments_end);
            w_lit(me, "\n");
        } else {
            emit_class(me, expr->class_decl, nullptr, container, cstr_sv(&name));
            isize_t size = cstr_size(my out);
            cstr_erase(my out, size - 1, 1);
            decl_comments_trailing(&my comments, my out, comments_end);
            w_lit(me, "\n");
        }
        cstr_drop(&name);
        return;
    }
    // `declare const _default: T; export default _default;` (a literal's: `= "x"`)
    cstr text = cstr_init();
    Ty const* expr_type = lazy_type_get((LazyType*)&expr->lazy_type);
    bool literal_value = expr_type && ty_is_fresh(expr_type) && is_primitive_literal_value(expr, true);
    cstr* outer = my type_out;
    my type_out = &text;
    Emitting saved = emit_enter(me, EMIT_DEFAULT_EXPORT, SIG_FUNCTION, span_null, statement->span);
    Root outer_root = my root;
    my root = (Root){ .kind = ROOT_EXPORT_DEFAULT, .expr = expr, .at = statement->span };
    Ty const* type = lazy_type_get((LazyType*)&expr->lazy_type);
    type = ty_widened_literal(my checker, ty_widened_nullable(my checker, ty_widened_objects(my checker, ty_regular(my checker, type))));
    serialize_declaration(me, pseudo_of_expression(my checker, expr, (PseudoPlace){0}), type, false, false);
    my root = outer_root;
    emit_leave(me, saved);
    my type_out = outer;
    cstr name = export_default_name(me, span_null);
    w_leading(me, comments_from);
    w_indent(me);
    if (is_file) w_lit(me, "declare ");
    w_lit(me, "const ");
    w(me, cstr_sv(&name));
    if (literal_value) {
        cstr_drop(&text);
        w_lit(me, " = ");
        w_type(me, expr_type);
    } else {
        w_lit(me, ": ");
        w_serialized(me, &text);
    }
    w_lit(me, ";");
    decl_comments_trailing(&my comments, my out, comments_end);
    w_lit(me, "\n");
    w_indent(me);
    w(me, keyword);
    w(me, cstr_sv(&name));
    w_lit(me, ";\n");
    cstr_drop(&name);
}

static void emit_statement_inner(Walk* me, Statement const* statement, Container container);
static void emit_export_default(Walk* me, Statement const* statement, Container container);

static void emit_statement(Walk* me, Statement const* statement, Container container) {
    if (is_internal_declaration(statement->span)) return;
    if (statement->tag == STATEMENT_EXPORT_DEFAULT_EXPR) {
        emit_export_default(me, statement, container); // (Its comments go to the declaration it makes)
        return;
    }
    isize_t start = w_leading(me, statement->span.buf);
    isize_t written_from = cstr_size(my out);
    emit_statement_inner(me, statement, container);
    w_unless_written(me, start, written_from, span_end_ptr(statement->span));
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
                emit_class(me, statement->decl.class, statement, container, c_sv(""));
            }
            return;
        case STATEMENT_DECL_INTERFACE:
            if (emit_visible(me, statement, container, statement->decl.interface->symbol)) {
                emit_interface(me, statement->decl.interface, statement, container);
            }
            return;
        case STATEMENT_DECL_TYPE: {
            TypeDecl const* decl = statement->decl.type;
            if (decl->jsdoc || decl->jsdoc_check_only) return;
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
    for_each (i, vec_statement, statements) emit_statement(me, i, container);
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
        }
    }
}

// The second walk: the file's `.d.ts`
static void emit_file(Walk* me, Container top) {
    my comments = (DeclComments){ .file = my file, .off = compiler_options()->remove_comments };
    decl_comments_detached(&my comments, my out);
    emit_directives(me);
    emit_statements(me, &my file->ast.statements, top);
    // (A module whose declarations wouldn't say so, or one with a declaration that isn't exported and nothing that says
    // which are: `export {};`)
    if (my file->is_module && (!my has_module_indicator || (my needs_scope_fix && !my has_scope_marker))) {
        w_lit(me, "export {};\n");
    }
    Statement const* last = nullptr;
    for_each (i, vec_statement, &my file->ast.statements) {
        if (span_valid(i->span) && i->tag != STATEMENT_SOURCE_DIRECTIVE) last = i;
    }
    if (last) decl_comments_file_end(&my comments, my out, span_end_ptr(last->span));
}

bool declaration_diagnostics_wanted(SourceFile const* file) {
    CompilerOptions const* options = compiler_options();
    if (!options->declaration && !options->composite) return false;
    if (file->is_declaration || file->is_json || file->is_stdlib || file->is_config) return false;
    // (A JavaScript file's declaration emit has its diagnostics too, but the walk only knows TypeScript's declarations:
    // only isolatedDeclarations' are reported there, see agents/hacks.md)
    if (file->is_js && !options->isolated_declarations) return false;
    // (tsc's sourceFileMayBeEmitted: not a package's file that an import found through a node_modules, even where a
    // `paths` mapping into a symlinked node_modules leads back into the workspace (isSourceFileFromExternalLibrary), nor
    // a referenced project's)
    if (file->found_searching_node_modules || file->from_reference) return false;
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

void declaration_diagnostics_check(SourceFile const* file, ErrorCtx* errs, bool emit) {
    if (file->is_js) js_declaration_diagnostics_check(file, errs);
    if (!declaration_diagnostics_wanted(file)) return;
    isize_t errors_before = vec_error_size(&errs->errors);
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
    if (file->is_module) collect_exported_locals(&walk, &file->ast.statements);
    Container top = { .global = !file->is_module, .module_top_level = file->is_module };
    check_statements(&walk, &file->ast.statements, top);
    // What the walk painted, and what that paints (transformAndReplaceLatePaintedStatements)
    for (u32 walked = 0; walked < walk.painted_count;) {
        u32 count = walk.painted_count;
        walk.late_pass = true;
        check_statements(&walk, &file->ast.statements, top);
        walk.late_pass = false;
        // (one no statement declares has nothing to walk)
        for (u32 i = 0; i < count; i++) walk.painted_done[i] = true;
        walked = count;
    }
    // The second walk writes the file, unless the first reported something about it (tsc doesn't write a declaration
    // file whose transform has diagnostics)
    if (emit && vec_error_size(&errs->errors) == errors_before) {
        ErrorCtx scratch = { .phase = errs->phase, .parent = errs->parent };
        cstr* output = c_new(cstr, cstr_init());
        walk.errs = &scratch;
        walk.out = output;
        emit_file(&walk, top);
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
        walk.errs = errs;
        error_ctx_drop(&scratch);
        SourceFile* writable = (SourceFile*)file;
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
    free(walk.painted_done);
    for (u32 i = 0; i < walk.specifier_count; i++) c_drop(cstr, &walk.specifiers[i].plain, &walk.specifiers[i].unsafe);
    free(walk.specifiers);
}
