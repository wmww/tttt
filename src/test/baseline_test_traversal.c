#include "baseline_test_traversal.h"
#include "baseline_test_case.h"
#include "type.h"
#include "interface_member.h"
#include "generic.h"
#include "expr.h"
#include "several_types.h"
#include "function_impl.h"
#include "port_statement.h"
#include "statement.h"
#include "block.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "function_impl.h"
#include "callable_type.h"
#include "ty/ty.h"
#include "symbol_chain.h"

// What the symbols baseline shows for a reference: the declarations of the symbol the name found (an alias shows its
// import, not what it resolves to), or of the declaration when there is no symbol (keywords, synthetic nodes)
static inline Symbol const* shown_declaration(Symbol const* symbol, Symbol const* decl) {
    return symbol && symbol->decl_count ? symbol : decl;
}

static inline VecSpan declaration_get_spans(Symbol const* me) {
    return symbol_declaration_spans(me);
}

// tsc's symbolToString(symbol, node.parent): the name of a symbol as the symbols baseline prints it from here
static cstr symbol_text(BaselineTestCase* ctx, Symbol const* symbol) {
    SymbolChain chain = symbol_chain_at(ctx->enclosing, symbol, 0, false);
    cstr ret = cstr_init();
    symbol_chain_append(&ret, ctx->enclosing, &chain);
    return ret;
}

// `A.B.C` as spans
// What the symbols baseline says about a property access, gathered over the members of a union or intersection
typedef struct PropSymbolInfo {
    bool found;
    u32 member_count; // Members of the object type that have the property
    bool missing_in_member; // Some member of a union has no such property: no symbol at all, like tsc
    Symbol const* parent;
    Symbol const* member_symbol; // A namespace's or module's export: named by its symbol
    Symbol const* namespace; // The object is a namespace's or module's value: the property is its export
    Span name_span;
    bool name_is_value;
    VecSpan declarations;
    // tsc's synthetic union/intersection property keeps the parent only when every member's property has the same
    // declaration (hasNonUniformValueDeclaration)
    Span first_declaration;
    bool non_uniform;
} PropSymbolInfo;

static void collect_prop_symbol(Checker* checker, Ty const* object, Atom const* name, PropSymbolInfo* info, u32 depth) {
    if (depth > 8) return;
    if (object->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) {
        Ty const* constraint = ty_base_constraint(checker, object);
        if (constraint) object = constraint;
    }
    if (object->kind == TY_UNION || object->kind == TY_INTERSECTION) {
        PropSymbolInfo inner = { .declarations = vec_span_init() };
        for (u32 i = 0; i < object->composite.types.len; i++) {
            collect_prop_symbol(checker, object->composite.types.data[i], name, &inner, depth + 1);
        }
        bool keep = inner.found && (object->kind == TY_INTERSECTION || !inner.missing_in_member);
        if (keep) {
            if (!info->found) {
                info->found = true;
                info->parent = inner.parent;
                info->namespace = inner.namespace;
                info->member_symbol = inner.member_count == 1 ? inner.member_symbol : nullptr;
                info->name_span = inner.name_span;
                info->name_is_value = inner.name_is_value;
                info->first_declaration = inner.first_declaration;
            } else if (!span_eq(info->first_declaration, inner.first_declaration)) {
                info->non_uniform = true;
            }
            info->non_uniform |= inner.non_uniform;
            info->member_count += inner.member_count;
            for_each (k, vec_span, &inner.declarations) {
                bool seen = false;
                for_each (l, vec_span, &info->declarations) seen |= span_cmp(*l, *k) == 0 && l->sv.size == k->sv.size;
                if (!seen) vec_span_push(&info->declarations, *k);
            }
        } else if (object->kind == TY_UNION) {
            info->missing_in_member = true;
        }
        vec_span_drop(&inner.declarations);
        return;
    }
    object = ty_apparent_type(checker, object);
    if (!(object->flags & TYF_OBJECT)) return;
    Prop const* prop = ty_get_object_prop(checker, object, name);
    if (!prop) {
        info->missing_in_member = true;
        return;
    }
    Prop const* root = prop;
    while (root->target && !root->decl_span_count) root = root->target;
    Span first_declaration = root->decl_span_count ? root->decl_spans[0] : span_null;
    if (!info->found) {
        info->found = true;
        info->parent = prop->parent;
        if (object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_NAMESPACE) {
            info->namespace = object->declared.namespace_symbol;
        }
        info->member_symbol = root->symbol;
        info->name_span = root->name_span;
        info->name_is_value = root->name_is_value;
        info->first_declaration = first_declaration;
    } else if (!span_eq(info->first_declaration, first_declaration)) {
        info->non_uniform = true;
    }
    info->member_count++;
    for (u32 j = 0; j < root->decl_span_count; j++) {
        bool seen = false;
        for_each (k, vec_span, &info->declarations) seen |= span_cmp(*k, root->decl_spans[j]) == 0 && k->sv.size == root->decl_spans[j].sv.size;
        if (!seen) vec_span_push(&info->declarations, root->decl_spans[j]);
    }
}

// The name of a member's declaration from here: its container's chain (see prop_text) and its name as written (a
// computed name `[x]` too). Without a container symbol, the container's name (`container_span`) or nothing
static cstr member_text(BaselineTestCase* ctx, Symbol const* container, Span container_span, csview name, bool enum_member) {
    cstr ret = cstr_init();
    SymbolChain chain = {0};
    if (container) chain = symbol_member_container_chain_at(ctx->enclosing, container);
    if (chain.len) {
        symbol_chain_append(&ret, ctx->enclosing, &chain);
        symbol_chain_append_member(&ret, name, enum_member);
    } else if (!container && span_valid(container_span)) {
        cstr_append_span(&ret, container_span);
        symbol_chain_append_member(&ret, name, enum_member);
    } else {
        cstr_append_sv(&ret, name);
    }
    return ret;
}

// The name of a property's symbol from here: a namespace's or module's export by its own chain, a member by the chain
// of its container (tsc's getContainersOfSymbol of a member), else bare
static cstr prop_text(PropSymbolInfo const* info, BaselineTestCase* ctx, Span fallback) {
    csview name = span_valid(info->name_span) ? info->name_span.sv : fallback.sv;
    cstr ret = cstr_init();
    Symbol const* member = info->member_symbol;
    // A member of a namespace's or module's object is its export (maybe an alias: getSymbolAtLocation doesn't resolve)
    // (by the name accessed: the export's own name may differ, `export default class C`)
    csview accessed = fallback.sv;
    if (accessed.size >= 2 && (accessed.buf[0] == '"' || accessed.buf[0] == '\'')) accessed = csview_slice(accessed, 1, accessed.size - 1);
    Symbol const* exported = info->member_count == 1 && info->namespace ?
        symbol_get_export(info->namespace, accessed) : nullptr;
    if (exported) {
        cstr_drop(&ret);
        return symbol_text(ctx, exported);
    }
    if (info->member_count == 1 && member && member->parent && !info->name_is_value &&
        (!info->parent || info->parent->is_module || member->parent == info->parent)
    ) {
        cstr_drop(&ret);
        return symbol_text(ctx, member);
    }
    SymbolChain chain = {0};
    if (info->parent && !(info->member_count > 1 && info->non_uniform)) {
        chain = symbol_member_container_chain_at(ctx->enclosing, info->parent);
    }
    if (!chain.len) {
        cstr_append_sv(&ret, name);
        return ret;
    }
    symbol_chain_append(&ret, ctx->enclosing, &chain);
    symbol_chain_append_member(&ret, name, false);
    return ret;
}

