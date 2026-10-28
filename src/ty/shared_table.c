// The shared graph's tables, see shared_table.h
#include "ty/shared_table.h"
#include <pthread.h>
#include <stdatomic.h>

enum { SHARDS = 64, INTERN_LONG = 0xffffu };

// (The slot within a shard by the hash's lowest bits: a private table is walked in the order of its high bits, and
// filling a growing table in that order would cluster)
static u32 slot_of(u64 hash, u32 cap) { return (u32)hash & (cap - 1); }
static u32 shard_of(u64 hash) { return (u32)(hash >> 41) & (SHARDS - 1); }

typedef struct Slot {
    _Atomic u64 hash; // 0: empty. Written last
    void const** _Atomic block;
} Slot;

typedef struct Slots {
    u32 cap;
    struct Slots* older;
    Slot slot[];
} Slots;

// (sg2's layout experiment, plan "sharing_design": the slots pointer every lookup reads is on a line of its own, apart
// from the lock and the length every insert writes; with TTTT_GRAPH_PAD=1 each slot takes a cache line of its own too,
// so that no two inserts' slots share a line)
#ifdef TTTT_SHARD_PACKED
// (The layout before the experiment, for its A/B: the slots pointer beside the lock)
typedef struct Shard {
    Slots* _Atomic slots;
    pthread_mutex_t lock;
    u32 len;
    char pad[40];
} Shard;
#else
typedef struct Shard {
    alignas(64) Slots* _Atomic slots;
    alignas(64) pthread_mutex_t lock;
    u32 len;
} Shard;
#endif

// Slots apart (TTTT_GRAPH_PAD=1: 4, a 16-byte slot per 64-byte line; else 1), fixed at the first table made
static u32 slot_stride;
static u32 stride(void) {
    if (!slot_stride) slot_stride = getenv("TTTT_GRAPH_PAD") && strcmp(getenv("TTTT_GRAPH_PAD"), "0") ? 4 : 1;
    return slot_stride;
}

struct SharedTable {
    Shard shards[SHARDS];
    ZeroPages* pages;
};

// (Never freed alone: a reader may still be probing an older one, and every one goes with the owner's pages)
static Slots* slots_new(ZeroPages* pages, u32 cap, Slots* older, size_t slot_size) {
    Slots* ret = zero_pages_alloc(pages, sizeof(Slots) + slot_size * cap * stride());
    ret->cap = cap;
    ret->older = older;
    return ret;
}

static void shards_init(Shard* shards, ZeroPages* pages, size_t slot_size) {
    for (u32 i = 0; i < SHARDS; i++) {
        pthread_mutex_init(&shards[i].lock, nullptr);
        atomic_init(&shards[i].slots, slots_new(pages, 64, nullptr, slot_size));
    }
}

static void shards_drop(Shard* shards) {
    for (u32 i = 0; i < SHARDS; i++) pthread_mutex_destroy(&shards[i].lock);
}

static u64 shards_len(Shard const* shards) {
    u64 ret = 0;
    for (u32 i = 0; i < SHARDS; i++) ret += shards[i].len;
    return ret;
}

SharedTable* shared_table_new(ZeroPages* pages) {
    SharedTable* me = aligned_alloc(64, (sizeof(SharedTable) + 63) & ~(size_t)63);
    if (me) memset(me, 0, sizeof(SharedTable));
    panic_if(!me, "out of memory");
    my pages = pages;
    shards_init(my shards, pages, sizeof(Slot));
    return me;
}

void shared_table_drop(SharedTable* me) {
    if (!me) return;
    shards_drop(my shards);
    free(me);
}

