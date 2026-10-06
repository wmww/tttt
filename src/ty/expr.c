#include "source_file.h"
#include "ty/ty_internal.h"
#include "statement.h"
#include "flow.h"
#include "binding.h"
#include "compiler_options.h"
#include "program.h"
#include "class_decl.h"
#include "scope.h"
#include "error.h"

// The types of expressions. ty_check_expr(expr, contextual) is the one entry point: whoever knows
// the contextual type of an expression (a declaration with a type annotation, a call resolving its arguments, an
// object literal checking its members, a return statement...) checks it with that type *before* anything else asks,
// and the result is the expression's memo. Everything else reads the memo through ty_expr_type(), which checks
// without a contextual type only if nobody got there first. That works because an expression's type is only ever
// demanded by its parent or by the declaration it initializes, and the typecheck pass walks top down.
//
// Like tsc, a function expression takes its parameter types from the first contextual signature it meets and keeps
// them (KEY_CONTEXT_PARAM links, read back by the parameter's own lazy type), while object and array literals are
// simply checked again when the contextual type changes (overload candidates, inference passes).
//
// Literal types that come out of an expression are fresh (TYF_FRESH_LITERAL, ty_fresh_literal()): they widen at a
// mutable location, and ty_regular() gives the regular twin, like tsc's fresh/regular literal type pairs.

static const LowerCtx expr_lower_ctx = { .generic = true };

typedef struct ExprCtx {
    Ty const* contextual; // may be null
    // While a call infers: the contextual type as a contextual signature sees it (tsc's ContextFlags.Signature, which
    // instantiates every level with the non-fixing mapper), when that differs from `contextual`
    Ty const* sig_contextual;
    // While a call infers: that view has no contextual type here though `contextual` has one (a property of a literal
    // whose contextual type is a type parameter with a default: `{}` under the non-fixing mapper, its constraint as
    // `contextual`). A function here has no contextual signature
    bool sig_absent;
    bool is_const; // inside `as const`
    bool template_context; // The index of an element access (a template literal there is a template type)
    // Context-sensitive functions are ty_any_function_type(), whatever contains one isn't remembered (tsc's
    // CheckMode.SkipContextSensitive: the first inference pass sees the rest of an object literal argument)
    bool skip_sensitive;
    GenericFunctionMode* generic_mode; // See GenericFunctionMode: the members of literal arguments
    // While a call infers: `contextual` before its top-level instantiation (what a nested call resolves with: tsc's
    // resolveCall takes getContextualType(), and instantiates it with the outer mappers itself)
    Ty const* raw_contextual;
} ExprCtx;

static Ty const* compute(Checker* me, Expr const* expr, ExprCtx ctx);
static Ty const* check_template(Checker* me, Expr const* expr, ExprCtx ctx);

static Ty const* any_type(Checker* me) { return ty_intrinsic(me, TY_ANY); }

// A property access that found nothing: tsc's errorType where it reports the missing property (an object type that
// isn't `typeof globalThis`), else any. HACK: approximates the reporting conditions of ty/check.c (JS files, unresolved
// bases and whatever else tttt doesn't know the members of stay any)
static Ty const* missing_property_type(Checker* me, Expr const* expr, Ty const* object) {
    if (object->kind == TY_TYPE_PARAM && object->param.state->is_this) object = ty_apparent_type(me, object);
    // Nothing but null and undefined: an error of its own and the error type (tsc's checkNonNullType)
    if ((object->flags & TYF_NULLABLE) && (ty_non_nullable(me, object)->flags & (TYF_NULLABLE | TYF_NEVER))) {
        return ty_intrinsic(me, TY_ERROR);
    }
    // A primitive's members are its apparent type's (`true.e` is reported against Boolean: the error type)
    if (!(object->flags & (TYF_OBJECT | TYF_ANY | TYF_UNKNOWN | TYF_NEVER | TYF_NULLABLE | TYF_TYPE_PARAM)) &&
        (object->kind == TY_LITERAL || (object->kind == TY_INTRINSIC &&
            (object->flags & (TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_ES_SYMBOL | TYF_BOOLEAN))))
    ) {
        object = ty_apparent_type(me, object);
    }
    bool global_this = object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_NAMESPACE &&
        object->declared.namespace_symbol && object->declared.namespace_symbol->is_global_this;
    if ((object->flags & TYF_ANY) || global_this || ty_has_unresolved_base(me, object)) return any_type(me);
    // What a JavaScript object literal made has every property, except in a JavaScript file that isn't checked (tsc's
    // checkPropertyAccessExpressionOrQualifiedName: isUncheckedJSSuggestion makes it a suggestion and errorType)
    SourceFile const* file = expr_outer_span(expr).source;
    bool unchecked_js = file && file->is_js && file->unchecked;
    // (Not when what it's a property of was declared in another global file: approximated by the object's name)
    Expr const* object_expr = &expr->property->expr;
    if (unchecked_js && object_expr->tag == EXPR_IDENTIFIER && object_expr->identifier.target &&
        object_expr->identifier.target->decl_count
    ) {
        SourceFile const* declared = object_expr->identifier.target->decls[0].full.source;
        if (declared && declared != file && !declared->is_module) unchecked_js = false;
    }
    if (!unchecked_js && ty_is_js_literal_type(me, object)) return any_type(me);
    // tsc reports TS2339 right here, and printing the type in the message can find a return type in progress (`this.x`
    // in an object literal method: TS7023). HACK: print it too while something is being resolved (tttt reports the
    // error later, from ty/check.c), see Checker.print_probe
    if (my resolving_len > my resolving_start && !my print_probe) {
        my print_probe = true;
        cstr printed = ty_to_cstr(me, object);
        cstr_drop(&printed);
        my print_probe = false;
    }
    return ty_intrinsic(me, TY_ERROR);
}

// Memo

static bool kind_uses_context(Expr const* expr) {
    switch (expr->tag) {
        case EXPR_OBJECT:
        case EXPR_ARRAY:
        case EXPR_FUNCTION:
        case EXPR_TERNARY:
        case EXPR_AS_TYPE:
        case EXPR_TEMPLATE:
        case EXPR_JSX_ATTRIBUTES:
            return true;
        case EXPR_UNARY_OP:
            // (A `yield`'s contextual type decides TS7057: one first evaluated without it is checked again with it)
            return expr->unary_op->op == UNARY_OP_RIGHT_BANG || expr->unary_op->op == UNARY_OP_AWAIT ||
                expr->unary_op->op == UNARY_OP_YIELD;
        case EXPR_BINARY_OP:
            switch (expr->binary_op->op) {
                case BINARY_OP_OR_OR:
                case BINARY_OP_AND_AND:
                case BINARY_OP_EH_EH:
                case BINARY_OP_COMMA:
                case BINARY_OP_EQUAL:
                    return true;
                default:
                    return false;
            }
        default:
            return false;
    }
}

Ty const* ty_expr_type(Checker* me, Expr const* expr) {
    LazyType* lazy = (LazyType*)&expr->lazy_type;
    if (lazy->tag != LAZY_TYPE_EXPR || !lazy->slot) return ty_compute_expr_without_context(me, expr); // implicit any and friends
    if (my flow_uncached) {
        // For the flow engine (ty_flow_expr_type()): finished memos only, and nothing is remembered
        LazyMemo* memo = lazy_type_state_peek(lazy);
        return memo && memo->done ? memo->evaluated : ty_compute_expr_without_context(me, expr);
    }
    return lazy_type_get(lazy); // Detects cycles
}

static bool has_generic_with_union_constraint(Checker* me, Ty const* ty);

// A reference whose type has a type variable with a union constraint is narrowed as the constraint where the
// contextual type isn't generic (see narrowable_type())
static bool identifier_uses_context(Checker* me, Expr const* expr) {
    // (A property access says so once it has been computed: access_flow_type())
    if (expr->tag == EXPR_PROPERTY) return intern_get(&my links, (u64[]){ KEY_ACCESS_USES_CONTEXT, (u64)(uintptr_t)expr }, 2) != nullptr;
    if (expr->tag != EXPR_IDENTIFIER || expr->identifier.keyword || !expr->identifier.target) return false;
    return has_generic_with_union_constraint(me, ty_identifier_reference_type(me, &expr->identifier));
}

// tsc's isValidConstAssertionArgument, as far as a const context changes what these check to
static bool is_const_context_kind(Expr const* expr) {
    switch (expr->tag) {
        case EXPR_LITERAL:
        case EXPR_TEMPLATE:
        case EXPR_ARRAY:
        case EXPR_OBJECT:
            return true;
        case EXPR_UNARY_OP:
            return expr->unary_op->op == UNARY_OP_MINUS || expr->unary_op->op == UNARY_OP_PLUS;
        default:
            return false;
    }
}

// tsc's isConstContext: inside `as const`, or the contextual type is a `const` type parameter (an argument of
// `f<const T>(t: T)`)
static bool in_const_context(Checker* me, Expr const* expr, ExprCtx ctx) {
    return ctx.is_const || (ctx.contextual && (ctx.contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) &&
        is_const_context_kind(expr) && ty_is_const_type_variable(me, ctx.contextual, 0));
}

static Ty const* check_with(Checker* me, Expr const* expr, ExprCtx ctx);
static bool has_context_sensitive_params(FunctionImpl const* function);

static _Thread_local struct { Expr const* data[64]; u32 len; bool overflow; } collected_returns;
static bool collect_return(Expr const* expr) {
    if (collected_returns.len == c_arraylen(collected_returns.data)) collected_returns.overflow = true;
    else collected_returns.data[collected_returns.len++] = expr;
    return false;
}

// A context-sensitive function checked while skipping context-sensitive arguments (tsc's returnOnlyType in
// checkFunctionExpressionOrObjectLiteralMethod): when its parameters aren't what makes it context sensitive (its
// returned expression is: `(): ... => x => x.length`, `() => ({ f: x => x })`) and the contextual return type could
// mention type parameters, a signature that only has the return type of the body, checked skipping context-sensitive
// parts too, so its other parts infer early. Non-inferrable: its (missing) parameters don't infer anything.
// Generators get tsc's anyFunctionType
static Ty const* return_only_type(Checker* me, FunctionImpl const* function, Ty const* contextual) {
    if (!contextual || function->ret_explicit || !function->has_body || function->is_generator ||
        has_context_sensitive_params(function)) {
        return ty_any_function_type(me);
    }
    Signature const* context_sig = ty_contextual_signature(me, contextual, function);
    if (!context_sig) return ty_any_function_type(me);
    signature_resolve(me, context_sig);
    Ty const* context_ret = signature_return_type(me, context_sig);
    if (!(context_ret->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return ty_any_function_type(me);
    u64 key[2] = { KEY_CONTEXT_FREE, (u64)(uintptr_t)function };
    void const* known = intern_get(&my links, key, 2);
    if (known) return known;

    // The returned expressions are collected first: checking them can get here for a nested function
    collected_returns.len = 0;
    collected_returns.overflow = false;
    statements_any_return(&function->body.statements, collect_return);
    if (collected_returns.overflow || !collected_returns.len) return ty_any_function_type(me);
    u32 count = collected_returns.len;
    Expr const* returned[c_arraylen(collected_returns.data)];
    memcpy(returned, collected_returns.data, count * sizeof(Expr const*));

    Ty const* context = function->is_async ? ty_async_contextual_awaited(me, context_ret) : context_ret;
    TyBuf types = {0};
    for (u32 i = 0; i < count; i++) {
        Ty const* type = check_with(me, returned[i], (ExprCtx){ .contextual = context, .skip_sensitive = true });
        ty_buf_push(&types, function->is_async ? ty_awaited_returned(me, type) : type);
    }
    FlowReachability end = ty_function_end_reachability(me, function);
    if (compiler_options()->strict_null_checks && (function->has_bare_return || end == FLOW_REACH_YES)) {
        ty_buf_push(&types, ty_intrinsic(me, TY_UNDEFINED));
    }
    Ty const* body = ty_widened_return_type(me, ty_union_subtype_reduced(me, types.data, types.len), context);
    ty_buf_drop(&types);
    if (function->is_async) body = ty_promise_of(me, function, body);

    Signature* sig = arena_new(&my arena, Signature);
    *sig = (Signature){
        .flags = SIGNATURE_NON_INFERRABLE, .state = LAZY_STATE_DONE, .ret_state = LAZY_STATE_DONE, .ret = body,
        .predicate_done = true,
    };
    Ty* type = (Ty*)ty_signature_type(me, sig);
    type->flags |= TYF_NON_INFERRABLE;
    *intern_slot(me, &my links, key, 2) = type;
    return type;
}

// Whether checking the expression may ask for a contextual signature: it is a function or has one where its contextual
// type reaches (what tsc asks getApparentTypeOfContextualType with ContextFlags.Signature for)
static bool may_want_signature(Expr const* expr) {
    switch (expr->tag) {
        case EXPR_FUNCTION: return true;
        case EXPR_OBJECT:
            for_each (i, vec_object_expr_item, &expr->object) {
                if (may_want_signature(&i->value)) return true;
            }
            return false;
        case EXPR_ARRAY:
            for_each (i, vec_expr, &expr->array) {
                if (may_want_signature(i)) return true;
            }
            return false;
        case EXPR_TERNARY:
            return may_want_signature(&expr->ternary->yes) || may_want_signature(&expr->ternary->no);
        case EXPR_BINARY_OP:
            return may_want_signature(&expr->binary_op->left) || may_want_signature(&expr->binary_op->right);
        case EXPR_JSX_ATTRIBUTES:
            for_each (i, vec_jsx_attribute, &expr->jsx_attributes) {
                if (i->tag == JSX_ATTRIBUTE_EXPR && may_want_signature(&i->value)) return true;
            }
            for_each (i, vec_jsx_child, &jsx_attributes_element(expr)->children) {
                if (i->tag == JSX_CHILD_EXPR && !i->spread && may_want_signature(&i->expr)) return true;
            }
            return false;
        default:
            return false;
    }
}

// The context of an operand whose contextual type is the expression's own (a conditional's branches): before its
// instantiation, which each operand does again (tsc's getContextualType recurses through such parents without
// instantiating, so a call there resolves with the type as written)
static ExprCtx passed_on_context(ExprCtx ctx) {
    if (ctx.raw_contextual && ctx.raw_contextual != ctx.contextual) {
        ctx.contextual = ctx.raw_contextual;
        ctx.sig_contextual = nullptr;
    }
    return ctx;
}

static Ty const* check_with(Checker* me, Expr const* expr, ExprCtx ctx) {
    // (tsc's getApparentTypeOfContextualType: every node's contextual type is instantiated at its top level with what the
    // call inferring now knows. The view a contextual signature sees only where one may be asked for: instantiating
    // creates types, which tsc wouldn't have yet)
    bool inferring = my outer_inference && ((ctx.contextual && (ctx.contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) ||
        ctx.sig_contextual);
    if (inferring) {
        Ty const* raw_sig = ctx.sig_absent ? nullptr : ctx.sig_contextual ? ctx.sig_contextual : ctx.contextual;
        ctx.raw_contextual = ctx.contextual;
        ctx.contextual = ty_instantiate_contextual_type(me, ctx.contextual);
        Ty const* sig_view = !raw_sig ? nullptr :
            may_want_signature(expr) ? ty_instantiate_contextual_type_for(me, raw_sig, true) : ctx.contextual;
        ctx.sig_contextual = sig_view != ctx.contextual ? sig_view : nullptr;
    } else {
        ctx.sig_contextual = nullptr;
        ctx.raw_contextual = ctx.contextual;
    }
    ctx.is_const = in_const_context(me, expr, ctx);
    if (ctx.skip_sensitive) {
        if (ty_expr_is_context_sensitive(expr)) {
            if (expr->tag == EXPR_FUNCTION) return return_only_type(me, expr->function, ctx.contextual);
            return compute(me, expr, ctx);
        }
        ctx.skip_sensitive = false;
    }
    if (ctx.generic_mode) {
        // Literals pass it on, and aren't remembered like this; anything else is a member it may apply to
        if (expr->tag == EXPR_ARRAY) return compute(me, expr, ctx);
        if (expr->tag == EXPR_OBJECT) {
            // (Nor is the contextual type its methods' `this` goes by: the check against the call's chosen candidate,
            // which a remembered type may answer without checking again, is what tsc's resolved signature says)
            u64 context_key[2] = { KEY_LITERAL_CONTEXT, (u64)(uintptr_t)expr };
            Ty const* recorded = intern_get(&my links, context_key, 2);
            Ty const* ret = compute(me, expr, ctx);
            if (recorded) *intern_slot(me, &my links, context_key, 2) = recorded;
            return ret;
        }
        GenericFunctionMode* mode = ctx.generic_mode;
        ctx.generic_mode = nullptr;
        if (!mode->sig && ty_calls_generic_function_returning_function(me, expr)) {
            // (Not a call resolved already, in an overload failure's inference: tsc's resolveCallExpression only skips
            // under SkipGenericFunctions when getResolvedSignature has nothing cached. HACK: only there, tttt resolves
            // nested calls earlier than tsc does, and doing it in every first pass costs effect 15 and react 2)
            Ty const* resolved = my overload_failure_inference ? ty_call_resolved_return(me, expr->call) : nullptr;
            if (resolved) return resolved;
            mode->skipped = true;
            return ty_intrinsic(me, TY_SILENT_NEVER);
        }
        // (Checked again from the top: with the contextual type before instantiation, which a call resolves with)
        Ty const* instantiated = ctx.contextual;
        ctx.contextual = ctx.raw_contextual;
        return ty_generic_function_in_context(me, check_with(me, expr, ctx), instantiated, mode);
    }
    LazyType* lazy = (LazyType*)&expr->lazy_type;
    if (lazy->tag != LAZY_TYPE_EXPR || !lazy->slot) return compute(me, expr, ctx); // implicit any and friends
    LazyMemo* memo = lazy_type_state_peek(lazy);
    if (memo && memo->done && !ctx.is_const && !ctx.template_context) {
        if (!ctx.contextual || memo->context == ctx.contextual || !(kind_uses_context(expr) || identifier_uses_context(me, expr))) {
            return memo->evaluated;
        }
    }
    if (!ctx.contextual && !ctx.sig_contextual && !ctx.is_const && !ctx.template_context) return ty_expr_type(me, expr); // through the LazyType, which detects cycles
    Ty const* ret = compute(me, expr, ctx);
    if (my flow_uncached) return ret;
    // (What such a reference is while type arguments are inferred isn't what it is once they are)
    if (my inferential_depth && identifier_uses_context(me, expr)) return ret;
    lazy_type_set(lazy, ret, ctx.contextual);
    return ret;
}

Ty const* ty_check_expr(Checker* me, Expr const* expr, Ty const* contextual) {
    return check_with(me, expr, (ExprCtx){ .contextual = contextual });
}

// `is_const`: in a const context whatever the contextual type (it was instantiated from a const type parameter)
Ty const* ty_check_expr_in_const(Checker* me, Expr const* expr, Ty const* contextual, bool is_const) {
    return check_with(me, expr, (ExprCtx){ .contextual = contextual, .is_const = is_const && is_const_context_kind(expr) });
}

Ty const* ty_check_literal_argument(
    Checker* me, Expr const* expr, Ty const* contextual, bool is_const, bool skip_sensitive, GenericFunctionMode* mode
) {
    return check_with(me, expr, (ExprCtx){
        .contextual = contextual, .is_const = is_const && is_const_context_kind(expr), .skip_sensitive = skip_sensitive,
        .generic_mode = expr->tag == EXPR_OBJECT || expr->tag == EXPR_ARRAY ? mode : nullptr,
    });
}

// Checked with the context-sensitive functions in it left out: see ExprCtx.skip_sensitive
Ty const* ty_check_expr_skipping_sensitive(Checker* me, Expr const* expr, Ty const* contextual, bool is_const) {
    return check_with(me, expr, (ExprCtx){
        .contextual = contextual, .is_const = is_const && is_const_context_kind(expr), .skip_sensitive = true,
    });
}

// Unions and widening

// tsc's isUnitType
static bool is_unit_for_reduction(Ty const* ty) {
    return ty->kind != TY_UNION && (ty->flags & (TYF_LITERAL | TYF_ENUM | TYF_UNIQUE_ES_SYMBOL | TYF_NULLABLE));
}

bool ty_is_unit(Ty const* ty) {
    return is_unit_for_reduction(ty);
}

// An instance type of a class (tsc's ObjectFlags.Class on getTargetType())
static bool is_class_instance(Ty const* ty) {
    if (ty->kind == TY_REFERENCE) ty = ty->reference.target;
    return ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_CLASS_INSTANCE;
}

Ty const* ty_union_subtype_reduced(Checker* me, Ty const* const* types, u32 len) {
    if (len < 2) return len ? types[0] : ty_intrinsic(me, TY_NEVER);
    // Flatten first, so members of member unions take part
    Ty const* flat = ty_union(me, types, len, nullptr, (TyList){0});
    if (flat->kind != TY_UNION || (flat->flags & TYF_BOOLEAN)) return flat;
    TyList members = flat->composite.types;
    // Like tsc's removeSubtypes, only structured and instantiable members are candidates for removal, unless there's
    // an empty object type to absorb primitives. Literals next to their primitive are gone already (ty_union()), of
    // tsc's removeRedundantLiteralTypes that leaves `undefined` next to `void`
    bool has_empty_object = false;
    bool has_void = false;
    for (u32 i = 0; i < members.len; i++) {
        Ty const* t = members.data[i];
        has_void |= (t->flags & TYF_VOID) != 0;
        if (has_empty_object || !(t->flags & TYF_OBJECT)) continue;
        Members const* m = ty_members(me, t);
        has_empty_object = !m->mapped && !m->prop_count && !m->call_count && !m->construct_count && !m->index_count;
    }
    u32 structured = TYF_OBJECT | TYF_UNION | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_TEMPLATE_LITERAL |
        TYF_STRING_MAPPING;
    // From the last member on, each is dropped if it's a strict subtype of one still there (so of mutual subtypes the
    // first stays). A member whose first unit-typed property differs from another's can't be its subtype. After 100000
    // checks, an estimate of more than a million is too complex (TS2590, and the error type)
    bool* dropped = calloc(members.len, sizeof(bool));
    panic_if(!dropped, "out of memory");
    u32 count = 0;
    Ty const* ret = nullptr;
    for (u32 i = members.len; i-- > 0 && !ret;) {
        Ty const* source = members.data[i];
        if (!has_empty_object && !(source->flags & structured)) {
            if (has_void && (source->flags & TYF_UNDEFINED)) dropped[i] = true;
            continue;
        }
        if (source->kind == TY_TYPE_PARAM) {
            // A type parameter constrained to a union is a subtype of the rest of the members as a whole, not of one of
            // them (`T extends A | B` next to A and B)
            Ty const* constraint = ty_base_constraint(me, source);
            if (constraint && constraint->kind == TY_UNION) {
                TyBuf rest = {0};
                for (u32 j = 0; j < members.len; j++) if (j != i && !dropped[j]) ty_buf_push(&rest, members.data[j]);
                Ty const* others = ty_union(me, rest.data, rest.len, nullptr, (TyList){0});
                ty_buf_drop(&rest);
                dropped[i] = ty_is_related(me, source, others, TY_RELATION_STRICT_SUBTYPE);
                continue;
            }
        }
        Prop const* key = nullptr;
        Ty const* key_type = nullptr;
        // (tsc: of objects, intersections and type variables, conditional types and substitutions, whose properties are
        // their apparent type's)
        Ty const* keyed = source->flags & (TYF_OBJECT | TYF_INTERSECTION) ? source :
            (source->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) && source->kind != TY_KEYOF ? ty_apparent_type(me, source) : nullptr;
        if (keyed && (keyed->flags & (TYF_OBJECT | TYF_INTERSECTION))) {
            Members const* m = ty_members(me, keyed);
            for (u32 k = 0; k < m->prop_count && !key; k++) {
                Ty const* t = prop_type(me, m->props[k]);
                if (is_unit_for_reduction(t)) {
                    key = m->props[k];
                    key_type = ty_regular(me, t);
                }
            }
        }
        for (u32 j = 0; j < members.len; j++) {
            if (j == i || dropped[j]) continue;
            Ty const* target = members.data[j];
            if (count == 100000) {
                u64 estimate = (u64)count / (members.len - i) * members.len;
                if (estimate > 1000000) {
                    ty_error_at_current_node_d(me, 2590, "Expression produces a union type that is too complex to represent.");
                    ret = ty_intrinsic(me, TY_ERROR);
                    break;
                }
            }
            count++;
            if (key && ((target->flags & (TYF_OBJECT | TYF_INTERSECTION)) ||
                ((target->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) && target->kind != TY_KEYOF))
            ) {
                Ty const* t = ty_prop_type_of(me, target, key->name);
                if (t && is_unit_for_reduction(t) && ty_regular(me, t) != key_type) continue;
            }
            // (One class's instances are a subtype of another's only when derived from it)
            if (ty_is_related(me, source, target, TY_RELATION_STRICT_SUBTYPE) &&
                (!is_class_instance(source) || !is_class_instance(target) || ty_is_type_derived_from(me, source, target))
            ) {
                dropped[i] = true;
                break;
            }
        }
    }
    if (!ret) {
        Ty const** kept = malloc(sizeof(Ty const*) * members.len);
        panic_if(!kept, "out of memory");
        u32 kept_len = 0;
        for (u32 i = 0; i < members.len; i++) if (!dropped[i]) kept[kept_len++] = members.data[i];
        if (kept_len == members.len) ret = flat;
        else if (!(ret = ty_named_union_of_reduced(me, types, len, kept, kept_len))) ret = ty_union(me, kept, kept_len, nullptr, (TyList){0});
        free(kept);
    }
    free(dropped);
    return ret;
}

static Ty const* union_of_two(Checker* me, Ty const* a, Ty const* b) {
    Ty const* parts[2] = { a, b };
    return ty_union_subtype_reduced(me, parts, 2);
}

// What an optional chain that may have ended evaluates to (tsc's getOptionalType: no subtype reduction, so a `void`
// result stays `void | undefined`)
static Ty const* optional_type(Checker* me, Ty const* ty) {
    Ty const* parts[2] = { ty, ty_intrinsic(me, TY_UNDEFINED) };
    return ty_union(me, parts, 2, nullptr, (TyList){0});
}

// What an access or call of an optional chain that may have ended evaluates to (tsc's propagateOptionalTypeMarker): at
// the chain's end the undefined of `optional_type()`, before it the marker the rest of the chain takes out again
static Ty const* propagate_optional(Checker* me, Ty const* ty, bool continues_chain, bool optional) {
    if (!optional) return ty;
    if (!continues_chain) return optional_type(me, ty);
    Ty const* parts[2] = { ty, ty_intrinsic(me, TY_OPTIONAL) };
    return ty_union(me, parts, 2, nullptr, (TyList){0});
}

// tsc's getWidenedLiteralLikeTypeForContextualType without the context: fresh literals and unique symbols widen, and what
// is left is regular (a mutable location holds regular literals)
static Ty const* widen_literals(Checker* me, Ty const* ty) {
    return ty_regular(me, ty_widened_unique_symbol(me, ty_widened_literal(me, ty)));
}

// Without strictNullChecks `null` and `undefined` widen to `any`. Unique symbols stay (tsc's getWidenedLiteralType): only
// one made for another declaration widens, and only at the top (binding.c's initialized_binding_type)
Ty const* ty_widened_for_declaration(Checker* me, Ty const* ty, bool literals) {
    // Not regularised: `const e = E.x` holds the fresh `E.x`, `let x = a` with `a: "a"` the regular "a"
    if (literals) ty = ty_widened_literal(me, ty);
    return ty_widened_nullable(me, ty_widened_objects(me, ty));
}

// Contextual types

static bool is_nullable_or_primitive(Ty const* ty) {
    return !(ty->flags & (TYF_OBJECT | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_UNION)) ||
        (ty->flags & TYF_BOOLEAN) != 0;
}

// Maps `get` over the members of a contextual union that have something to say
typedef Ty const* (*ContextGetter)(Checker* me, Ty const* object, void const* arg);

static Ty const* context_map(Checker* me, Ty const* contextual, ContextGetter get, void const* arg) {
    if (!contextual || (contextual->flags & TYF_ANY)) return nullptr;
    if (contextual->kind == TY_UNION && !(contextual->flags & TYF_BOOLEAN)) {
        TyList members = contextual->composite.types;
        Ty const** parts = malloc(sizeof(Ty const*) * members.len);
        u32 len = 0;
        bool unknown = false;
        for (u32 i = 0; i < members.len; i++) {
            Ty const* part = context_map(me, members.data[i], get, arg);
            // tsc maps contextual types without reductions (mapType's noReductions), so an `unknown` from one member
            // (an index signature) doesn't absorb what the others give. It contributes nothing itself: no members,
            // signatures or literals
            if (part && part->kind == TY_INTRINSIC && (part->flags & TYF_UNKNOWN)) unknown = true;
            else if (part) parts[len++] = part;
        }
        if (unknown && !len) parts[len++] = ty_intrinsic(me, TY_UNKNOWN_);
        // (Not literal-reduced either: `number | 1` has room for the literal 2)
        Ty const* ret = len ? ty_union_unreduced(me, parts, len) : nullptr;
        free(parts);
        return ret;
    }
    if (is_nullable_or_primitive(contextual)) return nullptr;
    return get(me, contextual, arg);
}

static bool is_generic_mapped_context(Checker* me, Ty const* ty) {
    return ty->kind == TY_ANONYMOUS && (ty->flags & TYF_OBJECT) && !ty->anonymous.function &&
        (ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) && ty_members(me, ty)->mapped && !ty_members(me, ty)->mapped->name_type;
}

// (tsc's appendContextualPropertyTypeConstituent: `any` says nothing next to the others)
static void push_prop_context_part(Checker* me, TyBuf* parts, Ty const* type) {
    if (type) ty_buf_push(parts, type->flags & TYF_ANY ? ty_intrinsic(me, TY_UNKNOWN_) : type);
}

// Of an intersection, the intersection of what its object members say: a generic mapped member what its template
// says for the key, the others their property, or if none has one, their index signatures (tsc's
// getTypeOfPropertyOfContextualType). Type variables in it say nothing
static bool is_contextual_type_variable(Ty const* ty) {
    return ty->kind == TY_TYPE_PARAM || ty->kind == TY_INDEXED || ty->kind == TY_CONDITIONAL || ty->kind == TY_SUBSTITUTION;
}

static Ty const* get_prop_context(Checker* me, Ty const* object, void const* arg);

// What get_prop_context() looks for: a property by name, its literal type made only when needed (tsc's
// getTypeOfPropertyOfContextualType: getStringLiteralType(name) only for index signatures and generic mapped types)
typedef struct PropContextKey {
    Atom const* name; // Null if the key is a type that names no property
    Ty const* key; // The key type, if known
} PropContextKey;

static Ty const* prop_context_key(Checker* me, PropContextKey const* k) {
    return k->key ? k->key : ty_prop_name_literal(me, k->name);
}

static Ty const* intersection_prop_context(Checker* me, Ty const* object, Ty const* key) {
    TyBuf parts = {0};
    TyBuf index_candidates = {0};
    bool ignore_index = false;
    Atom const* name = ty_literal_name(me, key);
    TyList members = object->composite.types;
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = members.data[i];
        // A type variable member as its constraint (tsc's getApparentTypeOfIntersectionType: getTypeWithThisArgument
        // with needApparentType). One that is a union or intersection: what that says (tsc distributes the
        // intersection over it instead)
        if (is_contextual_type_variable(member)) {
            member = ty_apparent_type(me, member);
            if (member->kind == TY_UNION || member->kind == TY_INTERSECTION) {
                PropContextKey member_key = { name, key };
                push_prop_context_part(me, &parts, context_map(me, member, get_prop_context, &member_key));
                continue;
            }
        }
        if (!(member->flags & TYF_OBJECT)) continue;
        if (is_generic_mapped_context(me, member)) {
            push_prop_context_part(me, &parts, ty_generic_mapped_prop_context(me, member, key));
            continue;
        }
        FoundProp prop = name ? ty_find_prop(me, member, name) : (FoundProp){0};
        if (!prop.found) {
            if (!ignore_index) ty_buf_push(&index_candidates, member);
            continue;
        }
        ignore_index = true;
        index_candidates.len = 0;
        // (Its members see the whole intersection as their `this`, tsc's getApparentTypeOfIntersectionType)
        push_prop_context_part(me, &parts, ty_access(me, ty_with_this_arg(me, member, object), key));
    }
    for (u32 i = 0; i < index_candidates.len; i++) {
        IndexInfo const* info = ty_applicable_index_info(me, index_candidates.data[i], key);
        push_prop_context_part(me, &parts, info ? info->value : nullptr);
    }
    Ty const* ret = !parts.len ? nullptr : parts.len == 1 ? parts.data[0] :
        ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    ty_buf_drop(&index_candidates);
    return ret;
}

static Ty const* get_prop_context_with_missing(Checker* me, Ty const* object, PropContextKey const* k);

// (An optional property says its declared type: under exactOptionalPropertyTypes without the missing type, tsc's
// getTypeOfConcretePropertyOfContextualType)
static Ty const* get_prop_context(Checker* me, Ty const* object, void const* arg) {
    Ty const* type = get_prop_context_with_missing(me, object, arg);
    return type ? ty_remove_missing(me, type, true) : nullptr;
}

static Ty const* get_prop_context_with_missing(Checker* me, Ty const* object, PropContextKey const* k) {
    // A type variable says what its constraint says, a generic mapped one included (tsc's
    // getApparentTypeOfContextualType)
    if (is_contextual_type_variable(object)) {
        object = ty_apparent_type(me, object);
        if (is_nullable_or_primitive(object) || object->kind == TY_TYPE_PARAM) return nullptr;
    }
    if (object->kind == TY_INTERSECTION && (object->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        return intersection_prop_context(me, object, prop_context_key(me, k));
    }
    // A property the object has: no literal type for its name (not generic: a generic mapped type wants the key)
    if (!k->key && !(object->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        Ty const* own = ty_access_own_named_prop(me, object, k->name);
        if (own) return own;
    }
    Ty const* key = prop_context_key(me, k);
    Ty const* mapped = ty_generic_mapped_prop_context(me, object, key);
    return mapped ? mapped : ty_access(me, object, key);
}

static Ty const* contextual_prop_type(Checker* me, Ty const* contextual, Ty const* key) {
    PropContextKey k = { nullptr, key };
    return context_map(me, contextual, get_prop_context, &k);
}

// contextual_prop_type() of a property name
static Ty const* contextual_named_prop_type(Checker* me, Ty const* contextual, Atom const* name) {
    PropContextKey k = { name, nullptr };
    return context_map(me, contextual, get_prop_context, &k);
}

typedef struct ElementContextArg {
    u32 index;
    u32 length; // Of the array literal, UINT32_MAX if unknown
    u32 first_spread, last_spread; // UINT32_MAX if none
} ElementContextArg;

// tsc's getContextualTypeForElementExpression for a tuple: the element at the index while the elements before it are
// the tuple's leading fixed ones, the element from the end when the elements after it are its trailing fixed ones,
// otherwise any of the elements in between
static Ty const* tuple_element_context(Checker* me, Ty const* tuple, ElementContextArg const* a) {
    TupleShape const* shape = tuple->reference.target->declared.tuple;
    u32 fixed = ty_tuple_fixed_length(shape);
    if ((a->first_spread == UINT32_MAX || a->index < a->first_spread) && a->index < fixed) {
        // (tsc's optional elements hold undefined, tttt's don't)
        Ty const* element = ty_reference_args(me, tuple).data[a->index];
        return shape->flags[a->index] & TUPLE_ELEMENT_OPTIONAL ? ty_optional_param_type(me, element) : element;
    }
    u32 offset = a->length != UINT32_MAX && (a->last_spread == UINT32_MAX || a->index > a->last_spread) ?
        a->length - a->index : 0;
    u32 fixed_end = 0;
    if (offset > 0 && fixed < shape->arity) {
        while (fixed_end < shape->arity && !(shape->flags[shape->arity - 1 - fixed_end] & TUPLE_ELEMENT_REST)) fixed_end++;
    }
    if (offset > 0 && offset <= fixed_end) return ty_reference_args(me, tuple).data[shape->arity - offset];
    u32 from = a->first_spread < fixed ? a->first_spread : fixed;
    u32 end_skip = fixed_end;
    if (a->length != UINT32_MAX && a->last_spread != UINT32_MAX && a->length - a->last_spread < end_skip) {
        end_skip = a->length - a->last_spread;
    }
    return ty_tuple_slice_element_type(me, tuple, from, end_skip, true);
}

// The element of an array literal whose contextual type is any (tsc's getIteratedTypeOrElementType for
// IterationUse.Element: any is iterable, but in ES5 it lacks the numeric index signature it wants)
static Ty const* any_element_context(Checker* me) {
    return compiler_options()->target >= SCRIPT_TARGET_ES2015 || compiler_options()->downlevel_iteration ?
        ty_intrinsic(me, TY_ANY) : nullptr;
}

static Ty const* get_element_context(Checker* me, Ty const* object, void const* arg) {
    ElementContextArg const* a = arg;
    u32 index = a->index;
    // any has no properties: only iterating it (ES2015 or downlevelIteration) gives an element, any (tsc's
    // getIteratedTypeOrElementType, which in ES5 wants a numeric index signature)
    if ((object->flags & TYF_ANY) ||
        ((object->kind == TY_TYPE_PARAM || (object->flags & TYF_DEFERRED)) && (ty_apparent_type(me, object)->flags & TYF_ANY))
    ) {
        return any_element_context(me);
    }
    // A type parameter as its constraint (tsc's getApparentTypeOfContextualType): the elements of a tuple constraint
    if (object->kind == TY_TYPE_PARAM) {
        Ty const* apparent = ty_apparent_type(me, object);
        if (ty_is_tuple(apparent) && apparent->kind == TY_REFERENCE) return tuple_element_context(me, apparent, a);
    }
    // A conditional type as its constraint, each member of a union one (`string | [unknown, ...unknown[]] | { [k in
    // keyof D]: N<D[k]> }` gives N<D["0"]> next to unknown)
    if (object->kind == TY_CONDITIONAL) {
        Ty const* apparent = ty_apparent_type(me, object);
        if (apparent != object && apparent->kind != TY_CONDITIONAL) return context_map(me, apparent, get_element_context, arg);
    }
    if (ty_is_tuple(object) && object->kind == TY_REFERENCE) return tuple_element_context(me, object, a);
    if (object->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
        cstr name = cstr_d(index);
        Ty const* mapped = ty_generic_mapped_prop_context(me, object, ty_string_literal(me, cstr_sv(&name)));
        cstr_drop(&name);
        if (mapped) return mapped;
    }
    Value value = value_number(index);
    Ty const* ret = ty_access(me, object, ty_literal(me, &value));
    if (!ret) ret = ty_access(me, object, ty_intrinsic(me, TY_NUMBER));
    if (!ret && (compiler_options()->target >= SCRIPT_TARGET_ES2015 || compiler_options()->downlevel_iteration)) {
        // Anything iterable: what it yields (tsc's getIteratedTypeOrElementType for IterationUse.Element)
        TyIterationTypes const* types = ty_iteration_types_of_iterable(me, object, ITERATION_ALLOWS_SYNC);
        if (types) ret = types->yield_type;
    }
    return ret;
}

static Ty const* contextual_element_type(Checker* me, Ty const* contextual, u32 index) {
    ElementContextArg arg = { index, UINT32_MAX, UINT32_MAX, UINT32_MAX };
    return context_map(me, contextual, get_element_context, &arg);
}

// The contextual type of the element at `index` of an array literal
static Ty const* contextual_array_element_type(Checker* me, Ty const* contextual, Expr const* array, u32 index) {
    if (!contextual) return nullptr;
    if (contextual->flags & TYF_ANY) return any_element_context(me);
    ElementContextArg arg = { index, (u32)vec_expr_size(&array->array), UINT32_MAX, UINT32_MAX };
    for_each (i, vec_expr, &array->array) {
        if (i->tag != EXPR_SPREAD) continue;
        if (arg.first_spread == UINT32_MAX) arg.first_spread = (u32)i_index;
        arg.last_spread = (u32)i_index;
    }
    return context_map(me, contextual, get_element_context, &arg);
}

Ty const* ty_contextual_prop_type(Checker* me, Ty const* contextual, Ty const* key) {
    return contextual_prop_type(me, contextual, key);
}

Ty const* ty_contextual_element_type(Checker* me, Ty const* contextual, u32 index) {
    return contextual_element_type(me, contextual, index);
}

Ty const* ty_contextual_element_type_of(Checker* me, Ty const* contextual, u32 index, u32 length) {
    ElementContextArg arg = { index, length, UINT32_MAX, UINT32_MAX };
    return context_map(me, contextual, get_element_context, &arg);
}

// tsc's isTupleLikeType past its "0" check: an array-like type (assignable to `readonly any[]`) whose length is number
// literals
static bool array_like_with_literal_length(Checker* me, Ty const* ty) {
    Ty const* length = ty_prop_type_of(me, ty, checker_atom(me, c_sv("length")));
    if (!length) return false;
    TyList parts = length->kind == TY_UNION ? length->composite.types : (TyList){ .data = &length, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        if (!(parts.data[i]->flags & TYF_NUMBER_LITERAL)) return false;
    }
    return parts.len && ty_is_related(me, ty, ty_array(me, ty_intrinsic(me, TY_ANY), true), TY_RELATION_ASSIGNABLE);
}

// A spread of a type variable that is array-like stays a variadic element of the tuple it's spread into: `[...self,
// that]` with `self: A extends readonly unknown[]` is `[...A, B]` (tsc's checkArrayLiteral, isArrayLikeType, and
// createNormalizedTupleType keeping InstantiableNonPrimitive variadic elements)
static bool is_generic_array_like(Checker* me, Ty const* ty) {
    return (ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) && ty_extends_array(me, ty);
}

static bool context_is_tuple_like(Checker* me, Ty const* contextual, u32 depth) {
    if (!contextual || depth > 8) return false;
    if (ty_is_tuple(contextual)) return true;
    if (contextual->kind == TY_DECLARED && contextual->declared.kind == TY_DECLARED_TUPLE_TARGET) return true;
    if (contextual->kind == TY_UNION) {
        for (u32 i = 0; i < contextual->composite.types.len; i++) {
            if (context_is_tuple_like(me, contextual->composite.types.data[i], depth + 1)) return true;
        }
        return false;
    }
    if (contextual->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < contextual->composite.types.len; i++) {
            if (context_is_tuple_like(me, contextual->composite.types.data[i], depth + 1)) return true;
        }
    }
    // Anything with a "0" property (tsc's isTupleLikeType: `Pick<[T, ...T[]], ...> & { length: 3 }`), or array-like
    // with a literal length (`number[] & { length: 2 }`, an interface extending `[]`)
    if ((contextual->kind == TY_INTERSECTION || contextual->kind == TY_DECLARED || contextual->kind == TY_REFERENCE) &&
        array_like_with_literal_length(me, contextual)
    ) {
        return true;
    }
    if ((contextual->flags & (TYF_OBJECT | TYF_INTERSECTION)) && ty_get_prop(me, contextual, checker_atom(me, c_sv("0")))) return true;
    if (contextual->kind == TY_TYPE_PARAM) {
        Ty const* constraint = ty_param_constraint(me, contextual);
        return constraint && constraint != contextual && context_is_tuple_like(me, constraint, depth + 1);
    }
    // An indexed access or conditional type: what its constraint is (tsc's getApparentTypeOfContextualType, the base
    // constraint: of a conditional both branches, `C extends true ? Required<[a?: A]> : [a?: A]`)
    if (contextual->flags & TYF_DEFERRED) {
        Ty const* constraint = ty_base_constraint(me, contextual);
        return constraint && constraint != contextual && context_is_tuple_like(me, constraint, depth + 1);
    }
    // A generic homomorphic mapped type (tsc's checkArrayLiteral: `{ [K in keyof T]: X }` could map a tuple). Whether
    // it's homomorphic goes by the declaration (tsc's getHomomorphicTypeVariable of the target), whose T must be a type
    // parameter: `{ [K in keyof T["a"]]: X }` isn't
    if (contextual->kind == TY_ANONYMOUS && (contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) &&
        ty_members(me, contextual)->mapped
    ) {
        MappedInfo const* mapped = ty_members(me, contextual)->mapped;
        Ty const* root = contextual->anonymous.target ? contextual->anonymous.target : contextual;
        MappedInfo const* declared = root != contextual && (root->flags & TYF_OBJECT) ? ty_members(me, root)->mapped : mapped;
        if (!declared) declared = mapped;
        Ty const* variable = declared->modifiers_type && declared->keyof_constraint ?
            ty_actual_type_variable(me, declared->modifiers_type) : nullptr;
        if (!mapped->name_type && mapped->modifiers_type && variable && variable->kind == TY_TYPE_PARAM &&
            (mapped->modifiers_type->kind == TY_TYPE_PARAM || (mapped->modifiers_type->flags & TYF_DEFERRED))
        ) {
            return true;
        }
    }
    if (contextual->kind == TY_ANONYMOUS) return ty_get_prop(me, contextual, checker_atom(me, c_sv("0"))) != nullptr;
    return false;
}

// The flags of a type and, through unions and intersections, of its parts
static u32 ty_flags_in_parts(Ty const* ty) {
    if (ty->kind != TY_UNION && ty->kind != TY_INTERSECTION) return ty->flags;
    u32 ret = ty->flags;
    for (u32 i = 0; i < ty->composite.types.len; i++) ret |= ty_flags_in_parts(ty->composite.types.data[i]);
    return ret;
}

// tsc's isLiteralOfContextualType: a literal type stays as it is where the contextual type has room for literals of
// its kind
static bool is_literal_of_contextual_type(Checker* me, Ty const* literal, Ty const* contextual, u32 depth) {
    if (!contextual || depth > 8) return false;
    if (contextual->kind == TY_UNION || contextual->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < contextual->composite.types.len; i++) {
            if (is_literal_of_contextual_type(me, literal, contextual->composite.types.data[i], depth + 1)) return true;
        }
        return false;
    }
    // (Generic or not: they're primitives to tsc)
    if (contextual->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) return (literal->flags & TYF_STRING_LITERAL) != 0;
    // `keyof T` has room for string literals, whatever T is (tsc: TypeFlags.Index is no InstantiableNonPrimitive)
    if (contextual->kind == TY_KEYOF) return (literal->flags & TYF_STRING_LITERAL) != 0;
    if (contextual->kind == TY_TYPE_PARAM || (contextual->flags & TYF_DEFERRED)) {
        // A type parameter or deferred type: what its constraint has room for. `T extends string` keeps "a"
        Ty const* constraint = ty_base_constraint(me, contextual);
        if (!constraint || constraint == contextual) return false;
        // (tsc's maybeTypeOfKind: `string` anywhere in a union or intersection counts)
        u32 kinds = ty_flags_in_parts(constraint);
        u32 f = literal->flags;
        if (((kinds & TYF_ES_SYMBOL) && (f & TYF_UNIQUE_ES_SYMBOL)) ||
            ((kinds & TYF_STRING) && (f & TYF_STRING_LITERAL)) ||
            ((kinds & TYF_NUMBER) && (f & TYF_NUMBER_LITERAL)) ||
            ((kinds & TYF_BIGINT) && (f & TYF_BIGINT_LITERAL)) ||
            ((kinds & TYF_BOOLEAN) && (f & TYF_BOOLEAN_LITERAL))
        ) {
            return true;
        }
        return is_literal_of_contextual_type(me, literal, constraint, depth + 1);
    }
    if ((contextual->flags & TYF_ES_SYMBOL_LIKE) && (literal->flags & TYF_UNIQUE_ES_SYMBOL)) return true;
    u32 kinds = TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_BIGINT_LITERAL | TYF_BOOLEAN_LITERAL;
    return contextual->kind == TY_LITERAL && (contextual->flags & literal->flags & kinds) != 0;
}

static bool any_literal_of_contextual_type(Checker* me, Ty const* ty, Ty const* contextual) {
    if (ty->kind == TY_LITERAL) return is_literal_of_contextual_type(me, ty, contextual, 0);
    if (ty->kind == TY_UNION && !(ty->flags & TYF_BOOLEAN)) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            Ty const* part = ty->composite.types.data[i];
            if (part->kind == TY_LITERAL && is_literal_of_contextual_type(me, part, contextual, 0)) return true;
        }
    }
    return (ty->flags & TYF_BOOLEAN) && contextual && is_literal_of_contextual_type(me, ty_intrinsic(me, TY_TRUE), contextual, 0);
}

