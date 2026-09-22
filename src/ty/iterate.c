#include "ty/ty_internal.h"
#include "program.h"
#include "scope.h"
#include "compiler_options.h"
#include "error.h"

// The iteration protocol: what for-of, spread, array destructuring and `yield*` get out of a value, tsc's
// getIterationTypesOfIterable() and getIteratedTypeOrElementType(). A type is iterable through its `[Symbol.iterator]()`
// method (`[Symbol.asyncIterator]()` for async iteration), whose return type's `next()` returns an `IteratorResult`:
// the `value` of the results that aren't `done` is the yield type. References to the global iterable and iterator
// interfaces (`Iterable<T>`, `Generator<T, R, N>`, `ArrayIterator<T>`...) take their type arguments directly, which is
// also what keeps it cheap. Results are cached per type.

typedef enum IterationGlobal {
    IG_ITERABLE,
    IG_ITERATOR_OBJECT,
    IG_ITERABLE_ITERATOR,
    IG_GENERATOR,
    IG_ITERATOR,
    IG_ARRAY_ITERATOR,
    IG_MAP_ITERATOR,
    IG_SET_ITERATOR,
    IG_STRING_ITERATOR,
    IG_ASYNC_ITERABLE,
    IG_ASYNC_ITERATOR_OBJECT,
    IG_ASYNC_ITERABLE_ITERATOR,
    IG_ASYNC_GENERATOR,
    IG_ASYNC_ITERATOR,
    IG_READABLE_STREAM_ASYNC_ITERATOR,
    IG_ITERATOR_YIELD_RESULT,
    IG_ITERATOR_RETURN_RESULT,
    IG_COUNT,
} IterationGlobal;

static char const* const iteration_global_names[IG_COUNT] = {
    "Iterable", "IteratorObject", "IterableIterator", "Generator", "Iterator", "ArrayIterator", "MapIterator",
    "SetIterator", "StringIterator", "AsyncIterable", "AsyncIteratorObject", "AsyncIterableIterator", "AsyncGenerator",
    "AsyncIterator", "ReadableStreamAsyncIterator", "IteratorYieldResult", "IteratorReturnResult",
};

typedef struct IterationState {
    Ty const* globals[IG_COUNT]; // The generic declared types, null if the lib doesn't have them
    Atom const* iterator_name; // The property name of `Symbol.iterator`, null if there's no such unique symbol
    Atom const* async_iterator_name;
} IterationState;

// The iteration types of "doesn't iterate": a distinct pointer, like tsc's noIterationTypes
static TyIterationTypes const no_iteration_types;

static Atom const* well_known_symbol_name(Checker* me, char const* name) {
    Symbol const* symbol = program_get_global(my program, c_sv("Symbol"), SYMBOL_VALUE);
    if (!symbol) return nullptr;
    Ty const* key = ty_access(me, ty_type_of_symbol(me, symbol), ty_string_literal(me, csview_from(name)));
    return key && (key->flags & TYF_UNIQUE_ES_SYMBOL) ? key->literal.string : nullptr;
}

static IterationState const* iteration_state(Checker* me) {
    if (my iteration) return my iteration;
    IterationState* state = arena_new(&my arena, IterationState);
    for (u32 i = 0; i < IG_COUNT; i++) {
        Symbol const* decl = program_get_global(my program, csview_from(iteration_global_names[i]), SYMBOL_TYPE);
        if (!decl || !(decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS))) continue;
        Ty const* declared = ty_declared_type_of(me, decl);
        if (declared->kind == TY_DECLARED && declared->declared.type_params.len) state->globals[i] = declared;
    }
    state->iterator_name = well_known_symbol_name(me, "iterator");
    state->async_iterator_name = well_known_symbol_name(me, "asyncIterator");
    my iteration = state;
    return state;
}

static bool is_reference_to(IterationState const* state, Ty const* ty, IterationGlobal which) {
    return state->globals[which] && ty->kind == TY_REFERENCE && ty->reference.target == state->globals[which];
}

