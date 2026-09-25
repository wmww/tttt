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
    GLOBAL_CALLABLE_FUNCTION,
    GLOBAL_NEWABLE_FUNCTION,
    GLOBAL_EMPTY_OBJECT, // Not a global: the `{}` type
} GlobalType;

typedef struct RelationState {
    TyRelation relation;
    bool reporting;
    VecTyReportLine lines; // innermost first while being collected
    u32 override_next_head; // A more specific message stands in for the next "Type S is not assignable to type T"
    // Relating to the members of an intersection target (tsc's IntersectionState.Target): no excess property checks,
    // the intersection as a whole had them
    bool intersection_target;
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
    static char const* const names[] = { "String", "Number", "Boolean", "Symbol", "BigInt", "Function", "Object", "RegExp", "CallableFunction", "NewableFunction" };
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

// tsc's getBaseTypeOfLiteralType: the primitive (or enum) a literal belongs to, fresh or not
Ty const* ty_base_type_of_literal(Checker* me, Ty const* ty) {
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
                Ty const* part = ty_base_type_of_literal(me, ty->composite.types.data[i]);
                if (!count || widened[count - 1] != part) widened[count++] = part;
            }
            Ty const* ret = ty_union(me, widened, count, nullptr, (TyList){0});
            free(widened);
            return ret;
        }
    }
    return ty;
}

// tsc's getWidenedLiteralType: what a literal becomes at a mutable location. Only fresh literals widen: a regular one
// was written as a type (or came from one)
Ty const* ty_widened_literal(Checker* me, Ty const* ty) {
    if (ty_is_fresh(ty)) return ty_base_type_of_literal(me, ty);
    if (ty->kind == TY_UNION && (ty->flags & TYF_FRESH_LITERAL) && !(ty->flags & TYF_BOOLEAN)) {
        TyBuf widened = {0};
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            Ty const* part = ty_widened_literal(me, ty->composite.types.data[i]);
            // Members of one enum all widen to the same (possibly huge) union: once is enough
            if (!widened.len || widened.data[widened.len - 1] != part) ty_buf_push(&widened, part);
        }
        Ty const* ret = ty_union(me, widened.data, widened.len, nullptr, (TyList){0});
        ty_buf_drop(&widened);
        return ret;
    }
    if (ty->kind == TY_UNION && (ty->flags & TYF_FRESH_LITERAL)) return ty_intrinsic(me, TY_BOOLEAN);
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
    if (ty->kind == TY_STRING_MAPPING) {
        // Through its constraint, like any instantiable type: Uppercase<string> is some string
        Ty const* constraint = ty_base_constraint(me, ty);
        if (constraint && constraint != ty) return could_have_singletons(me, constraint);
    }
    return ty->kind == TY_LITERAL ||
        (ty->flags & (TYF_DEFERRED | TYF_UNDEFINED | TYF_NULL | TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) != 0;
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
    if ((sf & TYF_STRING_LIKE) && (tf & TYF_STRING)) return true;
    if ((sf & TYF_NUMBER_LITERAL) && (tf & TYF_NUMBER)) return true;
    if ((sf & TYF_BIGINT_LITERAL) && (tf & TYF_BIGINT)) return true;
    if ((sf & TYF_BOOLEAN_LITERAL) && (tf & TYF_BOOLEAN)) return true;
    if ((sf & TYF_UNIQUE_ES_SYMBOL) && (tf & TYF_ES_SYMBOL)) return true;
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
    return ty->flags & (TYF_OBJECT | TYF_UNION | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_TEMPLATE_LITERAL |
        TYF_STRING_MAPPING);
}

// Reporting

static void report_line(RelationState* st, unsigned ts_code, cstr message) {
    // `boolean` is a union, whose member `false` reads as 'boolean' again once widened for display
    TyReportLine const* last = nullptr;
    for (isize_t i = vec_ty_report_line_size(&st->lines); i > 0 && !last; i--) {
        if (!vec_ty_report_line_at(&st->lines, i - 1)->related) last = vec_ty_report_line_at(&st->lines, i - 1);
    }
    if (last && cstr_eq(&last->message, &message)) {
        cstr_drop(&message);
        return;
    }
    vec_ty_report_line_push(&st->lines, (TyReportLine){ .ts_code = ts_code, .message = message });
}

static void report_line_at(RelationState* st, unsigned ts_code, cstr message, Span span) {
    vec_ty_report_line_push(&st->lines, (TyReportLine){ .ts_code = ts_code, .message = message, .span = span });
}

static void report_related(RelationState* st, unsigned ts_code, cstr message, Span span) {
    if (!span_valid(span)) {
        cstr_drop(&message);
        return;
    }
    vec_ty_report_line_push(&st->lines, (TyReportLine){ .ts_code = ts_code, .message = message, .span = span, .related = true });
}

static void report_truncate(RelationState* st, isize_t len) {
    while (vec_ty_report_line_size(&st->lines) > len) vec_ty_report_line_pop(&st->lines); // drops the line
}

static cstr display_for_report(Checker* me, Ty const* ty, bool widen) {
    cstr ret = cstr_null;
    ty_display(me, (TyDisplay){ .target = &ret, .widen_literals = widen }, ty);
    return ret;
}

// Whether a literal source is shown widened: not against `never` (tsc wants `assertNever(x)` errors to show x as is)
static bool report_widens(Checker* me, Ty const* target) {
    return !(target->flags & TYF_NEVER) && !could_have_singletons(me, target);
}

cstr ty_report_source_cstr(Checker* me, Ty const* source, Ty const* target) {
    source = ty_regular(me, source); // Messages see normalised types, like the relation
    bool widen = report_widens(me, target);
    return display_for_report(me, widen ? ty_base_type_of_literal(me, source) : source, widen);
}

// An unconstrained type parameter source could have been constrained to the target (tsc's reportRelationError: unless
// that constraint would be circular)
static void report_missing_constraint(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    if (source->kind != TY_TYPE_PARAM || source->param.state->is_this || ty_param_constraint(me, source)) return;
    Ty const* declared = source;
    while (declared->param.state->clone_of) declared = declared->param.state->clone_of;
    GenericItem const* item = declared->param.state->item;
    if (!item) return;
    Ty const* base = target;
    for (u32 depth = 0; base && base->kind == TY_TYPE_PARAM && depth < 16; depth++) {
        if (base == source || base == declared) return;
        base = ty_param_constraint(me, base);
    }
    cstr target_str = display_for_report(me, target, false);
    report_related(st, 2208, cstr_d("This type parameter might need an `extends ", target_str, "` constraint."), item->name);
    cstr_drop(&target_str);
}

static void report_relation_error(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    if (st->override_next_head) {
        st->override_next_head--;
        report_missing_constraint(me, st, source, target);
        return;
    }
    bool widen = report_widens(me, target);
    Ty const* generalized = widen ? ty_base_type_of_literal(me, source) : source;
    cstr source_str = display_for_report(me, generalized, widen);
    cstr target_str = display_for_report(me, target, false);
    if (target->kind == TY_TYPE_PARAM && !target->param.state->is_this) {
        // What a type parameter target could be (tsc's reportRelationError), below the head reported next
        Ty const* constraint = target;
        for (u32 depth = 0; constraint && constraint->kind == TY_TYPE_PARAM && depth < 16; depth++) {
            constraint = ty_param_constraint(me, constraint);
        }
        bool generalized_fits = constraint && ty_is_related(me, generalized, constraint, TY_RELATION_ASSIGNABLE);
        bool original_fits = constraint && !generalized_fits && ty_is_related(me, source, constraint, TY_RELATION_ASSIGNABLE);
        if (generalized_fits || original_fits) {
            cstr shown = original_fits ? display_for_report(me, source, false) : cstr_clone(source_str);
            cstr constraint_str = display_for_report(me, constraint, false);
            report_line(st, 5075, cstr_d(
                "'", shown, "' is assignable to the constraint of type '", target_str, "', but '", target_str,
                "' could be instantiated with a different subtype of constraint '", constraint_str, "'."
            ));
            c_drop(cstr, &shown, &constraint_str);
        } else {
            report_line(st, 5082, cstr_d(
                "'", target_str, "' could be instantiated with an arbitrary type which could be unrelated to '", source_str, "'."
            ));
        }
    }
    if (st->relation == TY_RELATION_COMPARABLE) {
        report_line(st, 2678, cstr_d("Type '", source_str, "' is not comparable to type '", target_str, "'."));
    } else {
        report_line(st, 2322, cstr_d("Type '", source_str, "' is not assignable to type '", target_str, "'."));
    }
    report_missing_constraint(me, st, source, target);
    c_drop(cstr, &source_str, &target_str);
}

// Apparent types and property lookup

