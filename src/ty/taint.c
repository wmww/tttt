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

#include "ty/fingerprint.inc"

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

static U64Map clean_results; // (kind, key fingerprint) -> result fingerprint
static U64Map clean_owner; // the same key -> checker serial << 1 | 1
static U64Map descriptions; // the same key -> malloc'd description (TTTT_TAINT_DESCRIBE=1)

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
static thread_local bool simplified_writing; // taint_simplified_writing(): the next TK_SIMPLIFIED result's `writing`

void taint_simplified_writing(bool writing) {
    simplified_writing = writing;
}

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
            key = fp_ty(subject);
            value = fp_ty(result);
            break;
        case TK_SIMPLIFIED:
            // (Keyed by (type, writing): a writing target's simplification is the intersection of what a read's is the
            // union of, so the two would mismatch as one key)
            key = fp_mix(fp_ty(subject), simplified_writing);
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
            u64* slot = map_find(&prop_owners, (u64)(uintptr_t)members_prop(current_checker, members, i), true, true, &inserted);
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
    if (!c) return;
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