bool ty_is_literal_of_contextual_type(Checker* me, Ty const* ty, Ty const* contextual) {
    return any_literal_of_contextual_type(me, ty, contextual);
}

// tsc's isTypeAssertion: `x as T` or `<T>x` (parenthesized or not; `satisfies` isn't one)
static bool is_type_assertion(Expr const* expr) {
    return expr->tag == EXPR_AS_TYPE && !expr->as_type->is_satisfies;
}

// tsc's checkExpressionForMutableLocation: the type a property or element gets from its initializer. What a type
// assertion says stays as it is (`{ a: S as typeof S }` keeps the unique symbol)
static Ty const* check_for_mutable_location(Checker* me, Expr const* expr, ExprCtx ctx) {
    Ty const* type = check_with(me, expr, ctx);
    if (in_const_context(me, expr, ctx)) return ty_regular(me, type);
    if (is_type_assertion(expr)) return type;
    if (any_literal_of_contextual_type(me, type, ty_instantiate_contextual_type(me, ctx.contextual))) return ty_regular(me, type);
    return widen_literals(me, type);
}

Ty const* ty_widened_return_type(Checker* me, Ty const* ty, Ty const* contextual) {
    // Only a unit type widens: a function that returns 1 or "a" says so
    bool unit = ty->kind == TY_LITERAL;
    if (unit) ty = contextual && any_literal_of_contextual_type(me, ty, contextual) ? ty_regular(me, ty) : widen_literals(me, ty);
    return ty_widened_nullable(me, ty_widened_objects(me, ty));
}

// The signature a function expression is contextually typed by, null if there's none (or no single one)
// tsc's isAritySmaller: a signature that takes fewer parameters than the function expression requires isn't its context
static bool is_arity_smaller(Checker* me, Signature const* sig, FunctionImpl const* function) {
    signature_resolve(me, sig);
    u32 required = 0;
    u32 index = 0;
    for_each (i, vec_function_arg, &function->args) {
        bool is_this = i->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(i->initializer.binding.content_span, "this");
        if (is_this) continue;
        index++;
        if (i->rest || i->explicit_optional || initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY) break;
        required = index;
    }
    for (u32 i = 0; i < sig->param_count; i++) {
        if (sig->params[i].rest) return false;
    }
    return sig->param_count < required;
}

// tsc's getContextualCallSignature: the call signatures that don't take fewer parameters than the function has
// required ones. Several are combined into one (getIntersectedSignatures), under noImplicitAny only
static Signature const* contextual_call_signature(Checker* me, Ty const* type, FunctionImpl const* function) {
    Signature const* stack_buf[16];
    Signature const** applicable = stack_buf;
    u32 cap = c_arraylen(stack_buf);
    u32 count = 0;
    if (type->flags & (TYF_OBJECT | TYF_INTERSECTION)) {
        Members const* members = ty_members(me, type);
        for (u32 j = 0; j < members->call_count; j++) {
            if (is_arity_smaller(me, members->call[j], function)) continue;
            if (count == cap) {
                cap *= 2;
                Signature const** grown = malloc(sizeof(Signature const*) * cap);
                panic_if(!grown, "out of memory");
                memcpy(grown, applicable, sizeof(Signature const*) * count);
                if (applicable != stack_buf) free(applicable);
                applicable = grown;
            }
            applicable[count++] = members->call[j];
        }
    }
    Signature const* ret = count == 1 ? applicable[0] : nullptr;
    if (count > 1 && compiler_options()->no_implicit_any) {
        ret = applicable[0];
        signature_resolve(me, ret);
        for (u32 i = 1; i < count && ret; i++) {
            Signature const* right = applicable[i];
            signature_resolve(me, right);
            if (right == ret) continue;
            ret = ty_type_params_identical(me, ret->type_params, right->type_params) ?
                ty_combine_intersection_signatures(me, ret, right) : nullptr;
        }
    }
    if (applicable != stack_buf) free(applicable);
    if (ret) signature_resolve(me, ret);
    return ret;
}

static Ty const* apparent_contextual_type(Checker* me, Ty const* ty);

// tsc's getContextualSignature: of a union, the signature its members have in common (up to return types)
static Signature const* contextual_signature(Checker* me, Ty const* contextual, FunctionImpl const* function, u32 depth) {
    if (!contextual || depth > 8 || (contextual->flags & TYF_ANY)) return nullptr;
    if (contextual->kind == TY_UNION) {
        Signature* stack_buf[16];
        Signature** found = stack_buf;
        u32 count = 0;
        u32 len = contextual->composite.types.len;
        if (len > c_arraylen(stack_buf)) {
            found = malloc(sizeof(Signature*) * len);
            panic_if(!found, "out of memory");
        }
        Signature const* ret = nullptr;
        bool compatible = true;
        for (u32 i = 0; i < len && compatible; i++) {
            Signature const* sig = contextual_signature(me, contextual->composite.types.data[i], function, depth + 1);
            if (!sig) continue;
            if (count && !ty_signatures_identical(me, found[0], sig, false, true)) compatible = false;
            else found[count++] = (Signature*)sig;
        }
        if (compatible && count) ret = count == 1 ? found[0] : ty_union_signature(me, found[0], found, count);
        if (found != stack_buf) free(found);
        return ret;
    }
    if (contextual->kind == TY_TYPE_PARAM) {
        Ty const* constraint = ty_param_constraint(me, contextual);
        return constraint && constraint != contextual ? contextual_signature(me, constraint, function, depth + 1) : nullptr;
    }
    // An indexed access or conditional type: its constraint's (tsc's getApparentTypeOfContextualType)
    if (contextual->kind == TY_INDEXED || contextual->kind == TY_CONDITIONAL || contextual->kind == TY_SUBSTITUTION) {
        Ty const* apparent = ty_apparent_type(me, contextual);
        return apparent != contextual ? contextual_signature(me, apparent, function, depth + 1) : nullptr;
    }
    if (!(contextual->flags & (TYF_OBJECT | TYF_INTERSECTION))) return nullptr;
    // An intersection's signatures are its members', a type variable's those of its constraint (tsc's
    // getSignaturesOfType of each member through getReducedApparentType)
    if (contextual->kind == TY_INTERSECTION && (contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        contextual = apparent_contextual_type(me, contextual);
        if (!(contextual->flags & (TYF_OBJECT | TYF_INTERSECTION))) return nullptr;
    }
    return contextual_call_signature(me, contextual, function);
}

Signature const* ty_contextual_signature(Checker* me, Ty const* contextual, FunctionImpl const* function) {
    return contextual_signature(me, contextual, function, 0);
}

// The elements of a tuple from `from` on (tsc's getRestTypeAtPosition of a rest tuple parameter): its rest element's
// array when that's all that's left
static Ty const* tuple_slice(Checker* me, Ty const* tuple, u32 from) {
    TupleShape const* shape = ty_tuple_shape(tuple);
    TyList args = ty_reference_args(me, tuple);
    u32 arity = shape->arity;
    if (from >= arity) {
        bool rest_last = arity && (shape->flags[arity - 1] & TUPLE_ELEMENT_REST);
        if (rest_last) return args.data[arity - 1];
        from = arity;
    }
    if (from == arity - 1 && (shape->flags[from] & TUPLE_ELEMENT_REST)) return args.data[from];
    u32 len = arity - from;
    TupleShape sliced = {
        .arity = len, .readonly = false, .flags = shape->flags + from, .names = shape->names ? shape->names + from : nullptr,
    };
    Ty const* target = ty_tuple_target(me, &sliced);
    return len ? ty_reference(me, target, (TyList){ .data = args.data + from, .len = len }) : target;
}

static Ty const* signature_type_at(Checker* me, Signature const* sig, u32 index, bool as_rest) {
    for (u32 i = 0; i < sig->param_count; i++) {
        SigParam const* param = &sig->params[i];
        if (!param->rest) {
            if (i == index && !as_rest) return param->optional ? ty_optional_param_type(me, param->type) : param->type;
            continue;
        }
        if (index < i) return as_rest ? ty_signature_params_tuple(me, sig, index, nullptr) : nullptr;
        // The rest parameter at the same position: its type as it is, `never` too (tsc's getRestTypeAtPosition with
        // getEffectiveRestType: any is any[]). Past it: an array of the element type
        if (as_rest && i == index) return (param->type->flags & TYF_ANY) ? ty_array(me, param->type, false) : param->type;
        if (as_rest && ty_is_tuple(param->type) && !ty_is_generic_tuple(me, param->type)) {
            return tuple_slice(me, param->type, index - i);
        }
        // A type variable: `T[0]` where it's by position, `T[number][]` for a rest past it (tsc's tryGetTypeAtPosition /
        // getRestTypeAtPosition)
        if (param->type->kind == TY_TYPE_PARAM) {
            if (as_rest) return ty_array(me, ty_indexed(me, param->type, ty_intrinsic(me, TY_NUMBER)), false);
            Value value = value_number(index - i);
            return ty_indexed(me, param->type, ty_literal(me, &value));
        }
        TupleShape const* shape = ty_tuple_shape(param->type);
        if (shape) {
            // Past the end of a tuple without rest elements: nothing (tsc's tryGetTypeAtPosition)
            bool variable = false;
            for (u32 k = 0; k < shape->arity; k++) variable |= (shape->flags[k] & TUPLE_ELEMENT_REST) != 0;
            if (!variable && index - i >= shape->arity) return nullptr;
        }
        Ty const* element;
        if (ty_is_tuple(param->type)) {
            Value value = value_number(index - i);
            element = ty_access(me, param->type, ty_literal(me, &value));
        } else if (ty_is_array(me, param->type) || (param->type->flags & TYF_ANY)) {
            element = ty_rest_element_type(me, param->type);
        } else {
            // By position (tsc's tryGetTypeAtPosition): a union of tuples gives each one's element there
            Value value = value_number(index - i);
            Ty const* at = ty_access(me, param->type, ty_literal(me, &value));
            element = at ? at : ty_access(me, param->type, ty_intrinsic(me, TY_NUMBER));
        }
        return as_rest && element ? ty_array(me, element, false) : element;
    }
    // A rest parameter where the signature has plain ones: the tuple of those left
    return as_rest ? ty_signature_params_tuple(me, sig, index, nullptr) : nullptr;
}

Ty const* ty_contextual_signature_type_at(Checker* me, Signature const* sig, u32 index, bool as_rest) {
    return signature_type_at(me, sig, index, as_rest);
}

// Whether the parameter whose pattern this is took its contextual type from the pattern, inference having given
// `unknown`: to tsc's getSymbolAtLocation, which asks the contextual signature again, its properties are unknown's
bool ty_contextual_parameter_is_pattern(Checker* me, Binding const* pattern) {
    while (pattern->parent) pattern = pattern->parent;
    u64 key[2] = { KEY_CONTEXT_PARAM_PATTERN, (u64)(uintptr_t)pattern };
    return intern_get(&my links, key, 2) != nullptr;
}

Ty const* ty_contextual_parameter_type(Checker* me, InitializedBinding const* parameter) {
    u64 key[2] = { KEY_CONTEXT_PARAM, (u64)(uintptr_t)parameter };
    Ty const* ret = intern_get(&my links, key, 2);
    if (!ret && initialized_binding_rare(parameter)->this_of) ret = ty_contextual_this_type(me, initialized_binding_rare(parameter)->this_of);
    return ret;
}

// tsc's ThisType<T> marker in a contextual type (getThisTypeFromContextualType): T, null if there's none
static Ty const* this_type_marker(Checker* me, Ty const* contextual) {
    if (contextual->kind == TY_UNION || contextual->kind == TY_INTERSECTION) {
        TyBuf found = {0};
        for (u32 i = 0; i < contextual->composite.types.len; i++) {
            Ty const* part = this_type_marker(me, contextual->composite.types.data[i]);
            if (part) {
                ty_buf_push(&found, part);
                if (contextual->kind == TY_INTERSECTION) break;
            }
        }
        Ty const* ret = found.len ? ty_union(me, found.data, found.len, nullptr, (TyList){0}) : nullptr;
        ty_buf_drop(&found);
        return ret;
    }
    if (contextual->kind != TY_REFERENCE) return nullptr;
    Symbol const* symbol = contextual->reference.target->declared.symbol;
    if (!symbol || symbol->parent || !csview_equals(symbol->name, "ThisType")) return nullptr;
    TyList args = ty_reference_args(me, contextual);
    return args.len == 1 ? args.data[0] : nullptr;
}

// The class a constructor function's `this` is the instance of: not that of an object literal's property, which is no
// constructor to tsc's isJSConstructor unless tagged `@class` (its symbol's members still make its type a class)
static Symbol const* js_constructor_for_this(FunctionImpl const* function) {
    if (function_impl_rare(function)->own_class_symbol && function_impl_rare(function)->this_literal && !function->jsdoc_class) return nullptr;
    return ty_js_constructor_class(function);
}

// tsc's getContextualThisParameterType: what `this` is in a function expression or object literal method that doesn't
// declare it. Null: nothing says (it's any)
Symbol const* ty_js_this_class(FunctionImpl const* function) {
    // (A literal's function-valued property that is a class of its own still sees the literal)
    Symbol const* js_class = js_constructor_for_this(function);
    if (js_class) return js_class;
    // A method of a constructor function's prototype (tsc's getClassNameFromPrototypeMethod: not an accessor of
    // `F.prototype = { get x() {} }`, whose `this` is the literal's)
    Expr const* constructor = function->accessor ? nullptr : function_impl_rare(function)->prototype_of;
    Symbol const* symbol = constructor ? expr_entity_name_symbol(constructor) : nullptr;
    SourceFile const* file = function->full_span.source;
    if (!symbol || !file || !file->is_js || !(symbol->flags & SYMBOL_CLASS) || !symbol_rare(symbol)->members) return nullptr;
    return symbol;
}

Ty const* ty_js_this_type(Checker* me, FunctionImpl const* function) {
    Symbol const* js_class = ty_js_this_class(function);
    if (!js_class) return nullptr;
    Ty const* declared = ty_declared_type_of(me, js_class);
    return declared->kind == TY_DECLARED && declared->declared.this_param ? declared->declared.this_param : declared;
}

// A type variable among a contextual type (or its union members) as its apparent type (tsc's
// getApparentTypeOfContextualType, which leaves mapped types alone). The apparent type of an intersection is the
// intersection of its members' (getApparentTypeOfIntersectionType)
static Ty const* apparent_contextual_type(Checker* me, Ty const* ty) {
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        TyList types = ty->composite.types;
        Ty const** mapped = malloc(sizeof(Ty const*) * types.len);
        panic_if(!mapped, "out of memory");
        bool changed = false;
        for (u32 i = 0; i < types.len; i++) {
            mapped[i] = apparent_contextual_type(me, types.data[i]);
            changed |= mapped[i] != types.data[i];
        }
        Ty const* ret = !changed ? ty : ty->kind == TY_UNION ? ty_union(me, mapped, types.len, nullptr, (TyList){0}) :
            ty_intersection(me, mapped, types.len, nullptr, (TyList){0});
        free(mapped);
        return ret;
    }
    if (ty->kind == TY_TYPE_PARAM || (ty->flags & TYF_DEFERRED)) return ty_apparent_type(me, ty);
    return ty;
}

Ty const* ty_contextual_this_type(Checker* me, FunctionImpl const* function) {
    // A JavaScript `@type {F}` gives its signature's `this` (tsc's getThisTypeOfDeclaration of a type tag's signature)
    if (function_impl_rare(function)->jsdoc_signature_type) ty_function_apply_jsdoc_type(me, function, nullptr);
    u64 this_key[2] = { KEY_CONTEXT_THIS, (u64)(uintptr_t)function };
    Ty const* from_signature = intern_get(&my links, this_key, 2);
    if (from_signature) return from_signature;
    // (tsc's getContextualThisParameterType: the containing literal or assigned object with noImplicitThis, or in JS)
    bool in_js = function->full_span.source && function->full_span.source->is_js;
    if (!compiler_options()->no_implicit_this && !in_js) return nullptr;
    if (function_impl_rare(function)->this_literal && !function_impl_rare(function)->this_literal_parenthesized) {
        Expr const* literal = function_impl_rare(function)->this_literal;
        u64 context_key[2] = { KEY_LITERAL_CONTEXT, (u64)(uintptr_t)literal };
        Ty const* contextual = intern_get(&my links, context_key, 2);
        // A ThisType<T> marker in the contextual type of the literal or of the literals it is a property value of
        // (tsc's getThisTypeOfObjectLiteral)
        Expr const* outer = literal;
        for (u32 depth = 0; outer && depth < 32; depth++) {
            u64 key[2] = { KEY_LITERAL_CONTEXT, (u64)(uintptr_t)outer };
            Ty const* outer_context = intern_get(&my links, key, 2);
            if (!outer_context) break;
            Ty const* marker = this_type_marker(me, ty_apparent_type(me, outer_context));
            // (A ThisType<T> marker, and only that, fixes what it mentions: tsc's getContextualThisParameterType)
            if (marker) return ty_instantiate_fixing(me, marker);
            u64 parent_key[2] = { KEY_LITERAL_PARENT, (u64)(uintptr_t)outer };
            outer = intern_get(&my links, parent_key, 2);
        }
        if (contextual) {
            // The apparent contextual type (tsc's getApparentTypeOfContextualType): a type variable is what it is at
            // least, `this` of a literal returned as a generic conditional type is the conditional's constraint
            contextual = apparent_contextual_type(me, contextual);
            return ty_widened_for_declaration(me, ty_non_nullable(me, contextual), false);
        }
        return ty_widened_for_declaration(me, ty_expr_type(me, literal), true);
    }
    if (function_impl_rare(function)->this_assigned_object) {
        return ty_widened_for_declaration(me, ty_expr_type(me, function_impl_rare(function)->this_assigned_object), true);
    }
    return nullptr;
}

