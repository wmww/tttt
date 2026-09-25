#include "class_decl.h"
#include "scope.h"
#include "binding.h"
#include "flow.h"
#include "ty/ty.h"
#include "statement.h"
#include "interface_member.h"
#include "callable_type.h"
#include "several_types.h"
#include "error.h"
#include "unused.h"
#include "source_file.h"

void class_item_drop(ClassItem* me) {
    vec_expr_drop(&my decorators);
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_drop(&my constructor.func);
            break;

        case CLASS_ITEM_METHOD:
            cstr_drop(&my method.name_string);
            if (my method.computed_key) {
                expr_drop(my method.computed_key);
                free(my method.computed_key);
            }
            function_impl_drop(&my method.func);
            lazy_type_drop(&my method.accessor_type);
            break;

        case CLASS_ITEM_FIELD:
            cstr_drop(&my field.name_string);
            if (my field.computed_key) {
                expr_drop(my field.computed_key);
                free(my field.computed_key);
            }
            type_drop(&my field.explicit_type);
            expr_drop(&my field.value);
            lazy_type_drop(&my field.lazy_type);
            break;

        case CLASS_ITEM_INTERFACE_MEMBER:
            interface_member_drop(&my interface_memeber);
            break;

        case CLASS_ITEM_STATIC_BLOCK:
            block_drop(&my static_block);
            break;
    }
}

Span class_item_private_name(ClassItem const* me) {
    Span name = my tag == CLASS_ITEM_METHOD ? my method.name_span : my tag == CLASS_ITEM_FIELD ? my field.name_span : span_null;
    return span_valid(name) && name.sv.size > 1 && name.sv.buf[0] == '#' ? name : span_null;
}

ClassItem const* class_decl_find_private_name(ClassDecl const* me, csview name) {
    for_each (i, vec_class_item, &my items) {
        Span item_name = class_item_private_name(i);
        if (span_valid(item_name) && csview_equals_sv(item_name.sv, name)) return i;
    }
    return nullptr;
}

bool class_item_is_static(ClassItem* me) {
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR: return false;
        case CLASS_ITEM_METHOD: return my method.is_static;
        case CLASS_ITEM_FIELD: return my field.is_static;
        case CLASS_ITEM_INTERFACE_MEMBER: return my interface_member_static;
        case CLASS_ITEM_STATIC_BLOCK: return true;
    }
    impossible;
}

Ty const* class_item_field_evaluate_type(ClassItem* me) {
    check(my tag == CLASS_ITEM_FIELD);
    Checker* checker = current_checker;
    if (my field.has_explicit_type) return ty_of_node(&my field.explicit_type);
    Ty const* ret = ty_widened_for_declaration(checker, ty_expr_type(checker, &my field.value), !my field.is_readonly);
    // A unique symbol made for another declaration is only a symbol here (tsc's widenTypeForVariableLikeDeclaration)
    if ((ret->flags & TYF_UNIQUE_ES_SYMBOL) && ret->literal.unique->key != me) ret = ty_intrinsic(checker, TY_ES_SYMBOL);
    return ret;
}

Ty const* class_item_accessor_evaluate_type(ClassItem* me) {
    check(my tag == CLASS_ITEM_METHOD && my method.accessor);
    if (my method.accessor == CLASS_ACCESSOR_SET && my method.accessor_getter) {
        return lazy_type_get(&my method.accessor_getter->method.accessor_type);
    }
    Checker* checker = current_checker;
    Members const* members = ty_members(checker, ty_function_type(checker, &my method.func));
    if (members->call_count != 1) return ty_intrinsic(checker, TY_ANY);
    Signature const* sig = members->call[0];
    signature_resolve(checker, sig);
    if (my method.accessor == CLASS_ACCESSOR_GET) {
        return ty_widened_for_declaration(checker, signature_return_type(checker, sig), true);
    }
    return sig->param_count ? sig->params[0].type : ty_intrinsic(checker, TY_ANY);
}

Ty const* class_item_get_type(ClassItem* me) {
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR: return lazy_type_get(&my constructor.func.lazy_type);
        case CLASS_ITEM_METHOD:
            return lazy_type_get(my method.accessor ? &my method.accessor_type : &my method.func.lazy_type);
        case CLASS_ITEM_FIELD: return lazy_type_get(&my field.lazy_type);
        case CLASS_ITEM_INTERFACE_MEMBER: return ty_of_node(&my interface_memeber.type);
        case CLASS_ITEM_STATIC_BLOCK: return ty_intrinsic(current_checker, TY_NEVER);
    }
    impossible;
}

