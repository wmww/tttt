#include "ty/pseudo.h"
#include "ty/ty_internal.h"
#include "compiler_options.h"
#include "statement.h"
#include "class_decl.h"

// The builder: tsgo's pseudochecker/lookup.go, function by function (named after them). tsgo looks up the parents of a
// node to decide isContextuallyTyped and IsInConstContext; tttt has no parents, so the builder passes them down as a
// PseudoPlace from where it starts: an object literal's member knows its own (ObjectExprItem.pseudo_contextual and
// pseudo_const), a function knows whether it is contextual (FunctionImpl.syntactically_contextual)

typedef struct Build {
    Checker* checker;
    bool strict_null_checks;
} Build;

static Pseudo* make(Build* me, PseudoKind kind) {
    Pseudo* pseudo = arena_new(&my checker->arena, Pseudo);
    pseudo->kind = kind;
    return pseudo;
}

static Pseudo const* direct(Build* me, TypeNode const* node) {
    Pseudo* pseudo = make(me, PSEUDO_DIRECT);
    pseudo->node = node;
    return pseudo;
}

static Pseudo const* inferred(Build* me, Expr const* expr) {
    Pseudo* pseudo = make(me, PSEUDO_INFERRED);
    pseudo->expr = expr;
    return pseudo;
}

static Pseudo const* inferred_return(Build* me, FunctionImpl const* function) {
    Pseudo* pseudo = make(me, PSEUDO_INFERRED);
    pseudo->function = function;
    pseudo->signature_return = true;
    return pseudo;
}

// An Inferred with error nodes: `copy`'s expression (or signature) with `errors`
static Pseudo const* inferred_with(Build* me, Pseudo const* copy, PseudoError const* errors, u32 count) {
    Pseudo* pseudo = make(me, PSEUDO_INFERRED);
    *pseudo = *copy;
    pseudo->kind = PSEUDO_INFERRED;
    PseudoError* list = arena_new_n(&my checker->arena, PseudoError, count);
    memcpy(list, errors, sizeof(PseudoError) * count);
    pseudo->errors = list;
    pseudo->count = count;
    return pseudo;
}

static Pseudo const* inferred_expr_with(Build* me, Expr const* expr, PseudoError const* errors, u32 count) {
    Pseudo base = { .kind = PSEUDO_INFERRED, .expr = expr };
    return inferred_with(me, &base, errors, count);
}

static Pseudo const* no_result(Build* me, PseudoDeclKind kind) {
    Pseudo* pseudo = make(me, PSEUDO_NO_RESULT);
    pseudo->decl_kind = kind;
    return pseudo;
}

static Pseudo const* keyword(Build* me, PseudoKind kind) { return make(me, kind); }

static Pseudo const* maybe_const(Build* me, Expr const* at, Pseudo const* const_type, Pseudo const* regular_type, bool in_const) {
    Pseudo* pseudo = make(me, PSEUDO_MAYBE_CONST);
    pseudo->in_const = in_const;
    pseudo->at = at;
    pseudo->const_type = const_type;
    pseudo->regular_type = regular_type;
    return pseudo;
}

static Pseudo const* literal(Build* me, PseudoKind kind, Expr const* expr) {
    Pseudo* pseudo = make(me, kind);
    pseudo->expr = expr;
    return pseudo;
}

// "Fell back without saying why": the caller tries something else (NoResult) instead
static bool inferred_plainly(Pseudo const* pseudo) { return pseudo->kind == PSEUDO_INFERRED && !pseudo->count; }

static Pseudo const* type_from_expression(Build* me, Expr const* expr, PseudoPlace place);
static Pseudo const* create_return_from_signature(Build* me, FunctionImpl const* function, PseudoPlace place);
static PseudoParam const* clone_parameters(Build* me, FunctionImpl const* function, PseudoPlace place, u32* count);
static Pseudo const* type_from_accessor(Build* me, FunctionImpl const* getter, FunctionImpl const* setter, bool of_setter);

static bool is_const_type(TypeNode const* type) {
    return type->tag == TYPE_IDENTIFIER && span_equals(type->identifier->name, "const") &&
        !identifier_type_namespace_count(type->identifier) && !vec_spanned_type_size(&type->identifier->generic_params);
}

static bool is_numeric_literal(Expr const* expr) { return expr->tag == EXPR_LITERAL && expr->literal.tag == VALUE_NUMBER; }
static bool is_bigint_literal(Expr const* expr) { return expr->tag == EXPR_LITERAL && expr->literal.tag == VALUE_BIGINT; }

