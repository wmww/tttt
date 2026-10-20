#include "ty/ty_internal.h"
#include "error.h"

// Property and element access: the type of `object[index]` for an index that is known (a literal, `string`, `number`,
// `symbol`), through apparent types like tsc's getPropertyOfType/getIndexedAccessType on non-generic operands. A type
// parameter is accessed through its constraint.

u32 ty_tuple_fixed_length(TupleShape const* shape) {
    u32 fixed = 0;
    while (fixed < shape->arity && !(shape->flags[fixed] & TUPLE_ELEMENT_REST)) fixed++;
    return fixed;
}

Ty const* ty_tuple_slice_element_type(Checker* me, Ty const* tuple, u32 from, u32 end_skip, bool no_reductions) {
    TupleShape const* shape = tuple->reference.target->declared.tuple;
    if (from + end_skip >= shape->arity) return nullptr;
    TyList elements = ty_reference_args(me, tuple);
    u32 len = shape->arity - end_skip - from;
    Ty const* stack[16];
    Ty const** parts = len <= c_arraylen(stack) ? stack : malloc(sizeof(Ty const*) * len);
    panic_if(!parts, "out of memory");
    for (u32 i = 0; i < len; i++) {
        Ty const* element = elements.data[from + i];
        if (shape->flags[from + i] & TUPLE_ELEMENT_REST) {
            // (A variadic `...T` is T[number], a generic mapped type `...{ [K in keyof T]: X<T[K]> }` X<T[number]>:
            // tsc's getIndexedAccessType)
            bool generic = element->kind == TY_TYPE_PARAM ||
                (element->kind != TY_REFERENCE && (element->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES));
            element = generic ? ty_indexed(me, element, ty_intrinsic(me, TY_NUMBER)) : ty_rest_element_type(me, element);
        }
        parts[i] = element;
    }
    Ty const* ret = no_reductions ? ty_union_unreduced(me, parts, len) : ty_union(me, parts, len, nullptr, (TyList){0});
    if (parts != stack) free(parts);
    return ret;
}

static Ty const* with_undefined(Checker* me, Ty const* type) {
    if (!compiler_options()->strict_null_checks) return type;
    Ty const* parts[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
    return ty_union(me, parts, 2, nullptr, (TyList){0});
}

// noUncheckedIndexedAccess's undefined of an index signature read: tsc's missingType, which `"k" in o` takes out of
// `o.k` (narrow_missing_by_presence()) and an optional property's undefined isn't
static Ty const* with_missing(Checker* me, Ty const* type) {
    // (Without strictNullChecks a union drops undefined, unless nothing else is left: `never` is undefined then)
    if (!compiler_options()->strict_null_checks) return type == ty_intrinsic(me, TY_NEVER) ? ty_intrinsic(me, TY_UNDEFINED) : type;
    Ty const* parts[2] = { type, ty_intrinsic(me, TY_MISSING) };
    return ty_union(me, parts, 2, nullptr, (TyList){0});
}

Atom const* ty_literal_name(Checker* me, Ty const* index) {
    if (!(index->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL))) return nullptr;
    if (index->literal.string) return index->literal.string;
    // (A small non-negative integer, the common enum member or tuple index, without the general JS number formatting)
    Value const* value = &index->literal.value;
    if (value->tag == VALUE_NUMBER && value->number >= 0 && value->number < 1e15 && value->number == (double)(i64)value->number) {
        // (-0 is "0" to JS's ToString too)
        char buf[24];
        char* end = buf + sizeof buf;
        char* at = end;
        u64 n = (u64)value->number;
        do { *--at = (char)('0' + n % 10); n /= 10; } while (n);
        return checker_atom(me, (csview){ .buf = at, .size = end - at });
    }
    cstr rendered = cstr_null;
    cstr_append_value_js(&rendered, &index->literal.value);
    Atom const* name = checker_atom(me, cstr_sv(&rendered));
    cstr_drop(&rendered);
    return name;
}

// `${number}` (tsc's numericStringType)
bool ty_is_numeric_string_type(Checker* me, Ty const* ty) {
    if (ty->kind != TY_TEMPLATE_LITERAL) return false;
    Atom const* empty = checker_atom(me, csview_from(""));
    Atom const* texts[2] = { empty, empty };
    Ty const* number = ty_intrinsic(me, TY_NUMBER);
    return ty == ty_template_literal(me, texts, &number, 1);
}

