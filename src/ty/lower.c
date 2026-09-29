#include "source_file.h"
#include "ty/ty_internal.h"
#include "program.h"
#include "compiler_options.h"
#include "scope.h"
#include "enum_decl.h"
#include "class_decl.h"
#include "interface_decl.h"
#include "statement.h"
#include "binding.h"
#include "port_statement.h"
#include <math.h>

// Lowering: syntax (`TypeNode`) to semantic types (`Ty`), and what symbols are as types (ty_declared_type_of()) and as
// values (ty_type_of_symbol()). Nodes are syntax only: what the parser made.

static Ty const* lower_node(Checker* me, TypeNode const* node, LowerCtx outer_ctx, Symbol const* alias, TyList alias_args);
static inline Ty const* lower(Checker* me, TypeNode const* node, LowerCtx ctx, Symbol const* alias, TyList alias_args);

SymbolLinks* ty_symbol_links(Checker* me, Symbol const* symbol) {
    u64 key[2] = { KEY_SYMBOL_LINKS, (u64)(uintptr_t)symbol };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (!*slot) *slot = arena_new(&my arena, SymbolLinks);
    return (SymbolLinks*)*slot;
}

static inline SymbolLinks* symbol_links(Checker* me, Symbol const* symbol) {
    return ty_symbol_links(me, symbol);
}

// The program's version of a symbol. A symbol that comes from inside the AST of the shared lib program (`Array` in
// the declaration of `ReadonlyArray`) is the lib's, which doesn't have what this program has added to it
static inline Symbol const* canonical(Checker* me, Symbol const* symbol) {
    return binder_redirect(my binder, symbol);
}

TyList ty_type_params_of(Checker* me, Generic const* generic) {
    if (generic_empty(generic)) return (TyList){0};
    u64 key[2] = { KEY_GENERIC_PARAMS, (u64)(uintptr_t)generic };
    TyList const* existing = intern_get(&my links, key, 2);
    if (existing) return *existing;

    u32 len = 0;
    Ty const** params = arena_new_n(&my arena, Ty const*, vec_generic_item_size(&generic->items));
    for_each (i, vec_generic_item, &generic->items) {
        Ty const* param = i->symbol_ref ?
            ty_type_param(me, i->symbol_ref) :
            ty_synthetic_type_param(me, i->name.sv);
        if (!param->param.state->item) param->param.state->item = i;
        // (Two of the same name are one symbol, one type parameter: tsc's appendIfUnique in
        // getTypeParametersFromDeclaration)
        bool seen = false;
        for (u32 j = 0; j < len && !seen; j++) seen = params[j] == param;
        if (!seen) params[len++] = param;
    }
    TyList* list = arena_new(&my arena, TyList);
    *list = (TyList){ .data = params, .len = len };
    *intern_slot(me, &my links, key, 2) = list;
    return *list;
}

// Makes the type parameters of a later declaration of a merged interface the same types as the first declaration's
static void unify_type_params(Checker* me, TyList params, Generic const* other) {
    for_each (i, vec_generic_item, &other->items) {
        if ((u32)i_index >= params.len || !i->symbol_ref) break;
        u64 key[2] = { KEY_TYPE_PARAM, (u64)(uintptr_t)i->symbol_ref };
        void const** slot = intern_slot(me, &my links, key, 2);
        if (!*slot) *slot = params.data[i_index];
    }
}

Span ty_symbol_display_name(Symbol const* symbol) {
    if (span_valid(symbol->name_span) || !(symbol->flags & SYMBOL_CLASS)) return symbol->name_span;
    ClassDecl const* class_decl = symbol_find_node(symbol, SYMBOL_DECL_CLASS);
    return class_decl ? class_decl_display_name(class_decl) : symbol->name_span;
}

// `A.B.name`. TODO(plan 04 stage 3): tsc qualifies a name as far as needed to be reachable from where it's printed
// (symbol accessibility). Always qualifying is what it does for error messages.
static void append_qualified_name(cstr* out, Symbol const* symbol) {
    Symbol const* parent = symbol->parent;
    // Modules and ambient modules (`declare module "m"`) are containers too, but not part of a name
    if (parent && !parent->is_module && !(parent->name.size && (*parent->name.buf == '"' || *parent->name.buf == '\''))) {
        append_qualified_name(out, parent);
        cstr_append_lit(out, ".");
    }
    cstr_append_span(out, ty_symbol_display_name(symbol));
}

Atom const* ty_qualified_name(Checker* me, Symbol const* symbol) {
    Symbol const* parent = symbol->parent;
    if (!parent || parent->is_module) return checker_atom(me, ty_symbol_display_name(symbol).sv);
    cstr qualified = cstr_null;
    append_qualified_name(&qualified, symbol);
    Atom const* ret = checker_atom(me, cstr_sv(&qualified));
    cstr_drop(&qualified);
    return ret;
}

// tsc's isThislessInterface (negated): an interface has a `this` type if one of its declarations mentions `this` or
// extends a class or an interface that has one
static bool interface_has_this(Symbol const* symbol, u32 depth) {
    if (depth > 32) return false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* interface_decl = symbol->decls[i].node;
        if (interface_decl->contains_this) return true;
        for_each (extends, vec_spanned_type, &interface_decl->extends) {
            if (extends->type.tag != TYPE_IDENTIFIER || extends->type.identifier->import_ref) continue;
            Symbol const* base = extends->type.identifier->target;
            if (!base || !(base->flags & SYMBOL_INTERFACE) || (base->flags & SYMBOL_CLASS)) return true;
            if (base != symbol && interface_has_this(base, depth + 1)) return true;
        }
    }
    return false;
}

// The type parameters of the scopes a declaration is in (tsc's getOuterTypeParameters), outermost first
TyList ty_outer_type_params(Checker* me, OuterGenerics const* outer) {
    if (!outer) return (TyList){0};
    TyList parent = ty_outer_type_params(me, outer->parent);
    TyList own = ty_type_params_of(me, outer->generic);
    if (!parent.len) return own;
    Ty const** data = arena_new_n(&my arena, Ty const*, parent.len + own.len);
    memcpy(data, parent.data, parent.len * sizeof(*data));
    memcpy(data + parent.len, own.data, own.len * sizeof(*data));
    return (TyList){ .data = data, .len = parent.len + own.len };
}

static Ty const* make_declared(Checker* me, Symbol const* symbol, TyDeclaredKind kind) {
    Ty* ret = ty_alloc(me, TY_DECLARED, TYF_OBJECT);
    ret->declared.kind = kind;
    ret->declared.symbol = symbol;
    ret->declared.name = ty_qualified_name(me, symbol);
    // A constructor function assigned to a property (`ns.C = function () {}`) is a class of the function expression's
    // own symbol in tsc (getExpandoSymbol), which has no parent
    if (kind == TY_DECLARED_CLASS_INSTANCE && symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_ASSIGNMENT &&
        symbol_decl_js_function(&symbol->decls[0])
    ) {
        ret->declared.name = checker_atom(me, ty_symbol_display_name(symbol).sv);
    }
    ret->declared.state = arena_new(&my arena, TyObjectState);
    if (kind == TY_DECLARED_INTERFACE || kind == TY_DECLARED_CLASS_INSTANCE || kind == TY_DECLARED_CLASS_STATIC) {
        ret->declared.outer = symbol_outer_generics(symbol);
        TyList outer = ty_outer_type_params(me, ret->declared.outer);
        ret->declared.outer_count = outer.len;
        ret->declared.type_params = outer;
    }
    if (kind == TY_DECLARED_INTERFACE || kind == TY_DECLARED_CLASS_INSTANCE) {
        Generic const* first = symbol_type_generic(symbol);
        TyList local = ty_type_params_of(me, first);
        TyList outer = ret->declared.type_params;
        if (!outer.len) {
            ret->declared.type_params = local;
        } else if (local.len) {
            Ty const** data = arena_new_n(&my arena, Ty const*, outer.len + local.len);
            memcpy(data, outer.data, outer.len * sizeof(*data));
            memcpy(data + outer.len, local.data, local.len * sizeof(*data));
            ret->declared.type_params = (TyList){ .data = data, .len = outer.len + local.len };
        }
        for (u32 i = 0; i < symbol->decl_count; i++) {
            if (symbol->decls[i].kind != SYMBOL_DECL_INTERFACE) continue;
            InterfaceDecl const* interface_decl = symbol->decls[i].node;
            if (&interface_decl->generic != first) unify_type_params(me, local, &interface_decl->generic);
        }
        // (A class merged with an interface is a class: tsc's getDeclaredTypeOfClassOrInterface)
        if (kind == TY_DECLARED_CLASS_INSTANCE || (symbol->flags & SYMBOL_CLASS) || interface_has_this(symbol, 0)) {
            ret->declared.this_param = ty_this_type_param(me, ret);
        }
        ret->declared.raw = arena_new(&my arena, TyObjectState);
    }
    return ret;
}

void ty_set_declared_type(Checker* me, Symbol const* symbol, Ty const* type) {
    SymbolLinks* links = symbol_links(me, symbol);
    links->declared_type = type;
    links->declared_state = LAZY_STATE_DONE;
}

// An enum member's type is made when its enum's is
static Ty const* enum_member_type(Checker* me, Symbol const* symbol) {
    if (!symbol->parent || !(symbol->parent->flags & SYMBOL_ENUM)) return ty_intrinsic(me, TY_ERROR);
    ty_declared_type_of(me, symbol->parent);
    SymbolLinks* links = symbol_links(me, symbol);
    if (links->declared_state == LAZY_STATE_DONE) return links->declared_type;
    // The enum is being resolved further up the stack and hasn't got to this member yet
    return nullptr;
}

// `type X = intrinsic` (the lib's Uppercase<S> & co.): the builtin name, not a type called `intrinsic`
static bool is_intrinsic_body(TypeNode const* body) {
    return body->tag == TYPE_IDENTIFIER && body->identifier->target && !vec_span_size(&body->identifier->namespaces) &&
        csview_equals(body->identifier->name.sv, "intrinsic") &&
        symbol_find_decl(body->identifier->target, SYMBOL_DECL_BUILTIN);
}

// The type a symbol stands for in a type position: the symbol must be the one that has the type meaning (what
// identifiers keep as `target`). Everything about a placeholder is any
Ty const* ty_declared_type_of(Checker* me, Symbol const* symbol) {
    symbol = canonical(me, symbol);
    if (symbol->is_placeholder) return ty_intrinsic(me, TY_ERROR);
    SymbolLinks* links = symbol_links(me, symbol);
    switch (links->declared_state) {
        case LAZY_STATE_DONE: return links->declared_type;
        case LAZY_STATE_IN_FLIGHT:
            // tsc's pushTypeResolution: every alias from this one on is part of the cycle, and fails when it finishes
            for (u32 i = my aliases_in_flight_len; i-- > 0;) {
                symbol_links(me, my aliases_in_flight[i])->circular = true;
                if (my aliases_in_flight[i] == symbol) break;
            }
            return ty_intrinsic(me, TY_ERROR);
        case LAZY_STATE_NOT_STARTED: break;
    }
    links->declared_state = LAZY_STATE_IN_FLIGHT;
    Ty const* ret;
    if (symbol->flags & SYMBOL_INTERFACE) {
        ret = make_declared(me, symbol, TY_DECLARED_INTERFACE);
    } else if (symbol->flags & SYMBOL_CLASS) {
        ret = make_declared(me, symbol, TY_DECLARED_CLASS_INSTANCE);
    } else if (symbol->flags & SYMBOL_ENUM) {
        ret = ty_resolve_enum(me, symbol);
    } else if (symbol->flags & SYMBOL_ENUM_MEMBER) {
        ret = enum_member_type(me, symbol);
        if (!ret) {
            links->declared_state = LAZY_STATE_NOT_STARTED;
            return ty_intrinsic(me, TY_ERROR);
        }
    } else if (symbol->flags & SYMBOL_TYPE_PARAMETER) {
        ret = ty_type_param(me, symbol);
    } else if (symbol->flags & SYMBOL_TYPE_ALIAS) {
        SymbolDecl const* decl = &symbol->decls[0];
        for (u32 i = symbol->decl_count; i--;) {
            if (symbol->decls[i].flags & SYMBOL_TYPE_ALIAS) decl = &symbol->decls[i];
        }
        if (decl->kind == SYMBOL_DECL_TYPE_ALIAS) {
            TypeDecl const* type_decl = decl->node;
            TyList params = ty_type_params_of(me, &type_decl->generic);
            // (A local alias can mention the type parameters around it too)
            LowerCtx ctx = { .generic = params.len > 0 || type_decl->outer_generics, .in_alias = true };
            TypeNode const* body = &type_decl->type;
            if (is_intrinsic_body(body)) {
                // `type X = intrinsic`: what the checker makes of the name. The string mappings and NoInfer are made
                // where they're referenced (lower_identifier()), this is what the bare name is
                bool undefined = csview_equals(symbol->name, "BuiltinIteratorReturn") &&
                    compiler_options()->strict_builtin_iterator_return;
                ret = ty_intrinsic(me, undefined ? TY_UNDEFINED : TY_ANY);
            } else {
                if (my aliases_in_flight_len == my aliases_in_flight_cap) {
                    my aliases_in_flight_cap = my aliases_in_flight_cap ? my aliases_in_flight_cap * 2 : 16;
                    my aliases_in_flight = realloc(my aliases_in_flight, sizeof(Symbol const*) * my aliases_in_flight_cap);
                    panic_if(!my aliases_in_flight, "out of memory");
                }
                my aliases_in_flight[my aliases_in_flight_len++] = symbol;
                ret = lower(me, body, ctx, symbol, params);
                my aliases_in_flight_len--;
                if (links->circular) {
                    // (An `@enum {T}`'s alias is reported at T: tsc's declaration is the tag, which has no name)
                    Span at = type_decl->jsdoc_enum && type_decl->type.tag == TYPE_IDENTIFIER ?
                        identifier_type_span(type_decl->type.identifier) : decl->name;
                    ty_error_at(me, at, 2456, cstr_d("Type alias '", symbol->name, "' circularly references itself."));
                    ret = ty_intrinsic(me, TY_ERROR);
                }
            }
        } else if (decl->kind == SYMBOL_DECL_BUILTIN) {
            // The `object` keyword is a builtin alias whose node is an empty interface
            ret = csview_equals(symbol->name, "object") ?
                ty_intrinsic(me, TY_NON_PRIMITIVE) :
                ty_from_node(me, decl->node, (LowerCtx){0});
        } else {
            ret = ty_intrinsic(me, TY_ERROR);
        }
    } else {
        ret = ty_intrinsic(me, TY_ERROR); // A value used as a type, TS2749
    }
    // symbol_links() pointers are stable (arena), the table growing in the meantime doesn't matter
    links->declared_type = ret;
    links->declared_state = LAZY_STATE_DONE;
    return ret;
}

// The static side of a class, the object of an enum: what the symbol is as a value, before any merging
Ty const* ty_static_type_of(Checker* me, Symbol const* symbol) {
    symbol = canonical(me, symbol);
    SymbolLinks* links = symbol_links(me, symbol);
    if (!links->static_type) {
        links->static_type = make_declared(
            me, symbol, symbol->flags & SYMBOL_CLASS ? TY_DECLARED_CLASS_STATIC : TY_DECLARED_ENUM_OBJECT
        );
    }
    return links->static_type;
}

// Type arguments as written, padded with defaults (then `any`) and truncated to the parameter count. The first `outer`
// parameters are a local class or interface's outer ones, which are passed as themselves
#define MAX_TYPE_ARGS 32
static TyList lower_type_args(
    Checker* me, TyList params, u32 outer, VecSpannedType const* written, LowerCtx ctx, Ty const* args[MAX_TYPE_ARGS]
) {
    panic_if(params.len > MAX_TYPE_ARGS, "too many type parameters");
    u32 written_len = (u32)vec_spanned_type_size(written) + outer;
    for (u32 i = 0; i < params.len; i++) {
        if (i < outer) {
            args[i] = params.data[i];
        } else if (i < written_len) {
            args[i] = ty_from_node(me, &vec_spanned_type_at(written, i - outer)->type, ctx);
        } else {
            Ty const* fallback = ty_param_default(me, params.data[i]);
            if (fallback) {
                // A default can mention the parameters before it
                // Arena-allocated: instantiation can keep the mapper (deferred conditional and object types)
                TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = params.data, .len = i }, (TyList){ .data = args, .len = i });
                fallback = ty_instantiate(me, fallback, mapper);
            }
            // TODO: TS2314 when there's no default. tsc fills in the error type too
            args[i] = fallback ? fallback : ty_intrinsic(me, written_len > outer ? TY_ERROR : TY_ANY);
        }
    }
    return (TyList){ .data = args, .len = params.len };
}

static Ty const* alias_instance(Checker* me, Symbol const* decl, Ty const* body, TyList params, TyList args) {
    u64 stack_key[32];
    u32 count = 2 + args.len;
    check(count <= c_arraylen(stack_key));
    stack_key[0] = KEY_ALIAS_INSTANCE;
    stack_key[1] = (u64)(uintptr_t)decl;
    for (u32 i = 0; i < args.len; i++) stack_key[2 + i] = args.data[i]->id;
    Ty const* ret = intern_get(&my links, stack_key, count);
    if (!ret) {
        TyMapper const* mapper = ty_mapper_new(me, params, args);
        ret = ty_instantiate(me, body, mapper);
        // (Its type parameters unused, the body is itself: it still goes by these arguments)
        if (ret == body && body->alias == decl) ret = ty_realias(me, ret, decl, args);
        *intern_slot(me, &my links, stack_key, count) = ret;
    }
    return ret;
}

// The instantiation of a generic type alias (canonical) with these type arguments, as `A<args>` written would be
Ty const* ty_alias_instance(Checker* me, Symbol const* decl, TyList args) {
    return alias_instance(me, decl, ty_declared_type_of(me, decl), ty_alias_type_params(me, decl), args);
}

TyList ty_alias_type_params(Checker* me, Symbol const* decl) {
    return ty_type_params_of(me, symbol_type_generic(decl));
}

static Ty const* lower_identifier(Checker* me, IdentifierType const* identifier, LowerCtx ctx);

// `{ [P in keyof T]: X }` with T a type parameter: instantiating it keeps the name it was written with (tsc's
// instantiateMappedType maps the constituents without the new alias), only a union made of it takes the new one
static bool is_homomorphic_mapped_node(TypeNode const* node) {
    if (node->tag != TYPE_INTERFACE || vec_interface_member_size(&node->interface.members) != 1) return false;
    InterfaceMember const* member = vec_interface_member_at(&node->interface.members, 0);
    if (member->key.tag != INTERFACE_KEY_MAPPED || member->key.mapped.in_type.tag != TYPE_KEYOF) return false;
    TypeNode const* operand = &member->key.mapped.in_type.keyof->interface;
    return operand->tag == TYPE_IDENTIFIER && operand->identifier->target &&
        (operand->identifier->target->flags & SYMBOL_TYPE_PARAMETER);
}

// The body of a type alias as written (the last declaration, like ty_declared_type_of()). Null if there's none
static TypeNode const* alias_body_node(Symbol const* decl) {
    TypeNode const* ret = nullptr;
    for (u32 i = 0; decl && i < decl->decl_count; i++) {
        if (decl->decls[i].kind == SYMBOL_DECL_TYPE_ALIAS) ret = &((TypeDecl const*)decl->decls[i].node)->type;
    }
    return ret;
}