void generic_compile_test_info(Generic const* me, BaselineTestCase* ctx) {
    for_each (i, vec_generic_item, &my items) {
        // (A type parameter of merged declarations is declared by all of them)
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .span = i->name,
            .declarations = i->owned_symbol ? symbol_declaration_spans(i->owned_symbol) : vec_span_single(i->full_span),
        });
        type_compile_test_info(&i->extends, ctx);
        if (i->fallback_explicit) {
            type_compile_test_info(&i->fallback, ctx);
        }
    }
}

void binding_content_compile_test_info(Binding const* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case BINDING_NOOP:
            baseline_test_case_add_type_node(ctx, my content_span, &type_undefined);
            break;

        case BINDING_IDENTIFIER:
            if (my parent) ctx->any_keeps_underline = true; // A binding element
            if (!my parent && span_valid(ctx->parameter_property_class) && my symbol) {
                // A parameter property's name declares the property (tsc's parameter.symbol is the property's)
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = symbol_declaration_spans(my symbol),
                    .namespaces = vec_span_single(ctx->parameter_property_class),
                    .span = my content_span,
                });
                baseline_test_case_add_symbol_value(ctx, my content_span, my symbol);
                break;
            }
            baseline_test_case_add_declaration(ctx, my content_span, my symbol);
            break;

        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) {
                binding_content_compile_test_info(i, ctx);
            }
            break;

        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) {
                if (i->computed_key) {
                    expr_compile_test_info(i->computed_key, ctx);
                }
                binding_content_compile_test_info(&i->target, ctx);
            }
            break;
    }
    if (my default_value) {
        expr_compile_test_info(my default_value, ctx);
    }
}

void binding_compile_test_info(Binding const* me, BaselineTestCase* ctx) {
    binding_content_compile_test_info(me, ctx);
    // (the parameters of a parameter property's type are no members)
    Span parameter_property_class = ctx->parameter_property_class;
    ctx->parameter_property_class = span_null;
    if (my has_explicit_type) {
        type_compile_test_info(&my explicit_type, ctx);
    }
    ctx->parameter_property_class = parameter_property_class;
}

void initialized_binding_compile_test_info(
    InitializedBinding* me,
    BaselineTestCase* ctx
) {
    binding_compile_test_info(&my binding, ctx);
    if (my value.tag != EXPR_IMPLICIT_ANY) {
        expr_compile_test_info(&my value, ctx);
    }
}

// The type of an optional property includes undefined, like tsc's type of its symbol
static Ty const* optional_member_type(Ty const* type, bool optional) {
    if (!optional || !compiler_options()->strict_null_checks) return type;
    Checker* checker = current_checker;
    Ty const* parts[2] = { type, ty_intrinsic(checker, TY_UNDEFINED) };
    return ty_union(checker, parts, 2, nullptr, (TyList){0});
}

// The type parameters of the class or interface aren't in scope in a computed member name (tsc's resolveName)
static void computed_member_name_compile_test_info(Expr* key, BaselineTestCase* ctx) {
    u32 inside = ctx->scope_params_len;
    bool in_class = ctx->class_params_len && ctx->class_params_outside + ctx->class_params_len <= inside;
    if (in_class) baseline_test_case_leave_generic(ctx, ctx->class_params_outside);
    expr_compile_test_info(key, ctx);
    if (in_class) {
        baseline_test_case_enter_type_params(ctx, (TyList){ ctx->class_params, ctx->class_params_len });
        check(ctx->scope_params_len == inside);
    }
}

void interface_member_compile_test_info(InterfaceMember const* me, Span scope, BaselineTestCase* ctx) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: {
            // Overloaded method signatures are one member, declared by all of them (of every declaration of an
            // interface), whose type has all the signatures
            Prop const* merged = nullptr;
            if (my key.name_is_member_function && ctx->member_owner) {
                Checker* checker = current_checker;
                Prop const* prop = ty_get_prop(checker, ctx->member_owner, checker_atom(checker, cstr_sv(&my key.name_string)));
                if (prop && prop->decl_span_count > 1 && (prop->flags & PROP_METHOD)) merged = prop;
            }
            VecSpan declarations = vec_span_init();
            if (merged) {
                for (u32 i = 0; i < merged->decl_span_count; i++) vec_span_push(&declarations, merged->decl_spans[i]);
            } else {
                declarations = vec_span_clone(my declarations);
            }
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = declarations,
                .span = my key.span,
                .text = member_text(ctx, ctx->member_container, scope, my key.span.sv, false),
            });
            Ty const* type = merged ? prop_type(current_checker, merged) : optional_member_type(ty_of_node(&my type), my optional);
            bool method = my key.name_is_member_function && my type.tag == TYPE_CALLABLE;
            u32 saved = method ? baseline_test_case_enter_generic(ctx, &my type.callable->function->generic) : 0;
            baseline_test_case_add_type(ctx, my key.span, type);
            if (method) baseline_test_case_leave_generic(ctx, saved);
        }   break;

        case INTERFACE_KEY_INDEX:
            baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = my key.index.name });
            baseline_test_case_add_type_node(ctx, my key.index.name, &my key.index.type);
            type_compile_test_info(&my key.index.type, ctx);
            break;

        case INTERFACE_KEY_EXPR:
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_single(my key.span),
                .span = my key.span,
                .text = member_text(ctx, ctx->member_container, scope, my key.span.sv, false),
            });
            baseline_test_case_add_type(ctx, my key.span, optional_member_type(ty_of_node(&my type), my optional));
            computed_member_name_compile_test_info(my key.expr.ref, ctx);
            break;

        case INTERFACE_KEY_MAPPED:
            baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = my key.mapped.type_name });
            type_compile_test_info(&my key.mapped.in_type, ctx);
            type_compile_test_info(&my key.mapped.as_type, ctx);
            break;

        case INTERFACE_KEY_CALLABLE: break;
    }
    type_compile_test_info(&my type, ctx);
}

void interface_compile_test_info(Interface const* me, Span scope, BaselineTestCase* ctx) {
    for_each (i, vec_interface_member, &my members) {
        interface_member_compile_test_info(i, scope, ctx);
    }
}

static void type_predicate_compile_test_info(TypePredicateNode* me, BaselineTestCase* ctx);

