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

Ty const* ty_tuple_slice_element_type(Checker* me, Ty const* tuple, u32 from, u32 end_skip) {
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
            // (A variadic `...T` is T[number])
            element = element->kind == TY_TYPE_PARAM ? ty_indexed(me, element, ty_intrinsic(me, TY_NUMBER)) :
                ty_rest_element_type(me, element);
        }
        parts[i] = element;
    }
    Ty const* ret = ty_union(me, parts, len, nullptr, (TyList){0});
    if (parts != stack) free(parts);
    return ret;
}

static Ty const* with_undefined(Checker* me, Ty const* type) {
    if (!compiler_options()->strict_null_checks) return type;
    Ty const* parts[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
    return ty_union(me, parts, 2, nullptr, (TyList){0});
}

Atom const* ty_literal_name(Checker* me, Ty const* index) {
    if (!(index->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL))) return nullptr;
    if (index->literal.string) return index->literal.string;
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
    // No access node (tsc's getIndexedAccessTypeOrUndefined for a constraint): a symbol key that no index signature
    // applies to reads the string index signature
    ACCESS_NO_NODE = 1 << 1,
};

// ACCESS_INCLUDE_UNDEFINED: what an index signature or the rest of a tuple gives includes undefined (noUncheckedIndexedAccess, tsc's
// AccessFlags.IncludeUndefined)
static Ty const* access(Checker* me, Ty const* object, Ty const* index, u32 depth, u32 flags) {
    bool include_undefined = (flags & ACCESS_INCLUDE_UNDEFINED) != 0;
    if (depth > 16) return nullptr;
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
    if (object->flags & TYF_NEVER) return object;
    if (index->flags & TYF_NEVER) {
        // What the index signatures say (never is assignable to every key type), else never (tsc's
        // getPropertyTypeForIndexType)
        Ty const* apparent = ty_apparent_type(me, object);
        if (apparent->flags & TYF_ANY) return apparent;
        IndexInfo const* info = (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) ? ty_applicable_index_info(me, apparent, index) : nullptr;
        if (!info) return index;
        return include_undefined ? with_undefined(me, info->value) : info->value;
    }
    if (index->flags & TYF_ANY) {
        // What the index signatures say (any is assignable to every key type: the specific ones, else a string one,
        // tsc's getPropertyTypeForIndexType with getApplicableIndexInfo)
        Ty const* apparent = ty_apparent_type(me, object);
        IndexInfo const* info = (apparent->flags & TYF_OBJECT) ? ty_applicable_index_info(me, apparent, index) : nullptr;
        if (!info) return ty_intrinsic(me, TY_ANY);
        return include_undefined ? with_undefined(me, info->value) : info->value;
    }
    bool simple = (ty_key_flags(index) & (TYF_STRING_LIKE | TYF_NUMBER | TYF_ES_SYMBOL_LIKE | TYF_NUMBER_LITERAL | TYF_ENUM)) != 0;
    if (!simple) return nullptr;

    if (object->kind == TY_UNION && !(object->flags & TYF_BOOLEAN)) {
        // Every member has to have it. null and undefined don't count: what to say about them is the caller's business
        Ty const** parts = malloc(sizeof(Ty const*) * object->composite.types.len);
        Ty const* ret = nullptr;
        u32 len = 0;
        bool missing = false, padded = false, found_in_some = false;
        for (u32 i = 0; i < object->composite.types.len && !missing; i++) {
            Ty const* member = object->composite.types.data[i];
            if (member->flags & TYF_NULLABLE) continue;
            Ty const* part = access(me, member, index, depth + 1, flags);
            // A tuple too short to have the element has undefined there when others have it (tsc's
            // createUnionOrIntersectionProperty: its index signature applies, and the rest element or undefined is read)
            if (!part && (index->flags & TYF_NUMBER_LITERAL) && ty_is_tuple(ty_apparent_type(me, member))) {
                part = ty_intrinsic(me, TY_UNDEFINED);
                padded = true;
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
        missing |= padded && !found_in_some;
        if (!missing && len) ret = ty_union(me, parts, len, nullptr, (TyList){0});
        free(parts);
        Atom const* name = ret ? ty_literal_name(me, index) : nullptr;
        if (name && ty_union_non_public_conflict(me, object, name)) ret = nullptr;
        return ret;
    }

    object = ty_apparent_type(me, object);
    if (object->flags & TYF_ANY) return object;
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
    if (ty_is_tuple(object) && (index->flags & TYF_NUMBER_LITERAL)) {
        double n = index->literal.value.number;
        TupleShape const* shape = object->reference.target->declared.tuple;
        TyList elements = ty_reference_args(me, object);
        if (n >= 0 && n == (u32)n) {
            u32 at = (u32)n;
            u32 fixed = ty_tuple_fixed_length(shape);
            if (at < fixed) {
                return shape->flags[at] & TUPLE_ELEMENT_OPTIONAL ? with_undefined(me, elements.data[at]) : elements.data[at];
            }
            // Past the leading fixed elements of a tuple with a rest element: any of the elements from there on
            // (tsc's getTupleElementTypeOutOfStartCount)
            if (fixed < shape->arity) {
                Ty const* rest = ty_tuple_slice_element_type(me, object, fixed, 0);
                return include_undefined ? with_undefined(me, rest) : rest;
            }
            return nullptr;
        }
    }
    if (numeric && (ty_is_tuple(object) || ty_is_array(me, object))) {
        Ty const* element = ty_rest_element_type(me, object);
        return include_undefined ? with_undefined(me, element) : element;
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
                if (prop.found) found[len++] = prop.flags & PROP_OPTIONAL ? with_undefined(me, prop.type) : prop.type;
            }
        }
        Ty const* ret = some_tuple && len ? ty_intersection(me, found, len, nullptr, (TyList){0}) : nullptr;
        free(found);
        if (ret) return ret;
    }
    if (name) {
        FoundProp prop = ty_find_prop(me, object, name);
        if (prop.found) return prop.flags & PROP_OPTIONAL ? with_undefined(me, prop.type) : prop.type;
    }
    Ty const* value = access_index_infos(me, object, index);
    if (!value && (flags & ACCESS_NO_NODE) && (ty_key_flags(index) & TYF_ES_SYMBOL_LIKE) && (object->flags & TYF_OBJECT)) {
        Members const* members = ty_members(me, object);
        for (u32 i = 0; i < members->index_count && !value; i++) {
            if (members->index[i].key->flags & TYF_STRING) value = members->index[i].value;
        }
    }
    // (Not an enum object indexed by its own members: `E[E.A]`)
    bool own_enum = (index->flags & TYF_ENUM_LITERAL) && object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_ENUM_OBJECT;
    return value && include_undefined && !own_enum ? with_undefined(me, value) : value;
}

Ty const* ty_access_reading(Checker* me, Ty const* object, Ty const* index) {
    return access(me, object, ty_regular(me, index), 0, compiler_options()->no_unchecked_indexed_access ? ACCESS_INCLUDE_UNDEFINED : 0);
}

Ty const* ty_access(Checker* me, Ty const* object, Ty const* index) {
    return access(me, object, ty_regular(me, index), 0, 0); // `o["a"]`: keys are regular
}

Ty const* ty_access_without_node(Checker* me, Ty const* object, Ty const* index) {
    return access(me, object, ty_regular(me, index), 0, ACCESS_NO_NODE);
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
    return write && (prop.flags & PROP_OPTIONAL) ? with_undefined(me, write) : write;
}