// A reference to one of the lib's intrinsic generic aliases: `Uppercase<T>` & co. are string mappings (tsc's
// getStringMappingType). Null if the alias isn't one
static Ty const* intrinsic_alias_instance(Checker* me, Symbol const* decl, Ty const* arg) {
    static char const* const mappings[] = { "Uppercase", "Lowercase", "Capitalize", "Uncapitalize" };
    TypeNode const* body = alias_body_node(decl);
    if (!body || !is_intrinsic_body(body)) return nullptr;
    for (u32 i = 0; i < c_arraylen(mappings); i++) {
        if (csview_equals(decl->name, mappings[i])) return ty_string_mapping(me, (TyStringMapping)i, decl, arg);
    }
    if (csview_equals(decl->name, "NoInfer")) return ty_no_infer(me, arg);
    return nullptr;
}

static bool symbol_is_generic_alias(Symbol const* symbol) {
    return symbol->flags & SYMBOL_TYPE_ALIAS && !symbol->is_placeholder && symbol_type_generic(symbol);
}

// The type of a type reference whose name doesn't resolve: tsc's unresolvedType, which prints as written
static Ty const* unresolved_reference(Checker* me, IdentifierType const* identifier) {
    // (A missing name, `a.` at the end of a line: no node to reuse, errorType)
    if (identifier->keyword || !span_valid(identifier->name) || !identifier->name.sv.size) return ty_intrinsic(me, TY_ERROR);
    cstr name = cstr_null;
    for_each (i, vec_span, &identifier->namespaces) {
        cstr_append_sv(&name, i->sv);
        cstr_append(&name, ".");
    }
    cstr_append_sv(&name, identifier->name.sv);
    // (Printed as written, type arguments too: tsc reuses the reference's node, `Class<T>`)
    if (vec_spanned_type_size(&identifier->generic_params)) {
        cstr_append(&name, "<");
        for_each (i, vec_spanned_type, &identifier->generic_params) {
            if (i_index) cstr_append(&name, ", ");
            cstr_append_sv(&name, i->span.sv);
        }
        cstr_append(&name, ">");
    }
    Ty const* ret = ty_unresolved(me, cstr_sv(&name));
    cstr_drop(&name);
    return ret;
}

static Ty const* lower_identifier(Checker* me, IdentifierType const* identifier, LowerCtx ctx) {
    switch ((JsDocIntrinsic)identifier->jsdoc_intrinsic) {
        case JSDOC_INTRINSIC_NONE: break;
        case JSDOC_INTRINSIC_STRING: return ty_intrinsic(me, TY_STRING);
        case JSDOC_INTRINSIC_NUMBER: return ty_intrinsic(me, TY_NUMBER);
        case JSDOC_INTRINSIC_BIGINT: return ty_intrinsic(me, TY_BIGINT);
        case JSDOC_INTRINSIC_BOOLEAN: return ty_intrinsic(me, TY_BOOLEAN);
        case JSDOC_INTRINSIC_VOID: return ty_intrinsic(me, TY_VOID);
        case JSDOC_INTRINSIC_UNDEFINED: return ty_intrinsic(me, TY_UNDEFINED);
        case JSDOC_INTRINSIC_NULL: return ty_intrinsic(me, TY_NULL);
        case JSDOC_INTRINSIC_ANY: return ty_intrinsic(me, TY_ANY);
        case JSDOC_INTRINSIC_INDEX_SIGNATURE: {
            TypeNode const* key = &vec_spanned_type_at(&identifier->generic_params, 0)->type;
            IndexInfo info = {
                .key = ty_intrinsic(me, key->tag == TYPE_STRING ? TY_STRING : TY_NUMBER),
                .value = ty_from_node(me, &vec_spanned_type_at(&identifier->generic_params, 1)->type, ctx),
                .param_name = checker_atom(me, c_sv("x")),
            };
            return ty_object_literal(me, nullptr, 0, &info, 1, OBJECT_SYNTHETIC);
        }
    }
    // (A JavaScript constructor function's `this` type)
    if (identifier->keyword == KEYWORD_IDENTIFIER_THIS && identifier->this_function) {
        Ty const* js_this = ty_js_this_type(me, identifier->this_function);
        if (js_this) return js_this;
    }
    Symbol const* decl = canonical(me, identifier->target);
    if (span_valid(identifier->import_span)) {
        // `import("./a").B`: an unresolved one was reported, or is any by design (shorthand ambient module)
        if (!decl || decl->is_placeholder) return ty_intrinsic(me, TY_ANY);
        if (identifier->import_typeof || identifier->import_prop_start >= 0) {
            Ty const* ret = ty_type_of_symbol(me, decl);
            isize count = vec_span_size(&identifier->namespaces) + 1;
            for (isize i = identifier->import_prop_start; i >= 0 && i < count; i++) {
                Span name = i == count - 1 ? identifier->name : *vec_span_at(&identifier->namespaces, i);
                Prop const* prop = ty_get_prop(me, ret, checker_atom(me, name.sv));
                if (!prop && i == 0) {
                    // A property of what `module.exports =` assigns, like the names before it (tsc's
                    // getTypeFromImportTypeNode reports it as a missing export)
                    cstr container_name = symbol_fully_qualified_name(decl);
                    ty_error_at(me, name, 2694, cstr_d("Namespace '", container_name, "' has no exported member '", name.sv, "'."));
                    cstr_drop(&container_name);
                    return ty_intrinsic(me, TY_ERROR);
                }
                if (!prop) return ty_intrinsic(me, TY_ANY); // TODO: TS2339
                ret = prop_type(me, prop);
            }
            // (A JSDoc `import("./m").C` of a class value means its instance, tsc's getTypeFromJSDocValueReference)
            if (!identifier->import_typeof && ret->kind == TY_DECLARED && ret->declared.kind == TY_DECLARED_CLASS_STATIC &&
                ret->declared.symbol
            ) {
                return ty_class_instance_type(me, ret->declared.symbol);
            }
            return ret;
        }
    }
    // A name that doesn't resolve to a type: an alias that doesn't resolve, a value outside JSDoc (TS2749) (tsc's
    // resolveTypeReferenceName makes those unresolved symbols too)
    if (!decl || (decl->is_placeholder && decl->is_unresolved) ||
        (!identifier->jsdoc && !identifier->keyword && !(decl->flags & SYMBOL_TYPE) && !decl->is_placeholder)
    ) {
        return unresolved_reference(me, identifier);
    }
    Ty const* args_buf[MAX_TYPE_ARGS];

    // `object` in a JavaScript file is any without noImplicitAny (tsc's getTypeFromTypeNode of ObjectKeyword)
    if (identifier->name.source && identifier->name.source->is_js && !compiler_options()->no_implicit_any &&
        decl->decl_count && decl->decls[0].kind == SYMBOL_DECL_BUILTIN && csview_equals(decl->name, "object")
    ) {
        return ty_intrinsic(me, TY_ANY);
    }
    // A value in JSDoc means its type (tsc's getTypeFromJSDocValueReference)
    if (identifier->jsdoc && !(decl->flags & SYMBOL_TYPE) && (decl->flags & SYMBOL_VALUE)) {
        // A JavaScript variable or assignment declaration whose value is a class expression or a constructor function
        // is the class (tsc's getExpandoSymbol: the initializer's symbol merged in, which is a class). A bare
        // `import("./m")` whose `module.exports =` is one, `exports.C = class {}` too
        Expr* init = nullptr;
        SymbolDecl const* first = nullptr;
        for (u32 i = 0; i < decl->decl_count && !first; i++) {
            if (decl->decls[i].kind != SYMBOL_DECL_EXPANDO) first = &decl->decls[i];
        }
        // `exports.K = NS.K`: the assignment of an entity name is the symbol it names (tsc's exportAssignmentIsAlias)
        for (u32 depth = 0; depth < 8 && first && first->kind == SYMBOL_DECL_ASSIGNMENT && first->full.source &&
            first->full.source->is_js && !(decl->flags & SYMBOL_TYPE_ALIAS); depth++
        ) {
            Expr const* value = first->node;
            if (value->tag != EXPR_PROPERTY || !expr_is_entity_name(value)) break;
            Symbol const* named = expr_entity_name_symbol(value);
            if (!named || named == decl) break;
            decl = named;
            first = nullptr;
            for (u32 i = 0; i < decl->decl_count && !first; i++) {
                if (decl->decls[i].kind != SYMBOL_DECL_EXPANDO) first = &decl->decls[i];
            }
        }
        if (first && first->full.source && first->full.source->is_js && !(decl->flags & SYMBOL_TYPE_ALIAS)) {
            if (first->kind == SYMBOL_DECL_ASSIGNMENT) {
                init = expando_initializer((Expr*)first->node, span_null, true);
            } else if (first->kind == SYMBOL_DECL_VARIABLE) {
                Binding const* binding = first->node;
                if (!binding->parent && binding->tag == BINDING_IDENTIFIER) {
                    init = expando_initializer(&((InitializedBinding*)binding)->value, binding->content_span, true);
                }
            }
        }
        if (init) {
            Symbol const* class_symbol = init->tag == EXPR_CLASS_DECL ? init->class_decl->symbol :
                init->tag == EXPR_FUNCTION ? ty_js_constructor_class(init->function) : nullptr;
            if (class_symbol) return ty_class_instance_type(me, class_symbol);
        }
        for (u32 i = 0; i < my jsdoc_values_in_flight_count; i++) {
            if (my jsdoc_values_in_flight[i] == decl) return ty_intrinsic(me, TY_CIRCULAR);
        }
        if (my jsdoc_values_in_flight_count >= c_arraylen(my jsdoc_values_in_flight)) return ty_intrinsic(me, TY_ANY);
        my jsdoc_values_in_flight[my jsdoc_values_in_flight_count++] = decl;
        Ty const* type = ty_type_of_symbol(me, decl);
        my jsdoc_values_in_flight_count--;
        // `import("./m").C` of a value whose type is a class is the class (tsc's getTypeFromJSDocValueReference: the
        // type reference of the value type's symbol)
        if (span_valid(identifier->import_span) && span_valid(identifier->name) && type->kind == TY_DECLARED &&
            type->declared.kind == TY_DECLARED_CLASS_STATIC && canonical(me, type->declared.symbol) != decl
        ) {
            return ty_class_instance_type(me, type->declared.symbol);
        }
        return type;
    }

    if (identifier->this_kind == THIS_CLASS_STATIC) return ty_intrinsic(me, TY_ERROR); // TS2526: no `this` type in statics
    // Too many or too few type arguments (TS2314, TS2315, TS2707) make errorType outside JavaScript (tsc's
    // getTypeFromClassOrInterfaceReference), for a type alias in JavaScript too (getTypeFromTypeAliasReference)
    if (!identifier->keyword && span_valid(identifier->name) && identifier->target && identifier->name.source &&
        (!identifier->name.source->is_js || (identifier->target->flags & SYMBOL_TYPE_ALIAS)) &&
        !several_types_arity_fits(identifier->target, (u32)vec_spanned_type_size(&identifier->generic_params), identifier->name.source)
    ) {
        return ty_intrinsic(me, TY_ERROR);
    }

    Ty const* declared = ty_declared_type_of(me, decl);
    if (decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) {
            // `this` is the class's or interface's own `this` type parameter
            if (identifier->keyword == KEYWORD_IDENTIFIER_THIS && declared->kind == TY_DECLARED && declared->declared.this_param) {
                return declared->declared.this_param;
            }
            if (declared->kind == TY_DECLARED && declared->declared.type_params.len) {
                // TODO: `this` and a bare `C` inside generic class C are the class's own instantiation
                // (type arguments = its type parameters), and `this` is really its own type parameter
                if (identifier->keyword) {
                    if (ctx.generic) return ty_reference(me, declared, declared->declared.type_params);
                }
                TyList args = lower_type_args(
                    me, declared->declared.type_params, declared->declared.outer_count, &identifier->generic_params, ctx, args_buf
                );
                return ty_reference(me, declared, args);
            }
            return declared;
    }
    if (symbol_is_generic_alias(decl)) {
        TyList params = ty_type_params_of(me, symbol_type_generic(decl));
        TyList args = lower_type_args(me, params, 0, &identifier->generic_params, ctx, args_buf);
        if (declared == ty_intrinsic(me, TY_ANY) && args.len == 1) {
            Ty const* intrinsic = intrinsic_alias_instance(me, decl, args.data[0]);
            if (intrinsic) return intrinsic;
        }
        return alias_instance(me, decl, declared, params, args);
    }
    return ty_regular(me, declared); // `E.x` as a type: an enum member's declared type is fresh
}

// tsc's getInferredTypeParameterConstraint: an `infer X` without `extends` is constrained by where it is written. With
// several declarations (`A<infer X> | B<infer X>`), by all of them
Ty const* ty_implied_infer_constraint(Checker* me, Ty const* param) {
    Symbol const* symbol = param->param.symbol;
    Ty const* found[8];
    u32 len = 0;
    for (u32 i = 0; symbol && i < symbol->decl_count && len < c_arraylen(found); i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_INFER) continue;
        InferredType const* inferred = symbol->decls[i].node;
        Ty const* constraint = nullptr;
        switch (inferred->position) {
            case INFER_POSITION_OTHER: break;
            case INFER_POSITION_REST: constraint = ty_array(me, ty_intrinsic(me, TY_UNKNOWN_), false); break;
            case INFER_POSITION_TEMPLATE: constraint = ty_intrinsic(me, TY_STRING); break;
            case INFER_POSITION_MAPPED_KEY: constraint = ty_keyof(me, ty_intrinsic(me, TY_ANY)); break;
            case INFER_POSITION_TYPE_ARG: {
                IdentifierType const* reference = inferred->reference;
                Symbol const* decl = canonical(me, reference->target);
                if (!decl || span_valid(reference->import_span) || reference->keyword) break;
                TyList params = {0};
                u32 outer = 0;
                if (decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) {
                    Ty const* declared = ty_declared_type_of(me, decl);
                    if (declared->kind == TY_DECLARED) {
                        params = declared->declared.type_params;
                        outer = declared->declared.outer_count;
                    }
                } else if (symbol_is_generic_alias(decl)) {
                    params = ty_type_params_of(me, symbol_type_generic(decl));
                }
                if (outer + inferred->arg_index >= params.len) break;
                Ty const* declared_constraint = ty_param_constraint(me, params.data[outer + inferred->arg_index]);
                if (!declared_constraint) break;
                // It can mention the other type parameters, which are what the reference passes (`Foo<infer X,
                // infer X>` with `U extends T` gives X itself, which is no constraint)
                Ty const* args_buf[MAX_TYPE_ARGS];
                TyList args = lower_type_args(me, params, outer, &reference->generic_params, (LowerCtx){ .generic = true }, args_buf);
                constraint = ty_instantiate(me, declared_constraint, ty_mapper_new(me, params, args));
                if (constraint == param) constraint = nullptr;
            }   break;
        }
        if (constraint) found[len++] = constraint;
    }
    if (len <= 1) return len ? found[0] : nullptr;
    return ty_intersection(me, found, len, nullptr, (TyList){0});
}

// Free type parameters of a node, see Ty.anonymous.free_params

#define MAX_FREE_PARAMS 16
typedef struct FreeParams {
    Ty const* params[MAX_FREE_PARAMS];
    u32 len;
    bool unknown;
    // Type parameters declared inside the walked nodes (by a signature, a mapped type, `infer`). They aren't free,
    // and since nothing outside a node can mention what's declared inside it, they're simply taken out at the end
    Ty const* bound[MAX_FREE_PARAMS];
    u32 bound_len;
    // Inside the extends type of a conditional being walked, which is what declares its `infer` type parameters.
    // Walking a node inside an extends type on its own (`(...args: any) => infer R`), R is free
    u32 in_extends;
    // The true branches (TypeNode.flow) whose extends types were walked already (the extends types' own nodes are in
    // the outer true branches, so a single "last chain" memo would thrash between them)
    struct TernaryType const* flow_done[16];
    u32 flow_done_len;
} FreeParams;

static void free_params_bind(FreeParams* out, Ty const* param) {
    if (out->bound_len == MAX_FREE_PARAMS) {
        out->unknown = true;
    } else {
        out->bound[out->bound_len++] = param;
    }
}

static void free_params_add(FreeParams* out, Ty const* param) {
    for (u32 i = 0; i < out->len; i++) {
        if (out->params[i] == param) return;
    }
    if (out->len == MAX_FREE_PARAMS) {
        out->unknown = true;
    } else {
        out->params[out->len++] = param;
    }
}

static void collect_free_params(Checker* me, TypeNode const* node, FreeParams* out);
static void collect_free_params_of_function(Checker* me, FunctionImpl const* function, FreeParams* out);

static void collect_free_params_of_generic(Checker* me, Generic const* generic, FreeParams* out) {
    for_each (i, vec_generic_item, &generic->items) {
        if (i->symbol_ref) free_params_bind(out, ty_type_param(me, i->symbol_ref));
        collect_free_params(me, &i->extends, out);
        collect_free_params(me, &i->fallback, out);
    }
}

static void collect_free_params(Checker* me, TypeNode const* node, FreeParams* out) {
    if (out->unknown) return;
    if (node->flow) {
        // What the node is in the true branches around it can mention their extends types (a substitution type, see
        // conditional_flow_type()), which are outside the node (tsc's isTypeParameterPossiblyReferenced). Mapped type
        // templates add nothing: their key's implied constraint mentions no type parameter
        u32 saved_extends = out->in_extends;
        out->in_extends = 0;
        for (TypeFlow const* flow = node->flow; flow; flow = flow->outer) {
            if (!flow->ternary) continue;
            bool done = false;
            for (u32 i = 0; i < out->flow_done_len && !done; i++) done = out->flow_done[i] == flow->ternary;
            if (done) continue;
            if (out->flow_done_len < c_arraylen(out->flow_done)) out->flow_done[out->flow_done_len++] = flow->ternary;
            collect_free_params(me, &flow->ternary->maybe_super, out);
        }
        out->in_extends = saved_extends;
    }
    switch (node->tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_UNIQUE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
            break;

        case TYPE_IDENTIFIER: {
            IdentifierType const* identifier = node->identifier;
            Symbol const* decl = identifier->target;
            if (identifier->keyword) {
                // `this`: the class's or interface's own `this` type parameter, else (a function's `this`...) unknown
                Ty const* declared = decl && (decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) &&
                    identifier->keyword == KEYWORD_IDENTIFIER_THIS && identifier->this_kind != THIS_CLASS_STATIC ?
                    ty_declared_type_of(me, decl) : nullptr;
                if (declared && declared->kind == TY_DECLARED && declared->declared.this_param) {
                    free_params_add(out, declared->declared.this_param);
                } else {
                    out->unknown = true;
                }
            } else if (decl && decl->flags & SYMBOL_TYPE_PARAMETER) {
                free_params_add(out, ty_type_param(me, decl));
            } else if (decl && (decl->flags & SYMBOL_TYPE_ALIAS) && symbol_outer_generics(decl)) {
                // A local alias's body can mention the type parameters around it, which it doesn't take as type
                // arguments (tsc's isTypeParameterPossiblyReferenced: a node in a block may reference anything)
                TyList outer = ty_outer_type_params(me, symbol_outer_generics(decl));
                for (u32 i = 0; i < outer.len; i++) free_params_add(out, outer.data[i]);
            }
            for_each (i, vec_spanned_type, &identifier->generic_params) {
                collect_free_params(me, &i->type, out);
            }
        }   break;

        case TYPE_INFERRED:
            if (node->inferred->symbol_ref) {
                Ty const* param = ty_type_param(me, node->inferred->symbol_ref);
                if (out->in_extends) {
                    free_params_bind(out, param);
                } else {
                    free_params_add(out, param);
                }
            }
            collect_free_params(me, &node->inferred->extends, out);
            break;

        case TYPE_CALLABLE:
            collect_free_params_of_function(me, node->callable->function, out);
            break;

        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &node->interface.members) {
                switch (i->key.tag) {
                    case INTERFACE_KEY_NAME:
                    case INTERFACE_KEY_CALLABLE:
                        break;
                    case INTERFACE_KEY_EXPR:
                        // A computed name in a type has a literal or unique symbol type (TS1170): no type parameter
                        break;
                    case INTERFACE_KEY_INDEX:
                        collect_free_params(me, &i->key.index.type, out);
                        break;
                    case INTERFACE_KEY_MAPPED:
                        if (i->key.mapped.symbol_ref) {
                            free_params_bind(out, ty_type_param(me, i->key.mapped.symbol_ref));
                        }
                        collect_free_params(me, &i->key.mapped.in_type, out);
                        if (i->key.mapped.has_as_type) collect_free_params(me, &i->key.mapped.as_type, out);
                        break;
                }
                collect_free_params(me, &i->type, out);
            }
            break;

        case TYPE_ARRAY:
            collect_free_params(me, node->array.element, out);
            break;

        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &node->tuple.elements) {
                collect_free_params(me, &i->type, out);
            }
            break;

        case TYPE_INDEXED:
            collect_free_params(me, &node->indexed->interface, out);
            collect_free_params(me, &node->indexed->index, out);
            break;

        case TYPE_KEYOF:
            collect_free_params(me, &node->keyof->interface, out);
            break;

        case TYPE_TERNARY:
            collect_free_params(me, &node->ternary->maybe_sub, out);
            out->in_extends++;
            collect_free_params(me, &node->ternary->maybe_super, out);
            out->in_extends--;
            collect_free_params(me, &node->ternary->if_true, out);
            collect_free_params(me, &node->ternary->if_false, out);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                collect_free_params(me, i, out);
            }
            break;

        case TYPE_TEMPLATE:
            for_each (i, vec_type, &node->template_->types) {
                collect_free_params(me, i, out);
            }
            break;

        case TYPE_JSDOC: collect_free_params(me, &node->jsdoc->type, out); break;
        case TYPE_PREDICATE: break;

        case TYPE_TYPEOF:
            // Not syntax: the type of an expression, which could be about any type parameter in scope
            out->unknown = true;
            break;
    }
}

