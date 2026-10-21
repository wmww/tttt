// The program's atom table, see atom_table.h
#include "ty/atom_table.h"
#include <pthread.h>
#include <stdatomic.h>

typedef struct AtomSlot {
    _Atomic u64 hash; // 0: empty. Written last
    Atom const* _Atomic atom;
} AtomSlot;

typedef struct AtomSlots {
    u32 cap; // a power of two
    struct AtomSlots* older; // What it replaced: a reader may still probe it, so it lives until the table is dropped
    AtomSlot slot[];
} AtomSlots;

// The atoms' storage: blocks of a shard, never freed before the table
typedef struct AtomBlock {
    struct AtomBlock* next;
    size_t used, cap;
    alignas(8) char data[];
} AtomBlock;

enum { ATOM_SHARDS = 64, ATOM_BLOCK = 64 << 10 };

typedef struct AtomShard {
    AtomSlots* _Atomic slots;
    pthread_mutex_t lock;
    u32 len; // Under the lock
    AtomBlock* blocks;
    char pad[64];
} AtomShard;

struct AtomTable {
    AtomShard shards[ATOM_SHARDS];
};

static AtomSlots* slots_new(u32 cap, AtomSlots* older) {
    AtomSlots* ret = zeroed_table(1, sizeof(AtomSlots) + sizeof(AtomSlot) * cap);
    panic_if(!ret, "out of memory");
    ret->cap = cap;
    ret->older = older;
    return ret;
}

AtomTable* atom_table_new(void) {
    AtomTable* me = calloc(1, sizeof(AtomTable));
    panic_if(!me, "out of memory");
    for (u32 i = 0; i < ATOM_SHARDS; i++) {
        pthread_mutex_init(&my shards[i].lock, nullptr);
        atomic_init(&my shards[i].slots, slots_new(64, nullptr));
    }
    return me;
}

void atom_table_drop(AtomTable* me) {
    if (!me) return;
    for (u32 i = 0; i < ATOM_SHARDS; i++) {
        AtomShard* shard = &my shards[i];
        for (AtomSlots* s = atomic_load(&shard->slots); s;) {
            AtomSlots* older = s->older;
            free(s);
            s = older;
        }
        for (AtomBlock* b = shard->blocks; b;) {
            AtomBlock* next = b->next;
            free(b);
            b = next;
        }
        pthread_mutex_destroy(&shard->lock);
    }
    free(me);
}

u32 atom_table_len(AtomTable const* me) {
    u32 ret = 0;
    for (u32 i = 0; i < ATOM_SHARDS; i++) ret += my shards[i].len;
    return ret;
}

// (The shard is chosen by the hash's top bits, the slot by its low ones)
static u32 slot_index(u64 hash, u32 cap) {
    return (u32)hash & (cap - 1);
}

static Atom const* probe(AtomSlots const* slots, u64 hash, csview sv, u32* empty) {
    for (u32 i = slot_index(hash, slots->cap);; i = (i + 1) & (slots->cap - 1)) {
        u64 h = atomic_load_explicit(&slots->slot[i].hash, memory_order_acquire);
        if (!h) {
            *empty = i;
            return nullptr;
        }
        if (h != hash) continue;
        Atom const* atom = atomic_load_explicit(&slots->slot[i].atom, memory_order_relaxed);
        if (atom->len == (u32)sv.size && memcmp(atom->str, sv.buf, (size_t)sv.size) == 0) return atom;
    }
}

static Atom* atom_new(AtomShard* shard, csview sv) {
    size_t size = (sizeof(Atom) + (size_t)sv.size + 1 + 7) & ~(size_t)7;
    AtomBlock* block = shard->blocks;
    if (!block || block->used + size > block->cap) {
        size_t cap = size > ATOM_BLOCK ? size : ATOM_BLOCK;
        block = malloc(sizeof(AtomBlock) + cap);
        panic_if(!block, "out of memory");
        *block = (AtomBlock){ .next = shard->blocks, .cap = cap };
        shard->blocks = block;
    }
    Atom* atom = (Atom*)(block->data + block->used);
    block->used += size;
    atom->len = (u32)sv.size;
    memcpy(atom->str, sv.buf, (size_t)sv.size);
    atom->str[sv.size] = 0;
    return atom;
}

Atom const* atom_table_get(AtomTable* me, csview sv) {
    // c_hash_n() leaves similar strings (`ns1.Foo`, `ns2.Foo`) close together in the low bits, which linear probing
    // turns into long runs: finish it off (murmur3's finalizer)
    u64 hash = c_hash_n(sv.buf, sv.size);
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    if (!hash) hash = 1;
    AtomShard* shard = &my shards[hash >> 58];
    u32 empty;
    Atom const* found = probe(atomic_load_explicit(&shard->slots, memory_order_acquire), hash, sv, &empty);
    if (found) return found;
    pthread_mutex_lock(&shard->lock);
    AtomSlots* slots = atomic_load_explicit(&shard->slots, memory_order_relaxed);
    found = probe(slots, hash, sv, &empty);
    if (!found) {
        if ((shard->len + 1) * 4 > slots->cap * 3) {
            AtomSlots* bigger = slots_new(slots->cap * 2, slots);
            for (u32 i = 0; i < slots->cap; i++) {
                u64 h = atomic_load_explicit(&slots->slot[i].hash, memory_order_relaxed);
                if (!h) continue;
                u32 j = slot_index(h, bigger->cap);
                while (atomic_load_explicit(&bigger->slot[j].hash, memory_order_relaxed)) j = (j + 1) & (bigger->cap - 1);
                atomic_store_explicit(&bigger->slot[j].atom, atomic_load_explicit(&slots->slot[i].atom, memory_order_relaxed), memory_order_relaxed);
                atomic_store_explicit(&bigger->slot[j].hash, h, memory_order_relaxed);
            }
            atomic_store_explicit(&shard->slots, bigger, memory_order_release);
            slots = bigger;
            probe(slots, hash, sv, &empty);
        }
        Atom* atom = atom_new(shard, sv);
        atomic_store_explicit(&slots->slot[empty].atom, atom, memory_order_relaxed);
        atomic_store_explicit(&slots->slot[empty].hash, hash, memory_order_release);
        shard->len++;
        found = atom;
    }
    pthread_mutex_unlock(&shard->lock);
    return found;
}
