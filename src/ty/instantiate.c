#include "ty/ty_internal.h"

// Instantiation never copies a type's structure. TypeNode parameters map to their targets, interned types get rebuilt
// through their constructors (and so come out as the existing type if there is one), and object types become a
// {target, mapper} pair whose members are instantiated when somebody asks for them.

// tsc's limits
#define MAX_INSTANTIATION_DEPTH 100
#define MAX_INSTANTIATION_COUNT 5000000

TyMapper const* ty_mapper_new(Checker* me, TyList sources, TyList targets) {
    check(sources.len == targets.len);
    TyMapper* ret = arena_new(&my arena, TyMapper);
    ret->len = sources.len;
    ret->sources = ty_list_copy(me, sources.data, sources.len).data;
    ret->targets = ty_list_copy(me, targets.data, targets.len).data;
    return ret;
}

// inner, then outer: what inner maps to gets instantiated by outer, and outer's own mappings are carried along
static TyMapper const* compose_mappers(Checker* me, TyMapper const* inner, TyMapper const* outer) {
    if (!inner || !inner->len) return outer;
    u32 cap = inner->len + outer->len;
    Ty const** pairs = malloc(sizeof(Ty const*) * cap * 2);
    Ty const** sources = pairs;
    Ty const** targets = pairs + cap;
    u32 len = 0;
    for (u32 i = 0; i < inner->len; i++) {
        sources[len] = inner->sources[i];
        targets[len++] = ty_instantiate(me, inner->targets[i], outer);
    }
    for (u32 i = 0; i < outer->len; i++) {
        bool shadowed = false;
        for (u32 j = 0; j < inner->len; j++) shadowed |= inner->sources[j] == outer->sources[i];
        if (!shadowed) {
            sources[len] = outer->sources[i];
            targets[len++] = outer->targets[i];
        }
    }
    TyMapper const* ret = ty_mapper_new(me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len });
    free(pairs);
    return ret;
}

// A conditional type's own `infer` type parameters aren't mapped by whoever instantiates it (tsc's conditional root
// maps its outer type parameters only): the recursive `Split<B, D>` in the true branch of `S extends `${infer A}.${infer
// B}` ? [A, ...Split<B, D>] : [S]` gets the outer B for its check type, not for its own `infer B`
TyList ty_conditional_own_infers(Checker* me, Ty const* root) {
    u64 key[2] = { KEY_CONDITIONAL_INFERS, root->id };
    TyList const* infers = intern_get(&my links, key, 2);
    return infers ? *infers : (TyList){0};
}

static TyMapper const* without_own_infers(Checker* me, Ty const* root, TyMapper const* mapper) {
    TyList own = ty_conditional_own_infers(me, root);
    TyList const* infers = &own;
    if (!infers->len || !mapper) return mapper;
    u32 dropped = 0;
    for (u32 i = 0; i < mapper->len; i++) {
        for (u32 j = 0; j < infers->len; j++) dropped += mapper->sources[i] == infers->data[j];
    }
    if (!dropped) return mapper;
    Ty const** pairs = malloc(sizeof(Ty const*) * mapper->len * 2);
    panic_if(!pairs, "out of memory");
    Ty const** sources = pairs;
    Ty const** targets = pairs + mapper->len;
    u32 len = 0;
    for (u32 i = 0; i < mapper->len; i++) {
        bool own = false;
        for (u32 j = 0; j < infers->len; j++) own |= mapper->sources[i] == infers->data[j];
        if (own) continue;
        sources[len] = mapper->sources[i];
        targets[len++] = mapper->targets[i];
    }
    TyMapper const* ret = ty_mapper_new(me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len });
    free(pairs);
    return ret;
}

