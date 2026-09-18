#include "ty/ty_internal.h"
#include "scope.h"
#include "class_decl.h"

// Members of object types are resolved on first use, never at construction. That's what makes recursive types legal:
// `interface List<T> { tail: List<T> }` only needs the *identity* of `List<T>` to describe its members.

static const Members empty_members = {0};

typedef struct MembersBuilder {
    Prop** props;
    u32 props_count, props_cap;
    Signature** call;
    u32 call_count, call_cap;
    Signature** construct;
    u32 construct_count, construct_cap;
    IndexInfo* index;
    u32 index_count, index_cap;
    MappedInfo const* mapped;
} MembersBuilder;

#define builder_push(me, field, value) do { \
        if ((me)->field##_count == (me)->field##_cap) { \
            (me)->field##_cap = (me)->field##_cap ? (me)->field##_cap * 2 : 8; \
            (me)->field = realloc((me)->field, sizeof(*(me)->field) * (me)->field##_cap); \
            panic_if(!(me)->field, "out of memory"); \
        } \
        (me)->field[(me)->field##_count++] = (value); \
    } while (false)

static Prop* builder_find_prop(MembersBuilder const* me, Atom const* name) {
    for (u32 i = 0; i < my props_count; i++) {
        if (my props[i]->name == name) return my props[i];
    }
    return nullptr;
}

static int prop_cmp_name(void const* a, void const* b) {
    uintptr_t a_name = (uintptr_t)(*(Prop* const*)a)->name;
    uintptr_t b_name = (uintptr_t)(*(Prop* const*)b)->name;
    return a_name < b_name ? -1 : a_name > b_name;
}

static Members const* builder_finish(Checker* me, MembersBuilder* builder) {
    Members* ret = arena_new(&my arena, Members);
    if (builder->props_count > 8) {
        Prop** sorted = arena_new_n(&my arena, Prop*, builder->props_count);
        memcpy(sorted, builder->props, sizeof(Prop*) * builder->props_count);
        qsort(sorted, builder->props_count, sizeof(Prop*), prop_cmp_name);
        ret->props_by_name = sorted;
    }
#define FINISH(field, count_field, T) \
    if (builder->field##_count) { \
        T* copy = arena_new_n(&my arena, T, builder->field##_count); \
        memcpy(copy, builder->field, sizeof(T) * builder->field##_count); \
        ret->field = copy; \
        ret->count_field = builder->field##_count; \
    } \
    free(builder->field);
    FINISH(props, prop_count, Prop*)
    FINISH(call, call_count, Signature*)
    FINISH(construct, construct_count, Signature*)
    FINISH(index, index_count, IndexInfo)
#undef FINISH
    ret->mapped = builder->mapped;
    return ret;
}

// `(...args: infer A) => any`: an `infer` type parameter in rest position is implicitly constrained to `unknown[]`
static void imply_rest_constraint(Checker* me, SigParam const* param) {
    if (!param->rest || param->type->kind != TY_TYPE_PARAM) return;
    TyParamState* state = param->type->param.state;
    if (!state->is_infer || ty_param_constraint(me, param->type)) return;
    state->constraint = ty_array(me, ty_intrinsic(me, TY_UNKNOWN_), false);
    state->constraint_state = LAZY_STATE_DONE;
}

static Signature* signature_from_node(Checker* me, CallableType const* node, LowerCtx ctx) {
    Signature* ret = arena_new(&my arena, Signature);
    ret->node = node;
    ret->ctx = ctx;
    if (node->is_constructor) ret->flags |= SIGNATURE_CONSTRUCT;
    if (node->is_abstract_constructor) ret->flags |= SIGNATURE_ABSTRACT;
    if (ctx.method) ret->flags |= SIGNATURE_METHOD;
    return ret;
}

static void builder_add_callable(Checker* me, MembersBuilder* builder, Type const* type, LowerCtx ctx) {
    Type const* callable = legacy_skip_wrappers(type);
    if (callable->tag != TYPE_CALLABLE) return;
    Signature* sig = signature_from_node(me, callable->callable, ctx);
    if (sig->flags & SIGNATURE_CONSTRUCT) {
        builder_push(builder, construct, sig);
    } else {
        builder_push(builder, call, sig);
    }
}

// Adds the members written in (or, for classes/enums/namespaces, evaluated by the legacy checker into) an Interface
static void builder_add_interface(
    Checker* me, MembersBuilder* builder, Interface const* interface, LowerCtx ctx, Declaration const* parent
) {
    for_each (i, vec_interface_member, &interface->members) {
        switch (i->key.tag) {
            case INTERFACE_KEY_NAME:
            case INTERFACE_KEY_EXPR: {
                Atom const* name = nullptr;
                if (i->key.tag == INTERFACE_KEY_NAME) {
                    name = checker_atom(me, cstr_sv(&i->key.name_string));
                } else {
                    // TODO(stage 4): unique symbol keys (`[Symbol.iterator]`) are silently left out
                    Ty const* key = ty_from_legacy(me, legacy_expr_type(i->key.expr.ref), ctx);
                    if (key->flags & TYF_STRING_LITERAL) {
                        name = key->literal.string;
                    } else if (key->flags & TYF_NUMBER_LITERAL) {
                        cstr rendered = cstr_null;
                        cstr_append_value_js(&rendered, &key->literal.value);
                        name = checker_atom(me, cstr_sv(&rendered));
                        cstr_drop(&rendered);
                    } else {
                        break;
                    }
                }
                bool is_method = i->key.tag == INTERFACE_KEY_NAME && (i->is_method || i->key.name_is_member_function) &&
                    legacy_skip_wrappers(&i->type)->tag == TYPE_CALLABLE;
                Prop* existing = builder_find_prop(builder, name);
                if (existing) {
                    // Overloads are separate members in the legacy interface and one method with several signatures here
                    if (is_method && (existing->flags & PROP_METHOD) && existing->node) {
                        u32 count = existing->overload_count ? existing->overload_count : 1;
                        Type const** nodes = arena_new_n(&my arena, Type const*, count + 1);
                        if (existing->overload_count) {
                            memcpy(nodes, existing->overloads, sizeof(Type const*) * count);
                        } else {
                            nodes[0] = existing->node;
                        }
                        nodes[count] = &i->type;
                        existing->overloads = nodes;
                        existing->overload_count = count + 1;
                    }
                    break;
                }
                Prop* prop = arena_new(&my arena, Prop);
                prop->name = name;
                prop->parent = i->origin_interface ? i->origin_interface : parent;
                prop->node = &i->type;
                prop->ctx = ctx;
                prop->ctx.method = is_method;
                if (i->optional) prop->flags |= PROP_OPTIONAL;
                if (i->readonly) prop->flags |= PROP_READONLY;
                if (is_method) prop->flags |= PROP_METHOD;
                builder_push(builder, props, prop);
            }   break;

            case INTERFACE_KEY_INDEX: {
                // TODO: index signature value types are lowered eagerly, unlike property types
                IndexInfo info = {
                    .key = ty_from_legacy(me, &i->key.index.type, ctx),
                    .value = ty_from_legacy(me, &i->type, ctx),
                    .param_name = checker_atom(me, i->key.index.name.sv),
                    .readonly = i->readonly,
                };
                builder_push(builder, index, info);
            }   break;

            case INTERFACE_KEY_MAPPED: {
                LowerCtx mapped_ctx = ctx;
                mapped_ctx.generic = true;
                MappedInfo* mapped = arena_new(&my arena, MappedInfo);
                mapped->type_param = i->key.mapped.declaration_ref ?
                    ty_type_param(me, i->key.mapped.declaration_ref) :
                    ty_synthetic_type_param(me, i->key.mapped.type_name.sv);
                mapped->constraint = ty_from_legacy(me, &i->key.mapped.in_type, mapped_ctx);
                if (i->key.mapped.has_as_type) {
                    mapped->name_type = ty_from_legacy(me, &i->key.mapped.as_type, mapped_ctx);
                }
                mapped->template = ty_from_legacy(me, &i->type, mapped_ctx);
                Type const* in_type = legacy_skip_refs(&i->key.mapped.in_type);
                if (in_type->tag == TYPE_KEYOF) {
                    mapped->modifiers_type = ty_from_legacy(me, &in_type->keyof->interface, mapped_ctx);
                }
                mapped->readonly = i->key.mapped.readonly;
                mapped->optional = i->key.mapped.optional;
                builder->mapped = mapped;
            }   break;

            case INTERFACE_KEY_CALLABLE:
                builder_add_callable(me, builder, &i->type, ctx);
                break;
        }
    }
}

static void builder_add_inherited(Checker* me, MembersBuilder* builder, Ty const* base) {
    Members const* members = ty_members(me, base);
    for (u32 i = 0; i < members->prop_count; i++) {
        if (!builder_find_prop(builder, members->props[i]->name)) {
            builder_push(builder, props, members->props[i]);
        }
    }
    // Signatures and index signatures are inherited unless the derived type declares its own
    if (!builder->call_count) {
        for (u32 i = 0; i < members->call_count; i++) builder_push(builder, call, members->call[i]);
    }
    if (!builder->construct_count) {
        for (u32 i = 0; i < members->construct_count; i++) builder_push(builder, construct, members->construct[i]);
    }
    for (u32 i = 0; i < members->index_count; i++) {
        bool has_own = false;
        for (u32 j = 0; j < builder->index_count; j++) has_own |= builder->index[j].key == members->index[i].key;
        if (!has_own) builder_push(builder, index, members->index[i]);
    }
}

// The instance type of the class a class extends, null if there's none or it isn't simply a (generic) class name.
// HACK(stage 4a): the legacy class type has no inherited members at all, and there's no link from a Declaration to
// its ClassDecl other than through the LazyType of the instance type
static Ty const* class_base_type(Checker* me, Type const* entry, LowerCtx ctx) {
    entry = legacy_skip_refs(entry);
    if (entry->tag != TYPE_LAZY || entry->lazy->tag != LAZY_TYPE_CLASS_INSTANCE) return nullptr;
    ClassDecl const* class_decl = c_container_of(entry->lazy, ClassDecl, instance_type);
    Expr const* extends = &class_decl->extends;
    VecSpannedType const* written_args = nullptr;
    if (extends->tag == EXPR_GENERIC_PARAMS) {
        written_args = &extends->generic_params->params;
        extends = &extends->generic_params->expr;
    }
    if (extends->tag != EXPR_IDENTIFIER || !extends->identifier.decl_ref) return nullptr;
    Declaration const* base_decl = extends->identifier.decl_ref;
    if (base_decl->type_tag != DECLARATION_CLASS || base_decl->is_typeof) return nullptr;
    Ty const* base = ty_declared_type_of(me, base_decl);
    if (base->kind != TY_DECLARED) return nullptr;
    TyList params = base->declared.type_params;
    if (!params.len) return base;
    Ty const* args[32];
    if (params.len > c_arraylen(args)) return nullptr;
    for (u32 i = 0; i < params.len; i++) {
        if (written_args && (isize_t)i < vec_spanned_type_size(written_args)) {
            args[i] = ty_from_legacy(me, &vec_spanned_type_at(written_args, i)->type, ctx);
        } else {
            Ty const* fallback = ty_param_default(me, params.data[i]);
            args[i] = fallback ? fallback : ty_intrinsic(me, TY_ANY);
        }
    }
    return ty_reference(me, base, (TyList){ .data = args, .len = params.len });
}

static Members const* resolve_declared_members(Checker* me, Ty const* ty) {
    Declaration const* decl = ty->declared.decl;
    LowerCtx ctx = { .generic = ty->declared.type_params.len > 0 };
    MembersBuilder builder = {0};
    switch (ty->declared.kind) {
        case TY_DECLARED_INTERFACE:
        case TY_DECLARED_CLASS_INSTANCE: {
            // Own members of every merged declaration first, then what their bases add
            DeclEntries entries = legacy_declaration_entries(decl);
            for (u32 i = 0; i < entries.len; i++) {
                InterfaceDecl const* interface_decl = legacy_entry_interface_decl(&entries.data[i]);
                if (interface_decl) {
                    check(interface_decl->type.tag == TYPE_INTERFACE);
                    builder_add_interface(me, &builder, &interface_decl->type.interface, ctx, decl);
                } else {
                    // HACK(stage 4a): classes get their members from the structural type the legacy checker made
                    Type const* structure = type_resolve(&entries.data[i]);
                    if (structure->tag == TYPE_INTERFACE) {
                        builder_add_interface(me, &builder, &structure->interface, ctx, decl);
                    }
                }
            }
            for (u32 i = 0; i < entries.len; i++) {
                InterfaceDecl const* interface_decl = legacy_entry_interface_decl(&entries.data[i]);
                if (!interface_decl) {
                    Ty const* base = class_base_type(me, &entries.data[i], ctx);
                    if (base && base != ty) builder_add_inherited(me, &builder, base);
                    continue;
                }
                for_each (extends, vec_spanned_type, &interface_decl->extends) {
                    Ty const* base = ty_from_legacy(me, &extends->type, ctx);
                    if (base != ty && (base->flags & TYF_OBJECT)) {
                        builder_add_inherited(me, &builder, base);
                    }
                }
            }
        }   break;

        case TY_DECLARED_CLASS_STATIC:
        case TY_DECLARED_ENUM_OBJECT: {
            // HACK(stage 4a): as above
            Type const* structure = type_resolve(&decl->declared_type);
            if (structure->tag == TYPE_INTERFACE) {
                builder_add_interface(me, &builder, &structure->interface, ctx, decl);
            } else if (structure->tag == TYPE_CALLABLE) {
                builder_add_callable(me, &builder, structure, ctx);
            }
        }   break;

        case TY_DECLARED_NAMESPACE: {
            // HACK(stage 4a): as above
            Type const* structure = lazy_type_get((LazyType*)&ty->declared.namespace_scope->value_interface);
            structure = type_resolve(structure);
            if (structure->tag == TYPE_INTERFACE) {
                builder_add_interface(me, &builder, &structure->interface, ctx, nullptr);
            }
        }   break;

        case TY_DECLARED_ENUM:
        case TY_DECLARED_TUPLE_TARGET: // TODO: numeric properties and `length`, then the array members (relations see a tuple as its array for now)
        case TY_DECLARED_SYNTHETIC:
            break;
    }
    return builder_finish(me, &builder);
}

static Members const* resolve_instantiated_members(Checker* me, Ty const* target, TyMapper const* mapper) {
    Members const* source = ty_members(me, target);
    MembersBuilder builder = {0};
    for (u32 i = 0; i < source->prop_count; i++) {
        Prop* prop = arena_new(&my arena, Prop);
        *prop = (Prop){
            .name = source->props[i]->name,
            .flags = source->props[i]->flags,
            .parent = source->props[i]->parent,
            .target = source->props[i],
            .mapper = mapper,
        };
        builder_push(&builder, props, prop);
    }
    for (u32 pass = 0; pass < 2; pass++) {
        u32 count = pass ? source->construct_count : source->call_count;
        Signature* const* sigs = pass ? source->construct : source->call;
        for (u32 i = 0; i < count; i++) {
            Signature* sig = arena_new(&my arena, Signature);
            *sig = (Signature){ .flags = sigs[i]->flags, .target = sigs[i], .mapper = mapper };
            if (pass) {
                builder_push(&builder, construct, sig);
            } else {
                builder_push(&builder, call, sig);
            }
        }
    }
    for (u32 i = 0; i < source->index_count; i++) {
        IndexInfo info = source->index[i];
        info.key = ty_instantiate(me, info.key, mapper);
        info.value = ty_instantiate(me, info.value, mapper);
        builder_push(&builder, index, info);
    }
    if (source->mapped) {
        MappedInfo* mapped = arena_new(&my arena, MappedInfo);
        *mapped = *source->mapped;
        mapped->constraint = ty_instantiate(me, mapped->constraint, mapper);
        if (mapped->name_type) mapped->name_type = ty_instantiate(me, mapped->name_type, mapper);
        mapped->template = ty_instantiate(me, mapped->template, mapper);
        if (mapped->modifiers_type) mapped->modifiers_type = ty_instantiate(me, mapped->modifiers_type, mapper);
        builder.mapped = mapped;
    }
    return builder_finish(me, &builder);
}

// A mapped type whose key type is known has ordinary members: a property per literal key, an index signature per
// primitive key. Returns the members unchanged while the key type is generic (or something this can't expand yet)
static Members const* expand_mapped(Checker* me, Members const* members) {
    MappedInfo const* mapped = members->mapped;
    if (!mapped || mapped->name_type) return members; // TODO: `as` clauses
    Ty const* constraint = mapped->constraint;
    if (constraint->flags & (TYF_COULD_CONTAIN_TYPE_VARIABLES | TYF_ANY | TYF_DEFERRED)) return members;
    Ty const* const* keys = &constraint;
    u32 key_count = 1;
    if (constraint->kind == TY_UNION) {
        keys = constraint->composite.types.data;
        key_count = constraint->composite.types.len;
    } else if (constraint->flags & TYF_NEVER) {
        key_count = 0;
    }
    for (u32 i = 0; i < key_count; i++) {
        if (!(keys[i]->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_STRING | TYF_NUMBER | TYF_ES_SYMBOL))) {
            return members;
        }
    }
    Ty const* modifiers = mapped->modifiers_type;
    if (modifiers && !(modifiers->flags & TYF_OBJECT)) modifiers = nullptr;
    // The template as a property to instantiate per key
    Prop* template = arena_new(&my arena, Prop);
    template->type = mapped->template;
    template->type_state = LAZY_STATE_DONE;

    MembersBuilder builder = {0};
    for (u32 i = 0; i < key_count; i++) {
        Ty const* key = keys[i];
        TyMapper const* mapper = ty_mapper_new(
            me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &key, .len = 1 }
        );
        if (key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) {
            Atom const* name = key->literal.string;
            if (!name) {
                cstr rendered = cstr_null;
                cstr_append_value_js(&rendered, &key->literal.value);
                name = checker_atom(me, cstr_sv(&rendered));
                cstr_drop(&rendered);
            }
            if (builder_find_prop(&builder, name)) continue;
            Prop const* modifiers_prop = modifiers ? ty_get_prop(me, modifiers, name) : nullptr;
            u32 flags = modifiers_prop ? modifiers_prop->flags & (PROP_OPTIONAL | PROP_READONLY) : 0;
            if (mapped->optional > 0) flags |= PROP_OPTIONAL;
            if (mapped->optional < 0) flags &= ~(u32)PROP_OPTIONAL;
            if (mapped->readonly > 0) flags |= PROP_READONLY;
            if (mapped->readonly < 0) flags &= ~(u32)PROP_READONLY;
            Prop* prop = arena_new(&my arena, Prop);
            *prop = (Prop){
                .name = name,
                .flags = flags,
                .parent = modifiers_prop ? modifiers_prop->parent : nullptr,
                .target = template,
                .mapper = mapper,
            };
            builder_push(&builder, props, prop);
        } else {
            IndexInfo info = {
                .key = key,
                .value = ty_instantiate(me, mapped->template, mapper),
                .param_name = checker_atom(me, c_sv("x")),
                .readonly = mapped->readonly > 0,
            };
            builder_push(&builder, index, info);
        }
    }
    return builder_finish(me, &builder);
}

static Members const* resolve_members_inner(Checker* me, Ty const* ty);

static Members const* resolve_members(Checker* me, Ty const* ty) {
    return expand_mapped(me, resolve_members_inner(me, ty));
}

static Members const* resolve_members_inner(Checker* me, Ty const* ty) {
    switch (ty->kind) {
        case TY_DECLARED:
            return resolve_declared_members(me, ty);

        case TY_REFERENCE: {
            Ty const* target = ty->reference.target;
            TyMapper const* mapper = ty_mapper_new(me, target->declared.type_params, ty_reference_args(me, ty));
            return resolve_instantiated_members(me, target, mapper);
        }

        case TY_ANONYMOUS: {
            if (ty->anonymous.target) {
                return resolve_instantiated_members(me, ty->anonymous.target, ty->anonymous.mapper);
            }
            MembersBuilder builder = {0};
            if (ty->anonymous.overload_count) {
                for (u32 i = 0; i < ty->anonymous.overload_count; i++) {
                    builder_add_callable(me, &builder, ty->anonymous.overloads[i], ty->anonymous.ctx);
                }
            } else if (ty->anonymous.node->tag == TYPE_CALLABLE) {
                builder_add_callable(me, &builder, ty->anonymous.node, ty->anonymous.ctx);
            } else {
                check(ty->anonymous.node->tag == TYPE_INTERFACE);
                builder_add_interface(me, &builder, &ty->anonymous.node->interface, ty->anonymous.ctx, nullptr);
            }
            return builder_finish(me, &builder);
        }

        default: impossible;
    }
}

static TyObjectState* object_state(Ty const* ty) {
    switch (ty->kind) {
        case TY_DECLARED: return ty->declared.state;
        case TY_REFERENCE: return ty->reference.state;
        case TY_ANONYMOUS: return ty->anonymous.state;
        default: return nullptr;
    }
}

Members const* ty_members(Checker* me, Ty const* ty) {
    TyObjectState* state = object_state(ty);
    if (!state) return &empty_members;
    switch (state->members_state) {
        case LAZY_STATE_DONE:
            return state->members;

        case LAZY_STATE_IN_FLIGHT:
            // Only a type whose own members depend on its own members gets here (`interface A extends A`)
            return &empty_members;

        case LAZY_STATE_NOT_STARTED:
            state->members_state = LAZY_STATE_IN_FLIGHT;
            state->members = resolve_members(me, ty);
            state->members_state = LAZY_STATE_DONE;
            return state->members;
    }
    impossible;
}

Prop const* ty_get_prop(Checker* me, Ty const* ty, Atom const* name) {
    Members const* members = ty_members(me, ty);
    if (members->props_by_name) {
        u32 lo = 0, hi = members->prop_count;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2;
            Atom const* mid_name = members->props_by_name[mid]->name;
            if (mid_name == name) return members->props_by_name[mid];
            if ((uintptr_t)mid_name < (uintptr_t)name) lo = mid + 1; else hi = mid;
        }
        return nullptr;
    }
    for (u32 i = 0; i < members->prop_count; i++) {
        if (members->props[i]->name == name) return members->props[i];
    }
    return nullptr;
}

