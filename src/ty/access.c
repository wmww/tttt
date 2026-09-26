#include "ty/ty_internal.h"
#include "error.h"

// Property and element access: the type of `object[index]` for an index that is known (a literal, `string`, `number`,
// `symbol`), through apparent types like tsc's getPropertyOfType/getIndexedAccessType on non-generic operands. A type
// parameter is accessed through its constraint.

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

// The index signature of `object` that applies to `index`, null if none
// Whether a specific (not `string`) index signature applies to a key (tsc's isApplicableIndexType): a numeric one to
// numbers and numeric strings, a symbol one to symbols, a pattern (`[k: `data-${string}`]`) to what it matches
bool ty_specific_index_applies(Checker* me, IndexInfo const* info, Ty const* index) {
    u32 key_flags = ty_key_flags(index);
    bool stringish = (key_flags & TYF_STRING_LIKE) != 0;
    bool numeric = (key_flags & (TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM)) != 0 ||
        ((index->flags & TYF_STRING_LITERAL) && index->literal.string && ty_is_numeric_name(index->literal.string));
    return ((info->key->flags & TYF_NUMBER) && numeric) || ((info->key->flags & TYF_ES_SYMBOL) && (key_flags & TYF_ES_SYMBOL_LIKE)) ||
        ((info->key->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING) || info->key->kind == TY_INTERSECTION) &&
            (stringish || numeric || info->key->kind == TY_INTERSECTION) &&
            ty_is_related(me, index, info->key, TY_RELATION_ASSIGNABLE));
}

IndexInfo const* ty_applicable_index_info(Checker* me, Ty const* object, Ty const* index) {
    if (object->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < object->composite.types.len; i++) {
            IndexInfo const* found = ty_applicable_index_info(me, ty_apparent_type(me, object->composite.types.data[i]), index);
            if (found) return found;
        }
        return nullptr;
    }
    Members const* members = ty_members(me, object);
    // The specific ones first. Several of them apply as one, whose value is the intersection of theirs (tsc's
    // findApplicableIndexInfo)
    IndexInfo const* found = nullptr;
    TyBuf values = {0};
    bool readonly = true;
    for (u32 i = 0; i < members->index_count; i++) {
        IndexInfo const* info = &members->index[i];
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
    bool stringish_or_numeric = (key_flags & (TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM)) != 0;
    for (u32 i = 0; i < members->index_count; i++) {
        IndexInfo const* info = &members->index[i];
        if ((info->key->flags & TYF_STRING) && stringish_or_numeric) return info;
    }
    return nullptr;
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

static Ty const* access(Checker* me, Ty const* object, Ty const* index, u32 depth) {
    if (depth > 16) return nullptr;
    if (index->kind == TY_UNION && !(index->flags & TYF_BOOLEAN)) {
        Ty const** parts = malloc(sizeof(Ty const*) * index->composite.types.len);
        Ty const* ret = nullptr;
        u32 len = 0;
        for (u32 i = 0; i < index->composite.types.len; i++) {
            Ty const* part = access(me, object, index->composite.types.data[i], depth + 1);
            if (!part) break;
            parts[len++] = part;
        }
        if (len == index->composite.types.len) ret = ty_union(me, parts, len, nullptr, (TyList){0});
        free(parts);
        return ret;
    }
    if (object->flags & TYF_ANY) return object;
    if (object->flags & TYF_NEVER) return object;
    if (index->flags & TYF_ANY) return ty_intrinsic(me, TY_ANY);
    bool simple = (ty_key_flags(index) & (TYF_STRING_LIKE | TYF_NUMBER | TYF_ES_SYMBOL_LIKE | TYF_NUMBER_LITERAL | TYF_ENUM)) != 0;
    if (!simple) return nullptr;

    if (object->kind == TY_UNION && !(object->flags & TYF_BOOLEAN)) {
        // Every member has to have it. null and undefined don't count: what to say about them is the caller's business
        Ty const** parts = malloc(sizeof(Ty const*) * object->composite.types.len);
        Ty const* ret = nullptr;
        u32 len = 0;
        bool missing = false, short_tuple = false, found_in_some = false;
        for (u32 i = 0; i < object->composite.types.len && !missing; i++) {
            Ty const* member = object->composite.types.data[i];
            if (member->flags & TYF_NULLABLE) continue;
            Ty const* part = access(me, member, index, depth + 1);
            // A tuple too short to have the element has undefined there when others have it (tsc's
            // createUnionOrIntersectionProperty: its index signature applies, and the rest element or undefined is read)
            if (!part && (index->flags & TYF_NUMBER_LITERAL) && ty_is_tuple(ty_apparent_type(me, member))) {
                part = ty_intrinsic(me, TY_UNDEFINED);
                short_tuple = true;
            } else if (part) {
                found_in_some = true;
            }
            if (part) parts[len++] = part; else missing = true;
        }
        missing |= short_tuple && !found_in_some;
        if (!missing && len) ret = ty_union(me, parts, len, nullptr, (TyList){0});
        free(parts);
        Atom const* name = ret ? ty_literal_name(me, index) : nullptr;
        if (name && ty_union_non_public_conflict(me, object, name)) ret = nullptr;
        return ret;
    }

    object = ty_apparent_type(me, object);
    if (object->flags & TYF_ANY) return object;
    if (object->kind == TY_UNION) return access(me, object, index, depth + 1); // The constraint of a type parameter
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
            if (at < shape->arity && !(shape->flags[at] & TUPLE_ELEMENT_REST)) {
                return shape->flags[at] & TUPLE_ELEMENT_OPTIONAL ? with_undefined(me, elements.data[at]) : elements.data[at];
            }
            if (shape->arity && (shape->flags[shape->arity - 1] & TUPLE_ELEMENT_REST)) {
                return ty_rest_element_type(me, elements.data[shape->arity - 1]);
            }
            return nullptr;
        }
    }
    if (numeric && (ty_is_tuple(object) || ty_is_array(me, object))) return ty_rest_element_type(me, object);

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
                Ty const* element = access(me, part, index, depth + 1);
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
    return access_index_infos(me, object, index);
}

Ty const* ty_access(Checker* me, Ty const* object, Ty const* index) {
    return access(me, object, ty_regular(me, index), 0); // `o["a"]`: keys are regular
}