// The mapper with one more (or one replaced) mapping
static TyMapper const* mapper_with(Checker* me, TyMapper const* mapper, Ty const* source, Ty const* target) {
    u32 cap = mapper->len + 1;
    Ty const** pairs = malloc(sizeof(Ty const*) * cap * 2);
    Ty const** sources = pairs;
    Ty const** targets = pairs + cap;
    u32 len = 0;
    for (u32 i = 0; i < mapper->len; i++) {
        if (mapper->sources[i] == source) continue;
        sources[len] = mapper->sources[i];
        targets[len++] = mapper->targets[i];
    }
    sources[len] = source;
    targets[len++] = target;
    TyMapper const* ret = ty_mapper_new(me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len });
    free(pairs);
    return ret;
}

static TyCondBranch instantiate_branch(Checker* me, TyCondBranch const* branch, TyMapper const* mapper) {
    TyCondBranch ret = *branch;
    if ((branch->node || branch->source) && branch->state != LAZY_STATE_DONE) {
        ret.mapper = compose_mappers(me, branch->mapper, mapper);
        ret.state = LAZY_STATE_NOT_STARTED;
    } else if (branch->type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
        ret = (TyCondBranch){ .source = branch->type, .mapper = mapper };
    }
    return ret;
}

static Ty const* conditional_from_root(Checker* me, Ty const* root, Ty const* check_type, TyMapper const* mapper) {
    TyCondBranch if_true = instantiate_branch(me, &root->conditional.branches[1], mapper);
    TyCondBranch if_false = instantiate_branch(me, &root->conditional.branches[0], mapper);
    return ty_conditional(
        me, check_type, ty_instantiate(me, root->conditional.extends, mapper), &if_true, &if_false, root, mapper,
        root->conditional.distributive, ty_conditional_own_infers(me, root)
    );
}

static TyList instantiate_list(Checker* me, TyList list, TyMapper const* mapper, Ty const** buf, u32 buf_len) {
    // Returns the list itself if nothing changed. buf must hold list.len entries
    check(list.len <= buf_len);
    bool changed = false;
    for (u32 i = 0; i < list.len; i++) {
        buf[i] = ty_instantiate(me, list.data[i], mapper);
        changed |= buf[i] != list.data[i];
    }
    return changed ? (TyList){ .data = buf, .len = list.len } : list;
}