static TyIterationTypes const* make_types(Checker* me, Ty const* yield_type, Ty const* return_type, Ty const* next_type) {
    TyIterationTypes* ret = arena_new(&my arena, TyIterationTypes);
    ret->yield_type = yield_type ? yield_type : ty_intrinsic(me, TY_NEVER);
    ret->return_type = return_type ? return_type : ty_intrinsic(me, TY_NEVER);
    ret->next_type = next_type ? next_type : ty_intrinsic(me, TY_UNKNOWN_);
    return ret;
}

static TyIterationTypes const* any_types(Checker* me) {
    Ty const* any = ty_intrinsic(me, TY_ANY);
    return make_types(me, any, any, any);
}

// tsc's combineIterationTypes: unions of yield and return types, intersection of next types
static TyIterationTypes const* combine(Checker* me, TyIterationTypes const* const* list, u32 count) {
    TyBuf yields = {0}, return_parts = {0}, nexts = {0};
    for (u32 i = 0; i < count; i++) {
        if (!list[i] || list[i] == &no_iteration_types) continue;
        if ((list[i]->yield_type->flags & TYF_ANY) && (list[i]->next_type->flags & TYF_ANY)) {
            ty_buf_drop(&yields), ty_buf_drop(&return_parts), ty_buf_drop(&nexts);
            return list[i];
        }
        ty_buf_push(&yields, list[i]->yield_type);
        ty_buf_push(&return_parts, list[i]->return_type);
        ty_buf_push(&nexts, list[i]->next_type);
    }
    TyIterationTypes const* ret = &no_iteration_types;
    if (yields.len) {
        ret = make_types(me,
            ty_union(me, yields.data, yields.len, nullptr, (TyList){0}),
            ty_union(me, return_parts.data, return_parts.len, nullptr, (TyList){0}),
            ty_intersection(me, nexts.data, nexts.len, nullptr, (TyList){0})
        );
    }
    ty_buf_drop(&yields), ty_buf_drop(&return_parts), ty_buf_drop(&nexts);
    return ret;
}

typedef enum IterationCache {
    CACHE_ITERABLE,
    CACHE_ASYNC_ITERABLE,
    CACHE_ITERATOR,
    CACHE_ASYNC_ITERATOR,
    CACHE_ITERATOR_RESULT,
} IterationCache;

static TyIterationTypes const** cache_slot(Checker* me, Ty const* ty, IterationCache which) {
    u64 key[3] = { KEY_ITERATION_TYPES, ty->id, which };
    return (TyIterationTypes const**)intern_slot(me, &my links, key, 3);
}

static TyIterationTypes const* cached(Checker* me, Ty const* ty, IterationCache which) {
    u64 key[3] = { KEY_ITERATION_TYPES, ty->id, which };
    return intern_get(&my links, key, 3);
}

static TyIterationTypes const* remember(Checker* me, Ty const* ty, IterationCache which, TyIterationTypes const* types) {
    *cache_slot(me, ty, which) = types;
    return types;
}

// tsc's getBuiltinIteratorReturnType: the TReturn of ArrayIterator<T> and friends
static Ty const* builtin_iterator_return(Checker* me) {
    return ty_intrinsic(me, compiler_options()->strict_builtin_iterator_return ? TY_UNDEFINED : TY_ANY);
}

// A property as getPropertyOfType() finds it: through the apparent type. `optional` is set for optional ones
static Ty const* property_type(Checker* me, Ty const* ty, Atom const* name, bool* optional, Prop const** prop_out) {
    *optional = false;
    if (prop_out) *prop_out = nullptr;
    if (!name) return nullptr;
    Ty const* apparent = ty_apparent_type(me, ty);
    if (apparent->flags & TYF_OBJECT) {
        Prop const* prop = ty_get_object_prop(me, apparent, name);
        if (!prop) return nullptr;
        *optional = (prop->flags & PROP_OPTIONAL) != 0;
        if (prop_out) *prop_out = prop;
        return prop_type(me, prop);
    }
    return ty_access(me, apparent, ty_prop_name_literal(me, name));
}

