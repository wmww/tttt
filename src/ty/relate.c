#include "ty/ty_internal.h"
#include "program.h"
#include "scope.h"
#include "error.h"

// Type relations (assignable, comparable) over Ty. The structure follows tsc's isTypeRelatedTo -> isRelatedTo ->
// recursiveTypeRelatedTo -> structuredTypeRelatedTo, because what's observable (which errors, which elaboration lines)
// depends on it:
// - results are cached per relation by (source id, target id), for the life of the checker
// - a pair that is being compared further up the stack is assumed related ("maybe"); such results only become
//   definitive when the comparison that assumed them finishes
// - a type whose recursion identity (generic target) is on the stack 3 times with growing ids is "deeply nested", and
//   when both sides are, the pair is assumed related. That is what terminates infinitely expanding generics
// - same-target references compare their type arguments by the measured variance of the type parameters
// Explaining a failure is a second run with `reporting` set, which skips cached failures to get at the reasons.

typedef enum Ternary {
    TERNARY_FALSE = 0,
    TERNARY_MAYBE = 1,
    TERNARY_TRUE = 3,
} Ternary;

enum {
    CACHE_SUCCEEDED = 1,
    CACHE_FAILED = 2,
};

#define MAX_RELATION_DEPTH 100

typedef enum GlobalType {
    GLOBAL_STRING,
    GLOBAL_NUMBER,
    GLOBAL_BOOLEAN,
    GLOBAL_SYMBOL,
    GLOBAL_BIGINT,
    GLOBAL_FUNCTION,
    GLOBAL_OBJECT,
    GLOBAL_REGEXP,
    GLOBAL_EMPTY_OBJECT, // Not a global: the `{}` type
} GlobalType;

typedef struct RelationState {
    TyRelation relation;
    bool reporting;
    VecTyReportLine lines; // innermost first while being collected
    u32 override_next_head; // A more specific message stands in for the next "Type S is not assignable to type T"
    u64* maybe_keys;
    u32 maybe_count, maybe_cap;
    Ty const* source_stack[MAX_RELATION_DEPTH];
    Ty const* target_stack[MAX_RELATION_DEPTH];
    u32 depth;
    unsigned expanding; // 1: source is deeply nested, 2: target is
    bool overflow;
    struct RelationState* outer;
} RelationState;

static Ternary is_related_to(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report);

// Relation cache

void relation_cache_drop(RelationCache* me) {
    free(my keys);
    free(my values);
    *me = (RelationCache){0};
}

static u32 relation_cache_index(RelationCache const* me, u64 key) {
    u64 h = key * 0x9e3779b97f4a7c15ull;
    u32 mask = my cap - 1;
    u32 i = (u32)(h >> 32) & mask;
    while (my keys[i] && my keys[i] != key) i = (i + 1) & mask;
    return i;
}

static unsigned relation_cache_get(RelationCache const* me, u64 key) {
    if (!my cap) return 0;
    u32 i = relation_cache_index(me, key);
    return my keys[i] ? my values[i] : 0;
}

static void relation_cache_put(RelationCache* me, u64 key, unsigned value) {
    if ((my len + 1) * 4 > my cap * 3) {
        RelationCache bigger = { .cap = my cap ? my cap * 2 : 256 };
        bigger.keys = calloc(bigger.cap, sizeof(u64));
        bigger.values = calloc(bigger.cap, 1);
        panic_if(!bigger.keys || !bigger.values, "out of memory");
        for (u32 i = 0; i < my cap; i++) {
            if (my keys[i]) {
                u32 j = relation_cache_index(&bigger, my keys[i]);
                bigger.keys[j] = my keys[i];
                bigger.values[j] = my values[i];
                bigger.len++;
            }
        }
        relation_cache_drop(me);
        *me = bigger;
    }
    u32 i = relation_cache_index(me, key);
    if (!my keys[i]) {
        my keys[i] = key;
        my len++;
    }
    my values[i] = (unsigned char)value;
}

static u64 relation_key(Ty const* source, Ty const* target) {
    return (u64)source->id << 32 | target->id; // ids start at 1, so a key is never 0
}

// Globals

static Ty const* global_type(Checker* me, GlobalType which) {
    static char const* const names[] = { "String", "Number", "Boolean", "Symbol", "BigInt", "Function", "Object", "RegExp" };
    if (!my global_types_resolved[which]) {
        my global_types_resolved[which] = true;
        if (which == GLOBAL_EMPTY_OBJECT) {
            Ty* empty = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT);
            empty->anonymous.state = arena_new(&my arena, TyObjectState);
            empty->anonymous.state->members = arena_new(&my arena, Members);
            empty->anonymous.state->members_state = LAZY_STATE_DONE;
            my global_types[which] = empty;
        } else {
            Symbol const* decl = program_get_global(my program, csview_from(names[which]), SYMBOL_TYPE);
            if (decl && decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) {
                Ty const* declared = ty_declared_type_of(me, decl);
                if (declared->kind == TY_DECLARED && !declared->declared.type_params.len) {
                    my global_types[which] = declared;
                }
            }
        }
    }
    return my global_types[which];
}

Ty const* ty_global_function_type(Checker* me) { return global_type(me, GLOBAL_FUNCTION); }
Ty const* ty_global_object_type(Checker* me) { return global_type(me, GLOBAL_OBJECT); }
Ty const* ty_empty_object_type(Checker* me) { return global_type(me, GLOBAL_EMPTY_OBJECT); }

// The type of a regular expression literal
Ty const* ty_global_regexp_type(Checker* me) {
    Ty const* regexp = global_type(me, GLOBAL_REGEXP);
    return regexp ? regexp : ty_intrinsic(me, TY_ANY);
}

static bool is_array_target(Checker* me, Ty const* target) {
    return target == my array_target || target == my readonly_array_target;
}

static bool is_tuple(Ty const* ty) {
    return ty->kind == TY_REFERENCE && ty->reference.target->declared.kind == TY_DECLARED_TUPLE_TARGET;
}

static bool is_empty_tuple(Ty const* ty) {
    return ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_TUPLE_TARGET;
}

// Literals

Ty const* ty_widened_literal(Checker* me, Ty const* ty) {
    if (ty->kind == TY_LITERAL) {
        if (ty->flags & TYF_ENUM_LITERAL) return ty_enum_of_member(me, ty);
        if (ty->flags & TYF_STRING_LITERAL) return ty_intrinsic(me, TY_STRING);
        if (ty->flags & TYF_NUMBER_LITERAL) return ty_intrinsic(me, TY_NUMBER);
        if (ty->flags & TYF_BIGINT_LITERAL) return ty_intrinsic(me, TY_BIGINT);
        if (ty->flags & TYF_BOOLEAN_LITERAL) return ty_intrinsic(me, TY_BOOLEAN);
    }
    if (ty->kind == TY_UNION && !(ty->flags & TYF_BOOLEAN)) {
        bool any_literal = false;
        for (u32 i = 0; i < ty->composite.types.len; i++) any_literal |= ty->composite.types.data[i]->kind == TY_LITERAL;
        if (any_literal) {
            u32 len = ty->composite.types.len;
            Ty const** widened = malloc(sizeof(Ty const*) * len);
            // Members of one enum all widen to the same (possibly huge) union: once is enough
            u32 count = 0;
            for (u32 i = 0; i < len; i++) {
                Ty const* part = ty_widened_literal(me, ty->composite.types.data[i]);
                if (!count || widened[count - 1] != part) widened[count++] = part;
            }
            Ty const* ret = ty_union(me, widened, count, nullptr, (TyList){0});
            free(widened);
            return ret;
        }
    }
    return ty;
}

// tsc's typeCouldHaveTopLevelSingletonTypes: if so, a literal source is shown as it is
static bool could_have_singletons(Checker* me, Ty const* ty) {
    if (ty->flags & TYF_BOOLEAN) return false;
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (could_have_singletons(me, ty->composite.types.data[i])) return true;
        }
        return false;
    }
    if (ty->kind == TY_TYPE_PARAM) {
        Ty const* constraint = ty_param_constraint(me, ty);
        return constraint && constraint != ty && could_have_singletons(me, constraint);
    }
    return ty->kind == TY_LITERAL || (ty->flags & (TYF_DEFERRED | TYF_UNDEFINED | TYF_NULL)) != 0;
}

// Enums

static csview unqualified(csview name) {
    isize_t dot = name.size;
    while (dot > 0 && name.buf[dot - 1] != '.') dot--;
    return csview_from_n(name.buf + dot, name.size - dot);
}

