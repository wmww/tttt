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

static Atom const* index_name(Checker* me, Ty const* index) {
    if (!(index->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL))) return nullptr;
    if (index->literal.string) return index->literal.string;
    cstr rendered = cstr_null;
    cstr_append_value_js(&rendered, &index->literal.value);
    Atom const* name = checker_atom(me, cstr_sv(&rendered));
    cstr_drop(&rendered);
    return name;
}

static Ty const* access_index_infos(Checker* me, Ty const* object, Ty const* index) {
    if (object->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < object->composite.types.len; i++) {
            Ty const* found = access_index_infos(me, ty_apparent_type(me, object->composite.types.data[i]), index);
            if (found) return found;
        }
        return nullptr;
    }
    Members const* members = ty_members(me, object);
    bool stringish = (index->flags & (TYF_STRING | TYF_STRING_LITERAL)) != 0;
    bool numeric = (index->flags & (TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM)) != 0;
    // A numeric index signature is more specific than a string one
    for (u32 pass = 0; pass < 2; pass++) {
        for (u32 i = 0; i < members->index_count; i++) {
            IndexInfo const* info = &members->index[i];
            bool applies = pass == 0 ?
                ((info->key->flags & TYF_NUMBER) && numeric) || ((info->key->flags & TYF_ES_SYMBOL) && (index->flags & TYF_ES_SYMBOL)) :
                (info->key->flags & TYF_STRING) && (stringish || numeric);
            if (applies) return info->value;
        }
    }
    return nullptr;
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
    bool simple = (index->flags & (TYF_STRING | TYF_NUMBER | TYF_ES_SYMBOL | TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_ENUM)) != 0;
    if (!simple) return nullptr;

    if (object->kind == TY_UNION && !(object->flags & TYF_BOOLEAN)) {
        // Every member has to have it. null and undefined don't count: what to say about them is the caller's business
        Ty const** parts = malloc(sizeof(Ty const*) * object->composite.types.len);
        Ty const* ret = nullptr;
        u32 len = 0;
        bool missing = false;
        for (u32 i = 0; i < object->composite.types.len && !missing; i++) {
            Ty const* member = object->composite.types.data[i];
            if (member->flags & TYF_NULLABLE) continue;
            Ty const* part = access(me, member, index, depth + 1);
            if (part) parts[len++] = part; else missing = true;
        }
        if (!missing && len) ret = ty_union(me, parts, len, nullptr, (TyList){0});
        free(parts);
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

    Atom const* name = index_name(me, index);
    if (name) {
        FoundProp prop = ty_find_prop(me, object, name);
        if (prop.found) return prop.flags & PROP_OPTIONAL ? with_undefined(me, prop.type) : prop.type;
    }
    return access_index_infos(me, object, index);
}

Ty const* ty_access(Checker* me, Ty const* object, Ty const* index) {
    return access(me, object, index, 0);
}
