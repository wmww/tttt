#include "display.h"
#include "ty/ty.h"
#include "type.h"
#include "several_types.h"
#include "callable_type.h"
#include "interface.h"
#include "interface_member.h"
#include "block.h"
#include "statement.h"
#include "expr.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "function_impl.h"
#include "port_statement.h"
#include "string_helpers.h"

#define ANSI(escape) { .code = c_sv_const_init(escape) }

AnsiColor color_none              = ANSI("");
AnsiColor color_normal            = ANSI("\x1b[0m");
AnsiColor color_error             = ANSI("\x1b[1;31m");
AnsiColor color_good              = ANSI("\x1b[1;32m");
AnsiColor color_highlighted_error = ANSI("\x1b[1;41m");
AnsiColor color_highlighted_good  = ANSI("\x1b[1;42m");

AnsiColor color_path        = ANSI("\x1b[94m");
AnsiColor color_status      = ANSI("\x1b[0;94m");
AnsiColor color_line_number = ANSI("\x1b[95m");
AnsiColor color_run_percent = ANSI("\x1b[1;44m");
AnsiColor color_implicit    = ANSI("\x1b[0;37m");

AnsiColor color_name   = ANSI("\x1b[0;35m");
AnsiColor color_value  = ANSI("\x1b[0;36m");
AnsiColor color_string = ANSI("\x1b[0;33m");
AnsiColor color_number = ANSI("\x1b[0;34m");
AnsiColor color_type   = ANSI("\x1b[0;32m");

#undef ANSI

const csview indent_string = c_sv_const_init("    ");

impl_display_no_drop(Span, span) { return debuggable_sv_display(span_sv(me), d); }

impl_display_no_drop(Ty const*, ty_d) {
    if (!current_checker) return debuggable_sv_display(c_sv("(type)"), d);
    cstr rendered = ty_to_cstr(current_checker, me);
    d = debuggable_sv_display(cstr_sv(&rendered), d);
    cstr_drop(&rendered);
    return d;
}

// The bridges that let AST nodes appear directly in cleat display sequences, see declare_ast_display()
impl_ast_display(Value, value)
impl_ast_display(InterfaceMember, interface_member)
impl_ast_display(TypeNode, type)
impl_ast_display(Expr, expr)
impl_ast_display(Statement, statement)
impl_ast_display(Block, block)

impl_display_no_drop(BriefType, brief_type) {
    Display td = display_borrow_ctx(&d);
    td.debug = true;
    td.brief = true;
    display_type(td, me.type);
    return d;
}

static void display_string_literal(Display d, csview sv) {
    display_lit(d, "\"");
    for (intptr_t i = 0; i < sv.size; i++) {
        switch (sv.buf[i]) {
            case '\n': display_lit(d, "\\n"); break;
            case '\r': display_lit(d, "\\r"); break;
            case '"': display_lit(d, "\\\""); break;
            default: display_sv(d, csview_from_n(sv.buf + i, 1)); break;
        }
    }
    display_lit(d, "\"");
}

static inline Display display_set_parsed(Display d, bool parsed) {
    if (parsed) {
        if (!d.nested) {
            d.nested = true;
            d.parsed = false;
        } else if (!d.parsed) {
            d.indent = 0;
            d.parsed = true;
        }
    } else {
        d.nested = false;
        d.parsed = false;
    }
    return d;
}

void display_value(Display d, Value const* me) {
    display_color(d, color_value);

    switch (my tag) {
        case VALUE_STRING:
            display_string_literal(d, cstr_sv(&my string));
            break;

        case VALUE_OBJECT: if (d.debug) {
            display_lit(d, "{");
            VecObjectItemRef items = js_object_sorted_items(my object.get);
            int size = vec_object_item_ref_size(&items);
            if (size > 1) {
                display_lit(d, "{");
                d.indent++;
                for_each (i, vec_object_item_ref, &items) {
                    display_newline(d);
                    display_s(d, *i->name);
                    display_lit(d, ": ");
                    display_value(d, &i->item->value);
                    display_color(d, color_value);
                    display_lit(d, ",");
                }
                d.indent--;
                display_newline(d);
            } else if (size == 1) {
                display_lit(d, " ");
                display_s(d, *vec_object_item_ref_at(&items, 0)->name);
                display_lit(d, ": ");
                display_value(d, &vec_object_item_ref_at(&items, 0)->item->value);
                display_color(d, color_value);
                display_lit(d, " ");
            }
            display_lit(d, "}");
            vec_object_item_ref_drop(&items);
            break;
        }   // Else fallthrough

        case VALUE_ARRAY: if (d.debug) {
            display_lit(d, "[");
            JsArray* value = my array.get;
            int size = vec_value_size(&value->vec);
            if (size > 1) {
                d.indent++;
                for_each (i, vec_value, &value->vec) {
                    display_newline(d);
                    display_value(d, i);
                    display_color(d, color_value);
                    display_lit(d, ",");
                }
                d.indent--;
                display_newline(d);
            } else if (size == 1) {
                display_lit(d, " ");
                display_value(d, vec_value_at(&value->vec, 0));
                display_color(d, color_value);
                display_lit(d, " ");
            }
            display_lit(d, "]");
            break;
        }   // Else fallthrough

        default:
            cstr_append_value_js(d.target, me);
            break;
    }

    display_normal(d);
}

