#include "ty/ty_internal.h"

// Prints a Ty the way tsc's typeToString() does, in its order of preference: alias name, reference name + arguments
// (array and tuple syntax for those targets), declared name, structure.

#define MAX_DISPLAY_DEPTH 8

static void out(TyDisplay d, csview sv) { cstr_append_sv(d.target, sv); }
#define out_lit(d, s) out(d, c_sv(s))

static void display_list(Checker* me, TyDisplay d, TyList list) {
    for (u32 i = 0; i < list.len; i++) {
        if (i) out_lit(d, ", ");
        ty_display(me, d, list.data[i]);
    }
}

// A declared name. HACK(plan 03): tsc qualifies a name as far as needed from where it's printed, which needs symbols.
// Error messages always qualify (so does tsc there), the `.types` baselines never do (right more often than not)
static void out_name(TyDisplay d, csview qualified) {
    if (d.unqualified_names) {
        isize_t dot = qualified.size;
        while (dot > 0 && qualified.buf[dot - 1] != '.') dot--;
        qualified = csview_from_n(qualified.buf + dot, qualified.size - dot);
    }
    out(d, qualified);
}

static void display_string_literal(TyDisplay d, csview sv) {
    out_lit(d, "\"");
    for (isize_t i = 0; i < sv.size; i++) {
        char c = sv.buf[i];
        switch (c) {
            case '"': out_lit(d, "\\\""); break;
            case '\\': out_lit(d, "\\\\"); break;
            case '\n': out_lit(d, "\\n"); break;
            case '\r': out_lit(d, "\\r"); break;
            case '\t': out_lit(d, "\\t"); break;
            default: out(d, csview_from_n(&c, 1)); break;
        }
    }
    out_lit(d, "\"");
}

static bool is_identifier_name(csview sv) {
    if (!sv.size || (sv.buf[0] >= '0' && sv.buf[0] <= '9')) return false;
    for (isize_t i = 0; i < sv.size; i++) {
        char c = sv.buf[i];
        bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
            c == '#' || (unsigned char)c >= 0x80;
        if (!valid) return false;
    }
    return true;
}

static bool is_numeric_name(csview sv) {
    if (!sv.size) return false;
    for (isize_t i = 0; i < sv.size; i++) {
        if (sv.buf[i] < '0' || sv.buf[i] > '9') return false;
    }
    return sv.size == 1 || sv.buf[0] != '0';
}

static void display_prop_name(TyDisplay d, Atom const* name) {
    csview sv = atom_sv(name);
    if (is_identifier_name(sv) || is_numeric_name(sv)) {
        out(d, sv);
    } else {
        display_string_literal(d, sv);
    }
}

static void display_type_params(Checker* me, TyDisplay d, TyList params) {
    if (!params.len) return;
    out_lit(d, "<");
    for (u32 i = 0; i < params.len; i++) {
        if (i) out_lit(d, ", ");
        ty_display(me, d, params.data[i]);
        Ty const* constraint = ty_param_constraint(me, params.data[i]);
        if (constraint) {
            out_lit(d, " extends ");
            ty_display(me, d, constraint);
        }
        Ty const* fallback = ty_param_default(me, params.data[i]);
        if (fallback) {
            out_lit(d, " = ");
            ty_display(me, d, fallback);
        }
    }
    out_lit(d, ">");
}

// `(a: A, b?: B, ...rest: C[])` followed by `separator` and the return type
static void display_signature(Checker* me, TyDisplay d, Signature const* sig, char const* separator, bool with_new);

void ty_display_signature(Checker* me, TyDisplay d, Signature const* sig) {
    display_signature(me, d, sig, ": ", (sig->flags & SIGNATURE_CONSTRUCT) != 0);
}

static void display_signature(Checker* me, TyDisplay d, Signature const* sig, char const* separator, bool with_new) {
    d.widen_literals = false;
    signature_resolve(me, sig);
    if (with_new) {
        if (sig->flags & SIGNATURE_ABSTRACT) out_lit(d, "abstract ");
        out_lit(d, "new ");
    }
    display_type_params(me, d, sig->type_params);
    out_lit(d, "(");
    for (u32 i = 0; i < sig->param_count; i++) {
        SigParam const* param = &sig->params[i];
        if (i) out_lit(d, ", ");
        if (param->rest) out_lit(d, "...");
        out(d, atom_sv(param->name));
        if (param->optional) out_lit(d, "?");
        out_lit(d, ": ");
        ty_display(me, d, param->type);
    }
    out_lit(d, ")");
    out(d, csview_from(separator));
    ty_display(me, d, signature_return_type(me, sig));
}

