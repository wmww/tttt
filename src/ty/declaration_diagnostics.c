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

typedef struct Walk {
    SourceFile const* file;
    ErrorCtx* errs;
    Checker* checker;
    bool strict_null_checks;
    // A module's local declarations that `export { x }` or `export default x` name (tsc's collectLinkedAliases makes
    // them visible)
    Symbol const** exported_locals;
    u32 exported_local_count;
} Walk;

static Place place_at(Near near, bool direct) { return (Place){ .near = near, .direct = direct }; }

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
    if (near.kind == NEAR_NONE) return;
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
        vec_span_is_empty(&type->identifier->namespaces) && !vec_spanned_type_size(&type->identifier->generic_params);
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
static bool return_of_signature(Walk* me, FunctionImpl const* fn, Place place, NearKind kind, bool report_fallback);
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
    if (fn->is_async && fn->is_generator) return true;
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

// createReturnFromSignature, and the report of inferReturnTypeOfSignatureSignature. Whether the builder fell back
static bool return_of_signature(Walk* me, FunctionImpl const* fn, Place place, NearKind kind, bool report_fallback) {
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
    Span at = function_error_span(fn);
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
        if (return_of_signature(me, pair.getter, place, NEAR_GET_ACCESSOR, true)) report_accessor(me, pair, false, place);
        return;
    }
    if (of_setter) report_accessor(me, pair, true, place);
}

