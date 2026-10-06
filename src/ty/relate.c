#include "ty/ty_internal.h"
#include "program.h"
#include "scope.h"
#include "error.h"
#include "class_decl.h"
#include "function_impl.h"
#include "statement.h"
#include "interface_decl.h"
#include "enum_decl.h"

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
    // Went through something that makes a variance measurement unreliable/unmeasurable (tsc's ReportsUnreliable,
    // ReportsUnmeasurable)
    CACHE_UNRELIABLE = 4,
    CACHE_UNMEASURABLE = 8,
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
    GLOBAL_ANY_FUNCTION, // Not a global: tsc's anyFunctionType
    GLOBAL_UNKNOWN_EMPTY_OBJECT, // Not a global: tsc's unknownEmptyObjectType, the `{}` of `unknown` narrowed
    GLOBAL_EMPTY_TYPE_LITERAL, // Not a global: tsc's emptyTypeLiteralType, every `{}` written without an alias
} GlobalType;

typedef struct RelationState {
    TyRelation relation;
    bool reporting;
    // Reporting, the source as given when a union of fresh literals: its members are related in its order, not the
    // regular union's (tsc relates the fresh union: `1 | ""` fails on `1` first even though `""` has the lower id)
    Ty const* fresh_union;
    // The pair whose alias variances failed on an invariant argument while reporting: related again without them (tsc's
    // relateVariances falls through to the rest of structuredTypeRelatedTo)
    Ty const* alias_fallback_source;
    Ty const* alias_fallback_target;
    VecTyReportLine lines; // innermost first while being collected
    u32 override_next_head; // A more specific message stands in for the next "Type S is not assignable to type T"
    // Relating to the members of an intersection target (tsc's IntersectionState.Target): no excess property checks,
    // the intersection as a whole had them
    bool intersection_target;
    // Relating the members of an intersection source (tsc's IntersectionState.Source): a member has no inferable index
    // signature, the intersection as a whole is related afterwards. Set for the members' relations (part of their cache
    // key), and turned into member_of_intersection_source for their own structure only. HACK: tsc keeps the state
    // down into their properties; doing that here made zod's deep-partial test run into the instantiation limits
    bool intersection_source;
    bool member_of_intersection_source;
    // The failure just reported heads the error itself (a weak type): no "not assignable" line for it
    bool head_reported;
    // The pairs assumed related while they're being compared (tsc's maybeKeys), and a hash index of them (tsc's
    // maybeKeysSet): a relation whose results stay Maybe keeps them all, 100k in d3-geo's `this & { stream }` methods,
    // and scanning them at each pair was quadratic. maybe_heads[hash] is 1 + the newest entry with that hash,
    // maybe_next[i] 1 + the one before it; entries go in stack order, so dropping from the top unlinks heads. Null
    // while the stack is short, see maybe_push()
    u64* maybe_keys;
    u32* maybe_next;
    u32* maybe_heads;
    u32 maybe_count, maybe_cap;
    Ty const* source_stack[MAX_RELATION_DEPTH];
    Ty const* target_stack[MAX_RELATION_DEPTH];
    // Each stack entry's recursion identity (ty_recursion_entry()), worked out once, when a deep nesting check first needs it
    uintptr_t source_entries[MAX_RELATION_DEPTH];
    uintptr_t target_entries[MAX_RELATION_DEPTH];
    u32 source_entries_valid, target_entries_valid; // How many from the bottom are up to date: filled when looked at
    u32 depth;
    // The stacks deep nesting looks at (tsc's sourceStack/targetStack): a relation to a member of a union or intersection
    // pushes only the side that changes (tsc's RecursionFlags), see is_related_to_side()
    u32 source_depth, target_depth;
    unsigned skip_push; // For the next recursive_related(): 1 don't push the source, 2 don't push the target
    unsigned expanding; // 1: source is deeply nested, 2: target is
    bool overflow;
    // What's left of the budget of structured results (tsc's relationCount: (16M - cached) / 8, one per result
    // cached). Running out is an overflow too, reported as TS2859 rather than a depth one
    i64 relation_count;
    // The relation whose "not assignable" line the last incompatible step stood in for (tsc's lastSkippedInfo)
    Ty const* skipped_source;
    Ty const* skipped_target;
    // The top-level relation's line is the error's head, which nothing stands in for (tsc's headMessage)
    bool at_top;
    // The code of the head the caller puts over the explanation (tsc's headMessage), 0 if none
    unsigned head_code;
    Checker* checker;
    struct RelationState* outer;
} RelationState;

// Relations compare an optional property's type with its optionality's undefined, except under
// exactOptionalPropertyTypes, where they compare the declared-type view (tsc's getNonMissingTypeOfSymbol)
static inline bool optional_adds_undefined(void) {
    return compiler_options()->strict_null_checks && !compiler_options()->exact_optional_property_types;
}

static Ternary is_related_to(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report);
static void report_variance(Checker* me, Ty const* ty, unsigned flag);
static Ty const* base_constraint(Checker* me, Ty const* ty, u32 depth);
static bool union_member_lacks_base_constraint(Checker* me, Ty const* ty, u32 depth);

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
        bigger.keys = zeroed_table(bigger.cap, sizeof(u64));
        bigger.values = zeroed_table(bigger.cap, 1);
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