// Whether a specific (not `string`) index signature applies to a key (tsc's isApplicableIndexType): a numeric one to
// numbers and numeric strings, a symbol one to symbols, a pattern (`[k: `data-${string}`]`) to what it matches
bool ty_specific_index_applies(Checker* me, IndexInfo const* info, Ty const* index) {
    if (index->flags & (TYF_NEVER | TYF_ANY)) return true; // (Assignable to any key type)
    u32 key_flags = ty_key_flags(index);
    bool stringish = (key_flags & TYF_STRING_LIKE) != 0;
    bool numeric = (key_flags & (TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM)) != 0 ||
        ((index->flags & TYF_STRING_LITERAL) && index->literal.string && ty_is_numeric_name(index->literal.string)) ||
        ty_is_numeric_string_type(me, index);
    return ((info->key->flags & TYF_NUMBER) && numeric) || ((info->key->flags & TYF_ES_SYMBOL) && (key_flags & TYF_ES_SYMBOL_LIKE)) ||
        ((info->key->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING) || info->key->kind == TY_INTERSECTION) &&
            (stringish || numeric || info->key->kind == TY_INTERSECTION) &&
            ty_is_related(me, index, info->key, TY_RELATION_ASSIGNABLE));
}

// tsc's findApplicableIndexInfo over a list of index infos
static IndexInfo const* find_applicable_index_info(Checker* me, IndexInfo const* infos, u32 count, Ty const* index) {
    // A private name is no key of an index signature (tsc's checkPropertyAccessExpressionOrQualifiedName: not for a
    // PrivateIdentifier); a string literal "#x" is
    if (index->kind == TY_LITERAL && (index->flags & TYF_STRING_LITERAL) && index->literal.string &&
        index->literal.string->len && index->literal.string->str[0] == '#' &&
        index->literal.string != checker_atom(me, atom_sv(index->literal.string))
    ) {
        return nullptr;
    }
    // The specific ones first. Several of them apply as one, whose value is the intersection of theirs
    IndexInfo const* found = nullptr;
    TyBuf values = {0};
    bool readonly = true;
    for (u32 i = 0; i < count; i++) {
        IndexInfo const* info = &infos[i];
        if ((info->key->flags & TYF_STRING) || !ty_specific_index_applies(me, info, index)) continue;
        if (!found) found = info;
        ty_buf_push(&values, info->value);
        readonly &= info->readonly;
    }
    if (values.len > 1) {
        IndexInfo* combined = arena_new(&my arena, IndexInfo);
        *combined = *found;
        combined->key = ty_intrinsic(me, TY_UNKNOWN_);
        combined->value = ty_intersection(me, values.data, values.len, nullptr, (TyList){0});
        combined->readonly = readonly;
        found = combined;
    }
    ty_buf_drop(&values);
    if (found) return found;
    u32 key_flags = ty_key_flags(index);
    bool stringish_or_numeric = (key_flags & (TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM)) != 0 ||
        (index->flags & (TYF_NEVER | TYF_ANY));
    for (u32 i = 0; i < count; i++) {
        IndexInfo const* info = &infos[i];
        if ((info->key->flags & TYF_STRING) && stringish_or_numeric) return info;
    }
    return nullptr;
}

IndexInfo const* ty_applicable_index_info(Checker* me, Ty const* object, Ty const* index) {
    // (An intersection's are its members' intersected per key, see resolve_intersection_members())
    Members const* members = ty_members(me, object);
    return find_applicable_index_info(me, members->index, members->index_count, index);
}

// What `U[any]` reads of a union U through index signatures, null if none: only the key types every member has an
// index signature for count (tsc's resolveUnionTypeMembers, getUnionIndexInfos by getIndexInfoOfType's exact key), so
// `(string | Record<string, X>)[any]` has none (String's is a number one), then each member's applicable one of those,
// united. Null and undefined members don't count (the caller reports them, tsc's checkNonNullExpression).
// `kinds`: the apparent member types that have index signatures here
Ty const* ty_union_any_index_type(Checker* me, Ty const* object, Ty const* index, u32 kinds) {
    TyList members = object->composite.types;
    Ty const* common[8];
    u32 common_count = 0;
    bool first = true;
    for (u32 i = 0; i < members.len; i++) {
        if (members.data[i]->flags & TYF_NULLABLE) continue;
        Ty const* member = ty_apparent_type(me, members.data[i]);
        if (!(member->flags & kinds)) return nullptr;
        Members const* m = ty_members(me, member);
        if (first) {
            for (u32 j = 0; j < m->index_count && common_count < c_arraylen(common); j++) common[common_count++] = m->index[j].key;
            first = false;
            continue;
        }
        u32 kept = 0;
        for (u32 k = 0; k < common_count; k++) {
            bool has = false;
            for (u32 j = 0; j < m->index_count && !has; j++) has = m->index[j].key == common[k];
            if (has) common[kept++] = common[k];
        }
        common_count = kept;
        if (!common_count) return nullptr;
    }
    if (!common_count) return nullptr;
    Ty const** parts = malloc(sizeof(Ty const*) * members.len);
    panic_if(!parts, "out of memory");
    u32 len = 0;
    bool all = true;
    for (u32 i = 0; i < members.len && all; i++) {
        if (members.data[i]->flags & TYF_NULLABLE) continue;
        Members const* m = ty_members(me, ty_apparent_type(me, members.data[i]));
        IndexInfo shared[8];
        u32 shared_count = 0;
        for (u32 j = 0; j < m->index_count; j++) {
            for (u32 k = 0; k < common_count; k++) {
                if (m->index[j].key == common[k] && shared_count < c_arraylen(shared)) shared[shared_count++] = m->index[j];
            }
        }
        IndexInfo const* info = find_applicable_index_info(me, shared, shared_count, index);
        if (info) parts[len++] = info->value;
        else all = false;
    }
    Ty const* ret = all && len ? ty_union(me, parts, len, nullptr, (TyList){0}) : nullptr;
    free(parts);
    return ret;
}

