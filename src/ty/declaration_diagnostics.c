#include "ty/declaration_diagnostics.h"
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

// tsc's declaration transformer walks what a `.d.ts` would contain: the visible statements (everything in a script,
// the exported ones and what `export { x }` names in a module), and in them the members that are emitted. Wherever a
// type would be written that the source doesn't spell out, it asks the syntactic type builder (expressionToTypeNode.ts)
// for one, and with isolatedDeclarations every place where that builder falls back to the checker's type is an error
// (reportInferenceFallback, TS9007-TS9025, with the "Add a type annotation" related information of
// createGetIsolatedDeclarationErrors). The transformer adds a few checks of its own (TS9020, TS9023, TS9026, TS9038).
//
// This file walks the same way and ports the builder's decisions (can this be typed from syntax, where does it fall
// back), building no type nodes. The builder's result is only "fell back" (tsc's `failed`, the caller reports) or not
// (a type, or a fallback already reported or not reported at all: tsc's alreadyReported and notImplemented)
//
// Not yet (plan 40 stage 2): written types (isEntityNameVisible, TS9039 through trackExistingEntityName), late painted
// statements (a non-exported declaration that a visible one names is visible too), JavaScript

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

// The ancestors of the node the builder looks at, as far as tsc's diagnostics and isContextuallyTyped care
typedef struct Place {
    Near near;
    // The node is the initializer of `near` (or the type of a type assertion that is), through parentheses and type
    // assertions (createExpressionError's `parentDeclaration === parent`)
    bool direct;
    // An ancestor is a call, JSX or has a type annotation (isContextuallyTyped, which stops at nothing: function
    // declarations don't count but their ancestors do)
    bool contextual;
    bool heritage; // In a class's `extends` clause: every fallback is TS9021
} Place;

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
} Walk;

typedef struct SpecifierMemo {
    SourceFile const* target;
    bool has_plain, has_unsafe;
    cstr plain; // getSpecifierForModuleSymbol without a mode of its own
    cstr unsafe; // Of the import type naming the module, empty if portable
} SpecifierMemo;

static Place place_at(Near near, bool direct) { return (Place){ .near = near, .direct = direct }; }

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
    if (access_has_type_name(e)) args[count++] = e->name.sv;
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
static void print_inferred(Walk* me, Ty const* type) {
    if (!type || my emitting.kind == EMIT_NONE) return;
    my tracker.truncated = false;
    // (With the types baseline's node reuse, which an underline turns on: tsc's declaration emit reuses annotations the
    // same way, naming what a reused name refers to anew where it means something else here)
    cstr text = cstr_init(), underline = cstr_init();
    ty_display(my checker, (TyDisplay){
        .target = &text, .underline = &underline, .enclosing = my scope, .tracker = &my tracker,
    }, type);
    if (getenv("TTTT_DEBUG_DECLARATION_TYPES")) {
        fprintf(stderr, "%.*s: %s\n", (int)my emitting.name.sv.size, my emitting.name.sv.buf, cstr_str(&text));
    }
    c_drop(cstr, &text, &underline);
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
    my fell_back = outer || fell_back;
    return fell_back;
}

