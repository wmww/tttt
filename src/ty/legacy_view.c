#include "ty/ty_internal.h"
#include "generic.h"

// Legacy code carrying and consuming `Ty` (plan 02 stage 4 scaffolding, all of it dies in stage 5).
//
// Declarations evaluate to `Ty` while expressions are still evaluated by the legacy checker, which wants to switch on
// the tag of a resolved `Type`. So a `Ty` travels through legacy code as a TYPE_NEW, and type_resolve() turns it into
// a *view*: one structural layer as a legacy type (an interface with its members, a union with its constituents...)
// whose children are TYPE_NEW again. Views are made once per Ty, owned by the checker, and never cloned deeply:
// cloning a TYPE_NEW copies a pointer.

Ty const* type_new_ty(Checker* me, Type const* type) {
    check(type->tag == TYPE_NEW);
    if (type->new_.ty) return type->new_.ty;
    if (type->new_.prop) return prop_type(me, type->new_.prop);
    if (type->new_.ret_of) return signature_return_type(me, type->new_.ret_of);
    return ty_intrinsic(me, TY_ERROR);
}

static CallableType const* signature_root_node(Signature const* sig) {
    while (sig->target) sig = sig->target;
    return sig->node;
}

static Type view_signature(Checker* me, Signature const* sig) {
    signature_resolve(me, sig);
    CallableType const* node = signature_root_node(sig);
    CallableType* callable = c_new(CallableType, {
        .is_constructor = (sig->flags & SIGNATURE_CONSTRUCT) != 0,
        .is_abstract_constructor = (sig->flags & SIGNATURE_ABSTRACT) != 0,
        .ret = type_new_return(sig),
    });
    // Legacy inference finds type parameters by their declarations
    for (u32 i = 0; i < sig->type_params.len; i++) {
        Ty const* param = sig->type_params.data[i];
        GenericItem const* item = param->param.state->item;
        if (item) {
            GenericItem copy = generic_item_clone(*item);
            vec_generic_item_push(&callable->generic.items, copy);
        }
    }
    for (u32 i = 0; i < sig->param_count; i++) {
        CallableArg arg = {
            .optional = sig->params[i].optional,
            .type = type_new(sig->params[i].type),
        };
        if (node && (isize_t)i < vec_callable_arg_size(&node->args)) {
            arg.source = vec_callable_arg_at(&node->args, i)->source;
        }
        if (!arg.source) {
            // Legacy code takes the parameter's name and restness from the FunctionArg it was declared by. A signature
            // that was never syntax (lowered from a legacy temporary) gets a nameless stand-in
            FunctionArg* stand_in = arena_new(&my arena, FunctionArg);
            stand_in->rest = sig->params[i].rest;
            arg.source = stand_in;
        }
        vec_callable_arg_push(&callable->args, arg);
    }
    return type_callable(callable);
}

static Type view_object(Checker* me, Ty const* ty) {
    Members const* members = ty_members(me, ty);
    if (!members->prop_count && !members->index_count && !members->mapped &&
        members->call_count + members->construct_count == 1 && ty->kind == TY_ANONYMOUS
    ) {
        return view_signature(me, members->call_count ? members->call[0] : members->construct[0]);
    }
    Interface interface = {0};
    for (u32 pass = 0; pass < 2; pass++) {
        u32 count = pass ? members->construct_count : members->call_count;
        Signature* const* sigs = pass ? members->construct : members->call;
        for (u32 i = 0; i < count; i++) {
            InterfaceMember member = {
                .origin_interface = ty->kind == TY_DECLARED ? ty->declared.decl : nullptr,
                .key.tag = INTERFACE_KEY_CALLABLE,
                .type = view_signature(me, sigs[i]),
            };
            Signature const* root = sigs[i];
            while (root->target) root = root->target;
            if (span_valid(root->decl_span)) member.declarations = vec_span_single(root->decl_span);
            member.key.span = root->name_span;
            vec_interface_member_push(&interface.members, member);
        }
    }
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members->props[i];
        Prop const* root = prop;
        while (root->target && !root->decl_span_count) root = root->target;
        InterfaceMember member = {
            .origin_interface = prop->parent,
            .readonly = (prop->flags & PROP_READONLY) != 0,
            .optional = (prop->flags & PROP_OPTIONAL) != 0,
            .is_method = (prop->flags & PROP_METHOD) != 0,
            .key = {
                .span = root->name_span,
                .tag = INTERFACE_KEY_NAME,
                .name_string = cstr_from_sv(atom_sv(prop->name)),
                .name_is_value = root->name_is_value,
            },
            .type = type_new_prop(prop),
        };
        for (u32 j = 0; j < root->decl_span_count; j++) vec_span_push(&member.declarations, root->decl_spans[j]);
        vec_interface_member_push(&interface.members, member);
    }
    for (u32 i = 0; i < members->index_count; i++) {
        IndexInfo const* info = &members->index[i];
        InterfaceMember member = {
            .readonly = info->readonly,
            .key = {
                .tag = INTERFACE_KEY_INDEX,
                .index = { .type = type_new(info->key) },
            },
            .type = type_new(info->value),
        };
        vec_interface_member_push(&interface.members, member);
    }
    return type_interface(interface);
}