// tsc's isEnumTypeRelatedTo: the same enum, or enums of the same name whose members agree (two versions of a library)
static bool enums_related(Checker* me, Symbol const* source, Symbol const* target) {
    if (source == target) return true;
    if (!source || !target || !csview_eq(&source->name, &target->name)) return false;
    if (!(source->flags & target->flags & SYMBOL_ENUM)) return false;
    Ty const* source_type = ty_declared_type_of(me, source);
    TyList members = source_type->kind == TY_UNION ?
        source_type->composite.types :
        (TyList){ .data = &source_type, .len = 1 };
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = members.data[i];
        if (member->kind != TY_LITERAL || !member->literal.enum_member || member->literal.enum_member == source) continue;
        Symbol const* other = symbol_get_export(target, member->literal.enum_member->name);
        if (!other || !(other->flags & SYMBOL_ENUM_MEMBER)) return false;
        Ty const* other_type = ty_declared_type_of(me, other);
        if (other_type->kind != TY_LITERAL) return false;
        bool known = (member->flags & TYF_LITERAL) && (other_type->flags & TYF_LITERAL);
        if (known) {
            if ((member->flags & TYF_LITERAL) != (other_type->flags & TYF_LITERAL)) return false;
            if ((member->flags & TYF_STRING_LITERAL) && member->literal.string != other_type->literal.string) return false;
            if ((member->flags & TYF_NUMBER_LITERAL) && member->literal.value.number != other_type->literal.value.number) {
                return false;
            }
        } else if ((member->flags | other_type->flags) & TYF_STRING_LITERAL) {
            return false; // An unknown value is assumed to be numeric
        }
    }
    return true;
}

static bool same_literal_value(Ty const* s, Ty const* t) {
    if ((s->flags & TYF_LITERAL) != (t->flags & TYF_LITERAL)) return false;
    if (s->flags & TYF_STRING_LITERAL) return s->literal.string == t->literal.string;
    if (s->flags & TYF_NUMBER_LITERAL) {
        return s->literal.value.number == t->literal.value.number ||
            (s->literal.value.number != s->literal.value.number && t->literal.value.number != t->literal.value.number);
    }
    return false;
}

// Simple relations: decided by flags and identity alone (tsc's isSimpleTypeRelatedTo)

static bool is_simple_related(Checker* me, RelationState const* st, Ty const* s, Ty const* t) {
    u32 sf = s->flags;
    u32 tf = t->flags;
    if ((tf & (TYF_ANY | TYF_UNKNOWN)) || (sf & TYF_NEVER)) return true;
    if (tf & TYF_NEVER) return false;
    if (sf & TYF_ANY) return st->relation != TY_RELATION_STRICT_SUBTYPE;
    if ((sf & TYF_STRING_LITERAL) && (tf & TYF_STRING)) return true;
    if ((sf & TYF_NUMBER_LITERAL) && (tf & TYF_NUMBER)) return true;
    if ((sf & TYF_BIGINT_LITERAL) && (tf & TYF_BIGINT)) return true;
    if ((sf & TYF_BOOLEAN_LITERAL) && (tf & TYF_BOOLEAN)) return true;
    if ((sf & TYF_UNDEFINED) && (tf & (TYF_UNDEFINED | TYF_VOID))) return true;
    if ((sf & TYF_NULL) && (tf & TYF_NULL)) return true;
    if ((sf & TYF_NULLABLE) && !compiler_options()->strict_null_checks &&
        st->relation != TY_RELATION_ASSIGNABLE_STRICT_NULLS && st->relation != TY_RELATION_STRICT_SUBTYPE
    ) {
        return true;
    }
    if ((sf & TYF_OBJECT) && (tf & TYF_NON_PRIMITIVE)) return true;
    // Enums
    if ((sf & TYF_ENUM) && (tf & TYF_NUMBER)) return true;
    if ((sf & TYF_ENUM_LITERAL) && (sf & TYF_LITERAL) && (tf & TYF_LITERAL) && !(tf & TYF_ENUM_LITERAL) &&
        s->kind == TY_LITERAL && t->kind == TY_LITERAL && same_literal_value(s, t)
    ) {
        return true;
    }
    if ((sf & TYF_ENUM) && (tf & TYF_ENUM)) {
        if (enums_related(me, s->literal.enum_decl, t->literal.enum_decl) &&
            csview_eq(&s->literal.enum_member->name, &t->literal.enum_member->name)
        ) {
            return true;
        }
    }
    if (ty_is_enum_union(s) && ty_is_enum_union(t) && enums_related(me, s->alias, t->alias)) return true;
    if (s->kind == TY_LITERAL && t->kind == TY_LITERAL && s->literal.enum_decl && t->literal.enum_decl) {
        if ((sf & TYF_LITERAL) && same_literal_value(s, t) && enums_related(me, s->literal.enum_decl, t->literal.enum_decl)) {
            return true;
        }
    }
    if (st->relation == TY_RELATION_ASSIGNABLE || st->relation == TY_RELATION_COMPARABLE ||
        st->relation == TY_RELATION_ASSIGNABLE_STRICT_NULLS
    ) {
        // Type number is assignable to any computed numeric enum type or any numeric enum literal type, and a numeric
        // literal type to a computed one or an enum literal with the same value (enums as bit flags)
        bool t_numeric_enum_literal = (tf & TYF_NUMBER_LITERAL) && (tf & TYF_ENUM_LITERAL);
        if ((sf & TYF_NUMBER) && ((tf & TYF_ENUM) || t_numeric_enum_literal)) return true;
        if ((sf & TYF_NUMBER_LITERAL) && !(sf & TYF_ENUM_LITERAL) && s->kind == TY_LITERAL &&
            ((tf & TYF_ENUM) || (t_numeric_enum_literal && t->kind == TY_LITERAL && same_literal_value(s, t)))
        ) {
            return true;
        }
    }
    if (st->relation == TY_RELATION_COMPARABLE) {
        // Enum members of the same value are comparable to each other
        if (s->kind == TY_LITERAL && t->kind == TY_LITERAL && (s->literal.enum_decl || t->literal.enum_decl) &&
            (sf & TYF_LITERAL) && same_literal_value(s, t)
        ) {
            return true;
        }
    }
    return false;
}

// Whether there's any structure to look at. If neither side has any, the simple relation was the answer
static bool is_structured(Ty const* ty) {
    return ty->flags & (TYF_OBJECT | TYF_UNION | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED);
}

// Reporting

static void report_line(RelationState* st, unsigned ts_code, cstr message) {
    // `boolean` is a union, whose member `false` reads as 'boolean' again once widened for display
    if (!vec_ty_report_line_is_empty(&st->lines) && cstr_eq(&vec_ty_report_line_back(&st->lines)->message, &message)) {
        cstr_drop(&message);
        return;
    }
    vec_ty_report_line_push(&st->lines, (TyReportLine){ .ts_code = ts_code, .message = message });
}

static void report_truncate(RelationState* st, isize_t len) {
    while (vec_ty_report_line_size(&st->lines) > len) vec_ty_report_line_pop(&st->lines); // drops the line
}

static cstr display_for_report(Checker* me, Ty const* ty, bool widen) {
    cstr ret = cstr_null;
    ty_display(me, (TyDisplay){ .target = &ret, .widen_literals = widen }, ty);
    return ret;
}

cstr ty_report_source_cstr(Checker* me, Ty const* source, Ty const* target) {
    bool widen = !could_have_singletons(me, target);
    return display_for_report(me, widen ? ty_widened_literal(me, source) : source, widen);
}

static void report_relation_error(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    if (st->override_next_head) {
        st->override_next_head--;
        return;
    }
    bool widen = !could_have_singletons(me, target);
    cstr source_str = display_for_report(me, widen ? ty_widened_literal(me, source) : source, widen);
    cstr target_str = display_for_report(me, target, false);
    if (st->relation == TY_RELATION_COMPARABLE) {
        report_line(st, 2678, cstr_d("Type '", source_str, "' is not comparable to type '", target_str, "'."));
    } else {
        report_line(st, 2322, cstr_d("Type '", source_str, "' is not assignable to type '", target_str, "'."));
    }
    c_drop(cstr, &source_str, &target_str);
}

// Apparent types and property lookup

