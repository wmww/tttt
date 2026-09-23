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
// Not here: fresh/regular literal type pairs. Every literal type that comes out of an expression counts as fresh, ie
// it widens at a mutable location (see hacks.md).

static const LowerCtx expr_lower_ctx = { .generic = true };

typedef struct ExprCtx {
    Ty const* contextual; // may be null
    bool is_const; // inside `as const`
} ExprCtx;

static Ty const* compute(Checker* me, Expr const* expr, ExprCtx ctx);

static Ty const* any_type(Checker* me) { return ty_intrinsic(me, TY_ANY); }

// Memo

static bool kind_uses_context(Expr const* expr) {
    switch (expr->tag) {
        case EXPR_OBJECT:
        case EXPR_ARRAY:
        case EXPR_FUNCTION:
        case EXPR_TERNARY:
        case EXPR_CALL:
        case EXPR_AS_TYPE:
            return true;
        case EXPR_UNARY_OP:
            return expr->unary_op->op == UNARY_OP_RIGHT_BANG;
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

static Ty const* check_with(Checker* me, Expr const* expr, ExprCtx ctx) {
    LazyType* lazy = (LazyType*)&expr->lazy_type;
    if (lazy->tag != LAZY_TYPE_EXPR || !lazy->slot) return compute(me, expr, ctx); // implicit any and friends
    LazyMemo* memo = lazy_type_state_peek(lazy);
    if (memo && memo->done && !ctx.is_const) {
        if (!ctx.contextual || memo->context == ctx.contextual || !kind_uses_context(expr)) return memo->evaluated;
    }
    if (!ctx.contextual && !ctx.is_const) return ty_expr_type(me, expr); // through the LazyType, which detects cycles
    Ty const* ret = compute(me, expr, ctx);
    if (my flow_uncached) return ret;
    lazy_type_set(lazy, ret, ctx.contextual);
    return ret;
}

Ty const* ty_check_expr(Checker* me, Expr const* expr, Ty const* contextual) {
    return check_with(me, expr, (ExprCtx){ .contextual = contextual });
}

// Unions and widening

Ty const* ty_union_subtype_reduced(Checker* me, Ty const* const* types, u32 len) {
    if (len < 2) return len ? types[0] : ty_intrinsic(me, TY_NEVER);
    // Flatten first, so members of member unions take part
    Ty const* flat = ty_union(me, types, len, nullptr, (TyList){0});
    if (flat->kind != TY_UNION || (flat->flags & TYF_BOOLEAN)) return flat;
    TyList members = flat->composite.types;
    if (members.len > 64) return flat; // PERF: quadratic
    Ty const** kept = malloc(sizeof(Ty const*) * members.len);
    u32 kept_len = 0;
    for (u32 i = 0; i < members.len; i++) {
        bool redundant = false;
        for (u32 j = 0; j < members.len && !redundant; j++) {
            if (i == j) continue;
            Ty const* a = members.data[i];
            Ty const* b = members.data[j];
            if (!ty_is_related(me, a, b, TY_RELATION_STRICT_SUBTYPE)) continue;
            // Mutual subtypes: the first one stays
            redundant = j < i || !ty_is_related(me, b, a, TY_RELATION_STRICT_SUBTYPE);
        }
        if (!redundant) kept[kept_len++] = members.data[i];
    }
    Ty const* ret = kept_len == members.len ? flat : ty_union(me, kept, kept_len, nullptr, (TyList){0});
    free(kept);
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

// tsc's getWidenedLiteralLikeTypeForContextualType without the context: fresh literals and unique symbols widen, and what
// is left is regular (a mutable location holds regular literals)
static Ty const* widen_literals(Checker* me, Ty const* ty) {
    return ty_regular(me, ty_widened_unique_symbol(me, ty_widened_literal(me, ty)));
}

// Without strictNullChecks `null` and `undefined` widen to `any`
static Ty const* widen_nullable(Checker* me, Ty const* ty) {
    if (compiler_options()->strict_null_checks) return ty;
    if (ty->flags & TYF_NULLABLE) return any_type(me);
    if (ty_is_array(me, ty) && ty->kind == TY_REFERENCE) {
        TyList args = ty_reference_args(me, ty);
        if (args.len == 1 && (args.data[0]->flags & TYF_NULLABLE)) return ty_array(me, any_type(me), false);
    }
    if (ty->kind == TY_UNION && !ty->alias) {
        // A union of nothing but null and undefined
        bool all = true;
        for (u32 i = 0; i < ty->composite.types.len; i++) all &= (ty->composite.types.data[i]->flags & TYF_NULLABLE) != 0;
        if (all) return any_type(me);
    }
    return ty;
}

Ty const* ty_widened_for_declaration(Checker* me, Ty const* ty, bool literals) {
    // Not regularised: `const e = E.x` holds the fresh `E.x`, `let x = a` with `a: "a"` the regular "a"
    if (literals) ty = ty_widened_unique_symbol(me, ty_widened_literal(me, ty));
    return widen_nullable(me, ty_widened_objects(me, ty));
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
        for (u32 i = 0; i < members.len; i++) {
            Ty const* part = context_map(me, members.data[i], get, arg);
            if (part) parts[len++] = part;
        }
        Ty const* ret = len ? ty_union(me, parts, len, nullptr, (TyList){0}) : nullptr;
        free(parts);
        return ret;
    }
    if (is_nullable_or_primitive(contextual)) return nullptr;
    return get(me, contextual, arg);
}

static Ty const* get_prop_context(Checker* me, Ty const* object, void const* arg) {
    return ty_access(me, object, arg);
}

static Ty const* contextual_prop_type(Checker* me, Ty const* contextual, Ty const* key) {
    return context_map(me, contextual, get_prop_context, key);
}

static Ty const* get_element_context(Checker* me, Ty const* object, void const* arg) {
    u32 index = *(u32 const*)arg;
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
    return context_map(me, contextual, get_element_context, &index);
}

static bool context_is_tuple_like(Checker* me, Ty const* contextual, u32 depth) {
    if (!contextual || depth > 8) return false;
    if (ty_is_tuple(contextual)) return true;
    if (contextual->kind == TY_DECLARED && contextual->declared.kind == TY_DECLARED_TUPLE_TARGET) return true;
    if (contextual->kind == TY_UNION || contextual->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < contextual->composite.types.len; i++) {
            if (context_is_tuple_like(me, contextual->composite.types.data[i], depth + 1)) return true;
        }
        return false;
    }
    if (contextual->kind == TY_TYPE_PARAM) {
        Ty const* constraint = ty_param_constraint(me, contextual);
        return constraint && constraint != contextual && context_is_tuple_like(me, constraint, depth + 1);
    }
    return false;
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
    if (contextual->kind == TY_TYPE_PARAM || (contextual->flags & TYF_DEFERRED)) {
        // A type parameter or deferred type: what its constraint has room for. `T extends string` keeps "a"
        Ty const* constraint = ty_base_constraint(me, contextual);
        if (!constraint || constraint == contextual) return false;
        if (constraint->flags & (TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_BOOLEAN | TYF_ES_SYMBOL)) {
            u32 f = literal->flags;
            return ((constraint->flags & TYF_ES_SYMBOL) && (f & TYF_UNIQUE_ES_SYMBOL)) ||
                ((constraint->flags & TYF_STRING) && (f & TYF_STRING_LITERAL)) ||
                ((constraint->flags & TYF_NUMBER) && (f & TYF_NUMBER_LITERAL)) ||
                ((constraint->flags & TYF_BIGINT) && (f & TYF_BIGINT_LITERAL)) ||
                ((constraint->flags & TYF_BOOLEAN) && (f & TYF_BOOLEAN_LITERAL));
        }
        return is_literal_of_contextual_type(me, literal, constraint, depth + 1);
    }
    if (contextual->kind == TY_KEYOF) return (literal->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) != 0;
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

// tsc's checkExpressionForMutableLocation: the type a property or element gets from its initializer
static Ty const* check_for_mutable_location(Checker* me, Expr const* expr, ExprCtx ctx) {
    Ty const* type = check_with(me, expr, ctx);
    if (ctx.is_const || any_literal_of_contextual_type(me, type, ctx.contextual)) return ty_regular(me, type);
    return widen_literals(me, type);
}

Ty const* ty_widened_return_type(Checker* me, Ty const* ty, Ty const* contextual) {
    // Only a unit type widens: a function that returns 1 or "a" says so
    bool unit = ty->kind == TY_LITERAL;
    if (unit) ty = contextual && any_literal_of_contextual_type(me, ty, contextual) ? ty_regular(me, ty) : widen_literals(me, ty);
    return widen_nullable(me, ty_widened_objects(me, ty));
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
        if (i->rest || i->explicit_optional || i->initializer.value.tag != EXPR_IMPLICIT_ANY) break;
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
    TyList parts = type->kind == TY_INTERSECTION ? type->composite.types : (TyList){ .data = &type, .len = 1 };
    Signature const* stack_buf[16];
    Signature const** applicable = stack_buf;
    u32 cap = c_arraylen(stack_buf);
    u32 count = 0;
    for (u32 i = 0; i < parts.len; i++) {
        Ty const* part = type->kind == TY_INTERSECTION ? ty_apparent_type(me, parts.data[i]) : parts.data[i];
        if (!(part->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, part);
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
    if (!(contextual->flags & (TYF_OBJECT | TYF_INTERSECTION))) return nullptr;
    return contextual_call_signature(me, contextual, function);
}

static Ty const* signature_type_at(Checker* me, Signature const* sig, u32 index, bool as_rest) {
    for (u32 i = 0; i < sig->param_count; i++) {
        SigParam const* param = &sig->params[i];
        if (!param->rest) {
            if (i == index && !as_rest) return param->optional ? ty_optional_param_type(me, param->type) : param->type;
            continue;
        }
        if (index < i) return nullptr;
        if (as_rest && i == index) return param->type;
        if (ty_is_tuple(param->type)) {
            Value value = value_number(index - i);
            return ty_access(me, param->type, ty_literal(me, &value));
        }
        if (ty_is_array(me, param->type) || (param->type->flags & TYF_ANY)) return ty_rest_element_type(me, param->type);
        return ty_access(me, param->type, ty_intrinsic(me, TY_NUMBER));
    }
    // A rest parameter where the signature has plain ones: the tuple of those left
    return as_rest ? ty_signature_params_tuple(me, sig, index, nullptr) : nullptr;
}

Ty const* ty_contextual_parameter_type(Checker* me, InitializedBinding const* parameter) {
    u64 key[2] = { KEY_CONTEXT_PARAM, (u64)(uintptr_t)parameter };
    return intern_get(&my links, key, 2);
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

// tsc's getContextualThisParameterType: what `this` is in a function expression or object literal method that doesn't
// declare it. Null: nothing says (it's any)
Ty const* ty_contextual_this_type(Checker* me, FunctionImpl const* function) {
    u64 this_key[2] = { KEY_CONTEXT_THIS, (u64)(uintptr_t)function };
    Ty const* from_signature = intern_get(&my links, this_key, 2);
    if (from_signature) return from_signature;
    if (!compiler_options()->no_implicit_this) return nullptr;
    if (function->this_literal) {
        Expr const* literal = function->this_literal;
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
            if (marker) return marker;
            u64 parent_key[2] = { KEY_LITERAL_PARENT, (u64)(uintptr_t)outer };
            outer = intern_get(&my links, parent_key, 2);
        }
        if (contextual) return ty_widened_for_declaration(me, ty_non_nullable(me, contextual), false);
        return ty_widened_for_declaration(me, ty_expr_type(me, literal), true);
    }
    if (function->this_assigned_object) {
        return ty_widened_for_declaration(me, ty_expr_type(me, function->this_assigned_object), true);
    }
    return nullptr;
}

Ty const* ty_contextual_return_type(Checker* me, FunctionImpl const* function) {
    u64 key[2] = { KEY_CONTEXT_RETURN, (u64)(uintptr_t)function };
    return intern_get(&my links, key, 2);
}

// A function expression meets a contextual type. Its unannotated parameters take their types from the contextual
// signature, once: the first contextual signature decides (tsc's ContextChecked)
static void assign_contextual_signature(Checker* me, FunctionImpl const* function, Ty const* contextual) {
    u64 done_key[2] = { KEY_CONTEXT_DONE, (u64)(uintptr_t)function };
    if (intern_get(&my links, done_key, 2)) return;
    Signature const* sig = contextual_signature(me, contextual, function, 0);
    if (!sig) return;
    *intern_slot(me, &my links, done_key, 2) = sig;
    if (sig->this_type) {
        u64 this_key[2] = { KEY_CONTEXT_THIS, (u64)(uintptr_t)function };
        *intern_slot(me, &my links, this_key, 2) = sig->this_type;
    }
    u32 index = 0;
    for_each (i, vec_function_arg, &function->args) {
        bool is_this = i->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(i->initializer.binding.content_span, "this");
        if (is_this) continue;
        u32 at = index++;
        // A parameter with an initializer takes the contextual type too (tsc's getContextuallyTypedParameterType)
        if (i->initializer.binding.has_explicit_type) continue;
        LazyMemo* param_memo = lazy_type_state_peek((LazyType*)&i->initializer.lazy_type);
        if (param_memo && param_memo->evaluation_started) continue;
        Ty const* type = signature_type_at(me, sig, at, i->rest);
        if (!type) continue;
        if (i->rest && !ty_is_array(me, type) && !ty_is_tuple(type) && !(type->flags & TYF_TYPE_PARAM)) type = ty_array(me, type, false);
        if (i->initializer.value.tag != EXPR_IMPLICIT_ANY) {
            // An initializer that doesn't fit the contextual type but is wider than it wins (tsc's
            // assignContextualParameterTypes): `(max = 3) => {}` against `(max: 1 | 2) => void` takes a number
            Ty const* initializer = ty_expr_type(me, &i->initializer.value);
            if (!ty_is_related(me, initializer, type, TY_RELATION_ASSIGNABLE)) {
                Ty const* widened = ty_widened_for_declaration(me, initializer, true);
                if (ty_is_related(me, type, widened, TY_RELATION_ASSIGNABLE)) type = widened;
            }
        }
        u64 key[2] = { KEY_CONTEXT_PARAM, (u64)(uintptr_t)&i->initializer };
        *intern_slot(me, &my links, key, 2) = type;
    }
    if (!function->ret_explicit) {
        Ty const* ret = signature_return_type(me, sig);
        if (ret && !(ret->flags & (TYF_ANY | TYF_VOID))) {
            u64 key[2] = { KEY_CONTEXT_RETURN, (u64)(uintptr_t)function };
            *intern_slot(me, &my links, key, 2) = ret;
        }
    }
}

bool ty_function_is_context_sensitive(FunctionImpl const* function) {
    if (vec_generic_item_size(&function->generic.items)) return false;
    for_each (i, vec_function_arg, &function->args) {
        if (!i->initializer.binding.has_explicit_type) return true;
    }
    // A function expression's `this` is contextually typed too, unless it declares one (tsc's
    // hasContextSensitiveParameters)
    if (!span_valid(function->arrow_span)) {
        FunctionArg const* first = vec_function_arg_size(&function->args) ? vec_function_arg_at(&function->args, 0) : nullptr;
        if (!(first && first->initializer.binding.tag == BINDING_IDENTIFIER &&
            span_equals(first->initializer.binding.content_span, "this"))
        ) {
            return true;
        }
    }
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
        default:
            return false;
    }
}

// Identifiers

Ty const* ty_identifier_reference_type(Checker* me, IdentifierExpr const* identifier) {
    Symbol const* decl = identifier->target;
    switch (identifier->this_kind) {
        case THIS_NONE: break;
        case THIS_ANY: {
            Ty const* contextual = identifier->this_function ? ty_contextual_this_type(me, identifier->this_function) : nullptr;
            return contextual ? contextual : any_type(me);
        }
        case THIS_PARAMETER: break;
        case THIS_CLASS_STATIC: return ty_static_type_of(me, decl);
        case THIS_INTERFACE: return any_type(me);
        case THIS_GLOBAL: return my binder->global_this ? ty_type_of_symbol(me, my binder->global_this) : any_type(me);
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
    if (!decl && !identifier->keyword && span_equals(identifier->name, "arguments")) {
        Symbol const* arguments = program_get_global(my program, c_sv("IArguments"), SYMBOL_TYPE);
        return arguments ? ty_declared_type_of(me, arguments) : any_type(me);
    }
    if (!decl) {
        // TODO: `this` outside of anything that has one is `typeof globalThis` (or implicitly any)
        return identifier->keyword == KEYWORD_IDENTIFIER_THIS ? ty_intrinsic(me, TY_UNDEFINED) : any_type(me);
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

static Ty const* base_constraint_or_type(Checker* me, Ty const* ty) {
    Ty const* constraint = ty->kind == TY_TYPE_PARAM || (ty->flags & TYF_DEFERRED) ? ty_base_constraint(me, ty) : nullptr;
    return constraint ? constraint : ty;
}

// tsc's getNarrowableTypeForReference: in a constraint position, a generic type with a union constraint is narrowed as
// its constraint (`c && c.flags` with `c: T extends Ctx | undefined` reads `flags` of Ctx)
static Ty const* narrowable_type(Checker* me, IdentifierExpr const* identifier, Ty const* type) {
    if (!identifier->constraint_position) return type;
    Ty const* const* members = &type;
    u32 count = 1;
    if (type->kind == TY_UNION) {
        members = type->composite.types.data;
        count = type->composite.types.len;
    }
    bool some = false;
    for (u32 i = 0; i < count && !some; i++) some = is_generic_with_union_constraint(me, members[i]);
    if (!some) return type;
    if (count == 1) return base_constraint_or_type(me, type);
    Ty const** mapped = malloc(sizeof(Ty const*) * count);
    panic_if(!mapped, "out of memory");
    for (u32 i = 0; i < count; i++) mapped[i] = base_constraint_or_type(me, members[i]);
    Ty const* ret = ty_union(me, mapped, count, nullptr, (TyList){0});
    free(mapped);
    return ret;
}

// The tail of tsc's checkIdentifier(): a variable or parameter has the type control flow gives it where it is read
static Ty const* identifier_flow_type(Checker* me, Expr const* expr, IdentifierFlowIssue* issue) {
    IdentifierExpr const* identifier = &expr->identifier;
    Ty const* type = narrowable_type(me, identifier, ty_identifier_reference_type(me, identifier));
    Symbol const* symbol = identifier->target;
    if (identifier->keyword == KEYWORD_IDENTIFIER_THIS) {
        return identifier->flow ? ty_flow_type(me, expr, type, type, identifier->flow) : type;
    }
    if (!symbol || identifier->keyword || !(symbol->flags & SYMBOL_VARIABLE)) return type;
    if (identifier->assignment_target == FLOW_TARGET_DEFINITE) {
        return identifier->compound_like_target ? base_type_of_literal(me, type) : type;
    }
    if (!identifier->flow) return type;
    Binding const* binding = symbol_find_node(symbol, SYMBOL_DECL_VARIABLE);
    if (type == ty_intrinsic(me, TY_AUTO)) {
        // Undefined until something is assigned. From a closure it is assumed to be assigned, to something control
        // flow may not know: any unless every path leads to an assignment in the closure (tsc's checkIdentifier)
        Ty const* initial = identifier->outer_reference && symbol->is_assigned ? type : ty_intrinsic(me, TY_UNDEFINED);
        Ty const* flow_type = ty_flow_type(me, expr, type, initial, identifier->flow);
        if (flow_type != type) return identifier->assignment_target ? base_type_of_literal(me, flow_type) : flow_type;
        *issue = IDENTIFIER_FLOW_IMPLICIT_ANY;
        return ty_intrinsic(me, TY_ANY);
    }
    // Where nothing says otherwise, a variable is `undefined` until something is assigned to it
    bool assume_initialized = symbol->is_parameter || symbol->is_ambient || !compiler_options()->strict_null_checks ||
        (type->flags & (TYF_ANY | TYF_UNKNOWN | TYF_VOID)) || !binding || binding->definitely_assigned ||
        identifier->assume_initialized ||
        includes_undefined(type);
    Ty const* initial = type;
    if (symbol->is_parameter && binding && binding->has_explicit_type && compiler_options()->strict_null_checks &&
        includes_undefined(type) && binding->source && binding->source->tag == LAZY_TYPE_INITIALIZED_BINDING
    ) {
        // `(o: T | undefined = {})` is a T in the body unless the default can be undefined (tsc's
        // removeOptionalityFromDeclaredType)
        InitializedBinding const* initialized = c_container_of(binding->source, InitializedBinding, lazy_type);
        // (Not from inside the default itself: `(x: number | undefined = x > 0 ? x : 0)`)
        LazyMemo const* memo = lazy_type_state_peek((LazyType*)&initialized->value.lazy_type);
        bool in_flight = memo && memo->evaluation_started && !memo->done;
        Ty const* value = initialized->value.tag == EXPR_IMPLICIT_ANY || in_flight ? nullptr : ty_expr_type(me, &initialized->value);
        if (value && !includes_undefined(value) && !(value->flags & (TYF_ANY | TYF_UNKNOWN | TYF_VOID))) {
            initial = ty_without_undefined(me, type);
        }
    }
    if (!assume_initialized) {
        Ty const* types[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
        initial = ty_union(me, types, 2, nullptr, (TyList){0});
    }
    Ty const* flow_type = ty_flow_type(me, expr, type, initial, identifier->flow);
    if (!assume_initialized && includes_undefined(flow_type)) {
        *issue = IDENTIFIER_FLOW_UNASSIGNED;
        return type;
    }
    return identifier->assignment_target ? base_type_of_literal(me, flow_type) : flow_type;
}

IdentifierFlowIssue ty_identifier_flow_issue(Checker* me, IdentifierExpr const* identifier) {
    IdentifierFlowIssue ret = IDENTIFIER_FLOW_OK;
    identifier_flow_type(me, c_container_of(identifier, Expr, identifier), &ret);
    return ret;
}

// A reference that could have a key (flow.c): names and literal keys down to an identifier or `this`. Unlike the
// builder's expr_is_narrowable_reference() also through `?.`: `typeof a?.b === "string" ? a?.b : ""`
static bool is_narrowable_access(Expr const* expr) {
    while (true) {
        switch (expr->tag) {
            case EXPR_IDENTIFIER:
                return expr->identifier.keyword != KEYWORD_IDENTIFIER_IMPORT;
            case EXPR_PROPERTY: {
                Expr const* key = &expr->property->prop;
                if (key->tag != EXPR_LITERAL && key->tag != EXPR_IDENTIFIER) return false;
                expr = &expr->property->expr;
                break;
            }
            case EXPR_UNARY_OP:
                if (expr->unary_op->op != UNARY_OP_RIGHT_BANG) return false;
                expr = &expr->unary_op->expr;
                break;
            default:
                return false;
        }
    }
}

// tsc's getFlowTypeOfAccessExpression()
static Ty const* access_flow_type(Checker* me, Expr const* expr, Ty const* type) {
    PropertyExpr const* access = expr->property;
    if (access->assignment_target == FLOW_TARGET_DEFINITE && access->compound_like_target) return base_type_of_literal(me, type);
    if (!access->flow || access->assignment_target == FLOW_TARGET_DEFINITE) return type;
    if (!is_narrowable_access(expr)) return type;
    // Methods don't narrow
    if (type->kind == TY_ANONYMOUS && type->anonymous.function && type->anonymous.function->is_method) return type;
    Ty const* flow_type = ty_flow_type(me, expr, type, type, access->flow);
    return access->assignment_target ? base_type_of_literal(me, flow_type) : flow_type;
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

static Ty const* binary_result(Checker* me, Ty const* left, BinaryOp op, Ty const* right) {
    switch (op) {
        case BINARY_OP_PLUS: {
            bool left_string = extends_strict_nulls(me, left, TY_STRING);
            bool right_string = extends_strict_nulls(me, right, TY_STRING);
            if (left_string || right_string) {
                bool known = (left_string && !(left->flags & TYF_ANY)) || (right_string && !(right->flags & TYF_ANY));
                return known ? ty_intrinsic(me, TY_STRING) : any_type(me);
            }
            if (extends_strict_nulls(me, left, TY_NUMBER) && extends_strict_nulls(me, right, TY_NUMBER)) {
                return ty_intrinsic(me, TY_NUMBER);
            }
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
            return ty_intrinsic(me, TY_NUMBER);

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
            if (!ty_can_be_truthy(me, left)) return left;
            if (!compiler_options()->strict_null_checks) return right;
            return union_of_two(me, definitely_falsy_part(me, left), right);

        case BINARY_OP_OR_OR: {
            if (!ty_can_be_falsy(me, left)) return left;
            Ty const* types[2] = { remove_definitely_falsy(me, left), right };

            return ty_union_subtype_reduced(me, types, 2);
        }

        case BINARY_OP_EH_EH: {
            if (!ty_can_be_nullish(me, left)) return left;
            Ty const* types[2] = { ty_non_nullable(me, left), right };
            return ty_union_subtype_reduced(me, types, 2);
        }

        case BINARY_OP_EQUAL:
        case BINARY_OP_COMMA:
            // TODO: TS2695: Left side of comma operator is unused and has no side effects.
            return right;

        default:
            // Compound assignment: the type of what gets assigned
            return binary_result(me, left, without_assignment(op), right);
    }
}

static Ty const* check_binary(Checker* me, BinaryOpExpr const* binary, ExprCtx ctx) {
    Ty const* left;
    Ty const* right;
    switch (binary->op) {
        case BINARY_OP_EQUAL:
            // The target contextually types what is assigned to it
            left = ty_expr_type(me, &binary->left);
            right = ty_check_expr(me, &binary->right, left->flags & TYF_ANY ? nullptr : left);
            break;
        case BINARY_OP_OR_OR:
        case BINARY_OP_EH_EH:
            left = ty_check_expr(me, &binary->left, ctx.contextual);
            right = ty_check_expr(me, &binary->right, ctx.contextual ? ctx.contextual : left);
            break;
        case BINARY_OP_AND_AND:
        case BINARY_OP_COMMA:
            left = ty_expr_type(me, &binary->left);
            right = ty_check_expr(me, &binary->right, ctx.contextual);
            break;
        default:
            left = ty_expr_type(me, &binary->left);
            right = ty_expr_type(me, &binary->right);
            break;
    }
    return binary_result(me, left, binary->op, right);
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
            static char const* const names[] = { "string", "number", "bigint", "boolean", "symbol", "undefined", "object", "function" };
            Ty const* types[c_arraylen(names)];
            for (u32 i = 0; i < c_arraylen(names); i++) types[i] = ty_string_literal(me, csview_from(names[i]));
            return ty_union(me, types, c_arraylen(names), nullptr, (TyList){0});
        }
        case UNARY_OP_VOID: return ty_intrinsic(me, TY_UNDEFINED);
        case UNARY_OP_TILDE: return ty_intrinsic(me, TY_NUMBER);
        case UNARY_OP_DELETE: return ty_intrinsic(me, TY_BOOLEAN);
        case UNARY_OP_AWAIT: return ty_awaited(me, ty_expr_type(me, operand));

        case UNARY_OP_YIELD:
        case UNARY_OP_YIELD_STAR:
            impossible; // check_yield()

        case UNARY_OP_PLUS:
        case UNARY_OP_MINUS:
            if (!expr_is_perenthisized(operand) && operand->tag == EXPR_LITERAL && operand->literal.tag == VALUE_NUMBER) {
                Value value = value_number(unary->op == UNARY_OP_PLUS ? operand->literal.number : -operand->literal.number);
                return ty_fresh_literal(me, ty_literal(me, &value));
            }
            return ty_intrinsic(me, TY_NUMBER);

        case UNARY_OP_LEFT_PLUS_PLUS:
        case UNARY_OP_LEFT_MINUS_MINUS:
        case UNARY_OP_RIGHT_PLUS_PLUS:
        case UNARY_OP_RIGHT_MINUS_MINUS:
            return ty_intrinsic(me, TY_NUMBER);

        case UNARY_OP_RIGHT_BANG:
            return ty_non_nullable(me, ty_expr_type(me, operand));
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
} LiteralBuilder;

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

// tsc's discriminateContextualTypeByObjectMembers: the members of a union contextual type that the literal's
// discriminant properties fit
static Ty const* discriminate_by_object_members(Checker* me, Ty const* contextual, ObjectExpr const* object) {
    TyList types = contextual->composite.types;
    if (types.len > 64) return contextual;
    // 0: excluded, 1: maybe, 3: included
    unsigned char include[64];
    u32 const primitive = TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_ES_SYMBOL_LIKE | TYF_BOOLEAN | TYF_LITERAL | TYF_ENUM |
        TYF_NULLABLE | TYF_VOID;
    for (u32 i = 0; i < types.len; i++) include[i] = types.data[i]->flags & primitive ? 0 : 3;
    for_each (item, vec_object_expr_item, object) {
        bool candidate = item->tag == OBJECT_ITEM_SHORTHAND ||
            (item->tag == OBJECT_ITEM_PROPERTY && is_possibly_discriminant_value(&item->value));
        if (!candidate || item->computed_key) continue;
        Atom const* name = checker_atom(me, cstr_sv(&item->name_string));
        if (!ty_is_discriminant_property(me, contextual, name)) continue;
        Ty const* value = ty_expr_type(me, &item->value);
        Ty const* key = ty_prop_name_literal(me, name);
        bool matched = false;
        for (u32 i = 0; i < types.len; i++) {
            if (!include[i]) continue;
            Ty const* target = ty_access(me, types.data[i], key);
            bool related = false;
            if (target) {
                Ty const* const* parts = &value;
                u32 count = 1;
                if (value->kind == TY_UNION) {
                    parts = value->composite.types.data;
                    count = value->composite.types.len;
                }
                for (u32 j = 0; j < count && !related; j++) related = ty_is_related(me, parts[j], target, TY_RELATION_ASSIGNABLE);
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
    return ty_union(me, kept, len, nullptr, (TyList){0});
}

static Ty const* check_object(Checker* me, Expr const* expr, ExprCtx ctx) {
    ObjectExpr const* object = &expr->object;
    if (ctx.contextual && ctx.contextual->kind == TY_UNION) {
        ctx.contextual = discriminate_by_object_members(me, ctx.contextual, object);
    }
    if (ctx.contextual) {
        // What `this` in its methods goes by (ty_contextual_this_type())
        u64 context_key[2] = { KEY_LITERAL_CONTEXT, (u64)(uintptr_t)expr };
        *intern_slot(me, &my links, context_key, 2) = ctx.contextual;
    }
    for_each (i, vec_object_expr_item, object) {
        if (i->tag == OBJECT_ITEM_PROPERTY && i->value.tag == EXPR_OBJECT) {
            u64 parent_key[2] = { KEY_LITERAL_PARENT, (u64)(uintptr_t)&i->value };
            *intern_slot(me, &my links, parent_key, 2) = expr;
        }
    }
    LiteralBuilder builder = {0};
    bool spread_any = false;
    // Accessor pairs share a property: the declarations of both halves
    Span* accessor_spans = nullptr;

    for_each (i, vec_object_expr_item, object) {
        if (i->tag == OBJECT_ITEM_SPREAD) {
            Ty const* spread = ty_apparent_type(me, ty_expr_type(me, &i->value));
            if (spread->flags & TYF_ANY) spread_any = true; // The whole literal is any (tsc's getSpreadType)
            if (spread->flags & TYF_OBJECT) {
                Members const* members = ty_members(me, spread);
                for (u32 j = 0; j < members->prop_count; j++) {
                    Prop const* prop = members->props[j];
                    Prop const* origin = prop;
                    while (origin->target) origin = origin->target;
                    ObjectLiteralProp* existing = literal_builder_find(&builder, prop->name);
                    if (existing && (prop->flags & PROP_OPTIONAL)) {
                        // An optional property may not be there: what was there before stays possible, and so does
                        // its being required (tsc's getSpreadType)
                        Ty const* left = existing->type;
                        Ty const* right = ty_without_undefined(me, prop_type(me, prop));
                        if (ty_without_undefined(me, left) != right) {
                            existing->type = union_of_two(me, left, right);
                            existing->asserted = nullptr;
                        }
                        u32 count = existing->decl_span_count + origin->decl_span_count;
                        Span* spans = arena_new_n(&my arena, Span, count ? count : 1);
                        for (u32 k = 0; k < existing->decl_span_count; k++) spans[k] = existing->decl_spans[k];
                        for (u32 k = 0; k < origin->decl_span_count; k++) spans[existing->decl_span_count + k] = origin->decl_spans[k];
                        existing->decl_spans = spans;
                        existing->decl_span_count = count;
                        continue;
                    }
                    // The annotation of the property it comes from still says what it is, unless instantiated
                    TypeNode const* annotation = origin->node && origin->node->parsed && !origin->lazy && !origin->function &&
                        !origin->overload_count && !origin->symbol && prop_type(me, prop) == prop_type(me, origin) ?
                        origin->node : nullptr;
                    literal_builder_add(&builder, (ObjectLiteralProp){
                        .name = prop->name,
                        .flags = prop->flags & (u32)~PROP_READONLY,
                        .type = prop_type(me, prop),
                        .asserted = annotation,
                        .name_span = origin->name_span,
                        .decl_spans = origin->decl_spans,
                        .decl_span_count = origin->decl_span_count,
                        .name_is_value = origin->name_is_value,
                    });
                }
            }
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
        if (ctx.contextual) {
            Ty const* key = name ? ty_prop_name_literal(me, name) : key_type;
            if (key) member_context = contextual_prop_type(me, ctx.contextual, key);
        }
        ExprCtx member_ctx = { .contextual = member_context, .is_const = ctx.is_const };
        Ty const* type = nullptr;
        bool is_accessor = i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER;
        switch (i->tag) {
            case OBJECT_ITEM_GETTER:
            case OBJECT_ITEM_SETTER:
                // Typed when asked (PROP_GETTER/PROP_SETTER), unless it's an index signature's
                if (!name) type = ty_object_literal_accessor_type(me, i->value.function, i->tag == OBJECT_ITEM_GETTER);
                break;
            case OBJECT_ITEM_METHOD:
                type = check_with(me, &i->value, member_ctx);
                break;
            default:
                type = check_for_mutable_location(me, &i->value, member_ctx);
                break;
        }

        if (!name) {
            // A key that's only known to be some string, number or symbol becomes an index signature. TODO: unions of
            // literals, for now the member is left out of the type
            if (!(key_type->flags & (TYF_STRING | TYF_NUMBER | TYF_ES_SYMBOL | TYF_ANY))) continue;
            Ty const* index_key = ty_intrinsic(me,
                key_type->flags & TYF_NUMBER ? TY_NUMBER : key_type->flags & TYF_ES_SYMBOL ? TY_ES_SYMBOL : TY_STRING);
            IndexInfo* existing = nullptr;
            for (u32 j = 0; j < builder.index_count; j++) {
                if (builder.index[j].key == index_key) existing = &builder.index[j];
            }
            if (existing) {
                existing->value = union_of_two(me, existing->value, type);
            } else {
                builder.index[builder.index_count++] = (IndexInfo){
                    .key = index_key, .value = type, .param_name = checker_atom(me, c_sv("x")),
                };
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
            if (i->tag == OBJECT_ITEM_GETTER) {
                existing->accessor = i->value.function;
                existing->flags = (existing->flags & ~(u32)PROP_SETTER) | PROP_GETTER;
            }
            continue;
        }
        TypeNode const* asserted = nullptr;
        if (i->tag == OBJECT_ITEM_PROPERTY && i->value.tag == EXPR_AS_TYPE && !i->value.as_type->is_satisfies &&
            !as_type_is_const(i->value.as_type) && type == ty_expr_type(me, &i->value)
        ) {
            asserted = &i->value.as_type->type;
        }
        Span* decl_span = arena_new(&my arena, Span);
        *decl_span = i->span;
        u32 flags = 0;
        if (i->tag == OBJECT_ITEM_METHOD) flags |= PROP_METHOD;
        if (i->tag == OBJECT_ITEM_GETTER) flags |= PROP_GETTER;
        if (i->tag == OBJECT_ITEM_SETTER) flags |= PROP_SETTER;
        if (ctx.is_const) flags |= PROP_READONLY;
        literal_builder_add(&builder, (ObjectLiteralProp){
            .name = name,
            .flags = flags,
            .type = type,
            .accessor = is_accessor ? i->value.function : nullptr,
            .name_span = i->name_span,
            .decl_spans = decl_span,
            .decl_span_count = 1,
            .name_is_value = name_is_value,
            .asserted = asserted,
            .own = true,
        });
    }
    free(accessor_spans);
    Ty const* ret = spread_any ? ty_intrinsic(me, TY_ANY) :
        ty_object_literal(me, builder.props, builder.count, builder.index, builder.index_count, OBJECT_LITERAL_FRESH);
    free(builder.props);
    return ret;
}

Ty const* ty_object_literal_accessor_type(Checker* me, FunctionImpl const* accessor, bool getter) {
    Signature const* sig = single_call_signature(me, ty_function_type(me, accessor));
    if (!sig) return any_type(me);
    if (getter) return widen_literals(me, signature_return_type(me, sig));
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
    if (prop) return prop_type(me, prop);
    // A computed key that made no property
    Ty const* value = ty_expr_type(me, &item->value);
    if (item->tag == OBJECT_ITEM_GETTER || item->tag == OBJECT_ITEM_SETTER) {
        Signature const* sig = single_call_signature(me, value);
        if (!sig) return any_type(me);
        if (item->tag == OBJECT_ITEM_GETTER) return widen_literals(me, signature_return_type(me, sig));
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
    bool as_tuple = ctx.is_const || context_is_tuple_like(me, ctx.contextual, 0);
    Ty const** elements = malloc(sizeof(Ty const*) * (len ? len : 1));
    unsigned char* flags = calloc(len ? len : 1, 1);
    u32 count = 0;
    for_each (i, vec_expr, &expr->array) {
        Ty const* element_context = contextual_element_type(me, ctx.contextual, (u32)i_index);
        ExprCtx element_ctx = { .contextual = element_context, .is_const = ctx.is_const };
        if (i->tag == EXPR_SPREAD) {
            Ty const* operand = ty_expr_type(me, i->spread);
            if (as_tuple) {
                flags[count] = TUPLE_ELEMENT_REST;
                elements[count++] = ty_is_array(me, operand) || ty_is_tuple(operand) ?
                    operand : ty_array(me, ty_iterated_type(me, ITERATION_SPREAD, operand, span_null, nullptr), false);
            } else {
                elements[count++] = ty_expr_type(me, i);
            }
            continue;
        }
        elements[count++] = as_tuple ? check_for_mutable_location(me, i, element_ctx) : check_for_mutable_location(me, i, element_ctx);
    }

    Ty const* ret;
    if (as_tuple) {
        ret = tuple_of(me, elements, flags, count, ctx.is_const);
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
            element = elements[0];
        } else {
            // `[]` (an unannotated variable initialized with it is an evolving array in tsc, see binding.c)
            element = compiler_options()->strict_null_checks ? ty_intrinsic(me, TY_IMPLICIT_NEVER) : any_type(me);
        }
        ret = ty_array(me, element, false);
    }
    free(elements);
    free(flags);
    return ret;
}

// The rest

bool as_type_is_const(AsTypeExpr const* as_type) {
    TypeNode const* type = &as_type->type;
    return type->tag == TYPE_IDENTIFIER && !type->identifier->target && span_equals(type->identifier->name, "const") &&
        vec_span_is_empty(&type->identifier->namespaces);
}

// `Bar<string>` as a value: the object type with the type arguments applied to its generic signatures. Returns the
// type itself if the mapper isn't about any of them
static Ty const* apply_type_arguments_mapper(Checker* me, Ty const* ty, TyMapper const* mapper) {
    Members const* members = ty_members(me, ty);
    bool relevant = false;
    for (u32 pass = 0; pass < 2 && !relevant; pass++) {
        u32 sig_count = pass ? members->construct_count : members->call_count;
        Signature* const* sigs = pass ? members->construct : members->call;
        for (u32 i = 0; i < sig_count && !relevant; i++) {
            signature_resolve(me, sigs[i]);
            for (u32 j = 0; j < sigs[i]->type_params.len; j++) {
                for (u32 k = 0; k < mapper->len; k++) relevant |= mapper->sources[k] == sigs[i]->type_params.data[j];
            }
        }
    }
    if (!relevant) return ty;
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

Ty const* ty_apply_type_arguments(Checker* me, Ty const* ty, TyList args) {
    Members const* members = ty_members(me, ty_apparent_type(me, ty));
    Ty const* sources[64];
    Ty const* targets[64];
    u32 len = 0;
    for (u32 pass = 0; pass < 2; pass++) {
        u32 sig_count = pass ? members->construct_count : members->call_count;
        Signature* const* sigs = pass ? members->construct : members->call;
        for (u32 i = 0; i < sig_count; i++) {
            signature_resolve(me, sigs[i]);
            TyList params = sigs[i]->type_params;
            if (!params.len || params.len < args.len) continue;
            for (u32 j = 0; j < params.len && len < c_arraylen(sources); j++) {
                bool seen = false;
                for (u32 k = 0; k < len; k++) seen |= sources[k] == params.data[j];
                if (seen) continue;
                Ty const* target = j < args.len ? args.data[j] : ty_param_default(me, params.data[j]);
                if (!target) target = ty_intrinsic(me, TY_UNKNOWN_);
                sources[len] = params.data[j];
                targets[len++] = target;
            }
        }
    }
    if (!len) return ty;
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len });
    return apply_type_arguments_mapper(me, ty, mapper);
}

// The type of the object of `object?.x` / `object.x` (or the callee of a call) as the access sees it. `optional`: the
// access is part of an optional chain that may have ended before it, so what it evaluates to may be undefined.
// HACK: tsc tells the undefined of a chain that ended from an undefined the object really has (a marker type); here
// every undefined of an object that continues a chain is taken for the former, so `a?.b.c` with an optional b passes
static Ty const* chain_object_type(Checker* me, Expr const* object_expr, bool is_root, bool* optional) {
    Ty const* object = ty_expr_type(me, object_expr);
    if (!compiler_options()->strict_null_checks) return is_root ? ty_non_nullable(me, object) : object;
    if (is_root) {
        *optional = ty_can_be_nullish(me, object) && !(object->flags & (TYF_ANY | TYF_UNKNOWN));
        return ty_non_nullable(me, object);
    }
    if (expr_is_optional_chain(object_expr) && !expr_is_perenthisized(object_expr) && includes_undefined(object)) {
        *optional = true;
        return ty_non_nullable(me, object);
    }
    return object;
}

// `Symbol()` or `Symbol.for()` with the global `Symbol` (tsc's isSymbolOrSymbolForCall)
static bool is_symbol_call(Checker* me, CallExpr const* call) {
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_PROPERTY && span_equals(callee->property->prop.inner_span, "for")) callee = &callee->property->expr;
    if (callee->tag != EXPR_IDENTIFIER || !csview_equals(callee->identifier.name.sv, "Symbol")) return false;
    Symbol const* global = program_get_global(my program, c_sv("Symbol"), SYMBOL_VALUE);
    return global && binder_redirect(my binder, callee->identifier.target) == global;
}

static Ty const* check_call(Checker* me, CallExpr const* call, ExprCtx ctx) {
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr; // call.c applies the type arguments
    bool optional = false;
    Ty const* callee_type = chain_object_type(me, callee, call->optional_chain, &optional);
    Ty const* ret = ty_call_return_type(me, callee_type, call, ctx.contextual);
    // The initializer of a `const` (or a `static readonly` property) makes a new unique symbol
    if (call->unique_symbol.key && (ret->flags & TYF_ES_SYMBOL_LIKE) && is_symbol_call(me, call)) {
        ret = ty_unique_symbol(me, &call->unique_symbol);
    }
    return optional ? optional_type(me, ret) : ret;
}

// What a `yield` evaluates to: what the generator is sent (its TNext), for `yield*` what the delegate returns (tsc's
// checkYieldExpression). The operand is checked with the yield type as its contextual type, a `yield*` operand with a
// generator that yields that, returns what the `yield*` itself is expected to be and is sent the same
static Ty const* check_yield(Checker* me, UnaryOpExpr const* yield, ExprCtx ctx) {
    FunctionImpl const* generator = yield->generator;
    Expr const* operand = &yield->expr;
    if (!generator) {
        if (operand->tag != EXPR_IMPLICIT_ANY) ty_expr_type(me, operand);
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

static Ty const* compute(Checker* me, Expr const* expr, ExprCtx ctx) {
    switch (expr->tag) {
        case EXPR_IMPLICIT_ANY: return any_type(me);
        case EXPR_LITERAL: return ty_fresh_literal(me, ty_literal(me, &expr->literal));
        case EXPR_IDENTIFIER: {
            IdentifierFlowIssue issue = IDENTIFIER_FLOW_OK; // Reported by the typecheck walk
            return identifier_flow_type(me, expr, &issue);
        }
        case EXPR_BINARY_OP: return check_binary(me, expr->binary_op, ctx);
        case EXPR_UNARY_OP:
            if (expr->unary_op->op == UNARY_OP_YIELD || expr->unary_op->op == UNARY_OP_YIELD_STAR) {
                return check_yield(me, expr->unary_op, ctx);
            }
            // `x!` has the contextual type of its operand
            if (expr->unary_op->op == UNARY_OP_RIGHT_BANG && ctx.contextual) {
                return ty_non_nullable(me, check_with(me, &expr->unary_op->expr, (ExprCtx){ .contextual = ctx.contextual }));
            }
            return check_unary(me, expr->unary_op);

        case EXPR_PROPERTY: {
            bool optional = false;
            Ty const* object = chain_object_type(me, &expr->property->expr, expr->property->optional_chain, &optional);
            Ty const* ret = ty_access(me, object, ty_expr_type(me, &expr->property->prop));
            if (!ret) return any_type(me);
            ret = access_flow_type(me, expr, ret);
            return optional ? optional_type(me, ret) : ret;
        }

        case EXPR_GENERIC_PARAMS: {
            // An instantiation expression. As the callee of a call it never gets here, see check_call()
            GenericParamsExpr const* generic = expr->generic_params;
            Ty const* args[32];
            u32 len = 0;
            for_each (i, vec_spanned_type, &generic->params) {
                if (len < c_arraylen(args)) args[len++] = ty_from_node(me, &i->type, expr_lower_ctx);
            }
            return ty_apply_type_arguments(me, ty_expr_type(me, &generic->expr), (TyList){ .data = args, .len = len });
        }

        case EXPR_AS_TYPE: {
            AsTypeExpr const* as_type = expr->as_type;
            if (as_type_is_const(as_type)) return check_with(me, &as_type->expr, (ExprCtx){ .is_const = true });
            Ty const* type = ty_from_node(me, &as_type->type, expr_lower_ctx);
            // The type of `e as T` doesn't depend on e, which is checked when the statement walk gets there (tsc defers
            // it too): `const data = get(id) as Data` in a loop that assigns id from data isn't circular
            if (!as_type->is_satisfies) return type;
            return ty_check_expr(me, &as_type->expr, type->flags & TYF_ANY ? nullptr : type);
        }

        case EXPR_TERNARY:
        {
            ty_expr_type(me, &expr->ternary->condition);
            // In source order: union members print in the order their types were made
            Ty const* yes = check_with(me, &expr->ternary->yes, ctx);
            Ty const* no = check_with(me, &expr->ternary->no, ctx);
            return union_of_two(me, yes, no);
        }

        case EXPR_CALL: return check_call(me, expr->call, ctx);

        case EXPR_FUNCTION: {
            if (ctx.contextual) assign_contextual_signature(me, expr->function, ctx.contextual);
            return ty_function_type(me, expr->function);
        }

        case EXPR_OBJECT: return check_object(me, expr, ctx);
        case EXPR_ARRAY: return check_array(me, expr, ctx);
        case EXPR_CLASS_DECL: return ty_type_of_symbol(me, expr->class_decl->symbol);

        case EXPR_REGEX: return ty_global_regexp_type(me);

        case EXPR_TEMPLATE: {
            if (!expr->template_->tag) return ty_intrinsic(me, TY_STRING);
            // TODO: a tagged template is a call (overloads, generics, argument checks). For now only the return type
            // of a plain function is used
            Signature const* sig = single_call_signature(me, ty_apparent_type(me, ty_expr_type(me, expr->template_->tag)));
            return sig && !sig->type_params.len ? signature_return_type(me, sig) : any_type(me);
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

// What the LazyType of an expression evaluates to: nobody checked it with a contextual type first
Ty const* ty_compute_expr_without_context(Checker* me, Expr const* expr) {
    return compute(me, expr, (ExprCtx){0});
}