void type_compile_test_info(TypeNode const* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_UNIQUE_SYMBOL:
        case TYPE_BIGINT:
            break;

        case TYPE_VALUE: {
            // tsc's walk sees `true`, `false` and `-1` (and its `1`) as expressions, not other literal types
            if (!span_valid(my value_span)) break;
            bool negative = (my value.tag == VALUE_NUMBER || my value.tag == VALUE_BIGINT) && my value_span.sv.size && my value_span.sv.buf[0] == '-';
            if (my value.tag != VALUE_BOOLEAN && !negative) break;
            baseline_test_case_add_type_node(ctx, my value_span, me);
            if (negative) {
                Span operand = my value_span;
                operand.sv.buf++;
                operand.sv.size--;
                while (operand.sv.size && (operand.sv.buf[0] == ' ' || operand.sv.buf[0] == '\t')) {
                    operand.sv.buf++;
                    operand.sv.size--;
                }
                Value positive = my value.tag == VALUE_BIGINT ? (Value){ .tag = VALUE_BIGINT, .bigint = -my value.bigint }
                                                              : value_number(-my value.number);
                baseline_test_case_add_type(ctx, operand, ty_literal(current_checker, &positive));
            }
        }   break;

        case TYPE_ANY:
            if (span_valid(my any.unkown_symbol_span)) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .allow_declaration_empty = true,
                    .name = sv_make_span(c_sv("unknown")),
                    .span = my any.unkown_symbol_span,
                });
            }
            break;

        case TYPE_IDENTIFIER:
            if (span_equals(my identifier->name, "object")) {
                break;
            }
            if (my identifier->keyword == KEYWORD_IDENTIFIER_THIS) break; // A `this` type isn't an identifier to tsc
            if (my identifier->target && !span_valid(my identifier->target->name_span)) {
                break;
            }
            VecSpan so_far = vec_span_init();
            for_each (i, vec_span, &my identifier->namespaces) {
                intptr_t index = i_index;
                Symbol const* decl = index < vec_symbol_cptr_size(&my identifier->namespace_symbols) ?
                    *vec_symbol_cptr_at(&my identifier->namespace_symbols, index) :
                    nullptr;
                ctx->any_keeps_underline = true; // Part of a qualified name
                baseline_test_case_add_type_node(ctx, *i, &type_any);
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = declaration_get_spans(decl),
                    .allow_declaration_empty = true,
                    .namespaces = vec_span_clone(so_far),
                    .span = *i,
                    .text = decl && !decl->is_placeholder ? symbol_text(ctx, decl) : cstr_init(),
                });
                vec_span_push(&so_far, *i);
            }
            vec_span_drop(&so_far);
            if (!span_valid(my identifier->name)) break; // A bare `import("./a")`
            Symbol const* shown = shown_declaration(my identifier->symbol, my identifier->target);
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .namespaces = vec_span_clone(my identifier->namespaces),
                .declarations = declaration_get_spans(shown),
                .allow_declaration_empty = true,
                .span = my identifier->name,
                .text = shown && !shown->is_placeholder ? symbol_text(ctx, shown) : cstr_init(),
            });
            for_each (i, vec_spanned_type, &my identifier->generic_params) {
                type_compile_test_info(&i->type, ctx);
            }
            break;

        case TYPE_INFERRED:
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = declaration_get_spans(my inferred->symbol_ref),
                .allow_declaration_empty = true,
                .span = my inferred->name,
            });
            break;

        case TYPE_CALLABLE: {
            Span parameter_property_class = ctx->parameter_property_class;
            ctx->parameter_property_class = span_null;
            u32 saved = baseline_test_case_enter_generic(ctx, &my callable->function->generic);
            generic_compile_test_info(&my callable->function->generic, ctx);
            for_each (i, vec_function_arg, &my callable->function->args) {
                binding_content_compile_test_info(&i->initializer.binding, ctx);
                if (i->initializer.binding.has_explicit_type) {
                    type_compile_test_info(&i->initializer.binding.explicit_type, ctx);
                }
            }
            if (my callable->function->predicate) {
                type_predicate_compile_test_info(my callable->function->predicate, ctx);
            } else {
                type_compile_test_info(&my callable->function->ret, ctx);
            }
            baseline_test_case_leave_generic(ctx, saved);
            ctx->parameter_property_class = parameter_property_class;
        }   break;

        case TYPE_INTERFACE: {
            Ty const* outer_owner = ctx->member_owner;
            ctx->member_owner = ty_of_node(me);
            Symbol const* outer_container = ctx->member_container;
            ctx->member_container = nullptr;
            u32 outer_class_params_len = ctx->class_params_len;
            ctx->class_params_len = 0; // (A type literal's computed names see them)
            interface_compile_test_info(&my interface, span_null, ctx);
            ctx->class_params_len = outer_class_params_len;
            ctx->member_container = outer_container;
            ctx->member_owner = outer_owner;
        }   break;

        case TYPE_ARRAY:
            type_compile_test_info(my array.element, ctx);
            break;

        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &my tuple.elements) {
                type_compile_test_info(&i->type, ctx);
            }
            break;

        case TYPE_INDEXED:
            type_compile_test_info(&my indexed->interface, ctx);
            type_compile_test_info(&my indexed->index, ctx);
            break;

        case TYPE_KEYOF:
            type_compile_test_info(&my keyof->interface, ctx);
            break;

        case TYPE_TYPEOF:
            expr_compile_test_info(my typeof_->expr, ctx);
            break;

        case TYPE_TERNARY:
            type_compile_test_info(&my ternary->maybe_sub, ctx);
            type_compile_test_info(&my ternary->maybe_super, ctx);
            type_compile_test_info(&my ternary->if_true, ctx);
            type_compile_test_info(&my ternary->if_false, ctx);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &my composite) {
                type_compile_test_info(i, ctx);
            }
            break;

        case TYPE_TEMPLATE:
            for_each (i, vec_type, &my template_->types) {
                type_compile_test_info(i, ctx);
            }
            break;
    }
}

// `x is T`: the symbols baseline has the parameter, the types baseline nothing
static void type_predicate_compile_test_info(TypePredicateNode* me, BaselineTestCase* ctx) {
    if (my symbol) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = symbol_declaration_spans(my symbol),
            .span = my name,
            .name = my name,
        });
    }
    if (my has_type) type_compile_test_info(&my type, ctx);
}

void function_impl_compile_test_info(FunctionImpl* me, BaselineTestCase* ctx) {
    Span outer_parameter_property_class = ctx->parameter_property_class;
    ctx->parameter_property_class = span_null;
    u32 saved = baseline_test_case_enter_generic(ctx, &my generic);
    if (span_valid(my name)) {
        baseline_test_case_add_declaration(ctx, my name, my symbol);
    }
    generic_compile_test_info(&my generic, ctx);
    for_each (i, vec_function_arg, &my args) {
        for_each (decorator, vec_expr, &i->decorators) {
            expr_compile_test_info(decorator, ctx);
        }
        Span saved = ctx->parameter_property_class;
        if (i->is_parameter_property && my constructed_class && span_valid(my constructed_class->name_span)) {
            ctx->parameter_property_class = my constructed_class->name_span;
        }
        initialized_binding_compile_test_info(&i->initializer, ctx);
        ctx->parameter_property_class = saved;
    }
    if (my predicate) {
        type_predicate_compile_test_info(my predicate, ctx);
    } else if (my ret_explicit && !my is_constructor) {
        type_compile_test_info(&my ret, ctx);
    }
    block_compile_test_info(&my body, ctx);
    baseline_test_case_leave_generic(ctx, saved);
    ctx->parameter_property_class = outer_parameter_property_class;
}

// An alias shows its own declaration as the symbol and the value of what it resolves to as the type
static void port_item_add_alias(PortItem const* me, BaselineTestCase* ctx) {
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        .declarations = symbol_declaration_spans(my symbol),
        .span = my decl_name,
        .text = symbol_text(ctx, my symbol),
    });
    // tsc shows aliases of pure types as any
    ctx->any_keeps_underline = true; // An import/export name
    baseline_test_case_add_symbol_value(ctx, my decl_name, my symbol);
}

