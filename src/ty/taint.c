// Plan 86 stage 4-0's taint prototype and cleandiff, see taint.h (only with -DTTTT_TAINT, scripts/taintstats.sh)
#ifdef TTTT_TAINT
#include "ty/ty_internal.h"
#include "error.h"
#include "scope.h"
#include "binding.h"
#include "source_file.h"
#include <x86intrin.h>

// One thread (taintstats.sh runs --threads 1)
static u64 taint_count;
static u64 reason_counts[TAINT_REASON_COUNT];
static u64 report_count;
static bool busy; // Describing for cleandiff: the hooks stand still
static bool lenient; // TTTT_TAINT_LENIENT
static u32 checker_serial;
static bool describe_all; // TTTT_TAINT_DESCRIBE
static int describe_kind = -1; // TTTT_TAINT_DESCRIBE_KIND: only that kind
#define describe (describe_all && (describe_kind < 0 || (int)kind == describe_kind))

typedef struct KindStats {
    u64 entries, tainted, clean_reports, unmatched;
    u64 cycles_clean, cycles_tainted;
    u64 compared, mismatched, unfingerprinted;
    u32 examples;
} KindStats;
static KindStats stats[TK_COUNT];

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

static U64Map tainted; // intern blocks and (kind, object) keys of tainted results

static u64 object_key(unsigned kind, void const* object) {
    return (u64)(uintptr_t)object ^ (u64)kind << 48;
}

// A lookup that missed: the computation of its value starts there
typedef struct Miss {
    void const* table;
    u64 hash;
    TaintFrame frame;
} Miss;
#define MISS_RING 1024
static Miss misses[MISS_RING];
static u32 miss_next;

// Intern entries put whose value isn't there yet (or was put since the last sweep)
typedef struct Pending {
    void const** block;
    u32 kind;
    TaintFrame frame;
} Pending;
static Pending* pending;
static u32 pending_len, pending_cap;

static void const* const* inflight_slots[1024];
static u32 inflight_len;

// The cycles of the results done so far, each counted once (inclusive less what results inside it took): a frame's own
// time is its inclusive time less what was attributed meanwhile
static u64 attributed;

TaintFrame taint_begin(void) {
    return (TaintFrame){ .taint = taint_count, .start = __rdtsc(), .reports = report_count, .attributed = attributed };
}

static void sweep_full(void);
static void sweep(void);

static void taint_bump(TaintReason reason) {
    static int exact = -1;
    if (exact < 0) exact = getenv("TTTT_TAINT_EXACT") && *getenv("TTTT_TAINT_EXACT");
    if (exact) sweep_full();
    else sweep();
    reason_counts[reason]++;
    taint_count++;
}

// Events by site (file, line)
#define SITES 512
static struct { char const* file; int line; u32 reason; u64 count; } sites[SITES];

// The site of each recent event, by event number (taint_count): what tainted a result first
#define EVENT_LOG (1u << 22)
static uint16_t* event_log;
static u64 first_cause[TK_COUNT][SITES + 1]; // tainted results by the site of the first event in their window

void taint_event_at(TaintReason reason, char const* file, int line) {
    if (busy) return;
    if (lenient && reason == TAINT_PEEK) return;
    if (!event_log) event_log = calloc(EVENT_LOG, sizeof(uint16_t));
    u32 h = ((u32)(uintptr_t)file * 31u + (u32)line) % SITES;
    for (u32 n = 0; n < SITES; n++, h = (h + 1) % SITES) {
        if (!sites[h].file || (sites[h].file == file && sites[h].line == line)) {
            sites[h].file = file;
            sites[h].line = line;
            sites[h].reason = reason;
            sites[h].count++;
            break;
        }
    }
    event_log[(taint_count + 1) % EVENT_LOG] = (uint16_t)(h + 1);
    taint_bump(reason);
}

// A tainted result is read: the event's site is what tainted that result in the first place (its root cause)
static void taint_read_root(u32 site) {
    if (busy) return;
    if (!event_log) event_log = calloc(EVENT_LOG, sizeof(uint16_t));
    event_log[(taint_count + 1) % EVENT_LOG] = (uint16_t)site;
    taint_bump(TAINT_READ_TAINTED);
}