static Type view_build(Checker* me, Ty const* ty) {
    switch ((TyKind)ty->kind) {
        case TY_INTRINSIC:
            if (ty->flags & (TYF_ANY | TYF_ERROR)) return type_any;
            if (ty->flags & TYF_UNKNOWN) return type_unknown;
            if (ty->flags & TYF_NEVER) return type_never;
            if (ty->flags & TYF_VOID) return type_void;
            if (ty->flags & TYF_UNDEFINED) return type_undefined;
            if (ty->flags & TYF_NULL) return type_null;
            if (ty->flags & TYF_STRING) return type_string;
            if (ty->flags & TYF_NUMBER) return type_number;
            if (ty->flags & TYF_BIGINT) return type_bigint;
            if (ty->flags & TYF_ES_SYMBOL) return type_symbol;
            if (ty->flags & TYF_NON_PRIMITIVE) return type_object;
            if (ty->flags & TYF_BOOLEAN_LITERAL) return type_value((Value){ .tag = VALUE_BOOLEAN, .boolean = ty == ty_intrinsic(me, TY_TRUE) });
            return type_any;

        case TY_LITERAL: {
            if (ty->flags & TYF_ENUM) return type_number;
            if (ty->flags & TYF_STRING_LITERAL) {
                return type_value((Value){ .tag = VALUE_STRING, .string = cstr_from_sv(atom_sv(ty->literal.string)) });
            }
            return type_value(value_clone(ty->literal.value));
        }

        case TY_TYPE_PARAM: {
            // What legacy code knows a type parameter as: an identifier for its declaration
            if (!ty->param.decl) return type_unknown;
            return type_identifier(c_new(IdentifierType, {
                .name = ty->param.decl->name,
                .decl_ref = ty->param.decl,
            }));
        }

        case TY_UNION:
        case TY_INTERSECTION: {
            if (ty->flags & TYF_BOOLEAN) return type_boolean;
            VecType types = vec_type_init();
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                vec_type_push(&types, type_new(ty->composite.types.data[i]));
            }
            return (Type){ .tag = ty->kind == TY_UNION ? TYPE_UNION : TYPE_INTERSECTION, .composite = types };
        }

        case TY_REFERENCE: {
            Ty const* target = ty->reference.target;
            TyList args = ty_reference_args(me, ty);
            if (target->declared.kind == TY_DECLARED_TUPLE_TARGET) {
                TupleShape const* shape = target->declared.tuple;
                VecTupleElement elements = vec_tuple_element_init();
                for (u32 i = 0; i < args.len; i++) {
                    vec_tuple_element_push(&elements, (TupleElement){
                        .type = type_new(args.data[i]),
                        .rest = (shape->flags[i] & TUPLE_ELEMENT_REST) != 0,
                        .optional = (shape->flags[i] & TUPLE_ELEMENT_OPTIONAL) != 0,
                    });
                }
                return type_tuple(elements, shape->readonly);
            }
            if ((target == my array_target || target == my readonly_array_target) && args.len == 1) {
                return type_array(c_new(Type, type_new(args.data[0])), target == my readonly_array_target);
            }
            return view_object(me, ty);
        }

        case TY_DECLARED:
            if (ty->declared.kind == TY_DECLARED_TUPLE_TARGET) {
                return type_tuple(vec_tuple_element_init(), ty->declared.tuple && ty->declared.tuple->readonly);
            }
            return view_object(me, ty);

        case TY_ANONYMOUS:
            return view_object(me, ty);

        case TY_KEYOF:
        case TY_INDEXED:
        case TY_CONDITIONAL: {
            // Deferred: about a type parameter. Legacy code sees what it's known to be at least, like it sees a type
            // parameter as its constraint
            Ty const* constraint = ty_base_constraint(me, ty);
            return constraint ? type_new(constraint) : type_unknown;
        }
    }
    impossible;
}