static Ty const* access_index_infos(Checker* me, Ty const* object, Ty const* index) {
    IndexInfo const* info = ty_applicable_index_info(me, object, index);
    return info ? info->value : nullptr;
}

bool ty_union_non_public_conflict(Checker* me, Ty const* object, Atom const* name) {
    Prop const* first_root = nullptr;
    bool non_public = false, differ = false;
    for (u32 i = 0; i < object->composite.types.len; i++) {
        Ty const* member = object->composite.types.data[i];
        if (member->flags & TYF_NULLABLE) continue;
        member = ty_apparent_type(me, member);
        if (!(member->flags & TYF_OBJECT)) return false; // TODO: intersections and nested unions
        Prop const* prop = ty_get_prop(me, member, name);
        if (!prop) {
            differ = true;
            continue;
        }
        non_public |= (prop->flags & (PROP_PRIVATE | PROP_PROTECTED)) != 0;
        Prop const* root = prop;
        while (root->target) root = root->target;
        if (!first_root) first_root = root; else differ |= root != first_root;
    }
    return non_public && differ;
}

enum {
    ACCESS_INCLUDE_UNDEFINED = 1 << 0,
    // No access node (tsc's getIndexedAccessTypeOrUndefined for a constraint): a negative index of a tuple without a
    // rest element is its number index signature's
    ACCESS_NO_NODE = 1 << 2,
    // A symbol key that no index signature applies to reads the string index signature, as tsc's
    // getPropertyTypeForIndexType does (reporting TS2538 where there's an access node). Not for lookups by name (tsc's
    // getPropertyOfType and getApplicableIndexInfoForName: a late-bound name only looks for symbol index signatures)
    ACCESS_SYMBOL_STRING = 1 << 1,
};

// ACCESS_INCLUDE_UNDEFINED: what an index signature or the rest of a tuple gives includes undefined (noUncheckedIndexedAccess, tsc's
// AccessFlags.IncludeUndefined)
// tsc's isStringIndexSignatureOnlyType: no properties and a string index signature only (of every member of a union or
// intersection)
static bool is_string_index_signature_only(Checker* me, Ty const* ty) {
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        if (ty->flags & TYF_BOOLEAN) return false;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!is_string_index_signature_only(me, ty->composite.types.data[i])) return false;
        }
        return true;
    }
    // (A tuple has `length`: no need to make its members. Nor a generic mapped type's, as tsc's !isGenericMappedType:
    // expanding one goes through its keys' constraints, typesaurus's `AllRequired<AllRequired<Model>[K1]>[K2]` chains
    // made 2x tsgo's types in date-fns/pkgs/docs)
    if (!(ty->flags & TYF_OBJECT) || ty_tuple_shape(ty) || ty_generic_mapped(me, ty)) return false;
    Members const* members = ty_members(me, ty);
    return !members->mapped && !members->prop_count && members->index_count == 1 && (members->index[0].key->flags & TYF_STRING);
}

// An element access or indexed access type of an object with nothing but a string index signature reads that for any
// string or number key, `o["toString"]` included (tsc's getIndexedAccessTypeOrUndefined; property access expressions
// find Object's toString)
Ty const* ty_string_index_only_key(Checker* me, Ty const* object, Ty const* index) {
    // (string and number too, whose answer is the same either way: the members it resolves are what tsc resolves, a
    // deferred reference's arguments in flight are TS4109, `[...Recur<T>[number][]]`)
    if ((index->flags & TYF_NULLABLE) || (index->kind == TY_INTRINSIC && !(index->flags & (TYF_STRING | TYF_NUMBER))) ||
        !is_string_index_signature_only(me, object)
    ) {
        return index;
    }
    if (ty_is_related(me, index, ty_intrinsic(me, TY_STRING), TY_RELATION_ASSIGNABLE) ||
        ty_is_related(me, index, ty_intrinsic(me, TY_NUMBER), TY_RELATION_ASSIGNABLE)
    ) {
        return ty_intrinsic(me, TY_STRING);
    }
    return index;
}