// The call signatures of a method's type; an intersection has those of all its members
typedef struct SigList {
    Signature const* inline_data[8];
    Signature const** data;
    u32 len, cap;
} SigList;

static void call_signatures(Checker* me, Ty const* ty, SigList* out) {
    if (!out->data) {
        out->data = out->inline_data;
        out->cap = c_arraylen(out->inline_data);
    }
    TyList parts = ty->kind == TY_INTERSECTION ? ty->composite.types : (TyList){ .data = &ty, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        Ty const* part = ty_apparent_type(me, parts.data[i]);
        if (!(part->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, part);
        for (u32 j = 0; j < members->call_count; j++) {
            if (out->len == out->cap) {
                out->cap *= 2;
                Signature const** grown = malloc(sizeof(Signature const*) * out->cap);
                panic_if(!grown, "out of memory");
                memcpy(grown, out->data, sizeof(Signature const*) * out->len);
                if (out->data != out->inline_data) free(out->data);
                out->data = grown;
            }
            out->data[out->len++] = members->call[j];
        }
    }
}

static void sig_list_drop(SigList* me) {
    if (my data != my inline_data) free(my data);
}

// Iterator results

static bool is_iterator_result(Checker* me, Ty const* ty, bool yield) {
    bool optional;
    Ty const* done = property_type(me, ty, checker_atom(me, c_sv("done")), &optional, nullptr);
    if (!done) done = ty_intrinsic(me, TY_FALSE);
    return ty_is_related(me, ty_intrinsic(me, yield ? TY_FALSE : TY_TRUE), done, TY_RELATION_ASSIGNABLE);
}

static Ty const* value_of(Checker* me, TyList members, bool yield) {
    TyBuf values = {0};
    Atom const* value = checker_atom(me, c_sv("value"));
    bool missing = false;
    for (u32 i = 0; i < members.len; i++) {
        if (!is_iterator_result(me, members.data[i], yield)) continue;
        bool optional;
        Ty const* type = property_type(me, members.data[i], value, &optional, nullptr);
        if (!type) missing = true; else ty_buf_push(&values, type);
    }
    Ty const* ret = values.len && !missing ? ty_union(me, values.data, values.len, nullptr, (TyList){0}) : nullptr;
    ty_buf_drop(&values);
    return ret;
}

// tsc's getIterationTypesOfIteratorResult: the value of what isn't `done` is yielded, of what is, returned
static TyIterationTypes const* iteration_types_of_result(Checker* me, Ty const* ty) {
    if (ty->flags & TYF_ANY) return any_types(me);
    TyIterationTypes const* found = cached(me, ty, CACHE_ITERATOR_RESULT);
    if (found) return found;
    IterationState const* state = iteration_state(me);
    if (is_reference_to(state, ty, IG_ITERATOR_YIELD_RESULT)) {
        return remember(me, ty, CACHE_ITERATOR_RESULT, make_types(me, ty_reference_args(me, ty).data[0], nullptr, nullptr));
    }
    if (is_reference_to(state, ty, IG_ITERATOR_RETURN_RESULT)) {
        return remember(me, ty, CACHE_ITERATOR_RESULT, make_types(me, nullptr, ty_reference_args(me, ty).data[0], nullptr));
    }
    TyList members = ty->kind == TY_UNION ? ty->composite.types : (TyList){ .data = &ty, .len = 1 };
    Ty const* yield_type = value_of(me, members, true);
    Ty const* return_type = value_of(me, members, false);
    if (!yield_type && !return_type) return remember(me, ty, CACHE_ITERATOR_RESULT, &no_iteration_types);
    return remember(me, ty, CACHE_ITERATOR_RESULT,
        make_types(me, yield_type, return_type ? return_type : ty_intrinsic(me, TY_VOID), nullptr));
}

// Iterators

// The type a type parameter of the global interface `owner` has at a property inherited from it: the mappers along the
// chain of instantiations, innermost first
static Ty const* param_at_prop(Checker* me, Prop const* prop, Ty const* owner, u32 index) {
    TyMapper const* mappers[32];
    u32 count = 0;
    for (; prop->target && count < c_arraylen(mappers); prop = prop->target) mappers[count++] = prop->mapper;
    Ty const* ret = owner->declared.type_params.data[index];
    while (count--) ret = ty_instantiate(me, ret, mappers[count]);
    return ret;
}

typedef enum IteratorMethod { METHOD_NEXT, METHOD_RETURN, METHOD_THROW } IteratorMethod;

// tsc's getIterationTypesOfMethod
static TyIterationTypes const* iteration_types_of_method(Checker* me, Ty const* ty, bool async, IteratorMethod which) {
    static char const* const names[] = { "next", "return", "throw" };
    bool optional;
    Prop const* prop;
    Ty const* method = property_type(me, ty, checker_atom(me, csview_from(names[which])), &optional, &prop);
    if (!method && which != METHOD_NEXT) return nullptr; // `return` and `throw` are optional
    if (method && which == METHOD_NEXT && optional) method = nullptr;
    if (method && which != METHOD_NEXT) method = ty_non_nullable(me, method);
    if (method && (method->flags & TYF_ANY)) return any_types(me);
    SigList sigs = {0};
    if (method) call_signatures(me, method, &sigs);
    if (!sigs.len) {
        sig_list_drop(&sigs);
        return which == METHOD_NEXT ? &no_iteration_types : nullptr;
    }

    // Declared by the global Iterator or Generator itself: its type arguments, which don't have the `undefined` of the
    // optional parameters in them
    IterationState const* state = iteration_state(me);
    if (prop && prop->parent && sigs.len == 1 && method->kind != TY_INTERSECTION) {
        Ty const* owner = nullptr;
        Ty const* generator = state->globals[async ? IG_ASYNC_GENERATOR : IG_GENERATOR];
        Ty const* iterator = state->globals[async ? IG_ASYNC_ITERATOR : IG_ITERATOR];
        if (generator && prop->parent == generator->declared.symbol) owner = generator;
        else if (iterator && prop->parent == iterator->declared.symbol) owner = iterator;
        if (owner && owner->declared.type_params.len == 3) {
            sig_list_drop(&sigs);
            return make_types(me,
                param_at_prop(me, prop, owner, 0),
                param_at_prop(me, prop, owner, 1),
                which == METHOD_NEXT ? param_at_prop(me, prop, owner, 2) : nullptr
            );
        }
    }

    TyBuf params = {0}, return_parts = {0};
    for (u32 i = 0; i < sigs.len; i++) {
        Signature const* sig = sigs.data[i];
        signature_resolve(me, sig);
        if (which != METHOD_THROW && sig->param_count) {
            Ty const* param = sig->params[0].type;
            if (sig->params[0].rest) {
                Ty const* element = ty_access(me, param, ty_literal(me, &(Value){ .tag = VALUE_NUMBER, .number = 0 }));
                param = element ? element : ty_intrinsic(me, TY_ANY);
            }
            ty_buf_push(&params, param);
        }
        ty_buf_push(&return_parts, signature_return_type(me, sig));
    }
    Ty const* next_type = nullptr;
    TyBuf return_types = {0};
    if (which != METHOD_THROW) {
        Ty const* param = params.len ? ty_union(me, params.data, params.len, nullptr, (TyList){0}) : ty_intrinsic(me, TY_UNKNOWN_);
        if (which == METHOD_NEXT) {
            next_type = param;
        } else {
            ty_buf_push(&return_types, async ? ty_awaited(me, param) : param);
        }
    }
    Ty const* result = ty_intersection(me, return_parts.data, return_parts.len, nullptr, (TyList){0});
    if (async) result = ty_awaited(me, result);
    TyIterationTypes const* from_result = iteration_types_of_result(me, result);
    Ty const* yield_type;
    if (from_result == &no_iteration_types) {
        yield_type = ty_intrinsic(me, TY_ANY);
        ty_buf_push(&return_types, ty_intrinsic(me, TY_ANY));
    } else {
        yield_type = from_result->yield_type;
        ty_buf_push(&return_types, from_result->return_type);
    }
    TyIterationTypes const* ret = make_types(me, yield_type,
        ty_union(me, return_types.data, return_types.len, nullptr, (TyList){0}), next_type);
    ty_buf_drop(&params), ty_buf_drop(&return_parts), ty_buf_drop(&return_types);
    sig_list_drop(&sigs);
    return ret;
}

// tsc's getIterationTypesOfIterator: null if it isn't one
TyIterationTypes const* ty_iteration_types_of_iterator(Checker* me, Ty const* ty, bool async) {
    if (ty->flags & TYF_ANY) return any_types(me);
    IterationCache which = async ? CACHE_ASYNC_ITERATOR : CACHE_ITERATOR;
    TyIterationTypes const* found = cached(me, ty, which);
    if (!found) {
        IterationState const* state = iteration_state(me);
        if (is_reference_to(state, ty, async ? IG_ASYNC_ITERABLE_ITERATOR : IG_ITERABLE_ITERATOR) ||
            is_reference_to(state, ty, async ? IG_ASYNC_ITERATOR : IG_ITERATOR) ||
            is_reference_to(state, ty, async ? IG_ASYNC_ITERATOR_OBJECT : IG_ITERATOR_OBJECT) ||
            is_reference_to(state, ty, async ? IG_ASYNC_GENERATOR : IG_GENERATOR)
        ) {
            TyList args = ty_reference_args(me, ty);
            found = make_types(me, args.data[0], args.data[1], args.data[2]);
        } else if (!async && (is_reference_to(state, ty, IG_ARRAY_ITERATOR) || is_reference_to(state, ty, IG_MAP_ITERATOR) ||
            is_reference_to(state, ty, IG_SET_ITERATOR) || is_reference_to(state, ty, IG_STRING_ITERATOR))
        ) {
            found = make_types(me, ty_reference_args(me, ty).data[0], builtin_iterator_return(me), ty_intrinsic(me, TY_UNKNOWN_));
        } else if (async && is_reference_to(state, ty, IG_READABLE_STREAM_ASYNC_ITERATOR)) {
            found = make_types(me, ty_reference_args(me, ty).data[0], builtin_iterator_return(me), ty_intrinsic(me, TY_UNKNOWN_));
        } else {
            TyIterationTypes const* methods[3] = {
                iteration_types_of_method(me, ty, async, METHOD_NEXT),
                iteration_types_of_method(me, ty, async, METHOD_RETURN),
                iteration_types_of_method(me, ty, async, METHOD_THROW),
            };
            found = combine(me, methods, 3);
        }
        remember(me, ty, which, found);
    }
    return found == &no_iteration_types ? nullptr : found;
}

// Iterables

static TyIterationTypes const* resolve_types(Checker* me, TyIterationTypes const* types, bool async) {
    if (!async) return types;
    return make_types(me, ty_awaited(me, types->yield_type), ty_awaited(me, types->return_type), types->next_type);
}

// tsc's getIterationTypesOfIterableFast + Slow for one of the resolvers
static TyIterationTypes const* iterable_types_with(Checker* me, Ty const* ty, bool async) {
    IterationCache which = async ? CACHE_ASYNC_ITERABLE : CACHE_ITERABLE;
    TyIterationTypes const* found = cached(me, ty, which);
    if (found) return found;
    IterationState const* state = iteration_state(me);
    if (is_reference_to(state, ty, async ? IG_ASYNC_ITERABLE : IG_ITERABLE) ||
        is_reference_to(state, ty, async ? IG_ASYNC_ITERATOR_OBJECT : IG_ITERATOR_OBJECT) ||
        is_reference_to(state, ty, async ? IG_ASYNC_ITERABLE_ITERATOR : IG_ITERABLE_ITERATOR) ||
        is_reference_to(state, ty, async ? IG_ASYNC_GENERATOR : IG_GENERATOR)
    ) {
        TyList args = ty_reference_args(me, ty);
        return remember(me, ty, which, resolve_types(me, make_types(me, args.data[0], args.data[1], args.data[2]), async));
    }
    if ((!async && (is_reference_to(state, ty, IG_ARRAY_ITERATOR) || is_reference_to(state, ty, IG_MAP_ITERATOR) ||
            is_reference_to(state, ty, IG_SET_ITERATOR) || is_reference_to(state, ty, IG_STRING_ITERATOR))) ||
        (async && is_reference_to(state, ty, IG_READABLE_STREAM_ASYNC_ITERATOR))
    ) {
        TyIterationTypes const* types =
            make_types(me, ty_reference_args(me, ty).data[0], builtin_iterator_return(me), ty_intrinsic(me, TY_UNKNOWN_));
        return remember(me, ty, which, resolve_types(me, types, async));
    }

    // The `[Symbol.iterator]()` method, its callable signatures' return types (intersected) as the iterator
    bool optional;
    Ty const* method = property_type(me, ty, async ? state->async_iterator_name : state->iterator_name, &optional, nullptr);
    if (method && optional) method = nullptr;
    if (method && (method->flags & TYF_ANY)) return remember(me, ty, which, any_types(me));
    SigList sigs = {0};
    if (method) call_signatures(me, method, &sigs);
    TyBuf iterators = {0};
    for (u32 i = 0; i < sigs.len; i++) {
        signature_resolve(me, sigs.data[i]);
        if (sigs.data[i]->min_args == 0) ty_buf_push(&iterators, signature_return_type(me, sigs.data[i]));
    }
    sig_list_drop(&sigs);
    if (!iterators.len) {
        ty_buf_drop(&iterators);
        return remember(me, ty, which, &no_iteration_types);
    }
    // In flight: an iterable whose iterator is itself (`[Symbol.iterator](): this` on an IterableIterator)
    remember(me, ty, which, &no_iteration_types);
    Ty const* iterator = ty_intersection(me, iterators.data, iterators.len, nullptr, (TyList){0});
    ty_buf_drop(&iterators);
    TyIterationTypes const* types = ty_iteration_types_of_iterator(me, iterator, async);
    return remember(me, ty, which, types ? types : &no_iteration_types);
}

// tsc's getIterationTypesOfIterableWorker, for a type that isn't a union
static TyIterationTypes const* iterable_types_of(Checker* me, Ty const* ty, u32 use) {
    if (ty->flags & TYF_ANY) return any_types(me);
    if (use & ITERATION_ALLOWS_ASYNC) {
        TyIterationTypes const* types = iterable_types_with(me, ty, true);
        if (types != &no_iteration_types) return types;
    }
    if (use & ITERATION_ALLOWS_SYNC) {
        TyIterationTypes const* types = iterable_types_with(me, ty, false);
        if (types != &no_iteration_types) return (use & ITERATION_ALLOWS_ASYNC) ? resolve_types(me, types, true) : types;
    }
    return &no_iteration_types;
}

TyIterationTypes const* ty_iteration_types_of_iterable(Checker* me, Ty const* ty, u32 use) {
    if (ty->flags & TYF_ANY) return any_types(me);
    if (ty->kind != TY_UNION) {
        TyIterationTypes const* types = iterable_types_of(me, ty, use);
        return types == &no_iteration_types ? nullptr : types;
    }
    TyIterationTypes const* stack[16];
    TyIterationTypes const** all = ty->composite.types.len <= c_arraylen(stack) ?
        stack : malloc(sizeof(TyIterationTypes const*) * ty->composite.types.len);
    TyIterationTypes const* ret = nullptr;
    u32 count = 0;
    bool every = true;
    for (u32 i = 0; i < ty->composite.types.len && every; i++) {
        TyIterationTypes const* types = iterable_types_of(me, ty->composite.types.data[i], use);
        if (types == &no_iteration_types) every = false; else all[count++] = types;
    }
    if (every) {
        ret = combine(me, all, count);
        if (ret == &no_iteration_types) ret = nullptr;
    }
    if (all != stack) free(all);
    return ret;
}

// Consumers

static bool is_string_like(Ty const* ty) {
    return (ty->flags & (TYF_STRING | TYF_STRING_LITERAL)) != 0;
}

// tsc's isES2015OrLaterIterable: lib types that are array-like at ES5 but iterate only from ES2015 on
static bool is_es2015_iterable_name(Ty const* ty) {
    static char const* const names[] = {
        "Float32Array", "Float64Array", "Int16Array", "Int32Array", "Int8Array", "NodeList", "Uint16Array", "Uint32Array",
        "Uint8Array", "Uint8ClampedArray",
    };
    Ty const* declared = ty->kind == TY_REFERENCE ? ty->reference.target : ty;
    if (declared->kind != TY_DECLARED || !declared->declared.symbol) return false;
    for (u32 i = 0; i < c_arraylen(names); i++) {
        if (csview_equals(declared->declared.symbol->name, names[i])) return true;
    }
    return false;
}

Ty const* ty_iterated_type(Checker* me, u32 use, Ty const* input, Span span, ErrorCtx* errs) {
    if (input->flags & TYF_ANY) return input;
    bool async = (use & ITERATION_ALLOWS_ASYNC) != 0;
    if (input == ty_intrinsic(me, TY_NEVER)) {
        if (errs) {
            error_ctx_error_d(errs, span, async ? 2504 : 2488, "Type '", input, async ?
                "' must have a '[Symbol.asyncIterator]()' method that returns an async iterator." :
                "' must have a '[Symbol.iterator]()' method that returns an iterator.");
        }
        return ty_intrinsic(me, TY_ANY);
    }
    ScriptTarget target = compiler_options()->target;
    bool uplevel = target >= SCRIPT_TARGET_ES2015;
    bool downlevel = !uplevel && compiler_options()->downlevel_iteration;
    if (uplevel || downlevel || async) {
        TyIterationTypes const* types = ty_iteration_types_of_iterable(me, input, use);
        if (!types && uplevel && errs) {
            error_ctx_error_d(errs, span, async ? 2504 : 2488, "Type '", input, async ?
                "' must have a '[Symbol.asyncIterator]()' method that returns an async iterator." :
                "' must have a '[Symbol.iterator]()' method that returns an iterator.");
        }
        if (types) return types->yield_type;
        if (uplevel) return ty_intrinsic(me, TY_ANY);
    }

    // Below ES2015 only arrays (and strings, where they are allowed) iterate
    Ty const* array = input;
    bool has_string = false;
    if (use & ITERATION_ALLOWS_STRING) {
        if (input->kind == TY_UNION && !(input->flags & TYF_BOOLEAN)) {
            TyBuf rest = {0};
            for (u32 i = 0; i < input->composite.types.len; i++) {
                if (!is_string_like(input->composite.types.data[i])) ty_buf_push(&rest, input->composite.types.data[i]);
            }
            if (rest.len != input->composite.types.len) array = ty_union_subtype_reduced(me, rest.data, rest.len);
            ty_buf_drop(&rest);
        } else if (is_string_like(input)) {
            array = ty_intrinsic(me, TY_NEVER);
        }
        has_string = array != input;
        if (has_string && (array->flags & TYF_NEVER)) return ty_intrinsic(me, TY_STRING);
    }
    if (!ty_extends_array(me, array)) {
        if (errs) {
            bool allows_strings = (use & ITERATION_ALLOWS_STRING) && !has_string;
            if (downlevel) {
                error_ctx_error_d(errs, span, allows_strings ? 2549 : 2548, "Type '", array, allows_strings ?
                    "' is not an array type or a string type or does not have a '[Symbol.iterator]()' method that returns an iterator." :
                    "' is not an array type or does not have a '[Symbol.iterator]()' method that returns an iterator.");
            } else if (ty_iteration_types_of_iterable(me, input, use) || is_es2015_iterable_name(input)) {
                error_ctx_error_d(errs, span, 2802, "Type '", array,
                    "' can only be iterated through when using the '--downlevelIteration' flag or with a '--target' of 'es2015' or higher.");
            } else if (allows_strings) {
                error_ctx_error_d(errs, span, 2495, "Type '", array, "' is not an array type or a string type.");
            } else {
                error_ctx_error_d(errs, span, 2461, "Type '", array, "' is not an array type.");
            }
        }
        return has_string ? ty_intrinsic(me, TY_STRING) : ty_intrinsic(me, TY_ANY);
    }
    Ty const* element = ty_access(me, array, ty_intrinsic(me, TY_NUMBER));
    if (!element) element = ty_intrinsic(me, TY_ANY);
    if (has_string) {
        if (is_string_like(element)) return ty_intrinsic(me, TY_STRING);
        Ty const* parts[2] = { element, ty_intrinsic(me, TY_STRING) };
        return ty_union_subtype_reduced(me, parts, 2);
    }
    return element;
}

// tsc prints `Iterable<T>` rather than `Iterable<T, any, any>` for these, which used to have one type parameter.
// HACK: tsc still prints what was written if all type arguments were
bool ty_elides_default_type_args(Checker* me, Ty const* target) {
    if (target->declared.type_params.len != 3) return false;
    IterationState const* state = iteration_state(me);
    return target == state->globals[IG_ITERABLE] || target == state->globals[IG_ITERABLE_ITERATOR] ||
        target == state->globals[IG_ASYNC_ITERABLE] || target == state->globals[IG_ASYNC_ITERABLE_ITERATOR];
}

// Generators

// tsc's getIterationTypesOfGeneratorFunctionReturnType: what a generator's return type says it yields, returns and is
// sent. Null if it says nothing
TyIterationTypes const* ty_generator_return_iteration_types(Checker* me, Ty const* ret, bool async) {
    if (ret->flags & TYF_ANY) return any_types(me);
    TyIterationTypes const* types =
        ty_iteration_types_of_iterable(me, ret, async ? ITERATION_ASYNC_GENERATOR_RETURN : ITERATION_GENERATOR_RETURN);
    return types ? types : ty_iteration_types_of_iterator(me, ret, async);
}

// What a generator's `yield`s and `return`s are about: its annotation, else its contextual return type. Null if neither
// says anything
TyIterationTypes const* ty_generator_context(Checker* me, FunctionImpl const* generator) {
    Ty const* ret = generator->ret_explicit ?
        ty_from_node(me, &generator->ret, (LowerCtx){ .generic = true }) : ty_contextual_return_type(me, generator);
    if (!ret || (ret->flags & TYF_ANY)) return nullptr;
    return ty_generator_return_iteration_types(me, ret, generator->is_async);
}

// tsc's createGeneratorType: `Generator<Y, R, N>` (`AsyncGenerator`), else `IterableIterator` of old libs, else `{}`
Ty const* ty_generator_type(Checker* me, bool async, Ty const* yield_type, Ty const* return_type, Ty const* next_type) {
    IterationState const* state = iteration_state(me);
    if (async) {
        yield_type = ty_awaited(me, yield_type);
        return_type = ty_awaited(me, return_type);
    }
    Ty const* args[3] = { yield_type, return_type, next_type };
    Ty const* target = state->globals[async ? IG_ASYNC_GENERATOR : IG_GENERATOR];
    if (!target || target->declared.type_params.len != 3) target = state->globals[async ? IG_ASYNC_ITERABLE_ITERATOR : IG_ITERABLE_ITERATOR];
    if (!target) return ty_empty_object_type(me);
    u32 count = target->declared.type_params.len < 3 ? target->declared.type_params.len : 3;
    return ty_reference(me, target, (TyList){ .data = args, .len = count });
}