Type const* ty_legacy_view(Checker* me, Ty const* ty) {
    if (ty->id < my views_len && my views[ty->id]) return my views[ty->id];
    Type built = view_build(me, ty);
    // Building can evaluate things (computed keys), which can ask for the same view
    if (ty->id < my views_len && my views[ty->id]) {
        type_drop(&built);
        return my views[ty->id];
    }
    if (ty->id >= my views_len) {
        u32 len = my views_len ? my views_len : 256;
        while (len <= ty->id) len *= 2;
        my views = realloc(my views, sizeof(Type*) * len);
        panic_if(!my views, "out of memory");
        memset(my views + my views_len, 0, sizeof(Type*) * (len - my views_len));
        my views_len = len;
    }
    Type* view = c_new(Type, built);
    my views[ty->id] = view;
    return view;
}

// Legacy resolution hands out pointers, so a type operator that the new core evaluated needs a TYPE_NEW that stays
Type const* ty_legacy_wrapper(Checker* me, Ty const* ty) {
    u64 key[2] = { KEY_NEW_WRAPPER, ty->id };
    Type const* wrapper = intern_get(&my links, key, 2);
    if (!wrapper) {
        Type* fresh = arena_new(&my arena, Type);
        *fresh = type_new(ty);
        *intern_slot(me, &my links, key, 2) = fresh;
        wrapper = fresh;
    }
    return wrapper;
}

void ty_legacy_views_drop(Checker* me) {
    for (u32 i = 0; i < my views_len; i++) {
        if (my views[i]) {
            type_drop(my views[i]);
            free(my views[i]);
        }
    }
    free(my views);
    my views = nullptr;
    my views_len = 0;
}

// `Bar<string>` as a value: the object type with the type arguments applied to its generic signatures. Returns the
// type itself if the mapper isn't about any of them
Ty const* ty_apply_type_arguments_mapper(Checker* me, Ty const* ty, TyMapper const* mapper) {
    Members const* members = ty_members(me, ty);
    bool relevant = false;
    for (u32 pass = 0; pass < 2 && !relevant; pass++) {
        u32 sig_count = pass ? members->construct_count : members->call_count;
        Signature* const* sigs = pass ? members->construct : members->call;
        for (u32 i = 0; i < sig_count && !relevant; i++) {
            signature_resolve(me, sigs[i]);
            for (u32 j = 0; j < sigs[i]->type_params.len; j++) {
                for (u32 k = 0; k < mapper->len; k++) relevant |= mapper->sources[k] == sigs[i]->type_params.data[j];
            }
        }
    }
    if (!relevant) return ty;
    u32 count = 3 + mapper->len * 2;
    u64* key = malloc(sizeof(u64) * count);
    key[0] = KEY_ANONYMOUS_INSTANCE;
    key[1] = ty->id;
    key[2] = UINT64_MAX; // Not a type id: tells these apart from ordinary instantiations
    u32 flags = TYF_OBJECT | (ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES);
    for (u32 i = 0; i < mapper->len; i++) {
        key[3 + i * 2] = mapper->sources[i]->id;
        key[4 + i * 2] = mapper->targets[i]->id;
        flags |= mapper->targets[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES;
    }
    Ty const* ret = intern_get(&my types, key, count);
    if (!ret) {
        Ty* fresh = ty_alloc(me, TY_ANONYMOUS, flags);
        fresh->anonymous.target = ty;
        fresh->anonymous.mapper = mapper;
        fresh->anonymous.applies_type_args = true;
        fresh->anonymous.free_params_unknown = true;
        fresh->anonymous.state = arena_new(&my arena, TyObjectState);
        *intern_slot(me, &my types, key, count) = fresh;
        ret = fresh;
    }
    free(key);
    return ret;
}

// Legacy generic instantiation (generic_degenericize_type, inferred call signatures) reaching a Ty
Ty const* ty_instantiate_with_refinements(Checker* me, Ty const* ty, VecTypeRefinement const* refinements) {
    u32 len = (u32)vec_type_refinement_size(refinements);
    if (!len || !(ty->flags & (TYF_COULD_CONTAIN_TYPE_VARIABLES | TYF_OBJECT))) return ty;
    Ty const** sources = malloc(sizeof(Ty const*) * len * 2);
    Ty const** targets = sources + len;
    u32 count = 0;
    for_each (i, vec_type_refinement, refinements) {
        if (!i->target || !i->refined) continue;
        sources[count] = ty_type_param(me, i->target);
        targets[count] = ty_from_legacy(me, i->refined, (LowerCtx){ .generic = true });
        count++;
    }
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = sources, .len = count }, (TyList){ .data = targets, .len = count });
    free(sources);
    Ty const* ret = ty_instantiate(me, ty, mapper);
    if (ret == ty && (ty->flags & TYF_OBJECT)) ret = ty_apply_type_arguments_mapper(me, ty, mapper);
    return ret;
}