static char const* const reason_names[] = {
    "cycle", "in-flight-lazy", "in-flight-members", "in-flight-prop", "in-flight-slot", "in-flight-other", "limit",
    "current-node", "read-tainted", "end-condition", "peek",
};

void taint_event(TaintReason reason) {
    taint_event_at(reason, reason_names[reason], 0);
}

void taint_report(void) {
    if (!busy) report_count++;
}

// Cleandiff

static U64Map ty_fps; // Ty* -> fingerprint (1: in progress), this checker's
static U64Map clean_results; // (kind, key fingerprint) -> result fingerprint
static U64Map clean_owner; // the same key -> checker serial << 1 | 1
static U64Map descriptions; // the same key -> malloc'd description (TTTT_TAINT_DESCRIBE=1)

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
static cstr* trace;
static u32 trace_depth;

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
            if (state->is_this || (!ty->param.symbol && state->constraint_state == LAZY_STATE_DONE)) h = M(h, fp_ty(state->constraint));
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
            if (ty->conditional.outer) h = M(h, (u64)(uintptr_t)ty->conditional.outer->node.buf);
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
static Ty const* fp_stack[256];
static u32 fp_stack_len;
static u32 fp_min_backref = UINT32_MAX;

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
        u64* owner = map_find(&prop_owners, (u64)(uintptr_t)prop, false, true, nullptr);
        // (By its owner if it has one: whether its type is resolved yet by the time it is fingerprinted is history, and
        // the type is compared on its own, TK_PROP_TYPE)
        if (owner) h = M(h, *owner);
        else if (prop->type_state == LAZY_STATE_DONE) h = M(h, fp_ty(prop->type));
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
    TyMapper const* mapper = sig->mapper;
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
        h = M(h, sig->param_count | (u64)sig->min_args << 32);
        h = fp_list(h, sig->type_params);
        for (u32 i = 0; i < sig->param_count; i++) {
            h = M(h, fp_atom(sig->params[i].name));
            h = M(h, fp_ty(sig->params[i].type));
            h = M(h, sig->params[i].optional | sig->params[i].rest << 1);
        }
        if (sig->ret_state == LAZY_STATE_DONE) h = M(h, fp_ty(sig->ret));
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
static Ty const* key_type(Checker* c, u64 id) {
    if (!id || id >= c->next_type_id) return nullptr;
    return lazy_slots_type(&c->lazy, (u32)id);
}