Ty const* prop_type(Checker* me, Prop const* const_prop) {
    Prop* prop = (Prop*)const_prop; // The lazily resolved type is the one mutable thing about a Prop
    switch (prop->type_state) {
        case LAZY_STATE_DONE: return prop->type;
        case LAZY_STATE_IN_FLIGHT: return ty_intrinsic(me, TY_ERROR); // TODO(stage 4): TS2502 and friends
        case LAZY_STATE_NOT_STARTED: break;
    }
    prop->type_state = LAZY_STATE_IN_FLIGHT;
    Ty const* type;
    if (prop->target) {
        type = ty_instantiate(me, prop_type(me, prop->target), prop->mapper);
    } else if (prop->overload_count) {
        type = ty_anonymous_from_overloads(me, prop->overloads, prop->overload_count, prop->ctx);
    } else {
        type = ty_from_legacy(me, prop->node, prop->ctx);
    }
    prop->type = type;
    prop->type_state = LAZY_STATE_DONE;
    return type;
}

void signature_resolve(Checker* me, Signature const* const_sig) {
    Signature* sig = (Signature*)const_sig;
    if (sig->state != LAZY_STATE_NOT_STARTED) return;
    sig->state = LAZY_STATE_IN_FLIGHT;
    if (sig->target) {
        Signature const* target = sig->target;
        signature_resolve(me, target);
        // TODO: a generic signature keeps its type parameters when instantiated, tsc makes fresh ones
        sig->type_params = target->type_params;
        SigParam* params = arena_new_n(&my arena, SigParam, target->param_count);
        for (u32 i = 0; i < target->param_count; i++) {
            params[i] = target->params[i];
            params[i].type = ty_instantiate(me, params[i].type, sig->mapper);
        }
        sig->params = params;
        sig->param_count = target->param_count;
        sig->min_args = target->min_args;
    } else {
        CallableType const* node = sig->node;
        sig->type_params = ty_type_params_of(me, &node->generic);
        LowerCtx ctx = sig->ctx;
        ctx.generic |= sig->type_params.len > 0;
        u32 count = (u32)vec_callable_arg_size(&node->args);
        SigParam* params = arena_new_n(&my arena, SigParam, count);
        bool counting_required = true;
        for_each (i, vec_callable_arg, &node->args) {
            SigParam* param = &params[i_index];
            if (i->source) {
                param->name = checker_atom(me, i->source->initializer.binding.content_span.sv);
                param->rest = i->source->rest;
            } else {
                param->name = checker_atom(me, c_sv("arg"));
            }
            param->optional = i->optional;
            param->type = ty_from_legacy(me, &i->type, ctx);
            imply_rest_constraint(me, param);
            if (param->optional || param->rest) counting_required = false;
            if (counting_required) sig->min_args++;
        }
        sig->params = params;
        sig->param_count = count;
        sig->ret_node = &node->ret;
    }
    sig->state = LAZY_STATE_DONE;
}