static Ty const* apparent_type(Checker* me, Ty const* ty) {
    for (u32 depth = 0; ty->kind == TY_TYPE_PARAM && depth < 16; depth++) {
        Ty const* constraint = ty_param_constraint(me, ty);
        if (!constraint || constraint == ty) return global_type(me, GLOBAL_EMPTY_OBJECT);
        ty = constraint;
    }
    u32 f = ty->flags;
    Ty const* global = nullptr;
    if (ty->kind == TY_INTRINSIC || ty->kind == TY_LITERAL || (f & TYF_BOOLEAN)) {
        if (f & (TYF_STRING | TYF_STRING_LITERAL)) global = global_type(me, GLOBAL_STRING);
        else if (f & (TYF_NUMBER | TYF_NUMBER_LITERAL)) global = global_type(me, GLOBAL_NUMBER);
        else if (f & (TYF_BOOLEAN | TYF_BOOLEAN_LITERAL)) global = global_type(me, GLOBAL_BOOLEAN);
        else if (f & TYF_ES_SYMBOL) global = global_type(me, GLOBAL_SYMBOL);
        else if (f & (TYF_BIGINT | TYF_BIGINT_LITERAL)) global = global_type(me, GLOBAL_BIGINT);
        else if (f & TYF_NON_PRIMITIVE) global = global_type(me, GLOBAL_EMPTY_OBJECT);
        else return ty;
        return global ? global : global_type(me, GLOBAL_EMPTY_OBJECT);
    }
    if (f & TYF_ENUM) {
        global = global_type(me, GLOBAL_NUMBER);
        return global ? global : global_type(me, GLOBAL_EMPTY_OBJECT);
    }
    return ty;
}

// A tuple seen as the array it also is
static Ty const* tuple_as_array(Checker* me, Ty const* tuple) {
    if (is_empty_tuple(tuple)) return ty_array(me, ty_intrinsic(me, TY_NEVER), tuple->declared.tuple->readonly);
    TupleShape const* shape = tuple->reference.target->declared.tuple;
    TyList args = ty_reference_args(me, tuple);
    Ty const** elements = malloc(sizeof(Ty const*) * (args.len + 1));
    u32 len = 0;
    for (u32 i = 0; i < args.len; i++) {
        Ty const* element = args.data[i];
        if ((shape->flags[i] & TUPLE_ELEMENT_REST) && element->kind == TY_REFERENCE && !is_tuple(element)) {
            TyList rest_args = ty_reference_args(me, element);
            if (rest_args.len == 1) element = rest_args.data[0];
        }
        elements[len++] = element;
        if (shape->flags[i] & TUPLE_ELEMENT_OPTIONAL) elements[len - 1] = element;
    }
    Ty const* element = ty_union(me, elements, len, nullptr, (TyList){0});
    free(elements);
    return ty_array(me, element, shape->readonly);
}

static FoundProp find_prop(Checker* me, Ty const* ty, Atom const* name, u32 depth) {
    FoundProp ret = {0};
    if (depth > 8) return ret;
    if (ty->kind == TY_INTERSECTION) {
        // The property of an intersection is a type of its own (tsc makes a synthetic symbol per name as well)
        u64 key[2] = { ty->id, (u64)(uintptr_t)name };
        FoundProp const* cached = intern_get(&my intersection_props, key, 2);
        if (cached) return *cached;
        Ty const* stack_buf[16];
        u32 len = 0;
        bool all_optional = true;
        u32 flags = 0;
        for (u32 i = 0; i < ty->composite.types.len && len < c_arraylen(stack_buf); i++) {
            FoundProp part = find_prop(me, apparent_type(me, ty->composite.types.data[i]), name, depth + 1);
            if (!part.found) continue;
            stack_buf[len++] = part.type;
            all_optional &= (part.flags & PROP_OPTIONAL) != 0;
            flags |= part.flags & ~(u32)PROP_OPTIONAL;
        }
        if (len) {
            ret.found = true;
            ret.type = ty_intersection(me, stack_buf, len, nullptr, (TyList){0});
            ret.flags = flags | (all_optional ? PROP_OPTIONAL : 0);
        }
        FoundProp* memo = arena_new(&my arena, FoundProp);
        *memo = ret;
        *intern_slot(me, &my intersection_props, key, 2) = memo;
        return ret;
    }
    if (!(ty->flags & TYF_OBJECT)) return ret;
    Prop const* prop = ty_get_prop(me, ty, name);
    if (!prop) {
        Members const* members = ty_members(me, ty);
        Ty const* fallback = members->call_count || members->construct_count ? global_type(me, GLOBAL_FUNCTION) : nullptr;
        if (fallback && fallback != ty) prop = ty_get_prop(me, fallback, name);
        if (!prop) {
            fallback = global_type(me, GLOBAL_OBJECT);
            if (fallback && fallback != ty) prop = ty_get_prop(me, fallback, name);
        }
    }
    if (prop) {
        ret.found = true;
        ret.type = prop_type(me, prop);
        ret.flags = prop->flags;
    }
    return ret;
}

FoundProp ty_find_prop(Checker* me, Ty const* ty, Atom const* name) { return find_prop(me, ty, name, 0); }
Ty const* ty_apparent_type(Checker* me, Ty const* ty) { return apparent_type(me, ty); }
bool ty_is_tuple(Ty const* ty) { return is_tuple(ty); }
bool ty_is_array(Checker* me, Ty const* ty) { return ty->kind == TY_REFERENCE && is_array_target(me, ty->reference.target); }

// Signatures

Ty const* ty_rest_element_type(Checker* me, Ty const* rest);
static Ty const* rest_element_type(Checker* me, Ty const* rest) { return ty_rest_element_type(me, rest); }

Ty const* ty_rest_element_type(Checker* me, Ty const* rest) {
    if (rest->kind == TY_REFERENCE && is_array_target(me, rest->reference.target)) {
        return ty_reference_args(me, rest).data[0];
    }
    if (is_tuple(rest)) {
        Ty const* array = tuple_as_array(me, rest);
        return ty_reference_args(me, array).data[0];
    }
    return ty_intrinsic(me, TY_ANY);
}

// The type of parameter i, null if there's none. A rest parameter stands for every position from its own on
static Ty const* param_type_at(Checker* me, Signature const* sig, u32 i) {
    if (i < sig->param_count && !sig->params[i].rest) return sig->params[i].type;
    if (sig->param_count && sig->params[sig->param_count - 1].rest && i + 1 >= sig->param_count) {
        return rest_element_type(me, sig->params[sig->param_count - 1].type);
    }
    return nullptr;
}

static bool has_rest(Signature const* sig) {
    return sig->param_count && sig->params[sig->param_count - 1].rest;
}

static Signature const* instantiate_signature(Checker* me, Signature const* sig, TyList targets) {
    Signature* ret = arena_new(&my arena, Signature);
    *ret = (Signature){
        .flags = sig->flags,
        .target = sig,
        .mapper = ty_mapper_new(me, sig->type_params, targets),
    };
    signature_resolve(me, ret);
    ret->type_params = (TyList){0};
    return ret;
}