void class_decl_drop(ClassDecl* me) {
    vec_expr_drop(&my decorators);
    generic_drop(&my generic);
    expr_drop(&my extends);
    vec_type_drop(&my implements);
    vec_class_item_drop(&my items);
}

// A computed member name or a decorator
static void lookup_names_without_super(Expr* expr, Scope* names, NoSuper why, ErrorCtx* errs) {
    Scope* scope = scope_new(names, SCOPE_BLOCK);
    scope->no_super = why;
    uint8_t initializer_context = names->flow->initializer_context;
    names->flow->initializer_context = INITIALIZER_NONE;
    expr_lookup_names(expr, scope, errs);
    names->flow->initializer_context = initializer_context;
}

void class_item_lookup_names(ClassItem* me, Scope* names, ErrorCtx* errs) {
    for_each (i, vec_expr, &my decorators) {
        lookup_names_without_super(i, names, NO_SUPER_DECORATOR, errs);
    }
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_lookup_names(&my constructor.func, false, names, errs);
            break;

        case CLASS_ITEM_METHOD:
            if (my method.computed_key) lookup_names_without_super(my method.computed_key, names, NO_SUPER_COMPUTED_NAME, errs);
            lazy_type_bind(&my method.accessor_type, LAZY_TYPE_CLASS_ITEM_ACCESSOR);
            my method.func.is_method = !my method.accessor;
            my method.func.accessor = (uint8_t)my method.accessor;
            my method.func.member_name = my method.name_span;
            function_impl_lookup_names(&my method.func, false, names, errs);
            break;

        case CLASS_ITEM_FIELD:
            if (my field.computed_key) lookup_names_without_super(my field.computed_key, names, NO_SUPER_COMPUTED_NAME, errs);
            lazy_type_bind(&my field.lazy_type, LAZY_TYPE_CLASS_ITEM_FIELD);
            type_lookup_names(&my field.explicit_type, names, errs);
            if (my field.value.tag == EXPR_IMPLICIT_ANY) {
                expr_lookup_names(&my field.value, names, errs);
            } else {
                // An initializer is a control flow container of its own
                FlowBuilder* outer = names->flow;
                FlowBuilder flow;
                flow_builder_init(&flow, outer, names, nullptr, errs);
                flow.current = flow_new_start(&flow, nullptr, false);
                flow.initializer_context = my field.is_static ? INITIALIZER_STATIC_FIELD : INITIALIZER_INSTANCE_FIELD;
                flow.initializer_field = me;
                names->flow = &flow;
                expr_lookup_names(&my field.value, names, errs);
                flow_builder_finish(&flow, c_sv("field"), my field.value.outer_span);
                names->flow = outer;
            }
            break;

        case CLASS_ITEM_INTERFACE_MEMBER:
            interface_member_lookup_names(&my interface_memeber, names, errs);
            break;

        case CLASS_ITEM_STATIC_BLOCK:
            Scope* block_scope = scope_new(names, SCOPE_STATIC_BLOCK);
            block_add_names(&my static_block, block_scope);
            block_lookup_names_inline_container(&my static_block, block_scope, errs);
            break;
    }
}

void class_decl_add_names(ClassDecl* me, Scope* scope, bool exported, bool declared) {
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported,
        .is_ambient = declared,
        .kind = SYMBOL_DECL_CLASS,
        .node = me,
    });
}

static void class_members_lookup_names(ClassDecl* me, Scope* names, ErrorCtx* errs, bool static_members) {
    Scope* this_scope = scope_new(names, SCOPE_BLOCK);
    this_scope->this_ref = (ThisRef){
        .kind = static_members ? THIS_CLASS_STATIC : THIS_CLASS_INSTANCE, .symbol = my symbol, .class_decl = me
    };

    for_each (i, vec_class_item, &my items) {
        if (class_item_is_static(i) == static_members) {
            class_item_lookup_names(i, this_scope, errs);
        }
    }
}