static Ty const* apparent_type(Checker* me, Ty const* ty) {
    if (ty->kind == TY_TYPE_PARAM) {
        // Through the constraint, which sees the type parameter as its `this` (`this.x` inside a class is `this`'s
        // x, and so is `t.x` for `t: T` with `T extends C`: tsc's getApparentType + getTypeWithThisArgument)
        Ty const* param = ty;
        for (u32 depth = 0; ty->kind == TY_TYPE_PARAM && depth < 16; depth++) {
            Ty const* constraint = ty_param_constraint(me, ty);
            if (!constraint || constraint == ty) return global_type(me, GLOBAL_EMPTY_OBJECT);
            ty = constraint;
        }
        if (ty->flags & (TYF_OBJECT | TYF_INTERSECTION)) return ty_with_this_arg(me, ty, param);
    }
    u32 f = ty->flags;
    Ty const* global = nullptr;
    if (ty->kind == TY_INTRINSIC || ty->kind == TY_LITERAL || (f & TYF_BOOLEAN)) {
        if (f & (TYF_STRING | TYF_STRING_LITERAL)) global = global_type(me, GLOBAL_STRING);
        else if (f & (TYF_NUMBER | TYF_NUMBER_LITERAL)) global = global_type(me, GLOBAL_NUMBER);
        else if (f & (TYF_BOOLEAN | TYF_BOOLEAN_LITERAL)) global = global_type(me, GLOBAL_BOOLEAN);
        else if (f & TYF_ES_SYMBOL_LIKE) global = global_type(me, GLOBAL_SYMBOL);
        else if (f & (TYF_BIGINT | TYF_BIGINT_LITERAL)) global = global_type(me, GLOBAL_BIGINT);
        else if (f & TYF_NON_PRIMITIVE) global = global_type(me, GLOBAL_EMPTY_OBJECT);
        else return ty;
        return global ? global : global_type(me, GLOBAL_EMPTY_OBJECT);
    }
    if (f & TYF_ENUM) {
        global = global_type(me, GLOBAL_NUMBER);
        return global ? global : global_type(me, GLOBAL_EMPTY_OBJECT);
    }
    if (f & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) {
        global = global_type(me, GLOBAL_STRING);
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

// A property of an object type, including those it has as a function (`Function`, or `CallableFunction` under
// strictBindCallApply like tsc's globalCallableFunctionType) and as an object (`Object`)
Prop const* ty_get_object_prop(Checker* me, Ty const* ty, Atom const* name) {
    Prop const* prop = ty_get_prop(me, ty, name);
    // The object of a const enum isn't a real object (tsc's skipObjectFunctionPropertyAugment)
    bool const_enum = ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_ENUM_OBJECT &&
        ty->declared.symbol && (ty->declared.symbol->flags & SYMBOL_CONST_ENUM);
    if (!prop && !const_enum) {
        Members const* members = ty_members(me, ty);
        Ty const* fallback = nullptr;
        if (members->call_count || members->construct_count) {
            if (compiler_options()->strict_bind_call_apply) {
                fallback = global_type(me, members->call_count ? GLOBAL_CALLABLE_FUNCTION : GLOBAL_NEWABLE_FUNCTION);
            }
            if (!fallback) fallback = global_type(me, GLOBAL_FUNCTION);
        }
        if (fallback && fallback != ty) prop = ty_get_prop(me, fallback, name);
        if (!prop) {
            fallback = global_type(me, GLOBAL_OBJECT);
            if (fallback && fallback != ty) prop = ty_get_prop(me, fallback, name);
        }
    }
    return prop;
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
            if (!ret.prop) ret.prop = part.prop;
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
    Prop const* prop = ty_get_object_prop(me, ty, name);
    if (prop) {
        ret.found = true;
        ret.type = prop_type(me, prop);
        ret.flags = prop->flags;
        ret.prop = prop;
    }
    return ret;
}

FoundProp ty_find_prop(Checker* me, Ty const* ty, Atom const* name) { return find_prop(me, ty, name, 0); }
Ty const* ty_tuple_as_array(Checker* me, Ty const* tuple) { return tuple_as_array(me, tuple); }
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

// The type of parameter i, null if there's none. A rest parameter stands for every position from its own on. An
// optional parameter holds undefined too (tsc's getTypeAtPosition)
static Ty const* param_type_at(Checker* me, Signature const* sig, u32 i) {
    if (i < sig->param_count && !sig->params[i].rest) {
        return sig->params[i].optional ? ty_optional_param_type(me, sig->params[i].type) : sig->params[i].type;
    }
    if (sig->param_count && sig->params[sig->param_count - 1].rest && i + 1 >= sig->param_count) {
        Ty const* rest = sig->params[sig->param_count - 1].type;
        if (ty_is_tuple(rest)) {
            // `...args: [a: A, b?: B, ...c: C[]]` is parameters of their own
            TupleShape const* shape = rest->reference.target->declared.tuple;
            TyList elements = ty_reference_args(me, rest);
            u32 j = i + 1 - sig->param_count;
            bool ends_in_rest = shape->arity && (shape->flags[shape->arity - 1] & TUPLE_ELEMENT_REST);
            if (j >= shape->arity) return ends_in_rest ? rest_element_type(me, elements.data[shape->arity - 1]) : nullptr;
            if (shape->flags[j] & TUPLE_ELEMENT_REST) return rest_element_type(me, elements.data[j]);
            return elements.data[j];
        }
        return rest_element_type(me, rest);
    }
    return nullptr;
}

Ty const* ty_signature_param_type_at(Checker* me, Signature const* sig, u32 i) { return param_type_at(me, sig, i); }

// How many parameters a signature has, with a rest tuple of fixed length counted as its elements
static u32 param_count_of(Signature const* sig) {
    if (!sig->param_count || !sig->params[sig->param_count - 1].rest) return sig->param_count;
    Ty const* rest = sig->params[sig->param_count - 1].type;
    if (!ty_is_tuple(rest)) return sig->param_count;
    return sig->param_count - 1 + rest->reference.target->declared.tuple->arity;
}

// Takes any number of arguments: a rest parameter that isn't a tuple of fixed length
static bool has_rest(Checker* me, Signature const* sig) {
    if (!sig->param_count || !sig->params[sig->param_count - 1].rest) return false;
    Ty const* rest = sig->params[sig->param_count - 1].type;
    if (!ty_is_tuple(rest)) return true;
    TupleShape const* shape = rest->reference.target->declared.tuple;
    return shape->arity && (shape->flags[shape->arity - 1] & TUPLE_ELEMENT_REST);
}

// tsc's SignatureCheckMode.StrictCallback / BivariantCallback: comparing the signatures of two callback parameters
typedef enum CallbackMode {
    CALLBACK_NONE,
    CALLBACK_STRICT,
    CALLBACK_BIVARIANT,
} CallbackMode;

// tsc's isInstantiatedGenericParameter: the parameter was generic before the signature was instantiated
static bool is_instantiated_generic_param(Checker* me, Signature const* sig, u32 i) {
    if (!sig->target) return false;
    signature_resolve(me, sig->target);
    Ty const* type = param_type_at(me, sig->target, i);
    if (!type) return false;
    if (ty_is_generic_index(type)) return true;
    return (type->flags & TYF_OBJECT) && (type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) && ty_members(me, type)->mapped;
}

// The one call signature of a callback parameter's type, if it is one
static Signature const* callback_signature(Checker* me, Signature const* sig, u32 i, Ty const* type) {
    if (is_instantiated_generic_param(me, sig, i)) return nullptr;
    Signature const* ret = ty_single_signature(me, ty_non_nullable(me, type), false, false);
    return ret && !signature_predicate(me, ret) ? ret : nullptr;
}

static Ternary compare_signatures(
    Checker* me, RelationState* st, Signature const* source, Signature const* target, CallbackMode callback, bool report
) {
    signature_resolve(me, source);
    signature_resolve(me, target);
    // A generic source is instantiated in the context of the target (whose type parameters, if any, are canonical
    // already: instantiations share them)
    if (source->type_params.len && source->type_params.data != target->type_params.data) {
        source = ty_instantiate_signature_in_context_of(me, source, target, nullptr);
    }

    u32 target_count = param_count_of(target);
    if (!has_rest(me, target) && source->min_args > target_count) {
        if (report) {
            report_line(st, 2849, cstr_d(
                "Target signature provides too few arguments. Expected ", source->min_args, " or more, but got ",
                target_count, "."
            ));
        }
        return TERNARY_FALSE;
    }

    bool strict = !callback && st->relation != TY_RELATION_COMPARABLE && compiler_options()->strict_function_types &&
        !(target->flags & (SIGNATURE_METHOD | SIGNATURE_CONSTRUCT));
    Ternary result = TERNARY_TRUE;
    // A source that says what `this` is needs a target that agrees (a `void` one is called any way)
    if (source->this_type && !(source->this_type->flags & TYF_VOID) && target->this_type) {
        Ternary related = TERNARY_FALSE;
        if (!strict) related = is_related_to(me, st, source->this_type, target->this_type, false);
        if (!related) related = is_related_to(me, st, target->this_type, source->this_type, report);
        if (!related) {
            if (report) report_line(st, 2685, cstr_d("The 'this' types of each signature are incompatible."));
            return TERNARY_FALSE;
        }
        result &= related;
    }
    u32 source_count = param_count_of(source);
    u32 count = source_count > target_count ? source_count : target_count;
    for (u32 i = 0; i < count; i++) {
        Ty const* source_type = param_type_at(me, source, i);
        Ty const* target_type = param_type_at(me, target, i);
        if (!source_type || !target_type) break;
        if (source_type == target_type) continue;
        // Two callbacks: their parameters are output positions, so only the signatures are compared, the other way
        // around (tsc's compareSignaturesRelated). A Promise<T> that uses T in callbacks only is covariant in T
        Signature const* source_callback = callback ? nullptr : callback_signature(me, source, i, source_type);
        Signature const* target_callback = source_callback ? callback_signature(me, target, i, target_type) : nullptr;
        Ternary related = TERNARY_FALSE;
        if (target_callback &&
            ty_undefined_or_null_facts(me, source_type) == ty_undefined_or_null_facts(me, target_type)
        ) {
            related = compare_signatures(me, st, target_callback, source_callback,
                strict ? CALLBACK_STRICT : CALLBACK_BIVARIANT, report);
        } else {
            if (!callback && !strict) related = is_related_to(me, st, source_type, target_type, false);
            if (!related) related = is_related_to(me, st, target_type, source_type, report);
        }
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

    // A return type that is being inferred further up the stack is any here, without making that inference circular
    // (tsc's isResolvingReturnTypeOfSignature)
    if (ty_signature_return_in_flight(me, target)) return result;
    Ty const* target_return = signature_return_type(me, target);
    if (target_return->flags & (TYF_VOID | TYF_ANY)) return result;
    if (ty_signature_return_in_flight(me, source)) return result;
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
                report_line(st, 2518, cstr_lit("A 'this'-based type guard is not compatible with a parameter-based type guard."));
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
    // Return types of callbacks are related both ways, or the type containing them wouldn't be covariant
    Ternary related = TERNARY_FALSE;
    if (st->relation == TY_RELATION_COMPARABLE || callback == CALLBACK_BIVARIANT) {
        related = is_related_to(me, st, target_return, source_return, false);
    }
    if (!related) related = is_related_to(me, st, source_return, target_return, report);
    return result & related;
}

// tsc's getErasedSignature: every type parameter is any
static Signature const* erased_signature(Checker* me, Signature const* sig) {
    signature_resolve(me, sig);
    if (!sig->type_params.len) return sig;
    Ty const* stack_anys[16];
    Ty const** anys = sig->type_params.len <= c_arraylen(stack_anys) ? stack_anys : malloc(sizeof(Ty const*) * sig->type_params.len);
    panic_if(!anys, "out of memory");
    for (u32 i = 0; i < sig->type_params.len; i++) anys[i] = ty_intrinsic(me, TY_ANY);
    Signature const* ret = ty_signature_instantiation(me, sig, (TyList){ .data = anys, .len = sig->type_params.len });
    if (anys != stack_anys) free(anys);
    return ret;
}

bool ty_implementation_compatible_with_overload(Checker* me, Signature const* implementation, Signature const* overload) {
    Signature const* source = erased_signature(me, implementation);
    Signature const* target = erased_signature(me, overload);
    Ty const* source_return = signature_return_type(me, source);
    Ty const* target_return = signature_return_type(me, target);
    if (target_return != ty_intrinsic(me, TY_VOID) && !ty_is_related(me, target_return, source_return, TY_RELATION_ASSIGNABLE) &&
        !ty_is_related(me, source_return, target_return, TY_RELATION_ASSIGNABLE)
    ) {
        return false;
    }
    // isSignatureAssignableTo with IgnoreReturnTypes: the target returning any compares nothing more
    Signature* copy = arena_new(&my arena, Signature);
    *copy = *target;
    copy->ret = ty_intrinsic(me, TY_ANY);
    copy->ret_state = LAZY_STATE_DONE;
    copy->predicate = nullptr;
    copy->predicate_done = true;
    return ty_is_related(me, ty_signature_type(me, source), ty_signature_type(me, copy), TY_RELATION_ASSIGNABLE);
}

// Instantiations of the same generic type or type literal, whose signatures pair up one to one
static bool same_signature_origin(Ty const* source, Ty const* target) {
    if (source->kind == TY_REFERENCE && target->kind == TY_REFERENCE) return source->reference.target == target->reference.target;
    return source->kind == TY_ANONYMOUS && target->kind == TY_ANONYMOUS && source->anonymous.target &&
        source->anonymous.target == target->anonymous.target;
}

// tsc's signaturesRelatedTo
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
    if (construct && source_count && (source_sigs[0]->flags & SIGNATURE_ABSTRACT) && !(target_sigs[0]->flags & SIGNATURE_ABSTRACT)) {
        if (report) report_line(st, 2517, cstr_lit("Cannot assign an abstract constructor type to a non-abstract constructor type."));
        return TERNARY_FALSE;
    }
    Ternary result = TERNARY_TRUE;
    if (source_count == target_count && same_signature_origin(source, target)) {
        // Pairwise, with type parameters erased: they are the same ones
        for (u32 i = 0; i < target_count; i++) {
            Ternary related = compare_signatures(
                me, st, erased_signature(me, source_sigs[i]), erased_signature(me, target_sigs[i]), CALLBACK_NONE, report
            );
            if (!related) return TERNARY_FALSE;
            result &= related;
        }
    } else if (source_count == 1 && target_count == 1) {
        // A single generic source is instantiated in the context of the target instead (only comparability erases)
        bool erase = st->relation == TY_RELATION_COMPARABLE;
        result = compare_signatures(
            me, st, erase ? erased_signature(me, source_sigs[0]) : source_sigs[0],
            erase ? erased_signature(me, target_sigs[0]) : target_sigs[0], CALLBACK_NONE, report
        );
    } else {
        // Every target signature needs a source signature, type parameters erased. Only the first failure explains
        for (u32 i = 0; i < target_count; i++) {
            Signature const* target_sig = erased_signature(me, target_sigs[i]);
            isize_t saved = vec_ty_report_line_size(&st->lines);
            bool elaborate = report;
            Ternary related = TERNARY_FALSE;
            for (u32 j = 0; j < source_count && !related; j++) {
                related = compare_signatures(me, st, erased_signature(me, source_sigs[j]), target_sig, CALLBACK_NONE, elaborate);
                elaborate = false;
            }
            if (related) {
                report_truncate(st, saved);
                result &= related;
                continue;
            }
            if (elaborate) {
                cstr source_str = display_for_report(me, source, false);
                cstr sig_str = cstr_null;
                ty_display_signature(me, (TyDisplay){ .target = &sig_str }, target_sigs[i]);
                report_line(st, 2658, cstr_d("Type '", source_str, "' provides no match for the signature '", sig_str, "'."));
                c_drop(cstr, &source_str, &sig_str);
            }
            return TERNARY_FALSE;
        }
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

static bool requires_optional_properties(Checker* me, Ty const* source) {
    Ty const* literal = source->kind == TY_ANONYMOUS && source->anonymous.target ? source->anonymous.target : source;
    if (literal->kind == TY_ANONYMOUS && literal->anonymous.object_literal) return false;
    if (is_tuple(source)) return false;
    // `[]` (never[]; tsc's isEmptyArrayLiteralType)
    return !(ty_is_array(me, source) && (ty_reference_args(me, source).data[0]->flags & TYF_NEVER));
}

static Prop const* root_declaration(Prop const* prop) {
    while (prop && prop->target) prop = prop->target;
    return prop;
}

// The class a property is declared in (tsc's getDeclaringClass)
static Ty const* declaring_class(Checker* me, Prop const* prop) {
    Symbol const* parent = root_declaration(prop) ? root_declaration(prop)->parent : nullptr;
    return parent && (parent->flags & SYMBOL_CLASS) ? ty_declared_type_of(me, parent) : nullptr;
}

// tsc's propertyRelatedTo on modifiers: a private property is only related to itself, a protected one to one declared
// in a class derived from its class, and a protected source property isn't a public one
static bool modifiers_related(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, FoundProp const* source_prop, Prop const* target_prop,
    bool report
) {
    u32 source_flags = source_prop->flags;
    u32 target_flags = target_prop->flags;
    bool related = true;
    unsigned code = 0;
    if ((source_flags | target_flags) & PROP_PRIVATE) {
        related = source_prop->prop && root_declaration(source_prop->prop) == root_declaration(target_prop);
        code = (source_flags & target_flags & PROP_PRIVATE) ? 2442 : 2325;
    } else if (target_flags & PROP_PROTECTED) {
        Ty const* target_class = declaring_class(me, target_prop);
        Ty const* source_class = source_prop->prop ? declaring_class(me, source_prop->prop) : nullptr;
        related = !target_class || (source_class && ty_has_base_type(me, source_class, target_class));
        code = 2443;
    } else if (source_flags & PROP_PROTECTED) {
        related = false;
        code = 2444;
    }
    if (related || !report) return related;
    cstr name = ty_prop_name_cstr(me, target_prop->name);
    if (code == 2442) {
        report_line(st, code, cstr_d("Types have separate declarations of a private property '", name, "'."));
    } else if (code == 2325) {
        bool source_private = source_flags & PROP_PRIVATE;
        cstr private_str = display_for_report(me, source_private ? source : target, source_private);
        cstr other_str = display_for_report(me, source_private ? target : source, !source_private);
        report_line(st, code, cstr_d("Property '", name, "' is private in type '", private_str, "' but not in type '",
            other_str, "'."));
        c_drop(cstr, &private_str, &other_str);
    } else if (code == 2443) {
        Ty const* source_class = source_prop->prop ? declaring_class(me, source_prop->prop) : nullptr;
        cstr source_str = display_for_report(me, source_class ? source_class : source, true);
        cstr target_str = display_for_report(me, declaring_class(me, target_prop), false);
        report_line(st, code, cstr_d("Property '", name, "' is protected but type '", source_str,
            "' is not a class derived from '", target_str, "'."));
        c_drop(cstr, &source_str, &target_str);
    } else {
        cstr source_str = display_for_report(me, source, true);
        cstr target_str = display_for_report(me, target, false);
        report_line(st, code, cstr_d("Property '", name, "' is protected in type '", source_str, "' but public in type '",
            target_str, "'."));
        c_drop(cstr, &source_str, &target_str);
    }
    cstr_drop(&name);
    return false;
}

// tsc's shouldReportUnmatchedPropertyError: a function (or class) without properties isn't told it lacks the
// target's properties, unless the target has signatures of the same kind
static bool should_report_unmatched_property(Checker* me, Ty const* source, Ty const* target) {
    if (!(source->flags & TYF_OBJECT)) return true;
    Members const* members = ty_members(me, source);
    if (!(members->call_count || members->construct_count) || members->prop_count) return true;
    Members const* target_members = ty_members(me, target);
    return (target_members->call_count && members->call_count) ||
        (target_members->construct_count && members->construct_count);
}

static bool is_mutable_array_or_tuple(Checker* me, Ty const* ty);

// tsc's tryElaborateArrayLikeErrors, without its own reporting: whether a list of missing properties is worth showing
static bool elaborate_array_like(Checker* me, Ty const* source, Ty const* target) {
    bool target_array_like = ty_is_array(me, target) || is_tuple(target) || is_empty_tuple(target);
    if (is_tuple(source) || is_empty_tuple(source)) {
        TupleShape const* shape = is_empty_tuple(source) ? source->declared.tuple : source->reference.target->declared.tuple;
        if (shape->readonly && is_mutable_array_or_tuple(me, target)) return false;
        return target_array_like;
    }
    bool source_readonly_array = source->kind == TY_REFERENCE && source->reference.target == my readonly_array_target;
    if (source_readonly_array && is_mutable_array_or_tuple(me, target)) return false;
    if (is_tuple(target) || is_empty_tuple(target)) return ty_is_array(me, source);
    return true;
}

static Ternary props_related(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report, bool source_is_apparent
) {
    Members const* target_members = ty_members(me, target);
    bool comparable = st->relation == TY_RELATION_COMPARABLE;

    // A primitive is shown as itself, not as the global interface its properties come from
    if (report && !source_is_apparent && should_report_unmatched_property(me, source, target)) {
        // Missing properties get a message of their own instead of the generic head
        Atom const* missing[5];
        Prop const* missing_prop = nullptr;
        u32 missing_count = 0;
        for (u32 i = 0; i < target_members->prop_count; i++) {
            Prop const* target_prop = target_members->props[i];
            if ((target_prop->flags & PROP_OPTIONAL) || find_prop(me, source, target_prop->name, 0).found) continue;
            if (missing_count < c_arraylen(missing)) missing[missing_count] = target_prop->name;
            if (!missing_count) missing_prop = target_prop;
            missing_count++;
        }
        if (missing_count) {
            cstr source_str = display_for_report(me, source, true);
            cstr target_str = display_for_report(me, target, false);
            if (missing_count == 1) {
                cstr name = ty_prop_name_cstr(me, missing[0]);
                report_line(st, 2741, cstr_d(
                    "Property '", name, "' is missing in type '", source_str, "' but required in type '", target_str, "'."
                ));
                report_related(st, 2728, cstr_d("'", name, "' is declared here."), ty_prop_decl_span(missing_prop));
                cstr_drop(&name);
            } else if (!elaborate_array_like(me, source, target)) {
                // Nothing to say about an array against a tuple and the like but the head (tsc's
                // tryElaborateArrayLikeErrors)
                c_drop(cstr, &source_str, &target_str);
                return TERNARY_FALSE;
            } else {
                cstr names = cstr_null;
                u32 shown = missing_count > 5 ? 4 : missing_count;
                for (u32 i = 0; i < shown; i++) {
                    if (i) cstr_append_lit(&names, ", ");
                    cstr name = ty_prop_name_cstr(me, missing[i]);
                    cstr_append_s(&names, name);
                    cstr_drop(&name);
                }
                if (missing_count > 5) { // (tsc's arbitrary cutoff: the first four and a count, from six on)
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
            // Subtypes need the optional properties too, unless an object literal, [] or a tuple leaves them out
            // (tsc's requireOptionalProperties)
            if ((target_prop->flags & PROP_OPTIONAL) && (st->relation != TY_RELATION_STRICT_SUBTYPE || !requires_optional_properties(me, source))) {
                continue;
            }
            return TERNARY_FALSE;
        }
        if (!modifiers_related(me, st, source, target, &source_prop, target_prop, report)) return TERNARY_FALSE;
        if ((source_prop.flags & PROP_OPTIONAL) && !(target_prop->flags & PROP_OPTIONAL) && !comparable) {
            if (report) {
                cstr source_str = display_for_report(me, source, true);
                cstr target_str = display_for_report(me, target, false);
                cstr name = ty_prop_name_cstr(me, target_prop->name);
                report_line(st, 2327, cstr_d(
                    "Property '", name, "' is optional in type '", source_str, "' but required in type '", target_str, "'."
                ));
                c_drop(cstr, &source_str, &target_str, &name);
            }
            return TERNARY_FALSE;
        }
        Ty const* source_type = source_prop.type;
        Ty const* target_type = prop_type(me, target_prop);
        if ((target_prop->flags & PROP_OPTIONAL) && compiler_options()->strict_null_checks) {
            Ty const* parts[2] = { target_type, undefined };
            target_type = ty_union(me, parts, 2, nullptr, (TyList){0});
        }
        // (An optional source is only related to a required target when comparing: it may be undefined too)
        if ((source_prop.flags & PROP_OPTIONAL) && compiler_options()->strict_null_checks) {
            Ty const* source_parts[2] = { source_type, undefined };
            source_type = ty_union(me, source_parts, 2, nullptr, (TyList){0});
        }
        Ternary related = is_related_to(me, st, source_type, target_type, report);
        if (!related) {
            if (report) {
                cstr name = ty_prop_name_cstr(me, target_prop->name);
                report_line(st, 2326, cstr_d("Types of property '", name, "' are incompatible."));
                cstr_drop(&name);
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
        // The source's signature for the same key, else one that applies (a string index covers numbers)
        IndexInfo const* source_info = nullptr;
        for (u32 j = 0; j < source_members->index_count && !source_info; j++) {
            if (source_members->index[j].key == target_info->key) source_info = &source_members->index[j];
        }
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
        } else if (has_inferable_index(source)) {
            // An object literal type is related if each of its properties the signature applies to is
            for (u32 j = 0; j < source_members->prop_count; j++) {
                Prop const* prop = source_members->props[j];
                bool symbol_named = ty_unique_symbol_of_name(me, prop->name) != nullptr;
                if ((target_info->key->flags & TYF_NUMBER) && !is_numeric_prop_name(prop->name)) continue;
                if (((target_info->key->flags & TYF_ES_SYMBOL) != 0) != symbol_named) continue;
                Ternary related = is_related_to(me, st, prop_type(me, prop), target_info->value, report);
                if (!related) {
                    if (report) {
                        cstr name = ty_prop_name_cstr(me, prop->name);
                        report_line(st, 2530, cstr_d("Property '", name, "' is incompatible with index signature."));
                        cstr_drop(&name);
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

// A reference made by marker_reference() (tsc's isMarkerType): what variances are measured with
static bool is_marker_reference(Checker* me, Ty const* ty) {
    if (ty->kind != TY_REFERENCE || ty->reference.deferred || ty->reference.this_arg) return false;
    TyList params = ty->reference.target->declared.type_params;
    TyList args = ty_reference_args(me, ty);
    u32 markers = 0;
    for (u32 i = 0; i < args.len && i < params.len; i++) {
        if (args.data[i] == params.data[i]) continue;
        Ty const* arg = args.data[i];
        if (arg != variance_marker(me, 0) && arg != variance_marker(me, 1) && arg != variance_marker(me, 2)) return false;
        markers++;
    }
    return markers == 1;
}

// Null while the variances are being measured (the caller then goes by Ternary Unknown, like tsc)
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

static Ty const* best_matching_type(Checker* me, RelationState* st, Ty const* source, Ty const* target);

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
        // tsc elaborates on the member that's the likely intent
        Ty const* best = best_matching_type(me, st, source, target);
        if (best) is_related_to(me, st, source, best, true);
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

// A mapped type whose keys aren't known yet as the target (tsc's isGenericMappedType branch of
// structuredTypeRelatedTo): S is related to `{ [P in Q]: T }` if Q is related to keyof S and S[Q] to T, to
// `{ [P in Q]: X[P] }` if S is related to X. With `?`, the keys S has in common with Q are enough
// A mapper from the key type parameter of a mapped type to one constrained by the mapped type's constraint
static TyMapper const* mapped_key_param_mapper(Checker* me, MappedInfo const* mapped) {
    u64 key[2] = { KEY_MAPPED_KEY_PARAM, (u64)(uintptr_t)mapped };
    TyMapper const* ret = intern_get(&my links, key, 2);
    if (ret) return ret;
    Ty const* param = ty_synthetic_type_param(me, atom_sv(mapped->type_param->param.name));
    param->param.state->constraint = mapped->constraint;
    param->param.state->constraint_state = LAZY_STATE_DONE;
    ret = ty_mapper_new(me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &param, .len = 1 });
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

// tsc's mappedTypeRelatedTo: two mapped types whose keys aren't known relate by their modifiers (the target at least as
// optional), constraints (the other way around), name types (the same) and templates, once the source's key type
// parameter is the target's
static Ternary mapped_related(Checker* me, RelationState* st, MappedInfo const* source, MappedInfo const* target, bool report) {
    bool modifiers_related = st->relation == TY_RELATION_COMPARABLE || source->optional <= target->optional;
    if (!modifiers_related) return TERNARY_FALSE;
    Ternary result = is_related_to(me, st, target->constraint, source->constraint, report);
    if (!result) return TERNARY_FALSE;
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = &source->type_param, .len = 1 },
        (TyList){ .data = &target->type_param, .len = 1 });
    Ty const* source_name = source->name_type ? ty_instantiate(me, source->name_type, mapper) : nullptr;
    Ty const* target_name = target->name_type ? ty_instantiate(me, target->name_type, mapper) : nullptr;
    if (source_name != target_name) return TERNARY_FALSE;
    return result & is_related_to(me, st, ty_instantiate(me, source->template, mapper), target->template, report);
}

static Ternary generic_mapped_target_related(
    Checker* me, RelationState* st, Ty const* source, MappedInfo const* mapped, bool report
) {
    Ty const* template = mapped->template;
    if (source->kind == TY_ANONYMOUS && (source->flags & TYF_OBJECT) && ty_members(me, source)->mapped) {
        return mapped_related(me, st, ty_members(me, source)->mapped, mapped, report);
    }
    if (!mapped->name_type && template->kind == TY_INDEXED && template->indexed.object == source &&
        template->indexed.index == mapped->type_param
    ) {
        return TERNARY_TRUE;
    }
    if (mapped->optional < 0 || (source->kind == TY_ANONYMOUS && ty_members(me, source)->mapped)) return TERNARY_FALSE;
    Ty const* target_keys = mapped->name_type ? mapped->name_type : mapped->constraint;
    Ty const* source_keys = ty_keyof(me, source);
    TyMapper const* keyed = nullptr;
    if (mapped->name_type) {
        // The key as a type parameter constrained by what it iterates over (tsc's mapped type parameter): `T[P]
        // extends Function ? P : never` is some of `keyof T`
        keyed = mapped_key_param_mapper(me, mapped);
        target_keys = ty_instantiate(me, target_keys, keyed);
    }
    if (mapped->optional > 0 && keyed) {
        // The keys that apply: S[those] against the template
        Ty const* both[2] = { target_keys, source_keys };
        Ty const* common = ty_intersection(me, both, 2, nullptr, (TyList){0});
        if (common->flags & TYF_NEVER) return TERNARY_FALSE;
        return is_related_to(me, st, ty_indexed(me, source, common), ty_instantiate(me, template, keyed), report);
    }
    if (mapped->optional > 0) {
        Ty const* both[2] = { target_keys, source_keys };
        Ty const* common = ty_intersection(me, both, 2, nullptr, (TyList){0});
        if (common->flags & TYF_NEVER) return TERNARY_FALSE;
        // The keys that are known: S[K] against the template with P = K
        TyBuf known = {0};
        Ty const* const* members = &common;
        u32 count = 1;
        if (common->kind == TY_UNION) {
            members = common->composite.types.data;
            count = common->composite.types.len;
        }
        for (u32 i = 0; i < count; i++) {
            if (!(members[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) ty_buf_push(&known, members[i]);
        }
        Ternary related = TERNARY_FALSE;
        if (known.len && !mapped->name_type) {
            Ty const* keys = ty_union(me, known.data, known.len, nullptr, (TyList){0});
            TyMapper const* mapper = ty_mapper_new(
                me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &keys, .len = 1 }
            );
            related = is_related_to(me, st, ty_indexed(me, source, keys), ty_instantiate(me, template, mapper), report);
        }
        ty_buf_drop(&known);
        return related;
    }
    if (!is_related_to(me, st, target_keys, source_keys, false)) return TERNARY_FALSE;
    if (keyed) return is_related_to(me, st, ty_indexed(me, source, target_keys), ty_instantiate(me, template, keyed), report);
    if (!mapped->name_type && template->kind == TY_INDEXED && template->indexed.index == mapped->type_param) {
        return is_related_to(me, st, source, template->indexed.object, report);
    }
    // S[P] against the template, with P as Q on both sides (P has no constraint of its own here)
    TyMapper const* mapper = ty_mapper_new(
        me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &mapped->constraint, .len = 1 }
    );
    return is_related_to(me, st, ty_indexed(me, source, target_keys), ty_instantiate(me, template, mapper), report);
}

// What can be written to `T[K]` whatever T and K turn out to be (tsc's AccessFlags.Writing): through a union
// constraint of T, a value has to fit every member's property, so it's the intersection
static Ty const* write_constraint_of_indexed(Checker* me, Ty const* indexed) {
    Ty const* object = indexed->indexed.object;
    Ty const* object_constraint = object->kind == TY_TYPE_PARAM ? ty_param_constraint(me, object) : nullptr;
    if (object_constraint && object_constraint->kind == TY_UNION) {
        Ty const* index = indexed->indexed.index;
        Ty const* index_constraint = index->kind == TY_TYPE_PARAM ? ty_param_constraint(me, index) : nullptr;
        if (index_constraint) index = index_constraint;
        TyList members = object_constraint->composite.types;
        Ty const** parts = malloc(sizeof(Ty const*) * members.len);
        panic_if(!parts, "out of memory");
        bool resolved = true;
        for (u32 i = 0; i < members.len && resolved; i++) {
            parts[i] = ty_indexed(me, members.data[i], index);
            resolved = !(parts[i]->flags & TYF_DEFERRED) && parts[i]->kind != TY_TYPE_PARAM;
        }
        Ty const* ret = resolved ? ty_intersection(me, parts, members.len, nullptr, (TyList){0}) : nullptr;
        free(parts);
        return ret;
    }
    return constraint_of_indexed(me, indexed);
}

// A distributive conditional type with its check type replaced by the constraint (tsc's
// getConstraintOfDistributiveConditionalType): InstanceType<T> for `T extends typeof C` is C, `Extract<keyof T, string>`
// is string. The root's check type parameter is what gets mapped, so an instantiated check type like `keyof T` has its
// constraint distributed over. Null if that's no different
static Ty const* distributive_constraint(Checker* me, Ty const* conditional) {
    if (!conditional->conditional.distributive) return nullptr;
    Ty const* check = conditional->conditional.check;
    Ty const* check_constraint = check->kind == TY_TYPE_PARAM ? ty_param_constraint(me, check) :
        (check->flags & TYF_DEFERRED) ? ty_base_constraint(me, check) : nullptr;
    if (!check_constraint || check_constraint == check) return nullptr;
    Ty const* root = conditional->conditional.root ? conditional->conditional.root : conditional;
    Ty const* root_check = root->conditional.check;
    if (root_check->kind != TY_TYPE_PARAM) return nullptr;
    TyMapper const* outer = conditional->conditional.mapper;
    u32 outer_len = outer ? outer->len : 0;
    Ty const** sources = malloc(sizeof(Ty const*) * (outer_len + 1) * 2);
    panic_if(!sources, "out of memory");
    Ty const** targets = sources + outer_len + 1;
    u32 len = 0;
    for (u32 i = 0; i < outer_len; i++) {
        if (outer->sources[i] == root_check) continue;
        sources[len] = outer->sources[i];
        targets[len++] = outer->targets[i];
    }
    sources[len] = root_check;
    targets[len++] = check_constraint;
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len });
    free(sources);
    Ty const* instantiated = ty_instantiate(me, root, mapper);
    return instantiated != conditional && !(instantiated->flags & TYF_NEVER) ? instantiated : nullptr;
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
            Ty const* distributive = distributive_constraint(me, ty);
            if (distributive) {
                if (distributive->kind != TY_TYPE_PARAM && !(distributive->flags & TYF_DEFERRED)) return distributive;
                Ty const* deeper = base_constraint(me, distributive, depth + 1);
                if (deeper) return deeper;
            }
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
        case TY_TEMPLATE_LITERAL: {
            // The template of the constraints, string if one has none
            u32 len = ty->template_literal.types.len;
            Ty const** constraints = malloc(sizeof(Ty const*) * len);
            panic_if(!constraints, "out of memory");
            Ty const* ret = nullptr;
            for (u32 i = 0; i < len && !ret; i++) {
                Ty const* t = ty->template_literal.types.data[i];
                if (t->kind == TY_TYPE_PARAM || (t->flags & TYF_DEFERRED)) {
                    constraints[i] = base_constraint(me, t, depth + 1);
                    if (!constraints[i]) ret = ty_intrinsic(me, TY_STRING);
                } else if (t->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) {
                    Ty const* constraint = base_constraint(me, t, depth + 1);
                    constraints[i] = constraint ? constraint : t;
                } else {
                    constraints[i] = t;
                }
            }
            if (!ret) ret = ty_template_literal(me, ty->template_literal.texts, constraints, len);
            free(constraints);
            return ret == ty ? nullptr : ret;
        }
        case TY_STRING_MAPPING: {
            Ty const* inner = ty->string_mapping.type;
            Ty const* constraint = inner->kind == TY_TYPE_PARAM || (inner->flags & TYF_DEFERRED) ? base_constraint(me, inner, depth + 1) : nullptr;
            if (!constraint || constraint == inner) return ty_intrinsic(me, TY_STRING);
            Ty const* ret = ty_string_mapping(me, ty->string_mapping.mapping, ty->string_mapping.symbol, constraint);
            return ret == ty ? nullptr : ret;
        }
        default:
            return nullptr;
    }
}

Ty const* ty_base_constraint(Checker* me, Ty const* ty) { return base_constraint(me, ty, 0); }

static Ternary target_each_related(Checker* me, RelationState* st, Ty const* source, TyList targets, bool report) {
    Ternary result = TERNARY_TRUE;
    bool saved = st->intersection_target;
    st->intersection_target = true;
    for (u32 i = 0; i < targets.len && result; i++) {
        Ternary related = is_related_to(me, st, source, targets.data[i], report);
        result = related ? result & related : TERNARY_FALSE;
    }
    st->intersection_target = saved;
    return result;
}

static bool is_mutable_array_or_tuple(Checker* me, Ty const* ty) {
    if (ty->kind == TY_REFERENCE && ty->reference.target == my array_target) return true;
    return is_tuple(ty) && !ty->reference.target->declared.tuple->readonly;
}

// An intersection with type variables, with their constraints in their place. Null if it has none
static Ty const* intersection_constraint(Checker* me, Ty const* ty) {
    TyList members = ty->composite.types;
    bool any = false;
    for (u32 i = 0; i < members.len && !any; i++) any = members.data[i]->kind == TY_TYPE_PARAM || (members.data[i]->flags & TYF_DEFERRED);
    if (!any) return nullptr;
    TyBuf parts = {0};
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = members.data[i];
        if (member->kind == TY_TYPE_PARAM || (member->flags & TYF_DEFERRED)) {
            Ty const* constraint = base_constraint(me, member, 0);
            if (!constraint || constraint == member) {
                ty_buf_drop(&parts);
                return nullptr;
            }
            member = constraint;
        }
        ty_buf_push(&parts, member);
    }
    Ty const* ret = ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    return ret == ty ? nullptr : ret;
}

// `[...T]` with T generic (tsc's isSingleElementGenericTupleType)
static bool is_single_generic_tuple(Checker* me, Ty const* ty) {
    if (!is_tuple(ty)) return false;
    TupleShape const* shape = ty->reference.target->declared.tuple;
    if (shape->arity != 1 || !(shape->flags[0] & TUPLE_ELEMENT_REST)) return false;
    Ty const* element = ty_reference_args(me, ty).data[0];
    return element->kind == TY_TYPE_PARAM || (element->flags & TYF_DEFERRED);
}

// A source whose discriminants are unions is related to a union if each combination of their members is related to
// some member, and those members are related in the rest (tsc's typeRelatedToDiscriminatedType):
// `{ kind: A | B, x: X }` to `{ kind: A, x: X } | { kind: B, x: X }`
static Ternary related_to_discriminated(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    enum { MAX_COMBINATIONS = 25, MAX_DISCRIMINANTS = 8 };
    Ty const* apparent = apparent_type(me, source);
    if (!(apparent->flags & TYF_OBJECT)) return TERNARY_FALSE;
    // The object members of the target
    TyBuf objects = {0};
    for (u32 i = 0; i < target->composite.types.len; i++) {
        Ty const* member = target->composite.types.data[i];
        if (member->flags & (TYF_OBJECT | TYF_INTERSECTION)) ty_buf_push(&objects, member);
    }
    Ty const* object_target = objects.len >= 2 ? ty_union(me, objects.data, objects.len, nullptr, (TyList){0}) : nullptr;
    ty_buf_drop(&objects);
    if (!object_target || object_target->kind != TY_UNION) return TERNARY_FALSE;

    Members const* source_members = ty_members(me, apparent);
    Prop const* discriminants[MAX_DISCRIMINANTS];
    TyList values[MAX_DISCRIMINANTS];
    u32 count = 0;
    u32 combinations = 1;
    for (u32 i = 0; i < source_members->prop_count; i++) {
        Prop const* prop = source_members->props[i];
        if (!ty_is_discriminant_property(me, object_target, prop->name)) continue;
        if (count == MAX_DISCRIMINANTS) return TERNARY_FALSE;
        Ty const* type = prop_type(me, prop);
        if (type->kind == TY_UNION) {
            values[count] = type->composite.types;
        } else {
            Ty const** one = arena_new_n(&my arena, Ty const*, 1);
            one[0] = type;
            values[count] = (TyList){ .data = one, .len = 1 };
        }
        combinations *= values[count].len;
        if (combinations > MAX_COMBINATIONS) return TERNARY_FALSE;
        discriminants[count++] = prop;
    }
    if (!count) return TERNARY_FALSE;

    TyList members = object_target->composite.types;
    bool* matching = calloc(members.len, sizeof(bool));
    panic_if(!matching, "out of memory");
    Ternary result = TERNARY_TRUE;
    for (u32 c = 0; c < combinations && result; c++) {
        bool has_match = false;
        for (u32 m = 0; m < members.len; m++) {
            bool fits = true;
            u32 rest = c;
            for (u32 d = 0; d < count && fits; d++) {
                Ty const* value = values[d].data[rest % values[d].len];
                rest /= values[d].len;
                FoundProp target_prop = find_prop(me, members.data[m], discriminants[d]->name, 0);
                if (!target_prop.found) {
                    fits = false;
                    break;
                }
                fits = is_related_to(me, st, value, target_prop.type, false) != TERNARY_FALSE;
            }
            if (fits) {
                matching[m] = true;
                has_match = true;
            }
        }
        if (!has_match) result = TERNARY_FALSE;
    }
    // The rest of each matching member (the discriminants are related whatever their type, by the above). The
    // properties of an intersection member are those of its parts, as the intersection has them
    for (u32 m = 0; m < members.len && result; m++) {
        if (!matching[m]) continue;
        Ty const* member = members.data[m];
        TyList parts = member->kind == TY_INTERSECTION ? member->composite.types : (TyList){ .data = &members.data[m], .len = 1 };
        TyBuf seen = {0}; // Names already compared, as literal types
        for (u32 p = 0; p < parts.len && result; p++) {
            Ty const* part = apparent_type(me, parts.data[p]);
            if (!(part->flags & TYF_OBJECT)) continue;
            Members const* part_members = ty_members(me, part);
            for (u32 i = 0; i < part_members->prop_count && result; i++) {
                Atom const* name = part_members->props[i]->name;
                bool skip = false;
                for (u32 d = 0; d < count; d++) skip |= discriminants[d]->name == name;
                Ty const* name_type = ty_prop_name_literal(me, name);
                for (u32 j = 0; j < seen.len && !skip; j++) skip = seen.data[j] == name_type;
                if (skip) continue;
                ty_buf_push(&seen, name_type);
                FoundProp target_prop = find_prop(me, member, name, 0);
                if (!target_prop.found) continue;
                FoundProp source_prop = find_prop(me, apparent, name, 0);
                if (!source_prop.found) {
                    if (!(target_prop.flags & PROP_OPTIONAL)) result = TERNARY_FALSE;
                    continue;
                }
                if ((source_prop.flags & PROP_OPTIONAL) && !(target_prop.flags & PROP_OPTIONAL) && st->relation != TY_RELATION_COMPARABLE) {
                    result = TERNARY_FALSE;
                    continue;
                }
                Ty const* target_type = target_prop.type;
                Ty const* source_type = source_prop.type;
                if ((target_prop.flags & PROP_OPTIONAL) && compiler_options()->strict_null_checks) {
                    Ty const* target_parts[2] = { target_type, ty_intrinsic(me, TY_UNDEFINED) };
                    target_type = ty_union(me, target_parts, 2, nullptr, (TyList){0});
                    if (source_prop.flags & PROP_OPTIONAL) {
                        Ty const* source_parts[2] = { source_type, ty_intrinsic(me, TY_UNDEFINED) };
                        source_type = ty_union(me, source_parts, 2, nullptr, (TyList){0});
                    }
                }
                result &= is_related_to(me, st, source_type, target_type, false);
            }
            if (result) {
                result &= signatures_related(me, st, apparent, part, false, false);
                if (result) result &= signatures_related(me, st, apparent, part, true, false);
                if (result && !(is_tuple(apparent) && is_tuple(part))) result &= index_signatures_related(me, st, apparent, part, false);
            }
        }
        ty_buf_drop(&seen);
    }
    free(matching);
    return result;
}

// The identity relation (tsc's identityRelation in isRelatedTo and structuredTypeRelatedTo)

// What tsc's TypeFlags say about a type, as opposed to the facts cached in them
#define IDENTITY_FLAGS (~(u32)(TYF_ERROR | TYF_COULD_CONTAIN_TYPE_VARIABLES | TYF_FRESH_LITERAL | TYF_FRESH_OBJECT | \
    TYF_NON_INFERRABLE))

static Ternary signatures_identical_in(
    Checker* me, RelationState* st, Signature const* source, Signature const* target, bool partial, bool ignore_return
);
static Ternary recursive_related(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report);
static bool is_deeply_nested_max(Ty const* ty, Ty const* const* stack, u32 depth, u32 max);

static Ternary identity_related(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    if ((source->flags ^ target->flags) & IDENTITY_FLAGS) return TERNARY_FALSE;
    // tsc's TypeFlags.Object covers interfaces, references and type literals alike: `interface P { x: number }` is
    // identical to `{ x: number }`
    if (source->kind != target->kind && !(source->flags & TYF_OBJECT)) return TERNARY_FALSE;
    // Intrinsics of the same flags are the same (`any` and the error `any`), literals are interned, and a type
    // parameter is only identical to itself
    if (source->kind == TY_INTRINSIC) return TERNARY_TRUE;
    if (source->kind == TY_LITERAL || source->kind == TY_TYPE_PARAM) return TERNARY_FALSE;
    return recursive_related(me, st, source, target, false);
}

static Ternary identical_to_some(Checker* me, RelationState* st, Ty const* source, TyList targets) {
    for (u32 i = 0; i < targets.len; i++) {
        if (targets.data[i] == source) return TERNARY_TRUE;
    }
    for (u32 i = 0; i < targets.len; i++) {
        Ternary related = is_related_to(me, st, source, targets.data[i], false);
        if (related) return related;
    }
    return TERNARY_FALSE;
}

static Ternary each_identical_to_some(Checker* me, RelationState* st, TyList sources, TyList targets) {
    Ternary result = TERNARY_TRUE;
    for (u32 i = 0; i < sources.len && result; i++) result &= identical_to_some(me, st, sources.data[i], targets);
    return result;
}

static bool is_readonly_prop(Prop const* prop) {
    return (prop->flags & PROP_READONLY) || ((prop->flags & PROP_GETTER) && !(prop->flags & PROP_SETTER));
}

// tsc's compareProperties
// tsc's compareProperties, less the types
static bool prop_modifiers_identical(Prop const* source, Prop const* target) {
    u32 access = PROP_PRIVATE | PROP_PROTECTED;
    if ((source->flags & access) != (target->flags & access)) return false;
    if (source->flags & access) {
        if (root_declaration(source) != root_declaration(target)) return false;
    } else if ((source->flags ^ target->flags) & PROP_OPTIONAL) {
        return false;
    }
    return is_readonly_prop(source) == is_readonly_prop(target);
}

static Ternary props_identical(Checker* me, RelationState* st, Prop const* source, Prop const* target) {
    if (source == target) return TERNARY_TRUE;
    if (!prop_modifiers_identical(source, target)) return TERNARY_FALSE;
    return is_related_to(me, st, prop_type(me, source), prop_type(me, target), false);
}

bool ty_props_identical(Checker* me, Prop const* source, Prop const* target) {
    if (source == target) return true;
    return prop_modifiers_identical(source, target) &&
        ty_is_related(me, prop_type(me, source), prop_type(me, target), TY_RELATION_IDENTITY);
}

// tsc's mappedTypeRelatedTo under identity: the same modifiers, identical constraints, name types and templates once
// the source's key type parameter is the target's
static Ternary mapped_identical(Checker* me, RelationState* st, MappedInfo const* source, MappedInfo const* target) {
    if (source->readonly != target->readonly || source->optional != target->optional) return TERNARY_FALSE;
    Ternary result = is_related_to(me, st, target->constraint, source->constraint, false);
    if (!result) return TERNARY_FALSE;
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = &source->type_param, .len = 1 },
        (TyList){ .data = &target->type_param, .len = 1 });
    Ty const* source_name = source->name_type ? ty_instantiate(me, source->name_type, mapper) : nullptr;
    if (source_name != target->name_type) return TERNARY_FALSE;
    return result & is_related_to(me, st, ty_instantiate(me, source->template, mapper), target->template, false);
}

static Ternary signature_lists_identical(
    Checker* me, RelationState* st, Signature* const* source, u32 source_count, Signature* const* target, u32 target_count
) {
    if (source_count != target_count) return TERNARY_FALSE;
    Ternary result = TERNARY_TRUE;
    for (u32 i = 0; i < source_count && result; i++) {
        result &= signatures_identical_in(me, st, source[i], target[i], false, false);
    }
    return result;
}

static Ternary objects_identical(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    Members const* source_members = ty_members(me, source);
    Members const* target_members = ty_members(me, target);
    if (source_members->mapped || target_members->mapped) {
        if (!source_members->mapped || !target_members->mapped) return TERNARY_FALSE;
        return mapped_identical(me, st, source_members->mapped, target_members->mapped);
    }
    // Two references to one generic type (or tuples of one shape): their type arguments
    if (source->kind == TY_REFERENCE && target->kind == TY_REFERENCE && source->reference.target == target->reference.target) {
        TyList source_args = ty_reference_args(me, source);
        TyList target_args = ty_reference_args(me, target);
        Ternary result = TERNARY_TRUE;
        for (u32 i = 0; i < source_args.len && result; i++) result &= is_related_to(me, st, source_args.data[i], target_args.data[i], false);
        Ty const* source_this = source->reference.this_arg;
        Ty const* target_this = target->reference.this_arg;
        if (result && source_this != target_this) {
            result &= is_related_to(me, st, source_this ? source_this : source, target_this ? target_this : target, false);
        }
        return result;
    }
    bool source_tuple = is_tuple(source) || is_empty_tuple(source);
    bool target_tuple = is_tuple(target) || is_empty_tuple(target);
    if (source_tuple || target_tuple) {
        // (Tuples of different shapes: labels aside, not identical)
        if (!source_tuple || !target_tuple || is_empty_tuple(source) || is_empty_tuple(target)) return TERNARY_FALSE;
        TupleShape const* source_shape = source->reference.target->declared.tuple;
        TupleShape const* target_shape = target->reference.target->declared.tuple;
        if (source_shape->arity != target_shape->arity || source_shape->readonly != target_shape->readonly ||
            memcmp(source_shape->flags, target_shape->flags, source_shape->arity) != 0
        ) {
            return TERNARY_FALSE;
        }
        TyList source_args = ty_reference_args(me, source);
        TyList target_args = ty_reference_args(me, target);
        Ternary result = TERNARY_TRUE;
        for (u32 i = 0; i < source_args.len && result; i++) result &= is_related_to(me, st, source_args.data[i], target_args.data[i], false);
        return result;
    }
    // Structurally: the same properties, signatures and index signatures, each identical (tsc's propertiesIdenticalTo,
    // signaturesIdenticalTo, indexSignaturesIdenticalTo)
    if (source_members->prop_count != target_members->prop_count) return TERNARY_FALSE;
    Ternary result = TERNARY_TRUE;
    for (u32 i = 0; i < source_members->prop_count && result; i++) {
        Prop const* source_prop = source_members->props[i];
        Prop const* target_prop = ty_get_prop(me, target, source_prop->name);
        if (!target_prop) return TERNARY_FALSE;
        result &= props_identical(me, st, source_prop, target_prop);
    }
    if (result) {
        result &= signature_lists_identical(me, st, source_members->call, source_members->call_count, target_members->call,
            target_members->call_count);
    }
    if (result) {
        result &= signature_lists_identical(me, st, source_members->construct, source_members->construct_count,
            target_members->construct, target_members->construct_count);
    }
    if (result && source_members->index_count != target_members->index_count) return TERNARY_FALSE;
    for (u32 i = 0; i < target_members->index_count && result; i++) {
        IndexInfo const* target_info = &target_members->index[i];
        IndexInfo const* source_info = nullptr;
        for (u32 j = 0; j < source_members->index_count && !source_info; j++) {
            if (source_members->index[j].key == target_info->key) source_info = &source_members->index[j];
        }
        if (!source_info || source_info->readonly != target_info->readonly) return TERNARY_FALSE;
        result &= is_related_to(me, st, source_info->value, target_info->value, false);
    }
    return result;
}

static Ternary identity_structured(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    Ternary result = TERNARY_FALSE;
    switch ((TyKind)source->kind) {
        case TY_UNION:
        case TY_INTERSECTION:
            result = each_identical_to_some(me, st, source->composite.types, target->composite.types);
            if (result) result &= each_identical_to_some(me, st, target->composite.types, source->composite.types);
            return result;
        case TY_KEYOF:
            return is_related_to(me, st, source->keyof.operand, target->keyof.operand, false);
        case TY_INDEXED:
            result = is_related_to(me, st, source->indexed.object, target->indexed.object, false);
            if (result) result &= is_related_to(me, st, source->indexed.index, target->indexed.index, false);
            return result;
        case TY_CONDITIONAL:
            if (source->conditional.distributive != target->conditional.distributive) return TERNARY_FALSE;
            result = is_related_to(me, st, source->conditional.check, target->conditional.check, false);
            if (result) result &= is_related_to(me, st, source->conditional.extends, target->conditional.extends, false);
            if (result) {
                result &= is_related_to(me, st, ty_conditional_branch(me, source, true), ty_conditional_branch(me, target, true), false);
            }
            if (result) {
                result &= is_related_to(me, st, ty_conditional_branch(me, source, false), ty_conditional_branch(me, target, false), false);
            }
            return result;
        case TY_TEMPLATE_LITERAL: {
            TyList source_types = source->template_literal.types;
            TyList target_types = target->template_literal.types;
            if (source_types.len != target_types.len) return TERNARY_FALSE;
            for (u32 i = 0; i <= source_types.len; i++) {
                if (source->template_literal.texts[i] != target->template_literal.texts[i]) return TERNARY_FALSE;
            }
            result = TERNARY_TRUE;
            for (u32 i = 0; i < source_types.len && result; i++) result &= is_related_to(me, st, source_types.data[i], target_types.data[i], false);
            return result;
        }
        case TY_STRING_MAPPING:
            if (source->string_mapping.mapping != target->string_mapping.mapping) return TERNARY_FALSE;
            return is_related_to(me, st, source->string_mapping.type, target->string_mapping.type, false);
        case TY_DECLARED:
        case TY_REFERENCE:
        case TY_ANONYMOUS:
            return objects_identical(me, st, source, target);
        default:
            return TERNARY_FALSE;
    }
}

// The properties of an intersection target, each the intersection of its members' (tsc's propertiesRelatedTo on the
// intersection with IntersectionState.None)
static Ternary intersection_props_related(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    Ternary result = TERNARY_TRUE;
    TyList members = target->composite.types;
    for (u32 m = 0; m < members.len && result; m++) {
        Members const* member = ty_members(me, members.data[m]);
        for (u32 i = 0; i < member->prop_count && result; i++) {
            Atom const* name = member->props[i]->name;
            // Each name once: at the first member that has it
            bool seen = false;
            for (u32 earlier = 0; earlier < m && !seen; earlier++) seen = ty_get_prop(me, members.data[earlier], name) != nullptr;
            if (seen) continue;
            // (Only the members' own properties, not Object's: tsc's skipObjectFunctionPropertyAugment)
            Ty const* parts[16];
            u32 part_count = 0;
            bool all_optional = true;
            for (u32 j = m; j < members.len && part_count < c_arraylen(parts); j++) {
                Prop const* part = ty_get_prop(me, members.data[j], name);
                if (!part) continue;
                parts[part_count++] = prop_type(me, part);
                all_optional &= (part->flags & PROP_OPTIONAL) != 0;
            }
            FoundProp target_prop = { .found = true, .flags = all_optional ? PROP_OPTIONAL : 0,
                .type = part_count == 1 ? parts[0] : ty_intersection(me, parts, part_count, nullptr, (TyList){0}) };
            FoundProp source_prop = find_prop(me, apparent_type(me, source), name, 0);
            if (!source_prop.found) continue; // (Missing ones were the members' business)
            Ty const* target_type = target_prop.type;
            Ty const* source_type = source_prop.type;
            if (compiler_options()->strict_null_checks) {
                Ty const* undefined = ty_intrinsic(me, TY_UNDEFINED);
                if (target_prop.flags & PROP_OPTIONAL) target_type = ty_union(me, (Ty const*[]){ target_type, undefined }, 2, nullptr, (TyList){0});
                if (source_prop.flags & PROP_OPTIONAL) source_type = ty_union(me, (Ty const*[]){ source_type, undefined }, 2, nullptr, (TyList){0});
            }
            Ternary related = is_related_to(me, st, source_type, target_type, report);
            if (!related && report) {
                cstr prop_name = ty_prop_name_cstr(me, name);
                report_line(st, 2326, cstr_d("Types of property '", prop_name, "' are incompatible."));
                cstr_drop(&prop_name);
            }
            result &= related;
        }
    }
    return result;
}

static Ternary structured_related(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    if (st->relation == TY_RELATION_IDENTITY) return identity_structured(me, st, source, target);
    bool comparable = st->relation == TY_RELATION_COMPARABLE;

    // Unions and intersections
    if (source->kind == TY_UNION) {
        if (comparable) return some_related(me, st, source->composite.types, target);
        return each_related(me, st, source->composite.types, target, report);
    }
    bool source_is_generic = source->kind == TY_TYPE_PARAM || (source->flags & TYF_DEFERRED) != 0;
    if (target->kind == TY_UNION) {
        // A generic source can still be related through its constraint, further down. The members of the union don't
        // check excess properties, the union as a whole did (tsc relates the regular object literal)
        isize_t saved = vec_ty_report_line_size(&st->lines);
        Ternary related = related_to_some(me, st, ty_widened_objects(me, source), target, report && !source_is_generic);
        if (!related && (source->flags & TYF_OBJECT)) {
            related = related_to_discriminated(me, st, source, target);
            if (related) report_truncate(st, saved);
        }
        if (related || (!source_is_generic && source->kind != TY_INTERSECTION)) return related;
    }
    if (target->kind == TY_INTERSECTION) {
        Ternary result = target_each_related(me, st, source, target->composite.types, report);
        // The properties of the intersection as a whole too, as if not in one (tsc: IntersectionState.None): the
        // weak type and excess property checks of what they combine to
        bool generic = false;
        for (u32 i = 0; i < target->composite.types.len; i++) {
            Ty const* member = target->composite.types.data[i];
            generic |= member->kind == TY_TYPE_PARAM || (member->flags & TYF_DEFERRED) || !(member->flags & TYF_OBJECT) ||
                ty_members(me, member)->mapped;
        }
        if (result && !st->intersection_target && !generic && (source->flags & TYF_OBJECT || source->kind == TY_INTERSECTION)) {
            result &= intersection_props_related(me, st, source, target, report);
        }
        return result;
    }
    if (source->kind == TY_INTERSECTION) {
        Ternary related = some_related(me, st, source->composite.types, target);
        if (related) return related;
        // The members' constraints together may be related (tsc's getEffectiveConstraintOfIntersection): `T & {}` with
        // `T extends Node | undefined` is a Node
        Ty const* constraint = intersection_constraint(me, source);
        if (constraint) {
            related = is_related_to(me, st, constraint, target, false);
            if (related) return related;
        }
        // Otherwise the members may still add up to the target, property by property (below)
    }
    // `[...T]` is T
    if (is_single_generic_tuple(me, source) && !source->reference.target->declared.tuple->readonly) {
        Ternary related = is_related_to(me, st, ty_reference_args(me, source).data[0], target, false);
        if (related) return related;
    }
    if (is_single_generic_tuple(me, target)) {
        Ty const* constraint = base_constraint(me, source, 0);
        if (!constraint) constraint = source;
        if (target->reference.target->declared.tuple->readonly || is_mutable_array_or_tuple(me, constraint)) {
            Ternary related = is_related_to(me, st, source, ty_reference_args(me, target).data[0], false);
            if (related) return related;
        }
    }

    // Generic targets
    if (my variance_depth && ((source->flags | target->flags) & TYF_DEFERRED)) my variance_unreliable = true;
    switch ((TyKind)target->kind) {
        case TY_TYPE_PARAM: {
            // `{ [P in Q]: X }` is related to T if keyof T is related to Q and X to T[P] (`Mutable<T>` to T)
            MappedInfo const* mapped = source->kind == TY_ANONYMOUS ? ty_members(me, source)->mapped : nullptr;
            if (mapped && !mapped->name_type && mapped->optional <= 0 &&
                is_related_to(me, st, ty_keyof(me, target), mapped->constraint, false)
            ) {
                Ternary related = is_related_to(me, st, mapped->template, ty_indexed(me, target, mapped->type_param), report);
                if (related) return related;
            }
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
            Ty const* constraint = write_constraint_of_indexed(me, target);
            if (constraint) {
                Ternary related = is_related_to(me, st, source, constraint, false);
                if (related) return related;
            }
        }   break;

        case TY_CONDITIONAL: {
            // Identical extends types, check types related one way or the other, and related branches (tsc's rule for
            // two conditional types; not inferring the source's `infer` type parameters from the target's extends type)
            if (source->kind == TY_CONDITIONAL && ty_is_related(me, source->conditional.extends, target->conditional.extends, TY_RELATION_IDENTITY)) {
                Ternary related = is_related_to(me, st, source->conditional.check, target->conditional.check, false);
                if (!related) related = is_related_to(me, st, target->conditional.check, source->conditional.check, false);
                if (related) related &= is_related_to(me, st, ty_conditional_branch(me, source, true), ty_conditional_branch(me, target, true), false);
                if (related) related &= is_related_to(me, st, ty_conditional_branch(me, source, false), ty_conditional_branch(me, target, false), false);
                if (related) return related;
            }
            // Related to both branches is related to whichever gets picked
            Ternary related = is_related_to(me, st, source, ty_conditional_branch(me, target, true), false);
            if (related) related &= is_related_to(me, st, source, ty_conditional_branch(me, target, false), false);
            if (related) return related;
        }   break;

        case TY_TEMPLATE_LITERAL:
            if (source->kind == TY_TEMPLATE_LITERAL) {
                if (comparable) return ty_templates_definitely_unrelated(source, target) ? TERNARY_FALSE : TERNARY_TRUE;
                // `foo-${number}` is related to `foo-${string}` though number isn't related to string
                if (my variance_depth) my variance_unreliable = true;
            }
            if (ty_template_matches(me, source, target)) return TERNARY_TRUE;
            break;

        case TY_STRING_MAPPING:
            if (source->kind != TY_STRING_MAPPING && ty_string_mapping_contains(me, source, target)) return TERNARY_TRUE;
            break;

        default: break;
    }

    // A mapped type whose keys aren't known yet (target side first, like in tsc)
    MappedInfo const* target_mapped = target->kind == TY_ANONYMOUS ? ty_members(me, target)->mapped : nullptr;
    if (target_mapped && !my variance_depth) {
        Ternary related = generic_mapped_target_related(me, st, source, target_mapped, report);
        // HACK: a generic source falls through to the member-by-member comparison, which a mapped type without
        // members passes: it stands in for tsc's rules between two generic mapped types (mappedTypeRelatedTo), a
        // homomorphic Readonly<T> and T, and the like, which don't exist here yet
        if (related || !(source->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return related;
    }

    // Generic sources
    switch ((TyKind)source->kind) {
        case TY_TYPE_PARAM: {
            Ty const* constraint = ty_param_constraint(me, source);
            if (!constraint || constraint == source || (constraint->flags & TYF_ANY)) {
                if (target->kind == TY_TYPE_PARAM || (target->flags & TYF_DEFERRED)) return TERNARY_FALSE;
                // Comparable: as `unknown` (tsc's getConstraintOfType(source) || unknownType), which is comparable to
                // anything
                if (st->relation == TY_RELATION_COMPARABLE) return is_related_to(me, st, ty_intrinsic(me, TY_UNKNOWN_), target, false);
                // An unconstrained type parameter is related to `{}`, but not to `object`
                if (target->flags & TYF_NON_PRIMITIVE) return TERNARY_FALSE;
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
            // Already checking this conditional type as a source further up: don't recurse (tsc's
            // isDeeplyNestedType(source, sourceStack, sourceDepth, 10); it is on the stack already)
            if (st->depth && is_deeply_nested_max(source, st->source_stack, st->depth - 1, 10)) return TERNARY_MAYBE;
            Ternary related = is_related_to(me, st, ty_conditional_branch(me, source, true), target, false);
            if (related) related &= is_related_to(me, st, ty_conditional_branch(me, source, false), target, false);
            if (related) return related;
            // Through the distributive constraint (not to another conditional type, like in tsc)
            if (target->kind != TY_CONDITIONAL) {
                Ty const* constraint = distributive_constraint(me, source);
                if (constraint) {
                    related = is_related_to(me, st, constraint, target, false);
                    if (related) return related;
                }
            }
            related = is_related_to(me, st, ty_conditional_branch(me, source, true), target, report);
            if (related) related &= is_related_to(me, st, ty_conditional_branch(me, source, false), target, report);
            return related;
        }

        case TY_TEMPLATE_LITERAL:
            if (!(target->flags & (TYF_OBJECT | TYF_TEMPLATE_LITERAL))) {
                Ty const* constraint = base_constraint(me, source, 0);
                if (constraint && constraint != source) {
                    Ternary related = is_related_to(me, st, constraint, target, report);
                    if (related) return related;
                }
            }
            break;

        case TY_STRING_MAPPING:
            if (target->kind == TY_STRING_MAPPING) {
                if (source->string_mapping.mapping != target->string_mapping.mapping) return TERNARY_FALSE;
                Ternary related = is_related_to(me, st, source->string_mapping.type, target->string_mapping.type, report);
                if (related) return related;
            } else {
                Ty const* constraint = base_constraint(me, source, 0);
                if (constraint && constraint != source) {
                    Ternary related = is_related_to(me, st, constraint, target, report);
                    if (related) return related;
                }
            }
            break;

        default: break;
    }
    if (target->kind == TY_TYPE_PARAM || (target->flags & TYF_DEFERRED) || target->kind == TY_UNION) return TERNARY_FALSE;

    // Object types from here on
    if (!(target->flags & TYF_OBJECT)) return TERNARY_FALSE;

    bool source_is_tuple = is_tuple(source) || is_empty_tuple(source);
    bool target_is_tuple = is_tuple(target) || is_empty_tuple(target);
    if (source_is_tuple && target_is_tuple) return tuples_related(me, st, source, target, report);
    if (target_is_tuple) {
        TupleShape const* shape = is_empty_tuple(target) ? target->declared.tuple : target->reference.target->declared.tuple;
        if (source->kind == TY_REFERENCE && is_array_target(me, source->reference.target)) {
            u32 min = 0;
            for (u32 i = 0; i < shape->arity; i++) {
                if (!(shape->flags[i] & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST))) min = i + 1;
            }
            if (report && min) {
                report_line(st, 2620, cstr_d("Target requires ", min, " element(s) but source may have fewer."));
            }
            return TERNARY_FALSE;
        }
        // Anything else is compared structurally, unless the tuple has a variable length (tsc's propertiesRelatedTo):
        // `interface I extends [string, number] { x: string }` is one
        for (u32 i = 0; i < shape->arity; i++) {
            if (shape->flags[i] & TUPLE_ELEMENT_REST) return TERNARY_FALSE;
        }
    }
    if (source_is_tuple) source = tuple_as_array(me, source);

    // A class or interface with a `this` type is a reference to itself (tsc: its declared type is one)
    bool source_self = source->kind == TY_DECLARED && source->declared.this_param;
    bool target_self = target->kind == TY_DECLARED && target->declared.this_param;
    if ((source->kind == TY_REFERENCE || source_self) && (target->kind == TY_REFERENCE || target_self)) {
        Ty const* source_target = source_self ? source : source->reference.target;
        Ty const* target_target = target_self ? target : target->reference.target;
        if (source_target == target_target) {
            Variances const* variances = get_variances(me, source_target);
            // A recursive measurement of the same variances: tsc assumes related (Ternary.Unknown), which measures
            // variance only from occurrences that aren't nested in recursive instantiations of the type
            if (!variances && !is_marker_reference(me, source) && !is_marker_reference(me, target) &&
                !is_array_target(me, source_target) && source_target->declared.kind != TY_DECLARED_TUPLE_TARGET
            ) {
                return TERNARY_MAYBE;
            }
            if (variances) {
                bool unreliable;
                isize_t saved = vec_ty_report_line_size(&st->lines);
                TyList source_args = source_self ? source->declared.type_params : ty_reference_args(me, source);
                TyList target_args = target_self ? target->declared.type_params : ty_reference_args(me, target);
                isize_t args_saved = vec_ty_report_line_size(&st->lines);
                Ternary related = type_args_related(me, st, variances, source_args, target_args, false, &unreliable);
                // A covariant `void` type argument of the target (`B<number>` to `B<void>`) falls back to comparing
                // structurally, where a `void` return takes anything (tsc's hasCovariantVoidArgument)
                if (!related) {
                    for (u32 i = 0; i < target_args.len && i < variances->len; i++) {
                        if ((variances->data[i] & (VARIANCE_BIVARIANT | VARIANCE_INDEPENDENT)) == VARIANCE_COVARIANT &&
                            (target_args.data[i]->flags & TYF_VOID)
                        ) {
                            unreliable = true;
                        }
                    }
                }
                if (!related && report && !unreliable) {
                    report_truncate(st, args_saved);
                    related = type_args_related(me, st, variances, source_args, target_args, true, &unreliable);
                }
                // The this-argument is one more covariant type argument (a reference without one is its own)
                Ty const* source_this = source->kind == TY_REFERENCE ? source->reference.this_arg : nullptr;
                Ty const* target_this = target->kind == TY_REFERENCE ? target->reference.this_arg : nullptr;
                if (related && source_target->declared.this_param && (source_this || target_this) && source_this != target_this) {
                    related &= is_related_to(me, st, source_this ? source_this : source, target_this ? target_this : target, report);
                }
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
    if (my variance_depth && target_mapped) my variance_unreliable = true;
    if (apparent->kind == TY_INTERSECTION) {
        // HACK: the signatures and index signatures of an intersection aren't combined (tsc concatenates signatures
        // and intersects index infos), each kind has to be satisfied by one member on its own
        Ternary result = props_related(me, st, apparent, target, report, false);
        Members const* target_members = ty_members(me, target);
        TyList parts = apparent->composite.types;
        for (u32 kind = 0; kind < 3 && result; kind++) {
            u32 count = kind == 0 ? target_members->call_count : kind == 1 ? target_members->construct_count : target_members->index_count;
            if (!count) continue;
            Ternary some = TERNARY_FALSE;
            for (u32 i = 0; i < parts.len && !some; i++) {
                Ty const* part = apparent_type(me, parts.data[i]);
                if (!(part->flags & TYF_OBJECT)) continue;
                some = kind == 2 ? index_signatures_related(me, st, part, target, false) :
                    signatures_related(me, st, part, target, kind == 1, false);
            }
            result &= some;
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
        case TY_CONDITIONAL: return ty->conditional.root;
        case TY_INDEXED: {
            Ty const* object = ty;
            while (object->kind == TY_INDEXED) object = object->indexed.object;
            return object;
        }
        default: return nullptr;
    }
}

// tsc's isDeeplyNestedType: `ty` (about to go on the stack) and at least two types further up with the same recursion
// identity, each newer (a higher id) than the previous one
bool ty_is_deeply_nested(Ty const* ty, Ty const* const* stack, u32 depth) {
    return is_deeply_nested_max(ty, stack, depth, 3);
}

static bool is_deeply_nested_max(Ty const* ty, Ty const* const* stack, u32 depth, u32 max) {
    Ty const* identity = recursion_identity(ty);
    if (!identity || depth + 1 < max) return false;
    u32 count = 0;
    u32 last_id = 0;
    for (u32 i = 0; i <= depth; i++) {
        Ty const* other = i < depth ? stack[i] : ty;
        if (recursion_identity(other) != identity) continue;
        if (other->id >= last_id && ++count >= max) return true;
        last_id = other->id;
    }
    return false;
}

static Ternary recursive_related(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    RelationCache* cache = &my relations[st->relation];
    u64 key = relation_key(source, target);
    // Relating to an intersection's members is a question of its own (tsc's getRelationKey postfix): no excess property
    // checks, and a pair compared inside it isn't the same pair on the stack outside it
    if (st->intersection_target) key ^= 1ull << 63;
    unsigned cached = relation_cache_get(cache, key);
    if (cached == CACHE_SUCCEEDED) return TERNARY_TRUE;
    // A cached failure is computed again only to explain it, and then a pair on the stack is still assumed related.
    // Which comparisons explain (`report`) decides what is blamed, so it follows tsc closely
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
    if (!(st->expanding & 1) && ty_is_deeply_nested(source, st->source_stack, st->depth)) st->expanding |= 1;
    if (!(st->expanding & 2) && ty_is_deeply_nested(target, st->target_stack, st->depth)) st->expanding |= 2;
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

// Excess property checks (tsc's hasExcessProperties and friends)

static bool union_has_member(Ty const* u, Ty const* t) {
    for (u32 i = 0; i < u->composite.types.len; i++) {
        if (u->composite.types.data[i] == t) return true;
    }
    return false;
}

static bool is_excess_property_check_target(Ty const* ty) {
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        bool any = false, all = true;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            bool is = is_excess_property_check_target(ty->composite.types.data[i]);
            any |= is;
            all &= is;
        }
        return ty->kind == TY_UNION ? any : all;
    }
    return (ty->flags & (TYF_OBJECT | TYF_NON_PRIMITIVE)) != 0;
}

// tsc's isEmptyObjectType
static bool is_empty_object_type(Checker* me, Ty const* ty) {
    if (ty->flags & TYF_NON_PRIMITIVE) return true;
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        bool any = false, all = true;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            bool is = is_empty_object_type(me, ty->composite.types.data[i]);
            any |= is;
            all &= is;
        }
        return ty->kind == TY_UNION ? any : all;
    }
    if (!(ty->flags & TYF_OBJECT)) return false;
    Members const* members = ty_members(me, ty);
    return !members->mapped && !members->prop_count && !members->call_count && !members->construct_count &&
        !members->index_count;
}

bool ty_is_numeric_name(Atom const* name) {
    if (!name->len) return false;
    char* end = nullptr;
    char buf[64];
    if (name->len >= sizeof(buf)) return false;
    memcpy(buf, name->str, name->len);
    buf[name->len] = 0;
    double value = strtod(buf, &end);
    if (*end) return false;
    // tsc's isNumericLiteralName: the name is what the number prints as
    cstr printed = cstr_null;
    cstr_append_value_js(&printed, &(Value){ .tag = VALUE_NUMBER, .number = value });
    csview printed_sv = cstr_sv(&printed);
    csview name_sv = atom_sv(name);
    bool ret = csview_eq(&printed_sv, &name_sv);
    cstr_drop(&printed);
    return ret;
}

// tsc's getApplicableIndexInfoForName (a string index also admits unique symbol names, for backwards compatibility)
static bool has_index_for_name(Checker* me, Members const* members, Atom const* name) {
    for (u32 i = 0; i < members->index_count; i++) {
        Ty const* key = members->index[i].key;
        if (key->flags & TYF_STRING) return true;
        if ((key->flags & TYF_NUMBER) && ty_is_numeric_name(name)) return true;
        if ((key->flags & TYF_ES_SYMBOL) && ty_unique_symbol_of_name(me, name)) return true;
        if (!(key->flags & (TYF_NUMBER | TYF_ES_SYMBOL))) return true; // (Patterns tttt doesn't have: let them through)
    }
    return false;
}

// tsc's isKnownProperty
static bool is_known_property(Checker* me, Ty const* target, Atom const* name) {
    if (target->flags & TYF_OBJECT) {
        Members const* members = ty_members(me, target);
        if (members->mapped) return true; // TODO: the keys of a mapped type
        if (ty_get_prop(me, target, name) || has_index_for_name(me, members, name)) return true;
    }
    if ((target->kind == TY_UNION || target->kind == TY_INTERSECTION) && is_excess_property_check_target(target)) {
        for (u32 i = 0; i < target->composite.types.len; i++) {
            if (is_known_property(me, target->composite.types.data[i], name)) return true;
        }
    }
    return false;
}

// The type of a property of an object type (its own, not Object's) or of the index signature that admits the name
static Ty const* prop_or_index_type(Checker* me, Ty const* ty, Atom const* name) {
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) return ty_access(me, ty, ty_prop_name_literal(me, name));
    if (!(ty->flags & TYF_OBJECT)) return nullptr;
    Prop const* prop = ty_get_prop(me, ty, name);
    if (prop && (prop->flags & PROP_OPTIONAL) && compiler_options()->strict_null_checks) {
        Ty const* parts[2] = { prop_type(me, prop), ty_intrinsic(me, TY_UNDEFINED) };
        return ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    if (prop) return prop_type(me, prop);
    Members const* members = ty_members(me, ty);
    for (u32 i = 0; i < members->index_count; i++) {
        Ty const* key = members->index[i].key;
        if (((key->flags & TYF_NUMBER) && ty_is_numeric_name(name)) ||
            ((key->flags & TYF_ES_SYMBOL) && ty_unique_symbol_of_name(me, name))
        ) {
            return members->index[i].value;
        }
    }
    for (u32 i = 0; i < members->index_count; i++) {
        if (members->index[i].key->flags & TYF_STRING) return members->index[i].value;
    }
    return nullptr;
}

static bool related_for_matching(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    if (st) return is_related_to(me, st, source, target, false) != TERNARY_FALSE;
    return ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE);
}

// tsc's discriminateTypeByDiscriminableItems over the source's discriminant properties (findMatchingDiscriminantType
// without the key property map): the members of a union target whose discriminants the source's fit, else the target.
// `*discriminants`: whether the source had any. Without `st`, relates by assignability
static Ty const* discriminated_union_target(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool* discriminants
) {
    *discriminants = false;
    TyList types = target->composite.types;
    unsigned char include_buf[64];
    unsigned char* include = types.len <= sizeof(include_buf) ? include_buf : malloc(types.len);
    for (u32 i = 0; i < types.len; i++) include[i] = !(types.data[i]->kind == TY_INTRINSIC || types.data[i]->kind == TY_LITERAL);
    Members const* members = ty_members(me, source);
    bool discriminated = false;
    for (u32 p = 0; p < members->prop_count; p++) {
        Prop const* prop = members->props[p];
        if (!ty_is_discriminant_property(me, target, prop->name)) continue;
        *discriminants = true;
        Ty const* prop_ty = prop_type(me, prop);
        TyList parts = prop_ty->kind == TY_UNION ? prop_ty->composite.types : (TyList){ .data = &prop_ty, .len = 1 };
        bool matched = false;
        for (u32 i = 0; i < types.len; i++) {
            if (!include[i]) continue;
            Ty const* target_prop = prop_or_index_type(me, types.data[i], prop->name);
            bool related = false;
            for (u32 j = 0; target_prop && j < parts.len && !related; j++) {
                related = related_for_matching(me, st, parts.data[j], target_prop);
            }
            if (related) matched = true;
            else include[i] = 2; // Maybe
        }
        for (u32 i = 0; i < types.len; i++) {
            if (include[i] == 2) include[i] = !matched;
        }
    }
    TyBuf kept = {0};
    for (u32 i = 0; i < types.len; i++) {
        if (include[i]) ty_buf_push(&kept, types.data[i]);
        else discriminated = true;
    }
    Ty const* ret = target;
    if (discriminated && kept.len) ret = ty_union(me, kept.data, kept.len, nullptr, (TyList){0});
    ty_buf_drop(&kept);
    if (include != include_buf) free(include);
    return ret;
}

// tsc's findMatchingDiscriminantType || filterPrimitivesIfContainsNonPrimitive
static Ty const* excess_check_target_of_union(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    TyList types = target->composite.types;
    bool discriminants;
    Ty const* ret = discriminated_union_target(me, st, source, target, &discriminants);
    if (ret != target) return ret;
    bool non_primitive = false;
    for (u32 i = 0; i < types.len; i++) non_primitive |= (types.data[i]->flags & TYF_NON_PRIMITIVE) != 0;
    if (!non_primitive) return target;
    TyBuf kept = {0};
    for (u32 i = 0; i < types.len; i++) {
        if (types.data[i]->kind != TY_INTRINSIC && types.data[i]->kind != TY_LITERAL) ty_buf_push(&kept, types.data[i]);
        else if (types.data[i]->flags & TYF_NON_PRIMITIVE) ty_buf_push(&kept, types.data[i]);
    }
    ret = kept.len ? ty_union(me, kept.data, kept.len, nullptr, (TyList){0}) : target;
    ty_buf_drop(&kept);
    return ret;
}

static bool has_signatures(Checker* me, Ty const* ty, bool construct) {
    if (!(ty->flags & TYF_OBJECT)) return false;
    Members const* members = ty_members(me, ty);
    return construct ? members->construct_count > 0 : members->call_count > 0;
}

// The union member a failed relation is most likely about (tsc's getBestMatchingType), null if none stands out
static Ty const* best_matching_type(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    if (target->kind != TY_UNION) return nullptr;
    TyList types = target->composite.types;
    bool structured_source = (source->flags & (TYF_OBJECT | TYF_INTERSECTION)) != 0;
    // findMatchingDiscriminantType
    if (structured_source) {
        bool discriminants;
        Ty const* discriminated = discriminated_union_target(me, st, source, target, &discriminants);
        if (discriminants && discriminated != target) return discriminated;
    }
    // findMatchingTypeReferenceOrTypeAliasReference
    if (source->kind == TY_REFERENCE || source->kind == TY_ANONYMOUS) {
        for (u32 i = 0; i < types.len; i++) {
            Ty const* t = types.data[i];
            if (t->kind != source->kind) continue;
            if (t->kind == TY_REFERENCE && t->reference.target == source->reference.target) return t;
            if (t->kind == TY_ANONYMOUS && source->alias && source->alias == t->alias) return t;
        }
    }
    // findBestTypeForObjectLiteral
    if (source->kind == TY_ANONYMOUS && source->anonymous.object_literal) {
        bool array_like = false;
        for (u32 i = 0; i < types.len; i++) array_like |= ty_is_array(me, types.data[i]) || is_tuple(types.data[i]);
        for (u32 i = 0; array_like && i < types.len; i++) {
            if (!ty_is_array(me, types.data[i]) && !is_tuple(types.data[i])) return types.data[i];
        }
    }
    // findBestTypeForInvokable
    for (u32 construct = 0; construct < 2; construct++) {
        if (!has_signatures(me, source, construct)) continue;
        for (u32 i = 0; i < types.len; i++) {
            if (has_signatures(me, types.data[i], construct)) return types.data[i];
        }
        break;
    }
    // findMostOverlappyType: the (last) member sharing the most property names with the source
    Ty const* best = nullptr;
    if (structured_source) {
        Members const* members = ty_members(me, source);
        u32 best_count = 0;
        for (u32 i = 0; i < types.len; i++) {
            Ty const* t = types.data[i];
            if (!(t->flags & (TYF_OBJECT | TYF_INTERSECTION))) continue;
            u32 count = 0;
            for (u32 p = 0; p < members->prop_count; p++) count += ty_get_prop(me, t, members->props[p]->name) != nullptr;
            if (count && count >= best_count) {
                best = t;
                best_count = count;
            }
        }
    }
    return best;
}

Ty const* ty_best_matching_type(Checker* me, Ty const* source, Ty const* target) {
    return best_matching_type(me, nullptr, source, target);
}

static bool keep_excess_check_target(Ty const* ty) { return is_excess_property_check_target(ty); }

static Ty const* filter_excess_check_targets(Checker* me, Ty const* ty) {
    if (ty->kind != TY_UNION) return ty;
    TyBuf kept = {0};
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (keep_excess_check_target(ty->composite.types.data[i])) ty_buf_push(&kept, ty->composite.types.data[i]);
    }
    Ty const* ret = kept.len == ty->composite.types.len ? ty : ty_union(me, kept.data, kept.len, nullptr, (TyList){0});
    ty_buf_drop(&kept);
    return ret;
}

static bool has_excess_properties(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    if (!is_excess_property_check_target(target)) return false;
    if (st->relation == TY_RELATION_ASSIGNABLE || st->relation == TY_RELATION_COMPARABLE ||
        st->relation == TY_RELATION_ASSIGNABLE_STRICT_NULLS
    ) {
        Ty const* object = global_type(me, GLOBAL_OBJECT);
        bool object_in_target = object && (target == object || (target->kind == TY_UNION && union_has_member(target, object)));
        if (object_in_target || is_empty_object_type(me, target)) return false;
    }
    Ty const* reduced = target;
    bool check_types = false;
    if (target->kind == TY_UNION) {
        reduced = excess_check_target_of_union(me, st, source, target);
        check_types = true;
    }
    Members const* members = ty_members(me, source);
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members->props[i];
        if (!prop->literal_own) continue;
        if (!is_known_property(me, reduced, prop->name)) {
            if (report) {
                cstr name = ty_prop_name_cstr(me, prop->name);
                cstr target_str = display_for_report(me, filter_excess_check_targets(me, reduced), false);
                report_line_at(st, 2353, cstr_d(
                    "Object literal may only specify known properties, and '", name, "' does not exist in type '",
                    target_str, "'."
                ), prop->name_span);
                c_drop(cstr, &name, &target_str);
                st->override_next_head++; // (tsc's reportParentSkippedError)
            }
            return true;
        }
        if (check_types) {
            TyList types = reduced->kind == TY_UNION ? reduced->composite.types : (TyList){ .data = &reduced, .len = 1 };
            TyBuf parts = {0};
            for (u32 j = 0; j < types.len; j++) {
                Ty const* part = prop_or_index_type(me, ty_apparent_type(me, types.data[j]), prop->name);
                ty_buf_push(&parts, part ? part : ty_intrinsic(me, TY_UNDEFINED));
            }
            Ty const* expected = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
            ty_buf_drop(&parts);
            if (!is_related_to(me, st, prop_type(me, prop), expected, report)) {
                if (report) {
                    cstr name = ty_prop_name_cstr(me, prop->name);
                    report_line(st, 2326, cstr_d("Types of property '", name, "' are incompatible."));
                    cstr_drop(&name);
                }
                return true;
            }
        }
    }
    return false;
}

static bool is_known_property(Checker* me, Ty const* target, Atom const* name);

// tsc's isWeakType: an object type whose properties (at least one) are all optional, and nothing else
static bool is_weak_type(Checker* me, Ty const* ty) {
    if (ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!is_weak_type(me, ty->composite.types.data[i])) return false;
        }
        return true;
    }
    if (!(ty->flags & TYF_OBJECT)) return false;
    Members const* members = ty_members(me, ty);
    if (members->mapped || members->call_count || members->construct_count || members->index_count || !members->prop_count) {
        return false;
    }
    for (u32 i = 0; i < members->prop_count; i++) {
        if (!(members->props[i]->flags & PROP_OPTIONAL)) return false;
    }
    return true;
}

// tsc's isRelatedTo common property check: a source with properties or signatures shares none with a weak target
static bool lacks_common_properties(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    if (st->relation == TY_RELATION_IDENTITY || st->intersection_target) return false;
    if (st->relation == TY_RELATION_COMPARABLE && !(source->kind == TY_LITERAL || (source->flags & TYF_NULLABLE))) return false;
    u32 primitive = TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT | TYF_BIGINT_LITERAL | TYF_BOOLEAN |
        TYF_BOOLEAN_LITERAL | TYF_ENUM | TYF_ES_SYMBOL_LIKE;
    if (!(source->flags & (primitive | TYF_OBJECT)) && source->kind != TY_INTERSECTION) return false;
    if (source == global_type(me, GLOBAL_OBJECT)) return false;
    if (!(target->flags & TYF_OBJECT) && target->kind != TY_INTERSECTION) return false;
    if (!is_weak_type(me, target)) return false;
    Ty const* apparent = ty_apparent_type(me, source);
    if (!(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) return false;
    Members const* members = ty_members(me, apparent);
    if (!members->prop_count && !members->call_count && !members->construct_count) return false;
    for (u32 i = 0; i < members->prop_count; i++) {
        if (is_known_property(me, target, members->props[i]->name)) return false;
    }
    return true;
}

static Ternary is_related_to(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    // Relations (and their caches) only see regular literals (tsc's getNormalizedType)
    source = ty_regular(me, source);
    target = ty_regular(me, target);
    if (source == target) return TERNARY_TRUE;
    if (st->relation == TY_RELATION_IDENTITY) return identity_related(me, st, source, target);
    // Comparability is mostly bidirectional (tsc's isRelatedTo: simple relations the other way around count too)
    if (st->relation == TY_RELATION_COMPARABLE && !(target->flags & TYF_NEVER) && is_simple_related(me, st, target, source)) {
        return TERNARY_TRUE;
    }
    if (is_simple_related(me, st, source, target)) return TERNARY_TRUE;
    if (ty_is_fresh_object(source) && !st->intersection_target && is_structured(target) &&
        has_excess_properties(me, st, source, target, report)
    ) {
        if (report) report_relation_error(me, st, source, target);
        return TERNARY_FALSE;
    }
    if (lacks_common_properties(me, st, source, target)) {
        if (report) {
            cstr source_str = display_for_report(me, source, false);
            cstr target_str = display_for_report(me, target, false);
            // Something callable whose result would fit
            Ty const* apparent = ty_apparent_type(me, source);
            Members const* members = apparent->flags & (TYF_OBJECT | TYF_INTERSECTION) ? ty_members(me, apparent) : nullptr;
            Signature const* sig = members && members->call_count ? members->call[0] :
                members && members->construct_count ? members->construct[0] : nullptr;
            if (sig && is_related_to(me, st, signature_return_type(me, sig), target, false)) {
                report_line(st, 2560, cstr_d("Value of type '", source_str, "' has no properties in common with type '",
                    target_str, "'. Did you mean to call it?"));
            } else {
                report_line(st, 2559, cstr_d("Type '", source_str, "' has no properties in common with type '", target_str, "'."));
            }
            isize_t count = vec_ty_report_line_size(&st->lines);
            if (count) vec_ty_report_line_at_mut(&st->lines, count - 1)->replaces_head = true;
            c_drop(cstr, &source_str, &target_str);
        }
        return TERNARY_FALSE;
    }
    Ternary result = TERNARY_FALSE;
    if (st->relation == TY_RELATION_COMPARABLE && !is_structured(source) && !is_structured(target)) {
        // Comparable is looser about literals: a primitive is comparable to its literals
        Ty const* widened = ty_base_type_of_literal(me, target);
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
    source = ty_regular(me, source);
    target = ty_regular(me, target);
    // The stacks are big and only ever read below `depth`, so the state isn't zeroed wholesale
    RelationState state;
    RelationState* st = &state;
    st->relation = relation;
    st->reporting = report_into != nullptr;
    st->lines = (VecTyReportLine){0};
    st->override_next_head = 0;
    st->intersection_target = false;
    st->maybe_keys = nullptr;
    st->maybe_count = st->maybe_cap = 0;
    st->depth = 0;
    st->expanding = 0;
    st->overflow = false;
    st->outer = my relating;
    if (source == target) return true;
    if (relation != TY_RELATION_IDENTITY && !st->reporting && is_simple_related(me, st, source, target)) return true;
    if (relation != TY_RELATION_IDENTITY && !st->reporting && !is_structured(source) && !is_structured(target)) {
        // Nothing to look into (narrowing asks this about every member of big literal unions): see is_related_to()
        if (relation != TY_RELATION_COMPARABLE || source->kind != TY_INTRINSIC) return false;
        Ty const* widened = ty_base_type_of_literal(me, target);
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
        // Collected innermost first; related information goes last, in the order it was associated
        for (isize_t i = vec_ty_report_line_size(&st->lines); i > 0; i--) {
            TyReportLine* line = vec_ty_report_line_at_mut(&st->lines, i - 1);
            if (!line->related) vec_ty_report_line_push(report_into, ty_report_line_clone(*line));
        }
        for_each (line, vec_ty_report_line, &st->lines) {
            if (line->related) vec_ty_report_line_push(report_into, ty_report_line_clone(*line));
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

bool ty_is_related_within(Checker* me, Ty const* source, Ty const* target) {
    RelationState* st = my relating;
    if (!st) return ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE);
    if ((source->flags | target->flags) & TYF_ERROR) return true;
    return is_related_to(me, st, source, target, false) != TERNARY_FALSE;
}

bool ty_is_related(Checker* me, Ty const* source, Ty const* target, TyRelation relation) {
    return check_related(me, source, target, relation, nullptr);
}

void ty_explain_unrelated(Checker* me, Ty const* source, Ty const* target, TyRelation relation, VecTyReportLine* out) {
    check_related(me, source, target, relation, out);
}

// Union signatures (tsc's getUnionSignatures): what a union of callables can be called with

static bool types_identical(Checker* me, Ty const* a, Ty const* b) {
    return a == b || ty_is_related(me, a, b, TY_RELATION_IDENTITY);
}

// How signatures_identical() compares two types: by identity (inside the identity relation `st` if there is one), or
// with `partial`, by strict subtype (tsc's compareTypesSubtypeOf)
static Ternary sig_types_compare(Checker* me, RelationState* st, Ty const* t, Ty const* s, bool partial) {
    if (partial) return t == s || ty_is_related(me, t, s, TY_RELATION_STRICT_SUBTYPE) ? TERNARY_TRUE : TERNARY_FALSE;
    if (st) return is_related_to(me, st, t, s, false);
    return types_identical(me, t, s) ? TERNARY_TRUE : TERNARY_FALSE;
}

static Ty const* type_at_position(Checker* me, Signature const* sig, u32 i) {
    Ty const* ret = param_type_at(me, sig, i);
    return ret ? ret : ty_intrinsic(me, TY_ANY);
}

// tsc's compareSignaturesIdentical: `partial` lets the source have more (optional) parameters, whose types are then
// only compared by subtype
static Ternary signatures_identical_in(
    Checker* me, RelationState* st, Signature const* source, Signature const* target, bool partial, bool ignore_return
) {
    if (source == target) return TERNARY_TRUE;
    signature_resolve(me, source);
    signature_resolve(me, target);
    bool same_shape = param_count_of(source) == param_count_of(target) && source->min_args == target->min_args &&
        has_rest(me, source) == has_rest(me, target);
    if (!same_shape && !(partial && source->min_args <= target->min_args)) return TERNARY_FALSE;
    if (source->type_params.len != target->type_params.len) return TERNARY_FALSE;
    if (target->type_params.len && source->type_params.data != target->type_params.data) {
        TyMapper const* mapper = ty_mapper_new(me, source->type_params, target->type_params);
        Ty const* unknown = ty_intrinsic(me, TY_UNKNOWN_);
        for (u32 i = 0; i < target->type_params.len; i++) {
            Ty const* s = source->type_params.data[i];
            Ty const* t = target->type_params.data[i];
            if (s == t) continue;
            Ty const* s_constraint = ty_param_constraint(me, s);
            Ty const* t_constraint = ty_param_constraint(me, t);
            Ty const* s_default = ty_param_default(me, s);
            Ty const* t_default = ty_param_default(me, t);
            if (!sig_types_compare(me, st, s_constraint ? ty_instantiate(me, s_constraint, mapper) : unknown,
                    t_constraint ? t_constraint : unknown, partial) ||
                !sig_types_compare(me, st, s_default ? ty_instantiate(me, s_default, mapper) : unknown,
                    t_default ? t_default : unknown, partial)
            ) {
                return TERNARY_FALSE;
            }
        }
        source = ty_signature_instantiation(me, source, target->type_params);
    }
    Ternary result = TERNARY_TRUE;
    if (source->this_type && target->this_type) {
        result &= sig_types_compare(me, st, source->this_type, target->this_type, partial);
        if (!result) return TERNARY_FALSE;
    }
    u32 target_len = param_count_of(target);
    for (u32 i = 0; i < target_len; i++) {
        result &= sig_types_compare(me, st, type_at_position(me, target, i), type_at_position(me, source, i), partial);
        if (!result) return TERNARY_FALSE;
    }
    if (ignore_return) return result;
    TyPredicate const* source_predicate = signature_predicate(me, source);
    TyPredicate const* target_predicate = signature_predicate(me, target);
    if (source_predicate || target_predicate) {
        if (!source_predicate || !target_predicate || source_predicate->is_this != target_predicate->is_this ||
            source_predicate->asserts != target_predicate->asserts ||
            source_predicate->param_index != target_predicate->param_index
        ) {
            return TERNARY_FALSE;
        }
        if (source_predicate->type == target_predicate->type) return result;
        if (!source_predicate->type || !target_predicate->type) return TERNARY_FALSE;
        return result & sig_types_compare(me, st, source_predicate->type, target_predicate->type, partial);
    }
    return result & sig_types_compare(me, st, signature_return_type(me, source), signature_return_type(me, target), partial);
}

static bool signatures_identical(
    Checker* me, Signature const* source, Signature const* target, bool partial, bool ignore_return
) {
    return signatures_identical_in(me, nullptr, source, target, partial, ignore_return) != TERNARY_FALSE;
}

typedef struct SignatureList {
    Signature* const* data;
    u32 len;
} SignatureList;

static Signature* find_matching_signature(
    Checker* me, SignatureList list, Signature const* sig, bool partial, bool ignore_return
) {
    for (u32 i = 0; i < list.len; i++) {
        if (signatures_identical(me, list.data[i], sig, partial, ignore_return)) return list.data[i];
    }
    return nullptr;
}

// The predicate of a signature standing for several (tsc's getUnionOrIntersectionTypePredicate): only if they all
// have one of the same kind on the same parameter, of the union of their types
static TyPredicate const* union_predicate(Checker* me, Signature* const* sigs, u32 count) {
    TyPredicate const* first = nullptr;
    Ty const** types = malloc(sizeof(Ty const*) * count);
    panic_if(!types, "out of memory");
    for (u32 i = 0; i < count; i++) {
        TyPredicate const* predicate = signature_predicate(me, sigs[i]);
        if (!predicate || !predicate->type || (first && (first->is_this != predicate->is_this ||
            first->asserts != predicate->asserts || first->param_index != predicate->param_index))
        ) {
            free(types);
            return nullptr;
        }
        if (!first) first = predicate;
        types[i] = predicate->type;
    }
    TyPredicate* ret = arena_new(&my arena, TyPredicate);
    *ret = *first;
    ret->type = ty_union(me, types, count, nullptr, (TyList){0});
    free(types);
    return ret;
}

// The `this` of a union signature: what every member's says (getUnionSignatures)
static Ty const* union_this_type(Checker* me, Signature* const* composites, u32 count) {
    Ty const* stack_buf[16];
    u32 len = 0;
    for (u32 i = 0; i < count && len < c_arraylen(stack_buf); i++) {
        if (composites[i]->this_type) stack_buf[len++] = composites[i]->this_type;
    }
    return len ? ty_intersection(me, stack_buf, len, nullptr, (TyList){0}) : nullptr;
}

// A signature with the parameters of `base` returning what any of `composites` returns (tsc's createUnionSignature)
static Signature* union_signature(Checker* me, Signature const* base, Signature* const* composites, u32 count) {
    Ty const** return_types = malloc(sizeof(Ty const*) * count);
    panic_if(!return_types, "out of memory");
    for (u32 i = 0; i < count; i++) return_types[i] = signature_return_type(me, composites[i]);
    Signature* ret = arena_new(&my arena, Signature);
    *ret = (Signature){
        .flags = base->flags,
        .name_span = base->name_span,
        .decl_span = base->decl_span,
        .state = LAZY_STATE_DONE,
        .type_params = base->type_params,
        .params = base->params,
        .param_count = base->param_count,
        .min_args = base->min_args,
        .this_type = union_this_type(me, composites, count),
        .ret_state = LAZY_STATE_DONE,
        .ret = ty_union_subtype_reduced(me, return_types, count),
        .predicate_done = true,
        .predicate = union_predicate(me, composites, count),
    };
    free(return_types);
    return ret;
}

// tsc's findMatchingSignatures: a signature of list `index` and its match in every other list, or none
static u32 find_matching_signatures(
    Checker* me, SignatureList const* lists, u32 list_count, Signature* sig, u32 index, Signature** out
) {
    if (sig->type_params.len) {
        // Generic signatures have to match exactly, and only the first list's are looked at
        if (index > 0) return 0;
        for (u32 i = 1; i < list_count; i++) {
            if (!find_matching_signature(me, lists[i], sig, false, false)) return 0;
        }
        out[0] = sig;
        return 1;
    }
    u32 count = 0;
    for (u32 i = 0; i < list_count; i++) {
        Signature* match = i == index ? sig : find_matching_signature(me, lists[i], sig, false, true);
        if (!match) match = find_matching_signature(me, lists[i], sig, true, true);
        if (!match) return 0;
        bool seen = false;
        for (u32 j = 0; j < count; j++) seen |= out[j] == match;
        if (!seen) out[count++] = match;
    }
    return count;
}

// tsc's combineSignaturesOfUnionMembers: one signature taking what both take (parameter types intersected, the longer
// parameter list), returning what either returns. `intersection`: combineSignaturesOfIntersectionMembers, the other
// way around (parameter types united, the return types intersected), for contextual signatures
static Signature* combine_signatures(Checker* me, Signature const* left, Signature const* right, bool intersection) {
    signature_resolve(me, left);
    signature_resolve(me, right);
    TyMapper const* mapper = left->type_params.len && right->type_params.len ?
        ty_mapper_new(me, right->type_params, left->type_params) : nullptr;
    u32 left_count = param_count_of(left);
    u32 right_count = param_count_of(right);
    Signature const* longest = left_count >= right_count ? left : right;
    Signature const* shorter = longest == left ? right : left;
    u32 longest_count = longest == left ? left_count : right_count;
    bool either_rest = has_rest(me, left) || has_rest(me, right);
    bool extra_rest = either_rest && !has_rest(me, longest);
    u32 count = longest_count + (extra_rest ? 1 : 0);
    SigParam* params = arena_new_n(&my arena, SigParam, count ? count : 1);
    Ty const* unknown = ty_intrinsic(me, TY_UNKNOWN_);
    for (u32 i = 0; i < longest_count; i++) {
        Ty const* longest_type = param_type_at(me, longest, i);
        if (longest == right) longest_type = ty_instantiate(me, longest_type, mapper);
        Ty const* shorter_type = param_type_at(me, shorter, i);
        shorter_type = shorter_type ? shorter == right ? ty_instantiate(me, shorter_type, mapper) : shorter_type : unknown;
        Ty const* both[2] = { longest_type, shorter_type };
        Ty const* type = intersection ? ty_union(me, both, 2, nullptr, (TyList){0}) : ty_intersection(me, both, 2, nullptr, (TyList){0});
        bool is_rest = either_rest && !extra_rest && i == longest_count - 1;
        bool optional = i >= longest->min_args && i >= shorter->min_args;
        Atom const* left_name = i < left->param_count && !left->params[i].rest ? left->params[i].name : nullptr;
        Atom const* right_name = i < right->param_count && !right->params[i].rest ? right->params[i].name : nullptr;
        Atom const* name = left_name == right_name || !right_name ? left_name : !left_name ? right_name : nullptr;
        if (!name) {
            char buf[16];
            snprintf(buf, sizeof buf, "arg%u", i);
            name = checker_atom(me, c_sv(buf, (isize_t)strlen(buf)));
        }
        params[i] = (SigParam){
            .name = name,
            .type = is_rest ? ty_array(me, type, false) : type,
            .optional = optional && !is_rest,
            .rest = is_rest,
        };
    }
    if (extra_rest) {
        Ty const* rest = type_at_position(me, shorter, longest_count);
        if (shorter == right) rest = ty_instantiate(me, rest, mapper);
        params[longest_count] = (SigParam){ .name = checker_atom(me, c_sv("args")), .type = ty_array(me, rest, false), .rest = true };
    }
    Ty const* return_types[2] = { signature_return_type(me, left), ty_instantiate(me, signature_return_type(me, right), mapper) };
    // The `this` types are combined like parameters (combineUnionThisParam, combineIntersectionThisParam)
    Ty const* this_type = left->this_type ? left->this_type : right->this_type ? ty_instantiate(me, right->this_type, mapper) : nullptr;
    if (left->this_type && right->this_type) {
        Ty const* both[2] = { left->this_type, ty_instantiate(me, right->this_type, mapper) };
        this_type = intersection ? ty_union(me, both, 2, nullptr, (TyList){0}) : ty_intersection(me, both, 2, nullptr, (TyList){0});
    }
    Signature* ret = arena_new(&my arena, Signature);
    *ret = (Signature){
        .flags = left->flags & SIGNATURE_CONSTRUCT,
        .name_span = left->name_span,
        .decl_span = left->decl_span,
        .state = LAZY_STATE_DONE,
        .type_params = left->type_params.len ? left->type_params : right->type_params,
        .params = params,
        .param_count = count,
        .min_args = left->min_args > right->min_args ? left->min_args : right->min_args,
        .this_type = this_type,
        .ret_state = LAZY_STATE_DONE,
        .ret = intersection ? ty_intersection(me, return_types, 2, nullptr, (TyList){0}) : ty_union_subtype_reduced(me, return_types, 2),
        .predicate_done = true,
    };
    return ret;
}

Signature const* ty_combine_intersection_signatures(Checker* me, Signature const* left, Signature const* right) {
    return combine_signatures(me, left, right, true);
}

bool ty_signatures_identical(Checker* me, Signature const* source, Signature const* target, bool partial, bool ignore_return) {
    return signatures_identical(me, source, target, partial, ignore_return);
}

Signature const* ty_union_signature(Checker* me, Signature const* base, Signature* const* composites, u32 count) {
    return union_signature(me, base, composites, count);
}

bool ty_type_params_identical(Checker* me, TyList source, TyList target) {
    if (source.len != target.len) return false;
    if (!source.len) return true;
    TyMapper const* mapper = ty_mapper_new(me, target, source);
    for (u32 i = 0; i < source.len; i++) {
        Ty const* s = ty_param_constraint(me, source.data[i]);
        Ty const* t = ty_param_constraint(me, target.data[i]);
        Ty const* unknown = ty_intrinsic(me, TY_UNKNOWN_);
        if (!types_identical(me, s ? s : unknown, t ? ty_instantiate(me, t, mapper) : unknown)) return false;
    }
    return true;
}

// The call (or construct) signatures of a union whose members are all callable (tsc's getUnionSignatures): those
// every member has (up to extra optional parameters), returning the union of what they return. Failing that, when at
// most one member is overloaded, its signatures combined with the others'. Null (and 0) if there are none
Signature* const* ty_union_signatures(Checker* me, TyList members, bool construct, u32* count_out) {
    *count_out = 0;
    SignatureList* lists = malloc(sizeof(SignatureList) * members.len);
    panic_if(!lists, "out of memory");
    u32 total = 0;
    i32 overloaded = -2; // -2: no list with more than one signature, -1: several
    for (u32 i = 0; i < members.len; i++) {
        Members const* m = ty_members(me, ty_apparent_type(me, members.data[i]));
        lists[i] = construct ? (SignatureList){ m->construct, m->construct_count } : (SignatureList){ m->call, m->call_count };
        if (!lists[i].len) {
            free(lists);
            return nullptr;
        }
        if (lists[i].len > 1) overloaded = overloaded == -2 ? (i32)i : -1;
        total += lists[i].len;
    }
    Signature** result = arena_new_n(&my arena, Signature*, total);
    u32 result_count = 0;
    Signature** matches = malloc(sizeof(Signature*) * members.len);
    panic_if(!matches, "out of memory");
    for (u32 i = 0; i < members.len; i++) {
        for (u32 j = 0; j < lists[i].len; j++) {
            Signature* sig = lists[i].data[j];
            signature_resolve(me, sig);
            SignatureList so_far = { result, result_count };
            if (result_count && find_matching_signature(me, so_far, sig, false, true)) continue;
            u32 match_count = find_matching_signatures(me, lists, members.len, sig, i, matches);
            if (!match_count) continue;
            result[result_count++] = match_count > 1 ? union_signature(me, sig, matches, match_count) : sig;
        }
    }
    free(matches);
    if (!result_count && overloaded != -1) {
        SignatureList master = lists[overloaded >= 0 ? overloaded : 0];
        Signature** combined = arena_new_n(&my arena, Signature*, master.len);
        for (u32 j = 0; j < master.len; j++) combined[j] = master.data[j];
        bool compatible = true;
        for (u32 i = 0; i < members.len && compatible; i++) {
            if (lists[i].data == master.data) continue;
            Signature* sig = lists[i].data[0];
            signature_resolve(me, sig);
            for (u32 j = 0; j < master.len && compatible; j++) {
                signature_resolve(me, combined[j]);
                if (sig->type_params.len && combined[j]->type_params.len &&
                    !ty_type_params_identical(me, sig->type_params, combined[j]->type_params)
                ) {
                    compatible = false;
                }
            }
            for (u32 j = 0; j < master.len && compatible; j++) combined[j] = combine_signatures(me, combined[j], sig, false);
        }
        if (compatible) {
            result = combined;
            result_count = master.len;
        }
    }
    free(lists);
    *count_out = result_count;
    return result_count ? result : nullptr;
}