// Whether `member[index]` reads a property of its own rather than an index signature: a tuple's elements before its
// first variadic one are properties "0", "1"... (tsc's createTupleTargetType), anything else's are its properties.
// A union member's decides between tsc's union property and the union's index signatures
static bool reads_own_prop(Checker* me, Ty const* member, Ty const* index) {
    Ty const* apparent = ty_apparent_type(me, member);
    TupleShape const* shape = ty_tuple_shape(apparent);
    if (shape && (index->flags & TYF_NUMBER_LITERAL)) {
        double n = index->literal.value.number;
        u32 props = 0;
        while (props < shape->arity && !(shape->flags[props] & TUPLE_ELEMENT_REST)) props++;
        if (n >= 0 && n == (u32)n && (u32)n < props) return true;
    }
    Atom const* name = ty_literal_name(me, index);
    return name && (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) && ty_find_prop(me, apparent, name).found;
}

// Whether a union of objects none of which has the property itself has an index signature for `index`: tsc's
// getUnionIndexInfos keeps the first member's index signatures whose key type every member has exactly
// (getIndexInfoOfType), so `T[] | Record<string, T>` has none. Unknown shapes (tuples, intersections, primitives) say yes
static bool union_index_applies(Checker* me, Ty const* object, Ty const* index, u32 flags) {
    Ty const* first = nullptr;
    for (u32 i = 0; i < object->composite.types.len; i++) {
        Ty const* member = object->composite.types.data[i];
        if (member->flags & TYF_NULLABLE) continue;
        Ty const* apparent = ty_apparent_type(me, member);
        if (apparent->kind != TY_DECLARED && apparent->kind != TY_ANONYMOUS && apparent->kind != TY_REFERENCE) return true;
        if (!(apparent->flags & TYF_OBJECT) || ty_tuple_shape(apparent) || reads_own_prop(me, member, index)) return true;
        if (!first) first = apparent;
    }
    if (!first) return true;
    Members const* first_members = ty_members(me, first);
    for (u32 k = 0; k < first_members->index_count; k++) {
        IndexInfo const* info = &first_members->index[k];
        // (A symbol reads the string index signature: getPropertyTypeForIndexType)
        bool symbol_string = (flags & ACCESS_SYMBOL_STRING) && (ty_key_flags(index) & TYF_ES_SYMBOL_LIKE) && (info->key->flags & TYF_STRING);
        if (!symbol_string && !find_applicable_index_info(me, info, 1, index)) continue;
        bool every = true;
        for (u32 i = 0; i < object->composite.types.len && every; i++) {
            Ty const* member = object->composite.types.data[i];
            if (member->flags & TYF_NULLABLE) continue;
            Members const* members = ty_members(me, ty_apparent_type(me, member));
            bool has = false;
            for (u32 j = 0; j < members->index_count && !has; j++) has = members->index[j].key == info->key;
            every = has;
        }
        if (every) return true;
    }
    return false;
}

static Ty const* access_uncached(Checker* me, Ty const* object, Ty const* index, u32 depth, u32 flags);
// (Memoized for a union like tsc's union property cache, resolveUnionTypeMembers' propertyCache: reading `node.type` of
// babel's 260-member Node union again built and ordered the union of the 260 property types each time. At depth 0
// only: the depth cutoff makes a deeper call's answer differ)
static Ty const access_none;
static Ty const* access(Checker* me, Ty const* object, Ty const* index, u32 depth, u32 flags) {
    if (depth || object->kind != TY_UNION) return access_uncached(me, object, index, depth, flags);
    u64 key[4] = { KEY_UNION_ACCESS, object->id, index->id, flags };
    Ty const* cached = intern_get(&my links, key, 4);
    if (cached) return cached == &access_none ? nullptr : cached;
    Ty const* ret = access_uncached(me, object, index, depth, flags);
    *intern_slot(me, &my links, key, 4) = ret ? ret : &access_none;
    return ret;
}

