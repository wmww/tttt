#include "ty/ty_internal.h"
#include "expr.h"
#include "scope.h"
#include "program.h"
#include "error.h"

// JSX (tsc's checkJsx* family): the JSX namespace, the element type and intrinsic elements. The attributes of an
// element are an object literal (check_jsx_attributes() in expr.c). Components, ie value tags, resolve like calls:
// TODO(plan 22 stage 3).

static Symbol const* export_with_meaning(Symbol const* container, csview name, u32 meaning) {
    Symbol const* found = symbol_for_meaning(symbol_get_export(container, name), meaning);
    return found && !found->is_placeholder ? found : nullptr;
}

// tsc's getJsxNamespaceAt: `JSX` of the factory's namespace (`React.JSX`), else the global one. Null if neither
Symbol const* ty_jsx_namespace(Checker* me, JsxElement const* element) {
    Symbol const* factory = symbol_for_meaning(element->factory_namespace, SYMBOL_NAMESPACE);
    if (factory && !factory->is_placeholder) {
        Symbol const* jsx = export_with_meaning(factory, c_sv("JSX"), SYMBOL_NAMESPACE);
        if (jsx) return jsx;
    }
    Symbol const* global = program_get_global(my program, c_sv("JSX"), SYMBOL_NAMESPACE);
    return global && !global->is_placeholder ? global : nullptr;
}

// tsc's getJsxType: the declared type of `JSX.<name>`, null if there's none (tsc's error type)
Ty const* ty_jsx_type(Checker* me, JsxElement const* element, csview name) {
    Symbol const* ns = ty_jsx_namespace(me, element);
    Symbol const* symbol = ns ? export_with_meaning(ns, name, SYMBOL_TYPE) : nullptr;
    return symbol ? ty_declared_type_of(me, symbol) : nullptr;
}

// What an element or fragment evaluates to (tsc's getJsxElementTypeAt): JSX.Element, else any
Ty const* ty_jsx_element_type(Checker* me, JsxElement const* element) {
    Ty const* type = ty_jsx_type(me, element, c_sv("Element"));
    return type ? type : ty_intrinsic(me, TY_ANY);
}

// tsc's getJsxElementChildrenPropertyName: the name of the one property of JSX.ElementChildrenAttribute, an empty name
// if it has none, null if there is no such type (then children aren't an attribute at all)
Atom const* ty_jsx_children_prop_name(Checker* me, JsxElement const* element) {
    JsxKind jsx = compiler_options()->jsx;
    if (jsx == JSX_REACT_JSX || jsx == JSX_REACT_JSXDEV) return checker_atom(me, c_sv("children"));
    Ty const* container = ty_jsx_type(me, element, c_sv("ElementChildrenAttribute"));
    if (!container || !(container->flags & TYF_OBJECT)) return nullptr;
    Members const* members = ty_members(me, container);
    if (members->prop_count == 0) return checker_atom(me, c_sv(""));
    if (members->prop_count == 1) return members->props[0]->name;
    return nullptr; // TODO: TS2608 on its declaration
}

// tsc's getIntrinsicTagSymbol and getIntrinsicAttributesTypeFromJsxOpeningLikeElement: what JSX.IntrinsicElements has
// for the tag, a property or else an index signature
JsxIntrinsic ty_jsx_intrinsic(Checker* me, JsxElement const* element) {
    JsxIntrinsic ret = { .elements = ty_jsx_type(me, element, c_sv("IntrinsicElements")) };
    if (!ret.elements || element->tag_name.tag != EXPR_IDENTIFIER) return ret;
    csview name = element->tag_name.identifier.name.sv;
    Ty const* apparent = ty_apparent_type(me, ret.elements);
    if (apparent->flags & TYF_OBJECT) ret.prop = ty_get_prop(me, apparent, checker_atom(me, name));
    if (ret.prop) {
        ret.props = prop_type(me, ret.prop);
        return ret;
    }
    ret.index = ty_applicable_index_info(me, apparent, ty_string_literal(me, name));
    if (ret.index) ret.props = ret.index->value;
    return ret;
}