// A signature written as a type, or an overload signature. A parameter without a type is `any`
static void collect_free_params_of_function(Checker* me, FunctionImpl const* function, FreeParams* out) {
    if (function->jsdoc_callback) out->unknown = true;
    collect_free_params_of_generic(me, &function->generic, out);
    for_each (i, vec_function_arg, &function->args) {
        if (i->initializer.binding.has_explicit_type) collect_free_params(me, &i->initializer.binding.explicit_type, out);
        // Not syntax: the type of an expression, which could be about any type parameter in scope
        if (i->initializer.value.tag != EXPR_IMPLICIT_ANY) out->unknown = true;
    }
    if (function->has_body && !function->ret_explicit) out->unknown = true;
    collect_free_params(me, &function->ret, out);
    if (function->predicate && function->predicate->has_type) collect_free_params(me, &function->predicate->type, out);
}

// Only when some type parameter is in scope (ctx.generic), otherwise there's nothing to find
static void anonymous_set_free_params(Checker* me, Ty* ty, FreeParams found) {
    u32 kept = 0;
    for (u32 i = 0; i < found.len; i++) {
        bool bound = false;
        for (u32 j = 0; j < found.bound_len; j++) bound |= found.bound[j] == found.params[i];
        if (!bound) found.params[kept++] = found.params[i];
    }
    found.len = kept;
    ty->anonymous.free_params_unknown = found.unknown;
    if (!found.unknown) ty->anonymous.free_params = ty_list_copy(me, found.params, found.len);
    if (found.unknown || found.len) ty->flags |= TYF_COULD_CONTAIN_TYPE_VARIABLES;
}

static TyConditionalOuter* collect_outer_params(Checker* me, TypeNode const* node) {
    FreeParams found = {0};
    collect_free_params(me, node, &found);
    u32 kept = 0;
    for (u32 i = 0; i < found.len; i++) {
        bool bound = false;
        for (u32 j = 0; j < found.bound_len; j++) bound |= found.bound[j] == found.params[i];
        if (!bound) found.params[kept++] = found.params[i];
    }
    TyConditionalOuter* outer = arena_new(&my arena, TyConditionalOuter);
    outer->unknown = found.unknown;
    if (!found.unknown) outer->params = ty_list_copy(me, found.params, kept);
    return outer;
}

static void conditional_set_outer(Checker* me, Ty const* root, TypeNode const* node) {
    u64 key[2] = { KEY_CONDITIONAL_OUTER, root->id };
    if (intern_get(&my links, key, 2)) return;
    TyConditionalOuter* outer = collect_outer_params(me, node);
    *intern_slot(me, &my links, key, 2) = outer;
}

// The type parameters declared outside a deferred reference's node that it mentions: what its instantiations are
// keyed by (tsc's getObjectTypeInstantiation keys by the outer type parameters the node references)
static TyConditionalOuter const* deferred_node_outer(Checker* me, TypeNode const* node) {
    u64 key[2] = { KEY_DEFERRED_OUTER, (u64)(uintptr_t)node };
    TyConditionalOuter const* ret = intern_get(&my links, key, 2);
    if (ret) return ret;
    TyConditionalOuter* outer = collect_outer_params(me, node);
    *intern_slot(me, &my links, key, 2) = outer;
    return outer;
}

TyConditionalOuter const* ty_conditional_outer(Checker* me, Ty const* root) {
    u64 key[2] = { KEY_CONDITIONAL_OUTER, root->id };
    return intern_get(&my links, key, 2);
}

Ty const* ty_anonymous_from_node(Checker* me, TypeNode const* node, LowerCtx ctx, Symbol const* alias, TyList alias_args) {
    check(node->tag == TYPE_INTERFACE || node->tag == TYPE_CALLABLE);
    ctx.in_alias = false; // Members are resolved lazily, which is what makes recursion through them legal
    u64 key[2] = { KEY_NODE, (u64)(uintptr_t)node };
    Ty const* existing = intern_get(&my links, key, 2);
    if (existing) return existing;

    Ty* ret = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT);
    ret->anonymous.node = node;
    ret->anonymous.ctx = ctx;
    ret->anonymous.state = arena_new(&my arena, TyObjectState);
    ret->alias = alias;
    ret->alias_args = alias_args;
    if (ctx.generic) {
        FreeParams found = {0};
        collect_free_params(me, node, &found);
        anonymous_set_free_params(me, ret, found);
        // (In JS, a type node is JSDoc, see Ty.anonymous.jsdoc_references_all)
        Span span = node->tag == TYPE_CALLABLE ? node->callable->function->args_span : node->interface.span;
        ret->anonymous.jsdoc_references_all = span.source && span.source->is_js;
    }
    // Collecting can intern type parameters, so the slot is looked up again
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

Ty const* ty_anonymous_from_overloads(Checker* me, FunctionImpl const* const* overloads, u32 count, LowerCtx ctx) {
    ctx.in_alias = false;
    u64 key[2] = { KEY_NODE, (u64)(uintptr_t)overloads };
    Ty const* existing = intern_get(&my links, key, 2);
    if (existing) return existing;

    Ty* ret = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT);
    ret->anonymous.overloads = overloads;
    ret->anonymous.overload_count = count;
    ret->anonymous.ctx = ctx;
    ret->anonymous.state = arena_new(&my arena, TyObjectState);
    if (ctx.generic) {
        FreeParams found = {0};
        for (u32 i = 0; i < count; i++) collect_free_params_of_function(me, overloads[i], &found);
        anonymous_set_free_params(me, ret, found);
    }
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

static Ty* make_namespace(Checker* me, Symbol const* symbol) {
    Ty* ret = ty_alloc(me, TY_DECLARED, TYF_OBJECT);
    ret->declared.kind = TY_DECLARED_NAMESPACE;
    ret->declared.namespace_symbol = symbol;
    if (symbol->is_module) {
        // `typeof import("a")`. `import * as ns` has a symbol of its own (make_gather_target())
        cstr module_name = module_name_for_error(symbol, nullptr, false);
        cstr name = cstr_d("import(", module_name, ")");
        ret->declared.name = checker_atom(me, cstr_sv(&name));
        cstr_drop(&name);
        cstr_drop(&module_name);
    } else {
        ret->declared.name = ty_qualified_name(me, symbol);
    }
    ret->declared.state = arena_new(&my arena, TyObjectState);
    return ret;
}

static Ty const* merged_export_equals_type(Checker* me, Symbol const* clone, bool in_file);

Ty const* ty_commonjs_exports_type(Checker* me, SourceFile const* file) {
    Symbol* module = file && file->scope ? file->scope->container : nullptr;
    if (!module) return ty_intrinsic(me, TY_ANY);
    Symbol* assigned = module_export_equals(module);
    Ty const* exports;
    if (assigned != module) {
        exports = assigned->merged_export_equals_of ? merged_export_equals_type(me, assigned, true) : ty_type_of_symbol(me, assigned);
    } else if (symbol_table_get(module->exports, c_sv("export="))) {
        // `module.exports = window.nonprop`: an alias that resolves to nothing (tsc's unknownSymbol). One of the
        // module itself (`module.exports = exports` in a function) is the module's namespace, `typeof import("m")`
        bool itself = symbol_resolve_alias(symbol_table_get(module->exports, c_sv("export="))) == module;
        exports = itself ? ty_namespace_type(me, module) : ty_intrinsic(me, TY_ERROR);
    } else {
        // The module's namespace object under the name `module.exports` (tsc gives `module` a copy of the file's
        // symbol by that name)
        u64 key[2] = { KEY_COMMONJS_EXPORTS, (u64)(uintptr_t)module };
        exports = intern_get(&my links, key, 2);
        if (!exports) {
            Ty* made = make_namespace(me, module);
            made->declared.name = checker_atom(me, c_sv("module.exports"));
            made->declared.commonjs_exports = true;
            *intern_slot(me, &my links, key, 2) = made;
            exports = made;
        }
    }
    return exports;
}

// The `module` of a CommonJS file: `{ exports: <what the module exports> }` (tsc builds it in
// getTypeOfVariableOrParameterOrPropertyWorker for SymbolFlags.ModuleExports)
Ty const* ty_commonjs_module_object(Checker* me, SourceFile const* file) {
    Symbol* module = file->scope ? file->scope->container : nullptr;
    if (!module) return ty_intrinsic(me, TY_ANY);
    Span* file_span = arena_new(&my arena, Span);
    *file_span = (Span){ .source = file, .sv = csview_from_n(source_file_start(file), 0) };
    Ty const* exports = ty_commonjs_exports_type(me, file);
    ObjectLiteralProp prop = {
        .name = checker_atom(me, c_sv("exports")),
        .type = exports,
        .name_span = *file_span,
        .decl_spans = file_span,
        .decl_span_count = 1,
    };
    return ty_object_literal(me, &prop, 1, nullptr, 0, OBJECT_SYNTHETIC);
}

