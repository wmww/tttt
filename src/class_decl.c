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

ClassItem* class_decl_find_member(ClassDecl const* me, csview name, bool is_static) {
    for_each (i, vec_class_item, &my items) {
        if (i->tag == CLASS_ITEM_FIELD && !i->field.computed_key && i->field.is_static == is_static &&
            csview_equals_sv(cstr_sv(&i->field.name_string), name)
        ) {
            return (ClassItem*)i;
        }
        if (i->tag == CLASS_ITEM_METHOD && !i->method.computed_key && i->method.is_static == is_static &&
            csview_equals_sv(cstr_sv(&i->method.name_string), name)
        ) {
            return (ClassItem*)i;
        }
    }
    return nullptr;
}

// What `this.x` is at the end of a flow container: null if nothing but null/undefined was assigned (tsc's
// getFlowTypeInConstructor)
static Ty const* field_flow_type(Checker* checker, Expr const* reference, FlowNode const* end) {
    Ty const* ret = ty_flow_type(checker, reference, ty_intrinsic(checker, TY_AUTO), ty_intrinsic(checker, TY_UNDEFINED), end);
    if (ret == ty_intrinsic(checker, TY_AUTO)) return ty_intrinsic(checker, TY_ANY);
    if (ret->kind != TY_UNION) return ret->flags & TYF_NULLABLE ? nullptr : ret;
    for (u32 i = 0; i < ret->composite.types.len; i++) {
        if (!(ret->composite.types.data[i]->flags & TYF_NULLABLE)) return ret;
    }
    return nullptr;
}

// A TypeScript field without a type or initializer under noImplicitAny: the type `this.x` has at the end of the
// constructor, or of the first static block that assigns more than null/undefined. Null: none (any)
static Ty const* field_type_from_assignments(Checker* checker, ClassItem const* me) {
    if (!my field.ts_assigned || !compiler_options()->no_implicit_any) return nullptr;
    for_each (i, vec_class_item, &my field.ts_assigned_class->items) {
        if (my field.is_static && i->tag == CLASS_ITEM_STATIC_BLOCK && i->static_block_return_flow) {
            Ty const* ret = field_flow_type(checker, my field.ts_assigned, i->static_block_return_flow);
            if (ret) return ret;
        }
        if (!my field.is_static && i->tag == CLASS_ITEM_CONSTRUCTOR && i->constructor.func.has_body) {
            FlowNode const* end = i->constructor.func.return_flow;
            return end ? field_flow_type(checker, my field.ts_assigned, end) : nullptr;
        }
    }
    return nullptr;
}

Ty const* class_item_field_evaluate_type(ClassItem* me) {
    check(my tag == CLASS_ITEM_FIELD);
    Checker* checker = current_checker;
    if (my field.has_explicit_type) return ty_of_node(&my field.explicit_type);
    if (my field.js_assigned) return ty_type_of_symbol(checker, my field.js_assigned);
    if (my field.value.tag == EXPR_IMPLICIT_ANY) {
        Ty const* assigned = field_type_from_assignments(checker, me);
        if (assigned) return assigned;
    }
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
    vec_spanned_type_drop(&my implements);
    vec_expr_drop(&my unchecked_extends);
    vec_type_drop(&my unchecked_implements);
    vec_expr_drop(&my invalid_implements);
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
            if (my method.computed_key) {
                names->flow->type_only_alias_ok = my method.is_abstract;
                lookup_names_without_super(my method.computed_key, names, NO_SUPER_COMPUTED_NAME, errs);
                names->flow->type_only_alias_ok = false;
            }
            lazy_type_bind(&my method.accessor_type, LAZY_TYPE_CLASS_ITEM_ACCESSOR);
            my method.func.is_method = !my method.accessor;
            my method.func.accessor = (uint8_t)my method.accessor;
            my method.func.member_name = my method.name_span;
            function_impl_lookup_names(&my method.func, false, names, errs);
            break;

        case CLASS_ITEM_FIELD:
            if (my field.computed_key) {
                names->flow->type_only_alias_ok = my field.is_abstract || my field.is_declare;
                lookup_names_without_super(my field.computed_key, names, NO_SUPER_COMPUTED_NAME, errs);
                names->flow->type_only_alias_ok = false;
            }
            lazy_type_bind(&my field.lazy_type, LAZY_TYPE_CLASS_ITEM_FIELD);
            type_lookup_names(&my field.explicit_type, names, errs);
            if (my field.value.tag == EXPR_IMPLICIT_ANY) {
                expr_lookup_names(&my field.value, names, errs);
            } else {
                // An initializer is a control flow container of its own
                FlowBuilder* outer = names->flow;
                FlowBuilder flow;
                flow_builder_init(&flow, outer, names, nullptr, errs);
                flow.contextual_depth += my field.has_explicit_type;
                flow.current = flow_new_start(&flow, nullptr, false);
                flow.initializer_context = my field.is_static ? INITIALIZER_STATIC_FIELD : INITIALIZER_INSTANCE_FIELD;
                flow.initializer_field = me;
                if (my field.is_static) {
                    // Evaluated with the class: in a parameter's initializer it's still there (tsc's getIsDeferredContext)
                    flow.parameter_initializing = outer->parameter_initializing;
                    flow.parameter_function_scope = outer->parameter_function_scope;
                }
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
            my static_block_return_flow = block_lookup_names_inline_container(&my static_block, block_scope, errs);
            break;
    }
}

