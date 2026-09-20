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

// What the symbols baseline shows for a reference: the declarations of the symbol the name found (an alias shows its
// import, not what it resolves to), or of the declaration when there is no symbol (keywords, synthetic nodes)
static inline Symbol const* shown_declaration(Symbol const* symbol, Symbol const* decl) {
    return symbol && symbol->decl_count ? symbol : decl;
}

static inline VecSpan declaration_get_spans(Symbol const* me) {
    return symbol_declaration_spans(me);
}

// `A.B.C` as spans
static void push_symbol_path(VecSpan* out, Symbol const* symbol) {
    if (symbol->parent && !symbol->parent->is_module) push_symbol_path(out, symbol->parent);
    vec_span_push(out, symbol->name_span);
}

void generic_compile_test_info(Generic const* me, BaselineTestCase* ctx) {
    for_each (i, vec_generic_item, &my items) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = i->name });
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
    if (my has_explicit_type) {
        type_compile_test_info(&my explicit_type, ctx);
    }
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

void interface_member_compile_test_info(InterfaceMember const* me, Span scope, BaselineTestCase* ctx) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME:
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_clone(my declarations),
                .namespaces = vec_span_single(scope),
                .span = my key.span,
                .name_is_value = my key.name_is_value,
            });
            baseline_test_case_add_type_node(ctx, my key.span, &my type);
            break;

        case INTERFACE_KEY_INDEX:
            baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = my key.index.name });
            baseline_test_case_add_type_node(ctx, my key.index.name, &my key.index.type);
            type_compile_test_info(&my key.index.type, ctx);
            break;

        case INTERFACE_KEY_EXPR:
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_single(my key.span),
                .namespaces = vec_span_single(scope),
                .span = my key.span,
                .name = my key.expr.ref->outer_span,
                .name_is_value = true,
            });
            baseline_test_case_add_type_node(ctx, my key.span, &my type);
            expr_compile_test_info(my key.expr.ref, ctx);
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

void type_compile_test_info(TypeNode const* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
            break;

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
            if (my identifier->target && !span_valid(my identifier->target->name_span)) {
                break;
            }
            VecSpan so_far = vec_span_init();
            for_each (i, vec_span, &my identifier->namespaces) {
                intptr_t index = i_index;
                Symbol const* decl = index < vec_symbol_cptr_size(&my identifier->namespace_symbols) ?
                    *vec_symbol_cptr_at(&my identifier->namespace_symbols, index) :
                    nullptr;
                baseline_test_case_add_type_node(ctx, *i, &type_any);
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = declaration_get_spans(decl),
                    .allow_declaration_empty = true,
                    .namespaces = vec_span_clone(so_far),
                    .span = *i,
                });
                vec_span_push(&so_far, *i);
            }
            vec_span_drop(&so_far);
            if (!span_valid(my identifier->name)) break; // A bare `import("./a")`
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .namespaces = vec_span_clone(my identifier->namespaces),
                .declarations = declaration_get_spans(shown_declaration(my identifier->symbol, my identifier->target)),
                .allow_declaration_empty = true,
                .span = my identifier->name,
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

        case TYPE_CALLABLE:
            generic_compile_test_info(&my callable->generic, ctx);
            for_each (i, vec_callable_arg, &my callable->args) {
                binding_content_compile_test_info(&i->source->initializer.binding, ctx);
                if (i->source->initializer.binding.has_explicit_type) {
                    type_compile_test_info(&i->type, ctx);
                }
            }
            type_compile_test_info(&my callable->ret, ctx);
            break;

        case TYPE_INTERFACE: interface_compile_test_info(&my interface, span_null, ctx); break;

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

        case TYPE_LAZY: break; // Not syntax

        case TYPE_REF:
            type_compile_test_info(my ref, ctx);
            break;

    }
}

void function_impl_compile_test_info(FunctionImpl* me, BaselineTestCase* ctx) {
    if (span_valid(my name)) {
        baseline_test_case_add_declaration(ctx, my name, my symbol);
    }
    generic_compile_test_info(&my generic, ctx);
    for_each (i, vec_function_arg, &my args) {
        for_each (decorator, vec_expr, &i->decorators) {
            expr_compile_test_info(decorator, ctx);
        }
        initialized_binding_compile_test_info(&i->initializer, ctx);
    }
    if (my ret_explicit && !my is_constructor) {
        type_compile_test_info(&my ret, ctx);
    }
    block_compile_test_info(&my body, ctx);
}