// The value of a namespace (or of a module, through `import * as ns`): a property per value it declares
Ty const* ty_namespace_type(Checker* me, Symbol const* symbol) {
    u64 key[2] = { KEY_NAMESPACE, (u64)(uintptr_t)symbol };
    Ty const* existing = intern_get(&my links, key, 2);
    if (existing) return existing;
    Ty const* ret = make_namespace(me, symbol);
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

// Whether a declaration of a symbol adds something to what the symbol is as a value
static bool decl_contributes_value(Symbol const* symbol, u32 index) {
    SymbolDecl const* decl = &symbol->decls[index];
    if (!(decl->flags & SYMBOL_VALUE)) return false;
    u32 once = 0; // Of these only the first counts
    switch (decl->kind) {
        case SYMBOL_DECL_VARIABLE:
        case SYMBOL_DECL_BUILTIN:
        case SYMBOL_DECL_EXPR:
            once = SYMBOL_VARIABLE; // `var a; var a;`, a parameter and a `var`: the first one gives the type. TODO: TS2403
            break;
        case SYMBOL_DECL_FUNCTION:
            // The implementation of overloads isn't a signature: an implementation right after another declaration
            // of the function (tsc's getSignaturesOfSymbol: same parent and kind, nothing but trivia between). Others,
            // like duplicate implementations in two files, are signatures of their own
            if (((FunctionImpl const*)decl->node)->has_body && index > 0) {
                SymbolDecl const* previous = &symbol->decls[index - 1];
                if (previous->kind != SYMBOL_DECL_FUNCTION || previous->full.source != decl->full.source ||
                    !span_valid(previous->full) || !span_valid(decl->full)
                ) {
                    return true;
                }
                char const* between = span_end_ptr(previous->full);
                char const* start = span_start_ptr(decl->full);
                if (between > start) return true;
                while (between < start) {
                    if (*between == ' ' || *between == '\t' || *between == '\r' || *between == '\n' || *between == ';') {
                        between++;
                    } else if (between[0] == '/' && between[1] == '/') {
                        while (between < start && *between != '\n') between++;
                    } else if (between[0] == '/' && between[1] == '*') {
                        char const* close = strstr(between + 2, "*/");
                        between = close ? close + 2 : start;
                    } else {
                        return true;
                    }
                }
                return false;
            }
            break;
        case SYMBOL_DECL_ENUM: once = SYMBOL_ENUM; break; // One object for every declaration of the enum
        case SYMBOL_DECL_NAMESPACE: once = SYMBOL_VALUE_MODULE; break; // And one for every block of a namespace
        case SYMBOL_DECL_ASSIGNMENT:
            // An expando of a namespace's variable (`namespace N { export var x }` and `N.x = 1`) is assigned to it
            for (u32 i = 0; i < symbol->decl_count; i++) {
                SymbolDeclKind kind = symbol->decls[i].kind;
                if (kind != SYMBOL_DECL_ASSIGNMENT && kind != SYMBOL_DECL_EXPANDO && (symbol->decls[i].flags & SYMBOL_VALUE)) return false;
            }
            break;
        case SYMBOL_DECL_EXPANDO:
            // A variable's or an assignment's expandos are members of its initializer
            if (symbol->flags & SYMBOL_VARIABLE) return false;
            for (u32 i = 0; i < symbol->decl_count; i++) {
                if (symbol->decls[i].kind == SYMBOL_DECL_ASSIGNMENT) return false;
            }
            once = SYMBOL_VALUE_MODULE;
            break;
        default: break;
    }
    for (u32 i = 0; i < index; i++) {
        if (symbol->decls[i].flags & once) return false;
    }
    return true;
}

// `const f: T = () => {}; f.x = v`: an expando of an annotated variable has the type the annotation gives its name
// (tsc's getAnnotatedTypeForAssignmentDeclaration), null if the annotation doesn't have it
static Ty const* annotated_expando_type(Checker* me, Symbol const* symbol) {
    Symbol const* host = symbol->parent;
    if (!host || host->is_module || !(host->flags & SYMBOL_VARIABLE)) return nullptr;
    Binding const* binding = symbol_find_node(host, SYMBOL_DECL_VARIABLE);
    if (!binding || binding->parent || binding->tag != BINDING_IDENTIFIER || !binding->has_explicit_type) return nullptr;
    Ty const* annotation = ty_binding_type(me, binding, (LowerCtx){ .generic = true });
    // (An optional property's type is read with its undefined: tsc's getTypeOfPropertyOfType)
    return ty_access(me, annotation, ty_string_literal(me, symbol->name));
}

// What a declaring assignment (`exports.x = v`, `f.x = v`) assigns, before widening object literals (tsc's
// getInitializerTypeFromAssignmentDeclaration): an export keeps its literal type, an expando's is widened
// `Object.defineProperty(o, "x", descriptor)`: the descriptor's `value`, else its getter's return type, else its setter's
// parameter type, else any (tsc's getInitializerTypeFromAssignmentDeclaration for a call)
static Ty const* define_property_type(Checker* me, Expr const* call) {
    Ty const* descriptor = ty_expr_type(me, vec_expr_at(&call->call->args, 2));
    Ty const* value = ty_prop_type_of(me, descriptor, checker_atom(me, c_sv("value")));
    if (value) return value;
    Ty const* get = ty_prop_type_of(me, descriptor, checker_atom(me, c_sv("get")));
    Signature const* get_sig = get ? ty_single_signature(me, get, false, true) : nullptr;
    if (get_sig) return signature_return_type(me, get_sig);
    Ty const* set = ty_prop_type_of(me, descriptor, checker_atom(me, c_sv("set")));
    Signature const* set_sig = set ? ty_single_signature(me, set, false, true) : nullptr;
    if (set_sig) {
        signature_resolve(me, set_sig);
        return set_sig->param_count ? ty_signature_param_type_at(me, set_sig, 0) : ty_intrinsic(me, TY_NEVER);
    }
    return ty_intrinsic(me, TY_ANY);
}

static Ty const* assignment_declaration_type(Checker* me, Symbol const* symbol, SymbolDecl const* decl) {
    if (decl->assignment == ASSIGNMENT_DECL_DEFINE_PROPERTY) return define_property_type(me, decl->node);
    Ty const* annotated = annotated_expando_type(me, symbol);
    if (annotated) return annotated;
    // A declaration that assigns nothing (`/** @type {T} */ this["x"];` whose tag isn't its): never, so any in the end
    // (tsc's getWidenedTypeForAssignmentDeclaration pushes neverType for it)
    Expr const* node = decl->node;
    if (node && node->tag == EXPR_PROPERTY && span_eq(node->outer_span, decl->full)) return ty_intrinsic(me, TY_NEVER);
    Ty const* type = ty_expr_type(me, decl->node);
    // `[]` is any[] in JavaScript, which noImplicitAny reports (tsc's getInitializerTypeFromAssignmentDeclaration:
    // isEmptyArrayLiteralType, see ty_js_assignment_implicit_any())
    SourceFile const* file = decl->full.source;
    TyList args = file && file->is_js && ty_is_array(me, type) ? ty_reference_args(me, type) : (TyList){0};
    Ty const* element = args.len == 1 ? args.data[0] : nullptr;
    if (element && element == ty_intrinsic(me, compiler_options()->strict_null_checks ? TY_IMPLICIT_NEVER : TY_UNDEFINED_WIDENING)) {
        *intern_slot(me, &my links, (u64[]){ KEY_JS_IMPLICIT_ANY, (u64)(uintptr_t)decl->node }, 2) = decl->node;
        return ty_array(me, ty_intrinsic(me, TY_ANY), false);
    }
    // (A direct export keeps its literal type, tsc's isDirectExport; a global declared by `this.x = v` doesn't, nor an
    // export through another name of `module.exports`)
    bool direct = symbol->parent && symbol->parent->is_module && decl->assignment != ASSIGNMENT_DECL_EXPORTS_ALIAS_PROPERTY;
    return direct ? ty_regular(me, type) : ty_widened_literal(me, type);
}

// JavaScript `var ns = {}` or `ns.a = {}` with expandos (`ns.x = 1`): the members they declare (tsc's
// getJSContainerObjectType). `value` is the initializer or assigned value
static bool is_js_container(Symbol const* symbol, Expr const* value, Span name) {
    if (!symbol->exports || !value) return false;
    SourceFile const* file = value->inner_span.source;
    if (!file || !file->is_js) return false;
    Expr const* init = expando_initializer((Expr*)value, name, true);
    return init && init->tag == EXPR_OBJECT;
}

static Ty const* value_entry_type(Checker* me, Symbol const* symbol, SymbolDecl const* decl) {
    switch (decl->kind) {
        case SYMBOL_DECL_VARIABLE: {
            Binding const* binding = decl->node;
            if (!binding->parent && binding->tag == BINDING_IDENTIFIER && !binding->has_explicit_type &&
                is_js_container(symbol, &((InitializedBinding const*)binding)->value, binding->content_span)
            ) {
                return ty_namespace_type(me, symbol);
            }
            Ty const* type = ty_binding_type(me, binding, (LowerCtx){ .generic = true });
            // `x?: T` is read as `T | undefined` inside the function. TODO: tsc also shows it in the signature
            if (binding->optional_parameter && compiler_options()->strict_null_checks && !(type->flags & TYF_ANY)) {
                Ty const* types[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
                type = ty_union(me, types, 2, nullptr, (TyList){0});
            }
            return type;
        }
        case SYMBOL_DECL_BUILTIN: return ty_from_node(me, decl->node, (LowerCtx){0});
        // `export default e` (exportDefaultStripsFreshness)
        case SYMBOL_DECL_EXPR:
            // (A JSDoc `@type` on it is its type: tsc's getEffectiveTypeAnnotationNode of an ExportAssignment)
            if (decl->jsdoc_type) return ty_of_node(decl->jsdoc_type);
            // A JSON file's value is widened, literals too (getTypeOfVariableOrParameterOrPropertyWorker)
            if (decl->full.source && decl->full.source->is_json) return ty_widened_for_declaration(me, ty_expr_type(me, decl->node), true);
            return ty_regular(me, ty_widened_objects(me, ty_expr_type(me, decl->node)));
        case SYMBOL_DECL_ASSIGNMENT:
            if (is_js_container(symbol, decl->node, span_null)) return ty_namespace_type(me, symbol);
            // tsc's getInitializerTypeFromAssignmentDeclaration: `exports.x = 1` exports `1`, `module.exports = 1` number
            if (csview_equals(symbol->name, "export=")) return ty_widened_for_declaration(me, ty_expr_type(me, decl->node), true);
            if (annotated_expando_type(me, symbol)) return annotated_expando_type(me, symbol);
            {
                Ty const* type = ty_widened_objects(me, assignment_declaration_type(me, symbol, decl));
                // Nothing but null and undefined is any in JavaScript (tsc's getWidenedTypeForAssignmentDeclaration),
                // except `= undefined`, an alias of undefined to tsc (isAliasableExpression)
                Expr const* value = decl->node;
                SourceFile const* file = decl->full.source;
                bool undefined_alias = value->tag == EXPR_LITERAL && value->literal.tag == VALUE_UNDEFINED;
                if (file && file->is_js && !undefined_alias && decl->assignment != ASSIGNMENT_DECL_DEFINE_PROPERTY &&
                    ty_non_nullable(me, type)->flags & TYF_NEVER
                ) {
                    *intern_slot(me, &my links, (u64[]){ KEY_JS_IMPLICIT_ANY, (u64)(uintptr_t)symbol }, 2) = symbol;
                    return ty_intrinsic(me, TY_ANY);
                }
                return type;
            }
        case SYMBOL_DECL_COMMONJS_MODULE: return ty_commonjs_module_object(me, decl->node);
        case SYMBOL_DECL_FUNCTION: return lazy_type_get((LazyType*)&((FunctionImpl const*)decl->node)->lazy_type);
        case SYMBOL_DECL_CLASS: {
            // A local class in a generic scope: applied to the outer type parameters, so that instantiating it with
            // the function's mapper instantiates its statics
            Ty const* static_type = ty_static_type_of(me, symbol);
            TyList outer = static_type->declared.type_params;
            Ty const* value = outer.len ? ty_reference(me, static_type, outer) : static_type;
            // A mixin class is also what it extends: `typeof C & T` (tsc's getTypeOfFuncClassEnumModule)
            Ty const* variable = ty_class_base_type_variable(me, symbol);
            if (variable) {
                Ty const* parts[2] = { value, variable };
                value = ty_intersection(me, parts, 2, nullptr, (TyList){0});
            }
            return value;
        }
        case SYMBOL_DECL_ENUM:
            return ty_static_type_of(me, symbol);
        case SYMBOL_DECL_ENUM_MEMBER: return ty_declared_type_of(me, symbol);
        case SYMBOL_DECL_NAMESPACE:
        case SYMBOL_DECL_EXPANDO:
        case SYMBOL_DECL_ALIAS: // `import * as ns`
            return ty_namespace_type(me, symbol);
        case SYMBOL_DECL_EXPORT_EQUALS_PROPERTY: {
            Ty const* type = ty_prop_type_of(me, ty_type_of_symbol(me, decl->node), checker_atom(me, symbol->name));
            return type ? type : ty_intrinsic(me, TY_ERROR);
        }
        default:
            return ty_intrinsic(me, TY_ERROR);
    }
}

// What a symbol is as a value, one type per declaration that contributes to it. malloc'd
static u32 value_entries(Checker* me, Symbol const* symbol, Ty const*** out) {
    u32 count = 0;
    *out = malloc(sizeof(Ty const*) * (symbol->decl_count + 1));
    panic_if(!*out, "out of memory");
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (decl_contributes_value(symbol, i)) (*out)[count++] = value_entry_type(me, symbol, &symbol->decls[i]);
    }
    return count;
}

// The properties of an intersection as tsc's getPropertiesOfType lists them: those of all of its members, each name
// once. One only a single member has is that member's (tsc's createUnionOrIntersectionProperty returns the single
// symbol), others of the type and with the modifiers the intersection has for them. malloc'd
static u32 intersection_props(Checker* me, Ty const* ty, ObjectLiteralProp** out) {
    u32 count = 0, cap = 0;
    *out = nullptr;
    for (u32 p = 0; p < ty->composite.types.len; p++) {
        Ty const* part = ty_apparent_type(me, ty->composite.types.data[p]);
        if (!(part->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, part);
        for (u32 i = 0; i < members->prop_count; i++) {
            Prop const* prop = members->props[i];
            bool seen = false;
            for (u32 k = 0; k < count && !seen; k++) seen = (*out)[k].name == prop->name;
            if (seen) continue;
            u32 owners = 0;
            for (u32 q = 0; q < ty->composite.types.len; q++) {
                Ty const* other = ty_apparent_type(me, ty->composite.types.data[q]);
                owners += (other->flags & TYF_OBJECT) && ty_get_prop(me, other, prop->name);
            }
            if (count == cap) {
                cap = cap ? cap * 2 : 8;
                *out = realloc(*out, sizeof(ObjectLiteralProp) * cap);
                panic_if(!*out, "out of memory");
            }
            if (owners == 1) {
                (*out)[count++] = (ObjectLiteralProp){ .name = prop->name, .reuse = prop };
                continue;
            }
            FoundProp found = ty_find_prop(me, ty, prop->name);
            (*out)[count++] = (ObjectLiteralProp){
                .name = prop->name, .flags = found.flags, .type = found.type, .name_span = prop->decl->name_span,
                .name_form = ty_prop_name_form_of(prop),
            };
        }
    }
    return count;
}

// `{ default: T }`, with T's own properties too if `spread` (tsc's createDefaultPropertyWrapperForModule, and
// getSpreadType of the type with it in getTypeWithSyntheticDefaultImportType)
static Ty const* with_synthetic_default(Checker* me, Ty const* type, bool spread) {
    Atom const* default_name = checker_atom(me, c_sv("default"));
    ObjectLiteralProp* from = nullptr;
    u32 from_count = 0;
    Members const* members = spread && (type->flags & (TYF_OBJECT | TYF_INTERSECTION)) ? ty_members(me, type) : nullptr;
    if (members && type->kind == TY_INTERSECTION) {
        from_count = intersection_props(me, type, &from);
    } else if (members) {
        from = malloc(sizeof(ObjectLiteralProp) * (members->prop_count + 1));
        panic_if(!from, "out of memory");
        for (u32 i = 0; i < members->prop_count; i++) from[from_count++] = (ObjectLiteralProp){ .reuse = members->props[i] };
    }
    ObjectLiteralProp* props = arena_new_n(&my arena, ObjectLiteralProp, from_count + 1);
    props[0] = (ObjectLiteralProp){ .name = default_name, .type = type };
    u32 n = 1;
    for (u32 i = 0; i < from_count; i++) {
        Atom const* name = from[i].reuse ? from[i].reuse->name : from[i].name;
        if (name != default_name) props[n++] = from[i];
    }
    free(from);
    return ty_object_literal(me, props, n, members ? members->index : nullptr, members ? members->index_count : 0,
        OBJECT_SYNTHETIC);
}

// `import * as ns` of a module with `export =` (tsc's resolveESModuleSymbol): what it assigns, with a synthetic
// `default` under esModuleInterop when that is something to import by default; an ES module's import of a JSON
// module has nothing but the default (isOnlyImportableAsDefault). Null if it is the assigned value as it is
static Ty const* namespace_import_type(Checker* me, Symbol const* symbol) {
    if (!symbol || !(symbol->flags & SYMBOL_ALIAS) || !symbol->alias || !symbol->alias->item) return nullptr;
    PortItem const* item = symbol->alias->item;
    if (item->tag != PORT_IMPORT_GATHER || item->is_require || !item->module_ref) return nullptr;
    Symbol* module = binder_module_of_ref(symbol->binder, item->module_ref);
    Symbol* assigned = module ? module_export_equals(module) : nullptr;
    if (!assigned || assigned == module || assigned->is_placeholder) return nullptr;
    CompilerOptions const* options = compiler_options();
    SourceFile const* importer = item->decl_name.source;
    SourceFile const* target = item->module_ref->file;
    bool node = options->module >= MODULE_KIND_NODE16 && options->module <= MODULE_KIND_NODENEXT;
    bool esm_usage = node && importer && importer->is_esm;
    Ty const* type = ty_type_of_symbol(me, assigned);
    if (type->flags & TYF_ERROR) return nullptr;
    if (esm_usage && target && target->is_json) return with_synthetic_default(me, type, false);
    bool esm_cjs = esm_usage && target && !target->is_esm && !target->is_json;
    if (!options->es_module_interop && !esm_cjs) return nullptr;
    Ty const* apparent = ty_apparent_type(me, type);
    // (tsc's getSignaturesOfStructuredType: an intersection has the signatures of its members)
    Members const* members = (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) ? ty_members(me, apparent) : nullptr;
    bool callable = members && (members->call_count || members->construct_count);
    bool has_default = members && ty_get_prop(me, apparent, checker_atom(me, c_sv("default")));
    if (!callable && !has_default && !esm_cjs) return nullptr;
    // A module that says it's an ES module has no synthetic default (tsc's canHaveSyntheticDefault)
    bool synthetic = !symbol_get_export(module, c_sv("__esModule"));
    bool structured = type->flags & (TYF_OBJECT | TYF_UNION | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED);
    if (!structured) return with_synthetic_default(me, type, false);
    return synthetic ? with_synthetic_default(me, type, true) : nullptr;
}

Ty const* ty_namespace_import_original(Checker* me, Symbol const* alias) {
    if (!namespace_import_type(me, alias)) return nullptr;
    return ty_type_of_symbol(me, module_export_equals(binder_module_of_ref(alias->binder, alias->alias->item->module_ref)));
}

bool ty_is_constructor_declared(Symbol const* symbol) {
    bool in_constructor = false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (decl->kind != SYMBOL_DECL_ASSIGNMENT || decl->jsdoc_type) return false;
        // (`/** @type {T} */ this["x"];` declares without assigning)
        Expr const* node = decl->node;
        if (node && node->tag == EXPR_PROPERTY && span_eq(node->outer_span, decl->full)) return false;
        if (decl->assignment != ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR && decl->assignment != ASSIGNMENT_DECL_THIS_IN_METHOD) {
            return false;
        }
        in_constructor |= decl->assignment == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR;
    }
    return in_constructor;
}

// tsc's getTypeOfPropertyInBaseClass: the type of a class member's namesake in the base class, null if none
static Ty const* base_class_property_type(Checker* me, Symbol const* member) {
    ClassDecl const* class_decl = member->parent ? symbol_find_node(member->parent, SYMBOL_DECL_CLASS) : nullptr;
    Ty const* base = class_decl ? ty_class_base_instance(me, class_decl) : nullptr;
    return base ? ty_prop_type_of(me, base, checker_atom(me, member->name)) : nullptr;
}

// What `this.x` is at `flow` in the class constructor that declares x by assignment (tsc's getFlowTypeOfProperty: it
// starts out as the base class's x, else undefined, auto-typed from there)
Ty const* ty_constructor_declared_flow_type(Checker* me, Symbol const* member, Expr const* reference, FlowNode const* flow) {
    Ty const* base = base_class_property_type(me, member);
    Ty const* ret = ty_flow_type(me, reference, ty_intrinsic(me, TY_AUTO), base ? base : ty_intrinsic(me, TY_UNDEFINED), flow);
    return ret == ty_intrinsic(me, TY_AUTO) ? ty_intrinsic(me, TY_ANY) : ret;
}

// A member only `this.x = v` assignments declare, one of them in a class constructor: the type `this.x` has at the end of
// the constructor (tsc's getFlowTypeInConstructor). Null if that is only null or undefined, or it isn't such a member
static Ty const* flow_type_in_constructor(Checker* me, Symbol const* symbol) {
    if (!symbol->constructor_assigned || !symbol->declaring_constructor || !symbol->declaring_constructor->return_flow) {
        return nullptr;
    }
    Ty const* ret = ty_constructor_declared_flow_type(me, symbol, symbol->constructor_assigned, symbol->declaring_constructor->return_flow);
    // Nothing but `[]` (tsc's autoArrayType, which never evolves here): any[], reported (getFlowTypeInConstructor)
    SourceFile const* file = symbol->constructor_assigned->inner_span.source;
    if (file && file->is_js && ret == ty_auto_array(me)) {
        *intern_slot(me, &my links, (u64[]){ KEY_JS_IMPLICIT_ANY, (u64)(uintptr_t)symbol }, 2) = (void const*)(uintptr_t)3;
        return ty_array(me, ty_intrinsic(me, TY_ANY), false);
    }
    TyList parts = ret->kind == TY_UNION ? ret->composite.types : (TyList){ .data = &ret, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        if (!(parts.data[i]->flags & TYF_NULLABLE)) return ret;
    }
    return nullptr;
}

// tsc's getWidenedTypeForAssignmentDeclaration: the union of what the assignments assign. A member `this.x = v`
// declares in a class constructor has the type the constructor gives it (flow_type_in_constructor()), else (a
// constructor function's) the union of the constructor's assignments. TODO: a JSDoc type on one of them is the type
static Ty const* assignment_symbol_type(Checker* me, Symbol const* symbol) {
    if (is_js_container(symbol, symbol->decls[0].node, span_null)) return ty_namespace_type(me, symbol);
    // A JavaScript function or class expression assigned first is the type, whatever the later assignments are (tsc's
    // getWidenedTypeForAssignmentDeclaration: an expando initializer of the value declaration is a container of its own)
    SourceFile const* first_file = symbol->decls[0].full.source;
    if (first_file && first_file->is_js && symbol->decl_count > 1 && symbol->decls[0].kind == SYMBOL_DECL_ASSIGNMENT &&
        !symbol->decls[0].jsdoc_type && symbol->decls[0].assignment != ASSIGNMENT_DECL_DEFINE_PROPERTY
    ) {
        Expr* init = expando_initializer((Expr*)symbol->decls[0].node, span_null, true);
        if (init && (init->tag == EXPR_FUNCTION || init->tag == EXPR_CLASS_DECL)) {
            return ty_widened_literal(me, ty_expr_type(me, init));
        }
    }
    // The first JSDoc `@type` of an assignment is the type (tsc's getAnnotatedTypeForAssignmentDeclaration)
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind == SYMBOL_DECL_ASSIGNMENT && symbol->decls[i].jsdoc_type) return ty_of_node(symbol->decls[i].jsdoc_type);
    }
    Ty const* stack[16];
    Ty const** types = symbol->decl_count <= c_arraylen(stack) ? stack : malloc(sizeof(Ty const*) * symbol->decl_count);
    bool constructor_declared = ty_is_constructor_declared(symbol);
    Ty const* in_constructor_type = constructor_declared ? flow_type_in_constructor(me, symbol) : nullptr;
    if (in_constructor_type) {
        if (types != stack) free(types);
        // (Widened like any assignment declaration's type: tsc's getWidenedTypeForAssignmentDeclaration)
        return ty_widened_objects(me, ty_widened_literal(me, in_constructor_type));
    }
    bool in_constructor = false, in_method = false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        in_constructor |= symbol->decls[i].assignment == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR;
        in_method |= symbol->decls[i].assignment == ASSIGNMENT_DECL_THIS_IN_METHOD;
    }
    // The constructor's assignments, unless they only assign null or undefined
    bool constructor_only = false;
    if (in_constructor) {
        for (u32 i = 0; i < symbol->decl_count && !constructor_only; i++) {
            if (symbol->decls[i].assignment != ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR) continue;
            constructor_only = !(ty_widened_for_declaration(me, ty_expr_type(me, symbol->decls[i].node), true)->flags & TYF_NULLABLE);
        }
    }
    u32 count = 0;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (decl->kind != SYMBOL_DECL_ASSIGNMENT) continue;
        if (constructor_only && decl->assignment != ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR) continue;
        types[count++] = csview_equals(symbol->name, "export=") ? value_entry_type(me, symbol, decl) :
            assignment_declaration_type(me, symbol, decl);
    }
    // (Object literals are widened together: the union has their properties as optional in each other)
    Ty const* ret = ty_widened_objects(me, ty_union(me, types, count, nullptr, (TyList){0}));
    if (types != stack) free(types);
    // Only methods assign it: what a base class declares it as, else possibly undefined
    Ty const* base_type = in_method && !in_constructor ? base_class_property_type(me, symbol) : nullptr;
    if (base_type && !(base_type->flags & TYF_NULLABLE)) {
        ret = base_type;
    } else if (in_method && !in_constructor && compiler_options()->strict_null_checks) {
        Ty const* parts[2] = { ret, ty_intrinsic(me, TY_UNDEFINED) };
        ret = ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    // Nothing but null and undefined is any in JavaScript
    SourceFile const* file = symbol->decls[0].full.source;
    if (file && file->is_js && ty_non_nullable(me, ret)->flags & TYF_NEVER) {
        *intern_slot(me, &my links, (u64[]){ KEY_JS_IMPLICIT_ANY, (u64)(uintptr_t)symbol }, 2) = (void const*)(uintptr_t)2;
        return ty_intrinsic(me, TY_ANY);
    }
    return ret;
}

// Whether the type of a JavaScript assignment declaration's member became any implicitly (tsc's reportImplicitAny in
// getWidenedTypeForAssignmentDeclaration): 1 if `value` (an assignment of `[]`) made it any[], 2 if only null or
// undefined are assigned (any), 3 if the constructor only assigns `[]` (any[]), else 0. Evaluates the member's type
int ty_js_assignment_implicit_any(Checker* me, Symbol const* member, Expr const* value) {
    ty_type_of_symbol(me, member);
    if (intern_get(&my links, (u64[]){ KEY_JS_IMPLICIT_ANY, (u64)(uintptr_t)value }, 2)) return 1;
    return (int)(uintptr_t)intern_get(&my links, (u64[]){ KEY_JS_IMPLICIT_ANY, (u64)(uintptr_t)member }, 2);
}

Ty const* ty_dynamic_import_module_type(Checker* me, Symbol* module) {
    Symbol* assigned = module_export_equals(module);
    Ty const* type = ty_type_of_symbol(me, assigned);
    // tsc's getTypeWithSyntheticDefaultImportType: an `export =` module gets a synthetic `default` (not one that says
    // it's an ES module)
    if (assigned == module || assigned->is_placeholder || (type->flags & TYF_ERROR) ||
        !compiler_options()->allow_synthetic_default_imports || symbol_get_export(module, c_sv("__esModule"))
    ) {
        return type;
    }
    bool structured = type->flags & (TYF_OBJECT | TYF_UNION | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED);
    return with_synthetic_default(me, type, structured);
}