static void display_binding_content_inner(Display d, Binding const* me);

static void display_binding_content(Display d, Binding const* me) {
    if (my rest) {
        display_lit(d, "...");
    }
    display_binding_content_inner(d, me);
    if (my default_value && d.debug) {
        display_lit(d, " = ");
        display_expr(d, my default_value);
    }
}

static void display_binding_content_inner(Display d, Binding const* me) {
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            display_colored_span(d, color_name, my content_span);
            if (d.debug) {
                if (!my symbol) {
                    display_colored(d, color_error, " /* unresolved */");
                }
            }
            break;

        case BINDING_ARRAY:
            display_lit(d, "[");
            for_each (i, vec_binding, &my array) {
                if (i_index != 0) {
                    display_lit(d, ", ");
                }
                display_binding_content(d, i);
            }
            display_lit(d, "]");
            break;

        case BINDING_OBJECT:
            display_lit(d, "{ ");
            for_each (i, vec_object_binding_item, &my object) {
                if (i_index != 0) {
                    display_lit(d, ", ");
                }
                if (i->computed_key) {
                    display_lit(d, "[");
                    display_expr(d, i->computed_key);
                    display_lit(d, "]: ");
                } else if (!i->target.rest) {
                    display_value(d, &i->source);
                    display_lit(d, ": ");
                }
                display_binding_content(d, &i->target);
            }
            display_lit(d, " }");
            break;
    }
}

static void display_binding(Display d, Binding const* me) {
    display_binding_content(d, me);
    if (my has_explicit_type) {
        display_lit(d, ": ");
        display_type(d, binding_type(me));
    }
    // If it has an implicit type that will get displayed with the binding content
}

static void display_initialized_binding(Display d, InitializedBinding const* me) {
    display_binding(d, &my binding);
    if (initialized_binding_value(me)->tag != EXPR_IMPLICIT_ANY) {
        display_lit(d, " = ");
        display_expr(d, initialized_binding_value(me));
    }
}

static void display_generic(Display d, Generic const* me) {
    if (generic_empty(me)) {
        return;
    }
    display_lit(d, "<");
    for_each (i, vec_generic_item, &my items) {
        if (i_index != 0) {
            display_lit(d, ", ");
        }
        display_colored_span(d, color_type, i->name);

        if (i->extends_explicit) {
            if (i->extends.tag == TYPE_ANY) {
                display_lit(d, " extends unknown");
            } else {
                display_lit(d, " extends ");
                display_type(d, &i->extends);
            }
        }

        if (i->fallback_explicit) {
            display_lit(d, " = ");
            display_type(d, i->fallback);
        }
    }
    display_lit(d, ">");
}

static void display_identifier_type(Display d, IdentifierType* me) {
    for_each (i, vec_span, identifier_type_namespaces(me)) {
        display_colored_span(d, color_type, *i);
        display_lit(d, ".");
    }
    if (my target && span_valid(my target->name_span)) {
        display_colored_span(d, color_type, my target->name_span);
    } else if (span_valid(my name)) {
        display_colored_span(d, color_type, my name);
    } else {
        display_colored(d, color_type, "(empty)");
    }

    if (!vec_spanned_type_is_empty(&my generic_params)) {
        display_lit(d, "<");
        for_each (i, vec_spanned_type, &my generic_params) {
            if (i_index != 0) {
                display_lit(d, ", ");
            }
            display_type(d, &i->type);
        }
        display_lit(d, ">");
    }

    if (d.debug) {
        if (!my target) {
            display_colored(d, color_error, " /* unresolved */");
        }
    }
}

static void display_inferred_type(Display d, InferredType* me) {
    if (d.debug) {
        display_lit(d, "infer ");
    }
    display_colored_span(d, color_type, my name);

    if (d.debug) {
        if (!my owned_symbol) {
            display_colored(d, color_error, " /* unresolved */");
        }
    }
}

static void display_callable(Display d, FunctionImpl const* me, bool is_interface_member_function) {
    d = display_set_parsed(d, true);
    if (my is_abstract_constructor) {
        display_lit(d, "abstract ");
    }
    if (my is_constructor) {
        display_lit(d, "new ");
    }
    display_generic(d, &my generic);
    display_lit(d, "(");
    for_each (i, vec_function_arg, &my args) {
        if (i_index != 0) {
            display_lit(d, ", ");
        }
        if (i->rest) {
            display_lit(d, "...");
        }
        display_binding_content(d, &i->initializer.binding);
        if (i->explicit_optional) {
            display_lit(d, "?");
        }
        if (i->initializer.binding.has_explicit_type) {
            display_lit(d, ": ");
            display_type(d, binding_type(&i->initializer.binding));
        }
    }
    display_lit(d, ")");

    if (is_interface_member_function) {
        display_lit(d, ": ");
    } else {
        display_lit(d, " => ");
    }

    display_type(d, &my ret);
}