static void display_mapped(Checker* me, TyDisplay d, MappedInfo const* mapped) {
    out_lit(d, "{ ");
    if (mapped->readonly) out(d, mapped->readonly > 0 ? c_sv("readonly ") : c_sv("-readonly "));
    out_lit(d, "[");
    ty_display(me, d, mapped->type_param);
    out_lit(d, " in ");
    ty_display(me, d, mapped->constraint);
    if (mapped->name_type) {
        out_lit(d, " as ");
        ty_display(me, d, mapped->name_type);
    }
    out_lit(d, "]");
    if (mapped->optional) out(d, mapped->optional > 0 ? c_sv("?") : c_sv("-?"));
    out_lit(d, ": ");
    ty_display(me, d, mapped->template);
    out_lit(d, "; }");
}

static void display_structure(Checker* me, TyDisplay d, Ty const* ty) {
    Members const* members = ty_members(me, ty);
    if (members->mapped) {
        display_mapped(me, d, members->mapped);
        return;
    }
    bool only_signatures = !members->prop_count && !members->index_count;
    if (only_signatures && members->call_count == 1 && !members->construct_count) {
        display_signature(me, d, members->call[0], " => ", false);
        return;
    }
    if (only_signatures && members->construct_count == 1 && !members->call_count) {
        display_signature(me, d, members->construct[0], " => ", true);
        return;
    }
    if (only_signatures && !members->call_count && !members->construct_count) {
        out_lit(d, "{}");
        return;
    }
    if (d.depth > MAX_DISPLAY_DEPTH) {
        out_lit(d, "...");
        return;
    }
    out_lit(d, "{ ");
    for (u32 i = 0; i < members->call_count; i++) {
        display_signature(me, d, members->call[i], ": ", false);
        out_lit(d, "; ");
    }
    for (u32 i = 0; i < members->construct_count; i++) {
        display_signature(me, d, members->construct[i], ": ", true);
        out_lit(d, "; ");
    }
    for (u32 i = 0; i < members->index_count; i++) {
        IndexInfo const* info = &members->index[i];
        if (info->readonly) out_lit(d, "readonly ");
        out_lit(d, "[");
        out(d, info->param_name ? atom_sv(info->param_name) : c_sv("x"));
        out_lit(d, ": ");
        ty_display(me, d, info->key);
        out_lit(d, "]: ");
        ty_display(me, d, info->value);
        out_lit(d, "; ");
    }
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members->props[i];
        Ty const* type = prop_type(me, prop);
        Members const* type_members = (prop->flags & PROP_METHOD) ? ty_members(me, type) : nullptr;
        if (type_members && type_members->call_count && !type_members->prop_count) {
            // Methods print once per signature
            for (u32 j = 0; j < type_members->call_count; j++) {
                display_prop_name(d, prop->name);
                if (prop->flags & PROP_OPTIONAL) out_lit(d, "?");
                display_signature(me, d, type_members->call[j], ": ", false);
                out_lit(d, "; ");
            }
            continue;
        }
        if (prop->flags & PROP_READONLY) out_lit(d, "readonly ");
        display_prop_name(d, prop->name);
        if (prop->flags & PROP_OPTIONAL) out_lit(d, "?");
        out_lit(d, ": ");
        ty_display(me, d, type);
        out_lit(d, "; ");
    }
    out_lit(d, "}");
}

static bool is_function_like(Checker* me, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS || ty->alias) return false;
    Members const* members = ty_members(me, ty);
    return !members->prop_count && !members->index_count && !members->mapped &&
        members->call_count + members->construct_count == 1;
}

static void display_parenthesized_if(Checker* me, TyDisplay d, Ty const* ty, bool parens) {
    if (parens) out_lit(d, "(");
    ty_display(me, d, ty);
    if (parens) out_lit(d, ")");
}

static bool needs_parens_in_array(Checker* me, Ty const* ty) {
    if (ty->alias) return false;
    if (ty->flags & TYF_BOOLEAN) return false;
    return ty->kind == TY_UNION || ty->kind == TY_INTERSECTION || ty->kind == TY_CONDITIONAL || ty->kind == TY_KEYOF ||
        is_function_like(me, ty);
}