Ty const* signature_return_type(Checker* me, Signature const* const_sig) {
    Signature* sig = (Signature*)const_sig;
    signature_resolve(me, sig);
    switch (sig->ret_state) {
        case LAZY_STATE_DONE: return sig->ret;
        case LAZY_STATE_IN_FLIGHT: return ty_intrinsic(me, TY_ERROR);
        case LAZY_STATE_NOT_STARTED: break;
    }
    sig->ret_state = LAZY_STATE_IN_FLIGHT;
    Ty const* ret;
    if (sig->target) {
        ret = ty_instantiate(me, signature_return_type(me, sig->target), sig->mapper);
    } else if (sig->ret_node) {
        LowerCtx ctx = sig->ctx;
        ctx.generic |= sig->type_params.len > 0;
        ctx.method = false;
        ret = ty_from_legacy(me, sig->ret_node, ctx);
    } else {
        ret = ty_intrinsic(me, TY_ANY);
    }
    sig->ret = ret;
    sig->ret_state = LAZY_STATE_DONE;
    return ret;
}

// Temporary legacy object types (LowerCtx.transient). Nothing may point into the legacy tree afterwards, so whatever
// isn't stable (see legacy_stable_view()) is lowered right away, and the type is interned by its shape: the same
// temporary lowered again is the same Ty. HACK(stage 4): goes away when expressions evaluate to Ty themselves

