#include "ty/ty_internal.h"
#include "expr.h"
#include "scope.h"
#include "program.h"
#include "error.h"
#include "source_file.h"
#include "ast_walk.h"
#include "statement.h"

// JSX (tsc's checkJsx* family): the JSX namespace, the element type and intrinsic elements. The attributes of an
// element are an object literal (check_jsx_attributes() in expr.c). Components, ie value tags, resolve like calls:
// TODO(plan 22 stage 3).

static Symbol const* export_with_meaning(Symbol const* container, csview name, u32 meaning) {
    Symbol const* found = symbol_for_meaning(symbol_get_export(container, name), meaning);
    return found && !found->is_placeholder ? found : nullptr;
}

// (The one that starts first: tsc's walk in tree order, which recovery's AST shapes needn't follow)
static AstWalk visit_first_jsx(Expr const* expr, void* arg) {
    if (expr->tag != EXPR_JSX_ELEMENT && expr->tag != EXPR_JSX_FRAGMENT) return AST_WALK_CONTINUE;
    // (A fragment's errors go on its opening `<>`)
    Span at = expr->tag == EXPR_JSX_FRAGMENT ? expr->jsx->opening_span : expr->inner_span;
    Span* first = arg;
    if (!span_valid(*first) || span_start_ptr(at) < span_start_ptr(*first)) *first = at;
    return AST_WALK_CONTINUE;
}

// tsc's getJsxNamespaceContainerForImplicitImport: the first time a checker asks about the runtime module of a file,
// what there is to say about it is said at the file's first JSX tag (links.firstJSXTagInFile), whichever element asks.
// Once per file and checker like tsgo's: another checker that asks says the same thing at the same tag, which the
// merge dedupes
static void report_runtime_module(Checker* me, JsxElement const* element) {
    SourceFile const* file = span_source(element->opening_span);
    ModuleRef const* ref = file ? file->jsx_runtime_ref : nullptr;
    if (!ref || !my errs || my errs->phase != ERROR_PHASE_TYPECHECK || !ty_first_check(me, ref)) return;
    Span first = span_null;
    for_each (i, vec_statement, &file->ast.statements) {
        ast_walk_statement(i, visit_first_jsx, &first);
        if (span_valid(first)) break;
    }
    jsx_report_runtime_module(file, span_valid(first) ? first : element->opening_span, my errs);
}