void class_decl_lookup_names(ClassDecl* me, Scope* names, ErrorCtx* errs) {
    for_each (i, vec_expr, &my decorators) {
        expr_lookup_names(i, names, errs);
    }

    my outer_class = scope_enclosing_class(names);
    unused_register(names, UNUSED_CLASS, me, span_null);
    names = scope_new_generic_merged(names, &my generic, my symbol, me, errs);

    for_each (i, vec_class_item, &my items) {
        if (i->tag == CLASS_ITEM_CONSTRUCTOR) {
            if (i->constructor.func.ret_explicit) {
                error_ctx_error_d(
                    errs,
                    i->constructor.func.ret_span,
                    1093,
                    "Type annotation cannot appear on a constructor declaration."
                );
                type_drop(&i->constructor.func.ret);
            }
            check(i->constructor.func.ret.tag == TYPE_ANY);
            i->constructor.func.ret_explicit = false;
            i->constructor.func.constructed_class = my symbol;
            i->constructor.func.constructor_access = (uint8_t)i->access;
        }
    }

    for_each (i, vec_class_item, &my items) {
        if (i->tag == CLASS_ITEM_FIELD && i->field.is_static && i->field.is_readonly && !i->field.computed_key) {
            unique_symbol_decl_mark(
                (UniqueSymbolDecl){ .key = i, .owner = my symbol, .member = i->field.name_span },
                &i->field.explicit_type, &i->field.value
            );
        }
        if (i->tag == CLASS_ITEM_METHOD && i->method.accessor) {
            for_each (other, vec_class_item, &my items) {
                if (other->tag == CLASS_ITEM_METHOD &&
                    other->method.accessor &&
                    other->method.is_static == i->method.is_static &&
                    cstr_eq(&other->method.name_string, &i->method.name_string)
                ) {
                    if (!i->method.accessor_first) {
                        i->method.accessor_first = other;
                    }
                    if (other->method.accessor == CLASS_ACCESSOR_GET && !i->method.accessor_getter) {
                        i->method.accessor_getter = other;
                        if (i->method.accessor == CLASS_ACCESSOR_SET) function_impl_pair_setter(&i->method.func, &other->method.func);
                    }
                }
            }
        }
    }

    expr_lookup_names(&my extends, names, errs);
    for_each (i, vec_type, &my implements) {
        type_lookup_names(i, names, errs);
    }

    class_members_lookup_names(me, names, errs, true);
    class_members_lookup_names(me, names, errs, false);
}

// TS1166/TS1168: the key of a property or of a method overload has to be something the class can be typed by
static void class_computed_key_typecheck(ClassItem* item, ErrorCtx* errs) {
    bool field = item->tag == CLASS_ITEM_FIELD;
    Expr* key = field ? item->field.computed_key : item->method.computed_key;
    expr_typecheck(key, errs);
    bool accessor = !field && item->method.accessor;
    ty_check_computed_name(current_checker, key, field ? item->field.name_span : item->method.name_span, !accessor, errs);
    if (!field && item->method.func.has_body) return; // Only overload signatures are restricted
    Ty const* key_type = expr_get_type(key);
    // tsc's checkGrammarForInvalidDynamicName: an entity name that isn't a literal is a late bound index signature
    if (key_type->flags & (TYF_LITERAL | TYF_ENUM | TYF_UNIQUE_ES_SYMBOL | TYF_ANY) || expr_is_entity_name(key)) return;
    Span span = field ? item->field.name_span : item->method.name_span;
    if (field) {
        error_ctx_error_d(errs, span, 1166,
            "A computed property name in a class property declaration must have a simple literal type or a 'unique symbol' type.");
    } else {
        error_ctx_error_d(errs, span, 1168,
            "A computed property name in a method overload must refer to an expression whose type is a literal type or a 'unique symbol' type.");
    }
}

static Atom const* item_member_name(Checker* checker, ClassDecl const* me, ClassItem const* item) {
    if (span_valid(class_item_private_name(item)) && my symbol) {
        return ty_private_name(checker, my symbol, class_item_private_name(item).sv);
    }
    bool is_field = item->tag == CLASS_ITEM_FIELD;
    return ty_member_name(checker, is_field ? &item->field.name_string : &item->method.name_string,
        is_field ? item->field.computed_key : item->method.computed_key);
}