typedef struct WordBuf {
    u64* data;
    u32 len, cap;
} WordBuf;

static void words_push(WordBuf* me, u64 word) {
    if (my len == my cap) {
        my cap = my cap ? my cap * 2 : 32;
        my data = realloc(my data, sizeof(u64) * my cap);
        panic_if(!my data, "out of memory");
    }
    my data[my len++] = word;
}

// A type inside a temporary: either something stable to lower later, or lowered now
typedef struct DetachedType {
    Type const* stable;
    Ty const* type;
} DetachedType;

static DetachedType detach_type(Checker* me, Type const* type, LowerCtx ctx, WordBuf* words) {
    u64 identity;
    DetachedType ret = { .stable = legacy_stable_view(me, type, &identity) };
    if (ret.stable) {
        words_push(words, 1);
        words_push(words, identity);
    } else {
        ret.type = ty_from_legacy(me, type, ctx);
        words_push(words, 0);
        words_push(words, ret.type->id);
    }
    return ret;
}

static Signature* structural_signature(Checker* me, CallableType const* node, LowerCtx ctx, WordBuf* words) {
    Signature* sig = arena_new(&my arena, Signature);
    sig->ctx = ctx;
    sig->ctx.transient = false;
    if (node->is_constructor) sig->flags |= SIGNATURE_CONSTRUCT;
    if (node->is_abstract_constructor) sig->flags |= SIGNATURE_ABSTRACT;
    if (ctx.method) sig->flags |= SIGNATURE_METHOD;
    ctx.method = false;
    u32 type_param_count = (u32)vec_generic_item_size(&node->generic.items);
    if (type_param_count) {
        Ty const** type_params = arena_new_n(&my arena, Ty const*, type_param_count);
        for_each (i, vec_generic_item, &node->generic.items) {
            type_params[i_index] = i->declaration_ref ?
                ty_type_param(me, i->declaration_ref) :
                ty_synthetic_type_param(me, i->name.sv);
        }
        sig->type_params = (TyList){ .data = type_params, .len = type_param_count };
        ctx.generic = true;
    }
    u32 count = (u32)vec_callable_arg_size(&node->args);
    SigParam* params = arena_new_n(&my arena, SigParam, count ? count : 1);
    bool counting_required = true;
    words_push(words, sig->flags);
    words_push(words, type_param_count);
    for (u32 i = 0; i < type_param_count; i++) words_push(words, sig->type_params.data[i]->id);
    words_push(words, count);
    for_each (i, vec_callable_arg, &node->args) {
        SigParam* param = &params[i_index];
        if (i->source) {
            param->name = checker_atom(me, i->source->initializer.binding.content_span.sv);
            param->rest = i->source->rest;
        } else {
            param->name = checker_atom(me, c_sv("arg"));
        }
        param->optional = i->optional;
        param->type = ty_from_legacy(me, &i->type, ctx);
        imply_rest_constraint(me, param);
        if (param->optional || param->rest) counting_required = false;
        if (counting_required) sig->min_args++;
        words_push(words, (u64)(uintptr_t)param->name);
        words_push(words, (u64)param->optional | (u64)param->rest << 1);
        words_push(words, param->type->id);
    }
    sig->params = params;
    sig->param_count = count;
    DetachedType ret = detach_type(me, &node->ret, ctx, words);
    sig->ret_node = ret.stable;
    if (ret.type) {
        sig->ret = ret.type;
        sig->ret_state = LAZY_STATE_DONE;
    }
    sig->state = LAZY_STATE_DONE;
    return sig;
}