// isPrimitiveLiteralValue
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
            if (expr_is_perenthisized(operand)) return false;
            if (expr->unary_op->op == UNARY_OP_MINUS) return is_numeric_literal(operand) || (include_bigint && is_bigint_literal(operand));
            return expr->unary_op->op == UNARY_OP_PLUS && is_numeric_literal(operand);
        }
        default: return false;
    }
}

// The written signature of a function's JSDoc `@type` (tsgo's FullSignature)
static TypeNode const* full_signature(FunctionImpl const* function) {
    return function_impl_rare(function)->jsdoc_signature_type;
}

// typeFromFunctionLikeExpression
static Pseudo const* type_from_function_like_expression(Build* me, FunctionImpl const* function, PseudoPlace place) {
    if (full_signature(function)) return direct(me, full_signature(function));
    // (A function isn't a const context for what is in it)
    place.in_const = false;
    Pseudo* pseudo = make(me, PSEUDO_SIGNATURE);
    pseudo->function = function;
    pseudo->ret = create_return_from_signature(me, function, place);
    u32 count = 0;
    pseudo->params = clone_parameters(me, function, place, &count);
    pseudo->count = count;
    return pseudo;
}

// typeFromTypeAssertion
static Pseudo const* type_from_type_assertion(Build* me, Expr const* expr, PseudoPlace place) {
    AsTypeExpr const* as = expr->as_type;
    if (is_const_type(&as->type)) {
        place.in_const = true;
        return type_from_expression(me, &as->expr, place);
    }
    return direct(me, &as->type);
}

// typeFromPrimitiveLiteralPrefix: `-1` as written, `+1` as its operand
static Pseudo const* type_from_primitive_literal_prefix(Build* me, Expr const* expr, PseudoPlace place) {
    Expr const* operand = &expr->unary_op->expr;
    Expr const* shown = expr->unary_op->op == UNARY_OP_PLUS ? operand : expr;
    if (is_bigint_literal(operand)) {
        return maybe_const(me, expr, literal(me, PSEUDO_BIGINT_LITERAL, shown), keyword(me, PSEUDO_BIGINT), place.in_const);
    }
    return maybe_const(me, expr, literal(me, PSEUDO_NUMERIC_LITERAL, shown), keyword(me, PSEUDO_NUMBER), place.in_const);
}

// typeFromArrayLiteral, with canGetTypeFromArrayLiteral
static Pseudo const* type_from_array_literal(Build* me, Expr const* expr, PseudoPlace place) {
    if (!place.in_const) {
        PseudoError error = { .kind = PSEUDO_ERROR_ARRAY, .expr = expr };
        return inferred_expr_with(me, expr, &error, 1);
    }
    for_each (i, vec_expr, &expr->array) {
        if (i->tag != EXPR_SPREAD) continue;
        PseudoError error = { .kind = PSEUDO_ERROR_SPREAD_ELEMENT, .expr = i };
        return inferred_expr_with(me, expr, &error, 1);
    }
    // (An `as const` array with a contextual type may or may not be readonly)
    if (place.contextual) return inferred(me, expr);
    u32 count = (u32)vec_expr_size(&expr->array);
    Pseudo const** types = arena_new_n(&my checker->arena, Pseudo const*, count ? count : 1);
    u32 n = 0;
    for_each (i, vec_expr, &expr->array) types[n++] = type_from_expression(me, i, place);
    Pseudo* pseudo = make(me, PSEUDO_TUPLE);
    pseudo->types = types;
    pseudo->count = count;
    return pseudo;
}

// The accessor pair an object literal's accessor belongs to (tsgo's GetAllAccessorDeclarationsForDeclaration through
// the literal's symbol): the first getter and setter of its name. `first`: the item is the first of them
static void object_accessors(ObjectExpr const* object, ObjectExprItem const* item, ObjectExprItem const** getter,
    ObjectExprItem const** setter, bool* first
) {
    *getter = *setter = nullptr;
    *first = false;
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
        if (!*getter && !*setter) *first = i == item;
        if (i->tag == OBJECT_ITEM_GETTER && !*getter) *getter = i;
        if (i->tag == OBJECT_ITEM_SETTER && !*setter) *setter = i;
    }
}

static TypeNode const* setter_annotation(FunctionImpl const* setter) {
    if (!setter || !vec_function_arg_size(&setter->args)) return nullptr;
    // (Not its `this` parameter: the first one, as tsgo's getTypeAnnotationFromAccessor takes)
    FunctionArg const* arg = vec_function_arg_at(&setter->args, 0);
    return arg->initializer.binding.has_explicit_type ? binding_type(&arg->initializer.binding) : nullptr;
}

