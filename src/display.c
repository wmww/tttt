#include "display.h"
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
#include "import_statement.h"
#include "string_helpers.h"

#define has_underline(d) (d.flags & DISPLAY_FLAGS_UNDERLINE_TYPES)

const char* color_normal = "\x1b[0m";
const char* color_error = "\x1b[1;31m";
const char* color_good = "\x1b[1;32m";
const char* color_highlighted_error = "\x1b[1;41m";
const char* color_highlighted_good = "\x1b[1;42m";

const char* color_path = "\x1b[94m";
const char* color_status = "\x1b[0;94m";
const char* color_line_number = "\x1b[95m";
const char* color_run_percent = "\x1b[1;44m";
const char* color_implicit = "\x1b[0;37m";

const char* color_name = "\x1b[0;35m";
const char* color_value = "\x1b[0;36m";
const char* color_string = "\x1b[0;33m";
const char* color_number = "\x1b[0;34m";
const char* color_type = "\x1b[0;32m";

const csview indent_string = c_sv_const_init("    ");

void display_fmt(Display d, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    cstr_vfmt(d.target, cstr_size(d.target), fmt, args);
    va_end(args);
}

void display_value(Display d, Value const* me) {
    display_color(d, color_value);

    switch (my tag) {
        case VALUE_STRING:
            display_lit(d, "\"");
            const char* buf = cstr_str(&my string);
            intptr_t len = cstr_size(&my string);
            for (intptr_t i = 0; i < len; i++) {
                switch (buf[i]) {
                    case '\n': display_lit(d, "\\n"); break;
                    case '\r': display_lit(d, "\\r"); break;
                    case '"': display_lit(d, "\\\""); break;
                    default: display_sv(d, csview_from_n(buf + i, 1)); break;
                }
            }
            display_lit(d, "\"");
            break;

        case VALUE_OBJECT: if (d.debug) {
            display_lit(d, "{");
            VecObjectItemRef items = js_object_sorted_items(my object.get);
            int size = vec_object_item_ref_size(&items);
            if (size > 1) {
                display_lit(d, "{");
                d.indent++;
                c_foreach(i, vec_object_item_ref, items) {
                    display_newline(d);
                    display_s(d, *i.ref->name);
                    display_lit(d, ": ");
                    display_value(d, &i.ref->item->value);
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
                c_foreach(i, vec_value, value->vec) {
                    display_newline(d);
                    display_value(d, i.ref);
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

static void display_binding_content(Display d, Binding const* me) {
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            display_colored_span(d, color_name, my content_span);
            if (d.debug) {
                if (!my declaration.get) {
                    display_colored(d, color_error, " /* unresolved */");
                } else if (!my has_explicit_type) {
                    display_colored(d, color_implicit, " /* ");
                    display_type(d, &my declaration.get->declared_type);
                    display_colored(d, color_implicit, " */");
                }
            }
            break;

        case BINDING_ARRAY:
            display_lit(d, "[");
            c_foreach(i, vec_binding, my array) {
                display_binding_content(d, i.ref);
                if (i.ref != i.end - 1) {
                    display_lit(d, ", ");
                }
            }
            display_lit(d, "]");
            break;

        case BINDING_OBJECT:
            display_lit(d, "{ ");
            c_foreach(i, vec_object_binding_item, my object) {
                display_value(d, &i.ref->source);
                display_lit(d, ": ");
                display_binding_content(d, &i.ref->target);
                if (i.ref != i.end - 1) {
                    display_lit(d, ", ");
                }
            }
            display_lit(d, " }");
            break;
    }
}

static void display_binding(Display d, Binding const* me) {
    display_binding_content(d, me);
    if (my has_explicit_type) {
        display_lit(d, ": ");
        display_type(d, binding_get_effective_type(me));
    }
    // If it has an implicit type that will get displayed with the binding content
}

static void display_initialized_binding(Display d, InitializedBinding const* me) {
    display_binding(d, &my binding);
    if (my value.tag != EXPR_IMPLICIT_ANY) {
        display_lit(d, " = ");
        display_expr(d, &my value);
    }
}

static void display_generic(Display d, Generic const* me) {
    if (generic_empty(me)) {
        return;
    }
    display_lit(d, "<");
    c_foreach(i, vec_generic_item, my items) {
        display_colored_span(d, color_type, i.ref->name);

        if (i.ref->extends_explicit) {
            if (i.ref->extends.tag == TYPE_ANY) {
                display_lit(d, " extends unknown");
            } else {
                display_lit(d, " extends ");
                display_type(d, &i.ref->extends);
            }
        }

        if (i.ref->fallback_explicit) {
            display_lit(d, " = ");
            display_type(d, &i.ref->fallback);
        }
        if (i.ref < i.end - 1) {
            display_lit(d, ", ");
        }
    }
    display_lit(d, ">");
}

static void display_identifier_type(Display d, IdentifierType* me) {
    if (d.hash_key && my decl_ref && vec_spanned_type_empty(&my generic_params)) {
        if (hset_declaration_cptr_insert(&d.hash_key_target->declarations, my decl_ref).inserted) {
            display_type(d, &my decl_ref->declared_type);
            hset_declaration_cptr_erase(&d.hash_key_target->declarations, my decl_ref);
            return;
        }
    }

    if (my decl_ref && my decl_ref->is_typeof) {
        display_lit(d, "typeof ");
    }

    if (!d.hash_key || d.debug) {
        c_foreach(i, vec_span, my namespaces) {
            display_colored_span(d, color_type, *i.ref);
            display_lit(d, ".");
        }
        if (my decl_ref && span_valid(my decl_ref->name)) {
            display_colored_span(d, color_type, my decl_ref->name);
        } else if (span_valid(my name)) {
            display_colored_span(d, color_type, my name);
        } else {
            display_colored(d, color_type, "(empty)");
        }
    }

    if (d.hash_key) {
        display_fmt(d, "@%p", (void*)my decl_ref);
    }

    if (!vec_spanned_type_empty(&my generic_params)) {
        display_lit(d, "<");
        c_foreach(i, vec_spanned_type, my generic_params) {
            display_type(d, &i.ref->type);
            if (i.ref != i.end - 1) {
                display_lit(d, ", ");
            }
        }
        display_lit(d, ">");
    }

    if (d.debug) {
        if (!my decl_ref) {
            display_colored(d, color_error, " /* unresolved */");
        }
    }
}

static void display_inferred_type(Display d, InferredType* me) {
    if (d.debug) {
        display_lit(d, "infer ");
    }
    display_colored_span(d, color_type, my name);

    if (d.hash_key) {
        display_fmt(d, "@%p", (void*)my declaration_ref);
    }

    if (d.debug) {
        if (!my declaration_ref) {
            display_colored(d, color_error, " /* unresolved */");
        }
    }
}

static void display_callable(Display d, CallableType* me, bool is_interface_member_function) {
    if (my is_abstract_constructor) {
        display_lit(d, "abstract ");
    }
    if (my is_constructor) {
        display_lit(d, "new ");
    }
    display_generic(d, &my generic);
    display_lit(d, "(");
    c_foreach(i, vec_callable_arg, my args) {
        if (i.ref->source->rest) {
            display_lit(d, "...");
        }
        display_binding_content(d, &i.ref->source->initializer.binding);
        if (i.ref->optional) {
            display_lit(d, "?");
        }
        display_lit(d, ": ");
        display_type(d, &i.ref->type);
        if (i.ref != i.end - 1) {
            display_lit(d, ", ");
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

static void display_interface_member(Display d, InterfaceMember const* me) {
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
            display_colored_span(d, color_type, my key.index.name);
            display_lit(d, ": ");
            display_type(d, &my key.index.type);
            display_lit(d, "]");
            break;

        case INTERFACE_KEY_EXPR:
            display_lit(d, "[");
            display_colored_span(d, color_type, my key.expr.ref->outer_span);
            display_lit(d, "]");
            break;

        case INTERFACE_KEY_MAPPED:
            if (my key.mapped.readonly) {
                if (my key.mapped.readonly > 0) {
                    display_lit(d, "+");
                } else {
                    display_lit(d, "-");
                }
                display_lit(d, "readonly ");
            }
            display_lit(d, "[");
            display_colored_span(d, color_type, my key.mapped.type_name);
            display_lit(d, " in ");
            display_type(d, &my key.mapped.in_type);
            if (my key.mapped.has_as_type) {
                display_lit(d, " as ");
                display_type(d, &my key.mapped.as_type);
            }
            display_lit(d, "]");
            if (my key.mapped.optional > 0) {
                display_lit(d, "+?");
            } else if (my key.mapped.optional < 0) {
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
        display_callable(d, my type.callable, true);
    } else if (my key.tag == INTERFACE_KEY_CALLABLE) {
        ASSERT(my type.tag == TYPE_CALLABLE);
        display_callable(d, my type.callable, true);
    } else {
        if (my optional) {
            display_lit(d, "?");
        }
        display_lit(d, ": ");
        display_type(d, &my type);
    }
}

static void display_interface(Display d, Interface const* me) {
    if (!d.debug &&
        vec_interface_member_size(&my members) == 1 &&
        my members.data->key.tag == INTERFACE_KEY_CALLABLE
    ) {
        // This interface has a single callable member, so display it as if it were just a callable
        display_type(d, &my members.data->type);
        return;
    }

    display_lit(d, "{");
    d.indent++;
    c_foreach(i, vec_interface_member, my members) {
        if (i.ref->key.tag != INTERFACE_KEY_NAME) {
            display_newline(d);
            display_interface_member(d, i.ref);
            display_lit(d, ";");
        }
    }
    c_foreach(i, vec_interface_member, my members) {
        if (i.ref->key.tag == INTERFACE_KEY_NAME) {
            display_newline(d);
            display_interface_member(d, i.ref);
            display_lit(d, ";");
        }
    }
    d.indent--;

    if (!vec_interface_member_empty(&my members)) {
        display_newline(d);
    }
    display_lit(d, "}");
}

static void display_indexed_type(Display d, IndexedType* me) {
    if (my is_from_binding) {
        Type t = type_indexed(me);
        display_type(d, type_resolve_one_layer(&t));
        // Didn't allocate, don't drop
    } else {
        display_type(d, &my interface);
        display_lit(d, "[");
        display_type(d, &my index);
        display_lit(d, "]");
    }
}

static void display_keyof_type(Display d, KeyofType* me) {
    display_lit(d, "keyof ");
    display_type(d, &my interface);
}

static void display_ternary_type(Display d, TernaryType* me) {
    display_type(d, &my maybe_sub);
    display_lit(d, " extends ");
    display_type(d, &my maybe_super);
    d.indent++;
    display_newline(d);
    display_lit(d, "? ");
    display_type(d, &my if_true);
    display_newline(d);
    display_lit(d, ": ");
    display_type(d, &my if_false);
}

void display_type(Display d, Type const* me) {
    if (d.hash_key) {
        // PERF: use an entirely different type serialization system for hashes that's faster and more compact
        display_fmt(d, "(%d:", me->tag);
    }

    switch (my tag) {
        case TYPE_ANY: display_colored(d, color_type, "any"); break;
        case TYPE_UNKNOWN: display_colored(d, color_type, "unknown"); break;
        case TYPE_NEVER: display_colored(d, color_type, "never"); break;
        case TYPE_VOID: display_colored(d, color_type, "void"); break;
        case TYPE_BOOLEAN: display_colored(d, color_type, "boolean"); break;
        case TYPE_NUMBER: display_colored(d, color_type, "number"); break;
        case TYPE_STRING: display_colored(d, color_type, "string"); break;
        case TYPE_SYMBOL: display_colored(d, color_type, "symbol"); break;
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
        case TYPE_CALLABLE: display_callable(d, my callable, false); break;
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
            c_foreach(i, vec_tuple_element, my tuple.elements) {
                if (i.ref->rest) {
                    display_lit(d, "...");
                }

                if (i.ref->readonly) {
                    display_lit(d, "readonly ");
                }

                display_type(d, &i.ref->type);

                if (i.ref != i.end - 1) {
                    display_lit(d, ", ");
                }
            }
            display_lit(d, "]");
            break;

        case TYPE_INDEXED: display_indexed_type(d, my indexed); break;
        case TYPE_KEYOF: display_keyof_type(d, my keyof); break;
        case TYPE_TERNARY: display_ternary_type(d, my ternary); break;
        case TYPE_ABSTRACTED: display_type(d, abstracted_type_resolve(my abstracted)); break;

        case TYPE_TYPEOF:
            display_lit(d, "typeof ");
            display_expr(d, my typeof_->expr);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            if (d.debug) {
                display_colored(d, color_implicit, "(");
            }
            bool is_first = true;
            c_foreach(i, vec_type, my composite) {
                if (!d.debug && i.ref->tag == (my tag == TYPE_UNION ? TYPE_NEVER : TYPE_ANY)) continue;

                if (is_first) {
                    is_first = false;
                } else if (my tag == TYPE_UNION) {
                    display_lit(d, " | ");
                } else {
                    display_lit(d, " & ");
                }

                bool display_peren = i.ref->tag == TYPE_CALLABLE;
                if (display_peren) display_lit(d, "(");
                display_type(d, i.ref);
                if (display_peren) display_lit(d, ")");
            }
            if (d.debug) {
                display_colored(d, color_implicit, ")");
            }
            break;

        case TYPE_LAZY:
            if (d.debug) display_colored(d, color_implicit, "(~> ");
            display_type(d, lazy_type_get(my lazy));
            if (d.debug) display_colored(d, color_implicit, ")");
            break;

        case TYPE_REF:
            if (d.debug) display_colored(d, color_implicit, "(-> ");
            display_type(d, my ref);
            if (d.debug) display_colored(d, color_implicit, ")");
            break;
    }

    if (d.debug && type_is_self_referential(me)) {
        display_colored(d, color_error, " /* self-referential */");
    }

    if (d.hash_key) {
        display_lit(d, ")");
    }
}

static void display_function_impl(Display d, FunctionImpl const* me, bool include_arrow) {
    display_generic(d, &my generic);
    display_lit(d, "(");
    c_foreach(i, vec_function_arg, my args) {
        if (i.ref->rest) {
            display_lit(d, "...");
        }
        display_initialized_binding(d, &i.ref->initializer);
        if (i.ref != i.end - 1) {
            display_lit(d, ", ");
        }
    }
    display_lit(d, ")");
    if (my ret_explicit) {
        display_lit(d, ": ");
        display_type(d, &my ret);
    }
    if (my has_body) {
        if (include_arrow) display_lit(d, " => "); else display_lit(d, " ");
        display_block(d, &my body);
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
    if (!vec_type_empty(&my implements)) {
        display_lit(d, "implements ");
        c_foreach(i, vec_type, my implements) {
            display_type(d, i.ref);
            display_lit(d, ", ");
        }
    }
    display_lit(d, "{");
    d.indent++;
    c_foreach(i, vec_class_item, my items) {
        display_newline(d);
        switch (i.ref->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                display_colored(d, color_name, "constructor");
                display_function_impl(d, &i.ref->constructor.func, false);
                break;

            case CLASS_ITEM_METHOD:
                if (i.ref->method.is_static) {
                    display_lit(d, "static ");
                }
                if (i.ref->method.is_abstract) {
                    display_lit(d, "abstract ");
                }
                display_color(d, color_name);
                display_s(d, i.ref->method.name_string);
                display_normal(d);
                if (i.ref->method.is_optional) {
                    display_lit(d, "?");
                }
                display_function_impl(d, &i.ref->method.func, false);
                break;

            case CLASS_ITEM_FIELD:
                if (i.ref->field.is_static) {
                    display_lit(d, "static ");
                }
                if (i.ref->field.is_abstract) {
                    display_lit(d, "abstract ");
                }
                if (i.ref->field.is_readonly) {
                    display_lit(d, "readonly ");
                }
                display_color(d, color_name);
                display_s(d, i.ref->field.name_string);
                display_normal(d);
                if (i.ref->field.is_optional) {
                    display_lit(d, "?");
                }
                if (i.ref->field.has_explicit_type) {
                    display_lit(d, ": ");
                    display_type(d, &i.ref->field.explicit_type);
                }
                display_lit(d, " = ");
                display_expr(d, &i.ref->field.value);
                break;

            case CLASS_ITEM_INTERFACE_MEMBER:
                display_interface_member(d, &i.ref->interface_memeber);
                break;

            case CLASS_ITEM_STATIC_BLOCK:
                display_lit(d, "static ");
                display_block(d, &i.ref->static_block);
                break;
        }
    }
    d.indent--;
    display_newline(d);
    display_lit(d, "}");
}

static void display_identifier_expr(Display d, IdentifierExpr const* me) {
    display_colored_span(d, color_name, my name);
    if (!vec_spanned_type_empty(&my generic_params)) {
        display_lit(d, "<");
        c_foreach(i, vec_spanned_type, my generic_params) {
            display_type(d, &i.ref->type);
            if (i.ref != i.end - 1) {
                display_lit(d, ", ");
            }
        }
        display_lit(d, ">");
    }
    if (d.debug) {
        if (!my decl_ref) {
            display_colored(d, color_error, " /* unresolved */");
        }
    }
}

void display_expr(Display d, Expr const* me) {
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
                display_colored(d, color_implicit, "<");
                if (my lazy_type.tag == LAZY_TYPE_EVALUATED) {
                    display_type(d, &my lazy_type.evaluated);
                } else {
                    display_colored(d, color_error, "/* unevaluated */");
                }
                display_colored(d, color_implicit, ">");
                break;
        }
    }

    intptr_t paren_depth = 0;
    for (const char* c = span_start_ptr(my outer_span); c < span_start_ptr(my inner_span); c++) {
        if (*c == '(') paren_depth++;
    }

    for (int i = 0; i < paren_depth; i++) {
        display_lit(d, "(");
    }

    switch (my tag) {
        case EXPR_IMPLICIT_ANY: display_lit(d, "/* implicit any */"); break;
        case EXPR_LITERAL: display_value(d, &my literal); break;
        case EXPR_IDENTIFIER: display_identifier_expr(d, &my identifier); break;

        case EXPR_BINARY_OP: {
            if (!paren_depth) {
                display_colored(d, color_implicit, "(");
            }
            display_expr(d, &my binary_op->left);
            display_lit(d, " ");
            display_sv(d, czview_sv(binary_op_get_zv(my binary_op->op)));
            display_lit(d, " ");
            display_expr(d, &my binary_op->right);
            if (!paren_depth) {
                display_colored(d, color_implicit, ")");
            }
        }   break;

        case EXPR_UNARY_OP: {
            if (!paren_depth) {
                display_colored(d, color_implicit, "(");
            }
            UnaryOp op = my unary_op->op;
            if (op < UNARY_OP_MIN_RIGHT_HAND_OP) {
                display_sv(d, czview_sv(unary_op_get_zv(op)));
            }
            display_expr(d, &my unary_op->expr);
            if (op >= UNARY_OP_MIN_RIGHT_HAND_OP) {
                display_sv(d, czview_sv(unary_op_get_zv(op)));
            }
            if (!paren_depth) {
                display_colored(d, color_implicit, ")");
            }
        }   break;

        case EXPR_PROPERTY: {
            if (!paren_depth) {
                display_colored(d, color_implicit, "(");
            }

            display_expr(d, &my property->expr);
            if (my property->optional_chain) {
                display_lit(d, "?");
            }

            switch (my property->form) {
                case PROPERTY_EXPR_DOT:
                    display_lit(d, ".");
                    ASSERT(my property->prop.tag == EXPR_LITERAL && my property->prop.literal.tag == VALUE_STRING);
                    display_s(d, my property->prop.literal.string);
                    break;

                case PROPERTY_EXPR_BRACKET:
                    display_lit(d, "[");
                    display_expr(d, &my property->prop);
                    display_lit(d, "]");
            }

            if (!paren_depth) {
                display_colored(d, color_implicit, ")");
            }
        }   break;

        case EXPR_AS_TYPE: {
            if (!paren_depth) {
                display_colored(d, color_implicit, "(");
            }
            display_expr(d, &my as_type->expr);
            display_lit(d, " as ");
            display_type(d, &my as_type->type);
            if (!paren_depth) {
                display_colored(d, color_implicit, ")");
            }
        }   break;

        case EXPR_TERNARY: {
            if (!paren_depth) {
                display_colored(d, color_implicit, "(");
            }
            display_expr(d, &my ternary->condition);
            display_lit(d, " ? ");
            display_expr(d, &my ternary->yes);
            display_lit(d, " : ");
            display_expr(d, &my ternary->no);
            if (!paren_depth) {
                display_colored(d, color_implicit, ")");
            }
        }   break;

        case EXPR_CALL: {
            if (my call->uses_new) {
                display_lit(d, "new ");
            }
            display_expr(d, &my call->callable);
            if (vec_expr_empty(&my call->args)) {
                display_lit(d, "()");
            } else {
                display_lit(d, "(");
                d.indent++;
                c_foreach(i, vec_expr, my call->args) {
                    display_expr(d, i.ref);
                    if (i.ref < i.end - 1) {
                        display_lit(d, ", ");
                    }
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
            c_foreach(i, vec_object_expr_item, my object) {
                display_newline(d);
                display_s(d, i.ref->name_string);
                display_lit(d, ": ");
                display_expr(d, &i.ref->value);
                display_lit(d, ",");
            }
            d.indent--;
            display_newline(d);
            display_lit(d, "}");
        }   break;

        case EXPR_ARRAY: {
            display_lit(d, "[");
            d.indent++;
            c_foreach(i, vec_expr, my array) {
                display_newline(d);
                display_expr(d, i.ref);
                display_lit(d, ",");
            }
            d.indent--;
            display_newline(d);
            display_lit(d, "]");
        }   break;

        case EXPR_CLASS_DECL:
            display_class_decl(d, my class_decl);
            break;
    }
    for (int i = 0; i < paren_depth; i++) {
        display_lit(d, ")");
    }
}

static void display_import_statement(Display d, ImportStatement const* me) {
    if (my main.is_export) {
        display_lit(d, "export ");
    } else {
        display_lit(d, "import ");
    }
    if (my main.type_only) {
        display_lit(d, "type ");
    }
    if (my main.source_tag != IMPORT_SOURCE_NONE) {
        if (my main.source_tag == IMPORT_SOURCE_WILDCARD) {
            display_colored(d, color_value, "*");
            display_lit(d, " as ");
        }
        display_colored_span(d, color_name, my main.decl_name);
        if (vec_import_item_size(&my items)) {
            display_lit(d, ", ");
        }
    }
    if (vec_import_item_size(&my items)) {
        display_lit(d, "{");
        c_foreach(i, vec_import_item, my items) {
            if (i.ref->type_only) {
                display_lit(d, "type ");
            }
            display_colored_span(d, color_name, i.ref->source_name);
            if (span_cmp(i.ref->decl_name, i.ref->source_name) != 0) {
                display_lit(d, " as ");
                display_colored_span(d, color_name, i.ref->decl_name);
            }
            if (i.ref != i.end - 1) {
                display_lit(d, ", ");
            }
        }
        display_lit(d, "}");
    }
    display_lit(d, " from ");
    display_color(d, color_string);
    display_s(d, my source_path);
    display_normal(d);
    display_lit(d, ";");
}

static void display_variable_statement(Display d, VariableStatement const* me) {
    switch (my keyword) {
        case VAR_INIT_VAR: display_lit(d, "var "); break;
        case VAR_INIT_LET: display_lit(d, "let "); break;
        case VAR_INIT_CONST: display_lit(d, "const "); break;
    }
    c_foreach(i, vec_initialized_binding, my initializers) {
        display_initialized_binding(d, i.ref);
        if (i.ref != i.end - 1) {
            display_lit(d, ", ");
        }
    }
}

static void display_enum_decl(Display d, EnumDecl const* me) {
    display_lit(d, "enum ");
    display_colored_span(d, color_name, my name);
    display_lit(d, " {");
    d.indent++;
    c_foreach(i, vec_enum_case, my cases) {
        display_newline(d);
        display_colored_span(d, color_name, i.ref->name);
        if (i.ref->value.tag != EXPR_IMPLICIT_ANY) {
            display_lit(d, " = ");
            display_expr(d, &i.ref->value);
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
    if (!vec_spanned_type_empty(&my extends)) {
        display_lit(d, " extends ");
        c_foreach(i, vec_spanned_type, my extends) {
            display_type(d, &i.ref->type);
            if (i.ref != i.end - 1) {
                display_lit(d, ", ");
            }
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
            display_sv(d, czview_sv(for_each_loop_statement_keyword_str(my for_each_loop)));
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
            c_foreach(i, vec_switch_case, my switch_->cases) {
                display_newline(d);
                if (i.ref->is_pattern) {
                    display_lit(d, "case ");
                    display_expr(d, &i.ref->pattern);
                    display_lit(d, ":");
                } else {
                    display_lit(d, "default:");
                }
                d.indent++;
                c_foreach(j, vec_statement, i.ref->statements) {
                    display_newline(d);
                    display_statement(d, j.ref);
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
                !vec_statement_empty(&my try_catch->catch_block.statements)
            ) {
                display_lit(d, " catch ");
                if (my try_catch->catch_var.tag != BINDING_NOOP) {
                    display_lit(d, "(");
                    display_binding(d, &my try_catch->catch_var);
                    display_lit(d, ") ");
                }
                display_block(d, &my try_catch->catch_block);
            }
            if (!vec_statement_empty(&my try_catch->finally_block.statements)) {
                display_lit(d, " finally ");
                display_block(d, &my try_catch->finally_block);
            }
            break;

        case STATEMENT_BREAK: display_lit(d, "break;"); break;

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

        case STATEMENT_IMPORT:
            display_import_statement(d, my import);
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
        case STATEMENT_DECL_TYPE: display_type_decl(d, &my decl.type); break;
        case STATEMENT_DECL_INTERFACE: display_interface_decl(d, my decl.interface); break;

        case STATEMENT_DECL_NAMESPACE:
            display_lit(d, "namespace ");
            display_colored_span(d, color_name, my decl.namespace.name);
            display_lit(d, " ");
            display_block(d, &my decl.namespace.contents);
            break;
    }
}

void display_block(Display d, Block const* me) {
    display_lit(d, "{");
    d.indent++;
    c_foreach(i, vec_statement, my statements) {
        display_newline(d);
        display_statement(d, i.ref);
    }
    d.indent--;
    display_newline(d);
    display_lit(d, "}");
}

void print_type_dbg(Type const* me) {
    cstr s = cstr_null;
    display_type(debug_display(&s), me);
    fprintf(stderr, "Type %p: %s\n", (void*)me, cstr_str(&s));
    cstr_drop(&s);
}