static Ty const* access_uncached(Checker* me, Ty const* object, Ty const* index, u32 depth, u32 flags) {
    bool include_undefined = (flags & ACCESS_INCLUDE_UNDEFINED) != 0;
    if (depth > 16) return nullptr;
    // (tsc's getIndexedAccessTypeOrUndefined: the object reduced)
    if (object->kind == TY_UNION || object->kind == TY_INTERSECTION) object = ty_reduced(me, object);
    if (index->kind == TY_UNION && !(index->flags & TYF_BOOLEAN)) {
        Ty const** parts = malloc(sizeof(Ty const*) * index->composite.types.len);
        Ty const* ret = nullptr;
        u32 len = 0;
        for (u32 i = 0; i < index->composite.types.len; i++) {
            Ty const* part = access(me, object, index->composite.types.data[i], depth + 1, flags);
            if (!part) break;
            parts[len++] = part;
        }
        if (len == index->composite.types.len) ret = ty_union(me, parts, len, nullptr, (TyList){0});
        free(parts);
        return ret;
    }
    if (object->flags & TYF_ANY) return object;
    if (object->flags & TYF_NEVER) {
        // (Only by a key: tsc's getPropertyTypeForIndexType returns never for a string, number or symbol-like one, a
        // bigint or object key is TS2538 and the error type)
        bool key = (index->flags & TYF_ANY) || (!(index->flags & TYF_NULLABLE) &&
            (ty_key_flags(index) & (TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM | TYF_ES_SYMBOL_LIKE)));
        return key || index->kind == TY_UNION || ty_is_generic_index(index) ? object : ty_intrinsic(me, TY_ERROR);
    }
    if (index->flags & TYF_NEVER) {
        // What the index signatures say (never is assignable to every key type), else never (tsc's
        // getPropertyTypeForIndexType)
        Ty const* apparent = ty_apparent_type(me, object);
        if (apparent->flags & TYF_ANY) return apparent;
        IndexInfo const* info = (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) ? ty_applicable_index_info(me, apparent, index) : nullptr;
        // (The plain never, also for inference_mapper_no_default()'s silent never: tsc's neverType, which infers)
        if (!info) return ty_intrinsic(me, TY_NEVER);
        return include_undefined ? with_missing(me, info->value) : info->value;
    }
    if (index->flags & TYF_ANY) {
        // What the index signatures say (any is assignable to every key type: the specific ones, else a string one,
        // tsc's getPropertyTypeForIndexType with getApplicableIndexInfo)
        u32 any_key_kinds = TYF_OBJECT | TYF_INTERSECTION;
        Ty const* apparent = ty_apparent_type(me, object);
        if (apparent->kind == TY_UNION && !(apparent->flags & TYF_BOOLEAN)) {
            Ty const* ret = ty_union_any_index_type(me, apparent, index, any_key_kinds);
            return !ret ? ty_intrinsic(me, TY_ANY) : include_undefined ? with_missing(me, ret) : ret;
        }
        IndexInfo const* info = (apparent->flags & any_key_kinds) ? ty_applicable_index_info(me, apparent, index) : nullptr;
        if (!info) return ty_intrinsic(me, TY_ANY);
        return include_undefined ? with_missing(me, info->value) : info->value;
    }
    // (A generic intersection, `keyof T & string`, is a generic index: the access is deferred, tsc's getIndexedAccessType)
    if (index->kind == TY_INTERSECTION && ty_is_generic_index(index)) return nullptr;
    bool simple = (ty_key_flags(index) & (TYF_STRING_LIKE | TYF_NUMBER | TYF_ES_SYMBOL_LIKE | TYF_NUMBER_LITERAL | TYF_ENUM)) != 0;
    if (!simple) return nullptr;

    if (object->kind == TY_UNION && !(object->flags & TYF_BOOLEAN)) {
        // A member's own property makes a union property, whose other members' index signatures are read without
        // undefined (tsc's createUnionOrIntersectionProperty: noUncheckedIndexedAccess is getPropertyTypeForIndexType's,
        // for the union's own index signatures)
        if (include_undefined) {
            for (u32 i = 0; i < object->composite.types.len; i++) {
                Ty const* member = object->composite.types.data[i];
                if (!(member->flags & TYF_NULLABLE) && reads_own_prop(me, member, index)) {
                    flags &= ~(u32)ACCESS_INCLUDE_UNDEFINED;
                    include_undefined = false;
                    break;
                }
            }
        }
        // A symbol reads the string index signature of the union, not of each member: one member's own `[sym]`
        // doesn't make a union property with the others' string index signatures (tsc's
        // createUnionOrIntersectionProperty skips index signatures for a late-bound name)
        bool symbol_string = (flags & ACCESS_SYMBOL_STRING) && (ty_key_flags(index) & TYF_ES_SYMBOL_LIKE);
        if (symbol_string) flags &= ~(u32)ACCESS_SYMBOL_STRING;
        // Every member has to have it. null and undefined don't count: what to say about them is the caller's business
        Ty const** parts = malloc(sizeof(Ty const*) * object->composite.types.len);
        Ty const* ret = nullptr;
        u32 len = 0;
        bool missing = false, padded = false, found_in_some = false;
        u64 padded_tuples = 0; // (bit i: parts[i] is a too short tuple's undefined)
        for (u32 i = 0; i < object->composite.types.len && !missing; i++) {
            Ty const* member = object->composite.types.data[i];
            if (member->flags & TYF_NULLABLE) continue;
            // (Nor an intersection that is never once apparent, `undefined & M` with M an object: tsc's
            // createUnionOrIntersectionProperty skips members whose apparent type is never)
            if (member->kind == TY_INTERSECTION && (ty_reduced(me, ty_apparent_type(me, member))->flags & TYF_NEVER)) continue;
            // (Nor one whose apparent type is the error type: `T | { foo: 1 }` with T constrained to an unresolved type
            // reads 1)
            if (ty_union_member_apparent_is_error(me, member)) continue;
            Ty const* part = access(me, member, index, depth + 1, flags);
            // A tuple too short to have the element has undefined there when others have it (tsc's
            // createUnionOrIntersectionProperty: its index signature applies, and the rest element or undefined is read)
            // (A numeric name too: `T["1"]`)
            bool numeric_name = (index->flags & TYF_NUMBER_LITERAL) || ((index->flags & TYF_STRING_LITERAL) &&
                !(index->flags & TYF_ENUM_LITERAL) && index->literal.string && ty_is_numeric_name(index->literal.string));
            if (!part && numeric_name && ty_tuple_shape(ty_apparent_type(me, member))) {
                part = ty_intrinsic(me, TY_UNDEFINED);
                padded = true;
                if (len < 64) padded_tuples |= 1ull << len;
            } else if (!part && (index->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) &&
                ty_is_object_literal_without_spread(me, member)
            ) {
                // An object literal without it reads undefined (the same rule: `(params ?? {}).target`)
                part = ty_intrinsic(me, TY_UNDEFINED);
                padded = true;
            } else if (part) {
                found_in_some = true;
            }
            if (part) parts[len++] = part; else missing = true;
        }
        // But only a member's own property makes a union property (tsc's createUnionOrIntersectionProperty): when the
        // others only have index signatures (`(string[] | [])[1]`), the union's index signatures are read, which are
        // a tuple's element type. (Of a union of tuples only, each tuple's rest element or undefined is read: tsc's
        // getPropertyTypeForIndexType with everyType(isTupleType))
        if (padded_tuples && found_in_some && !missing) {
            bool own = false, all_tuples = true;
            for (u32 i = 0; i < object->composite.types.len && !own; i++) {
                Ty const* member = object->composite.types.data[i];
                if (member->flags & TYF_NULLABLE) continue;
                own = reads_own_prop(me, member, index);
                all_tuples &= ty_tuple_shape(ty_apparent_type(me, member)) != nullptr;
            }
            own |= all_tuples;
            for (u32 i = 0, k = 0; !own && i < object->composite.types.len; i++) {
                Ty const* member = object->composite.types.data[i];
                if (member->flags & TYF_NULLABLE) continue;
                if (k < 64 && (padded_tuples & (1ull << k))) {
                    Ty const* element = access(me, member, ty_intrinsic(me, TY_NUMBER), depth + 1, flags);
                    if (element) parts[k] = element;
                }
                k++;
            }
        }
        missing |= padded && !found_in_some;
        if (!missing && len) ret = ty_union(me, parts, len, nullptr, (TyList){0});
        if (ret && !padded && !union_index_applies(me, object, index, flags)) ret = nullptr;
        free(parts);
        Atom const* name = ret ? ty_literal_name(me, index) : nullptr;
        if (name && ty_union_non_public_conflict(me, object, name)) ret = nullptr;
        if (!ret && symbol_string) ret = access(me, object, ty_intrinsic(me, TY_STRING), depth + 1, flags);
        return ret;
    }

    object = ty_apparent_type(me, object);
    // (Reduced: a type parameter constrained to `{ a: "foo" } & { a: "bar" }` reads never, tsc's getReducedApparentType
    // and getPropertyTypeForIndexType of never)
    if (object->kind == TY_INTERSECTION) object = ty_reduced(me, object);
    if (object->flags & (TYF_ANY | TYF_NEVER)) return object;
    if (object->kind == TY_UNION) return access(me, object, index, depth + 1, flags); // The constraint of a type parameter
    if (!(object->flags & (TYF_OBJECT | TYF_INTERSECTION))) return nullptr;

    // `T["0"]` is `T[0]` for a list (mapped types over tuples index with the property name)
    if ((index->flags & TYF_STRING_LITERAL) && (ty_is_tuple(object) || ty_is_array(me, object))) {
        csview name = atom_sv(index->literal.string);
        bool digits = name.size > 0 && name.size < 10 && (name.size == 1 || name.buf[0] != '0');
        double n = 0;
        for (isize_t i = 0; digits && i < name.size; i++) {
            digits = name.buf[i] >= '0' && name.buf[i] <= '9';
            n = n * 10 + (name.buf[i] - '0');
        }
        if (digits) {
            Value value = value_number(n);
            index = ty_literal(me, &value);
        }
    }
    bool numeric = (index->flags & (TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM)) != 0;
    // (The empty tuple `[]` is its own target: it has no elements either)
    if ((index->flags & TYF_NUMBER_LITERAL) && !ty_is_tuple(object) && ty_tuple_shape(object)) return nullptr;
    if (ty_is_tuple(object) && (index->flags & TYF_NUMBER_LITERAL)) {
        double n = index->literal.value.number;
        TupleShape const* shape = object->reference.target->declared.tuple;
        TyList elements = ty_reference_args(me, object);
        if (n >= 0 && n == (u32)n) {
            u32 at = (u32)n;
            u32 fixed = ty_tuple_fixed_length(shape);
            if (at < fixed) {
                return elements.data[at];
            }
            // Past the leading fixed elements of a tuple with a rest element: any of the elements from there on
            // (tsc's getTupleElementTypeOutOfStartCount)
            if (fixed < shape->arity) {
                Ty const* rest = ty_tuple_slice_element_type(me, object, fixed, 0, false);
                // Only past as many elements as the tuple surely has, the trailing fixed ones included (tsc's
                // getTotalFixedElementCount)
                u32 end_fixed = 0;
                while (end_fixed < shape->arity - fixed && !(shape->flags[shape->arity - 1 - end_fixed] & TUPLE_ELEMENT_REST)) end_fixed++;
                return include_undefined && at >= fixed + end_fixed ? with_undefined(me, rest) : rest;
            }
            return nullptr;
        }
        // A negative index of a tuple without a rest element finds nothing (TS2514), with an access node (tsc's
        // getPropertyTypeForIndexType)
        if (n < 0 && !(flags & ACCESS_NO_NODE) && ty_tuple_fixed_length(shape) == shape->arity) return nullptr;
    }
    // (Elements by number as the lib's Array declares them: with noLib and an Array interface without a number index
    // signature, an array has none, tsc reads it off the interface)
    if (numeric && (ty_is_tuple(object) || ty_is_array(me, object)) &&
        (!compiler_options()->no_lib || ty_is_tuple(object) ||
            ty_applicable_index_info(me, object, ty_intrinsic(me, TY_NUMBER)))
    ) {
        Ty const* element = ty_rest_element_type(me, object);
        return include_undefined ? with_missing(me, element) : element;
    }

    Atom const* name = ty_literal_name(me, index);
    if (object->kind == TY_INTERSECTION && (index->flags & TYF_NUMBER_LITERAL)) {
        // The element of a tuple member is a property (tsc's tuples have "0", "1", ...), and the index signatures of
        // the other members don't take part
        TyList parts = object->composite.types;
        Ty const** found = malloc(sizeof(Ty const*) * parts.len);
        panic_if(!found, "out of memory");
        u32 len = 0;
        bool some_tuple = false;
        for (u32 i = 0; i < parts.len; i++) {
            Ty const* part = ty_apparent_type(me, parts.data[i]);
            if (ty_is_tuple(part)) {
                some_tuple = true;
                Ty const* element = access(me, part, index, depth + 1, flags);
                if (element) found[len++] = element;
            } else if (name) {
                FoundProp prop = ty_find_prop(me, part, name);
                if (prop.found) found[len++] = prop.type;
            }
        }
        Ty const* ret = some_tuple && len ? ty_intersection(me, found, len, nullptr, (TyList){0}) : nullptr;
        free(found);
        if (ret) return ret;
    }
    if (name) {
        FoundProp prop = ty_find_prop(me, object, name);
        if (prop.found) return prop.type;
    }
    Ty const* value = access_index_infos(me, object, index);
    // A symbol reads the string index signature when none applies (tsc's getPropertyTypeForIndexType), an
    // intersection's too
    if (!value && (flags & ACCESS_SYMBOL_STRING) && (ty_key_flags(index) & TYF_ES_SYMBOL_LIKE)) {
        value = access_index_infos(me, object, ty_intrinsic(me, TY_STRING));
    }
    // (Not an enum object indexed by its own members: `E[E.A]`)
    bool own_enum = (index->flags & TYF_ENUM_LITERAL) && object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_ENUM_OBJECT &&
        index->literal.enum_decl == object->declared.symbol;
    return value && include_undefined && !own_enum ? with_missing(me, value) : value;
}