static TypeNode const* getter_annotation(FunctionImpl const* getter) {
    return getter && getter->ret_explicit && !getter->predicate ? &getter->ret : nullptr;
}

// getAccessorMember
static bool get_accessor_member(Build* me, ObjectExpr const* object, ObjectExprItem const* item, PseudoPlace place,
    PseudoMember* out
) {
    ObjectExprItem const* getter = nullptr;
    ObjectExprItem const* setter = nullptr;
    bool first = false;
    object_accessors(object, item, &getter, &setter, &first);
    FunctionImpl const* get = getter ? getter->value.function : nullptr;
    FunctionImpl const* set = setter ? setter->value.function : nullptr;
    FunctionImpl const* accessor = item->value.function;
    *out = (PseudoMember){ .item = item, .function = accessor };
    if (get && get->ret_explicit && set && setter_annotation(set)) {
        // Both are annotated: they may say different types, both are kept
        if (item->tag == OBJECT_ITEM_GETTER) {
            out->kind = PSEUDO_MEMBER_GETTER;
            out->type = type_from_accessor(me, get, set, false);
        } else {
            out->kind = PSEUDO_MEMBER_SETTER;
            u32 count = 0;
            out->params = clone_parameters(me, accessor, place, &count);
            out->param_count = count ? 1 : 0;
        }
        return true;
    }
    if (!first) return false;
    // Only one is annotated: a property, `readonly` for a lone getter
    out->kind = PSEUDO_MEMBER_PROPERTY;
    out->type = type_from_accessor(me, get, set, item->tag == OBJECT_ITEM_SETTER);
    out->readonly = item->tag == OBJECT_ITEM_GETTER && !set;
    return true;
}

// typeFromObjectLiteral, with canGetTypeFromObjectLiteral
static Pseudo const* type_from_object_literal(Build* me, Expr const* expr, PseudoPlace place) {
    ObjectExpr const* object = &expr->object;
    u32 error_count = 0;
    PseudoError* errors = nullptr;
    for_each (i, vec_object_expr_item, object) {
        PseudoError error = { .item = i };
        if (i->tag == OBJECT_ITEM_SHORTHAND || i->tag == OBJECT_ITEM_SPREAD || object_item_is_private_name(i)) {
            error.kind = PSEUDO_ERROR_OBJECT_MEMBER;
        } else if (i->computed_key && !is_primitive_literal_value(i->computed_key, false)) {
            error.kind = PSEUDO_ERROR_COMPUTED_NAME;
        } else {
            continue;
        }
        if (!errors) errors = arena_new_n(&my checker->arena, PseudoError, vec_object_expr_item_size(object));
        errors[error_count++] = error;
    }
    if (error_count) return inferred_expr_with(me, expr, errors, error_count);
    u32 count = 0;
    PseudoMember* members = arena_new_n(&my checker->arena, PseudoMember, vec_object_expr_item_size(object) + 1);
    for_each (i, vec_object_expr_item, object) {
        PseudoMember* member = &members[count];
        switch (i->tag) {
            case OBJECT_ITEM_METHOD: {
                FunctionImpl const* function = i->value.function;
                bool optional = i->optional_method;
                if (full_signature(function)) {
                    *member = (PseudoMember){ .kind = PSEUDO_MEMBER_PROPERTY, .item = i, .optional = optional,
                        .type = direct(me, full_signature(function)) };
                } else {
                    PseudoPlace inner = { .contextual = place.contextual };
                    u32 param_count = 0;
                    PseudoParam const* params = clone_parameters(me, function, inner, &param_count);
                    *member = (PseudoMember){ .kind = PSEUDO_MEMBER_METHOD, .item = i, .optional = optional, .function = function,
                        .params = params, .param_count = param_count, .type = create_return_from_signature(me, function, inner) };
                }
                count++;
                break;
            }
            case OBJECT_ITEM_PROPERTY:
                *member = (PseudoMember){ .kind = PSEUDO_MEMBER_PROPERTY, .item = i, .optional = i->question_offset != 0,
                    .type = type_from_expression(me, &i->value, place) };
                count++;
                break;
            case OBJECT_ITEM_GETTER:
            case OBJECT_ITEM_SETTER: {
                PseudoPlace inner = { .contextual = place.contextual };
                if (get_accessor_member(me, object, i, inner, member)) count++;
                break;
            }
            default: break;
        }
    }
    Pseudo* pseudo = make(me, PSEUDO_OBJECT);
    pseudo->expr = expr;
    pseudo->in_const = place.in_const;
    pseudo->members = members;
    pseudo->count = count;
    return pseudo;
}