Ty const* ty_contextual_return_type(Checker* me, FunctionImpl const* function) {
    u64 key[2] = { KEY_CONTEXT_RETURN, (u64)(uintptr_t)function };
    return intern_get(&my links, key, 2);
}

Ty const* ty_return_expressions_context(Checker* me, FunctionImpl const* function) {
    Ty const* ret = ty_contextual_return_type(me, function);
    if (ret || !function->is_iife) return ret;
    u64 key[2] = { KEY_CONTEXT_IIFE_RETURN, (u64)(uintptr_t)function };
    return intern_get(&my links, key, 2);
}

// A function expression meets a contextual type. Its unannotated parameters take their types from the contextual
// signature, once: the first contextual signature decides (tsc's ContextChecked)
Signature const* ty_function_contextual_signature(Checker* me, FunctionImpl const* function, Ty const* contextual) {
    return contextual_signature(me, ty_instantiate_contextual_type_for(me, contextual, true), function, 0);
}

Signature const* ty_function_assigned_contextual_signature(Checker* me, FunctionImpl const* function) {
    u64 key[2] = { KEY_CONTEXT_DONE, (u64)(uintptr_t)function };
    return intern_get(&my links, key, 2);
}

// What a JavaScript function's synthetic `args` rest parameter (it uses `arguments`) is contextually typed as: the rest
// of its contextual signature from position `index` on (tsc's assignContextualParameterTypes for a DeferredType rest
// parameter). Null without a contextual signature
Ty const* ty_contextual_synthetic_rest_type(Checker* me, FunctionImpl const* function, u32 index) {
    Signature const* sig = ty_function_assigned_contextual_signature(me, function);
    if (!sig) return nullptr;
    Ty const* type = signature_type_at(me, sig, index, true);
    return type;
}

// The contextual type of a function's return expressions: its contextual signature's return type, as the last check of
// the function saw it (tsc goes through the resolved signature of the call it is an argument of), instantiated where it
// is looked at while the call infers (ty_instantiate_contextual_type())
// When that signature is the function's own (the call it is an argument of inferred its parameter's type from it:
// `id(() => "x")`), its unit return type is the literal of its context and stays (tsc's getReturnTypeFromBody,
// `contextualSignature === getSignatureFromDeclaration(func)`)
static void set_contextual_return(Checker* me, FunctionImpl const* function, Signature const* sig) {
    if (function->ret_explicit) return;
    // (Later checks of its return expressions see the return type it has then, like tsc's getContextualReturnType)
    if (sig->function == function && !sig->target) {
        *intern_slot(me, &my links, (u64[]){ KEY_CONTEXT_RETURN_SELF, (u64)(uintptr_t)function }, 2) = function;
        // (Not while its return type is being decided: a call re-entered from its body checks it against itself)
        if (ty_function_return_in_flight(me, function)) return;
    }
    Ty const* ret = signature_return_type(me, sig);
    u64 key[2] = { KEY_CONTEXT_RETURN, (u64)(uintptr_t)function };
    // (A `void` context says nothing about what a function returns, except an async one's: its return expressions are
    // contextually typed `void | PromiseLike<void>`, so `return new Promise(r => r())` infers `void`: tsc's
    // getContextualTypeForReturnExpression)
    u32 ignored = function->is_async && !function->is_generator ? TYF_ANY : TYF_ANY | TYF_VOID;
    if (ret && !(ret->flags & ignored)) *intern_slot(me, &my links, key, 2) = ret;
}

// A function whose first check had no contextual signature (KEY_CONTEXT_NONE) has its return type inferred with the
// context of its last check once the call it is an argument of is resolved, and without any while it isn't (tsc's
// getReturnTypeFromBody asks getContextualSignatureForFunctionLikeDeclaration, through the call's resolvedSignature
// or resolvingSignature): `asFunction(() => 1)` returns `() => 1` (its own signature is the context)
void ty_late_contextual_return(Checker* me, FunctionImpl const* function) {
    CallExpr const* call = intern_get(&my links, (u64[]){ KEY_CONTEXT_NONE, (u64)(uintptr_t)function }, 2);
    if (!call || !ty_call_resolved_return(me, call)) return;
    Ty const* contextual = intern_get(&my links, (u64[]){ KEY_FUNCTION_SIG_CONTEXT, (u64)(uintptr_t)function }, 2);
    if (!contextual) return;
    // HACK: only its own type (what the call inferred from it), whose signature keeps a literal it returns: tsc only asks
    // for the contextual return type of a unit return type, any other would be inferred now, eagerly, and may need this
    // very return type (a composite of it and its siblings: compositeContextualSignature)
    Ty const* own = ty_function_type(me, function);
    if (contextual->kind == TY_UNION) {
        // (Its own type among members without signatures: `T | number`)
        bool found = false;
        for (u32 i = 0; i < contextual->composite.types.len; i++) {
            Ty const* member = contextual->composite.types.data[i];
            if (member == own) found = true;
            else if ((member->flags & (TYF_OBJECT | TYF_INTERSECTION)) && ty_members(me, member)->call_count) return;
        }
        if (!found) return;
    } else if (contextual != own) {
        return;
    }
    Members const* members = ty_members(me, own);
    if (members->call_count == 1) set_contextual_return(me, function, members->call[0]);
}

// `evaluating`: the parameter whose type evaluation asked for this, which takes its contextual type like the others
// `raw`: the contextual type before a generic call's inference instantiated it (null: the same)
static void assign_contextual_signature_as(
    Checker* me, FunctionImpl const* function, Ty const* contextual, Signature const* given, InitializedBinding const* evaluating,
    Ty const* raw_contextual
) {
    u64 done_key[2] = { KEY_CONTEXT_DONE, (u64)(uintptr_t)function };
    if (intern_get(&my links, done_key, 2)) return;
    Signature const* sig = given ? given : contextual_signature(me, ty_instantiate_contextual_type_for(me, contextual, true), function, 0);
    if (!sig) return;
    // The type parameters of the generic call this is an argument of are fixed now (a contextual type that left them
    // generic because nothing was inferred for them: `unknown`)
    Signature const* unfixed = sig;
    sig = ty_fix_contextual_signature(me, sig, function);
    *intern_slot(me, &my links, done_key, 2) = sig;
    if (sig != unfixed) *intern_slot(me, &my links, (u64[]){ KEY_CONTEXT_UNFIXED, (u64)(uintptr_t)function }, 2) = unfixed;
    if (sig->this_type) {
        u64 this_key[2] = { KEY_CONTEXT_THIS, (u64)(uintptr_t)function };
        *intern_slot(me, &my links, this_key, 2) = sig->this_type;
    }
    u32 index = 0;
    for_each (i, vec_function_arg, &function->args) {
        bool is_this = i->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(i->initializer.binding.content_span, "this");
        if (is_this) {
            // An unannotated `this` parameter takes the contextual signature's (tsc's assignContextualParameterTypes)
            if (!i->initializer.binding.has_explicit_type && sig->this_type) {
                *intern_slot(me, &my links, (u64[]){ KEY_CONTEXT_PARAM, (u64)(uintptr_t)&i->initializer }, 2) = sig->this_type;
            }
            continue;
        }
        u32 at = index++;
        // A parameter with an initializer takes the contextual type too (tsc's getContextuallyTypedParameterType)
        if (i->initializer.binding.has_explicit_type) continue;
        LazyMemo* param_memo = lazy_type_state_peek((LazyType*)&i->initializer.lazy_type);
        if (param_memo && param_memo->evaluation_started && &i->initializer != evaluating) continue;
        Ty const* type = signature_type_at(me, sig, at, i->rest);
        if (!type && !given) {
            // Nothing there once instantiated (`(...args: TArgs)` with TArgs inferred as []): the parameter's own type
            // asks the contextual signature again, uninstantiated (tsc's assignParameterType without a type ->
            // getContextuallyTypedParameterType), `TArgs[0]`
            Signature const* raw = unfixed != sig ? unfixed : contextual_signature(me, raw_contextual ? raw_contextual : contextual, function, 0);
            if (raw && raw != sig) type = signature_type_at(me, raw, at, i->rest);
        }
        if (!type) continue;
        if (initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY) {
            // An initializer that doesn't fit the contextual type but is wider than it wins (tsc's
            // assignContextualParameterTypes): `(max = 3) => {}` against `(max: 1 | 2) => void` takes a number
            // (Checked with that contextual type: tsc's checkDeclarationInitializer, getContextuallyTypedParameterType)
            Ty const* initializer = ty_check_expr(me, initialized_binding_value(&i->initializer), type);
            if (!ty_is_related(me, initializer, type, TY_RELATION_ASSIGNABLE)) {
                Ty const* widened = ty_widened_for_declaration(me, initializer, true);
                if (ty_is_related(me, type, widened, TY_RELATION_ASSIGNABLE)) type = widened;
            }
        }
        // A destructured parameter inference gave nothing but `unknown` falls back to its pattern's type (tsc's
        // assignParameterType)
        if (type == ty_intrinsic(me, TY_UNKNOWN_) &&
            (i->initializer.binding.tag == BINDING_OBJECT || i->initializer.binding.tag == BINDING_ARRAY)
        ) {
            type = ty_binding_pattern_type(me, &i->initializer.binding, false);
            u64 pattern_key[2] = { KEY_CONTEXT_PARAM_PATTERN, (u64)(uintptr_t)&i->initializer.binding };
            *intern_slot(me, &my links, pattern_key, 2) = type;
        }
        u64 key[2] = { KEY_CONTEXT_PARAM, (u64)(uintptr_t)&i->initializer };
        *intern_slot(me, &my links, key, 2) = type;
    }
    set_contextual_return(me, function, unfixed);
}

void ty_function_apply_jsdoc_type(Checker* me, FunctionImpl const* function, InitializedBinding const* evaluating) {
    if (!function_impl_rare(function)->jsdoc_signature_type) return;
    // A declaration's parameters take the types at their positions whatever its arity (tsc's getParameterTypeOfTypeTag
    // through getSignatureOfTypeTag, not a contextual signature)
    Ty const* type = ty_of_node(function_impl_rare(function)->jsdoc_signature_type);
    assign_contextual_signature_as(me, function, type, nullptr, evaluating, nullptr);
    u64 done_key[2] = { KEY_CONTEXT_DONE, (u64)(uintptr_t)function };
    if (intern_get(&my links, done_key, 2) || function->is_expression || !(type->flags & TYF_OBJECT)) return;
    // (One the arity rules out as a contextual signature: more parameters than it has)
    Members const* members = ty_members(me, type);
    if (members->call_count == 1 && !members->construct_count && !members->call[0]->type_params.len) {
        assign_contextual_signature_as(me, function, type, members->call[0], evaluating, nullptr);
    }
}

// tsc's hasContextSensitiveParameters
static bool has_context_sensitive_params(FunctionImpl const* function) {
    if (vec_generic_item_size(&function->generic.items)) return false;
    for_each (i, vec_function_arg, &function->args) {
        if (!i->initializer.binding.has_explicit_type) return true;
    }
    // A function expression's `this` is contextually typed too, unless it declares one
    if (!function_impl_is_arrow(function)) {
        FunctionArg const* first = vec_function_arg_size(&function->args) ? vec_function_arg_at(&function->args, 0) : nullptr;
        if (!(first && first->initializer.binding.tag == BINDING_IDENTIFIER &&
            span_equals(first->initializer.binding.content_span, "this"))
        ) {
            return true;
        }
    }
    return false;
}

bool ty_function_is_context_sensitive(FunctionImpl const* function) {
    if (vec_generic_item_size(&function->generic.items)) return false;
    if (has_context_sensitive_params(function)) return true;
    // So is one whose returned expression is (tsc's hasContextSensitiveReturnExpression): `() => x => x.length`
    return !function->ret_explicit && function->has_body &&
        statements_any_return(&function->body.statements, ty_expr_is_context_sensitive);
}

bool ty_expr_is_context_sensitive(Expr const* expr) {
    switch (expr->tag) {
        case EXPR_FUNCTION: return ty_function_is_context_sensitive(expr->function);
        case EXPR_OBJECT:
            for_each (i, vec_object_expr_item, &expr->object) {
                if (ty_expr_is_context_sensitive(&i->value)) return true;
            }
            return false;
        case EXPR_ARRAY:
            for_each (i, vec_expr, &expr->array) {
                if (ty_expr_is_context_sensitive(i)) return true;
            }
            return false;
        case EXPR_TERNARY:
            return ty_expr_is_context_sensitive(&expr->ternary->yes) || ty_expr_is_context_sensitive(&expr->ternary->no);
        case EXPR_BINARY_OP:
            return (expr->binary_op->op == BINARY_OP_OR_OR || expr->binary_op->op == BINARY_OP_EH_EH) &&
                (ty_expr_is_context_sensitive(&expr->binary_op->left) || ty_expr_is_context_sensitive(&expr->binary_op->right));
        case EXPR_JSX_ATTRIBUTES: {
            // Its attributes, and the children of its element
            for_each (i, vec_jsx_attribute, &expr->jsx_attributes) {
                if (i->tag == JSX_ATTRIBUTE_EXPR && ty_expr_is_context_sensitive(&i->value)) return true;
            }
            for_each (i, vec_jsx_child, &jsx_attributes_element(expr)->children) {
                if (i->tag == JSX_CHILD_EXPR && !i->spread && ty_expr_is_context_sensitive(&i->expr)) return true;
            }
            return false;
        }
        default:
            return false;
    }
}

// Identifiers

// tsc's checkSuperExpression: for a property access the base class instance with `this` as this-argument, in a static
// member the base constructor; the callee of `super(...)` is the base constructor
static Ty const* super_type(Checker* me, IdentifierExpr const* identifier) {
    ClassDecl const* class_decl = identifier->target ? symbol_find_node(identifier->target, SYMBOL_DECL_CLASS) : nullptr;
    // Where it can't be (TS2335 & co.) it's the error type, in an object literal's method any (checkSuperExpression)
    if (identifier->super_invalid) return ty_intrinsic(me, TY_ERROR);
    if (!class_decl || class_decl->extends.tag == EXPR_IMPLICIT_ANY) return any_type(me);
    // `extends null`: `super()` is an error of its own (TS17005), `super.x` reads null (tsc's checkSuperExpression)
    if (class_decl->extends.tag == EXPR_LITERAL && class_decl->extends.literal.tag == VALUE_NULL) {
        return identifier->super_member ? ty_intrinsic(me, TY_NULL_WIDENING) : ty_intrinsic(me, TY_ERROR);
    }
    // (No base type, the base class got the wrong number of type arguments: tsc's checkSuperExpression)
    if (!class_decl_base_arity_fits(class_decl)) return ty_intrinsic(me, TY_ERROR);
    if (!identifier->super_member) {
        Expr const* extends = &class_decl->extends;
        if (extends->tag == EXPR_GENERIC_PARAMS) extends = &extends->generic_params->expr;
        Ty const* base = ty_expr_type(me, extends);
        // Not a constructor type (TS2507): the error type (tsc's getBaseConstructorTypeOfClass)
        if (base->kind == TY_ANONYMOUS && !ty_members(me, base)->construct_count) return ty_intrinsic(me, TY_ERROR);
        // (Nor is one whose construct signatures don't take the type arguments, TS2508: no base type)
        if (ty_class_base_constructor_unfit(me, class_decl)) return ty_intrinsic(me, TY_ERROR);
        return base;
    }
    if (identifier->this_kind == THIS_CLASS_STATIC) return ty_expr_type(me, &class_decl->extends);
    Ty const* base = ty_class_base_instance(me, class_decl);
    if (!base) {
        // (Extending any or the error type, e.g. an unresolved import, it's the base type as it is: tsc's
        // resolveBaseTypesOfClass. So `super.m<T>()` on the error type isn't TS2347)
        Expr const* extends = &class_decl->extends;
        if (extends->tag == EXPR_GENERIC_PARAMS) extends = &extends->generic_params->expr;
        Ty const* constructor = ty_expr_type(me, extends);
        if (constructor->flags & TYF_ANY) return constructor;
        // (No construct signature takes the type arguments, TS2508, or none at all: no base, the error type)
        return ty_class_base_constructor_unfit(me, class_decl) ? ty_intrinsic(me, TY_ERROR) : any_type(me);
    }
    Ty const* declared = ty_declared_type_of(me, identifier->target);
    Ty const* this_param = declared->kind == TY_DECLARED ? declared->declared.this_param : nullptr;
    return this_param ? ty_with_this_arg(me, base, this_param) : base;
}

Ty const* ty_identifier_reference_type(Checker* me, IdentifierExpr const* identifier) {
    Symbol const* decl = identifier->target;
    // (`import * as ns` of an `export =` module can be more than what it assigns: see ty_type_of_symbol())
    if (identifier->symbol && (identifier->symbol->flags & SYMBOL_ALIAS) && identifier->symbol != decl) {
        decl = identifier->symbol;
    }
    if (identifier->keyword == KEYWORD_IDENTIFIER_SUPER) return super_type(me, identifier);
    switch (identifier->this_kind) {
        case THIS_NONE: break;
        case THIS_ANY: {
            Ty const* js_this = identifier_this_function(identifier) ? ty_js_this_type(me, identifier_this_function(identifier)) : nullptr;
            if (js_this) return js_this;
            Ty const* contextual = identifier_this_function(identifier) ? ty_contextual_this_type(me, identifier_this_function(identifier)) : nullptr;
            return contextual ? contextual : any_type(me);
        }
        case THIS_PARAMETER: break;
        case THIS_CLASS_STATIC: return ty_type_of_symbol(me, decl); // (With what a namespace merges in)
        case THIS_INTERFACE: return any_type(me);
        case THIS_GLOBAL: return my binder->global_this ? ty_type_of_symbol(me, my binder->global_this) : any_type(me);
        case THIS_COMMONJS: return ty_commonjs_exports_type(me, identifier_expr_name(identifier).source);
        case THIS_NAMESPACE: case THIS_ENUM: return any_type(me);
        case THIS_CLASS_INSTANCE: {
            // The class's `this` type (else the instance type, for a generic class applied to its own parameters)
            Ty const* declared = ty_declared_type_of(me, decl);
            if (declared->kind == TY_DECLARED && declared->declared.this_param) return declared->declared.this_param;
            if (declared->kind == TY_DECLARED && declared->declared.type_params.len) {
                return ty_reference(me, declared, declared->declared.type_params);
            }
            return declared;
        }
    }
    if (identifier->keyword == KEYWORD_IDENTIFIER_NEW_TARGET) {
        // tsc's checkNewTargetMetaProperty(): the class of a constructor, else the function
        if (decl) return ty_type_of_symbol(me, decl);
        return identifier_this_function(identifier) ? ty_function_type(me, identifier_this_function(identifier)) : ty_intrinsic(me, TY_ERROR);
    }
    if (!decl && identifier->is_arguments && !identifier->invalid_initializer_reference) {
        Symbol const* arguments = program_get_global(my program, c_sv("IArguments"), SYMBOL_TYPE);
        return arguments ? ty_declared_type_of(me, arguments) : ty_empty_object_type(me); // (tsc's getGlobalType fallback)
    }
    if (!decl) {
        // TODO: `this` outside of anything that has one is `typeof globalThis` (or implicitly any)
        // (A name that wasn't found is tsc's errorType)
        return identifier->keyword == KEYWORD_IDENTIFIER_THIS ? ty_intrinsic(me, TY_UNDEFINED) : ty_intrinsic(me, TY_ERROR);
    }
    // tsc's checkIdentifier: assigning to what isn't a variable, or to a constant, is an error of type errorType
    // (A JavaScript file's `exports = ...` is no error)
    if (identifier_is_invalid_assignment_target(identifier) &&
        !(identifier_expr_name(identifier).source && identifier_expr_name(identifier).source->is_js && (identifier->symbol->flags & SYMBOL_VALUE_MODULE))
    ) {
        return ty_intrinsic(me, TY_ERROR);
    }
    // `exports` of a CommonJS module that assigns `module.exports` is what it assigns (tsc's
    // getTypeOfFuncClassEnumModuleWorker of the file's symbol)
    SourceFile const* file = identifier_expr_name(identifier).source;
    if (decl->is_module && file && file->is_commonjs && file->scope && file->scope->container == decl &&
        decl->exports && symbol_table_get(decl->exports, c_sv("export="))
    ) {
        return ty_commonjs_exports_type(me, file);
    }
    return ty_type_of_symbol(me, decl);
}

static bool includes_undefined(Ty const* ty) {
    if (ty->flags & TYF_UNDEFINED) return true;
    if (ty->kind != TY_UNION) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (ty->composite.types.data[i]->flags & TYF_UNDEFINED) return true;
    }
    return false;
}

static Ty const* base_type_of_literal(Checker* me, Ty const* ty) { return ty_base_type_of_literal(me, ty); }

static bool is_generic_with_union_constraint(Checker* me, Ty const* ty) {
    if (ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (is_generic_with_union_constraint(me, ty->composite.types.data[i])) return true;
        }
        return false;
    }
    if (ty->kind != TY_TYPE_PARAM && !(ty->flags & TYF_DEFERRED)) return false;
    Ty const* constraint = ty_base_constraint(me, ty);
    return constraint && constraint != ty && ((constraint->flags & TYF_NULLABLE) || constraint->kind == TY_UNION);
}

static bool is_generic_type(Checker* me, Ty const* ty);

bool ty_is_generic_type(Checker* me, Ty const* ty) {
    return is_generic_type(me, ty);
}

// The contextual signature tsc's shouldReportErrorsFromWideningWithContextualSignature sees when a call
// with a return mapper checks a function argument: the parameter type's, not the deeply instantiated one tttt checks
// the function with (KEY_CONTEXT_WIDENING, first one wins)
void ty_function_note_widening_context(Checker* me, FunctionImpl const* function, Ty const* param_type) {
    if (compiler_options()->strict_null_checks || !compiler_options()->no_implicit_any || function->ret_explicit) return;
    Signature const** slot = (Signature const**)intern_slot(me, &my links, (u64[]){ KEY_CONTEXT_WIDENING, (u64)(uintptr_t)function }, 2);
    if (*slot || intern_get(&my links, (u64[]){ KEY_CONTEXT_WIDENING_ABSENT, (u64)(uintptr_t)function }, 2)) return;
    Signature const* sig = contextual_signature(me, ty_instantiate_contextual_type_for(me, param_type, true), function, 0);
    if (sig) *slot = sig;
}

bool ty_function_context_is_self(Checker* me, FunctionImpl const* function) {
    // (Only when its first check had no contextual signature: one it had then decided its return type, no circularity)
    return intern_get(&my links, (u64[]){ KEY_CONTEXT_NONE, (u64)(uintptr_t)function }, 2) &&
        intern_get(&my links, (u64[]){ KEY_CONTEXT_RETURN_SELF, (u64)(uintptr_t)function }, 2);
}

// tsc's shouldReportErrorsFromWideningWithContextualSignature for WideningKind.FunctionReturn (GeneratorYield with
// `yield`): a function expression's contextual signature, if it has one, has a generic return type (a generator's: the
// return or yield type it iterates with)
bool ty_function_widening_reportable(Checker* me, FunctionImpl const* function, bool yield) {
    // (The first contextual signature, which tsc's getReturnTypeFromBody reports with: while a call infers, its type
    // parameters are still there)
    Signature const* sig = intern_get(&my links, (u64[]){ KEY_CONTEXT_WIDENING, (u64)(uintptr_t)function }, 2);
    if (!sig && intern_get(&my links, (u64[]){ KEY_CONTEXT_WIDENING_ABSENT, (u64)(uintptr_t)function }, 2)) return true;
    if (!sig) sig = intern_get(&my links, (u64[]){ KEY_CONTEXT_UNFIXED, (u64)(uintptr_t)function }, 2);
    if (!sig) sig = ty_function_assigned_contextual_signature(me, function);
    if (!sig) {
        Ty const* contextual = intern_get(&my links, (u64[]){ KEY_FUNCTION_CONTEXT, (u64)(uintptr_t)function }, 2);
        sig = contextual ? ty_function_contextual_signature(me, function, contextual) : nullptr;
    }
    if (!sig) return true;
    Ty const* ret = signature_return_type(me, sig);
    if (function->is_generator) {
        TyIterationTypes const* types = ty_generator_return_iteration_types(me, ret, function->is_async);
        if (yield) return types && types->yield_type && is_generic_type(me, types->yield_type);
        if (types && types->return_type) ret = types->return_type;
    } else if (function->is_async) {
        Ty const* awaited = ty_awaited_no_alias(me, ret);
        if (awaited) ret = awaited;
    }
    return is_generic_type(me, ret);
}

static Ty const* base_constraint_or_type(Checker* me, Ty const* ty) {
    if (ty->kind == TY_INTERSECTION && is_generic_type(me, ty)) {
        // The intersection of its members' base constraints, without the members that have none (tsc's
        // computeBaseConstraint)
        TyList members = ty->composite.types;
        Ty const** parts = malloc(sizeof(Ty const*) * members.len);
        panic_if(!parts, "out of memory");
        u32 len = 0;
        for (u32 i = 0; i < members.len; i++) {
            Ty const* member = members.data[i];
            bool instantiable = member->kind == TY_TYPE_PARAM || (member->flags & TYF_DEFERRED);
            Ty const* constraint = instantiable ? ty_base_constraint(me, member) : member;
            if (constraint) parts[len++] = constraint;
        }
        Ty const* ret = len ? ty_intersection(me, parts, len, nullptr, (TyList){0}) : ty;
        free(parts);
        return ret;
    }
    Ty const* constraint = ty->kind == TY_TYPE_PARAM || (ty->flags & TYF_DEFERRED) ? ty_base_constraint(me, ty) : nullptr;
    return constraint ? constraint : ty;
}

static bool has_generic_with_union_constraint(Checker* me, Ty const* ty) {
    if (ty->kind != TY_UNION) return is_generic_with_union_constraint(me, ty);
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (is_generic_with_union_constraint(me, ty->composite.types.data[i])) return true;
    }
    return false;
}

// tsc's isGenericType: a generic object type (a type variable, a mapped type over generic keys, a generic tuple) or a
// generic index type (a type variable, `keyof`, a template or string mapping that isn't a pattern), at the top or in a
// union or intersection
static bool is_generic_type(Checker* me, Ty const* ty) {
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (is_generic_type(me, ty->composite.types.data[i])) return true;
        }
        return false;
    }
    if (ty->kind == TY_TEMPLATE_LITERAL || ty->kind == TY_STRING_MAPPING) return !ty_is_pattern_literal(ty);
    return ty_is_generic_object(me, ty);
}

// A binding pattern's implied type (TYF_BINDING_PATTERN) of a pattern with a rest element: an object one has an
// index signature, an array one a rest element
static bool pattern_type_has_rest(Checker* me, Ty const* pattern) {
    if (ty_is_tuple(pattern)) {
        TupleShape const* shape = pattern->reference.target->declared.tuple;
        return shape->arity && (shape->flags[shape->arity - 1] & TUPLE_ELEMENT_REST);
    }
    return (pattern->flags & TYF_OBJECT) && ty_members(me, pattern)->index_count;
}

// tsc's getNarrowableTypeForReference: in a constraint position, or where the contextual type isn't generic, a generic
// type with a union constraint is narrowed as its constraint (`c && c.flags` with `c: T extends Ctx | undefined` reads
// `flags` of Ctx; `isA(x) ? takesA(x) : takesB(x)` with `x: T extends A | B`)
// tsc's someType(type, isGenericTypeWithoutNullableConstraint)
static bool has_generic_without_nullable_constraint(Checker* me, Ty const* type) {
    TyList members = type->kind == TY_UNION ? type->composite.types : (TyList){ .data = &type, .len = 1 };
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = members.data[i];
        if (!(member->flags & (TYF_TYPE_PARAM | TYF_DEFERRED))) continue;
        Ty const* constraint = base_constraint_or_type(me, member);
        if (!ty_can_be_null(me, constraint) && !ty_can_be_undefined(me, constraint)) return true;
    }
    return false;
}

// `element_index`: the index of `x[i]` that the reference is the object of (null for any other constraint position)
static Ty const* narrowable_type(Checker* me, bool constraint_position, Expr const* element_index, Ty const* type, Ty const* contextual) {
    if (ty_is_no_infer(type)) type = type->substitution.base;
    // HACK: tsc types the initializer of a pattern with a rest element again for the rest (CheckMode.RestBindingElement),
    // without the pattern as contextual type, so that the rest of a `T` stays generic. tttt has one type per
    // expression: a pattern with a rest element isn't a contextual type here
    if (contextual && (contextual->flags & TYF_BINDING_PATTERN) && pattern_type_has_rest(me, contextual)) contextual = nullptr;
    if (!has_generic_with_union_constraint(me, type)) return type;
    // `x[i]` with a generic i keeps a type variable of x that can't be nullable as it is (tsc's isConstraintPosition)
    if (constraint_position && element_index && has_generic_without_nullable_constraint(me, type) &&
        ty_is_generic_index(ty_expr_type(me, element_index))
    ) {
        constraint_position = false;
    }
    bool substitute = constraint_position ||
        (contextual && !my inferential_depth && !is_generic_type(me, contextual));
    if (!substitute) return type;
    Ty const* const* members = &type;
    u32 count = 1;
    if (type->kind == TY_UNION) {
        members = type->composite.types.data;
        count = type->composite.types.len;
    }
    if (count == 1) return base_constraint_or_type(me, type);
    Ty const** mapped = malloc(sizeof(Ty const*) * count);
    panic_if(!mapped, "out of memory");
    for (u32 i = 0; i < count; i++) mapped[i] = base_constraint_or_type(me, members[i]);
    Ty const* ret = ty_union(me, mapped, count, nullptr, (TyList){0});
    free(mapped);
    return ret;
}

// `(o: T | undefined = {})` (or an optional parameter typed by the context or a JSDoc `@type` tag) is a T in the body
// unless the default can be undefined (tsc's removeOptionalityFromDeclaredType)
static Ty const* parameter_initial_type(Checker* me, Binding const* binding, Ty const* type) {
    if (!binding || !compiler_options()->strict_null_checks || !includes_undefined(type) || !binding->source ||
        binding->source->tag != LAZY_TYPE_INITIALIZED_BINDING
    ) {
        return type;
    }
    InitializedBinding const* initialized = c_container_of(binding->source, InitializedBinding, lazy_type);
    // (Not from inside the default itself: `(x: number | undefined = x > 0 ? x : 0)`)
    LazyMemo const* memo = lazy_type_state_peek((LazyType*)&initialized_binding_value(initialized)->lazy_type);
    bool in_flight = memo && memo->evaluation_started && !memo->done;
    Ty const* value = initialized_binding_value(initialized)->tag == EXPR_IMPLICIT_ANY || in_flight ? nullptr : ty_expr_type(me, initialized_binding_value(initialized));
    // (`any` and `unknown` can't be undefined here, tsc's UnknownFacts)
    // (A type parameter by its constraint: tsc's hasTypeFacts(IsUndefined))
    bool may_be_undefined = value && (includes_undefined(value) || (value->flags & TYF_VOID) ||
        (!(value->flags & (TYF_ANY | TYF_UNKNOWN)) && ty_can_be_undefined(me, value)));
    if (value && !may_be_undefined) {
        return ty_without_undefined(me, type);
    }
    return type;
}

// The tail of tsc's checkIdentifier(): a variable or parameter has the type control flow gives it where it is read
// With `issue_only` it only looks for the issue: nullptr when there can't be one, before walking the flow graph (every
// identifier is asked when checked, and its type has been computed with its contextual type already)
// The flow type of a variable, except that `x!` where control flow says x is only null or undefined is the declared type
// (tsc's getFlowTypeOfReference: "the operand of a non-null assertion is never nullish")
static Ty const* non_null_flow_type(Checker* me, Expr const* expr, Ty const* declared, Ty const* initial) {
    Ty const* flow_type = ty_flow_type(me, expr, declared, initial, expr->identifier.flow);
    if (expr->identifier.non_null_operand && !(flow_type->flags & TYF_NEVER) &&
        // (getTypeWithFacts(NEUndefinedOrNull), whatever strictNullChecks says: `undefined` has no non-nullish part)
        (ty_non_nullish_part(me, flow_type)->flags & TYF_NEVER)
    ) {
        return declared;
    }
    return flow_type;
}