// An alias shows its own declaration as the symbol and the value of what it resolves to as the type
static void port_item_add_alias(PortItem const* me, BaselineTestCase* ctx) {
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        .declarations = symbol_declaration_spans(my symbol),
        .span = my decl_name,
        .name = my decl_name,
    });
    // tsc shows aliases of pure types as any
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
                baseline_test_case_add_type_node(ctx, my main.decl_name, &type_any);
            } else {
                port_item_add_alias(&my main, ctx);
            }
            for_each (i, vec_span, &my main.alias_path) {
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
                    .name = decl->name_span,
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
        if (i->decl_name.sv.buf != i->source_name.sv.buf) {
            if (i->source_symbol) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = symbol_declaration_spans(i->source_symbol),
                    .span = i->source_name,
                });
            }
            baseline_test_case_add_symbol_value(ctx, i->source_name, i->symbol);
        }
        port_item_add_alias(i, ctx);
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
            .namespaces = vec_span_single(my name),
            .declarations = symbol_declaration_spans(declaration),
            .span = span,
            .name = i->name_is_value ? i->full_name : declaration->name_span,
            .name_is_value = i->name_is_value,
        });
        baseline_test_case_add_symbol_value(ctx, span, declaration);
        if (i->value.tag != EXPR_IMPLICIT_ANY) {
            expr_compile_test_info(&i->value, ctx);
        }
    }
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
            VecSpan declarations = {0};
            if (my method.accessor && my method.accessor_first) {
                // Both halves of an accessor pair declare the same property. Items live in one vec, so walk forward
                // from the first
                ClassItem* first = my method.accessor_first;
                vec_span_push(&declarations, first->span);
                for_each (other, vec_class_item, items) {
                    if (other != first && other->tag == CLASS_ITEM_METHOD && other->method.accessor_first == first) {
                        vec_span_push(&declarations, other->span);
                    }
                }
            } else {
                vec_span_push(&declarations, my span);
            }
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = declarations,
                .namespaces = vec_span_single(class_name),
                .span = my method.name_span,
                .name_is_value = my method.name_is_value,
            });
            TypeNode t = class_item_get_type(me);
            baseline_test_case_add_type_node(ctx, my method.name_span, &t);
            type_drop(&t);
            function_impl_compile_test_info(&my method.func, ctx);
        }   break;

        case CLASS_ITEM_FIELD: {
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_single(my span),
                .namespaces = vec_span_single(class_name),
                .span = my field.name_span,
                .name_is_value = my field.name_is_value,
            });
            TypeNode t = class_item_get_type(me);
            baseline_test_case_add_type_node(ctx, my field.name_span, &t);
            type_drop(&t);

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
    if (span_valid(my name)) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(my symbol),
            .span = my name,
        });
        if (is_expr) {
            baseline_test_case_add_symbol_value(ctx, my name, my symbol);
        } else {
            baseline_test_case_add_type_node(ctx, my name, &my instance_identifier_type);
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
    for_each (i, vec_class_item, &my items) {
        class_item_compile_test_info(i, &my items, my name, ctx);
    }
}

void type_decl_compile_test_info(TypeDecl const* me, BaselineTestCase* ctx) {
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
}

void interface_decl_compile_test_info(InterfaceDecl const* me, BaselineTestCase* ctx) {
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
    interface_compile_test_info(&my type.interface, my name, ctx);
}

void namespace_decl_compile_test_info(NamespaceDecl const* me, BaselineTestCase* ctx) {
    if (span_valid(my name) && my symbol) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(my symbol),
            .span = my name,
        });
        if (my symbol->flags & SYMBOL_VALUE) baseline_test_case_add_symbol_value(ctx, my name, my symbol);
    }
    Scope const* outer = ctx->enclosing;
    if (my inner_scope) ctx->enclosing = my inner_scope;
    block_compile_test_info(&my contents, ctx);
    ctx->enclosing = outer;
}