void port_statement_compile_test_info(PortStatement const* me, BaselineTestCase* ctx) {
    switch (my main.tag) {
        case PORT_IMPORT_DEFAULT:
        case PORT_IMPORT_GATHER:
        case PORT_REEXPORT_GATHER:
            // Null when the scope refused the import, eg `import a = require("b")` inside an ambient module
            if (my main.symbol) port_item_add_alias(&my main, ctx);
            break;

        case PORT_IMPORT_ALIAS:
            if (!my main.symbol) {
                ctx->any_keeps_underline = true;
                baseline_test_case_add_type_node(ctx, my main.decl_name, &type_any);
            } else {
                port_item_add_alias(&my main, ctx);
            }
            for_each (i, vec_span, &my main.alias_path) {
                ctx->any_keeps_underline = true; // Part of a qualified name
                if (i_index >= vec_symbol_cptr_size(&my main.alias_path_symbols)) {
                    baseline_test_case_add_type_node(ctx, *i, &type_any);
                    continue;
                }
                Symbol const* symbol = *vec_symbol_cptr_at(&my main.alias_path_symbols, i_index);
                Symbol const* decl = symbol_for_meaning(symbol, SYMBOL_ANY_MEANING);
                if (!decl) decl = symbol;
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = symbol_declaration_spans(decl),
                    .span = *i,
                    .text = decl->is_placeholder ? cstr_from_sv(decl->name_span.sv) : symbol_text(ctx, shown_declaration(symbol, decl)),
                });
                if (decl->flags & SYMBOL_TYPE && !(decl->flags & SYMBOL_NAMESPACE) && !decl->is_placeholder) {
                    TypeNode t = type_identifier(c_new(IdentifierType, {
                        .name = decl->name_span,
                        .target = decl,
                    }));
                    baseline_test_case_add_type_node(ctx, *i, &t);
                    type_drop(&t);
                } else {
                    baseline_test_case_add_symbol_value(ctx, *i, decl);
                }
            }
            break;

        default: break;
    }
    for_each (i, vec_port_item, &my items) {
        if (!i->symbol) continue;
        if (i->tag == PORT_IMPORT_GATHER) { // `import d, * as ns`
            port_item_add_alias(i, ctx);
            continue;
        }
        if (i->decl_name.sv.buf != i->source_name.sv.buf) {
            if (i->source_symbol) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = symbol_declaration_spans(i->source_symbol),
                    .span = i->source_name,
                    .text = i->source_symbol->is_placeholder ? cstr_init() : symbol_text(ctx, i->source_symbol),
                });
            }
            ctx->any_keeps_underline = true;
            baseline_test_case_add_symbol_value(ctx, i->source_name, i->symbol);
        }
        port_item_add_alias(i, ctx);
    }
    // `with { type: "json" }`: the keys written as identifiers are any
    if (my attributes) {
        for_each (i, vec_import_attribute, &my attributes->elements) {
            if (!span_valid(i->name) || i->name.sv.buf[0] == '"' || i->name.sv.buf[0] == '\'') continue;
            ctx->any_keeps_underline = true;
            baseline_test_case_add_type_node(ctx, i->name, &type_any);
        }
    }
}

void variable_statement_compile_test_info(VariableStatement const* me, BaselineTestCase* ctx) {
    for_each (i, vec_initialized_binding, &my initializers) {
        initialized_binding_compile_test_info(i, ctx);
    }
}

void enum_decl_compile_test_info(EnumDecl const* me, BaselineTestCase* ctx) {
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        // Every declaration of a merged enum
        .declarations = my symbol ? symbol_declaration_spans(my symbol) : vec_span_single(my full_span),
        .span = my name,
    });
    baseline_test_case_add_type(
        ctx, my name,
        my symbol ? ty_declared_type_of(current_checker, my symbol) : ty_unresolved(current_checker, my name.sv)
    );
    for_each (i, vec_enum_case, &my cases) {
        //baseline_test_case_add_declaration(ctx, i->name, i->symbol);
        Span span = i->full_name;
        Symbol const* declaration = i->symbol;
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = symbol_declaration_spans(declaration),
            .span = span,
            .text = member_text(ctx, my symbol, my name, (i->name_is_value ? i->full_name : declaration->name_span).sv, true),
        });
        baseline_test_case_add_symbol_value(ctx, span, declaration);
        if (i->value.tag != EXPR_IMPLICIT_ANY) {
            expr_compile_test_info(&i->value, ctx);
        }
    }
}

// The members of a class merge when the first of a name (and staticness) is a method: then its overloads are one
// symbol (a property or accessor first conflicts with the methods, which then stand alone)
static bool overloads_merge(ClassItem const* me, VecClassItem const* items, u32* count) {
    *count = 0;
    bool first_seen = false, merge = false;
    for_each (other, vec_class_item, items) {
        bool is_method = other->tag == CLASS_ITEM_METHOD;
        if (!is_method && other->tag != CLASS_ITEM_FIELD) continue;
        if (is_method ? other->method.computed_key != nullptr : other->field.computed_key != nullptr) continue;
        if (class_item_is_static((ClassItem*)other) != my method.is_static) continue;
        if (!cstr_eq(is_method ? &other->method.name_string : &other->field.name_string, &my method.name_string)) continue;
        if (!first_seen) {
            first_seen = true;
            merge = is_method && !other->method.accessor;
        }
        if (is_method && !other->method.accessor) (*count)++;
    }
    return merge;
}

void class_item_compile_test_info(ClassItem* me, VecClassItem const* items, Span class_name, BaselineTestCase* ctx) {
    for_each (i, vec_expr, &my decorators) {
        expr_compile_test_info(i, ctx);
    }
    switch(my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_compile_test_info(&my constructor.func, ctx);
            break;

        case CLASS_ITEM_METHOD: {
            u32 saved_generic = baseline_test_case_enter_generic(ctx, &my method.func.generic);
            VecSpan declarations = {0};
            Span written_name = my method.name_span;
            if (my method.accessor && my method.accessor_first) {
                // Both halves of an accessor pair declare the same property. Items live in one vec, so walk forward
                // from the first
                ClassItem* first = my method.accessor_first;
                written_name = first->method.name_span;
                vec_span_push(&declarations, first->span);
                for_each (other, vec_class_item, items) {
                    if (other != first && other->tag == CLASS_ITEM_METHOD && other->method.accessor_first == first) {
                        vec_span_push(&declarations, other->span);
                    }
                }
            } else if (!my method.computed_key && overloads_merge(me, items, &(u32){0})) {
                // Overloads are declarations of one symbol
                for_each (other, vec_class_item, items) {
                    if (other->tag == CLASS_ITEM_METHOD && !other->method.accessor && !other->method.computed_key &&
                        other->method.is_static == my method.is_static && cstr_eq(&other->method.name_string, &my method.name_string)
                    ) {
                        if (vec_span_is_empty(&declarations)) written_name = other->method.name_span;
                        vec_span_push(&declarations, other->span);
                    }
                }
            } else {
                vec_span_push(&declarations, my span);
            }
            // (named as its first declaration writes it)
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = declarations,
                .span = my method.name_span,
                .text = member_text(ctx, ctx->class_symbol, class_name, written_name.sv, false),
            });
            // An overloaded method is all of its overloads (the member's type)
            u32 overload_count = 0;
            Ty const* type = class_item_get_type(me);
            if (!my method.accessor && !my method.computed_key && ctx->class_symbol && overloads_merge(me, items, &overload_count) &&
                overload_count > 1
            ) {
                Checker* checker = current_checker;
                Ty const* owner = my method.is_static ? ty_static_type_of(checker, ctx->class_symbol) :
                    ty_declared_type_of(checker, ctx->class_symbol);
                Prop const* prop = ty_get_prop(checker, owner, checker_atom(checker, cstr_sv(&my method.name_string)));
                if (prop) type = prop_type(checker, prop);
            }
            baseline_test_case_add_type(ctx, my method.name_span, optional_member_type(type, my method.is_optional));
            // (Not in scope in its computed name)
            baseline_test_case_leave_generic(ctx, saved_generic);
            if (my method.computed_key) computed_member_name_compile_test_info(my method.computed_key, ctx);
            function_impl_compile_test_info(&my method.func, ctx);
        }   break;

        case CLASS_ITEM_FIELD: {
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_single(my span),
                .span = my field.name_span,
                .text = member_text(ctx, ctx->class_symbol, class_name, my field.name_span.sv, false),
            });
            baseline_test_case_add_type(ctx, my field.name_span, optional_member_type(class_item_get_type(me), my field.is_optional));
            if (my field.computed_key) computed_member_name_compile_test_info(my field.computed_key, ctx);

            if (my field.has_explicit_type) {
                type_compile_test_info(&my field.explicit_type, ctx);
            }
            if (my field.value.tag != EXPR_IMPLICIT_ANY) {
                expr_compile_test_info(&my field.value, ctx);
            }
        }   break;

        case CLASS_ITEM_INTERFACE_MEMBER:
            interface_member_compile_test_info(&my interface_memeber, class_name, ctx);
            break;

        case CLASS_ITEM_STATIC_BLOCK:
            block_compile_test_info(&my static_block, ctx);
            break;
    }
}