// What an iterated element out of bounds may be (tsc's includeUndefinedInIndexSignature with
// IterationUse.PossiblyOutOfBounds): undefined too with noUncheckedIndexedAccess
Ty const* ty_possibly_out_of_bounds(Checker* me, Ty const* element) {
    return element && compiler_options()->no_unchecked_indexed_access ? with_missing(me, element) : element;
}

Ty const* ty_access_reading(Checker* me, Ty const* object, Ty const* index) {
    return access(me, object, ty_regular(me, index), 0, compiler_options()->no_unchecked_indexed_access ? ACCESS_INCLUDE_UNDEFINED : 0);
}

Ty const* ty_access(Checker* me, Ty const* object, Ty const* index) {
    return access(me, object, ty_regular(me, index), 0, 0); // `o["a"]`: keys are regular
}

Ty const* ty_access_own_named_prop(Checker* me, Ty const* object, Atom const* name) {
    if (object->kind == TY_UNION) return nullptr;
    object = ty_apparent_type(me, object);
    // (access()'s path for a name on a plain object type: lists read digits as elements, intersections and unions
    // combine their members)
    if (!(object->flags & TYF_OBJECT) || object->kind == TY_UNION || object->kind == TY_INTERSECTION) return nullptr;
    if (ty_tuple_shape(object) || ty_is_array(me, object)) return nullptr;
    FoundProp prop = ty_find_prop(me, object, name);
    if (!prop.found) return nullptr;
    return prop.type;
}