// typeFromExpression: tsgo's checkExpression for pseudo types
static Pseudo const* type_from_expression(Build* me, Expr const* expr, PseudoPlace place) {
    switch (expr->tag) {
        case EXPR_IMPLICIT_ANY: // (An omitted expression of an array literal)
            return expr->missing ? inferred(me, expr) : keyword(me, PSEUDO_UNDEFINED);
        case EXPR_LITERAL:
            switch (expr->literal.tag) {
                case VALUE_UNDEFINED: return keyword(me, PSEUDO_UNDEFINED); // (The `undefined` identifier)
                case VALUE_NULL: return keyword(me, PSEUDO_NULL);
                case VALUE_BOOLEAN:
                    return maybe_const(me, expr, keyword(me, expr->literal.boolean ? PSEUDO_TRUE : PSEUDO_FALSE),
                        keyword(me, PSEUDO_BOOLEAN), place.in_const);
                case VALUE_NUMBER:
                    return maybe_const(me, expr, literal(me, PSEUDO_NUMERIC_LITERAL, expr), keyword(me, PSEUDO_NUMBER), place.in_const);
                case VALUE_STRING:
                    return maybe_const(me, expr, literal(me, PSEUDO_STRING_LITERAL, expr), keyword(me, PSEUDO_STRING), place.in_const);
                case VALUE_BIGINT:
                    return maybe_const(me, expr, literal(me, PSEUDO_BIGINT_LITERAL, expr), keyword(me, PSEUDO_BIGINT), place.in_const);
                default: return inferred(me, expr);
            }
        case EXPR_FUNCTION:
            // (A class method's FunctionImpl isn't an expression; an object literal's method is one but is only seen as
            // a member)
            return type_from_function_like_expression(me, expr->function, place);
        case EXPR_AS_TYPE:
            if (expr->as_type->is_satisfies) return inferred(me, expr);
            return type_from_type_assertion(me, expr, place);
        case EXPR_UNARY_OP:
            if (is_primitive_literal_value(expr, true)) return type_from_primitive_literal_prefix(me, expr, place);
            return inferred(me, expr);
        case EXPR_ARRAY: return type_from_array_literal(me, expr, place);
        case EXPR_OBJECT: return type_from_object_literal(me, expr, place);
        case EXPR_CLASS_DECL: {
            PseudoError error = { .kind = PSEUDO_ERROR_CLASS, .expr = expr };
            return inferred_expr_with(me, expr, &error, 1);
        }
        case EXPR_TEMPLATE:
            if (expr->template_->tag) return inferred(me, expr);
            // (A template literal with holes isn't a literal type the pseudochecker knows)
            if (place.in_const) return inferred(me, expr);
            return maybe_const(me, expr, inferred(me, expr), keyword(me, PSEUDO_STRING), place.in_const);
        default: return inferred(me, expr);
    }
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

// typeFromSingleReturnExpression: a function whose body is an expression, or has one `return` directly in its body
static Pseudo const* type_from_single_return_expression(Build* me, FunctionImpl const* function, PseudoPlace place) {
    Expr const* candidate = nullptr;
    if (function->has_body && !function->body_missing) {
        // (getFunctionFlags & FunctionFlags.AsyncGenerator: either flag)
        if (function->is_async || function->is_generator) return inferred_return(me, function);
        if (function->expression_body) {
            Statement const* ret = vec_statement_at(&function_impl_body(function)->statements, 0);
            if (ret && ret->tag == STATEMENT_RETURN_EXPR) candidate = &ret->expr;
        } else {
            // (forEachReturnStatement in source order: a `return` that isn't directly in the body, or a second one,
            // means none; a first bare `return;` leaves the candidate for the next)
            for_each (i, vec_statement, &function_impl_body(function)->statements) {
                if (i->tag == STATEMENT_RETURN_EXPR) {
                    Expr const* expr = statement_is_bare_return(i) ? nullptr : &i->expr;
                    if (candidate) {
                        candidate = nullptr;
                        break;
                    }
                    candidate = expr;
                    continue;
                }
                if (has_return(i)) {
                    candidate = nullptr;
                    break;
                }
            }
        }
    }
    if (candidate) {
        if (place.contextual) {
            // Only a type assertion's type can be relied on
            if (candidate->tag == EXPR_AS_TYPE && !candidate->as_type->is_satisfies && !expr_is_perenthisized(candidate) &&
                !is_const_type(&candidate->as_type->type)
            ) {
                return direct(me, &candidate->as_type->type);
            }
        } else {
            return type_from_expression(me, candidate, (PseudoPlace){0});
        }
    }
    return inferred_return(me, function);
}

// isValueSignatureDeclaration: a function with a body (or that could have one)
static bool is_value_signature(FunctionImpl const* function) { return !function->is_type; }

// createReturnFromSignature: never null
static Pseudo const* create_return_from_signature(Build* me, FunctionImpl const* function, PseudoPlace place) {
    if (function->predicate && !function->predicate->jsdoc) {
        // (The predicate node, tsgo's Direct type predicate)
        Pseudo* pseudo = make(me, PSEUDO_DIRECT);
        pseudo->predicate = true;
        pseudo->function = function;
        return pseudo;
    }
    if (function->ret_explicit) return direct(me, &function->ret);
    if (is_value_signature(function)) return type_from_single_return_expression(me, function, place);
    Pseudo* pseudo = (Pseudo*)no_result(me, PSEUDO_DECL_FUNCTION);
    pseudo->function = function;
    return pseudo;
}

// typeFromAccessor: the annotation of either accessor (this one's first), else the getter's single returned expression
static Pseudo const* type_from_accessor(Build* me, FunctionImpl const* getter, FunctionImpl const* setter, bool of_setter) {
    TypeNode const* annotation = of_setter ? setter_annotation(setter) : getter_annotation(getter);
    if (!annotation) annotation = of_setter ? getter_annotation(getter) : setter_annotation(setter);
    if (annotation) return direct(me, annotation);
    if (getter) {
        Pseudo const* ret = create_return_from_signature(me, getter, (PseudoPlace){ .contextual = getter->syntactically_contextual });
        if (inferred_plainly(ret)) {
            // (The error moves up to the accessors)
            PseudoError errors[2] = { { .kind = PSEUDO_ERROR_ACCESSOR, .function = getter },
                { .kind = PSEUDO_ERROR_ACCESSOR, .function = setter } };
            ret = inferred_with(me, ret, errors, setter ? 2 : 1);
        }
        return ret;
    }
    Pseudo* pseudo = (Pseudo*)no_result(me, PSEUDO_DECL_FUNCTION);
    pseudo->function = of_setter ? setter : getter;
    return pseudo;
}

// isOptionalInitializedOrRestParameter
static bool parameter_not_required(FunctionArg const* arg) {
    return arg->rest || arg->explicit_optional || initialized_binding_value(&arg->initializer)->tag != EXPR_IMPLICIT_ANY;
}

// lastRequiredParamIndex: just past the last required parameter
static u32 last_required_index(FunctionImpl const* function) {
    for (u32 i = (u32)vec_function_arg_size(&function->args); i > 0; i--) {
        if (!parameter_not_required(vec_function_arg_at(&function->args, i - 1))) return i;
    }
    return 0;
}

// addUndefinedIfDefinitelyRequired
static Pseudo const* add_undefined_if_definitely_required(Build* me, Pseudo const* pseudo) {
    if (pseudo_could_be_undefined(pseudo)) return pseudo;
    return pseudo_add_undefined(me->checker, pseudo);
}

// The setter a setter's parameter belongs to has its pair's type: the getter of the same property
static FunctionImpl const* paired_getter(FunctionImpl const* setter) {
    FunctionArg const* arg = vec_function_arg_size(&setter->args) ? vec_function_arg_at(&setter->args, 0) : nullptr;
    if (!arg) return nullptr;
    FunctionImpl const* late = initialized_binding_rare(&arg->initializer)->late_setter;
    return late ? function_impl_late_getter(late) : initialized_binding_rare(&arg->initializer)->getter;
}

// typeFromParameterWorker
static Pseudo const* type_from_parameter_worker(Build* me, FunctionImpl const* function, FunctionArg const* arg, u32 index,
    u32 last_required, PseudoPlace place
) {
    if (function->accessor == 2) return type_from_accessor(me, paired_getter(function), function, true);
    bool required_after = index + 1 < last_required;
    Binding const* binding = &arg->initializer.binding;
    Expr const* init = initialized_binding_value(&arg->initializer);
    bool initialized = init->tag != EXPR_IMPLICIT_ANY;
    if (binding->has_explicit_type) {
        Pseudo const* pseudo = direct(me, binding_type(binding));
        // (An initialized parameter before a required one: tsgo's getTypeOfParameter adds optionality)
        if (me->strict_null_checks && initialized && required_after) return add_undefined_if_definitely_required(me, pseudo);
        return pseudo;
    }
    if (initialized && binding->tag == BINDING_IDENTIFIER && !place.contextual) {
        Pseudo const* pseudo = type_from_expression(me, init, (PseudoPlace){0});
        if (inferred_plainly(pseudo)) {
            // (The error moves up to the parameter)
            PseudoError error = { .kind = PSEUDO_ERROR_PARAMETER, .function = function, .arg = arg };
            pseudo = inferred_with(me, pseudo, &error, 1);
        }
        if (!me->strict_null_checks || !required_after) return pseudo;
        return add_undefined_if_definitely_required(me, pseudo);
    }
    Pseudo* pseudo = (Pseudo*)no_result(me, PSEUDO_DECL_PARAMETER);
    pseudo->function = function;
    pseudo->arg = arg;
    return pseudo;
}

// cloneParameters: with `this` (tsgo's parameter list has it)
static PseudoParam const* clone_parameters(Build* me, FunctionImpl const* function, PseudoPlace place, u32* count) {
    u32 n = (u32)vec_function_arg_size(&function->args);
    *count = n;
    if (!n) return nullptr;
    u32 last_required = last_required_index(function);
    PseudoParam* params = arena_new_n(&my checker->arena, PseudoParam, n);
    u32 index = 0;
    for_each (i, vec_function_arg, &function->args) {
        bool optional = i->explicit_optional;
        // (An initialized parameter is optional only if every one after it is: tsgo's isOptionalParameter)
        if (!optional && initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY) optional = index + 1 >= last_required;
        params[index] = (PseudoParam){ .arg = i, .rest = i->rest, .optional = optional,
            .type = type_from_parameter_worker(me, function, i, index, last_required, place) };
        index++;
    }
    return params;
}

static Build build_for(Checker* me) {
    return (Build){ .checker = me, .strict_null_checks = compiler_options()->strict_null_checks };
}

Pseudo const* pseudo_of_object_member(Checker* me, ObjectExprItem const* item) {
    Pseudo const* known = ty_link_get(me, KEY_PSEUDO, item);
    if (known) return known;
    Build build = build_for(me);
    PseudoPlace place = { .contextual = item->pseudo_contextual, .in_const = item->pseudo_const };
    Pseudo const* pseudo = nullptr;
    // typeFromPropertyAssignment (a JSDoc `@type` is the annotation)
    if (object_item_rare(item)->jsdoc_type) {
        pseudo = direct(&build, object_item_rare(item)->jsdoc_type);
    } else if (item->tag == OBJECT_ITEM_PROPERTY) {
        pseudo = type_from_expression(&build, &item->value, place);
        if (inferred_plainly(pseudo)) pseudo = nullptr;
    } else if ((item->tag == OBJECT_ITEM_METHOD || item->tag == OBJECT_ITEM_GETTER || item->tag == OBJECT_ITEM_SETTER) &&
        item->value.tag == EXPR_FUNCTION
    ) {
        // (A method's own type isn't asked for: its signature is printed. An accessor's is typeFromAccessor, see
        // pseudo_of_accessor())
        pseudo = type_from_function_like_expression(&build, item->value.function, place);
    }
    if (!pseudo) {
        Pseudo* none = (Pseudo*)no_result(&build, PSEUDO_DECL_OBJECT_MEMBER);
        none->item = item;
        pseudo = none;
    }
    ty_link_set(me, KEY_PSEUDO, item, pseudo);
    return pseudo;
}

Pseudo const* pseudo_of_variable(Checker* me, InitializedBinding const* binding, bool is_const, PseudoPlace place) {
    Build build = build_for(me);
    if (binding->binding.has_explicit_type) return direct(&build, binding_type(&binding->binding));
    Expr const* init = initialized_binding_value(binding);
    if (init->tag != EXPR_IMPLICIT_ANY && !place.contextual) {
        // (A const's template literal stays inferred, for template literal freshness later)
        if (!(is_const && init->tag == EXPR_TEMPLATE && !init->template_->tag)) {
            Pseudo const* pseudo = type_from_expression(&build, init, (PseudoPlace){0});
            if (!inferred_plainly(pseudo)) return pseudo;
        }
    }
    Pseudo* none = (Pseudo*)no_result(&build, PSEUDO_DECL_VARIABLE);
    none->binding = binding;
    return none;
}

Pseudo const* pseudo_of_parameter(Checker* me, FunctionImpl const* function, FunctionArg const* arg) {
    Pseudo const* known = ty_link_get(me, KEY_PSEUDO, arg);
    if (known) return known;
    Build build = build_for(me);
    u32 index = (u32)(arg - vec_function_arg_at(&function->args, 0));
    PseudoPlace place = { .contextual = function->syntactically_contextual };
    Pseudo const* pseudo = type_from_parameter_worker(&build, function, arg, index, last_required_index(function), place);
    ty_link_set(me, KEY_PSEUDO, arg, pseudo);
    return pseudo;
}

Pseudo const* pseudo_of_return(Checker* me, FunctionImpl const* function) {
    Pseudo const* known = ty_link_get(me, KEY_PSEUDO, function);
    if (known) return known;
    Build build = build_for(me);
    Pseudo const* pseudo = function->accessor == 1 ? type_from_accessor(&build, function, nullptr, false) :
        create_return_from_signature(&build, function, (PseudoPlace){ .contextual = function->syntactically_contextual });
    ty_link_set(me, KEY_PSEUDO, function, pseudo);
    return pseudo;
}

Pseudo const* pseudo_of_accessor(Checker* me, FunctionImpl const* accessor) {
    Build build = build_for(me);
    if (accessor->accessor == 2) return type_from_accessor(&build, paired_getter(accessor), accessor, true);
    return type_from_accessor(&build, accessor, nullptr, false);
}

Pseudo const* pseudo_of_accessors(Checker* me, FunctionImpl const* getter, FunctionImpl const* setter, bool of_setter) {
    Build build = build_for(me);
    return type_from_accessor(&build, getter, setter, of_setter);
}

// typeFromProperty (a class's property declarations are never contextually typed here: the walk asking for them only
// visits class declarations)
Pseudo const* pseudo_of_field(Checker* me, ClassItem const* field) {
    Build build = build_for(me);
    if (field->field.has_explicit_type) return direct(&build, &field->field.explicit_type);
    Expr const* init = &field->field.value;
    if (init->tag != EXPR_IMPLICIT_ANY) {
        // (A readonly field's template literal stays inferred, for template literal freshness later)
        if (!(field->field.is_readonly && init->tag == EXPR_TEMPLATE && !init->template_->tag)) {
            Pseudo const* pseudo = type_from_expression(&build, init, (PseudoPlace){0});
            if (!inferred_plainly(pseudo)) {
                // (`x? = v`: the initializer's type and undefined)
                if (pseudo->kind != PSEUDO_DIRECT && field->field.is_optional) return add_undefined_if_definitely_required(&build, pseudo);
                return pseudo;
            }
        }
    }
    Pseudo* none = (Pseudo*)no_result(&build, PSEUDO_DECL_FIELD);
    none->field = field;
    return none;
}

PseudoError pseudo_no_result_node(Pseudo const* pseudo) {
    switch (pseudo->decl_kind) {
        case PSEUDO_DECL_VARIABLE: return (PseudoError){ .kind = PSEUDO_ERROR_VARIABLE, .binding = pseudo->binding };
        case PSEUDO_DECL_PARAMETER:
            return (PseudoError){ .kind = PSEUDO_ERROR_PARAMETER, .function = pseudo->function, .arg = pseudo->arg };
        case PSEUDO_DECL_OBJECT_MEMBER: return (PseudoError){ .kind = PSEUDO_ERROR_OBJECT_MEMBER, .item = pseudo->item };
        case PSEUDO_DECL_FUNCTION: return (PseudoError){ .kind = PSEUDO_ERROR_FUNCTION, .function = pseudo->function };
        case PSEUDO_DECL_FIELD: return (PseudoError){ .kind = PSEUDO_ERROR_FIELD, .field = pseudo->field };
    }
    impossible;
}

Pseudo const* pseudo_of_expression(Checker* me, Expr const* expr, PseudoPlace place) {
    Build build = build_for(me);
    return type_from_expression(&build, expr, place);
}

// isUndefinedPseudoType
static bool is_undefined(Pseudo const* pseudo) {
    return pseudo->kind == PSEUDO_UNDEFINED || (pseudo->kind == PSEUDO_MAYBE_CONST && is_undefined(pseudo->const_type));
}

// typeNodeCouldReferToUndefined: whether a type node may mean something with undefined in it, by its syntax
static bool node_could_be_undefined(TypeNode const* node) {
    switch (node->tag) {
        case TYPE_IDENTIFIER: return !node->identifier->keyword; // (A reference, an import type; not `this` or `object`)
        case TYPE_INDEXED:
        case TYPE_TYPEOF:
        case TYPE_TERNARY:
        case TYPE_KEYOF: // (Type operators)
        case TYPE_UNIQUE_SYMBOL:
        case TYPE_PREDICATE:
            return true;
        case TYPE_ARRAY: return node->array.readonly; // (`readonly T[]` is a type operator)
        case TYPE_TUPLE: return node->tuple.readonly;
        case TYPE_VALUE: return node->value.tag == VALUE_UNDEFINED;
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                if (!i->placeholder && node_could_be_undefined(i)) return true;
            }
            return false;
        default: return false;
    }
}