// A homomorphic mapped type (`{ [P in keyof T]: X }`) instantiated with an array or tuple for T is an array or tuple of
// the template, not an object with the array methods mapped, and with a union for T a union (tsc's
// instantiateMappedType). Null if this isn't that
static Ty const* instantiate_mapped_over_list(Checker* me, Ty const* ty, TyMapper const* mapper) {
    // Asking for the members of every object type that gets instantiated would defeat their laziness, so look at the
    // syntax first
    Ty const* root = ty->anonymous.target ? ty->anonymous.target : ty;
    TypeNode const* node = root->anonymous.node;
    if (!node || node->tag != TYPE_INTERFACE || vec_interface_member_size(&node->interface.members) != 1) return nullptr;
    InterfaceMember const* member = vec_interface_member_at(&node->interface.members, 0);
    if (member->key.tag != INTERFACE_KEY_MAPPED || member->key.mapped.in_type.tag != TYPE_KEYOF) return nullptr;

    MappedInfo const* mapped = ty_members(me, ty)->mapped;
    if (!mapped || mapped->name_type || !mapped->modifiers_type || mapped->modifiers_type->kind != TY_TYPE_PARAM) return nullptr;
    Ty const* list = ty_instantiate(me, mapped->modifiers_type, mapper);
    if (list == mapped->modifiers_type) return nullptr;
    if (list->kind == TY_UNION) {
        // Distributes over a union (tsc's mapTypeWithAlias): `Mutable<A | B>` is `Mutable<A> | Mutable<B>` by the
        // name it was written with. Primitives map to themselves
        u32 len = list->composite.types.len;
        Ty const** parts = malloc(sizeof(Ty const*) * len);
        panic_if(!parts, "out of memory");
        u32 const mappable = TYF_ANY | TYF_UNKNOWN | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_OBJECT | TYF_INTERSECTION;
        for (u32 i = 0; i < len; i++) {
            Ty const* part = list->composite.types.data[i];
            parts[i] = part->flags & mappable ? ty_instantiate(me, ty, mapper_with(me, mapper, mapped->modifiers_type, part)) : part;
        }
        Ty const* alias_buf[16];
        TyList alias_args = ty->alias_args.len <= c_arraylen(alias_buf) ?
            instantiate_list(me, ty->alias_args, mapper, alias_buf, c_arraylen(alias_buf)) : (TyList){0};
        Ty const* ret = ty_union(me, parts, len, alias_args.len == ty->alias_args.len ? ty->alias : nullptr, alias_args);
        free(parts);
        return ret;
    }
    if (list->kind != TY_REFERENCE) return nullptr;

    if (ty_is_array(me, list)) {
        bool readonly = mapped->readonly ? mapped->readonly > 0 : list->reference.target == my readonly_array_target;
        TyMapper const* element_mapper = mapper_with(me, mapper, mapped->type_param, ty_intrinsic(me, TY_NUMBER));
        return ty_array(me, ty_instantiate(me, mapped->template, element_mapper), readonly);
    }
    if (!ty_is_tuple(list)) return nullptr;
    TupleShape const* shape = list->reference.target->declared.tuple;
    TyList elements = ty_reference_args(me, list);
    if (shape->arity > 32) return nullptr;
    Ty const* mapped_elements[32];
    unsigned char flags[32];
    for (u32 i = 0; i < shape->arity; i++) {
        if (shape->flags[i] & TUPLE_ELEMENT_REST) return nullptr; // TODO: rest and variadic elements
        cstr index = cstr_d(i);
        Ty const* key = ty_string_literal(me, cstr_sv(&index));
        cstr_drop(&index);
        mapped_elements[i] = ty_instantiate(me, mapped->template, mapper_with(me, mapper, mapped->type_param, key));
        flags[i] = shape->flags[i];
        if (mapped->optional > 0) flags[i] |= TUPLE_ELEMENT_OPTIONAL;
        if (mapped->optional < 0) flags[i] &= ~(unsigned)TUPLE_ELEMENT_OPTIONAL;
    }
    (void)elements;
    TupleShape new_shape = {
        .arity = shape->arity,
        .readonly = mapped->readonly ? mapped->readonly > 0 : shape->readonly,
        .flags = flags,
        .names = shape->names,
    };
    Ty const* target = ty_tuple_target(me, &new_shape);
    if (!shape->arity) return target;
    return ty_reference(me, target, (TyList){ .data = mapped_elements, .len = shape->arity });
}