Ty const* ty_access_without_node_include_undefined(Checker* me, Ty const* object, Ty const* index, bool include_undefined) {
    return access(me, object, ty_regular(me, index), 0, ACCESS_NO_NODE | ACCESS_SYMBOL_STRING | (include_undefined ? ACCESS_INCLUDE_UNDEFINED : 0));
}

Ty const* ty_access_without_node(Checker* me, Ty const* object, Ty const* index) {
    return access(me, object, ty_regular(me, index), 0, ACCESS_NO_NODE | ACCESS_SYMBOL_STRING);
}

// `object[index]` as an element access expression reads it: a symbol key reads a string index signature too (TS2538)
Ty const* ty_access_element(Checker* me, Ty const* object, Ty const* index, bool reading) {
    u32 flags = ACCESS_SYMBOL_STRING | (reading && compiler_options()->no_unchecked_indexed_access ? ACCESS_INCLUDE_UNDEFINED : 0);
    return access(me, object, ty_regular(me, index), 0, flags);
}

// Whether `object[index]` finds something only because a symbol key read the string index signature: tsc's
// getPropertyTypeForIndexType reports TS2538 at the access node then
bool ty_symbol_reads_string_index(Checker* me, Ty const* object, Ty const* index) {
    index = ty_regular(me, index);
    if (!(ty_key_flags(index) & TYF_ES_SYMBOL_LIKE) || (index->flags & TYF_ANY)) return false;
    return access(me, object, index, 0, ACCESS_SYMBOL_STRING) && !access(me, object, index, 0, 0);
}