static Ternary compare_signatures(
    Checker* me, RelationState* st, Signature const* source, Signature const* target, bool report
) {
    signature_resolve(me, source);
    signature_resolve(me, target);
    if (source->type_params.len && source->type_params.data != target->type_params.data) {
        // TODO(inference): tsc instantiates the source in the context of the target. Unifying same-length type
        // parameter lists covers identical generic signatures, anything else has its type parameters erased
        Ty const* stack_buf[16];
        u32 len = source->type_params.len;
        if (len <= c_arraylen(stack_buf)) {
            for (u32 i = 0; i < len; i++) {
                stack_buf[i] = target->type_params.len == len ? target->type_params.data[i] : ty_intrinsic(me, TY_ANY);
            }
            source = instantiate_signature(me, source, (TyList){ .data = stack_buf, .len = len });
        }
    }

    if (!has_rest(target) && source->min_args > target->param_count) {
        if (report) {
            report_line(st, 2849, cstr_d(
                "Target signature provides too few arguments. Expected ", source->min_args, " or more, but got ",
                target->param_count, "."
            ));
        }
        return TERNARY_FALSE;
    }

    bool strict = st->relation != TY_RELATION_COMPARABLE && compiler_options()->strict_function_types &&
        !(target->flags & (SIGNATURE_METHOD | SIGNATURE_CONSTRUCT));
    Ternary result = TERNARY_TRUE;
    u32 count = source->param_count > target->param_count ? source->param_count : target->param_count;
    for (u32 i = 0; i < count; i++) {
        Ty const* source_type = param_type_at(me, source, i);
        Ty const* target_type = param_type_at(me, target, i);
        if (!source_type || !target_type) break;
        Ternary related = TERNARY_FALSE;
        if (!strict) related = is_related_to(me, st, source_type, target_type, false);
        if (!related) related = is_related_to(me, st, target_type, source_type, report);
        if (!related) {
            if (report) {
                u32 source_index = i < source->param_count ? i : source->param_count - 1;
                u32 target_index = i < target->param_count ? i : target->param_count - 1;
                report_line(st, 2328, cstr_d(
                    "Types of parameters '", atom_sv(source->params[source_index].name), "' and '",
                    atom_sv(target->params[target_index].name), "' are incompatible."
                ));
            }
            return TERNARY_FALSE;
        }
        result &= related;
    }

    Ty const* target_return = signature_return_type(me, target);
    if (target_return->flags & (TYF_VOID | TYF_ANY)) return result;
    Ty const* source_return = signature_return_type(me, source);
    // A type predicate is only assignable to the same predicate (a `boolean` target takes any)
    TyPredicate const* target_predicate = signature_predicate(me, target);
    if (target_predicate) {
        TyPredicate const* source_predicate = signature_predicate(me, source);
        if (!source_predicate) {
            if (report) {
                cstr sig_str = cstr_null;
                ty_display_signature(me, (TyDisplay){ .target = &sig_str }, source);
                report_line(st, 1224, cstr_d("Signature '", sig_str, "' must be a type predicate."));
                cstr_drop(&sig_str);
            }
            return TERNARY_FALSE;
        }
        Ternary related = TERNARY_FALSE;
        if (source_predicate->is_this != target_predicate->is_this || source_predicate->asserts != target_predicate->asserts) {
            if (report && source_predicate->is_this != target_predicate->is_this) {
                report_line(st, 1228, cstr_lit("A 'this'-based type guard is not compatible with a parameter-based type guard."));
            }
        } else if (!source_predicate->is_this && source_predicate->param_index != target_predicate->param_index) {
            if (report) {
                report_line(st, 1227, cstr_d(
                    "Parameter '", atom_sv(source_predicate->param_name), "' is not in the same position as parameter '",
                    atom_sv(target_predicate->param_name), "'."
                ));
            }
        } else if (!source_predicate->type || !target_predicate->type) {
            related = source_predicate->type == target_predicate->type ? TERNARY_TRUE : TERNARY_FALSE;
        } else {
            related = is_related_to(me, st, source_predicate->type, target_predicate->type, report);
        }
        if (!related && report) {
            cstr source_str = cstr_null;
            cstr target_str = cstr_null;
            ty_display_predicate(me, (TyDisplay){ .target = &source_str }, source_predicate);
            ty_display_predicate(me, (TyDisplay){ .target = &target_str }, target_predicate);
            report_line(st, 1226, cstr_d("Type predicate '", source_str, "' is not assignable to '", target_str, "'."));
            c_drop(cstr, &source_str, &target_str);
        }
        return result & related;
    }
    Ternary related = TERNARY_FALSE;
    if (st->relation == TY_RELATION_COMPARABLE) related = is_related_to(me, st, target_return, source_return, false);
    if (!related) related = is_related_to(me, st, source_return, target_return, report);
    return result & related;
}

static Ternary signatures_related(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool construct, bool report
) {
    Members const* source_members = ty_members(me, source);
    Members const* target_members = ty_members(me, target);
    Signature* const* source_sigs = construct ? source_members->construct : source_members->call;
    Signature* const* target_sigs = construct ? target_members->construct : target_members->call;
    u32 source_count = construct ? source_members->construct_count : source_members->call_count;
    u32 target_count = construct ? target_members->construct_count : target_members->call_count;
    if (!target_count) return TERNARY_TRUE;
    Ternary result = TERNARY_TRUE;
    for (u32 i = 0; i < target_count; i++) {
        Signature const* target_sig = target_sigs[i];
        Ternary related = TERNARY_FALSE;
        for (u32 j = 0; j < source_count && !related; j++) {
            Signature const* source_sig = source_sigs[j];
            if (construct && (source_sig->flags & SIGNATURE_ABSTRACT) && !(target_sig->flags & SIGNATURE_ABSTRACT)) {
                if (report) {
                    report_line(st, 2517, cstr_lit("Cannot assign an abstract constructor type to a non-abstract constructor type."));
                }
                continue;
            }
            isize_t saved = vec_ty_report_line_size(&st->lines);
            related = compare_signatures(me, st, source_sig, target_sig, report && source_count == 1 && target_count == 1);
            if (related) report_truncate(st, saved);
        }
        if (!related) {
            if (report && !(source_count == 1 && target_count == 1)) {
                cstr source_str = display_for_report(me, source, false);
                cstr sig_str = cstr_null;
                ty_display_signature(me, (TyDisplay){ .target = &sig_str }, target_sig);
                report_line(st, 2658, cstr_d("Type '", source_str, "' provides no match for the signature '", sig_str, "'."));
                c_drop(cstr, &source_str, &sig_str);
            }
            return TERNARY_FALSE;
        }
        result &= related;
    }
    return result;
}

// Object members

static bool is_numeric_prop_name(Atom const* name) {
    if (!name->len) return false;
    for (u32 i = 0; i < name->len; i++) {
        if (name->str[i] < '0' || name->str[i] > '9') return false;
    }
    return true;
}

static Ternary props_related(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report, bool source_is_apparent
) {
    Members const* target_members = ty_members(me, target);
    bool comparable = st->relation == TY_RELATION_COMPARABLE;

    // A primitive is shown as itself, not as the global interface its properties come from
    if (report && !source_is_apparent) {
        // Missing properties get a message of their own instead of the generic head
        Atom const* missing[4];
        u32 missing_count = 0;
        for (u32 i = 0; i < target_members->prop_count; i++) {
            Prop const* target_prop = target_members->props[i];
            if ((target_prop->flags & PROP_OPTIONAL) || find_prop(me, source, target_prop->name, 0).found) continue;
            if (missing_count < c_arraylen(missing)) missing[missing_count] = target_prop->name;
            missing_count++;
        }
        if (missing_count) {
            cstr source_str = display_for_report(me, source, true);
            cstr target_str = display_for_report(me, target, false);
            if (missing_count == 1) {
                report_line(st, 2741, cstr_d(
                    "Property '", atom_sv(missing[0]), "' is missing in type '", source_str, "' but required in type '",
                    target_str, "'."
                ));
            } else {
                cstr names = cstr_null;
                u32 shown = missing_count > 4 ? 4 : missing_count;
                for (u32 i = 0; i < shown; i++) {
                    if (i) cstr_append_lit(&names, ", ");
                    cstr_append_sv(&names, atom_sv(missing[i]));
                }
                if (missing_count > 4) {
                    report_line(st, 2740, cstr_d(
                        "Type '", source_str, "' is missing the following properties from type '", target_str, "': ",
                        names, ", and ", missing_count - 4, " more."
                    ));
                } else {
                    report_line(st, 2739, cstr_d(
                        "Type '", source_str, "' is missing the following properties from type '", target_str, "': ", names
                    ));
                }
                cstr_drop(&names);
            }
            c_drop(cstr, &source_str, &target_str);
            st->override_next_head++;
            return TERNARY_FALSE;
        }
    }

    Ternary result = TERNARY_TRUE;
    Ty const* undefined = ty_intrinsic(me, TY_UNDEFINED);
    for (u32 i = 0; i < target_members->prop_count; i++) {
        Prop const* target_prop = target_members->props[i];
        FoundProp source_prop = find_prop(me, source, target_prop->name, 0);
        if (!source_prop.found) {
            if ((target_prop->flags & PROP_OPTIONAL) && st->relation != TY_RELATION_STRICT_SUBTYPE) continue;
            return TERNARY_FALSE;
        }
        if ((source_prop.flags & PROP_OPTIONAL) && !(target_prop->flags & PROP_OPTIONAL) && !comparable) {
            if (report) {
                cstr source_str = display_for_report(me, source, true);
                cstr target_str = display_for_report(me, target, false);
                report_line(st, 2327, cstr_d(
                    "Property '", atom_sv(target_prop->name), "' is optional in type '", source_str,
                    "' but required in type '", target_str, "'."
                ));
                c_drop(cstr, &source_str, &target_str);
            }
            return TERNARY_FALSE;
        }
        Ty const* source_type = source_prop.type;
        Ty const* target_type = prop_type(me, target_prop);
        if ((target_prop->flags & PROP_OPTIONAL) && compiler_options()->strict_null_checks) {
            Ty const* parts[2] = { target_type, undefined };
            target_type = ty_union(me, parts, 2, nullptr, (TyList){0});
            if (source_prop.flags & PROP_OPTIONAL) {
                Ty const* source_parts[2] = { source_type, undefined };
                source_type = ty_union(me, source_parts, 2, nullptr, (TyList){0});
            }
        }
        Ternary related = is_related_to(me, st, source_type, target_type, report);
        if (!related) {
            if (report) {
                report_line(st, 2326, cstr_d("Types of property '", atom_sv(target_prop->name), "' are incompatible."));
            }
            return TERNARY_FALSE;
        }
        result &= related;
    }
    return result;
}

