#include "ty/ty_internal.h"
#include "enum_decl.h"
#include "scope.h"
#include "binding.h"
#include "expr.h"
#include <math.h>

// Enums. The type of an enum is the union of the types of its members (with the enum as the union's alias, which is
// how it prints as `E`); a member's type is a literal type that remembers the enum it belongs to, or, when its value
// isn't a constant, an opaque "computed" member type. An enum with a single member *is* that member's type, as in tsc.

typedef struct EnumValue {
    bool known;
    bool is_string;
    double number;
    cstr string; // owned
} EnumValue;

static EnumValue enum_value_number(double number) { return (EnumValue){ .known = true, .number = number }; }
static const EnumValue enum_value_unknown = {0};

static EnumValue evaluate(Checker* me, Expr const* expr, u32 depth);

static i32 to_int32(double number) {
    if (!isfinite(number)) return 0;
    double wrapped = fmod(trunc(number), 4294967296.0);
    if (wrapped < 0) wrapped += 4294967296.0;
    return (i32)(u32)wrapped;
}

// The value of another enum member, if it's known already (a member that comes later, or whose enum is being
// resolved further up the stack, isn't)
static EnumValue member_value(Checker* me, Symbol const* decl) {
    if (!decl || !(decl->flags & SYMBOL_ENUM_MEMBER)) return enum_value_unknown;
    Ty const* type = ty_declared_type_of(me, decl);
    if (type->kind != TY_LITERAL || !type->literal.enum_decl) return enum_value_unknown;
    if (type->flags & TYF_STRING_LITERAL) {
        return (EnumValue){ .known = true, .is_string = true, .string = cstr_from_sv(atom_sv(type->literal.string)) };
    }
    if (type->flags & TYF_NUMBER_LITERAL) return enum_value_number(type->literal.value.number);
    return enum_value_unknown;
}

static EnumValue evaluate_binary(Checker* me, BinaryOpExpr const* op, u32 depth) {
    EnumValue left = evaluate(me, &op->left, depth);
    EnumValue right = evaluate(me, &op->right, depth);
    EnumValue ret = enum_value_unknown;
    if (left.known && right.known && !left.is_string && !right.is_string) {
        double a = left.number, b = right.number;
        switch (op->op) {
            case BINARY_OP_OR: ret = enum_value_number(to_int32(a) | to_int32(b)); break;
            case BINARY_OP_AND: ret = enum_value_number(to_int32(a) & to_int32(b)); break;
            case BINARY_OP_XOR: ret = enum_value_number(to_int32(a) ^ to_int32(b)); break;
            case BINARY_OP_LESS_LESS: ret = enum_value_number((i32)((u32)to_int32(a) << ((u32)to_int32(b) & 31))); break;
            case BINARY_OP_GREATER_GREATER: ret = enum_value_number(to_int32(a) >> ((u32)to_int32(b) & 31)); break;
            case BINARY_OP_GREATER_GREATER_GREATER:
                ret = enum_value_number((u32)to_int32(a) >> ((u32)to_int32(b) & 31));
                break;
            case BINARY_OP_STAR: ret = enum_value_number(a * b); break;
            case BINARY_OP_SLASH: ret = enum_value_number(a / b); break;
            case BINARY_OP_PLUS: ret = enum_value_number(a + b); break;
            case BINARY_OP_MINUS: ret = enum_value_number(a - b); break;
            case BINARY_OP_MOD: ret = enum_value_number(fmod(a, b)); break;
            case BINARY_OP_STAR_STAR: ret = enum_value_number(pow(a, b)); break;
            default: break;
        }
    } else if (left.known && right.known && op->op == BINARY_OP_PLUS) {
        cstr joined = cstr_null;
        for (u32 i = 0; i < 2; i++) {
            EnumValue const* side = i ? &right : &left;
            if (side->is_string) {
                cstr_append_sv(&joined, cstr_sv(&side->string));
            } else {
                Value number = { .tag = VALUE_NUMBER, .number = side->number };
                cstr_append_value_js(&joined, &number);
            }
        }
        ret = (EnumValue){ .known = true, .is_string = true, .string = joined };
    }
    cstr_drop(&left.string);
    cstr_drop(&right.string);
    return ret;
}