bool pseudo_could_be_undefined(Pseudo const* pseudo) {
    if (pseudo->kind == PSEUDO_NO_RESULT || pseudo->kind == PSEUDO_INFERRED || is_undefined(pseudo)) return true;
    if (pseudo->kind == PSEUDO_MAYBE_CONST) return pseudo_could_be_undefined(pseudo->regular_type);
    // (A predicate is a possibly-undefined referencing node to tsgo)
    if (pseudo->kind == PSEUDO_DIRECT) return pseudo->predicate || node_could_be_undefined(pseudo->node);
    if (pseudo->kind == PSEUDO_UNION) {
        for (u32 i = 0; i < pseudo->count; i++) {
            if (pseudo_could_be_undefined(pseudo->types[i])) return true;
        }
    }
    return false;
}

Pseudo const* pseudo_add_undefined(Checker* me, Pseudo const* pseudo) {
    Pseudo const** types = arena_new_n(&my arena, Pseudo const*, 2);
    types[0] = pseudo;
    Pseudo* undefined = arena_new(&my arena, Pseudo);
    undefined->kind = PSEUDO_UNDEFINED;
    types[1] = undefined;
    Pseudo* both = arena_new(&my arena, Pseudo);
    both->kind = PSEUDO_UNION;
    both->types = types;
    both->count = 2;
    return both;
}