void display_interface_member(Display d, InterfaceMember const* me) {
    if (my readonly) {
        display_lit(d, "readonly ");
    }

    switch (my key.tag) {
        case INTERFACE_KEY_NAME:
            display_color(d, color_type);
            display_s(d, my key.name_string);
            display_normal(d);
            break;

        case INTERFACE_KEY_INDEX:
            display_lit(d, "[");
            display_colored_span(d, color_type, my key.index->name);
            display_lit(d, ": ");
            display_type(d, &my key.index->type);
            display_lit(d, "]");
            break;

        case INTERFACE_KEY_EXPR:
            display_lit(d, "[");
            display_colored_span(d, color_type, expr_outer_span(my key.expr));
            display_lit(d, "]");
            break;

        case INTERFACE_KEY_MAPPED:
            if (my key.mapped->readonly) {
                if (my key.mapped->readonly > 0) {
                    display_lit(d, "+");
                } else {
                    display_lit(d, "-");
                }
                display_lit(d, "readonly ");
            }
            display_lit(d, "[");
            display_colored_span(d, color_type, my key.mapped->type_name);
            display_lit(d, " in ");
            display_type(d, &my key.mapped->in_type);
            if (my key.mapped->has_as_type) {
                display_lit(d, " as ");
                display_type(d, &my key.mapped->as_type);
            }
            display_lit(d, "]");
            if (my key.mapped->optional > 0) {
                display_lit(d, "+?");
            } else if (my key.mapped->optional < 0) {
                display_lit(d, "-?");
            }
            break;

        case INTERFACE_KEY_CALLABLE:
            break;
    }

    if (
        my key.tag == INTERFACE_KEY_NAME &&
        my key.name_is_member_function &&
        my type.tag == TYPE_CALLABLE
    ) {
        display_callable(d, my type.callable->function, true);
    } else if (my key.tag == INTERFACE_KEY_CALLABLE) {
        check(my type.tag == TYPE_CALLABLE);
        display_callable(d, my type.callable->function, true);
    } else {
        if (my optional) {
            display_lit(d, "?");
        }
        display_lit(d, ": ");
        display_type(d, &my type);
    }
}

// A semantic type inside the rendering of syntax
static void display_ty(Display d, Ty const* ty) {
    cstr rendered = ty_to_cstr(current_checker, ty);
    display_color(d, color_type);
    display_sv(d, cstr_sv(&rendered));
    display_normal(d);
    cstr_drop(&rendered);
}

static void display_interface(Display d, Interface const* me) {
    d = display_set_parsed(d, my parsed);
    if (!d.debug &&
        vec_interface_member_size(&my members) == 1 &&
        my members.data->key.tag == INTERFACE_KEY_CALLABLE
    ) {
        // This interface has a single callable member, so display it as if it were just a callable
        display_type(d, &my members.data->type);
        return;
    }

    if (d.brief) {
        display_lit(d, "{");
        if (vec_interface_member_size(&my members) > 0) {
            display_lit(d, " ");
            display_interface_member(d, my members.data);
            display_lit(d, "; ");
            if (vec_interface_member_size(&my members) > 1) {
                display_color(d, color_value);
                display_lit(d, "...and ");
                display_i64(d, vec_interface_member_size(&my members) - 1);
                display_lit(d, " more ");
                display_color(d, color_normal);
            }
        }
        display_lit(d, "}");
        return;
    }

    display_lit(d, "{");
    d.indent++;
    for_each (i, vec_interface_member, &my members) {
        if (i->key.tag != INTERFACE_KEY_NAME) {
            d = display_set_parsed(d, false);
            display_newline(d);
            display_interface_member(d, i);
            display_lit(d, ";");
        }
    }
    for_each (i, vec_interface_member, &my members) {
        if (i->key.tag == INTERFACE_KEY_NAME) {
            display_newline(d);
            display_interface_member(d, i);
            display_lit(d, ";");
        }
    }
    d.indent--;

    if (!vec_interface_member_is_empty(&my members)) {
        display_newline(d);
    }
    display_lit(d, "}");
}

static void display_indexed_type(Display d, IndexedType* me) {
    display_type(d, &my interface);
    display_lit(d, "[");
    display_type(d, &my index);
    display_lit(d, "]");
}

static void display_keyof_type(Display d, KeyofType* me) {
    display_lit(d, "keyof ");
    display_type(d, &my interface);
}

static void display_ternary_type(Display d, TernaryType* me) {
    display_type(d, &my maybe_sub);
    display_lit(d, " extends ");
    display_type(d, &my maybe_super);
    display_lit(d, " ? ");
    display_type(d, &my if_true);
    display_lit(d, " : ");
    display_type(d, &my if_false);
}