void class_decl_compile_test_info(ClassDecl* me, bool is_expr, BaselineTestCase* ctx) {
    for_each (i, vec_expr, &my decorators) {
        expr_compile_test_info(i, ctx);
    }
    // (Those of the merged declarations: one symbol each, like tsc's)
    TyList class_params = my symbol ? ty_declared_type_of(current_checker, my symbol)->declared.type_params :
        ty_type_params_of(current_checker, &my generic);
    u32 saved_generic = baseline_test_case_enter_type_params(ctx, class_params);
    if (span_valid(my name)) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(my symbol),
            .span = my name,
        });
        if (is_expr) {
            baseline_test_case_add_symbol_value(ctx, my name, my symbol);
        } else {
            baseline_test_case_add_type(ctx, my name, ty_class_instance_type(current_checker, my symbol));
        }
    }
    generic_compile_test_info(&my generic, ctx);
    if (my extends.tag != EXPR_IMPLICIT_ANY) {
        // tsc shows the instance type at the name of the base class, not `typeof Base`
        Ty const* extends_type = ty_expr_type(current_checker, &my extends);
        if (extends_type->kind == TY_DECLARED && extends_type->declared.kind == TY_DECLARED_CLASS_STATIC &&
            extends_type->declared.symbol
        ) {
            if (my extends.tag != EXPR_GENERIC_PARAMS) {
                Members const* statics = ty_members(current_checker, extends_type);
                Ty const* instance = statics->construct_count ?
                    signature_return_type(current_checker, statics->construct[0]) :
                    extends_type;
                baseline_test_case_add_type_recursive(ctx, my extends.outer_span, my extends.inner_span, instance);
            }
            expr_compile_test_info_inner(&my extends, ctx);
        } else {
            expr_compile_test_info(&my extends, ctx);
        }
    }
    for_each (i, vec_type, &my implements) {
        type_compile_test_info(i, ctx);
    }
    Symbol const* outer_class = ctx->class_symbol;
    Symbol const* outer_container = ctx->member_container;
    ctx->class_symbol = my symbol;
    ctx->member_container = my symbol;
    TyList outer_class_params = { ctx->class_params, ctx->class_params_len };
    u32 outer_class_params_outside = ctx->class_params_outside;
    ctx->class_params = class_params.data;
    ctx->class_params_len = class_params.len;
    ctx->class_params_outside = saved_generic;
    for_each (i, vec_class_item, &my items) {
        // (The class's type parameters aren't in scope in a static member)
        bool is_static = class_item_is_static(i);
        if (is_static) baseline_test_case_leave_generic(ctx, saved_generic);
        class_item_compile_test_info(i, &my items, is_expr ? class_decl_display_name(me) : my name, ctx);
        if (is_static) baseline_test_case_enter_type_params(ctx, class_params);
    }
    ctx->class_symbol = outer_class;
    ctx->member_container = outer_container;
    ctx->class_params = outer_class_params.data;
    ctx->class_params_len = outer_class_params.len;
    ctx->class_params_outside = outer_class_params_outside;
    baseline_test_case_leave_generic(ctx, saved_generic);
}

void type_decl_compile_test_info(TypeDecl const* me, BaselineTestCase* ctx) {
    u32 saved_generic = baseline_test_case_enter_generic(ctx, &my generic);
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        .declarations = declaration_get_spans(my symbol),
        .span = my name,
    });
    // The declared type as the new core knows it, which carries the alias name where tsc would
    baseline_test_case_add_type(
        ctx, my name,
        my symbol ? ty_declared_type_of(current_checker, my symbol) : ty_intrinsic(current_checker, TY_ANY)
    );
    generic_compile_test_info(&my generic, ctx);
    type_compile_test_info(&my type, ctx);
    baseline_test_case_leave_generic(ctx, saved_generic);
}

void interface_decl_compile_test_info(InterfaceDecl const* me, BaselineTestCase* ctx) {
    TyList params = my symbol ? ty_declared_type_of(current_checker, my symbol)->declared.type_params :
        ty_type_params_of(current_checker, &my generic);
    u32 saved_generic = baseline_test_case_enter_type_params(ctx, params);
    TyList outer_class_params = { ctx->class_params, ctx->class_params_len };
    u32 outer_class_params_outside = ctx->class_params_outside;
    ctx->class_params = params.data;
    ctx->class_params_len = params.len;
    ctx->class_params_outside = saved_generic;
    if (span_valid(my name)) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(my symbol),
            .span = my name,
        });
    }
    generic_compile_test_info(&my generic, ctx);
    for_each (i, vec_spanned_type, &my extends) {
        type_compile_test_info(&i->type, ctx);
    }
    Ty const* outer_owner = ctx->member_owner;
    ctx->member_owner = my symbol && !(my symbol->flags & SYMBOL_CLASS) ? ty_declared_type_of(current_checker, my symbol) : nullptr;
    Symbol const* outer_container = ctx->member_container;
    ctx->member_container = my symbol;
    interface_compile_test_info(&my type.interface, my name, ctx);
    ctx->member_container = outer_container;
    ctx->member_owner = outer_owner;
    ctx->class_params = outer_class_params.data;
    ctx->class_params_len = outer_class_params.len;
    ctx->class_params_outside = outer_class_params_outside;
    baseline_test_case_leave_generic(ctx, saved_generic);
}

void namespace_decl_compile_test_info(NamespaceDecl const* me, BaselineTestCase* ctx) {
    if (my is_global && span_valid(my name)) {
        // `declare global`: a namespace named `global` of its own in tsc, its value `typeof global` if it has one
        baseline_test_case_add_symbol(ctx, (BaselineSym){ .declarations = vec_span_single(my full_span), .span = my name });
        if (namespace_decl_is_instantiated(me)) {
            baseline_test_case_add_type_text(ctx, my name, c_sv("typeof global"));
        } else {
            ctx->any_keeps_underline = true;
            baseline_test_case_add_type_node(ctx, my name, &type_any);
        }
    }
    if (span_valid(my name) && my symbol) {
        // An augmentation of a file is that file's module symbol: named by its path, declared by the whole file
        // first
        SourceFile const* file = my symbol->is_module ? my symbol->module->file : nullptr;
        VecSpan decls = vec_span_init();
        cstr file_name = cstr_init();
        if (file) {
            vec_span_push(&decls, (Span){ .source = file, .sv = csview_from_n(source_file_start(file), 0) });
            file_name = module_name_for_error(my symbol, nullptr, false);
        }
        VecSpan own = declaration_get_spans(my symbol);
        for_each (i, vec_span, &own) vec_span_push(&decls, *i);
        vec_span_drop(&own);
        // An ambient module's name is its string in double quotes
        if (!file && my is_ambient_module && my module_ref) file_name = cstr_d("\"", my module_ref->path, "\"");
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = decls,
            .span = my name,
            .name = cstr_size(&file_name) ? sv_make_span(cstr_sv(&file_name)) : span_null,
        });
        cstr_drop(&file_name);
        if (my symbol->flags & SYMBOL_VALUE) baseline_test_case_add_symbol_value(ctx, my name, my symbol);
    }
    Scope const* outer = ctx->enclosing;
    if (my inner_scope) ctx->enclosing = my inner_scope;
    block_compile_test_info(&my contents, ctx);
    ctx->enclosing = outer;
}