// tsc's isOptionalParameter: `?`, or an initializer with no required parameter after it
static bool is_optional_parameter(FunctionImpl const* fn, FunctionArg const* arg) {
    if (arg->explicit_optional) return true;
    if (arg->initializer.value.tag == EXPR_IMPLICIT_ANY) return false;
    bool after = false;
    for_each (i, vec_function_arg, &fn->args) {
        if (i == arg) {
            after = true;
            continue;
        }
        if (!after) continue;
        bool initialized = i->initializer.value.tag != EXPR_IMPLICIT_ANY;
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
    bool initialized = arg->initializer.value.tag != EXPR_IMPLICIT_ANY;
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
    Expr const* init = &arg->initializer.value;
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

// typeFromFunctionLikeExpression: never falls back, its parts report
static void type_of_function_expression(Walk* me, FunctionImpl const* fn, Place place) {
    return_of_signature(me, fn, place, NEAR_FUNCTION_EXPRESSION, true);
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
                return_of_signature(me, fn, inner, NEAR_METHOD, true);
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
                        if (return_of_signature(me, fn, inner, NEAR_GET_ACCESSOR, true)) report_accessor(me, pair, false, inner);
                    } else {
                        report_accessor(me, pair, true, inner);
                        if (pair.getter && return_of_signature(me, pair.getter, inner, NEAR_GET_ACCESSOR, true)) {
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

static bool statement_visible(Walk* me, Statement const* statement, Container container, Symbol const* symbol) {
    if (container.global || container.ambient) return true;
    if (statement_declares_name(statement->tag) && statement->decl.props.exported != DECL_EXPORTED_NO) return true;
    return container.module_top_level && is_exported_local(me, symbol);
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

// typeFromVariable
static void check_variable(Walk* me, InitializedBinding const* decl, bool is_const) {
    Binding const* binding = &decl->binding;
    Expr const* init = &decl->value;
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
    if (binding->has_explicit_type) return;
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

static void check_function_declaration(Walk* me, FunctionImpl const* fn) {
    if (is_overload_implementation(fn)) return;
    check_parameters(me, fn, (Place){0}, true);
    return_of_signature(me, fn, (Place){0}, NEAR_FUNCTION_DECLARATION, true);
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
static void check_field(Walk* me, ClassItem const* item) {
    if (item->access == ACCESS_MODIFIER_PRIVATE) return;
    Expr const* init = &item->field.value;
    bool initialized = init->tag != EXPR_IMPLICIT_ANY;
    Near near = { .kind = NEAR_PROPERTY, .span = item->field.name_span, .name = item->field.name_span.sv };
    if (item->field.is_readonly && initialized && is_literal_const_field(item)) {
        if (!is_primitive_literal_value(init, true)) report_declaration(me, near);
        return;
    }
    if (item->field.has_explicit_type) return;
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

static void check_class(Walk* me, ClassDecl const* cls) {
    for_each (i, vec_class_item, &cls->items) {
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
                    return_of_signature(me, &i->method.func, (Place){0}, NEAR_METHOD, true);
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
                            check_parameter(me, ctor, j, (Place){0}, false);
                        }
                    }
                }
                if (i->access != ACCESS_MODIFIER_PRIVATE) check_parameters(me, ctor, (Place){0}, true);
                break;
            }
            case CLASS_ITEM_METHOD: {
                if (item_has_private_name(i)) break;
                FunctionImpl const* fn = &i->method.func;
                if (i->method.accessor == CLASS_ACCESSOR_NONE) {
                    if (i->access == ACCESS_MODIFIER_PRIVATE || is_class_overload_implementation(cls, fn)) break;
                    return_of_signature(me, fn, (Place){0}, NEAR_METHOD, true);
                    check_parameters(me, fn, (Place){0}, true);
                    break;
                }
                if (i->access == ACCESS_MODIFIER_PRIVATE) break;
                Accessors pair = class_accessors(i);
                if (i->method.accessor == CLASS_ACCESSOR_GET) {
                    find_setter(cls, i, &pair);
                    if (!fn->ret_explicit) type_of_accessor(me, pair, false, (Place){0});
                } else {
                    type_of_accessor(me, pair, true, (Place){0});
                }
                break;
            }
            case CLASS_ITEM_FIELD:
                if (item_has_private_name(i)) break;
                check_field(me, i);
                break;
            default: break;
        }
    }
    // A base that isn't a name is written as a variable of its type
    Expr const* base = &cls->extends;
    if (base->tag == EXPR_GENERIC_PARAMS) base = &base->generic_params->expr;
    bool is_null = base->tag == EXPR_LITERAL && base->literal.tag == VALUE_NULL;
    if (base->tag != EXPR_IMPLICIT_ANY && !is_null && !is_entity_name_expression(base)) {
        Place heritage = { .heritage = true };
        if (type_of_expression(me, base, heritage, false, false, false)) report_heritage(me, base->outer_span);
    }
}

// The members of an interface
static void check_interface(Walk* me, InterfaceDecl const* decl) {
    if (decl->type.tag != TYPE_INTERFACE) return;
    for_each (i, vec_interface_member, &decl->type.interface.members) {
        if (i->origin_interface && i->origin_interface != decl->symbol) continue;
        Span at = interface_member_error_span(i);
        if (i->key.tag == INTERFACE_KEY_CALLABLE && vec_span_size(&i->declarations)) {
            // (A signature's node ends after its `;` or `,`)
            at = *vec_span_at(&i->declarations, 0);
            if (span_valid(at) && (span_end_ptr(at)[0] == ';' || span_end_ptr(at)[0] == ',')) at.sv.size++;
        }
        if (i->key.tag == INTERFACE_KEY_EXPR && i->key.expr.ref) {
            Expr const* key = i->key.expr.ref;
            if (is_dynamic_name(key) && !is_global_symbol_reference(me, key) && !is_entity_name_expression(key)) {
                report(me, at, 9014, "Computed properties must be number or string literals, variables or dotted "
                    "expressions with --isolatedDeclarations.");
                continue;
            }
        }
        if (i->implicit_any) {
            // A property signature without a type: getDiagnostic has no case for it
            report(me, i->key.span, 9013, msg_expression);
            continue;
        }
        if (i->type.tag != TYPE_CALLABLE || !i->type.callable) continue;
        FunctionImpl const* fn = i->type.callable->function;
        if (!fn || fn->ret_explicit || fn->predicate) continue;
        if (fn->is_constructor) {
            Error* error = report(me, at, 9008,
                "Method must have an explicit return type annotation with --isolatedDeclarations.");
            add_related(error, (Near){ .kind = NEAR_FUNCTION_DECLARATION, .span = at });
        } else {
            report(me, at, 9013, msg_expression); // (method and call signatures: no case either)
        }
    }
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

static void check_statement(Walk* me, Statement const* statement, Container container) {
    switch (statement->tag) {
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
            if (statement_visible(me, statement, container, fn->symbol)) check_function_declaration(me, fn);
            return;
        }
        case STATEMENT_DECL_CLASS: {
            ClassDecl const* cls = statement->decl.class;
            if (statement_visible(me, statement, container, cls->symbol)) check_class(me, cls);
            return;
        }
        case STATEMENT_DECL_INTERFACE: {
            InterfaceDecl const* decl = statement->decl.interface;
            if (statement_visible(me, statement, container, decl->symbol)) check_interface(me, decl);
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
            check_statements(me, &decl->contents.statements, (Container){ .ambient = ambient });
            return;
        }
        case STATEMENT_EXPORT_DEFAULT_EXPR: {
            Expr const* expr = &statement->expr;
            if (expr->tag == EXPR_IDENTIFIER) return;
            Near near = { .kind = NEAR_EXPORT_ASSIGNMENT, .span = statement->span };
            if (type_of_expression(me, expr, place_at(near, true), false, false, true)) {
                report_expression(me, expr->outer_span, place_at(near, true), 0, nullptr);
            }
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
    if (!declaration_diagnostics_wanted(file) || !compiler_options()->isolated_declarations) return;
    Walk walk = {
        .file = file,
        .errs = errs,
        .checker = current_checker,
        .strict_null_checks = compiler_options()->strict_null_checks,
    };
    if (file->is_module) collect_exported_locals(&walk, &file->ast.statements);
    check_statements(&walk, &file->ast.statements, (Container){ .global = !file->is_module, .module_top_level = file->is_module });
    free(walk.exported_locals);
}