static bool index_key_applies(IndexInfo const* source, IndexInfo const* target) {
    if (source->key == target->key) return true;
    // A string index signature covers numeric keys as well
    return (source->key->flags & TYF_STRING) && (target->key->flags & TYF_NUMBER);
}

static bool has_inferable_index(Ty const* ty) {
    if (ty->kind == TY_ANONYMOUS) return true;
    return ty->kind == TY_DECLARED &&
        (ty->declared.kind == TY_DECLARED_NAMESPACE || ty->declared.kind == TY_DECLARED_ENUM_OBJECT);
}

static Ternary index_signatures_related(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    Members const* target_members = ty_members(me, target);
    if (!target_members->index_count) return TERNARY_TRUE;
    Members const* source_members = ty_members(me, source);
    bool target_has_string_index = false;
    for (u32 i = 0; i < target_members->index_count; i++) {
        target_has_string_index |= (target_members->index[i].key->flags & TYF_STRING) != 0;
    }
    Ternary result = TERNARY_TRUE;
    for (u32 i = 0; i < target_members->index_count; i++) {
        IndexInfo const* target_info = &target_members->index[i];
        if (target_has_string_index && (target_info->value->flags & TYF_ANY)) continue;
        IndexInfo const* source_info = nullptr;
        for (u32 j = 0; j < source_members->index_count && !source_info; j++) {
            if (index_key_applies(&source_members->index[j], target_info)) source_info = &source_members->index[j];
        }
        if (source_info) {
            Ternary related = is_related_to(me, st, source_info->value, target_info->value, report);
            if (!related) {
                if (report) {
                    cstr key_str = display_for_report(me, target_info->key, false);
                    report_line(st, 2634, cstr_d("'", key_str, "' index signatures are incompatible."));
                    cstr_drop(&key_str);
                }
                return TERNARY_FALSE;
            }
            result &= related;
        } else if (has_inferable_index(source) && !(target_info->key->flags & TYF_ES_SYMBOL)) {
            // An object literal type is related if each of its properties the signature applies to is
            for (u32 j = 0; j < source_members->prop_count; j++) {
                Prop const* prop = source_members->props[j];
                if ((target_info->key->flags & TYF_NUMBER) && !is_numeric_prop_name(prop->name)) continue;
                Ternary related = is_related_to(me, st, prop_type(me, prop), target_info->value, report);
                if (!related) {
                    if (report) {
                        report_line(st, 2530, cstr_d(
                            "Property '", atom_sv(prop->name), "' is incompatible with index signature."
                        ));
                    }
                    return TERNARY_FALSE;
                }
                result &= related;
            }
        } else {
            if (report) {
                cstr key_str = display_for_report(me, target_info->key, false);
                cstr source_str = display_for_report(me, source, true);
                report_line(st, 2329, cstr_d("Index signature for type '", key_str, "' is missing in type '", source_str, "'."));
                c_drop(cstr, &key_str, &source_str);
            }
            return TERNARY_FALSE;
        }
    }
    return result;
}

// Tuples

static Ternary tuples_related(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    TupleShape const* source_shape = is_empty_tuple(source) ? source->declared.tuple : source->reference.target->declared.tuple;
    TupleShape const* target_shape = is_empty_tuple(target) ? target->declared.tuple : target->reference.target->declared.tuple;
    if (source_shape->readonly && !target_shape->readonly && st->relation != TY_RELATION_COMPARABLE) {
        if (report) {
            cstr source_str = display_for_report(me, source, false);
            cstr target_str = display_for_report(me, target, false);
            report_line(st, 4104, cstr_d(
                "The type '", source_str, "' is 'readonly' and cannot be assigned to the mutable type '", target_str, "'."
            ));
            c_drop(cstr, &source_str, &target_str);
            st->override_next_head++;
        }
        return TERNARY_FALSE;
    }
    TyList source_args = is_empty_tuple(source) ? (TyList){0} : ty_reference_args(me, source);
    TyList target_args = is_empty_tuple(target) ? (TyList){0} : ty_reference_args(me, target);
    u32 source_min = 0, target_min = 0;
    bool source_rest = false, target_rest = false;
    for (u32 i = 0; i < source_shape->arity; i++) {
        if (!(source_shape->flags[i] & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST))) source_min = i + 1;
        source_rest |= (source_shape->flags[i] & TUPLE_ELEMENT_REST) != 0;
    }
    for (u32 i = 0; i < target_shape->arity; i++) {
        if (!(target_shape->flags[i] & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST))) target_min = i + 1;
        target_rest |= (target_shape->flags[i] & TUPLE_ELEMENT_REST) != 0;
    }
    if (source_min < target_min) {
        if (report) {
            report_line(st, 2618, cstr_d("Source has ", source_min, " element(s) but target requires ", target_min, "."));
        }
        return TERNARY_FALSE;
    }
    if (!target_rest && (source_rest || source_shape->arity > target_shape->arity)) {
        if (report) {
            if (source_rest) {
                report_line(st, 2619, cstr_d("Source has ", source_min, " element(s) but target allows only ", target_shape->arity, "."));
            } else {
                report_line(st, 2618 + 1, cstr_d(
                    "Source has ", source_shape->arity, " element(s) but target allows only ", target_shape->arity, "."
                ));
            }
        }
        return TERNARY_FALSE;
    }
    Ternary result = TERNARY_TRUE;
    for (u32 i = 0; i < source_shape->arity; i++) {
        Ty const* source_type = source_args.data[i];
        if (source_shape->flags[i] & TUPLE_ELEMENT_REST) source_type = rest_element_type(me, source_type);
        // Past the target's fixed elements everything lands in its rest element (only a trailing one is handled)
        u32 target_index = i < target_shape->arity ? i : target_shape->arity - 1;
        if (!target_shape->arity) break;
        Ty const* target_type = target_args.data[target_index];
        if (target_shape->flags[target_index] & TUPLE_ELEMENT_REST) target_type = rest_element_type(me, target_type);
        Ternary related = is_related_to(me, st, source_type, target_type, report);
        if (!related) {
            if (report) {
                report_line(st, 2626, cstr_d("Type at position ", i, " in source is not compatible with type at position ", target_index, " in target."));
            }
            return TERNARY_FALSE;
        }
        result &= related;
    }
    return result;
}

// Variances

typedef struct Variances {
    LazyState state;
    u32 len;
    unsigned char data[];
} Variances;

static Ty const* variance_marker(Checker* me, u32 which) {
    // 0: super, 1: sub (constrained to super, so it's related to it), 2: unrelated to either
    u64 key[2] = { KEY_MARKER, which };
    Ty const* marker = intern_get(&my links, key, 2);
    if (!marker) {
        static char const* const names[] = { "super-T", "sub-T", "other-T" };
        marker = ty_synthetic_type_param(me, csview_from(names[which]));
        if (which == 1) {
            Ty const* super = variance_marker(me, 0);
            marker->param.state->constraint = super;
            marker->param.state->constraint_state = LAZY_STATE_DONE;
        }
        *intern_slot(me, &my links, key, 2) = marker;
    }
    return marker;
}

static Ty const* marker_reference(Checker* me, Ty const* target, u32 index, Ty const* marker) {
    TyList params = target->declared.type_params;
    Ty const** args = malloc(sizeof(Ty const*) * params.len);
    for (u32 i = 0; i < params.len; i++) args[i] = i == index ? marker : params.data[i];
    Ty const* ret = ty_reference(me, target, (TyList){ .data = args, .len = params.len });
    free(args);
    return ret;
}