void identifier_expr_compile_test_info(IdentifierExpr* me, BaselineTestCase* ctx) {
    if (my keyword == KEYWORD_IDENTIFIER_SUPER) {
        // The base class (target is the class `super` is in), in a static member the base constructor's type
        ClassDecl const* class_decl = my target ? symbol_find_node(my target, SYMBOL_DECL_CLASS) : nullptr;
        Ty const* base = !class_decl || class_decl->extends.tag == EXPR_IMPLICIT_ANY ? nullptr :
            my this_kind == THIS_CLASS_STATIC ? ty_expr_type(current_checker, &class_decl->extends) :
            ty_class_base_instance(current_checker, class_decl);
        if (base && base->kind == TY_REFERENCE) base = base->reference.target;
        Symbol const* symbol = base && base->kind == TY_DECLARED ? base->declared.symbol : nullptr;
        if (symbol && span_valid(symbol->name_span)) {
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = declaration_get_spans(symbol),
                .span = my name,
                .name = symbol->name_span,
            });
        }
        return;
    }
    if (my target && span_valid(my target->name_span)) {
        Symbol const* shown = shown_declaration(my symbol, my target);
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(shown),
            .span = my name,
            .text = symbol_text(ctx, shown),
        });
    }
}

// `#x in obj`: the member `#x` names, from the class that declares it
static void private_name_compile_test_info(Expr* me, BaselineTestCase* ctx) {
    Checker* checker = current_checker;
    Atom const* name = nullptr;
    Ty const* object = ty_private_name_owner(checker, me, &name);
    if (!object) return;
    PropSymbolInfo info = {0};
    collect_prop_symbol(checker, object, name, &info, 0);
    if (info.found) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = vec_span_clone(info.declarations),
            .allow_declaration_empty = true,
            .span = my inner_span,
            .text = prop_text(&info, ctx, my inner_span),
        });
    }
    vec_span_drop(&info.declarations);
}

// A namespaced JSX name `a:b` (tsc's JsxNamespacedName): its two identifiers, which are the error type
static void jsx_namespaced_name_compile_test_info(Span name, BaselineTestCase* ctx) {
    isize colon = csview_find(name.sv, ":");
    if (colon == c_NPOS) return;
    Span parts[2] = { name, name };
    parts[0].sv = csview_slice(name.sv, 0, colon);
    parts[1].sv = csview_slice(name.sv, colon + 1, name.sv.size);
    for (u32 i = 0; i < 2; i++) {
        while (parts[i].sv.size && isspace((unsigned char)parts[i].sv.buf[0])) parts[i].sv = csview_slice(parts[i].sv, 1, parts[i].sv.size);
        while (parts[i].sv.size && isspace((unsigned char)parts[i].sv.buf[parts[i].sv.size - 1])) parts[i].sv.size--;
        if (parts[i].sv.size) baseline_test_case_add_type(ctx, parts[i], ty_intrinsic(current_checker, TY_ERROR));
    }
}

// The opening or closing tag name of an element: a value tag is a reference, an intrinsic one is `any` and names a
// member of JSX.IntrinsicElements (its property, or its index signature: `__index`)
static void jsx_tag_compile_test_info(JsxElement const* element, Expr* name, BaselineTestCase* ctx) {
    if (name->tag == EXPR_IMPLICIT_ANY || !span_valid(name->inner_span) || !name->inner_span.sv.size) return;
    if (!jsx_element_is_intrinsic(element)) {
        expr_compile_test_info(name, ctx);
        return;
    }
    if (element->namespaced) {
        jsx_namespaced_name_compile_test_info(name->inner_span, ctx);
        return;
    }
    Checker* checker = current_checker;
    JsxIntrinsic intrinsic = ty_jsx_intrinsic(checker, element);
    if (intrinsic.prop) {
        PropSymbolInfo info = {0};
        collect_prop_symbol(checker, intrinsic.elements, intrinsic.prop->name, &info, 0);
        if (info.found) {
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_clone(info.declarations),
                .allow_declaration_empty = true,
                .span = name->inner_span,
                .text = prop_text(&info, ctx, name->inner_span),
            });
        }
        vec_span_drop(&info.declarations);
    } else if (intrinsic.index) {
        Symbol const* container = intrinsic.elements->kind == TY_DECLARED ? intrinsic.elements->declared.symbol :
            intrinsic.elements->alias;
        // (tsc's getApplicableIndexSymbol: the index signatures that apply, several patterns can. An index signature
        // without a declaration, of a mapped type, is IntrinsicElements itself)
        VecSpan declarations = {0};
        Members const* members = ty_members(checker, ty_apparent_type(checker, intrinsic.elements));
        Ty const* key = ty_string_literal(checker, name->identifier.name.sv);
        for (u32 i = 0; i < members->index_count; i++) {
            IndexInfo const* info = &members->index[i];
            if (info->key->flags & TYF_STRING || !ty_specific_index_applies(checker, info, key)) continue;
            if (span_valid(info->decl_span)) vec_span_push(&declarations, info->decl_span);
        }
        if (vec_span_is_empty(&declarations) && span_valid(intrinsic.index->decl_span)) {
            vec_span_push(&declarations, intrinsic.index->decl_span);
        }
        cstr text = container ? symbol_text(ctx, container) : cstr_init();
        if (!vec_span_is_empty(&declarations)) {
            if (cstr_size(&text)) cstr_append_lit(&text, ".");
            cstr_append_lit(&text, "__index");
        } else if (container) {
            declarations = symbol_declaration_spans(container);
        }
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declarations,
            .allow_declaration_empty = true,
            .span = name->inner_span,
            .text = text,
        });
    }
    // tsc checks the name as an expression: what it is as a value here (see jsx_lookup_names()), else any
    Symbol const* value = name->tag == EXPR_IDENTIFIER ? name->identifier.target : nullptr;
    ctx->any_keeps_underline = true; // (tsc's harness keeps it for JSX tag names)
    baseline_test_case_add_type(ctx, name->inner_span, value ? ty_type_of_symbol(checker, value) : ty_intrinsic(checker, TY_ANY));
}