void display_type(Display d, TypeNode const* me) {
    switch (my tag) {
        case TYPE_ANY: display_colored(d, color_type, "any"); break;
        case TYPE_UNKNOWN: display_colored(d, color_type, "unknown"); break;
        case TYPE_NEVER: display_colored(d, color_type, "never"); break;
        case TYPE_VOID: display_colored(d, color_type, "void"); break;
        case TYPE_BOOLEAN: display_colored(d, color_type, "boolean"); break;
        case TYPE_NUMBER: display_colored(d, color_type, "number"); break;
        case TYPE_STRING: display_colored(d, color_type, "string"); break;
        case TYPE_SYMBOL: display_colored(d, color_type, "symbol"); break;
        case TYPE_UNIQUE_SYMBOL: display_colored(d, color_type, "unique symbol"); break;
        case TYPE_JSDOC: display_type(d, &my jsdoc->type); break;
        case TYPE_PREDICATE:
            if (my predicate->asserts) display_colored(d, color_type, "void");
            else display_colored(d, color_type, "boolean");
            break;
        case TYPE_BIGINT: display_colored(d, color_type, "bigint"); break;

        case TYPE_VALUE:
            display_color(d, color_type);
            Display value_d = d;
            value_d.color = false;
            display_value(value_d, &my value);
            display_normal(d);
            break;

        case TYPE_IDENTIFIER: display_identifier_type(d, my identifier); break;
        case TYPE_INFERRED: display_inferred_type(d, my inferred); break;
        case TYPE_CALLABLE: display_callable(d, my callable->function, false); break;
        case TYPE_INTERFACE: display_interface(d, &my interface); break;

        case TYPE_ARRAY: {
            bool needs_peren = false;
            switch (my array.element->tag) {
                case TYPE_CALLABLE:
                case TYPE_UNION:
                case TYPE_INTERSECTION: needs_peren = true;
                default: break;
            }
            if (needs_peren) {
                display_colored(d, color_implicit, "(");
            }
            if (my array.readonly) {
                display_lit(d, "readonly ");
            }
            display_type(d, my array.element);
            if (needs_peren) {
                display_colored(d, color_implicit, ")");
            }
            display_lit(d, "[]");
        }   break;

        case TYPE_TUPLE:
            if (my tuple.readonly) {
                display_lit(d, "readonly ");
            }
            display_lit(d, "[");
            for_each (i, vec_tuple_element, &my tuple.elements) {
                if (i_index != 0) {
                    display_lit(d, ", ");
                }

                if (i->rest) {
                    display_lit(d, "...");
                }

                if (span_valid(i->maybe_name)) {
                    display_span(d, i->maybe_name);
                    display_lit(d, ": ");
                }

                display_type(d, &i->type);

                if (i->optional) {
                    display_lit(d, "?");
                }
            }
            display_lit(d, "]");
            break;

        case TYPE_INDEXED: display_indexed_type(d, my indexed); break;
        case TYPE_KEYOF: display_keyof_type(d, my keyof); break;
        case TYPE_TERNARY: display_ternary_type(d, my ternary); break;

        case TYPE_TYPEOF:
            display_lit(d, "typeof ");
            display_expr(d, my typeof_->expr);
            break;

        case TYPE_TEMPLATE:
            display_lit(d, "`");
            for (isize_t i = 0; i < vec_cstr_size(&my template_->texts); i++) {
                if (i) {
                    display_lit(d, "${");
                    display_type(d, vec_type_at(&my template_->types, i - 1));
                    display_lit(d, "}");
                }
                display_sv(d, cstr_sv(vec_cstr_at(&my template_->texts, i)));
            }
            display_lit(d, "`");
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            d = display_set_parsed(d, false);

            if (d.debug) {
                display_colored(d, color_implicit, "(");
            }

            intptr_t count = 0;
            for_each (i, vec_type, &my composite) {
                if (!d.debug && i->tag == type_composite_base(my tag).tag) continue;

                if (count > 0) {
                    my tag == TYPE_UNION ?
                        display_lit(d, " | ") :
                        display_lit(d, " & ");
                }

                if (d.brief && count >= 2) {
                    display_color(d, color_value);
                    display_lit(d, "...and ");
                    display_i64(d, vec_type_size(&my composite) - count);
                    display_lit(d, " more");
                    display_color(d, color_normal);
                    break;
                }

                bool display_peren = i->tag == TYPE_CALLABLE;
                if (display_peren) display_lit(d, "(");
                display_type(d, i);
                if (display_peren) display_lit(d, ")");
                count++;
            }
            if (d.debug) {
                display_colored(d, color_implicit, ")");
            }
            break;
    }

    if (d.debug && type_is_self_referential(me)) {
        display_colored(d, color_error, " /* self-referential */");
    }

}

static void display_function_impl(Display d, FunctionImpl const* me, bool include_arrow) {
    display_generic(d, &my generic);
    display_lit(d, "(");
    for_each (i, vec_function_arg, &my args) {
        if (i_index != 0) {
            display_lit(d, ", ");
        }
        if (i->rest) {
            display_lit(d, "...");
        }
        display_initialized_binding(d, &i->initializer);
    }
    display_lit(d, ")");
    if (my ret_explicit) {
        display_lit(d, ": ");
        display_type(d, &my ret);
    }
    if (my has_body) {
        if (include_arrow) display_lit(d, " => "); else display_lit(d, " ");
        display_block(d, function_impl_body(me));
    } else {
        display_lit(d, ";");
    }
}

