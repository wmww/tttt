// The shared graph's tables, see shared_table.h
#include "ty/shared_table.h"
#include <pthread.h>
#include <stdatomic.h>

enum { SHARDS = 256, INTERN_LONG = 0xffffu };

// (The slot within a shard by the hash's lowest bits: a private table is walked in the order of its high bits, and
// filling a growing table in that order would cluster)
static u32 slot_of(u64 hash, u32 cap) { return (u32)hash & (cap - 1); }
// (Bits 39-46 of InternTable's hash: 47 is always set, the count is above it)
static u32 shard_of(u64 hash) { return (u32)(hash >> 39) & (SHARDS - 1); }

typedef struct Slot {
    _Atomic u64 hash; // 0: empty. Written last
    void const** _Atomic block;
} Slot;

typedef struct Slots {
    u32 cap;
    struct Slots* older;
    Slot slot[];
} Slots;

// A shard's lock and length, a line of their own: every insert writes them. The slot array pointers every lookup reads
// are elsewhere, packed eight to a line (Shards.slots): 256 shards' fit in 2 KB a table, as 64 shards' did when each
// pointer had a line of its own (sg2's layout experiment, plan "sharing_design", kept the pointer off the lock's line).
// With TTTT_GRAPH_PAD=1 each slot takes a cache line of its own too, so that no two inserts' slots share a line
#ifdef TTTT_SHARD_PACKED
// (The layout before sg2's experiment, for its A/B: the slots pointer beside the lock)
typedef struct Shard {
    Slots* _Atomic slots;
    pthread_mutex_t lock;
    u32 len;
    u32 locked, contended;
    char pad[4];
} Shard;
#define SHARD_SLOTS(sh, i) (&(sh)->shards[i].slots)
typedef struct Shards {
    Shard shards[SHARDS];
    ZeroPages* pages;
} Shards;
#else
typedef struct Shard {
    alignas(64) pthread_mutex_t lock;
    u32 len;
    u32 locked, contended; // TTTT_GRAPH_STATS (wrapping past 4G: statistics)
} Shard;
#define SHARD_SLOTS(sh, i) (&(sh)->slots[i])
typedef struct Shards {
    Slots* _Atomic slots[SHARDS];
    Shard shards[SHARDS];
    ZeroPages* pages;
} Shards;
#endif

// Slots apart (TTTT_GRAPH_PAD=1: 4, a 16-byte slot per 64-byte line; else 1), fixed at the first table made
static u32 slot_stride;
static u32 stride(void) {
    if (!slot_stride) slot_stride = getenv("TTTT_GRAPH_PAD") && strcmp(getenv("TTTT_GRAPH_PAD"), "0") ? 4 : 1;
    return slot_stride;
}

struct SharedTable {
    Shards sh;
};

// (Never freed alone: a reader may still be probing an older one, and every one goes with the owner's pages)
static Slots* slots_new(ZeroPages* pages, u32 cap, Slots* older, size_t slot_size) {
    Slots* ret = zero_pages_alloc(pages, sizeof(Slots) + slot_size * cap * stride());
    ret->cap = cap;
    ret->older = older;
    return ret;
}

static void shards_init(Shards* sh, ZeroPages* pages, size_t slot_size) {
    sh->pages = pages;
    for (u32 i = 0; i < SHARDS; i++) {
        pthread_mutex_init(&sh->shards[i].lock, nullptr);
        atomic_init(SHARD_SLOTS(sh, i), slots_new(pages, 16, nullptr, slot_size));
    }
}

static void shards_drop(Shards* sh) {
    for (u32 i = 0; i < SHARDS; i++) pthread_mutex_destroy(&sh->shards[i].lock);
}

extern bool graph_stats_on; // (ty/graph.h)
// (TTTT_GRAPH_STATS: the shard's inserts and those that found its lock taken, counted under the lock, on its line)
static inline void shard_lock(Shard* shard) {
    bool contended = pthread_mutex_trylock(&shard->lock) != 0;
    if (contended) pthread_mutex_lock(&shard->lock);
    if (graph_stats_on) {
        shard->locked++;
        shard->contended += contended;
    }
}

static void shards_contention(Shards const* sh, u64* locked, u64* contended) {
    for (u32 i = 0; i < SHARDS; i++) *locked += sh->shards[i].locked, *contended += sh->shards[i].contended;
}

void shared_table_contention(SharedTable const* me, u64* locked, u64* contended) { shards_contention(&my sh, locked, contended); }

static u64 shards_len(Shards const* sh) {
    u64 ret = 0;
    for (u32 i = 0; i < SHARDS; i++) ret += sh->shards[i].len;
    return ret;
}