static Ty const* identifier_flow_type(Checker* me, Expr const* expr, Ty const* contextual, IdentifierFlowIssue* issue, bool issue_only) {
    IdentifierExpr const* identifier = &expr->identifier;
    if (my contextual_pattern_count && identifier->target && (identifier->target->flags & SYMBOL_VARIABLE)) {
        Binding const* binding = symbol_find_node(identifier->target, SYMBOL_DECL_VARIABLE);
        Binding const* pattern = binding ? binding->parent : nullptr;
        for (u32 i = 0; pattern && i < my contextual_pattern_count; i++) {
            if (my contextual_patterns[i] == pattern && span_start_ptr(pattern->content_span) <= span_start_ptr(expr_outer_span(expr)) &&
                span_end_ptr(expr_outer_span(expr)) <= span_end_ptr(pattern->content_span)
            ) {
                return ty_intrinsic(me, TY_ANY);
            }
        }
    }
    Ty const* type = narrowable_type(me, identifier->constraint_position, identifier_element_index(identifier), ty_identifier_reference_type(me, identifier), contextual);
    Symbol const* symbol = identifier->target;
    if (identifier->keyword == KEYWORD_IDENTIFIER_THIS) {
        if (issue_only) return nullptr;
        return identifier->flow ? ty_flow_type(me, expr, type, type, identifier->flow) : type;
    }
    if (!symbol || identifier->keyword || !(symbol->flags & SYMBOL_VARIABLE)) return type;
    if (identifier->assignment_target == FLOW_TARGET_DEFINITE) {
        return identifier->compound_like_target ? base_type_of_literal(me, type) : type;
    }
    if (!identifier->flow) return type;
    Binding const* binding = symbol_find_node(symbol, SYMBOL_DECL_VARIABLE);
    Ty const* auto_any = ty_intrinsic(me, TY_AUTO);
    if (type == auto_any || (type == my auto_array && type)) {
        // `let x;`, `let x = []` (tsc's checkIdentifier). Undefined until something is assigned. From a closure it is
        // assumed to be assigned, to something control flow may not know: any unless every path leads to an
        // assignment in the closure. `x!` is what was assigned, non-nullable
        bool non_null = identifier->non_null_operand;
        bool assume_initialized = non_null || (identifier->outer_reference && (symbol->is_assigned || type != auto_any));
        Ty const* initial = assume_initialized && !non_null ? type : ty_intrinsic(me, TY_UNDEFINED);
        Ty const* flow_type = non_null_flow_type(me, expr, type, initial);
        // (tsc's getNonNullableType makes an intersection of any, which is the plain any)
        if (non_null && compiler_options()->strict_null_checks) {
            flow_type = flow_type == auto_any ? ty_intrinsic(me, TY_ANY) : ty_non_nullable(me, flow_type);
        }
        bool still_auto = flow_type == auto_any || flow_type == my auto_array;
        if (still_auto || (!assume_initialized && includes_undefined(flow_type))) {
            if (!ty_is_evolving_array_operation_target(me, expr)) {
                if (still_auto) {
                    *issue = flow_type == auto_any ? IDENTIFIER_FLOW_IMPLICIT_ANY : IDENTIFIER_FLOW_IMPLICIT_ANY_ARRAY;
                    return flow_type == auto_any ? ty_intrinsic(me, TY_ANY) : ty_array(me, ty_intrinsic(me, TY_ANY), false);
                }
            } else if (!assume_initialized && includes_undefined(flow_type)) {
                *issue = IDENTIFIER_FLOW_UNASSIGNED; // `let x; x.push(1)`
                return type;
            }
        }
        return identifier->assignment_target ? base_type_of_literal(me, flow_type) : flow_type;
    }
    // Where nothing says otherwise, a variable is `undefined` until something is assigned to it
    bool assume_initialized = symbol->is_parameter || symbol->is_ambient || !compiler_options()->strict_null_checks ||
        (type->flags & (TYF_ANY | TYF_UNKNOWN | TYF_VOID)) || !binding || binding->definitely_assigned ||
        identifier->assume_initialized ||
        includes_undefined(type) ||
        // A variable of another function than the one the reference is in (tsc's isOuterVariable): its initial type is
        // the declared type, which is also what the loop caches go by
        (identifier->outer_reference && !ty_symbol_never_initialized(symbol));
    if (issue_only && assume_initialized) return nullptr;
    // What the pattern's other elements say about the value destructured (tsc's getNarrowedTypeOfSymbol)
    if (binding && binding->parent) {
        Ty const* narrowed = ty_destructured_narrowed_type(me, binding, identifier->flow);
        if (narrowed) type = narrowed;
    } else if (binding && symbol->is_parameter) {
        Ty const* narrowed = ty_parameter_narrowed_type(me, binding, identifier->flow);
        if (narrowed) type = narrowed;
    }
    Ty const* initial = symbol->is_parameter ? parameter_initial_type(me, binding, type) : type;
    if (!assume_initialized) {
        Ty const* types[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
        initial = ty_union(me, types, 2, nullptr, (TyList){0});
    }
    Ty const* flow_type = non_null_flow_type(me, expr, type, initial);
    if (!assume_initialized && includes_undefined(flow_type)) {
        *issue = IDENTIFIER_FLOW_UNASSIGNED;
        return type;
    }
    return identifier->assignment_target ? base_type_of_literal(me, flow_type) : flow_type;
}

IdentifierFlowIssue ty_identifier_flow_issue(Checker* me, IdentifierExpr const* identifier) {
    IdentifierFlowIssue ret = IDENTIFIER_FLOW_OK;
    identifier_flow_type(me, c_container_of(identifier, Expr, identifier), nullptr, &ret, true);
    return ret;
}

bool ty_is_import_meta(Expr const* expr) {
    if (expr->tag != EXPR_PROPERTY || expr->property->form != PROPERTY_EXPR_DOT) return false;
    Expr const* object = &expr->property->expr;
    return object->tag == EXPR_IDENTIFIER && object->identifier.keyword == KEYWORD_IDENTIFIER_IMPORT;
}

// The type of `import.x`: ImportMeta for `import.meta`, else the error type (TS17012, tsc's checkImportMetaProperty)
Ty const* ty_import_meta_expr_type(Checker* me, Expr const* expr) {
    return csview_equals(expr->property->prop.inner_span.sv, "meta") ? ty_import_meta_type(me) : ty_intrinsic(me, TY_ERROR);
}

// `import.meta`: the global ImportMeta interface (tsc's getGlobalImportMetaType, `{}` without one)
Ty const* ty_import_meta_type(Checker* me) {
    Symbol const* symbol = program_get_global(my program, c_sv("ImportMeta"), SYMBOL_TYPE);
    return symbol ? ty_declared_type_of(me, symbol) : ty_empty_object_type(me);
}

// A reference that could have a key (flow.c): names and literal keys down to an identifier or `this`. Unlike the
// builder's expr_is_narrowable_reference() also through `?.`: `typeof a?.b === "string" ? a?.b : ""`
static bool is_narrowable_access(Expr const* expr) {
    while (true) {
        switch (expr->tag) {
            case EXPR_IDENTIFIER:
                return true; // (`import` too: the object of `import.meta`)
            case EXPR_PROPERTY: {
                Expr const* key = &expr->property->prop;
                // (`m[E.A]`: a dotted key names something too, see flow.c's is_constant_entity_name())
                if (key->tag != EXPR_LITERAL && key->tag != EXPR_IDENTIFIER && !expr_is_entity_name(key)) return false;
                expr = &expr->property->expr;
                break;
            }
            case EXPR_UNARY_OP:
                if (expr->unary_op->op != UNARY_OP_RIGHT_BANG) return false;
                expr = &expr->unary_op->expr;
                break;
            case EXPR_AS_TYPE:
                if (!expr->as_type->is_jsdoc) return false; // (A JSDoc cast is tsc's ParenthesizedExpression)
                expr = &expr->as_type->expr;
                break;
            case EXPR_BINARY_OP:
                if (expr->binary_op->op != BINARY_OP_COMMA) return false; // `(a, x).y`
                expr = &expr->binary_op->right;
                break;
            default:
                return false;
        }
    }
}

// tsc's getFlowTypeOfAccessExpression()
static Ty const* access_flow_type(Checker* me, Expr const* expr, Ty const* type, Ty const* contextual) {
    PropertyExpr const* access = expr->property;
    if (access->assignment_target == FLOW_TARGET_DEFINITE && access->compound_like_target) return base_type_of_literal(me, type);
    if (!access->flow || access->assignment_target == FLOW_TARGET_DEFINITE) return type;
    if (!is_narrowable_access(expr)) return type;
    // Methods don't narrow
    if (type->kind == TY_ANONYMOUS && type->anonymous.function && type->anonymous.function->is_method) return type;
    if (!access->constraint_position && (type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) && has_generic_with_union_constraint(me, type)) {
        // (What it is depends on the contextual type and on whether an inference is running, see check_with())
        *intern_slot(me, &my links, (u64[]){ KEY_ACCESS_USES_CONTEXT, (u64)(uintptr_t)expr }, 2) = expr;
    }
    type = narrowable_type(me, access->constraint_position, nullptr, type, contextual);
    Ty const* flow_type = ty_flow_type(me, expr, type, type, access->flow);
    // tsc's getFlowTypeOfReference: the operand of a non-null assertion is never nullish (see non_null_flow_type())
    if (access->non_null_operand && !(flow_type->flags & TYF_NEVER) && (ty_non_nullable(me, flow_type)->flags & TYF_NEVER)) {
        return type;
    }
    return access->assignment_target ? base_type_of_literal(me, flow_type) : flow_type;
}

// The second case of tsc's assumeUninitialized: a JavaScript assignment declaration (`d.q = v`) read in the control
// flow container that declares it (tsc's getControlFlowContainer), where it's undefined until assigned
static bool is_read_in_declaring_container(Checker* me, PropertyExpr const* access) {
    if (!my binder->has_assignment_flow_containers) return false;
    if (access->form != PROPERTY_EXPR_DOT || !span_valid(access->prop.inner_span) || property_private_class(access)) return false;
    Ty const* object = ty_expr_type(me, &access->expr);
    if (object->kind == TY_UNION || (object->flags & (TYF_ANY | TYF_ERROR))) return false;
    Ty const* apparent = ty_apparent_type(me, object);
    if (!(apparent->flags & TYF_OBJECT)) return false;
    Prop const* prop = ty_property_of_type(me, apparent, ty_literal_name(me, ty_access_key_type(me, access)));
    Symbol const* symbol = prop && prop->decl ? prop->decl->symbol : nullptr;
    if (!symbol || !symbol->decl_count) return false;
    SymbolDecl const* decl = &symbol->decls[0];
    return decl->kind == SYMBOL_DECL_ASSIGNMENT && decl->js && decl->js->flow_container && decl->js->flow_container == property_context(access)->flow_container;
}

// tsc's getFlowTypeOfAccessExpression with assumeUninitialized: `this.x` in the constructor of the class that declares
// x without an initializer is undefined until assigned (TS2565), and so is a JavaScript assignment declaration in the
// container that assigns it
void ty_check_property_used_before_assigned(Checker* me, Expr const* expr, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    if (!options->strict_null_checks) return;
    PropertyExpr const* access = expr->property;
    if (access->assignment_target == FLOW_TARGET_DEFINITE || !access->flow) return;
    Expr const* object = &access->expr;
    bool this_access = object->tag == EXPR_IDENTIFIER && object->identifier.keyword == KEYWORD_IDENTIFIER_THIS;
    // (`this.x` under strictPropertyInitialization is only the first case)
    if (!(this_access && options->strict_property_initialization) && is_read_in_declaring_container(me, access)) {
        Ty const* type = ty_expr_type(me, expr);
        if (includes_undefined(type)) return;
        Ty const* parts[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
        Ty const* initial = ty_union(me, parts, 2, nullptr, (TyList){0});
        if (!includes_undefined(ty_flow_type(me, expr, type, initial, access->flow))) return;
        error_ctx_error_d(errs, access->prop.inner_span, 2565, "Property '", access->prop.inner_span, "' is used before being assigned.");
        return;
    }
    if (!options->strict_property_initialization || !property_context(access)->in_constructor_of) return;
    if (object->tag != EXPR_IDENTIFIER || object->identifier.keyword != KEYWORD_IDENTIFIER_THIS ||
        object->identifier.this_kind != THIS_CLASS_INSTANCE || object->identifier.target != property_context(access)->in_constructor_of
    ) {
        return;
    }
    if (access->form != PROPERTY_EXPR_DOT || !span_valid(access->prop.inner_span)) return;
    ClassDecl const* class_decl = symbol_find_node(property_context(access)->in_constructor_of, SYMBOL_DECL_CLASS);
    if (!class_decl || class_decl->is_ambient) return;
    ClassItem const* field = class_decl_find_member(class_decl, access->prop.inner_span.sv, false);
    if (!field || field->tag != CLASS_ITEM_FIELD || field->field.value.tag != EXPR_IMPLICIT_ANY || field->field.is_abstract ||
        field->field.is_declare || field->field.is_auto_accessor || class_item_field_is_definite(field)
    ) {
        return;
    }
    Ty const* type = ty_expr_type(me, expr);
    if (includes_undefined(type)) return;
    Ty const* parts[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
    Ty const* initial = ty_union(me, parts, 2, nullptr, (TyList){0});
    if (!includes_undefined(ty_flow_type(me, expr, type, initial, access->flow))) return;
    error_ctx_error_d(errs, access->prop.inner_span, 2565, "Property '", access->prop.inner_span, "' is used before being assigned.");
}

// Operators

static bool extends_strict_nulls(Checker* me, Ty const* source, TyIntrinsic target) {
    return ty_is_related(me, source, ty_intrinsic(me, target), TY_RELATION_ASSIGNABLE_STRICT_NULLS);
}

static bool is_definitely_falsy(Ty const* ty) {
    if (ty->flags & (TYF_NULLABLE | TYF_VOID)) return true;
    if (ty->kind != TY_LITERAL || (ty->flags & TYF_ENUM)) return false;
    if (ty->flags & TYF_STRING_LITERAL) return !ty->literal.string || ty->literal.string->len == 0; // (The value's string is borrowed)
    return !value_to_boolean(&ty->literal.value);
}

// The part of a type that survives `x || ...` being taken: x was truthy
static Ty const* remove_definitely_falsy(Checker* me, Ty const* ty) {
    return ty_remove_definitely_falsy(me, ty);
}

// The falsy values a type allows: what `x && ...` is when x was falsy
static Ty const* definitely_falsy_part(Checker* me, Ty const* ty) {
    if (ty->flags & TYF_BOOLEAN) return ty_intrinsic(me, TY_FALSE);
    if (is_definitely_falsy(ty)) return ty;
    if (ty->flags & TYF_STRING) return ty_string_literal(me, c_sv(""));
    if (ty->flags & TYF_NUMBER) {
        Value zero = value_number(0);
        return ty_literal(me, &zero);
    }
    if (ty->kind == TY_UNION) {
        TyList members = ty->composite.types;
        Ty const** parts = malloc(sizeof(Ty const*) * members.len);
        for (u32 i = 0; i < members.len; i++) parts[i] = definitely_falsy_part(me, members.data[i]);
        Ty const* ret = ty_union(me, parts, members.len, nullptr, (TyList){0});
        free(parts);
        return ret;
    }
    // A type parameter contributes nothing, whatever its constraint (tsc's getDefinitelyFalsyPartOfType)
    if (ty->flags & (TYF_ANY | TYF_UNKNOWN)) return ty;
    return ty_intrinsic(me, TY_NEVER);
}

static BinaryOp without_assignment(BinaryOp op) {
    switch (op) {
        case BINARY_OP_PLUS_EQUAL: return BINARY_OP_PLUS;
        case BINARY_OP_STAR_STAR_EQUAL: return BINARY_OP_STAR_STAR;
        case BINARY_OP_STAR_EQUAL: return BINARY_OP_STAR;
        case BINARY_OP_SLASH_EQUAL: return BINARY_OP_SLASH;
        case BINARY_OP_MOD_EQUAL: return BINARY_OP_MOD;
        case BINARY_OP_MINUS_EQUAL: return BINARY_OP_MINUS;
        case BINARY_OP_LESS_LESS_EQUAL: return BINARY_OP_LESS_LESS;
        case BINARY_OP_GREATER_GREATER_EQUAL: return BINARY_OP_GREATER_GREATER;
        case BINARY_OP_GREATER_GREATER_GREATER_EQUAL: return BINARY_OP_GREATER_GREATER_GREATER;
        case BINARY_OP_AND_EQUAL: return BINARY_OP_AND;
        case BINARY_OP_XOR_EQUAL: return BINARY_OP_XOR;
        case BINARY_OP_OR_EQUAL: return BINARY_OP_OR;
        case BINARY_OP_AND_AND_EQUAL: return BINARY_OP_AND_AND;
        case BINARY_OP_OR_OR_EQUAL: return BINARY_OP_OR_OR;
        case BINARY_OP_EH_EH_EQUAL: return BINARY_OP_EH_EH;
        default: return op;
    }
}

// Some member of `ty` has one of the flags (tsc's maybeTypeOfKind)
static bool maybe_of_kind(Ty const* ty, u32 kind) {
    if (ty->flags & kind) return true;
    if (ty->kind != TY_UNION && ty->kind != TY_INTERSECTION) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (maybe_of_kind(ty->composite.types.data[i], kind)) return true;
    }
    return false;
}

static bool is_bigint_like(Checker* me, Ty const* ty) {
    return (ty->flags & (TYF_BIGINT | TYF_BIGINT_LITERAL)) || extends_strict_nulls(me, ty, TY_BIGINT);
}

// tsc's checkBinaryLikeExpression for `*`, `-`, `<<`... : number, unless bigint is involved
static Ty const* arithmetic_result(Checker* me, Ty const* left, Ty const* right) {
    left = ty_non_nullable(me, left);
    right = ty_non_nullable(me, right);
    u32 any_or_unknown = TYF_ANY | TYF_UNKNOWN;
    u32 bigint_like = TYF_BIGINT | TYF_BIGINT_LITERAL;
    if (((left->flags & any_or_unknown) && (right->flags & any_or_unknown)) ||
        !(maybe_of_kind(left, bigint_like) || maybe_of_kind(right, bigint_like))
    ) {
        return ty_intrinsic(me, TY_NUMBER);
    }
    if (is_bigint_like(me, left) && is_bigint_like(me, right)) return ty_intrinsic(me, TY_BIGINT);
    return any_type(me); // Mixed: an error (tsc's errorType)
}

// What tsc's checkNonNullType leaves of an operand under strictNullChecks: the error type for unknown or if nothing is
// left
static Ty const* non_null_operand(Checker* me, Ty const* type) {
    if (type->flags & TYF_UNKNOWN) return ty_intrinsic(me, TY_ERROR);
    if (!ty_can_be_null(me, type) && !ty_can_be_undefined(me, type)) return type;
    Ty const* non_null = ty_non_nullable(me, type);
    return non_null->flags & (TYF_NULLABLE | TYF_NEVER) ? ty_intrinsic(me, TY_ERROR) : non_null;
}

static Ty const* binary_result(Checker* me, Ty const* left, BinaryOp op, Ty const* right) {
    switch (op) {
        case BINARY_OP_PLUS: {
            // Unless one side is a string, both are checked non-null first (tsc's checkNonNullType of the operands,
            // the error type if nothing is left)
            if (compiler_options()->strict_null_checks) {
                Ty const* string = ty_intrinsic(me, TY_STRING);
                if (!ty_is_related(me, left, string, TY_RELATION_ASSIGNABLE) &&
                    !ty_is_related(me, right, string, TY_RELATION_ASSIGNABLE)
                ) {
                    left = non_null_operand(me, left);
                    right = non_null_operand(me, right);
                }
            }
            // (tsc's strict isTypeAssignableToKind: never is a number first)
            u32 loose = TYF_ANY | TYF_UNKNOWN | TYF_VOID | TYF_NULLABLE;
            if (!((left->flags | right->flags) & loose)) {
                if (extends_strict_nulls(me, left, TY_NUMBER) && extends_strict_nulls(me, right, TY_NUMBER)) {
                    return ty_intrinsic(me, TY_NUMBER);
                }
                if (is_bigint_like(me, left) && is_bigint_like(me, right)) return ty_intrinsic(me, TY_BIGINT);
            }
            bool left_string = extends_strict_nulls(me, left, TY_STRING);
            bool right_string = extends_strict_nulls(me, right, TY_STRING);
            if (left_string || right_string) {
                bool known = (left_string && !(left->flags & TYF_ANY)) || (right_string && !(right->flags & TYF_ANY));
                if (known) return ty_intrinsic(me, TY_STRING);
            } else if (extends_strict_nulls(me, left, TY_NUMBER) && extends_strict_nulls(me, right, TY_NUMBER)) {
                return ty_intrinsic(me, TY_NUMBER);
            }
            // (tsc: the error type if either side is)
            if ((left->flags | right->flags) & TYF_ERROR) return ty_intrinsic(me, TY_ERROR);
            return any_type(me);
        }

        case BINARY_OP_STAR_STAR:
        case BINARY_OP_STAR:
        case BINARY_OP_SLASH:
        case BINARY_OP_MOD:
        case BINARY_OP_MINUS:
        case BINARY_OP_LESS_LESS:
        case BINARY_OP_GREATER_GREATER:
        case BINARY_OP_GREATER_GREATER_GREATER:
        case BINARY_OP_AND:
        case BINARY_OP_XOR:
        case BINARY_OP_OR:
            return arithmetic_result(me, left, right);

        case BINARY_OP_DOUBLE_EQUAL:
        case BINARY_OP_TRIPLE_EQUAL:
        case BINARY_OP_NOT_EQUAL:
        case BINARY_OP_TRIPLE_NOT_EQUAL:
        case BINARY_OP_LESS:
        case BINARY_OP_GREATER:
        case BINARY_OP_LESS_EQUAL:
        case BINARY_OP_GREATER_EQUAL:
        case BINARY_OP_IN:
        case BINARY_OP_INSTANCEOF:
            return ty_intrinsic(me, TY_BOOLEAN);

        case BINARY_OP_AND_AND:
        {
            if (!ty_can_be_truthy(me, left)) return left;
            // (Literal reduction only, `void | undefined` stays. Without strictNullChecks the falsy part is the right
            // operand's base type's: `"a" && "b"` is `"" | "b"`)
            Ty const* falsy = definitely_falsy_part(me, compiler_options()->strict_null_checks ? left : ty_base_type_of_literal(me, right));
            Ty const* parts[2] = { falsy, right };
            return ty_union(me, parts, 2, nullptr, (TyList){0});
        }

        case BINARY_OP_OR_OR: {
            if (!ty_can_be_falsy(me, left)) return left;
            // (tsc's getNonNullableType of it: a T that may be nullish is NonNullable<T>, the identity without strictNullChecks)
            Ty const* types[2] = { ty_non_nullable(me, remove_definitely_falsy(me, left)), right };

            return ty_union_subtype_reduced(me, types, 2);
        }

        case BINARY_OP_EH_EH: {
            if (!ty_can_be_nullish(me, left)) return left;
            Ty const* types[2] = { ty_non_nullable(me, left), right };
            return ty_union_subtype_reduced(me, types, 2);
        }

        case BINARY_OP_EQUAL:
        case BINARY_OP_COMMA:
            return right;

        default:
            // Compound assignment: the type of what gets assigned
            return binary_result(me, left, without_assignment(op), right);
    }
}

// Whether the left operand of `binary` is checked with a contextual type (see check_binary())
static bool left_takes_context(BinaryOpExpr const* binary) {
    return binary->op == BINARY_OP_OR_OR || binary->op == BINARY_OP_EH_EH;
}

// A long `a + b + c + ...` would recurse once per operand (compute -> the left operand's LazyType -> compute): tsc
// checks binary chains iteratively (checkBinaryExpression's state machine). Here the left spine is evaluated bottom
// up first, so each level finds its left operand memoised. Only the part of the spine checked without a contextual
// type, since that's what its memo is; each level's own walk stops at the first finished memo, so a chain is O(n)
static void check_left_spine(Checker* me, BinaryOpExpr const* binary, ExprCtx ctx) {
    if (my flow_uncached) return; // Nothing is memoised
    bool contextual = ctx.contextual && left_takes_context(binary);
    Expr const* first = nullptr;
    u32 depth = 0;
    for (Expr const* e = &binary->left; e->tag == EXPR_BINARY_OP; e = &e->binary_op->left) {
        LazyType* lazy = (LazyType*)&e->lazy_type;
        if (lazy->tag != LAZY_TYPE_EXPR || !lazy->slot) break;
        LazyMemo* memo = lazy_type_state_peek(lazy);
        if (memo && (memo->done || memo->evaluation_started)) break;
        if (!contextual) {
            if (!first) first = e;
            depth++;
        }
        contextual = contextual && left_takes_context(e->binary_op);
    }
    if (depth < 32) return; // Shallow enough to recurse
    Expr const** spine = malloc(sizeof(Expr const*) * depth);
    panic_if(!spine, "out of memory");
    u32 len = 0;
    for (Expr const* e = first; len < depth; e = &e->binary_op->left) spine[len++] = e;
    for (u32 i = len; i-- > 0;) ty_expr_type(me, spine[i]);
    free(spine);
}

// `exports.x = v`, `module.exports = v` in a CommonJS file: an assignment that declares
static bool is_commonjs_declaration(BinaryOpExpr const* binary) {
    if (binary_commonjs_declaration(binary, nullptr) != COMMONJS_TARGET_NONE) return true;
    // `module.exports = v` of any JavaScript file, an ES module's too (tsc's getAssignmentDeclarationKind is syntactic:
    // AssignmentDeclarationKind.ModuleExports), unless v is `void 0`
    SourceFile const* file = expr_outer_span(&binary->left).source;
    if (!file || !file->is_js || binary->op != BINARY_OP_EQUAL || expr_is_perenthisized(&binary->left) ||
        !expr_is_module_exports(&binary->left)
    ) {
        return false;
    }
    Expr const* value = &binary->right;
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    return !(value->tag == EXPR_UNARY_OP && value->unary_op->op == UNARY_OP_VOID && value->unary_op->expr.tag == EXPR_LITERAL);
}

// JavaScript `a.b = {}` (or `F.prototype = {...}`) declaring a member that has expandos of its own (tsc's
// isAssignmentDeclaration in checkBinaryLikeExpression: getAssignedExpandoInitializer is an object literal and the
// symbol has exports)
static bool is_js_container_declaration(BinaryOpExpr const* binary) {
    Expr const* left = &binary->left;
    if (left->tag != EXPR_PROPERTY) return false;
    SourceFile const* file = expr_outer_span(left).source;
    if (!file || !file->is_js) return false;
    Symbol const* symbol = left->property->declares;
    // (A CommonJS export: `module.exports.A = {}; module.exports.A.B = 1`)
    Span name = span_null;
    if (!symbol && file->scope && file->scope->container && file->scope->container->exports &&
        binary_commonjs_declaration(binary, &name) == COMMONJS_TARGET_EXPORTS_PROPERTY
    ) {
        symbol = symbol_table_get(file->scope->container->exports, name.sv);
    }
    if (!symbol) return false;
    if (!symbol->exports || !symbol->exports->count) return false;
    Expr const* value = &binary->right;
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    // (`a.b = a.b || {}`: tsc's getDefaultedExpandoInitializer)
    if (value->tag == EXPR_BINARY_OP && value->binary_op->defaulted_expando) value = &value->binary_op->right;
    if (value->tag != EXPR_OBJECT) return false;
    return !vec_object_expr_item_size(&value->object) || span_equals(expr_static_property_name(left), "prototype");
}

// tsc's getContextualTypeForAssignmentDeclaration for `exports.x = v` and `module.exports = v`: the JSDoc `@type` of
// the export's value declaration (the first assignment that declares it), else none
static Ty const* commonjs_declaration_context(BinaryOpExpr const* binary) {
    if (binary->jsdoc_type) return ty_of_node(binary->jsdoc_type);
    SourceFile const* file = expr_outer_span(&binary->left).source;
    Symbol const* module = file && file->scope ? file->scope->container : nullptr;
    if (!module || !module->exports) return nullptr;
    Span name = span_null;
    CommonJsTarget target = binary_commonjs_declaration(binary, &name);
    Symbol const* symbol = target == COMMONJS_TARGET_EXPORTS_PROPERTY ? symbol_table_get(module->exports, name.sv) :
        target == COMMONJS_TARGET_MODULE_EXPORTS ? symbol_table_get(module->exports, c_sv("export=")) : nullptr;
    if (!symbol || !symbol->decl_count || symbol->decls[0].kind != SYMBOL_DECL_ASSIGNMENT) return nullptr;
    return symbol_decl_jsdoc_type(&symbol->decls[0]) ? ty_of_node(symbol_decl_jsdoc_type(&symbol->decls[0])) : nullptr;
}

static bool expando_target_is_annotated(PropertyExpr const* access) {
    Symbol const* declared = access->declares;
    if (declared->decl_count && symbol_decl_jsdoc_type(&declared->decls[0])) return true;
    if (access->expr.tag != EXPR_IDENTIFIER || access->expr.identifier.keyword) return false;
    Symbol const* parent = access->expr.identifier.target;
    if (!parent || !(parent->flags & SYMBOL_VARIABLE)) return false;
    Binding const* binding = symbol_find_node(parent, SYMBOL_DECL_VARIABLE);
    return binding && !binding->parent && binding->has_explicit_type;
}

// What the target of `a = v` (or `||=`, `&&=`, `??=`) gives v as its contextual type, given the target's type (tsc's
// getContextualTypeForBinaryOperand), past the CommonJS and JavaScript container declarations check_binary() handles
// tsc's isBindableStaticNameExpression(expr, excludeThisKeyword=true): a name, or accesses of names and literals of one
static bool is_bindable_static_name(Expr const* expr) {
    if (expr_is_perenthisized(expr)) return false;
    if (expr->tag == EXPR_IDENTIFIER) return !expr->identifier.keyword;
    if (expr->tag != EXPR_PROPERTY) return false;
    if (expr->property->form == PROPERTY_EXPR_BRACKET) {
        Expr const* key = &expr->property->prop;
        if (key->tag != EXPR_LITERAL || (key->literal.tag != VALUE_STRING && key->literal.tag != VALUE_NUMBER)) return false;
    } else if (expr->property->prop.tag != EXPR_IDENTIFIER) {
        return false; // (`a.#x`)
    }
    return is_bindable_static_name(&expr->property->expr);
}

// `this.b = v`, or `x.b = v` where x isn't a dotted name (or another operator than `=`): when b is a property declared
// with a type annotation (in a class or a type literal/interface), that annotation is v's contextual type, rather than
// the type of `a.b` (tsc's getContextualTypeForAssignmentDeclaration, for AssignmentDeclarationKind.None and
// ThisProperty). Unlike the target's type, it doesn't hold the undefined of an optional property. Null otherwise
static Ty const* annotated_property_context(Checker* me, BinaryOpExpr const* binary) {
    PropertyExpr const* access = binary->left.property;
    // (`a.b = v` is AssignmentDeclarationKind.Property: the type of `a.b`)
    if (binary->op == BINARY_OP_EQUAL && is_bindable_static_name(&access->expr)) return nullptr;
    Ty const* object = ty_expr_type(me, &access->expr);
    if (object->flags & TYF_ANY) return nullptr;
    object = ty_apparent_type(me, object);
    // (A union's or intersection's property is a synthetic one, left to the target's type)
    if (!(object->flags & TYF_OBJECT) || object->kind == TY_UNION || object->kind == TY_INTERSECTION) return nullptr;
    Atom const* name = ty_literal_name(me, ty_access_key_type(me, access));
    if (!name) return nullptr;
    Prop const* prop = ty_find_prop(me, object, name).prop;
    if (!prop || (prop->flags & (PROP_METHOD | PROP_GETTER | PROP_SETTER))) return nullptr;
    Prop const* declared = prop;
    while (declared->target && !declared->mapped) declared = declared->target;
    if (declared->target || !declared->decl->node || declared->decl->parameter_property) return nullptr;
    return prop_type(me, prop);
}

static Ty const* assignment_value_context(Checker* me, BinaryOpExpr const* binary, Ty const* left) {
    Ty const* assigned_context = left->flags & TYF_ANY ? nullptr : left;
    // (Only an optional property's type differs from its annotation's: don't look further for others)
    if (assigned_context && binary->left.tag == EXPR_PROPERTY && compiler_options()->strict_null_checks &&
        ty_can_be_undefined(me, assigned_context)
    ) {
        Ty const* annotated = annotated_property_context(me, binary);
        if (annotated) assigned_context = annotated;
    }
    // (A JavaScript object literal assigned to something of type any has that contextual type, so it isn't
    // open-ended: tsc's isJSObjectLiteral in checkObjectLiteral)
    if ((left->flags & TYF_ANY) && binary->right.tag == EXPR_OBJECT && binary->right.inner_span.source &&
        binary->right.inner_span.source->is_js && binary->op == BINARY_OP_EQUAL && binary->left.tag == EXPR_IDENTIFIER
    ) {
        assigned_context = left;
    }
    // (An any target is a context too, which a `yield` records as what its generator is sent)
    if (!assigned_context && binary->right.tag == EXPR_UNARY_OP && binary->right.unary_op->op == UNARY_OP_YIELD) {
        assigned_context = left;
    }
    // `a.b = v` or `this.b = v` that declares b (an expando): v's type is what b is, so it gives v no context
    // unless b is declared with a type, or `a` is (tsc's getContextualTypeForAssignmentDeclaration)
    if (binary->op == BINARY_OP_EQUAL && binary->left.tag == EXPR_PROPERTY && binary->left.property->declares &&
        !binary->jsdoc_type && !expando_target_is_annotated(binary->left.property)
    ) {
        assigned_context = nullptr;
    }
    if (binary->op == BINARY_OP_EQUAL && binary->left.tag == EXPR_OBJECT && binary->left.assignment_pattern &&
        (left->flags & TYF_OBJECT) && left->kind == TY_ANONYMOUS && binary->right.tag != EXPR_CALL
    ) {
        // An object pattern's type is a pattern (tsc's checkObjectLiteral: `result.pattern = node`), unless a
        // spread or computed name leaves its properties open. HACK: not for a call, whose return type infers from
        // it like from any contextual type (TYF_BINDING_PATTERN also means tsc's isFromBindingPattern, which an
        // assignment pattern isn't): `({ promise: this.p } = withResolvers())`
        bool open = false;
        for_each (i, vec_object_expr_item, &binary->left.object) open |= i->tag == OBJECT_ITEM_SPREAD || i->computed_key;
        if (!open) assigned_context = ty_pattern_clone(me, left);
    }
    return assigned_context;
}

Ty const* ty_assigned_value_context(Checker* me, BinaryOpExpr const* assignment) {
    if (assignment->op == BINARY_OP_EQUAL && is_commonjs_declaration(assignment)) return commonjs_declaration_context(assignment);
    if (assignment->op == BINARY_OP_EQUAL && is_js_container_declaration(assignment)) return nullptr;
    return assignment_value_context(me, assignment, ty_expr_type(me, &assignment->left));
}

static Ty const* check_binary(Checker* me, BinaryOpExpr const* binary, ExprCtx ctx) {
    check_left_spine(me, binary, ctx);
    Ty const* left;
    Ty const* right;
    switch (binary->op) {
        case BINARY_OP_EQUAL:
            if (is_commonjs_declaration(binary)) {
                // `exports.x = v` declares x: it is what v is (no context but a JSDoc type, tsc's
                // getContextualTypeForAssignmentDeclaration). `module.exports = v` has the type of its target
                // (checkBinaryLikeExpression: isAssignmentDeclaration), `exports.x = v` v's, unless v is a container
                right = ty_check_expr(me, &binary->right, commonjs_declaration_context(binary));
                if (is_js_container_declaration(binary) || expr_is_module_exports(&binary->left)) {
                    return ty_expr_type(me, &binary->left);
                }
                ty_expr_type(me, &binary->left);
                return right;
            }
            if (is_js_container_declaration(binary)) {
                // JavaScript `ns.a = {}` whose target has expandos is the container's type (tsc's
                // checkBinaryLikeExpression: isAssignmentDeclaration returns the left type)
                ty_check_expr(me, &binary->right, nullptr);
                return ty_expr_type(me, &binary->left);
            }
            // fallthrough
        case BINARY_OP_OR_OR_EQUAL:
        case BINARY_OP_AND_AND_EQUAL:
        case BINARY_OP_EH_EH_EQUAL:
            // The target contextually types what is assigned to it (tsc's getContextualTypeForBinaryOperand)
            left = ty_expr_type(me, &binary->left);
            Ty const* assigned_context = assignment_value_context(me, binary, left);
            right = ty_check_expr(me, &binary->right, assigned_context);
            // `{} = v` is v without null and undefined, the error type if nothing is left (tsc's
            // checkObjectLiteralAssignment returns checkNonNullType's)
            if (binary->op == BINARY_OP_EQUAL && binary->left.tag == EXPR_OBJECT && !expr_is_perenthisized(&binary->left) &&
                !vec_object_expr_item_size(&binary->left.object) && compiler_options()->strict_null_checks
            ) {
                if (right->flags & TYF_UNKNOWN) return ty_intrinsic(me, TY_ERROR);
                if (ty_can_be_null(me, right) || ty_can_be_undefined(me, right)) {
                    Ty const* non_null = ty_non_nullable(me, right);
                    return non_null->flags & (TYF_NULLABLE | TYF_NEVER) ? ty_intrinsic(me, TY_ERROR) : non_null;
                }
            }
            break;
        case BINARY_OP_OR_OR:
        case BINARY_OP_EH_EH: {
            // (The operands' contextual type is the expression's as written, see passed_on_context())
            Ty const* passed = passed_on_context(ctx).contextual;
            if (binary->defaulted_expando) return ty_check_expr(me, &binary->right, passed);
            left = ty_check_expr(me, &binary->left, passed);
            right = ty_check_expr(me, &binary->right, ctx.contextual ? passed : left);
            break;
        }
        case BINARY_OP_AND_AND:
        case BINARY_OP_COMMA:
            left = ty_expr_type(me, &binary->left);
            right = ty_check_expr(me, &binary->right, passed_on_context(ctx).contextual);
            break;
        default:
            left = ty_expr_type(me, &binary->left);
            right = ty_expr_type(me, &binary->right);
            break;
    }
    return binary_result(me, left, binary->op, right);
}

// tsc's getUnaryResultType: -, ~, ++ and -- of a bigint are a bigint
static Ty const* unary_result_type(Checker* me, Ty const* operand) {
    if (!maybe_of_kind(operand, TYF_BIGINT | TYF_BIGINT_LITERAL)) return ty_intrinsic(me, TY_NUMBER);
    if ((operand->flags & (TYF_ANY | TYF_UNKNOWN)) || maybe_of_kind(operand, TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM)) {
        Ty const* parts[2] = { ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_BIGINT) };
        return ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    return ty_intrinsic(me, TY_BIGINT);
}

static Ty const* check_unary(Checker* me, UnaryOpExpr const* unary) {
    Expr const* operand = &unary->expr;
    switch (unary->op) {
        case UNARY_OP_LEFT_BANG: {
            Ty const* type = ty_expr_type(me, operand);
            bool truthy = ty_can_be_truthy(me, type);
            bool falsy = ty_can_be_falsy(me, type);
            if (truthy != falsy) return ty_fresh_literal(me, ty_intrinsic(me, truthy ? TY_FALSE : TY_TRUE));
            return ty_intrinsic(me, TY_BOOLEAN);
        }
        case UNARY_OP_TYPEOF: {
            ty_expr_type(me, operand); // Evaluated like any operand: a circular reference through it is one
            return my typeof_type;
        }
        case UNARY_OP_VOID: return ty_intrinsic(me, TY_UNDEFINED_WIDENING);
        case UNARY_OP_TILDE: return unary_result_type(me, ty_expr_type(me, operand));
        case UNARY_OP_DELETE: return ty_intrinsic(me, TY_BOOLEAN);
        case UNARY_OP_AWAIT: return ty_awaited(me, ty_expr_type(me, operand));

        case UNARY_OP_YIELD:
        case UNARY_OP_YIELD_STAR:
            impossible; // check_yield()

        case UNARY_OP_PLUS:
        case UNARY_OP_MINUS:
            // (tsc's checkPrefixUnaryExpression checks the operand first: `-1` makes the literal 1 before -1)
            if (!expr_is_perenthisized(operand) && operand->tag == EXPR_LITERAL) ty_expr_type(me, operand);
            if (!expr_is_perenthisized(operand) && operand->tag == EXPR_LITERAL && operand->literal.tag == VALUE_NUMBER) {
                Value value = value_number(unary->op == UNARY_OP_PLUS ? operand->literal.number : -operand->literal.number);
                return ty_fresh_literal(me, ty_literal(me, &value));
            }
            if (unary->op == UNARY_OP_MINUS && !expr_is_perenthisized(operand) && operand->tag == EXPR_LITERAL &&
                operand->literal.tag == VALUE_BIGINT
            ) {
                Value value = value_bigint_negated(&operand->literal); // (bigints have no -0: printed and keyed as 0)
                return ty_fresh_literal(me, ty_literal(me, &value));
            }
            if (unary->op == UNARY_OP_PLUS) return ty_intrinsic(me, TY_NUMBER);
            return unary_result_type(me, ty_expr_type(me, operand));

        case UNARY_OP_LEFT_PLUS_PLUS:
        case UNARY_OP_LEFT_MINUS_MINUS:
        case UNARY_OP_RIGHT_PLUS_PLUS:
        case UNARY_OP_RIGHT_MINUS_MINUS:
            return unary_result_type(me, ty_expr_type(me, operand));

        case UNARY_OP_RIGHT_BANG: {
            // (tsc's getNonNullableType: without strictNullChecks nothing is nullable, `null!` is null)
            Ty const* type = ty_expr_type(me, operand);
            if (!compiler_options()->strict_null_checks) return type;
            // Part of an optional chain (`a?.b!.c`): the chain may still have ended, tsc's checkNonNullChain keeps its
            // marker
            bool optional = false;
            type = ty_optional_expression_type(me, type, false, &optional);
            return propagate_optional(me, ty_non_nullable(me, type), unary->continues_chain, optional);
        }
    }
    impossible;
}

// Object literals

Atom const* ty_property_name_of_key(Checker* me, Ty const* key, bool* is_number) {
    if (key->kind != TY_LITERAL || (key->flags & TYF_ENUM)) return nullptr;
    if (key->flags & (TYF_STRING_LITERAL | TYF_UNIQUE_ES_SYMBOL)) return key->literal.string;
    if (!(key->flags & TYF_NUMBER_LITERAL)) return nullptr;
    cstr rendered = cstr_null;
    cstr_append_value_js(&rendered, &key->literal.value);
    Atom const* name = checker_atom(me, cstr_sv(&rendered));
    cstr_drop(&rendered);
    *is_number = true;
    return name;
}

typedef struct LiteralBuilder {
    ObjectLiteralProp* props;
    u32 count, cap;
    IndexInfo index[3];
    u32 index_count;
    bool empty_object; // Spreads: nothing, tsc's emptyObjectType, rather than an object literal without properties
    // Spreads: the names of private and protected properties the spread object has, which remove what came before
    // (tsc's getSpreadType: skippedPrivateMembers)
    Atom const** skipped;
    u32 skipped_count;
} LiteralBuilder;

static void literal_builder_skip(LiteralBuilder* builder, Atom const* name) {
    builder->skipped = realloc(builder->skipped, sizeof(Atom const*) * (builder->skipped_count + 1));
    panic_if(!builder->skipped, "out of memory");
    builder->skipped[builder->skipped_count++] = name;
}

static bool literal_builder_skips(LiteralBuilder const* builder, Atom const* name) {
    for (u32 i = 0; i < builder->skipped_count; i++) {
        if (builder->skipped[i] == name) return true;
    }
    return false;
}

static ObjectLiteralProp* literal_builder_find(LiteralBuilder* builder, Atom const* name) {
    for (u32 i = 0; i < builder->count; i++) {
        if (builder->props[i].name == name) return &builder->props[i];
    }
    return nullptr;
}

static ObjectLiteralProp* literal_builder_add(LiteralBuilder* builder, ObjectLiteralProp prop) {
    // A later property of the same name replaces the earlier one in place
    ObjectLiteralProp* existing = literal_builder_find(builder, prop.name);
    if (existing) {
        *existing = prop;
        return existing;
    }
    if (builder->count == builder->cap) {
        builder->cap = builder->cap ? builder->cap * 2 : 8;
        builder->props = realloc(builder->props, sizeof(ObjectLiteralProp) * builder->cap);
        panic_if(!builder->props, "out of memory");
    }
    builder->props[builder->count] = prop;
    return &builder->props[builder->count++];
}

static void literal_builder_drop(LiteralBuilder* builder) {
    free(builder->props);
    free(builder->skipped);
    *builder = (LiteralBuilder){0};
}

static LiteralBuilder literal_builder_clone(LiteralBuilder const* builder) {
    LiteralBuilder ret = *builder;
    ret.skipped = nullptr;
    ret.skipped_count = 0;
    ret.cap = builder->count;
    ret.props = builder->count ? malloc(sizeof(ObjectLiteralProp) * builder->count) : nullptr;
    panic_if(builder->count && !ret.props, "out of memory");
    if (builder->count) memcpy(ret.props, builder->props, sizeof(ObjectLiteralProp) * builder->count);
    return ret;
}

// Spreads (tsc's getSpreadType): what `{ ...a, x: 1, ...b }` is made of, in the order tsc makes it. Spreading `right`
// into `left` gives right's properties (in its order), then those of left that right doesn't have; an optional
// property of right adds to left's of that name rather than replacing it. The properties written between spreads are
// spread into what came before like that too. A union is spread member by member (the result is a union), a generic
// object stays itself (the result is an intersection with it)

// tsc's isSpreadableProperty (and the modifier check of getSpreadType/getRestType): not private, protected or `#x`, nor
// a method or accessor declared in a class
bool ty_is_spreadable_prop(Checker* me, Prop const* prop) {
    if (prop->flags & (PROP_PRIVATE | PROP_PROTECTED)) return false;
    if (ty_private_name_class(me, prop->name)) return false;
    Prop const* origin = prop;
    while (origin->target) origin = origin->target;
    u32 kinds = PROP_METHOD | PROP_ACCESSOR | PROP_GETTER | PROP_SETTER;
    return !((prop->flags | origin->flags) & kinds) || !origin->parent || !(origin->parent->flags & SYMBOL_CLASS);
}

// The properties of an object type as a spread takes them (tsc's isSpreadableProperty and getSpreadSymbol): not
// private or protected ones, nor methods of classes, and none readonly. `partial`: each one optional (tsc's
// getAnonymousPartialType). `readonly`: in a const context, every one readonly, methods too as properties
static void spread_props_of(Checker* me, Ty const* object, bool partial, bool readonly, LiteralBuilder* out) {
    if (object->kind == TY_INTERSECTION) {
        // The properties of all of its members, each of the type the intersection has for it: every member's
        // properties are spread, then made what the intersection says (optional only if optional everywhere)
        LiteralBuilder all = {0};
        for (u32 i = 0; i < object->composite.types.len; i++) {
            Ty const* member = ty_apparent_type(me, object->composite.types.data[i]);
            if (!(member->flags & (TYF_OBJECT | TYF_INTERSECTION))) continue;
            LiteralBuilder part = {0};
            spread_props_of(me, member, partial, readonly, &part);
            for (u32 j = 0; j < part.count; j++) {
                ObjectLiteralProp* existing = literal_builder_find(&all, part.props[j].name);
                if (!existing) {
                    literal_builder_add(&all, part.props[j]);
                } else if (!(part.props[j].flags & PROP_OPTIONAL)) {
                    existing->flags &= ~(u32)PROP_OPTIONAL;
                    existing->parent = nullptr;
                }
            }
            for (u32 j = 0; j < part.index_count && all.index_count < c_arraylen(all.index); j++) all.index[all.index_count++] = part.index[j];
            for (u32 j = 0; j < part.skipped_count; j++) literal_builder_skip(out, part.skipped[j]);
            literal_builder_drop(&part);
        }
        for (u32 j = 0; j < all.count; j++) {
            all.props[j].parent = nullptr; // (A property of the intersection: a symbol of its own)
            Ty const* type = ty_access(me, object, ty_prop_name_literal(me, all.props[j].name));
            // (Under exactOptionalPropertyTypes the optional property keeps its declared type: the read's missing goes)
            bool exact = compiler_options()->exact_optional_property_types && compiler_options()->strict_null_checks;
            if (type && exact && (all.props[j].flags & PROP_OPTIONAL)) {
                all.props[j].type = ty_remove_missing(me, type, true);
                literal_builder_add(out, all.props[j]);
                continue;
            }
            if (type && (all.props[j].flags & PROP_OPTIONAL) && !partial) type = ty_without_undefined(me, type);
            if (type) {
                all.props[j].type = (all.props[j].flags & PROP_OPTIONAL) && compiler_options()->strict_null_checks ?
                    optional_type(me, type) : type;
                // (`asserted` stays: the types baseline reuses it where it still says the same, like tsc does the
                // annotation of the intersection property's first declaration)
            }
            literal_builder_add(out, all.props[j]);
        }
        for (u32 j = 0; j < all.index_count && out->index_count < c_arraylen(out->index); j++) out->index[out->index_count++] = all.index[j];
        literal_builder_drop(&all);
        return;
    }
    Members const* members = ty_members(me, object);
    for (u32 j = 0; j < members->prop_count; j++) {
        Prop const* prop = members->props[j];
        if (prop->flags & (PROP_PRIVATE | PROP_PROTECTED)) literal_builder_skip(out, prop->name);
        if (!ty_is_spreadable_prop(me, prop)) continue;
        if ((prop->flags & PROP_METHOD) && prop->decl->function) continue; // (a class method)
        Prop const* origin = prop;
        while (origin->target) origin = origin->target;
        // (A set-only accessor spreads as undefined: tsc's getSpreadSymbol)
        bool setonly = ((prop->flags | origin->flags) & PROP_SETTER) && !((prop->flags | origin->flags) & PROP_GETTER);
        Ty const* type = setonly ? ty_intrinsic(me, TY_UNDEFINED) : prop_type(me, prop);
        u32 flags = prop->flags & (u32)~(PROP_READONLY | (setonly ? PROP_SETTER : 0));
        Ty const* declared_type = type;
        if (readonly) flags = (flags & (u32)~PROP_METHOD) | PROP_READONLY; // (tsc's getSpreadSymbol makes a property)
        if (partial || setonly || readonly != !!(prop->flags & PROP_READONLY)) flags |= PROP_NO_VALUE_DECLARATION;
        if (partial && !(flags & PROP_OPTIONAL)) {
            flags |= PROP_OPTIONAL;
            if (compiler_options()->strict_null_checks && !compiler_options()->exact_optional_property_types) type = optional_type(me, type);
        }
        // The annotation of the property it comes from still says what it is, unless instantiated (or made optional:
        // an optional one of a partial spread, `{ ...(o as Opts | undefined) }`, still is what it says)
        TypeNode const* annotation = origin->decl->node && origin->decl->node->parsed && !origin->decl->lazy && !origin->decl->function &&
            !origin->decl->overload_count && !origin->decl->symbol && declared_type == prop_type(me, origin) ? origin->decl->node : nullptr;
        TypeNode const* annotation_required = nullptr;
        if (annotation && type != declared_type) {
            annotation_required = annotation;
            annotation = nullptr;
        }
        literal_builder_add(out, (ObjectLiteralProp){
            .name = prop->name,
            .flags = flags,
            .type = type,
            .asserted = annotation,
            .asserted_required = annotation_required,
            .name_span = origin->decl->name_span,
            .name_form = ty_prop_name_form_of(prop),
            .name_type = prop->decl->name_type,
            .decl_spans = origin->decl->decl_spans,
            .decl_span_count = origin->decl->decl_span_count,
            .name_is_value = origin->decl->name_is_value,
            .parent = !partial && !setonly && !(prop->flags & PROP_READONLY) ? prop->parent : nullptr,
        });
    }
    // (Not readonly outside a const context, like the properties: tsc's getIndexInfoWithReadonly in getSpreadType)
    for (u32 j = 0; j < members->index_count && out->index_count < c_arraylen(out->index); j++) {
        out->index[out->index_count] = members->index[j];
        out->index[out->index_count++].readonly = readonly;
    }
}

// tsc's getSpreadType of two object types, `left` (tsc's emptyObjectType if `left_empty`) becoming the result
static void spread_into(Checker* me, LiteralBuilder* left, LiteralBuilder const* right) {
    LiteralBuilder result = {0};
    for (u32 i = 0; i < right->count; i++) {
        ObjectLiteralProp const* r = &right->props[i];
        ObjectLiteralProp* l = literal_builder_find(left, r->name);
        if (!l || !(r->flags & PROP_OPTIONAL)) {
            literal_builder_add(&result, *r);
            continue;
        }
        // Optional: what was there stays possible, and so does its being required
        ObjectLiteralProp merged = *l;
        merged.parent = nullptr;
        merged.flags = (l->flags & (PROP_OPTIONAL | PROP_METHOD)) | PROP_NO_VALUE_DECLARATION; // (tsc: a fresh symbol)
        merged.own = false;
        // (Under exactOptionalPropertyTypes only the missing goes, which the declared type doesn't have: tsc's
        // removeMissingOrUndefinedType)
        bool exact = compiler_options()->exact_optional_property_types;
        Ty const* right_type = exact ? r->type : ty_without_undefined(me, r->type);
        if (l->type && (exact ? l->type : ty_without_undefined(me, l->type)) != right_type) {
            Ty const* both[2] = { l->type, right_type };
            merged.type = ty_union_subtype_reduced(me, both, 2);
        }
        // (tsc's merged symbol has both declarations: the types baseline reuses the first annotation that still says
        // the same)
        if (!merged.asserted) merged.asserted = r->asserted ? r->asserted : r->asserted_required;
        merged.asserted_required = nullptr;
        u32 count = l->decl_span_count + r->decl_span_count;
        Span* spans = arena_new_n(&my arena, Span, count ? count : 1);
        for (u32 k = 0; k < l->decl_span_count; k++) spans[k] = l->decl_spans[k];
        for (u32 k = 0; k < r->decl_span_count; k++) spans[l->decl_span_count + k] = r->decl_spans[k];
        merged.decl_spans = spans;
        merged.decl_span_count = count;
        literal_builder_add(&result, merged);
    }
    for (u32 i = 0; i < left->count; i++) {
        if (literal_builder_skips(right, left->props[i].name)) continue;
        if (!literal_builder_find((LiteralBuilder*)right, left->props[i].name)) literal_builder_add(&result, left->props[i]);
    }
    // Index signatures: right's into nothing, else the keys both have (tsc's getUnionIndexInfos)
    if (left->empty_object) {
        result.index_count = right->index_count;
        for (u32 i = 0; i < right->index_count; i++) result.index[i] = right->index[i];
    } else {
        for (u32 i = 0; i < left->index_count; i++) {
            for (u32 j = 0; j < right->index_count; j++) {
                if (left->index[i].key != right->index[j].key) continue;
                // (A new one, without a declaration)
                IndexInfo info = { .key = left->index[i].key, .param_name = checker_atom(me, c_sv("x")) };
                info.value = union_of_two(me, left->index[i].value, right->index[j].value);
                info.readonly = left->index[i].readonly && right->index[j].readonly;
                result.index[result.index_count++] = info;
            }
        }
    }
    free(left->props);
    free(left->skipped);
    *left = result;
}

// A member of the intersection a spread of generics makes (tsc's getSpreadType keeps `{ a } & T & { b }` in order,
// merging only neighbouring non-generic objects): a generic object, or objects spread since the last one
typedef struct SpreadPart {
    Ty const* generic;
    LiteralBuilder* alts;
    u32 alt_count;
} SpreadPart;

typedef struct SpreadAcc {
    LiteralBuilder* alts; // What was spread since the last generic (tsc's `spread`): one object per member of a union
    u32 alt_count;
    SpreadPart* parts; // Finished members, when a generic object was spread
    u32 part_count;
    bool any; // Something `any` was spread: the whole literal is any
    bool readonly; // In a const context: what's spread is readonly
    // The last thing spread was no object (TS2698): the literal is errorType (tsc's checkObjectLiteral; a spread after
    // it makes any of it, getSpreadType)
    bool error;
} SpreadAcc;

static void spread_acc_push_part(SpreadAcc* acc, SpreadPart part) {
    acc->parts = realloc(acc->parts, sizeof(SpreadPart) * (acc->part_count + 1));
    panic_if(!acc->parts, "out of memory");
    acc->parts[acc->part_count++] = part;
}

// The objects spread so far become a member of the intersection (nothing if they're empty: tsc's getSpreadType
// returns the generic right side for an empty left)
static void spread_acc_flush(SpreadAcc* acc) {
    if (!acc->alt_count) return;
    if (acc->alt_count == 1 && !acc->alts[0].count && !acc->alts[0].index_count) {
        literal_builder_drop(&acc->alts[0]);
        free(acc->alts);
    } else {
        spread_acc_push_part(acc, (SpreadPart){ .alts = acc->alts, .alt_count = acc->alt_count });
    }
    acc->alts = nullptr;
    acc->alt_count = 0;
}

static void spread_acc_push(SpreadAcc* acc, LiteralBuilder alt) {
    acc->alts = realloc(acc->alts, sizeof(LiteralBuilder) * (acc->alt_count + 1));
    panic_if(!acc->alts, "out of memory");
    acc->alts[acc->alt_count++] = alt;
}

// Properties spread into what was spread so far (the ones written since the last spread, an object spread)
static void spread_acc_props(Checker* me, SpreadAcc* acc, LiteralBuilder const* right) {
    if (!acc->alt_count) spread_acc_push(acc, (LiteralBuilder){ .empty_object = true });
    for (u32 i = 0; i < acc->alt_count; i++) spread_into(me, &acc->alts[i], right);
}

// Empty for tsc's spread purposes (isEmptyObjectTypeOrSpreadsIntoEmptyObject): no properties, or a primitive
static bool spreads_into_nothing(Checker* me, Ty const* ty) {
    if (ty->flags & (TYF_NULLABLE | TYF_BOOLEAN | TYF_BOOLEAN_LITERAL | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT |
        TYF_BIGINT_LITERAL | TYF_STRING_LIKE | TYF_ENUM | TYF_NON_PRIMITIVE | TYF_VOID)) {
        return true;
    }
    if (ty->kind == TY_KEYOF) return true;
    if (!(ty->flags & TYF_OBJECT) || (ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED))) return false;
    Members const* members = ty_members(me, ty);
    return !members->prop_count && !members->index_count && !members->call_count && !members->construct_count;
}

// Something spread: `...x`
static void spread_acc_type(Checker* me, SpreadAcc* acc, Ty const* type) {
    acc->error = false;
    if (type->flags & TYF_ANY) {
        acc->any = true;
        return;
    }
    TyList members = type->kind == TY_UNION && !(type->flags & TYF_BOOLEAN) ? type->composite.types : (TyList){ .data = &type, .len = 1 };
    // A union of one object with things that spread into nothing is that object, every property optional (tsc's
    // tryMergeUnionOfObjectTypeAndEmptyObject)
    Ty const* partial = nullptr;
    if (members.len > 1) {
        u32 objects = 0;
        for (u32 i = 0; i < members.len; i++) {
            if (!spreads_into_nothing(me, members.data[i])) {
                objects++;
                partial = members.data[i];
            }
        }
        if (objects == 0) return;
        if (objects != 1) partial = nullptr;
    }
    if (partial || members.len == 1) {
        Ty const* object = partial ? partial : members.data[0];
        if (!partial && ty_is_generic_object(me, object)) {
            spread_acc_flush(acc);
            spread_acc_push_part(acc, (SpreadPart){ .generic = object });
            return;
        }
        Ty const* apparent = ty_apparent_type(me, object);
        if (apparent->flags & TYF_ANY) {
            acc->any = true;
            return;
        }
        if (!(apparent->flags & TYF_OBJECT) && apparent->kind != TY_INTERSECTION) return;
        // (An empty object still makes what was spread a new object rather than tsc's emptyObjectType: index signatures
        // written after it are the ones both have, none)
        if (spreads_into_nothing(me, object) && !(object->flags & TYF_OBJECT)) return;
        LiteralBuilder right = { .empty_object = false };
        spread_props_of(me, apparent, partial != nullptr, acc->readonly, &right);
        spread_acc_props(me, acc, &right);
        literal_builder_drop(&right);
        return;
    }
    // Member by member, each into each of what was spread so far
    if (!acc->alt_count) spread_acc_push(acc, (LiteralBuilder){ .empty_object = true });
    SpreadAcc next = {0};
    for (u32 i = 0; i < acc->alt_count; i++) {
        for (u32 j = 0; j < members.len; j++) {
            LiteralBuilder alt = literal_builder_clone(&acc->alts[i]);
            Ty const* apparent = ty_apparent_type(me, members.data[j]);
            if (!spreads_into_nothing(me, members.data[j]) && ((apparent->flags & TYF_OBJECT) || apparent->kind == TY_INTERSECTION)) {
                LiteralBuilder right = {0};
                spread_props_of(me, apparent, false, acc->readonly, &right);
                spread_into(me, &alt, &right);
                literal_builder_drop(&right);
            }
            spread_acc_push(&next, alt);
        }
        literal_builder_drop(&acc->alts[i]);
    }
    free(acc->alts);
    acc->alts = next.alts;
    acc->alt_count = next.alt_count;
}

static Ty const* spread_acc_objects(Checker* me, LiteralBuilder const* alts, u32 alt_count, ObjectLiteralKind kind) {
    TyBuf objects = {0};
    for (u32 i = 0; i < alt_count; i++) {
        LiteralBuilder const* alt = &alts[i];
        ty_buf_push(&objects, ty_object_literal(me, alt->props, alt->count, alt->index, alt->index_count, kind));
    }
    Ty const* ret = ty_union(me, objects.data, objects.len, nullptr, (TyList){0});
    ty_buf_drop(&objects);
    return ret;
}

// What the literal is. `own`: the properties written after the last spread (the whole literal if nothing was spread)
static Ty const* spread_acc_finish(Checker* me, SpreadAcc* acc, LiteralBuilder* own, ObjectLiteralKind kind, Expr const* expr) {
    Ty const* ret;
    if (acc->any) {
        ret = acc->error ? ty_intrinsic(me, TY_ERROR) : any_type(me);
    } else if (!acc->alt_count && !acc->part_count) {
        // (`{}`: a type of its own, see ty_object_literal_of())
        bool empty = !own->count && !own->index_count && expr && expr->tag == EXPR_OBJECT && !vec_object_expr_item_size(&expr->object);
        ret = ty_object_literal_of(me, own->props, own->count, own->index, own->index_count, kind, empty ? expr : nullptr);
    } else {
        if (own->count || own->index_count) spread_acc_props(me, acc, own);
        if (!acc->part_count) {
            ret = acc->alt_count ? spread_acc_objects(me, acc->alts, acc->alt_count, kind) :
                ty_object_literal(me, nullptr, 0, nullptr, 0, kind);
        } else {
            spread_acc_flush(acc);
            TyBuf members = {0};
            for (u32 i = 0; i < acc->part_count; i++) {
                SpreadPart const* part = &acc->parts[i];
                ty_buf_push(&members, part->generic ? part->generic : spread_acc_objects(me, part->alts, part->alt_count, kind));
            }
            ret = members.len == 1 ? members.data[0] : ty_intersection(me, members.data, members.len, nullptr, (TyList){0});
            ty_buf_drop(&members);
        }
    }
    for (u32 i = 0; i < acc->alt_count; i++) literal_builder_drop(&acc->alts[i]);
    free(acc->alts);
    for (u32 i = 0; i < acc->part_count; i++) {
        for (u32 j = 0; j < acc->parts[i].alt_count; j++) literal_builder_drop(&acc->parts[i].alts[j]);
        free(acc->parts[i].alts);
    }
    free(acc->parts);
    return ret;
}

static Signature const* single_call_signature(Checker* me, Ty const* ty) {
    Members const* members = ty_members(me, ty);
    if (members->call_count != 1) return nullptr;
    signature_resolve(me, members->call[0]);
    return members->call[0];
}

// tsc's isPossiblyDiscriminantValue: what can say which member of a union an object literal is meant to be
static bool is_possibly_discriminant_value(Expr const* expr) {
    switch (expr->tag) {
        case EXPR_LITERAL:
        case EXPR_IDENTIFIER:
            return true;
        case EXPR_PROPERTY:
            return is_possibly_discriminant_value(&expr->property->expr);
        case EXPR_TEMPLATE:
            return !expr->template_->tag;
        default:
            return false;
    }
}

// A property that may pick members of a union contextual type, with the type it has
typedef struct Discriminator {
    Atom const* name;
    Ty const* type;
} Discriminator;

// tsc's discriminateTypeByDiscriminableItems: the members of a union contextual type whose properties the
// discriminators fit (a discriminator that fits none excludes nothing)
static Ty const* discriminate_by_items(Checker* me, Ty const* contextual, Discriminator const* items, u32 count) {
    TyList types = contextual->composite.types;
    if (types.len > 64) return contextual;
    // 0: excluded, 1: maybe, 3: included
    unsigned char include[64];
    u32 const primitive = TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_ES_SYMBOL_LIKE | TYF_BOOLEAN | TYF_LITERAL | TYF_ENUM |
        TYF_NULLABLE | TYF_VOID;
    for (u32 i = 0; i < types.len; i++) include[i] = types.data[i]->flags & primitive ? 0 : 3;
    for (u32 d = 0; d < count; d++) {
        Ty const* value = items[d].type;
        Ty const* key = ty_prop_name_literal(me, items[d].name);
        bool matched = false;
        for (u32 i = 0; i < types.len; i++) {
            if (!include[i]) continue;
            Ty const* target = ty_access(me, types.data[i], key);
            bool related = false;
            if (target) {
                Ty const* const* parts = &value;
                u32 part_count = 1;
                if (value->kind == TY_UNION) {
                    parts = value->composite.types.data;
                    part_count = value->composite.types.len;
                }
                for (u32 j = 0; j < part_count && !related; j++) related = ty_is_related(me, parts[j], target, TY_RELATION_ASSIGNABLE);
            }
            if (related) matched = true;
            else include[i] = 1;
        }
        for (u32 i = 0; i < types.len; i++) {
            if (include[i] == 1) include[i] = matched ? 0 : 3;
        }
    }
    Ty const* kept[64];
    u32 len = 0;
    for (u32 i = 0; i < types.len; i++) {
        if (include[i]) kept[len++] = types.data[i];
    }
    if (len == types.len || !len) return contextual;
    return ty_union_unreduced(me, kept, len); // (tsc's filterType)
}

// The optional discriminant properties of a union contextual type that a literal doesn't have are undefined in it (the
// second half of tsc's discriminateContextualTypeBy*). `has(name)`: whether the literal has one of that name. `skip`:
// a name not to count (JSX children given as children)
static void push_missing_discriminators(
    Checker* me, Ty const* contextual, bool (*has)(void const*, Checker*, Atom const*), void const* literal,
    Atom const* skip, Discriminator** items, u32* count
) {
    // (The names come from the first member, whose parts an intersection's are)
    Ty const* first = ty_apparent_type(me, contextual->composite.types.data[0]);
    TyList parts = first->kind == TY_INTERSECTION ? first->composite.types : (TyList){ .data = &first, .len = 1 };
    for (u32 p = 0; p < parts.len; p++) {
        Ty const* part = ty_apparent_type(me, parts.data[p]);
        if (!(part->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, part);
        for (u32 i = 0; i < members->prop_count; i++) {
            Atom const* name = members->props[i]->name;
            if (name == skip || has(literal, me, name)) continue;
            // A property of the union (every member has it), optional if it is in some member
            bool in_all = true, optional = false;
            for (u32 j = 0; j < contextual->composite.types.len && in_all; j++) {
                FoundProp found = ty_find_prop(me, ty_apparent_type(me, contextual->composite.types.data[j]), name);
                in_all = found.found;
                optional |= found.found && (found.flags & PROP_OPTIONAL);
            }
            if (!in_all || !optional || !ty_is_discriminant_property(me, contextual, name)) continue;
            *items = realloc(*items, sizeof(Discriminator) * (*count + 1));
            panic_if(!*items, "out of memory");
            (*items)[(*count)++] = (Discriminator){ .name = name, .type = ty_intrinsic(me, TY_UNDEFINED) };
        }
    }
}

static bool object_has_member(void const* literal, Checker* me, Atom const* name) {
    for_each (item, vec_object_expr_item, (ObjectExpr const*)literal) {
        if (item->tag == OBJECT_ITEM_SPREAD) continue;
        Atom const* own = item->computed_key ? nullptr : checker_atom(me, cstr_sv(&item->name_string));
        if (own == name) return true;
    }
    return false;
}

// tsc's discriminateContextualTypeByObjectMembers
static Ty const* discriminate_by_object_members(Checker* me, Ty const* contextual, ObjectExpr const* object) {
    Discriminator* items = nullptr;
    u32 count = 0;
    for_each (item, vec_object_expr_item, object) {
        bool candidate = item->tag == OBJECT_ITEM_SHORTHAND ||
            (item->tag == OBJECT_ITEM_PROPERTY && is_possibly_discriminant_value(&item->value));
        if (!candidate || item->computed_key) continue;
        Atom const* name = checker_atom(me, cstr_sv(&item->name_string));
        if (!ty_is_discriminant_property(me, contextual, name)) continue;
        items = realloc(items, sizeof(Discriminator) * (count + 1));
        panic_if(!items, "out of memory");
        items[count++] = (Discriminator){ .name = name, .type = ty_expr_type(me, &item->value) };
    }
    push_missing_discriminators(me, contextual, object_has_member, object, nullptr, &items, &count);
    Ty const* ret = discriminate_by_items(me, contextual, items, count);
    free(items);
    return ret;
}

static bool attributes_have(void const* literal, Checker* me, Atom const* name) {
    Expr const* attributes = literal;
    for_each (i, vec_jsx_attribute, &attributes->jsx_attributes) {
        if (i->tag != JSX_ATTRIBUTE_SPREAD && checker_atom(me, i->name.sv) == name) return true;
    }
    return false;
}

// tsc's discriminateContextualTypeByJSXAttributes: an attribute without a value is `true`
static Ty const* discriminate_by_jsx_attributes(Checker* me, Ty const* contextual, Expr const* attributes) {
    Discriminator* items = nullptr;
    u32 count = 0;
    for_each (i, vec_jsx_attribute, &attributes->jsx_attributes) {
        if (i->tag == JSX_ATTRIBUTE_SPREAD) continue;
        bool has_value = i->tag != JSX_ATTRIBUTE_NO_VALUE && i->tag != JSX_ATTRIBUTE_EMPTY_EXPR;
        if (has_value && !is_possibly_discriminant_value(&i->value)) continue;
        Atom const* name = checker_atom(me, i->name.sv);
        if (!ty_is_discriminant_property(me, contextual, name)) continue;
        items = realloc(items, sizeof(Discriminator) * (count + 1));
        panic_if(!items, "out of memory");
        items[count++] = (Discriminator){ .name = name, .type = has_value ? ty_expr_type(me, &i->value) : ty_intrinsic(me, TY_TRUE) };
    }
    JsxElement const* element = jsx_attributes_element(attributes);
    Atom const* children = ty_jsx_children_prop_name(me, element);
    u32 child_count = 0;
    for_each (i, vec_jsx_child, &element->children) child_count += ty_jsx_child_is_semantic(i);
    push_missing_discriminators(me, contextual, attributes_have, attributes, child_count ? children : nullptr, &items, &count);
    Ty const* ret = discriminate_by_items(me, contextual, items, count);
    free(items);
    return ret;
}


// tsc's propertiesArray since the last spread, for the index signatures computed names make
// (getObjectLiteralIndexInfo): every member's type and what its name is
typedef struct LiteralMember {
    Ty const* type; // Null for an accessor: typed from it when needed
    FunctionImpl const* accessor;
    bool getter;
    bool numeric_name, symbol_name; // (A computed name of type `any` is both)
    Expr const* computed_key;
} LiteralMember;
typedef struct LiteralMembers {
    LiteralMember* items;
    u32 count, cap;
    bool computed[3]; // A computed name made an index signature of this key: string, number, symbol
} LiteralMembers;

static IndexInfo literal_members_index(Checker* me, LiteralMembers const* members, TyIntrinsic key, bool readonly) {
    Ty const** types = malloc(sizeof(Ty const*) * (members->count ? members->count : 1));
    IndexComponent* components = arena_new_n(&my arena, IndexComponent, members->count ? members->count : 1);
    u32 n = 0, component_count = 0;
    for (u32 i = 0; i < members->count; i++) {
        LiteralMember const* m = &members->items[i];
        bool included = key == TY_STRING ? !m->symbol_name : key == TY_NUMBER ? m->numeric_name : m->symbol_name;
        if (!included) continue;
        types[n] = m->type ? m->type : ty_object_literal_accessor_type(me, m->accessor, m->getter);
        if (m->computed_key) components[component_count++] = (IndexComponent){ .name = m->computed_key, .type = types[n] };
        n++;
    }
    IndexInfo ret = {
        .key = ty_intrinsic(me, key),
        .value = n ? ty_union_subtype_reduced(me, types, n) : ty_intrinsic(me, TY_UNDEFINED),
        .param_name = checker_atom(me, c_sv("x")),
        .readonly = readonly,
        .components = components,
        .component_count = component_count,
    };
    free(types);
    return ret;
}

// The index signatures of the members since the last spread, into the builder; then starts over
static void literal_members_flush(Checker* me, LiteralMembers* members, IndexInfo* index, u32* index_count, bool readonly) {
    static TyIntrinsic const keys[3] = { TY_STRING, TY_NUMBER, TY_ES_SYMBOL };
    for (u32 key = 0; key < 3; key++) {
        if (members->computed[key]) index[(*index_count)++] = literal_members_index(me, members, keys[key], readonly);
    }
    members->count = 0;
    members->computed[0] = members->computed[1] = members->computed[2] = false;
}

static void literal_members_push(LiteralMembers* members, LiteralMember member) {
    if (members->count == members->cap) {
        members->cap = members->cap ? members->cap * 2 : 8;
        members->items = realloc(members->items, sizeof(LiteralMember) * members->cap);
    }
    members->items[members->count++] = member;
}

// tsc's getApparentTypeOfContextualType for an object literal: type variables (of a union too) as their constraints,
// which the literal's members then discriminate
// (tsc's getApparentTypeOfContextualType maps getApparentType over a union: a primitive member is its wrapper, whose
// properties say something about the literal's, `string | { normalize?: F }` both String's normalize and F)
static bool is_wrapped_primitive(Ty const* ty) {
    return (ty->flags & (TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT | TYF_BIGINT_LITERAL | TYF_BOOLEAN |
        TYF_BOOLEAN_LITERAL | TYF_ES_SYMBOL_LIKE)) && ty->kind != TY_UNION;
}

static Ty const* apparent_literal_context(Checker* me, Ty const* contextual) {
    if (is_contextual_type_variable(contextual)) {
        Ty const* apparent = ty_apparent_type(me, contextual);
        return apparent->kind == TY_INTRINSIC && (apparent->flags & TYF_UNKNOWN) ? contextual : apparent;
    }
    if (contextual->kind != TY_UNION || (contextual->flags & TYF_BOOLEAN)) return contextual;
    bool primitive = false;
    for (u32 i = 0; i < contextual->composite.types.len && !primitive; i++) {
        primitive = is_wrapped_primitive(contextual->composite.types.data[i]);
    }
    if (!primitive && !(contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return contextual;
    TyBuf parts = {0};
    bool changed = false;
    for (u32 i = 0; i < contextual->composite.types.len; i++) {
        Ty const* part = contextual->composite.types.data[i];
        Ty const* apparent = is_wrapped_primitive(part) ? ty_apparent_type(me, part) : apparent_literal_context(me, part);
        changed |= apparent != part;
        ty_buf_push(&parts, apparent);
    }
    // (Without reductions, mapType's noReductions: an `any` member doesn't absorb the others' properties)
    Ty const* ret = changed ? ty_union_unreduced(me, parts.data, parts.len) : contextual;
    ty_buf_drop(&parts);
    return ret;
}

static Ty const* check_object(Checker* me, Expr const* expr, ExprCtx ctx) {
    ObjectExpr const* object = &expr->object;
    Ty const* written_context = ctx.contextual;
    Ty const* written_sig_context = ctx.sig_contextual;
    if (ctx.contextual) ctx.contextual = apparent_literal_context(me, ctx.contextual);
    if (ctx.contextual && ctx.contextual->kind == TY_UNION) {
        ctx.contextual = discriminate_by_object_members(me, ctx.contextual, object);
    }
    if (ctx.sig_contextual) ctx.sig_contextual = apparent_literal_context(me, ctx.sig_contextual);
    if (ctx.sig_contextual && ctx.sig_contextual->kind == TY_UNION) {
        ctx.sig_contextual = discriminate_by_object_members(me, ctx.sig_contextual, object);
    }
    if (ctx.contextual && !ctx.skip_sensitive) {
        // What `this` in its methods goes by (ty_contextual_this_type()). Not while skipping context-sensitive
        // functions: none that would ask is checked
        u64 context_key[2] = { KEY_LITERAL_CONTEXT, (u64)(uintptr_t)expr };
        *intern_slot(me, &my links, context_key, 2) = ctx.contextual;
    }
    for_each (i, vec_object_expr_item, object) {
        if (i->tag == OBJECT_ITEM_PROPERTY && i->value.tag == EXPR_OBJECT) {
            u64 parent_key[2] = { KEY_LITERAL_PARENT, (u64)(uintptr_t)&i->value };
            *intern_slot(me, &my links, parent_key, 2) = expr;
        }
    }
    LiteralBuilder builder = {0}; // What was written since the last spread (tsc's propertiesTable)
    SpreadAcc spread = { .readonly = ctx.is_const };
    // The implied type of an object binding pattern (`const { a = 1 } = { a: 2 }`): a property is optional where the
    // pattern's is (tsc's checkObjectLiteral, contextualTypeHasPattern)
    Ty const* pattern_context = ctx.contextual && (ctx.contextual->flags & TYF_BINDING_PATTERN) &&
        (ctx.contextual->flags & TYF_OBJECT) && !ty_is_tuple(ctx.contextual) && !pattern_type_has_rest(me, ctx.contextual) &&
        !intern_get(&my links, (u64[]){ KEY_PATTERN_COMPUTED, ctx.contextual->id }, 2) ? ctx.contextual : nullptr;
    // Accessor pairs share a property: the declarations of both halves
    Span* accessor_spans = nullptr;

    LiteralMembers members = {0};
    for_each (i, vec_object_expr_item, object) {
        if (i->tag == OBJECT_ITEM_SPREAD) {
            literal_members_flush(me, &members, builder.index, &builder.index_count, ctx.is_const);
            if (builder.count || builder.index_count) spread_acc_props(me, &spread, &builder);
            literal_builder_drop(&builder);
            // (Its contextual type is the literal's as given: tsc's getContextualType of a SpreadAssignment)
            Ty const* spread_type = check_with(me, &i->value, (ExprCtx){
                .contextual = written_context, .sig_contextual = written_sig_context, .sig_absent = ctx.sig_absent,
                .skip_sensitive = ctx.skip_sensitive,
            });
            // (A spread of no object is TS2698 and makes the literal errorType, tsc's checkObjectLiteral)
            if (!ty_is_valid_spread_type(me, spread_type)) spread.any = spread.error = true;
            else spread_acc_type(me, &spread, spread_type);
            continue;
        }

        // The name
        Atom const* name = nullptr;
        bool name_is_value = i->name_is_value;
        Ty const* key_type = nullptr;
        if (i->computed_key) {
            key_type = ty_expr_type(me, i->computed_key);
            bool is_number = false;
            name = ty_property_name_of_key(me, key_type, &is_number);
            if (name) name_is_value = is_number;
        } else {
            name = checker_atom(me, cstr_sv(&i->name_string));
        }

        // The type
        Ty const* member_context = nullptr;
        bool is_accessor = i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER;
        // (An accessor has none: asking for it would type the contextual type's same-named accessor, which is this
        // one again when inference rechecks the literal against what it inferred from it, `get sub() { return f(X) }`
        // in X's initializer)
        Ty const* member_sig_context = nullptr;
        if ((ctx.contextual || ctx.sig_contextual) && !is_accessor) {
            if (name) {
                if (ctx.contextual) member_context = contextual_named_prop_type(me, ctx.contextual, name);
                if (ctx.sig_contextual) member_sig_context = contextual_named_prop_type(me, ctx.sig_contextual, name);
            } else if (key_type) {
                if (ctx.contextual) member_context = contextual_prop_type(me, ctx.contextual, key_type);
                if (ctx.sig_contextual) member_sig_context = contextual_prop_type(me, ctx.sig_contextual, key_type);
            }
        }
        ExprCtx member_ctx = {
            .contextual = member_context, .sig_contextual = member_sig_context,
            .sig_absent = ctx.sig_absent || (ctx.sig_contextual && !member_sig_context),
            .is_const = ctx.is_const, .skip_sensitive = ctx.skip_sensitive,
            .generic_mode = ctx.generic_mode,
        };
        Ty const* type = nullptr;
        switch (i->tag) {
            case OBJECT_ITEM_GETTER:
            case OBJECT_ITEM_SETTER:
                // Typed when asked (PROP_GETTER/PROP_SETTER), unless it's an index signature's
                if (!name) type = ty_object_literal_accessor_type(me, i->value.function, i->tag == OBJECT_ITEM_GETTER);
                break;
            case OBJECT_ITEM_METHOD:
                if (i->jsdoc_type) {
                    member_ctx.contextual = ty_of_node(i->jsdoc_type);
                    check_with(me, &i->value, member_ctx);
                    type = member_ctx.contextual;
                    break;
                }
                type = check_with(me, &i->value, member_ctx);
                break;
            default:
                if (i->jsdoc_type) {
                    // The declared type, which the value is checked against (expr_typecheck())
                    Ty const* declared = ty_of_node(i->jsdoc_type);
                    member_ctx.contextual = declared;
                    check_for_mutable_location(me, &i->value, member_ctx);
                    type = declared;
                    break;
                }
                // `{ x = 1 }` outside a destructuring target (TS1312) is typed by its default (tsc's checkObjectLiteral)
                if (i->tag == OBJECT_ITEM_SHORTHAND && i->shorthand_default && !expr->assignment_pattern) {
                    type = check_for_mutable_location(me, i->shorthand_default, member_ctx);
                    break;
                }
                type = check_for_mutable_location(me, &i->value, member_ctx);
                break;
        }

        // (tsc's checkObjectLiteral: a context-sensitive property or method is an intra-expression inference site of the
        // call inferring now, against its contextual type as the literal gives it)
        if ((i->tag == OBJECT_ITEM_PROPERTY || i->tag == OBJECT_ITEM_METHOD) && member_context && !ctx.skip_sensitive &&
            ty_expr_is_context_sensitive(&i->value)
        ) {
            ty_add_intra_expression_site(me, type, member_context);
        }

        if (object_item_is_private_name(i)) continue;

        // What the member adds to index signatures (tsc's isSymbolWithNumericName, isSymbolWithSymbolName: the
        // computed name's type is assignable to number or symbol)
        bool numeric_name = name ? ty_is_numeric_name(name) : (key_type->flags & (TYF_NUMBER | TYF_NUMBER_LITERAL)) ||
            ty_is_related(me, key_type, ty_intrinsic(me, TY_NUMBER), TY_RELATION_ASSIGNABLE);
        bool symbol_name = key_type && ((key_type->flags & TYF_ES_SYMBOL_LIKE) ||
            (!name && ty_is_related(me, key_type, ty_intrinsic(me, TY_ES_SYMBOL), TY_RELATION_ASSIGNABLE)));
        literal_members_push(&members, (LiteralMember){
            .type = type, .accessor = is_accessor ? i->value.function : nullptr, .getter = i->tag == OBJECT_ITEM_GETTER,
            .numeric_name = numeric_name, .symbol_name = symbol_name, .computed_key = i->computed_key,
        });

        if (!name) {
            // A key that's only known to be some string, number or symbol makes an index signature (the one it's
            // assignable to: `any` is a number), of all the members since the last spread. Others make nothing
            Ty const* string_number_symbol = union_of_two(me, union_of_two(me, ty_intrinsic(me, TY_STRING),
                ty_intrinsic(me, TY_NUMBER)), ty_intrinsic(me, TY_ES_SYMBOL));
            if (ty_is_related(me, key_type, string_number_symbol, TY_RELATION_ASSIGNABLE)) {
                members.computed[numeric_name ? 1 : symbol_name ? 2 : 0] = true;
            }
            continue;
        }

        ObjectLiteralProp* existing = literal_builder_find(&builder, name);
        if (existing && is_accessor && existing->decl_span_count == 1 && existing->accessor) {
            // The other half of an accessor pair. The getter decides the type
            Span* both = arena_new_n(&my arena, Span, 2);
            both[0] = existing->decl_spans[0];
            both[1] = i->span;
            existing->decl_spans = both;
            existing->decl_span_count = 2;
            existing->name_form = ty_prop_name_form_merge(existing->name_form,
                ty_prop_name_form(i->name_span.sv, i->computed_key, key_type, true));
            if (i->tag == OBJECT_ITEM_GETTER) {
                existing->setter = existing->accessor;
                existing->accessor = i->value.function;
                existing->flags = (existing->flags & ~(u32)PROP_SETTER) | PROP_GETTER;
            } else {
                existing->setter = i->value.function;
                if (!ctx.is_const) existing->flags &= ~(u32)PROP_READONLY; // Not getter-only after all
            }
            continue;
        }
        TypeNode const* asserted = i->jsdoc_type;
        // (Only an assertion that is the initializer itself, not parenthesized: tsc's typeFromPropertyAssignment)
        if (!asserted && i->tag == OBJECT_ITEM_PROPERTY && i->value.tag == EXPR_AS_TYPE && !i->value.as_type->is_satisfies &&
            !expr_is_perenthisized(&i->value) &&
            !as_type_is_const(i->value.as_type) && type == ty_expr_type(me, &i->value)
        ) {
            asserted = &i->value.as_type->type;
        }
        uint8_t name_form = ty_prop_name_form(i->name_span.sv, i->computed_key, key_type, true);
        // (A method or accessor after another kind of member is a symbol of its own to tsc's binder, whose excludes
        // conflict: its name is only its own)
        bool own_symbol = (i->tag == OBJECT_ITEM_METHOD && !(existing && (existing->flags & PROP_METHOD))) || is_accessor;
        // (A member written again is the same binder symbol, declared by both: `{ a: 1, a: 2 }`, `{ m() {}, m() {} }`)
        bool redeclares = existing && !own_symbol && !existing->accessor;
        u32 decl_span_count = redeclares ? existing->decl_span_count + 1 : 1;
        Span* decl_span = arena_new_n(&my arena, Span, decl_span_count);
        if (redeclares) memcpy(decl_span, existing->decl_spans, sizeof(Span) * existing->decl_span_count);
        decl_span[decl_span_count - 1] = i->span;
        if (existing && !own_symbol) name_form = ty_prop_name_form_merge(existing->name_form, name_form);
        u32 flags = 0;
        if (i->tag == OBJECT_ITEM_METHOD) flags |= PROP_METHOD | (i->optional_method ? PROP_OPTIONAL : 0);
        if (i->tag == OBJECT_ITEM_GETTER) flags |= PROP_GETTER | PROP_READONLY; // Until a setter shows up
        if (i->tag == OBJECT_ITEM_SETTER) flags |= PROP_SETTER;
        if (ctx.is_const) flags |= PROP_READONLY;
        if (pattern_context) {
            Prop const* implied = ty_get_prop(me, pattern_context, name);
            if (implied && (implied->flags & PROP_OPTIONAL)) flags |= PROP_OPTIONAL | PROP_PATTERN_OPTIONAL;
        }
        // A destructuring target's member with a default is optional (tsc's checkObjectLiteral inDestructuringPattern)
        if (expr->assignment_pattern && ((i->tag == OBJECT_ITEM_SHORTHAND && i->shorthand_default) ||
            (i->tag == OBJECT_ITEM_PROPERTY && i->value.tag == EXPR_BINARY_OP && i->value.binary_op->op == BINARY_OP_EQUAL))
        ) {
            flags |= PROP_OPTIONAL | PROP_PATTERN_OPTIONAL;
        }
        literal_builder_add(&builder, (ObjectLiteralProp){
            .name = name,
            .flags = flags,
            .type = type,
            .accessor = is_accessor ? i->value.function : nullptr,
            .name_span = i->name_span,
            .decl_spans = decl_span,
            .decl_span_count = decl_span_count,
            .name_is_value = name_is_value,
            .asserted = asserted,
            .own = true,
            .name_form = name_form,
            .name_type = i->computed_key && (key_type->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) ? key_type : nullptr,
        });
    }
    free(accessor_spans);
    literal_members_flush(me, &members, builder.index, &builder.index_count, ctx.is_const);
    free(members.items);
    // (An object literal with a contextual type or an `@enum` tag isn't open-ended: tsc's isJSObjectLiteral in
    // checkObjectLiteral)
    SourceFile const* file = expr->inner_span.source;
    bool js = file && file->is_js && !ctx.contextual && !expr->jsdoc_enum_literal;
    Ty const* ret = spread_acc_finish(me, &spread, &builder, OBJECT_LITERAL_FRESH | (js ? OBJECT_JS : 0), expr);
    free(builder.props);
    return ret;
}

// tsc's checkObjectLiteral: a literal contextually typed by the implied type of an object binding pattern (`{ a } =
// { a: 1, b: 2 }`) may only have the pattern's properties, unless the pattern has a rest element or a computed name
void ty_check_object_literal_pattern(Checker* me, Expr const* expr, ErrorCtx* errs) {
    u64 context_key[2] = { KEY_LITERAL_CONTEXT, (u64)(uintptr_t)expr };
    Ty const* contextual = intern_get(&my links, context_key, 2);
    if (!contextual || !(contextual->flags & TYF_BINDING_PATTERN) || !(contextual->flags & TYF_OBJECT)) return;
    if (ty_is_tuple(contextual) || pattern_type_has_rest(me, contextual)) return;
    if (intern_get(&my links, (u64[]){ KEY_PATTERN_COMPUTED, contextual->id }, 2)) return;
    for_each (i, vec_object_expr_item, &expr->object) {
        if (i->tag != OBJECT_ITEM_PROPERTY && i->tag != OBJECT_ITEM_SHORTHAND && i->tag != OBJECT_ITEM_METHOD) continue;
        if (object_item_is_private_name(i)) continue;
        Atom const* name = nullptr;
        if (i->computed_key) {
            bool is_number = false;
            name = ty_property_name_of_key(me, ty_expr_type(me, i->computed_key), &is_number);
        } else {
            name = checker_atom(me, cstr_sv(&i->name_string));
        }
        if (name && ty_get_prop(me, contextual, name)) continue;
        // (tsc's symbolToString of the member: a name that isn't an identifier as written)
        csview written = i->name_span.sv;
        bool as_written = !name || (written.size && (written.buf[0] == '"' || written.buf[0] == '\'' ||
            written.buf[0] == '[' || (written.buf[0] >= '0' && written.buf[0] <= '9')));
        cstr name_str = as_written ? cstr_from_sv(written) : ty_prop_name_cstr(me, name);
        cstr target = ty_to_cstr(me, contextual);
        error_ctx_error_d(errs, i->name_span, 2353, "Object literal may only specify known properties, and '", name_str,
            "' does not exist in type '", target, "'.");
        c_drop(cstr, &name_str, &target);
    }
}

Ty const* ty_object_literal_accessor_type(Checker* me, FunctionImpl const* accessor, bool getter) {
    Signature const* sig = single_call_signature(me, ty_function_type(me, accessor));
    if (!sig) return any_type(me);
    // (An annotated getter's type is as written: `get s(): typeof S` stays the unique symbol)
    // (Without one, tsc's getReturnTypeFromBody widens a unit type only, no contextual type for an accessor: a union of
    // literals stays)
    if (getter) {
        Ty const* ret = signature_return_type(me, sig);
        return accessor->ret_explicit || !ty_is_unit(ret) ? ret : widen_literals(me, ret);
    }
    // A setter without a getter: its parameter's annotation, else any (tsc's getTypeOfAccessors, whatever the context
    // or an initializer would make of the parameter)
    for_each (i, vec_function_arg, &accessor->args) {
        Binding const* binding = &i->initializer.binding;
        if (binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this")) continue;
        if (!binding->has_explicit_type && !initialized_binding_rare(&i->initializer)->getter) return any_type(me);
        break;
    }
    signature_resolve(me, sig);
    return sig->param_count ? sig->params[0].type : any_type(me);
}

Ty const* ty_object_literal_member_type(Checker* me, Expr const* object, ObjectExprItem const* item) {
    Ty const* type = ty_expr_type(me, object);
    Atom const* name = nullptr;
    if (item->computed_key) {
        bool is_number = false;
        name = ty_property_name_of_key(me, ty_expr_type(me, item->computed_key), &is_number);
    } else {
        name = checker_atom(me, cstr_sv(&item->name_string));
    }
    Prop const* prop = name && (type->flags & TYF_OBJECT) ? ty_get_prop(me, type, name) : nullptr;
    // (Unless a later spread replaced it, or merged its optional property in)
    // (A member written again, `{ a: 1, a: 2 }`, is declared by both but typed by the last: the others aren't it)
    u32 spans = prop ? prop->decl->decl_span_count : 0;
    bool accessor_pair = prop && (prop->flags & (PROP_GETTER | PROP_SETTER)) && spans == 2;
    bool own = prop && prop->decl->literal_own && (!spans || span_eq(prop->decl->decl_spans[spans - 1], item->span) ||
        (accessor_pair && span_eq(prop->decl->decl_spans[0], item->span)));
    if (prop && own) {
        // (An optional method's symbol has undefined in its type, tsc's getTypeOfFuncClassEnumModule)
        if ((prop->flags & PROP_OPTIONAL) && (prop->flags & PROP_METHOD)) return ty_optional_prop_type(me, prop_type(me, prop));
        return prop_type(me, prop);
    }
    // A computed key that made no property: its part of an index signature, as the literal checked it (tsc's
    // checkPropertyAssignment, whose literal is kept by the contextual type: `[k]: true` for `T` inferred as boolean)
    if (item->computed_key && (type->flags & TYF_OBJECT) && item->tag == OBJECT_ITEM_PROPERTY) {
        Members const* members = ty_members(me, type);
        for (u32 i = 0; i < members->index_count; i++) {
            for (u32 j = 0; j < members->index[i].component_count; j++) {
                if (members->index[i].components[j].name == item->computed_key) return members->index[i].components[j].type;
            }
        }
    }
    Ty const* value = ty_expr_type(me, &item->value);
    if (item->tag == OBJECT_ITEM_GETTER || item->tag == OBJECT_ITEM_SETTER) {
        Signature const* sig = single_call_signature(me, value);
        if (!sig) return any_type(me);
        if (item->tag == OBJECT_ITEM_GETTER) {
            Ty const* ret = signature_return_type(me, sig);
            return item->value.tag == EXPR_FUNCTION && item->value.function->ret_explicit ? ret : widen_literals(me, ret);
        }
        return sig->param_count ? sig->params[0].type : any_type(me);
    }
    return widen_literals(me, value);
}

// Array literals

static Ty const* tuple_of(Checker* me, Ty const* const* elements, unsigned char const* flags, u32 len, bool readonly) {
    TupleShape shape = { .arity = len, .flags = flags, .readonly = readonly };
    Ty const* target = ty_tuple_target(me, &shape);
    return len ? ty_reference(me, target, (TyList){ .data = elements, .len = len }) : target;
}

static Ty const* check_array(Checker* me, Expr const* expr, ExprCtx ctx) {
    u32 len = (u32)vec_expr_size(&expr->array);
    // (A destructuring assignment target is a tuple of its targets: tsc's inDestructuringPattern)
    bool as_tuple = ctx.is_const || expr->assignment_pattern || expr->spread_into_call ||
        context_is_tuple_like(me, ctx.contextual, 0);
    Ty const** elements = malloc(sizeof(Ty const*) * (len ? len : 1));
    unsigned char* flags = calloc(len ? len : 1, 1);
    u32 count = 0;
    bool has_omitted = false;
    bool spreads_never = false;
    for_each (i, vec_expr, &expr->array) {
        Ty const* element_context = contextual_array_element_type(me, ctx.contextual, expr, (u32)i_index);
        Ty const* element_sig_context = ctx.sig_contextual ? contextual_array_element_type(me, ctx.sig_contextual, expr, (u32)i_index) : nullptr;
        ExprCtx element_ctx = {
            .contextual = element_context, .sig_contextual = element_sig_context,
            .sig_absent = ctx.sig_absent || (ctx.sig_contextual && !element_sig_context),
            .is_const = ctx.is_const, .skip_sensitive = ctx.skip_sensitive,
            .generic_mode = ctx.generic_mode,
        };
        if (i->tag == EXPR_SPREAD) {
            // (A spread of a const context is in it too: tsc's isConstContext goes up through SpreadElement)
            Ty const* operand = ctx.is_const ? check_with(me, i->spread, (ExprCtx){ .is_const = true }) : ty_expr_type(me, i->spread);
            if (as_tuple) {
                flags[count] = TUPLE_ELEMENT_REST;
                if (expr->assignment_pattern && !ty_is_array(me, operand) && !ty_is_tuple(operand) && !(operand->flags & TYF_ANY) &&
                    !(operand->kind == TY_DECLARED && operand->declared.kind == TY_DECLARED_TUPLE_TARGET)
                ) {
                    // A rest target of a destructuring assignment: its number index signature, else what it iterates,
                    // else unknown (tsc's checkArrayLiteral with inDestructuringPattern)
                    Ty const* apparent = ty_apparent_type(me, operand);
                    IndexInfo const* index = (apparent->flags & TYF_OBJECT) ?
                        ty_applicable_index_info(me, apparent, ty_intrinsic(me, TY_NUMBER)) : nullptr;
                    TyIterationTypes const* iterated = index ? nullptr : ty_iteration_types_of_iterable(me, operand, ITERATION_DESTRUCTURING);
                    Ty const* element = index ? index->value : iterated ? iterated->yield_type : ty_intrinsic(me, TY_UNKNOWN_);
                    elements[count++] = ty_array(me, element, false);
                    continue;
                }
                // (A spread of never makes the tuple never: tsc's createNormalizedTupleType maps a variadic union or
                // never element over its members)
                spreads_never |= (operand->flags & TYF_NEVER) != 0;
                // (The empty tuple is the tuple target, which spreads nothing)
                bool empty_tuple = operand->kind == TY_DECLARED && operand->declared.kind == TY_DECLARED_TUPLE_TARGET;
                elements[count++] = ty_is_array(me, operand) || ty_is_tuple(operand) || empty_tuple ||
                    is_generic_array_like(me, operand) ?
                    operand : ty_array(me, ty_iterated_type(me, ITERATION_SPREAD, operand, span_null, nullptr), false);
            } else {
                // (never is array-like, its element never: tsc's checkArrayLiteral, getIndexedAccessTypeOrUndefined.
                // The spread expression itself is what iterating gives, any)
                // (A generic array-like one is its `[number]`: tsc's getIndexedAccessTypeOrUndefined of a variadic
                // element, `[...arg]` with arg: T & {} is (T & {})[number][])
                elements[count++] = operand->flags & TYF_NEVER ? operand :
                    (is_generic_array_like(me, operand) || (operand->kind == TY_INTERSECTION && is_generic_type(me, operand) &&
                        ty_extends_array(me, operand))) ? ty_indexed(me, operand, ty_intrinsic(me, TY_NUMBER)) : ty_expr_type(me, i);
            }
            continue;
        }
        if (compiler_options()->exact_optional_property_types && i->tag == EXPR_LITERAL && expr_outer_span(i).sv.size == 0) {
            // A hole is an optional element that isn't there (tsc's checkArrayLiteral with an OmittedExpression)
            (void)ty_expr_type(me, i);
            has_omitted = true;
            flags[count] = TUPLE_ELEMENT_OPTIONAL;
            elements[count++] = ty_undefined_or_missing(me);
            continue;
        }
        Ty const* element = check_for_mutable_location(me, i, element_ctx);
        // (tsc's checkArrayLiteral: in a tuple context, a context-sensitive element is an intra-expression inference site)
        if (element_context && !ctx.skip_sensitive && context_is_tuple_like(me, ctx.contextual, 0) && ty_expr_is_context_sensitive(i)) {
            ty_add_intra_expression_site(me, element, element_context);
        }
        if (has_omitted) {
            flags[count] = TUPLE_ELEMENT_OPTIONAL;
            element = ty_optional_prop_type(me, element);
        }
        elements[count++] = element;
    }

    Ty const* ret;
    if (as_tuple) {
        // (A const type parameter constrained to a mutable array gets a mutable tuple)
        ret = spreads_never ? ty_intrinsic(me, TY_NEVER) :
            tuple_of(me, elements, flags, count, ctx.is_const && !ty_some_mutable_array_like(me, ctx.contextual));
    } else {
        // Without strictNullChecks null and undefined elements don't show next to others
        bool strict = compiler_options()->strict_null_checks;
        u32 kept = 0;
        for (u32 i = 0; i < count; i++) {
            if (!strict && (elements[i]->flags & TYF_NULLABLE)) continue;
            elements[kept++] = elements[i];
        }
        Ty const* element;
        if (kept) {
            element = ty_union_subtype_reduced(me, elements, kept);
        } else if (count) {
            element = ty_union(me, elements, count, nullptr, (TyList){0});
        } else {
            // `[]` (an unannotated variable initialized with it is an evolving array in tsc, see binding.c)
            element = compiler_options()->strict_null_checks ? ty_intrinsic(me, TY_IMPLICIT_NEVER) : ty_intrinsic(me, TY_UNDEFINED_WIDENING);
        }
        ret = ty_array(me, element, false);
    }
    free(elements);
    free(flags);
    return ret;
}

// JSX attributes

// The contextual type of one of several children (tsc's getContextualTypeForChildJsxExpression): of an array-like
// children type, its element
static Ty const* jsx_child_context(Checker* me, Ty const* children, u32 index, u32 count) {
    if (!children || count == 1) return children;
    TyList parts = children->kind == TY_UNION ? children->composite.types : (TyList){ .data = &children, .len = 1 };
    TyBuf mapped = {0};
    for (u32 i = 0; i < parts.len; i++) {
        Ty const* part = parts.data[i];
        if (ty_extends_array(me, part)) {
            Value value = value_number(index);
            part = ty_access(me, part, ty_literal(me, &value));
        }
        if (part) ty_buf_push(&mapped, part);
    }
    Ty const* ret = mapped.len ? ty_union(me, mapped.data, mapped.len, nullptr, (TyList){0}) : nullptr;
    ty_buf_drop(&mapped);
    return ret;
}

// tsc's getSemanticJsxChildren: whitespace with a line break and empty `{}` aren't children
bool ty_jsx_child_is_semantic(JsxChild const* child) {
    return child->tag == JSX_CHILD_TEXT ? !child->whitespace_only : child->tag != JSX_CHILD_EMPTY_EXPR;
}

// The type of a child (tsc's checkJsxChildren)
static Ty const* check_jsx_child(Checker* me, JsxChild const* child, Ty const* contextual, bool skip_sensitive) {
    if (child->tag == JSX_CHILD_TEXT) return ty_intrinsic(me, TY_STRING);
    return check_for_mutable_location(me, &child->expr, (ExprCtx){ .contextual = contextual, .skip_sensitive = skip_sensitive });
}

// The attributes of an element as an object literal (tsc's createJsxAttributesTypeFromAttributesProperty), with the
// children as one more property, named by JSX.ElementChildrenAttribute
static Ty const* check_jsx_attributes(Checker* me, Expr const* expr, ExprCtx ctx) {
    JsxElement const* element = jsx_attributes_element(expr);
    if (ctx.contextual && ctx.contextual->kind == TY_UNION) {
        ctx.contextual = discriminate_by_jsx_attributes(me, ctx.contextual, expr);
    }
    LiteralBuilder builder = {0}; // The attributes since the last spread
    SpreadAcc spread = {0};
    bool has_attribute = false, has_spread = false, has_children = false;
    for_each (i, vec_jsx_attribute, &expr->jsx_attributes) {
        has_attribute |= i->tag != JSX_ATTRIBUTE_SPREAD;
        if (i->tag == JSX_ATTRIBUTE_SPREAD) {
            has_spread = true;
            if (builder.count) spread_acc_props(me, &spread, &builder);
            literal_builder_drop(&builder);
            spread_acc_type(me, &spread, check_with(me, &i->value, (ExprCtx){ .contextual = ctx.contextual }));
            continue;
        }
        Atom const* name = checker_atom(me, i->name.sv);
        Ty const* member_context = ctx.contextual ? contextual_named_prop_type(me, ctx.contextual, name) : nullptr;
        Ty const* type;
        switch (i->tag) {
            case JSX_ATTRIBUTE_NO_VALUE: type = ty_intrinsic(me, TY_TRUE); break; // `<a x />` is `<a x={true} />`
            case JSX_ATTRIBUTE_EMPTY_EXPR: type = any_type(me); break; // (the error type)
            default:
                type = check_for_mutable_location(me, &i->value,
                    (ExprCtx){ .contextual = member_context, .skip_sensitive = ctx.skip_sensitive });
                break;
        }
        {
            // (What ty_jsx_attribute_type() shows when a spread of any hides the property)
            u64 key[2] = { KEY_JSX_ATTRIBUTE, (u64)(uintptr_t)i };
            *intern_slot(me, &my links, key, 2) = type;
        }
        Span* decl_span = arena_new(&my arena, Span);
        *decl_span = i->span;
        literal_builder_add(&builder, (ObjectLiteralProp){
            .name = name,
            .type = type,
            .name_span = i->name,
            .decl_spans = decl_span,
            .decl_span_count = 1,
            .own = true,
        });
    }

    // The children
    u32 child_count = 0;
    for_each (i, vec_jsx_child, &element->children) child_count += ty_jsx_child_is_semantic(i);
    if (child_count) {
        Atom const* children_name = ty_jsx_children_prop_name(me, element);
        Ty const* children_context = ctx.contextual && children_name && atom_sv(children_name).size ?
            contextual_named_prop_type(me, ctx.contextual, children_name) : nullptr;
        TyBuf types = {0};
        u32 index = 0;
        for_each (i, vec_jsx_child, &element->children) {
            if (!ty_jsx_child_is_semantic(i)) continue;
            ty_buf_push(&types, check_jsx_child(me, i, jsx_child_context(me, children_context, index++, child_count), ctx.skip_sensitive));
        }
        if (!spread.any && children_name && atom_sv(children_name).size) {
            Ty const* type = types.data[0];
            if (types.len > 1) {
                if (context_is_tuple_like(me, children_context, 0)) {
                    unsigned char* flags = calloc(types.len, 1); // all required
                    type = tuple_of(me, types.data, flags, types.len, false);
                    free(flags);
                } else {
                    type = ty_array(me, ty_union(me, types.data, types.len, nullptr, (TyList){0}), false);
                }
            }
            // Spread in after the attributes
            if (builder.count) spread_acc_props(me, &spread, &builder);
            literal_builder_drop(&builder);
            LiteralBuilder children = {0};
            literal_builder_add(&children, (ObjectLiteralProp){ .name = children_name, .type = type, .own = true });
            spread_acc_props(me, &spread, &children);
            literal_builder_drop(&children);
            has_children = true;
        }
        ty_buf_drop(&types);
    }

    // Fresh (excess properties are errors) if tsc made it with createJsxAttributesTypeHelper(): there's an attribute, or
    // nothing was spread into it (children are spread in)
    bool fresh = has_attribute || (!has_spread && !has_children);
    Ty const* ret = spread_acc_finish(me, &spread, &builder, fresh ? OBJECT_JSX_ATTRIBUTES_FRESH : OBJECT_JSX_ATTRIBUTES, nullptr);
    free(builder.props);
    return ret;
}

Ty const* ty_jsx_attribute_type(Checker* me, Expr const* attributes, JsxAttribute const* attribute) {
    if (attribute->tag == JSX_ATTRIBUTE_NO_VALUE) return ty_intrinsic(me, TY_TRUE);
    if (attribute->tag == JSX_ATTRIBUTE_EMPTY_EXPR) return any_type(me);
    Ty const* type = ty_expr_type(me, attributes);
    Prop const* prop = type->flags & TYF_OBJECT ? ty_get_prop(me, type, checker_atom(me, attribute->name.sv)) : nullptr;
    // (Unless a later spread replaced it)
    // (Not merged with a spread's optional property, which may have no declaration span of its own: `literal_own`)
    if (prop && prop->decl->decl_span_count == 1 && span_eq(prop->decl->decl_spans[0], attribute->span) &&
        prop->decl->literal_own
    ) {
        return prop_type(me, prop);
    }
    u64 key[2] = { KEY_JSX_ATTRIBUTE, (u64)(uintptr_t)attribute };
    Ty const* checked = intern_get(&my links, key, 2);
    if (checked) return checked;
    return widen_literals(me, ty_expr_type(me, &attribute->value));
}

Ty const* ty_mutable_location_type(Checker* me, Expr const* expr, Ty const* contextual) {
    Ty const* type = ty_expr_type(me, expr);
    if (is_type_assertion(expr)) return type;
    if (contextual && any_literal_of_contextual_type(me, type, contextual)) return ty_regular(me, type);
    return widen_literals(me, type);
}

Ty const* ty_array_literal_as_tuple(Checker* me, Expr const* expr, Ty const* contextual) {
    u32 len = (u32)vec_expr_size(&expr->array);
    Ty const** elements = malloc(sizeof(Ty const*) * (len ? len : 1));
    unsigned char* flags = calloc(len ? len : 1, 1);
    Ty const* ret = nullptr;
    for_each (i, vec_expr, &expr->array) {
        if (i->tag == EXPR_SPREAD) {
            // As check_array() makes a spread element of a tuple. An array literal is a tuple too (tsc passes
            // forceTuple on to the spread expression)
            Ty const* operand = i->spread->tag == EXPR_ARRAY ? ty_array_literal_as_tuple(me, i->spread, nullptr) :
                ty_expr_type(me, i->spread);
            flags[i_index] = TUPLE_ELEMENT_REST;
            elements[i_index] = ty_is_array(me, operand) || ty_is_tuple(operand) || is_generic_array_like(me, operand) ?
                operand : ty_array(me, ty_iterated_type(me, ITERATION_SPREAD, operand, span_null, nullptr), false);
            continue;
        }
        elements[i_index] = ty_mutable_location_type(me, i, contextual_array_element_type(me, contextual, expr, (u32)i_index));
    }
    ret = tuple_of(me, elements, flags, len, false);
    free(elements);
    free(flags);
    return ret;
}

// The rest

bool as_type_is_const(AsTypeExpr const* as_type) {
    TypeNode const* type = &as_type->type;
    return type->tag == TYPE_IDENTIFIER && !type->identifier->target && span_equals(type->identifier->name, "const") &&
        !identifier_type_namespace_count(type->identifier);
}

// Whether an instantiation expression keeps a signature (tsc's getInstantiatedSignatures: generic, with the right
// number of type arguments): the type arguments are mapped to the type parameters of exactly those
static bool applies_to_signature(Signature const* sig, TyMapper const* mapper) {
    if (!sig->type_params.len) return false;
    for (u32 k = 0; k < mapper->len; k++) {
        if (mapper->sources[k] == sig->type_params.data[0]) return true;
    }
    return false;
}

bool ty_instantiation_keeps_signature(Checker* me, Signature const* sig, TyMapper const* mapper) {
    signature_resolve(me, sig);
    return applies_to_signature(sig, mapper);
}

// `Bar<string>` as a value: the object type with the type arguments applied to its generic signatures, the others
// dropped (`instantiate_members()`). Returns the type itself if it has no signatures
Ty const* ty_apply_type_arguments_mapper(Checker* me, Ty const* ty, TyMapper const* mapper) {
    Members const* members = ty_members(me, ty);
    if (!members->call_count && !members->construct_count) return ty;
    u32 count = 3 + mapper->len * 2;
    u64* key = malloc(sizeof(u64) * count);
    key[0] = KEY_ANONYMOUS_INSTANCE;
    key[1] = ty->id;
    key[2] = UINT64_MAX; // Not a type id: tells these apart from ordinary instantiations
    u32 flags = TYF_OBJECT | (ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES);
    for (u32 i = 0; i < mapper->len; i++) {
        key[3 + i * 2] = mapper->sources[i]->id;
        key[4 + i * 2] = mapper->targets[i]->id;
        flags |= mapper->targets[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES;
    }
    Ty const* ret = intern_get(&my types, key, count);
    if (!ret) {
        Ty* fresh = ty_alloc(me, TY_ANONYMOUS, flags);
        fresh->anonymous.target = ty;
        fresh->anonymous.mapper = mapper;
        fresh->anonymous.applies_type_args = true;
        fresh->anonymous.free_params_unknown = true;
        fresh->anonymous.state = arena_new(&my arena, TyObjectState);
        *intern_slot(me, &my types, key, count) = fresh;
        ret = fresh;
    }
    free(key);
    return ret;
}

// tsc's getInstantiationExpressionType: object types get their applicable signatures instantiated, unions and
// intersections each member, a type parameter its constraint's
static Ty const* apply_type_arguments_part(Checker* me, Ty const* ty, TyList args, u32 depth) {
    if (depth > 8) return ty;
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        u32 len = ty->composite.types.len;
        Ty const** parts = malloc(sizeof(Ty const*) * (len ? len : 1));
        bool changed = false;
        for (u32 i = 0; i < len; i++) {
            parts[i] = apply_type_arguments_part(me, ty->composite.types.data[i], args, depth + 1);
            changed |= parts[i] != ty->composite.types.data[i];
        }
        Ty const* ret = !changed ? ty : ty->kind == TY_UNION ?
            ty_union(me, parts, len, nullptr, (TyList){0}) : ty_intersection(me, parts, len, nullptr, (TyList){0});
        free(parts);
        return ret;
    }
    if (ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) {
        Ty const* constraint = ty_base_constraint(me, ty);
        if (!constraint || constraint == ty) return ty;
        Ty const* instantiated = apply_type_arguments_part(me, constraint, args, depth + 1);
        return instantiated != constraint ? instantiated : ty;
    }
    if (!(ty->flags & TYF_OBJECT)) return ty;
    Members const* members = ty_members(me, ty);
    Ty const* sources[64];
    Ty const* targets[64];
    u32 len = 0;
    u32 total = 0, applicable = 0, fitting = 0;
    for (u32 pass = 0; pass < 2; pass++) {
        u32 sig_count = pass ? members->construct_count : members->call_count;
        Signature* const* sigs = pass ? members->construct : members->call;
        for (u32 i = 0; i < sig_count; i++) {
            signature_resolve(me, sigs[i]);
            TyList params = sigs[i]->type_params;
            // (tsc's hasCorrectTypeArgumentArity)
            u32 min = 0;
            for (u32 j = 0; j < params.len; j++) {
                if (!ty_param_default(me, params.data[j])) min = j + 1;
            }
            total++;
            if (!params.len || args.len < min || args.len > params.len) continue;
            applicable++;
            if (len + params.len > c_arraylen(sources)) continue;
            // (Type arguments that don't satisfy the constraints, TS2344, leave the signature as it is: tsc's
            // getInstantiatedSignatures, checkTypeArguments)
            Ty const* own[64];
            for (u32 j = 0; j < params.len; j++) {
                Ty const* target = j < args.len ? args.data[j] : ty_param_default(me, params.data[j]);
                own[j] = target ? target : ty_intrinsic(me, TY_UNKNOWN_);
            }
            if (params.len > c_arraylen(own)) continue;
            TyMapper const* own_mapper = ty_mapper_new(me, params, (TyList){ .data = own, .len = params.len });
            bool fits = true;
            for (u32 j = 0; j < params.len && fits; j++) {
                Ty const* constraint = ty_param_constraint(me, params.data[j]);
                fits = !constraint || ty_is_related(me, own[j], ty_instantiate(me, constraint, own_mapper), TY_RELATION_ASSIGNABLE);
            }
            if (!fits) continue;
            fitting++;
            for (u32 j = 0; j < params.len; j++) {
                bool seen = false;
                for (u32 k = 0; k < len; k++) seen |= sources[k] == params.data[j];
                if (seen) continue;
                sources[len] = params.data[j];
                targets[len++] = own[j];
            }
        }
    }
    // (None of them fits and none was dropped: the type as it is. HACK: with some dropped, the ones that don't fit are
    // dropped too, where tsc keeps them uninstantiated)
    if (!fitting && applicable && applicable == total) return ty;
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len });
    return ty_apply_type_arguments_mapper(me, ty, mapper);
}

Ty const* ty_apply_type_arguments(Checker* me, Ty const* ty, TyList args) {
    if (ty->flags & TYF_ANY) return ty;
    return apply_type_arguments_part(me, ty, args, 0);
}

Ty const* ty_optional_expression_type(Checker* me, Ty const* object, bool is_root, bool* optional) {
    bool dummy = false;
    if (!optional) optional = &dummy;
    if (!compiler_options()->strict_null_checks) return is_root ? ty_non_nullable(me, object) : object;
    if (is_root) {
        *optional = ty_can_be_nullish(me, object) && !(object->flags & (TYF_ANY | TYF_UNKNOWN));
        // (tsc's getNonNullableType, which takes void too)
        return ty_non_nullish_part(me, object);
    }
    // (Only the rest of an optional chain sees the marker, see TY_OPTIONAL: no need to ask whether object_expr is one)
    Ty const* ret = ty_remove_optional_marker(me, object);
    *optional = ret != object;
    return ret;
}

static Ty const* chain_object_type(Checker* me, Expr const* object_expr, bool is_root, bool* optional) {
    return ty_optional_expression_type(me, ty_expr_type(me, object_expr), is_root, optional);
}

// `Symbol()` or `Symbol.for()` with the global `Symbol` (tsc's isSymbolOrSymbolForCall)
static bool is_symbol_call(Checker* me, CallExpr const* call) {
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_PROPERTY && span_equals(callee->property->prop.inner_span, "for")) callee = &callee->property->expr;
    if (callee->tag != EXPR_IDENTIFIER || !csview_equals(callee->inner_span.sv, "Symbol")) return false;
    Symbol const* global = program_get_global(my program, c_sv("Symbol"), SYMBOL_VALUE);
    return global && binder_redirect(my binder, callee->identifier.target) == global;
}

// An argument of a call as tsc's getEffectiveCallArguments has it: the spread of a tuple is its elements (a rest
// element being a spread of its array)
typedef struct EffectiveArg {
    Expr const* expr; // A written argument, else an element of a spread tuple
    Ty const* type; // The element's type (the array's, for a rest element)
    bool spread; // A spread: of `expr` (a spread of something else than a tuple) or of the array `type`
    Atom const* tuple_name; // An element of a spread tuple: its label, if any
} EffectiveArg;

// Null if a spread tuple has generic elements: where the arguments go isn't known then
static EffectiveArg* effective_call_args(Checker* me, CallExpr const* call, u32* count_out) {
    u32 arg_count = (u32)vec_expr_size(&call->args);
    u32 cap = arg_count + 8;
    EffectiveArg* ret = malloc(sizeof(EffectiveArg) * cap);
    panic_if(!ret, "out of memory");
    u32 count = 0;
    for (u32 a = 0; a < arg_count; a++) {
        Expr const* arg = vec_expr_at(&call->args, a);
        if (arg->tag != EXPR_SPREAD) {
            if (count == cap) ret = realloc(ret, sizeof(EffectiveArg) * (cap *= 2));
            ret[count++] = (EffectiveArg){ .expr = arg };
            continue;
        }
        Ty const* spread = ty_expr_type(me, arg->spread);
        if (!ty_tuple_shape(spread)) {
            if (count == cap) ret = realloc(ret, sizeof(EffectiveArg) * (cap *= 2));
            ret[count++] = (EffectiveArg){ .expr = arg, .type = spread, .spread = true };
            continue;
        }
        TupleShape const* shape = ty_tuple_shape(spread);
        TyList elements = shape->arity ? ty_reference_args(me, spread) : (TyList){0};
        for (u32 i = 0; i < shape->arity && i < elements.len; i++) {
            bool rest = shape->flags[i] & TUPLE_ELEMENT_REST;
            if (rest && (elements.data[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
                free(ret);
                return nullptr;
            }
            if (count == cap) ret = realloc(ret, sizeof(EffectiveArg) * (cap *= 2));
            ret[count++] = (EffectiveArg){
                .type = elements.data[i], // (a rest element's is its array)
                .spread = rest,
                .tuple_name = shape->names ? shape->names[i] : nullptr,
            };
        }
    }
    *count_out = count;
    return ret;
}

// How many arguments an immediately invoked function gets (tsc's getEffectiveCallArguments(iife).length): past them its
// unannotated parameters are optional
u32 ty_iife_effective_arg_count(Checker* me, FunctionImpl const* function) {
    CallExpr const* call = function_impl_rare(function)->iife_call;
    if (!call) return function_impl_rare(function)->iife_arg_count;
    bool spread = false;
    for_each (i, vec_expr, &call->args) spread |= i->tag == EXPR_SPREAD;
    if (!spread) return function_impl_rare(function)->iife_arg_count;
    u32 count = 0;
    EffectiveArg* args = effective_call_args(me, call, &count);
    if (!args) return function_impl_rare(function)->iife_arg_count;
    free(args);
    return count;
}

// tsc's getContextuallyTypedParameterType of an immediately invoked function expression: its unannotated parameters
// are the widened types of the (effective) arguments, a rest parameter the tuple of those left (getSpreadArgumentType),
// a missing argument undefined (widening to any without strictNullChecks) unless the parameter has an initializer
static void assign_iife_parameter_types(Checker* me, FunctionImpl const* function, CallExpr const* call) {
    u64 done_key[2] = { KEY_CONTEXT_DONE, (u64)(uintptr_t)function };
    if (intern_get(&my links, done_key, 2)) return;
    u32 arg_count = 0;
    EffectiveArg* args = effective_call_args(me, call, &arg_count);
    if (!args) return;
    u32 index = 0;
    for_each (i, vec_function_arg, &function->args) {
        bool is_this = i->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(i->initializer.binding.content_span, "this");
        if (is_this) continue;
        u32 at = index++;
        if (i->initializer.binding.has_explicit_type) continue;
        LazyMemo* param_memo = lazy_type_state_peek((LazyType*)&i->initializer.lazy_type);
        if (param_memo && param_memo->evaluation_started) continue;
        Ty const* type = nullptr;
        // (What a spread element gives a parameter that isn't a rest isn't done)
        bool unknown = false;
        for (u32 a = at; a < arg_count && (i->rest || a == at); a++) {
            unknown |= args[a].expr && args[a].spread && !i->rest;
        }
        if (unknown) continue;
        if (i->rest) {
            u32 arity = arg_count > at ? arg_count - at : 0;
            if (arg_count && args[arg_count - 1].spread && at >= arg_count - 1) {
                // Both the parameter and the last argument are a rest: its array (tsc's getMutableArrayOrTupleType,
                // which keeps a type variable constrained to a mutable array)
                type = args[arg_count - 1].type;
                if (type->flags & TYF_ANY) {
                    // (tsc's getMutableArrayOrTupleType keeps any)
                } else if (ty_is_tuple(type) && ty_tuple_shape(type)->readonly) {
                    unknown = true; // (TODO: the mutable copy)
                } else if (!ty_extends_array(me, type) && type->kind != TY_TYPE_PARAM) {
                    // An iterable: the array of what it iterates
                    type = ty_array(me, ty_iterated_type(me, ITERATION_SPREAD, type, span_null, nullptr), false);
                } else if (!ty_is_tuple(type) && !ty_is_array(me, type) && type->kind != TY_TYPE_PARAM) {
                    unknown = true;
                }
                if (unknown) continue;
            } else {
                unsigned char* flags = calloc(arity ? arity : 1, 1);
                Ty const** elements = calloc(arity ? arity : 1, sizeof(Ty const*));
                Atom const** names = nullptr; // (The labels of spread tuple elements stay: tsc's getSpreadArgumentType)
                for (u32 a = 0; a < arity; a++) {
                    EffectiveArg const* arg = &args[at + a];
                    if (arg->tuple_name) {
                        if (!names) names = calloc(arity, sizeof(Atom const*));
                        names[a] = arg->tuple_name;
                    }
                    if (arg->spread && arg->expr && !ty_extends_array(me, arg->type)) {
                        // An iterable: a rest element of what it iterates
                        elements[a] = ty_array(me, ty_iterated_type(me, ITERATION_SPREAD, arg->type, span_null, nullptr), false);
                        flags[a] = TUPLE_ELEMENT_REST;
                    } else if (arg->spread) {
                        // (a variadic element when it's a type variable)
                        elements[a] = arg->type;
                        flags[a] = TUPLE_ELEMENT_REST;
                    } else {
                        elements[a] = ty_widened_literal(me, arg->expr ? ty_expr_type(me, arg->expr) : arg->type);
                    }
                }
                TupleShape shape = { .arity = arity, .flags = flags, .names = names };
                Ty const* target = ty_tuple_target(me, &shape);
                type = arity ? ty_reference(me, target, (TyList){ .data = elements, .len = arity }) : target;
                free(flags);
                free(elements);
                free(names);
            }
        } else if (at < arg_count) {
            // (then widened like a declaration's type: null is any without strictNullChecks). A spread element is
            // what it spreads (tsc's checkSyntheticExpression)
            EffectiveArg const* arg = &args[at];
            Ty const* arg_type = arg->expr ? ty_expr_type(me, arg->expr) :
                arg->spread ? ty_reference_args(me, arg->type).data[0] : arg->type;
            type = ty_widened_for_declaration(me, arg_type, true);
        } else if (initialized_binding_value(&i->initializer)->tag == EXPR_IMPLICIT_ANY) {
            type = ty_intrinsic(me, compiler_options()->strict_null_checks ? TY_UNDEFINED : TY_ANY);
        }
        if (!type) continue;
        u64 key[2] = { KEY_CONTEXT_PARAM, (u64)(uintptr_t)&i->initializer };
        void const** slot = intern_slot(me, &my links, key, 2);
        if (!*slot) *slot = type;
    }
    free(args);
}

void ty_assign_iife_parameter_types(Checker* me, CallExpr const* call) {
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    if (callee->tag == EXPR_FUNCTION && callee->function->is_iife && !call->uses_new) {
        assign_iife_parameter_types(me, callee->function, call);
    }
}

static Ty const* check_call(Checker* me, CallExpr const* call, Span call_span, ExprCtx ctx) {
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr; // call.c applies the type arguments
    // An immediately invoked function returns into the call's contextual type (tsc's getContextualReturnType), which
    // only its return expressions see: the return type isn't widened against it (getReturnTypeFromBody)
    if (callee->tag == EXPR_FUNCTION && callee->function->is_iife && !callee->function->ret_explicit && ctx.contextual &&
        !(ctx.contextual->flags & (TYF_ANY | TYF_VOID))
    ) {
        u64 key[2] = { KEY_CONTEXT_IIFE_RETURN, (u64)(uintptr_t)callee->function };
        void const** slot = intern_slot(me, &my links, key, 2);
        if (!*slot) *slot = ctx.contextual;
    }
    ty_assign_iife_parameter_types(me, call);
    bool optional = false;
    Ty const* callee_type = chain_object_type(me, callee, call->optional_chain, &optional);
    // An incomplete `never` of control flow calls to nothing (tsc's silentNeverSignature)
    if (callee_type == ty_intrinsic(me, TY_SILENT_NEVER)) return callee_type;
    if ((callee_type->flags & TYF_UNKNOWN) && !call->is_extends && compiler_options()->strict_null_checks) {
        callee_type = ty_intrinsic(me, TY_ERROR); // (See expr_typecheck(): TS18046)
    }
    if (!call->uses_new && !call->is_extends) callee_type = ty_check_non_null_callee(me, callee_type, span_null, nullptr);
    Ty const* ret = ty_call_return_type(me, callee_type, call, call_span, ctx.raw_contextual);
    if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_SUPER) {
        return ty_intrinsic(me, TY_VOID); // Whatever it resolved to, and even if it's an error (`new super()` too)
    }
    // The initializer of a `const` (or a `static readonly` property) makes a new unique symbol
    if (call->unique_symbol && call->unique_symbol->key && (ret->flags & TYF_ES_SYMBOL_LIKE) && is_symbol_call(me, call)) {
        ret = ty_unique_symbol(me, call->unique_symbol);
    }
    return propagate_optional(me, ret, call->continues_chain, optional);
}

// What a `yield` evaluates to: what the generator is sent (its TNext), for `yield*` what the delegate returns (tsc's
// checkYieldExpression). The operand is checked with the yield type as its contextual type, a `yield*` operand with a
// generator that yields that, returns what the `yield*` itself is expected to be and is sent the same
static Ty const* check_yield(Checker* me, UnaryOpExpr const* yield, ExprCtx ctx) {
    FunctionImpl const* generator = yield->generator;
    Expr const* operand = &yield->expr;
    // What the generator is sent (tsc's checkAndAggregateYieldOperandTypes: getContextualType of each yield)
    if (ctx.contextual && yield->op == UNARY_OP_YIELD) {
        u64 key[2] = { KEY_YIELD_CONTEXT, (u64)(uintptr_t)yield };
        // (A call's type parameter, which the resolved signature has as what this any yield inferred: see
        // UnaryOpExpr.no_type_params_in_scope)
        Ty const* sent = ctx.contextual->kind == TY_TYPE_PARAM && yield->no_type_params_in_scope ? ty_intrinsic(me, TY_ANY) : ctx.contextual;
        if (!intern_get(&my links, key, 2)) *intern_slot(me, &my links, key, 2) = sent;
    }
    if (!generator) {
        if (operand->tag != EXPR_IMPLICIT_ANY && !yield->outside_generator) ty_expr_type(me, operand);
        return any_type(me);
    }
    bool async = generator->is_async;
    TyIterationTypes const* context = ty_generator_context(me, generator);
    if (yield->op == UNARY_OP_YIELD_STAR) {
        Ty const* operand_context = nullptr;
        if (context) {
            Ty const* silent = ty_intrinsic(me, TY_SILENT_NEVER);
            Ty const* yield_type = context->yield_type;
            Ty const* return_type = ctx.contextual ? ctx.contextual : silent;
            operand_context = ty_generator_type(me, false, yield_type, return_type, context->next_type);
            if (async) {
                Ty const* both[2] = { operand_context, ty_generator_type(me, true, yield_type, return_type, context->next_type) };
                operand_context = ty_union(me, both, 2, nullptr, (TyList){0});
            }
        }
        Ty const* delegate_type = check_with(me, operand, (ExprCtx){ .contextual = operand_context });
        u32 use = async ? ITERATION_ASYNC_YIELD_STAR : ITERATION_YIELD_STAR;
        TyIterationTypes const* delegate = ty_iteration_types_of_iterable(me, delegate_type, use);
        return delegate ? delegate->return_type : any_type(me);
    }
    if (operand->tag != EXPR_IMPLICIT_ANY) check_with(me, operand, (ExprCtx){ .contextual = context ? context->yield_type : nullptr });
    return context ? context->next_type : any_type(me);
}

// `t[5]` of tuples (every member of a union) with no element there: undefined after the error
static bool tuple_past_the_end(Checker* me, Ty const* object, Ty const* key) {
    if (!(key->flags & TYF_NUMBER_LITERAL)) return false;
    TyList types = object->kind == TY_UNION ? object->composite.types : (TyList){ .data = &object, .len = 1 };
    for (u32 i = 0; i < types.len; i++) {
        Ty const* apparent = ty_apparent_type(me, types.data[i]);
        if (!ty_is_tuple(apparent) && !ty_tuple_shape(apparent)) return false; // (`[]` is its own target)
    }
    return true;
}

// `{ a: 1, b: "" }[k]` that doesn't find k (tsc's getPropertyTypeForIndexType on an object literal type): a missing
// literal key is undefined (TS2339 under noImplicitAny), a string or number key any of the properties or undefined
static Ty const* object_literal_access(Checker* me, Ty const* object, Ty const* key) {
    key = ty_regular(me, key);
    TyList keys = key->kind == TY_UNION && !(key->flags & TYF_BOOLEAN) ? key->composite.types : (TyList){ .data = &key, .len = 1 };
    TyBuf parts = {0};
    for (u32 i = 0; i < keys.len; i++) {
        Ty const* part = ty_access(me, object, keys.data[i]);
        if (!part && (keys.data[i]->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL))) {
            // (Only with noImplicitAny, where it's TS2339: else tsc's getPropertyTypeForIndexType goes on to any)
            if (!compiler_options()->no_implicit_any) {
                ty_buf_drop(&parts);
                return nullptr;
            }
            part = ty_intrinsic(me, TY_UNDEFINED);
        } else if (!part && (keys.data[i]->flags & (TYF_STRING | TYF_NUMBER))) {
            Members const* members = ty_members(me, object);
            for (u32 j = 0; j < members->prop_count; j++) ty_buf_push(&parts, prop_type(me, members->props[j]));
            part = ty_intrinsic(me, TY_UNDEFINED);
        }
        if (!part) {
            ty_buf_drop(&parts);
            return nullptr;
        }
        ty_buf_push(&parts, part);
    }
    Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    return ret;
}

// What `o[k] = v` takes for a union of keys: every key's property, the intersection of their (write) types (tsc's
// getIndexedAccessType with AccessFlags.Writing). Null when a key has none (the read reports it)
static Ty const* union_key_write_type(Checker* me, Ty const* object, Ty const* key) {
    // (A generic object with keys that aren't: its apparent type's, like tsc's getIndexedAccessType of an element access
    // expression, which only defers for a generic index or tuple)
    if (ty_is_generic_object(me, object)) object = ty_apparent_type(me, object);
    if (ty_is_generic_object(me, object)) return nullptr;
    TyList keys = key->composite.types;
    TyBuf parts = {0};
    for (u32 i = 0; i < keys.len; i++) {
        Ty const* k = keys.data[i];
        if (!(k->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL))) {
            ty_buf_drop(&parts);
            return nullptr;
        }
        Ty const* part = ty_access_write_type(me, object, k);
        if (!part) part = ty_access(me, object, k);
        if (!part) {
            ty_buf_drop(&parts);
            return nullptr;
        }
        ty_buf_push(&parts, part);
    }
    Ty const* ret = ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    return ret;
}

Ty const* ty_compound_assignee_type(Checker* me, Expr const* target, Ty const* read_type) {
    if (target->tag != EXPR_PROPERTY || target->property->form != PROPERTY_EXPR_DOT) return read_type;
    Ty const* object = ty_expr_type(me, &target->property->expr);
    Ty const* write = ty_access_write_type(me, object, ty_access_key_type(me, target->property));
    return write ? write : read_type;
}

// tsc's isStringIndexSignatureOnlyType: nothing but a string index signature
static bool is_string_index_only(Checker* me, Ty const* ty) {
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!is_string_index_only(me, ty->composite.types.data[i])) return false;
        }
        return true;
    }
    if (!(ty->flags & TYF_OBJECT)) return false;
    Members const* members = ty_members(me, ty);
    return !members->prop_count && !members->call_count && !members->construct_count && !members->mapped &&
        members->index_count == 1 && (members->index[0].key->flags & TYF_STRING);
}