static void structural_add_callable(Checker* me, MembersBuilder* builder, Type const* type, LowerCtx ctx, WordBuf* words) {
    Type const* callable = legacy_skip_wrappers(type);
    if (callable->tag != TYPE_CALLABLE) return;
    words_push(words, 2);
    Signature* sig = structural_signature(me, callable->callable, ctx, words);
    if (sig->flags & SIGNATURE_CONSTRUCT) {
        builder_push(builder, construct, sig);
    } else {
        builder_push(builder, call, sig);
    }
}

Ty const* ty_structural_from_legacy(Checker* me, Type const* node, LowerCtx ctx) {
    check(node->tag == TYPE_INTERFACE || node->tag == TYPE_CALLABLE);
    // PERF: the members are built in the arena before it's known whether the shape exists already
    MembersBuilder builder = {0};
    WordBuf words = {0};
    words_push(&words, KEY_STRUCTURAL);
    u32 flags = TYF_OBJECT;
    if (node->tag == TYPE_CALLABLE) {
        structural_add_callable(me, &builder, node, ctx, &words);
    } else {
        ctx.method = false;
        for_each (i, vec_interface_member, &node->interface.members) {
            switch (i->key.tag) {
                case INTERFACE_KEY_NAME:
                case INTERFACE_KEY_EXPR: {
                    Atom const* name = nullptr;
                    if (i->key.tag == INTERFACE_KEY_NAME) {
                        name = checker_atom(me, cstr_sv(&i->key.name_string));
                    } else {
                        Ty const* key = ty_from_legacy(me, legacy_expr_type(i->key.expr.ref), ctx);
                        if (key->flags & TYF_STRING_LITERAL) {
                            name = key->literal.string;
                        } else if (key->flags & TYF_NUMBER_LITERAL) {
                            cstr rendered = cstr_null;
                            cstr_append_value_js(&rendered, &key->literal.value);
                            name = checker_atom(me, cstr_sv(&rendered));
                            cstr_drop(&rendered);
                        } else {
                            break;
                        }
                    }
                    // TODO: overloads of a method in a temporary type, only the first signature is kept
                    if (builder_find_prop(&builder, name)) break;
                    bool is_method = i->key.tag == INTERFACE_KEY_NAME && (i->is_method || i->key.name_is_member_function) &&
                        legacy_skip_wrappers(&i->type)->tag == TYPE_CALLABLE;
                    Prop* prop = arena_new(&my arena, Prop);
                    prop->name = name;
                    prop->parent = i->origin_interface;
                    if (i->optional) prop->flags |= PROP_OPTIONAL;
                    if (i->readonly) prop->flags |= PROP_READONLY;
                    if (is_method) prop->flags |= PROP_METHOD;
                    prop->ctx = ctx;
                    prop->ctx.method = is_method;
                    words_push(&words, 1);
                    words_push(&words, (u64)(uintptr_t)name);
                    words_push(&words, prop->flags);
                    DetachedType type = detach_type(me, &i->type, prop->ctx, &words);
                    if (type.stable) {
                        prop->node = type.stable;
                        prop->ctx.transient = false;
                    } else {
                        prop->type = type.type;
                        prop->type_state = LAZY_STATE_DONE;
                        flags |= type.type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES;
                    }
                    builder_push(&builder, props, prop);
                }   break;

                case INTERFACE_KEY_INDEX: {
                    IndexInfo info = {
                        .key = ty_from_legacy(me, &i->key.index.type, ctx),
                        .value = ty_from_legacy(me, &i->type, ctx),
                        .param_name = checker_atom(me, i->key.index.name.sv),
                        .readonly = i->readonly,
                    };
                    flags |= (info.key->flags | info.value->flags) & TYF_COULD_CONTAIN_TYPE_VARIABLES;
                    words_push(&words, 3);
                    words_push(&words, info.key->id);
                    words_push(&words, info.value->id);
                    words_push(&words, (u64)(uintptr_t)info.param_name << 1 | info.readonly);
                    builder_push(&builder, index, info);
                }   break;

                case INTERFACE_KEY_MAPPED: {
                    LowerCtx mapped_ctx = ctx;
                    mapped_ctx.generic = true;
                    MappedInfo* mapped = arena_new(&my arena, MappedInfo);
                    mapped->type_param = i->key.mapped.declaration_ref ?
                        ty_type_param(me, i->key.mapped.declaration_ref) :
                        ty_synthetic_type_param(me, i->key.mapped.type_name.sv);
                    mapped->constraint = ty_from_legacy(me, &i->key.mapped.in_type, mapped_ctx);
                    if (i->key.mapped.has_as_type) {
                        mapped->name_type = ty_from_legacy(me, &i->key.mapped.as_type, mapped_ctx);
                    }
                    mapped->template = ty_from_legacy(me, &i->type, mapped_ctx);
                    Type const* in_type = legacy_skip_refs(&i->key.mapped.in_type);
                    if (in_type->tag == TYPE_KEYOF) {
                        mapped->modifiers_type = ty_from_legacy(me, &in_type->keyof->interface, mapped_ctx);
                    }
                    mapped->readonly = i->key.mapped.readonly;
                    mapped->optional = i->key.mapped.optional;
                    words_push(&words, 4);
                    words_push(&words, mapped->type_param->id);
                    words_push(&words, mapped->constraint->id);
                    words_push(&words, mapped->name_type ? mapped->name_type->id : 0);
                    words_push(&words, mapped->template->id);
                    words_push(&words, mapped->modifiers_type ? mapped->modifiers_type->id : 0);
                    words_push(&words, (u64)(unsigned char)mapped->readonly << 8 | (unsigned char)mapped->optional);
                    flags |= TYF_COULD_CONTAIN_TYPE_VARIABLES;
                    builder.mapped = mapped;
                }   break;

                case INTERFACE_KEY_CALLABLE:
                    structural_add_callable(me, &builder, &i->type, ctx, &words);
                    break;
            }
        }
    }
    // What isn't lowered yet, and everything inside signatures, could mention type parameters
    if (ctx.generic) flags |= TYF_COULD_CONTAIN_TYPE_VARIABLES;

    Ty const* ret = intern_get(&my links, words.data, words.len);
    if (ret) {
        free(builder.props);
        free(builder.call);
        free(builder.construct);
        free(builder.index);
    } else {
        Ty* fresh = ty_alloc(me, TY_ANONYMOUS, flags);
        fresh->anonymous.ctx = ctx;
        fresh->anonymous.ctx.transient = false;
        fresh->anonymous.free_params_unknown = true;
        fresh->anonymous.state = arena_new(&my arena, TyObjectState);
        fresh->anonymous.state->members = expand_mapped(me, builder_finish(me, &builder));
        fresh->anonymous.state->members_state = LAZY_STATE_DONE;
        *intern_slot(me, &my links, words.data, words.len) = fresh;
        ret = fresh;
    }
    free(words.data);
    return ret;
}