Span class_decl_display_name(ClassDecl const* me) {
    if (span_valid(my name)) return my name;
    if (span_valid(my assigned_name)) return my assigned_name;
    return sv_make_span(my is_expression ? c_sv("(Anonymous class)") : c_sv("default")); // `export default class {}`
}

void class_decl_add_names(ClassDecl* me, Scope* scope, DeclExported exported, bool declared) {
    my is_ambient = declared || scope->ambient;
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported == DECL_EXPORTED_YES,
        .default_export = exported == DECL_EXPORTED_YES_DEFAULT,
        .is_ambient = declared,
        .kind = SYMBOL_DECL_CLASS,
        .node = me,
    });
}

// The value names the constructor (with a body) declares, when instance fields are initialized in it: those names
// can't be used by the fields (tsc's propertyWithInvalidInitializer). In the binder's arena, *count 0 if none
static Span const* constructor_locals(ClassDecl const* me, Arena* arena, u32* count) {
    *count = 0;
    CompilerOptions const* options = compiler_options();
    if (options->use_define_for_class_fields && options->target >= SCRIPT_TARGET_ES2022) return nullptr;
    FunctionImpl const* constructor = nullptr;
    for_each (i, vec_class_item, &my items) {
        if (i->tag == CLASS_ITEM_CONSTRUCTOR && i->constructor.func.has_body) {
            constructor = &i->constructor.func;
            break;
        }
    }
    if (!constructor) return nullptr;
    VecSpan names = {0};
    function_collect_local_value_names(constructor, &names);
    Span* ret = nullptr;
    if (names.size) {
        ret = arena_new_n(arena, Span, names.size);
        memcpy(ret, names.data, sizeof(Span) * (size_t)names.size);
        *count = (u32)names.size;
    }
    vec_span_drop(&names);
    return ret;
}

static void class_members_lookup_names(ClassDecl* me, Scope* names, ErrorCtx* errs, bool static_members) {
    Scope* this_scope = scope_new(names, SCOPE_BLOCK);
    this_scope->this_ref = (ThisRef){
        .kind = static_members ? THIS_CLASS_STATIC : THIS_CLASS_INSTANCE, .symbol = my symbol, .class_decl = me
    };
    this_scope->strict = true;

    u32 local_count = 0;
    Span const* locals = static_members ? nullptr : constructor_locals(me, &names->binder->arena, &local_count);
    for_each (i, vec_class_item, &my items) {
        if (class_item_is_static(i) == static_members) {
            Scope* scope = this_scope;
            if (local_count && i->tag == CLASS_ITEM_FIELD) {
                scope = scope_new(this_scope, SCOPE_BLOCK);
                scope->instance_field = i;
                scope->constructor_locals = locals;
                scope->constructor_local_count = local_count;
            }
            if (static_members && i->tag == CLASS_ITEM_FIELD && !vec_expr_is_empty(&my decorators) &&
                compiler_options()->experimental_decorators
            ) {
                scope = scope_new(this_scope, SCOPE_BLOCK);
                scope->decorated_static_field = true;
            }
            class_item_lookup_names(i, scope, errs);
        }
    }
}

