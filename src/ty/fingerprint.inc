// The structural fingerprints of plan 86 stage 4-0's cleandiff (taint.c, -DTTTT_TAINT) and of the shared graph's
// fingerprint-at-publish oracle (graph_debug.c, -DTTTT_GRAPH_FINGERPRINT): a type by what it is, ids replaced by the
// referents' fingerprints, AST nodes, symbols and declarations by address (stable within one process only). Included
// by those two files only; FP_TLS is each one's storage for the fingerprinting state (thread_local where workers
// fingerprint at once)
#ifndef FP_TLS
#define FP_TLS
#endif
// Whether a fingerprint includes parts resolved lazily, when they are done (a synthetic type parameter's constraint, an
// owner-less property's type, a synthetic signature's return): cleandiff's do; the shared graph's oracle leaves them out,
// since whether they are done yet when another result is fingerprinted is the schedule's (they are compared on their own)
#ifndef FP_LAZY_PARTS
#define FP_LAZY_PARTS 1
#endif

// Sets and maps of u64 keys (never 0)
typedef struct U64Map {
    u64* keys;
    u64* values; // null for a set
    u32 cap, len;
} U64Map;

static u64 mix64(u64 h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

static void map_grow(U64Map* me, bool with_values) {
    U64Map bigger = { .cap = my cap ? my cap * 2 : 1024, .len = my len };
    bigger.keys = calloc(bigger.cap, sizeof(u64));
    if (with_values) bigger.values = calloc(bigger.cap, sizeof(u64));
    for (u32 i = 0; i < my cap; i++) {
        if (!my keys[i]) continue;
        u32 j = (u32)mix64(my keys[i]) & (bigger.cap - 1);
        while (bigger.keys[j]) j = (j + 1) & (bigger.cap - 1);
        bigger.keys[j] = my keys[i];
        if (with_values) bigger.values[j] = my values[i];
    }
    free(my keys);
    free(my values);
    *me = bigger;
}

// The slot of `key` (inserted with value 0 if `insert`), null if absent
static u64* map_find(U64Map* me, u64 key, bool insert, bool with_values, bool* inserted) {
    if (inserted) *inserted = false;
    if (!my cap) {
        if (!insert) return nullptr;
        map_grow(me, with_values);
    }
    if (insert && (my len + 1) * 2 > my cap) map_grow(me, with_values);
    for (u32 i = (u32)mix64(key) & (my cap - 1);; i = (i + 1) & (my cap - 1)) {
        if (my keys[i] == key) return with_values ? &my values[i] : &my keys[i];
        if (!my keys[i]) {
            if (!insert) return nullptr;
            my keys[i] = key;
            my len++;
            if (inserted) *inserted = true;
            return with_values ? &my values[i] : &my keys[i];
        }
    }
}

static void map_clear(U64Map* me) {
    free(my keys);
    free(my values);
    *me = (U64Map){0};
}

static FP_TLS U64Map ty_fps; // Ty* -> fingerprint (1: in progress), this checker's

static u64 fp_mix(u64 h, u64 v) {
    return mix64(h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)));
}

static u64 fp_bytes(void const* data, size_t len) {
    u64 h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < len; i++) h = (h ^ ((unsigned char const*)data)[i]) * 0x100000001b3ull;
    return h | 1;
}

static u64 fp_atom(Atom const* atom) {
    if (!atom) return 7;
    // (A unique symbol's escaped name `__@name@id` has its type's id: the same name in another history)
    u32 len = atom->len;
    if (len > 3 && memcmp(atom->str, "__@", 3) == 0) {
        while (len > 3 && atom->str[len - 1] >= '0' && atom->str[len - 1] <= '9') len--;
    }
    return fp_bytes(atom->str, len);
}

static u64 fp_ty(Ty const* ty);
static bool from_type_node_fp;

// TTTT_TAINT_DESCRIBE: what a mismatching fingerprint is made of (the fields, nested types to a depth)
static FP_TLS cstr* trace;
static FP_TLS u32 trace_depth;