// The type of a symbol as a value. Give it the symbol a name found or the one that has the value meaning (`target`
// of an identifier), aliases are seen through. Everything about a placeholder is any
Ty const* ty_type_of_symbol(Checker* me, Symbol const* symbol) {
    if (symbol && (symbol->flags & SYMBOL_ALIAS)) {
        Ty const* namespace_import = namespace_import_type(me, symbol);
        if (namespace_import) return namespace_import;
        // `/** @type {T} */ export default x` along the way: T is the value's type (tsc's getTypeOfAlias: the declared
        // type of the export assignment)
        Symbol const* step = symbol;
        for (int depth = 0; step && (step->flags & SYMBOL_ALIAS) && depth < 32; depth++) {
            for (u32 i = 0; i < step->decl_count; i++) {
                if (step->decls[i].kind == SYMBOL_DECL_ALIAS && step->decls[i].jsdoc_type) {
                    return ty_of_node(step->decls[i].jsdoc_type);
                }
            }
            Symbol const* target = symbol_resolve_alias((Symbol*)step);
            // (An import's own step is the export it names, which may be an alias itself)
            PortItem const* item = step->alias ? step->alias->item : nullptr;
            step = item && item->source_symbol ? item->source_symbol : target;
        }
    }
    symbol = symbol_for_meaning(symbol, SYMBOL_VALUE);
    if (symbol && symbol->merged_export_equals_of) return merged_export_equals_type(me, symbol, false);
    symbol = canonical(me, symbol);
    if (!symbol || symbol->is_placeholder) return ty_intrinsic(me, symbol && symbol->is_unresolved ? TY_ERROR : TY_ANY);
    // `typeof import("./a")`, `import("./a")`, `globalThis`. A shorthand ambient module is any
    if (symbol->is_module && symbol->is_shorthand_module) return ty_intrinsic(me, TY_ANY);
    if (symbol->is_module || symbol->is_global_this) return ty_namespace_type(me, symbol);
    bool assignments = symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_ASSIGNMENT;
    if (assignments && (symbol->decl_count > 1 || symbol->decls[0].assignment != ASSIGNMENT_DECL_PROPERTY ||
        symbol->decls[0].jsdoc_type)
    ) {
        return assignment_symbol_type(me, symbol);
    }
    SymbolDecl const* only = nullptr;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (!decl_contributes_value(symbol, i)) continue;
        if (only) return ty_merged_value_type(me, symbol);
        only = &symbol->decls[i];
    }
    // Not memoized here: what gives a declaration its type has a memo of its own, which is also what detects cycles
    return only ? value_entry_type(me, symbol, only) : ty_intrinsic(me, TY_ERROR);
}

// The value of a symbol that several declarations contribute a value to: function overloads, a function, class
// or enum merged with a namespace. The members of all of them (members.c: builder_add_merged_value)
Ty const* ty_merged_value_type(Checker* me, Symbol const* symbol) {
    u64 key[2] = { KEY_MERGED_VALUE, (u64)(uintptr_t)symbol };
    Ty const* existing = intern_get(&my links, key, 2);
    if (existing) return existing;

    // With a namespace, class or enum among them it goes by name (`typeof f`), overloads alone are anonymous
    Ty const* named = nullptr;
    Ty const** entries;
    u32 count = value_entries(me, symbol, &entries);
    for (u32 i = 0; i < count && !named; i++) {
        if (entries[i]->kind == TY_DECLARED) named = entries[i];
    }
    free(entries);
    Ty* ret;
    if (named) {
        ret = named->declared.kind == TY_DECLARED_NAMESPACE ?
            make_namespace(me, named->declared.namespace_symbol) :
            ty_alloc(me, TY_DECLARED, TYF_OBJECT);
        if (named->declared.kind != TY_DECLARED_NAMESPACE) {
            ret->declared = named->declared;
            ret->declared.state = arena_new(&my arena, TyObjectState);
        }
        ret->declared.merged_value = symbol;
        if (!ret->declared.name->len) ret->declared.name = ty_qualified_name(me, symbol);
    } else {
        // A function body can mention every type parameter in scope
        ret = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT | TYF_COULD_CONTAIN_TYPE_VARIABLES);
        ret->anonymous.merged_value = symbol;
        ret->anonymous.ctx = (LowerCtx){ .generic = true };
        ret->anonymous.free_params_unknown = true;
        ret->anonymous.state = arena_new(&my arena, TyObjectState);
    }
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

u32 ty_merged_value_entries(Checker* me, Symbol const* symbol, Ty const*** out) {
    if (symbol->merged_export_equals_of) {
        *out = malloc(sizeof(Ty const*));
        panic_if(!*out, "out of memory");
        (*out)[0] = ty_type_of_symbol(me, symbol->merged_export_equals_of);
        return 1;
    }
    return value_entries(me, symbol, out);
}

// What a module's `export =` assigns merged with its other exports (module_export_equals()): an object type gets them
// as properties (tsc's getTypeOfFuncClassEnumModule of the merged symbol, or getInitializerTypeFromAssignmentDeclaration
// of `module.exports =`), anything else is what is assigned
// `in_file`: as the module's own `module.exports` (tsc's getTypeOfFuncClassEnumModule of the file: the exports are
// always merged in, getWidenedTypeForAssignmentDeclaration), else as an importer sees it (the type of
// getCommonJsExportEquals' clone, where a variable keeps its own type: `module.exports = f; module.exports.x = 1` of a
// `const f`)
static Ty const* merged_export_equals_type(Checker* me, Symbol const* clone, bool in_file) {
    Ty const* original = ty_type_of_symbol(me, clone->merged_export_equals_of);
    if (!(original->flags & TYF_OBJECT)) return original;
    Symbol const* original_symbol = clone->merged_export_equals_of;
    for (u32 i = 0; !in_file && i < original_symbol->decl_count; i++) {
        if (!(original_symbol->decls[i].flags & SYMBOL_VALUE)) continue;
        if (original_symbol->decls[i].kind == SYMBOL_DECL_VARIABLE) return original;
        break;
    }
    // A class or interface instance becomes an anonymous type with its members and the exports (tsc keeps the type's
    // symbol only if nothing was added)
    bool instance = original->kind == TY_REFERENCE || (original->kind == TY_DECLARED &&
        (original->declared.kind == TY_DECLARED_CLASS_INSTANCE || original->declared.kind == TY_DECLARED_INTERFACE));
    if (!instance && original->kind != TY_DECLARED && original->kind != TY_ANONYMOUS) return original;
    bool any = false;
    for (u32 i = 0; i < clone->exports->count && !any; i++) {
        Symbol const* value = symbol_for_meaning(clone->exports->entries[i].symbol, SYMBOL_VALUE);
        any = value && !value->is_placeholder;
    }
    if (!any) return original;
    if (!instance) return ty_merged_value_type(me, clone);
    u64 key[2] = { KEY_MERGED_EXPORT_EQUALS_INSTANCE, (u64)(uintptr_t)clone };
    Ty const* existing = intern_get(&my links, key, 2);
    if (existing) return existing;
    Ty* ret = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT);
    ret->anonymous.merged_value = clone;
    ret->anonymous.state = arena_new(&my arena, TyObjectState);
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

static Ty const* tuple_target_of_node(Checker* me, TypeNode const* node) {
    u32 arity = (u32)vec_tuple_element_size(&node->tuple.elements);
    if (!arity) {
        TupleShape shape = { .readonly = node->tuple.readonly };
        return ty_tuple_target(me, &shape);
    }
    unsigned char* flags = malloc(arity);
    Atom const** names = calloc(arity, sizeof(Atom const*));
    bool any_named = false;
    for_each (i, vec_tuple_element, &node->tuple.elements) {
        flags[i_index] = (i->optional ? TUPLE_ELEMENT_OPTIONAL : 0) | (i->rest ? TUPLE_ELEMENT_REST : 0);
        if (span_valid(i->maybe_name)) {
            names[i_index] = checker_atom(me, i->maybe_name.sv);
            any_named = true;
        }
    }
    TupleShape shape = {
        .arity = arity,
        .readonly = node->tuple.readonly,
        .flags = flags,
        .names = any_named ? names : nullptr,
    };
    Ty const* ret = ty_tuple_target(me, &shape);
    free(flags);
    free(names);
    return ret;
}

// `[b?: number]` has an element of type `number | undefined` (tsc's addOptionality)
static Ty const* tuple_element_type(Checker* me, TupleElement const* element, LowerCtx ctx) {
    Ty const* type = ty_from_node(me, &element->type, ctx);
    // `...args: Args` of an array type is a rest element of its elements (tsc's createNormalizedTypeReference), a
    // plain array whatever alias it went by
    if (element->rest && type->kind == TY_REFERENCE && ty_is_array(me, type) && type->alias) {
        return ty_array(me, ty_reference_args(me, type).data[0], false);
    }
    return element->optional ? ty_optional_prop_type(me, type) : type;
}

static Ty const* lower_tuple(Checker* me, TypeNode const* node, LowerCtx ctx) {
    Ty const* target = tuple_target_of_node(me, node);
    u32 arity = (u32)vec_tuple_element_size(&node->tuple.elements);
    if (!arity) return target;
    Ty const** args = malloc(sizeof(Ty const*) * arity);
    for_each (i, vec_tuple_element, &node->tuple.elements) {
        // TODO: tsc keeps the element type of a rest element (`T` for `...T[]`), this keeps what was written
        args[i_index] = tuple_element_type(me, i, ctx);
    }
    Ty const* ret = ty_reference(me, target, (TyList){ .data = args, .len = arity });
    free(args);
    return ret;
}

// tsc's getArrayElementTypeNode: what `...x: N` spreads if N is syntactically an array
static TypeNode const* array_element_type_node(TypeNode const* node) {
    if (node->tag == TYPE_ARRAY) return node->array.element;
    if (node->tag == TYPE_TUPLE && vec_tuple_element_size(&node->tuple.elements) == 1) {
        TupleElement const* only = vec_tuple_element_at(&node->tuple.elements, 0);
        if (only->rest) return array_element_type_node(&only->type);
    }
    return nullptr;
}

// A rest element whose type isn't written as an array: tsc flags it Variadic (getRestTypeElementFlags)
bool ty_tuple_node_has_variadic(TypeNode const* node) {
    for_each (i, vec_tuple_element, &node->tuple.elements) {
        if (i->rest && !array_element_type_node(&i->type)) return true;
    }
    return false;
}

// Deferred references, see Ty.reference.deferred

// Whether lowering the node right now would run into a type alias that is being resolved, also through the bodies of
// aliases that aren't resolved yet, which lowering the node would resolve. Object types aren't looked into, their
// members are lazy anyway. `budget` bounds the walk (aliases that aren't resolved yet can name each other)
static bool node_reaches_alias_in_flight_through(Checker* me, TypeNode const* node, u32* budget) {
    if (!*budget) return false;
    --*budget;
    switch (node->tag) {
        case TYPE_IDENTIFIER: {
            Symbol const* decl = canonical(me, node->identifier->target);
            if (decl && decl->flags & SYMBOL_TYPE_ALIAS) {
                u64 key[2] = { KEY_SYMBOL_LINKS, (u64)(uintptr_t)decl };
                SymbolLinks const* links = intern_get(&my links, key, 2);
                if (links && links->declared_state == LAZY_STATE_IN_FLIGHT) return true;
                if (!links || links->declared_state == LAZY_STATE_NOT_STARTED) {
                    for (u32 i = 0; i < decl->decl_count; i++) {
                        if (decl->decls[i].kind != SYMBOL_DECL_TYPE_ALIAS) continue;
                        TypeDecl const* type_decl = decl->decls[i].node;
                        if (node_reaches_alias_in_flight_through(me, &type_decl->type, budget)) return true;
                    }
                }
            }
            for_each (i, vec_spanned_type, &node->identifier->generic_params) {
                if (node_reaches_alias_in_flight_through(me, &i->type, budget)) return true;
            }
            return false;
        }
        case TYPE_ARRAY: return node_reaches_alias_in_flight_through(me, node->array.element, budget);
        case TYPE_JSDOC: return node_reaches_alias_in_flight_through(me, &node->jsdoc->type, budget);
        case TYPE_PREDICATE: return false;
        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &node->tuple.elements) {
                if (node_reaches_alias_in_flight_through(me, &i->type, budget)) return true;
            }
            return false;
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                if (node_reaches_alias_in_flight_through(me, i, budget)) return true;
            }
            return false;
        default: return false;
    }
}

bool ty_node_reaches_alias_in_flight(Checker* me, TypeNode const* node) {
    u32 budget = 64;
    return node_reaches_alias_in_flight_through(me, node, &budget);
}

// Whether lowering the node could resolve a type alias, going by the syntax alone (tsc's mayResolveTypeAlias): what
// makes a reference inside an alias body deferred
static bool may_resolve_type_alias(Checker* me, TypeNode const* node) {
    switch (node->tag) {
        case TYPE_IDENTIFIER: {
            Symbol const* decl = canonical(me, node->identifier->target);
            return decl && (decl->flags & SYMBOL_TYPE_ALIAS);
        }
        case TYPE_TYPEOF: return true;
        case TYPE_KEYOF: return may_resolve_type_alias(me, &node->keyof->interface);
        case TYPE_JSDOC: return may_resolve_type_alias(me, &node->jsdoc->type);
        case TYPE_PREDICATE: return false;
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                if (may_resolve_type_alias(me, i)) return true;
            }
            return false;
        case TYPE_INDEXED:
            return may_resolve_type_alias(me, &node->indexed->interface) || may_resolve_type_alias(me, &node->indexed->index);
        case TYPE_TERNARY:
            return may_resolve_type_alias(me, &node->ternary->maybe_sub) ||
                may_resolve_type_alias(me, &node->ternary->maybe_super) ||
                may_resolve_type_alias(me, &node->ternary->if_true) || may_resolve_type_alias(me, &node->ternary->if_false);
        default: return false;
    }
}

// tsc's mayResolveTypeAlias over the elements of a tuple node (a rest element's array type isn't one, its element is)
static bool tuple_elements_may_resolve_type_alias(Checker* me, TypeNode const* node) {
    for_each (i, vec_tuple_element, &node->tuple.elements) {
        TypeNode const* type = i->rest && i->type.tag == TYPE_ARRAY ? i->type.array.element : &i->type;
        if (may_resolve_type_alias(me, type)) return true;
    }
    return false;
}

// A tuple whose one element is a rest element of an array is that array (tsc's getTupleTargetType): the element
static TypeNode const* lone_rest_array_element(TypeNode const* node) {
    return node->tag == TYPE_TUPLE ? array_element_type_node(node) : nullptr;
}

static Ty const* deferred_reference_target(Checker* me, TypeNode const* node) {
    if (node->tag == TYPE_IDENTIFIER) return ty_declared_type_of(me, canonical(me, node->identifier->target));
    if (node->tag == TYPE_TUPLE && !lone_rest_array_element(node)) return tuple_target_of_node(me, node);
    ty_array(me, ty_intrinsic(me, TY_ANY), false); // Makes sure the targets are looked up
    ty_array(me, ty_intrinsic(me, TY_ANY), true);
    bool readonly = node->tag == TYPE_TUPLE ? node->tuple.readonly : node->array.readonly;
    return readonly ? my readonly_array_target : my array_target;
}

Ty const* ty_deferred_reference(
    Checker* me, TypeNode const* node, LowerCtx ctx, TyMapper const* mapper, Symbol const* alias, TyList alias_args
) {
    // Only what the node mentions is kept of the mapper, so instantiations that differ elsewhere are one type
    u32 kept = 0;
    Ty const* stack_pairs[32];
    Ty const** pairs = stack_pairs;
    if (mapper) {
        TyConditionalOuter const* outer = deferred_node_outer(me, node);
        pairs = mapper->len * 2 <= c_arraylen(stack_pairs) ? stack_pairs : malloc(sizeof(Ty const*) * mapper->len * 2);
        for (u32 i = 0; i < mapper->len; i++) {
            bool mentioned = outer->unknown;
            for (u32 j = 0; !mentioned && j < outer->params.len; j++) mentioned = outer->params.data[j] == mapper->sources[i];
            if (!mentioned || mapper->sources[i] == mapper->targets[i]) continue;
            pairs[kept * 2] = mapper->sources[i];
            pairs[kept * 2 + 1] = mapper->targets[i];
            kept++;
        }
    }
    u64 stack_key[64];
    u32 count = 2 + kept * 2;
    u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
    key[0] = KEY_DEFERRED_REFERENCE;
    key[1] = (u64)(uintptr_t)node;
    for (u32 i = 0; i < kept * 2; i++) key[2 + i] = pairs[i]->id;
    Ty const* ret = intern_get(&my links, key, count);
    if (!ret) {
        if (kept != (mapper ? mapper->len : 0)) {
            Ty const** sources = arena_new_n(&my arena, Ty const*, kept ? kept : 1);
            Ty const** targets = arena_new_n(&my arena, Ty const*, kept ? kept : 1);
            for (u32 i = 0; i < kept; i++) {
                sources[i] = pairs[i * 2];
                targets[i] = pairs[i * 2 + 1];
            }
            mapper = kept ? ty_mapper_new(me, (TyList){ .data = sources, .len = kept }, (TyList){ .data = targets, .len = kept }) : nullptr;
        }
        Ty const* target = deferred_reference_target(me, node);
        // Whether it mentions type variables isn't known until the arguments resolve
        Ty* fresh = ty_alloc(me, TY_REFERENCE, TYF_OBJECT | (ctx.generic ? TYF_COULD_CONTAIN_TYPE_VARIABLES : 0));
        fresh->reference.target = target;
        fresh->reference.state = arena_new(&my arena, TyObjectState);
        TyDeferredRef* deferred = arena_new(&my arena, TyDeferredRef);
        deferred->node = node;
        deferred->ctx = ctx; // Still resolved by the alias, syntactically (tsc's isResolvedByTypeAlias)
        deferred->mapper = mapper;
        fresh->reference.deferred = deferred;
        // An array or tuple written as the body of an alias goes by the alias name. The node says which alias, so the
        // name isn't part of the key
        fresh->alias = alias;
        fresh->alias_args = ty_list_copy(me, alias_args.data, alias_args.len);
        *intern_slot(me, &my links, key, count) = fresh;
        ret = fresh;
    }
    if (key != stack_key) free(key);
    if (pairs != stack_pairs) free(pairs);
    return ret;
}

TyList ty_reference_args(Checker* me, Ty const* reference) {
    check(reference->kind == TY_REFERENCE);
    TyDeferredRef* deferred = reference->reference.deferred;
    if (!deferred) return reference->reference.args;
    if (deferred->state == LAZY_STATE_DONE) return deferred->args;
    u32 len = reference->reference.target->declared.type_params.len;
    Ty const** args = arena_new_n(&my arena, Ty const*, len ? len : 1);
    if (deferred->state == LAZY_STATE_IN_FLIGHT) {
        // Only an illegal cycle gets here (its arguments need its arguments)
        deferred->circular = true;
        for (u32 i = 0; i < len; i++) args[i] = ty_intrinsic(me, TY_ERROR);
        return (TyList){ .data = args, .len = len };
    }
    deferred->state = LAZY_STATE_IN_FLIGHT;
    TypeNode const* node = deferred->node;
    if (node->tag == TYPE_IDENTIFIER) {
        Ty const* written_buf[MAX_TYPE_ARGS];
        Ty const* target = reference->reference.target;
        TyList written = lower_type_args(me, target->declared.type_params, target->declared.outer_count,
            &node->identifier->generic_params, deferred->ctx, written_buf);
        for (u32 i = 0; i < len; i++) args[i] = ty_instantiate(me, written.data[i], deferred->mapper);
    } else {
        for (u32 i = 0; i < len; i++) {
            TypeNode const* lone = lone_rest_array_element(node);
            Ty const* element = node->tag == TYPE_ARRAY ? ty_from_node(me, node->array.element, deferred->ctx) :
                lone ? ty_from_node(me, lone, deferred->ctx) :
                tuple_element_type(me, vec_tuple_element_at(&node->tuple.elements, i), deferred->ctx);
            args[i] = ty_instantiate(me, element, deferred->mapper);
        }
    }
    if (deferred->circular) {
        // tsc's getTypeArguments when popTypeResolution fails: error type arguments, reported at the node (array and
        // tuple nodes have no span here: the current node instead)
        for (u32 i = 0; i < len; i++) args[i] = ty_intrinsic(me, TY_ERROR);
        if (node->tag == TYPE_IDENTIFIER) {
            Symbol const* symbol = reference->reference.target->declared.symbol;
            ty_error_at(me, identifier_type_span(node->identifier), 4109, cstr_d("Type arguments for '",
                symbol ? symbol->name : c_sv("?"), "' circularly reference themselves."));
        } else {
            ty_error_at_current_node_d(me, 4110, "Tuple type arguments circularly reference themselves.");
        }
    }
    deferred->args = (TyList){ .data = args, .len = len };
    deferred->state = LAZY_STATE_DONE;
    return deferred->args;
}