static Ty const* instantiate_anonymous(Checker* me, Ty const* ty, TyMapper const* mapper) {
    // HACK: instantiation expressions only exist for the legacy checker's `new Bar<T>()`, whose result it
    // substitutes itself; they aren't instantiated further
    if (ty->anonymous.applies_type_args) return ty;
    Ty const* as_list = instantiate_mapped_over_list(me, ty, mapper);
    if (as_list) return as_list;
    Ty const* target = ty->anonymous.target ? ty->anonymous.target : ty;
    TyMapper const* inner = ty->anonymous.mapper;

    // The flat composition of the mapper this type already has with the new one: inner's targets get instantiated,
    // and whatever else the new mapper maps is carried along, because the target may mention those parameters too.
    // Only the type parameters the target's node mentions are kept (like tsc), so `{ v: T }` under {T: string, U: X}
    // and under {T: string, U: Y} is one type, and a node that mentions none is never instantiated at all.
    u32 cap = (inner ? inner->len : 0) + mapper->len;
    Ty const* stack_buf[32];
    Ty const** pairs = cap * 2 <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * cap * 2);
    Ty const** sources = pairs;
    Ty const** targets = pairs + cap;
    u32 len = 0;
    for (u32 i = 0; inner && i < inner->len; i++) {
        Ty const* mapped = ty_instantiate(me, inner->targets[i], mapper);
        if (mapped != inner->sources[i]) { // Identity mappings say nothing
            sources[len] = inner->sources[i];
            targets[len++] = mapped;
        }
    }
    for (u32 i = 0; i < mapper->len; i++) {
        bool shadowed = mapper->sources[i] == mapper->targets[i];
        for (u32 j = 0; inner && j < inner->len; j++) shadowed |= inner->sources[j] == mapper->sources[i];
        if (!shadowed) {
            sources[len] = mapper->sources[i];
            targets[len++] = mapper->targets[i];
        }
    }
    TyList free_params = target->anonymous.free_params;
    bool free_params_unknown = target->anonymous.free_params_unknown;
    u32 flags = TYF_OBJECT | (free_params_unknown ? TYF_COULD_CONTAIN_TYPE_VARIABLES : 0);
    if (!free_params_unknown) {
        u32 kept = 0;
        for (u32 i = 0; i < len; i++) {
            bool mentioned = false;
            for (u32 j = 0; j < free_params.len; j++) mentioned |= free_params.data[j] == sources[i];
            if (mentioned) {
                sources[kept] = sources[i];
                targets[kept++] = targets[i];
            }
        }
        len = kept;
        // What's left generic about the result: mapped-to types that are, and mentioned parameters nothing maps
        for (u32 j = 0; j < free_params.len; j++) {
            bool mapped = false;
            for (u32 i = 0; i < len; i++) {
                if (sources[i] == free_params.data[j]) {
                    mapped = true;
                    flags |= targets[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES;
                }
            }
            if (!mapped) flags |= TYF_COULD_CONTAIN_TYPE_VARIABLES;
        }
    }

    Ty const* ret = target;
    if (len) {
        u64 stack_key[64];
        u32 count = 2 + len * 2;
        u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
        key[0] = KEY_ANONYMOUS_INSTANCE;
        key[1] = target->id;
        for (u32 i = 0; i < len; i++) {
            key[2 + i * 2] = sources[i]->id;
            key[3 + i * 2] = targets[i]->id;
        }
        ret = intern_get(&my types, key, count);
        if (!ret) {
            TyMapper const* composed = ty_mapper_new(
                me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len }
            );
            Ty* fresh = ty_alloc(me, TY_ANONYMOUS, flags);
            fresh->anonymous.target = target;
            fresh->anonymous.mapper = composed;
            fresh->anonymous.ctx = target->anonymous.ctx;
            fresh->anonymous.state = arena_new(&my arena, TyObjectState);
            fresh->alias = target->alias;
            if (target->alias_args.len) {
                Ty const** alias_args = arena_new_n(&my arena, Ty const*, target->alias_args.len);
                for (u32 i = 0; i < target->alias_args.len; i++) {
                    alias_args[i] = ty_instantiate(me, target->alias_args.data[i], composed);
                }
                fresh->alias_args = (TyList){ .data = alias_args, .len = target->alias_args.len };
            }
            // Instantiating the alias arguments may have grown the table, so the slot is looked up again
            *intern_slot(me, &my types, key, count) = fresh;
            ret = fresh;
        }
        if (key != stack_key) free(key);
    }
    if (pairs != stack_buf) free(pairs);
    if (ty->anonymous.realiased && ret != ty) {
        Ty const* alias_buf[16];
        check(ty->alias_args.len <= c_arraylen(alias_buf));
        TyList alias_args = instantiate_list(me, ty->alias_args, mapper, alias_buf, c_arraylen(alias_buf));
        ret = ty_realias(me, ret, ty->alias, alias_args);
    }
    return ret;
}