// The errors of looking an intrinsic tag up, at the opening or closing tag (tsc checks both)
static void report_intrinsic_lookup(Checker* me, JsxElement const* element, JsxIntrinsic const* intrinsic, Span at, ErrorCtx* errs) {
    if (!intrinsic->elements) {
        // (TODO(plan 22 stage 4): the JSX namespace of the runtime module, until then it's not known to be missing)
        if (element->runtime_import) return;
        if (compiler_options()->no_implicit_any) {
            error_ctx_error_d(errs, at, 7026, "JSX element implicitly has type 'any' because no interface 'JSX.IntrinsicElements' exists.");
        }
    } else if (!intrinsic->props) {
        error_ctx_error_d(errs, at, 2339, "Property '", element->tag_name.identifier.name.sv,
            "' does not exist on type 'JSX.IntrinsicElements'.");
    }
    (void)me;
}

// What createJsxAttributesTypeFromAttributesProperty and checkJsxExpression report, whatever the tag resolves to
static void check_children(Checker* me, JsxElement const* element, ErrorCtx* errs) {
    bool has_children = false;
    for_each (i, vec_jsx_child, &element->children) {
        has_children |= ty_jsx_child_is_semantic(i);
        // `{...x}` as a child: x has to be an array (TS2609)
        if (i->tag == JSX_CHILD_EXPR && i->spread) {
            Ty const* type = ty_expr_type(me, &i->expr);
            if (!(type->flags & TYF_ANY) && !ty_is_array(me, type)) {
                error_ctx_error_d(errs, i->span, 2609, "JSX spread child must be an array type.");
            }
        }
    }
    VecJsxAttribute const* attributes = &element->attributes.jsx_attributes;
    if (!has_children || vec_jsx_attribute_is_empty(attributes)) return;
    // Children and an attribute of the same name: the attribute is overwritten (TS2710)
    Atom const* atom = ty_jsx_children_prop_name(me, element);
    csview name = atom ? atom_sv(atom) : c_sv("");
    if (!name.size) return;
    bool explicit = false;
    for_each (i, vec_jsx_attribute, attributes) {
        if (i->tag == JSX_ATTRIBUTE_SPREAD) {
            if (ty_expr_type(me, &i->value)->flags & TYF_ANY) return;
        } else {
            explicit |= csview_eq(&i->name.sv, &name);
        }
    }
    if (!explicit) return;
    // (at all of the attributes)
    Span span = vec_jsx_attribute_at(attributes, 0)->span;
    span.sv.size = span_end_ptr(vec_jsx_attribute_back(attributes)->span) - span.sv.buf;
    error_ctx_error_d(errs, span, 2710, "'", name, "' are specified twice. The attribute named '", name, "' will be overwritten.");
}

// tsc's checkJsxOpeningLikeElementOrOpeningFragment, and resolveJsxOpeningLikeElement's intrinsic tags: the attributes
// are checked against JSX.IntrinsicElements[tag], with IntrinsicAttributes as part of their contextual type
void ty_check_jsx_element(Checker* me, Expr const* expr, ErrorCtx* errs) {
    JsxElement const* element = expr->jsx;
    // (tsc's checkJsxPreconditions has a TS2602 for a missing JSX.Element that can't happen: getJsxType() gives the
    // error type, not undefined)
    check_children(me, element, errs);
    if (expr->tag == EXPR_JSX_FRAGMENT || !element->tag_name.inner_span.sv.size || !jsx_element_is_intrinsic(element)) {
        return; // TODO(plan 22 stage 3): fragments and components resolve like calls
    }
    JsxIntrinsic intrinsic = ty_jsx_intrinsic(me, element);
    report_intrinsic_lookup(me, element, &intrinsic, element->opening_span, errs);
    if (!element->self_closing && span_valid(element->closing_span)) {
        report_intrinsic_lookup(me, element, &intrinsic, element->closing_span, errs);
    }
    if (!intrinsic.props) return;
    Ty const* context = intrinsic.props;
    Ty const* intrinsic_attributes = ty_jsx_type(me, element, c_sv("IntrinsicAttributes"));
    if (intrinsic_attributes) {
        Ty const* both[2] = { intrinsic_attributes, intrinsic.props };
        context = ty_intersection(me, both, 2, nullptr, (TyList){0});
    }
    Ty const* source = ty_check_expr(me, &element->attributes, context);
    ty_check_assignable_expr(me, source, intrinsic.props, element->tag_name.inner_span, &element->attributes, errs);
}