Ty const* ty_param_constraint(Checker* me, Ty const* param) {
    check(param->kind == TY_TYPE_PARAM);
    TyParamState* state = param->param.state;
    if (state->constraint_state == LAZY_STATE_NOT_STARTED) {
        state->constraint_state = LAZY_STATE_IN_FLIGHT;
        Type const* node = state->constraint_node;
        if (state->item) {
            node = state->item->extends_explicit ? &state->item->extends : nullptr;
        } else if (node && legacy_skip_wrappers(node)->tag == TYPE_ANY) {
            node = nullptr; // An absent constraint is stored as `any`
        }
        state->constraint = node ? ty_from_legacy(me, node, (LowerCtx){ .generic = true }) : nullptr;
        state->constraint_state = LAZY_STATE_DONE;
    }
    // In flight means the constraint is circular (`T extends T`), tsc reports TS2313 and carries on without one
    return state->constraint_state == LAZY_STATE_DONE ? state->constraint : nullptr;
}

Ty const* ty_param_default(Checker* me, Ty const* param) {
    check(param->kind == TY_TYPE_PARAM);
    TyParamState* state = param->param.state;
    if (state->fallback_state == LAZY_STATE_NOT_STARTED) {
        state->fallback_state = LAZY_STATE_IN_FLIGHT;
        if (state->item && state->item->fallback_explicit) {
            state->fallback = ty_from_legacy(me, &state->item->fallback, (LowerCtx){ .generic = true });
        }
        state->fallback_state = LAZY_STATE_DONE;
    }
    return state->fallback_state == LAZY_STATE_DONE ? state->fallback : nullptr;
}