static u64 mixt(u64 h, char const* label, u64 v) {
    if (trace && trace_depth <= 14) {
        char const* at = strrchr(label, '>');
        cstr_append_fmt(trace, " %s=%llx", at ? at + 1 : label, (unsigned long long)v);
    }
    return fp_mix(h, v);
}
#define M(h, v) mixt(h, #v, v)

static u64 fp_list(u64 h, TyList list) {
    h = M(h, list.len);
    for (u32 i = 0; i < list.len; i++) h = M(h, fp_ty(list.data[i]));
    return h;
}

static u64 fp_mapper(TyMapper const* mapper) {
    if (!mapper) return 11;
    u64 h = M(13, mapper->len);
    for (u32 i = 0; i < mapper->len; i++) {
        h = M(h, fp_ty(mapper->sources[i]));
        h = M(h, fp_ty(mapper->targets[i]));
    }
    return h;
}

static u64 fp_ty_inner(Checker* c, Ty const* ty) {
    u64 h = M(17, ty->kind);
    h = M(h, ty->flags);
    h = M(h, (u64)(uintptr_t)ty->alias);
    h = fp_list(h, ty->alias_args);
    switch ((TyKind)ty->kind) {
        case TY_INTRINSIC:
            for (u32 i = 0; i < TY_INTRINSIC_COUNT; i++) {
                if (c->intrinsics[i] == ty) return M(h, 1000 + i);
            }
            h = M(h, fp_bytes(ty->intrinsic.name.buf, ty->intrinsic.name.size));
            return fp_list(h, ty->intrinsic.unresolved_args);
        case TY_LITERAL: {
            h = M(h, ty->literal.value.tag);
            if (ty->literal.string) h = M(h, fp_atom(ty->literal.string));
            else if (ty->literal.value.tag == VALUE_NUMBER) h = M(h, fp_bytes(&ty->literal.value.number, sizeof(double)));
            else if (ty->literal.value.tag == VALUE_BOOLEAN) h = M(h, ty->literal.value.boolean);
            h = M(h, (u64)(uintptr_t)ty->literal.enum_decl);
            h = M(h, (u64)(uintptr_t)ty->literal.enum_member);
            return M(h, (u64)(uintptr_t)ty->literal.unique);
        }
        case TY_TYPE_PARAM: {
            TyParamState const* state = ty->param.state;
            h = M(h, (u64)(uintptr_t)ty->param.symbol);
            h = M(h, fp_atom(ty->param.name));
            h = M(h, state->is_infer | state->is_this << 1 | state->is_check_marker << 2);
            if (state->clone_of) {
                // (The clones of one signature's type parameters share a mapper that maps every one of them to its
                // clone: the clones by their position in it, or which one is fingerprinted first would matter)
                h = M(h, fp_ty(state->clone_of));
                TyMapper const* mapper = state->clone_mapper;
                for (u32 i = 0; mapper && i < mapper->len; i++) {
                    Ty const* target = mapper->targets[i];
                    bool sibling = target->kind == TY_TYPE_PARAM && target->param.state->clone_mapper == mapper;
                    h = M(h, fp_ty(mapper->sources[i]));
                    h = M(h, sibling ? 0x51b + i : fp_ty(target));
                }
            }
            if (state->distributed_of) h = M(h, fp_ty(state->distributed_of));
            // (Synthetic ones (no symbol: a mapped type's key stand-in, tuple_params' T) are told apart by their constraint)
            if (state->is_this || (FP_LAZY_PARTS && !ty->param.symbol && sw_load(&state->constraint) > SW_POISON)) {
                h = M(h, fp_ty(ty_param_constraint_published(state)));
            }
            return h;
        }
        case TY_DECLARED: {
            h = M(h, ty->declared.kind | ty->declared.js_container << 8);
            h = M(h, (u64)(uintptr_t)ty->declared.symbol);
            h = M(h, (u64)(uintptr_t)ty->declared.namespace_symbol);
            h = M(h, (u64)(uintptr_t)ty->declared.merged_value);
            h = M(h, fp_atom(ty->declared.name));
            TupleShape const* tuple = ty->declared.tuple;
            if (tuple) {
                h = M(h, tuple->arity | (u64)tuple->readonly << 32);
                if (tuple->flags) h = M(h, fp_bytes(tuple->flags, tuple->arity));
                for (u32 i = 0; tuple->names && i < tuple->arity; i++) h = M(h, fp_atom(tuple->names[i]));
                for (u32 i = 0; tuple->labels && i < tuple->arity; i++) h = M(h, (u64)(uintptr_t)tuple->labels[i]);
            }
            return h;
        }
        case TY_REFERENCE: {
            h = M(h, fp_ty(ty->reference.target));
            if (ty->reference.deferred) {
                h = M(h, (u64)(uintptr_t)ty->reference.deferred->node);
                h = M(h, fp_mapper(ty->reference.deferred->mapper));
            } else {
                h = fp_list(h, ty->reference.args);
            }
            if (ty->reference.this_arg) h = M(h, fp_ty(ty->reference.this_arg));
            // (Who made it first: a named mechanism (plan 86 stage 4b), counted apart with TTTT_TAINT_FROM_TYPE_NODE=1)
            return from_type_node_fp ? M(h, ty->reference.from_type_node) : h;
        }
        case TY_ANONYMOUS: {
            h = M(h, (u64)(uintptr_t)ty->anonymous.node);
            h = M(h, (u64)(uintptr_t)ty->anonymous.function);
            if (ty->anonymous.is_reverse) {
                ReverseMapped const* reverse = ty->anonymous.reverse;
                h = M(h, fp_ty(reverse->source));
                h = M(h, fp_ty(reverse->operand));
                h = M(h, fp_ty(reverse->mapped->type_param));
            } else {
                h = M(h, (u64)(uintptr_t)ty->anonymous.merged_value);
            }
            if (ty->anonymous.overload_count) h = M(h, (u64)(uintptr_t)ty->anonymous.overloads[0]);
            else if (ty->anonymous.object_literal || ty->anonymous.rest_of_binding) {
                h = M(h, (u64)(uintptr_t)ty->anonymous.literal_expr);
            }
            if (ty->anonymous.target) h = M(h, fp_ty(ty->anonymous.target));
            h = M(h, fp_mapper(ty->anonymous.mapper));
            h = M(h, ty->anonymous.overload_count | (u64)ty->anonymous.spread_alternative << 16);
            u64 bits = ty->anonymous.applies_type_args | ty->anonymous.realiased << 1 | ty->anonymous.alias_body_twin << 2 |
                ty->anonymous.object_literal << 3 | ty->anonymous.regular << 4 | ty->anonymous.jsx_attributes << 5 |
                ty->anonymous.js_literal << 6 | ty->anonymous.is_reverse << 7 | ty->anonymous.contains_spread << 8 |
                ty->anonymous.module_clone << 9 | ty->anonymous.empty_symbol << 10 | ty->anonymous.mapped << 11 |
                ty->anonymous.rest_of_binding << 12;
            return M(h, bits);
        }
        case TY_UNION:
        case TY_INTERSECTION:
            h = fp_list(h, ty->composite.types);
            if (ty->composite.origin) h = M(h, fp_ty(ty->composite.origin));
            return h;
        case TY_KEYOF:
            return M(h, fp_ty(ty->keyof.operand));
        case TY_INDEXED:
            h = M(h, fp_ty(ty->indexed.object));
            h = M(h, fp_ty(ty->indexed.index));
            return M(h, ty->indexed.include_undefined);
        case TY_CONDITIONAL: {
            Ty const* root = ty->conditional.root;
            if (root && root != ty) return fp_mix(M(h, fp_ty(root)), fp_mapper(ty->conditional.mapper));
            h = M(h, fp_ty(ty->conditional.check));
            h = M(h, fp_ty(ty->conditional.extends));
            h = M(h, ty->conditional.distributive | ty->conditional.check_tuples << 1);
            if (ty_root_outer(ty)) h = M(h, (u64)(uintptr_t)ty_root_outer(ty)->node.buf);
            return M(h, fp_mapper(ty->conditional.mapper));
        }
        case TY_SUBSTITUTION:
            return fp_mix(M(h, fp_ty(ty->substitution.base)), fp_ty(ty->substitution.constraint));
        case TY_TEMPLATE_LITERAL:
            for (u32 i = 0; i <= ty->template_literal.types.len; i++) h = M(h, fp_atom(ty->template_literal.texts[i]));
            return fp_list(h, ty->template_literal.types);
        case TY_STRING_MAPPING:
            return fp_mix(M(h, ty->string_mapping.mapping), fp_ty(ty->string_mapping.type));
        case TY_EVOLVING_ARRAY:
            return M(h, fp_ty(ty->evolving_array.element));
    }
    return h;
}