// A `const` variable without a type annotation whose initializer evaluates, declared before `use` in its file
static EnumValue const_variable_value(Checker* me, Symbol const* symbol, Span use, u32 depth) {
    if (!symbol || !symbol->is_const || symbol->decl_count != 1 || symbol->decls[0].kind != SYMBOL_DECL_VARIABLE) {
        return enum_value_unknown;
    }
    Binding const* binding = symbol->decls[0].node;
    if (binding->parent || binding->has_explicit_type || !binding->source || binding->source->tag != LAZY_TYPE_INITIALIZED_BINDING) {
        return enum_value_unknown;
    }
    if (symbol->name_span.source == use.source && span_start_ptr(symbol->name_span) >= span_start_ptr(use)) {
        return enum_value_unknown;
    }
    InitializedBinding const* initialized = c_container_of(binding->source, InitializedBinding, lazy_type);
    return evaluate(me, &initialized->value, depth);
}

// tsc's evaluator (enum initializers, template literal expressions)
static EnumValue evaluate(Checker* me, Expr const* expr, u32 depth) {
    if (depth > 64) return enum_value_unknown;
    depth++;
    switch (expr->tag) {
        case EXPR_LITERAL:
            if (expr->literal.tag == VALUE_NUMBER) return enum_value_number(expr->literal.number);
            if (expr->literal.tag == VALUE_STRING) {
                return (EnumValue){ .known = true, .is_string = true, .string = cstr_clone(expr->literal.string) };
            }
            return enum_value_unknown;

        case EXPR_UNARY_OP: {
            EnumValue operand = evaluate(me, &expr->unary_op->expr, depth);
            if (!operand.known || operand.is_string) {
                cstr_drop(&operand.string);
                return enum_value_unknown;
            }
            switch (expr->unary_op->op) {
                case UNARY_OP_PLUS: return operand;
                case UNARY_OP_MINUS: return enum_value_number(-operand.number);
                case UNARY_OP_TILDE: return enum_value_number(~to_int32(operand.number));
                default: return enum_value_unknown;
            }
        }

        case EXPR_BINARY_OP:
            return evaluate_binary(me, expr->binary_op, depth);

        case EXPR_IDENTIFIER:
            if (!expr->identifier.target || !(expr->identifier.target->flags & SYMBOL_ENUM_MEMBER)) {
                if (csview_equals(expr->identifier.name.sv, "NaN")) return enum_value_number(NAN);
                if (csview_equals(expr->identifier.name.sv, "Infinity")) return enum_value_number(INFINITY);
                return const_variable_value(me, expr->identifier.target, expr->identifier.name, depth);
            }
            return member_value(me, expr->identifier.target);

        case EXPR_TEMPLATE: {
            TemplateExpr const* template = expr->template_;
            if (template->tag || !vec_cstr_size(&template->texts)) return enum_value_unknown;
            cstr joined = cstr_clone(*vec_cstr_at(&template->texts, 0));
            for_each (i, vec_expr, &template->substitutions) {
                EnumValue part = evaluate(me, i, depth);
                if (!part.known) {
                    cstr_drop(&joined);
                    return enum_value_unknown;
                }
                if (part.is_string) {
                    cstr_append_sv(&joined, cstr_sv(&part.string));
                } else {
                    Value number = { .tag = VALUE_NUMBER, .number = part.number };
                    cstr_append_value_js(&joined, &number);
                }
                cstr_drop(&part.string);
                cstr_append_sv(&joined, cstr_sv(vec_cstr_at(&template->texts, i_index + 1)));
            }
            return (EnumValue){ .known = true, .is_string = true, .string = joined };
        }

        case EXPR_PROPERTY: {
            // `E.A` and `E["A"]`
            PropertyExpr const* property = expr->property;
            if (property->expr.tag != EXPR_IDENTIFIER || !property->expr.identifier.target) return enum_value_unknown;
            Symbol const* enum_decl = property->expr.identifier.target;
            if (!(enum_decl->flags & SYMBOL_ENUM)) return enum_value_unknown;
            Span name;
            if (property->form == PROPERTY_EXPR_DOT && property->prop.tag == EXPR_IDENTIFIER) {
                name = property->prop.identifier.name;
            } else if (property->prop.tag == EXPR_LITERAL && property->prop.literal.tag == VALUE_STRING) {
                name = property->prop.inner_span;
                if (name.sv.size < 2) return enum_value_unknown;
                name = span_make(name.source, span_start_ptr(name) + 1, span_end_ptr(name) - 1);
            } else {
                return enum_value_unknown;
            }
            return member_value(me, symbol_get_export(enum_decl, name.sv));
        }

        default:
            return enum_value_unknown;
    }
}