void expr_compile_test_info_inner(Expr* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
        case EXPR_LITERAL:
            if (my literal.tag == VALUE_UNDEFINED && csview_equals(my inner_span.sv, "undefined")) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .allow_declaration_empty = true,
                    .span = my inner_span,
                });
            }
            break;

        case EXPR_IDENTIFIER:
            identifier_expr_compile_test_info(&my identifier, ctx);
            if (!my identifier.target && my identifier.is_arguments) {
                // A function's `arguments` (tsc's argumentsSymbol, which has no declaration)
                baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = my identifier.name, .allow_declaration_empty = true });
            }
            if (my identifier.keyword == KEYWORD_IDENTIFIER_NEW_TARGET) {
                // tsc shows the `target` of `new.target` too, like the name of a property access
                Span target = my identifier.name;
                target.sv = csview_from_n(span_end_ptr(target) - 6, 6);
                if (my identifier.target) {
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = symbol_declaration_spans(my identifier.target),
                        .span = target,
                        .name = my identifier.target->name_span,
                    });
                }
                baseline_test_case_add_type(ctx, target, ty_regular(current_checker, expr_get_type(me)));
            }
            break;

        case EXPR_BINARY_OP:
            expr_compile_test_info(&my binary_op->left, ctx);
            expr_compile_test_info(&my binary_op->right, ctx);
            break;

        case EXPR_UNARY_OP:
            if (my unary_op->op != UNARY_OP_YIELD || my unary_op->expr.tag != EXPR_IMPLICIT_ANY) { // Not a bare `yield`
                expr_compile_test_info(&my unary_op->expr, ctx);
            }
            break;

        case EXPR_PROPERTY: {
            bool has_compiled_expr_info = false;
            // The symbol of the property: the declarations of the property of every member of the (apparent) object
            // type that has one, like tsc's synthetic union/intersection property symbols. The parent is the first
            // member's, so a union of two interfaces gets the plain name like tsc's synthetic symbol
            Checker* checker = current_checker;
            Ty const* object = ty_expr_type(checker, &my property->expr);
            Ty const* key = ty_access_key_type(checker, my property);
            PropSymbolInfo info = {0};
            if ((key->flags & TYF_STRING_LITERAL) && !(key->flags & TYF_ENUM_LITERAL)) {
                collect_prop_symbol(checker, object, key->literal.string, &info, 0);
            }
            if (info.found) {
                if (my property->form == PROPERTY_EXPR_DOT) {
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = vec_span_clone(info.declarations),
                        .allow_declaration_empty = true,
                        .span = my inner_span,
                        .text = prop_text(&info, ctx, my property->prop.outer_span),
                    });
                }
                expr_compile_test_info(&my property->expr, ctx);
                has_compiled_expr_info = true;
                // tsc has no line for the name in a private name access (`a.#b`)
                if (my property->prop.tag == EXPR_LITERAL && !csview_starts_with(my property->prop.outer_span.sv, "#")) {
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = vec_span_clone(info.declarations),
                        .allow_declaration_empty = true,
                        .span = my property->prop.outer_span,
                        .text = prop_text(&info, ctx, my property->prop.outer_span),
                    });
                }
            }
            vec_span_drop(&info.declarations);

            bool dot = my property->form != PROPERTY_EXPR_BRACKET;
            if (!has_compiled_expr_info) {
                ctx->any_keeps_underline = dot; // The object of a property access
                expr_compile_test_info(&my property->expr, ctx);
            }

            if (!dot) {
                expr_compile_test_info(&my property->prop, ctx);
            } else if (!csview_starts_with(my property->prop.outer_span.sv, "#")) {
                ctx->any_keeps_underline = true;
                baseline_test_case_add_type(ctx, my property->prop.outer_span, ty_regular(current_checker, expr_get_type((Expr*)me)));
            }
        }   break;

        case EXPR_GENERIC_PARAMS:
            expr_compile_test_info(&my generic_params->expr, ctx);
            for_each (i, vec_spanned_type, &my generic_params->params) {
                type_compile_test_info(&i->type, ctx);
            }
            break;

        case EXPR_AS_TYPE:
            if (my as_type->angle_bracket_syntax) {
                type_compile_test_info(&my as_type->type, ctx);
            }
            expr_compile_test_info(&my as_type->expr, ctx);
            if (!my as_type->angle_bracket_syntax) {
                type_compile_test_info(&my as_type->type, ctx);
            }
            break;

        case EXPR_TERNARY:
            expr_compile_test_info(&my ternary->condition, ctx);
            expr_compile_test_info(&my ternary->yes, ctx);
            expr_compile_test_info(&my ternary->no, ctx);
            break;

        case EXPR_CALL:
            expr_compile_test_info(&my call->callable, ctx);
            for_each (i, vec_expr, &my call->args) {
                expr_compile_test_info(i, ctx);
            }
            break;

        case EXPR_FUNCTION:
            function_impl_compile_test_info(my function, ctx);
            break;

        case EXPR_OBJECT:
            for_each (i, vec_object_expr_item, &my object) {
                if (i->tag != OBJECT_ITEM_SPREAD) {
                    VecSpan declarations = {0};
                    if ((i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER) && !i->computed_key) {
                        for_each (other, vec_object_expr_item, &my object) {
                            if ((other->tag == OBJECT_ITEM_GETTER || other->tag == OBJECT_ITEM_SETTER) &&
                                !other->computed_key &&
                                cstr_eq(&other->name_string, &i->name_string)
                            ) {
                                vec_span_push(&declarations, other->span);
                            }
                        }
                    } else {
                        vec_span_push(&declarations, i->span);
                    }
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = declarations,
                        .span = i->name_span,
                        .name_is_value = i->name_is_value,
                    });
                    if (i->computed_key && i->computed_key->tag == EXPR_LITERAL) {
                        baseline_test_case_add_symbol(ctx, (BaselineSym){
                            .declarations = vec_span_single(i->span),
                            .span = i->computed_key->outer_span,
                            .name = i->name_span,
                        });
                    }
                    // (A method's type parameters are in scope at its name)
                    bool method = i->value.tag == EXPR_FUNCTION && i->tag != OBJECT_ITEM_PROPERTY && i->tag != OBJECT_ITEM_SHORTHAND;
                    u32 saved = method ? baseline_test_case_enter_generic(ctx, &i->value.function->generic) : 0;
                    baseline_test_case_add_type(ctx, i->name_span, ty_object_literal_member_type(current_checker, me, i));
                    if (method) baseline_test_case_leave_generic(ctx, saved);
                }
                if (i->computed_key) {
                    expr_compile_test_info(i->computed_key, ctx);
                }

                switch (i->tag) {
                    case OBJECT_ITEM_SHORTHAND:
                        // tsc only shows the property, not the identifier expression it's initialized from
                        if (i->shorthand_default) expr_compile_test_info(i->shorthand_default, ctx);
                        break;

                    case OBJECT_ITEM_METHOD:
                    case OBJECT_ITEM_GETTER:
                    case OBJECT_ITEM_SETTER:
                        // No line for the function as a whole, unlike a function expression
                        function_impl_compile_test_info(i->value.function, ctx);
                        break;

                    default:
                        expr_compile_test_info(&i->value, ctx);
                        break;
                }
            }
            break;

        case EXPR_ARRAY:
            for_each (i, vec_expr, &my array) {
                expr_compile_test_info(i, ctx);
            }
            break;

        case EXPR_CLASS_DECL:
            class_decl_compile_test_info(my class_decl, true, ctx);
            break;

        case EXPR_SPREAD:
            expr_compile_test_info(my spread, ctx);
            break;

        case EXPR_REGEX:
            break;

        case EXPR_PRIVATE_NAME:
            private_name_compile_test_info(me, ctx);
            break;

        case EXPR_TEMPLATE:
            if (my template_->tag) {
                expr_compile_test_info(my template_->tag, ctx);
                if (vec_expr_is_empty(&my template_->substitutions)) {
                    // Don't clone the string, and don't drop the type
                    TypeNode t = type_value(value_string(my template_->plain_string));
                    baseline_test_case_add_type_node(ctx, my template_->template_span, &t);
                } else {
                    baseline_test_case_add_type_node(ctx, my template_->template_span, &type_string);
                }
            }
            for_each (i, vec_expr, &my template_->substitutions) {
                expr_compile_test_info(i, ctx);
            }
            break;

        case EXPR_JSX_ELEMENT:
        case EXPR_JSX_FRAGMENT:
            if (my tag == EXPR_JSX_ELEMENT) jsx_tag_compile_test_info(my jsx, &my jsx->tag_name, ctx);
            for_each (i, vec_spanned_type, &my jsx->type_args) type_compile_test_info(&i->type, ctx);
            expr_compile_test_info_inner(&my jsx->attributes, ctx);
            for_each (i, vec_jsx_child, &my jsx->children) {
                if (i->tag == JSX_CHILD_EXPR || i->tag == JSX_CHILD_ELEMENT) expr_compile_test_info(&i->expr, ctx);
            }
            if (my tag == EXPR_JSX_ELEMENT && !my jsx->self_closing) jsx_tag_compile_test_info(my jsx, &my jsx->closing_name, ctx);
            break;

        case EXPR_JSX_ATTRIBUTES:
            for_each (i, vec_jsx_attribute, &my jsx_attributes) {
                if (i->tag != JSX_ATTRIBUTE_SPREAD) {
                    // An attribute declares a property of the attributes object
                    baseline_test_case_add_symbol(ctx, (BaselineSym){ .declarations = vec_span_single(i->span), .span = i->name });
                    baseline_test_case_add_type(ctx, i->name, ty_jsx_attribute_type(current_checker, me, i));
                    jsx_namespaced_name_compile_test_info(i->name, ctx);
                }
                // (tsc has no line for a string attribute's value)
                if (i->tag != JSX_ATTRIBUTE_NO_VALUE && i->tag != JSX_ATTRIBUTE_EMPTY_EXPR && i->tag != JSX_ATTRIBUTE_STRING) {
                    expr_compile_test_info(&i->value, ctx);
                }
            }
            break;
    }
}