// The types being fingerprinted, outermost first. A type reached again while it is (a cycle: a deferred reference's
// mapper, a recursive instantiation) is its distance up the stack, so a type's fingerprint is the same whichever part
// of the cycle is fingerprinted first; a type whose fingerprint used such a back reference to one of its ancestors
// isn't memoized (it is relative to them)
static FP_TLS Ty const* fp_stack[256];
static FP_TLS u32 fp_stack_len;
static FP_TLS u32 fp_min_backref = UINT32_MAX;

static u64 fp_ty(Ty const* ty) {
    if (!ty) return 3;
    for (u32 k = fp_stack_len; k--;) {
        if (fp_stack[k] == ty) {
            if (k < fp_min_backref) fp_min_backref = k;
            return fp_mix(5, fp_stack_len - k);
        }
    }
    if (!trace) {
        u64* known = map_find(&ty_fps, (u64)(uintptr_t)ty, false, true, nullptr);
        if (known && *known) return *known;
    }
    if (fp_stack_len == c_arraylen(fp_stack)) {
        // (Too deep: cut off here, relative to the outermost type like a back reference to it)
        fp_min_backref = 0;
        return 9;
    }
    bool traced = trace && trace_depth <= 14;
    if (traced) {
        cstr_append_fmt(trace, " (%u:", (unsigned)ty->kind);
        trace_depth++;
    }
    u32 index = fp_stack_len;
    fp_stack[fp_stack_len++] = ty;
    u32 saved_min = fp_min_backref;
    fp_min_backref = UINT32_MAX;
    u64 h = fp_ty_inner(current_checker, ty) | 2;
    fp_stack_len--;
    if (fp_min_backref >= index && !trace) *map_find(&ty_fps, (u64)(uintptr_t)ty, true, true, nullptr) = h;
    if (saved_min < fp_min_backref) fp_min_backref = saved_min;
    if (traced) {
        trace_depth--;
        cstr_append_fmt(trace, ")");
    }
    return h;
}

