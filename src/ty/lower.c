#include "ty/ty_internal.h"
#include "scope.h"
#include "enum_decl.h"
#include "class_decl.h"
#include <math.h>

// Lowering: syntax (`TypeNode`) to semantic types (`Ty`). Besides what the parser makes, nodes include the links name
// resolution sets up between declarations and what gives them their type: TYPE_REF (another node), TYPE_LAZY (the
// LazyType of an expression, binding, function, class...) and the TYPE_INDEXED of a destructuring binding.

static Ty const* lower(Checker* me, TypeNode const* node, LowerCtx ctx, Declaration const* alias, TyList alias_args);

static DeclLinks* decl_links(Checker* me, Declaration const* decl) {
    u64 key[2] = { KEY_DECL_LINKS, (u64)(uintptr_t)decl };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (!*slot) *slot = arena_new(&my arena, DeclLinks);
    return (DeclLinks*)*slot;
}

TyList ty_type_params_of(Checker* me, Generic const* generic) {
    if (generic_empty(generic)) return (TyList){0};
    u64 key[2] = { KEY_GENERIC_PARAMS, (u64)(uintptr_t)generic };
    TyList const* existing = intern_get(&my links, key, 2);
    if (existing) return *existing;

    u32 len = (u32)vec_generic_item_size(&generic->items);
    Ty const** params = arena_new_n(&my arena, Ty const*, len);
    for_each (i, vec_generic_item, &generic->items) {
        Ty const* param = i->declaration_ref ?
            ty_type_param(me, i->declaration_ref) :
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
        if ((u32)i_index >= params.len || !i->declaration_ref) break;
        u64 key[2] = { KEY_TYPE_PARAM, (u64)(uintptr_t)i->declaration_ref };
        void const** slot = intern_slot(me, &my links, key, 2);
        if (!*slot) *slot = params.data[i_index];
    }
}

static Ty const* registered_type_param(Checker* me, Declaration const* decl) {
    // `infer X` and mapped type keys are DECLARATION_TYPE declarations like aliases are. They're type parameters if
    // whatever introduced them was lowered first, which it is since they're only in scope inside of it.
    u64 key[2] = { KEY_TYPE_PARAM, (u64)(uintptr_t)decl };
    return intern_get(&my links, key, 2);
}

// `A.B.name`. TODO(plan 03): tsc qualifies a name as far as needed to be reachable from where it's printed, which
// needs symbols and an enclosing declaration. Always qualifying is what it does for error messages.
static Atom const* qualified_name(Checker* me, VecSpan const* namespaces, Span name) {
    if (!vec_span_size(namespaces)) return checker_atom(me, name.sv);
    cstr qualified = cstr_null;
    for_each (i, vec_span, namespaces) {
        // Ambient modules (`declare module "m"`) are namespaces too, but not part of a name
        if (i->sv.size && (*i->sv.buf == '"' || *i->sv.buf == '\'')) continue;
        cstr_append_span(&qualified, *i);
        cstr_append_lit(&qualified, ".");
    }
    cstr_append_span(&qualified, name);
    Atom const* ret = checker_atom(me, cstr_sv(&qualified));
    cstr_drop(&qualified);
    return ret;
}

Atom const* ty_qualified_name(Checker* me, Declaration const* decl) {
    return qualified_name(me, &decl->namespaces, decl->name);
}

static Ty const* make_declared(Checker* me, Declaration const* decl, TyDeclaredKind kind) {
    Ty* ret = ty_alloc(me, TY_DECLARED, TYF_OBJECT);
    ret->declared.kind = kind;
    ret->declared.decl = decl;
    ret->declared.name = qualified_name(me, &decl->namespaces, decl->name);
    ret->declared.state = arena_new(&my arena, TyObjectState);
    if (kind == TY_DECLARED_INTERFACE || kind == TY_DECLARED_CLASS_INSTANCE) {
        ret->declared.type_params = ty_type_params_of(me, decl->type_generic);
        for_each (node, vec_decl_node, &decl->nodes) {
            if (node->kind != DECL_NODE_INTERFACE) continue;
            InterfaceDecl const* interface_decl = node->ptr;
            if (&interface_decl->generic != decl->type_generic) {
                unify_type_params(me, ret->declared.type_params, &interface_decl->generic);
            }
        }
    }
    return ret;
}

void ty_set_declared_type(Checker* me, Declaration const* decl, Ty const* type) {
    DeclLinks* links = decl_links(me, decl);
    links->declared_type = type;
    links->declared_state = LAZY_STATE_DONE;
}