// `type Bar = Omit<Foo, "c">`: the instantiation of a generic alias written directly as the body of another alias goes
// by the new name (tsc passes the new alias symbol to getTypeAliasInstantiation). Same members, its own identity
Ty const* ty_realias(Checker* me, Ty const* ty, Symbol const* alias, TyList alias_args) {
    if (ty->alias == alias) return ty;
    if (ty->kind == TY_UNION && !(ty->flags & TYF_BOOLEAN)) {
        return ty_union(me, ty->composite.types.data, ty->composite.types.len, alias, alias_args);
    }
    if (ty->kind == TY_INTERSECTION) {
        return ty_intersection(me, ty->composite.types.data, ty->composite.types.len, alias, alias_args);
    }
    bool deferred = ty->kind == TY_INDEXED || ty->kind == TY_CONDITIONAL;
    if (!deferred && (ty->kind != TY_ANONYMOUS || !ty->anonymous.target || ty->anonymous.applies_type_args)) return ty;
    u64 stack_key[40];
    u32 count = 4 + alias_args.len;
    if (count > c_arraylen(stack_key)) return ty;
    stack_key[0] = KEY_ANONYMOUS_INSTANCE;
    stack_key[1] = ty->id;
    stack_key[2] = UINT64_MAX - 1; // Not a type id: tells these apart from ordinary instantiations
    stack_key[3] = (u64)(uintptr_t)alias;
    for (u32 i = 0; i < alias_args.len; i++) stack_key[4 + i] = alias_args.data[i]->id;
    Ty const* ret = intern_get(&my types, stack_key, count);
    if (!ret) {
        Ty* fresh = ty_alloc(me, ty->kind, ty->flags);
        if (ty->kind == TY_INDEXED) {
            fresh->indexed = ty->indexed;
        } else if (ty->kind == TY_CONDITIONAL) {
            // The root stays the unnamed one; instantiation puts the name back on (instantiate_inner)
            fresh->conditional = ty->conditional;
        } else {
            fresh->anonymous = ty->anonymous;
            fresh->anonymous.realiased = true;
            fresh->anonymous.state = arena_new(&my arena, TyObjectState);
        }
        fresh->alias = alias;
        fresh->alias_args = ty_list_copy(me, alias_args.data, alias_args.len);
        *intern_slot(me, &my types, stack_key, count) = fresh;
        ret = fresh;
    }
    return ret;
}

// What an instantiation of a type operator made by an alias goes by: the alias, with its arguments instantiated
static Ty const* instantiate_alias(Checker* me, Ty const* ty, Ty const* result, TyMapper const* mapper) {
    if (!ty->alias) return result;
    Ty const* alias_buf[16];
    check(ty->alias_args.len <= c_arraylen(alias_buf));
    TyList alias_args = instantiate_list(me, ty->alias_args, mapper, alias_buf, c_arraylen(alias_buf));
    return ty_realias(me, result, ty->alias, alias_args);
}

// Start over from the conditional type as written, with everything that has happened to it since
static Ty const* resolve_conditional_instance(Checker* me, Ty const* root, TyMapper const* combined, bool* distributed) {
    Ty const* check_type = ty_instantiate(me, root->conditional.check, combined);
    if (root->conditional.distributive && root->conditional.check->kind == TY_TYPE_PARAM &&
        check_type != root->conditional.check
    ) {
        // Distribution: the whole conditional type is instantiated once per member, with the check type
        // parameter mapped to that member everywhere
        if (check_type->flags & TYF_NEVER) return check_type;
        if (check_type->kind == TY_UNION) {
            TyList members = check_type->composite.types;
            Ty const** results = malloc(sizeof(Ty const*) * members.len);
            for (u32 i = 0; i < members.len; i++) {
                TyMapper const* member_mapper = mapper_with(me, combined, root->conditional.check, members.data[i]);
                results[i] = conditional_from_root(me, root, members.data[i], member_mapper);
            }
            Ty const* ret = ty_union(me, results, members.len, nullptr, (TyList){0});
            free(results);
            *distributed = true;
            return ret;
        }
    }
    return conditional_from_root(me, root, check_type, combined);
}

