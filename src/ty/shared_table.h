#pragma once

// The tables of a graph checkers share (plan 86 stage 4d, ty/graph.h): word-keyed blocks like InternTable's, and
// node links like LinkTable's. 64 shards by hash; reads take no lock (a shard's slot array is swapped whole when it
// grows and the old one is kept until the table is dropped; an entry is written block or value first and hash or key
// last, with release, and read with acquire); an insert locks its shard, looks again, and keeps what is there: the
// first insert of a key wins and every later one gets the winner back
#include "prelude.h"

typedef struct SharedTable SharedTable;
typedef struct SharedLinks SharedLinks;

SharedTable* shared_table_new(ZeroPages* pages); // Its slot arrays come from the pages, freed with them
void shared_table_drop(SharedTable* me);
// The block of the key (InternTable's layout: the value, a long key's word count, the words), null if none.
// `hash` is InternTable's (the count in its top 16 bits)
void const** shared_table_find(SharedTable const* me, u64 hash, u64 const* words, u32 count);
// Inserts the block (it stays where it is) unless the key is there already: the block that is there after
u64 shared_table_insert(SharedTable* me, u64 hash, void const** block, u32 count, void const*** out);
u64 shared_table_len(SharedTable const* me);
u32 shared_table_shard(u64 hash); // Which shard (lock) a key's insert takes: batches sort by it
u32 shared_links_shard(u64 key);

SharedLinks* shared_links_new(ZeroPages* pages);
void shared_links_drop(SharedLinks* me);
void const* shared_links_get(SharedLinks const* me, u64 key); // LinkTable's key (kind + 1 in the top byte, subject)
// Inserts unless the key is there: the value there after
void const* shared_links_insert(SharedLinks* me, u64 key, void const* value);
u64 shared_links_len(SharedLinks const* me);
// TTTT_GRAPH_STATS: inserts, and those that found their shard locked (added to)
void shared_table_contention(SharedTable const* me, u64* locked, u64* contended);
void shared_links_contention(SharedLinks const* me, u64* locked, u64* contended);
