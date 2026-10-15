#include "class_decl.h"
#include "port_statement.h"
#include "scope.h"
#include "binding.h"
#include "flow.h"
#include "ast_walk.h"
#include "ty/ty.h"
#include "statement.h"
#include "interface_member.h"
#include "callable_type.h"
#include "several_types.h"
#include "error.h"
#include "unused.h"
#include "source_file.h"
#include "block.h"
#include "name_index.h"

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
    return span_valid(name) && name.size > 1 && name.buf[0] == '#' ? name : span_null;
}

ClassItem const* class_decl_find_private_name(ClassDecl const* me, csview name) {
    for_each (i, vec_class_item, &my items) {
        Span item_name = class_item_private_name(i);
        if (span_valid(item_name) && name_equals(span_sv(item_name), name)) return i;
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

// A member's computed key when it is late bound (tsc's binder leaves it nameless: an entity name, or a dynamic one),
// null when the member has a name the binder declares: written, or a computed literal (`["kk"]` is `kk`)
static Expr const* item_late_key(ClassItem const* item) {
    Expr const* key = item->tag == CLASS_ITEM_FIELD ? item->field.computed_key : item->tag == CLASS_ITEM_METHOD ? item->method.computed_key : nullptr;
    return key && !computed_key_early_name(key, nullptr) ? key : nullptr;
}

ClassItem* class_decl_find_member(ClassDecl const* me, csview name, bool is_static) {
    for_each (i, vec_class_item, &my items) {
        if (i->tag == CLASS_ITEM_FIELD && !item_late_key(i) && i->field.is_static == is_static &&
            csview_equals_sv(cstr_sv(&i->field.name_string), name)
        ) {
            return (ClassItem*)i;
        }
        if (i->tag == CLASS_ITEM_METHOD && !item_late_key(i) && i->method.is_static == is_static &&
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
    // (In JavaScript always: tsc's getWidenedTypeForVariableLikeDeclaration)
    if (!my field.ts_assigned || !(compiler_options()->no_implicit_any || (span_source(my field.name_span) && span_source(my field.name_span)->is_js))) return nullptr;
    for_each (i, vec_class_item, &my field.ts_assigned_class->items) {
        if (my field.is_static && i->tag == CLASS_ITEM_STATIC_BLOCK && i->static_block_return_flow) {
            Ty const* ret = field_flow_type(checker, my field.ts_assigned, i->static_block_return_flow);
            if (ret) return ret;
        }
        if (!my field.is_static && i->tag == CLASS_ITEM_CONSTRUCTOR && i->constructor.func.has_body) {
            FlowNode const* end = function_impl_rare(&i->constructor.func)->return_flow;
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
    // A call of a single non-generic signature has its type without the call being resolved (tsc's
    // checkDeclarationInitializer, as for variables: initialized_binding_evaluate_type())
    Ty const* value = ty_quick_expr_type(checker, &my field.value, false);
    if (!value) value = ty_expr_type(checker, &my field.value);
    // `p = []` in JavaScript is any[] (tsc's widenTypeInferredFromInitializer, TS7008 in class_field_check_implicit_any())
    if (span_source(my field.name_span) && span_source(my field.name_span)->is_js && ty_is_empty_array_literal_type(checker, value)) {
        return ty_array(checker, ty_intrinsic(checker, TY_ANY), false);
    }
    Ty const* ret = ty_widened_for_declaration(checker, value, !my field.is_readonly);
    // A unique symbol made for another declaration is only a symbol here (tsc's widenTypeForVariableLikeDeclaration)
    if ((ret->flags & TYF_UNIQUE_ES_SYMBOL) && ret->literal.unique->key != me) ret = ty_intrinsic(checker, TY_ES_SYMBOL);
    return ret;
}

Ty const* class_item_accessor_evaluate_type(ClassItem* me) {
    check(my tag == CLASS_ITEM_METHOD && my method.accessor);
    Checker* checker = current_checker;
    // (With a computed name, only if it is a literal or unique symbol: tsc's isLateBindableName)
    Expr const* key = my method.computed_key;
    bool late_bound = !key || key->tag == EXPR_LITERAL ||
        (ty_expr_type(checker, (Expr*)key)->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL));
    if (my method.accessor == CLASS_ACCESSOR_SET && my method.accessor_getter && late_bound) {
        return lazy_type_get(&my method.accessor_getter->method.accessor_type);
    }
    Members const* members = ty_members(checker, ty_function_type(checker, &my method.func));
    if (members->call_count != 1) return ty_intrinsic(checker, TY_ANY);
    Signature const* sig = members->call[0];
    signature_resolve(checker, sig);
    if (my method.accessor == CLASS_ACCESSOR_GET) {
        // (A unit type only, like tsc's getReturnTypeFromBody without a contextual type: a union of literals stays)
        Ty const* ret = signature_return_type(checker, sig);
        return ty_widened_for_declaration(checker, ret, ty_is_unit(ret));
    }
    // A setter without a getter: its parameter's annotation, else any (tsc's getTypeOfAccessors; not what an
    // initializer or a rest parameter would make of it)
    for_each (i, vec_function_arg, &my method.func.args) {
        Binding const* binding = &i->initializer.binding;
        if (binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this")) continue;
        if (!binding->has_explicit_type) return ty_intrinsic(checker, TY_ANY);
        break;
    }
    return sig->param_count ? sig->params[0].type : ty_intrinsic(checker, TY_ANY);
}

Ty const* class_item_get_type(ClassItem* me) {
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR: return ty_function_type(current_checker, &my constructor.func);
        case CLASS_ITEM_METHOD:
            return my method.accessor ? lazy_type_get(&my method.accessor_type) : ty_function_type(current_checker, &my method.func);
        case CLASS_ITEM_FIELD:
            if (my field.joined_accessor) {
                ClassItem* joined = my field.joined_accessor;
                return joined->tag == CLASS_ITEM_FIELD ? lazy_type_get(&joined->field.lazy_type) : lazy_type_get(&joined->method.accessor_type);
            }
            return lazy_type_get(&my field.lazy_type);
        case CLASS_ITEM_INTERFACE_MEMBER: return ty_of_node(&my interface_memeber.type);
        case CLASS_ITEM_STATIC_BLOCK: return ty_intrinsic(current_checker, TY_NEVER);
    }
    impossible;
}

static void drop_namesakes(ClassDecl* me);

void class_decl_drop(ClassDecl* me) {
    vec_expr_drop(&my decorators);
    generic_drop(&my generic);
    expr_drop(&my extends);
    vec_spanned_type_drop(&my implements);
    vec_expr_drop(&my unchecked_extends);
    vec_type_drop(&my unchecked_implements);
    vec_expr_drop(&my invalid_implements);
    vec_class_item_drop(&my items);
    vec_expr_cptr_drop(&my computed_this_assignments);
    drop_namesakes(me);
    if (my jsdoc_augments) {
        type_drop(my jsdoc_augments);
        free(my jsdoc_augments);
    }
}

// A computed member name or a decorator. `own_container`: the name of a method, an accessor or a field with an
// initializer, which tsc's binder binds inside the member's own flow container (bindContainer saves and restores
// currentFlow): what it assigns isn't seen after the member
// `member`: the method or accessor whose computed name it is, its containing function to tsc's getContainingFunction
// `[K in T]` as a class member's name, a mapped type's member written in a class: tsc's isInvalidComputedPropertyName
// gives it errorType without checking the expression (TS7061 for a property said it all)
static bool class_item_key_is_misplaced_mapped(ClassItem const* item) {
    if (item->tag == CLASS_ITEM_METHOD && item->method.accessor) return false;
    Expr const* key = item->tag == CLASS_ITEM_FIELD ? item->field.computed_key :
        item->tag == CLASS_ITEM_METHOD ? item->method.computed_key : nullptr;
    return key && key->tag == EXPR_BINARY_OP && key->binary_op->op == BINARY_OP_IN && !expr_is_perenthisized(key);
}

static void lookup_names_without_super(
    Expr* expr, Scope* names, NoSuper why, bool own_container, FunctionImpl const* member, ErrorCtx* errs
) {
    ScopePendingMark pending_mark = scope_pending_mark(names->binder);
    Scope* scope = scope_new(names, SCOPE_BLOCK);
    scope->no_super = why;
    if (member) scope_rare_mut(scope)->decorated_function = member;
    if (why == NO_SUPER_COMPUTED_NAME) {
        ClassDecl const* class_decl = scope_enclosing_class(names);
        if (class_decl) scope_rare_mut(scope)->computed_name_of = &class_decl->generic;
    }
    uint8_t initializer_context = names->flow->initializer_context;
    bool member_name = names->flow->member_name;
    bool field_decorator = why == NO_SUPER_DECORATOR && names->parent && names->parent->in_field;
    names->flow->initializer_context = field_decorator ? INITIALIZER_FIELD_DECORATOR : INITIALIZER_NONE;
    if (why == NO_SUPER_COMPUTED_NAME) names->flow->member_name = true;
    FlowNode* flow_before = names->flow->current;
    expr_lookup_names(expr, scope, errs);
    if (own_container) names->flow->current = flow_before;
    names->flow->initializer_context = initializer_context;
    names->flow->member_name = member_name;
    scope_release(scope, pending_mark);
}

// tsc's nodeCanBeDecorated for a class member: checkDecorators looks at nothing else (TS1206/TS1249 instead)
static bool class_item_decoratable(ClassDecl const* class_decl, ClassItem const* item) {
    bool legacy = compiler_options()->experimental_decorators;
    if (legacy && span_valid(class_item_private_name(item))) return false;
    switch (item->tag) {
        case CLASS_ITEM_FIELD: return legacy ? !class_decl->is_expression : !(item->field.is_declare || item->field.is_abstract);
        case CLASS_ITEM_METHOD: return item->method.func.has_body && (!legacy || !class_decl->is_expression);
        default: return false;
    }
}

static void class_item_lookup_names(ClassDecl const* class_decl, ClassItem* me, Scope* names, ErrorCtx* errs) {
    // (What can't be decorated, a static block, a constructor, an overload..., is TS1206 and nothing else: tsc's
    // checkDecorators doesn't check it. Their names are still resolved for the baselines)
    ErrorCtx* decorator_errs = vec_expr_size(&my decorators) && !class_item_decoratable(class_decl, me) ?
        binder_discarded_errs(names->binder) : errs;
    if (my tag == CLASS_ITEM_FIELD) {
        names = scope_new(names, SCOPE_BLOCK);
        names->in_field = true;
    }
    // (Its decorators' flow container is the member, tsc's getControlFlowContainer: what they read is an outer variable)
    bool member_name = names->flow->member_name;
    names->flow->member_name = true;
    for_each (i, vec_expr, &my decorators) {
        Scope* scope = scope_new(names, SCOPE_BLOCK);
        scope->member_decorator = true;
        lookup_names_without_super(i, scope, NO_SUPER_DECORATOR, false, nullptr, decorator_errs);
    }
    names->flow->member_name = member_name;
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_lookup_names(&my constructor.func, false, names, errs);
            break;

        case CLASS_ITEM_METHOD:
            // (Named first: errors in the computed name may point at the method, TS1308's related information)
            function_impl_set_member_name(&me->method.func, my method.name_span);
            if (my method.computed_key) {
                names->flow->type_only_alias_ok = my method.is_abstract;
                bool method_name = names->flow->method_name;
                names->flow->method_name = true;
                lookup_names_without_super(my method.computed_key, names, NO_SUPER_COMPUTED_NAME, true, &my method.func,
                    class_item_key_is_misplaced_mapped(me) ? nullptr : errs);
                names->flow->method_name = method_name;
                names->flow->type_only_alias_ok = false;
            }
            lazy_type_bind(&my method.accessor_type, LAZY_TYPE_CLASS_ITEM_ACCESSOR);
            my method.func.is_method = !my method.accessor;
            my method.func.accessor = (uint8_t)my method.accessor;
            if (my method.is_declare && !my method.accessor && !names->ambient) {
                // (`declare m() {}`, TS1031: tsc's parseClassElement parses the member in an ambient context, unlike an
                // accessor, parsed before it looks for `declare`)
                names = scope_new(names, SCOPE_BLOCK);
                names->ambient = true;
            }
            function_impl_lookup_names(&my method.func, false, names, errs);
            break;

        case CLASS_ITEM_FIELD:
            if (my field.computed_key) {
                names->flow->type_only_alias_ok = my field.is_abstract || my field.is_declare;
                lookup_names_without_super(my field.computed_key, names, NO_SUPER_COMPUTED_NAME, my field.value.tag != EXPR_IMPLICIT_ANY,
                    nullptr, class_item_key_is_misplaced_mapped(me) ? nullptr : errs);
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
                flow.container = me;
                // Not deferred: using the variable a class expression initializes is using it in its initializer
                // (TS2448, tsc's isImmediatelyUsedInInitializerOfBlockScopedVariable stops at functions only)
                flow.initializing = outer->initializing;
                if (my field.is_static) {
                    // Evaluated with the class: in a parameter's initializer it's still there (tsc's getIsDeferredContext)
                    flow.parameter_initializing = outer->parameter_initializing;
                    flow.parameter_function_scope = outer->parameter_function_scope;
                }
                names->flow = &flow;
                expr_lookup_names(&my field.value, names, errs);
                flow_builder_finish(&flow, c_sv("field"), expr_outer_span(&my field.value));
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
    if (my is_expression) return sv_make_span(c_sv("(Anonymous class)"));
    // `export default class {}`; a class statement missing its name otherwise (a parse error) is tsc's
    // InternalSymbolName.Missing
    return sv_make_span(!my symbol || my symbol->is_default_export ? c_sv("default") : c_sv("__missing"));
}

void class_decl_add_names(ClassDecl* me, Scope* scope, DeclStatementProps props) {
    my is_ambient = props.declared || scope->ambient;
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = props.exported == DECL_EXPORTED_YES,
        .default_export = props.exported == DECL_EXPORTED_YES_DEFAULT,
        .declare_keyword = span_valid(props.declare_keyword),
        .is_ambient = props.declared,
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

bool class_item_private_in_ambient(ClassDecl const* me, ClassItem const* item);

static void class_members_lookup_names(ClassDecl* me, Scope* names, ErrorCtx* errs, bool static_members) {
    Scope* this_scope = scope_new(names, SCOPE_BLOCK);
    scope_rare_mut(this_scope)->this_ref = (ThisRef){
        .kind = static_members ? THIS_CLASS_STATIC : THIS_CLASS_INSTANCE, .symbol = my symbol, .class_decl = me
    };
    this_scope->strict = true;
    this_scope->class_strict = true;

    u32 local_count = 0;
    Span const* locals = static_members ? nullptr : constructor_locals(me, binder_arena(names->binder), &local_count);
    for_each (i, vec_class_item, &my items) {
        if (class_item_is_static(i) == static_members) {
            Scope* scope = this_scope;
            if (local_count && i->tag == CLASS_ITEM_FIELD) {
                scope = scope_new(this_scope, SCOPE_BLOCK);
                scope_rare_mut(scope)->instance_field = i;
                scope_rare_mut(scope)->constructor_locals = locals;
                scope_rare_mut(scope)->constructor_local_count = local_count;
            }
            if (static_members && i->tag == CLASS_ITEM_FIELD && !vec_expr_is_empty(&my decorators) &&
                compiler_options()->experimental_decorators
            ) {
                scope = scope_new(this_scope, SCOPE_BLOCK);
                scope->decorated_static_field = true;
            }
            if (static_members && i->tag == CLASS_ITEM_METHOD && !i->method.computed_key && !i->method.accessor) {
                function_impl_rare_mut(&i->method.func)->static_method_of = my symbol;
            }
            if (i->tag == CLASS_ITEM_METHOD) i->method.func.in_class_expression = my is_expression;
            // (Here, not when checking: the checkers share the AST)
            if (i->tag == CLASS_ITEM_CONSTRUCTOR) i->constructor.func.private_ambient = class_item_private_in_ambient(me, i);
            if (i->tag == CLASS_ITEM_METHOD) {
                i->method.func.private_ambient = class_item_private_in_ambient(me, i) || i->method.is_declare;
            }
            class_item_lookup_names(me, i, scope, errs);
        }
    }
}

// A class member's written name, staticness and binder flags, false if it has none (computed names are late bound)
enum { BIND_PROPERTY = 1, BIND_METHOD = 2, BIND_GET = 4, BIND_SET = 8 };
static bool member_binding(ClassItem const* item, cstr const** name, bool* is_static, u32* flags) {
    // (A bigint name is a symbol of its own outside the table: tsc's binder names it __missing)
    if (item->tag == CLASS_ITEM_FIELD ? span_is_bigint_name(item->field.name_span, item->field.name_is_value) && !item->field.computed_key :
        item->tag == CLASS_ITEM_METHOD && span_is_bigint_name(item->method.name_span, item->method.name_is_value) && !item->method.computed_key
    ) {
        return false;
    }
    if (item->tag == CLASS_ITEM_FIELD && !item_late_key(item)) {
        *name = &item->field.name_string;
        *is_static = item->field.is_static;
        *flags = item->field.is_auto_accessor ? BIND_GET | BIND_SET : BIND_PROPERTY;
        return true;
    }
    if (item->tag == CLASS_ITEM_METHOD && !item_late_key(item)) {
        *name = &item->method.name_string;
        *is_static = item->method.is_static;
        *flags = item->method.accessor == CLASS_ACCESSOR_GET ? BIND_GET :
            item->method.accessor == CLASS_ACCESSOR_SET ? BIND_SET : BIND_METHOD;
        return true;
    }
    return false;
}

// A method's or field's name_string (a computed key's text), null for other items
static cstr const* item_name_string(ClassItem const* item) {
    if (item->tag == CLASS_ITEM_METHOD) return &item->method.name_string;
    if (item->tag == CLASS_ITEM_FIELD) return &item->field.name_string;
    return nullptr;
}

static bool item_has_computed_key(ClassItem const* item) {
    return (item->tag == CLASS_ITEM_METHOD && item->method.computed_key) || (item->tag == CLASS_ITEM_FIELD && item->field.computed_key);
}

static void drop_namesakes(ClassDecl* me) {
    free(my namesake_first);
    free(my namesake_next);
    free(my computed_items);
    my namesake_first = my namesake_next = my computed_items = nullptr;
    my computed_item_count = 0;
}

// ClassDecl.namesake_first and the rest
static void link_namesakes(ClassDecl* me) {
    drop_namesakes(me);
    isize count = vec_class_item_size(&my items);
    if (count <= CLASS_NAMESAKES_FROM) return;
    my namesake_first = malloc(sizeof(i32) * (size_t)count);
    my namesake_next = malloc(sizeof(i32) * (size_t)count);
    my computed_items = malloc(sizeof(i32) * (size_t)count);
    panic_if(!my namesake_first || !my namesake_next || !my computed_items, "out of memory");
    NameIndex last = name_index_make(count); // The last item of each name so far, its chain's end
    for (isize i = 0; i < count; i++) {
        ClassItem const* item = vec_class_item_at(&my items, i);
        my namesake_next[i] = my namesake_first[i] = -1;
        cstr const* name = item_name_string(item);
        if (!name) continue;
        if (item_has_computed_key(item)) my computed_items[my computed_item_count++] = (i32)i;
        csview sv = cstr_sv(name);
        u64 slot = name_index_slot(&last, symbol_name_hash(sv));
        while (last.slots[slot] >= 0 && !cstr_equals_sv(item_name_string(vec_class_item_at(&my items, last.slots[slot])), sv)) {
            slot = name_index_next(&last, slot);
        }
        if (last.slots[slot] >= 0) {
            my namesake_next[last.slots[slot]] = (i32)i;
            my namesake_first[i] = my namesake_first[last.slots[slot]];
        } else {
            my namesake_first[i] = (i32)i;
        }
        last.slots[slot] = i;
    }
    name_index_drop(&last);
}

ClassNamesakes class_namesakes(ClassDecl const* decl, ClassItem const* item, bool with_computed) {
    ClassNamesakes ret = { .decl = decl, .chain = -1, .index = -1, .computed = decl->computed_item_count };
    isize at = item - decl->items.data;
    ret.all = !decl->namesake_first || decl->namesake_first[at] < 0 || (with_computed && item_has_computed_key(item));
    if (!ret.all) {
        ret.chain = decl->namesake_first[at];
        if (with_computed) ret.computed = 0;
    }
    return ret;
}

// tsc's binder for the members of a class, in order: a member joins the symbol the first one of its name made unless
// that symbol has a meaning it excludes (TypeScript 7: a property excludes all but properties and accessors, a method
// everything but methods, an accessor everything but the other accessor and properties), then it stands alone and isn't
// a member
static void bind_member_symbols(ClassDecl* me) {
    for_each (i, vec_class_item, &my items) {
        cstr const* name;
        bool is_static;
        u32 flags;
        i->symbol_first = nullptr;
        if (!member_binding(i, &name, &is_static, &flags)) continue;
        i->symbol_first = i;
        // The symbol in the table: the first of the name, what joined it so far
        ClassItem* first = nullptr;
        u32 existing = 0;
        for (ClassNamesakes it = class_namesakes(me, i, false); class_namesakes_next(&it) && it.item < i;) {
            ClassItem* j = it.item;
            cstr const* other_name;
            bool other_static;
            u32 other_flags;
            if (!j->symbol_first || !member_binding(j, &other_name, &other_static, &other_flags)) continue;
            // (A private name is its class's own, `#a` and `["#a"]` don't meet: tsc's `__#1@#a`)
            if (other_static != is_static || !cstr_eq(other_name, name) ||
                span_valid(class_item_private_name(j)) != span_valid(class_item_private_name(i))
            ) {
                continue;
            }
            if (!first) first = j;
            if (j->symbol_first == first) existing |= other_flags;
        }
        if (!first) continue;
        // (An auto-accessor is both accessors: tsc's AccessorExcludes, everything but a property)
        u32 excludes = flags == BIND_PROPERTY ? (u32)~(BIND_PROPERTY | BIND_GET | BIND_SET) : flags == BIND_METHOD ? (u32)~BIND_METHOD :
            flags == (BIND_GET | BIND_SET) ? (u32)~BIND_PROPERTY :
            flags == BIND_GET ? (u32)~(BIND_SET | BIND_PROPERTY) : (u32)~(BIND_GET | BIND_PROPERTY);
        if (!(existing & excludes)) i->symbol_first = first;
    }
}

// The first member of a computed name's late-bound symbol that `target` is part of (tsc's lateBindMember, which
// declares like the binder: a member that conflicts with the symbol already bound to the name stands alone), going
// by the key's source text the way accessor pairs are matched. `[k]() {}` before `get [k]()` and `set [k](v)` keeps
// both accessors alone: no pair. Null for a member without a computed name
static ClassItem const* computed_symbol_first(ClassDecl const* me, ClassItem const* target) {
    if (!item_late_key(target)) return nullptr;
    bool target_static = class_item_is_static((ClassItem*)target);
    cstr const* name = target->tag == CLASS_ITEM_METHOD ? &target->method.name_string : &target->field.name_string;
    // (Each earlier member of the name with the first of its symbol)
    ClassItem const* seen[64];
    ClassItem const* seen_first[64];
    u32 seen_count = 0;
    for (ClassItem const* i = my items.data; i <= target; i++) {
        bool method = i->tag == CLASS_ITEM_METHOD;
        if ((!method && i->tag != CLASS_ITEM_FIELD) || !item_late_key(i)) continue;
        if (class_item_is_static((ClassItem*)i) != target_static) continue;
        if (!cstr_eq(method ? &i->method.name_string : &i->field.name_string, name)) continue;
        u32 flags = i->tag == CLASS_ITEM_FIELD ? (i->field.is_auto_accessor ? BIND_GET | BIND_SET : BIND_PROPERTY) :
            i->method.accessor == CLASS_ACCESSOR_GET ? BIND_GET : i->method.accessor == CLASS_ACCESSOR_SET ? BIND_SET : BIND_METHOD;
        ClassItem const* first = seen_count ? seen[0] : nullptr;
        u32 existing = 0;
        for (u32 j = 0; j < seen_count; j++) {
            if (seen_first[j] != first) continue;
            ClassItem const* other = seen[j];
            existing |= other->tag == CLASS_ITEM_FIELD ? (other->field.is_auto_accessor ? BIND_GET | BIND_SET : BIND_PROPERTY) :
                other->method.accessor == CLASS_ACCESSOR_GET ? BIND_GET : other->method.accessor == CLASS_ACCESSOR_SET ? BIND_SET : BIND_METHOD;
        }
        u32 excludes = flags == BIND_PROPERTY ? (u32)~(BIND_PROPERTY | BIND_GET | BIND_SET) : flags == BIND_METHOD ? (u32)~BIND_METHOD :
            flags == (BIND_GET | BIND_SET) ? (u32)~BIND_PROPERTY :
            flags == BIND_GET ? (u32)~(BIND_SET | BIND_PROPERTY) : (u32)~(BIND_GET | BIND_PROPERTY);
        ClassItem const* own_first = first && !(existing & excludes) ? first : i;
        if (i == target) return own_first;
        if (seen_count == 64) return target; // (Pathological: treat it as alone)
        seen[seen_count] = i;
        seen_first[seen_count++] = own_first;
    }
    return target;
}

// Each overload signature of a method or constructor: its implementation, see FunctionImpl.overload_implementation
static void link_overload_implementations(ClassDecl* me) {
    for_each (i, vec_class_item, &my items) {
        bool is_constructor = i->tag == CLASS_ITEM_CONSTRUCTOR;
        if (is_constructor ? i->constructor.func.has_body :
            i->tag != CLASS_ITEM_METHOD || i->method.func.has_body || !i->symbol_first) continue;
        for (ClassNamesakes it = class_namesakes(me, i, false); class_namesakes_next(&it);) {
            ClassItem* j = it.item;
            if (is_constructor ? j->tag == CLASS_ITEM_CONSTRUCTOR && j->constructor.func.has_body :
                j->tag == CLASS_ITEM_METHOD && j->method.func.has_body && j->symbol_first == i->symbol_first
            ) {
                FunctionImpl* overload = is_constructor ? &i->constructor.func : &i->method.func;
                function_impl_rare_mut(overload)->overload_implementation = is_constructor ? &j->constructor.func : &j->method.func;
                break;
            }
        }
    }
}

u32 class_item_symbol_items(ClassItem const* me, VecClassItem const* items, ClassItem const** out, u32 cap) {
    u32 len = 0;
    if (!my symbol_first) return 0;
    bool alone = my symbol_first == me;
    for (ClassItem const* j = items->data; alone && j < items->data + items->size; j++) {
        if (j != me && j->symbol_first == me) alone = false;
    }
    if (alone) {
        if (cap) out[len++] = me;
        return len;
    }
    for (ClassItem const* j = items->data; j < items->data + items->size; j++) {
        if (j->symbol_first == my symbol_first && len < cap) out[len++] = j;
    }
    return len;
}

void class_decl_lookup_names(ClassDecl* me, Scope* names, ErrorCtx* errs) {
    // tsc's setNodeLinksForPrivateIdentifierScope and checkWeakMapSetCollision: private members are emitted through
    // WeakMap and WeakSet up to ES2021, which a runtime declaration of those names around the class would shadow
    CompilerOptions const* options = compiler_options();
    if (options->target <= SCRIPT_TARGET_ES2021 && !options->no_emit && !names->ambient) {
        bool private_names = false;
        for_each (i, vec_class_item, &my items) private_names |= span_valid(class_item_private_name(i));
        if (private_names) {
            report_emit_name_collisions(names, "WeakMap", true);
            report_emit_name_collisions(names, "WeakSet", true);
        }
    }
    if (vec_expr_size(&my decorators)) {
        Scope* scope = scope_new(names, SCOPE_BLOCK);
        scope_rare_mut(scope)->decorator_of = me;
        // (A class expression can't have legacy decorators: TS1206, and tsc's checkDecorators never looks at them)
        bool invalid = compiler_options()->experimental_decorators && my is_expression;
        for_each (i, vec_expr, &my decorators) {
            expr_lookup_names(i, scope, invalid ? nullptr : errs);
        }
    }

    my outer_class = scope_enclosing_class(names);
    unused_register(names, UNUSED_CLASS, me);
    my outer_generics = names->generics;
    if (!names->class_strict || !names->strict) {
        // (The type parameters, heritage clauses and body are strict code of the class)
        names = scope_new(names, SCOPE_BLOCK);
        names->strict = names->class_strict = true;
    }
    names = scope_new_generic_merged(names, &my generic, my symbol, me, class_decl_display_name(me), errs);

    for_each (i, vec_class_item, &my items) {
        if (i->tag == CLASS_ITEM_CONSTRUCTOR) {
            if (i->constructor.func.ret_explicit) {
                // (tsc's checkGrammarConstructorTypeAnnotation: grammarErrorOnNode)
                error_ctx_grammar_error_d(
                    errs,
                    function_impl_ret_span(&i->constructor.func),
                    1093,
                    "Type annotation cannot appear on a constructor declaration."
                );
                if (i->constructor.func.ret.tag == TYPE_ANY && span_valid(i->constructor.func.ret.any.unkown_symbol_span)) {
                    function_impl_rare_mut(&i->constructor.func)->constructor_missing_type = i->constructor.func.ret.any.unkown_symbol_span;
                }
                type_drop(&i->constructor.func.ret);
            }
            check(i->constructor.func.ret.tag == TYPE_ANY);
            i->constructor.func.ret_explicit = false;
            function_impl_rare_mut(&i->constructor.func)->constructed_class = my symbol;
            function_impl_rare_mut(&i->constructor.func)->constructor_access = (uint8_t)i->access;
        }
    }

    link_namesakes(me);
    bind_member_symbols(me);
    if (!my is_ambient) link_overload_implementations(me);
    for_each (i, vec_class_item, &my items) {
        if (i->tag == CLASS_ITEM_FIELD && i->field.is_static && i->field.is_readonly && !i->field.computed_key) {
            unique_symbol_decl_mark(
                (UniqueSymbolDecl){ .key = i, .owner = my symbol, .member = i->field.name_span },
                &i->field.explicit_type, &i->field.value
            );
        }
        if (i->tag == CLASS_ITEM_METHOD && i->method.accessor) {
            // (A computed name that isn't a literal or an entity name, `["" + ""]`, is a symbol of its own: tsc's
            // isLateBindableName)
            Expr const* own_key = item_late_key(i);
            bool bindable = !own_key || own_key->tag == EXPR_LITERAL || expr_is_entity_name(own_key);
            for (ClassNamesakes it = class_namesakes(me, i, false); class_namesakes_next(&it);) {
                ClassItem* other = it.item;
                if (!bindable && other != i) continue;
                if (other->tag == CLASS_ITEM_METHOD &&
                    other->method.accessor &&
                    other->method.is_static == i->method.is_static &&
                    class_methods_same_name(other, i) &&
                    other->symbol_first == i->symbol_first &&
                    (!own_key || computed_symbol_first(me, other) == computed_symbol_first(me, i))
                ) {
                    if (!i->method.accessor_first) {
                        i->method.accessor_first = other;
                    }
                    if (other->method.accessor == CLASS_ACCESSOR_GET && !i->method.accessor_getter) {
                        i->method.accessor_getter = other;
                        // (A computed name that isn't a literal or an entity name, `[1 << 6]`, makes no symbol that
                        // would pair the two: tsc's isLateBindableName)
                        Expr const* key = i->method.computed_key;
                        if (i->method.accessor == CLASS_ACCESSOR_SET && (!key || key->tag == EXPR_LITERAL || expr_is_entity_name(key))) {
                            function_impl_pair_setter(&i->method.func, &other->method.func, key);
                        }
                    }
                }
            }
        }
    }
    // A property an accessor after it joined: the accessors type their symbol
    for_each (i, vec_class_item, &my items) {
        if (i->tag != CLASS_ITEM_FIELD) continue;
        i->field.joined_accessor = nullptr;
        if (i->symbol_first != i || i->field.is_auto_accessor) continue;
        for (ClassNamesakes it = class_namesakes(me, i, false); class_namesakes_next(&it);) {
            ClassItem* j = it.item;
            if (j->symbol_first == i && j->tag == CLASS_ITEM_METHOD && j->method.accessor && j->method.accessor_first) {
                i->field.joined_accessor = j->method.accessor_first;
                break;
            }
            // (An auto-accessor is both accessors)
            if (j->symbol_first == i && j != i && j->tag == CLASS_ITEM_FIELD && j->field.is_auto_accessor) {
                i->field.joined_accessor = j;
                break;
            }
        }
    }

    Expr* base = my extends.tag == EXPR_GENERIC_PARAMS ? &my extends.generic_params->expr : &my extends;
    if (my extends.tag == EXPR_GENERIC_PARAMS) my extends.generic_params->heritage = true;
    if (base->tag == EXPR_IDENTIFIER) base->identifier.class_base = true;
    Expr* head = base;
    while (head->tag == EXPR_PROPERTY && head->property->form == PROPERTY_EXPR_DOT) head = &head->property->expr;
    if (head->tag == EXPR_IDENTIFIER && !head->identifier.keyword) head->identifier.extends_entity_ = base;
    if (!generic_empty(&my generic) && base->tag != EXPR_IMPLICIT_ANY) {
        // The base class expression can't see the class's type parameters, its type arguments can
        ScopePendingMark pending_mark = scope_pending_mark(names->binder);
        Scope* scope = scope_new(names, SCOPE_BLOCK);
        scope_rare_mut(scope)->base_expression_of = &my generic;
        if (my extends.tag == EXPR_GENERIC_PARAMS) {
            lazy_type_bind(&my extends.lazy_type, LAZY_TYPE_EXPR);
            expr_lookup_names(&my extends.generic_params->expr, scope, errs);
            for_each (i, vec_spanned_type, &my extends.generic_params->params) type_lookup_names(&i->type, names, errs);
        } else {
            expr_lookup_names(&my extends, scope, errs);
        }
        scope_release(scope, pending_mark);
    } else {
        expr_lookup_names(&my extends, names, errs);
    }
    for_each (i, vec_spanned_type, &my implements) {
        type_lookup_names(&i->type, names, errs);
    }
    if (my jsdoc_augments) type_lookup_names(my jsdoc_augments, names, errs);
    for_each (i, vec_expr, &my unchecked_extends) expr_lookup_names(i, names, nullptr);
    for_each (i, vec_type, &my unchecked_implements) type_lookup_names(i, names, nullptr);
    for_each (i, vec_expr, &my invalid_implements) expr_lookup_names(i, names, nullptr);

    class_members_lookup_names(me, names, errs, true);
    class_members_lookup_names(me, names, errs, false);
}

// TS1166/TS1168: the key of a property or of a method overload has to be something the class can be typed by
// (`modifier_error`: checkGrammarModifiers reported the member, which ends its grammar checks)
static void class_computed_key_typecheck(ClassDecl const* me, ClassItem* item, bool modifier_error, ErrorCtx* errs) {
    bool field = item->tag == CLASS_ITEM_FIELD;
    Expr* key = field ? item->field.computed_key : item->method.computed_key;
    bool misplaced_mapped = class_item_key_is_misplaced_mapped(item);
    // (Typed all the same, quietly: tsc's errorType for it is any to the members)
    expr_typecheck(key, misplaced_mapped ? nullptr : errs);
    bool accessor = !field && item->method.accessor;
    if (!misplaced_mapped) {
        ty_check_computed_name(current_checker, key, field ? item->field.name_span : item->method.name_span, !accessor, errs);
    }
    // tsc's checkGrammarProperty: `[K in T]: X` is a mapped type's member written in a class, reported at the class's
    // first member (a property, method or constructor: at its name)
    if (field && key->tag == EXPR_BINARY_OP && key->binary_op->op == BINARY_OP_IN && !expr_is_perenthisized(key) &&
        vec_class_item_size(&my items)
    ) {
        ClassItem const* first = vec_class_item_at(&my items, 0);
        Span span = first->tag == CLASS_ITEM_FIELD ? first->field.name_span :
            first->tag == CLASS_ITEM_METHOD ? first->method.name_span :
            first->tag == CLASS_ITEM_CONSTRUCTOR ? first->constructor.name_span : span_null;
        if (span_valid(span)) {
            error_ctx_grammar_error_d(errs, span, 7061, "A mapped type may not declare properties or methods.");
            return;
        }
    }
    if (!field && item->method.func.has_body) return; // Only overload signatures are restricted
    if (modifier_error || !expr_is_non_bindable_dynamic_name(key)) return;
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
        return ty_private_name(checker, my symbol, span_sv(class_item_private_name(item)));
    }
    bool is_field = item->tag == CLASS_ITEM_FIELD;
    return ty_member_name(checker, is_field ? &item->field.name_string : &item->method.name_string,
        is_field ? item->field.computed_key : item->method.computed_key);
}

// item_member_name(checker, me, item) == name, without atomizing a plain name (the override checks ask this of every
// member of a class for every property of its bases)
static bool item_has_member_name(Checker* checker, ClassDecl const* me, ClassItem const* item, Atom const* name) {
    bool is_field = item->tag == CLASS_ITEM_FIELD;
    if (!span_valid(class_item_private_name(item)) && !(is_field ? item->field.computed_key : item->method.computed_key)) {
        return csview_equals_sv(name_cooked(cstr_sv(is_field ? &item->field.name_string : &item->method.name_string)), atom_sv(name));
    }
    return item_member_name(checker, me, item) == name;
}

// tsc's containsUndefinedType
static bool contains_undefined(Ty const* ty) {
    if (ty->flags & TYF_UNDEFINED) return true;
    if (ty->kind != TY_UNION) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (ty->composite.types.data[i]->flags & TYF_UNDEFINED) return true;
    }
    return false;
}

// tsc's isPropertyInitializedInConstructor: `this.x` isn't possibly undefined at the end of the constructor, when it
// starts out undefined there. The reference is the constructor's first `this.x = v` (field.ts_assigned): without one
// it's undefined wherever the end can be reached
static bool field_initialized_in_constructor(
    Checker* checker, ClassDecl const* me, ClassItem const* field, Ty const* type, ClassItem const* constructor
) {
    FlowNode const* end = function_impl_rare(&constructor->constructor.func)->return_flow;
    if (!end || flow_is_unreachable(end)) return true;
    Expr const* reference = field->field.ts_assigned;
    if (!reference && my computed_this_assignments.size) {
        // `this[Flags.X] = v` (tsc's flow reference is `this.x`, which such an access matches by its key's literal)
        Atom const* name = item_member_name(checker, me, field);
        for_each (i, vec_expr_cptr, &my computed_this_assignments) {
            bool is_number = false;
            Ty const* key = ty_expr_type(checker, &(*i)->property->index);
            if (name && ty_property_name_of_key(checker, key, &is_number) == name) {
                reference = *i;
                break;
            }
        }
    }
    if (!reference) return false;
    Ty const* parts[2] = { type, ty_intrinsic(checker, TY_UNDEFINED) };
    Ty const* initial = ty_union(checker, parts, 2, nullptr, (TyList){0});
    return !contains_undefined(ty_flow_type(checker, reference, type, initial, end));
}

static ClassItem const* class_constructor_with_body(ClassDecl const* me) {
    for_each (i, vec_class_item, &my items) {
        if (i->tag == CLASS_ITEM_CONSTRUCTOR && i->constructor.func.has_body) return i;
    }
    return nullptr;
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
    bool late_bound; // `[k]: T` named by an entity name: tsc's late-bound (transient) member symbol
    Span name;
    ClassItem const* field; // The property declaration
} MemberInfo;

static MemberInfo class_instance_member(Checker* checker, ClassDecl const* me, Atom const* name) {
    MemberInfo ret = {0};
    for_each (i, vec_class_item, &my items) {
        if ((i->tag != CLASS_ITEM_FIELD && i->tag != CLASS_ITEM_METHOD) || class_item_is_static((ClassItem*)i)) continue;
        if (!item_has_member_name(checker, me, i, name)) continue;
        if (i->tag == CLASS_ITEM_FIELD) {
            ret.kind = i->field.is_auto_accessor ? MEMBER_ACCESSOR : MEMBER_PROPERTY;
            ret.is_abstract = i->field.is_abstract;
            ret.is_declare = i->field.is_declare;
            ret.uninitialized_field = !i->field.is_auto_accessor && i->field.value.tag == EXPR_IMPLICIT_ANY;
            ret.definite = i->field.is_definite;
            ret.identifier_name = !i->field.computed_key && !i->field.name_is_value;
            Expr const* key = i->field.computed_key;
            ret.late_bound = key && (key->tag == EXPR_IDENTIFIER || key->tag == EXPR_PROPERTY);
            ret.name = i->field.name_span;
            ret.field = i;
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
            if (checker_atom(checker, span_sv(arg->initializer.binding.content_span)) != name) continue;
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

// A base type of an interface merged with the class has a member of the name that isn't `prop` itself
static bool merged_interface_base_has(Checker* checker, ClassDecl const* me, Prop const* prop) {
    Symbol const* symbol = my symbol;
    for (u32 i = 0; symbol && i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* interface_decl = symbol->decls[i].node;
        for_each (extends, vec_spanned_type, &interface_decl->extends) {
            Ty const* base = ty_from_node(checker, &extends->type, (LowerCtx){ .generic = true });
            if (!(base->flags & (TYF_OBJECT | TYF_INTERSECTION))) continue;
            Prop const* other = ty_get_prop(checker, base, prop->name);
            if (other && other != prop) return true;
        }
    }
    return false;
}

// A class property's accessor flags (tsc's SymbolFlags.GetAccessor 1, SetAccessor 2), 0 if it isn't one
static u32 accessor_kinds(Prop const* prop) {
    if (!(prop->flags & PROP_ACCESSOR)) return 0;
    bool getter = prop->flags & PROP_CLASS_GETTER;
    return (getter ? 1 : 0) | (!getter || !(prop->flags & PROP_READONLY) ? 2 : 0);
}

// tsc's checkKindsOfPropertyMemberOverrides(): what an instance member of the base class is overridden with (a
// property with an accessor or the other way around, a method with an accessor...), and the abstract members a
// class that isn't abstract inherits without implementing
static void class_check_member_overrides(ClassDecl* me, Ty const* type, Ty const* base, ErrorCtx* errs) {
    Checker* checker = current_checker;
    CompilerOptions const* options = compiler_options();
    cstr missing = cstr_null; // The first four (all five when there are five), quoted
    u32 missing_count = 0;
    cstr first_missing = cstr_null;
    cstr five = cstr_null; // The fifth: listed when it is the last
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
            // (One that a later part has too is the intersection's synthetic property, which is a property unless every
            // part has the same kind of accessor: tsc's createUnionOrIntersectionProperty)
            // A synthetic one any declaration of which is an interface's or abstract isn't checked (tsc's
            // isPropertyAbstractOrInterface with `some`)
            bool synthetic = false, synthetic_exempt = false, several = false;
            for (u32 later = part + 1; later < parts.len; later++) {
                Ty const* later_type = parts.data[later];
                Prop const* other = (later_type->flags & TYF_OBJECT) ? ty_get_prop(checker, later_type, base_prop->name) : nullptr;
                if (!other) continue;
                several = true;
                synthetic |= accessor_kinds(other) != accessor_kinds(base_prop);
                ClassDecl const* other_class = class_of_prop(other);
                MemberInfo other_info = other_class ? class_instance_member(checker, other_class, other->name) : (MemberInfo){0};
                synthetic_exempt |= !other_class || other_info.is_abstract;
            }
            ClassDecl const* base_class = class_of_prop(base_prop);
            if (!base_class) continue; // (an interface merged into the class)
            MemberInfo base_info = class_instance_member(checker, base_class, base_prop->name);
            if (!base_info.kind) continue;
            Prop const* derived_prop = ty_get_prop(checker, type, base_prop->name);
            if (!derived_prop) continue;
            ClassDecl const* derived_class = class_of_prop(derived_prop);
            if (!derived_class) continue;
            if (derived_class == base_class) {
                // Inherited as it is: an abstract member has to be implemented, unless another base (an interface
                // merged with the class extends it) brings a member of the name (tsc's checkKindsOfPropertyMemberOverrides)
                // (Not when another part of an intersection base has it too: that synthetic property has no abstract
                // modifier, tsc's getDeclarationModifierFlagsFromSymbol keeps only accessibility and static)
                if (base_info.is_abstract && !several && !my is_abstract && !merged_interface_base_has(checker, me, base_prop)) {
                    // (Names as symbolToString prints them: `[Symbol.iterator]`, not the escaped `__@iterator@12`)
                    if (!missing_count) first_missing = ty_prop_name_cstr(checker, base_prop->name);
                    if (missing_count < 5) {
                        if (missing_count == 4) five = ty_prop_name_cstr(checker, base_prop->name);
                        else {
                            cstr name = ty_prop_name_cstr(checker, base_prop->name);
                            cstr_append_d(&missing, DEFAULT_DISPLAY, missing_count ? ", '" : "'", name, "'");
                            cstr_drop(&name);
                        }
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
                if (synthetic && (synthetic_exempt || base_info.is_abstract)) continue;
            }
            cstr name = ty_prop_name_cstr(checker, base_prop->name);
            if (base_property_like && derived_property_like) {
                if (synthetic) base_info.kind = MEMBER_PROPERTY;
                if (base_info.kind == MEMBER_ACCESSOR && derived_info.kind == MEMBER_PROPERTY) {
                    error_ctx_error_d(errs, at, 2610, "'", name, "' is defined as an accessor in class '",
                        base, "', but is overridden here in '", type, "' as an instance property.");
                } else if (base_info.kind == MEMBER_PROPERTY && derived_info.kind == MEMBER_ACCESSOR) {
                    error_ctx_error_d(errs, at, 2611, "'", name, "' is defined as a property in class '",
                        base, "', but is overridden here in '", type, "' as an accessor.");
                } else if (options->use_define_for_class_fields && derived_info.uninitialized_field && !base_info.is_abstract &&
                    !derived_info.is_abstract && !derived_info.is_declare && !my is_ambient && !derived_info.late_bound
                ) {
                    // (tsc's isPropertyInitializedInConstructor gets the class type for the property's here, which
                    // only differs when it's assigned undefined)
                    ClassItem const* constructor = class_constructor_with_body(me);
                    if (derived_info.definite || !constructor || !derived_info.identifier_name || !options->strict_null_checks ||
                        !field_initialized_in_constructor(checker, me, derived_info.field, type, constructor)
                    ) {
                        error_ctx_error_d(errs, at, 2612, "Property '", name, "' will overwrite the base "
                            "property in '", base, "'. If this is intentional, add an initializer. Otherwise, add a 'declare' "
                            "modifier or remove the redundant declaration.");
                    }
                }
            } else if (base_info.kind == MEMBER_METHOD) {
                if (derived_info.kind == MEMBER_ACCESSOR) {
                    error_ctx_error_d(errs, at, 2423, "Class '", base, "' defines instance member function '",
                        name, "', but extended class '", type, "' defines it as instance member accessor.");
                }
            } else if (base_info.kind == MEMBER_ACCESSOR) {
                error_ctx_error_d(errs, at, 2426, "Class '", base, "' defines instance member accessor '",
                    name, "', but extended class '", type, "' defines it as instance member function.");
            } else {
                error_ctx_error_d(errs, at, 2425, "Class '", base, "' defines instance member property '",
                    name, "', but extended class '", type, "' defines it as instance member function.");
            }
            cstr_drop(&name);
        }
    }
    if (missing_count == 5) cstr_append_d(&missing, DEFAULT_DISPLAY, ", '", five, "'");
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
                    "member '", first_missing, "' from class '", base, "'.");
            } else {
                error_ctx_error_d(errs, at, 2656, "Non-abstract class expression is missing implementations for the "
                    "following members of '", base, "': ", missing, ".");
            }
        } else if (missing_count == 1) {
            error_ctx_error_d(errs, at, 2515, "Non-abstract class '", type, "' does not implement inherited abstract "
                "member ", first_missing, " from class '", base, "'.");
        } else {
            error_ctx_error_d(errs, at, 2654, "Non-abstract class '", type, "' is missing implementations for the "
                "following members of '", base, "': ", missing, ".");
        }
    }
    cstr_drop(&missing);
}

// The property of a member in the class or its base: an intersection's (a mixin's) comes from its parts
static Prop const* override_find_prop(Checker* checker, Ty const* type, Atom const* name) {
    if (!type || !name) return nullptr;
    return ty_property_of_type(checker, ty_apparent_type(checker, type), name);
}

// tsc's getSuggestedSymbolForNonexistentClassMember: a close name among the base's properties
static Atom const* override_suggestion(Checker* checker, Ty const* base, Atom const* name) {
    base = ty_apparent_type(checker, base);
    TyList parts = base->kind == TY_INTERSECTION ? base->composite.types : (TyList){ .data = &base, .len = 1 };
    u32 count = 0;
    for (u32 i = 0; i < parts.len; i++) count += ty_members(checker, ty_apparent_type(checker, parts.data[i]))->prop_count;
    if (!count) return nullptr;
    csview* names = malloc(sizeof(csview) * count);
    Atom const** atoms = malloc(sizeof(Atom const*) * count);
    panic_if(!names || !atoms, "out of memory");
    u32 n = 0;
    for (u32 i = 0; i < parts.len; i++) {
        Members const* members = ty_members(checker, ty_apparent_type(checker, parts.data[i]));
        for (u32 j = 0; j < members->prop_count; j++) {
            atoms[n] = members->props[j]->name;
            names[n++] = atom_sv(members->props[j]->name);
        }
    }
    isize best = spelling_suggestion(atom_sv(name), names, n);
    Atom const* ret = best >= 0 ? atoms[best] : nullptr;
    free(names);
    free(atoms);
    return ret;
}

// Whether a declaration of the base's member is abstract
static bool override_base_abstract(Checker* checker, Prop const* base_prop) {
    if (!base_prop || (base_prop->flags & PROP_STATIC)) return false;
    ClassDecl const* base_class = class_of_prop(base_prop);
    return base_class && class_instance_member(checker, base_class, base_prop->name).is_abstract;
}

typedef struct OverrideCtx {
    ClassDecl const* class_decl;
    Ty const* type; // The class's instance type
    Ty const* static_type;
    Ty const* base; // Its base's instance type (tsc's baseWithThis), null if it extends nothing, `any` for `extends any`
    Ty const* base_static;
    bool base_any; // It extends `any`: its base has no properties
    bool is_js;
} OverrideCtx;

// tsc's checkMemberForOverrideModifier for one member (a parameter property when `parameter`)
static void check_member_override(OverrideCtx const* ctx, Atom const* name, bool dynamic_name, bool has_override,
    bool is_abstract, bool is_static, bool parameter, Span at, ErrorCtx* errs
) {
    Checker* checker = current_checker;
    bool js = ctx->is_js;
    if (has_override && dynamic_name) {
        if (js) {
            error_ctx_error_d(errs, at, 4128, "This member cannot have a JSDoc comment with an 'override' tag because its name is dynamic.");
        } else {
            error_ctx_error_d(errs, at, 4127, "This member cannot have an 'override' modifier because its name is dynamic.");
        }
        return;
    }
    if (!name) return;
    bool no_implicit = compiler_options()->no_implicit_override;
    if (ctx->base) {
        if (!has_override && !no_implicit) return;
        Prop const* prop = override_find_prop(checker, is_static ? ctx->static_type : ctx->type, name);
        Ty const* base_type = is_static ? ctx->base_static : ctx->base;
        Prop const* base_prop = ctx->base_any && (!is_static || ctx->base_static == ctx->base) ? nullptr :
            override_find_prop(checker, base_type, name);
        if (prop && !base_prop && has_override) {
            Atom const* suggestion = ctx->base_any ? nullptr : override_suggestion(checker, base_type, name);
            if (suggestion && js) {
                error_ctx_error_d(errs, at, 4123, "This member cannot have a JSDoc comment with an 'override' tag because it is "
                    "not declared in the base class '", ctx->base, "'. Did you mean '", atom_sv(suggestion), "'?");
            } else if (suggestion) {
                error_ctx_error_d(errs, at, 4117, "This member cannot have an 'override' modifier because it is not declared in "
                    "the base class '", ctx->base, "'. Did you mean '", atom_sv(suggestion), "'?");
            } else if (js) {
                error_ctx_error_d(errs, at, 4122, "This member cannot have a JSDoc comment with an '@override' tag because it is "
                    "not declared in the base class '", ctx->base, "'.");
            } else {
                error_ctx_error_d(errs, at, 4113, "This member cannot have an 'override' modifier because it is not declared in "
                    "the base class '", ctx->base, "'.");
            }
        } else if (prop && base_prop && no_implicit && !ctx->class_decl->is_ambient && !has_override) {
            bool base_abstract = override_base_abstract(checker, base_prop);
            if (!base_abstract) {
                if (parameter && js) {
                    error_ctx_error_d(errs, at, 4120, "This parameter property must have a JSDoc comment with an '@override' tag "
                        "because it overrides a member in the base class '", ctx->base, "'.");
                } else if (parameter) {
                    error_ctx_error_d(errs, at, 4115, "This parameter property must have an 'override' modifier because it "
                        "overrides a member in base class '", ctx->base, "'.");
                } else if (js) {
                    error_ctx_error_d(errs, at, 4119, "This member must have a JSDoc comment with an '@override' tag because it "
                        "overrides a member in the base class '", ctx->base, "'.");
                } else {
                    error_ctx_error_d(errs, at, 4114, "This member must have an 'override' modifier because it overrides a member "
                        "in the base class '", ctx->base, "'.");
                }
            } else if (is_abstract) {
                error_ctx_error_d(errs, at, 4116, "This member must have an 'override' modifier because it overrides an abstract "
                    "method that is declared in the base class '", ctx->base, "'.");
            }
        }
    } else if (has_override) {
        if (js) {
            error_ctx_error_d(errs, at, 4121, "This member cannot have a JSDoc comment with an '@override' tag because its "
                "containing class '", ctx->type, "' does not extend another class.");
        } else {
            error_ctx_error_d(errs, at, 4112, "This member cannot have an 'override' modifier because its containing class '",
                ctx->type, "' does not extend another class.");
        }
    }
}

// tsc's checkMembersForOverrideModifier: `override` where a member overrides nothing (TS4112, TS4113, TS4117) and,
// under noImplicitOverride, where it's missing (TS4114-TS4116), with JSDoc `@override` in JavaScript (TS4119-TS4123)
static void class_check_override_modifiers(ClassDecl* me, Ty const* base, ErrorCtx* errs) {
    Checker* checker = current_checker;
    if (!my symbol) return;
    OverrideCtx ctx = {
        .class_decl = me,
        .type = ty_declared_type_of(checker, my symbol),
        .static_type = ty_static_type_of(checker, my symbol),
        .base = base,
        .is_js = span_source(my full_span) && span_source(my full_span)->is_js,
    };
    Expr const* extends = &my extends;
    if (extends->tag == EXPR_GENERIC_PARAMS) extends = &extends->generic_params->expr;
    if (extends->tag != EXPR_IMPLICIT_ANY) {
        Ty const* constructor = ty_expr_type(checker, extends);
        if (base) ctx.base_static = constructor;
        else if ((constructor->flags & TYF_ANY) && !(constructor->flags & TYF_ERROR)) {
            ctx.base_any = true;
            ctx.base = ctx.base_static = constructor;
        } else {
            // A constructor of `any` (`new (...args: any[]) => any`): the base type is any, which has no properties,
            // but the static side still inherits the constructor's, Function's among them (tsc's
            // checkMembersForOverrideModifier with baseTypes [any], getBaseConstructorTypeOfClass)
            Ty const* apparent = ty_apparent_type(checker, constructor);
            Members const* members = (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) ? ty_members(checker, apparent) : nullptr;
            Ty const* constructed = members && members->construct_count ? signature_return_type(checker, members->construct[0]) : nullptr;
            if (constructed && (constructed->flags & TYF_ANY) && !(constructed->flags & TYF_ERROR)) {
                ctx.base_any = true;
                ctx.base = constructed;
                ctx.base_static = constructor;
            }
        }
    }
    for_each (i, vec_class_item, &my items) {
        switch (i->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                for_each (arg, vec_function_arg, &i->constructor.func.args) {
                    if (!arg->is_parameter_property || arg->initializer.binding.tag != BINDING_IDENTIFIER) continue;
                    Atom const* name = checker_atom(checker, span_sv(arg->initializer.binding.content_span));
                    check_member_override(&ctx, name, false, arg->is_override_property, false, false, true, arg->span, errs);
                }
                break;
            case CLASS_ITEM_FIELD:
                if (i->field.is_declare) break;
                // fallthrough
            case CLASS_ITEM_METHOD: {
                bool is_field = i->tag == CLASS_ITEM_FIELD;
                Expr const* computed_key = is_field ? i->field.computed_key : i->method.computed_key;
                Atom const* name = item_member_name(checker, me, i);
                // A JavaScript method's `@overload`s are members of their own first, at their tag names, without the
                // method's `@override` (tsgo's reparsed signatures)
                FunctionImplRare const* rare = is_field ? nullptr : function_impl_rare(&i->method.func);
                for (u32 o = 0; rare && o < rare->jsdoc_overload_count; o++) {
                    Span tag = rare->jsdoc_overloads[o].callable->function->full_span;
                    if (!csview_starts_with(span_sv(tag), "@overload")) continue;
                    Span at = span_make_in(tag, tag.buf + 1, tag.buf + 9);
                    check_member_override(&ctx, name, computed_key && !name, false, i->method.is_abstract,
                        class_item_is_static(i), false, at, errs);
                }
                check_member_override(&ctx, name, computed_key && !name, i->is_override,
                    is_field ? i->field.is_abstract : i->method.is_abstract, class_item_is_static(i), false,
                    is_field ? i->field.name_span : i->method.name_span, errs);
            }   break;
            default: break;
        }
    }
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
    if (ty_sig_param_adds_undefined(&sig->params[0])) return false;
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
    // (Not a constructor type, TS2507: the base constructor is the error type, no mixin. tsc's
    // getBaseConstructorTypeOfClass, isConstructorType)
    Ty const* apparent = ty_apparent_type(checker, base_constructor);
    if (apparent->flags & TYF_ANY) return;
    if ((apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) && !ty_members(checker, apparent)->construct_count) return;
    // (Without a name, the first token: tsc's getErrorSpanForNode)
    Span span = span_valid(my name) ? my name : block_first_token(my full_span);
    // (Of the class's value type, `typeof C & T`: the variable's construct signatures count unless they're a mixin's,
    // tsc's getTypeOfSymbol, resolveIntersectionTypeMembers)
    if (!is_mixin_constructor_type(checker, ty_type_of_symbol(checker, my symbol))) {
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

// A class can't extend one whose constructor is private, outside of it (tsc's checkBaseTypeAccessibility)
static void class_check_base_accessibility(ClassDecl* me, ErrorCtx* errs) {
    Checker* checker = current_checker;
    Expr const* extends = &my extends;
    if (extends->tag == EXPR_GENERIC_PARAMS) extends = &extends->generic_params->expr;
    if (extends->tag == EXPR_IMPLICIT_ANY || !my symbol) return;
    Members const* members = ty_members(checker, ty_apparent_type(checker, ty_expr_type(checker, extends)));
    // (Every construct signature, the first private one outside its class reported: tsgo 7.1's
    // getConstructorAccessibilityError, `typeof A & typeof B`)
    for (u32 i = 0; i < members->construct_count; i++) {
        Signature const* sig = members->construct[i];
        while (sig && !sig->function && sig->target) sig = sig->target;
        FunctionImpl const* function = sig ? sig->function : nullptr;
        if (!function || !function_impl_rare(function)->constructed_class || function_impl_rare(function)->constructor_access != ACCESS_MODIFIER_PRIVATE) continue;
        ClassDecl const* base = symbol_find_node(function_impl_rare(function)->constructed_class, SYMBOL_DECL_CLASS);
        if (base && span_valid(base->full_span) && span_source(base->full_span) == span_source(extends->inner_span) &&
            span_start_ptr(base->full_span) <= span_start_ptr(extends->inner_span) &&
            span_end_ptr(extends->inner_span) <= span_end_ptr(base->full_span)
        ) {
            continue;
        }
        cstr name = symbol_fully_qualified_name(function_impl_rare(function)->constructed_class);
        error_ctx_error_d(errs, my extends.inner_span, 2675, "Cannot extend a class '", name,
            "'. Class constructor is marked as private.");
        cstr_drop(&name);
        return;
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
    // Its members asked for while its bases were resolved (tsc's getBaseTypes, reportCircularBaseType)
    if (span_valid(my name) && ty_bases_reentered(checker, my symbol)) {
        Ty const* shown = declared->declared.type_params.len ? ty_reference(checker, declared, declared->declared.type_params) : declared;
        error_ctx_error_d(errs, my name, 2310, "Type '", shown, "' recursively references itself as a base type.");
    }
    Ty const* base = my extends.tag != EXPR_IMPLICIT_ANY ? ty_class_base_instance(checker, me) : nullptr;
    // A class that extends itself through its bases (tsc's circularity in getBaseConstructorTypeOfClass), and then
    // nothing is compared
    if (my extends.tag != EXPR_IMPLICIT_ANY && ty_class_base_constructor_circular(checker, me)) {
        error_ctx_error_d(errs, span, 2506, "'", span, "' is referenced directly or indirectly in its own base expression.");
        base = nullptr;
    } else if (base && ty_has_base_type(checker, base, declared)) {
        error_ctx_error_d(errs, span, 2506, "'", span, "' is referenced directly or indirectly in its own base expression.");
        base = nullptr;
    }
    // `extends null` with an interface merged into the class that extends something: that is its base type (tsc's
    // getBaseTypes of the class type), and the static side is compared with null's
    bool null_base = false;
    if (!base && my extends.tag != EXPR_IMPLICIT_ANY && (ty_expr_type(checker, &my extends)->flags & TYF_NULL)) {
        base = class_decl_merged_interface_base(me);
        null_base = base != nullptr;
    }
    if (!base && my extends.tag != EXPR_IMPLICIT_ANY) {
        Ty const* never_base = ty_class_never_base(checker, me);
        unsigned code;
        cstr why;
        if (never_base && ty_never_intersection_elaboration(checker, never_base, &code, &why)) {
            Expr const* extends = my extends.tag == EXPR_GENERIC_PARAMS ? &my extends.generic_params->expr : &my extends;
            Error* error = error_ctx_error_d(errs, expr_outer_span(extends), 2509, "Base constructor return type 'never' is not an "
                "object type or intersection of object types with statically known members.");
            if (error) error_sub_error_s(error, (Span){0}, code, why); else cstr_drop(&why);
        }
    }
    class_check_mixin(me, errs);
    class_check_base_accessibility(me, errs);
    class_check_override_modifiers(me, base && (base->flags & (TYF_OBJECT | TYF_INTERSECTION)) ? base : nullptr, errs);
    if (base && (base->flags & (TYF_OBJECT | TYF_INTERSECTION)) && ty_class_base_constructors_differ(checker, me, base)) {
        Expr const* extends = my extends.tag == EXPR_GENERIC_PARAMS ? &my extends.generic_params->expr : &my extends;
        error_ctx_error_d(errs, expr_outer_span(extends), 2510, "Base constructors must all have the same return type.");
    }
    if (base && (base->flags & (TYF_OBJECT | TYF_INTERSECTION))) {
        class_check_member_overrides(me, declared, base, errs);
        bool fits = ty_check_heritage(checker, type_with_this, ty_with_this_arg(checker, base, this_param), members, count,
            2415, "Class", "incorrectly extends base class", span, errs);
        // The static side only when the instance side fits
        Ty const* base_static = ty_apparent_type(checker, ty_expr_type(checker, &my extends));
        if (fits && null_base) {
            Ty const* static_type = ty_type_of_symbol(checker, my symbol);
            cstr derived_str = ty_to_cstr(checker, static_type);
            ty_report_unassignable_with_head(checker, static_type, base_static, 2417, cstr_d("Class static side '", derived_str,
                "' incorrectly extends base class static side 'null'."), span, errs);
            cstr_drop(&derived_str);
        } else if (fits && !(base_static->flags & (TYF_ANY | TYF_NULLABLE))) {
            // (The value of the class, with what merges into it: a namespace's exports are static members too)
            ty_check_static_heritage(checker, ty_type_of_symbol(checker, my symbol), base_static, span, errs);
        }
    }
    for_each (i, vec_spanned_type, &my implements) {
        Ty const* implemented = ty_of_node(&i->type);
        if (i->type.tag == TYPE_IDENTIFIER && !ty_is_valid_base_type(checker, implemented)) { // (a keyword is TS2864)
            error_ctx_error_d(errs, i->span, 2422, "A class can only implement an object type or intersection of object "
                "types with statically known members.");
            continue;
        }
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

// The name a method's computed key binds, null if it's dynamic (tsc's hasBindableName: a literal, or an entity name
// whose type is a literal or unique symbol, which is late bound)
static Atom const* bindable_computed_name(ClassItem const* item) {
    Expr const* key = item->method.computed_key;
    if (expr_is_perenthisized(key)) return nullptr;
    bool signed_number = key->tag == EXPR_UNARY_OP &&
        (key->unary_op->op == UNARY_OP_MINUS || key->unary_op->op == UNARY_OP_PLUS) &&
        key->unary_op->expr.tag == EXPR_LITERAL && !expr_is_perenthisized(&key->unary_op->expr);
    if (key->tag != EXPR_LITERAL && !signed_number && !expr_is_entity_name(key)) return nullptr;
    return ty_member_name(current_checker, &item->method.name_string, key);
}

// Whether a method's name is known (not a dynamic computed name)
static bool bindable_method(ClassItem const* item) {
    return item->tag != CLASS_ITEM_METHOD || !item->method.computed_key || bindable_computed_name(item);
}

static bool same_member(ClassItem const* a, ClassItem const* b) {
    if (a->tag != b->tag) return false;
    if (a->tag == CLASS_ITEM_CONSTRUCTOR) return true;
    if (span_valid(class_item_private_name(a)) != span_valid(class_item_private_name(b))) return false;
    if (!a->method.computed_key && !b->method.computed_key) return cstr_eq(&a->method.name_string, &b->method.name_string);
    Atom const* a_name = a->method.computed_key ? bindable_computed_name(a) :
        checker_atom(current_checker, cstr_sv(&a->method.name_string));
    Atom const* b_name = b->method.computed_key ? bindable_computed_name(b) :
        checker_atom(current_checker, cstr_sv(&b->method.name_string));
    return a_name && a_name == b_name;
}

// tsc's checkConstructorDeclarationDiagnostics: without standard class fields, a derived class with initialized
// instance properties, parameter properties or private names runs their initialization right after `super(...)`,
// which then has to be a statement of the constructor's body
static bool class_super_call_should_be_root(ClassDecl const* me, FunctionImpl const* constructor) {
    CompilerOptions const* options = compiler_options();
    if (options->use_define_for_class_fields && options->target >= SCRIPT_TARGET_ES2022) return false;
    for_each (i, vec_class_item, &my items) {
        if (span_valid(class_item_private_name(i))) return true;
        if (i->tag == CLASS_ITEM_FIELD && !i->field.is_static && i->field.value.tag != EXPR_IMPLICIT_ANY) return true;
    }
    for_each (arg, vec_function_arg, &constructor->args) {
        if (arg->is_parameter_property) return true;
    }
    return false;
}

// TS2401 when the call isn't a statement of the body, else TS2376 when a statement before it refers to `this` or
// `super` (tsc's nodeImmediatelyReferencesSuperOrThis: not in a nested function or arrow function)
static void check_super_call_root(Span name, FunctionImpl const* func, Expr const* super_call, ErrorCtx* errs) {
    Statement const* statement = nullptr;
    for_each (i, vec_statement, &func->body.statements) {
        if (i->tag == STATEMENT_EXPR && &i->expr == super_call) { // (Parentheses are the same Expr)
            statement = i;
            break;
        }
    }
    if (!statement) {
        error_ctx_error_d(errs, expr_outer_span(super_call), 2401, "A 'super' call must be a root-level statement within a ",
            "constructor of a derived class that contains initialized properties, parameter properties, or private ",
            "identifiers.");
        return;
    }
    Span first = function_impl_rare(func)->first_this_or_super;
    Span body = block_span(&func->body);
    if (span_valid(first) && span_valid(body) && span_start_ptr(first) >= span_start_ptr(body) &&
        span_start_ptr(first) < span_start_ptr(statement->span)
    ) {
        // (At the `constructor` keyword: the name span runs to the parameters)
        while (name.size && (name.buf[name.size - 1] == ' ' || name.buf[name.size - 1] == '\t')) name.size--;
        error_ctx_error_d(errs, name, 2376, "A 'super' call must be the first statement in the ",
            "constructor to refer to 'super' or 'this' when a derived class contains initialized properties, parameter ",
            "properties, or private identifiers.");
    }
}

static Span overload_name(ClassItem const* item) {
    return item->tag == CLASS_ITEM_CONSTRUCTOR ? item->constructor.name_span : item->method.name_span;
}

// tsc's checkFlagAgreementBetweenOverloads and checkQuestionTokenAgreementBetweenOverloads for the methods of a class
// (the declarations of one method symbol, with an overload among them): each declaration that differs from the
// canonical one (the implementation, else the first) in accessibility (TS2385), else abstractness (TS2512), and in
// optionality (TS2386)
static void class_check_overload_agreement(ClassDecl const* me, ErrorCtx* errs) {
    isize count = vec_class_item_size(&my items);
    // The constructors, in accessibility
    {
        ClassItem const* first = nullptr;
        ClassItem const* implementation = nullptr;
        bool overloads = false, differ = false;
        for (isize j = 0; j < count; j++) {
            ClassItem const* other = vec_class_item_at(&my items, j);
            if (other->tag != CLASS_ITEM_CONSTRUCTOR) continue;
            if (!first) first = other;
            differ |= other->access != first->access;
            if (other->constructor.func.has_body) {
                if (!implementation) implementation = other;
            } else {
                overloads = true;
            }
        }
        ClassItem const* canonical = implementation ? implementation : first;
        for (isize j = 0; overloads && differ && j < count; j++) {
            ClassItem const* other = vec_class_item_at(&my items, j);
            if (other->tag != CLASS_ITEM_CONSTRUCTOR || other->access == canonical->access) continue;
            error_ctx_error_d(errs, other->constructor.name_span, 2385, "Overload signatures must all be public, private or protected.");
        }
    }
    for (isize i = 0; i < count; i++) {
        ClassItem const* item = vec_class_item_at(&my items, i);
        if (item->tag != CLASS_ITEM_METHOD || item->method.accessor || !bindable_method(item)) continue;
        bool first = true, overloads = false;
        ClassItem const* implementation = nullptr;
        u32 some_flags = 0, all_flags = ~0u;
        bool some_optional = false, all_optional = true;
        for (ClassNamesakes it = class_namesakes(me, item, true); class_namesakes_next(&it);) {
            ClassItem const* other = it.item;
            if (other->tag != CLASS_ITEM_METHOD || other->method.accessor || !same_member(item, other) ||
                other->method.is_static != item->method.is_static
            ) {
                continue;
            }
            if (it.index < i) first = false;
            u32 flags = (u32)other->access << 1 | other->method.is_abstract;
            some_flags |= flags;
            all_flags &= flags;
            some_optional |= other->method.is_optional;
            all_optional &= other->method.is_optional;
            if (other->method.func.has_body) {
                if (!implementation) implementation = other;
            } else {
                overloads = true;
            }
        }
        if (!first || !overloads) continue;
        ClassItem const* canonical = implementation ? implementation : item;
        for (ClassNamesakes it = class_namesakes(me, item, true); class_namesakes_next(&it);) {
            ClassItem const* other = it.item;
            if (other->tag != CLASS_ITEM_METHOD || other->method.accessor || !same_member(item, other) ||
                other->method.is_static != item->method.is_static
            ) {
                continue;
            }
            if (some_flags != all_flags) {
                if (other->access != canonical->access) {
                    error_ctx_error_d(errs, other->method.name_span, 2385, "Overload signatures must all be public, private or protected.");
                } else if (other->method.is_abstract != canonical->method.is_abstract) {
                    error_ctx_error_d(errs, other->method.name_span, 2512, "Overload signatures must all be abstract or non-abstract.");
                }
            }
            if (some_optional != all_optional && other->method.is_optional != canonical->method.is_optional) {
                error_ctx_error_d(errs, other->method.name_span, 2386, "Overload signatures must all be optional or required.");
            }
        }
    }
}

// Whether `next` directly follows `previous` (tsc's `previous.end === next.pos`: only trivia between, and the `;` that
// ends a declaration without a body)
static bool overloads_adjacent(ClassItem const* previous, ClassItem const* next) {
    FunctionImpl const* func = overloadable(previous);
    return span_start_ptr(next->span) >= span_end_ptr(previous->span) &&
        span_only_trivia_between(span_end_ptr(previous->span), span_start_ptr(next->span), !func->has_body && !func->body_missing);
}

// Whether two methods are written with the same name, as tsc's reportImplementationExpectedError compares them: both
// private names of one text, both computed names of identical types, or both written (an identifier or a literal) of
// one text
static bool overload_names_match(ClassItem const* a, ClassItem const* b) {
    bool a_private = span_valid(class_item_private_name(a)), b_private = span_valid(class_item_private_name(b));
    if (a_private || b_private) return a_private && b_private && cstr_eq(&a->method.name_string, &b->method.name_string);
    Expr const* a_key = a->method.computed_key;
    Expr const* b_key = b->method.computed_key;
    if (a_key && b_key) {
        Checker* checker = current_checker;
        return ty_is_related(checker, ty_regular(checker, ty_expr_type(checker, a_key)),
            ty_regular(checker, ty_expr_type(checker, b_key)), TY_RELATION_IDENTITY);
    }
    return !a_key && !b_key && cstr_eq(&a->method.name_string, &b->method.name_string);
}

// tsc's reportImplementationExpectedError: the declaration right after `item` is its implementation of another name
// (TS2389), of its name but the other staticness (TS2387/TS2388; else nothing: the binder reported it), or there is
// none (TS2390, TS2516, TS2391)
static void report_implementation_expected(ClassDecl const* me, ClassItem const* item, ErrorCtx* errs) {
    bool is_constructor = item->tag == CLASS_ITEM_CONSTRUCTOR;
    // (A missing name: tsc's reportImplementationExpectedError says nothing)
    if (!is_constructor && span_valid(item->method.name_span) && !item->method.name_span.size) return;
    ClassItem const* next = item + 1 < my items.data + my items.size ? item + 1 : nullptr;
    if (next && overloadable(next) && next->tag == item->tag && overloads_adjacent(item, next)) {
        if (!is_constructor && overload_names_match(item, next)) {
            if (item->method.is_static != next->method.is_static) {
                error_ctx_error_d(errs, next->method.name_span, item->method.is_static ? 2387 : 2388,
                    item->method.is_static ? "Function overload must be static." : "Function overload must not be static.");
            }
            return;
        }
        if (overloadable(next)->has_body) {
            cstr name = cstr_from_sv(span_sv(overload_name(item)));
            error_ctx_error_d(errs, overload_name(next), 2389, "Function implementation name must be '", name, "'.");
            cstr_drop(&name);
            return;
        }
    }
    if (is_constructor) {
        error_ctx_error_d(errs, overload_name(item), 2390, "Constructor implementation is missing.");
    } else if (item->method.is_abstract) {
        error_ctx_error_d(errs, overload_name(item), 2516, "All declarations of an abstract method must be consecutive.");
    } else {
        error_ctx_error_d(errs, overload_name(item), 2391, "Function implementation is missing or not immediately following the declaration.");
    }
}

// The first early-bound member of the name a late-bound method binds (the early symbol of that name, which its late
// symbol merges into), null if none
static ClassItem const* early_namesake_of_late(ClassDecl const* me, ClassItem const* item) {
    Atom const* name = bindable_computed_name(item);
    if (!name) return nullptr;
    for (ClassNamesakes it = class_namesakes(me, item, true); class_namesakes_next(&it);) {
        ClassItem const* other = it.item;
        if ((other->tag != CLASS_ITEM_METHOD && other->tag != CLASS_ITEM_FIELD) || item_late_key(other) || !other->symbol_first ||
            class_item_is_static((ClassItem*)other) != item->method.is_static || span_valid(class_item_private_name(other))
        ) {
            continue;
        }
        cstr const* other_name = other->tag == CLASS_ITEM_METHOD ? &other->method.name_string : &other->field.name_string;
        if (cstr_equals_sv(other_name, atom_sv(name))) return other->symbol_first;
    }
    return nullptr;
}

// The declarations of the method symbol (or the constructor) `item` leads, in tsc's order, into `out`: 0 unless `item`
// leads one. An early-bound method's symbol is the binder's (ClassItem.symbol_first), then the late-bound methods that
// merge into it (when it is a method's: mergeSymbol); a late-bound one not merged leads its own (lateBindMember's,
// see computed_symbol_first())
// (`early_of`: per item, the early method a late-bound one merges into; null when none does)
static u32 overload_symbol_declarations(ClassDecl const* me, ClassItem const* item, ClassItem const* const* early_of, ClassItem const** out, u32 cap) {
    if (!overloadable(item)) return 0;
    u32 len = 0;
    if (item->tag == CLASS_ITEM_CONSTRUCTOR) {
        for_each (other, vec_class_item, &my items) {
            if (other->tag != CLASS_ITEM_CONSTRUCTOR) continue;
            if (!len && other != item) return 0; // (The first constructor leads)
            if (len < cap) out[len++] = other;
        }
        return len;
    }
    if (!bindable_method(item)) return 0;
    if (item_late_key(item)) {
        if (early_of && early_of[item - my items.data]) return 0; // (Merged: the early one leads)
        if (computed_symbol_first(me, item) != item) return 0;
        for (ClassNamesakes it = class_namesakes(me, item, true); class_namesakes_next(&it);) {
            ClassItem const* other = it.item;
            if (other->tag == CLASS_ITEM_METHOD && !other->method.accessor && item_late_key(other) &&
                other->method.is_static == item->method.is_static && cstr_eq(&other->method.name_string, &item->method.name_string) &&
                computed_symbol_first(me, other) == item && len < cap
            ) {
                out[len++] = other;
            }
        }
        return len;
    }
    if (item->symbol_first != item) return 0;
    for (ClassNamesakes it = class_namesakes(me, item, false); class_namesakes_next(&it);) {
        ClassItem const* other = it.item;
        if (overloadable(other) && other->symbol_first == item && len < cap) out[len++] = other;
    }
    for (ClassNamesakes it = class_namesakes(me, item, true); early_of && class_namesakes_next(&it);) {
        ClassItem const* other = it.item;
        if (early_of[other - my items.data] == item && len < cap) out[len++] = other;
    }
    return len;
}

// tsc's checkFunctionOrConstructorSymbol for the methods and constructors of a class: overloads are consecutive and
// followed by the implementation (TS2391, TS2389, TS2390, TS2516, TS2387/TS2388), one implementation (TS2393)
static void class_check_overloads(ClassDecl* me, ErrorCtx* errs) {
    SourceFile const* file = span_source(my name) ? span_source(my name) : span_source(my full_span);
    if ((my symbol && my symbol->is_ambient) || !file || file->is_declaration) return;
    isize count = vec_class_item_size(&my items);
    // Each method symbol's declarations (and the constructor's), in tsc's order: the early ones, then the late-bound
    // ones merged into them (see overload_symbol_declarations())
    ClassItem const** decls = malloc(sizeof(ClassItem const*) * (size_t)(count ? count : 1));
    ClassItem const** early_of = calloc((size_t)(count ? count : 1), sizeof(ClassItem const*));
    panic_if(!decls || !early_of, "out of memory");
    bool merged = false;
    for (isize i = 0; i < count; i++) {
        ClassItem const* item = vec_class_item_at(&my items, i);
        if (overloadable(item) && item->tag == CLASS_ITEM_METHOD && item_late_key(item) && bindable_method(item)) {
            ClassItem const* early = early_namesake_of_late(me, item);
            if (early && early->tag == CLASS_ITEM_METHOD && !early->method.accessor) early_of[i] = early;
            merged |= early_of[i] != nullptr;
        }
    }
    if (!merged) {
        free(early_of);
        early_of = nullptr;
    }
    for (isize i = 0; i < count; i++) {
        ClassItem const* item = vec_class_item_at(&my items, i);
        u32 decl_count = overload_symbol_declarations(me, item, early_of, decls, (u32)count);
        if (!decl_count) continue;
        // tsc's walk of the declarations: one not right after the previous one (and not a second implementation)
        // leaves the previous one without its implementation
        ClassItem const* previous = nullptr;
        ClassItem const* last = nullptr;
        bool body_seen = false;
        for (u32 d = 0; d < decl_count; d++) {
            ClassItem const* node = decls[d];
            FunctionImpl const* func = overloadable(node);
            // (`declare m()`: ambient, tsc's NodeFlags.Ambient, interleaving is no error)
            bool ambient = node->tag == CLASS_ITEM_METHOD && node->method.is_declare;
            if (ambient) previous = nullptr;
            if (!(func->has_body && body_seen) && previous && !overloads_adjacent(previous, node)) {
                report_implementation_expected(me, previous, errs);
            }
            body_seen |= func->has_body;
            previous = node;
            if (!ambient) last = node;
        }
        // The last one without a body is only an error for what needs an implementation (a body missing its `{` is
        // one here: tsc's `!body`)
        if (last && !overloadable(last)->has_body && !overloadable(last)->body_missing &&
            !(last->tag == CLASS_ITEM_METHOD && (last->method.is_abstract || last->method.is_optional))
        ) {
            report_implementation_expected(me, last, errs);
        }
    }
    free(decls);
    free(early_of);
    // The overloads of each method (and of the constructor) fit its implementation (TS2394)
    for (isize i = 0; i < count; i++) {
        ClassItem const* item = vec_class_item_at(&my items, i);
        FunctionImpl const* func = overloadable(item);
        if (!func || !bindable_method(item)) continue;
        bool first = true;
        FunctionImpl const* implementation = nullptr;
        Span implementation_name = span_null;
        FunctionImpl const* overloads[32];
        Span names[32];
        u32 overload_count = 0;
        for (ClassNamesakes it = class_namesakes(me, item, true); class_namesakes_next(&it);) {
            ClassItem const* other = it.item;
            FunctionImpl const* other_func = overloadable(other);
            if (!other_func || !same_member(item, other)) continue;
            if (item->tag == CLASS_ITEM_METHOD && other->method.is_static != item->method.is_static) continue;
            if (it.index < i) first = false;
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
        } else if (first && implementation && item->tag == CLASS_ITEM_CONSTRUCTOR) {
            function_impl_check_jsdoc_overloads(implementation, implementation_name, errs); // (a JavaScript constructor's)
        }
    }
    // Two implementations of a method (constructors have TS2392)
    for (isize i = 0; i < count; i++) {
        ClassItem const* item = vec_class_item_at(&my items, i);
        FunctionImpl const* func = overloadable(item);
        if (!func || item->tag == CLASS_ITEM_CONSTRUCTOR || !bindable_method(item)) continue;
        bool first = true, is_static = item->method.is_static;
        u32 bodies = 0;
        for (ClassNamesakes it = class_namesakes(me, item, true); class_namesakes_next(&it);) {
            ClassItem const* other = it.item;
            if (!overloadable(other) || !same_member(item, other) || other->method.is_static != is_static) continue;
            if (it.index < i) first = false;
            if (other->method.func.has_body) bodies++;
        }
        if (!first || bodies < 2) continue;
        for (ClassNamesakes it = class_namesakes(me, item, true); class_namesakes_next(&it);) {
            ClassItem const* other = it.item;
            if (!overloadable(other) || !same_member(item, other) || other->method.is_static != is_static) continue;
            error_ctx_error_d(errs, other->method.name_span, 2393, "Duplicate function implementation.");
        }
    }
}

// A decorator, from its `@`
static Span decorator_span(Expr const* decorator) {
    Span ret = expr_outer_span(decorator);
    const char* start = span_start_ptr(ret);
    const char* file_start = source_file_start(span_source(ret));
    while (start > file_start && start[-1] != '@') start--;
    if (start > file_start) start--;
    span_set_sv(&ret, csview_from_n(start, span_end_ptr(ret) - start));
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
            if (!vec_expr_size(function_arg_decorators(arg))) continue;
            Span at = decorator_span(vec_expr_at(function_arg_decorators(arg), 0));
            ty_check_emit_helpers(current_checker, at, EMIT_HELPER_DECORATE | EMIT_HELPER_PARAM, errs);
            if (options->emit_decorator_metadata) ty_check_emit_helpers(current_checker, at, EMIT_HELPER_METADATA, errs);
        }
    }
}

// tsc's isPrivateWithinAmbient: private members of an ambient class have no implicit any errors
bool class_item_private_in_ambient(ClassDecl const* me, ClassItem const* item) {
    return my is_ambient && (item->access == ACCESS_MODIFIER_PRIVATE || span_valid(class_item_private_name(item)));
}

// tsc's reportImplicitAny of a property declaration: no type and no initializer, or (without strictNullChecks) an
// initializer that widens to any (`x = null`)
static void class_field_check_implicit_any(ClassDecl const* me, ClassItem const* item, ErrorCtx* errs) {
    Span name = item->field.name_span;
    // An initializer that needs the field's own type (tsc's reportCircularityError)
    if (span_valid(name) && !item->field.has_explicit_type && compiler_options()->no_implicit_any &&
        !(span_source(name) && span_source(name)->is_js) && item->field.value.tag != EXPR_IMPLICIT_ANY &&
        class_item_get_type((ClassItem*)item) == ty_intrinsic(current_checker, TY_CIRCULAR)
    ) {
        error_ctx_error_d(errs, name, 7022, "'", name, "' implicitly has type 'any' because it does not have a type "
            "annotation and is referenced directly or indirectly in its own initializer.");
        return;
    }
    if (item->field.has_explicit_type || !compiler_options()->no_implicit_any || !span_valid(name)) return;
    // (Typed by the accessors it joined: its own declaration's type is never asked for)
    if (item->field.joined_accessor) return;
    if (span_source(name) && span_source(name)->is_js && item->field.value.tag != EXPR_IMPLICIT_ANY && !item->field.js_assigned &&
        ty_is_empty_array_literal_type(current_checker, ty_expr_type(current_checker, &item->field.value))
    ) {
        error_ctx_error_d(errs, name, 7008, "Member '", name, "' implicitly has an 'any[]' type.");
        return;
    }
    // (In JavaScript a field without an initializer nothing assigns is any too; one `this.x = v` assigns has the
    // assignments' type)
    bool js = span_source(name) && span_source(name)->is_js;
    if ((js && (item->field.value.tag != EXPR_IMPLICIT_ANY || item->field.js_assigned)) || class_item_private_in_ambient(me, item)) return;
    if (item->field.value.tag != EXPR_IMPLICIT_ANY) {
        // tsc's reportErrorsFromWidening: inside the type if it can (object literal properties), else at the member
        // with the widened type (`x = null`: any, `x = []`: any[])
        if (compiler_options()->strict_null_checks) return;
        Ty const* value = ty_expr_type(current_checker, &item->field.value);
        if (!(value->xflags & TYX_CONTAINS_WIDENING) || binding_report_widening_errors_in_type(value, errs)) return;
        cstr type = ty_to_cstr(current_checker, ty_widened_nullable(current_checker, value));
        error_ctx_error_d(errs, name, 7008, "Member '", name, "' implicitly has an '", type, "' type.");
        cstr_drop(&type);
        return;
    }
    // (Without an initializer, what the constructor assigns is its type: see field_type_from_assignments())
    if (item->field.ts_assigned) return;
    // (`static prototype;` merges into the class's prototype property, whose type is the instance type: tsc's
    // getTypeOfPrototypeProperty. TS2699 says the rest)
    if (item->field.is_static && !item->field.computed_key && cstr_equals(&item->field.name_string, "prototype")) return;
    error_ctx_error_d(errs, name, 7008, "Member '", name, "' implicitly has an 'any' type.");
}

// tsc's resolveBaseTypesOfClass: a base constructor that is a class is referenced like a type (getTypeFromClassOrInterfaceReference),
// which needs its type arguments
// The class a base constructor that is a class names, and how many type arguments it gets (null: not a class)
static Symbol const* class_base_reference(ClassDecl const* me, u32* arg_count, bool* missing_extends) {
    Expr const* base = &my extends;
    *arg_count = 0;
    // (tsc's getTypeFromClassOrInterfaceReference: the clause, whatever type arguments an `@extends` tag gave it)
    *missing_extends = span_source(expr_outer_span(&my extends)) && span_source(expr_outer_span(&my extends))->is_js;
    if (base->tag == EXPR_GENERIC_PARAMS) {
        *arg_count = (u32)vec_spanned_type_size(&base->generic_params->params);
        base = &base->generic_params->expr;
    }
    if (expr_is_perenthisized(base)) return nullptr;
    Symbol const* target = nullptr;
    if (base->tag == EXPR_IDENTIFIER && !base->identifier.keyword) {
        target = base->identifier.target;
    } else if (base->tag == EXPR_PROPERTY && base->property->form == PROPERTY_EXPR_DOT &&
        base->property->expr.tag == EXPR_IDENTIFIER && !expr_is_perenthisized(&base->property->expr)
    ) {
        // A class in a namespace, `M.E` (like the members' class_base())
        Symbol const* namespace = symbol_for_meaning(base->property->expr.identifier.target, SYMBOL_NAMESPACE);
        PropertyExpr const* access = base->property;
        if (namespace && !namespace->is_placeholder && (access->form == PROPERTY_EXPR_DOT ||
            access->index.tag == EXPR_IDENTIFIER || access->index.tag == EXPR_LITERAL)
        ) {
            target = symbol_get_export(namespace, span_sv(property_key_inner_span(access)));
        }
    }
    target = symbol_for_meaning(target, SYMBOL_CLASS);
    if (!target || target->is_placeholder || !(target->flags & SYMBOL_CLASS)) return nullptr;
    return target;
}

bool class_decl_base_arity_fits(ClassDecl const* me) {
    u32 arg_count;
    bool missing_extends;
    Symbol const* target = class_base_reference(me, &arg_count, &missing_extends);
    // (In JavaScript the reference is still made, the missing arguments any: tsc's getTypeFromClassOrInterfaceReference)
    SourceFile const* source = span_source(expr_outer_span(&my extends));
    return !target || (source && source->is_js) || several_types_arity_fits(target, arg_count, source);
}

Ty const* class_decl_merged_interface_base(ClassDecl const* me) {
    for (u32 d = 0; my symbol && d < my symbol->decl_count; d++) {
        if (my symbol->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* interface = my symbol->decls[d].node;
        if (vec_spanned_type_size(&interface->extends)) return ty_of_node(&vec_spanned_type_at(&interface->extends, 0)->type);
    }
    return nullptr;
}

static void class_check_base_arity(ClassDecl const* me, ErrorCtx* errs) {
    u32 arg_count;
    bool missing_extends;
    Symbol const* target = class_base_reference(me, &arg_count, &missing_extends);
    Expr const* base = my extends.tag == EXPR_GENERIC_PARAMS ? &my extends.generic_params->expr : &my extends;
    if (!target && base->tag != EXPR_IMPLICIT_ANY) {
        // Any other expression (tsc's resolveBaseTypesOfClass): a constructor type that is a class's static side, when
        // the class has no outer type parameters, is a reference to that class, type arguments and all
        // (getTypeFromClassOrInterfaceReference); else a construct signature has to take that many (TS2508)
        Checker* checker = current_checker;
        Ty const* constructor = ty_apparent_type(checker, ty_expr_type(checker, base));
        if (constructor->kind == TY_DECLARED && constructor->declared.kind == TY_DECLARED_CLASS_STATIC) {
            Ty const* instance = ty_declared_type_of(checker, constructor->declared.symbol);
            if (instance->kind == TY_DECLARED && !instance->declared.outer_count) target = constructor->declared.symbol;
        }
        if (!target && (constructor->flags & (TYF_OBJECT | TYF_INTERSECTION)) &&
            !ty_base_constructors_fit(checker, constructor, arg_count, span_source(expr_outer_span(&my extends))->is_js)
        ) {
            error_ctx_error_d(errs, expr_outer_span(base), 2508, "No base constructor has the specified number of type arguments.");
            return;
        }
    }
    if (!target) return;
    // (A class without construct signatures, its default ones filtered by its own base's type arguments, is TS2507:
    // the base constructor is errorType, there's no reference to check)
    if (!span_valid(my jsdoc_extends) && !ty_members(current_checker, ty_static_type_of(current_checker, target))->construct_count) return;
    // (An `@augments` tag's own reference is checked too, at it: the type arguments the clause took from it, or none)
    if (span_valid(my jsdoc_extends)) {
        SpannedType const* first = my extends.tag == EXPR_GENERIC_PARAMS && vec_spanned_type_size(&my extends.generic_params->params) ?
            vec_spanned_type_at(&my extends.generic_params->params, 0) : nullptr;
        several_types_check_arity(target, first && first->type.from_jsdoc ? arg_count : 0, my jsdoc_extends, false, errs);
    }
    several_types_check_arity(target, arg_count, expr_outer_span(&my extends), missing_extends, errs);
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
            name, "' of constructor function '", span_sv(class_decl_display_name(me)), "'.");
    }
}


// tsc's checkPropertyInitialization: TS2564
static void class_check_property_initialization(ClassDecl const* me, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    if (!options->strict_null_checks || !options->strict_property_initialization || my is_ambient) return;
    Checker* checker = current_checker;
    ClassItem const* constructor = class_constructor_with_body(me);
    for_each (i, vec_class_item, &my items) {
        if (i->tag != CLASS_ITEM_FIELD || i->field.is_static || i->field.is_declare || i->field.is_abstract) continue;
        // (Auto-accessors and JavaScript fields too)
        if (i->field.value.tag != EXPR_IMPLICIT_ANY || !span_valid(i->field.name_span)) continue;
        if (i->field.name_is_value && !i->field.computed_key) continue; // (a string or numeric name)
        if (i->field.is_definite) continue;
        // (Its symbol's type: an optional one has undefined, unless its symbol's first declaration says otherwise)
        Ty const* type = ty_class_field_symbol_type(checker, me, i);
        if ((type->flags & (TYF_ANY | TYF_UNKNOWN)) || contains_undefined(type)) continue;
        if (constructor && field_initialized_in_constructor(checker, me, i, type, constructor)) continue;
        error_ctx_error_d(errs, i->field.name_span, 2564, "Property '", i->field.name_span,
            "' has no initializer and is not definitely assigned in the constructor.");
    }
}

// tsc's markDecoratorMedataDataTypeNodeAsReferenced (getEntityNameForDecoratorMetadata, then
// markEntityNameOrEntityExpressionAsReference): under isolatedModules and emitDecoratorMetadata with ES module output,
// the serialized type of a decorated declaration can't be a type that came through a plain import (TS1272)
static bool decorator_metadata_checked(void) {
    CompilerOptions const* options = compiler_options();
    return options->emit_decorator_metadata && (options->isolated_modules || options->verbatim_module_syntax) &&
        options->module >= MODULE_KIND_ES2015;
}

static TypeNode const* metadata_entity_type(TypeNode const* node) {
    if (!node) return nullptr;
    if (node->tag == TYPE_IDENTIFIER && !node->identifier->keyword && !identifier_type_import_ref(node->identifier)) return node;
    if (node->tag != TYPE_UNION && node->tag != TYPE_INTERSECTION) return nullptr;
    // (getEntityNameForDecoratorMetadataFromTypeList: the members' common entity name, never/null/undefined aside)
    TypeNode const* common = nullptr;
    for_each (i, vec_type, &node->composite) {
        if (i->tag == TYPE_NEVER) continue;
        TypeNode const* entity = metadata_entity_type(i);
        if (!entity) {
            if (!compiler_options()->strict_null_checks && i->tag == TYPE_VALUE &&
                (i->value.tag == VALUE_NULL || i->value.tag == VALUE_UNDEFINED)
            ) {
                continue;
            }
            return nullptr;
        }
        if (!common) common = entity;
        else if (!csview_equals_sv(span_sv(common->identifier->name), span_sv(entity->identifier->name))) return nullptr;
    }
    return common;
}

static void check_decorator_metadata_type(TypeNode const* node, ErrorCtx* errs) {
    TypeNode const* entity = metadata_entity_type(node);
    if (!entity) return;
    IdentifierType const* identifier = entity->identifier;
    bool qualified = identifier_type_namespace_count(identifier) > 0;
    Symbol const* root = qualified ? (vec_symbol_cptr_size(identifier_type_namespace_symbols(identifier)) ?
        *vec_symbol_cptr_front(identifier_type_namespace_symbols(identifier)) : nullptr) : identifier->symbol;
    if (!root || !(root->flags & SYMBOL_ALIAS) || !root->alias || !root->alias->item) return;
    if (symbol_resolved_flags(root) & SYMBOL_VALUE) return;
    PortItem const* item = root->alias->item;
    if (item->type_only) return;
    Span first = qualified ? *vec_span_front(identifier_type_namespaces(identifier)) : identifier->name;
    Span name = span_make_in(first, span_start_ptr(first), span_end_ptr(identifier->name));
    Error* error = error_ctx_error_d(errs, name, 1272, "A type referenced in a decorated signature must be imported with "
        "'import type' or a namespace import when 'isolatedModules' and 'emitDecoratorMetadata' are enabled.");
    if (error) error_add_related_d(error, item->full_span, 1376, "'", first, "' was imported here.");
}

static void check_decorator_metadata_signature(FunctionImpl const* function, bool with_return, ErrorCtx* errs) {
    for_each (i, vec_function_arg, &function->args) {
        if (i->initializer.binding.has_explicit_type) check_decorator_metadata_type(binding_type(&i->initializer.binding), errs);
    }
    if (with_return && function->ret_explicit) check_decorator_metadata_type(&function->ret, errs);
}

static void class_typecheck_implements(ClassDecl* me, ErrorCtx* errs);

void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs) {
    if (!ty_first_check(current_checker, me)) return;

    // tsc's checkClassNameCollisionWithObject (named by the module option, not the file's format)
    if (!my is_ambient && span_valid(my name) && csview_equals(span_sv(my name), "Object") && span_source(my name) &&
        source_file_emit_module_kind(span_source(my name)) < MODULE_KIND_ES2015
    ) {
        static char const* const module_names[] = {
            [MODULE_KIND_NONE] = "None", [MODULE_KIND_COMMONJS] = "CommonJS", [MODULE_KIND_AMD] = "AMD",
            [MODULE_KIND_UMD] = "UMD", [MODULE_KIND_SYSTEM] = "System", [MODULE_KIND_ES2015] = "ES2015",
            [MODULE_KIND_ES2020] = "ES2020", [MODULE_KIND_ES2022] = "ES2022", [MODULE_KIND_ESNEXT] = "ESNext",
            [MODULE_KIND_NODE16] = "Node16", [MODULE_KIND_NODE18] = "Node18", [MODULE_KIND_NODE20] = "Node20",
            [MODULE_KIND_NODENEXT] = "NodeNext", [MODULE_KIND_PRESERVE] = "Preserve",
        };
        ModuleKind module = compiler_options()->module;
        error_ctx_error_d(
            errs, my name, 2725, "Class name cannot be 'Object' when targeting ES5 and above with module ",
            module < c_arraylen(module_names) && module_names[module] ? module_names[module] : "ES2015", "."
        );
    }

    if (compiler_options()->experimental_decorators && my is_expression && vec_expr_size(&my decorators)) {
        Span at = decorator_span(vec_expr_at(&my decorators, 0));
        at.size = 1;
        error_ctx_grammar_error_d(errs, at, 1206, "Decorators are not valid here.");
    }
    // tsgo's checkClassDeclaration: legacy decorators of a class with a static private member
    if (compiler_options()->experimental_decorators && !my is_expression && vec_expr_size(&my decorators)) {
        for_each (i, vec_class_item, &my items) {
            if (!class_item_is_static(i) || !span_valid(class_item_private_name(i))) continue;
            error_ctx_grammar_error_d(errs, decorator_span(vec_expr_at(&my decorators, 0)), 18036,
                "Class decorators can't be used with static private identifier. Consider removing the experimental decorator.");
            break;
        }
    }
    for_each (i, vec_expr, &my decorators) {
        if (!(compiler_options()->experimental_decorators && my is_expression)) {
            ty_check_decorator(current_checker, i, (DecoratorSite){ .class_decl = me, .param_index = -1, .at = decorator_span(i) }, errs);
        }
        expr_typecheck(i, errs);
    }
    // (tsc's markDecoratorAliasReferenced of a class: its first constructor with a body's parameters)
    if (vec_expr_size(&my decorators) && !(compiler_options()->experimental_decorators && my is_expression) &&
        decorator_metadata_checked()
    ) {
        for_each (i, vec_class_item, &my items) {
            if (i->tag != CLASS_ITEM_CONSTRUCTOR || !i->constructor.func.has_body) continue;
            check_decorator_metadata_signature(&i->constructor.func, false, errs);
            break;
        }
    }
    if (!my is_ambient) class_check_static_name_conflicts(me, errs);
    bool legacy = compiler_options()->experimental_decorators;
    for_each (i, vec_class_item, &my items) {
        // tsc's checkGrammarModifiers via nodeCanBeDecorated(), at the `@` of the first decorator
        bool decorator_error = false;
        if (vec_expr_size(&i->decorators) && span_valid(i->span)) {
            bool valid = class_item_decoratable(me, i);
            // (Legacy decorators can't decorate a private name, TS1206 whatever else)
            unsigned code = i->tag == CLASS_ITEM_METHOD && !i->method.func.has_body && !i->method.accessor &&
                !(legacy && span_valid(class_item_private_name(i))) ? 1249 : 1206;
            if (!valid) {
                decorator_error = true;
                Span at = i->span;
                span_set_sv(&at, csview_from_n(span_start_ptr(i->span), 1));
                if (code == 1249) {
                    error_ctx_grammar_error_d(errs, at, 1249, "A decorator can only decorate a method implementation, not an overload.");
                } else {
                    error_ctx_grammar_error_d(errs, at, 1206, "Decorators are not valid here.");
                }
            }
        }
        // tsc's checkAmbientInitializer for a field of an ambient class or a `declare` field (one grammar error per
        // member: not after the decorators')
        if (!decorator_error && i->tag == CLASS_ITEM_FIELD && (my is_ambient || i->field.is_declare) && i->field.value.tag != EXPR_IMPLICIT_ANY) {
            if (i->field.is_readonly && !i->field.has_explicit_type) {
                if (!expr_is_ambient_const_initializer(&i->field.value)) {
                    error_ctx_grammar_error_d(errs, expr_outer_span(&i->field.value), 1254,
                        "A 'const' initializer in an ambient context must be a string or numeric literal or literal enum reference.");
                }
            } else {
                error_ctx_grammar_error_d(errs, expr_outer_span(&i->field.value), 1039, "Initializers are not allowed in ambient contexts.");
            }
        }
        // tsc's checkAccessorDeclaration: a getter and setter pair agree on abstract (TS2676), and the getter is at least
        // as accessible as the setter (TS2808), reported at both names
        if (i->tag == CLASS_ITEM_METHOD && i->method.accessor == CLASS_ACCESSOR_SET && i->method.accessor_getter &&
            !i->method.computed_key
        ) {
            ClassItem const* getter = i->method.accessor_getter;
            Span names[2] = { getter->method.name_span, i->method.name_span };
            if (getter->method.is_abstract != i->method.is_abstract) {
                for (u32 n = 0; n < 2; n++) {
                    error_ctx_error_d(errs, names[n], 2676, "Accessors must both be abstract or non-abstract.");
                }
            }
            if ((getter->access == ACCESS_MODIFIER_PROTECTED && i->access == ACCESS_MODIFIER_PUBLIC) ||
                (getter->access == ACCESS_MODIFIER_PRIVATE && i->access != ACCESS_MODIFIER_PRIVATE)
            ) {
                for (u32 n = 0; n < 2; n++) {
                    error_ctx_error_d(errs, names[n], 2808, "A get accessor must be at least as accessible as the setter");
                }
            }
        }
        // tsc's getTypeOfAccessors: no annotation on either accessor and no getter body to infer from is implicitly any,
        // reported at the setter if there is one (TS7032), else at the getter (TS7033)
        if (i->tag == CLASS_ITEM_METHOD && i->method.accessor && compiler_options()->no_implicit_any &&
            !(my is_ambient && i->access == ACCESS_MODIFIER_PRIVATE) && span_valid(i->method.name_span) &&
            span_source(i->method.name_span)
        ) {
            FunctionImpl const* func = &i->method.func;
            if (i->method.accessor == CLASS_ACCESSOR_SET) {
                ClassItem const* getter = i->method.accessor_getter;
                bool typed = vec_function_arg_size(&func->args) &&
                    vec_function_arg_at(&func->args, 0)->initializer.binding.has_explicit_type;
                if (!typed && (!getter || (!getter->method.func.ret_explicit && !getter->method.func.has_body && !getter->method.func.body_missing))) {
                    error_ctx_error_d(errs, i->method.name_span, 7032, "Property '", i->method.name_span,
                        "' implicitly has type 'any', because its set accessor lacks a parameter type annotation.");
                }
            } else if (!func->ret_explicit && !func->has_body && !func->body_missing) {
                bool has_setter = false;
                for_each (j, vec_class_item, &my items) {
                    has_setter |= j->tag == CLASS_ITEM_METHOD && j->method.accessor == CLASS_ACCESSOR_SET && j->method.accessor_getter == i;
                }
                if (!has_setter) {
                    error_ctx_error_d(errs, i->method.name_span, 7033, "Property '", i->method.name_span,
                        "' implicitly has type 'any', because its get accessor lacks a return type annotation.");
                }
            }
        }
        // Parameters: only legacy decorators, of methods, setters and constructors with a body in a class declaration
        FunctionImpl const* function = i->tag == CLASS_ITEM_METHOD ? &i->method.func :
            i->tag == CLASS_ITEM_CONSTRUCTOR ? &i->constructor.func : nullptr;
        for (isize a = 0; function && a < vec_function_arg_size(&function->args); a++) {
            FunctionArg const* arg = vec_function_arg_at(&function->args, a);
            if (!vec_expr_size(function_arg_decorators(arg))) continue;
            bool getter = i->tag == CLASS_ITEM_METHOD && i->method.accessor == CLASS_ACCESSOR_GET;
            // (tsc's parameter index doesn't count a `this` parameter, which can't be decorated)
            FunctionArg const* first = vec_function_arg_at(&function->args, 0);
            bool has_this = first->initializer.binding.tag == BINDING_IDENTIFIER &&
                span_equals(first->initializer.binding.content_span, "this");
            bool valid = legacy && function->has_body && !getter && !my is_expression;
            if (!valid && span_valid(arg->span)) {
                Span at = arg->span;
                span_set_sv(&at, csview_from_n(span_start_ptr(arg->span), 1));
                error_ctx_grammar_error_d(errs, at, 1206, "Decorators are not valid here.");
            }
            // (What can't be decorated isn't checked at all, a `this` parameter's decorators neither: tsc's checkDecorators)
            bool checked = valid && span_valid(arg->span) && !(has_this && a == 0);
            ErrorCtx ignored = error_ctx_scratch(errs);
            for_each (decorator, vec_expr, function_arg_decorators(arg)) {
                if (checked) {
                    ty_check_decorator(current_checker, decorator, (DecoratorSite){ .class_decl = me, .item = i,
                        .param_index = (i32)(a - (has_this ? 1 : 0)), .at = decorator_span(decorator) }, errs);
                }
                expr_typecheck(decorator, checked ? errs : &ignored);
            }
            error_ctx_drop(&ignored);
        }
        if (i->tag != CLASS_ITEM_STATIC_BLOCK && vec_expr_size(&i->decorators)) {
            ErrorCtx ignored = error_ctx_scratch(errs);
            for_each (decorator, vec_expr, &i->decorators) {
                if (!decorator_error) {
                    ty_check_decorator(current_checker, decorator, (DecoratorSite){ .class_decl = me, .item = i, .param_index = -1,
                        .at = decorator_span(decorator) }, errs);
                }
                expr_typecheck(decorator, decorator_error ? &ignored : errs);
            }
            error_ctx_drop(&ignored);
        }
    }

    class_check_emit_helpers(me, errs);

    generic_typecheck(&my generic, errs);
    ty_check_variance_annotations(current_checker, my symbol, &my generic, false, errs);

    if (my extends.tag != EXPR_IMPLICIT_ANY) {
        // The bases first, whose resolution evaluates the expression under its in-flight entry (tsc's getBaseTypes
        // before checkSourceElement(baseTypeNode.Expression()): TS2506 where the expression needs the class)
        if (my symbol) ty_class_base_constructor_circular(current_checker, me);
        expr_typecheck(&my extends, errs);
        Ty const* extends_type = ty_expr_type(current_checker, &my extends);
        if (extends_type->flags & TYF_NULL) {
        } else {
            // (Errors are about the expression, without its type arguments: tsc's baseTypeNode.expression)
            Expr const* expression = my extends.tag == EXPR_GENERIC_PARAMS ? &my extends.generic_params->expr : &my extends;
            CallExpr call_expr = {
                .uses_new = true,
                .is_extends = true,
                .callable = expr_implicit_any(expr_outer_span(expression)),
                .args = vec_expr_init(),
            };
            ty_call_typecheck(current_checker, extends_type, &call_expr, nullptr, expr_outer_span(&my extends), errs);
        }
    }

    class_heritage_typecheck(me, errs);
    class_check_overloads(me, errs);
    class_check_property_initialization(me, errs);
    class_check_overload_agreement(me, errs);
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

    // A class expression's members are checked after the statements of its file (tsc's checkClassExpressionDeferred)
    if (my is_expression && current_checker->defer_checks) {
        class_typecheck_implements(me, errs);
        checker_defer(current_checker, DEFERRED_CLASS_MEMBERS, me);
    } else {
        class_decl_typecheck_members(me, errs);
        class_typecheck_implements(me, errs);
    }
}

// The members of a class (tsc's checkSourceElement of each, which a class expression defers)
void class_decl_typecheck_members(ClassDecl* me, ErrorCtx* errs) {
    int constructor_count = 0; // Implementations
    for_each (i, vec_class_item, &my items) {
        // (Not after the decorators' grammar error: checkGrammarModifiers comes first, one grammar error per member)
        Span first_char = i->span;
        span_set_sv(&first_char, csview_from_n(span_start_ptr(i->span), span_valid(i->span) ? 1 : 0));
        bool decorators_rejected = vec_expr_size(&i->decorators) && span_valid(i->span) &&
            (error_ctx_find(errs, first_char, 1206) || error_ctx_find(errs, first_char, 1249));
        if (vec_expr_size(&i->decorators) && !decorators_rejected && decorator_metadata_checked()) {
            // (tsc's markDecoratorAliasReferenced)
            if (i->tag == CLASS_ITEM_METHOD && i->method.accessor) {
                ClassItem const* getter = i->method.accessor == CLASS_ACCESSOR_GET ? i : i->method.accessor_getter;
                FunctionImpl const* func = &i->method.func;
                if (getter && getter->method.func.ret_explicit) check_decorator_metadata_type(&getter->method.func.ret, errs);
                else if (i->method.accessor == CLASS_ACCESSOR_SET && vec_function_arg_size(&func->args) &&
                    vec_function_arg_back(&func->args)->initializer.binding.has_explicit_type
                ) {
                    check_decorator_metadata_type(binding_type(&vec_function_arg_back(&func->args)->initializer.binding), errs);
                }
            } else if (i->tag == CLASS_ITEM_METHOD) {
                check_decorator_metadata_signature(&i->method.func, true, errs);
            } else if (i->tag == CLASS_ITEM_FIELD && i->field.has_explicit_type) {
                check_decorator_metadata_type(&i->field.explicit_type, errs);
            }
        }
        switch (i->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                function_impl_typecheck(&i->constructor.func, errs);
                // tsc's checkConstructorDeclaration
                if (i->constructor.func.has_body && my extends.tag != EXPR_IMPLICIT_ANY) {
                    Expr const* super_call = function_impl_rare(&i->constructor.func)->first_super_call;
                    bool extends_null = my extends.tag == EXPR_LITERAL && my extends.literal.tag == VALUE_NULL;
                    if (super_call && extends_null) {
                        error_ctx_error_d(errs, super_call->inner_span, 17005,
                            "A constructor cannot contain a 'super' call when its class extends 'null'.");
                    }
                    if (!super_call && !extends_null) {
                        error_ctx_error_d(errs, i->constructor.name_span, 2377,
                            "Constructors for derived classes must contain a 'super' call.");
                    }
                    if (super_call && class_super_call_should_be_root(me, &i->constructor.func)) {
                        check_super_call_root(i->constructor.name_span, &i->constructor.func, super_call, errs);
                    }
                }
                if (i->constructor.func.has_body) constructor_count++; // Overloads are fine
                break;

            case CLASS_ITEM_METHOD:
                if (i->method.computed_key) class_computed_key_typecheck(me, i, decorators_rejected, errs);
                if (i->method.accessor && !i->method.func.has_body && !i->method.is_abstract && !i->method.func.ambient &&
                    !i->method.is_declare
                ) {
                    // tsc's checkGrammarAccessor: at the accessor's last character (its `;` if it has one)
                    Span full = i->method.func.full_span;
                    while (full.size && strchr(" \t\r\n", full.buf[full.size - 1])) full.size--;
                    full = span_with_semicolon(full);
                    char const* end = span_end_ptr(full);
                    error_ctx_grammar_error_d(errs, span_make_in(full, end - 1, end), 1005, "'{' expected.");
                }
                if (!i->method.computed_key && !i->method.name_is_value && span_equals(i->method.name_span, "constructor")) {
                    if (i->method.accessor) { // (tsc's checkAccessorDeclaration)
                        error_ctx_error_d(errs, i->method.name_span, 1341, "Class constructor may not be an accessor.");
                    } else if (i->method.func.is_generator) { // (tsc's checkMethodDeclaration)
                        error_ctx_error_d(errs, i->method.name_span, 1368, "Class constructor may not be a generator.");
                    }
                }
                // (tsc's checkGrammarModifiers: legacy decorators on both accessors of a pair, at the second)
                if (i->method.accessor && compiler_options()->experimental_decorators && vec_expr_size(&i->decorators) &&
                    i->method.accessor_first && i->method.accessor_first != i && vec_expr_size(&i->method.accessor_first->decorators)
                ) {
                    error_ctx_grammar_error_d(errs, block_first_token(i->span), 1207,
                        "Decorators cannot be applied to multiple get/set accessors of the same name.");
                }
                function_impl_typecheck(&i->method.func, errs);
                function_impl_check_implicit_any_return(&i->method.func, i->method.name_span, i->method.name_span, errs);
                if (i->method.is_abstract && !i->method.accessor && i->method.func.has_body) { // (tsc's checkMethodDeclaration)
                    error_ctx_error_d(errs, i->method.name_span, 1245, "Method '", i->method.name_span,
                        "' cannot have an implementation because it is marked abstract.");
                }
                break;

            case CLASS_ITEM_FIELD: {
                if (i->field.computed_key) class_computed_key_typecheck(me, i, decorators_rejected, errs);
                if (i->field.has_explicit_type) type_typecheck(&i->field.explicit_type, errs);
                // (tsc's checkVariableLikeDeclaration)
                if (!i->field.computed_key && span_is_bigint_name(i->field.name_span, i->field.name_is_value)) {
                    error_ctx_error_d(errs, i->field.name_span, 1539, "A 'bigint' literal cannot be used as a property name.");
                }
                // (tsc's checkPropertyDeclaration)
                if (i->field.is_abstract && i->field.value.tag != EXPR_IMPLICIT_ANY && span_valid(i->field.name_span)) {
                    error_ctx_error_d(errs, i->field.name_span, 1267, "Property '", i->field.name_span,
                        "' cannot have an initializer because it is marked abstract.");
                }
                // (tsc's checkGrammarTypeOperatorNode)
                if (i->field.has_explicit_type && i->field.explicit_type.tag == TYPE_UNIQUE_SYMBOL &&
                    (!i->field.is_static || !i->field.is_readonly)
                ) {
                    error_ctx_grammar_error_d(errs, i->field.name_span, 1331,
                        "A property of a class whose type is a 'unique symbol' type must be both 'static' and 'readonly'.");
                }
                if (i->field.has_explicit_type && i->field.value.tag != EXPR_IMPLICIT_ANY) {
                    // The initializer has the annotation as its contextual type (like a variable's)
                    // (An optional field's declared type has undefined: tsc's getTypeOfSymbol, addOptionality)
                    Ty const* contextual = ty_from_node(current_checker, &i->field.explicit_type, (LowerCtx){ .generic = true });
                    if (i->field.is_optional) contextual = ty_optional_prop_type(current_checker, contextual);
                    ty_check_expr(current_checker, &i->field.value, contextual);
                }
                expr_typecheck(&i->field.value, errs);
                class_field_check_implicit_any(me, i, errs);
                if (i->field.has_explicit_type && i->field.value.tag != EXPR_IMPLICIT_ANY) {
                    // (Joined by accessors, it's their symbol's type: tsc's checkVariableLikeDeclaration of the symbol's
                    // valueDeclaration checks the initializer against getTypeOfSymbol)
                    Ty const* declared = i->field.joined_accessor ? class_item_get_type(i) : ty_of_node(&i->field.explicit_type);
                    if (i->field.is_optional) declared = ty_optional_prop_type(current_checker, declared);
                    ty_check_assignable_expr(
                        current_checker, expr_get_type(&i->field.value), declared, i->field.name_span, &i->field.value, errs
                    );
                }
            }   break;

            case CLASS_ITEM_INTERFACE_MEMBER:
                interface_member_typecheck(&i->interface_memeber, true, false, errs);
                break;

            case CLASS_ITEM_STATIC_BLOCK:
                block_typecheck(&i->static_block, (BlockTypecheckCtx){ .in_function = true }, errs);
                break;
        }
    }

    if (constructor_count > 1) {
        for_each (i, vec_class_item, &my items) {
            // (tsc's checkFunctionOrConstructorSymbol: on every declaration, overloads too)
            if (i->tag == CLASS_ITEM_CONSTRUCTOR) {
                check(span_valid(i->constructor.name_span));
                error_ctx_error_d(
                    errs,
                    i->constructor.name_span, // (tsc's getErrorSpanForNode: the modifiers and the keyword)
                    2392,
                    "Multiple constructor implementations are not allowed."
                );
            }
        }
    }
}

// The implements clause and the base's type argument count (the end of tsc's checkClassLikeDeclaration)
static void class_typecheck_implements(ClassDecl* me, ErrorCtx* errs) {
    // (Whether the class implements them is class_check_heritage()'s business)
    for_each (i, vec_spanned_type, &my implements) {
        type_typecheck(&i->type, errs);
        // (A keyword type: what the parser made of an identifier in an expression's place, tsc's
        // checkAndReportErrorForUsingTypeAsValue)
        if (i->type.tag != TYPE_IDENTIFIER && is_primitive_type_name(span_sv(i->span))) {
            error_ctx_error_d(errs, i->span, 2864, "A class cannot implement a primitive type like '", i->span,
                "'. It can only implement other named object types.");
        }
    }
    for (u32 i = 0; i < my invalid_implements_checked; i++) {
        error_ctx_error_d(errs, expr_outer_span(&my invalid_implements.data[i]), 2500,
            "A class can only implement an identifier/qualified-name with optional type arguments.");
    }
    class_check_base_arity(me, errs);
    if (span_valid(my jsdoc_augments_name)) {
        // (tsgo's checkJSDocAugmentsTagMatchesExtends: the tag's type is the base type, else TS8023)
        bool identical = false;
        if (my jsdoc_augments) {
            type_typecheck(my jsdoc_augments, errs);
            Ty const* tag = ty_of_node(my jsdoc_augments);
            Ty const* base = ty_class_base_instance(current_checker, me);
            if (!base) base = ty_intrinsic(current_checker, TY_ERROR);
            identical = tag == base || ty_is_related(current_checker, tag, base, TY_RELATION_IDENTITY);
        }
        if (!identical) {
            error_ctx_error_d(errs, my jsdoc_augments_name, 8023, "JSDoc '@", my jsdoc_augments_tag, " ",
                my jsdoc_augments_name, "' does not match the 'extends ", my jsdoc_extends_name, "' clause.");
        }
    }
}