// JavaScript `this.x` in the constructor that declares x, when only `this.x = v` assignments do (tsc's
// isThisPropertyAccessInConstructor with isConstructorDeclaredProperty): x, whose type is being inferred from them.
// Null otherwise
static Symbol const* constructor_declared_member(PropertyExpr const* access) {
    // `this.x`, `this["x"]`
    csview name = access->prop.inner_span.sv;
    if (access->form != PROPERTY_EXPR_DOT) {
        if (access->prop.tag != EXPR_LITERAL || access->prop.literal.tag != VALUE_STRING || name.size < 2) return nullptr;
        name = csview_slice(name, 1, name.size - 1);
    }
    Expr const* object = &access->expr;
    IdentifierExpr const* this = expr_this_or_alias(object);
    if (!this) return nullptr;
    SourceFile const* file = object->inner_span.source;
    if (!file || !file->is_js) return nullptr;
    Symbol const* class_symbol = nullptr;
    if (this->this_kind == THIS_CLASS_INSTANCE) {
        if (!property_context(access)->in_constructor_of) return nullptr;
        class_symbol = this->target;
    } else if (this->this_kind == THIS_ANY && identifier_this_function(this)) {
        class_symbol = js_constructor_for_this(identifier_this_function(this));
    }
    if (!class_symbol || !symbol_rare(class_symbol)->members) return nullptr;
    Symbol const* member = symbol_table_get(symbol_rare(class_symbol)->members, name);
    if (!member) return nullptr;
    if (!ty_is_constructor_declared(member)) return nullptr;
    // Only in the constructor (tsc: getThisContainer(node) === getDeclaringConstructor(prop))
    if (this->this_kind == THIS_ANY && js_constructor_for_this(identifier_this_function(this)) != class_symbol) return nullptr;
    // Not when the class body declares it
    ClassDecl const* class_decl = symbol_find_node(class_symbol, SYMBOL_DECL_CLASS);
    return !class_decl || !class_decl_find_member(class_decl, name, false) ? member : nullptr;
}