// tsc's getJsxNamespaceAt: `JSX` of the runtime module or else the factory's namespace (`React.JSX`), else the global
// one. Null if neither
Symbol const* ty_jsx_namespace(Checker* me, JsxElement const* element) {
    report_runtime_module(me, element);
    Symbol const* factory = element->runtime_module ? module_export_equals((Symbol*)element->runtime_module) :
        symbol_for_meaning(element->factory_namespace, SYMBOL_NAMESPACE);
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

// What an element or fragment evaluates to (tsc's getJsxElementTypeAt): JSX.Element, else the error type for an
// element and any for a fragment
Ty const* ty_jsx_element_type(Checker* me, Expr const* expr) {
    report_runtime_module(me, expr->jsx);
    Ty const* type = ty_jsx_type(me, expr->jsx, c_sv("Element"));
    return type ? type : ty_intrinsic(me, expr->tag == EXPR_JSX_FRAGMENT ? TY_ANY : TY_ERROR);
}

// tsc's getJSXFragmentType: what a fragment resolves like a tag of. The fragment factory's first name (getJsxNamespace,
// `React` by default) if it is `Fragment`, else its `Fragment` export. Null (any) when there's no factory reference to
// resolve, the error type when it isn't there
Ty const* ty_jsx_fragment_type(Checker* me, JsxElement const* element) {
    CompilerOptions const* options = compiler_options();
    SourceFile const* file = span_source(element->opening_span);
    if (options->jsx != JSX_REACT && !cstr_size(&options->jsx_fragment_factory)) return nullptr;
    Symbol const* factory = element->factory_value;
    if (!factory) {
        // (`null` as the fragment factory resolves nothing: any, else nothing found: the error type)
        csview pragma = file ? file->jsx_pragmas.fragment_factory : c_sv("");
        csview name = pragma.size ? pragma : cstr_sv(&options->jsx_fragment_factory);
        return csview_equals(name, "null") ? nullptr : ty_intrinsic(me, TY_ERROR);
    }
    if (csview_equals(factory->name, "Fragment")) return ty_type_of_symbol(me, factory);
    Symbol* resolved = (Symbol*)factory;
    if (resolved->flags & SYMBOL_ALIAS) resolved = symbol_resolve_alias(resolved);
    Symbol const* fragment = resolved ? export_with_meaning(resolved, c_sv("Fragment"), SYMBOL_BLOCK_SCOPED_VARIABLE) : nullptr;
    return fragment ? ty_type_of_symbol(me, fragment) : ty_intrinsic(me, TY_ERROR);
}

// The type of what an element's tag names (a fragment's: ty_jsx_fragment_type(), any if none)
static Ty const* jsx_tag_type(Checker* me, JsxElement const* element) {
    if (!element->fragment) return ty_expr_type(me, &element->tag_name);
    Ty const* fragment = ty_jsx_fragment_type(me, element);
    return fragment ? fragment : ty_intrinsic(me, TY_ANY);
}

// tsc's getNameFromJsxElementAttributesContainer: the name of the one property of `JSX.<container>`, an empty name if
// it has none, null if there is no such type (or it has several)
static Atom const* container_prop_name(Checker* me, JsxElement const* element, csview container_name) {
    Ty const* container = ty_jsx_type(me, element, container_name);
    if (!container || !(container->flags & TYF_OBJECT)) return nullptr;
    Members const* members = ty_members(me, container);
    if (members->prop_count == 0) return my atoms.empty;
    if (members->prop_count == 1) return members->props[0]->name;
    return nullptr; // TODO: TS2608 on its declaration
}

// tsc's getJsxElementChildrenPropertyName: JSX.ElementChildrenAttribute's (then children aren't an attribute at all
// without one)
Atom const* ty_jsx_children_prop_name(Checker* me, JsxElement const* element) {
    JsxKind jsx = compiler_options()->jsx;
    if (jsx == JSX_REACT_JSX || jsx == JSX_REACT_JSXDEV) return my atoms.children;
    return container_prop_name(me, element, c_sv("ElementChildrenAttribute"));
}

// tsc's instantiateAliasOrInterfaceWithDefaults: a generic alias or interface with these first type arguments, the
// others defaulted. Null if it takes fewer
static Ty const* instantiate_with_defaults(Checker* me, Symbol const* symbol, Ty const* const* args, u32 count) {
    symbol = binder_redirect(my binder, symbol);
    Ty const* declared = ty_declared_type_of(me, symbol);
    bool alias = (symbol->flags & SYMBOL_TYPE_ALIAS) != 0;
    // (Only of a type alias, class or interface: tsgo's instantiateAliasOrInterfaceWithDefaults, an enum is none)
    if (!alias && !(symbol->flags & (SYMBOL_CLASS | SYMBOL_INTERFACE))) return nullptr;
    TyList params = alias ? ty_alias_type_params(me, symbol) :
        declared->kind == TY_DECLARED ? declared->declared.type_params : (TyList){0};
    if (params.len < count) return nullptr;
    if (!params.len) return declared;
    Ty const** filled = arena_new_n(&my arena, Ty const*, params.len);
    for (u32 i = 0; i < params.len; i++) {
        if (i < count) {
            filled[i] = args[i];
            continue;
        }
        Ty const* fallback = ty_param_default(me, params.data[i]);
        TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = params.data, .len = i }, (TyList){ .data = filled, .len = i });
        filled[i] = fallback ? ty_instantiate(me, fallback, mapper) : ty_intrinsic(me, TY_UNKNOWN_);
    }
    TyList list = { .data = filled, .len = params.len };
    return alias ? ty_alias_instance(me, symbol, list) : ty_reference(me, declared, list);
}

static Ty const* intersect(Checker* me, Ty const* a, Ty const* b) {
    if (!a) return b;
    if (!b) return a;
    Ty const* both[2] = { a, b };
    return ty_intersection(me, both, 2, nullptr, (TyList){0});
}

// tsc's getJsxManagedAttributesFromLocatedAttributes: JSX.LibraryManagedAttributes<the tag's type, props>, if there is
// such a type
static Ty const* managed_attributes(Checker* me, JsxElement const* element, Ty const* props) {
    Symbol const* ns = ty_jsx_namespace(me, element);
    Symbol const* managed = ns ? export_with_meaning(ns, c_sv("LibraryManagedAttributes"), SYMBOL_TYPE) : nullptr;
    if (!managed) return props;
    // (tsc's getStaticTypeOfReferencedJsxConstructor)
    Ty const* args[2] = { jsx_tag_type(me, element), props };
    Ty const* ret = instantiate_with_defaults(me, managed, args, 2);
    return ret ? ret : props;
}