SharedTable* shared_table_new(ZeroPages* pages) {
    SharedTable* me = cache_aligned_alloc((sizeof(SharedTable) + 63) & ~(size_t)63);
    if (me) memset(me, 0, sizeof(SharedTable));
    panic_if(!me, "out of memory");
    shards_init(&my sh, pages, sizeof(Slot));
    return me;
}

void shared_table_drop(SharedTable* me) {
    if (!me) return;
    shards_drop(&my sh);
    cache_aligned_free(me);
}

u64 shared_table_len(SharedTable const* me) { return shards_len(&my sh); }
u32 shared_table_shard(u64 hash) { return shard_of(hash); }

// (A block keeps the key's count in the top 16 bits of its word 0, InternTable's layout: `words` may be a block's own
// words or the maker's)
static bool key_is(void const* const* block, u64 const* words, u32 count) {
    u64 const* key = (u64 const*)(block + 1);
    if (count >= INTERN_LONG && *key++ != count) return false;
    if (!count) return true;
    if (key[0] != (words[0] | (u64)count << 48)) return false;
    for (u32 k = 1; k < count; k++) if (key[k] != words[k]) return false;
    return true;
}

// The slot of the key, or the empty one where it would go
static Slot* probe(Slots* slots, u64 hash, u64 const* words, u32 count) {
    for (u32 i = slot_of(hash, slots->cap);; i = (i + 1) & (slots->cap - 1)) {
        Slot* slot = &slots->slot[i * slot_stride];
        u64 h = atomic_load_explicit(&slot->hash, memory_order_acquire);
        if (!h) return slot;
        if (h == hash && key_is(atomic_load_explicit(&slot->block, memory_order_relaxed), words, count)) return slot;
    }
}

// (What the probe saw decides: an empty slot it stopped at may take another key's entry the moment after, which a second
// look at the slot would return for this key)
void const** shared_table_find(SharedTable const* me, u64 hash, u64 const* words, u32 count) {
    Slots* slots = atomic_load_explicit(SHARD_SLOTS((Shards*)&my sh, shard_of(hash)), memory_order_acquire);
    for (u32 i = slot_of(hash, slots->cap);; i = (i + 1) & (slots->cap - 1)) {
        Slot* slot = &slots->slot[i * slot_stride];
        u64 h = atomic_load_explicit(&slot->hash, memory_order_acquire);
        if (!h) return nullptr;
        if (h != hash) continue;
        void const** block = atomic_load_explicit(&slot->block, memory_order_relaxed);
        if (key_is(block, words, count)) return block;
    }
}

static u64 const* block_words(void const** block, u32 count) {
    return (u64 const*)(block + 1) + (count >= INTERN_LONG);
}

u64 shared_table_insert(SharedTable* me, u64 hash, void const** block, u32 count, void const*** out) {
    u32 at = shard_of(hash);
    Shard* shard = &my sh.shards[at];
    Slots* _Atomic* ref = SHARD_SLOTS(&my sh, at);
    u64 const* words = block_words(block, count);
    shard_lock(shard);
    Slots* slots = atomic_load_explicit(ref, memory_order_relaxed);
    Slot* slot = probe(slots, hash, words, count);
    if (atomic_load_explicit(&slot->hash, memory_order_relaxed)) {
        *out = atomic_load_explicit(&slot->block, memory_order_relaxed);
        pthread_mutex_unlock(&shard->lock);
        return 0;
    }
    if ((shard->len + 1) * 4 > slots->cap * 3) {
        Slots* bigger = slots_new(my sh.pages, slots->cap * 2, slots, sizeof(Slot));
        for (u32 i = 0; i < slots->cap; i++) {
            u64 h = atomic_load_explicit(&slots->slot[i * slot_stride].hash, memory_order_relaxed);
            if (!h) continue;
            u32 j = slot_of(h, bigger->cap);
            while (atomic_load_explicit(&bigger->slot[j * slot_stride].hash, memory_order_relaxed)) j = (j + 1) & (bigger->cap - 1);
            atomic_store_explicit(&bigger->slot[j * slot_stride].block, atomic_load_explicit(&slots->slot[i * slot_stride].block, memory_order_relaxed), memory_order_relaxed);
            atomic_store_explicit(&bigger->slot[j * slot_stride].hash, h, memory_order_relaxed);
        }
        atomic_store_explicit(ref, bigger, memory_order_release);
        slot = probe(bigger, hash, words, count);
    }
    atomic_store_explicit(&slot->block, block, memory_order_relaxed);
    atomic_store_explicit(&slot->hash, hash, memory_order_release);
    shard->len++;
    pthread_mutex_unlock(&shard->lock);
    *out = block;
    return 1;
}