// An assignment to it: any
static bool is_constructor_declared_target(Checker* me, PropertyExpr const* access) {
    (void)me;
    return access->assignment_target == FLOW_TARGET_DEFINITE && constructor_declared_member(access);
}

// A read of it in a class constructor: what control flow says it has been assigned so far (tsc's getFlowTypeOfProperty
// with autoType). Null otherwise
static Ty const* constructor_declared_read(Checker* me, Expr const* expr) {
    PropertyExpr const* access = expr->property;
    if (access->assignment_target || !access->flow) return nullptr;
    Symbol const* member = constructor_declared_member(access);
    if (!member || !symbol_rare(member)->declaring_constructor || !function_impl_rare(symbol_rare(member)->declaring_constructor)->return_flow) return nullptr;
    if (expr_this_or_alias(&access->expr)->this_kind != THIS_CLASS_INSTANCE) return nullptr;
    return ty_constructor_declared_flow_type(me, member, expr, access->flow);
}

// `o[k]` with o made of nothing but a string index signature and a key that is a string or a number (each on its own,
// tsc's isTypeAssignableToKind: `K extends string | number` is neither) reads that signature
static bool reads_string_index_only(Checker* me, Ty const* object, Ty const* key) {
    return is_string_index_only(me, object) && !(key->flags & TYF_NULLABLE) &&
        (ty_is_related(me, key, ty_intrinsic(me, TY_STRING), TY_RELATION_ASSIGNABLE) ||
            ty_is_related(me, key, ty_intrinsic(me, TY_NUMBER), TY_RELATION_ASSIGNABLE));
}

