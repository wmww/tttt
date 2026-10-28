# The intern tables at 8 bytes a slot (2026-10-29, worker intern: built, step 1 below)

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
  - tag (high half) = the hash's low 32 bits (the ones table_index() reads) with bit 0 forced to 1 (occupied: 0 stays
    the empty slot). Positions and probe sequences are exactly the old ones (table_index() reads the top bits only),
    so every key lands in the slot it landed in before. The probe compares it as a u32 read of the slot's high half
    (one compare with memory).
  - Grow rehashes from the tag alone (sequential, no block read) for caps up to 2^31.
  - 31 - log2(cap) tag bits are independent of the position (11 at 1M slots): a false tag match (a wasted block
    read) on about 1 compared slot in 2000.
  - low half, bit 0 clear: twice a signed offset in words from `InternTable.base`, the table's first block (decoded
    by one lea, scale 4): blocks within ±8 GB of it, i.e. every block of a checker arena unless the process maps more
    than 8 GB between the two. Bit 0 set (INTERN_FAR): twice an index into `InternTable.far`, a vector of block
    pointers, never taken on any project here (`--time` prints it if it is), there so that correctness doesn't depend
    on the address space's layout; a `-DTTTT_INTERN_ALL_FAR` build sends every block there (the suite and zod/drizzle
    identical).
- **The count moves into the block**: the hash's top 16 bits held it, the tag can't. The first key word's top 16 bits
  carry the count (modulo 2^16) in the stored block; every key has a first word (its kind) below 2^48: `hash_words()`
  panics otherwise (no key of the suite or the corpora breaks it). A long key keeps its count word before word 0 (a
  short key's block[1] is >= 2^48, a long key's count is below 2^48: they can't be confused). The probe compares
  `words[0] | count << 48` first: kind and length in one compare, as exact as the old hash-then-words. Readers of a
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
1. The slot format above: built (wt-intern 9bee77cad, on d1dc1f48f's first draft). Output-identical: suite, quickgate
   --vs-main -k, expecttypes 100%, checkerdiff -k 8 with and without TTTT_SHARED_GRAPH=1 (the flag's drizzle "K8
   twice different" is residue C), sgcheck -k 4,8,16 on its default list (only drizzle and mikro-orm/seeder differ,
   the known residues, at main's rates: drizzle K8 def 2/10 against main's 3/10, seeder K16 rr 2/4 both), TSan clean
   on drizzle/effect/excalidraw/zod at T4/T8, the -DTTTT_GRAPH_CHECKS build reports the same one site as main's
   (kind 10 in instantiate_inner) on drizzle, zod, effect, excalidraw.
2. Probe order: not done. After step 1 the slot read (checker.c's tag compare) is still most of the probes' misses
   (store: 0.6M LL, 3.0M D1 against the block's 0.1M / 0.8M): random lines of a table far bigger than L2. A prefetch
   can't help a load that is needed at once; the lever left is fewer probes (the callers) or a smaller table (a higher
   load factor, which with doubling changes none of the census tables' capacities).
3. Key-block layout: not done, for the same reason (the block's misses are a sixth of the slot's).

## Results of step 1 (this box, against main ed02bc573, release builds, K1 `--checker-memory off`)
| | imm | store | zod | drizzle | effect |
|---|---|---|---|---|---|
| tables MB | 20 -> 10 | 48 -> 24 | 24.5 -> 12 | 36 -> 18 | 32 -> 16 |
| peak RSS MB (abbench, median of 7) | 265 -> 250 | 273 -> 235 | 386 -> 370 | 454 -> 420 | 717 -> 671 |
| minor faults | -6.0% | -14.6% | -4.2% | -7.7% | -6.7% |
| instructions (callgrind) | +0.6% | +1.9% | +0.65% | +0.86% | +0.56% |
| cachegrind D1 read misses | -1.3% | -6.7% | -1.9% | -1.8% | -1.0% |
| cachegrind LL read misses (all / without intern_grow's) | -28 / -16% | -32 / -22% | -15 / -3.5% | -19 / -11% | -12 / -3.8% |
| cachegrind stall estimate | -8.5% | -17.5% | -3.2% | -5.3% | -2.5% |
| wall K1, interleaved median of 7, load ~2 | -2.4% | -8.9% | -1.8% | -2.8% | -0.3% |
At the defaults (median of 7): drizzle wall -7.5%, RSS -11%; effect wall +0.4%, RSS -3.4%; excalidraw -3.4%, RSS -2.7%.
On store the slot line's LL read misses fell 1.10M -> 0.61M (-45%), its D1 misses 3.3M -> 3.0M: this box's LL is 32
MB and store's tables are now 24 MB; on the laptop (16 MB L3) expect less. The instructions are the probe's (the
hash_words check, the decode, the word-0 compare: about +3 a lookup) and intern_put's (+20 a put), less intern_grow
(-25%).

## For the laptop (the stop rule's numbers)
BASE `scripts/abbuild.sh ed02bc573 --fast`, NEW `scripts/fastbuild.sh` at the branch's head; footprint.md's protocol
(taskset -c 2, box idle, from an empty directory, TTTT_SYNC_EXIT=1), on imm, store, zod, drizzle, effect:

    perf stat -r 5 -x, -e cycles:u,instructions:u,L1-dcache-load-misses,ls_dmnd_fills_from_sys.local_ccx,ls_dmnd_fills_from_sys.dram_io_all,page-faults \
      taskset -c 2 env TTTT_SYNC_EXIT=1 $BIN --checkers 1 --threads 1 --checker-memory off --noEmit -p $PROJECT

and the attribution (`perf record -e L1-dcache-load-misses/period=5000/,ls_dmnd_fills_from_sys.local_ccx/period=500/,
ls_dmnd_fills_from_sys.dram_io_all/period=200/` on -g builds, bucketed as footprint.md did: intern = checker.c's
hash_words..intern_has). **Stop rule**: on store the intern-attributed L3 and DRAM fills fall by at least half and
K1 cycles by at least 2%; else step 1 is kept only if its cycles repay its +0.6-1.9% instructions on all five.