static void display_class_decl(Display d, ClassDecl const* me) {
    if (my is_abstract) {
        display_lit(d, "abstract ");
    }
    display_lit(d, "class ");
    if (span_valid(my name)) {
        display_colored_span(d, color_name, my name);
    }
    display_generic(d, &my generic);
    if (span_valid(my name) || !generic_empty(&my generic)) {
        display_lit(d, " ");
    }
    if (my extends.tag != EXPR_IMPLICIT_ANY) {
        display_lit(d, "extends ");
        display_expr(d, &my extends);
        display_lit(d, " ");
    }
    if (!vec_spanned_type_is_empty(&my implements)) {
        display_lit(d, "implements ");
        for_each (i, vec_spanned_type, &my implements) {
            display_type(d, &i->type);
            display_lit(d, ", ");
        }
    }
    display_lit(d, "{");
    d.indent++;
    for_each (i, vec_class_item, &my items) {
        display_newline(d);
        switch (i->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                display_colored(d, color_name, "constructor");
                display_function_impl(d, &i->constructor.func, false);
                break;

            case CLASS_ITEM_METHOD:
                if (i->method.is_static) {
                    display_lit(d, "static ");
                }
                if (i->method.is_abstract) {
                    display_lit(d, "abstract ");
                }
                display_color(d, color_name);
                display_s(d, i->method.name_string);
                display_normal(d);
                if (i->method.is_optional) {
                    display_lit(d, "?");
                }
                display_function_impl(d, &i->method.func, false);
                break;

            case CLASS_ITEM_FIELD:
                if (i->field.is_static) {
                    display_lit(d, "static ");
                }
                if (i->field.is_abstract) {
                    display_lit(d, "abstract ");
                }
                if (i->field.is_readonly) {
                    display_lit(d, "readonly ");
                }
                display_color(d, color_name);
                display_s(d, i->field.name_string);
                display_normal(d);
                if (i->field.is_optional) {
                    display_lit(d, "?");
                }
                if (i->field.has_explicit_type) {
                    display_lit(d, ": ");
                    display_type(d, &i->field.explicit_type);
                }
                display_lit(d, " = ");
                display_expr(d, &i->field.value);
                break;

            case CLASS_ITEM_INTERFACE_MEMBER:
                display_interface_member(d, &i->interface_memeber);
                break;

            case CLASS_ITEM_STATIC_BLOCK:
                display_lit(d, "static ");
                display_block(d, &i->static_block);
                break;
        }
    }
    d.indent--;
    display_newline(d);
    display_lit(d, "}");
}

static void display_identifier_expr(Display d, IdentifierExpr const* me) {
    display_colored_span(d, color_name, identifier_expr_name(me));
    if (d.debug) {
        if (!my target) {
            display_colored(d, color_error, " /* unresolved */");
        }
    }
}