// Like tsc's getConditionalTypeInstantiation: the mapper is cut down to the root's outer type parameters, which is
// also what the result is cached by, so the same conditional type under another mapper that agrees on those is one
// lookup, and mappers don't grow with every level of nesting
static Ty const* instantiate_conditional(Checker* me, Ty const* ty, TyMapper const* mapper, bool* distributed) {
    Ty const* root = ty->conditional.root;
    TyConditionalOuter const* outer = ty_conditional_outer(me, root);
    if (!outer || outer->unknown) {
        TyMapper const* combined = without_own_infers(me, root, compose_mappers(me, ty->conditional.mapper, mapper));
        return resolve_conditional_instance(me, root, combined, distributed);
    }

    u32 len = outer->params.len;
    u64 stack_key[34];
    Ty const* stack_targets[32];
    u64* key = len + 2 <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * (len + 2));
    Ty const** targets = len <= c_arraylen(stack_targets) ? stack_targets : malloc(sizeof(Ty const*) * len);
    key[0] = KEY_CONDITIONAL_INSTANCE;
    key[1] = root->id;
    u32 mapped = 0;
    for (u32 i = 0; i < len; i++) {
        // What the parameter maps to through this conditional type's mapper and then the new one: no need to compose
        // the two, which would instantiate every one of the first's targets
        targets[i] = ty_instantiate(me, ty_instantiate(me, outer->params.data[i], ty->conditional.mapper), mapper);
        key[2 + i] = targets[i]->id;
        mapped += targets[i] != outer->params.data[i];
    }
    Ty const* const* cached = intern_get(&my types, key, len + 2);
    Ty const* ret = nullptr;
    if (cached) {
        ret = cached[0];
        *distributed = (bool)(uintptr_t)cached[1];
    } else {
        // Only what changes: identity mappings say nothing
        Ty const** sources = malloc(sizeof(Ty const*) * (mapped ? mapped * 2 : 1));
        Ty const** kept = sources + mapped;
        u32 kept_len = 0;
        for (u32 i = 0; i < len; i++) {
            if (targets[i] == outer->params.data[i]) continue;
            sources[kept_len] = outer->params.data[i];
            kept[kept_len++] = targets[i];
        }
        TyMapper const* restricted = ty_mapper_new(
            me, (TyList){ .data = sources, .len = kept_len }, (TyList){ .data = kept, .len = kept_len }
        );
        free(sources);
        // A result cut short by the instantiation limits (TS2589) isn't what this instantiation is: not cached
        bool limit_hit = my instantiation_limit_hit;
        my instantiation_limit_hit = false;
        ret = resolve_conditional_instance(me, root, restricted, distributed);
        bool cacheable = !my instantiation_limit_hit;
        my instantiation_limit_hit |= limit_hit;
        if (cacheable) {
            Ty const** entry = arena_new_n(&my arena, Ty const*, 2);
            entry[0] = ret;
            entry[1] = (Ty const*)(uintptr_t)*distributed;
            *intern_slot(me, &my types, key, len + 2) = entry;
        }
    }
    if (key != stack_key) free(key);
    if (targets != stack_targets) free(targets);
    return ret;
}