static void display_tuple(Checker* me, TyDisplay d, TupleShape const* shape, TyList args) {
    if (shape->readonly) out_lit(d, "readonly ");
    out_lit(d, "[");
    for (u32 i = 0; i < shape->arity; i++) {
        if (i) out_lit(d, ", ");
        bool rest = shape->flags[i] & TUPLE_ELEMENT_REST;
        bool optional = shape->flags[i] & TUPLE_ELEMENT_OPTIONAL;
        if (rest) out_lit(d, "...");
        if (shape->names && shape->names[i]) {
            out(d, atom_sv(shape->names[i]));
            if (optional) out_lit(d, "?");
            out_lit(d, ": ");
            ty_display(me, d, args.data[i]);
        } else {
            display_parenthesized_if(me, d, args.data[i], optional && needs_parens_in_array(me, args.data[i]));
            if (optional) out_lit(d, "?");
        }
    }
    out_lit(d, "]");
}

static void display_union(Checker* me, TyDisplay d, Ty const* ty) {
    // tsc's formatUnionTypes(): `true | false` prints as `boolean`, `null` and `undefined` go last
    TyList types = ty->composite.types;
    bool first = true;
    u32 nullable = 0;
    for (u32 i = 0; i < types.len; i++) {
        Ty const* t = types.data[i];
        if (t->kind == TY_INTRINSIC && (t->flags & TYF_NULLABLE)) {
            nullable |= t->flags;
            continue;
        }
        if (!first) out_lit(d, " | ");
        first = false;
        if (t == ty_intrinsic(me, TY_FALSE) && i + 1 < types.len && types.data[i + 1] == ty_intrinsic(me, TY_TRUE)) {
            out_lit(d, "boolean");
            i++;
            continue;
        }
        // A run of all the members of an enum prints as the enum
        if (t->kind == TY_LITERAL && t->literal.enum_decl) {
            Ty const* enum_type = ty_enum_of_member(me, t);
            if (enum_type->kind == TY_UNION) {
                u32 count = enum_type->composite.types.len;
                if (i + count <= types.len && types.data[i + count - 1] == enum_type->composite.types.data[count - 1]) {
                    ty_display(me, d, enum_type);
                    i += count - 1;
                    continue;
                }
            }
        }
        display_parenthesized_if(me, d, t, is_function_like(me, t) || (t->kind == TY_CONDITIONAL && !t->alias));
    }
    if (nullable & TYF_NULL) {
        out(d, first ? c_sv("null") : c_sv(" | null"));
        first = false;
    }
    if (nullable & TYF_UNDEFINED) {
        out(d, first ? c_sv("undefined") : c_sv(" | undefined"));
    }
}