// `[a, b, ...rest]`: what `rest` gets out of a tuple or array, `start` being its index in the pattern
static Ty const* binding_rest_elements(Checker* me, Ty const* object, u32 start) {
    if (!ty_is_tuple(object)) return object; // An array (TODO: a readonly array gives a mutable one), or the empty tuple
    TupleShape const* shape = object->reference.target->declared.tuple;
    TyList elements = ty_reference_args(me, object);
    if (start >= shape->arity) {
        bool ends_in_rest = shape->arity && (shape->flags[shape->arity - 1] & TUPLE_ELEMENT_REST);
        if (ends_in_rest) start = shape->arity - 1;
        else return ty_tuple_target(me, &(TupleShape){0});
    }
    TupleShape rest_shape = {
        .arity = shape->arity - start,
        .flags = shape->flags + start,
        .names = shape->names ? shape->names + start : nullptr,
    };
    return ty_reference(me, ty_tuple_target(me, &rest_shape), (TyList){ .data = elements.data + start, .len = rest_shape.arity });
}

// The elements of a tuple from `start` on, or of each member of a union of tuples; null for anything else (tsc's
// `everyType(sourceType, isTupleType) ? mapType(sourceType, t => sliceTupleType(t, start))` of a rest element)
Ty const* ty_tuple_rest_slice(Checker* me, Ty const* source, u32 start) {
    if (ty_is_tuple(source)) return binding_rest_elements(me, source, start);
    if (source->kind != TY_UNION) return nullptr;
    TyList types = source->composite.types;
    for (u32 i = 0; i < types.len; i++) {
        if (!ty_is_tuple(types.data[i])) return nullptr;
    }
    TyBuf sliced = {0};
    for (u32 i = 0; i < types.len; i++) ty_buf_push(&sliced, binding_rest_elements(me, types.data[i], start));
    Ty const* ret = ty_union(me, sliced.data, sliced.len, nullptr, (TyList){0});
    ty_buf_drop(&sliced);
    return ret;
}

// tsc's getSyntheticElementAccess: `x.y.a` for the `a` of `const { a } = x.y`, and `x.b.c` / `x[0]` for the elements of
// nested patterns (`const { b: { c } } = x`, `const [d] = x`), made once per binding. Null if the initializer isn't a
// reference with a flow node. `flow` is set to that node
static Expr const* synthetic_element_access(Checker* me, Binding const* binding, FlowNode** flow) {
    Binding const* parent = binding->parent;
    if (!parent || binding->rest) return nullptr;
    Value key;
    Span key_span;
    if (parent->tag == BINDING_OBJECT) {
        ObjectBindingItem const* item = binding->parent_item;
        if (!item || item->computed_key) return nullptr;
        if (item->source.tag != VALUE_STRING && item->source.tag != VALUE_NUMBER) return nullptr;
        key = item->source;
        key_span = item->source_span;
    } else if (parent->tag == BINDING_ARRAY) {
        key = value_number(binding->parent_index);
        key_span = binding->element_span;
    } else {
        return nullptr;
    }
    Expr const* object;
    if (parent->parent) {
        object = synthetic_element_access(me, parent, flow);
        if (!object) return nullptr;
    } else {
        if (!parent->source || parent->source->tag != LAZY_TYPE_INITIALIZED_BINDING) return nullptr;
        InitializedBinding const* initialized = c_container_of(parent->source, InitializedBinding, lazy_type);
        object = &initialized->value;
        *flow = object->tag == EXPR_IDENTIFIER ? object->identifier.flow :
            object->tag == EXPR_PROPERTY ? object->property->flow : nullptr;
        if (!*flow) return nullptr;
    }
    u64 memo_key[2] = { KEY_SYNTHETIC_ACCESS, (u64)(uintptr_t)binding };
    Expr const* access = intern_get(&my links, memo_key, 2);
    if (access) return access;
    PropertyExpr* property = arena_new(&my arena, PropertyExpr);
    *property = (PropertyExpr){
        .expr = *object,
        .prop = expr_literal(key_span, key),
        .form = key.tag == VALUE_STRING ? PROPERTY_EXPR_DOT : PROPERTY_EXPR_BRACKET,
        .flow = *flow,
    };
    Expr* synthetic = arena_new(&my arena, Expr);
    *synthetic = (Expr){
        .tag = EXPR_PROPERTY, .property = property, .inner_span = key_span, .outer_span = key_span,
        .lazy_type.tag = LAZY_TYPE_EXPR,
    };
    *intern_slot(me, &my links, memo_key, 2) = synthetic;
    return synthetic;
}

// `const { a } = x.y` after `if (x.y.a)`: an element of a pattern whose initializer is a reference has the flow type of
// `x.y.a` there (tsc's getFlowTypeOfDestructuring)
static Ty const* destructured_flow_type(Checker* me, Binding const* binding, Ty const* declared) {
    FlowNode* flow = nullptr;
    Expr const* access = synthetic_element_access(me, binding, &flow);
    return access ? ty_flow_type(me, access, declared, declared, flow) : declared;
}

Ty const* ty_binding_element_type_from(Checker* me, Binding const* binding, Ty const* object) {
    Binding const* parent = binding->parent;
    Ty const* key;
    if (parent->tag == BINDING_ARRAY) {
        Value index = value_number(binding->parent_index);
        key = ty_literal(me, &index);
    } else {
        ObjectBindingItem const* item = binding->parent_item;
        key = item->computed_key ? ty_expr_type(me, item->computed_key) : ty_literal(me, &item->source);
    }
    Ty const* ret = ty_access_reading(me, object, key);
    if (!ret) return nullptr;
    if (!parent->has_explicit_type) ret = destructured_flow_type(me, binding, ret);
    if (!parent->has_explicit_type && !binding_is_parameter(parent)) ret = ty_widened_for_declaration(me, ret, false);
    return ret;
}


static bool is_generic_with_undefined_constraint(Checker* me, Ty const* ty) {
    if (ty->kind != TY_TYPE_PARAM && !(ty->flags & TYF_DEFERRED)) return false;
    Ty const* constraint = ty_base_constraint(me, ty);
    if (!constraint || constraint == ty) return false;
    if (constraint->flags & TYF_UNDEFINED) return true;
    if (constraint->kind != TY_UNION) return false;
    for (u32 i = 0; i < constraint->composite.types.len; i++) {
        if (constraint->composite.types.data[i]->flags & TYF_UNDEFINED) return true;
    }
    return false;
}

// Without undefined, a generic type whose constraint has it being its constraint (tsc's getNonUndefinedType):
// `const { a = 1 } = p` with `p: Readonly<P>` and `P extends { a?: number }` makes a number
static Ty const* non_undefined_type(Checker* me, Ty const* ty) {
    TyList members = ty->kind == TY_UNION ? ty->composite.types : (TyList){ .data = &ty, .len = 1 };
    bool some = false;
    for (u32 i = 0; i < members.len && !some; i++) some = is_generic_with_undefined_constraint(me, members.data[i]);
    if (some) {
        TyBuf mapped = {0};
        for (u32 i = 0; i < members.len; i++) {
            Ty const* member = members.data[i];
            bool generic = member->kind == TY_TYPE_PARAM || (member->flags & TYF_DEFERRED);
            Ty const* constraint = generic ? ty_base_constraint(me, member) : nullptr;
            ty_buf_push(&mapped, constraint ? constraint : member);
        }
        ty = ty_union(me, mapped.data, mapped.len, nullptr, (TyList){0});
        ty_buf_drop(&mapped);
    }
    return ty_without_undefined(me, ty);
}

// Whether a computed key of type `key` names the property `name` (tsc's getRestType: the property's literal key type
// is assignable to the key's; numeric names against `number` aren't told apart from string ones, TODO)
static bool computed_key_names(Checker* me, Ty const* key, Atom const* name) {
    if (key->kind == TY_UNION) {
        for (u32 i = 0; i < key->composite.types.len; i++) {
            if (computed_key_names(me, key->composite.types.data[i], name)) return true;
        }
        return false;
    }
    if (key->flags & TYF_ANY) return true;
    bool is_number = false;
    Atom const* named = ty_property_name_of_key(me, key, &is_number);
    if (named) return named == name;
    return key->kind == TY_INTRINSIC && (key->flags & TYF_STRING) && !ty_unique_symbol_of_name(me, name);
}

// Whether the object pattern takes the property `name` out of its rest
static bool pattern_names(Checker* me, Binding const* pattern, Atom const* name) {
    for_each (item, vec_object_binding_item, &pattern->object) {
        if (item->target.rest) continue;
        if (item->computed_key) {
            if (computed_key_names(me, ty_expr_type(me, item->computed_key), name)) return true;
            continue;
        }
        bool is_number = false;
        if (ty_property_name_of_key(me, ty_literal(me, &item->source), &is_number) == name) return true;
    }
    return false;
}

// The annotation a property was declared with, which the types baseline reuses for a copy of it where it still says
// the same (ObjectLiteralProp.asserted)
static TypeNode const* prop_annotation(Prop const* prop) {
    PropDecl const* decl = prop->decl;
    if (decl->asserted) return decl->asserted;
    return decl->node && decl->node->parsed && !decl->lazy && !decl->function && !decl->overload_count && !decl->symbol ? decl->node : nullptr;
}

// The rest of an intersection: the properties of all of its members, each of the intersection's type for it
static Ty const* intersection_rest_type(Checker* me, Ty const* source, Binding const* pattern) {
    TyList parts = source->composite.types;
    for (u32 i = 0; i < parts.len; i++) {
        Ty const* part = ty_apparent_type(me, parts.data[i]);
        // (A generic member keeps the whole thing, like a generic object: TODO Omit)
        if (!(part->flags & TYF_OBJECT) || ty_members(me, part)->mapped) return source;
    }
    ObjectLiteralProp* props = nullptr;
    u32 count = 0;
    for (u32 i = 0; i < parts.len; i++) {
        Members const* members = ty_members(me, ty_apparent_type(me, parts.data[i]));
        for (u32 j = 0; j < members->prop_count; j++) {
            Atom const* name = members->props[j]->name;
            bool seen = false;
            for (u32 k = 0; k < count && !seen; k++) seen = props[k].name == name;
            if (seen || pattern_names(me, pattern, name)) continue;
            FoundProp found = ty_find_prop(me, source, name);
            if (!found.found || (found.flags & (PROP_PRIVATE | PROP_PROTECTED))) continue;
            if (found.prop && !ty_is_spreadable_prop(me, found.prop)) continue;
            Prop const* origin = found.prop;
            while (origin && origin->target) origin = origin->target;
            props = realloc(props, sizeof(ObjectLiteralProp) * (count + 1));
            panic_if(!props, "out of memory");
            props[count++] = (ObjectLiteralProp){
                .name = name,
                .flags = found.flags & (u32)~(PROP_READONLY | PROP_GETTER | PROP_SETTER | PROP_ACCESSOR),
                .type = found.type,
                .name_span = origin ? origin->decl->name_span : (Span){0},
                .decl_spans = origin ? origin->decl->decl_spans : nullptr,
                .decl_span_count = origin ? origin->decl->decl_span_count : 0,
                .name_is_value = origin && origin->decl->name_is_value,
                .name_form = found.prop ? ty_prop_name_form_of(found.prop) : 0,
                .asserted = origin ? prop_annotation(origin) : nullptr,
            };
        }
    }
    // (The intersection's index signatures stay: tsc's getRestType with getIndexInfosOfType)
    Members const* own = ty_members(me, source);
    Ty const* ret = ty_object_literal(me, props, count, own->index, own->index_count, OBJECT_SYNTHETIC);
    free(props);
    return ret;
}

// The rest of a generic object (or of a pattern with a generic computed name): `Omit<T, names>`, the names being those
// the pattern takes and those of the properties a rest leaves out anyway (private, protected, class methods). T itself
// if that's no names (tsc's getRestType)
static Ty const* generic_rest_type(Checker* me, Ty const* source, Binding const* pattern) {
    TyBuf keys = {0};
    for_each (item, vec_object_binding_item, &pattern->object) {
        if (item->target.rest) continue;
        ty_buf_push(&keys, item->computed_key ? ty_expr_type(me, item->computed_key) : ty_literal(me, &item->source));
    }
    Ty const* apparent = ty_apparent_type(me, source);
    if (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) {
        Members const* members = ty_members(me, apparent);
        for (u32 i = 0; i < members->prop_count; i++) {
            if (!ty_is_spreadable_prop(me, members->props[i])) ty_buf_push(&keys, ty_prop_name_literal(me, members->props[i]->name));
        }
    }
    Ty const* omitted = keys.len ? ty_union(me, keys.data, keys.len, nullptr, (TyList){0}) : nullptr;
    ty_buf_drop(&keys);
    if (!omitted || (omitted->flags & TYF_NEVER)) return source;
    Symbol const* omit = program_get_global(my program, c_sv("Omit"), SYMBOL_TYPE);
    if (omit) omit = binder_redirect(my binder, omit);
    if (!omit || !(omit->flags & SYMBOL_TYPE_ALIAS) || ty_alias_type_params(me, omit).len != 2) return ty_intrinsic(me, TY_ERROR);
    Ty const* args[2] = { source, omitted };
    return ty_alias_instance(me, omit, (TyList){ .data = args, .len = 2 });
}

static bool pattern_has_generic_key(Checker* me, Binding const* pattern) {
    for_each (item, vec_object_binding_item, &pattern->object) {
        if (item->computed_key && !item->target.rest && ty_is_generic_index(ty_expr_type(me, item->computed_key))) return true;
    }
    return false;
}

// tsc's getRestType: `...rest` of an object pattern, without the properties the pattern names, private and protected
// members and class methods, and with nothing readonly
static Ty const* object_rest_type(Checker* me, Ty const* source, Binding const* pattern) {
    if (source->kind == TY_UNION) {
        Ty const* stack[16];
        TyList types = source->composite.types;
        Ty const** mapped = types.len <= c_arraylen(stack) ? stack : malloc(sizeof(Ty const*) * types.len);
        u32 len = 0;
        for (u32 i = 0; i < types.len; i++) {
            if (!(types.data[i]->flags & TYF_NULLABLE)) mapped[len++] = object_rest_type(me, types.data[i], pattern);
        }
        Ty const* ret = len ? ty_union(me, mapped, len, nullptr, (TyList){0}) : ty_object_literal(me, nullptr, 0, nullptr, 0, OBJECT_SYNTHETIC);
        if (mapped != stack) free((void*)mapped);
        return ret;
    }
    if (source->flags & TYF_NEVER) return ty_object_literal(me, nullptr, 0, nullptr, 0, OBJECT_SYNTHETIC);
    if (ty_is_generic_object(me, source) || pattern_has_generic_key(me, pattern)) return generic_rest_type(me, source, pattern);
    Ty const* apparent = ty_apparent_type(me, source);
    if (apparent->kind == TY_INTERSECTION && !ty_is_generic_index(source)) return intersection_rest_type(me, apparent, pattern);
    if (!(apparent->flags & TYF_OBJECT) || ty_is_generic_index(source)) return source;
    Members const* members = ty_members(me, apparent);
    if (members->mapped) return source;
    ObjectLiteralProp* props = malloc(sizeof(ObjectLiteralProp) * (members->prop_count ? members->prop_count : 1));
    panic_if(!props, "out of memory");
    u32 count = 0;
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members->props[i];
        if (!ty_is_spreadable_prop(me, prop)) continue;
        if (pattern_names(me, pattern, prop->name)) continue;
        // (tsc's getSpreadSymbol: the same property unless that makes it writable)
        if (!(prop->flags & PROP_READONLY)) {
            props[count++] = (ObjectLiteralProp){ .reuse = prop };
            continue;
        }
        Prop const* declared = prop;
        while (declared->target && !declared->decl->decl_span_count) declared = declared->target;
        props[count++] = (ObjectLiteralProp){
            .name = prop->name,
            .flags = prop->flags & (u32)~(PROP_READONLY | PROP_GETTER | PROP_SETTER | PROP_ACCESSOR),
            .type = prop_type(me, prop),
            .name_span = declared->decl->name_span,
            .name_form = ty_prop_name_form_of(declared),
            .decl_spans = declared->decl->decl_spans,
            .decl_span_count = declared->decl->decl_span_count,
            .name_is_value = declared->decl->name_is_value,
            .asserted = prop_annotation(declared),
        };
    }
    Ty const* ret = ty_object_literal(me, props, count, members->index, members->index_count, OBJECT_SYNTHETIC);
    free(props);
    return ret;
}