static bool vec_expr_contains_elements_other_than_undefined(VecExpr const* me) {
    for_each (i, vec_expr, me) {
        if (i->tag != EXPR_LITERAL || i->literal.tag != VALUE_UNDEFINED) {
            return true;
        }
    }
    return false;
}

void expr_compile_test_info(Expr* me, BaselineTestCase* ctx) {
    // Without strictNullChecks `[]` and `[undefined]` are undefined[] before widening (tsc's undefinedWideningType)
    if (my tag == EXPR_ARRAY && !compiler_options()->strict_null_checks &&
        !vec_expr_contains_elements_other_than_undefined(&my array)
    ) {
        Checker* checker = current_checker;
        Ty const* undefined_array = ty_array(checker, ty_intrinsic(checker, TY_UNDEFINED), false);
        baseline_test_case_add_type_recursive(ctx, my outer_span, my inner_span, undefined_array);
    } else if (my tag == EXPR_LITERAL && my literal.tag == VALUE_NULL) {
    } else if (my tag == EXPR_GENERIC_PARAMS) {
    } else if (my tag == EXPR_IDENTIFIER && my identifier.keyword == KEYWORD_IDENTIFIER_IMPORT) {
        // The `import` of `import("m")` and `import.meta` isn't an expression of its own
    } else {
        // tsc prints an expression's regular type (getRegularTypeOfExpression)
        baseline_test_case_add_type_recursive(ctx, my outer_span, my inner_span, ty_regular(current_checker, expr_get_type(me)));
    }

    expr_compile_test_info_inner(me, ctx);
}

void statement_compile_test_info(Statement* me, BaselineTestCase* ctx) {
    if (span_valid(my jump_label)) {
        // Both labels and the targets of break and continue are shown as any
        ctx->any_keeps_underline = true;
        baseline_test_case_add_type_node(ctx, my jump_label, &type_any);
    }
    switch (my tag) {
        case STATEMENT_LABELED:
            ctx->any_keeps_underline = true;
            baseline_test_case_add_type_node(ctx, my labeled.name, &type_any);
            statement_compile_test_info(my labeled.body, ctx);
            break;

        case STATEMENT_NOOP:
        case STATEMENT_SOURCE_DIRECTIVE:
            break;

        case STATEMENT_BLOCK:
            block_compile_test_info(&my block, ctx);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
            // A name exported by `export = X` or `export default X` isn't an expression: it shows the type it declares
            // if it declares one (tsc's getTypeOfNode, isInRightSideOfImportOrExportAssignment)
            if (my tag == STATEMENT_EXPORT_DEFAULT_EXPR && my expr.tag == EXPR_IDENTIFIER && !my expr.identifier.keyword &&
                my expr.identifier.symbol
            ) {
                Symbol const* type_symbol = symbol_for_meaning(my expr.identifier.symbol, SYMBOL_TYPE);
                if (type_symbol && !type_symbol->is_placeholder &&
                    (type_symbol->flags & (SYMBOL_CLASS | SYMBOL_INTERFACE | SYMBOL_ENUM | SYMBOL_TYPE_ALIAS))
                ) {
                    Symbol const* shown = my expr.identifier.target ? my expr.identifier.target : type_symbol;
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = declaration_get_spans(shown_declaration(my expr.identifier.symbol, shown)),
                        .span = my expr.identifier.name,
                        .name = shown_declaration(my expr.identifier.symbol, shown)->name_span,
                    });
                    ctx->any_keeps_underline = true;
                    baseline_test_case_add_type(ctx, my expr.identifier.name, ty_declared_type_of(current_checker, type_symbol));
                    break;
                }
            }
            // fallthrough
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            // (A bare `return;` returns an implicit undefined literal, a written `undefined` is an expression)
            if (my expr.tag != EXPR_LITERAL || my expr.literal.tag != VALUE_UNDEFINED ||
                csview_equals(my expr.inner_span.sv, "undefined")
            ) {
                ctx->any_keeps_underline = my tag == STATEMENT_EXPORT_DEFAULT_EXPR; // Of an export assignment
                expr_compile_test_info(&my expr, ctx);
            }
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&my if_else.statements);
            int cond_size = vec_expr_size(&my if_else.conditions);
            for (int i = 0; i < statement_size; i++) {
                if (i < cond_size) {
                    expr_compile_test_info(vec_expr_at_mut(&my if_else.conditions, i), ctx);
                }
                statement_compile_test_info(vec_statement_at_mut(&my if_else.statements, i), ctx);
            }
        }   break;

        case STATEMENT_FOR_LOOP:
            if (my for_loop->is_do_while) {
                statement_compile_test_info(&my for_loop->body, ctx);
                expr_compile_test_info(&my for_loop->condition, ctx);
                break;
            }
            statement_compile_test_info(&my for_loop->initialization, ctx);
            // (`for (;;)` has a `true` condition the parser made up, with an empty span)
            if (my for_loop->condition.inner_span.sv.size) expr_compile_test_info(&my for_loop->condition, ctx);
            if (my for_loop->afterthought.tag != EXPR_LITERAL ||
                my for_loop->afterthought.literal.tag != VALUE_UNDEFINED
            ) {
                expr_compile_test_info(&my for_loop->afterthought, ctx);
            }
            statement_compile_test_info(&my for_loop->body, ctx);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    variable_statement_compile_test_info(&my for_each_loop->variable.statement, ctx);
                    break;

                case FOR_EACH_LOOP_EXPR:
                    expr_compile_test_info(&my for_each_loop->variable.expr, ctx);
                    break;
            }
            expr_compile_test_info(&my for_each_loop->iterable, ctx);
            statement_compile_test_info(&my for_each_loop->body, ctx);
            break;

        case STATEMENT_SWITCH:
            expr_compile_test_info(&my switch_->needle, ctx);
            for_each (i, vec_switch_case, &my switch_->cases) {
                if (i->is_pattern) {
                    expr_compile_test_info(&i->pattern, ctx);
                }
                for_each (j, vec_statement, &i->statements) {
                    statement_compile_test_info(j, ctx);
                }
            }
            break;

        case STATEMENT_TRY_CATCH:
            block_compile_test_info(&my try_catch->try_block, ctx);
            if (my try_catch->catch_var.tag != BINDING_NOOP) {
                binding_compile_test_info(&my try_catch->catch_var, ctx);
                block_compile_test_info(&my try_catch->catch_block, ctx);
            }
            block_compile_test_info(&my try_catch->finally_block, ctx);
            break;

        case STATEMENT_BREAK: break;
        case STATEMENT_CONTINUE: break;

        case STATEMENT_IMPORT_EXPORT:
            port_statement_compile_test_info(my import_export, ctx);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_compile_test_info(&my decl.variable, ctx);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_compile_test_info(my decl.function.impl, ctx);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_compile_test_info(my decl.enum_, ctx);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_compile_test_info(my decl.class, false, ctx);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_compile_test_info(&my decl.type, ctx);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_compile_test_info(my decl.interface, ctx);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_compile_test_info(&my decl.namespace, ctx);
            break;
    }
}

void block_compile_test_info(Block const* me, BaselineTestCase* ctx) {
    for_each (i, vec_statement, &my statements) {
        statement_compile_test_info(i, ctx);
    }
}