static U64Map prop_owners; // Prop* -> its first Members' owner's fingerprint and its index there
// (graph_debug.c's is shared by its workers, under a lock)
#ifndef FP_PROP_OWNER
static u64 fp_prop_owner(Prop const* prop) {
    u64* owner = map_find(&prop_owners, (u64)(uintptr_t)prop, false, true, nullptr);
    return owner ? *owner : 0;
}
#define FP_PROP_OWNER(prop) fp_prop_owner(prop)
#endif

static u64 fp_prop(Prop const* prop, u32 depth) {
    if (!prop) return 19;
    u64 h = M(23, fp_atom(prop->name));
    h = M(h, prop->flags);
    h = M(h, prop->thisless | prop->mapped << 1 | prop->synthetic << 2 | prop->is_template << 3);
    h = M(h, (u64)(uintptr_t)prop->parent);
    PropDecl const* decl = prop->decl;
    h = M(h, (u64)(uintptr_t)decl->node);
    h = M(h, (u64)(uintptr_t)decl->lazy);
    h = M(h, (u64)(uintptr_t)decl->function);
    h = M(h, (u64)(uintptr_t)decl->symbol);
    if (prop->target && depth < 32) {
        u64 target = fp_prop(prop->target, depth + 1);
        if (!target) return 0;
        h = M(h, target);
    }
    h = M(h, fp_mapper(prop->mapper));
    // (A property made with its type: synthetic, a literal's. Or one typed lazily by its owner: a mapped type's. Without
    // either it has no identity to go by)
    if (!prop->target && !decl->node && !decl->lazy && !decl->function && !decl->symbol) {
        u64 owner = FP_PROP_OWNER(prop);
        // (By its owner if it has one: whether its type is resolved yet by the time it is fingerprinted is history, and
        // the type is compared on its own, TK_PROP_TYPE)
        if (owner) h = M(h, owner);
        else if (FP_LAZY_PARTS && prop_type_known(prop)) h = M(h, fp_ty(prop_type_known(prop)));
        else return 0;
    }
    return h;
}