// `o[k]` with a generic key that ty_access() couldn't read through a constraint: the deferred
// `O[K]` (tsc's getIndexedAccessType), unless o has nothing but a string index signature, which is what it reads then
static Ty const* generic_element_access(Checker* me, PropertyExpr const* access, Ty const* object, Ty const* key) {
    key = ty_regular(me, key);
    if (!ty_is_generic_index(key)) return nullptr;
    if (object->flags & (TYF_ANY | TYF_UNKNOWN)) return object;
    if (reads_string_index_only(me, object, key)) {
        Ty const* string = ty_intrinsic(me, TY_STRING);
        // (A compound assignment reads it too: tsc's AccessFlags.ExpressionPosition unless Definite)
        return access->assignment_target == FLOW_TARGET_DEFINITE ? ty_access(me, object, string) : ty_access_reading(me, object, string);
    }
    // (A read under noUncheckedIndexedAccess: tsc's AccessFlags.IncludeUndefined on the deferred type)
    bool include_undefined = compiler_options()->no_unchecked_indexed_access && access->assignment_target != FLOW_TARGET_DEFINITE;
    Ty const* ret = ty_indexed_include_undefined(me, object, key, include_undefined);
    // (What tsc's checkIndexedAccessIndexType rejects is the error type, reported as TS2536 by the typecheck pass)
    return ret->kind == TY_INDEXED && !ty_indexed_access_index_ok(me, ret) ? ty_intrinsic(me, TY_ERROR) : ret;
}