// tsc's getTypeFromBindingElement: a default's type widened (and optional), a nested pattern's own type, else any
static Ty const* binding_element_implied_type(Checker* me, Binding const* element, bool include_pattern) {
    if (element->default_value) {
        // (Not remembered while the pattern is a contextual type: its sibling references are `any` then)
        if (include_pattern) my flow_uncached++;
        bool nested = (element->tag == BINDING_OBJECT && vec_object_binding_item_size(&element->object)) ||
            (element->tag == BINDING_ARRAY && vec_binding_size(&element->array));
        Ty const* type = nested ?
            ty_check_expr(me, element->default_value, ty_binding_pattern_type(me, element, true)) :
            ty_expr_type(me, element->default_value);
        type = ty_widened_for_declaration(me, type, true);
        if (include_pattern) my flow_uncached--;
        if (!compiler_options()->strict_null_checks) return type;
        Ty const* parts[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
        return ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    if (element->tag == BINDING_OBJECT || element->tag == BINDING_ARRAY) return ty_binding_pattern_type(me, element, include_pattern);
    return ty_intrinsic(me, TY_ANY);
}

// tsc's getTypeFromBindingPattern: what an unannotated `{ a, b = 1 }` or `[a, b = 1]` parameter takes, `{ a: any; b?:
// number }` or `[any, number?]`
// A clone of the type marked as a pattern's (tsc's cloneTypeReference, `pattern`)
Ty const* ty_pattern_clone(Checker* me, Ty const* ty) {
    Ty* ret = ty_alloc(me, (TyKind)ty->kind, ty->flags);
    u32 id = ret->id;
    *ret = *ty;
    ret->id = id;
    ret->flags |= TYF_BINDING_PATTERN;
    return ret;
}

static Ty const* binding_pattern_type(Checker* me, Binding const* pattern, bool include_pattern);

Ty const* ty_binding_pattern_type(Checker* me, Binding const* pattern, bool include_pattern) {
    bool pushed = include_pattern && my contextual_pattern_count < c_arraylen(my contextual_patterns);
    if (pushed) my contextual_patterns[my contextual_pattern_count++] = pattern;
    Ty const* ret = binding_pattern_type(me, pattern, include_pattern);
    if (pushed) my contextual_pattern_count--;
    return ret;
}

static Ty const* binding_pattern_type(Checker* me, Binding const* pattern, bool include_pattern) {
    if (pattern->tag == BINDING_OBJECT) {
        ObjectLiteralProp* props = nullptr;
        u32 count = 0;
        IndexInfo index = {0};
        u32 index_count = 0;
        bool has_computed = false;
        for_each (item, vec_object_binding_item, &pattern->object) {
            if (item->target.rest) {
                index = (IndexInfo){ .key = ty_intrinsic(me, TY_STRING), .value = ty_intrinsic(me, TY_ANY),
                    .param_name = checker_atom(me, c_sv("x")) };
                index_count = 1;
                continue;
            }
            // A computed name of a literal or unique symbol type is a property like any other (tsc's
            // isTypeUsableAsPropertyName)
            bool is_number = false;
            Ty const* key = item->computed_key ? ty_expr_type(me, item->computed_key) : ty_literal(me, &item->source);
            Atom const* name = ty_property_name_of_key(me, key, &is_number);
            if (!name) {
                has_computed |= item->computed_key != nullptr;
                continue;
            }
            props = realloc(props, sizeof(ObjectLiteralProp) * (count + 1));
            panic_if(!props, "out of memory");
            props[count++] = (ObjectLiteralProp){
                .name = name,
                .flags = item->target.default_value ? PROP_OPTIONAL : 0,
                .type = binding_element_implied_type(me, &item->target, include_pattern), // (tsc's symbol has no declaration)
            };
        }
        Ty const* ret = ty_object_literal(me, props, count, index_count ? &index : nullptr, index_count, OBJECT_SYNTHETIC);
        free(props);
        if (!include_pattern) return ret;
        ret = ty_pattern_clone(me, ret);
        // (tsc's ObjectLiteralPatternWithComputedProperties: its literal gets no excess property check)
        if (has_computed) *intern_slot(me, &my links, (u64[]){ KEY_PATTERN_COMPUTED, ret->id }, 2) = ret;
        return ret;
    }
    if (pattern->tag != BINDING_ARRAY) return ty_intrinsic(me, TY_ANY);
    u32 len = (u32)vec_binding_size(&pattern->array);
    Binding const* last = len ? vec_binding_at(&pattern->array, len - 1) : nullptr;
    bool has_rest = last && last->rest;
    if (!len || (len == 1 && has_rest)) {
        // Anything iterable
        Symbol const* iterable = compiler_options()->target >= SCRIPT_TARGET_ES2015 ?
            program_get_global(my program, c_sv("Iterable"), SYMBOL_TYPE) : nullptr;
        Ty const* declared = iterable ? ty_declared_type_of(me, iterable) : nullptr;
        Ty const* any = ty_intrinsic(me, TY_ANY);
        if (declared && declared->kind == TY_DECLARED && declared->declared.type_params.len) {
            TyList params = declared->declared.type_params;
            Ty const** args = arena_new_n(&my arena, Ty const*, params.len);
            for (u32 i = 0; i < params.len; i++) args[i] = any;
            return ty_reference(me, declared, (TyList){ .data = args, .len = params.len });
        }
        return ty_array(me, any, false);
    }
    // Required up to the last element that has no default and isn't a hole or the rest
    u32 min = 0;
    for (u32 i = 0; i < len; i++) {
        Binding const* element = vec_binding_at(&pattern->array, i);
        if (!(element->rest || element->tag == BINDING_NOOP || element->default_value)) min = i + 1;
    }
    Ty const** elements = arena_new_n(&my arena, Ty const*, len);
    unsigned char* flags = calloc(len, 1);
    panic_if(!flags, "out of memory");
    for (u32 i = 0; i < len; i++) {
        Binding const* element = vec_binding_at(&pattern->array, i);
        elements[i] = element->tag == BINDING_NOOP ? ty_intrinsic(me, TY_ANY) : binding_element_implied_type(me, element, include_pattern);
        if (element->rest) {
            flags[i] = TUPLE_ELEMENT_REST;
            elements[i] = ty_array(me, elements[i], false);
        } else if (i >= min) {
            flags[i] = TUPLE_ELEMENT_OPTIONAL;
        }
    }
    TupleShape shape = { .arity = len, .flags = flags };
    Ty const* ret = ty_reference(me, ty_tuple_target(me, &shape), (TyList){ .data = elements, .len = len });
    free(flags);
    return include_pattern ? ty_pattern_clone(me, ret) : ret;
}

Ty const* ty_pad_initializer_type(Checker* me, Ty const* type, Binding const* pattern) {
    if (!binding_is_parameter(pattern)) return type;
    if (pattern->tag == BINDING_OBJECT && type->kind == TY_ANONYMOUS && type->anonymous.object_literal) {
        // The elements with a default the literal lacks are optional properties (tsc's padObjectLiteralType)
        Members const* members = ty_members(me, type);
        ObjectLiteralProp* props = nullptr;
        u32 count = 0;
        for_each (item, vec_object_binding_item, &pattern->object) {
            if (!item->target.default_value || item->target.rest || item->computed_key) continue;
            bool is_number = false;
            Atom const* name = ty_property_name_of_key(me, ty_literal(me, &item->source), &is_number);
            if (!name || ty_get_prop(me, type, name)) continue;
            if (!props) {
                props = malloc(sizeof(ObjectLiteralProp) * (members->prop_count + vec_object_binding_item_size(&pattern->object)));
                panic_if(!props, "out of memory");
                for (u32 i = 0; i < members->prop_count; i++) props[count++] = (ObjectLiteralProp){ .reuse = members->props[i] };
            }
            props[count++] = (ObjectLiteralProp){
                .name = name,
                .flags = PROP_OPTIONAL,
                .type = binding_element_implied_type(me, &item->target, false),
            };
        }
        if (!props) return type;
        Ty const* ret = ty_object_literal(me, props, count, members->index, members->index_count,
            (type->flags & TYF_FRESH_OBJECT ? OBJECT_LITERAL_FRESH : OBJECT_LITERAL) |
            (type->kind == TY_ANONYMOUS && type->anonymous.js_literal ? OBJECT_JS : 0));
        free(props);
        return ret;
    }
    if (pattern->tag != BINDING_ARRAY) return type;
    // A tuple shorter than the pattern gets optional elements: the defaults' types, else any (tsc's padTupleType)
    TupleShape const* shape = ty_tuple_shape(type);
    u32 len = (u32)vec_binding_size(&pattern->array);
    if (!shape || shape->arity >= len) return type;
    for (u32 i = 0; i < shape->arity; i++) {
        if (shape->flags[i] & TUPLE_ELEMENT_REST) return type;
    }
    TyList args = shape->arity ? ty_reference_args(me, type) : (TyList){0};
    Ty const** elements = arena_new_n(&my arena, Ty const*, len);
    unsigned char* flags = calloc(len, 1);
    panic_if(!flags, "out of memory");
    for (u32 i = 0; i < shape->arity; i++) {
        elements[i] = args.data[i];
        flags[i] = shape->flags[i];
    }
    u32 arity = shape->arity;
    for (u32 i = shape->arity; i < len; i++) {
        Binding const* element = vec_binding_at(&pattern->array, i);
        if (i == len - 1 && element->rest) break;
        elements[arity] = element->tag != BINDING_NOOP && element->default_value ?
            binding_element_implied_type(me, element, false) : ty_intrinsic(me, TY_ANY);
        flags[arity++] = TUPLE_ELEMENT_OPTIONAL;
    }
    TupleShape padded = { .arity = arity, .flags = flags, .readonly = shape->readonly };
    Ty const* ret = ty_reference(me, ty_tuple_target(me, &padded), (TyList){ .data = elements, .len = arity });
    free(flags);
    return ret;
}

// The declaration the root of a pattern belongs to, if it has an initializer or can be a parameter
static InitializedBinding const* root_initialized(Binding const* root) {
    if (!root->source || root->source->tag != LAZY_TYPE_INITIALIZED_BINDING) return nullptr;
    return c_container_of(root->source, InitializedBinding, lazy_type);
}

static Ty const* binding_element_context(Checker* me, Binding const* element);
static Ty const* binding_default_type(Checker* me, Binding const* element);

// tsc's getContextualTypeForVariableLikeDeclaration: what a pattern (or one of its elements) is expected to be
static Ty const* binding_declared_context(Checker* me, Binding const* binding) {
    if (binding->parent) return binding_element_context(me, binding);
    if (binding->has_explicit_type) return ty_from_node(me, &binding->explicit_type, (LowerCtx){ .generic = true });
    InitializedBinding const* initialized = root_initialized(binding);
    return initialized && binding_is_parameter(binding) ? ty_contextual_parameter_type(me, initialized) : nullptr;
}

// tsc's getContextualTypeForBindingElement: the element's property (or element) of what its pattern is expected to be,
// or of the pattern's initializer
static Ty const* binding_element_context(Checker* me, Binding const* element) {
    Binding const* pattern = element->parent;
    Ty const* parent_type = binding_declared_context(me, pattern);
    InitializedBinding const* initialized = pattern->parent ? nullptr : root_initialized(pattern);
    if (!parent_type && initialized && initialized->value.tag != EXPR_IMPLICIT_ANY) {
        parent_type = ty_pad_initializer_type(me, ty_expr_type(me, &initialized->value), pattern);
    }
    if (!parent_type) return nullptr;
    if (pattern->tag == BINDING_ARRAY) {
        // (An element's name is itself when there is no property name: a pattern has no context from here)
        if (element->tag == BINDING_OBJECT || element->tag == BINDING_ARRAY) return nullptr;
        return ty_contextual_element_type(me, parent_type, element->parent_index);
    }
    ObjectBindingItem const* item = element->parent_item;
    Ty const* key = item->computed_key ? ty_expr_type(me, item->computed_key) : ty_literal(me, &item->source);
    if (!(key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL))) return nullptr;
    return ty_contextual_prop_type(me, parent_type, key);
}

Ty const* ty_binding_default_type(Checker* me, Binding const* element) {
    for (u32 i = 0; i < my defaults_in_flight_count; i++) {
        if (my defaults_in_flight[i] == element) return ty_expr_type(me, element->default_value);
    }
    if (my defaults_in_flight_count >= c_arraylen(my defaults_in_flight)) return ty_expr_type(me, element->default_value);
    my defaults_in_flight[my defaults_in_flight_count++] = element;
    Ty const* ret = binding_default_type(me, element);
    my defaults_in_flight_count--;
    return ret;
}

static Ty const* binding_default_type(Checker* me, Binding const* element) {
    Ty const* contextual = binding_element_context(me, element);
    // (tsc's getContextualTypeForInitializerExpression: else what a non-empty pattern implies)
    if (!contextual && ((element->tag == BINDING_OBJECT && vec_object_binding_item_size(&element->object)) ||
        (element->tag == BINDING_ARRAY && vec_binding_size(&element->array)))
    ) {
        contextual = ty_binding_pattern_type(me, element, true);
    }
    return ty_check_expr(me, element->default_value, contextual);
}

// What is assigned to a binding: the initializer for the root of a pattern, for an element the property or element
// of what its pattern is assigned
Ty const* ty_binding_assigned_type(Checker* me, Binding const* binding) {
    Binding const* parent = binding->parent;
    // A catch variable (the only root without a source) is any, or unknown under useUnknownInCatchVariables
    if (!parent && !binding->source) {
        return ty_intrinsic(me, compiler_options()->use_unknown_in_catch_variables ? TY_UNKNOWN_ : TY_ANY);
    }
    if (!parent) {
        // (A for-of's pattern destructures the iterated type as it is, the variable is widened)
        if (binding->tag != BINDING_IDENTIFIER && binding->source->tag == LAZY_TYPE_FOR_EACH_LOOP_VAR) {
            return for_each_loop_statement_pattern_source_type(
                c_container_of(binding->source, ForEachLoopStatement, var_type));
        }
        return lazy_type_get(binding->source);
    }
    Ty const* object = ty_binding_type(me, parent, (LowerCtx){ .generic = true });
    bool in_array = parent->tag == BINDING_ARRAY;
    // An array pattern takes elements of an array (or tuple) by index, of anything else what iterating it gives
    // (tsc's getTypeForBindingElement). What an element lacks is reported by binding_typecheck()
    bool array_like = in_array && (object->flags & TYF_ANY || ty_extends_array(me, object));
    Ty const* ret;
    if (in_array && !array_like) {
        Ty const* element = ty_iterated_type(me, ITERATION_DESTRUCTURING, object, span_null, nullptr);
        ret = binding->rest ? ty_array(me, element, false) : element;
    } else if (binding->rest && in_array) {
        ret = binding_rest_elements(me, object, binding->parent_index);
    } else if (binding->rest) {
        ret = object_rest_type(me, object, parent);
    } else if (in_array) {
        Value index = value_number(binding->parent_index);
        // (Read like an expression: noUncheckedIndexedAccess's undefined, tsc's AccessFlags.ExpressionPosition)
        ret = ty_access_reading(me, object, ty_literal(me, &index));
        // Past the end of a tuple (TS2493, binding_typecheck()) is undefined (tsc's getIndexedAccessTypeOrUndefined)
        TupleShape const* shape = ty_tuple_shape(object);
        bool has_rest = false;
        for (u32 i = 0; shape && i < shape->arity; i++) has_rest |= (shape->flags[i] & TUPLE_ELEMENT_REST) != 0;
        if (shape && !has_rest && binding->parent_index >= shape->arity) ret = ty_intrinsic(me, TY_UNDEFINED);
        if (ret && !parent->has_explicit_type) ret = destructured_flow_type(me, binding, ret);
    } else {
        ObjectBindingItem const* item = binding->parent_item;
        ret = ty_access_reading(me, object,
            item->computed_key ? ty_expr_type(me, item->computed_key) : ty_literal(me, &item->source));
        // A property an object literal lacks is undefined when there's a default, which decides (tsc's
        // AccessFlags.AllowMissing)
        if (!ret && binding->default_value && object->kind == TY_ANONYMOUS && object->anonymous.object_literal) {
            ret = ty_intrinsic(me, TY_UNDEFINED);
        }
        if (ret && !parent->has_explicit_type) ret = destructured_flow_type(me, binding, ret);
        // A property the object lacks (TS2339, binding_typecheck()) is the error type (tsc's getIndexedAccessType
        // reporting it): a later `var` of the same name doesn't have to agree with it
        if (!ret) ret = ty_intrinsic(me, TY_ERROR);
    }
    if (!ret) ret = ty_intrinsic(me, TY_ANY);
    // (An element of an unannotated pattern is widened itself: the pattern's source isn't, see initialized_binding's)
    if (!parent->has_explicit_type && !binding_is_parameter(parent)) ret = ty_widened_for_declaration(me, ret, false);
    if (binding->default_value) {
        // `[x = 1]`: undefined is replaced by the default. Under a written type, the default only removes undefined
        // (unless it can be undefined itself), else the two are a subtype-reduced union (tsc's getTypeForBindingElement)
        Binding const* root = parent;
        while (root->parent) root = root->parent;
        Ty const* types[2] = { non_undefined_type(me, ret), nullptr };
        if (root->has_explicit_type) {
            Ty const* default_type = ty_pad_initializer_type(me, ty_binding_default_type(me, binding), binding);
            // (any and unknown can't be undefined either: tsc's UnknownFacts lack IsUndefined)
            if (compiler_options()->strict_null_checks && ty_without_undefined(me, default_type) == default_type) {
                ret = types[0];
            }
            return ret;
        }
        // Not ty_check_expr() with types[0] as the contextual type: only the uncontextual path detects `[x = x]`
        Ty const* default_type = ty_pad_initializer_type(me, ty_binding_default_type(me, binding), binding);
        // tsc unions first and then widens what's left fresh: a default that fits (`name = "a"` for a `"a" | "b"`
        // property) is absorbed, literal and all
        if (!(ret->flags & TYF_ANY) && !(types[0]->flags & TYF_NEVER) &&
            ty_is_related(me, ty_regular(me, default_type), types[0], TY_RELATION_STRICT_SUBTYPE)) {
            return types[0];
        }
        types[1] = ty_widened_for_declaration(me, default_type, true);
        ret = ret->flags & TYF_ANY ? ret : ty_union_subtype_reduced(me, types, 2);
    }
    return ret;
}

// The type of a binding: what is written, else what is assigned. `ctx` is what a written type is lowered with
Ty const* ty_binding_type(Checker* me, Binding const* binding, LowerCtx ctx) {
    if (binding->has_explicit_type) {
        Ty const* type = ty_from_node(me, &binding->explicit_type, ctx);
        // A catch variable annotated with anything but any or unknown is any (TS1196)
        if (!binding->parent && !binding->source && !(type->flags & (TYF_ANY | TYF_UNKNOWN))) return ty_intrinsic(me, TY_ANY);
        return type;
    }
    return ty_binding_assigned_type(me, binding);
}

static bool is_naked_type_param_node(TypeNode const* node) {
    if (node->tag != TYPE_IDENTIFIER || !node->identifier->target) return false;
    Symbol const* decl = node->identifier->target;
    return (decl->flags & SYMBOL_TYPE_PARAMETER) != 0;
}

// The `infer` type parameters a type node declares (what type_add_inferred_names() declares for an extends type)
static void collect_declared_infers(Checker* me, TypeNode const* node, TyBuf* out) {
    switch (node->tag) {
        case TYPE_IDENTIFIER:
            for_each (i, vec_spanned_type, &node->identifier->generic_params) collect_declared_infers(me, &i->type, out);
            break;
        case TYPE_INFERRED:
            if (node->inferred->symbol_ref) ty_buf_push(out, ty_type_param(me, node->inferred->symbol_ref));
            break;
        case TYPE_CALLABLE:
            for_each (i, vec_function_arg, &node->callable->function->args) {
                if (i->initializer.binding.has_explicit_type) collect_declared_infers(me, &i->initializer.binding.explicit_type, out);
            }
            collect_declared_infers(me, &node->callable->function->ret, out);
            // (`(arg: any) => arg is infer R`)
            if (node->callable->function->predicate && node->callable->function->predicate->has_type) {
                collect_declared_infers(me, &node->callable->function->predicate->type, out);
            }
            break;
        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &node->interface.members) {
                if (i->key.tag == INTERFACE_KEY_MAPPED) {
                    collect_declared_infers(me, &i->key.mapped.in_type, out);
                    if (i->key.mapped.has_as_type) collect_declared_infers(me, &i->key.mapped.as_type, out);
                }
                collect_declared_infers(me, &i->type, out);
            }
            break;
        case TYPE_ARRAY: collect_declared_infers(me, node->array.element, out); break;
        case TYPE_JSDOC: collect_declared_infers(me, &node->jsdoc->type, out); break;
        case TYPE_PREDICATE: break;
        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &node->tuple.elements) collect_declared_infers(me, &i->type, out);
            break;
        case TYPE_INDEXED:
            collect_declared_infers(me, &node->indexed->interface, out);
            collect_declared_infers(me, &node->indexed->index, out);
            break;
        case TYPE_KEYOF: collect_declared_infers(me, &node->keyof->interface, out); break;
        case TYPE_TERNARY:
            // What its extends type declares is the nested conditional type's own
            collect_declared_infers(me, &node->ternary->maybe_sub, out);
            break;
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) collect_declared_infers(me, i, out);
            break;
        case TYPE_TEMPLATE:
            for_each (i, vec_type, &node->template_->types) collect_declared_infers(me, i, out);
            break;
        default: break;
    }
}