void display_expr(Display d, Expr const* me) {
    intptr_t paren_depth = 0;
    bool display_implicit_peren = false;
    for (const char* c = span_start_ptr(expr_outer_span(me)); c < span_start_ptr(my inner_span); c++) {
        if (*c == '(') paren_depth++;
    }

    if (d.debug) {
        switch (my tag) {
            case EXPR_IMPLICIT_ANY:
            case EXPR_LITERAL:
            case EXPR_AS_TYPE:
            case EXPR_IDENTIFIER:
            case EXPR_OBJECT:
            case EXPR_ARRAY:
                break;

            default:
                if (!paren_depth) {
                    display_implicit_peren = true;
                }
                display_colored(d, color_implicit, "<");
                if (!current_checker) {
                    display_colored(d, color_implicit, "?"); // Nothing can be evaluated before there's a checker
                } else {
                    display_ty(d, ty_expr_type(current_checker, me));
                }
                display_colored(d, color_implicit, ">");
                break;
        }
    }

    for (int i = 0; i < paren_depth; i++) {
        display_lit(d, "(");
    }

    if (display_implicit_peren) {
        display_colored(d, color_implicit, "(");
    }

    switch (my tag) {
        case EXPR_IMPLICIT_ANY: display_lit(d, "/* implicit any */"); break;
        case EXPR_LITERAL: display_value(d, &my literal); break;
        case EXPR_IDENTIFIER: display_identifier_expr(d, &my identifier); break;

        case EXPR_BINARY_OP: {
            display_expr(d, &my binary_op->left);
            display_lit(d, " ");
            display_sv(d, zsview_sv(binary_op_get_zv(my binary_op->op)));
            display_lit(d, " ");
            display_expr(d, &my binary_op->right);
        }   break;

        case EXPR_UNARY_OP: {
            UnaryOp op = my unary_op->op;
            if (op < UNARY_OP_MIN_RIGHT_HAND_OP) {
                display_sv(d, zsview_sv(unary_op_get_zv(op)));
            }
            display_expr(d, &my unary_op->expr);
            if (op >= UNARY_OP_MIN_RIGHT_HAND_OP) {
                display_sv(d, zsview_sv(unary_op_get_zv(op)));
            }
        }   break;

        case EXPR_PROPERTY: {
            display_expr(d, &my property->expr);
            if (my property->optional_chain) {
                display_lit(d, "?");
            }

            switch (my property->form) {
                case PROPERTY_EXPR_DOT:
                    display_lit(d, ".");
                    display_sv(d, name_cooked(span_sv(property_name(my property))));
                    break;

                case PROPERTY_EXPR_BRACKET:
                    if (my property->optional_chain) {
                        display_lit(d, ".");
                    }
                    display_lit(d, "[");
                    display_expr(d, &my property->index);
                    display_lit(d, "]");
            }
        }   break;

        case EXPR_GENERIC_PARAMS:
            display_expr(d, &my generic_params->expr);
            display_lit(d, "<");
            for_each (i, vec_spanned_type, &my generic_params->params) {
                if (i_index != 0) {
                    display_lit(d, ", ");
                }
                display_type(d, &i->type);
            }
            display_lit(d, ">");
            break;

        case EXPR_AS_TYPE: {
            display_expr(d, &my as_type->expr);
            if (my as_type->is_satisfies) {
                display_lit(d, " satisfies ");
            } else {
                display_lit(d, " as ");
            }
            display_type(d, &my as_type->type);
        }   break;

        case EXPR_TERNARY: {
            display_expr(d, &my ternary->condition);
            display_lit(d, " ? ");
            display_expr(d, &my ternary->yes);
            display_lit(d, " : ");
            display_expr(d, &my ternary->no);
        }   break;

        case EXPR_CALL: {
            if (my call->uses_new) {
                display_lit(d, "new ");
            }
            display_expr(d, &my call->callable);
            if (my call->optional_chain) {
                display_lit(d, "?.");
            }
            if (vec_expr_is_empty(&my call->args)) {
                display_lit(d, "()");
            } else {
                display_lit(d, "(");
                d.indent++;
                for_each (i, vec_expr, &my call->args) {
                    if (i_index != 0) {
                        display_lit(d, ", ");
                    }
                    display_expr(d, i);
                }
                d.indent--;
                display_lit(d, ")");
            }
        }   break;

        case EXPR_FUNCTION:
            display_function_impl(d, my function, true);
            break;

        case EXPR_OBJECT: {
            display_lit(d, "{");
            d.indent++;
            for_each (i, vec_object_expr_item, &my object) {
                display_newline(d);
                if (i->tag == OBJECT_ITEM_SPREAD) {
                    display_lit(d, "...");
                } else {
                    if (i->tag == OBJECT_ITEM_GETTER) {
                        display_lit(d, "get ");
                    } else if (i->tag == OBJECT_ITEM_SETTER) {
                        display_lit(d, "set ");
                    }
                    if (i->computed_key) {
                        display_lit(d, "[");
                        display_expr(d, i->computed_key);
                        display_lit(d, "]");
                    } else {
                        display_s(d, i->name_string);
                    }
                    display_lit(d, ": ");
                }
                display_expr(d, &i->value);
                display_lit(d, ",");
            }
            d.indent--;
            display_newline(d);
            display_lit(d, "}");
        }   break;

        case EXPR_ARRAY: {
            display_lit(d, "[");
            d.indent++;
            for_each (i, vec_expr, &my array) {
                display_newline(d);
                display_expr(d, i);
                display_lit(d, ",");
            }
            d.indent--;
            display_newline(d);
            display_lit(d, "]");
        }   break;

        case EXPR_REGEX:
        case EXPR_PRIVATE_NAME:
        case EXPR_JSX_ELEMENT:
        case EXPR_JSX_FRAGMENT:
        case EXPR_JSX_ATTRIBUTES:
            display_sv(d, span_sv(my inner_span));
            break;

        case EXPR_TEMPLATE:
            if (my template_->tag) {
                display_expr(d, my template_->tag);
            }
            display_lit(d, "`");
            for_each (i, vec_expr, &my template_->substitutions) {
                display_lit(d, "${");
                display_expr(d, i);
                display_lit(d, "}");
            }
            display_lit(d, "`");
            break;

        case EXPR_SPREAD:
            display_lit(d, "...");
            display_expr(d, my spread);
            break;

        case EXPR_CLASS_DECL:
            display_class_decl(d, my class_decl);
            break;
    }

    for (int i = 0; i < paren_depth; i++) {
        display_lit(d, ")");
    }

    if (display_implicit_peren) {
        display_colored(d, color_implicit, ")");
    }
}