// Null while the variances are being measured (the caller compares structurally then, like tsc)
static Variances const* get_variances(Checker* me, Ty const* target) {
    u64 key[2] = { KEY_VARIANCES, target->id };
    Variances* variances = (Variances*)intern_get(&my links, key, 2);
    if (variances) return variances->state == LAZY_STATE_DONE ? variances : nullptr;

    u32 len = target->declared.type_params.len;
    variances = arena_alloc(&my arena, sizeof(Variances) + len);
    variances->len = len;
    variances->state = LAZY_STATE_IN_FLIGHT;
    *intern_slot(me, &my links, key, 2) = variances;
    if (is_array_target(me, target) || target->declared.kind == TY_DECLARED_TUPLE_TARGET ||
        target->declared.kind == TY_DECLARED_SYNTHETIC
    ) {
        memset(variances->data, VARIANCE_COVARIANT, len);
        variances->state = LAZY_STATE_DONE;
        return variances;
    }
    bool saved_unreliable = my variance_unreliable;
    my variance_depth++;
    for (u32 i = 0; i < len; i++) {
        my variance_unreliable = false;
        Ty const* with_super = marker_reference(me, target, i, variance_marker(me, 0));
        Ty const* with_sub = marker_reference(me, target, i, variance_marker(me, 1));
        unsigned variance =
            (ty_is_related(me, with_sub, with_super, TY_RELATION_ASSIGNABLE) ? VARIANCE_COVARIANT : 0) |
            (ty_is_related(me, with_super, with_sub, TY_RELATION_ASSIGNABLE) ? VARIANCE_CONTRAVARIANT : 0);
        if (variance == VARIANCE_BIVARIANT) {
            Ty const* with_other = marker_reference(me, target, i, variance_marker(me, 2));
            if (ty_is_related(me, with_other, with_super, TY_RELATION_ASSIGNABLE)) variance = VARIANCE_INDEPENDENT;
        }
        if (my variance_unreliable) variance |= VARIANCE_UNRELIABLE;
        variances->data[i] = (unsigned char)variance;
    }
    my variance_depth--;
    my variance_unreliable = saved_unreliable;
    variances->state = LAZY_STATE_DONE;
    if (getenv("TTTT_DUMP_VARIANCES")) {
        cstr name = ty_to_cstr(me, target);
        for (u32 i = 0; i < len; i++) log_info("variance of ", name, " #", i, ": ", (u32)variances->data[i]);
        cstr_drop(&name);
    }
    return variances;
}

bool ty_type_param_is_contravariant(Checker* me, Ty const* target, u32 index) {
    if (target->kind != TY_DECLARED || index >= target->declared.type_params.len) return false;
    Variances const* variances = get_variances(me, target);
    if (!variances || index >= variances->len) return false;
    return (variances->data[index] & (VARIANCE_BIVARIANT | VARIANCE_INDEPENDENT)) == VARIANCE_CONTRAVARIANT;
}

// Returns TERNARY_MAYBE + *unknown when the variances can't decide
static Ternary type_args_related(
    Checker* me, RelationState* st, Variances const* variances, TyList source_args, TyList target_args, bool report,
    bool* unreliable
) {
    Ternary result = TERNARY_TRUE;
    *unreliable = false;
    for (u32 i = 0; i < source_args.len && i < target_args.len; i++) {
        unsigned variance = i < variances->len ? variances->data[i] : VARIANCE_COVARIANT;
        if (variance & VARIANCE_UNRELIABLE) *unreliable = true;
        unsigned kind = variance & (VARIANCE_BIVARIANT | VARIANCE_INDEPENDENT);
        if (kind == VARIANCE_INDEPENDENT) continue;
        Ty const* s = source_args.data[i];
        Ty const* t = target_args.data[i];
        Ternary related;
        if (kind == VARIANCE_COVARIANT) {
            related = is_related_to(me, st, s, t, report);
        } else if (kind == VARIANCE_CONTRAVARIANT) {
            related = is_related_to(me, st, t, s, report);
        } else if (kind == VARIANCE_BIVARIANT) {
            related = is_related_to(me, st, t, s, false);
            if (!related) related = is_related_to(me, st, s, t, report);
        } else {
            related = is_related_to(me, st, s, t, report);
            if (related) related &= is_related_to(me, st, t, s, report);
        }
        if (!related) return TERNARY_FALSE;
        result &= related;
    }
    return result;
}

// Structured relation

static Ternary each_related(Checker* me, RelationState* st, TyList sources, Ty const* target, bool report) {
    Ternary result = TERNARY_TRUE;
    for (u32 i = 0; i < sources.len; i++) {
        Ternary related = is_related_to(me, st, sources.data[i], target, report);
        if (!related) return TERNARY_FALSE;
        result &= related;
    }
    return result;
}

static Ternary some_related(Checker* me, RelationState* st, TyList sources, Ty const* target) {
    for (u32 i = 0; i < sources.len; i++) {
        Ternary related = is_related_to(me, st, sources.data[i], target, false);
        if (related) return related;
    }
    return TERNARY_FALSE;
}

static Ternary related_to_some(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    TyList targets = target->composite.types;
    for (u32 i = 0; i < targets.len; i++) {
        if (targets.data[i] == source) return TERNARY_TRUE;
    }
    // A unit type is only ever related to a union member by identity (above) or through a non-unit member
    bool source_is_unit = (source->kind == TY_LITERAL && !source->literal.enum_decl) ||
        (source->kind == TY_INTRINSIC && (source->flags & TYF_NULLABLE));
    for (u32 i = 0; i < targets.len; i++) {
        if (source_is_unit && targets.data[i]->kind == TY_LITERAL && !targets.data[i]->literal.enum_decl &&
            st->relation != TY_RELATION_COMPARABLE
        ) {
            continue;
        }
        Ternary related = is_related_to(me, st, source, targets.data[i], false);
        if (related) return related;
    }
    if (report) {
        // tsc elaborates on the one member that's the obvious intent: the only object type for an object source
        Ty const* best = nullptr;
        u32 candidates = 0;
        if (source->flags & TYF_OBJECT) {
            for (u32 i = 0; i < targets.len; i++) {
                if (targets.data[i]->flags & TYF_OBJECT) {
                    best = targets.data[i];
                    candidates++;
                }
            }
        }
        if (candidates == 1) is_related_to(me, st, source, best, true);
    }
    return TERNARY_FALSE;
}

static Ternary object_related(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report, bool source_is_apparent
) {
    Ternary result = props_related(me, st, source, target, report, source_is_apparent);
    if (source_is_apparent) report = false;
    if (result) result &= signatures_related(me, st, source, target, false, report);
    if (result) result &= signatures_related(me, st, source, target, true, report);
    if (result) result &= index_signatures_related(me, st, source, target, report);
    return result;
}

static Ty const* constraint_of_indexed(Checker* me, Ty const* indexed) {
    Ty const* object = indexed->indexed.object;
    Ty const* index = indexed->indexed.index;
    Ty const* object_constraint = object->kind == TY_TYPE_PARAM ? ty_param_constraint(me, object) : nullptr;
    Ty const* index_constraint = index->kind == TY_TYPE_PARAM ? ty_param_constraint(me, index) : nullptr;
    if (!object_constraint && !index_constraint) return nullptr;
    Ty const* ret = ty_indexed(me, object_constraint ? object_constraint : object, index_constraint ? index_constraint : index);
    return ret == indexed || (ret->flags & TYF_DEFERRED) ? nullptr : ret;
}

// What a type variable is known to be at least: tsc's getBaseConstraintOfType, without the circularity reporting.
// Null if there's nothing to know (an unconstrained type parameter, `T[K]` that stays generic...)
static Ty const* base_constraint(Checker* me, Ty const* ty, u32 depth) {
    if (depth > 8) return nullptr;
    switch ((TyKind)ty->kind) {
        case TY_TYPE_PARAM: {
            Ty const* constraint = ty_param_constraint(me, ty);
            if (!constraint || constraint == ty) return nullptr;
            if (constraint->kind == TY_TYPE_PARAM || (constraint->flags & TYF_DEFERRED)) {
                return base_constraint(me, constraint, depth + 1);
            }
            return constraint;
        }
        case TY_KEYOF: {
            Ty const* all[3] = { ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_ES_SYMBOL) };
            return ty_union(me, all, 3, nullptr, (TyList){0});
        }
        case TY_INDEXED: {
            Ty const* object = ty->indexed.object;
            Ty const* index = ty->indexed.index;
            Ty const* object_constraint = base_constraint(me, object, depth + 1);
            Ty const* index_constraint = base_constraint(me, index, depth + 1);
            if (!object_constraint && !index_constraint) return nullptr;
            Ty const* ret = ty_indexed(me, object_constraint ? object_constraint : object, index_constraint ? index_constraint : index);
            if (ret == ty) return nullptr;
            return ret->flags & TYF_DEFERRED ? base_constraint(me, ret, depth + 1) : ret;
        }
        case TY_CONDITIONAL: {
            Ty const* parts[2];
            for (u32 i = 0; i < 2; i++) {
                parts[i] = ty_conditional_branch(me, ty, i);
                if (parts[i]->kind == TY_TYPE_PARAM || (parts[i]->flags & TYF_DEFERRED)) {
                    parts[i] = base_constraint(me, parts[i], depth + 1);
                    if (!parts[i]) return nullptr;
                }
            }
            return ty_union(me, parts, 2, nullptr, (TyList){0});
        }
        default:
            return nullptr;
    }
}