// The value of a template literal expression whose parts are all constants (tsc's evaluate()), false if it has none
bool ty_evaluate_string(Checker* me, Expr const* expr, cstr* out) {
    EnumValue value = evaluate(me, expr, 0);
    if (!value.known || !value.is_string) {
        cstr_drop(&value.string);
        return false;
    }
    *out = value.string;
    return true;
}

Ty const* ty_resolve_enum(Checker* me, Symbol const* decl) {
    Ty const** members = nullptr;
    u32 count = 0, cap = 0;
    for (u32 decl_index = 0; decl_index < decl->decl_count; decl_index++) {
        if (decl->decls[decl_index].kind != SYMBOL_DECL_ENUM) continue;
        EnumDecl const* enum_decl = decl->decls[decl_index].node;
        EnumValue previous = enum_value_number(-1);
        for_each (i, vec_enum_case, &enum_decl->cases) {
            EnumValue value;
            if (i->value.tag == EXPR_IMPLICIT_ANY) {
                // TODO: members of an ambient non-const enum without initializer are computed
                bool can_increment = previous.known && !previous.is_string;
                value = can_increment ? enum_value_number(previous.number + 1) : enum_value_unknown;
            } else {
                value = evaluate(me, &i->value, 0);
            }
            Symbol const* member = i->symbol;
            Ty const* type;
            if (!member) {
                type = ty_intrinsic(me, TY_ERROR);
            } else if (value.known) {
                Value literal = { .tag = value.is_string ? VALUE_STRING : VALUE_NUMBER };
                if (value.is_string) literal.string = value.string; else literal.number = value.number;
                type = ty_enum_literal(me, &literal, decl, member);
            } else {
                type = ty_enum_computed_member(me, decl, member);
            }
            // Members of the same name (TS2300) are symbols of their own. A member's type is fresh, the enum's are regular
            if (member) ty_set_declared_type(me, member, ty_fresh_literal(me, type));
            if (count == cap) {
                cap = cap ? cap * 2 : 16;
                members = realloc(members, sizeof(Ty const*) * cap);
                panic_if(!members, "out of memory");
            }
            members[count++] = type;
            cstr_drop(&previous.string);
            previous = value;
        }
        cstr_drop(&previous.string);
    }
    // An enum without members is a computed enum type of its own
    Ty const* ret = count ?
        ty_union(me, members, count, decl, (TyList){0}) :
        ty_enum_computed_member(me, decl, decl);
    free(members);
    return ret;
}

Ty const* ty_enum_of_member(Checker* me, Ty const* member) {
    check(member->kind == TY_LITERAL && member->literal.enum_decl);
    return ty_declared_type_of(me, member->literal.enum_decl);
}

bool ty_is_enum_union(Ty const* ty) {
    return ty->kind == TY_UNION && ty->alias && ty->alias->flags & SYMBOL_ENUM && !ty->alias_args.len;
}