// (Only what a signature is made with: signature_resolve() fills in its parameters, and a JSDoc one's target, later)
static u64 fp_signature(Signature const* sig, u32 depth) {
    if (!sig) return 29;
    u64 h = M(31, sig->flags);
    h = M(h, (u64)(uintptr_t)sig->function);
    h = M(h, fp_ty(sig->class_instance));
    h = fp_list(h, sig->class_type_params);
    if (!sig->function && sig->target && depth < 32) h = M(h, fp_signature(sig->target, depth + 1));
    // (Resolving a generic one adds its own type parameters' clones to the mapper, clone_type_params(): not those)
    // (Its key is what it is made of: the resolved mapper (sig_mapper()) is only there once it is resolved)
    TyMapper const* mapper = FP_LAZY_PARTS ? sig_mapper_published(sig) : sig->mapper;
    for (u32 i = 0; mapper && i < mapper->len; i++) {
        Ty const* target = mapper->targets[i];
        if (target->kind == TY_TYPE_PARAM && target->param.state->clone_mapper == mapper) continue;
        bool own = false;
        for (u32 j = 0; j < mapper->len && !own; j++) {
            Ty const* t = mapper->targets[j];
            own = t->kind == TY_TYPE_PARAM && t->param.state->clone_mapper == mapper && t->param.state->clone_of == mapper->sources[i];
        }
        if (own) continue;
        h = M(h, fp_ty(mapper->sources[i]));
        h = M(h, fp_ty(target));
    }
    if (!sig->function && !sig->target) {
        SigResolved const* r = sig_res_published(sig);
        h = M(h, r->param_count | (u64)r->min_args << 32);
        h = fp_list(h, r->type_params);
        for (u32 i = 0; i < r->param_count; i++) {
            h = M(h, fp_atom(r->params[i].name));
            h = M(h, fp_ty(r->params[i].type));
            h = M(h, r->params[i].optional | r->params[i].rest << 1);
        }
        if (FP_LAZY_PARTS && sig->ret_state == LAZY_STATE_DONE) h = M(h, fp_ty(sig->ret));
    }
    return h;
}

static u64 fp_members(Members const* members) {
    if (!members) return 37;
    u64 h = M(41, members->prop_count);
    for (u32 i = 0; i < members->prop_count; i++) {
        // (A property without an identity in this history (a synthetic one whose owner this checker never saw, its type
        // not resolved yet): the members can't be compared)
        u64 prop = fp_prop(members->props[i], 0);
        if (!prop) return 0;
        h = M(h, prop);
    }
    h = M(h, members->call_count | (u64)members->construct_count << 32);
    for (u32 i = 0; i < members->call_count; i++) h = M(h, fp_signature(members->call[i], 0));
    for (u32 i = 0; i < members->construct_count; i++) h = M(h, fp_signature(members->construct[i], 0));
    h = M(h, members->index_count);
    for (u32 i = 0; i < members->index_count; i++) {
        h = M(h, fp_ty(members->index[i].key));
        h = M(h, fp_ty(members->index[i].value));
        h = M(h, members->index[i].readonly);
    }
    if (members->mapped) {
        h = M(h, fp_ty(members->mapped->type_param));
        h = M(h, fp_ty(members->mapped->constraint));
        h = M(h, fp_ty(members->mapped->name_type));
    }
    MembersRare const* rare = members_rare(members);
    h = M(h, rare->generic_keys_known | rare->index_signatures_incomplete << 1 | rare->contains_error << 2 |
        rare->recursive_bases << 3 | (u64)rare->base_count << 8 | (u64)rare->eager_base_count << 24);
    for (u32 i = 0; i < rare->base_count; i++) h = M(h, fp_ty(rare->bases[i]));
    return h;
}