// tsc's getEffectiveFirstArgumentForJsxSignature: what the attributes are checked against for a signature of the tag.
// A function component's props are its first parameter, a class component's the property of its instance type that
// JSX.ElementAttributesProperty names (else its constructor's first parameter), with IntrinsicClassAttributes
Ty const* ty_jsx_signature_props(Checker* me, JsxElement const* element, Signature const* sig, TyMapper const* mapper, bool class_component) {
    SigResolved const* sig_rs = signature_resolve(me, sig);
    Ty const* unknown = ty_intrinsic(me, TY_UNKNOWN_);
    Ty const* first_param = sig_rs->param_count ? ty_instantiate(me, ty_signature_param_type_at(me, sig, 0), mapper) : unknown;
    Ty const* intrinsic_attributes = ty_jsx_type(me, element, c_sv("IntrinsicAttributes"));
    if (!class_component) return intersect(me, intrinsic_attributes, managed_attributes(me, element, first_param));

    Atom const* location = container_prop_name(me, element, c_sv("ElementAttributesProperty"));
    Ty const* props = first_param;
    Ty const* instance = nullptr;
    if (location) {
        instance = ty_instantiate(me, signature_return_type(me, sig), mapper);
        Ty const* tag_type = ty_expr_type(me, &element->tag_name);
        if (!atom_sv(location).size || (instance->flags & TYF_ANY)) {
            props = instance;
        } else if (instance->kind == TY_UNION && tag_type->kind == TY_UNION) {
            // The signature of a union of classes (tsc's compositeSignatures): the intersection of what each instance
            // has (getJsxPropsTypeFromClassElement), so `RC1 || RC2` with x: number and x: string takes x: never
            TyBuf parts = {0};
            for (u32 i = 0; i < instance->composite.types.len && props; i++) {
                Ty const* member = instance->composite.types.data[i];
                if (member->flags & TYF_ANY) {
                    props = member;
                    break;
                }
                Ty const* apparent = ty_apparent_type(me, member);
                // (A property, not an index signature's: tsc's getTypeOfPropertyOfType)
                FoundProp found = apparent->flags & (TYF_OBJECT | TYF_INTERSECTION | TYF_UNION) ? ty_find_prop(me, apparent, location) : (FoundProp){0};
                Ty const* part = found.found ? ty_access(me, apparent, ty_prop_name_literal(me, location)) : nullptr;
                if (!part) props = nullptr;
                else ty_buf_push(&parts, part);
            }
            if (props && !(props->flags & TYF_ANY)) props = ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
            ty_buf_drop(&parts);
            if (!props) return unknown;
        } else {
            Ty const* apparent = ty_apparent_type(me, instance);
            Prop const* prop = apparent->flags & (TYF_OBJECT | TYF_INTERSECTION) ? ty_get_prop(me, apparent, location) : nullptr;
            if (!prop && apparent->kind == TY_INTERSECTION) props = ty_access(me, apparent, ty_prop_name_literal(me, location));
            else props = prop ? prop_type(me, prop) : nullptr;
            if (!props) return unknown; // (TS2607, see check_class_component_props())
        }
    }
    props = managed_attributes(me, element, props);
    if (props->flags & TYF_ANY) return props;
    Symbol const* ns = ty_jsx_namespace(me, element);
    Symbol const* class_attributes = ns ? export_with_meaning(ns, c_sv("IntrinsicClassAttributes"), SYMBOL_TYPE) : nullptr;
    if (class_attributes) {
        if (!instance) instance = ty_instantiate(me, signature_return_type(me, sig), mapper);
        // (tsc instantiates the type parameters of the declared type's symbol: a type alias's type is some other
        // symbol's, or none's, so an alias stays as declared, its type parameters free)
        Symbol const* redirected = binder_redirect(my binder, class_attributes);
        Ty const* host = redirected->flags & SYMBOL_TYPE_ALIAS ? nullptr :
            instantiate_with_defaults(me, class_attributes, &instance, 1);
        if (!host) host = ty_declared_type_of(me, redirected);
        props = intersect(me, host, props);
    }
    return intersect(me, intrinsic_attributes, props);
}