// The (first) AST node of the given kind a declaration was made from
static void const* decl_node(Declaration const* decl, DeclNodeKind kind) {
    for_each (i, vec_decl_node, &decl->nodes) {
        if (i->kind == kind) return i->ptr;
    }
    return nullptr;
}

// An enum member's type is made when its enum's is
static Ty const* enum_member_type(Checker* me, Declaration const* decl) {
    EnumCase const* enum_case = decl_node(decl, DECL_NODE_ENUM_CASE);
    if (!enum_case || !enum_case->parent || !enum_case->parent->declaration.get) return ty_intrinsic(me, TY_ERROR);
    Ty const* enum_type = ty_declared_type_of(me, enum_case->parent->declaration.get);
    DeclLinks* links = decl_links(me, decl);
    if (links->declared_state == LAZY_STATE_DONE) return links->declared_type;
    // The enum is being resolved further up the stack and hasn't got to this member yet
    (void)enum_type;
    return nullptr;
}

Ty const* ty_declared_type_of(Checker* me, Declaration const* decl) {
    DeclLinks* links = decl_links(me, decl);
    switch (links->declared_state) {
        case LAZY_STATE_DONE: return links->declared_type;
        case LAZY_STATE_IN_FLIGHT: return ty_intrinsic(me, TY_ERROR); // TODO: TS2456 circular alias
        case LAZY_STATE_NOT_STARTED: break;
    }
    links->declared_state = LAZY_STATE_IN_FLIGHT;
    Ty const* ret;
    switch (decl->type_tag) {
        case DECLARATION_INTERFACE:
            ret = make_declared(me, decl, TY_DECLARED_INTERFACE);
            break;

        case DECLARATION_CLASS:
            ret = make_declared(me, decl, decl->is_typeof ? TY_DECLARED_CLASS_STATIC : TY_DECLARED_CLASS_INSTANCE);
            break;

        case DECLARATION_ENUM:
            ret = ty_resolve_enum(me, decl);
            break;

        case DECLARATION_ENUM_CASE:
        case DECLARATION_ENUM_CASE_TYPE:
            if (decl->type_tag == DECLARATION_ENUM_CASE_TYPE && decl->is_typeof) {
                ret = make_declared(me, decl, TY_DECLARED_ENUM_OBJECT);
                break;
            }
            ret = enum_member_type(me, decl);
            if (!ret) {
                links->declared_state = LAZY_STATE_NOT_STARTED;
                return ty_intrinsic(me, TY_ERROR);
            }
            break;

        case DECLARATION_TYPE: {
            DeclEntries entries = declaration_type_entries(decl);
            if (entries.len != 1) {
                ret = ty_intrinsic(me, TY_ERROR);
            } else if (decl->is_typeof) {
                ret = ty_from_node(me, &entries.data[0], (LowerCtx){0});
                // `import * as ns`: the module's value goes by the name it was imported as (`typeof ns`)
                if (ret->kind == TY_DECLARED && ret->declared.kind == TY_DECLARED_NAMESPACE && !ret->declared.name->len) {
                    Ty* named = ty_alloc(me, TY_DECLARED, TYF_OBJECT);
                    named->declared = ret->declared; // Shares the members
                    named->declared.name = checker_atom(me, decl->name.sv);
                    ret = named;
                }
            } else {
                TyList params = ty_type_params_of(me, decl->type_generic);
                ret = lower(me, &entries.data[0], (LowerCtx){ .generic = params.len > 0, .in_alias = true }, decl, params);
            }
        }   break;

        case DECLARATION_GENERIC_TYPE:
            ret = ty_type_param(me, decl);
            break;

        default:
            ret = ty_intrinsic(me, TY_ERROR);
            break;
    }
    // decl_links() pointers are stable (arena), the table growing in the meantime doesn't matter
    links->declared_type = ret;
    links->declared_state = LAZY_STATE_DONE;
    return ret;
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

static Ty const* alias_instance(Checker* me, Declaration const* decl, Ty const* body, TyList params, TyList args) {
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

static Ty const* lower_identifier(Checker* me, IdentifierType const* identifier, LowerCtx ctx) {
    Declaration const* decl = identifier->decl_ref;
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

    Ty const* param = registered_type_param(me, decl);
    if (param) return param;

    if (decl->type_tag == DECLARATION_TYPE && !decl->type_generic && !vec_span_size(&decl->declarations)) {
        // HACK: the `object` keyword is a builtin alias for the legacy checker's empty interface
        if (csview_equals(decl->name.sv, "object")) return ty_intrinsic(me, TY_NON_PRIMITIVE);
    }

    Ty const* declared = ty_declared_type_of(me, decl);
    switch (decl->type_tag) {
        case DECLARATION_INTERFACE:
        case DECLARATION_CLASS:
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

        case DECLARATION_TYPE:
            if (decl->type_generic && !decl->is_typeof) {
                TyList params = ty_type_params_of(me, decl->type_generic);
                TyList args = lower_type_args(me, params, &identifier->generic_params, ctx, args_buf);
                return alias_instance(me, decl, declared, params, args);
            }
            return declared;

        default:
            return declared;
    }
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

static void collect_free_params_of_generic(Checker* me, Generic const* generic, FreeParams* out) {
    for_each (i, vec_generic_item, &generic->items) {
        if (i->declaration_ref) free_params_bind(out, ty_type_param(me, i->declaration_ref));
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
            Declaration const* decl = identifier->decl_ref;
            if (identifier->keyword) {
                out->unknown = true; // `this` is the enclosing class applied to its type parameters
            } else if (decl) {
                Ty const* param = decl->type_tag == DECLARATION_GENERIC_TYPE ?
                    ty_type_param(me, decl) :
                    registered_type_param(me, decl);
                if (param) free_params_add(out, param);
            }
            for_each (i, vec_spanned_type, &identifier->generic_params) {
                collect_free_params(me, &i->type, out);
            }
        }   break;

        case TYPE_INFERRED:
            if (node->inferred->declaration_ref) {
                free_params_bind(out, ty_type_param(me, node->inferred->declaration_ref));
            }
            collect_free_params(me, &node->inferred->extends, out);
            break;

        case TYPE_CALLABLE:
            collect_free_params_of_generic(me, &node->callable->generic, out);
            for_each (i, vec_callable_arg, &node->callable->args) {
                collect_free_params(me, &i->type, out);
            }
            collect_free_params(me, &node->callable->ret, out);
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
                        if (i->key.mapped.declaration_ref) {
                            free_params_bind(out, ty_type_param(me, i->key.mapped.declaration_ref));
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
            if (node->indexed->is_from_binding) out->unknown = true;
            collect_free_params(me, &node->indexed->interface, out);
            collect_free_params(me, &node->indexed->index, out);
            break;

        case TYPE_KEYOF:
            collect_free_params(me, &node->keyof->interface, out);
            break;

        case TYPE_TERNARY:
            collect_free_params(me, &node->ternary->maybe_sub, out);
            collect_free_params(me, &node->ternary->maybe_super, out);
            collect_free_params(me, &node->ternary->if_true, out);
            collect_free_params(me, &node->ternary->if_false, out);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                collect_free_params(me, i, out);
            }
            break;

        case TYPE_REF:
            collect_free_params(me, node->ref, out);
            break;

        case TYPE_TYPEOF:
        case TYPE_LAZY:
            // Not syntax: the type of an expression, which could be about any type parameter in scope
            out->unknown = true;
            break;
    }
}

static void anonymous_set_free_params(Checker* me, Ty* ty, TypeNode const* const* nodes, u32 count) {
    if (!ty->anonymous.ctx.generic) return; // No type parameter in scope, nothing to find
    FreeParams found = {0};
    for (u32 i = 0; i < count; i++) collect_free_params(me, nodes[i], &found);
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

Ty const* ty_anonymous_from_node(Checker* me, TypeNode const* node, LowerCtx ctx, Declaration const* alias, TyList alias_args) {
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
    anonymous_set_free_params(me, ret, &node, 1);
    // Collecting can intern type parameters, so the slot is looked up again
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

Ty const* ty_anonymous_from_overloads(Checker* me, TypeNode const* const* overloads, u32 count, LowerCtx ctx) {
    ctx.in_alias = false;
    u64 key[2] = { KEY_NODE, (u64)(uintptr_t)overloads };
    Ty const* existing = intern_get(&my links, key, 2);
    if (existing) return existing;

    Ty* ret = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT);
    ret->anonymous.node = overloads[0];
    ret->anonymous.overloads = overloads;
    ret->anonymous.overload_count = count;
    ret->anonymous.ctx = ctx;
    ret->anonymous.state = arena_new(&my arena, TyObjectState);
    anonymous_set_free_params(me, ret, overloads, count);
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

static Ty* make_namespace(Checker* me, Symbol const* symbol) {
    Ty* ret = ty_alloc(me, TY_DECLARED, TYF_OBJECT);
    ret->declared.kind = TY_DECLARED_NAMESPACE;
    ret->declared.namespace_symbol = symbol;
    // No name: a module (`import * as ns`), which prints as its structure
    ret->declared.name = symbol->is_module ? checker_atom(me, c_sv("")) : ty_qualified_name(me, &symbol->decl);
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

// The value of a declaration that several declarations contribute a value to: function overloads, a function, class
// or enum merged with a namespace. The members of all of them (members.c: builder_add_merged_value)
Ty const* ty_merged_value_type(Checker* me, Declaration const* decl) {
    u64 key[2] = { KEY_MERGED_VALUE, (u64)(uintptr_t)decl };
    Ty const* existing = intern_get(&my links, key, 2);
    if (existing) return existing;

    // With a namespace, class or enum among them it goes by name (`typeof f`), overloads alone are anonymous
    Ty const* named = nullptr;
    for_each (i, vec_type, &decl->merged_value_type.merge_list) {
        Ty const* entry = ty_from_node(me, i, (LowerCtx){ .generic = true });
        if (entry->kind == TY_DECLARED && !named) named = entry;
    }
    Ty* ret;
    if (named) {
        ret = named->declared.kind == TY_DECLARED_NAMESPACE ?
            make_namespace(me, named->declared.namespace_symbol) :
            ty_alloc(me, TY_DECLARED, TYF_OBJECT);
        if (named->declared.kind != TY_DECLARED_NAMESPACE) {
            ret->declared = named->declared;
            ret->declared.state = arena_new(&my arena, TyObjectState);
        }
        ret->declared.merged_value = decl;
        if (!ret->declared.name->len) ret->declared.name = ty_qualified_name(me, decl);
    } else {
        // A function body can mention every type parameter in scope
        ret = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT | TYF_COULD_CONTAIN_TYPE_VARIABLES);
        ret->anonymous.merged_value = decl;
        ret->anonymous.ctx = (LowerCtx){ .generic = true };
        ret->anonymous.free_params_unknown = true;
        ret->anonymous.state = arena_new(&my arena, TyObjectState);
    }
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

static Ty const* lower_tuple(Checker* me, TypeNode const* node, LowerCtx ctx) {
    Ty const* target = tuple_target_of_node(me, node);
    u32 arity = (u32)vec_tuple_element_size(&node->tuple.elements);
    if (!arity) return target;
    Ty const** args = malloc(sizeof(Ty const*) * arity);
    for_each (i, vec_tuple_element, &node->tuple.elements) {
        // TODO: tsc keeps the element type of a rest element (`T` for `...T[]`), this keeps what was written
        args[i_index] = ty_from_node(me, &i->type, ctx);
    }
    Ty const* ret = ty_reference(me, target, (TyList){ .data = args, .len = arity });
    free(args);
    return ret;
}

// Deferred references, see Ty.reference.deferred

// Whether lowering the node right now would run into a type alias that is being resolved. Object types aren't looked
// into, their members are lazy anyway
static bool node_reaches_alias_in_flight(Checker* me, TypeNode const* node, u32 depth) {
    if (depth > 32) return false;
    switch (node->tag) {
        case TYPE_IDENTIFIER: {
            Declaration const* decl = node->identifier->decl_ref;
            if (decl && decl->type_tag == DECLARATION_TYPE && !registered_type_param(me, decl)) {
                u64 key[2] = { KEY_DECL_LINKS, (u64)(uintptr_t)decl };
                DeclLinks const* links = intern_get(&my links, key, 2);
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
        case TYPE_REF: return node_reaches_alias_in_flight(me, node->ref, depth + 1);
        default: return false;
    }
}

static bool node_reaches_alias_in_flight_deep(Checker* me, TypeNode const* node) {
    if (node_reaches_alias_in_flight(me, node, 0)) return true;
    node = node_skip_refs(node);
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
    Checker* me, TypeNode const* node, LowerCtx ctx, TyMapper const* mapper, Declaration const* alias, TyList alias_args
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
        TypeNode const* element = node->tag == TYPE_ARRAY ? node->array.element : &vec_tuple_element_at(&node->tuple.elements, i)->type;
        args[i] = ty_instantiate(me, ty_from_node(me, element, deferred->ctx), deferred->mapper);
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

static Ty const* without_undefined(Checker* me, Ty const* ty) {
    if (ty->flags & TYF_UNDEFINED) return ty_intrinsic(me, TY_NEVER);
    if (ty->kind != TY_UNION) return ty;
    TyList members = ty->composite.types;
    Ty const** kept = malloc(sizeof(Ty const*) * members.len);
    u32 len = 0;
    for (u32 i = 0; i < members.len; i++) {
        if (!(members.data[i]->flags & TYF_UNDEFINED)) kept[len++] = members.data[i];
    }
    Ty const* ret = len == members.len ? ty : ty_union(me, kept, len, nullptr, (TyList){0});
    free(kept);
    return ret;
}

// The type a destructuring binding takes out of its initializer: a property or element access
static Ty const* lower_binding_access(Checker* me, IndexedType const* indexed, LowerCtx ctx) {
    Ty const* object = ty_from_node(me, &indexed->interface, ctx);
    // TODO: iterables. No TS2339-style errors from here either
    if (indexed->array_only && !ty_extends_array(me, object)) return ty_intrinsic(me, TY_ANY);
    Ty const* ret;
    if (indexed->rest && indexed->array_only) {
        ret = binding_rest_elements(me, object, (u32)indexed->index.value.number);
    } else if (indexed->rest) {
        ret = object; // TODO: without the properties the pattern names, methods and accessors (tsc's getRestType())
    } else {
        ret = ty_access(me, object, ty_from_node(me, &indexed->index, ctx));
    }
    if (!ret) ret = ty_intrinsic(me, TY_ANY);
    if (indexed->default_value) {
        // `[x = 1]`: undefined is replaced by the default
        Ty const* types[2] = { without_undefined(me, ret), nullptr };
        // Not ty_check_expr() with types[0] as the contextual type: only the uncontextual path detects `[x = x]`
        types[1] = ty_widened_for_declaration(me, ty_expr_type(me, indexed->default_value), true);
        ret = ret->flags & TYF_ANY ? ret : ty_union(me, types, 2, nullptr, (TyList){0});
    }
    return ret;
}

static bool is_naked_type_param_node(Checker* me, TypeNode const* node) {
    node = node_skip_refs(node);
    if (node->tag != TYPE_IDENTIFIER || !node->identifier->decl_ref) return false;
    Declaration const* decl = node->identifier->decl_ref;
    return decl->type_tag == DECLARATION_GENERIC_TYPE || registered_type_param(me, decl);
}

static bool node_reaches_alias_in_flight_deep(Checker* me, TypeNode const* node);

static TyCondBranch lower_branch(Checker* me, TypeNode const* node, LowerCtx ctx) {
    if (node_reaches_alias_in_flight_deep(me, node)) {
        ctx.in_alias = false;
        return (TyCondBranch){ .node = node, .ctx = ctx };
    }
    return (TyCondBranch){ .type = ty_from_node(me, node, ctx) };
}

static Ty const* lower(Checker* me, TypeNode const* node, LowerCtx outer_ctx, Declaration const* alias, TyList alias_args) {
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
            Declaration const* decl = node->identifier->decl_ref;
            // The body of an alias is an instantiation of a generic alias: it goes by the new name
            if (alias && decl && decl->type_tag == DECLARATION_TYPE && decl->type_generic && !decl->is_typeof &&
                !registered_type_param(me, decl)
            ) {
                ret = ty_realias(me, ret, alias, alias_args);
            }
            return ret;
        }

        case TYPE_INFERRED: {
            if (!node->inferred->declaration_ref) return ty_intrinsic(me, TY_ERROR);
            Ty const* param = ty_type_param(me, node->inferred->declaration_ref);
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

        case TYPE_TUPLE:
            if ((alias || (ctx.in_alias && node_reaches_alias_in_flight(me, node, 0)))) {
                return ty_deferred_reference(me, node, ctx, nullptr, alias, alias_args);
            }
            return lower_tuple(me, node, ctx);

        case TYPE_INDEXED: {
            if (node->indexed->is_from_binding) return lower_binding_access(me, node->indexed, ctx);
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
            TypeNode const* extends_node = node_skip_refs(&node->ternary->maybe_super);
            if (extends_node->tag == TYPE_CALLABLE) {
                for_each (i, vec_callable_arg, &extends_node->callable->args) {
                    TypeNode const* arg_type = node_skip_refs(&i->type);
                    if (!i->source || !i->source->rest || arg_type->tag != TYPE_INFERRED) continue;
                    if (!arg_type->inferred->declaration_ref) continue;
                    Ty const* param = ty_type_param(me, arg_type->inferred->declaration_ref);
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
            bool distributive = is_naked_type_param_node(me, &node->ternary->maybe_sub);
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

        case TYPE_LAZY:
            return lazy_type_get((LazyType*)node->lazy);

        case TYPE_REF:
            return lower(me, node->ref, outer_ctx, alias, alias_args);
    }
    impossible;
}

Ty const* ty_from_node(Checker* me, TypeNode const* node, LowerCtx ctx) {
    return lower(me, node, ctx, nullptr, (TyList){0});
}