bool pseudo_is_structural(Pseudo const* pseudo) {
    switch (pseudo->kind) {
        case PSEUDO_OBJECT:
        case PSEUDO_TUPLE:
        case PSEUDO_SIGNATURE: return true;
        case PSEUDO_MAYBE_CONST: return pseudo_is_structural(pseudo->const_type) || pseudo_is_structural(pseudo->regular_type);
        default: return false;
    }
}

bool pseudo_is_entity_name(Expr const* expr) {
    if (expr_is_perenthisized(expr)) return false;
    if (expr->tag == EXPR_IDENTIFIER) return !expr->identifier.keyword;
    if (expr->tag != EXPR_PROPERTY || expr->property->form != PROPERTY_EXPR_DOT) return false;
    return !expr->property->optional_chain && pseudo_is_entity_name(&expr->property->expr);
}

static void report(PseudoReporter const* reporter, PseudoError error) {
    if (reporter) reporter->report(reporter->ctx, &error);
}

void pseudo_report_inferred(PseudoReporter const* reporter, Pseudo const* pseudo) {
    if (!reporter) return;
    for (u32 i = 0; i < pseudo->count; i++) report(reporter, pseudo->errors[i]);
    if (pseudo->count) return;
    if (pseudo->signature_return) {
        report(reporter, (PseudoError){ .kind = PSEUDO_ERROR_FUNCTION, .function = pseudo->function });
    } else {
        report(reporter, (PseudoError){ .kind = PSEUDO_ERROR_EXPRESSION, .expr = pseudo->expr });
    }
}

void pseudo_report_inferred_node(PseudoReporter const* reporter, Pseudo const* pseudo, ObjectExprItem const* property,
    FunctionImpl const* function
) {
    if (!reporter) return;
    if (!pseudo->count && !pseudo->signature_return && pseudo_is_entity_name(pseudo->expr)) {
        // (A property assignment reports its initializer as an expression; an arrow function, its return type)
        if (property) {
            report(reporter, (PseudoError){ .kind = PSEUDO_ERROR_OBJECT_MEMBER, .item = property });
            return;
        }
        if (function) {
            report(reporter, (PseudoError){ .kind = PSEUDO_ERROR_FUNCTION, .function = function });
            return;
        }
    }
    pseudo_report_inferred(reporter, pseudo);
}