// `o[k]` with k generic that tsc's checkIndexedAccessIndexType rejects: the indexed access type, else null
Ty const* ty_invalid_generic_element_access(Checker* me, PropertyExpr const* access, Ty const* object) {
    if (access->form != PROPERTY_EXPR_BRACKET) return nullptr;
    Ty const* key = ty_regular(me, ty_access_key_type(me, access));
    if (!ty_is_generic_index(key) || (object->flags & (TYF_ANY | TYF_UNKNOWN | TYF_ERROR))) return nullptr;
    if (ty_access(me, object, key)) return nullptr;
    if (reads_string_index_only(me, object, key)) {
        return nullptr;
    }
    Ty const* ret = ty_indexed(me, object, key);
    return ret->kind == TY_INDEXED && !ty_indexed_access_index_ok(me, ret) ? ret : nullptr;
}

// A class expression's static fields without a type annotation have the contextual type of the property of the
// same name of the class expression's contextual type (tsc's getContextualTypeForInitializerExpression)
static void contextually_type_static_fields(Checker* me, ClassDecl const* class_decl, Ty const* contextual) {
    for_each (i, vec_class_item, &class_decl->items) {
        if (i->tag != CLASS_ITEM_FIELD || !i->field.is_static || i->field.has_explicit_type) continue;
        if (i->field.value.tag == EXPR_IMPLICIT_ANY || span_valid(class_item_private_name(i))) continue;
        Ty const* key = i->field.computed_key ? ty_regular(me, ty_expr_type(me, i->field.computed_key)) :
            ty_string_literal(me, cstr_sv(&i->field.name_string));
        if (!(key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL))) continue;
        Ty const* prop = ty_contextual_prop_type(me, contextual, key);
        if (prop) ty_check_expr(me, &i->field.value, prop);
    }
}

static Ty const* compute_inner(Checker* me, Expr const* expr, ExprCtx ctx);

static Ty const* compute(Checker* me, Expr const* expr, ExprCtx ctx) {
    // tsc's checkExpression: the current node, and the instantiation limit is per expression, not per program
    CurrentNode saved = ty_enter_expr(me, expr);
    Ty const* ret = compute_inner(me, expr, ctx);
    ty_leave_node(me, saved);
    return ret;
}

static Ty const* compute_inner(Checker* me, Expr const* expr, ExprCtx ctx) {
    switch (expr->tag) {
        case EXPR_IMPLICIT_ANY: return any_type(me);
        case EXPR_LITERAL:
            if (expr->literal.tag == VALUE_NULL) return ty_intrinsic(me, TY_NULL_WIDENING);
            if (expr->undefined_assigned) return ty_intrinsic(me, TY_ERROR); // (tsc's checkIdentifier)
            if (expr->literal.tag == VALUE_UNDEFINED) return ty_intrinsic(me, TY_UNDEFINED_WIDENING);
            return ty_fresh_literal(me, ty_literal(me, &expr->literal));
        case EXPR_IDENTIFIER: {
            IdentifierFlowIssue issue = IDENTIFIER_FLOW_OK; // Reported by the typecheck walk
            return identifier_flow_type(me, expr, ctx.contextual, &issue, false);
        }
        case EXPR_BINARY_OP: return check_binary(me, expr->binary_op, ctx);
        case EXPR_UNARY_OP:
            if (expr->unary_op->op == UNARY_OP_YIELD || expr->unary_op->op == UNARY_OP_YIELD_STAR) {
                return check_yield(me, expr->unary_op, ctx);
            }
            // `x!` has the contextual type of its operand
            if (expr->unary_op->op == UNARY_OP_RIGHT_BANG && ctx.contextual) {
                Ty const* type = check_with(me, &expr->unary_op->expr, (ExprCtx){ .contextual = ctx.contextual });
                return ty_non_nullable(me, type);
            }
            // tsc's getContextualTypeForAwaitOperand: what the context awaits to, or a promise of it
            if (expr->unary_op->op == UNARY_OP_AWAIT && ctx.contextual) {
                Ty const* awaited = ty_awaited_no_alias(me, ctx.contextual);
                Ty const* parts[2] = { awaited, ty_promise_like_of(me, awaited) };
                Ty const* context = ty_union(me, parts, 2, nullptr, (TyList){0});
                // (Still what a binding pattern implies, which calls treat differently: tsc's isFromBindingPattern)
                if (ctx.contextual->flags & TYF_BINDING_PATTERN) context = ty_pattern_clone(me, context);
                return ty_awaited(me, check_with(me, &expr->unary_op->expr, (ExprCtx){ .contextual = context }));
            }
            return check_unary(me, expr->unary_op);

        case EXPR_PROPERTY: {
            if (ty_is_import_meta(expr)) return ty_import_meta_expr_type(me, expr);
            if (is_constructor_declared_target(me, expr->property)) return any_type(me);
            Ty const* in_constructor = constructor_declared_read(me, expr);
            if (in_constructor) return in_constructor;
            bool optional = false;
            Ty const* object = chain_object_type(me, &expr->property->expr, expr->property->optional_chain, &optional);
            // (tsc's getWidenedType: an object literal isn't one any more, a fresh literal widens)
            if (expr->property->method_call || expr->property->assignment_target) {
                object = ty_widened_objects(me, ty_widened_nullable(me, object));
            }
            // An incomplete `never` of control flow is like any (tsc's silentNeverType), a property of the error type is
            // the error type (checkPropertyAccessExpressionOrQualifiedName)
            // (isErrorType: an unresolved type name is one too)
            if (object == ty_intrinsic(me, TY_SILENT_NEVER)) return object;
            if (object->flags & TYF_ERROR) return ty_intrinsic(me, TY_ERROR);
            // `never` has no properties (TS2339), `never[k]` is never
            if ((ty_reduced(me, object)->flags & TYF_NEVER) && expr->property->form == PROPERTY_EXPR_DOT) return any_type(me);
            Ty const* key = ty_access_key_type(me, expr->property);
            if (expr->property->form == PROPERTY_EXPR_BRACKET) key = ty_string_index_only_key(me, object, key);
            // `o[k]` with k any and no index signature that takes it is errorType (tsc's getPropertyTypeForIndexType
            // finds nothing for an access expression)
            if (expr->property->form == PROPERTY_EXPR_BRACKET && (key->flags & TYF_ANY) && !(object->flags & TYF_ANY)) {
                Ty const* apparent = ty_apparent_type(me, object);
                if ((apparent->flags & TYF_OBJECT) && !(apparent->flags & TYF_ANY) && !ty_is_js_literal_type(me, apparent) &&
                    !ty_applicable_index_info(me, apparent, key)
                ) {
                    return ty_intrinsic(me, TY_ERROR);
                }
            }
            // (Written to, a generic object's index signatures don't apply: TS2339/TS2862, errorType)
            if (ty_generic_write_index_info(me, expr->property, object, key)) return ty_intrinsic(me, TY_ERROR);
            // (A compound assignment reads it too: tsc's AccessFlags.ExpressionPosition unless Definite)
            // (`never[K]` with a generic K is deferred like any generic access: tsc's getIndexedAccessTypeOrUndefined
            // looks at the index before the object)
            bool deferred_never = (object->flags & TYF_NEVER) && expr->property->form == PROPERTY_EXPR_BRACKET &&
                ty_is_generic_index(ty_regular(me, key));
            Ty const* ret = deferred_never ? nullptr :
                expr->property->assignment_target == FLOW_TARGET_DEFINITE ? ty_access(me, object, key) :
                ty_access_reading(me, object, key);
            // (An element access: a property access of what an object literal lacks is TS2339, checkPropertyAccessExpression)
            if (!ret && ty_is_fresh_object(object) && !expr->property->assignment_target && expr->property->form == PROPERTY_EXPR_BRACKET) {
                ret = object_literal_access(me, object, key);
            }
            if (!ret && expr->property->form == PROPERTY_EXPR_BRACKET) ret = generic_element_access(me, expr->property, object, key);
            if (!ret && expr->property->qualified_name && (key->flags & TYF_STRING_LITERAL)) {
                Prop const* prop = ty_namespace_type_only_prop(me, object, ty_literal_name(me, key));
                if (prop) ret = prop_type(me, prop);
            }
            if (!ret && expr->property->form == PROPERTY_EXPR_BRACKET && tuple_past_the_end(me, object, key)) {
                return ty_intrinsic(me, TY_UNDEFINED); // TS2493/TS2514 (tsc's getPropertyTypeForIndexType)
            }
            if (!ret) return missing_property_type(me, expr, object);
            if (ty_is_readonly_assignment(me, expr->property, object)) return any_type(me);
            if (expr->property->assignment_target == FLOW_TARGET_DEFINITE) {
                Ty const* write = key->kind == TY_UNION ? union_key_write_type(me, object, key) : ty_access_write_type(me, object, key);
                if (write) ret = write;
                ret = ty_remove_missing(me, ret, true); // (tsc's getFlowTypeOfAccessExpression)
            }
            // (A property of any isn't narrowed: tsc's checkPropertyAccessExpressionOrQualifiedName returns any before
            // getFlowTypeOfAccessExpression. An element access is)
            if (expr->property->form != PROPERTY_EXPR_DOT || !(ty_apparent_type(me, object)->flags & TYF_ANY)) {
                ret = access_flow_type(me, expr, ret, ctx.contextual);
            }
            return propagate_optional(me, ret, expr->property->continues_chain, optional);
        }

        case EXPR_GENERIC_PARAMS: {
            // An instantiation expression. As the callee of a call it never gets here, see check_call()
            GenericParamsExpr const* generic = expr->generic_params;
            // (A class's `extends B<T>` is a type reference's type arguments, what the class's base constructor type
            // and super calls make of them: tsc's getBaseConstructorTypeOfClass checks the expression alone)
            if (generic->heritage) return ty_expr_type(me, &generic->expr);
            Ty const* args[32];
            u32 len = 0;
            for_each (i, vec_spanned_type, &generic->params) {
                if (len < c_arraylen(args)) args[len++] = ty_from_node(me, &i->type, expr_lower_ctx);
            }
            return ty_apply_type_arguments(me, ty_expr_type(me, &generic->expr), (TyList){ .data = args, .len = len });
        }

        case EXPR_AS_TYPE: {
            AsTypeExpr const* as_type = expr->as_type;
            // (tsc's checkAssertion: the regular literal type, which no longer widens)
            // (Its operand has the contextual type of the assertion: tsc's getContextualType of an AsExpression)
            if (as_type_is_const(as_type)) {
                return ty_regular(me, check_with(me, &as_type->expr, (ExprCtx){ .contextual = ctx.contextual, .is_const = true }));
            }
            Ty const* type = ty_from_node(me, &as_type->type, expr_lower_ctx);
            // The type of `e as T` doesn't depend on e, but checking it checks e right away (tsc's checkAssertionWorker:
            // only the comparison is deferred). A declaration's initializer and control flow don't check it
            // (ty_quick_expr_type(), tsc's getQuickTypeOfExpression): `const data = get(id) as Data` in a loop that
            // assigns id from data isn't circular
            if (!as_type->is_satisfies) {
                if (!my flow_uncached) ty_check_expr(me, &as_type->expr, type);
                return type;
            }
            Ty const* checked = ty_check_expr(me, &as_type->expr, type);
            return type->flags & TYF_ERROR ? type : checked; // (tsc's checkSatisfiesExpressionWorker)
        }

        case EXPR_TERNARY:
        {
            ty_expr_type(me, &expr->ternary->condition);
            // In source order: union members print in the order their types were made
            ExprCtx branch = passed_on_context(ctx);
            Ty const* yes = check_with(me, &expr->ternary->yes, branch);
            Ty const* no = check_with(me, &expr->ternary->no, branch);
            return union_of_two(me, yes, no);
        }

        case EXPR_CALL: return check_call(me, expr->call, expr->inner_span, ctx);

        case EXPR_FUNCTION: {
            // A first check, under a call's resolution, with a contextual type that has no signature for it decides
            // that it has none (tsc's ContextChecked is set either way): `m(() => 1, (z) => () => 2)` with `zero: Z`
            // doesn't take Z's inference (the function itself) as its contextual signature while m is resolved. Its
            // return type only sees the context once the call is resolved (ty_late_contextual_return())
            u64 none_key[2] = { KEY_CONTEXT_NONE, (u64)(uintptr_t)expr->function };
            // A first check while a call infers whose contextual type, instantiated with what it knows so far, has no
            // signature for it (`<TP extends Partial<T> = {}>(p?: TP)`: the default `{}`) decides its return type
            // without one (tsc's getReturnTypeFromBody then reports widening, TS7011, whatever comes later)
            if (ctx.sig_absent && my outer_inference && !expr->function->ret_explicit && !compiler_options()->strict_null_checks &&
                compiler_options()->no_implicit_any && !ty_function_is_context_sensitive(expr->function) &&
                !intern_get(&my links, (u64[]){ KEY_CONTEXT_DONE, (u64)(uintptr_t)expr->function }, 2) &&
                !intern_get(&my links, (u64[]){ KEY_CONTEXT_WIDENING, (u64)(uintptr_t)expr->function }, 2)
            ) {
                Ty const** slot = (Ty const**)intern_slot(me, &my links, (u64[]){ KEY_CONTEXT_WIDENING_ABSENT, (u64)(uintptr_t)expr->function }, 2);
                if (!*slot) *slot = (Ty const*)expr->function;
            }
            CallExpr const* none_call = (ctx.contextual || ctx.sig_contextual) && !ctx.sig_absent ? intern_get(&my links, none_key, 2) : nullptr;
            if (none_call) {
                *intern_slot(me, &my links, (u64[]){ KEY_FUNCTION_CONTEXT, (u64)(uintptr_t)expr->function }, 2) = ctx.contextual;
                *intern_slot(me, &my links, (u64[]){ KEY_FUNCTION_SIG_CONTEXT, (u64)(uintptr_t)expr->function }, 2) =
                    ctx.sig_contextual ? ctx.sig_contextual : ctx.contextual;
            } else if ((ctx.contextual || ctx.sig_contextual) && !ctx.sig_absent) {
                *intern_slot(me, &my links, (u64[]){ KEY_FUNCTION_CONTEXT, (u64)(uintptr_t)expr->function }, 2) = ctx.contextual;
                u64 done_key[2] = { KEY_CONTEXT_DONE, (u64)(uintptr_t)expr->function };
                bool assigned = intern_get(&my links, done_key, 2) != nullptr;
                assign_contextual_signature_as(me, expr->function, ctx.sig_contextual ? ctx.sig_contextual : ctx.contextual, nullptr, nullptr,
                    ctx.raw_contextual);
                // (Only one that isn't context sensitive: tttt checks those in places tsc skips them, or with other
                // contextual types, so their parameters would miss what tsc gives them)
                if (!assigned && my resolving_call && !intern_get(&my links, done_key, 2) &&
                    !ty_function_is_context_sensitive(expr->function)) {
                    *intern_slot(me, &my links, none_key, 2) = my resolving_call;
                }
                if (assigned) {
                    Signature const* sig = contextual_signature(me, ty_instantiate_contextual_type_for(me, ctx.sig_contextual ? ctx.sig_contextual : ctx.contextual, true), expr->function, 0);
                    if (sig) set_contextual_return(me, expr->function, sig);
                }
                // Its return type is decided at its first check with a contextual signature, which may be now, while the
                // call it is an argument of infers (tsc's contextuallyCheckFunctionExpressionOrObjectLiteralMethod)
                // (Not again while it is being decided further up: tsc's ContextChecked flag is set by then, so a call
                // re-entered from its own body, `wrap()` returning `new Proxy(t, { get() { return wrap(...) } })`,
                // doesn't make it circular)
                if (my outer_inference && !expr->function->ret_explicit &&
                    intern_get(&my links, done_key, 2) && !ty_function_return_in_flight(me, expr->function)) {
                    ty_argument_return_type(me, expr->function);
                }
            }
            return ty_function_type(me, expr->function);
        }

        case EXPR_OBJECT: return check_object(me, expr, ctx);
        case EXPR_ARRAY: return check_array(me, expr, ctx);
        case EXPR_CLASS_DECL:
            if (ctx.contextual) contextually_type_static_fields(me, expr->class_decl, ctx.contextual);
            return ty_type_of_symbol(me, expr->class_decl->symbol);

        case EXPR_REGEX: return ty_global_regexp_type(me);
        case EXPR_PRIVATE_NAME: return any_type(me);
        case EXPR_JSX_ELEMENT: case EXPR_JSX_FRAGMENT: return ty_jsx_element_type(me, expr);
        case EXPR_JSX_ATTRIBUTES: return check_jsx_attributes(me, expr, ctx);

        case EXPR_TEMPLATE: {
            if (!expr->template_->tag) return check_template(me, expr, ctx);
            Expr const* tag = expr->template_->tag;
            if (tag->tag == EXPR_GENERIC_PARAMS) tag = &tag->generic_params->expr; // call.c applies the type arguments
            return ty_tagged_template_return_type(me, ty_expr_type(me, tag), expr, ctx.raw_contextual);
        }

        case EXPR_SPREAD: {
            // The type of each spread element: of an array its elements, of anything else what iterating it gives
            Ty const* operand = ty_expr_type(me, expr->spread);
            if (ty_is_array(me, operand) || ty_is_tuple(operand)) return ty_rest_element_type(me, operand);
            return ty_iterated_type(me, ITERATION_SPREAD, operand, span_null, nullptr);
        }
    }
    impossible;
}

// tsc's isTemplateLiteralContextualType
static bool is_template_contextual(Checker* me, Ty const* ty) {
    if (ty->kind == TY_UNION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (is_template_contextual(me, ty->composite.types.data[i])) return true;
        }
        return false;
    }
    if (ty->flags & (TYF_STRING_LITERAL | TYF_TEMPLATE_LITERAL)) return true;
    if (ty->kind == TY_TYPE_PARAM || ty->kind == TY_INDEXED || ty->kind == TY_CONDITIONAL) {
        Ty const* constraint = ty_base_constraint(me, ty);
        if (!constraint) return false;
        TyList parts = constraint->kind == TY_UNION ? constraint->composite.types : (TyList){ .data = &constraint, .len = 1 };
        for (u32 i = 0; i < parts.len; i++) {
            if (parts.data[i]->flags & TYF_STRING_LIKE) return true;
        }
    }
    return false;
}

// tsc's checkTemplateExpression: a constant is a (fresh) string literal, in a const context, an element access or with a
// string literal / template contextual type a template literal type, else string
static Ty const* check_template(Checker* me, Expr const* expr, ExprCtx ctx) {
    TemplateExpr const* template = expr->template_;
    u32 len = (u32)vec_expr_size(&template->substitutions);
    Ty const* allowed = nullptr;
    Ty const** types = malloc(sizeof(Ty const*) * (len ? len : 1));
    panic_if(!types, "out of memory");
    for_each (i, vec_expr, &template->substitutions) {
        // (A substitution of a const context is in it too: tsc's isConstContext goes up through TemplateSpan)
        Ty const* type = ctx.is_const ? check_with(me, i, (ExprCtx){ .is_const = true }) : ty_expr_type(me, i);
        if (!allowed) {
            // tsc's templateConstraintType
            Ty const* parts[6] = {
                ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_BIGINT),
                ty_intrinsic(me, TY_BOOLEAN), ty_intrinsic(me, TY_NULL), ty_intrinsic(me, TY_UNDEFINED),
            };
            allowed = ty_union(me, parts, 6, nullptr, (TyList){0});
        }
        types[i_index] = ty_is_related(me, type, allowed, TY_RELATION_ASSIGNABLE) ? ty_regular(me, type) : ty_intrinsic(me, TY_STRING);
    }
    Ty const* ret = nullptr;
    cstr evaluated = cstr_null;
    if (!template->is_tag && ty_evaluate_string(me, expr, &evaluated)) {
        ret = ty_fresh_literal(me, ty_string_literal(me, cstr_sv(&evaluated)));
        cstr_drop(&evaluated);
    } else if (ctx.is_const || ctx.template_context || (ctx.contextual && is_template_contextual(me, ctx.contextual))) {
        Atom const** texts = malloc(sizeof(Atom const*) * (len + 1));
        panic_if(!texts, "out of memory");
        for_each (i, vec_cstr, &template->texts) texts[i_index] = checker_atom(me, cstr_sv(i));
        ret = ty_template_literal(me, texts, types, len);
        free(texts);
    } else {
        ret = ty_intrinsic(me, TY_STRING);
    }
    free(types);
    return ret;
}

// The type of the index of an element access: a template literal there is typed as a template (tsc's
// isTemplateLiteralContext)
Ty const* ty_element_index_type(Checker* me, Expr const* index) {
    if (index->tag == EXPR_TEMPLATE && !index->template_->tag) return check_with(me, index, (ExprCtx){ .template_context = true });
    return ty_expr_type(me, index);
}

// Whether an index signature keyed by `key` is among a type's own (tsc's getIndexInfoOfType)
static bool has_index_key(Members const* members, Ty const* key) {
    for (u32 i = 0; i < members->index_count; i++) {
        if (members->index[i].key == key) return true;
    }
    return false;
}

// tsc's hasNumericPropertyNames: the only index signature of the type is a number one. A union's index signatures are
// the keys every member has (tsc's getUnionIndexInfos)
static bool has_numeric_property_names(Checker* me, Ty const* type) {
    if (type->kind != TY_UNION) {
        Ty const* object = ty_apparent_type(me, type);
        if (!(object->flags & (TYF_OBJECT | TYF_INTERSECTION))) return false; // (getIndexInfosOfType)
        Members const* members = ty_members(me, object);
        return members->index_count == 1 && (members->index[0].key->flags & TYF_NUMBER);
    }
    TyList types = type->composite.types;
    Members const* first = ty_members(me, ty_apparent_type(me, types.data[0]));
    u32 count = 0;
    bool numeric = false;
    for (u32 i = 0; i < first->index_count; i++) {
        Ty const* key = first->index[i].key;
        bool everywhere = true;
        for (u32 t = 1; t < types.len && everywhere; t++) {
            everywhere = has_index_key(ty_members(me, ty_apparent_type(me, types.data[t])), key);
        }
        if (!everywhere) continue;
        count++;
        numeric = key->flags & TYF_NUMBER;
    }
    return count == 1 && numeric;
}

// tsc's isForInVariableForNumericPropertyNames: `k` of `for (const k in xs)` where xs has only a number index
// signature (an array) indexes as a number, in the loop's body
static bool is_for_in_numeric_variable(Checker* me, PropertyExpr const* access) {
    ForEachLoopStatement const* loop = property_for_in_loop(access);
    return loop && has_numeric_property_names(me, ty_non_nullable(me, ty_expr_type(me, &loop->iterable)));
}

Ty const* ty_access_key_type(Checker* me, PropertyExpr const* access) {
    if (property_private_class(access) && property_private_class(access)->symbol) {
        return ty_prop_name_literal(me, ty_private_name(me, property_private_class(access)->symbol, access->prop.inner_span.sv));
    }
    // (A `#x` no class around declares names nothing, not even what an index signature has)
    Span name = access->prop.inner_span;
    // (A lone `#` is TS1127's, and the members named so are too)
    if (access->form == PROPERTY_EXPR_DOT && access->prop.tag == EXPR_LITERAL && span_valid(name) && name.sv.size > 1 &&
        name.sv.buf[0] == '#'
    ) {
        return ty_prop_name_literal(me, ty_private_name(me, nullptr, name.sv));
    }
    if (access->form == PROPERTY_EXPR_BRACKET && is_for_in_numeric_variable(me, access)) return ty_intrinsic(me, TY_NUMBER);
    return ty_element_index_type(me, &access->prop);
}

// What the LazyType of an expression evaluates to: nobody checked it with a contextual type first
Ty const* ty_compute_expr_without_context(Checker* me, Expr const* expr) {
    return compute(me, expr, (ExprCtx){0});
}