static Ty const* instantiate_inner(Checker* me, Ty const* ty, TyMapper const* mapper) {
    Ty const* stack_buf[16];
    switch ((TyKind)ty->kind) {
        case TY_INTRINSIC:
        case TY_LITERAL:
        case TY_DECLARED:
            return ty;

        case TY_TYPE_PARAM:
            for (u32 i = 0; i < mapper->len; i++) {
                if (mapper->sources[i] == ty) return mapper->targets[i];
            }
            return ty;

        case TY_REFERENCE: {
            if (ty->reference.deferred) {
                TyDeferredRef const* deferred = ty->reference.deferred;
                if (deferred->state == LAZY_STATE_DONE && !ty->alias) {
                    // Resolved by now, so it can become an ordinary reference
                    TyList args = deferred->args;
                    Ty const** buf = args.len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * args.len);
                    TyList mapped = instantiate_list(me, args, mapper, buf, args.len);
                    Ty const* ret = mapped.data == args.data ? ty : ty_reference(me, ty->reference.target, mapped);
                    if (buf != stack_buf) free(buf);
                    return ret;
                }
                Ty const* alias_buf[16];
                check(ty->alias_args.len <= c_arraylen(alias_buf));
                TyList alias_args = instantiate_list(me, ty->alias_args, mapper, alias_buf, c_arraylen(alias_buf));
                return ty_deferred_reference(
                    me, deferred->node, deferred->ctx, compose_mappers(me, deferred->mapper, mapper), ty->alias, alias_args
                );
            }
            TyList args = ty->reference.args;
            Ty const** buf = args.len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * args.len);
            TyList mapped = instantiate_list(me, args, mapper, buf, args.len);
            Ty const* this_arg = ty->reference.this_arg;
            Ty const* mapped_this = this_arg ? ty_instantiate(me, this_arg, mapper) : nullptr;
            Ty const* ret = mapped.data == args.data && mapped_this == this_arg ? ty :
                ty_reference_with_this(me, ty->reference.target, mapped, mapped_this);
            if (buf != stack_buf) free(buf);
            return ret;
        }

        case TY_ANONYMOUS:
            return instantiate_anonymous(me, ty, mapper);

        case TY_UNION:
        case TY_INTERSECTION: {
            TyList types = ty->composite.types;
            Ty const** buf = types.len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * types.len);
            TyList mapped = instantiate_list(me, types, mapper, buf, types.len);
            Ty const* alias_buf[16];
            check(ty->alias_args.len <= c_arraylen(alias_buf));
            TyList alias_args = instantiate_list(me, ty->alias_args, mapper, alias_buf, c_arraylen(alias_buf));
            Ty const* ret = ty;
            if (mapped.data != types.data || alias_args.data != ty->alias_args.data) {
                ret = ty->kind == TY_UNION ?
                    ty_union(me, mapped.data, mapped.len, ty->alias, alias_args) :
                    ty_intersection(me, mapped.data, mapped.len, ty->alias, alias_args);
            }
            if (buf != stack_buf) free(buf);
            return ret;
        }

        case TY_KEYOF:
            return ty_keyof(me, ty_instantiate(me, ty->keyof.operand, mapper));

        case TY_TEMPLATE_LITERAL: {
            TyList types = ty->template_literal.types;
            Ty const** buf = types.len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * types.len);
            TyList mapped = instantiate_list(me, types, mapper, buf, types.len);
            Ty const* ret = mapped.data == types.data ? ty : ty_template_literal(me, ty->template_literal.texts, mapped.data, mapped.len);
            if (buf != stack_buf) free(buf);
            return ret;
        }

        case TY_STRING_MAPPING:
            return ty_string_mapping(
                me, ty->string_mapping.mapping, ty->string_mapping.symbol, ty_instantiate(me, ty->string_mapping.type, mapper)
            );

        case TY_INDEXED: {
            Ty const* ret = ty_indexed(
                me,
                ty_instantiate(me, ty->indexed.object, mapper),
                ty_instantiate(me, ty->indexed.index, mapper)
            );
            return ret->kind == TY_INDEXED ? instantiate_alias(me, ty, ret, mapper) : ret;
        }

        case TY_CONDITIONAL: {
            bool distributed = false;
            Ty const* ret = instantiate_conditional(me, ty, mapper, &distributed);
            bool keeps_alias = ret->kind == (distributed ? TY_UNION : TY_CONDITIONAL);
            return keeps_alias ? instantiate_alias(me, ty, ret, mapper) : ret;
        }
    }
    impossible;
}

Ty const* ty_instantiate(Checker* me, Ty const* ty, TyMapper const* mapper) {
    if (!mapper || !mapper->len || !(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return ty;
    if (my instantiation_depth >= MAX_INSTANTIATION_DEPTH || my instantiation_count >= MAX_INSTANTIATION_COUNT) {
        // Whoever reports errors next turns the flag into TS2589 (check_assignable_or_error_internal, for now)
        my instantiation_limit_hit = true;
        return ty_intrinsic(me, TY_ERROR);
    }
    my instantiation_depth++;
    my instantiation_count++;
    Ty const* ret = instantiate_inner(me, ty, mapper);
    my instantiation_depth--;
    return ret;
}