// A declaration of this file that an emitted declaration names: if it isn't visible, it is emitted too
static void paint(Walk* me, Symbol const* symbol) {
    if (!symbol || symbol->exported || (symbol->flags & SYMBOL_TYPE_PARAMETER) || symbol->is_parameter) return;
    bool here = false;
    for (u32 i = 0; i < symbol->decl_count && !here; i++) here = symbol->decls[i].name.source == my file;
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
        *name = e->name.sv;
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

// symbolToTypeNode's import type: TS2742 when its specifier can only reach the module through a node_modules (in
// node16/nodenext, unless the other resolution mode's specifier can, with a `resolution-mode` attribute)
static cstr unsafe_specifier(Walk* me, SourceFile const* target) {
    CompilerOptions const* o = compiler_options();
    if (o->module_resolution == MODULE_RESOLUTION_CLASSIC) return cstr_null;
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

static void track_module_file(DeclTracker* tracker, SourceFile const* target) {
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
    report_once(me, at, 2742, cstr_d("The inferred type of '", name, "' cannot be named without a reference to '",
        memo->unsafe, "'. This is likely not portable. A type annotation is necessary."));
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
    report_access(me, SYMBOL_NOT_ACCESSIBLE, first.sv, c_sv(""), false, first);
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

static Error* report(Walk* me, Span at, unsigned code, char const* message) {
    my fell_back = true;
    if (!my isolated) return nullptr;
    return error_ctx_error_d(my errs, at, code, message);
}

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
    if (place.heritage) {
        report_heritage(me, at);
        return;
    }
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
    if (place.heritage) {
        report_heritage(me, at);
        return;
    }
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

// tsc's isEntityNameExpression: a name, or a dotted name of names
static bool is_entity_name_expression(Expr const* expr) {
    if (expr_is_perenthisized(expr)) return false;
    if (expr->tag == EXPR_IDENTIFIER) return !expr->identifier.keyword;
    if (expr->tag != EXPR_PROPERTY || expr->property->form != PROPERTY_EXPR_DOT) return false;
    return !expr->property->optional_chain && is_entity_name_expression(&expr->property->expr);
}

// tsc's isDefinitelyReferenceToGlobalSymbolObject: exactly `Symbol.x` (or `globalThis.Symbol.x`) with the global Symbol
static bool is_global_symbol_reference(Walk* me, Expr const* expr) {
    if (expr->tag != EXPR_PROPERTY || expr->property->form != PROPERTY_EXPR_DOT) return false;
    Expr const* object = &expr->property->expr;
    if (object->tag == EXPR_PROPERTY) {
        if (object->property->form != PROPERTY_EXPR_DOT || object->property->expr.tag != EXPR_IDENTIFIER) return false;
        Expr const* prop = &object->property->prop;
        bool symbol = prop->tag == EXPR_LITERAL && prop->literal.tag == VALUE_STRING &&
            cstr_equals(&prop->literal.string, "Symbol");
        IdentifierExpr const* global = &object->property->expr.identifier;
        return symbol && csview_equals(global->name.sv, "globalThis") && global->target && global->target->is_global_this;
    }
    if (object->tag != EXPR_IDENTIFIER || !csview_equals(object->identifier.name.sv, "Symbol")) return false;
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

// tsc's canAddUndefined: `T | undefined` can be written without knowing what T is
static bool can_add_undefined(Walk* me, TypeNode const* type) {
    if (!my strict_null_checks) return true;
    switch (type->tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
        case TYPE_CALLABLE:
        case TYPE_INTERFACE:
        case TYPE_TEMPLATE:
            return true;
        case TYPE_ARRAY: return !type->array.readonly; // (`readonly T[]` is a type operator)
        case TYPE_TUPLE: return !type->tuple.readonly;
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &type->composite) {
                if (!i->placeholder && !can_add_undefined(me, i)) return false;
            }
            return true;
        default: return false;
    }
}

// tsc's isConstTypeReference: `as const`
static bool is_const_type(TypeNode const* type) {
    return type->tag == TYPE_IDENTIFIER && span_equals(type->identifier->name, "const") &&
        !identifier_type_namespace_count(type->identifier) && !vec_spanned_type_size(&type->identifier->generic_params);
}

// Where the type of a type assertion is written
static Span assertion_type_span(Expr const* expr) {
    AsTypeExpr const* as = expr->as_type;
    char const* end = span_end_ptr(expr->inner_span);
    char const* start = span_end_ptr(as->keyword_span);
    if (as->angle_bracket_syntax) {
        start = span_start_ptr(expr->inner_span) + 1;
        end = span_start_ptr(as->expr.outer_span);
        while (end > start && end[-1] != '>') end--;
        if (end > start) end--;
    }
    while (start < end && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) end--;
    return span_make(expr->inner_span.source, start, end);
}

// The syntactic builder

static bool type_of_expression(Walk* me, Expr const* expr, Place place, bool const_context, bool add_undefined,
    bool preserve_literals);
static void check_written_type(Walk* me, TypeNode const* type);
static void check_written_signature(Walk* me, FunctionImpl const* fn, EmitSig sig, bool declaration);
static void check_generic(Walk* me, Generic const* generic, EmitSig sig);
static bool return_of_signature(Walk* me, FunctionImpl const* fn, Place place, NearKind kind, bool report_fallback,
    Span at);
static void check_parameter(Walk* me, FunctionImpl const* fn, FunctionArg const* arg, Place place, bool in_function);

// The kind of `near` a function is
static NearKind function_kind(FunctionImpl const* fn) {
    if (fn->accessor == 1) return NEAR_GET_ACCESSOR;
    if (fn->accessor == 2) return NEAR_SET_ACCESSOR;
    if (fn->is_method) return NEAR_METHOD;
    if (fn->is_expression) return NEAR_FUNCTION_EXPRESSION;
    return NEAR_FUNCTION_DECLARATION;
}

// typeFromTypeAssertion
static bool type_of_assertion(Walk* me, Expr const* expr, Place place, bool add_undefined) {
    AsTypeExpr const* as = expr->as_type;
    Place inner = place;
    inner.contextual = true;
    if (is_const_type(&as->type)) return type_of_expression(me, &as->expr, inner, true, add_undefined, false);
    bool outer_reusing = my reusing;
    my reusing = true;
    check_written_type(me, &as->type);
    my reusing = outer_reusing;
    // serializeExistingTypeNodeWithFallback
    if (add_undefined && !can_add_undefined(me, &as->type)) {
        report_expression(me, assertion_type_span(expr), place, 0, nullptr);
    }
    return false;
}

// Whether a statement has a `return` in it (not in a nested function)
static bool has_return(Statement const* me) {
    switch (my tag) {
        case STATEMENT_RETURN_EXPR: return true;
        case STATEMENT_BLOCK:
            for_each (i, vec_statement, &my block.statements) {
                if (has_return(i)) return true;
            }
            return false;
        case STATEMENT_IF_ELSE:
            for_each (i, vec_statement, &my if_else.statements) {
                if (has_return(i)) return true;
            }
            return false;
        case STATEMENT_FOR_LOOP: return has_return(&my for_loop->initialization) || has_return(&my for_loop->body);
        case STATEMENT_FOR_EACH_LOOP: return has_return(&my for_each_loop->body);
        case STATEMENT_SWITCH:
            for_each (i, vec_switch_case, &my switch_->cases) {
                for_each (j, vec_statement, &i->statements) {
                    if (has_return(j)) return true;
                }
            }
            return false;
        case STATEMENT_TRY_CATCH: {
            VecStatement const* blocks[] = { &my try_catch->try_block.statements, &my try_catch->catch_block.statements,
                &my try_catch->finally_block.statements };
            for (u32 b = 0; b < c_arraylen(blocks); b++) {
                for_each (i, vec_statement, blocks[b]) {
                    if (has_return(i)) return true;
                }
            }
            return false;
        }
        case STATEMENT_LABELED: return has_return(my labeled.body);
        case STATEMENT_WITH: return has_return(&my with->body);
        default: return false;
    }
}

// typeFromSingleReturnExpression: a function whose body is one expression, or that has a single `return` directly in
// its body. Whether the builder fell back
static bool type_of_single_return(Walk* me, FunctionImpl const* fn, Place place) {
    if (!fn->has_body || fn->body_missing) return true;
    // (getFunctionFlags & FunctionFlags.AsyncGenerator: either flag)
    if (fn->is_async || fn->is_generator) return true;
    Expr const* candidate = nullptr;
    Near near = place.near;
    if (fn->expression_body) {
        Statement const* ret = vec_statement_at(&fn->body.statements, 0);
        if (ret && ret->tag == STATEMENT_RETURN_EXPR) candidate = &ret->expr;
    } else {
        // (forEachReturnStatement: a second `return`, or one that isn't directly in the body, means none)
        bool seen = false;
        for_each (i, vec_statement, &fn->body.statements) {
            if (i->tag == STATEMENT_RETURN_EXPR) {
                Expr const* expr = statement_is_bare_return(i) ? nullptr : &i->expr;
                if (!candidate) {
                    candidate = expr;
                    if (seen && !expr) continue;
                    seen = true;
                    continue;
                }
                candidate = nullptr;
                break;
            }
            if (has_return(i)) {
                candidate = nullptr;
                break;
            }
        }
        // The nearest declaration of a returned expression is the function the `return` is in
        near = (Near){ .kind = function_kind(fn), .span = function_error_span(fn) };
    }
    if (!candidate) return true;
    if (place.contextual) {
        // Only a type assertion's type can be relied on
        if (candidate->tag == EXPR_AS_TYPE && !candidate->as_type->is_satisfies) return is_const_type(&candidate->as_type->type);
        return true;
    }
    return type_of_expression(me, candidate, (Place){ .near = near }, false, false, false);
}

// createReturnFromSignature, and the report of inferReturnTypeOfSignatureSignature. Whether the builder fell back. `at`:
// where the function's diagnostics go, if not function_error_span()
static bool return_of_signature(Walk* me, FunctionImpl const* fn, Place place, NearKind kind, bool report_fallback,
    Span at
) {
    if (fn->ret_explicit || fn->predicate) return false;
    if (!type_of_single_return(me, fn, place)) return false;
    if (!report_fallback) return true;
    if (place.heritage) {
        report_heritage(me, function_error_span(fn));
        return true;
    }
    if (kind == NEAR_GET_ACCESSOR || kind == NEAR_SET_ACCESSOR) return true; // (the caller reports the accessor pair)
    unsigned code = 0;
    char const* message = nullptr;
    kind_error(kind, &code, &message);
    // createReturnTypeError: the nearest declaration (none for a declaration directly in a statement list or class)
    // and the function
    if (!span_valid(at)) at = function_error_span(fn);
    Error* error = report(me, at, code, message);
    add_related(error, place.near);
    add_related(error, (Near){ .kind = kind, .span = at });
    return true;
}

// An accessor pair: the getter, the setter (either may be null)
typedef struct Accessors {
    FunctionImpl const* getter;
    FunctionImpl const* setter;
    Span getter_name, setter_name;
} Accessors;

// createAccessorTypeError for `at` (one of the pair)
static void report_accessor(Walk* me, Accessors pair, bool on_setter, Place place) {
    Span at = on_setter ? pair.setter_name : pair.getter_name;
    if (on_setter && vec_function_arg_size(&pair.setter->args)) at = vec_function_arg_at(&pair.setter->args, 0)->span;
    if (place.heritage) {
        report_heritage(me, at);
        return;
    }
    Error* error = report(me, at, 9009,
        "At least one accessor must have an explicit type annotation with --isolatedDeclarations.");
    if (pair.setter) add_related(error, (Near){ .kind = NEAR_SET_ACCESSOR, .span = pair.setter_name });
    if (pair.getter) add_related(error, (Near){ .kind = NEAR_GET_ACCESSOR, .span = pair.getter_name });
}

static TypeNode const* setter_type(FunctionImpl const* setter) {
    if (!setter || !vec_function_arg_size(&setter->args)) return nullptr;
    FunctionArg const* arg = vec_function_arg_at(&setter->args, 0);
    return arg->initializer.binding.has_explicit_type ? &arg->initializer.binding.explicit_type : nullptr;
}

static bool accessors_annotated(Accessors pair) {
    return (pair.getter && pair.getter->ret_explicit) || setter_type(pair.setter);
}

// serializeTypeOfAccessor for one of a pair (typeFromAccessor, else inferAccessorType)
static void type_of_accessor(Walk* me, Accessors pair, bool of_setter, Place place) {
    if (accessors_annotated(pair)) return;
    if (pair.getter) {
        if (return_of_signature(me, pair.getter, place, NEAR_GET_ACCESSOR, true, (Span){0})) report_accessor(me, pair, false, place);
        return;
    }
    if (of_setter) report_accessor(me, pair, true, place);
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
    Ty const* type = ty_from_node(my checker, &binding->explicit_type, (LowerCtx){0});
    if (type->flags & (TYF_ERROR | TYF_UNDEFINED)) return false;
    if (type->kind == TY_UNION) {
        for (u32 i = 0; i < type->composite.types.len; i++) {
            if (type->composite.types.data[i]->flags & TYF_UNDEFINED) return false;
        }
    }
    return true;
}

static Near parameter_near(FunctionArg const* arg) {
    return (Near){ .kind = NEAR_PARAMETER, .span = arg->span, .name = arg->initializer.binding.content_span.sv };
}

// typeFromParameter, and createParameterError where it falls back. `place`: where the function is
static void check_parameter(Walk* me, FunctionImpl const* fn, FunctionArg const* arg, Place place, bool in_function) {
    if (arg->initializer.binding.tag == BINDING_IDENTIFIER && csview_equals(arg->initializer.binding.content_span.sv, "this")) {
        return; // (getThisParameter: not part of the parameter list)
    }
    Binding const* binding = &arg->initializer.binding;
    Expr const* init = initialized_binding_value(&arg->initializer);
    bool add_undefined = requires_adding_undefined(me, fn, arg, in_function);
    Near near = parameter_near(arg);
    bool failed = true;
    if (binding->has_explicit_type) {
        // serializeTypeAnnotationOfDeclaration with requiresAddingUndefined: the annotation is reused unless the
        // undefined can't be added to it syntactically
        failed = add_undefined && !can_add_undefined(me, &binding->explicit_type);
    } else if (init->tag != EXPR_IMPLICIT_ANY && binding->tag == BINDING_IDENTIFIER && !place.contextual) {
        failed = type_of_expression(me, init, (Place){ .near = near, .direct = true }, false, add_undefined, false);
    }
    if (!failed) return;
    if (place.heritage) {
        report_heritage(me, arg->span);
        return;
    }
    // (createParameterError asks again, with the function as the enclosing declaration)
    add_undefined = requires_adding_undefined(me, fn, arg, true);
    if (!add_undefined && init->tag != EXPR_IMPLICIT_ANY) {
        report_expression(me, init->outer_span, (Place){ .near = near, .direct = true }, 0, nullptr);
        return;
    }
    if (add_undefined) {
        add_related(report(me, arg->span, 9025, "Declaration emit for this parameter requires implicitly adding "
            "undefined to its type. This is not supported with --isolatedDeclarations."), near);
        return;
    }
    report_declaration(me, near);
}

// The parameters of a function the declaration has (tsc's updateParamsList / ensureParameter)
static void check_parameters(Walk* me, FunctionImpl const* fn, Place place, bool in_function) {
    for_each (i, vec_function_arg, &fn->args) check_parameter(me, fn, i, place, in_function);
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

// The parameters of a declaration the transformer visits, each with diagnostics of its own
static void check_declaration_parameters(Walk* me, FunctionImpl const* fn, EmitSig sig) {
    for_each (i, vec_function_arg, &fn->args) {
        Span name = i->initializer.binding.content_span;
        Emitting saved = emit_enter(me, EMIT_PARAMETER, sig, name, name);
        bool outer = fallback_begin(me);
        check_parameter(me, fn, i, (Place){0}, true);
        fallback_end(me, outer, parameter_type(me, fn, i));
        emit_leave(me, saved);
    }
}

// The return type of a declaration's signature, with the signature's diagnostics
static bool check_declaration_return(Walk* me, FunctionImpl const* fn, NearKind kind, EmitSig sig, Span name, Span at) {
    if (!span_valid(name)) name = fn->name;
    Emitting saved = emit_enter(me, EMIT_RETURN, sig, name, span_valid(at) ? at : span_valid(name) ? name : function_error_span(fn));
    bool outer = fallback_begin(me);
    bool failed = return_of_signature(me, fn, (Place){0}, kind, true, at);
    my fell_back |= failed;
    Ty const* type = nullptr;
    if (my fell_back && !fn->ret_explicit) {
        Signature const* sig_ = ty_function_signature(my checker, fn);
        type = sig_ ? signature_return_type(my checker, sig_) : nullptr;
    }
    fallback_end(me, outer, type);
    emit_leave(me, saved);
    return failed;
}

// typeFromFunctionLikeExpression: never falls back, its parts report
static void type_of_function_expression(Walk* me, FunctionImpl const* fn, Place place) {
    bool outer_reusing = my reusing;
    my reusing = true;
    check_written_signature(me, fn, SIG_FUNCTION, false); // (its annotations are reused)
    my reusing = outer_reusing;
    return_of_signature(me, fn, place, NEAR_FUNCTION_EXPRESSION, true, (Span){0});
    Place params = place;
    params.direct = false;
    check_parameters(me, fn, params, true);
}

// typeFromArrayLiteral
static void type_of_array(Walk* me, Expr const* expr, Place place, bool const_context) {
    if (!const_context) {
        report_with_near(me, expr->outer_span, place, 9017, "Only const arrays can be inferred with --isolatedDeclarations.");
        return;
    }
    for_each (i, vec_expr, &expr->array) {
        if (i->tag == EXPR_SPREAD) {
            report_with_near(me, i->outer_span, place, 9018,
                "Arrays with spread elements can't inferred with --isolatedDeclarations.");
            return;
        }
    }
    Place element = place;
    element.direct = false;
    for_each (i, vec_expr, &expr->array) {
        if (i->tag == EXPR_IMPLICIT_ANY) continue; // (an omitted expression)
        if (type_of_expression(me, i, element, true, false, false)) report_expression(me, i->outer_span, element, 0, nullptr);
    }
}

// The accessor pair an object literal accessor belongs to
static Accessors object_accessors(ObjectExpr const* object, ObjectExprItem const* item, bool* first) {
    Accessors pair = {0};
    *first = false;
    for_each (i, vec_object_expr_item, object) {
        if (i->tag != OBJECT_ITEM_GETTER && i->tag != OBJECT_ITEM_SETTER) continue;
        if (i != item && (i->computed_key || item->computed_key || !cstr_equals_sv(&i->name_string, cstr_sv(&item->name_string)))) continue;
        if (!pair.getter && !pair.setter) *first = i == item;
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

// typeFromObjectLiteral: never falls back, its properties report
static void type_of_object(Walk* me, Expr const* expr, Place place, bool const_context) {
    ObjectExpr const* object = &expr->object;
    Place inner = place;
    inner.direct = false;
    // canGetTypeFromObjectLiteral
    bool can = true;
    for_each (i, vec_object_expr_item, object) {
        if (i->tag == OBJECT_ITEM_SHORTHAND) {
            report_with_near(me, i->name_span, inner, 9016,
                "Objects that contain shorthand properties can't be inferred with --isolatedDeclarations.");
            can = false;
        } else if (i->tag == OBJECT_ITEM_SPREAD) {
            report_with_near(me, i->span, inner, 9015,
                "Objects that contain spread assignments can't be inferred with --isolatedDeclarations.");
            can = false;
        } else if (object_item_is_private_name(i)) {
            can = false;
        } else if (i->computed_key && !is_primitive_literal_value(i->computed_key, false) &&
            !is_global_symbol_reference(me, i->computed_key)
        ) {
            report_with_near(me, i->name_span, inner, 9038,
                "Computed property names on class or object literals cannot be inferred with --isolatedDeclarations.");
            can = false;
        }
    }
    if (!can) return;
    for_each (i, vec_object_expr_item, object) {
        switch (i->tag) {
            case OBJECT_ITEM_METHOD: {
                FunctionImpl const* fn = i->value.function;
                return_of_signature(me, fn, inner, NEAR_METHOD, true, (Span){0});
                check_parameters(me, fn, inner, true);
                break;
            }
            case OBJECT_ITEM_PROPERTY:
                if (type_of_expression(me, &i->value, inner, const_context, false, false)) {
                    report_expression(me, i->value.outer_span, inner, 0, nullptr);
                }
                break;
            case OBJECT_ITEM_GETTER:
            case OBJECT_ITEM_SETTER: {
                // typeFromObjectLiteralAccessor
                bool first = false;
                Accessors pair = object_accessors(object, i, &first);
                FunctionImpl const* fn = i->value.function;
                bool both = pair.getter && pair.getter->ret_explicit && setter_type(pair.setter);
                if (both) {
                    check_parameters(me, fn, inner, true);
                } else if (first && !accessors_annotated(pair)) {
                    // inferAccessorType
                    if (i->tag == OBJECT_ITEM_GETTER) {
                        if (return_of_signature(me, fn, inner, NEAR_GET_ACCESSOR, true, (Span){0})) report_accessor(me, pair, false, inner);
                    } else {
                        report_accessor(me, pair, true, inner);
                        if (pair.getter && return_of_signature(me, pair.getter, inner, NEAR_GET_ACCESSOR, true, (Span){0})) {
                            report_accessor(me, pair, false, inner);
                        }
                    }
                }
                break;
            }
            default: break;
        }
    }
}

// typeFromExpression. Whether the builder fell back (the caller reports, at its declaration or at the expression)
static bool type_of_expression(Walk* me, Expr const* expr, Place place, bool const_context, bool add_undefined,
    bool preserve_literals
) {
    switch (expr->tag) {
        case EXPR_LITERAL:
            switch (expr->literal.tag) {
                case VALUE_UNDEFINED: // (the `undefined` identifier)
                case VALUE_NULL:
                case VALUE_BOOLEAN:
                case VALUE_NUMBER:
                case VALUE_STRING:
                case VALUE_BIGINT: return false;
                default: return true;
            }
        case EXPR_FUNCTION:
            type_of_function_expression(me, expr->function, place);
            return false;
        case EXPR_AS_TYPE:
            if (expr->as_type->is_satisfies) return true;
            return type_of_assertion(me, expr, place, add_undefined);
        case EXPR_UNARY_OP:
            return !is_primitive_literal_value(expr, true);
        case EXPR_ARRAY:
            type_of_array(me, expr, place, const_context);
            return false;
        case EXPR_OBJECT:
            type_of_object(me, expr, place, const_context);
            return false;
        case EXPR_CLASS_DECL:
            // (A class expression's error span is its name, else its first token)
            report_expression(me, span_valid(expr->class_decl->name) ? expr->class_decl->name : block_first_token(expr->inner_span), place, 9022,
                "Inference from class expressions is not supported with --isolatedDeclarations.");
            return false;
        case EXPR_TEMPLATE:
            return expr->template_->tag || const_context || preserve_literals;
        default: return true;
    }
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
        if (name.sv.size > member->name.size && csview_equals_sv(csview_slice(name.sv, name.sv.size - member->name.size, name.sv.size), member->name)) {
            name = span_make(at.source, span_end_ptr(at) - member->name.size, span_end_ptr(at));
        }
        Emitting saved = emit_enter(me, EMIT_PROPERTY, SIG_INTERFACE, name, at);
        bool outer = fallback_begin(me);
        my fell_back = true;
        fallback_end(me, outer, ty_type_of_symbol(my checker, member));
        emit_leave(me, saved);
        my member_static = outer_static, my member_in_class = outer_in_class;
    }
    return any;
}

static bool is_expando_variable(Walk* me, Binding const* binding, Expr const* init) {
    if (init->tag != EXPR_FUNCTION && init->tag != EXPR_CLASS_DECL) return false;
    return check_expandos(me, binding->symbol, false);
}

// reportInferenceFallback of a variable declaration
static void report_variable(Walk* me, Binding const* binding, Expr const* init) {
    if (is_expando_variable(me, binding, init)) {
        check_expandos(me, binding->symbol, true);
        // The checker's type is written instead, and its signature's return type and parameters go through the
        // builder again (the node builder's serializeReturnTypeForSignature)
        Near near = { .kind = NEAR_VARIABLE, .span = binding->content_span, .name = binding->content_span.sv };
        if (init->tag == EXPR_FUNCTION) type_of_function_expression(me, init->function, place_at(near, false));
        return;
    }
    report_declaration(me, (Near){ .kind = NEAR_VARIABLE, .span = binding->content_span, .name = binding->content_span.sv });
}

// tsc's isLiteralConstDeclaration: a const (or readonly) declaration of a fresh literal type, which declaration emit
// writes with its initializer
static bool is_literal_const(Walk* me, Symbol const* symbol) {
    return symbol && ty_is_fresh(ty_type_of_symbol(my checker, symbol));
}

static bool is_literal_const_field(ClassItem const* item) {
    return ty_is_fresh(lazy_type_get((LazyType*)&item->field.lazy_type));
}

// The elements of a binding pattern are emitted as variables of their own: TS9019
static void check_binding_elements(Walk* me, Binding const* pattern) {
    if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) {
            if (i->tag == BINDING_IDENTIFIER) {
                report(me, i->content_span, 9019, "Binding elements can't be exported directly with --isolatedDeclarations.");
            } else {
                check_binding_elements(me, i);
            }
        }
    } else if (pattern->tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &pattern->object) {
            Binding const* target = &i->target;
            if (target->tag == BINDING_IDENTIFIER) {
                report(me, target->content_span, 9019,
                    "Binding elements can't be exported directly with --isolatedDeclarations.");
            } else {
                check_binding_elements(me, target);
            }
        }
    }
}

static void check_variable_type(Walk* me, InitializedBinding const* decl, bool is_const);

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

static void check_variable(Walk* me, InitializedBinding const* decl, bool is_const) {
    Binding const* binding = &decl->binding;
    if (binding->tag != BINDING_IDENTIFIER) {
        check_variable_type(me, decl, is_const);
        print_binding_elements(me, binding);
        return;
    }
    Emitting saved = emit_enter(me, EMIT_VARIABLE, SIG_FUNCTION, binding->content_span, binding->content_span);
    bool outer = fallback_begin(me);
    check_variable_type(me, decl, is_const);
    // (A literal const is written with its initializer)
    bool literal = is_const && initialized_binding_value(decl)->tag != EXPR_IMPLICIT_ANY && is_literal_const(me, binding->symbol);
    if (literal) my fell_back = false;
    fallback_end(me, outer, binding->symbol ? ty_type_of_symbol(my checker, binding->symbol) : nullptr);
    emit_leave(me, saved);
}

// typeFromVariable
static void check_variable_type(Walk* me, InitializedBinding const* decl, bool is_const) {
    Binding const* binding = &decl->binding;
    Expr const* init = initialized_binding_value(decl);
    if (binding->tag != BINDING_IDENTIFIER) {
        bool empty = (binding->tag == BINDING_ARRAY && !vec_binding_size(&binding->array)) ||
            (binding->tag == BINDING_OBJECT && !vec_object_binding_item_size(&binding->object));
        if (!empty) check_binding_elements(me, binding);
        return;
    }
    bool initialized = init->tag != EXPR_IMPLICIT_ANY;
    if (is_const && initialized && is_literal_const(me, binding->symbol)) {
        // ensureNoInitializer
        if (!is_primitive_literal_value(init, true)) report_variable(me, binding, init);
        return;
    }
    if (binding->has_explicit_type) {
        check_written_type(me, &binding->explicit_type);
        return;
    }
    Near near = { .kind = NEAR_VARIABLE, .span = binding->content_span, .name = binding->content_span.sv };
    bool failed = true;
    if (initialized && symbol_declared_once(binding->symbol) && !is_expando_variable(me, binding, init)) {
        failed = type_of_expression(me, init, place_at(near, true), false, false, is_const);
    }
    if (failed) report_variable(me, binding, init);
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
    check_declaration_return(me, fn, NEAR_FUNCTION_DECLARATION, SIG_FUNCTION, (Span){0}, at);
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
static void check_field_type(Walk* me, ClassItem const* item);
static void check_field(Walk* me, ClassItem const* item) {
    if (item->access == ACCESS_MODIFIER_PRIVATE) return;
    Emitting saved = emit_enter(me, EMIT_PROPERTY, SIG_CLASS, item->field.name_span, item->field.name_span);
    bool outer = fallback_begin(me);
    check_field_type(me, item);
    fallback_end(me, outer, lazy_type_get((LazyType*)&item->field.lazy_type));
    emit_leave(me, saved);
}

static void check_field_type(Walk* me, ClassItem const* item) {
    Expr const* init = &item->field.value;
    bool initialized = init->tag != EXPR_IMPLICIT_ANY;
    Near near = { .kind = NEAR_PROPERTY, .span = item->field.name_span, .name = item->field.name_span.sv };
    if (item->field.is_readonly && initialized && is_literal_const_field(item)) {
        if (!is_primitive_literal_value(init, true)) report_declaration(me, near);
        return;
    }
    if (item->field.has_explicit_type) {
        check_written_type(me, &item->field.explicit_type);
        return;
    }
    bool failed = true;
    if (initialized) failed = type_of_expression(me, init, place_at(near, true), false, false, item->field.is_readonly);
    if (failed) report_declaration(me, near);
}

static bool is_class_overload_implementation(ClassDecl const* cls, FunctionImpl const* fn) {
    if (!fn->has_body) return false;
    for_each (i, vec_class_item, &cls->items) {
        FunctionImpl const* other = i->tag == CLASS_ITEM_METHOD ? &i->method.func :
            i->tag == CLASS_ITEM_CONSTRUCTOR ? &i->constructor.func : nullptr;
        if (other && other != fn && other->overload_implementation == fn) return true;
    }
    return false;
}

// `statement`: the class declaration's (an anonymous one's diagnostics go to its first token)
static void check_class(Walk* me, ClassDecl const* cls, Span statement) {
    bool outer_static = my member_static, outer_in_class = my member_in_class;
    my member_in_class = true;
    for_each (i, vec_class_item, &cls->items) {
        my member_static = class_item_is_static((ClassItem*)i);
        Expr const* key = i->tag == CLASS_ITEM_METHOD ? i->method.computed_key :
            i->tag == CLASS_ITEM_FIELD ? i->field.computed_key : nullptr;
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
                    return_of_signature(me, &i->method.func, (Place){0}, NEAR_METHOD, true, (Span){0});
                    check_parameters(me, &i->method.func, (Place){0}, true);
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
                        if (j->initializer.binding.tag != BINDING_IDENTIFIER) {
                            check_binding_elements(me, &j->initializer.binding);
                        } else if (j->access != ACCESS_MODIFIER_PRIVATE) {
                            // (a parameter's diagnostics, a property's if the constructor is private)
                            Span name = j->initializer.binding.content_span;
                            bool private_ctor = i->access == ACCESS_MODIFIER_PRIVATE;
                            Emitting saved = emit_enter(me, private_ctor ? EMIT_PROPERTY : EMIT_PARAMETER, SIG_CONSTRUCTOR, name, name);
                            bool outer = fallback_begin(me);
                            check_parameter(me, ctor, j, (Place){0}, false);
                            fallback_end(me, outer, parameter_type(me, ctor, j));
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
                    if (i->access == ACCESS_MODIFIER_PRIVATE || is_class_overload_implementation(cls, fn)) break;
                    check_written_signature(me, fn, SIG_METHOD, true);
                    check_declaration_return(me, fn, NEAR_METHOD, SIG_METHOD, i->method.name_span, (Span){0});
                    check_declaration_parameters(me, fn, SIG_METHOD);
                    break;
                }
                if (i->access == ACCESS_MODIFIER_PRIVATE) break;
                check_written_signature(me, fn, SIG_ACCESSOR, true);
                Accessors pair = class_accessors(i);
                bool getter = i->method.accessor == CLASS_ACCESSOR_GET;
                Emitting saved = emit_enter(me, getter ? EMIT_GETTER : EMIT_SETTER, SIG_ACCESSOR, i->method.name_span, i->method.name_span);
                bool outer = fallback_begin(me);
                if (getter) {
                    find_setter(cls, i, &pair);
                    if (!fn->ret_explicit) type_of_accessor(me, pair, false, (Place){0});
                } else {
                    type_of_accessor(me, pair, true, (Place){0});
                }
                Ty const* type = nullptr;
                if (my fell_back) {
                    // (the accessor pair's type: the getter's return type, else the setter's parameter's)
                    Signature const* sig = pair.getter ? ty_function_signature(my checker, pair.getter) : nullptr;
                    if (sig) type = signature_return_type(my checker, sig);
                    else if (pair.setter && vec_function_arg_size(&pair.setter->args)) {
                        type = parameter_type(me, pair.setter, vec_function_arg_at(&pair.setter->args, 0));
                    }
                }
                fallback_end(me, outer, type);
                emit_leave(me, saved);
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
    if (base->tag == EXPR_GENERIC_PARAMS) {
        for_each (i, vec_spanned_type, &base->generic_params->params) check_written_type(me, &i->type);
        base = &base->generic_params->expr;
    }
    bool is_null = base->tag == EXPR_LITERAL && base->literal.tag == VALUE_NULL;
    if (base->tag == EXPR_IMPLICIT_ANY || is_null) return;
    Emitting saved = emit_enter(me, EMIT_EXTENDS, SIG_CLASS, cls->name,
        span_valid(cls->extends_clause) ? cls->extends_clause : cls->extends.outer_span);
    my emitting.fallback = block_first_token(statement);
    if (is_entity_name_expression(base)) {
        check_entity_expression(me, base);
    } else {
        // (written as a variable `_base` of the expression's type)
        bool outer = fallback_begin(me);
        Place heritage = { .heritage = true };
        if (type_of_expression(me, base, heritage, false, false, false)) report_heritage(me, base->outer_span);
        my fell_back = true;
        fallback_end(me, outer, lazy_type_get((LazyType*)&base->lazy_type));
    }
    emit_leave(me, saved);
}

static void check_generic(Walk* me, Generic const* generic, EmitSig sig) {
    for_each (i, vec_generic_item, &generic->items) {
        Emitting saved = emit_enter_nested(me, EMIT_TYPE_PARAMETER, sig, i->name, i->name);
        if (i->extends_explicit) check_written_type(me, &i->extends);
        if (i->fallback_explicit) check_written_type(me, &i->fallback);
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
        check_written_type(me, &i->initializer.binding.explicit_type);
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
        if (i->key.tag == INTERFACE_KEY_CALLABLE && vec_span_size(&i->declarations)) {
            // (A signature's node ends after its `;` or `,`)
            at = *vec_span_at(&i->declarations, 0);
            if (span_valid(at) && (span_end_ptr(at)[0] == ';' || span_end_ptr(at)[0] == ',')) at.sv.size++;
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
        if (i->key.tag == INTERFACE_KEY_EXPR && i->key.expr.ref) {
            Expr const* key = i->key.expr.ref;
            if (is_dynamic_name(key) && !is_global_symbol_reference(me, key) && !is_entity_name_expression(key)) {
                report(me, at, 9014, "Computed properties must be number or string literals, variables or dotted "
                    "expressions with --isolatedDeclarations.");
                continue;
            }
            // A late bound name's entity (checkName)
            if (is_entity_name_expression(key)) {
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
            if (type->typeof_->expr) check_entity_expression(me, type->typeof_->expr);
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
    Expr const* prop = &expr->property->prop;
    if (prop->tag != EXPR_LITERAL || prop->literal.tag != VALUE_STRING) return nullptr;
    Symbol const* object = enum_entity_symbol(&expr->property->expr);
    if (!object || !(object->flags & (SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE | SYMBOL_ENUM))) return nullptr;
    return symbol_get_export(object, cstr_sv(&prop->literal.string));
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
        case STATEMENT_DECL_TYPE: return statement->decl.type.symbol;
        case STATEMENT_DECL_ENUM: return statement->decl.enum_->symbol;
        case STATEMENT_DECL_NAMESPACE: return statement->decl.namespace.symbol;
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
        NamespaceDecl const* decl = &statement->decl.namespace;
        bool augmentation = decl->is_augmentation || decl->is_global;
        if (augmentation || statement_visible_at_first(me, statement, container, decl->symbol)) {
            check_statement_now(me, statement, container); // (still a late pass inside)
            return;
        }
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
            if (symbol->decls[j].name.source == target) return true;
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

static void check_statement_now(Walk* me, Statement const* statement, Container container) {
    switch (statement->tag) {
        case STATEMENT_IMPORT_EXPORT:
            check_import(me, statement);
            return;
        case STATEMENT_DECL_VARIABLE: {
            if (!container.global && !container.ambient && statement->decl.props.exported == DECL_EXPORTED_NO) {
                // (`export { x }` makes that one declaration visible)
                bool any = false;
                for_each (i, vec_initialized_binding, &statement->decl.variable.initializers) {
                    any |= i->binding.tag == BINDING_IDENTIFIER && container.module_top_level &&
                        is_exported_local(me, i->binding.symbol);
                }
                if (!any) return;
                for_each (i, vec_initialized_binding, &statement->decl.variable.initializers) {
                    if (i->binding.tag == BINDING_IDENTIFIER && is_exported_local(me, i->binding.symbol)) {
                        check_variable(me, i, variable_statement_is_const(&statement->decl.variable));
                    }
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
            TypeDecl const* decl = &statement->decl.type;
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
            NamespaceDecl const* decl = &statement->decl.namespace;
            bool augmentation = decl->is_augmentation || decl->is_global;
            if (!augmentation && !statement_visible(me, statement, container, decl->symbol)) return;
            bool ambient = container.ambient || statement->decl.props.declared || decl->is_ambient_module || decl->is_global;
            Scope const* outer_scope = my scope;
            if (decl->inner_scope) my scope = decl->inner_scope;
            check_statements(me, &decl->contents.statements, (Container){ .ambient = ambient });
            my scope = outer_scope;
            return;
        }
        case STATEMENT_EXPORT_DEFAULT_EXPR: {
            Expr const* expr = &statement->expr;
            if (expr->tag == EXPR_IDENTIFIER) return;
            Near near = { .kind = NEAR_EXPORT_ASSIGNMENT, .span = statement->span };
            Emitting saved = emit_enter(me, EMIT_DEFAULT_EXPORT, SIG_FUNCTION, span_null, statement->span);
            bool outer = fallback_begin(me);
            if (type_of_expression(me, expr, place_at(near, true), false, false, true)) {
                report_expression(me, expr->outer_span, place_at(near, true), 0, nullptr);
            }
            fallback_end(me, outer, lazy_type_get((LazyType*)&expr->lazy_type));
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

bool declaration_diagnostics_wanted(SourceFile const* file) {
    CompilerOptions const* options = compiler_options();
    if (!options->declaration && !options->composite) return false;
    if (file->is_declaration || file->is_json || file->is_js || file->is_stdlib || file->is_config) return false;
    // (A package's file that an import found: tsc's isSourceFileFromExternalLibrary)
    return !(file->loaded_by_import && strstr(cstr_str(&file->canonical_path), "node_modules/"));
}

void declaration_diagnostics_check(SourceFile const* file, ErrorCtx* errs) {
    if (!declaration_diagnostics_wanted(file)) return;
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
        .track_module_file = track_module_file, .ctx = &walk,
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
    symbol_chain_set_painted(nullptr, nullptr);
    symbol_chain_set_module_specifier(nullptr, nullptr);
    free(walk.exported_locals);
    free(walk.painted);
    free(walk.painted_done);
    for (u32 i = 0; i < walk.specifier_count; i++) c_drop(cstr, &walk.specifiers[i].plain, &walk.specifiers[i].unsafe);
    free(walk.specifiers);
}
