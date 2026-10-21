#include "ty/ty_internal.h"
#include "program.h"
#include "enum_decl.h"
#include "scope.h"
#include "binding.h"
#include "expr.h"
#include "error.h"
#include <math.h>

// Enums. The type of an enum is the union of the types of its members (with the enum as the union's alias, which is
// how it prints as `E`); a member's type is a literal type that remembers the enum it belongs to, or, when its value
// isn't a constant, an opaque "computed" member type. An enum with a single member *is* that member's type, as in tsc.

typedef struct EnumValue {
    bool known;
    bool is_string;
    // Written as a string (a string literal, a template, `+` with one): what isolatedModules asks of a string member
    // (tsc's isSyntacticallyString)
    bool syntactic_string;
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
static EnumValue evaluate(Checker* me, Expr const* expr, u32 depth);
static EnumValue member_value(Checker* me, Symbol const* decl, u32 depth) {
    if (!decl || !(decl->flags & SYMBOL_ENUM_MEMBER)) return enum_value_unknown;
    // A member declared later in the file (TS2651) evaluates to 0 (tsc's evaluateEnumMember)
    EnumCase const* at = my enum_evaluating;
    if (at && decl != at->symbol && span_source(decl->name_span) == span_source(at->name) &&
        span_start_ptr(decl->name_span) > span_start_ptr(at->name)
    ) {
        return enum_value_number(0);
    }
    Ty const* type = ty_declared_type_of(me, decl);
    if (type->kind != TY_LITERAL || !type->literal.enum_decl) return enum_value_unknown;
    if (type->flags & TYF_STRING_LITERAL) {
        // (Syntactically a string if its own initializer is: tsc's getEnumMemberValue keeps isSyntacticallyString)
        SymbolDecl const* member = symbol_find_decl(decl, SYMBOL_DECL_ENUM_MEMBER);
        bool syntactic = true;
        if (member) {
            EnumCase const* outer = my enum_evaluating;
            my enum_evaluating = member->node;
            EnumValue own = evaluate(me, &((EnumCase const*)member->node)->value, depth);
            my enum_evaluating = outer;
            syntactic = !own.known || own.syntactic_string;
            cstr_drop(&own.string);
        }
        return (EnumValue){ .known = true, .is_string = true, .string = cstr_from_sv(atom_sv(type->literal.string)),
            .syntactic_string = syntactic };
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
        bool syntactic = left.syntactic_string || right.syntactic_string;
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
        ret = (EnumValue){ .known = true, .is_string = true, .string = joined, .syntactic_string = syntactic };
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
    if (span_source(symbol->name_span) == span_source(use) && span_start_ptr(symbol->name_span) >= span_start_ptr(use)) {
        return enum_value_unknown;
    }
    InitializedBinding const* initialized = c_container_of(binding->source, InitializedBinding, lazy_type);
    EnumValue ret = evaluate(me, initialized_binding_value(initialized), depth);
    // (From another file it isn't written here: tsc's evaluateEntityNameExpression)
    if (span_source(symbol->name_span) != span_source(use)) ret.syntactic_string = false;
    return ret;
}

// The symbol a name or a dotted name of namespaces refers to, null if it isn't one
static Symbol const* entity_symbol(Expr const* expr) {
    if (expr->tag == EXPR_IDENTIFIER) return expr->identifier.target;
    if (expr->tag != EXPR_PROPERTY || expr->property->form != PROPERTY_EXPR_DOT) return nullptr;
    Symbol const* object = entity_symbol(&expr->property->expr);
    if (!object || !(object->flags & (SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE))) return nullptr;
    return symbol_get_export(object, name_cooked(span_sv(property_name(expr->property))));
}

// tsc's evaluator (enum initializers, template literal expressions)
static EnumValue evaluate(Checker* me, Expr const* expr, u32 depth) {
    if (depth > 64) return enum_value_unknown;
    depth++;
    switch (expr->tag) {
        case EXPR_LITERAL:
            if (expr->literal.tag == VALUE_NUMBER) return enum_value_number(expr->literal.number);
            if (expr->literal.tag == VALUE_STRING) {
                return (EnumValue){ .known = true, .is_string = true, .string = cstr_clone(expr->literal.string),
                    .syntactic_string = true };
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
                // (The global ones, not a local of that name)
                Symbol const* target = expr->identifier.target;
                csview name = span_sv(expr->inner_span);
                bool global = !target || target == program_get_global(my program, name, SYMBOL_VALUE);
                if (global && csview_equals(name, "NaN")) return enum_value_number(NAN);
                if (global && csview_equals(name, "Infinity")) return enum_value_number(INFINITY);
                return const_variable_value(me, expr->identifier.target, expr->inner_span, depth);
            }
            return member_value(me, expr->identifier.target, depth);

        case EXPR_TEMPLATE: {
            TemplateExpr const* template = expr->template_;
            if (template->tag || !ast_cstrs_size(&template->texts)) return enum_value_unknown;
            cstr joined = cstr_clone(*ast_cstrs_at(&template->texts, 0));
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
                cstr_append_sv(&joined, cstr_sv(ast_cstrs_at(&template->texts, i_index + 1)));
            }
            return (EnumValue){ .known = true, .is_string = true, .string = joined, .syntactic_string = true };
        }

        case EXPR_PROPERTY: {
            // `E.A`, `E["A"]` and `N.E.A`, or a namespace's const variable `N.C`
            PropertyExpr const* property = expr->property;
            Symbol const* enum_decl = entity_symbol(&property->expr);
            if (enum_decl && !(enum_decl->flags & SYMBOL_ENUM)) {
                return const_variable_value(me, entity_symbol(expr), expr_outer_span(expr), depth);
            }
            if (!enum_decl) return enum_value_unknown;
            csview name;
            if (!property_string_key(property, &name)) return enum_value_unknown;
            return member_value(me, symbol_get_export(enum_decl, name), depth);
        }

        default:
            return enum_value_unknown;
    }
}

// The reference checks of tsc's evaluateEnumMember, for the members an initializer of `member` names directly (the
// evaluation doesn't go into other members' initializers): itself (TS2565) or one declared after it in the file (TS2651)
static void check_enum_reference(EnumCase const* member, Symbol const* referenced, Span at, ErrorCtx* errs) {
    if (!referenced || !(referenced->flags & SYMBOL_ENUM_MEMBER)) return;
    if (referenced == member->symbol) {
        error_ctx_error_d(errs, at, 2565, "Property '", referenced->name_span, "' is used before being assigned.");
    } else if (span_source(referenced->name_span) == span_source(member->name) &&
        span_start_ptr(referenced->name_span) > span_start_ptr(member->name)
    ) {
        error_ctx_error_d(errs, at, 2651, "A member initializer in a enum declaration cannot reference members declared "
            "after it, including members defined in other enums.");
    }
}

void ty_check_enum_initializer_references(Checker* me, EnumCase const* member, Expr const* expr, ErrorCtx* errs) {
    switch (expr->tag) {
        case EXPR_UNARY_OP:
            ty_check_enum_initializer_references(me, member, &expr->unary_op->expr, errs);
            break;
        case EXPR_BINARY_OP:
            ty_check_enum_initializer_references(me, member, &expr->binary_op->left, errs);
            ty_check_enum_initializer_references(me, member, &expr->binary_op->right, errs);
            break;
        case EXPR_TEMPLATE:
            if (expr->template_->tag) break;
            for_each (i, vec_expr, &expr->template_->substitutions) ty_check_enum_initializer_references(me, member, i, errs);
            break;
        case EXPR_IDENTIFIER:
            check_enum_reference(member, expr->identifier.target, expr_outer_span(expr), errs);
            break;
        case EXPR_PROPERTY: {
            PropertyExpr const* property = expr->property;
            Symbol const* enum_decl = entity_symbol(&property->expr);
            if (!enum_decl || !(enum_decl->flags & SYMBOL_ENUM)) break;
            csview name;
            if (!property_string_key(property, &name)) break;
            check_enum_reference(member, symbol_get_export(enum_decl, name),
                expr_outer_span(expr), errs);
            break;
        }
        default:
            break;
    }
}

// The value of a template literal expression whose parts are all constants (tsc's evaluate()), false if it has none
bool ty_enum_initializer_is_syntactic_string(Checker* me, Expr const* expr) {
    EnumValue value = evaluate(me, expr, 0);
    cstr_drop(&value.string);
    return !value.known || !value.is_string || value.syntactic_string;
}

bool ty_evaluate_number(Checker* me, Expr const* expr, double* out) {
    EnumValue value = evaluate(me, expr, 0);
    bool ret = value.known && !value.is_string;
    if (ret) *out = value.number;
    cstr_drop(&value.string);
    return ret;
}

bool ty_evaluate_string(Checker* me, Expr const* expr, cstr* out) {
    EnumValue value = evaluate(me, expr, 0);
    if (!value.known || !value.is_string) {
        cstr_drop(&value.string);
        return false;
    }
    *out = value.string;
    return true;
}

int ty_enum_member_constant(Checker* me, EnumCase const* member, double* number) {
    EnumCase const* outer = my enum_evaluating;
    my enum_evaluating = member;
    EnumValue value = evaluate(me, &member->value, 0);
    my enum_evaluating = outer;
    int ret = !value.known ? 0 : value.is_string ? 2 : 1;
    if (ret == 1) *number = value.number;
    cstr_drop(&value.string);
    return ret;
}

Ty const* ty_resolve_enum(Checker* me, Symbol const* decl, u64 taint) {
    Ty const** members = nullptr;
    u32 count = 0, cap = 0;
    for (u32 decl_index = 0; decl_index < decl->decl_count; decl_index++) {
        if (decl->decls[decl_index].kind != SYMBOL_DECL_ENUM) continue;
        EnumDecl const* enum_decl = decl->decls[decl_index].node;
        EnumValue previous = enum_value_number(-1);
        // (In an ambient non-const enum, a member without initializer is computed, not the next number: tsc's
        // computeEnumMemberValue)
        bool ambient = enum_decl->ambient && !enum_decl->is_const;
        for_each (i, vec_enum_case, &enum_decl->cases) {
            EnumValue value;
            if (i->value.tag == EXPR_IMPLICIT_ANY) {
                bool can_increment = !ambient && previous.known && !previous.is_string;
                value = can_increment ? enum_value_number(previous.number + 1) : enum_value_unknown;
            } else {
                EnumCase const* outer = my enum_evaluating;
                my enum_evaluating = i;
                value = evaluate(me, &i->value, 0);
                my enum_evaluating = outer;
            }
            Symbol const* member = i->symbol;
            // (A member without a bindable name is none of the enum's literal types: its type is the enum's, below.
            // tsc's getDeclaredTypeOfEnum and getDeclaredTypeOfEnumMember)
            if (i->computed_name) {
                cstr_drop(&previous.string);
                previous = value;
                continue;
            }
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
            if (member) ty_set_declared_type(me, member, ty_fresh_literal(me, type), taint);
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
    for (u32 decl_index = 0; decl_index < decl->decl_count; decl_index++) {
        if (decl->decls[decl_index].kind != SYMBOL_DECL_ENUM) continue;
        EnumDecl const* enum_decl = decl->decls[decl_index].node;
        for_each (i, vec_enum_case, &enum_decl->cases) {
            if (i->computed_name && i->symbol) ty_set_declared_type(me, i->symbol, ret, taint);
        }
    }
    return ret;
}

Ty const* ty_enum_of_member(Checker* me, Ty const* member) {
    check(member->kind == TY_LITERAL && member->literal.enum_decl);
    return ty_declared_type_of(me, member->literal.enum_decl);
}

bool ty_is_enum_union(Ty const* ty) {
    return ty->kind == TY_UNION && ty->alias && ty->alias->flags & SYMBOL_ENUM && !ty->alias_args.len;
}