// tsc's checkClassLikeDeclaration: the class (with its own `this`) has to fit what it extends and implements, with
// that `this`
static void class_heritage_typecheck(ClassDecl* me, ErrorCtx* errs) {
    Checker* checker = current_checker;
    if (!my symbol) return;
    Ty const* declared = ty_declared_type_of(checker, my symbol);
    if (declared->kind != TY_DECLARED || !declared->declared.this_param) return;
    Ty const* this_param = declared->declared.this_param;
    Ty const* type_with_this = ty_with_this_arg(checker, declared, this_param);
    HeritageMember* members = malloc(sizeof(HeritageMember) * (vec_class_item_size(&my items) + 1));
    u32 count = 0;
    for_each (i, vec_class_item, &my items) {
        if (i->tag == CLASS_ITEM_FIELD && !i->field.is_static) {
            members[count++] = (HeritageMember){
                item_member_name(checker, me, i), i->field.name_span,
            };
        } else if (i->tag == CLASS_ITEM_METHOD && !i->method.is_static) {
            members[count++] = (HeritageMember){
                item_member_name(checker, me, i), i->method.name_span,
            };
        }
    }
    Span span = span_valid(my name) ? my name : my full_span;
    Ty const* base = my extends.tag != EXPR_IMPLICIT_ANY ? ty_class_base_instance(checker, me) : nullptr;
    // A class that extends itself through its bases is TS2506 in tsc, and nothing is compared
    if (base && ty_has_base_type(checker, base, declared)) base = nullptr;
    if (base && (base->flags & TYF_OBJECT)) {
        ty_check_heritage(checker, type_with_this, ty_with_this_arg(checker, base, this_param), members, count, 2415,
            "Class", "incorrectly extends base class", span, errs);
    }
    for_each (i, vec_type, &my implements) {
        Ty const* implemented = ty_of_node(i);
        if (!(implemented->flags & (TYF_OBJECT | TYF_INTERSECTION))) continue;
        Ty const* target = implemented->kind == TY_REFERENCE ? implemented->reference.target : implemented;
        bool is_class = target->kind == TY_DECLARED && target->declared.symbol && (target->declared.symbol->flags & SYMBOL_CLASS);
        ty_check_heritage(checker, type_with_this, ty_with_this_arg(checker, implemented, this_param), members, count,
            is_class ? 2720 : 2420, "Class",
            is_class ? "incorrectly implements class" : "incorrectly implements interface", span, errs);
    }
    free(members);
}

// A method (not an accessor) or constructor: what tsc's checkFunctionOrConstructorSymbol compares
static FunctionImpl const* overloadable(ClassItem const* item) {
    if (item->tag == CLASS_ITEM_CONSTRUCTOR) return &item->constructor.func;
    if (item->tag == CLASS_ITEM_METHOD && !item->method.accessor) return &item->method.func;
    return nullptr;
}

static bool same_member(ClassItem const* a, ClassItem const* b) {
    if (a->tag != b->tag) return false;
    if (a->tag == CLASS_ITEM_CONSTRUCTOR) return true;
    return !a->method.computed_key && !b->method.computed_key && cstr_eq(&a->method.name_string, &b->method.name_string);
}

static Span overload_name(ClassItem const* item) {
    return item->tag == CLASS_ITEM_CONSTRUCTOR ? item->constructor.name_span : item->method.name_span;
}