static void display_port_statement(Display d, PortStatement const* me) {
    display_span(d, my keyword_span);
    display_lit(d, " ");

    if (my main.type_only) {
        display_lit(d, "type ");
    }

    switch (my main.tag) {
        case PORT_NOOP: break;

        case PORT_IMPORT_DEFAULT:
            display_colored_span(d, color_name, my main.decl_name);
            break;

        case PORT_IMPORT_GATHER:
        case PORT_REEXPORT_GATHER:
            display_colored(d, color_value, "*");
            display_lit(d, " as ");
            display_colored_span(d, color_name, my main.decl_name);
            break;

        case PORT_REEXPORT_SPLAT:
            display_colored(d, color_value, "*");
            break;

        case PORT_EXPORT_AS_NAMESPACE:
            display_lit(d, "as namespace ");
            display_colored_span(d, color_name, my main.decl_name);
            break;

        case PORT_IMPORT_ALIAS:
            display_colored_span(d, color_name, my main.decl_name);
            display_lit(d, " = ");
            for_each (i, vec_span, &port_item_alias(&my main)->path) {
                if (i_index != 0) {
                    display_lit(d, ".");
                }
                display_colored_span(d, color_name, *i);
            }
            break;

        case PORT_IMPORT_SPECIFIC:
        case PORT_EXPORT_SPECIFIC:
        case PORT_REEXPORT_SPECIFIC:
            log_error("Unexpected main tag for port statement: ", (i32)my main.tag);
            break;
    }

    if (my main.tag != PORT_NOOP && vec_port_item_size(&my items)) {
        display_lit(d, ", ");
    }

    if (vec_port_item_size(&my items)) {
        display_lit(d, "{");
        for_each (i, vec_port_item, &my items) {
            if (i_index != 0) {
                display_lit(d, ", ");
            }
            if (i->type_only) {
                display_lit(d, "type ");
            }
            display_colored_span(d, color_name, i->source_name);
            if (span_cmp(i->decl_name, i->source_name) != 0) {
                display_lit(d, " as ");
                display_colored_span(d, color_name, i->decl_name);
            }
        }
        display_lit(d, "}");
    }

    if (my requires_target) {
        display_lit(d, " from ");
        display_color(d, color_string);
        display_string_literal(d, port_statement_target_path(me));
        display_normal(d);
    }

    display_lit(d, ";");
}

static void display_variable_statement(Display d, VariableStatement const* me) {
    switch (my keyword) {
        case VAR_INIT_VAR: display_lit(d, "var "); break;
        case VAR_INIT_LET: display_lit(d, "let "); break;
        case VAR_INIT_CONST: display_lit(d, "const "); break;
        case VAR_INIT_USING: display_lit(d, "using "); break;
        case VAR_INIT_AWAIT_USING: display_lit(d, "await using "); break;
    }
    for_each (i, vec_initialized_binding, &my initializers) {
        if (i_index != 0) {
            display_lit(d, ", ");
        }
        display_initialized_binding(d, i);
    }
}

static void display_enum_decl(Display d, EnumDecl const* me) {
    display_lit(d, "enum ");
    display_colored_span(d, color_name, my name);
    display_lit(d, " {");
    d.indent++;
    for_each (i, vec_enum_case, &my cases) {
        display_newline(d);
        display_colored_span(d, color_name, i->name);
        if (i->value.tag != EXPR_IMPLICIT_ANY) {
            display_lit(d, " = ");
            display_expr(d, &i->value);
        }
        display_lit(d, ",");
    }
    d.indent--;
    display_newline(d);
    display_lit(d, "}");
}

static void display_type_decl(Display d, TypeDecl const* me) {
    display_lit(d, "type ");
    display_colored_span(d, color_name, my name);
    display_generic(d, &my generic);
    display_lit(d, " = ");
    display_type(d, &my type);
}

static void display_interface_decl(Display d, InterfaceDecl const* me) {
    display_lit(d, "interface ");
    display_colored_span(d, color_name, my name);
    display_generic(d, &my generic);
    if (!vec_spanned_type_is_empty(&my extends)) {
        display_lit(d, " extends ");
        for_each (i, vec_spanned_type, &my extends) {
            if (i_index != 0) {
                display_lit(d, ", ");
            }
            display_type(d, &i->type);
        }
    }
    display_lit(d, " ");
    display_type(d, &my type);
}

