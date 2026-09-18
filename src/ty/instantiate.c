#include "ty/ty_internal.h"

// Instantiation never copies a type's structure. Type parameters map to their targets, interned types get rebuilt
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

static Ty const* instantiate_anonymous(Checker* me, Ty const* ty, TyMapper const* mapper) {
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
            TyList args = ty->reference.args;
            Ty const** buf = args.len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * args.len);
            TyList mapped = instantiate_list(me, args, mapper, buf, args.len);
            Ty const* ret = mapped.data == args.data ? ty : ty_reference(me, ty->reference.target, mapped);
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

        case TY_INDEXED:
            return ty_indexed(
                me,
                ty_instantiate(me, ty->indexed.object, mapper),
                ty_instantiate(me, ty->indexed.index, mapper)
            );

        case TY_CONDITIONAL:
            return ty_conditional(
                me,
                ty_instantiate(me, ty->conditional.check, mapper),
                ty_instantiate(me, ty->conditional.extends, mapper),
                ty_instantiate(me, ty->conditional.if_true, mapper),
                ty_instantiate(me, ty->conditional.if_false, mapper)
            );
    }
    impossible;
}

Ty const* ty_instantiate(Checker* me, Ty const* ty, TyMapper const* mapper) {
    if (!mapper || !mapper->len || !(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return ty;
    if (my instantiation_depth >= MAX_INSTANTIATION_DEPTH || my instantiation_count >= MAX_INSTANTIATION_COUNT) {
        // TODO(stage 3): TS2589 "Type instantiation is excessively deep and possibly infinite." at the current node
        my instantiation_limit_hit = true;
        return ty_intrinsic(me, TY_ERROR);
    }
    my instantiation_depth++;
    my instantiation_count++;
    Ty const* ret = instantiate_inner(me, ty, mapper);
    my instantiation_depth--;
    return ret;
}