// tsc's getIntrinsicTagSymbol and getIntrinsicAttributesTypeFromJsxOpeningLikeElement: what JSX.IntrinsicElements has
// for the tag, a property or else an index signature
JsxIntrinsic ty_jsx_intrinsic_named(Checker* me, JsxElement const* element, csview name) {
    JsxIntrinsic ret = { .elements = ty_jsx_type(me, element, c_sv("IntrinsicElements")) };
    if (!ret.elements) return ret;
    Ty const* apparent = ty_apparent_type(me, ret.elements);
    Atom const* atom = checker_atom(me, name);
    // A union of element maps: the union property, when every member has it (tsc's getPropertyOfType)
    if (apparent->kind == TY_UNION && !(apparent->flags & TYF_BOOLEAN)) {
        TyList members = apparent->composite.types;
        TyBuf types = {0};
        Prop const* first = nullptr;
        bool every = members.len > 0;
        for (u32 i = 0; i < members.len && every; i++) {
            Ty const* member = ty_apparent_type(me, members.data[i]);
            Prop const* prop = member->flags & TYF_OBJECT ? ty_get_object_prop(me, member, atom) : nullptr;
            every = prop != nullptr;
            if (prop) {
                if (!first) first = prop;
                ty_buf_push(&types, prop_type(me, prop));
            }
        }
        if (every) {
            ret.prop = first;
            ret.props = ty_union(me, types.data, types.len, nullptr, (TyList){0});
        }
        ty_buf_drop(&types);
        if (every) return ret;
    }
    // (Object's members too: tsc's getIntrinsicTagSymbol asks getPropertyOfType, `<toString />` is Object's method)
    if (apparent->flags & TYF_OBJECT) ret.prop = ty_get_object_prop(me, apparent, atom);
    if (ret.prop) {
        ret.props = prop_type(me, ret.prop);
        return ret;
    }
    ret.index = ty_applicable_index_info(me, apparent, ty_string_literal(me, name));
    if (ret.index) ret.props = ret.index->value;
    return ret;
}

JsxIntrinsic ty_jsx_intrinsic(Checker* me, JsxElement const* element) {
    if (element->tag_name.tag != EXPR_IDENTIFIER) return (JsxIntrinsic){ .elements = ty_jsx_type(me, element, c_sv("IntrinsicElements")) };
    return ty_jsx_intrinsic_named(me, element, span_sv(element->tag_name.inner_span));
}

// The errors of looking an intrinsic tag up, at the opening or closing tag (tsc checks both)
static void report_intrinsic_lookup(Checker* me, csview name, JsxIntrinsic const* intrinsic, Span at, ErrorCtx* errs) {
    if (!intrinsic->elements) {
        if (compiler_options()->no_implicit_any) {
            error_ctx_error_d(errs, at, 7026, "JSX element implicitly has type 'any' because no interface 'JSX.IntrinsicElements' exists.");
        }
    } else if (!intrinsic->props) {
        error_ctx_error_d(errs, at, 2339, "Property '", name, "' does not exist on type 'JSX.IntrinsicElements'.");
    }
    (void)me;
}

// The closing tag of an intrinsic name, looked up by that name (tsc's checkJsxElementDeferred: getIntrinsicTagSymbol
// of the closing element, whatever the opening tag is)
static void check_closing_tag(Checker* me, JsxElement const* element, ErrorCtx* errs) {
    if (element->self_closing || !span_valid(element->closing_span) || !element->closing_name.inner_span.size ||
        element->closing_name.tag == EXPR_IMPLICIT_ANY || !jsx_closing_is_intrinsic(element)
    ) {
        return;
    }
    csview name = span_sv(element->closing_name.inner_span);
    JsxIntrinsic intrinsic = element->closing_name.tag == EXPR_IDENTIFIER ? ty_jsx_intrinsic_named(me, element, name) :
        (JsxIntrinsic){ .elements = ty_jsx_type(me, element, c_sv("IntrinsicElements")) };
    report_intrinsic_lookup(me, name, &intrinsic, element->closing_span, errs);
}