// tsc's checkFunctionOrConstructorSymbol for the methods and constructors of a class: overloads are consecutive and
// followed by the implementation (TS2391, TS2389, TS2390, TS2516, TS2387/TS2388), one implementation (TS2393)
static void class_check_overloads(ClassDecl* me, ErrorCtx* errs) {
    SourceFile const* file = my name.source ? my name.source : my full_span.source;
    if ((my symbol && my symbol->is_ambient) || !file || file->is_declaration || file->has_parse_errors) return;
    isize count = vec_class_item_size(&my items);
    for (isize i = 0; i < count; i++) {
        ClassItem const* item = vec_class_item_at(&my items, i);
        FunctionImpl const* func = overloadable(item);
        if (!func || func->has_body) continue;
        bool is_constructor = item->tag == CLASS_ITEM_CONSTRUCTOR;
        if (!is_constructor && item->method.computed_key) continue;
        ClassItem const* next = i + 1 < count ? vec_class_item_at(&my items, i + 1) : nullptr;
        if (next && !overloadable(next)) next = nullptr;
        if (next && same_member(item, next)) {
            bool is_static = !is_constructor && item->method.is_static;
            if (!is_constructor && is_static != next->method.is_static) {
                error_ctx_error_d(errs, next->method.name_span, is_static ? 2387 : 2388,
                    is_static ? "Function overload must be static." : "Function overload must not be static.");
            }
            continue;
        }
        bool is_abstract = !is_constructor && item->method.is_abstract;
        bool later = false;
        for (isize j = i + 1; j < count && !later; j++) later = same_member(item, vec_class_item_at(&my items, j));
        // The last one without a body is only an error for what needs an implementation
        if (!later && (is_abstract || (!is_constructor && item->method.is_optional))) continue;
        if (next && overloadable(next)->has_body && next->tag == item->tag) {
            cstr name = cstr_from_sv(overload_name(item).sv);
            error_ctx_error_d(errs, overload_name(next), 2389, "Function implementation name must be '", name, "'.");
            cstr_drop(&name);
        } else if (is_constructor) {
            error_ctx_error_d(errs, overload_name(item), 2390, "Constructor implementation is missing.");
        } else if (is_abstract) {
            error_ctx_error_d(errs, overload_name(item), 2516, "All declarations of an abstract method must be consecutive.");
        } else {
            error_ctx_error_d(errs, overload_name(item), 2391,
                "Function implementation is missing or not immediately following the declaration.");
        }
    }
    // Two implementations of a method (constructors have TS2392)
    for (isize i = 0; i < count; i++) {
        ClassItem const* item = vec_class_item_at(&my items, i);
        FunctionImpl const* func = overloadable(item);
        if (!func || item->tag == CLASS_ITEM_CONSTRUCTOR || item->method.computed_key) continue;
        bool first = true, is_static = item->method.is_static;
        u32 bodies = 0;
        for (isize j = 0; j < count; j++) {
            ClassItem const* other = vec_class_item_at(&my items, j);
            if (!overloadable(other) || !same_member(item, other) || other->method.is_static != is_static) continue;
            if (j < i) first = false;
            if (other->method.func.has_body) bodies++;
        }
        if (!first || bodies < 2) continue;
        for (isize j = 0; j < count; j++) {
            ClassItem const* other = vec_class_item_at(&my items, j);
            if (!overloadable(other) || !same_member(item, other) || other->method.is_static != is_static) continue;
            error_ctx_error_d(errs, other->method.name_span, 2393, "Duplicate function implementation.");
        }
    }
}