Ty const* ty_base_constraint(Checker* me, Ty const* ty) { return base_constraint(me, ty, 0); }

static Ternary target_each_related(Checker* me, RelationState* st, Ty const* source, TyList targets, bool report) {
    Ternary result = TERNARY_TRUE;
    for (u32 i = 0; i < targets.len; i++) {
        Ternary related = is_related_to(me, st, source, targets.data[i], report);
        if (!related) return TERNARY_FALSE;
        result &= related;
    }
    return result;
}

static Ternary structured_related(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    bool comparable = st->relation == TY_RELATION_COMPARABLE;

    // Unions and intersections
    if (source->kind == TY_UNION) {
        if (comparable) return some_related(me, st, source->composite.types, target);
        return each_related(me, st, source->composite.types, target, report);
    }
    bool source_is_generic = source->kind == TY_TYPE_PARAM || (source->flags & TYF_DEFERRED) != 0;
    if (target->kind == TY_UNION) {
        // A generic source can still be related through its constraint, further down
        Ternary related = related_to_some(me, st, source, target, report && !source_is_generic);
        if (related || (!source_is_generic && source->kind != TY_INTERSECTION)) return related;
    }
    if (target->kind == TY_INTERSECTION) return target_each_related(me, st, source, target->composite.types, report);
    if (source->kind == TY_INTERSECTION) {
        Ternary related = some_related(me, st, source->composite.types, target);
        if (related) return related;
        // Otherwise the members may still add up to the target, property by property (below)
    }

    // Generic targets
    if (my variance_depth && ((source->flags | target->flags) & TYF_DEFERRED)) my variance_unreliable = true;
    switch ((TyKind)target->kind) {
        case TY_TYPE_PARAM: {
            if (comparable) {
                Ty const* constraint = ty_param_constraint(me, target);
                if (constraint && constraint != target) {
                    Ternary related = is_related_to(me, st, source, constraint, false);
                    if (related) return related;
                }
            }
            if (source->kind != TY_TYPE_PARAM && !(source->flags & TYF_DEFERRED) && source->kind != TY_INTERSECTION) {
                return TERNARY_FALSE;
            }
        }   break;

        case TY_KEYOF: {
            // keyof is contravariant: the keys of the constraint of T are keys of every T
            if (source->kind == TY_KEYOF) {
                Ternary related = is_related_to(me, st, target->keyof.operand, source->keyof.operand, false);
                if (related) return related;
            }
            Ty const* operand = target->keyof.operand;
            Ty const* constraint = operand->kind == TY_TYPE_PARAM ? ty_param_constraint(me, operand) : nullptr;
            if (constraint && constraint != operand) {
                Ty const* keys = ty_keyof(me, constraint);
                if (keys != target) {
                    Ternary related = is_related_to(me, st, source, keys, false);
                    if (related == TERNARY_TRUE) return related;
                }
            }
        }   break;

        case TY_INDEXED: {
            if (source->kind == TY_INDEXED) {
                Ternary related = is_related_to(me, st, source->indexed.object, target->indexed.object, false);
                if (related) related &= is_related_to(me, st, source->indexed.index, target->indexed.index, false);
                if (related) return related;
            }
            Ty const* constraint = constraint_of_indexed(me, target);
            if (constraint) {
                Ternary related = is_related_to(me, st, source, constraint, false);
                if (related) return related;
            }
        }   break;

        case TY_CONDITIONAL: {
            if (source->kind == TY_CONDITIONAL && source->conditional.check == target->conditional.check &&
                source->conditional.extends == target->conditional.extends
            ) {
                Ternary related = is_related_to(me, st, ty_conditional_branch(me, source, true), ty_conditional_branch(me, target, true), false);
                if (related) related &= is_related_to(me, st, ty_conditional_branch(me, source, false), ty_conditional_branch(me, target, false), false);
                if (related) return related;
            }
            // Related to both branches is related to whichever gets picked
            Ternary related = is_related_to(me, st, source, ty_conditional_branch(me, target, true), false);
            if (related) related &= is_related_to(me, st, source, ty_conditional_branch(me, target, false), false);
            if (related) return related;
        }   break;

        default: break;
    }

    // Generic sources
    switch ((TyKind)source->kind) {
        case TY_TYPE_PARAM: {
            Ty const* constraint = ty_param_constraint(me, source);
            if (!constraint || constraint == source || (constraint->flags & TYF_ANY)) {
                // An unconstrained type parameter is related to `{}`, but not to `object`
                if (target->flags & TYF_NON_PRIMITIVE) return TERNARY_FALSE;
                if (target->kind == TY_TYPE_PARAM || (target->flags & TYF_DEFERRED)) return TERNARY_FALSE;
                return is_related_to(me, st, global_type(me, GLOBAL_EMPTY_OBJECT), target, false);
            }
            return is_related_to(me, st, constraint, target, report);
        }

        case TY_KEYOF: {
            Ty const* all[3] = { ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_ES_SYMBOL) };
            return is_related_to(me, st, ty_union(me, all, 3, nullptr, (TyList){0}), target, report);
        }

        case TY_INDEXED: {
            Ty const* constraint = constraint_of_indexed(me, source);
            return constraint ? is_related_to(me, st, constraint, target, report) : TERNARY_FALSE;
        }

        case TY_CONDITIONAL: {
            Ternary related = is_related_to(me, st, ty_conditional_branch(me, source, true), target, report);
            if (related) related &= is_related_to(me, st, ty_conditional_branch(me, source, false), target, report);
            return related;
        }

        default: break;
    }
    if (target->kind == TY_TYPE_PARAM || (target->flags & TYF_DEFERRED) || target->kind == TY_UNION) return TERNARY_FALSE;

    // Object types from here on
    if (!(target->flags & TYF_OBJECT)) return TERNARY_FALSE;

    bool source_is_tuple = is_tuple(source) || is_empty_tuple(source);
    bool target_is_tuple = is_tuple(target) || is_empty_tuple(target);
    if (source_is_tuple && target_is_tuple) return tuples_related(me, st, source, target, report);
    if (target_is_tuple) {
        if (report && source->kind == TY_REFERENCE && is_array_target(me, source->reference.target)) {
            TupleShape const* shape = is_empty_tuple(target) ? target->declared.tuple : target->reference.target->declared.tuple;
            u32 min = 0;
            for (u32 i = 0; i < shape->arity; i++) {
                if (!(shape->flags[i] & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST))) min = i + 1;
            }
            if (min) {
                report_line(st, 2620, cstr_d("Target requires ", min, " element(s) but source may have fewer."));
            }
        }
        return TERNARY_FALSE;
    }
    if (source_is_tuple) source = tuple_as_array(me, source);

    if (source->kind == TY_REFERENCE && target->kind == TY_REFERENCE) {
        Ty const* source_target = source->reference.target;
        Ty const* target_target = target->reference.target;
        if (source_target == target_target) {
            Variances const* variances = get_variances(me, source_target);
            if (variances) {
                bool unreliable;
                isize_t saved = vec_ty_report_line_size(&st->lines);
                Ternary related = type_args_related(
                    me, st, variances, ty_reference_args(me, source), ty_reference_args(me, target), report, &unreliable
                );
                if (related || !unreliable) return related;
                report_truncate(st, saved);
            }
        } else if (is_array_target(me, source_target) && is_array_target(me, target_target)) {
            // HACK: Array<S> and ReadonlyArray<T> relate like their element types. It's what comparing the two
            // interfaces member by member comes to, without depending on how well generic methods compare yet
            if (source_target == my readonly_array_target && !comparable) {
                if (report) {
                    cstr source_str = display_for_report(me, source, false);
                    cstr target_str = display_for_report(me, target, false);
                    report_line(st, 4104, cstr_d(
                        "The type '", source_str, "' is 'readonly' and cannot be assigned to the mutable type '",
                        target_str, "'."
                    ));
                    c_drop(cstr, &source_str, &target_str);
                    st->override_next_head++;
                }
                return TERNARY_FALSE;
            }
            return is_related_to(me, st, ty_reference_args(me, source).data[0], ty_reference_args(me, target).data[0], report);
        }
    }

    Ty const* apparent = source->kind == TY_INTERSECTION ? source : apparent_type(me, source);
    if (!(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) return TERNARY_FALSE;
    if (my variance_depth && ty_members(me, target)->mapped) my variance_unreliable = true;
    if (apparent->kind == TY_INTERSECTION) {
        // Signatures and index signatures of intersections aren't combined yet, properties are
        Ternary result = props_related(me, st, apparent, target, report, false);
        Members const* target_members = ty_members(me, target);
        if (result && (target_members->call_count || target_members->construct_count || target_members->index_count)) {
            return TERNARY_FALSE;
        }
        return result;
    }
    return object_related(me, st, apparent, target, report, apparent != source);
}