// The type a write to `object[index]` is checked against when it isn't what a read gets: a divergent accessor's
// setter parameter type (tsc's getWriteTypeOfSymbol). Null if there's nothing special about writing it
Ty const* ty_access_write_type(Checker* me, Ty const* object, Ty const* index) {
    Atom const* name = ty_literal_name(me, ty_regular(me, index));
    if (!name) return nullptr;
    if (object->kind == TY_UNION && !(object->flags & TYF_BOOLEAN)) {
        // The union of what each member takes, if one of them has a write type of its own (tsc's
        // createUnionOrIntersectionProperty)
        TyList members = object->composite.types;
        Ty const** parts = malloc(sizeof(Ty const*) * members.len);
        u32 len = 0;
        bool divergent = false, complete = true;
        for (u32 i = 0; i < members.len && complete; i++) {
            if (members.data[i]->flags & TYF_NULLABLE) continue;
            Ty const* write = ty_access_write_type(me, members.data[i], index);
            divergent |= write != nullptr;
            if (!write) write = ty_access(me, members.data[i], index);
            if (write) parts[len++] = write; else complete = false;
        }
        Ty const* ret = divergent && complete && len ? ty_union(me, parts, len, nullptr, (TyList){0}) : nullptr;
        free(parts);
        return ret;
    }
    object = ty_apparent_type(me, object);
    if (object->kind == TY_INTERSECTION) {
        // The intersection of what the members that have it take
        TyList members = object->composite.types;
        Ty const** parts = malloc(sizeof(Ty const*) * members.len);
        u32 len = 0;
        bool divergent = false;
        for (u32 i = 0; i < members.len; i++) {
            Ty const* member = ty_apparent_type(me, members.data[i]);
            if (!(member->flags & TYF_OBJECT) || !ty_find_prop(me, member, name).found) continue;
            Ty const* write = ty_access_write_type(me, member, index);
            divergent |= write != nullptr;
            parts[len++] = write ? write : ty_access(me, member, index);
        }
        Ty const* ret = divergent && len ? ty_intersection(me, parts, len, nullptr, (TyList){0}) : nullptr;
        free(parts);
        return ret;
    }
    if (!(object->flags & TYF_OBJECT)) return nullptr;
    FoundProp prop = ty_find_prop(me, object, name);
    if (!prop.found || !prop.prop) return nullptr;
    Ty const* write = prop_write_type(me, prop.prop);
    // (Under exactOptionalPropertyTypes an optional property takes its declared type: tsc's removeMissingType of a
    // Definite assignment target)
    if (write && (prop.flags & PROP_OPTIONAL) && compiler_options()->exact_optional_property_types) return write;
    return write && (prop.flags & PROP_OPTIONAL) ? with_undefined(me, write) : write;
}