// What createJsxAttributesTypeFromAttributesProperty and checkJsxExpression report, whatever the tag resolves to
static void check_children(Checker* me, JsxElement const* element, ErrorCtx* errs) {
    bool has_children = false;
    for_each (i, vec_jsx_child, &element->children) {
        has_children |= ty_jsx_child_is_semantic(i);
        // `{...x}` as a child: x has to be an array (TS2609)
        if (i->tag == JSX_CHILD_EXPR && i->spread) {
            Ty const* type = ty_expr_type(me, &i->expr);
            // (tsc compares with anyType: the error type isn't exempt)
            if (type != ty_intrinsic(me, TY_ANY) && !ty_is_array(me, type)) {
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
            // (Not a check that decides the contextual types of the functions in it: tsc's spreads are checked in
            // the attributes' check, with their contextual type)
            Ty const* spread = ty_expr_is_context_sensitive(&i->value) ? ty_check_expr_skipping_sensitive(me, &i->value, nullptr, false) :
                ty_expr_type(me, &i->value);
            if (spread->flags & TYF_ANY) return;
        } else {
            explicit |= csview_equals_sv(span_sv(i->name), name);
        }
    }
    if (!explicit) return;
    // (at all of the attributes)
    Span span = vec_jsx_attribute_at(attributes, 0)->span;
    span.size = span_end_ptr(vec_jsx_attribute_back(attributes)->span) - span.buf;
    error_ctx_error_d(errs, span, 2710, "'", name, "' are specified twice. The attribute named '", name, "' will be overwritten.");
}

// A tag's type against JSX.ElementType, if there is one (TS2786). Returns whether there is
static bool check_element_type(Checker* me, JsxElement const* element, Ty const* tag_type, ErrorCtx* errs) {
    Symbol const* ns = ty_jsx_namespace(me, element);
    Symbol const* symbol = ns ? export_with_meaning(ns, c_sv("ElementType"), SYMBOL_TYPE) : nullptr;
    Ty const* constraint = symbol ? instantiate_with_defaults(me, symbol, nullptr, 0) : nullptr;
    // (Only an unresolved one is none: a written `type ElementType = any` takes every tag, and no return type is
    // checked then, tsc's getJsxElementTypeTypeAt with isErrorType. octane's JSX namespace)
    if (!constraint || (constraint->flags & TYF_ERROR)) return false;
    if (constraint->flags & TYF_ANY) return true;
    ty_check_jsx_component(me, tag_type, constraint, element->tag_name.inner_span, JSX_COMPONENT_TYPE, errs);
    return true;
}

// tsc's checkJsxReturnAssignableToAppropriateBound: what a component returns (a function component) or makes (a class
// component) must be JSX.Element (or null) or JSX.ElementClass
static void check_return_bound(Checker* me, JsxElement const* element, JsxResolution const* resolution, ErrorCtx* errs) {
    Ty const* ret = resolution->ret;
    if (!ret) return;
    Ty const* element_type = ty_jsx_type(me, element, c_sv("Element"));
    Ty const* stateless = nullptr;
    if (element_type) {
        Ty const* parts[2] = { element_type, ty_intrinsic(me, TY_NULL) };
        stateless = ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    Ty const* element_class = ty_jsx_type(me, element, c_sv("ElementClass"));
    Span name = element->tag_name.inner_span;
    if (resolution->function_component) {
        if (stateless) ty_check_jsx_component(me, ret, stateless, name, JSX_COMPONENT_RETURN, errs);
    } else if (resolution->class_component) {
        if (element_class) ty_check_jsx_component(me, ret, element_class, name, JSX_COMPONENT_INSTANCE, errs);
    } else if (stateless && element_class) {
        Ty const* parts[2] = { stateless, element_class };
        ty_check_jsx_component(me, ret, ty_union(me, parts, 2, nullptr, (TyList){0}), name, JSX_COMPONENT_ELEMENT, errs);
    }
}

static bool is_string_literal(Ty const* ty) {
    return (ty->flags & TYF_STRING_LITERAL) && !(ty->flags & TYF_ENUM_LITERAL) && ty->literal.string;
}

// A value tag whose type is a string literal (or a union of them) names intrinsic elements (tsc's
// getIntrinsicAttributesTypeFromStringLiteralType and its fake signatures, of which a union takes the intersection of
// the props)
static void check_string_literal_tag(Checker* me, JsxElement const* element, Ty const* tag_type, ErrorCtx* errs) {
    TyList names = tag_type->kind == TY_UNION ? tag_type->composite.types : (TyList){ .data = &tag_type, .len = 1 };
    TyBuf all = {0};
    bool missing = false;
    for (u32 i = 0; i < names.len; i++) {
        csview name = atom_sv(names.data[i]->literal.string);
        JsxIntrinsic intrinsic = ty_jsx_intrinsic_named(me, element, name);
        if (intrinsic.elements && !intrinsic.props) {
            error_ctx_error_d(errs, element->opening_span, 2339, "Property '", name, "' does not exist on type 'JSX.IntrinsicElements'.");
            missing = true;
        }
        if (intrinsic.props) ty_buf_push(&all, intrinsic.props);
    }
    // (That name has no fake signature, nor does the union then: tsc's resolveJsxOpeningLikeElement finds none)
    if (missing) {
        error_ctx_error_d(errs, element->tag_name.inner_span, 2604, "JSX element type '", element->tag_name.inner_span,
            "' does not have any construct or call signatures.");
    }
    if (all.len == names.len && all.len) {
        Ty const* each = all.len == 1 ? all.data[0] : ty_intersection(me, all.data, all.len, nullptr, (TyList){0});
        Ty const* intrinsic = ty_jsx_type(me, element, c_sv("IntrinsicAttributes"));
        Ty const* props = intersect(me, intrinsic, managed_attributes(me, element, each));
        Ty const* source = ty_check_expr(me, &element->attributes, props);
        ty_check_assignable_expr(me, source, props, element->tag_name.inner_span, &element->attributes, errs);
    }
    ty_buf_drop(&all);
}

// tsc's getJsxPropsTypeFromClassType: JSX.ElementAttributesProperty names a property the instance type doesn't have
// (TS2607, only when there are attributes)
static void check_class_component_props(Checker* me, JsxElement const* element, JsxResolution const* resolution, ErrorCtx* errs) {
    if (!resolution->class_component) return;
    // (tsc's getNameFromJsxElementAttributesContainer: more than one property is TS2608 at its declaration)
    Ty const* container = ty_jsx_type(me, element, c_sv("ElementAttributesProperty"));
    if (container && (container->flags & TYF_OBJECT) && ty_members(me, container)->prop_count > 1) {
        Ty const* declared = container->kind == TY_REFERENCE ? container->reference.target : container;
        Symbol const* symbol = declared->kind == TY_DECLARED ? declared->declared.symbol : nullptr;
        if (symbol && symbol->decl_count && span_valid(symbol->decls[0].name)) {
            error_ctx_error_d(errs, symbol->decls[0].name, 2608,
                "The global type 'JSX.ElementAttributesProperty' may not have more than one property.");
        }
    }
    if (!resolution->sig || vec_jsx_attribute_is_empty(&element->attributes.jsx_attributes)) return;
    Atom const* location = container_prop_name(me, element, c_sv("ElementAttributesProperty"));
    if (!location || !atom_sv(location).size) return;
    Ty const* instance = ty_instantiate(me, signature_return_type(me, resolution->sig), resolution->mapper);
    if (instance->flags & TYF_ANY) return;
    // (A property, not an index signature's: tsc's getTypeOfPropertyOfType, an any base's `[x: string]: any` isn't one)
    Ty const* apparent = ty_apparent_type(me, instance);
    if ((apparent->flags & (TYF_OBJECT | TYF_INTERSECTION | TYF_UNION)) && ty_find_prop(me, apparent, location).found) return;
    error_ctx_error_d(errs, element->opening_span, 2607, "JSX element class does not support attributes because it does not have a '",
        atom_sv(location), "' property.");
}

// tsc's resolveJsxOpeningLikeElement and checkJsxOpeningLikeElementOrOpeningFragment for a value tag (`<Comp />`)
static void check_value_tag(Checker* me, JsxElement const* element, ErrorCtx* errs) {
    if (element->tag_name_broken) return;
    Ty const* tag_type = ty_expr_type(me, &element->tag_name);
    bool string_literals = is_string_literal(tag_type);
    for (u32 i = 0; tag_type->kind == TY_UNION && i < tag_type->composite.types.len; i++) {
        string_literals = is_string_literal(tag_type->composite.types.data[i]);
        if (!string_literals) break;
    }
    if (string_literals) {
        check_string_literal_tag(me, element, tag_type, errs);
        check_element_type(me, element, tag_type, errs);
        return;
    }
    JsxResolution resolution = ty_resolve_jsx_element(me, element, errs);
    check_class_component_props(me, element, &resolution, errs);
    if (!check_element_type(me, element, tag_type, errs)) check_return_bound(me, element, &resolution, errs);
}

// tsc's checkJsxOpeningLikeElementOrOpeningFragment, and resolveJsxOpeningLikeElement's intrinsic tags: the attributes
// are checked against JSX.IntrinsicElements[tag], with IntrinsicAttributes as part of their contextual type
static void check_jsx_element(Checker* me, Expr const* expr, ErrorCtx* errs) {
    JsxElement const* element = expr->jsx;
    // (tsc's checkJsxPreconditions has a TS2602 for a missing JSX.Element that can't happen: getJsxType() gives the
    // error type, not undefined)
    check_children(me, element, errs);
    if (element->fragment) {
        // A fragment resolves like a call of what the fragment factory names, its children the attributes
        // (resolveJsxOpeningLikeElement with getJSXFragmentType)
        if (ty_jsx_fragment_type(me, element)) ty_resolve_jsx_element(me, element, errs);
        return;
    }
    if (!element->tag_name.inner_span.size) return;
    check_closing_tag(me, element, errs);
    if (!jsx_element_is_intrinsic(element)) {
        check_value_tag(me, element, errs);
        return;
    }
    check_element_type(me, element, ty_string_literal(me, span_sv(element->tag_name.inner_span)), errs);
    if (!vec_spanned_type_is_empty(&element->type_args)) {
        // An intrinsic element takes no type arguments (resolveJsxOpeningLikeElement)
        // (the list, a trailing comma included)
        Span last = span_valid(element->type_args_trailing_comma) ? element->type_args_trailing_comma :
            vec_spanned_type_back(&element->type_args)->span;
        Span span = span_between(vec_spanned_type_front(&element->type_args)->span, last);
        error_ctx_error_d(errs, span, 2558, "Expected 0 type arguments, but got ", vec_spanned_type_size(&element->type_args), ".");
    }
    JsxIntrinsic intrinsic = ty_jsx_intrinsic(me, element);
    report_intrinsic_lookup(me, span_sv(element->tag_name.inner_span), &intrinsic, element->opening_span, errs);
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

void ty_check_jsx_element(Checker* me, Expr const* expr, ErrorCtx* errs) {
    report_runtime_module(me, expr->jsx);
    JsxElement const* outer = my jsx_element;
    my jsx_element = expr->jsx;
    check_jsx_element(me, expr, errs);
    my jsx_element = outer;
}

// tsc's reportErrorResults: attributes that don't fit `JSX.IntrinsicAttributes & props` (or IntrinsicClassAttributes)
// get no "not assignable" head of their own, when both of those types exist
bool ty_jsx_skips_head(Checker* me, Ty const* source, Ty const* target) {
    if (!my jsx_element || source->kind != TY_ANONYMOUS || !source->anonymous.jsx_attributes || target->kind != TY_INTERSECTION) {
        return false;
    }
    Ty const* attributes = ty_jsx_type(me, my jsx_element, c_sv("IntrinsicAttributes"));
    Ty const* class_attributes = ty_jsx_type(me, my jsx_element, c_sv("IntrinsicClassAttributes"));
    if (!attributes || !class_attributes) return false;
    for (u32 i = 0; i < target->composite.types.len; i++) {
        Ty const* member = target->composite.types.data[i];
        if (member == attributes || member == class_attributes) return true;
    }
    return false;
}

// The factory's entity name (tsc's getJsxFactoryEntity): the file's `@jsx` pragma, the jsxFactory option, else
// `<reactNamespace>.createElement`. Invalid names count as not given
static cstr jsx_factory_entity(JsxElement const* element) {
    CompilerOptions const* options = compiler_options();
    SourceFile const* file = span_source(element->opening_span);
    if (file && compiler_option_is_entity_name(file->jsx_pragmas.factory)) return cstr_from_sv(file->jsx_pragmas.factory);
    if (compiler_option_is_entity_name(cstr_sv(&options->jsx_factory))) return cstr_clone(options->jsx_factory);
    if (!cstr_size(&options->jsx_factory) && cstr_size(&options->react_namespace)) {
        return cstr_d(options->react_namespace, ".createElement");
    }
    return cstr_lit("React.createElement");
}

// tsc's resolveEntityName on the factory: its first name as the resolve walk found it, then exports of namespaces
static Symbol const* jsx_factory_symbol(Checker* me, JsxElement const* element, csview entity) {
    isize dot = csview_find(entity, ".");
    Symbol* symbol = (Symbol*)(dot == c_NPOS ? element->factory_value : element->factory_namespace);
    while (symbol && dot != c_NPOS) {
        if (symbol->flags & SYMBOL_ALIAS) symbol = symbol_resolve_alias(symbol);
        if (!symbol) return nullptr;
        entity = csview_slice(entity, dot + 1, entity.size);
        dot = csview_find(entity, ".");
        csview name = dot == c_NPOS ? entity : csview_slice(entity, 0, dot);
        symbol = (Symbol*)symbol_for_meaning(symbol_get_export(symbol, name), dot == c_NPOS ? SYMBOL_VALUE : SYMBOL_NAMESPACE);
    }
    if (symbol && symbol->flags & SYMBOL_ALIAS) symbol = symbol_resolve_alias(symbol);
    return symbol && (symbol->flags & SYMBOL_VALUE) ? binder_redirect(my binder, symbol) : nullptr;
}

// tsc's checkTagNameDoesNotExpectTooManyArguments: a function component must not require more arguments than any
// signature of the factory passes to what its first parameter is (React.createElement: props and context). False
// (and TS6229) if it does
bool ty_jsx_tag_arity_fits(Checker* me, JsxElement const* element, Ty const* tag_type, ErrorCtx* errs) {
    if (element->runtime_module) return true;
    Ty const* apparent = ty_apparent_type(me, tag_type);
    if (!(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) || apparent->kind == TY_UNION) return true;
    Members const* tag_members = ty_members(me, apparent);
    if (!tag_members->call_count) return true;
    cstr entity = jsx_factory_entity(element);
    Symbol const* factory = jsx_factory_symbol(me, element, cstr_sv(&entity));
    Members const* factory_members = factory ? ty_members(me, ty_apparent_type(me, ty_type_of_symbol(me, factory))) : nullptr;
    bool fits = true;
    u32 max_params = 0;
    bool component_params = false;
    for (u32 i = 0; factory_members && i < factory_members->call_count && fits; i++) {
        Signature const* sig = factory_members->call[i];
        signature_resolve(me, sig);
        Ty const* first = ty_signature_param_type_at(me, sig, 0);
        if (!first) continue;
        Members const* first_members = ty_members(me, ty_apparent_type(me, first));
        for (u32 j = 0; j < first_members->call_count; j++) {
            Signature const* param_sig = first_members->call[j];
            signature_resolve(me, param_sig);
            component_params = true;
            if (ty_signature_has_rest(me, param_sig)) {
                fits = false; // (any number of arguments: nothing to check)
                break;
            }
            u32 count = ty_signature_param_count(me, param_sig);
            if (count > max_params) max_params = count;
        }
    }
    if (!fits || !component_params) {
        cstr_drop(&entity);
        return true;
    }
    u32 min_args = UINT32_MAX;
    for (u32 i = 0; i < tag_members->call_count; i++) {
        signature_resolve(me, tag_members->call[i]);
        if (sig_res(me, tag_members->call[i])->min_args < min_args) min_args = sig_res(me, tag_members->call[i])->min_args;
    }
    if (min_args <= max_params) {
        cstr_drop(&entity);
        return true;
    }
    if (errs) {
        Span tag = element->tag_name.inner_span;
        Error* error = error_ctx_error_d(errs, tag, 6229, "Tag '", tag, "' expects at least '", min_args,
            "' arguments, but the JSX factory '", entity, "' provides at most '", max_params, "'.");
        Symbol const* symbol = element->tag_name.tag == EXPR_IDENTIFIER ? element->tag_name.identifier.symbol : nullptr;
        for (u32 i = 0; error && symbol && !(symbol->flags & SYMBOL_ALIAS) && i < symbol->decl_count; i++) {
            if (!(symbol->decls[i].flags & SYMBOL_VALUE)) continue;
            error_add_related_d(error, symbol->decls[i].name, 2728, "'", tag, "' is declared here.");
            break;
        }
    }
    cstr_drop(&entity);
    return false;
}