void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs) {
    if (my has_typechecked) {
        return;
    }
    my has_typechecked = true;

    for_each (i, vec_expr, &my decorators) {
        expr_typecheck(i, errs);
    }
    for_each (i, vec_class_item, &my items) {
        if (i->tag == CLASS_ITEM_FIELD && (i->field.is_declare || i->field.is_abstract) &&
            vec_expr_size(&i->decorators) && !compiler_options()->experimental_decorators
        ) {
            // tsc's checkGrammarModifiers via nodeCanBeDecorated(), at the `@` of the first decorator
            Span at = i->span;
            at.sv = csview_from_n(span_start_ptr(i->span), 1);
            error_ctx_grammar_error_d(errs, at, 1206, "Decorators are not valid here.");
        }
        for_each (decorator, vec_expr, &i->decorators) {
            expr_typecheck(decorator, errs);
        }
    }

    generic_typecheck(&my generic, errs);

    if (my extends.tag != EXPR_IMPLICIT_ANY) {
        expr_typecheck(&my extends, errs);
        Ty const* extends_type = ty_expr_type(current_checker, &my extends);
        if (extends_type->flags & TYF_NULL) {
        } else {
            CallExpr call_expr = {
                .uses_new = true,
                .is_extends = true,
                .callable = expr_implicit_any(my extends.outer_span),
                .args = vec_expr_init(),
            };
            ty_call_typecheck(current_checker, extends_type, &call_expr, nullptr, my extends.outer_span, errs);
        }
    }

    class_heritage_typecheck(me, errs);
    class_check_overloads(me, errs);
    ty_check_class_duplicate_members(current_checker, me, errs);
    if (my symbol && symbol_find_node(my symbol, SYMBOL_DECL_CLASS) == me) {
        ty_check_type_parameter_lists_identical(current_checker, my symbol, errs);
    }
    {
        InterfaceMember const* indexes[16];
        u32 index_count = 0;
        for_each (i, vec_class_item, &my items) {
            if (i->tag == CLASS_ITEM_INTERFACE_MEMBER && !i->interface_member_static &&
                i->interface_memeber.key.tag == INTERFACE_KEY_INDEX && index_count < c_arraylen(indexes)
            ) {
                indexes[index_count++] = &i->interface_memeber;
            }
        }
        if (index_count > 1) ty_check_duplicate_index_signatures(current_checker, indexes, index_count, errs);
    }
    if (my symbol) {
        Checker* checker = current_checker;
        ty_check_index_constraints(checker, ty_declared_type_of(checker, my symbol), my symbol, false, nullptr, 0, my name, errs);
        ty_check_index_constraints(checker, ty_static_type_of(checker, my symbol), my symbol, true, nullptr, 0, my name, errs);
    }

    int constructor_count = 0; // Implementations
    for_each (i, vec_class_item, &my items) {
        // tsc's checkGrammarProperty/Method/Accessor (not for an ambient class's accessors)
        Span private_name = class_item_private_name(i);
        bool ambient_accessor = i->tag == CLASS_ITEM_METHOD && i->method.accessor && my symbol && my symbol->is_ambient;
        if (span_valid(private_name) && compiler_options()->target < SCRIPT_TARGET_ES2015 && !ambient_accessor) {
            error_ctx_grammar_error_d(errs, private_name, 18028,
                "Private identifiers are only available when targeting ECMAScript 2015 and higher.");
        }
        switch (i->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                function_impl_typecheck(&i->constructor.func, errs);
                // tsc's checkConstructorDeclaration. TODO: TS2401/TS2376 (where the call must be with initialized
                // properties or parameter properties)
                if (i->constructor.func.has_body && my extends.tag != EXPR_IMPLICIT_ANY) {
                    Expr const* super_call = i->constructor.func.first_super_call;
                    bool extends_null = my extends.tag == EXPR_LITERAL && my extends.literal.tag == VALUE_NULL;
                    if (super_call && extends_null) {
                        error_ctx_error_d(errs, super_call->inner_span, 17005,
                            "A constructor cannot contain a 'super' call when its class extends 'null'.");
                    } else if (!super_call && !extends_null) {
                        error_ctx_error_d(errs, i->constructor.name_span, 2377,
                            "Constructors for derived classes must contain a 'super' call.");
                    }
                }
                if (i->constructor.func.has_body) constructor_count++; // Overloads are fine
                break;

            case CLASS_ITEM_METHOD:
                if (i->method.computed_key) class_computed_key_typecheck(i, errs);
                function_impl_typecheck(&i->method.func, errs);
                break;

            case CLASS_ITEM_FIELD: {
                if (i->field.computed_key) class_computed_key_typecheck(i, errs);
                expr_typecheck(&i->field.value, errs);
                if (i->field.has_explicit_type) {
                    ty_check_assignable_expr(
                        current_checker, expr_get_type(&i->field.value), ty_of_node(&i->field.explicit_type),
                        i->field.name_span, i->field.value.tag == EXPR_IMPLICIT_ANY ? nullptr : &i->field.value, errs
                    );
                }
            }   break;

            case CLASS_ITEM_INTERFACE_MEMBER:
                interface_member_typecheck(&i->interface_memeber, true, errs);
                break;

            case CLASS_ITEM_STATIC_BLOCK:
                block_typecheck(&i->static_block, (BlockTypecheckCtx){ .in_function = true }, errs);
                break;
        }
    }

    if (constructor_count > 1) {
        for_each (i, vec_class_item, &my items) {
            if (i->tag == CLASS_ITEM_CONSTRUCTOR && i->constructor.func.has_body) {
                check(span_valid(i->constructor.name_span));
                check(span_valid(i->constructor.func.full_span));
                error_ctx_error_d(
                    errs,
                    span_between(i->constructor.name_span, i->constructor.func.full_span),
                    2392,
                    "Multiple constructor implementations are not allowed."
                );
            }
        }
    }

    for_each (i, vec_type, &my implements) {
        ignore i;
        // TODO: check that we do, in fact, implement each type
    }
}