void display_statement(Display d, Statement const* me) {
    switch (statement_get_exported(me)) {
        case DECL_EXPORTED_NO: break;
        case DECL_EXPORTED_YES: display_lit(d, "export "); break;
        case DECL_EXPORTED_YES_DEFAULT: display_lit(d, "export default "); break;
    }

    if (statement_tag_is_decl(my tag) && my decl.props.declared) {
        display_lit(d, "declare ");
    }

    switch (my tag) {
        case STATEMENT_NOOP: display_lit(d, ";"); break;

        case STATEMENT_SOURCE_DIRECTIVE:
            display_lit(d, "/// <");
            switch (my source_directive->tag) {
                case SOURCE_DIRECTIVE_REFERENCE_PATH:
                case SOURCE_DIRECTIVE_REFERENCE_LIB:
                case SOURCE_DIRECTIVE_REFERENCE_TYPES:
                    display_lit(d, "reference ");
                    break;
            }
            display_lit(d, "path=");
            display_string_literal(d, cstr_sv(&my source_directive->path));
            if (my source_directive->preserve) {
                display_lit(d, " preserve=\"true\"");
            }
            display_lit(d, " />");
            break;

        case STATEMENT_BLOCK: display_block(d, &my block); break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&my if_else.statements);
            int cond_size = vec_expr_size(&my if_else.conditions);
            for (int i = 0; i < statement_size; i++) {
                if (i < cond_size) {
                    display_lit(d, "if (");
                    display_expr(d, vec_expr_at(&my if_else.conditions, i));
                    display_lit(d, ") ");
                }
                display_statement(d, vec_statement_at(&my if_else.statements, i));
                if (i < statement_size - 1) {
                    display_lit(d, " else ");
                }
            }
        }   break;

        case STATEMENT_FOR_LOOP:
            display_lit(d, "for (");
            display_statement(d, &my for_loop->initialization);
            if (cstr_str(d.target)[cstr_size(d.target) - 1] != ';') {
                display_lit(d, ";");
            }
            display_lit(d, " ");
            display_expr(d, &my for_loop->condition);
            display_lit(d, "; ");
            display_expr(d, &my for_loop->afterthought);
            display_lit(d, ") ");
            display_statement(d, &my for_loop->body);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            display_lit(d, "for (");
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    display_variable_statement(d, &my for_each_loop->variable.statement);
                    break;

                case FOR_EACH_LOOP_EXPR:
                    display_expr(d, &my for_each_loop->variable.expr);
                    break;
            }
            display_lit(d, " ");
            display_sv(d, zsview_sv(for_each_loop_statement_keyword_str(my for_each_loop)));
            display_lit(d, " ");
            display_expr(d, &my for_each_loop->iterable);
            display_lit(d, ") ");
            display_statement(d, &my for_each_loop->body);
            break;

        case STATEMENT_SWITCH:
            display_lit(d, "switch (");
            display_expr(d, &my switch_->needle);
            display_lit(d, ") {");
            d.indent++;
            for_each (i, vec_switch_case, &my switch_->cases) {
                display_newline(d);
                if (i->is_pattern) {
                    display_lit(d, "case ");
                    display_expr(d, &i->pattern);
                    display_lit(d, ":");
                } else {
                    display_lit(d, "default:");
                }
                d.indent++;
                for_each (j, vec_statement, &i->statements) {
                    display_newline(d);
                    display_statement(d, j);
                }
                d.indent--;
            }
            d.indent--;
            display_newline(d);
            display_lit(d, "}");
            break;

        case STATEMENT_TRY_CATCH:
            display_lit(d, "try ");
            display_block(d, &my try_catch->try_block);
            if (my try_catch->catch_var.tag != BINDING_NOOP ||
                !vec_statement_is_empty(&my try_catch->catch_block.statements)
            ) {
                display_lit(d, " catch ");
                if (my try_catch->catch_var.tag != BINDING_NOOP) {
                    display_lit(d, "(");
                    display_binding(d, &my try_catch->catch_var);
                    display_lit(d, ") ");
                }
                display_block(d, &my try_catch->catch_block);
            }
            if (!vec_statement_is_empty(&my try_catch->finally_block.statements)) {
                display_lit(d, " finally ");
                display_block(d, &my try_catch->finally_block);
            }
            break;

        case STATEMENT_LABELED:
            display_span(d, my labeled.name);
            display_lit(d, ": ");
            display_statement(d, my labeled.body);
            break;

        case STATEMENT_WITH:
            display_lit(d, "with (");
            display_expr(d, &my with->object);
            display_lit(d, ") ");
            display_statement(d, &my with->body);
            break;

        case STATEMENT_BREAK: display_lit(d, "break;"); break;
        case STATEMENT_CONTINUE: display_lit(d, "continue;"); break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
            display_expr(d, &my expr);
            display_lit(d, ";");
            break;

        case STATEMENT_RETURN_EXPR:
            display_lit(d, "return ");
            display_expr(d, &my expr);
            display_lit(d, ";");
            break;

        case STATEMENT_THROW_EXPR:
            display_lit(d, "throw ");
            display_expr(d, &my expr);
            display_lit(d, ";");
            break;

        case STATEMENT_IMPORT_EXPORT:
            display_port_statement(d, my import_export);
            break;

        case STATEMENT_DECL_VARIABLE:
            display_variable_statement(d, &my decl.variable);
            display_lit(d, ";");
            break;

        case STATEMENT_DECL_FUNCTION:
            display_lit(d, "function ");
            display_colored_span(d, color_name, my decl.function.impl->name);
            display_function_impl(d, my decl.function.impl, false);
            break;

        case STATEMENT_DECL_ENUM:
            display_enum_decl(d, my decl.enum_);
            break;

        case STATEMENT_DECL_CLASS: display_class_decl(d, my decl.class); break;
        case STATEMENT_DECL_TYPE: display_type_decl(d, my decl.type); break;
        case STATEMENT_DECL_INTERFACE: display_interface_decl(d, my decl.interface); break;

        case STATEMENT_DECL_NAMESPACE:
            display_lit(d, "namespace ");
            display_colored_span(d, color_name, my decl.namespace->name);
            display_lit(d, " ");
            display_block(d, &my decl.namespace->contents);
            break;
    }
}

void display_block(Display d, Block const* me) {
    display_lit(d, "{");
    d.indent++;
    for_each (i, vec_statement, &my statements) {
        display_newline(d);
        display_statement(d, i);
    }
    d.indent--;
    display_newline(d);
    display_lit(d, "}");
}

void print_type_dbg(TypeNode const* me) {
    eprintln("Type ", (void*)me, ": ", debug_d(me));
}
