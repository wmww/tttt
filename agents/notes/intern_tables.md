# The intern tables at 8 bytes a slot (2026-10-29, worker intern)

footprint.md's structural item 2. Owner code: checker.c's `hash_words`..`intern_has`, `intern_grow`, `intern_put`;
the readers of a table's entries (graph.c's publication, survivors.c, dupcensus.c, graph_debug.c); shared_table.c's
key compare.

## Today (main ed02bc573)
- `InternEntry` = { u64 hash (47 hash bits, bit 47 set, the key's word count in the top 16), void const** block }:
  16 bytes, 4 a line. Tables double at 5/8 load (so 30-62% full). The block is in the checker arena: the value, a long
  key's count (>= 0xffff words), the key words; with --shared-graph the full hash at block[-1].
- Position: `table_index()` = the top log2(cap) bits of the hash's low 32 (monotonic in the hash), linear probing.
- A lookup touches: the slot line (compare the 64-bit hash; a mismatch can't need the block), on a hash match the
  block's line(s) (compare the words), and the caller then reads block[0], the value (same line). Hit: slot + block,
  2 random lines (3 when the slot run or the block straddles a line). Miss: the slot lines of the run only.
- Bytes (`--time`, K1): imm types 1M + links 256k slots = 20 MB, store 1M + 2M = 48 MB, zod 24.5, drizzle 36 (incl.
  intersection props 256k), effect 32.
- Where the misses are (cachegrind, -g release, this box: D1 48 KB, LL 32 MB): store's probes, checker.c:233 (the
  slot's hash) 1.1M LL read misses and 3.3M D1, against :239 (the key block) 0.14M LL and 1.1M D1. imm: 0.43M LL / 1.5M
  D1 at the slot, 0.02M / 0.7M at the block. **The slot array is the lever**: the block usually sits beside the type
  allocated right after it (which the caller reads next) and is warm. intern_grow's misses are the model's
  (sequential), its real cost is faulting the new table (fe: 3-5% of the typecheck).

## The target
- **Slot = u64**: the high 32 bits a tag, the low 32 where the block is.
  - tag = the hash's low 32 bits (the ones table_index() reads) with bit 0 forced to 1 (occupied: 0 stays the empty
    slot) and bit 1 the "far" flag. Positions and probe sequences are exactly today's (table_index() reads the top
    bits only), so every key lands in the slot it lands in today.
  - Grow rehashes from the tag alone (sequential, no block read) for caps up to 2^30.
  - The probe compares the 30 tag bits, then the key. 30 - log2(cap) of them are independent of the position (10 at
    1M slots, 9 at 2M): a false tag match (a wasted block read) on about 1 compared slot in 1000.
  - near (far bit 0): a signed 32-bit offset in words from `InternTable.base`, the table's first block: blocks within
    ±16 GB of it, i.e. every block of a checker arena unless the process maps more than 16 GB between the two. far:
    an index into `InternTable.far`, a vector of block pointers (never taken in practice, there so that correctness
    doesn't depend on the address space's layout; one predictable branch).
- **The count moves into the block**: the hash's top 16 bits held it, the tag can't. The first key word's top 16 bits
  carry min(count, 0xffff) in the stored block (every kind's word 0 is below 2^48: `hash_words()` checks it, a panic
  otherwise); a count-0 key stores a placeholder word 0; a long key keeps its count word before word 0 (a short key's
  block[1] is >= 2^48 or 0, a long key's count is below 2^48: they can't be confused). The probe compares
  `words[0] | count << 48` first: kind and length in one compare, as exact as today's hash-then-words. Readers of a
  block's key (block_kind(), survivors, dupcensus, the fingerprints, intern_check_fields_done()'s rehash) mask it;
  intern_key_list()'s type lists start at word 1 or later and are untouched.
- Load factor unchanged (5/8): with doubling, 3/4 would change the capacity of none of the census tables.
- Not changed: InternMiss (hash, cap, index), the arena blocks and their addresses (callers keep slot pointers), the
  hit path's line count (slot + block); the miss path still reads only slots.

## --shared-graph
InternTable.shared/others, the SharedTable format (16-byte slots with the full hash, lock-free reads), publication
and the private-first memo kinds are unchanged: the private table maps the same keys to the same blocks, only its
encoding differs. What reads the private table's hash today reads it elsewhere: intern_publish() and the memo flush
already take block[-1]; publish_table()/publish_identities() (which walk the private table, including blocks put
before graph_new() linked the tables, which have no block[-1]) recompute it from the block's key
(`intern_block_hash()`). shared_table.c's key compare encodes the probe's word 0 the same way (`| count << 48`), so a
private probe and a shared one agree on what equals what. The private tables stay single-threaded (DEBUG_OWN_TABLE),
so nothing new is shared between threads: TSan's view is unchanged.

## Expected
- Bytes: the tables halve: imm 20 -> 10 MB, store 48 -> 24, zod 24.5 -> 12, drizzle 36 -> 18, effect 32 -> 16 (RSS
  K1 by about that; at the defaults per checker). intern_grow faults and copies half.
- Misses: the slot reads' LL/L3 misses fall where the halved array fits or nearly fits the last level (here store's
  24 MB < 32 MB LL: most of its 1.1M; the laptop's 16 MB L3: imm's and zod's 8 MB types table fits, store's 24 MB
  half does); D1 misses on slots stay about the same (random lines in a table far bigger than L1/L2; probe runs
  cross a line half as often). The block's misses don't move.
- So on store the intern share of L3 and DRAM fills (25% each on the laptop) may halve; its L1D share (25%) won't.
  Instructions: about +2 a probe (decode the slot), less the halved grow.

## Steps
1. The slot format above (output-identical; the gates of the brief).
2. Probe order: only if step 1's profile shows the run's second line or false tag matches; candidates are a prefetch
   of the block on a tag match before the key compare's branch, and comparing word 0 before the remaining words.
3. Key-block layout (no block straddling a line): the blocks' misses are a tenth of the slots' on store, so only if
   step 1 leaves them visible.