// Node links

typedef struct LinkSlot {
    _Atomic u64 key; // 0: empty. Written last
    void const* _Atomic value;
} LinkSlot;

typedef struct LinkSlots {
    u32 cap;
    struct LinkSlots* older;
    LinkSlot slot[];
} LinkSlots;

struct SharedLinks {
    Shards sh;
};

static u64 link_hash(u64 key) {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdull;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ull;
    key ^= key >> 33;
    return key;
}

SharedLinks* shared_links_new(ZeroPages* pages) {
    SharedLinks* me = cache_aligned_alloc((sizeof(SharedLinks) + 63) & ~(size_t)63);
    if (me) memset(me, 0, sizeof(SharedLinks));
    panic_if(!me, "out of memory");
    shards_init(&my sh, pages, sizeof(LinkSlot));
    return me;
}

void shared_links_drop(SharedLinks* me) {
    if (!me) return;
    shards_drop(&my sh);
    cache_aligned_free(me);
}

u64 shared_links_len(SharedLinks const* me) { return shards_len(&my sh); }
u32 shared_links_shard(u64 key) { return shard_of(link_hash(key)); }

static LinkSlot* link_probe(LinkSlots* slots, u64 hash, u64 key) {
    for (u32 i = slot_of(hash, slots->cap);; i = (i + 1) & (slots->cap - 1)) {
        LinkSlot* slot = &slots->slot[i * slot_stride];
        u64 k = atomic_load_explicit(&slot->key, memory_order_acquire);
        if (!k || k == key) return slot;
    }
}

// (As shared_table_find(): by what the probe saw, never a second look at the slot)
void const* shared_links_get(SharedLinks const* me, u64 key) {
    u64 hash = link_hash(key);
    LinkSlots* slots = (LinkSlots*)atomic_load_explicit(SHARD_SLOTS((Shards*)&my sh, shard_of(hash)), memory_order_acquire);
    for (u32 i = slot_of(hash, slots->cap);; i = (i + 1) & (slots->cap - 1)) {
        LinkSlot* slot = &slots->slot[i * slot_stride];
        u64 k = atomic_load_explicit(&slot->key, memory_order_acquire);
        if (!k) return nullptr;
        if (k == key) return atomic_load_explicit(&slot->value, memory_order_relaxed);
    }
}

void const* shared_links_insert(SharedLinks* me, u64 key, void const* value) {
    u64 hash = link_hash(key);
    u32 at = shard_of(hash);
    Shard* shard = &my sh.shards[at];
    Slots* _Atomic* ref = SHARD_SLOTS(&my sh, at);
    shard_lock(shard);
    LinkSlots* slots = (LinkSlots*)atomic_load_explicit(ref, memory_order_relaxed);
    LinkSlot* slot = link_probe(slots, hash, key);
    if (atomic_load_explicit(&slot->key, memory_order_relaxed)) {
        void const* there = atomic_load_explicit(&slot->value, memory_order_relaxed);
        pthread_mutex_unlock(&shard->lock);
        return there;
    }
    if ((shard->len + 1) * 4 > slots->cap * 3) {
        LinkSlots* bigger = (LinkSlots*)slots_new(my sh.pages, slots->cap * 2, (Slots*)slots, sizeof(LinkSlot));
        for (u32 i = 0; i < slots->cap; i++) {
            u64 k = atomic_load_explicit(&slots->slot[i * slot_stride].key, memory_order_relaxed);
            if (!k) continue;
            u32 j = slot_of(link_hash(k), bigger->cap);
            while (atomic_load_explicit(&bigger->slot[j * slot_stride].key, memory_order_relaxed)) j = (j + 1) & (bigger->cap - 1);
            atomic_store_explicit(&bigger->slot[j * slot_stride].value, atomic_load_explicit(&slots->slot[i * slot_stride].value, memory_order_relaxed), memory_order_relaxed);
            atomic_store_explicit(&bigger->slot[j * slot_stride].key, k, memory_order_relaxed);
        }
        atomic_store_explicit(ref, (Slots*)bigger, memory_order_release);
        slot = link_probe(bigger, hash, key);
    }
    atomic_store_explicit(&slot->value, value, memory_order_relaxed);
    atomic_store_explicit(&slot->key, key, memory_order_release);
    shard->len++;
    pthread_mutex_unlock(&shard->lock);
    return value;
}

void shared_links_contention(SharedLinks const* me, u64* locked, u64* contended) { shards_contention(&my sh, locked, contended); }