void ty_display(Checker* me, TyDisplay d, Ty const* ty) {
    d.depth++;
    if (d.depth > MAX_DISPLAY_DEPTH * 4) {
        out_lit(d, "...");
        return;
    }

    // Widening only reaches through the structure of expression types (temporaries lowered from the legacy checker)
    if (d.widen_literals && !(ty->kind == TY_LITERAL || ty->kind == TY_UNION ||
        (ty->kind == TY_ANONYMOUS && !ty->anonymous.node && !ty->anonymous.target))
    ) {
        d.widen_literals = false;
    }

    if (ty_is_enum_union(ty)) {
        out_name(d, atom_sv(ty_qualified_name(me, ty->alias)));
        return;
    }
    if (ty->alias) {
        out(d, ty->alias->name.sv);
        if (ty->alias_args.len) {
            out_lit(d, "<");
            display_list(me, d, ty->alias_args);
            out_lit(d, ">");
        }
        return;
    }

    switch ((TyKind)ty->kind) {
        case TY_INTRINSIC:
            out(d, ty->intrinsic.name);
            break;

        case TY_LITERAL:
            if (d.widen_literals && !ty->literal.enum_decl) {
                ty_display(me, (TyDisplay){ .target = d.target, .depth = d.depth, .visiting = d.visiting }, ty_widened_literal(me, ty));
                break;
            }
            if (ty->literal.enum_decl) {
                // The only member of an enum is the enum. TODO(fresh literals): tsc prints the *fresh* type of the
                // expression `E.x` as `E.x` even then (error messages); `.types` baselines show the widened `E`
                out_name(d, atom_sv(ty_qualified_name(me, ty->literal.enum_decl)));
                if (ty->literal.enum_member != ty->literal.enum_decl && ty_enum_of_member(me, ty) != ty) {
                    out_lit(d, ".");
                    out(d, ty->literal.enum_member->name.sv);
                }
            } else if (ty->literal.string) {
                display_string_literal(d, atom_sv(ty->literal.string));
            } else if (ty->flags & TYF_BOOLEAN_LITERAL) {
                out(d, ty->literal.value.boolean ? c_sv("true") : c_sv("false"));
            } else {
                cstr_append_value_js(d.target, &ty->literal.value);
                if (ty->flags & TYF_BIGINT_LITERAL) out_lit(d, "n");
            }
            break;

        case TY_TYPE_PARAM:
            out(d, atom_sv(ty->param.name));
            break;

        case TY_DECLARED:
            switch (ty->declared.kind) {
                case TY_DECLARED_CLASS_STATIC:
                case TY_DECLARED_ENUM_OBJECT:
                case TY_DECLARED_NAMESPACE:
                    out_lit(d, "typeof ");
                    out_name(d, atom_sv(ty->declared.name));
                    break;

                case TY_DECLARED_TUPLE_TARGET:
                    display_tuple(me, d, ty->declared.tuple, ty->declared.type_params);
                    break;

                default:
                    out_name(d, atom_sv(ty->declared.name));
                    if (ty->declared.type_params.len) {
                        out_lit(d, "<");
                        display_list(me, d, ty->declared.type_params);
                        out_lit(d, ">");
                    }
                    break;
            }
            break;

        case TY_REFERENCE: {
            Ty const* target = ty->reference.target;
            TyList args = ty_reference_args(me, ty);
            if (target->declared.kind == TY_DECLARED_TUPLE_TARGET) {
                display_tuple(me, d, target->declared.tuple, args);
            } else if (target == my array_target || target == my readonly_array_target) {
                if (target == my readonly_array_target) out_lit(d, "readonly ");
                display_parenthesized_if(me, d, args.data[0], needs_parens_in_array(me, args.data[0]));
                out_lit(d, "[]");
            } else {
                out_name(d, atom_sv(target->declared.name));
                out_lit(d, "<");
                display_list(me, d, args);
                out_lit(d, ">");
            }
        }   break;

        case TY_ANONYMOUS: {
            // An anonymous type inside its own rendering prints as `any`, like tsc's elided information placeholder
            // (`function f() { return f }` is `() => any`)
            for (TyDisplayVisit const* i = d.visiting; i; i = i->outer) {
                if (i->ty == ty) {
                    out_lit(d, "any");
                    return;
                }
            }
            TyDisplayVisit visit = { .ty = ty, .outer = d.visiting };
            d.visiting = &visit;
            display_structure(me, d, ty);
        }   break;

        case TY_UNION:
            display_union(me, d, ty);
            break;

        case TY_INTERSECTION:
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                Ty const* t = ty->composite.types.data[i];
                if (i) out_lit(d, " & ");
                display_parenthesized_if(me, d, t, !t->alias && (t->kind == TY_UNION || is_function_like(me, t)));
            }
            break;

        case TY_KEYOF:
            out_lit(d, "keyof ");
            display_parenthesized_if(me, d, ty->keyof.operand, needs_parens_in_array(me, ty->keyof.operand));
            break;

        case TY_INDEXED:
            display_parenthesized_if(me, d, ty->indexed.object, needs_parens_in_array(me, ty->indexed.object));
            out_lit(d, "[");
            ty_display(me, d, ty->indexed.index);
            out_lit(d, "]");
            break;

        case TY_CONDITIONAL:
            display_parenthesized_if(
                me, d, ty->conditional.check,
                is_function_like(me, ty->conditional.check) || ty->conditional.check->kind == TY_CONDITIONAL
            );
            out_lit(d, " extends ");
            display_parenthesized_if(me, d, ty->conditional.extends, ty->conditional.extends->kind == TY_CONDITIONAL);
            out_lit(d, " ? ");
            ty_display(me, d, ty_conditional_branch(me, ty, true));
            out_lit(d, " : ");
            ty_display(me, d, ty_conditional_branch(me, ty, false));
            break;
    }
}

cstr ty_to_cstr(Checker* me, Ty const* ty) {
    cstr ret = cstr_null;
    ty_display(me, (TyDisplay){ .target = &ret }, ty);
    return ret;
}