// Deep nesting (tsc's isDeeplyNestedType)

static Ty const* recursion_identity(Ty const* ty) {
    switch ((TyKind)ty->kind) {
        case TY_REFERENCE: return ty->reference.target;
        case TY_ANONYMOUS: return ty->anonymous.target;
        case TY_INDEXED: {
            Ty const* object = ty;
            while (object->kind == TY_INDEXED) object = object->indexed.object;
            return object;
        }
        default: return nullptr;
    }
}

static bool is_deeply_nested(Ty const* ty, Ty const* const* stack, u32 depth) {
    Ty const* identity = recursion_identity(ty);
    if (!identity || depth < 3) return false;
    u32 count = 0;
    u32 last_id = 0;
    for (u32 i = 0; i < depth; i++) {
        Ty const* other = stack[i];
        if (recursion_identity(other) == identity && other->id >= last_id) {
            if (++count >= 3) return true;
            last_id = other->id;
        }
    }
    return false;
}

static Ternary recursive_related(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    RelationCache* cache = &my relations[st->relation];
    u64 key = relation_key(source, target);
    unsigned cached = relation_cache_get(cache, key);
    if (cached == CACHE_SUCCEEDED) return TERNARY_TRUE;
    if (cached == CACHE_FAILED && !report) return TERNARY_FALSE;
    for (u32 i = 0; i < st->maybe_count; i++) {
        if (st->maybe_keys[i] == key) return TERNARY_MAYBE;
    }
    if (st->depth == MAX_RELATION_DEPTH) {
        st->overflow = true;
        return TERNARY_FALSE;
    }
    u32 maybe_start = st->maybe_count;
    if (st->maybe_count == st->maybe_cap) {
        st->maybe_cap = st->maybe_cap ? st->maybe_cap * 2 : 32;
        st->maybe_keys = realloc(st->maybe_keys, sizeof(u64) * st->maybe_cap);
        panic_if(!st->maybe_keys, "out of memory");
    }
    st->maybe_keys[st->maybe_count++] = key;
    unsigned saved_expanding = st->expanding;
    if (!(st->expanding & 1) && is_deeply_nested(source, st->source_stack, st->depth)) st->expanding |= 1;
    if (!(st->expanding & 2) && is_deeply_nested(target, st->target_stack, st->depth)) st->expanding |= 2;
    st->source_stack[st->depth] = source;
    st->target_stack[st->depth] = target;
    st->depth++;
    Ternary result = st->expanding == 3 ? TERNARY_MAYBE : structured_related(me, st, source, target, report);
    st->depth--;
    st->expanding = saved_expanding;

    if (result) {
        if (result == TERNARY_TRUE || st->depth == 0) {
            // Everything assumed along the way held up
            for (u32 i = maybe_start; i < st->maybe_count; i++) relation_cache_put(cache, st->maybe_keys[i], CACHE_SUCCEEDED);
            st->maybe_count = maybe_start;
        }
    } else {
        if (!st->overflow) relation_cache_put(cache, key, CACHE_FAILED);
        st->maybe_count = maybe_start;
    }
    return result;
}

static Ternary is_related_to(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    if (source == target) return TERNARY_TRUE;
    if (is_simple_related(me, st, source, target)) return TERNARY_TRUE;
    Ternary result = TERNARY_FALSE;
    if (st->relation == TY_RELATION_COMPARABLE && !is_structured(source) && !is_structured(target)) {
        // Comparable is looser about literals: a primitive is comparable to its literals
        Ty const* widened = ty_widened_literal(me, target);
        if (widened != target && widened == source) result = TERNARY_TRUE;
    } else if (is_structured(source) || is_structured(target)) {
        isize_t saved = vec_ty_report_line_size(&st->lines);
        result = recursive_related(me, st, source, target, report);
        if (result) report_truncate(st, saved);
    }
    if (!result && report) report_relation_error(me, st, source, target);
    return result;
}

static bool check_related(
    Checker* me, Ty const* source, Ty const* target, TyRelation relation, VecTyReportLine* report_into
) {
    // The error type is related to everything, in both directions: whatever made it was reported already
    if ((source->flags | target->flags) & TYF_ERROR) return true;
    // The stacks are big and only ever read below `depth`, so the state isn't zeroed wholesale
    RelationState state;
    RelationState* st = &state;
    st->relation = relation;
    st->reporting = report_into != nullptr;
    st->lines = (VecTyReportLine){0};
    st->override_next_head = 0;
    st->maybe_keys = nullptr;
    st->maybe_count = st->maybe_cap = 0;
    st->depth = 0;
    st->expanding = 0;
    st->overflow = false;
    st->outer = my relating;
    if (!st->reporting && (source == target || is_simple_related(me, st, source, target))) return true;
    if (!st->reporting && !is_structured(source) && !is_structured(target)) {
        // Nothing to look into (narrowing asks this about every member of big literal unions): see is_related_to()
        if (relation != TY_RELATION_COMPARABLE || source->kind != TY_INTRINSIC) return false;
        Ty const* widened = ty_widened_literal(me, target);
        return widened != target && widened == source;
    }
    my relating = st;
    Ternary result = is_related_to(me, st, source, target, st->reporting);
    my relating = st->outer;
    if (st->overflow) {
        my instantiation_limit_hit = true;
        result = TERNARY_FALSE;
    }
    if (report_into && !result) {
        // Collected innermost first
        while (!vec_ty_report_line_is_empty(&st->lines)) {
            vec_ty_report_line_push(report_into, vec_ty_report_line_pull(&st->lines));
        }
    }
    vec_ty_report_line_drop(&st->lines);
    free(st->maybe_keys);
    static int debug = -1;
    if (debug < 0) debug = getenv("TTTT_DEBUG_RELATE") != nullptr;
    if (debug) {
        cstr source_str = ty_to_cstr(me, source);
        cstr target_str = ty_to_cstr(me, target);
        log_info("relation ", (u32)relation, ": '", source_str, "' #", source->id, " -> '", target_str, "' #", target->id, ": ",
            result ? "yes" : "no");
        c_drop(cstr, &source_str, &target_str);
    }
    return result != TERNARY_FALSE;
}

bool ty_is_comparable_either_way(Checker* me, Ty const* a, Ty const* b) {
    if (a == b) return true;
    // Two string or number literals (enum members included) of different values never are, and discriminant
    // narrowing asks that about every member of a union
    u32 valued = TYF_STRING_LITERAL | TYF_NUMBER_LITERAL;
    if (a->kind == TY_LITERAL && b->kind == TY_LITERAL && (a->flags & valued) && (b->flags & valued) &&
        !same_literal_value(a, b)
    ) {
        return false;
    }
    return ty_is_related(me, a, b, TY_RELATION_COMPARABLE) || ty_is_related(me, b, a, TY_RELATION_COMPARABLE);
}

bool ty_is_related(Checker* me, Ty const* source, Ty const* target, TyRelation relation) {
    return check_related(me, source, target, relation, nullptr);
}

void ty_explain_unrelated(Checker* me, Ty const* source, Ty const* target, TyRelation relation, VecTyReportLine* out) {
    check_related(me, source, target, relation, out);
}
