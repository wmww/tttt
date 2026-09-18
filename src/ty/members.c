#include "ty/ty_internal.h"
#include "scope.h"

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

static Members const* builder_finish(Checker* me, MembersBuilder* builder) {
    Members* ret = arena_new(&my arena, Members);
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

static Signature* signature_from_node(Checker* me, CallableType const* node, LowerCtx ctx) {
    Signature* ret = arena_new(&my arena, Signature);
    ret->node = node;
    ret->ctx = ctx;
    if (node->is_constructor) ret->flags |= SIGNATURE_CONSTRUCT;
    if (node->is_abstract_constructor) ret->flags |= SIGNATURE_ABSTRACT;
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
                LowerCtx mapped_ctx = { .generic = true };
                MappedInfo* mapped = arena_new(&my arena, MappedInfo);
                mapped->type_param = i->key.mapped.declaration_ref ?
                    ty_type_param(me, i->key.mapped.declaration_ref) :
                    ty_synthetic_type_param(me, i->key.mapped.type_name.sv);
                mapped->constraint = ty_from_legacy(me, &i->key.mapped.in_type, mapped_ctx);
                if (i->key.mapped.has_as_type) {
                    mapped->name_type = ty_from_legacy(me, &i->key.mapped.as_type, mapped_ctx);
                }
                mapped->template = ty_from_legacy(me, &i->type, mapped_ctx);
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
                if (!interface_decl) continue;
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
        case TY_DECLARED_TUPLE_TARGET: // TODO(stage 3): numeric properties and `length`, then the array members
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
        builder.mapped = mapped;
    }
    return builder_finish(me, &builder);
}

static Members const* resolve_members(Checker* me, Ty const* ty) {
    switch (ty->kind) {
        case TY_DECLARED:
            return resolve_declared_members(me, ty);

        case TY_REFERENCE: {
            Ty const* target = ty->reference.target;
            TyMapper const* mapper = ty_mapper_new(me, target->declared.type_params, ty->reference.args);
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
    // PERF: linear. Fine for object literals, not for `Window`; sort or hash big member tables when relations land
    Members const* members = ty_members(me, ty);
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
        // TODO(stage 3): a generic signature keeps its type parameters when instantiated, tsc makes fresh ones
        sig->type_params = target->type_params;
        SigParam* params = arena_new_n(&my arena, SigParam, target->param_count);
        for (u32 i = 0; i < target->param_count; i++) {
            params[i] = target->params[i];
            params[i].type = ty_instantiate(me, params[i].type, sig->mapper);
        }
        sig->params = params;
        sig->param_count = target->param_count;
        sig->min_args = target->min_args;
        sig->ret = ty_instantiate(me, target->ret, sig->mapper);
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
            if (param->optional || param->rest) counting_required = false;
            if (counting_required) sig->min_args++;
        }
        sig->params = params;
        sig->param_count = count;
        sig->ret = ty_from_legacy(me, &node->ret, ctx);
    }
    sig->state = LAZY_STATE_DONE;
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