void class_decl_lookup_names(ClassDecl* me, Scope* names, ErrorCtx* errs) {
    for_each (i, vec_expr, &my decorators) {
        expr_lookup_names(i, names, errs);
    }

    my outer_class = scope_enclosing_class(names);
    unused_register(names, UNUSED_CLASS, me, span_null);
    my outer_generics = names->generics;
    names = scope_new_generic_merged(names, &my generic, my symbol, me, class_decl_display_name(me), errs);

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

    Expr* base = my extends.tag == EXPR_GENERIC_PARAMS ? &my extends.generic_params->expr : &my extends;
    if (base->tag == EXPR_IDENTIFIER) base->identifier.class_base = true;
    expr_lookup_names(&my extends, names, errs);
    for_each (i, vec_spanned_type, &my implements) {
        type_lookup_names(&i->type, names, errs);
    }
    for_each (i, vec_expr, &my unchecked_extends) expr_lookup_names(i, names, nullptr);
    for_each (i, vec_type, &my unchecked_implements) type_lookup_names(i, names, nullptr);
    for_each (i, vec_expr, &my invalid_implements) expr_lookup_names(i, names, nullptr);

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
    if (!expr_is_non_bindable_dynamic_name(key)) return;
    Span span = field ? item->field.name_span : item->method.name_span;
    if (field) {
        error_ctx_grammar_error_d(errs, span, 1166,
            "A computed property name in a class property declaration must have a simple literal type or a 'unique symbol' type.");
    } else {
        error_ctx_grammar_error_d(errs, span, 1168,
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

// What an instance member of a class is, for tsc's checkKindsOfPropertyMemberOverrides()
typedef enum MemberKind {
    MEMBER_NONE,
    MEMBER_PROPERTY,
    MEMBER_ACCESSOR, // get/set, `accessor x`
    MEMBER_METHOD,
} MemberKind;

typedef struct MemberInfo {
    MemberKind kind;
    bool is_abstract;
    bool is_private;
    bool is_declare;
    bool uninitialized_field; // A property declaration without an initializer
    bool definite; // `x!: T`
    bool identifier_name;
    Span name;
} MemberInfo;

static MemberInfo class_instance_member(Checker* checker, ClassDecl const* me, Atom const* name) {
    MemberInfo ret = {0};
    for_each (i, vec_class_item, &my items) {
        if ((i->tag != CLASS_ITEM_FIELD && i->tag != CLASS_ITEM_METHOD) || class_item_is_static((ClassItem*)i)) continue;
        if (item_member_name(checker, me, i) != name) continue;
        if (i->tag == CLASS_ITEM_FIELD) {
            ret.kind = i->field.is_auto_accessor ? MEMBER_ACCESSOR : MEMBER_PROPERTY;
            ret.is_abstract = i->field.is_abstract;
            ret.is_declare = i->field.is_declare;
            ret.uninitialized_field = !i->field.is_auto_accessor && i->field.value.tag == EXPR_IMPLICIT_ANY;
            // `x!: T` (the parser doesn't keep the `!`)
            char const* after = span_valid(i->field.name_span) ? span_end_ptr(i->field.name_span) : nullptr;
            while (after && (*after == ' ' || *after == '\t')) after++;
            ret.definite = after && *after == '!';
            ret.identifier_name = !i->field.computed_key && !i->field.name_is_value;
            ret.name = i->field.name_span;
        } else {
            ret.kind = i->method.accessor ? MEMBER_ACCESSOR : MEMBER_METHOD;
            ret.is_abstract = i->method.is_abstract;
            ret.name = i->method.name_span;
        }
        ret.is_private = i->access == ACCESS_MODIFIER_PRIVATE;
        return ret;
    }
    // `constructor(public x: T)`
    for_each (i, vec_class_item, &my items) {
        if (i->tag != CLASS_ITEM_CONSTRUCTOR) continue;
        for_each (arg, vec_function_arg, &i->constructor.func.args) {
            if (!arg->is_parameter_property || arg->initializer.binding.tag != BINDING_IDENTIFIER) continue;
            if (checker_atom(checker, arg->initializer.binding.content_span.sv) != name) continue;
            ret.kind = MEMBER_PROPERTY;
            ret.is_private = arg->access == ACCESS_MODIFIER_PRIVATE;
            ret.name = arg->initializer.binding.content_span;
            return ret;
        }
    }
    return ret;
}

static ClassDecl const* class_of_prop(Prop const* prop) {
    while (prop->target) prop = prop->target;
    return prop->parent ? symbol_find_node(prop->parent, SYMBOL_DECL_CLASS) : nullptr;
}

// tsc's checkKindsOfPropertyMemberOverrides(): what an instance member of the base class is overridden with (a
// property with an accessor or the other way around, a method with an accessor...), and the abstract members a
// class that isn't abstract inherits without implementing
static void class_check_member_overrides(ClassDecl* me, Ty const* type, Ty const* base, ErrorCtx* errs) {
    Checker* checker = current_checker;
    CompilerOptions const* options = compiler_options();
    cstr missing = cstr_null; // The first four (all five when there are five), quoted
    u32 missing_count = 0;
    Atom const* first_missing = nullptr;
    Atom const* five = nullptr; // The fifth: listed when it is the last
    // (A mixin's base is an intersection: the properties of every part, each name once)
    TyList parts = base->kind == TY_INTERSECTION ? base->composite.types : (TyList){ .data = &base, .len = 1 };
    for (u32 part = 0; part < parts.len; part++) {
        Ty const* part_type = parts.data[part];
        if (!(part_type->flags & TYF_OBJECT)) part_type = ty_apparent_type(checker, part_type);
        if (!(part_type->flags & TYF_OBJECT)) continue;
        Members const* base_members = ty_members(checker, part_type);
        for (u32 i = 0; i < base_members->prop_count; i++) {
            Prop const* base_prop = base_members->props[i];
            if (base_prop->flags & PROP_STATIC) continue;
            bool seen = false;
            for (u32 earlier = 0; earlier < part && !seen; earlier++) {
                Ty const* earlier_type = parts.data[earlier];
                seen = (earlier_type->flags & TYF_OBJECT) && ty_get_prop(checker, earlier_type, base_prop->name);
            }
            if (seen) continue;
            ClassDecl const* base_class = class_of_prop(base_prop);
            if (!base_class) continue; // (an interface merged into the class)
            MemberInfo base_info = class_instance_member(checker, base_class, base_prop->name);
            if (!base_info.kind) continue;
            Prop const* derived_prop = ty_get_prop(checker, type, base_prop->name);
            if (!derived_prop) continue;
            ClassDecl const* derived_class = class_of_prop(derived_prop);
            if (!derived_class) continue;
            if (derived_class == base_class) {
                // Inherited as it is: an abstract member has to be implemented
                if (base_info.is_abstract && !my is_abstract) {
                    if (!missing_count) first_missing = base_prop->name;
                    if (missing_count < 5) {
                        if (missing_count == 4) five = base_prop->name;
                        else cstr_append_d(&missing, DEFAULT_DISPLAY, missing_count ? ", '" : "'", atom_sv(base_prop->name), "'");
                    }
                    missing_count++;
                }
                continue;
            }
            if (derived_class != me) continue;
            MemberInfo derived_info = class_instance_member(checker, derived_class, base_prop->name);
            if (!derived_info.kind || base_info.is_private || derived_info.is_private) continue;
            bool base_property_like = base_info.kind == MEMBER_PROPERTY || base_info.kind == MEMBER_ACCESSOR;
            bool derived_property_like = derived_info.kind == MEMBER_PROPERTY || derived_info.kind == MEMBER_ACCESSOR;
            Span at = derived_info.name;
            if (base_property_like && derived_property_like) {
                if (base_info.is_abstract && (base_info.kind != MEMBER_PROPERTY || base_info.uninitialized_field)) continue;
                if (base_info.kind == MEMBER_ACCESSOR && derived_info.kind == MEMBER_PROPERTY) {
                    error_ctx_error_d(errs, at, 2610, "'", atom_sv(base_prop->name), "' is defined as an accessor in class '",
                        base, "', but is overridden here in '", type, "' as an instance property.");
                } else if (base_info.kind == MEMBER_PROPERTY && derived_info.kind == MEMBER_ACCESSOR) {
                    error_ctx_error_d(errs, at, 2611, "'", atom_sv(base_prop->name), "' is defined as a property in class '",
                        base, "', but is overridden here in '", type, "' as an accessor.");
                } else if (options->use_define_for_class_fields && derived_info.uninitialized_field && !base_info.is_abstract &&
                    !derived_info.is_abstract && !derived_info.is_declare && !my is_ambient
                ) {
                    // TODO: whether the constructor initializes it (tsc's isPropertyInitializedInConstructor, flow
                    // analysis): until then only the cases that don't depend on it
                    bool has_constructor = false;
                    for_each (item, vec_class_item, &my items) {
                        if (item->tag == CLASS_ITEM_CONSTRUCTOR && item->constructor.func.has_body) has_constructor = true;
                    }
                    if (derived_info.definite || !has_constructor || !derived_info.identifier_name || !options->strict_null_checks) {
                        error_ctx_error_d(errs, at, 2612, "Property '", atom_sv(base_prop->name), "' will overwrite the base "
                            "property in '", base, "'. If this is intentional, add an initializer. Otherwise, add a 'declare' "
                            "modifier or remove the redundant declaration.");
                    }
                }
            } else if (base_info.kind == MEMBER_METHOD) {
                if (derived_info.kind == MEMBER_ACCESSOR) {
                    error_ctx_error_d(errs, at, 2423, "Class '", base, "' defines instance member function '",
                        atom_sv(base_prop->name), "', but extended class '", type, "' defines it as instance member accessor.");
                }
            } else if (base_info.kind == MEMBER_ACCESSOR) {
                error_ctx_error_d(errs, at, 2426, "Class '", base, "' defines instance member accessor '",
                    atom_sv(base_prop->name), "', but extended class '", type, "' defines it as instance member function.");
            } else {
                error_ctx_error_d(errs, at, 2425, "Class '", base, "' defines instance member property '",
                    atom_sv(base_prop->name), "', but extended class '", type, "' defines it as instance member function.");
            }
        }
    }
    if (missing_count == 5) cstr_append_d(&missing, DEFAULT_DISPLAY, ", '", atom_sv(five), "'");
    if (missing_count > 5) {
        Span at = span_valid(my name) ? my name : block_first_token(my full_span);
        if (my is_expression && !span_valid(my name)) {
            error_ctx_error_d(errs, at, 2650, "Non-abstract class expression is missing implementations for the "
                "following members of '", base, "': ", missing, " and ", missing_count - 4, " more.");
        } else {
            error_ctx_error_d(errs, at, 2655, "Non-abstract class '", type, "' is missing implementations for the "
                "following members of '", base, "': ", missing, " and ", missing_count - 4, " more.");
        }
    } else if (missing_count) {
        Span at = span_valid(my name) ? my name : block_first_token(my full_span);
        if (my is_expression && !span_valid(my name)) {
            if (missing_count == 1) {
                error_ctx_error_d(errs, at, 2653, "Non-abstract class expression does not implement inherited abstract "
                    "member '", atom_sv(first_missing), "' from class '", base, "'.");
            } else {
                error_ctx_error_d(errs, at, 2656, "Non-abstract class expression is missing implementations for the "
                    "following members of '", base, "': ", missing, ".");
            }
        } else if (missing_count == 1) {
            error_ctx_error_d(errs, at, 2515, "Non-abstract class '", type, "' does not implement inherited abstract "
                "member ", atom_sv(first_missing), " from class '", base, "'.");
        } else {
            error_ctx_error_d(errs, at, 2654, "Non-abstract class '", type, "' is missing implementations for the "
                "following members of '", base, "': ", missing, ".");
        }
    }
    cstr_drop(&missing);
}

// tsc's isMixinConstructorType: one construct signature without type parameters, whose only parameter is `...args: any[]`
static bool is_mixin_constructor_type(Checker* checker, Ty const* type) {
    Members const* members = ty_members(checker, type);
    if (members->construct_count != 1) return false;
    Signature const* sig = members->construct[0];
    signature_resolve(checker, sig);
    if (sig->type_params.len || sig->param_count != 1 || !sig->params[0].rest) return false;
    Ty const* param = sig->params[0].type;
    if (param->flags & TYF_ANY) return true;
    return ty_is_array(checker, param) && (ty_reference_args(checker, param).data[0]->flags & TYF_ANY) &&
        !(ty_reference_args(checker, param).data[0]->flags & TYF_ERROR);
}

// A class extending a type variable is a mixin: its constructor has to take anything (TS2545), and it has to be
// abstract when the variable's constructor is (TS2797)
static void class_check_mixin(ClassDecl* me, ErrorCtx* errs) {
    Checker* checker = current_checker;
    Expr const* extends = &my extends;
    if (extends->tag == EXPR_GENERIC_PARAMS) extends = &extends->generic_params->expr;
    if (extends->tag == EXPR_IMPLICIT_ANY || !my symbol) return;
    Ty const* base_constructor = ty_expr_type(checker, extends);
    if (!(base_constructor->flags & TYF_TYPE_PARAM) && base_constructor->kind != TY_INDEXED) return;
    Span span = span_valid(my name) ? my name : my full_span;
    if (!is_mixin_constructor_type(checker, ty_static_type_of(checker, my symbol))) {
        error_ctx_error_d(errs, span, 2545, "A mixin class must have a constructor with a single rest parameter of type 'any[]'.");
        return;
    }
    Members const* members = ty_members(checker, ty_apparent_type(checker, base_constructor));
    bool abstract = false;
    for (u32 i = 0; i < members->construct_count; i++) abstract |= (members->construct[i]->flags & SIGNATURE_ABSTRACT) != 0;
    if (abstract && !my is_abstract) {
        error_ctx_error_d(errs, span, 2797, "A mixin class that extends from a type variable containing an abstract construct "
            "signature must also be declared 'abstract'.");
    }
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
    // A class that extends itself through its bases (tsc's circularity in getBaseConstructorTypeOfClass), and then
    // nothing is compared
    if (base && ty_has_base_type(checker, base, declared)) {
        error_ctx_error_d(errs, span, 2506, "'", span, "' is referenced directly or indirectly in its own base expression.");
        base = nullptr;
    }
    class_check_mixin(me, errs);
    if (base && (base->flags & (TYF_OBJECT | TYF_INTERSECTION))) {
        class_check_member_overrides(me, declared, base, errs);
        bool fits = ty_check_heritage(checker, type_with_this, ty_with_this_arg(checker, base, this_param), members, count,
            2415, "Class", "incorrectly extends base class", span, errs);
        // The static side only when the instance side fits
        Ty const* base_static = ty_apparent_type(checker, ty_expr_type(checker, &my extends));
        if (fits && !(base_static->flags & (TYF_ANY | TYF_NULLABLE))) {
            ty_check_static_heritage(checker, ty_static_type_of(checker, my symbol), base_static, span, errs);
        }
    }
    for_each (i, vec_spanned_type, &my implements) {
        Ty const* implemented = ty_of_node(&i->type);
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
    // The overloads of each method (and of the constructor) fit its implementation (TS2394)
    for (isize i = 0; i < count; i++) {
        ClassItem const* item = vec_class_item_at(&my items, i);
        FunctionImpl const* func = overloadable(item);
        if (!func || (item->tag == CLASS_ITEM_METHOD && item->method.computed_key)) continue;
        bool first = true;
        FunctionImpl const* implementation = nullptr;
        Span implementation_name = span_null;
        FunctionImpl const* overloads[32];
        Span names[32];
        u32 overload_count = 0;
        for (isize j = 0; j < count; j++) {
            ClassItem const* other = vec_class_item_at(&my items, j);
            FunctionImpl const* other_func = overloadable(other);
            if (!other_func || !same_member(item, other)) continue;
            if (item->tag == CLASS_ITEM_METHOD && other->method.is_static != item->method.is_static) continue;
            if (j < i) first = false;
            if (other_func->has_body) {
                if (!implementation) {
                    implementation = other_func;
                    implementation_name = overload_name(other);
                }
            } else if (overload_count < c_arraylen(overloads)) {
                names[overload_count] = overload_name(other);
                overloads[overload_count++] = other_func;
            }
        }
        if (first && implementation && overload_count) {
            ty_check_overloads_compatible(current_checker, implementation, implementation_name, overloads, names, overload_count, errs);
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

// A decorator, from its `@`
static Span decorator_span(Expr const* decorator) {
    Span ret = decorator->outer_span;
    const char* start = span_start_ptr(ret);
    const char* file_start = source_file_start(ret.source);
    while (start > file_start && start[-1] != '@') start--;
    if (start > file_start) start--;
    ret.sv = csview_from_n(start, span_end_ptr(ret) - start);
    return ret;
}

// nodeCanBeDecorated() of an ES decorator on a member (legacy decorators aren't asked)
static bool item_es_decorated(ClassItem const* item) {
    if (!vec_expr_size(&item->decorators)) return false;
    switch (item->tag) {
        case CLASS_ITEM_FIELD: return !(item->field.is_declare || item->field.is_abstract);
        case CLASS_ITEM_METHOD: return item->method.func.has_body;
        default: return false;
    }
}

// tsc's getFirstTransformableStaticClassElement(): where the emitter builds the class's static side, and so names the
// class with __setFunctionName (an invalid span if it doesn't)
static Span class_transformable_static_element(ClassDecl const* me) {
    CompilerOptions const* options = compiler_options();
    bool decorated = !options->experimental_decorators && options->target < SCRIPT_TARGET_ESNEXT &&
        vec_expr_size(&my decorators);
    bool standard_fields = options->use_define_for_class_fields && options->target >= SCRIPT_TARGET_ES2022;
    for_each (i, vec_class_item, &my items) {
        if (decorated && item_es_decorated(i)) return decorator_span(vec_expr_at(&my decorators, 0));
        if (options->target < SCRIPT_TARGET_ESNEXT) {
            if (i->tag == CLASS_ITEM_STATIC_BLOCK) return i->span;
            if (!class_item_is_static((ClassItem*)i)) continue;
            Span name = i->tag == CLASS_ITEM_METHOD ? i->method.name_span : i->field.name_span;
            if (span_valid(class_item_private_name(i))) return name;
            if (!standard_fields && i->tag == CLASS_ITEM_FIELD && i->field.value.tag != EXPR_IMPLICIT_ANY) return name;
        }
    }
    return span_null;
}

// The tslib helpers the class's emit uses: `extends` below ES2015, decorators (tsc's checkDecorators())
static void class_check_emit_helpers(ClassDecl* me, ErrorCtx* errs) {
    if (my is_ambient) return;
    CompilerOptions const* options = compiler_options();
    bool legacy = options->experimental_decorators;
    if (vec_expr_size(&my decorators) && !(legacy && my is_expression)) {
        Span at = decorator_span(vec_expr_at(&my decorators, 0));
        if (legacy) {
            ty_check_emit_helpers(current_checker, at, EMIT_HELPER_DECORATE, errs);
            if (options->emit_decorator_metadata) ty_check_emit_helpers(current_checker, at, EMIT_HELPER_METADATA, errs);
        } else if (options->target < SCRIPT_TARGET_ESNEXT) {
            ty_check_emit_helpers(current_checker, at, EMIT_HELPER_DECORATE, errs);
            if (!my is_expression && (!span_valid(my name) || span_valid(class_transformable_static_element(me)))) {
                ty_check_emit_helpers(current_checker, at, EMIT_HELPER_SET_FUNCTION_NAME, errs);
            }
        }
    }
    // tsc's checkClassExpressionExternalHelpers()
    if (my is_expression && my named_evaluation) {
        Span at = !legacy && options->target < SCRIPT_TARGET_ESNEXT && vec_expr_size(&my decorators) ?
            decorator_span(vec_expr_at(&my decorators, 0)) : class_transformable_static_element(me);
        if (span_valid(at)) {
            ty_check_emit_helpers(current_checker, at, EMIT_HELPER_SET_FUNCTION_NAME, errs);
            if (my named_evaluation == CLASS_NAMED_BY_COMPUTED_KEY) {
                ty_check_emit_helpers(current_checker, at, EMIT_HELPER_PROP_KEY, errs);
            }
        }
    }
    if (span_valid(my extends_clause) && options->target < SCRIPT_TARGET_ES2015) {
        ty_check_emit_helpers(current_checker, my extends_clause, EMIT_HELPER_EXTENDS, errs);
    }
    for_each (i, vec_class_item, &my items) {
        ClassItem const* item = i;
        if (vec_expr_size(&item->decorators)) {
            Span at = decorator_span(vec_expr_at(&item->decorators, 0));
            if (legacy) {
                bool valid = !my is_expression &&
                    (item->tag == CLASS_ITEM_FIELD || (item->tag == CLASS_ITEM_METHOD && item->method.func.has_body));
                if (valid) {
                    ty_check_emit_helpers(current_checker, at, EMIT_HELPER_DECORATE, errs);
                    if (options->emit_decorator_metadata) {
                        ty_check_emit_helpers(current_checker, at, EMIT_HELPER_METADATA, errs);
                    }
                }
            } else if (item_es_decorated(item) && options->target < SCRIPT_TARGET_ESNEXT) {
                ty_check_emit_helpers(current_checker, at, EMIT_HELPER_DECORATE, errs);
                bool private_name = span_valid(class_item_private_name(item));
                if (private_name && (item->tag == CLASS_ITEM_METHOD || item->field.is_auto_accessor)) {
                    ty_check_emit_helpers(current_checker, at, EMIT_HELPER_SET_FUNCTION_NAME, errs);
                }
                Expr const* computed = item->tag == CLASS_ITEM_METHOD ? item->method.computed_key : item->field.computed_key;
                if (computed) ty_check_emit_helpers(current_checker, at, EMIT_HELPER_PROP_KEY, errs);
            }
        }
        // Parameters: legacy decorators of methods and constructors with a body in a class declaration
        FunctionImpl const* function = item->tag == CLASS_ITEM_METHOD ? &item->method.func :
            item->tag == CLASS_ITEM_CONSTRUCTOR ? &item->constructor.func : nullptr;
        if (!legacy || !function || !function->has_body || my is_expression) continue;
        if (item->tag == CLASS_ITEM_METHOD && item->method.accessor == CLASS_ACCESSOR_GET) continue;
        for_each (arg, vec_function_arg, &function->args) {
            if (!vec_expr_size(&arg->decorators)) continue;
            Span at = decorator_span(vec_expr_at(&arg->decorators, 0));
            ty_check_emit_helpers(current_checker, at, EMIT_HELPER_DECORATE | EMIT_HELPER_PARAM, errs);
            if (options->emit_decorator_metadata) ty_check_emit_helpers(current_checker, at, EMIT_HELPER_METADATA, errs);
        }
    }
}

// tsc's isPrivateWithinAmbient: private members of an ambient class have no implicit any errors
static bool class_item_private_in_ambient(ClassDecl const* me, ClassItem const* item) {
    return my is_ambient && (item->access == ACCESS_MODIFIER_PRIVATE || span_valid(class_item_private_name(item)));
}

// tsc's reportImplicitAny of a property declaration: no type and no initializer, or (without strictNullChecks) an
// initializer that widens to any (`x = null`)
static void class_field_check_implicit_any(ClassDecl const* me, ClassItem const* item, ErrorCtx* errs) {
    Span name = item->field.name_span;
    if (item->field.has_explicit_type || !compiler_options()->no_implicit_any || !span_valid(name)) return;
    if ((name.source && name.source->is_js) || class_item_private_in_ambient(me, item) || item->field.ts_assigned) return;
    if (item->field.value.tag != EXPR_IMPLICIT_ANY) {
        if (compiler_options()->strict_null_checks) return;
        Ty const* value = ty_expr_type(current_checker, &item->field.value);
        if (value->kind != TY_INTRINSIC || !(value->flags & TYF_NULLABLE)) return;
    }
    error_ctx_error_d(errs, name, 7008, "Member '", name, "' implicitly has an 'any' type.");
}

// tsc's resolveBaseTypesOfClass: a base constructor that is a class is referenced like a type (getTypeFromClassOrInterfaceReference),
// which needs its type arguments
static void class_check_base_arity(ClassDecl const* me, ErrorCtx* errs) {
    Expr const* base = &my extends;
    u32 arg_count = 0;
    if (base->tag == EXPR_GENERIC_PARAMS) {
        arg_count = (u32)vec_spanned_type_size(&base->generic_params->params);
        base = &base->generic_params->expr;
    }
    if (base->tag != EXPR_IDENTIFIER || base->identifier.keyword || expr_is_perenthisized(base)) return;
    Symbol const* target = base->identifier.target;
    if (!target || target->is_placeholder || !(target->flags & SYMBOL_CLASS)) return;
    several_types_check_arity(target, arg_count, my extends.outer_span, errs);
}

// tsc's checkClassForStaticPropertyNameConflicts: a static member named like a property every function has (TS2699).
// Only `prototype` with useDefineForClassFields, the others are just redefined then
static void class_check_static_name_conflicts(ClassDecl const* me, ErrorCtx* errs) {
    for_each (i, vec_class_item, &my items) {
        if (!class_item_is_static(i)) continue;
        cstr const* name_string = nullptr;
        Expr const* computed_key = nullptr;
        Span name_span = span_null;
        if (i->tag == CLASS_ITEM_METHOD) {
            name_string = &i->method.name_string;
            computed_key = i->method.computed_key;
            name_span = i->method.name_span;
        } else if (i->tag == CLASS_ITEM_FIELD) {
            name_string = &i->field.name_string;
            computed_key = i->field.computed_key;
            name_span = i->field.name_span;
        } else {
            continue;
        }
        csview name = cstr_sv(name_string);
        if (computed_key) {
            // (tsc's getEffectivePropertyNameForPropertyNameNode: a computed name of a literal type)
            bool is_number = false;
            Atom const* atom = ty_property_name_of_key(current_checker, ty_expr_type(current_checker, computed_key), &is_number);
            name = atom ? atom_sv(atom) : c_sv("");
        }
        bool function_member = csview_equals(name, "name") || csview_equals(name, "length") ||
            csview_equals(name, "caller") || csview_equals(name, "arguments");
        if (!(function_member && !compiler_options()->use_define_for_class_fields) && !csview_equals(name, "prototype")) {
            continue;
        }
        error_ctx_error_d(errs, name_span, 2699, "Static property '", name, "' conflicts with built-in property 'Function.",
            name, "' of constructor function '", class_decl_display_name(me).sv, "'.");
    }
}

void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs) {
    if (my has_typechecked) {
        return;
    }
    my has_typechecked = true;

    // tsc's checkClassNameCollisionWithObject (named by the module option, not the file's format)
    if (!my is_ambient && span_valid(my name) && csview_equals(my name.sv, "Object") && my name.source &&
        source_file_emit_module_kind(my name.source) < MODULE_KIND_ES2015
    ) {
        static char const* const module_names[] = {
            [MODULE_KIND_NONE] = "None", [MODULE_KIND_COMMONJS] = "CommonJS", [MODULE_KIND_AMD] = "AMD",
            [MODULE_KIND_UMD] = "UMD", [MODULE_KIND_SYSTEM] = "System", [MODULE_KIND_ES2015] = "ES2015",
            [MODULE_KIND_ES2020] = "ES2020", [MODULE_KIND_ES2022] = "ES2022", [MODULE_KIND_ESNEXT] = "ESNext",
            [MODULE_KIND_NODE16] = "Node16", [MODULE_KIND_NODE18] = "Node18", [MODULE_KIND_NODENEXT] = "NodeNext",
            [MODULE_KIND_PRESERVE] = "Preserve",
        };
        ModuleKind module = compiler_options()->module;
        error_ctx_error_d(
            errs, my name, 2725, "Class name cannot be 'Object' when targeting ES5 with module ",
            module < c_arraylen(module_names) && module_names[module] ? module_names[module] : "ES2015", "."
        );
    }

    for_each (i, vec_expr, &my decorators) {
        expr_typecheck(i, errs);
    }
    if (!my is_ambient) class_check_static_name_conflicts(me, errs);
    bool legacy = compiler_options()->experimental_decorators;
    for_each (i, vec_class_item, &my items) {
        // tsc's checkGrammarModifiers via nodeCanBeDecorated(), at the `@` of the first decorator
        if (vec_expr_size(&i->decorators) && span_valid(i->span)) {
            bool valid = false;
            unsigned code = 1206;
            switch (i->tag) {
                case CLASS_ITEM_FIELD:
                    valid = legacy ? !my is_expression : !(i->field.is_declare || i->field.is_abstract);
                    break;
                case CLASS_ITEM_METHOD:
                    valid = i->method.func.has_body && (!legacy || !my is_expression);
                    if (!i->method.func.has_body && !i->method.accessor) code = 1249;
                    break;
                default:
                    break;
            }
            if (!valid) {
                Span at = i->span;
                at.sv = csview_from_n(span_start_ptr(i->span), 1);
                if (code == 1249) {
                    error_ctx_grammar_error_d(errs, at, 1249, "A decorator can only decorate a method implementation, not an overload.");
                } else {
                    error_ctx_grammar_error_d(errs, at, 1206, "Decorators are not valid here.");
                }
            }
        }
        // Parameters: only legacy decorators, of methods, setters and constructors with a body in a class declaration
        FunctionImpl const* function = i->tag == CLASS_ITEM_METHOD ? &i->method.func :
            i->tag == CLASS_ITEM_CONSTRUCTOR ? &i->constructor.func : nullptr;
        for (isize a = 0; function && a < vec_function_arg_size(&function->args); a++) {
            FunctionArg const* arg = vec_function_arg_at(&function->args, a);
            if (!vec_expr_size(&arg->decorators) || !span_valid(arg->span)) continue;
            bool getter = i->tag == CLASS_ITEM_METHOD && i->method.accessor == CLASS_ACCESSOR_GET;
            if (legacy && function->has_body && !getter && !my is_expression) continue;
            Span at = arg->span;
            at.sv = csview_from_n(span_start_ptr(arg->span), 1);
            error_ctx_grammar_error_d(errs, at, 1206, "Decorators are not valid here.");
        }
        for_each (decorator, vec_expr, &i->decorators) {
            expr_typecheck(decorator, errs);
        }
    }

    class_check_emit_helpers(me, errs);

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
        ty_check_property_modifiers(current_checker, my symbol, me, nullptr, errs);
    } else if (!my symbol) {
        ty_check_property_modifiers(current_checker, nullptr, me, nullptr, errs);
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
                i->constructor.func.private_ambient = class_item_private_in_ambient(me, i);
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
                i->method.func.private_ambient = class_item_private_in_ambient(me, i);
                if (i->method.accessor && !i->method.func.has_body && !i->method.is_abstract && !i->method.func.ambient) {
                    // tsc's checkGrammarAccessor: at the accessor's last character (its `;` if it has one)
                    Span full = i->method.func.full_span;
                    while (full.sv.size && strchr(" \t\r\n", full.sv.buf[full.sv.size - 1])) full.sv.size--;
                    full = span_with_semicolon(full);
                    char const* end = span_end_ptr(full);
                    error_ctx_grammar_error_d(errs, span_make(full.source, end - 1, end), 1005, "'{' expected.");
                }
                function_impl_typecheck(&i->method.func, errs);
                function_impl_check_implicit_any_return(&i->method.func, i->method.name_span, i->method.name_span, errs);
                break;

            case CLASS_ITEM_FIELD: {
                if (i->field.computed_key) class_computed_key_typecheck(i, errs);
                if (i->field.has_explicit_type) type_typecheck(&i->field.explicit_type, errs);
                if (i->field.has_explicit_type && i->field.value.tag != EXPR_IMPLICIT_ANY) {
                    // The initializer has the annotation as its contextual type (like a variable's)
                    Ty const* contextual = ty_from_node(current_checker, &i->field.explicit_type, (LowerCtx){ .generic = true });
                    ty_check_expr(current_checker, &i->field.value, contextual->flags & TYF_ANY ? nullptr : contextual);
                }
                expr_typecheck(&i->field.value, errs);
                class_field_check_implicit_any(me, i, errs);
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

    // (Whether the class implements them is class_check_heritage()'s business)
    for_each (i, vec_spanned_type, &my implements) {
        type_typecheck(&i->type, errs);
        // (A keyword type: what the parser made of an identifier in an expression's place, tsc's
        // checkAndReportErrorForUsingTypeAsValue)
        if (i->type.tag != TYPE_IDENTIFIER && is_primitive_type_name(i->span.sv)) {
            error_ctx_error_d(errs, i->span, 2864, "A class cannot implement a primitive type like '", i->span,
                "'. It can only implement other named object types.");
        }
    }
    for (u32 i = 0; i < my invalid_implements_checked; i++) {
        error_ctx_error_d(errs, my invalid_implements.data[i].outer_span, 2500,
            "A class can only implement an identifier/qualified-name with optional type arguments.");
    }
    class_check_base_arity(me, errs);
}