// A type id in a key word, or 0 if it can't be one
// (Any checker's id with --shared-graph: the chunk read with an acquire, a word that is no id of a chunk read is none)
static Ty const* key_type(Checker* c, u64 id) {
    if (!id || id >> 32) return nullptr;
    if (!c->graph && id >= c->next_type_id) return nullptr;
    Ty const** chunk = __atomic_load_n(&c->lazy.types[(u32)id >> LAZY_TYPE_CHUNK_BITS], __ATOMIC_ACQUIRE);
    return chunk ? __atomic_load_n(&chunk[id & ((1 << LAZY_TYPE_CHUNK_BITS) - 1)], __ATOMIC_ACQUIRE) : nullptr;
}

// The fingerprint of an intern key of a kind cleandiff knows the words of (0 for another kind)
static u64 fp_key(Checker* c, u32 kind, u64 const* words, u32 count) {
    u64 h = fp_mix(43, kind);
    switch (kind) {
        case KEY_ANONYMOUS_INSTANCE: {
            Ty const* target = key_type(c, words[1]);
            if (!target) return 0;
            h = fp_mix(h, fp_ty(target));
            // (ty_alias_body_mapped_instance()'s and ty_realias()'s keys: a marker word and the alias Symbol)
            if (count >= 4 && (words[2] == UINT64_MAX - 2 || words[2] == UINT64_MAX - 1)) {
                h = fp_mix(fp_mix(h, words[2]), words[3]);
                for (u32 i = 4; i < count; i++) h = fp_mix(h, fp_ty(key_type(c, words[i])));
                return h;
            }
            for (u32 i = 2; i < count; i++) {
                h = fp_mix(h, fp_ty(key_type(c, words[i] >> 32)));
                h = fp_mix(h, fp_ty(key_type(c, words[i] & 0xffffffffu)));
            }
            return h;
        }
        case KEY_CONDITIONAL_INSTANCE:
        case KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT: {
            Ty const* root = key_type(c, words[1]);
            if (!root) return 0;
            h = fp_mix(h, fp_ty(root));
            for (u32 i = 2; i < count; i++) {
                h = fp_mix(h, (words[i] & 0xffffffffu) ? fp_ty(key_type(c, words[i] & 0xffffffffu)) : 0);
                h = fp_mix(h, (words[i] >> 32) ? fp_ty(key_type(c, words[i] >> 32)) : 0);
            }
            return h;
        }
        case KEY_REGULAR_UNION:
        case KEY_WIDENED_OBJECTS:
            // (Keyed by the type's address, not its id: a fresh checker reuses its predecessor's addresses)
            return fp_mix(h, fp_ty((Ty const*)(uintptr_t)words[1]));
        case KEY_INDEXED:
        case KEY_UNION_PAIR:
        case KEY_DEFINITELY_RELATED:
        case KEY_KEYOF_PROPERTIES:
        case KEY_NORMALIZED_INTERSECTION:
        case KEY_DISTRIBUTIVE_CONSTRAINT:
        case KEY_REDUCED_UNION:
        case KEY_SIMPLIFIED: {
            // (Type ids, then flag words: a word that is no id is taken as it is)
            for (u32 i = 1; i < count; i++) {
                Ty const* ty = words[i] > 1 ? key_type(c, words[i]) : nullptr;
                h = fp_mix(h, ty ? fp_ty(ty) : words[i]);
            }
            return h;
        }
        default:
            return 0;
    }
}

// The fingerprint of an intern value of a kind fp_key() knows
static u64 fp_value(u32 kind, void const* value) {
    switch (kind) {
        case KEY_CONDITIONAL_INSTANCE:
        case KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT: {
            Ty const* const* entry = value;
            return fp_mix(fp_ty(entry[0]), (uintptr_t)entry[1]);
        }
        case KEY_DEFINITELY_RELATED:
            return (uintptr_t)value;
        default:
            return fp_ty(value);
    }
}