[[gnu::noinline, gnu::cold]] static Ty const* resolve_global_type(Checker* me, GlobalType which) {
    static char const* const names[] = { "String", "Number", "Boolean", "Symbol", "BigInt", "Function", "Object", "RegExp", "CallableFunction", "NewableFunction" };
    {
        my global_types_resolved[which] = true;
        if (which == GLOBAL_EMPTY_OBJECT || which == GLOBAL_ANY_FUNCTION || which == GLOBAL_UNKNOWN_EMPTY_OBJECT ||
            which == GLOBAL_EMPTY_TYPE_LITERAL
        ) {
            Ty* empty = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT | (which == GLOBAL_ANY_FUNCTION ? TYF_NON_INFERRABLE : 0));
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

// (Split so the check inlines: apparent_type() asks on every relation)
static inline Ty const* global_type(Checker* me, GlobalType which) {
    return my global_types_resolved[which] ? my global_types[which] : resolve_global_type(me, which);
}

// tsc's initializeTypeChecker creates these before checking anything, in this order: their type ids come first, which
// decides where they sort in printed unions (`string | RegExp | SomeType`)
void ty_init_global_types(Checker* me) {
    static char const* const names[] = {
        "IArguments", "Array", "Object", "Function", "CallableFunction", "NewableFunction", "String", "Number", "Boolean",
        "RegExp", "ReadonlyArray", "ThisType",
    };
    // (tsc makes its empty object types before the global ones, when the checker is created)
    global_type(me, GLOBAL_EMPTY_OBJECT);
    global_type(me, GLOBAL_EMPTY_TYPE_LITERAL);
    global_type(me, GLOBAL_UNKNOWN_EMPTY_OBJECT);
    global_type(me, GLOBAL_ANY_FUNCTION);
    for (u32 i = 0; i < c_arraylen(names); i++) {
        bool bind_call_apply = !strcmp(names[i], "CallableFunction") || !strcmp(names[i], "NewableFunction");
        if (bind_call_apply && !compiler_options()->strict_bind_call_apply) continue;
        Symbol const* decl = program_get_global(my program, csview_from(names[i]), SYMBOL_TYPE);
        if (decl && decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) ty_declared_type_of(me, decl);
        // (anyArrayType right after RegExp (ty_array() makes autoArrayType before it), anyReadonlyArrayType after ReadonlyArray)
        if (!strcmp(names[i], "RegExp") && program_get_global(my program, csview_from("Array"), SYMBOL_TYPE)) {
            ty_array(me, ty_intrinsic(me, TY_ANY), false);
        }
        if (!strcmp(names[i], "ReadonlyArray") && program_get_global(my program, csview_from("Array"), SYMBOL_TYPE)) {
            ty_array(me, ty_intrinsic(me, TY_ANY), true);
        }
    }
}

Ty const* ty_global_function_type(Checker* me) { return global_type(me, GLOBAL_FUNCTION); }
Ty const* ty_global_object_type(Checker* me) { return global_type(me, GLOBAL_OBJECT); }
Ty const* ty_empty_object_type(Checker* me) { return global_type(me, GLOBAL_EMPTY_OBJECT); }
Ty const* ty_unknown_empty_object_type(Checker* me) { return global_type(me, GLOBAL_UNKNOWN_EMPTY_OBJECT); }
Ty const* ty_empty_type_literal(Checker* me) { return global_type(me, GLOBAL_EMPTY_TYPE_LITERAL); }
Ty const* ty_any_function_type(Checker* me) { return global_type(me, GLOBAL_ANY_FUNCTION); }
Symbol const* ty_global_value_symbol(Checker* me, csview name) { return program_get_global(my program, name, SYMBOL_VALUE); }

// The type of a regular expression literal
Ty const* ty_global_regexp_type(Checker* me) {
    Ty const* regexp = global_type(me, GLOBAL_REGEXP);
    return regexp ? regexp : ty_intrinsic(me, TY_ANY);
}

static bool is_array_target(Checker* me, Ty const* target) {
    if (target == my array_target) return true;
    return target->kind == TY_DECLARED && target == ty_readonly_array_target(me);
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
    if (ty->kind == TY_STRING_MAPPING || (ty->flags & TYF_DEFERRED)) {
        // Through its constraint, like any instantiable type: Uppercase<string> is some string
        Ty const* constraint = ty_base_constraint(me, ty);
        if (constraint && constraint != ty) return could_have_singletons(me, constraint);
    }
    return ty->kind == TY_LITERAL ||
        (ty->flags & (TYF_UNDEFINED | TYF_NULL | TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) != 0;
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
    if (!source || !target || !name_equals(source->name, target->name)) return false;
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

static inline bool same_literal_value(Ty const* s, Ty const* t);
[[gnu::cold]] static void report_line(RelationState* st, unsigned ts_code, cstr message);
[[gnu::cold]] static cstr display_fully_qualified(Checker* me, Ty const* ty, bool widen);

// An enum member's value as a message shows it, `"a"` or `1`: the plain literal of the same value
static cstr enum_value_display(Checker* me, Ty const* member) {
    Value value = { .tag = VALUE_NUMBER };
    if (member->flags & TYF_STRING_LITERAL) {
        value = (Value){ .tag = VALUE_STRING, .string = cstr_from_sv(atom_sv(member->literal.string)) };
    } else {
        value.number = member->literal.value.number;
    }
    cstr ret = cstr_null;
    ty_display(me, (TyDisplay){ .target = &ret }, ty_literal(me, &value));
    if (value.tag == VALUE_STRING) cstr_drop(&value.string);
    return ret;
}

// Why enums_related() said no (tsc's isEnumTypeRelatedTo with an error reporter): the first member of the source
// enum that the target lacks or has another value for. Only regular enums of the same name get that far
static void report_enums_unrelated(Checker* me, RelationState* st, Symbol const* source, Symbol const* target) {
    if (!source || !target || source == target || !name_equals(source->name, target->name)) return;
    if (!(source->flags & target->flags & SYMBOL_REGULAR_ENUM)) return;
    for (u32 decl_index = 0; decl_index < source->decl_count; decl_index++) {
        if (source->decls[decl_index].kind != SYMBOL_DECL_ENUM) continue;
        EnumDecl const* enum_decl = source->decls[decl_index].node;
        for_each (i, vec_enum_case, &enum_decl->cases) {
            Symbol const* member = i->symbol;
            if (!member || i->computed_name) continue;
            Symbol const* other = symbol_get_export(target, member->name);
            if (!other || !(other->flags & SYMBOL_ENUM_MEMBER)) {
                cstr target_str = display_fully_qualified(me, ty_declared_type_of(me, target), false);
                report_line(st, 2324, cstr_d("Property '", member->name, "' is missing in type '", target_str, "'."));
                cstr_drop(&target_str);
                return;
            }
            Ty const* source_value = ty_regular(me, ty_declared_type_of(me, member));
            Ty const* target_value = ty_regular(me, ty_declared_type_of(me, other));
            bool source_known = source_value->kind == TY_LITERAL && (source_value->flags & TYF_LITERAL);
            bool target_known = target_value->kind == TY_LITERAL && (target_value->flags & TYF_LITERAL);
            if (source_known && target_known) {
                if (same_literal_value(source_value, target_value)) continue;
                cstr expected = enum_value_display(me, target_value);
                cstr given = enum_value_display(me, source_value);
                report_line(st, 4125, cstr_d("Each declaration of '", target->name, ".", other->name, "' differs in its value, where '",
                    expected, "' was expected but '", given, "' was given."));
                c_drop(cstr, &expected, &given);
                return;
            }
            Ty const* known = source_known ? source_value : target_known ? target_value : nullptr;
            if (known && (known->flags & TYF_STRING_LITERAL)) {
                cstr shown = enum_value_display(me, known);
                report_line(st, 4126, cstr_d("One value of '", target->name, ".", other->name, "' is the string '", shown,
                    "', and the other is assumed to be an unknown numeric value."));
                cstr_drop(&shown);
                return;
            }
        }
    }
}

static inline bool same_literal_value(Ty const* s, Ty const* t) {
    if ((s->flags & TYF_LITERAL) != (t->flags & TYF_LITERAL)) return false;
    if (s->flags & TYF_STRING_LITERAL) return s->literal.string == t->literal.string;
    if (s->flags & TYF_NUMBER_LITERAL) {
        return s->literal.value.number == t->literal.value.number ||
            (s->literal.value.number != s->literal.value.number && t->literal.value.number != t->literal.value.number);
    }
    return false;
}

// Simple relations: decided by flags and identity alone (tsc's isSimpleTypeRelatedTo)

static bool is_readonly_prop(Prop const* prop) {
    return (prop->flags & PROP_READONLY) || ((prop->flags & PROP_GETTER) && !(prop->flags & PROP_SETTER));
}

static bool is_subtype_relation(TyRelation relation) {
    return relation == TY_RELATION_SUBTYPE || relation == TY_RELATION_STRICT_SUBTYPE;
}

// The enum cases of is_simple_related(): only asked when one side is an enum type, most relations never get here
static bool is_simple_related_enum(Checker* me, RelationState const* st, Ty const* s, Ty const* t) {
    u32 sf = s->flags;
    u32 tf = t->flags;
    if ((sf & TYF_ENUM) && (tf & TYF_NUMBER)) return true;
    if ((sf & TYF_ENUM_LITERAL) && (sf & TYF_LITERAL) && (tf & TYF_LITERAL) && !(tf & TYF_ENUM_LITERAL) &&
        s->kind == TY_LITERAL && t->kind == TY_LITERAL && same_literal_value(s, t)
    ) {
        return true;
    }
    if ((sf & TYF_ENUM) && (tf & TYF_ENUM)) {
        if (enums_related(me, s->literal.enum_decl, t->literal.enum_decl) &&
            name_equals(s->literal.enum_member->name, t->literal.enum_member->name)
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

// tsc's isUnknownLikeUnionType: `undefined | null | {}` (with strictNullChecks), to which anything is assignable
static bool is_unknown_like_union(Checker* me, Ty const* t) {
    if (t->kind != TY_UNION || !(t->xflags & TYX_UNDEFINED_AND_NULL) || t->composite.types.len < 3 ||
        !compiler_options()->strict_null_checks
    ) {
        return false;
    }
    for (u32 i = 0; i < t->composite.types.len; i++) {
        Ty const* member = t->composite.types.data[i];
        if (member->kind == TY_ANONYMOUS && ty_is_empty_anonymous_object(me, member)) return true;
    }
    return false;
}

// The enum cases of tsc's isSimpleTypeRelatedTo that ask isEnumTypeRelatedTo, explained
static void report_enum_pair(Checker* me, RelationState* st, Ty const* s, Ty const* t) {
    if (ty_is_enum_union(s) && ty_is_enum_union(t)) {
        report_enums_unrelated(me, st, s->alias, t->alias);
    } else if (s->kind == TY_LITERAL && t->kind == TY_LITERAL && s->literal.enum_decl && t->literal.enum_decl &&
        (((s->flags & t->flags & TYF_LITERAL) && same_literal_value(s, t)) ||
            ((s->flags & t->flags & TYF_ENUM) && name_equals(s->literal.enum_member->name, t->literal.enum_member->name)))
    ) {
        report_enums_unrelated(me, st, s->literal.enum_decl, t->literal.enum_decl);
    }
}

static bool is_simple_related(Checker* me, RelationState const* st, Ty const* s, Ty const* t) {
    u32 sf = s->flags;
    u32 tf = t->flags;
    // (The wildcard of a permissive instantiation relates to everything, never too: tsc's isSimpleTypeRelatedTo)
    if ((tf & TYF_ANY) || (sf & TYF_NEVER) || s == ty_intrinsic(me, TY_WILDCARD)) return true;
    // (any isn't a strict subtype of unknown, tsc's isSimpleTypeRelatedTo)
    if ((tf & TYF_UNKNOWN) && !(st->relation == TY_RELATION_STRICT_SUBTYPE && (sf & TYF_ANY))) return true;
    if (tf & TYF_NEVER) return false;
    if (sf & TYF_ANY) return !is_subtype_relation(st->relation);
    if ((sf & TYF_STRING_LIKE) && (tf & TYF_STRING)) return true;
    if ((sf & TYF_NUMBER_LITERAL) && (tf & TYF_NUMBER)) return true;
    if ((sf & TYF_BIGINT_LITERAL) && (tf & TYF_BIGINT)) return true;
    if ((sf & TYF_BOOLEAN_LITERAL) && (tf & TYF_BOOLEAN)) return true;
    if ((sf & TYF_UNIQUE_ES_SYMBOL) && (tf & TYF_ES_SYMBOL)) return true;
    if ((sf & TYF_UNDEFINED) && (tf & (TYF_UNDEFINED | TYF_VOID))) return true;
    if ((sf & TYF_NULL) && (tf & TYF_NULL)) return true;
    // (tsc's isSimpleTypeRelatedTo: in every relation, but a subtype of a union or intersection has to be a subtype
    // of its members; tttt's `boolean` isn't taken apart)
    if ((sf & TYF_NULLABLE) && !compiler_options()->strict_null_checks &&
        st->relation != TY_RELATION_ASSIGNABLE_STRICT_NULLS && st->relation != TY_RELATION_IDENTITY &&
        (!is_subtype_relation(st->relation) || (tf & TYF_BOOLEAN) || !(tf & (TYF_UNION | TYF_INTERSECTION)))
    ) {
        return true;
    }
    if ((sf & TYF_OBJECT) && (tf & TYF_NON_PRIMITIVE)) {
        // `{}` may be a primitive, so it isn't a strict subtype of `object` (unless it's a fresh `{}` literal)
        if (st->relation != TY_RELATION_STRICT_SUBTYPE || s->kind != TY_ANONYMOUS || s->anonymous.function ||
            ty_is_fresh_object(s)
        ) {
            return true;
        }
        Members const* members = ty_members(me, s);
        return members->prop_count || members->call_count || members->construct_count || members->index_count || members->mapped;
    }
    // Enum literals and computed members carry an enum flag; enum types themselves are aliased unions
    if ((((sf | tf) & (TYF_ENUM | TYF_ENUM_LITERAL)) || (s->kind == TY_UNION && t->kind == TY_UNION && s->alias && t->alias)) &&
        is_simple_related_enum(me, st, s, t)
    ) {
        return true;
    }
    // Anything is assignable to a union containing undefined, null and {}
    if ((st->relation == TY_RELATION_ASSIGNABLE || st->relation == TY_RELATION_COMPARABLE) && is_unknown_like_union(me, t)) {
        return true;
    }
    return false;
}

// Whether there's any structure to look at. If neither side has any, the simple relation was the answer
static bool is_structured(Ty const* ty) {
    return ty->flags & (TYF_OBJECT | TYF_UNION | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_TEMPLATE_LITERAL |
        TYF_STRING_MAPPING);
}

// Reporting

static void flush_incompatible(RelationState* st);

[[gnu::cold]] static void report_line(RelationState* st, unsigned ts_code, cstr message) {
    flush_incompatible(st);
    vec_ty_report_line_push(&st->lines, (TyReportLine){ .ts_code = ts_code, .message = message });
}

[[gnu::cold]] static void report_line_at(RelationState* st, unsigned ts_code, cstr message, Span span) {
    flush_incompatible(st);
    vec_ty_report_line_push(&st->lines, (TyReportLine){ .ts_code = ts_code, .message = message, .span = span });
}

[[gnu::cold]] static void report_related(RelationState* st, unsigned ts_code, cstr message, Span span) {
    if (!span_valid(span)) {
        cstr_drop(&message);
        return;
    }
    vec_ty_report_line_push(&st->lines, (TyReportLine){ .ts_code = ts_code, .message = message, .span = span, .related = true });
}

static void report_truncate(RelationState* st, isize_t len) {
    while (vec_ty_report_line_size(&st->lines) > len) vec_ty_report_line_pop(&st->lines); // drops the line
}

[[gnu::cold]] static cstr display_for_report(Checker* me, Ty const* ty, bool widen) {
    cstr ret = cstr_null;
    Scope const* enclosing = ty_report_enclosing(me, ty);
    ty_display(me, (TyDisplay){ .target = &ret, .widen_literals = widen, .enclosing = enclosing, .reuse_nodes = enclosing != nullptr }, ty);
    return ret;
}

[[gnu::cold]] static cstr display_fully_qualified(Checker* me, Ty const* ty, bool widen) {
    cstr ret = cstr_null;
    ty_display(me, (TyDisplay){ .target = &ret, .widen_literals = widen, .fully_qualified = true, .typeof_unique_symbols = true }, ty);
    return ret;
}

static bool is_plain_identifier(csview sv) {
    if (!sv.size || (sv.buf[0] >= '0' && sv.buf[0] <= '9')) return false;
    for (isize i = 0; i < sv.size; i++) {
        char c = sv.buf[i];
        if (!(c == '_' || c == '$' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

// The name of the one missing property of TS2741 (tsc's symbolToString with WriteComputedProps): a mapped type's
// property keyed by an enum member is `[E.A]`
static cstr missing_prop_name(Checker* me, Prop const* prop) {
    Prop const* template = prop->target;
    if (template && prop_decl_rare(template->decl)->template_of && !prop_decl_rare(template->decl)->template_of->name_type && prop->mapper &&
        prop->mapper->len == 1
    ) {
        Ty const* key = prop->mapper->targets[0];
        if (key->kind == TY_LITERAL && (key->flags & TYF_ENUM_LITERAL) && key->literal.enum_member &&
            key->literal.enum_member != key->literal.enum_decl && is_plain_identifier(key->literal.enum_member->name)
        ) {
            // (The member's symbol as an expression: `E.A`, even for an enum's only member)
            return cstr_d("[", ty_symbol_display_name(key->literal.enum_decl).sv, ".", key->literal.enum_member->name, "]");
        }
    }
    return ty_prop_written_name(me, prop);
}

// A deferred reference as tsc's getNormalizedType makes it for the relation: a plain reference, without the alias
// name of the alias whose body it is; a class or interface that adds nothing to its single base, that base (what
// structural messages below the head print)
static Ty const* normalized_reference(Checker* me, Ty const* ty) {
    for (u32 depth = 0; depth < 16; depth++) {
        if (ty->kind == TY_REFERENCE && ty->reference.deferred && ty->alias) {
            ty = ty_reference(me, ty->reference.target, ty_reference_args(me, ty));
            continue;
        }
        Ty const* base = ty_single_base_for_non_augmenting_subtype(me, ty);
        if (!base) break;
        ty = base;
    }
    return ty;
}

// The apparent type of an intersection as tsc's getApparentTypeOfIntersectionType makes it: each member's apparent
// type (`Number & { a: 1 }`), a new intersection without alias. Only for messages, the relation looks at the members
static Ty const* apparent_intersection(Checker* me, Ty const* ty) {
    if (ty->kind != TY_INTERSECTION) return ty;
    TyList types = ty->composite.types;
    bool primitive = false;
    // (Or a type parameter: `T & {}` with T unconstrained is `{}`)
    for (u32 i = 0; i < types.len && !primitive; i++) {
        primitive = !(types.data[i]->flags & (TYF_OBJECT | TYF_TYPE_PARAM | TYF_DEFERRED)) || types.data[i]->kind == TY_TYPE_PARAM;
    }
    if (!primitive) return ty;
    Ty const** apparent = malloc(sizeof(Ty const*) * types.len);
    panic_if(!apparent, "out of memory");
    for (u32 i = 0; i < types.len; i++) apparent[i] = ty_apparent_type(me, types.data[i]);
    Ty const* ret = ty_intersection(me, apparent, types.len, nullptr, (TyList){0});
    free(apparent);
    return ret;
}

// tsc's isLiteralType: a unit type or a union of them (booleans and enums included)
static bool is_literal_type(Ty const* ty) {
    u32 unit = TYF_LITERAL | TYF_ENUM_LITERAL | TYF_UNIQUE_ES_SYMBOL | TYF_NULLABLE | TYF_BOOLEAN;
    if (ty->kind == TY_UNION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!(ty->composite.types.data[i]->flags & unit)) return false;
        }
        return true;
    }
    return (ty->flags & unit) != 0;
}

// A source and target as messages show them side by side: told apart by their full names if they'd print the same
// (tsc's getTypeNamesForErrorDisplay)
[[gnu::cold]] static void display_pair(Checker* me, Ty const* source, bool widen, Ty const* target, cstr* source_out, cstr* target_out) {
    // A widened literal source is shown by tsc's getTypeNameForErrorDisplay (reportRelationError's generalizedSource):
    // fully qualified, a unique symbol as `typeof sym`
    // (Only a source made of literals is generalized: one with other members is shown as is)
    *source_out = widen && is_literal_type(source) ? display_fully_qualified(me, source, widen) :
        display_for_report(me, source, false);
    *target_out = display_for_report(me, target, false);
    if (!cstr_eq(source_out, target_out)) return;
    c_drop(cstr, source_out, target_out);
    *source_out = display_fully_qualified(me, source, widen && is_literal_type(source));
    *target_out = display_fully_qualified(me, target, false);
}

// Whether a literal source is shown widened: not against `never` (tsc wants `assertNever(x)` errors to show x as is)
static bool report_widens(Checker* me, Ty const* target) {
    return !(target->flags & TYF_NEVER) && !could_have_singletons(me, target);
}

void ty_display_pair_cstr(Checker* me, Ty const* a, Ty const* b, cstr* a_out, cstr* b_out) {
    display_pair(me, a, false, b, a_out, b_out);
}

void ty_report_pair_cstr(Checker* me, Ty const* source, Ty const* target, cstr* source_out, cstr* target_out) {
    source = ty_regular(me, source); // Messages see normalised types, like the relation
    bool widen = report_widens(me, target);
    display_pair(me, widen && is_literal_type(source) ? ty_base_type_of_literal(me, source) : source, widen, target, source_out,
        target_out);
}

// Whether the base constraint of `ty`, were it the constraint of the type parameter, would need the type parameter's
// own (tsc's hasNonCircularBaseConstraint of the synthetic parameter): it is the parameter, a union or intersection with
// it, `P extends ...` over it, an indexed access into it
static bool constraint_resolves_through(Ty const* ty, Ty const* source, Ty const* declared) {
    if (ty == source || ty == declared) return true;
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (constraint_resolves_through(ty->composite.types.data[i], source, declared)) return true;
        }
        return false;
    }
    if (ty->kind == TY_CONDITIONAL) return ty->conditional.check == source || ty->conditional.check == declared;
    if (ty->kind == TY_INDEXED) return ty->indexed.object == source || ty->indexed.object == declared;
    return false;
}

// Whether instantiating the target for the synthetic parameter makes an intersection of it and a primitive, `object`
// or `{}` (`NonNullable<T>`, `T & string`): tsc's getIntersectionType resolves the base constraint of the type variable
// of such a pair (its constraint reduction), before reportRelationError gives the parameter its constraint, so that is
// no constraint and never circular
static bool constraint_resolved_early(Checker* me, Ty const* ty, Ty const* source, Ty const* declared, u32 depth) {
    if (depth > 8) return false;
    if (ty->kind == TY_UNION || (ty->kind == TY_INTERSECTION && ty->composite.types.len != 2)) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (constraint_resolved_early(me, ty->composite.types.data[i], source, declared, depth + 1)) return true;
        }
        return false;
    }
    if (ty->kind != TY_INTERSECTION) return false;
    Ty const* const* pair = ty->composite.types.data;
    u32 variable = pair[0] == source || pair[0] == declared ? 0 : pair[1] == source || pair[1] == declared ? 1 : 2;
    if (variable == 2) return false;
    Ty const* other = pair[1 - variable];
    u32 primitive = TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT | TYF_BIGINT_LITERAL | TYF_BOOLEAN |
        TYF_BOOLEAN_LITERAL | TYF_ES_SYMBOL_LIKE | TYF_VOID | TYF_UNDEFINED | TYF_NULL | TYF_ENUM | TYF_NON_PRIMITIVE;
    bool generic_string = (other->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) && !ty_is_pattern_literal(other); // (tsc's isGenericStringLikeType)
    return ((other->flags & primitive) && !generic_string) || ty_is_empty_anonymous_object(me, other);
}

// An unconstrained type parameter source could have been constrained to the target (tsc's reportRelationError: unless
// that constraint would be circular)
static void report_missing_constraint(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    if (source->kind != TY_TYPE_PARAM || source->param.state->is_this || ty_param_constraint(me, source)) return;
    Ty const* declared = source;
    while (declared->param.state->clone_of) declared = declared->param.state->clone_of;
    GenericItem const* item = declared->param.state->item;
    // (An `infer X` has no GenericItem: it is named where its symbol is declared)
    Symbol const* infer_symbol = !item && declared->param.state->is_infer ? declared->param.symbol : nullptr;
    if (!item && !(infer_symbol && infer_symbol->decl_count)) return;
    // (Nor when the source as constrained by the target would have a circular base constraint, tsc's
    // hasNonCircularBaseConstraint: `Extract<T, string>` as a constraint of T is resolved through T's. Another type
    // parameter is no such target: instantiating it leaves it, and its own constraint mentions the original)
    if (!constraint_resolved_early(me, target, source, declared, 0) && constraint_resolves_through(target, source, declared)) return;
    // (tsc's typeToString(target, the type parameter's declaration): names as reachable from there, `JSX.X`)
    Symbol const* param_symbol = item ? item->owned_symbol : infer_symbol;
    Scope const* enclosing = param_symbol && param_symbol->decl_count ? param_symbol->decls[0].block : nullptr;
    cstr target_str = cstr_null;
    ty_display(me, (TyDisplay){ .target = &target_str, .enclosing = enclosing }, target);
    report_related(st, 2208, cstr_d("This type parameter might need an `extends ", target_str, "` constraint."),
        item ? item->name : infer_symbol->decls[0].name);
    cstr_drop(&target_str);
}

// tsc's reportIncompatibleError: `message` is what the step says on its own (for a signature's return types, always
// in the words of the ones with parameters)
[[gnu::cold]] static void report_incompatible(RelationState* st, unsigned ts_code, cstr message) {
    st->override_next_head++;
    st->skipped_source = st->skipped_target = nullptr;
    vec_ty_report_line_push(&st->lines, (TyReportLine){ .ts_code = ts_code, .message = message, .incompatible = true });
}

static void report_relation_error_now(Checker* me, RelationState* st, Ty const* source, Ty const* target);

static bool is_identifier_text(csview text) {
    if (!text.size) return false;
    for (isize i = 0; i < text.size; i++) {
        unsigned char c = (unsigned char)text.buf[i];
        bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 0x80;
        if (!letter && !(i && c >= '0' && c <= '9')) return false;
    }
    return true;
}

// tsc's reportIncompatibleStack: the pending incompatible steps (the innermost first) become one line naming the path
// through them, eg "The types of 'a.b' are incompatible between these types.", unless there's only one
static void flush_incompatible(RelationState* st) {
    isize_t count = 0;
    for (isize_t i = vec_ty_report_line_size(&st->lines); i > 0 && vec_ty_report_line_at(&st->lines, i - 1)->incompatible; i--) {
        count++;
    }
    if (!count) return;
    Ty const* skipped_source = st->skipped_source;
    Ty const* skipped_target = st->skipped_target;
    st->skipped_source = st->skipped_target = nullptr;
    if (count == 1) {
        TyReportLine only = vec_ty_report_line_pull(&st->lines);
        only.incompatible = false;
        // A signature's return types alone say nothing (elidedInCompatabilityPyramid)
        if (only.ts_code >= 2202 && only.ts_code <= 2205) {
            cstr_drop(&only.message);
        } else {
            vec_ty_report_line_push(&st->lines, only);
        }
    } else {
        cstr path = cstr_null;
        VecTyReportLine secondary = {0};
        // Off the end, the outermost first: the path is built from left to right
        for (isize_t i = 0; i < count; i++) {
            TyReportLine step = vec_ty_report_line_pull(&st->lines);
            step.incompatible = false;
            if (step.ts_code == 2326) {
                // "Types of property 'NAME' are incompatible."
                csview message = cstr_sv(&step.message);
                csview name = csview_slice(message, 19, message.size - 19);
                if (cstr_starts_with(&path, "new ")) {
                    cstr wrapped = cstr_d("(", path, ")");
                    cstr_take(&path, wrapped);
                }
                if (!cstr_size(&path)) {
                    cstr_append_sv(&path, name);
                } else if (is_identifier_text(name)) {
                    cstr_append(&path, ".");
                    cstr_append_sv(&path, name);
                } else if (name.size && name.buf[0] == '[' && name.buf[name.size - 1] == ']') {
                    cstr_append_sv(&path, name);
                } else {
                    cstr_append(&path, "[");
                    cstr_append_sv(&path, name);
                    cstr_append(&path, "]");
                }
                cstr_drop(&step.message);
            } else if (step.ts_code >= 2202 && step.ts_code <= 2205 && cstr_size(&path)) {
                bool construct = step.ts_code == 2203 || step.ts_code == 2205;
                char const* params = step.ts_code >= 2204 ? "" : "...";
                cstr called = cstr_d(construct ? "new " : "", path, "(", params, ")");
                cstr_take(&path, called);
                cstr_drop(&step.message);
            } else {
                // (a signature's return types at the start of the path, said like those with parameters: see
                // report_incompatible(); positions in tuples)
                if (step.ts_code == 2204 || step.ts_code == 2205) step.ts_code -= 2;
                vec_ty_report_line_insert_n(&secondary, 0, &step, 1);
            }
        }
        if (cstr_size(&path)) {
            bool returned = cstr_ends_with(&path, ")");
            vec_ty_report_line_push(&st->lines, (TyReportLine){
                .ts_code = returned ? 2201 : 2200,
                .message = returned ? cstr_d("The types returned by '", path, "' are incompatible between these types.") :
                    cstr_d("The types of '", path, "' are incompatible between these types."),
            });
        } else if (vec_ty_report_line_size(&secondary)) {
            // (the innermost says what the line below it says already)
            vec_ty_report_line_erase_n(&secondary, 0, 1);
        }
        for (isize_t i = 0; i < vec_ty_report_line_size(&secondary); i++) {
            vec_ty_report_line_push(&st->lines, *vec_ty_report_line_at(&secondary, i));
        }
        secondary.size = 0; // (moved)
        vec_ty_report_line_drop(&secondary);
        cstr_drop(&path);
    }
    if (skipped_source) report_relation_error_now(st->checker, st, skipped_source, skipped_target);
}

// tsc's reportErrorResults
static bool is_mutable_array_or_tuple(Checker* me, Ty const* ty);
static void report_relation_error(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool top,
    bool source_has_base
) {
    bool suppress = st->override_next_head > 0;
    if (suppress) st->override_next_head--;
    // (the head stays when what stood in for it is an incompatible step, tsc's headMessage)
    bool pending = vec_ty_report_line_size(&st->lines) && vec_ty_report_line_back(&st->lines)->incompatible;
    // Said below the head whether or not there is one (tsc's tryElaborateErrorsForPrimitivesAndObjects)
    static struct { GlobalType wrapper; TyIntrinsic primitive; } const wrappers[] = {
        { GLOBAL_STRING, TY_STRING }, { GLOBAL_NUMBER, TY_NUMBER }, { GLOBAL_BOOLEAN, TY_BOOLEAN }, { GLOBAL_SYMBOL, TY_ES_SYMBOL },
    };
    u32 primitive = TYF_VOID | TYF_NULLABLE | TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_BOOLEAN | TYF_ES_SYMBOL_LIKE | TYF_LITERAL |
        TYF_ENUM | TYF_ENUM_LITERAL | TYF_TEMPLATE_LITERAL;
    bool wrapper_case = (source->flags & TYF_OBJECT) && (target->flags & primitive);
    for (u32 i = 0; i < c_arraylen(wrappers) && wrapper_case; i++) {
        if (source != global_type(me, wrappers[i].wrapper) || target != ty_intrinsic(me, wrappers[i].primitive)) continue;
        cstr source_str = display_for_report(me, source, false);
        cstr target_str = display_for_report(me, target, false);
        report_line(st, 2692, cstr_d("'", target_str, "' is a primitive, but '", source_str, "' is a wrapper object. Prefer using '",
            target_str, "' when possible."));
        c_drop(cstr, &source_str, &target_str);
    }
    // (tsc's reportErrorResults: about the global Object type as a source, unless the target is a primitive)
    // (Not when the source as written was a class or interface adding nothing to it, which tsc reports about instead:
    // `class C extends Object {}`, sourceHasBase)
    bool object_line = !wrapper_case && !source_has_base && source == global_type(me, GLOBAL_OBJECT);
    if (object_line) {
        report_line(st, 2696, cstr_from("The 'Object' type is assignable to very few other types. Did you mean to use the 'any' type instead?"));
    }
    // A readonly array or tuple where a mutable one is wanted says so instead of the head (tsc's reportErrorResults
    // with tryElaborateArrayLikeErrors)
    if ((source->flags & TYF_OBJECT) && (target->flags & TYF_OBJECT) && is_mutable_array_or_tuple(me, target)) {
        TupleShape const* shape = is_tuple(source) ? source->reference.target->declared.tuple :
            is_empty_tuple(source) ? source->declared.tuple : nullptr;
        bool readonly = shape ? shape->readonly :
            source->kind == TY_REFERENCE && source->reference.target == my readonly_array_target;
        if (readonly) {
            cstr source_str = display_for_report(me, source, false);
            cstr target_str = display_for_report(me, target, false);
            report_line(st, 4104, cstr_d(
                "The type '", source_str, "' is 'readonly' and cannot be assigned to the mutable type '", target_str, "'."
            ));
            c_drop(cstr, &source_str, &target_str);
            suppress = true;
        }
    }
    if (suppress && object_line) {
        // (tsc's reportError of that line flushed the incompatible steps first, so the head it then skips is never
        // reported: lastSkippedInfo waits for a stack that stays empty)
        report_missing_constraint(me, st, source, target);
        return;
    }
    if (suppress) {
        if (!top || !pending) {
            report_missing_constraint(me, st, source, target);
            st->skipped_source = source;
            st->skipped_target = target;
            return;
        }
    }
    report_relation_error_now(me, st, source, target);
}

// tsc's isExactOptionalPropertyMismatch: source may be undefined, target has an optional property's missing
bool ty_exact_optional_mismatch(Checker* me, Ty const* source, Ty const* target) {
    if (!compiler_options()->exact_optional_property_types || !source || !target) return false;
    bool maybe_undefined = source->flags & TYF_UNDEFINED;
    if (source->kind == TY_UNION) {
        for (u32 i = 0; i < source->composite.types.len; i++) {
            if (source->composite.types.data[i]->flags & TYF_UNDEFINED) maybe_undefined = true;
        }
    }
    return maybe_undefined && ty_contains_missing(me, target);
}

static Ty const* apparent_type(Checker* me, Ty const* ty);

// Whether a union lacks a property only in its object literals (and has it in some member)
static bool union_literal_lacks(Checker* me, Ty const* source, Atom const* name) {
    bool some_has = false;
    for (u32 i = 0; i < source->composite.types.len; i++) {
        Ty const* member = apparent_type(me, source->composite.types.data[i]);
        if (ty_find_prop(me, member, name).found) {
            some_has = true;
        } else if (!ty_is_object_literal_without_spread(me, member)) {
            return false;
        }
    }
    return some_has;
}

// Whether a property of `owner` (some of tsc's getPropertiesOfType(target): of a union, the properties every member has,
// which are some of its first member's; of an intersection, every member's) is an optional one of target that source
// has with an undefined it can't take
static bool exact_optional_unassignable_in(Checker* me, Ty const* source, Ty const* target, Ty const* owner, u32 depth) {
    owner = apparent_type(me, owner);
    if (depth > 8) return false;
    if (owner->kind == TY_UNION && !(owner->flags & TYF_BOOLEAN)) {
        return exact_optional_unassignable_in(me, source, target, owner->composite.types.data[0], depth + 1);
    }
    if (owner->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < owner->composite.types.len; i++) {
            if (exact_optional_unassignable_in(me, source, target, owner->composite.types.data[i], depth + 1)) return true;
        }
        return false;
    }
    if (!(owner->flags & TYF_OBJECT)) return false;
    Members const* members = ty_members(me, owner);
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* target_prop = members->props[i];
        Ty const* target_type;
        if (owner != target) {
            // (A union's or an intersection's property is the combined one)
            FoundProp common = ty_find_prop(me, target, target_prop->name);
            if (!common.found || !(common.flags & PROP_OPTIONAL)) continue;
            target_type = ty_optional_prop_type(me, common.type);
        } else {
            if (!(target_prop->flags & PROP_OPTIONAL)) continue;
            target_type = ty_optional_prop_type(me, prop_type(me, target_prop));
        }
        FoundProp source_prop = ty_find_prop(me, source, target_prop->name);
        if (!source_prop.found) {
            // (A union's object literal that lacks it has it as missing, when another member has it: tsc's
            // createUnionOrIntersectionProperty, CheckFlags.WritePartial)
            if (source->kind == TY_UNION && union_literal_lacks(me, source, target_prop->name) &&
                ty_exact_optional_mismatch(me, ty_intrinsic(me, TY_MISSING), target_type)
            ) {
                return true;
            }
            continue;
        }
        Ty const* source_type = source_prop.flags & PROP_OPTIONAL ? ty_optional_prop_type(me, source_prop.type) :
            source_prop.type;
        if (ty_exact_optional_mismatch(me, source_type, target_type)) return true;
    }
    return false;
}

// tsc's getExactOptionalUnassignableProperties(...).length
bool ty_has_exact_optional_unassignable(Checker* me, Ty const* source, Ty const* target) {
    if (!compiler_options()->exact_optional_property_types || !compiler_options()->strict_null_checks) return false;
    if (is_tuple(source) && is_tuple(target)) return false;
    // (Both through their apparent types, `this` or a type parameter as its constraint: tsc's getPropertiesOfType and
    // getTypeOfPropertyOfType)
    target = apparent_type(me, target);
    return exact_optional_unassignable_in(me, apparent_type(me, source), target, target, 0);
}

static Ty const* indexed_or_null(Checker* me, Ty const* object, Ty const* index);

// The relation error line (its first one is the head, which callers replace with their own words: tsc's headMessage)
static void report_relation_error_now(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    flush_incompatible(st);
    bool widen = report_widens(me, target);
    Ty const* generalized = widen && is_literal_type(source) ? ty_base_type_of_literal(me, source) : source;
    cstr source_str, target_str;
    display_pair(me, generalized, widen, target, &source_str, &target_str);
    // (An indexed access into a type parameter counts as one: tsc's reportRelationError looks at its object's flags)
    Ty const* param_target = target->kind == TY_INDEXED && source->kind != TY_INDEXED ? target->indexed.object : target;
    if (param_target->kind == TY_TYPE_PARAM && !param_target->param.state->is_check_marker) {
        // What a type parameter target could be (tsc's reportRelationError), below the head reported next
        Ty const* constraint = target;
        if (target->kind == TY_INDEXED) {
            // (HACK: base_constraint() makes an access that isn't valid anyway, where tsc's getBaseConstraintOfType has
            // none: `T[P]` with `T extends object`. Relations depend on it, so only the report asks first)
            Ty const* object = base_constraint(me, target->indexed.object, 0);
            Ty const* index = base_constraint(me, target->indexed.index, 0);
            bool valid = ty_simplified(me, target, false) != target ||
                indexed_or_null(me, object ? object : target->indexed.object, index ? index : target->indexed.index);
            constraint = valid ? base_constraint(me, target, 0) : nullptr;
        } else {
            for (u32 depth = 0; constraint && constraint->kind == TY_TYPE_PARAM && depth < 16; depth++) {
                constraint = ty_param_constraint(me, constraint);
            }
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
            // (Nothing said below matters then: tsc drops the elaboration so far)
            isize_t kept = 0;
            for (isize_t i = 0; i < vec_ty_report_line_size(&st->lines); i++) {
                TyReportLine* line = vec_ty_report_line_at_mut(&st->lines, i);
                if (line->related) {
                    *vec_ty_report_line_at_mut(&st->lines, kept++) = *line;
                } else {
                    cstr_drop(&line->message);
                }
            }
            st->lines.size = kept;
            report_line(st, 5082, cstr_d(
                "'", target_str, "' could be instantiated with an arbitrary type which could be unrelated to '", source_str, "'."
            ));
        }
    }
    if (st->relation == TY_RELATION_COMPARABLE) {
        report_line(st, 2678, cstr_d("Type '", source_str, "' is not comparable to type '", target_str, "'."));
    } else if (cstr_eq(&source_str, &target_str)) { // (a head too: callers replace it like TS2322)
        report_line(st, 2719, cstr_d("Type '", source_str, "' is not assignable to type '", target_str,
            "'. Two different types with this name exist, but they are unrelated."));
    } else if (ty_has_exact_optional_unassignable(me, source, target)) {
        report_line(st, 2375, cstr_d("Type '", source_str, "' is not assignable to type '", target_str,
            "' with 'exactOptionalPropertyTypes: true'. Consider adding 'undefined' to the types of the target's properties."));
    } else {
        // A string literal that looks like one of a union's (tsc's getSuggestedTypeForNonexistentStringLiteralType)
        Ty const* suggested = nullptr;
        if ((source->flags & TYF_STRING_LITERAL) && !(source->flags & TYF_ENUM_LITERAL) && target->kind == TY_UNION) {
            TyList types = target->composite.types;
            csview* names = malloc(sizeof(csview) * (types.len + 1));
            Ty const** candidates = malloc(sizeof(Ty const*) * (types.len + 1));
            panic_if(!names || !candidates, "out of memory");
            isize count = 0;
            for (u32 i = 0; i < types.len; i++) {
                Ty const* t = types.data[i];
                if (!(t->flags & TYF_STRING_LITERAL)) continue;
                candidates[count] = t;
                names[count++] = atom_sv(t->literal.string);
            }
            isize best = spelling_suggestion(atom_sv(source->literal.string), names, count);
            if (best >= 0) suggested = candidates[best];
            free(names);
            free(candidates);
        }
        if (suggested) {
            cstr suggested_str = display_for_report(me, suggested, false);
            report_line(st, 2820, cstr_d("Type '", source_str, "' is not assignable to type '", target_str, "'. Did you mean '",
                suggested_str, "'?"));
            cstr_drop(&suggested_str);
            c_drop(cstr, &source_str, &target_str);
            return;
        }
        report_line(st, 2322, cstr_d("Type '", source_str, "' is not assignable to type '", target_str, "'."));
    }
    report_missing_constraint(me, st, source, target);
    c_drop(cstr, &source_str, &target_str);
}

// Apparent types and property lookup

static MappedInfo const* mapped_info_of(Checker* me, Ty const* ty, bool* generic);

// tsc's getResolvedApparentTypeOfMappedType: a homomorphic mapped type over a type parameter whose constraint is arrays
// and tuples is that mapped type applied to the constraint (`Partial<T>` with `T extends any[]` is an array)
static Ty const* apparent_type_of_mapped(Checker* me, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS || !(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return ty;
    bool generic = false;
    MappedInfo const* mapped = mapped_info_of(me, ty, &generic);
    if (!mapped || !generic || !mapped->keyof_constraint || mapped->name_type || !mapped->modifiers_type) return ty;
    if (mapped->constraint->kind != TY_KEYOF) return ty; // (Substituted: not homomorphic, see homomorphic_variable())
    // (tsc's getHomomorphicTypeVariable on the constraint, where the keys of a generic mapped type are its constraint's:
    // `Identity<UnboxArray<T>>` iterates over `keyof T`)
    Ty const* operand = mapped->constraint->keyof.operand;
    for (u32 depth = 0; operand->kind == TY_ANONYMOUS && depth < 8; depth++) {
        bool inner_generic = false;
        MappedInfo const* inner = mapped_info_of(me, operand, &inner_generic);
        if (!inner || !inner_generic || inner->name_type || inner->constraint->kind != TY_KEYOF) break;
        operand = inner->constraint->keyof.operand;
    }
    Ty const* variable = ty_actual_type_variable(me, operand);
    if (variable->kind != TY_TYPE_PARAM) return ty;
    Ty const* constraint = base_constraint(me, variable, 0);
    if (!constraint || constraint == variable || (constraint->flags & TYF_NEVER)) return ty;
    TyList parts = constraint->kind == TY_UNION ? constraint->composite.types : (TyList){ .data = &constraint, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        Ty const* part = parts.data[i];
        if (!is_tuple(part) && !(part->kind == TY_REFERENCE && is_array_target(me, part->reference.target))) return ty;
    }
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = &variable, .len = 1 }, (TyList){ .data = &constraint, .len = 1 });
    return ty_instantiate(me, ty, mapper);
}

static Ty const* apparent_type_inner(Checker* me, Ty const* ty);
// tsc's getReducedApparentType
static inline Ty const* apparent_type(Checker* me, Ty const* ty) {
    return ty_reduced(me, apparent_type_inner(me, ty_reduced(me, ty)));
}

static Ty const* apparent_type_resolve(Checker* me, Ty const* ty);

// A generic intersection or instantiable type whose apparent type needs itself (a constraint feeding its own `infer`:
// `C extends Ctor<Props<C> & X>`) is circular, and apparently unknown (tsc's circularConstraintType through
// pushTypeResolution in getResolvedBaseConstraint)
static Ty const* apparent_type_inner(Checker* me, Ty const* ty) {
    bool generic = ty->kind == TY_INTERSECTION || ty->kind == TY_INDEXED || ty->kind == TY_CONDITIONAL ||
        ty->kind == TY_SUBSTITUTION;
    if (!generic) return apparent_type_resolve(me, ty);
    for (u32 i = 0; i < my apparent_depth; i++) {
        if (my apparent_stack[i] == ty) return ty_intrinsic(me, TY_UNKNOWN_);
    }
    if (my apparent_depth == c_arraylen(my apparent_stack)) return ty_intrinsic(me, TY_UNKNOWN_);
    my apparent_stack[my apparent_depth++] = ty;
    Ty const* ret = apparent_type_resolve(me, ty);
    my apparent_depth--;
    return ret;
}

static Ty const* apparent_type_resolve(Checker* me, Ty const* ty) {
    if (ty->kind == TY_ANONYMOUS) return apparent_type_of_mapped(me, ty);
    if (ty->kind == TY_TYPE_PARAM) {
        // Through the constraint, which sees the type parameter as its `this` (`this.x` inside a class is `this`'s
        // x, and so is `t.x` for `t: T` with `T extends C`: tsc's getApparentType + getTypeWithThisArgument)
        Ty const* param = ty;
        for (u32 depth = 0; ty->kind == TY_TYPE_PARAM && depth < 16; depth++) {
            Ty const* constraint = ty_param_constraint(me, ty);
            // (`T extends R | string` with R unconstrained has no base constraint either: union_member_lacks_base_constraint)
            if (!constraint || constraint == ty || union_member_lacks_base_constraint(me, constraint, 0)) {
                // (tsc's getApparentType: the base constraint is unknown, which is `{}` only without strictNullChecks)
                return compiler_options()->strict_null_checks ? ty_intrinsic(me, TY_UNKNOWN_) : global_type(me, GLOBAL_EMPTY_OBJECT);
            }
            ty = constraint;
        }
        if (ty->flags & (TYF_OBJECT | TYF_INTERSECTION)) return ty_with_this_arg(me, ty, param);
    }
    if (ty->kind == TY_INTERSECTION) {
        // The intersection of what its generic members are apparently (tsc's getApparentTypeOfIntersectionType, through
        // getTypeWithThisArgument(needApparentType)): `NonNullable<T["p"]>` with `p?: X` is `(X | undefined) & {}`,
        // which is X
        bool generic = false;
        for (u32 i = 0; i < ty->composite.types.len && !generic; i++) {
            Ty const* member = ty->composite.types.data[i];
            generic = member->kind == TY_TYPE_PARAM || member->kind == TY_INDEXED || member->kind == TY_CONDITIONAL ||
                member->kind == TY_SUBSTITUTION;
        }
        if (!generic) return ty;
        Ty const* stack_buf[16];
        TyList members = ty->composite.types;
        Ty const** parts = members.len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * members.len);
        panic_if(!parts, "out of memory");
        bool changed = false;
        for (u32 i = 0; i < members.len; i++) {
            Ty const* member = members.data[i];
            bool member_generic = member->kind == TY_TYPE_PARAM || member->kind == TY_INDEXED ||
                member->kind == TY_CONDITIONAL || member->kind == TY_SUBSTITUTION;
            parts[i] = member_generic ? apparent_type(me, member) : member;
            // (A constraint that is unknown or any says nothing: the member stays as it is. HACK: tsc's default constraint
            // of a conditional type leaves an any branch out, base_constraint() doesn't)
            if (parts[i]->flags & (TYF_UNKNOWN | TYF_ANY)) parts[i] = member;
            changed |= parts[i] != member;
        }
        Ty const* ret = changed ? ty_intersection(me, parts, members.len, nullptr, (TyList){0}) : ty;
        if (parts != stack_buf) free(parts);
        // (What the members made of it may be apparent itself: `T & object` without strictNullChecks is `{} & object`,
        // which is `object`, apparently `{}`)
        if (ret != ty && ret->kind != TY_INTERSECTION) ret = apparent_type(me, ret);
        return ret;
    }
    if (ty->kind == TY_INDEXED || ty->kind == TY_CONDITIONAL || ty->kind == TY_SUBSTITUTION) {
        // (tsc's getApparentType: an instantiable type is its base constraint, which sees it as its `this` when that is
        // a class or interface or an intersection: a chain of `this`-returning builder methods on `T` stays generic,
        // `PgSelectJoin<PgSelectJoin<T, ...>, ...>`)
        Ty const* constraint = base_constraint(me, ty, 0);
        if (constraint && constraint != ty) {
            if (constraint->kind == TY_REFERENCE || constraint->kind == TY_INTERSECTION) {
                return apparent_type(me, ty_with_this_arg(me, constraint, ty));
            }
            return apparent_type(me, constraint);
        }
        // (Without a base constraint, unknown: `Mapped6<K>[string]` has no properties)
        if (!constraint) return compiler_options()->strict_null_checks ? ty_intrinsic(me, TY_UNKNOWN_) : global_type(me, GLOBAL_EMPTY_OBJECT);
    }
    u32 f = ty->flags;
    Ty const* global = nullptr;
    // (A computed enum member, TY_LITERAL without a value: `declare enum E { A }`'s members, is NumberLike)
    if (ty->kind == TY_INTRINSIC || (ty->kind == TY_LITERAL && !(f & TYF_ENUM)) || (f & TYF_BOOLEAN)) {
        if (f & (TYF_STRING | TYF_STRING_LITERAL)) global = global_type(me, GLOBAL_STRING);
        else if (f & (TYF_NUMBER | TYF_NUMBER_LITERAL)) global = global_type(me, GLOBAL_NUMBER);
        else if (f & (TYF_BOOLEAN | TYF_BOOLEAN_LITERAL)) global = global_type(me, GLOBAL_BOOLEAN);
        else if (f & TYF_ES_SYMBOL_LIKE) global = global_type(me, GLOBAL_SYMBOL);
        else if (f & (TYF_BIGINT | TYF_BIGINT_LITERAL)) global = global_type(me, GLOBAL_BIGINT);
        else if (f & TYF_NON_PRIMITIVE) global = global_type(me, GLOBAL_EMPTY_OBJECT);
        // (tsc's getApparentType: unknown is `{}` without strictNullChecks, `T extends any`'s constraint included)
        else if ((f & TYF_UNKNOWN) && !compiler_options()->strict_null_checks) global = global_type(me, GLOBAL_EMPTY_OBJECT);
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
        } else if (shape->flags[i] & TUPLE_ELEMENT_REST) {
            // A variadic element `...T` contributes T[number] (tsc's getTupleBaseType)
            element = ty_indexed(me, element, ty_intrinsic(me, TY_NUMBER));
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
        // (tsc's anyFunctionType, what a context-sensitive function is while it's skipped, has Function's)
        if (ty == my global_types[GLOBAL_ANY_FUNCTION]) {
            fallback = global_type(me, GLOBAL_FUNCTION);
        } else if (members->call_count || members->construct_count) {
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

// `skip_augment`: only the types' own properties, not those of Object or Function (tsc's getPropertyOfType's
// skipObjectFunctionPropertyAugment, which an intersection asks its members with first)
static FoundProp find_prop_in(Checker* me, Ty const* ty, Atom const* name, u32 depth, bool skip_augment);
static FoundProp find_prop(Checker* me, Ty const* ty, Atom const* name, u32 depth) {
    return find_prop_in(me, ty, name, depth, false);
}

static FoundProp find_prop_in(Checker* me, Ty const* ty, Atom const* name, u32 depth, bool skip_augment) {
    FoundProp ret = {0};
    if (depth > 8) return ret;
    if (ty->kind == TY_INTERSECTION && ty_reduced(me, ty) != ty) return ret; // (tsc's getReducedApparentType)
    if (ty->kind == TY_INTERSECTION) {
        // The property of an intersection is a type of its own (tsc makes a synthetic symbol per name as well)
        u64 key[2] = { ty->id << 1 | skip_augment, (u64)(uintptr_t)name };
        FoundProp const* cached = intern_get(&my intersection_props, key, 2);
        if (cached) return *cached;
        Ty const* stack_buf[16];
        u32 len = 0;
        bool all_optional = true, any_member = false, any_non_member = false, all_readonly = true, any_public = false;
        u32 flags = 0;
        u32 optional_parts = 0; // (bit i: stack_buf[i] came from an optional property)
        // (The members' own properties first, Object's and Function's only if none has one)
        bool own_only = true;
        again:
        for (u32 i = 0; i < ty->composite.types.len && len < c_arraylen(stack_buf); i++) {
            // Its members see the whole intersection as their `this` (tsc's getApparentTypeOfIntersectionType):
            // `(R & { T: X })['component']` with `component: (props: this['T']) => void` in R takes X
            Ty const* member = ty->composite.types.data[i];
            // (One that has a `this` already keeps it, `RefI<this = DB>` of a class's base: tsc's
            // getTypeWithThisArgument leaves a reference with its this-argument as it is)
            if (member->kind != TY_TYPE_PARAM && !(member->kind == TY_REFERENCE && member->reference.this_arg)) {
                member = ty_with_this_arg(me, member, ty);
            }
            FoundProp part = find_prop_in(me, apparent_type(me, member), name, depth + 1, own_only);
            if (!part.found) continue;
            if (!ret.prop) ret.prop = part.prop;
            if (part.flags & PROP_OPTIONAL) optional_parts |= 1u << len;
            stack_buf[len++] = part.type;
            // Only class members (properties, methods, accessors) have a say: a variable of a namespace or of
            // `typeof globalThis` is neither optional nor required (tsc's createUnionOrIntersectionProperty)
            bool class_member = !part.prop || !part.prop->decl->symbol;
            if (class_member) all_optional &= (part.flags & PROP_OPTIONAL) != 0;
            any_member |= class_member;
            any_non_member |= !class_member;
            flags |= part.flags & ~(u32)PROP_OPTIONAL;
            all_readonly &= (part.flags & PROP_READONLY) != 0;
            any_public |= !(part.flags & (PROP_PRIVATE | PROP_PROTECTED));
        }
        // (Readonly only if every member's is: tsc's createUnionOrIntersectionProperty)
        if (!all_readonly) flags &= ~(u32)PROP_READONLY;
        // (Private if any member's is, else public if any is: tsc's getDeclarationModifierFlagsFromSymbol of a synthetic
        // property)
        if (!(flags & PROP_PRIVATE) && any_public) flags &= ~(u32)PROP_PROTECTED;
        if (!len && own_only && !skip_augment) {
            own_only = false;
            goto again;
        }
        if (len) {
            ret.found = true;
            // tsc intersects the members' getTypeOfSymbol, which includes an optional property's undefined: in
            // `{ name?: X } & { name: undefined }` the property is required and `undefined`, not `X & undefined`.
            // (When all are optional the result is optional and its readers add undefined themselves.)
            bool optional_result = all_optional && any_member;
            // (Under exactOptionalPropertyTypes that's the missing type: `{ x: X | undefined } & { x?: X }` has a
            // required x of `X | missing`, which an optional target property doesn't take)
            if (optional_parts && !optional_result && compiler_options()->strict_null_checks) {
                for (u32 i = 0; i < len; i++) {
                    if (optional_parts & (1u << i)) stack_buf[i] = ty_optional_prop_type(me, stack_buf[i]);
                }
            }
            ret.type = ty_intersection(me, stack_buf, len, nullptr, (TyList){0});
            ret.flags = flags | (all_optional && any_member ? PROP_OPTIONAL : 0);
            // An optional member's property next to one that isn't a member's (a namespace's function: `typeof f &
            // { m?: X }`) is optional, but its type is `F & (X | undefined)`, without undefined: as readers add it to
            // an optional property's type, it is required to them
            if (optional_result && any_non_member && optional_adds_undefined()) {
                Ty const* undefined = ty_intrinsic(me, TY_UNDEFINED);
                bool keeps_undefined = true;
                for (u32 i = 0; i < len && keeps_undefined; i++) {
                    Ty const* part = stack_buf[i];
                    keeps_undefined = (optional_parts & (1u << i)) ||
                        (part->flags & (TYF_ANY | TYF_UNKNOWN | TYF_UNDEFINED | TYF_VOID | TYF_TYPE_PARAM | TYF_DEFERRED));
                    for (u32 j = 0; part->kind == TY_UNION && j < part->composite.types.len && !keeps_undefined; j++) {
                        keeps_undefined = part->composite.types.data[j] == undefined;
                    }
                }
                if (!keeps_undefined) ret.flags &= ~(u32)PROP_OPTIONAL;
            }
        }
        FoundProp* memo = arena_new(&my arena, FoundProp);
        *memo = ret;
        *intern_slot(me, &my intersection_props, key, 2) = memo;
        return ret;
    }
    if (ty->kind == TY_UNION && !(ty->flags & TYF_BOOLEAN)) {
        // A member of an intersection whose apparent type is a union (`T & X` with `T extends A | B`): every member
        // has to have it (tsc's createUnionOrIntersectionProperty, reached through getPropertyOfType of the union)
        TyList members = ty->composite.types;
        Ty const* stack_buf[16];
        Ty const** types = members.len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * members.len);
        u32 flags = 0;
        bool missing = false;
        for (u32 i = 0; i < members.len && !missing; i++) {
            FoundProp part = find_prop_in(me, apparent_type(me, members.data[i]), name, depth + 1, skip_augment);
            missing = !part.found;
            if (missing) break;
            if (!ret.prop) ret.prop = part.prop;
            types[i] = part.type;
            flags |= part.flags;
        }
        if (!missing && members.len) {
            ret.found = true;
            ret.type = ty_union(me, types, members.len, nullptr, (TyList){0});
            ret.flags = flags;
        } else {
            ret.prop = nullptr;
        }
        if (types != stack_buf) free(types);
        return ret;
    }
    if (!(ty->flags & TYF_OBJECT)) return ret;
    Prop const* prop = skip_augment ? ty_get_prop(me, ty, name) : ty_get_object_prop(me, ty, name);
    if (prop) {
        ret.found = true;
        ret.type = prop_type(me, prop);
        ret.flags = prop->flags;
        ret.prop = prop;
    }
    return ret;
}

static bool source_has_prop(Checker* me, Ty const* ty, Atom const* name, u32 depth);
static bool is_static_private_name(Checker* me, Prop const* prop);

// tsc's getUnmatchedProperty from the target's `from`th property on: one the source lacks (optional ones only with
// require_optional)
static bool has_unmatched_target_prop(Checker* me, Ty const* source, Members const* target_members, u32 from, bool require_optional) {
    for (u32 i = from; i < target_members->prop_count; i++) {
        Prop const* target_prop = target_members->props[i];
        if (target_prop->flags & PROP_PROTOTYPE) continue;
        if ((target_prop->flags & PROP_OPTIONAL) && !require_optional) continue;
        if (source_has_prop(me, source, target_prop->name, 0) || is_static_private_name(me, target_prop)) continue;
        return true;
    }
    return false;
}

// find_prop()'s answer to whether there is one, without typing it (tsc's getPropertyOfType in getUnmatchedProperty)
static bool source_has_prop(Checker* me, Ty const* ty, Atom const* name, u32 depth) {
    if (depth > 8) return false;
    if (ty->kind == TY_INTERSECTION) return find_prop(me, ty, name, depth).found; // (precomputed)
    if (ty->kind == TY_UNION && !(ty->flags & TYF_BOOLEAN)) {
        TyList members = ty->composite.types;
        for (u32 i = 0; i < members.len; i++) {
            if (!source_has_prop(me, apparent_type(me, members.data[i]), name, depth + 1)) return false;
        }
        return members.len > 0;
    }
    if (!(ty->flags & TYF_OBJECT)) return false;
    return ty_get_object_prop(me, ty, name) != nullptr;
}

FoundProp ty_find_prop(Checker* me, Ty const* ty, Atom const* name) { return find_prop(me, ty, name, 0); }
Prop const* ty_property_of_type(Checker* me, Ty const* ty, Atom const* name) { return find_prop(me, ty, name, 0).prop; }
Ty const* ty_tuple_as_array(Checker* me, Ty const* tuple) { return tuple_as_array(me, tuple); }
Ty const* ty_apparent_type(Checker* me, Ty const* ty) { return apparent_type(me, ty); }
bool ty_is_tuple(Ty const* ty) { return is_tuple(ty); }
TupleShape const* ty_tuple_shape(Ty const* ty) {
    if (is_tuple(ty)) return ty->reference.target->declared.tuple;
    return ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_TUPLE_TARGET ? ty->declared.tuple : nullptr;
}
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
        TupleShape const* shape = ty_tuple_shape(rest); // (`[]` too, which is no reference)
        if (shape) {
            // `...args: [a: A, b?: B, ...c: C[]]` is parameters of their own
            TyList elements = shape->arity ? ty_reference_args(me, rest) : (TyList){0};
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

// Whether position i is the rest parameter of a signature whose rest parameter is `[]`
static bool past_empty_rest(Signature const* sig, u32 i) {
    if (!sig->param_count || i + 1 < sig->param_count || !sig->params[sig->param_count - 1].rest) return false;
    Ty const* rest = sig->params[sig->param_count - 1].type;
    return rest->kind == TY_DECLARED && rest->declared.kind == TY_DECLARED_TUPLE_TARGET && !rest->declared.tuple->arity;
}

// How many parameters a signature has, with a rest tuple of fixed length counted as its elements
u32 ty_signature_param_count(Signature const* sig) {
    if (!sig->param_count || !sig->params[sig->param_count - 1].rest) return sig->param_count;
    TupleShape const* shape = ty_tuple_shape(sig->params[sig->param_count - 1].type);
    if (!shape) return sig->param_count;
    return sig->param_count - 1 + shape->arity;
}

// The arguments a signature needs, the required elements of a rest tuple included (tsc's getMinArgumentCount, less
// its trailing void parameters)
static u32 signature_min_args(Signature const* sig) {
    if (!sig->param_count || !sig->params[sig->param_count - 1].rest) return sig->min_args;
    TupleShape const* shape = ty_tuple_shape(sig->params[sig->param_count - 1].type);
    if (!shape) return sig->min_args;
    u32 required = 0;
    while (required < shape->arity && !(shape->flags[required] & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST))) required++;
    return required ? sig->param_count - 1 + required : sig->min_args;
}

// Takes any number of arguments: a rest parameter that isn't a tuple of fixed length
bool ty_signature_has_rest(Signature const* sig) {
    if (!sig->param_count || !sig->params[sig->param_count - 1].rest) return false;
    TupleShape const* shape = ty_tuple_shape(sig->params[sig->param_count - 1].type);
    if (!shape) return true;
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
    Signature const* ret = ty_single_signature_non_nullable(me, type, false, false);
    return ret && !signature_predicate(me, ret) ? ret : nullptr;
}

// tsc's getMinArgumentCount: trailing parameters that accept void can be left out
static u32 min_argument_count(Checker* me, Signature const* sig) {
    u32 min = sig->min_args;
    while (min > 0) {
        Ty const* type = param_type_at(me, sig, min - 1);
        bool accepts_void = type && (type->flags & TYF_VOID);
        for (u32 i = 0; type && type->kind == TY_UNION && i < type->composite.types.len && !accepts_void; i++) {
            accepts_void = (type->composite.types.data[i]->flags & TYF_VOID) != 0;
        }
        if (!accepts_void) break;
        min--;
    }
    return min;
}

// tsc's getMinArgumentCount with the required elements of a rest tuple (`(...args: [a: A]) => R` takes one)
static u32 tuple_min_argument_count(Checker* me, Signature const* sig) {
    u32 min = min_argument_count(me, sig);
    if (!sig->param_count || !sig->params[sig->param_count - 1].rest) return min;
    TupleShape const* shape = ty_tuple_shape(sig->params[sig->param_count - 1].type);
    if (!shape) return min;
    u32 required = 0;
    while (required < shape->arity && !(shape->flags[required] & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST))) required++;
    u32 with_rest = sig->param_count - 1 + required;
    return required && with_rest > min ? with_rest : min;
}

// `return_code`: what failing return types say (tsc's incompatibleErrorReporter), 0 for nothing: 2202-2205, see
// report_incompatible()
// tsc's getNonArrayRestType
static Ty const* non_array_rest_type(Checker* me, Signature const* sig) {
    if (!sig->param_count || !sig->params[sig->param_count - 1].rest) return nullptr;
    Ty const* rest = sig->params[sig->param_count - 1].type;
    if ((rest->flags & TYF_ANY) || (rest->kind == TY_REFERENCE && is_array_target(me, rest->reference.target))) return nullptr;
    return rest;
}

// A parameter's name as tsc's getParameterNameAtPosition gives it: its symbol's, which is `__<index>` for a binding pattern
[[gnu::cold]] static cstr message_param_name(Signature const* sig, u32 index) {
    FunctionArg const* decl = sig->params[index].decl;
    if (decl && decl->initializer.binding.tag != BINDING_IDENTIFIER) return cstr_d("__", index);
    return cstr_from_sv(atom_sv(sig->params[index].name));
}

// tsc's getEffectiveRestType: a rest parameter that isn't a tuple (any as any[]), else what the tuple has from its first
// rest or variadic element on (`...X[]` is X[], `...T` is T), null if it has none
static Ty const* effective_rest_type(Checker* me, Signature const* sig) {
    Ty const* rest_param = sig->param_count && sig->params[sig->param_count - 1].rest ? sig->params[sig->param_count - 1].type : nullptr;
    if (!rest_param) return nullptr;
    TupleShape const* rest_shape = ty_tuple_shape(rest_param);
    Ty const* effective = rest_shape ? nullptr : rest_param;
    if (rest_shape) {
        u32 first = 0;
        while (first < rest_shape->arity && !(rest_shape->flags[first] & TUPLE_ELEMENT_REST)) first++;
        if (first < rest_shape->arity) {
            TyList elements = ty_reference_args(me, rest_param);
            if (first + 1 == rest_shape->arity) {
                effective = elements.data[first];
            } else {
                Ty const** parts = arena_new_n(&my arena, Ty const*, rest_shape->arity - first);
                for (u32 k = first; k < rest_shape->arity; k++) parts[k - first] = elements.data[k];
                TupleShape shape = { .arity = rest_shape->arity - first, .flags = rest_shape->flags + first };
                effective = ty_reference(me, ty_tuple_target(me, &shape), (TyList){ .data = parts, .len = shape.arity });
            }
        }
    }
    if (effective && (effective->flags & TYF_ANY)) effective = ty_array(me, ty_intrinsic(me, TY_ANY), false);
    return effective;
}

// tsc's getNonArrayRestType
static Ty const* non_array_effective_rest(Checker* me, Signature const* sig) {
    if (!sig->param_count || !sig->params[sig->param_count - 1].rest) return nullptr;
    Ty const* rest = effective_rest_type(me, sig);
    return rest && !ty_is_array(me, rest) && !(rest->flags & TYF_ANY) ? rest : nullptr;
}

// The parameters from `i` on as one type (tsc's getRestTypeAtPosition: a rest parameter there as it is, else the tuple
// of them), any if that's an array of any (getRestOrAnyTypeAtPosition)
[[gnu::noinline]] static Ty const* rest_or_any_type_at(Checker* me, Signature const* sig, u32 pos) {
    Ty const* rest_param = sig->param_count && sig->params[sig->param_count - 1].rest ? sig->params[sig->param_count - 1].type : nullptr;
    TupleShape const* rest_shape = rest_param ? ty_tuple_shape(rest_param) : nullptr;
    Ty const* effective = effective_rest_type(me, sig);
    u32 count = ty_signature_param_count(sig);
    Ty const* ret;
    if (effective && pos + 1 >= count) {
        ret = pos + 1 == count ? effective : ty_array(me, ty_indexed(me, effective, ty_intrinsic(me, TY_NUMBER)), false);
    } else {
        // (The tuple of the positions from here on, the effective rest last: tsc's createTupleType there)
        u32 len = count > pos ? count - pos : 0;
        u32 min = signature_min_args(sig);
        Ty const** elements = arena_new_n(&my arena, Ty const*, len ? len : 1);
        unsigned char* flags = arena_new_n(&my arena, unsigned char, len ? len : 1);
        Atom const** names = arena_new_n(&my arena, Atom const*, len ? len : 1);
        bool named = false;
        u32 own = sig->param_count - (rest_param ? 1 : 0);
        for (u32 i = pos; i < pos + len; i++) {
            bool last = effective && i + 1 == count;
            elements[i - pos] = last ? effective : param_type_at(me, sig, i);
            flags[i - pos] = last ? TUPLE_ELEMENT_REST : i < min ? 0 : TUPLE_ELEMENT_OPTIONAL;
            if (!elements[i - pos]) elements[i - pos] = ty_intrinsic(me, TY_UNKNOWN_);
            // (Labelled like the parameters or tuple elements they are: tsc's getNameableDeclarationAtPosition)
            SigParam const* param = i < own ? &sig->params[i] : rest_param && !rest_shape ? &sig->params[own] : nullptr;
            names[i - pos] = param ? (param->decl && param->decl->initializer.binding.tag == BINDING_IDENTIFIER ? param->name : nullptr) :
                rest_shape && rest_shape->names && i - own < rest_shape->arity ? rest_shape->names[i - own] : nullptr;
            named |= names[i - pos] != nullptr;
        }
        TupleShape shape = { .arity = len, .flags = flags, .names = named ? names : nullptr };
        Ty const* target = ty_tuple_target(me, &shape);
        ret = len ? ty_reference(me, target, (TyList){ .data = elements, .len = len }) : target;
    }
    // (tsc's getRestOrAnyTypeAtPosition)
    Ty const* element = ty_is_array(me, ret) ? ty_reference_args(me, ret).data[0] : nullptr;
    return element && (element->flags & TYF_ANY) ? ty_intrinsic(me, TY_ANY) : ret;
}

// tsc's isTopSignature: `(...args: any) => any`, `(...args: never) => unknown` and the like, which every signature is
// assignable to
static bool is_top_signature(Checker* me, Signature const* sig) {
    if (sig->type_params.len || (sig->this_type && !(sig->this_type->flags & TYF_ANY)) || sig->param_count != 1 ||
        !sig->params[0].rest
    ) {
        return false;
    }
    Ty const* rest = sig->params[0].type;
    if (ty_is_array(me, rest)) rest = ty_reference_args(me, rest).data[0];
    return (rest->flags & (TYF_ANY | TYF_NEVER)) && (signature_return_type(me, sig)->flags & (TYF_ANY | TYF_UNKNOWN));
}

static Ternary compare_signatures(
    Checker* me, RelationState* st, Signature const* source, Signature const* target, CallbackMode callback, bool report,
    unsigned return_code
) {
    signature_resolve(me, source);
    signature_resolve(me, target);
    if (source == target) return TERNARY_TRUE;
    // A top signature target takes anything; under the subtype relations a top source only takes another one
    // (SignatureCheckMode.StrictTopSignature, which callbacks don't pass on)
    bool strict_top = !callback && (st->relation == TY_RELATION_SUBTYPE || st->relation == TY_RELATION_STRICT_SUBTYPE);
    bool source_top = strict_top && is_top_signature(me, source);
    bool target_top = is_top_signature(me, target);
    if (!source_top && target_top) return TERNARY_TRUE;
    if (source_top && !target_top) return TERNARY_FALSE;
    // A generic source is instantiated in the context of the target (whose type parameters, if any, are canonical
    // already: instantiations share them)
    if (source->type_params.len && source->type_params.data != target->type_params.data) {
        source = ty_instantiate_signature_in_context_of(me, source, target, nullptr);
    }

    // A rest parameter that isn't an array relates by its elements, not by what it's instantiated with
    Ty const* non_array_rest = non_array_rest_type(me, source);
    if (!non_array_rest) non_array_rest = non_array_rest_type(me, target);
    if (non_array_rest) report_variance(me, non_array_rest, VARIANCE_UNRELIABLE);

    u32 target_count = ty_signature_param_count(target);
    u32 source_min = source->min_args > target_count ? min_argument_count(me, source) : source->min_args;
    // The strict subtype relation counts optional parameters too (tsc's SignatureCheckMode.StrictArity): of `() => void`
    // and `(b?: string) => void`, only the first is a strict subtype of the other
    bool strict_arity = st->relation == TY_RELATION_STRICT_SUBTYPE && !ty_signature_has_rest(target) &&
        (ty_signature_has_rest(source) || ty_signature_param_count(source) > target_count);
    if (strict_arity) return TERNARY_FALSE;
    if (!ty_signature_has_rest(target) && source_min > target_count) {
        if (report) {
            report_line(st, 2849, cstr_d(
                "Target signature provides too few arguments. Expected ", source_min, " or more, but got ",
                target_count, "."
            ));
        }
        return TERNARY_FALSE;
    }

    bool strict = !callback && st->relation != TY_RELATION_COMPARABLE && compiler_options()->strict_function_types &&
        !(target->flags & SIGNATURE_METHOD) &&
        // (A class's `constructor` declaration is bivariant, a construct signature type isn't: tsc's strictVariance
        // goes by the target's declaration kind)
        !((target->flags & SIGNATURE_CONSTRUCT) && (target->function && target->function->is_constructor && !target->function->is_type));
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
    u32 source_count = ty_signature_param_count(source);
    u32 count = source_count > target_count ? source_count : target_count;
    // With a rest parameter that isn't an array on either side, the positions both have, the last of them relating
    // what's left of each as a whole (tsc's compareSignaturesRelated: restIndex, getRestOrAnyTypeAtPosition)
    // (tsc's getNonArrayRestType: the effective rest, `T` of `...args: [a, ...T]`, isn't an array)
    Ty const* whole_rest = non_array_effective_rest(me, source);
    if (!whole_rest) whole_rest = non_array_effective_rest(me, target);
    if (whole_rest) count = source_count < target_count ? source_count : target_count;
    for (u32 i = 0; i < count; i++) {
        // (A position past a rest `[]` has no type: tsc's tryGetTypeAtPosition)
        if (past_empty_rest(source, i) || past_empty_rest(target, i)) break;
        bool rest_index = whole_rest && i + 1 == count;
        Ty const* source_type = rest_index ? rest_or_any_type_at(me, source, i) : param_type_at(me, source, i);
        Ty const* target_type = rest_index ? rest_or_any_type_at(me, target, i) : param_type_at(me, target, i);
        if (!source_type || !target_type) break;
        // (With strict arity an optional source parameter against a required one fails even of the same type: tsc's
        // `sourceType !== targetType || checkMode & StrictArity`)
        bool arity_check = st->relation == TY_RELATION_STRICT_SUBTYPE && i >= tuple_min_argument_count(me, source) &&
            i < tuple_min_argument_count(me, target);
        if (source_type == target_type && !arity_check) continue;
        if (arity_check) {
            // `(x?: T) => void` isn't a strict subtype of `(x: T | undefined) => void`, the other way around it is
            if (is_related_to(me, st, source_type, target_type, false)) return TERNARY_FALSE;
        }
        // Two callbacks: their parameters are output positions, so only the signatures are compared, the other way
        // around (tsc's compareSignaturesRelated). A Promise<T> that uses T in callbacks only is covariant in T
        Signature const* source_callback = callback ? nullptr : callback_signature(me, source, i, source_type);
        Signature const* target_callback = source_callback ? callback_signature(me, target, i, target_type) : nullptr;
        Ternary related = TERNARY_FALSE;
        if (target_callback &&
            ty_undefined_or_null_facts(me, source_type) == ty_undefined_or_null_facts(me, target_type)
        ) {
            related = compare_signatures(me, st, target_callback, source_callback,
                strict ? CALLBACK_STRICT : CALLBACK_BIVARIANT, report, return_code);
        } else {
            if (!callback && !strict) related = is_related_to(me, st, source_type, target_type, false);
            if (!related) related = is_related_to(me, st, target_type, source_type, report);
        }
        if (!related) {
            if (report) {
                u32 source_index = i < source->param_count ? i : source->param_count - 1;
                u32 target_index = i < target->param_count ? i : target->param_count - 1;
                cstr source_name = message_param_name(source, source_index), target_name = message_param_name(target, target_index);
                report_line(st, 2328, cstr_d("Types of parameters '", source_name, "' and '", target_name, "' are incompatible."));
                c_drop(cstr, &source_name, &target_name);
            }
            return TERNARY_FALSE;
        }
        result &= related;
    }

    // A return type that is being inferred further up the stack is any here, without making that inference circular
    // (tsc's isResolvingReturnTypeOfSignature): a source's is still related (any isn't a subtype of string)
    if (ty_signature_return_resolving(me, target)) return result;
    Ty const* target_return = signature_return_type(me, target);
    if (target_return->flags & (TYF_VOID | TYF_ANY)) return result;
    Ty const* source_return = ty_signature_return_resolving(me, source) ? ty_intrinsic(me, TY_ANY) :
        signature_return_type(me, source);
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
    if (!related && report && return_code) {
        cstr source_str = display_for_report(me, source_return, false);
        cstr target_str = display_for_report(me, target_return, false);
        bool construct = return_code == 2203 || return_code == 2205;
        report_incompatible(st, return_code, cstr_d(construct ? "Construct" : "Call", " signature return types '", source_str,
            "' and '", target_str, "' are incompatible."));
        c_drop(cstr, &source_str, &target_str);
    }
    return result & related;
}

// The return_code of compare_signatures() for signatures_related()
static unsigned signature_return_code(Signature const* source, Signature const* target, bool construct) {
    bool no_params = !source->param_count && !target->param_count;
    return construct ? (no_params ? 2205 : 2203) : (no_params ? 2204 : 2202);
}

// tsc's getErasedSignature: every type parameter is any
Signature const* ty_erased_signature(Checker* me, Signature const* sig) {
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

// A class's `constructor` declaration's signature with the class's type parameters erased: tsc gives a constructor's
// signature the class's (getSignatureFromDeclaration), which getErasedSignature then makes any
static Signature const* erase_constructor_class_params(Checker* me, Signature const* sig) {
    if (!sig->function || !sig->function->is_constructor) return sig;
    Symbol const* class_symbol = function_impl_rare(sig->function)->constructed_class;
    if (!class_symbol) return sig;
    TyList params = ty_local_type_params(ty_declared_type_of(me, class_symbol));
    if (!params.len) return sig;
    Ty const** anys = arena_new_n(&my arena, Ty const*, params.len);
    for (u32 i = 0; i < params.len; i++) anys[i] = ty_intrinsic(me, TY_ANY);
    Signature* ret = arena_new(&my arena, Signature);
    *ret = (Signature){
        .flags = sig->flags,
        .target = sig,
        .mapper = ty_mapper_new(me, params, (TyList){ .data = anys, .len = params.len }),
    };
    signature_resolve(me, ret);
    return ret;
}

bool ty_implementation_compatible_with_overload(Checker* me, Signature const* implementation, Signature const* overload) {
    Signature const* source = erase_constructor_class_params(me, ty_erased_signature(me, implementation));
    Signature const* target = erase_constructor_class_params(me, ty_erased_signature(me, overload));
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
    // (compareSignaturesRelated, not signaturesRelatedTo: a constructor's accessibility or abstractness doesn't matter,
    // so both are compared as call signatures)
    copy->flags &= ~(u32)SIGNATURE_CONSTRUCT;
    if (source->flags & SIGNATURE_CONSTRUCT) {
        Signature* source_copy = arena_new(&my arena, Signature);
        *source_copy = *source;
        source_copy->flags &= ~(u32)SIGNATURE_CONSTRUCT;
        source = source_copy;
    }
    Ty const* source_type = ty_signature_type(me, source);
    return ty_is_related(me, source_type, ty_signature_type(me, copy), TY_RELATION_ASSIGNABLE);
}

// Instantiations of the same generic type or type literal, whose signatures pair up one to one
static bool same_signature_origin(Ty const* source, Ty const* target) {
    if (source->kind == TY_REFERENCE && target->kind == TY_REFERENCE) return source->reference.target == target->reference.target;
    return source->kind == TY_ANONYMOUS && target->kind == TY_ANONYMOUS && source->anonymous.target &&
        source->anonymous.target == target->anonymous.target;
}

// The accessibility of the constructor a construct signature was declared by, -1 if none declared it
static int constructor_access(Signature const* sig) {
    while (sig && !sig->function && sig->target) sig = sig->target;
    FunctionImpl const* function = sig ? sig->function : nullptr;
    return function && function_impl_rare(function)->constructed_class && function->is_constructor ? function_impl_rare(function)->constructor_access : -1;
}

// A signature declared by a class's `constructor` (tsc's declaration.kind === SyntaxKind.Constructor)
static bool declared_constructor(Signature const* sig) {
    while (sig && !sig->function && sig->target) sig = sig->target;
    return sig && sig->function && function_impl_rare(sig->function)->constructed_class && sig->function->is_constructor;
}

// Whether two object types have an ObjectFlags bit in common (tsc's `sourceObjectFlags & targetObjectFlags`), as far as
// the kind of object goes: anonymous types (a class's static side too), classes and interfaces, references
static int object_flags_kind(Ty const* ty) {
    if (ty->kind != TY_DECLARED) return ty->kind;
    switch (ty->declared.kind) {
        case TY_DECLARED_CLASS_STATIC: case TY_DECLARED_ENUM_OBJECT: case TY_DECLARED_NAMESPACE: return TY_ANONYMOUS;
        default: return -1 - (int)ty->declared.kind;
    }
}

static bool share_object_flags(Ty const* source, Ty const* target) {
    return object_flags_kind(source) == object_flags_kind(target);
}

// tsc's signaturesRelatedTo
static Ternary signatures_related(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool construct, bool report
) {
    if (source == my global_types[GLOBAL_ANY_FUNCTION] || target == my global_types[GLOBAL_ANY_FUNCTION]) return TERNARY_TRUE;
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
    if (construct && source_count) {
        // tsc's constructorVisibilitiesAreCompatible: a less visible constructor isn't assignable to a more visible one
        int source_access = constructor_access(source_sigs[0]), target_access = constructor_access(target_sigs[0]);
        if (source_access >= 0 && target_access >= 0 && target_access != ACCESS_MODIFIER_PRIVATE &&
            !(target_access == ACCESS_MODIFIER_PROTECTED && source_access != ACCESS_MODIFIER_PRIVATE) &&
            source_access != ACCESS_MODIFIER_PUBLIC
        ) {
            if (report) {
                static char const* const names[] = { [ACCESS_MODIFIER_PUBLIC] = "public", [ACCESS_MODIFIER_PROTECTED] = "protected",
                    [ACCESS_MODIFIER_PRIVATE] = "private" };
                report_line(st, 2672, cstr_d("Cannot assign a '", names[source_access], "' constructor type to a '",
                    names[target_access], "' constructor type."));
            }
            return TERNARY_FALSE;
        }
    }
    Ternary result = TERNARY_TRUE;
    if (source_count == target_count && same_signature_origin(source, target)) {
        // Pairwise, with type parameters erased: they are the same ones
        for (u32 i = 0; i < target_count; i++) {
            Ternary related = compare_signatures(
                me, st, ty_erased_signature(me, source_sigs[i]), ty_erased_signature(me, target_sigs[i]), CALLBACK_NONE, report,
                signature_return_code(source_sigs[i], target_sigs[i], construct)
            );
            if (!related) return TERNARY_FALSE;
            result &= related;
        }
    } else if (source_count == 1 && target_count == 1) {
        // A single generic source is instantiated in the context of the target instead (only comparability erases)
        bool erase = st->relation == TY_RELATION_COMPARABLE;
        result = compare_signatures(
            me, st, erase ? ty_erased_signature(me, source_sigs[0]) : source_sigs[0],
            erase ? ty_erased_signature(me, target_sigs[0]) : target_sigs[0], CALLBACK_NONE, report,
            signature_return_code(source_sigs[0], target_sigs[0], construct)
        );
        // A class's constructor says which signatures it was about (tsc's signaturesRelatedTo)
        if (!result && report && construct && (declared_constructor(source_sigs[0]) || declared_constructor(target_sigs[0])) &&
            share_object_flags(source, target)
        ) {
            cstr source_str = cstr_null, target_str = cstr_null;
            ty_display_signature_arrow(me, (TyDisplay){ .target = &source_str }, source_sigs[0]);
            ty_display_signature_arrow(me, (TyDisplay){ .target = &target_str }, target_sigs[0]);
            report_line(st, 2322, cstr_d("Type '", source_str, "' is not assignable to type '", target_str, "'."));
            report_line(st, 2419, cstr_lit("Types of construct signatures are incompatible."));
            c_drop(cstr, &source_str, &target_str);
        }
    } else {
        // Every target signature needs a source signature, type parameters erased. Only the first failure explains
        for (u32 i = 0; i < target_count; i++) {
            Signature const* target_sig = ty_erased_signature(me, target_sigs[i]);
            isize_t saved = vec_ty_report_line_size(&st->lines);
            bool elaborate = report;
            Ternary related = TERNARY_FALSE;
            for (u32 j = 0; j < source_count && !related; j++) {
                related = compare_signatures(me, st, ty_erased_signature(me, source_sigs[j]), target_sig, CALLBACK_NONE, elaborate,
                    signature_return_code(source_sigs[j], target_sigs[i], construct));
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
    // (Not one widened for a declaration: tsc's getWidenedTypeOfObjectLiteral drops ObjectFlags.ObjectLiteral, so
    // `const empty = {}` lacks the optional properties of a weak type and isn't its subtype the other way round)
    if (ty_is_unwidened_object_literal(literal)) return false;
    if (is_tuple(source)) return false;
    // `[]` (never[]; tsc's isEmptyArrayLiteralType)
    // (Without strictNullChecks its element is the widening undefined: isEmptyLiteralType)
    if (!ty_is_array(me, source)) return true;
    Ty const* element = ty_reference_args(me, source).data[0];
    return compiler_options()->strict_null_checks ? !(element->flags & TYF_NEVER) : element != ty_intrinsic(me, TY_UNDEFINED_WIDENING);
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
    cstr name = ty_prop_written_name(me, target_prop);
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
// (The signatures of an intersection count, its properties don't: tsc's getSignaturesOfStructuredType and
// getPropertiesOfObjectType)
static bool should_report_unmatched_property(Checker* me, Ty const* source, Ty const* target) {
    if (!(source->flags & (TYF_OBJECT | TYF_INTERSECTION))) return true;
    Members const* members = ty_members(me, source);
    u32 prop_count = source->flags & TYF_OBJECT ? members->prop_count : 0;
    if (!(members->call_count || members->construct_count) || prop_count) return true;
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

// A static `#x` isn't required of a source (tsc's getUnmatchedProperties: isStaticPrivateIdentifierProperty), which is
// how a derived class's static side relates to its base's
static bool is_static_private_name(Checker* me, Prop const* prop) {
    return (prop->flags & PROP_STATIC) && ty_private_name_class(me, prop->name);
}

typedef struct SourceProp {
    Prop const* prop;
    Ty const* type;
    u32 flags;
} SourceProp;

static SourceProp* source_props(Checker* me, Ty const* source, Members const* source_members, u32* count_out);
static bool source_has_prop(Checker* me, Ty const* ty, Atom const* name, u32 depth);

static Ternary props_related_impl(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report, bool source_is_apparent,
    bool optionals_only
);

static Ternary props_related(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report, bool source_is_apparent
) {
    return props_related_impl(me, st, source, target, report, source_is_apparent, false);
}

// `optionals_only`: only the optional properties of the target that the source has (tsc's optionalsOnly)
static Ternary props_related_impl(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report, bool source_is_apparent,
    bool optionals_only
) {
    Members const* target_members = ty_members(me, target);
    bool comparable = st->relation == TY_RELATION_COMPARABLE;

    // A primitive is shown as itself, not as the global interface its properties come from
    if (report && !source_is_apparent && !optionals_only && should_report_unmatched_property(me, source, target)) {
        // Missing properties get a message of their own instead of the generic head
        Prop const* missing[5];
        Prop const* missing_prop = nullptr;
        u32 missing_count = 0;
        for (u32 i = 0; i < target_members->prop_count; i++) {
            Prop const* target_prop = target_members->props[i];
            if ((target_prop->flags & PROP_OPTIONAL) || find_prop(me, source, target_prop->name, 0).found) continue;
            if (is_static_private_name(me, target_prop)) continue;
            if (missing_count < c_arraylen(missing)) missing[missing_count] = target_prop;
            if (!missing_count) missing_prop = target_prop;
            missing_count++;
        }
        // A `#x` of another class that the source class has one of its own of (tsc's reportUnmatchedProperty)
        Symbol const* private_owner = missing_count ? ty_private_name_class(me, missing_prop->name) : nullptr;
        Ty const* source_declared = source->kind == TY_REFERENCE ? source->reference.target : source;
        Symbol const* source_class = source_declared->kind == TY_DECLARED &&
            source_declared->declared.kind == TY_DECLARED_CLASS_INSTANCE ? source_declared->declared.symbol : nullptr;
        if (private_owner && source_class &&
            find_prop(me, source, ty_private_name(me, source_class, atom_sv(missing_prop->name)), 0).found
        ) {
            report_line(st, 2322, cstr_d(
                "Property '", atom_sv(missing_prop->name), "' in type '",
                source_class->name.size ? source_class->name : c_sv("(Anonymous class)"),
                "' refers to a different member that cannot be accessed from within type '",
                private_owner->name.size ? private_owner->name : c_sv("(Anonymous class)"), "'."
            ));
            return TERNARY_FALSE;
        }
        if (missing_count) {
            cstr source_str, target_str;
            display_pair(me, apparent_intersection(me, normalized_reference(me, source)), true,
                normalized_reference(me, target), &source_str, &target_str);
            if (missing_count == 1) {
                cstr name = missing_prop_name(me, missing_prop);
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
                    cstr name = ty_prop_written_name(me, missing[i]);
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
            // (The relation's own line stays under a class's `implements` head: tsc's reportUnmatchedProperty)
            if (st->head_code != 2420 && st->head_code != 2720) st->override_next_head++;
            return TERNARY_FALSE;
        }
    }

    // An object literal that isn't widened yet has no room for other properties, unless they may be undefined (tsc's
    // propertiesRelatedTo for an isObjectLiteralType target): `string` isn't a subtype of the element `{}` of
    // `x.push("s"); x.push({})`, so the evolving array ends up `(string | {})[]`
    if (!optionals_only && ty_is_unwidened_object_literal(target) && (source->flags & (TYF_OBJECT | TYF_INTERSECTION))) {
        u32 prop_count;
        SourceProp* props = source_props(me, source, source->kind == TY_INTERSECTION ? nullptr : ty_members(me, source), &prop_count);
        for (u32 i = 0; i < prop_count; i++) {
            // (An optional property's type has undefined in it under strictNullChecks: tsc's missingType too)
            bool undefined = (props[i].type->flags & TYF_UNDEFINED) ||
                ((props[i].flags & PROP_OPTIONAL) && compiler_options()->strict_null_checks);
            if (undefined || ty_get_prop(me, target, props[i].prop->name)) continue;
            if (report) {
                cstr name = ty_prop_written_name(me, props[i].prop);
                cstr target_str = display_for_report(me, target, false);
                report_line(st, 2339, cstr_d("Property '", name, "' does not exist on type '", target_str, "'."));
                c_drop(cstr, &name, &target_str);
            }
            free(props);
            return TERNARY_FALSE;
        }
        free(props);
    }

    // A property the source lacks fails it before any property's type is asked for (tsc's getUnmatchedProperty at the
    // top of propertiesRelatedTo: `typeof globalThis extends { crypto: infer T; onmessage: any }` in @types/node, the
    // declared type of the global `crypto` itself, is false without typing `crypto`). Only types that aren't known yet
    // could tell: an object's properties are checked when the first of them is about to be resolved, a union's first
    bool object_source = (source->flags & TYF_OBJECT) && source->kind != TY_INTERSECTION && source->kind != TY_UNION;
    bool require_optional = !optionals_only && is_subtype_relation(st->relation) && requires_optional_properties(me, source);
    // (An intersection's properties are made with their types: nothing to resolve)
    bool unmatched_checked = optionals_only || source->kind == TY_INTERSECTION;
    if (!unmatched_checked && !object_source) {
        if (has_unmatched_target_prop(me, source, target_members, 0, require_optional)) return TERNARY_FALSE;
        unmatched_checked = true;
    }

    Ternary result = TERNARY_TRUE;
    Ty const* undefined = ty_intrinsic(me, TY_UNDEFINED);
    for (u32 i = 0; i < target_members->prop_count; i++) {
        Prop const* target_prop = target_members->props[i];
        if (optionals_only && !(target_prop->flags & PROP_OPTIONAL)) continue;
        // (A class's `prototype` is what its construct signatures return: tsc's propertiesRelatedTo skips it)
        if (target_prop->flags & PROP_PROTOTYPE) continue;
        // The same accessor of the same object literal, typed or not (tsc's `sourceProp !== targetProp`: the literal's
        // types share the declaration's symbol). Not typing it matters when the literal is rechecked against what was
        // inferred from it, in the initializer its getter refers back to
        if ((target_prop->flags & (PROP_GETTER | PROP_SETTER)) && target_prop->decl->function && !target_prop->target &&
            (source->flags & TYF_OBJECT)
        ) {
            Prop const* same = ty_get_object_prop(me, source, target_prop->name);
            if (same && same->decl->function == target_prop->decl->function && !same->target && same->flags == target_prop->flags) continue;
        }
        FoundProp source_prop = {0};
        if (object_source) {
            Prop const* prop = ty_get_object_prop(me, source, target_prop->name);
            if (prop && prop->type_state != LAZY_STATE_DONE && !unmatched_checked) {
                unmatched_checked = true;
                if (has_unmatched_target_prop(me, source, target_members, i + 1, require_optional)) {
                    result = TERNARY_FALSE;
                    goto done;
                }
            }
            // (Its type is asked for after the target property's: tsc's isPropertySymbolTypeRelated)
            if (prop) source_prop = (FoundProp){ .found = true, .flags = prop->flags, .prop = prop };
        } else {
            source_prop = find_prop(me, source, target_prop->name, 0);
        }
        if (optionals_only && !source_prop.found) continue;
        // The same property (tsc's `sourceProp !== targetProp`): a member a derived class inherits against its base.
        // (Not an intersection's property combined from several members: tsc's is a synthetic symbol, `IC & V`'s
        // `reference` against IC's own)
        if (source_prop.found && source_prop.prop == target_prop &&
            (source->kind != TY_INTERSECTION || !source_prop.type || source_prop.type == prop_type(me, target_prop))
        ) {
            continue;
        }
        if (!source_prop.found) {
            if (is_static_private_name(me, target_prop)) continue;
            // Subtypes need the optional properties too, unless an object literal, [] or a tuple leaves them out
            // (tsc's requireOptionalProperties)
            if ((target_prop->flags & PROP_OPTIONAL) && (!is_subtype_relation(st->relation) || !requires_optional_properties(me, source))) {
                continue;
            }
            result = TERNARY_FALSE;
            goto done;
        }
        if (!modifiers_related(me, st, source, target, &source_prop, target_prop, report)) { result = TERNARY_FALSE; goto done; }
        // `{ readonly a: T }` isn't a strict subtype of `{ a: T }` (the other way around it is), which orders subtype
        // reduction and narrowing by a type predicate (tsc's propertiesRelatedTo, strictSubtypeRelation only)
        if (st->relation == TY_RELATION_STRICT_SUBTYPE && !is_readonly_prop(target_prop) &&
            (source_prop.prop ? is_readonly_prop(source_prop.prop) : (source_prop.flags & PROP_READONLY))
        ) {
            result = TERNARY_FALSE;
            goto done;
        }
        Ty const* target_type = prop_type(me, target_prop);
        Ty const* source_type = source_prop.type ? source_prop.type : prop_type(me, source_prop.prop);
        if ((target_prop->flags & PROP_OPTIONAL) && optional_adds_undefined()) {
            Ty const* parts[2] = { target_type, undefined };
            target_type = ty_union(me, parts, 2, nullptr, (TyList){0});
        }
        // (An optional source is only related to a required target when comparing: it may be undefined too)
        if ((source_prop.flags & PROP_OPTIONAL) && optional_adds_undefined()) {
            Ty const* source_parts[2] = { source_type, undefined };
            source_type = ty_union(me, source_parts, 2, nullptr, (TyList){0});
        }
        Ternary related = is_related_to(me, st, source_type, target_type, report);
        if (!related) {
            if (report) {
                cstr name = ty_prop_written_name(me, target_prop);
                report_incompatible(st, 2326, cstr_d("Types of property '", name, "' are incompatible."));
                cstr_drop(&name);
            }
            result = TERNARY_FALSE;
            goto done;
        }
        // (the types first: their failure explains better)
        if ((source_prop.flags & PROP_OPTIONAL) && !(target_prop->flags & PROP_OPTIONAL) && !comparable) {
            if (report) {
                cstr source_str = display_for_report(me, source, true);
                cstr target_str = display_for_report(me, target, false);
                cstr name = ty_prop_written_name(me, target_prop);
                report_line(st, 2327, cstr_d(
                    "Property '", name, "' is optional in type '", source_str, "' but required in type '", target_str, "'."
                ));
                c_drop(cstr, &source_str, &target_str, &name);
            }
            result = TERNARY_FALSE;
            goto done;
        }
        result &= related;
    }
done:
    return result;
}

static bool index_key_applies(IndexInfo const* source, IndexInfo const* target) {
    if (source->key == target->key) return true;
    // A string index signature covers numeric keys as well
    return (source->key->flags & TYF_STRING) && (target->key->flags & TYF_NUMBER);
}

// tsc's isObjectTypeWithInferableIndex: object literal and type literal types, namespaces and enums, but nothing callable
static bool has_inferable_index(Checker* me, Ty const* ty) {
    if (ty->kind == TY_INTERSECTION) {
        bool empty_seen = false;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            Ty const* member = ty->composite.types.data[i];
            // (`object` is `{}` in the intersection's apparent type, which drops it: tsc's getApparentTypeOfIntersectionType)
            if (member->flags & TYF_NON_PRIMITIVE) continue;
            // A type variable is its base constraint there, or unknown, which is nothing with strictNullChecks and
            // tsc's emptyObjectType without: the first empty object type of an intersection is the one it keeps, so
            // NonNullable's `T[K] & {}` has none then
            if (member->kind == TY_TYPE_PARAM || member->kind == TY_SUBSTITUTION || (member->flags & TYF_DEFERRED)) {
                Ty const* constraint = base_constraint(me, member, 0);
                if (!constraint || constraint == member || (constraint->flags & TYF_UNKNOWN)) {
                    if (compiler_options()->strict_null_checks || empty_seen) continue;
                    return false;
                }
                member = apparent_type(me, constraint);
            }
            empty_seen |= member->kind == TY_ANONYMOUS && ty_is_empty_anonymous_object(me, member);
            if (!has_inferable_index(me, member)) return false;
        }
        return true;
    }
    // (Not tsc's emptyObjectType, which has no symbol: the apparent type of `object`)
    if (ty == global_type(me, GLOBAL_EMPTY_OBJECT)) return false;
    if (ty->kind == TY_ANONYMOUS) {
        Ty const* origin = ty->anonymous.target && ty->anonymous.target->kind == TY_ANONYMOUS ? ty->anonymous.target : ty;
        if (ty->anonymous.no_inferable_index || origin->anonymous.no_inferable_index) return false;
        // (A reverse mapped type has one if what it was inferred from has: `T` from an interface has none)
        if (ty->anonymous.is_reverse) return has_inferable_index(me, ty->anonymous.reverse->source);
        Members const* members = ty_members(me, ty);
        return !members->call_count && !members->construct_count;
    }
    return ty->kind == TY_DECLARED &&
        (ty->declared.kind == TY_DECLARED_NAMESPACE || ty->declared.kind == TY_DECLARED_ENUM_OBJECT);
}

// The properties of an object type, or of every member of an intersection (tsc's getPropertiesOfUnionOrIntersectionType:
// the first of each name, with the type and flags of the intersection's). Free the result
static SourceProp* source_props(Checker* me, Ty const* source, Members const* source_members, u32* count_out) {
    u32 count = 0, cap = 8;
    SourceProp* ret = malloc(sizeof(SourceProp) * cap);
    panic_if(!ret, "out of memory");
    TyList parts = source->kind == TY_INTERSECTION ? source->composite.types : (TyList){ .data = &source, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        Members const* members = source->kind == TY_INTERSECTION ? ty_members(me, parts.data[i]) : source_members;
        for (u32 j = 0; j < members->prop_count; j++) {
            Prop const* prop = members->props[j];
            SourceProp entry = { .prop = prop, .type = nullptr, .flags = prop->flags };
            if (source->kind == TY_INTERSECTION) {
                bool seen = false;
                for (u32 k = 0; k < count && !seen; k++) seen = ret[k].prop->name == prop->name;
                if (seen) continue;
                FoundProp found = ty_find_prop(me, source, prop->name);
                if (!found.found) continue;
                entry.type = found.type;
                entry.flags = found.flags;
            } else {
                entry.type = prop_type(me, prop);
            }
            if (count == cap) {
                cap *= 2;
                ret = realloc(ret, sizeof(SourceProp) * cap);
                panic_if(!ret, "out of memory");
            }
            ret[count++] = entry;
        }
    }
    *count_out = count;
    return ret;
}

// `source_is_primitive`: `source` is a primitive's apparent type, which a string `any` index signature doesn't take as is
// tsc's indexInfoRelatedTo: both keys when the source's signature is another key's
[[gnu::noinline, gnu::cold]] static void report_index_incompatible(Checker* me, RelationState* st, IndexInfo const* source_info, IndexInfo const* target_info) {
    cstr key_str = display_for_report(me, source_info->key, false);
    if (source_info->key == target_info->key) {
        report_line(st, 2634, cstr_d("'", key_str, "' index signatures are incompatible."));
    } else {
        cstr target_key_str = display_for_report(me, target_info->key, false);
        report_line(st, 2330, cstr_d("'", key_str, "' and '", target_key_str, "' index signatures are incompatible."));
        cstr_drop(&target_key_str);
    }
    cstr_drop(&key_str);
}

// The index signatures of a source with an inferable index that apply to a target's key (tsc's
// membersRelatedToIndexInfo). Rare, and out of line: relate.c is at GCC's inline-unit-growth limit, where any code
// added to a hot function costs some other inlining (props_related_impl's find_prop, +0.4% instructions on vue)
[[gnu::noinline, gnu::cold]] static Ternary source_indexes_related(
    Checker* me, RelationState* st, Members const* source_members, IndexInfo const* target_info, bool report
) {
    Ternary result = TERNARY_TRUE;
    for (u32 j = 0; j < source_members->index_count; j++) {
        IndexInfo const* info = &source_members->index[j];
        bool applies = info->key == target_info->key ||
            (target_info->key == ty_intrinsic(me, TY_STRING) && (info->key->flags & TYF_NUMBER)) ||
            ty_is_related(me, info->key, target_info->key, TY_RELATION_ASSIGNABLE);
        if (!applies) continue;
        Ternary related = is_related_to(me, st, info->value, target_info->value, report);
        if (!related) {
            if (report) report_index_incompatible(me, st, info, target_info);
            return TERNARY_FALSE;
        }
        result &= related;
    }
    return result;
}

static TyMapper const* mapped_key_param_mapper(Checker* me, MappedInfo const* mapped);
static Ternary index_signatures_related(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool source_is_primitive, bool report
) {
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
        if (target_has_string_index && (target_info->value->flags & TYF_ANY) && !source_is_primitive &&
            st->relation != TY_RELATION_STRICT_SUBTYPE
        ) {
            continue;
        }
        // A generic mapped type's template stands for all of its values (tsc's indexSignaturesRelatedTo), its key type
        // parameter constrained by the instantiated constraint (tsc's fresh type parameter of an instantiated mapped type)
        if (source_members->mapped && target_has_string_index) {
            MappedInfo const* mapped = source_members->mapped;
            Ty const* template = ty_mapped_template_fresh(me, mapped);
            // (With the mapped type's `?`, its undefined: tsc's getTemplateTypeFromMappedType, see expand_mapped())
            if (mapped->optional > 0 && compiler_options()->strict_null_checks) {
                Ty const* parts[2] = { template, ty_intrinsic(me, TY_UNDEFINED) };
                template = ty_union(me, parts, 2, nullptr, (TyList){0});
            }
            Ternary related = is_related_to(me, st, template, target_info->value, report);
            if (!related) return TERNARY_FALSE;
            result &= related;
            continue;
        }
        // The source's signature for the same key, else one that applies (a string index covers numbers)
        IndexInfo const* source_info = nullptr;
        for (u32 j = 0; j < source_members->index_count && !source_info; j++) {
            if (source_members->index[j].key == target_info->key) source_info = &source_members->index[j];
        }
        for (u32 j = 0; j < source_members->index_count && !source_info; j++) {
            if (index_key_applies(&source_members->index[j], target_info)) source_info = &source_members->index[j];
        }
        // (tsc's getApplicableIndexInfo(source, targetInfo.keyType): `string & A & B` takes `string & A`'s, `a${string}a`
        // both `a${string}`'s and `${string}a`'s)
        if (!source_info && source_members->index_count && (target_info->key->kind == TY_INTERSECTION ||
            (target_info->key->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)))
        ) {
            source_info = ty_applicable_index_info(me, source, target_info->key);
        }
        if (source_info) {
            Ternary related = is_related_to(me, st, source_info->value, target_info->value, report);
            if (!related) {
                if (report) report_index_incompatible(me, st, source_info, target_info);
                return TERNARY_FALSE;
            }
            result &= related;
        } else if (!st->member_of_intersection_source && has_inferable_index(me, source) && (st->relation != TY_RELATION_STRICT_SUBTYPE || ty_is_fresh_object(source))) {
            // An object literal type is related if each of its properties the signature applies to is. Not in the strict
            // subtype relation unless it's fresh: `{ [k: string]: X }` is a strict subtype of `{}`, not vice versa. Of an
            // intersection, every property of every member, with the type they have together
            u32 prop_count;
            SourceProp* props = source_props(me, source, source_members, &prop_count);
            for (u32 j = 0; j < prop_count; j++) {
                Prop const* prop = props[j].prop;
                bool symbol_named = ty_unique_symbol_of_name(me, prop->name) != nullptr;
                if ((target_info->key->flags & TYF_NUMBER) && !is_numeric_prop_name(prop->name)) continue;
                if (((target_info->key->flags & TYF_ES_SYMBOL) != 0) != symbol_named) continue;
                // A pattern (`do-${string}`) applies to the names it matches (tsc's isApplicableIndexType)
                if ((target_info->key->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING) || target_info->key->kind == TY_INTERSECTION) &&
                    !ty_is_related(me, ty_prop_name_literal(me, prop->name), target_info->key, TY_RELATION_ASSIGNABLE)
                ) {
                    continue;
                }
                // (tsc's isIgnoredJsxProperty)
                if (source->kind == TY_ANONYMOUS && source->anonymous.jsx_attributes && csview_contains(atom_sv(prop->name), "-")) continue;
                // An optional property's undefined doesn't count (tsc's membersRelatedToIndexer)
                Ty const* type = props[j].type;
                if ((props[j].flags & PROP_OPTIONAL) && !(target_info->key->flags & TYF_NUMBER) && !(type->flags & TYF_UNDEFINED) &&
                    !compiler_options()->exact_optional_property_types
                ) {
                    type = ty_without_undefined(me, type);
                }
                // (For a numeric index signature it does: tsc relates getNonMissingTypeOfSymbol as is)
                if ((props[j].flags & PROP_OPTIONAL) && (target_info->key->flags & TYF_NUMBER) &&
                    compiler_options()->strict_null_checks && !compiler_options()->exact_optional_property_types
                ) {
                    type = ty_union(me, (Ty const*[]){ type, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
                }
                Ternary related = is_related_to(me, st, type, target_info->value, report);
                if (!related) {
                    if (report) {
                        cstr name = ty_prop_written_name(me, prop);
                        report_line(st, 2530, cstr_d("Property '", name, "' is incompatible with index signature."));
                        cstr_drop(&name);
                    }
                    free(props);
                    return TERNARY_FALSE;
                }
                result &= related;
            }
            free(props);
            // And the source's own index signatures that apply to the key (tsc's membersRelatedToIndexInfo:
            // `{ [x: number]: X }` against a string index signature)
            if (source_members->index_count) {
                Ternary related = source_indexes_related(me, st, source_members, target_info, report);
                if (!related) return TERNARY_FALSE;
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

// A rest element (`...X[]`, tsc's ElementFlags.Rest) rather than a variadic one (`...T`). HACK: anything but a type
// parameter counts: tsc normalizes `[...X]` for an X that isn't a type variable when it makes the tuple (a mapped type
// over an array, `{ [S in K]: [a: number] }[K]` simplified), tttt keeps X, whose elements are X[number]
static bool is_rest_array(Checker* me, Ty const* rest) {
    (void)me;
    return rest->kind != TY_TYPE_PARAM;
}

// A class or interface whose length is number literals (one extending a tuple): an array is no match for it, the
// tuple itself is compared with it member by member
static bool is_numeric_prop_name(Atom const* name);
static bool has_numeric_props(Checker* me, Ty const* ty) {
    Members const* members = ty_members(me, ty);
    for (u32 i = 0; i < members->prop_count; i++) {
        if (is_numeric_prop_name(members->props[i]->name)) return true;
    }
    return false;
}

static bool has_literal_length(Checker* me, Ty const* ty) {
    Ty const* length = ty_prop_type_of(me, ty, checker_atom(me, c_sv("length")));
    return length && !(length->flags & TYF_NUMBER) && !(length->flags & TYF_ANY) && (length->flags & TYF_NUMBER_LITERAL ||
        (length->kind == TY_UNION && (length->composite.types.data[0]->flags & TYF_NUMBER_LITERAL)));
}

static Ternary tuple_shapes_related(
    Checker* me, RelationState* st, TupleShape const* source_shape, TyList source_args, Ty const* target, bool report
);
static Ternary tuples_related(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    TupleShape const* source_shape = is_empty_tuple(source) ? source->declared.tuple : source->reference.target->declared.tuple;
    TyList source_args = is_empty_tuple(source) ? (TyList){0} : ty_reference_args(me, source);
    return tuple_shapes_related(me, st, source_shape, source_args, target, report);
}

// (An array source is one rest element: tsc's propertiesRelatedTo for isArrayOrTupleType(source))
static Ternary tuple_shapes_related(
    Checker* me, RelationState* st, TupleShape const* source_shape, TyList source_args, Ty const* target, bool report
) {
    TupleShape const* target_shape = is_empty_tuple(target) ? target->declared.tuple : target->reference.target->declared.tuple;
    // (TS4104 is report_relation_error()'s, tsc's propertiesRelatedTo says nothing)
    if (source_shape->readonly && !target_shape->readonly && st->relation != TY_RELATION_COMPARABLE) return TERNARY_FALSE;
    TyList target_args = is_empty_tuple(target) ? (TyList){0} : ty_reference_args(me, target);
    u32 source_arity = source_shape->arity, target_arity = target_shape->arity;
    // (A rest element of an array type is tsc's Rest, of anything else (a type variable) its Variadic)
    u32 source_min = 0, target_min = 0;
    bool source_rest = false, target_variable = false;
    for (u32 i = 0; i < source_arity; i++) {
        if (!(source_shape->flags[i] & TUPLE_ELEMENT_OPTIONAL)) source_min += !(source_shape->flags[i] & TUPLE_ELEMENT_REST) || !is_rest_array(me, source_args.data[i]);
        source_rest |= (source_shape->flags[i] & TUPLE_ELEMENT_REST) && is_rest_array(me, source_args.data[i]);
    }
    for (u32 i = 0; i < target_arity; i++) {
        if (!(target_shape->flags[i] & TUPLE_ELEMENT_OPTIONAL)) target_min += !(target_shape->flags[i] & TUPLE_ELEMENT_REST) || !is_rest_array(me, target_args.data[i]);
        target_variable |= (target_shape->flags[i] & TUPLE_ELEMENT_REST) != 0;
    }
    if (!source_rest && source_arity < target_min) {
        if (report) report_line(st, 2618, cstr_d("Source has ", source_arity, " element(s) but target requires ", target_min, "."));
        return TERNARY_FALSE;
    }
    if (!target_variable && target_arity < source_min) {
        if (report) report_line(st, 2619, cstr_d("Source has ", source_min, " element(s) but target allows only ", target_arity, "."));
        return TERNARY_FALSE;
    }
    if (!target_variable && (source_rest || target_arity < source_arity)) {
        if (report && source_min < target_min) {
            report_line(st, 2620, cstr_d("Target requires ", target_min, " element(s) but source may have fewer."));
        } else if (report) {
            report_line(st, 2621, cstr_d("Target allows only ", target_arity, " element(s) but source may have more."));
        }
        return TERNARY_FALSE;
    }
    // The target's elements before its first rest one and after its last (tsc's getStartElementCount/getEndElementCount)
    // (ElementFlags.NonRest: a variadic element counts, only an array rest one ends them)
    u32 target_start = 0, target_end = 0;
    while (target_start < target_arity && !((target_shape->flags[target_start] & TUPLE_ELEMENT_REST) &&
        is_rest_array(me, target_args.data[target_start]))) {
        target_start++;
    }
    while (target_end < target_arity && !((target_shape->flags[target_arity - 1 - target_end] & TUPLE_ELEMENT_REST) &&
        is_rest_array(me, target_args.data[target_arity - 1 - target_end]))) {
        target_end++;
    }
    Ternary result = TERNARY_TRUE;
    for (u32 i = 0; i < source_arity; i++) {
        u32 from_end = source_arity - 1 - i;
        u32 target_index = target_variable && i >= target_start ? target_arity - 1 - (from_end < target_end ? from_end : target_end) : i;
        unsigned char source_flags = source_shape->flags[i], target_flags = target_shape->flags[target_index];
        bool source_variadic = (source_flags & TUPLE_ELEMENT_REST) && !is_rest_array(me, source_args.data[i]);
        bool target_variadic = (target_flags & TUPLE_ELEMENT_REST) && !is_rest_array(me, target_args.data[target_index]);
        bool source_required = !(source_flags & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST));
        bool target_required = !(target_flags & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST));
        if (target_variadic && !source_variadic) {
            if (report) report_line(st, 2624, cstr_d("Source provides no match for variadic element at position ", target_index, " in target."));
            return TERNARY_FALSE;
        }
        if (source_variadic && !(target_flags & TUPLE_ELEMENT_REST)) {
            if (report) {
                report_line(st, 2625, cstr_d("Variadic element at position ", i, " in source does not match element at position ",
                    target_index, " in target."));
            }
            return TERNARY_FALSE;
        }
        if (target_required && !source_required) {
            if (report) report_line(st, 2623, cstr_d("Source provides no match for required element at position ", target_index, " in target."));
            return TERNARY_FALSE;
        }
        // A variadic source element against a rest one relates as a whole to the rest's array type
        // (tsc's removeMissingType: an optional element's missing only matches an optional one)
        Ty const* source_type = ty_remove_missing(me, source_args.data[i], source_flags & target_flags & TUPLE_ELEMENT_OPTIONAL);
        if ((source_flags & TUPLE_ELEMENT_REST) && !source_variadic) source_type = rest_element_type(me, source_type);
        Ty const* target_type = ty_remove_missing(me, target_args.data[target_index], target_flags & TUPLE_ELEMENT_OPTIONAL);
        if ((target_flags & TUPLE_ELEMENT_REST) && !target_variadic && !source_variadic) target_type = rest_element_type(me, target_type);
        Ternary related = is_related_to(me, st, source_type, target_type, report);
        if (!related) {
            if (report && (target_arity > 1 || source_arity > 1)) {
                if (target_variable && i >= target_start && from_end >= target_end && target_start != source_arity - target_end - 1) {
                    report_incompatible(st, 2627, cstr_d("Type at positions ", target_start, " through ", source_arity - target_end - 1,
                        " in source is not compatible with type at position ", target_index, " in target."));
                } else {
                    report_incompatible(st, 2626, cstr_d("Type at position ", i, " in source is not compatible with type at position ", target_index, " in target."));
                }
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

// Measuring variances through a relation that doesn't go by what the type parameter is instantiated with: the
// measurement is unreliable (VARIANCE_UNRELIABLE) or unmeasurable (VARIANCE_UNMEASURABLE) if the type mentions a marker
// (tsc's instantiateType(source, reportUnreliableMapper))
static void report_variance(Checker* me, Ty const* ty, unsigned flag) {
    if (!my variance_depth || (my variance_reports & flag)) return;
    Ty const* markers[3] = { variance_marker(me, 0), variance_marker(me, 1), variance_marker(me, 2) };
    if (ty_mentions_any(me, ty, markers, 3)) my variance_reports |= (unsigned char)flag;
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

// What variances are measured for: the type parameters of a generic declared type, or of a generic type alias
typedef struct VarianceSubject {
    Ty const* declared;
    Symbol const* alias;
    TyList params;
} VarianceSubject;

// The subject instantiated with its own type parameters, except the one at `index` replaced by `marker`
static Ty const* marker_instance(Checker* me, VarianceSubject subject, u32 index, Ty const* marker) {
    if (subject.declared) return marker_reference(me, subject.declared, index, marker);
    Ty const** args = malloc(sizeof(Ty const*) * subject.params.len);
    for (u32 i = 0; i < subject.params.len; i++) args[i] = i == index ? marker : subject.params.data[i];
    Ty const* ret = ty_alias_instance(me, subject.alias, (TyList){ .data = args, .len = subject.params.len });
    free(args);
    u64 key[2] = { KEY_ALIAS_MARKER, ret->id };
    *intern_slot(me, &my links, key, 2) = (void*)ret;
    return ret;
}

Ty const* ty_variance_check_instance(Checker* me, Symbol const* owner, u32 index, bool sub) {
    Ty const* declared = owner->flags & (SYMBOL_CLASS | SYMBOL_INTERFACE) ? ty_declared_type_of(me, owner) : nullptr;
    VarianceSubject subject = declared && declared->kind == TY_DECLARED ?
        (VarianceSubject){ .declared = declared, .params = declared->declared.type_params } :
        (VarianceSubject){ .alias = owner, .params = ty_alias_type_params(me, owner) };
    if (subject.declared) index += declared->declared.outer_count;
    if (index >= subject.params.len) return nullptr;
    Atom const* name = subject.params.data[index]->param.name;
    Ty const* markers[2];
    for (u32 which = 0; which < 2; which++) {
        u64 key[3] = { KEY_CHECK_MARKER, (u64)(uintptr_t)name, which };
        markers[which] = intern_get(&my links, key, 3);
        if (markers[which]) continue;
        cstr marker_name = cstr_d(which ? "sub-" : "super-", atom_sv(name));
        markers[which] = ty_synthetic_type_param(me, cstr_sv(&marker_name));
        cstr_drop(&marker_name);
        markers[which]->param.state->is_check_marker = true;
        if (which == 1) {
            markers[which]->param.state->constraint = markers[0];
            markers[which]->param.state->constraint_state = LAZY_STATE_DONE;
        }
        *intern_slot(me, &my links, key, 3) = (void*)markers[which];
    }
    Ty const* ret = marker_instance(me, subject, index, markers[sub]);
    u64 key[2] = { KEY_CHECK_MARKER_REF, ret->id };
    *intern_slot(me, &my links, key, 2) = (void*)ret;
    return ret;
}

// A reference made by ty_variance_check_instance(): related structurally, not by variances (tsc's isMarkerType)
static bool is_check_marker(Checker* me, Ty const* ty) {
    u64 key[2] = { KEY_CHECK_MARKER_REF, ty->id };
    return intern_get(&my links, key, 2) != nullptr;
}

static bool is_alias_marker(Checker* me, Ty const* ty) {
    u64 key[2] = { KEY_ALIAS_MARKER, ty->id };
    return intern_get(&my links, key, 2) != nullptr;
}

unsigned ty_variance_modifiers(Symbol const* owner, u32 index) {
    unsigned ret = 0;
    for (u32 d = 0; owner && d < owner->decl_count; d++) {
        Generic const* generic;
        switch (owner->decls[d].kind) {
            case SYMBOL_DECL_CLASS: generic = &((ClassDecl const*)owner->decls[d].node)->generic; break;
            case SYMBOL_DECL_INTERFACE: generic = &((InterfaceDecl const*)owner->decls[d].node)->generic; break;
            case SYMBOL_DECL_TYPE_ALIAS: generic = &((TypeDecl const*)owner->decls[d].node)->generic; break;
            default: continue;
        }
        if (index >= (u32)vec_generic_item_size(&generic->items)) continue;
        GenericItem const* item = vec_generic_item_at(&generic->items, index);
        ret |= (item->is_in ? VARIANCE_MODIFIER_IN : 0) | (item->is_out ? VARIANCE_MODIFIER_OUT : 0);
    }
    return ret;
}

// The variance annotations of the subject's type parameter at `index`
static unsigned subject_variance_modifiers(VarianceSubject subject, u32 index) {
    Symbol const* owner = subject.declared ? subject.declared->declared.symbol : subject.alias;
    u32 outer = subject.declared ? subject.declared->declared.outer_count : 0;
    if (owner && index >= outer) return ty_variance_modifiers(owner, index - outer);
    Ty const* param = subject.params.data[index];
    GenericItem const* item = param->kind == TY_TYPE_PARAM && param->param.state ? param->param.state->item : nullptr;
    return item ? (item->is_in ? VARIANCE_MODIFIER_IN : 0) | (item->is_out ? VARIANCE_MODIFIER_OUT : 0) : 0;
}

static void measure_variances(Checker* me, VarianceSubject subject, Variances* variances) {
    unsigned char saved_reports = my variance_reports;
    my variance_depth++;
    for (u32 i = 0; i < variances->len; i++) {
        // Variance annotations (`in T`, `out T`, `in out T`) are taken as they are (tsc's getVariancesWorker), those of
        // every declaration of a merged interface together
        unsigned modifiers = subject_variance_modifiers(subject, i);
        if (modifiers) {
            variances->data[i] = (unsigned char)(modifiers == (VARIANCE_MODIFIER_IN | VARIANCE_MODIFIER_OUT) ?
                VARIANCE_INVARIANT : modifiers == VARIANCE_MODIFIER_OUT ? VARIANCE_COVARIANT : VARIANCE_CONTRAVARIANT);
            continue;
        }
        my variance_reports = 0;
        Ty const* with_super = marker_instance(me, subject, i, variance_marker(me, 0));
        Ty const* with_sub = marker_instance(me, subject, i, variance_marker(me, 1));
        unsigned variance =
            (ty_is_related(me, with_sub, with_super, TY_RELATION_ASSIGNABLE) ? VARIANCE_COVARIANT : 0) |
            (ty_is_related(me, with_super, with_sub, TY_RELATION_ASSIGNABLE) ? VARIANCE_CONTRAVARIANT : 0);
        if (variance == VARIANCE_BIVARIANT) {
            Ty const* with_other = marker_instance(me, subject, i, variance_marker(me, 2));
            if (ty_is_related(me, with_other, with_super, TY_RELATION_ASSIGNABLE)) variance = VARIANCE_INDEPENDENT;
        }
        variance |= my variance_reports;
        variances->data[i] = (unsigned char)variance;
    }
    my variance_depth--;
    my variance_reports = saved_reports;
    variances->state = LAZY_STATE_DONE;
    if (debug_env.dump_variances) {
        cstr name = subject.declared ? ty_to_cstr(me, subject.declared) : cstr_from_sv(subject.alias->name);
        for (u32 i = 0; i < variances->len; i++) log_info("variance of ", name, " #", i, ": ", (u32)variances->data[i]);
        cstr_drop(&name);
    }
}

static Variances* new_variances(Checker* me, u64 const* key, u32 len) {
    Variances* variances = arena_alloc(&my arena, sizeof(Variances) + len);
    variances->len = len;
    variances->state = LAZY_STATE_IN_FLIGHT;
    *intern_slot(me, &my links, key, 2) = variances;
    return variances;
}

// Null while the variances are being measured (the caller then goes by Ternary Unknown, like tsc)
static Variances const* get_variances(Checker* me, Ty const* target) {
    u64 key[2] = { KEY_VARIANCES, target->id };
    Variances* variances = (Variances*)intern_get(&my links, key, 2);
    if (variances) return variances->state == LAZY_STATE_DONE ? variances : nullptr;

    variances = new_variances(me, key, target->declared.type_params.len);
    if (is_array_target(me, target) || target->declared.kind == TY_DECLARED_TUPLE_TARGET ||
        target->declared.kind == TY_DECLARED_SYNTHETIC
    ) {
        memset(variances->data, VARIANCE_COVARIANT, variances->len);
        variances->state = LAZY_STATE_DONE;
        return variances;
    }
    measure_variances(me, (VarianceSubject){ .declared = target, .params = target->declared.type_params }, variances);
    return variances;
}

// The variances of a generic type alias's type parameters (tsc's getAliasVariances), null while being measured
static Variances const* get_alias_variances(Checker* me, Symbol const* alias) {
    u64 key[2] = { KEY_ALIAS_VARIANCES, (u64)(uintptr_t)alias };
    Variances* variances = (Variances*)intern_get(&my links, key, 2);
    if (variances) return variances->state == LAZY_STATE_DONE ? variances : nullptr;

    TyList params = ty_alias_type_params(me, alias);
    variances = new_variances(me, key, params.len);
    measure_variances(me, (VarianceSubject){ .alias = alias, .params = params }, variances);
    return variances;
}

bool ty_alias_param_is_contravariant(Checker* me, Symbol const* alias, u32 index) {
    Variances const* variances = get_alias_variances(me, alias);
    if (!variances || index >= variances->len) return false;
    return (variances->data[index] & (VARIANCE_BIVARIANT | VARIANCE_INDEPENDENT)) == VARIANCE_CONTRAVARIANT;
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
    for (u32 i = 0; i < variances->len; i++) {
        if (variances->data[i] & (VARIANCE_UNRELIABLE | VARIANCE_UNMEASURABLE)) *unreliable = true;
    }
    for (u32 i = 0; i < source_args.len && i < target_args.len; i++) {
        unsigned variance = i < variances->len ? variances->data[i] : VARIANCE_COVARIANT;
        unsigned kind = variance & (VARIANCE_BIVARIANT | VARIANCE_INDEPENDENT);
        if (kind == VARIANCE_INDEPENDENT) continue;
        Ty const* s = source_args.data[i];
        Ty const* t = target_args.data[i];
        Ternary related;
        if (variance & VARIANCE_UNMEASURABLE) {
            // Even so identical arguments make identical instantiations
            related = st->relation == TY_RELATION_IDENTITY ? is_related_to(me, st, s, t, false) :
                ty_is_related(me, s, t, TY_RELATION_IDENTITY) ? TERNARY_TRUE : TERNARY_FALSE;
        } else if (kind == VARIANCE_COVARIANT) {
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

// A covariant `void` type argument of the target (`B<number>` to `B<void>`): a failed comparison by variances falls
// back to comparing structurally, where a `void` return takes anything (tsc's hasCovariantVoidArgument). Unlike an
// unreliable variance, the variance error stays, and the structural comparison doesn't report (tsc's
// reportStructuralErrors: only if nothing was reported yet)
static bool covariant_void_argument(Variances const* variances, TyList target_args) {
    for (u32 i = 0; i < target_args.len && i < variances->len; i++) {
        if ((variances->data[i] & (VARIANCE_BIVARIANT | VARIANCE_INDEPENDENT)) == VARIANCE_COVARIANT &&
            (target_args.data[i]->flags & TYF_VOID)
        ) {
            return true;
        }
    }
    return false;
}

// Structured relation

// A relation that pushes only one side on the deep nesting stacks: `skip` 1 leaves the source (a member of a union or
// intersection target against the same source, tsc's RecursionFlags.Target), 2 the target (RecursionFlags.Source)
static Ternary is_related_to_side(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report, unsigned skip) {
    unsigned saved = st->skip_push;
    st->skip_push = skip;
    Ternary ret = is_related_to(me, st, source, target, report);
    st->skip_push = saved;
    return ret;
}

static Ternary each_related(Checker* me, RelationState* st, TyList sources, Ty const* target, bool report) {
    Ternary result = TERNARY_TRUE;
    TyList fresh = {0};
    if (report && st->fresh_union && ty_regular(st->checker, st->fresh_union)->composite.types.data == sources.data &&
        st->fresh_union->composite.types.len == sources.len
    ) {
        fresh = st->fresh_union->composite.types;
    }
    for (u32 i = 0; i < sources.len; i++) {
        Ty const* member = fresh.len ? ty_regular(me, fresh.data[i]) : sources.data[i];
        Ternary related = is_related_to_side(me, st, member, target, report, 2);
        if (!related) return TERNARY_FALSE;
        result &= related;
    }
    return result;
}

// Whether `t` is a member of the union `u`, whose members are sorted by id (tsc's containsType)
static bool union_has_member(Ty const* u, Ty const* t) {
    TyList members = u->composite.types;
    u32 lo = 0, hi = members.len;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (members.data[mid] == t) return true;
        if (members.data[mid]->id < t->id) lo = mid + 1; else hi = mid;
    }
    return false;
}

// (Explained by the last member, `report`: tsc's someTypeRelatedToType)
static Ternary some_related(Checker* me, RelationState* st, TyList sources, Ty const* target, bool report) {
    for (u32 i = 0; i < sources.len; i++) {
        Ternary related = is_related_to_side(me, st, sources.data[i], target, report && i == sources.len - 1, 2);
        if (related) return related;
    }
    return TERNARY_FALSE;
}

static Ty const* best_matching_type(Checker* me, RelationState* st, Ty const* source, Ty const* target);
static MappedInfo const* mapped_info_of(Checker* me, Ty const* ty, bool* generic);

static Ternary related_to_some(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    TyList targets = target->composite.types;
    if (union_has_member(target, source)) return TERNARY_TRUE;
    // The member with the source's key first (tsc's getMatchingUnionConstituentForType)
    Atom const* key = source->flags & (TYF_OBJECT | TYF_INTERSECTION) ? ty_union_key_property(me, target) : nullptr;
    Ty const* key_type = key ? ty_prop_type_of(me, source, key) : nullptr;
    Ty const* match = key_type ? ty_union_member_for_key(me, target, key_type) : nullptr;
    if (match) {
        Ternary related = is_related_to_side(me, st, source, match, false, 1);
        if (related) return related;
    }
    // A unit type is only ever related to a union member by identity (above) or through a non-unit member
    bool source_is_unit = (source->kind == TY_LITERAL && !source->literal.enum_decl) ||
        (source->kind == TY_INTRINSIC && (source->flags & TYF_NULLABLE));
    // Nor is an enum literal to another member of its enum (members of one value share their type)
    Symbol const* source_enum = source->kind == TY_LITERAL && (source->flags & TYF_ENUM_LITERAL) ?
        source->literal.enum_decl : nullptr;
    for (u32 i = 0; i < targets.len; i++) {
        if (source_is_unit && targets.data[i]->kind == TY_LITERAL && !targets.data[i]->literal.enum_decl &&
            st->relation != TY_RELATION_COMPARABLE
        ) {
            continue;
        }
        if (source_enum && targets.data[i]->kind == TY_LITERAL && (targets.data[i]->flags & TYF_ENUM_LITERAL) &&
            targets.data[i]->literal.enum_decl == source_enum
        ) {
            continue;
        }
        Ternary related = is_related_to_side(me, st, source, targets.data[i], false, 1);
        if (related) return related;
    }
    if (report) {
        // tsc elaborates on the member that's the likely intent
        Ty const* best = best_matching_type(me, st, source, target);
        if (best) is_related_to_side(me, st, source, best, true, 1);
    }
    return TERNARY_FALSE;
}

static Ternary object_related(
    Checker* me, RelationState* st, Ty const* original, Ty const* source, Ty const* target, bool report, bool source_is_apparent
) {
    // (A primitive's apparent type explains nothing: tsc's reportStructuralErrors)
    if (source_is_apparent) report = false;
    Ternary result = props_related(me, st, source, target, report, source_is_apparent);
    if (result) result &= signatures_related(me, st, source, target, false, report);
    if (result) result &= signatures_related(me, st, source, target, true, report);
    bool primitive = source_is_apparent && !(original->flags & (TYF_OBJECT | TYF_INTERSECTION | TYF_UNION | TYF_TYPE_PARAM |
        TYF_DEFERRED | TYF_NON_PRIMITIVE | TYF_ANY | TYF_UNKNOWN | TYF_NEVER));
    if (result) result &= index_signatures_related(me, st, source, target, primitive, report);
    return result;
}

static Ty const* intersection_constraint(Checker* me, Ty const* ty);
static Ty const* normalized_intersection(Checker* me, Ty const* ty, bool writing);

// The mapped type an object type is, generic (keys not known yet) or not (tsc's ObjectFlags.Mapped). Null if none
static MappedInfo const* mapped_info_of(Checker* me, Ty const* ty, bool* generic) {
    if (ty->kind != TY_ANONYMOUS || !(ty->flags & TYF_OBJECT) || ty->anonymous.function) return nullptr;
    Members const* members = ty_members(me, ty);
    if (generic) *generic = members->mapped != nullptr;
    return members->mapped ? members->mapped : members->expanded_from;
}

// tsc's getMappedTypeNameTypeKind == Remapping: an `as` clause that isn't just a filter of the keys
static bool is_remapping(Checker* me, MappedInfo const* mapped) {
    return mapped->name_type && !ty_is_related(me, mapped->name_type, mapped->type_param, TY_RELATION_ASSIGNABLE);
}

bool ty_mapped_is_remapping(Checker* me, MappedInfo const* mapped) { return is_remapping(me, mapped); }

// tsc's getCombinedMappedTypeOptionality: whether the mapped types a homomorphic mapped type takes its modifiers from
// add `?` (1) or remove it (-1)
static int combined_mapped_optionality(Checker* me, Ty const* ty, u32 depth) {
    if (depth > 8) return 0;
    if (ty->kind == TY_INTERSECTION) {
        TyList members = ty->composite.types;
        int optionality = combined_mapped_optionality(me, members.data[0], depth + 1);
        for (u32 i = 1; i < members.len; i++) {
            if (combined_mapped_optionality(me, members.data[i], depth + 1) != optionality) return 0;
        }
        return optionality;
    }
    MappedInfo const* mapped = mapped_info_of(me, ty, nullptr);
    if (!mapped) return 0;
    if (mapped->optional) return mapped->optional;
    return mapped->modifiers_type ? combined_mapped_optionality(me, mapped->modifiers_type, depth + 1) : 0;
}

// tsc's couldAccessOptionalProperty: some optional property of the object has a key the index could be
static bool could_access_optional_property(Checker* me, Ty const* object, Ty const* index) {
    Ty const* index_constraint = base_constraint(me, index, 0);
    if (!index_constraint) return false;
    Members const* members = ty_members(me, object);
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members->props[i];
        if (!(prop->flags & PROP_OPTIONAL)) continue;
        Ty const* key = ty_prop_key_literal(me, prop);
        if (key && ty_is_related(me, key, index_constraint, TY_RELATION_ASSIGNABLE)) return true;
    }
    return false;
}

// `{ [P in K]: E }[X]` as E with X for P (tsc's substituteIndexedMappedType), plus undefined if the property can be
// optional
static Ty const* substitute_indexed_mapped(Checker* me, Ty const* object, MappedInfo const* mapped, bool generic, Ty const* index) {
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &index, .len = 1 });
    Ty const* ret = ty_instantiate(me, ty_mapped_template(me, mapped), mapper);
    bool optional = mapped->optional > 0 || (generic ?
        mapped->modifiers_type && combined_mapped_optionality(me, mapped->modifiers_type, 0) > 0 :
        could_access_optional_property(me, object, index));
    if (optional && compiler_options()->strict_null_checks) {
        Ty const* both[2] = { ret, ty_intrinsic(me, TY_UNDEFINED) };
        ret = ty_union(me, both, 2, nullptr, (TyList){0});
    }
    return ret;
}

// tsc's getActualTypeVariable: what a substitution type stands for
static Ty const* actual_type_variable(Ty const* ty) {
    while (ty->kind == TY_SUBSTITUTION) ty = ty->substitution.base;
    return ty;
}

// tsc's isExcludedMappedPropertyName: a conditional type pretending to be a negated type (`Exclude<keyof P, "a">`)
// leaves out the names its extends type takes
static bool is_excluded_mapped_name(Checker* me, Ty const* constraint, Ty const* key) {
    if (constraint->kind == TY_CONDITIONAL) {
        return (ty_conditional_branch(me, constraint, true)->flags & TYF_NEVER) &&
            actual_type_variable(ty_conditional_branch(me, constraint, false)) == actual_type_variable(constraint->conditional.check) &&
            ty_is_related(me, key, constraint->conditional.extends, TY_RELATION_ASSIGNABLE);
    }
    if (constraint->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < constraint->composite.types.len; i++) {
            if (is_excluded_mapped_name(me, constraint->composite.types.data[i], key)) return true;
        }
    }
    return false;
}

// The contextual type of property `key` in a generic mapped type without an `as` clause (tsc's
// getTypeOfPropertyOfContextualType): the template with the key for the type parameter, if the constraint allows the
// key. Null if it isn't such a mapped type or the key isn't one of its keys
Ty const* ty_generic_mapped_prop_context(Checker* me, Ty const* object, Ty const* key) {
    if (!(object->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return nullptr;
    bool generic = false;
    MappedInfo const* mapped = mapped_info_of(me, object, &generic);
    if (!mapped || !generic || mapped->name_type) return nullptr;
    Ty const* constraint = mapped->constraint;
    if (is_excluded_mapped_name(me, constraint, key)) return nullptr;
    // (tsc's getBaseConstraintOfType: of a union too, `[K in T["type"] | "*"]` takes any string)
    Ty const* base = constraint->kind == TY_TYPE_PARAM || (constraint->flags & (TYF_DEFERRED | TYF_UNION | TYF_INTERSECTION)) ?
        base_constraint(me, constraint, 0) : nullptr;
    if (!ty_is_related(me, key, base ? base : constraint, TY_RELATION_ASSIGNABLE)) return nullptr;
    return substitute_indexed_mapped(me, object, mapped, true, key);
}

// tsc's shouldDeferIndexType for an intersection: a type variable in it next to `{}` (`NonNullable<T[K]>` is
// `T[K] & {}`), which distributing would lose
static bool defers_index(Checker* me, Ty const* intersection) {
    bool instantiable = false, empty = false;
    for (u32 i = 0; i < intersection->composite.types.len; i++) {
        Ty const* member = intersection->composite.types.data[i];
        instantiable |= member->kind == TY_TYPE_PARAM || (member->flags & TYF_DEFERRED);
        empty |= ty_is_empty_anonymous_object(me, member);
    }
    return instantiable && empty;
}

static Ty const* simplified_parts(Checker* me, Ty const* object, Ty const* index, TyList over, bool over_index, bool intersect, bool writing) {
    Ty const** parts = malloc(sizeof(Ty const*) * over.len);
    panic_if(!parts, "out of memory");
    for (u32 i = 0; i < over.len; i++) {
        // (tsc's getIndexedAccessType without an access node: what isn't there is unknown)
        Ty const* part = over_index ? ty_indexed_instantiated(me, object, over.data[i], false) : ty_indexed_instantiated(me, over.data[i], index, false);
        parts[i] = ty_simplified(me, part, writing);
    }
    Ty const* ret = intersect ? ty_intersection(me, parts, over.len, nullptr, (TyList){0}) : ty_union(me, parts, over.len, nullptr, (TyList){0});
    free(parts);
    return ret;
}

static Ty const* compute_simplified(Checker* me, Ty const* ty, bool writing) {
    Ty const* object = ty_simplified(me, ty->indexed.object, writing);
    Ty const* index = ty_simplified(me, ty->indexed.index, writing);
    // T[A | B] -> T[A] | T[B] (reading), T[A] & T[B] (writing)
    if (index->kind == TY_UNION) return simplified_parts(me, object, index, index->composite.types, true, writing, writing);
    // (T | U)[K] -> T[K] | U[K] (reading), T[K] & U[K] (writing); (T & U)[K] -> T[K] & U[K]. Only once the index can't be
    // instantiated into a union that would distribute again
    if (index->kind != TY_TYPE_PARAM && !(index->flags & TYF_DEFERRED)) {
        if (object->kind == TY_UNION) return simplified_parts(me, object, index, object->composite.types, false, writing, writing);
        if (object->kind == TY_INTERSECTION && !defers_index(me, object)) {
            return simplified_parts(me, object, index, object->composite.types, false, true, writing);
        }
    }
    // `{ [P in K]: E }[X]` with K generic is E with X for P (not through a remapping `as` clause)
    bool generic = false;
    MappedInfo const* mapped = (object->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) ? mapped_info_of(me, object, &generic) : nullptr;
    if (mapped && generic && !is_remapping(me, mapped)) {
        Ty const* substituted = substitute_indexed_mapped(me, object, mapped, true, ty->indexed.index);
        if (substituted->kind != TY_UNION) return ty_simplified(me, substituted, writing);
        TyList members = substituted->composite.types;
        Ty const** parts = malloc(sizeof(Ty const*) * members.len);
        panic_if(!parts, "out of memory");
        for (u32 i = 0; i < members.len; i++) parts[i] = ty_simplified(me, members.data[i], writing);
        Ty const* ret = ty_union(me, parts, members.len, nullptr, (TyList){0});
        free(parts);
        return ret;
    }
    return ty;
}

// tsc's distributeIndexOverObjectType for reading: `(T | U)[K]` is T[K] | U[K], `(T & U)[K]` T[K] & U[K]. Null if the
// object is neither (or an intersection that defers its index)
Ty const* ty_distribute_index_over_object(Checker* me, Ty const* object, Ty const* index) {
    if (object->kind == TY_UNION) return simplified_parts(me, object, index, object->composite.types, false, false, false);
    if (object->kind == TY_INTERSECTION && !defers_index(me, object)) {
        return simplified_parts(me, object, index, object->composite.types, false, true, false);
    }
    return nullptr;
}

// `T extends U ? T : never` is T when T is always a U, never when it never is, and `T extends U ? never : T` the other
// way around (tsc's getSimplifiedConditionalType)
static Ty const* compute_simplified_conditional(Checker* me, Ty const* ty, bool writing) {
    Ty const* check = ty->conditional.check;
    Ty const* extends = ty->conditional.extends;
    Ty const* if_true = ty_conditional_branch(me, ty, true);
    Ty const* if_false = ty_conditional_branch(me, ty, false);
    Ty const* both[2] = { check, extends };
    if ((if_false->flags & TYF_NEVER) && actual_type_variable(if_true) == actual_type_variable(check)) {
        if ((check->flags & TYF_ANY) || ty_definitely_assignable(me, check, extends)) return ty_simplified(me, if_true, writing);
        if (ty_intersection(me, both, 2, nullptr, (TyList){0})->flags & TYF_NEVER) return ty_intrinsic(me, TY_NEVER);
    } else if ((if_true->flags & TYF_NEVER) && actual_type_variable(if_false) == actual_type_variable(check)) {
        if (!(check->flags & TYF_ANY) && ty_definitely_assignable(me, check, extends)) return ty_intrinsic(me, TY_NEVER);
        if ((check->flags & TYF_ANY) || (ty_intersection(me, both, 2, nullptr, (TyList){0})->flags & TYF_NEVER)) {
            return ty_simplified(me, if_false, writing);
        }
    }
    return ty;
}

// A simpler form of an indexed access or conditional type, or the type itself (tsc's getSimplifiedType). `writing`:
// the target of an assignment, where a union of keys is the intersection of their properties
Ty const* ty_simplified(Checker* me, Ty const* ty, bool writing) {
    if (ty->kind != TY_INDEXED && ty->kind != TY_CONDITIONAL) return ty;
    u64 key[3] = { KEY_SIMPLIFIED, ty->id, writing };
    Ty const* cached = intern_get(&my links, key, 3);
    if (cached) return cached;
    // (Itself while in flight, like tsc's circularConstraintType marker)
    *intern_slot(me, &my links, key, 3) = ty;
    Ty const* ret = ty->kind == TY_CONDITIONAL ? compute_simplified_conditional(me, ty, writing) : compute_simplified(me, ty, writing);
    *intern_slot(me, &my links, key, 3) = ret;
    return ret;
}

// tsc's getIndexedAccessTypeOrUndefined without an access node: null for a property that isn't there
static Ty const* indexed_or_null_flagged(Checker* me, Ty const* object, Ty const* index, bool include_undefined) {
    if (ty_is_generic_object(me, object) || ty_is_generic_index(index) || (object->flags & TYF_ANY) ||
        (index->flags & (TYF_ERROR | TYF_NEVER))
    ) {
        return ty_indexed_include_undefined(me, object, index, include_undefined);
    }
    return ty_access_without_node_include_undefined(me, object, index, include_undefined);
}

static Ty const* indexed_or_null(Checker* me, Ty const* object, Ty const* index) {
    return indexed_or_null_flagged(me, object, index, false);
}

// `{ [P in K]: E }[X]` with K known and X generic (tsc's isMappedTypeGenericIndexedAccess): its constraint is E with X
// for P. Null if it isn't one
static Ty const* mapped_generic_indexed_access(Checker* me, Ty const* indexed) {
    Ty const* object = indexed->indexed.object;
    if (!ty_is_generic_index(indexed->indexed.index)) return nullptr;
    bool generic = false;
    MappedInfo const* mapped = mapped_info_of(me, object, &generic);
    if (!mapped || generic || mapped->optional < 0 || mapped->name_type) return nullptr;
    return substitute_indexed_mapped(me, object, mapped, false, indexed->indexed.index);
}

static Ty const* constraint_of_indexed(Checker* me, Ty const* indexed);

// tsc's getSimplifiedTypeOrConstraint: getConstraintOfType of what can't be simplified
static Ty const* simplified_or_constraint(Checker* me, Ty const* ty) {
    Ty const* simplified = ty_simplified(me, ty, false);
    if (simplified != ty) return simplified;
    switch ((TyKind)ty->kind) {
        case TY_TYPE_PARAM: return ty_param_constraint(me, ty);
        case TY_INDEXED: return constraint_of_indexed(me, ty);
        case TY_INTERSECTION: return intersection_constraint(me, ty);
        case TY_CONDITIONAL:
        case TY_KEYOF:
        case TY_TEMPLATE_LITERAL:
        case TY_STRING_MAPPING:
        case TY_SUBSTITUTION:
        case TY_REFERENCE: // (a generic tuple)
            return base_constraint(me, ty, 0);
        default:
            return nullptr;
    }
}

// What `T[K]` is at least (tsc's getConstraintFromIndexedAccess): through the index's constraint first, then the
// object's. The result can be deferred itself (`T[K]` -> `Record<K, number>[K]`), the relation goes on from there
static Ty const* constraint_of_indexed(Checker* me, Ty const* indexed) {
    Ty const* mapped = mapped_generic_indexed_access(me, indexed);
    if (mapped) return mapped;
    Ty const* object = indexed->indexed.object;
    Ty const* index = indexed->indexed.index;
    Ty const* index_constraint = simplified_or_constraint(me, index);
    bool include_undefined = indexed->indexed.include_undefined;
    if (index_constraint && index_constraint != index) {
        Ty const* ret = indexed_or_null_flagged(me, object, index_constraint, include_undefined);
        if (ret && ret != indexed) return ret;
    }
    Ty const* object_constraint = simplified_or_constraint(me, object);
    if (object_constraint && object_constraint != object) {
        Ty const* ret = indexed_or_null_flagged(me, object_constraint, index, include_undefined);
        if (ret != indexed) return ret;
    }
    return nullptr;
}

// The `as` type of each key of the apparent type of a mapped type's `keyof X` (tsc's getApparentMappedTypeKeys with
// forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType): X's properties and index signature keys. An intersection's
// generic members have none
static Ty const* apparent_mapped_type_keys(Checker* me, MappedInfo const* mapped) {
    Ty const* modifiers = mapped->modifiers_type;
    TyList parts = modifiers->kind == TY_INTERSECTION ? modifiers->composite.types : (TyList){ .data = &modifiers, .len = 1 };
    TyBuf keys = {0};
    for (u32 p = 0; p < parts.len; p++) {
        Ty const* part = apparent_type(me, parts.data[p]);
        if (!(part->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, part);
        if (members->mapped) continue;
        for (u32 i = 0; i < members->prop_count + members->index_count; i++) {
            Ty const* key = i < members->prop_count ? ty_prop_key_literal(me, members->props[i]) :
                members->index[i - members->prop_count].key;
            if (i < members->prop_count && (members->props[i]->flags & (PROP_PRIVATE | PROP_PROTECTED))) continue;
            TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &key, .len = 1 });
            ty_buf_push(&keys, ty_instantiate(me, mapped->name_type, mapper));
        }
    }
    Ty const* ret = ty_union(me, keys.data, keys.len, nullptr, (TyList){0});
    ty_buf_drop(&keys);
    return ret;
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

// A generic mapped type's template with its key as the type parameter of this instantiation (tsc's
// getTemplateTypeFromMappedType of an instantiated mapped type, whose type parameter is fresh): what inference and the
// index signature relation both see, so `F[P]` inferred from `Readonly<F>` is the `F[P]` it is related as
Ty const* ty_mapped_template_fresh(Checker* me, MappedInfo const* mapped) {
    return ty_instantiate(me, ty_mapped_template(me, mapped), mapped_key_param_mapper(me, mapped));
}

// tsc's mappedTypeRelatedTo: two mapped types whose keys aren't known relate by their modifiers (the target at least as
// optional), constraints (the other way around), name types (the same) and templates, once the source's key type
// parameter is the target's
static int combined_mapped_info_optionality(Checker* me, MappedInfo const* mapped) {
    if (mapped->optional) return mapped->optional;
    return mapped->modifiers_type ? combined_mapped_optionality(me, mapped->modifiers_type, 1) : 0;
}

static Ternary mapped_related(Checker* me, RelationState* st, MappedInfo const* source, MappedInfo const* target, bool report) {
    bool modifiers_related = st->relation == TY_RELATION_COMPARABLE ||
        combined_mapped_info_optionality(me, source) <= combined_mapped_info_optionality(me, target);
    if (!modifiers_related) return TERNARY_FALSE;
    report_variance(me, source->constraint,
        combined_mapped_info_optionality(me, source) < 0 ? VARIANCE_UNMEASURABLE : VARIANCE_UNRELIABLE);
    Ternary result = is_related_to(me, st, target->constraint, source->constraint, report);
    if (!result) return TERNARY_FALSE;
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = &source->type_param, .len = 1 },
        (TyList){ .data = &target->type_param, .len = 1 });
    Ty const* source_name = source->name_type ? ty_instantiate(me, source->name_type, mapper) : nullptr;
    Ty const* target_name = target->name_type ? ty_instantiate(me, target->name_type, mapper) : nullptr;
    if (source_name != target_name) return TERNARY_FALSE;
    // (Templates with `?` include undefined: tsc's getTemplateTypeFromMappedType)
    Ty const* source_template = ty_mapped_template(me, source);
    if (source->optional > 0) source_template = ty_optional_prop_type(me, source_template);
    Ty const* target_template = ty_mapped_template(me, target);
    if (target->optional > 0) target_template = ty_optional_prop_type(me, target_template);
    return result & is_related_to(me, st, ty_instantiate(me, source_template, mapper), target_template, report);
}

// A tuple with a variadic element (a rest element that isn't an array: tsc's ElementFlags.Variadic)
static bool has_variadic_element(Checker* me, Ty const* ty) {
    if (!ty_is_tuple(ty)) return false;
    TupleShape const* shape = ty_tuple_shape(ty);
    TyList args = ty_reference_args(me, ty);
    for (u32 i = 0; i < shape->arity && i < args.len; i++) {
        if ((shape->flags[i] & TUPLE_ELEMENT_REST) && !ty_is_array(me, args.data[i]) && !ty_is_tuple(args.data[i])) return true;
    }
    return false;
}

static Ternary generic_mapped_target_related(
    Checker* me, RelationState* st, Ty const* source, MappedInfo const* mapped, bool report
) {
    Ty const* template = ty_mapped_template(me, mapped);
    // (Only a generic mapped source relates as a mapped type: `{ [P in string]: X }` is an object with an index
    // signature, whose keys are none, tsc's isGenericMappedType(source))
    bool generic_source = false;
    if (source->kind == TY_ANONYMOUS && (source->flags & TYF_OBJECT) && ty_members(me, source)->mapped) {
        mapped_info_of(me, source, &generic_source);
    }
    if (generic_source) return mapped_related(me, st, ty_members(me, source)->mapped, mapped, report);
    // (Not with `-?`: tsc skips the target's rules for MappedTypeModifiers.ExcludeOptional, T isn't Required<T>)
    if (mapped->optional < 0) return TERNARY_FALSE;
    if (!mapped->name_type && template->kind == TY_INDEXED && template->indexed.object == source &&
        template->indexed.index == mapped->type_param
    ) {
        return TERNARY_TRUE;
    }
    Ty const* target_keys = mapped->name_type ? mapped->name_type : mapped->constraint;
    // (The source's property names: its index signatures don't make it have every key, IndexFlags.NoIndexSignatures)
    Ty const* source_keys = ty_keyof_without_index_signatures(me, source);
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
        Ty const* indexed = ty_indexed(me, source, common);
        return is_related_to(me, st, indexed, ty_instantiate(me, template, keyed), report);
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
            Ty const* indexed = ty_indexed(me, source, keys);
            related = is_related_to(me, st, indexed, ty_instantiate(me, template, mapper), report);
        } else if (!known.len && !mapped->name_type) {
            // Only generic keys in common (`{ x: 1 }` to `{ [K in keyof T]?: number }`): S[common & P] against the
            // template (tsc's structuredTypeRelatedToWorker with filteredByApplicability)
            Ty const* parts[2] = { common, mapped->type_param };
            Ty const* indexing = ty_intersection(me, parts, 2, nullptr, (TyList){0});
            related = is_related_to(me, st, ty_indexed(me, source, indexing), template, report);
        }
        ty_buf_drop(&known);
        return related;
    }
    if (!is_related_to(me, st, target_keys, source_keys, false)) return TERNARY_FALSE;
    if (keyed) {
        Ty const* indexed = ty_indexed(me, source, target_keys);
        return is_related_to(me, st, indexed, ty_instantiate(me, template, keyed), report);
    }
    if (!mapped->name_type && template->kind == TY_INDEXED && template->indexed.index == mapped->type_param) {
        return is_related_to(me, st, source, template->indexed.object, report);
    }
    // S[P] against the template, with P as Q on both sides (P has no constraint of its own here)
    TyMapper const* mapper = ty_mapper_new(
        me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &mapped->constraint, .len = 1 }
    );
    Ty const* indexed = ty_indexed(me, source, target_keys);
    return is_related_to(me, st, indexed, ty_instantiate(me, template, mapper), report);
}

// What can be written to `T[K]` whatever T and K turn out to be (tsc's structuredTypeRelatedTo with AccessFlags.Writing):
// the access on the base constraints, if that isn't generic any more. Through a union of keys, a value has to fit
// every key's property, so it's the intersection; a union object is read like any union (the union of the members'
// properties). HACK: tsc also ignores index signatures when the object is a constraint (AccessFlags.NoIndexSignatures)
// Whether `object[key]` would be read from an index signature whose key isn't number (tsc's getPropertyTypeForIndexType:
// not a property the literal key names)
[[gnu::noinline, gnu::cold]] static bool through_non_number_index(Checker* me, Ty const* object, Ty const* key) {
    if (!(object->flags & (TYF_OBJECT | TYF_INTERSECTION))) return false;
    if (key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL)) {
        bool is_number = false;
        Atom const* name = key->kind == TY_LITERAL ? ty_property_name_of_key(me, key, &is_number) : nullptr;
        if (name && ty_get_prop(me, object, name)) return false;
    }
    IndexInfo const* info = ty_applicable_index_info(me, object, key);
    return info && !(info->key->flags & TYF_NUMBER);
}

// (An optional property is written its declared type, without exactOptionalPropertyTypes' missing type: tsc's
// getWriteTypeOfSymbol)
static Ty const* write_type_or_null(Checker* me, Ty const* type) {
    return type ? ty_remove_missing(me, type, true) : nullptr;
}

static Ty const* write_constraint_of_indexed(Checker* me, Ty const* indexed) {
    Ty const* object = indexed->indexed.object;
    Ty const* index = indexed->indexed.index;
    Ty const* base_object = object->kind == TY_INTERSECTION ? intersection_constraint(me, object) : base_constraint(me, object, 0);
    Ty const* base_index = base_constraint(me, index, 0);
    if (!base_object) base_object = object;
    if (!base_index) base_index = index;
    if (ty_is_generic_object(me, base_object) || ty_is_generic_index(base_index)) return nullptr;
    // (Through the object's constraint, not its index signatures other than a number one: tsc's
    // AccessFlags.NoIndexSignatures, writing `T[keyof T]` with `T extends { [k: string]: number }` isn't safe)
    bool no_index_signatures = base_object != object;
    if (base_index->kind != TY_UNION || (base_index->flags & TYF_BOOLEAN)) {
        if (no_index_signatures && through_non_number_index(me, base_object, base_index)) return nullptr;
        return write_type_or_null(me, indexed_or_null(me, base_object, base_index));
    }
    TyList members = base_index->composite.types;
    Ty const** parts = malloc(sizeof(Ty const*) * members.len);
    panic_if(!parts, "out of memory");
    bool resolved = true;
    for (u32 i = 0; i < members.len && resolved; i++) {
        parts[i] = no_index_signatures && through_non_number_index(me, base_object, members.data[i]) ? nullptr :
            write_type_or_null(me, indexed_or_null(me, base_object, members.data[i]));
        resolved = parts[i] != nullptr;
    }
    Ty const* ret = resolved ? ty_intersection(me, parts, members.len, nullptr, (TyList){0}) : nullptr;
    free(parts);
    return ret;
}

// A distributive conditional type with its check type replaced by the constraint (tsc's
// getConstraintOfDistributiveConditionalType): InstanceType<T> for `T extends typeof C` is C, `Extract<keyof T, string>`
// is string. The root's check type parameter is what gets mapped, so an instantiated check type like `keyof T` has its
// constraint distributed over. Null if that's no different
// The conditional types a root instantiated to (one, or a distribution's union of them) as instances of the alias
// `like` goes by (tsc keeps the alias on the root, so they print as `ReturnType<T[keyof T]>`). Only when the alias's
// type parameters are what the root's mapper maps (checked on `like`)
static Ty const* realias_conditional_instances(Checker* me, Ty const* ty, Ty const* like, Ty const* root) {
    TyList params = ty_alias_type_params(me, like->alias);
    if (params.len != like->alias_args.len || !params.len || params.len > 16) return ty;
    for (u32 i = 0; i < params.len; i++) {
        if (ty_instantiate(me, params.data[i], like->conditional.mapper) != like->alias_args.data[i]) return ty;
    }
    TyList members = ty->kind == TY_UNION ? ty->composite.types : (TyList){ .data = &ty, .len = 1 };
    Ty const** realiased = malloc(sizeof(Ty const*) * members.len);
    panic_if(!realiased, "out of memory");
    bool changed = false;
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = members.data[i];
        realiased[i] = member;
        if (member->kind != TY_CONDITIONAL || member->alias || member->conditional.root != root) continue;
        Ty const* args[16];
        for (u32 j = 0; j < params.len; j++) args[j] = ty_instantiate(me, params.data[j], member->conditional.mapper);
        realiased[i] = ty_realias(me, member, like->alias, (TyList){ .data = args, .len = params.len });
        changed = true;
    }
    Ty const* ret = !changed ? ty : ty->kind == TY_UNION ? ty_union(me, realiased, members.len, nullptr, (TyList){0}) : realiased[0];
    free(realiased);
    return ret;
}

static Ty const* distributive_constraint_as(Checker* me, Ty const* conditional, u32 depth, bool immediate);
static Ty const* distributive_constraint(Checker* me, Ty const* conditional, u32 depth) {
    return distributive_constraint_as(me, conditional, depth, false);
}

// `immediate`: the check type's constraint is tsc's getConstraintOfType, one step (an indexed access's is
// getConstraintFromIndexedAccess, which the relation goes on from: `(any[] & T)[K]` -> `(any[] & T)[keyof T]`). Else
// its base constraint (HACK: what base_constraint() and the other users took before the relation did it tsc's way;
// the immediate one there makes base_constraint() stop at a union of conditional types, see hacks.md)
static Ty const* distributive_constraint_uncached(Checker* me, Ty const* conditional, u32 depth, bool immediate);
static Ty const* distributive_constraint_as(Checker* me, Ty const* conditional, u32 depth, bool immediate) {
    if (!conditional->conditional.distributive) return nullptr;
    // (Cached like tsc's resolvedConstraintOfDistributive: the relation asks again and again, and base_constraint()
    // of nested conditional types too. Not below the top of a base_constraint(), whose depth cutoff may have cut it)
    if (!immediate && depth) return distributive_constraint_uncached(me, conditional, depth, false);
    u64 key[2] = { immediate ? KEY_IMMEDIATE_DISTRIBUTIVE : KEY_BASE_DISTRIBUTIVE, conditional->id };
    Ty const* cached = intern_get(&my links, key, 2);
    if (cached) return cached == conditional ? nullptr : cached;
    Ty const* ret = distributive_constraint_uncached(me, conditional, depth, immediate);
    *intern_slot(me, &my links, key, 2) = ret ? ret : conditional;
    return ret;
}

static Ty const* distributive_constraint_uncached(Checker* me, Ty const* conditional, u32 depth, bool immediate) {
    if (!conditional->conditional.distributive) return nullptr;
    Ty const* check = conditional->conditional.check;
    Ty const* check_constraint = check->kind == TY_TYPE_PARAM ? ty_param_constraint(me, check) :
        immediate && check->kind == TY_INDEXED ? simplified_or_constraint(me, check) :
        // (A union or intersection's is its base constraint, tsc's getConstraintOfType: NonNullable<T[K]>'s `T[K] & {}`)
        (check->flags & TYF_DEFERRED) || ((check->kind == TY_UNION || check->kind == TY_INTERSECTION) &&
            (check->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) ? base_constraint(me, check, depth + 1) : nullptr;
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
    Ty const* saved_root = my for_constraint_root;
    my for_constraint_root = root;
    Ty const* instantiated = ty_instantiate(me, root, mapper);
    my for_constraint_root = saved_root;
    if (conditional->alias && immediate) instantiated = realias_conditional_instances(me, instantiated, conditional, root);
    return instantiated != conditional && !(instantiated->flags & TYF_NEVER) ? instantiated : nullptr;
}

// The true branch of a conditional type as its default constraint sees it (tsc's getInferredTrueTypeFromConditionalType):
// with its `infer` type parameters instantiated with what was inferred for them. A deferred conditional type whose
// check type is generic inferred nothing, so they're unknown (or their constraint, when unknown doesn't satisfy it):
// the true branch of Awaited<T> is `F extends (value: infer V, ...) => any ? Awaited<V> : never` with F unknown, never
static Ty const* inferred_true_branch(Checker* me, Ty const* conditional) {
    Ty const* branch = ty_conditional_branch(me, conditional, true);
    Ty const* root = conditional->conditional.root ? conditional->conditional.root : conditional;
    TyList infers = ty_conditional_own_infers(me, root);
    if (!infers.len || !(conditional->conditional.check->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return branch;
    // (Cached like tsc's resolvedInferredTrueType: every relation of the conditional type as a source asks)
    u64 key[2] = { KEY_INFERRED_TRUE_BRANCH, conditional->id };
    Ty const* cached = intern_get(&my links, key, 2);
    if (cached) return cached;
    Ty const* stack_buf[16];
    Ty const** targets = infers.len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * infers.len);
    panic_if(!targets, "out of memory");
    for (u32 i = 0; i < infers.len; i++) {
        Ty const* constraint = ty_param_constraint(me, infers.data[i]);
        if (constraint && conditional->conditional.mapper) constraint = ty_instantiate(me, constraint, conditional->conditional.mapper);
        targets[i] = constraint && !(constraint->flags & (TYF_UNKNOWN | TYF_ANY)) ? constraint : ty_intrinsic(me, TY_UNKNOWN_);
    }
    Ty const* ret = ty_instantiate(me, branch, ty_mapper_new(me, infers, (TyList){ .data = targets, .len = infers.len }));
    if (targets != stack_buf) free(targets);
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

// tsc's computeBaseConstraint of a union: none when a member is a type variable without one (`R | string` with R
// unconstrained is no more than unknown, not string). An intersection keeps the members that have one
static bool union_member_lacks_base_constraint(Checker* me, Ty const* ty, u32 depth) {
    if (ty->kind != TY_UNION) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        Ty const* part = ty->composite.types.data[i];
        if ((part->kind == TY_TYPE_PARAM || (part->flags & TYF_DEFERRED)) && !base_constraint(me, part, depth + 1)) return true;
    }
    return false;
}

// What a type variable is known to be at least: tsc's getBaseConstraintOfType, without the circularity reporting.
// Null if there's nothing to know (an unconstrained type parameter, `T[K]` that stays generic...)
static Ty const* base_constraint_uncached(Checker* me, Ty const* ty, u32 depth) {
    if (depth > 8) return nullptr;
    switch ((TyKind)ty->kind) {
        case TY_TYPE_PARAM: {
            Ty const* constraint = ty_param_constraint(me, ty);
            if (!constraint || constraint == ty) return nullptr;
            if (constraint->kind == TY_TYPE_PARAM || (constraint->flags & TYF_DEFERRED)) {
                return base_constraint(me, constraint, depth + 1);
            }
            // Of a union or intersection, the base constraints of its members (tsc's getBaseConstraint of the
            // constraint): `T extends X & (C extends U ? A : B)` is at least `X & (A | B)`
            if ((constraint->kind == TY_UNION || constraint->kind == TY_INTERSECTION) &&
                (constraint->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)
            ) {
                if (union_member_lacks_base_constraint(me, constraint, depth + 1)) return nullptr;
                Ty const* base = base_constraint(me, constraint, depth + 1);
                if (base) return base;
            }
            return constraint;
        }
        case TY_KEYOF: {
            Ty const* all[3] = { ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_ES_SYMBOL) };
            return ty_union(me, all, 3, nullptr, (TyList){0});
        }
        case TY_INDEXED: {
            // (tsc's getResolvedBaseConstraint simplifies first)
            Ty const* simple = ty_simplified(me, ty, false);
            if (simple == ty) simple = mapped_generic_indexed_access(me, ty);
            if (simple && simple != ty) {
                // (tsc's computeBaseConstraint of the simplified type: of a union or intersection its members', where
                // `T[string] & X` with T unconstrained is X)
                if ((simple->kind == TY_UNION || simple->kind == TY_INTERSECTION) && (simple->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
                    if (union_member_lacks_base_constraint(me, simple, depth + 1)) return nullptr;
                    Ty const* base = base_constraint(me, simple, depth + 1);
                    return base ? base : simple;
                }
                if (simple->kind != TY_TYPE_PARAM && !(simple->flags & TYF_DEFERRED)) return simple;
                Ty const* deeper = base_constraint(me, simple, depth + 1);
                return deeper ? deeper : nullptr;
            }
            Ty const* object = ty->indexed.object;
            Ty const* index = ty->indexed.index;
            Ty const* object_constraint = base_constraint(me, object, depth + 1);
            Ty const* index_constraint = base_constraint(me, index, depth + 1);
            if (!object_constraint && !index_constraint) return nullptr;
            // (A type variable without a constraint leaves nothing to know: tsc's computeBaseConstraint needs both,
            // `T[K]` with `K extends keyof T` isn't `T[string] | T[number] | T[symbol]`)
            if (!object_constraint && (object->kind == TY_TYPE_PARAM || (object->flags & TYF_DEFERRED))) return nullptr;
            if (!index_constraint && (index->kind == TY_TYPE_PARAM || (index->flags & TYF_DEFERRED))) return nullptr;
            Ty const* ret = ty_indexed_include_undefined(me, object_constraint ? object_constraint : object,
                index_constraint ? index_constraint : index, ty->indexed.include_undefined);
            if (ret == ty) return nullptr;
            if (ret->kind == TY_UNION) {
                if (union_member_lacks_base_constraint(me, ret, depth + 1)) return nullptr;
                Ty const* base = base_constraint(me, ret, depth + 1);
                return base ? base : ret;
            }
            // (tsc's computeBaseConstraint takes the base constraint of the access too: `[T][0]` is T's)
            return ret->kind == TY_TYPE_PARAM || (ret->flags & TYF_DEFERRED) ? base_constraint(me, ret, depth + 1) : ret;
        }
        case TY_CONDITIONAL: {
            Ty const* distributive = distributive_constraint(me, ty, depth);
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
                } else if ((parts[i]->kind == TY_UNION || parts[i]->kind == TY_INTERSECTION) &&
                    (parts[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)
                ) {
                    // (A union branch is its members' base constraints too, none if one has none: tsc's getBaseConstraint
                    // of the conditional's constraint)
                    if (union_member_lacks_base_constraint(me, parts[i], depth + 1)) return nullptr;
                    Ty const* base = base_constraint(me, parts[i], depth + 1);
                    if (base) parts[i] = base;
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
        case TY_SUBSTITUTION: {
            // Of `base & constraint`: the intersection of what the parts are at least
            Ty const* both = ty_substitution_intersection(me, ty);
            TyList parts = both->kind == TY_INTERSECTION ? both->composite.types : (TyList){ .data = &both, .len = 1 };
            TyBuf bases = {0};
            for (u32 i = 0; i < parts.len; i++) {
                Ty const* part = parts.data[i];
                Ty const* base = part->kind == TY_TYPE_PARAM || (part->flags & TYF_DEFERRED) ? base_constraint(me, part, depth + 1) : part;
                if (base) ty_buf_push(&bases, base);
            }
            Ty const* ret = bases.len ? ty_intersection(me, bases.data, bases.len, nullptr, (TyList){0}) : nullptr;
            ty_buf_drop(&bases);
            return ret;
        }
        case TY_UNION:
        case TY_INTERSECTION: {
            // The union or intersection of the members' (tsc's computeBaseConstraint): `T["k"] & {}` with T["k"]
            // constrained to `X | undefined` is X
            if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return nullptr;
            if (union_member_lacks_base_constraint(me, ty, depth)) return nullptr;
            TyList types = ty->composite.types;
            TyBuf bases = {0};
            bool different = false;
            for (u32 i = 0; i < types.len; i++) {
                Ty const* part = types.data[i];
                Ty const* base = part->kind == TY_TYPE_PARAM || (part->flags & (TYF_DEFERRED | TYF_UNION | TYF_INTERSECTION)) ?
                    base_constraint(me, part, depth + 1) : part;
                // (A nested union or intersection without type variables to replace has none of its own: itself)
                if (!base && (part->kind == TY_UNION || part->kind == TY_INTERSECTION) && !(part->flags & TYF_DEFERRED)) base = part;
                if (base != part) different = true;
                if (base) ty_buf_push(&bases, base);
            }
            Ty const* ret = !different ? nullptr : ty->kind == TY_UNION ?
                ty_union(me, bases.data, bases.len, nullptr, (TyList){0}) :
                ty_intersection(me, bases.data, bases.len, nullptr, (TyList){0});
            ty_buf_drop(&bases);
            return ret;
        }
        case TY_STRING_MAPPING: {
            Ty const* inner = ty->string_mapping.type;
            Ty const* constraint = inner->kind == TY_TYPE_PARAM || (inner->flags & TYF_DEFERRED) ? base_constraint(me, inner, depth + 1) : nullptr;
            if (!constraint || constraint == inner) return ty_intrinsic(me, TY_STRING);
            Ty const* ret = ty_string_mapping(me, ty->string_mapping.mapping, ty->string_mapping.symbol, constraint);
            return ret == ty ? nullptr : ret;
        }
        case TY_REFERENCE: {
            // A generic tuple: its variadic elements as the arrays they're constrained to (tsc's computeBaseConstraint),
            // `[S, ...T]` with `T extends readonly X[]` is `[S, ...X[]]`
            if (!is_tuple(ty) || !ty_is_generic_tuple(me, ty)) return nullptr;
            TupleShape const* shape = ty_tuple_shape(ty);
            TyList args = ty_reference_args(me, ty);
            Ty const** elements = malloc(sizeof(Ty const*) * (args.len ? args.len : 1));
            panic_if(!elements, "out of memory");
            bool changed = false;
            for (u32 i = 0; i < args.len; i++) {
                elements[i] = args.data[i];
                if (i >= shape->arity || !(shape->flags[i] & TUPLE_ELEMENT_REST) || args.data[i]->kind != TY_TYPE_PARAM) continue;
                Ty const* constraint = base_constraint(me, args.data[i], depth + 1);
                if (constraint && ty_is_array(me, constraint)) {
                    elements[i] = constraint;
                    changed = true;
                }
            }
            Ty const* ret = changed ? ty_reference(me, ty->reference.target, (TyList){ .data = elements, .len = args.len }) : nullptr;
            free(elements);
            return ret;
        }
        default:
            return nullptr;
    }
}

// (Cached at depth 0 like tsc's resolvedBaseConstraint: React's `T & {}` check types ask again and again. The depth
// cutoff makes a deeper call's answer differ, so those aren't cached; a re-entered type computes again)
static Ty const* base_constraint_uncached(Checker* me, Ty const* ty, u32 depth);
static Ty const base_constraint_in_flight;
static Ty const* base_constraint(Checker* me, Ty const* ty, u32 depth) {
    if (depth || !(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return base_constraint_uncached(me, ty, depth);
    u64 key[2] = { KEY_BASE_CONSTRAINT, ty->id };
    Ty const* cached = intern_get(&my links, key, 2);
    if (cached) return cached == ty ? nullptr : cached == &base_constraint_in_flight ? base_constraint_uncached(me, ty, 0) : cached;
    *intern_slot(me, &my links, key, 2) = &base_constraint_in_flight;
    Ty const* ret = base_constraint_uncached(me, ty, 0);
    *intern_slot(me, &my links, key, 2) = ret ? ret : ty;
    return ret;
}

Ty const* ty_base_constraint(Checker* me, Ty const* ty) { return base_constraint(me, ty, 0); }

// tsc's getConstraintOfConditionalType: the distributive constraint, else the default one, the union of the branches
// (an `any` branch counting as never)
Ty const* ty_conditional_constraint(Checker* me, Ty const* conditional) {
    Ty const* distributive = distributive_constraint(me, conditional, 0);
    if (distributive) return distributive;
    Ty const* parts[2] = { inferred_true_branch(me, conditional), ty_conditional_branch(me, conditional, false) };
    if (parts[0]->flags & TYF_ANY) return parts[1];
    if (parts[1]->flags & TYF_ANY) return parts[0];
    return ty_union(me, parts, 2, nullptr, (TyList){0});
}

static Ternary target_each_related(Checker* me, RelationState* st, Ty const* source, TyList targets, bool report) {
    Ternary result = TERNARY_TRUE;
    bool saved = st->intersection_target;
    st->intersection_target = true;
    for (u32 i = 0; i < targets.len && result; i++) {
        Ternary related = is_related_to_side(me, st, source, targets.data[i], report, 1);
        result = related ? result & related : TERNARY_FALSE;
    }
    st->intersection_target = saved;
    return result;
}

bool ty_is_mutable_array_or_tuple(Checker* me, Ty const* ty) { return is_mutable_array_or_tuple(me, ty); }

static bool is_mutable_array_or_tuple(Checker* me, Ty const* ty) {
    if (ty->kind == TY_REFERENCE && ty->reference.target == my array_target) return true;
    if (is_empty_tuple(ty)) return !ty->declared.tuple->readonly;
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
            // (One without a constraint is left out, tsc's computeBaseConstraint: `"a" & K` is at least "a")
            Ty const* constraint = base_constraint(me, member, 0);
            if (!constraint || constraint == member) continue;
            member = constraint;
        }
        ty_buf_push(&parts, member);
    }
    if (!parts.len) {
        ty_buf_drop(&parts);
        return nullptr;
    }
    Ty const* ret = ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    return ret == ty ? nullptr : ret;
}

// tsc's getConstraintOfType, one step: a type parameter's constraint, an indexed access's getConstraintFromIndexedAccess,
// a conditional type's constraint, else the base constraint
static Ty const* immediate_constraint(Checker* me, Ty const* ty) {
    switch ((TyKind)ty->kind) {
        case TY_TYPE_PARAM: return ty_param_constraint(me, ty);
        case TY_INDEXED: return constraint_of_indexed(me, ty);
        case TY_CONDITIONAL: return ty_conditional_constraint(me, ty);
        default: return base_constraint(me, ty, 0);
    }
}

// What an intersection source with type variables is related through once its members weren't (tsc's
// getEffectiveConstraintOfIntersection): each instantiable member's constraint, followed through type parameters,
// keyof and conditional types but not indexed accesses (which may be circular), together with the member itself
// against a union target, and the members of disjoint domains or empty object types. Only against a union target or
// with such a member, else null. `V[K] & {}` (NonNullable) is `V[keyof V] & {}` there, not the `{}` its base constraint
// may come to
static Ty const* effective_intersection_constraint(Checker* me, Ty const* source, bool target_is_union) {
    TyList members = source->composite.types;
    TyBuf parts = {0};
    bool disjoint = false;
    u32 disjoint_flags = TYF_NON_PRIMITIVE | TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM | TYF_BIGINT |
        TYF_BIGINT_LITERAL | TYF_BOOLEAN | TYF_BOOLEAN_LITERAL | TYF_ES_SYMBOL_LIKE | TYF_VOID | TYF_UNDEFINED | TYF_NULL;
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = members.data[i];
        if (member->kind == TY_TYPE_PARAM || member->kind == TY_SUBSTITUTION || (member->flags & TYF_DEFERRED)) {
            Ty const* constraint = immediate_constraint(me, member);
            for (u32 depth = 0; constraint && depth < 32 &&
                (constraint->kind == TY_TYPE_PARAM || constraint->kind == TY_KEYOF || constraint->kind == TY_CONDITIONAL); depth++
            ) {
                Ty const* next = immediate_constraint(me, constraint);
                constraint = next == constraint ? nullptr : next;
            }
            if (constraint) {
                ty_buf_push(&parts, constraint);
                if (target_is_union) ty_buf_push(&parts, member);
            }
        } else if ((member->flags & disjoint_flags) || ty_is_empty_anonymous_object(me, member)) {
            disjoint = true;
        }
    }
    Ty const* ret = nullptr;
    if (parts.len && (target_is_union || disjoint)) {
        for (u32 i = 0; disjoint && i < members.len; i++) {
            Ty const* member = members.data[i];
            if ((member->flags & disjoint_flags) || ty_is_empty_anonymous_object(me, member)) ty_buf_push(&parts, member);
        }
        // (Normalized like the source was: `V[string | number | symbol] & {}` is `(V[string] & {}) | ...`)
        ret = normalized_intersection(me, ty_intersection_no_constraint_reduction(me, parts.data, parts.len), false);
        // (Not when the expansion contains the source itself)
        TyList expanded = ret->kind == TY_UNION ? ret->composite.types : (TyList){ .data = &ret, .len = 1 };
        for (u32 i = 0; ret && i < expanded.len; i++) if (expanded.data[i] == source) ret = nullptr;
    }
    ty_buf_drop(&parts);
    return ret;
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
// `{ kind: A | B, x: X }` to `{ kind: A, x: X } | { kind: B, x: X }`. The source may be an intersection (`{ x: X } &
// { kind: A | B }`, as `Omit<T, K> & N` is)
static Ternary related_to_discriminated(Checker* me, RelationState* st, Ty const* source, Ty const* target) {
    enum { MAX_COMBINATIONS = 25, MAX_DISCRIMINANTS = 8 };
    Ty const* apparent = apparent_type(me, source);
    if (!(apparent->flags & TYF_OBJECT) && apparent->kind != TY_INTERSECTION) return TERNARY_FALSE;
    // The object members of the target
    TyBuf objects = {0};
    for (u32 i = 0; i < target->composite.types.len; i++) {
        Ty const* member = target->composite.types.data[i];
        if (member->flags & (TYF_OBJECT | TYF_INTERSECTION)) ty_buf_push(&objects, member);
    }
    Ty const* object_target = objects.len >= 2 ? ty_union(me, objects.data, objects.len, nullptr, (TyList){0}) : nullptr;
    ty_buf_drop(&objects);
    if (!object_target || object_target->kind != TY_UNION) return TERNARY_FALSE;

    // The source's properties: an intersection's are those of its members, each name once (tsc's getPropertiesOfType)
    TyList parts = apparent->kind == TY_INTERSECTION ? apparent->composite.types : (TyList){ .data = &apparent, .len = 1 };
    Atom const* discriminants[MAX_DISCRIMINANTS];
    TyList values[MAX_DISCRIMINANTS];
    u32 count = 0;
    u32 combinations = 1;
    for (u32 p = 0; p < parts.len; p++) {
        Ty const* part = apparent->kind == TY_INTERSECTION ? apparent_type(me, parts.data[p]) : apparent;
        if (!(part->flags & TYF_OBJECT)) continue;
        Members const* source_members = ty_members(me, part);
        for (u32 i = 0; i < source_members->prop_count; i++) {
            Prop const* prop = source_members->props[i];
            bool seen = false;
            for (u32 d = 0; d < count && !seen; d++) seen = discriminants[d] == prop->name;
            if (seen || !ty_is_discriminant_property(me, object_target, prop->name)) continue;
            if (count == MAX_DISCRIMINANTS) return TERNARY_FALSE;
            FoundProp found = apparent->kind == TY_INTERSECTION ? find_prop(me, apparent, prop->name, 0) : (FoundProp){0};
            Ty const* type = apparent->kind == TY_INTERSECTION ? found.type : prop_type(me, prop);
            // (Optional ones with their undefined: tsc's getNonMissingTypeOfSymbol)
            if (((apparent->kind == TY_INTERSECTION ? found.flags : prop->flags) & PROP_OPTIONAL) && optional_adds_undefined()) {
                type = ty_union(me, (Ty const*[]){ type, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
            }
            if (type->kind == TY_UNION) {
                values[count] = type->composite.types;
            } else {
                Ty const** one = arena_new_n(&my arena, Ty const*, 1);
                one[0] = type;
                values[count] = (TyList){ .data = one, .len = 1 };
            }
            combinations *= values[count].len;
            if (combinations > MAX_COMBINATIONS) return TERNARY_FALSE;
            discriminants[count++] = prop->name;
        }
    }
    if (!count) return TERNARY_FALSE;

    TyList members = object_target->composite.types;
    bool require_optional = is_subtype_relation(st->relation) && requires_optional_properties(me, source);
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
                FoundProp target_prop = find_prop(me, members.data[m], discriminants[d], 0);
                if (!target_prop.found) {
                    fits = false;
                    break;
                }
                // (An optional target property takes `undefined`, which a source property typed `T | undefined` has
                // among its values: tsc's isPropertySymbolTypeRelated adds the optionality to the target's type)
                Ty const* target_type = target_prop.type;
                if ((target_prop.flags & PROP_OPTIONAL) && optional_adds_undefined()) {
                    Ty const* target_parts[2] = { target_type, ty_intrinsic(me, TY_UNDEFINED) };
                    target_type = ty_union(me, target_parts, 2, nullptr, (TyList){0});
                }
                fits = is_related_to(me, st, value, target_type, false) != TERNARY_FALSE;
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
            if (is_tuple(apparent) && is_tuple(part)) {
                // Tuples by their elements, the discriminant positions left out while the lengths are fixed (tsc's
                // propertiesRelatedTo with excludedProperties); their methods are the arrays' (tsc returns there)
                TupleShape const* source_shape = apparent->reference.target->declared.tuple;
                TupleShape const* target_shape = part->reference.target->declared.tuple;
                bool fixed = source_shape->arity == target_shape->arity;
                for (u32 i = 0; i < source_shape->arity && fixed; i++) {
                    fixed = !((source_shape->flags[i] | target_shape->flags[i]) & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST));
                }
                if (!fixed) {
                    result &= tuples_related(me, st, apparent, part, false);
                    continue;
                }
                TyList source_elements = ty_reference_args(me, apparent);
                TyList target_elements = ty_reference_args(me, part);
                for (u32 i = 0; i < source_shape->arity && result; i++) {
                    char digits[16];
                    snprintf(digits, sizeof(digits), "%u", i);
                    Atom const* name = checker_atom(me, csview_from(digits));
                    bool skip = false;
                    for (u32 d = 0; d < count; d++) skip |= discriminants[d] == name;
                    if (!skip) result &= is_related_to(me, st, source_elements.data[i], target_elements.data[i], false);
                }
                continue;
            }
            Members const* part_members = ty_members(me, part);
            for (u32 i = 0; i < part_members->prop_count && result; i++) {
                Atom const* name = part_members->props[i]->name;
                bool skip = false;
                for (u32 d = 0; d < count; d++) skip |= discriminants[d] == name;
                Ty const* name_type = ty_prop_name_literal(me, name);
                for (u32 j = 0; j < seen.len && !skip; j++) skip = seen.data[j] == name_type;
                if (skip) continue;
                ty_buf_push(&seen, name_type);
                FoundProp target_prop = find_prop(me, member, name, 0);
                if (!target_prop.found) continue;
                FoundProp source_prop = find_prop(me, apparent, name, 0);
                if (!source_prop.found) {
                    // (Optional ones too for the subtype relations: tsc's propertiesRelatedTo, requireOptionalProperties)
                    if (!(target_prop.flags & PROP_OPTIONAL) || require_optional) result = TERNARY_FALSE;
                    continue;
                }
                if ((source_prop.flags & PROP_OPTIONAL) && !(target_prop.flags & PROP_OPTIONAL) && st->relation != TY_RELATION_COMPARABLE) {
                    result = TERNARY_FALSE;
                    continue;
                }
                Ty const* target_type = target_prop.type;
                Ty const* source_type = source_prop.type;
                if ((target_prop.flags & PROP_OPTIONAL) && optional_adds_undefined()) {
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
                if (result && !(is_tuple(apparent) && is_tuple(part))) result &= index_signatures_related(me, st, apparent, part, false, false);
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
static bool relation_deeply_nested(
    Checker* me, Ty const* ty, Ty const* const* stack, uintptr_t* entries, u32* valid, u32 depth, u32 max, bool pushing
);

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

// The type tsc's compareProperties compares: an optional property's includes undefined with strictNullChecks (tsc's
// getTypeOfSymbol adds optionality; its missing type is identical to undefined), so `{ a?: string }` is identical to
// `{ a?: string | undefined }` and to `Partial<{ a: string }>` mapped again (whose template read added undefined)
static Ty const* identity_prop_type(Checker* me, Prop const* prop) {
    Ty const* type = prop_type(me, prop);
    // (Also under exactOptionalPropertyTypes: identity takes missing for undefined, both are the one Undefined singleton)
    if (!(prop->flags & PROP_OPTIONAL) || !compiler_options()->strict_null_checks) return type;
    Ty const* parts[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
    return ty_union(me, parts, 2, nullptr, (TyList){0});
}

static Ternary props_identical(Checker* me, RelationState* st, Prop const* source, Prop const* target) {
    if (source == target) return TERNARY_TRUE;
    if (!prop_modifiers_identical(source, target)) return TERNARY_FALSE;
    return is_related_to(me, st, identity_prop_type(me, source), identity_prop_type(me, target), false);
}

bool ty_props_identical(Checker* me, Prop const* source, Prop const* target) {
    if (source == target) return true;
    return prop_modifiers_identical(source, target) &&
        ty_is_related(me, identity_prop_type(me, source), identity_prop_type(me, target), TY_RELATION_IDENTITY);
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
    return result & is_related_to(me, st, ty_instantiate(me, ty_mapped_template(me, source), mapper), ty_mapped_template(me, target), false);
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
    // by the measured variances like the other relations, so an independent one doesn't count (tsc's relateVariances:
    // `C<unknown>` and `C<{}>` are identical if C doesn't use its type parameter). Where a measurement was unreliable
    // and they differ, structurally
    if (source->kind == TY_REFERENCE && target->kind == TY_REFERENCE && source->reference.target == target->reference.target &&
        !is_check_marker(me, source) && !is_check_marker(me, target)
    ) {
        TyList source_args = ty_reference_args(me, source);
        TyList target_args = ty_reference_args(me, target);
        Ty const* declared = source->reference.target;
        bool list = is_array_target(me, declared) || declared->declared.kind == TY_DECLARED_TUPLE_TARGET;
        Variances const* variances = list ? nullptr : get_variances(me, declared);
        if (!list && !variances && !is_marker_reference(me, source) && !is_marker_reference(me, target)) return TERNARY_MAYBE;
        Ternary result = TERNARY_TRUE;
        if (variances) {
            bool unreliable = false;
            result = type_args_related(me, st, variances, source_args, target_args, false, &unreliable);
            if (!result && unreliable) goto structural;
        } else {
            for (u32 i = 0; i < source_args.len && result; i++) result &= is_related_to(me, st, source_args.data[i], target_args.data[i], false);
        }
        Ty const* source_this = source->reference.this_arg;
        Ty const* target_this = target->reference.this_arg;
        if (result && source_this != target_this) {
            result &= is_related_to(me, st, source_this ? source_this : source, target_this ? target_this : target, false);
        }
        return result;
    }
structural:;
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
                Ty const* source_branch = ty_conditional_branch(me, source, true);
                result &= is_related_to(me, st, source_branch, ty_conditional_branch(me, target, true), false);
            }
            if (result) {
                Ty const* source_branch = ty_conditional_branch(me, source, false);
                result &= is_related_to(me, st, source_branch, ty_conditional_branch(me, target, false), false);
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
            Prop const* part_props[16];
            u32 part_count = 0;
            bool all_optional = true;
            u32 optional_parts = 0;
            for (u32 j = m; j < members.len && part_count < c_arraylen(parts); j++) {
                Prop const* part = ty_get_prop(me, members.data[j], name);
                if (!part) continue;
                if (part->flags & PROP_OPTIONAL) optional_parts |= 1u << part_count;
                part_props[part_count++] = part;
                all_optional &= (part->flags & PROP_OPTIONAL) != 0;
            }
            // The source's own property, which only one member has: the same symbol, nothing to relate (tsc's
            // propertiesRelatedTo skips `sourceProp === targetProp`; its type may still be being computed: a class's
            // static `create = f(C)` relating `typeof C` to `C & ...`)
            if (part_count == 1 && source->flags & TYF_OBJECT && ty_get_prop(me, apparent_type(me, source), name) == part_props[0]) continue;
            for (u32 j = 0; j < part_count; j++) parts[j] = prop_type(me, part_props[j]);
            // (An optional part's undefined counts when the property is required: see find_prop_in())
            if (optional_parts && !all_optional && compiler_options()->strict_null_checks) {
                for (u32 j = 0; j < part_count; j++) {
                    if (optional_parts & (1u << j)) parts[j] = ty_optional_prop_type(me, parts[j]);
                }
            }
            FoundProp target_prop = { .found = true, .flags = all_optional ? PROP_OPTIONAL : 0,
                .type = part_count == 1 ? parts[0] : ty_intersection(me, parts, part_count, nullptr, (TyList){0}) };
            FoundProp source_prop = find_prop(me, apparent_type(me, source), name, 0);
            if (!source_prop.found) {
                // Usually the members' business already, but a member can have been related without looking at its
                // properties: `Omit<any, K> & X` to `Omit<I, K> & X` through the alias's variances (tsc's
                // getUnmatchedProperty in propertiesRelatedTo)
                if (all_optional && (!is_subtype_relation(st->relation) || !requires_optional_properties(me, source))) continue;
                if (report) {
                    // (tsc names the member the property is in)
                    cstr source_str, target_str;
                    display_pair(me, source, true, members.data[m], &source_str, &target_str);
                    cstr prop_name = ty_prop_written_name(me, member->props[i]);
                    report_line(st, 2741, cstr_d(
                        "Property '", prop_name, "' is missing in type '", source_str, "' but required in type '", target_str, "'."
                    ));
                    report_related(st, 2728, cstr_d("'", prop_name, "' is declared here."), ty_prop_decl_span(member->props[i]));
                    c_drop(cstr, &source_str, &target_str, &prop_name);
                }
                return TERNARY_FALSE;
            }
            Ty const* target_type = target_prop.type;
            Ty const* source_type = source_prop.type;
            if (optional_adds_undefined()) {
                Ty const* undefined = ty_intrinsic(me, TY_UNDEFINED);
                if (target_prop.flags & PROP_OPTIONAL) target_type = ty_union(me, (Ty const*[]){ target_type, undefined }, 2, nullptr, (TyList){0});
                if (source_prop.flags & PROP_OPTIONAL) source_type = ty_union(me, (Ty const*[]){ source_type, undefined }, 2, nullptr, (TyList){0});
            }
            Ternary related = is_related_to(me, st, source_type, target_type, report);
            if (!related && report) {
                cstr prop_name = ty_prop_name_cstr(me, name);
                report_incompatible(st, 2326, cstr_d("Types of property '", prop_name, "' are incompatible."));
                cstr_drop(&prop_name);
            }
            result &= related;
        }
    }
    return result;
}

static bool is_empty_object_type(Checker* me, Ty const* ty);

static Ternary structured_related_impl(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report, bool use_variances
);

static Ternary structured_related(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    return structured_related_impl(me, st, source, target, report, true);
}

// Whether an intersection's apparent type is a union: a member type variable whose apparent type (its base
// constraint) is one, which the intersection distributes over
static bool intersection_apparent_is_union(Checker* me, Ty const* source) {
    if (!(source->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return false;
    for (u32 i = 0; i < source->composite.types.len; i++) {
        Ty const* member = source->composite.types.data[i];
        if (member->kind != TY_TYPE_PARAM && !(member->flags & TYF_DEFERRED)) continue;
        if (apparent_type(me, member)->kind == TY_UNION) return true;
    }
    return false;
}

// A conditional source: through its default constraint, then its distributive one (tsc's structuredTypeRelatedToWorker)
static Ternary conditional_source_related(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    // Already checking this conditional type as a source further up: don't recurse (tsc's
    // isDeeplyNestedType(source, sourceStack, sourceDepth, 10); it is on the stack already)
    if (st->source_depth && relation_deeply_nested(me, source, st->source_stack, st->source_entries, &st->source_entries_valid, st->source_depth - 1, 10, false)) return TERNARY_MAYBE;
    // Through the default constraint, the union of the branches (an any branch counts as never)
    Ty const* true_branch = inferred_true_branch(me, source);
    Ty const* false_branch = ty_conditional_branch(me, source, false);
    if (true_branch->flags & TYF_ANY) true_branch = ty_intrinsic(me, TY_NEVER);
    else if (false_branch->flags & TYF_ANY) false_branch = ty_intrinsic(me, TY_NEVER);
    // HACK: tsc relates the default constraint with RecursionFlags.Source; pushing the target too (a deep-nesting
    // cut-off tsc doesn't have) saves react-hook-form's Path/FieldPathValue chains 10% of the react corpus
    // (A union source: some member for the comparable relation, each one otherwise)
    bool some = st->relation == TY_RELATION_COMPARABLE;
    Ternary related = is_related_to(me, st, true_branch, target, false);
    if (some && !related) related = is_related_to(me, st, false_branch, target, false);
    else if (related && !some) related &= is_related_to(me, st, false_branch, target, false);
    if (related) return related;
    // Through the distributive constraint (not to another conditional type, like in tsc)
    if (target->kind != TY_CONDITIONAL) {
        // tsc's is the immediate one (distributive_constraint_as()). HACK: the base constraint one first, which
        // is cheaper when it works: the immediate one alone made react-hook-form's paths (bulletproof-react)
        // TS2589, see hacks.md
        Ty const* constraint = distributive_constraint(me, source, 0);
        if (constraint) {
            related = is_related_to_side(me, st, constraint, target, false, 2);
            if (related) return related;
        }
        // (Only with a base constraint, like tsc's hasNonCircularBaseConstraint gate.) Not while measuring
        // variances, where relating it walked react-hook-form's Path types through marker instantiations
        // (+12% instructions on bulletproof-react): the measurement is unreliable instead, so type arguments
        // it would fail are compared structurally
        Ty const* immediate = constraint ? distributive_constraint_as(me, source, 0, true) : nullptr;
        if (immediate && immediate != constraint && my variance_depth) {
            report_variance(me, source, VARIANCE_UNRELIABLE);
        } else if (immediate && immediate != constraint) {
            related = is_related_to_side(me, st, immediate, target, false, 2);
            if (related) return related;
        }
        // (What tsc reports: the distributive constraint's failure replaces the default one's)
        if (report && (immediate || constraint)) {
            return is_related_to_side(me, st, immediate ? immediate : constraint, target, report, 2);
        }
    }
    if (report) {
        // (Explained as the default constraint, the union of the branches, then its failing member: tsc's
        // isRelatedTo(getDefaultConstraintOfConditionalType(source), ...))
        Ty const* pair[2] = { true_branch, false_branch };
        Ty const* branches = ty_union(me, pair, 2, nullptr, (TyList){0});
        return is_related_to_side(me, st, branches, target, report, 2);
    }
    related = is_related_to_side(me, st, true_branch, target, report && !some, 2);
    if (some && !related) related = is_related_to_side(me, st, false_branch, target, report, 2);
    else if (related && !some) related &= is_related_to_side(me, st, false_branch, target, report, 2);
    return related;
}

static Ternary structured_related_members(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report, bool use_variances
);

// An intersection with its instantiable members replaced by their base constraints (unknown if none), null if it has none
[[gnu::cold]] static Ty const* hoisted_constraints(Checker* me, Ty const* intersection) {
    TyList types = intersection->composite.types;
    Ty const** parts = malloc(sizeof(Ty const*) * types.len);
    panic_if(!parts, "out of memory");
    bool changed = false;
    for (u32 i = 0; i < types.len; i++) {
        Ty const* t = types.data[i];
        parts[i] = t;
        if (t->kind == TY_TYPE_PARAM || (t->flags & TYF_DEFERRED)) {
            Ty const* constraint = base_constraint(me, t, 0);
            parts[i] = constraint ? constraint : ty_intrinsic(me, TY_UNKNOWN_);
            changed = true;
        }
    }
    Ty const* ret = changed ? ty_intersection(me, parts, types.len, nullptr, (TyList){0}) : nullptr;
    free(parts);
    return ret;
}

// Whether two instantiations of one generic type alias relate by its variances first (see structured_related_impl)
static bool alias_variance_applies(Checker* me, RelationState const* st, Ty const* source, Ty const* target, bool use_variances) {
    return ((source->flags & TYF_OBJECT) || source->kind == TY_CONDITIONAL) && source->alias && source->alias_args.len &&
        source->alias == target->alias && source->alias_args.len == target->alias_args.len &&
        source->kind != TY_REFERENCE && target->kind != TY_REFERENCE &&
        !is_alias_marker(me, source) && !is_alias_marker(me, target) && use_variances &&
        !(st->alias_fallback_source == source && st->alias_fallback_target == target);
}

// `use_variances` false: two instantiations of the same generic are compared by their members, as for an explanation
static Ternary structured_related_impl(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report, bool use_variances
) {
    bool saved_source = st->intersection_source, saved_member = st->member_of_intersection_source;
    st->member_of_intersection_source = st->intersection_source;
    st->intersection_source = false;
    Ternary ret = structured_related_members(me, st, source, target, report, use_variances);
    st->intersection_source = saved_source;
    st->member_of_intersection_source = saved_member;
    return ret;
}

static Ternary structured_related_members(
    Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report, bool use_variances
) {
    if (st->relation == TY_RELATION_IDENTITY) return identity_structured(me, st, source, target);
    bool comparable = st->relation == TY_RELATION_COMPARABLE;

    // Unions and intersections
    if (source->kind == TY_UNION) {
        if (target->kind == TY_UNION) {
            // tsc's unionOrIntersectionRelatedTo: `A & B` (distributed) is related to the named union A, and a named
            // union to a union of unions it is written in
            Ty const* so = source->composite.origin;
            if (so && so->kind == TY_INTERSECTION && target->alias) {
                for (u32 i = 0; i < so->composite.types.len; i++) if (so->composite.types.data[i] == target) return TERNARY_TRUE;
            }
            Ty const* to = target->composite.origin;
            if (to && to->kind == TY_UNION && source->alias) {
                for (u32 i = 0; i < to->composite.types.len; i++) if (to->composite.types.data[i] == source) return TERNARY_TRUE;
            }
        }
        if (comparable) {
            if (union_has_member(source, target)) return TERNARY_TRUE; // (tsc's someTypeRelatedToType)
            return some_related(me, st, source->composite.types, target,
                report && !(source->flags & TYF_BOOLEAN) && !ty_is_enum_union(source));
        }
        // (Not about the members of `boolean` or an enum, primitives: tsc's unionOrIntersectionRelatedTo)
        return each_related(me, st, source->composite.types, target, report && !(source->flags & TYF_BOOLEAN) && !ty_is_enum_union(source));
    }
    bool source_is_generic = source->kind == TY_TYPE_PARAM || (source->flags & TYF_DEFERRED) != 0;
    // A fresh empty object literal is never a subtype target of something non-empty, so `c || {}` reduces to c's type
    // rather than to whichever of two mutual subtypes comes first (tsc's structuredTypeRelatedToWorker)
    // (Both subtype relations: inference's common supertype asks the plain one, `assign({}, options)`. The source as it
    // apparently is: `object`, and `unknown` without strictNullChecks, are `{}` there)
    if ((st->relation == TY_RELATION_STRICT_SUBTYPE || st->relation == TY_RELATION_SUBTYPE) && !source_is_generic &&
        ty_is_fresh_object(target) && is_empty_object_type(me, target) && !is_empty_object_type(me, apparent_type(me, source))
    ) {
        return TERNARY_FALSE;
    }
    if (target->kind == TY_UNION) {
        // A generic source can still be related through its constraint, further down. The members of the union don't
        // check excess properties, the union as a whole did (tsc relates the regular object literal)
        isize_t saved = vec_ty_report_line_size(&st->lines);
        // (A member of an intersection source relates to the union's members as one still: tsc's typeRelatedToSomeType
        // passes the intersectionState on, so `{}` of `{ a: number } & {}` has no inferable index signature there either)
        bool saved_source = st->intersection_source;
        st->intersection_source = st->member_of_intersection_source;
        Ternary related = related_to_some(me, st, ty_regular_object_literal(me, source), target, report && !source_is_generic);
        st->intersection_source = saved_source;
        if (!related && ((source->flags & TYF_OBJECT) || source->kind == TY_INTERSECTION)) {
            related = related_to_discriminated(me, st, source, target);
            if (related) report_truncate(st, saved);
        }
        // An instantiation of a generic alias whose union instantiation is the target is still related by the alias's
        // variances (tsc's structuredTypeRelatedToWorker goes on to them after unionOrIntersectionRelatedTo for an
        // object source): `Mutable<E>` to `Mutable<El>`, a union of `Mutable` of each member, with `E extends El`
        if (!related && alias_variance_applies(me, st, source, target, use_variances)) {
            Variances const* variances = get_alias_variances(me, source->alias);
            bool unreliable;
            if (!variances) return TERNARY_MAYBE;
            Ternary by_alias = type_args_related(me, st, variances, source->alias_args, target->alias_args, false, &unreliable);
            if (by_alias) {
                report_truncate(st, saved);
                return by_alias;
            }
        }
        if (related || (!source_is_generic && source->kind != TY_INTERSECTION)) return related;
    }
    if (target->kind == TY_INTERSECTION) {
        isize_t saved = vec_ty_report_line_size(&st->lines);
        Ternary result = target_each_related(me, st, source, target->composite.types, report);
        // A conditional source whose members aren't all related may still be related as a whole through its default
        // or distributive constraint (tsc's structuredTypeRelatedToWorker goes on for an instantiable source, with the
        // whole target): `Emitted<L>` with `L extends { e: any }` (any) to `Emitted<L> & (T extends "*" ? unknown :
        // { type: T })`; vue's `shallowRef(r)`, whose branches are each a ShallowRef<T> but not the weak
        // `{ [ShallowRefMarker]?: true }` alone
        if (!result && source->kind == TY_CONDITIONAL) {
            bool saved_target = st->intersection_target;
            st->intersection_target = false;
            Ternary related = conditional_source_related(me, st, source, target, false);
            st->intersection_target = saved_target;
            if (related) {
                report_truncate(st, saved);
                return related;
            }
        }
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
            // (And a fresh object literal's properties against the index signatures they combine to)
            if (result && ty_is_fresh_object(source) && source->kind == TY_ANONYMOUS && source->anonymous.object_literal) {
                result &= index_signatures_related(me, st, source, target, false, report);
            }
        }
        return result;
    }
    if (source->kind == TY_INTERSECTION && comparable && (target->flags & (TYF_STRING_LIKE | TYF_NUMBER | TYF_BIGINT | TYF_BOOLEAN |
        TYF_LITERAL | TYF_ENUM | TYF_ENUM_LITERAL | TYF_ES_SYMBOL_LIKE | TYF_VOID | TYF_NULLABLE))
    ) {
        // Comparing with a primitive, the members' constraints take their place (tsc's unionOrIntersectionRelatedTo:
        // `T & number` with `T extends string | number` is number, which "hello" isn't comparable to)
        Ty const* reduced = hoisted_constraints(me, source);
        if (reduced) {
            if (reduced->flags & TYF_NEVER) return TERNARY_FALSE;
            if (reduced->kind != TY_INTERSECTION) {
                Ternary related = is_related_to_side(me, st, reduced, target, false, 2);
                return related ? related : is_related_to_side(me, st, target, reduced, false, 2);
            }
            source = reduced;
        }
    }
    if (source->kind == TY_INTERSECTION) {
        bool saved_source = st->intersection_source;
        st->intersection_source = true;
        Ternary related = some_related(me, st, source->composite.types, target, false);
        st->intersection_source = saved_source;
        // The members' constraints together may be related (tsc's getEffectiveConstraintOfIntersection): `T & {}` with
        // `T extends Node | undefined` is a Node
        // (tsc relates the intersection normalized, its members simplified, in the first place: `Partial<T>[K] & {}` is
        // `T[K] & {}`. Only once the members as they are failed here, which keeps the common case cheap)
        Ty const* normalized = related ? source : normalized_intersection(me, source, false);
        if (normalized != source) related = is_related_to_side(me, st, normalized, target, false, 2);
        Ty const* constraint = related ? nullptr : effective_intersection_constraint(me, source, target->kind == TY_UNION);
        if (constraint) related = is_related_to_side(me, st, constraint, target, false, 2);
        if (related) {
            // A member being related isn't enough for the target's optional properties, which the intersection as a
            // whole may have with another type: `{ a: null } & { b: string }` to `{ a?: number, b: string }` (tsc's
            // extra property check in structuredTypeRelatedTo)
            bool member_is_target = false;
            for (u32 i = 0; i < source->composite.types.len; i++) member_is_target |= source->composite.types.data[i] == target;
            if (!member_is_target && (target->flags & TYF_OBJECT) && !ty_is_generic_object(me, target) &&
                !(target->kind == TY_REFERENCE && is_array_target(me, target->reference.target)) && !is_tuple(target)
            ) {
                related &= props_related_impl(me, st, apparent_type(me, source), target, report, false, true);
            }
            return related;
        }
        // Otherwise the members may still add up to the target, property by property (below)
    }
    // Two instantiations of one generic type alias relate by their type arguments with the alias's variances (tsc
    // limits it to object and conditional types, whose aliases don't depend on what was checked first). If that
    // fails with an unreliable variance, structurally
    // Not type references: tsc's getNormalizedType makes a deferred one a plain reference first, which has no alias
    // (`type A<T> = Box<T extends string ? C<T> : never>`: A<symbol | string> is Box<C<string>>, which A<string> is too)
    if (alias_variance_applies(me, st, source, target, use_variances)) {
        Variances const* variances = get_alias_variances(me, source->alias);
        if (!variances) return TERNARY_MAYBE;
        bool unreliable;
        isize_t saved = vec_ty_report_line_size(&st->lines);
        Ternary related = type_args_related(me, st, variances, source->alias_args, target->alias_args, false, &unreliable);
        bool void_fallback = !related && !unreliable && covariant_void_argument(variances, target->alias_args);
        if (!related && report && !unreliable && !void_fallback) {
            report_truncate(st, saved);
            related = type_args_related(me, st, variances, source->alias_args, target->alias_args, true, &unreliable);
            bool invariant = false;
            for (u32 i = 0; i < variances->len; i++) {
                invariant |= (variances->data[i] & (VARIANCE_BIVARIANT | VARIANCE_INDEPENDENT)) == VARIANCE_INVARIANT;
            }
            // Two instantiations of one generic class or interface decide by their own variances (tsc's relateVariances
            // leaves the relation to the rest of structuredTypeRelatedTo, whose reference case returns what it finds)
            bool same_reference = source->kind == TY_REFERENCE && target->kind == TY_REFERENCE &&
                source->reference.target == target->reference.target && !is_tuple(source);
            if (!related && invariant && same_reference) {
                report_truncate(st, saved);
                Ty const* saved_source = st->alias_fallback_source, *saved_target = st->alias_fallback_target;
                st->alias_fallback_source = source;
                st->alias_fallback_target = target;
                related = structured_related_impl(me, st, source, target, true, true);
                st->alias_fallback_source = saved_source;
                st->alias_fallback_target = saved_target;
                return related;
            }
            if (!related && invariant) {
                // Explained by the members, as for references below
                VecTyReportLine variance_lines = {0};
                while (vec_ty_report_line_size(&st->lines) > saved) {
                    vec_ty_report_line_push(&variance_lines, vec_ty_report_line_pull(&st->lines));
                }
                u32 override_saved = st->override_next_head;
                if (structured_related_impl(me, st, source, target, true, false)) {
                    report_truncate(st, saved);
                    st->override_next_head = override_saved;
                    while (vec_ty_report_line_size(&variance_lines)) {
                        vec_ty_report_line_push(&st->lines, vec_ty_report_line_pull(&variance_lines));
                    }
                }
                vec_ty_report_line_drop(&variance_lines);
                return TERNARY_FALSE;
            }
        }
        if (related || (!unreliable && !void_fallback)) return related;
        report_truncate(st, saved);
        if (void_fallback && report) {
            type_args_related(me, st, variances, source->alias_args, target->alias_args, true, &unreliable);
            report = false;
        }
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
    switch ((TyKind)target->kind) {
        case TY_TYPE_PARAM: {
            // `{ [P in Q]: X }` is related to T if keyof T is related to Q and X to T[P] (`Mutable<T>` to T)
            // (Any mapped type, tsc's ObjectFlags.Mapped: one whose keys are known too, `{ [P in keyof (T & Record<any,
            // any>)]: ... }` with keys any)
            MappedInfo const* mapped = mapped_info_of(me, source, nullptr);
            if (mapped && !mapped->name_type && mapped->optional <= 0 &&
                is_related_to(me, st, ty_keyof(me, target), mapped->constraint, false)
            ) {
                Ternary related = is_related_to(me, st, ty_mapped_template(me, mapped), ty_indexed(me, target, mapped->type_param), report);
                if (related) return related;
            }
            if (comparable && source->kind == TY_TYPE_PARAM) {
                // Two type parameters are comparable only if one extends the other (comparability is mostly
                // bidirectional: the other way around is tried too)
                Ty const* constraint = ty_param_constraint(me, source);
                for (u32 depth = 0; constraint && constraint != source && depth < 16; depth++) {
                    bool has_param = constraint->kind == TY_TYPE_PARAM;
                    for (u32 i = 0; constraint->kind == TY_UNION && i < constraint->composite.types.len; i++) {
                        has_param |= constraint->composite.types.data[i]->kind == TY_TYPE_PARAM;
                    }
                    if (!has_param) break;
                    Ternary related = is_related_to(me, st, constraint, target, false);
                    if (related) return related;
                    if (constraint->kind != TY_TYPE_PARAM) break;
                    constraint = ty_param_constraint(me, constraint);
                }
                return TERNARY_FALSE;
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
            // A tuple with variadic elements: its known keys (tsc's getKnownKeysOfTupleType)
            if (is_tuple(operand)) {
                TupleShape const* shape = operand->reference.target->declared.tuple;
                TyBuf keys = {0};
                for (u32 i = 0; i < shape->arity && !(shape->flags[i] & TUPLE_ELEMENT_REST); i++) {
                    char digits[16];
                    snprintf(digits, sizeof digits, "%u", i);
                    ty_buf_push(&keys, ty_string_literal(me, csview_from(digits)));
                }
                ty_buf_push(&keys, ty_keyof(me, tuple_as_array(me, operand)));
                Ty const* known = ty_union(me, keys.data, keys.len, nullptr, (TyList){0});
                ty_buf_drop(&keys);
                Ternary related = is_related_to(me, st, source, known, report);
                if (related) return related;
            }
            // A union kept as `keyof` it because an instantiation may reduce it (ty_keyof()): keyof each member,
            // without that check (tsc's IndexFlags.NoReducibleCheck)
            if (operand->kind == TY_UNION) {
                TyBuf parts = {0};
                for (u32 i = 0; i < operand->composite.types.len; i++) ty_buf_push(&parts, ty_keyof(me, operand->composite.types.data[i]));
                Ty const* keys = ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
                ty_buf_drop(&parts);
                if (keys != target) {
                    Ternary related = is_related_to(me, st, source, keys, report);
                    if (related == TERNARY_TRUE) return related;
                }
            }
            // Through the simplified operand, or its constraint (tsc's getSimplifiedTypeOrConstraint)
            Ty const* constraint = simplified_or_constraint(me, operand);
            if (constraint && constraint != operand) {
                Ty const* keys = ty_keyof(me, constraint);
                if (keys != target) {
                    Ternary related = is_related_to(me, st, source, keys, report);
                    if (related == TERNARY_TRUE) return related;
                }
            } else if (!constraint && operand->kind == TY_ANONYMOUS && ty_members(me, operand)->mapped &&
                ty_is_generic_index(ty_members(me, operand)->mapped->constraint)
            ) {
                // A generic mapped type's keys are at least its `as` type, or else its constraint, and with `as` over
                // `keyof X` the `as` type of X's apparent keys (tsc's getApparentMappedTypeKeys)
                MappedInfo const* mapped = ty_members(me, operand)->mapped;
                Ty const* keys = mapped->constraint;
                if (mapped->name_type) {
                    // (The `as` type with the key parameter as the constraint: tsc's getIndexTypeForMappedType,
                    // `get${P}` over K is `get${K}`)
                    TyMapper const* key_mapper = ty_mapper_new(me, (TyList){ .data = &mapped->type_param, .len = 1 },
                        (TyList){ .data = &mapped->constraint, .len = 1 });
                    keys = ty_instantiate(me, mapped->name_type, key_mapper);
                }
                if (mapped->name_type && mapped->keyof_constraint && mapped->modifiers_type) {
                    Ty const* both[2] = { apparent_mapped_type_keys(me, mapped), mapped->name_type };
                    keys = ty_union(me, both, 2, nullptr, (TyList){0});
                }
                Ternary related = is_related_to(me, st, source, keys, report);
                if (related == TERNARY_TRUE) return related;
            }
        }   break;

        case TY_INDEXED: {
            // S[K] to T[J] by S to T and K to J first. Reporting, the shorter explanation of that and of the one through
            // the constraint below is kept, this one if they're as long (tsc's originalErrorInfo, countMessageChainBreadth)
            isize_t start = vec_ty_report_line_size(&st->lines);
            VecTyReportLine original = {0};
            bool have_original = false;
            if (source->kind == TY_INDEXED) {
                Ternary related = is_related_to(me, st, source->indexed.object, target->indexed.object, report);
                if (related) related &= is_related_to(me, st, source->indexed.index, target->indexed.index, report);
                if (related) return related;
                if (report && !is_subtype_relation(st->relation)) {
                    have_original = true;
                    while (vec_ty_report_line_size(&st->lines) > start) vec_ty_report_line_push(&original, vec_ty_report_line_pull(&st->lines));
                }
            }
            // (tsc: only for assignability and comparability, not the subtype relations)
            Ty const* constraint = is_subtype_relation(st->relation) ? nullptr : write_constraint_of_indexed(me, target);
            if (constraint) {
                Ternary related = is_related_to(me, st, source, constraint, report);
                if (related) {
                    vec_ty_report_line_drop(&original);
                    return related;
                }
                isize_t added = vec_ty_report_line_size(&st->lines) - start;
                if (have_original && added && vec_ty_report_line_size(&original) <= added) report_truncate(st, start);
                else if (added) have_original = false;
            }
            // (Back in their order: `original` holds them outermost first)
            while (have_original && vec_ty_report_line_size(&original)) vec_ty_report_line_push(&st->lines, vec_ty_report_line_pull(&original));
            vec_ty_report_line_drop(&original);
        }   break;

        case TY_CONDITIONAL: {
            // Ten instantiations of this conditional type on the target stack: an infinitely expanding recursive
            // conditional type, assumed related (tsc's isDeeplyNestedType(target, targetStack, targetDepth, 10))
            if (st->target_depth && relation_deeply_nested(me, target, st->target_stack, st->target_entries, &st->target_entries_valid, st->target_depth - 1, 10, false)) return TERNARY_MAYBE;
            // Identical extends types, check types related one way or the other, and related branches (tsc's rule for
            // two conditional types). The source's own `infer` type parameters are inferred from the target's extends
            // type first: `HM extends Box<infer H> ? H : never` against another such alias
            if (source->kind == TY_CONDITIONAL) {
                Ty const* source_extends = source->conditional.extends;
                TyMapper const* mapper = nullptr;
                TyList infers = ty_conditional_own_infers(me, source->conditional.root ? source->conditional.root : source);
                if (infers.len) {
                    Inference inf;
                    inference_init(&inf, infers);
                    infer_types(me, &inf, target->conditional.extends, source_extends, INFER_PRIORITY_NORMAL);
                    Ty const** results = arena_new_n(&my arena, Ty const*, inf.len);
                    for (u32 i = 0; i < inf.len; i++) {
                        results[i] = inference_conditional_result(me, &inf, i);
                        if (!results[i]) results[i] = ty_intrinsic(me, TY_UNKNOWN_);
                    }
                    mapper = ty_mapper_new(me, infers, (TyList){ .data = results, .len = inf.len });
                    inference_drop(&inf);
                    source_extends = ty_instantiate(me, source_extends, mapper);
                }
                if (ty_is_related(me, source_extends, target->conditional.extends, TY_RELATION_IDENTITY)) {
                    Ternary related = is_related_to(me, st, source->conditional.check, target->conditional.check, false);
                    if (!related) related = is_related_to(me, st, target->conditional.check, source->conditional.check, false);
                    if (related) {
                        Ty const* source_true = ty_instantiate(me, ty_conditional_branch(me, source, true), mapper);
                        related &= is_related_to(me, st, source_true, ty_conditional_branch(me, target, true), false);
                    }
                    if (related) {
                        Ty const* source_false = ty_conditional_branch(me, source, false);
                        related &= is_related_to(me, st, source_false, ty_conditional_branch(me, target, false), false);
                    }
                    if (related) return related;
                }
            }
            // Related to both branches is related to whichever gets picked. Not if a branch depends on what the check
            // type distributes over or infers, nor between two instantiations of one conditional type
            Ty const* target_root = target->conditional.root ? target->conditional.root : target;
            Ty const* source_root = source->kind != TY_CONDITIONAL ? nullptr :
                source->conditional.root ? source->conditional.root : source;
            if (source_root != target_root && !ty_conditional_own_infers(me, target_root).len &&
                !ty_conditional_distribution_dependent(me, target)
            ) {
                // A branch that can't be picked doesn't count: not the true one when not even the permissive
                // instantiations (type variables the wildcard) are assignable (tsc's skipTrue), not the false one when
                // the restrictive ones are (skipFalse, asked only when the false branch isn't related anyway)
                // (The check type as tsc's ConditionalType.checkType has it: with the substitution an outer true branch
                // gave it, which an instance resolves without: `T[K] extends unknown[] ? ...` inside `K extends keyof T`)
                Ty const* check = target->conditional.check;
                Ty const* root_check = target_root->conditional.check;
                if (root_check != check && root_check->kind != TY_TYPE_PARAM && target->conditional.mapper &&
                    ty_actual_type_variable(me, root_check) != root_check
                ) {
                    check = ty_instantiate(me, root_check, target->conditional.mapper);
                }
                Ty const* extends = target->conditional.extends;
                bool skip_true = ty_definitely_unassignable(me, check, extends);
                // (Only the target is pushed on the deep nesting stacks: tsc's RecursionFlags.Target)
                Ternary related = skip_true ? TERNARY_TRUE :
                    is_related_to_side(me, st, source, ty_conditional_branch(me, target, true), false, 1);
                if (related) {
                    Ternary related_false = is_related_to_side(me, st, source, ty_conditional_branch(me, target, false), false, 1);
                    if (!related_false && !skip_true && ty_definitely_assignable(me, check, extends)) related_false = TERNARY_TRUE;
                    related &= related_false;
                }
                if (related) return related;
            }
        }   break;

        case TY_TEMPLATE_LITERAL:
            if (source->kind == TY_TEMPLATE_LITERAL) {
                if (comparable) return ty_templates_definitely_unrelated(source, target) ? TERNARY_FALSE : TERNARY_TRUE;
                // `foo-${number}` is related to `foo-${string}` though number isn't related to string
                report_variance(me, source, VARIANCE_UNRELIABLE);
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
    // An empty object type is related to any mapped type with `?` (tsc's isPartialMappedType), generic or not:
    // `object` is a `Partial<Record<string, X>>`
    MappedInfo const* target_partial = mapped_info_of(me, target, nullptr);
    if (target_partial && target_partial->optional > 0 && st->relation != TY_RELATION_SUBTYPE &&
        st->relation != TY_RELATION_STRICT_SUBTYPE && is_empty_object_type(me, source)
    ) {
        return TERNARY_TRUE;
    }
    if (target_mapped && source->kind == TY_ANONYMOUS && (source->flags & TYF_OBJECT) && ty_members(me, source)->mapped) {
        // Two mapped types whose keys aren't known yet: by constraint and template (tsc's mappedTypeRelatedTo)
        return mapped_related(me, st, ty_members(me, source)->mapped, target_mapped, report);
    }
    if (target_mapped && !my variance_depth) {
        isize_t saved = vec_ty_report_line_size(&st->lines);
        Ternary related = generic_mapped_target_related(me, st, source, target_mapped, report);
        // (Why S[K] isn't the template says nothing: tsc resets the error info, resetErrorInfo(saveErrorInfo))
        if (!related) report_truncate(st, saved);
        // HACK: a generic source falls through to the member-by-member comparison, which a mapped type without
        // members passes: it stands in for tsc's rules between two generic mapped types (mappedTypeRelatedTo), a
        // homomorphic Readonly<T> and T, and the like, which don't exist here yet
        if (related || !(source->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return related;
        // (tsc's structuredTypeRelatedToWorker returns False for a generic mapped target unless the source is a mapped
        // type too, whatever else it is; generic_mapped_target_related() isn't complete enough to rely on that: zod's
        // SortItem<output<T>> against its Flatten-like `{ [K in keyof A | keyof B]: (A & B)[K] }` needs the fallback.
        // Only an object type without properties and an array or tuple are done here: `W<T>[]` isn't
        // `{ [K in keyof T["a"]]: X }`. Variadic tuples still fall through: `[...Foo<T>]` of a spread argument, where
        // tsc has `Foo<T>` (getSpreadArgumentType), passes restTypeRetainsMappyness that way)
        if ((source->flags & TYF_OBJECT) && source->kind != TY_INTERSECTION && !ty_members(me, source)->mapped &&
            (!ty_members(me, source)->prop_count || ty_is_array(me, source) || (ty_is_tuple(source) && !has_variadic_element(me, source)))
        ) {
            return related;
        }
    }

    // Generic sources
    switch ((TyKind)source->kind) {
        case TY_TYPE_PARAM: {
            Ty const* constraint = ty_param_constraint(me, source);
            if (!constraint || constraint == source) {
                // (Comparable to a deferred type, as unknown is: `T` and `GetDecoderResult<D>`; to another type parameter
                // not, the carve-out of tsc's structuredTypeRelatedToWorker for type parameter targets)
                if (target->kind == TY_TYPE_PARAM ||
                    ((target->flags & TYF_DEFERRED) && st->relation != TY_RELATION_COMPARABLE)
                ) {
                    return TERNARY_FALSE;
                }
                // As `unknown` (tsc's getConstraintOfType(source) || unknownType): comparable to anything, and without
                // strictNullChecks its apparent type `{}` against object types (apparent_type()), though not as a `{}`
                // that any mapped type with `?` takes
                return is_related_to(me, st, ty_intrinsic(me, TY_UNKNOWN_), target, false);
            }
            // The constraint as it is first, then with the type parameter as its `this` (tsc: raw `this` types from
            // base classes need it, subclassWithPolymorphicThisIsAssignable). From one type parameter to another, or from
            // an `unknown` one, the constraint's failure doesn't explain anything
            bool report_constraint = report && target->kind != TY_TYPE_PARAM && constraint != ty_intrinsic(me, TY_UNKNOWN_);
            Ty const* with_this = ty_with_this_arg(me, constraint, source);
            if (with_this == constraint) return is_related_to_side(me, st, constraint, target, report_constraint, 2);
            Ternary related = is_related_to_side(me, st, constraint, target, false, 2);
            if (related) return related;
            return is_related_to_side(me, st, with_this, target, report_constraint, 2);
        }

        case TY_KEYOF: {
            Ty const* all[3] = { ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_ES_SYMBOL) };
            Ty const* operand = source->keyof.operand;
            // keyof a generic mapped type: also its keys, the `as` type (or the constraint) (tsc's isDeferredMappedIndex)
            MappedInfo const* mapped = operand->kind == TY_ANONYMOUS && (operand->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) ?
                ty_members(me, operand)->mapped : nullptr;
            if (!mapped) return is_related_to(me, st, ty_union(me, all, 3, nullptr, (TyList){0}), target, report);
            Ternary related = is_related_to(me, st, ty_union(me, all, 3, nullptr, (TyList){0}), target, false);
            if (related) return related;
            Ty const* keys = mapped->name_type && mapped->keyof_constraint && mapped->modifiers_type ?
                apparent_mapped_type_keys(me, mapped) : mapped->name_type ? mapped->name_type : mapped->constraint;
            return is_related_to(me, st, keys, target, report);
        }

        case TY_INDEXED: {
            // Two indexed access types relate by their parts only (the target's case above), not through the source's
            // constraint: `S[K]` isn't `U[K]` because `S[keyof T]` happens to be (tsc's structuredTypeRelatedToWorker)
            if (target->kind == TY_INDEXED) break;
            // (tsc: `getConstraintOfType(source) || unknownType`, which is comparable to anything; not elaborated)
            Ty const* constraint = constraint_of_indexed(me, source);
            if (!constraint) return is_related_to(me, st, ty_intrinsic(me, TY_UNKNOWN_), target, false);
            // `{ [P in K]: E }[X]` was E with X for P; `{ [P in K]: E }[C]`, with C the constraint of X, may be related
            // where that isn't (tsc's isMappedTypeGenericIndexedAccess branch)
            if (!mapped_generic_indexed_access(me, source)) return is_related_to_side(me, st, constraint, target, report, 2);
            Ternary related = is_related_to_side(me, st, constraint, target, false, 2);
            if (related) return related;
            Ty const* index_constraint = simplified_or_constraint(me, source->indexed.index);
            Ty const* whole = index_constraint && index_constraint != source->indexed.index ?
                indexed_or_null(me, source->indexed.object, index_constraint) : nullptr;
            if (whole && whole != source) {
                related = is_related_to_side(me, st, whole, target, false, 2);
                if (related) return related;
            }
            return is_related_to_side(me, st, constraint, target, report, 2);
        }

        case TY_CONDITIONAL: {
            // (Its constraints relate to an intersection target's member without IntersectionState.Target: tsc passes
            // no intersectionState there, so a weak member is checked for common properties)
            bool saved = st->intersection_target;
            st->intersection_target = false;
            Ternary related = conditional_source_related(me, st, source, target, report);
            st->intersection_target = saved;
            return related;
        }

        case TY_TEMPLATE_LITERAL:
            if (!(target->flags & (TYF_OBJECT | TYF_TEMPLATE_LITERAL))) {
                Ty const* constraint = base_constraint(me, source, 0);
                if (constraint && constraint != source) {
                    Ternary related = is_related_to_side(me, st, constraint, target, report, 2);
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
                    Ternary related = is_related_to_side(me, st, constraint, target, report, 2);
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
    if (source_is_tuple && target_is_tuple) {
        // A generic tuple against a non-generic one relates as its base constraint (tsc's structuredTypeRelatedToWorker,
        // isGenericTupleType(source)): `[S, ...T]` with `T extends readonly X[]` as `[S, ...X[]]`
        if (is_tuple(source) && ty_is_generic_tuple(me, source) && !(is_tuple(target) && ty_is_generic_tuple(me, target))) {
            Ty const* constraint = base_constraint(me, source, 0);
            if (constraint && constraint != source) return is_related_to_side(me, st, constraint, target, report, 2);
        }
        return tuples_related(me, st, source, target, report);
    }
    if (target_is_tuple && source->kind == TY_ANONYMOUS) {
        // A homomorphic mapped type over a type parameter constrained to a tuple is a tuple (its apparent type)
        Ty const* apparent = apparent_type(me, source);
        if (apparent != source && (is_tuple(apparent) || is_empty_tuple(apparent))) {
            return tuples_related(me, st, apparent, target, report);
        }
    }
    if (target_is_tuple) {
        TupleShape const* shape = is_empty_tuple(target) ? target->declared.tuple : target->reference.target->declared.tuple;
        if (source->kind == TY_REFERENCE && is_array_target(me, source->reference.target)) {
            // Against a tuple with a rest element, as the tuple `[...X[]]`, element by element (tsc's
            // propertiesRelatedTo takes an array source as one rest element)
            bool has_rest = false;
            for (u32 i = 0; i < shape->arity; i++) has_rest |= (shape->flags[i] & TUPLE_ELEMENT_REST) != 0;
            if (has_rest) {
                unsigned char rest_flag = TUPLE_ELEMENT_REST;
                TupleShape as_tuple = { .arity = 1, .flags = &rest_flag, .readonly = source->reference.target == my readonly_array_target };
                return tuple_shapes_related(me, st, &as_tuple, (TyList){ .data = &source, .len = 1 }, target, report);
            }
            u32 min = 0;
            for (u32 i = 0; i < shape->arity; i++) {
                if (!(shape->flags[i] & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST))) min = i + 1;
            }
            // (A readonly array to a mutable tuple fails first, TS4104's: tsc's propertiesRelatedTo)
            bool readonly_to_mutable = source->reference.target == my readonly_array_target && !shape->readonly;
            bool target_rest = false;
            for (u32 i = 0; i < shape->arity; i++) target_rest |= (shape->flags[i] & TUPLE_ELEMENT_REST) != 0;
            if (report && !readonly_to_mutable && min) {
                report_line(st, 2620, cstr_d("Target requires ", min, " element(s) but source may have fewer."));
            } else if (report && !readonly_to_mutable && !target_rest) {
                report_line(st, 2621, cstr_d("Target allows only ", shape->arity, " element(s) but source may have more."));
            }
            return TERNARY_FALSE;
        }
        // Anything else is compared structurally, unless the tuple has a variable length (tsc's propertiesRelatedTo):
        // `interface I extends [string, number] { x: string }` is one
        for (u32 i = 0; i < shape->arity; i++) {
            if (shape->flags[i] & TUPLE_ELEMENT_REST) return TERNARY_FALSE;
        }
    }
    // (As the array it extends against generic interfaces and classes, where variances help; structurally, with its
    // element properties, against anything else: `{ 0: number }`, `Pick<[string, ...string[]], ...>`)
    // (Not against an interface that declares element properties, `interface tup { 0: X; 1: Y }`, generic or not:
    // `interface KeyValuePair<K, V> extends Array<K | V> { 0: K; 1: V }`)
    if (source_is_tuple && (target->kind == TY_REFERENCE || target->kind == TY_DECLARED) &&
        ((target->kind == TY_REFERENCE && is_array_target(me, target->reference.target)) ||
            (!has_literal_length(me, target) && !has_numeric_props(me, target)))
    ) {
        source = tuple_as_array(me, source);
    }

    // A class or interface with a `this` type is a reference to itself (tsc: its declared type is one)
    bool source_self = source->kind == TY_DECLARED && source->declared.this_param;
    bool target_self = target->kind == TY_DECLARED && target->declared.this_param;
    if ((source->kind == TY_REFERENCE || source_self) && (target->kind == TY_REFERENCE || target_self)) {
        Ty const* source_target = source_self ? source : source->reference.target;
        Ty const* target_target = target_self ? target : target->reference.target;
        if (source_target == target_target && use_variances && !is_check_marker(me, source) && !is_check_marker(me, target)) {
            // `[]` to `never[]` without strictNullChecks (tsc's isEmptyArrayLiteralType)
            if (!source_self && is_array_target(me, source_target) && !compiler_options()->strict_null_checks &&
                ty_reference_args(me, source).len == 1 && ty_reference_args(me, source).data[0] == ty_intrinsic(me, TY_UNDEFINED_WIDENING)
            ) {
                return TERNARY_TRUE;
            }
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
                bool void_fallback = !related && !unreliable && covariant_void_argument(variances, target_args);
                if (!related && report && !unreliable && !void_fallback) {
                    report_truncate(st, args_saved);
                    related = type_args_related(me, st, variances, source_args, target_args, true, &unreliable);
                    bool invariant = false;
                    for (u32 i = 0; i < variances->len; i++) {
                        invariant |= (variances->data[i] & (VARIANCE_BIVARIANT | VARIANCE_INDEPENDENT)) == VARIANCE_INVARIANT;
                    }
                    if (!related && invariant) {
                        // An invariant type argument is explained by the members that use it, if they do differ (tsc's
                        // structuredTypeRelatedToWorker: the variance error is kept only if they don't)
                        VecTyReportLine variance_lines = {0};
                        while (vec_ty_report_line_size(&st->lines) > args_saved) {
                            vec_ty_report_line_push(&variance_lines, vec_ty_report_line_pull(&st->lines));
                        }
                        u32 override_saved = st->override_next_head;
                        if (structured_related_impl(me, st, source, target, true, false)) {
                            report_truncate(st, args_saved);
                            st->override_next_head = override_saved;
                            while (vec_ty_report_line_size(&variance_lines)) {
                                vec_ty_report_line_push(&st->lines, vec_ty_report_line_pull(&variance_lines));
                            }
                        }
                        vec_ty_report_line_drop(&variance_lines);
                        return TERNARY_FALSE;
                    }
                }
                // The this-argument is one more covariant type argument, compared only when both have one (tsc's
                // typeArgumentsRelatedTo goes as far as the shorter list: a written reference `C<X>` has none). A
                // class or interface's own declared type stands for its own `this`
                Ty const* source_this = source->kind == TY_REFERENCE ? source->reference.this_arg : nullptr;
                Ty const* target_this = target->kind == TY_REFERENCE ? target->reference.this_arg : target;
                if (related && source_target->declared.this_param && source_this && target_this &&
                    (source_this != source || target_this != target) && source_this != target_this
                ) {
                    related &= is_related_to(me, st, source_this, target_this, report);
                }
                if (related || (!unreliable && !void_fallback)) return related;
                report_truncate(st, saved);
                if (void_fallback && report) {
                    type_args_related(me, st, variances, source_args, target_args, true, &unreliable);
                    report = false;
                }
            }
        } else if (is_array_target(me, source_target) && is_array_target(me, target_target)) {
            // HACK: Array<S> and ReadonlyArray<T> relate like their element types. It's what comparing the two
            // interfaces member by member comes to, without depending on how well generic methods compare yet
            // (TS4104 is report_relation_error()'s)
            if (source_target == my readonly_array_target) return TERNARY_FALSE;
            return is_related_to(me, st, ty_reference_args(me, source).data[0], ty_reference_args(me, target).data[0], report);
        }
    }

    // An intersection with a type variable whose constraint is a union is a union once apparent (tsc's
    // getApparentTypeOfIntersectionType distributes `(A | B) & X`), which tsc's structuredTypeRelatedToWorker doesn't
    // compare structurally: `T & { op: number }` with `T extends F | S` isn't assignable to `{ cp: number; op: number }`
    // unless a member is (getInferredType then falls back to the constraint)
    if (source->kind == TY_INTERSECTION && (target->flags & TYF_OBJECT) && intersection_apparent_is_union(me, source)) {
        return TERNARY_FALSE;
    }
    Ty const* apparent = source->kind == TY_INTERSECTION ? source : apparent_type(me, source);
    if (apparent != source && apparent->kind == TY_UNION && target->kind == TY_REFERENCE &&
        is_array_target(me, target->reference.target)
    ) {
        // A union of arrays and tuples (the apparent type of a homomorphic mapped type over them) against an array: by
        // element types, if every member is a tuple, or for a readonly array also an array (tsc's
        // structuredTypeRelatedToWorker, isReadonlyArrayType(target) ? everyType(source, isArrayOrTupleType) : ...)
        bool readonly_target = target->reference.target == my readonly_array_target;
        TyList members = apparent->composite.types;
        bool every = true;
        for (u32 i = 0; i < members.len && every; i++) {
            Ty const* member = members.data[i];
            TupleShape const* shape = ty_tuple_shape(member);
            bool array = member->kind == TY_REFERENCE && is_array_target(me, member->reference.target);
            every = readonly_target ? (shape || array) : (shape && !shape->readonly);
        }
        if (every) {
            Ty const** elements = malloc(sizeof(Ty const*) * members.len);
            panic_if(!elements, "out of memory");
            for (u32 i = 0; i < members.len; i++) {
                Ty const* member = members.data[i];
                Ty const* array = ty_tuple_shape(member) ? tuple_as_array(me, member) : member;
                elements[i] = ty_reference_args(me, array).data[0];
            }
            Ty const* element = ty_union(me, elements, members.len, nullptr, (TyList){0});
            free(elements);
            return is_related_to(me, st, element, ty_reference_args(me, target).data[0], report);
        }
    }
    if (!(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) return TERNARY_FALSE;
    if (apparent->kind == TY_INTERSECTION) {
        // As a whole: its signatures and index infos are those of its members together (resolve_intersection_members())
        Ternary result = props_related(me, st, apparent, target, report, false);
        if (result) result &= signatures_related(me, st, apparent, target, false, report);
        if (result) result &= signatures_related(me, st, apparent, target, true, report);
        if (result) result &= index_signatures_related(me, st, apparent, target, false, report);
        return result;
    }
    // Only a primitive's apparent type is kept quiet about (tsc's sourceIsPrimitive): `object` and `unknown` relate and
    // report as `{}`, a type parameter as its constraint
    u32 primitive = TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT | TYF_BIGINT_LITERAL | TYF_BOOLEAN |
        TYF_BOOLEAN_LITERAL | TYF_ENUM | TYF_ENUM_LITERAL | TYF_ES_SYMBOL_LIKE | TYF_VOID | TYF_NULLABLE;
    return object_related(me, st, source, apparent, target, report, apparent != source && (source->flags & primitive));
}

// Deep nesting (tsc's isDeeplyNestedType)

static Ty const* recursion_identity(Ty const* ty) {
    switch ((TyKind)ty->kind) {
        case TY_REFERENCE: return ty->reference.target;
        // (An uninstantiated type literal or function type is its own: tsc tracks object types by their symbol, and
        // `type S = { states?: Record<string, S> }` nests as S. Not object literal expressions)
        case TY_ANONYMOUS: return ty->anonymous.target ? ty->anonymous.target : ty->anonymous.object_literal ? nullptr : ty;
        case TY_CONDITIONAL: return ty->conditional.root;
        case TY_INDEXED: {
            Ty const* object = ty;
            while (object->kind == TY_INDEXED) object = object->indexed.object;
            return object;
        }
        default: return nullptr;
    }
}

// tsc's getMappedTargetWithSymbol: an instantiated mapped type stands for its modifiers type while that has a symbol
// (an object type, a type parameter, or an intersection with one of those), so `Id<T> = { [K in keyof T]: Id<T[K]> }`
// over nested type literals is a new identity at each level, and `Id2<Id2<T[K]>>` unwraps to the same literal as well
static bool has_symbol_for_recursion(Ty const* ty) {
    if (ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            Ty const* member = ty->composite.types.data[i];
            if (member->kind != TY_INTERSECTION && has_symbol_for_recursion(member)) return true;
        }
        return false;
    }
    if (ty->kind == TY_REFERENCE) return ty->reference.target->declared.kind != TY_DECLARED_TUPLE_TARGET;
    // (Not a type parameter, which tsc's has: a mapped type over one keeps the mapped type's identity)
    return ty->kind == TY_DECLARED || ty->kind == TY_ANONYMOUS;
}

// (tsc's ObjectFlags.InstantiatedMapped)
static inline bool is_mapped_instance(Ty const* ty) {
    return ty->kind == TY_ANONYMOUS && ty->anonymous.mapped && ty->anonymous.target;
}

static Ty const* mapped_target_with_symbol(Checker* me, Ty const* ty) {
    for (u32 depth = 0; depth < 64 && is_mapped_instance(ty); depth++) {
        MappedInfo const* mapped = ty_mapped_info_unexpanded(me, ty);
        if (!mapped || !mapped->modifiers_type || !has_symbol_for_recursion(mapped->modifiers_type)) break;
        ty = mapped->modifiers_type;
    }
    return ty;
}

// What `ty` stands for in deep nesting: an instantiated mapped type unwrapped (mapped_target_with_symbol())
static Ty const* recursion_key(Checker* me, Ty const* ty) {
    return is_mapped_instance(ty) ? mapped_target_with_symbol(me, ty) : ty;
}

// A stack entry's recursion identity, worked out once when it's pushed: an intersection's key (tagged with bit 0) has
// each of its members' (tsc's hasMatchingRecursionIdentity), anything else the one of its key or null
uintptr_t ty_recursion_entry(Checker* me, Ty const* ty) {
    Ty const* key = recursion_key(me, ty);
    return key->kind == TY_INTERSECTION ? (uintptr_t)key | 1 : (uintptr_t)recursion_identity(key);
}

static bool intersection_has_identity(Checker* me, Ty const* key, Ty const* identity) {
    for (u32 i = 0; i < key->composite.types.len; i++) {
        Ty const* member = recursion_key(me, key->composite.types.data[i]);
        if (member->kind == TY_INTERSECTION ? intersection_has_identity(me, member, identity) : recursion_identity(member) == identity) {
            return true;
        }
    }
    return false;
}

// Whether `identity` is on the stack at least `max` times counting `ty` (the type about to go on it, which has it),
// each newer than the one before. `entries` are the stack's ty_recursion_entry(), null to work them out
static bool is_deeply_nested_as(
    Checker* me, Ty const* identity, Ty const* ty, Ty const* const* stack, uintptr_t const* entries, u32 depth, u32 max
) {
    u32 count = 0;
    u32 last_id = 0;
    for (u32 i = 0; i < depth; i++) {
        uintptr_t entry = entries ? entries[i] : ty_recursion_entry(me, stack[i]);
        if (entry != (uintptr_t)identity && !((entry & 1) && intersection_has_identity(me, (Ty const*)(entry & ~(uintptr_t)1), identity))) {
            continue;
        }
        if (stack[i]->id >= last_id && ++count >= max) return true;
        last_id = stack[i]->id;
    }
    return ty->id >= last_id && ++count >= max;
}

// tsc's isDeeplyNestedType: `ty` (about to go on the stack, ty_recursion_entry() `entry`) and at least two types
// further up with the same recursion identity, each newer (a higher id) than the previous one
static bool is_deeply_nested_max(Checker* me, Ty const* ty, uintptr_t entry, Ty const* const* stack, uintptr_t const* entries, u32 depth, u32 max) {
    if (depth + 1 < max) return false;
    // An intersection is as deeply nested as its most deeply nested member: `C<T> & { a: 1 }` whose members return
    // `this & { a: 1 }` grows a new intersection at each level, with C<T> in each
    if (entry & 1) {
        Ty const* key = (Ty const*)(entry & ~(uintptr_t)1);
        for (u32 i = 0; i < key->composite.types.len; i++) {
            Ty const* member = recursion_key(me, key->composite.types.data[i]);
            Ty const* identity = member->kind != TY_INTERSECTION ? recursion_identity(member) : nullptr;
            if (identity && is_deeply_nested_as(me, identity, ty, stack, entries, depth, max)) return true;
        }
        return false;
    }
    return entry && is_deeply_nested_as(me, (Ty const*)entry, ty, stack, entries, depth, max);
}

// A relation's stacks: the entries below `depth` not up to date yet are worked out first (`valid` of them are)
static bool relation_deeply_nested(
    Checker* me, Ty const* ty, Ty const* const* stack, uintptr_t* entries, u32* valid, u32 depth, u32 max, bool pushing
) {
    if (depth + 1 < max) return false;
    for (u32 i = *valid; i < depth; i++) entries[i] = ty_recursion_entry(me, stack[i]);
    uintptr_t entry = ty_recursion_entry(me, ty);
    // (`pushing`: `ty` goes on the stack at `depth` next, its entry with it)
    if (pushing) {
        entries[depth] = entry;
        *valid = depth + 1;
    } else if (*valid < depth) {
        *valid = depth;
    }
    return is_deeply_nested_max(me, ty, entry, stack, entries, depth, max);
}

// (Inference's stacks are short: their entries are worked out as they're scanned)
bool ty_is_deeply_nested(Checker* me, Ty const* ty, Ty const* const* stack, u32 depth, u32 max) {
    if (depth + 1 < max) return false;
    return is_deeply_nested_max(me, ty, ty_recursion_entry(me, ty), stack, nullptr, depth, max);
}

// The weak type check of tsc's isRelatedTo, reported. Done once a pair isn't cached (the answer only depends on it)
static bool lacks_common_properties(Checker* me, RelationState* st, Ty const* source, Ty const* target);
static bool fails_common_properties(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    if (!lacks_common_properties(me, st, source, target)) return false;
    {
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
            st->head_reported = true;
            c_drop(cstr, &source_str, &target_str);
        }
        return true;
    }
}

// The maybe stack (RelationState.maybe_keys). Scanned while it's short (most relations stay below a few dozen pairs,
// and the index's allocations cost 0.5% of valibot), indexed from MAYBE_INDEXED_CAP on, with 2x the capacity in buckets
#define MAYBE_INDEXED_CAP 128

static u32 maybe_hash(RelationState const* st, u64 key) {
    return (u32)((key * 0x9E3779B97F4A7C15ull) >> 32) & (st->maybe_cap * 2 - 1);
}

static bool maybe_contains(RelationState const* st, u64 key) {
    if (!st->maybe_heads) {
        for (u32 i = 0; i < st->maybe_count; i++) {
            if (st->maybe_keys[i] == key) return true;
        }
        return false;
    }
    for (u32 i = st->maybe_heads[maybe_hash(st, key)]; i; i = st->maybe_next[i - 1]) {
        if (st->maybe_keys[i - 1] == key) return true;
    }
    return false;
}

static void maybe_push(RelationState* st, u64 key) {
    if (st->maybe_count == st->maybe_cap) {
        st->maybe_cap = st->maybe_cap ? st->maybe_cap * 2 : 32;
        st->maybe_keys = realloc(st->maybe_keys, sizeof(u64) * st->maybe_cap);
        panic_if(!st->maybe_keys, "out of memory");
        if (st->maybe_cap >= MAYBE_INDEXED_CAP) {
            st->maybe_next = realloc(st->maybe_next, sizeof(u32) * st->maybe_cap);
            free(st->maybe_heads);
            st->maybe_heads = calloc(st->maybe_cap * 2, sizeof(u32));
            panic_if(!st->maybe_next || !st->maybe_heads, "out of memory");
            for (u32 i = 0; i < st->maybe_count; i++) {
                u32 h = maybe_hash(st, st->maybe_keys[i]);
                st->maybe_next[i] = st->maybe_heads[h];
                st->maybe_heads[h] = i + 1;
            }
        }
    }
    st->maybe_keys[st->maybe_count++] = key;
    if (st->maybe_heads) {
        u32 h = maybe_hash(st, key);
        st->maybe_next[st->maybe_count - 1] = st->maybe_heads[h];
        st->maybe_heads[h] = st->maybe_count;
    }
}

static void maybe_truncate(RelationState* st, u32 len) {
    if (!st->maybe_heads) {
        st->maybe_count = len;
        return;
    }
    while (st->maybe_count > len) {
        u32 i = --st->maybe_count;
        st->maybe_heads[maybe_hash(st, st->maybe_keys[i])] = st->maybe_next[i];
    }
}

static Ternary recursive_related(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    RelationCache* cache = &my relations[st->relation];
    u64 key = relation_key(source, target);
    // Relating to an intersection's members is a question of its own (tsc's getRelationKey postfix): no excess property
    // checks, and a pair compared inside it isn't the same pair on the stack outside it
    if (st->intersection_target) key ^= 1ull << 63;
    if (st->intersection_source) key ^= 1ull << 62;
    unsigned cached = relation_cache_get(cache, key);
    // A measurement of variances that goes through the pair again is as unreliable as the first one was
    if (cached & CACHE_UNRELIABLE) report_variance(me, source, VARIANCE_UNRELIABLE);
    if (cached & CACHE_UNMEASURABLE) report_variance(me, source, VARIANCE_UNMEASURABLE);
    cached &= ~(CACHE_UNRELIABLE | CACHE_UNMEASURABLE);
    if (cached == CACHE_SUCCEEDED) return TERNARY_TRUE;
    // A cached failure is computed again only to explain it, and then a pair on the stack is still assumed related.
    // Which comparisons explain (`report`) decides what is blamed, so it follows tsc closely
    if (cached == CACHE_FAILED && !report) return TERNARY_FALSE;
    if (maybe_contains(st, key)) return TERNARY_MAYBE;
    if (fails_common_properties(me, st, source, target, report)) {
        relation_cache_put(cache, key, CACHE_FAILED);
        return TERNARY_FALSE;
    }
    if (st->relation_count <= 0 || st->depth == MAX_RELATION_DEPTH) {
        st->overflow = true;
        return TERNARY_FALSE;
    }
    u32 maybe_start = st->maybe_count;
    maybe_push(st, key);
    unsigned saved_expanding = st->expanding;
    bool push_source = !(st->skip_push & 1), push_target = !(st->skip_push & 2);
    st->skip_push = 0;
    if (push_source) {
        if (st->source_entries_valid > st->source_depth) st->source_entries_valid = st->source_depth;
        if (!(st->expanding & 1) && st->source_depth >= 2 && relation_deeply_nested(me, source, st->source_stack, st->source_entries, &st->source_entries_valid, st->source_depth, 3, true)) {
            st->expanding |= 1;
        }
        st->source_stack[st->source_depth++] = source;
    }
    if (push_target) {
        if (st->target_entries_valid > st->target_depth) st->target_entries_valid = st->target_depth;
        if (!(st->expanding & 2) && st->target_depth >= 2 && relation_deeply_nested(me, target, st->target_stack, st->target_entries, &st->target_entries_valid, st->target_depth, 3, true)) {
            st->expanding |= 2;
        }
        st->target_stack[st->target_depth++] = target;
    }
    st->depth++;

    unsigned char saved_reports = my variance_reports;
    my variance_reports = 0;
    Ternary result = st->expanding == 3 ? TERNARY_MAYBE : structured_related(me, st, source, target, report);
    unsigned unreliable = (my variance_reports & VARIANCE_UNRELIABLE ? CACHE_UNRELIABLE : 0) |
        (my variance_reports & VARIANCE_UNMEASURABLE ? CACHE_UNMEASURABLE : 0);
    if (debug_env.debug_relate_nested) {
        cstr source_str = ty_to_cstr(me, source);
        cstr target_str = ty_to_cstr(me, target);
        log_info("nested ", st->depth, ": '", source_str, "' -> '", target_str, "': ", (u32)result, unreliable ? " (unreliable)" : "");
        c_drop(cstr, &source_str, &target_str);
    }
    my variance_reports |= saved_reports;
    st->depth--;
    if (push_source) st->source_depth--;
    if (push_target) st->target_depth--;
    st->expanding = saved_expanding;

    if (result) {
        if (result == TERNARY_TRUE || st->depth == 0) {
            // Everything assumed along the way held up
            for (u32 i = maybe_start; i < st->maybe_count; i++) {
                relation_cache_put(cache, st->maybe_keys[i], CACHE_SUCCEEDED | unreliable);
            }
            st->relation_count -= st->maybe_count - maybe_start;
            maybe_truncate(st, maybe_start);
        }
    } else {
        relation_cache_put(cache, key, CACHE_FAILED | unreliable);
        st->relation_count--;
        maybe_truncate(st, maybe_start);
    }
    return result;
}

// Excess property checks (tsc's hasExcessProperties and friends)

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
    if (ty->kind == TY_SUBSTITUTION) return is_excess_property_check_target(ty->substitution.base); // (`NoInfer<{ a: 1 }>`)
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

bool ty_is_empty_object_type(Checker* me, Ty const* ty) {
    return is_empty_object_type(me, ty);
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
        // A pattern (`data${string}`, `string & Tag`) takes the names it matches (tsc's isApplicableIndexType)
        if ((key->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) || key->kind == TY_INTERSECTION) {
            if (ty_is_related(me, ty_prop_name_literal(me, name), key, TY_RELATION_ASSIGNABLE)) return true;
            continue;
        }
        if (!(key->flags & (TYF_NUMBER | TYF_ES_SYMBOL))) return true; // (Anything else tttt doesn't have: let it through)
    }
    return false;
}

// tsc's getLowerBoundOfKeyType: `keyof T` is the keys of T's apparent type, a type parameter stays (and names nothing)
static Ty const* lower_bound_of_key_type(Checker* me, Ty const* ty, u32 depth) {
    if (depth > 8) return ty;
    if (ty->kind == TY_KEYOF) return ty_keyof(me, apparent_type(me, ty->keyof.operand));
    if (ty->kind == TY_UNION) {
        TyBuf parts = {0};
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            ty_buf_push(&parts, lower_bound_of_key_type(me, ty->composite.types.data[i], depth + 1));
        }
        Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
        return ret;
    }
    return ty;
}

// Whether a key type of a mapped type makes a member that admits `name` (tsc's addMemberForKeyTypeWorker: a property for
// a literal or unique symbol, an index signature for string, number, symbol, any or a pattern)
static bool key_admits_name(Checker* me, Ty const* key, Atom const* name) {
    if (key->kind == TY_UNION) {
        for (u32 i = 0; i < key->composite.types.len; i++) {
            if (key_admits_name(me, key->composite.types.data[i], name)) return true;
        }
        return false;
    }
    Atom const* literal = ty_literal_name(me, key);
    if (literal) return literal == name;
    if (key->flags & (TYF_ANY | TYF_STRING)) return true;
    if (key->flags & TYF_NUMBER) return ty_is_numeric_name(name);
    if (key->flags & TYF_ES_SYMBOL) return ty_unique_symbol_of_name(me, name) != nullptr;
    if (key->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) {
        return ty_is_related(me, ty_prop_name_literal(me, name), key, TY_RELATION_ASSIGNABLE);
    }
    return false;
}

// tsc's resolveMappedTypeMembers of a generic mapped type, as far as isKnownProperty asks: the keys of the lower bound
// of its constraint (a homomorphic one's are its modifiers type's apparent keys), each through the `as` clause.
// `Pick<S, keyof S>` with S unconstrained knows nothing, `Partial<S>` with `S extends { b: number }` knows b
static bool generic_mapped_knows(Checker* me, MappedInfo const* mapped, Atom const* name) {
    Ty const* keys = lower_bound_of_key_type(me, mapped->constraint, 0);
    if (!mapped->name_type) return key_admits_name(me, keys, name);
    TyList parts = keys->kind == TY_UNION ? keys->composite.types : (TyList){ .data = &keys, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        if (parts.data[i]->flags & TYF_NEVER) continue;
        TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = &mapped->type_param, .len = 1 },
            (TyList){ .data = &parts.data[i], .len = 1 });
        if (key_admits_name(me, ty_instantiate(me, mapped->name_type, mapper), name)) return true;
    }
    return false;
}

// tsc's isKnownProperty
static bool is_known_property(Checker* me, Ty const* target, Atom const* name) {
    if (target->kind == TY_SUBSTITUTION) return is_known_property(me, target->substitution.base, name);
    if (target->flags & TYF_OBJECT) {
        Members const* members = ty_members(me, target);
        // (One over known keys has them as properties)
        bool generic = false;
        MappedInfo const* mapped = members->mapped ? mapped_info_of(me, target, &generic) : nullptr;
        if (mapped && generic) return generic_mapped_knows(me, mapped, name);
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
        Ty const* parts[2] = { prop_type(me, prop), ty_undefined_or_missing(me) };
        return ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    if (prop) return prop_type(me, prop);
    // (Any index signature that applies to the name: template literal and `string & {}` keys too, tsc's
    // getApplicableIndexInfoForName)
    if (!ty_members(me, ty)->index_count) return nullptr;
    IndexInfo const* info = ty_applicable_index_info(me, ty, ty_prop_name_literal(me, name));
    return info ? info->value : nullptr;
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
    // (Of an intersection, the properties of its members, with the types they have together)
    u32 prop_count;
    SourceProp* props = nullptr;
    Members const* members = nullptr;
    if (source->kind == TY_INTERSECTION) {
        props = source_props(me, source, nullptr, &prop_count);
    } else {
        members = ty_members(me, source);
        prop_count = members->prop_count;
    }
    bool discriminated = false;
    for (u32 p = 0; p < prop_count; p++) {
        Prop const* prop = props ? props[p].prop : members->props[p];
        if (!ty_is_discriminant_property(me, target, prop->name)) continue;
        *discriminants = true;
        Ty const* prop_ty = props ? props[p].type : prop_type(me, prop);
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
    free(props);
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

// tsc's getSignaturesOfType(ty, kind).length > 0 (of the apparent type, an intersection's being its members')
static bool has_signatures(Checker* me, Ty const* ty, bool construct) {
    ty = ty_apparent_type(me, ty);
    if (!(ty->flags & (TYF_OBJECT | TYF_INTERSECTION))) return false;
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
            // (A mapped type isn't one: tsc's ObjectFlags.Mapped isn't Anonymous)
            if (t->kind == TY_ANONYMOUS && source->alias && source->alias == t->alias && !mapped_info_of(me, source, nullptr) &&
                !mapped_info_of(me, t, nullptr)
            ) {
                return t;
            }
        }
    }
    // findBestTypeForObjectLiteral
    // (Of an object literal's type before widening: tsc's widened copy has no ObjectFlags.ObjectLiteral)
    if (source->kind == TY_ANONYMOUS && source->anonymous.object_literal && (source->flags & TYF_FRESH_OBJECT)) {
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
    // findMostOverlappyType: the (last) member sharing the most property names with the source (keys, so an index
    // signature takes every name it applies to)
    Ty const* best = nullptr;
    if (structured_source) {
        // (Of an intersection, every member's properties; the types aren't needed otherwise)
        u32 prop_count;
        SourceProp* props = nullptr;
        bool index_keys = false;
        if (source->kind == TY_INTERSECTION) {
            props = source_props(me, source, nullptr, &prop_count);
        } else {
            Members const* members = ty_members(me, source);
            prop_count = members->prop_count;
            index_keys = members->index_count > 0;
            props = malloc(sizeof(SourceProp) * (prop_count ? prop_count : 1));
            panic_if(!props, "out of memory");
            for (u32 p = 0; p < prop_count; p++) props[p] = (SourceProp){ .prop = members->props[p] };
        }
        u32 best_count = 0;
        for (u32 i = 0; i < types.len; i++) {
            Ty const* t = types.data[i];
            // A generic member: the source's keys `&` its `keyof` is a union of intersections, none a unit type, when
            // the source has several keys (one is an intersection, no match): it matches with no overlap, if nothing
            // before overlapped
            if (t->kind == TY_TYPE_PARAM || t->kind == TY_INDEXED || t->kind == TY_CONDITIONAL || t->kind == TY_SUBSTITUTION) {
                if ((prop_count >= 2 || index_keys) && !best_count) best = t;
                continue;
            }
            if (!(t->flags & (TYF_OBJECT | TYF_INTERSECTION))) continue;
            u32 count = 0;
            for (u32 p = 0; p < prop_count; p++) {
                Atom const* name = props[p].prop->name;
                count += find_prop(me, apparent_type(me, t), name, 0).found ||
                    ty_applicable_index_info(me, t, ty_prop_name_literal(me, name));
            }
            if (count && count >= best_count) {
                best = t;
                best_count = count;
            }
        }
        free(props);
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

// The property names of a type as tsc's getPropertiesOfType lists them: an intersection's are all of its members', a
// union's the ones every member has
static void collect_prop_names(Checker* me, Ty const* ty, csview** names, u32* count, u32* cap, u32 depth) {
    if (depth > 8) return;
    if (ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) collect_prop_names(me, ty->composite.types.data[i], names, count, cap, depth + 1);
        return;
    }
    if (ty->kind == TY_UNION) {
        if (!ty->composite.types.len) return;
        u32 start = *count;
        collect_prop_names(me, ty->composite.types.data[0], names, count, cap, depth + 1);
        for (u32 i = start; i < *count; i++) {
            Atom const* name = checker_atom(me, (*names)[i]);
            for (u32 j = 1; j < ty->composite.types.len; j++) {
                Ty const* member = ty_apparent_type(me, ty->composite.types.data[j]);
                if (!ty_get_object_prop(me, member, name)) (*names)[i] = (csview){0};
            }
        }
        return;
    }
    ty = ty_apparent_type(me, ty);
    if (ty->kind == TY_INTERSECTION) {
        collect_prop_names(me, ty, names, count, cap, depth + 1);
        return;
    }
    if (!(ty->flags & TYF_OBJECT)) return;
    Members const* members = ty_members(me, ty);
    for (u32 i = 0; i < members->prop_count; i++) {
        if (*count == *cap) {
            *cap = *cap ? *cap * 2 : 16;
            *names = realloc(*names, sizeof(csview) * *cap);
            panic_if(!*names, "out of memory");
        }
        (*names)[(*count)++] = atom_sv(members->props[i]->name);
    }
}

// tsc's getSuggestedSymbolForNonexistentJSXAttribute: `for` is `htmlFor`, `class` is `className`, else a spelling
// suggestion. Empty if none
static csview jsx_attribute_suggestion(Checker* me, Ty const* target, Atom const* name) {
    csview* names = nullptr;
    u32 count = 0, cap = 0;
    collect_prop_names(me, target, &names, &count, &cap, 0);
    csview wanted = atom_sv(name);
    csview special = csview_equals(wanted, "for") ? c_sv("htmlFor") : csview_equals(wanted, "class") ? c_sv("className") : c_sv("");
    csview ret = {0};
    for (u32 i = 0; i < count && special.size && !ret.size; i++) {
        if (csview_eq(&names[i], &special)) ret = names[i];
    }
    if (!ret.size && !special.size) {
        isize best = spelling_suggestion(wanted, names, count);
        if (best >= 0) ret = names[best];
    }
    free(names);
    return ret;
}

static bool has_excess_properties(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    if (!is_excess_property_check_target(target)) return false;
    bool is_jsx = source->kind == TY_ANONYMOUS && source->anonymous.jsx_attributes;
    if (st->relation == TY_RELATION_ASSIGNABLE || st->relation == TY_RELATION_COMPARABLE ||
        st->relation == TY_RELATION_ASSIGNABLE_STRICT_NULLS
    ) {
        Ty const* object = global_type(me, GLOBAL_OBJECT);
        bool object_in_target = object && (target == object || (target->kind == TY_UNION && union_has_member(target, object)));
        if (object_in_target || (!is_jsx && is_empty_object_type(me, target))) return false;
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
        if (!prop->decl->literal_own) continue;
        // A hyphenated JSX attribute (`data-x`) is never excess (tsc's isIgnoredJsxProperty)
        if (is_jsx && csview_contains(atom_sv(prop->name), "-")) continue;
        if (!is_known_property(me, reduced, prop->name)) {
            if (report && is_jsx) {
                // Not an object literal to the user: the error moves to the attribute, under the usual head (an
                // attribute's name is shown as written)
                cstr name = cstr_from_sv(atom_sv(prop->name));
                Ty const* error_target = filter_excess_check_targets(me, reduced);
                cstr target_str = display_for_report(me, error_target, false);
                csview suggestion = jsx_attribute_suggestion(me, error_target, prop->name);
                cstr suggestion_str = suggestion.size ? ty_prop_name_cstr(me, checker_atom(me, suggestion)) : cstr_null;
                cstr message = suggestion.size ?
                    cstr_d("Property '", name, "' does not exist on type '", target_str, "'. Did you mean '", suggestion_str, "'?") :
                    cstr_d("Property '", name, "' does not exist on type '", target_str, "'.");
                cstr_drop(&suggestion_str);
                vec_ty_report_line_push(&st->lines, (TyReportLine){
                    .ts_code = suggestion.size ? 2551 : 2339,
                    .message = message,
                    .span = prop->decl->name_span,
                    .relocates = span_valid(prop->decl->name_span), // (not the children, which aren't an attribute)
                });
                c_drop(cstr, &name, &target_str);
            } else if (report) {
                cstr name = ty_prop_written_name(me, prop);
                Ty const* error_target = filter_excess_check_targets(me, reduced);
                cstr target_str = display_for_report(me, error_target, false);
                // A property written as an identifier gets a spelling suggestion (tsc: `isIdentifier(name)`)
                csview written = prop->decl->name_span.sv;
                bool identifier = span_valid(prop->decl->name_span) && written.size && written.buf[0] != '"' &&
                    written.buf[0] != '\'' && written.buf[0] != '[' && written.buf[0] != '#' &&
                    !(written.buf[0] >= '0' && written.buf[0] <= '9');
                Prop const* suggestion = identifier ? ty_suggested_property(me, error_target, atom_sv(prop->name)) : nullptr;
                // (The whole diagnostic goes to the property wherever the line ends up: tsc's errorNode = name)
                if (suggestion) {
                    report_line_at(st, 2561, cstr_d(
                        "Object literal may only specify known properties, but '", name, "' does not exist in type '",
                        target_str, "'. Did you mean to write '", atom_sv(suggestion->name), "'?"
                    ), prop->decl->name_span);
                } else {
                    report_line_at(st, 2353, cstr_d(
                        "Object literal may only specify known properties, and '", name, "' does not exist in type '",
                        target_str, "'."
                    ), prop->decl->name_span);
                }
                vec_ty_report_line_back_mut(&st->lines)->relocates = span_valid(prop->decl->name_span);
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
                    cstr name = ty_prop_written_name(me, prop);
                    report_incompatible(st, 2326, cstr_d("Types of property '", name, "' are incompatible."));
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
    if (ty->kind == TY_SUBSTITUTION) return is_weak_type(me, ty->substitution.base); // (NoInfer<X>)
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
    u32 prop_count;
    SourceProp* props = source_props(me, apparent, members, &prop_count);
    bool ret = prop_count || members->call_count || members->construct_count;
    bool is_jsx = source->kind == TY_ANONYMOUS && source->anonymous.jsx_attributes;
    for (u32 i = 0; i < prop_count && ret; i++) {
        if (is_known_property(me, target, props[i].prop->name)) ret = false;
        if (is_jsx && csview_contains(atom_sv(props[i].prop->name), "-")) ret = false; // `data-x`
    }
    free(props);
    return ret;
}

Ty const* ty_strip_nullable_target(Checker* me, Ty const* source, Ty const* target) {
    (void)me;
    u32 non_nullable = TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT | TYF_BIGINT_LITERAL | TYF_BOOLEAN |
        TYF_BOOLEAN_LITERAL | TYF_ENUM | TYF_ENUM_LITERAL | TYF_ES_SYMBOL_LIKE | TYF_OBJECT | TYF_NON_PRIMITIVE;
    if ((source->kind == TY_UNION && !(source->flags & TYF_BOOLEAN)) || !(source->flags & non_nullable)) return target;
    if (target->kind != TY_UNION || target->composite.types.len < 2 || target->composite.types.len > 3) return target;
    Ty const* candidate = nullptr;
    for (u32 i = 0; i < target->composite.types.len; i++) {
        Ty const* part = target->composite.types.data[i];
        if (part->flags & TYF_NULLABLE) continue;
        if (candidate) return target;
        candidate = part;
    }
    // (`boolean | undefined` is three members in tsc: true and false aren't one. `false | undefined` is two)
    return candidate && !(candidate->kind == TY_UNION && (candidate->flags & TYF_BOOLEAN)) ? candidate : target;
}

// tsc's getNormalizedUnionOrIntersectionType for an intersection with an instantiable member next to a nullable or
// empty object one (shouldNormalizeIntersection): its members normalized, `Partial<T>[K] & {}` is `T[K] & {}`
static Ty const* normalized_intersection(Checker* me, Ty const* ty, bool writing) {
    if (ty->kind != TY_INTERSECTION || !(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return ty;
    TyList members = ty->composite.types;
    // (Only members that simplify can change anything: indexed accesses, conditional and substitution types)
    bool simplifiable = false, nullable_or_empty = false;
    for (u32 i = 0; i < members.len && !simplifiable; i++) {
        Ty const* member = members.data[i];
        simplifiable = member->kind == TY_INDEXED || member->kind == TY_CONDITIONAL || member->kind == TY_SUBSTITUTION;
    }
    if (!simplifiable) return ty;
    for (u32 i = 0; i < members.len && !nullable_or_empty; i++) {
        Ty const* member = members.data[i];
        nullable_or_empty = (member->flags & TYF_NULLABLE) || (member->kind == TY_ANONYMOUS && ty_is_empty_anonymous_object(me, member));
    }
    if (!nullable_or_empty) return ty;
    u64 key[3] = { KEY_NORMALIZED_INTERSECTION, ty->id, writing };
    Ty const* cached = intern_get(&my links, key, 3);
    if (cached) return cached;
    TyBuf parts = {0};
    bool changed = false;
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = members.data[i];
        for (u32 n = 0; n < 16 && (member->kind == TY_INDEXED || member->kind == TY_CONDITIONAL || member->kind == TY_SUBSTITUTION); n++) {
            Ty const* next = member->kind != TY_SUBSTITUTION ? ty_regular(me, ty_simplified(me, member, writing)) :
                writing ? member->substitution.base : ty_substitution_intersection(me, member);
            if (next == member) break;
            member = next;
        }
        changed |= member != members.data[i];
        ty_buf_push(&parts, member);
    }
    Ty const* ret = changed ? ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0}) : ty;
    ty_buf_drop(&parts);
    *intern_slot(me, &my links, key, 3) = ret;
    return ret;
}

// tsc's getNormalizedTupleType: a tuple with a spread of something generic (`[...{ [S in K]: [a: number] }[K]]`) has
// its simplifiable elements simplified, which may splice a tuple in (`[a: number]`). Itself otherwise
static Ty const* normalized_generic_tuple(Checker* me, Ty const* ty, bool writing) {
    if (!is_tuple(ty) || !(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return ty;
    TupleShape const* shape = ty->reference.target->declared.tuple;
    TyList args = ty->reference.args;
    bool variadic = false;
    for (u32 i = 0; i < shape->arity && i < args.len && !variadic; i++) {
        variadic = (shape->flags[i] & TUPLE_ELEMENT_REST) && (args.data[i]->kind == TY_TYPE_PARAM || (args.data[i]->flags & TYF_DEFERRED));
    }
    if (!variadic) return ty;
    Ty const** simplified = nullptr;
    for (u32 i = 0; i < args.len; i++) {
        Ty const* arg = args.data[i];
        if (arg->kind != TY_INDEXED && arg->kind != TY_CONDITIONAL) continue;
        Ty const* next = ty_simplified(me, arg, writing);
        if (next == arg) continue;
        if (!simplified) {
            simplified = malloc(sizeof(Ty const*) * args.len);
            panic_if(!simplified, "out of memory");
            memcpy(simplified, args.data, sizeof(Ty const*) * args.len);
        }
        simplified[i] = next;
    }
    if (!simplified) return ty;
    Ty const* ret = ty_reference(me, ty->reference.target, (TyList){ .data = simplified, .len = args.len });
    free(simplified);
    return ret;
}

Ty const* ty_normalized_generic_tuple(Checker* me, Ty const* ty, bool writing) {
    return normalized_generic_tuple(me, ty, writing);
}

// The source as the head of a relation error names it: tsc's reportErrorResults uses getNormalizedType's (simplified
// indexed accesses and conditional types, read) unless it has an alias
Ty const* ty_report_source(Checker* me, Ty const* source) {
    if (source->alias) return source;
    for (u32 i = 0; i < 16 && (source->kind == TY_INDEXED || source->kind == TY_CONDITIONAL || source->kind == TY_SUBSTITUTION); i++) {
        Ty const* next = source->kind != TY_SUBSTITUTION ? ty_regular(me, ty_simplified(me, source, false)) :
            source->substitution.base;
        if (next == source || next->alias != source->alias) break;
        source = next;
    }
    return source;
}

Ty const* ty_report_target(Checker* me, Ty const* source, Ty const* target) {
    if (target->alias) return target;
    for (u32 i = 0; i < 16 && (target->kind == TY_INDEXED || target->kind == TY_CONDITIONAL || target->kind == TY_SUBSTITUTION); i++) {
        Ty const* next = target->kind != TY_SUBSTITUTION ? ty_regular(me, ty_simplified(me, target, true)) :
            target->substitution.base;
        if (next == target) break;
        target = next;
    }
    target = normalized_generic_tuple(me, target, true);
    return ty_strip_nullable_target(me, ty_regular(me, source), target);
}

static Ternary is_related_to(Checker* me, RelationState* st, Ty const* source, Ty const* target, bool report) {
    bool top = st->at_top;
    st->at_top = false;
    // Relations (and their caches) only see regular literals (tsc's getNormalizedType)
    source = ty_regular(me, source);
    target = ty_regular(me, target);
    if (source == target) return TERNARY_TRUE;
    // (And reduced: getNormalizedType's getReducedType)
    Ty const* unreduced_target = target;
    if (source->kind == TY_UNION || source->kind == TY_INTERSECTION) source = ty_reduced(me, source);
    if (target->kind == TY_UNION || target->kind == TY_INTERSECTION) target = ty_reduced(me, target);
    if (source == target) return TERNARY_TRUE;
    // Unions of the same members (differing in alias or origin only)
    if (source->kind == TY_UNION && target->kind == TY_UNION &&
        source->composite.types.len == target->composite.types.len &&
        !memcmp(source->composite.types.data, target->composite.types.data,
            sizeof(Ty const*) * source->composite.types.len)) {
        return TERNARY_TRUE;
    }
    // A class or interface that adds nothing to its one base is that base (getNormalizedType)
    bool source_has_base = false;
    Ty const* unnormalized_source = source;
    Ty const* unnormalized_target = target;
    if (source->kind == TY_DECLARED || source->kind == TY_REFERENCE) {
        Ty const* base;
        for (u32 i = 0; i < 16 && (base = ty_non_augmenting_base(me, source)) && base != source; i++) {
            source = ty_regular(me, base);
            source_has_base = true;
        }
    }
    if (target->kind == TY_DECLARED || target->kind == TY_REFERENCE) {
        Ty const* base;
        for (u32 i = 0; i < 16 && (base = ty_non_augmenting_base(me, target)) && base != target; i++) target = ty_regular(me, base);
    }
    if (source == target) return TERNARY_TRUE;
    bool deferred = ((source->flags | target->flags) & TYF_DEFERRED) != 0;
    if (deferred) {
        // A substitution type is `base & constraint` as a source, its base as a target (getNormalizedType)
        while (source->kind == TY_SUBSTITUTION) source = ty_substitution_intersection(me, source);
        while (target->kind == TY_SUBSTITUTION) target = target->substitution.base;
        if (source == target) return TERNARY_TRUE;
    }
    if (st->relation == TY_RELATION_IDENTITY) return identity_related(me, st, source, target);
    Ty const* original_target = target;
    if (deferred) {
        // (And simplified indexed accesses: the target as written to)
        while (source->kind == TY_INDEXED || source->kind == TY_CONDITIONAL || source->kind == TY_SUBSTITUTION) {
            Ty const* next = source->kind != TY_SUBSTITUTION ? ty_regular(me, ty_simplified(me, source, false)) :
                ty_substitution_intersection(me, source);
            if (next == source) break;
            source = next;
        }
        while (target->kind == TY_INDEXED || target->kind == TY_CONDITIONAL || target->kind == TY_SUBSTITUTION) {
            Ty const* next = target->kind != TY_SUBSTITUTION ? ty_regular(me, ty_simplified(me, target, true)) :
                target->substitution.base;
            if (next == target) break;
            target = next;
        }
        if (source == target) return TERNARY_TRUE;
    }
    if ((source->flags | target->flags) & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
        source = normalized_generic_tuple(me, source, false);
        target = normalized_generic_tuple(me, target, true);
        if (source == target) return TERNARY_TRUE;
    }
    Ty const* stripped = ty_strip_nullable_target(me, source, target);
    // (What's left is normalized too: tsc's getNormalizedType(nullStrippedTarget, writing))
    while (stripped != target && (stripped->kind == TY_INDEXED || stripped->kind == TY_CONDITIONAL || stripped->kind == TY_SUBSTITUTION)) {
        Ty const* next = stripped->kind != TY_SUBSTITUTION ? ty_regular(me, ty_simplified(me, stripped, true)) :
            stripped->substitution.base;
        if (next == stripped) break;
        stripped = next;
    }
    target = stripped;
    if (source == target) return TERNARY_TRUE;
    // (Errors name an alias as it was: tsc's reportErrorResults)
    // (And a class or interface that adds nothing to its base, as written: tsc's reportErrorResults)
    Ty const* report_target = original_target->alias ? original_target : target;
    if (report && unnormalized_target != original_target && ty_non_augmenting_base(me, unnormalized_target)) report_target = unnormalized_target;
    Ty const* report_source = source_has_base ? unnormalized_source : source;
    // Comparability is mostly bidirectional (tsc's isRelatedTo: simple relations the other way around count too)
    if (st->relation == TY_RELATION_COMPARABLE && !(target->flags & TYF_NEVER) && is_simple_related(me, st, target, source)) {
        return TERNARY_TRUE;
    }
    if (is_simple_related(me, st, source, target)) return TERNARY_TRUE;
    // (tsc's isSimpleTypeRelatedTo with an error reporter: why two enums of the same name aren't related)
    if (report && ((source->flags | target->flags) & (TYF_ENUM | TYF_ENUM_LITERAL | TYF_UNION))) report_enum_pair(me, st, source, target);
    if (ty_is_fresh_object(source) && !st->intersection_target && is_structured(target) &&
        has_excess_properties(me, st, source, target, report)
    ) {
        if (report) report_relation_error(me, st, report_source, report_target, top, source_has_base);
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
    if (st->head_reported) {
        st->head_reported = false;
        return result;
    }
    // (tsc's reportErrorResults: JSX attributes against `IntrinsicAttributes & P` get no head, unlike an excess
    // property above)
    if (!result && report && !ty_jsx_skips_head(me, source, target)) {
        // Why the target is never (tsc's reportErrorResults with elaborateNeverIntersection)
        unsigned code;
        cstr message;
        if (unreduced_target != target && ty_never_intersection_elaboration(me, unreduced_target, &code, &message)) {
            report_line(st, code, message);
        }
        report_relation_error(me, st, report_source, report_target, top, source_has_base);
    }
    return result;
}

// The error type is related to everything, in both directions: whatever made it was reported already. Except as a
// source of the subtype relations, where it is any, which is a subtype of nothing but any and unknown (tsc has no
// shortcut, isSimpleTypeRelatedTo: an overload taking `any` wins over a generic one for an unresolved name's argument)
static inline bool error_related(Ty const* source, Ty const* target, TyRelation relation) {
    if (target->flags & TYF_ERROR) return true;
    // (Not to never either, which any isn't: `isNever(unresolved)` is TS2345 to tsc)
    return (source->flags & TYF_ERROR) && !(target->flags & TYF_NEVER) && relation != TY_RELATION_SUBTYPE &&
        relation != TY_RELATION_STRICT_SUBTYPE;
}

static bool check_related(
    Checker* me, Ty const* source, Ty const* target, TyRelation relation, VecTyReportLine* report_into, unsigned head_code
) {
    if (error_related(source, target, relation)) return true;
    Ty const* fresh_union = source->kind == TY_UNION && (source->flags & TYF_FRESH_LITERAL) ? source : nullptr;
    source = ty_regular(me, source);
    target = ty_regular(me, target);
    if (source == target) return true;
    // Two members of one enum: narrowing asks this about every member of big enum unions (SyntaxKind). Members of
    // the same value share their type, so this is what is_simple_related() would conclude after its walk
    if (source->kind == TY_LITERAL && target->kind == TY_LITERAL && (source->flags & target->flags & TYF_ENUM_LITERAL) &&
        source->literal.enum_decl == target->literal.enum_decl && relation != TY_RELATION_IDENTITY && !report_into
    ) {
        return same_literal_value(source, target);
    }
    // The stacks are big and only ever read below `depth`, so the state isn't zeroed wholesale
    RelationState state;
    RelationState* st = &state;
    st->relation = relation;
    st->reporting = report_into != nullptr;
    st->lines = (VecTyReportLine){0};
    st->override_next_head = 0;
    st->intersection_target = false;
    st->intersection_source = false;
    st->member_of_intersection_source = false;
    st->maybe_keys = nullptr;
    st->maybe_next = st->maybe_heads = nullptr;
    st->maybe_count = st->maybe_cap = 0;
    st->depth = 0;
    st->source_depth = st->target_depth = 0;
    st->source_entries_valid = st->target_entries_valid = 0;
    st->skip_push = 0;
    st->expanding = 0;
    st->overflow = false;
    st->relation_count = ((i64)16000000 - (i64)my relations[relation].len) >> 3;
    st->head_reported = false;
    st->skipped_source = st->skipped_target = nullptr;
    st->at_top = true;
    st->head_code = head_code;
    st->checker = me;
    st->outer = my relating;
    st->fresh_union = report_into ? fresh_union : nullptr;
    st->alias_fallback_source = st->alias_fallback_target = nullptr;
    if (source == target) return true;
    // (Unreduced, like tsc's isTypeRelatedTo: `any` is assignable to an intersection that is never, which
    // is_related_to() reduces first and finds it isn't)
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
        // tsc's checkTypeRelatedTo: TS2859 when the relation count ran out, else TS2321 (the stack reached 100), at the
        // error node, else the current node
        bool complexity = st->relation_count <= 0;
        cstr source_str = ty_to_cstr(me, source);
        cstr target_str = ty_to_cstr(me, target);
        ty_error_at_current_node_d(me, complexity ? 2859 : 2321, complexity ? "Excessive complexity" : "Excessive stack depth",
            " comparing types '", source_str, "' and '", target_str, "'.");
        c_drop(cstr, &source_str, &target_str);
        // (A limit like the instantiation ones: what it cut short says nothing more)
        my instantiation_limit_hits++;
        if (complexity) my relation_complexity_hits++;
        else my relation_depth_hits++;
        result = TERNARY_FALSE;
    }
    if (report_into && !result) {
        flush_incompatible(st);
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
    free(st->maybe_next);
    free(st->maybe_heads);
    if (debug_env.debug_relate) {
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
    if (error_related(source, target, st->relation)) return true;
    return is_related_to(me, st, source, target, false) != TERNARY_FALSE;
}

bool ty_is_related(Checker* me, Ty const* source, Ty const* target, TyRelation relation) {
    return check_related(me, source, target, relation, nullptr, 0);
}

bool ty_explain_unrelated(Checker* me, Ty const* source, Ty const* target, TyRelation relation, VecTyReportLine* out) {
    return check_related(me, source, target, relation, out, 0);
}

bool ty_explain_unrelated_headed(
    Checker* me, Ty const* source, Ty const* target, TyRelation relation, unsigned head_code, VecTyReportLine* out
) {
    return check_related(me, source, target, relation, out, head_code);
}

// Union signatures (tsc's getUnionSignatures): what a union of callables can be called with

static bool types_identical(Checker* me, Ty const* a, Ty const* b) {
    return a == b || ty_is_related(me, a, b, TY_RELATION_IDENTITY);
}

// How signatures_identical() compares two types: by identity (inside the identity relation `st` if there is one), or
// with `partial`, by strict subtype (tsc's compareTypesSubtypeOf)
static Ternary sig_types_compare(Checker* me, RelationState* st, Ty const* t, Ty const* s, bool partial) {
    if (partial) return t == s || ty_is_related(me, t, s, TY_RELATION_SUBTYPE) ? TERNARY_TRUE : TERNARY_FALSE;
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
    u32 source_min = signature_min_args(source), target_min = signature_min_args(target);
    bool same_shape = ty_signature_param_count(source) == ty_signature_param_count(target) && source_min == target_min &&
        ty_signature_has_rest(source) == ty_signature_has_rest(target);
    if (!same_shape && !(partial && source_min <= target_min)) return TERNARY_FALSE;
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
    u32 target_len = ty_signature_param_count(target);
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
    Ty const* source_return = signature_return_type(me, source);
    return result & sig_types_compare(me, st, source_return, signature_return_type(me, target), partial);
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
    u32 some_abstract = 0;
    for (u32 i = 0; i < count; i++) {
        if (composites[i]->flags & (SIGNATURE_ABSTRACT | SIGNATURE_SOME_ABSTRACT)) some_abstract = SIGNATURE_SOME_ABSTRACT;
    }
    Signature* ret = arena_new(&my arena, Signature);
    *ret = (Signature){
        .flags = base->flags | some_abstract,
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
    u32 left_count = ty_signature_param_count(left);
    u32 right_count = ty_signature_param_count(right);
    Signature const* longest = left_count >= right_count ? left : right;
    Signature const* shorter = longest == left ? right : left;
    u32 longest_count = longest == left ? left_count : right_count;
    bool either_rest = ty_signature_has_rest(left) || ty_signature_has_rest(right);
    bool extra_rest = either_rest && !ty_signature_has_rest(longest);
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
        bool optional = i >= signature_min_args(longest) && i >= signature_min_args(shorter); // (tsc's getMinArgumentCount)
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
        .flags = (left->flags & SIGNATURE_CONSTRUCT) |
            ((left->flags | right->flags) & (SIGNATURE_ABSTRACT | SIGNATURE_SOME_ABSTRACT) ? SIGNATURE_SOME_ABSTRACT : 0),
        .name_span = left->name_span,
        .decl_span = left->decl_span,
        .declared_by = left->declared_by ? left->declared_by : left,
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

// The contextual signature of a union (tsc's getContextualSignature): `base`'s `this` as it is, not the intersection
// of every member's (that is getUnionSignatures' doing, createUnionSignature clones the first's)
Signature const* ty_union_signature(Checker* me, Signature const* base, Signature* const* composites, u32 count) {
    Signature* ret = union_signature(me, base, composites, count);
    ret->this_type = base->this_type;
    // (Whose `this` parameter that is: the clone's thisParameter is the base's symbol)
    ret->declared_by = base->declared_by ? base->declared_by : base;
    return ret;
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
    Signature* const** lists = malloc(sizeof(Signature* const*) * members.len);
    u32* lens = malloc(sizeof(u32) * members.len);
    panic_if(!lists || !lens, "out of memory");
    Ty const* function_type = construct ? nullptr : ty_global_function_type(me);
    for (u32 i = 0; i < members.len; i++) {
        // `Function` is called through tsc's unknownSignature: no parameters, returning errorType
        // (resolveUnionTypeMembers)
        if (function_type && members.data[i] == function_type) {
            if (!my unknown_signature) {
                Signature* unknown = arena_new(&my arena, Signature);
                *unknown = (Signature){
                    .state = LAZY_STATE_DONE, .ret_state = LAZY_STATE_DONE, .ret = ty_intrinsic(me, TY_ERROR), .predicate_done = true,
                };
                my unknown_signature = arena_new_n(&my arena, Signature*, 1);
                my unknown_signature[0] = unknown;
            }
            lists[i] = my unknown_signature;
            lens[i] = 1;
            continue;
        }
        Ty const* member = ty_apparent_type(me, members.data[i]);
        // (An intersection's are its members', see resolve_intersection_members())
        Members const* m = ty_members(me, member);
        lists[i] = construct ? m->construct : m->call;
        lens[i] = construct ? m->construct_count : m->call_count;
    }
    Signature* const* ret = ty_union_signature_lists(me, lists, lens, members.len, count_out);
    free(lists);
    free(lens);
    return ret;
}

// The same for the signatures of each member as given (JSX takes a member's construct signatures, else its call ones)
Signature* const* ty_union_signature_lists(Checker* me, Signature* const* const* member_lists, u32 const* lens, u32 member_count, u32* count_out) {
    *count_out = 0;
    SignatureList* lists = malloc(sizeof(SignatureList) * member_count);
    panic_if(!lists, "out of memory");
    u32 total = 0;
    i32 overloaded = -2; // -2: no list with more than one signature, -1: several
    for (u32 i = 0; i < member_count; i++) {
        lists[i] = (SignatureList){ member_lists[i], lens[i] };
        if (!lists[i].len) {
            free(lists);
            return nullptr;
        }
        if (lists[i].len > 1) overloaded = overloaded == -2 ? (i32)i : -1;
        total += lists[i].len;
    }
    Signature** result = arena_new_n(&my arena, Signature*, total);
    u32 result_count = 0;
    Signature** matches = malloc(sizeof(Signature*) * member_count);
    panic_if(!matches, "out of memory");
    for (u32 i = 0; i < member_count; i++) {
        for (u32 j = 0; j < lists[i].len; j++) {
            Signature* sig = lists[i].data[j];
            signature_resolve(me, sig);
            SignatureList so_far = { result, result_count };
            if (result_count && find_matching_signature(me, so_far, sig, false, true)) continue;
            u32 match_count = find_matching_signatures(me, lists, member_count, sig, i, matches);
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
        for (u32 i = 0; i < member_count && compatible; i++) {
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