// tsc's isDeferredTypeReferenceNode for a reference to a generic interface or class: the body of an alias (which it then
// goes by), or a reference inside one with defaulted type arguments or ones that may resolve an alias, is a deferred
// reference whose type arguments are lowered on first use (`type RD<T> = ... ReadonlyArray<RD<I>>`). Null otherwise
static Ty const* deferred_interface_reference(
    Checker* me, TypeNode const* node, LowerCtx ctx, Symbol const* alias, TyList alias_args
) {
    IdentifierType const* identifier = node->identifier;
    if (identifier->keyword || identifier->jsdoc_intrinsic || span_valid(identifier->import_span)) return nullptr;
    Symbol const* decl = canonical(me, identifier->target);
    if (!decl || decl->is_placeholder || !(decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS))) return nullptr;
    Ty const* declared = ty_declared_type_of(me, decl);
    if (declared->kind != TY_DECLARED || !declared->declared.type_params.len) return nullptr;
    TyList params = ty_local_type_params(declared);
    u32 written = (u32)vec_spanned_type_size(&identifier->generic_params);
    // A wrong number of type arguments is an error, not deferred
    if (written > params.len) return nullptr;
    for (u32 i = written; i < params.len; i++) {
        if (!ty_param_default(me, params.data[i])) return nullptr;
    }
    if (!alias) {
        bool defer = written != params.len;
        for_each (i, vec_spanned_type, &identifier->generic_params) defer = defer || may_resolve_type_alias(me, &i->type);
        if (!defer) return nullptr;
    }
    return ty_deferred_reference(me, node, ctx, nullptr, alias, alias_args);
}

// Lowered when first needed, like tsc's (getTypeFromConditionalTypeNode only resolves the check and extends types)
static TyCondBranch lower_branch(TypeNode const* node, LowerCtx ctx) {
    return (TyCondBranch){ .node = node, .ctx = ctx };
}

// The check and extends types of a conditional type as they imply a constraint on the types in its true branch (tsc's
// getImpliedConstraint): `[A] extends [B]` implies what `A extends B` does
typedef struct ImpliedConstraint {
    TypeNode const* check_node;
    TypeNode const* extends_node;
    LazyState check_state;
    Ty const* check; // ty_actual_type_variable() of it
    Ty const* extends; // Lowered when first needed
} ImpliedConstraint;

static bool is_unary_tuple_node(TypeNode const* node) {
    if (node->tag != TYPE_TUPLE || vec_tuple_element_size(&node->tuple.elements) != 1) return false;
    TupleElement const* element = vec_tuple_element_at(&node->tuple.elements, 0);
    return !element->rest && !element->optional;
}

static ImpliedConstraint* implied_constraint(Checker* me, TernaryType const* ternary) {
    u64 key[2] = { KEY_IMPLIED_CONSTRAINT, (u64)(uintptr_t)ternary };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (*slot) return (ImpliedConstraint*)*slot;
    ImpliedConstraint* ret = arena_new(&my arena, ImpliedConstraint);
    TypeNode const* check_node = &ternary->maybe_sub;
    TypeNode const* extends_node = &ternary->maybe_super;
    while (is_unary_tuple_node(check_node) && is_unary_tuple_node(extends_node)) {
        check_node = &vec_tuple_element_at(&check_node->tuple.elements, 0)->type;
        extends_node = &vec_tuple_element_at(&extends_node->tuple.elements, 0)->type;
    }
    ret->check_node = check_node;
    ret->extends_node = extends_node;
    *slot = ret;
    return ret;
}

// In the template of a homomorphic mapped type `{ [K in keyof T]: ... }` over a T constrained to arrays and tuples, K
// is also `number | `${number}`` (the other half of tsc's getConditionalFlowTypeOfType). Null if `ty` isn't that K
static Ty const* mapped_key_flow_constraint(Checker* me, InterfaceMember const* mapped, Ty const* ty, Ty const** actual) {
    if (ty->kind != TY_TYPE_PARAM || !mapped->key.mapped.symbol_ref || mapped->key.mapped.in_type.tag != TYPE_KEYOF) {
        return nullptr;
    }
    if (!*actual) *actual = ty_actual_type_variable(me, ty);
    if (*actual != ty_type_param(me, mapped->key.mapped.symbol_ref)) return nullptr;
    // (getHomomorphicTypeVariable: the constraint is `keyof T` for a type parameter T)
    Ty const* keys = ty_from_node(me, &mapped->key.mapped.in_type, (LowerCtx){ .generic = true });
    if (keys->kind != TY_KEYOF) return nullptr;
    Ty const* variable = ty_actual_type_variable(me, keys->keyof.operand);
    if (variable->kind != TY_TYPE_PARAM) return nullptr;
    Ty const* constraint = ty_param_constraint(me, variable);
    if (!constraint) return nullptr;
    TyList parts = constraint->kind == TY_UNION ? constraint->composite.types : (TyList){ .data = &constraint, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        if (!ty_is_tuple(parts.data[i]) && !ty_is_array(me, parts.data[i])) return nullptr;
    }
    Atom const* empty = checker_atom(me, csview_from(""));
    Atom const* texts[2] = { empty, empty };
    Ty const* number = ty_intrinsic(me, TY_NUMBER);
    Ty const* both[2] = { number, ty_template_literal(me, texts, &number, 1) };
    return ty_union(me, both, 2, nullptr, (TyList){0});
}

// tsc's getConditionalFlowTypeOfType: what the true branches around `node` imply about its type `ty`, as a
// substitution type. Only type variables are substituted in parameter positions (where the constraint could be wrong)
static Ty const* conditional_flow_type(Checker* me, TypeNode const* node, Ty const* ty) {
    Ty const* actual = nullptr;
    TyBuf constraints = {0};
    bool type_variable = ty->kind == TY_TYPE_PARAM || ty->kind == TY_INDEXED;
    for (TypeFlow const* flow = node->flow; flow; flow = flow->outer) {
        if (flow->mapped) {
            Ty const* implied = mapped_key_flow_constraint(me, flow->mapped, ty, &actual);
            if (implied) ty_buf_push(&constraints, implied);
            continue;
        }
        if (!flow->covariant && !type_variable) continue;
        ImpliedConstraint* implied = implied_constraint(me, flow->ternary);
        if (implied->check_state == LAZY_STATE_IN_FLIGHT) continue;
        if (implied->check_state == LAZY_STATE_NOT_STARTED) {
            implied->check_state = LAZY_STATE_IN_FLIGHT;
            Ty const* check = ty_from_node(me, implied->check_node, (LowerCtx){ .generic = true });
            implied->check = ty_actual_type_variable(me, check);
            implied->check_state = LAZY_STATE_DONE;
        }
        if (!actual) actual = ty_actual_type_variable(me, ty);
        if (implied->check != actual) continue;
        if (!implied->extends) implied->extends = ty_from_node(me, implied->extends_node, (LowerCtx){ .generic = true });
        ty_buf_push(&constraints, implied->extends);
    }
    if (constraints.len) {
        Ty const* constraint = constraints.len == 1 ? constraints.data[0] :
            ty_intersection(me, constraints.data, constraints.len, nullptr, (TyList){0});
        ty = ty_substitution(me, ty, constraint);
    }
    ty_buf_drop(&constraints);
    return ty;
}

static Ty const* lower_node(Checker* me, TypeNode const* node, LowerCtx outer_ctx, Symbol const* alias, TyList alias_args);

static inline __attribute__((always_inline)) Ty const* lower(
    Checker* me, TypeNode const* node, LowerCtx ctx, Symbol const* alias, TyList alias_args
) {
    Ty const* ret = lower_node(me, node, ctx, alias, alias_args);
    return node->flow ? conditional_flow_type(me, node, ret) : ret;
}

static Ty const* lower_node(Checker* me, TypeNode const* node, LowerCtx outer_ctx, Symbol const* alias, TyList alias_args) {
    // What the operands of this node are lowered with
    LowerCtx ctx = outer_ctx;
    ctx.method = false;
    switch (node->tag) {
        case TYPE_ANY: return ty_intrinsic(me, node->any.self_referential_error ? TY_ERROR : TY_ANY);
        case TYPE_UNKNOWN: return ty_intrinsic(me, TY_UNKNOWN_);
        case TYPE_NEVER: return ty_intrinsic(me, TY_NEVER);
        case TYPE_VOID: return ty_intrinsic(me, TY_VOID);
        case TYPE_BOOLEAN: return ty_intrinsic(me, TY_BOOLEAN);
        case TYPE_NUMBER: return ty_intrinsic(me, TY_NUMBER);
        case TYPE_STRING: return ty_intrinsic(me, TY_STRING);
        case TYPE_SYMBOL: return ty_intrinsic(me, TY_ES_SYMBOL);
        case TYPE_UNIQUE_SYMBOL: return ty_unique_symbol(me, &node->unique_symbol);
        case TYPE_BIGINT: return ty_intrinsic(me, TY_BIGINT);
        case TYPE_VALUE: {
            // tsc checks the literal as an expression, which makes the fresh twin too (it's in the id order)
            Ty const* ret = ty_literal(me, &node->value);
            ty_fresh_literal(me, ret);
            return ret;
        }
        case TYPE_IDENTIFIER: {
            if (alias || ctx.in_alias) {
                Ty const* deferred = deferred_interface_reference(me, node, ctx, alias, alias_args);
                if (deferred) return deferred;
            }
            // tsc's walk checks a type reference (checkSourceElement: its errors go there) before anything asks for its
            // type; tttt's often asks first. A generic reference inside the node being checked stands for it
            CurrentNode saved_node = my current_node;
            if (vec_spanned_type_size(&node->identifier->generic_params) &&
                span_contains_span(ty_current_node_span(me), node->identifier->name)
            ) {
                my current_node = (CurrentNode){ .span = identifier_type_span(node->identifier) };
            }
            Ty const* ret = lower_identifier(me, node->identifier, ctx);
            my current_node = saved_node;
            Symbol const* decl = node->identifier->target;
            // The body of an alias is an instantiation of a generic alias: it goes by the new name
            // Not when a conditional type resolved (`type C = Chars<"ab">` is the union made along the way, tsc's
            // getConditionalType only names a deferred one or a distribution over a union), nor for an intrinsic (`type H = Uppercase<"a" | "b">`)
            // (Syntactically: the alias may be being resolved)
            TypeNode const* body = alias && decl && symbol_is_generic_alias(decl) ? alias_body_node(canonical(me, decl)) : nullptr;
            bool renames = false;
            if (body && !is_intrinsic_body(body) && body->tag != TYPE_KEYOF) {
                // An object type takes the new name when instantiated (tsc's getObjectTypeInstantiation), except a
                // homomorphic mapped type; anything else keeps a name only if it still is what the alias names
                SymbolLinks const* links = symbol_links(me, canonical(me, decl));
                bool object = links->declared_state == LAZY_STATE_DONE ?
                    links->declared_type->kind == TY_ANONYMOUS : body->tag == TYPE_INTERFACE || body->tag == TYPE_CALLABLE;
                // (The declared type's node when there is one: `{ [K in keyof T]: T[K] } & {}` is the mapped type)
                TypeNode const* object_node = links->declared_state == LAZY_STATE_DONE &&
                    links->declared_type->kind == TY_ANONYMOUS && links->declared_type->anonymous.node ?
                    links->declared_type->anonymous.node : body;
                renames = object ? !is_homomorphic_mapped_node(object_node) || ret->kind == TY_UNION : ret->alias == canonical(me, decl);
                // An indexed access distributed over a union index is that union by the new name (getIndexedAccessType),
                // and so is a conditional type distributed over a union (getConditionalTypeInstantiation's mapTypeWithAlias)
                Ty const* declared = links->declared_state == LAZY_STATE_DONE ? links->declared_type : nullptr;
                Ty const* distributes_over = !declared ? nullptr :
                    declared->kind == TY_INDEXED ? declared->indexed.index :
                    declared->kind == TY_CONDITIONAL && declared->conditional.distributive ? declared->conditional.check : nullptr;
                if (!renames && ret->kind == TY_UNION && distributes_over) {
                    Ty const* args_buf[MAX_TYPE_ARGS];
                    TyList params = ty_type_params_of(me, symbol_type_generic(canonical(me, decl)));
                    TyList args = lower_type_args(me, params, 0, &node->identifier->generic_params, ctx, args_buf);
                    TyMapper const* mapper = ty_mapper_new(me, params, args);
                    renames = ty_instantiate(me, distributes_over, mapper)->kind == TY_UNION;
                }
            }
            if (renames) {
                ret = ty_realias(me, ret, alias, alias_args);
            }
            return ret;
        }

        case TYPE_INFERRED: {
            if (!node->inferred->symbol_ref) return ty_intrinsic(me, TY_ERROR);
            Ty const* param = ty_type_param(me, node->inferred->symbol_ref);
            param->param.state->is_infer = true;
            return param;
        }

        case TYPE_INTERFACE:
            return ty_anonymous_from_node(me, node, ctx, alias, alias_args);

        case TYPE_CALLABLE:
            return ty_anonymous_from_node(me, node, outer_ctx, alias, alias_args);

        case TYPE_JSDOC: return lower(me, &node->jsdoc->type, outer_ctx, alias, alias_args);
        case TYPE_PREDICATE: return ty_intrinsic(me, node->predicate->asserts ? TY_VOID : TY_BOOLEAN);

        case TYPE_ARRAY:
            // tsc's isDeferredTypeReferenceNode
            if (alias || (ctx.in_alias && may_resolve_type_alias(me, node->array.element))) {
                return ty_deferred_reference(me, node, ctx, nullptr, alias, alias_args);
            }
            return ty_array(me, ty_from_node(me, node->array.element, ctx), node->array.readonly);

        case TYPE_TUPLE: {
            // A tuple with a variadic element (going by the syntax) isn't deferred in tsc, so it goes by no alias
            // An empty tuple is its target (tsc's isDeferredTypeReferenceNode, getTypeFromArrayOrTupleTypeNode)
            if (!ty_tuple_node_has_variadic(node) && vec_tuple_element_size(&node->tuple.elements) &&
                (alias || (ctx.in_alias && tuple_elements_may_resolve_type_alias(me, node)))
            ) {
                return ty_deferred_reference(me, node, ctx, nullptr, alias, alias_args);
            }
        }
            return lower_tuple(me, node, ctx);

        case TYPE_INDEXED: {
            Ty const* index = ty_from_node(me, &node->indexed->index, ctx);
            Ty const* ret = ty_indexed_written(me, ty_from_node(me, &node->indexed->interface, ctx), index);
            // `T["a"]` written as a type reads an optional property's missing as a real undefined (tsc's
            // getPropertyTypeForIndexType with an IndexedAccessTypeNode)
            if (ty_contains_missing(me, ret)) ret = ty_union(me, (Ty const*[]){ ret, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
            // With a union index the result is the union of what each member accesses, made under the alias's name
            // (tsc's getIndexedAccessType passes the alias symbol to getUnionType)
            bool renames = ret->kind == TY_INDEXED || (ret->kind == TY_UNION && index->kind == TY_UNION);
            return alias && renames ? ty_realias(me, ret, alias, alias_args) : ret;
        }

        case TYPE_KEYOF:
            return ty_keyof(me, ty_from_node(me, &node->keyof->interface, ctx));

        case TYPE_TYPEOF:
            // Widened like tsc's getTypeFromTypeQueryNode: `typeof undefined` is any without strictNullChecks.
            // TODO: `typeof x` of an identifier is its declared type, not the narrowed one (plan 03)
            return ty_regular(me, ty_widened_for_declaration(me, ty_expr_type(me, node->typeof_->expr), false)); // (objects too)

        case TYPE_TERNARY: {
            // The extends type goes before the branches, it's what declares the `infer` type parameters
            LowerCtx inner = ctx;
            inner.generic = true;
            TypeNode const* extends_node = &node->ternary->maybe_super;
            Ty const* check_type = ty_from_node(me, &node->ternary->maybe_sub, ctx);
            Ty const* extends = ty_from_node(me, &node->ternary->maybe_super, inner);
            TyCondBranch if_true = lower_branch(&node->ternary->if_true, inner);
            TyCondBranch if_false = lower_branch(&node->ternary->if_false, ctx);
            // (tsc's isDistributive looks at the check type: a type parameter substituted by an outer true branch
            // isn't one)
            bool distributive = is_naked_type_param_node(&node->ternary->maybe_sub) && check_type->kind == TY_TYPE_PARAM;
            TyBuf infers = {0};
            collect_declared_infers(me, extends_node, &infers);
            Ty const* ret = ty_conditional(
                me, check_type, extends, &if_true, &if_false, nullptr, nullptr, distributive,
                (TyList){ .data = infers.data, .len = infers.len }
            );
            if (ret->kind == TY_CONDITIONAL && ret->conditional.root == ret) {
                // Instantiation must leave them alone: they're this conditional type's own, whatever an outer one of
                // the same declaration inferred (`Split<B, D>` in the true branch of Split, see instantiate.c)
                u64 key[2] = { KEY_CONDITIONAL_INFERS, ret->id };
                if (infers.len && !intern_get(&my links, key, 2)) {
                    TyList* list = arena_new(&my arena, TyList);
                    *list = ty_list_copy(me, infers.data, infers.len);
                    *intern_slot(me, &my links, key, 2) = list;
                }
                conditional_set_outer(me, ret, node);
            }
            ty_buf_drop(&infers);
            return alias && ret->kind == TY_CONDITIONAL ? ty_realias(me, ret, alias, alias_args) : ret;
        }

        case TYPE_UNION:
        case TYPE_INTERSECTION: {
            u32 len = (u32)vec_type_size(&node->composite);
            Ty const** types = malloc(sizeof(Ty const*) * (len ? len : 1));
            u32 placeholders = 0;
            for_each (i, vec_type, &node->composite) {
                // HACK: parse_type() turns the leading operator of `& A & B` into an `any` operand (`never` for `|`),
                // which the legacy code knows to skip. A written `any` is an identifier, so this can't be one
                if (i_index == 0 && node->tag == TYPE_INTERSECTION && i->tag == TYPE_ANY) {
                    placeholders++;
                    continue;
                }
                types[i_index - placeholders] = ty_from_node(me, i, ctx);
            }
            len -= placeholders;
            // A written `string & {}` (or number, bigint) stays as it is: the idiom for "any string, but show the
            // literals" (tsc's NoSupertypeReduction)
            bool keep_empty_object = node->tag == TYPE_INTERSECTION && len == 2 &&
                (types[0] == ty_intrinsic(me, TY_STRING) || types[0] == ty_intrinsic(me, TY_NUMBER) ||
                    types[0] == ty_intrinsic(me, TY_BIGINT)) &&
                vec_type_back(&node->composite)->tag == TYPE_INTERFACE &&
                !vec_interface_member_size(&vec_type_back(&node->composite)->interface.members);
            Ty const* ret = node->tag == TYPE_UNION ? ty_union(me, types, len, alias, alias_args) :
                keep_empty_object ? ty_intersection_keeping_empty_object(me, types, len, alias, alias_args) :
                ty_intersection(me, types, len, alias, alias_args);
            free(types);
            return ret;
        }

        case TYPE_TEMPLATE: {
            TemplateType const* template = node->template_;
            u32 len = (u32)vec_type_size(&template->types);
            Ty const** types = malloc(sizeof(Ty const*) * len);
            Atom const** texts = malloc(sizeof(Atom const*) * (len + 1));
            panic_if(!types || !texts, "out of memory");
            ctx.in_alias = false; // tsc's isResolvedByTypeAlias stops at template literal types
            for_each (i, vec_type, &template->types) {
                types[i_index] = ty_from_node(me, i, ctx);
            }
            for_each (i, vec_cstr, &template->texts) texts[i_index] = checker_atom(me, cstr_sv(i));
            Ty const* ret = ty_template_literal(me, texts, types, len);
            free(types);
            free(texts);
            return ret;
        }

    }
    impossible;
}

Ty const* ty_from_node(Checker* me, TypeNode const* node, LowerCtx ctx) {
    return lower(me, node, ctx, nullptr, (TyList){0});
}