u64 shared_table_len(SharedTable const* me) { return shards_len(my shards); }
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
    Shard const* shard = &my shards[shard_of(hash)];
    Slots* slots = atomic_load_explicit(&((Shard*)shard)->slots, memory_order_acquire);
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
    Shard* shard = &my shards[shard_of(hash)];
    u64 const* words = block_words(block, count);
    pthread_mutex_lock(&shard->lock);
    Slots* slots = atomic_load_explicit(&shard->slots, memory_order_relaxed);
    Slot* slot = probe(slots, hash, words, count);
    if (atomic_load_explicit(&slot->hash, memory_order_relaxed)) {
        *out = atomic_load_explicit(&slot->block, memory_order_relaxed);
        pthread_mutex_unlock(&shard->lock);
        return 0;
    }
    if ((shard->len + 1) * 4 > slots->cap * 3) {
        Slots* bigger = slots_new(my pages, slots->cap * 2, slots, sizeof(Slot));
        for (u32 i = 0; i < slots->cap; i++) {
            u64 h = atomic_load_explicit(&slots->slot[i * slot_stride].hash, memory_order_relaxed);
            if (!h) continue;
            u32 j = slot_of(h, bigger->cap);
            while (atomic_load_explicit(&bigger->slot[j * slot_stride].hash, memory_order_relaxed)) j = (j + 1) & (bigger->cap - 1);
            atomic_store_explicit(&bigger->slot[j * slot_stride].block, atomic_load_explicit(&slots->slot[i * slot_stride].block, memory_order_relaxed), memory_order_relaxed);
            atomic_store_explicit(&bigger->slot[j * slot_stride].hash, h, memory_order_relaxed);
        }
        atomic_store_explicit(&shard->slots, bigger, memory_order_release);
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
    Shard shards[SHARDS];
    ZeroPages* pages;
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
    SharedLinks* me = aligned_alloc(64, (sizeof(SharedLinks) + 63) & ~(size_t)63);
    if (me) memset(me, 0, sizeof(SharedLinks));
    panic_if(!me, "out of memory");
    my pages = pages;
    shards_init(my shards, pages, sizeof(LinkSlot));
    return me;
}

void shared_links_drop(SharedLinks* me) {
    if (!me) return;
    shards_drop(my shards);
    free(me);
}

u64 shared_links_len(SharedLinks const* me) { return shards_len(my shards); }
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
    Shard* shard = (Shard*)&my shards[shard_of(hash)];
    LinkSlots* slots = (LinkSlots*)atomic_load_explicit(&shard->slots, memory_order_acquire);
    for (u32 i = slot_of(hash, slots->cap);; i = (i + 1) & (slots->cap - 1)) {
        LinkSlot* slot = &slots->slot[i * slot_stride];
        u64 k = atomic_load_explicit(&slot->key, memory_order_acquire);
        if (!k) return nullptr;
        if (k == key) return atomic_load_explicit(&slot->value, memory_order_relaxed);
    }
}

void const* shared_links_insert(SharedLinks* me, u64 key, void const* value) {
    u64 hash = link_hash(key);
    Shard* shard = &my shards[shard_of(hash)];
    pthread_mutex_lock(&shard->lock);
    LinkSlots* slots = (LinkSlots*)atomic_load_explicit(&shard->slots, memory_order_relaxed);
    LinkSlot* slot = link_probe(slots, hash, key);
    if (atomic_load_explicit(&slot->key, memory_order_relaxed)) {
        void const* there = atomic_load_explicit(&slot->value, memory_order_relaxed);
        pthread_mutex_unlock(&shard->lock);
        return there;
    }
    if ((shard->len + 1) * 4 > slots->cap * 3) {
        LinkSlots* bigger = (LinkSlots*)slots_new(my pages, slots->cap * 2, (Slots*)slots, sizeof(LinkSlot));
        for (u32 i = 0; i < slots->cap; i++) {
            u64 k = atomic_load_explicit(&slots->slot[i * slot_stride].key, memory_order_relaxed);
            if (!k) continue;
            u32 j = slot_of(link_hash(k), bigger->cap);
            while (atomic_load_explicit(&bigger->slot[j * slot_stride].key, memory_order_relaxed)) j = (j + 1) & (bigger->cap - 1);
            atomic_store_explicit(&bigger->slot[j * slot_stride].value, atomic_load_explicit(&slots->slot[i * slot_stride].value, memory_order_relaxed), memory_order_relaxed);
            atomic_store_explicit(&bigger->slot[j * slot_stride].key, k, memory_order_relaxed);
        }
        atomic_store_explicit(&shard->slots, (Slots*)bigger, memory_order_release);
        slot = link_probe(bigger, hash, key);
    }
    atomic_store_explicit(&slot->value, value, memory_order_relaxed);
    atomic_store_explicit(&slot->key, key, memory_order_release);
    shard->len++;
    pthread_mutex_unlock(&shard->lock);
    return value;
}
