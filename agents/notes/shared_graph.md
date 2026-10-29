# The shared type graph (the default mode at K > 1 since 2026-10-29)
One type graph shared by every checker on real threads (`src/ty/graph.{h,c}`, `overlay.{h,c}`, `shared_table.c`,
`taint.h`; documentation.md "The shared graph"; the oracles in `scripts/sharedgraph/README.md`, the gate
`scripts/sgcheck.py`). The default whenever more than one checker runs (vision.md "Decisions", the human's call on
their laptop's numbers); one checker never has one; `TTTT_SHARED_GRAPH=0` is plain (each checker its own graph), the
oracles' reference. Built 2026-10-20..22, parked 2026-10-21/22 as too expensive per instruction, made byte-identical
and option-grade 2026-10-29 (walk order, variance frames). The full design log (2240 lines, every stage, the census of
lazily written fields, bun's design read from its source) is git history:
`git show af936072c:agents/plans/86_multiples.md` and `git show af936072c:agents/plans/86_stage4_census.md`.

## Why it was built
At the defaults on 12 cores each checker re-derives the instantiations of the project's own generics (vscode ~1.2x
duplicated work; drizzle 1.4x serialized, 4x per-file time at K12), and recycling adds 5-8%. A shared declared layer
(lib and `.d.ts` declarations only) was measured first and rejected: those are 2-6% of the re-derived work
(measurements.md "What the checkers re-derive"). Only a graph holding instantiations could remove it, and it was also
the one lever on memory at the defaults. bun canary (oven-sh/bun#45800) shares its whole graph and keeps ~20 of 24
threads busy at 2.2 GB on vscode: the existence proof that sharing can pay.

## The design that was built ("by construction", 2026-10-22)
- **Write-once shared words.** Every lazily computed value reachable from a shared object (members, raw members, prop
  types, signature resolution and return, predicates, constraints, defaults, mapped templates, conditional branches,
  declared types, reference args, inferred returns, declaration memos, variance slots: 18 sites plus memos) is one
  `SharedWord`: null -> clean value | NONE | POISON, by CAS with release, read with acquire. Nothing else on a shared
  object is written after publication.
- **In-flight state lives only in the computing worker** (a ClaimSet keyed by the word's address, a private Overlay
  for unpublished values). No worker reads, waits for or learns of another's in-flight state, so no wait cycle and no
  redo exist.
- At a computation's end: clean -> CAS the value (a loser adopts the winner's, its own objects being unreferenced);
  tainted -> CAS POISON and keep the value privately. A POISONed key is computed privately by each worker that needs
  it (the default path's semantics). Intern-table memo kinds keep a private-first table, so a tainted entry just
  stays private.
- **"Clean"**, precisely (one per-worker counter, frames compare it): read nothing in flight to this worker (own
  cycle, early members, an enclosing variance claim, TY_CIRCULAR); read no private value (every Overlay hit is an
  event); moved no limit counter and met no deeply-nested cut or depth limit in a relation; reported nothing at the
  current node; `flow_uncached == 0` for expression kinds; ran in the typecheck phase. A done-ness peek that selects
  between results counts as a read.
- **Identities are published whatever their taint, so what they are made of must not be private**: an identity
  built from a record this worker resolved privately (a signature's type parameters, its clones) is this worker's
  only (KEY_SIGNATURE_INSTANCE_OWN, bugs/shared_graph.md: effect's and folo's residues, 2026-10-29). Any other
  identity whose fields come from a resolved record has the same shape.
- **Why deterministic**: a clean value is a function of published clean values and hash-consed identities, so any
  worker computing that key cleanly computes the same value. A worker's view is a history a sequential checker could
  have had (the class recycling, stealing and K already produce).
- **Shadowed words**: a worker whose tainted value lost to another's clean one keeps reading its own (Overlay first)
  until its file ends, then hands over to a fresh checker on the same graph (`graph_fresh_worker()`).
- **Variances** (per frame since 2026-10-29, "Variance frames" below): each measurement is a frame, clean when every
  taint event in it read state made inside its own span (a claim of a frame at or inside it, values measured inside it
  or clean ones, a CACHE_DEEP pair concluded inside it); a clean frame publishes its slot (a nested one at its own end),
  over an empty or a poisoned slot; equal results adopt, differing ones shadow; a tainted frame poisons an empty slot.
  (Before: a group, the subjects of one outermost measurement, published all or nothing.)
- **Rejected**: (A) claimed-and-waited fields with tainted values reset: waiting creates the wait cycles a redo must
  break, and a breaker's history is no sequential checker's; this was built (4d/4e: owner-tagged states, a wait-for
  graph, a per-file redo, a global variance lock costing drizzle +54% wall) and deleted. (B) immutable types with
  per-worker lazy tables published at quiescent points: a table probe on every lazy read costs 4-6x the words' fast
  path (+2-3% on drizzle and zod for two kinds alone), and per-file publication lets a worker's own objects escape
  into back-pointers (instantiate_members' `target`, the private/protected root-prop comparisons) before it learns
  it lost.
- Earlier gates that still describe the code: union order's type-id fallback decides 0.003-0.6% of ties and is not
  observable under seeded permutation once spread alternatives got a structural key (`TTTT_ORDER_STATS`,
  `TTTT_ORDER_SEED`); is_deeply_nested_as()'s "newer id" test goes by the walk order under the flag (sharing_design.md §6; first-touch
  stamps until b2b3cdcd2); the taint
  prototype (G0) found members and prop types 99.7-100% clean, intern memo kinds tainted 0.01-0.14%.

## Where it ended (2026-10-22)
Numbers: measurements.md "Shared graph end state", "Shared graph K1 overhead", "Shared graph gate G2", "Oracle totals
with and without the shared graph".
- Default path: unchanged output, no measurable instruction cost (state bytes beside each word, < 0.1%).
- With the flag at K12: 20/20 run tables on drizzle, zod, effect, excalidraw, nest; 300 loaded drizzle runs identical;
  effect ~1% of loaded runs differ; TSan clean on 14 corpora; kdiff 8 / 3814 against plain's 3; K1 cost +4%
  instructions; work K12/K1 drizzle 1.305, zod 1.285, effect 1.134.
- **Residues today: agents/bugs/shared_graph.md** (2026-10-29: auth-api's private-name race, the missing one-checker
  rerun and the TSan groups fixed; drizzle's C was the first-touch stamps, closed by the walk order at b2b3cdcd2; effect and folo's renderer fixed), gated by
  `scripts/sgcheck.py`. The rest of this bullet is the 2026-10-22 state.
- **Open residues** (the release criterion wanted them closed and plain/flagged oracles identical): **C**, conditional
  and alias instances published as identities whatever their taint while carrying resolved state (mikro-orm x3, the
  drizzle seeds' TS2322 at singlestore-core/query-builders/query.ts(139,3)); proposed fix: the instance object an
  identity published at creation, its resolution a word published only when clean. **V**, variance groups' side state
  (results computed at variance_depth > 0, marker instances) an adopter lacks; no project shows it now. **W**,
  trpc/examples/nuxt and folo's renderer (mechanism open). Effect's Schema.tst.ts(2936,39) TS2345 cascade (~1% loaded,
  0-15% at load 40+, bimodal fingerprint, the C+V family). Each converted site had exposed a class of per-worker state
  the taint counter couldn't see; the fix each time was tsc's shape (derive from the node, not from history).
- The laptop bench (measurements.md "The 12-core box at 5f2cd699"): no wall win at the defaults; vue 2x slower
  (contention, K1 unchanged), type-fest, effect, valibot slower; drizzle and excalidraw faster; memory vscode 1581 ->
  2924 MB at K12.

## The perf accounting (2026-10-21, 16-vCPU box, callgrind; why it's parked)
Work in G instructions (typecheck and worker threads):

| corpus | plain K1 | flag K1 | plain K12 | flag K12 T1 | flag K12 T12 | flag K12 / flag K1 |
|---|---|---|---|---|---|---|
| drizzle | 4.88 | 5.15 | 10.54 | 6.58 | 6.64 | 1.29 |
| zod | 4.13 | 4.36 | 6.34 | 5.57 | 5.64 | 1.29 |
| effect | 7.86 | 8.20 | 9.02 | 9.44 | 9.32 | 1.14 |
| vscode | - | 77.36 | - | - | 84.38 | 1.09 |

The flag K12 excess over flag K1, by bucket:
- (a) recomputation after POISON: words 0.7-3% of CPU; **variance groups** 60% of drizzle's excess (1.81 -> 2.68 G
  under measurements; 266 of 397 groups tainted, each worker re-measures), 48% of zod's, 5% of effect's. The first
  taint event of a tainted group is mostly a deep-nesting cut (relation_deep_cut(), or a CACHE_DEEP or tainted memo
  hit that came from one). By tsc's own semantics the cut depends on type-id order, i.e. the worker's history: the
  taint is correct, not conservative.
- (b) races: under 1% of instructions (T1 vs T12); adopted words' cycles drizzle 4%, zod 2.3%, the rest under 1%,
  three quarters in the first ~60 ms. The start-of-run lockstep is a dokploy-shape problem (one huge central type).
- (c) relations outside variance measurement: small. The shared relation cache doesn't pay as built
  (`TTTT_GRAPH_SHARE_RELATIONS=0`: zod 5.617 vs 5.636 G, CPU -2..-5%), although sharing definitive results is tsc's
  own semantics (a probe found 0 disagreements in ~186k duplicate puts; it would take 7-11% of K12 relation pairs,
  40% on excalidraw).
- (d) the sharing machinery itself: 43-61% of the excess (drizzle 0.72 G, zod 0.52, effect 0.75, vscode 4.27):
  ~150-300 instructions per shared probe after a private miss, per identity publication (a mutex insert), per memo
  flush.
- (e) zeroed fresh tables (memset) 0.11-0.69 G; private intern work.

**Per instruction** (best per-file check-time sum / instructions, ns): drizzle K1 0.141 plain / 0.139 flag, K12 0.193
plain / **0.314 flag**; zod 0.161 / 0.153, 0.239 / **0.381**; effect 0.163 / 0.155, 0.264 / **0.325**. A flag
instruction at K12 costs 1.2-1.6x a plain K12 one (2.0-2.4x a K1 one; plain K12 is 1.4-1.6x from all-core clocks).
At K4 the premium is 1.19x on separate cores, 1.14x on SMT siblings. Cachegrind gives the flag *fewer* LL misses than
plain (drizzle 17.1 M vs 24.2 M) and context switches are ~1000 a run, so it is neither working set nor sleeping:
inferred to be lines written by one core and read by others (shared tables, published objects, CAS'd words). (2026-10-29: not so. That cachegrind run simulated one cache for all K12 threads, where plain's twelve graphs
compete; at one thread and K8 the flag has 1.7x plain's LL misses and the per-instruction premium is already there:
"The premium, attributed" below.) Not
measured: this VM has no performance counters. Effect's critical file Schema.ts: 469 M instructions plain, 549 M flag,
117 ms plain vs 162-184 ms flag at K12. Net: drizzle's flag per-file sum equals plain's on 37% fewer instructions;
zod and effect lose.

**Verdict**: the design shares well (races < 1-4%, recomputation a few percent but variances); it doesn't make
sharing cheap. Even a perfect 1.0x work ratio gives drizzle only -20% per-file sum and effect a loss. The flag wins
only where plain recycling thrashes: dokploy and monkeytype run 2.2-2.5x faster (measurements.md "The thrashers with
the flag").

## The premium, attributed (2026-10-29, worker premium)
The question (hub_regime.md): the flag loses 11 of 13 corpora by about +15% check CPU at K8. Is that (a) extra
instructions on the flag's paths, (b) cache-coherence stalls, or (c) lost work? Measured on this box (16 vCPU EPYC
Turin, 8 cores, 1 MB L2 a core, 32 MB L3, no performance counters) at 2c389b213. Instructions: callgrind, release build
with `-g` (same code), check phase only (program_typecheck plus the worker threads). CPU: the typecheck phase's process
CPU from `--time`, best of 5 interleaved at load 1-3. Seven projects: zod (tsconfig.test), drizzle, effect, xstate,
react (the losers) and dokploy/apps/dokploy, openbot/packages/i18n (the winners). Scripts and raw data were in
/tmp/premium-cg (cpu.py, buckets.py, prof.py: deleted; the method is below).

**The method.** The flag's check CPU over plain's at K8 is (instructions flag/plain) x (CPU per instruction at ONE
thread, `--threads 1 --checkers 8`, flag/plain) x (the concurrency factor: flag's T8/T1 CPU per instruction over
plain's). At one thread no line is ever shared between cores, so the middle factor holds no coherence; what the third
holds that isn't coherence is races (work a callgrind run, which serialises threads, doesn't show). At K1 the flag's
CPU per instruction equals plain's (0.98-1.03): the K1 cost (+4-6%) is instructions only.

| project | dup removed | (a) machinery instr | (c) lost work instr | instr flag/plain | CPU/instr at T1 | concurrency | check CPU flag/plain K8 |
|---|---|---|---|---|---|---|---|
| zod | -27% | +15% | +8.5% | 0.964 | x1.14 | x1.02 | 1.12 |
| drizzle | -45% | +13% | +6.9% | 0.750 | x1.24 | x1.11 | 1.03 |
| effect | -11% | +14% | +3.3% | 1.063 | x1.12 | x0.99 | 1.19 |
| xstate | -30% | +13% | +14% | 0.972 | x1.08 | x1.12 | 1.18 |
| react | -38% | +13% | +9.0% | 0.838 | x1.00 | x1.25 | 1.05 |
| dokploy | -63% | +7% | +18% | 0.620 | x1.13 | x1.61 | 1.13 |
| i18n | -71% | +3% | +16% | 0.485 | x1.13 | x1.06 | 0.58 |

(Percentages are of plain K8's check instructions. "dup removed": plain K8's instructions beyond plain K1's, outside
the machinery functions. "machinery": overlay.c's words, the shared tables and their mutexes, the memo log, flush and
taint, the relation share, first-touch stamps, and memset, flag K8 minus plain K8 by function. "lost work": the rest
of the flag's K8 instructions beyond plain K1's, of which flag K1 already has 0-1.3%.)

What each column is:
- **(a) machinery, +13-15% of the instructions on the corpora.** Top added functions over the seven at K8 (M
  instructions): shared_table_find 1026, shared_table_insert 655, intern_shared_find 589, shared_links_get 372,
  is_deeply_nested_stamped 311 (net of is_deeply_nested_max's saving, the stamps cost about half that), graph_memo_log_put
  291, sw_finish 271, probe (the insert's) 263, shared_links_insert 242, sw_look_slow 216, pthread_mutex_lock 188,
  intern_publish 183, memset 166 (the shared tables' growth: zeroed_table() of each doubled shard), graph_count_shared_hit
  151 (a stats call made with the stats off), pthread_mutex_unlock 151, ty_stamp 147, graph_memo_flush 138, sw_claim
  131. At K1 the top ones are sw_finish, the stamps, sw_look_slow, graph_memo_hit_cold, sw_claim.
- **The CPU per instruction at one thread, x1.08-1.24, is the largest factor, and it is cache misses, not
  coherence.** Sampled time (bench/timeline at 200 us, zod `--threads 1 --checkers 8`, 6 runs a side) against
  callgrind's instructions per function: shared_table_find takes 0.66 ns an instruction and shared_links_get 0.74,
  against 0.05-0.2 for the checker's own code; the two are +105 ms of the flag's +87 ms (the rest nets out). Cachegrind
  at the same setting: LL read misses 2.30M plain, 3.93M flag; +0.63M in shared_table_find and +0.18M in
  shared_table_insert, the other half spread over the relation and member code (ty_erased_signature, is_weak_type,
  signatures_related...): reading types another checker made, which plain would have remade warm in its own arena. So
  sharing trades cheap warm recomputation for cold reads: each shared probe is a random line of a table far bigger
  than L2.
- **(c) lost work, +3-18%.** At K8 the flag leaves almost no type duplication (types made K8/K1: zod 1.01, drizzle
  1.00, effect 1.00, xstate 1.02-1.06, react 1.04, i18n 1.03; plain 1.44-4.77), but variance groups are re-measured by
  each worker (`TTTT_VARIANCE_CENSUS` measurements K1 -> K8: zod 287 -> 1086, drizzle 666 -> 1336, effect 536 -> 937,
  xstate 165 -> 887: the tainted groups of "The perf accounting"), and POISONed words are recomputed privately
  (dokploy 47k computations).
- **Races, natively.** callgrind serialises threads; natively at T8 (`TTTT_GRAPH_STATS`, 3 runs) words adopted
  (computed in full by a worker that lost the race): zod 6-9k of 782k published, drizzle 5-9k, effect 11-33, xstate
  20k of 154k, react 11-14k of 112k, i18n 11-12k of 105k, **dokploy 564-584k of 1.04M** (with relation pairs +40% and
  types +40% over its T1 run). That is dokploy's x1.61: every worker builds the tRPC core at once (hub_regime.md).
- **(b) coherence is bounded by the concurrency factor less the races: x0.99-1.02 on zod and effect, up to x1.11 on
  drizzle and xstate.** It is the smallest of the three on every project. The earlier conclusion that the per-instruction
  premium was cross-core traffic (this note's "perf accounting", sharing_design.md) is not supported: the premium is
  there at one thread.

**Verdict.** On the losing corpora the +12-19% is about half (a) machinery instructions and half the cache misses of
reading a shared graph (the shared-table probes and other checkers' objects), with lost work (variance re-measurement)
next and coherence last. Neither of the large two is a layout bug: they are what replacing a checker's own warm copy
with one shared copy costs, per lookup. The flag wins where the duplication it removes is large (dokploy, i18n,
drizzle: dup -45..-71%) and loses where it is small (effect -11%, zod -27%).

**Cuts built (75f79d714):** an identity found in the shared table and a node link found in the shared links are kept in
the checker's own tables (85-93% of shared identity hits repeated a key the same checker had found: zod's shared hits
966k -> 240k); scratch kinds and the links' variances don't probe the shared table (15% of probes, all misses);
KEY_PROP_MAPPED_TYPE (keyed by a Prop just made) doesn't probe. Output identical: quickgate --vs-main -k; sgcheck -k 4,8,16 both deals, 3 runs on the default list:
6 of 1044 runs differ, all known residues at their rates (drizzle C 3 at K16 def, mikro-orm seeder 2 at K16 rr) or
present on main too (excalidraw 1, bugs/shared_graph.md); TSan clean on drizzle, effect, excalidraw, zod at T4/T8; the
-DTTTT_GRAPH_CHECKS build reports main's one site (kind 10) on the same four; the effect is small, because repeated hits were on
warm lines and the probe time left moves to the next touch of the same lines: flag K8 check CPU -1..-3% at one thread,
-0..-3% at T8 (best of 7 at load 6-8; zod 0.952 -> 0.952 s, effect 1.654 -> 1.630, xstate 0.531 -> 0.522, i18n 0.971 ->
0.946), instructions -1% (zod, effect). After them, flag/plain check CPU at K8 T8: zod 1.11, effect 1.17, xstate 1.14,
dokploy 1.13, drizzle 0.94, react 0.96, i18n 0.59. `TTTT_GRAPH_STATS` now has a shared-misses column per kind (the
probes that found nothing: zod K8 1.18M against 232k hits after the cuts, KEY_ANONYMOUS_INSTANCE 179k of them).
**Measured and not kept:** the shared tables at load 1/2 (+1-3% CPU); the shared tables' slot arrays on 2 MB pages
(-2..-3% check CPU at T1, A/B by env on one binary: the TLB), not kept because huge pages were removed on 2026-10-28
for their compaction-stall risk (kernel_time.md). If the laptop shows dTLB misses in the shared probes, it is the one
layout change worth reopening.

**What would cut the premium further** (not built): fewer shared lookups per unit of work, not cheaper ones: identity
creation probes (KEY_ANONYMOUS_INSTANCE alone 0.74M misses on four projects) could be skipped where the key holds an
object this checker just made (as KEY_PROP_MAPPED_TYPE now), the first-touch stamps replaced by a content order
(done: the walk order, b2b3cdcd2, -0.3..-1.9% of the flag's K8 instructions), the variance groups shared
(stage 3). None changes the verdict above: the flag pays a lookup's miss for each piece of shared state a checker
first reads, and wins only where that state would otherwise be rebuilt several times.

**For the laptop** (splits (b) from (a)+(c) with counters; `lscpu -e` for one hardware thread per big core): for P in
zod, effect, drizzle, xstate, B in plain and `TTTT_SHARED_GRAPH=1`, T in 1 and 4,

    perf stat -r 5 -x, -e cycles:u,instructions:u,L1-dcache-load-misses,dTLB-load-misses,ls_dmnd_fills_from_sys.local_ccx,ls_dmnd_fills_from_sys.near_cache,ls_dmnd_fills_from_sys.dram_io_all \
      taskset -c BIGCORES env $B TTTT_SYNC_EXIT=1 $BIN --threads $T --checkers 4 --noEmit -p $P

and `perf c2c record --all-user -- env TTTT_SHARED_GRAPH=1 $BIN --threads 4 --checkers 4 --noEmit -p zod` then
`perf c2c report --stdio` (HITM lines and their symbols). Read: cycles/instruction flag/plain at T1 is the locality
cost (expected x1.1-1.2 here); its T4/T1 growth over plain's is (b) plus races; near_cache fills (another CCX's cache)
growing with T only under the flag are the cross-core traffic; dTLB misses in shared_table_find say whether the 2 MB
pages would pay there.

## Variance frames (2026-10-29, worker vargroup, f10fa5e30, e0c8b01e6)
The census (`scripts/variancecensus.py --flag`, a `-DTTTT_TAINT_SITES` build: VC lines with every event's site, `:i`
when internal to the outermost frame, `:kN` a tainted memo hit's KEY_ kind; `TTTT_TAINT_LOG=1` prints every event)
at K8 T8 on main a55802b5f: measurements K1 -> flag K8 zod 287 -> 1070, xstate 165 -> 862, drizzle 666 -> 1366,
effect 541 -> 904, dokploy/apps/dokploy 458 -> 2901, openbot i18n 13 -> 23. The first non-internal event of a tainted
outermost group was mostly propagation: CACHE_DEEP hits 41-54%, tainted memo hits (KEY_VARIANCES / KEY_ALIAS_VARIANCES:
earlier tainted groups' private values) 20-53%, poisoned slots 7-27%; origins were deep cuts 3-8% and enclosing-claim
reads 1-3% (18-50% of the types on drizzle and xstate: the restart's set-aside claims). A group was all or nothing, so
one tainted nested measurement or one enclosing read poisoned every subject it measured.

Built: per-frame cleanliness (commit f10fa5e30 has the uniqueness argument in full: a clean frame read nothing outside
its own span but published clean values, hash-consed identities and event-free relation entries, so it is a function
of its subject and the published state), DEEP pairs stamped with the frame clock (`DeepStamps`), poisoned slots
re-claimed and replaced by a clean frame, restarts keeping the frames they set aside on the stack (a base index), nested
clean frames published at their own end. Measurements K8 (median of 3, T8): zod 1126 -> 452, xstate 794 -> 527,
drizzle 1394 -> 1033, effect 939 -> 576, dokploy 2618 -> 1244, i18n 23 -> 14; with ckey's `TTTT_NESTING_ORDER=walk`
merged on top: 433, 511, 897, 567, 1171, 13.

**The work barely moves**: the measurements removed were cheap (clean nested subjects every worker re-measured
inside its own tainted outer group). Callgrind, check phase, flag K8 (T8 serialised), claim_and_measure inclusive at the
outermost level, main -> this: zod 0.735 -> 0.730 G (14% of 5.2 G; plain K1 0.289), xstate 0.550 -> 0.545 (plain K1
0.132), drizzle 2.557 -> 2.534 (plain K1 1.618), effect 0.119 -> 0.097, i18n 0.006 -> 0.005, dokploy 1.08 -> 1.36 (a
racy run, total 20.5 -> 24.5 G, noise of the serialised schedule). With walk: ckey alone -> ckey + this: drizzle
2.67 -> 2.30 G (total 6.50 -> 6.19), zod 0.727 -> 0.695, xstate 0.530 -> 0.517, effect 0.121 -> 0.100. Check CPU at
K8 T8 best of 5 interleaved (load 12-25): within +-3%. What costs is the tainted outermost groups (80-98% of the types
made in outermost measurements), and what taints them now is the flag's private-value cascade, not anything
variance-specific: xstate at K8 reads 54k private Overlay values and 17k CACHE_DEEP pairs concluded outside any frame;
ActionFunction/StateNode first read a tainted KEY_SIMPLIFIED or KEY_VARIANCES. Sharing them needs fewer POISONed words
upstream (or bun's rollback), not a variance rule.

**What it did buy: determinism.** Residue C is closed by the walk order (b2b3cdcd2); the frames alone, before it,
already removed most of that schedule dependence (fewer poisoned variances, fewer private histories): sgcheck at
K8/K16, 8 runs per (K, deal), drizzle's C 12/16 differing runs with the scheduler's deal -> 1/16, mikro-orm seeder at
K16 rr 8/8 -> 2/8. Both together (rebased on 06f16128c): the default list at K4/8/16, both deals, 2 runs, 0 of 696 differ.
DeepStamps and the walk order: under walk (and content) a deep cut is no taint event, so the pair it concludes is
cached CACHE_DEEP and only its hits are events; a hit inside the frame that concluded it is internal (the cut was
decided by that frame's own stacks). Under id-like orders the cut itself is an external event, so the frame is
tainted anyway and the stamps change nothing there.
Gates: quickgate --vs-main -k (plain unchanged, +0.03% instructions), sgcheck default list K4/8/16 both deals x2
(3 of 696 differ: drizzle C 1 at K16 def, mikro-orm seeder 2 at K16 rr, the known residues),
fingerprint oracle at K8 (0 variance differences on drizzle, zod, effect, xstate, excalidraw; the oracle's mirror of
the Variances layout was off by a byte before, now ty_variances_data()), TSan clean at T4/T8 on the five,
-DTTTT_GRAPH_CHECKS main's one site.

## The wall loss beyond the CPU loss, attributed (2026-10-29, worker sgwall)
The laptop (measurements.md "The 12-core box at 15f15fdeb") had vscode's flag wall +10..12% on +4..5% user, effect
+4/+14% wall. On this box (16 vCPU, load 10-35 from other workers all day: no quiet number exists) with the timeline
(bench/timeline, `TIMELINE_ALL=1`, now `timeline.py --blocked`) and `TTTT_FILE_TIMES=1`:

- **vscode: the checkers slept in the kernel's mm lock.** Check phase at K16, the checker threads' blocked time
  (span less run less run queue, summed): plain 0.16-1.3 s, flag 1.2-13.5 s (load 23-36). The D-state samples were
  mprotect() under malloc (memo_at, declare_symbol_flags, add_member_entry, binder_new_symbol: any malloc), the S ones
  shard-mutex waits behind holders stuck there. strace: the flag made 11.3k mprotect() calls, plain 41. The graph's
  memory (shared tables' slot arrays at every doubling, memo pages) and the first-touch stamp chunks (since deleted
  with the stamps, b2b3cdcd2) were malloc'd on the checker threads, past what the front end had freed, and a non-main glibc arena grows its heap a page at a time,
  an mprotect() each, which takes the mm lock for writing while every other thread's faults (and the arenas'
  MADV_POPULATE_WRITE ranges, which hold it for reading) queue. Fixed by ZeroPages (ba727aa29, 246925f4f, 033070c1d):
  that memory now comes from mappings of its own, bumped under a lock, faulted in 256 KB ahead, freed with its owner:
  39-44 mprotect() calls, blocked time at plain's level (0.7-0.9 s against 0.6-1.3 at load 24), check wall 2.4 -> 1.64-1.74 s
  against plain's 1.50-1.77, check CPU 19.5 -> 18.9 s against 17.5 (+8%), flag instructions -1.2% (no memset of the
  doubled tables). The mechanism doesn't need load: a writer waiting on the mm lock stalls every new fault behind it.
- **Shard locks: rare contention, long waits under load.** `TTTT_GRAPH_STATS` counts the inserts that found their
  shard locked: vscode K16 0.07% of 5.1M, effect K16 0.2% of 1.1M. Holds are short (99.8% under 1 us, TSC histogram),
  but each contended insert waited ~50-100 us: inserts are ~12% of the flag's check CPU, so a preempted thread is often
  a holder, and at ~400k inserts a second every thread reaches its shard within milliseconds (lock waits come in bursts,
  20-30% of a 20 ms window). 256 shards (bf7b2a36e) cut the contended inserts 2.5x and effect K16's lock-wait samples
  from 5.3-6.7% to 2.5-4.5%. A spinning mutex, a spin-yield lock and probing before the lock were tried and not kept
  (bf7b2a36e's message). What would remove it: a lock-free insert (CAS the slot; a doubling marks the empty slots moved
  so a racing insert retries in the new array); only worth it if the laptop at 24/24 shows lock waits.
- **Not the deal, not a serial part.** The checkers' finishing spread (the idle tail) is the same in both modes (vscode
  88 ms plain, 107 flag; effect 11 / 14), the main thread's serial parts are equal (checkers start 2-4 ms into the
  phase, the join ~10 ms), vscode doesn't recycle the graph (no `recycle due` line) and doesn't rerun. vscode's tail is
  a cost-proxy problem of both modes: its costliest file, mapSessionEvents.test.ts (274 ms check), is dealt by its
  bytes and starts ~840 ms into a 1.1 s phase.
- **effect is CPU on the critical path.** No mprotect storm there (the fix changes nothing); flag check CPU +17-20%
  at K8, its costliest files slower by more (Schema.ts 95 -> 124 ms CPU, stream.ts 72 -> 95): the per-read premium
  of "The premium, attributed" on the files the span waits for, plus the shard-lock waits above at K16.

**For the laptop** (confirms the fix where the loss was measured): `B=$(scripts/abbuild.sh becddb01b --fast)` (main
before the series), `A=$(scripts/abbuild.sh main --fast)` (with the series landed), then for MODE in nothing and `--threads 24 --checkers 24`:
`TTTT_SHARED_GRAPH=1 scripts/abbench.sh -n 5 $B $A vscode effect zod drizzle $DOKPLOY -- $MODE` and once plain
(`scripts/abbench.sh -n 5 $B $B vscode effect -- $MODE`: the plain path is unchanged). Expected: vscode's flag wall
from +10..12% to about its user loss (+4..5%). Whether shard locks still matter at 24/24: `strace -f -c -e
trace=mprotect` (flag ~40 calls, as plain), and the timeline of a release build, `LD_PRELOAD=timeline.so
TIMELINE_ALL=1 ... --time` then `timeline.py OUT --phase typecheck --blocked`: lock waits (`__lll_lock_wait <
shared_*_insert`) above ~2% of the samples there make the lock-free insert worth building.

## The ceiling, measured (2026-10-22)
The most a shared graph could take off a checker: the share of its check instructions that only repeat what one
checker would do once, (W(K) - W(1)) / W(K), with W the instructions inside check_share (callgrind per thread,
`--threads K --checkers K`, release build, worker "dup"; notes/duplication.md has the method and the census):
| corpus | K12 | K24 |
|---|---|---|
| vscode | 8% | 12% |
| effect | 13% | 18% |
| excalidraw | 29% | 38% |
| drizzle | 54% | 63% |
This is the upper bound a shared graph could take off the critical path, before its own 1.2-1.6x per-instruction
overhead: on vscode (~12% at K24) the overhead alone exceeds it; drizzle-like projects (their own generic code
instantiated per checker) are the only case for reopening. Counting types overstates it: 44% / 52% of a vscode
checker's types are also made by another, but they are cheap ones (literals, anonymous types); types every checker
makes are only 3-5% of a checker's (excalidraw 10-13%). K12 -> K24 on vscode adds only +4.3% instructions (77.2 ->
80.5 G against 70.9 G at K1): the laptop's +12 CPU s for K24 is SMT accounting (24 threads on 12 cores each counting
full time), not duplicated work.

## Before reopening it
1. **Hardware counters on the laptop** (the human's 12-core box has them): `perf stat -e
   cycles,instructions,cache-misses,LLC-load-misses,...` and `perf c2c record` on drizzle K12, flag against plain.
   Candidates if it is cross-core traffic: publication writes into lines other cores read (words inside
   TyObjectState and Signatures beside hot read-only fields; shared-table slots written beside probed ones), the
   private-miss shared probe on every first use, the per-chunk type directory. Layout changes are low risk.
2. **A design for variance groups cut by deep nesting** (48-60% of drizzle's and zod's excess): graph-wide stamps
   (the first toucher's order: nondeterministic, 4676 of 1.36 M dokploy decisions would change; high risk), or
   measuring a tainted group once and publishing it keyed by the stamps of the types it cut on (medium-high risk).
   Without them drizzle and zod K12 work would be ~1.12x.
3. **Cheaper machinery** (halving (d) takes drizzle/zod 1.29x -> ~1.2x): cache a shared identity in the private
   table on first hit, lock-free insert-if-absent for identities, batched memo flushes; drop or gate the shared
   relation cache. Smaller flag-only items left: a single look-and-claim call at the members.c sites (~2% of the
   flag's K1 cost), retiring shadowed workers at once (-0.2..-0.6%), a narrower outer-inference taint event
   (function_return_type()'s keep_outer, hacks.md), object literals' and JS function expressions' identity keyed
   syntactically.
4. The residues C, V, W, and both full oracle runs identical plain and flagged, before any default flip.
5. Policy ideas that were never built: **adaptive** (plain by default, the flag when plain thrashes: a checker
   recycling after fewer than CHECKER_MEMORY_BACKOFF_FILES files repeatedly, or a first segment's warm-up over its
   budget share; switching is a recycle into a fresh shared graph at a file boundary); the graph budget (800 MB, at
   dokploy's working set): don't recycle when under a quarter of the files remain, or double after a short epoch.
6. **Sharing clean variances alone (bun's claim/wait) is closed** (2026-10-22, worker "var", `scripts/variancecensus.py`
   and callgrind at `--threads 1`, plain build). Variance measurement is 87% / 68% / 9% / 3.4% of the K12 excess over K1
   on drizzle / zod / effect / excalidraw (inclusive instructions in claim_and_measure: drizzle 1.58 -> 5.13 G of 5.44
   -> 9.50 G), but 98% / 94% of it on drizzle / zod is in *tainted* outermost measurements (95% with deep-nesting cuts of
   their own); clean duplicates are 0.04-0.07 G, under 1% of K12 instructions on every corpus. And taint is history's,
   not the symbol's: subjects clean in some checkers and tainted in others exist on three of the four corpora (drizzle
   11, zod 3, effect 2), with differing values (drizzle's MakeColumnConfig clean `010101` in 8 checkers, `010901` in one,
   tainted by an earlier deep-cut relation; PgArrayBuilder `0103` at K1, `0109` in two K12 checkers), so first clean
   publisher wins is not K-independent by construction. Clean values never disagreed (~3600). What remains is tsc's
   semantics: each checker measures the dialects' big generic closures (SingleStoreSession, PgSelectBuilder...: 200-270
   M instructions and 500-680 deep cuts each, in every checker that enters them first; tsgo --singleThreaded makes
   comparable types there, 44k against tttt's 30k), and which subject of a closure is entered first only moves the cost
   between subjects.

## What bun does differently (read from its source at 6bb5dc19, 2026-10-20)
One content-keyed hash-consed store for every type, signature and mapper, created into directly with global ids from
per-thread id ranges (a LOCAL bit for records that must not escape); every tsgo "link" in shared tables behind a
task-local buffer, committed in numbered commits under one mutex, first entry per key wins; union order and
isDeeplyNestedType's id test replaced by a content order (costing tsgo parity in edge cases, accepted); determinism
from a large body of program-order rules, discard-and-recheck of files and a serial last round (where its known bugs
are). Checkers are recycled after 1/128 of the total cost. Its CPU at default threads is 1.6-3.0x its single-thread
time (vscode 43 CPU s vs 14 single): sharing costs it too, it just has more cores busy.

## Tools left behind
`TTTT_GRAPH_STATS=1` (per kind: shared hits, shared misses, published...; variance slots published, adopted,
unpoisoned...), `TTTT_TAINT_LOG=1` in a `-DTTTT_TAINT_SITES` build (every taint event's site as it happens), `TTTT_GRAPH_NO_CACHE=1` (shared hits not
kept in the checker's own tables: the A/B of 75f79d714), `TTTT_GRAPH_WORK=1` (cycles per word computation by outcome), `TTTT_GRAPH_LEAD=1` /
`TTTT_GRAPH_NO_LEAD=1`, `TTTT_GRAPH_SHARE_RELATIONS=0`, `TTTT_GRAPH_BUDGET_MB` (graph-wide recycle, default 800),
`TTTT_DEBUG_PUBLISH` (debug build: a publication referencing a private range aborts), `scripts/sgcheck.py` (the gate:
flagged against plain K1 at K 1/4/8/16 with two deals), a `-DTTTT_GRAPH_CHECKS` build with `TTTT_GRAPH_CHECKS=1` (identities published or
found after a taint event in their constructor: bugs/shared_graph.md), `scripts/sharedgraph/` (20-run
tables, seeds, fingerprint-at-publish, TSan, the recheck), `scripts/taintstats.sh`, `scripts/kdiff.py` with the flag.
