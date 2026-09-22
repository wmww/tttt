#include "ty/ty_internal.h"
#include "compiler_options.h"
#include "scope.h"
#include "enum_decl.h"
#include "class_decl.h"
#include "interface_decl.h"
#include "statement.h"
#include "binding.h"
#include <math.h>

// Lowering: syntax (`TypeNode`) to semantic types (`Ty`), and what symbols are as types (ty_declared_type_of()) and as
// values (ty_type_of_symbol()). Nodes are syntax only: what the parser made.

static Ty const* lower(Checker* me, TypeNode const* node, LowerCtx ctx, Symbol const* alias, TyList alias_args);

static SymbolLinks* symbol_links(Checker* me, Symbol const* symbol) {
    u64 key[2] = { KEY_SYMBOL_LINKS, (u64)(uintptr_t)symbol };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (!*slot) *slot = arena_new(&my arena, SymbolLinks);
    return (SymbolLinks*)*slot;
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

    u32 len = (u32)vec_generic_item_size(&generic->items);
    Ty const** params = arena_new_n(&my arena, Ty const*, len);
    for_each (i, vec_generic_item, &generic->items) {
        Ty const* param = i->symbol_ref ?
            ty_type_param(me, i->symbol_ref) :
            ty_synthetic_type_param(me, i->name.sv);
        if (!param->param.state->item) param->param.state->item = i;
        params[i_index] = param;
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

// `A.B.name`. TODO(plan 04 stage 3): tsc qualifies a name as far as needed to be reachable from where it's printed
// (symbol accessibility). Always qualifying is what it does for error messages.
static void append_qualified_name(cstr* out, Symbol const* symbol) {
    Symbol const* parent = symbol->parent;
    // Modules and ambient modules (`declare module "m"`) are containers too, but not part of a name
    if (parent && !parent->is_module && !(parent->name.size && (*parent->name.buf == '"' || *parent->name.buf == '\''))) {
        append_qualified_name(out, parent);
        cstr_append_lit(out, ".");
    }
    cstr_append_span(out, symbol->name_span);
}

Atom const* ty_qualified_name(Checker* me, Symbol const* symbol) {
    Symbol const* parent = symbol->parent;
    if (!parent || parent->is_module) return checker_atom(me, symbol->name_span.sv);
    cstr qualified = cstr_null;
    append_qualified_name(&qualified, symbol);
    Atom const* ret = checker_atom(me, cstr_sv(&qualified));
    cstr_drop(&qualified);
    return ret;
}

static Ty const* make_declared(Checker* me, Symbol const* symbol, TyDeclaredKind kind) {
    Ty* ret = ty_alloc(me, TY_DECLARED, TYF_OBJECT);
    ret->declared.kind = kind;
    ret->declared.symbol = symbol;
    ret->declared.name = ty_qualified_name(me, symbol);
    ret->declared.state = arena_new(&my arena, TyObjectState);
    if (kind == TY_DECLARED_INTERFACE || kind == TY_DECLARED_CLASS_INSTANCE) {
        Generic const* first = symbol_type_generic(symbol);
        ret->declared.type_params = ty_type_params_of(me, first);
        for (u32 i = 0; i < symbol->decl_count; i++) {
            if (symbol->decls[i].kind != SYMBOL_DECL_INTERFACE) continue;
            InterfaceDecl const* interface_decl = symbol->decls[i].node;
            if (&interface_decl->generic != first) {
                unify_type_params(me, ret->declared.type_params, &interface_decl->generic);
            }
        }
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

// The type a symbol stands for in a type position: the symbol must be the one that has the type meaning (what
// identifiers keep as `target`). Everything about a placeholder is any
Ty const* ty_declared_type_of(Checker* me, Symbol const* symbol) {
    symbol = canonical(me, symbol);
    if (symbol->is_placeholder) return ty_intrinsic(me, TY_ERROR);
    SymbolLinks* links = symbol_links(me, symbol);
    switch (links->declared_state) {
        case LAZY_STATE_DONE: return links->declared_type;
        case LAZY_STATE_IN_FLIGHT: return ty_intrinsic(me, TY_ERROR); // TODO: TS2456 circular alias
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
            LowerCtx ctx = { .generic = params.len > 0, .in_alias = true };
            ret = lower(me, &type_decl->type, ctx, symbol, params);
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

// Type arguments as written, padded with defaults (then `any`) and truncated to the parameter count
#define MAX_TYPE_ARGS 32
static TyList lower_type_args(
    Checker* me, TyList params, VecSpannedType const* written, LowerCtx ctx, Ty const* args[MAX_TYPE_ARGS]
) {
    panic_if(params.len > MAX_TYPE_ARGS, "too many type parameters");
    u32 written_len = (u32)vec_spanned_type_size(written);
    for (u32 i = 0; i < params.len; i++) {
        if (i < written_len) {
            args[i] = ty_from_node(me, &vec_spanned_type_at(written, i)->type, ctx);
        } else {
            Ty const* fallback = ty_param_default(me, params.data[i]);
            if (fallback) {
                // A default can mention the parameters before it
                TyMapper mapper = { .len = i, .sources = params.data, .targets = args };
                fallback = ty_instantiate(me, fallback, &mapper);
            }
            // TODO: TS2314 when there's no default. tsc fills in the error type too
            args[i] = fallback ? fallback : ty_intrinsic(me, written_len ? TY_ERROR : TY_ANY);
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
        *intern_slot(me, &my links, stack_key, count) = ret;
    }
    return ret;
}

static Ty const* lower_identifier(Checker* me, IdentifierType const* identifier, LowerCtx ctx);

static bool symbol_is_generic_alias(Symbol const* symbol) {
    return symbol->flags & SYMBOL_TYPE_ALIAS && !symbol->is_placeholder && symbol_type_generic(symbol);
}

static Ty const* lower_identifier(Checker* me, IdentifierType const* identifier, LowerCtx ctx) {
    Symbol const* decl = canonical(me, identifier->target);
    if (span_valid(identifier->import_span)) {
        // `import("./a").B`: an unresolved one was reported, or is any by design (shorthand ambient module)
        if (!decl || decl->is_placeholder) return ty_intrinsic(me, TY_ANY);
        if (identifier->import_typeof) {
            Ty const* ret = ty_type_of_symbol(me, decl);
            isize count = vec_span_size(&identifier->namespaces) + 1;
            for (isize i = identifier->import_prop_start; i >= 0 && i < count; i++) {
                Span name = i == count - 1 ? identifier->name : *vec_span_at(&identifier->namespaces, i);
                Prop const* prop = ty_get_prop(me, ret, checker_atom(me, name.sv));
                if (!prop) return ty_intrinsic(me, TY_ANY); // TODO: TS2339
                ret = prop_type(me, prop);
            }
            return ret;
        }
    }
    if (!decl) {
        if (identifier->keyword || !span_valid(identifier->name)) return ty_intrinsic(me, TY_ERROR);
        cstr name = cstr_null;
        for_each (i, vec_span, &identifier->namespaces) {
            cstr_append_sv(&name, i->sv);
            cstr_append(&name, ".");
        }
        cstr_append_sv(&name, identifier->name.sv);
        Ty const* ret = ty_unresolved(me, cstr_sv(&name));
        cstr_drop(&name);
        return ret;
    }
    Ty const* args_buf[MAX_TYPE_ARGS];

    if (identifier->this_kind == THIS_CLASS_STATIC) return ty_static_type_of(me, decl);

    Ty const* declared = ty_declared_type_of(me, decl);
    if (decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) {
            if (declared->kind == TY_DECLARED && declared->declared.type_params.len) {
                // TODO: `this` and a bare `C` inside generic class C are the class's own instantiation
                // (type arguments = its type parameters), and `this` is really its own type parameter
                if (identifier->keyword) {
                    if (ctx.generic) return ty_reference(me, declared, declared->declared.type_params);
                }
                TyList args = lower_type_args(me, declared->declared.type_params, &identifier->generic_params, ctx, args_buf);
                return ty_reference(me, declared, args);
            }
            return declared;
    }
    if (symbol_is_generic_alias(decl)) {
        TyList params = ty_type_params_of(me, symbol_type_generic(decl));
        TyList args = lower_type_args(me, params, &identifier->generic_params, ctx, args_buf);
        return alias_instance(me, decl, declared, params, args);
    }
    return declared;
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
    switch (node->tag) {
        case TYPE_ANY:
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

        case TYPE_IDENTIFIER: {
            IdentifierType const* identifier = node->identifier;
            Symbol const* decl = identifier->target;
            if (identifier->keyword) {
                out->unknown = true; // `this` is the enclosing class applied to its type parameters
            } else if (decl && decl->flags & SYMBOL_TYPE_PARAMETER) {
                free_params_add(out, ty_type_param(me, decl));
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
                        out->unknown = true;
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

        case TYPE_TYPEOF:
            // Not syntax: the type of an expression, which could be about any type parameter in scope
            out->unknown = true;
            break;
    }
}

// A signature written as a type, or an overload signature. A parameter without a type is `any`
static void collect_free_params_of_function(Checker* me, FunctionImpl const* function, FreeParams* out) {
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
            // The implementation of overloads isn't a signature
            if (((FunctionImpl const*)decl->node)->has_body) once = SYMBOL_FUNCTION;
            break;
        case SYMBOL_DECL_ENUM: once = SYMBOL_ENUM; break; // One object for every declaration of the enum
        case SYMBOL_DECL_NAMESPACE: once = SYMBOL_VALUE_MODULE; break; // And one for every block of a namespace
        default: break;
    }
    for (u32 i = 0; i < index; i++) {
        if (symbol->decls[i].flags & once) return false;
    }
    return true;
}

static Ty const* value_entry_type(Checker* me, Symbol const* symbol, SymbolDecl const* decl) {
    switch (decl->kind) {
        case SYMBOL_DECL_VARIABLE: {
            Binding const* binding = decl->node;
            Ty const* type = ty_binding_type(me, binding, (LowerCtx){ .generic = true });
            // `x?: T` is read as `T | undefined` inside the function. TODO: tsc also shows it in the signature
            if (binding->optional_parameter && compiler_options()->strict_null_checks && !(type->flags & TYF_ANY)) {
                Ty const* types[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
                type = ty_union(me, types, 2, nullptr, (TyList){0});
            }
            return type;
        }
        case SYMBOL_DECL_BUILTIN: return ty_from_node(me, decl->node, (LowerCtx){0});
        case SYMBOL_DECL_EXPR: return ty_expr_type(me, decl->node);
        case SYMBOL_DECL_FUNCTION: return lazy_type_get((LazyType*)&((FunctionImpl const*)decl->node)->lazy_type);
        case SYMBOL_DECL_CLASS:
        case SYMBOL_DECL_ENUM:
            return ty_static_type_of(me, symbol);
        case SYMBOL_DECL_ENUM_MEMBER: return ty_declared_type_of(me, symbol);
        case SYMBOL_DECL_NAMESPACE:
        case SYMBOL_DECL_ALIAS: // `import * as ns`
            return ty_namespace_type(me, symbol);
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

// The type of a symbol as a value. Give it the symbol a name found or the one that has the value meaning (`target`
// of an identifier), aliases are seen through. Everything about a placeholder is any
Ty const* ty_type_of_symbol(Checker* me, Symbol const* symbol) {
    symbol = canonical(me, symbol_for_meaning(symbol, SYMBOL_VALUE));
    if (!symbol || symbol->is_placeholder) return ty_intrinsic(me, TY_ANY);
    // `typeof import("./a")`, `import("./a")`, `globalThis`
    if (symbol->is_module || symbol->is_global_this) return ty_namespace_type(me, symbol);
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
    return value_entries(me, symbol, out);
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
    return element->optional ? ty_optional_param_type(me, type) : type;
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
static bool tuple_node_has_variadic(TypeNode const* node) {
    for_each (i, vec_tuple_element, &node->tuple.elements) {
        if (i->rest && !array_element_type_node(&i->type)) return true;
    }
    return false;
}

// Deferred references, see Ty.reference.deferred

// Whether lowering the node right now would run into a type alias that is being resolved. Object types aren't looked
// into, their members are lazy anyway
static bool node_reaches_alias_in_flight(Checker* me, TypeNode const* node, u32 depth) {
    if (depth > 32) return false;
    switch (node->tag) {
        case TYPE_IDENTIFIER: {
            Symbol const* decl = canonical(me, node->identifier->target);
            if (decl && decl->flags & SYMBOL_TYPE_ALIAS) {
                u64 key[2] = { KEY_SYMBOL_LINKS, (u64)(uintptr_t)decl };
                SymbolLinks const* links = intern_get(&my links, key, 2);
                if (links && links->declared_state == LAZY_STATE_IN_FLIGHT) return true;
            }
            for_each (i, vec_spanned_type, &node->identifier->generic_params) {
                if (node_reaches_alias_in_flight(me, &i->type, depth + 1)) return true;
            }
            return false;
        }
        case TYPE_ARRAY: return node_reaches_alias_in_flight(me, node->array.element, depth + 1);
        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &node->tuple.elements) {
                if (node_reaches_alias_in_flight(me, &i->type, depth + 1)) return true;
            }
            return false;
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                if (node_reaches_alias_in_flight(me, i, depth + 1)) return true;
            }
            return false;
        default: return false;
    }
}

static bool node_reaches_alias_in_flight_deep(Checker* me, TypeNode const* node) {
    if (node_reaches_alias_in_flight(me, node, 0)) return true;
    switch (node->tag) {
        case TYPE_TERNARY:
            return node_reaches_alias_in_flight_deep(me, &node->ternary->maybe_sub) ||
                node_reaches_alias_in_flight_deep(me, &node->ternary->maybe_super) ||
                node_reaches_alias_in_flight_deep(me, &node->ternary->if_true) ||
                node_reaches_alias_in_flight_deep(me, &node->ternary->if_false);
        case TYPE_INDEXED:
            return node_reaches_alias_in_flight_deep(me, &node->indexed->interface) ||
                node_reaches_alias_in_flight_deep(me, &node->indexed->index);
        case TYPE_KEYOF: return node_reaches_alias_in_flight_deep(me, &node->keyof->interface);
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                if (node_reaches_alias_in_flight_deep(me, i)) return true;
            }
            return false;
        default: return false;
    }
}

static Ty const* deferred_reference_target(Checker* me, TypeNode const* node) {
    if (node->tag == TYPE_TUPLE) return tuple_target_of_node(me, node);
    ty_array(me, ty_intrinsic(me, TY_ANY), false); // Makes sure the targets are looked up
    ty_array(me, ty_intrinsic(me, TY_ANY), true);
    return node->array.readonly ? my readonly_array_target : my array_target;
}

Ty const* ty_deferred_reference(
    Checker* me, TypeNode const* node, LowerCtx ctx, TyMapper const* mapper, Symbol const* alias, TyList alias_args
) {
    u64 stack_key[64];
    u32 count = 2 + (mapper ? mapper->len * 2 : 0);
    u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
    key[0] = KEY_DEFERRED_REFERENCE;
    key[1] = (u64)(uintptr_t)node;
    for (u32 i = 0; mapper && i < mapper->len; i++) {
        key[2 + i * 2] = mapper->sources[i]->id;
        key[3 + i * 2] = mapper->targets[i]->id;
    }
    Ty const* ret = intern_get(&my links, key, count);
    if (!ret) {
        Ty const* target = deferred_reference_target(me, node);
        // Whether it mentions type variables isn't known until the arguments resolve
        Ty* fresh = ty_alloc(me, TY_REFERENCE, TYF_OBJECT | (ctx.generic ? TYF_COULD_CONTAIN_TYPE_VARIABLES : 0));
        fresh->reference.target = target;
        fresh->reference.state = arena_new(&my arena, TyObjectState);
        TyDeferredRef* deferred = arena_new(&my arena, TyDeferredRef);
        deferred->node = node;
        deferred->ctx = ctx;
        deferred->ctx.in_alias = false;
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
        for (u32 i = 0; i < len; i++) args[i] = ty_intrinsic(me, TY_ERROR);
        return (TyList){ .data = args, .len = len };
    }
    deferred->state = LAZY_STATE_IN_FLIGHT;
    TypeNode const* node = deferred->node;
    for (u32 i = 0; i < len; i++) {
        Ty const* element = node->tag == TYPE_ARRAY ? ty_from_node(me, node->array.element, deferred->ctx) :
            tuple_element_type(me, vec_tuple_element_at(&node->tuple.elements, i), deferred->ctx);
        args[i] = ty_instantiate(me, element, deferred->mapper);
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


// `const { a } = x.y` after `if (x.y.a)`: an element of a top-level object pattern whose initializer is a reference
// has the flow type of `x.y.a` there (tsc's getFlowTypeOfDestructuring, through a synthetic access made once)
static Ty const* destructured_flow_type(Checker* me, Binding const* binding, Ty const* declared) {
    Binding const* parent = binding->parent;
    ObjectBindingItem const* item = binding->parent_item;
    if (parent->parent || parent->tag != BINDING_OBJECT || binding->rest || item->computed_key || !item) return declared;
    if (item->source.tag != VALUE_STRING || !parent->source || parent->source->tag != LAZY_TYPE_INITIALIZED_BINDING) {
        return declared;
    }
    InitializedBinding const* initialized = c_container_of(parent->source, InitializedBinding, lazy_type);
    Expr const* object = &initialized->value;
    FlowNode* flow = object->tag == EXPR_IDENTIFIER ? object->identifier.flow :
        object->tag == EXPR_PROPERTY ? object->property->flow : nullptr;
    if (!flow) return declared;
    u64 key[2] = { KEY_SYNTHETIC_ACCESS, (u64)(uintptr_t)binding };
    Expr const* access = intern_get(&my links, key, 2);
    if (!access) {
        PropertyExpr* property = arena_new(&my arena, PropertyExpr);
        *property = (PropertyExpr){
            .expr = *object,
            .prop = expr_literal(item->source_span, item->source),
            .form = PROPERTY_EXPR_DOT,
            .flow = flow,
        };
        Expr* synthetic = arena_new(&my arena, Expr);
        *synthetic = (Expr){
            .tag = EXPR_PROPERTY, .property = property, .inner_span = item->source_span, .outer_span = item->source_span,
            .lazy_type.tag = LAZY_TYPE_EXPR,
        };
        *intern_slot(me, &my links, key, 2) = synthetic;
        access = synthetic;
    }
    return ty_flow_type(me, access, declared, declared, flow);
}

// What is assigned to a binding: the initializer for the root of a pattern, for an element the property or element
// of what its pattern is assigned
Ty const* ty_binding_assigned_type(Checker* me, Binding const* binding) {
    Binding const* parent = binding->parent;
    if (!parent) return binding->source ? lazy_type_get(binding->source) : ty_intrinsic(me, TY_ANY);
    Ty const* object = ty_binding_type(me, parent, (LowerCtx){ .generic = true });
    bool in_array = parent->tag == BINDING_ARRAY;
    // TODO: iterables. No TS2339-style errors from here either
    if (in_array && !ty_extends_array(me, object)) return ty_intrinsic(me, TY_ANY);
    Ty const* ret;
    if (binding->rest && in_array) {
        ret = binding_rest_elements(me, object, binding->parent_index);
    } else if (binding->rest) {
        ret = object; // TODO: without the properties the pattern names, methods and accessors (tsc's getRestType())
    } else if (in_array) {
        Value index = value_number(binding->parent_index);
        ret = ty_access(me, object, ty_literal(me, &index));
    } else {
        ObjectBindingItem const* item = binding->parent_item;
        ret = ty_access(me, object, item->computed_key ? ty_expr_type(me, item->computed_key) : ty_literal(me, &item->source));
        if (ret && !parent->has_explicit_type) ret = destructured_flow_type(me, binding, ret);
    }
    if (!ret) ret = ty_intrinsic(me, TY_ANY);
    if (binding->default_value) {
        // `[x = 1]`: undefined is replaced by the default. Under a written type, the default only removes undefined
        // (unless it can be undefined itself), else the two are a subtype-reduced union (tsc's getTypeForBindingElement)
        Binding const* root = parent;
        while (root->parent) root = root->parent;
        Ty const* types[2] = { ty_without_undefined(me, ret), nullptr };
        if (root->has_explicit_type) {
            Ty const* default_type = ty_expr_type(me, binding->default_value);
            if (compiler_options()->strict_null_checks && !(default_type->flags & (TYF_ANY | TYF_UNKNOWN)) &&
                ty_without_undefined(me, default_type) == default_type) {
                ret = types[0];
            }
            return ret;
        }
        // Not ty_check_expr() with types[0] as the contextual type: only the uncontextual path detects `[x = x]`
        types[1] = ty_widened_for_declaration(me, ty_expr_type(me, binding->default_value), true);
        ret = ret->flags & TYF_ANY ? ret : ty_union_subtype_reduced(me, types, 2);
    }
    return ret;
}

// The type of a binding: what is written, else what is assigned. `ctx` is what a written type is lowered with
Ty const* ty_binding_type(Checker* me, Binding const* binding, LowerCtx ctx) {
    if (binding->has_explicit_type) return ty_from_node(me, &binding->explicit_type, ctx);
    return ty_binding_assigned_type(me, binding);
}

static bool is_naked_type_param_node(TypeNode const* node) {
    if (node->tag != TYPE_IDENTIFIER || !node->identifier->target) return false;
    Symbol const* decl = node->identifier->target;
    return (decl->flags & SYMBOL_TYPE_PARAMETER) != 0;
}

static bool node_reaches_alias_in_flight_deep(Checker* me, TypeNode const* node);

static TyCondBranch lower_branch(Checker* me, TypeNode const* node, LowerCtx ctx) {
    if (node_reaches_alias_in_flight_deep(me, node)) {
        ctx.in_alias = false;
        return (TyCondBranch){ .node = node, .ctx = ctx };
    }
    return (TyCondBranch){ .type = ty_from_node(me, node, ctx) };
}

static Ty const* lower(Checker* me, TypeNode const* node, LowerCtx outer_ctx, Symbol const* alias, TyList alias_args) {
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
        case TYPE_BIGINT: return ty_intrinsic(me, TY_BIGINT);
        case TYPE_VALUE: return ty_literal(me, &node->value);
        case TYPE_IDENTIFIER: {
            Ty const* ret = lower_identifier(me, node->identifier, ctx);
            Symbol const* decl = node->identifier->target;
            // The body of an alias is an instantiation of a generic alias: it goes by the new name
            if (alias && decl && symbol_is_generic_alias(decl)) {
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

        case TYPE_ARRAY:
            if ((alias || (ctx.in_alias && node_reaches_alias_in_flight(me, node, 0)))) {
                return ty_deferred_reference(me, node, ctx, nullptr, alias, alias_args);
            }
            return ty_array(me, ty_from_node(me, node->array.element, ctx), node->array.readonly);

        case TYPE_TUPLE: {
            // A tuple with a variadic element (going by the syntax) isn't deferred in tsc, so it goes by no alias
            bool variadic = tuple_node_has_variadic(node);
            if (((alias && !variadic) || (ctx.in_alias && node_reaches_alias_in_flight(me, node, 0)))) {
                return ty_deferred_reference(me, node, ctx, nullptr, variadic ? nullptr : alias, alias_args);
            }
        }
            return lower_tuple(me, node, ctx);

        case TYPE_INDEXED: {
            Ty const* ret = ty_indexed(
                me,
                ty_from_node(me, &node->indexed->interface, ctx),
                ty_from_node(me, &node->indexed->index, ctx)
            );
            return alias && ret->kind == TY_INDEXED ? ty_realias(me, ret, alias, alias_args) : ret;
        }

        case TYPE_KEYOF:
            return ty_keyof(me, ty_from_node(me, &node->keyof->interface, ctx));

        case TYPE_TYPEOF:
            // TODO: `typeof x` of an identifier is its declared type, not the narrowed one (plan 03)
            return ty_expr_type(me, node->typeof_->expr);

        case TYPE_TERNARY: {
            // The extends type goes before the branches, it's what declares the `infer` type parameters
            LowerCtx inner = ctx;
            inner.generic = true;
            // `F extends (...args: infer A) => any`: an `infer` type in rest position is implicitly constrained to
            // `unknown[]`. Set up front, the signature itself is only resolved when somebody looks at it
            TypeNode const* extends_node = &node->ternary->maybe_super;
            if (extends_node->tag == TYPE_CALLABLE) {
                for_each (i, vec_function_arg, &extends_node->callable->function->args) {
                    Binding const* binding = &i->initializer.binding;
                    if (!i->rest || !binding->has_explicit_type || binding->explicit_type.tag != TYPE_INFERRED) continue;
                    TypeNode const* arg_type = &binding->explicit_type;
                    if (!arg_type->inferred->symbol_ref) continue;
                    Ty const* param = ty_type_param(me, arg_type->inferred->symbol_ref);
                    if (!ty_param_constraint(me, param)) {
                        param->param.state->constraint = ty_array(me, ty_intrinsic(me, TY_UNKNOWN_), false);
                        param->param.state->constraint_state = LAZY_STATE_DONE;
                    }
                }
            }
            Ty const* check_type = ty_from_node(me, &node->ternary->maybe_sub, ctx);
            Ty const* extends = ty_from_node(me, &node->ternary->maybe_super, inner);
            TyCondBranch if_true = lower_branch(me, &node->ternary->if_true, inner);
            TyCondBranch if_false = lower_branch(me, &node->ternary->if_false, ctx);
            bool distributive = is_naked_type_param_node(&node->ternary->maybe_sub);
            Ty const* ret = ty_conditional(me, check_type, extends, &if_true, &if_false, nullptr, nullptr, distributive);
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
            Ty const* ret = node->tag == TYPE_UNION ?
                ty_union(me, types, len, alias, alias_args) :
                ty_intersection(me, types, len, alias, alias_args);
            free(types);
            return ret;
        }

    }
    impossible;
}

Ty const* ty_from_node(Checker* me, TypeNode const* node, LowerCtx ctx) {
    return lower(me, node, ctx, nullptr, (TyList){0});
}