// The fingerprint of an intern key of a kind cleandiff knows the words of (0 for another kind)
static u64 fp_key(Checker* c, u32 kind, u64 const* words, u32 count) {
    u64 h = fp_mix(43, kind);
    switch (kind) {
        case KEY_ANONYMOUS_INSTANCE: {
            Ty const* target = key_type(c, words[1]);
            if (!target) return 0;
            h = fp_mix(h, fp_ty(target));
            if (count >= 4 && words[2] == UINT64_MAX - 2) {
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

static cstr describe_ty(Checker* c, Ty const* ty) {
    return ty ? ty_to_cstr(c, ty) : cstr_from("(null)");
}

static bool print_types; // TTTT_TAINT_DESCRIBE=2: also print the types (perturbs: printing resolves what it prints)

static cstr describe_value(Checker* c, unsigned kind, void const* subject, void const* result) {
    if (!print_types) {
        if (kind == TK_PROP_TYPE) return cstr_d("prop ", atom_sv(((Prop const*)subject)->name));
        if (kind == TK_SYMBOL_DECLARED) return cstr_d("symbol ", ((Symbol const*)subject)->name);
        return cstr_d("kind ", kind);
    }
    busy = true;
    cstr ret;
    if (kind == TK_MEMBERS || kind == TK_DECLARED_RAW) {
        cstr s = describe_ty(c, subject);
        Members const* members = result;
        ret = cstr_d(cstr_sv(&s), " -> members(", members ? members->prop_count : 0, " props, ", members ? members->call_count : 0,
            " calls, ", members ? members->construct_count : 0, " constructs, ", members ? members->index_count : 0, " indexes)");
        cstr_drop(&s);
    } else if (kind < 160) {
        void const* const* block = subject;
        cstr r = kind == KEY_CONDITIONAL_INSTANCE || kind == KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT ?
            describe_ty(c, ((Ty const* const*)result)[0]) : kind == KEY_DEFINITELY_RELATED ? cstr_d((u64)(uintptr_t)result) :
            describe_ty(c, result);
        Ty const* first = key_type(c, ((u64 const*)block)[2]);
        cstr k = describe_ty(c, first);
        ret = cstr_d("key[1] ", cstr_sv(&k), " -> ", cstr_sv(&r));
        cstr_drop(&k);
        cstr_drop(&r);
    } else {
        cstr k = kind == TK_PROP_TYPE ? cstr_d("prop ", atom_sv(((Prop const*)subject)->name)) :
            kind == TK_SIGNATURE_RETURN ? cstr_from("signature") :
            kind == TK_PARAM_CONSTRAINT || kind == TK_BASE_CONSTRAINT || kind == TK_SIMPLIFIED ? describe_ty(c, subject) :
            kind == TK_SYMBOL_DECLARED ? cstr_d("symbol ", ((Symbol const*)subject)->name) : cstr_from("?");
        cstr r = kind == TK_VARIANCES ? cstr_from("variances") : describe_ty(c, result);
        ret = cstr_d(cstr_sv(&k), " -> ", cstr_sv(&r));
        cstr_drop(&k);
        cstr_drop(&r);
    }
    busy = false;
    return ret;
}

static u64 fp_value(u32 kind, void const* value);

// What the value's fingerprint is made of
static void const* key_subject;

static cstr trace_value(unsigned kind, void const* result) {
    cstr ret = cstr_init();
    trace = &ret;
    trace_depth = 0;
    u64 v = 0;
    if (kind == TK_MEMBERS || kind == TK_DECLARED_RAW || kind == TK_PARAM_CONSTRAINT || kind == TK_BASE_CONSTRAINT ||
        kind == TK_SIMPLIFIED || kind == TK_VARIANCES) {
        cstr_append_fmt(&ret, " KEY");
        fp_ty(key_subject);
        cstr_append_fmt(&ret, " VALUE");
    }
    if (kind == TK_MEMBERS || kind == TK_DECLARED_RAW) v = fp_members(result);
    else if (kind < 160) v = fp_value(kind, result);
    else if (kind != TK_VARIANCES) v = fp_ty(result);
    cstr_append_fmt(&ret, " VALUE=%llx", (unsigned long long)v);
    trace = nullptr;
    return ret;
}

// A clean result: what another history computed for its key must be the same
static void cleandiff(unsigned kind, void const* subject, void const* result) {
    Checker* c = current_checker;
    if (!c) return;
    key_subject = subject;
    u64 key = 0, value = 0;
    switch (kind) {
        case TK_MEMBERS:
        case TK_DECLARED_RAW:
            key = fp_ty(subject);
            value = fp_members(result);
            break;
        case TK_PROP_TYPE:
            key = fp_prop(subject, 0);
            value = fp_ty(result);
            break;
        case TK_SIGNATURE_RETURN:
            key = fp_signature(subject, 0);
            value = fp_ty(result);
            break;
        case TK_PARAM_CONSTRAINT:
        case TK_BASE_CONSTRAINT:
        case TK_SIMPLIFIED:
            key = fp_ty(subject);
            value = fp_ty(result);
            break;
        case TK_VARIANCES: {
            // (relate.c's Variances, which only it knows)
            struct { u32 len; unsigned char data[]; } const* v = result;
            key = fp_ty(subject);
            value = fp_bytes(v->data, v->len);
            break;
        }
        case TK_SYMBOL_DECLARED:
        case TK_FUNCTION_RETURN:
            key = (u64)(uintptr_t)subject;
            value = fp_ty(result);
            break;
        default:
            if (kind >= TK_LAZY_DECL && kind < TK_LAZY_EXPR) {
                key = (u64)(uintptr_t)subject;
                value = fp_ty(result);
            }
            break;
    }
    if (!key || ((kind == TK_MEMBERS || kind == TK_DECLARED_RAW) && !value)) {
        stats[kind].unfingerprinted++;
        return;
    }
    u64 slot_key = fp_mix(key, kind) | 1;
    bool inserted;
    u64* known = map_find(&clean_results, slot_key, true, true, &inserted);
    if (inserted) {
        *known = value;
        *map_find(&clean_owner, slot_key, true, true, nullptr) = (u64)checker_serial << 1 | 1;
        if (describe && (kind == TK_MEMBERS || kind == TK_DECLARED_RAW)) {
            U64Map saved = ty_fps;
            ty_fps = (U64Map){0};
            u64 fresh = fp_members(result);
            map_clear(&ty_fps);
            ty_fps = saved;
            static u32 stale;
            if (fresh != value && stale++ < 5) {
                cstr t = trace_value(kind, result);
                fprintf(stderr, "taint-stale members %llx fresh %llx: %s\n", (unsigned long long)value, (unsigned long long)fresh, cstr_str(&t));
                cstr_drop(&t);
            }
        }
        if (describe) {
            key_subject = subject;
            cstr t = trace_value(kind, result);
            cstr d = describe_value(c, kind, subject, result);
            cstr_append_fmt(&d, " ||%s", cstr_str(&t));
            cstr_drop(&t);
            *map_find(&descriptions, slot_key, true, true, nullptr) = (u64)(uintptr_t)strdup(cstr_str(&d));
            cstr_drop(&d);
        }
        return;
    }
    u64 owner = *map_find(&clean_owner, slot_key, true, true, nullptr) >> 1;
    if (owner == checker_serial) return; // (The same history computing it again: a recycled key)
    stats[kind].compared++;
    if (*known == value) return;
    stats[kind].mismatched++;
    if (stats[kind].examples++ < 8) {
        u64* first = describe ? map_find(&descriptions, slot_key, false, true, nullptr) : nullptr;
        if (describe) {
            U64Map saved = ty_fps;
            ty_fps = (U64Map){0};
            u64 fresh = kind == TK_MEMBERS || kind == TK_DECLARED_RAW ? fp_members(result) : kind == TK_PROP_TYPE ||
                kind == TK_SIGNATURE_RETURN || (kind >= TK_PARAM_CONSTRAINT && kind != TK_VARIANCES) ? fp_ty(result) : 0;
            map_clear(&ty_fps);
            ty_fps = saved;
            fprintf(stderr, "taint-fresh first %llx now %llx fresh %llx\n", (unsigned long long)*known, (unsigned long long)value,
                (unsigned long long)fresh);
        }
        key_subject = subject;
        cstr t = describe ? trace_value(kind, result) : cstr_init();
        cstr now = describe ? describe_value(c, kind, subject, result) : cstr_init();
        if (describe) cstr_append_fmt(&now, " ||%s", cstr_str(&t));
        cstr_drop(&t);
        eprintln("taint-mismatch kind ", kind, " checker ", owner, " vs ", checker_serial);
        eprintln("taint-A ", first && *first ? (char const*)(uintptr_t)*first : "?");
        eprintln("taint-B ", cstr_str(&now));
        cstr_drop(&now);
    }
}

// Intern entries keyed by fp_key(): fingerprinted with the key's words
static void cleandiff_intern(unsigned kind, void const** block, u32 count) {
    Checker* c = current_checker;
    if (!c) return;
    u64 key = fp_key(c, kind, (u64 const*)block + 1 + (count >= 0xffffu), count);
    if (!key) return;
    u64 value = fp_value(kind, block[0]);
    u64 slot_key = fp_mix(key, kind) | 1;
    bool inserted;
    u64* known = map_find(&clean_results, slot_key, true, true, &inserted);
    if (inserted) {
        *known = value;
        *map_find(&clean_owner, slot_key, true, true, nullptr) = (u64)checker_serial << 1 | 1;
        if (describe) {
            cstr t = trace_value(kind, block[0]);
            cstr d = describe_value(c, kind, block, block[0]);
            cstr_append_fmt(&d, " ||%s", cstr_str(&t));
            cstr_drop(&t);
            *map_find(&descriptions, slot_key, true, true, nullptr) = (u64)(uintptr_t)strdup(cstr_str(&d));
            cstr_drop(&d);
        }
        return;
    }
    u64 owner = *map_find(&clean_owner, slot_key, true, true, nullptr) >> 1;
    if (owner == checker_serial) return;
    stats[kind].compared++;
    if (*known == value) return;
    stats[kind].mismatched++;
    if (stats[kind].examples++ < 8) {
        u64* first = describe ? map_find(&descriptions, slot_key, false, true, nullptr) : nullptr;
        cstr t = describe ? trace_value(kind, block[0]) : cstr_init();
        cstr now = describe ? describe_value(c, kind, block, block[0]) : cstr_init();
        if (describe) cstr_append_fmt(&now, " ||%s", cstr_str(&t));
        cstr_drop(&t);
        eprintln("taint-mismatch kind ", kind, " checker ", owner, " vs ", checker_serial);
        eprintln("taint-A ", first && *first ? (char const*)(uintptr_t)*first : "?");
        eprintln("taint-B ", cstr_str(&now));
        cstr_drop(&now);
    }
}

// A result is done: clean or tainted
static void complete(unsigned kind, TaintFrame frame, u64 key, void const* subject, void const* result, u32 key_count) {
    u32 root_site = 0;
    bool is_tainted = taint_count != frame.taint;
    Checker* c = current_checker;
    // (Lenient: flow_uncached only for what evaluates expressions; types are flow-isolated where they read one)
    // (Declarations other than variables are flow-isolated while they evaluate: what flow_uncached is after doesn't
    // say anything about them)
    // Lenient: only expressions. Types read expressions only through declarations, and the variables that a loop in
    // progress elsewhere first asks for are module-level ones (drizzle's `export const TableName = Symbol.for(...)`):
    // their initializers aren't in the loop. Cleandiff says whether that holds
    bool flow_matters = kind == TK_LAZY_EXPR ||
        (!lenient && (kind < TK_LAZY_DECL || kind == TK_LAZY_DECL + LAZY_TYPE_INITIALIZED_BINDING));
    if (!is_tainted && c && ((flow_matters && c->flow_uncached) || c->errs->phase != ERROR_PHASE_TYPECHECK)) {
        is_tainted = true;
        taint_event_at(TAINT_END_CONDITION, c->errs->phase != ERROR_PHASE_TYPECHECK ? "end-phase" : "end-flow-uncached", 0);
        if (getenv("TTTT_TAINT_SHOW_END") && kind == TK_LAZY_DECL + LAZY_TYPE_INITIALIZED_BINDING) {
            Span span = c_container_of((LazyType*)subject, InitializedBinding, lazy_type)->binding.content_span;
            eprintln("taint-end-binding ", span_source(span)->canonical_path, ":", span_line_index(span) + 1, " ", span);
        }
    }
    if ((kind == TK_MEMBERS || kind == TK_DECLARED_RAW) && result) {
        Members const* members = result;
        u64 owner = 0;
        for (u32 i = 0; i < members->prop_count; i++) {
            bool inserted;
            u64* slot = map_find(&prop_owners, (u64)(uintptr_t)members->props[i], true, true, &inserted);
            if (!inserted) continue;
            if (!owner) owner = fp_ty(subject);
            *slot = fp_mix(owner, i) | 1;
        }
    }
    KindStats* s = &stats[kind];
    s->entries++;
    u64 inclusive = __rdtsc() - frame.start;
    u64 inner = attributed - frame.attributed;
    u64 cycles = inclusive > inner ? inclusive - inner : 0;
    attributed += cycles;
    if (is_tainted) {
        s->tainted++;
        u32 site = event_log && taint_count - frame.taint < EVENT_LOG ? event_log[(frame.taint + 1) % EVENT_LOG] : 0;
        first_cause[kind][site] += cycles;
        root_site = site;
        s->cycles_tainted += cycles;
        *map_find(&tainted, key, true, true, nullptr) = root_site + 1;
        return;
    }
    s->cycles_clean += cycles;
    // (Computed again, cleanly: a value written over a tainted one, an expression evaluated uncached before)
    if (tainted.len) {
        u64* stale = map_find(&tainted, key, false, true, nullptr);
        if (stale) *stale = 0;
    }
    if (report_count != frame.reports) s->clean_reports++;
    if (kind < 160) cleandiff_intern(kind, (void const**)subject, key_count);
    else cleandiff(kind, subject, result);
}

// The word count of a key, from its block (checker.c: the value, a long key's count, the words). Pending entries
// keep theirs
static u32* pending_counts;

static bool sweeping;
static u32 sweeps_since_full;

// The pending entries by block (linear probing, deletion by backward shift): a hit on one completes it first
typedef struct { void const* const* block; u32 index; } PendingSlot;
static PendingSlot* pending_index;
static u32 pending_index_cap; // a power of two, at least 4x pending_cap

static u32 pending_hash(void const* const* block) {
    return (u32)mix64((u64)(uintptr_t)block) & (pending_index_cap - 1);
}

static PendingSlot* pending_slot(void const* const* block) {
    if (!pending_index_cap) return nullptr;
    for (u32 i = pending_hash(block);; i = (i + 1) & (pending_index_cap - 1)) {
        if (pending_index[i].block == block) return &pending_index[i];
        if (!pending_index[i].block) return nullptr;
    }
}

static void pending_index_put(void const* const* block, u32 index) {
    u32 i = pending_hash(block);
    while (pending_index[i].block && pending_index[i].block != block) i = (i + 1) & (pending_index_cap - 1);
    pending_index[i] = (PendingSlot){ block, index };
}

static void pending_index_remove(void const* const* block) {
    PendingSlot* slot = pending_slot(block);
    if (!slot) return;
    u32 mask = pending_index_cap - 1;
    u32 i = (u32)(slot - pending_index);
    for (u32 j = (i + 1) & mask; pending_index[j].block; j = (j + 1) & mask) {
        u32 home = pending_hash(pending_index[j].block);
        // (j's entry can move to i if its home isn't cyclically in (i, j])
        if (((j - home) & mask) >= ((j - i) & mask)) {
            pending_index[i] = pending_index[j];
            i = j;
        }
    }
    pending_index[i] = (PendingSlot){0};
}

static void pending_index_rebuild(void) {
    free(pending_index);
    pending_index_cap = 1;
    while (pending_index_cap < pending_cap * 4) pending_index_cap *= 2;
    pending_index = calloc(pending_index_cap, sizeof(PendingSlot));
    for (u32 i = 0; i < pending_len; i++) pending_index_put(pending[i].block, i);
}

static void sweep_at(u32 i) {
    Pending done = pending[i];
    u32 count = pending_counts[i];
    pending_index_remove(done.block);
    pending[i] = pending[pending_len - 1];
    pending_counts[i] = pending_counts[pending_len - 1];
    pending_len--;
    if (i < pending_len) pending_slot(pending[i].block)->index = i;
    complete(done.kind, done.frame, (u64)(uintptr_t)done.block, done.block, done.block[0], count);
}

// Completes the pending entries that have their value now. Values mostly come in the order their keys were put, last
// first, so each hook looks at the newest few, and at all of them once per pending_len hooks (amortized constant: a
// full scan at every hook made vscode quadratic) and before every taint event (so no entry done before an event is
// tainted by it)
static void sweep_full(void) {
    if (sweeping) return;
    sweeping = true;
    sweeps_since_full = 0;
    for (u32 i = 0; i < pending_len;) {
        if (!pending[i].block[0]) i++;
        else sweep_at(i);
    }
    sweeping = false;
}

static void sweep(void) {
    if (sweeping || !pending_len) return;
    if (getenv("TTTT_TAINT_FULL") || ++sweeps_since_full >= pending_len) {
        sweep_full();
        return;
    }
    sweeping = true;
    for (u32 n = 0; n < 8 && pending_len && pending[pending_len - 1].block[0]; n++) sweep_at(pending_len - 1);
    sweeping = false;
}

void taint_flow_change(void) {
    if (!busy) sweep_full();
}

void taint_end(TaintFrame frame, unsigned kind, void const* object, void const* subject, void const* result) {
    if (busy) return;
    sweep();
    // (ty_simplified()'s object is its intern entry, which the intern hooks look up)
    complete(kind, frame, kind == TK_SIMPLIFIED ? (u64)(uintptr_t)object : object_key(kind, object), subject, result, 0);
}

void taint_read(unsigned kind, void const* object) {
    if (busy || !tainted.len) return;
    u64* root = map_find(&tainted, object_key(kind, object), false, true, nullptr);
    if (root && *root) taint_read_root((u32)*root - 1);
}

void taint_slot_begin(void const* const* block) {
    if (inflight_len < c_arraylen(inflight_slots)) inflight_slots[inflight_len++] = block;
}

void taint_slot_end(void const* const* block) {
    for (u32 i = inflight_len; i--;) {
        if (inflight_slots[i] == block) {
            memmove(&inflight_slots[i], &inflight_slots[i + 1], sizeof(inflight_slots[0]) * (inflight_len - i - 1));
            inflight_len--;
            return;
        }
    }
}

void taint_intern_miss(void const* table, u64 hash) {
    if (busy) return;
    misses[miss_next++ % MISS_RING] = (Miss){ .table = table, .hash = hash, .frame = taint_begin() };
}

// Kinds whose values are written before they are done (a placeholder, an in-flight record): instrumented explicitly
static bool explicit_kind(unsigned kind) {
    switch (kind) {
        case KEY_SIMPLIFIED: case KEY_VARIANCES: case KEY_ALIAS_VARIANCES: case KEY_FUNCTION_TYPE_MENTIONS:
        case KEY_TYPE_VARIABLE_REDUCTION: case KEY_BINDING_COMPUTED_KEY: case KEY_FLOW_EXHAUSTIVE:
            return true;
        default:
            return false;
    }
}

void taint_intern_put(void const* table, u64 hash, void const** block, unsigned kind) {
    if (busy) return;
    sweep();
    Checker* c = current_checker;
    if (!c || table == &c->atoms) return;
    if (table == &c->intersection_props) kind = 254;
    if (explicit_kind(kind)) return;
    TaintFrame frame;
    bool matched = false;
    for (u32 i = 0; i < MISS_RING; i++) {
        Miss* m = &misses[(miss_next - 1 - i) % MISS_RING];
        if (m->hash == hash && m->table == table) {
            frame = m->frame;
            m->hash = 0;
            matched = true;
            break;
        }
    }
    if (!matched) {
        frame = taint_begin();
        stats[kind].unmatched++;
    }
    if (pending_len == pending_cap) {
        pending_cap = pending_cap ? pending_cap * 2 : 256;
        if (getenv("TTTT_TAINT_PENDING")) {
            u32 by_kind[256] = {0};
            for (u32 i = 0; i < pending_len; i++) by_kind[pending[i].kind & 255]++;
            for (u32 k = 0; k < 256; k++) if (by_kind[k] > pending_len / 10) eprintln("taint-pending-grow ", pending_cap, " kind ", k, " ", by_kind[k]);
        }
        pending = realloc(pending, sizeof(Pending) * pending_cap);
        pending_counts = realloc(pending_counts, sizeof(u32) * pending_cap);
    }
    if (pending_index_cap < pending_cap * 4) pending_index_rebuild();
    pending_counts[pending_len] = (u32)(hash >> 48) == 0xffffu ? (u32)(uintptr_t)block[1] : (u32)(hash >> 48);
    pending_index_put(block, pending_len);
    pending[pending_len++] = (Pending){ .block = block, .kind = kind, .frame = frame };
}

void taint_intern_hit(void const* const* block) {
    if (busy) return;
    sweep();
    for (u32 i = inflight_len; i--;) {
        if (inflight_slots[i] == block) {
            taint_event(TAINT_IN_FLIGHT_SLOT);
            return;
        }
    }
    PendingSlot* slot = pending_len ? pending_slot(block) : nullptr;
    if (slot) {
        if (!block[0]) {
            taint_event(TAINT_IN_FLIGHT_SLOT);
            return;
        }
        // (Done since the last sweep: completed now, so whether it was tainted is known)
        if (!sweeping) {
            sweeping = true;
            sweep_at(slot->index);
            sweeping = false;
        }
    }
    u64* tainted_kind = tainted.len ? map_find(&tainted, (u64)(uintptr_t)block, false, true, nullptr) : nullptr;
    if (tainted_kind && *tainted_kind) taint_read_root((u32)*tainted_kind - 1);
}

static u64 first_tsc;

void taint_checker_new(void* checker) {
    (void)checker;
    if (!first_tsc) first_tsc = __rdtsc();
    checker_serial++;
    map_clear(&ty_fps);
    map_clear(&tainted);
    map_clear(&prop_owners);
    pending_len = 0;
    if (pending_index) memset(pending_index, 0, sizeof(PendingSlot) * pending_index_cap);
    inflight_len = 0;
    memset(misses, 0, sizeof(misses));
    lenient = getenv("TTTT_TAINT_LENIENT") && *getenv("TTTT_TAINT_LENIENT");
    char const* env = getenv("TTTT_TAINT_DESCRIBE");
    describe_all = env && *env;
    print_types = describe_all && *env == '2';
    env = getenv("TTTT_TAINT_DESCRIBE_KIND");
    describe_kind = env && *env ? atoi(env) : -1;
    env = getenv("TTTT_TAINT_FROM_TYPE_NODE");
    from_type_node_fp = env && *env;
}

void taint_checker_drop(void* checker) {
    (void)checker;
    if (busy) return;
    // (What is still pending never got a value)
    pending_len = 0;
    if (pending_index) memset(pending_index, 0, sizeof(PendingSlot) * pending_index_cap);
    map_clear(&ty_fps);
    map_clear(&tainted);
    map_clear(&prop_owners);
}

static char const* kind_name(unsigned kind) {
    static char const* const explicit_names[] = {
        "TK_MEMBERS", "TK_DECLARED_RAW", "TK_PROP_TYPE", "TK_SIGNATURE_RETURN", "TK_PARAM_CONSTRAINT", "TK_SYMBOL_DECLARED",
        "TK_FUNCTION_RETURN", "TK_BASE_CONSTRAINT", "TK_SIMPLIFIED", "TK_VARIANCES", "TK_FREE_PARAMS",
    };
    static char buf[32];
    if (kind >= TK_MEMBERS && kind < TK_MEMBERS + c_arraylen(explicit_names)) return explicit_names[kind - TK_MEMBERS];
    if (kind >= TK_LAZY_DECL && kind < TK_LAZY_EXPR) {
        snprintf(buf, sizeof buf, "TK_LAZY_DECL+%u", kind - TK_LAZY_DECL);
        return buf;
    }
    if (kind == TK_LAZY_EXPR) return "TK_LAZY_EXPR";
    if (kind == 254) return "INTERSECTION_PROPS";
    snprintf(buf, sizeof buf, "key-%u", kind);
    return buf;
}

void taint_print(void) {
    sweep_full();
    if (getenv("TTTT_TAINT_PENDING")) {
        u32 by_kind[256] = {0};
        for (u32 i = 0; i < pending_len; i++) by_kind[pending[i].kind & 255]++;
        for (u32 k = 0; k < 256; k++) if (by_kind[k]) eprintln("taint-pending ", k, " ", by_kind[k]);
    }
    static char const* const reasons[] = {
        "cycle", "in-flight-lazy", "in-flight-members", "in-flight-prop", "in-flight-slot", "in-flight-other", "limit",
        "current-node", "read-tainted", "end-condition", "peek",
    };
    for (u32 i = 0; i < TAINT_REASON_COUNT; i++) eprintln("taint-reason ", (char const*)reasons[i], " ", reason_counts[i]);
    for (u32 i = 0; i < SITES; i++) {
        if (!sites[i].file) continue;
        char const* slash = strrchr(sites[i].file, '/');
        fprintf(stderr, "taint-site %s:%d %s %llu\n", slash ? slash + 1 : sites[i].file, sites[i].line,
            reasons[sites[i].reason], (unsigned long long)sites[i].count);
    }
    for (u32 kind = 0; kind < TK_COUNT; kind++) {
        KindStats* s = &stats[kind];
        if (!s->entries) continue;
        fprintf(stderr, "taint-kind %u %s %llu %llu %llu %llu %llu %llu %llu %llu %llu\n", kind, kind_name(kind),
            (unsigned long long)s->entries, (unsigned long long)s->tainted, (unsigned long long)s->cycles_clean,
            (unsigned long long)s->cycles_tainted, (unsigned long long)s->clean_reports, (unsigned long long)s->unmatched,
            (unsigned long long)s->compared, (unsigned long long)s->mismatched, (unsigned long long)s->unfingerprinted);
    }
    for (u32 kind = 0; kind < TK_COUNT; kind++) {
        for (u32 site = 0; site <= SITES; site++) {
            if (!first_cause[kind][site]) continue;
            char const* file = site ? sites[site - 1].file : "(end condition or untracked)";
            char const* slash = strrchr(file, '/');
            fprintf(stderr, "taint-cause %u %s:%d %llu\n", kind, slash ? slash + 1 : file, site ? sites[site - 1].line : 0,
                (unsigned long long)first_cause[kind][site]);
        }
    }
    eprintln("taint-checkers ", checker_serial);
    fprintf(stderr, "taint-total %llu %llu\n", (unsigned long long)(__rdtsc() - first_tsc), (unsigned long long)attributed);
}

#else
// (Nothing without TTTT_TAINT: ISO C wants something in a translation unit)
typedef int taint_unused;
#endif