void identifier_expr_compile_test_info(IdentifierExpr* me, BaselineTestCase* ctx) {
    if (my target && span_valid(my target->name_span)) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(shown_declaration(my symbol, my target)),
            .span = my name,
            .name = shown_declaration(my symbol, my target)->name_span,
        });
    }
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
            // The symbol of the property, if the object's type has one by that name
            Checker* checker = current_checker;
            Ty const* object = ty_expr_type(checker, &my property->expr);
            Ty const* key = ty_expr_type(checker, &my property->prop);
            if (object->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) {
                Ty const* constraint = ty_base_constraint(checker, object);
                if (constraint) object = constraint;
            }
            Prop const* prop = (object->flags & TYF_OBJECT) && (key->flags & TYF_STRING_LITERAL) && !(key->flags & TYF_ENUM_LITERAL) ?
                ty_get_prop(checker, object, key->literal.string) :
                nullptr;
            if (prop) {
                Prop const* root = prop;
                while (root->target && !root->decl_span_count) root = root->target;
                VecSpan declarations = vec_span_init();
                for (u32 j = 0; j < root->decl_span_count; j++) vec_span_push(&declarations, root->decl_spans[j]);
                VecSpan namespaces = vec_span_init();
                if (prop->parent) push_symbol_path(&namespaces, prop->parent);
                if (my property->form == PROPERTY_EXPR_DOT) {
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = vec_span_clone(declarations),
                        .allow_declaration_empty = true,
                        .namespaces = vec_span_clone(namespaces),
                        .span = my outer_span,
                        .name = span_valid(root->name_span) ? root->name_span : my property->prop.outer_span,
                        .name_is_value = root->name_is_value,
                    });
                }
                expr_compile_test_info(&my property->expr, ctx);
                has_compiled_expr_info = true;
                // tsc has no line for the name in a private name access (`a.#b`)
                if (my property->prop.tag == EXPR_LITERAL && !csview_starts_with(my property->prop.outer_span.sv, "#")) {
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = vec_span_clone(declarations),
                        .allow_declaration_empty = true,
                        .namespaces = vec_span_clone(namespaces),
                        .span = my property->prop.outer_span,
                        .name = root->name_span,
                        .name_is_value = root->name_is_value,
                    });
                }
                vec_span_drop(&namespaces);
                vec_span_drop(&declarations);
            }

            if (!has_compiled_expr_info) {
                expr_compile_test_info(&my property->expr, ctx);
            }

            if (my property->form == PROPERTY_EXPR_BRACKET) {
                expr_compile_test_info(&my property->prop, ctx);
            } else if (!csview_starts_with(my property->prop.outer_span.sv, "#")) {
                baseline_test_case_add_type(ctx, my property->prop.outer_span, expr_get_type((Expr*)me));
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
                    baseline_test_case_add_type(ctx, i->name_span, ty_object_literal_member_type(current_checker, me, i));
                }
                if (i->computed_key) {
                    expr_compile_test_info(i->computed_key, ctx);
                }

                switch (i->tag) {
                    case OBJECT_ITEM_SHORTHAND:
                        // tsc only shows the property, not the identifier expression it's initialized from
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
    if (my tag == EXPR_IDENTIFIER && my identifier.target && !my identifier.keyword) {
        // Do NOT degenericize
        baseline_test_case_add_type_recursive(
            ctx,
            my outer_span,
            my inner_span,
            ty_identifier_reference_type(current_checker, &my identifier)
        );
    } else if (my tag == EXPR_ARRAY && !vec_expr_contains_elements_other_than_undefined(&my array)) {
        Checker* checker = current_checker;
        Ty const* undefined_array = ty_array(checker, ty_intrinsic(checker, TY_UNDEFINED), false);
        baseline_test_case_add_type_recursive(ctx, my outer_span, my inner_span, undefined_array);
    } else if (my tag == EXPR_LITERAL && my literal.tag == VALUE_NULL) {
    } else if (my tag == EXPR_GENERIC_PARAMS) {
    } else {
        baseline_test_case_add_type_recursive(ctx, my outer_span, my inner_span, expr_get_type(me));
    }

    expr_compile_test_info_inner(me, ctx);
}

void statement_compile_test_info(Statement* me, BaselineTestCase* ctx) {
    if (span_valid(my jump_label)) {
        // Both labels and the targets of break and continue are shown as any
        baseline_test_case_add_type_node(ctx, my jump_label, &type_any);
    }
    switch (my tag) {
        case STATEMENT_LABELED:
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
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            if (my expr.tag != EXPR_LITERAL || my expr.literal.tag != VALUE_UNDEFINED) {
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
            expr_compile_test_info(&my for_loop->condition, ctx);
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
